// Mirrorly - AirPlay receiver: discovery, RTSP control channel, pairing, FairPlay, SETUP
//
// Protocol behaviour follows the open-source UxPlay / RPiPlay receivers (legacy pairing,
// FairPlay SAPv1 via playfair, AES-CTR mirroring, AES-CBC audio).
#include "receiver.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

#include "log.h"
#include "native_media.h"
#include "plist.h"
#include "streams.h"

namespace mn {

static const char* kSourceVersion = "220.68";
static const char* kModel = "AppleTV3,2";
static const uint64_t kFeatures = 0x5A7FFEE6ULL;  // bit 27 = legacy pairing, bit 7 = mirroring, bit 9 = audio

// ============================================================== HTTP/RTSP message helpers

struct Request {
    std::string method, url, protocol;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<uint8_t> body;
    std::string header(const std::string& name) const {
        for (auto& h : headers) {
            if (h.first.size() != name.size()) continue;
            bool eq = true;
            for (size_t i = 0; i < name.size(); i++)
                if (tolower((unsigned char)h.first[i]) != tolower((unsigned char)name[i])) { eq = false; break; }
            if (eq) return h.second;
        }
        return "";
    }
};

struct Response {
    int code = 200;
    std::string reason = "OK";
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<uint8_t> body;
    bool close = false;
    void set(int c, const std::string& r) { code = c; reason = r; }
    void add(const std::string& k, const std::string& v) { headers.emplace_back(k, v); }
    void plist(const PlistPtr& p) {
        body = plist_write_binary(p);
        add("Content-Type", "application/x-apple-binary-plist");
    }
};

// ============================================================== Connection

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(Receiver* rx, sock_t s, uint32_t ip) : rx_(rx), sock_(s), remoteIp_(ip) {}
    ~Connection() { sock_close(sock_); }
    void run();
    void kick() { sock_shutdown(sock_); }
    bool done() const { return done_; }
    std::thread thread;

private:
    bool readRequest(Request& rq);
    void handle(const Request& rq, Response& rs);
    void handleInfo(const Request& rq, Response& rs);
    void handlePairSetup(const Request& rq, Response& rs);
    void handlePairVerify(const Request& rq, Response& rs);
    void handleFpSetup(const Request& rq, Response& rs);
    void handleSetup(const Request& rq, Response& rs);
    void handleGetParameter(const Request& rq, Response& rs);
    void handleSetParameter(const Request& rq, Response& rs);
    void handleTeardown(const Request& rq, Response& rs);
    void parseDmap(const uint8_t* d, size_t n, std::string& title, std::string& artist, std::string& album, int depth = 0);

    Receiver* rx_;
    sock_t sock_;
    uint32_t remoteIp_;
    std::vector<uint8_t> inbuf_;
    volatile bool running_ = true;
    std::atomic<bool> done_{false};

    // pairing state
    bool pairVerified_ = false;
    bool haveSecret_ = false;
    uint8_t ecdhOurSecret_[32], ecdhOurPub_[32], ecdhTheirPub_[32], edTheirPub_[32], ecdhShared_[32];
    // fairplay
    uint8_t keymsg_[164];
    bool haveKeymsg_ = false;
    float volumeDb_ = 0.0f;
};

