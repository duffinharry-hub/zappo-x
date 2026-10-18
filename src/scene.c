/* Zappo X — the moving backgrounds: particles for each element (see scene.h). */
#include "scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W 640.0f
#define H 480.0f
#define PI 3.14159265f

/* ------------------------------------------------------------- the pictures */

typedef struct { uint32_t *px; int w, h; } Sprite;
static Sprite sprites[SP_COUNT];

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float smooth(float e0, float e1, float x) {
	float t = clampf((x - e0) / (e1 - e0), 0, 1);
	return t * t * (3 - 2 * t);
}
static uint32_t argb(float a, float r, float g, float b) {
	int A = (int)(clampf(a, 0, 1) * 255 + 0.5f), R = (int)(clampf(r, 0, 1) * 255 + 0.5f);
	int G = (int)(clampf(g, 0, 1) * 255 + 0.5f), B = (int)(clampf(b, 0, 1) * 255 + 0.5f);
	return (uint32_t)A << 24 | (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
}
/* distance from p to the segment a-b */
static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
	float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
	float t = l > 0 ? clampf(((px - ax) * dx + (py - ay) * dy) / l, 0, 1) : 0;
	float ex = ax + t * dx - px, ey = ay + t * dy - py;
	return sqrtf(ex * ex + ey * ey);
}

/* each picture is worked out per pixel, 4 x 4 samples for smooth edges */
typedef void (*Shader)(float x, float y, float w, float h, float rgba[4]);

static void make(int sp, int w, int h, Shader f) {
	Sprite *s = &sprites[sp];
	s->w = w;
	s->h = h;
	s->px = malloc(sizeof(uint32_t) * (size_t)(w * h));
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			float acc[4] = {0, 0, 0, 0};
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++) {
					float c[4];
					f(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4, (float)w, (float)h, c);
					/* average with alpha weighting, so edges keep their colour */
					acc[0] += c[0] * c[3];
					acc[1] += c[1] * c[3];
					acc[2] += c[2] * c[3];
					acc[3] += c[3];
				}
			float a = acc[3] / 16;
			s->px[y * w + x] = a > 0 ? argb(a, acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3]) : 0;
		}
}

/* a soft round glow, white (tinted when drawn) */
static void sh_glow(float x, float y, float w, float h, float c[4]) {
	float dx = (x - w / 2) / (w / 2), dy = (y - h / 2) / (h / 2), r2 = dx * dx + dy * dy;
	float core = expf(-r2 * 18), halo = expf(-r2 * 4.5f) * 0.55f;
	c[0] = c[1] = c[2] = 1;
	c[3] = clampf((core + halo) * smooth(1.0f, 0.75f, sqrtf(r2)), 0, 1);
}

/* a little lightning bolt: white-hot core, yellow glow (Electric) */
static const float BOLT[][2] = {{0.60f, 0.12f}, {0.36f, 0.50f}, {0.56f, 0.48f}, {0.40f, 0.88f}};
static void sh_bolt(float x, float y, float w, float h, float c[4]) {
	float d = 1e9f;
	for (int i = 0; i < 3; i++)
		d = fminf(d, seg_dist(x, y, BOLT[i][0] * w, BOLT[i][1] * h, BOLT[i + 1][0] * w, BOLT[i + 1][1] * h));
	float core = smooth(1.6f, 0.6f, d), glow = expf(-d * d / 10.0f) * 0.7f;
	float a = clampf(core + glow * (1 - core), 0, 1);
	/* white in the middle, the Electric Zappo's yellow outside */
	c[0] = 1;
	c[1] = 0.80f + 0.20f * core;
	c[2] = 0.26f + 0.74f * core;
	c[3] = a;
}

