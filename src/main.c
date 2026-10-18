/* Clove — a fast, customisable launcher for GarlicOS on the Anbernic RG35XX.
 *
 * GarlicOS's start script runs CFW/main in a loop. Clove's wrapper puts this
 * program there instead: it shows the menu, and when a game is chosen it
 * writes CFW/retroarch/command.sh and exits so the start loop can run it.
 * A non-zero exit makes the wrapper fall back to the original GarlicOS menu. */
#include "config.h"
#include "data.h"
#include "font.h"
#include "import.h"
#include "music.h"
#include "plat.h"
#include "ui.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The program in three parts, so a browser can drive it one frame at a time
 * (plat_web.c) while the handheld runs the usual loop in main(). */
static uint64_t last_frame;
double app_render_ms; /* how long the last picture took to draw (speed tests) */

int app_start(void) {
	if (plat_init() != 0) {
		fprintf(stderr, "clove: no usable display\n");
		plat_quit();
		return 2;
	}
	mkdir_p(g_paths.clove);
	char p[512];
	snfmt(p, sizeof p, "%s/clove.log", g_paths.clove);
	log_open(p);
	LOG("Zappo %s starting (%s)", CLOVE_VERSION, g_paths.mmc_is_tf2 ? "running from the TF2 card" : g_paths.sd_present ? "TF1, with a TF2 card" : "TF1 only");

	settings_load();
	input_load();
	music_init(g_set.music_volume, g_set.music_shuffle, g_set.music_repeat);
	music_set_levelling(g_set.music_level);
	if (g_set.music_resume) {
		/* the song from before the last game or restart, paused where it was */
		char st[600];
		snfmt(st, sizeof st, "%s/music-state.tsv", g_paths.clove);
		if (music_load_state(st)) LOG("music: picked up where it left off");
	}
	/* Browsing needs little CPU; games set their own speed in command.sh. */
	plat_cpu_set(NULL, g_set.menu_cpu, 0);

	data_init();
	ImportResult imp;
	import_run(&imp);
	data_scan();
	ui_init();
	{
		char msg[200];
		import_summary(&imp, 0, msg, sizeof msg);
		/* a quiet word when the handheld restarted on its own (main.sh leaves the note) */
		char tp[512];
		snfmt(tp, sizeof tp, "%s/trouble.txt", g_paths.clove);
		char *trouble = file_read_all(tp, NULL);
		if (trouble) {
			size_t l = strlen(trouble);
			while (l && (trouble[l - 1] == '\n' || trouble[l - 1] == '\r')) trouble[--l] = 0;
			if (l) ui_notice(trouble);
			free(trouble);
			unlink(tp);
		} else if (msg[0]) ui_notice(msg);
		else if (g_set.new_looks) ui_notice("Meet Zappo: new look, theme and picture. Settings > Look changes any of it");
	}
	if (!font_get(20)) {
		LOG("no font could be loaded (looked in the theme folder and CFW/font)");
		plat_quit();
		return 3;
	}
	last_frame = 0;
	return 0;
}

/* One pass: input, update, and a new picture when something changed.
 * Returns 1 while the menu is running. `wait` lets it sleep when idle. */
int app_frame(int wait) {
	InputEvent ev;
	while (ui_state() == UI_RUN && plat_poll(&ev)) ui_event(&ev);
	ui_update();
	if (ui_state() != UI_RUN) return 0;
	if (ui_dirty()) {
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		ui_render();
		clock_gettime(CLOCK_MONOTONIC, &t1);
		app_render_ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
		plat_present();
		if (wait && !plat_has_vsync() && !plat_is_host()) {
			/* no vsync available: cap at ~60 fps */
			uint64_t now = plat_ticks();
			if (last_frame && now - last_frame < 16) usleep((useconds_t)(16 - (now - last_frame)) * 1000);
			last_frame = plat_ticks();
		}
	} else if (wait) {
		plat_wait(ui_wait_ms());
	}
	return 1;
}

/* Saves everything and lets go of the screen and sound.  The exit code:
 * non-zero makes the wrapper open the GarlicOS menu instead. */
int app_end(void) {
	if (ui_state() == UI_LAUNCH) {
		ui_render(); /* loading screen stays up until RetroArch draws */
		plat_present();
	}
	settings_save();
	ui_save_music();
	music_shutdown(); /* frees the sound device before RetroArch starts */
	LOG("Clove exiting (%s)", ui_state() == UI_LAUNCH ? "launching" : ui_state() == UI_FALLBACK ? "buttons not recognised" : "quit");
	plat_quit();
	return ui_state() == UI_FALLBACK ? 4 : 0;
}

#ifndef ZAPPO_NO_MAIN
int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	int r = app_start();
	if (r) return r;
	while (app_frame(1)) {}
	return app_end();
}
#endif
