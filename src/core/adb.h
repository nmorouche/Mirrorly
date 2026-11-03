// Mirrorly - minimal client for the adb server's host protocol (TCP, usually 127.0.0.1:5037)
//
// Talks to an already running adb server, so no adb process is needed per command:
// device tracking, file push (sync protocol), port forwarding and shell commands.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "platform.h"

namespace mn {

struct AdbDevice {
    std::string serial;
    std::string state;  // "device", "unauthorized", "offline", "authorizing", ...
    std::string model;  // from "model:" (underscores replaced by spaces), may be empty
};

class AdbClient {
public:
    explicit AdbClient(uint16_t port = 5037) : port_(port) {}
    uint16_t port() const { return port_; }

    // Returns the server's protocol version, or -1 if no server answers.
    int serverVersion();

    // Opens "host:track-devices-l". Returns the socket (caller closes) or invalid.
    sock_t trackDevices();
    // Reads one device-list update from a tracking socket. False when the socket closes.
    static bool readDeviceList(sock_t s, std::vector<AdbDevice>& out, const volatile bool* running);

    bool push(const std::string& serial, const std::vector<uint8_t>& data, const std::string& remotePath, int mode = 0644);
    bool forward(const std::string& serial, uint16_t localPort, const std::string& remote);
    bool killForward(const std::string& serial, uint16_t localPort);
    // Starts a shell command and returns the socket streaming its output (caller closes).
    sock_t shell(const std::string& serial, const std::string& command);

    // Wireless debugging (Android 11+). Both return the server's reply text, e.g.
    // "Successfully paired to 192.168.1.20:37099 [guid=...]" or "connected to 192.168.1.20:41235".
    bool pair(const std::string& hostPort, const std::string& code, std::string& reply);
    bool connect(const std::string& hostPort, std::string& reply);

    // Asks the adb server to exit.
    void killServer();

    std::string lastError() const { return error_; }

    static std::vector<AdbDevice> parseDeviceList(const std::string& text);

private:
    sock_t connectServer();
    bool request(sock_t s, const std::string& service, int timeoutMs = 10000);  // sends + reads OKAY/FAIL
    bool query(const std::string& service, std::string& reply, int timeoutMs);
    sock_t openTransport(const std::string& serial);
    uint16_t port_;
    std::string error_;
};

}  // namespace mn
