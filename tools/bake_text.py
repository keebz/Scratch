#!/usr/bin/env python3
"""bake_text.py - draw the static text into the skin images (system python with Pillow), after make_skin.py:
    python3 -I tools/bake_text.py
Text drawn as a Label widget over a button did not show on the device (and may swallow the button's touches), so the
text is part of the pictures: the background carries the titles, mixer and knob names; every button / pad has its own
on/off image with its caption. The device fonts cannot be used here; DejaVu is used for the lettering."""
import json
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SKINS = os.path.join(os.path.dirname(HERE), "skins")
FONTS = {"bold": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "regular": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}
data = json.load(open(os.path.join(HERE, ".bake.json")))
buttons = {name: rect for name, rect in data["buttons"]}
strips = {name: (rect, frames) for name, rect, frames in data.get("strips", [])}


def draw_text(img, t, ox=0, oy=0):
    text, x, y, w, h, font, style, size, colour, just = t
    f = ImageFont.truetype(FONTS["bold" if style in ("Bold", "Black", "SemiBold") else "regular"], int(size * (0.82 if font == "Nexa" else 0.88)))
    col = tuple(int(colour[i:i + 2], 16) for i in (2, 4, 6)) + (255,)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    l, tp, r, b = d.textbbox((0, 0), text, font=f)
    tw, th = r - l, b - tp
    X, Y, W, H = x - ox, y - oy, w, h
    tx = X + (W - tw) / 2 if "horizontallyCentred" in just else X
    ty = Y + (H - th) / 2 - tp if "verticallyCentred" in just else Y - tp
    d.text((tx - l, ty), text, font=f, fill=col)
    img.alpha_composite(layer)


def inside(t, rect):
    cx, cy = t[1] + t[3] / 2, t[2] + t[4] / 2
    return rect[0] <= cx < rect[0] + rect[2] and rect[1] <= cy < rect[1] + rect[3]


bg = Image.open(os.path.join(SKINS, "bg.png")).convert("RGBA")
per_button = {name: [] for name in buttons}
per_strip = {}
for t in data["texts"]:
    owner = next((n for n, r in buttons.items() if inside(t, r)), None)
    sowner = next((n for n, (r, f) in strips.items() if inside(t, r)), None)
    if owner: per_button[owner].append(t)
    elif sowner: per_strip.setdefault(sowner, []).append(t)
    else: draw_text(bg, t)
bg.save(os.path.join(SKINS, "bg.png"))

BASE = {"preview": "btn", "arm": "btn", "tob": "btn", "stopb": "btn", "s33": "speed", "s45": "speed", "stop": "speed"}
for name, rect in buttons.items():
    base = BASE[name]
    for state in ("on", "off"):
        img = Image.open(os.path.join(SKINS, "%s_%s.png" % (base, state))).convert("RGBA")
        for t in per_button[name]:
            draw_text(img, t, rect[0], rect[1])
        img.save(os.path.join(SKINS, "%s_%s.png" % (name, state)))
print("baked text into bg.png and %d buttons" % len(buttons))

# film strips with their own lettering (the pad slots: the number is part of every frame)
for name, (rect, frames) in strips.items():
    base = Image.open(os.path.join(SKINS, "slot_strip.png")).convert("RGBA")
    h = rect[3]
    for f in range(frames):
        frame = base.crop((0, f * h, base.width, (f + 1) * h))
        for t in per_strip.get(name, []):
            draw_text(frame, t, rect[0], rect[1])
        base.paste(frame, (0, f * h))
    base.save(os.path.join(SKINS, "%s_strip.png" % name))
print("baked %d slot strips" % len(strips))