bool Connection::readRequest(Request& rq) {
    // read header block
    size_t hdrEnd = std::string::npos;
    uint8_t tmp[16384];
    while (true) {
        if (inbuf_.size() >= 4) {
            for (size_t i = 0; i + 3 < inbuf_.size(); i++)
                if (inbuf_[i] == '\r' && inbuf_[i + 1] == '\n' && inbuf_[i + 2] == '\r' && inbuf_[i + 3] == '\n') { hdrEnd = i; break; }
        }
        if (hdrEnd != std::string::npos) break;
        if (inbuf_.size() > 64 * 1024) return false;
        int w = sock_wait_readable(sock_, 1000);
        if (w < 0) return false;
        if (w == 0) {
            if (!running_ || !rx_->running()) return false;
            continue;
        }
        int r = recv(sock_, (char*)tmp, sizeof(tmp), 0);
        if (r <= 0) return false;
        inbuf_.insert(inbuf_.end(), tmp, tmp + r);
    }
    std::string head((const char*)inbuf_.data(), hdrEnd);
    std::istringstream hs(head);
    std::string line;
    std::getline(hs, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
        std::istringstream ls(line);
        ls >> rq.method >> rq.url >> rq.protocol;
    }
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c), v = line.substr(c + 1);
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        rq.headers.emplace_back(k, v);
    }
    size_t clen = 0;
    std::string cl = rq.header("Content-Length");
    if (!cl.empty()) clen = (size_t)strtoul(cl.c_str(), nullptr, 10);
    if (clen > 32 * 1024 * 1024) return false;
    inbuf_.erase(inbuf_.begin(), inbuf_.begin() + hdrEnd + 4);
    while (inbuf_.size() < clen) {
        int w = sock_wait_readable(sock_, 1000);
        if (w < 0) return false;
        if (w == 0) {
            if (!running_ || !rx_->running()) return false;
            continue;
        }
        int r = recv(sock_, (char*)tmp, sizeof(tmp), 0);
        if (r <= 0) return false;
        inbuf_.insert(inbuf_.end(), tmp, tmp + r);
    }
    rq.body.assign(inbuf_.begin(), inbuf_.begin() + clen);
    inbuf_.erase(inbuf_.begin(), inbuf_.begin() + clen);
    return true;
}

void Connection::run() {
    sock_set_nodelay(sock_);
    LOGI("connection from %s", ip_to_string(remoteIp_).c_str());
    while (running_ && rx_->running()) {
        Request rq;
        if (!readRequest(rq)) break;
        Response rs;
        handle(rq, rs);
        std::string cseq = rq.header("CSeq");
        std::ostringstream o;
        o << (rq.protocol.empty() ? "RTSP/1.0" : rq.protocol) << " " << rs.code << " " << rs.reason << "\r\n";
        for (auto& h : rs.headers) o << h.first << ": " << h.second << "\r\n";
        o << "Server: AirTunes/" << kSourceVersion << "\r\n";
        if (!cseq.empty()) o << "CSeq: " << cseq << "\r\n";
        if (!rs.body.empty()) o << "Content-Length: " << rs.body.size() << "\r\n";
        else {
            for (auto& h : rs.headers)
                if (h.first == "Content-Type") { o << "Content-Length: 0\r\n"; break; }
        }
        o << "\r\n";
        std::string hdr = o.str();
        std::vector<uint8_t> out(hdr.begin(), hdr.end());
        out.insert(out.end(), rs.body.begin(), rs.body.end());
        if (!sock_send_all(sock_, out.data(), out.size())) break;
        if (rs.close) break;
    }
    LOGI("connection from %s closed", ip_to_string(remoteIp_).c_str());
    rx_->endSession(this);
    done_ = true;
}

void Connection::handle(const Request& rq, Response& rs) {
    const std::string& m = rq.method;
    const std::string& u = rq.url;
    if (m != "POST" || u != "/feedback") LOGD("%s %s", m.c_str(), u.c_str());
    if (!rq.header("CSeq").empty() && m != "RECORD") rs.add("Audio-Jack-Status", "connected; type=digital");

    if (m == "GET" && u.find("/info") != std::string::npos) handleInfo(rq, rs);
    else if (m == "POST" && u == "/pair-setup") handlePairSetup(rq, rs);
    else if (m == "POST" && u == "/pair-verify") handlePairVerify(rq, rs);
    else if (m == "POST" && u == "/fp-setup") handleFpSetup(rq, rs);
    else if (m == "POST" && (u == "/feedback" || u == "/audioMode")) { /* 200 OK */ }
    else if (m == "OPTIONS") rs.add("Public", "SETUP, RECORD, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER");
    else if (m == "SETUP") handleSetup(rq, rs);
    else if (m == "GET_PARAMETER") handleGetParameter(rq, rs);
    else if (m == "SET_PARAMETER") handleSetParameter(rq, rs);
    else if (m == "RECORD") {
        rs.add("Audio-Latency", "11025");
        rs.add("Audio-Jack-Status", "connected; type=analog");
    } else if (m == "FLUSH") {
        auto s = rx_->activeSession(this);
        if (s) {
            std::lock_guard<std::mutex> lk(s->mutex);
            if (s->audio) s->audio->flush();
        }
    } else if (m == "TEARDOWN") handleTeardown(rq, rs);
    else if (m == "POST" && (u == "/pair-pin-start" || u == "/pair-setup-pin")) {
        LOGW("sender requested PIN pairing, which is not enabled");
        rs.set(501, "Not Implemented");
    } else {
        LOGI("unhandled request %s %s %s", m.c_str(), u.c_str(), rq.protocol.c_str());
        if (rq.protocol == "RTSP/1.0" && m != "POST" && m != "GET") rs.set(501, "Not Implemented");
    }
}

