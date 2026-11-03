#include "settings.h"

#include <algorithm>

#include "platform.h"
#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <vector>

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring from_utf8(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring Settings::dataDir() {
    wchar_t path[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path) != S_OK) return L".";
    std::wstring dir = std::wstring(path) + L"\\Mirrorly";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::wstring iniPath() { return Settings::dataDir() + L"\\settings.ini"; }

static std::wstring readStr(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[512];
    GetPrivateProfileStringW(L"Mirrorly", key, def, buf, 512, iniPath().c_str());
    return buf;
}

static int readInt(const wchar_t* key, int def) { return (int)GetPrivateProfileIntW(L"Mirrorly", key, def, iniPath().c_str()); }

static void writeStr(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(L"Mirrorly", key, v.c_str(), iniPath().c_str());
}

void Settings::resolutionSize(int& w, int& h) const {
    static const int W[] = {1280, 1920, 2560, 3840};
    static const int H[] = {720, 1080, 1440, 2160};
    int r = resolution < 0 || resolution > 3 ? 1 : resolution;
    w = W[r];
    h = H[r];
}

void Settings::load() {
    wchar_t comp[256];
    DWORD cl = 256;
    if (!GetComputerNameW(comp, &cl)) wcscpy(comp, L"Windows PC");
    name = readStr(L"Name", comp);
    if (name.empty()) name = comp;
    resolution = readInt(L"Resolution", 1);
    fps = readInt(L"Fps", 60) == 30 ? 30 : 60;
    audio = readInt(L"Audio", 1) != 0;
    autoFullscreen = readInt(L"AutoFullscreen", 0) != 0;
    theme = readStr(L"Theme", L"default");
    usbAndroid = readInt(L"AndroidUSB", 1) != 0;
    wifiAndroid = readInt(L"AndroidWiFi", 1) != 0;
    controlAndroid = readInt(L"AndroidControl", 1) != 0;
    gamepadAndroid = readInt(L"AndroidGamepad", 1) != 0;
    skippedUpdate = to_utf8(readStr(L"SkippedUpdate", L""));
    volume = std::max(0, std::min(100, readInt(L"Volume", 100)));

    bool dirty = false;
    auto seedHex = mn::hex_decode(to_utf8(readStr(L"Key", L"")));
    if (seedHex.size() == 32) {
        memcpy(seed, seedHex.data(), 32);
    } else {
        mn::random_bytes(seed, 32);
        dirty = true;
    }
    auto dev = mn::hex_decode(to_utf8(readStr(L"DeviceId", L"")));
    if (dev.size() == 6) {
        memcpy(deviceId, dev.data(), 6);
    } else {
        mn::random_bytes(deviceId, 6);
        deviceId[0] = (uint8_t)((deviceId[0] & 0xFC) | 0x02);  // locally administered, unicast
        dirty = true;
    }
    pi = to_utf8(readStr(L"Pi", L""));
    if (pi.size() != 36) {
        uint8_t u[16];
        mn::random_bytes(u, 16);
        u[6] = (uint8_t)((u[6] & 0x0F) | 0x40);
        u[8] = (uint8_t)((u[8] & 0x3F) | 0x80);
        char buf[40];
        snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", u[0], u[1], u[2], u[3],
                 u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        pi = buf;
        dirty = true;
    }
    if (dirty) save();
}

void Settings::save() const {
    writeStr(L"Name", name);
    writeStr(L"Resolution", std::to_wstring(resolution));
    writeStr(L"Fps", std::to_wstring(fps));
    writeStr(L"Audio", audio ? L"1" : L"0");
    writeStr(L"AutoFullscreen", autoFullscreen ? L"1" : L"0");
    writeStr(L"Theme", theme);
    writeStr(L"AndroidUSB", usbAndroid ? L"1" : L"0");
    writeStr(L"AndroidWiFi", wifiAndroid ? L"1" : L"0");
    writeStr(L"AndroidControl", controlAndroid ? L"1" : L"0");
    writeStr(L"AndroidGamepad", gamepadAndroid ? L"1" : L"0");
    writeStr(L"SkippedUpdate", from_utf8(skippedUpdate));
    writeStr(L"Volume", std::to_wstring(volume));
    writeStr(L"Key", from_utf8(mn::hex_encode(seed, 32)));
    writeStr(L"DeviceId", from_utf8(mn::hex_encode(deviceId, 6)));
    writeStr(L"Pi", from_utf8(pi));
}
