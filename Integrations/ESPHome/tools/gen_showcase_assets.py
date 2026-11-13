#!/usr/bin/env python3
"""Generates the pre-rendered images for the TAB-1 showcase firmware.

  images/showcase_wall.png      1280x800 wallpaper for the Clock page with the
                                frosted "glass" cards painted in, dithered so
                                every pixel is already an exact RGB565 colour.
  images/showcase_d0..d9.png    clock digits, 8-bit coverage (white on black)
  images/showcase_dc.png        clock colon

The card rectangles are read from showcase.h (the SC_GLASS_RECTS block), so the
painted cards and the on-screen widgets always line up.

Usage:  python3 tools/gen_showcase_assets.py [--host-dir DIR] [--font TTF]
  --font TTF      font for the clock digits (default: the Montserrat Medium that
                  comes with LVGL in the ESPHome build directory)
  --host-dir DIR  also write raw files for the host test harness:
                  showcase_wall.rgb565 (little-endian), showcase_d*.a8 and
                  showcase_assets.txt (name width height per line)
Needs only Pillow.
"""
import argparse
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

W, H = 1280, 800
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DIGIT_PX = 240          # font size of the clock digits
# Montserrat Medium ships with the LVGL sources that ESPHome fetches into the
# build directory. ESPHome empties that directory on a clean rebuild, so pass
# --font with another copy if it is not there.
DEFAULT_FONT = os.path.join(
    ROOT, ".esphome/build/apollo-tab-1/managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf")
# 8x8 Bayer matrix, values 0..63
BAYER = [
    [0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21],
]


def glass_rects():
    """(radius, [(x, y, w, h), ...]) from the SC_GLASS_RECTS block in showcase.h."""
    src = open(os.path.join(ROOT, "showcase.h"), encoding="utf-8").read()
    m = re.search(r"SC_GLASS_RECTS_BEGIN(.*?)SC_GLASS_RECTS_END", src, re.S)
    if not m:
        sys.exit("showcase.h: SC_GLASS_RECTS block not found")
    block = m.group(1)
    radius = int(re.search(r"#define\s+SC_GLASS_RADIUS\s+(\d+)", block).group(1))
    rects = [tuple(int(v) for v in r) for r in
             re.findall(r"X\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", block)]
    if not rects:
        sys.exit("showcase.h: no X(x, y, w, h) entries in SC_GLASS_RECTS")
    return radius, rects


def linear_gradient(c0, c1):
    """135-degree gradient: c0 at the top-left corner, c1 at the bottom-right."""
    ramp = Image.new("RGB", (256, 1))
    ramp.putdata([tuple(round(c0[i] + (c1[i] - c0[i]) * t / 255) for i in range(3)) for t in range(256)])
    # A 2x2 seed scaled bilinearly gives t = (x/W + y/H) / 2 without banding in the mask.
    seed = Image.new("L", (2, 2))
    seed.putdata([0, 128, 128, 255])
    t = seed.resize((W, H), Image.BILINEAR, box=(0.5, 0.5, 1.5, 1.5))
    r, g, b = (t.point(list(ramp.getchannel(i).getdata())) for i in range(3))
    return Image.merge("RGB", (r, g, b))


def glow(img, cx, cy, radius, color, alpha):
    """Adds a radial glow whose opacity falls linearly from alpha to 0 at radius."""
    size = 256
    spot = Image.new("L", (size, size), 0)
    px = spot.load()
    for y in range(size):
        for x in range(size):
            d = ((x - size / 2 + 0.5) ** 2 + (y - size / 2 + 0.5) ** 2) ** 0.5 / (size / 2)
            if d < 1:
                px[x, y] = round(255 * alpha * (1 - d))
    spot = spot.resize((radius * 2, radius * 2), Image.BICUBIC)
    mask = Image.new("L", (W, H), 0)
    mask.paste(spot, (cx - radius, cy - radius))
    img.paste(Image.new("RGB", (W, H), color), (0, 0), mask)


