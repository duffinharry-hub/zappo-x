/* Clove — the two looks (included from ui.c before the screens are drawn).
 *
 * Style (Settings > Look > Style):
 *   Classic  the theme's colours as they are
 *   Glass    the wallpaper, blurred and darkened, behind every screen; panels
 *            you can see through, a slim tab bar of icons, white-glass
 *            selection
 * Layout "Shelf" (SELECT on the console list or a game list): the art big in
 * the middle with its neighbours either side, on a background tinted from
 * the colours of whatever is selected. */

static int glass(void) { return g_set.style == STYLE_GLASS; }

/* the theme's colours, turned see-through for Glass (called after theme_load) */
static void style_apply(void) {
	if (!glass()) return;
	g_theme.surface = RGBA(10, 8, 18, 196);
	g_theme.surface_hi = RGBA(255, 255, 255, 40);
	g_theme.shadow = RGBA(0, 0, 0, 110);
	g_theme.text = 0xFFFFFFFF;
	g_theme.text_dim = RGBA(226, 222, 236, 255);
}

/* a box behind a group of things */
static void draw_panel(int x, int y, int w, int h, int r) {
	gfx_rrect(x, y, w, h, r, g_theme.surface);
	if (glass()) gfx_rrect_stroke(x, y, w, h, r, 1, RGBA(255, 255, 255, 30));
}

/* a box that must stay readable over anything (menus, notices) */
static uint32_t solid_surface(void) { return glass() ? RGBA(24, 20, 34, 238) : g_theme.surface; }

/* the selected row */
static void draw_sel(int x, int y, int w, int h, int r) {
	if (glass()) {
		gfx_rrect(x, y, w, h, r, RGBA(255, 255, 255, 58));
		gfx_rrect_stroke(x, y, w, h, r, 1, RGBA(255, 255, 255, 120));
		/* a little accent mark at the left edge */
		gfx_rrect(x + 4, y + h / 2 - h / 5, 3, h * 2 / 5, 1, g_theme.accent);
	} else {
		gfx_rrect(x, y, w, h, r, g_theme.accent);
	}
}
static uint32_t sel_fg(void) { return glass() ? g_theme.text : g_theme.accent_text; }
static uint32_t sel_dim(void) { return glass() ? col_alpha(g_theme.text, 200) : col_alpha(g_theme.accent_text, 190); }
/* the ring around a selected card */
static uint32_t sel_ring(void) { return glass() ? RGBA(255, 255, 255, 200) : g_theme.accent; }

/* --------------------------------------------------------- Glass wallpaper */

/* the lock screen picture if one is chosen, else the theme's wallpaper, else Clove's */
static void glass_wallpaper_path(char *out, size_t n) {
	lockpic_path(out, n);
	if (out[0]) return;
	snfmt(out, n, "%s/wallpaper.png", g_theme.dir);
	if (file_exists(out)) return;
	snfmt(out, n, "%s/wallpaper.png", g_paths.clove);
	if (file_exists(out)) return;
	if (g_theme.background[0]) sncpy(out, g_theme.background, n);
	else out[0] = 0;
}
static Image *glass_bg_make(void) {
	char p[700];
	glass_wallpaper_path(p, sizeof p);
	Image *src = p[0] ? img_load(p) : NULL;
	if (!src) return NULL;
	Image *full = img_cover(src, SCREEN_W, SCREEN_H);
	img_free(src);
	if (!full) return NULL;
	/* blur: down to a few pixels and back up, twice, so it's smooth */
	Image *a = img_scale(full, 64, 48);
	img_free(full);
	Image *b = a ? img_scale(a, 160, 120) : NULL;
	img_free(a);
	Image *c = b ? img_scale(b, SCREEN_W, SCREEN_H) : NULL;
	img_free(b);
	if (!c) return NULL;
	/* darkened, more towards the bottom, so text reads on any picture */
	for (int y = 0; y < SCREEN_H; y++) {
		float f = 0.34f - 0.12f * y / SCREEN_H;
		for (int x = 0; x < SCREEN_W; x++) {
			uint32_t v = c->px[y * SCREEN_W + x];
			int r = (int)(((v >> 16) & 255) * f + 8 * (1 - f)), g = (int)(((v >> 8) & 255) * f + 7 * (1 - f)), bl = (int)((v & 255) * f + 16 * (1 - f));
			c->px[y * SCREEN_W + x] = RGBA(r, g, bl, 255);
		}
	}
	return c;
}

/* ------------------------------------------------------------ Shelf colours */

typedef struct { char key[600]; uint32_t c; } TintEnt;
static TintEnt tints[32];
static int ntints;
static float tint_cur[3] = {-1, -1, -1};
static uint32_t tint_want;
static int tint_has;

