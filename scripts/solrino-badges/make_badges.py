"""Draws Solrino's animated badges from code, with no outside artwork.

The mark is an S inside an outlined emblem, usually inside a cut-corner neon frame
on a dark backing with drifting lines. Writes 1x-4x animated WebP files (18, 36,
72 and 144 pixels) for each badge, plus preview sheets.

    python scripts/solrino-badges/make_badges.py <output folder> [only these badges]
"""
import ctypes
import math
import os
import sys

import numpy as np
from PIL import Image

MASTER = 288
FRAMES = 90
FRAME_MS = 33
# The sizes the app expects for each image of a badge
SIZES = {"1x": 18, "2x": 36, "3x": 72, "4x": 144}
TAU = 2 * math.pi

# The emblem around the S: shield, sun, diamond or star
SHAPE = "star"

yy, xx = np.mgrid[0:MASTER, 0:MASTER].astype(np.float32)
X = (xx + 0.5) / MASTER * 2 - 1
Y = (yy + 0.5) / MASTER * 2 - 1
PIXEL = 2 / MASTER


def rgb(value):
    return np.array([int(value[i:i + 2], 16) / 255 for i in (1, 3, 5)], np.float32)


def mix(a, b, t):
    return a + (b - a) * t


def smooth(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0, 1)
    return t * t * (3 - 2 * t)


def fill(d, soft=1.2):
    return np.clip(0.5 - d / (PIXEL * soft), 0, 1)


# ---------------------------------------------------------------- Shapes


def sd_polygon(points):
    """Signed distance to a polygon: negative inside."""
    pts = np.array(points, np.float32)
    best = np.full(X.shape, 1e9, np.float32)
    inside = np.zeros(X.shape, bool)
    for i in range(len(pts)):
        ax, ay = pts[i]
        bx, by = pts[i - 1]
        ex, ey = bx - ax, by - ay
        wx, wy = X - ax, Y - ay
        t = np.clip((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1)
        dx, dy = wx - ex * t, wy - ey * t
        best = np.minimum(best, dx * dx + dy * dy)
        crosses = (ay > Y) != (by > Y)
        inside ^= crosses & (X < ex * (Y - ay) / (ey if ey != 0 else 1e-6) + ax)
    d = np.sqrt(best)
    return np.where(inside, -d, d)


def sd_rounded_square(half, radius):
    qx = np.abs(X) - half + radius
    qy = np.abs(Y) - half + radius
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    return outside + np.minimum(np.maximum(qx, qy), 0) - radius


def sd_cut_corners(half, cut, radius):
    """A square with its corners cut off, slightly rounded."""
    h, c = half - radius, cut
    return sd_polygon([(-h + c, -h), (h - c, -h), (h, -h + c), (h, h - c),
                       (h - c, h), (-h + c, h), (-h, h - c), (-h, -h + c)]) - radius


def sd_capsule(a, b, radius):
    ax, ay = a
    bx, by = b
    ex, ey = bx - ax, by - ay
    wx, wy = X - ax, Y - ay
    t = np.clip((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1)
    return np.hypot(wx - ex * t, wy - ey * t) - radius


def shield_points():
    right = [(0.0, -0.72), (0.2, -0.66), (0.42, -0.62), (0.68, -0.6)]
    # The side curves in from the shoulder down to the point
    for i in range(1, 25):
        t = i / 24
        x = (1 - t) ** 2 * 0.68 + 2 * t * (1 - t) * 0.68
        y = (1 - t) ** 2 * -0.1 + 2 * t * (1 - t) * 0.46 + t * t * 0.84
        right.append((x, y))
    return right + [(-x, y) for x, y in reversed(right[1:-1])]


# Moves the star down so it sits in the middle of its frame
STAR_DOWN = 0.03


def star_points():
    points = []
    for i in range(10):
        a = -math.pi / 2 + math.pi * i / 5
        # Sharp points, with a body wide enough to hold the S
        r = 0.94 if i % 2 == 0 else 0.56
        points.append((r * math.cos(a), r * math.sin(a) + STAR_DOWN))
    return points


def sd_emblem(scale, cy, shape=None):
    """The outer edge of the emblem. Negative inside."""
    shape = shape or SHAPE
    if shape == "shield":
        d = sd_polygon([(x * scale, y * scale + cy) for x, y in shield_points()]) - 0.02 * scale
    elif shape == "diamond":
        d = sd_polygon([(0, -0.84 * scale + cy), (0.8 * scale, cy), (0, 0.84 * scale + cy),
                        (-0.8 * scale, cy)]) - 0.04 * scale
    elif shape == "star":
        d = sd_polygon([(x * scale, y * scale + cy) for x, y in star_points()]) - 0.05 * scale
    else:
        d = np.hypot(X, Y - cy) - 0.66 * scale
    return d


def sd_letter(scale, cy, thickness, size=0.14):
    """A bold S with rounded ends."""
    r = size * scale
    points = []
    for i in range(41):
        a = math.radians(-22 - 248 * i / 40)
        points.append((r * 1.12 * math.cos(a), cy - r + r * math.sin(a)))
    for i in range(1, 41):
        a = math.radians(-90 + 248 * i / 40)
        points.append((r * 1.12 * math.cos(a), cy + r + r * math.sin(a)))
    d = np.full(X.shape, 1e9, np.float32)
    half = thickness / 2
    for a, b in zip(points, points[1:]):
        d = np.minimum(d, sd_capsule(a, b, half))
    return d


# Where the S sits in each emblem: how far down, its size and its thickness
LETTER_FIT = {
    "shield": (0.0, 0.15, 0.2),
    "sun": (0.0, 0.14, 0.19),
    "diamond": (0.0, 0.115, 0.165),
    "star": (STAR_DOWN, 0.145, 0.205),
}
RING = {"shield": 0.15, "sun": 0.14, "diamond": 0.15, "star": 0.135}


def mark(scale, shape=None):
    """The emblem outline with the S inside it, as a single shape."""
    shape = shape or SHAPE
    outer = sd_emblem(scale, 0.0, shape)
    ring = np.maximum(outer, -(outer + RING[shape] * scale))
    down, size, thickness = LETTER_FIT[shape]
    letter = sd_letter(scale, down * scale, thickness * scale, size)
    if shape == "star":
        # A solid star with the S cut out of it
        return np.maximum(outer, -letter)
    d = np.minimum(ring, letter)
    if shape == "sun":
        # Short rays around the ring
        angle = np.arctan2(Y, X)
        r = np.hypot(X, Y)
        wave = smooth(0.35, 0.8, np.cos(12 * angle))
        rays = np.maximum(r - (0.72 + 0.24 * wave) * scale, 0.72 * scale - r)
        rays = np.maximum(rays, 0.45 - wave)
        d = np.minimum(d, rays)
    return d


def frame_ring():
    outer = sd_cut_corners(0.97, 0.3, 0.05)
    return np.maximum(outer, -(outer + 0.14))


def sd_gear():
    angle = np.arctan2(Y, X)
    r = np.hypot(X, Y)
    teeth = smooth(-0.55, 0.55, np.cos(8 * (angle + TAU / 16)))
    return r - (0.79 + 0.17 * teeth)


# ---------------------------------------------------------------- Colour


def backing(tint, phase, lines=True):
    """Near-black backing with faint lines drifting through it."""
    color = np.ones((MASTER, MASTER, 3), np.float32) * 0.012 + tint * 0.05
    if lines:
        for offset, height in ((0.0, -0.45), (2.1, 0.05), (4.0, 0.52)):
            wave = Y - height - 0.13 * np.sin(X * 2.6 + phase + offset)
            color = color + tint * (np.exp(-(wave / 0.016) ** 2) * 0.28)[..., None]
    return color


def two_tone(phase, dark, light, offset=0.0, stretch=1.0):
    """Two colours flowing diagonally through the shape."""
    t = 0.5 + 0.5 * np.sin((X * 1.15 + Y * 1.5) * 2.1 * stretch - phase + offset)
    return mix(dark, light, smooth(0.42, 0.97, t)[..., None])


def caustics(phase):
    """Thin bright veins that wander, like light on water."""
    a = np.sin(X * 6.1 + 2.1 * np.sin(Y * 3.7 + phase) + phase)
    b = np.sin(Y * 7.3 - 1.9 * np.sin(X * 4.3 - 2 * phase) - phase)
    c = np.sin((X + Y) * 9.1 + 1.5 * np.sin((X - Y) * 5.3 + 2 * phase))
    n = (a + b + c) / 3
    return np.exp(-(n / 0.075) ** 2)


def chrome(d, base, light, dark, phase):
    """Liquid metal: reflections slide over a rounded surface."""
    height = np.sin(np.clip(-d / 0.13, 0, 1) * math.pi / 2)
    gy, gx = np.gradient(height, PIXEL)
    flow = (np.sin(X * 2.3 + 1.8 * np.sin(Y * 1.9 + phase) + phase) +
            np.sin(Y * 2.7 - 1.6 * np.sin(X * 2.1 - phase) - phase)) * 0.5
    reflect = gx * 0.07 - gy * 0.1 + flow * 0.8
    bands = 0.5 + 0.5 * np.sin(reflect * 3.0 + phase)
    highlight = smooth(0.72, 0.98, bands)[..., None]
    out = mix(dark, base, smooth(0.08, 0.72, bands)[..., None])
    return out + (light - out) * highlight


def wobble(image, phase, amount=0.016):
    """Bends a finished frame gently, so metal looks liquid."""
    dx = amount * np.sin(Y * 4.0 + phase) / PIXEL
    dy = amount * np.sin(X * 3.6 + phase * 2 + 1.0) / PIXEL
    sx = np.clip(np.rint(xx + dx), 0, MASTER - 1).astype(np.int32)
    sy = np.clip(np.rint(yy + dy), 0, MASTER - 1).astype(np.int32)
    return image[sy, sx]


def over(under, color, coverage):
    return under + (color - under) * coverage[..., None]


# ---------------------------------------------------------------- Badges

PLATE_FILL = fill(sd_cut_corners(0.97, 0.3, 0.05))
# Badges with no frame sit on nothing: their glow fades out long before the edge
FREE_SCALE = 0.88


def free_alpha():
    around = smooth(0.2, 0.0, sd_emblem(FREE_SCALE, 0.0))
    return np.minimum(around, smooth(1.0, 0.88, np.maximum(np.abs(X), np.abs(Y))))


def framed(frame_dark, frame_light, mark_dark, mark_light, tint, offset=1.3):
    """Rounded neon frame with the heart inside, colours flowing through both."""
    ring = fill(frame_ring())
    symbol = fill(mark(0.84))

    def run(f):
        phase = TAU * f
        out = backing(tint, phase)
        out = over(out, two_tone(phase, frame_dark, frame_light, stretch=0.8), ring)
        out = over(out, two_tone(phase, mark_dark, mark_light, offset), symbol)
        return out, PLATE_FILL

    return run


def supporter():
    return framed(rgb("#b80f2a"), rgb("#ff6b5c"), rgb("#d3122f"), rgb("#ff9c8a"), rgb("#e0162f"))


def supporter_plus():
    return framed(rgb("#00918f"), rgb("#7ff5e3"), rgb("#00a8a3"), rgb("#a8fff0"), rgb("#00c2b8"))


def supporter_plus_alt():
    ring = fill(frame_ring())
    symbol = fill(mark(0.84))
    tint = rgb("#00c2b8")
    base = rgb("#00857f")
    white = rgb("#e6fffb")

    def run(f):
        phase = TAU * f
        veins = caustics(phase)[..., None]
        out = backing(tint, phase)
        out = over(out, base + (white - base) * veins * 0.95, ring)
        pulse = 0.9 + 0.1 * math.sin(phase * 2)
        out = over(out, (mix(base, rgb("#5ff0dc"), 0.55) + (white - base) * veins * 0.45) * pulse, symbol)
        return out, PLATE_FILL

    return run


def supporter_plus_plus():
    return framed(rgb("#d98a00"), rgb("#ffe680"), rgb("#efa100"), rgb("#fff0a8"), rgb("#ffb000"))


def supporter_plus_plus_alt():
    symbol = fill(mark(FREE_SCALE))
    alpha = free_alpha()
    tint = rgb("#ffb000")
    pink = rgb("#e89400")
    light = rgb("#fff3bf")

    def run(f):
        phase = TAU * f
        out = backing(tint, phase)
        stripes = np.sin((X * 1.5 + Y * 2.3) * 3.2 - phase * 2)
        amount = smooth(0.32, 0.5, stripes)[..., None] * (0.55 + 0.45 * math.sin(phase * 1 + 0.6) ** 2)
        out = over(out, pink + (light - pink) * amount, symbol)
        return out, alpha

    return run


def founder():
    symbol = fill(mark(FREE_SCALE))
    alpha = free_alpha()
    grey = rgb("#a8646e")
    white = rgb("#ffe7dc")
    amber = rgb("#6b2a34")

    def run(f):
        phase = TAU * f
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.015
        glowing = np.exp(-(X * X + Y * Y) * 1.3)[..., None]
        out = out + amber * glowing * (0.45 + 0.2 * math.sin(phase))
        out = over(out, two_tone(phase, grey, white, stretch=1.1), symbol)
        return out, alpha

    return run


def top_donor():
    return framed(rgb("#c4006b"), rgb("#ff7ac6"), rgb("#ff2e74"), rgb("#ffc46b"), rgb("#e0007a"))


def top_donor_alt():
    d = mark(FREE_SCALE)
    cover = fill(d)
    alpha = free_alpha()
    tint = rgb("#e0007a")
    base, light, dark = rgb("#f0288c"), rgb("#ffe6f3"), rgb("#5c0033")

    def run(f):
        phase = TAU * f
        out = backing(tint, phase)
        top = chrome(d, base, light, dark, phase)
        image = np.dstack([top, cover])
        bent = wobble(image, phase)
        return over(out, bent[..., :3], bent[..., 3]), alpha

    return run


def developer():
    return framed(rgb("#5f9e00"), rgb("#d9ff70"), rgb("#7fc400"), rgb("#ecff9e"), rgb("#8fd400"))


def contributor():
    gear = sd_gear()
    # The star hole stays inside the gear's body, so the gear holds together
    hole = sd_emblem(0.7, 0.0)
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = np.maximum(sd_letter(0.78, down * 0.7, thickness * 0.78, size), hole)
    d = np.minimum(np.maximum(gear, -hole), letter)
    cover = fill(d)
    base, light, dark = rgb("#4f86f0"), rgb("#eaf2ff"), rgb("#14275c")

    def run(f):
        phase = TAU * f
        top = chrome(d, base, light, dark, phase)
        image = np.dstack([top, cover])
        bent = wobble(image, phase)
        return bent[..., :3], bent[..., 3]

    return run


def obsidian():
    """A black star, lit around its edge, with a white S glowing at its center."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    alpha = free_alpha()
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter_cover = fill(letter)
    star_glow = np.exp(-np.maximum(outer, 0) / 0.13) * (outer > -0.001)
    letter_glow = np.exp(-np.maximum(letter, 0) / 0.075) * (letter > -0.001)
    rim = np.exp(-((outer + 0.022) / 0.024) ** 2)
    body = mix(rgb("#17171c"), rgb("#050507"), np.clip((Y + 0.8) / 1.6, 0, 1)[..., None])
    white = rgb("#ffffff")
    silver = rgb("#7d8088")
    haze = rgb("#c9d0e0")

    def run(f):
        phase = TAU * f
        breath = 0.72 + 0.28 * math.sin(phase)
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.008
        out = out + haze * (star_glow * 0.62 * breath)[..., None]
        out = over(out, body, cover)
        # A soft shine crossing the black, then light running around the edge
        band = np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * (f % 1.0))) / 0.3) ** 2)
        out = out + white * (band * cover * 0.2)[..., None]
        edge = two_tone(phase, silver, white, stretch=1.1)
        out = out + (edge - out) * (rim * cover * (0.75 + 0.25 * breath))[..., None]
        # The S burns brightest
        out = out + white * (letter_glow * (1 - letter_cover) * cover * 0.65 * breath)[..., None]
        out = over(out, white, letter_cover)
        return out, alpha

    return run


def rainbow(t):
    """A vivid colour wheel. Adding a whole number to t comes back to the same colour."""
    return np.stack([0.5 + 0.5 * np.cos(TAU * (t + shift)) for shift in (0.0, 0.33, 0.67)], -1)


def sparkle(cx, cy, size):
    """A four-pointed glint: 1 at its center, fading to 0 along thin arms."""
    if size <= 0:
        return np.zeros(X.shape, np.float32)
    reach = np.sqrt(np.abs(X - cx)) + np.sqrt(np.abs(Y - cy))
    return np.clip(1 - reach / math.sqrt(size), 0, 1)


def special():
    """A holographic star: a rainbow runs around its edge, the night sky turns inside it."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    alpha = free_alpha()
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter_cover = fill(letter)
    letter_glow = np.exp(-np.maximum(letter, 0) / 0.09) * (letter > -0.001)
    star_glow = np.exp(-np.maximum(outer, 0) / 0.09) * (outer > -0.001)
    rim = smooth(-0.1, -0.045, outer)
    angle = np.arctan2(Y, X)
    white = rgb("#ffffff")
    night = rgb("#1a0638")
    # Tiny stars in the body, each twinkling on its own beat
    rng = np.random.default_rng(7)
    dots = [(rng.uniform(-0.6, 0.6), rng.uniform(-0.6, 0.6), rng.uniform(0, 1)) for _ in range(26)]
    # Glints sit in the gaps between the star's points
    glints = [(0.5, -0.56, 0.0, 0.2), (-0.66, 0.2, 0.36, 0.17), (0.0, 0.68, 0.68, 0.15)]

    def run(f):
        phase = TAU * f
        wheel = rainbow(angle / TAU * 2 - f)
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.01
        out = out + wheel * (star_glow * (0.8 + 0.2 * math.sin(phase * 2)))[..., None]
        # The body: deep violet with slow bands of colour crossing it
        body = night + rainbow((X * 0.5 + Y * 0.7) * 0.9 + f) * (0.2 + 0.17 * np.sin((X - Y) * 3.0 + phase))[..., None]
        for dx, dy, beat in dots:
            light = max(0.0, math.sin(TAU * (f + beat))) ** 2
            body = body + white * (np.exp(-(((X - dx) ** 2 + (Y - dy) ** 2) / 0.00028)) * light)[..., None]
        out = over(out, body, cover)
        # A band of white light sweeps across once per loop
        band = np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * f)) / 0.2) ** 2)
        out = out + white * (band * cover * 0.38)[..., None]
        # The edge, bright, with a little white flowing through it
        edge = mix(wheel, white, 0.22 * (0.5 + 0.5 * np.sin(angle * 5 - phase))[..., None])
        out = over(out, edge, rim * cover)
        out = out + white * (letter_glow * (1 - letter_cover) * cover * 0.55)[..., None]
        out = over(out, white, letter_cover)
        # Glints go on top and may reach past the star
        shown = alpha
        for cx, cy, beat, reach in glints:
            power = max(0.0, math.sin(TAU * (f + beat))) ** 3
            glint = smooth(0.0, 0.4, sparkle(cx, cy, reach * power))
            out = over(out, white, glint)
            shown = np.maximum(shown, glint)
        return out, shown

    return run


