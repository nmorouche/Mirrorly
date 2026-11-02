// Mirrorly theme: "Acid poster"
//
//  - concrete-grey paper with film grain
//  - giant acid-lime type bleeding off the canvas
//  - an electric cobalt block carrying pixel lettering (built-in 5x7 dot-matrix face)
//  - a gunmetal halftone object with lime cracks; it turns cobalt while a device connects
//  - small black technical labels, square controls
#include <algorithm>
#include <cmath>

#include "app.h"
#include "platform.h"

namespace {

const uint32_t PAPER = 0xC4C7BD;   // background: concrete grey with a green cast
const uint32_t PAPER2 = 0xB2B5AA;  // quiet fills on paper
const uint32_t INK = 0x000000;     // primary text
const uint32_t INK2 = 0x34372F;    // secondary text
const uint32_t INK3 = 0x666A5F;    // tertiary text
const uint32_t ACID = 0xD4FF1E;    // signal: giant type, ready, selected
const uint32_t COBALT = 0x2F3DFF;  // identity block, primary action, connecting
const uint32_t WHITE = 0xF5F6F0;   // text on cobalt / on video
const uint32_t GUN = 0x1F211E;     // halftone gunmetal
const uint32_t ALERT = 0xFF4A17;   // destructive, problems

// Square icon button. On paper: black outline, acid fill on hover. On video: white icon.
void squareButton(App& a, int id, wchar_t g, float cx, float cy, float size, float alpha, bool destructive, bool onVideo) {
    float r = size / 2;
    D2D1_RECT_F rc = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);
    bool hov = a.hover_ == id;
    uint32_t fg = onVideo ? WHITE : INK;
    if (hov) {
        a.fillRect(rc, destructive ? ALERT : ACID, alpha);
        fg = destructive ? WHITE : INK;
    } else if (!onVideo) {
        a.strokeRect(rc, INK, 0.9f * alpha);
    }
    a.icon(g, cx, cy, size * 0.40f, fg, alpha);
    a.addHit(id, rc);
}

void posterLink(App& a, int id, const std::wstring& s, float x, float y, bool alignRight) {
    IDWriteTextFormat* f = a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float w = a.textWidth(s, f);
    float x0 = alignRight ? x - w : x;
    D2D1_RECT_F r = D2D1::RectF(x0, y - 11, x0 + w, y + 11);
    bool hov = a.hover_ == id;
    uint32_t c = hov ? COBALT : INK;
    a.text(s, r, f, c);
    a.line(x0, y + 9, x0 + w, y + 9, c, 1.0f, hov ? 2.0f : 1.0f);
    a.addHit(id, D2D1::RectF(r.left - 4, r.top - 4, r.right + 4, r.bottom + 4));
}

std::wstring firstWord(const std::wstring& s, size_t minLen, size_t maxLen) {
    std::wstring w = s;
    size_t sp = w.find(L' ');
    if (sp != std::wstring::npos && sp >= minLen) w = w.substr(0, sp);
    if (w.size() > maxLen) w = w.substr(0, maxLen);
    return w;
}

}  // namespace

class PosterTheme : public Theme {
public:
    const wchar_t* id() const override { return L"poster"; }
    const wchar_t* name() const override { return L"Acid poster"; }
    int iconResource() const override { return 2; }

    bool darkTitleBar(App&) const override { return false; }
    uint32_t windowColor(App&) const override { return PAPER; }
    uint32_t editBackground(App&) const override { return PAPER; }
    uint32_t editText(App&) const override { return INK; }
    const wchar_t* uiFont(App& a, bool) const override { return a.bahnschrift_.c_str(); }