/* a crystal shard: a long six-sided gem with a lighter facet (Crystal) */
static void sh_shard(float x, float y, float w, float h, float c[4]) {
	float u = (x - w / 2) / (w / 2), v = (y - h / 2) / (h / 2);	/* -1..1 */
	/* hexagonal outline: pointed top and bottom */
	float half = 1 - fmaxf(0, (fabsf(v) - 0.45f) / 0.55f);	/* width at this height */
	float edge = (half - fabsf(u)) * w / 2;	/* pixels inside the side edge */
	float in = clampf(edge + 0.5f, 0, 1) * clampf((1 - fabsf(v)) * h / 2 + 0.5f, 0, 1);
	if (in <= 0) { c[3] = 0; return; }
	int facet = u < -0.08f + v * 0.15f;	/* the left facet catches the light */
	float r = facet ? 0.86f : 0.58f, g = facet ? 0.74f : 0.38f, b = 1.0f;
	/* a bright rim and a glint near the top */
	float rim = smooth(1.6f, 0.0f, edge);
	r += (1 - r) * rim * 0.6f; g += (1 - g) * rim * 0.6f;
	float gl = expf(-((u + 0.25f) * (u + 0.25f) * 30 + (v + 0.45f) * (v + 0.45f) * 40));
	r += (1 - r) * gl; g += (1 - g) * gl;
	c[0] = r; c[1] = g; c[2] = b;
	c[3] = in * 0.92f;
}

/* an ember: a small flame, yellow at its heart, orange then red outside (Fire) */
static void sh_ember(float x, float y, float w, float h, float c[4]) {
	float u = (x - w / 2) / (w / 2), v = (y - h * 0.62f) / (h * 0.38f);	/* round bottom at v=0..1 */
	/* teardrop: a circle below, narrowing to a point at the top */
	float r;
	if (v >= 0) r = sqrtf(u * u + v * v);
	else {
		float t = clampf(-v / 1.6f, 0, 1);	/* 0 at the widest, 1 at the tip */
		float width = (1 - t) * (1 - t * 0.3f);
		r = width > 0 ? fabsf(u) / width : 9;
		if (t >= 1) r = 9;
	}
	float body = smooth(1.0f, 0.82f, r);
	float glow = expf(-r * r * 1.6f) * 0.35f;
	float heat = smooth(0.85f, 0.15f, r) * smooth(-1.2f, 0.2f, v);	/* hottest low in the middle */
	c[0] = 1;
	c[1] = 0.32f + 0.60f * heat;
	c[2] = 0.05f + 0.55f * heat * heat;
	c[3] = clampf(body + glow * (1 - body), 0, 1);
}

/* a water drop, pointed at the top, with a highlight (Water) */
static void sh_drop(float x, float y, float w, float h, float c[4]) {
	float u = (x - w / 2) / (w / 2), v = (y - h * 0.64f) / (h * 0.36f);
	float r;
	if (v >= 0) r = sqrtf(u * u + v * v);
	else {
		float t = clampf(-v / 1.75f, 0, 1);
		float width = 1 - t;
		r = width > 0.001f ? fabsf(u) / width : 9;
		if (t >= 1) r = 9;
	}
	float in = smooth(1.0f, 0.86f, r);
	float shade = clampf(0.55f + 0.45f * (u * -0.5f + v * 0.6f), 0, 1);
	float hl = expf(-((u + 0.35f) * (u + 0.35f) * 12 + (v + 0.05f) * (v + 0.05f) * 10));
	float rim = smooth(0.7f, 1.0f, r) * in;
	c[0] = 0.25f + 0.25f * shade + 0.7f * hl;
	c[1] = 0.70f + 0.15f * shade + 0.3f * hl;
	c[2] = 1;
	c[3] = clampf(in * (0.55f + 0.35f * rim + 0.4f * hl), 0, 1);
}

/* a bubble: a thin ring with a bright spot (Water) */
static void sh_bubble(float x, float y, float w, float h, float c[4]) {
	float dx = (x - w / 2) / (w / 2), dy = (y - h / 2) / (h / 2), r = sqrtf(dx * dx + dy * dy);
	float ring = smooth(0.13f, 0.0f, fabsf(r - 0.8f)) * smooth(1.0f, 0.93f, r);
	float fill = smooth(0.8f, 0.6f, r) * 0.12f;
	float hl = expf(-((dx + 0.35f) * (dx + 0.35f) + (dy + 0.4f) * (dy + 0.4f)) * 40);
	c[0] = c[1] = c[2] = 1;
	c[3] = clampf(ring * 0.9f + fill + hl, 0, 1);
}

