#include "font.h"
#include "gfx.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x)
#include "../third_party/stb_truetype.h"

#define MAX_FACES 16

typedef struct {
	char path[512];
	unsigned char *data;
	stbtt_fontinfo info;
	int loaded, failed, lazy;
} Face;

typedef struct {
	uint32_t cp;
	int used;
	int8_t face;
	int16_t w, h, xoff, yoff;
	float adv;
	uint8_t *bmp;
} Glyph;

struct Font {
	int px;
	int ascent, height;
	float scale[MAX_FACES];
	int base_shift[MAX_FACES]; /* align fallback baselines with the primary */
	Glyph *tab;
	int cap, count;
	int primary; /* the face the glyphs come from first (0 = the theme's font) */
	Font *next;
};

static Face faces[MAX_FACES];
static int nfaces;
static Font *fonts;

static int face_load(Face *f) {
	if (f->loaded) return 1;
	if (f->failed) return 0;
	f->data = (unsigned char *)file_read_all(f->path, NULL);
	if (!f->data || !stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) {
		LOG("font: cannot load %s", f->path);
		free(f->data);
		f->data = NULL;
		f->failed = 1;
		return 0;
	}
	f->loaded = 1;
	return 1;
}

int font_add_face(const char *path, int lazy) {
	if (nfaces >= MAX_FACES || !file_exists(path)) return -1;
	Face *f = &faces[nfaces];
	memset(f, 0, sizeof *f);
	sncpy(f->path, path, sizeof f->path);
	f->lazy = lazy;
	if (!lazy && !face_load(f)) return -1;
	return nfaces++;
}

static int font_weight;

/* drop every cached glyph (the faces stay loaded) */
static void font_flush(void) {
	for (Font *ft = fonts; ft; ft = ft->next) {
		for (int i = 0; i < ft->cap; i++) free(ft->tab[i].bmp);
		memset(ft->tab, 0, sizeof(Glyph) * (size_t)ft->cap);
		ft->count = 0;
	}
}

void font_set_weight(int weight) {
	weight = weight < 0 ? 0 : weight > 2 ? 2 : weight;
	if (weight == font_weight) return;
	font_weight = weight;
	font_flush();
}

/* smear the glyph sideways by s pixels (0 < s <= 2): out = max(in[x], k * in[x - 1]) per step */
static unsigned char *embolden(unsigned char *bmp, int *w, int h, float s) {
	int steps = s > 1.0f ? 2 : 1;
	int nw = *w + steps;
	unsigned char *o = malloc((size_t)nw * (size_t)(h > 0 ? h : 1));
	if (!o) return bmp;
	for (int y = 0; y < h; y++) {
		unsigned char *src = bmp + (size_t)y * (size_t)*w, *dst = o + (size_t)y * (size_t)nw;
		for (int x = 0; x < nw; x++) dst[x] = x < *w ? src[x] : 0;
		for (int st = 0; st < steps; st++) {
			float k = st == 0 ? (s > 1.0f ? 1.0f : s) : s - 1.0f;
			for (int x = nw - 1; x > 0; x--) {
				int v = (int)(dst[x - 1] * k + 0.5f);
				if (v > dst[x]) dst[x] = (unsigned char)v;
			}
		}
	}
	free(bmp);
	*w = nw;
	return o;
}

void font_clear_faces(void) {
	for (Font *ft = fonts; ft;) {
		Font *n = ft->next;
		for (int i = 0; i < ft->cap; i++) free(ft->tab[i].bmp);
		free(ft->tab);
		free(ft);
		ft = n;
	}
	fonts = NULL;
	for (int i = 0; i < nfaces; i++) free(faces[i].data);
	nfaces = 0;
}

static void font_metrics(Font *ft, int fi) {
	Face *f = &faces[fi];
	ft->scale[fi] = stbtt_ScaleForPixelHeight(&f->info, (float)ft->px);
	int asc, desc, gap;
	stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
	int a = (int)lroundf(asc * ft->scale[fi]);
	if (fi == ft->primary) {
		ft->ascent = a;
		ft->height = (int)lroundf((asc - desc + gap) * ft->scale[fi]);
		ft->base_shift[fi] = 0;
	} else {
		ft->base_shift[fi] = ft->ascent - a;
	}
}

