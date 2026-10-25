/* Zappo X — hardware layer for Knulli handhelds (the Anbernic RG40XX H first).
 *
 * Screen: SDL2, Knulli's own copy - on the handheld it draws through the Mali
 * GPU, on the device twin through KMS - with the menu uploaded as one
 * 640x480 picture per change.  Buttons: evdev, shared with the RG35XX layer
 * (plat_evdev.inc).  Battery, brightness and power: Knulli's names and tools,
 * so the same binary runs on the handheld and the twin.
 *
 * Knulli itself keeps the volume and power buttons (its hotkey daemon turns
 * the volume and puts the handheld to sleep, in games too), so Zappo leaves
 * those to it. */
#include "plat.h"
#include "util.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <dirent.h>
#include <ucontext.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int lights_tick(int wait_ms);
#define PLAT_IDLE_HOOK lights_tick
#include "plat_evdev.inc"
#include "config.h"
#include "scene.h"
#include "ui.h"

#define CPU_DIR "/sys/devices/system/cpu/cpu0/cpufreq"
#define PSY "/sys/class/power_supply"

/* ---------------------------------------------------------------- display */

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static int vsync;
static int drm_fd = -1; /* SDL's KMS device (the twin), to take the screen over */
static uint32_t canvas[SCREEN_W * SCREEN_H];
static char video_desc[96] = "no screen";

static void sdl_log(void *ud, int category, SDL_LogPriority prio, const char *msg) {
	LOG("SDL: %s", msg);
}

static int screen_open(void) {
	/* SDL's own messages in the log, when asked for (ZAPPO_SDL_LOG=1) */
	if (getenv("ZAPPO_SDL_LOG")) {
		SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
		SDL_LogSetOutputFunction(sdl_log, NULL);
	}
	/* KMS/DRM on the twin, Mali on the handheld: SDL picks what it was built
	 * for. Just after switch-on the screen can still be busy (Knulli's start-up
	 * picture): SDL then settles for its "offscreen" driver, which shows
	 * nothing - so wait for the real one (up to 30 s). */
	for (int attempt = 0;; attempt++) {
		if (SDL_Init(SDL_INIT_VIDEO) == 0) {
			const char *drv = SDL_GetCurrentVideoDriver();
			if (drv && strcmp(drv, "offscreen") && strcmp(drv, "dummy") && strcmp(drv, "evdev")) {
				if (attempt) LOG("screen: %s after %d tries", drv, attempt + 1);
				break;
			}
			if (attempt == 0 || attempt % 10 == 0) LOG("screen: only SDL's %s driver so far; waiting for the screen", drv ? drv : "?");
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		} else if (attempt == 0 || attempt % 10 == 0) LOG("SDL_Init: %s", SDL_GetError());
		if (attempt >= 60) return -1;
		usleep(500000);
	}
	SDL_ShowCursor(SDL_DISABLE);
	/* the screen as it is (a TV on HDMI can be 1280 x 720): Zappo's 640 x 480
	 * picture is scaled to fit, with bars at the sides */
	win = SDL_CreateWindow("Zappo X", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	                       SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN_DESKTOP);
	if (!win) {
		LOG("SDL_CreateWindow: %s", SDL_GetError());
		return -1;
	}
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
	if (!ren) {
		LOG("SDL_CreateRenderer: %s", SDL_GetError());
		return -1;
	}
	SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
	{
		int ow = 0, oh = 0;
		SDL_GetRendererOutputSize(ren, &ow, &oh);
		/* made bigger: smoothly (not every pixel grows by the same amount) */
		if (ow != SCREEN_W || oh != SCREEN_H) SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}
	tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
	if (!tex) {
		LOG("SDL_CreateTexture: %s", SDL_GetError());
		return -1;
	}
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE); /* the canvas's top byte is not alpha */
	SDL_RendererInfo ri;
	int w = 0, h = 0;
	SDL_GetRendererInfo(ren, &ri);
	SDL_GetWindowSize(win, &w, &h);
	vsync = (ri.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if (SDL_GetWindowWMInfo(win, &wm) && wm.subsystem == SDL_SYSWM_KMSDRM) drm_fd = wm.info.kmsdrm.drm_fd;
	snfmt(video_desc, sizeof video_desc, "%dx%d, %s on %s%s", w, h, ri.name,
	      SDL_GetCurrentVideoDriver(), vsync ? ", vsync" : "");
	LOG("screen: %s", video_desc);
	return 0;
}

uint32_t *plat_canvas(void) { return canvas; }
int plat_has_vsync(void) { return vsync; }

static uint64_t t_start, t_screen;

/* ------------------------------------------------------------ stick lights */

/* By default the lights follow the section on screen, in its Zappo's colour
 * (Settings > Lights: one colour, Knulli's own setting, or off). Knulli's
 * light daemon does the talking to the controller: it reads the colours from
 * its values file each second, applies Knulli's light brightness (scaled with
 * the screen's, when set so) and still shows low battery and charging on top.
 * It also rewrites that file from Knulli's settings whenever batocera.conf
 * changes, so Zappo X checks now and then that its colours are still there. */
