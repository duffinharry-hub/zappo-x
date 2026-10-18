"""The charging screen from the animated scene (art/zappo_charging_scene.mp4): the
two Zappos either side of a battery that fills.

The handheld can't play a video while it charges switched off: its charger shows
six battery pictures in turn (battery_0..5), plus battery_charge and battery_fail,
and u-boot shows charger_logo.bmp with a 120x186 battery frame drawn in the middle.
So the scene is taken from its sharpest frame, both Zappos held still, and only the
battery is swapped in from the frames where it is 0, 20 ... 100% full; each picture
differs from the next only inside the battery, so the steps never twitch.

The boot partition is 10 MB, so the pictures are slimmed to the size of the drawn
ones (about 165 KB each): the video's grain is cleaned off, the characters are
kept at 6 bits a colour (invisible on fur) and the sky is smoothed, with its stars
and sparkles kept sharp, at 7 bits.

usage: charger_video.py OUTDIR [VIDEO]
writes OUTDIR/charger_logo.bmp(.gz), 0..5_charger_frame.bmp(.gz), battery_low.bmp(.gz)
and OUTDIR/charger/battery_*.png (the ramdisk pictures)"""
import os
import subprocess
import sys
import tempfile

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from charger import FH, FRAME_POS, FW, H, W, gz  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def frames(video):
    d = tempfile.mkdtemp(prefix="zappo-charge-")
    subprocess.run(["ffmpeg", "-v", "error", "-i", video, "-vsync", "0", os.path.join(d, "%03d.png")], check=True)
    return [os.path.join(d, f) for f in sorted(os.listdir(d))]


def battery_box(a):
    """the battery's light outline: the biggest light-grey blob in the middle of the picture"""
    h, w = a.shape[:2]
    x0, y0 = int(w * 0.36), int(h * 0.27)
    win = a[y0:int(h * 0.73), x0:int(w * 0.62)].astype(int)
    m = ((win.max(2) > 150) & ((win.max(2) - win.min(2)) < 45)).astype(np.uint8)
    n, lab, st, _ = cv2.connectedComponentsWithStats(m)
    k = 1 + int(np.argmax(st[1:, cv2.CC_STAT_AREA]))
    x, y, bw, bh = st[k, :4]
    return x0 + x, y0 + y, x0 + x + bw, y0 + y + bh


def fill_level(a, box):
    x0, y0, x1, y1 = box
    b = a[y0 + 20:y1 - 6, x0 + 8:x1 - 8].astype(int)
    yellow = (b[..., 0] > 190) & (b[..., 1] > 160) & (b[..., 2] < 150)
    return float((yellow.mean(1) > 0.5).mean())


def sharpness(a):
    """how crisp the two characters are (left and right thirds, where they stand)"""
    g = cv2.cvtColor(a, cv2.COLOR_RGB2GRAY).astype(np.float32)
    h, w = g.shape
    return sum(float(cv2.Laplacian(g[int(h * .21):int(h * .7), x0:x1], cv2.CV_32F).var())
               for x0, x1 in ((int(w * .04), int(w * .38)), (int(w * .59), int(w * .97))))


def to_screen(a, box):
    """scaled to 640x480 and nudged so the battery sits where the system draws its frame"""
    h, w = a.shape[:2]
    s = W / w
    im = Image.fromarray(a).resize((W, H), Image.LANCZOS)
    cy = (box[1] - 20 + box[3]) / 2 * s           # battery and its cap
    dx = int(round(FRAME_POS[0] + FW / 2 - (box[0] + box[2]) / 2 * s))
    dy = int(round(FRAME_POS[1] + FH / 2 - cy))
    out = np.asarray(im)
    out = np.roll(out, (dy, dx), (0, 1))
    if dy > 0: out[:dy] = out[dy:dy + 1]
    if dy < 0: out[dy:] = out[dy - 1:dy]
    if dx > 0: out[:, :dx] = out[:, dx:dx + 1]
    if dx < 0: out[:, dx:] = out[:, dx - 1:dx]
    return Image.fromarray(out).filter(ImageFilter.UnsharpMask(0.8, 40, 2))


def battery_mask():
    m = Image.new("L", (W, H), 0)
    x, y = FRAME_POS
    ImageDraw.Draw(m).rounded_rectangle((x + 6, y + 4, x + FW - 6, y + FH - 2), radius=10, fill=255)
    return m.filter(ImageFilter.GaussianBlur(2.5))


