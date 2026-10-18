"""Fire Zappo: the same drawing recoloured (navy fur -> warm black-brown,
gold markings -> glowing orange lava, cream fur stays cream), plus the effects drawn
around the two Zappos on the charging screen: electricity and embers."""
import numpy as np, cv2
def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)
def band(h, lo, hi, soft=4):
    return smooth(h, lo - soft, lo) * (1 - smooth(h, hi, hi + soft))
def gradmap(L, stops):
    xs = np.array([s[0] for s in stops]); cs = np.array([s[1] for s in stops], np.float32)
    out = np.empty(L.shape + (3,), np.float32)
    for c in range(3): out[..., c] = np.interp(L, xs, cs[:, c])
    return out
FIRE = [(0.0, (60, 12, 4)), (0.35, (150, 42, 10)), (0.55, (214, 82, 16)), (0.72, (245, 124, 22)), (0.85, (252, 164, 30)), (0.94, (255, 206, 72)), (1.0, (255, 238, 160))]
def recolor(rgba, pal):
    """the drawing in another element's colours. pal: 'mark' (gold markings, ears,
    eyes), 'fur' (navy fur), 'chest' (cream fur; None keeps it), 'tint' (the glow
    where they meet) - each a list of (lightness 0..1, rgb) stops"""
    rgb = rgba[..., :3].astype(np.float32); a = rgba[..., 3]
    hsv = cv2.cvtColor(np.ascontiguousarray(rgba[..., :3]), cv2.COLOR_RGB2HSV).astype(np.float32)
    H, Sa, V = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    L = cv2.cvtColor(np.ascontiguousarray(rgba[..., :3]), cv2.COLOR_RGB2LAB)[..., 0].astype(np.float32) / 255
    mouth = (band(H, 0, 8, 2) + band(H, 168, 180, 2)) * smooth(Sa, 70, 110) * smooth(V, 110, 150)
    wy = band(H, 9, 40, 5) * smooth(Sa, 70, 100) * (1 - mouth)                       # gold markings, ears, eyes, their glow
    wn = band(H, 96, 152, 8) * smooth(Sa, 14, 34)                                   # navy fur
    wc = (1 - smooth(Sa, 70, 100)) * smooth(V, 120, 170) * (1 - wy) * (1 - wn)       # cream fur
    # pale highlights inside the markings: small pale patches mostly ringed by gold join the marking
    cm = (wc > 0.5).astype(np.uint8); gm = (wy > 0.5).astype(np.uint8)
    n, lab, st, _ = cv2.connectedComponentsWithStats(cm, connectivity=8)
    join = np.zeros(cm.shape, np.float32)
    for i in range(1, n):
        if st[i, cv2.CC_STAT_AREA] > 9000: continue
        x, y, w, h = st[i, 0], st[i, 1], st[i, 2], st[i, 3]
        x0, y0, x1, y1 = max(0, x - 8), max(0, y - 8), min(cm.shape[1], x + w + 8), min(cm.shape[0], y + h + 8)
        comp = (lab[y0:y1, x0:x1] == i).astype(np.uint8)
        ring = cv2.dilate(comp, np.ones((9, 9), np.uint8)) - comp
        if ring.sum() and (gm[y0:y1, x0:x1] * ring).sum() / ring.sum() > 0.45:
            join[y0:y1, x0:x1] = np.maximum(join[y0:y1, x0:x1], comp)
    join = cv2.GaussianBlur(join, (0, 0), 1.5)
    wy = np.maximum(wy, join * (1 - mouth)); wc = wc * (1 - join)
    wo = np.clip(1 - wy - wn - wc - mouth, 0, 1) * smooth(V, 40, 90)                # in-between tints (glow over the fur)
    yel = gradmap(L, pal["mark"])
    nav = gradmap(L, pal["fur"])
    cre = rgb if pal.get("chest") is None else gradmap(L, pal["chest"])
    oth = gradmap(L, pal["tint"])
    keep = np.clip(1 - (wy + wn + wc + wo), 0, 1)
    out = rgb * keep[..., None] + yel * wy[..., None] + nav * wn[..., None] + cre * wc[..., None] + oth * wo[..., None]
    return np.dstack([np.clip(out, 0, 255).astype(np.uint8), a]), wy


