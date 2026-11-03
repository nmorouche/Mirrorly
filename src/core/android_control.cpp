// Mirrorly - scrcpy control messages (see android_control.h)
#include "android_control.h"

#include <algorithm>
#include <cstring>

#include "log.h"

namespace mn {

namespace {
enum MsgType : uint8_t {
    INJECT_KEYCODE = 0, INJECT_TEXT = 1, INJECT_TOUCH = 2, INJECT_SCROLL = 3, BACK_OR_SCREEN_ON = 4,
    SET_CLIPBOARD = 9, UHID_CREATE = 12, UHID_INPUT = 13, UHID_DESTROY = 14,
};

void be16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back((uint8_t)(v >> 8));
    o.push_back((uint8_t)v);
}
void be32(std::vector<uint8_t>& o, uint32_t v) {
    be16(o, (uint16_t)(v >> 16));
    be16(o, (uint16_t)v);
}
void be64(std::vector<uint8_t>& o, uint64_t v) {
    be32(o, (uint32_t)(v >> 32));
    be32(o, (uint32_t)v);
}
void position(std::vector<uint8_t>& o, int x, int y, int w, int h) {
    be32(o, (uint32_t)x);
    be32(o, (uint32_t)y);
    be16(o, (uint16_t)w);
    be16(o, (uint16_t)h);
}
// cut at max bytes without splitting a UTF-8 sequence
size_t utf8Cut(const std::string& s, size_t max) {
    if (s.size() <= max) return s.size();
    size_t n = max;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    return n;
}
void string32(std::vector<uint8_t>& o, const std::string& s, size_t max) {
    size_t n = utf8Cut(s, max);
    be32(o, (uint32_t)n);
    o.insert(o.end(), s.begin(), s.begin() + n);
}
int16_t i16fp(float f) {
    f = std::max(-1.0f, std::min(1.0f, f));
    int32_t i = (int32_t)(f * 32768.0f);
    return (int16_t)std::min(i, 0x7fff);
}

// HID report descriptor of scrcpy's virtual gamepad
const uint8_t kGamepadDesc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0xA1, 0x00, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34,
    0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02, 0x05, 0x01, 0x09, 0x32, 0x09,
    0x35, 0x15, 0x00, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02, 0x05, 0x09, 0x19, 0x01, 0x29, 0x10,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x10, 0x75, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0xC0, 0xC0,
};
}  // namespace

AndroidControl::AndroidControl(sock_t s) : sock_(s) {
    sock_set_nodelay(sock_);
    writer_ = std::thread(&AndroidControl::writer, this);
    reader_ = std::thread(&AndroidControl::reader, this);
}

AndroidControl::~AndroidControl() { stop(); }

void AndroidControl::stop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        running_ = false;
    }
    cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (reader_.joinable()) reader_.join();
    alive_ = false;
}

void AndroidControl::send(std::vector<uint8_t> msg) {
    if (!alive_) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (queue_.size() > 1024) queue_.pop_front();  // a stalled phone must not grow this forever
        queue_.push_back(std::move(msg));
    }
    cv_.notify_one();
}

void AndroidControl::writer() {
    for (;;) {
        std::vector<uint8_t> msg;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return !running_ || !queue_.empty(); });
            if (!running_) return;
            msg = std::move(queue_.front());
            queue_.pop_front();
        }
        if (!sock_send_all(sock_, msg.data(), msg.size())) {
            LOGW("android: control channel closed");
            alive_ = false;
            return;
        }
    }
}

// Messages from the phone (clipboard changes, acks, UHID output): read and dropped.
void AndroidControl::reader() {
    uint8_t type;
    std::vector<uint8_t> buf;
    while (running_) {
        if (!sock_recv_all(sock_, &type, 1, &running_)) break;
        uint8_t h[8];
        if (type == 0) {  // clipboard: len32 + text
            if (!sock_recv_all(sock_, h, 4, &running_)) break;
            uint32_t n = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
            if (n > (1u << 20)) break;
            buf.resize(n);
            if (n && !sock_recv_all(sock_, buf.data(), n, &running_)) break;
        } else if (type == 1) {  // clipboard ack: sequence
            if (!sock_recv_all(sock_, h, 8, &running_)) break;
        } else if (type == 2) {  // UHID output: id16 + len16 + data
            if (!sock_recv_all(sock_, h, 4, &running_)) break;
            uint16_t n = (uint16_t)((h[2] << 8) | h[3]);
            buf.resize(n);
            if (n && !sock_recv_all(sock_, buf.data(), n, &running_)) break;
        } else {
            LOGW("android: unknown message %u from the phone", type);
            break;
        }
    }
}