/* a snowflake: six arms with little branches (Ice) */
static void sh_flake(float x, float y, float w, float h, float c[4]) {
	float cx = w / 2, cy = h / 2, R = w * 0.46f, d = 1e9f;
	for (int i = 0; i < 6; i++) {
		float a = i * PI / 3, ca = cosf(a), sa = sinf(a);
		d = fminf(d, seg_dist(x, y, cx, cy, cx + ca * R, cy + sa * R));
		/* a V of branches two thirds out */
		for (int s = -1; s <= 1; s += 2) {
			float bx = cx + ca * R * 0.58f, by = cy + sa * R * 0.58f;
			float b2 = a + s * 0.75f;
			d = fminf(d, seg_dist(x, y, bx, by, bx + cosf(b2) * R * 0.32f, by + sinf(b2) * R * 0.32f));
		}
	}
	float line = smooth(1.3f, 0.5f, d);
	float glow = expf(-d * d / 5.0f) * 0.35f;
	c[0] = c[1] = c[2] = 1;
	c[3] = clampf(line + glow * (1 - line), 0, 1);
}

/* a leaf: pointed oval with a lighter middle vein (Leaf) */
static void sh_leaf(float x, float y, float w, float h, float c[4]) {
	float u = (x - w / 2) / (w * 0.46f), v = (y - h / 2) / (h * 0.42f);
	/* lens shape: |v| < (1 - u^2) */
	float lim = 1 - u * u;
	float d = (lim - fabsf(v)) * h * 0.42f;	/* pixels inside */
	float in = clampf(d + 0.5f, 0, 1) * (fabsf(u) < 1 ? 1.0f : 0.0f);
	/* a short stalk on the left */
	float stalk = smooth(1.2f, 0.4f, seg_dist(x, y, w * 0.02f, h * 0.52f, w * 0.12f, h * 0.5f));
	float vein = smooth(0.9f, 0.2f, fabsf(v * h * 0.42f + u * 1.2f)) * (fabsf(u) < 0.85f);
	float shade = 0.5f + 0.5f * (v < 0 ? 1.0f : 0.6f);
	c[0] = (0.40f + 0.25f * vein) * shade;
	c[1] = (0.78f + 0.22f * vein) * shade;
	c[2] = (0.22f + 0.30f * vein) * shade;
	c[3] = fmaxf(in, stalk);
	if (stalk > in) { c[0] = 0.45f; c[1] = 0.62f; c[2] = 0.25f; }
}

/* a four-pointed glint (Crystal's sparkles) */
static void sh_star(float x, float y, float w, float h, float c[4]) {
	float dx = fabsf(x - w / 2) / (w / 2), dy = fabsf(y - h / 2) / (h / 2);
	float arms = expf(-dx * 22) * smooth(1, 0, dy) + expf(-dy * 22) * smooth(1, 0, dx);
	float core = expf(-(dx * dx + dy * dy) * 30);
	c[0] = c[1] = c[2] = 1;
	c[3] = clampf(arms + core, 0, 1);
}

const uint32_t *scene_sprite(int sp, int *w, int *h) {
	if (sp < 0 || sp >= SP_COUNT) return NULL;
	if (!sprites[sp].px) {
		switch (sp) {
		case SP_GLOW: make(sp, 32, 32, sh_glow); break;
		case SP_BOLT: make(sp, 32, 44, sh_bolt); break;
		case SP_SHARD: make(sp, 18, 40, sh_shard); break;
		case SP_EMBER: make(sp, 20, 32, sh_ember); break;
		case SP_DROP: make(sp, 18, 28, sh_drop); break;
		case SP_BUBBLE: make(sp, 32, 32, sh_bubble); break;
		case SP_FLAKE: make(sp, 32, 32, sh_flake); break;
		case SP_LEAF: make(sp, 34, 18, sh_leaf); break;
		case SP_STAR: make(sp, 32, 32, sh_star); break;
		}
	}
	*w = sprites[sp].w;
	*h = sprites[sp].h;
	return sprites[sp].px;
}

/* ------------------------------------------------------------ the particles */

enum { K_DUST, K_BOLT, K_SPARK, K_SHARD, K_GLINT, K_EMBER, K_CINDER, K_DROP, K_BUBBLE, K_FLAKE, K_LEAF };

typedef struct {
	float x, y, vx, vy;
	float age, life;
	float size, rot, vrot, phase, bright;
	float fade;      /* 0..1: comes in when born, goes when its element leaves */
	int8_t dying;    /* its element has gone: fade out, then go */
	uint8_t kind, el;
} Part;

