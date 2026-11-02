// Mirrorly - rendering. Visual identity: "acid poster".
//
//  - concrete-grey paper with film grain
//  - giant acid-lime type bleeding off the canvas
//  - an electric cobalt block carrying pixel lettering (our own 5x7 dot-matrix face)
//  - a gunmetal halftone object with lime cracks; it turns cobalt while a device connects
//  - small black technical labels
#include "app.h"

#define _USE_MATH_DEFINES
#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "log.h"
#include "platform.h"

using namespace tok;

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
    rt_->CreateSolidColorBrush(rgb(INK), &brush_);

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
    wchar_t key[96];
    swprintf(key, 96, L"%d|%d|%d|%d", (int)(size * 10), (int)w, (int)a, (int)role);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    const wchar_t* family = role == F_ICON ? iconFont_.c_str() : uiFont_.c_str();
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

// Square icon button. On paper: black icon, acid fill on hover. On video: white icon.
void App::iconButton(int id, wchar_t g, float cx, float cy, float size, float alpha, bool destructive) {
    float r = size / 2;
    D2D1_RECT_F rc = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);
    bool hov = hover_ == id;
    bool onVideo = mirrorActive_ && !settingsOpen_;
    uint32_t fg = onVideo ? WHITE : INK;
    if (hov) {
        fillRect(rc, destructive ? ALERT : ACID, alpha);
        fg = destructive ? WHITE : INK;
    } else if (!onVideo) {
        strokeRect(rc, INK, 0.9f * alpha);
    }
    icon(g, cx, cy, size * 0.40f, fg, alpha);
    addHit(id, rc);
}

void App::toggle(int id, bool on, float x, float y) {
    float w = 48, h = 24;
    D2D1_RECT_F r = D2D1::RectF(x, y - h / 2, x + w, y + h / 2);
    if (on) fillRect(r, ACID);
    else if (hover_ == id) fillRect(r, PAPER2);
    strokeRect(r, INK, 1.0f, 0, 1.5f);
    float k = 16;
    float kx = on ? x + w - 4 - k : x + 4;
    fillRect(D2D1::RectF(kx, y - k / 2, kx + k, y + k / 2), on ? INK : INK3);
    addHit(id, D2D1::RectF(x - 6, y - 18, x + w + 6, y + 18));
}

void App::choices(const std::vector<std::pair<int, std::wstring>>& items, int selected, float right, float cy) {
    IDWriteTextFormat* f = fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    float segW = 62, h = 30;
    float x = right - segW * items.size();
    for (size_t i = 0; i < items.size(); i++) {
        D2D1_RECT_F r = D2D1::RectF(x + segW * i, cy - h / 2, x + segW * (i + 1), cy + h / 2);
        bool sel = items[i].first == selected;
        if (sel) fillRect(r, ACID);
        else if (hover_ == items[i].first) fillRect(r, PAPER2);
        text(items[i].second, r, f, INK, sel ? 1.0f : 0.75f);
        addHit(items[i].first, r);
    }
    strokeRect(D2D1::RectF(x, cy - h / 2, right, cy + h / 2), INK, 1.0f, 0, 1.5f);
    for (size_t i = 1; i < items.size(); i++) line(x + segW * i, cy - h / 2, x + segW * i, cy + h / 2, INK, 1.0f, 1.0f);
}

