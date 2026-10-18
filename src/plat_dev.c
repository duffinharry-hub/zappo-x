/* Clove — RG35XX hardware layer (Linux framebuffer + evdev + sysfs).
 * Built as a static binary so it runs inside GarlicOS's uClibc chroot
 * without needing any of its shared libraries. */
#include "plat.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

#define CPU_DIR "/sys/devices/system/cpu/cpu0/cpufreq"
#define BACKLIGHT "/sys/class/backlight/backlight.2"
#define BATTERY "/sys/class/power_supply/battery"

/* ---------------------------------------------------------------- display */

static int fb_fd = -1;
static uint8_t *fb_mem;
static size_t fb_len;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static int fb_pages = 1, fb_page = 0, fb_vsync = 0;
static uint32_t canvas[SCREEN_W * SCREEN_H];
static int tty_fd = -1;

/* Put the console back in text mode so the next program (RetroArch, the
 * GarlicOS menu) can start its own graphics mode. */
static void restore_console(void) {
	if (tty_fd < 0) return;
	ioctl(tty_fd, KDSETMODE, KD_TEXT);
	close(tty_fd);
	tty_fd = -1;
}

static void crash_handler(int sig) {
	if (tty_fd >= 0) ioctl(tty_fd, KDSETMODE, KD_TEXT);
	signal(sig, SIG_DFL);
	raise(sig);
}

/* Test hook: CLOVE_FAKEFB=<file> renders into memory and dumps the last frame
 * there as raw RGB565. Never set on a device. */
static const char *fake_fb;
#include "plat_evdev.inc"

static void fb_open(void) {
	fake_fb = getenv("CLOVE_FAKEFB");
	if (fake_fb) {
		vinfo.xres = vinfo.xres_virtual = SCREEN_W;
		vinfo.yres = vinfo.yres_virtual = SCREEN_H;
		vinfo.bits_per_pixel = 16;
		vinfo.red.offset = 11; vinfo.red.length = 5;
		vinfo.green.offset = 5; vinfo.green.length = 6;
		vinfo.blue.offset = 0; vinfo.blue.length = 5;
		finfo.line_length = SCREEN_W * 2;
		fb_len = (size_t)finfo.line_length * SCREEN_H;
		fb_mem = calloc(1, fb_len);
		LOG("using a fake framebuffer for testing");
		return;
	}
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd < 0) { LOG("cannot open /dev/fb0: %s", strerror(errno)); return; }
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 || ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		LOG("framebuffer ioctl failed");
		close(fb_fd); fb_fd = -1; return;
	}
	fb_len = finfo.smem_len;
	fb_mem = mmap(NULL, fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_mem == MAP_FAILED) { LOG("framebuffer mmap failed"); fb_mem = NULL; close(fb_fd); fb_fd = -1; return; }
	size_t page_bytes = (size_t)finfo.line_length * vinfo.yres;
	if (vinfo.yres_virtual >= vinfo.yres * 2 && fb_len >= page_bytes * 2) fb_pages = 2;
	uint32_t dummy = 0;
	fb_vsync = ioctl(fb_fd, FBIO_WAITFORVSYNC, &dummy) == 0;
	LOG("fb0 %ux%u virt %ux%u %ubpp line %u pages %d vsync %d rgb off %u/%u/%u",
	    vinfo.xres, vinfo.yres, vinfo.xres_virtual, vinfo.yres_virtual, vinfo.bits_per_pixel,
	    finfo.line_length, fb_pages, fb_vsync, vinfo.red.offset, vinfo.green.offset, vinfo.blue.offset);
}

static inline uint32_t pack_generic(uint32_t c) {
	uint32_t r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
	return ((r >> (8 - vinfo.red.length)) << vinfo.red.offset) |
	       ((g >> (8 - vinfo.green.length)) << vinfo.green.offset) |
	       ((b >> (8 - vinfo.blue.length)) << vinfo.blue.offset);
}

