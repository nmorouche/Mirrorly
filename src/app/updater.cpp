// Mirrorly - automatic updates from the project's GitHub Releases
//
// 1. Ask GitHub for the latest release (tag + Mirrorly.exe + Mirrorly.exe.sha256).
// 2. If it is newer, the app offers it. Updating downloads the exe next to the running one,
//    checks its SHA-256, renames the running exe to Mirrorly.exe.old (Windows allows renaming
//    a running program), puts the new one in its place, and restarts into it.
// 3. The new instance deletes Mirrorly.exe.old.
#include "app.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#include "log.h"

#ifndef MIRRORLY_REPO
#define MIRRORLY_REPO "nmorouche/Airplay-server"
#endif

namespace {

std::wstring widen(const std::string& s) { return from_utf8(s); }

// GET url. Writes to file if given, otherwise into body. progress(done, total) may cancel.
bool httpGet(const std::string& url, std::string* body, HANDLE file, const std::function<void(uint64_t, uint64_t)>& progress,
             std::string& err) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256] = {0}, path[2048] = {0};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    wchar_t extra[2048] = {0};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        err = "bad address";
        return false;
    }
    std::wstring ua = std::wstring(L"Mirrorly/") + kVersion;
    HINTERNET ses = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) {
        err = "no network";
        return false;
    }
    WinHttpSetTimeouts(ses, 10000, 10000, 15000, 30000);
    bool ok = false;
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    HINTERNET req = nullptr;
    if (con) {
        std::wstring p = std::wstring(path) + extra;
        req = WinHttpOpenRequest(con, L"GET", p.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    if (req) {
        const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
        if (WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                                WINHTTP_NO_HEADER_INDEX);
            wchar_t lenBuf[32] = {0};
            DWORD lsz = sizeof(lenBuf);
            uint64_t total = 0;
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lsz, WINHTTP_NO_HEADER_INDEX))
                total = _wcstoui64(lenBuf, nullptr, 10);
            if (status != 200) {
                err = "server answered " + std::to_string(status);
            } else {
                ok = true;
                uint64_t done = 0;
                std::vector<char> buf(64 * 1024);
                for (;;) {
                    DWORD got = 0;
                    if (!WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &got)) {
                        err = "download interrupted";
                        ok = false;
                        break;
                    }
                    if (got == 0) break;
                    if (file) {
                        DWORD wr = 0;
                        if (!WriteFile(file, buf.data(), got, &wr, nullptr) || wr != got) {
                            err = "could not write the download";
                            ok = false;
                            break;
                        }
                    } else {
                        body->append(buf.data(), got);
                        if (body->size() > 4 * 1024 * 1024) {
                            err = "answer too large";
                            ok = false;
                            break;
                        }
                    }
                    done += got;
                    if (progress) progress(done, total);
                }
                if (ok && total && done != total) {
                    err = "download incomplete";
                    ok = false;
                }
            }
        } else {
            err = "could not reach " + to_utf8(host);
        }
    } else if (err.empty()) {
        err = "could not reach " + to_utf8(host);
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

// value of "key": "..." after position from (no unescaping needed for the fields we read)
std::string jsonString(const std::string& j, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    size_t k = j.find("\"" + key + "\"", from);
    if (k == std::string::npos) return "";
    size_t c = j.find(':', k);
    size_t q = c == std::string::npos ? c : j.find('"', c);
    if (q == std::string::npos) return "";
    size_t e = j.find('"', q + 1);
    if (e == std::string::npos) return "";
    if (at) *at = e;
    return j.substr(q + 1, e - q - 1);
}

bool sha256File(const std::wstring& path, std::string& hex) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0;
    std::vector<uint8_t> buf(256 * 1024);
    DWORD got = 0;
    while (ok && ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr) && got > 0) ok = BCryptHashData(h, buf.data(), got, 0) == 0;
    uint8_t dig[32];
    if (ok) ok = BCryptFinishHash(h, dig, 32, 0) == 0;
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    if (ok) hex = mn::hex_encode(dig, 32);
    return ok;
}

std::wstring exePath() {
    wchar_t p[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
    return std::wstring(p, n);
}

// shared with the worker threads (never touch the App from them: they may outlive it)
std::atomic<bool> g_busy{false};
std::mutex g_m;
std::string g_exeUrl, g_shaUrl;

void postUpdate(HWND hwnd, int state, const std::string& version, const std::string& error, float progress) {
    Event* e = new Event();
    e->type = EV_UPDATE;
    e->i = state;
    e->s1 = version;
    e->s2 = error;
    e->f = progress;
    if (!PostMessageW(hwnd, WM_APP_EVENT, 0, (LPARAM)e)) delete e;
}

}  // namespace

// "v1.2.3" / "1.2.3" -> comparable numbers; returns true if a is newer than b
bool App::versionNewer(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& s, int v[4]) {
        size_t i = (s.size() && (s[0] == 'v' || s[0] == 'V')) ? 1 : 0;
        for (int k = 0; k < 4; k++) {
            v[k] = 0;
            while (i < s.size() && isdigit((unsigned char)s[i])) v[k] = v[k] * 10 + (s[i++] - '0');
            if (i < s.size() && s[i] == '.') i++;
            else {
                for (int r = k + 1; r < 4; r++) v[r] = 0;
                break;
            }
        }
    };
    int x[4], y[4];
    parse(a, x);
    parse(b, y);
    for (int k = 0; k < 4; k++)
        if (x[k] != y[k]) return x[k] > y[k];
    return false;
}