void App::link(int id, const std::wstring& s, float x, float y, bool alignRight) {
    IDWriteTextFormat* f = fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float w = textWidth(s, f);
    float x0 = alignRight ? x - w : x;
    D2D1_RECT_F r = D2D1::RectF(x0, y - 11, x0 + w, y + 11);
    bool hov = hover_ == id;
    uint32_t c = hov ? COBALT : INK;
    text(s, r, f, c);
    line(x0, y + 9, x0 + w, y + 9, c, 1.0f, hov ? 2.0f : 1.0f);
    addHit(id, D2D1::RectF(r.left - 4, r.top - 4, r.right + 4, r.bottom + 4));
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
        editFont_ = CreateFontW(-MulDiv(16, dpi_, 96), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                uiFont_.c_str());
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

// ================================================================== views

void App::drawTopBar(float W, bool back, const wchar_t* title) {
    float y = 34;
    if (back) {
        iconButton(H_BACK, G_BACK, 38, y, 36);
        pixelText(title, 72, y - 10.5f, 3.0f, INK);
        return;
    }
    pixelText(L"MIRRORLY", 26, y - 7, 2.0f, INK);
    (void)W;
}

void App::drawHome(float W, float H) {
    double t = animTime();
    bool busy = connecting_ || sessionActive_;
    std::wstring name = settings_.name;

    // 1. giant lime type bleeding off the top-left corner
    std::wstring giant = name;
    size_t sp = giant.find(L' ');
    if (sp != std::wstring::npos && sp >= 2) giant = giant.substr(0, sp);
    if (giant.size() > 6) giant = giant.substr(0, 6);
    float gs = H * 0.70f;
    giantType(giant, -W * 0.035f, -gs * 0.30f, gs, ACID);

    // 2. halftone object with cracks
    float r = std::min(W * 0.24f, H * 0.30f);
    float ocx = W * 0.60f, ocy = H * 0.46f;
    drawCracks(ocx, ocy, r, W, H, ACID, 0.95f);
    drawHalftoneOrb(ocx, ocy, r, t, busy ? 1.0f : 0.0f, busy ? COBALT : GUN, ACID);

    drawGrain(W, H, 0.55f);

    // 3. top bar
    drawTopBar(W, false, kAppName);
    iconButton(H_SETTINGS, G_SETTINGS, W - 40, 34, 36);

    // 4. identity block: the receiver's name in pixel lettering on cobalt
    float bx = std::max(28.0f, W * 0.075f);
    float px = std::max(4.0f, std::min(9.0f, H / 72.0f));
    D2D1_RECT_F blk = pixelBlock(name, bx, ocy - px * 9, std::min(W * 0.62f, W - bx - 28), px, COBALT, WHITE);

    // 5. status line
    float sy = blk.bottom + 30;
    std::wstring status;
    uint32_t mark = ACID;
    if (busy) {
        status = L"Connecting to " + from_utf8(client_.name);
        mark = COBALT;
    } else if (!rx_.running()) {
        status = L"AirPlay is stopped. Restart Mirrorly.";
        mark = ALERT;
    } else if (!rx_.discoveryOk()) {
        status = L"Your iPhone can't see this PC. Allow Mirrorly in the firewall.";
        mark = ALERT;
    } else {
        status = L"Ready for your iPhone";
    }
    bool blink = busy && fmod(t * 2.0, 1.0) > 0.5;
    D2D1_RECT_F m = D2D1::RectF(bx, sy - 7, bx + 14, sy + 7);
    if (!blink) fillRect(m, mark);
    strokeRect(m, INK, 1.0f, 0, 1.5f);
    text(status, D2D1::RectF(bx + 24, sy - 14, W - 40, sy + 14), fmt(19, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK);

    // 6. the three steps (a real sequence, so they are numbered)
    float stY = std::max(sy + 54, H - 76);
    float x = bx;
    const std::wstring steps[3] = {L"Open Control Center", L"Tap Screen Mirroring", L"Choose " + name};
    IDWriteTextFormat* stf = fmt(14, DWRITE_FONT_WEIGHT_NORMAL);
    for (int i = 0; i < 3; i++) {
        D2D1_RECT_F nb = D2D1::RectF(x, stY - 11, x + 22, stY + 11);
        fillRect(nb, INK);
        pixelText(std::to_wstring(i + 1), x + 11 - 4.5f, stY - 5.25f, 1.5f, ACID);
        float tw = textWidth(steps[i], stf);
        text(steps[i], D2D1::RectF(x + 30, stY - 12, x + 30 + tw + 2, stY + 12), stf, INK);
        x += 30 + tw + 26;
    }

    // 7. technical labels, top right
    std::wstring ips;
    for (auto& ni : mn::list_ipv4_interfaces()) {
        ips = from_utf8(mn::ip_to_string(ni.addr));
        break;
    }
    int rw, rh;
    settings_.resolutionSize(rw, rh);
    const wchar_t* resName[] = {L"720P", L"1080P", L"1440P", L"4K"};
    labelColumn({L"AIRPLAY RECEIVER", ips.empty() ? L"NO NETWORK" : ips, L"PORT " + std::to_wstring(rx_.port()), L"",
                 std::wstring(resName[std::max(0, std::min(3, settings_.resolution))]) + L" / " + std::to_wstring(settings_.fps) + L" FPS",
                 settings_.audio ? L"SOUND ON" : L"SOUND OFF", L"", std::wstring(L"V") + kVersion},
                W - 28, 66, INK);

    // 8. help link, bottom right
    link(H_FIREWALL_LINK, L"Not on your iPhone? Fix the firewall", W - 28, H - 30, true);
}

void App::drawSettings(float W, float H) {
    drawGrain(W, H, 0.55f);
    drawTopBar(W, true, L"SETTINGS");
    float x0 = 72;
    float x1 = std::min(W - 40, x0 + 720);
    float rowH = std::max(50.0f, std::min(64.0f, (H - 250) / 6.0f));
    float y = 80;
    IDWriteTextFormat* lab = fmt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    IDWriteTextFormat* desc = fmt(13);

    auto section = [&](const wchar_t* title) {
        y += 14;
        text(title, D2D1::RectF(x0, y, x1, y + 22), fmt(13, DWRITE_FONT_WEIGHT_BOLD), INK);
        y += 26;
        line(x0, y, x1, y, INK, 1.0f, 1.5f);
    };
    auto label = [&](float ry, const wchar_t* l, const wchar_t* d, float reserve) {
        text(l, D2D1::RectF(x0, ry - 19, x1 - reserve, ry + 1), lab, INK);
        text(d, D2D1::RectF(x0, ry + 1, x1 - reserve, ry + 19), desc, INK2);
    };
    auto divider = [&](float ry) { line(x0, ry, x1, ry, INK, 0.25f, 1.0f); };

    section(L"This PC");
    float ry = y + rowH / 2;
    label(ry, L"Name", L"How this PC appears on your iPhone", 300);
    D2D1_RECT_F box = D2D1::RectF(x1 - 270, ry - 16, x1, ry + 16);
    bool focused = GetFocus() == edit_;
    if (hover_ == H_NAMEBOX && !focused) fillRect(box, PAPER2);
    line(box.left, box.bottom, box.right, box.bottom, focused ? COBALT : INK, 1.0f, focused ? 3.0f : 1.5f);
    addHit(H_NAMEBOX, box);
    layoutEdit(true, D2D1::RectF(box.left + 2, box.top, box.right - 2, box.bottom - 3));
    y += rowH;

    section(L"Screen mirroring");
    ry = y + rowH / 2;
    label(ry, L"Resolution", L"The sharpest picture your iPhone will send", 270);
    choices({{H_RES0, L"720p"}, {H_RES1, L"1080p"}, {H_RES2, L"1440p"}, {H_RES3, L"4K"}}, H_RES0 + settings_.resolution, x1, ry);
    divider(y + rowH);
    ry += rowH;
    label(ry, L"Frame rate", L"60 is smoother, 30 uses less Wi-Fi", 150);
    choices({{H_FPS30, L"30"}, {H_FPS60, L"60"}}, settings_.fps == 30 ? H_FPS30 : H_FPS60, x1, ry);
    divider(y + rowH * 2);
    ry += rowH;
    label(ry, L"Full screen on start", L"Esc leaves full screen, F11 switches it", 70);
    toggle(H_AUTOFS, settings_.autoFullscreen, x1 - 48, ry);
    y += rowH * 3;

    section(L"Sound and network");
    ry = y + rowH / 2;
    label(ry, L"Play sound", L"Sound from your iPhone plays on this PC", 70);
    toggle(H_AUDIO, settings_.audio, x1 - 48, ry);
    divider(y + rowH);
    ry += rowH;
    label(ry, L"Windows Firewall", L"Fixes it when your iPhone can't find this PC", 140);
    {
        IDWriteTextFormat* bf = fmt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F b = D2D1::RectF(x1 - 120, ry - 17, x1, ry + 17);
        fillRect(b, hover_ == H_FIREWALL ? INK : COBALT);
        icon(G_SHIELD, b.left + 20, ry, 13, WHITE);
        text(L"Allow", D2D1::RectF(b.left + 28, b.top, b.right, b.bottom), bf, WHITE);
        addHit(H_FIREWALL, b);
    }
    y += rowH * 2 + 26;

    std::wstring ver = std::wstring(kAppName) + L" " + kVersion + L", AirPlay port " + std::to_wstring(rx_.port());
    text(ver, D2D1::RectF(x0, y - 11, x0 + 400, y + 11), fmt(12), INK3);
    link(H_OPENLOG, L"Open log file", x1, y, true);
}

void App::drawMirror(float W, float H) {
    rt_->Clear(D2D1::ColorF(0, 0, 0));
    double t = animTime();
    if (video_ && haveFrame_) {
        float vw = (float)videoW_, vh = (float)videoH_;
        float s = std::min(W / vw, H / vh);
        float dw = vw * s, dh = vh * s;
        D2D1_RECT_F dst = D2D1::RectF((W - dw) / 2, (H - dh) / 2, (W + dw) / 2, (H + dh) / 2);
        if (dc_) dc_->DrawBitmap(video_, &dst, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        else rt_->DrawBitmap(video_, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (paused_) {
            brush_->SetColor(D2D1::ColorF(0, 0, 0, 0.6f));
            rt_->FillRectangle(D2D1::RectF(0, 0, W, H), brush_);
            float pw = pixelTextWidth(L"SCREEN LOCKED", 4) + 20;
            pixelBlock(L"SCREEN LOCKED", W / 2 - pw / 2, H / 2 - 30, W - 40, 4, COBALT, WHITE);
            text(L"Unlock " + from_utf8(client_.name) + L" to carry on", D2D1::RectF(0, H / 2 + 22, W, H / 2 + 46),
                 fmt(14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), WHITE, 0.85f);
        }
    } else {
        drawHalftoneOrb(W / 2, H / 2 - 30, std::min(W, H) * 0.12f, t * 1.5, 1.0f, WHITE, ACID);
        text(L"Starting screen mirroring from " + from_utf8(client_.name), D2D1::RectF(0, H / 2 + std::min(W, H) * 0.12f, W, H / 2 + std::min(W, H) * 0.12f + 30),
             fmt(15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), WHITE, 0.85f);
    }

    float a = overlayAlpha_;
    if (a < 0.02f) return;
    ID2D1GradientStopCollection* stops = nullptr;
    D2D1_GRADIENT_STOP gs[2] = {{0.0f, D2D1::ColorF(0, 0, 0, 0.75f * a)}, {1.0f, D2D1::ColorF(0, 0, 0, 0)}};
    rt_->CreateGradientStopCollection(gs, 2, &stops);
    ID2D1LinearGradientBrush* lg = nullptr;
    if (stops) rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 110)), stops, &lg);
    if (lg) rt_->FillRectangle(D2D1::RectF(0, 0, W, 110), lg);
    SafeRelease(lg);
    SafeRelease(stops);

    // the sender, as a small identity block
    rt_->PushAxisAlignedClip(D2D1::RectF(0, 0, W, H), D2D1_ANTIALIAS_MODE_ALIASED);
    std::wstring who = from_utf8(client_.name);
    D2D1_RECT_F b = pixelBlock(who, 20, 18, std::min(380.0f, W - 260), 2.5f, COBALT, WHITE);
    std::wstring sub = haveFrame_ ? std::to_wstring(videoW_) + L" × " + std::to_wstring(videoH_) + L", " + std::to_wstring(fps_) + L" fps"
                                  : std::wstring(L"Waiting for video");
    if (audioActive_) sub += muted_ ? L", muted" : L", sound on";
    text(sub, D2D1::RectF(b.right + 14, b.top, W - 220, b.bottom), fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), WHITE, 0.85f * a);
    rt_->PopAxisAlignedClip();

    float bx = W - 36;
    iconButton(H_DISCONNECT, G_CLOSE, bx, 36, 38, a, true);
    bx -= 46;
    iconButton(H_FULLSCREEN, fullscreen_ ? G_WINDOW : G_FULL, bx, 36, 38, a);
    bx -= 46;
    iconButton(H_MUTE, muted_ ? G_MUTE : G_VOLUME, bx, 36, 38, a);
    bx -= 46;
    iconButton(H_SETTINGS, G_SETTINGS, bx, 36, 38, a);
}