#define LED_VALUES "/var/run/knulli-rgb-led-daemon.values"
#define LED_DAEMON_PID "/var/run/knulli-rgb-led-daemon.led.pid"
const LightColour plat_light_colours[LIGHT_COLOURS] = {
	{"Electric yellow", 0xf5c842}, {"Crystal purple", 0xb57cff}, {"Fire orange", 0xff8a1f},
	{"Water blue", 0x3fc1ff}, {"Ice blue", 0x8fd3ff}, {"Leaf green", 0x9fd84a},
	{"Snow white", 0xe9ecf5}, {"Ruby red", 0xff3b4e}, {"Rose pink", 0xff7ac8},
};
/* each section's Zappo: Recent, Favorites, Consoles, Music, Videos, Knulli's menu, Settings */
static const int SECTION_COLOUR[] = {0, 1, 2, 3, 4, 6, 5};

/* batocera.conf "key=value", read directly (batocera-settings-get is slow) */
#define KNULLI_CONF "/userdata/system/batocera.conf"
static uint64_t conf_written_at; /* (Knulli's light daemon reacts to changes: see lights_tick) */
int plat_conf_get(const char *key, char *out, size_t n) {
	out[0] = 0;
	FILE *f = fopen(KNULLI_CONF, "r");
	if (!f) return 0;
	char line[1024];
	size_t kl = strlen(key);
	int found = 0;
	while (fgets(line, sizeof line, f))
		if (!strncmp(line, key, kl) && line[kl] == '=') {
			sncpy(out, line + kl + 1, n);
			out[strcspn(out, "\r\n")] = 0;
			found = 1;	/* (the last one counts, as for Knulli) */
		}
	fclose(f);
	return found;
}

int plat_conf_int(const char *key, int fallback) {
	char v[64];
	if (!plat_conf_get(key, v, sizeof v) || !v[0]) return fallback;
	if (!strcmp(v, "true") || !strcmp(v, "on")) return 1;
	if (!strcmp(v, "false") || !strcmp(v, "off")) return 0;
	return atoi(v);
}

/* Like batocera-settings-set: the key's line is changed where it is (an
 * unset one is commented out, so Knulli's default applies), else added at the
 * end. Written beside and renamed over, so the file is never left half done. */
void plat_conf_set(const char *key, const char *value) {
	size_t len = 0;
	char *txt = file_read_all(KNULLI_CONF, &len);
	if (!txt) { txt = xmalloc(1); txt[0] = 0; }
	char tmp[] = KNULLI_CONF ".zappo";
	FILE *f = fopen(tmp, "w");
	if (!f) { free(txt); return; }
	size_t kl = strlen(key);
	int done = 0;
	for (char *line = txt; *line;) {
		char *nl = strchr(line, '\n');
		size_t ll = nl ? (size_t)(nl - line) : strlen(line);
		int active = !strncmp(line, key, kl) && line[kl] == '=';
		int commented = line[0] == '#' && !strncmp(line + 1, key, kl) && line[kl + 1] == '=';
		if ((active || commented) && value && !done) { fprintf(f, "%s=%s\n", key, value); done = 1; }
		else if (active) fprintf(f, "#%.*s\n", (int)ll, line);
		else fprintf(f, "%.*s\n", (int)ll, line);
		line = nl ? nl + 1 : line + ll;
	}
	if (value && !done) fprintf(f, "%s=%s\n", key, value);
	free(txt);
	fflush(f);
	fsync(fileno(f));
	fclose(f);
	rename(tmp, KNULLI_CONF);
	conf_written_at = now_ms();
}

int plat_lights_available(void) { return file_exists(LED_DAEMON_PID) && file_exists(LED_VALUES); }

static char led_written[96];   /* what Zappo X last put in the values file */
static long long led_stamp = -1;
static int led_mode_applied = -1;
static uint64_t led_next_check;

static long long led_file_stamp(void) {
	struct stat sb;
	return stat(LED_VALUES, &sb) == 0 ? (long long)sb.st_mtime * 1000003LL + sb.st_size + (long long)sb.st_ino * 7919 : -1;
}

/* the values line for now, or 0 when Knulli's own setting applies. With
 * Zappo's section colours, the effect, brightness and speed are still Knulli's
 * (led.mode, led.brightness, led.speed): only the colour follows the section. */
static int lights_line(char *out, size_t n) {
	if (g_set.lights != LIGHTS_FOLLOW) return 0;
	int mode = CLAMP(plat_conf_int("led.mode", 1), 0, 6);
	if (mode == 0) { snfmt(out, n, "0 0 0 0 0 0 0 0 0\n"); return 1; }
	int t = ui_tab();
	if (t < 0 || t >= (int)ARRAY_LEN(SECTION_COLOUR)) t = 2;
	uint32_t c = plat_light_colours[CLAMP(SECTION_COLOUR[t], 0, LIGHT_COLOURS - 1)].rgb;
	int r = (int)(c >> 16) & 255, g = (int)(c >> 8) & 255, b = (int)c & 255;
	snfmt(out, n, "%d %d %d %d %d %d %d %d %d\n", mode,
	      plat_conf_int("led.brightness", 70), plat_conf_int("led.speed", 0), r, g, b, r, g, b);
	return 1;
}