void App::checkForUpdate(bool manual) {
    if (g_busy.exchange(true)) return;
    if (manual) {
        update_.state = UPD_CHECKING;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    HWND hwnd = hwnd_;
    std::string url = "https://api.github.com/repos/" MIRRORLY_REPO "/releases/latest";
    if (const char* o = getenv("MIRRORLY_UPDATE_URL")) url = o;  // for testing
    std::thread([hwnd, url, manual]() {
        std::string body, err;
        bool ok = httpGet(url, &body, nullptr, nullptr, err);
        std::string tag = ok ? jsonString(body, "tag_name") : "";
        std::string exeUrl, shaUrl;
        for (size_t at = 0;;) {
            std::string u = jsonString(body, "browser_download_url", at, &at);
            if (u.empty()) break;
            if (u.size() >= 13 && u.compare(u.size() - 13, 13, "/Mirrorly.exe") == 0) exeUrl = u;
            if (u.size() >= 20 && u.compare(u.size() - 20, 20, "/Mirrorly.exe.sha256") == 0) shaUrl = u;
        }
        g_busy = false;
        if (!ok || tag.empty() || exeUrl.empty()) {
            mn::logf(mn::LogLevel::Info, "update: check failed (%s)", ok ? "no Mirrorly.exe in the latest release" : err.c_str());
            postUpdate(hwnd, manual ? UPD_CHECK_FAILED : UPD_NONE, "", ok ? "No download in the latest release" : err, 0);
            return;
        }
        std::string ver = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
        bool newer = versionNewer(ver, MIRRORLY_VERSION);
        mn::logf(mn::LogLevel::Info, "update: latest release %s, this is %s%s", ver.c_str(), MIRRORLY_VERSION,
                 newer ? " (update available)" : "");
        {
            std::lock_guard<std::mutex> lk(g_m);
            g_exeUrl = exeUrl;
            g_shaUrl = shaUrl;
        }
        postUpdate(hwnd, newer ? UPD_AVAILABLE : UPD_UP_TO_DATE, ver, "", 0);
    }).detach();
}

void App::startUpdate() {
    if (update_.state == UPD_DOWNLOADING || update_.state == UPD_INSTALLING || g_busy.exchange(true)) return;
    std::string exeUrl, shaUrl;
    {
        std::lock_guard<std::mutex> lk(g_m);
        exeUrl = g_exeUrl;
        shaUrl = g_shaUrl;
    }
    std::string version = update_.version;
    update_.state = UPD_DOWNLOADING;
    update_.progress = 0;
    update_.error.clear();
    HWND hwnd = hwnd_;
    std::wstring target = exePath() + L".download";
    mn::logf(mn::LogLevel::Info, "update: downloading %s", exeUrl.c_str());
    std::thread([hwnd, exeUrl, shaUrl, version, target]() {
        std::string err;
        HANDLE f = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool ok = f != INVALID_HANDLE_VALUE;
        if (!ok) err = "Mirrorly can't write to its folder. Move Mirrorly.exe to a folder you own (Documents, for example).";
        uint64_t lastPost = 0;
        if (ok) {
            ok = httpGet(exeUrl, nullptr, f, [&](uint64_t done, uint64_t total) {
                uint64_t now = mn::now_ms();
                if (total && now - lastPost > 100) {
                    lastPost = now;
                    postUpdate(hwnd, UPD_DOWNLOADING, version, "", (float)done / (float)total);
                }
            }, err);
            CloseHandle(f);
        }
        // integrity: the release publishes the exe's SHA-256 next to it
        if (ok && !shaUrl.empty()) {
            std::string sha, have, e2;
            if (!httpGet(shaUrl, &sha, nullptr, nullptr, e2) || sha.size() < 64) {
                ok = false;
                err = "could not download the checksum";
            } else if (!sha256File(target, have) || _strnicmp(have.c_str(), sha.c_str(), 64) != 0) {
                ok = false;
                err = "the download is damaged (checksum mismatch)";
            }
        }
        g_busy = false;
        if (!ok) {
            DeleteFileW(target.c_str());
            mn::logf(mn::LogLevel::Warn, "update: failed: %s", err.c_str());
            postUpdate(hwnd, UPD_FAILED, version, err, 0);
            return;
        }
        mn::logf(mn::LogLevel::Info, "update: %s downloaded and verified", version.c_str());
        postUpdate(hwnd, UPD_READY, version, "", 1);
    }).detach();
}

// Swaps the exe files and arranges the restart (runs on the UI thread).
void App::installUpdate() {
    std::wstring exe = exePath(), old = exe + L".old", dl = exe + L".download";
    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        update_.state = UPD_FAILED;
        update_.error = "could not replace Mirrorly.exe (error " + std::to_string(GetLastError()) + ")";
        return;
    }
    if (!MoveFileExW(dl.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DWORD e = GetLastError();
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);  // put the old one back
        update_.state = UPD_FAILED;
        update_.error = "could not install the new version (error " + std::to_string(e) + ")";
        return;
    }
    mn::logf(mn::LogLevel::Info, "update: installed %s, restarting", update_.version.c_str());
    relaunch_ = exe;
    update_.state = UPD_INSTALLING;
    DestroyWindow(hwnd_);  // run() returns, the engines stop, then wWinMain starts the new exe
}

// Called at startup: remove what the previous version left behind.
void App::cleanupAfterUpdate(bool justUpdated) {
    std::wstring old = exePath() + L".old", dl = exePath() + L".download";
    DeleteFileW(dl.c_str());
    if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::thread([old]() {
            for (int i = 0; i < 50 && !DeleteFileW(old.c_str()); i++) Sleep(200);  // the old process may still be exiting
        }).detach();
    }
    if (justUpdated) showToast(std::wstring(L"Mirrorly is up to date: version ") + kVersion);
}
