// Mirrorly - AirPlay receiver for Windows (screen mirroring + audio)
// Win32 + Direct2D/DirectWrite user interface.
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio_out.h"
#include "log.h"
#include "platform.h"
#include "receiver.h"
#include "settings.h"

#define WM_APP_EVENT (WM_APP + 1)
#define WM_APP_FRAME (WM_APP + 2)

static const wchar_t* kAppName = L"Mirrorly";
static const wchar_t* kVersion = L"1.0.0";

template <class T> static void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

// ------------------------------------------------------------------ colours
static D2D1_COLOR_F rgb(uint32_t c, float a = 1.0f) {
    return D2D1::ColorF(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, a);
}
static const uint32_t C_BG = 0x0B0D12, C_SURFACE = 0x141821, C_SURFACE2 = 0x1B2030, C_BORDER = 0x262D3D,
                      C_TEXT = 0xF2F4F8, C_TEXT2 = 0x9AA3B5, C_TEXT3 = 0x667085, C_ACCENT = 0x5B8CFF,
                      C_ACCENT2 = 0x9B6BFF, C_GREEN = 0x3DDC97, C_AMBER = 0xFFB547, C_RED = 0xFF5C6C;

// ------------------------------------------------------------------ icon glyphs (Segoe Fluent Icons / MDL2 Assets)
static const wchar_t G_SETTINGS = 0xE713, G_BACK = 0xE72B, G_CLOSE = 0xE711, G_FULL = 0xE740, G_WINDOW = 0xE73F,
                     G_VOLUME = 0xE767, G_MUTE = 0xE74F, G_PHONE = 0xE8EA, G_MUSIC = 0xE8D6, G_TV = 0xE7F4,
                     G_SHIELD = 0xEA18, G_INFO = 0xE946;

// ------------------------------------------------------------------ events from the engine
enum EvType {
    EV_CONNECTING, EV_SESSION_START, EV_SESSION_END, EV_MIRROR_START, EV_MIRROR_STOP, EV_PAUSED, EV_AUDIO_START,
    EV_AUDIO_STOP, EV_VOLUME, EV_METADATA, EV_ARTWORK, EV_PROGRESS, EV_FIREWALL_DONE
};
struct Event {
    EvType type;
    mn::ClientInfo client;
    float f = 0;
    bool b = false;
    std::string s1, s2, s3;
    std::vector<uint8_t> data;
    double d1 = 0, d2 = 0;
};

enum Hit {
    H_NONE, H_SETTINGS, H_BACK, H_FIREWALL, H_RES0, H_RES1, H_RES2, H_RES3, H_FPS30, H_FPS60, H_AUDIO, H_AUTOFS,
    H_MUTE, H_FULLSCREEN, H_DISCONNECT, H_OPENLOG, H_FIREWALL_LINK, H_NAMEBOX
};

class App : public mn::ReceiverListener {
public:
    bool init(HINSTANCE inst, int show);
    int run();

    // ReceiverListener (engine threads)
    void onClientConnecting(const mn::ClientInfo& c) override { post(EV_CONNECTING, [&](Event& e) { e.client = c; }); }
    void onSessionStart(const mn::ClientInfo& c) override { post(EV_SESSION_START, [&](Event& e) { e.client = c; }); }
    void onSessionEnd() override { post(EV_SESSION_END, nullptr); }
    void onMirrorStart() override { post(EV_MIRROR_START, nullptr); }
    void onMirrorStop() override { post(EV_MIRROR_STOP, nullptr); }
    void onVideoPaused(bool p) override { post(EV_PAUSED, [&](Event& e) { e.b = p; }); }
    void onVideoFrame() override {
        if (!framePosted_.exchange(true)) PostMessageW(hwnd_, WM_APP_FRAME, 0, 0);
    }
    void onAudioStart(int ct) override { post(EV_AUDIO_START, [&](Event& e) { e.f = (float)ct; }); }
    void onAudioStop() override { post(EV_AUDIO_STOP, nullptr); }
    void onAudioPcm(const int16_t* pcm, int frames) override {
        if (audioEnabled_) audio_.push(pcm, frames);
    }
    void onAudioFlush() override { audio_.flush(); }
    void onVolume(float db) override { post(EV_VOLUME, [&](Event& e) { e.f = db; }); }
    void onMetadata(const std::string& t, const std::string& a, const std::string& al) override {
        post(EV_METADATA, [&](Event& e) { e.s1 = t; e.s2 = a; e.s3 = al; });
    }
    void onArtwork(const std::vector<uint8_t>& img) override { post(EV_ARTWORK, [&](Event& e) { e.data = img; }); }
    void onProgress(double pos, double dur) override { post(EV_PROGRESS, [&](Event& e) { e.d1 = pos; e.d2 = dur; }); }

private:
    template <class F> void post(EvType t, F fill) {
        Event* e = new Event();
        e->type = t;
        fill(*e);
        if (!PostMessageW(hwnd_, WM_APP_EVENT, 0, (LPARAM)e)) delete e;
    }
    void post(EvType t, std::nullptr_t) {
        Event* e = new Event();
        e->type = t;
        if (!PostMessageW(hwnd_, WM_APP_EVENT, 0, (LPARAM)e)) delete e;
    }

    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT handle(UINT m, WPARAM w, LPARAM l);
    void handleEvent(Event* e);
    void onFrame();
    void startReceiver();
    void applySettings(bool reannounce);
    void commitName();
    void click(int hit);
    void setFullscreen(bool fs);
    void updateGain();
    void allowFirewall();
    void showToast(const std::wstring& msg);
    void openLog();

    // rendering
    bool createDeviceResources();
    void discardDeviceResources();
    void render();
    void drawHome(float W, float H);
    void drawSettings(float W, float H);
    void drawMirror(float W, float H);
    void drawNowPlaying(float W, float H);
    void drawTopBar(float W, bool back, const wchar_t* title);
    void drawToast(float W, float H);
    IDWriteTextFormat* fmt(float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL,
                           DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING, bool icon = false);
    void text(const std::wstring& s, D2D1_RECT_F r, IDWriteTextFormat* f, uint32_t color, float alpha = 1.0f);
    float textWidth(const std::wstring& s, IDWriteTextFormat* f);
    void icon(wchar_t g, float cx, float cy, float size, uint32_t color, float alpha = 1.0f);
    void fillRound(D2D1_RECT_F r, float rad, uint32_t color, float alpha = 1.0f);
    void strokeRound(D2D1_RECT_F r, float rad, uint32_t color, float alpha = 1.0f, float w = 1.0f);
    void iconButton(int id, wchar_t g, float cx, float cy, float size, bool overlay = false, float alpha = 1.0f);
    void toggle(int id, bool on, float x, float y);
    void segmented(const std::vector<std::pair<int, std::wstring>>& items, int selected, float right, float cy);
    void spinner(float cx, float cy, float r, uint32_t color);
    void addHit(int id, D2D1_RECT_F r) { hits_.push_back({id, r}); }
    int hitTest(float x, float y);
    bool isPressed(int id) const { return hover_ == id; }
    void layoutEdit(bool visible, D2D1_RECT_F r);

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    WNDPROC editOrigProc_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    UINT dpi_ = 96;

