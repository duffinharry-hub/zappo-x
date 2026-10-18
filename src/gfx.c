#include "gfx.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO_FALLBACK
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x)
#define STBI_NO_SIMD
#include "../third_party/stb_image.h"

static uint32_t *cv;
static int cw, ch;
static int clx0, cly0, clx1, cly1;
static int keep_alpha;

void gfx_bind(uint32_t *canvas, int w, int h) {
	cv = canvas;
	cw = w;
	ch = h;
	keep_alpha = 0;
	gfx_unclip();
}
void gfx_keep_alpha(int on) { keep_alpha = on; }
void gfx_clear_transparent(void) { memset(cv, 0, sizeof(uint32_t) * (size_t)(cw * ch)); }
void gfx_clip(int x, int y, int w, int h) {
	clx0 = MAX(0, x);
	cly0 = MAX(0, y);
	clx1 = MIN(cw, x + w);
	cly1 = MIN(ch, y + h);
}
void gfx_unclip(void) {
	clx0 = cly0 = 0;
	clx1 = cw;
	cly1 = ch;
}

uint32_t col_alpha(uint32_t c, int a) { return (c & 0x00FFFFFFu) | ((uint32_t)CLAMP(a, 0, 255) << 24); }

uint32_t col_mix(uint32_t a, uint32_t b, float t) {
	if (t <= 0) return a;
	if (t >= 1) return b;
	int it = (int)(t * 256);
	uint32_t out = 0;
	for (int s = 0; s < 32; s += 8) {
		int ca = (a >> s) & 255, cb = (b >> s) & 255;
		out |= (uint32_t)((ca * (256 - it) + cb * it) >> 8) << s;
	}
	return out;
}

static int hexv(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}
uint32_t col_parse(const char *s, uint32_t fallback) {
	if (!s) return fallback;
	if (*s == '#') s++;
	size_t n = strlen(s);
	if (n != 6 && n != 8) return fallback;
	uint32_t v[4] = {0, 0, 0, 255};
	for (size_t i = 0; i < n / 2; i++) {
		int hi = hexv(s[i * 2]), lo = hexv(s[i * 2 + 1]);
		if (hi < 0 || lo < 0) return fallback;
		v[i] = (uint32_t)(hi * 16 + lo);
	}
	return RGBA(v[0], v[1], v[2], v[3]);
}