void Connection::handleInfo(const Request& rq, Response& rs) {
    auto res = Plist::makeDict();
    std::string ctype = rq.header("Content-Type");
    bool addTxtAirplay = false, addTxtRaop = false;
    if (ctype.find("application/x-apple-binary-plist") != std::string::npos) {
        auto req = plist_parse_binary(rq.body.data(), rq.body.size());
        if (req && req->isDict()) {
            auto q = req->get("qualifier");
            if (q && q->isArray() && !q->array.empty() && q->array[0]->isString()) {
                if (q->array[0]->s == "txtAirPlay") addTxtAirplay = true;
                if (q->array[0]->s == "txtRAOP") addTxtRaop = true;
            }
        }
    }
    bool ble = rq.header("CSeq").empty();
    if (ble) {
        addTxtAirplay = rq.url.find("txtAirPlay") != std::string::npos;
        addTxtRaop = rq.url.find("txtRAOP") != std::string::npos;
    }
    if (addTxtAirplay) res->set("txtAirPlay", Plist::makeData(rx_->airplayTxtRecord()));
    if (addTxtRaop) res->set("txtRAOP", Plist::makeData(rx_->raopTxtRecord()));
    if (!ctype.empty()) {
        rs.plist(res);
        return;
    }
    ReceiverConfig cfg = rx_->config();
    char mac[32];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", cfg.deviceId[0], cfg.deviceId[1], cfg.deviceId[2],
             cfg.deviceId[3], cfg.deviceId[4], cfg.deviceId[5]);
    res->set("deviceID", Plist::makeString(mac));
    res->set("macAddress", Plist::makeString(mac));
    res->set("pk", Plist::makeData(rx_->edKey().pub, 32));
    res->set("features", Plist::makeInt(rx_->features()));
    res->set("name", Plist::makeString(cfg.name));
    res->set("pi", Plist::makeString(cfg.pi));
    res->set("vv", Plist::makeInt(2));
    res->set("statusFlags", Plist::makeInt(68));
    res->set("keepAliveLowPower", Plist::makeBool(true));
    res->set("sourceVersion", Plist::makeString(kSourceVersion));
    res->set("keepAliveSendStatsAsBody", Plist::makeBool(true));
    res->set("model", Plist::makeString(kModel));
    if (!ble) {
        res->set("initialVolume", Plist::makeReal(volumeDb_));
        auto lat = Plist::makeArray();
        for (int t : {100, 101}) {
            auto d = Plist::makeDict();
            d->set("type", Plist::makeInt((uint64_t)t));
            d->set("inputLatencyMicros", Plist::makeInt(0));
            d->set("audioType", Plist::makeString("default"));
            d->set("outputLatencyMicros", Plist::makeBool(false));
            lat->append(d);
        }
        res->set("audioLatencies", lat);
        auto fmts = Plist::makeArray();
        for (int t : {100, 101}) {
            auto d = Plist::makeDict();
            d->set("audioOutputFormats", Plist::makeInt(0x3fffffc));
            d->set("type", Plist::makeInt((uint64_t)t));
            d->set("audioInputFormats", Plist::makeInt(0x3fffffc));
            fmts->append(d);
        }
        res->set("audioFormats", fmts);
        auto displays = Plist::makeArray();
        auto d = Plist::makeDict();
        d->set("uuid", Plist::makeString("e0ff8a27-6738-3d56-8a16-cc53aacee925"));
        d->set("widthPhysical", Plist::makeInt(0));
        d->set("heightPhysical", Plist::makeInt(0));
        d->set("width", Plist::makeInt((uint64_t)cfg.width));
        d->set("height", Plist::makeInt((uint64_t)cfg.height));
        d->set("widthPixels", Plist::makeInt((uint64_t)cfg.width));
        d->set("heightPixels", Plist::makeInt((uint64_t)cfg.height));
        d->set("rotation", Plist::makeBool(false));
        d->set("refreshRate", Plist::makeReal(1.0 / (cfg.maxFps > 0 ? cfg.maxFps : 60)));
        d->set("maxFPS", Plist::makeInt((uint64_t)cfg.maxFps));
        d->set("overscanned", Plist::makeBool(false));
        d->set("features", Plist::makeInt(14));
        displays->append(d);
        res->set("displays", displays);
    }
    rs.plist(res);
}