    ID2D1Factory* d2d_ = nullptr;
    IDWriteFactory* dw_ = nullptr;
    IWICImagingFactory* wic_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    ID2D1Bitmap* video_ = nullptr;
    int videoW_ = 0, videoH_ = 0;
    ID2D1Bitmap* artwork_ = nullptr;
    std::vector<uint8_t> artworkData_;
    std::map<std::wstring, IDWriteTextFormat*> formats_;
    std::wstring uiFont_ = L"Segoe UI", iconFont_ = L"Segoe MDL2 Assets";

    std::vector<std::pair<int, D2D1_RECT_F>> hits_;
    int hover_ = H_NONE, pressed_ = H_NONE;
    bool settingsOpen_ = false;
    bool fullscreen_ = false;
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
    uint64_t lastMouseMove_ = 0;
    float overlayAlpha_ = 0;
    std::wstring toast_;
    uint64_t toastUntil_ = 0;
    D2D1_RECT_F editRect_{};
    bool editVisible_ = false;

    // engine / session state
    Settings settings_;
    mn::Receiver rx_;
    AudioOut audio_;
    std::atomic<bool> audioEnabled_{true};
    std::atomic<bool> framePosted_{false};
    bool connecting_ = false, sessionActive_ = false, mirrorActive_ = false, audioActive_ = false, paused_ = false;
    bool haveFrame_ = false;
    mn::ClientInfo client_;
    float airplayDb_ = 0.0f;
    bool muted_ = false;
    std::wstring title_, artist_, album_;
    double progPos_ = 0, progDur_ = 0;
    uint64_t progStamp_ = 0;
    int fpsCount_ = 0, fps_ = 0;
    uint64_t fpsStamp_ = 0;
    uint64_t startTime_ = 0;
    FILE* logFile_ = nullptr;
    std::wstring logPath_;
};

static App* g_app = nullptr;

// ================================================================== setup

static UINT getDpi(HWND h) {
    typedef UINT(WINAPI * Fn)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && h) return fn(h);
    HDC dc = GetDC(nullptr);
    UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return d;
}

bool App::init(HINSTANCE inst, int show) {
    inst_ = inst;
    startTime_ = mn::now_ms();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // log file next to the executable when possible (handy for troubleshooting), else in %APPDATA%
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    logPath_ = dir + L"\\Mirrorly.log";
    logFile_ = _wfopen(logPath_.c_str(), L"w");
    if (!logFile_) {
        logPath_ = Settings::dataDir() + L"\\Mirrorly.log";
        logFile_ = _wfopen(logPath_.c_str(), L"w");
    }
    FILE* lf = logFile_;
    mn::log_set_sink([lf](mn::LogLevel lvl, const std::string& msg) {
        if (!lf) return;
        static const char* n[] = {"DBG", "INF", "WRN", "ERR"};
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_s(&tmv, &t);
        fprintf(lf, "%02d:%02d:%02d [%s] %s\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, n[(int)lvl], msg.c_str());
        fflush(lf);
    });
    mn::log_set_level(mn::LogLevel::Debug);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&d2d_)))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw_)))
        return false;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&wic_);

    // pick the nicest available fonts
    IDWriteFontCollection* fc = nullptr;
    if (SUCCEEDED(dw_->GetSystemFontCollection(&fc, FALSE)) && fc) {
        UINT32 idx;
        BOOL exists = FALSE;
        if (SUCCEEDED(fc->FindFamilyName(L"Segoe UI Variable Text", &idx, &exists)) && exists) uiFont_ = L"Segoe UI Variable Text";
        exists = FALSE;
        if (SUCCEEDED(fc->FindFamilyName(L"Segoe Fluent Icons", &idx, &exists)) && exists) iconFont_ = L"Segoe Fluent Icons";
        fc->Release();
    }

    settings_.load();
    audioEnabled_ = settings_.audio;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = L"MirrorlyWindow";
    wc.hbrBackground = CreateSolidBrush(RGB(0x0B, 0x0D, 0x12));
    RegisterClassExW(&wc);

    UINT sysDpi = getDpi(nullptr);
    int w = MulDiv(1000, sysDpi, 96), h = MulDiv(660, sysDpi, 96);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                            CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;

    // dark title bar (Windows 10 2004+/11) and matching caption colour (Windows 11)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    COLORREF cap = RGB(0x0B, 0x0D, 0x12);
    DwmSetWindowAttribute(hwnd_, 35, &cap, sizeof(cap));

    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);
    SetTimer(hwnd_, 1, 33, nullptr);
    startReceiver();
    return true;
}

void App::startReceiver() {
    mn::ReceiverConfig cfg;
    cfg.name = to_utf8(settings_.name);
    memcpy(cfg.deviceId, settings_.deviceId, 6);
    memcpy(cfg.edSeed, settings_.seed, 32);
    cfg.pi = settings_.pi;
    settings_.resolutionSize(cfg.width, cfg.height);
    cfg.maxFps = settings_.fps;
    cfg.audioEnabled = settings_.audio;
    if (!rx_.start(cfg, this)) {
        showToast(L"Could not start the AirPlay service. Is another receiver running?");
    }
}

void App::applySettings(bool reannounce) {
    settings_.save();
    audioEnabled_ = settings_.audio;
    if (!settings_.audio) audio_.flush();
    mn::ReceiverConfig cfg = rx_.config();
    cfg.name = to_utf8(settings_.name);
    settings_.resolutionSize(cfg.width, cfg.height);
    cfg.maxFps = settings_.fps;
    cfg.audioEnabled = settings_.audio;
    if (reannounce) rx_.updateConfig(cfg);
    else rx_.updateConfig(cfg);
}

int App::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    rx_.stop();
    audio_.stop();
    if (logFile_) {
        mn::log_set_sink(nullptr);
        fclose(logFile_);
    }
    return (int)msg.wParam;
}

// ================================================================== window procedure

LRESULT CALLBACK App::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)l;
        g_app = (App*)cs->lpCreateParams;
        g_app->hwnd_ = h;
    }
    if (g_app && g_app->hwnd_ == h) return g_app->handle(m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK App::EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE)) {
        if (w == VK_ESCAPE) SetWindowTextW(h, g_app->settings_.name.c_str());
        SetFocus(g_app->hwnd_);
        return 0;
    }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_app->editOrigProc_, h, m, w, l);
}

