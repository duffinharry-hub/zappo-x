#!/usr/bin/env python3
"""Clove "Swirl" artwork: glossy folded ribbons spiralling into a glass orb
that carries the Clove mark, in several colourways. Everything is drawn
procedurally (height field -> normals -> lighting), so it is original art.

usage: swirl.py OUTDIR
writes, per colourway NAME:
  NAME/wallpaper.png        640x480  screensaver picture
  NAME/background.png       640x480  dimmed version for behind the menus
  NAME/theme.json           the Clove theme
  pc/Clove Swirl NAME 1920x1080.png
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_art import POPPINS, credit_image  # noqa: E402
import logos  # noqa: E402

# name: (deep, mid, light, accent for the UI, accent text)
COLOURWAYS = {
    "Green": ((6, 52, 10), (52, 168, 38), (190, 245, 120), "#7be05a", "#0b2a06"),
    "Amber": ((60, 26, 4), (196, 120, 34), (255, 222, 150), "#ffb347", "#2e1600"),
    "Blue": ((6, 22, 70), (40, 104, 214), (170, 215, 255), "#6fb4ff", "#04142e"),
    "Violet": ((30, 8, 64), (130, 70, 214), (226, 196, 255), "#c3a2f7", "#1d0b33"),
    "Gold": ((70, 46, 2), (214, 168, 30), (255, 238, 160), "#ffd34d", "#2e2000"),
    "Graphite": ((10, 11, 14), (70, 76, 88), (205, 214, 228), "#c9d3e6", "#11141a"),
}


def palette(t, deep, mid, light):
    """map 0..1 through deep -> mid -> light"""
    t = np.clip(t, 0, 1)[..., None]
    deep, mid, light = (np.array(c, np.float32) for c in (deep, mid, light))
    lo = deep + (mid - deep) * np.clip(t * 2, 0, 1)
    hi = mid + (light - mid) * np.clip(t * 2 - 1, 0, 1)
    return np.where(t < 0.5, lo, hi)


def swirl_field(W, H, cx, cy, seed=0):
    """height field of ribbons twisting into (cx, cy)"""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    s = float(H)
    dx, dy = (xx - cx) / s, (yy - cy) / s
    r = np.sqrt(dx * dx + dy * dy) + 1e-4
    th = np.arctan2(dy, dx)
    # logarithmic spiral: the twist tightens towards the centre
    phi = th + 2.3 * np.log(r + 0.06)
    # a slow wobble so the ribbons fold rather than look machine-made
    phi += 0.22 * np.sin(r * 9.0 + th * 2.0 + seed)
    arms = 3
    band = np.sin(arms * phi)
    # flatten the crests into broad ribbons with soft folds between them
    h = np.tanh(band * 2.6) * 0.5 + 0.5
    # ribbons rise towards the centre a little (a shallow funnel)
    h = h * (0.55 + 0.45 * np.exp(-r * 2.2)) - 0.35 * np.exp(-r * 6.0)
    return h, r, th


def shade(h, r, deep, mid, light, relief=15.0, light_dir=(-0.45, -0.65, 0.62)):
    gy, gx = np.gradient(h)
    n = np.dstack([-gx * relief * h.shape[0] / 480.0, -gy * relief * h.shape[0] / 480.0, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    L = np.array(light_dir, np.float32)
    L /= np.linalg.norm(L)
    diff = np.clip((n * L).sum(axis=2), 0, 1)
    V = np.array([0, 0, 1.0], np.float32)
    Hv = (L + V) / np.linalg.norm(L + V)
    spec = np.clip((n * Hv).sum(axis=2), 0, 1) ** 60
    sheen = np.clip((n * Hv).sum(axis=2), 0, 1) ** 8
    tone = 0.06 + 0.62 * diff + 0.30 * h
    tone += 0.35 * np.exp(-r * 3.2)  # glow from the centre
    tone *= 1.0 - 0.45 * np.clip(r - 0.35, 0, 1) ** 1.2  # vignette
    img = palette(tone, deep, mid, light)
    img += (np.array(light, np.float32) * 0.35 + 255 * 0.65 - img) * np.clip(sheen * 0.30 + spec * 0.95, 0, 1)[..., None]
    return np.clip(img, 0, 255)


def orb(size, accent_dark, rim, mark_key="bud"):
    """glossy glass orb with the Clove mark, RGBA, size x size (plus margin for the glow)"""
    S = 3
    R = size * S / 2.0
    pad = int(R * 0.6)
    N = int(2 * R + 2 * pad)
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    cx = cy = N / 2.0
    dx, dy = (xx - cx) / R, (yy - cy) / R
    d2 = dx * dx + dy * dy
    inside = d2 <= 1.0
    z = np.sqrt(np.clip(1 - d2, 0, 1))
    n = np.dstack([dx, dy, z])
    L = np.array([-0.5, -0.7, 0.5], np.float32)
    L /= np.linalg.norm(L)
    diff = np.clip((n * L).sum(axis=2), 0, 1)
    Hv = (L + np.array([0, 0, 1.0])) / np.linalg.norm(L + np.array([0, 0, 1.0]))
    spec = np.clip((n * Hv).sum(axis=2), 0, 1) ** 40
    base = np.array([226, 231, 236], np.float32)
    col = base * (0.62 + 0.42 * diff)[..., None]
    # coloured reflection of the swirl around the lower rim
    rimmask = np.clip((np.sqrt(d2) - 0.55) / 0.45, 0, 1) ** 2 * np.clip(dy + 0.2, 0, 1.2)
    col += (np.array(rim, np.float32) - col) * (rimmask * 0.75)[..., None]
    col += (255 - col) * (spec * 0.9)[..., None]
    rgba = np.zeros((N, N, 4), np.float32)
    rgba[..., :3] = col
    # anti-aliased edge
    edge = np.clip((1.0 - np.sqrt(d2)) * R / 1.5, 0, 1)
    rgba[..., 3] = edge * 255
    im = Image.fromarray(rgba.astype(np.uint8), "RGBA")

    # the mark, inset in the accent colour with a soft inner shadow
    msize = int(R * 1.22)
    from make_art import brand_mark  # Zappo, in full colour
    mk = brand_mark(msize * 0.9)
    pad_im = Image.new("RGBA", (msize, msize), (0, 0, 0, 0))
    pad_im.alpha_composite(mk, ((msize - mk.width) // 2, (msize - mk.height) // 2 + msize // 20))
    mk = pad_im
    mx, my = int(cx - msize / 2), int(cy - msize / 2)
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    sh.alpha_composite(Image.new("RGBA", mk.size, (0, 0, 0, 0)), (0, 0))
    a = mk.split()[3].point(lambda v: v * 0.55)
    shadow = Image.new("RGBA", mk.size, (0, 0, 0, 255))
    shadow.putalpha(a)
    sh.alpha_composite(shadow, (mx + int(R * 0.02), my + int(R * 0.035)))
    sh = sh.filter(ImageFilter.GaussianBlur(R * 0.025))
    im.alpha_composite(sh)
    im.alpha_composite(mk, (mx, my))
    # a slight lift on the mark's upper edge
    hl = Image.new("RGBA", mk.size, (255, 255, 255, 0))
    hl.putalpha(mk.split()[3].point(lambda v: v * 0.25))
    im.alpha_composite(hl, (mx - int(R * 0.008), my - int(R * 0.012)))
    im.alpha_composite(mk, (mx, my))

    # the classic glossy cap on the top half
    cap = Image.new("L", im.size, 0)
    cd = ImageDraw.Draw(cap)
    cd.ellipse([cx - R * 0.72, cy - R * 0.93, cx + R * 0.72, cy - R * 0.02], fill=255)
    capg = np.asarray(cap, np.float32) / 255.0
    fade = np.clip(1 - (yy - (cy - R * 0.93)) / (R * 0.95), 0, 1) ** 1.3
    capa = (capg * fade * 0.62 * inside * 255).astype(np.uint8)
    capl = Image.new("RGBA", im.size, (255, 255, 255, 0))
    capl.putalpha(Image.fromarray(capa).filter(ImageFilter.GaussianBlur(R * 0.01)))
    im.alpha_composite(capl)

    # clip everything to the sphere
    alpha = np.asarray(im.split()[3], np.float32) * edge
    im.putalpha(Image.fromarray(alpha.astype(np.uint8)))
    im = im.resize((N // S, N // S), Image.LANCZOS)
    return im, pad // S


def place_orb(base, cx, cy, size, cw, mark_key="bud"):
    deep, mid, light, accent, _ = cw
    acc = tuple(int(accent[i:i + 2], 16) for i in (1, 3, 5))
    accent_dark = tuple(int(c * 0.78) for c in mid)
    ob, pad = orb(size, accent_dark, light, mark_key)
    W, H = base.size
    # glow + shadow under the orb
    glow = Image.new("RGBA", base.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    g = size * 0.75
    gd.ellipse([cx - g, cy - g, cx + g, cy + g], fill=tuple(light) + (110,))
    glow = glow.filter(ImageFilter.GaussianBlur(size * 0.22))
    base.alpha_composite(glow)
    sh = Image.new("RGBA", base.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(sh)
    sd.ellipse([cx - size * 0.42, cy + size * 0.40, cx + size * 0.42, cy + size * 0.58], fill=(0, 0, 0, 120))
    base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(size * 0.06)))
    base.alpha_composite(ob, (int(cx - ob.width / 2), int(cy - ob.height / 2)))
    return base


def wordmark(base, x, y, size, anchor, alpha=235, credit=True):
    font = ImageFont.truetype(POPPINS, int(size))
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    from make_art import BRAND, brand_mark
    tb = d.textbbox((0, 0), BRAND, font=font)
    zm = brand_mark(size * 1.2, alpha)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]
    cred = credit_image(size * 0.46, fill=(255, 255, 255), alpha=245) if credit else None
    if anchor == "ct":
        x0, y0 = x - tw / 2, y
        if cred is not None:
            layer.alpha_composite(cred, (int(x - cred.width / 2), int(y0 + th + size * 0.30)))
    else:  # right-bottom, with the credit under it
        if cred is not None:
            layer.alpha_composite(cred, (int(x - cred.width), int(y - cred.height)))
            y -= cred.height + size * 0.12
        x0, y0 = x - tw, y - th
    d.text((x0 - tb[0], y0 - tb[1]), BRAND, font=font, fill=(255, 255, 255, alpha))
    layer.alpha_composite(zm, (int(x0 - zm.width - size * 0.15), int(y0 + th / 2 - zm.height / 2)))
    sh = layer.split()[3].filter(ImageFilter.GaussianBlur(size * 0.08)).point(lambda v: v * 0.55)
    shl = Image.new("RGBA", base.size, (0, 0, 0, 0))
    shl.putalpha(sh)
    base.alpha_composite(shl, (0, int(size * 0.04)))
    base.alpha_composite(layer)
    return base


def render(W, H, cw, seed, with_orb=True, orb_xy=(0.5, 0.47), orb_size=0.29, mark="ct", mark_key="bud"):
    deep, mid, light, _, _ = cw
    cx, cy = W * orb_xy[0], H * orb_xy[1]
    S = 2  # supersample the ribbons
    h, r, th = swirl_field(W * S, H * S, cx * S, cy * S, seed)
    img = shade(h, r, deep, mid, light)
    pil = Image.fromarray(img.astype(np.uint8), "RGB").resize((W, H), Image.LANCZOS).convert("RGBA")
    if with_orb:
        size = H * orb_size
        place_orb(pil, cx, cy, size, cw, mark_key)
        if mark == "ct":
            wordmark(pil, cx, cy + size * 0.70, H * 0.075, "ct")
        elif mark == "rb":
            wordmark(pil, W - H * 0.045, H - H * 0.045, H * 0.06, "rb")
    return pil


def orb_with(mark_key, W, H, cw_index):
    """a small swirl swatch with the orb, for the logo sheet"""
    cw = list(COLOURWAYS.values())[cw_index % len(COLOURWAYS)]
    return render(W, H, cw, cw_index, orb_xy=(0.5, 0.5), orb_size=0.62, mark=None, mark_key=mark_key)


def theme_background(W, H, cw, seed):
    """the swirl alone, dimmed and softened so menus stay readable"""
    pil = render(W, H, cw, seed, with_orb=False, orb_xy=(0.62, 0.55))
    a = np.asarray(pil.convert("RGB"), np.float32)
    lum = a.mean(axis=2, keepdims=True)
    a = (a * 0.75 + lum * 0.25) * 0.42
    out = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.6))
    return out


def theme_json(name, cw):
    deep, mid, light, accent, accent_text = cw
    hx = lambda c, a="": "#%02x%02x%02x%s" % (c[0], c[1], c[2], a)
    sdeep = tuple(int(c * 0.55) for c in deep)
    return {
        "name": "Swirl " + name,
        "colors": {
            "background": hx(tuple(int(c * 0.9) for c in deep)),
            "background2": hx(tuple(int(c * 0.45) for c in deep)),
            "surface": hx(sdeep, "aa"),
            "surface_highlight": hx(tuple(min(255, int(c * 1.6)) for c in deep), "cc"),
            "text": "#f6f8f4",
            "text_dim": hx(tuple(int(c * 0.55 + 255 * 0.3) for c in light)),
            "accent": accent,
            "accent_text": accent_text,
            "favorite": "#ffd479",
            "shadow": "#00000080",
        },
        "font": {"file": "FiraSansCondensed-Regular.ttf", "title": 28, "body": 23, "small": 18},
        "background": "background.png",
        "background_dim": 0,
        "radius": 14,
        "home_layout": "carousel",
        "game_layout": "list_art",
        "list_rows": 9,
        "art_side": "right",
        "text_align": "left",
    }


MARK = os.environ.get("CLOVE_MARK", "flower")

if __name__ == "__main__":
    out = sys.argv[1]
    only = sys.argv[2:] or list(COLOURWAYS)
    os.makedirs(os.path.join(out, "pc"), exist_ok=True)
    for i, name in enumerate(COLOURWAYS):
        if name not in only:
            continue
        cw = COLOURWAYS[name]
        d = os.path.join(out, "Swirl " + name)
        os.makedirs(d, exist_ok=True)
        # handheld screensaver: orb in the middle, wordmark bottom-right
        # (the clock sits top-left and what's playing bottom-left)
        render(640, 480, cw, i, orb_xy=(0.5, 0.52), orb_size=0.30, mark="rb", mark_key=MARK).convert("RGB").save(os.path.join(d, "wallpaper.png"), optimize=True)
        theme_background(640, 480, cw, i).save(os.path.join(d, "background.png"), optimize=True)
        with open(os.path.join(d, "theme.json"), "w") as f:
            json.dump(theme_json(name, cw), f, indent=2)
            f.write("\n")
        render(1920, 1080, cw, i, orb_xy=(0.5, 0.44), orb_size=0.26, mark="ct", mark_key=MARK).convert("RGB").save(
            os.path.join(out, "pc", "Clove Swirl %s 1920x1080.png" % name), optimize=True)
        print("done", name, flush=True)
