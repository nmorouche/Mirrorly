#!/usr/bin/env python3
# Draws the installer artwork: 24-bit BMPs at the exact size the installer shows them (164x314 and
# 150x57), drawn 4x larger and scaled down for smooth edges.
# Run from the repository root: python3 res/installer/make_art.py
from PIL import Image, ImageDraw, ImageFont

TOP, BOTTOM = (98, 136, 255), (146, 112, 255)   # the app icon's gradient
FONT = "/usr/share/fonts/opentype/inter/InterDisplay-SemiBold.otf"
FONT_LIGHT = "/usr/share/fonts/opentype/inter/InterDisplay-Medium.otf"


def gradient(w, h):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            t = min(1.0, (0.35 * x / w + 0.65 * y / h))
            px[x, y] = tuple(round(a + (b - a) * t) for a, b in zip(TOP, BOTTOM))
    return im


def glyph(d, cx, cy, s, color=(255, 255, 255)):
    """The icon's monitor + phone, centered on (cx, cy), s = monitor width."""
    w, h = s, s * 0.62
    lw = max(2, round(s * 0.07))
    x0, y0 = cx - w / 2, cy - h / 2 - s * 0.08
    d.rounded_rectangle([x0, y0, x0 + w, y0 + h], radius=s * 0.08, outline=color, width=lw)
    pw, ph = s * 0.17, s * 0.34
    d.rounded_rectangle([cx - pw / 2, y0 + h / 2 - ph / 2, cx + pw / 2, y0 + h / 2 + ph / 2], radius=s * 0.04,
                        outline=color, width=max(2, round(lw * 0.7)))
    sw = s * 0.28
    d.rounded_rectangle([cx - sw / 2, y0 + h + s * 0.09, cx + sw / 2, y0 + h + s * 0.09 + lw], radius=lw / 2, fill=color)


K = 4  # supersampling


def sidebar():
    W, H = 164 * K, 314 * K
    im = gradient(W, H)
    d = ImageDraw.Draw(im)
    glyph(d, W / 2, 115 * K, 85 * K)
    f = ImageFont.truetype(FONT, 22 * K)
    d.text((W / 2, 197 * K), "Mirrorly", font=f, fill="white", anchor="mm")
    f2 = ImageFont.truetype(FONT_LIGHT, 11 * K)
    d.text((W / 2, 220 * K), "Your phone, on your PC", font=f2, fill=(232, 234, 255), anchor="mm")
    im.resize((164, 314), Image.LANCZOS).save("res/installer/sidebar.bmp")


def header():
    W, H = 150 * K, 57 * K
    im = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(im)
    S = 42 * K
    g = gradient(S, S)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=10 * K, fill=255)
    x0, y0 = W - S - 8 * K, (H - S) // 2
    im.paste(g, (x0, y0), mask)
    glyph(d, x0 + S / 2, y0 + S / 2 + 2 * K, 25 * K)
    im.resize((150, 57), Image.LANCZOS).save("res/installer/header.bmp")


sidebar()
header()
