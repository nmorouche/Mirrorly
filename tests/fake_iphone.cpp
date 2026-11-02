// Mirrorly end-to-end test: runs the receiver in-process and drives it like an iPhone would:
// mDNS query, /info, legacy pair-setup + pair-verify, FairPlay fp-setup, SETUP (keys, mirror, audio),
// encrypted H.264 mirroring stream, encrypted RTP audio (ALAC or AAC-LC), metadata, TEARDOWN.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "crypto.h"
#include "log.h"
#include "native_media.h"
#include "platform.h"
#include "plist.h"
#include "receiver.h"

extern "C" {
#include "monocypher-ed25519.h"
}

using namespace mn;

static int failures = 0;
#define CHECK(cond, ...)                                   \
    do {                                                   \
        if (cond) { printf("  PASS  "); printf(__VA_ARGS__); printf("\n"); } \
        else { printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); failures++; } \
    } while (0)

struct TestListener : ReceiverListener {
    std::atomic<int> frames{0}, sessionStarts{0}, sessionEnds{0}, mirrorStarts{0}, mirrorStops{0};
    std::atomic<int> audioStarts{0}, audioStops{0};
    std::atomic<long> pcmFrames{0};
    std::atomic<double> pcmEnergy{0};
    std::atomic<float> volume{1};
    std::string title, artist, album;
    size_t artworkBytes = 0;
    Receiver* rx = nullptr;
    int lastW = 0, lastH = 0;
    uint8_t centerPixel[4] = {0};
    void onSessionStart(const ClientInfo& c) override { sessionStarts++; printf("  [event] session start: %s (%s)\n", c.name.c_str(), c.model.c_str()); }
    void onSessionEnd() override { sessionEnds++; printf("  [event] session end\n"); }
    void onMirrorStart() override { mirrorStarts++; }
    void onMirrorStop() override { mirrorStops++; }
    void onAudioStart(int) override { audioStarts++; }
    void onAudioStop() override { audioStops++; }
    void onVideoFrame() override {
        frames++;
        const VideoFrame* f = rx->frames().acquireRead();
        if (f) {
            lastW = f->width;
            lastH = f->height;
            memcpy(centerPixel, &f->pixels[(size_t)(f->height / 2) * f->stride + (f->width / 2) * 4], 4);
        }
    }
    void onAudioPcm(const int16_t* pcm, int n) override {
        pcmFrames += n;
        double e = 0;
        for (int i = 0; i < n * 2; i++) e += (double)pcm[i] * pcm[i];
        pcmEnergy = pcmEnergy + e;
    }
    void onVolume(float db) override { volume = db; }
    void onMetadata(const std::string& t, const std::string& a, const std::string& al) override { title = t; artist = a; album = al; }
    void onArtwork(const std::vector<uint8_t>& img) override { artworkBytes = img.size(); }
};

// ---------------------------------------------------------------- RTSP client
struct Rtsp {
    sock_t s;
    int cseq = 0;
    std::vector<uint8_t> buf;
    bool connect(uint16_t port) {
        s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(0x7f000001);
        return ::connect(s, (sockaddr*)&a, sizeof(a)) == 0;
    }
    struct Resp { int code = 0; std::string head; std::vector<uint8_t> body; };
    Resp request(const std::string& method, const std::string& url, const std::vector<uint8_t>& body,
                 const std::string& ctype, const std::string& extra = "") {
        std::string h = method + " " + url + " RTSP/1.0\r\nCSeq: " + std::to_string(++cseq) + "\r\n";
        h += "User-Agent: AirPlay/690.7.1\r\n";
        if (!ctype.empty()) h += "Content-Type: " + ctype + "\r\n";
        h += extra;
        h += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        std::vector<uint8_t> out(h.begin(), h.end());
        out.insert(out.end(), body.begin(), body.end());
        sock_send_all(s, out.data(), out.size());
        Resp r;
        // read headers
        uint8_t tmp[8192];
        size_t end;
        while (true) {
            std::string sb(buf.begin(), buf.end());
            end = sb.find("\r\n\r\n");
            if (end != std::string::npos) break;
            if (sock_wait_readable(s, 5000) <= 0) return r;
            int n = recv(s, (char*)tmp, sizeof(tmp), 0);
            if (n <= 0) return r;
            buf.insert(buf.end(), tmp, tmp + n);
        }
        r.head.assign(buf.begin(), buf.begin() + end);
        buf.erase(buf.begin(), buf.begin() + end + 4);
        sscanf(r.head.c_str(), "RTSP/1.0 %d", &r.code);
        size_t clen = 0;
        auto p = r.head.find("Content-Length: ");
        if (p != std::string::npos) clen = strtoul(r.head.c_str() + p + 16, nullptr, 10);
        while (buf.size() < clen) {
            if (sock_wait_readable(s, 5000) <= 0) return r;
            int n = recv(s, (char*)tmp, sizeof(tmp), 0);
            if (n <= 0) return r;
            buf.insert(buf.end(), tmp, tmp + n);
        }
        r.body.assign(buf.begin(), buf.begin() + clen);
        buf.erase(buf.begin(), buf.begin() + clen);
        return r;
    }
};

