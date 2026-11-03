// Mirrorly - wired Android mirroring (see android_mirror.h)
#include "android_mirror.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

#include "log.h"
#include "streams.h"

namespace mn {

static const char* kRemoteJar = "/data/local/tmp/mirrorly-server.jar";
static const uint32_t kCodecH264 = 0x68323634;  // "h264"
static const uint32_t kCodecRaw = 0x00726177;   // "raw"
static const uint64_t kFlagConfig = 1ull << 62;

static uint32_t rd32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t rd64be(const uint8_t* p) { return ((uint64_t)rd32be(p) << 32) | rd32be(p + 4); }

// A free TCP port on 127.0.0.1 for the adb tunnel.
static uint16_t freeLocalPort() {
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == MN_INVALID_SOCKET) return 0;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x7F000001);
    uint16_t port = 0;
    if (bind(s, (const sockaddr*)&a, sizeof(a)) == 0) {
        socklen_t l = sizeof(a);
        if (getsockname(s, (sockaddr*)&a, &l) == 0) port = ntohs(a.sin_port);
    }
    sock_close(s);
    return port;
}

AndroidMirror::AndroidMirror(Receiver& rx) : rx_(rx) {
    rx_.setPreemptHook([this]() {
        // an AirPlay sender takes over the screen
        if (sessionActive_) {
            LOGI("android: AirPlay sender took over, stopping USB mirroring");
            stopSession(true);
        }
    });
}

AndroidMirror::~AndroidMirror() {
    stop();
    rx_.setPreemptHook(nullptr);
}

bool AndroidMirror::start(const AndroidConfig& cfg, AndroidStatusListener* sl) {
    if (running_) return true;
    {
        std::lock_guard<std::mutex> lk(m_);
        cfg_ = cfg;
    }
    statusListener_ = sl;
    running_ = true;
    watchThread_ = std::thread(&AndroidMirror::watchLoop, this);
    return true;
}

void AndroidMirror::stop() {
    if (!running_ && !watchThread_.joinable()) return;
    running_ = false;
    if (watchThread_.joinable()) watchThread_.join();
    browser_.stop();
    stopSession(false);
    if (sessionThread_.joinable()) sessionThread_.join();
    sessionDone_ = false;
}

void AndroidMirror::updateConfig(const AndroidConfig& cfg) {
    bool disable;
    {
        std::lock_guard<std::mutex> lk(m_);
        bool wifi = isWireless(sessSerial_);
        disable = (cfg_.enabled && !cfg.enabled) || (!sessSerial_.empty() && (wifi ? !cfg.wireless : !cfg.usb));
        cfg_ = cfg;
    }
    if (disable && sessionActive_) stopSession(false);
    publishStatus();
}

void AndroidMirror::disconnect() {
    if (sessionActive_) stopSession(true);
}

void AndroidMirror::mirrorAgain() {
    std::lock_guard<std::mutex> lk(m_);
    dismissed_.clear();
    failures_.clear();
}

AndroidStatus AndroidMirror::status() {
    std::lock_guard<std::mutex> lk(m_);
    return last_;
}

// ---------------------------------------------------------------- device watching

