/* Blockbox — a small Minecraft-style voxel sandbox, written as a libretro core.
 *
 * It uses no C library at all, so the same .so loads in GarlicOS's uClibc
 * RetroArch and in any glibc RetroArch. Rendering is a per-pixel voxel
 * raycaster into a 320x240 RGB565 frame; the world is 128 x 64 x 128 blocks
 * and is saved through RetroArch's save RAM (.srm), so each ".world" file
 * you launch is its own world. */
#include "libretro.h"
#include "font6x10.h"

#define EXPORT __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))

/* ------------------------------------------------- tiny freestanding libc */

HIDDEN void *memset(void *d, int c, size_t n) {
	unsigned char *p = d;
	while (n--) *p++ = (unsigned char)c;
	return d;
}
HIDDEN void *memcpy(void *d, const void *s, size_t n) {
	unsigned char *p = d;
	const unsigned char *q = s;
	while (n--) *p++ = *q++;
	return d;
}
HIDDEN void *memmove(void *d, const void *s, size_t n) {
	unsigned char *p = d;
	const unsigned char *q = s;
	if (p < q) while (n--) *p++ = *q++;
	else { p += n; q += n; while (n--) *--p = *--q; }
	return d;
}
HIDDEN int memcmp(const void *a, const void *b, size_t n) {
	const unsigned char *p = a, *q = b;
	for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
	return 0;
}
/* ARM EABI helpers some compilers emit for struct copies/zeroing. */
HIDDEN void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
HIDDEN void __aeabi_memcpy4(void *d, const void *s, size_t n) { memcpy(d, s, n); }
HIDDEN void __aeabi_memcpy8(void *d, const void *s, size_t n) { memcpy(d, s, n); }
HIDDEN void __aeabi_memmove(void *d, const void *s, size_t n) { memmove(d, s, n); }
HIDDEN void __aeabi_memset(void *d, size_t n, int c) { memset(d, c, n); }
HIDDEN void __aeabi_memset4(void *d, size_t n, int c) { memset(d, c, n); }
HIDDEN void __aeabi_memclr(void *d, size_t n) { memset(d, 0, n); }
HIDDEN void __aeabi_memclr4(void *d, size_t n) { memset(d, 0, n); }
HIDDEN void __aeabi_memclr8(void *d, size_t n) { memset(d, 0, n); }

#ifdef BLOCKBOX_NO_LIBGCC
/* Integer division helpers normally supplied by libgcc. The Cortex-A9 has no
 * divide instruction; these are plain shift-and-subtract versions so the core
 * links without any toolchain runtime (GarlicOS uses the soft-float ABI). */
static unsigned udiv_(unsigned n, unsigned d, unsigned *rem) {
	if (d == 0) { *rem = 0; return 0xFFFFFFFFu; }
	unsigned q = 0, r = 0;
	for (int i = 31; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1u);
		if (r >= d) { r -= d; q |= 1u << i; }
	}
	*rem = r;
	return q;
}
HIDDEN unsigned __aeabi_uidiv(unsigned n, unsigned d) { unsigned r; return udiv_(n, d, &r); }
HIDDEN unsigned long long __aeabi_uidivmod(unsigned n, unsigned d) {
	unsigned r, q = udiv_(n, d, &r);
	return ((unsigned long long)r << 32) | q;
}
HIDDEN int __aeabi_idiv(int n, int d) {
	unsigned r, q = udiv_(n < 0 ? -(unsigned)n : (unsigned)n, d < 0 ? -(unsigned)d : (unsigned)d, &r);
	return (n < 0) != (d < 0) ? -(int)q : (int)q;
}
HIDDEN unsigned long long __aeabi_idivmod(int n, int d) {
	unsigned r, q = udiv_(n < 0 ? -(unsigned)n : (unsigned)n, d < 0 ? -(unsigned)d : (unsigned)d, &r);
	int qs = (n < 0) != (d < 0) ? -(int)q : (int)q;
	int rs = n < 0 ? -(int)r : (int)r;
	return ((unsigned long long)(unsigned)rs << 32) | (unsigned)qs;
}
#endif

