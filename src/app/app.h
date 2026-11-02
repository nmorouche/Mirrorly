// Mirrorly - application window, state and rendering (Win32 + Direct2D/DirectWrite)
#pragma once
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

#include <atomic>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "audio_out.h"
#include "receiver.h"
#include "settings.h"

#define WM_APP_EVENT (WM_APP + 1)
#define WM_APP_FRAME (WM_APP + 2)

#ifndef MIRRORLY_VERSION
#define MIRRORLY_VERSION "1.0.0"
#endif
static const wchar_t* const kAppName = L"Mirrorly";
static const wchar_t* const kVersion = L"" MIRRORLY_VERSION;

template <class T> static inline void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

// ------------------------------------------------------------------ identity: "acid poster"
// Concrete-grey paper, giant acid-lime type bleeding off the edges, an electric cobalt block
// carrying pixel lettering, a gunmetal halftone object and small black technical labels.
namespace tok {
const uint32_t PAPER  = 0xC4C7BD;  // background: concrete grey with a green cast
const uint32_t PAPER2 = 0xB2B5AA;  // hairlines and quiet outlines on paper
const uint32_t INK    = 0x000000;  // primary text
const uint32_t INK2   = 0x34372F;  // secondary text
const uint32_t INK3   = 0x666A5F;  // tertiary text
const uint32_t ACID   = 0xD4FF1E;  // signal: giant type, ready, selected
const uint32_t COBALT = 0x2F3DFF;  // identity block, primary action, connecting
const uint32_t WHITE  = 0xF5F6F0;  // text on cobalt / on video
const uint32_t GUN    = 0x1F211E;  // halftone gunmetal
const uint32_t ALERT  = 0xFF4A17;  // destructive, problems
}  // namespace tok

static inline D2D1_COLOR_F rgb(uint32_t c, float a = 1.0f) {
    return D2D1::ColorF(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, a);
}

// icon glyphs (Segoe Fluent Icons / Segoe MDL2 Assets)
static const wchar_t G_SETTINGS = 0xE713, G_BACK = 0xE72B, G_CLOSE = 0xE711, G_FULL = 0xE740, G_WINDOW = 0xE73F,
                     G_VOLUME = 0xE767, G_MUTE = 0xE74F, G_MUSIC = 0xE8D6, G_SHIELD = 0xEA18;

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

enum FontRole { F_UI, F_CONDENSED, F_ICON };

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

    static App* instance;

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

    // ---- window / behaviour (main.cpp)
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

    // ---- rendering (render.cpp)
    bool createDeviceResources();
    void discardDeviceResources();
    void render();
    double animTime() const;
    void drawHome(float W, float H);
    void drawSettings(float W, float H);
    void drawMirror(float W, float H);
    void drawNowPlaying(float W, float H);
    void drawTopBar(float W, bool back, const wchar_t* title);
    void drawToast(float W, float H);
    // poster elements
    void drawGrain(float W, float H, float opacity);
    void drawHalftoneOrb(float cx, float cy, float r, double t, float energy, uint32_t ink, uint32_t rim);
    void drawCracks(float cx, float cy, float r, float W, float H, uint32_t color, float alpha);
    void giantType(const std::wstring& s, float x, float y, float size, uint32_t color, float alpha = 1.0f);
    float pixelText(const std::wstring& s, float x, float y, float px, uint32_t color, float alpha = 1.0f);
    float pixelTextWidth(const std::wstring& s, float px) const;
    D2D1_RECT_F pixelBlock(const std::wstring& s, float x, float y, float maxW, float px, uint32_t bg, uint32_t fg);
    void labelColumn(const std::vector<std::wstring>& lines, float right, float top, uint32_t color);
    // primitives
    IDWriteTextFormat* fmt(float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL,
                           DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING, FontRole role = F_UI);
    void text(const std::wstring& s, D2D1_RECT_F r, IDWriteTextFormat* f, uint32_t color, float alpha = 1.0f);
    float textWidth(const std::wstring& s, IDWriteTextFormat* f);
    void icon(wchar_t g, float cx, float cy, float size, uint32_t color, float alpha = 1.0f);
    void fillRect(D2D1_RECT_F r, uint32_t color, float alpha = 1.0f, float radius = 0);
    void strokeRect(D2D1_RECT_F r, uint32_t color, float alpha = 1.0f, float radius = 0, float w = 1.0f);
    void line(float x0, float y0, float x1, float y1, uint32_t color, float alpha = 1.0f, float w = 1.0f);
    void dot(float cx, float cy, float r, uint32_t color, float alpha = 1.0f);
    void iconButton(int id, wchar_t g, float cx, float cy, float size, float alpha = 1.0f, bool destructive = false);
    void toggle(int id, bool on, float x, float y);
    void choices(const std::vector<std::pair<int, std::wstring>>& items, int selected, float right, float cy);
    void link(int id, const std::wstring& s, float x, float y, bool alignRight);
    void addHit(int id, D2D1_RECT_F r) { hits_.push_back({id, r}); }
    int hitTest(float x, float y);
    void layoutEdit(bool visible, D2D1_RECT_F r);

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    WNDPROC editOrigProc_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    UINT dpi_ = 96;
    bool reduceMotion_ = false;

    ID2D1Factory* d2d_ = nullptr;
    IDWriteFactory* dw_ = nullptr;
    IWICImagingFactory* wic_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    ID2D1BitmapBrush* grain_ = nullptr;
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

UINT app_get_dpi(HWND h);
