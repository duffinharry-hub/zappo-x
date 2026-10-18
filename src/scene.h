/* Zappo X — the moving backgrounds (M5): each section's element drifting
 * behind the menus, like the Zappo charging scene. Recent: Electric sparks,
 * Favorites: Crystal shards, Consoles: Fire embers, Music: Water drops,
 * Videos: Ice snow, Settings: Leaf leaves.
 *
 * This part only moves the particles and draws their little pictures (once);
 * the platform puts them on screen (on the handheld: with the GPU, between the
 * background picture and the menu). Coordinates are Zappo's 640 x 480. */
#ifndef ZAPPO_SCENE_H
#define ZAPPO_SCENE_H

#include <stdint.h>

enum { EL_ELECTRIC, EL_CRYSTAL, EL_FIRE, EL_WATER, EL_ICE, EL_LEAF, EL_DUST, EL_COUNT, EL_NONE = -1 };

/* the little pictures: straight-alpha 0xAARRGGBB */
enum { SP_GLOW, SP_BOLT, SP_SHARD, SP_EMBER, SP_DROP, SP_BUBBLE, SP_FLAKE, SP_LEAF, SP_STAR, SP_COUNT };

typedef struct {
	float x, y;       /* centre */
	float w, h;       /* size drawn */
	float angle;      /* degrees, clockwise */
	float alpha;      /* 0..1 */
	uint32_t tint;    /* 0xRRGGBB multiplied in (white = as drawn) */
	uint8_t sprite;   /* SP_* */
	uint8_t additive; /* glows: added to what's below */
} SceneQuad;

/* the element on screen (EL_NONE: nothing moving); density 2 = full, 1 = fewer.
 * Changing it fades the old particles out as the new ones come in. */
void scene_set(int element, int density);
int scene_element(void);
/* moves everything on by dt seconds and lists what to draw, back to front */
int scene_update(float dt, SceneQuad *out, int max);
/* the picture for SP_*, made the first time it's asked for */
const uint32_t *scene_sprite(int sprite, int *w, int *h);
/* draws the quads into an opaque 0xRRGGBB picture, without a GPU */
void scene_draw_soft(uint32_t *dst, int w, int h, const SceneQuad *q, int n);

#endif