/* Zappo X 0.1 had its own "one colour" and "off": they become Knulli's settings */
static void lights_migrate(void) {
	if (g_set.lights == LIGHTS_ONE) {
		uint32_t c = plat_light_colours[CLAMP(g_set.lights_colour, 0, LIGHT_COLOURS - 1)].rgb;
		char v[32];
		snfmt(v, sizeof v, "%d %d %d", (int)(c >> 16) & 255, (int)(c >> 8) & 255, (int)c & 255);
		plat_conf_set("led.colour", v);
		g_set.lights = LIGHTS_KNULLI;
	} else if (g_set.lights == LIGHTS_OFF) {
		plat_conf_set("led.mode", "0");
		g_set.lights = LIGHTS_KNULLI;
	}
}

static void lights_write(const char *line) {
	char tmp[] = LED_VALUES ".zappo";
	FILE *f = fopen(tmp, "w");
	if (!f) return;
	fputs(line, f);
	fclose(f);
	rename(tmp, LED_VALUES);
	sncpy(led_written, line, sizeof led_written);
	led_stamp = led_file_stamp();
}

/* force: write even if nothing changed on Zappo's side */
static void lights_update(int force) {
	if (!plat_lights_available()) return;
	lights_migrate();
	if (g_set.lights == LIGHTS_KNULLI) {
		if (led_mode_applied != LIGHTS_KNULLI) {
			/* hand back to Knulli's own colours and effect */
			if (system("knulli-rgb-led-daemon import >/dev/null 2>&1 &") != 0) LOG("lights: could not hand back to Knulli");
			led_written[0] = 0;
		}
		led_mode_applied = LIGHTS_KNULLI;
		return;
	}
	char line[96];
	lights_line(line, sizeof line);
	if (force || strcmp(line, led_written) != 0 || led_mode_applied != g_set.lights) lights_write(line);
	led_mode_applied = g_set.lights;
}

static void lights_follow(void) { lights_update(0); }

/* when idle: put Zappo's colours back if Knulli's daemon rewrote the file (it
 * does about a second after any change to batocera.conf - so for a few
 * seconds after Zappo X changes it, look often, to keep the flash short) */
static int lights_tick(int wait_ms) {
	int soon = now_ms() - conf_written_at < 4000;
	if (soon && wait_ms > 150) wait_ms = 150;
	if (now_ms() < led_next_check && !soon) return wait_ms;
	led_next_check = now_ms() + 900;
	if (g_set.lights == LIGHTS_KNULLI || !led_written[0]) return wait_ms;
	if (led_file_stamp() != led_stamp) lights_update(1);
	return wait_ms;
}

void plat_lights_apply(void) { lights_update(1); }

/* ------------------------------------------------------------------ HDMI */

/* Knulli shows everything on the TV while one is plugged in (its start
 * script switches the output: knulli/zappo-x.sh does the same); Zappo X
 * starts again on the new screen when the cable goes in or out. */
#define HDMI_STATE "/sys/devices/platform/soc/6000000.hdmi/extcon/hdmi/state"
static int hdmi_read(void) {
	const char *p = getenv("ZAPPO_HDMI_STATE");	/* (the twin has no HDMI: a file stands in) */
	char b[32] = "";
	FILE *f = fopen(p && *p ? p : HDMI_STATE, "r");
	if (!f) return -1;
	if (!fgets(b, sizeof b, f)) b[0] = 0;
	fclose(f);
	return strstr(b, "HDMI=1") != NULL;
}

int plat_tv(void) { return hdmi_read() == 1; }

int plat_display_changed(void) {
	static int at_start = -2;
	static uint64_t next;
	if (now_ms() < next) return 0;
	next = now_ms() + 1000;
	int h = hdmi_read();
	if (at_start == -2) at_start = h;
	return h != at_start;
}

/* ------------------------------------------------------- battery saver */

#define SAVER_PAUSE "/var/run/battery-saver/zappo-x.pause"
void plat_keep_awake(int on) {
	static int now = -1;
	if (on == now) return;
	now = on;
	if (on) {
		mkdir("/var/run/battery-saver", 0755);
		FILE *f = fopen(SAVER_PAUSE, "w");
		if (f) fclose(f);
	} else unlink(SAVER_PAUSE);
}

/* ------------------------------------------------- the moving backgrounds */

/* Three layers, put together by the GPU each frame: the background picture,
 * the section's particles (scene.c), and Zappo's menu drawn see-through
 * (premultiplied) by the software renderer - which only redraws it when
 * something changes. */
static SDL_Texture *bgtex, *atlas, *oldtex;
static uint64_t fade_start;	/* changing section: the old menu fades out (GPU only) */
#define FADE_MS 180
static SDL_FRect sp_uv[SP_COUNT];	/* where each particle picture sits in the atlas (0..1) */
static int scene_on, scene_ready = -1;
static uint64_t scene_last, scene_next;
static SDL_BlendMode premul;
static int scene_ms = 33;	/* 30 frames a second; fewer on a GPU done in software (the twin's) */
static int soft_gpu;
static uint32_t soft_bg[SCREEN_W * SCREEN_H];	/* the background, for putting the picture together without the GPU */

