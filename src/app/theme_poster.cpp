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

// Volume popover: [speaker = mute] [slider] [value], square and inked.
void volumePopover(App& a, D2D1_RECT_F btn, bool onVideo, float al, float W, float H, bool below) {
    const float pw = 264, ph = 46;
    float bcx = (btn.left + btn.right) / 2;
    // speaker right above / below the button; on the right edge, grow to the left
    float left = bcx - ph / 2;
    if (left + pw > W - 12) left = bcx + ph / 2 - pw;
    left = std::max(12.0f, left);
    float top = below ? btn.bottom + 8 : btn.top - 8 - ph;
    top = std::max(8.0f, std::min(H - 8 - ph, top));
    D2D1_RECT_F r = D2D1::RectF(left, top, left + pw, top + ph);
    uint32_t bg = onVideo ? INK : PAPER, fg = onVideo ? WHITE : INK;
    if (!onVideo) a.fillRect(D2D1::RectF(r.left + 4, r.top + 4, r.right + 4, r.bottom + 4), INK, al);  // hard offset shadow
    a.fillRect(r, bg, al);
    a.strokeRect(r, onVideo ? ACID : INK, al, 0, 1.5f);
    a.addHit(H_VOL_POPOVER, r);
    float cy = (r.top + r.bottom) / 2;
    bool silent = a.muted_ || a.settings_.volume == 0;
    float icx = r.left + ph / 2;
    D2D1_RECT_F ib = D2D1::RectF(icx - 15, cy - 15, icx + 15, cy + 15);
    bool hov = a.hover_ == H_VOL_ICON;
    if (hov) a.fillRect(ib, ACID, al);
    a.icon(silent ? G_MUTE : G_VOLUME, icx, cy, 14, hov ? INK : (a.muted_ ? ALERT : fg), al);
    a.addHit(H_VOL_ICON, ib);

    D2D1_RECT_F track = D2D1::RectF(r.left + ph + 4, cy - 3, r.right - 68, cy + 3);
    float f = a.muted_ ? 0.0f : a.settings_.volume / 100.0f;
    float kx = track.left + (track.right - track.left) * f;
    a.strokeRect(track, fg, 0.9f * al, 0, 1.0f);
    if (kx > track.left) a.fillRect(D2D1::RectF(track.left, track.top, kx, track.bottom), ACID, al);
    bool active = a.hover_ == H_VOL_SLIDER || a.volumeDrag_;
    float k = active ? 8.0f : 7.0f;
    D2D1_RECT_F knob = D2D1::RectF(kx - k, cy - k, kx + k, cy + k);
    a.fillRect(knob, active ? ACID : fg, al);
    a.strokeRect(knob, INK, al, 0, 1.5f);
    a.volumeSlider(track);

    std::wstring val = a.muted_ ? L"OFF" : std::to_wstring(a.settings_.volume);
    float vw = a.pixelTextWidth(val, 2.0f);
    a.pixelText(val, r.right - 16 - vw, cy - 7, 2.0f, a.muted_ ? ALERT : fg, al);
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

        // cable (USB) line under the status
        std::wstring action;
        bool attention = false;
        int actionHit = H_USB_ACTION;
        std::wstring wired = a.wiredHint(action, attention, actionHit);
        if (!wired.empty()) {
            float wy = sy + 34;
            IDWriteTextFormat* wf = a.fmt(14);
            a.fillRect(D2D1::RectF(bx, wy - 9, bx + 34, wy + 9), attention ? ALERT : INK);
            a.pixelText(L"USB", bx + 4, wy - 3.5f, 1.0f, attention ? INK : ACID);
            float tw = std::min(a.textWidth(wired, wf), W - bx - 200);
            a.text(wired, D2D1::RectF(bx + 44, wy - 11, bx + 44 + tw + 2, wy + 11), wf, INK);
            if (!action.empty()) {
                std::wstring label = action;
                for (auto& c : label) c = towupper(c);
                float lw = a.pixelTextWidth(label, 1.5f);
                D2D1_RECT_F br = D2D1::RectF(bx + 44 + tw + 14, wy - 12, bx + 44 + tw + 14 + lw + 20, wy + 12);
                bool hov = a.hover_ == actionHit;
                a.fillRect(br, hov ? COBALT : INK);
                a.pixelText(label, br.left + 10, wy - 5.25f, 1.5f, hov ? WHITE : ACID);
                a.addHit(actionHit, br);
            }
            sy = wy;
        }

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
        if (a.volumeOpen_) volumePopover(a, D2D1::RectF(cx, y - 18, cx + 36, y + 18), false, 1.0f, W, H, false);
    }

    // ---------------------------------------------------------------- key zones
    void drawKeyZone(App& a, D2D1_POINT_2F c, const std::wstring& label, int state, float al) override {
        std::wstring up = label;
        for (auto& ch : up) ch = towupper(ch);
        bool pixel = true;
        for (wchar_t ch : up)
            if (!(ch == L' ' || ch == L'+' || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'))) pixel = false;
        float px = state == 2 ? 1.5f : 2.0f;
        IDWriteTextFormat* f = a.fmt(13, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        float tw = pixel ? a.pixelTextWidth(up, px) : a.textWidth(label, f);
        float h = 34, w = std::max(h, tw + 18);
        D2D1_RECT_F r = D2D1::RectF(c.x - w / 2, c.y - h / 2, c.x + w / 2, c.y + h / 2);
        uint32_t fill = state == 3 ? ACID : state == 2 ? COBALT : state == 1 ? ACID : INK;
        uint32_t tc = state == 3 || state == 1 ? INK : state == 2 ? WHITE : ACID;
        if (state != 0) a.fillRect(D2D1::RectF(r.left + 3, r.top + 3, r.right + 3, r.bottom + 3), INK, al);
        a.fillRect(r, fill, (state == 0 ? 0.7f : 1.0f) * al);
        a.strokeRect(r, state == 0 ? ACID : INK, al, 0, 1.5f);
        if (state == 2 && fmod(a.animTime() * 2.0, 1.0) > 0.5) a.strokeRect(D2D1::RectF(r.left - 5, r.top - 5, r.right + 5, r.bottom + 5), ACID, al, 0, 2.0f);
        if (pixel) a.pixelText(up, c.x - tw / 2, c.y - 3.5f * px, px, tc, al);
        else a.text(label, r, f, tc, al);
    }

    void drawKeymapBar(App& a, float W, float H, D2D1_RECT_F video) override {
        (void)H;
        D2D1_RECT_F bar = D2D1::RectF(0, 0, W, 64);
        a.fillRect(bar, INK, 0.92f);
        a.line(0, 64, W, 64, ACID, 1.0f, 2.0f);
        a.addHit(H_KEYMAP_BAR, bar);
        a.pixelText(L"KEY ZONES", 22, 20, 3.0f, ACID);
        std::wstring hint = a.zones_.empty() ? L"Click the screen where a key should tap" : L"Click to add. Drag to move. Right-click to remove.";
        float x = 22 + a.pixelTextWidth(L"KEY ZONES", 3.0f) + 20;
        float bx = W - 18;
        auto btn = [&](int id, const std::wstring& t, bool primary) {
            float w = a.pixelTextWidth(t, 2.0f) + 24;
            D2D1_RECT_F r = D2D1::RectF(bx - w, 15, bx, 49);
            bool hov = a.hover_ == id;
            a.fillRect(r, primary ? (hov ? WHITE : ACID) : (hov ? COBALT : INK));
            if (!primary) a.strokeRect(r, ACID, 1.0f, 0, 1.5f);
            a.pixelText(t, r.left + 12, 25, 2.0f, primary ? INK : (hov ? WHITE : ACID));
            a.addHit(id, r);
            bx -= w + 10;
        };
        btn(H_KEYMAP_DONE, L"DONE", true);
        btn(H_KEYMAP_CLEAR, L"CLEAR", false);
        btn(H_KEYMAP_TOGGLE, a.keymapOn_ ? L"KEYS ON" : L"KEYS OFF", false);
        a.text(hint, D2D1::RectF(x, 0, bx - 10, 64), a.fmt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD), WHITE, 0.9f);
    }

    // ---------------------------------------------------------------- updates
    void drawUpdateBadge(App& a, float W, float H) override {
        bool failed = a.update_.state == UPD_FAILED;
        std::wstring label = failed ? L"UPDATE FAILED. RETRY" : L"NEW VERSION " + from_utf8(a.update_.version) + L". UPDATE";
        for (auto& ch : label) ch = towupper(ch);
        float lw = a.pixelTextWidth(label, 1.5f);
        float x = 26 + a.pixelTextWidth(L"MIRRORLY", 2.0f) + 18;
        D2D1_RECT_F r = D2D1::RectF(x, 20, x + lw + 22, 44);
        bool hov = a.hover_ == H_UPDATE;
        bool blink = !hov && !failed && fmod(a.animTime(), 1.6) > 1.2;
        a.fillRect(r, hov ? COBALT : (failed ? ALERT : INK));
        a.pixelText(label, r.left + 11, r.top + 6.75f, 1.5f, hov ? WHITE : (blink ? WHITE : ACID));
        a.addHit(H_UPDATE, r);
        (void)W;
        (void)H;
    }

    void drawUpdateDialog(App& a, float W, float H) override {
        a.brush_->SetColor(D2D1::ColorF(0, 0, 0, 0.55f));
        a.rt_->FillRectangle(D2D1::RectF(0, 0, W, H), a.brush_);
        int st = a.update_.state;
        bool failed = st == UPD_FAILED;
        std::wstring ver = from_utf8(a.update_.version);
        const float cw = std::min(460.0f, W - 40), ch = 236;
        D2D1_RECT_F c = D2D1::RectF(W / 2 - cw / 2, H / 2 - ch / 2, W / 2 + cw / 2, H / 2 + ch / 2);
        a.fillRect(D2D1::RectF(c.left + 8, c.top + 8, c.right + 8, c.bottom + 8), ACID);
        a.fillRect(c, PAPER);
        a.strokeRect(c, INK, 1.0f, 0, 2.0f);
        D2D1_RECT_F head = D2D1::RectF(c.left, c.top, c.right, c.top + 46);
        a.fillRect(head, failed ? ALERT : COBALT);
        std::wstring ht = failed ? L"UPDATE FAILED" : (st == UPD_DOWNLOADING ? L"DOWNLOADING" : (st == UPD_AVAILABLE ? L"NEW VERSION" : L"INSTALLING"));
        a.pixelText(ht, c.left + 18, c.top + 12, 3.0f, WHITE);
        float vw = a.pixelTextWidth(L"V" + ver, 2.0f);
        a.pixelText(L"V" + ver, c.right - 18 - vw, c.top + 16, 2.0f, ACID);
        float y = head.bottom + 34;
        IDWriteTextFormat* f = a.fmt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        if (st == UPD_DOWNLOADING) {
            D2D1_RECT_F tr = D2D1::RectF(c.left + 18, y - 9, c.right - 18, y + 9);
            a.strokeRect(tr, INK, 1.0f, 0, 1.5f);
            float fx = tr.left + (tr.right - tr.left) * std::max(0.0f, std::min(1.0f, a.update_.progress));
            if (fx > tr.left + 2) a.fillRect(D2D1::RectF(tr.left + 2, tr.top + 2, fx - 2, tr.bottom - 2), ACID);
            std::wstring pct = std::to_wstring((int)(a.update_.progress * 100)) + L"%";
            a.pixelText(pct, c.left + 18, y + 22, 2.0f, INK);
        } else if (st == UPD_READY || st == UPD_INSTALLING) {
            a.text(L"Mirrorly restarts in a moment.", D2D1::RectF(c.left + 18, y - 12, c.right - 18, y + 12), f, INK);
        } else if (failed) {
            a.text(from_utf8(a.update_.error), D2D1::RectF(c.left + 18, y - 14, c.right - 18, y + 30), f, INK);
        } else {
            a.text(std::wstring(L"You have V") + kVersion + L". Mirrorly downloads the new one and restarts by itself.",
                   D2D1::RectF(c.left + 18, y - 14, c.right - 18, y + 30), f, INK);
        }
        if (st == UPD_AVAILABLE || failed) {
            float by = c.bottom - 34;
            std::wstring go = failed ? L"TRY AGAIN" : L"UPDATE NOW", no = failed ? L"CLOSE" : L"LATER";
            float gw = a.pixelTextWidth(go, 2.0f) + 32, nw = a.pixelTextWidth(no, 2.0f) + 32;
            D2D1_RECT_F gr = D2D1::RectF(c.right - 18 - gw, by - 17, c.right - 18, by + 17);
            D2D1_RECT_F nr = D2D1::RectF(gr.left - 12 - nw, by - 17, gr.left - 12, by + 17);
            bool hg = a.hover_ == H_UPDATE_NOW, hn = a.hover_ == H_UPDATE_LATER;
            a.fillRect(gr, hg ? COBALT : INK);
            a.pixelText(go, gr.left + 16, by - 7, 2.0f, hg ? WHITE : ACID);
            if (hn) a.fillRect(nr, ACID);
            a.strokeRect(nr, INK, 1.0f, 0, 1.5f);
            a.pixelText(no, nr.left + 16, by - 7, 2.0f, INK);
            a.addHit(H_UPDATE_NOW, gr);
            a.addHit(H_UPDATE_LATER, nr);
        }
    }

    // ---------------------------------------------------------------- pairing over Wi-Fi
    void drawPairing(App& a, float W, float H) override {
        a.fillRect(D2D1::RectF(0, 0, W, H), PAPER);
        a.giantType(L"WIFI", -W * 0.03f, H * 0.38f, H * 0.62f, ACID, 0.9f);
        a.drawGrain(W, H, 0.55f);
        squareButton(a, H_PAIR_CLOSE, G_BACK, 38, 34, 36, 1.0f, false, false);
        a.pixelText(L"PAIR A PHONE", 72, 23.5f, 3.0f, INK);

        const float qrSide = 232;
        float bx = std::max(28.0f, W * 0.075f);
        bool wide = W >= 720;
        float qx = wide ? W - bx - qrSide : bx;
        float top = 104;
        // steps
        const wchar_t* titles[3] = {L"Same Wi-Fi as this PC", L"Developer options > Wireless debugging",
                                    L"Pair device with QR code. Scan from there, not the Camera app"};
        float y = top + 12;
        float tw = wide ? qx - bx - 40 : W - bx * 2;
        for (int i = 0; i < 3; i++) {
            a.fillRect(D2D1::RectF(bx, y - 13, bx + 26, y + 13), INK);
            a.pixelText(std::to_wstring(i + 1), bx + 13 - 4.5f, y - 5.25f, 1.5f, ACID);
            a.text(titles[i], D2D1::RectF(bx + 38, y - 14, bx + 38 + tw, y + 14), a.fmt(17, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK);
            y += 48;
        }
        // QR in an inked box with a hard shadow
        float qy = wide ? top : y + 6;
        D2D1_RECT_F q = D2D1::RectF(qx, qy, qx + qrSide, qy + qrSide);
        a.fillRect(D2D1::RectF(q.left + 6, q.top + 6, q.right + 6, q.bottom + 6), INK);
        a.fillRect(q, 0xFFFFFF);
        a.strokeRect(q, INK, 1.0f, 0, 2.0f);
        bool busy, error;
        std::wstring status = a.pairingText(busy, error);
        bool codeMode = !a.pair_.codeTarget.empty() &&
                        (a.pair_.phase == mn::PairingStatus::Waiting || a.pair_.phase == mn::PairingStatus::Pairing);
        if (codeMode) {  // "Pair device with pairing code": type the 6 digits the phone shows
            a.pixelText(L"CODE", q.left + 16, q.top + 18, 3.0f, INK);
            a.text(L"Shown on the phone", D2D1::RectF(q.left + 16, q.top + 48, q.right - 10, q.top + 68), a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK2);
            const float bw = 28, gap = 6;
            float bx0 = (q.left + q.right) / 2 - (6 * bw + 5 * gap) / 2, by = q.top + 100;
            bool caretOn = fmod(a.animTime(), 1.0) < 0.6;
            for (int i = 0; i < 6; i++) {
                D2D1_RECT_F b = D2D1::RectF(bx0 + i * (bw + gap), by, bx0 + i * (bw + gap) + bw, by + 40);
                bool cur = (int)a.pairTyped_.size() == i;
                if (cur) a.fillRect(b, ACID);
                a.strokeRect(b, INK, 1.0f, 0, cur ? 2.5f : 1.5f);
                if (i < (int)a.pairTyped_.size())
                    a.pixelText(std::wstring(1, (wchar_t)a.pairTyped_[i]), b.left + bw / 2 - 7.5f, by + 9.5f, 3.0f, INK);
                else if (cur && caretOn && a.pair_.phase == mn::PairingStatus::Waiting)
                    a.fillRect(D2D1::RectF(b.left + bw / 2 - 1, by + 10, b.left + bw / 2 + 1, by + 30), INK);
            }
            a.text(L"PHONE AT " + from_utf8(a.pair_.codeTarget), D2D1::RectF(q.left + 16, q.bottom - 40, q.right - 10, q.bottom - 18),
                   a.fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK3);
        } else if (a.pair_.phase == mn::PairingStatus::Waiting && !a.pair_.qr.empty()) {
            a.drawQr(a.pair_.qr, D2D1::RectF(q.left + 8, q.top + 8, q.right - 8, q.bottom - 8), INK, 0xFFFFFF);
        } else {
            a.drawHalftoneOrb((q.left + q.right) / 2, (q.top + q.bottom) / 2, qrSide * 0.3f, a.animTime() * 1.5,
                              busy ? 1.0f : 0.0f, error ? ALERT : COBALT, ACID);
        }
        // status
        float sy = std::max(y + 10, wide ? q.bottom + 40 : q.bottom + 40);
        if (wide) sy = std::max(y + 20, top + 3 * 48 + 30);
        bool blink = busy && fmod(a.animTime() * 2.0, 1.0) > 0.5;
        D2D1_RECT_F m = D2D1::RectF(bx, sy - 7, bx + 14, sy + 7);
        if (!blink) a.fillRect(m, error ? ALERT : busy ? COBALT : ACID);
        a.strokeRect(m, INK, 1.0f, 0, 1.5f);
        a.text(status, D2D1::RectF(bx + 24, sy - 14, (wide ? qx - 20 : W - 28), sy + 14), a.fmt(16, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK);
        if (error) {
            float lw = a.pixelTextWidth(L"TRY AGAIN", 2.0f);
            D2D1_RECT_F br = D2D1::RectF(bx, sy + 26, bx + lw + 28, sy + 58);
            bool hov = a.hover_ == H_PAIR_RETRY;
            a.fillRect(br, hov ? COBALT : INK);
            a.pixelText(L"TRY AGAIN", br.left + 14, br.top + 9, 2.0f, hov ? WHITE : ACID);
            a.addHit(H_PAIR_RETRY, br);
        }
        float ny = H - 44;
        if (a.pair_.phase == mn::PairingStatus::Waiting && mn::now_ms() - a.pairOpenedAt_ > 30000) {
            posterLink(a, H_FIREWALL_LINK, L"Nothing happens on the phone? Allow Mirrorly in the firewall", bx, ny - 30, false);
        } else if (a.pair_.phase == mn::PairingStatus::Waiting && !codeMode) {
            a.text(L"Can't scan? Pair device with pairing code on the phone instead.", D2D1::RectF(bx, ny - 42, W - 28, ny - 20),
                   a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK);
        }
        a.text(L"Pair once. After that the phone shows up whenever Wireless debugging is on. Android 11+.",
               D2D1::RectF(bx, ny, W - 28, H - 22), a.fmt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), INK2);
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
        if (a.controlling()) {
            bx -= 46;
            squareButton(a, H_KEYMAP, G_KEYBOARD, bx, 36, 38, al, false, true);
        }
        if (a.volumeOpen_) {  // the controls are at the top: open downwards
            float mx = W - 36 - 92;
            volumePopover(a, D2D1::RectF(mx - 19, 17, mx + 19, 55), true, al, W, H, true);
        }
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
        // a sample of the home screen, branded rather than showing anyone's PC name
        a.giantType(L"M", r.left - w * 0.04f, r.top - h * 0.28f, h * 0.95f, ACID);
        a.drawHalftoneOrb(r.left + w * 0.64f, r.top + h * 0.52f, h * 0.30f, 0.0, 0.0f, GUN, ACID);
        if (a.grain_) {
            a.grain_->SetOpacity(0.45f);
            a.rt_->FillRectangle(r, a.grain_);
        }
        // the cobalt name block, with pixel "lettering" that spells nothing
        {
            const float px = 2.5f, pad = px * 2.5f;
            const int cols = 17, rows = 7;  // three 5-wide glyphs + gaps, 7 rows high like the pixel font
            float bx = r.left + w * 0.09f, by = r.top + h * 0.40f;
            a.fillRect(D2D1::RectF(bx, by, bx + cols * px + pad * 2, by + rows * px + pad * 2), COBALT);
            static const char* glyphs[rows] = {"10111 11010 01110",
                                               "10001 01011 10011",
                                               "11101 11001 10101",
                                               "00101 10111 11001",
                                               "11101 10001 00111",
                                               "10001 11011 10101",
                                               "11111 01110 11101"};
            for (int y = 0; y < rows; y++)
                for (int x = 0; x < cols; x++)
                    if (glyphs[y][x] == '1')
                        a.fillRect(D2D1::RectF(bx + pad + x * px, by + pad + y * px, bx + pad + (x + 1) * px, by + pad + (y + 1) * px), WHITE);
        }
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
