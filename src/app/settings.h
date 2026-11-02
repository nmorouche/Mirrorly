// Mirrorly - persisted user settings (%APPDATA%\Mirrorly\settings.ini)
#pragma once
#include <cstdint>
#include <string>

struct Settings {
    std::wstring name;
    int resolution = 1;        // 0=720p 1=1080p 2=1440p 3=4K
    int fps = 60;              // 30 or 60
    bool audio = true;
    bool autoFullscreen = false;
    std::wstring theme = L"default";  // id of the selected theme
    bool usbAndroid = true;           // mirror Android phones plugged in over USB
    uint8_t seed[32] = {0};
    uint8_t deviceId[6] = {0};
    std::string pi;

    void load();
    void save() const;
    static std::wstring dataDir();
    void resolutionSize(int& w, int& h) const;
};

std::string to_utf8(const std::wstring& w);
std::wstring from_utf8(const std::string& s);
