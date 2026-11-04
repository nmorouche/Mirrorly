#!/usr/bin/env python3
# Draws the "Acid poster" theme icon: the Default icon's screen and phone, in acid lime on a cobalt tile.
# Each size is drawn on its own (4x supersampled) so the lines stay crisp; small sizes get thicker lines
# and, at 24 px and below, a solid phone.
# Run from the repository root: python3 res/make_acid_icon.py  → res/mirrorly-acid.ico + .png
import io
import struct

from PIL import Image, ImageDraw

ACID = (0xD4, 0xFF, 0x1E)
COBALT = (0x2F, 0x3D, 0xFF)
K = 4  # supersampling
SIZES = [256, 128, 96, 64, 48, 40, 32, 24, 20, 16]


def icon(size):
    S = size * K
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=S * 0.22, fill=COBALT + (255,))
    lw = S * (0.06 if size > 32 else 0.085)
    # screen
    x0, y0, x1, y1 = S * 0.20, S * 0.25, S * 0.80, S * 0.67
    d.rounded_rectangle([x0, y0, x1, y1], radius=S * 0.05, outline=ACID, width=round(lw))
    # phone on the screen
    pw, ph = S * (0.11 if size > 32 else 0.13), S * 0.24
    cx, cy = S * 0.5, (y0 + y1) / 2
    box = [cx - pw / 2, cy - ph / 2, cx + pw / 2, cy + ph / 2]
    if size <= 24:
        d.rounded_rectangle(box, radius=S * 0.03, fill=ACID)
    else:
        d.rounded_rectangle(box, radius=S * 0.03, outline=ACID, width=round(lw * 0.7))
    # stand
    sw = S * 0.20
    d.rounded_rectangle([cx - sw / 2, S * 0.735, cx + sw / 2, S * 0.735 + lw * 0.9], radius=lw * 0.45, fill=ACID)
    return im.resize((size, size), Image.LANCZOS)


def write_ico(path, images):
    """ICO with one PNG-compressed entry per size (Windows Vista and later)."""
    blobs = []
    for im in images:
        b = io.BytesIO()
        im.save(b, "PNG", optimize=True)
        blobs.append(b.getvalue())
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for im, blob in zip(images, blobs):
        w = im.width if im.width < 256 else 0
        out += struct.pack("<BBBBHHII", w, w, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(out + b"".join(blobs))


if __name__ == "__main__":
    images = [icon(s) for s in SIZES]
    write_ico("res/mirrorly-acid.ico", images)
    images[0].save("res/mirrorly-acid.png", optimize=True)