LRESULT App::handle(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        dpi_ = getDpi(hwnd_);
        edit_ = CreateWindowExW(0, L"EDIT", settings_.name.c_str(), WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_,
                                (HMENU)100, inst_, nullptr);
        SendMessageW(edit_, EM_LIMITTEXT, 48, 0);
        editOrigProc_ = (WNDPROC)SetWindowLongPtrW(edit_, GWLP_WNDPROC, (LONG_PTR)EditProc);
        editBrush_ = CreateSolidBrush(RGB(0x1B, 0x20, 0x30));
        return 0;
    }
    case WM_SIZE:
        if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(w);
        RECT* r = (RECT*)l;
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (rt_) rt_->SetDpi((float)dpi_, (float)dpi_);
        if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
        editRect_ = D2D1::RectF();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize.x = MulDiv(640, dpi_, 96);
        mm->ptMinTrackSize.y = MulDiv(480, dpi_, 96);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        render();
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_TIMER: {
        uint64_t now = mn::now_ms();
        bool animate = false;
        bool inMirror = mirrorActive_ && !settingsOpen_;
        float target = (inMirror && (now - lastMouseMove_ < 2500 || !haveFrame_ || hover_ != H_NONE)) ? 1.0f : 0.0f;
        if (inMirror && fabsf(target - overlayAlpha_) > 0.01f) {
            overlayAlpha_ += (target - overlayAlpha_) * 0.25f;
            animate = true;
        }
        if (inMirror && fullscreen_ && target == 0 && overlayAlpha_ < 0.02f) {
            POINT p;
            GetCursorPos(&p);
            RECT wr;
            GetWindowRect(hwnd_, &wr);
            if (PtInRect(&wr, p) && GetForegroundWindow() == hwnd_) SetCursor(nullptr);
        }
        if (!inMirror || !haveFrame_ || paused_) animate = true;  // home pulse, spinners, progress
        if (toastUntil_ && now < toastUntil_ + 400) animate = true;
        if (now - fpsStamp_ >= 1000) {
            fps_ = fpsCount_;
            fpsCount_ = 0;
            fpsStamp_ = now;
        }
        if (animate && !IsIconic(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_APP_FRAME:
        onFrame();
        return 0;
    case WM_APP_EVENT:
        handleEvent((Event*)l);
        return 0;
    case WM_MOUSEMOVE: {
        float x = GET_X_LPARAM(l) * 96.0f / dpi_, y = GET_Y_LPARAM(l) * 96.0f / dpi_;
        int h = hitTest(x, y);
        uint64_t now = mn::now_ms();
        if (mirrorActive_ && now - lastMouseMove_ > 100) InvalidateRect(hwnd_, nullptr, FALSE);
        lastMouseMove_ = now;
        if (h != hover_) {
            hover_ = h;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        hover_ = H_NONE;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, (hover_ != H_NONE && hover_ != H_NAMEBOX) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        pressed_ = hover_;
        if (GetFocus() == edit_ && pressed_ != H_NAMEBOX) SetFocus(hwnd_);
        SetCapture(hwnd_);
        return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        int p = pressed_;
        pressed_ = H_NONE;
        if (p != H_NONE && p == hover_) click(p);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (mirrorActive_ && !settingsOpen_ && hover_ == H_NONE) setFullscreen(!fullscreen_);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_F11 || (w == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) setFullscreen(!fullscreen_);
        else if (w == VK_ESCAPE) {
            if (fullscreen_) setFullscreen(false);
            else if (settingsOpen_) click(H_BACK);
        } else if (w == 'M') click(H_MUTE);
        return 0;
    case WM_SYSKEYDOWN:
        if (w == VK_RETURN) {
            setFullscreen(!fullscreen_);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(w) == 100 && HIWORD(w) == EN_KILLFOCUS) commitName();
        if (LOWORD(w) == 100 && (HIWORD(w) == EN_KILLFOCUS || HIWORD(w) == EN_SETFOCUS)) InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, RGB(0xF2, 0xF4, 0xF8));
        SetBkColor(dc, RGB(0x1B, 0x20, 0x30));
        return (LRESULT)editBrush_;
    }
    case WM_DESTROY:
        KillTimer(hwnd_, 1);
        discardDeviceResources();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}

// ================================================================== actions

void App::click(int hit) {
    switch (hit) {
    case H_SETTINGS:
        settingsOpen_ = true;
        SetWindowTextW(edit_, settings_.name.c_str());
        break;
    case H_BACK:
        commitName();
        settingsOpen_ = false;
        SetFocus(hwnd_);
        break;
    case H_RES0: case H_RES1: case H_RES2: case H_RES3:
        settings_.resolution = hit - H_RES0;
        applySettings(false);
        break;
    case H_FPS30: case H_FPS60:
        settings_.fps = hit == H_FPS30 ? 30 : 60;
        applySettings(false);
        break;
    case H_AUDIO:
        settings_.audio = !settings_.audio;
        applySettings(false);
        if (settings_.audio && audioActive_) audio_.start();
        if (!settings_.audio) audio_.stop();
        break;
    case H_AUTOFS:
        settings_.autoFullscreen = !settings_.autoFullscreen;
        applySettings(false);
        break;
    case H_MUTE:
        muted_ = !muted_;
        updateGain();
        showToast(muted_ ? L"Sound muted" : L"Sound on");
        break;
    case H_FULLSCREEN:
        setFullscreen(!fullscreen_);
        break;
    case H_DISCONNECT:
        rx_.disconnectClient();
        break;
    case H_FIREWALL:
    case H_FIREWALL_LINK:
        allowFirewall();
        break;
    case H_OPENLOG:
        openLog();
        break;
    case H_NAMEBOX:
        SetFocus(edit_);
        break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::commitName() {
    wchar_t buf[128];
    GetWindowTextW(edit_, buf, 128);
    std::wstring n(buf);
    while (!n.empty() && iswspace(n.back())) n.pop_back();
    while (!n.empty() && iswspace(n.front())) n.erase(0, 1);
    if (n.empty()) {
        SetWindowTextW(edit_, settings_.name.c_str());
        return;
    }
    if (n != settings_.name) {
        settings_.name = n;
        applySettings(true);
        showToast(L"Your PC now appears as “" + n + L"”");
    }
}

void App::updateGain() {
    float g = (muted_ || airplayDb_ <= -100.0f) ? 0.0f : powf(10.0f, airplayDb_ / 20.0f);
    audio_.setGain(g);
}

void App::setFullscreen(bool fs) {
    if (fs == fullscreen_) return;
    DWORD style = (DWORD)GetWindowLongW(hwnd_, GWL_STYLE);
    if (fs) {
        GetWindowPlacement(hwnd_, &placement_);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(hwnd_, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &placement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen_ = fs;
    lastMouseMove_ = mn::now_ms();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::showToast(const std::wstring& msg) {
    toast_ = msg;
    toastUntil_ = mn::now_ms() + 3000;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::openLog() {
    if (logFile_) fflush(logFile_);
    ShellExecuteW(hwnd_, L"open", L"notepad.exe", logPath_.c_str(), nullptr, SW_SHOWNORMAL);
}

void App::allowFirewall() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring e(exe);
    // Remove any earlier rules for this program (including "block" rules created when the
    // Windows Security prompt was dismissed), then allow it on every network profile.
    std::wstring args = L"/c netsh advfirewall firewall delete rule name=all program=\"" + e +
                        L"\" & netsh advfirewall firewall delete rule name=\"Mirrorly\" & "
                        L"netsh advfirewall firewall add rule name=\"Mirrorly\" dir=in action=allow program=\"" + e +
                        L"\" enable=yes profile=any & "
                        L"netsh advfirewall firewall add rule name=\"Mirrorly\" dir=in action=allow protocol=UDP localport=5353 "
                        L"enable=yes profile=any";
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd_;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        showToast(L"Firewall change cancelled");
        return;
    }
    HANDLE proc = sei.hProcess;
    std::thread([this, proc]() {
        WaitForSingleObject(proc, 30000);
        DWORD code = 1;
        GetExitCodeProcess(proc, &code);
        CloseHandle(proc);
        post(EV_FIREWALL_DONE, [&](Event& ev) { ev.b = code == 0; });
    }).detach();
}

// ================================================================== engine events

void App::handleEvent(Event* e) {
    std::unique_ptr<Event> guard(e);
    switch (e->type) {
    case EV_CONNECTING:
        connecting_ = true;
        client_ = e->client;
        break;
    case EV_SESSION_START:
        connecting_ = true;
        sessionActive_ = true;
        client_ = e->client;
        airplayDb_ = 0;
        updateGain();
        break;
    case EV_SESSION_END:
        connecting_ = sessionActive_ = mirrorActive_ = audioActive_ = paused_ = haveFrame_ = false;
        audio_.stop();
        SafeRelease(video_);
        SafeRelease(artwork_);
        title_.clear(); artist_.clear(); album_.clear();
        progDur_ = 0;
        if (fullscreen_) setFullscreen(false);
        break;
    case EV_MIRROR_START:
        mirrorActive_ = true;
        haveFrame_ = false;
        paused_ = false;
        lastMouseMove_ = mn::now_ms();
        if (settings_.autoFullscreen && !settingsOpen_) setFullscreen(true);
        break;
    case EV_MIRROR_STOP:
        mirrorActive_ = false;
        haveFrame_ = false;
        SafeRelease(video_);
        if (fullscreen_) setFullscreen(false);
        break;
    case EV_PAUSED:
        paused_ = e->b;
        break;
    case EV_AUDIO_START:
        audioActive_ = true;
        if (settings_.audio) audio_.start();
        updateGain();
        break;
    case EV_AUDIO_STOP:
        audioActive_ = false;
        audio_.stop();
        break;
    case EV_VOLUME:
        airplayDb_ = e->f;
        updateGain();
        break;
    case EV_METADATA:
        title_ = from_utf8(e->s1);
        artist_ = from_utf8(e->s2);
        album_ = from_utf8(e->s3);
        break;
    case EV_ARTWORK:
        artworkData_ = e->data;
        SafeRelease(artwork_);
        break;
    case EV_PROGRESS:
        progPos_ = e->d1;
        progDur_ = e->d2;
        progStamp_ = mn::now_ms();
        break;
    case EV_FIREWALL_DONE:
        showToast(e->b ? L"Windows Firewall now allows Mirrorly" : L"Firewall rules could not be changed");
        break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::onFrame() {
    framePosted_ = false;
    const mn::VideoFrame* f = rx_.frames().acquireRead();
    if (!f || !mirrorActive_ || !rt_) return;
    if (!video_ || videoW_ != f->width || videoH_ != f->height) {
        SafeRelease(video_);
        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(rt_->CreateBitmap(D2D1::SizeU(f->width, f->height), bp, &video_))) return;
        videoW_ = f->width;
        videoH_ = f->height;
        mn::logf(mn::LogLevel::Info, "video size %dx%d", videoW_, videoH_);
    }
    video_->CopyFromMemory(nullptr, f->pixels.data(), f->stride);
    if (!haveFrame_) lastMouseMove_ = mn::now_ms();
    haveFrame_ = true;
    fpsCount_++;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ================================================================== rendering helpers

bool App::createDeviceResources() {
    if (rt_) return true;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = props.dpiY = (float)dpi_;
    if (FAILED(d2d_->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right, rc.bottom)), &rt_)))
        return false;
    rt_->SetDpi((float)dpi_, (float)dpi_);
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    rt_->QueryInterface(__uuidof(ID2D1DeviceContext), (void**)&dc_);
    rt_->CreateSolidColorBrush(rgb(C_TEXT), &brush_);
    return true;
}

void App::discardDeviceResources() {
    SafeRelease(video_);
    SafeRelease(artwork_);
    SafeRelease(brush_);
    SafeRelease(dc_);
    SafeRelease(rt_);
}

IDWriteTextFormat* App::fmt(float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT a, bool iconFont) {
    wchar_t key[96];
    swprintf(key, 96, L"%d|%d|%d|%d", (int)(size * 10), (int)w, (int)a, iconFont ? 1 : 0);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    dw_->CreateTextFormat(iconFont ? iconFont_.c_str() : uiFont_.c_str(), nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                          DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
    if (f) {
        f->SetTextAlignment(a);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        if (!iconFont) {
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            IDWriteInlineObject* ell = nullptr;
            dw_->CreateEllipsisTrimmingSign(f, &ell);
            f->SetTrimming(&trim, ell);
            SafeRelease(ell);
        }
    }
    formats_[key] = f;
    return f;
}

void App::text(const std::wstring& s, D2D1_RECT_F r, IDWriteTextFormat* f, uint32_t color, float alpha) {
    if (!f || s.empty()) return;
    brush_->SetColor(rgb(color, alpha));
    rt_->DrawText(s.c_str(), (UINT32)s.size(), f, r, brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float App::textWidth(const std::wstring& s, IDWriteTextFormat* f) {
    IDWriteTextLayout* l = nullptr;
    if (FAILED(dw_->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 4000, 100, &l)) || !l) return 0;
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    l->Release();
    return m.widthIncludingTrailingWhitespace;
}

void App::icon(wchar_t g, float cx, float cy, float size, uint32_t color, float alpha) {
    wchar_t s[2] = {g, 0};
    IDWriteTextFormat* f = fmt(size, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    brush_->SetColor(rgb(color, alpha));
    rt_->DrawText(s, 1, f, D2D1::RectF(cx - size, cy - size, cx + size, cy + size), brush_);
}

void App::fillRound(D2D1_RECT_F r, float rad, uint32_t color, float alpha) {
    brush_->SetColor(rgb(color, alpha));
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), brush_);
}

void App::strokeRound(D2D1_RECT_F r, float rad, uint32_t color, float alpha, float w) {
    brush_->SetColor(rgb(color, alpha));
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), brush_, w);
}

void App::iconButton(int id, wchar_t g, float cx, float cy, float size, bool overlay, float alpha) {
    float r = size / 2;
    D2D1_RECT_F rc = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);
    bool hov = hover_ == id;
    if (overlay) {
        brush_->SetColor(D2D1::ColorF(1, 1, 1, (hov ? 0.22f : 0.12f) * alpha));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush_);
    } else if (hov) {
        fillRound(rc, 8, C_SURFACE2);
    }
    uint32_t col = id == H_DISCONNECT && hov ? C_RED : C_TEXT;
    icon(g, cx, cy, size * 0.42f, col, alpha);
    addHit(id, rc);
}

void App::toggle(int id, bool on, float x, float y) {
    float w = 44, h = 24;
    D2D1_RECT_F r = D2D1::RectF(x, y - h / 2, x + w, y + h / 2);
    if (on) {
        fillRound(r, h / 2, C_ACCENT, hover_ == id ? 0.9f : 1.0f);
    } else {
        fillRound(r, h / 2, C_SURFACE2);
        strokeRound(r, h / 2, hover_ == id ? C_TEXT3 : C_BORDER, 1.0f, 1.0f);
    }
    float kx = on ? x + w - h / 2 : x + h / 2;
    brush_->SetColor(rgb(on ? 0xFFFFFF : C_TEXT2));
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, y), 8, 8), brush_);
    addHit(id, D2D1::RectF(x - 6, y - 16, x + w + 6, y + 16));
}

