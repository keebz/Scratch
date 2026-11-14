"""skin_art.py - procedural artwork for the Scratch touch screen (needs numpy; outputs are committed in skins/).

Everything here is drawn from scratch with numpy: grimy cement, a battle-position turntable, tonearm, knobs, LED
meters, pads. No third-party artwork. Run via tools/make_skin.py.
"""
import math
import struct
import zlib

import numpy as np

W, H = 1280, 628

# ---- layout (one grid: 12 px outer margin and 12 px gaps; every module spans y 12..616) ---------------------------
DECK = (12, 12, 487, 616)            # turntable module (a Technics SL-1200 top view turned 90 degrees: battle position)
MIX = (499, 12, 799, 616)            # two-channel mixer module
PADS = (811, 12, 1268, 616)          # pads module
S_DECK = 0.908                       # photo (665 x 523) -> module scale

def T(px, py):
    """map a point of the reference photo (tonearm on the right) to the module (rotated 90 degrees counter-clockwise)"""
    return (DECK[0] + S_DECK * py, DECK[1] + S_DECK * (665 - px))

REC_C = tuple(int(round(v)) for v in T(265, 265))     # record centre
REC_R = 195                                           # vinyl radius
PLATTER_R = 224
LABEL_R = 65
LABEL_FS = 140                                        # label film-strip frame size
STICKER_FS = 200                                      # sticker film-strip frame size
STICKER_R = 84                                        # sticker orbit radius
KNOB_FS = 60
MIX_XM = 649                                          # centre line of the mixer
MIX_COLS = (580, 718)                                 # knob centres of channel A / B (each column is 150 wide)
KNOB_ROWS = (108, 188, 268, 348)                      # four evenly spaced rows (78 px)
KNOB_COLS = MIX_COLS
METER_W, METER_H = 124, 14                            # horizontal LED meters in a black strip under the knobs
METER_XY = ((518, 403), (656, 403))
XF_Y = 582                                            # crossfader slot centre line
PAD_S, PAD_GAP = 96, 12
PAD_H = 76                                          # pad height: the rest of each 108 px cell holds the sample name
PAD_X0, PAD_Y0 = 829, 106
PAD_STEP = PAD_S + PAD_GAP
BASE_GRID_X = (829, 973, 1117)

# ---- low level helpers ------------------------------------------------------------------------------------------
def write_png(path, a):
    """a: float array (h, w, 3 or 4) in 0..1 -> PNG (8-bit RGB/RGBA)."""
    a = np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8)
    h, w, c = a.shape
    raw = b"".join(b"\x00" + a[y].tobytes() for y in range(h))

    def chunk(tag, data):
        c2 = struct.pack(">I", len(data)) + tag + data
        return c2 + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ctype = 6 if c == 4 else 2
    blob = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    blob += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(blob)


def _box(a, r, axis):
    n = a.shape[axis]
    if r < 1 or n < 2:
        return a
    r = min(r, n - 1)
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r, r)
    ap = np.pad(a, pad, mode="wrap")
    c = np.cumsum(ap, axis=axis, dtype=np.float64)
    zshape = list(c.shape); zshape[axis] = 1
    c = np.concatenate([np.zeros(zshape), c], axis=axis)
    hi = [slice(None)] * a.ndim; lo = [slice(None)] * a.ndim
    hi[axis] = slice(2 * r + 1, 2 * r + 1 + n); lo[axis] = slice(0, n)
    return (c[tuple(hi)] - c[tuple(lo)]) / (2 * r + 1)


def blur(a, r, passes=3):
    for _ in range(passes):
        a = _box(_box(a, r, 0), r, 1)
    return a


def fnoise(h, w, scale, rng):
    n = rng.standard_normal((h, w))
    n = blur(n, max(1, int(scale * 0.8)))
    s = n.std()
    return n / s if s > 0 else n


def disc(yy, xx, cx, cy, r):
    return np.clip(r - np.hypot(xx - cx, yy - cy) + 0.5, 0.0, 1.0)


def ring(yy, xx, cx, cy, r0, r1):
    d = np.hypot(xx - cx, yy - cy)
    return np.clip(r1 - d + 0.5, 0, 1) * np.clip(d - r0 + 0.5, 0, 1)


def rrect(yy, xx, x0, y0, x1, y1, rad):
    """coverage of a rounded rectangle"""
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - rad, (y1 - y0) / 2 - rad
    qx = np.abs(xx - cx) - hx
    qy = np.abs(yy - cy) - hy
    d = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - rad
    return np.clip(0.5 - d, 0, 1), -d          # coverage, inside distance (positive inside)


def mix(dst, rgb, cov):
    """dst (h,w,3) <- rgb (scalar triple or (h,w,3)) with coverage (h,w)"""
    c = cov[..., None]
    return dst * (1 - c) + np.asarray(rgb) * c


def grid(h, w):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    return yy, xx