void AndroidMirror::watchLoop() {
    uint64_t lastLaunch = 0;
    while (running_) {
        bool enabled;
        {
            std::lock_guard<std::mutex> lk(m_);
            enabled = cfg_.enabled;
        }
        if (!enabled) {
            if (browser_.running()) browser_.stop();
            evaluate();
            sleep_ms(300);
            continue;
        }
        if (adb_.serverVersion() < 0) {
            uint64_t now = now_ms();
            bool started = false;
            if (launcher_ && (lastLaunch == 0 || now - lastLaunch > 15000)) {
                lastLaunch = now;
                LOGI("android: starting the adb server");
                started = launcher_() && adb_.serverVersion() >= 0;
            }
            if (!started) {
                {
                    std::lock_guard<std::mutex> lk(m_);
                    adbOk_ = false;
                    adbError_ = launcher_ ? "adb could not be started" : "adb is not running";
                    devices_.clear();
                }
                evaluate();
                for (int i = 0; i < 10 && running_; i++) sleep_ms(300);
                continue;
            }
        }
        sock_t s = adb_.trackDevices();
        if (s == MN_INVALID_SOCKET) {
            sleep_ms(1000);
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            adbOk_ = true;
        }
        LOGI("android: watching for USB devices (adb server on port %u)", adb_.port());
        while (running_) {
            {
                std::lock_guard<std::mutex> lk(m_);
                if (!cfg_.enabled) break;
            }
            int w = sock_wait_readable(s, 250);
            if (w < 0) break;
            bool pairDone = false;
            if (w > 0) {
                std::vector<AdbDevice> list;
                if (!AdbClient::readDeviceList(s, list, &running_)) break;
                std::lock_guard<std::mutex> lk(m_);
                for (auto& d : list) {
                    bool known = false;
                    for (auto& o : devices_)
                        if (o.serial == d.serial && o.state == d.state) known = true;
                    if (!known) LOGI("android: %s is %s%s%s", d.serial.c_str(), d.state.c_str(), d.model.empty() ? "" : ", ",
                                     d.model.c_str());
                }
                // phones that join over Wi-Fi wait for the user ("Mirror"), except right after pairing
                uint64_t nowMs = now_ms();
                for (auto& d : list) {
                    if (!isWireless(d.serial) || d.state != "device") continue;
                    bool known = false;
                    for (auto& o : devices_)
                        if (o.serial == d.serial && o.state == "device") known = true;
                    if (known) continue;
                    if (nowMs >= autoStartUntil_) dismissed_.insert(d.serial);
                    else autoStartUntil_ = 0;  // the phone we just paired: mirror it now, once
                    if (pair_.phase == PairingStatus::Connecting) {
                        pair_.phase = PairingStatus::Done;
                        pair_.device = d.model;
                        pairDone = true;
                    }
                }
                // forget dismissals / failures of phones that were unplugged
                for (auto it = dismissed_.begin(); it != dismissed_.end();) {
                    bool present = false;
                    for (auto& d : list) present |= d.serial == *it;
                    it = present ? std::next(it) : dismissed_.erase(it);
                }
                for (auto it = failures_.begin(); it != failures_.end();) {
                    bool present = false;
                    for (auto& d : list) present |= d.serial == it->first;
                    it = present ? std::next(it) : failures_.erase(it);
                }
                devices_ = list;
            }
            if (pairDone) setPairing(PairingStatus::Done, "", "");
            wirelessTick();
            evaluate();
        }
        sock_close(s);
        if (running_) sleep_ms(500);
    }
}

void AndroidMirror::evaluate() {
    if (sessionDone_ && sessionThread_.joinable()) {
        sessionThread_.join();
        sessionDone_ = false;
    }
    AdbDevice target;
    bool want = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (cfg_.enabled && !sessionActive_ && !sessionThread_.joinable()) {
            uint64_t now = now_ms();
            for (int pass = 0; pass < 2 && !want; pass++) {  // USB first, then Wi-Fi
                for (auto& d : devices_) {
                    if (isWireless(d.serial) != (pass == 1)) continue;
                    if (pass == 0 ? !cfg_.usb : !cfg_.wireless) continue;
                    if (d.state != "device" || dismissed_.count(d.serial)) continue;
                    auto f = failures_.find(d.serial);
                    if (f != failures_.end() && (f->second.count >= 3 || now < f->second.retryAt)) continue;
                    target = d;
                    want = true;
                    break;
                }
            }
        }
    }
    if (want) startSession(target);
    publishStatus();
}