def bug_hunter():
    """A terminal-green star with flickering pixels that glitches apart now and then."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    alpha = free_alpha()
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter_cover = fill(letter)
    letter_glow = np.exp(-np.maximum(letter, 0) / 0.08) * (letter > -0.001)
    star_glow = np.exp(-np.maximum(outer, 0) / 0.08) * (outer > -0.001)
    rim = smooth(-0.085, -0.04, outer)
    green = rgb("#39ff88")
    deep = rgb("#02180c")
    pale = rgb("#d6ffe6")
    # A grid of pixels, each blinking on its own beat
    cell = 24
    rng = np.random.default_rng(11)
    beats = rng.uniform(0, 1, (MASTER // cell + 1, MASTER // cell + 1))
    speeds = rng.integers(1, 4, beats.shape)
    row = (yy // cell).astype(np.int32)
    col = (xx // cell).astype(np.int32)
    gap = ((xx % cell) > 2) & ((yy % cell) > 2)

    def run(f):
        phase = TAU * f
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.008
        out = out + green * (star_glow * 0.5)[..., None]
        lit = np.maximum(0, np.sin(TAU * (speeds[row, col] * f + beats[row, col])) - 0.45) / 0.55
        out = over(out, deep + green * (lit * gap * 0.5)[..., None], cover)
        out = over(out, mix(green, pale, 0.2 + 0.2 * math.sin(phase * 2)), rim * cover)
        out = out + green * (letter_glow * (1 - letter_cover) * cover * 0.6)[..., None]
        out = over(out, pale, letter_cover)
        # Two short bursts in each loop tear the picture sideways
        burst = max(0.0, 1 - abs(f - 0.3) / 0.045) + max(0.0, 1 - abs(f - 0.72) / 0.035)
        shown = alpha
        if burst > 0:
            image = np.dstack([out, alpha])
            frame = int(round(f * FRAMES))
            tear = np.random.default_rng(frame + 100)
            top = 0
            while top < MASTER:
                height = int(tear.integers(10, 44))
                shift = int(tear.integers(-26, 27) * burst)
                image[top:top + height] = np.roll(image[top:top + height], shift, axis=1)
                top += height
            # Red and blue split apart while it tears
            split = int(8 * burst) + 1
            image[..., 0] = np.roll(image[..., 0], split, axis=1)
            image[..., 2] = np.roll(image[..., 2], -split, axis=1)
            out, shown = image[..., :3], image[..., 3]
        return out, shown

    return run


def moderator():
    """A steel star holding a green shield, watched over by a passing scan line."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    alpha = free_alpha()
    shield = sd_polygon([(x * 0.62, y * 0.62 + 0.03) for x, y in shield_points()]) - 0.012
    shield_cover = fill(shield)
    shield_edge = fill(np.abs(shield) - 0.026)
    letter = sd_letter(0.66, 0.05, 0.135, 0.145)
    letter_cover = fill(letter)
    star_glow = np.exp(-np.maximum(outer, 0) / 0.085) * (outer > -0.001)
    rim = smooth(-0.075, -0.035, outer)
    steel_dark, steel, steel_light = rgb("#343a44"), rgb("#7f8894"), rgb("#e6ebf2")
    green = rgb("#22e27a")
    pine = rgb("#04331d")
    white = rgb("#f2fff8")
    brushed = 0.5 + 0.5 * np.sin(Y * 44 + np.sin(X * 3.0) * 1.5)

    def run(f):
        phase = TAU * f
        beat = 0.62 + 0.38 * math.sin(phase * 2)
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.008
        out = out + green * (star_glow * 0.5 * beat)[..., None]
        # Brushed steel, with a soft shine sliding across
        shine = np.exp(-(((X * 0.8 + Y * 0.6) - (-2.0 + 4.0 * f)) / 0.42) ** 2)
        body = mix(steel_dark, steel, (0.28 + 0.2 * brushed)[..., None])
        body = body + (steel_light - body) * (shine * 0.7)[..., None]
        out = over(out, body, cover)
        out = over(out, mix(steel_light, green, 0.35 + 0.45 * beat), rim * cover)
        # The shield, with a pulse widening out from its middle
        ring = np.exp(-((np.hypot(X, Y - 0.03) - 0.56 * (2 * f % 1.0)) / 0.05) ** 2) * (1 - (2 * f % 1.0))
        out = over(out, pine + green * (0.12 + ring * 0.6)[..., None], shield_cover)
        out = over(out, green * (0.75 + 0.25 * beat), shield_edge * cover)
        out = over(out, white, letter_cover)
        # A green line scans down the star, and back up
        y = -0.95 + 1.9 * (0.5 - 0.5 * math.cos(phase))
        scan = np.exp(-((Y - y) / 0.022) ** 2)
        trail = np.exp(-((Y - y) / 0.13) ** 2)
        out = out + green * ((scan * 0.85 + trail * 0.16) * cover)[..., None]
        return out, alpha

    return run


def sd_spider(cx, cy, heading, size, gait):
    """A spider seen from above: a fat body, a small head and eight jointed legs."""
    c, s = math.cos(heading), math.sin(heading)

    def place(forward, side):
        return (cx + (forward * c - side * s) * size, cy + (forward * s + side * c) * size)

    # The body is one long oval
    ax, ay = place(-0.32, 0)
    body_x = (X - ax) * c + (Y - ay) * s
    body_y = -(X - ax) * s + (Y - ay) * c
    d = (np.hypot(body_x / (0.46 * size), body_y / (0.33 * size)) - 1) * 0.33 * size
    hx, hy = place(0.24, 0)
    d = np.minimum(d, np.hypot(X - hx, Y - hy) - 0.2 * size)
    # Four legs a side, moving in alternating pairs
    leg = 0.05 * size
    for side in (-1, 1):
        for i, spread in enumerate((48, 16, -16, -48)):
            swing = 13 * math.sin(gait + math.pi * ((i + (side > 0)) % 2))
            a = math.radians(spread + swing)
            base = place(0.1, side * 0.1)
            knee = place(0.1 + 0.62 * math.sin(a), side * (0.1 + 0.62 * math.cos(a)))
            b = a - math.radians(24 * (1 if spread > 0 else -1))
            foot = (knee[0] + (0.6 * (math.sin(b) * c - side * math.cos(b) * s)) * size,
                    knee[1] + (0.6 * (math.sin(b) * s + side * math.cos(b) * c)) * size)
            d = np.minimum(d, sd_capsule(base, knee, leg))
            d = np.minimum(d, sd_capsule(knee, foot, leg * 0.8))
    eyes = [place(0.33, -0.08), place(0.33, 0.08)]
    return d, eyes


