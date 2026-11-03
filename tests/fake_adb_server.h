// Mirrorly test helper: a fake adb server with one fake Android phone behind it.
//
// Speaks the adb host protocol (version, track-devices-l, transport, sync push, shell,
// forward / killforward) and, once a port is forwarded, plays the part of the scrcpy
// server on the phone: dummy byte, device name, H.264 video packets (from an Annex-B
// file) and raw 48 kHz PCM audio.
#pragma once
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform.h"

namespace fakeadb {

using namespace mn;

static inline void put32be(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static inline void put64be(uint8_t* p, uint64_t v) { put32be(p, (uint32_t)(v >> 32)); put32be(p + 4, (uint32_t)v); }

class FakeAdb {
public:
    std::string serial = "FAKE0001";
    std::string model = "Fake_Phone";
    std::string deviceName = "Fake Phone";
    std::string videoPath;
    bool audio = true;          // false: the phone reports "no audio" (Android < 11)
    bool loopVideo = false;     // keep streaming the file in a loop (for UI tests)
    int fps = 60;

    // stats
    std::atomic<int> pushes{0}, forwards{0}, killForwards{0}, shells{0}, videoConns{0}, audioConns{0};
    std::atomic<long> pushedBytes{0};
    std::atomic<int> streamsClosed{0};
    std::string lastShell;

    bool start(uint16_t port) {
        listen_ = tcp_listen(port, &port_);
        if (listen_ == MN_INVALID_SOCKET) return false;
        running_ = true;
        thread_ = std::thread([this]() { acceptLoop(); });
        return true;
    }
    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        sock_close(listen_);
        std::lock_guard<std::mutex> lk(m_);
        for (auto& t : threads_)
            if (t.joinable()) t.join();
        threads_.clear();
    }
    ~FakeAdb() { stop(); }
    uint16_t port() const { return port_; }

    // "" (nothing plugged), "unauthorized", "device", "offline"
    void setState(const std::string& st) {
        std::lock_guard<std::mutex> lk(m_);
        state_ = st;
        listVersion_++;
    }
    // Simulates the phone ending the stream (e.g. the scrcpy server dies).
    void dropStreams() { dropGen_++; }

    // Wireless debugging: "host:pair:<code>:<ip:port>" succeeds with this code; once paired,
    // "host:connect:<ip:port>" adds the phone as a Wi-Fi device with serial <ip:port>.
    std::string pairCode;
    std::atomic<int> pairs{0}, connects{0};
    std::string wifiSerial() {
        std::lock_guard<std::mutex> lk(m_);
        return wifiSerial_;
    }
    void dropWifi() {
        std::lock_guard<std::mutex> lk(m_);
        wifiSerial_.clear();
        listVersion_++;
    }

private:
    std::string listText() {
        std::lock_guard<std::mutex> lk(m_);
        std::string t;
        if (state_ == "device")
            t += serial + "\tdevice usb:1-1 product:fake model:" + model + " device:fake transport_id:3\n";
        else if (!state_.empty())
            t += serial + "\t" + state_ + " usb:1-1 transport_id:3\n";
        if (!wifiSerial_.empty())
            t += wifiSerial_ + "\tdevice product:fake model:" + model + " device:fake transport_id:4\n";
        return t;
    }
    // which known serial a "host-serial:<serial>:..." / "host:transport:<serial>" request names
    std::string matchSerial(const std::string& rest) {
        std::lock_guard<std::mutex> lk(m_);
        if (!wifiSerial_.empty() && rest.compare(0, wifiSerial_.size(), wifiSerial_) == 0) return wifiSerial_;
        if (rest.compare(0, serial.size(), serial) == 0) return serial;
        return "";
    }

    void spawn(std::function<void()> f) {
        std::lock_guard<std::mutex> lk(m_);
        threads_.emplace_back(std::move(f));
    }

    void acceptLoop() {
        while (running_) {
            if (sock_wait_readable(listen_, 100) <= 0) continue;
            sock_t c = accept(listen_, nullptr, nullptr);
            if (c == MN_INVALID_SOCKET) continue;
            spawn([this, c]() { handle(c); });
        }
    }

    static bool sendStr(sock_t s, const std::string& x) { return sock_send_all(s, (const uint8_t*)x.data(), x.size()); }
    static std::string hex4(size_t n) {
        char b[8];
        snprintf(b, sizeof(b), "%04x", (unsigned)n);
        return b;
    }
    bool readReq(sock_t s, std::string& out) {
        char h[5] = {0};
        if (!sock_recv_all(s, (uint8_t*)h, 4, &running_)) return false;
        unsigned n = 0;
        if (sscanf(h, "%4x", &n) != 1) return false;
        out.assign(n, '\0');
        return !n || sock_recv_all(s, (uint8_t*)&out[0], n, &running_);
    }

    void handle(sock_t c) {
        std::string req;
        if (!readReq(c, req)) {
            sock_close(c);
            return;
        }
        if (req == "host:version") {
            sendStr(c, "OKAY00040029");
        } else if (req == "host:track-devices-l") {
            sendStr(c, "OKAY");
            int seen = -1;
            while (running_) {
                int v = listVersion_;
                if (v != seen) {
                    seen = v;
                    std::string t = listText();
                    if (!sendStr(c, hex4(t.size()) + t)) break;
                }
                // detect the client hanging up
                if (sock_wait_readable(c, 50) != 0) {
                    char b;
                    if (recv(c, &b, 1, 0) <= 0) break;
                }
            }
        } else if (req.compare(0, 10, "host:pair:") == 0) {
            // host:pair:<code>:<ip:port>
            std::string rest = req.substr(10);
            std::string code = rest.substr(0, rest.find(':')), hp = rest.substr(rest.find(':') + 1);
            bool good = code == pairCode || pairCode == "*";
            std::string r = good ? "Successfully paired to " + hp + " [guid=adb-FAKEWIFI]" : "Failed: Wrong password or connection was dropped.";
            if (good) {
                pairs++;
                paired_ = true;
            }
            sendStr(c, "OKAY" + hex4(r.size()) + r);
        } else if (req.compare(0, 13, "host:connect:") == 0) {
            std::string hp = req.substr(13), r;
            connects++;
            if (paired_) {
                std::lock_guard<std::mutex> lk(m_);
                wifiSerial_ = hp;
                listVersion_++;
                r = "connected to " + hp;
            } else {
                r = "failed to connect to '" + hp + "': Connection refused";
            }
            sendStr(c, "OKAY" + hex4(r.size()) + r);
        } else if (req.compare(0, 15, "host:transport:") == 0 && !matchSerial(req.substr(15)).empty()) {
            sendStr(c, "OKAY");
            std::string svc;
            if (readReq(c, svc)) {
                if (svc == "sync:") {
                    sendStr(c, "OKAY");
                    doSync(c);
                } else if (svc.compare(0, 6, "shell:") == 0) {
                    sendStr(c, "OKAY");
                    shells++;
                    {
                        std::lock_guard<std::mutex> lk(m_);
                        lastShell = svc.substr(6);
                    }
                    sendStr(c, "[server] INFO: Device: [fake] Fake Phone (Android 14)\n");
                    // stay open until the client closes it
                    while (running_) {
                        int w = sock_wait_readable(c, 100);
                        if (w < 0) break;
                        if (w > 0) {
                            char b[64];
                            if (recv(c, b, sizeof(b), 0) <= 0) break;
                        }
                    }
                } else {
                    std::string m = "unknown service";
                    sendStr(c, "FAIL" + hex4(m.size()) + m);
                }
            }
        } else if (req.compare(0, 12, "host-serial:") == 0 && !matchSerial(req.substr(12)).empty()) {
            std::string rest = req.substr(12 + matchSerial(req.substr(12)).size() + 1);
            if (rest.compare(0, 12, "forward:tcp:") == 0) {
                int port = atoi(rest.c_str() + 12);
                sendStr(c, "OKAYOKAY");
                forwards++;
                startPhone((uint16_t)port);
            } else if (rest.compare(0, 16, "killforward:tcp:") == 0) {
                killForwards++;
                sendStr(c, "OKAY");
            } else {
                sendStr(c, "FAIL0007unknown");
            }
        } else if (req.compare(0, 15, "host:transport:") == 0) {
            std::string m = "device '" + req.substr(15) + "' not found";
            sendStr(c, "FAIL" + hex4(m.size()) + m);
        } else {
            sendStr(c, "FAIL0007unknown");
        }
        sock_close(c);
    }

    void doSync(sock_t c) {
        uint8_t h[8];
        while (running_ && sock_recv_all(c, h, 8, &running_)) {
            uint32_t n = h[4] | (h[5] << 8) | (h[6] << 16) | ((uint32_t)h[7] << 24);
            if (!memcmp(h, "SEND", 4)) {
                std::vector<uint8_t> spec(n);
                if (n && !sock_recv_all(c, spec.data(), n, &running_)) return;
            } else if (!memcmp(h, "DATA", 4)) {
                std::vector<uint8_t> d(n);
                if (n && !sock_recv_all(c, d.data(), n, &running_)) return;
                pushedBytes += n;
            } else if (!memcmp(h, "DONE", 4)) {
                pushes++;
                uint8_t ok[8] = {'O', 'K', 'A', 'Y', 0, 0, 0, 0};
                sock_send_all(c, ok, 8);
            } else if (!memcmp(h, "QUIT", 4)) {
                return;
            } else {
                return;
            }
        }
    }

    // ------------------------------------------------------------ the "phone"
    void startPhone(uint16_t port) {
        sock_t l = tcp_listen(port, nullptr);
        if (l == MN_INVALID_SOCKET) return;
        int gen = dropGen_;
        spawn([this, l, gen]() {
            auto acceptOne = [&](int ms) {
                uint64_t until = now_ms() + (uint64_t)ms;
                while (running_ && now_ms() < until)
                    if (sock_wait_readable(l, 50) > 0) return accept(l, nullptr, nullptr);
                return (sock_t)MN_INVALID_SOCKET;
            };
            sock_t video = acceptOne(10000);
            if (video == MN_INVALID_SOCKET) {
                sock_close(l);
                return;
            }
            videoConns++;
            // the audio socket (if the client wants sound) connects right after the video one
            std::thread at([this, l, gen, acceptOne]() mutable {
                sock_t aud = acceptOne(1500);
                sock_close(l);
                if (aud == MN_INVALID_SOCKET) return;
                audioConns++;
                audioStream(aud, gen);
            });
            videoStream(video, gen);
            at.join();
            streamsClosed++;
        });
    }

    void videoStream(sock_t s, int gen) {
        uint8_t b = 0;
        sock_send_all(s, &b, 1);  // dummy byte (tunnel_forward)
        uint8_t meta[64] = {0};
        snprintf((char*)meta, sizeof(meta), "%s", deviceName.c_str());
        sock_send_all(s, meta, 64);
        uint8_t id[4] = {'h', '2', '6', '4'};
        sock_send_all(s, id, 4);

        std::ifstream in(videoPath, std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // split into NAL units
        std::vector<std::pair<size_t, size_t>> nals;
        for (size_t i = 0; i + 3 < d.size();) {
            if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
                size_t st = (i > 0 && d[i - 1] == 0) ? i - 1 : i;
                if (!nals.empty()) nals.back().second = st;
                nals.push_back({st, d.size()});
                i += 3;
            } else {
                i++;
            }
        }
        bool ok = true;
        uint64_t pts = 0;
        do {
            std::vector<uint8_t> cfg, frame;
            for (size_t k = 0; ok && k < nals.size() && running_ && gen == dropGen_; k++) {
                size_t st = nals[k].first, en = nals[k].second;
                size_t sc = d[st + 2] == 1 ? 3 : 4;
                int type = d[st + sc] & 0x1f;
                if (type == 7 || type == 8) {
                    if (type == 7 && !cfg.empty()) cfg.clear();
                    cfg.insert(cfg.end(), d.begin() + st, d.begin() + en);
                    continue;
                }
                frame.insert(frame.end(), d.begin() + st, d.begin() + en);
                if (type != 1 && type != 5) continue;
                uint8_t h[12];
                if (!cfg.empty()) {
                    uint8_t sh[12] = {0x80, 0, 0, 0};  // session packet before each new configuration
                    put32be(sh + 4, 1080);
                    put32be(sh + 8, 2400);
                    ok = sock_send_all(s, sh, 12);
                    put64be(h, 1ull << 62);
                    put32be(h + 8, (uint32_t)cfg.size());
                    ok = ok && sock_send_all(s, h, 12) && sock_send_all(s, cfg.data(), cfg.size());
                    cfg.clear();
                }
                put64be(h, pts | (type == 5 ? (1ull << 61) : 0));
                put32be(h + 8, (uint32_t)frame.size());
                ok = ok && sock_send_all(s, h, 12) && sock_send_all(s, frame.data(), frame.size());
                frame.clear();
                pts += 1000000 / fps;
                sleep_ms(1000 / fps);
            }
        } while (ok && loopVideo && running_ && gen == dropGen_);
        // like a phone with a static screen: keep the connection until it is closed
        while (ok && running_ && gen == dropGen_) {
            int w = sock_wait_readable(s, 100);
            if (w < 0) break;
            if (w > 0) {
                char x;
                if (recv(s, &x, 1, 0) <= 0) break;
            }
        }
        sock_close(s);
    }

    void audioStream(sock_t s, int gen) {
        if (!audio) {
            uint8_t z[4] = {0, 0, 0, 0};
            sock_send_all(s, z, 4);
            sock_close(s);
            return;
        }
        uint8_t id[4] = {0, 'r', 'a', 'w'};
        if (!sock_send_all(s, id, 4)) return;
        const int n = 480;  // 10 ms
        std::vector<int16_t> pcm(n * 2);
        uint64_t pts = 0;
        double ph = 0;
        while (running_ && gen == dropGen_) {
            for (int i = 0; i < n; i++) {
                int16_t v = (int16_t)(8000 * sin(ph));
                ph += 2 * 3.14159265358979 * 440 / 48000;
                pcm[i * 2] = pcm[i * 2 + 1] = v;
            }
            uint8_t h[12];
            put64be(h, pts);
            put32be(h + 8, n * 4);
            if (!sock_send_all(s, h, 12) || !sock_send_all(s, (const uint8_t*)pcm.data(), n * 4)) break;
            pts += 10000;
            sleep_ms(10);
        }
        sock_close(s);
    }

    sock_t listen_ = MN_INVALID_SOCKET;
    uint16_t port_ = 0;
    volatile bool running_ = false;
    std::thread thread_;
    std::mutex m_;
    std::vector<std::thread> threads_;
    std::string state_;
    std::atomic<int> listVersion_{0};
    std::atomic<int> dropGen_{0};
    std::atomic<bool> paired_{false};
    std::string wifiSerial_;
};

}  // namespace fakeadb