void AndroidMirror::publishStatus() {
    AndroidStatus st;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!cfg_.enabled) {
            st.state = AndroidState::Off;
        } else if (sessionActive_) {
            if (!sessRunning_) return;  // winding down: the final state follows
            st.state = mirroring_ ? AndroidState::Mirroring : AndroidState::Connecting;
            st.device = sessName_.empty() ? sessModel_ : sessName_;
            st.wireless = isWireless(sessSerial_);
        } else if (!adbOk_) {
            st.state = AndroidState::NoAdb;
            st.detail = adbError_;
        } else {
            st.state = AndroidState::Waiting;
            int rank = 0;
            auto take = [&](int r, AndroidState s, const AdbDevice& d, const std::string& detail) {
                if (r <= rank) return;
                rank = r;
                st.state = s;
                st.device = d.model;
                st.detail = detail;
                st.wireless = isWireless(d.serial);
            };
            for (auto& d : devices_) {
                auto f = failures_.find(d.serial);
                if (isWireless(d.serial) && d.state != "device") continue;  // a phone that left the Wi-Fi
                if (isWireless(d.serial) ? !cfg_.wireless : !cfg_.usb) continue;
                if (d.state == "unauthorized") take(5, AndroidState::Unauthorized, d, "");
                else if (d.state == "device" && f != failures_.end() && f->second.count >= 3)
                    take(4, AndroidState::Failed, d, f->second.error);
                else if (d.state == "device" && dismissed_.count(d.serial)) take(3, AndroidState::Ready, d, "");
                else if (d.state == "device") take(2, AndroidState::Connecting, d, "");
                else take(1, AndroidState::Offline, d, "");
            }
        }
        if (haveLast_ && st.state == last_.state && st.device == last_.device && st.detail == last_.detail &&
            st.wireless == last_.wireless)
            return;
        last_ = st;
        haveLast_ = true;
    }
    if (statusListener_) statusListener_->onAndroidStatus(st);
}

// ---------------------------------------------------------------- session

void AndroidMirror::startSession(const AdbDevice& d) {
    {
        std::lock_guard<std::mutex> lk(m_);
        sessSerial_ = d.serial;
        sessModel_ = d.model;
        sessName_.clear();
    }
    mirroring_ = false;
    stopRequested_ = false;
    sessRunning_ = true;
    sessionActive_ = true;
    sessionDone_ = false;
    sessionThread_ = std::thread(&AndroidMirror::sessionMain, this, d.serial, d.model);
}

void AndroidMirror::stopSession(bool dismiss) {
    std::string serial;
    {
        std::lock_guard<std::mutex> lk(m_);
        serial = sessSerial_;
        if (dismiss && !serial.empty()) dismissed_.insert(serial);
    }
    stopRequested_ = true;
    sessRunning_ = false;
    {
        std::lock_guard<std::mutex> lk(sockM_);
        for (sock_t s : socks_) sock_shutdown(s);
    }
    // Wait for the session to wind down (reads poll the stop flag every 200 ms). The watcher
    // thread joins the finished thread. Never called from the session thread itself.
    while (sessionActive_) sleep_ms(20);
    publishStatus();
}

void AndroidMirror::sessionMain(std::string serial, std::string model) {
    LOGI("android: connecting to %s (%s)", serial.c_str(), model.empty() ? "unknown model" : model.c_str());
    publishStatus();
    std::string name, error;
    bool ok = runSession(serial, name, error);
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!ok && !stopRequested_) {
            Failure& f = failures_[serial];
            f.count++;
            f.retryAt = now_ms() + 3000;
            f.error = error;
            LOGW("android: %s (attempt %d)", error.c_str(), f.count);
        } else if (ok) {
            failures_.erase(serial);
            // the phone stopped the stream (unplugged, or the server ended): don't restart in a loop
            if (!stopRequested_) dismissed_.insert(serial);
        }
        sessSerial_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(sockM_);
        socks_.clear();
    }
    mirroring_ = false;
    sessionActive_ = false;
    sessionDone_ = true;
    publishStatus();
}

