// Mirrorly - minimal multicast DNS / DNS-SD responder (RFC 6762 / 6763)
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform.h"

namespace mn {

struct MdnsService {
    std::string instance;               // e.g. "Living Room"
    std::string type;                   // e.g. "_airplay._tcp"
    uint16_t port = 0;
    std::vector<std::string> txt;       // "key=value"
};

class MdnsResponder {
public:
    MdnsResponder();
    ~MdnsResponder();

    // hostname without ".local"
    bool start(const std::string& hostname, const std::vector<MdnsService>& services);
    void stop();
    // Replace advertised services (sends goodbye for the old ones, then announces).
    void update(const std::vector<MdnsService>& services);
    bool running() const { return running_; }
    std::string lastError() const { return error_; }

private:
    void threadMain();
    void announce(bool goodbye);
    void refreshInterfaces();
    void handlePacket(const uint8_t* data, size_t len, const sockaddr_in& from);
    std::vector<uint8_t> buildResponse(const std::vector<int>& answerKinds, uint32_t iface_addr, bool goodbye,
                                       uint16_t id, const std::vector<uint8_t>* question, bool legacy);
    void sendMulticast(const std::vector<uint8_t>& pkt, uint32_t iface_addr);

    sock_t sock_ = MN_INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::string hostname_;
    std::vector<MdnsService> services_;
    std::vector<NetInterface> ifaces_;
    std::vector<uint32_t> joined_;
    uint64_t lastIfaceCheck_ = 0;
    int pendingAnnounces_ = 0;
    uint64_t nextAnnounce_ = 0;
    std::string error_;
};

// A service instance found on the network by MdnsBrowser.
struct MdnsFound {
    std::string instance;  // first label, e.g. "adb-1A2B3C-xyz"
    std::string type;      // e.g. "_adb-tls-connect._tcp"
    std::string host;      // target host name
    uint32_t ip = 0;       // IPv4, network byte order
    uint16_t port = 0;
    uint64_t seen = 0;     // now_ms() of the last answer
};

// Minimal DNS-SD browser: asks for some service types now and then and keeps what the
// network answers (and announces). Shares UDP 5353 with the responder.
class MdnsBrowser {
public:
    ~MdnsBrowser() { stop(); }
    bool start(const std::vector<std::string>& types);
    void stop();
    bool running() const { return running_; }
    void queryNow() { nextQuery_ = 0; }
    // Resolved instances of a type (address and port known), seen within maxAgeMs.
    std::vector<MdnsFound> find(const std::string& type, uint64_t maxAgeMs = 120000);

private:
    void loop();
    void sendQueries();
    void handlePacket(const uint8_t* d, size_t n, uint32_t fromIp);
    sock_t sock_ = MN_INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex m_;
    std::vector<std::string> types_;
    struct Inst {
        std::string label, type, host;
        uint16_t port = 0;
        uint64_t seen = 0;
        bool srv = false;
        uint32_t from = 0;     // address the answer came from (used when no A record is sent)
        bool logged = false;
    };
    std::vector<Inst> insts_;
    std::vector<std::pair<std::string, uint32_t>> hosts_;  // host name (lower case) -> ip
    std::vector<uint32_t> joined_;
    uint64_t nextQuery_ = 0, lastIfaceCheck_ = 0;
};

}  // namespace mn