void AndroidControl::touch(int action, uint64_t pointer, int x, int y, int w, int h, float pressure) {
    std::vector<uint8_t> m{INJECT_TOUCH, (uint8_t)action};
    be64(m, pointer);
    position(m, x, y, w, h);
    float p = std::max(0.0f, std::min(1.0f, action == amotion::ACTION_UP ? 0.0f : pressure));
    be16(m, p >= 1.0f ? 0xffff : (uint16_t)(p * 65536.0f));
    uint32_t button = pointer == kPointerMouse ? amotion::BUTTON_PRIMARY : 0;
    be32(m, button);                                              // action button
    be32(m, action == amotion::ACTION_UP ? 0 : button);           // buttons held
    send(std::move(m));
}

void AndroidControl::scroll(int x, int y, int w, int h, float hscroll, float vscroll) {
    std::vector<uint8_t> m{INJECT_SCROLL};
    position(m, x, y, w, h);
    be16(m, (uint16_t)i16fp(hscroll / 16.0f));
    be16(m, (uint16_t)i16fp(vscroll / 16.0f));
    be32(m, 0);
    send(std::move(m));
}

void AndroidControl::key(int action, int keycode, int repeat, int meta) {
    std::vector<uint8_t> m{INJECT_KEYCODE, (uint8_t)action};
    be32(m, (uint32_t)keycode);
    be32(m, (uint32_t)repeat);
    be32(m, (uint32_t)meta);
    send(std::move(m));
}

void AndroidControl::text(const std::string& utf8) {
    if (utf8.empty()) return;
    std::vector<uint8_t> m{INJECT_TEXT};
    string32(m, utf8, 300);
    send(std::move(m));
}

void AndroidControl::backOrScreenOn(int action) { send({BACK_OR_SCREEN_ON, (uint8_t)action}); }

void AndroidControl::setClipboard(const std::string& utf8, bool paste) {
    std::vector<uint8_t> m{SET_CLIPBOARD};
    be64(m, ++clipSeq_);
    m.push_back(paste ? 1 : 0);
    string32(m, utf8, (1 << 18) - 14);
    send(std::move(m));
}

void AndroidControl::gamepadOpen(uint16_t id) {
    static const char name[] = "Microsoft X-Box 360 Pad";
    std::vector<uint8_t> m{UHID_CREATE};
    be16(m, id);
    be16(m, 0x045e);
    be16(m, 0x028e);
    m.push_back((uint8_t)(sizeof(name) - 1));
    m.insert(m.end(), name, name + sizeof(name) - 1);
    be16(m, (uint16_t)sizeof(kGamepadDesc));
    m.insert(m.end(), kGamepadDesc, kGamepadDesc + sizeof(kGamepadDesc));
    send(std::move(m));
}

void AndroidControl::gamepadInput(uint16_t id, const uint8_t report[15]) {
    std::vector<uint8_t> m{UHID_INPUT};
    be16(m, id);
    be16(m, 15);
    m.insert(m.end(), report, report + 15);
    send(std::move(m));
}

void AndroidControl::gamepadClose(uint16_t id) {
    std::vector<uint8_t> m{UHID_DESTROY};
    be16(m, id);
    send(std::move(m));
}

void AndroidControl::gamepadReport(uint8_t out[15], int lx, int ly, int rx, int ry, int lt, int rt, uint16_t buttons, int hat) {
    auto le16 = [&](int at, uint16_t v) {
        out[at] = (uint8_t)v;
        out[at + 1] = (uint8_t)(v >> 8);
    };
    // sticks: -32768..32767 -> 0..65535 ; triggers 0..32767
    le16(0, (uint16_t)(lx + 0x8000));
    le16(2, (uint16_t)(ly + 0x8000));
    le16(4, (uint16_t)(rx + 0x8000));
    le16(6, (uint16_t)(ry + 0x8000));
    le16(8, (uint16_t)std::max(0, std::min(32767, lt)));
    le16(10, (uint16_t)std::max(0, std::min(32767, rt)));
    le16(12, buttons);
    out[14] = (uint8_t)hat;
}

}  // namespace mn