// Returns true when a mirroring session ran (and has now ended); false with error otherwise.
bool AndroidMirror::runSession(const std::string& serial, std::string& name, std::string& error) {
    AndroidConfig cfg;
    {
        std::lock_guard<std::mutex> lk(m_);
        cfg = cfg_;
    }
    const bool wireless = isWireless(serial);
    if (jar_.empty()) {
        error = "the phone server is missing from this build";
        return false;
    }
    if (!adb_.push(serial, jar_, kRemoteJar)) {
        error = "could not copy the server to the phone (" + adb_.lastError() + ")";
        return false;
    }

    std::random_device rd;
    uint32_t scid = rd() & 0x7FFFFFFF;
    char sockName[32];
    snprintf(sockName, sizeof(sockName), "scrcpy_%08x", scid);
    uint16_t port = freeLocalPort();
    if (!port || !adb_.forward(serial, port, std::string("localabstract:") + sockName)) {
        error = "could not set up the USB tunnel (" + adb_.lastError() + ")";
        return false;
    }

    char cmd[768];
    snprintf(cmd, sizeof(cmd),
             "CLASSPATH=%s app_process / com.genymobile.scrcpy.Server %s scid=%08x log_level=info tunnel_forward=true "
             "control=false video_codec=h264 video_bit_rate=%d max_size=%d max_fps=%d %s",
             kRemoteJar, kScrcpyServerVersion, scid, wireless ? std::min(cfg.videoBitRate, 8000000) : cfg.videoBitRate,
             cfg.maxSize, cfg.maxFps,
             cfg.audio ? "audio_codec=raw" : "audio=false");
    sock_t sh = adb_.shell(serial, cmd);
    if (sh == MN_INVALID_SOCKET) {
        adb_.killForward(serial, port);
        error = "could not start the server on the phone (" + adb_.lastError() + ")";
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(sockM_);
        socks_.push_back(sh);
    }
    // relay the server's log lines
    std::thread shellThread([this, sh]() {
        std::string line;
        char buf[512];
        while (true) {
            int w = sock_wait_readable(sh, 200);
            if (w < 0) break;
            if (w == 0) {
                if (!sessRunning_) break;
                continue;
            }
            int r = recv(sh, buf, sizeof(buf), 0);
            if (r <= 0) break;
            for (int i = 0; i < r; i++) {
                if (buf[i] == '\n') {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) LOGI("android: phone: %s", line.c_str());
                    line.clear();
                } else if (line.size() < 400) {
                    line += buf[i];
                }
            }
        }
    });

    auto cleanup = [&](sock_t v, sock_t a) {
        sessRunning_ = false;
        {
            std::lock_guard<std::mutex> lk(sockM_);
            for (sock_t s : socks_) sock_shutdown(s);
            socks_.clear();
        }
        if (shellThread.joinable()) shellThread.join();
        if (v != MN_INVALID_SOCKET) sock_close(v);
        if (a != MN_INVALID_SOCKET) sock_close(a);
        sock_close(sh);
    };

    // The tunnel accepts connections before the server listens: a connection is real once the
    // server's first (dummy) byte arrives.
    const uint32_t lo = htonl(0x7F000001);
    sock_t video = MN_INVALID_SOCKET;
    for (int attempt = 0; attempt < 100 && sessRunning_; attempt++) {
        sock_t s = tcp_connect(lo, port, 1000);
        if (s != MN_INVALID_SOCKET) {
            uint8_t b;
            if (sock_wait_readable(s, 2000) > 0 && recv(s, (char*)&b, 1, 0) == 1) {
                video = s;
                break;
            }
            sock_close(s);
        }
        sleep_ms(100);
    }
    if (video == MN_INVALID_SOCKET) {
        adb_.killForward(serial, port);
        cleanup(MN_INVALID_SOCKET, MN_INVALID_SOCKET);
        error = stopRequested_ ? "cancelled" : "the phone did not answer";
        return false;
    }
    sock_t audio = MN_INVALID_SOCKET;
    if (cfg.audio) audio = tcp_connect(lo, port, 2000);
    adb_.killForward(serial, port);
    {
        std::lock_guard<std::mutex> lk(sockM_);
        socks_.push_back(video);
        if (audio != MN_INVALID_SOCKET) socks_.push_back(audio);
    }
    int rcvbuf = 4 << 20;
    setsockopt(video, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

    uint8_t meta[64];
    if (!sock_recv_all(video, meta, 64, &sessRunning_)) {
        cleanup(video, audio);
        error = "the phone closed the connection";
        return false;
    }
    meta[63] = 0;
    name = (const char*)meta;
    {
        std::lock_guard<std::mutex> lk(m_);
        sessName_ = name;
    }
    LOGI("android: connected to %s", name.c_str());

    uint8_t hdr[12];
    if (!sock_recv_all(video, hdr, 4, &sessRunning_) || rd32be(hdr) != kCodecH264) {
        cleanup(video, audio);
        error = "the phone could not start its video encoder";
        return false;
    }

    // From here on this is a mirroring session: take over the screen from any AirPlay sender.
    rx_.disconnectClient();
    ReceiverListener* l = rx_.listener();
    ClientInfo ci;
    ci.name = name.empty() ? "Android phone" : name;
    ci.model = "Android";
    ci.ip = wireless ? "Wi-Fi" : "USB";
    ci.deviceId = serial;
    if (l) {
        l->onClientConnecting(ci);
        l->onSessionStart(ci);
    }
    std::thread audioThread;
    if (audio != MN_INVALID_SOCKET) audioThread = std::thread(&AndroidMirror::audioLoop, this, audio);

    {
        VideoPublisher video_out(rx_.frames(), l, "android");
        std::vector<uint8_t> config, packet;
        bool started = false;
        uint32_t sessW = 0, sessH = 0;
        while (sessRunning_) {
            if (!sock_recv_all(video, hdr, 12, &sessRunning_)) break;
            if (hdr[0] & 0x80) {  // session packet: new capture size (e.g. rotation)
                uint32_t sw = rd32be(hdr + 4), shh = rd32be(hdr + 8);
                if (sw != sessW || shh != sessH) LOGI("android: capture size %ux%u", sw, shh);
                sessW = sw;
                sessH = shh;
                continue;
            }
            uint64_t pts = rd64be(hdr);
            uint32_t len = rd32be(hdr + 8);
            if (len == 0 || len > 32u * 1024 * 1024) {
                LOGE("android: invalid packet size %u", len);
                break;
            }
            packet.resize(len);
            if (!sock_recv_all(video, packet.data(), len, &sessRunning_)) break;
            if (pts & kFlagConfig) {  // SPS/PPS: prepend to the next frame
                config = packet;
                continue;
            }
            if (!started) {
                started = true;
                mirroring_ = true;
                if (l) l->onMirrorStart();
                publishStatus();
            }
            if (!config.empty()) {
                size_t o = config.size();
                config.resize(o + packet.size());
                memcpy(config.data() + o, packet.data(), packet.size());
                video_out.decode(config.data(), config.size());
                config.clear();
            } else {
                video_out.decode(packet.data(), packet.size());
            }
        }
        if (sessRunning_) LOGI("android: the phone ended the stream");
        if (started && l) l->onMirrorStop();
    }
    mirroring_ = false;
    cleanup(video, MN_INVALID_SOCKET);
    if (audioThread.joinable()) audioThread.join();
    if (audio != MN_INVALID_SOCKET) sock_close(audio);
    if (l) l->onSessionEnd();
    LOGI("android: session ended");
    return true;
}

