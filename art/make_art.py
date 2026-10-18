#!/usr/bin/env python3
"""Clove artwork: the "Lagoon" wallpaper (screensaver), its theme background,
and the RG35XX boot logo. Everything is drawn procedurally here, so the
result is original artwork: an underwater lagoon with a clove tree growing in
a floating glass bowl, fish swimming past, and the Clove wordmark.

usage: make_art.py OUTDIR
"""
import math
import os
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

POPPINS = "/usr/share/fonts/truetype/google-fonts/Poppins-Medium.ttf"
POPPINS_LIGHT = "/usr/share/fonts/truetype/google-fonts/Poppins-Light.ttf"
LAVENDER = (195, 162, 247)
CREAM = (244, 239, 230)


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def gradient(w, h, stops):
    """vertical gradient through (pos, (r,g,b)) stops"""
    y = np.linspace(0, 1, h)[:, None]
    out = np.zeros((h, w, 3), np.float32)
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        t = smooth((y - p0) / max(p1 - p0, 1e-6))
        m = ((y >= p0) & (y <= p1)).astype(np.float32)
        for k in range(3):
            out[..., k] += m[:, :1] * (c0[k] + (c1[k] - c0[k]) * t[:, :1]) * np.ones((1, w))
    # rows exactly on a boundary get added twice: normalise by coverage
    cov = np.zeros((h, 1), np.float32)
    for (p0, _), (p1, _) in zip(stops, stops[1:]):
        cov += ((y >= p0) & (y <= p1)).astype(np.float32)
    return out / np.maximum(cov[..., None], 1)


def add_light(img, mask, color, alpha):
    for k in range(3):
        img[..., k] = img[..., k] + (color[k] - img[..., k]) * np.clip(mask * alpha, 0, 1)


def paste_rgba(base, layer):
    """alpha-composite an RGBA PIL layer onto a float RGB array"""
    a = np.asarray(layer, np.float32) / 255.0
    al = a[..., 3:4]
    base[:] = base * (1 - al) + a[..., :3] * 255.0 * al