#define MAXP 160
static Part parts[MAXP];
static int nparts;
static int cur_el = EL_NONE, cur_density = 2;
static float clock_s;

static uint32_t rng = 0x9e3779b9u;
static float frand(void) {
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return (rng >> 8) / 16777216.0f;
}
static float frange(float a, float b) { return a + (b - a) * frand(); }

static const uint32_t EL_TINT[EL_COUNT] = {0xf5c842, 0xb57cff, 0xff8a1f, 0x3fc1ff, 0xcfeaff, 0x9fd84a, 0xe9ecf5};

/* (re)starts a particle; `anywhere`: placed anywhere on screen, part-way
 * through its life (so a new scene doesn't start with everything in a row) */
static void spawn(Part *p, int kind, int el, int anywhere) {
	memset(p, 0, sizeof *p);
	p->kind = (uint8_t)kind;
	p->el = (uint8_t)el;
	p->phase = frange(0, 2 * PI);
	p->bright = frange(0.6f, 1.0f);
	p->fade = anywhere ? 0 : 1;
	switch (kind) {
	case K_DUST:
		p->x = frange(0, W); p->y = frange(0, H);
		p->vx = frange(-4, 4); p->vy = frange(-7, -1);
		p->life = frange(8, 16); p->size = frange(5, 10);
		break;
	case K_BOLT:
		p->x = frange(30, W - 30); p->y = frange(40, H - 40);
		p->vx = frange(-6, 6); p->vy = frange(-6, 6);
		p->life = frange(0.3f, 0.7f); p->size = frange(26, 42);
		p->rot = frange(-35, 35);
		break;
	case K_SPARK:
		p->x = frange(0, W); p->y = frange(H * 0.2f, H + 10);
		p->vx = frange(-10, 10); p->vy = frange(-28, -10);
		p->life = frange(2.5f, 5); p->size = frange(7, 13);
		break;
	case K_SHARD:
		p->x = frange(0, W); p->y = anywhere ? frange(0, H) : H + 30;
		p->vx = frange(-6, 6); p->vy = frange(-16, -6);
		p->life = 60; p->size = frange(14, 30);
		p->rot = frange(-40, 40); p->vrot = frange(-18, 18);
		break;
	case K_GLINT:
		p->x = frange(20, W - 20); p->y = frange(20, H - 20);
		p->life = frange(0.5f, 0.9f); p->size = frange(10, 20);
		break;
	case K_EMBER:
		p->x = frange(0, W); p->y = anywhere ? frange(H * 0.25f, H + 20) : frange(H + 5, H + 40);
		p->vx = frange(-6, 6); p->vy = frange(-48, -22);
		p->life = frange(3, 6); p->size = frange(13, 24);
		break;
	case K_CINDER:
		p->x = frange(0, W); p->y = anywhere ? frange(H * 0.3f, H) : H + 5;
		p->vx = frange(-15, 15); p->vy = frange(-80, -40);
		p->life = frange(1.5f, 3.2f); p->size = frange(5, 9);
		break;
	case K_DROP:
		p->x = frange(0, W); p->y = anywhere ? frange(-20, H) : frange(-60, -20);
		p->vy = frange(30, 60); p->life = 30; p->size = frange(12, 20);
		break;
	case K_BUBBLE:
		p->x = frange(0, W); p->y = anywhere ? frange(0, H) : H + 20;
		p->vy = frange(-26, -10); p->life = 60; p->size = frange(8, 20);
		break;
	case K_FLAKE:
		p->x = frange(-20, W + 20); p->y = anywhere ? frange(-20, H) : frange(-40, -12);
		p->size = frange(7, 22);
		p->vy = 12 + p->size * frange(0.8f, 1.4f); p->vx = frange(-6, 6);
		p->life = 60; p->rot = frange(0, 60); p->vrot = frange(-25, 25);
		break;
	case K_LEAF:
		p->x = anywhere ? frange(-20, W) : frange(-60, W * 0.7f); p->y = anywhere ? frange(-20, H) : frange(-50, -16);
		p->vx = frange(10, 26); p->vy = frange(16, 30);
		p->life = 60; p->size = frange(20, 32);
		p->rot = frange(0, 360); p->vrot = frange(-50, 50);
		break;
	}
	if (anywhere) p->age = frange(0, p->life < 30 ? p->life : 6);
	/* bolts and glints come now and then, not all the time */
	if (kind == K_BOLT) p->age = -frange(anywhere ? 0 : 0.3f, 1.8f);
	if (kind == K_GLINT) p->age = -frange(anywhere ? 0 : 0.4f, 2.4f);
}