void AndroidMirror::audioLoop(sock_t s) {
    ReceiverListener* l = rx_.listener();
    uint8_t hdr[12];
    if (!sock_recv_all(s, hdr, 4, &sessRunning_)) return;
    uint32_t codec = rd32be(hdr);
    if (codec == 0) {
        LOGI("android: no sound from this phone (needs Android 11 or later)");
        return;
    }
    if (codec != kCodecRaw) {
        LOGW("android: unexpected audio codec 0x%08x", codec);
        return;
    }
    if (l) l->onAudioStart(kAudioPcm48k);
    std::vector<uint8_t> pkt;
    while (sessRunning_) {
        if (!sock_recv_all(s, hdr, 12, &sessRunning_)) break;
        uint32_t len = rd32be(hdr + 8);
        if (len > 4u * 1024 * 1024) break;
        pkt.resize(len);
        if (len && !sock_recv_all(s, pkt.data(), len, &sessRunning_)) break;
        if (rd64be(hdr) & kFlagConfig) continue;
        // 16-bit little-endian stereo; Windows and every supported target is little-endian
        if (l && len >= 4) l->onAudioPcm((const int16_t*)pkt.data(), (int)(len / 4));
    }
    if (l) l->onAudioStop();
}

// ---------------------------------------------------------------- Wi-Fi