def walk_star(u, scale):
    """A point and its direction, part way round the star's outline (u = 0 to 1)."""
    points = [(x * scale, y * scale) for x, y in star_points()]
    t = (u % 1.0) * len(points)
    i = int(t)
    a, b = points[i], points[(i + 1) % len(points)]
    k = t - i
    return a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, math.atan2(b[1] - a[1], b[0] - a[0])


def spooky():
    """A black star wrapped in cobweb, with spiders scuttling over it and one dropping down."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    alpha = free_alpha()
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter_cover = fill(letter)
    letter_glow = np.exp(-np.maximum(letter, 0) / 0.055) * (letter > -0.001)
    star_glow = np.exp(-np.maximum(outer, 0) / 0.1) * (outer > -0.001)
    rim = smooth(-0.075, -0.035, outer)
    black = rgb("#0b0608")
    ember = rgb("#ff6a00")
    amber = rgb("#ffb52e")
    pale = rgb("#fff0b8")
    violet = rgb("#7a2be2")
    silk = rgb("#d8d2cc")
    blood = rgb("#ff1a1a")
    # The cobweb: threads out from the middle, tied together by sagging rings
    angle = np.arctan2(Y - STAR_DOWN, X)
    r = np.hypot(X, Y - STAR_DOWN)
    spokes = TAU / 10
    near = np.abs((angle + spokes / 2) % spokes - spokes / 2) * r
    threads = np.exp(-(near / 0.009) ** 2)
    sag = r * (1 + 0.13 * np.abs(np.sin(angle * 5)))
    rings = np.exp(-((((sag + 0.05) % 0.17) - 0.085) / 0.0075) ** 2)
    web = np.clip(threads + rings, 0, 1) * smooth(0.14, 0.26, r)

    def run(f):
        phase = TAU * f
        flicker = 0.74 + 0.15 * math.sin(phase * 3) + 0.08 * math.sin(phase * 7 + 1.0) + 0.05 * math.sin(phase * 13 + 2.0)
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.008
        out = out + violet * (star_glow * 0.5)[..., None]
        candle = np.exp(-(X * X * 3.2 + (Y - 0.2) ** 2 * 3.6)) * flicker
        smoke = 0.5 + 0.5 * np.sin(X * 5.0 + 1.8 * np.sin(Y * 3.0 + phase) + Y * 2.0 + phase)
        wisps = smooth(0.6, 0.98, smoke) * smooth(0.9, -0.4, Y)
        body = black + ember * (candle * 0.42)[..., None] + violet * (wisps * 0.2)[..., None]
        body = body + (silk - body) * (web * 0.42 * (0.6 + 0.4 * flicker))[..., None]
        out = over(out, body, cover)
        out = over(out, mix(ember, amber, 0.3 + 0.5 * flicker) * (0.55 + 0.45 * flicker), rim * cover)
        out = out + ember * (letter_glow * (1 - letter_cover) * cover * 0.7 * flicker)[..., None]
        hot = mix(amber, pale, smooth(0.4, -0.3, Y)[..., None] * (0.35 + 0.65 * flicker))
        out = over(out, hot, letter_cover)

        # Spiders dart, freeze, then dart again, all the way round the edge
        crawlers = []
        for start, turns, big, beat in ((0.04, 1, 0.3, 0.0), (0.56, -1, 0.25, 0.37)):
            u = start + turns * (f + math.sin(TAU * 3 * (f + beat)) / (TAU * 3) * 0.92)
            x, y, heading = walk_star(u, FREE_SCALE * 0.97)
            if turns < 0:
                heading += math.pi
            crawlers.append((x, y, heading, big, TAU * 9 * f + beat * TAU))
        # A big one lowers itself from the top point, hangs over the S, and climbs back
        drop = 0.5 - 0.5 * math.cos(phase)
        hang_y = -0.6 + 0.6 * drop ** 1.6
        sway = 0.035 * math.sin(phase * 2)
        crawlers.append((sway, hang_y, math.pi / 2, 0.36, TAU * 4 * f))
        thread = fill(sd_capsule((0.0, -0.86), (sway, hang_y), 0.009))
        out = over(out, silk, thread * 0.85)
        shown = np.maximum(alpha, thread)
        for x, y, heading, big, gait in crawlers:
            d, eyes = sd_spider(x, y, heading, big, gait)
            # A thin lit edge keeps the black spider visible on the black star
            out = over(out, ember * 0.8, fill(d - 0.011))
            spider = fill(d)
            out = over(out, rgb("#050304"), spider)
            shown = np.maximum(shown, fill(d - 0.011))
            for ex, ey in eyes:
                out = over(out, blood, fill(np.hypot(X - ex, Y - ey) - 0.05 * big - 0.004))
        return out, shown

    return run


def winter_2026():
    """A cut-ice star with light turning across its facets and snow falling past it."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    alpha = free_alpha()
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter_cover = fill(letter)
    star_glow = np.exp(-np.maximum(outer, 0) / 0.09) * (outer > -0.001)
    rim = smooth(-0.07, -0.03, outer)
    deep = rgb("#123f86")
    ice = rgb("#8fd8ff")
    white = rgb("#ffffff")
    navy = rgb("#0a2150")
    frost = rgb("#bfeaff")
    # Ten flat faces, like a cut gem
    angle = np.arctan2(Y - STAR_DOWN, X) + math.pi / 2
    sector = np.floor((angle % TAU) / (TAU / 10))
    middles = (sector + 0.5) * TAU / 10
    # Frost grows in from the edge as fine feathers
    feathers = 0.5 + 0.5 * np.sin(angle * 40 + np.sin(X * 19) * 2 + np.sin(Y * 23) * 2)
    rime = smooth(-0.24, -0.07, outer) * smooth(0.45, 0.95, feathers)
    rng = np.random.default_rng(26)
    # Whole-number speeds, so each flake lands back where it began
    dots = [(rng.uniform(-1, 1), rng.uniform(0, 1), rng.integers(1, 3), rng.uniform(0, 1), rng.uniform(0.012, 0.026)) for _ in range(30)]
    flakes = [(-0.7, 0.1, 0.1), (0.72, 0.55, 0.095), (0.05, 0.8, 0.085)]

    def run(f):
        phase = TAU * f
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.01
        out = out + ice * (star_glow * (0.55 + 0.15 * math.sin(phase * 2)))[..., None]
        light = 0.5 + 0.5 * np.cos(middles - phase)
        body = mix(deep, ice, (light ** 1.6)[..., None])
        # Every other face catches a little more light, so the cuts read as edges
        body = body + (white - body) * (((sector % 2) * 0.13) * cover)[..., None]
        body = body + (white - body) * (rime * 0.55)[..., None]
        out = over(out, body, cover)
        out = over(out, mix(frost, white, 0.5 + 0.5 * math.sin(phase * 2)), rim * cover)
        out = over(out, navy, letter_cover)
        shown = alpha
        # Soft snow
        snow = np.zeros(X.shape, np.float32)
        for x, start, speed, sway, big in dots:
            y = ((start + speed * f) % 1.0) * 2.1 - 1.05
            px = x + 0.05 * math.sin(TAU * (f + sway))
            fade = min(1.0, (1.05 - abs(y)) / 0.15)
            snow = np.maximum(snow, fill(np.hypot(X - px, Y - y) - big) * fade)
        # A few big six-armed flakes drifting and turning
        for x, start, big in flakes:
            y = ((start + f) % 1.0) * 2.2 - 1.1
            turn = phase + start * TAU
            px = x + 0.07 * math.sin(phase + start * TAU)
            d = np.full(X.shape, 1e9, np.float32)
            for arm in range(3):
                a = turn / 3 + arm * math.pi / 3
                dx, dy = math.cos(a) * big, math.sin(a) * big
                d = np.minimum(d, sd_capsule((px - dx, y - dy), (px + dx, y + dy), 0.012))
            fade = min(1.0, max(0.0, (1.1 - abs(y)) / 0.15))
            snow = np.maximum(snow, fill(d) * fade)
        out = over(out, white, snow)
        shown = np.maximum(shown, snow)
        # A glint on the ice
        power = max(0.0, math.sin(phase * 2 + 0.6)) ** 4
        glint = smooth(0.0, 0.4, sparkle(-0.3, -0.34, 0.2 * power))
        out = over(out, white, glint)
        return out, np.maximum(shown, glint)

    return run


def sd_heart(cx, cy, size, tilt=0.0):
    """A heart centered on (cx, cy), with its point downward."""
    c, s = math.cos(tilt), math.sin(tilt)
    dx, dy = (X - cx) / size, (Y - cy) / size
    px = np.abs(dx * c + dy * s)
    py = 0.5 - (-dx * s + dy * c)
    upper = np.hypot(px - 0.25, py - 0.75) - math.sqrt(2) / 4
    middle = 0.5 * np.maximum(px + py, 0)
    lower = np.sqrt(np.minimum(px * px + (py - 1) ** 2,
                               (px - middle) ** 2 + (py - middle) ** 2))
    return np.where(px + py > 1, upper, lower * np.sign(px - py)) * size


def valentines_2027():
    """A pink glass star with a heart beating inside it and small hearts rising past."""
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter_cover = fill(letter)
    letter_glow = np.exp(-np.maximum(letter, 0) / 0.05) * (letter > -0.001)
    deep = rgb("#8f0a44")
    rose = rgb("#ff3d86")
    blush = rgb("#ffc2dc")
    hot = rgb("#ff1f6d")
    white = rgb("#ffffff")
    # Top lit, bottom deep, like light through coloured glass
    glass = mix(rgb("#c9135f"), rgb("#5c0630"), smooth(-0.7, 0.7, Y)[..., None])
    # The curved shine across the top of the glass
    shine = smooth(0.1, -0.02, Y + 0.3 - 0.38 * (X + 0.1) ** 2) * smooth(-0.72, -0.5, Y)
    rng = np.random.default_rng(14)
    rising = []
    for i in range(9):
        side = -1 if i % 2 == 0 else 1
        rising.append((side * rng.uniform(0.48, 0.9), rng.uniform(0, 1), rng.uniform(0.09, 0.15),
                       rng.uniform(-0.5, 0.5), rng.uniform(0, 1)))

    def run(f):
        phase = TAU * f
        # Lub-dub, twice in each loop
        beat = 0.0
        for at, power in ((0.08, 1.0), (0.17, 0.6), (0.58, 1.0), (0.67, 0.6)):
            beat = max(beat, power * math.exp(-((f - at) / 0.035) ** 2))
        scale = FREE_SCALE * (1 + 0.03 * beat)
        outer = sd_emblem(scale, 0.0)
        cover = fill(outer)
        star_glow = np.exp(-np.maximum(outer, 0) / 0.1) * (outer > -0.001)
        rim = smooth(-0.075, -0.03, outer)
        shown = np.minimum(smooth(0.2, 0.0, outer),
                           smooth(1.0, 0.88, np.maximum(np.abs(X), np.abs(Y))))

        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.012
        out = out + hot * (star_glow * (0.55 + 0.4 * beat))[..., None]
        out = over(out, glass, cover)

        heart = sd_heart(0.0, STAR_DOWN + 0.03, 0.86 * (1 + 0.1 * beat))
        out = out + hot * (np.exp(-np.maximum(heart, 0) / 0.07) * cover * (0.55 + 0.45 * beat))[..., None]
        fill_color = mix(rgb("#ff2b78"), rgb("#ff7fb0"), smooth(0.45, -0.45, Y)[..., None])
        fill_color = fill_color + (blush - fill_color) * (0.35 * beat)
        out = over(out, fill_color, fill(heart) * cover)
        out = over(out, blush, fill(np.abs(heart + 0.012) - 0.012) * cover * 0.9)

        out = out + white * (shine * cover * (0.26 + 0.08 * math.sin(phase)))[..., None]
        band = np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * f)) / 0.16) ** 2)
        out = out + white * (band * cover * 0.32)[..., None]
        out = over(out, mix(blush, white, 0.35 + 0.4 * beat), rim * cover)

        out = over(out, deep * 0.55, letter_glow * (1 - letter_cover) * cover * 0.5)
        out = over(out, white, letter_cover)

        for x, start, big, tilt, sway in rising:
            t = (start + f) % 1.0
            y = 1.05 - 2.1 * t
            px = x + 0.05 * math.sin(TAU * (f + sway))
            fade = min(1.0, t / 0.14, (1 - t) / 0.14)
            d = sd_heart(px, y, big, tilt + 0.25 * math.sin(TAU * (f + sway)))
            cover_heart = fill(d) * fade
            out = over(out, mix(rose, blush, 0.45), cover_heart)
            shown = np.maximum(shown, cover_heart)
        return out, shown

    return run


