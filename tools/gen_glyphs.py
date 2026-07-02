#!/usr/bin/env python3
"""gen_glyphs.py — rasterize icons/*.svg into 1-bit LVGL glyphs for the hint bar.

The `icons/` SVGs are vector line-art; this renders each at a small target size and
thresholds the alpha to 1-bit (no anti-aliasing — crisp on the mono OLED). Emits
components/taskmaster_core/ui/hint_glyphs.{c,h} as LVGL A1 images (drawn recolored
white) and prints an ASCII preview of each glyph.

Needs cairosvg + Pillow + a native cairo (brew install cairo). Run:
  python3 -m venv /tmp/glyphvenv && /tmp/glyphvenv/bin/pip install cairosvg pillow
  DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib /tmp/glyphvenv/bin/python tools/gen_glyphs.py
The generated .c is committed, so the firmware build never needs these deps.
"""
import io
import os
import cairosvg
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# name -> (file under icons/ (.svg or .png), target square px, alpha threshold 0..255).
# Sizes fit the hint cells: Home box ~16 px tall, encoder rotate cell ~12 px.
_WX = "--Streamline-Sharp-Streamline-Material.png"
GLYPHS = {
    "home":          ("home.svg",          14, 80),
    "scroll":        ("scroll.svg",        12, 80),
    "connected":     ("connected.svg",     12, 80),   # Wi-Fi arcs (status bar)
    "not_connected": ("not_connected.svg", 12, 80),   # Wi-Fi with a slash
    # Weather (status bar), downscaled from 48px PNGs → thresholded 1-bit.
    "wx_sun":         ("Sunny" + _WX,             13, 120),
    "wx_cloud_day":   ("Partly-Cloudy-Day" + _WX, 13, 120),
    "wx_cloud_night": ("Partly-Cloudy-Night" + _WX, 13, 120),
    "wx_rain":        ("Rainy" + _WX,             13, 120),
    "wx_snow":        ("Snow-Flake--Streamline-Micro.png", 13, 120),
    # Hint-bar action glyphs (click / Select cells).
    "check":          ("Check--Streamline-Ultimate.png",      11, 120),  # DON (complete)
    "select":         ("Select-All--Streamline-Ultimate.png", 11, 120),  # OPN / SEL
    "menu":           ("Navigation-Menu-1--Streamline-Ultimate.png", 11, 120),      # MNU
    "reset":          ("Triangle-Arrow-Rotate-Left-4--Streamline-Flex.png", 11, 120),  # RST
}


def rasterize(src, px, thresh):
    if src.lower().endswith(".svg"):
        png = cairosvg.svg2png(url=os.path.join(ROOT, "icons", src),
                               output_width=px, output_height=px)
        im = Image.open(io.BytesIO(png)).convert("RGBA")
    else:  # a raster (PNG) — load + downscale to the target
        im = Image.open(os.path.join(ROOT, "icons", src)).convert("RGBA")
        im = im.resize((px, px), Image.LANCZOS)
    a = im.split()[3]                       # alpha marks where the shape is drawn
    rows = []
    for y in range(im.height):
        rows.append("".join("X" if a.getpixel((x, y)) >= thresh else "."
                            for x in range(im.width)))
    return rows


def pack(rows):
    h = len(rows)
    w = max(len(r) for r in rows)
    stride = (w + 7) // 8
    data = []
    for r in rows:
        r = r.ljust(w, ".")
        rb = [0] * stride
        for i, ch in enumerate(r):
            if ch == "X":
                rb[i // 8] |= 0x80 >> (i % 8)
        data += rb
    return w, h, stride, data


HDR = ("/* hint_glyphs.h — 1-bit hint-bar glyphs, rasterized from the icons/ SVGs by\n"
       " * tools/gen_glyphs.py (do not edit by hand). LVGL A1 images; drawn recolored white. */\n"
       "#pragma once\n#include \"lvgl.h\"\n\n")

SRC = ("/* hint_glyphs.c — GENERATED from the icons/ SVGs by tools/gen_glyphs.py. Do not edit. */\n"
       "#include \"hint_glyphs.h\"\n\n")


def main():
    outdir = os.path.join(ROOT, "components", "taskmaster_core", "ui")
    h, c = HDR, SRC
    for name, (svg, px, thresh) in GLYPHS.items():
        rows = rasterize(svg, px, thresh)
        w, ht, stride, data = pack(rows)
        print("=== %s (%s @ %dpx, %dx%d) ===" % (name, svg, px, w, ht))
        for r in rows:
            print("  " + r.replace(".", " ").replace("X", "#"))
        arr = ", ".join("0x%02X" % b for b in data)
        c += "static const uint8_t %s_map[] = { %s };\n" % (name, arr)
        c += ("const lv_image_dsc_t glyph_%s = {\n"
              "    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A1,\n"
              "                .w = %d, .h = %d, .stride = %d },\n"
              "    .data_size = sizeof(%s_map),\n    .data = %s_map,\n};\n\n"
              % (name, w, ht, stride, name, name))
        h += "extern const lv_image_dsc_t glyph_%s;\n" % name
    with open(os.path.join(outdir, "hint_glyphs.h"), "w") as f:
        f.write(h)
    with open(os.path.join(outdir, "hint_glyphs.c"), "w") as f:
        f.write(c)
    print("wrote hint_glyphs.{c,h}")


if __name__ == "__main__":
    main()