// "192.168.1.20:41235" (adb connect) or "adb-SERIAL-xyz._adb-tls-connect._tcp" (adb's own mDNS)
bool AndroidMirror::isWireless(const std::string& serial) {
    return serial.find(':') != std::string::npos || serial.find("._adb-tls-connect.") != std::string::npos;
}

void AndroidMirror::setPairing(PairingStatus::Phase phase, const std::string& device, const std::string& detail) {
    PairingStatus copy;
    {
        std::lock_guard<std::mutex> lk(m_);
        pair_.phase = phase;
        if (!device.empty()) pair_.device = device;
        pair_.detail = detail;
        if (phase != PairingStatus::Waiting) pair_.qr.clear();
        copy = pair_;
    }
    if (statusListener_) statusListener_->onPairingStatus(copy);
}

void AndroidMirror::startPairing() {
    static const char* abc = "abcdefghijkmnpqrstuvwxyz23456789";
    std::random_device rd;
    std::string name = "mirrorly-", code;
    for (int i = 0; i < 6; i++) name += abc[rd() % 32];
    for (int i = 0; i < 10; i++) code += (char)('0' + rd() % 10);
    PairingStatus copy;
    {
        std::lock_guard<std::mutex> lk(m_);
        pairName_ = name;
        pairCode_ = code;
        pairStarted_ = now_ms();
        pair_ = PairingStatus();
        pair_.phase = PairingStatus::Waiting;
        typedCode_.clear();
        codeTargetHp_.clear();
        // the format Android's "Pair device with QR code" scanner understands
        pair_.qr = "WIFI:T:ADB;S:" + name + ";P:" + code + ";;";
        copy = pair_;
    }
    LOGI("android: waiting for a phone to scan the pairing code (%s)", name.c_str());
    browser_.queryNow();
    if (statusListener_) statusListener_->onPairingStatus(copy);
}

void AndroidMirror::pairWithCode(const std::string& code) {
    std::lock_guard<std::mutex> lk(m_);
    typedCode_ = code;
    lastWirelessTick_ = 0;  // handle it on the next tick
}

void AndroidMirror::cancelPairing() {
    std::lock_guard<std::mutex> lk(m_);
    if (pair_.phase != PairingStatus::Idle) LOGI("android: pairing closed");
    pair_ = PairingStatus();
}