def wallpaper(W, H, seed=7, wordmark=True):
    rnd = random.Random(seed)
    s = H / 480.0
    img = gradient(W, H, [(0.0, (92, 182, 150)), (0.35, (38, 128, 108)), (0.75, (14, 72, 66)), (1.0, (8, 42, 40))])
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    nx, ny = xx / H, yy / H

    # caustic light ripples, strongest near the surface
    c = (np.sin(nx * 23 + np.sin(ny * 17 + nx * 5) * 2.1) + np.sin(ny * 29 - nx * 11 + np.sin(nx * 13) * 1.7)
         + np.sin((nx + ny) * 19 + np.cos(nx * 7 - ny * 3) * 2.4))
    c2 = (np.sin(nx * 41 + np.sin(ny * 31) * 1.3) + np.sin(ny * 47 + np.cos(nx * 23) * 1.5))
    caus = np.exp(-np.abs(c) * 3.0) * 0.7 + np.exp(-np.abs(c2) * 4.0) * 0.5
    caus_img = Image.fromarray((np.clip(caus, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2 * s))
    caus = np.asarray(caus_img, np.float32) / 255.0
    depth = np.clip(1.0 - ny / (H / H * 0.85), 0, 1) ** 1.6
    add_light(img, caus * depth, (210, 255, 225), 0.16)

    # sun rays from the surface
    rays = np.zeros((H, W), np.float32)
    for i in range(7):
        x0 = W * (0.15 + 0.13 * i + rnd.uniform(-0.04, 0.04))
        ang = math.radians(rnd.uniform(14, 22))
        width = W * rnd.uniform(0.018, 0.045)
        d = (xx - (x0 + yy * math.tan(ang))) * math.cos(ang)
        rays += np.exp(-(d / width) ** 2) * rnd.uniform(0.5, 1.0)
    rays *= np.clip(1 - ny / 0.95, 0, 1) ** 1.3
    add_light(img, rays, (220, 255, 230), 0.16)

    # sandy haze along the bottom
    sand = np.clip((ny - 0.78) / 0.22, 0, 1) ** 1.5
    noise = np.asarray(Image.fromarray((np.random.RandomState(seed).rand(H // 8 + 1, W // 8 + 1) * 255).astype(np.uint8))
                       .resize((W, H), Image.BICUBIC), np.float32) / 255.0
    add_light(img, sand * (0.6 + 0.4 * noise), (120, 160, 110), 0.35)

    # floating specks
    specks = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(specks)
    for _ in range(int(140 * (W * H) / (640 * 480))):
        x, y = rnd.uniform(0, W), rnd.uniform(0, H)
        r = rnd.uniform(0.4, 1.3) * s
        d.ellipse([x - r, y - r, x + r, y + r], fill=(220, 255, 230, rnd.randint(30, 90)))
    paste_rgba(img, specks)

    # --- background fish (far, blurred) ---
    far = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for (fx, fy, sz, flip) in [(0.80, 0.20, 0.034, 0), (0.86, 0.25, 0.030, 0), (0.91, 0.19, 0.026, 0), (0.13, 0.36, 0.024, 1),
                               (0.17, 0.39, 0.020, 1), (0.12, 0.41, 0.018, 1)]:
        fish(far, fx * W, fy * H, sz * H * 1.6, flip, (196, 196, 120), 0.55, rnd)
    far = far.filter(ImageFilter.GaussianBlur(1.6 * s))
    paste_rgba(img, far)

    # --- the bowl, soil and clove tree ---
    cx, top, R = W * 0.5, H * 0.57, H * 0.33
    bowl = glass_bowl(W, H, cx, top, R, rnd)
    tree = clove_tree(W, H, cx, top + R * 0.33, R, rnd)
    paste_rgba(img, tree["back"])
    paste_rgba(img, bowl["inside"])
    paste_rgba(img, tree["front"])
    paste_rgba(img, bowl["glass"])

    # --- near fish ---
    near = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for (fx, fy, sz, flip) in [(0.79, 0.31, 0.050, 0), (0.85, 0.40, 0.046, 0), (0.90, 0.50, 0.042, 0), (0.95, 0.59, 0.037, 0),
                               (0.25, 0.86, 0.080, 1)]:
        fish(near, fx * W, fy * H, sz * H * 1.6, flip, (226, 214, 120), 0.95, rnd)
    paste_rgba(img, near.filter(ImageFilter.GaussianBlur(0.35 * s)))

    # bubbles rising from the bowl
    bub = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(bub)
    for _ in range(18):
        x = cx + rnd.uniform(-0.9, 0.9) * R
        y = rnd.uniform(0.08, 0.55) * H
        r = rnd.uniform(1.2, 3.6) * s
        d.ellipse([x - r, y - r, x + r, y + r], outline=(230, 255, 240, 120), width=max(1, int(0.8 * s)))
        d.ellipse([x - r * 0.45, y - r * 0.6, x - r * 0.05, y - r * 0.2], fill=(255, 255, 255, 140))
    paste_rgba(img, bub)

    # soft vignette
    v = ((xx - W / 2) / (W * 0.75)) ** 2 + ((yy - H * 0.45) / (H * 0.8)) ** 2
    img *= (1 - np.clip(v, 0, 1) * 0.35)[..., None]

    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")
    if wordmark:
        out = draw_wordmark(out, W - 18 * s, H - 14 * s, 26 * s, anchor="rb", alpha=230, credit=True)
    return out


def _ellipse_pts(cx, cy, rx, ry, a0, a1, n=24):
    return [(cx + math.cos(math.radians(a)) * rx, cy + math.sin(math.radians(a)) * ry)
            for a in np.linspace(a0, a1, n)]


def fish(layer, x, y, size, flip, color, alpha, rnd):
    """A striped butterflyfish (head on the left), drawn 4x then scaled down."""
    S = 4
    w, h = int(size * 1.9 * S) + 8, int(size * 1.5 * S) + 8
    a = int(255 * alpha)
    bx, by = w * 0.44, h * 0.5
    bw, bh = size * 1.0 * S, size * 0.86 * S
    fin = (min(255, int(color[0] * 1.05)), int(color[1] * 0.85), int(color[2] * 0.45))
    body_col = (min(255, color[0] + 18), min(255, color[1] + 20), min(255, color[2] + 70))

    def layer_with(draw_fn):
        L = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        draw_fn(ImageDraw.Draw(L))
        return L

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    # tail fan
    tx = bx + bw * 0.46
    out.alpha_composite(layer_with(lambda d: d.polygon(
        [(tx - bw * 0.05, by - bh * 0.10), (tx + bw * 0.30, by - bh * 0.30), (tx + bw * 0.26, by),
         (tx + bw * 0.30, by + bh * 0.30), (tx - bw * 0.05, by + bh * 0.10)], fill=fin + (a,))))
    # dorsal and anal fins sweeping back
    out.alpha_composite(layer_with(lambda d: d.polygon(
        _ellipse_pts(bx + bw * 0.05, by, bw * 0.52, bh * 0.66, 200, 345) + [(bx + bw * 0.42, by - bh * 0.05)], fill=fin + (a,))))
    out.alpha_composite(layer_with(lambda d: d.polygon(
        _ellipse_pts(bx + bw * 0.08, by, bw * 0.48, bh * 0.60, 15, 140) + [(bx + bw * 0.40, by + bh * 0.05)], fill=fin + (a,))))
    # body with a pointed snout
    body_pts = _ellipse_pts(bx, by, bw / 2, bh / 2, -150, 150, 40) + [(bx - bw * 0.64, by + bh * 0.04)]
    body = layer_with(lambda d: d.polygon(body_pts, fill=body_col + (a,)))
    mask = body.split()[3]
    # stripes and shading, kept inside the body
    deco = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    dd = ImageDraw.Draw(deco)
    for k, off in enumerate((-0.30, -0.06, 0.18, 0.40)):
        sx = bx + bw * off
        sw = bw * (0.07 if k else 0.09)
        dd.polygon([(sx - sw, by - bh), (sx + sw, by - bh), (sx + sw * 0.6 + bw * 0.04, by + bh), (sx - sw * 0.6 + bw * 0.04, by + bh)],
                   fill=(38, 40, 36, int(a * 0.9)))
    shade = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(shade).ellipse([bx - bw * 0.5, by + bh * 0.05, bx + bw * 0.5, by + bh * 0.75], fill=(0, 30, 30, int(a * 0.35)))
    shine = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(shine).ellipse([bx - bw * 0.42, by - bh * 0.42, bx + bw * 0.1, by - bh * 0.12], fill=(255, 255, 240, int(a * 0.35)))
    for L in (deco, shade.filter(ImageFilter.GaussianBlur(bh * 0.12)), shine.filter(ImageFilter.GaussianBlur(bh * 0.08))):
        body.alpha_composite(Image.composite(L, Image.new("RGBA", (w, h), (0, 0, 0, 0)), mask))
    out.alpha_composite(body)
    # eye
    er = bh * 0.07
    ex, ey = bx - bw * 0.36, by - bh * 0.10
    out.alpha_composite(layer_with(lambda d: (d.ellipse([ex - er, ey - er, ex + er, ey + er], fill=(18, 18, 18, a)),
                                                   d.ellipse([ex - er * 0.35, ey - er * 0.6, ex + er * 0.1, ey - er * 0.15], fill=(255, 255, 255, a)))))
    f = out.resize((max(1, w // S), max(1, h // S)), Image.LANCZOS)
    if flip:
        f = f.transpose(Image.FLIP_LEFT_RIGHT)
    f = f.rotate(rnd.uniform(-10, 10), resample=Image.BICUBIC, expand=True)
    layer.alpha_composite(f, (int(x - f.width / 2), int(y - f.height / 2)))


def glass_bowl(W, H, cx, top, R, rnd):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    dx, dy = xx - cx, yy - top
    r = np.sqrt(dx * dx + dy * dy)
    inside = (r <= R) & (dy >= 0)
    soil_top = top + R * 0.30
    # water inside the bowl: slightly brighter and greener
    inner = np.zeros((H, W, 4), np.float32)
    water = inside & (yy < soil_top)
    inner[water] = (150, 220, 170, 60)
    # soil: grass band, then layered earth with pebbles
    soil = inside & (yy >= soil_top)
    t = np.clip((yy - soil_top) / (R * 0.7), 0, 1)
    rs = np.random.RandomState(3)
    grain = np.asarray(Image.fromarray((rs.rand(H // 2 + 1, W // 2 + 1) * 255).astype(np.uint8)).resize((W, H), Image.BILINEAR),
                       np.float32) / 255.0
    earth = np.stack([lerp(112, 52, t) + grain * 26, lerp(92, 40, t) + grain * 20, lerp(62, 28, t) + grain * 14], -1)
    inner[soil, :3] = earth[soil]
    inner[soil, 3] = 235
    grass = soil & (yy < soil_top + R * 0.07)
    inner[grass] = np.stack([60 + grain * 30, 130 + grain * 50, 60 + grain * 20, np.full_like(grain, 245)], -1)[grass]
    inside_img = Image.fromarray(np.clip(inner, 0, 255).astype(np.uint8), "RGBA")
    d = ImageDraw.Draw(inside_img)
    # pebbles
    for _ in range(int(160 * (H / 480) ** 2)):
        px = cx + rnd.uniform(-R, R)
        py = rnd.uniform(soil_top + R * 0.1, top + R)
        if (px - cx) ** 2 + (py - top) ** 2 > (R * 0.96) ** 2:
            continue
        pr = rnd.uniform(0.6, 2.4) * H / 480
        g = rnd.randint(70, 150)
        d.ellipse([px - pr, py - pr * 0.7, px + pr, py + pr * 0.7], fill=(g, int(g * 0.88), int(g * 0.72), 220))
    # grass tufts along the surface
    for _ in range(int(120 * H / 480)):
        gx = cx + rnd.uniform(-R * 0.94, R * 0.94)
        if abs(gx - cx) > math.sqrt(max(R * R - (R * 0.3) ** 2, 0)) * 0.98:
            continue
        gh = rnd.uniform(2, 7) * H / 480
        lean = rnd.uniform(-2, 2) * H / 480
        gcol = (rnd.randint(60, 120), rnd.randint(140, 200), rnd.randint(60, 100), 255)
        d.line([(gx, soil_top + 1), (gx + lean, soil_top - gh)], fill=gcol, width=max(1, int(H / 480)))

    # the glass: rim ellipse, edge glow and a highlight
    glass = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ring = (np.abs(r - R) < 2.2 * H / 480) & (dy >= -1)
    edge = np.clip(1 - np.abs(r - R) / (R * 0.10), 0, 1) * (r <= R) * (dy >= 0)
    g = np.zeros((H, W, 4), np.float32)
    g[..., :3] = (225, 255, 235)
    g[..., 3] = np.clip(edge * 70 + ring * 150, 0, 255)
    glass = Image.fromarray(g.astype(np.uint8), "RGBA")
    gd = ImageDraw.Draw(glass)
    rim_h = R * 0.10
    gd.ellipse([cx - R, top - rim_h, cx + R, top + rim_h], outline=(235, 255, 240, 150), width=max(1, int(1.6 * H / 480)))
    # specular streak on the left of the bowl
    hl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    hd = ImageDraw.Draw(hl)
    hd.arc([cx - R * 0.9, top - R * 0.9, cx + R * 0.9, top + R * 0.9], 120, 165, fill=(255, 255, 255, 120), width=max(2, int(4 * H / 480)))
    glass.alpha_composite(hl.filter(ImageFilter.GaussianBlur(1.5 * H / 480)))
    return {"inside": inside_img, "glass": glass}


def clove_tree(W, H, cx, base_y, R, rnd):
    """A clove tree: a dark, branching trunk under a full dome of glossy
    leaves, with a few clusters of pink-red clove buds. Drawn at 3x."""
    S = 3
    h = H / 480
    back = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    trunk = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    front = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    db, dt, df = ImageDraw.Draw(back), ImageDraw.Draw(trunk), ImageDraw.Draw(front)
    tips = []
    bark = (46, 32, 24, 255)

    def branch(x, y, ang, length, width, depth):
        steps = 4
        px, py = x, y
        for i in range(steps):  # slightly crooked limbs
            ang += rnd.uniform(-0.12, 0.12)
            nx = px + math.cos(ang) * length / steps
            ny = py - math.sin(ang) * length / steps
            wdt = width * (1 - 0.25 * i / steps)
            dt.line([(px * S, py * S), (nx * S, ny * S)], fill=bark, width=max(1, int(wdt * S)))
            dt.ellipse([nx * S - wdt * S / 2, ny * S - wdt * S / 2, nx * S + wdt * S / 2, ny * S + wdt * S / 2], fill=bark)
            px, py = nx, ny
        if depth == 0:
            tips.append((px, py))
            return
        n = 2 if depth >= 4 else rnd.choice([2, 3])
        for i in range(n):
            na = ang + (i - (n - 1) / 2) * rnd.uniform(0.6, 0.95) + rnd.uniform(-0.12, 0.12)
            branch(px, py, na, length * rnd.uniform(0.62, 0.78), width * 0.64, depth - 1)

    trunk_len = R * 0.46
    branch(cx, base_y, math.pi / 2 + rnd.uniform(-0.05, 0.05), trunk_len, 11 * h, 5)

    # the crown: a dome of leaves around the branch tips
    xs = [t[0] for t in tips]
    ys = [t[1] for t in tips]
    ccx, ccy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2 - 6 * h
    crx, cry = max((max(xs) - min(xs)) / 2 + 40 * h, R * 1.02), max((max(ys) - min(ys)) / 2 + 34 * h, R * 0.62)
    ccx = (ccx + cx) / 2
    ccy = min(ccy, base_y - R * 1.15)
    crx = max(crx, max(abs(x - ccx) for x in xs) + 26 * h)
    cry = max(cry, max(abs(y - ccy) for y in ys) + 22 * h)

    ph1, ph2 = rnd.uniform(0, 6.28), rnd.uniform(0, 6.28)

    def in_crown(px, py):
        u, v = (px - ccx) / crx, (py - ccy) / cry
        th = math.atan2(v, u)
        lump = 1 + 0.07 * math.sin(5 * th + ph1) + 0.05 * math.sin(9 * th + ph2)
        return u * u + v * v * (1.4 if py > ccy else 1.0) < lump * lump

    def leaf(d, lx, ly, ln, ang, col):
        c, s_ = math.cos(ang), math.sin(ang)
        pts = []
        for t in np.linspace(0, math.pi, 7):
            pts.append((lx + c * ln * math.cos(t) - s_ * ln * 0.38 * math.sin(t), ly + s_ * ln * math.cos(t) + c * ln * 0.38 * math.sin(t)))
        for t in np.linspace(math.pi, 2 * math.pi, 7):
            pts.append((lx + c * ln * math.cos(t) - s_ * ln * 0.38 * math.sin(t), ly + s_ * ln * math.cos(t) + c * ln * 0.38 * math.sin(t)))
        d.polygon([(p[0] * S, p[1] * S) for p in pts], fill=col)

    # dark under-layer for depth
    for _ in range(int(1400 * h * h)):
        lx, ly = ccx + rnd.uniform(-crx, crx), ccy + rnd.uniform(-cry, cry * 1.1)
        if not in_crown(lx, ly):
            continue
        g = rnd.randint(70, 105)
        leaf(db, lx, ly, rnd.uniform(4, 7) * h, rnd.uniform(0, math.pi), (int(g * 0.35), g, int(g * 0.45), 255))
    # lit leaves on top, brighter towards the upper left
    for _ in range(int(2300 * h * h)):
        lx, ly = ccx + rnd.uniform(-crx, crx), ccy + rnd.uniform(-cry, cry * 0.9)
        if not in_crown(lx, ly):
            continue
        u, v = (lx - ccx) / crx, (ly - ccy) / cry
        light = np.clip(0.55 - 0.45 * v - 0.25 * u + rnd.uniform(-0.18, 0.18), 0, 1)
        if rnd.random() < 0.25 and v > 0.25:
            continue  # leave gaps low in the crown so branches show
        r_ = int(lerp(60, 170, light))
        col = (int(r_ * 0.62), int(lerp(110, 205, light)), int(lerp(52, 92, light)), 255)
        leaf(df, lx, ly, rnd.uniform(3.2, 6.5) * h, rnd.uniform(0, math.pi), col)
    # clove bud clusters: little stems with round pink-red heads
    for _ in range(14):
        while True:
            bx, by = ccx + rnd.uniform(-crx, crx) * 0.85, ccy + rnd.uniform(-cry, cry) * 0.8
            if in_crown(bx, by):
                break
        for _k in range(rnd.randint(3, 5)):
            sx, sy = bx + rnd.uniform(-4, 4) * h, by + rnd.uniform(-3, 3) * h
            ln = rnd.uniform(3, 5) * h
            a = math.pi / 2 + rnd.uniform(-0.6, 0.6)
            ex, ey = sx + math.cos(a) * ln, sy - math.sin(a) * ln
            df.line([(sx * S, sy * S), (ex * S, ey * S)], fill=(150, 72, 54, 255), width=max(1, int(1.2 * h * S)))
            hr = rnd.uniform(1.2, 1.8) * h
            col = rnd.choice([(226, 112, 112), (206, 92, 100), (236, 146, 126)])
            df.ellipse([(ex - hr) * S, (ey - hr) * S, (ex + hr) * S, (ey + hr) * S], fill=col + (255,))
    back = back.resize((W, H), Image.LANCZOS).filter(ImageFilter.GaussianBlur(0.5 * h))
    trunk = trunk.resize((W, H), Image.LANCZOS)
    front = front.resize((W, H), Image.LANCZOS)
    back.alpha_composite(trunk)
    return {"back": back, "front": front}


def clove_mark(size, color):
    """The Clove mark: a clove bud - a round head sitting in a four-pointed
    crown of sepals, on a long tapering stem."""
    S = 4
    w = h = int(size * S)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    a = color[3] if len(color) > 3 else 255
    dark = (int(color[0] * 0.80), int(color[1] * 0.74), int(color[2] * 0.92), a)
    light = (min(255, int(color[0] * 1.10)), min(255, int(color[1] * 1.14)), min(255, int(color[2] * 1.02)), a)
    P = lambda x, y: (x * w, y * h)
    # stem
    d.polygon([P(0.43, 0.40), P(0.57, 0.40), P(0.52, 0.98), P(0.48, 0.98)], fill=dark)
    # crown of four sepals
    d.polygon([P(0.36, 0.42), P(0.64, 0.42), P(0.80, 0.20), P(0.63, 0.30), P(0.58, 0.12), P(0.50, 0.28),
               P(0.42, 0.12), P(0.37, 0.30), P(0.20, 0.20)], fill=dark)
    # the round head
    r = 0.135
    d.ellipse([P(0.5 - r, 0.20 - r), P(0.5 + r, 0.20 + r)], fill=light)
    d.ellipse([P(0.5 - r * 0.45, 0.20 - r * 0.7), P(0.5 - r * 0.05, 0.20 - r * 0.3)], fill=(255, 255, 255, int(a * 0.45)))
    return im.resize((int(size), int(size)), Image.LANCZOS)


CREDIT = ("created by ", "CovGNG")


def credit_image(size, fill=(232, 240, 236), alpha=210):
    """'created by CovGNG' as a small RGBA picture: light weight, then the name in medium."""
    fa = ImageFont.truetype(POPPINS_LIGHT, max(8, int(round(size))))
    fb = ImageFont.truetype(POPPINS, max(8, int(round(size))))
    tmp = ImageDraw.Draw(Image.new("RGBA", (4, 4)))
    ba, bb = tmp.textbbox((0, 0), CREDIT[0], font=fa), tmp.textbbox((0, 0), CREDIT[1], font=fb)
    wa = tmp.textlength(CREDIT[0], font=fa)
    top = min(ba[1], bb[1])
    w = int(math.ceil(wa + bb[2])) + 2
    h = int(math.ceil(max(ba[3], bb[3]) - top)) + 2
    w += 2
    h += 2
    txt = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(txt)
    d.text((0, -top), CREDIT[0], font=fa, fill=fill + (int(alpha * 0.88),))
    d.text((wa, -top), CREDIT[1], font=fb, fill=fill + (alpha,))
    # a tight dark edge so the small text reads on bright parts of a wallpaper
    edge = txt.split()[3].filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(0.8)).point(lambda v: v * 0.55)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sh = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sh.putalpha(edge)
    im.alpha_composite(sh, (1, 1))
    im.alpha_composite(txt)
    return im


BRAND = "zappo"
GOLD = (250, 214, 92)
_ZAPPO = None


def clean_cutout(z):
    """tidy a cut-out drawing's edge: the paper it was cut from shows as a light
    rim on dark backgrounds, so pull the edge in by a pixel, soften it, and take
    the paper's white back out of the half-transparent edge pixels. The inside of
    the drawing (eye whites, highlights, cream fur) is left exactly as it is."""
    a = np.asarray(z.convert("RGBA")).astype(np.float32)
    rgb, al = a[..., :3], a[..., 3] / 255.0
    mask = Image.fromarray((al * 255).astype(np.uint8))
    mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.6))
    al2 = np.asarray(mask).astype(np.float32) / 255.0
    al2 = np.minimum(al2, al)
    # un-mix from white where the old edge was part paper: fg = (c - (1-a)*255) / a
    edge = (al > 0.02) & (al < 0.98)
    fg = rgb.copy()
    fg[edge] = np.clip((rgb[edge] - (1 - al[edge, None]) * 255.0) / al[edge, None], 0, 255)
    out = np.dstack([fg, al2 * 255.0]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def enhance_cutout(z):
    """the drawing at twice its size, with the compression blocks flattened
    (edge-preserving) and the line work crisped; the edge stays anti-aliased"""
    import cv2
    a = np.asarray(z.convert("RGBA"))
    bgr = cv2.cvtColor(np.ascontiguousarray(a[..., :3]), cv2.COLOR_RGB2BGR)
    h, w = bgr.shape[:2]
    up = cv2.resize(bgr, (w * 2, h * 2), interpolation=cv2.INTER_LANCZOS4)
    for _ in range(2):
        up = cv2.bilateralFilter(up, 9, 22, 6)
    lab = cv2.cvtColor(up, cv2.COLOR_BGR2LAB)
    l, aa, bb = cv2.split(lab)
    lf = l.astype(np.float32)
    for sigma, amt in ((1.6, 1.1), (4.0, 0.35)):
        lf = cv2.addWeighted(lf, 1 + amt, cv2.GaussianBlur(lf, (0, 0), sigma), -amt, 0)
    up = cv2.cvtColor(cv2.merge([np.clip(lf, 0, 255).astype(np.uint8), aa, bb]), cv2.COLOR_LAB2BGR)
    hsv = cv2.cvtColor(up, cv2.COLOR_BGR2HSV).astype(np.float32)
    hsv[..., 1] = np.clip(hsv[..., 1] * 1.08, 0, 255)
    up = cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2BGR)
    al = cv2.resize(a[..., 3], (w * 2, h * 2), interpolation=cv2.INTER_CUBIC).astype(np.float32) / 255
    al = np.clip((al - 0.15) / 0.7, 0, 1)
    al = al * al * (3 - 2 * al)
    rgba = np.dstack([cv2.cvtColor(up, cv2.COLOR_BGR2RGB), (al * 255).astype(np.uint8)])
    return Image.fromarray(rgba, "RGBA")


def zappo_cutout():
    """Zappo (the electric cub): zappo_cut.png (already cut out), edge cleaned,
    then enhanced at twice the size so every use of him downscales from a clean source"""
    global _ZAPPO
    if _ZAPPO is None:
        z = Image.open(os.path.join(os.path.dirname(__file__), "zappo_cut.png")).convert("RGBA")
        z = enhance_cutout(clean_cutout(z))
        _ZAPPO = z.crop(z.getbbox())
    return _ZAPPO


_EMBER = None


def ember_cutout():
    """Fire Zappo: the same drawing in black and red (see ember.py)"""
    global _EMBER
    if _EMBER is None:
        from ember import ember
        e, _ = ember(np.asarray(zappo_cutout()))
        _EMBER = Image.fromarray(e, "RGBA")
    return _EMBER


def brand_mark(h, alpha=255):
    """the mark next to the wordmark: Zappo himself, h pixels tall"""
    z = zappo_cutout().copy()
    z.thumbnail((int(h * 1.2), int(h)), Image.LANCZOS)
    if alpha < 255:
        a = z.split()[3].point(lambda v: v * alpha // 255)
        z.putalpha(a)
    return z


def draw_wordmark(img, x, y, size, anchor="rb", alpha=255, sub=None, credit=False):
    img = img.convert("RGBA")
    font = ImageFont.truetype(POPPINS, int(size))
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    text = BRAND
    tb = d.textbbox((0, 0), text, font=font)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]
    mark = brand_mark(size * 1.25, alpha)
    gap = size * 0.18
    total_w = mark.width + gap + tw
    cred = (credit_image(size * (0.50 if anchor == "rb" else 0.28), fill=(255, 255, 255) if anchor == "rb" else (232, 240, 236),
                         alpha=245 if anchor == "rb" else 200) if credit else None)
    if anchor == "rb":
        if cred is not None:  # the credit sits under the wordmark, right-aligned
            layer.alpha_composite(cred, (int(x - cred.width), int(y - cred.height)))
            y -= cred.height + size * 0.10
        x0, y0 = x - total_w, y - max(th, mark.height)
    else:  # centred on x, y = top
        x0, y0 = x - total_w / 2, y
    my = y0 + (max(th, mark.height) - mark.height) / 2
    layer.alpha_composite(mark, (int(x0), int(my)))
    d.text((x0 + mark.width + gap - tb[0], y0 + (max(th, mark.height) - th) / 2 - tb[1]), text, font=font, fill=CREAM + (alpha,))
    if sub:
        sf = ImageFont.truetype(POPPINS_LIGHT, int(size * 0.32))
        sb = d.textbbox((0, 0), sub, font=sf)
        d.text((x0 + total_w / 2 - (sb[2] - sb[0]) / 2 - sb[0], y0 + max(th, mark.height) + size * 0.35), sub, font=sf,
               fill=GOLD + (int(alpha * 0.9),))
    if cred is not None and anchor != "rb":
        cy = y0 + max(th, mark.height) + size * (0.35 + 0.32 + 0.45 if sub else 0.35)
        layer.alpha_composite(cred, (int(x0 + total_w / 2 - cred.width / 2), int(cy)))
    shadow = layer.split()[3].filter(ImageFilter.GaussianBlur(size * 0.08))
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sh.putalpha(shadow.point(lambda v: v * 0.5))
    img.alpha_composite(sh)
    img.alpha_composite(layer)
    return img.convert("RGB")


def boot_logo(W=640, H=480):
    img = gradient(W, H, [(0.0, (12, 44, 41)), (1.0, (5, 20, 19))])
    out = Image.fromarray(img.astype(np.uint8), "RGB")
    out = draw_wordmark(out, W / 2, H * 0.40, 78, anchor="ct", sub="GarlicOS  ·  RG35XX", credit=True)
    return out


def save_bmp24(img, path):
    img.convert("RGB").save(path, "BMP")


if __name__ == "__main__":
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    wp = wallpaper(640, 480)
    wp.save(os.path.join(outdir, "wallpaper.png"), optimize=True)
    wallpaper(1920, 1080).save(os.path.join(outdir, "Clove Lagoon 1920x1080.png"), optimize=True)
    # theme background: the same scene, softened and darkened behind the menu
    bg = wallpaper(640, 480, wordmark=False).filter(ImageFilter.GaussianBlur(6))
    bg = Image.blend(bg, Image.new("RGB", bg.size, (6, 22, 21)), 0.45)
    bg.save(os.path.join(outdir, "lagoon-background.png"), optimize=True)
    logo = boot_logo()
    logo.save(os.path.join(outdir, "boot_logo.png"))
    save_bmp24(logo, os.path.join(outdir, "boot_logo.bmp"))
    print("done")