def sd_mini_star(cx, cy, size, turn):
    points = []
    for i in range(10):
        a = -math.pi / 2 + turn + math.pi * i / 5
        r = size if i % 2 == 0 else size * 0.46
        points.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return sd_polygon(points)


def friend_of_solrino():
    """A warm gold star, softly lit, with two small stars circling it together."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    alpha = free_alpha()
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter_cover = fill(letter)
    star_glow = np.exp(-np.maximum(outer, 0) / 0.13) * (outer > -0.001)
    rim = smooth(-0.07, -0.03, outer)
    # A rounded surface, so the light can move across it
    height = np.sin(np.clip(-outer / 0.2, 0, 1) * math.pi / 2)
    gy, gx = np.gradient(height, PIXEL)
    amber = rgb("#d97706")
    honey = rgb("#ffb72b")
    cream = rgb("#fff4c9")
    brown = rgb("#3f1d00")
    halo = rgb("#ffa928")
    tilt = -0.2
    tc, ts = math.cos(tilt), math.sin(tilt)

    def orbit(angle):
        x, y = 0.86 * math.cos(angle), 0.56 * math.sin(angle)
        return x * tc - y * ts, x * ts + y * tc + STAR_DOWN, math.sin(angle)

    def draw(out, shown, angle, big, color):
        # A short tail of fading dust behind each small star
        for step in range(1, 7):
            tx, ty, _ = orbit(angle - step * 0.11)
            dust = fill(np.hypot(X - tx, Y - ty) - 0.024 * (1 - step / 8)) * (0.75 - step * 0.1)
            out = over(out, cream, dust)
            shown = np.maximum(shown, dust)
        x, y, z = orbit(angle)
        d = sd_mini_star(x, y, big * (1 + 0.28 * z), angle)
        coverage = fill(d)
        glow = np.exp(-np.maximum(d, 0) / 0.035) * (d > -0.001)
        out = out + halo * (glow * 0.8)[..., None]
        out = over(out, color, coverage)
        return out, np.maximum(shown, np.maximum(coverage, glow * 0.8))

    def run(f):
        phase = TAU * f
        breath = 0.8 + 0.2 * math.sin(phase * 2)
        shown = alpha
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.012
        out = out + halo * (star_glow * 0.75 * breath)[..., None]

        pair = [(phase, 0.155, cream), (phase + math.pi, 0.13, rgb("#ffe08a"))]
        for angle, big, color in pair:
            if math.sin(angle) < 0:
                out, shown = draw(out, shown, angle, big, color)

        # Soft light circling over the rounded gold
        lx, ly = math.cos(phase) * 0.55, math.sin(phase) * 0.55 - 0.6
        lit = np.clip(0.5 + (gx * lx + gy * ly) * 0.085, 0, 1)
        warm = 1 - smooth(0.0, 0.95, np.hypot(X, Y - STAR_DOWN))
        body = mix(amber * 0.82, honey, np.clip(lit * 0.95 + warm * 0.5 - 0.1, 0, 1)[..., None])
        body = mix(body, cream, smooth(0.66, 1.0, lit)[..., None] * 0.85)
        out = over(out, body, cover)
        band = np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * f)) / 0.22) ** 2)
        out = out + cream * (band * cover * 0.28)[..., None]
        out = over(out, cream, rim * cover * (0.75 + 0.25 * breath))
        out = over(out, brown, letter_cover)

        for angle, big, color in pair:
            if math.sin(angle) >= 0:
                out, shown = draw(out, shown, angle, big, color)
        return out, shown

    return run


def bilinear(field, sx, sy):
    x0 = np.clip(np.floor(sx).astype(np.int32), 0, MASTER - 2)
    y0 = np.clip(np.floor(sy).astype(np.int32), 0, MASTER - 2)
    tx = np.clip(sx - x0, 0, 1)
    ty = np.clip(sy - y0, 0, 1)
    top = field[y0, x0] * (1 - tx) + field[y0, x0 + 1] * tx
    bottom = field[y0 + 1, x0] * (1 - tx) + field[y0 + 1, x0 + 1] * tx
    return top * (1 - ty) + bottom * ty


def black_hole():
    """A star being dragged into a black hole, its points bending into the spin."""
    star = sd_emblem(FREE_SCALE, 0.0)
    r = np.hypot(X, Y)
    angle = np.arctan2(Y, X)
    safe_r = np.maximum(r, 0.02)
    horizon = 0.37
    hole = fill(r - horizon)
    ring = np.exp(-((r - horizon - 0.018) / 0.018) ** 2)
    ring_wide = np.exp(-np.maximum(r - horizon, 0) / 0.075) * (r > horizon - 0.01)
    letter = sd_letter(0.52, 0.0, 0.108, 0.16)
    letter_cover = fill(letter)
    letter_glow = np.exp(-np.maximum(letter, 0) / 0.03) * (letter > -0.001)
    white = rgb("#ffffff")
    gold = rgb("#ffd27a")
    orange = rgb("#ff6a13")
    magenta = rgb("#b5179e")
    violet = rgb("#3a0ca3")
    # Hottest at the inner edge, cooling outward
    heat = np.clip((r - horizon) / 0.5, 0, 1)[..., None]
    disk = np.where(heat < 0.3, mix(gold, orange, heat / 0.3),
                    np.where(heat < 0.65, mix(orange, magenta, (heat - 0.3) / 0.35),
                             mix(magenta, violet, (heat - 0.65) / 0.35)))
    # The side spinning toward you looks brighter
    doppler = 0.72 + 0.48 * np.cos(angle - 2.5)
    rng = np.random.default_rng(66)
    debris = [(rng.uniform(0, TAU), rng.uniform(0, 1), rng.uniform(0.009, 0.019)) for _ in range(30)]

    def run(f):
        phase = TAU * f
        # Bend the star: the nearer the middle, the more it's twisted
        twist = (1.3 + 0.22 * math.sin(phase)) * np.clip(1.1 - r, 0, 1) ** 1.2
        a = angle - twist
        sx = (r * np.cos(a) + 1) * MASTER / 2 - 0.5
        sy = (r * np.sin(a) + 1) * MASTER / 2 - 0.5
        outer = bilinear(star, sx, sy)
        cover = fill(outer)
        rim = smooth(-0.06, -0.025, outer)
        star_glow = np.exp(-np.maximum(outer, 0) / 0.1) * (outer > -0.001)
        shown = np.minimum(smooth(0.2, 0.0, outer),
                           smooth(1.0, 0.88, np.maximum(np.abs(X), np.abs(Y))))

        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.006
        out = out + (violet + magenta * 0.45) * (star_glow * 0.75)[..., None]

        # Spiral arms of hot gas falling inward
        spiral = 0.5 + 0.5 * np.sin(3 * angle + 7.5 * np.log(safe_r) + phase)
        fine = 0.5 + 0.5 * np.sin(8 * angle + 15 * np.log(safe_r) + 2 * phase)
        gas = (smooth(0.25, 0.95, spiral) * 0.85 + fine * 0.22) * (1 - heat[..., 0] * 0.55)
        body = rgb("#07020f") + disk * (gas * doppler)[..., None]
        out = over(out, body, cover)
        edge = mix(gold, magenta, smooth(0.35, 0.95, r)[..., None])
        out = over(out, edge * doppler[..., None], rim * cover)

        # Light that has been bent round the hole
        out = out + gold * (ring_wide * doppler * 0.95)[..., None]
        out = over(out, rgb("#000000"), hole)
        out = out + (white * 0.6 + gold * 0.5) * (ring * (0.65 + 0.4 * doppler))[..., None]

        # Sparks spiralling in, stretched thin as they fall
        for start_angle, start, big in debris:
            t = (start + f) % 1.0
            radius = 1.02 * (1 - t) ** 0.8 + horizon * (1 - (1 - t) ** 0.8)
            spin = start_angle + 2.2 / max(radius, 0.1)
            x, y = radius * math.cos(spin), radius * math.sin(spin)
            fade = min(1.0, t / 0.12, (1 - t) / 0.08)
            spark = fill(np.hypot(X - x, Y - y) - big * (0.6 + radius)) * fade
            spark = spark * (r > horizon)
            out = over(out, mix(gold, white, 0.5), spark)
            shown = np.maximum(shown, spark)

        out = out + white * (letter_glow * (1 - letter_cover) * hole * 0.5)[..., None]
        out = over(out, white, letter_cover)
        return out, shown

    return run


def supporter_plus_plus_plus():
    """The top supporter tier: pearly platinum with a clean shine and glints on the frame."""
    ring = fill(frame_ring())
    symbol = fill(mark(0.84))
    metal = np.maximum(ring, symbol)
    white = rgb("#ffffff")
    tint = rgb("#a8b8ff")
    glints = [(-0.66, -0.66, 0.0), (0.66, -0.66, 0.34), (0.66, 0.66, 0.67)]

    def run(f):
        phase = TAU * f
        out = backing(tint, phase)
        out = over(out, two_tone(phase, rgb("#5f6c8f"), white, stretch=0.8), ring)
        face = two_tone(phase, rgb("#a4b0d2"), white, 1.3)
        # A little colour moving through the metal, like the inside of a shell
        pearl = rainbow((X + Y) * 0.32 + f)
        face = mix(face, pearl, 0.2)
        out = over(out, face, symbol)
        band = np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * f)) / 0.15) ** 2)
        out = out + white * (band * metal * 0.75)[..., None]
        for x, y, offset in glints:
            t = (f + offset) % 1.0
            size = 0.3 * math.sin(math.pi * t / 0.26) if t < 0.26 else 0.0
            out = out + white * (sparkle(x, y, size) ** 2 * 1.6)[..., None]
        return out, PLATE_FILL

    return run


def number_one_donor():
    """Ruby star wearing a gold crown, with slow rays behind it. Only one person holds it."""
    scale = 0.74
    cy = 0.17
    outer = sd_emblem(scale, cy)
    cover = fill(outer)
    rim = smooth(-0.075, -0.03, outer)
    glow = np.exp(-np.maximum(outer, 0) / 0.1) * (outer > -0.001)
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = fill(sd_letter(scale, down * scale + cy, thickness * scale, size))
    crown_d = sd_polygon([(-0.4, -0.5), (-0.46, -0.86), (-0.2, -0.68), (0.0, -0.97), (0.2, -0.68),
                          (0.46, -0.86), (0.4, -0.5)]) - 0.012
    crown = fill(crown_d)
    crown_glow = np.exp(-np.maximum(crown_d, 0) / 0.05) * (crown_d > -0.001)
    jewels = [(-0.46, -0.87), (0.0, -0.97), (0.46, -0.87)]
    gold_dark, gold_light = rgb("#c57a00"), rgb("#fff0a0")
    ruby, ruby_light, ruby_dark = rgb("#d80f3c"), rgb("#ffd2c0"), rgb("#470012")
    white = rgb("#ffffff")
    angle = np.arctan2(Y - cy, X)
    radius = np.hypot(X, Y - cy)
    rays_fade = smooth(1.02, 0.5, radius) * smooth(0.3, 0.55, radius)
    edge = smooth(1.0, 0.9, np.maximum(np.abs(X), np.abs(Y)))

    def run(f):
        phase = TAU * f
        pulse = 0.75 + 0.25 * math.sin(phase * 2)
        rays = (0.5 + 0.5 * np.cos(angle * 10 + phase)) ** 3 * rays_fade * edge
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.012
        out = out + gold_light * (rays * 0.6)[..., None]
        out = out + rgb("#ff3355") * (glow * pulse * 0.8)[..., None]
        out = over(out, chrome(outer, ruby, ruby_light, ruby_dark, phase), cover)
        out = over(out, two_tone(phase, gold_dark, gold_light, stretch=0.9), rim * cover)
        out = over(out, two_tone(phase, gold_dark, gold_light, 1.3), letter)
        out = out + gold_light * (crown_glow * 0.55 * pulse)[..., None]
        out = over(out, two_tone(phase, gold_dark, gold_light, 0.4, 1.4), crown)
        band = np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * f)) / 0.14) ** 2)
        out = out + white * (band * np.maximum(crown, letter) * 0.7)[..., None]
        for i, (x, y) in enumerate(jewels):
            t = (f + i / 3) % 1.0
            size_now = 0.2 * math.sin(math.pi * t / 0.24) if t < 0.24 else 0.0
            out = out + white * (sparkle(x, y, size_now) ** 2 * 1.6)[..., None]
        shown = np.maximum.reduce([smooth(0.2, 0.0, outer) * edge, smooth(0.1, 0.0, crown_d) * edge,
                                   np.clip(rays * 0.7, 0, 1)])
        return out, shown

    return run


def beta_tester():
    """A glass star half full of bubbling liquid, like something still being tried out."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    rim = smooth(-0.07, -0.03, outer)
    glow = np.exp(-np.maximum(outer, 0) / 0.1) * (outer > -0.001)
    alpha = free_alpha()
    down, size, thickness = LETTER_FIT[SHAPE]
    letter_d = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter = fill(letter_d)
    halo = np.exp(-np.maximum(letter_d, 0) / 0.04) * (letter_d > -0.001)
    glass = mix(rgb("#1c2340"), rgb("#0c1024"), smooth(-0.7, 0.7, Y)[..., None])
    liquid_top, liquid_bottom = rgb("#39e6ff"), rgb("#7a2cff")
    cyan = rgb("#7df3ff")
    white = rgb("#ffffff")
    shine = smooth(0.1, -0.02, Y + 0.3 - 0.38 * (X + 0.1) ** 2) * smooth(-0.72, -0.5, Y)
    rng = np.random.default_rng(23)
    bubbles = [(rng.uniform(-0.42, 0.42), rng.uniform(0, 1), rng.uniform(0.035, 0.07), rng.uniform(0, 1))
               for _ in range(11)]

    def run(f):
        phase = TAU * f
        level = 0.02 + 0.035 * np.sin(X * 4 + phase) + 0.02 * np.sin(X * 7 - phase * 2)
        depth = Y - level
        liquid = smooth(-0.012, 0.012, depth)
        color = mix(liquid_top, liquid_bottom, smooth(0.0, 0.62, Y)[..., None])
        surface = np.exp(-(depth / 0.026) ** 2)

        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.012
        out = out + liquid_top * (glow * (0.55 + 0.15 * math.sin(phase * 2)))[..., None]
        out = over(out, glass, cover)
        out = over(out, color, liquid * cover)
        out = out + cyan * (surface * cover * 0.85)[..., None]

        for x, start, big, sway in bubbles:
            t = (start + f) % 1.0
            y = 0.72 - 0.86 * t
            px = x + 0.035 * math.sin(TAU * (f * 2 + sway))
            fade = min(1.0, t / 0.1, (1 - t) / 0.12)
            d = np.hypot(X - px, Y - y) - big
            ring = fill(np.abs(d) - 0.013)
            out = out + white * (ring * cover * fade * 0.9)[..., None]
            spot = fill(np.hypot(X - px + big * 0.3, Y - y + big * 0.3) - big * 0.28)
            out = out + white * (spot * cover * fade)[..., None]

        out = out + white * (shine * cover * 0.22)[..., None]
        out = over(out, mix(cyan, white, 0.3), rim * cover)
        out = over(out, rgb("#080b1c"), halo * (1 - letter) * cover * 0.7)
        out = over(out, white, letter)
        return out, alpha

    return run