void Connection::handlePairSetup(const Request& rq, Response& rs) {
    if (rq.body.size() != 32) {
        LOGW("pair-setup: unexpected data length %u", (unsigned)rq.body.size());
        rs.set(400, "Bad Request");
        return;
    }
    rs.body.assign(rx_->edKey().pub, rx_->edKey().pub + 32);
    rs.add("Content-Type", "application/octet-stream");
}

static void deriveVerifyKeys(const uint8_t secret[32], uint8_t key[16], uint8_t iv[16]) {
    uint8_t h[64];
    const char* sk = "Pair-Verify-AES-Key";
    const char* si = "Pair-Verify-AES-IV";
    sha512_2((const uint8_t*)sk, strlen(sk), secret, 32, h);
    memcpy(key, h, 16);
    sha512_2((const uint8_t*)si, strlen(si), secret, 32, h);
    memcpy(iv, h, 16);
}

void Connection::handlePairVerify(const Request& rq, Response& rs) {
    const auto& d = rq.body;
    if (d.size() < 4) {
        rs.set(400, "Bad Request");
        return;
    }
    if (d[0] == 1) {
        if (d.size() != 4 + 32 + 32) {
            rs.set(400, "Bad Request");
            return;
        }
        memcpy(ecdhTheirPub_, &d[4], 32);
        memcpy(edTheirPub_, &d[36], 32);
        x25519_keypair(ecdhOurSecret_, ecdhOurPub_);
        x25519_shared(ecdhOurSecret_, ecdhTheirPub_, ecdhShared_);
        haveSecret_ = true;
        pairVerified_ = false;
        uint8_t msg[64], sig[64], key[16], iv[16];
        memcpy(msg, ecdhOurPub_, 32);
        memcpy(msg + 32, ecdhTheirPub_, 32);
        rx_->edKey().sign(msg, 64, sig);
        deriveVerifyKeys(ecdhShared_, key, iv);
        AesCtr ctr;
        ctr.init(key, iv);
        ctr.process(sig, 64);
        rs.body.assign(ecdhOurPub_, ecdhOurPub_ + 32);
        rs.body.insert(rs.body.end(), sig, sig + 64);
        rs.add("Content-Type", "application/octet-stream");
    } else if (d[0] == 0) {
        if (d.size() != 4 + 64 || !haveSecret_) {
            rs.set(400, "Bad Request");
            return;
        }
        uint8_t key[16], iv[16], sig[64], zeros[64] = {0};
        deriveVerifyKeys(ecdhShared_, key, iv);
        AesCtr ctr;
        ctr.init(key, iv);
        ctr.process(zeros, 64);  // skip the keystream used for our own signature
        memcpy(sig, &d[4], 64);
        ctr.process(sig, 64);
        uint8_t msg[64];
        memcpy(msg, ecdhTheirPub_, 32);
        memcpy(msg + 32, ecdhOurPub_, 32);
        if (!ed25519_verify(edTheirPub_, msg, 64, sig)) {
            LOGW("pair-verify: client signature invalid");
            rs.set(470, "Connection Authorization Required");
            rs.close = true;
            return;
        }
        pairVerified_ = true;
        LOGI("pair-verify: paired with sender");
        rs.add("Content-Type", "application/octet-stream");
    }
}

