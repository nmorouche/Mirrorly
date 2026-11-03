// Stand-alone fake adb server + phone, for trying the app's wired-mirroring UI without a phone.
//
//   fake_adb video.h264 [port=5037] [script]
// script: a sequence of "<seconds>:<state>" steps, e.g. "0:unauthorized,4:device,30:"
// (state "" = unplugged; "clip=<text>" = the phone copies <text>). The video loops until the app
// disconnects.
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
            std::string st = step.substr(c + 1);
            if (st.compare(0, 5, "clip=") == 0)
                adb.phoneClipboard(st.substr(5));
            else
                adb.setState(st);
            printf("[%.1fs] state: %s\n", at, step.substr(c + 1).c_str());
            fflush(stdout);
        }
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    // print the control messages the app sends (touches, keys, text...)
    std::string pend;
    for (;;) {
        mn::sleep_ms(100);
        pend += adb.takeControlBytes();
        auto u32 = [&](size_t at) { return ((uint32_t)(uint8_t)pend[at] << 24) | ((uint32_t)(uint8_t)pend[at + 1] << 16) |
                                           ((uint32_t)(uint8_t)pend[at + 2] << 8) | (uint8_t)pend[at + 3]; };
        for (;;) {
            if (pend.empty()) break;
            uint8_t t = (uint8_t)pend[0];
            size_t need = t == 0 ? 14 : t == 1 ? 5 : t == 2 ? 32 : t == 3 ? 21 : t == 4 ? 2 : t == 9 ? 14 : t == 13 ? 5 : t == 14 ? 3 : 1;
            if (t == 12) need = 8;
            if (pend.size() < need) break;
            if (t == 1) need += u32(1);
            if (t == 9) need += u32(10);
            if (t == 12) need += (uint8_t)pend[7] + 2;
            if (t == 12 && pend.size() >= need) need += ((uint8_t)pend[need - 2] << 8) | (uint8_t)pend[need - 1];
            if (t == 13) need += ((uint8_t)pend[3] << 8) | (uint8_t)pend[4];
            if (pend.size() < need) break;
            const char* names[] = {"key", "text", "touch", "scroll", "back", "?", "?", "?", "?", "clipboard", "?", "?", "uhid-create", "uhid-input", "uhid-destroy"};
            if (t == 2)
                printf("control: touch action=%d pointer=%08x pos=%u,%u\n", pend[1], u32(6), u32(10), u32(14));
            else if (t == 0)
                printf("control: key action=%d keycode=%u meta=%x\n", pend[1], u32(2), u32(10));
            else if (t == 1)
                printf("control: text \"%s\"\n", pend.substr(5, u32(1)).c_str());
            else if (t == 9)
                printf("control: clipboard paste=%d \"%s\"\n", pend[9], pend.substr(14, u32(10)).c_str());
            else
                printf("control: %s\n", t < 15 ? names[t] : "?");
            fflush(stdout);
            pend.erase(0, need);
        }
    }
}
