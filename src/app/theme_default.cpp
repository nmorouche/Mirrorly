// Mirrorly theme: "Default"
//
// Clean and quiet: soft greys, one blue accent for interactive things,
// grouped inset lists, soft layered shadows, frosted controls over video. Follows the Windows
// light/dark setting, with each mode tuned separately rather than inverted.
#include <algorithm>
#include <cmath>

#include "app.h"
#include "platform.h"

namespace {

struct Pal {
    bool dark;
    uint32_t base, card, fill, field, knobOff, text, text2, text3, accent, accentHover, green, red, orange;
    uint32_t sep;
    float sepA, borderA, shadow;
};

const Pal LIGHT = {false, 0xF5F5F7, 0xFFFFFF, 0xE8E8ED, 0xFFFFFF, 0xE3E3E8, 0x1D1D1F, 0x6E6E73, 0xAEAEB2,
                   0x007AFF, 0x0066D6, 0x34C759, 0xFF3B30, 0xFF9500, 0x000000, 0.08f, 0.06f, 0.07f};
const Pal DARK = {true, 0x1C1C1E, 0x2C2C2E, 0x3A3A3C, 0x1C1C1E, 0x48484A, 0xF5F5F7, 0x98989D, 0x636366,
                  0x0A84FF, 0x409CFF, 0x30D158, 0xFF453A, 0xFF9F0A, 0xFFFFFF, 0.09f, 0.07f, 0.30f};

const Pal& pal(App& a) { return a.systemDark_ ? DARK : LIGHT; }

void roundFill(App& a, D2D1_RECT_F r, float rad, uint32_t c, float al = 1.0f) { a.fillRect(r, c, al, rad); }

void hairlineBorder(App& a, D2D1_RECT_F r, float rad, uint32_t c, float al) { a.strokeRect(r, c, al, rad, 1.0f); }

// round toolbar-style icon button (circle on hover)
void circleButton(App& a, int id, wchar_t g, float cx, float cy, float d, uint32_t fg, uint32_t hoverFill, float hoverA,
                  float al = 1.0f) {
    bool hov = a.hover_ == id;
    if (hov) a.dot(cx, cy, d / 2, hoverFill, hoverA * al);
    a.icon(g, cx, cy, d * 0.36f, fg, al);
    a.addHit(id, D2D1::RectF(cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2));
}

// the "screen mirroring" symbol: two overlapping rounded rectangles, monoline
void mirrorSymbol(App& a, float cx, float cy, float s, uint32_t color, uint32_t knockout) {
    float lw = std::max(1.5f, s * 0.055f);
    D2D1_RECT_F back = D2D1::RectF(cx - s * 0.50f, cy - s * 0.36f, cx + s * 0.40f, cy + s * 0.22f);
    D2D1_RECT_F front = D2D1::RectF(cx - s * 0.12f, cy - s * 0.04f, cx + s * 0.50f, cy + s * 0.40f);
    a.strokeRect(back, color, 1.0f, s * 0.10f, lw);
    a.fillRect(D2D1::RectF(front.left - lw, front.top - lw, front.right + lw, front.bottom + lw), knockout, 1.0f, s * 0.09f + lw);
    a.strokeRect(front, color, 1.0f, s * 0.08f, lw);
}

// frosted "material" panel over video: blurred video behind, dark tint, hairline edge
void frosted(App& a, D2D1_RECT_F r, float rad, D2D1_RECT_F video, float al) {
    if (a.video_ && a.haveFrame_) a.drawBlurred(a.video_, video, 26.0f, al, &r, rad);
    roundFill(a, r, rad, 0x1C1C1E, 0.58f * al);
    hairlineBorder(a, r, rad, 0xFFFFFF, 0.14f * al);
}

std::wstring fmtTime(double s) {
    int v = (int)s;
    wchar_t b[16];
    swprintf(b, 16, L"%d:%02d", v / 60, v % 60);
    return b;
}

}  // namespace

class DefaultTheme : public Theme {
public:
    const wchar_t* id() const override { return L"default"; }
    const wchar_t* name() const override { return L"Default"; }
    int iconResource() const override { return 1; }
    void onActivate(App& a) override { a.readSystemTheme(); }

