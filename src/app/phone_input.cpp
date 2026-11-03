// Mirrorly - controlling an Android phone from the PC
//
// Mouse: left button = finger (tap, drag, swipe), right button = Back, middle button = Home,
// wheel = scroll. Keyboard: keys and text go to the phone.
// Clipboard: text copied on the phone lands on the PC's clipboard; text copied on the PC goes to
// the phone right away (Mirrorly may be minimized), and Ctrl+V pastes it there.
// Key zones: spots on the screen bound to a key or a combination (e.g. "A", "Ctrl+A"); while
// the key is held, the spot is touched, so games can be played with the keyboard. Game
// controllers (XInput) appear on the phone as an Xbox 360 pad.
#include "app.h"

#include <xinput.h>

#include <cmath>
#include <cstring>
#include <cstdio>

#include "log.h"

using mn::AndroidControl;
namespace akey = mn::akey;
namespace amotion = mn::amotion;

namespace {

int currentMods() {
    int m = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= MOD_CONTROL;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= MOD_SHIFT;
    if (GetKeyState(VK_MENU) & 0x8000) m |= MOD_ALT;
    return m;
}

bool isModifier(int vk) {
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL ||
           vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
}

int androidMeta(int mods) {
    int m = 0;
    if (mods & MOD_SHIFT) m |= akey::META_SHIFT;
    if (mods & MOD_CONTROL) m |= akey::META_CTRL;
    if (mods & MOD_ALT) m |= akey::META_ALT;
    return m;
}

// Windows virtual key -> Android keycode for keys sent as key events (0 = send as text instead)
int androidKey(int vk, int mods) {
    bool altGr = (mods & MOD_CONTROL) && (mods & MOD_ALT);
    if (vk >= 'A' && vk <= 'Z' && !altGr) return akey::A + (vk - 'A');
    if (vk >= '0' && vk <= '9' && (mods & (MOD_CONTROL | MOD_ALT)) && !altGr) return akey::KEY_0 + (vk - '0');
    if (vk >= VK_F1 && vk <= VK_F12) return akey::F1 + (vk - VK_F1);
    switch (vk) {
    case VK_SPACE: return akey::SPACE;
    case VK_RETURN: return akey::ENTER;
    case VK_BACK: return akey::DEL;
    case VK_DELETE: return akey::FORWARD_DEL;
    case VK_TAB: return akey::TAB;
    case VK_ESCAPE: return akey::ESCAPE;
    case VK_UP: return akey::DPAD_UP;
    case VK_DOWN: return akey::DPAD_DOWN;
    case VK_LEFT: return akey::DPAD_LEFT;
    case VK_RIGHT: return akey::DPAD_RIGHT;
    case VK_HOME: return akey::MOVE_HOME;
    case VK_END: return akey::MOVE_END;
    case VK_PRIOR: return akey::PAGE_UP;
    case VK_NEXT: return akey::PAGE_DOWN;
    case VK_INSERT: return akey::INSERT;
    case VK_APPS: return akey::MENU;
    case VK_VOLUME_UP: return akey::VOLUME_UP;
    case VK_VOLUME_DOWN: return akey::VOLUME_DOWN;
    }
    return 0;
}

std::wstring keymapDir() {
    std::wstring d = Settings::dataDir() + L"\\keymaps";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XINPUT_STATE*);

}  // namespace

bool App::controlling() const {
    return mirrorActive_ && haveFrame_ && clientIsAndroid() && settings_.controlAndroid && videoW_ > 0 && !settingsOpen_;
}

// window position (DIPs) -> pixel in the phone's video frame
bool App::framePoint(float x, float y, int& fx, int& fy, bool clamp) const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_RECT_F v = videoRect(rc.right * 96.0f / dpi_, rc.bottom * 96.0f / dpi_);
    float u = (x - v.left) / (v.right - v.left), w = (y - v.top) / (v.bottom - v.top);
    if (!clamp && (u < 0 || u > 1 || w < 0 || w > 1)) return false;
    u = std::max(0.0f, std::min(1.0f, u));
    w = std::max(0.0f, std::min(1.0f, w));
    fx = std::min(videoW_ - 1, (int)(u * videoW_));
    fy = std::min(videoH_ - 1, (int)(w * videoH_));
    return true;
}