/* how many of each, for an element */
typedef struct { int kind, n; } Mix;
static int mix_of(int el, Mix *m) {
	int k = 0;
	switch (el) {
	case EL_ELECTRIC: m[k++] = (Mix){K_DUST, 14}; m[k++] = (Mix){K_SPARK, 14}; m[k++] = (Mix){K_BOLT, 5}; break;
	case EL_CRYSTAL: m[k++] = (Mix){K_DUST, 14}; m[k++] = (Mix){K_SHARD, 12}; m[k++] = (Mix){K_GLINT, 4}; break;
	case EL_FIRE: m[k++] = (Mix){K_DUST, 10}; m[k++] = (Mix){K_EMBER, 20}; m[k++] = (Mix){K_CINDER, 12}; break;
	case EL_WATER: m[k++] = (Mix){K_DUST, 10}; m[k++] = (Mix){K_DROP, 10}; m[k++] = (Mix){K_BUBBLE, 10}; break;
	case EL_ICE: m[k++] = (Mix){K_DUST, 8}; m[k++] = (Mix){K_FLAKE, 30}; break;
	case EL_LEAF: m[k++] = (Mix){K_DUST, 10}; m[k++] = (Mix){K_LEAF, 13}; break;
	case EL_DUST: m[k++] = (Mix){K_DUST, 26}; break;
	}
	return k;
}

void scene_set(int el, int density) {
	if (el == cur_el && density == cur_density) return;
	if (el < EL_NONE || el >= EL_COUNT) el = EL_NONE;
	int first = cur_el == EL_NONE && nparts == 0;
	for (int i = 0; i < nparts; i++) parts[i].dying = 1;
	cur_el = el;
	cur_density = density < 1 ? 1 : density > 2 ? 2 : density;
	if (el == EL_NONE) return;
	Mix m[4];
	int k = mix_of(el, m);
	for (int j = 0; j < k; j++) {
		int n = cur_density == 2 ? m[j].n : (m[j].n + 1) / 2;
		for (int i = 0; i < n && nparts < MAXP; i++) {
			Part *p = &parts[nparts++];
			spawn(p, m[j].kind, el, 1);
			/* fade in, staggered */
			p->fade = first ? 1 : 0;
			p->phase += i * 0.37f;
		}
	}
}

int scene_element(void) { return cur_el; }

static float tri(float x) { return 1 - fabsf(fmodf(x, 2.0f) - 1); }	/* 0..1..0 */

