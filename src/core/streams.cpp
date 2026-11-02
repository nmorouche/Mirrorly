// Mirrorly - AirPlay media streams
#include "streams.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "native_media.h"

namespace mn {

static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

// ============================================================== NTP

bool NtpClient::start(uint32_t remote_ip_be, uint16_t remote_port) {
    remoteIp_ = remote_ip_be;
    remotePort_ = remote_port;
    sock_ = udp_bind(0, &port_);
    if (sock_ == MN_INVALID_SOCKET) return false;
    running_ = true;
    thread_ = std::thread(&NtpClient::loop, this);
    return true;
}

void NtpClient::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    sock_close(sock_);
    sock_ = MN_INVALID_SOCKET;
}

void NtpClient::loop() {
    uint64_t nextSend = 0;
    uint8_t buf[128];
    while (running_) {
        uint64_t now = now_ms();
        if (remotePort_ && now >= nextSend) {
            uint8_t req[32] = {0x80, 0xd2, 0x00, 0x07};
            uint64_t t = ntp_now();
            for (int k = 0; k < 8; k++) req[24 + k] = (uint8_t)(t >> (56 - 8 * k));
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_addr.s_addr = remoteIp_;
            to.sin_port = htons(remotePort_);
            sendto(sock_, (const char*)req, sizeof(req), 0, (sockaddr*)&to, sizeof(to));
            nextSend = now + 3000;
        }
        if (sock_wait_readable(sock_, 200) > 0) {
            recvfrom(sock_, (char*)buf, sizeof(buf), 0, nullptr, nullptr);  // replies are not needed
        }
    }
}

// ============================================================== Mirroring (video)

bool MirrorStream::start(const uint8_t aeskey[16], uint64_t streamConnectionId) {
    // key/iv = SHA512("AirPlayStreamKey<id>" || aeskey)[0:16], SHA512("AirPlayStreamIV<id>" || aeskey)[0:16]
    char skey[64], siv[64];
    snprintf(skey, sizeof(skey), "AirPlayStreamKey%llu", (unsigned long long)streamConnectionId);
    snprintf(siv, sizeof(siv), "AirPlayStreamIV%llu", (unsigned long long)streamConnectionId);
    uint8_t hk[64], hv[64];
    sha512_2((const uint8_t*)skey, strlen(skey), aeskey, 16, hk);
    sha512_2((const uint8_t*)siv, strlen(siv), aeskey, 16, hv);
    ctr_.init(hk, hv);

    listenSock_ = tcp_listen(0, &port_);
    if (listenSock_ == MN_INVALID_SOCKET) {
        LOGE("mirror: could not open data port");
        return false;
    }
    running_ = true;
    thread_ = std::thread(&MirrorStream::loop, this);
    LOGI("mirror: listening for video on port %u", port_);
    return true;
}

void MirrorStream::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    sock_close(listenSock_);
    listenSock_ = MN_INVALID_SOCKET;
}

void MirrorStream::loop() {
    decoder_ = mn_vdec_create(4);
    if (!decoder_) LOGE("mirror: could not create H.264 decoder");
    sock_t client = MN_INVALID_SOCKET;
    uint8_t hdr[128];
    std::vector<uint8_t> payload;
    bool loggedFirst = false;
    while (running_) {
        if (client == MN_INVALID_SOCKET) {
            if (sock_wait_readable(listenSock_, 200) <= 0) continue;
            client = accept(listenSock_, nullptr, nullptr);
            if (client == MN_INVALID_SOCKET) continue;
            sock_set_nodelay(client);
            sock_set_keepalive(client);
            int rcvbuf = 4 << 20;
            setsockopt(client, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));
            LOGI("mirror: video connection accepted");
            continue;
        }
        if (!sock_recv_all(client, hdr, 128, &running_)) {
            if (running_) LOGI("mirror: video connection closed by sender");
            sock_close(client);
            client = MN_INVALID_SOCKET;
            continue;
        }
        uint32_t size = le32(hdr);
        if (size > 64u * 1024 * 1024) {
            LOGE("mirror: invalid payload size %u", size);
            sock_close(client);
            client = MN_INVALID_SOCKET;
            continue;
        }
        payload.resize(size);
        if (size && !sock_recv_all(client, payload.data(), size, &running_)) {
            sock_close(client);
            client = MN_INVALID_SOCKET;
            continue;
        }
        lastData_ = now_ms();
        uint8_t type = hdr[4];
        uint8_t option = hdr[6];
        if (type == 0x00) {
            if (!loggedFirst) {
                LOGI("mirror: first video frame (%u bytes)", size);
                loggedFirst = true;
            }
            handleVideo(payload);
        } else if (type == 0x01) {
            // unencrypted avcC: [5] version.. [6..7] sps len, sps, [n] count, pps len, pps
            if (option == 0x56 || option == 0x5e) {
                if (!paused_) {
                    paused_ = true;
                    if (rx_->listener()) rx_->listener()->onVideoPaused(true);
                }
            } else if (paused_) {
                paused_ = false;
                if (rx_->listener()) rx_->listener()->onVideoPaused(false);
            }
            if (size < 11) {
                if (size == 0) LOGW("mirror: sender offered a non-H.264 codec (unsupported)");
                continue;
            }
            if (size >= 8 && memcmp(payload.data() + 4, "hvc1", 4) == 0) {
                LOGW("mirror: HEVC stream is not supported");
                continue;
            }
            size_t spsLen = be16(&payload[6]);
            if (8 + spsLen + 3 > size) continue;
            size_t ppsLen = be16(&payload[8 + spsLen + 1]);
            if (8 + spsLen + 3 + ppsLen > size) continue;
            const uint8_t sc[4] = {0, 0, 0, 1};
            spsPps_.clear();
            spsPps_.insert(spsPps_.end(), sc, sc + 4);
            spsPps_.insert(spsPps_.end(), payload.begin() + 8, payload.begin() + 8 + spsLen);
            spsPps_.insert(spsPps_.end(), sc, sc + 4);
            spsPps_.insert(spsPps_.end(), payload.begin() + 8 + spsLen + 3, payload.begin() + 8 + spsLen + 3 + ppsLen);
            float w, h;
            uint32_t wi = le32(&hdr[56]), hi = le32(&hdr[60]);
            memcpy(&w, &wi, 4);
            memcpy(&h, &hi, 4);
            LOGI("mirror: codec config received, stream %dx%d", (int)w, (int)h);
        }
        // type 2 (heartbeat) and 5 (stream report) need no handling
    }
    sock_close(client);
    if (decoder_) {
        mn_vdec_destroy(decoder_);
        decoder_ = nullptr;
    }
}