# ---- cement -----------------------------------------------------------------------------------------------------
def cement(rng):
    yy, xx = grid(H, W)
    n = (0.35 * fnoise(H, W, 1, rng) + 0.5 * fnoise(H, W, 3, rng) + 0.7 * fnoise(H, W, 10, rng)
         + 0.9 * fnoise(H, W, 35, rng) + 0.9 * fnoise(H, W, 120, rng))
    base = 0.205 + 0.040 * n
    img = base[..., None] * np.array([1.0, 0.99, 0.965])
    st = np.clip((fnoise(H, W, 60, rng) - 0.5) * 0.55, 0, 1)                 # damp stains
    img *= (1 - 0.40 * st)[..., None]
    col = fnoise(1, W, 2, rng)[0]                                             # vertical grime streaks
    env = fnoise(H, 1, 150, rng)[:, 0]
    streak = np.clip(col[None, :] * 0.55 + 0.25, 0, 1) * np.clip(env[:, None] * 0.5 + 0.3, 0, 1)
    img *= (1 - 0.14 * streak)[..., None]
    # pits: dark hole with a light lower-right rim
    for _ in range(850):
        cx, cy, r = rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0.7, 2.4)
        x0, x1, y0, y1 = int(cx - 6), int(cx + 7), int(cy - 6), int(cy + 7)
        x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, W), min(y1, H)
        if x1 <= x0 or y1 <= y0: continue
        sy, sx = yy[y0:y1, x0:x1], xx[y0:y1, x0:x1]
        hole = disc(sy, sx, cx, cy, r); rim = disc(sy, sx, cx + 0.8, cy + 0.8, r + 0.8) - hole
        img[y0:y1, x0:x1] = mix(img[y0:y1, x0:x1], [0.05, 0.05, 0.05], hole * 0.8)
        img[y0:y1, x0:x1] = mix(img[y0:y1, x0:x1], [0.42, 0.41, 0.39], np.clip(rim, 0, 1) * 0.55)
    # cracks: random walks
    # cast-concrete formwork: vertical seams and tie-hole dimples on a grid
    for sx_ in (619, 897):
        img[:, sx_] *= 0.55; img[:, sx_ + 1] = img[:, sx_ + 1] * 1.0 + 0.05
    img[311] *= 0.6; img[312] = img[312] + 0.045
    for tx in (310, 760, 1085):
        for ty in (150, 470):
            hole = disc(yy, xx, tx, ty, 9); rim = disc(yy, xx, tx + 1.5, ty + 1.5, 11) - hole
            img = mix(img, [0.04, 0.04, 0.04], hole * 0.85)
            img = mix(img, [0.38, 0.37, 0.35], np.clip(rim, 0, 1) * 0.45)
    for _ in range(11):
        x, y, a = rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0, 2 * math.pi)
        for _ in range(int(rng.uniform(80, 260))):
            a += rng.normal(0, 0.30); x += math.cos(a) * 2.0; y += math.sin(a) * 2.0
            ix, iy = int(x), int(y)
            if 1 <= ix < W - 1 and 1 <= iy < H - 1:
                img[iy, ix] *= 0.2; img[iy, ix + 1] *= 0.5; img[iy - 1, ix] *= 0.7
                img[iy + 1, ix + 1] = img[iy + 1, ix + 1] * 0.8 + 0.07
    # spray paint: worn orange / yellow splatter near the corners, with a few drips
    for (cx, cy, col2, n2) in [(45, 565, (0.85, 0.40, 0.08), 520), (1235, 440, (0.95, 0.75, 0.10), 420),
                              (1240, 20, (0.85, 0.40, 0.08), 260), (620, 614, (0.9, 0.20, 0.12), 240), (900, 8, (0.9, 0.2, 0.12), 150)]:
        for _ in range(n2):
            px, py = cx + rng.normal(0, 38), cy + rng.normal(0, 30)
            r = abs(rng.normal(0, 1.3)) + 0.4
            x0, x1, y0, y1 = int(px - 4), int(px + 5), int(py - 4), int(py + 5)
            x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, W), min(y1, H)
            if x1 <= x0 or y1 <= y0: continue
            c = disc(yy[y0:y1, x0:x1], xx[y0:y1, x0:x1], px, py, r) * rng.uniform(0.25, 0.6)
            img[y0:y1, x0:x1] = mix(img[y0:y1, x0:x1], col2, c)
        x = cx + rng.normal(0, 8)                                              # a drip
        for yy2 in range(int(cy), int(min(H - 1, cy + rng.uniform(15, 60)))):
            xi = int(x + rng.normal(0, 0.3))
            if 0 <= xi < W: img[yy2, xi] = img[yy2, xi] * 0.7 + np.array(col2) * 0.3
    vig = 1 - 0.30 * (((xx - W / 2) / (W / 2)) ** 2 * 0.7 + ((yy - H / 2) / (H / 2)) ** 2 * 0.5)
    return np.clip(img * vig[..., None], 0, 1)


def brushed(h, w, rng, strength=0.03):
    n = rng.standard_normal((h, w))
    n = _box(n, 24, 1)
    n = n / max(n.std(), 1e-9)
    return n * strength + fnoise(h, w, 2, rng) * strength * 0.3


# ---- the modules, painted onto the cement ----------------------------------------------------------------------
def plate(img, yy, xx, box, rad, base, rng, grime=0.5, bevel=5, tint=(1.0, 0.99, 0.97), brush=0.022):
    x0, y0, x1, y1 = box
    cov, inside = rrect(yy, xx, x0, y0, x1, y1, rad)
    sy, sx = slice(max(y0 - 10, 0), min(y1 + 10, H)), slice(max(x0 - 10, 0), min(x1 + 10, W))
    cy_, cx_ = yy[sy, sx], xx[sy, sx]
    c, ins = cov[sy, sx], inside[sy, sx]
    tex = base + brushed(c.shape[0], c.shape[1], rng, brush)
    dirt = np.clip(fnoise(c.shape[0], c.shape[1], 25, rng) * 0.5 + 0.3, 0, 1) * grime
    tex = tex * (1 - 0.30 * dirt) - 0.010 * np.clip(fnoise(c.shape[0], c.shape[1], 2, rng), 0, 3)
    face = np.stack([tex * tint[0], tex * tint[1], tex * tint[2]], axis=-1)
    edge = np.clip(1 - ins / bevel, 0, 1) * (ins > 0)
    lit = np.clip(((cx_ - (x0 + x1) / 2) * -0.7 + (cy_ - (y0 + y1) / 2) * -0.7) / 300, -1, 1)
    face = face + (edge * lit * 0.16)[..., None]
    face = mix(face, [0.07, 0.07, 0.065], np.clip(1 - ins / 1.6, 0, 1) * (ins > 0) * 0.9)      # crisp dark outline
    shadow = np.clip(blur(rrect(cy_, cx_, x0 + 3, y0 + 6, x1 + 3, y1 + 6, rad)[0], 7), 0, 1)
    region = img[sy, sx]
    region = mix(region, [0, 0, 0], shadow * 0.60 * (1 - c))
    img[sy, sx] = mix(region, face, c)


