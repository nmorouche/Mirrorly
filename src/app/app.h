// Mirrorly - application window, state, drawing toolkit and the theme interface
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
#include <utility>
#include <vector>

#include "android_mirror.h"
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

static inline D2D1_COLOR_F rgb(uint32_t c, float a = 1.0f) {
    return D2D1::ColorF(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, a);
}

// icon glyphs (Segoe Fluent Icons / Segoe MDL2 Assets)
static const wchar_t G_SETTINGS = 0xE713, G_BACK = 0xE72B, G_CLOSE = 0xE711, G_FULL = 0xE740, G_WINDOW = 0xE73F,
                     G_VOLUME = 0xE767, G_MUTE = 0xE74F, G_MUSIC = 0xE8D6, G_SHIELD = 0xEA18, G_LOCK = 0xE72E,
                     G_CHECK = 0xE73E, G_USB = 0xE88E;

// ------------------------------------------------------------------ events from the engine
enum EvType {
    EV_CONNECTING, EV_SESSION_START, EV_SESSION_END, EV_MIRROR_START, EV_MIRROR_STOP, EV_PAUSED, EV_AUDIO_START,
    EV_AUDIO_STOP, EV_VOLUME, EV_METADATA, EV_ARTWORK, EV_PROGRESS, EV_FIREWALL_DONE, EV_ANDROID_STATUS
};
struct Event {
    EvType type;
    mn::ClientInfo client;
    float f = 0;
    bool b = false;
    std::string s1, s2, s3;
    std::vector<uint8_t> data;
    double d1 = 0, d2 = 0;
    int i = 0;
};

// clickable things. Theme cards use H_THEME + index.
enum Hit {
    H_NONE, H_SETTINGS, H_BACK, H_FIREWALL, H_RES0, H_RES1, H_RES2, H_RES3, H_FPS30, H_FPS60, H_AUDIO, H_AUTOFS,
    H_MUTE, H_FULLSCREEN, H_DISCONNECT, H_OPENLOG, H_FIREWALL_LINK, H_NAMEBOX, H_THEME_STRIP, H_ANDROID, H_USB_ACTION,
    H_VOL_POPOVER, H_VOL_ICON, H_VOL_SLIDER,
    H_THEME = 100
};

// Font roles. Each theme maps F_UI to its own family; F_UI_DISPLAY is used for large type.
enum FontRole { F_UI, F_UI_DISPLAY, F_CONDENSED, F_ICON };

typedef std::vector<std::pair<int, std::wstring>> ChoiceList;

class App;

// ------------------------------------------------------------------ themes
// A theme draws every screen and every settings control. To add one:
//   1. create src/app/theme_<name>.cpp with a class deriving from Theme
//   2. add it to allThemes() in themes.cpp (and an icon in res/app.rc if it has its own)
class Theme {
public:
    virtual ~Theme() {}
    // identity
    virtual const wchar_t* id() const = 0;      // saved in settings.ini
    virtual const wchar_t* name() const = 0;    // shown in the theme picker
    virtual int iconResource() const = 0;       // window / taskbar icon (resource id)
    virtual void onActivate(App&) {}            // theme became current (e.g. read system light/dark)
    // window chrome
    virtual bool darkTitleBar(App&) const = 0;
    virtual uint32_t windowColor(App&) const = 0;       // caption + background behind the first paint
    virtual uint32_t editBackground(App&) const = 0;    // native name field
    virtual uint32_t editText(App&) const = 0;
    virtual const wchar_t* uiFont(App&, bool display) const = 0;

    // full screens
    virtual void drawHome(App&, float W, float H) = 0;
    virtual void drawNowPlaying(App&, float W, float H) = 0;
    virtual void drawMirrorWaiting(App&, float W, float H) = 0;
    virtual void drawMirrorPaused(App&, float W, float H, D2D1_RECT_F video) = 0;
    virtual void drawMirrorOverlay(App&, float W, float H, float alpha, D2D1_RECT_F video) = 0;
    virtual void drawToast(App&, float W, float H, const std::wstring& msg, float alpha) = 0;
    // thumbnail for the theme picker
    virtual void drawPreview(App&, D2D1_RECT_F r) = 0;

