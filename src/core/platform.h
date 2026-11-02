// Mirrorly - small cross-platform socket / system layer (Windows + POSIX)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define MN_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
typedef int sock_t;
#define MN_INVALID_SOCKET (-1)
#endif

namespace mn {

bool net_init();
void sock_close(sock_t s);
void sock_shutdown(sock_t s);
bool sock_set_reuse(sock_t s);
bool sock_set_nodelay(sock_t s);
bool sock_set_keepalive(sock_t s);
int sock_last_error();
// waits until readable; returns 1 readable, 0 timeout, -1 error
int sock_wait_readable(sock_t s, int timeout_ms);
// Receives exactly len bytes (blocking with periodic stop checks). Returns false on EOF/error/stop.
bool sock_recv_all(sock_t s, uint8_t* buf, size_t len, const volatile bool* running);
bool sock_send_all(sock_t s, const uint8_t* buf, size_t len);

// Creates a listening TCP socket bound to 0.0.0.0:port (port 0 = ephemeral). Returns actual port.
sock_t tcp_listen(uint16_t port, uint16_t* actual_port);
// Connects to ip_be:port with a timeout. Returns an invalid socket on failure.
sock_t tcp_connect(uint32_t ip_be, uint16_t port, int timeout_ms);
// Creates a UDP socket bound to 0.0.0.0:port (0 = ephemeral). Returns actual port.
sock_t udp_bind(uint16_t port, uint16_t* actual_port);
// Windows: stop ICMP 'port unreachable' from turning into recvfrom errors on UDP sockets.
void udp_disable_connreset(sock_t s);

struct NetInterface {
    std::string name;
    std::string description;  // adapter description (Windows), e.g. "Apple Mobile Device Ethernet"
    uint32_t addr;     // network byte order
    uint32_t netmask;  // network byte order
    uint8_t mac[6];
    bool has_mac;
};
std::vector<NetInterface> list_ipv4_interfaces();
std::string ip_to_string(uint32_t addr_be);

void random_bytes(uint8_t* buf, size_t len);
uint64_t now_ms();           // monotonic milliseconds
uint64_t ntp_now();          // NTP 64-bit timestamp (seconds since 1900 << 32 | fraction)
void sleep_ms(int ms);

std::string hex_encode(const uint8_t* data, size_t len, bool upper = false);
std::vector<uint8_t> hex_decode(const std::string& hex);

}  // namespace mn