def staff():
    """Black enamel lit red from below, with a neon edge and light racing around it."""
    outer = sd_emblem(FREE_SCALE, 0.0)
    cover = fill(outer)
    rim = smooth(-0.065, -0.03, outer)
    bevel = np.exp(-((outer + 0.1) / 0.025) ** 2) * cover
    glow = np.exp(-np.maximum(outer, 0) / 0.1) * (outer > -0.001)
    alpha = free_alpha()
    down, size, thickness = LETTER_FIT[SHAPE]
    letter_d = sd_letter(FREE_SCALE, down * FREE_SCALE, thickness * FREE_SCALE, size)
    letter = fill(letter_d)
    letter_glow = np.exp(-np.maximum(letter_d, 0) / 0.055) * (letter_d > -0.001)
    black = rgb("#0a0a0e")
    ember = rgb("#b30d1c")
    red = rgb("#ff1c2e")
    hot = rgb("#ff8a7a")
    white = rgb("#ffffff")
    # Red light rising from the bottom of the star
    lit = smooth(-0.15, 0.85, Y) ** 1.4
    body = mix(black, ember, lit[..., None] * 0.9)
    silver = mix(white, rgb("#c4c8d2"), smooth(-0.4, 0.5, Y)[..., None])
    angle = np.arctan2(Y, X)

    def run(f):
        phase = TAU * f
        pulse = 0.8 + 0.2 * math.sin(phase * 2)
        # Two lights chasing each other around the edge
        gap = np.mod(angle - phase + math.pi, TAU) - math.pi
        other = np.mod(angle - phase, TAU) - math.pi
        racing = np.exp(-(gap / 0.42) ** 2) + np.exp(-(other / 0.42) ** 2)

        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.012
        out = out + red * (glow * (0.5 * pulse + 0.75 * racing))[..., None]
        out = over(out, body, cover)
        out = out + red * (bevel * 0.32)[..., None]
        band = np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * f)) / 0.16) ** 2)
        out = out + white * (band * cover * 0.13)[..., None]
        out = over(out, mix(red, hot, np.clip(racing, 0, 1)[..., None]), rim * cover)
        out = out + white * (rim * cover * np.clip(racing, 0, 1) ** 2 * 0.75)[..., None]
        out = out + red * (letter_glow * (1 - letter) * cover * (0.75 * pulse))[..., None]
        out = over(out, silver, letter)
        out = out + white * (band * letter * 0.5)[..., None]
        return out, alpha

    return run


def supporter_plus_plus_plus_alt():
    """Supporter+++ with no frame: liquid platinum with colour moving through it."""
    d = mark(FREE_SCALE)
    cover = fill(d)
    alpha = free_alpha()
    tint = rgb("#a8b8ff")
    base, light, dark = rgb("#c3cbe4"), rgb("#ffffff"), rgb("#3c4563")
    white = rgb("#ffffff")
    tips = star_points()[::2]

    def run(f):
        phase = TAU * f
        out = backing(tint, phase)
        top = chrome(d, base, light, dark, phase)
        top = mix(top, rainbow((X - Y) * 0.34 + f), 0.2)
        image = np.dstack([top, cover])
        bent = wobble(image, phase)
        out = over(out, bent[..., :3], bent[..., 3])
        for i, (x, y) in enumerate(tips):
            t = (f + i / 5) % 1.0
            size = 0.22 * math.sin(math.pi * t / 0.2) if t < 0.2 else 0.0
            out = out + white * (sparkle(x * FREE_SCALE, y * FREE_SCALE, size) ** 2 * 1.6)[..., None]
        return out, alpha

    return run


def donor_of_the_month():
    """A magenta award star hanging over gold ribbon tails. Only one person holds it."""
    scale = 0.76
    cy = -0.13
    outer = sd_emblem(scale, cy)
    cover = fill(outer)
    rim = smooth(-0.075, -0.03, outer)
    glow = np.exp(-np.maximum(outer, 0) / 0.09) * (outer > -0.001)
    down, size, thickness = LETTER_FIT[SHAPE]
    letter = fill(sd_letter(scale, down * scale + cy, thickness * scale, size))
    left = sd_polygon([(-0.13, 0.2), (-0.46, 0.2), (-0.56, 0.93), (-0.36, 0.76), (-0.2, 0.93)]) - 0.012
    right = sd_polygon([(0.13, 0.2), (0.2, 0.93), (0.36, 0.76), (0.56, 0.93), (0.46, 0.2)]) - 0.012
    ribbon_d = np.minimum(left, right)
    ribbon = fill(ribbon_d)
    gold_dark, gold_light = rgb("#b56f00"), rgb("#fff0a0")
    pink, pink_light, pink_dark = rgb("#ec1f86"), rgb("#ffd9ec"), rgb("#54002e")
    white = rgb("#ffffff")
    edge = smooth(1.0, 0.9, np.maximum(np.abs(X), np.abs(Y)))
    shown = np.maximum(smooth(0.2, 0.0, outer) * edge, smooth(0.05, 0.0, ribbon_d) * edge)

    def run(f):
        phase = TAU * f
        pulse = 0.75 + 0.25 * math.sin(phase * 2)
        out = np.ones((MASTER, MASTER, 3), np.float32) * 0.012
        shade = two_tone(phase, gold_dark, gold_light, 0.8, 1.2)
        # Each tail is darker toward the star, as if it passes behind it
        shade = shade * (0.62 + 0.38 * smooth(0.2, 0.62, Y))[..., None]
        out = over(out, shade, ribbon)
        out = out + rgb("#ff3d9e") * (glow * pulse * 0.8)[..., None]
        out = over(out, chrome(outer, pink, pink_light, pink_dark, phase), cover)
        out = over(out, two_tone(phase, gold_dark, gold_light, stretch=0.9), rim * cover)
        out = over(out, two_tone(phase, gold_dark, gold_light, 1.3), letter)
        band = np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * f)) / 0.14) ** 2)
        out = out + white * (band * np.maximum(ribbon, letter) * 0.6)[..., None]
        return out, shown

    return run


def star_parts(scale=FREE_SCALE, cy=0.0):
    """The pieces most badges start from: the star, its rim and glow, and the S."""
    outer = sd_emblem(scale, cy)
    down, size, thickness = LETTER_FIT[SHAPE]
    letter_d = sd_letter(scale, down * scale + cy, thickness * scale, size)
    return {
        "outer": outer,
        "cover": fill(outer),
        "rim": smooth(-0.07, -0.03, outer),
        "glow": np.exp(-np.maximum(outer, 0) / 0.1) * (outer > -0.001),
        "letter": fill(letter_d),
        "halo": np.exp(-np.maximum(letter_d, 0) / 0.045) * (letter_d > -0.001),
    }


def empty_canvas():
    return np.ones((MASTER, MASTER, 3), np.float32) * 0.012


EDGE = smooth(1.0, 0.9, np.maximum(np.abs(X), np.abs(Y)))


def sd_crown(dy=0.0):
    return sd_polygon([(-0.4, -0.5 + dy), (-0.46, -0.86 + dy), (-0.2, -0.68 + dy), (0.0, -0.97 + dy),
                       (0.2, -0.68 + dy), (0.46, -0.86 + dy), (0.4, -0.5 + dy)]) - 0.012


def glints(points, f, size=0.2, span=0.24):
    """Short four-pointed flashes, taking turns."""
    total = np.zeros(X.shape, np.float32)
    for i, (x, y) in enumerate(points):
        t = (f + i / len(points)) % 1.0
        now = size * math.sin(math.pi * t / span) if t < span else 0.0
        total = total + sparkle(x, y, now) ** 2 * 1.6
    return total


def moving_shine(f, width=0.14):
    return np.exp(-(((X + Y * 0.55) - (-2.3 + 4.6 * f)) / width) ** 2)


def donor_of_the_year():
    """Dark gold star inside a full laurel wreath. Only one person holds it."""
    cy = 0.0
    parts = star_parts(0.62, cy)
    radius = 0.78
    leaves = np.full(X.shape, 1e9, np.float32)
    veins = np.full(X.shape, 1e9, np.float32)
    for side in (-1, 1):
        for i in range(8):
            theta = math.radians(10 + i * 19.5)
            x, y = side * radius * math.sin(theta), radius * math.cos(theta) + cy
            # Each leaf points up the branch, tilted outward then inward in turn
            tx, ty = side * math.cos(theta), -math.sin(theta)
            ox, oy = side * math.sin(theta), math.cos(theta)
            lean = 0.55 if i % 2 == 0 else -0.55
            lx, ly = tx + ox * lean, ty + oy * lean
            size = math.hypot(lx, ly)
            lx, ly = lx / size, ly / size
            length = 0.24
            # A leaf: wide in the middle, pointed at both ends
            along = (X - x) * lx + (Y - y) * ly
            across = -(X - x) * ly + (Y - y) * lx
            t = np.clip(along / length, 0, 1)
            width = 0.062 * np.maximum(np.sin(t * math.pi), 0) ** 0.7
            leaf = np.maximum(np.abs(across) - width, np.maximum(-along, along - length))
            leaves = np.minimum(leaves, leaf)
    stem = np.maximum(np.abs(np.hypot(X, Y - cy) - radius) - 0.016, -(Y - cy) - 0.2)
    wreath_d = np.minimum(leaves, stem)
    wreath = fill(wreath_d)
    leaf_dark, leaf_light = rgb("#a8891a"), rgb("#fff0a0")
    base, light, dark = rgb("#8a5f0a"), rgb("#ffe9a0"), rgb("#241500")
    bright = rgb("#fff3bd")
    white = rgb("#ffffff")
    shown = np.maximum(smooth(0.2, 0.0, parts["outer"]) * EDGE, smooth(0.03, 0.0, wreath_d) * EDGE)
    tips = [(x * 0.62, y * 0.62 + cy) for x, y in star_points()[::2]]

    def run(f):
        phase = TAU * f
        pulse = 0.75 + 0.25 * math.sin(phase * 2)
        out = empty_canvas()
        out = out + bright * (parts["glow"] * pulse * 0.7)[..., None]
        out = over(out, two_tone(phase, leaf_dark, leaf_light, 0.6, 1.1), wreath)
        out = over(out, chrome(parts["outer"], base, light, dark, phase), parts["cover"])
        out = over(out, bright, parts["rim"] * parts["cover"])
        out = over(out, rgb("#160c00"), parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.85)
        out = over(out, bright, parts["letter"])
        band = moving_shine(f)
        out = out + white * (band * np.maximum(wreath, parts["cover"]) * 0.45)[..., None]
        out = out + white * glints(tips, f)[..., None]
        return out, shown

    return run


