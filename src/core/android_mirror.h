// Mirrorly - wired Android mirroring (USB, through adb)
//
// When an Android phone with USB debugging is plugged in, Mirrorly pushes the scrcpy
// server (Apache 2.0, https://github.com/Genymobile/scrcpy) to it, starts it with
// app_process, and reads its H.264 video and raw PCM audio through adb port forwarding.
// Frames and sound go through the same pipeline as AirPlay, so the app shows them the
// same way.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "adb.h"
#include "mdns.h"
#include "receiver.h"

namespace mn {

// Version of the scrcpy server we bundle; the server refuses to start if this doesn't match.
constexpr const char* kScrcpyServerVersion = "5.0.1";
// onAudioStart() codec value for the 48 kHz 16-bit stereo PCM sent by Android phones.
constexpr int kAudioPcm48k = 0x100;

struct AndroidConfig {
    bool enabled = true;   // the whole Android engine (adb)
    bool usb = true;       // mirror phones plugged in with a cable
    int maxSize = 1920;   // longest edge in pixels (0 = phone's native size)
    int maxFps = 60;
    bool audio = true;
    int videoBitRate = 12000000;
    bool wireless = true;  // connect to paired phones over Wi-Fi (Wireless debugging, Android 11+)
};

enum class AndroidState {
    Off,           // disabled in Settings
    NoAdb,         // the adb server could not be started
    Waiting,       // nothing plugged in (or USB debugging is off)
    Unauthorized,  // plugged in, waiting for "Allow USB debugging" on the phone
    Offline,       // plugged in but not ready yet
    Ready,         // plugged in, idle because the user closed the mirror
    Connecting,
    Mirroring,
    Failed,        // could not start on this phone (detail says why)
};

struct AndroidStatus {
    AndroidState state = AndroidState::Off;
    std::string device;  // phone name, when known
    std::string detail;  // error text for Failed / NoAdb
    bool wireless = false;  // the phone is connected over Wi-Fi, not USB
};

// Pairing a phone over Wi-Fi: the phone scans a QR code (Developer options > Wireless
// debugging > Pair device with QR code), then advertises a pairing service over mDNS.
struct PairingStatus {
    enum Phase { Idle, Waiting, Pairing, Connecting, Done, Failed } phase = Idle;
    std::string qr;      // text to show as a QR code while Waiting
    // "Pair device with pairing code" on the phone: its address, when a phone is offering it
    // (then the user types the 6-digit code the phone shows)
    std::string codeTarget;
    std::string device;  // phone name once known
    std::string detail;  // error text for Failed
};

class AndroidStatusListener {
public:
    virtual ~AndroidStatusListener() {}
    virtual void onAndroidStatus(const AndroidStatus&) = 0;
    virtual void onPairingStatus(const PairingStatus&) {}
};

class AndroidMirror {
public:
    explicit AndroidMirror(Receiver& rx);
    ~AndroidMirror();

    // Starts an adb server when none answers (e.g. runs the bundled adb.exe start-server).
    void setAdbLauncher(std::function<bool()> launcher) { launcher_ = launcher; }
    // The scrcpy server pushed to the phone.
    void setServerJar(std::vector<uint8_t> jar) { jar_ = std::move(jar); }
    void setAdbPort(uint16_t port) { adb_ = AdbClient(port); }

    bool start(const AndroidConfig& cfg, AndroidStatusListener* statusListener);
    void stop();
    // New settings apply to the next connection; disabling ends the current one.
    void updateConfig(const AndroidConfig& cfg);
    // The user closed the mirror: leave the phone alone until it is plugged in again.
    void disconnect();
    // The user wants to mirror the plugged-in phone again.
    void mirrorAgain();
    bool active() const { return sessionActive_; }
    // Wi-Fi pairing with a QR code; the status comes back through onPairingStatus.
    void startPairing();
    void cancelPairing();
    // Pairs with the phone showing "Pair device with pairing code", using the code it displays.
    void pairWithCode(const std::string& code);
    static bool isWireless(const std::string& serial);
    AndroidStatus status();

private:
    void watchLoop();
    void evaluate();
    void publishStatus();
    void startSession(const AdbDevice& d);
    void stopSession(bool dismiss);
    void sessionMain(std::string serial, std::string model);
    bool runSession(const std::string& serial, std::string& name, std::string& error);
    void audioLoop(sock_t s);
    void wirelessTick();
    void setPairing(PairingStatus::Phase phase, const std::string& device, const std::string& detail);

    Receiver& rx_;
    AdbClient adb_;
    std::function<bool()> launcher_;
    std::vector<uint8_t> jar_;
    AndroidStatusListener* statusListener_ = nullptr;

    std::mutex m_;
    AndroidConfig cfg_;
    std::vector<AdbDevice> devices_;
    std::set<std::string> dismissed_;
    struct Failure { int count = 0; uint64_t retryAt = 0; std::string error; };
    std::map<std::string, Failure> failures_;
    bool adbOk_ = true;
    std::string adbError_;
    AndroidStatus last_;
    bool haveLast_ = false;

    // Wi-Fi
    MdnsBrowser browser_;
    PairingStatus pair_;
    std::string pairName_, pairCode_;
    std::string typedCode_;          // set by pairWithCode, consumed by the watcher thread
    std::string codeTargetHp_;       // ip:port of the phone offering code pairing
    uint64_t pairStarted_ = 0;
    uint32_t pairedIp_ = 0;
    uint64_t autoStartUntil_ = 0;  // wireless phones that appear before this start mirroring
    std::map<std::string, uint64_t> connectTried_;
    uint64_t lastWirelessTick_ = 0;

    volatile bool running_ = false;
    std::thread watchThread_;

    // current session
    std::thread sessionThread_;
    volatile bool sessRunning_ = false;
    std::atomic<bool> stopRequested_{false};   // stopped by us (user, settings, takeover), not by the phone
    std::atomic<bool> sessionActive_{false};   // thread alive
    std::atomic<bool> sessionDone_{false};     // thread finished, waiting to be joined
    std::atomic<bool> mirroring_{false};       // video flowing to the app
    std::string sessSerial_, sessModel_, sessName_;
    std::mutex sockM_;
    std::vector<sock_t> socks_;   // to unblock reads on stop
};

}  // namespace mn
