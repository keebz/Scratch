#!/usr/bin/env python3
"""mockup_text.py - add the labels to docs/mockup_notext.png with real fonts (system python with Pillow):
    python3 -I tools/mockup_text.py
The device uses its own fonts (Titillium Web / Roboto / Nexa); DejaVu stands in here, so widths differ a little."""
import json
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
FONTS = {"bold": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "regular": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}

img = Image.open(os.path.join(PROJ, "docs", "mockup_notext.png")).convert("RGB")
d = ImageDraw.Draw(img)
for text, x, y, w, h, font, style, size, colour, just in json.load(open(os.path.join(HERE, ".mockup_texts.json"))):
    f = ImageFont.truetype(FONTS["bold" if style in ("Bold", "Black", "SemiBold") else "regular"], int(size * (0.82 if font == "Nexa" else 0.88)))
    col = tuple(int(colour[i:i + 2], 16) for i in (2, 4, 6))
    l, t, r, b = d.textbbox((0, 0), text, font=f)
    tw, th = r - l, b - t
    tx = x + (w - tw) / 2 if "horizontallyCentred" in just else x
    ty = y + (h - th) / 2 - t if "verticallyCentred" in just else y - t
    d.text((tx - l, ty), text, font=f, fill=col)
img.save(os.path.join(PROJ, "docs", "mockup.png"))
print("wrote docs/mockup.png")