static void convert_to(uint8_t *dst) {
	int w = MIN(SCREEN_W, (int)vinfo.xres), h = MIN(SCREEN_H, (int)vinfo.yres);
	int bpp = vinfo.bits_per_pixel;
	for (int y = 0; y < h; y++) {
		const uint32_t *src = canvas + y * SCREEN_W;
		uint8_t *row = dst + (size_t)y * finfo.line_length;
		if (bpp == 16 && vinfo.red.offset == 11 && vinfo.green.offset == 5 && vinfo.blue.offset == 0) {
			/* ordered dithering hides the banding a 16-bit panel shows in dark gradients */
			static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
			uint16_t *d = (uint16_t *)row;
			const uint8_t *bm = BAYER[y & 3];
			for (int x = 0; x < w; x++) {
				uint32_t c = src[x];
				uint32_t t = bm[x & 3];
				uint32_t r = ((c >> 16) & 255) + (t >> 1), g = ((c >> 8) & 255) + (t >> 2), b = (c & 255) + (t >> 1);
				if (r > 255) r = 255;
				if (g > 255) g = 255;
				if (b > 255) b = 255;
				d[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
			}
		} else if (bpp == 32 && vinfo.red.offset == 16 && vinfo.blue.offset == 0) {
			uint32_t *d = (uint32_t *)row;
			for (int x = 0; x < w; x++) d[x] = src[x] | 0xFF000000u;
		} else if (bpp == 32 && vinfo.red.offset == 0 && vinfo.blue.offset == 16) {
			uint32_t *d = (uint32_t *)row;
			for (int x = 0; x < w; x++) {
				uint32_t c = src[x];
				d[x] = 0xFF000000u | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
			}
		} else if (bpp == 16) {
			uint16_t *d = (uint16_t *)row;
			for (int x = 0; x < w; x++) d[x] = (uint16_t)pack_generic(src[x]);
		} else if (bpp == 24) {
			for (int x = 0; x < w; x++) {
				uint32_t c = pack_generic(src[x]);
				row[x * 3] = c & 255; row[x * 3 + 1] = (c >> 8) & 255; row[x * 3 + 2] = (c >> 16) & 255;
			}
		} else {
			uint32_t *d = (uint32_t *)row;
			for (int x = 0; x < w; x++) d[x] = pack_generic(src[x]);
		}
	}
}

uint32_t *plat_canvas(void) { return canvas; }
int plat_has_vsync(void) { return fb_vsync || fb_pages == 2; }

void plat_present(void) {
	if (!fb_mem) return;
	frames_presented++;
	if (fake_fb) { convert_to(fb_mem); return; }
	size_t page_bytes = (size_t)finfo.line_length * vinfo.yres;
	if (fb_pages == 2) {
		int next = fb_page ^ 1;
		convert_to(fb_mem + page_bytes * (size_t)next);
		vinfo.yoffset = vinfo.yres * (unsigned)next;
		vinfo.xoffset = 0;
		if (fb_vsync) { uint32_t z = 0; ioctl(fb_fd, FBIO_WAITFORVSYNC, &z); }
		if (ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo) == 0) fb_page = next;
		else { fb_pages = 1; convert_to(fb_mem); }
	} else {
		if (fb_vsync) { uint32_t z = 0; ioctl(fb_fd, FBIO_WAITFORVSYNC, &z); }
		convert_to(fb_mem + page_bytes * (size_t)fb_page);
	}
}

/* ------------------------------------------------------------------ sysfs */

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

int plat_battery(void) { return read_int(BATTERY "/capacity", -1); }
int plat_charging(void) { return read_int(BATTERY "/charger_online", 0) > 0; }