static int scene_init(void) {
	if (scene_ready >= 0) return scene_ready;
	scene_ready = 0;
	bgtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
	if (!bgtex) { LOG("scene: no background texture (%s)", SDL_GetError()); return 0; }
	SDL_SetTextureBlendMode(bgtex, SDL_BLENDMODE_NONE);
	/* the particle pictures side by side in one texture, so all the particles
	 * go out in two draws (one blended, one glowing) instead of one each */
	int aw = 0, ah = 0;
	for (int i = 0; i < SP_COUNT; i++) {
		int w, h;
		scene_sprite(i, &w, &h);
		aw += w + 2;
		if (h + 2 > ah) ah = h + 2;
	}
	uint32_t *apx = calloc((size_t)(aw * ah), 4);
	if (!apx) return 0;
	for (int i = 0, x = 1; i < SP_COUNT; i++) {
		int w, h;
		const uint32_t *px = scene_sprite(i, &w, &h);
		for (int y = 0; y < h; y++) memcpy(apx + (y + 1) * aw + x, px + y * w, (size_t)w * 4);
		sp_uv[i] = (SDL_FRect){(float)x / aw, 1.0f / ah, (float)w / aw, (float)h / ah};
		x += w + 2;
	}
	atlas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, aw, ah);
	if (atlas) {
		SDL_UpdateTexture(atlas, NULL, apx, aw * 4);
		SDL_SetTextureScaleMode(atlas, SDL_ScaleModeLinear);
	}
	free(apx);
	if (!atlas) { LOG("scene: no particle texture (%s)", SDL_GetError()); return 0; }
	/* the menu: already multiplied by its own cover */
	premul = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
	                                    SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
	if (SDL_SetTextureBlendMode(tex, premul) != 0) { LOG("scene: the GPU can't layer (%s)", SDL_GetError()); return 0; }
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
	/* which GPU: the handheld's Mali, or Mesa drawing in software (the twin) */
	typedef const unsigned char *(*GetString)(unsigned);
	GetString gs = (GetString)SDL_GL_GetProcAddress("glGetString");
	const char *gpu = gs ? (const char *)gs(0x1F01 /* GL_RENDERER */) : NULL;
	if (gpu && (strstr(gpu, "softpipe") || strstr(gpu, "llvmpipe"))) { soft_gpu = 1; scene_ms = 100; }
	scene_ready = 1;
	LOG("scene: ready on %s, a frame every %d ms", gpu ? gpu : "an unknown GPU", scene_ms);
	return 1;
}

int plat_layers_ok(void) { return ren && tex && scene_init(); }

void plat_scene(const uint32_t *bg, int bg_changed, int element, int density) {
	int on = bg != NULL && scene_init();
	if (on && bg_changed && soft_gpu) memcpy(soft_bg, bg, sizeof soft_bg);	/* (kept: the menu's may go away) */
	if (on && bg_changed && !soft_gpu) {
		if (SDL_UpdateTexture(bgtex, NULL, bg, SCREEN_W * 4) != 0) {
			static int logged;
			if (!logged++) LOG("scene: background picture: %s", SDL_GetError());
		}
	}
	if (on != scene_on) {
		SDL_SetTextureBlendMode(tex, on && !soft_gpu ? premul : SDL_BLENDMODE_NONE);
		scene_last = now_ms();
	}
	scene_on = on;
	scene_set(on ? element : EL_NONE, density);
}

