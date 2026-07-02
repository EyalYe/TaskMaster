# Skill: 1-bit glyphs (`gen_glyphs.py`)

**Core-only.** This is how the OS's icons are made; app authors don't do this (they just pick a hint
token — see add-an-app.md). Lives in the **core repo**.

## The pipeline

`icons/*.{svg,png}` → `tools/gen_glyphs.py` (rasterize + **alpha-threshold** to hard 1-bit, no
anti-aliasing) → `components/taskmaster_core/ui/hint_glyphs.{c,h}` (LVGL A1 images, drawn recolored
white). The generated `.c` is committed, so the firmware build never needs the tooling.

Two glyph families use it: the **hint bar** (home, check, select, menu, reset, back) and the **status
bar** (wifi connected/not, weather: sun / cloud-day / cloud-night / rain / snow).

## Setup (one-time, needs a native cairo)

```bash
brew install cairo
python3 -m venv /tmp/glyphvenv
/tmp/glyphvenv/bin/pip install cairosvg pillow
```

## Add / change a glyph

1. Drop a **square, dark-on-transparent** icon in `icons/` (SVG or 48×48 PNG).
2. Add one line to the `GLYPHS` dict in `tools/gen_glyphs.py`: `name: (file, target_px, alpha_thresh)`.
   Hint-bar action glyphs are ~14 px (≈1 px padding in the 20 px boxes); status glyphs ~12–13 px.
3. Regenerate (prints an ASCII preview of each glyph so you can eyeball it before flashing):
   ```bash
   DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib /tmp/glyphvenv/bin/python tools/gen_glyphs.py
   ```
4. To make a **new hint token** render as that glyph, map it in `hint_action_glyph()` in
   `components/taskmaster_core/ui/ui_frame.c` (e.g. `"MNU" → &glyph_menu`). Text is always the fallback.
5. Rebuild + flash (from the template) and check on the panel. The owner iterates on size/threshold and
   pixel position — keep offsets named (`HINT_*` in `ui/hint_bar.h`).

## Aesthetic rules

- **No anti-aliasing / dithering.** Thresholding gives hard on/off pixels — that's the point. If a
  rasterized icon looks muddy at the target size, shrink the icon or raise the threshold, don't smooth.
- The hint bar is **three equal boxes, one per button** (Home / Encoder-push / Select). Encoder
  *rotation* is never hinted (it always scrolls). If `.click` == `.select`, the OS drops the middle box.