static Font *font_make(int px, int primary) {
	for (Font *f = fonts; f; f = f->next)
		if (f->px == px && f->primary == primary) return f;
	if (!nfaces || !face_load(&faces[primary])) return NULL;
	Font *f = xcalloc(1, sizeof *f);
	f->px = px;
	f->primary = primary;
	f->cap = 512;
	f->tab = xcalloc((size_t)f->cap, sizeof(Glyph));
	font_metrics(f, primary);
	f->next = fonts;
	fonts = f;
	return f;
}

Font *font_get(int px) { return font_make(px, 0); }

/* a size of another font file (registered as a fallback face if it's new) */
Font *font_get_file(const char *path, int px) {
	int fi = -1;
	for (int i = 0; i < nfaces; i++) if (!strcmp(faces[i].path, path)) fi = i;
	if (fi < 0) fi = font_add_face(path, 1);
	if (fi < 0) return font_get(px);
	Font *f = font_make(px, fi);
	return f ? f : font_get(px);
}

int font_height(Font *f) { return f ? f->height : 16; }
int font_ascent(Font *f) { return f ? f->ascent : 12; }

static void grow(Font *f);

static Glyph *glyph(Font *f, uint32_t cp) {
	if (f->count * 2 >= f->cap) grow(f);
	uint32_t i = (cp * 2654435761u) & (uint32_t)(f->cap - 1);
	while (f->tab[i].used) {
		if (f->tab[i].cp == cp) return &f->tab[i];
		i = (i + 1) & (uint32_t)(f->cap - 1);
	}
	Glyph *g = &f->tab[i];
	memset(g, 0, sizeof *g);
	g->used = 1;
	g->cp = cp;
	f->count++;
	int fi = -1, gi = 0;
	for (int kk = -1; kk < nfaces && fi < 0; kk++) {
		int k = kk < 0 ? f->primary : kk; /* the font's own face first */
		if (kk == f->primary) continue;
		if (!face_load(&faces[k])) continue;
		gi = stbtt_FindGlyphIndex(&faces[k].info, (int)cp);
		if (gi) fi = k;
	}
	if (fi < 0) { fi = f->primary; gi = stbtt_FindGlyphIndex(&faces[fi].info, '?'); }
	if (fi != f->primary && f->scale[fi] == 0) font_metrics(f, fi);
	g->face = (int8_t)fi;
	float sc = f->scale[fi];
	int adv, lsb;
	stbtt_GetGlyphHMetrics(&faces[fi].info, gi, &adv, &lsb);
	g->adv = adv * sc;
	int w, h, xo, yo;
	g->bmp = stbtt_GetGlyphBitmap(&faces[fi].info, sc, sc, gi, &w, &h, &xo, &yo);
	if (font_weight && f->primary == 0 && g->bmp && w > 0 && h > 0) {
		/* firm: about 0.6 px at body size, bold about 1.1 px; in step with the size */
		float s = (font_weight == 1 ? 0.6f : 1.1f) * (float)f->px / 23.0f;
		if (s < 0.5f && font_weight == 2) s = 0.5f; /* small print still reads bold */
		if (s > 2.0f) s = 2.0f;
		if (s >= 0.2f) {
			g->bmp = embolden(g->bmp, &w, h, s);
			g->adv += s * 0.5f; /* a hair more room so letters don't touch */
		}
	}
	g->w = (int16_t)w;
	g->h = (int16_t)h;
	g->xoff = (int16_t)xo;
	g->yoff = (int16_t)(yo + f->base_shift[fi]);
	return g;
}

static void grow(Font *f) {
	Glyph *old = f->tab;
	int oldcap = f->cap;
	f->cap *= 2;
	f->tab = xcalloc((size_t)f->cap, sizeof(Glyph));
	f->count = 0;
	for (int i = 0; i < oldcap; i++) {
		if (!old[i].used) continue;
		uint32_t j = (old[i].cp * 2654435761u) & (uint32_t)(f->cap - 1);
		while (f->tab[j].used) j = (j + 1) & (uint32_t)(f->cap - 1);
		f->tab[j] = old[i];
		f->count++;
	}
	free(old);
}

static uint32_t utf8_next(const char **s) {
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t c = *p++;
	if (c >= 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
		c = ((c & 7) << 18) | ((uint32_t)(p[0] & 63) << 12) | ((uint32_t)(p[1] & 63) << 6) | (p[2] & 63);
		p += 3;
	} else if (c >= 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
		c = ((c & 15) << 12) | ((uint32_t)(p[0] & 63) << 6) | (p[1] & 63);
		p += 2;
	} else if (c >= 0xC0 && (p[0] & 0xC0) == 0x80) {
		c = ((c & 31) << 6) | (p[0] & 63);
		p += 1;
	}
	*s = (const char *)p;
	return c;
}