def screw(img, yy, xx, cx, cy, r=6):
    x0, x1, y0, y1 = int(cx - r - 3), int(cx + r + 4), int(cy - r - 3), int(cy + r + 4)
    sy, sx = yy[y0:y1, x0:x1], xx[y0:y1, x0:x1]
    sub = img[y0:y1, x0:x1]
    sub = mix(sub, [0, 0, 0], disc(sy, sx, cx + 1.2, cy + 1.8, r + 1) * 0.45)
    sub = mix(sub, [0.62, 0.62, 0.60], disc(sy, sx, cx, cy, r))
    sub = mix(sub, [0.32, 0.32, 0.30], ring(sy, sx, cx, cy, r - 1.4, r))
    sl = np.clip(1.3 - np.abs((sx - cx) * 0.7 + (sy - cy) * 0.7), 0, 1) * disc(sy, sx, cx, cy, r - 1.4)
    img[y0:y1, x0:x1] = mix(sub, [0.10, 0.10, 0.10], sl * 0.85)


def draw_deck(img, yy, xx, rng):
    """The reference turntable (silver Technics-style deck) turned 90 degrees counter-clockwise."""
    plate(img, yy, xx, DECK, 18, 0.815, rng, grime=0.22, tint=(1.0, 1.0, 1.0), brush=0.012)
    cx, cy = REC_C
    x0, x1, y0, y1 = cx - 240, cx + 240, cy - 240, cy + 240
    sy, sx = yy[y0:y1, x0:x1], xx[y0:y1, x0:x1]
    sub = img[y0:y1, x0:x1].copy()
    d = np.hypot(sx - cx, sy - cy)
    phi = np.arctan2(sx - cx, -(sy - cy))
    sub = mix(sub, [0, 0, 0], disc(sy, sx, cx + 6, cy + 9, PLATTER_R + 2) * 0.35)             # soft shadow
    sub = mix(sub, [0.72, 0.72, 0.70], disc(sy, sx, cx, cy, PLATTER_R))                         # aluminium rim
    sub = mix(sub, [0.40, 0.40, 0.38], ring(sy, sx, cx, cy, PLATTER_R - 2, PLATTER_R))
    sub = mix(sub, [0.035, 0.035, 0.035], disc(sy, sx, cx, cy, PLATTER_R - 3))                  # strobe band
    for rr, n_, off in ((216, 156, 0.0), (209, 156, 0.5)):                                      # two rows of strobe dots
        step = 2 * math.pi / n_
        k = np.round((phi - off * step) / step)
        ang = k * step + off * step
        px_, py_ = cx + rr * np.sin(ang), cy - rr * np.cos(ang)
        dot = np.clip(1.6 - np.hypot(sx - px_, sy - py_), 0, 1) * (np.abs(d - rr) < 3)
        sub = mix(sub, [0.90, 0.90, 0.86], dot * 0.95)
    sub = mix(sub, [0.20, 0.20, 0.19], ring(sy, sx, cx, cy, REC_R + 3, REC_R + 8))
    gr = rng.standard_normal(300); gr = np.convolve(gr, np.ones(2) / 2, mode="same")
    groove = np.interp(d, np.arange(300), gr)
    base = 0.030 + 0.009 * groove
    for gap_r in (180, 160, 138, 118, 98):
        base -= 0.018 * np.clip(1 - np.abs(d - gap_r) / 2.5, 0, 1)
    glare = (0.085 * np.maximum(0, np.cos(2 * (phi - 0.62))) ** 5 + 0.075 * np.maximum(0, np.cos(2 * (phi - 3.76))) ** 5
             + 0.025 * np.maximum(0, np.cos(2 * (phi - 2.2))) ** 3)
    glare *= np.clip((d - 70) / 30, 0, 1) * np.clip((REC_R - 2 - d) / 6, 0, 1)
    vin = np.clip(base + glare * (0.75 + 0.5 * groove), 0, 1)
    sub = mix(sub, np.stack([vin, vin, vin * 1.05], axis=-1), disc(sy, sx, cx, cy, REC_R))
    for _ in range(40):                                                                         # a few fine scratches
        a0, r0 = rng.uniform(0, 2 * math.pi), rng.uniform(80, 190)
        for ta in np.linspace(a0, a0 + rng.uniform(0.02, 0.18), 30):
            ix, iy = int(cx + r0 * math.sin(ta)) - x0, int(cy - r0 * math.cos(ta)) - y0
            if 0 <= ix < x1 - x0 and 0 <= iy < y1 - y0:
                sub[iy, ix] = np.clip(sub[iy, ix] + 0.08, 0, 1)
    sub = mix(sub, [0.02, 0.02, 0.02], disc(sy, sx, cx, cy, LABEL_R + 4))
    img[y0:y1, x0:x1] = sub

    # decoration taken from the reference photo (mapped through T)
    ax, ay = T(62, 58)                                                                          # 45 rpm adaptor
    img[:] = mix(img, [0.0, 0.0, 0.0], disc(yy, xx, ax + 2, ay + 3, 27) * 0.30)
    img[:] = mix(img, [0.78, 0.78, 0.76], disc(yy, xx, ax, ay, 26))
    img[:] = mix(img, [0.45, 0.45, 0.43], ring(yy, xx, ax, ay, 24, 26))
    img[:] = mix(img, [0.10, 0.10, 0.10], disc(yy, xx, ax, ay, 8))
    tx, ty = T(458, 52)                                                                         # target-light hole
    img[:] = mix(img, [0.06, 0.06, 0.06], disc(yy, xx, tx, ty, 9)); img[:] = mix(img, [0.55, 0.55, 0.53], ring(yy, xx, tx, ty, 9, 11))
    # pedestal / pitch fader slot (top edge) / start-stop / 33-45 / power knob / reset / switch
    fx0, fy0 = T(606, 430); fx1, fy1 = T(606, 270)
    cov, _ = rrect(yy, xx, min(fx0, fx1) - 2, fy1 - 4, max(fx0, fx1) + 2, fy0 + 4, 3)
    img[:] = mix(img, [0.03, 0.03, 0.03], cov)
    for k in range(9):
        px, py = T(584, 270 + 20 * k)
        img[:] = mix(img, [0.15, 0.15, 0.14], np.clip(1 - np.abs(xx - px) / 0.8, 0, 1) * ((yy > py - 7) & (yy < py - 3)))
    capx = T(606, 350)[0]
    cov, ins = rrect(yy, xx, capx - 6, fy1 - 10, capx + 6, fy1 + 12, 3)
    img[:] = mix(img, [0.0, 0.0, 0.0], cov * 0.35 * 0 + np.clip(1 - np.clip(ins, 0, 1.6) / 1.6, 0, 1) * (ins > 0) * 0.5)
    img[:] = mix(img, [0.80, 0.80, 0.78], cov); img[:] = mix(img, [0.10, 0.10, 0.10], np.clip(1 - np.abs(xx - capx) / 0.9, 0, 1) * cov)
    sx0, sy0 = T(28, 500); sx1, sy1 = T(90, 455)                                                # start / stop
    cov, ins = rrect(yy, xx, sx0, sy1, sx1, sy0, 5)
    img[:] = mix(img, [0.62, 0.62, 0.60], cov); img[:] = mix(img, [0.35, 0.35, 0.33], np.clip(1 - ins / 1.6, 0, 1) * (ins > 0))
    kx, ky = T(45, 415)                                                                         # power knob
    img[:] = mix(img, [0, 0, 0], disc(yy, xx, kx + 1.5, ky + 2.5, 22) * 0.4)
    img[:] = mix(img, [0.07, 0.07, 0.07], disc(yy, xx, kx, ky, 21)); img[:] = mix(img, [0.30, 0.30, 0.29], ring(yy, xx, kx, ky, 19, 21))
    img[:] = mix(img, [0.75, 0.12, 0.10], disc(yy, xx, kx, ky - 14, 2.6))
    for (px_, py_) in ((100, 492), (140, 492)):                                                 # 33 / 45 buttons
        bx, by = T(px_, py_)
        cov, ins = rrect(yy, xx, bx - 8, by - 16, bx + 8, by + 16, 3)
        img[:] = mix(img, [0.20, 0.20, 0.19], cov); img[:] = mix(img, [0.55, 0.55, 0.52], np.clip(1 - ins / 1.5, 0, 1) * (ins > 0) * 0.6)
    rx, ry = T(565, 410)
    img[:] = mix(img, [0.0, 0.0, 0.0], disc(yy, xx, rx + 1, ry + 2, 15) * 0.3)
    img[:] = mix(img, [0.80, 0.80, 0.78], disc(yy, xx, rx, ry, 14)); img[:] = mix(img, [0.40, 0.40, 0.38], ring(yy, xx, rx, ry, 12, 14))
    wx, wy = T(392, 484)
    cov, ins = rrect(yy, xx, wx - 14, wy - 8, wx + 14, wy + 8, 8)
    img[:] = mix(img, [0.52, 0.52, 0.50], cov); img[:] = mix(img, [0.20, 0.20, 0.19], np.clip(1 - ins / 1.5, 0, 1) * (ins > 0))
    img[:] = mix(img, [0.88, 0.88, 0.85], disc(yy, xx, wx - 6, wy, 5))
    # tonearm pedestal (black round base, upper left) drawn here; the arm itself is the overlay
    bx, by = T(545, 125)
    img[:] = mix(img, [0, 0, 0], disc(yy, xx, bx + 3, by + 5, 72) * 0.35)
    img[:] = mix(img, [0.04, 0.04, 0.04], disc(yy, xx, bx, by, 70))
    img[:] = mix(img, [0.30, 0.30, 0.29], ring(yy, xx, bx, by, 66, 70) * 0.8)
    img[:] = mix(img, [0.12, 0.12, 0.11], disc(yy, xx, bx, by, 58))
    img[:] = mix(img, [0.62, 0.62, 0.60], ring(yy, xx, bx, by, 38, 44))
    ax2, ay2 = T(607, 145)                                                                      # anti-skate dial
    img[:] = mix(img, [0.05, 0.05, 0.05], disc(yy, xx, ax2, ay2, 11)); img[:] = mix(img, [0.35, 0.35, 0.33], ring(yy, xx, ax2, ay2, 9, 11))


