#!/usr/bin/env python3
"""Clove logo concepts. Each mark is drawn as a white-on-transparent RGBA
image (mark(name, size, color)) so it can be used flat, in the glossy orb,
in the boot logo and in the status bar.

usage: logos.py OUT.png   (a concept sheet)
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_art import POPPINS, POPPINS_LIGHT, clove_mark  # noqa: E402

SS = 4  # supersampling


def _canvas(size):
    w = int(size * SS)
    im = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im), w


def _done(im, size):
    return im.resize((int(size), int(size)), Image.LANCZOS)


def mark_bud(size, color):
    """1. Bud: the clove bud redrawn - a round head cupped by four sepals
    that curl up around it, on a short tapering stem (a clove is a 'nail')."""
    im, d, w = _canvas(size)
    P = lambda x, y: (x * w, y * w)
    c = tuple(color)
    # stem
    d.polygon([P(0.43, 0.60), P(0.57, 0.60), P(0.52, 0.95), P(0.48, 0.95)], fill=c)
    # cup: a bowl whose rim rises into four sepal points around the head
    d.polygon([P(0.28, 0.26), P(0.36, 0.40), P(0.41, 0.24), P(0.47, 0.40), P(0.53, 0.40), P(0.59, 0.24),
               P(0.64, 0.40), P(0.72, 0.26), P(0.71, 0.48), P(0.62, 0.62), P(0.38, 0.62), P(0.29, 0.48)], fill=c)
    # head, separated from the cup by a thin gap
    r = 0.15
    hy = 0.33
    gap = Image.new("L", im.size, 0)
    ImageDraw.Draw(gap).ellipse([P(0.5 - r - 0.03, hy - r - 0.03), P(0.5 + r + 0.03, hy + r + 0.03)], fill=255)
    a = np.asarray(im.split()[3], np.int16) - np.asarray(gap, np.int16)
    im.putalpha(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)))
    d = ImageDraw.Draw(im)
    d.ellipse([P(0.5 - r, hy - r), P(0.5 + r, hy + r)], fill=c)
    return _done(im, size)


def mark_c(size, color):
    """2. Bud-C: a lowercase c drawn as a curling stem that ends in a clove
    bud - the first letter of the name and the plant in one stroke."""
    im, d, w = _canvas(size)
    c = tuple(color)
    cx, cy, rc = 0.47 * w, 0.55 * w, 0.27 * w
    pts_o, pts_i = [], []
    a0, a1 = math.radians(52), math.radians(312)  # upper right, round the left, to the lower right
    n = 160
    for i in range(n + 1):
        t = i / n
        a = a0 + (a1 - a0) * t
        th = w * (0.12 - 0.095 * t ** 1.3)  # thick under the bud, thin at the tail
        pts_o.append((cx + (rc + th / 2) * math.cos(a), cy - (rc + th / 2) * math.sin(a)))
        pts_i.append((cx + (rc - th / 2) * math.cos(a), cy - (rc - th / 2) * math.sin(a)))
    d.polygon(pts_o + pts_i[::-1], fill=c)
    # the bud sits on the upper end in a small crown of sepals
    ex, ey = cx + rc * math.cos(a0), cy - rc * math.sin(a0)
    br = 0.095 * w
    bx, by = ex + 0.05 * w, ey - 0.085 * w
    d.polygon([(ex - 0.07 * w, ey + 0.02 * w), (bx - br * 1.25, by - br * 0.55), (bx - br * 0.35, by + br * 0.35),
               (bx + br * 0.45, by + br * 0.55), (bx + br * 1.05, by + br * 0.05), (ex + 0.065 * w, ey + 0.05 * w)], fill=c)
    gap = Image.new("L", im.size, 0)
    ImageDraw.Draw(gap).ellipse([bx - br - 0.022 * w, by - br - 0.022 * w, bx + br + 0.022 * w, by + br + 0.022 * w], fill=255)
    a = np.asarray(im.split()[3], np.int16) - np.asarray(gap, np.int16)
    im.putalpha(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)))
    d = ImageDraw.Draw(im)
    d.ellipse([bx - br, by - br, bx + br, by + br], fill=c)
    return _done(im, size)


def mark_garlic(size, color):
    """3. Clove + Play: a single garlic clove (a nod to GarlicOS, which Clove
    runs on) with a play button cut out of it - it's a game launcher."""
    im, d, w = _canvas(size)
    c = tuple(color)

    def bez(p0, p1, p2, p3, n=60):
        out = []
        for i in range(n + 1):
            t = i / n
            mt = 1 - t
            out.append(((mt ** 3 * p0[0] + 3 * mt * mt * t * p1[0] + 3 * mt * t * t * p2[0] + t ** 3 * p3[0]) * w,
                        (mt ** 3 * p0[1] + 3 * mt * mt * t * p1[1] + 3 * mt * t * t * p2[1] + t ** 3 * p3[1]) * w))
        return out
    tip = (0.66, 0.07)
    pts = []
    pts += bez((0.30, 0.90), (0.10, 0.78), (0.12, 0.40), tip)          # the round back, up to the tip
    pts += bez(tip, (0.70, 0.30), (0.86, 0.55), (0.80, 0.84))           # inner side, gently curved
    pts += bez((0.80, 0.84), (0.78, 0.93), (0.40, 0.95), (0.30, 0.90))  # the flat root end
    d.polygon(pts, fill=c)
    # a highlight groove along the back
    groove = Image.new("L", im.size, 0)
    gd = ImageDraw.Draw(groove)
    gl = bez((0.36, 0.82), (0.24, 0.66), (0.28, 0.40), (0.52, 0.20), 40)
    gd.line(gl, fill=255, width=int(w * 0.022), joint="curve")
    # play triangle
    px, py, ps = 0.52, 0.60, 0.20
    gd.polygon([((px - ps * 0.42) * w, (py - ps * 0.55) * w), ((px - ps * 0.42) * w, (py + ps * 0.55) * w),
                ((px + ps * 0.55) * w, py * w)], fill=255)
    a = np.asarray(im.split()[3], np.int16) - np.asarray(groove, np.int16)
    im.putalpha(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)))
    return _done(im, size)