int scene_update(float dt, SceneQuad *out, int max) {
	if (dt > 0.1f) dt = 0.1f;
	clock_s += dt;
	int nq = 0;
	for (int i = 0; i < nparts;) {
		Part *p = &parts[i];
		p->age += dt;
		/* come in, or go when the element has changed */
		if (p->dying) p->fade -= dt / 0.6f;
		else if (p->fade < 1) p->fade = fminf(1, p->fade + dt / 0.8f);
		if (p->dying && p->fade <= 0) { parts[i] = parts[--nparts]; continue; }
		if (p->age < 0) { i++; continue; }	/* waiting for its turn */
		float t = p->age, lf = p->life;
		float x = p->x, y = p->y, a = 0, rot = p->rot, sw = p->size, shh = p->size;
		int sp = SP_GLOW, add = 1;
		uint32_t tint = 0xffffff;
		int ended = 0;
		switch (p->kind) {
		case K_DUST:
			x += p->vx * t + sinf(t * 0.5f + p->phase) * 6;
			y += p->vy * t;
			a = 0.16f + 0.22f * p->bright * (0.6f + 0.4f * sinf(t * 1.3f + p->phase));
			a *= smooth(0, 1.5f, t) * smooth(lf, lf - 1.5f, t);
			tint = EL_TINT[p->el];
			ended = t >= lf;
			break;
		case K_SPARK:
			x += p->vx * t + sinf(t * 3 + p->phase) * 4;
			y += p->vy * t;
			a = (0.5f + 0.5f * p->bright) * (sinf(t * 23 + p->phase) > -0.5f ? 1 : 0.35f);
			a *= smooth(0, 0.3f, t) * smooth(lf, lf * 0.6f, t);
			tint = 0xffe27a;
			ended = t >= lf;
			break;
		case K_BOLT: {
			x += p->vx * t; y += p->vy * t;
			sp = SP_BOLT;
			sw = p->size * 0.66f; shh = p->size;
			/* on, off, on: a crackle */
			float f = t / lf;
			a = (f < 0.3f || (f > 0.45f && f < 0.8f)) ? 1.0f : 0.15f;
			a *= p->bright;
			ended = t >= lf;
			break;
		}
		case K_SHARD:
			x += p->vx * t + sinf(t * 0.6f + p->phase) * 10;
			y += p->vy * t;
			rot = p->rot + p->vrot * t;
			sp = SP_SHARD; add = 0;
			sw = p->size * 0.45f; shh = p->size;
			a = (0.45f + 0.35f * p->bright) * (0.8f + 0.2f * sinf(t * 1.7f + p->phase));
			ended = y < -40;
			break;
		case K_GLINT:
			sp = SP_STAR;
			a = tri(t / lf) * p->bright;
			sw = shh = p->size * (0.5f + 0.5f * tri(t / lf));
			rot = t * 40;
			tint = 0xe6d2ff;
			ended = t >= lf;
			break;
		case K_EMBER: {
			float f = t / lf;
			x += p->vx * t + sinf(t * 2.1f + p->phase) * 9 * f;
			y += p->vy * t;
			sp = SP_EMBER;
			float s = 1 - 0.55f * f;
			sw = p->size * 0.62f * s; shh = p->size * s;
			rot = sinf(t * 2.1f + p->phase) * 12;
			a = (0.55f + 0.4f * p->bright) * smooth(0, 0.4f, t) * powf(1 - f, 0.7f);
			ended = t >= lf;
			break;
		}
		case K_CINDER:
			x += p->vx * t + sinf(t * 5 + p->phase) * 3;
			y += p->vy * t;
			a = (0.6f + 0.4f * p->bright) * (1 - t / lf) * (sinf(t * 17 + p->phase) > -0.6f ? 1 : 0.4f);
			tint = 0xffb04a;
			ended = t >= lf;
			break;
		case K_DROP:
			y += p->vy * t;
			x += sinf(t * 0.7f + p->phase) * 3;
			sp = SP_DROP; add = 0;
			sw = p->size * 0.64f; shh = p->size;
			a = (0.55f + 0.3f * p->bright) * smooth(H + 10, H - 70, y);
			ended = y > H + 20;
			break;
		case K_BUBBLE:
			y += p->vy * t;
			x += sinf(t * 1.6f + p->phase) * 6;
			sp = SP_BUBBLE; add = 0;
			a = (0.3f + 0.25f * p->bright) * smooth(-20, 40, y);
			tint = 0xa6e4ff;
			ended = y < -20;
			break;
		case K_FLAKE:
			y += p->vy * t;
			x += p->vx * t + sinf(t * 0.9f + p->phase) * 14;
			rot = p->rot + p->vrot * t;
			sp = SP_FLAKE;
			a = 0.35f + 0.45f * (p->size / 22) * p->bright;
			tint = 0xe6f6ff;
			ended = y > H + 25;
			break;
		case K_LEAF: {
			x += p->vx * t + sinf(t * 1.1f + p->phase) * 22;
			y += p->vy * t + sinf(t * 2.2f + p->phase) * 4;
			rot = p->rot + p->vrot * t + sinf(t * 1.1f + p->phase) * 25;
			sp = SP_LEAF; add = 0;
			/* turning over as it falls */
			float flip = fabsf(cosf(t * 1.4f + p->phase));
			sw = p->size; shh = p->size * 0.53f * (0.25f + 0.75f * flip);
			a = 0.75f + 0.2f * p->bright;
			tint = p->phase > PI ? 0xffffff : 0xd8f0a0;
			ended = y > H + 30 || x > W + 40;
			break;
		}
		}
		if (ended) {
			if (p->dying) { parts[i] = parts[--nparts]; continue; }
			spawn(p, p->kind, p->el, 0);
			p->fade = 1;
			continue;	/* (drawn from the next frame) */
		}
		a *= p->fade;
		/* embers and bolts light up the air around them */
		if ((p->kind == K_EMBER || p->kind == K_BOLT) && a > 0.01f && nq < max) {
			SceneQuad *g = &out[nq++];
			g->x = x; g->y = y; g->w = g->h = shh * (p->kind == K_BOLT ? 1.6f : 2.2f);
			g->angle = 0; g->alpha = clampf(a * (p->kind == K_BOLT ? 0.45f : 0.32f), 0, 1);
			g->tint = p->kind == K_BOLT ? 0xf5c842 : 0xff7a1a; g->sprite = SP_GLOW; g->additive = 1;
		}
		if (a > 0.01f && nq < max && x > -40 && x < W + 40 && y > -40 && y < H + 40) {
			SceneQuad *q = &out[nq++];
			q->x = x; q->y = y; q->w = sw; q->h = shh;
			q->angle = rot; q->alpha = clampf(a, 0, 1);
			q->tint = tint; q->sprite = (uint8_t)sp; q->additive = (uint8_t)add;
		}
		i++;
	}
	return nq;
}

