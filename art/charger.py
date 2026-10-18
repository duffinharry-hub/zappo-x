"""The RG35XX's charging screen and boot logo in Clove's style, for the TF1
boot partition: boot_logo.bmp, charger_logo.bmp (640x480, shown while the
handheld charges switched off), 0..5_charger_frame.bmp (120x186, the battery
drawn over it, one frame a second) and battery_low.bmp.
usage: charger.py OUTDIR   (writes the .bmp files and gzipped copies)"""
import gzip
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
import random  # noqa: E402

from PIL import ImageOps  # noqa: E402

from ember import draw_bolts, draw_embers, draw_flicks, draw_sparks  # noqa: E402
from make_art import CREAM, LAVENDER, POPPINS, POPPINS_LIGHT, draw_wordmark, ember_cutout, gradient, save_bmp24, zappo_cutout  # noqa: E402

W, H = 640, 480
FW, FH = 120, 186
BG = (6, 23, 22)  # flat, so the frame's box blends into the screen
FRAME_POS = ((W - FW) // 2, (H - FH) // 2)  # where the system draws the frames (centred)


def boot_logo(k=1):
    img = gradient(W * k, H * k, [(0.0, (12, 44, 41)), (1.0, (5, 20, 19))])
    out = Image.fromarray(img.astype(np.uint8), "RGB")
    return draw_wordmark(out, W * k / 2, H * k * 0.40, 78 * k, anchor="ct", credit=True)


def charger_logo(k=1, fx=0, t=None):
    """a flat background (the battery frame sits in the middle) with Zappo on
    the left, crackling with electricity, and fire Zappo on the right, giving
    off embers, both facing the battery; 'charging' under it. fx picks the
    moment of the sparks and embers (the charger shows one frame a second, so
    they flicker and drift). k > 1 draws the same screen k times bigger."""
    out = Image.new("RGB", (W * k, H * k), BG).convert("RGBA")
    fx0, fy0 = FRAME_POS[0] * k, FRAME_POS[1] * k
    zl = zappo_cutout().copy()
    zl.thumbnail((236 * k, 236 * k), Image.LANCZOS)
    zr = ImageOps.mirror(ember_cutout())
    zr.thumbnail((236 * k, 236 * k), Image.LANCZOS)
    gap = 12 * k
    lx, rx = fx0 - zl.width - gap, fx0 + FW * k + gap
    ly = fy0 + FH * k - zl.height + 6 * k
    ry = fy0 + FH * k - zr.height + 6 * k
    # soft shadows under them; a faint warm glow behind the fire one
    sh = Image.new("RGBA", out.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(sh)
    for x, y, z in ((lx, ly, zl), (rx, ry, zr)):
        sd.ellipse((x - 8 * k, y + z.height * 0.78, x + z.width + 8 * k, y + z.height + 12 * k), fill=(0, 0, 0, 110))
    sd.ellipse((rx - 10 * k, ry + zr.height * 0.15, rx + zr.width + 10 * k, ry + zr.height * 1.02), fill=(220, 90, 20, 38))
    out.alpha_composite(sh.filter(ImageFilter.GaussianBlur(12 * k)))
    out.alpha_composite(zl, (int(lx), int(ly)))
    out.alpha_composite(zr, (int(rx), int(ry)))
    # electricity round the left one (floating bolts, a couple of arcs), fire round the right one
    fxl = Image.new("RGBA", out.size, (0, 0, 0, 0))
    rnd = random.Random(1000 + fx)
    al = np.asarray(zl)[..., 3]
    draw_sparks(fxl, al, lx, ly, rnd, 2, (255, 206, 64), (255, 252, 230), 18 * k, 1.5 * k)
    draw_bolts(fxl, (lx - 2 * k, ly + 6 * k, lx + zl.width + 2 * k, ly + zl.height - 6 * k), rnd, 6, (255, 196, 40), (255, 238, 150), 12 * k)
    ar = np.asarray(zr)[..., 3]
    draw_flicks(fxl, ar, rx, ry, rnd, 5, k)
    emb = Image.new("RGBA", out.size, (0, 0, 0, 0))
    draw_embers(emb, (rx - 10 * k, ry - 24 * k, rx + zr.width + 10 * k, ry + zr.height * 0.95), random.Random(77), fx if t is None else t, 24, 1.2 * k)
    # the embers rise from behind him: none in front of his face or body
    body = np.zeros((out.height, out.width), np.float32)
    body[int(ry):int(ry) + zr.height, int(rx):int(rx) + zr.width] = ar[:out.height - int(ry), :out.width - int(rx)] / 255.0
    ea = np.asarray(emb).copy()
    ea[..., 3] = (ea[..., 3] * (1 - body)).astype(np.uint8)
    fxl.alpha_composite(Image.fromarray(ea, "RGBA"))
    out.alpha_composite(fxl)
    out = out.convert("RGB")
    d = ImageDraw.Draw(out)
    f = ImageFont.truetype(POPPINS_LIGHT, 22 * k)
    t = "charging"
    tb = d.textbbox((0, 0), t, font=f)
    d.text((W * k / 2 - (tb[2] - tb[0]) / 2 - tb[0], fy0 + (FH + 26) * k), t, font=f, fill=(170, 200, 190))
    return out


def battery(level, low=False, k=1):
    """level 0..5 of the battery; a gold fill with a soft glow and a bolt"""
    S = 4 * k  # drawn big, then shrunk, for smooth edges
    im = Image.new("RGBA", (FW * S, FH * S), BG + (255,))
    d = ImageDraw.Draw(im)
    bx0, by0, bx1, by1 = 10 * S, 22 * S, (FW - 10) * S, (FH - 8) * S
    r = 16 * S
    # the cap
    d.rounded_rectangle((FW * S / 2 - 22 * S, 6 * S, FW * S / 2 + 22 * S, 26 * S), radius=6 * S, fill=(120, 132, 128, 255))
    # the body
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=r, outline=(200, 212, 206, 255), width=4 * S, fill=(10, 34, 32, 255))
    inner = (bx0 + 9 * S, by0 + 9 * S, bx1 - 9 * S, by1 - 9 * S)
    ih = inner[3] - inner[1]
    if low:
        fill_col = (255, 96, 96, 255)
        top = inner[3] - ih * 0.12
    else:
        fill_col = (250, 214, 92, 255)
        top = inner[3] - ih * (level / 5.0)
    if (low or level > 0) and top < inner[3] - 2:
        glow = Image.new("RGBA", im.size, (0, 0, 0, 0))
        gd = ImageDraw.Draw(glow)
        gd.rounded_rectangle((inner[0], top, inner[2], inner[3]), radius=8 * S, fill=fill_col[:3] + (150,))
        glow = glow.filter(ImageFilter.GaussianBlur(6 * S))
        im.alpha_composite(glow)
        d.rounded_rectangle((inner[0], top, inner[2], inner[3]), radius=8 * S, fill=fill_col)
    if not low:
        # a bolt in the middle
        cx, cy = FW * S / 2, (by0 + by1) / 2
        pts = [(cx + 4 * S, cy - 30 * S), (cx - 14 * S, cy + 4 * S), (cx - 1 * S, cy + 4 * S), (cx - 5 * S, cy + 30 * S),
               (cx + 14 * S, cy - 5 * S), (cx + 1 * S, cy - 5 * S)]
        sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).polygon([(x + 2 * S, y + 3 * S) for x, y in pts], fill=(0, 0, 0, 110))
        im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(2 * S)))
        d.polygon(pts, fill=CREAM + (255,))
    else:
        f = ImageFont.truetype(POPPINS, 64 * S)
        tb = d.textbbox((0, 0), "!", font=f)
        d.text((FW * S / 2 - (tb[2] - tb[0]) / 2 - tb[0], (by0 + by1) / 2 - (tb[3] - tb[1]) / 2 - tb[1]), "!", font=f,
               fill=(255, 120, 120, 255))
    return im.resize((FW * k, FH * k), Image.LANCZOS).convert("RGB")