    // settings building blocks (layout is shared, see App::drawSettings)
    virtual float headerHeight() const { return 72; }
    virtual void settingsColumn(float W, float& x0, float& x1) const = 0;
    virtual float rowHeight(float H) const = 0;
    virtual float rowInset() const = 0;                 // padding inside a group
    virtual void settingsBackground(App&, float W, float H) = 0;
    virtual void settingsHeader(App&, float W, bool scrolled) = 0;
    virtual float sectionTitle(App&, float x0, float x1, float y, const wchar_t* title) = 0;  // returns next y
    virtual void group(App&, D2D1_RECT_F r, int rows, float rowH) = 0;
    virtual void rowText(App&, float x0, float x1, float cy, const wchar_t* label, const wchar_t* desc) = 0;
    virtual void toggle(App&, int id, bool on, float right, float cy) = 0;
    virtual void choices(App&, const ChoiceList& items, int selected, float right, float cy) = 0;
    virtual void button(App&, int id, const wchar_t* label, wchar_t glyph, float right, float cy) = 0;
    // draws the name field and returns where the native edit box goes
    virtual D2D1_RECT_F textField(App&, D2D1_RECT_F r, bool focused, bool hovered) = 0;
    virtual void link(App&, int id, const std::wstring& s, float x, float cy, bool alignRight) = 0;
    virtual void footnote(App&, const std::wstring& s, float x, float cy) = 0;
    virtual void themeCard(App&, D2D1_RECT_F preview, Theme* t, bool selected, bool hovered) = 0;
};

std::vector<Theme*>& allThemes();
Theme* findTheme(const std::wstring& id);

// ------------------------------------------------------------------ the application
// Members are public: themes draw with the toolkit below and read the session state.
class App : public mn::ReceiverListener, public mn::AndroidStatusListener {
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
    // AndroidStatusListener (engine threads)
    void onAndroidStatus(const mn::AndroidStatus& s) override {
        post(EV_ANDROID_STATUS, [&](Event& e) { e.i = (int)s.state; e.s1 = s.device; e.s2 = s.detail; });
    }

    static App* instance;

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
    void applyTheme(bool announce);
    void readSystemTheme();
    void commitName();
    void click(int hit);
    void setFullscreen(bool fs);
    void updateGain();
    void allowFirewall();
    void showToast(const std::wstring& msg);
    void openLog();
    // wired mirroring (android_usb.cpp)
    void startAndroid();
    mn::AndroidConfig androidConfig() const;
    bool launchAdb();
    void stopAdb();
    void checkUsbIphone();
    bool clientIsUsb() const { return client_.ip == "USB"; }

    // ---- rendering (render.cpp)
    bool createDeviceResources();
    void discardDeviceResources();
    void render();
    double animTime() const;
    void drawSettings(float W, float H);
    void drawMirror(float W, float H);
    void ensureArtwork();
    D2D1_RECT_F videoRect(float W, float H) const;