void App::drawNowPlaying(float W, float H) {
    double t = animTime();
    std::wstring title = title_.empty() ? L"Now playing" : title_;

    // giant lime title, low on the canvas, cropped by the right edge
    std::wstring giant = title;
    size_t sp = giant.find(L' ');
    if (sp != std::wstring::npos && sp >= 3) giant = giant.substr(0, sp);
    if (giant.size() > 8) giant = giant.substr(0, 8);
    float gs = H * 0.55f;
    giantType(giant, W * 0.30f, H - gs * 0.92f, gs, ACID);
    drawGrain(W, H, 0.55f);

    drawTopBar(W, false, kAppName);
    iconButton(H_DISCONNECT, G_CLOSE, W - 40, 34, 36, 1.0f, true);
    iconButton(H_SETTINGS, G_SETTINGS, W - 86, 34, 36);

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

    float x0 = std::max(28.0f, W * 0.075f);
    float size = std::max(140.0f, std::min({300.0f, H - 230, W * 0.32f}));
    float top = std::max(84.0f, (H - size) / 2 - 10);
    D2D1_RECT_F art = D2D1::RectF(x0, top, x0 + size, top + size);
    fillRect(D2D1::RectF(art.left + 12, art.top + 12, art.right + 12, art.bottom + 12), ACID);  // hard offset shadow
    if (artwork_) {
        rt_->DrawBitmap(artwork_, art, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        fillRect(art, COBALT);
        rt_->PushAxisAlignedClip(art, D2D1_ANTIALIAS_MODE_ALIASED);
        drawHalftoneOrb((art.left + art.right) / 2, (art.top + art.bottom) / 2, size * 0.30f, t, audio_.level() * 2.0f, WHITE, ACID);
        rt_->PopAxisAlignedClip();
    }
    strokeRect(art, INK, 1.0f, 0, 2.0f);

    // details column
    float cx = art.right + 48;
    float cw = W - cx - 40;
    D2D1_RECT_F tb = pixelBlock(title, cx, top, cw, std::max(3.0f, std::min(6.0f, H / 110.0f)), COBALT, WHITE);
    float y = tb.bottom + 26;
    std::wstring sub = artist_;
    if (!album_.empty()) sub += (sub.empty() ? L"" : L", ") + album_;
    if (sub.empty()) sub = L"From " + from_utf8(client_.name);
    text(sub, D2D1::RectF(cx, y - 12, cx + cw, y + 12), fmt(16, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK);
    y += 34;

    float bw = std::min(420.0f, cw);
    if (progDur_ > 0) {
        double pos = progPos_ + (reduceMotion_ ? 0 : (mn::now_ms() - progStamp_) / 1000.0);
        pos = std::min(pos, progDur_);
        float frac = (float)(pos / progDur_);
        line(cx, y, cx + bw, y, INK, 0.3f, 2.0f);
        line(cx, y, cx + bw * frac, y, INK, 1.0f, 3.0f);
        auto fmtTime = [](double s) {
            int v = (int)s;
            wchar_t b[16];
            swprintf(b, 16, L"%d:%02d", v / 60, v % 60);
            return std::wstring(b);
        };
        IDWriteTextFormat* tf = fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        text(fmtTime(pos), D2D1::RectF(cx, y + 6, cx + 80, y + 24), tf, INK2);
        text(fmtTime(progDur_), D2D1::RectF(cx + bw - 80, y + 6, cx + bw, y + 24), fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING), INK2);
        y += 46;
    } else {
        y += 10;
    }

    // live oscilloscope of what is playing
    float sh = 64;
    static float buf[512];
    audio_.scope(buf, 512);
    ID2D1PathGeometry* g = nullptr;
    d2d_->CreatePathGeometry(&g);
    if (g) {
        ID2D1GeometrySink* sk = nullptr;
        g->Open(&sk);
        float mid = y + sh / 2;
        sk->BeginFigure(D2D1::Point2F(cx, mid - buf[0] * sh * 0.9f), D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 1; i < 512; i++) {
            float v = std::max(-1.0f, std::min(1.0f, buf[i] * 1.6f));
            sk->AddLine(D2D1::Point2F(cx + bw * i / 511.0f, mid - v * sh / 2));
        }
        sk->EndFigure(D2D1_FIGURE_END_OPEN);
        sk->Close();
        sk->Release();
        brush_->SetColor(rgb(INK));
        rt_->DrawGeometry(g, brush_, 1.5f);
        g->Release();
    }
    y += sh + 22;
    iconButton(H_MUTE, muted_ ? G_MUTE : G_VOLUME, cx + 18, y, 36);
    std::wstring st = !settings_.audio ? L"Sound is off in Settings" : (muted_ ? L"Muted" : L"Playing on this PC");
    text(st, D2D1::RectF(cx + 46, y - 12, cx + cw, y + 12), fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), settings_.audio ? INK2 : ALERT);
}

void App::drawToast(float W, float H) {
    uint64_t now = mn::now_ms();
    if (!toastUntil_ || now > toastUntil_ + 250) return;
    float a = now > toastUntil_ ? 1.0f - (now - toastUntil_) / 250.0f : 1.0f;
    IDWriteTextFormat* f = fmt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float tw = std::min(textWidth(toast_, f) + 36, W - 56);
    D2D1_RECT_F r = D2D1::RectF(28, H - 100, 28 + tw, H - 64);
    fillRect(r, INK, a);
    fillRect(D2D1::RectF(r.left, r.top, r.left + 6, r.bottom), ACID, a);
    text(toast_, D2D1::RectF(r.left + 20, r.top, r.right - 8, r.bottom), f, ACID, a);
}

void App::render() {
    if (!createDeviceResources()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = rc.right * 96.0f / dpi_, H = rc.bottom * 96.0f / dpi_;
    hits_.clear();
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(rgb(PAPER));
    if (settingsOpen_) {
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