static void draw_particles(void) {
	static SceneQuad q[256];
	static SDL_Vertex v[2][256 * 4];
	static int idx[256 * 6];
	if (!idx[1]) for (int i = 0; i < 256; i++) {
		int *p = idx + i * 6;
		p[0] = i * 4; p[1] = i * 4 + 1; p[2] = i * 4 + 2; p[3] = i * 4; p[4] = i * 4 + 2; p[5] = i * 4 + 3;
	}
	uint64_t now = now_ms();
	float dt = scene_last ? (now - scene_last) / 1000.0f : 0.033f;
	scene_last = now;
	int n = scene_update(dt, q, 256), nv[2] = {0, 0};
	for (int i = 0; i < n; i++) {
		int g = q[i].additive ? 1 : 0;
		SDL_Vertex *o = v[g] + nv[g];
		nv[g] += 4;
		float a = q[i].angle * 0.017453293f, c = cosf(a), sn = sinf(a), hw = q[i].w / 2, hh = q[i].h / 2;
		const SDL_FRect *uv = &sp_uv[q[i].sprite];
		SDL_Color col = {(Uint8)(q[i].tint >> 16), (Uint8)(q[i].tint >> 8), (Uint8)q[i].tint, (Uint8)(q[i].alpha * 255 + 0.5f)};
		static const float CX[4] = {-1, 1, 1, -1}, CY[4] = {-1, -1, 1, 1};
		for (int k = 0; k < 4; k++) {
			float dx = CX[k] * hw, dy = CY[k] * hh;
			o[k].position.x = q[i].x + dx * c - dy * sn;
			o[k].position.y = q[i].y + dx * sn + dy * c;
			o[k].color = col;
			o[k].tex_coord.x = uv->x + (CX[k] > 0 ? uv->w : 0);
			o[k].tex_coord.y = uv->y + (CY[k] > 0 ? uv->h : 0);
		}
	}
	/* the solid ones first, then the glows over them */
	for (int g = 0; g < 2; g++) {
		if (!nv[g]) continue;
		SDL_SetTextureBlendMode(atlas, g ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
		SDL_RenderGeometry(ren, atlas, v[g], nv[g], idx, nv[g] / 4 * 6);
	}
}

/* one picture on screen: with the moving background under the menu, or the menu alone */
static void compose(int fresh) {
	(void)fresh;
	uint64_t t0 = now_ms();
	SDL_RenderClear(ren);
	if (scene_on && soft_gpu) {
		/* a GPU done in software draws each whole-screen layer slowly: put
		 * them together here and give it one */
		static uint32_t mix[SCREEN_W * SCREEN_H];
		uint64_t now = now_ms();
		float dt = scene_last ? (now - scene_last) / 1000.0f : 0.033f;
		scene_last = now;
		scene_compose_soft(mix, soft_bg, canvas, SCREEN_W, SCREEN_H, dt);
		SDL_UpdateTexture(bgtex, NULL, mix, SCREEN_W * 4);
		SDL_RenderCopy(ren, bgtex, NULL, NULL);
	} else {
		if (scene_on) {
			SDL_RenderCopy(ren, bgtex, NULL, NULL);
			draw_particles();
		}
		uint64_t since = now_ms() - fade_start;
		if (fade_start && scene_on && since < FADE_MS) {
			/* both menus, premultiplied: so the colours fade with the cover */
			Uint8 o = (Uint8)(255 - 255 * since / FADE_MS), nw = (Uint8)(255 - o);
			SDL_SetTextureColorMod(oldtex, o, o, o);
			SDL_SetTextureAlphaMod(oldtex, o);
			SDL_RenderCopy(ren, oldtex, NULL, NULL);
			SDL_SetTextureColorMod(tex, nw, nw, nw);
			SDL_SetTextureAlphaMod(tex, nw);
			SDL_RenderCopy(ren, tex, NULL, NULL);
			SDL_SetTextureColorMod(tex, 255, 255, 255);
			SDL_SetTextureAlphaMod(tex, 255);
		} else {
			fade_start = 0;
			SDL_RenderCopy(ren, tex, NULL, NULL);
		}
	}
	SDL_RenderPresent(ren);
	/* a GPU done in software: no more than a fifth of the time on this */
	if (soft_gpu) scene_ms = (int)CLAMP((now_ms() - t0) * 4, 100, 1000);
	scene_next = now_ms() + (uint64_t)scene_ms;
	/* how long a picture takes to put together, now and then in the log */
	static uint64_t sum;
	static int n, reports;
	if (scene_on) {
		sum += now_ms() - t0;
		if (++n == 20 && reports < 3) { reports++; LOG("scene: %.1f ms a picture", sum / 20.0); }
		if (n == 20) { n = 0; sum = 0; }
	}
}

int plat_scene_frame(void) {
	if (!scene_on || !tex) return 0;
	uint64_t now = now_ms();
	int step = fade_start ? 16 : scene_ms;	/* (a fade goes at 60 frames a second) */
	if (fade_start && scene_next > now + 16) scene_next = now + 16;
	if (now < scene_next) return (int)(scene_next - now);
	compose(0);
	if (fade_start) scene_next = now_ms() + 16;
	return step;
}

void plat_transition(void) {
	if (!scene_on || soft_gpu || !tex) return;
	if (!oldtex) {
		oldtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
		if (!oldtex) return;
		SDL_SetTextureBlendMode(oldtex, premul);
	}
	/* the canvas still holds what's on screen: the new section isn't drawn yet */
	SDL_UpdateTexture(oldtex, NULL, canvas, SCREEN_W * 4);
	fade_start = now_ms();
}

void plat_present(void) {
	frames_presented++;
	lights_follow();
	if (!tex) return;
	if (frames_presented == 1)
		LOG("first picture %llu ms after start (screen ready after %llu ms)",
		    (unsigned long long)(now_ms() - t_start), (unsigned long long)(t_screen - t_start));
	/* KMS: if the program before (a game, Knulli's menu) was still letting go
	 * of the screen when this one started, SDL isn't its master and nothing
	 * shows; take it over as soon as it is free (harmless when already ours) */
	if (drm_fd >= 0 && now_ms() - t_start < 30000) {
		static int was_master = -1;
		int m = ioctl(drm_fd, 0x641e /* DRM_IOCTL_SET_MASTER */, 0) == 0;
		if (m != was_master) LOG("screen: %s", m ? "ours" : "still held by another program");
		was_master = m;
	}
	SDL_UpdateTexture(tex, NULL, canvas, SCREEN_W * 4);
	/* The first picture goes out three times: after a game, the screen can be
	 * left switched on with nothing to show, and SDL's KMS driver only sets
	 * it up again when a page flip fails - Zappo X draws only when something
	 * changes, so otherwise it stayed black until the clock next ticked. */
	for (int i = frames_presented == 1 ? 3 : 1; i > 0; i--) compose(1);
	/* nothing listens to SDL's events, but some video drivers want them pumped */
	SDL_PumpEvents();
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}

/* ---------------------------------------------------------- sysfs, tools */

static int read_int(const char *path, int fallback) {
	FILE *f = fopen(path, "r");
	if (!f) return fallback;
	int v = fallback;
	if (fscanf(f, "%d", &v) != 1) v = fallback;
	fclose(f);
	return v;
}
static void write_str(const char *path, const char *s) {
	FILE *f = fopen(path, "w");
	if (!f) return;
	fputs(s, f);
	fclose(f);
}
static void write_int(const char *path, int v) {
	char b[32];
	snfmt(b, sizeof b, "%d", v);
	write_str(path, b);
}

/* the battery and the charger: the RG40XX H's AXP2202 names, else the first
 * battery and mains/USB supply found */
static char bat_dir[128], chg_dir[128];

static void find_power(void) {
	if (dir_exists(PSY "/axp2202-battery")) sncpy(bat_dir, PSY "/axp2202-battery", sizeof bat_dir);
	if (dir_exists(PSY "/axp2202-usb")) sncpy(chg_dir, PSY "/axp2202-usb", sizeof chg_dir);
	DIR *d = opendir(PSY);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[256], type[32] = "";
		snfmt(p, sizeof p, PSY "/%s/type", e->d_name);
		FILE *f = fopen(p, "r");
		if (f) { if (fscanf(f, "%31s", type) != 1) type[0] = 0; fclose(f); }
		if (!bat_dir[0] && strcmp(type, "Battery") == 0) snfmt(bat_dir, sizeof bat_dir, PSY "/%s", e->d_name);
		if (!chg_dir[0] && (strcmp(type, "USB") == 0 || strcmp(type, "Mains") == 0))
			snfmt(chg_dir, sizeof chg_dir, PSY "/%s", e->d_name);
	}
	if (d) closedir(d);
	LOG("battery %s, charger %s", bat_dir[0] ? bat_dir : "none", chg_dir[0] ? chg_dir : "none");
}