void App::segmented(const std::vector<std::pair<int, std::wstring>>& items, int selected, float right, float cy) {
    IDWriteTextFormat* f = fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    float segW = 64, h = 32, pad = 3;
    float total = segW * items.size() + pad * 2;
    D2D1_RECT_F outer = D2D1::RectF(right - total, cy - h / 2, right, cy + h / 2);
    fillRound(outer, 9, C_SURFACE2);
    for (size_t i = 0; i < items.size(); i++) {
        D2D1_RECT_F r = D2D1::RectF(outer.left + pad + segW * i, outer.top + pad, outer.left + pad + segW * (i + 1), outer.bottom - pad);
        bool sel = items[i].first == selected;
        if (sel) fillRound(r, 7, C_ACCENT);
        else if (hover_ == items[i].first) fillRound(r, 7, C_BORDER);
        text(items[i].second, r, f, sel ? 0xFFFFFF : C_TEXT2);
        addHit(items[i].first, r);
    }
}

void App::spinner(float cx, float cy, float r, uint32_t color) {
    double t = (mn::now_ms() - startTime_) / 1000.0;
    int lead = (int)(t * 12) % 12;
    for (int i = 0; i < 12; i++) {
        float a = (float)(i * 2 * M_PI / 12);
        int age = (lead - i + 12) % 12;
        float alpha = std::max(0.12f, 1.0f - age / 8.0f);
        brush_->SetColor(rgb(color, alpha));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * sinf(a), cy - r * cosf(a)), r * 0.16f, r * 0.16f), brush_);
    }
}