def first_donor():
    """Blazing gold star with a red ribbon marked 1. There's only ever one."""
    parts = star_parts(0.82, -0.14)
    ribbon_d = sd_polygon([(-0.88, 0.47), (0.88, 0.47), (0.74, 0.67), (0.88, 0.87), (-0.88, 0.87),
                           (-0.74, 0.67)]) - 0.01
    ribbon = fill(ribbon_d)
    one_d = np.minimum(sd_capsule((0.0, 0.57), (0.0, 0.78), 0.047), sd_capsule((0.0, 0.57), (-0.08, 0.63), 0.04))
    one = fill(one_d)
    side_stars = np.minimum(sd_mini_star(-0.42, 0.67, 0.1, 0.0), sd_mini_star(0.42, 0.67, 0.1, 0.0))
    side = fill(side_stars)
    base, light, dark = rgb("#ffc21a"), rgb("#fffbe0"), rgb("#8c4f00")
    white = rgb("#ffffff")
    gold = rgb("#ffe680")
    shown = np.maximum(smooth(0.2, 0.0, parts["outer"]) * EDGE, smooth(0.03, 0.0, ribbon_d) * EDGE)
    tips = [(x * 0.82, y * 0.82 - 0.14) for x, y in star_points()[::2]][:3]

    def run(f):
        phase = TAU * f
        out = empty_canvas()
        out = out + rgb("#ffb000") * (parts["glow"] * (0.75 + 0.25 * math.sin(phase * 2)))[..., None]
        out = over(out, chrome(parts["outer"], base, light, dark, phase), parts["cover"])
        out = over(out, rgb("#fff4b8"), parts["rim"] * parts["cover"])
        out = over(out, rgb("#7a0a14"), parts["letter"])
        out = over(out, two_tone(phase, rgb("#a80c1a"), rgb("#ff4d42"), 0.5, 0.9), ribbon)
        out = over(out, gold, one)
        out = over(out, gold, side * ribbon)
        band = moving_shine(f)
        out = out + white * (band * ribbon * 0.6)[..., None]
        out = out + white * glints(tips, f)[..., None]
        return out, shown

    return run


def one_year_on_solrino():
    """A silver star with a ring that fills as the year goes round, then flares."""
    parts = star_parts(0.68, 0.0)
    radius = np.hypot(X, Y)
    ring_d = np.abs(radius - 0.88) - 0.05
    ring = fill(ring_d)
    around = np.mod(np.arctan2(Y, X) + math.pi / 2, TAU) / TAU
    base, light, dark = rgb("#c3c9d6"), rgb("#ffffff"), rgb("#3b4150")
    hot = rgb("#ff5a2e")
    bright = rgb("#ffc36b")
    white = rgb("#ffffff")
    shown = np.maximum(smooth(0.2, 0.0, parts["outer"]), smooth(0.05, 0.0, ring_d))

    def run(f):
        phase = TAU * f
        progress = min(1.0, f / 0.8)
        flare = math.exp(-((f - 0.84) / 0.05) ** 2)
        fade = 1.0 if f < 0.93 else max(0.0, 1 - (f - 0.93) / 0.07)
        filled = smooth(progress + 0.006, progress - 0.006, around) * fade
        head_x = 0.88 * math.sin(TAU * progress)
        head_y = -0.88 * math.cos(TAU * progress)
        head = np.exp(-(np.hypot(X - head_x, Y - head_y) / 0.1) ** 2) * (1 - flare) * (f < 0.8)

        out = empty_canvas()
        out = over(out, rgb("#2a2c33"), ring)
        color = mix(hot, bright, around[..., None])
        out = over(out, color, ring * filled)
        out = out + hot * (np.exp(-np.maximum(ring_d, 0) / 0.05) * filled * (0.5 + flare))[..., None]
        out = out + white * head[..., None]
        out = out + bright * (parts["glow"] * (0.3 + flare))[..., None]
        out = over(out, chrome(parts["outer"], base, light, dark, phase), parts["cover"])
        out = over(out, mix(rgb("#ffffff"), bright, flare), parts["rim"] * parts["cover"])
        out = over(out, rgb("#1b1e27"), parts["letter"])
        out = out + white * (moving_shine(f) * parts["cover"] * 0.25 + parts["cover"] * flare * 0.25)[..., None]
        return out, np.maximum(shown, head * EDGE)

    return run


def veteran():
    """Battered steel with a warm fire still burning in the scratches."""
    parts = star_parts()
    rng = np.random.default_rng(31)
    scratches = np.full(X.shape, 1e9, np.float32)
    for _ in range(15):
        cx, cy = rng.uniform(-0.55, 0.55), rng.uniform(-0.5, 0.65)
        angle = rng.uniform(-1.2, 1.2) + 0.6
        length = rng.uniform(0.14, 0.42)
        dx, dy = math.cos(angle) * length, math.sin(angle) * length
        scratches = np.minimum(scratches, sd_capsule((cx - dx, cy - dy), (cx + dx, cy + dy), rng.uniform(0.006, 0.012)))
    scratch = fill(scratches)
    lit = np.exp(-np.maximum(scratches, 0) / 0.03)
    brushed = 0.92 + 0.08 * np.sin(Y * 70 + X * 9 + 2 * np.sin(X * 6))
    dents = 0.86 + 0.14 * np.sin(X * 4.2 + 1) * np.sin(Y * 3.7 + 2)
    steel = mix(rgb("#a4a7ae"), rgb("#44464c"), smooth(-0.8, 0.8, Y + X * 0.3)[..., None]) * (brushed * dents)[..., None]
    alpha = free_alpha()
    amber = rgb("#ff9a2e")
    hot = rgb("#ffd27a")
    white = rgb("#ffffff")

    def run(f):
        phase = TAU * f
        breath = 0.72 + 0.28 * math.sin(phase * 2)
        out = empty_canvas()
        out = out + amber * (parts["glow"] * breath * 0.55)[..., None]
        out = over(out, steel, parts["cover"])
        out = over(out, rgb("#16161a"), scratch * parts["cover"] * 0.9)
        out = out + amber * (lit * parts["cover"] * breath * 0.5)[..., None]
        out = over(out, two_tone(phase, rgb("#6b3a17"), rgb("#e9a15d"), 0.2, 0.8), parts["rim"] * parts["cover"])
        out = over(out, rgb("#120a04"), parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.6)
        out = over(out, mix(amber, hot, breath), parts["letter"])
        out = out + white * (moving_shine(f, 0.12) * parts["cover"] * 0.14)[..., None]
        return out, alpha

    return run


def alpha_tester():
    """Beta Tester's rough older sibling: cracked glass and something unstable boiling inside."""
    parts = star_parts()
    alpha = free_alpha()
    rng = np.random.default_rng(41)
    cracks = np.full(X.shape, 1e9, np.float32)
    for start, end in (((-0.1, -0.62), (-0.26, -0.28)), ((-0.26, -0.28), (-0.08, -0.08)), ((-0.26, -0.28), (-0.52, -0.2)),
                       ((0.42, 0.3), (0.2, 0.46)), ((0.2, 0.46), (0.26, 0.68))):
        cracks = np.minimum(cracks, sd_capsule(start, end, 0.009))
    crack = fill(cracks)
    bubbles = [(rng.uniform(-0.45, 0.45), rng.uniform(0, 1), rng.uniform(0.04, 0.085), rng.uniform(0, 1)) for _ in range(12)]
    glass = mix(rgb("#26121c"), rgb("#0d0508"), smooth(-0.7, 0.7, Y)[..., None])
    top, bottom = rgb("#ff8a1a"), rgb("#7a0040")
    orange = rgb("#ff7a1a")
    white = rgb("#ffffff")

    def run(f):
        phase = TAU * f
        surge = math.sin(phase * 3)
        level = -0.08 + 0.07 * np.sin(X * 5 + phase * 2) + 0.05 * np.sin(X * 9 - phase * 3) + 0.03 * surge
        depth = Y - level
        liquid = smooth(-0.012, 0.012, depth)
        color = mix(top, bottom, smooth(-0.1, 0.65, Y)[..., None])
        flicker = 0.75 + 0.25 * (math.sin(phase * 7) > -0.6)
        out = empty_canvas()
        out = out + orange * (parts["glow"] * flicker * 0.75)[..., None]
        out = over(out, glass, parts["cover"])
        out = over(out, color, liquid * parts["cover"])
        out = out + rgb("#ffd27a") * (np.exp(-(depth / 0.024) ** 2) * parts["cover"] * 0.9)[..., None]
        for x, start, big, sway in bubbles:
            t = (start + f * 2) % 1.0
            y = 0.72 - 0.9 * t
            px = x + 0.05 * math.sin(TAU * (f * 3 + sway))
            fade = min(1.0, t / 0.1, (1 - t) / 0.1)
            ring = fill(np.abs(np.hypot(X - px, Y - y) - big) - 0.014)
            out = out + rgb("#ffe0b0") * (ring * parts["cover"] * fade * 0.9)[..., None]
        out = over(out, rgb("#ffd9b0"), crack * parts["cover"] * 0.9)
        out = over(out, orange * flicker, parts["rim"] * parts["cover"])
        out = over(out, rgb("#120408"), parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.7)
        out = over(out, white, parts["letter"])
        return out, alpha

    return run


def shrunk(image, scale, dy):
    """A frame drawn smaller and lower, to make room above it."""
    sx = np.rint(((X / scale) + 1) / 2 * MASTER - 0.5).astype(np.int32)
    sy = np.rint((((Y - dy) / scale) + 1) / 2 * MASTER - 0.5).astype(np.int32)
    inside = (sx >= 0) & (sx < MASTER) & (sy >= 0) & (sy < MASTER)
    return image[np.clip(sy, 0, MASTER - 1), np.clip(sx, 0, MASTER - 1)], inside


def top_bug_hunter():
    """The Bug Hunter badge wearing a gold crown. Only one person holds it."""
    base = bug_hunter()
    crown_d = sd_crown(0.05)
    crown = fill(crown_d)
    crown_glow = np.exp(-np.maximum(crown_d, 0) / 0.05) * (crown_d > -0.001)
    gold_dark, gold_light = rgb("#c57a00"), rgb("#fff0a0")
    jewels = [(-0.46, -0.82), (0.0, -0.92), (0.46, -0.82)]
    white = rgb("#ffffff")

    def run(f):
        phase = TAU * f
        inner, inner_alpha = base(f)
        moved, inside = shrunk(np.dstack([inner, inner_alpha]), 0.8, 0.17)
        out = moved[..., :3] * inside[..., None]
        shown = moved[..., 3] * inside
        out = out + gold_light * (crown_glow * 0.5)[..., None]
        out = over(out, two_tone(phase, gold_dark, gold_light, 0.4, 1.4), crown)
        out = out + white * (moving_shine(f) * crown * 0.7)[..., None]
        out = out + white * glints(jewels, f)[..., None]
        return out, np.maximum(shown, smooth(0.05, 0.0, crown_d) * EDGE)

    return run


def katana():
    """A black star. Every loop a blade cuts it clean in two, and it seals back up."""
    parts = star_parts()
    alpha = free_alpha()
    angle = math.radians(-32)
    tx, ty = math.cos(angle), math.sin(angle)
    nx, ny = -ty, tx
    across = X * nx + Y * ny
    along = X * tx + Y * ty
    body = mix(rgb("#26262c"), rgb("#050507"), smooth(-0.8, 0.8, along)[..., None])
    red = rgb("#ff1c2e")
    white = rgb("#ffffff")

    def run(f):
        phase = TAU * f
        out = empty_canvas()
        out = out + red * (parts["glow"] * 0.45)[..., None]
        out = over(out, body, parts["cover"])
        out = out + white * (np.exp(-((along - (-2 + 4 * f)) / 0.1) ** 2) * parts["cover"] * 0.12)[..., None]
        out = over(out, red, parts["rim"] * parts["cover"])
        out = over(out, white, parts["letter"])

        # The cut: each half slides along the blade's line, then closes
        split = 0.17 * math.exp(-((f - 0.3) / 0.075) ** 2) if f > 0.21 else 0.0
        side = np.sign(across)
        dx = (tx * side * split + nx * side * split * 0.3) / PIXEL
        dy = (ty * side * split + ny * side * split * 0.3) / PIXEL
        sx = np.clip(np.rint(xx - dx), 0, MASTER - 1).astype(np.int32)
        sy = np.clip(np.rint(yy - dy), 0, MASTER - 1).astype(np.int32)
        cut = out[sy, sx]
        shown = alpha[sy, sx]

        streak_head = -1.5 + 3.0 * ((f - 0.16) / 0.07)
        if 0.16 < f < 0.3:
            tail = smooth(streak_head + 0.02, streak_head - 0.1, along) * smooth(0.3, 0.2, np.float32(f)).item()
            streak = np.exp(-(across / 0.013) ** 2) * tail * EDGE
            cut = cut + white * (streak * 2)[..., None]
            cut = cut + red * (np.exp(-(across / 0.06) ** 2) * tail * EDGE * 0.8)[..., None]
            shown = np.maximum(shown, np.clip(streak * 1.5, 0, 1))
        seam = math.exp(-((f - 0.42) / 0.14) ** 2) if f > 0.3 else 0.0
        cut = cut + red * (np.exp(-(across / 0.016) ** 2) * parts["cover"] * seam * 1.1)[..., None]
        return cut, shown

    return run


