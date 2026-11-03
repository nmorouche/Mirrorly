// Mirrorly - rendering: device resources, the drawing toolkit shared by all themes,
// the Settings layout (theme-styled), the theme picker and the mirroring base layer.
// Each theme's look lives in theme_*.cpp.
#include "app.h"

extern "C" {
#include "qrcodegen.h"
}

#include <d2d1effects.h>

#define _USE_MATH_DEFINES
#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "log.h"
#include "platform.h"


// ================================================================== pixel font (5x7)

namespace {

typedef std::array<uint8_t, 7> Glyph;

const std::map<wchar_t, Glyph>& pixelFont() {
    static const std::map<wchar_t, Glyph> f = {
        {L'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {L'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {L'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {L'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        {L'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {L'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {L'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {L'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {L'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {L'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
        {L'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {L'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        {L'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {L'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
        {L'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {L'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {L'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {L'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {L'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {L'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {L'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {L'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {L'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {L'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
        {L'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, {L'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
        {L'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {L'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {L'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {L'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
        {L'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {L'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
        {L'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {L'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {L'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {L'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
        {L'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {L',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
        {L'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, {L':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
        {L'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}}, {L'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
        {L'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}}, {L'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
        {L'&', {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}}, {L'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
        {L')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}}, {L'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
        {L'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}}, {L'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
        {L'@', {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}}, {L' ', {0, 0, 0, 0, 0, 0, 0}},
    };
    return f;
}

wchar_t pixelChar(wchar_t c) {
    if (c >= L'a' && c <= L'z') return (wchar_t)(c - L'a' + L'A');
    static const wchar_t* from = L"àáâäãÀÁÂÄçÇèéêëÈÉÊË"
                                 L"ìíîïÎÏòóôöõÔÖùúûüÙÛÜ"
                                 L"ñÑ’‘";
    static const wchar_t* to = L"AAAAAAAAACCEEEEEEEEIIIIIIOOOOOOOUUUUUUUNN''";
    for (int i = 0; from[i]; i++)
        if (from[i] == c) return to[i];
    if (pixelFont().count(c)) return c;
    return L'?';
}

}  // namespace

float App::pixelTextWidth(const std::wstring& s, float px) const {
    if (s.empty()) return 0;
    return (float)(s.size() * 6 - 1) * px;
}

float App::pixelText(const std::wstring& s, float x, float y, float px, uint32_t color, float alpha) {
    brush_->SetColor(rgb(color, alpha));
    const auto& font = pixelFont();
    float cx = x;
    for (wchar_t raw : s) {
        const Glyph& g = font.at(pixelChar(raw));
        for (int row = 0; row < 7; row++) {
            int bits = g[row];
            int col = 0;
            while (col < 5) {  // merge horizontal runs into one rectangle
                if (!(bits & (0x10 >> col))) { col++; continue; }
                int start = col;
                while (col < 5 && (bits & (0x10 >> col))) col++;
                rt_->FillRectangle(D2D1::RectF(cx + start * px, y + row * px, cx + col * px, y + (row + 1) * px), brush_);
            }
        }
        cx += 6 * px;
    }
    return cx - x - px;
}

// Draws pixel lettering on a solid block. Shrinks, then wraps by words, to fit maxW.
D2D1_RECT_F App::pixelBlock(const std::wstring& s, float x, float y, float maxW, float px, uint32_t bg, uint32_t fg) {
    std::wstring t = s.empty() ? L" " : s;
    float pad = px * 2.5f;
    while (px > 2.0f && pixelTextWidth(t, px) + pad * 2 > maxW) {
        px -= 0.5f;
        pad = px * 2.5f;
    }
    std::vector<std::wstring> lines;
    if (pixelTextWidth(t, px) + pad * 2 <= maxW) {
        lines.push_back(t);
    } else {
        std::wstring cur, word;
        auto flush = [&](const std::wstring& w) {
            std::wstring cand = cur.empty() ? w : cur + L" " + w;
            if (!cur.empty() && pixelTextWidth(cand, px) + pad * 2 > maxW) {
                lines.push_back(cur);
                cur = w;
            } else {
                cur = cand;
            }
        };
        for (wchar_t c : t) {
            if (c == L' ') { if (!word.empty()) flush(word); word.clear(); }
            else word += c;
        }
        if (!word.empty()) flush(word);
        if (!cur.empty()) lines.push_back(cur);
        if (lines.size() > 3) {
            lines.resize(3);
            lines[2] += L"...";
        }
        for (auto& l : lines) {  // a single very long word: cut it
            size_t maxChars = (size_t)std::max(1.0f, (maxW - pad * 2 + px) / (6 * px));
            if (l.size() > maxChars) l = l.substr(0, maxChars);
        }
    }
    float w = 0;
    for (auto& l : lines) w = std::max(w, pixelTextWidth(l, px));
    float lineH = 7 * px, gap = 3 * px;
    float h = lines.size() * lineH + (lines.size() - 1) * gap;
    D2D1_RECT_F r = D2D1::RectF(x, y, x + w + pad * 2, y + h + pad * 2);
    fillRect(r, bg);
    float ly = y + pad;
    for (auto& l : lines) {
        pixelText(l, x + pad, ly, px, fg);
        ly += lineH + gap;
    }
    return r;
}

// ================================================================== device resources

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
    rt_->CreateSolidColorBrush(rgb(0x000000), &brush_);

    // film grain: a small tile of black and white specks, repeated over the canvas
    const int N = 192;
    std::vector<uint32_t> px(N * N);
    uint32_t seed = 0x9E3779B9u;
    for (auto& p : px) {
        seed = seed * 1664525u + 1013904223u;
        uint32_t r = seed >> 24;
        uint32_t a = (seed >> 8) & 0x3F;  // 0..63
        if (r < 120) p = (a << 24);                                    // dark speck
        else if (r < 200) { uint32_t v = a * 255 / 255; p = (a << 24) | (v << 16) | (v << 8) | v; }  // light speck
        else p = 0;
    }
    ID2D1Bitmap* tile = nullptr;
    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (SUCCEEDED(rt_->CreateBitmap(D2D1::SizeU(N, N), px.data(), N * 4, bp, &tile))) {
        rt_->CreateBitmapBrush(tile, D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP), &grain_);
        tile->Release();
    }
    return true;
}

void App::discardDeviceResources() {
    SafeRelease(video_);
    SafeRelease(artwork_);
    SafeRelease(grain_);
    SafeRelease(brush_);
    SafeRelease(dc_);
    SafeRelease(rt_);
}

double App::animTime() const {
    if (reduceMotion_) return 0.0;
    return (mn::now_ms() - startTime_) / 1000.0;
}

// ================================================================== primitives

IDWriteTextFormat* App::fmt(float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT a, FontRole role) {
    const wchar_t* family = iconFont_.c_str();
    if (role == F_UI) family = theme_ ? theme_->uiFont(*this, false) : segoeText_.c_str();
    else if (role == F_UI_DISPLAY) family = theme_ ? theme_->uiFont(*this, true) : segoeDisplay_.c_str();
    else if (role == F_CONDENSED) family = bahnschrift_.c_str();
    wchar_t key[160];
    swprintf(key, 160, L"%d|%d|%d|%d|%ls", (int)(size * 10), (int)w, (int)a, (int)role, family);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    DWRITE_FONT_STRETCH stretch = role == F_CONDENSED ? DWRITE_FONT_STRETCH_CONDENSED : DWRITE_FONT_STRETCH_NORMAL;
    dw_->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL, stretch, size, L"en-us", &f);
    if (f) {
        f->SetTextAlignment(a);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        if (role != F_ICON) {
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
    if (FAILED(dw_->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 8000, 1000, &l)) || !l) return 0;
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    l->Release();
    return m.widthIncludingTrailingWhitespace;
}

void App::icon(wchar_t g, float cx, float cy, float size, uint32_t color, float alpha) {
    wchar_t s[2] = {g, 0};
    IDWriteTextFormat* f = fmt(size, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, F_ICON);
    brush_->SetColor(rgb(color, alpha));
    rt_->DrawText(s, 1, f, D2D1::RectF(cx - size, cy - size, cx + size, cy + size), brush_);
}

void App::fillRect(D2D1_RECT_F r, uint32_t color, float alpha, float radius) {
    brush_->SetColor(rgb(color, alpha));
    if (radius > 0) rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_);
    else rt_->FillRectangle(r, brush_);
}

void App::strokeRect(D2D1_RECT_F r, uint32_t color, float alpha, float radius, float w) {
    brush_->SetColor(rgb(color, alpha));
    float h = w / 2;
    D2D1_RECT_F in = D2D1::RectF(r.left + h, r.top + h, r.right - h, r.bottom - h);
    if (radius > 0) rt_->DrawRoundedRectangle(D2D1::RoundedRect(in, radius, radius), brush_, w);
    else rt_->DrawRectangle(in, brush_, w);
}

void App::line(float x0, float y0, float x1, float y1, uint32_t color, float alpha, float w) {
    brush_->SetColor(rgb(color, alpha));
    rt_->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush_, w);
}

void App::dot(float cx, float cy, float r, uint32_t color, float alpha) {
    brush_->SetColor(rgb(color, alpha));
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush_);
}

void App::labelColumn(const std::vector<std::wstring>& lines, float right, float top, uint32_t color) {
    IDWriteTextFormat* f = fmt(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING, F_CONDENSED);
    float y = top;
    for (auto& l : lines) {
        if (l.empty()) { y += 10; continue; }
        text(l, D2D1::RectF(right - 320, y, right, y + 15), f, color);
        y += 15;
    }
}

void App::addHit(int id, D2D1_RECT_F r) {
    if (hitClipOn_) {
        r.left = std::max(r.left, hitClip_.left);
        r.top = std::max(r.top, hitClip_.top);
        r.right = std::min(r.right, hitClip_.right);
        r.bottom = std::min(r.bottom, hitClip_.bottom);
        if (r.right <= r.left || r.bottom <= r.top) return;
    }
    hits_.push_back({id, r});
}

void App::volumeSlider(D2D1_RECT_F track) {
    volTrackL_ = track.left;
    volTrackR_ = std::max(track.left + 1, track.right);
    float cy = (track.top + track.bottom) / 2;
    addHit(H_VOL_SLIDER, D2D1::RectF(track.left - 8, cy - 14, track.right + 8, cy + 14));
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
        editFont_ = CreateFontW(-MulDiv(15, dpi_, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                theme_ ? theme_->uiFont(*this, false) : L"Segoe UI");
        SendMessageW(edit_, WM_SETFONT, (WPARAM)editFont_, TRUE);
    }
    if (memcmp(&r, &editRect_, sizeof(r)) != 0 || !editVisible_) {
        editRect_ = r;
        float s = dpi_ / 96.0f;
        int fh = MulDiv(22, dpi_, 96);
        int x = (int)(r.left * s), w = (int)((r.right - r.left) * s);
        int y = (int)(((r.top + r.bottom) / 2) * s) - fh / 2;
        MoveWindow(edit_, x, y, w, fh, TRUE);
        ShowWindow(edit_, SW_SHOW);
        editVisible_ = true;
    }
}

// ================================================================== poster elements

void App::drawGrain(float W, float H, float opacity) {
    if (!grain_) return;
    grain_->SetOpacity(opacity);
    rt_->FillRectangle(D2D1::RectF(0, 0, W, H), grain_);
}

void App::giantType(const std::wstring& s, float x, float y, float size, uint32_t color, float alpha) {
    if (s.empty()) return;
    IDWriteTextFormat* f = fmt(std::floor(size), DWRITE_FONT_WEIGHT_BLACK, DWRITE_TEXT_ALIGNMENT_LEADING, F_CONDENSED);
    std::wstring up = s;
    for (auto& c : up) c = (wchar_t)towupper(c);
    brush_->SetColor(rgb(color, alpha));
    rt_->DrawText(up.c_str(), (UINT32)up.size(), f, D2D1::RectF(x, y, x + 6000, y + size * 1.25f), brush_);
}

// A sphere-ish object rendered as a halftone dot screen: dots grow where the surface is in shadow.
// "energy" (0..1) agitates the outline; the rim catches an acid-lime light.
void App::drawHalftoneOrb(float cx, float cy, float r, double t, float energy, uint32_t ink, uint32_t rim) {
    float s = std::max(5.0f, r / 21.0f);
    double lx = cos(t * 0.35) * 0.55 + 0.25, ly = -0.55 + sin(t * 0.27) * 0.15, lz = 0.62;
    // light ink on a dark ground: dots grow with light instead of shadow
    bool lightInk = (((ink >> 16) & 255) + ((ink >> 8) & 255) + (ink & 255)) > 384;
    double ll = sqrt(lx * lx + ly * ly + lz * lz);
    lx /= ll; ly /= ll; lz /= ll;
    int row = 0;
    for (float gy = -r * 1.15f; gy <= r * 1.15f; gy += s * 0.866f, row++) {
        float off = (row & 1) ? s * 0.5f : 0.0f;
        for (float gx = -r * 1.15f + off; gx <= r * 1.15f; gx += s) {
            float d = sqrtf(gx * gx + gy * gy);
            double th = atan2(gy, gx);
            double rr = r * (1.0 + 0.045 * sin(3 * th + t * 0.6) + 0.03 * sin(5 * th - t * 0.9) +
                             energy * (0.05 * sin(7 * th + t * 4.0) + 0.03 * sin(11 * th - t * 5.0)));
            if (d > rr) continue;
            double q = d / rr;
            double nz = sqrt(std::max(0.0, 1.0 - q * q));
            double nx = gx / rr, ny = gy / rr;
            double lit = std::max(0.0, nx * lx + ny * ly + nz * lz);
            double shade = lightInk ? lit : 1.0 - lit;
            float dr = (float)(s * 0.56 * pow(shade, 0.85) + s * 0.07);
            // soften the silhouette so the dot lattice doesn't read as facets
            float edge = (float)std::min(1.0, (1.0 - q) * 9.0 + 0.35);
            // a thin rim of acid light along the lower left edge
            double rimAmt = (q > 0.86 ? (q - 0.86) / 0.14 : 0.0) * std::max(0.0, -nx * 0.7 + ny * 0.7);
            if (rimAmt > 0.35) {
                dot(cx + gx, cy + gy, (float)(s * 0.42), rim);
            } else {
                dot(cx + gx, cy + gy, dr * edge, ink);
            }
        }
    }
}

// Thin jagged lime cracks running away from the object (fixed pattern, seeded).
void App::drawCracks(float cx, float cy, float r, float W, float H, uint32_t color, float alpha) {
    uint32_t seed = 0x1234567u;
    auto rnd = [&]() {
        seed = seed * 1103515245u + 12345u;
        return ((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    const float starts[3] = {-2.5f, 0.35f, 2.2f};
    brush_->SetColor(rgb(color, alpha));
    for (int c = 0; c < 3; c++) {
        float a = starts[c];
        float x = cx + cosf(a) * r * 0.96f, y = cy + sinf(a) * r * 0.96f;
        float len = std::min(W, H) * (0.22f + 0.1f * c);
        float walked = 0;
        while (walked < len) {
            float seg = 10 + rnd() * 26;
            a += (rnd() - 0.5f) * 1.1f;
            float nx = x + cosf(a) * seg, ny = y + sinf(a) * seg;
            rt_->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(nx, ny), brush_, 1.4f);
            if (rnd() > 0.78f) {  // small branch
                float ba = a + (rnd() > 0.5f ? 0.9f : -0.9f);
                rt_->DrawLine(D2D1::Point2F(nx, ny), D2D1::Point2F(nx + cosf(ba) * seg * 0.6f, ny + sinf(ba) * seg * 0.6f), brush_, 1.0f);
            }
            x = nx; y = ny;
            walked += seg;
        }
    }
}

// ================================================================== more toolkit

void App::softShadow(D2D1_RECT_F r, float radius, float strength, float spread, float dy) {
    const int steps = 7;
    for (int k = steps; k >= 1; k--) {
        float f = (float)k / steps;
        float e = spread * f;
        D2D1_RECT_F s = D2D1::RectF(r.left - e, r.top - e + dy * f, r.right + e, r.bottom + e + dy * f);
        brush_->SetColor(D2D1::ColorF(0, 0, 0, strength / steps * (1.15f - f)));
        rt_->FillRoundedRectangle(D2D1::RoundedRect(s, radius + e, radius + e), brush_);
    }
}

// Activity indicator: twelve rounded spokes with a rotating fade.
void App::activitySpinner(float cx, float cy, float r, uint32_t color) {
    int lead = (int)((mn::now_ms() - startTime_) / 83) % 12;
    if (reduceMotion_) lead = 0;
    float w = r * 0.18f;
    for (int i = 0; i < 12; i++) {
        float a = (float)(i * 2 * M_PI / 12);
        int age = (lead - i + 12) % 12;
        float alpha = std::max(0.2f, 1.0f - age / 9.0f);
        float sx = sinf(a), sy = -cosf(a);
        brush_->SetColor(rgb(color, alpha));
        D2D1_POINT_2F p0 = D2D1::Point2F(cx + sx * r * 0.5f, cy + sy * r * 0.5f);
        D2D1_POINT_2F p1 = D2D1::Point2F(cx + sx * r, cy + sy * r);
        ID2D1StrokeStyle* round = nullptr;
        D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
        d2d_->CreateStrokeStyle(sp, nullptr, 0, &round);
        rt_->DrawLine(p0, p1, brush_, w, round);
        SafeRelease(round);
    }
}

// Draws a bitmap with a Gaussian blur into dst (optionally masked to a rounded rect).
void App::drawBlurred(ID2D1Bitmap* bmp, D2D1_RECT_F dst, float blur, float opacity, D2D1_RECT_F* clip, float clipRadius) {
    if (!bmp) return;
    ID2D1RoundedRectangleGeometry* g = nullptr;
    ID2D1Layer* layer = nullptr;
    D2D1_RECT_F mask = clip ? *clip : dst;
    d2d_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(mask, clipRadius, clipRadius), &g);
    rt_->CreateLayer(&layer);
    if (g && layer)
        rt_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), layer);
    ID2D1Effect* fx = nullptr;
    D2D1_SIZE_F bs = bmp->GetSize();
    if (dc_ && SUCCEEDED(dc_->CreateEffect(CLSID_D2D1GaussianBlur, &fx)) && fx && bs.width > 0) {
        float sx = (dst.right - dst.left) / bs.width, sy = (dst.bottom - dst.top) / bs.height;
        float dev = blur / std::max(0.01f, sx);  // blur is given in DIPs on screen
        D2D1_BORDER_MODE bm = D2D1_BORDER_MODE_HARD;
        fx->SetInput(0, bmp, TRUE);
        fx->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, D2D1_PROPERTY_TYPE_UNKNOWN, (const BYTE*)&dev, sizeof(dev));
        fx->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_PROPERTY_TYPE_UNKNOWN, (const BYTE*)&bm, sizeof(bm));
        D2D1_MATRIX_3X2_F old;
        rt_->GetTransform(&old);
        rt_->SetTransform(D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(dst.left, dst.top) * old);
        dc_->DrawImage(fx, D2D1_INTERPOLATION_MODE_LINEAR);
        rt_->SetTransform(old);
        fx->Release();
    } else {
        rt_->DrawBitmap(bmp, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    if (g && layer) rt_->PopLayer();
    SafeRelease(layer);
    SafeRelease(g);
}

std::wstring App::clientName() const {
    std::wstring n = from_utf8(client_.name);
    return n.empty() ? L"your iPhone" : n;
}

std::wstring App::primaryIp() const {
    for (auto& ni : mn::list_ipv4_interfaces()) return from_utf8(mn::ip_to_string(ni.addr));
    return L"";
}

std::wstring App::wiredHint(std::wstring& action, bool& attention, int& actionHit) const {
    using mn::AndroidState;
    action.clear();
    attention = false;
    actionHit = H_USB_ACTION;
    std::wstring dev = from_utf8(usbStatus_.device);
    if (dev.empty()) dev = L"your phone";
    const wchar_t* over = usbStatus_.wireless ? L" over Wi-Fi" : L" over USB";
    switch (usbStatus_.state) {
    case AndroidState::Unauthorized:
        attention = true;
        return L"Tap “Allow” on your phone to mirror it over USB";
    case AndroidState::Offline:
        return L"Your phone is getting ready over USB…";
    case AndroidState::Connecting:
        return L"Connecting to " + dev + over + L"…";
    case AndroidState::Ready:
        action = L"Mirror";
        return dev + (usbStatus_.wireless ? L" is available over Wi-Fi" : L" is plugged in");
    case AndroidState::Failed:
        attention = true;
        action = L"Try again";
        return L"Couldn't mirror " + dev + over;
    case AndroidState::NoAdb:
        if (iphoneUsb_) break;
        attention = true;
        return L"Mirroring Android phones isn't available";
    default:
        break;
    }
    if (iphoneUsb_) return L"iPhone connected by cable: use Screen Mirroring";
    if (usbStatus_.state == AndroidState::Off) return L"Or plug in your iPhone with Personal Hotspot on";
    if (settings_.wifiAndroid) {
        action = L"Pair over Wi-Fi";
        actionHit = H_PAIR;
        return settings_.usbAndroid ? L"Android phone? Plug it in with USB debugging on, or" : L"Android phone?";
    }
    return L"Or plug in a phone: Android with USB debugging, iPhone with Personal Hotspot";
}

std::wstring App::androidSettingsText() const {
    using mn::AndroidState;
    std::wstring dev = from_utf8(usbStatus_.device);
    if (dev.empty()) dev = L"The phone";
    if (!settings_.usbAndroid) return L"Mirror an Android phone plugged in with a cable";
    if (usbStatus_.wireless && usbStatus_.state != AndroidState::NoAdb) return L"Turn on USB debugging on the phone, then plug it in";
    switch (usbStatus_.state) {
    case AndroidState::NoAdb: return L"Not available: " + from_utf8(usbStatus_.detail);
    case AndroidState::Unauthorized: return L"Tap “Allow” on the phone";
    case AndroidState::Ready:
    case AndroidState::Connecting:
    case AndroidState::Offline: return dev + L" is plugged in";
    case AndroidState::Mirroring: return dev + L" is mirroring";
    case AndroidState::Failed: return L"Couldn't start: " + from_utf8(usbStatus_.detail);
    default: return L"Turn on USB debugging on the phone, then plug it in";
    }
}

std::wstring App::pairingText(bool& busy, bool& error) const {
    using P = mn::PairingStatus;
    busy = error = false;
    if (usbStatus_.state == mn::AndroidState::NoAdb) {
        error = true;
        return L"Android mirroring isn't available: " + from_utf8(usbStatus_.detail);
    }
    switch (pair_.phase) {
    case P::Waiting:
        if (!pair_.codeTarget.empty()) return L"Type the 6-digit code shown on the phone";
        busy = true;
        return L"Waiting for your phone to scan the code…";
    case P::Pairing: busy = true; return L"Pairing with your phone…";
    case P::Connecting: busy = true; return L"Paired. Connecting…";
    case P::Done: return L"Paired with " + (pair_.device.empty() ? std::wstring(L"your phone") : from_utf8(pair_.device));
    case P::Failed: error = true; return from_utf8(pair_.detail.empty() ? "Pairing failed" : pair_.detail);
    default: busy = true; return L"Getting ready…";
    }
}

void App::drawQr(const std::string& text, D2D1_RECT_F r, uint32_t fg, uint32_t bg) {
    static std::string lastText;
    static std::vector<uint8_t> qr(qrcodegen_BUFFER_LEN_MAX);
    static bool ok = false;
    if (text != lastText) {
        std::vector<uint8_t> tmp(qrcodegen_BUFFER_LEN_MAX);
        ok = qrcodegen_encodeText(text.c_str(), tmp.data(), qr.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                  qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
        lastText = text;
    }
    fillRect(r, bg);
    if (!ok || text.empty()) return;
    int n = qrcodegen_getSize(qr.data());
    float side = std::min(r.right - r.left, r.bottom - r.top);
    float m = std::floor(side / (n + 4));  // whole pixels per module, 2-module quiet zone
    if (m < 1) m = side / (n + 4);
    float ox = std::floor((r.left + r.right - m * n) / 2), oy = std::floor((r.top + r.bottom - m * n) / 2);
    brush_->SetColor(rgb(fg));
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (qrcodegen_getModule(qr.data(), x, y))
                rt_->FillRectangle(D2D1::RectF(ox + x * m, oy + y * m, ox + (x + 1) * m, oy + (y + 1) * m), brush_);
}

std::wstring App::videoInfo() const {
    std::wstring s = haveFrame_ ? std::to_wstring(videoW_) + L" × " + std::to_wstring(videoH_) + L", " + std::to_wstring(fps_) + L" fps"
                                : std::wstring(L"Waiting for video");
    if (audioActive_) s += muted_ ? L", muted" : L", sound on";
    return s;
}

void App::ensureArtwork() {
    if (artwork_ || artworkData_.empty() || !wic_ || !rt_) return;
    IWICStream* st = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(wic_->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory(artworkData_.data(), (DWORD)artworkData_.size())) &&
        SUCCEEDED(wic_->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnLoad, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(wic_->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        rt_->CreateBitmapFromWicBitmap(conv, nullptr, &artwork_);
    }
    SafeRelease(conv);
    SafeRelease(fr);
    SafeRelease(dec);
    SafeRelease(st);
    if (!artwork_) artworkData_.clear();
}

D2D1_RECT_F App::videoRect(float W, float H) const {
    if (videoW_ <= 0 || videoH_ <= 0) return D2D1::RectF(0, 0, W, H);
    float vw = (float)videoW_, vh = (float)videoH_;
    float s = std::min(W / vw, H / vh);
    float dw = vw * s, dh = vh * s;
    return D2D1::RectF((W - dw) / 2, (H - dh) / 2, (W + dw) / 2, (H + dh) / 2);
}

// ================================================================== settings (layout shared, look per theme)

void App::drawSettings(float W, float H) {
    Theme& t = *theme_;
    t.settingsBackground(*this, W, H);
    float hh = t.headerHeight();
    float x0, x1;
    t.settingsColumn(W, x0, x1);
    float rowH = t.rowHeight(H), pad = t.rowInset();
    const float gap = 26;

    float maxScroll = std::max(0.0f, settingsContentH_ - (H - hh));
    settingsScroll_ = std::max(0.0f, std::min(settingsScroll_, maxScroll));

    rt_->PushAxisAlignedClip(D2D1::RectF(0, hh, W, H), D2D1_ANTIALIAS_MODE_ALIASED);
    hitClip_ = D2D1::RectF(0, hh, W, H);
    hitClipOn_ = true;
    float y = hh + 10 - settingsScroll_;
    float lx0 = x0 + pad, lx1 = x1 - pad;

    // ---- This PC
    y = t.sectionTitle(*this, x0, x1, y, L"This PC");
    D2D1_RECT_F g = D2D1::RectF(x0, y, x1, y + rowH);
    t.group(*this, g, 1, rowH);
    float cy = g.top + rowH / 2;
    t.rowText(*this, lx0, lx1 - 280, cy, L"Name", L"How this PC appears on your devices");
    D2D1_RECT_F box = D2D1::RectF(lx1 - 260, cy - 15, lx1, cy + 15);
    D2D1_RECT_F editArea = t.textField(*this, box, GetFocus() == edit_, hover_ == H_NAMEBOX);
    addHit(H_NAMEBOX, box);
    layoutEdit(box.top >= hh && box.bottom <= H, editArea);
    y = g.bottom + gap;

    // ---- Screen mirroring
    y = t.sectionTitle(*this, x0, x1, y, L"Screen mirroring");
    g = D2D1::RectF(x0, y, x1, y + rowH * 3);
    t.group(*this, g, 3, rowH);
    cy = g.top + rowH / 2;
    t.rowText(*this, lx0, lx1 - 270, cy, L"Resolution", L"The sharpest picture your phone will send");
    t.choices(*this, {{H_RES0, L"720p"}, {H_RES1, L"1080p"}, {H_RES2, L"1440p"}, {H_RES3, L"4K"}}, H_RES0 + settings_.resolution, lx1, cy);
    cy += rowH;
    t.rowText(*this, lx0, lx1 - 150, cy, L"Frame rate", L"60 is smoother, 30 uses less Wi-Fi");
    t.choices(*this, {{H_FPS30, L"30"}, {H_FPS60, L"60"}}, settings_.fps == 30 ? H_FPS30 : H_FPS60, lx1, cy);
    cy += rowH;
    t.rowText(*this, lx0, lx1 - 70, cy, L"Full screen on start", L"Esc leaves full screen, F11 switches it");
    t.toggle(*this, H_AUTOFS, settings_.autoFullscreen, lx1, cy);
    y = g.bottom + gap;

    // ---- Sound and network
    y = t.sectionTitle(*this, x0, x1, y, L"Sound and network");
    g = D2D1::RectF(x0, y, x1, y + rowH * 2);
    t.group(*this, g, 2, rowH);
    cy = g.top + rowH / 2;
    t.rowText(*this, lx0, lx1 - 70, cy, L"Play sound", L"Sound from your phone plays on this PC");
    t.toggle(*this, H_AUDIO, settings_.audio, lx1, cy);
    cy += rowH;
    t.rowText(*this, lx0, lx1 - 140, cy, L"Windows Firewall", L"Fixes it when your iPhone can't find this PC");
    t.button(*this, H_FIREWALL, L"Allow", G_SHIELD, lx1, cy);
    y = g.bottom + gap;

    // ---- Android
    y = t.sectionTitle(*this, x0, x1, y, L"Android");
    g = D2D1::RectF(x0, y, x1, y + rowH * 5);
    t.group(*this, g, 5, rowH);
    cy = g.top + rowH / 2;
    std::wstring androidDesc = androidSettingsText();
    t.rowText(*this, lx0, lx1 - 70, cy, L"USB cable", androidDesc.c_str());
    t.toggle(*this, H_ANDROID, settings_.usbAndroid, lx1, cy);
    cy += rowH;
    std::wstring wifiDesc = L"Phones paired over Wi-Fi show up when Wireless debugging is on";
    if (settings_.wifiAndroid && usbStatus_.wireless &&
        (usbStatus_.state == mn::AndroidState::Ready || usbStatus_.state == mn::AndroidState::Mirroring))
        wifiDesc = from_utf8(usbStatus_.device) + (usbStatus_.state == mn::AndroidState::Mirroring ? L" is mirroring" : L" is available");
    t.rowText(*this, lx0, lx1 - 70, cy, L"Wi-Fi", wifiDesc.c_str());
    t.toggle(*this, H_ANDROID_WIFI, settings_.wifiAndroid, lx1, cy);
    cy += rowH;
    t.rowText(*this, lx0, lx1 - 140, cy, L"Pair a phone over Wi-Fi", L"Android 11 or later, on the same Wi-Fi as this PC");
    t.button(*this, H_PAIR, L"Pair", G_WIFI, lx1, cy);
    cy += rowH;
    t.rowText(*this, lx0, lx1 - 70, cy, L"Control the phone", L"Mouse, keyboard and clipboard; right-click is Back, middle-click is Home");
    t.toggle(*this, H_ANDROID_CONTROL, settings_.controlAndroid, lx1, cy);
    cy += rowH;
    t.rowText(*this, lx0, lx1 - 70, cy, L"Game controllers", L"Controllers plugged into this PC work on the phone");
    t.toggle(*this, H_GAMEPAD, settings_.gamepadAndroid, lx1, cy);
    y = g.bottom + gap;

    // ---- iPhone
    y = t.sectionTitle(*this, x0, x1, y, L"iPhone");
    g = D2D1::RectF(x0, y, x1, y + rowH);
    t.group(*this, g, 1, rowH);
    cy = g.top + rowH / 2;
    t.rowText(*this, lx0, lx1, cy, L"USB cable",
              iphoneUsb_ ? L"Connected by cable: use Screen Mirroring on the iPhone"
                         : L"Turn on Personal Hotspot on the iPhone, then plug it in");
    y = g.bottom + gap;

    // ---- Updates
    y = t.sectionTitle(*this, x0, x1, y, L"Updates");
    g = D2D1::RectF(x0, y, x1, y + rowH);
    t.group(*this, g, 1, rowH);
    cy = g.top + rowH / 2;
    {
        std::wstring label = std::wstring(L"Mirrorly ") + kVersion, desc;
        bool offer = update_.state == UPD_AVAILABLE || update_.state == UPD_FAILED;
        switch (update_.state) {
        case UPD_CHECKING: desc = L"Checking for updates…"; break;
        case UPD_UP_TO_DATE: desc = L"You have the latest version"; break;
        case UPD_AVAILABLE: desc = L"Version " + from_utf8(update_.version) + L" is available"; break;
        case UPD_DOWNLOADING: desc = L"Downloading " + from_utf8(update_.version) + L"…"; break;
        case UPD_READY:
        case UPD_INSTALLING: desc = L"Installing…"; break;
        case UPD_FAILED: desc = L"Update failed: " + from_utf8(update_.error); break;
        default: desc = L"Updates are checked automatically"; break;
        }
        t.rowText(*this, lx0, lx1 - 150, cy, label.c_str(), desc.c_str());
        if (offer) t.button(*this, H_UPDATE, L"Update", G_DOWNLOAD, lx1, cy);
        else if (update_.state != UPD_DOWNLOADING && update_.state != UPD_READY && update_.state != UPD_INSTALLING)
            t.button(*this, H_UPDATE_CHECK, L"Check", G_REFRESH, lx1, cy);
    }
    y = g.bottom + gap;

    // ---- Theme picker: a horizontal strip, one card per registered theme
    y = t.sectionTitle(*this, x0, x1, y, L"Theme");
    auto& list = allThemes();
    const float cardW = 176, cardH = 110, nameH = 34, cgap = 16;
    themeStripW_ = list.size() * cardW + (list.size() - 1) * cgap;
    themeStripMax_ = std::max(0.0f, themeStripW_ - (x1 - x0));
    themeScroll_ = std::max(0.0f, std::min(themeScroll_, themeStripMax_));
    themeStripRect_ = D2D1::RectF(x0 - 6, y - 6, x1 + 6, y + cardH + nameH + 6);
    addHit(H_THEME_STRIP, themeStripRect_);
    rt_->PushAxisAlignedClip(themeStripRect_, D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_RECT_F oldClip = hitClip_;
    hitClip_ = D2D1::RectF(std::max(themeStripRect_.left, oldClip.left), std::max(themeStripRect_.top, oldClip.top),
                           std::min(themeStripRect_.right, oldClip.right), std::min(themeStripRect_.bottom, oldClip.bottom));
    for (size_t i = 0; i < list.size(); i++) {
        float cx = x0 + i * (cardW + cgap) - themeScroll_;
        D2D1_RECT_F pv = D2D1::RectF(cx, y, cx + cardW, y + cardH);
        int id = H_THEME + (int)i;
        t.themeCard(*this, pv, list[i], list[i] == theme_, hover_ == id);
        addHit(id, D2D1::RectF(pv.left, pv.top, pv.right, pv.bottom + nameH));
    }
    hitClip_ = oldClip;
    rt_->PopAxisAlignedClip();
    if (themeStripMax_ > 0) {  // tiny scroll hint
        float vis = (x1 - x0) / themeStripW_;
        float tw = (x1 - x0) * vis, tx = x0 + (x1 - x0 - tw) * (themeScroll_ / themeStripMax_);
        line(tx, y + cardH + nameH + 2, tx + tw, y + cardH + nameH + 2, 0x808080, 0.5f, 3.0f);
    }
    y += cardH + nameH + gap;

    // ---- footer
    std::wstring ver = std::wstring(kAppName) + L" " + kVersion + L", AirPlay port " + std::to_wstring(rx_.port());
    t.footnote(*this, ver, x0 + 2, y);
    t.link(*this, H_OPENLOG, L"Open log file", x1 - 2, y, true);
    y += 34;

    settingsContentH_ = y + settingsScroll_ - hh;
    hitClipOn_ = false;
    rt_->PopAxisAlignedClip();
    t.settingsHeader(*this, W, settingsScroll_ > 1);
}

// ================================================================== mirroring base layer

void App::drawMirror(float W, float H) {
    rt_->Clear(D2D1::ColorF(0, 0, 0));
    D2D1_RECT_F dst = videoRect(W, H);
    if (video_ && haveFrame_) {
        if (dc_) dc_->DrawBitmap(video_, &dst, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        else rt_->DrawBitmap(video_, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (paused_) theme_->drawMirrorPaused(*this, W, H, dst);
    } else {
        theme_->drawMirrorWaiting(*this, W, H);
    }
    if (keymapEdit_ && controlling()) fillRect(dst, 0x000000, 0.2f);  // the screen dims while editing key zones
    drawKeyZones(W, H, dst);
    if (overlayAlpha_ >= 0.02f && !keymapEdit_) theme_->drawMirrorOverlay(*this, W, H, overlayAlpha_, dst);
    if (keymapEdit_ && controlling()) theme_->drawKeymapBar(*this, W, H, dst);
}

// ================================================================== frame

void App::render() {
    if (!createDeviceResources()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = rc.right * 96.0f / dpi_, H = rc.bottom * 96.0f / dpi_;
    hits_.clear();
    hitClipOn_ = false;
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(rgb(theme_->windowColor(*this)));
    if (pairingOpen_ && !mirrorActive_) {
        layoutEdit(false, D2D1::RectF());
        theme_->drawPairing(*this, W, H);
    } else if (settingsOpen_) {
        drawSettings(W, H);
    } else {
        layoutEdit(false, D2D1::RectF());
        if (mirrorActive_) drawMirror(W, H);
        else if (sessionActive_ && audioActive_) { ensureArtwork(); theme_->drawNowPlaying(*this, W, H); }
        else {
            theme_->drawHome(*this, W, H);
            bool offer = update_.state == UPD_AVAILABLE || update_.state == UPD_FAILED;
            if (offer && !updateDialog_) theme_->drawUpdateBadge(*this, W, H);
        }
    }
    if (updateDialog_ && !mirrorActive_) {
        addHit(H_UPDATE_SCRIM, D2D1::RectF(0, 0, W, H));  // nothing underneath is clickable
        theme_->drawUpdateDialog(*this, W, H);
    }
    uint64_t now = mn::now_ms();
    if (toastUntil_ && now <= toastUntil_ + 250) {
        float a = now > toastUntil_ ? 1.0f - (now - toastUntil_) / 250.0f : 1.0f;
        theme_->drawToast(*this, W, H, toast_, a);
    }
    HRESULT hr = rt_->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
        haveFrame_ = false;
    }
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