int App::hitTest(float x, float y) {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        auto& r = it->second;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return it->first;
    }
    return H_NONE;
}

void App::layoutEdit(bool visible, D2D1_RECT_F r) {
    if (!visible) {
        if (editVisible_) {
            if (GetFocus() == edit_) SetFocus(hwnd_);
            ShowWindow(edit_, SW_HIDE);
            editVisible_ = false;
        }
        return;
    }
    if (!editFont_) {
        editFont_ = CreateFontW(-MulDiv(14, dpi_, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                L"Segoe UI");
        SendMessageW(edit_, WM_SETFONT, (WPARAM)editFont_, TRUE);
    }
    if (memcmp(&r, &editRect_, sizeof(r)) != 0 || !editVisible_) {
        editRect_ = r;
        float s = dpi_ / 96.0f;
        int fh = MulDiv(20, dpi_, 96);
        int x = (int)((r.left + 12) * s), w = (int)((r.right - r.left - 24) * s);
        int y = (int)(((r.top + r.bottom) / 2) * s) - fh / 2;
        MoveWindow(edit_, x, y, w, fh, TRUE);
        ShowWindow(edit_, SW_SHOW);
        editVisible_ = true;
    }
}

// ================================================================== views

void App::drawTopBar(float W, bool back, const wchar_t* title) {
    float y = 32;
    if (back) {
        iconButton(H_BACK, G_BACK, 36, y, 36);
        text(title, D2D1::RectF(64, y - 14, W - 80, y + 14), fmt(17, DWRITE_FONT_WEIGHT_SEMI_BOLD), C_TEXT);
        return;
    }
    // logo
    D2D1_RECT_F lr = D2D1::RectF(22, y - 14, 50, y + 14);
    ID2D1GradientStopCollection* stops = nullptr;
    D2D1_GRADIENT_STOP gs[2] = {{0.0f, rgb(C_ACCENT)}, {1.0f, rgb(C_ACCENT2)}};
    rt_->CreateGradientStopCollection(gs, 2, &stops);
    ID2D1LinearGradientBrush* gb = nullptr;
    if (stops) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(lr.left, lr.top), D2D1::Point2F(lr.right, lr.bottom)), stops, &gb);
    if (gb) rt_->FillRoundedRectangle(D2D1::RoundedRect(lr, 8, 8), gb);
    SafeRelease(gb);
    SafeRelease(stops);
    icon(G_TV, 36, y, 14, 0xFFFFFF);
    text(title, D2D1::RectF(60, y - 14, W - 80, y + 14), fmt(16, DWRITE_FONT_WEIGHT_SEMI_BOLD), C_TEXT);
}