/* ------------------------------------------------------------- without a GPU */

void scene_draw_soft(uint32_t *dst, int dw, int dh, const SceneQuad *qs, int n) {
	for (int qi = 0; qi < n; qi++) {
		const SceneQuad *q = &qs[qi];
		int w, h;
		const uint32_t *sp = scene_sprite(q->sprite, &w, &h);
		float ang = q->angle * PI / 180, ca = cosf(ang), sa = sinf(ang);
		float hx = q->w / 2, hy = q->h / 2, ext = sqrtf(hx * hx + hy * hy) + 1;
		int tr = (q->tint >> 16) & 255, tg = (q->tint >> 8) & 255, tb = q->tint & 255;
		int qa = (int)(q->alpha * 256);
		int y0 = (int)(q->y - ext), y1 = (int)(q->y + ext), x0 = (int)(q->x - ext), x1 = (int)(q->x + ext);
		if (y0 < 0) y0 = 0;
		if (x0 < 0) x0 = 0;
		if (y1 >= dh) y1 = dh - 1;
		if (x1 >= dw) x1 = dw - 1;
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++) {
				float dx = x + 0.5f - q->x, dy = y + 0.5f - q->y;
				float u = (dx * ca + dy * sa) / q->w + 0.5f, v = (-dx * sa + dy * ca) / q->h + 0.5f;
				if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
				uint32_t s = sp[(int)(v * h) * w + (int)(u * w)];	/* nearest: it's a stand-in */
				int a = (int)(s >> 24) * qa >> 8;
				if (a <= 0) continue;
				int r = (int)((s >> 16) & 255) * tr / 255, g = (int)((s >> 8) & 255) * tg / 255, b = (int)(s & 255) * tb / 255;
				uint32_t d = dst[y * dw + x];
				int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
				if (q->additive) {
					dr += r * a / 255; dg += g * a / 255; db += b * a / 255;
					if (dr > 255) dr = 255;
					if (dg > 255) dg = 255;
					if (db > 255) db = 255;
				} else {
					dr += (r - dr) * a / 255; dg += (g - dg) * a / 255; db += (b - db) * a / 255;
				}
				dst[y * dw + x] = 0xFF000000u | (uint32_t)dr << 16 | (uint32_t)dg << 8 | (uint32_t)db;
			}
	}
}

void scene_compose_soft(uint32_t *dst, const uint32_t *bg, const uint32_t *menu, int w, int h, float dt) {
	static SceneQuad q[256];
	memcpy(dst, bg, sizeof(uint32_t) * (size_t)(w * h));
	scene_draw_soft(dst, w, h, q, scene_update(dt, q, 256));
	for (int i = 0; i < w * h; i++) {
		uint32_t c = menu[i], ca = c >> 24;
		if (ca == 255) dst[i] = c;
		else if (ca) {
			uint32_t d = dst[i], ia = 255 - ca;
			uint32_t r = ((c >> 16) & 255) + ((d >> 16) & 255) * ia / 255;
			uint32_t g = ((c >> 8) & 255) + ((d >> 8) & 255) * ia / 255;
			uint32_t b = (c & 255) + (d & 255) * ia / 255;
			dst[i] = 0xFF000000u | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
		}
	}
}