def disc(img, cx, cy, radius, color, alpha, soft):
    """Adds a soft-edged translucent disc (the shapes the frosted cards blur)."""
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=round(255 * alpha))
    mask = mask.filter(ImageFilter.GaussianBlur(soft))
    img.paste(Image.new("RGB", (W, H), color), (0, 0), mask)


def rounded_mask(rects, radius, grow=0):
    """Anti-aliased mask (4x supersampled) covering the rounded card rects."""
    s = 4
    big = Image.new("L", (W * s, H * s), 0)
    d = ImageDraw.Draw(big)
    for x, y, w, h in rects:
        d.rounded_rectangle(((x - grow) * s, (y - grow) * s, (x + w + grow) * s - 1, (y + h + grow) * s - 1),
                            radius=(radius + grow) * s, fill=255)
    return big.resize((W, H), Image.LANCZOS)


def wallpaper(radius, rects):
    img = linear_gradient((0x07, 0x3B, 0x4C), (0x31, 0x2E, 0x81))
    glow(img, 256, 120, 442, (255, 255, 255), 0.10)
    glow(img, 1024, 640, 483, (61, 218, 194), 0.16)
    for cx, cy, r, color, alpha in (
            (980, 220, 150, (0x3D, 0xDA, 0xC2), 0.14), (300, 560, 190, (0x7C, 0x5C, 0xD6), 0.22),
            (640, 330, 120, (0xFF, 0xD4, 0x8A), 0.08), (1130, 690, 130, (0x6C, 0xB4, 0xFF), 0.14),
            (120, 180, 90, (0xFF, 0xFF, 0xFF), 0.06), (760, 720, 100, (0xFF, 0x9F, 0x5A), 0.10)):
        disc(img, cx, cy, r, color, alpha, 10)
    # What the cards show: the same picture heavily blurred, darkened so white
    # text keeps its contrast everywhere, then given a faint white veil.
    frosted = img.filter(ImageFilter.GaussianBlur(28))
    frosted = Image.blend(frosted, Image.new("RGB", (W, H), (6, 14, 34)), 0.20)
    frosted = Image.blend(frosted, Image.new("RGB", (W, H), (255, 255, 255)), 0.07)
    cards = rounded_mask(rects, radius)
    # Soft shadow under the cards, painted before the cards themselves.
    shadow = Image.new("L", (W, H), 0)
    shadow.paste(cards.point(lambda v: v * 60 // 255), (0, 8))
    shadow = shadow.filter(ImageFilter.GaussianBlur(16))
    img.paste(Image.new("RGB", (W, H), (0, 0, 0)), (0, 0), shadow)
    # 1 px lighter rim: paint the rim colour on the full card shape, then the
    # frosted fill on the shape shrunk by 1 px.
    rim = Image.blend(frosted, Image.new("RGB", (W, H), (255, 255, 255)), 0.22)
    img.paste(rim, (0, 0), cards)
    inner = rounded_mask([(x + 1, y + 1, w - 2, h - 2) for x, y, w, h in rects], radius - 1)
    img.paste(frosted, (0, 0), inner)
    return img


def dither_rgb565(img):
    """Ordered-dithers to 5/6/5 bits and expands back to 8 bits by bit replication,
    so ESPHome's plain truncation to RGB565 returns exactly these colours."""
    out = []
    for ch, bits in zip(img.split(), (5, 6, 5)):
        step = 1 << (8 - bits)
        cell = Image.new("L", (8, 8))
        cell.putdata([BAYER[y][x] * step // 64 for y in range(8) for x in range(8)])
        thresh = Image.new("L", (W, H))
        for y in range(0, H, 8):
            for x in range(0, W, 8):
                thresh.paste(cell, (x, y))
        ch = ImageChops.add(ch, thresh)      # clips at 255
        keep = 0xFF & ~(step - 1)
        out.append(ch.point(lambda v, keep=keep, bits=bits: (v & keep) | (v >> bits)))
    return Image.merge("RGB", out)


def to_rgb565_le(img):
    r, g, b = (list(c.getdata()) for c in img.split())
    buf = bytearray(W * H * 2)
    for i in range(W * H):
        v = (r[i] >> 3) << 11 | (g[i] >> 2) << 5 | (b[i] >> 3)
        buf[2 * i] = v & 0xFF
        buf[2 * i + 1] = v >> 8
    return bytes(buf)


def digits(font_path):
    """{'0'..'9', ':'} -> 'L' images. Digits share one cell size; the colon is narrower."""
    font = ImageFont.truetype(font_path, DIGIT_PX)
    boxes = {ch: font.getbbox(ch) for ch in "0123456789:"}
    top = min(boxes[ch][1] for ch in "0123456789")
    bottom = max(boxes[ch][3] for ch in "0123456789")
    cell_h = bottom - top + 4
    out = {}
    for ch in "0123456789:":
        adv = round(font.getlength(ch))
        cell_w = max(round(font.getlength(d)) for d in "0123456789") if ch != ":" else adv
        im = Image.new("L", (cell_w, cell_h), 0)
        ImageDraw.Draw(im).text(((cell_w - adv) // 2, 2 - top), ch, font=font, fill=255)
        out[ch] = im
    return out


SECONDARY = (0xDC, 0xE3, 0xF0)   # the Clock page's secondary text colour


def rel_lum(rgb):
    def lin(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * lin(rgb[0]) + 0.7152 * lin(rgb[1]) + 0.0722 * lin(rgb[2])


def contrast(a, b):
    la, lb = sorted((rel_lum(a), rel_lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host-dir")
    ap.add_argument("--font")
    args = ap.parse_args()
    font_path = args.font or DEFAULT_FONT
    if not os.path.exists(font_path):
        sys.exit("clock digit font not found: %s\npass --font /path/to/Montserrat-Medium.ttf" % font_path)
    images = os.path.join(ROOT, "images")
    os.makedirs(images, exist_ok=True)
    radius, rects = glass_rects()
    wall = dither_rgb565(wallpaper(radius, rects))
    wall.save(os.path.join(images, "showcase_wall.png"), optimize=True)
    manifest = ["showcase_wall %d %d" % (W, H)]
    glyphs = digits(font_path)
    for ch, im in glyphs.items():
        name = "showcase_d%s" % ("c" if ch == ":" else ch)
        im.save(os.path.join(images, name + ".png"), optimize=True)
        manifest.append("%s %d %d" % (name, im.width, im.height))
    if args.host_dir:
        os.makedirs(args.host_dir, exist_ok=True)
        open(os.path.join(args.host_dir, "showcase_wall.rgb565"), "wb").write(to_rgb565_le(wall))
        for ch, im in glyphs.items():
            name = "showcase_d%s" % ("c" if ch == ":" else ch)
            open(os.path.join(args.host_dir, name + ".a8"), "wb").write(im.tobytes())
        open(os.path.join(args.host_dir, "showcase_assets.txt"), "w").write("\n".join(manifest) + "\n")
    # Report the worst text contrast in the header strip and inside each card
    # (inset by the 16 px padding, where text can be).
    for x, y, w, h in [(24, 12, 1232, 56)] + rects:
        crop = wall.crop((x + 16, y + 16, x + w - 16, y + h - 16))
        worst = max(crop.getdata(), key=rel_lum)
        print("area %4d,%3d %4dx%3d  lightest rgb %-15s  white text %.1f:1  secondary text %.1f:1"
              % (x, y, w, h, worst, contrast((255, 255, 255), worst), contrast(SECONDARY, worst)))
    print("font: %s" % font_path)
    print("\n".join(manifest))


if __name__ == "__main__":
    main()