void App::drawHome(float W, float H) {
    drawTopBar(W, false, kAppName);
    iconButton(H_SETTINGS, G_SETTINGS, W - 36, 32, 36);

    float cx = W / 2;
    float cy = std::max(170.0f, H * 0.36f);
    double t = (mn::now_ms() - startTime_) / 1000.0;
    bool busy = connecting_ || sessionActive_;

    // soft glow
    ID2D1GradientStopCollection* stops = nullptr;
    D2D1_GRADIENT_STOP gs[2] = {{0.0f, rgb(C_ACCENT, 0.16f)}, {1.0f, rgb(C_ACCENT, 0.0f)}};
    rt_->CreateGradientStopCollection(gs, 2, &stops);
    ID2D1RadialGradientBrush* rg = nullptr;
    if (stops) rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), 260, 260), stops, &rg);
    if (rg) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 260, 260), rg);
    SafeRelease(rg);
    SafeRelease(stops);

    // pulse rings
    for (int k = 0; k < 3; k++) {
        double ph = fmod(t / 2.6 + k / 3.0, 1.0);
        float rad = 50 + (float)ph * 80;
        brush_->SetColor(rgb(busy ? C_ACCENT2 : C_ACCENT, (float)(1.0 - ph) * 0.45f));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), brush_, 1.5f);
    }
    // center disc
    D2D1_GRADIENT_STOP gs2[2] = {{0.0f, rgb(C_ACCENT)}, {1.0f, rgb(C_ACCENT2)}};
    rt_->CreateGradientStopCollection(gs2, 2, &stops);
    ID2D1LinearGradientBrush* lg = nullptr;
    if (stops) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(cx - 46, cy - 46), D2D1::Point2F(cx + 46, cy + 46)), stops, &lg);
    if (lg) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 46, 46), lg);
    SafeRelease(lg);
    SafeRelease(stops);
    if (busy) spinner(cx, cy, 18, 0xFFFFFF);
    else icon(G_PHONE, cx, cy, 30, 0xFFFFFF);

    float y = cy + 150;
    text(settings_.name, D2D1::RectF(40, y - 22, W - 40, y + 22), fmt(30, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER), C_TEXT);
    y += 46;

    // status pill
    std::wstring status;
    uint32_t dot;
    if (busy) {
        status = L"Connecting to " + from_utf8(client_.name) + L"…";
        dot = C_ACCENT2;
    } else if (!rx_.running()) {
        status = L"AirPlay service stopped";
        dot = C_RED;
    } else if (!rx_.discoveryOk()) {
        status = L"Ready — but network discovery is blocked";
        dot = C_AMBER;
    } else {
        status = L"Ready for AirPlay";
        dot = C_GREEN;
    }
    IDWriteTextFormat* pf = fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    float tw = textWidth(status, pf);
    float pw = tw + 40;
    D2D1_RECT_F pill = D2D1::RectF(cx - pw / 2, y - 15, cx + pw / 2, y + 15);
    fillRound(pill, 15, C_SURFACE);
    strokeRound(pill, 15, C_BORDER);
    float pulse = 0.65f + 0.35f * (float)sin(t * 3.0);
    brush_->SetColor(rgb(dot, busy ? pulse : 1.0f));
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pill.left + 17, y), 4.5f, 4.5f), brush_);
    text(status, D2D1::RectF(pill.left + 29, y - 15, pill.right, y + 15), pf, C_TEXT);
    y += 44;

    // instructions
    std::wstring how = L"On your iPhone or iPad, open Control Center, tap Screen Mirroring and choose “" +
                       settings_.name + L"”. To stream music only, pick it from the AirPlay audio menu.";
    float iw = std::min(480.0f, W - 80);
    text(how, D2D1::RectF(cx - iw / 2, y - 6, cx + iw / 2, y + 44), fmt(14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), C_TEXT2);

    // footer
    float fy = H - 26;
    std::wstring ips;
    for (auto& ni : mn::list_ipv4_interfaces()) {
        if (!ips.empty()) ips += L", ";
        ips += from_utf8(mn::ip_to_string(ni.addr));
        if (ips.size() > 40) break;
    }
    if (ips.empty()) ips = L"No network connection";
    std::wstring left = ips + L"  ·  port " + std::to_wstring(rx_.port());
    text(left, D2D1::RectF(24, fy - 12, W / 2, fy + 12), fmt(12), C_TEXT3);
    std::wstring link = L"Not showing up on your iPhone?";
    IDWriteTextFormat* lf = fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
    float lw = textWidth(link, lf);
    D2D1_RECT_F lr = D2D1::RectF(W - 24 - lw, fy - 12, W - 24, fy + 12);
    text(link, lr, lf, hover_ == H_FIREWALL_LINK ? C_TEXT : C_ACCENT);
    addHit(H_FIREWALL_LINK, lr);
}