static std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::vector<std::vector<uint8_t>> readPackets(const std::string& p) {
    auto d = readFile(p);
    std::vector<std::vector<uint8_t>> out;
    size_t pos = 0;
    while (pos + 4 <= d.size()) {
        uint32_t l = d[pos] | (d[pos + 1] << 8) | (d[pos + 2] << 16) | ((uint32_t)d[pos + 3] << 24);
        pos += 4;
        out.emplace_back(d.begin() + pos, d.begin() + pos + l);
        pos += l;
    }
    return out;
}

static bool mdnsTest() {
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    std::vector<uint8_t> q = {0x12, 0x34, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    for (const char* l : {"_airplay", "_tcp", "local"}) {
        q.push_back((uint8_t)strlen(l));
        q.insert(q.end(), l, l + strlen(l));
    }
    q.push_back(0);
    q.insert(q.end(), {0, 12, 0, 1});
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(5353);
    to.sin_addr.s_addr = htonl(0x7f000001);
    sendto(s, (const char*)q.data(), (int)q.size(), 0, (sockaddr*)&to, sizeof(to));
    uint8_t buf[4096];
    bool ok = false;
    if (sock_wait_readable(s, 2000) > 0) {
        int n = recvfrom(s, (char*)buf, sizeof(buf), 0, nullptr, nullptr);
        int an = (buf[6] << 8) | buf[7], ar = (buf[10] << 8) | buf[11];
        std::string raw((char*)buf, n);
        printf("  mDNS response: %d bytes, %d answers, %d additional\n", n, an, ar);
        ok = n > 0 && buf[0] == 0x12 && buf[1] == 0x34 && an >= 1 && ar >= 3 && raw.find("features=0x5A7FFEE6") != std::string::npos &&
             raw.find("Test Receiver") != std::string::npos;
    }
    sock_close(s);
    return ok;
}

int main(int argc, char** argv) {
    std::string media = argc > 1 ? argv[1] : "testmedia";
    int audioCt = argc > 2 ? atoi(argv[2]) : 2;
    int externalPort = argc > 3 ? atoi(argv[3]) : 0;
    std::string mode = argc > 4 ? argv[4] : "mirror";
    int seconds = argc > 5 ? atoi(argv[5]) : 0;
    bool external = externalPort != 0;
    log_set_level(LogLevel::Info);
    net_init();

    // ---------------------------------------------------------- start receiver
    ReceiverConfig cfg;
    cfg.name = "Test Receiver";
    random_bytes(cfg.edSeed, 32);
    cfg.port = 7100;
    TestListener L;
    Receiver rx;
    L.rx = &rx;
    printf("== starting receiver\n");
    uint16_t rtspPort = (uint16_t)externalPort;
    if (!external) {
        CHECK(rx.start(cfg, &L), "receiver started on port %u", rx.port());
        rtspPort = rx.port();
        sleep_ms(300);
        printf("== discovery\n");
        CHECK(mdnsTest(), "mDNS answers _airplay._tcp PTR query with SRV/TXT/A");
    }

    Rtsp c;
    CHECK(c.connect(rtspPort), "TCP connect to RTSP port");

    // ---------------------------------------------------------- /info
    printf("== /info\n");
    {
        auto q = Plist::makeDict();
        auto arr = Plist::makeArray();
        arr->append(Plist::makeString("txtAirPlay"));
        q->set("qualifier", arr);
        auto r = c.request("GET", "/info", plist_write_binary(q), "application/x-apple-binary-plist");
        auto p = plist_parse_binary(r.body.data(), r.body.size());
        CHECK(r.code == 200 && p && p->getData("txtAirPlay"), "GET /info (qualifier txtAirPlay) returns TXT record");
    }
    uint8_t serverPk[32] = {0};
    {
        auto r = c.request("GET", "/info", {}, "");
        auto p = plist_parse_binary(r.body.data(), r.body.size());
        CHECK(r.code == 200 && p, "GET /info returns plist (%zu bytes)", r.body.size());
        if (p) {
            auto pk = p->getData("pk");
            CHECK(pk && pk->size() == 32, "info.pk is 32 bytes");
            if (pk) memcpy(serverPk, pk->data(), 32);
            CHECK(p->getUInt("features") == 0x5A7FFEE6ULL, "info.features = 0x5A7FFEE6");
            auto d = p->get("displays");
            CHECK(d && d->isArray() && d->array.size() == 1 && d->array[0]->getUInt("width") == 1920, "info.displays 1920 wide");
            if (!external) CHECK(p->getString("name") == "Test Receiver", "info.name");
        }
    }

    // ---------------------------------------------------------- pairing
    printf("== legacy pairing\n");
    uint8_t edSeed[32], edSecret[64], edPub[32];
    random_bytes(edSeed, 32);
    crypto_ed25519_key_pair(edSecret, edPub, edSeed);
    {
        auto r = c.request("POST", "/pair-setup", std::vector<uint8_t>(edPub, edPub + 32), "application/octet-stream");
        CHECK(r.code == 200 && r.body.size() == 32 && memcmp(r.body.data(), serverPk, 32) == 0, "pair-setup returns server Ed25519 key");
    }
    uint8_t xSecret[32], xPub[32], shared[32];
    x25519_keypair(xSecret, xPub);
    uint8_t serverX[32];
    {
        std::vector<uint8_t> body = {1, 0, 0, 0};
        body.insert(body.end(), xPub, xPub + 32);
        body.insert(body.end(), edPub, edPub + 32);
        auto r = c.request("POST", "/pair-verify", body, "application/octet-stream");
        CHECK(r.code == 200 && r.body.size() == 96, "pair-verify step 1 returns key + signature");
        memcpy(serverX, r.body.data(), 32);
        x25519_shared(xSecret, serverX, shared);
        uint8_t h[64], key[16], iv[16];
        sha512_2((const uint8_t*)"Pair-Verify-AES-Key", 19, shared, 32, h); memcpy(key, h, 16);
        sha512_2((const uint8_t*)"Pair-Verify-AES-IV", 18, shared, 32, h); memcpy(iv, h, 16);
        AesCtr ctr;
        ctr.init(key, iv);
        uint8_t sig[64];
        memcpy(sig, r.body.data() + 32, 64);
        ctr.process(sig, 64);
        uint8_t msg[64];
        memcpy(msg, serverX, 32);
        memcpy(msg + 32, xPub, 32);
        CHECK(ed25519_verify(serverPk, msg, 64, sig), "server signature verifies with advertised pk");
        // step 2: our signature, encrypted with the continuing keystream
        memcpy(msg, xPub, 32);
        memcpy(msg + 32, serverX, 32);
        uint8_t mysig[64];
        crypto_ed25519_sign(mysig, edSecret, msg, 64);
        ctr.process(mysig, 64);
        std::vector<uint8_t> b2 = {0, 0, 0, 0};
        b2.insert(b2.end(), mysig, mysig + 64);
        auto r2 = c.request("POST", "/pair-verify", b2, "application/octet-stream");
        CHECK(r2.code == 200, "pair-verify step 2 accepted (code %d)", r2.code);
    }

    // ---------------------------------------------------------- FairPlay
    printf("== FairPlay\n");
    uint8_t keymsg[164];
    {
        std::vector<uint8_t> m1 = {'F', 'P', 'L', 'Y', 3, 1, 1, 0, 0, 0, 0, 4, 2, 0, 2, 0xbb};
        auto r = c.request("POST", "/fp-setup", m1, "application/octet-stream");
        CHECK(r.code == 200 && r.body.size() == 142 && memcmp(r.body.data(), "FPLY", 4) == 0 && r.body[13] == 2, "fp-setup phase 1 (mode 2)");
        random_bytes(keymsg, 164);
        const uint8_t fphdr[13] = {'F', 'P', 'L', 'Y', 3, 1, 3, 0, 0, 0, 0, 0x98, 2};
        memcpy(keymsg, fphdr, 13);
        auto r2 = c.request("POST", "/fp-setup", std::vector<uint8_t>(keymsg, keymsg + 164), "application/octet-stream");
        CHECK(r2.code == 200 && r2.body.size() == 32 && memcmp(r2.body.data() + 12, keymsg + 144, 20) == 0, "fp-setup phase 2");
    }

    // ---------------------------------------------------------- SETUP 1
    printf("== SETUP (keys + timing)\n");
    uint8_t ekey[72], eiv[16];
    random_bytes(ekey, 72);
    random_bytes(eiv, 16);
    uint8_t aeskey[16];
    mn_fairplay_decrypt(keymsg, ekey, aeskey);
    {
        uint8_t h[64];
        sha512_2(aeskey, 16, shared, 32, h);
        memcpy(aeskey, h, 16);
    }
    uint16_t myTimingPort = 0;
    sock_t timing = udp_bind(0, &myTimingPort);
    {
        auto q = Plist::makeDict();
        q->set("ekey", Plist::makeData(ekey, 72));
        q->set("eiv", Plist::makeData(eiv, 16));
        q->set("timingProtocol", Plist::makeString("NTP"));
        q->set("timingPort", Plist::makeInt(myTimingPort));
        q->set("name", Plist::makeString(external ? "Nassim\u2019s iPhone" : "Test iPhone"));
        q->set("model", Plist::makeString("iPhone15,2"));
        q->set("deviceID", Plist::makeString("AA:BB:CC:DD:EE:FF"));
        q->set("osVersion", Plist::makeString("18.1"));
        q->set("isScreenMirroringSession", Plist::makeBool(true));
        auto r = c.request("SETUP", "rtsp://127.0.0.1/1234", plist_write_binary(q), "application/x-apple-binary-plist");
        auto p = plist_parse_binary(r.body.data(), r.body.size());
        CHECK(r.code == 200 && p && p->getUInt("timingPort") > 0, "SETUP 1 returns timingPort");
        if (!external) CHECK(L.sessionStarts == 1, "session start event");
        bool gotNtp = false;
        if (sock_wait_readable(timing, 3000) > 0) {
            uint8_t b[64];
            int n = recvfrom(timing, (char*)b, sizeof(b), 0, nullptr, nullptr);
            gotNtp = n == 32 && b[0] == 0x80 && b[1] == 0xd2;
        }
        CHECK(gotNtp, "receiver sends NTP timing requests to the sender");
    }

    // ---------------------------------------------------------- SETUP mirror
    printf("== SETUP mirroring stream\n");
    uint64_t scid = 0xF123456789ABCDEFULL;  // > 2^63: must be formatted unsigned
    uint16_t mirrorPort = 0;
    if (mode != "audio") {
        auto q = Plist::makeDict();
        auto streams = Plist::makeArray();
        auto st = Plist::makeDict();
        st->set("type", Plist::makeInt(110));
        st->set("streamConnectionID", Plist::makeInt(scid));
        st->set("timestampInfo", Plist::makeArray());
        streams->append(st);
        q->set("streams", streams);
        auto r = c.request("SETUP", "rtsp://127.0.0.1/1234", plist_write_binary(q), "application/x-apple-binary-plist");
        auto p = plist_parse_binary(r.body.data(), r.body.size());
        if (p && p->get("streams") && !p->get("streams")->array.empty()) mirrorPort = (uint16_t)p->get("streams")->array[0]->getUInt("dataPort");
        CHECK(r.code == 200 && mirrorPort > 0, "mirror dataPort = %u", mirrorPort);
    }

    // ---------------------------------------------------------- SETUP audio
    printf("== SETUP audio stream (ct=%d)\n", audioCt);
    uint16_t audioPort = 0, controlPort = 0;
    {
        auto q = Plist::makeDict();
        auto streams = Plist::makeArray();
        auto st = Plist::makeDict();
        st->set("type", Plist::makeInt(96));
        st->set("ct", Plist::makeInt((uint64_t)audioCt));
        st->set("spf", Plist::makeInt(audioCt == 2 ? 352 : 1024));
        st->set("controlPort", Plist::makeInt(50000));
        st->set("usingScreen", Plist::makeBool(true));
        streams->append(st);
        q->set("streams", streams);
        auto r = c.request("SETUP", "rtsp://127.0.0.1/1234", plist_write_binary(q), "application/x-apple-binary-plist");
        auto p = plist_parse_binary(r.body.data(), r.body.size());
        if (p && p->get("streams") && !p->get("streams")->array.empty()) {
            audioPort = (uint16_t)p->get("streams")->array[0]->getUInt("dataPort");
            controlPort = (uint16_t)p->get("streams")->array[0]->getUInt("controlPort");
        }
        CHECK(r.code == 200 && audioPort && controlPort, "audio dataPort=%u controlPort=%u", audioPort, controlPort);
    }
    {
        auto r = c.request("RECORD", "rtsp://127.0.0.1/1234", {}, "");
        CHECK(r.code == 200 && r.head.find("Audio-Latency") != std::string::npos, "RECORD");
        auto r2 = c.request("SET_PARAMETER", "rtsp://127.0.0.1/1234", std::vector<uint8_t>({'v','o','l','u','m','e',':',' ','-','1','5','.','0','\r','\n'}), "text/parameters");
        sleep_ms(50);
        if (!external) CHECK(r2.code == 200 && fabs(L.volume - (-15.0f)) < 0.01, "SET_PARAMETER volume = %.1f dB", (float)L.volume);
        auto r3 = c.request("GET_PARAMETER", "rtsp://127.0.0.1/1234", std::vector<uint8_t>({'v','o','l','u','m','e','\r','\n'}), "text/parameters");
        CHECK(r3.code == 200 && std::string(r3.body.begin(), r3.body.end()).find("volume: -15") != std::string::npos, "GET_PARAMETER volume");
        // dmap metadata
        auto tag = [](const char* t, const std::string& v) {
            std::vector<uint8_t> o(t, t + 4);
            uint32_t l = (uint32_t)v.size();
            o.push_back((uint8_t)(l >> 24)); o.push_back((uint8_t)(l >> 16)); o.push_back((uint8_t)(l >> 8)); o.push_back((uint8_t)l);
            o.insert(o.end(), v.begin(), v.end());
            return o;
        };
        auto inner = tag("minm", "Song Title");
        auto a = tag("asar", "The Artist");
        auto al = tag("asal", "An Album");
        inner.insert(inner.end(), a.begin(), a.end());
        inner.insert(inner.end(), al.begin(), al.end());
        auto mlit = tag("mlit", std::string(inner.begin(), inner.end()));
        c.request("SET_PARAMETER", "rtsp://127.0.0.1/1234", mlit, "application/x-dmap-tagged");
        c.request("SET_PARAMETER", "rtsp://127.0.0.1/1234", std::vector<uint8_t>(1234, 0xAB), "image/jpeg");
        if (!external) CHECK(L.title == "Song Title" && L.artist == "The Artist" && L.album == "An Album", "DMAP metadata parsed");
        if (!external) CHECK(L.artworkBytes == 1234, "artwork delivered");
    }

    // ---------------------------------------------------------- send video
    printf("== mirroring video\n");
    int framesSent = 0;
    if (mode != "audio") {
        sock_t v = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(mirrorPort);
        a.sin_addr.s_addr = htonl(0x7f000001);
        CHECK(::connect(v, (sockaddr*)&a, sizeof(a)) == 0, "connect mirror data port");
        // keys
        char skey[64], siv[64];
        snprintf(skey, sizeof(skey), "AirPlayStreamKey%llu", (unsigned long long)scid);
        snprintf(siv, sizeof(siv), "AirPlayStreamIV%llu", (unsigned long long)scid);
        uint8_t hk[64], hv[64];
        sha512_2((const uint8_t*)skey, strlen(skey), aeskey, 16, hk);
        sha512_2((const uint8_t*)siv, strlen(siv), aeskey, 16, hv);
        AesCtr ctr;
        ctr.init(hk, hv);

        auto h264 = readFile(media + "/video.h264");
        // split annex-b into NALs
        std::vector<std::vector<uint8_t>> nals;
        size_t i = 0, start = std::string::npos;
        while (i + 3 <= h264.size()) {
            bool sc3 = h264[i] == 0 && h264[i + 1] == 0 && h264[i + 2] == 1;
            if (sc3) {
                if (start != std::string::npos) {
                    size_t endp = i;
                    if (endp > start && h264[endp - 1] == 0) endp--;
                    nals.emplace_back(h264.begin() + start, h264.begin() + endp);
                }
                start = i + 3;
                i += 3;
            } else i++;
        }
        if (start != std::string::npos) nals.emplace_back(h264.begin() + start, h264.end());
        std::vector<uint8_t> sps, pps, pendingNonVcl;
        auto sendPacket = [&](uint8_t type, const std::vector<uint8_t>& payload, uint8_t option) {
            uint8_t hdr[128] = {0};
            uint32_t n = (uint32_t)payload.size();
            hdr[0] = (uint8_t)n; hdr[1] = (uint8_t)(n >> 8); hdr[2] = (uint8_t)(n >> 16); hdr[3] = (uint8_t)(n >> 24);
            hdr[4] = type;
            hdr[6] = option;
            hdr[7] = type == 1 ? 1 : 0;
            if (type == 1) {
                float w = 640, h = 360;
                memcpy(&hdr[56], &w, 4);
                memcpy(&hdr[60], &h, 4);
            }
            sock_send_all(v, hdr, 128);
            sock_send_all(v, payload.data(), payload.size());
        };
        bool sentConfig = false;
        int passes = seconds ? (seconds + 2) / 3 : 1;  // the clip is 3 s long; loop it on the same connection
        for (int pass = 0; pass < passes; pass++)
        for (auto& nal : nals) {
            if (nal.empty()) continue;
            int t = nal[0] & 0x1f;
            if (t == 7) { sps = nal; continue; }
            if (t == 8) { pps = nal; continue; }
            if (t == 9) continue;
            if (t == 6) {  // SEI: send along with next frame
                uint32_t l = (uint32_t)nal.size();
                pendingNonVcl.push_back((uint8_t)(l >> 24)); pendingNonVcl.push_back((uint8_t)(l >> 16));
                pendingNonVcl.push_back((uint8_t)(l >> 8)); pendingNonVcl.push_back((uint8_t)l);
                pendingNonVcl.insert(pendingNonVcl.end(), nal.begin(), nal.end());
                continue;
            }
            if (t == 1 || t == 5) {
                if (t == 5 && !sps.empty() && !pps.empty() && !sentConfig) {
                    std::vector<uint8_t> avcc = {1, sps[1], sps[2], sps[3], 0xFF, 0xE1, (uint8_t)(sps.size() >> 8), (uint8_t)sps.size()};
                    avcc.insert(avcc.end(), sps.begin(), sps.end());
                    avcc.push_back(1);
                    avcc.push_back((uint8_t)(pps.size() >> 8));
                    avcc.push_back((uint8_t)pps.size());
                    avcc.insert(avcc.end(), pps.begin(), pps.end());
                    sendPacket(1, avcc, 0x16);
                    sentConfig = true;
                }
                std::vector<uint8_t> frame = pendingNonVcl;
                pendingNonVcl.clear();
                uint32_t l = (uint32_t)nal.size();
                frame.push_back((uint8_t)(l >> 24)); frame.push_back((uint8_t)(l >> 16));
                frame.push_back((uint8_t)(l >> 8)); frame.push_back((uint8_t)l);
                frame.insert(frame.end(), nal.begin(), nal.end());
                ctr.process(frame.data(), frame.size());
                sendPacket(0, frame, 0);
                framesSent++;
                if (framesSent % 10 == 0) {  // heartbeat-ish report packet
                    sendPacket(5, std::vector<uint8_t>(20, 0), 0);
                }
                sleep_ms(seconds ? 33 : 5);
            }
        }
        sleep_ms(500);
        printf("  sent %d frames\n", framesSent);
        if (!external) CHECK(L.frames >= framesSent - 2, "decoded %d / %d frames", (int)L.frames, framesSent);
        if (!external) CHECK(L.lastW == 640 && L.lastH == 360, "decoded size %dx%d", L.lastW, L.lastH);
        printf("  center pixel BGRA = %d %d %d %d\n", L.centerPixel[0], L.centerPixel[1], L.centerPixel[2], L.centerPixel[3]);
        sock_close(v);
    }

    // ---------------------------------------------------------- send audio
    printf("== audio (RTP, AES-CBC)\n");
    {
        auto pkts = readPackets(media + (audioCt == 2 ? "/alac_packets.bin" : "/aac_packets.bin"));
        sock_t u = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(audioPort);
        to.sin_addr.s_addr = htonl(0x7f000001);
        uint16_t seq = 65500;  // exercise wrap-around
        uint32_t ts = 0;
        int spf = audioCt == 2 ? 352 : 1024;
        std::vector<std::vector<uint8_t>> rtp;
        for (auto& p : pkts) {
            std::vector<uint8_t> r = {0x80, 0x60, (uint8_t)(seq >> 8), (uint8_t)seq,
                                      (uint8_t)(ts >> 24), (uint8_t)(ts >> 16), (uint8_t)(ts >> 8), (uint8_t)ts, 0, 0, 0, 0};
            std::vector<uint8_t> enc = p;
            size_t n16 = enc.size() / 16 * 16;
            aes_cbc_encrypt(aeskey, eiv, enc.data(), n16);
            r.insert(r.end(), enc.begin(), enc.end());
            rtp.push_back(r);
            seq++;
            ts += spf;
        }
        // send with duplicates and a swapped pair to exercise the reorder buffer
        if (external && seconds) {
            int spfms = audioCt == 2 ? 8 : 23;
            for (int rep = 0; rep < seconds / 3; rep++)
                for (auto& p : pkts) {
                    std::vector<uint8_t> enc = p;
                    aes_cbc_encrypt(aeskey, eiv, enc.data(), enc.size() / 16 * 16);
                    std::vector<uint8_t> r = {0x80, 0x60, (uint8_t)(seq >> 8), (uint8_t)seq, (uint8_t)(ts >> 24), (uint8_t)(ts >> 16), (uint8_t)(ts >> 8), (uint8_t)ts, 0, 0, 0, 0};
                    r.insert(r.end(), enc.begin(), enc.end());
                    sendto(u, (const char*)r.data(), (int)r.size(), 0, (sockaddr*)&to, sizeof(to));
                    seq++; ts += spf;
                    sleep_ms(spfms);
                }
        }
        for (size_t k = 0; k < rtp.size() && !(external && seconds); k++) {
            size_t idx = k;
            if (k % 50 == 10) idx = k + 1;
            else if (k % 50 == 11) idx = k - 1;
            sendto(u, (const char*)rtp[idx].data(), (int)rtp[idx].size(), 0, (sockaddr*)&to, sizeof(to));
            sendto(u, (const char*)rtp[idx].data(), (int)rtp[idx].size(), 0, (sockaddr*)&to, sizeof(to));
            if (k % 8 == 0) sleep_ms(2);
        }
        sleep_ms(500);
        long expected = (long)pkts.size() * spf;
        double rms = L.pcmFrames > 0 ? sqrt(L.pcmEnergy / (2.0 * L.pcmFrames)) : 0;
        printf("  PCM frames %ld (expected ~%ld), RMS %.0f\n", (long)L.pcmFrames, expected, rms);
        if (!external) CHECK(L.pcmFrames >= expected * 9 / 10 && L.pcmFrames <= expected + spf * 2, "audio decoded without duplicates");
        double lo = audioCt == 2 ? 11000 : 1500, hi = audioCt == 2 ? 12000 : 3200;  // ALAC file: amplitude 0.5, AAC file: ffmpeg sine default 1/8
        CHECK(rms > lo && rms < hi, "audio content is the 440 Hz test tone (rms %.0f)", rms);
        sock_close(u);
    }

    // ---------------------------------------------------------- teardown
    printf("== teardown\n");
    {
        auto mk = [](int type) {
            auto q = Plist::makeDict();
            auto streams = Plist::makeArray();
            auto st = Plist::makeDict();
            st->set("type", Plist::makeInt((uint64_t)type));
            streams->append(st);
            q->set("streams", streams);
            return plist_write_binary(q);
        };
        auto r1 = c.request("TEARDOWN", "rtsp://127.0.0.1/1234", mk(110), "application/x-apple-binary-plist");
        sleep_ms(100);
        if (!external) CHECK(r1.code == 200 && L.mirrorStops == 1, "TEARDOWN mirror");
        auto r2 = c.request("TEARDOWN", "rtsp://127.0.0.1/1234", mk(96), "application/x-apple-binary-plist");
        sleep_ms(100);
        if (!external) CHECK(r2.code == 200 && L.audioStops == 1, "TEARDOWN audio");
        auto r3 = c.request("TEARDOWN", "rtsp://127.0.0.1/1234", {}, "");
        sleep_ms(100);
        if (!external) CHECK(r3.code == 200 && L.sessionEnds == 1, "TEARDOWN session");
    }
    sock_close(c.s);
    sock_close(timing);
    rx.stop();
    printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