int plat_battery(void) {
	char p[192];
	if (!bat_dir[0]) return -1;
	snfmt(p, sizeof p, "%s/capacity", bat_dir);
	return read_int(p, -1);
}

int plat_charging(void) {
	char p[192];
	if (chg_dir[0]) {
		snfmt(p, sizeof p, "%s/online", chg_dir);
		if (read_int(p, 0) > 0) return 1;
	}
	if (bat_dir[0]) {
		char st[32] = "";
		snfmt(p, sizeof p, "%s/status", bat_dir);
		FILE *f = fopen(p, "r");
		if (f) { if (fscanf(f, "%31s", st) != 1) st[0] = 0; fclose(f); }
		return strcmp(st, "Charging") == 0 || strcmp(st, "Full") == 0;
	}
	return 0;
}

/* Brightness goes through Knulli's batocera-brightness (0-100 %), which knows
 * the panel's backlight and keeps the level for the next start. */
static int bri_pct = -1;

static int bri_read(void) {
	FILE *p = popen("batocera-brightness 2>/dev/null", "r");
	int v = -1;
	if (p) {
		if (fscanf(p, "%d", &v) != 1) v = -1;
		pclose(p);
	}
	return v;
}

static void bri_write(int pct) {
	char cmd[64];
	snfmt(cmd, sizeof cmd, "batocera-brightness %d >/dev/null 2>&1", CLAMP(pct, 0, 100));
	if (system(cmd) != 0) LOG("batocera-brightness failed");
}

int plat_brightness_get(void) {
	if (bri_pct < 0) bri_pct = bri_read();
	if (bri_pct < 0) return 7;
	return CLAMP((bri_pct + 5) / 10, 1, 10);
}

void plat_brightness_set(int level, int persist) {
	level = CLAMP(level, 1, 10);
	bri_pct = level * 10;
	bri_write(bri_pct);
}

/* 0 = as set, 1 = very dim (the lock screen clock), 2 = off */
static int scr_mode, bri_saved = -1;
static void screen_mode(int mode) {
	if (mode == scr_mode) return;
	if (scr_mode == 0) bri_saved = bri_pct >= 0 ? bri_pct : bri_read();
	if (mode == 0) {
		if (bri_saved >= 0) bri_write(bri_saved);
	} else {
		bri_write(mode == 1 ? 3 : 0);
	}
	scr_mode = mode;
}
void plat_screen_off(int off) { screen_mode(off ? 2 : 0); }
void plat_screen_dim(int dim) { screen_mode(dim ? 1 : 0); }