# colours taken from the meadow picture's elemental Zappos
PALETTES = {
    "fire": {"mark": FIRE,
             "fur": [(0.0, (10, 6, 5)), (0.25, (24, 14, 11)), (0.45, (43, 25, 19)), (0.7, (74, 46, 34)), (1.0, (122, 82, 60))],
             "chest": None,
             "tint": [(0.0, (12, 6, 4)), (0.4, (80, 30, 12)), (0.7, (170, 72, 22)), (1.0, (240, 150, 60))]},
    "leaf": {"mark": [(0.0, (30, 50, 6)), (0.35, (70, 112, 16)), (0.55, (112, 162, 28)), (0.72, (150, 198, 46)), (0.86, (188, 226, 76)), (0.94, (214, 242, 124)), (1.0, (238, 252, 196))],
             "fur": [(0.0, (6, 12, 5)), (0.25, (16, 30, 12)), (0.45, (32, 54, 24)), (0.7, (60, 90, 42)), (1.0, (108, 140, 76))],
             "chest": None,
             "tint": [(0.0, (8, 14, 6)), (0.4, (40, 70, 20)), (0.7, (100, 150, 40)), (1.0, (190, 230, 110))]},
    "crystal": {"mark": [(0.0, (34, 8, 64)), (0.35, (84, 28, 146)), (0.55, (132, 58, 206)), (0.72, (168, 98, 238)), (0.86, (200, 148, 255)), (0.94, (224, 194, 255)), (1.0, (244, 230, 255))],
                "fur": [(0.0, (9, 5, 15)), (0.25, (24, 14, 38)), (0.45, (44, 28, 66)), (0.7, (78, 54, 110)), (1.0, (132, 104, 168))],
                "chest": [(0.0, (30, 22, 40)), (0.5, (118, 98, 140)), (0.8, (204, 186, 224)), (1.0, (242, 232, 252))],
                "tint": [(0.0, (10, 6, 18)), (0.4, (60, 30, 100)), (0.7, (140, 80, 200)), (1.0, (220, 180, 255))]},
    "ice": {"mark": [(0.0, (50, 100, 160)), (0.35, (100, 160, 225)), (0.55, (142, 200, 248)), (0.72, (180, 224, 255)), (0.86, (212, 238, 255)), (1.0, (250, 253, 255))],
            "fur": [(0.0, (44, 64, 98)), (0.25, (88, 120, 166)), (0.45, (132, 166, 212)), (0.7, (176, 206, 238)), (1.0, (222, 236, 252))],
            "chest": [(0.0, (58, 68, 88)), (0.5, (168, 184, 204)), (0.8, (226, 236, 246)), (1.0, (252, 253, 255))],
            "tint": [(0.0, (20, 34, 60)), (0.4, (70, 120, 180)), (0.7, (150, 200, 245)), (1.0, (235, 248, 255))]},
    "water": {"mark": [(0.0, (0, 44, 84)), (0.35, (0, 92, 164)), (0.55, (16, 142, 218)), (0.72, (54, 184, 244)), (0.86, (116, 216, 255)), (1.0, (214, 248, 255))],
              "fur": [(0.0, (3, 8, 26)), (0.25, (10, 24, 62)), (0.45, (20, 46, 110)), (0.7, (44, 82, 160)), (1.0, (92, 142, 214))],
              "chest": [(0.0, (48, 54, 64)), (0.5, (166, 178, 190)), (0.8, (226, 234, 242)), (1.0, (250, 252, 255))],
              "tint": [(0.0, (4, 12, 30)), (0.4, (10, 70, 140)), (0.7, (60, 170, 240)), (1.0, (200, 240, 255))]},
}


def ember(rgba):
    return recolor(rgba, PALETTES["fire"])


# ---------------------------------------------------------------- effects
def _jagged(p0, p1, rnd, rough=0.35, depth=4):
    pts = [np.array(p0, float), np.array(p1, float)]
    for _ in range(depth):
        out = [pts[0]]
        for a, b in zip(pts[:-1], pts[1:]):
            d = b - a
            n = np.array([-d[1], d[0]])
            m = (a + b) / 2 + n * rnd.uniform(-rough, rough)
            out += [m, b]
        pts = out
    return [tuple(p) for p in pts]


