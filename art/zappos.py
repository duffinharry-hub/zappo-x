"""The Zappo family for the handheld: electric (the original), fire, leaf,
crystal, ice and water - one clean drawing recoloured per element (ember.py),
written at exactly the sizes Zappo shows them, so nothing is scaled on the
device:

  CFW/clove/zappos/<type>-s.png, <type>-s-flip.png   72 px tall: the corners of the Recent / Favorites shelf
  CFW/clove/zappos/<type>-m.png                       120 px tall: above the empty-screen messages
  CFW/clove/lockscreens/1x Zappo <Type>.jpg           640x480 lock screen pictures
  CFW/clove/themes/Zappo <Type>/                      a theme per element: its colours, the picture sharp behind
                                                      every screen (Zappo a little smaller there, so the menus
                                                      don't hide his face) and the full-size one as its wallpaper

Everything is drawn 3x and shrunk (Lanczos), the sharpest the 640x480 screen can show.
usage: zappos.py PACKAGE_CFW_CLOVE_DIR"""
import json
import os
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ember import (PALETTES, draw_bolts, draw_drops, draw_embers, draw_flicks, draw_leaves, draw_shards,  # noqa: E402
                   draw_snow, draw_sparkles, draw_sparks, recolor)
from make_art import zappo_cutout  # noqa: E402

TYPES = ["electric", "fire", "leaf", "crystal", "ice", "water"]
NAMES = {"electric": "Electric", "fire": "Fire", "leaf": "Leaf", "crystal": "Crystal", "ice": "Ice", "water": "Water"}
SS = 3
W, H = 640, 480

# background gradient (top, bottom) and glow per element, and the theme colours
LOOK = {
    "electric": {"bg": ((18, 22, 44), (6, 8, 18)), "glow": (255, 206, 64)},
    "fire": {"bg": ((40, 16, 8), (12, 5, 3)), "glow": (255, 120, 30)},
    "leaf": {"bg": ((14, 34, 16), (4, 12, 6)), "glow": (150, 220, 70)},
    "crystal": {"bg": ((30, 16, 52), (9, 5, 18)), "glow": (190, 120, 255)},
    "ice": {"bg": ((24, 44, 72), (7, 14, 26)), "glow": (170, 220, 255)},
    "water": {"bg": ((8, 28, 60), (2, 8, 20)), "glow": (60, 180, 245)},
}
THEME = {
    "electric": dict(background="#121426", background2="#0a0b16", surface="#1a1d33", surface_highlight="#262a47", text="#f6f3e8",
                     text_dim="#a3a9c9", accent="#f5c842", accent_text="#1a1407", favorite="#c3a2f7"),
    "fire": dict(background="#1a0c06", background2="#0d0603", surface="#2a140a", surface_highlight="#4a220e", text="#fbf3ea",
                 text_dim="#d9b89a", accent="#ff8a1f", accent_text="#2a1200", favorite="#ffc24a"),
    "leaf": dict(background="#0c1a0c", background2="#060f06", surface="#142614", surface_highlight="#24401f", text="#f2f8ec",
                 text_dim="#b8cfa4", accent="#9fd84a", accent_text="#142406", favorite="#e6f59a"),
    "crystal": dict(background="#160c22", background2="#0b0614", surface="#221438", surface_highlight="#3a2560", text="#f6f0ff",
                    text_dim="#c4b4e0", accent="#b57cff", accent_text="#1c0b33", favorite="#e2c8ff"),
    "ice": dict(background="#0c1726", background2="#060c16", surface="#14233a", surface_highlight="#24395c", text="#f4f9ff",
                text_dim="#b4c8e2", accent="#8fd3ff", accent_text="#08203a", favorite="#d8f1ff"),
    "water": dict(background="#061428", background2="#030a16", surface="#0c2240", surface_highlight="#143a66", text="#eef8ff",
                  text_dim="#a8c4e0", accent="#3fc1ff", accent_text="#021a30", favorite="#9fe6ff"),
}

_cache = {}


def zappo(kind):
    if kind not in _cache:
        z = zappo_cutout()
        _cache[kind] = z if kind == "electric" else Image.fromarray(recolor(np.asarray(z), PALETTES[kind])[0], "RGBA")
    return _cache[kind]


def sized(kind, h, flip=False):
    """the drawing at exactly h pixels tall, shrunk in one Lanczos step from the 2x source, edges crisped"""
    z = zappo(kind)
    if flip: z = ImageOps.mirror(z)
    w = round(z.width * h / z.height)
    im = z.resize((w, h), Image.LANCZOS)
    rgb = im.convert("RGB").filter(ImageFilter.UnsharpMask(0.8, 45, 2))
    out = rgb.convert("RGBA"); out.putalpha(im.getchannel("A"))
    return out


