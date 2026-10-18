"""The twin's stick lights: reads what the software sends to the RG40XX H's
light controller (/dev/ttyS5 on the handheld, led.sock on the twin), keeps
the current state, and draws the two 8-LED rings - alone or in a picture of
the whole handheld with the twin's screen in it.

  ledview.py record             follow led.sock; keeps led-state.json and led.log
  ledview.py frame OUT.png [T]  the handheld: twin screen + both rings at time T s
  ledview.py rings OUT.png [T]  just the two rings

The controller protocol (from Knulli's knulli-rgb-led, read not copied):
  modes 1-4  [mode, brightness, 8 x RGB right ring, 8 x RGB left ring, sum & 0xFF]
             1 = steady, 2/3/4 = breathing fast/medium/slow
  modes 5-6  [mode, brightness, 1, 1, speed, sum & 0xFF]
             5 = whole ring cycles through colours, 6 = rainbow swirl
  brightness and speed are 0-255.  How fast the controller breathes and
  cycles, and where LED 0 sits on the ring, are guesses until the real
  handheld is here (on the arrival checklist).
"""
import colorsys
import json
import math
import os
import socket
import sys
import time

RUN = os.environ.get("TWIN_RUN", "/home/claude/twin/run")
STATE = os.path.join(RUN, "led-state.json")
BREATHE = {2: 1.0, 3: 2.0, 4: 4.0}         # seconds per breath (guess)


def parse(buf):
    """split a byte stream into frames; returns (frames, leftover)"""
    out = []
    while buf:
        m = buf[0]
        n = 51 if 1 <= m <= 4 else 6 if m in (5, 6) else 0
        if not n:
            buf = buf[1:]                  # not a frame start: resync
            continue
        if len(buf) < n:
            break
        f = buf[:n]
        if sum(f[:-1]) & 0xFF == f[-1]:
            out.append(f)
            buf = buf[n:]
        else:
            buf = buf[1:]
    return out, buf


def decode(f):
    m, b = f[0], f[1]
    s = {"mode": m, "brightness": b, "time": time.time()}
    if m <= 4:
        px = [tuple(f[2 + 3 * i: 5 + 3 * i]) for i in range(16)]
        s["right"], s["left"] = px[:8], px[8:]
    else:
        s["speed"] = f[4]
    return s


def leds(state, t=None):
    """colours of (left ring, right ring), 8 LEDs each, as seen at time t"""
    if not state:
        return [(0, 0, 0)] * 8, [(0, 0, 0)] * 8
    t = time.time() if t is None else t
    m, b = state["mode"], state["brightness"] / 255.0
    if m <= 4:
        k = b
        if m in BREATHE:
            k *= 0.5 - 0.5 * math.cos(2 * math.pi * (t - state["time"]) / BREATHE[m])
        sc = lambda c: tuple(int(v * k) for v in c)
        return [sc(c) for c in state["left"]], [sc(c) for c in state["right"]]
    period = 12.0 / (1 + state.get("speed", 0) / 16.0)    # guess
    ph = (t - state["time"]) / period
    ring = []
    for i in range(8):
        h = (ph + (i / 8.0 if m == 6 else 0)) % 1.0
        ring.append(tuple(int(255 * v * b) for v in colorsys.hsv_to_rgb(h, 1, 1)))
    return ring, list(reversed(ring)) if m == 6 else ring


def load():
    try:
        return json.load(open(STATE))
    except (OSError, ValueError):
        return None


def record():
    log = open(os.path.join(RUN, "led.log"), "a")
    while True:
        try:
            s = socket.socket(socket.AF_UNIX)
            s.connect(os.path.join(RUN, "led.sock"))
        except OSError:
            time.sleep(0.5)
            continue
        buf = b""
        while True:
            d = s.recv(4096)
            if not d:
                break
            frames, buf = parse(buf + d)
            for f in frames:
                st = decode(f)
                tmp = STATE + ".tmp"
                json.dump(st, open(tmp, "w"))
                os.replace(tmp, STATE)
                log.write("%.3f %s\n" % (st["time"], f.hex()))
                log.flush()
        time.sleep(0.5)


