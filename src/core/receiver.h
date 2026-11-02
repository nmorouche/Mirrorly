// Mirrorly - AirPlay screen mirroring + audio receiver engine
#pragma once
#include <atomic>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "crypto.h"
#include "mdns.h"
#include "platform.h"

namespace mn {

struct ReceiverConfig {
    std::string name = "Mirrorly";
    uint8_t deviceId[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t edSeed[32] = {0};
    std::string pi = "2e388006-13ba-4041-9a67-25dd4a43d536";
    int width = 1920;
    int height = 1080;
    int maxFps = 60;
    bool audioEnabled = true;
    uint16_t port = 7000;
    std::string hostname;  // mDNS host label (without .local); generated if empty
};

struct ClientInfo {
    std::string name;
    std::string model;
    std::string deviceId;
    std::string osVersion;
    std::string ip;
};

struct VideoFrame {
    int width = 0, height = 0, stride = 0;
    std::vector<uint8_t> pixels;  // BGRA
    uint64_t seq = 0;
};

// Lock-light triple buffer between the decoder thread and the renderer.
class FrameExchange {
public:
    VideoFrame& beginWrite() { return bufs_[write_]; }
    void endWrite() {
        std::lock_guard<std::mutex> lk(m_);
        std::swap(write_, ready_);
        fresh_ = true;
    }
    // Returns the newest frame if one arrived since the last call (owned by caller until next call).
    const VideoFrame* acquireRead() {
        std::lock_guard<std::mutex> lk(m_);
        if (!fresh_) return nullptr;
        std::swap(read_, ready_);
        fresh_ = false;
        return &bufs_[read_];
    }
    const VideoFrame* current() const { return &bufs_[read_]; }
private:
    std::mutex m_;
    VideoFrame bufs_[3];
    int write_ = 0, ready_ = 1, read_ = 2;
    bool fresh_ = false;
};

class ReceiverListener {
public:
    virtual ~ReceiverListener() {}
    virtual void onClientConnecting(const ClientInfo&) {}
    virtual void onSessionStart(const ClientInfo&) {}
    virtual void onSessionEnd() {}
    virtual void onMirrorStart() {}
    virtual void onMirrorStop() {}
    virtual void onVideoFrame() {}            // a new frame is waiting in Receiver::frames()
    virtual void onVideoPaused(bool) {}
    virtual void onAudioStart(int /*ct*/) {}
    virtual void onAudioStop() {}
    virtual void onAudioPcm(const int16_t* /*interleaved stereo*/, int /*frames*/) {}
    virtual void onAudioFlush() {}
    virtual void onVolume(float /*db: -144 (mute) .. 0*/) {}
    virtual void onMetadata(const std::string& /*title*/, const std::string& /*artist*/, const std::string& /*album*/) {}
    virtual void onArtwork(const std::vector<uint8_t>& /*jpeg or png*/) {}
    virtual void onProgress(double /*position s*/, double /*duration s*/) {}
};

class Session;
class Connection;

class Receiver {
public:
    Receiver();
    ~Receiver();

    bool start(const ReceiverConfig& cfg, ReceiverListener* listener);
    void stop();
    bool running() const { return running_; }

    // Applies a new configuration (name / resolution / audio). Re-announces on the network.
    void updateConfig(const ReceiverConfig& cfg);
    ReceiverConfig config();

    void disconnectClient();          // drops the current sender
    // Called when an AirPlay sender starts a session, before it takes the screen
    // (lets another source, e.g. a USB phone, step aside).
    void setPreemptHook(std::function<void()> hook) {
        std::lock_guard<std::mutex> lk(hookMutex_);
        preempt_ = hook;
    }
    FrameExchange& frames() { return frames_; }
    uint16_t port() const { return port_; }
    std::string publicKeyHex() const;
    std::string lastError() const { return error_; }
    bool discoveryOk() const { return mdns_.running(); }

    // internals used by Connection / Session
    ReceiverListener* listener() { return listener_; }
    const Ed25519Key& edKey() const { return edKey_; }
    std::vector<uint8_t> airplayTxtRecord();
    std::vector<uint8_t> raopTxtRecord();
    uint64_t features() const;
    void activateSession(std::shared_ptr<Session> s, Connection* owner);
    void endSession(Connection* owner);
    std::shared_ptr<Session> activeSession(Connection* owner);

private:
    void acceptLoop();
    std::vector<MdnsService> buildServices();
    std::vector<std::string> airplayTxt();
    std::vector<std::string> raopTxt();

    ReceiverConfig cfg_;
    std::mutex cfgMutex_;
    ReceiverListener* listener_ = nullptr;
    Ed25519Key edKey_;
    MdnsResponder mdns_;
    sock_t listenSock_ = MN_INVALID_SOCKET;
    uint16_t port_ = 0;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::mutex connMutex_;
    std::vector<std::shared_ptr<Connection>> conns_;
    std::mutex sessionMutex_;
    std::shared_ptr<Session> session_;
    Connection* sessionOwner_ = nullptr;
    FrameExchange frames_;
    std::string error_;
    std::mutex hookMutex_;
    std::function<void()> preempt_;
};

}  // namespace mn