void MirrorStream::handleVideo(std::vector<uint8_t>& payload) {
    ctr_.process(payload.data(), payload.size());
    // AVCC (4-byte big-endian lengths) -> Annex-B start codes
    size_t pos = 0;
    bool valid = true;
    while (pos + 4 <= payload.size()) {
        uint32_t nl = be32(&payload[pos]);
        if (nl == 0 || nl > payload.size() - pos - 4) { valid = false; break; }
        payload[pos] = 0; payload[pos + 1] = 0; payload[pos + 2] = 0; payload[pos + 3] = 1;
        if (payload[pos + 4] & 0x80) { valid = false; break; }
        pos += 4 + nl;
    }
    if (pos != payload.size()) valid = false;
    if (!valid) {
        static int warned = 0;
        if (warned++ < 5) LOGW("mirror: could not decrypt video frame (key mismatch?)");
        return;
    }
    if (!spsPps_.empty()) {
        std::vector<uint8_t> combined;
        combined.reserve(spsPps_.size() + payload.size());
        combined.insert(combined.end(), spsPps_.begin(), spsPps_.end());
        combined.insert(combined.end(), payload.begin(), payload.end());
        spsPps_.clear();
        decodeAndPublish(combined.data(), combined.size());
    } else {
        decodeAndPublish(payload.data(), payload.size());
    }
}

void MirrorStream::decodeAndPublish(const uint8_t* annexb, size_t len) {
    if (!decoder_) return;
    int n = mn_vdec_decode(decoder_, annexb, (int)len);
    if (n <= 0) return;
    int w = 0, h = 0;
    if (mn_vdec_frame_size(decoder_, &w, &h) != 0 || w <= 0 || h <= 0) return;
    VideoFrame& f = rx_->frames().beginWrite();
    f.width = w;
    f.height = h;
    f.stride = w * 4;
    f.pixels.resize((size_t)f.stride * h);
    f.seq = ++frameSeq_;
    if (mn_vdec_frame_bgra(decoder_, f.pixels.data(), f.stride, w, h) != 0) return;
    rx_->frames().endWrite();
    if (rx_->listener()) rx_->listener()->onVideoFrame();
}

// ============================================================== Audio (RTP)

bool AudioStream::start(const uint8_t aeskey[16], const uint8_t aesiv[16], int ct, uint32_t /*remote_ip_be*/,
                        uint16_t /*remoteControlPort*/) {
    memcpy(key_, aeskey, 16);
    memcpy(iv_, aesiv, 16);
    ct_ = ct;
    dataSock_ = udp_bind(0, &dataPort_);
    controlSock_ = udp_bind(0, &controlPort_);
    if (dataSock_ == MN_INVALID_SOCKET || controlSock_ == MN_INVALID_SOCKET) {
        LOGE("audio: could not open UDP ports");
        return false;
    }
    running_ = true;
    thread_ = std::thread(&AudioStream::loop, this);
    LOGI("audio: ct=%d (%s), data port %u, control port %u", ct,
         ct == 2 ? "ALAC" : ct == 4 ? "AAC-LC" : ct == 8 ? "AAC-ELD" : "unknown", dataPort_, controlPort_);
    return true;
}