    // ---- drawing toolkit (render.cpp), shared by all themes
    IDWriteTextFormat* fmt(float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL,
                           DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING, FontRole role = F_UI);
    void text(const std::wstring& s, D2D1_RECT_F r, IDWriteTextFormat* f, uint32_t color, float alpha = 1.0f);
    float textWidth(const std::wstring& s, IDWriteTextFormat* f);
    void icon(wchar_t g, float cx, float cy, float size, uint32_t color, float alpha = 1.0f);
    void fillRect(D2D1_RECT_F r, uint32_t color, float alpha = 1.0f, float radius = 0);
    void strokeRect(D2D1_RECT_F r, uint32_t color, float alpha = 1.0f, float radius = 0, float w = 1.0f);
    void line(float x0, float y0, float x1, float y1, uint32_t color, float alpha = 1.0f, float w = 1.0f);
    void dot(float cx, float cy, float r, uint32_t color, float alpha = 1.0f);
    void softShadow(D2D1_RECT_F r, float radius, float strength, float spread = 10, float dy = 4);
    void activitySpinner(float cx, float cy, float r, uint32_t color);
    void drawBlurred(ID2D1Bitmap* bmp, D2D1_RECT_F dst, float blur, float opacity, D2D1_RECT_F* clip, float clipRadius);
    void addHit(int id, D2D1_RECT_F r);
    // volume popover: themes draw it and register the slider track (x range used for dragging)
    void volumeSlider(D2D1_RECT_F track);
    void setVolumeFromX(float x);
    void changeVolume(int delta);
    void closeVolume() { volumeOpen_ = volumeDrag_ = false; }
    int hitTest(float x, float y);
    void layoutEdit(bool visible, D2D1_RECT_F r);
    // poster toolkit (also usable by other themes)
    void drawGrain(float W, float H, float opacity);
    void drawHalftoneOrb(float cx, float cy, float r, double t, float energy, uint32_t ink, uint32_t rim);
    void drawCracks(float cx, float cy, float r, float W, float H, uint32_t color, float alpha);
    void giantType(const std::wstring& s, float x, float y, float size, uint32_t color, float alpha = 1.0f);
    float pixelText(const std::wstring& s, float x, float y, float px, uint32_t color, float alpha = 1.0f);
    float pixelTextWidth(const std::wstring& s, float px) const;
    D2D1_RECT_F pixelBlock(const std::wstring& s, float x, float y, float maxW, float px, uint32_t bg, uint32_t fg);
    void labelColumn(const std::vector<std::wstring>& lines, float right, float top, uint32_t color);
    // shared text helpers
    std::wstring clientName() const;
    std::wstring primaryIp() const;
    std::wstring videoInfo() const;
    // one line about cable connections for the home screen; action = button label (may be empty)
    std::wstring wiredHint(std::wstring& action, bool& attention) const;
    std::wstring androidSettingsText() const;

    // ---- state
    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    WNDPROC editOrigProc_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    UINT dpi_ = 96;
    bool reduceMotion_ = false;
    bool systemDark_ = false;
    Theme* theme_ = nullptr;

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
    std::wstring bahnschrift_ = L"Segoe UI", segoeText_ = L"Segoe UI", segoeDisplay_ = L"Segoe UI",
                 iconFont_ = L"Segoe MDL2 Assets";

    std::vector<std::pair<int, D2D1_RECT_F>> hits_;
    D2D1_RECT_F hitClip_{};
    bool hitClipOn_ = false;
    int hover_ = H_NONE, pressed_ = H_NONE;
    bool settingsOpen_ = false;
    float settingsScroll_ = 0, settingsContentH_ = 0;
    float themeScroll_ = 0, themeStripW_ = 0, themeStripMax_ = 0;
    D2D1_RECT_F themeStripRect_{};
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
    int audioRate_ = 44100;
    mn::AndroidMirror android_{rx_};
    mn::AndroidStatus usbStatus_;
    bool iphoneUsb_ = false;        // an iPhone shares its connection over USB (Personal Hotspot)
    uint64_t ifaceCheck_ = 0;
    std::atomic<bool> startedAdb_{false};
    std::atomic<bool> audioEnabled_{true};
    std::atomic<bool> framePosted_{false};
    bool connecting_ = false, sessionActive_ = false, mirrorActive_ = false, audioActive_ = false, paused_ = false;
    bool haveFrame_ = false;
    mn::ClientInfo client_;
    float airplayDb_ = 0.0f;
    bool muted_ = false;
    bool volumeOpen_ = false, volumeDrag_ = false;
    uint64_t volumeTouched_ = 0;
    float volTrackL_ = 0, volTrackR_ = 1;
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