static uint32_t hsv_col(float h, float s, float v) {
	h = fmodf(h < 0 ? h + 1 : h, 1.0f) * 6.0f;
	int i = (int)h;
	float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
	float r, g, b;
	switch (i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	return RGBA((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

/* the picture's main colour, made deep enough to sit behind white text */
static uint32_t tint_of_image(const Image *im) {
	double R = 0, G = 0, B = 0, W = 0;
	int step = MAX(1, (im->w * im->h) / 900);
	for (int i = 0; i < im->w * im->h; i += step) {
		uint32_t v = im->px[i];
		if ((v >> 24) < 128) continue;
		float r = ((v >> 16) & 255) / 255.0f, g = ((v >> 8) & 255) / 255.0f, b = (v & 255) / 255.0f;
		float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
		float sat = mx > 0 ? (mx - mn) / mx : 0;
		float w = sat * mx + 0.04f; /* colourful pixels count most */
		R += r * w; G += g * w; B += b * w; W += w;
	}
	if (W <= 0) return RGBA(40, 34, 56, 255);
	float r = (float)(R / W), g = (float)(G / W), b = (float)(B / W);
	float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn, h = 0;
	if (d > 0.0001f) {
		if (mx == r) h = fmodf((g - b) / d, 6.0f);
		else if (mx == g) h = (b - r) / d + 2;
		else h = (r - g) / d + 4;
		h /= 6.0f;
	}
	float s = mx > 0 ? d / mx : 0;
	return hsv_col(h, CLAMP(s * 1.1f, 0.12f, 0.72f), 0.40f);
}

static int tint_cached(const char *key, uint32_t *out) {
	for (int i = 0; i < ntints; i++) if (!strcmp(tints[i].key, key)) { *out = tints[i].c; return 1; }
	return 0;
}
static void tint_store(const char *key, uint32_t c) {
	int slot = ntints < ARRAY_LEN(tints) ? ntints++ : (int)(plat_ticks() % ARRAY_LEN(tints));
	sncpy(tints[slot].key, key, sizeof tints[slot].key);
	tints[slot].c = c;
}

/* a colour from a name, for things without a picture */
static uint32_t tint_of_name(const char *s) {
	uint32_t h = 2166136261u;
	for (; *s; s++) h = (h ^ (uint8_t)tolower((unsigned char)*s)) * 16777619u;
	return hsv_col((h % 360) / 360.0f, 0.5f, 0.40f);
}

/* the shelf's background follows this colour */
static void tint_target(uint32_t c) { tint_want = c; tint_has = 1; }
static void tint_step(float k) {
	if (!tint_has) return;
	float t[3] = {(float)((tint_want >> 16) & 255), (float)((tint_want >> 8) & 255), (float)(tint_want & 255)};
	for (int i = 0; i < 3; i++) {
		if (tint_cur[i] < 0 || !g_set.animations) { tint_cur[i] = t[i]; continue; }
		float d = t[i] - tint_cur[i];
		if (fabsf(d) < 0.6f) tint_cur[i] = t[i];
		else { tint_cur[i] += d * k * 0.45f; animating = 1; }
	}
}

static void draw_tint_background(void) {
	uint32_t top = tint_cur[0] < 0 ? tint_want : RGBA((int)tint_cur[0], (int)tint_cur[1], (int)tint_cur[2], 255);
	top = col_mix(top, RGBA(6, 5, 10, 255), 0.25f);
	uint32_t mid = col_mix(top, g_theme.bg2, 0.6f), bot = col_mix(top, RGBA(6, 5, 10, 255), 0.9f);
	gfx_vgradient(0, 0, SCREEN_W, SCREEN_H * 3 / 5, top, mid);
	gfx_vgradient(0, SCREEN_H * 3 / 5, SCREEN_W, SCREEN_H - SCREEN_H * 3 / 5, mid, bot);
}

/* a tile for something without a picture: its colours, its name */
typedef struct { char key[300]; Image *im; uint64_t used; } GenTile;
static GenTile gtiles[24];
static Image *gen_tile(const char *name, int w, int h) {
	char key[300];
	snfmt(key, sizeof key, "%dx%d|%s", w, h, name);
	int slot = 0;
	for (int i = 0; i < ARRAY_LEN(gtiles); i++) {
		if (gtiles[i].im && !strcmp(gtiles[i].key, key)) { gtiles[i].used = plat_ticks(); return gtiles[i].im; }
		if (gtiles[i].used < gtiles[slot].used) slot = i;
	}
	uint32_t hh = 2166136261u;
	for (const char *s = name; *s; s++) hh = (hh ^ (uint8_t)tolower((unsigned char)*s)) * 16777619u;
	float hue = (hh % 360) / 360.0f;
	uint32_t c1 = hsv_col(hue, 0.42f, 0.86f), c2 = hsv_col(hue + 0.08f, 0.62f, 0.46f);
	Image *im = img_new(w, h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			float t = CLAMP((x * 0.3f + y * 0.7f) / (float)MAX(w, h), 0, 1);
			uint32_t c = col_mix(c1, c2, t);
			/* fine diagonal stripes */
			if (((x + y) / 7) % 2 == 0) c = col_mix(c, 0xFFFFFFFF, 0.05f);
			im->px[y * w + x] = c | 0xFF000000u;
		}
	img_free(gtiles[slot].im);
	sncpy(gtiles[slot].key, key, sizeof gtiles[slot].key);
	gtiles[slot].im = im;
	gtiles[slot].used = plat_ticks();
	return im;
}

/* ------------------------------------------------------------ Glass tab bar */

static void icon_ring(int cx, int cy, int r, int t, uint32_t c) { gfx_rrect_stroke(cx - r, cy - r, r * 2, r * 2, r, t, c); }

/* small line icons for the tabs, about s pixels across */
static void draw_tab_icon(int tabi, int cx, int cy, int s, uint32_t c) {
	int h = s / 2;
	switch (tabi) {
	case TAB_RECENT: /* a clock */
		icon_ring(cx, cy, h, 2, c);
		gfx_rect(cx - 1, cy - h + 4, 2, h - 3, c);
		gfx_rect(cx - 1, cy - 1, h - 3, 2, c);
		break;
	case TAB_FAVORITES: draw_star(cx, cy, h + 1, c); break;
	case TAB_CONSOLES: { /* four squares */
		int q = (s - 3) / 2;
		for (int i = 0; i < 4; i++) gfx_rrect(cx - h + (i % 2) * (q + 3), cy - h + (i / 2) * (q + 3), q, q, 2, c);
		break;
	}
	case TAB_MUSIC: draw_note(cx - h / 2 - 1, cy - h, s, c); break;
	case TAB_VIDEOS: { /* a screen with a play mark */
		gfx_rrect_stroke(cx - h, cy - h + 2, s, s - 4, 3, 2, c);
		int ph = s / 2;
		for (int i = 0; i < ph; i++) gfx_rect(cx - 2, cy - ph / 2 + i, (i < ph / 2 ? i : ph - 1 - i) / 2 + 1, 1, c);
		break;
	}
	case TAB_RETROARCH: /* stacked layers */
		for (int i = 0; i < 3; i++) gfx_rrect(cx - h + i, cy - h + 1 + i * (s / 3), s - 2 * i, 3, 1, col_alpha(c, (int)((c >> 24) * (100 - i * 25) / 100)));
		break;
	case TAB_SETTINGS: /* sliders */
		for (int i = 0; i < 3; i++) {
			int y = cy - h + 2 + i * (s / 3);
			gfx_rect(cx - h, y, s, 2, c);
			int kx = cx - h + (i == 1 ? s - 6 : i == 0 ? 3 : s / 2 - 2);
			gfx_circle(kx + 2, y + 1, 3, c);
		}
		break;
	}
}

/* the Glass tab bar: one pill of icons, the open tab with its name */
static void draw_glass_tabs(int limit) {
	int tabs[TAB_COUNT];
	int nt = tab_list(tabs);
	const int ih = 32, iw = 34, gap = 2;
	int total = 6;
	for (int i = 0; i < nt; i++) total += (tabs[i] == tab ? iw + text_width(F_SMALL, g_theme.label[tabs[i]]) + 14 : iw) + gap;
	/* centred, unless it would run into the clock */
	int x = (SCREEN_W - total) / 2, y = TOP_H / 2 - (ih + 6) / 2;
	if (x + total > limit - 8) x = MAX(4, limit - 8 - total);
	gfx_rrect(x, y, total, ih + 6, (ih + 6) / 2, RGBA(10, 8, 18, 150));
	gfx_rrect_stroke(x, y, total, ih + 6, (ih + 6) / 2, 1, RGBA(255, 255, 255, 50));
	int cx = x + 3;
	for (int i = 0; i < nt; i++) {
		int on = tabs[i] == tab;
		const char *lab = g_theme.label[tabs[i]];
		int w = on ? iw + text_width(F_SMALL, lab) + 14 : iw;
		if (on) gfx_rrect(cx, y + 3, w, ih, ih / 2, RGBA(255, 255, 255, 64));
		uint32_t c = on ? g_theme.text : col_alpha(g_theme.text, 175);
		draw_tab_icon(tabs[i], cx + iw / 2, y + 3 + ih / 2, 14, c);
		if (on) text_draw(F_SMALL, cx + iw - 4, y + 3 + ih / 2 - font_height(F_SMALL) / 2, lab, g_theme.text);
		cx += w + gap;
	}
}
