/* Clove — desktop stand-in for the hardware layer, used for automated tests.
 *
 * Environment:
 *   CLOVE_MMC     folder that plays the role of /mnt/mmc (TF1 ROMS partition)
 *   CLOVE_SD      folder that plays the role of /mnt/SDCARD (optional)
 *   CLOVE_SCRIPT  space-separated steps: button names (a, down, l1...),
 *                 "down:NAME" / "up:NAME" (hold and let go, for combos),
 *                 "wait:MS", "shot:FILE.png", "quit"
 * Time is virtual (16 ms per frame) so screenshots are deterministic. */
#include "plat.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x)
#include "../third_party/stb_image_write.h"

static uint32_t canvas[SCREEN_W * SCREEN_H];
static uint64_t vtime = 1000;
static char *script;
static char *cursor;
static uint64_t next_at;
static int pending_release = -1;
static int brightness = 7;
static int cpu_max = 1008000;
static char cpu_gov[32] = "interactive";
static int frames;
static int quit_sent;

uint32_t *plat_canvas(void) { return canvas; }
int plat_has_vsync(void) { return 1; }

static void save_png(const char *path) {
	uint8_t *rgb = xmalloc(SCREEN_W * SCREEN_H * 3);
	for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
		rgb[i * 3] = (canvas[i] >> 16) & 255;
		rgb[i * 3 + 1] = (canvas[i] >> 8) & 255;
		rgb[i * 3 + 2] = canvas[i] & 255;
	}
	stbi_write_png(path, SCREEN_W, SCREEN_H, 3, rgb, SCREEN_W * 3);
	free(rgb);
	LOG("saved screenshot %s", path);
}

void plat_present(void) {
	frames++;
	vtime += 16;
}

void plat_set_input_map(const InputMap *m) { (void)m; }

static int button_by_name(const char *s) {
	for (int i = 0; i < B_COUNT; i++)
		if (str_ieq(s, BUTTON_NAMES[i])) return i;
	return -1;
}

int plat_poll(InputEvent *ev) {
	memset(ev, 0, sizeof *ev);
	sncpy(ev->dev, "host script", sizeof ev->dev);
	if (quit_sent || vtime < next_at) return 0;
	if (pending_release >= 0) {
		ev->button = pending_release;
		ev->pressed = 0;
		pending_release = -1;
		next_at = vtime + 80;
		return 1;
	}
	if (!cursor) { ev->button = B_QUIT; ev->pressed = 1; quit_sent = 1; return 1; }
	while (*cursor == ' ' || *cursor == '\n' || *cursor == '\t') cursor++;
	if (!*cursor) { cursor = NULL; ev->button = B_QUIT; ev->pressed = 1; quit_sent = 1; return 1; }
	char tok[512];
	int n = 0;
	while (*cursor && *cursor != ' ' && *cursor != '\n' && *cursor != '\t' && n < 511) tok[n++] = *cursor++;
	tok[n] = 0;
	if (str_starts_with(tok, "wait:")) {
		next_at = vtime + (uint64_t)atoi(tok + 5);
		return 0;
	}
	if (str_starts_with(tok, "shot:")) {
		save_png(tok + 5);
		return 0;
	}
	if (str_ieq(tok, "quit")) { ev->button = B_QUIT; ev->pressed = 1; quit_sent = 1; return 1; }
	if (str_ieq(tok, "unknown")) { /* a raw key the mapping doesn't know */
		ev->button = -1; ev->pressed = 1; ev->raw_type = 1; ev->raw_code = 999; ev->raw_value = 1;
		next_at = vtime + 40;
		return 1;
	}
	if (str_starts_with(tok, "down:") || str_starts_with(tok, "up:")) { /* hold / let go, for button combos */
		int down = tok[0] == 'd';
		int hb = button_by_name(tok + (down ? 5 : 3));
		if (hb < 0) { LOG("script: unknown step '%s'", tok); return 0; }
		ev->button = hb;
		ev->pressed = down;
		ev->raw_type = 1;
		ev->raw_code = 300 + hb;
		ev->raw_value = down;
		next_at = vtime + 40;
		return 1;
	}
	int b = button_by_name(tok);
	if (b < 0) { LOG("script: unknown step '%s'", tok); return 0; }
	ev->button = b;
	ev->pressed = 1;
	ev->raw_type = 1;
	ev->raw_code = 300 + b;
	ev->raw_value = 1;
	pending_release = b;
	next_at = vtime + 40;
	return 1;
}

void plat_wait(int timeout_ms) {
	/* Never sleep for real; just let virtual time catch up with the script. */
	uint64_t step = timeout_ms > 0 ? (uint64_t)timeout_ms : 16;
	if (next_at > vtime && next_at - vtime < step) step = next_at - vtime;
	if (step < 1) step = 1;
	vtime += step;
}

uint64_t plat_ticks(void) { return vtime; }
int plat_is_host(void) { return 1; }

int plat_battery(void) { const char *e = getenv("CLOVE_BATTERY"); return e ? atoi(e) : 76; }
int plat_charging(void) { return 0; }
int plat_brightness_get(void) { return brightness; }
void plat_brightness_set(int level, int persist) { (void)persist; brightness = CLAMP(level, 1, 10); }
void plat_screen_off(int off) { (void)off; }
void plat_screen_dim(int dim) { (void)dim; }

int plat_cpu_freqs(int *out, int max) {
	static const int f[] = {240000, 504000, 720000, 840000, 1008000, 1104000, 1200000, 1296000, 1392000, 1488000};
	int n = MIN(max, ARRAY_LEN(f));
	memcpy(out, f, sizeof(int) * (size_t)n);
	return n;
}
void plat_cpu_set(const char *governor, int max_khz, int min_khz) {
	(void)min_khz;
	if (governor && *governor) sncpy(cpu_gov, governor, sizeof cpu_gov);
	if (max_khz > 0) cpu_max = max_khz;
}
int plat_cpu_cur(void) { return cpu_max; }
int plat_cpu_max(void) { return cpu_max; }

void plat_describe(char *out, size_t n) {
	snfmt(out, n, "CPU: %d MHz, governor %s\nMemory: 180 MB free of 233 MB\nScreen: 640x480, 32-bit, host test\nInputs: host script",
	      cpu_max / 1000, cpu_gov);
}

void plat_power(int reboot) {
	LOG("host: would %s now", reboot ? "reboot" : "power off");
	exit(0);
}

int plat_init(void) {
	const char *mmc = getenv("CLOVE_MMC");
	const char *sd = getenv("CLOVE_SD");
	sncpy(g_paths.mmc, mmc ? mmc : "./testcard/mmc", sizeof g_paths.mmc);
	sncpy(g_paths.sd, sd ? sd : "./testcard/SDCARD", sizeof g_paths.sd);
	snfmt(g_paths.cfw, sizeof g_paths.cfw, "%s/CFW", g_paths.mmc);
	snfmt(g_paths.clove, sizeof g_paths.clove, "%s/clove", g_paths.cfw);
	g_paths.sd_present = sd && dir_exists(sd);
	g_paths.mmc_is_tf2 = getenv("CLOVE_MMC_IS_TF2") != NULL;
	const char *s = getenv("CLOVE_SCRIPT");
	script = xstrdup(s ? s : "wait:200 quit");
	cursor = script;
	return 0;
}

void plat_quit(void) {
	const char *last = getenv("CLOVE_LAST_SHOT");
	if (last) save_png(last);
	LOG("host: %d frames presented", frames);
	free(script);
	script = cursor = NULL;
}
