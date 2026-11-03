// Mirrorly - controlling an Android phone through the scrcpy control channel
//
// Touches, scrolling, keys, text, clipboard and virtual (UHID) game controllers are sent as
// scrcpy control messages on a third socket next to video and audio. Messages are queued and
// written by a background thread, so callers (the UI) never block on the network.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform.h"

namespace mn {

// Android constants used by the messages
namespace akey {
enum : int {
    ACTION_DOWN = 0, ACTION_UP = 1,
    HOME = 3, BACK = 4, KEY_0 = 7, DPAD_UP = 19, DPAD_DOWN = 20, DPAD_LEFT = 21, DPAD_RIGHT = 22,
    VOLUME_UP = 24, VOLUME_DOWN = 25, POWER = 26, A = 29, TAB = 61, SPACE = 62, ENTER = 66, DEL = 67, MENU = 82,
    PAGE_UP = 92, PAGE_DOWN = 93, ESCAPE = 111, FORWARD_DEL = 112, MOVE_HOME = 122, MOVE_END = 123, INSERT = 124,
    F1 = 131, APP_SWITCH = 187,
};
enum : int { META_SHIFT = 0x41, META_ALT = 0x12, META_CTRL = 0x3000, META_META = 0x10000 };  // *_ON | *_LEFT_ON
}  // namespace akey
namespace amotion {
enum : int { ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, BUTTON_PRIMARY = 1 };
}
constexpr uint64_t kPointerMouse = (uint64_t)-1;
constexpr uint64_t kPointerFinger = (uint64_t)-2;

class AndroidControl {
public:
    // onClipboard is called (on the reader thread) with the phone's clipboard text whenever it changes
    explicit AndroidControl(sock_t s, std::function<void(const std::string&)> onClipboard = nullptr);
    ~AndroidControl();
    void stop();
    bool alive() const { return alive_; }

    // x, y in video-frame pixels; w, h = size of the video frame they refer to
    void touch(int action, uint64_t pointer, int x, int y, int w, int h, float pressure = 1.0f);
    void scroll(int x, int y, int w, int h, float hscroll, float vscroll);
    void key(int action, int keycode, int repeat, int meta);
    void text(const std::string& utf8);
    void backOrScreenOn(int action);
    void setClipboard(const std::string& utf8, bool paste);
    // virtual HID game controller (Android sees an Xbox 360 pad)
    void gamepadOpen(uint16_t id);
    void gamepadInput(uint16_t id, const uint8_t report[15]);
    void gamepadClose(uint16_t id);

    // the 15-byte gamepad report (see scrcpy hid_gamepad.c)
    static void gamepadReport(uint8_t out[15], int lx, int ly, int rx, int ry, int lt, int rt, uint16_t buttons, int hat);

private:
    void send(std::vector<uint8_t> msg);
    void writer();
    void reader();
    sock_t sock_;
    std::atomic<bool> alive_{true};
    volatile bool running_ = true;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::vector<uint8_t>> queue_;
    std::thread writer_, reader_;
    uint64_t clipSeq_ = 0;
    std::function<void(const std::string&)> onClipboard_;
};

}  // namespace mn
