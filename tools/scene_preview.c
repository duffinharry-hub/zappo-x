/* Renders the moving backgrounds (src/scene.c) in software, to look at them
 * off the handheld: frames as PPM on stdout (pipe into ffmpeg), over a
 * background picture darkened like Zappo's Glass style.
 *   scene_preview BACKGROUND.png ELEMENT SECONDS [fps] > frames.ppm
 * ELEMENT: 0 electric, 1 crystal, 2 fire, 3 water, 4 ice, 5 leaf, 6 dust; -1 = each in turn */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/scene.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#define SW 640
#define SH 480
static float bg[SH][SW][3], fb[SH][SW][3];

static void load_bg(const char *path) {
	int w, h, n;
	unsigned char *px = stbi_load(path, &w, &h, &n, 3);
	if (!px) { fprintf(stderr, "no picture %s\n", path); exit(1); }
	/* cover 640x480, then blur by averaging blocks (like the Glass style), darkened */
	float s = fmaxf((float)SW / w, (float)SH / h);
	int ox = (int)((w * s - SW) / 2), oy = (int)((h * s - SH) / 2);
	static float tmp[SH][SW][3];
	for (int y = 0; y < SH; y++)
		for (int x = 0; x < SW; x++) {
			int sx = (int)((x + ox) / s), sy = (int)((y + oy) / s);
			if (sx >= w) sx = w - 1;
			if (sy >= h) sy = h - 1;
			for (int c = 0; c < 3; c++) tmp[y][x][c] = px[(sy * w + sx) * 3 + c] / 255.0f;
		}
	int R = 9;
	for (int y = 0; y < SH; y++)
		for (int x = 0; x < SW; x++) {
			float acc[3] = {0, 0, 0};
			int n2 = 0;
			for (int dy = -R; dy <= R; dy += 3)
				for (int dx = -R; dx <= R; dx += 3) {
					int yy = y + dy, xx = x + dx;
					if (yy < 0 || yy >= SH || xx < 0 || xx >= SW) continue;
					for (int c = 0; c < 3; c++) acc[c] += tmp[yy][xx][c];
					n2++;
				}
			float f = 0.34f - 0.12f * y / SH;
			float base[3] = {8 / 255.0f, 7 / 255.0f, 16 / 255.0f};
			for (int c = 0; c < 3; c++) bg[y][x][c] = acc[c] / n2 * f + base[c] * (1 - f);
		}
	stbi_image_free(px);
}

static void draw_quad(const SceneQuad *q) {
	int w, h;
	const uint32_t *sp = scene_sprite(q->sprite, &w, &h);
	float ang = q->angle * 3.14159265f / 180, ca = cosf(ang), sa = sinf(ang);
	float hx = q->w / 2, hy = q->h / 2, ext = sqrtf(hx * hx + hy * hy) + 1;
	float tr = ((q->tint >> 16) & 255) / 255.0f, tg = ((q->tint >> 8) & 255) / 255.0f, tb = (q->tint & 255) / 255.0f;
	for (int y = (int)(q->y - ext); y <= (int)(q->y + ext); y++) {
		if (y < 0 || y >= SH) continue;
		for (int x = (int)(q->x - ext); x <= (int)(q->x + ext); x++) {
			if (x < 0 || x >= SW) continue;
			float dx = x + 0.5f - q->x, dy = y + 0.5f - q->y;
			/* into the quad's own axes, then into the sprite */
			float u = (dx * ca + dy * sa) / q->w + 0.5f, v = (-dx * sa + dy * ca) / q->h + 0.5f;
			if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
			float fx = u * w - 0.5f, fy = v * h - 0.5f;
			int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
			float ax = fx - x0, ay = fy - y0, c[4] = {0, 0, 0, 0};
			for (int k = 0; k < 4; k++) {
				int sx = x0 + (k & 1), sy = y0 + (k >> 1);
				if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
				float wt = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
				uint32_t p = sp[sy * w + sx];
				float pa = (p >> 24) / 255.0f;
				c[0] += ((p >> 16) & 255) / 255.0f * pa * wt;
				c[1] += ((p >> 8) & 255) / 255.0f * pa * wt;
				c[2] += (p & 255) / 255.0f * pa * wt;
				c[3] += pa * wt;
			}
			float a = c[3] * q->alpha;
			if (a <= 0) continue;
			float col[3] = {c[0] / fmaxf(c[3], 1e-6f) * tr, c[1] / fmaxf(c[3], 1e-6f) * tg, c[2] / fmaxf(c[3], 1e-6f) * tb};
			for (int k = 0; k < 3; k++) {
				if (q->additive) fb[y][x][k] = fminf(1, fb[y][x][k] + col[k] * a);
				else fb[y][x][k] = col[k] * a + fb[y][x][k] * (1 - a);
			}
		}
	}
}

int main(int argc, char **argv) {
	if (argc < 4) { fprintf(stderr, "usage: scene_preview BG ELEMENT SECONDS [fps]\n"); return 1; }
	load_bg(argv[1]);
	int el = atoi(argv[2]);
	float secs = (float)atof(argv[3]);
	int fps = argc > 4 ? atoi(argv[4]) : 30;
	int cycle = el < 0;
	scene_set(cycle ? 0 : el, 2);
	static SceneQuad q[256];
	static unsigned char out[SH][SW][3];
	int frames = (int)(secs * fps);
	for (int f = 0; f < frames; f++) {
		if (cycle) scene_set((int)((float)f / fps / (secs / 6)) % 6, 2);
		int n = scene_update(1.0f / fps, q, 256);
		memcpy(fb, bg, sizeof fb);
		for (int i = 0; i < n; i++) draw_quad(&q[i]);
		for (int y = 0; y < SH; y++)
			for (int x = 0; x < SW; x++)
				for (int k = 0; k < 3; k++) out[y][x][k] = (unsigned char)(fminf(1, fb[y][x][k]) * 255 + 0.5f);
		printf("P6\n%d %d\n255\n", SW, SH);
		fwrite(out, 1, sizeof out, stdout);
	}
	return 0;
}
