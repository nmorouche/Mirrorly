// Stand-alone fake adb server + phone, for trying the app's wired-mirroring UI without a phone.
//
//   fake_adb video.h264 [port=5037] [script]
// script: a sequence of "<seconds>:<state>" steps, e.g. "0:unauthorized,4:device,30:"
// (state "" = unplugged). The video loops until the app disconnects.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "fake_adb_server.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: fake_adb video.h264 [port] [script]\n");
        return 2;
    }
    mn::net_init();
    fakeadb::FakeAdb adb;
    adb.videoPath = argv[1];
    adb.loopVideo = true;
    adb.model = "Pixel_8";
    adb.deviceName = "Pixel 8";
    adb.pairCode = "*";  // Wi-Fi pairing: accept any code
    uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : 5037;
    if (!adb.start(port)) {
        printf("cannot listen on %u\n", port);
        return 1;
    }
    printf("fake adb on %u\n", adb.port());
    fflush(stdout);
    std::string script = argc > 3 ? argv[3] : "0:unauthorized,4:device";
    uint64_t t0 = mn::now_ms();
    size_t pos = 0;
    while (pos <= script.size()) {
        size_t e = script.find(',', pos);
        std::string step = script.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        size_t c = step.find(':');
        if (c != std::string::npos) {
            double at = atof(step.substr(0, c).c_str());
            while (mn::now_ms() - t0 < (uint64_t)(at * 1000)) mn::sleep_ms(50);
            adb.setState(step.substr(c + 1));
            printf("[%.1fs] state: %s\n", at, step.substr(c + 1).c_str());
            fflush(stdout);
        }
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    for (;;) mn::sleep_ms(1000);
}