def halo():
    """A white-gold star with a glowing ring floating above it and light falling down."""
    scale, cy = 0.74, 0.2
    parts = star_parts(scale, cy)
    base, light, dark = rgb("#ffe9a8"), rgb("#ffffff"), rgb("#b4852a")
    warm = rgb("#fff0b0")
    white = rgb("#ffffff")
    rng = np.random.default_rng(5)
    motes = [(rng.uniform(-0.6, 0.6), rng.uniform(0, 1), rng.uniform(0.012, 0.022), rng.uniform(0, 1)) for _ in range(9)]
    tips = [(x * scale, y * scale + cy) for x, y in star_points()[::2]][1:4]

    def run(f):
        phase = TAU * f
        ring_y = -0.73 + 0.02 * math.sin(phase * 2)
        q = np.sqrt((X / 0.44) ** 2 + ((Y - ring_y) / 0.115) ** 2)
        ring_d = np.abs((q - 1) * 0.115) - 0.03
        ring = fill(ring_d)
        back = ring * (Y < ring_y)
        front = ring * (Y >= ring_y)
        glow = np.exp(-np.maximum(ring_d, 0) / 0.06) * (ring_d > -0.002)
        beam = smooth(0.62, 0.28, np.abs(X) - (Y - ring_y) * 0.14) * smooth(ring_y, ring_y + 0.15, Y) * smooth(1.0, 0.45, Y)
        out = empty_canvas()
        out = out + warm * (beam * (0.2 + 0.04 * math.sin(phase * 2)))[..., None]
        out = out + warm * (glow * 0.85)[..., None]
        out = over(out, warm, back)
        out = out + warm * (parts["glow"] * 0.65)[..., None]
        out = over(out, chrome(parts["outer"], base, light, dark, phase), parts["cover"])
        out = over(out, rgb("#ffffff"), parts["rim"] * parts["cover"])
        out = over(out, rgb("#7a5200"), parts["letter"])
        out = over(out, mix(warm, white, 0.5), front)
        out = out + white * glints(tips, f)[..., None]
        shown = np.maximum.reduce([smooth(0.2, 0.0, parts["outer"]) * EDGE, smooth(0.05, 0.0, ring_d) * EDGE,
                                   np.clip(beam * 0.3, 0, 1) * EDGE])
        for x, start, big, sway in motes:
            t = (start + f) % 1.0
            y = ring_y + 0.15 + 1.5 * t
            px = x + 0.04 * math.sin(TAU * (f + sway))
            dot = fill(np.hypot(X - px, Y - y) - big) * min(1.0, t / 0.2, (1 - t) / 0.2) * EDGE
            out = over(out, white, dot)
            shown = np.maximum(shown, dot)
        return out, shown

    return run


def demon():
    """A black and red star with curved horns and ember eyes that slowly open."""
    scale, cy = 0.8, 0.14
    parts = star_parts(scale, cy)
    horn_d = np.full(X.shape, 1e9, np.float32)
    for side in (-1, 1):
        for i in range(26):
            t = i / 25
            x = side * ((1 - t) ** 2 * 0.2 + 2 * (1 - t) * t * 0.68 + t ** 2 * 0.42)
            y = (1 - t) ** 2 * -0.3 + 2 * (1 - t) * t * -0.5 + t ** 2 * -0.97
            horn_d = np.minimum(horn_d, np.hypot(X - x, Y - y) - (0.11 * (1 - t) + 0.006))
    horn = fill(horn_d)
    body = mix(rgb("#090406"), rgb("#6a0812"), smooth(-0.3, 0.9, Y)[..., None])
    horn_color = mix(rgb("#a3232a"), rgb("#fff0d8"), smooth(-0.4, -0.85, Y)[..., None])
    horn_edge = fill(horn_d - 0.016)
    red = rgb("#ff1f1f")
    ember = rgb("#ffcc33")
    rng = np.random.default_rng(66)
    embers = [(rng.uniform(-0.8, 0.8), rng.uniform(0, 1), rng.uniform(0.012, 0.026), rng.uniform(0, 1)) for _ in range(10)]
    shown_base = np.maximum(smooth(0.2, 0.0, parts["outer"]) * EDGE, smooth(0.05, 0.0, horn_d) * EDGE)
    eye_y = -0.19

    def run(f):
        phase = TAU * f
        opening = smooth(0.15, 0.3, np.float32(f)).item() * smooth(0.82, 0.68, np.float32(f)).item()
        out = empty_canvas()
        out = out + red * (parts["glow"] * (0.55 + 0.3 * opening))[..., None]
        out = over(out, red * 0.8, horn_edge)
        out = over(out, horn_color, horn)
        out = over(out, body, parts["cover"])
        out = over(out, red, parts["rim"] * parts["cover"])
        out = over(out, red * (0.75 + 0.25 * opening), parts["letter"])
        if opening > 0.01:
            for side in (-1, 1):
                dx, dy = X - side * 0.15, Y - eye_y
                tilt = 0.45 * side
                u = dx * math.cos(tilt) + dy * math.sin(tilt)
                v = -dx * math.sin(tilt) + dy * math.cos(tilt)
                eye = np.sqrt((u / 0.095) ** 2 + (v / (0.042 * opening + 0.002)) ** 2) - 1
                cover = smooth(0.15, -0.1, eye)
                out = out + ember * (np.exp(-np.maximum(eye, 0) / 0.5) * opening * parts["cover"] * 0.55)[..., None]
                out = over(out, mix(ember, rgb("#ffffff"), 0.3), cover * parts["cover"])
        shown = shown_base
        for x, start, big, sway in embers:
            t = (start + f) % 1.0
            y = 0.98 - 1.9 * t
            px = x + 0.06 * math.sin(TAU * (f + sway))
            dot = fill(np.hypot(X - px, Y - y) - big) * min(1.0, t / 0.15, (1 - t) / 0.2) * EDGE
            out = over(out, rgb("#ff7a2a"), dot)
            shown = np.maximum(shown, dot)
        return out, shown

    return run


def matrix_rain():
    """A black star with green characters falling through it."""
    parts = star_parts()
    alpha = free_alpha()
    columns, rows = 10, 10
    rng = np.random.default_rng(12)
    glyphs = rng.random((32, 5, 5)) < 0.55
    glyphs[:, :, 0] = False
    glyphs[:, 0, :] = False
    col = np.clip(((X + 1) / 2 * columns).astype(np.int32), 0, columns - 1)
    row = np.clip(((Y + 1) / 2 * rows).astype(np.int32), 0, rows - 1)
    u = np.clip((((X + 1) / 2 * columns) % 1 * 5).astype(np.int32), 0, 4)
    v = np.clip((((Y + 1) / 2 * rows) % 1 * 5).astype(np.int32), 0, 4)
    starts = rng.random(columns)
    speeds = rng.integers(1, 3, columns).astype(np.float32)
    seeds = rng.integers(0, 32, (rows, columns))
    green = rgb("#22ff66")
    deep = rgb("#03140a")
    white = rgb("#ffffff")

    def run(f):
        phase = TAU * f
        span = rows * 1.5
        heads = ((starts + f * speeds) % 1.0) * span
        behind = (heads[col] - row) % span
        fade = np.exp(-behind / 6.5)
        which = (seeds[row, col] + int(f * 18) * (1 + (col % 3))) % 32
        on = glyphs[which, v, u].astype(np.float32)
        out = empty_canvas()
        out = out + green * (parts["glow"] * (0.5 + 0.12 * math.sin(phase * 2)))[..., None]
        out = over(out, deep, parts["cover"])
        tone = mix(green * 0.55, green, fade[..., None])
        tone = tone + (white - tone) * np.exp(-behind / 0.8)[..., None] * 0.9
        out = out + green * (fade * parts["cover"] * 0.16)[..., None]
        out = over(out, tone, np.clip(on * fade * 1.7, 0, 1) * parts["cover"])
        out = over(out, green, parts["rim"] * parts["cover"])
        out = over(out, rgb("#010503"), parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.9)
        out = over(out, rgb("#eafff1"), parts["letter"])
        return out, alpha

    return run


def plasma_ball():
    """A glass star with purple lightning reaching from its center out to each point."""
    parts = star_parts()
    alpha = free_alpha()
    angle = np.arctan2(Y - STAR_DOWN * FREE_SCALE, X)
    radius = np.hypot(X, Y - STAR_DOWN * FREE_SCALE)
    points = star_points()[::2]
    tips = [math.atan2(y * FREE_SCALE - STAR_DOWN * FREE_SCALE, x * FREE_SCALE) for x, y in points]
    glass = mix(rgb("#1c0a33"), rgb("#07030f"), smooth(0.05, 0.85, radius)[..., None])
    pink, violet = rgb("#ff4de1"), rgb("#8f5bff")
    white = rgb("#ffffff")
    wrap = lambda value: np.mod(value + math.pi, TAU) - math.pi

    def run(f):
        phase = TAU * f
        bolts = np.zeros(X.shape, np.float32)
        aura = np.zeros(X.shape, np.float32)
        for k, tip in enumerate(tips):
            bend = (0.34 * radius * np.sin(radius * 11 + k * 2.1 + phase * (1 + k % 2)) +
                    0.14 * radius * np.sin(radius * 25 - k + phase * 2))
            distance = np.abs(wrap(angle - tip - bend)) * radius
            reach = smooth(0.88, 0.66, radius)
            strength = 0.75 + 0.25 * math.sin(phase * 3 + k * 1.7)
            bolts = np.maximum(bolts, np.exp(-(distance / 0.018) ** 2) * reach * strength)
            aura = np.maximum(aura, np.exp(-(distance / 0.075) ** 2) * reach * strength)
        core = np.exp(-(radius / 0.3) ** 2)
        out = empty_canvas()
        out = out + violet * (parts["glow"] * 0.7)[..., None]
        out = over(out, glass, parts["cover"])
        out = out + violet * (aura * parts["cover"] * 0.75)[..., None]
        out = out + pink * (core * parts["cover"] * (0.7 + 0.2 * math.sin(phase * 3)))[..., None]
        out = over(out, mix(pink, white, 0.6), bolts * parts["cover"])
        out = over(out, rgb("#d9c4ff"), parts["rim"] * parts["cover"])
        out = over(out, rgb("#10041f"), parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.7)
        out = over(out, white, parts["letter"])
        return out, alpha

    return run


def clockwork():
    """A brass star with its face open, showing big meshing gears turning inside."""
    parts = star_parts()
    alpha = free_alpha()
    window = fill(parts["outer"] + 0.085)
    down = STAR_DOWN * FREE_SCALE
    # The big one sits behind the S, and the small ones fill the star's points
    gears = [(0.0, down, 0.36, 14, 1, 0.55), (0.0, -0.46, 0.15, 7, -1, 1.0),
             (-0.38, 0.3, 0.17, 7, -1, 1.0), (0.38, 0.3, 0.17, 7, -1, 1.0)]
    fields = [(np.hypot(X - x, Y - y), np.arctan2(Y - y, X - x), big, teeth, way, tone)
              for x, y, big, teeth, way, tone in gears]
    brass, brass_light, brass_dark = rgb("#b57f24"), rgb("#ffe7a3"), rgb("#3a2305")
    white = rgb("#ffffff")

    def run(f):
        phase = TAU * f
        out = empty_canvas()
        out = out + rgb("#ffb347") * (parts["glow"] * 0.5)[..., None]
        out = over(out, chrome(parts["outer"], brass, brass_light, brass_dark, phase), parts["cover"])
        out = over(out, rgb("#100a03"), window)
        shade = two_tone(phase, rgb("#a8741c"), rgb("#ffe08a"), 0.5, 1.2)
        for radius, angle, big, teeth, way, tone in fields:
            # Turns a whole number of teeth each loop, so the loop has no jump
            turn = way * phase * 2 / teeth
            bumps = np.tanh(5 * np.cos(teeth * (angle - turn)))
            edge = radius - (big + 0.045 * bumps)
            d = np.maximum(edge, big * 0.46 - radius)
            cover = fill(d) * window
            face = shade * tone
            out = over(out, face, cover)
            out = out + white * (np.exp(-((radius - big * 0.46) / 0.012) ** 2) * cover * 0.25 * tone)[..., None]
        out = over(out, rgb("#100a03"), parts["halo"] * (1 - parts["letter"]) * window * 0.9)
        out = over(out, rgb("#fff4d6"), parts["letter"])
        out = out + white * (moving_shine(f) * parts["cover"] * (1 - window) * 0.6)[..., None]
        return out, alpha

    return run


