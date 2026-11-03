// Mirrorly end-to-end test for wired Android mirroring: runs the receiver and the USB
// engine in-process against a fake adb server with a fake phone behind it.
//
//   ./out/linux/android_usb /path/to/video.h264
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "android_mirror.h"
#include "fake_adb_server.h"
#include "log.h"
#include "mdns.h"
#include "receiver.h"

using namespace mn;

static int failures = 0;
#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        if (cond) { printf("  PASS  "); printf(__VA_ARGS__); printf("\n"); }      \
        else { printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); failures++; } \
    } while (0)

struct Listener : ReceiverListener, AndroidStatusListener {
    Receiver* rx = nullptr;
    std::atomic<int> frames{0}, sessionStarts{0}, sessionEnds{0}, mirrorStarts{0}, mirrorStops{0}, audioStarts{0}, audioStops{0};
    std::atomic<int> audioCt{0};
    std::atomic<long> pcmFrames{0};
    std::atomic<double> energy{0};
    std::mutex m;
    std::string client, model, clientIp;
    std::atomic<int> pairPhase{0};
    std::string qr, pairDevice, pairDetail, codeTarget;
    bool lastWireless = false;
    std::vector<AndroidState> states;
    std::string lastDevice;
    int lastW = 0, lastH = 0;

    void onSessionStart(const ClientInfo& c) override {
        sessionStarts++;
        std::lock_guard<std::mutex> lk(m);
        client = c.name;
        model = c.model;
        clientIp = c.ip;
    }
    void onSessionEnd() override { sessionEnds++; }
    void onMirrorStart() override { mirrorStarts++; }
    void onMirrorStop() override { mirrorStops++; }
    void onAudioStart(int ct) override { audioStarts++; audioCt = ct; }
    void onAudioStop() override { audioStops++; }
    void onAudioPcm(const int16_t* pcm, int n) override {
        pcmFrames += n;
        double e = 0;
        for (int i = 0; i < n * 2; i++) e += (double)pcm[i] * pcm[i];
        energy = energy + e;
    }
    void onVideoFrame() override {
        frames++;
        const VideoFrame* f = rx->frames().acquireRead();
        if (f) {
            lastW = f->width;
            lastH = f->height;
        }
    }
    void onAndroidStatus(const AndroidStatus& s) override {
        static const char* names[] = {"Off", "NoAdb", "Waiting", "Unauthorized", "Offline", "Ready", "Connecting", "Mirroring", "Failed"};
        printf("  [status] %s %s %s\n", names[(int)s.state], s.device.c_str(), s.detail.c_str());
        std::lock_guard<std::mutex> lk(m);
        states.push_back(s.state);
        lastDevice = s.device;
        lastWireless = s.wireless;
    }
    void onPairingStatus(const PairingStatus& p) override {
        static const char* names[] = {"Idle", "Waiting", "Pairing", "Connecting", "Done", "Failed"};
        printf("  [pairing] %s %s %s%s%s\n", names[p.phase], p.device.c_str(), p.detail.c_str(),
               p.codeTarget.empty() ? "" : " code offered by ", p.codeTarget.c_str());
        std::lock_guard<std::mutex> lk(m);
        if (!p.qr.empty()) qr = p.qr;
        codeTarget = p.codeTarget;
        pairDevice = p.device;
        pairDetail = p.detail;
        pairPhase = p.phase;
    }
    bool saw(AndroidState st) {
        std::lock_guard<std::mutex> lk(m);
        for (auto s : states)
            if (s == st) return true;
        return false;
    }
    AndroidState last() {
        std::lock_guard<std::mutex> lk(m);
        return states.empty() ? AndroidState::Off : states.back();
    }
};