def edge_points(alpha, rnd, n, avoid=None):
    """n points on the drawing's outline, each with its outward direction"""
    cs, _ = cv2.findContours((alpha > 128).astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    c = max(cs, key=cv2.contourArea).reshape(-1, 2)
    dist = cv2.distanceTransform((alpha > 128).astype(np.uint8), cv2.DIST_L2, 5)
    gy, gx = np.gradient(cv2.GaussianBlur(dist, (0, 0), 3))
    out = []
    for _ in range(n * 6):
        if len(out) >= n: break
        x, y = c[rnd.randint(0, len(c) - 1)]
        if avoid and avoid(x, y): continue
        d = -np.array([gx[y, x], gy[y, x]])
        if np.linalg.norm(d) < 1e-6: continue
        d /= np.linalg.norm(d)
        if any((x - ox) ** 2 + (y - oy) ** 2 < (alpha.shape[0] * 0.12) ** 2 for ox, oy, _ in out): continue
        out.append((x, y, d))
    return out


def draw_sparks(layer, alpha, ox, oy, rnd, n, glow, core, length, width):
    """little jagged arcs leaping off the outline (layer: RGBA PIL image, drawn additively-ish)"""
    from PIL import Image, ImageDraw, ImageFilter
    W, H = layer.size
    g = Image.new("RGBA", layer.size, (0, 0, 0, 0)); c = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    gd, cd = ImageDraw.Draw(g), ImageDraw.Draw(c)
    for x, y, d in edge_points(alpha, rnd, n):
        L = length * rnd.uniform(0.7, 1.3)
        side = np.array([-d[1], d[0]]) * rnd.uniform(-0.6, 0.6)
        p0 = np.array([x + ox, y + oy]) - d * width * 2
        p1 = p0 + (d + side) * L
        pts = _jagged(p0, p1, rnd)
        gd.line(pts, fill=glow + (150,), width=max(2, int(width * 3.2)), joint="curve")
        cd.line(pts, fill=core + (255,), width=max(1, int(round(width))), joint="curve")
        if rnd.random() < 0.6:  # a fork
            k = rnd.randint(len(pts) // 3, len(pts) - 2)
            q0 = np.array(pts[k]); q1 = q0 + (d * 0.5 + np.array([-d[1], d[0]]) * rnd.choice([-0.7, 0.7])) * L * 0.45
            f = _jagged(q0, q1, rnd, depth=3)
            gd.line(f, fill=glow + (120,), width=max(2, int(width * 2.4)), joint="curve")
            cd.line(f, fill=core + (230,), width=max(1, int(round(width * 0.8))), joint="curve")
    g = g.filter(ImageFilter.GaussianBlur(width * 1.6))
    layer.alpha_composite(g); layer.alpha_composite(g); layer.alpha_composite(c)


def draw_embers(layer, box, rnd, t, n, scale):
    """little flames and sparks drifting up from (and around) the drawing; t moves them up"""
    from PIL import Image, ImageDraw, ImageFilter
    x0, y0, x1, y1 = box
    e = Image.new("RGBA", layer.size, (0, 0, 0, 0)); ed = ImageDraw.Draw(e)
    for i in range(n):
        bx = rnd.uniform(x0 + (x1 - x0) * 0.02, x1 - (x1 - x0) * 0.02)
        by = rnd.uniform(y0 + (y1 - y0) * 0.2, y1 - (y1 - y0) * 0.05)
        sp = rnd.uniform(0.6, 1.4)
        life = (rnd.random() + t * 0.18 * sp) % 1.0          # 0 just lit .. 1 gone
        x = bx + np.sin(life * 6.0 + i) * 5 * scale
        y = by - life * (y1 - y0) * 0.45
        a = int(255 * (1 - life) ** 0.6)
        if rnd.random() < 0.6:
            # a flame: round bottom, pointed flickering tip, yellow heart
            r = rnd.uniform(2.2, 4.0) * scale * (1.0 - 0.4 * life)
            lean = rnd.uniform(-0.5, 0.5) * r
            def flame(rr, col):
                pts = []
                for k in range(16):
                    th = np.pi * k / 15
                    pts.append((x + np.cos(th + np.pi) * rr, y + np.sin(th) * rr * 0.9))
                pts += [(x + rr * 0.55 + lean * 0.4, y - rr * 0.9), (x + lean, y - rr * 2.6), (x - rr * 0.55 + lean * 0.4, y - rr * 0.9)]
                ed.polygon(pts, fill=col)
            flame(r, (250, 110, 20, a))
            flame(r * 0.55, (255, 226, 120, a))
        else:
            r = rnd.uniform(0.8, 1.6) * scale * (1.0 - 0.5 * life)
            col = (255, 220, 120) if rnd.random() < 0.5 else (255, 140, 40)
            ed.ellipse((x - r, y - r, x + r, y + r), fill=col + (a,))
    glow = e.filter(ImageFilter.GaussianBlur(2.4 * scale))
    layer.alpha_composite(glow); layer.alpha_composite(glow); layer.alpha_composite(e)


def draw_flicks(layer, alpha, ox, oy, rnd, n, scale):
    """small flames licking up off the drawing's outline (back, tail, feet)"""
    from PIL import Image, ImageDraw, ImageFilter
    e = Image.new("RGBA", layer.size, (0, 0, 0, 0)); ed = ImageDraw.Draw(e)
    pts_all = edge_points(alpha, rnd, n, avoid=lambda x, y: y < alpha.shape[0] * 0.3)
    for x, y, d in pts_all:
        x, y = x + ox, y + oy
        h = rnd.uniform(9, 16) * scale
        w = h * 0.38
        up = np.array([d[0] * 0.5, -1.0]); up /= np.linalg.norm(up)
        side = np.array([-up[1], up[0]])
        tip = np.array([x, y]) + up * h
        for frac, col in ((1.0, (236, 90, 16, 220)), (0.6, (255, 200, 70, 240))):
            base = np.array([x, y]) + up * h * (1 - frac) * 0.3
            ww = w * frac
            poly = [tuple(base - side * ww), tuple(base + up * h * frac * 0.45 + side * ww * 0.8), tuple(base + up * h * frac), tuple(base + up * h * frac * 0.45 - side * ww * 0.8)]
            ed.polygon([poly[0], tuple(base + side * ww), poly[1], poly[2], poly[3]], fill=col)
    glow = e.filter(ImageFilter.GaussianBlur(2.0 * scale))
    layer.alpha_composite(glow); layer.alpha_composite(e)


def draw_bolts(layer, box, rnd, n, glow, core, size):
    """tiny free-floating lightning bolts round the drawing, like the live wallpaper's sparks"""
    from PIL import Image, ImageDraw, ImageFilter
    x0, y0, x1, y1 = box
    g = Image.new("RGBA", layer.size, (0, 0, 0, 0)); c = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    gd, cd = ImageDraw.Draw(g), ImageDraw.Draw(c)
    for _ in range(n):
        # on a ring round the box, not over the face
        ang = rnd.uniform(0, 2 * np.pi)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        rx, ry = (x1 - x0) / 2 * rnd.uniform(0.92, 1.12), (y1 - y0) / 2 * rnd.uniform(0.92, 1.12)
        x, y = cx + np.cos(ang) * rx, cy + np.sin(ang) * ry
        s = size * rnd.uniform(0.7, 1.25)
        rot = rnd.uniform(-0.6, 0.6)
        zig = [(0.15, -1.0), (-0.35, 0.05), (0.05, 0.05), (-0.2, 1.0), (0.4, -0.15), (0.0, -0.15)]
        pts = [(x + (px * np.cos(rot) - py * np.sin(rot)) * s, y + (px * np.sin(rot) + py * np.cos(rot)) * s) for px, py in zig]
        gd.polygon(pts, fill=glow + (170,))
        cd.polygon(pts, fill=core + (255,))
    g = g.filter(ImageFilter.GaussianBlur(size * 0.35))
    layer.alpha_composite(g); layer.alpha_composite(c)


# ---------------------------------------------------------------- the other elements' particles
def _ring_points(box, rnd, n, spread=(0.95, 1.25)):
    x0, y0, x1, y1 = box
    cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    for _ in range(n):
        a = rnd.uniform(0, 2 * np.pi)
        f = rnd.uniform(*spread)
        yield cx + np.cos(a) * rx * f, cy + np.sin(a) * ry * f


def _glowed(layer, draw_fn, blur, twice=False):
    from PIL import Image, ImageDraw, ImageFilter
    e = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(e))
    g = e.filter(ImageFilter.GaussianBlur(blur))
    layer.alpha_composite(g)
    if twice: layer.alpha_composite(g)
    layer.alpha_composite(e)


def draw_leaves(layer, box, rnd, n, size):
    def fn(d):
        for x, y in _ring_points(box, rnd, n):
            s = size * rnd.uniform(0.7, 1.3); r = rnd.uniform(0, np.pi)
            col = rnd.choice([(118, 186, 46), (150, 210, 70), (92, 160, 40), (186, 228, 96)])
            pts = []
            for k in range(20):
                t = 2 * np.pi * k / 20
                px, py = np.cos(t) * s, np.sin(t) * s * 0.42 * (1 - 0.35 * np.cos(t))
                pts.append((x + px * np.cos(r) - py * np.sin(r), y + px * np.sin(r) + py * np.cos(r)))
            d.polygon(pts, fill=col + (235,))
            d.line([(x - np.cos(r) * s * 0.9, y - np.sin(r) * s * 0.9), (x + np.cos(r) * s * 0.9, y + np.sin(r) * s * 0.9)],
                   fill=(60, 110, 24, 220), width=max(1, int(s * 0.12)))
    _glowed(layer, fn, size * 0.25)


def draw_shards(layer, box, rnd, n, size):
    def fn(d):
        for x, y in _ring_points(box, rnd, n):
            s = size * rnd.uniform(0.7, 1.3); r = rnd.uniform(-0.6, 0.6)
            pts = [(0, -1.4), (0.38, 0), (0, 0.9), (-0.38, 0)]
            P = [(x + (px * np.cos(r) - py * np.sin(r)) * s, y + (px * np.sin(r) + py * np.cos(r)) * s) for px, py in pts]
            d.polygon(P, fill=(168, 104, 240, 230))
            d.polygon([P[0], P[1], (x, y)], fill=(224, 196, 255, 240))
    _glowed(layer, fn, size * 0.45, twice=True)


def draw_sparkles(layer, box, rnd, n, size, col):
    def fn(d):
        for x, y in _ring_points(box, rnd, n, (0.85, 1.3)):
            s = size * rnd.uniform(0.5, 1.1)
            d.polygon([(x, y - s), (x + s * 0.22, y - s * 0.22), (x + s, y), (x + s * 0.22, y + s * 0.22),
                       (x, y + s), (x - s * 0.22, y + s * 0.22), (x - s, y), (x - s * 0.22, y - s * 0.22)], fill=col + (240,))
    _glowed(layer, fn, size * 0.5, twice=True)


def draw_snow(layer, box, rnd, n, size):
    def fn(d):
        for x, y in _ring_points(box, rnd, n):
            s = size * rnd.uniform(0.6, 1.2); r = rnd.uniform(0, np.pi / 3)
            w = max(1, int(round(s * 0.14)))
            for k in range(6):
                a = r + k * np.pi / 3
                ex, ey = x + np.cos(a) * s, y + np.sin(a) * s
                d.line([(x, y), (ex, ey)], fill=(236, 248, 255, 245), width=w)
                for f in (0.55,):
                    bx, by = x + np.cos(a) * s * f, y + np.sin(a) * s * f
                    for sgn in (-1, 1):
                        b = a + sgn * 0.7
                        d.line([(bx, by), (bx + np.cos(b) * s * 0.32, by + np.sin(b) * s * 0.32)], fill=(236, 248, 255, 235), width=w)
    _glowed(layer, fn, size * 0.3, twice=True)


def draw_drops(layer, box, rnd, n, size):
    def fn(d):
        for x, y in _ring_points(box, rnd, n):
            s = size * rnd.uniform(0.6, 1.2)
            if rnd.random() < 0.65:
                pts = []
                for k in range(16):
                    th = np.pi * k / 15
                    pts.append((x + np.cos(th + np.pi) * s, y + np.sin(th) * s))
                pts += [(x + s * 0.6, y - s * 0.7), (x, y - s * 2.2), (x - s * 0.6, y - s * 0.7)]
                d.polygon(pts, fill=(64, 178, 244, 225))
                d.ellipse((x - s * 0.55, y - s * 0.4, x - s * 0.15, y), fill=(220, 246, 255, 230))
            else:
                d.ellipse((x - s, y - s, x + s, y + s), outline=(170, 226, 255, 230), width=max(1, int(s * 0.25)))
                d.ellipse((x - s * 0.5, y - s * 0.6, x - s * 0.15, y - s * 0.25), fill=(235, 250, 255, 230))
    _glowed(layer, fn, size * 0.3)