/* dst = src*a + dst*(1-a), a in 0..255 */
static inline uint32_t blend(uint32_t dst, uint32_t src, uint32_t a) {
	if (a >= 255) return src | 0xFF000000u;
	if (a == 0) return dst;
	uint32_t ia = 255 - a;
	uint32_t rb = ((src & 0xFF00FF) * a + (dst & 0xFF00FF) * ia + 0x800080) ;
	uint32_t g = ((src & 0x00FF00) * a + (dst & 0x00FF00) * ia + 0x008000);
	rb = ((rb + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
	g = ((g + ((g >> 8) & 0x00FF00)) >> 8) & 0x00FF00;
	/* (on a see-through canvas the colour above is already premultiplied:
	 * the cover adds up the same way) */
	uint32_t da = keep_alpha ? a + (((dst >> 24) * ia + 127) / 255) : 255;
	return (da << 24) | rb | g;
}

void gfx_clear(uint32_t c) {
	c |= 0xFF000000u;
	for (int i = 0; i < cw * ch; i++) cv[i] = c;
}

static void hspan(int x0, int x1, int y, uint32_t c, uint32_t a) {
	if (y < cly0 || y >= cly1) return;
	if (x0 < clx0) x0 = clx0;
	if (x1 > clx1) x1 = clx1;
	uint32_t *p = cv + y * cw;
	if (a >= 255) {
		uint32_t s = c | 0xFF000000u;
		for (int x = x0; x < x1; x++) p[x] = s;
	} else if (a) {
		for (int x = x0; x < x1; x++) p[x] = blend(p[x], c, a);
	}
}

static inline void plot(int x, int y, uint32_t c, uint32_t a) {
	if (x < clx0 || x >= clx1 || y < cly0 || y >= cly1 || !a) return;
	uint32_t *p = cv + y * cw + x;
	*p = blend(*p, c, a);
}

void gfx_rect(int x, int y, int w, int h, uint32_t c) {
	uint32_t a = c >> 24;
	for (int yy = MAX(y, cly0); yy < MIN(y + h, cly1); yy++) hspan(x, x + w, yy, c, a);
}

/* Coverage (0..1) of pixel centre (fx, fy) by a rounded rectangle. */
static float cov_rrect(float fx, float fy, float x, float y, float w, float h, float r) {
	if (w <= 0 || h <= 0) return 0;
	if (r <= 0.5f) {
		float cx = fminf(fx + 0.5f, x + w) - fmaxf(fx - 0.5f, x);
		float cy = fminf(fy + 0.5f, y + h) - fmaxf(fy - 0.5f, y);
		if (cx <= 0 || cy <= 0) return 0;
		return fminf(cx, 1) * fminf(cy, 1);
	}
	float cx = CLAMP(fx, x + r, x + w - r);
	float cy = CLAMP(fy, y + r, y + h - r);
	float dx = fx - cx, dy = fy - cy;
	float d = sqrtf(dx * dx + dy * dy);
	float c = r - d + 0.5f;
	return c <= 0 ? 0 : (c >= 1 ? 1 : c);
}

void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c) {
	if (w <= 0 || h <= 0) return;
	r = MIN(r, MIN(w, h) / 2);
	uint32_t a = c >> 24;
	for (int yy = MAX(y, cly0); yy < MIN(y + h, cly1); yy++) {
		if (yy >= y + r && yy < y + h - r) { hspan(x, x + w, yy, c, a); continue; }
		float fy = yy + 0.5f;
		for (int xx = x; xx < x + r; xx++) plot(xx, yy, c, (uint32_t)(a * cov_rrect(xx + 0.5f, fy, x, y, w, h, r)));
		hspan(x + r, x + w - r, yy, c, a);
		for (int xx = x + w - r; xx < x + w; xx++) plot(xx, yy, c, (uint32_t)(a * cov_rrect(xx + 0.5f, fy, x, y, w, h, r)));
	}
}

void gfx_rrect_stroke(int x, int y, int w, int h, int r, int t, uint32_t c) {
	if (w <= 0 || h <= 0 || t <= 0) return;
	r = MIN(r, MIN(w, h) / 2);
	uint32_t a = c >> 24;
	int ir = MAX(0, r - t);
	int band = MAX(r, t) + 1;
	for (int yy = MAX(y, cly0); yy < MIN(y + h, cly1); yy++) {
		float fy = yy + 0.5f;
		int full = yy < y + band || yy >= y + h - band;
		for (int xx = x; xx < x + w; xx++) {
			if (!full && xx == x + band && x + w - band - 1 > xx) { xx = x + w - band - 1; continue; }
			float co = cov_rrect(xx + 0.5f, fy, x, y, w, h, r);
			float ci = cov_rrect(xx + 0.5f, fy, x + t, y + t, w - 2 * t, h - 2 * t, ir);
			float cc = co - ci;
			if (cc > 0) plot(xx, yy, c, (uint32_t)(a * cc));
		}
	}
}

void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
	for (int yy = MAX(y, cly0); yy < MIN(y + h, cly1); yy++) {
		uint32_t c = col_mix(top, bottom, h > 1 ? (float)(yy - y) / (h - 1) : 0);
		hspan(x, x + w, yy, c, c >> 24);
	}
}

void gfx_shadow(int x, int y, int w, int h, int r, int spread, uint32_t c) {
	int a = (int)(c >> 24);
	for (int i = spread; i >= 1; i--) {
		float f = 1.0f - (float)i / (spread + 1);
		gfx_rrect_stroke(x - i, y - i, w + 2 * i, h + 2 * i, r + i, 1, col_alpha(c, (int)(a * f * f)));
	}
}

void gfx_circle(int cx, int cy, int r, uint32_t c) { gfx_rrect(cx - r, cy - r, r * 2, r * 2, r, c); }