def effects(layer, kind, z, x, y, k, seed):
    """the element's particles round a drawing placed at (x, y) on layer"""
    rnd = random.Random(seed)
    box = (x + 4 * k, y + 6 * k, x + z.width - 4 * k, y + z.height - 8 * k)
    a = np.asarray(z)[..., 3]
    if kind == "electric":
        draw_sparks(layer, a, x, y, rnd, 2, (255, 206, 64), (255, 252, 230), 22 * k, 1.6 * k)
        draw_bolts(layer, box, rnd, 7, (255, 196, 40), (255, 238, 150), 14 * k)
    elif kind == "fire":
        draw_flicks(layer, a, x, y, rnd, 5, k * 1.2)
        emb = Image.new("RGBA", layer.size, (0, 0, 0, 0))
        draw_embers(emb, (x - 10 * k, y - 30 * k, x + z.width + 10 * k, y + z.height * 0.95), rnd, 0.3, 30, 1.4 * k)
        body = np.zeros((layer.height, layer.width), np.float32)
        hh, ww = min(z.height, layer.height - y), min(z.width, layer.width - x)
        body[y:y + hh, x:x + ww] = a[:hh, :ww] / 255.0
        ea = np.asarray(emb).copy(); ea[..., 3] = (ea[..., 3] * (1 - body)).astype(np.uint8)
        layer.alpha_composite(Image.fromarray(ea, "RGBA"))
    elif kind == "leaf":
        draw_leaves(layer, box, rnd, 9, 11 * k)
    elif kind == "crystal":
        draw_shards(layer, box, rnd, 6, 12 * k)
        draw_sparkles(layer, box, rnd, 6, 7 * k, (236, 214, 255))
    elif kind == "ice":
        draw_snow(layer, box, rnd, 8, 10 * k)
        draw_sparkles(layer, box, rnd, 5, 6 * k, (230, 246, 255))
    elif kind == "water":
        draw_drops(layer, box, rnd, 9, 7 * k)


def scene(kind, seed=7, size=300, margin=(34, 22)):
    """a 640x480 picture: the element's colours, a glow, and that Zappo bottom-right
    facing left (clear of the lock screen's clock top-left and song card bottom-left);
    size = how tall Zappo stands, margin = his gap from the right and bottom edges"""
    k = SS
    w, h = W * k, H * k
    top, bot = LOOK[kind]["bg"]
    t = np.linspace(0, 1, h)[:, None, None]
    grad = (np.array(top)[None, None, :] * (1 - t) + np.array(bot)[None, None, :] * t) * np.ones((1, w, 1))
    im = Image.fromarray(grad.astype(np.uint8), "RGB").convert("RGBA")
    z = zappo(kind)
    z = ImageOps.mirror(z)
    z.thumbnail((size * k, size * k), Image.LANCZOS)
    x, y = w - z.width - margin[0] * k, h - z.height - margin[1] * k
    glow = Image.new("RGBA", im.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    g = LOOK[kind]["glow"]
    gd.ellipse((x - 60 * k, y - 30 * k, x + z.width + 60 * k, y + z.height + 40 * k), fill=g + (60,))
    im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(70 * k)))
    # soft bokeh in the element's colour
    rnd = random.Random(seed)
    bk = Image.new("RGBA", im.size, (0, 0, 0, 0))
    bd = ImageDraw.Draw(bk)
    for _ in range(18):
        bx, by = rnd.uniform(0, w), rnd.uniform(0, h * 0.85)
        r = rnd.uniform(6, 22) * k
        bd.ellipse((bx - r, by - r, bx + r, by + r), fill=g + (rnd.randint(10, 28),))
    im.alpha_composite(bk.filter(ImageFilter.GaussianBlur(6 * k)))
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse((x - 10 * k, y + z.height * 0.84, x + z.width + 10 * k, y + z.height + 14 * k), fill=(0, 0, 0, 120))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(12 * k)))
    im.alpha_composite(z, (int(x), int(y)))
    fx = Image.new("RGBA", im.size, (0, 0, 0, 0))
    effects(fx, kind, z, int(x), int(y), k, seed)
    im.alpha_composite(fx)
    return im.convert("RGB").resize((W, H), Image.LANCZOS)


def main(out):
    zd = os.path.join(out, "zappos")
    os.makedirs(zd, exist_ok=True)
    for kind in TYPES:
        sized(kind, 72).save(os.path.join(zd, kind + "-s.png"), optimize=True)
        sized(kind, 72, flip=True).save(os.path.join(zd, kind + "-s-flip.png"), optimize=True)
        sized(kind, 120).save(os.path.join(zd, kind + "-m.png"), optimize=True)
    ld = os.path.join(out, "lockscreens")
    os.makedirs(ld, exist_ok=True)
    for i, kind in enumerate(TYPES):
        pic = scene(kind)
        pic.save(os.path.join(ld, "%02d Zappo %s.jpg" % (10 + i, NAMES[kind])), quality=94, subsampling=0, optimize=True)
        td = os.path.join(out, "themes", "Zappo " + NAMES[kind])
        os.makedirs(td, exist_ok=True)
        pic.save(os.path.join(td, "wallpaper.png"), optimize=True)
        scene(kind, size=225, margin=(30, 18)).save(os.path.join(td, "background.png"), optimize=True)
        base = json.load(open(os.path.join(out, "themes", "Zappo", "theme.json")))
        base["name"] = "Zappo " + NAMES[kind]
        base["colors"] = dict(THEME[kind], shadow="#00000080")
        base["background"] = "background.png"
        base["background_dim"] = 70
        base["background_sharp"] = True
        with open(os.path.join(td, "theme.json"), "w") as f:
            json.dump(base, f, indent=2)
            f.write("\n")
    print("done")


if __name__ == "__main__":
    main(sys.argv[1])