def slim(img, keep):
    """clean the video grain; characters at 6 bits a colour, the smoothed sky at 7"""
    a = np.asarray(img.convert("RGB"))
    bgr = a[..., ::-1].copy()
    clean = cv2.fastNlMeansDenoisingColored(bgr, None, 5, 5, 7, 21)[..., ::-1].astype(np.float32)
    q = 4
    six = np.clip(np.floor(clean / q + 0.5) * q, 0, 255)
    blur = cv2.GaussianBlur(a.astype(np.float32), (0, 0), 8)
    lum = np.array([.3, .59, .11], np.float32)
    star = ((a.astype(np.float32) @ lum) - (blur @ lum)) > 12          # keep the stars and sparkles sharp
    star = cv2.dilate(star.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(np.float32)
    star = cv2.GaussianBlur(star, (0, 0), 0.7)[..., None]
    bg = blur * (1 - star) + clean * star
    bg = np.clip(np.floor(bg / 2 + 0.5) * 2, 0, 255)                     # 7 bits
    k = keep[..., None]
    return Image.fromarray((six * k + bg * (1 - k)).round().clip(0, 255).astype(np.uint8))


def keep_mask(a, box_screen):
    """where the characters, battery and caption are (kept detailed); the rest is sky"""
    m = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(m)
    x, y = FRAME_POS
    for b in [(18, 86, 262, 352), (378, 82, 634, 352), (x - 2, y - 2, x + FW + 2, y + FH + 3), (x, y + FH + 15, x + FW, y + FH + 66)]:
        d.rounded_rectangle(b, 16, fill=255)
    return np.asarray(m.filter(ImageFilter.GaussianBlur(6)), np.float32) / 255


def failed(img):
    """battery_fail / battery_low: the empty battery's outline and bolt in red"""
    a = np.asarray(img.convert("RGB")).astype(np.float32)
    x, y = FRAME_POS
    r = a[y:y + FH, x:x + FW]
    mx, mn = r.max(2), r.min(2)
    light = ((mx > 120) & ((mx - mn) < 60)).astype(np.float32)
    light[FH - 9:] = 0                     # the battery only, not the glow on the floor round it
    light[:, :8] = 0
    light[:, FW - 8:] = 0
    light = cv2.GaussianBlur(light, (0, 0), 0.6)[..., None]
    lumi = (r @ np.array([.3, .59, .11], np.float32))[..., None] / 255
    red = np.concatenate([lumi * 255, lumi * 80, lumi * 70], 2)
    a[y:y + FH, x:x + FW] = r * (1 - light) + red * light
    return Image.fromarray(a.round().clip(0, 255).astype(np.uint8))


def main(out, video):
    os.makedirs(os.path.join(out, "charger"), exist_ok=True)
    fs = frames(video)
    first = np.asarray(Image.open(fs[0]).convert("RGB"))
    box = battery_box(first)
    arr = [np.asarray(Image.open(f).convert("RGB")) for f in fs]
    lv = np.array([fill_level(a, box) for a in arr])
    full = int(np.argmax(lv))
    best = max(range(len(arr)), key=lambda i: sharpness(arr[i]))   # the scene: its sharpest moment
    base = to_screen(arr[best], box)
    mask = battery_mask()
    keep = keep_mask(None, None)
    picks = [int(np.argmin(np.abs(lv[:full + 1] - lv[full] * k / 5))) for k in range(6)]
    print("scene frame %d, battery frames %s" % (best + 1, [p + 1 for p in picks]))
    shots = []
    for p in picks:
        c = base.copy()
        c.paste(to_screen(arr[p], box), (0, 0), mask)
        shots.append(slim(c, keep))
    fail = slim(failed(base_with(base, to_screen(arr[picks[0]], box), mask)), keep)
    x, y = FRAME_POS
    for i, s in enumerate(shots):
        s.save(os.path.join(out, "charger", "battery_%d.png" % i), optimize=True)
        s.crop((x, y, x + FW, y + FH)).save(os.path.join(out, "%d_charger_frame.bmp" % i), "BMP")
    shots[5].save(os.path.join(out, "charger", "battery_charge.png"), optimize=True)
    fail.save(os.path.join(out, "charger", "battery_fail.png"), optimize=True)
    fail.crop((x, y, x + FW, y + FH)).save(os.path.join(out, "battery_low.bmp"), "BMP")
    shots[0].save(os.path.join(out, "charger_logo.bmp"), "BMP")
    for f in ["charger_logo.bmp", "battery_low.bmp"] + ["%d_charger_frame.bmp" % i for i in range(6)]:
        gz(os.path.join(out, f))
    print("done")


def base_with(base, part, mask):
    c = base.copy()
    c.paste(part, (0, 0), mask)
    return c


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "zappo_charging_scene.mp4"))