void Connection::handleFpSetup(const Request& rq, Response& rs) {
    const auto& d = rq.body;
    if (d.size() == 16) {
        if (d[4] != 0x03) {
            LOGW("fp-setup: unsupported FairPlay version 0x%02x", d[4]);
            rs.set(501, "Not Implemented");
            return;
        }
        uint8_t out[142];
        if (mn_fairplay_setup(d.data(), out) != 0) {
            rs.set(400, "Bad Request");
            return;
        }
        rs.body.assign(out, out + 142);
        rs.add("Content-Type", "application/octet-stream");
        haveKeymsg_ = false;
    } else if (d.size() == 164) {
        uint8_t out[32];
        if (mn_fairplay_handshake(d.data(), out) != 0) {
            rs.set(400, "Bad Request");
            return;
        }
        memcpy(keymsg_, d.data(), 164);
        haveKeymsg_ = true;
        rs.body.assign(out, out + 32);
        rs.add("Content-Type", "application/octet-stream");
        LOGI("fp-setup: FairPlay handshake complete");
    } else {
        rs.set(400, "Bad Request");
    }
}

void Connection::handleSetup(const Request& rq, Response& rs) {
    auto req = plist_parse_binary(rq.body.data(), rq.body.size());
    if (!req || !req->isDict()) {
        LOGW("SETUP without a valid plist body");
        rs.set(400, "Bad Request");
        return;
    }
    auto res = Plist::makeDict();
    const auto* ekey = req->getData("ekey");
    const auto* eiv = req->getData("eiv");
    std::shared_ptr<Session> session = rx_->activeSession(this);

    if (ekey && eiv) {
        // ---- SETUP 1: keys + timing
        if (ekey->size() != 72 || eiv->size() != 16 || !haveKeymsg_) {
            LOGE("SETUP: invalid ekey/eiv or FairPlay handshake missing");
            rs.set(400, "Bad Request");
            return;
        }
        session = std::make_shared<Session>(rx_);
        session->remoteIp = remoteIp_;
        session->client.name = req->getString("name");
        session->client.model = req->getString("model");
        session->client.deviceId = req->getString("deviceID");
        session->client.osVersion = req->getString("osVersion");
        session->client.ip = ip_to_string(remoteIp_);
        if (session->client.name.empty()) session->client.name = "AirPlay device";
        if (rx_->listener()) rx_->listener()->onClientConnecting(session->client);

        uint8_t aeskey[16];
        mn_fairplay_decrypt(keymsg_, ekey->data(), aeskey);
        std::string ua = rq.header("User-Agent");
        bool oldProtocol = ua.find("AirMyPC") != std::string::npos;
        if (haveSecret_ && !oldProtocol) {
            uint8_t h[64];
            sha512_2(aeskey, 16, ecdhShared_, 32, h);
            memcpy(aeskey, h, 16);
        }
        memcpy(session->aeskey, aeskey, 16);
        memcpy(session->aesiv, eiv->data(), 16);

        uint16_t timingPort = (uint16_t)req->getUInt("timingPort");
        session->ntp.reset(new NtpClient());
        session->ntp->start(remoteIp_, timingPort);
        res->set("eventPort", Plist::makeInt(0));
        res->set("timingPort", Plist::makeInt(session->ntp->localPort()));
        rx_->activateSession(session, this);
        LOGI("SETUP: session started with \"%s\" (%s, iOS %s)", session->client.name.c_str(),
             session->client.model.c_str(), session->client.osVersion.c_str());
    }

    auto streams = req->get("streams");
    if (streams && streams->isArray()) {
        if (!session) {
            LOGE("SETUP streams without an active session");
            rs.set(400, "Bad Request");
            return;
        }
        auto outStreams = Plist::makeArray();
        for (auto& st : streams->array) {
            if (!st || !st->isDict()) continue;
            uint64_t type = st->getUInt("type");
            if (type == 110) {
                uint64_t scid = st->getUInt("streamConnectionID");
                session->stopMirror();
                std::unique_ptr<MirrorStream> m(new MirrorStream(rx_));
                if (!m->start(session->aeskey, scid)) {
                    rs.set(500, "Internal Server Error");
                    return;
                }
                auto o = Plist::makeDict();
                o->set("dataPort", Plist::makeInt(m->port()));
                o->set("type", Plist::makeInt(110));
                outStreams->append(o);
                {
                    std::lock_guard<std::mutex> lk(session->mutex);
                    session->mirror = std::move(m);
                }
                if (rx_->listener()) rx_->listener()->onMirrorStart();
            } else if (type == 96) {
                int ct = (int)st->getUInt("ct");
                uint16_t rcport = (uint16_t)st->getUInt("controlPort");
                session->stopAudio();
                std::unique_ptr<AudioStream> a(new AudioStream(rx_));
                if (!a->start(session->aeskey, session->aesiv, ct, remoteIp_, rcport)) {
                    rs.set(500, "Internal Server Error");
                    return;
                }
                auto o = Plist::makeDict();
                o->set("dataPort", Plist::makeInt(a->dataPort()));
                o->set("controlPort", Plist::makeInt(a->controlPort()));
                o->set("type", Plist::makeInt(96));
                outStreams->append(o);
                {
                    std::lock_guard<std::mutex> lk(session->mutex);
                    session->audio = std::move(a);
                }
                if (rx_->listener()) rx_->listener()->onAudioStart(ct);
            } else {
                LOGW("SETUP: unknown stream type %llu", (unsigned long long)type);
            }
        }
        res->set("streams", outStreams);
    }
    rs.plist(res);
}

