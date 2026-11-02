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
    std::string client, model;
    std::vector<AndroidState> states;
    std::string lastDevice;
    int lastW = 0, lastH = 0;

    void onSessionStart(const ClientInfo& c) override {
        sessionStarts++;
        std::lock_guard<std::mutex> lk(m);
        client = c.name;
        model = c.model;
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
    am.stop();
    CHECK(L.sessionEnds == 5, "stop() ends the session");

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