int plat_cpu_freqs(int *out, int max) {
	FILE *f = fopen(CPU_DIR "/scaling_available_frequencies", "r");
	int n = 0;
	if (f) {
		int v;
		while (n < max && fscanf(f, "%d", &v) == 1) out[n++] = v;
		fclose(f);
	}
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && out[j - 1] > out[j]; j--) { int t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
	return n;
}
void plat_cpu_set(const char *governor, int max_khz, int min_khz) {
	if (governor && *governor) write_str(CPU_DIR "/scaling_governor", governor);
	if (min_khz > 0) write_int(CPU_DIR "/scaling_min_freq", min_khz);
	if (max_khz > 0) write_int(CPU_DIR "/scaling_max_freq", max_khz);
}
int plat_cpu_cur(void) { return read_int(CPU_DIR "/scaling_cur_freq", 0); }
int plat_cpu_max(void) { return read_int(CPU_DIR "/cpuinfo_max_freq", 0); }

void plat_describe(char *out, size_t n) {
	char gov[32] = "?", model[64] = "?";
	FILE *f = fopen(CPU_DIR "/scaling_governor", "r");
	if (f) { if (fscanf(f, "%31s", gov) != 1) sncpy(gov, "?", sizeof gov); fclose(f); }
	f = fopen("/proc/device-tree/model", "r");
	if (f) { if (!fgets(model, sizeof model, f)) sncpy(model, "?", sizeof model); fclose(f); }
	long mem_kb = 0, avail_kb = 0;
	f = fopen("/proc/meminfo", "r");
	if (f) {
		char line[128];
		while (fgets(line, sizeof line, f)) {
			if (str_starts_with(line, "MemTotal:")) sscanf(line + 9, "%ld", &mem_kb);
			if (str_starts_with(line, "MemAvailable:")) sscanf(line + 13, "%ld", &avail_kb);
		}
		fclose(f);
	}
	char inputs[256] = "";
	for (int i = 0; i < ndevs; i++) {
		size_t l = strlen(inputs);
		snfmt(inputs + l, sizeof inputs - l, "%s%s%s", i ? ", " : "", devs[i].name, devs[i].is_joy ? " (pad)" : "");
	}
	snfmt(out, n,
	      "Handheld: %s\nCPU: %d MHz, governor %s\nMemory: %ld MB free of %ld MB\nScreen: %s\nInputs: %s",
	      model, plat_cpu_cur() / 1000, gov, avail_kb / 1024, mem_kb / 1024, video_desc,
	      ndevs ? inputs : "none found");
}

void plat_power(int reboot) {
	sync();
	/* through init, so Knulli's services save their state (Bluetooth pairings, ...) */
	if (system(reboot ? "reboot" : "poweroff") != 0) LOG("power command failed");
	for (;;) sleep(5);
}

/* ------------------------------------------------------- films: mpv remote */

/* Knulli's mpv plays the films; its own gamepad support (through SDL) blanks
 * its picture, so the handheld's buttons reach it this way instead: this runs
 * beside mpv ("zappo-x --mpv-remote SOCKET"), reads the buttons and sends mpv
 * commands over its IPC socket. Ends when mpv does. */
int plat_mpv_remote(const char *sock_path) {
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	sncpy(sa.sun_path, sock_path, sizeof sa.sun_path);
	int fd = -1;
	for (int i = 0; i < 200 && fd < 0; i++) {	/* mpv takes a moment to open it */
		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) { close(fd); fd = -1; usleep(100000); }
	}
	if (fd < 0) return 1;
	open_inputs();
	input_map_defaults(&imap);
	static const struct { int b; const char *cmd; } MAP[] = {
		{B_A, "[\"cycle\",\"pause\"]"}, {B_START, "[\"cycle\",\"pause\"]"},
		{B_B, "[\"quit-watch-later\"]"}, {B_MENU, "[\"quit-watch-later\"]"},
		{B_SELECT, "[\"show-progress\"]"},
		{B_LEFT, "[\"seek\",-10]"}, {B_RIGHT, "[\"seek\",10]"},
		{B_UP, "[\"seek\",60]"}, {B_DOWN, "[\"seek\",-60]"},
		{B_L1, "[\"playlist-prev\"]"}, {B_R1, "[\"playlist-next\"]"},
		{B_Y, "[\"cycle\",\"sub\"]"}, {B_X, "[\"cycle\",\"audio\"]"},
	};
	for (;;) {
		struct pollfd pfd[MAX_DEV + 1];
		int n = 0;
		pfd[n].fd = fd; pfd[n].events = POLLIN; n++;
		for (int i = 0; i < ndevs; i++) { pfd[n].fd = devs[i].fd; pfd[n].events = POLLIN; n++; }
		if (poll(pfd, (nfds_t)n, 1000) < 0) continue;
		if (pfd[0].revents & (POLLIN | POLLHUP | POLLERR)) {
			char junk[4096];	/* mpv's own messages: only its going away matters */
			if (read(fd, junk, sizeof junk) <= 0) break;
		}
		InputEvent ev;
		while (plat_poll(&ev)) {
			if (!ev.pressed || ev.button < 0) continue;
			for (int k = 0; k < ARRAY_LEN(MAP); k++)
				if (MAP[k].b == ev.button) {
					char msg[128];
					int l = snfmt(msg, sizeof msg, "{\"command\":%s}\n", MAP[k].cmd);
					if (write(fd, msg, (size_t)l) < 0) return 0;
				}
		}
	}
	close(fd);
	return 0;
}

/* -------------------------------------------------------------- lifecycle */