SS = 3  # the handheld's pictures are drawn 3x and shrunk: the smoothest edges its screen can show


def logo_1x(fx=0):
    return charger_logo(SS, fx).resize((W, H), Image.LANCZOS)


def charging_screen(level, k=1, fx=None):
    """the whole screen as the handheld shows it while charging"""
    im = charger_logo(k, level if fx is None else fx)
    im.paste(battery(level, k=k), (FRAME_POS[0] * k, FRAME_POS[1] * k))
    return im


def gz(path):
    with open(path, "rb") as f, gzip.open(path + ".gz", "wb", compresslevel=9) as g:
        g.write(f.read())


def ramdisk_pngs(out, logo):
    """the pictures for the ramdisk's charger (see ramdisk.py): whole screens,
    the battery frame composed over the charging screen"""
    d = os.path.join(out, "ramdisk-png")
    os.makedirs(d, exist_ok=True)
    names = {"battery_%d" % i: (battery(i), i) for i in range(6)}
    names["battery_charge"] = (battery(5), 6)
    names["battery_fail"] = (battery(0, low=True), 7)
    for name, (fr, fx) in names.items():
        im = logo_1x(fx)   # each frame its own moment of sparks and embers
        im.paste(fr, FRAME_POS)
        im.save(os.path.join(d, name + ".png"), optimize=True)
    return d


if __name__ == "__main__":
    if sys.argv[1] == "--picture":   # charger.py --picture K LEVEL OUT.png : a K-times-bigger picture of the screen
        charging_screen(int(sys.argv[3]), int(sys.argv[2])).save(sys.argv[4], optimize=True)
        sys.exit(0)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    files = {"boot_logo.bmp": boot_logo(SS).resize((W, H), Image.LANCZOS), "charger_logo.bmp": logo_1x(0), "battery_low.bmp": battery(0, low=True)}
    for i in range(6):
        files["%d_charger_frame.bmp" % i] = battery(i)
    for name, im in files.items():
        p = os.path.join(out, name)
        save_bmp24(im, p)
        gz(p)
    ramdisk_pngs(out, files["charger_logo.bmp"])
    # a preview: the frames over the charging screen
    prev = Image.new("RGB", (W * 3, H * 2), (0, 0, 0))
    prev.paste(files["boot_logo.bmp"], (0, 0))
    for k in range(5):
        base = files["charger_logo.bmp"].copy()
        base.paste(files["%d_charger_frame.bmp" % (k + 1)], FRAME_POS)
        prev.paste(base, (((k + 1) % 3) * W, ((k + 1) // 3) * H))
    prev.save(os.path.join(out, "preview.png"))
    print("done")
