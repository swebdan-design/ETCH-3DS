#!/usr/bin/env python3
"""
ETCH 3DS -- by Synth Centric
Turns the host-drawn screens (build/host/screen_*.ppm, from tests/host/
test_screens.c) into docs/images: one PNG per screen at 2x, and a contact
sheet, screens.png.

    tools/screens_png.py
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "build/host")
DST = os.path.join(ROOT, "docs/images")
NAMES = ["title", "live", "fill", "punch", "xy", "panel", "drum", "keys", "gen", "song", "file", "about1", "about3"]

os.makedirs(DST, exist_ok=True)
ims = []
for n in NAMES:
    im = Image.open(os.path.join(SRC, "screen_%s.ppm" % n)).convert("RGB")
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(DST, "screen_%s.png" % n))
    ims.append(im)
W, H = ims[0].size
cols, gap = 4, 12
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * W + (cols + 1) * gap, rows * H + (rows + 1) * gap), (24, 24, 24))
for i, im in enumerate(ims):
    sheet.paste(im, (gap + (i % cols) * (W + gap), gap + (i // cols) * (H + gap)))
sheet.save(os.path.join(DST, "screens.png"))
print("docs/images: %d screens + screens.png" % len(ims))