#define PI 3.14159265f
static inline float fabs_(float x) { return __builtin_fabsf(x); }
static inline float sqrt_(float x) { return __builtin_sqrtf(x); }
static inline int ifloor(float x) { int i = (int)x; return x < (float)i ? i - 1 : i; }
static inline float fmin_(float a, float b) { return a < b ? a : b; }
static inline float fmax_(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float sin_(float x) {
	x -= 2 * PI * (float)ifloor((x + PI) / (2 * PI));
	if (x > PI / 2) x = PI - x;
	else if (x < -PI / 2) x = -PI - x;
	float x2 = x * x;
	return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static float cos_(float x) { return sin_(x + PI / 2); }

/* ------------------------------------------------------------------ world */

#define WX 128
#define WY 64
#define WZ 128
#define SEA 20
#define IDX(x, y, z) ((((y) << 7) | (z)) << 7 | (x))

enum { AIR, GRASS, DIRT, STONE, COBBLE, SAND, LOG, LEAVES, PLANKS, BRICK, SNOW, WATER, GLASS, BEDROCK, NBLOCK };
static const char *const BLOCK_NAMES[NBLOCK] = {
	"Air", "Grass", "Dirt", "Stone", "Cobblestone", "Sand", "Log", "Leaves", "Planks", "Brick", "Snow", "Water", "Glass", "Bedrock",
};
static const unsigned char HOTBAR[] = {GRASS, DIRT, STONE, COBBLE, PLANKS, LOG, LEAVES, BRICK, GLASS, SAND};
#define NSLOT ((int)sizeof(HOTBAR))

#define SAVE_MAGIC 0x584F4242u /* "BBOX" */
#define SAVE_VERSION 1
typedef struct {
	uint32_t magic, version, seed, flags;
	float px, py, pz, yaw, pitch;
	uint8_t fly, slot, time, detail;
	uint8_t sens_turn, sens_look, sens_move, invert_look; /* 1..10, 0 = default (5) */
	uint8_t pad[24];
	uint8_t blocks[WX * WY * WZ];
} Save;
static Save sv;
static uint8_t hm[WX * WZ]; /* first y above the top-most opaque block per column */
static uint32_t pending_seed;
static int world_ready;

static inline int solid(int b) { return b != AIR && b != WATER; }
static inline int opaque(int b) { return b != AIR && b != WATER && b != LEAVES && b != GLASS; }
static inline int casts_shadow(int b) { return b != AIR && b != WATER && b != GLASS; }
static int top_y = WY; /* nothing solid at or above this height */

static uint32_t hash3(int x, int y, uint32_t seed) {
	uint32_t h = seed ^ ((uint32_t)x * 374761393u) ^ ((uint32_t)y * 668265263u);
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}
static float rnd01(int x, int y, uint32_t seed) { return (hash3(x, y, seed) & 0xFFFFFF) / 16777216.0f; }
static float vnoise(float x, float y, uint32_t seed) {
	int xi = ifloor(x), yi = ifloor(y);
	float fx = x - xi, fy = y - yi;
	float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
	float a = rnd01(xi, yi, seed), b = rnd01(xi + 1, yi, seed);
	float c = rnd01(xi, yi + 1, seed), d = rnd01(xi + 1, yi + 1, seed);
	return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
}
static float fbm(float x, float y, uint32_t seed, int oct) {
	float s = 0, amp = 0.5f, norm = 0;
	for (int i = 0; i < oct; i++) {
		s += vnoise(x, y, seed + (uint32_t)i * 7919u) * amp;
		norm += amp;
		x *= 2.03f;
		y *= 2.03f;
		amp *= 0.5f;
	}
	return s / norm;
}

static inline uint8_t get(int x, int y, int z) {
	if ((unsigned)x >= WX || (unsigned)z >= WZ) return BEDROCK;
	if ((unsigned)y >= WY) return y < 0 ? BEDROCK : AIR;
	return sv.blocks[IDX(x, y, z)];
}

static void column_height(int x, int z) {
	int y = WY - 1;
	while (y > 0 && !casts_shadow(sv.blocks[IDX(x, y, z)])) y--;
	hm[z * WX + x] = (uint8_t)(y + 1);
	if (y + 2 > top_y) top_y = y + 2;
}
static void all_heights(void) {
	top_y = 0;
	for (int z = 0; z < WZ; z++)
		for (int x = 0; x < WX; x++) column_height(x, z);
}

static void set(int x, int y, int z, int b) {
	if ((unsigned)x >= WX || (unsigned)y >= WY || (unsigned)z >= WZ) return;
	sv.blocks[IDX(x, y, z)] = (uint8_t)b;
	column_height(x, z);
}

static void place_tree(int x, int y, int z, uint32_t seed) {
	int th = 4 + (int)(hash3(x, z, seed + 77) % 3);
	for (int dy = -2; dy <= 1; dy++) {
		int r = dy >= 0 ? 1 : 2;
		for (int dz = -r; dz <= r; dz++)
			for (int dx = -r; dx <= r; dx++) {
				if (r == 2 && (dx == -2 || dx == 2) && (dz == -2 || dz == 2) && (hash3(x + dx, z + dz, seed + dy) & 1)) continue;
				int yy = y + th + dy;
				if ((unsigned)(x + dx) < WX && (unsigned)(z + dz) < WZ && (unsigned)yy < WY && sv.blocks[IDX(x + dx, yy, z + dz)] == AIR)
					sv.blocks[IDX(x + dx, yy, z + dz)] = LEAVES;
			}
	}
	for (int i = 0; i < th && y + i < WY; i++) sv.blocks[IDX(x, y + i, z)] = LOG;
}

static void generate(uint32_t seed) {
	memset(&sv, 0, sizeof sv);
	sv.magic = SAVE_MAGIC;
	sv.version = SAVE_VERSION;
	sv.seed = seed;
	for (int z = 0; z < WZ; z++)
		for (int x = 0; x < WX; x++) {
			float n = fbm(x / 40.0f, z / 40.0f, seed, 4);
			float m = fbm(x / 90.0f + 31.7f, z / 90.0f, seed + 101, 3);
			float edge = fmin_(fmin_((float)x, (float)(WX - 1 - x)), fmin_((float)z, (float)(WZ - 1 - z)));
			int h = (int)(10 + n * 20 + fmax_(0, m - 0.52f) * 70);
			if (edge < 10) h = (int)(h * (0.55f + 0.045f * edge)); /* island-ish shore near the edges */
			if (h < 4) h = 4;
			if (h > WY - 8) h = WY - 8;
			for (int y = 0; y < h; y++) {
				int b = STONE;
				if (y == 0) b = BEDROCK;
				else if (y >= h - 4) b = DIRT;
				if (y == h - 1) {
					if (h - 1 <= SEA + 1) b = SAND;
					else if (h > 41) b = SNOW;
					else b = GRASS;
				} else if (y >= h - 4 && h - 1 <= SEA + 1) {
					b = SAND;
				}
				if (h > 41 && y < h - 1 && y >= h - 3) b = STONE;
				sv.blocks[IDX(x, y, z)] = (uint8_t)b;
			}
			for (int y = h; y <= SEA; y++) sv.blocks[IDX(x, y, z)] = WATER;
		}
	for (int z = 3; z < WZ - 3; z++)
		for (int x = 3; x < WX - 3; x++) {
			if (rnd01(x, z, seed + 5) > 0.014f) continue;
			int y = WY - 1;
			while (y > 0 && sv.blocks[IDX(x, y, z)] == AIR) y--;
			if (sv.blocks[IDX(x, y, z)] == GRASS && y + 9 < WY) place_tree(x, y + 1, z, seed);
		}
	all_heights();
	/* spawn on dry land near the middle */
	int bx = WX / 2, bz = WZ / 2;
	for (int r = 0; r < 60; r++) {
		int found = 0;
		for (int dz = -r; dz <= r && !found; dz++)
			for (int dx = -r; dx <= r && !found; dx++) {
				int x = WX / 2 + dx, z = WZ / 2 + dz;
				if ((unsigned)x >= WX || (unsigned)z >= WZ) continue;
				int y = hm[z * WX + x] - 1;
				if (y > SEA && sv.blocks[IDX(x, y, z)] == GRASS) { bx = x; bz = z; found = 1; }
			}
		if (found) break;
	}
	sv.px = bx + 0.5f;
	sv.pz = bz + 0.5f;
	sv.py = (float)hm[bz * WX + bx] + 0.01f;
	sv.yaw = 0.6f;
	sv.pitch = -0.15f;
	sv.time = 0;
	sv.detail = 0;
}

/* --------------------------------------------------------------- textures */

static uint32_t tex[NBLOCK][3][256]; /* top, side, bottom; 0 = transparent, else 0xFFRRGGBB */

static uint32_t rgb(int r, int g, int b) {
	r = r < 0 ? 0 : r > 255 ? 255 : r;
	g = g < 0 ? 0 : g > 255 ? 255 : g;
	b = b < 0 ? 0 : b > 255 ? 255 : b;
	return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static int jitter(int x, int y, int salt, int amt) { return (int)(hash3(x, y, 0xB10C + (uint32_t)salt) % (uint32_t)(amt * 2 + 1)) - amt; }

static void make_textures(void) {
	for (int f = 0; f < 3; f++)
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				int i = y * 16 + x; /* y = 0 is the top row of the texture */
				int j = jitter(x, y, f, 14), k = jitter(x, y, 40 + f, 10);
				uint32_t dirt = rgb(134 + j, 96 + j, 67 + j / 2);
				uint32_t grass = rgb(95 + k, 159 + k, 53 + k / 2);
				tex[DIRT][f][i] = dirt;
				tex[GRASS][f][i] = f == 0 ? grass : f == 2 ? dirt : (y < 3 + (int)(hash3(x, 0, 9) % 3) ? grass : dirt);
				int s = 125 + jitter(x, y, 3, 16);
				if ((hash3(x, y, 4) & 15) == 0) s -= 35;
				tex[STONE][f][i] = rgb(s, s, s + 4);
				/* cobblestone: irregular cells with dark mortar */
				{
					int cx = (x + (y / 4 % 2) * 2) / 4, cy = y / 4;
					int edge = ((x + (y / 4 % 2) * 2) % 4 == 0) || (y % 4 == 0);
					int c = 112 + (int)(hash3(cx, cy, 5) % 40) + jitter(x, y, 6, 6);
					tex[COBBLE][f][i] = edge ? rgb(70, 70, 72) : rgb(c, c, c);
				}
				tex[SAND][f][i] = rgb(219 + j / 2, 206 + j / 2, 160 + j / 2);
				/* log: bark on the sides, rings on top and bottom */
				if (f == 1) {
					int bark = 92 + jitter(x, 0, 7, 10) + ((x % 4 == 0) ? -18 : 0) + jitter(x, y, 8, 5);
					tex[LOG][f][i] = rgb(bark, bark * 70 / 100, bark * 42 / 100);
				} else {
					int dx = x - 8, dy = y - 8;
					int ring = (int)sqrt_((float)(dx * dx + dy * dy)) % 3;
					int v = ring == 0 ? 150 : 176;
					tex[LOG][f][i] = (dx * dx + dy * dy > 56) ? rgb(98, 70, 40) : rgb(v + j / 3, v * 80 / 100 + j / 3, v * 52 / 100);
				}
				/* leaves with a few see-through holes */
				tex[LEAVES][f][i] = (hash3(x, y, 9 + f) % 100) < 22 ? 0 : rgb(58 + k, 125 + k, 40 + k / 2);
				/* planks: boards with seams */
				{
					int board = y / 4;
					int seam = (y % 4 == 3) || ((x + board * 5) % 16 == 0);
					int v = 176 + jitter(x / 3, y, 10, 8) + (int)(hash3(board, 0, 11) % 14);
					tex[PLANKS][f][i] = seam ? rgb(120, 88, 50) : rgb(v, v * 76 / 100, v * 46 / 100);
				}
				/* brick */
				{
					int row = y / 4;
					int off = (row % 2) * 4;
					int mortar = (y % 4 == 3) || ((x + off) % 8 == 7);
					int v = 160 + jitter(x, y, 12, 12);
					tex[BRICK][f][i] = mortar ? rgb(196, 190, 180) : rgb(v, v * 52 / 100, v * 42 / 100);
				}
				uint32_t snow = rgb(238 + j / 3, 244 + j / 3, 250);
				tex[SNOW][f][i] = f == 0 ? snow : f == 2 ? dirt : (y < 4 + (int)(hash3(x, 1, 13) % 2) ? snow : dirt);
				tex[WATER][f][i] = rgb(40 + k, 92 + k, 196 + k);
				/* glass: light frame, clear middle with a glint */
				{
					int frame = x == 0 || y == 0 || x == 15 || y == 15;
					int glint = (x - y == 4 || x - y == 5) && x > 4 && x < 12;
					tex[GLASS][f][i] = frame ? rgb(206, 232, 238) : glint ? rgb(240, 250, 252) : 0;
				}
				int b = 52 + jitter(x, y, 14, 14);
				tex[BEDROCK][f][i] = rgb(b, b, b);
			}
}

/* --------------------------------------------------------------- renderer */

#define FB_W 320
#define FB_H 240
static uint16_t fb[FB_W * FB_H];
/* Auto quality ladder: 0 = 320x240 every frame, 1 = 160x240 every frame,
 * 2 = 160x240 every other frame, 3 = 160x120 every other frame. */
static int auto_step = 1;
static const uint8_t STEP_LEVEL[4] = {0, 1, 1, 2};
static const uint8_t STEP_HALF[4] = {0, 0, 1, 1};

typedef struct { float r, g, b; } Col;

static float daylight(void) {
	static const float L[4] = {1.0f, 0.78f, 0.38f, 0.92f}; /* day, sunset, night, dawn */
	return L[sv.time & 3];
}
static Col sky_col(float dy) {
	static const Col zen[4] = {{0.34f, 0.56f, 0.94f}, {0.30f, 0.32f, 0.62f}, {0.03f, 0.04f, 0.11f}, {0.42f, 0.55f, 0.85f}};
	static const Col hor[4] = {{0.70f, 0.84f, 0.98f}, {0.98f, 0.62f, 0.40f}, {0.10f, 0.12f, 0.22f}, {0.98f, 0.80f, 0.70f}};
	float t = clampf(dy * 1.6f, 0, 1);
	Col z = zen[sv.time & 3], h = hor[sv.time & 3];
	Col c = {h.r + (z.r - h.r) * t, h.g + (z.g - h.g) * t, h.b + (z.b - h.b) * t};
	return c;
}

static float max_dist = 40.0f;
static int target_ok, tx_, ty_, tz_, px_, py_, pz_;

static uint16_t pack(Col c) {
	int r = (int)(clampf(c.r, 0, 1) * 31 + 0.5f), g = (int)(clampf(c.g, 0, 1) * 63 + 0.5f), b = (int)(clampf(c.b, 0, 1) * 31 + 0.5f);
	return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t cast(float ox, float oy, float oz, float dx, float dy, float dz) {
	int x = ifloor(ox), y = ifloor(oy), z = ifloor(oz);
	int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
	float tdx = dx != 0 ? fabs_(1.0f / dx) : 1e30f;
	float tdy = dy != 0 ? fabs_(1.0f / dy) : 1e30f;
	float tdz = dz != 0 ? fabs_(1.0f / dz) : 1e30f;
	float tmx = (sx > 0 ? (x + 1 - ox) : (ox - x)) * tdx;
	float tmy = (sy > 0 ? (y + 1 - oy) : (oy - y)) * tdy;
	float tmz = (sz > 0 ? (z + 1 - oz) : (oz - z)) * tdz;
	float t = 0, water_t = -1;
	int face = 1;
	const float maxd = max_dist;
	for (;;) {
		if (tmx < tmy) {
			if (tmx < tmz) { x += sx; t = tmx; tmx += tdx; face = 0; }
			else { z += sz; t = tmz; tmz += tdz; face = 2; }
		} else {
			if (tmy < tmz) { y += sy; t = tmy; tmy += tdy; face = 1; }
			else { z += sz; t = tmz; tmz += tdz; face = 2; }
		}
		if (t > maxd || (unsigned)x >= WX || (unsigned)z >= WZ) break;
		if (y >= top_y && sy > 0) break; /* climbing above every block: it's sky */
		if ((unsigned)y >= WY) {
			if (y >= WY && sy > 0) break; /* above the world: only sky from here */
			if (y < 0) break;
			continue;
		}
		int b = sv.blocks[IDX(x, y, z)];
		if (b == AIR) continue;
		if (b == WATER) {
			if (water_t < 0) water_t = t;
			continue;
		}
		float hx = ox + dx * t, hy = oy + dy * t, hz = oz + dz * t;
		float u, v;
		int kind;
		if (face == 1) { kind = sy > 0 ? 2 : 0; u = hx - ifloor(hx); v = hz - ifloor(hz); }
		else if (face == 0) { kind = 1; u = hz - ifloor(hz); v = 1 - (hy - ifloor(hy)); if (sx < 0) u = 1 - u; }
		else { kind = 1; u = hx - ifloor(hx); v = 1 - (hy - ifloor(hy)); if (sz > 0) u = 1 - u; }
		int tu = (int)(u * 16), tv = (int)(v * 16);
		uint32_t tx = tex[b][kind][(tv & 15) * 16 + (tu & 15)];
		if (!tx) continue; /* see-through texel (leaves, glass) */
		if (target_ok && x == tx_ && y == ty_ && z == tz_) {
			/* outline the block you're pointing at; thinner when it's close */
			float e = clampf(0.011f * t, 0.012f, 0.06f);
			if (u < e || u > 1 - e || v < e || v > 1 - e) tx = 0xFF1A1A1Au;
		}
		float shade = face == 1 ? (sy > 0 ? 0.52f : 1.0f) : (face == 0 ? 0.80f : 0.66f);
		/* sky light: the cell in front of this face — is it under something? */
		int fx = x, fy = y, fz = z;
		if (face == 0) fx -= sx; else if (face == 1) fy -= sy; else fz -= sz;
		if ((unsigned)fx < WX && (unsigned)fz < WZ && fy < hm[fz * WX + fx]) shade *= 0.62f;
		shade *= daylight();
		Col c = {((tx >> 16) & 255) / 255.0f * shade, ((tx >> 8) & 255) / 255.0f * shade, (tx & 255) / 255.0f * shade};
		if (water_t >= 0) {
			float w = clampf(0.35f + (t - water_t) * 0.22f, 0, 0.9f);
			float dl = daylight();
			c.r += (0.10f * dl - c.r) * w;
			c.g += (0.28f * dl - c.g) * w;
			c.b += (0.60f * dl - c.b) * w;
		}
		float fog = clampf((t - maxd * 0.55f) / (maxd * 0.45f), 0, 1);
		if (fog > 0) {
			Col s = sky_col(0);
			c.r += (s.r - c.r) * fog;
			c.g += (s.g - c.g) * fog;
			c.b += (s.b - c.b) * fog;
		}
		return pack(c);
	}
	Col s = sky_col(dy);
	if (water_t >= 0) {
		float dl = daylight();
		s.r = 0.12f * dl; s.g = 0.30f * dl; s.b = 0.58f * dl;
	}
	return pack(s);
}

static void render(void) {
	int level = sv.detail ? sv.detail - 1 : STEP_LEVEL[auto_step];
	int cw = level == 0 ? FB_W : FB_W / 2;
	int ch = level == 2 ? FB_H / 2 : FB_H;
	max_dist = level == 0 ? 44.0f : level == 1 ? 40.0f : 34.0f;
	float cy = cos_(sv.yaw), syw = sin_(sv.yaw), cp = cos_(sv.pitch), sp = sin_(sv.pitch);
	float fx = syw * cp, fy = sp, fz = cy * cp;     /* forward */
	float rx = cy, ry = 0, rz = -syw;               /* right */
	float ux = -syw * sp, uy = cp, uz = -cy * sp;   /* up */
	const float th = 0.70f, tw = 0.70f * 4.0f / 3.0f;
	float ox = sv.px, oy = sv.py + 1.62f, oz = sv.pz;
	for (int j = 0; j < ch; j++) {
		float v = (1 - 2 * (j + 0.5f) / ch) * th;
		float bx = fx + ux * v, by = fy + uy * v, bz = fz + uz * v;
		uint16_t *row = fb + (level == 2 ? j * 2 : j) * FB_W;
		for (int i = 0; i < cw; i++) {
			float u = (2 * (i + 0.5f) / cw - 1) * tw;
			float dx = bx + rx * u, dy = by + ry * u, dz = bz + rz * u;
			float inv = 1.0f / sqrt_(dx * dx + dy * dy + dz * dz);
			uint16_t c = cast(ox, oy, oz, dx * inv, dy * inv, dz * inv);
			if (cw == FB_W) row[i] = c;
			else { row[i * 2] = c; row[i * 2 + 1] = c; }
		}
		if (level == 2) memcpy(row + FB_W, row, FB_W * 2);
	}
}

/* ------------------------------------------------------------------- HUD */

static void hud_rect(int x, int y, int w, int h, uint16_t c) {
	for (int j = y; j < y + h; j++) {
		if ((unsigned)j >= FB_H) continue;
		for (int i = x; i < x + w; i++)
			if ((unsigned)i < FB_W) fb[j * FB_W + i] = c;
	}
}
static void hud_dim(int x, int y, int w, int h) {
	for (int j = y; j < y + h; j++) {
		if ((unsigned)j >= FB_H) continue;
		for (int i = x; i < x + w; i++)
			if ((unsigned)i < FB_W) fb[j * FB_W + i] = (uint16_t)((fb[j * FB_W + i] >> 1) & 0x7BEF);
	}
}
static int hud_text(int x, int y, const char *s, uint16_t c) {
	for (; *s; s++) {
		int ch = (unsigned char)*s;
		if (ch < 32 || ch > 126) ch = '?';
		const unsigned char *g = FONT[ch - 32];
		for (int r = 0; r < FONT_H; r++)
			for (int b = 0; b < FONT_W; b++)
				if ((g[r] >> b) & 1) {
					int px = x + b, py = y + r;
					if ((unsigned)px < FB_W && (unsigned)py < FB_H) {
						if ((unsigned)(px + 1) < FB_W && (unsigned)(py + 1) < FB_H) fb[(py + 1) * FB_W + px + 1] = 0;
						fb[py * FB_W + px] = c;
					}
				}
		x += FONT_W;
	}
	return x;
}
static int text_w(const char *s) { int n = 0; while (s[n]) n++; return n * FONT_W; }
static void hud_text_c(int cx, int y, const char *s, uint16_t c) { hud_text(cx - text_w(s) / 2, y, s, c); }

static void itoa_(int v, char *out) {
	char tmp[12];
	int n = 0, neg = v < 0;
	unsigned u = neg ? (unsigned)-v : (unsigned)v;
	do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
	int o = 0;
	if (neg) out[o++] = '-';
	while (n) out[o++] = tmp[--n];
	out[o] = 0;
}
static char *cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; return d; }

static void block_icon(int x, int y, int b) {
	/* a tiny isometric-ish cube: top on the upper half, side below */
	for (int j = 0; j < 16; j++)
		for (int i = 0; i < 16; i++) {
			int kind = j < 6 ? 0 : 1;
			uint32_t t = tex[b][kind][(j < 6 ? j * 2 + 2 : j) * 16 + i];
			if (!t) continue;
			float s = kind ? 0.78f : 1.0f;
			Col c = {((t >> 16) & 255) / 255.0f * s, ((t >> 8) & 255) / 255.0f * s, (t & 255) / 255.0f * s};
			fb[(y + j) * FB_W + x + i] = pack(c);
		}
}

static const char *msg_text;
static int msg_frames;
static int menu_open, menu_sel, confirm_new;
static int show_coords;

static void draw_hud(void) {
	/* crosshair */
	int cx = FB_W / 2, cy = FB_H / 2;
	for (int i = -4; i <= 4; i++) {
		if (i > -2 && i < 2) continue;
		fb[cy * FB_W + cx + i] = (uint16_t)~fb[cy * FB_W + cx + i];
		fb[(cy + i) * FB_W + cx] = (uint16_t)~fb[(cy + i) * FB_W + cx];
	}
	/* hotbar */
	int sw = 22, total = NSLOT * sw;
	int x0 = (FB_W - total) / 2, y0 = FB_H - 26;
	hud_dim(x0 - 2, y0 - 2, total + 4, sw + 4);
	for (int s = 0; s < NSLOT; s++) {
		int x = x0 + s * sw;
		if (s == sv.slot) {
			hud_rect(x, y0, sw, 2, 0xFFFF); hud_rect(x, y0 + sw - 2, sw, 2, 0xFFFF);
			hud_rect(x, y0, 2, sw, 0xFFFF); hud_rect(x + sw - 2, y0, 2, sw, 0xFFFF);
		}
		block_icon(x + 3, y0 + 3, HOTBAR[s]);
	}
	hud_text_c(FB_W / 2, y0 - 13, BLOCK_NAMES[HOTBAR[sv.slot]], 0xFFFF);
	if (sv.fly) hud_text(6, 6, "FLYING", 0xFFE0);
	if (show_coords) {
		char b[64] = "XYZ ", n[12];
		itoa_((int)sv.px, n); cat(b, n); cat(b, " ");
		itoa_((int)sv.py, n); cat(b, n); cat(b, " ");
		itoa_((int)sv.pz, n); cat(b, n);
		hud_text(FB_W - text_w(b) - 6, 6, b, 0xFFFF);
	}
	if (msg_frames > 0 && msg_text) {
		hud_dim(FB_W / 2 - text_w(msg_text) / 2 - 6, 40, text_w(msg_text) + 12, 16);
		hud_text_c(FB_W / 2, 43, msg_text, 0xFFFF);
	}
}

static const char *MENU_ITEMS[] = {"Resume", "Flying", "Detail", "Time of day", "Coordinates", "Sensitivity", "Controls", "New world"};
#define NMENU 8
static int show_help, show_sens, sens_sel;

/* Sensitivity levels 1..10; 0 in an older save means the default, 5 */
static int lvl(uint8_t v) { return v ? (v > 10 ? 10 : v) : 5; }
static float sens_factor(uint8_t v) { return lvl(v) / 5.0f; }               /* 0.2x .. 2x */
static float move_factor(uint8_t v) { return 0.5f + lvl(v) * 0.1f; }        /* 0.6x .. 1.5x */
#define NSENS 5
static const char *SENS_ITEMS[NSENS] = {"Turn speed", "Look speed", "Move speed", "Invert look", "Back"};

static void draw_bar(int x, int y, int level, uint16_t c) {
	for (int i = 1; i <= 10; i++) {
		int h = 3 + i;
		if (i <= level) hud_rect(x + (i - 1) * 6, y + 10 - h, 4, h, c);
		else hud_rect(x + (i - 1) * 6, y + 9, 4, 1, 0x8410);
	}
}

static void draw_menu(void) {
	hud_dim(0, 0, FB_W, FB_H);
	hud_dim(40, 24, FB_W - 80, FB_H - 48);
	hud_text_c(FB_W / 2, 32, "BLOCKBOX", 0xFFE0);
	if (show_sens) {
		hud_text_c(FB_W / 2, 46, "SENSITIVITY", 0xFFFF);
		for (int i = 0; i < NSENS; i++) {
			int y = 70 + i * 22;
			uint16_t c = i == sens_sel ? 0xFFE0 : 0xFFFF;
			if (i == sens_sel) hud_text(60, y, ">", c);
			hud_text(72, y, SENS_ITEMS[i], c);
			uint8_t v = i == 0 ? sv.sens_turn : i == 1 ? sv.sens_look : sv.sens_move;
			if (i < 3) {
				char n[8];
				itoa_(lvl(v), n);
				draw_bar(FB_W - 140, y - 1, lvl(v), c);
				hud_text(FB_W - 70, y, n, c);
			}
			if (i == 3) hud_text(FB_W - 64 - text_w(sv.invert_look ? "On" : "Off"), y, sv.invert_look ? "On" : "Off", c);
		}
		hud_text_c(FB_W / 2, FB_H - 40, "LEFT/RIGHT: change   B: back", 0xBDF7);
		return;
	}
	if (show_help) {
		static const char *H[] = {
			"D-pad up/down   walk", "D-pad left/right turn", "L1 / R1   step sideways", "L2 / R2   look up / down",
			"B   jump (fly up)", "SELECT   fly down", "Y   break block", "A   place block", "X   next block", "START   this menu",
			"Sticks also move and look.",
		};
		for (int i = 0; i < 11; i++) hud_text(56, 52 + i * 13, H[i], 0xFFFF);
		hud_text_c(FB_W / 2, FB_H - 40, "B: back", 0xBDF7);
		return;
	}
	static const char *DET[] = {"Auto", "High", "Medium", "Low"};
	static const char *TOD[] = {"Day", "Sunset", "Night", "Dawn"};
	for (int i = 0; i < NMENU; i++) {
		int y = 56 + i * 18;
		char line[48] = "";
		cat(line, MENU_ITEMS[i]);
		const char *val = "";
		if (i == 1) val = sv.fly ? "On" : "Off";
		if (i == 2) val = DET[sv.detail & 3];
		if (i == 3) val = TOD[sv.time & 3];
		if (i == 4) val = show_coords ? "On" : "Off";
		if (i == 7 && confirm_new) val = "Press A again";
		uint16_t c = i == menu_sel ? 0xFFE0 : 0xFFFF;
		if (i == menu_sel) hud_text(60, y, ">", c);
		hud_text(72, y, line, c);
		if (*val) hud_text(FB_W - 64 - text_w(val), y, val, c);
	}
	hud_text_c(FB_W / 2, FB_H - 40, "A: change   START/B: close", 0xBDF7);
}

/* --------------------------------------------------------------- gameplay */

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;
static struct retro_perf_callback perf;
static int have_perf;
static bool can_dupe;

static float vy;
static int on_ground;
static int prev_btn[16];
static int break_cool, place_cool;

static int collides(float x, float y, float z) {
	const float r = 0.3f, hgt = 1.8f;
	int x0 = ifloor(x - r), x1 = ifloor(x + r), z0 = ifloor(z - r), z1 = ifloor(z + r);
	int y0 = ifloor(y), y1 = ifloor(y + hgt - 0.001f);
	for (int yy = y0; yy <= y1; yy++)
		for (int zz = z0; zz <= z1; zz++)
			for (int xx = x0; xx <= x1; xx++)
				if (solid(get(xx, yy, zz))) return 1;
	return 0;
}

static void move_axis(float *p, float d, int axis) {
	if (d == 0) return;
	float step = d;
	/* split long moves so we never tunnel through a block */
	int n = (int)(fabs_(d) / 0.4f) + 1;
	step = d / n;
	for (int i = 0; i < n; i++) {
		float old = *p;
		*p += step;
		if (collides(sv.px, sv.py, sv.pz)) {
			*p = old;
			if (axis == 1) {
				if (step < 0) on_ground = 1;
				vy = 0;
			}
			return;
		}
	}
}

static void raycast_target(void) {
	float cy = cos_(sv.yaw), syw = sin_(sv.yaw), cp = cos_(sv.pitch), sp = sin_(sv.pitch);
	float dx = syw * cp, dy = sp, dz = cy * cp;
	float ox = sv.px, oy = sv.py + 1.62f, oz = sv.pz;
	int x = ifloor(ox), y = ifloor(oy), z = ifloor(oz);
	int lx = x, ly = y, lz = z;
	int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
	float tdx = dx != 0 ? fabs_(1 / dx) : 1e30f, tdy = dy != 0 ? fabs_(1 / dy) : 1e30f, tdz = dz != 0 ? fabs_(1 / dz) : 1e30f;
	float tmx = (sx > 0 ? (x + 1 - ox) : (ox - x)) * tdx, tmy = (sy > 0 ? (y + 1 - oy) : (oy - y)) * tdy, tmz = (sz > 0 ? (z + 1 - oz) : (oz - z)) * tdz;
	target_ok = 0;
	for (int i = 0; i < 32; i++) {
		lx = x; ly = y; lz = z;
		float t;
		if (tmx < tmy && tmx < tmz) { x += sx; t = tmx; tmx += tdx; }
		else if (tmy < tmz) { y += sy; t = tmy; tmy += tdy; }
		else { z += sz; t = tmz; tmz += tdz; }
		if (t > 5.0f) return;
		int b = get(x, y, z);
		if (b != AIR && b != WATER) {
			target_ok = 1;
			tx_ = x; ty_ = y; tz_ = z;
			px_ = lx; py_ = ly; pz_ = lz;
			return;
		}
	}
}

/* little click sound for breaking / placing */
static int16_t audio_buf[735 * 2 * 2];
static int snd_left, snd_kind;
static uint32_t snd_seed = 1;
static void sound(int kind) { snd_left = 1600; snd_kind = kind; }
static void audio_frame(void) {
	int n = 735; /* 44100 / 60 */
	for (int i = 0; i < n; i++) {
		int16_t s = 0;
		if (snd_left > 0) {
			snd_seed = snd_seed * 1664525u + 1013904223u;
			int noise = (int)(snd_seed >> 20) - 2048;
			int env = snd_left;
			s = (int16_t)(noise * env / (snd_kind ? 900 : 500));
			if (snd_kind) s = (int16_t)((i / 18 % 2 ? 1 : -1) * env * 2 + s / 3);
			snd_left--;
		}
		audio_buf[i * 2] = s;
		audio_buf[i * 2 + 1] = s;
	}
	audio_batch_cb(audio_buf, (size_t)n);
}

static void message(const char *m) { msg_text = m; msg_frames = 90; }

static int pressed(int id) { return input_cb(0, RETRO_DEVICE_JOYPAD, 0, (unsigned)id) != 0; }
static int edge(int id) {
	int p = pressed(id), e = p && !prev_btn[id];
	return e;
}
static float axis(int idx, int id) {
	int v = input_cb(0, RETRO_DEVICE_ANALOG, (unsigned)idx, (unsigned)id);
	float f = v / 32768.0f;
	return fabs_(f) < 0.18f ? 0 : f;
}

static void menu_input(void) {
	if (edge(RETRO_DEVICE_ID_JOYPAD_START) || (edge(RETRO_DEVICE_ID_JOYPAD_B) && !show_help && !show_sens)) { menu_open = 0; confirm_new = 0; return; }
	if (show_help) { if (edge(RETRO_DEVICE_ID_JOYPAD_B) || edge(RETRO_DEVICE_ID_JOYPAD_A)) show_help = 0; return; }
	if (show_sens) {
		if (edge(RETRO_DEVICE_ID_JOYPAD_B)) { show_sens = 0; return; }
		if (edge(RETRO_DEVICE_ID_JOYPAD_UP)) sens_sel = (sens_sel + NSENS - 1) % NSENS;
		if (edge(RETRO_DEVICE_ID_JOYPAD_DOWN)) sens_sel = (sens_sel + 1) % NSENS;
		int r = edge(RETRO_DEVICE_ID_JOYPAD_RIGHT), lf = edge(RETRO_DEVICE_ID_JOYPAD_LEFT), ok = edge(RETRO_DEVICE_ID_JOYPAD_A);
		if (sens_sel == 4) { if (ok) show_sens = 0; return; }
		if (sens_sel == 3) { if (r || lf || ok) sv.invert_look ^= 1; return; }
		int d = r || ok ? 1 : lf ? -1 : 0;
		if (!d) return;
		uint8_t *p = sens_sel == 0 ? &sv.sens_turn : sens_sel == 1 ? &sv.sens_look : &sv.sens_move;
		int v = lvl(*p) + d;
		if (ok && v > 10) v = 1; /* A cycles round */
		*p = (uint8_t)(v < 1 ? 1 : v > 10 ? 10 : v);
		return;
	}
	if (edge(RETRO_DEVICE_ID_JOYPAD_UP)) { menu_sel = (menu_sel + NMENU - 1) % NMENU; confirm_new = 0; }
	if (edge(RETRO_DEVICE_ID_JOYPAD_DOWN)) { menu_sel = (menu_sel + 1) % NMENU; confirm_new = 0; }
	int a = edge(RETRO_DEVICE_ID_JOYPAD_A) || edge(RETRO_DEVICE_ID_JOYPAD_RIGHT);
	int l = edge(RETRO_DEVICE_ID_JOYPAD_LEFT);
	if (!a && !l) return;
	int d = l ? -1 : 1;
	switch (menu_sel) {
	case 0: menu_open = 0; break;
	case 1: sv.fly ^= 1; vy = 0; break;
	case 2: sv.detail = (uint8_t)((sv.detail + 4 + d) % 4); break;
	case 3: sv.time = (uint8_t)((sv.time + 4 + d) % 4); break;
	case 4: show_coords ^= 1; break;
	case 5: show_sens = 1; sens_sel = 0; break;
	case 6: show_help = 1; break;
	case 7:
		if (!confirm_new) { confirm_new = 1; break; }
		generate(sv.seed * 2654435761u + 12345u);
		confirm_new = 0;
		menu_open = 0;
		message("A brand new world");
		break;
	}
}

static void game_input(float dt) {
	float turn = 0, look = 0, fwd = 0, side = 0;
	if (pressed(RETRO_DEVICE_ID_JOYPAD_LEFT)) turn -= 1;
	if (pressed(RETRO_DEVICE_ID_JOYPAD_RIGHT)) turn += 1;
	if (pressed(RETRO_DEVICE_ID_JOYPAD_UP)) fwd += 1;
	if (pressed(RETRO_DEVICE_ID_JOYPAD_DOWN)) fwd -= 1;
	if (pressed(RETRO_DEVICE_ID_JOYPAD_L)) side -= 1;
	if (pressed(RETRO_DEVICE_ID_JOYPAD_R)) side += 1;
	if (pressed(RETRO_DEVICE_ID_JOYPAD_L2)) look += 1;
	if (pressed(RETRO_DEVICE_ID_JOYPAD_R2)) look -= 1;
	fwd -= axis(RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
	side += axis(RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
	turn += axis(RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) * 1.2f;
	look -= axis(RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
	if (sv.invert_look) look = -look;
	sv.yaw += turn * 2.4f * sens_factor(sv.sens_turn) * dt;
	sv.pitch = clampf(sv.pitch + look * 1.8f * sens_factor(sv.sens_look) * dt, -1.45f, 1.45f);

	float speed = (sv.fly ? 8.0f : 4.3f) * move_factor(sv.sens_move);
	float s = sin_(sv.yaw), c = cos_(sv.yaw);
	float mx = (s * fwd + c * side) * speed * dt;
	float mz = (c * fwd - s * side) * speed * dt;
	int in_water = get(ifloor(sv.px), ifloor(sv.py + 0.4f), ifloor(sv.pz)) == WATER;
	if (in_water && !sv.fly) { mx *= 0.6f; mz *= 0.6f; }
	move_axis(&sv.px, mx, 0);
	move_axis(&sv.pz, mz, 2);

	if (sv.fly) {
		float up = 0;
		if (pressed(RETRO_DEVICE_ID_JOYPAD_B)) up += 1;
		if (pressed(RETRO_DEVICE_ID_JOYPAD_SELECT)) up -= 1;
		vy = up * 7.0f;
		on_ground = 0;
		move_axis(&sv.py, vy * dt, 1);
	} else {
		if (in_water) {
			vy -= 9.0f * dt;
			if (vy < -3.0f) vy = -3.0f;
			if (pressed(RETRO_DEVICE_ID_JOYPAD_B)) vy = 3.4f;
		} else {
			vy -= 26.0f * dt;
			if (vy < -40) vy = -40;
			if (on_ground && pressed(RETRO_DEVICE_ID_JOYPAD_B)) vy = 8.2f;
		}
		on_ground = 0;
		move_axis(&sv.py, vy * dt, 1);
	}
	if (sv.py < -10) { /* fell out of the world */
		int x = ifloor(sv.px), z = ifloor(sv.pz);
		if ((unsigned)x >= WX) x = WX / 2;
		if ((unsigned)z >= WZ) z = WZ / 2;
		sv.px = x + 0.5f; sv.pz = z + 0.5f;
		sv.py = hm[z * WX + x] + 1.0f;
		vy = 0;
	}

	if (edge(RETRO_DEVICE_ID_JOYPAD_X)) sv.slot = (uint8_t)((sv.slot + 1) % NSLOT);
	if (break_cool) break_cool--;
	if (place_cool) place_cool--;
	raycast_target();
	if (pressed(RETRO_DEVICE_ID_JOYPAD_Y) && !break_cool && target_ok) {
		if (get(tx_, ty_, tz_) != BEDROCK) {
			set(tx_, ty_, tz_, ty_ <= SEA && (get(tx_ + 1, ty_, tz_) == WATER || get(tx_ - 1, ty_, tz_) == WATER ||
			                                    get(tx_, ty_ + 1, tz_) == WATER || get(tx_, ty_, tz_ + 1) == WATER ||
			                                    get(tx_, ty_, tz_ - 1) == WATER) ? WATER : AIR);
			sound(0);
		}
		break_cool = 12;
	}
	if (pressed(RETRO_DEVICE_ID_JOYPAD_A) && !place_cool && target_ok) {
		int b = get(px_, py_, pz_);
		if (b == AIR || b == WATER) {
			set(px_, py_, pz_, HOTBAR[sv.slot]);
			if (collides(sv.px, sv.py, sv.pz)) set(px_, py_, pz_, b);
			else sound(1);
		}
		place_cool = 12;
	}
	if (edge(RETRO_DEVICE_ID_JOYPAD_START)) { menu_open = 1; menu_sel = 0; show_help = 0; show_sens = 0; }
}

/* --------------------------------------------------------- libretro glue */

EXPORT void retro_set_environment(retro_environment_t cb) {
	env_cb = cb;
	bool no_game = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}
EXPORT void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
EXPORT void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
EXPORT void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
EXPORT void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
EXPORT void retro_set_input_state(retro_input_state_t cb) { input_cb = cb; }
EXPORT unsigned retro_api_version(void) { return RETRO_API_VERSION; }
EXPORT void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

EXPORT void retro_get_system_info(struct retro_system_info *info) {
	memset(info, 0, sizeof *info);
	info->library_name = "Blockbox";
	info->library_version = "1.0";
	info->valid_extensions = "world|txt";
	info->need_fullpath = true;
	info->block_extract = true;
}

EXPORT void retro_get_system_av_info(struct retro_system_av_info *info) {
	memset(info, 0, sizeof *info);
	info->geometry.base_width = FB_W;
	info->geometry.base_height = FB_H;
	info->geometry.max_width = FB_W;
	info->geometry.max_height = FB_H;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps = 60.0;
	info->timing.sample_rate = 44100.0;
}

EXPORT void retro_init(void) { make_textures(); }
EXPORT void retro_deinit(void) {}
EXPORT void retro_reset(void) {
	sv.px = WX / 2 + 0.5f;
	sv.pz = WZ / 2 + 0.5f;
	sv.py = hm[(WZ / 2) * WX + WX / 2] + 1.0f;
	vy = 0;
}

static uint32_t seed_from(const char *s) {
	uint32_t h = 2166136261u;
	const char *base = s;
	for (const char *p = s; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
	for (; *base; base++) { h ^= (uint8_t)*base; h *= 16777619u; }
	return h ? h : 1;
}

EXPORT bool retro_load_game(const struct retro_game_info *game) {
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
	if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) return false;
	struct retro_input_descriptor desc[] = {
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Walk forward"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Walk back"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Turn left"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Turn right"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Step left"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Step right"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Look up"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Look down"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Jump / fly up"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Fly down"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Break block"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Place block"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Next block"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Menu"},
		{0, 0, 0, 0, NULL},
	};
	env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
	have_perf = env_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf) && perf.get_time_usec;
	if (!env_cb(RETRO_ENVIRONMENT_GET_CAN_DUPE, &can_dupe)) can_dupe = false;
	pending_seed = game && game->path ? seed_from(game->path) : 0x5EED1234u;
	generate(pending_seed);
	world_ready = 0; /* RetroArch may now copy the saved world into sv */
	message("START: menu & controls");
	return true;
}

EXPORT bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
	(void)type; (void)info; (void)num;
	return false;
}
EXPORT void retro_unload_game(void) {}
EXPORT unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

EXPORT void *retro_get_memory_data(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? &sv : NULL; }
EXPORT size_t retro_get_memory_size(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? sizeof sv : 0; }

EXPORT size_t retro_serialize_size(void) { return sizeof sv; }
EXPORT bool retro_serialize(void *data, size_t size) {
	if (size < sizeof sv) return false;
	memcpy(data, &sv, sizeof sv);
	return true;
}
EXPORT bool retro_unserialize(const void *data, size_t size) {
	if (size < sizeof sv) return false;
	const Save *s = data;
	if (s->magic != SAVE_MAGIC || s->version != SAVE_VERSION) return false;
	memcpy(&sv, data, sizeof sv);
	all_heights();
	vy = 0;
	return true;
}
EXPORT void retro_cheat_reset(void) {}
EXPORT void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

static int frame_no;
static int64_t slow_since;

EXPORT void retro_run(void) {
	if (!world_ready) {
		/* first frame: a loaded .srm has replaced sv by now, or we keep the fresh world */
		if (sv.magic != SAVE_MAGIC || sv.version != SAVE_VERSION) generate(pending_seed);
		all_heights();
		if (collides(sv.px, sv.py, sv.pz)) sv.py = (float)hm[ifloor(sv.pz) * WX + ifloor(sv.px)];
		world_ready = 1;
	}
	poll_cb();
	const float dt = 1.0f / 60.0f;
	if (menu_open) menu_input();
	else game_input(dt);
	for (int i = 0; i < 16; i++) prev_btn[i] = pressed(i);

	int half = sv.detail == 0 && STEP_HALF[auto_step];
	if (half && (frame_no & 1) && can_dupe && !menu_open) {
		/* half-rate rendering: the game still runs at 60, the picture updates at 30 */
		frame_no++;
		if (msg_frames > 0) msg_frames--;
		video_cb(NULL, FB_W, FB_H, FB_W * 2);
		audio_frame();
		return;
	}
	int64_t t0 = have_perf ? perf.get_time_usec() : 0;
	render();
	if (have_perf && sv.detail == 0) {
		/* Auto detail: step down when frames take too long, step up when there's headroom. */
		int64_t took = perf.get_time_usec() - t0;
		int64_t budget = STEP_HALF[auto_step] ? 26000 : 13000;
		if (took > budget) {
			if (!slow_since) slow_since = t0;
			if (t0 - slow_since > 500000 && auto_step < 3) { auto_step++; slow_since = 0; }
		} else {
			slow_since = 0;
			int64_t up_budget = STEP_HALF[auto_step - (auto_step > 0)] ? 15000 : 6500;
			if (auto_step > 0 && took < up_budget && (frame_no % 300) == 0) auto_step--;
		}
	}
	if (menu_open) draw_menu();
	else draw_hud();
	if (msg_frames > 0) msg_frames--;
	frame_no++;
	video_cb(fb, FB_W, FB_H, FB_W * 2);
	audio_frame();
}