void AudioStream::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    sock_close(dataSock_);
    sock_close(controlSock_);
    dataSock_ = controlSock_ = MN_INVALID_SOCKET;
}

void AudioStream::flush() {
    std::lock_guard<std::mutex> lk(flushMutex_);
    flushRequested_ = true;
}

void AudioStream::loop() {
    decoder_ = mn_adec_create(ct_);
    if (!decoder_) LOGE("audio: no decoder for ct=%d", ct_);
    pcm_.resize(8192 * 2);
    std::vector<uint8_t> buf(4096);
    while (running_) {
        {
            std::lock_guard<std::mutex> lk(flushMutex_);
            if (flushRequested_) {
                flushRequested_ = false;
                haveExpected_ = false;
                pending_.clear();
                if (decoder_) mn_adec_flush(decoder_);
                if (rx_->listener()) rx_->listener()->onAudioFlush();
            }
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(dataSock_, &rfds);
        FD_SET(controlSock_, &rfds);
        timeval tv{0, 100000};
        int maxfd = (int)std::max(dataSock_, controlSock_);
        int r = select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
        if (r <= 0) continue;
        if (FD_ISSET(dataSock_, &rfds)) {
            int n = recvfrom(dataSock_, (char*)buf.data(), (int)buf.size(), 0, nullptr, nullptr);
            if (n > 0) onRtp(buf.data(), (size_t)n);
        }
        if (FD_ISSET(controlSock_, &rfds)) {
            int n = recvfrom(controlSock_, (char*)buf.data(), (int)buf.size(), 0, nullptr, nullptr);
            if (n >= 16 && (buf[1] & 0x7f) == 0x56) onRtp(buf.data() + 4, (size_t)n - 4);  // retransmitted packet
            // 0x54 = sync packets: not needed for real-time playback
        }
    }
    if (decoder_) {
        mn_adec_destroy(decoder_);
        decoder_ = nullptr;
    }
}

void AudioStream::onRtp(const uint8_t* pkt, size_t len) {
    if (len < 12) return;
    if (ct_ == 2 && len == 44) return;  // ALAC format-only packets
    uint16_t seq = be16(pkt + 2);
    std::vector<uint8_t> payload;
    static const uint8_t noData[4] = {0x00, 0x68, 0x34, 0x00};
    if (len > 12 && !(len == 16 && memcmp(pkt + 12, noData, 4) == 0)) payload.assign(pkt + 12, pkt + len);

    if (!haveExpected_) {
        haveExpected_ = true;
        expected_ = seq;
    }
    int16_t d = (int16_t)(seq - expected_);
    if (d < 0) return;  // duplicate (AAC-ELD packets are sent redundantly) or too late
    for (auto& p : pending_)
        if (p.first == seq) return;
    pending_.emplace_back(seq, std::move(payload));

    auto drain = [&]() {
        bool progressed = true;
        while (progressed) {
            progressed = false;
            for (size_t k = 0; k < pending_.size(); k++) {
                if (pending_[k].first == expected_) {
                    if (!pending_[k].second.empty()) process(pending_[k].second);
                    pending_.erase(pending_.begin() + k);
                    expected_++;
                    progressed = true;
                    break;
                }
            }
        }
    };
    drain();
    if (pending_.size() > 8) {
        // a packet is lost: skip ahead to the oldest packet we have
        int16_t best = 32767;
        for (auto& p : pending_) best = std::min<int16_t>(best, (int16_t)(p.first - expected_));
        expected_ = (uint16_t)(expected_ + best);
        drain();
    }
}

void AudioStream::process(const std::vector<uint8_t>& payload) {
    if (!decoder_) return;
    std::vector<uint8_t> data(payload);
    size_t enc = data.size() / 16 * 16;
    if (enc) aes_cbc_decrypt(key_, iv_, data.data(), enc);
    int frames = mn_adec_decode(decoder_, data.data(), (int)data.size(), pcm_.data(), (int)(pcm_.size() / 2));
    if (frames > 0 && rx_->listener()) rx_->listener()->onAudioPcm(pcm_.data(), frames);
}

// ============================================================== Session

void Session::stopMirror() {
    std::unique_ptr<MirrorStream> m;
    {
        std::lock_guard<std::mutex> lk(mutex);
        m = std::move(mirror);
    }
    if (m) {
        m->stop();
        if (rx_->listener()) rx_->listener()->onMirrorStop();
    }
}

void Session::stopAudio() {
    std::unique_ptr<AudioStream> a;
    {
        std::lock_guard<std::mutex> lk(mutex);
        a = std::move(audio);
    }
    if (a) {
        a->stop();
        if (rx_->listener()) rx_->listener()->onAudioStop();
    }
}

void Session::shutdown() {
    stopMirror();
    stopAudio();
    std::unique_ptr<NtpClient> n;
    {
        std::lock_guard<std::mutex> lk(mutex);
        n = std::move(ntp);
    }
    if (n) n->stop();
}

}  // namespace mn
