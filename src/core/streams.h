// Mirrorly - AirPlay media streams (mirroring video over TCP, audio over RTP/UDP, NTP timing)
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "crypto.h"
#include "platform.h"
#include "receiver.h"

namespace mn {

// Sends NTP timing requests to the sender's timing port (AirPlay legacy timing protocol).
class NtpClient {
public:
    ~NtpClient() { stop(); }
    bool start(uint32_t remote_ip_be, uint16_t remote_port);
    void stop();
    uint16_t localPort() const { return port_; }
private:
    void loop();
    sock_t sock_ = MN_INVALID_SOCKET;
    uint16_t port_ = 0;
    uint32_t remoteIp_ = 0;
    uint16_t remotePort_ = 0;
    volatile bool running_ = false;
    std::thread thread_;
};

class MirrorStream {
public:
    MirrorStream(Receiver* rx) : rx_(rx) {}
    ~MirrorStream() { stop(); }
    bool start(const uint8_t aeskey[16], uint64_t streamConnectionId);
    void stop();
    uint16_t port() const { return port_; }
    uint64_t lastDataMs() const { return lastData_; }
private:
    void loop();
    void handleVideo(std::vector<uint8_t>& payload);
    void decodeAndPublish(const uint8_t* annexb, size_t len);
    Receiver* rx_;
    sock_t listenSock_ = MN_INVALID_SOCKET;
    uint16_t port_ = 0;
    volatile bool running_ = false;
    std::thread thread_;
    AesCtr ctr_;
    std::vector<uint8_t> spsPps_;  // Annex-B SPS+PPS waiting to be prepended
    void* decoder_ = nullptr;
    bool paused_ = false;
    std::atomic<uint64_t> lastData_{0};
    uint64_t frameSeq_ = 0;
    int lastW_ = 0, lastH_ = 0;
};

class AudioStream {
public:
    AudioStream(Receiver* rx) : rx_(rx) {}
    ~AudioStream() { stop(); }
    bool start(const uint8_t aeskey[16], const uint8_t aesiv[16], int ct, uint32_t remote_ip_be, uint16_t remoteControlPort);
    void stop();
    void flush();
    uint16_t dataPort() const { return dataPort_; }
    uint16_t controlPort() const { return controlPort_; }
    int ct() const { return ct_; }
private:
    void loop();
    void onRtp(const uint8_t* pkt, size_t len);
    void process(const std::vector<uint8_t>& payload);
    Receiver* rx_;
    sock_t dataSock_ = MN_INVALID_SOCKET, controlSock_ = MN_INVALID_SOCKET;
    uint16_t dataPort_ = 0, controlPort_ = 0;
    uint8_t key_[16], iv_[16];
    int ct_ = 0;
    volatile bool running_ = false;
    std::thread thread_;
    void* decoder_ = nullptr;
    std::mutex flushMutex_;
    bool flushRequested_ = false;
    bool haveExpected_ = false;
    uint16_t expected_ = 0;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> pending_;
    std::vector<int16_t> pcm_;
};

// One sender session (created by the first SETUP of a connection).
class Session {
public:
    Session(Receiver* rx) : rx_(rx) {}
    ~Session() { shutdown(); }
    void shutdown();

    Receiver* rx_;
    ClientInfo client;
    uint8_t aeskey[16] = {0};
    uint8_t aesiv[16] = {0};
    uint32_t remoteIp = 0;
    std::unique_ptr<NtpClient> ntp;
    std::unique_ptr<MirrorStream> mirror;
    std::unique_ptr<AudioStream> audio;
    std::mutex mutex;

    void stopMirror();
    void stopAudio();
};

}  // namespace mn