static float width_f(Font *f, const char *s, const char *end) {
	float w = 0;
	while (*s && (!end || s < end)) w += glyph(f, utf8_next(&s))->adv;
	return w;
}

int text_width(Font *f, const char *s) {
	if (!f || !s) return 0;
	return (int)ceilf(width_f(f, s, NULL));
}

static float draw_range(Font *f, float x, int y, const char *s, const char *end, uint32_t color) {
	while (*s && (!end || s < end)) {
		Glyph *g = glyph(f, utf8_next(&s));
		if (g->bmp) gfx_mask(g->bmp, g->w, g->h, g->w, (int)lroundf(x) + g->xoff, y + f->ascent + g->yoff, color);
		x += g->adv;
	}
	return x;
}

int text_draw(Font *f, int x, int y, const char *s, uint32_t color) {
	if (!f || !s) return x;
	return (int)ceilf(draw_range(f, (float)x, y, s, NULL, color));
}

static const char *ellipsis(Font *f) {
	return glyph(f, 0x2026)->face == 0 && stbtt_FindGlyphIndex(&faces[0].info, 0x2026) ? "\xE2\x80\xA6" : "...";
}

void text_draw_fit(Font *f, int x, int y, int maxw, const char *s, uint32_t color, int align) {
	if (!f || !s) return;
	float w = width_f(f, s, NULL);
	if (w <= maxw) {
		int ox = align == 1 ? (int)((maxw - w) / 2) : align == 2 ? (int)(maxw - w) : 0;
		draw_range(f, (float)(x + ox), y, s, NULL, color);
		return;
	}
	const char *ell = ellipsis(f);
	float ew = width_f(f, ell, NULL);
	float acc = 0;
	const char *p = s, *cut = s;
	while (*p) {
		const char *q = p;
		float a = glyph(f, utf8_next(&q))->adv;
		if (acc + a + ew > maxw) break;
		acc += a;
		p = q;
		cut = p;
	}
	/* don't leave a dangling space before the ellipsis */
	while (cut > s && cut[-1] == ' ') cut--;
	float px = draw_range(f, (float)x, y, s, cut, color);
	draw_range(f, px, y, ell, NULL, color);
}

int text_wrap_lines(Font *f, int maxw, int max_lines, const char *s) {
	if (!f || !s) return 0;
	int lines = 0;
	const char *p = s;
	while (*p && lines < max_lines) {
		while (*p == ' ') p++;
		const char *last_space = NULL, *q = p;
		float w = 0;
		while (*q && *q != '\n') {
			const char *r = q;
			uint32_t cp = utf8_next(&r);
			float a = glyph(f, cp)->adv;
			if (w + a > maxw && q > p) break;
			if (cp == ' ') last_space = q;
			w += a;
			q = r;
		}
		const char *line_end = (*q && *q != '\n' && last_space && last_space > p) ? last_space : q;
		lines++;
		if (lines == max_lines) break;
		p = line_end;
		if (*p == '\n') p++;
	}
	return lines;
}

int text_draw_wrap(Font *f, int x, int y, int maxw, int max_lines, const char *s, uint32_t color, int align) {
	if (!f || !s) return 0;
	int lines = 0;
	const char *p = s;
	while (*p && lines < max_lines) {
		while (*p == ' ') p++;
		const char *line_end = p, *last_space = NULL;
		const char *q = p;
		float w = 0;
		while (*q && *q != '\n') {
			const char *r = q;
			uint32_t cp = utf8_next(&r);
			float a = glyph(f, cp)->adv;
			if (w + a > maxw && q > p) break;
			if (cp == ' ') last_space = q;
			w += a;
			q = r;
		}
		if (*q && *q != '\n' && last_space && last_space > p) line_end = last_space;
		else line_end = q;
		lines++;
		if (lines == max_lines && *line_end && *line_end != '\n') {
			/* last allowed line: ellipsise the remainder */
			char buf[512];
			size_t n = strlen(p);
			sncpy(buf, p, MIN(n + 1, sizeof buf));
			text_draw_fit(f, x, y, maxw, buf, color, align);
		} else {
			float lw = width_f(f, p, line_end);
			int ox = align == 1 ? (int)((maxw - lw) / 2) : align == 2 ? (int)(maxw - lw) : 0;
			draw_range(f, (float)(x + ox), y, p, line_end, color);
		}
		y += f->height;
		p = line_end;
		if (*p == '\n') p++;
	}
	return lines;
}