void gfx_mask(const uint8_t *mask, int mw, int mh, int stride, int x, int y, uint32_t c) {
	uint32_t ca = c >> 24;
	int y0 = MAX(y, cly0), y1 = MIN(y + mh, cly1);
	int x0 = MAX(x, clx0), x1 = MIN(x + mw, clx1);
	for (int yy = y0; yy < y1; yy++) {
		const uint8_t *m = mask + (yy - y) * stride;
		uint32_t *p = cv + yy * cw;
		for (int xx = x0; xx < x1; xx++) {
			uint32_t v = m[xx - x];
			if (!v) continue;
			p[xx] = blend(p[xx], c, (v * ca + 127) / 255);
		}
	}
}

/* ----------------------------------------------------------------- images */

Image *img_new(int w, int h) {
	Image *im = xmalloc(sizeof *im);
	im->w = w;
	im->h = h;
	im->px = xcalloc((size_t)w * (size_t)h, 4);
	return im;
}
void img_free(Image *im) {
	if (!im) return;
	free(im->px);
	free(im);
}

Image *img_load(const char *path) {
	int w, h, n;
	unsigned char *d = stbi_load(path, &w, &h, &n, 4);
	if (!d) return NULL;
	if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { stbi_image_free(d); return NULL; }
	Image *im = xmalloc(sizeof *im);
	im->w = w;
	im->h = h;
	im->px = xmalloc((size_t)w * (size_t)h * 4);
	for (int i = 0; i < w * h; i++) {
		const unsigned char *s = d + i * 4;
		im->px[i] = RGBA(s[0], s[1], s[2], s[3]);
	}
	stbi_image_free(d);
	return im;
}

Image *img_load_mem(const unsigned char *buf, int len) {
	int w, h, n;
	if (!buf || len <= 0) return NULL;
	unsigned char *d = stbi_load_from_memory(buf, len, &w, &h, &n, 4);
	if (!d) return NULL;
	if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { stbi_image_free(d); return NULL; }
	Image *im = xmalloc(sizeof *im);
	im->w = w;
	im->h = h;
	im->px = xmalloc((size_t)w * (size_t)h * 4);
	for (int i = 0; i < w * h; i++) {
		const unsigned char *s = d + i * 4;
		im->px[i] = RGBA(s[0], s[1], s[2], s[3]);
	}
	stbi_image_free(d);
	return im;
}

Image *img_scale(const Image *src, int w, int h) {
	if (!src || w <= 0 || h <= 0) return NULL;
	Image *out = img_new(w, h);
	if (w <= src->w && h <= src->h) {
		/* Box filter with alpha-weighted colour, good for downscaling art. */
		for (int y = 0; y < h; y++) {
			int sy0 = y * src->h / h, sy1 = MAX(sy0 + 1, (y + 1) * src->h / h);
			for (int x = 0; x < w; x++) {
				int sx0 = x * src->w / w, sx1 = MAX(sx0 + 1, (x + 1) * src->w / w);
				uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
				for (int sy = sy0; sy < sy1; sy++) {
					const uint32_t *row = src->px + sy * src->w;
					for (int sx = sx0; sx < sx1; sx++) {
						uint32_t c = row[sx], ca = c >> 24;
						r += ((c >> 16) & 255) * ca;
						g += ((c >> 8) & 255) * ca;
						b += (c & 255) * ca;
						a += ca;
						n++;
					}
				}
				out->px[y * w + x] = a ? RGBA(r / a, g / a, b / a, a / n) : 0;
			}
		}
	} else {
		/* Bilinear for enlarging. */
		for (int y = 0; y < h; y++) {
			float fy = ((y + 0.5f) * src->h / h) - 0.5f;
			int y0 = (int)floorf(fy);
			float ty = fy - y0;
			int ya = CLAMP(y0, 0, src->h - 1), yb = CLAMP(y0 + 1, 0, src->h - 1);
			for (int x = 0; x < w; x++) {
				float fx = ((x + 0.5f) * src->w / w) - 0.5f;
				int x0 = (int)floorf(fx);
				float tx = fx - x0;
				int xa = CLAMP(x0, 0, src->w - 1), xb = CLAMP(x0 + 1, 0, src->w - 1);
				uint32_t c[4] = {src->px[ya * src->w + xa], src->px[ya * src->w + xb], src->px[yb * src->w + xa], src->px[yb * src->w + xb]};
				float wt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
				float r = 0, g = 0, b = 0, a = 0;
				for (int k = 0; k < 4; k++) {
					float ca = (c[k] >> 24) * wt[k];
					r += ((c[k] >> 16) & 255) * ca;
					g += ((c[k] >> 8) & 255) * ca;
					b += (c[k] & 255) * ca;
					a += ca;
				}
				out->px[y * w + x] = a > 0.001f ? RGBA((int)(r / a), (int)(g / a), (int)(b / a), (int)(a + 0.5f)) : 0;
			}
		}
	}
	return out;
}