/* A crash leaves its trail in crash.log: where it happened (pc, lr, the
 * address touched) and where the program and its libraries sat in memory, to
 * be read against the unstripped build (build/zappo-x.debug). */
static void crash_handler(int sig, siginfo_t *si, void *ucv) {
	char p[300], line[256];
	snfmt(p, sizeof p, "%s/crash.log", g_paths.clove);
	int fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		ucontext_t *uc = ucv;
#if defined(__aarch64__)
		int l = snprintf(line, sizeof line, "signal %d, address %p, pc %llx, lr %llx\n", sig, si->si_addr,
		                 (unsigned long long)uc->uc_mcontext.pc, (unsigned long long)uc->uc_mcontext.regs[30]);
#else
		int l = snprintf(line, sizeof line, "signal %d, address %p\n", sig, si->si_addr);
		(void)uc;
#endif
		if (write(fd, line, (size_t)l) < 0) l = 0;
		int m = open("/proc/self/maps", O_RDONLY);
		char buf[4096];
		ssize_t r;
		while (m >= 0 && (r = read(m, buf, sizeof buf)) > 0)
			if (write(fd, buf, (size_t)r) < 0) break;
		if (m >= 0) close(m);
		close(fd);
	}
	signal(sig, SIG_DFL);
	raise(sig);
}

static void crash_handlers(void) {
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = crash_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
	for (int i = 0; i < 5; i++) sigaction(sigs[i], &sa, NULL);
}

static double uptime_s(void) {
	double u = 0;
	FILE *f = fopen("/proc/uptime", "r");
	if (f) { if (fscanf(f, "%lf", &u) != 1) u = 0; fclose(f); }
	return u;
}

/* A stock Anbernic card in TF2: Knulli mounts its partitions under /media; the
 * one with a Roms folder of console folders holds its games, which Zappo X
 * lists beside the Knulli card's own (data.c: STOCK_DIRS). Only read. */
static void find_stock_card(void) {
	for (int tries = 0; tries < 20 && !g_paths.sd_present; tries++) {
		FILE *f = fopen("/proc/mounts", "r");
		if (!f) return;
		char dev[128], mp[256];
		while (fscanf(f, "%127s %255s %*s %*s %*d %*d", dev, mp) == 2) {
			if (!str_starts_with(mp, "/media/") || !str_starts_with(dev, "/dev/")) continue;
			if (!strcmp(mp, "/media/SHARE") || !strcmp(mp, "/media/BATOCERA")) continue;
			char p[300];
			snfmt(p, sizeof p, "%s/Roms", mp);
			if (!dir_exists(p)) continue;
			int known = 0;
			static const char *SOME[] = {"MD", "GBA", "PS", "FC", "SFC", "MAME", "GB", "PSP"};
			for (int k = 0; k < ARRAY_LEN(SOME) && !known; k++) {
				snfmt(p, sizeof p, "%s/Roms/%s", mp, SOME[k]);
				known = dir_exists(p);
			}
			if (!known) continue;
			sncpy(g_paths.sd, mp, sizeof g_paths.sd);
			g_paths.sd_present = g_paths.sd_stock = 1;
			LOG("TF2: a stock Anbernic card at %s (%s): its games are listed too", mp, dev);
			break;
		}
		fclose(f);
		/* just after switch-on Knulli may still be mounting it: wait up to 5 s
		 * then, if a second card is in at all (later starts don't wait) */
		if (g_paths.sd_present || !file_exists("/dev/mmcblk1") || uptime_s() > 90) break;
		usleep(250000);
	}
}

int plat_init(void) {
	t_start = now_ms();
	const char *root = getenv("ZAPPO_USERDATA");
	sncpy(g_paths.mmc, root && *root ? root : "/userdata", sizeof g_paths.mmc);
	g_paths.sd[0] = 0;
	g_paths.sd_present = 0;
	g_paths.sd_stock = 0;
	find_stock_card();
	snfmt(g_paths.cfw, sizeof g_paths.cfw, "%s/system", g_paths.mmc);
	snfmt(g_paths.clove, sizeof g_paths.clove, "%s/zappo-x", g_paths.cfw);
	snfmt(g_paths.music, sizeof g_paths.music, "%s/music", g_paths.mmc);
	snfmt(g_paths.fonts, sizeof g_paths.fonts, "%s/font", g_paths.clove);

	if (getenv("CLOVE_TEST_FRAMES")) { test_frames = atoi(getenv("CLOVE_TEST_FRAMES")); test_started = now_ms(); }
	test_keys = getenv("CLOVE_TEST_KEYS");
	test_key_at = now_ms() + 500;

	crash_handlers();
	find_power();
	open_inputs();
	input_map_defaults(&imap);
	int r = screen_open();
	t_screen = now_ms();
	return r;
}

void plat_quit(void) {
	for (int i = 0; i < ndevs; i++) close(devs[i].fd);
	ndevs = 0;
	/* The screen stays SDL's until the program ends a moment later (Zappo X
	 * quits to start a game); the kernel then hands it to the next program.
	 * Tearing SDL down here unloads the GL driver, whose exit handlers then
	 * crash the program as it ends (Mesa on the twin does). */
}