def mark_flower(size, color):
    """4. Flower: the clove flower seen from above - four rounded sepals
    around the bud, upright like a plus so it reads at tiny sizes."""
    im, d, w = _canvas(size)
    c = tuple(color)
    cx = cy = 0.5 * w
    for k in range(4):
        a = math.pi / 2 * k
        # a sepal: a rounded leaf from the centre outwards
        pts = []
        for i in range(61):
            t = i / 60
            ang = -0.5 + t
            rr = 0.40 * w * (math.cos(ang * math.pi) ** 0.55)
            pts.append((cx + rr * math.cos(a + ang * 0.95), cy + rr * math.sin(a + ang * 0.95)))
        d.polygon([(cx, cy)] + pts, fill=c)
    # hollow ring around the bud, then the bud itself
    ring = Image.new("L", im.size, 0)
    rd = ImageDraw.Draw(ring)
    r1 = 0.17 * w
    rd.ellipse([cx - r1, cy - r1, cx + r1, cy + r1], fill=255)
    a = np.asarray(im.split()[3], np.int16) - np.asarray(ring, np.int16)
    im.putalpha(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)))
    d = ImageDraw.Draw(im)
    r2 = 0.12 * w
    d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=c)
    return _done(im, size)


MARKS = [
    ("bud", "1  Bud", "The clove bud redrawn: a round\nhead in a crown of sepals.", mark_bud),
    ("c", "2  Bud-C", "A lowercase c that grows a\nclove bud. Name + plant in one.", mark_c),
    ("garlic", "3  Clove + Play", "A garlic clove (Clove runs on\nGarlicOS) with a play button.", mark_garlic),
    ("flower", "4  Flower", "The clove flower from above.\nReads even at tiny sizes.", mark_flower),
]


def get_mark(key, size, color):
    for k, _, _, fn in MARKS:
        if k == key:
            return fn(size, color)
    if key == "old":
        return clove_mark(size, tuple(color))
    raise KeyError(key)


def concept_sheet(path):
    from swirl import orb_with  # late import: swirl imports this module
    W, H = 1600, 1000
    sheet = Image.new("RGB", (W, H), (16, 15, 20))
    d = ImageDraw.Draw(sheet)
    tf = ImageFont.truetype(POPPINS, 44)
    d.text((60, 40), "Clove logo ideas", font=tf, fill=(244, 239, 230))
    sf = ImageFont.truetype(POPPINS_LIGHT, 22)
    d.text((60, 100), "Each shown flat, as the wordmark, and in the glossy orb used by the Swirl wallpapers.", font=sf, fill=(160, 155, 170))
    lav = (195, 162, 247, 255)
    cw = (W - 120) // 4
    for i, (key, title, blurb, fn) in enumerate(MARKS):
        x0 = 60 + i * cw
        card = Image.new("RGBA", (cw - 24, 800), (34, 31, 41, 255))
        cd = ImageDraw.Draw(card)
        m = fn(170, lav)
        card.alpha_composite(m, ((card.width - 170) // 2, 40))
        # wordmark
        wf = ImageFont.truetype(POPPINS, 46)
        sm = fn(56, lav)
        tb = cd.textbbox((0, 0), "clove", font=wf)
        tw = tb[2] - tb[0]
        total = 56 + 10 + tw
        wx = (card.width - total) // 2
        card.alpha_composite(sm, (wx, 260))
        cd.text((wx + 66 - tb[0], 262 + (56 - (tb[3] - tb[1])) // 2 - tb[1]), "clove", font=wf, fill=(244, 239, 230))
        # orb on a little swirl swatch
        sw = orb_with(key, card.width - 40, 250, (i * 2 + 1) % 6)
        card.alpha_composite(sw, (20, 350))
        # text
        cd.text((24, 625), title, font=ImageFont.truetype(POPPINS, 30), fill=(244, 239, 230))
        cd.multiline_text((24, 675), blurb, font=ImageFont.truetype(POPPINS_LIGHT, 19), fill=(170, 165, 180), spacing=6)
        sheet.paste(card, (x0, 160), card)
    sheet.save(path, optimize=True)


if __name__ == "__main__":
    concept_sheet(sys.argv[1])
