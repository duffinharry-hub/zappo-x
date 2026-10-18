/* Clove — software 2D renderer on a 32-bit canvas. */
#ifndef CLOVE_GFX_H
#define CLOVE_GFX_H

#include <stdint.h>

typedef struct {
	int w, h;
	uint32_t *px; /* 0xAARRGGBB, straight (non-premultiplied) alpha */
} Image;

typedef struct {
	int x, y, w, h;
} Rect;

/* Colours are 0xAARRGGBB. */
#define RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
uint32_t col_alpha(uint32_t c, int a); /* replace alpha (0..255) */
uint32_t col_mix(uint32_t a, uint32_t b, float t);
uint32_t col_parse(const char *s, uint32_t fallback); /* "#rrggbb" or "#rrggbbaa" */

void gfx_bind(uint32_t *canvas, int w, int h);
/* Layers (Zappo X's moving backgrounds): the canvas starts see-through and
 * keeps track of how covered each pixel is, so it can go over a background
 * drawn elsewhere. Its colours are then premultiplied by that cover. Binding
 * another canvas switches this off. */
void gfx_keep_alpha(int on);
void gfx_clear_transparent(void);
void gfx_clip(int x, int y, int w, int h);
void gfx_unclip(void);

void gfx_clear(uint32_t c);
void gfx_rect(int x, int y, int w, int h, uint32_t c);
void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c);
void gfx_rrect_stroke(int x, int y, int w, int h, int r, int t, uint32_t c);
void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_shadow(int x, int y, int w, int h, int r, int spread, uint32_t c);
void gfx_circle(int cx, int cy, int r, uint32_t c);
/* Blend an 8-bit coverage mask (e.g. a glyph) tinted with colour c. */
void gfx_mask(const uint8_t *mask, int mw, int mh, int stride, int x, int y, uint32_t c);

Image *img_new(int w, int h);
void img_free(Image *im);
Image *img_load(const char *path);
Image *img_load_mem(const unsigned char *buf, int len); /* JPEG / PNG / BMP bytes */
Image *img_scale(const Image *src, int w, int h);       /* area-average / bilinear */
Image *img_fit(const Image *src, int maxw, int maxh);  /* keep aspect ratio */
Image *img_cover(const Image *src, int w, int h);      /* crop to fill */
void gfx_blit(const Image *im, int x, int y, int alpha);
void gfx_blit_rounded(const Image *im, int x, int y, int r, int alpha);
void gfx_blit_tint(const Image *im, int x, int y, uint32_t tint); /* recolour using image alpha */

#endif