static int bl_max(void) {
	int m = read_int(BACKLIGHT "/max_brightness", 255);
	return m > 0 ? m : 255;
}
int plat_brightness_get(void) {
	int v = read_int(BACKLIGHT "/brightness", -1);
	if (v < 0) return 7;
	return CLAMP((v * 10 + bl_max() / 2) / bl_max(), 1, 10);
}
static int bl_saved = -1;
void plat_brightness_set(int level, int persist) {
	level = CLAMP(level, 1, 10);
	int v = MAX(1, bl_max() * level / 10);
	write_int(BACKLIGHT "/brightness", v);
	if (persist) {
		/* GarlicOS restores this file at boot, so the level survives a reboot. */
		char p[512];
		snfmt(p, sizeof p, "%s/brightness", g_paths.cfw);
		write_int(p, v);
	}
}
/* the backlight: 0 = as set, 1 = very dim (the lock screen clock), 2 = off */
static int scr_mode;
static void screen_mode(int mode) {
	if (mode == scr_mode) return;
	if (scr_mode == 0) bl_saved = read_int(BACKLIGHT "/brightness", -1);
	if (mode == 0) {
		if (bl_saved >= 0) write_int(BACKLIGHT "/brightness", bl_saved);
	} else {
		write_int(BACKLIGHT "/brightness", mode == 1 ? MAX(1, bl_max() / 16) : 0);
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
	/* sort ascending */
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
	char gov[32] = "?";
	FILE *f = fopen(CPU_DIR "/scaling_governor", "r");
	if (f) { if (fscanf(f, "%31s", gov) != 1) sncpy(gov, "?", sizeof gov); fclose(f); }
	long mem_kb = 0, avail_kb = 0;
	f = fopen("/proc/meminfo", "r");
	if (f) {
		char line[128];
		while (fgets(line, sizeof line, f)) {
			if (str_starts_with(line, "MemTotal:")) sscanf(line + 9, "%ld", &mem_kb);
			if (str_starts_with(line, "MemFree:") && !avail_kb) sscanf(line + 8, "%ld", &avail_kb);
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
	      "CPU: %d MHz, governor %s\nMemory: %ld MB free of %ld MB\nScreen: %ux%u, %u-bit, %s\nInputs: %s",
	      plat_cpu_cur() / 1000, gov, avail_kb / 1024, mem_kb / 1024, vinfo.xres, vinfo.yres,
	      vinfo.bits_per_pixel, fb_pages == 2 ? "double-buffered" : (fb_vsync ? "vsync" : "single buffer"),
	      ndevs ? inputs : "none found");
}

void plat_power(int reboot) {
	sync();
	char cmd[512];
	snfmt(cmd, sizeof cmd, "mount -o remount,ro %s; mount -o remount,ro %s; %s -f",
	      g_paths.mmc, g_paths.sd, reboot ? "reboot" : "poweroff");
	if (system(cmd) != 0) LOG("power command failed");
	for (;;) sleep(5);
}

/* -------------------------------------------------------------- lifecycle */

int plat_init(void) {
	sncpy(g_paths.mmc, "/mnt/mmc", sizeof g_paths.mmc);
	sncpy(g_paths.sd, "/mnt/SDCARD", sizeof g_paths.sd);
	snfmt(g_paths.cfw, sizeof g_paths.cfw, "%s/CFW", g_paths.mmc);
	snfmt(g_paths.clove, sizeof g_paths.clove, "%s/clove", g_paths.cfw);
	/* TF2 counts as present when it is a separate mount with something on it. */
	struct stat a, b;
	g_paths.sd_present = stat(g_paths.sd, &a) == 0 && stat("/mnt", &b) == 0 && a.st_dev != b.st_dev;
	if (g_paths.sd_present) {
		/* an empty mount point (TF2 boot mode, or no card) doesn't count */
		DIR *d = opendir(g_paths.sd);
		int any = 0;
		struct dirent *e;
		while (d && (e = readdir(d)))
			if (e->d_name[0] != '.') { any = 1; break; }
		if (d) closedir(d);
		g_paths.sd_present = any;
	}
	/* Clove's TF2 boot binds the TF2 card (mmcblk1) over /mnt/mmc */
	FILE *mf = fopen("/proc/mounts", "r");
	if (mf) {
		char line[512], src[256], mnt[256];
		while (fgets(line, sizeof line, mf))
			if (sscanf(line, "%255s %255s", src, mnt) == 2 && strcmp(mnt, g_paths.mmc) == 0 && strstr(src, "mmcblk1")) g_paths.mmc_is_tf2 = 1;
		fclose(mf);
	}

	if (getenv("CLOVE_TEST_FRAMES")) { test_frames = atoi(getenv("CLOVE_TEST_FRAMES")); test_started = now_ms(); }
	test_keys = getenv("CLOVE_TEST_KEYS");
	test_key_at = now_ms() + 500;
	/* Graphics mode stops the text console drawing over the menu. It MUST be
	 * undone on exit: while the console is left in graphics mode the kernel
	 * ignores virtual-terminal switches, and the SDL used by RetroArch and the
	 * GarlicOS menu waits forever for one at start-up (a hang on the loading
	 * screen). See restore_console(). */
	tty_fd = open("/dev/tty0", O_RDWR);
	if (tty_fd >= 0) {
		ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
		signal(SIGSEGV, crash_handler);
		signal(SIGBUS, crash_handler);
		signal(SIGILL, crash_handler);
		signal(SIGFPE, crash_handler);
		signal(SIGABRT, crash_handler);
		signal(SIGTERM, crash_handler);
	}
	fb_open();
	open_inputs();
	input_map_defaults(&imap);
	return fb_mem ? 0 : -1;
}

void plat_quit(void) {
	/* Leave the last frame (the "Loading…" screen) on page 0, where
	 * RetroArch's SDL video expects the visible buffer to be. */
	if (fb_mem && !fake_fb && fb_pages == 2 && fb_page != 0) {
		convert_to(fb_mem);
		vinfo.yoffset = 0;
		ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
	}
	for (int i = 0; i < ndevs; i++) close(devs[i].fd);
	if (fake_fb && fb_mem) {
		FILE *f = fopen(fake_fb, "wb");
		if (f) { fwrite(fb_mem, 1, fb_len, f); fclose(f); }
		free(fb_mem);
		fb_mem = NULL;
	}
	if (fb_mem) munmap(fb_mem, fb_len);
	if (fb_fd >= 0) close(fb_fd);
	restore_console();
}
