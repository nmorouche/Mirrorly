// Mirrorly - AirPlay receiver for Windows (screen mirroring + audio)
// Window, input and engine wiring. Drawing lives in render.cpp.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <memory>
#include <thread>

#include "log.h"
#include "platform.h"

App* App::instance = nullptr;

// ================================================================== setup

UINT app_get_dpi(HWND h) {
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
        if (SUCCEEDED(fc->FindFamilyName(L"Bahnschrift", &idx, &exists)) && exists) uiFont_ = L"Bahnschrift";
        exists = FALSE;
        if (SUCCEEDED(fc->FindFamilyName(L"Segoe Fluent Icons", &idx, &exists)) && exists) iconFont_ = L"Segoe Fluent Icons";
        fc->Release();
    }

    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    reduceMotion_ = !anim;

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
    wc.hbrBackground = CreateSolidBrush(RGB(0xC4, 0xC7, 0xBD));
    RegisterClassExW(&wc);

    UINT sysDpi = app_get_dpi(nullptr);
    int w = MulDiv(1000, sysDpi, 96), h = MulDiv(660, sysDpi, 96);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                            CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;

    // light title bar that blends into the paper background (Windows 11 caption colours)
    BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    COLORREF cap = RGB(0xC4, 0xC7, 0xBD), capText = RGB(0, 0, 0);
    DwmSetWindowAttribute(hwnd_, 35, &cap, sizeof(cap));
    DwmSetWindowAttribute(hwnd_, 36, &capText, sizeof(capText));

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
        App::instance = (App*)cs->lpCreateParams;
        App::instance->hwnd_ = h;
    }
    if (App::instance && App::instance->hwnd_ == h) return App::instance->handle(m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK App::EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE)) {
        if (w == VK_ESCAPE) SetWindowTextW(h, App::instance->settings_.name.c_str());
        SetFocus(App::instance->hwnd_);
        return 0;
    }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    return CallWindowProcW(App::instance->editOrigProc_, h, m, w, l);
}

LRESULT App::handle(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        dpi_ = app_get_dpi(hwnd_);
        edit_ = CreateWindowExW(0, L"EDIT", settings_.name.c_str(), WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_,
                                (HMENU)100, inst_, nullptr);
        SendMessageW(edit_, EM_LIMITTEXT, 48, 0);
        editOrigProc_ = (WNDPROC)SetWindowLongPtrW(edit_, GWLP_WNDPROC, (LONG_PTR)EditProc);
        editBrush_ = CreateSolidBrush(RGB(0xC4, 0xC7, 0xBD));
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
        SetTextColor(dc, RGB(0, 0, 0));
        SetBkColor(dc, RGB(0xC4, 0xC7, 0xBD));
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
