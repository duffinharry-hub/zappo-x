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
#include <dirent.h>
#include <ucontext.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "plat_evdev.inc"

#define CPU_DIR "/sys/devices/system/cpu/cpu0/cpufreq"
#define PSY "/sys/class/power_supply"

/* ---------------------------------------------------------------- display */

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static int vsync;
static uint32_t canvas[SCREEN_W * SCREEN_H];
static char video_desc[96] = "no screen";

static int screen_open(void) {
	/* KMS/DRM on the twin, Mali on the handheld: SDL picks what it was built for */
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		LOG("SDL_Init: %s", SDL_GetError());
		return -1;
	}
	SDL_ShowCursor(SDL_DISABLE);
	win = SDL_CreateWindow("Zappo X", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	                       SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
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
	snfmt(video_desc, sizeof video_desc, "%dx%d, %s on %s%s", w, h, ri.name,
	      SDL_GetCurrentVideoDriver(), vsync ? ", vsync" : "");
	LOG("screen: %s", video_desc);
	return 0;
}

uint32_t *plat_canvas(void) { return canvas; }
int plat_has_vsync(void) { return vsync; }

static uint64_t t_start, t_screen;

void plat_present(void) {
	frames_presented++;
	if (!tex) return;
	if (frames_presented == 1)
		LOG("first picture %llu ms after start (screen ready after %llu ms)",
		    (unsigned long long)(now_ms() - t_start), (unsigned long long)(t_screen - t_start));
	SDL_UpdateTexture(tex, NULL, canvas, SCREEN_W * 4);
	/* The first picture goes out three times: after a game, the screen can be
	 * left switched on with nothing to show, and SDL's KMS driver only sets
	 * it up again when a page flip fails - Zappo X draws only when something
	 * changes, so otherwise it stayed black until the clock next ticked. */
	for (int i = frames_presented == 1 ? 3 : 1; i > 0; i--) {
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
	}
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

int plat_init(void) {
	t_start = now_ms();
	const char *root = getenv("ZAPPO_USERDATA");
	sncpy(g_paths.mmc, root && *root ? root : "/userdata", sizeof g_paths.mmc);
	g_paths.sd[0] = 0;
	g_paths.sd_present = 0;
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