def draw_ring(img, cx, cy, ring, r=46):
    from PIL import Image, ImageDraw, ImageFilter
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    g = ImageDraw.Draw(glow)
    for i, c in enumerate(ring):                      # LED 0 at the top, clockwise (guess)
        a = -math.pi / 2 + i * 2 * math.pi / 8
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        g.ellipse((x - 16, y - 16, x + 16, y + 16), fill=c + (150,))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(9)))
    d = ImageDraw.Draw(img)
    d.ellipse((cx - r - 9, cy - r - 9, cx + r + 9, cy + r + 9), outline=(70, 72, 80, 255), width=3)
    for i, c in enumerate(ring):
        a = -math.pi / 2 + i * 2 * math.pi / 8
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        lit = tuple(min(255, int(v * 0.6 + 100 * (max(c) > 8))) for v in c)
        d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=(lit if max(c) > 8 else (40, 40, 46)) + (255,))
    d.ellipse((cx - 33, cy - 33, cx + 33, cy + 33), fill=(28, 28, 32, 255), outline=(55, 56, 62, 255), width=2)
    d.ellipse((cx - 24, cy - 24, cx + 24, cy + 24), fill=(36, 36, 42, 255))


def rings_image(t=None):
    from PIL import Image
    left, right = leds(load(), t)
    img = Image.new("RGBA", (360, 180), (18, 18, 22, 255))
    draw_ring(img, 90, 90, left)
    draw_ring(img, 270, 90, right)
    return img


def frame_image(screen=None, t=None):
    """a plain stand-in for the handheld (not Anbernic's artwork): body, the
    640x480 screen, d-pad, ABXY, and both sticks with their light rings"""
    from PIL import Image, ImageDraw
    W, H = 1120, 600
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, W - 1, H - 1), 70, fill=(30, 31, 36, 255), outline=(52, 54, 60, 255), width=3)
    d.rounded_rectangle((226, 40, 894, 536), 14, fill=(8, 8, 10, 255))
    if screen is not None:
        img.paste(screen.convert("RGB").resize((640, 480)), (240, 48))
    # d-pad
    cx, cy = 118, 150
    d.rounded_rectangle((cx - 15, cy - 50, cx + 15, cy + 50), 6, fill=(20, 20, 24, 255))
    d.rounded_rectangle((cx - 50, cy - 15, cx + 50, cy + 15), 6, fill=(20, 20, 24, 255))
    # face buttons
    for lab, (x, y) in {"X": (1002, 105), "Y": (957, 150), "A": (1047, 150), "B": (1002, 195)}.items():
        d.ellipse((x - 21, y - 21, x + 21, y + 21), fill=(20, 20, 24, 255))
        d.text((x - 4, y - 7), lab, fill=(150, 150, 160, 255))
    for x, lab in ((98, "SELECT"), (1022, "START"), (560, "MENU")):
        y = 560 if lab == "MENU" else 255
        d.rounded_rectangle((x - 22, y - 7, x + 22, y + 7), 7, fill=(20, 20, 24, 255))
    left, right = leds(load(), t)
    draw_ring(img, 118, 400, left)
    draw_ring(img, 1002, 400, right)
    return img


def main(a):
    from PIL import Image
    if a[0] == "record":
        record()
    elif a[0] in ("frame", "rings"):
        t = float(a[2]) if len(a) > 2 else None
        if a[0] == "rings":
            rings_image(t).save(a[1])
        else:
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            import twinctl
            tmp = os.path.join(RUN, ".frame-screen.png")
            twinctl.shot(tmp)
            frame_image(Image.open(tmp), t).save(a[1])
        print(a[1])


if __name__ == "__main__":
    main(sys.argv[1:])