void Connection::handleGetParameter(const Request& rq, Response& rs) {
    std::string body(rq.body.begin(), rq.body.end());
    if (body.find("volume") != std::string::npos) {
        char v[64];
        snprintf(v, sizeof(v), "volume: %9.6f\r\n", volumeDb_);
        rs.body.assign(v, v + strlen(v));
        rs.add("Content-Type", "text/parameters");
    }
}

void Connection::parseDmap(const uint8_t* d, size_t n, std::string& title, std::string& artist, std::string& album, int depth) {
    size_t p = 0;
    while (p + 8 <= n && depth < 4) {
        std::string tag((const char*)d + p, 4);
        uint32_t len = ((uint32_t)d[p + 4] << 24) | ((uint32_t)d[p + 5] << 16) | ((uint32_t)d[p + 6] << 8) | d[p + 7];
        if (p + 8 + len > n) break;
        const uint8_t* v = d + p + 8;
        if (tag == "mlit" || tag == "mlcl" || tag == "cmst") parseDmap(v, len, title, artist, album, depth + 1);
        else if (tag == "minm") title.assign((const char*)v, len);
        else if (tag == "asar") artist.assign((const char*)v, len);
        else if (tag == "asal") album.assign((const char*)v, len);
        p += 8 + len;
    }
}

void Connection::handleSetParameter(const Request& rq, Response& rs) {
    std::string ctype = rq.header("Content-Type");
    auto* l = rx_->listener();
    if (ctype == "text/parameters") {
        std::string body(rq.body.begin(), rq.body.end());
        if (body.compare(0, 8, "volume: ") == 0) {
            volumeDb_ = (float)atof(body.c_str() + 8);
            if (l) l->onVolume(volumeDb_);
        } else if (body.compare(0, 10, "progress: ") == 0) {
            unsigned long long a = 0, b = 0, c = 0;
            if (sscanf(body.c_str() + 10, "%llu/%llu/%llu", &a, &b, &c) == 3 && l && c >= a)
                l->onProgress((double)(b - a) / 44100.0, (double)(c - a) / 44100.0);
        }
    } else if (ctype == "image/jpeg" || ctype == "image/png") {
        if (l) l->onArtwork(rq.body);
    } else if (ctype == "application/x-dmap-tagged") {
        std::string t, a, al;
        parseDmap(rq.body.data(), rq.body.size(), t, a, al);
        if (l) l->onMetadata(t, a, al);
    }
    (void)rs;
}

void Connection::handleTeardown(const Request& rq, Response& rs) {
    bool t96 = false, t110 = false;
    auto req = plist_parse_binary(rq.body.data(), rq.body.size());
    if (req && req->isDict()) {
        auto streams = req->get("streams");
        if (streams && streams->isArray())
            for (auto& st : streams->array) {
                if (!st || !st->isDict()) continue;
                uint64_t t = st->getUInt("type");
                if (t == 96) t96 = true;
                if (t == 110) t110 = true;
            }
    }
    rs.add("Connection", "close");
    auto s = rx_->activeSession(this);
    if (t96 || t110) {
        if (s && t96) s->stopAudio();
        if (s && t110) s->stopMirror();
    } else {
        rx_->endSession(this);
    }
}

