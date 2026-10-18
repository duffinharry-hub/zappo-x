/* Clove — TrueType/OpenType text via stb_truetype with a glyph cache. */
#ifndef CLOVE_FONT_H
#define CLOVE_FONT_H

#include <stdint.h>

typedef struct Font Font;

/* Register a font file; the first one becomes the primary face and the rest
 * are fallbacks used for characters the primary lacks (loaded lazily). */
int font_add_face(const char *path, int lazy);
void font_clear_faces(void);
/* 0 regular, 1 firm, 2 bold: the main (first) font drawn heavier, every glyph
 * thickened a fraction of a pixel, so line lengths barely change */
void font_set_weight(int weight);
Font *font_get(int px);       /* cached per pixel size */
Font *font_get_file(const char *path, int px); /* the same, from another font file */
int font_height(Font *f);     /* line height */
int font_ascent(Font *f);
int text_width(Font *f, const char *s);
/* Draws at top-left (x, y). Returns the pen x after the text. */
int text_draw(Font *f, int x, int y, const char *s, uint32_t color);
/* Like text_draw but ellipsises to fit maxw. align: 0 left, 1 centre, 2 right within maxw. */
void text_draw_fit(Font *f, int x, int y, int maxw, const char *s, uint32_t color, int align);
/* Wraps into at most max_lines lines inside maxw; returns lines used. */
int text_draw_wrap(Font *f, int x, int y, int maxw, int max_lines, const char *s, uint32_t color, int align);
int text_wrap_lines(Font *f, int maxw, int max_lines, const char *s); /* how many lines text_draw_wrap would use */

#endif