// Mouse on the mirrored screen. Returns true when the event was used.
bool App::phoneMouse(UINT msg, float x, float y, WPARAM wp) {
    if (!controlling()) return false;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_RECT_F v = videoRect(rc.right * 96.0f / dpi_, rc.bottom * 96.0f / dpi_);

    if (keymapEdit_) {  // placing, moving and removing key zones
        auto zoneAt = [&](float px, float py) {
            for (int i = (int)zones_.size() - 1; i >= 0; i--) {
                float zx = v.left + zones_[i].x * (v.right - v.left), zy = v.top + zones_[i].y * (v.bottom - v.top);
                if ((px - zx) * (px - zx) + (py - zy) * (py - zy) <= 26 * 26) return i;
            }
            return -1;
        };
        bool inside = x >= v.left && x <= v.right && y >= v.top && y <= v.bottom;
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
            if (hover_ != H_NONE) return false;  // the bar's buttons
            int z = zoneAt(x, y);
            if (z >= 0) {
                zoneDrag_ = z;
                dragDx_ = v.left + zones_[z].x * (v.right - v.left) - x;
                dragDy_ = v.top + zones_[z].y * (v.bottom - v.top) - y;
                SetCapture(hwnd_);
            } else if (inside) {
                if (zonePending_ >= 0 && zones_[zonePending_].vk == 0) zones_.erase(zones_.begin() + zonePending_);
                KeyZone nz;
                nz.x = (x - v.left) / (v.right - v.left);
                nz.y = (y - v.top) / (v.bottom - v.top);
                zones_.push_back(nz);
                zonePending_ = (int)zones_.size() - 1;
                pendingModifier_ = 0;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (msg == WM_MOUSEMOVE && zoneDrag_ >= 0) {
            zones_[zoneDrag_].x = std::max(0.0f, std::min(1.0f, (x + dragDx_ - v.left) / (v.right - v.left)));
            zones_[zoneDrag_].y = std::max(0.0f, std::min(1.0f, (y + dragDy_ - v.top) / (v.bottom - v.top)));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (msg == WM_LBUTTONUP && zoneDrag_ >= 0) {
            zoneDrag_ = -1;
            ReleaseCapture();
            saveKeymap();
            return true;
        }
        if (msg == WM_RBUTTONDOWN) {  // remove a zone
            int z = zoneAt(x, y);
            if (z >= 0) {
                zones_.erase(zones_.begin() + z);
                if (zonePending_ == z) zonePending_ = -1;
                else if (zonePending_ > z) zonePending_--;
                saveKeymap();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
        return msg != WM_MOUSEMOVE;
    }

    auto ctl = android_.control();
    if (!ctl) return false;
    int fx, fy;
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (hover_ != H_NONE || !framePoint(x, y, fx, fy, false)) return false;
        ctl->touch(amotion::ACTION_DOWN, mn::kPointerFinger, fx, fy, videoW_, videoH_);
        touchDown_ = true;
        SetCapture(hwnd_);
        return true;
    case WM_MOUSEMOVE:
        if (!touchDown_) return false;
        framePoint(x, y, fx, fy, true);
        ctl->touch(amotion::ACTION_MOVE, mn::kPointerFinger, fx, fy, videoW_, videoH_);
        return true;
    case WM_LBUTTONUP:
        if (!touchDown_) return false;
        framePoint(x, y, fx, fy, true);
        ctl->touch(amotion::ACTION_UP, mn::kPointerFinger, fx, fy, videoW_, videoH_);
        touchDown_ = false;
        ReleaseCapture();
        return true;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        if (!framePoint(x, y, fx, fy, false)) return false;
        ctl->backOrScreenOn(msg == WM_RBUTTONDOWN ? akey::ACTION_DOWN : akey::ACTION_UP);
        return true;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        if (!framePoint(x, y, fx, fy, false)) return false;
        ctl->key(msg == WM_MBUTTONDOWN ? akey::ACTION_DOWN : akey::ACTION_UP, akey::HOME, 0, 0);
        return true;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        if (!framePoint(x, y, fx, fy, false)) return false;
        float d = GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
        if (msg == WM_MOUSEWHEEL) ctl->scroll(fx, fy, videoW_, videoH_, 0, d);
        else ctl->scroll(fx, fy, videoW_, videoH_, -d, 0);
        return true;
    }
    }
    return false;
}

// Keyboard while controlling. Returns true when the key went to the phone.
bool App::phoneKey(UINT msg, WPARAM w, LPARAM l) {
    if (!controlling()) return false;
    int vk = (int)w;
    bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    bool up = msg == WM_KEYUP || msg == WM_SYSKEYUP;
    bool repeat = down && (l & (1 << 30));
    int mods = currentMods();

    // keys the app keeps: full screen on/off, Esc in full screen, Alt+F4
    if (vk == VK_F11 || (vk == VK_RETURN && (mods & MOD_ALT)) || (vk == VK_F4 && (mods & MOD_ALT))) return false;
    if (vk == VK_ESCAPE && fullscreen_ && !keymapEdit_) return false;

    if (keymapEdit_) {
        if (msg == WM_CHAR || msg == WM_SYSCHAR) return true;
        if (down && vk == VK_ESCAPE) {
            if (zonePending_ >= 0 && zones_[zonePending_].vk == 0) {
                zones_.erase(zones_.begin() + zonePending_);
                zonePending_ = -1;
            } else {
                setKeymapEdit(false);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (zonePending_ < 0) return true;
        KeyZone& z = zones_[zonePending_];
        if (down && !repeat) {
            if (isModifier(vk)) {
                pendingModifier_ = vk;  // could be a combination, or the modifier on its own
            } else {
                z.vk = vk;
                z.mods = mods;
                pendingModifier_ = 0;
            }
        } else if (up && isModifier(vk) && pendingModifier_ && z.vk == 0) {
            z.vk = vk == VK_LSHIFT || vk == VK_RSHIFT ? VK_SHIFT : vk == VK_LCONTROL || vk == VK_RCONTROL ? VK_CONTROL
                 : vk == VK_LMENU || vk == VK_RMENU ? VK_MENU : vk;
            z.mods = 0;
            pendingModifier_ = 0;
        }
        if (z.vk) {
            // a key can only drive one zone: the new binding replaces older ones
            for (int i = 0; i < (int)zones_.size(); i++)
                if (i != zonePending_ && zones_[i].vk == z.vk && zones_[i].mods == z.mods) {
                    zones_.erase(zones_.begin() + i);
                    if (zonePending_ > i) zonePending_--;
                    i--;
                }
            zonePending_ = -1;
            saveKeymap();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    auto ctl = android_.control();
    if (!ctl) return false;

    // key zones: the key presses a spot on the screen
    if (keymapOn_ && (down || up) && !zones_.empty()) {
        int base = vk == VK_LSHIFT || vk == VK_RSHIFT ? VK_SHIFT : vk == VK_LCONTROL || vk == VK_RCONTROL ? VK_CONTROL
                 : vk == VK_LMENU || vk == VK_RMENU ? VK_MENU : vk;
        bool hit = false;
        for (int i = 0; i < (int)zones_.size(); i++) {
            const KeyZone& z = zones_[i];
            if (z.vk != base) continue;
            bool modsOk = isModifier(base) || z.mods == mods;
            int fx = std::min(videoW_ - 1, (int)(z.x * videoW_)), fy = std::min(videoH_ - 1, (int)(z.y * videoH_));
            auto it = std::find(zonesDown_.begin(), zonesDown_.end(), i);
            if (down && modsOk) {
                hit = true;
                if (it == zonesDown_.end()) {
                    ctl->touch(amotion::ACTION_DOWN, 0x100 + i, fx, fy, videoW_, videoH_);
                    zonesDown_.push_back(i);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            } else if (up && it != zonesDown_.end()) {
                hit = true;
                ctl->touch(amotion::ACTION_UP, 0x100 + i, fx, fy, videoW_, videoH_);
                zonesDown_.erase(it);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        if (hit) return true;
    }

    if (msg == WM_CHAR || msg == WM_SYSCHAR) {
        wchar_t c = (wchar_t)w;
        // letters, space and control characters already went as key events
        if (c < 32 || c == 127 || c == L' ') return true;
        bool altGr = (mods & MOD_CONTROL) && (mods & MOD_ALT);
        if (((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) && !altGr) return true;
        if ((mods & (MOD_CONTROL | MOD_ALT)) && !altGr) return true;
        ctl->text(to_utf8(std::wstring(1, c)));
        return true;
    }
    if (!down && !up) return false;
    if (isModifier(vk)) return true;

    // Ctrl+V: paste the PC's clipboard on the phone (Ctrl+C / Ctrl+X go as keys; the phone sends back what it copied)
    if (down && vk == 'V' && mods == MOD_CONTROL) {
        if (!repeat) sharePcClipboard(true);
        return true;
    }
    int ak = androidKey(vk, mods);
    if (!ak) return true;  // its character comes as text (WM_CHAR)
    ctl->key(down ? akey::ACTION_DOWN : akey::ACTION_UP, ak, repeat ? 1 : 0, androidMeta(mods));
    return true;
}

// ------------------------------------------------------------------ clipboard

static bool openClipboard(HWND hwnd) {
    for (int i = 0; i < 5; i++) {  // another app may hold it for a moment
        if (OpenClipboard(hwnd)) return true;
        Sleep(15);
    }
    return false;
}

static std::wstring readPcClipboard(HWND hwnd) {
    std::wstring t;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !openClipboard(hwnd)) return t;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const wchar_t* p = h ? (const wchar_t*)GlobalLock(h) : nullptr;
    if (p) {
        t = p;
        GlobalUnlock(h);
    }
    CloseClipboard();
    return t;
}

void App::phoneCopied(const std::string& utf8) {
    if (!sessionActive_ || !clientIsAndroid() || !settings_.controlAndroid || utf8.empty()) return;
    std::wstring in = from_utf8(utf8), t;
    for (size_t i = 0; i < in.size(); i++) {  // Android lines end with \n, Windows ones with \r\n
        if (in[i] == L'\n' && (i == 0 || in[i - 1] != L'\r')) t += L'\r';
        t += in[i];
    }
    if (t == lastClip_) return;  // what we just gave the phone
    if (!openClipboard(hwnd_)) {
        LOGW("clipboard: busy, the phone's copy was not received");
        return;
    }
    EmptyClipboard();
    size_t bytes = (t.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (void* p = g ? GlobalLock(g) : nullptr) {
        memcpy(p, t.c_str(), bytes);
        GlobalUnlock(g);
        if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
    }
    CloseClipboard();
    lastClip_ = t;
    clipSeq_ = GetClipboardSequenceNumber();
    LOGI("clipboard: %d characters copied from the phone", (int)t.size());
    showToast(L"Copied from your phone");
}

// Password managers mark what they copy as private (the formats Windows' clipboard history honours).
static bool privateClipboard() {
    static const UINT exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    static const UINT history = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    if (exclude && IsClipboardFormatAvailable(exclude)) return true;
    if (!history || !IsClipboardFormatAvailable(history) || !OpenClipboard(nullptr)) return false;
    bool no = false;
    HANDLE h = GetClipboardData(history);
    const DWORD* v = h && GlobalSize(h) >= sizeof(DWORD) ? (const DWORD*)GlobalLock(h) : nullptr;
    if (v) {
        no = *v == 0;
        GlobalUnlock(h);
    }
    CloseClipboard();
    return no;
}

void App::sharePcClipboard(bool paste) {
    auto ctl = android_.control();
    if (!ctl || !clientIsAndroid() || !settings_.controlAndroid) return;
    DWORD seq = GetClipboardSequenceNumber();
    if (!paste && seq == clipSeq_) return;  // nothing new since the last exchange (or our own write)
    clipSeq_ = seq;
    if (!paste && privateClipboard()) {  // passwords only go to the phone with an explicit Ctrl+V
        LOGI("clipboard: private copy (password manager) not shared with the phone");
        return;
    }
    std::wstring t = readPcClipboard(hwnd_);
    if (t.empty() || (!paste && t == lastClip_)) return;
    lastClip_ = t;
    std::wstring lf;
    for (wchar_t c : t)
        if (c != L'\r') lf += c;
    ctl->setClipboard(to_utf8(lf), paste);
    LOGI("clipboard: %d characters %s the phone", (int)lf.size(), paste ? "pasted on" : "shared with");
}

// Lift every finger and key zone (focus lost, session ended, editing started).
void App::releasePhoneInput() {
    auto ctl = android_.control();
    if (ctl && videoW_ > 0) {
        for (int i : zonesDown_)
            if (i < (int)zones_.size())
                ctl->touch(amotion::ACTION_UP, 0x100 + i, (int)(zones_[i].x * (videoW_ - 1)), (int)(zones_[i].y * (videoH_ - 1)), videoW_,
                           videoH_);
        if (touchDown_) ctl->touch(amotion::ACTION_UP, mn::kPointerFinger, 0, 0, videoW_, videoH_);
    }
    zonesDown_.clear();
    if (touchDown_) {
        touchDown_ = false;
        ReleaseCapture();
    }
}

// Game controllers: XInput pads become virtual Xbox 360 pads on the phone (UHID).
void App::pollGamepads() {
    auto ctl = (mirrorActive_ && clientIsAndroid() && settings_.gamepadAndroid) ? android_.control() : nullptr;
    if (!xinputGetState_) {
        static bool tried = false;
        if (tried) return;
        tried = true;
        HMODULE m = LoadLibraryW(L"xinput1_4.dll");
        if (!m) m = LoadLibraryW(L"xinput9_1_0.dll");
        if (m) xinputGetState_ = (void*)GetProcAddress(m, "XInputGetState");
        if (!xinputGetState_) return;
    }
    auto get = (XInputGetStateFn)xinputGetState_;
    for (int i = 0; i < 4; i++) {
        uint16_t id = (uint16_t)(3 + i);
        XINPUT_STATE st{};
        bool connected = ctl && get(i, &st) == ERROR_SUCCESS;
        if (!connected) {
            if (padOpen_[i]) {
                if (ctl) ctl->gamepadClose(id);
                padOpen_[i] = false;
                mn::logf(mn::LogLevel::Info, "gamepad %d removed", i + 1);
            }
            continue;
        }
        if (!padOpen_[i]) {
            ctl->gamepadOpen(id);
            padOpen_[i] = true;
            padPacket_[i] = 0xFFFFFFFF;
            mn::logf(mn::LogLevel::Info, "gamepad %d connected to the phone", i + 1);
            showToast(L"Controller connected to the phone");
        }
        if (st.dwPacketNumber == padPacket_[i]) continue;
        padPacket_[i] = st.dwPacketNumber;
        const XINPUT_GAMEPAD& g = st.Gamepad;
        uint16_t b = 0;
        if (g.wButtons & XINPUT_GAMEPAD_A) b |= 0x0001;
        if (g.wButtons & XINPUT_GAMEPAD_B) b |= 0x0002;
        if (g.wButtons & XINPUT_GAMEPAD_X) b |= 0x0008;
        if (g.wButtons & XINPUT_GAMEPAD_Y) b |= 0x0010;
        if (g.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) b |= 0x0040;
        if (g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) b |= 0x0080;
        if (g.wButtons & XINPUT_GAMEPAD_BACK) b |= 0x0400;
        if (g.wButtons & XINPUT_GAMEPAD_START) b |= 0x0800;
        if (g.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) b |= 0x2000;
        if (g.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) b |= 0x4000;
        bool u = g.wButtons & XINPUT_GAMEPAD_DPAD_UP, d = g.wButtons & XINPUT_GAMEPAD_DPAD_DOWN;
        bool lf = g.wButtons & XINPUT_GAMEPAD_DPAD_LEFT, r = g.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT;
        int hat = u ? (lf ? 8 : r ? 2 : 1) : d ? (lf ? 6 : r ? 4 : 5) : lf ? 7 : r ? 3 : 0;
        // XInput: Y axes point up, the HID pad expects down
        auto inv = [](SHORT v) { return v == -32768 ? 32767 : -(int)v; };
        uint8_t rep[15];
        AndroidControl::gamepadReport(rep, g.sThumbLX, inv(g.sThumbLY), g.sThumbRX, inv(g.sThumbRY), g.bLeftTrigger * 32767 / 255,
                                      g.bRightTrigger * 32767 / 255, b, hat);
        ctl->gamepadInput(id, rep);
    }
}

// ---------------------------------------------------------------- key zones

void App::loadKeymap() {
    std::wstring name = from_utf8(client_.name);
    for (auto& c : name)
        if (!iswalnum(c)) c = L'_';
    std::wstring id = name + (videoW_ >= videoH_ ? L"-landscape" : L"-portrait");
    if (id == keymapId_) return;
    releasePhoneInput();
    keymapId_ = id;
    zones_.clear();
    keymapOn_ = true;
    zonePending_ = zoneDrag_ = -1;
    FILE* f = _wfopen((keymapDir() + L"\\" + id + L".txt").c_str(), L"r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        KeyZone z;
        int on;
        if (sscanf(line, "enabled %d", &on) == 1) keymapOn_ = on != 0;
        else if (sscanf(line, "%f %f %d %d", &z.x, &z.y, &z.vk, &z.mods) == 4 && z.vk) zones_.push_back(z);
    }
    fclose(f);
    mn::logf(mn::LogLevel::Info, "key zones: %u loaded for %s", (unsigned)zones_.size(), to_utf8(id).c_str());
}

void App::saveKeymap() {
    if (keymapId_.empty()) return;
    FILE* f = _wfopen((keymapDir() + L"\\" + keymapId_ + L".txt").c_str(), L"w");
    if (!f) return;
    fprintf(f, "enabled %d\n", keymapOn_ ? 1 : 0);
    for (auto& z : zones_)
        if (z.vk) fprintf(f, "%.5f %.5f %d %d\n", z.x, z.y, z.vk, z.mods);
    fclose(f);
}

void App::setKeymapEdit(bool on) {
    if (on == keymapEdit_) return;
    releasePhoneInput();
    keymapEdit_ = on;
    if (!on) {
        if (zonePending_ >= 0 && zonePending_ < (int)zones_.size() && zones_[zonePending_].vk == 0)
            zones_.erase(zones_.begin() + zonePending_);
        zonePending_ = zoneDrag_ = -1;
        saveKeymap();
    }
    closeVolume();
    lastMouseMove_ = mn::now_ms();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring App::zoneLabel(const KeyZone& z) const {
    if (!z.vk) return L"";
    std::wstring s;
    if (z.mods & MOD_CONTROL) s += L"Ctrl+";
    if (z.mods & MOD_ALT) s += L"Alt+";
    if (z.mods & MOD_SHIFT) s += L"Shift+";
    int vk = z.vk;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return s + (wchar_t)vk;
    if (vk >= VK_F1 && vk <= VK_F24) return s + L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return s + L"Num" + std::to_wstring(vk - VK_NUMPAD0);
    switch (vk) {
    case VK_SPACE: return s + L"Space";
    case VK_RETURN: return s + L"Enter";
    case VK_TAB: return s + L"Tab";
    case VK_BACK: return s + L"⌫";
    case VK_UP: return s + L"↑";
    case VK_DOWN: return s + L"↓";
    case VK_LEFT: return s + L"←";
    case VK_RIGHT: return s + L"→";
    case VK_SHIFT: return s + L"Shift";
    case VK_CONTROL: return s + L"Ctrl";
    case VK_MENU: return s + L"Alt";
    }
    // anything else: the key's name in the keyboard layout (e.g. ";" or "ù")
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t name[32] = {0};
    if (sc && GetKeyNameTextW((LONG)(sc << 16), name, 32) > 0) return s + name;
    return s + L"#" + std::to_wstring(vk);
}

void App::drawKeyZones(float W, float H, D2D1_RECT_F v) {
    (void)W;
    (void)H;
    if (!controlling() || (zones_.empty() && !keymapEdit_)) return;
    bool show = keymapEdit_ || (keymapOn_ && overlayAlpha_ >= 0.02f) || (keymapOn_ && !zonesDown_.empty());
    if (show) {
        for (int i = 0; i < (int)zones_.size(); i++) {
            D2D1_POINT_2F c = D2D1::Point2F(v.left + zones_[i].x * (v.right - v.left), v.top + zones_[i].y * (v.bottom - v.top));
            bool pressed = std::find(zonesDown_.begin(), zonesDown_.end(), i) != zonesDown_.end();
            int state = keymapEdit_ ? (i == zonePending_ ? 2 : 1) : pressed ? 3 : 0;
            if (!keymapEdit_ && !pressed && overlayAlpha_ < 0.02f) continue;
            float a = keymapEdit_ || pressed ? 1.0f : overlayAlpha_;
            theme_->drawKeyZone(*this, c, i == zonePending_ ? L"Press a key" : zoneLabel(zones_[i]), state, a);
        }
    }
}
