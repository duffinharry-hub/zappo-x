/* Zappo — the hardware layer for the browser build (WebAssembly).
 *
 * The page (web/zappo.js) owns the screen, the buttons and the sound: it
 * calls zx_start() once, then zx_frame() on every animation frame, passes
 * button presses in with zx_key(), and draws the 640x480 canvas when
 * zx_present tells it to.  Files live in the page's own small file system
 * (the kit's themes, art and fonts, plus a few test files), saved in the
 * browser between visits.  Games and films don't run here - the page shows
 * what the handheld would start. */
#include "music.h"
#include "plat.h"
#include "ui.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEB_IMPORT(name) __attribute__((import_module("zappo"), import_name(#name)))
WEB_IMPORT(present) void js_present(const uint32_t *canvas, int w, int h);
WEB_IMPORT(now_ms) double js_now_ms(void);
WEB_IMPORT(battery) int js_battery(void);
WEB_IMPORT(charging) int js_charging(void);
WEB_IMPORT(brightness) void js_brightness(int level);
WEB_IMPORT(launch) void js_launch(const char *command, int len);
WEB_IMPORT(power) void js_power(int reboot);
WEB_IMPORT(describe) int js_describe(char *out, int n);

int app_start(void);
int app_frame(int wait);
int app_end(void);

static uint32_t canvas[SCREEN_W * SCREEN_H];
static int brightness = 7;
static int cpu_max = 1512000;
static char cpu_gov[32] = "ondemand";

/* button presses from the page, oldest first */
#define QN 64
static struct { int b, down; } q[QN];
static int qh, qt;

uint32_t *plat_canvas(void) { return canvas; }
int plat_has_vsync(void) { return 1; } /* the browser paces frames */
void plat_present(void) { js_present(canvas, SCREEN_W, SCREEN_H); }
void plat_set_input_map(const InputMap *m) { (void)m; }

int plat_poll(InputEvent *ev) {
	if (qh == qt) return 0;
	memset(ev, 0, sizeof *ev);
	ev->button = q[qh].b;
	ev->pressed = q[qh].down;
	ev->raw_type = 1;
	ev->raw_code = 300 + q[qh].b;
	ev->raw_value = q[qh].down;
	sncpy(ev->dev, "browser", sizeof ev->dev);
	qh = (qh + 1) % QN;
	return 1;
}

void plat_wait(int timeout_ms) { (void)timeout_ms; } /* the page calls again next frame */
uint64_t plat_ticks(void) { return (uint64_t)js_now_ms(); }
int plat_is_host(void) { return 0; }

int plat_battery(void) { return js_battery(); }
int plat_charging(void) { return js_charging(); }
int plat_brightness_get(void) { return brightness; }
void plat_brightness_set(int level, int persist) { (void)persist; brightness = CLAMP(level, 1, 10); js_brightness(brightness); }
void plat_screen_off(int off) { js_brightness(off ? 0 : brightness); }
void plat_screen_dim(int dim) { js_brightness(dim ? 1 : brightness); }

int plat_cpu_freqs(int *out, int max) {
	/* the RG40XX H's H700 steps, so the speed menus look as they will there */
	static const int f[] = {480000, 720000, 1008000, 1200000, 1416000, 1512000};
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

void plat_describe(char *out, size_t n) {
	char extra[200] = "";
	js_describe(extra, (int)sizeof extra);
	snfmt(out, n, "Zappo X preview in the browser\nScreen: 640x480, 32-bit\n%s", extra);
}

void plat_power(int reboot) { js_power(reboot); }

int plat_init(void) {
	sncpy(g_paths.mmc, "/mmc", sizeof g_paths.mmc);
	sncpy(g_paths.sd, "/sdcard", sizeof g_paths.sd);
	sncpy(g_paths.cfw, "/mmc/CFW", sizeof g_paths.cfw);
	sncpy(g_paths.clove, "/mmc/CFW/clove", sizeof g_paths.clove);
	g_paths.sd_present = 0;
	g_paths.mmc_is_tf2 = 0;
	qh = qt = 0;
	return 0;
}

void plat_quit(void) {}

/* ---------------------------------------------------------- for the page */

__attribute__((export_name("zx_start"))) int zx_start(void) { return app_start(); }

/* 1 while the menu runs; 0 when it has finished (a game or film was chosen,
 * or Quit) - zx_state() then says which */
__attribute__((export_name("zx_frame"))) int zx_frame(void) {
	music_pump();
	return app_frame(0);
}

__attribute__((export_name("zx_end"))) int zx_end(void) { return app_end(); }
__attribute__((export_name("zx_state"))) int zx_state(void) { return ui_state(); }
__attribute__((export_name("zx_tab"))) int zx_tab(void) { return ui_tab(); }

__attribute__((export_name("zx_key"))) void zx_key(int button, int down) {
	if (button < 0 || button >= B_COUNT) return;
	int nt = (qt + 1) % QN;
	if (nt == qh) return; /* full: drop it */
	q[qt].b = button;
	q[qt].down = down ? 1 : 0;
	qt = nt;
}

__attribute__((export_name("zx_music_playing"))) int zx_music_playing(void) { return music_is_playing(); }

/* what the handheld would have run: data.c's command.sh */
void web_launch(const char *body) { js_launch(body, (int)strlen(body)); }