    bool darkTitleBar(App& a) const override { return a.systemDark_; }
    uint32_t windowColor(App& a) const override { return pal(a).base; }
    uint32_t editBackground(App& a) const override { return pal(a).field; }
    uint32_t editText(App& a) const override { return pal(a).text; }
    const wchar_t* uiFont(App& a, bool display) const override { return display ? a.segoeDisplay_.c_str() : a.segoeText_.c_str(); }

    // ---------------------------------------------------------------- home
    void drawHome(App& a, float W, float H) override {
        const Pal& p = pal(a);
        a.fillRect(D2D1::RectF(0, 0, W, H), p.base);
        circleButton(a, H_SETTINGS, G_SETTINGS, W - 32, 30, 34, p.text2, p.text, 0.07f);

        bool busy = a.connecting_ || a.sessionActive_;
        float cx = W / 2;
        float cy = std::max(112.0f, H * 0.28f);
        double t = a.animTime();

        // symbol in a soft accent disc, with a gentle "ready" pulse
        if (!busy && a.rx_.running() && !a.reduceMotion_) {
            double ph = fmod(t / 2.6, 1.0);
            float rr = 44 + (float)ph * 22;
            a.brush_->SetColor(rgb(p.accent, (float)(1.0 - ph) * 0.18f));
            a.rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rr, rr), a.brush_, 1.5f);
        }
        a.dot(cx, cy, 44, p.accent, p.dark ? 0.20f : 0.11f);
        uint32_t knock = p.dark ? 0x1F2A3A : 0xE6F0FF;  // the disc colour, so the front shape occludes the back one
        mirrorSymbol(a, cx, cy, 40, p.accent, knock);

        // name
        float y = cy + 78;
        a.text(a.settings_.name, D2D1::RectF(24, y - 20, W - 24, y + 20),
               a.fmt(30, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, F_UI_DISPLAY), p.text);
        y += 36;

        // status
        std::wstring status;
        uint32_t dotc = p.green;
        if (busy) {
            status = L"Connecting to " + a.clientName() + L"…";
        } else if (!a.rx_.running()) {
            status = L"AirPlay is turned off. Restart Mirrorly.";
            dotc = p.red;
        } else if (!a.rx_.discoveryOk()) {
            status = L"Your iPhone can't see this PC yet";
            dotc = p.orange;
        } else {
            status = L"Ready for AirPlay";
        }
        IDWriteTextFormat* sf = a.fmt(14);
        float sw = a.textWidth(status, sf);
        float sx = cx - (sw + 18) / 2;
        if (busy) a.activitySpinner(sx + 6, y, 7, p.text2);
        else a.dot(sx + 5, y, 4, dotc);
        a.text(status, D2D1::RectF(sx + 18, y - 11, sx + 18 + sw + 4, y + 11), sf, p.text2);
        y += 32;

        // grouped card with the three steps
        float cw = std::min(400.0f, W - 48);
        float rowH = 44;
        D2D1_RECT_F card = D2D1::RectF(cx - cw / 2, y, cx + cw / 2, y + rowH * 3);
        if (!p.dark) a.softShadow(card, 12, p.shadow, 8, 3);
        roundFill(a, card, 12, p.card);
        hairlineBorder(a, card, 12, p.sep, p.borderA);
        const std::wstring steps[3] = {L"Open Control Center on your iPhone", L"Tap Screen Mirroring",
                                       L"Choose “" + a.settings_.name + L"”"};
        for (int i = 0; i < 3; i++) {
            float ry = card.top + rowH * i + rowH / 2;
            a.dot(card.left + 27, ry, 11, p.accent, p.dark ? 0.22f : 0.12f);
            a.text(std::to_wstring(i + 1), D2D1::RectF(card.left + 16, ry - 11, card.left + 38, ry + 11),
                   a.fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER), p.accent);
            a.text(steps[i], D2D1::RectF(card.left + 50, ry - 11, card.right - 16, ry + 11), a.fmt(14), p.text);
            if (i > 0) a.line(card.left + 50, card.top + rowH * i, card.right, card.top + rowH * i, p.sep, p.sepA, 1.0f);
        }
        y = card.bottom + 26;

        // cable (USB) line
        std::wstring action;
        bool attention = false;
        std::wstring wired = a.wiredHint(action, attention);
        if (!wired.empty()) {
            IDWriteTextFormat* wf = a.fmt(13);
            IDWriteTextFormat* af = a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            float tw = std::min(a.textWidth(wired, wf), W - 140), aw = action.empty() ? 0 : a.textWidth(action, af) + 14;
            float x = cx - (22 + tw + aw) / 2;
            a.icon(G_USB, x + 8, y, 15, attention ? p.orange : p.text2);
            a.text(wired, D2D1::RectF(x + 22, y - 10, x + 22 + tw + 2, y + 10), wf, attention ? p.text : p.text2);
            if (!action.empty()) {
                D2D1_RECT_F ar = D2D1::RectF(x + 22 + tw + 14, y - 10, x + 22 + tw + aw + 2, y + 10);
                bool hov = a.hover_ == H_USB_ACTION;
                a.text(action, ar, af, hov ? p.accentHover : p.accent);
                if (hov) a.line(ar.left, ar.bottom - 1, ar.right - 2, ar.bottom - 1, p.accentHover, 1.0f, 1.0f);
                a.addHit(H_USB_ACTION, D2D1::RectF(ar.left - 6, ar.top - 4, ar.right + 6, ar.bottom + 4));
            }
            y += 34;
        }

        // footer
        std::wstring ip = a.primaryIp();
        std::wstring info = (ip.empty() ? std::wstring(L"Not connected to a network") : ip + L", port " + std::to_wstring(a.rx_.port()));
        a.text(info, D2D1::RectF(0, y - 9, W, y + 9), a.fmt(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), p.text3);
        y += 22;
        std::wstring help = L"Can't find this PC?";
        IDWriteTextFormat* lf = a.fmt(13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        float lw = a.textWidth(help, lf);
        D2D1_RECT_F lr = D2D1::RectF(cx - lw / 2, y - 10, cx + lw / 2, y + 10);
        bool hov = a.hover_ == H_FIREWALL_LINK;
        a.text(help, lr, lf, hov ? p.accentHover : p.accent);
        if (hov) a.line(lr.left, lr.bottom - 1, lr.right, lr.bottom - 1, p.accentHover, 1.0f, 1.0f);
        a.addHit(H_FIREWALL_LINK, D2D1::RectF(lr.left - 6, lr.top - 4, lr.right + 6, lr.bottom + 4));
    }

    // ---------------------------------------------------------------- now playing
    void drawNowPlaying(App& a, float W, float H) override {
        const Pal& p = pal(a);
        a.fillRect(D2D1::RectF(0, 0, W, H), p.base);
        if (a.artwork_) {  // the artwork, enlarged and blurred, as the backdrop
            D2D1_SIZE_F s = a.artwork_->GetSize();
            float sc = std::max(W / s.width, H / s.height) * 1.2f;
            float dw = s.width * sc, dh = s.height * sc;
            D2D1_RECT_F full = D2D1::RectF(0, 0, W, H);
            a.drawBlurred(a.artwork_, D2D1::RectF((W - dw) / 2, (H - dh) / 2, (W + dw) / 2, (H + dh) / 2), 60.0f, 1.0f, &full, 0);
            a.fillRect(full, p.base, p.dark ? 0.55f : 0.62f);
        }
        circleButton(a, H_DISCONNECT, G_CLOSE, W - 32, 30, 34, p.text2, p.text, 0.08f);
        circleButton(a, H_SETTINGS, G_SETTINGS, W - 72, 30, 34, p.text2, p.text, 0.08f);

        float size = std::max(120.0f, std::min(260.0f, H - 270));
        float total = size + 160;
        float top = std::max(64.0f, (H - total) / 2);
        float cx = W / 2;
        D2D1_RECT_F art = D2D1::RectF(cx - size / 2, top, cx + size / 2, top + size);
        a.softShadow(art, 12, p.dark ? 0.45f : 0.22f, 18, 10);
        if (a.artwork_) {
            ID2D1RoundedRectangleGeometry* g = nullptr;
            ID2D1Layer* layer = nullptr;
            a.d2d_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(art, 12, 12), &g);
            a.rt_->CreateLayer(&layer);
            if (g && layer) a.rt_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g), layer);
            a.rt_->DrawBitmap(a.artwork_, art, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            if (g && layer) a.rt_->PopLayer();
            SafeRelease(layer);
            SafeRelease(g);
        } else {
            roundFill(a, art, 12, p.card);
            a.icon(G_MUSIC, cx, (art.top + art.bottom) / 2, size * 0.2f, p.text3);
        }
        hairlineBorder(a, art, 12, p.sep, p.borderA);

        float y = art.bottom + 34;
        std::wstring title = a.title_.empty() ? std::wstring(L"Playing audio") : a.title_;
        a.text(title, D2D1::RectF(24, y - 15, W - 24, y + 15), a.fmt(22, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, F_UI_DISPLAY), p.text);
        y += 28;
        std::wstring sub = a.artist_;
        if (!a.album_.empty()) sub += (sub.empty() ? L"" : L", ") + a.album_;
        if (sub.empty()) sub = L"From " + a.clientName();
        a.text(sub, D2D1::RectF(24, y - 11, W - 24, y + 11), a.fmt(15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), p.text2);
        y += 32;

        float bw = std::min(320.0f, W - 80);
        if (a.progDur_ > 0) {
            double pos = a.progPos_ + (a.reduceMotion_ ? 0 : (mn::now_ms() - a.progStamp_) / 1000.0);
            pos = std::min(pos, a.progDur_);
            float frac = (float)(pos / a.progDur_);
            D2D1_RECT_F track = D2D1::RectF(cx - bw / 2, y - 2, cx + bw / 2, y + 2);
            roundFill(a, track, 2, p.text, 0.15f);
            roundFill(a, D2D1::RectF(track.left, track.top, track.left + std::max(4.0f, bw * frac), track.bottom), 2, p.text, 0.75f);
            a.text(fmtTime(pos), D2D1::RectF(track.left, y + 6, track.left + 60, y + 22), a.fmt(11), p.text2);
            a.text(fmtTime(a.progDur_), D2D1::RectF(track.right - 60, y + 6, track.right, y + 22),
                   a.fmt(11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING), p.text2);
            y += 36;
        }
        circleButton(a, H_MUTE, a.muted_ ? G_MUTE : G_VOLUME, cx, y + 6, 38, a.muted_ ? p.red : p.text, p.text, 0.08f);
        if (!a.settings_.audio)
            a.text(L"Sound is turned off in Settings", D2D1::RectF(0, y + 30, W, y + 48),
                   a.fmt(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), p.orange);
    }

    // ---------------------------------------------------------------- mirroring
    void drawMirrorWaiting(App& a, float W, float H) override {
        a.activitySpinner(W / 2, H / 2 - 28, 14, 0xFFFFFF);
        a.text(L"Starting Screen Mirroring", D2D1::RectF(0, H / 2 + 2, W, H / 2 + 26),
               a.fmt(17, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, F_UI_DISPLAY), 0xFFFFFF, 0.9f);
        a.text(a.clientName(), D2D1::RectF(0, H / 2 + 28, W, H / 2 + 48), a.fmt(13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER),
               0xFFFFFF, 0.55f);
    }

    void drawMirrorPaused(App& a, float W, float H, D2D1_RECT_F video) override {
        D2D1_RECT_F full = D2D1::RectF(0, 0, W, H);
        a.drawBlurred(a.video_, video, 30.0f, 1.0f, &full, 0);
        a.fillRect(full, 0x000000, 0.35f);
        float pw = 300, ph = 150;
        D2D1_RECT_F panel = D2D1::RectF(W / 2 - pw / 2, H / 2 - ph / 2, W / 2 + pw / 2, H / 2 + ph / 2);
        frosted(a, panel, 16, video, 1.0f);
        a.icon(G_LOCK, W / 2, panel.top + 42, 22, 0xFFFFFF, 0.95f);
        a.text(L"Screen locked", D2D1::RectF(panel.left, panel.top + 72, panel.right, panel.top + 96),
               a.fmt(17, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, F_UI_DISPLAY), 0xFFFFFF);
        a.text(L"Unlock " + a.clientName() + L" to continue", D2D1::RectF(panel.left + 12, panel.top + 98, panel.right - 12, panel.top + 120),
               a.fmt(13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), 0xFFFFFF, 0.7f);
    }

    void drawMirrorOverlay(App& a, float W, float H, float al, D2D1_RECT_F video) override {
        const Pal& p = pal(a);
        // top: who is mirroring
        IDWriteTextFormat* nf = a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        IDWriteTextFormat* inf = a.fmt(12);
        std::wstring who = a.clientName(), info = a.videoInfo();
        float w1 = a.textWidth(who, nf), w2 = a.textWidth(info, inf);
        float pw = std::min(W - 32, 16 + 14 + w1 + 10 + w2 + 16);
        D2D1_RECT_F top = D2D1::RectF(W / 2 - pw / 2, 14, W / 2 + pw / 2, 14 + 36);
        frosted(a, top, 18, video, al);
        float x = top.left + 16;
        a.dot(x + 4, 32, 4, DARK.green, al);
        x += 14;
        a.text(who, D2D1::RectF(x, top.top, x + w1 + 2, top.bottom), nf, 0xFFFFFF, al);
        x += w1 + 10;
        a.text(info, D2D1::RectF(x, top.top, top.right - 12, top.bottom), inf, 0xFFFFFF, 0.7f * al);

        // bottom: controls
        float bd = 40, gap = 4, padd = 6;
        float bw = padd * 2 + bd * 4 + gap * 3 + 9;
        D2D1_RECT_F bar = D2D1::RectF(W / 2 - bw / 2, H - 24 - (bd + padd * 2), W / 2 + bw / 2, H - 24);
        frosted(a, bar, (bd + padd * 2) / 2, video, al);
        float bcy = (bar.top + bar.bottom) / 2;
        float bx = bar.left + padd + bd / 2;
        circleButton(a, H_MUTE, a.muted_ ? G_MUTE : G_VOLUME, bx, bcy, bd, 0xFFFFFF, 0xFFFFFF, 0.16f, al);
        bx += bd + gap;
        circleButton(a, H_FULLSCREEN, a.fullscreen_ ? G_WINDOW : G_FULL, bx, bcy, bd, 0xFFFFFF, 0xFFFFFF, 0.16f, al);
        bx += bd + gap;
        circleButton(a, H_SETTINGS, G_SETTINGS, bx, bcy, bd, 0xFFFFFF, 0xFFFFFF, 0.16f, al);
        bx += bd / 2 + 4;
        a.line(bx + 0.5f, bcy - 12, bx + 0.5f, bcy + 12, 0xFFFFFF, 0.2f * al, 1.0f);
        bx += 5 + bd / 2;
        bool hov = a.hover_ == H_DISCONNECT;
        circleButton(a, H_DISCONNECT, G_CLOSE, bx, bcy, bd, hov ? 0xFFFFFF : DARK.red, DARK.red, 0.9f, al);
        (void)p;
    }

    void drawToast(App& a, float W, float H, const std::wstring& msg, float al) override {
        IDWriteTextFormat* f = a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        float tw = std::min(a.textWidth(msg, f) + 40, W - 48);
        D2D1_RECT_F r = D2D1::RectF(W / 2 - tw / 2, H - 78, W / 2 + tw / 2, H - 42);
        a.softShadow(r, 18, 0.25f * al, 10, 4);
        roundFill(a, r, 18, 0x2C2C2E, 0.96f * al);
        a.text(msg, r, f, 0xFFFFFF, al);
    }

    void drawPreview(App& a, D2D1_RECT_F r) override {
        const Pal& p = pal(a);
        float w = r.right - r.left, h = r.bottom - r.top, cx = (r.left + r.right) / 2;
        a.fillRect(r, p.base);
        float cy = r.top + h * 0.27f;
        a.dot(cx, cy, h * 0.13f, p.accent, p.dark ? 0.2f : 0.11f);
        mirrorSymbol(a, cx, cy, h * 0.12f, p.accent, p.dark ? 0x1F2A3A : 0xE6F0FF);
        roundFill(a, D2D1::RectF(cx - w * 0.17f, r.top + h * 0.45f, cx + w * 0.17f, r.top + h * 0.45f + 6), 3, p.text, 0.85f);
        roundFill(a, D2D1::RectF(cx - w * 0.11f, r.top + h * 0.55f, cx + w * 0.11f, r.top + h * 0.55f + 4), 2, p.text2, 0.5f);
        D2D1_RECT_F card = D2D1::RectF(cx - w * 0.30f, r.top + h * 0.66f, cx + w * 0.30f, r.top + h * 0.95f);
        if (!p.dark) a.softShadow(card, 5, 0.06f, 4, 2);
        roundFill(a, card, 5, p.card);
        for (int i = 0; i < 3; i++) {
            float ly = card.top + (card.bottom - card.top) * (i + 0.5f) / 3;
            a.dot(card.left + 8, ly, 2.5f, p.accent, 0.5f);
            roundFill(a, D2D1::RectF(card.left + 15, ly - 1.5f, card.left + 15 + w * (0.30f - 0.05f * i), ly + 1.5f), 1.5f, p.text, 0.35f);
        }
    }

    // ---------------------------------------------------------------- settings
    float headerHeight() const override { return 64; }
    void settingsColumn(float W, float& x0, float& x1) const override {
        float cw = std::min(620.0f, W - 48);
        x0 = (W - cw) / 2;
        x1 = x0 + cw;
    }
    float rowHeight(float) const override { return 54; }
    float rowInset() const override { return 16; }

    void settingsBackground(App& a, float W, float H) override { a.fillRect(D2D1::RectF(0, 0, W, H), pal(a).base); }

    void settingsHeader(App& a, float W, bool scrolled) override {
        const Pal& p = pal(a);
        float hh = headerHeight();
        a.fillRect(D2D1::RectF(0, 0, W, hh), p.base, 0.97f);
        if (scrolled) a.line(0, hh - 0.5f, W, hh - 0.5f, p.sep, p.sepA * 1.4f, 1.0f);
        float x0, x1;
        settingsColumn(W, x0, x1);
        float bx = std::max(28.0f, x0 - 26);
        circleButton(a, H_BACK, G_BACK, bx, 32, 32, p.accent, p.text, 0.07f);
        a.text(L"Settings", D2D1::RectF(bx + 22, 14, x1, 50), a.fmt(22, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, F_UI_DISPLAY), p.text);
    }

    float sectionTitle(App& a, float x0, float x1, float y, const wchar_t* title) override {
        a.text(title, D2D1::RectF(x0 + 16, y, x1, y + 18), a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), pal(a).text2);
        return y + 26;
    }

    void group(App& a, D2D1_RECT_F r, int rows, float rowH) override {
        const Pal& p = pal(a);
        if (!p.dark) a.softShadow(r, 10, 0.045f, 5, 1.5f);
        roundFill(a, r, 10, p.card);
        hairlineBorder(a, r, 10, p.sep, p.borderA);
        for (int i = 1; i < rows; i++) a.line(r.left + 16, r.top + rowH * i, r.right, r.top + rowH * i, p.sep, p.sepA, 1.0f);
    }

    void rowText(App& a, float x0, float x1, float cy, const wchar_t* label, const wchar_t* desc) override {
        const Pal& p = pal(a);
        a.text(label, D2D1::RectF(x0, cy - 18, x1, cy + 1), a.fmt(14), p.text);
        a.text(desc, D2D1::RectF(x0, cy + 1, x1, cy + 17), a.fmt(12), p.text2);
    }

    void toggle(App& a, int id, bool on, float right, float cy) override {
        const Pal& p = pal(a);
        float w = 42, h = 24, x = right - w;
        D2D1_RECT_F r = D2D1::RectF(x, cy - h / 2, x + w, cy + h / 2);
        bool hov = a.hover_ == id;
        roundFill(a, r, h / 2, on ? (hov ? p.accentHover : p.accent) : p.knobOff);
        if (!on && hov) roundFill(a, r, h / 2, p.text, 0.06f);
        float kx = on ? x + w - h / 2 : x + h / 2;
        a.dot(kx, cy + 0.8f, 10.5f, 0x000000, 0.12f);
        a.dot(kx, cy, 10, 0xFFFFFF);
        a.addHit(id, D2D1::RectF(x - 6, cy - 16, x + w + 6, cy + 16));
    }

    void choices(App& a, const ChoiceList& items, int selected, float right, float cy) override {
        const Pal& p = pal(a);
        float segW = 58, h = 28;
        float x = right - segW * items.size() - 4;
        D2D1_RECT_F track = D2D1::RectF(x, cy - h / 2, right, cy + h / 2);
        roundFill(a, track, 8, p.fill);
        IDWriteTextFormat* f = a.fmt(12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        IDWriteTextFormat* fs = a.fmt(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        for (size_t i = 0; i < items.size(); i++) {
            D2D1_RECT_F s = D2D1::RectF(x + 2 + segW * i, track.top + 2, x + 2 + segW * (i + 1), track.bottom - 2);
            bool sel = items[i].first == selected;
            if (sel) {
                if (!p.dark) a.softShadow(s, 6, 0.10f, 2, 1);
                roundFill(a, s, 6, p.dark ? 0x636366 : 0xFFFFFF);
            } else if (a.hover_ == items[i].first) {
                roundFill(a, s, 6, p.text, 0.05f);
            }
            a.text(items[i].second, s, sel ? fs : f, p.text, sel ? 1.0f : 0.8f);
            a.addHit(items[i].first, s);
        }
    }

    void button(App& a, int id, const wchar_t* label, wchar_t, float right, float cy) override {
        const Pal& p = pal(a);
        IDWriteTextFormat* f = a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        float w = a.textWidth(label, f) + 32;
        D2D1_RECT_F b = D2D1::RectF(right - w, cy - 14, right, cy + 14);
        roundFill(a, b, 7, a.hover_ == id ? p.accentHover : p.accent);
        a.text(label, b, f, 0xFFFFFF);
        a.addHit(id, b);
    }

    D2D1_RECT_F textField(App& a, D2D1_RECT_F box, bool focused, bool hovered) override {
        const Pal& p = pal(a);
        if (focused) roundFill(a, D2D1::RectF(box.left - 3, box.top - 3, box.right + 3, box.bottom + 3), 9, p.accent, 0.30f);
        roundFill(a, box, 7, p.field);
        hairlineBorder(a, box, 7, focused ? p.accent : p.sep, focused ? 1.0f : (hovered ? 0.25f : 0.15f));
        return D2D1::RectF(box.left + 10, box.top + 1, box.right - 10, box.bottom - 1);
    }

    void link(App& a, int id, const std::wstring& s, float x, float cy, bool alignRight) override {
        const Pal& p = pal(a);
        IDWriteTextFormat* f = a.fmt(13);
        float w = a.textWidth(s, f);
        float x0 = alignRight ? x - w : x;
        D2D1_RECT_F r = D2D1::RectF(x0, cy - 10, x0 + w, cy + 10);
        bool hov = a.hover_ == id;
        a.text(s, r, f, hov ? p.accentHover : p.accent);
        if (hov) a.line(x0, cy + 8, x0 + w, cy + 8, p.accentHover, 1.0f, 1.0f);
        a.addHit(id, D2D1::RectF(r.left - 4, r.top - 4, r.right + 4, r.bottom + 4));
    }

    void footnote(App& a, const std::wstring& s, float x, float cy) override {
        a.text(s, D2D1::RectF(x + 14, cy - 10, x + 420, cy + 10), a.fmt(12), pal(a).text3);
    }

    void themeCard(App& a, D2D1_RECT_F pv, Theme* t, bool selected, bool hovered) override {
        const Pal& p = pal(a);
        if (selected) a.strokeRect(D2D1::RectF(pv.left - 4, pv.top - 4, pv.right + 4, pv.bottom + 4), p.accent, 1.0f, 13, 3.0f);
        else if (hovered) a.strokeRect(D2D1::RectF(pv.left - 4, pv.top - 4, pv.right + 4, pv.bottom + 4), p.text3, 0.6f, 13, 2.0f);
        ID2D1RoundedRectangleGeometry* g = nullptr;
        ID2D1Layer* layer = nullptr;
        a.d2d_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(pv, 9, 9), &g);
        a.rt_->CreateLayer(&layer);
        a.rt_->PushAxisAlignedClip(pv, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (g && layer) a.rt_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g), layer);
        t->drawPreview(a, pv);
        if (g && layer) a.rt_->PopLayer();
        a.rt_->PopAxisAlignedClip();
        SafeRelease(layer);
        SafeRelease(g);
        hairlineBorder(a, pv, 9, p.sep, 0.12f);
        a.text(t->name(), D2D1::RectF(pv.left, pv.bottom + 10, pv.right, pv.bottom + 30),
               a.fmt(13, selected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER),
               selected ? p.text : p.text2);
    }
};

Theme* makeDefaultTheme() {
    static DefaultTheme t;
    return &t;
}
