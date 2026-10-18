#!/usr/bin/env python3
"""Lock screen pictures from the user's phone screenshots: cut the picture
out of the screenshot, crop a 4:3 window around the interesting part, resize
to the RG35XX's 640x480, darken the corners a touch so the clock reads, and
add the Clove mark + "created by CovGNG" (same as the Lagoon wallpaper).

usage: lockscreens.py UPLOADDIR OUTDIR"""
import os
import sys

import numpy as np
from PIL import Image, ImageEnhance, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_art import draw_wordmark  # noqa: E402

W, H = 640, 480
# (screenshot, file name, picture x0, x1 in the screenshot, crop top y, crop height (None = 3/4 of width))
PICS = [
    ("78b74392", "01 Sunset Sea", 187, 983, 560, None),
    ("6b148f44", "02 Neon City", 187, 983, 600, None),
    ("9baa7806", "03 Moonlit Clouds", 188, 982, 658, None),
    ("d2059c35", "04 Block River", 190, 980, 640, None),
    ("b069429d", "05 Block Canyon", 188, 982, 420, None),
    ("aa9f69c8", "06 Moon and Tree", 205, 965, 940, None),
    ("626f35ff", "07 Blue Waves", 0, 1170, 420, None),
]


def make(src, x0, x1, top, h):
    w = x1 - x0
    h = h or int(round(w * 3 / 4))
    im = src.crop((x0, top, x1, top + h)).resize((W, H), Image.LANCZOS)
    im = im.filter(ImageFilter.UnsharpMask(radius=1.2, percent=40, threshold=2))
    a = np.asarray(im).astype(np.float32)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    # behind the clock (top left) and along the bottom (now playing, the mark)
    clock = np.clip(1 - (((xx - 120) / 360) ** 2 + ((yy - 70) / 170) ** 2), 0, 1) ** 1.5 * 0.30
    bottom = np.clip((yy - H * 0.70) / (H * 0.30), 0, 1) ** 1.6 * 0.32
    edge = np.clip(((xx - W / 2) / (W * 0.62)) ** 2 + ((yy - H / 2) / (H * 0.70)) ** 2, 0, 1) * 0.18
    dark = np.clip(clock + bottom + edge, 0, 0.5)
    a *= (1 - dark)[..., None]
    out = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGB")
    return draw_wordmark(out, W - 18, H - 14, 26, anchor="rb", alpha=230, credit=True)


def main():
    up, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    for key, name, x0, x1, top, h in PICS:
        src = Image.open(os.path.join(up, key + "-image.png")).convert("RGB")
        out = make(src, x0, x1, top, h)
        p = os.path.join(outdir, name + ".jpg")
        out.save(p, quality=90, subsampling=0, optimize=True)
        print(p, os.path.getsize(p))


if __name__ == "__main__":
    main()