Image *img_fit(const Image *src, int maxw, int maxh) {
	if (!src) return NULL;
	float s = fminf((float)maxw / src->w, (float)maxh / src->h);
	int w = MAX(1, (int)(src->w * s + 0.5f)), h = MAX(1, (int)(src->h * s + 0.5f));
	return img_scale(src, w, h);
}

Image *img_cover(const Image *src, int w, int h) {
	if (!src) return NULL;
	float s = fmaxf((float)w / src->w, (float)h / src->h);
	int sw = MAX(w, (int)(src->w * s + 0.5f)), sh = MAX(h, (int)(src->h * s + 0.5f));
	Image *big = img_scale(src, sw, sh);
	Image *out = img_new(w, h);
	int ox = (sw - w) / 2, oy = (sh - h) / 2;
	for (int y = 0; y < h; y++) memcpy(out->px + y * w, big->px + (y + oy) * sw + ox, (size_t)w * 4);
	img_free(big);
	return out;
}

void gfx_blit(const Image *im, int x, int y, int alpha) {
	if (!im) return;
	int x0 = MAX(x, clx0), x1 = MIN(x + im->w, clx1);
	int y0 = MAX(y, cly0), y1 = MIN(y + im->h, cly1);
	for (int yy = y0; yy < y1; yy++) {
		const uint32_t *s = im->px + (yy - y) * im->w - x;
		uint32_t *p = cv + yy * cw;
		for (int xx = x0; xx < x1; xx++) {
			uint32_t c = s[xx];
			uint32_t a = c >> 24;
			if (alpha < 255) a = a * (uint32_t)alpha / 255;
			if (a) p[xx] = blend(p[xx], c, a);
		}
	}
}

void gfx_blit_rounded(const Image *im, int x, int y, int r, int alpha) {
	if (!im) return;
	r = MIN(r, MIN(im->w, im->h) / 2);
	int x0 = MAX(x, clx0), x1 = MIN(x + im->w, clx1);
	int y0 = MAX(y, cly0), y1 = MIN(y + im->h, cly1);
	for (int yy = y0; yy < y1; yy++) {
		const uint32_t *s = im->px + (yy - y) * im->w - x;
		uint32_t *p = cv + yy * cw;
		int corner_row = (yy - y) < r || (yy - y) >= im->h - r;
		for (int xx = x0; xx < x1; xx++) {
			uint32_t c = s[xx];
			uint32_t a = (c >> 24) * (uint32_t)alpha / 255;
			if (corner_row && ((xx - x) < r || (xx - x) >= im->w - r))
				a = (uint32_t)(a * cov_rrect(xx + 0.5f, yy + 0.5f, x, y, im->w, im->h, r));
			if (a) p[xx] = blend(p[xx], c, a);
		}
	}
}

void gfx_blit_tint(const Image *im, int x, int y, uint32_t tint) {
	if (!im) return;
	uint32_t ta = tint >> 24;
	int x0 = MAX(x, clx0), x1 = MIN(x + im->w, clx1);
	int y0 = MAX(y, cly0), y1 = MIN(y + im->h, cly1);
	for (int yy = y0; yy < y1; yy++) {
		const uint32_t *s = im->px + (yy - y) * im->w - x;
		uint32_t *p = cv + yy * cw;
		for (int xx = x0; xx < x1; xx++) {
			uint32_t a = (s[xx] >> 24) * ta / 255;
			if (a) p[xx] = blend(p[xx], tint, a);
		}
	}
}