template <class F>
static bool waitFor(F cond, int ms) {
    for (int i = 0; i < ms / 20; i++) {
        if (cond()) return true;
        sleep_ms(20);
    }
    return cond();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: android_usb video.h264\n");
        return 2;
    }
    log_set_level(LogLevel::Info);
    net_init();

    fakeadb::FakeAdb adb;
    adb.videoPath = argv[1];
    CHECK(adb.start(0), "fake adb server on port %u", adb.port());

    ReceiverConfig rcfg;
    rcfg.name = "Test Receiver";
    rcfg.port = 7300;
    Listener L;
    Receiver rx;
    L.rx = &rx;
    CHECK(rx.start(rcfg, &L), "receiver started");

    AndroidMirror am(rx);
    am.setAdbPort(adb.port());
    am.setServerJar(std::vector<uint8_t>(200000, 0x5A));
    AndroidConfig cfg;
    cfg.maxSize = 1280;
    cfg.maxFps = 30;
    am.start(cfg, &L);

    printf("== nothing plugged in\n");
    CHECK(waitFor([&] { return L.last() == AndroidState::Waiting; }, 3000), "status: waiting for a phone");

    printf("== phone plugged in, USB debugging not yet allowed\n");
    adb.setState("unauthorized");
    CHECK(waitFor([&] { return L.last() == AndroidState::Unauthorized; }, 3000), "status: allow USB debugging on the phone");
    CHECK(adb.shells == 0, "nothing started before the phone allows it");

    printf("== user taps Allow on the phone\n");
    adb.setState("device");
    CHECK(waitFor([&] { return L.frames >= 20; }, 10000), "video frames decoded (%d)", (int)L.frames);
    CHECK(L.saw(AndroidState::Connecting) && L.last() == AndroidState::Mirroring, "status: connecting, then mirroring");
    CHECK(adb.pushes == 1 && adb.pushedBytes == 200000, "server pushed to the phone (%ld bytes)", (long)adb.pushedBytes);
    CHECK(adb.forwards == 1 && adb.killForwards == 1, "tunnel forwarded, then removed once connected");
    CHECK(adb.lastShell.find("com.genymobile.scrcpy.Server 5.0.1") != std::string::npos &&
              adb.lastShell.find("tunnel_forward=true") != std::string::npos &&
              adb.lastShell.find("max_size=1280") != std::string::npos && adb.lastShell.find("max_fps=30") != std::string::npos &&
              adb.lastShell.find("audio_codec=raw") != std::string::npos && adb.lastShell.find("control=false") != std::string::npos,
          "server command line: %s", adb.lastShell.c_str());
    CHECK(L.sessionStarts == 1 && L.client == "Fake Phone" && L.model == "Android", "session started for \"%s\" (%s)",
          L.client.c_str(), L.model.c_str());
    CHECK(L.mirrorStarts == 1 && L.lastW > 0 && L.lastH > 0, "mirroring %dx%d", L.lastW, L.lastH);
    CHECK(waitFor([&] { return L.pcmFrames > 48000 / 4; }, 3000), "audio: %ld frames of 48 kHz PCM", (long)L.pcmFrames);
    CHECK(L.audioStarts == 1 && L.audioCt == kAudioPcm48k, "audio started as 48 kHz PCM");
    double rms = sqrt(L.energy / std::max(1L, (long)L.pcmFrames * 2));
    CHECK(rms > 4000 && rms < 7000, "audio level matches the phone's sine (rms %.0f)", rms);

    printf("== user closes the mirror\n");
    am.disconnect();
    CHECK(waitFor([&] { return L.sessionEnds == 1 && L.mirrorStops == 1 && L.audioStops == 1; }, 3000), "session, video and audio ended");
    CHECK(waitFor([&] { return L.last() == AndroidState::Ready; }, 2000), "status: ready (phone still plugged in)");
    sleep_ms(1500);
    CHECK(L.sessionStarts == 1, "does not restart on its own");

    printf("== user asks to mirror again\n");
    int f0 = L.frames;
    am.mirrorAgain();
    CHECK(waitFor([&] { return L.frames > f0 + 10; }, 10000), "mirroring again (%d new frames)", L.frames - f0);
    CHECK(L.sessionStarts == 2 && adb.pushes == 2, "second session");

    printf("== the phone ends the stream\n");
    adb.dropStreams();
    CHECK(waitFor([&] { return L.sessionEnds == 2; }, 4000), "session ended when the phone stopped streaming");
    CHECK(waitFor([&] { return L.last() == AndroidState::Ready; }, 2000), "no restart loop: waits for the user");

    printf("== unplugged and plugged in again\n");
    adb.setState("");
    CHECK(waitFor([&] { return L.last() == AndroidState::Waiting; }, 3000), "status: waiting after unplugging");
    adb.setState("device");
    CHECK(waitFor([&] { return L.sessionStarts == 3 && L.frames > 0; }, 10000), "mirrors again on re-plug");

    printf("== Settings: wired mirroring turned off\n");
    AndroidConfig off = cfg;
    off.enabled = false;
    am.updateConfig(off);
    CHECK(waitFor([&] { return L.sessionEnds == 3 && L.last() == AndroidState::Off; }, 4000), "session stopped, status off");
    am.updateConfig(cfg);
    CHECK(waitFor([&] { return L.sessionStarts == 4; }, 10000), "turned back on: mirrors the plugged-in phone");

    printf("== phone without sound support (Android 10)\n");
    am.disconnect();
    waitFor([&] { return L.sessionEnds == 4; }, 3000);
    adb.audio = false;
    int a0 = L.audioStarts;
    f0 = L.frames;
    am.mirrorAgain();
    CHECK(waitFor([&] { return L.frames > f0 + 10; }, 10000), "video still mirrors");
    CHECK(L.audioStarts == a0, "no audio session");
    adb.audio = true;

    printf("== pairing over Wi-Fi (QR code)\n");
    am.disconnect();
    waitFor([&] { return L.sessionEnds == 5; }, 3000);
    adb.setState("");  // cable unplugged
    CHECK(waitFor([&] { return L.last() == AndroidState::Waiting; }, 3000), "unplugged");
    am.startPairing();
    CHECK(waitFor([&] { return L.pairPhase == PairingStatus::Waiting && !L.qr.empty(); }, 2000), "QR code: %s", L.qr.c_str());
    std::string svcName, code;
    {
        size_t a = L.qr.find(";S:"), b = L.qr.find(";P:");
        CHECK(L.qr.compare(0, 13, "WIFI:T:ADB;S:") == 0 && a != std::string::npos && b != std::string::npos, "QR uses the ADB pairing format");
        svcName = L.qr.substr(a + 3, b - a - 3);
        code = L.qr.substr(b + 3, L.qr.find(';', b + 3) - b - 3);
    }
    adb.pairCode = code;
    // the phone scanned the code: it advertises the pairing service under that name
    MdnsResponder phone;
    MdnsService pairing;
    pairing.instance = svcName;
    pairing.type = "_adb-tls-pairing._tcp";
    pairing.port = 37099;
    CHECK(phone.start("fake-android-phone", {pairing}), "phone advertises %s._adb-tls-pairing._tcp", svcName.c_str());
    CHECK(waitFor([&] { return adb.pairs == 1; }, 15000), "paired through adb with the code from the QR");
    // after pairing, the phone advertises its Wireless debugging connect service
    MdnsService conn;
    conn.instance = "adb-FAKEWIFI-x1y2z3";
    conn.type = "_adb-tls-connect._tcp";
    conn.port = 41000;
    phone.update({conn});
    int s0 = L.sessionStarts;
    CHECK(waitFor([&] { return L.pairPhase == PairingStatus::Done; }, 15000), "pairing done (%s)", L.pairDevice.c_str());
    CHECK(waitFor([&] { return L.sessionStarts == s0 + 1 && L.frames > 0; }, 10000) && L.clientIp == "Wi-Fi",
          "mirroring starts over Wi-Fi right after pairing (serial %s)", adb.wifiSerial().c_str());
    CHECK(adb.wifiSerial().find(":41000") != std::string::npos, "connected to the phone's Wireless debugging port");
    CHECK(adb.lastShell.find("video_bit_rate=8000000") != std::string::npos, "lower bit rate over Wi-Fi");

    printf("== Wi-Fi phone comes back later\n");
    am.disconnect();
    waitFor([&] { return L.sessionEnds == 6; }, 3000);
    adb.dropWifi();  // left the network
    CHECK(waitFor([&] { return L.last() == AndroidState::Waiting; }, 3000), "status: waiting");
    int c0 = adb.connects;
    CHECK(waitFor([&] { return !adb.wifiSerial().empty(); }, 15000) && adb.connects > c0, "reconnects to the paired phone on its own");
    CHECK(waitFor([&] { return L.last() == AndroidState::Ready && L.lastWireless; }, 3000),
          "status: available over Wi-Fi, waits for the user");
    sleep_ms(1000);
    CHECK(L.sessionStarts == s0 + 1, "no automatic mirroring over Wi-Fi");
    f0 = L.frames;
    am.mirrorAgain();
    CHECK(waitFor([&] { return L.sessionStarts == s0 + 2 && L.frames > f0 + 5; }, 10000), "\"Mirror\" starts it");
    phone.stop();

    printf("== pairing with a 6-digit code instead of the QR code\n");
    am.disconnect();
    waitFor([&] { return L.sessionEnds == 7; }, 3000);
    adb.dropWifi();
    adb.pairCode = "482913";
    am.startPairing();
    MdnsResponder phone2;
    MdnsService offer;
    offer.instance = "adb-FAKE2SERIAL-Qx7Lm2";  // what "Pair device with pairing code" advertises
    offer.type = "_adb-tls-pairing._tcp";
    offer.port = 38001;
    CHECK(phone2.start("fake-android-phone-2", {offer}), "phone offers pairing with a code");
    CHECK(waitFor([&] { std::lock_guard<std::mutex> lk(L.m); return !L.codeTarget.empty(); }, 15000), "Mirrorly sees the offer (%s)",
          L.codeTarget.c_str());
    int p0 = adb.pairs;
    am.pairWithCode("111111");
    CHECK(waitFor([&] { return L.pairPhase == PairingStatus::Failed; }, 10000) && adb.pairs == p0, "wrong code: %s", L.pairDetail.c_str());
    am.startPairing();
    CHECK(waitFor([&] { std::lock_guard<std::mutex> lk(L.m); return !L.codeTarget.empty() && L.pairPhase == PairingStatus::Waiting; }, 15000),
          "try again: offer still seen");
    am.pairWithCode("482913");
    CHECK(waitFor([&] { return adb.pairs == p0 + 1; }, 10000), "paired with the typed code");
    MdnsService conn2;
    conn2.instance = "adb-FAKE2SERIAL-c0nn";
    conn2.type = "_adb-tls-connect._tcp";
    conn2.port = 41001;
    phone2.update({conn2});
    int s1 = L.sessionStarts;
    CHECK(waitFor([&] { return L.pairPhase == PairingStatus::Done; }, 15000), "pairing done");
    CHECK(waitFor([&] { return L.sessionStarts == s1 + 1; }, 10000) && adb.wifiSerial().find(":41001") != std::string::npos,
          "mirroring over Wi-Fi after code pairing");
    phone2.stop();

    am.stop();
    CHECK(L.sessionEnds == 8, "stop() ends the session");

    printf("== adb not available\n");
    {
        AndroidMirror am2(rx);
        am2.setAdbPort(1);  // nothing listens there
        bool launched = false;
        am2.setAdbLauncher([&] { launched = true; return false; });
        am2.setServerJar({1});
        Listener L2;
        am2.start(cfg, &L2);
        CHECK(waitFor([&] { return L2.last() == AndroidState::NoAdb; }, 3000) && launched, "launcher tried, status: adb unavailable");
        am2.stop();
    }

    rx.stop();
    adb.stop();
    printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