// ============================================================== Receiver

Receiver::Receiver() {}
Receiver::~Receiver() { stop(); }

uint64_t Receiver::features() const { return kFeatures; }

std::string Receiver::publicKeyHex() const { return hex_encode(edKey_.pub, 32); }

ReceiverConfig Receiver::config() {
    std::lock_guard<std::mutex> lk(cfgMutex_);
    return cfg_;
}

std::vector<std::string> Receiver::airplayTxt() {
    ReceiverConfig c = config();
    char mac[32], feat[32];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", c.deviceId[0], c.deviceId[1], c.deviceId[2], c.deviceId[3],
             c.deviceId[4], c.deviceId[5]);
    snprintf(feat, sizeof(feat), "0x%X,0x%X", (unsigned)(kFeatures & 0xFFFFFFFF), (unsigned)(kFeatures >> 32));
    return {std::string("deviceid=") + mac,
            std::string("features=") + feat,
            "pw=false",
            "flags=0x4",
            std::string("model=") + kModel,
            "pk=" + publicKeyHex(),
            "pi=" + c.pi,
            std::string("srcvers=") + kSourceVersion,
            "vv=2"};
}

std::vector<std::string> Receiver::raopTxt() {
    char feat[32];
    snprintf(feat, sizeof(feat), "0x%X,0x%X", (unsigned)(kFeatures & 0xFFFFFFFF), (unsigned)(kFeatures >> 32));
    return {"ch=2",          "cn=0,1,2,3",    "da=true",  "et=0,3,5",
            "vv=2",          std::string("ft=") + feat, std::string("am=") + kModel, "md=0,1,2",
            "rhd=5.6.0.0",   "pw=false",      "sf=0x4",   "sr=44100",
            "ss=16",         "sv=false",      "tp=UDP",   "txtvers=1",
            std::string("vs=") + kSourceVersion, "vn=65537", "pk=" + publicKeyHex()};
}

static std::vector<uint8_t> encodeTxt(const std::vector<std::string>& txt) {
    std::vector<uint8_t> o;
    for (auto& t : txt) {
        o.push_back((uint8_t)std::min<size_t>(t.size(), 255));
        o.insert(o.end(), t.begin(), t.begin() + std::min<size_t>(t.size(), 255));
    }
    return o;
}

std::vector<uint8_t> Receiver::airplayTxtRecord() { return encodeTxt(airplayTxt()); }
std::vector<uint8_t> Receiver::raopTxtRecord() { return encodeTxt(raopTxt()); }

std::vector<MdnsService> Receiver::buildServices() {
    ReceiverConfig c = config();
    MdnsService airplay;
    airplay.instance = c.name;
    airplay.type = "_airplay._tcp";
    airplay.port = port_;
    airplay.txt = airplayTxt();
    MdnsService raop;
    raop.instance = hex_encode(c.deviceId, 6, true) + "@" + c.name;
    raop.type = "_raop._tcp";
    raop.port = port_;
    raop.txt = raopTxt();
    return {airplay, raop};
}

bool Receiver::start(const ReceiverConfig& cfg, ReceiverListener* listener) {
    stop();
    net_init();
    {
        std::lock_guard<std::mutex> lk(cfgMutex_);
        cfg_ = cfg;
        if (cfg_.hostname.empty()) cfg_.hostname = "Mirrorly-" + hex_encode(cfg_.deviceId, 6, true);
    }
    listener_ = listener;
    edKey_.fromSeed(cfg.edSeed);
    error_.clear();
    LOGI("%s", mn_version());

    uint16_t want = cfg.port ? cfg.port : 7000;
    for (int i = 0; i < 20 && listenSock_ == MN_INVALID_SOCKET; i++) listenSock_ = tcp_listen((uint16_t)(want + i), &port_);
    if (listenSock_ == MN_INVALID_SOCKET) listenSock_ = tcp_listen(0, &port_);
    if (listenSock_ == MN_INVALID_SOCKET) {
        error_ = "Could not open a network port for AirPlay.";
        LOGE("%s", error_.c_str());
        return false;
    }
    running_ = true;
    acceptThread_ = std::thread(&Receiver::acceptLoop, this);
    LOGI("AirPlay service listening on TCP port %u", port_);

    if (!mdns_.start(config().hostname, buildServices())) {
        error_ = "Network discovery could not start: " + mdns_.lastError();
        LOGE("%s", error_.c_str());
    } else {
        LOGI("advertising \"%s\" via Bonjour", cfg.name.c_str());
    }
    return true;
}

