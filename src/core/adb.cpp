// Mirrorly - adb host protocol client
#include "adb.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <sstream>

#include "log.h"

namespace mn {

static const uint32_t kLocalhost = htonl(0x7F000001);

static bool sendStr(sock_t s, const std::string& str) {
    return sock_send_all(s, (const uint8_t*)str.data(), str.size());
}

static bool readExact(sock_t s, void* buf, size_t n, int timeoutMs = 10000) {
    // bounded wait so a stuck server can't hang the caller forever
    uint8_t* p = (uint8_t*)buf;
    size_t got = 0;
    uint64_t deadline = now_ms() + (uint64_t)timeoutMs;
    while (got < n) {
        if (now_ms() > deadline) return false;
        int w = sock_wait_readable(s, 200);
        if (w < 0) return false;
        if (w == 0) continue;
        int r = recv(s, (char*)p + got, (int)(n - got), 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static bool readHexLen(sock_t s, size_t& len, const volatile bool* running = nullptr) {
    char h[5] = {0};
    bool ok = running ? sock_recv_all(s, (uint8_t*)h, 4, running) : readExact(s, h, 4);
    if (!ok) return false;
    unsigned v = 0;
    if (sscanf(h, "%4x", &v) != 1) return false;
    len = v;
    return true;
}

static void put32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

sock_t AdbClient::connectServer() {
    sock_t s = tcp_connect(kLocalhost, port_, 1500);
    if (s == MN_INVALID_SOCKET) error_ = "the adb server is not running";
    return s;
}

bool AdbClient::request(sock_t s, const std::string& service, int timeoutMs) {
    char hdr[8];
    snprintf(hdr, sizeof(hdr), "%04x", (unsigned)service.size());
    if (!sendStr(s, std::string(hdr, 4) + service)) {
        error_ = "adb: send failed";
        return false;
    }
    char st[4];
    if (!readExact(s, st, 4, timeoutMs)) {
        error_ = "adb: no reply to " + service;
        return false;
    }
    if (memcmp(st, "OKAY", 4) == 0) return true;
    if (memcmp(st, "FAIL", 4) == 0) {
        size_t n = 0;
        std::string msg;
        if (readHexLen(s, n) && n < 4096) {
            msg.resize(n);
            if (n) readExact(s, &msg[0], n);
        }
        error_ = "adb: " + msg;
        return false;
    }
    error_ = "adb: unexpected reply to " + service;
    return false;
}

int AdbClient::serverVersion() {
    sock_t s = connectServer();
    if (s == MN_INVALID_SOCKET) return -1;
    int v = -1;
    size_t n = 0;
    if (request(s, "host:version") && readHexLen(s, n) && n == 4) {
        char h[5] = {0};
        unsigned u = 0;
        if (readExact(s, h, 4) && sscanf(h, "%4x", &u) == 1) v = (int)u;
    }
    sock_close(s);
    return v;
}

bool AdbClient::query(const std::string& service, std::string& reply, int timeoutMs) {
    reply.clear();
    sock_t s = connectServer();
    if (s == MN_INVALID_SOCKET) return false;
    bool ok = request(s, service, timeoutMs);
    size_t n = 0;
    char h[5] = {0};
    if (ok) {
        unsigned v = 0;
        ok = readExact(s, h, 4, timeoutMs) && sscanf(h, "%4x", &v) == 1;
        n = v;
    }
    if (ok && n) {
        reply.resize(n);
        ok = readExact(s, &reply[0], n, 2000);
    }
    if (!ok && reply.empty()) reply = error_.empty() ? "no answer from adb" : error_;
    sock_close(s);
    return ok;
}

bool AdbClient::pair(const std::string& hostPort, const std::string& code, std::string& reply) {
    bool ok = query("host:pair:" + code + ":" + hostPort, reply, 30000);
    return ok && reply.find("Successfully paired") != std::string::npos;
}

bool AdbClient::connect(const std::string& hostPort, std::string& reply) {
    bool ok = query("host:connect:" + hostPort, reply, 15000);
    return ok && (reply.compare(0, 12, "connected to") == 0 || reply.compare(0, 20, "already connected to") == 0);
}

void AdbClient::killServer() {
    sock_t s = connectServer();
    if (s == MN_INVALID_SOCKET) return;
    request(s, "host:kill");
    sock_close(s);
}

sock_t AdbClient::trackDevices() {
    sock_t s = connectServer();
    if (s == MN_INVALID_SOCKET) return s;
    if (!request(s, "host:track-devices-l")) {
        sock_close(s);
        return MN_INVALID_SOCKET;
    }
    return s;
}

std::vector<AdbDevice> AdbClient::parseDeviceList(const std::string& text) {
    std::vector<AdbDevice> out;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        AdbDevice d;
        // "<serial> <state> [key:value ...]"; the separator after the serial is a tab or spaces
        size_t p = line.find_first_of(" \t");
        if (p == std::string::npos) continue;
        d.serial = line.substr(0, p);
        size_t q = line.find_first_not_of(" \t", p);
        if (q == std::string::npos) continue;
        size_t e = line.find_first_of(" \t", q);
        d.state = line.substr(q, e == std::string::npos ? std::string::npos : e - q);
        if (d.state == "no") d.state = "no permissions";
        size_t m = line.find(" model:");
        if (m != std::string::npos) {
            size_t me = line.find(' ', m + 7);
            d.model = line.substr(m + 7, me == std::string::npos ? std::string::npos : me - m - 7);
            for (char& c : d.model)
                if (c == '_') c = ' ';
        }
        out.push_back(d);
    }
    return out;
}

bool AdbClient::readDeviceList(sock_t s, std::vector<AdbDevice>& out, const volatile bool* running) {
    size_t n = 0;
    if (!readHexLen(s, n, running)) return false;
    std::string text(n, '\0');
    if (n && !sock_recv_all(s, (uint8_t*)&text[0], n, running)) return false;
    out = parseDeviceList(text);
    return true;
}

sock_t AdbClient::openTransport(const std::string& serial) {
    sock_t s = connectServer();
    if (s == MN_INVALID_SOCKET) return s;
    if (!request(s, "host:transport:" + serial)) {
        sock_close(s);
        return MN_INVALID_SOCKET;
    }
    return s;
}

bool AdbClient::push(const std::string& serial, const std::vector<uint8_t>& data, const std::string& remotePath, int mode) {
    sock_t s = openTransport(serial);
    if (s == MN_INVALID_SOCKET) return false;
    bool ok = request(s, "sync:");
    if (ok) {
        // SEND <len> "<path>,<mode>"
        std::string spec = remotePath + "," + std::to_string(0100000 | mode);
        std::vector<uint8_t> msg(8 + spec.size());
        memcpy(&msg[0], "SEND", 4);
        put32le(&msg[4], (uint32_t)spec.size());
        memcpy(&msg[8], spec.data(), spec.size());
        ok = sock_send_all(s, msg.data(), msg.size());
        // DATA chunks (max 64 KiB each)
        for (size_t off = 0; ok && off < data.size(); off += 65536) {
            size_t n = std::min<size_t>(65536, data.size() - off);
            uint8_t h[8];
            memcpy(h, "DATA", 4);
            put32le(h + 4, (uint32_t)n);
            ok = sock_send_all(s, h, 8) && sock_send_all(s, data.data() + off, n);
        }
        if (ok) {
            uint8_t h[8];
            memcpy(h, "DONE", 4);
            put32le(h + 4, (uint32_t)time(nullptr));
            ok = sock_send_all(s, h, 8);
        }
        uint8_t r[8];
        if (ok) ok = readExact(s, r, 8, 30000);
        if (ok && memcmp(r, "OKAY", 4) != 0) {
            uint32_t n = (uint32_t)r[4] | ((uint32_t)r[5] << 8) | ((uint32_t)r[6] << 16) | ((uint32_t)r[7] << 24);
            std::string msg(n < 1024 ? n : 0, '\0');
            if (!msg.empty()) readExact(s, &msg[0], msg.size());
            error_ = "adb push failed: " + msg;
            ok = false;
        } else if (!ok && error_.empty()) {
            error_ = "adb push failed";
        }
        uint8_t quit[8] = {'Q', 'U', 'I', 'T', 0, 0, 0, 0};
        sock_send_all(s, quit, 8);
    }
    sock_close(s);
    return ok;
}

bool AdbClient::forward(const std::string& serial, uint16_t localPort, const std::string& remote) {
    sock_t s = connectServer();
    if (s == MN_INVALID_SOCKET) return false;
    bool ok = request(s, "host-serial:" + serial + ":forward:tcp:" + std::to_string(localPort) + ";" + remote);
    if (ok) {
        // newer servers send a second status once the listener is installed
        if (sock_wait_readable(s, 300) > 0) {
            char st[4];
            if (readExact(s, st, 4, 1000) && memcmp(st, "FAIL", 4) == 0) {
                error_ = "adb: forward failed";
                ok = false;
            }
        }
    }
    sock_close(s);
    return ok;
}

bool AdbClient::killForward(const std::string& serial, uint16_t localPort) {
    sock_t s = connectServer();
    if (s == MN_INVALID_SOCKET) return false;
    bool ok = request(s, "host-serial:" + serial + ":killforward:tcp:" + std::to_string(localPort));
    sock_close(s);
    return ok;
}

sock_t AdbClient::shell(const std::string& serial, const std::string& command) {
    sock_t s = openTransport(serial);
    if (s == MN_INVALID_SOCKET) return s;
    if (!request(s, "shell:" + command)) {
        sock_close(s);
        return MN_INVALID_SOCKET;
    }
    return s;
}

}  // namespace mn