void AndroidMirror::wirelessTick() {
    uint64_t now = now_ms();
    if (now - lastWirelessTick_ < 700) return;
    lastWirelessTick_ = now;
    bool wireless;
    PairingStatus::Phase phase;
    std::string name, code;
    {
        std::lock_guard<std::mutex> lk(m_);
        wireless = cfg_.wireless;
        phase = pair_.phase;
        name = pairName_;
        code = pairCode_;
    }
    bool pairingNow = phase == PairingStatus::Waiting || phase == PairingStatus::Connecting;
    if (!wireless && !pairingNow) {
        if (browser_.running()) browser_.stop();
        return;
    }
    if (!browser_.running() && !browser_.start({"_adb-tls-pairing._tcp", "_adb-tls-connect._tcp"})) return;

    // 1. pairing. With the QR code, the phone that scanned it advertises
    //    "<name>._adb-tls-pairing._tcp". With "Pair device with pairing code", the phone advertises
    //    a pairing service under its own name and the user types the code it shows.
    if (phase == PairingStatus::Waiting) {
        if (now - pairStarted_ > 10 * 60 * 1000) {
            setPairing(PairingStatus::Failed, "", "No phone answered. Try again.");
            return;
        }
        std::string typed, target;
        {
            std::lock_guard<std::mutex> lk(m_);
            typed = typedCode_;
            typedCode_.clear();
        }
        std::vector<MdnsFound> offers;
        for (auto& f : browser_.find("_adb-tls-pairing._tcp", 30000))
            if (f.instance != name) offers.push_back(f);
        // the newest code-pairing offer is the one on screen right now
        const MdnsFound* offer = nullptr;
        for (auto& f : offers)
            if (!offer || f.seen > offer->seen) offer = &f;
        target = offer ? ip_to_string(offer->ip) + ":" + std::to_string(offer->port) : "";
        bool changed = false;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (target != codeTargetHp_) {
                codeTargetHp_ = target;
                pair_.codeTarget = offer ? ip_to_string(offer->ip) : "";
                changed = true;
            }
        }
        if (changed) {
            if (offer) LOGI("android: a phone offers pairing with a code at %s", target.c_str());
            setPairing(PairingStatus::Waiting, "", "");
        }
        for (auto& f : browser_.find("_adb-tls-pairing._tcp", 60000)) {
            bool qrMatch = f.instance == name;
            bool codeMatch = !typed.empty() && offer && f.instance == offer->instance;
            if (!qrMatch && !codeMatch) continue;
            std::string hp = ip_to_string(f.ip) + ":" + std::to_string(f.port);
            LOGI("android: pairing with the phone at %s (%s)", hp.c_str(), qrMatch ? "QR code" : "pairing code");
            setPairing(PairingStatus::Pairing, "", "");
            std::string reply;
            if (adb_.pair(hp, qrMatch ? code : typed, reply)) {
                LOGI("android: %s", reply.c_str());
                {
                    std::lock_guard<std::mutex> lk(m_);
                    pairedIp_ = f.ip;
                    autoStartUntil_ = now_ms() + 60000;
                    connectTried_.clear();
                }
                setPairing(PairingStatus::Connecting, "", "");
                browser_.queryNow();
            } else {
                LOGW("android: pairing failed: %s", reply.c_str());
                std::string why = reply.empty() ? "Pairing failed" : reply;
                if (!qrMatch && reply.find("Wrong password") != std::string::npos) why = "Wrong code. Check the code on the phone and try again.";
                setPairing(PairingStatus::Failed, "", why);
            }
            return;
        }
        if (!typed.empty()) setPairing(PairingStatus::Failed, "", "The phone stopped offering a pairing code. Try again.");
    }
    if (phase == PairingStatus::Connecting && now > autoStartUntil_) {
        setPairing(PairingStatus::Failed, "", "Paired, but the phone didn't connect. Is it on the same Wi-Fi as this PC?");
        return;
    }

    // 2. connect to phones with Wireless debugging on (only paired ones accept)
    for (auto& f : browser_.find("_adb-tls-connect._tcp", 60000)) {
        std::string hp = ip_to_string(f.ip) + ":" + std::to_string(f.port);
        bool justPaired;
        {
            std::lock_guard<std::mutex> lk(m_);
            bool present = false;
            for (auto& d : devices_)
                if (d.serial == hp || d.serial.compare(0, f.instance.size(), f.instance) == 0) present = true;
            if (present) continue;
            justPaired = f.ip == pairedIp_ && now < autoStartUntil_;
            uint64_t& t = connectTried_[hp];
            if (t && now - t < (justPaired ? 2000u : 10000u)) continue;
            t = now;
        }
        std::string reply;
        bool ok = adb_.connect(hp, reply);
        if (ok || justPaired) LOGI("android: adb connect %s: %s", hp.c_str(), reply.c_str());
    }
}

}  // namespace mn
