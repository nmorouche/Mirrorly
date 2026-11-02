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

}  // namespace mn