def engraved_line(img, yy, xx, x0, y0, x1, y1):
    """thin engraved divider: dark line with a light line beside it (vertical or horizontal)"""
    if x0 == x1:
        m = ((yy >= y0) & (yy <= y1)).astype(float)
        img[:] = mix(img, [0.10, 0.09, 0.07], np.clip(1 - np.abs(xx - x0) / 0.8, 0, 1) * m)
        img[:] = mix(img, [0.90, 0.86, 0.76], np.clip(1 - np.abs(xx - (x0 + 1.2)) / 0.8, 0, 1) * m * 0.55)
    else:
        m = ((xx >= x0) & (xx <= x1)).astype(float)
        img[:] = mix(img, [0.10, 0.09, 0.07], np.clip(1 - np.abs(yy - y0) / 0.8, 0, 1) * m)
        img[:] = mix(img, [0.90, 0.86, 0.76], np.clip(1 - np.abs(yy - (y0 + 1.2)) / 0.8, 0, 1) * m * 0.55)


def draw_mixer(img, yy, xx, rng):
    """Champagne brushed-aluminium two-channel mixer, laid out like the Vestax PMC-06 Pro A reference."""
    plate(img, yy, xx, MIX, 12, 0.66, rng, grime=0.40, tint=(1.0, 0.95, 0.83), brush=0.028)
    for (sx_, sy_) in [(MIX[0] + 14, MIX[1] + 14), (MIX[2] - 14, MIX[1] + 14), (MIX[0] + 14, MIX[3] - 14), (MIX[2] - 14, MIX[3] - 14)]:
        screw(img, yy, xx, sx_, sy_, 5)
    xm = MIX_XM
    # channel columns: divider lines like the reference (left edge, centre, right edge), red header bars
    for x in (MIX[0] + 12, xm, MIX[2] - 12):
        engraved_line(img, yy, xx, x, 38, x, 452)
    for ci, cx in enumerate(MIX_COLS):
        bar = rrect(yy, xx, cx - 53, 26, cx + 53, 32, 2)[0]
        img[:] = mix(img, [0.62, 0.10, 0.07], bar)
        for cy in KNOB_ROWS:
            img[:] = mix(img, [0, 0, 0], disc(yy, xx, cx + 1.5, cy + 2, 31) * 0.20)
            knob_ticks(img, yy, xx, cx, cy)
    # black LED strip with a dB scale under it (the reference has the same strip across both channels)
    cov, ins = rrect(yy, xx, MIX[0] + 12, 395, MIX[2] - 12, 441, 5)
    img[:] = mix(img, [0.03, 0.03, 0.035], cov)
    img[:] = mix(img, [0.40, 0.37, 0.30], np.clip(1 - ins / 2, 0, 1) * (ins > 0) * 0.7)
    for (mx, my) in METER_XY:
        for k in range(8):
            tx = mx + 6 + k * (METER_W - 12) / 7
            img[:] = mix(img, [0.10, 0.09, 0.07], np.clip(1 - np.abs(xx - tx) / 0.8, 0, 1) * ((yy > my + 22) & (yy < my + (28 if k in (0, 3, 7) else 25))))
    # centre indicator lamp (the reference has one between the channel sections)
    img[:] = mix(img, [0, 0, 0], disc(yy, xx, xm + 1, 450, 6) * 0.4)
    img[:] = mix(img, [0.15, 0.14, 0.12], disc(yy, xx, xm, 449, 5)); img[:] = mix(img, [0.85, 0.85, 0.80], disc(yy, xx, xm - 1.2, 447.8, 1.6) * 0.8)
    # crossfader module: framed, curved scale with fan marks, slot
    sl_x0, sl_x1, sl_y = xm - 100, xm + 100, XF_Y
    cov, ins = rrect(yy, xx, MIX[0] + 12, 520, MIX[2] - 12, 606, 6)
    img[:] = mix(img, [0.13, 0.12, 0.10], cov * 0.18)
    img[:] = mix(img, [0.10, 0.09, 0.07], np.clip(1 - np.abs(ins - 1) / 0.9, 0, 1) * (ins > 0) * 0.8)
    for t in np.linspace(0, 1, 260):                                                            # the curve above the slot
        xc = xm - 112 + 224 * t
        yc = 556 - 22 * (1 - (2 * t - 1) ** 2) * 0.55
        img[:] = mix(img, [0.10, 0.09, 0.07], disc(yy, xx, xc, yc, 1.0) * 0.9)
    for side in (-1, 1):                                                                        # fan marks at each end
        for k in range(6):
            fx = xm + side * (122 + k * 4.2)
            hgt = 3 + k * 2.4
            img[:] = mix(img, [0.10, 0.09, 0.07], np.clip(1 - np.abs(xx - fx) / 0.9, 0, 1) * ((yy > sl_y - hgt) & (yy < sl_y + hgt)))
    cov, ins = rrect(yy, xx, sl_x0 - 14, sl_y - 8, sl_x1 + 14, sl_y + 8, 8)
    img[:] = mix(img, [0.03, 0.03, 0.03], cov)
    img[:] = mix(img, [0.38, 0.35, 0.28], np.clip(1 - ins / 2, 0, 1) * (ins > 0) * 0.8)
    img[:] = mix(img, [0.09, 0.08, 0.07], np.clip(1 - np.abs(yy - sl_y) / 0.7, 0, 1) * ((xx > sl_x0 - 6) & (xx < sl_x1 + 6)))