void Receiver::stop() {
    if (!running_ && !acceptThread_.joinable()) return;
    mdns_.stop();
    running_ = false;
    if (acceptThread_.joinable()) acceptThread_.join();
    sock_close(listenSock_);
    listenSock_ = MN_INVALID_SOCKET;
    std::vector<std::shared_ptr<Connection>> conns;
    {
        std::lock_guard<std::mutex> lk(connMutex_);
        conns.swap(conns_);
    }
    for (auto& c : conns) c->kick();
    for (auto& c : conns)
        if (c->thread.joinable()) c->thread.join();
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lk(sessionMutex_);
        s = session_;
        session_.reset();
        sessionOwner_ = nullptr;
    }
    if (s) {
        s->shutdown();
        if (listener_) listener_->onSessionEnd();
    }
}

void Receiver::updateConfig(const ReceiverConfig& cfg) {
    {
        std::lock_guard<std::mutex> lk(cfgMutex_);
        std::string host = cfg_.hostname;
        cfg_ = cfg;
        cfg_.hostname = host;
    }
    if (mdns_.running()) mdns_.update(buildServices());
}

void Receiver::acceptLoop() {
    while (running_) {
        if (sock_wait_readable(listenSock_, 250) <= 0) {
            // reap finished connections
            std::lock_guard<std::mutex> lk(connMutex_);
            for (auto it = conns_.begin(); it != conns_.end();) {
                if ((*it)->done()) {
                    if ((*it)->thread.joinable()) (*it)->thread.join();
                    it = conns_.erase(it);
                } else {
                    ++it;
                }
            }
            continue;
        }
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        sock_t s = accept(listenSock_, (sockaddr*)&from, &fl);
        if (s == MN_INVALID_SOCKET) continue;
        auto c = std::make_shared<Connection>(this, s, from.sin_addr.s_addr);
        std::lock_guard<std::mutex> lk(connMutex_);
        conns_.push_back(c);
        c->thread = std::thread([c]() { c->run(); });
    }
}

void Receiver::activateSession(std::shared_ptr<Session> s, Connection* owner) {
    std::shared_ptr<Session> old;
    Connection* oldOwner = nullptr;
    {
        std::lock_guard<std::mutex> lk(sessionMutex_);
        old = session_;
        oldOwner = sessionOwner_;
        session_ = s;
        sessionOwner_ = owner;
    }
    if (old) {
        LOGI("a new sender took over the receiver");
        old->shutdown();
        if (oldOwner && oldOwner != owner) {
            std::lock_guard<std::mutex> lk(connMutex_);
            for (auto& c : conns_)
                if (c.get() == oldOwner) c->kick();
        }
    }
    if (listener_) listener_->onSessionStart(s->client);
}

void Receiver::endSession(Connection* owner) {
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lk(sessionMutex_);
        if (sessionOwner_ != owner || !session_) return;
        s = session_;
        session_.reset();
        sessionOwner_ = nullptr;
    }
    s->shutdown();
    LOGI("session ended");
    if (listener_) listener_->onSessionEnd();
}

std::shared_ptr<Session> Receiver::activeSession(Connection* owner) {
    std::lock_guard<std::mutex> lk(sessionMutex_);
    return sessionOwner_ == owner ? session_ : nullptr;
}

void Receiver::disconnectClient() {
    Connection* owner;
    {
        std::lock_guard<std::mutex> lk(sessionMutex_);
        owner = sessionOwner_;
    }
    if (!owner) return;
    endSession(owner);
    std::lock_guard<std::mutex> lk(connMutex_);
    for (auto& c : conns_)
        if (c.get() == owner) c->kick();
}

}  // namespace mn