void App::drawSettings(float W, float H) {
    drawTopBar(W, true, L"Settings");
    float cw = std::min(640.0f, W - 48);
    float x0 = (W - cw) / 2, x1 = x0 + cw;
    float y = 72;
    IDWriteTextFormat* lab = fmt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    IDWriteTextFormat* desc = fmt(12);
    const float rowH = 68;

    auto section = [&](const wchar_t* title, int rows) {
        text(title, D2D1::RectF(x0 + 4, y, x1, y + 24), fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD), C_TEXT3);
        y += 28;
        D2D1_RECT_F card = D2D1::RectF(x0, y, x1, y + rowH * rows);
        fillRound(card, 12, C_SURFACE);
        strokeRound(card, 12, C_BORDER, 0.7f);
        for (int i = 1; i < rows; i++) {
            brush_->SetColor(rgb(C_BORDER, 0.7f));
            rt_->DrawLine(D2D1::Point2F(x0 + 18, y + rowH * i), D2D1::Point2F(x1 - 18, y + rowH * i), brush_, 1.0f);
        }
    };
    auto label = [&](float ry, const wchar_t* l, const wchar_t* d, float rightReserve) {
        text(l, D2D1::RectF(x0 + 20, ry - 20, x1 - rightReserve, ry), lab, C_TEXT);
        text(d, D2D1::RectF(x0 + 20, ry, x1 - rightReserve, ry + 18), desc, C_TEXT2);
    };

    // ---- Receiver
    section(L"RECEIVER", 1);
    float ry = y + rowH / 2;
    label(ry, L"Name", L"How this PC appears on your devices", 300);
    D2D1_RECT_F box = D2D1::RectF(x1 - 20 - 260, ry - 18, x1 - 20, ry + 18);
    bool focused = GetFocus() == edit_;
    fillRound(box, 8, C_SURFACE2);
    strokeRound(box, 8, focused ? C_ACCENT : (hover_ == H_NAMEBOX ? C_TEXT3 : C_BORDER), 1.0f, focused ? 1.5f : 1.0f);
    addHit(H_NAMEBOX, box);
    layoutEdit(true, box);
    y += rowH + 22;

    // ---- Display
    section(L"SCREEN MIRRORING", 3);
    ry = y + rowH / 2;
    label(ry, L"Resolution", L"Highest quality your iPhone will send", 300);
    segmented({{H_RES0, L"720p"}, {H_RES1, L"1080p"}, {H_RES2, L"1440p"}, {H_RES3, L"4K"}}, H_RES0 + settings_.resolution, x1 - 20, ry);
    ry += rowH;
    label(ry, L"Frame rate", L"60 fps is smoother, 30 fps uses less bandwidth", 180);
    segmented({{H_FPS30, L"30"}, {H_FPS60, L"60"}}, settings_.fps == 30 ? H_FPS30 : H_FPS60, x1 - 20, ry);
    ry += rowH;
    label(ry, L"Full screen when mirroring starts", L"Press Esc to leave full screen, F11 to toggle", 90);
    toggle(H_AUTOFS, settings_.autoFullscreen, x1 - 20 - 44, ry);
    y += rowH * 3 + 22;

    // ---- Audio & network
    section(L"AUDIO & NETWORK", 2);
    ry = y + rowH / 2;
    label(ry, L"Play audio", L"Sound from your iPhone plays on this PC", 90);
    toggle(H_AUDIO, settings_.audio, x1 - 20 - 44, ry);
    ry += rowH;
    label(ry, L"Windows Firewall", L"Fixes discovery when the PC is not listed on your iPhone", 140);
    {
        IDWriteTextFormat* bf = fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F b = D2D1::RectF(x1 - 20 - 110, ry - 17, x1 - 20, ry + 17);
        fillRound(b, 8, hover_ == H_FIREWALL ? C_BORDER : C_SURFACE2);
        strokeRound(b, 8, C_BORDER);
        icon(G_SHIELD, b.left + 18, ry, 13, C_ACCENT);
        text(L"Allow", D2D1::RectF(b.left + 26, b.top, b.right, b.bottom), bf, C_TEXT);
        addHit(H_FIREWALL, b);
    }
    y += rowH * 2 + 18;

    // ---- footer
    std::wstring ver = std::wstring(kAppName) + L" " + kVersion + L"  ·  AirPlay port " + std::to_wstring(rx_.port());
    text(ver, D2D1::RectF(x0 + 4, y, x0 + cw / 2 + 60, y + 22), fmt(12), C_TEXT3);
    IDWriteTextFormat* lf = fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
    D2D1_RECT_F lr = D2D1::RectF(x1 - 120, y, x1 - 4, y + 22);
    text(L"Open log file", lr, lf, hover_ == H_OPENLOG ? C_TEXT : C_ACCENT);
    addHit(H_OPENLOG, lr);
    (void)H;
}

void App::drawMirror(float W, float H) {
    rt_->Clear(D2D1::ColorF(0, 0, 0));
    if (video_ && haveFrame_) {
        float vw = (float)videoW_, vh = (float)videoH_;
        float s = std::min(W / vw, H / vh);
        float dw = vw * s, dh = vh * s;
        D2D1_RECT_F dst = D2D1::RectF((W - dw) / 2, (H - dh) / 2, (W + dw) / 2, (H + dh) / 2);
        if (dc_) dc_->DrawBitmap(video_, &dst, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        else rt_->DrawBitmap(video_, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (paused_) {
            brush_->SetColor(D2D1::ColorF(0, 0, 0, 0.55f));
            rt_->FillRectangle(D2D1::RectF(0, 0, W, H), brush_);
            std::wstring msg = L"Paused — the screen is locked or asleep on " + from_utf8(client_.name);
            IDWriteTextFormat* f = fmt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
            float tw = textWidth(msg, f) + 36;
            D2D1_RECT_F p = D2D1::RectF(W / 2 - tw / 2, H / 2 - 20, W / 2 + tw / 2, H / 2 + 20);
            fillRound(p, 20, C_SURFACE, 0.95f);
            text(msg, p, f, C_TEXT);
        }
    } else {
        spinner(W / 2, H / 2 - 12, 16, C_TEXT2);
        text(L"Starting screen mirroring from " + from_utf8(client_.name) + L"…",
             D2D1::RectF(0, H / 2 + 16, W, H / 2 + 44), fmt(14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), C_TEXT2);
    }

    // overlay controls
    float a = overlayAlpha_;
    if (a < 0.02f) return;
    ID2D1GradientStopCollection* stops = nullptr;
    D2D1_GRADIENT_STOP gs[2] = {{0.0f, D2D1::ColorF(0, 0, 0, 0.7f * a)}, {1.0f, D2D1::ColorF(0, 0, 0, 0)}};
    rt_->CreateGradientStopCollection(gs, 2, &stops);
    ID2D1LinearGradientBrush* lg = nullptr;
    if (stops) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 96)), stops, &lg);
    if (lg) rt_->FillRectangle(D2D1::RectF(0, 0, W, 96), lg);
    SafeRelease(lg);
    SafeRelease(stops);

    icon(G_PHONE, 32, 36, 16, 0xFFFFFF, a);
    text(from_utf8(client_.name), D2D1::RectF(52, 18, W - 200, 38), fmt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), 0xFFFFFF, a);
    std::wstring sub;
    if (haveFrame_) sub = std::to_wstring(videoW_) + L" × " + std::to_wstring(videoH_) + L"  ·  " + std::to_wstring(fps_) + L" fps";
    else sub = L"Waiting for video";
    if (audioActive_) sub += muted_ ? L"  ·  muted" : L"  ·  audio on";
    text(sub, D2D1::RectF(52, 38, W - 200, 56), fmt(12), 0xD0D5DD, a);

    float bx = W - 30;
    iconButton(H_DISCONNECT, G_CLOSE, bx, 36, 40, true, a);
    bx -= 50;
    iconButton(H_FULLSCREEN, fullscreen_ ? G_WINDOW : G_FULL, bx, 36, 40, true, a);
    bx -= 50;
    iconButton(H_MUTE, muted_ ? G_MUTE : G_VOLUME, bx, 36, 40, true, a);
    bx -= 50;
    iconButton(H_SETTINGS, G_SETTINGS, bx, 36, 40, true, a);
}