def knob_ticks(img, yy, xx, cx, cy, r=32):
    for i in range(11):
        a = math.radians(-135 + 27 * i)
        big = i in (0, 5, 10)
        for rr in np.linspace(r, r + (6 if big else 4), 8):
            px, py = cx + rr * math.sin(a), cy - rr * math.cos(a)
            col = (0.62, 0.08, 0.05) if i == 5 else (0.10, 0.09, 0.07)
            ix, iy = int(round(px)), int(round(py))
            for dx in (0, 1) if big else (0,):
                img[iy, ix + dx] = col


LIST_XY, LIST_W, LIST_ROW, LIST_ROWS = (829, 100), 210, 44, 5      # the sample list window (left half; big text)


def draw_pads_panel(img, yy, xx, rng):
    """Dark rubber module: title, sample list window, hazard tape; same top and bottom as the other modules."""
    plate(img, yy, xx, PADS, 16, 0.105, rng, grime=0.7, tint=(1.0, 1.0, 1.04), brush=0.008)
    for (sx_, sy_) in [(PADS[0] + 14, PADS[1] + 14), (PADS[2] - 14, PADS[1] + 14), (PADS[0] + 14, PADS[3] - 14), (PADS[2] - 14, PADS[3] - 14)]:
        screw(img, yy, xx, sx_, sy_, 5)
    lx, ly = LIST_XY
    cov, ins = rrect(yy, xx, lx - 6, ly - 6, lx + LIST_W + 6, ly + LIST_ROW * LIST_ROWS + 6, 10)
    img[:] = mix(img, [0.02, 0.02, 0.025], cov)
    img[:] = mix(img, [0.16, 0.16, 0.15], np.clip(1 - ins / 2, 0, 1) * (ins > 0) * 0.8)
    for k in range(1, LIST_ROWS):                                    # faint row lines
        line = np.clip(1 - np.abs(yy - (ly + k * LIST_ROW)) / 0.7, 0, 1) * ((xx > lx + 4) & (xx < lx + LIST_W - 4))
        img[:] = mix(img, [0.12, 0.12, 0.11], line * 0.8)
    sy0, sy1 = 590, 604
    stripe = ((((xx + yy) // 14) % 2) == 0) & (yy >= sy0) & (yy < sy1) & (xx >= 829) & (xx < 1249)
    img[:] = mix(img, [0.05, 0.05, 0.05], ((yy >= sy0) & (yy < sy1) & (xx >= 829) & (xx < 1249)).astype(float) * 0.9)
    img[:] = mix(img, [0.80, 0.62, 0.08], stripe.astype(float) * 0.85)


def list_highlight_strip():
    """9 frames (one per list line) of the list window's size: transparent except an orange band on that line."""
    w, h = LIST_W, LIST_ROW * LIST_ROWS
    frames = []
    for r in range(LIST_ROWS):
        f = np.zeros((h, w, 4))
        y0, y1 = r * LIST_ROW + 1, (r + 1) * LIST_ROW - 1
        f[y0:y1, 2:w - 2, 0], f[y0:y1, 2:w - 2, 1], f[y0:y1, 2:w - 2, 2], f[y0:y1, 2:w - 2, 3] = 0.85, 0.42, 0.06, 0.55
        f[y0:y0 + 1, 2:w - 2, 3] = 0.9; f[y1 - 1:y1, 2:w - 2, 3] = 0.9
        f[y0:y1, 2:5, 3] = 1.0
        frames.append(f)
    return np.concatenate(frames, axis=0)


def slot_strip(w=96, h=42):
    """4 frames of a pad slot: 0 empty, 1 loaded, 2 playing (its pad is held), 3 flashing (LOAD is armed)."""
    yy, xx = grid(h, w)
    frames = []
    for st in range(4):
        cov, ins = rrect(yy, xx, 1, 1, w - 1, h - 1, 7)
        base = [0.045, 0.045, 0.05]
        face = np.zeros((h, w, 3)); face[:] = base
        edge = {0: ([0.20, 0.20, 0.19], 0.7), 1: ([0.55, 0.38, 0.12], 0.9), 2: ([1.0, 0.62, 0.12], 1.0), 3: ([1.0, 0.95, 0.75], 1.0)}[st]
        if st == 2: face = mix(face, [0.85, 0.40, 0.05], np.ones((h, w)) * 0.55)
        if st == 3: face = mix(face, [0.95, 0.70, 0.20], np.ones((h, w)) * 0.45)
        face = mix(face, edge[0], np.clip(1 - np.abs(ins - 1.5) / 1.3, 0, 1) * (ins > 0) * edge[1])
        frames.append(np.concatenate([face, cov[..., None]], axis=2))
    return np.concatenate(frames, axis=0)


# ---- sprites ----------------------------------------------------------------------------------------------------
def _down(a, ss):
    h, w = a.shape[0] // ss, a.shape[1] // ss
    return a.reshape(h, ss, w, ss, a.shape[2]).mean(axis=(1, 3))


def label_frame(size, theta, ss=2):
    """White record label (like the reference photo) with a small printed design so the turning is visible. RGBA."""
    s_ = size * ss
    yy, xx = grid(s_, s_)
    c = s_ / 2
    d = np.hypot(xx - c, yy - c) / ss
    phi = np.arctan2(xx - c, -(yy - c)) - theta
    phi = (phi + math.pi) % (2 * math.pi) - math.pi
    rgb = np.ones((s_, s_, 3)) * np.array([0.94, 0.93, 0.90])
    rgb = rgb * (1 - 0.10 * np.clip((d - 40) / 25, 0, 1))[..., None]                              # soft edge shading
    rgb = mix(rgb, [0.10, 0.10, 0.10], np.clip(LABEL_R - 3 - d, 0, 1) * np.clip(d - (LABEL_R - 5), 0, 1))   # thin black ring
    tri = (np.abs(phi) < (50 - d) * 0.016) * (d > 30) * (d < 50)                                   # pointer triangle at the top
    rgb = mix(rgb, [0.08, 0.08, 0.08], tri.astype(float))
    dash = ((np.sin(phi * 20) > 0) & (phi > 0.9) & (phi < 2.3)).astype(float) * np.clip(31 - d, 0, 1) * np.clip(d - 26, 0, 1)
    rgb = mix(rgb, [0.12, 0.12, 0.12], dash)
    line = np.clip(1.3 - np.abs(np.sin(phi - 1.5708) * d), 0, 1) * (np.cos(phi - 1.5708) > 0) * (d > 16) * (d < LABEL_R - 6)
    rgb = mix(rgb, [0.75, 0.10, 0.08], line * 0.9)                                                 # red radial mark
    rgb = mix(rgb, [0.62, 0.62, 0.60], np.clip(8 - d, 0, 1))
    rgb = mix(rgb, [0.03, 0.03, 0.03], np.clip(2.6 - d, 0, 1))
    a = np.clip(LABEL_R + 0.5 - d, 0, 1)[..., None]
    return _down(np.concatenate([rgb, a], axis=2), ss)


def label_strip(frames=72, size=LABEL_FS):
    return np.concatenate([label_frame(size, 2 * math.pi * i / frames) for i in range(frames)], axis=0)


def sticker_frame(size, j, ss=3):
    """frame 0 is blank; frame j >= 1 puts the dot at angle (j-1) x 10 degrees clockwise from 12 o'clock."""
    s = size * ss
    out = np.zeros((s, s, 4))
    if j > 0:
        yy, xx = grid(s, s)
        a = math.radians(10 * (j - 1))
        cx, cy = s / 2 + STICKER_R * ss * math.sin(a), s / 2 - STICKER_R * ss * math.cos(a)
        r = 9 * ss
        out[..., 3] = disc(yy, xx, cx + 2 * ss, cy + 3 * ss, r + ss) * 0.45
        body = disc(yy, xx, cx, cy, r)
        shade = 0.82 + 0.25 * np.clip(1 - np.hypot(xx - (cx - r * 0.35), yy - (cy - r * 0.35)) / (r * 1.2), 0, 1)
        col = np.stack([1.0 * shade, 0.85 * shade, 0.05 * shade], axis=-1)
        col = mix(col, [0.12, 0.09, 0.0], ring(yy, xx, cx, cy, r - 1.6 * ss, r))
        col = mix(col, [1, 1, 0.85], disc(yy, xx, cx - r * 0.35, cy - r * 0.38, r * 0.22) * 0.7)
        out[..., :3] = mix(out[..., :3], col, body)
        out[..., 3] = np.maximum(out[..., 3], body)
    return _down(out, ss)


def sticker_strip(steps=36, size=STICKER_FS):
    return np.concatenate([sticker_frame(size, j) for j in range(steps + 1)], axis=0)


def arm_overlay():
    """Tonearm from the upper-left pedestal over to the record (the reference photo, turned 90 degrees). 440 x 300 RGBA."""
    w, h = 440, 300
    yy, xx = grid(h, w)
    col = np.zeros((h, w, 3)); al = np.zeros((h, w))

    def stamp(cx, cy, r, c, a=1.0):
        x0, x1, y0, y1 = max(int(cx - r - 2), 0), min(int(cx + r + 3), w), max(int(cy - r - 2), 0), min(int(cy + r + 3), h)
        if x1 <= x0 or y1 <= y0: return
        m = disc(yy[y0:y1, x0:x1], xx[y0:y1, x0:x1], cx, cy, r) * a
        col[y0:y1, x0:x1] = col[y0:y1, x0:x1] * (1 - m[..., None]) + np.asarray(c) * m[..., None]
        al[y0:y1, x0:x1] = np.maximum(al[y0:y1, x0:x1], m)

    bx, by = T(545, 125)                                                       # pedestal centre (upper left)
    cx0, cy0 = REC_C
    L = 270.0                                                                  # arm length (photo: ~297 px x 0.908)
    # needle lands on the outer groove: |N - B| = L and |N - C| = 172 (pick the upper solution)
    d_bc = math.hypot(cx0 - bx, cy0 - by)
    a_ = (L * L - 172.0 ** 2 + d_bc * d_bc) / (2 * d_bc)
    hh = math.sqrt(max(L * L - a_ * a_, 0.0))
    ux, uy = (cx0 - bx) / d_bc, (cy0 - by) / d_bc
    nx, ny = bx + a_ * ux + hh * uy, by + a_ * uy - hh * ux
    if ny > by + a_ * uy:                                                      # choose the solution that is to the right/up
        nx, ny = bx + a_ * ux - hh * uy, by + a_ * uy + hh * ux
    dx, dy = nx - bx, ny - by
    dl = math.hypot(dx, dy); dx /= dl; dy /= dl
    px_, py_ = -dy, dx                                                         # perpendicular
    P0 = (bx + dx * 8, by + dy * 8)
    P3 = (nx - dx * 34, ny - dy * 34)                                          # where the headshell starts
    P1 = (P0[0] + dx * 70 + px_ * 22, P0[1] + dy * 70 + py_ * 22)             # gentle S
    P2 = (P3[0] - dx * 70 - px_ * 22, P3[1] - dy * 70 - py_ * 22)
    pts = []
    for t in np.linspace(0, 1, 300):
        bb = [(1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t ** 2 * (1 - t), t ** 3]
        pts.append((sum(b * q[0] for b, q in zip(bb, (P0, P1, P2, P3))), sum(b * q[1] for b, q in zip(bb, (P0, P1, P2, P3)))))
    sh = np.zeros((h, w))                                                      # shadow cast on the deck / record
    for (x, y) in pts + [(nx, ny)]:
        sh = np.maximum(sh, disc(yy, xx, x + 9, y + 13, 6))
    sh = blur(sh, 4) * 0.5
    for (x, y) in pts:
        stamp(x, y, 5.2, (0.30, 0.30, 0.29))
    for (x, y) in pts:
        stamp(x - 0.4, y - 0.5, 4.0, (0.70, 0.70, 0.68))
    for (x, y) in pts:
        stamp(x - 1.1, y - 1.3, 1.9, (0.96, 0.96, 0.94))
    # headshell: black with a chrome edge, then cartridge and stylus
    for t in np.linspace(0, 1, 40):
        x, y = P3[0] + (nx - P3[0]) * t, P3[1] + (ny - P3[1]) * t
        stamp(x, y, 9.5, (0.05, 0.05, 0.05))
    for t in np.linspace(0, 1, 40):
        x, y = P3[0] + (nx - P3[0]) * t, P3[1] + (ny - P3[1]) * t
        stamp(x - 1, y - 1, 7.0, (0.14, 0.14, 0.13)); stamp(x - 2, y - 2, 2.2, (0.78, 0.78, 0.75))
    stamp(nx, ny, 8.0, (0.04, 0.04, 0.04)); stamp(nx, ny, 5.2, (0.55, 0.55, 0.52)); stamp(nx + dx * 3, ny + dy * 3, 2.4, (0.95, 0.5, 0.1))
    # pivot collar and counterweight (behind the pedestal, away from the record)
    stamp(bx, by, 30, (0.45, 0.45, 0.43)); stamp(bx, by, 26, (0.78, 0.78, 0.76)); stamp(bx - 1, by - 1, 16, (0.55, 0.55, 0.53))
    stamp(bx - 2, by - 2, 7, (0.92, 0.92, 0.90)); stamp(bx, by, 3, (0.10, 0.10, 0.10))
    for k in np.linspace(34, 66, 44):
        stamp(bx - dx * k, by - dy * k, 12.5, (0.22, 0.22, 0.21))
    for k in np.linspace(34, 66, 44):
        stamp(bx - dx * k - 0.8, by - dy * k - 1.0, 10.0, (0.70, 0.70, 0.68)); stamp(bx - dx * k - 2, by - dy * k - 2.4, 3.2, (0.97, 0.97, 0.95))
    for k in (42, 55):                                                          # two engraved grooves on the weight
        stamp(bx - dx * k, by - dy * k, 10.4, (0.30, 0.30, 0.29))
        stamp(bx - dx * k - 0.8, by - dy * k - 1.0, 9.2, (0.68, 0.68, 0.66))
    out = np.zeros((h, w, 4))
    out[..., :3] = col
    out[..., 3] = np.clip(np.maximum(al, sh * (1 - al)), 0, 1)
    out[..., :3][al < 0.02] = 0.0
    return out


def knob_frame(size, t, ss=3):
    s = size * ss
    yy, xx = grid(s, s)
    c = s / 2
    k = size / 76.0
    d = np.hypot(xx - c, yy - c) / ss
    phi = np.arctan2(xx - c, -(yy - c))
    ang = math.radians(-135 + 270 * t)
    a = np.maximum(np.zeros((s, s)), disc(yy, xx, c + 2 * k * ss, c + 3 * k * ss, 34 * k * ss) * 0.45)
    rgb = np.zeros((s, s, 3))
    body = disc(yy, xx, c, c, 34 * k * ss)
    rgb = mix(rgb, [0.10, 0.10, 0.09], body)
    shade = 0.62 - 0.30 * ((xx - c) * 0.7 + (yy - c) * 0.7) / (31 * k * ss)
    cream = np.stack([0.82 * shade, 0.77 * shade, 0.63 * shade], axis=-1)
    knurl = 0.05 * np.sin((phi - ang) * 40) * np.clip((d - 25 * k) / 2, 0, 1)
    cream = cream + knurl[..., None]
    rgb = mix(rgb, cream, disc(yy, xx, c, c, 31 * k * ss))
    cap = np.stack([0.9 * (shade + 0.08), 0.85 * (shade + 0.08), 0.70 * (shade + 0.08)], axis=-1) + (0.012 * np.sin(d * 2.4))[..., None]
    rgb = mix(rgb, cap, disc(yy, xx, c, c, 24 * k * ss))
    rgb = mix(rgb, [0.30, 0.28, 0.22], ring(yy, xx, c, c, 24 * k * ss - 1.2, 24 * k * ss) * 0.8)
    px, py = np.sin(ang), -np.cos(ang)
    along = ((xx - c) * px + (yy - c) * py) / ss
    perp = np.abs(-(xx - c) * py + (yy - c) * px) / ss
    ptr = np.clip(2.4 - perp, 0, 1) * np.clip(along - 5 * k, 0, 1) * np.clip(26 * k - along, 0, 1)
    rgb = mix(rgb, [0.06, 0.05, 0.05], ptr)
    a = np.maximum(a, body)
    return _down(np.concatenate([rgb, a[..., None]], axis=2), ss)


def knob_strip(frames=64, size=KNOB_FS):
    return np.concatenate([knob_frame(size, i / (frames - 1)) for i in range(frames)], axis=0)


def meter_images(w=METER_W, h=METER_H, leds=12):
    """horizontal LED row, lit from the left"""
    yy, xx = grid(h, w)
    off = np.zeros((h, w, 4)); on = np.zeros((h, w, 4))
    step = w / leds
    for i in range(leds):
        x0 = i * step + 1.2
        x1 = x0 + step - 2.4
        col = (0.12, 0.92, 0.22) if i < 8 else ((0.96, 0.80, 0.10) if i < 10 else (0.96, 0.16, 0.10))
        cov = rrect(yy, xx, x0, 2, x1, h - 2, 1.5)[0]
        on[..., :3] = mix(on[..., :3], col, cov); on[..., 3] = np.maximum(on[..., 3], cov)
        off[..., :3] = mix(off[..., :3], np.array(col) * 0.16 + 0.02, cov); off[..., 3] = np.maximum(off[..., 3], cov)
    return off, on


def xfader_cap(w=40, h=44):
    yy, xx = grid(h, w)
    out = np.zeros((h, w, 4))
    cov, ins = rrect(yy, xx, 4, 4, w - 4, h - 4, 5)
    sh = rrect(yy, xx, 6, 7, w - 2, h, 5)[0] * 0.5
    shade = 0.78 - 0.18 * (xx - w / 2) / (w / 2)
    ridges = 0.04 * np.sin(yy * 1.6)
    body = np.stack([(shade + ridges) * 0.92, (shade + ridges) * 0.88, (shade + ridges) * 0.76], axis=-1)
    body = mix(body, [0.10, 0.09, 0.08], np.clip(1 - np.abs(xx - w / 2) / 1.5, 0, 1) * (np.abs(yy - h / 2) < 16))
    body = mix(body, [0.25, 0.23, 0.18], np.clip(1 - ins / 1.5, 0, 1) * (ins > 0))
    out[..., :3] = body
    out[..., 3] = np.maximum(cov, sh * (1 - cov))
    out[..., :3] *= (cov > 0.02)[..., None]
    return out


def hamster_images(w=84, h=40):
    yy, xx = grid(h, w)
    imgs = []
    for on in (False, True):
        out = np.zeros((h, w, 4))
        cov, ins = rrect(yy, xx, 4, 4, w - 4, h - 4, 10)
        out[..., :3] = mix(np.zeros((h, w, 3)), [0.06, 0.06, 0.055], cov)
        rim = np.clip(1 - ins / 2, 0, 1) * (ins > 0)
        out[..., :3] = mix(out[..., :3], [0.38, 0.37, 0.34], rim * 0.8)
        lx = (w - 24) if on else 24
        lever = rrect(yy, xx, lx - 14, 8, lx + 14, h - 8, 8)
        col = np.array([0.95, 0.45, 0.08]) if on else np.array([0.55, 0.55, 0.52])
        shade = 0.7 + 0.3 * np.clip((h / 2 - yy) / (h / 2), -1, 1)
        out[..., :3] = mix(out[..., :3], col[None, None, :] * shade[..., None], lever[0])
        out[..., 3] = cov
        imgs.append(out)
    return imgs


def pad_images(w=PAD_S, h=PAD_H):
    s = w
    yy, xx = grid(h, w)
    imgs = []
    rng = np.random.default_rng(7)
    grain = fnoise(h, w, 1, rng) * 0.012
    for on in (False, True):
        cov, ins = rrect(yy, xx, 1, 1, w - 1, h - 1, 8)
        base = 0.075 + grain
        face = np.stack([base, base, base * 1.05], axis=-1)
        lit = np.clip(1 - ins / 6, 0, 1) * (ins > 0)
        tl = np.clip(((xx - w / 2) * -0.7 + (yy - h / 2) * -0.7) / w, -1, 1)
        face = face + (lit * tl * 0.10)[..., None]
        if on:
            g = np.clip(1 - np.hypot(xx - w / 2, yy - h / 2) / (s * 0.55), 0, 1)
            glow = np.stack([1.0 * g, 0.50 * g, 0.08 * g], axis=-1)
            face = np.clip(face + glow * 1.1, 0, 1)
            face = mix(face, [1.0, 0.9, 0.6], np.clip(1 - np.hypot(xx - w / 2, yy - h / 2) / (s * 0.20), 0, 1) * 0.5)
        else:
            face = mix(face, [0.22, 0.22, 0.21], np.clip(1 - np.abs(ins - 4) / 1.1, 0, 1) * 0.5)
        imgs.append(np.concatenate([face, cov[..., None]], axis=2))
    return imgs


def button_images(w, h, accent=(0.95, 0.45, 0.08)):
    yy, xx = grid(h, w)
    imgs = []
    for on in (False, True):
        cov, ins = rrect(yy, xx, 1, 1, w - 1, h - 1, 7)
        base = np.stack([np.full((h, w), 0.09)] * 3, axis=-1)
        lit = np.clip(1 - ins / 3, 0, 1) * (ins > 0)
        base = mix(base, [0.40, 0.39, 0.36], lit * 0.7)
        if on:
            g = np.clip(1 - np.abs(yy - h / 2) / (h * 0.7), 0, 1) * 0.55
            base = np.clip(base + np.stack([accent[0] * g, accent[1] * g, accent[2] * g], axis=-1), 0, 1)
            base = mix(base, accent, lit * 0.8)
        imgs.append(np.concatenate([base, cov[..., None]], axis=2))
    return imgs
