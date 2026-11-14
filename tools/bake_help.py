#!/usr/bin/env python3
"""bake_help.py - draw the HELP page (skins/bg_help.png) on the plain cement background (system python with Pillow):
    python3 -I tools/bake_help.py        (run after make_skin.py)
All text is part of the picture (Label widgets are not reliable on the device)."""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SKINS = os.path.join(os.path.dirname(HERE), "skins")
B = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
R = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
INK, GOLD, RED = (222, 222, 214, 255), (233, 168, 37, 255), (255, 120, 90, 255)

COLS = [
    [("h", "WHAT IT IS"),
     ("p", "A DJ scratch instrument. The touch strip is the record: touch it to grab the record, slide to scrub, lift to let it go. The sample list picks what is on the deck; pads A01-A08 hold samples and double as the crossfader."),
     ("h", "THE STRIP"),
     ("p", "Touch = grab the record. Moving = scratch it, forward and back. Lift = the record plays on at its own speed. The strip works the same everywhere, including the middle."),
     ("p", "Strip Zone (SETUP) = Whole strip, Bottom half or Top half. Throw = how far the record moves for one pull. Smooth and Lead = how soft and how quick the strip follows your finger."),
     ("h", "PADS A01 - A08"),
     ("p", "Each pad holds one sample. Hit a pad to put its sample on the record (at its sticker if it has one). Hit the same pad again and it only works the cut. While held, the pad opens the crossfader; the harder you hit, the faster it opens."),
     ("p", "Pad 12 toggles the HAMSTER switch, which reverses the crossfader.")],
    [("h", "SAMPLE LIST"),
     ("p", "Turn the encoder to scroll one sample per notch. The list shows the files in the plugin's Content/Samples folder."),
     ("p", "PREVIEW  plays the selected sample on channel B. Press again to stop."),
     ("p", "LOAD  arms the 8 pad slots, which flash on screen. Hit one of the pads A01-A08 and the selected sample is stored on that pad."),
     ("p", "TO B  loops the selected sample on channel B, seamlessly. Press the same sample again to clear it."),
     ("p", "STOP B  stops channel B."),
     ("h", "DECK AND PLATTER"),
     ("p", "33 / 45 / STOP set the platter speed. Tap the record on screen to stick a cue marker at the needle; it turns with the platter and is where a pad cues that sample. There is one sticker; tap the record again to remove it."),
     ("h", "MIXER"),
     ("p", "Two channels: A is the deck, B is the loop. Each has GAIN, HI, MID and LOW with the knob at 12 o'clock = flat (0 dB). Gain goes down to silent, EQ to kill. The meters show the level. The crossfader cap shows the cut."),
     ("p", "The MASTER level is on the SETUP tab.")],
    [("h", "SET THE MPC FIRST  (IMPORTANT)"),
     ("p", "The pads only line up with the plugin when the MPC's pad notes are CHROMATIC: set the Pad Mode / Note layout of the program to Chromatic, with the first pad on the Shot Base note (SETUP, default 36). Pads A01-A08 are then the notes 36-43 and the hamster pad is note 47."),
     ("p", "If a pad does nothing, check the note it sends and set Shot Base / Ham Note on the SETUP tab to match."),
     ("h", "PAD LAYOUT"),
     ("p", "A01-A04 are the bottom row of the MPC pads, A05-A08 the row above - the same as on the screen."),
     ("h", "RECORDING IN THE SEQUENCER"),
     ("p", "The sequencer records the strip and the pads, but not where the record sat. Loop Snap (SETUP, default On) fixes that: the record's position at the start of the take is remembered, and every time the loop restarts the record snaps back to it. To start a new take at a different position, pick another record while stopped, or switch Loop Snap Off and On."),
     ("h", "SETUP TAB"),
     ("p", "Strip Src, Strip Zone, Throw, Smooth, Lead, Strip Rel, Deck sample, Go Sticker, Cut curve, Ham Note, Shot Base, Master and Loop Snap. Anything on the BATTLE tab is not repeated here. The Q-Links follow the same layout."),
],
]


def wrap(d, text, font, width):
    lines, cur = [], ""
    for w in text.split():
        t = (cur + " " + w).strip()
        if d.textlength(t, font=font) <= width:
            cur = t
        else:
            lines.append(cur); cur = w
    if cur:
        lines.append(cur)
    return lines


img = Image.open(os.path.join(SKINS, "bg_plain.png")).convert("RGBA")
img.alpha_composite(Image.new("RGBA", img.size, (0, 0, 0, 170)))
d = ImageDraw.Draw(img)
d.text((23, 8), "HELP  -  KEEBZ SCRATCH", font=ImageFont.truetype(B, 22), fill=GOLD)
X0, W, GAP, Y0 = 23, 392, 30, 52
fh, fp = ImageFont.truetype(B, 16), ImageFont.truetype(R, 14)
for ci, col in enumerate(COLS):
    x, y = X0 + ci * (W + GAP), Y0
    warn = ci == 2
    for kind, text in col:
        if kind == "h":
            y += 6
            d.text((x, y), text, font=fh, fill=RED if warn and "MPC" in text else GOLD); y += 23
        else:
            for line in wrap(d, text, fp, W):
                d.text((x, y), line, font=fp, fill=INK); y += 19
            y += 4
    print("column", ci + 1, "ends at y", y)
    assert y <= 624, "help text too long"
img.save(os.path.join(SKINS, "bg_help.png"))
print("wrote bg_help.png")