def kaleidoscope():
    """A star of mirrored glass shards in rings, flowing outward from the center."""
    parts = star_parts()
    alpha = free_alpha()
    center_y = STAR_DOWN * FREE_SCALE
    radius = np.hypot(X, Y - center_y)
    angle = np.arctan2(Y - center_y, X)
    white = rgb("#ffffff")
    wedge = TAU / 5
    rings = 6

    def run(f):
        phase = TAU * f
        folded = np.abs(np.mod(angle - math.pi / 2 + wedge / 2, wedge) - wedge / 2)
        u = radius * np.cos(folded)
        v = radius * np.sin(folded)
        place = u * rings - f * rings
        ring = np.floor(place)
        k = place - ring
        # Diamonds that point along each ring, mirrored across the fold
        diamond = np.abs(k - 0.5) * 2 + v * 5.2
        inside = smooth(1.03, 0.97, diamond)
        line = np.exp(-((diamond - 1.0) / 0.075) ** 2)
        shard = rainbow(ring / rings * 2 + 0.12 * k)
        between = rainbow(ring / rings * 2 + 0.5) * 0.5
        tile = mix(between, shard, inside[..., None])
        tile = tile * (0.72 + 0.4 * (1 - np.abs(k - 0.5) * 2))[..., None]
        out = empty_canvas()
        out = out + rainbow(f + 0.0 * X) * (parts["glow"] * 0.55)[..., None]
        out = over(out, tile, parts["cover"])
        out = over(out, rgb("#06040c"), line * parts["cover"] * 0.8)
        out = over(out, rgb("#fff1c4"), parts["rim"] * parts["cover"])
        out = over(out, rgb("#06030d"), parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.9)
        out = over(out, white, parts["letter"])
        return out, alpha

    return run


def lava_lamp():
    """A glass star with slow glowing blobs rising, splitting and sinking inside."""
    parts = star_parts()
    alpha = free_alpha()
    glass = mix(rgb("#250a33"), rgb("#6b0a1e"), smooth(-0.7, 0.85, Y)[..., None])
    rng = np.random.default_rng(18)
    blobs = [(rng.uniform(-0.34, 0.34), rng.uniform(0.09, 0.17), int(rng.integers(1, 3)), rng.uniform(0, TAU), rng.uniform(0.28, 0.52))
             for _ in range(7)]
    hot, red = rgb("#ffe066"), rgb("#ff3a12")
    white = rgb("#ffffff")
    shine = smooth(0.1, -0.02, Y + 0.3 - 0.38 * (X + 0.1) ** 2) * smooth(-0.72, -0.5, Y)

    def run(f):
        phase = TAU * f
        field = smooth(0.42, 0.72, Y) * 1.4
        for x, big, speed, offset, reach in blobs:
            y = 0.1 + reach * math.sin(phase * speed + offset)
            px = x + 0.05 * math.sin(phase + offset * 2)
            field = field + big ** 2 / ((X - px) ** 2 + (Y - y) ** 2 + 1e-4)
        lava = smooth(0.92, 1.1, field)
        edge_glow = np.clip(field, 0, 1) ** 2
        color = mix(hot, red, smooth(0.6, -0.5, Y)[..., None])
        out = empty_canvas()
        out = out + red * (parts["glow"] * (0.6 + 0.15 * math.sin(phase * 2)))[..., None]
        out = over(out, glass, parts["cover"])
        out = out + red * (edge_glow * parts["cover"] * 0.55)[..., None]
        out = over(out, color, lava * parts["cover"])
        out = out + white * (shine * parts["cover"] * 0.2)[..., None]
        out = over(out, rgb("#ffb066"), parts["rim"] * parts["cover"])
        out = over(out, rgb("#1a0410"), parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.75)
        out = over(out, white, parts["letter"])
        return out, alpha

    return run


def oil_slick():
    """Glossy black with rainbow colors sliding across it, like oil on water."""
    parts = star_parts()
    alpha = free_alpha()
    height = np.sin(np.clip(-parts["outer"] / 0.16, 0, 1) * math.pi / 2)
    white = rgb("#ffffff")
    gloss = smooth(0.1, -0.02, Y + 0.28 - 0.38 * (X + 0.1) ** 2) * smooth(-0.72, -0.5, Y)

    def run(f):
        phase = TAU * f
        flow = (np.sin(X * 3 + 1.5 * np.sin(Y * 2.6 + phase) + phase) +
                np.sin(Y * 3.4 - 1.3 * np.sin(X * 2.4 - phase) - phase))
        film = rainbow(flow * 0.5 + f)
        bands = 0.5 + 0.5 * np.sin(flow * 4.6 + phase)
        strength = 0.14 + 0.95 * smooth(0.2, 0.8, bands)
        surface = film * (strength * (0.45 + 0.7 * height))[..., None]
        edge = rainbow(np.arctan2(Y, X) / TAU + f)
        out = empty_canvas()
        out = out + edge * (parts["glow"] * 0.6)[..., None]
        out = over(out, surface, parts["cover"])
        out = out + white * (gloss * parts["cover"] * 0.2)[..., None]
        out = over(out, 0.45 + edge * 0.6, parts["rim"] * parts["cover"])
        out = over(out, rgb("#030305"), parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.95)
        out = over(out, rgb("#f4f6ff"), parts["letter"])
        return out, alpha

    return run


def nebula():
    """Deep space inside a star: slow purple and blue clouds and tiny twinkling stars."""
    parts = star_parts()
    alpha = free_alpha()
    rng = np.random.default_rng(77)
    stars = []
    for _ in range(26):
        x, y = rng.uniform(-0.7, 0.7), rng.uniform(-0.7, 0.8)
        stars.append((np.exp(-(np.hypot(X - x, Y - y) / rng.uniform(0.012, 0.024)) ** 2), rng.uniform(0, TAU), int(rng.integers(1, 4))))
    deep = rgb("#04050f")
    purple, pink, blue = rgb("#6a1fd0"), rgb("#ff4fb4"), rgb("#2a7bff")
    white = rgb("#ffffff")

    def run(f):
        phase = TAU * f
        wx = X + 0.28 * np.sin(Y * 3.1 + phase) + 0.1 * np.sin(Y * 8 - phase)
        wy = Y + 0.28 * np.sin(X * 2.7 - phase) + 0.1 * np.sin(X * 7 + phase * 2)
        a = 0.5 + 0.5 * np.sin(wx * 3.4 + wy * 1.7 + 1 + np.sin(wy * 5 + phase))
        b = 0.5 + 0.5 * np.sin(wy * 4.1 - wx * 2.3 + 2 + np.sin(wx * 6 - phase))
        c = 0.5 + 0.5 * np.sin((wx + wy) * 5.7 + np.sin((wx - wy) * 4 + phase))
        out = empty_canvas()
        out = out + rgb("#8f6bff") * (parts["glow"] * 0.6)[..., None]
        sky = deep + purple * (smooth(0.3, 0.95, a) * 0.95)[..., None]
        sky = sky + blue * (smooth(0.4, 0.95, b) * 0.75)[..., None]
        sky = sky + pink * (smooth(0.62, 1.0, c * a) * 0.85)[..., None]
        for dot, offset, speed in stars:
            sky = sky + white * (dot * (0.55 + 0.45 * math.sin(phase * speed + offset)))[..., None]
        out = over(out, sky, parts["cover"])
        out = over(out, rgb("#cfd8ff"), parts["rim"] * parts["cover"])
        out = out + white * (np.exp(-np.maximum(parts["halo"] * 0, 0)) * 0)[..., None]
        out = over(out, deep, parts["halo"] * (1 - parts["letter"]) * parts["cover"] * 0.75)
        out = over(out, white, parts["letter"])
        return out, alpha

    return run


BADGES = [
    ("donor_of_the_year", donor_of_the_year),
    ("first_donor", first_donor),
    ("one_year_on_solrino", one_year_on_solrino),
    ("veteran", veteran),
    ("alpha_tester", alpha_tester),
    ("top_bug_hunter", top_bug_hunter),
    ("katana", katana),
    ("halo", halo),
    ("demon", demon),
    ("matrix_rain", matrix_rain),
    ("plasma_ball", plasma_ball),
    ("clockwork", clockwork),
    ("kaleidoscope", kaleidoscope),
    ("lava_lamp", lava_lamp),
    ("oil_slick", oil_slick),
    ("nebula", nebula),
    ("staff", staff),
    ("supporter_plus_plus_plus_alt", supporter_plus_plus_plus_alt),
    ("donor_of_the_month", donor_of_the_month),
    ("supporter_plus_plus_plus", supporter_plus_plus_plus),
    ("number_one_donor", number_one_donor),
    ("beta_tester", beta_tester),
    ("black_hole", black_hole),
    ("friend_of_solrino", friend_of_solrino),
    ("valentines_2027", valentines_2027),
    ("winter_2026", winter_2026),
    ("spooky", spooky),
    ("moderator", moderator),
    ("bug_hunter", bug_hunter),
    ("special", special),
    ("top_donor_alt", top_donor_alt),
    ("obsidian", obsidian),
    ("contributor", contributor),
    ("developer", developer),
    ("top_donor", top_donor),
    ("founder", founder),
    ("supporter_plus_plus_alt", supporter_plus_plus_alt),
    ("supporter_plus_plus", supporter_plus_plus),
    ("supporter_plus_alt", supporter_plus_alt),
    ("supporter_plus", supporter_plus),
    ("supporter", supporter),
]


def to_image(out, alpha):
    image = np.dstack([np.clip(out, 0, 1), np.clip(alpha, 0, 1)])
    return Image.fromarray((image * 255 + 0.5).astype(np.uint8))


def main(output, only=None):
    # Background work: anything else on the PC goes first
    if os.name == "nt":
        ctypes.windll.kernel32.SetPriorityClass(
            ctypes.windll.kernel32.GetCurrentProcess(), 0x00004000)

    os.makedirs(output, exist_ok=True)
    rows = []
    for name, build in BADGES:
        if only and name not in only:
            continue
        run = build()
        frames = [to_image(*run(i / FRAMES)) for i in range(FRAMES)]
        folder = os.path.join(output, name)
        os.makedirs(folder, exist_ok=True)
        sizes = []
        for label, size in SIZES.items():
            small = [f.resize((size, size), Image.LANCZOS) for f in frames]
            path = os.path.join(folder, label + ".webp")
            small[0].save(path, save_all=True, append_images=small[1:],
                          duration=FRAME_MS, loop=0, quality=88, method=4,
                          alpha_quality=95, lossless=False)
            sizes.append(f"{label} {os.path.getsize(path) // 1024}KB")
        print(name, ", ".join(sizes), flush=True)
        rows.append((name, frames))

    # Kept out of the output folder, which is bundled into the app.
    # The sheets show the whole set, so they're skipped when making only some.
    if not only:
        write_previews(os.path.dirname(os.path.abspath(__file__)), rows)


def write_previews(output, rows):
    background = (24, 20, 22, 255)
    picks = [0, 15, 30, 45, 60, 75]
    cell = 120

    # Still sheet: frames across the loop, real chat sizes, then 18px enlarged
    sheet = Image.new("RGBA", (6 * cell + 230, len(rows) * cell), background)
    for row, (_, frames) in enumerate(rows):
        y = row * cell
        for col, pick in enumerate(picks):
            f = frames[pick].resize((cell - 8, cell - 8), Image.LANCZOS)
            sheet.alpha_composite(f, (col * cell + 4, y + 4))
        x = 6 * cell + 6
        for size, ox, oy in ((18, 0, 6), (36, 26, 6), (72, 0, 44)):
            f = frames[12].resize((size, size), Image.LANCZOS)
            sheet.alpha_composite(f, (x + ox, y + oy))
        tiny = frames[12].resize((18, 18), Image.LANCZOS).resize((108, 108), Image.NEAREST)
        sheet.alpha_composite(tiny, (x + 112, y + 6))
    sheet.save(os.path.join(output, "preview.png"))

    # Moving sheet, for opening in a browser
    per_row = 5
    size = 88
    animated = []
    for i in range(FRAMES):
        canvas = Image.new("RGBA", (per_row * size, (len(rows) // per_row) * size), background)
        for j, (_, frames) in enumerate(rows):
            f = frames[i].resize((72, 72), Image.LANCZOS)
            canvas.alpha_composite(f, (j % per_row * size + 8, j // per_row * size + 8))
        animated.append(canvas.convert("RGB"))
    animated[0].save(os.path.join(output, "preview-animated.webp"), save_all=True,
                     append_images=animated[1:], duration=FRAME_MS, loop=0, quality=90, method=4)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
