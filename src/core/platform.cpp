// Mirrorly - cross-platform socket / system layer
#include "platform.h"

#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <iphlpapi.h>
#include <bcrypt.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <fcntl.h>
#include <sys/time.h>
#ifdef __linux__
#include <netpacket/packet.h>
#endif
#endif

namespace mn {

bool net_init() {
#ifdef _WIN32
    static bool done = false;
    if (done) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    done = true;
#endif
    return true;
}

void sock_close(sock_t s) {
    if (s == MN_INVALID_SOCKET) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

void sock_shutdown(sock_t s) {
    if (s == MN_INVALID_SOCKET) return;
#ifdef _WIN32
    shutdown(s, SD_BOTH);
#else
    shutdown(s, SHUT_RDWR);
#endif
}

bool sock_set_reuse(sock_t s) {
    int one = 1;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one)) == 0;
}

bool sock_set_nodelay(sock_t s) {
    int one = 1;
    return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one)) == 0;
}

bool sock_set_keepalive(sock_t s) {
    int one = 1;
    return setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&one, sizeof(one)) == 0;
}

int sock_last_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

int sock_wait_readable(sock_t s, int timeout_ms) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select((int)s + 1, &rfds, nullptr, nullptr, &tv);
    if (r < 0) return -1;
    return r > 0 ? 1 : 0;
}

bool sock_recv_all(sock_t s, uint8_t* buf, size_t len, const volatile bool* running) {
    size_t got = 0;
    while (got < len) {
        if (running && !*running) return false;
        int w = sock_wait_readable(s, 200);
        if (w < 0) return false;
        if (w == 0) continue;
        int r = recv(s, (char*)buf + got, (int)(len - got), 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

bool sock_send_all(sock_t s, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int r = send(s, (const char*)buf + sent, (int)(len - sent), 0);
#else
        int r = (int)send(s, buf + sent, len - sent, MSG_NOSIGNAL);
#endif
        if (r <= 0) return false;
        sent += (size_t)r;
    }
    return true;
}

static uint16_t local_port(sock_t s) {
    sockaddr_in a{};
    socklen_t l = sizeof(a);
    if (getsockname(s, (sockaddr*)&a, &l) != 0) return 0;
    return ntohs(a.sin_port);
}

sock_t tcp_listen(uint16_t port, uint16_t* actual_port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == MN_INVALID_SOCKET) return s;
#ifndef _WIN32
    sock_set_reuse(s);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 8) != 0) {
        sock_close(s);
        return MN_INVALID_SOCKET;
    }
    if (actual_port) *actual_port = local_port(s);
    return s;
}

sock_t udp_bind(uint16_t port, uint16_t* actual_port) {
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == MN_INVALID_SOCKET) return s;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
        sock_close(s);
        return MN_INVALID_SOCKET;
    }
    int rcvbuf = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));
    udp_disable_connreset(s);
    if (actual_port) *actual_port = local_port(s);
    return s;
}

void udp_disable_connreset(sock_t s) {
#ifdef _WIN32
    BOOL off = FALSE;
    DWORD ret = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);  // SIO_UDP_CONNRESET
#else
    (void)s;
#endif
}

std::vector<NetInterface> list_ipv4_interfaces() {
    std::vector<NetInterface> out;
#ifdef _WIN32
    ULONG size = 32 * 1024;
    std::vector<uint8_t> buf(size);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    }
    if (r != NO_ERROR) return out;
    for (auto* ad = (IP_ADAPTER_ADDRESSES*)buf.data(); ad; ad = ad->Next) {
        if (ad->OperStatus != IfOperStatusUp) continue;
        if (ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (ad->NoMulticast) continue;
        for (auto* ua = ad->FirstUnicastAddress; ua; ua = ua->Next) {
            auto* sa = (sockaddr_in*)ua->Address.lpSockaddr;
            if (sa->sin_family != AF_INET) continue;
            NetInterface ni{};
            char nm[256];
            WideCharToMultiByte(CP_UTF8, 0, ad->FriendlyName, -1, nm, sizeof(nm), nullptr, nullptr);
            ni.name = nm;
            ni.addr = sa->sin_addr.s_addr;
            ULONG mask = 0;
            ConvertLengthToIpv4Mask(ua->OnLinkPrefixLength, &mask);
            ni.netmask = mask;
            ni.has_mac = ad->PhysicalAddressLength == 6;
            if (ni.has_mac) memcpy(ni.mac, ad->PhysicalAddress, 6);
            // skip link-local 169.254/16
            uint32_t h = ntohl(ni.addr);
            if ((h >> 16) == 0xA9FE) continue;
            out.push_back(ni);
        }
    }
#else
    ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) != 0) return out;
    for (ifaddrs* p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (p->ifa_flags & IFF_LOOPBACK) continue;
        if (!(p->ifa_flags & IFF_UP)) continue;
        NetInterface ni{};
        ni.name = p->ifa_name;
        ni.addr = ((sockaddr_in*)p->ifa_addr)->sin_addr.s_addr;
        ni.netmask = p->ifa_netmask ? ((sockaddr_in*)p->ifa_netmask)->sin_addr.s_addr : 0;
        ni.has_mac = false;
        out.push_back(ni);
    }
#ifdef __linux__
    for (ifaddrs* p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_PACKET) continue;
        auto* ll = (sockaddr_ll*)p->ifa_addr;
        for (auto& ni : out)
            if (ni.name == p->ifa_name && ll->sll_halen == 6) {
                memcpy(ni.mac, ll->sll_addr, 6);
                ni.has_mac = true;
            }
    }
#endif
    freeifaddrs(ifa);
#endif
    return out;
}

std::string ip_to_string(uint32_t addr_be) {
    char buf[INET_ADDRSTRLEN] = {0};
    in_addr a;
    a.s_addr = addr_be;
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

void random_bytes(uint8_t* buf, size_t len) {
#ifdef _WIN32
    BCryptGenRandom(nullptr, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t r = fread(buf, 1, len, f);
        (void)r;
        fclose(f);
    }
#endif
}

uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint64_t ntp_now() {
    using namespace std::chrono;
    auto us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    uint64_t secs = (uint64_t)(us / 1000000) + 2208988800ULL;
    uint64_t frac = ((uint64_t)(us % 1000000) << 32) / 1000000ULL;
    return (secs << 32) | frac;
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

std::string hex_encode(const uint8_t* data, size_t len, bool upper) {
    static const char* lo = "0123456789abcdef";
    static const char* up = "0123456789ABCDEF";
    const char* d = upper ? up : lo;
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        s.push_back(d[data[i] >> 4]);
        s.push_back(d[data[i] & 15]);
    }
    return s;
}

std::vector<uint8_t> hex_decode(const std::string& hex) {
    std::vector<uint8_t> out;
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int a = v(hex[i]), b = v(hex[i + 1]);
        if (a < 0 || b < 0) break;
        out.push_back((uint8_t)(a * 16 + b));
    }
    return out;
}

}  // namespace mn
