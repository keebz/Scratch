#!/usr/bin/env python3
"""make_skin.py - build the Scratch touch-screen skin (skins/ folder) and a mockup.

    python tools/make_skin.py            (needs numpy; the generated files are committed, the build does not need numpy)

Writes: skins/TUI.json, skins/Q-Links.json, skins/*.png and docs/mockup.png.
Parameter indices below must match the order of addParam() calls in src/Scratch.cpp.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import skin_art as A  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SKINS = os.path.join(PROJ, "skins")
DOCS = os.path.join(PROJ, "docs")

# ---- parameter indices (src/Scratch.cpp enum ParamIndex) ---------------------------------------------------------
P = dict(master=0, a_gain=1, a_high=2, a_mid=3, a_low=4, b_gain=5, b_high=6, b_mid=7, b_low=8, strip_src=9, scrub=10,
         throw=11, strip_rel=12, cut_note=13, cut_learn=14, cut_curve=15, hamster=16, deck=17, platter=18, sticker=19,
         go_sticker=20, shot_base=21, smooth=22, lead=23, rec_angle=24, cut_pos=25, level_a=26, level_b=27, load=28,
         pad1=29, stk1=37, strip_zone=45, ham_note=46, pad_smp1=47, browse=55, row1=56, browse_hi=65,
         preview=66, arm=67, status=68, slot1=69, b_loop=77, send_b=78, stop_b=79, scroll=80, loop_snap=81)
NUM_PARAMS = 82
# SETUP tab: knobs by column (Q-Links follow the same table)
# (platter, hamster, browse and preview are on the Battle page, so they are not repeated here)
SLOTS = [["strip_src", "throw", "smooth", "lead"], ["strip_zone", "strip_rel", "deck", "go_sticker"],
         ["cut_curve", "ham_note", "shot_base", "master"], ["loop_snap"]]

PAD_COLS = 4
LIST_B = (A.LIST_XY[0], A.LIST_XY[1], A.LIST_W, A.LIST_ROW * A.LIST_ROWS)                  # the sample list window
BTN_NAMES = ["preview", "arm", "tob", "stopb"]                                             # PREVIEW | LOAD | TO B | STOP B
BTN_B = [(1055 + (i % 2) * 101, 100 + (i // 2) * 114, 93, 106) for i in range(4)]   # right of the list: PREVIEW LOAD / TO B STOP B
SLOT_W, SLOT_H = 96, 46                                                                   # the eight pad slots, 2 rows of 4
SLOT_B = [(829 + (i % 4) * 108, 408 - (i // 4) * 52, SLOT_W, SLOT_H) for i in range(8)]   # like the MPC: A01-A04 on the bottom row, A05-A08 above
SPEED_NAMES = ["s33", "s45", "stop"]
SPEED_B = [(829, 482, 132, 96), (973, 482, 132, 96), (1117, 482, 132, 96)]
XM = A.MIX_XM
HAM_B = (XM - 42, 476, 84, 40)
XFADE_B = (XM - 120, A.XF_Y - 22, 240, 44)
METER_B = [(A.METER_XY[0][0], A.METER_XY[0][1], A.METER_W, A.METER_H), (A.METER_XY[1][0], A.METER_XY[1][1], A.METER_W, A.METER_H)]
HIT_B = (A.REC_C[0] - 200, A.REC_C[1] - 200, 400, 400)

INK = "ff1a1612"
GRAY = "ff9a9a92"
GOLD = "ffe9a825"
WHITE = "ffd8d8d0"
CENTRE = "horizontallyCentred verticallyCentred"
LEFT = "left verticallyCentred"

# (text, x, y, w, h, font, style, size, colour, justification)
TEXTS = [
    ("KEEBZ SCRATCH", 829, 22, 420, 46, "Nexa", "Bold", 38.0, GOLD, LEFT),
    ("BATTLE DECK", 831, 68, 300, 20, "Roboto", "Regular", 15.0, GRAY, LEFT),
    ("A  DECK", A.MIX_COLS[0] - 63, 35, 126, 18, "Nexa", "Bold", 14.0, INK, CENTRE),
    ("B  LOOP", A.MIX_COLS[1] - 63, 35, 126, 18, "Nexa", "Bold", 14.0, INK, CENTRE),
    ("HAMSTER  (PAD 12)", XM - 70, 460, 140, 14, "Roboto", "Bold", 11.0, INK, CENTRE),
    ("CUT / CROSSFADER", XM - 70, 523, 140, 14, "Roboto", "Bold", 11.0, INK, CENTRE),
    ("PREVIEW", BTN_B[0][0], BTN_B[0][1], BTN_B[0][2], BTN_B[0][3], "Nexa", "Bold", 18.0, WHITE, CENTRE),
    ("LOAD", BTN_B[1][0], BTN_B[1][1], BTN_B[1][2], BTN_B[1][3], "Nexa", "Bold", 18.0, WHITE, CENTRE),
    ("TO B", BTN_B[2][0], BTN_B[2][1], BTN_B[2][2], BTN_B[2][3], "Nexa", "Bold", 18.0, WHITE, CENTRE),
    ("STOP B", BTN_B[3][0], BTN_B[3][1], BTN_B[3][2], BTN_B[3][3], "Nexa", "Bold", 18.0, WHITE, CENTRE),
    ("PLATTER", 829, 462, 200, 16, "Roboto", "Regular", 13.0, GRAY, LEFT),
    ("33", SPEED_B[0][0], SPEED_B[0][1], SPEED_B[0][2], SPEED_B[0][3], "Nexa", "Bold", 28.0, WHITE, CENTRE),
    ("45", SPEED_B[1][0], SPEED_B[1][1], SPEED_B[1][2], SPEED_B[1][3], "Nexa", "Bold", 28.0, WHITE, CENTRE),
    ("STOP", SPEED_B[2][0], SPEED_B[2][1], SPEED_B[2][2], SPEED_B[2][3], "Nexa", "Bold", 28.0, WHITE, CENTRE),
]
ROW_LABELS = ["GAIN", "HI", "MID", "LOW"]
for ci, cx in enumerate(A.KNOB_COLS):
    for ri, cy in enumerate(A.KNOB_ROWS):
        TEXTS.append((ROW_LABELS[ri], cx - 30, cy - 52, 60, 16, "Roboto", "Bold", 13.0, INK, CENTRE))
for i in range(8):                                                                           # pad numbers, baked into each slot's frames
    SX, SY = SLOT_B[i][0], SLOT_B[i][1]
    TEXTS.append(("A%02d" % (i + 1), SX + 6, SY + 3, 34, 12, "Roboto", "Bold", 10.0, "ff8a8a84", LEFT))
MOCK_LIST = ["%02d" % (i + 1) for i in range(A.LIST_ROWS)]   # only for the mockup
MOCK_PADS = ["01", "02", "03", "04", "05", "06", "07", "08"]
VALUE_LABELS = []
for k in range(A.LIST_ROWS):                                                                 # the sample list: one Value label per line
    VALUE_LABELS.append(("Parameter %d" % (P["row1"] + k), A.LIST_XY[0] + 10, A.LIST_XY[1] + k * A.LIST_ROW, A.LIST_W - 20, A.LIST_ROW,
                         "Roboto", "Medium", 32.0, "ffe6dcc0", LEFT, MOCK_LIST[k]))
VALUE_LABELS.append(("Parameter %d" % P["status"], 829, 332, 250, 18, "Roboto", "Bold", 13.0, GOLD, LEFT, "PRESS A PAD  (A01 - A08)"))
VALUE_LABELS.append(("Parameter %d" % P["b_loop"], 1119, 332, 130, 18, "Roboto", "Bold", 13.0, "ffe6dcc0", LEFT, "BEAT_LOOP"))
for i in range(8):                                                                           # sample name on each pad slot
    SX, SY, SW, SH = SLOT_B[i]
    VALUE_LABELS.append(("Parameter %d" % (P["pad_smp1"] + i), SX, SY + 17, SW, 24, "Roboto", "Bold", 13.0, "ffd8d8d0", CENTRE, MOCK_PADS[i] or "EMPTY"))

# ---- JSON builders (key order follows docs/ui-reference.md) ------------------------------------------------------
def bounds(x, y, w, h, focus="No", show="Show", when="Always"):
    return {"version": 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": when,
            "boundsType": "Absolute", "bounds": "%g %g %g %g" % (x, y, w, h)}


def entry(name, typ, data, b, remap=None):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in (remap or {}).items()]},
            "bounds": b}


def textstyle(font, style, size, colour, just):
    return {"version": 1, "font": {"version": 1, "name": font, "style": style, "height": size}, "colour": colour,
            "justification": just, "case": "Original"}


def label_text(t):
    text, x, y, w, h, font, style, size, colour, just = t
    return entry("txt", "Label", {"version": 1, "textStyle": textstyle(font, style, size, colour, just), "type": "Text",
                                  "handleName": "Data", "text": text}, bounds(x, y, w, h))


def label_value(handle, x, y, w, h, font, style, size, colour, just):
    return entry("val", "Label", {"version": 1, "textStyle": textstyle(font, style, size, colour, just), "type": "Value",
                                  "handleName": handle}, bounds(x, y, w, h))


def image(path, x, y, w, h):
    return entry("img", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": path}, bounds(x, y, w, h))


def strip(path, frames, handle, x, y, w, h):
    return entry("strip", "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": path,
                                    "numFrames": frames, "handleName": handle}, bounds(x, y, w, h))


def button(on, off, bid, n, handle, x, y, w, h):
    return entry("btn", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": bid, "numButtonsInGroup": n,
                                   "handleName": handle, "gestureBehaviour": "Instant"}, bounds(x, y, w, h, focus="No"))


def transparent_bg():
    return {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""},
            "unfocussed": {"version": 1, "colour": "0", "image": ""}}


def focus_entry(w, h, colour="ffe9a825"):
    return entry("focus", "Focus", {"version": 1, "backgroundColour": "19ffffff", "outlineColour": colour,
                                    "backgroundInset": 2.0, "outlineThickness": 1.0},
                 bounds(0, 0, w, h, when="WhenFocussed"))


def component(children, actions=None):
    return {"version": 2, "actions": actions or [], "backgroundData": transparent_bg(), "ignoreMousePresses": False,
            "disableCoarseDataWheel": False, "componentsData": children}


QLINK_ACTION = [{"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""}]


def build_tui(label_frames_n, sticker_frames_n):
    eq_knob = component([
        focus_entry(A.KNOB_FS, A.KNOB_FS),
        entry("knob", "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": "knob_strip.png", "numFrames": 64,
                               "invert": False, "dragOrientation": "Vertical", "handleName": "Data"},
              bounds(0, 0, A.KNOB_FS, A.KNOB_FS)),
    ], QLINK_ACTION)
    list_knob = component([
        focus_entry(A.LIST_W, A.LIST_ROW * A.LIST_ROWS),
        entry("knob", "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": "blank_strip.png", "numFrames": 2,
                               "invert": False, "dragOrientation": "Vertical", "handleName": "Data"},
              bounds(0, 0, A.LIST_W, A.LIST_ROW * A.LIST_ROWS)),
    ])
    setup_knob = component([
        focus_entry(248, 107),
        entry("knob", "Knob", {"version": 5, "knobType": "GenericLegacy", "polarity": "UniPolar",
                               "dragOrientation": "Vertical", "handleName": "Data"}, bounds(15, 10, 90, 90)),
        entry("name", "Label", {"version": 1, "textStyle": textstyle("Titillium Web", "SemiBold", 24.0, "ffd9c28a",
                                                                       "left verticallyCentred"),
                                "type": "Name", "handleName": "Data"}, bounds(108, 15, 136, 40)),
    ], QLINK_ACTION)

    c = []
    c.append(image("bg.png", 0, 0, 1280, 628))
    cx0, cy0 = A.REC_C
    c.append(strip("label_strip.png", label_frames_n, "Parameter %d" % P["rec_angle"],
                   cx0 - A.LABEL_FS // 2, cy0 - A.LABEL_FS // 2, A.LABEL_FS, A.LABEL_FS))
    c.append(strip("sticker_strip.png", sticker_frames_n, "Parameter %d" % P["stk1"],      # one sticker per record
                   cx0 - A.STICKER_FS // 2, cy0 - A.STICKER_FS // 2, A.STICKER_FS, A.STICKER_FS))
    c.append(button("hit_on.png", "hit_off.png", 1, 1, "Parameter %d" % P["sticker"], *HIT_B))   # tap the record = sticker
    c.append(image("arm.png", 0, 0, 440, 300))
    for ci, cx in enumerate(A.KNOB_COLS):
        for ri, cy in enumerate(A.KNOB_ROWS):
            pidx = P["a_gain"] + ri + 4 * ci
            c.append(entry("eq", "eqKnob", {"version": 1, "handleName": "Data"},
                           bounds(cx - A.KNOB_FS // 2, cy - A.KNOB_FS // 2, A.KNOB_FS, A.KNOB_FS, focus="Yes"),
                           {"Data": "Parameter %d" % pidx}))
    for ci, mb in enumerate(METER_B):
        h = "Parameter %d" % (P["level_a"] + ci)
        c.append(entry("meter", "Meter", {"version": 3, "direction": "Right", "invert": False,
                                          "inactiveImage": "meter_off.png", "peakImage": "meter_on.png",
                                          "rmsImage": "meter_on.png", "peakHandle": h, "rmsHandle": h,
                                          "peakHoldHandle": "Hold/" + h, "peakHoldSize": 4, "peakHoldColour": "ffffffff"},
                       bounds(*mb)))
    c.append(entry("xfader", "Slider", {"version": 2, "sliderType": "TrackAndThumb", "direction": "Horizontal",
                                        "thumbImage": "xfader_cap.png", "handleName": "Parameter %d" % P["cut_pos"]},
                   bounds(*XFADE_B)))
    c.append(button("hamster_on.png", "hamster_off.png", 1, 1, "Parameter %d" % P["hamster"], *HAM_B))
    c.append(strip("list_hi.png", A.LIST_ROWS, "Parameter %d" % P["browse_hi"], *LIST_B))        # highlight band on the selected line
    for vl in VALUE_LABELS[:A.LIST_ROWS]:                                                       # the list lines
        c.append(label_value(vl[0], *vl[1:10]))
    c.append(entry("list", "listKnob", {"version": 1, "handleName": "Data"}, bounds(*LIST_B, focus="Yes"),    # the encoder (or a drag) scrolls it
                   {"Data": "Parameter %d" % P["scroll"]}))
    for i, bb in enumerate(BTN_B):                                                              # PREVIEW | LOAD | TO B (toggles)
        par = [P["preview"], P["arm"], P["send_b"], P["stop_b"]][i]
        c.append(button("%s_on.png" % BTN_NAMES[i], "%s_off.png" % BTN_NAMES[i], 1, 1, "Parameter %d" % par, *bb))
    for i, sb in enumerate(SLOT_B):                                                             # pad slots (display only)
        c.append(strip("slot%d_strip.png" % (i + 1), 4, "Parameter %d" % (P["slot1"] + i), *sb))
    for i, sb in enumerate(SPEED_B):
        c.append(button("%s_on.png" % SPEED_NAMES[i], "%s_off.png" % SPEED_NAMES[i], i, 3, "Parameter %d" % P["platter"], *sb))
    # Static text is baked into the images (tools/bake_text.py): Label widgets over buttons did not show up on the
    # device and may swallow the touches meant for the button below.
    for vl in VALUE_LABELS[A.LIST_ROWS:]:
        c.append(label_value(vl[0], *vl[1:10]))

    qb = ["%d 56 %d 330" % (A.KNOB_COLS[0] - 66, 132), "%d 56 %d 330" % (A.KNOB_COLS[1] - 66, 132)]
    tab1 = {"version": 1, "tabName": "BATTLE", "fnKeyIndex": 0, "fnKeySubIndex": 0,
            "componentDefinition": {"version": 2, "actions": [],
                                    "backgroundData": {"version": 1,
                                                       "focussed": {"version": 1, "colour": "ff121212", "image": ""},
                                                       "unfocussed": {"version": 1, "colour": "ff121212", "image": ""}},
                                    "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": c},
            "qlinkBoundsData": qb}

    # ---- tab 2: SETUP (generic knobs on cement) ----
    slots = SLOTS
    xs, ys = [38, 358, 678, 998], [45, 196, 346, 497]       # same x and width as the Q-Link columns below
    c2 = [image("bg_plain.png", 0, 0, 1280, 628)]
    for col in range(len(slots)):
        for row in range(len(slots[col])):
            name = slots[col][row]
            c2.append(entry(name, "setupKnob", {"version": 1, "handleName": "Data"},
                            bounds(xs[col], ys[row], 248, 107, focus="Yes"), {"Data": "Parameter %d" % P[name]}))
    c2.append(label_text(("SETUP  -  STRIP FEEL / CUT / DECK", 23, 8, 700, 34, "Nexa", "Bold", 24.0, GOLD,
                          "left verticallyCentred")))
    tab2 = {"version": 1, "tabName": "SETUP", "fnKeyIndex": 1, "fnKeySubIndex": 0,
            "componentDefinition": {"version": 2, "actions": [],
                                    "backgroundData": {"version": 1,
                                                       "focussed": {"version": 1, "colour": "ff121212", "image": ""},
                                                       "unfocussed": {"version": 1, "colour": "ff121212", "image": ""}},
                                    "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": c2},
            "qlinkBoundsData": ["38 0 248 624", "358 0 248 624", "678 0 248 624", "998 0 248 624"]}

    tab3 = {"version": 1, "tabName": "HELP", "fnKeyIndex": 2, "fnKeySubIndex": 0,
            "componentDefinition": {"version": 2, "actions": [],
                                    "backgroundData": {"version": 1,
                                                       "focussed": {"version": 1, "colour": "ff121212", "image": ""},
                                                       "unfocussed": {"version": 1, "colour": "ff121212", "image": ""}},
                                    "ignoreMousePresses": False, "disableCoarseDataWheel": False,
                                    "componentsData": [image("bg_help.png", 0, 0, 1280, 628)]},
            "qlinkBoundsData": []}

    repl = [
        {"key": "Parameter %d" % P["platter"], "value": ["33 1/3", "45", "STOP"]},
        {"key": "Parameter %d" % P["strip_src"], "value": ["Bend", "ModWheel", "Scrub"]},
        {"key": "Parameter %d" % P["hamster"], "value": ["Off", "On"]},
        {"key": "Parameter %d" % P["strip_zone"], "value": ["Whole", "Bottom", "Top"]},
        {"key": "Parameter %d" % P["cut_learn"], "value": ["Off", "On"]},
        {"key": "Parameter %d" % P["loop_snap"], "value": ["Off", "On"]},
    ]
    tui = {"pageData": {"version": 1,
                        "componentDefinitions": {"version": 2, "importFiles": [],
                                                 "localComponentDefinitions": [{"key": "eqKnob", "value": eq_knob}, {"key": "listKnob", "value": list_knob},
                                                                               {"key": "setupKnob", "value": setup_knob}]},
                        "replacementStrings": repl,
                        "info": {"version": 1, "type": "CompleteDescription"},
                        "tabs": [tab1, tab2, tab3]}}
    return tui


def build_qlinks():
    def blank():
        return {("Q-Link %d" % i): -1 for i in range(1, 17)}

    q1 = blank()
    colq = [[13, 9, 5, 1], [14, 10, 6, 2]]
    for ci in range(2):
        for ri in range(4):
            q1["Q-Link %d" % colq[ci][ri]] = P["a_gain"] + ri + 4 * ci
    q2 = blank()
    slots = SLOTS
    for col in range(len(slots)):
        for row in range(len(slots[col])):
            q2["Q-Link %d" % (13 + col - 4 * row)] = P[slots[col][row]]
    prog = [1, 2, 3, 4, 5, 6, 7, 8, 15, 11, 22, 23, 17, 18, 0, 12]
    return {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
            "Screen Mode Q-Links": {"version": 4, "map": [
                {"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": q1},
                {"Tab": 2, "SubTab": 1, "Bank Direction": "Column", "Q-Links": q2}]},
            "Program Mode Q-Links": {("Q-Link %d" % (i + 1)): prog[i] for i in range(16)}}


def paste(dst, src_rgba, x, y):
    h, w = src_rgba.shape[:2]
    x0, y0 = max(x, 0), max(y, 0)
    x1, y1 = min(x + w, dst.shape[1]), min(y + h, dst.shape[0])
    if x1 <= x0 or y1 <= y0: return
    s = src_rgba[y0 - y:y1 - y, x0 - x:x1 - x]
    a = s[..., 3:4]
    dst[y0:y1, x0:x1] = dst[y0:y1, x0:x1] * (1 - a) + s[..., :3] * a


def main():
    os.makedirs(SKINS, exist_ok=True)
    os.makedirs(DOCS, exist_ok=True)
    rng = np.random.default_rng(1963)
    yy, xx = A.grid(A.H, A.W)
    cem = A.cement(rng)
    A.write_png(os.path.join(SKINS, "bg_plain.png"), cem)
    bg = cem.copy()
    A.draw_deck(bg, yy, xx, rng)
    A.draw_mixer(bg, yy, xx, rng)
    A.draw_pads_panel(bg, yy, xx, rng)
    A.write_png(os.path.join(SKINS, "bg.png"), bg)

    LF, SF = 72, 36
    A.write_png(os.path.join(SKINS, "label_strip.png"), A.label_strip(LF))
    A.write_png(os.path.join(SKINS, "sticker_strip.png"), A.sticker_strip(SF))
    arm = A.arm_overlay()
    A.write_png(os.path.join(SKINS, "arm.png"), arm)
    A.write_png(os.path.join(SKINS, "knob_strip.png"), A.knob_strip(64))
    moff, mon = A.meter_images()
    A.write_png(os.path.join(SKINS, "meter_off.png"), moff); A.write_png(os.path.join(SKINS, "meter_on.png"), mon)
    cap = A.xfader_cap(); A.write_png(os.path.join(SKINS, "xfader_cap.png"), cap)
    ho, hn = A.hamster_images()
    A.write_png(os.path.join(SKINS, "hamster_off.png"), ho); A.write_png(os.path.join(SKINS, "hamster_on.png"), hn)
    bo, bn = A.button_images(BTN_B[0][2], BTN_B[0][3]); A.write_png(os.path.join(SKINS, "btn_off.png"), bo); A.write_png(os.path.join(SKINS, "btn_on.png"), bn)
    so, sn = A.button_images(SPEED_B[0][2], SPEED_B[0][3]); A.write_png(os.path.join(SKINS, "speed_off.png"), so); A.write_png(os.path.join(SKINS, "speed_on.png"), sn)
    A.write_png(os.path.join(SKINS, "list_hi.png"), A.list_highlight_strip())
    sstrip = A.slot_strip(SLOT_W, SLOT_H); A.write_png(os.path.join(SKINS, "slot_strip.png"), sstrip)
    A.write_png(os.path.join(SKINS, "blank_strip.png"), np.zeros((80, 40, 4)))
    hit = np.zeros((HIT_B[3], HIT_B[2], 4)); A.write_png(os.path.join(SKINS, "hit_off.png"), hit); A.write_png(os.path.join(SKINS, "hit_on.png"), hit)

    with open(os.path.join(SKINS, "TUI.json"), "w") as f:
        json.dump(build_tui(LF, SF + 1), f, indent=1)
    with open(os.path.join(SKINS, "Q-Links.json"), "w") as f:
        json.dump(build_qlinks(), f, indent=1)

    # ---- mockup: the main tab as it would look, with plausible live values ----
    m = bg.copy()
    cx, cy = A.REC_C
    paste(m, A.label_frame(A.LABEL_FS, np.radians(52)), cx - A.LABEL_FS // 2, cy - A.LABEL_FS // 2)
    for j in (1,):                                                # the one sticker on the record
        paste(m, A.sticker_frame(A.STICKER_FS, j), cx - A.STICKER_FS // 2, cy - A.STICKER_FS // 2)
    paste(m, arm, 0, 0)
    knob_vals = [[0.5] * 4, [0.5] * 4]                                 # every knob at 12 o'clock = 0 dB
    for ci, cxk in enumerate(A.KNOB_COLS):
        for ri, cyk in enumerate(A.KNOB_ROWS):
            paste(m, A.knob_frame(A.KNOB_FS, knob_vals[ci][ri]), cxk - A.KNOB_FS // 2, cyk - A.KNOB_FS // 2)
    for mb, lvl in zip(METER_B, (0.72, 0.30)):
        mx, my, mw, mh = mb
        paste(m, moff, mx, my)
        sub = mon.copy(); sub[:, int(mw * lvl):, 3] = 0
        paste(m, sub, mx, my)
    xb = XFADE_B
    paste(m, cap, int(xb[0] + 0.5 * (xb[2] - cap.shape[1])), xb[1] + (xb[3] - cap.shape[0]) // 2)
    paste(m, ho, *HAM_B[:2])
    paste(m, A.list_highlight_strip()[2 * A.LIST_ROW * 0 + 3 * (A.LIST_ROW * A.LIST_ROWS):4 * (A.LIST_ROW * A.LIST_ROWS)], A.LIST_XY[0], A.LIST_XY[1])   # line 4 selected
    paste(m, bo, *BTN_B[0][:2]); paste(m, bn, *BTN_B[1][:2]); paste(m, bo, *BTN_B[2][:2]); paste(m, bo, *BTN_B[3][:2])         # LOAD is armed in the mockup
    for i, sb in enumerate(SLOT_B):
        st = [2, 1, 3, 3, 3, 3, 3, 3][i]                                                        # pad 1 playing, pad 2 loaded, the rest flashing
        paste(m, sstrip[st * SLOT_H:(st + 1) * SLOT_H], sb[0], sb[1])
    for i, sb in enumerate(SPEED_B):
        paste(m, sn if i == 0 else so, *sb[:2])
    A.write_png(os.path.join(DOCS, "mockup_notext.png"), m)
    buttons = [[BTN_NAMES[i], list(BTN_B[i])] for i in range(4)]
    buttons += [[SPEED_NAMES[i], list(SPEED_B[i])] for i in range(3)]
    strips = [["slot%d" % (i + 1), list(SLOT_B[i]), 4] for i in range(8)]
    with open(os.path.join(HERE, ".bake.json"), "w") as f:
        json.dump({"texts": [list(x) for x in TEXTS], "buttons": buttons, "strips": strips}, f)
    texts = [list(x) for x in TEXTS] + [[v[10]] + list(v[1:10]) for v in VALUE_LABELS]
    with open(os.path.join(HERE, ".mockup_texts.json"), "w") as f:
        json.dump(texts, f)
    print("skin written to", SKINS)


if __name__ == "__main__":
    main()