void App::drawNowPlaying(float W, float H) {
    drawTopBar(W, false, kAppName);
    iconButton(H_DISCONNECT, G_CLOSE, W - 36, 32, 36);
    iconButton(H_SETTINGS, G_SETTINGS, W - 80, 32, 36);

    if (!artwork_ && !artworkData_.empty() && wic_) {
        IWICStream* st = nullptr;
        IWICBitmapDecoder* dec = nullptr;
        IWICBitmapFrameDecode* fr = nullptr;
        IWICFormatConverter* conv = nullptr;
        if (SUCCEEDED(wic_->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory(artworkData_.data(), (DWORD)artworkData_.size())) &&
            SUCCEEDED(wic_->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnLoad, &dec)) &&
            SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(wic_->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            rt_->CreateBitmapFromWicBitmap(conv, nullptr, &artwork_);
        }
        SafeRelease(conv);
        SafeRelease(fr);
        SafeRelease(dec);
        SafeRelease(st);
        if (!artwork_) artworkData_.clear();
    }

    float size = std::min(260.0f, std::min(W * 0.5f, H - 300));
    size = std::max(size, 120.0f);
    float cx = W / 2, top = std::max(84.0f, (H - size - 190) / 2);
    D2D1_RECT_F art = D2D1::RectF(cx - size / 2, top, cx + size / 2, top + size);
    if (artwork_) {
        ID2D1RoundedRectangleGeometry* g = nullptr;
        d2d_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(art, 14, 14), &g);
        ID2D1Layer* layer = nullptr;
        rt_->CreateLayer(&layer);
        if (g && layer) rt_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g), layer);
        rt_->DrawBitmap(artwork_, art, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (g && layer) rt_->PopLayer();
        SafeRelease(layer);
        SafeRelease(g);
    } else {
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2] = {{0.0f, rgb(C_ACCENT)}, {1.0f, rgb(C_ACCENT2)}};
        rt_->CreateGradientStopCollection(gs, 2, &stops);
        ID2D1LinearGradientBrush* lg = nullptr;
        if (stops) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(art.left, art.top), D2D1::Point2F(art.right, art.bottom)), stops, &lg);
        if (lg) rt_->FillRoundedRectangle(D2D1::RoundedRect(art, 14, 14), lg);
        SafeRelease(lg);
        SafeRelease(stops);
        icon(G_MUSIC, cx, art.top + size / 2, size * 0.28f, 0xFFFFFF, 0.9f);
    }
    float y = art.bottom + 34;
    std::wstring t = title_.empty() ? L"Playing audio" : title_;
    text(t, D2D1::RectF(40, y - 16, W - 40, y + 16), fmt(22, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER), C_TEXT);
    y += 28;
    std::wstring sub = artist_;
    if (!album_.empty()) sub += (sub.empty() ? L"" : L" — ") + album_;
    if (sub.empty()) sub = L"From " + from_utf8(client_.name);
    text(sub, D2D1::RectF(40, y - 12, W - 40, y + 12), fmt(14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), C_TEXT2);
    y += 34;

    float bw = std::min(380.0f, W - 120);
    if (progDur_ > 0) {
        double pos = progPos_ + (mn::now_ms() - progStamp_) / 1000.0;
        pos = std::min(pos, progDur_);
        float frac = (float)(pos / progDur_);
        D2D1_RECT_F bar = D2D1::RectF(cx - bw / 2, y - 2, cx + bw / 2, y + 2);
        fillRound(bar, 2, C_SURFACE2);
        fillRound(D2D1::RectF(bar.left, bar.top, bar.left + bw * frac, bar.bottom), 2, C_TEXT);
        auto fmtTime = [](double s) {
            int v = (int)s;
            wchar_t b[16];
            swprintf(b, 16, L"%d:%02d", v / 60, v % 60);
            return std::wstring(b);
        };
        text(fmtTime(pos), D2D1::RectF(bar.left, y + 6, bar.left + 80, y + 24), fmt(11), C_TEXT3);
        text(fmtTime(progDur_), D2D1::RectF(bar.right - 80, y + 6, bar.right, y + 24), fmt(11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING), C_TEXT3);
        y += 40;
    } else {
        y += 8;
    }
    // level meter + mute
    iconButton(H_MUTE, muted_ ? G_MUTE : G_VOLUME, cx - 70, y + 4, 36);
    float lvl = audio_.level();
    for (int i = 0; i < 10; i++) {
        float on = lvl * 14 > i ? 1.0f : 0.0f;
        D2D1_RECT_F b = D2D1::RectF(cx - 40 + i * 12, y - 2, cx - 40 + i * 12 + 8, y + 10);
        fillRound(b, 2, on > 0 ? C_ACCENT : C_SURFACE2);
    }
    if (!settings_.audio) {
        text(L"Audio playback is turned off in Settings", D2D1::RectF(0, y + 24, W, y + 44),
             fmt(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), C_AMBER);
    }
}

void App::drawToast(float W, float H) {
    uint64_t now = mn::now_ms();
    if (!toastUntil_ || now > toastUntil_ + 300) return;
    float a = now > toastUntil_ ? 1.0f - (now - toastUntil_) / 300.0f : 1.0f;
    IDWriteTextFormat* f = fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    float tw = std::min(textWidth(toast_, f) + 40, W - 40);
    D2D1_RECT_F r = D2D1::RectF(W / 2 - tw / 2, H - 92, W / 2 + tw / 2, H - 56);
    fillRound(r, 18, C_SURFACE2, 0.97f * a);
    strokeRound(r, 18, C_BORDER, a);
    text(toast_, r, f, C_TEXT, a);
}

void App::render() {
    if (!createDeviceResources()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = rc.right * 96.0f / dpi_, H = rc.bottom * 96.0f / dpi_;
    hits_.clear();
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(rgb(C_BG));
    bool showSettings = settingsOpen_;
    if (showSettings) {
        drawSettings(W, H);
    } else {
        layoutEdit(false, D2D1::RectF());
        if (mirrorActive_) drawMirror(W, H);
        else if (sessionActive_ && audioActive_) drawNowPlaying(W, H);
        else drawHome(W, H);
    }
    drawToast(W, H);
    HRESULT hr = rt_->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
        haveFrame_ = false;
    }
    // keep the hover state correct after layout changes
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(hwnd_, &p);
    int h = hitTest(p.x * 96.0f / dpi_, p.y * 96.0f / dpi_);
    if (h != hover_ && GetCapture() != hwnd_) {
        RECT cr;
        GetClientRect(hwnd_, &cr);
        if (PtInRect(&cr, p)) hover_ = h;
    }
}

// ================================================================== entry point

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    // per-monitor DPI awareness (the manifest also declares it)
    typedef BOOL(WINAPI * SetCtx)(HANDLE);
    if (auto fn = (SetCtx)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        fn((HANDLE)-4);  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Mirrorly.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"MirrorlyWindow", nullptr);
        if (other) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    mn::net_init();
    App app;
    if (!app.init(inst, show)) {
        MessageBoxW(nullptr, L"Mirrorly could not start (Direct2D is unavailable).", kAppName, MB_ICONERROR);
        return 1;
    }
    int r = app.run();
    if (mutex) CloseHandle(mutex);
    return r;
}