    // ---------------------------------------------------------------- home
    void drawHome(App& a, float W, float H) override {
        double t = a.animTime();
        bool busy = a.connecting_ || a.sessionActive_;
        std::wstring name = a.settings_.name;

        float gs = H * 0.70f;
        a.giantType(firstWord(name, 2, 6), -W * 0.035f, -gs * 0.30f, gs, ACID);

        float r = std::min(W * 0.24f, H * 0.30f);
        float ocx = W * 0.60f, ocy = H * 0.46f;
        a.drawCracks(ocx, ocy, r, W, H, ACID, 0.95f);
        a.drawHalftoneOrb(ocx, ocy, r, t, busy ? 1.0f : 0.0f, busy ? COBALT : GUN, ACID);
        a.drawGrain(W, H, 0.55f);

        a.pixelText(L"MIRRORLY", 26, 27, 2.0f, INK);
        squareButton(a, H_SETTINGS, G_SETTINGS, W - 40, 34, 36, 1.0f, false, false);

        float bx = std::max(28.0f, W * 0.075f);
        float px = std::max(4.0f, std::min(9.0f, H / 72.0f));
        D2D1_RECT_F blk = a.pixelBlock(name, bx, ocy - px * 9, std::min(W * 0.62f, W - bx - 28), px, COBALT, WHITE);

        float sy = blk.bottom + 30;
        std::wstring status;
        uint32_t mark = ACID;
        if (busy) {
            status = L"Connecting to " + a.clientName();
            mark = COBALT;
        } else if (!a.rx_.running()) {
            status = L"AirPlay is stopped. Restart Mirrorly.";
            mark = ALERT;
        } else if (!a.rx_.discoveryOk()) {
            status = L"Your iPhone can't see this PC. Allow Mirrorly in the firewall.";
            mark = ALERT;
        } else {
            status = L"Ready for your iPhone";
        }
        bool blink = busy && fmod(t * 2.0, 1.0) > 0.5;
        D2D1_RECT_F m = D2D1::RectF(bx, sy - 7, bx + 14, sy + 7);
        if (!blink) a.fillRect(m, mark);
        a.strokeRect(m, INK, 1.0f, 0, 1.5f);
        a.text(status, D2D1::RectF(bx + 24, sy - 14, W - 40, sy + 14), a.fmt(19, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK);

        float stY = std::max(sy + 54, H - 76);
        float x = bx;
        const std::wstring steps[3] = {L"Open Control Center", L"Tap Screen Mirroring", L"Choose " + name};
        IDWriteTextFormat* stf = a.fmt(14);
        for (int i = 0; i < 3; i++) {
            a.fillRect(D2D1::RectF(x, stY - 11, x + 22, stY + 11), INK);
            a.pixelText(std::to_wstring(i + 1), x + 11 - 4.5f, stY - 5.25f, 1.5f, ACID);
            float tw = a.textWidth(steps[i], stf);
            a.text(steps[i], D2D1::RectF(x + 30, stY - 12, x + 30 + tw + 2, stY + 12), stf, INK);
            x += 30 + tw + 26;
        }

        std::wstring ip = a.primaryIp();
        const wchar_t* resName[] = {L"720P", L"1080P", L"1440P", L"4K"};
        a.labelColumn({L"AIRPLAY RECEIVER", ip.empty() ? L"NO NETWORK" : ip, L"PORT " + std::to_wstring(a.rx_.port()), L"",
                       std::wstring(resName[std::max(0, std::min(3, a.settings_.resolution))]) + L" / " +
                           std::to_wstring(a.settings_.fps) + L" FPS",
                       a.settings_.audio ? L"SOUND ON" : L"SOUND OFF", L"", std::wstring(L"V") + kVersion},
                      W - 28, 66, INK);
        posterLink(a, H_FIREWALL_LINK, L"Not on your iPhone? Fix the firewall", W - 28, H - 30, true);
    }

    // ---------------------------------------------------------------- now playing
    void drawNowPlaying(App& a, float W, float H) override {
        double t = a.animTime();
        std::wstring title = a.title_.empty() ? L"Now playing" : a.title_;
        float gs = H * 0.55f;
        a.giantType(firstWord(title, 3, 8), W * 0.30f, H - gs * 0.92f, gs, ACID);
        a.drawGrain(W, H, 0.55f);

        a.pixelText(L"MIRRORLY", 26, 27, 2.0f, INK);
        squareButton(a, H_DISCONNECT, G_CLOSE, W - 40, 34, 36, 1.0f, true, false);
        squareButton(a, H_SETTINGS, G_SETTINGS, W - 86, 34, 36, 1.0f, false, false);

        float x0 = std::max(28.0f, W * 0.075f);
        float size = std::max(140.0f, std::min({300.0f, H - 230, W * 0.32f}));
        float top = std::max(84.0f, (H - size) / 2 - 10);
        D2D1_RECT_F art = D2D1::RectF(x0, top, x0 + size, top + size);
        a.fillRect(D2D1::RectF(art.left + 12, art.top + 12, art.right + 12, art.bottom + 12), ACID);
        if (a.artwork_) {
            a.rt_->DrawBitmap(a.artwork_, art, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            a.fillRect(art, COBALT);
            a.rt_->PushAxisAlignedClip(art, D2D1_ANTIALIAS_MODE_ALIASED);
            a.drawHalftoneOrb((art.left + art.right) / 2, (art.top + art.bottom) / 2, size * 0.30f, t, a.audio_.level() * 2.0f, WHITE, ACID);
            a.rt_->PopAxisAlignedClip();
        }
        a.strokeRect(art, INK, 1.0f, 0, 2.0f);

        float cx = art.right + 48;
        float cw = W - cx - 40;
        D2D1_RECT_F tb = a.pixelBlock(title, cx, top, cw, std::max(3.0f, std::min(6.0f, H / 110.0f)), COBALT, WHITE);
        float y = tb.bottom + 26;
        std::wstring sub = a.artist_;
        if (!a.album_.empty()) sub += (sub.empty() ? L"" : L", ") + a.album_;
        if (sub.empty()) sub = L"From " + a.clientName();
        a.text(sub, D2D1::RectF(cx, y - 12, cx + cw, y + 12), a.fmt(16, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK);
        y += 34;

        float bw = std::min(420.0f, cw);
        if (a.progDur_ > 0) {
            double pos = a.progPos_ + (a.reduceMotion_ ? 0 : (mn::now_ms() - a.progStamp_) / 1000.0);
            pos = std::min(pos, a.progDur_);
            float frac = (float)(pos / a.progDur_);
            a.line(cx, y, cx + bw, y, INK, 0.3f, 2.0f);
            a.line(cx, y, cx + bw * frac, y, INK, 1.0f, 3.0f);
            auto fmtTime = [](double s) {
                int v = (int)s;
                wchar_t b[16];
                swprintf(b, 16, L"%d:%02d", v / 60, v % 60);
                return std::wstring(b);
            };
            a.text(fmtTime(pos), D2D1::RectF(cx, y + 6, cx + 80, y + 24), a.fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK2);
            a.text(fmtTime(a.progDur_), D2D1::RectF(cx + bw - 80, y + 6, cx + bw, y + 24),
                   a.fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING), INK2);
            y += 46;
        } else {
            y += 10;
        }

        // live oscilloscope of what is playing
        float sh = 64;
        static float buf[512];
        a.audio_.scope(buf, 512);
        ID2D1PathGeometry* g = nullptr;
        a.d2d_->CreatePathGeometry(&g);
        if (g) {
            ID2D1GeometrySink* sk = nullptr;
            g->Open(&sk);
            float mid = y + sh / 2;
            sk->BeginFigure(D2D1::Point2F(cx, mid), D2D1_FIGURE_BEGIN_HOLLOW);
            for (int i = 1; i < 512; i++) {
                float v = std::max(-1.0f, std::min(1.0f, buf[i] * 1.6f));
                sk->AddLine(D2D1::Point2F(cx + bw * i / 511.0f, mid - v * sh / 2));
            }
            sk->EndFigure(D2D1_FIGURE_END_OPEN);
            sk->Close();
            sk->Release();
            a.brush_->SetColor(rgb(INK));
            a.rt_->DrawGeometry(g, a.brush_, 1.5f);
            g->Release();
        }
        y += sh + 22;
        squareButton(a, H_MUTE, a.muted_ ? G_MUTE : G_VOLUME, cx + 18, y, 36, 1.0f, false, false);
        std::wstring st = !a.settings_.audio ? L"Sound is off in Settings" : (a.muted_ ? L"Muted" : L"Playing on this PC");
        a.text(st, D2D1::RectF(cx + 46, y - 12, cx + cw, y + 12), a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), a.settings_.audio ? INK2 : ALERT);
    }

    // ---------------------------------------------------------------- mirroring
    void drawMirrorWaiting(App& a, float W, float H) override {
        float r = std::min(W, H) * 0.12f;
        a.drawHalftoneOrb(W / 2, H / 2 - 30, r, a.animTime() * 1.5, 1.0f, WHITE, ACID);
        a.text(L"Starting screen mirroring from " + a.clientName(), D2D1::RectF(0, H / 2 + r, W, H / 2 + r + 30),
               a.fmt(15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), WHITE, 0.85f);
    }

    void drawMirrorPaused(App& a, float W, float H, D2D1_RECT_F) override {
        a.brush_->SetColor(D2D1::ColorF(0, 0, 0, 0.6f));
        a.rt_->FillRectangle(D2D1::RectF(0, 0, W, H), a.brush_);
        float pw = a.pixelTextWidth(L"SCREEN LOCKED", 4) + 20;
        a.pixelBlock(L"SCREEN LOCKED", W / 2 - pw / 2, H / 2 - 30, W - 40, 4, COBALT, WHITE);
        a.text(L"Unlock " + a.clientName() + L" to carry on", D2D1::RectF(0, H / 2 + 22, W, H / 2 + 46),
               a.fmt(14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), WHITE, 0.85f);
    }

    void drawMirrorOverlay(App& a, float W, float H, float al, D2D1_RECT_F) override {
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2] = {{0.0f, D2D1::ColorF(0, 0, 0, 0.75f * al)}, {1.0f, D2D1::ColorF(0, 0, 0, 0)}};
        a.rt_->CreateGradientStopCollection(gs, 2, &stops);
        ID2D1LinearGradientBrush* lg = nullptr;
        if (stops) a.rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 110)), stops, &lg);
        if (lg) a.rt_->FillRectangle(D2D1::RectF(0, 0, W, 110), lg);
        SafeRelease(lg);
        SafeRelease(stops);

        D2D1_RECT_F b = a.pixelBlock(a.clientName(), 20, 18, std::min(380.0f, W - 260), 2.5f, COBALT, WHITE);
        a.text(a.videoInfo(), D2D1::RectF(b.right + 14, b.top, W - 220, b.bottom), a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), WHITE, 0.85f * al);

        float bx = W - 36;
        squareButton(a, H_DISCONNECT, G_CLOSE, bx, 36, 38, al, true, true);
        bx -= 46;
        squareButton(a, H_FULLSCREEN, a.fullscreen_ ? G_WINDOW : G_FULL, bx, 36, 38, al, false, true);
        bx -= 46;
        squareButton(a, H_MUTE, a.muted_ ? G_MUTE : G_VOLUME, bx, 36, 38, al, false, true);
        bx -= 46;
        squareButton(a, H_SETTINGS, G_SETTINGS, bx, 36, 38, al, false, true);
        (void)H;
    }

    void drawToast(App& a, float W, float H, const std::wstring& msg, float al) override {
        IDWriteTextFormat* f = a.fmt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        float tw = std::min(a.textWidth(msg, f) + 36, W - 56);
        D2D1_RECT_F r = D2D1::RectF(28, 84, 28 + tw, 120);  // top left: clear of the steps and labels
        a.fillRect(r, INK, al);
        a.fillRect(D2D1::RectF(r.left, r.top, r.left + 6, r.bottom), ACID, al);
        a.text(msg, D2D1::RectF(r.left + 20, r.top, r.right - 8, r.bottom), f, ACID, al);
    }

    void drawPreview(App& a, D2D1_RECT_F r) override {
        float w = r.right - r.left, h = r.bottom - r.top;
        a.fillRect(r, PAPER);
        a.giantType(L"NA", r.left - w * 0.04f, r.top - h * 0.28f, h * 0.95f, ACID);
        a.drawHalftoneOrb(r.left + w * 0.64f, r.top + h * 0.52f, h * 0.30f, 0.0, 0.0f, GUN, ACID);
        if (a.grain_) {
            a.grain_->SetOpacity(0.45f);
            a.rt_->FillRectangle(r, a.grain_);
        }
        a.pixelBlock(L"NASS", r.left + w * 0.09f, r.top + h * 0.40f, w * 0.6f, 2.5f, COBALT, WHITE);
        a.fillRect(D2D1::RectF(r.left + w * 0.09f, r.top + h * 0.75f, r.left + w * 0.09f + 6, r.top + h * 0.75f + 6), ACID);
        a.line(r.left + w * 0.09f + 11, r.top + h * 0.75f + 3, r.left + w * 0.5f, r.top + h * 0.75f + 3, INK, 0.8f, 2.0f);
    }

    // ---------------------------------------------------------------- settings
    void settingsColumn(float W, float& x0, float& x1) const override {
        x0 = 72;
        x1 = std::min(W - 40, x0 + 720);
    }
    float rowHeight(float H) const override { return std::max(54.0f, std::min(64.0f, (H - 250) / 6.0f)); }
    float rowInset() const override { return 0; }

    void settingsBackground(App& a, float W, float H) override { a.drawGrain(W, H, 0.55f); }

    void settingsHeader(App& a, float W, bool scrolled) override {
        float hh = headerHeight();
        a.fillRect(D2D1::RectF(0, 0, W, hh), PAPER);
        a.drawGrain(W, hh, 0.55f);
        if (scrolled) a.line(0, hh, W, hh, INK, 1.0f, 1.5f);
        squareButton(a, H_BACK, G_BACK, 38, 34, 36, 1.0f, false, false);
        a.pixelText(L"SETTINGS", 72, 23.5f, 3.0f, INK);
    }

    float sectionTitle(App& a, float x0, float x1, float y, const wchar_t* title) override {
        y += 4;
        a.text(title, D2D1::RectF(x0, y, x1, y + 22), a.fmt(13, DWRITE_FONT_WEIGHT_BOLD), INK);
        y += 26;
        a.line(x0, y, x1, y, INK, 1.0f, 1.5f);
        return y;
    }

    void group(App& a, D2D1_RECT_F r, int rows, float rowH) override {
        for (int i = 1; i < rows; i++) a.line(r.left, r.top + rowH * i, r.right, r.top + rowH * i, INK, 0.25f, 1.0f);
    }

    void rowText(App& a, float x0, float x1, float cy, const wchar_t* label, const wchar_t* desc) override {
        a.text(label, D2D1::RectF(x0, cy - 19, x1, cy + 1), a.fmt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK);
        a.text(desc, D2D1::RectF(x0, cy + 1, x1, cy + 19), a.fmt(13), INK2);
    }

    void toggle(App& a, int id, bool on, float right, float y) override {
        float w = 48, h = 24, x = right - w;
        D2D1_RECT_F r = D2D1::RectF(x, y - h / 2, x + w, y + h / 2);
        if (on) a.fillRect(r, ACID);
        else if (a.hover_ == id) a.fillRect(r, PAPER2);
        a.strokeRect(r, INK, 1.0f, 0, 1.5f);
        float k = 16;
        float kx = on ? x + w - 4 - k : x + 4;
        a.fillRect(D2D1::RectF(kx, y - k / 2, kx + k, y + k / 2), on ? INK : INK3);
        a.addHit(id, D2D1::RectF(x - 6, y - 18, x + w + 6, y + 18));
    }

    void choices(App& a, const ChoiceList& items, int selected, float right, float cy) override {
        IDWriteTextFormat* f = a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        float segW = 62, h = 30;
        float x = right - segW * items.size();
        for (size_t i = 0; i < items.size(); i++) {
            D2D1_RECT_F r = D2D1::RectF(x + segW * i, cy - h / 2, x + segW * (i + 1), cy + h / 2);
            bool sel = items[i].first == selected;
            if (sel) a.fillRect(r, ACID);
            else if (a.hover_ == items[i].first) a.fillRect(r, PAPER2);
            a.text(items[i].second, r, f, INK, sel ? 1.0f : 0.75f);
            a.addHit(items[i].first, r);
        }
        a.strokeRect(D2D1::RectF(x, cy - h / 2, right, cy + h / 2), INK, 1.0f, 0, 1.5f);
        for (size_t i = 1; i < items.size(); i++) a.line(x + segW * i, cy - h / 2, x + segW * i, cy + h / 2, INK, 1.0f, 1.0f);
    }

    void button(App& a, int id, const wchar_t* label, wchar_t glyph, float right, float cy) override {
        IDWriteTextFormat* bf = a.fmt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F b = D2D1::RectF(right - 120, cy - 17, right, cy + 17);
        a.fillRect(b, a.hover_ == id ? INK : COBALT);
        a.icon(glyph, b.left + 20, cy, 13, WHITE);
        a.text(label, D2D1::RectF(b.left + 28, b.top, b.right, b.bottom), bf, WHITE);
        a.addHit(id, b);
    }

    D2D1_RECT_F textField(App& a, D2D1_RECT_F box, bool focused, bool hovered) override {
        if (hovered && !focused) a.fillRect(box, PAPER2);
        a.line(box.left, box.bottom, box.right, box.bottom, focused ? COBALT : INK, 1.0f, focused ? 3.0f : 1.5f);
        return D2D1::RectF(box.left + 2, box.top, box.right - 2, box.bottom - 3);
    }

    void link(App& a, int id, const std::wstring& s, float x, float cy, bool alignRight) override { posterLink(a, id, s, x, cy, alignRight); }

    void footnote(App& a, const std::wstring& s, float x, float cy) override {
        a.text(s, D2D1::RectF(x, cy - 11, x + 420, cy + 11), a.fmt(12), INK3);
    }

    void themeCard(App& a, D2D1_RECT_F pv, Theme* t, bool selected, bool hovered) override {
        if (selected) a.fillRect(D2D1::RectF(pv.left + 7, pv.top + 7, pv.right + 7, pv.bottom + 7), ACID);
        a.rt_->PushAxisAlignedClip(pv, D2D1_ANTIALIAS_MODE_ALIASED);
        t->drawPreview(a, pv);
        a.rt_->PopAxisAlignedClip();
        a.strokeRect(pv, INK, 1.0f, 0, selected || hovered ? 2.5f : 1.5f);
        std::wstring label = t->name();
        a.text(label, D2D1::RectF(pv.left, pv.bottom + 10, pv.right, pv.bottom + 30), a.fmt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK,
               selected ? 1.0f : 0.7f);
    }
};

Theme* makePosterTheme() {
    static PosterTheme t;
    return &t;
}
