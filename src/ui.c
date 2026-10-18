#include "ui.h"
#include "config.h"
#include "data.h"
#include "font.h"
#include "gfx.h"
#include "import.h"
#include "music.h"
#include "musiclib.h"
#include "net.h"
#include "playlists.h"
#include "util.h"
#include "video.h"
#include "../third_party/qrcodegen.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define TOP_H 56
#define GUIDE_H 44
#define CONTENT_Y TOP_H
#define CONTENT_H (SCREEN_H - TOP_H - GUIDE_H)
#define PAD 16

static Font *F_TITLE, *F_BODY, *F_SMALL;
static int g_state = UI_RUN;
static int dirty = 1;
static int animating;
static int pending_loads;
static uint64_t last_tick, last_minute;
static int settings_dirty;
static uint64_t settings_changed_at;

/* ------------------------------------------------------------ image cache */

enum { IM_FIT, IM_COVER };
typedef struct {
	char key[700];
	Image *img;
	uint64_t used;
	int set;
} CacheEnt;
#define CACHE_N 48
static CacheEnt cache[CACHE_N];
static int decode_budget;
static Image *bg_img;

static void cache_clear(void) {
	for (int i = 0; i < CACHE_N; i++) {
		img_free(cache[i].img);
		memset(&cache[i], 0, sizeof cache[i]);
	}
	img_free(bg_img);
	bg_img = NULL;
}

static Image *cached_image(const char *path, int w, int h, int mode) {
	char key[700];
	snfmt(key, sizeof key, "%s|%d|%d|%d", path, w, h, mode);
	for (int i = 0; i < CACHE_N; i++)
		if (cache[i].set && strcmp(cache[i].key, key) == 0) {
			cache[i].used = plat_ticks();
			return cache[i].img;
		}
	if (decode_budget <= 0) { pending_loads = 1; return NULL; }
	decode_budget--;
	Image *src = img_load(path), *out = NULL;
	if (src) {
		out = mode == IM_FIT ? img_fit(src, w, h) : img_cover(src, w, h);
		img_free(src);
	}
	int slot = 0;
	for (int i = 0; i < CACHE_N; i++) {
		if (!cache[i].set) { slot = i; break; }
		if (cache[i].used < cache[slot].used) slot = i;
	}
	img_free(cache[slot].img);
	sncpy(cache[slot].key, key, sizeof cache[slot].key);
	cache[slot].img = out;
	cache[slot].used = plat_ticks();
	cache[slot].set = 1;
	return out;
}

/* Returns 1 if the image is known (loaded or known missing). */
static int cache_known(const char *path, int w, int h, int mode) {
	char key[700];
	snfmt(key, sizeof key, "%s|%d|%d|%d", path, w, h, mode);
	for (int i = 0; i < CACHE_N; i++)
		if (cache[i].set && strcmp(cache[i].key, key) == 0) return 1;
	return 0;
}

/* ------------------------------------------------------------------ views */

typedef enum { V_HOME, V_LIST, V_MENU, V_INPUT, V_SEARCH, V_SHOTS } VType;
enum { LK_SYSTEM, LK_RECENT, LK_FAV, LK_VIDEO };
enum { M_ROOT, M_APPEAR, M_TABS, M_SYSTEMS, M_SYSTEM, M_PERF, M_CONTROLS, M_POWER, M_ABOUT, M_PICK, M_CLOCK, M_SENS, M_MUSIC, M_HOME, M_GAMES, M_WIFI, M_BT, M_SEND, M_LIGHTS, M_ACH };

typedef struct {
	int sel, top;
	float asel, atop;
} Cursor;

typedef struct {
	VType type;
	int kind, sys;
	char dir[512];
	EntryList items;
	Cursor cur;
	int menu, arg;
	uint64_t sel_time;
} View;

static View stack[12];
static int depth;
static View v_recent, v_fav, v_settings;
static View v_video; /* the Videos tab (ui_video.inc) */
static int tab = TAB_CONSOLES;
int ui_tab(void) { return tab; } /* which section is showing (the stick lights follow it) */
static Cursor sys_cur;
static int vis[256], nvis;

/* overlay menu (options / power / confirm) */
enum { OV_NONE, OV_GAME, OV_POWER, OV_QUICK, OV_CONFIRM };
enum { OA_FAV = 1, OA_UNRECENT, OA_SYSSET, OA_CANCEL, OA_SLEEP, OA_OFF, OA_REBOOT, OA_SETTINGS, OA_YES, OA_MOVE,
       OA_STARTOVER, OA_SEARCH, OA_SHOTS, OA_VWATCH, OA_VHELP, OA_VSORT, OA_SURPRISE, OA_VCONT, OA_NOWPLAY, OA_MPAUSE,
       OA_BT_CONNECT, OA_BT_DISCONNECT, OA_BT_FORGET };
typedef struct {
	int kind;
	char title[240];
	int n, sel;
	float asel;
	char label[40][64];
	int act[40];
	int top; /* first visible row when the list scrolls */
	char path[1024];
	char name[256];
	int sys;
	int confirm_action;
} Overlay;
static Overlay ov;

static char toast_msg[160];
static uint64_t toast_until;

/* Music tab (ui_music.inc) */
static uint32_t music_gen_seen;
static int music_np;      /* Now Playing is open */
static int lib_dirty = 1; /* the music library needs reading */
static int lib_scan_req;  /* read it on the next update */
static void draw_music(void);
static void draw_now_playing(void);
static int music_button(int b);
static void music_enter(void);
static void mrows_build(void);
static int music_overlay_action(int act);
static void music_lib_invalidate(void);
static void anim_music_cursors(float k);
static int music_hint_a(const char **label);
static int music_hint_y(const char **label);
static int fav_song_has(const char *path);
static void music_relayout(void);
static int music_is_active(void);
static void music_update(void);
static void video_enter(void);
static int video_overlay_action(int act);
static void surprise_me(void);
static void draw_videos(View *v);
static void video_apply_sort(View *v);
static int video_sys = -1;
static const Entry *vmeta_for;
static int video_rows(void);
static int video_cols(void);
static void push_video_list(const char *dir);
static void video_reclamp(void);
static void draw_track_cover(int ti, int x, int y, int size, int radius);
static uint64_t last_sec;
static int sleep_timer_text(char *out, size_t n);
static uint64_t sleep_end;  /* (ui_music.inc) */
static int batt_measured;   /* (ui_extra.inc) */
static float batt_rate;
static void sleep_timer_set(int minutes);

/* keyboard, Search, screenshots, battery (ui_extra.inc) */
enum { KB_NONE, KB_NEWPL, KB_NEWPL_ADD, KB_SEARCH, KB_WIFI, KB_RA_USER, KB_RA_PASS };
static WifiNet wifi_pending; /* the network a password is being typed for */
static BtDev bt_chosen;      /* the device its options are about */
static WifiNet wifi_forget;  /* the network "Forget?" asks about */
static void kb_open(int purpose, const char *title, const char *initial);
static int kb_button(int b);
static void draw_kb_modal(void);
static void search_open(void);
static void search_button(int b);
static void draw_search(void);
typedef struct { const char *btn, *label; } Hint;
static int video_hints(Hint *h, View *v);
static int search_hints(Hint *h);
static void shots_open(void);
static void shots_button(int b);
static void draw_shots(void);
static int batt_left_text(char *out, size_t n);
static void batt_update(uint64_t now);

/* screensaver */
static int saver;
static uint64_t last_input;
static Image *saver_img;
/* lock screen pictures: .jpg / .png pictures (640 x 480) in CFW/clove/lockscreens */
static char lockpics[48][128];
static int nlockpics = -1, lockpic_cur = -1;
static void lockpics_scan(void) {
	char p[512];
	snfmt(p, sizeof p, "%s/lockscreens", g_paths.clove);
	nlockpics = 0;
	DIR *d = opendir(p);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) && nlockpics < (int)ARRAY_LEN(lockpics)) {
			const char *x = path_ext(e->d_name);
			if (e->d_name[0] == '.' || !(str_ieq(x, "jpg") || str_ieq(x, "jpeg") || str_ieq(x, "png"))) continue;
			sncpy(lockpics[nlockpics++], e->d_name, sizeof lockpics[0]);
		}
		closedir(d);
	}
	for (int i = 1; i < nlockpics; i++)
		for (int j = i; j > 0 && str_icmp_natural(lockpics[j - 1], lockpics[j]) > 0; j--) {
			char t[128];
			memcpy(t, lockpics[j], sizeof t);
			memcpy(lockpics[j], lockpics[j - 1], sizeof t);
			memcpy(lockpics[j - 1], t, sizeof t);
		}
}
/* "01 Sunset Sea.jpg" -> "Sunset Sea" */
static void lockpic_name(const char *file, char *out, size_t n) {
	const char *s = file;
	while (*s >= '0' && *s <= '9') s++;
	if (s != file && *s == ' ') s++;
	else s = file;
	sncpy(out, s, n);
	char *dot = strrchr(out, '.');
	if (dot) *dot = 0;
}
/* the picture to show now ("" = the theme's wallpaper) */
static void lockpic_path(char *out, size_t n) {
	out[0] = 0;
	if (!g_set.lock_pic[0]) return;
	if (nlockpics < 0) lockpics_scan();
	const char *f = NULL;
	if (!strcmp(g_set.lock_pic, "*")) {
		if (nlockpics > 0) {
			if (lockpic_cur < 0 || lockpic_cur >= nlockpics) lockpic_cur = (int)(plat_ticks() % (uint64_t)nlockpics);
			f = lockpics[lockpic_cur];
		}
	} else {
		f = g_set.lock_pic;
	}
	if (f) {
		snfmt(out, n, "%s/lockscreens/%s", g_paths.clove, f);
		if (!file_exists(out)) out[0] = 0;
	}
}
/* shuffle: a different picture each time the screensaver or lock screen comes up */
static void lockpic_next(void) {
	if (strcmp(g_set.lock_pic, "*")) return;
	if (nlockpics < 0) lockpics_scan();
	if (nlockpics < 2) return;
	int r = (int)((plat_ticks() / 7 + (uint64_t)time(NULL)) % (uint64_t)(nlockpics - 1));
	lockpic_cur = lockpic_cur < 0 ? r : (lockpic_cur + 1 + r) % nlockpics;
	if (saver_img) { img_free(saver_img); saver_img = NULL; }
}
static void saver_start(void) { lockpic_next(); saver = 1; dirty = 1; }

/* clock fonts: .ttf / .otf files in CFW/clove/fonts (Settings > Screen & power) */
static char clockfonts[24][64];
static int nclockfonts = -1;
static void clockfonts_scan(void) {
	char p[512];
	snfmt(p, sizeof p, "%s/fonts", g_paths.clove);
	nclockfonts = 0;
	DIR *d = opendir(p);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) && nclockfonts < (int)ARRAY_LEN(clockfonts)) {
			const char *x = path_ext(e->d_name);
			if (e->d_name[0] == '.' || !(str_ieq(x, "ttf") || str_ieq(x, "otf"))) continue;
			sncpy(clockfonts[nclockfonts++], e->d_name, sizeof clockfonts[0]);
		}
		closedir(d);
	}
	for (int i = 1; i < nclockfonts; i++)
		for (int j = i; j > 0 && str_icmp_natural(clockfonts[j - 1], clockfonts[j]) > 0; j--) {
			char t[64];
			memcpy(t, clockfonts[j], sizeof t);
			memcpy(clockfonts[j], clockfonts[j - 1], sizeof t);
			memcpy(clockfonts[j - 1], t, sizeof t);
		}
}
/* the big clock's font at a size (the theme's own when none is chosen) */
static Font *clock_font(int px) {
	if (!g_set.clock_font[0]) return font_get(px);
	char p[600];
	snfmt(p, sizeof p, "%s/fonts/%s", g_paths.clove, g_set.clock_font);
	return file_exists(p) ? font_get_file(p, px) : font_get(px);
}
static void clock_font_name(char *out, size_t n) {
	if (!g_set.clock_font[0]) { sncpy(out, "Theme", n); return; }
	sncpy(out, g_set.clock_font, n);
	char *dot = strrchr(out, '.');
	if (dot) *dot = 0;
}

/* The lock screen: the wallpaper, clock and what's playing on a very dim
 * screen (or the screen off). Music keeps playing. Pressing L1 and R1
 * together unlocks; any other button lights the screen up for a few seconds
 * to show the time and how to unlock. */
static int sleeping;              /* locked */
static int lock_down[2];          /* L1 / R1 held while locked */
static uint64_t lock_peek_until;  /* lit up until then (0 = dark) */
#define LOCK_PEEK_MS 6000
static void lock_dark(void) {
	lock_peek_until = 0;
	if (g_set.sleep_clock) plat_screen_dim(1);
	else plat_screen_off(1);
}
static void lock_peek(void) {
	if (!lock_peek_until) plat_screen_dim(0); /* full brightness again */
	lock_peek_until = plat_ticks() + LOCK_PEEK_MS;
	if (!lock_peek_until) lock_peek_until = 1;
}
static void sleep_enter(void) {
	if (!saver) lockpic_next(); /* straight from the menu: a fresh picture */
	sleeping = 1;
	memset(lock_down, 0, sizeof lock_down);
	lock_dark();
}
static void sleep_leave(void) {
	sleeping = 0;
	lock_peek_until = 0;
	memset(lock_down, 0, sizeof lock_down);
	plat_screen_dim(0);
}

/* key repeat */
static int held[B_COUNT];
static uint64_t rep_at[B_COUNT], held_at[B_COUNT];

/* input test log */
static char itest[10][96];
static int itest_n;
static char itest_last[64];

/* launch info for the loading screen */
static char launch_name[256], launch_art[1024];
static int launch_sys = -1;

/* a launch waiting for "Start anyway" (missing BIOS) */
static char pend_path[1024], pend_name[256];
static int pend_sys = -1, pend_kind;

/* the Import item a console is being picked for */
static char pick_path[1024];
static int pick_vis[300], pick_n;

static View *top(void) { return &stack[depth - 1]; }

static void toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void toast(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(toast_msg, sizeof toast_msg, fmt, ap);
	va_end(ap);
	toast_until = plat_ticks() + 2200;
	dirty = 1;
}

static void mark_settings(void) {
	settings_dirty = 1;
	settings_changed_at = plat_ticks();
	dirty = 1;
}

/* ------------------------------------------------------------------ fonts */

static const char *ui_font_dir(void) {
	static char d[300];
	if (g_paths.fonts[0]) return g_paths.fonts;
	snfmt(d, sizeof d, "%s/font", g_paths.cfw);
	return d;
}

static void load_fonts(void) {
	font_clear_faces();
	font_set_weight(g_set.font_weight);
	char p[768];
	snfmt(p, sizeof p, "%s/%s", g_theme.dir, g_theme.font_file);
	if (font_add_face(p, 0) < 0) {
		snfmt(p, sizeof p, "%s/%s", ui_font_dir(), g_theme.font_file);
		if (font_add_face(p, 0) < 0) {
			static const char *fallbacks[] = {"FiraSansCondensed-Regular.ttf", "NotoSans-Regular.ttf", "Oswald-Regular.otf"};
			for (int i = 0; i < ARRAY_LEN(fallbacks); i++) {
				snfmt(p, sizeof p, "%s/%s", ui_font_dir(), fallbacks[i]);
				if (font_add_face(p, 0) >= 0) break;
			}
		}
	}
	static const char *extra[] = {"NotoSans-Regular.ttf", "NotoSansSC-Regular.otf", "NotoSansTC-Regular.otf", "NotoSansThai-Regular.ttf"};
	for (int i = 0; i < ARRAY_LEN(extra); i++) {
		snfmt(p, sizeof p, "%s/%s", ui_font_dir(), extra[i]);
		font_add_face(p, 1);
	}
	F_TITLE = font_get(g_theme.font_title);
	F_BODY = font_get(g_theme.font_body);
	F_SMALL = font_get(g_theme.font_small);
}

static void style_apply(void);
static void apply_theme(void) {
	theme_load(g_set.theme);
	style_apply();
	if (saver_img) { img_free(saver_img); saver_img = NULL; } /* the theme may bring its own wallpaper */
	cache_clear();
	load_fonts();
	dirty = 1;
}

/* ----------------------------------------------------------- list helpers */

static int game_cols(void) { return 4; }
static int list_rows(void) { return g_theme.list_rows; }

static void clamp_cursor(Cursor *c, int n, int rows, int cols) {
	if (n <= 0) { c->sel = c->top = 0; return; }
	c->sel = CLAMP(c->sel, 0, n - 1);
	if (cols <= 1) {
		if (c->sel < c->top) c->top = c->sel;
		if (c->sel >= c->top + rows) c->top = c->sel - rows + 1;
		c->top = CLAMP(c->top, 0, MAX(0, n - rows));
	} else {
		int row = c->sel / cols;
		if (row < c->top) c->top = row;
		if (row >= c->top + rows) c->top = row - rows + 1;
		int total_rows = (n + cols - 1) / cols;
		c->top = CLAMP(c->top, 0, MAX(0, total_rows - rows));
	}
}

static void load_list(View *v) {
	if (v->kind == LK_RECENT) data_list_recents(&v->items);
	else if (v->kind == LK_FAV) data_list_favorites(&v->items);
	else data_list_system(v->sys, v->dir, &v->items);
}

static void refresh_visible(void) {
	nvis = data_visible_systems(vis, ARRAY_LEN(vis));
	sys_cur.sel = CLAMP(sys_cur.sel, 0, MAX(0, nvis - 1));
}

static int tab_list(int *out) {
	int n = 0;
	for (int i = 0; i < TAB_COUNT; i++)
		if (g_set.tab_visible[i] || i == TAB_CONSOLES) out[n++] = i;
	return n;
}

static void enter_tab(int t) {
	tab = t;
	if (t == TAB_RECENT) { v_recent.type = V_LIST; v_recent.kind = LK_RECENT; load_list(&v_recent); clamp_cursor(&v_recent.cur, v_recent.items.n, list_rows(), 1); }
	if (t == TAB_FAVORITES) { v_fav.type = V_LIST; v_fav.kind = LK_FAV; load_list(&v_fav); clamp_cursor(&v_fav.cur, v_fav.items.n, list_rows(), 1); }
	if (t == TAB_SETTINGS) { v_settings.type = V_MENU; v_settings.menu = M_ROOT; }
	if (t == TAB_MUSIC) music_enter();
	if (t == TAB_VIDEOS) video_enter();
	dirty = 1;
}

static void push_list(int sys, const char *dir) {
	if (depth >= ARRAY_LEN(stack)) return;
	View *v = &stack[depth++];
	memset(v, 0, sizeof *v);
	v->type = V_LIST;
	v->kind = LK_SYSTEM;
	v->sys = sys;
	sncpy(v->dir, dir ? dir : "", sizeof v->dir);
	load_list(v);
	v->sel_time = plat_ticks();
	dirty = 1;
}

static void push_menu(int menu, int arg) {
	if (depth >= ARRAY_LEN(stack)) return;
	View *v = &stack[depth++];
	memset(v, 0, sizeof *v);
	v->type = V_MENU;
	v->menu = menu;
	v->arg = arg;
	dirty = 1;
}

static void send_done(void);

static void pop_view(void) {
	if (depth <= 1) return;
	View *v = top();
	if (v->type == V_MENU && v->menu == M_SEND) send_done();
	el_clear(&v->items);
	depth--;
	dirty = 1;
	/* lists under us may have changed (favourites toggled, etc.) */
	if (top()->type == V_HOME) {
		refresh_visible();
		if (tab == TAB_FAVORITES || tab == TAB_RECENT) enter_tab(tab);
	} else if (top()->type == V_LIST) {
		for (int i = 0; i < top()->items.n; i++) top()->items.v[i].fav = data_is_fav(top()->items.v[i].path);
	}
}

/* The list the user is currently interacting with, if any. */
static View *active_list(void) {
	View *v = top();
	if (v->type == V_LIST) return v;
	if (v->type == V_HOME && tab == TAB_RECENT) return &v_recent;
	if (v->type == V_HOME && tab == TAB_FAVORITES) return &v_fav;
	if (v->type == V_HOME && tab == TAB_VIDEOS) return &v_video;
	return NULL;
}

/* -------------------------------------------------------------- launching */

static void save_now(void) {
	settings_save();
	settings_dirty = 0;
}

static void open_overlay(int kind, const char *title);
static void ov_add(const char *label, int act);

static void launch_now(const Entry *e, int from_kind);

static void launch_entry(const Entry *e, int from_kind) {
	char files[256];
	if (e->sys >= 0 && data_bios_status(e->sys, files, sizeof files) == BIOS_REQUIRED_MISSING) {
		sncpy(pend_path, e->path, sizeof pend_path);
		sncpy(pend_name, e->name, sizeof pend_name);
		pend_sys = e->sys;
		pend_kind = from_kind;
		char t[96];
		snfmt(t, sizeof t, "BIOS missing: %s", files);
		open_overlay(OV_CONFIRM, t);
		ov.confirm_action = 1003;
		ov_add("Cancel", OA_CANCEL);
		ov_add("Start anyway", OA_YES);
		return;
	}
	launch_now(e, from_kind);
}

/* an MP4 whose index has to be moved before the video player can open it */
static VideoPrep *vprep;
static char vprep_path[1024], vprep_name[256];
static int vprep_sys = -1, vprep_kind;
static uint64_t vprep_t0;

static void launch_now(const Entry *e, int from_kind) {
	char err[256];
	if (e->sys >= 0 && strstr(data_sys_core(e->sys), "ffmpeg")) {
		if (video_file_size(e->path) > VIDEO_MAX_BYTES) {
			toast("Over 2 GB: too big for the video player. Make a smaller copy in HandBrake.");
			return;
		}
		if (video_needs_prep(e->path)) {
			vprep = video_prep_start(e->path, err, sizeof err);
			if (!vprep) { toast("%s", err); return; }
			sncpy(vprep_path, e->path, sizeof vprep_path);
			sncpy(vprep_name, e->name, sizeof vprep_name);
			vprep_sys = e->sys;
			vprep_kind = from_kind;
			vprep_t0 = plat_ticks();
			dirty = 1;
			return;
		}
	}
	if (data_launch(e, err, sizeof err) != 0) { toast("%s", err); return; }
	if (e->sys >= 0 && strstr(data_sys_core(e->sys), "ffmpeg")) pc_bump(e->path); /* watched */
	sncpy(launch_name, e->name, sizeof launch_name);
	launch_sys = e->sys;
	if (!data_art_path(e, launch_art, sizeof launch_art)) launch_art[0] = 0;
	state_set("tab", from_kind == LK_RECENT ? "recent" : from_kind == LK_FAV ? "favorites" : from_kind == LK_VIDEO ? "videos" : "consoles");
	state_set("system", e->sys >= 0 ? g_sys[e->sys].code : NULL);
	state_set("path", e->path);
	View *v = top();
	state_set("dir", v->type == V_LIST && (v->kind == LK_SYSTEM || v->kind == LK_VIDEO) && v->sys == e->sys ? v->dir : "");
	save_now();
	g_state = UI_LAUNCH;
	dirty = 1;
}

/* ----------------------------------------------------------------- drawing */

static void draw_star(int cx, int cy, int r, uint32_t c);
static void draw_note(int x, int y, int h, uint32_t c);
#include "ui_style.inc"

static int shelf_screen(void);
static void draw_background(void) {
	uint32_t *cv = plat_canvas();
	if (shelf_screen()) { draw_tint_background(); return; }
	if (!bg_img && glass() && !g_theme.background_sharp) bg_img = glass_bg_make();
	if (!bg_img) {
		bg_img = img_new(SCREEN_W, SCREEN_H);
		gfx_bind(bg_img->px, SCREEN_W, SCREEN_H);
		gfx_vgradient(0, 0, SCREEN_W, SCREEN_H, g_theme.bg, g_theme.bg2);
		if (g_theme.background[0]) {
			Image *src = img_load(g_theme.background);
			if (src) {
				Image *c = img_cover(src, SCREEN_W, SCREEN_H);
				gfx_blit(c, 0, 0, 255);
				img_free(c);
				img_free(src);
				if (g_theme.background_dim) gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_alpha(g_theme.bg2, g_theme.background_dim));
			}
		}
		gfx_bind(cv, SCREEN_W, SCREEN_H);
	}
	memcpy(cv, bg_img->px, sizeof(uint32_t) * SCREEN_W * SCREEN_H);
}

static void draw_star(int cx, int cy, int r, uint32_t c) {
	float px[10], py[10];
	for (int i = 0; i < 10; i++) {
		float a = -1.5707963f + i * 0.6283185f;
		float rr = (i & 1) ? r * 0.45f : (float)r;
		px[i] = cx + cosf(a) * rr;
		py[i] = cy + sinf(a) * rr;
	}
	for (int y = cy - r - 1; y <= cy + r + 1; y++) {
		for (int x = cx - r - 1; x <= cx + r + 1; x++) {
			int hits = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++) {
					float fx = x + (sx + 0.5f) / 4, fy = y + (sy + 0.5f) / 4;
					int in = 0;
					for (int i = 0, j = 9; i < 10; j = i++)
						if (((py[i] > fy) != (py[j] > fy)) && (fx < (px[j] - px[i]) * (fy - py[i]) / (py[j] - py[i]) + px[i])) in = !in;
					hits += in;
				}
			if (hits) gfx_rect(x, y, 1, 1, col_alpha(c, (int)((c >> 24) * hits / 16)));
		}
	}
}

static void draw_note(int x, int y, int h, uint32_t c);
static void draw_folder(int x, int y, int h, uint32_t c) {
	int w = h * 5 / 4;
	gfx_rrect(x, y + h / 6, w / 2, h / 4, 2, c);
	gfx_rrect(x, y + h / 3, w, h * 2 / 3, 3, c);
}

static void draw_battery(int right, int y) {
	if (g_set.battery_mode == 2) return;
	int pct = plat_battery();
	int chg = plat_charging();
	int bw = 28, bh = 14;
	int x = right - bw - 3;
	uint32_t col = (pct >= 0 && pct < 15 && !chg) ? col_parse("#ff5a5a", 0) : g_theme.text;
	gfx_rrect_stroke(x, y, bw, bh, 4, 2, col_alpha(col, 230));
	gfx_rrect(x + bw, y + 4, 3, bh - 8, 1, col_alpha(col, 230));
	int fill = pct < 0 ? bw - 6 : (bw - 6) * CLAMP(pct, 0, 100) / 100;
	if (fill > 0) gfx_rrect(x + 3, y + 3, fill, bh - 6, 2, chg ? g_theme.accent : col);
	if (g_set.battery_mode == 0 && pct >= 0) {
		char b[16];
		snfmt(b, sizeof b, "%d%%", pct);
		int tw = text_width(F_SMALL, b);
		text_draw(F_SMALL, x - tw - 6, y + bh / 2 - font_height(F_SMALL) / 2, b, g_theme.text_dim);
	}
}

static int draw_status(void) {
	/* clock + battery at the top right; returns the left edge used */
	int right = SCREEN_W - PAD;
	int y = TOP_H / 2 - 7;
	draw_battery(right, y);
	int used = g_set.battery_mode == 2 ? 0 : (g_set.battery_mode == 0 ? 80 : 40);
	right -= used;
	MusicInfo mus;
	music_info(&mus);
	if (mus.active) {
		right -= 18 + (used ? 10 : 0);
		draw_note(right, TOP_H / 2 - 9, 18, mus.paused ? g_theme.text_dim : g_theme.accent);
		used = 1;
	}
	if (g_set.clock_mode != 2) {
		time_t t = time(NULL) + g_set.clock_offset;
		struct tm tmv;
		localtime_r(&t, &tmv);
		char b[16];
		if (g_set.clock_mode == 1) strftime(b, sizeof b, "%I:%M %p", &tmv);
		else strftime(b, sizeof b, "%H:%M", &tmv);
		if (b[0] == '0' && g_set.clock_mode == 1) memmove(b, b + 1, strlen(b));
		int tw = text_width(F_SMALL, b);
		right -= tw + (used ? 12 : 0);
		text_draw(F_SMALL, right, TOP_H / 2 - font_height(F_SMALL) / 2, b, g_theme.text);
	}
	return right;
}

static void draw_topbar(const char *title) {
	int limit = draw_status() - 12;
	if (title) {
		gfx_clip(0, 0, limit, TOP_H);
		text_draw_fit(F_TITLE, PAD + 4, TOP_H / 2 - font_height(F_TITLE) / 2, limit - PAD - 4, title, g_theme.text, 0);
		gfx_unclip();
		return;
	}
	/* home: tab strip */
	if (glass()) { draw_glass_tabs(limit); return; }
	int tabs[TAB_COUNT];
	int nt = tab_list(tabs);
	Font *f = F_BODY;
	int room = limit - (PAD - 4) - 2 * nt; /* the clock and battery start at limit */
	int text = 0;
	for (int i = 0; i < nt; i++) text += text_width(f, g_theme.label[tabs[i]]);
	if (text + 24 * nt > room) {
		f = F_SMALL;
		text = 0;
		for (int i = 0; i < nt; i++) text += text_width(f, g_theme.label[tabs[i]]);
	}
	/* the padding round each name shrinks before the names can run into the clock */
	int padw = f == F_BODY ? 24 : 20;
	if (nt && text + padw * nt > room) padw = MAX(6, (room - text) / nt);
	int x = PAD - 4;
	int ph = font_height(f) + 10;
	for (int i = 0; i < nt; i++) {
		const char *lab = g_theme.label[tabs[i]];
		int w = text_width(f, lab) + padw;
		int y = TOP_H / 2 - ph / 2;
		if (tabs[i] == tab) {
			gfx_rrect(x, y, w, ph, ph / 2, g_theme.accent);
			text_draw(f, x + (w - text_width(f, lab)) / 2, y + 5, lab, g_theme.accent_text);
		} else {
			text_draw(f, x + (w - text_width(f, lab)) / 2, y + 5, lab, g_theme.text_dim);
		}
		x += w + 2;
	}
}

static void draw_hint_button(int x, int y, const char *btn) {
	int h = 24;
	int w = (int)strlen(btn) > 1 ? text_width(F_SMALL, btn) + 14 : h;
	gfx_rrect(x, y, w, h, h / 2, g_theme.surface_hi);
	text_draw(F_SMALL, x + (w - text_width(F_SMALL, btn)) / 2, y + h / 2 - font_height(F_SMALL) / 2, btn, g_theme.text);
}
static int hint_width(const Hint *h) {
	int bw = (int)strlen(h->btn) > 1 ? text_width(F_SMALL, h->btn) + 14 : 24;
	return bw + 6 + text_width(F_SMALL, h->label);
}

static void draw_guide(const Hint *hints, int n) {
	if (!g_set.show_guide) return;
	int y = SCREEN_H - GUIDE_H + (GUIDE_H - 24) / 2;
	int x = SCREEN_W - PAD;
	if (glass() && n) {
		/* one centred pill */
		int w = 12;
		for (int i = 0; i < n; i++) w += hint_width(&hints[i]) + 16;
		int px = (SCREEN_W - (w + 6)) / 2;
		gfx_rrect(px, y - 6, w + 6, 36, 18, RGBA(10, 8, 18, 170));
		gfx_rrect_stroke(px, y - 6, w + 6, 36, 18, 1, RGBA(255, 255, 255, 44));
		x = px + w + 6 - 10;
	}
	for (int i = n - 1; i >= 0; i--) {
		int w = hint_width(&hints[i]);
		x -= w;
		draw_hint_button(x, y, hints[i].btn);
		int bw = (int)strlen(hints[i].btn) > 1 ? text_width(F_SMALL, hints[i].btn) + 14 : 24;
		text_draw(F_SMALL, x + bw + 6, y + 12 - font_height(F_SMALL) / 2, hints[i].label, g_theme.text_dim);
		x -= 16;
	}
}

static const char *confirm_btn(void) { return g_set.swap_ab ? "B" : "A"; }
static const char *back_btn(void) { return g_set.swap_ab ? "A" : "B"; }

static void draw_sys_icon(int sys, int x, int y, int w, int h, int alpha) {
	char p[768];
	Image *im = NULL;
	if (data_icon_path(sys, p, sizeof p)) im = cached_image(p, w, h, IM_FIT);
	if (im) {
		gfx_blit(im, x + (w - im->w) / 2, y + (h - im->h) / 2, alpha);
	} else if (!pending_loads || !data_icon_path(sys, p, sizeof p)) {
		/* no artwork: a lettered badge */
		int bw = MIN(w, h * 2), bh = MIN(h, w * 2 / 3);
		int bx = x + (w - bw) / 2, by = y + (h - bh) / 2;
		uint32_t fill = g_theme.surface_hi;
		gfx_rrect(bx, by, bw, bh, MIN(bw, bh) / 5, col_alpha(fill, alpha));
		/* letters that stand out from the badge, light or dark (Glass makes it light) */
		int lum = (int)(((fill >> 16) & 255) * 299 + ((fill >> 8) & 255) * 587 + (fill & 255) * 114) / 1000;
		uint32_t ink = lum > 150 ? g_theme.bg2 : g_theme.text;
		Font *f = bh > 60 ? F_TITLE : F_SMALL;
		text_draw_fit(f, bx + 4, by + bh / 2 - font_height(f) / 2, bw - 8, g_sys[sys].short_name, col_alpha(ink, alpha), 1);
	}
}

static void sys_count_label(int sys, char *out, size_t n) {
	int c = g_sys[sys].count;
	if (g_sys[sys].is_import) snfmt(out, n, c == 1 ? "1 item to sort" : "%d items to sort", c);
	else if (c == 0) sncpy(out, "No games yet", n);
	else if (!strcmp(data_sys_core(sys), "/bin/sh")) snfmt(out, n, c == 1 ? "1 item" : "%d items", c);
	else if (!strcmp(g_sys[sys].code, "BLOCKBOX")) snfmt(out, n, c == 1 ? "1 world" : "%d worlds", c);
	else snfmt(out, n, c == 1 ? "1 game" : "%d games", c);
}

static int sys_empty(int sys) { return g_sys[sys].count == 0 && !g_sys[sys].is_import; }

/* "Needs BIOS: disksys.rom" under a console; returns 0 when nothing to say */
static int sys_bios_line(int sys, char *out, size_t n, uint32_t *col) {
	char files[256];
	int st = data_bios_status(sys, files, sizeof files);
	if (st == BIOS_REQUIRED_MISSING) { snfmt(out, n, "Needs BIOS: %s (put it in BIOS or Import)", files); *col = g_theme.favorite; return 1; }
	if (st == BIOS_OPTIONAL_MISSING) { snfmt(out, n, "Optional BIOS: %s", files); *col = g_theme.text_dim; return 1; }
	if (g_sys[sys].is_import) { sncpy(out, "Zappo couldn't tell which console these are for. Open to choose.", n); *col = g_theme.text_dim; return 1; }
	return 0;
}

/* ---- consoles: carousel / grid / list */

/* ------------------------------------------------------------------ shelf */

/* where tile i sits on the shelf: x of its centre, for the cursor at asel */
static float shelf_x(float off, int bw, int sw, int gap) {
	float ao = fabsf(off), d1 = bw / 2.0f + sw / 2.0f + gap;
	float x = ao <= 1 ? ao * d1 : d1 + (ao - 1) * (sw + gap);
	return SCREEN_W / 2.0f + (off < 0 ? -x : x);
}

static uint32_t sys_tint(int sys, int w, int h, int *known) {
	char p[768];
	uint32_t c;
	*known = 1;
	if (data_icon_path(sys, p, sizeof p)) {
		if (tint_cached(p, &c)) return c;
		Image *im = cached_image(p, w, h, IM_FIT);
		if (im) { c = tint_of_image(im); tint_store(p, c); return c; }
		if (pending_loads) *known = 0;
	}
	return tint_of_name(g_sys[sys].name);
}

static void draw_consoles_shelf(void) {
	int r = g_theme.radius + 4;
	const int bw = 236, bh = 188, sw = 150, sh = 120, gap = 16;
	int cyc = CONTENT_Y + 22 + bh / 2;
	int lo = MAX(0, (int)floorf(sys_cur.asel) - 3), hi = MIN(nvis - 1, (int)ceilf(sys_cur.asel) + 3);
	for (int pass = 3; pass >= 0; pass--) {
		for (int i = lo; i <= hi; i++) {
			float off = i - sys_cur.asel, ao = fabsf(off);
			if ((int)(ao + 0.5f) != pass) continue;
			/* two fixed sizes (big in the middle, small beside it): each picture is
			 * only ever decoded at those two sizes, so sliding along stays smooth */
			int big = ao < 0.5f;
			int w = big ? bw : sw, h = big ? bh : sh;
			int x = (int)(shelf_x(off, bw, sw, gap) - w / 2.0f), y = cyc - h / 2;
			if (x > SCREEN_W || x + w < 0) continue;
			int alpha = big ? 255 : (int)(190 - fminf(ao - 1, 1.0f) * 70);
			int known;
			uint32_t tc = sys_tint(vis[i], 200, 140, &known);
			uint32_t fill = col_mix(tc, RGBA(8, 7, 14, 255), big ? 0.35f : 0.5f);
			gfx_shadow(x, y + 6, w, h, r, 12, col_alpha(0x000000, alpha / 2));
			gfx_rrect(x, y, w, h, r, col_alpha(fill, 230 * alpha / 255));
			gfx_rrect_stroke(x, y, w, h, r, 1, RGBA(255, 255, 255, alpha / 4));
			if (i == sys_cur.sel && big) gfx_rrect_stroke(x - 4, y - 4, w + 8, h + 8, r + 4, 3, RGBA(255, 255, 255, 235));
			int iw = w * 82 / 100, ih = h * 74 / 100;
			draw_sys_icon(vis[i], x + (w - iw) / 2, y + (h - ih) / 2, iw, ih, sys_empty(vis[i]) ? alpha * 2 / 5 : alpha);
		}
	}
	int s = vis[sys_cur.sel], known;
	tint_target(sys_tint(s, 200, 140, &known));
	char cnt[32], line[64];
	sys_count_label(s, cnt, sizeof cnt);
	snfmt(line, sizeof line, "%s  \xC2\xB7  %d / %d", cnt, sys_cur.sel + 1, nvis);
	int ty = cyc + bh / 2 + 18;
	text_draw_fit(F_TITLE, PAD + 2, ty + 2, SCREEN_W - PAD * 2, data_sys_name(s), col_alpha(0x000000, 140), 1);
	text_draw_fit(F_TITLE, PAD, ty, SCREEN_W - PAD * 2, data_sys_name(s), 0xFFFFFFFF, 1);
	text_draw_fit(F_SMALL, PAD, ty + font_height(F_TITLE) + 2, SCREEN_W - PAD * 2, line, RGBA(230, 226, 240, 255), 1);
	char bl[200];
	uint32_t bc;
	if (sys_bios_line(s, bl, sizeof bl, &bc))
		text_draw_fit(F_SMALL, PAD, ty + font_height(F_TITLE) + font_height(F_SMALL) + 6, SCREEN_W - PAD * 2, bl, bc, 1);
}

static void draw_consoles(void) {
	if (nvis == 0) {
		text_draw_wrap(F_BODY, 60, CONTENT_Y + 120, SCREEN_W - 120, 2, "No games found yet", g_theme.text, 1);
		text_draw_wrap(F_SMALL, 60, CONTENT_Y + 160, SCREEN_W - 120, 3,
		               "Copy games into Roms/<system> folders on either card, e.g. Roms/GBA or Roms/PS.", g_theme.text_dim, 1);
		return;
	}
	int layout = effective_home_layout();
	int r = g_theme.radius;
	if (layout == HL_SHELF) { draw_consoles_shelf(); return; }
	if (layout == HL_CAROUSEL) {
		int cw = 300, ch = 210, cy = CONTENT_Y + 30;
		int lo = MAX(0, (int)floorf(sys_cur.asel) - 3), hi = MIN(nvis - 1, (int)ceilf(sys_cur.asel) + 3);
		/* draw far cards first so the centre card sits on top */
		for (int pass = 3; pass >= 0; pass--) {
			for (int i = lo; i <= hi; i++) {
				float off = i - sys_cur.asel;
				float ao = fabsf(off);
				if ((int)(ao + 0.5f) != pass) continue;
				float s = 1.0f - fminf(ao, 2.0f) * 0.27f;
				int w = (int)(cw * s), h = (int)(ch * s);
				int x = (int)(SCREEN_W / 2 + off * 282 - w / 2);
				int y = cy + (ch - h) / 2;
				int alpha = (int)(255 - fminf(ao, 2.0f) * 80);
				gfx_shadow(x, y + 4, w, h, r, 8, col_alpha(g_theme.shadow, (int)((g_theme.shadow >> 24) * alpha / 255)));
				if (glass()) { gfx_rrect(x, y, w, h, r, RGBA(10, 8, 18, 120 + alpha / 4)); gfx_rrect_stroke(x, y, w, h, r, 1, RGBA(255, 255, 255, alpha / 4)); }
				else gfx_rrect(x, y, w, h, r, col_mix(g_theme.bg2, g_theme.surface, alpha / 255.0f));
				if (i == sys_cur.sel && ao < 0.5f) gfx_rrect_stroke(x - 3, y - 3, w + 6, h + 6, r + 3, 3, sel_ring());
				/* the picture at one of two sizes, so it isn't re-read while the cards slide */
				int iw = ao < 0.5f ? 220 : 160, ih = ao < 0.5f ? 150 : 110;
				draw_sys_icon(vis[i], x + (w - iw) / 2, y + (h - ih) / 2, iw, ih, sys_empty(vis[i]) ? alpha * 2 / 5 : alpha);
			}
		}
		int s = vis[sys_cur.sel];
		char cnt[32];
		sys_count_label(s, cnt, sizeof cnt);
		text_draw_fit(F_TITLE, PAD, cy + ch + 22, SCREEN_W - PAD * 2, data_sys_name(s), g_theme.text, 1);
		text_draw_fit(F_SMALL, PAD, cy + ch + 22 + font_height(F_TITLE) + 2, SCREEN_W - PAD * 2, cnt, g_theme.text_dim, 1);
		char bl[200];
		uint32_t bc;
		if (sys_bios_line(s, bl, sizeof bl, &bc))
			text_draw_fit(F_SMALL, PAD, cy + ch + 22 + font_height(F_TITLE) + font_height(F_SMALL) + 6, SCREEN_W - PAD * 2, bl, bc, 1);
		/* position dots */
		int dy = CONTENT_Y + CONTENT_H - 22;
		if (nvis <= 24) {
			int dw = nvis * 14;
			for (int i = 0; i < nvis; i++) {
				int on = i == sys_cur.sel;
				gfx_circle(SCREEN_W / 2 - dw / 2 + i * 14 + 7, dy, on ? 4 : 3, on ? g_theme.accent : col_alpha(g_theme.text_dim, 120));
			}
		} else {
			char b[32];
			snfmt(b, sizeof b, "%d / %d", sys_cur.sel + 1, nvis);
			text_draw_fit(F_SMALL, PAD, dy - font_height(F_SMALL) / 2, SCREEN_W - PAD * 2, b, g_theme.text_dim, 1);
		}
	} else if (layout == HL_GRID) {
		int cols = 4, rows = 3, gw = 140, gh = 112, gap = 12;
		int x0 = (SCREEN_W - (cols * gw + (cols - 1) * gap)) / 2;
		int y0 = CONTENT_Y + 10;
		float off = sys_cur.atop * (gh + gap);
		gfx_clip(0, CONTENT_Y, SCREEN_W, CONTENT_H);
		int first_row = MAX(0, (int)floorf(sys_cur.atop) - 1);
		for (int i = first_row * cols; i < nvis && i < (first_row + rows + 2) * cols; i++) {
			int cx = x0 + (i % cols) * (gw + gap);
			int cyy = (int)(y0 + (i / cols) * (gh + gap) - off);
			int sel = i == sys_cur.sel;
			if (sel) gfx_shadow(cx, cyy + 3, gw, gh, r, 8, g_theme.shadow);
			gfx_rrect(cx, cyy, gw, gh, r, sel ? g_theme.surface_hi : g_theme.surface);
			if (sel) gfx_rrect_stroke(cx - 3, cyy - 3, gw + 6, gh + 6, r + 3, 3, sel_ring());
			int empty = sys_empty(vis[i]);
			draw_sys_icon(vis[i], cx + 14, cyy + 10, gw - 28, 62, empty ? 100 : 255);
			text_draw_fit(F_SMALL, cx + 8, cyy + gh - font_height(F_SMALL) - 10, gw - 16, data_sys_name(vis[i]),
			              sel ? g_theme.text : empty ? col_alpha(g_theme.text_dim, 150) : g_theme.text_dim, 1);
		}
		gfx_unclip();
	} else {
		int rows = list_rows();
		int rh = CONTENT_H / rows;
		int lw = 330;
		gfx_clip(0, CONTENT_Y, SCREEN_W, CONTENT_H);
		float hy = CONTENT_Y + (sys_cur.asel - sys_cur.atop) * rh;
		draw_sel(PAD, (int)hy + 3, lw, rh - 6, MIN(r, (rh - 6) / 2));
		for (int i = sys_cur.top - 1; i <= sys_cur.top + rows && i < nvis; i++) {
			if (i < 0) continue;
			int y = (int)(CONTENT_Y + (i - sys_cur.atop) * rh);
			int sel = i == sys_cur.sel;
			int empty = sys_empty(vis[i]);
			uint32_t tc = sel ? sel_fg() : empty ? g_theme.text_dim : g_theme.text;
			draw_sys_icon(vis[i], PAD + 6, y + 6, 54, rh - 12, empty && !sel ? 100 : 255);
			char cnt[16];
			if (empty) sncpy(cnt, "\xE2\x80\x93", sizeof cnt);
			else snfmt(cnt, sizeof cnt, "%d", g_sys[vis[i]].count);
			int cwid = text_width(F_SMALL, cnt);
			text_draw_fit(F_BODY, PAD + 68, y + rh / 2 - font_height(F_BODY) / 2, lw - 80 - cwid, data_sys_name(vis[i]), tc, 0);
			text_draw(F_SMALL, PAD + lw - 12 - cwid, y + rh / 2 - font_height(F_SMALL) / 2, cnt, sel ? sel_dim() : g_theme.text_dim);
		}
		gfx_unclip();
		int px = PAD + lw + 16, pw = SCREEN_W - px - PAD;
		int s = vis[sys_cur.sel];
		draw_panel(px, CONTENT_Y + 8, pw, 200, r);
		draw_sys_icon(s, px + 20, CONTENT_Y + 24, pw - 40, 168, 255);
		text_draw_wrap(F_TITLE, px, CONTENT_Y + 222, pw, 2, data_sys_name(s), g_theme.text, 1);
		char cnt[32], info[160];
		sys_count_label(s, cnt, sizeof cnt);
		text_draw_fit(F_SMALL, px, CONTENT_Y + 290, pw, cnt, g_theme.text_dim, 1);
		int cpu = data_sys_cpu(s);
		if (cpu > 0) snfmt(info, sizeof info, "%s  \xC2\xB7  %d MHz", data_core_label(data_sys_core(s)), cpu / 1000);
		else snfmt(info, sizeof info, "%s  \xC2\xB7  automatic CPU", data_core_label(data_sys_core(s)));
		if (!g_sys[s].is_import) text_draw_fit(F_SMALL, px, CONTENT_Y + 316, pw, info, g_theme.text_dim, 1);
		char bl[200];
		uint32_t bc;
		if (sys_bios_line(s, bl, sizeof bl, &bc)) text_draw_wrap(F_SMALL, px, CONTENT_Y + (g_sys[s].is_import ? 316 : 342), pw, 2, bl, bc, 1);
	}
}

/* ---- game lists */

/* "today", "yesterday", "3 days ago", "12 Oct" */
static void resume_when(time_t when, char *out, size_t n) {
	out[0] = 0;
	if (!when) return;
	time_t now = time(NULL);
	struct tm a, b;
	time_t lw = when + g_set.clock_offset, ln = now + g_set.clock_offset;
	localtime_r(&lw, &a);
	localtime_r(&ln, &b);
	long days = (long)(b.tm_year - a.tm_year) * 365 + (b.tm_yday - a.tm_yday);
	if (a.tm_year + 1900 < 2024 || now < when) return; /* the clock isn't set */
	if (days <= 0) sncpy(out, "today", n);
	else if (days == 1) sncpy(out, "yesterday", n);
	else if (days < 7) snfmt(out, n, "%ld days ago", days);
	else strftime(out, n, "%e %b", &a);
	if (out[0] == ' ') memmove(out, out + 1, strlen(out));
}

static void draw_art_panel(View *v, int px, int pw) {
	if (v->items.n == 0) return;
	Entry *e = &v->items.v[v->cur.sel];
	int r = g_theme.radius;
	int aw = pw, ah = 250;
	char art[1024], thumb[1024];
	time_t rwhen;
	Image *im = NULL;
	int resume = data_resume_point(e, thumb, sizeof thumb, &rwhen);
	int has_art = data_art_path(e, art, sizeof art);
	if (!has_art && resume && thumb[0]) { sncpy(art, thumb, sizeof art); has_art = 1; } /* where you left off */
	int settled = plat_ticks() - v->sel_time > 90 || cache_known(art, aw, ah, IM_FIT);
	if (has_art && settled) im = cached_image(art, aw, ah, IM_FIT);
	else if (has_art) pending_loads = 1;
	int y = CONTENT_Y + 10;
	if (im) {
		int ix = px + (pw - im->w) / 2;
		gfx_shadow(ix, y + 4, im->w, im->h, r, 10, g_theme.shadow);
		gfx_blit_rounded(im, ix, y, r, 255);
		y += im->h + 14;
	} else {
		draw_panel(px, y, pw, 170, r);
		if (e->is_dir) draw_folder(px + pw / 2 - 40, y + 45, 64, col_alpha(g_theme.text_dim, 160));
		else if (e->sys >= 0) draw_sys_icon(e->sys, px + 24, y + 20, pw - 48, 130, 150);
		y += 184;
	}
	int lines = text_draw_wrap(F_BODY, px, y, pw, 3, e->name, g_theme.text, 1);
	y += lines * font_height(F_BODY) + 4;
	char sub[96] = "";
	if (e->is_dir) sncpy(sub, "Folder", sizeof sub);
	else if (e->sys >= 0) snfmt(sub, sizeof sub, "%s%s", data_sys_name(e->sys), e->fav ? "  ·  Favorite" : "");
	text_draw_fit(F_SMALL, px, y, pw, sub, e->fav ? g_theme.favorite : g_theme.text_dim, 1);
	if (resume) {
		char when[48], line[96];
		resume_when(rwhen, when, sizeof when);
		snfmt(line, sizeof line, "Continues where you left off%s%s", when[0] ? " \xC2\xB7 " : "", when);
		text_draw_wrap(F_SMALL, px, y + font_height(F_SMALL) + 4, pw, 2, line, g_theme.accent, 1);
	}
}

/* the Zappo family (CFW/clove/zappos, made by art/zappos.py at exactly the
 * size they're shown, so they're drawn 1:1 and never scaled here) */
static Image *zappo_pic(const char *name) {
	static struct { char name[32]; Image *im; int tried; } z[24];
	for (int i = 0; i < ARRAY_LEN(z); i++) {
		if (z[i].tried && !strcmp(z[i].name, name)) return z[i].im;
		if (!z[i].tried) {
			char p[600];
			snfmt(p, sizeof p, "%s/zappos/%s.png", g_paths.clove, name);
			sncpy(z[i].name, name, sizeof z[i].name);
			z[i].im = file_exists(p) ? img_load(p) : NULL;
			z[i].tried = 1;
			return z[i].im;
		}
	}
	return NULL;
}

/* an empty screen: a Zappo (each screen its own type) over the message */
static void draw_empty_z(const char *kind, const char *title, const char *hint) {
	Image *z = NULL;
	if (kind) {
		char nm[40];
		snfmt(nm, sizeof nm, "%s-m", kind);
		z = zappo_pic(nm);
	}
	int ty = CONTENT_Y + 130;
	if (z) {
		gfx_blit(z, (SCREEN_W - z->w) / 2, CONTENT_Y + 12, 255);
		ty = CONTENT_Y + 12 + z->h + 14;
	}
	text_draw_wrap(F_BODY, 60, ty, SCREEN_W - 120, 2, title, g_theme.text, 1);
	text_draw_wrap(F_SMALL, 60, ty + 40, SCREEN_W - 120, 3, hint, g_theme.text_dim, 1);
}

/* two Zappos keeping the bottom corners of the Recent / Favorites shelf company */
static void draw_shelf_pair(const char *left, const char *right) {
	char nm[40];
	snfmt(nm, sizeof nm, "%s-s", left);
	Image *a = zappo_pic(nm);
	snfmt(nm, sizeof nm, "%s-s-flip", right);
	Image *b = zappo_pic(nm);
	if (a) gfx_blit(a, 10, SCREEN_H - a->h - 6, 255);
	if (b) gfx_blit(b, SCREEN_W - b->w - 10, SCREEN_H - b->h - 6, 255);
}

/* a game's tile on the shelf: its box art, or one drawn in its colours with its name */
static void draw_game_tile(Entry *e, int x, int y, int w, int h, int r, int alpha, int *known, uint32_t *tint) {
	char art[1024];
	Image *im = NULL;
	*known = 1;
	if (g_set.show_art && data_art_path(e, art, sizeof art)) {
		im = cached_image(art, w, h, IM_COVER);
		if (!im && pending_loads) *known = 0;
		if (im && tint) {
			uint32_t c;
			if (!tint_cached(art, &c)) { c = tint_of_image(im); tint_store(art, c); }
			*tint = c;
		}
	}
	if (im) {
		gfx_blit_rounded(im, x, y, r, alpha);
		return;
	}
	if (tint) *tint = tint_of_name(e->name);
	gfx_blit_rounded(gen_tile(e->name, w, h), x, y, r, alpha);
	uint32_t tc = col_alpha(0xFFFFFFFF, alpha);
	if (e->is_dir) {
		draw_folder(x + w / 2 - 28, y + h / 2 - 22, 44, col_alpha(0xFFFFFFFF, alpha * 3 / 4));
		text_draw_wrap(F_SMALL, x + 10, y + h - font_height(F_SMALL) * 2 - 10, w - 20, 2, e->name, tc, 1);
	} else {
		Font *f = w >= 150 ? F_BODY : F_SMALL;
		text_draw_wrap(f, x + 13, y + 13, w - 24, 3, e->name, col_alpha(0x000000, alpha / 3), 0);
		text_draw_wrap(f, x + 12, y + 12, w - 24, 3, e->name, tc, 0);
	}
}

static void draw_list_shelf(View *v) {
	int n = v->items.n, r = g_theme.radius + 2;
	const int bw = 170, bh = 226, sw = 116, sh = 154, gap = 14;
	int cyc = CONTENT_Y + 12 + bh / 2;
	int lo = MAX(0, (int)floorf(v->cur.asel) - 4), hi = MIN(n - 1, (int)ceilf(v->cur.asel) + 4);
	uint32_t want = 0;
	for (int pass = 4; pass >= 0; pass--) {
		for (int i = lo; i <= hi; i++) {
			float off = i - v->cur.asel, ao = fabsf(off);
			if ((int)(ao + 0.5f) != pass) continue;
			int big = ao < 0.5f;
			int w = big ? bw : sw, h = big ? bh : sh;
			int x = (int)(shelf_x(off, bw, sw, gap) - w / 2.0f), y = cyc - h / 2;
			if (x > SCREEN_W || x + w < 0) continue;
			int alpha = big ? 255 : (int)(190 - fminf(ao - 1, 1.0f) * 60);
			Entry *e = &v->items.v[i];
			int known;
			uint32_t tc = 0;
			if (big) gfx_shadow(x, y + 8, w, h, r, 14, col_alpha(0x000000, 110));
			draw_game_tile(e, x, y, w, h, r, alpha, &known, i == v->cur.sel ? &tc : NULL);
			if (i == v->cur.sel) want = tc;
			if (big) gfx_rrect_stroke(x - 4, y - 4, w + 8, h + 8, r + 4, 3, RGBA(255, 255, 255, 235));
			if (e->fav) draw_star(x + w - 16, y + 16, big ? 10 : 7, col_alpha(g_theme.favorite, alpha));
		}
	}
	if (want) tint_target(want);
	Entry *e = &v->items.v[v->cur.sel];
	int ty = cyc + bh / 2 + 14;
	text_draw_fit(F_TITLE, PAD + 2, ty + 2, SCREEN_W - PAD * 2, e->name, col_alpha(0x000000, 140), 1);
	text_draw_fit(F_TITLE, PAD, ty, SCREEN_W - PAD * 2, e->name, 0xFFFFFFFF, 1);
	char sub[160];
	if (e->is_dir) snfmt(sub, sizeof sub, "Folder  \xC2\xB7  %d / %d", v->cur.sel + 1, n);
	else snfmt(sub, sizeof sub, "%s  \xC2\xB7  %d / %d", e->sys >= 0 ? data_sys_name(e->sys) : "", v->cur.sel + 1, n);
	text_draw_fit(F_SMALL, PAD, ty + font_height(F_TITLE) + 1, SCREEN_W - PAD * 2, sub, col_alpha(g_theme.text, 190), 1);
	time_t when;
	if (data_resume_point(e, NULL, 0, &when)) {
		char w[48], line[96];
		resume_when(when, w, sizeof w);
		snfmt(line, sizeof line, "Continues where you left off%s%s", w[0] ? " \xC2\xB7 " : "", w);
		text_draw_fit(F_SMALL, PAD, ty + font_height(F_TITLE) + font_height(F_SMALL) + 3, SCREEN_W - PAD * 2, line, g_theme.accent, 1);
	}
}

static void draw_list(View *v) {
	int n = v->items.n;
	if (n == 0) {
		if (v->kind == LK_RECENT) draw_empty_z("electric", "Nothing played yet", "Games you start show up here.");
		else if (v->kind == LK_FAV) draw_empty_z("crystal", "No favorites yet", "Press Y on any game or film to add it here.");
		else if (g_sys[v->sys].is_import) {
			draw_empty_z("leaf", "Everything is sorted", "Copy games into the Import folder on your card and Zappo puts them in the right console.");
		} else {
			char h[400], bl[200];
			uint32_t bc;
#ifdef ZAPPO_KNULLI
			snfmt(h, sizeof h, "Copy games into the import folder on the card (Zappo sorts them), or straight into roms/%s.%s%s",
			       g_sys[v->sys].dir,
#else
			snfmt(h, sizeof h, "Copy games into the Import folder on the %s card (Zappo sorts them), or straight into Roms/%s.%s%s",
			       g_paths.mmc_is_tf2 ? "TF2" : "GarlicOS", g_sys[v->sys].code,
#endif
			       sys_bios_line(v->sys, bl, sizeof bl, &bc) ? "  " : "", sys_bios_line(v->sys, bl, sizeof bl, &bc) ? bl : "");
			draw_empty_z("water", v->dir[0] ? "This folder is empty" : "No games yet", h);
		}
		return;
	}
	int layout = effective_game_layout();
	int r = g_theme.radius;
	if (layout == GL_SHELF && !(v->kind == LK_SYSTEM && v->sys >= 0 && g_sys[v->sys].is_import)) { draw_list_shelf(v); return; }
	if (layout == GL_SHELF) layout = GL_LIST_ART;
	if (layout == GL_GRID) {
		int cols = game_cols(), rows = 2;
		int tw = 140, th = 176, gap = 12;
		int x0 = (SCREEN_W - (cols * tw + (cols - 1) * gap)) / 2;
		int y0 = CONTENT_Y + 8;
		float off = v->cur.atop * (th + gap);
		gfx_clip(0, CONTENT_Y, SCREEN_W, CONTENT_H);
		int first_row = MAX(0, (int)floorf(v->cur.atop) - 1);
		for (int i = first_row * cols; i < n && i < (first_row + rows + 2) * cols; i++) {
			Entry *e = &v->items.v[i];
			int x = x0 + (i % cols) * (tw + gap);
			int y = (int)(y0 + (i / cols) * (th + gap) - off);
			int sel = i == v->cur.sel;
			int ah = 136;
			if (sel) gfx_shadow(x, y + 3, tw, ah, r, 8, g_theme.shadow);
			char art[1024];
			Image *im = NULL;
			if (g_set.show_art && data_art_path(e, art, sizeof art)) im = cached_image(art, tw, ah, IM_COVER);
			if (im) gfx_blit_rounded(im, x, y, r, 255);
			else {
				gfx_rrect(x, y, tw, ah, r, sel ? g_theme.surface_hi : g_theme.surface);
				if (e->is_dir) draw_folder(x + tw / 2 - 30, y + 40, 48, col_alpha(g_theme.text_dim, 160));
				else text_draw_wrap(F_SMALL, x + 10, y + 30, tw - 20, 4, e->name, g_theme.text_dim, 1);
			}
			if (sel) gfx_rrect_stroke(x - 3, y - 3, tw + 6, ah + 6, r + 3, 3, sel_ring());
			if (e->fav) draw_star(x + tw - 14, y + 14, 8, g_theme.favorite);
			text_draw_fit(F_SMALL, x, y + ah + 8, tw, e->name, sel ? g_theme.text : g_theme.text_dim, 1);
		}
		gfx_unclip();
		return;
	}
	int rows = list_rows();
	int rh = CONTENT_H / rows;
	int show_art = layout == GL_LIST_ART && g_set.show_art;
	int art_w = 212;
	int lx = PAD, lw = SCREEN_W - PAD * 2 - 10;
	if (show_art) {
		lw = SCREEN_W - PAD * 3 - art_w - 10;
		if (g_theme.art_left) lx = PAD * 2 + art_w;
	}
	gfx_clip(0, CONTENT_Y, SCREEN_W, CONTENT_H);
	float hy = CONTENT_Y + (v->cur.asel - v->cur.atop) * rh;
	draw_sel(lx, (int)hy + 3, lw, rh - 6, MIN(r, (rh - 6) / 2));
	int show_sys = v->kind == LK_RECENT || v->kind == LK_FAV;
	for (int i = v->cur.top - 1; i <= v->cur.top + rows && i < n; i++) {
		if (i < 0) continue;
		Entry *e = &v->items.v[i];
		int y = (int)(CONTENT_Y + (i - v->cur.atop) * rh);
		int sel = i == v->cur.sel;
		uint32_t tc = sel ? sel_fg() : g_theme.text;
		int tx = lx + 14, tw = lw - 28;
		if (e->is_dir) {
			draw_folder(tx, y + rh / 2 - 9, 18, sel ? sel_fg() : g_theme.text_dim);
			tx += 30;
			tw -= 30;
		}
		if (e->fav) {
			draw_star(lx + lw - 20, y + rh / 2, 8, sel && !glass() ? g_theme.accent_text : g_theme.favorite);
			tw -= 24;
		}
		if (show_sys && e->sys >= 0) {
			const char *sn = g_sys[e->sys].short_name;
			int sw = text_width(F_SMALL, sn);
			text_draw(F_SMALL, tx + tw - sw, y + rh / 2 - font_height(F_SMALL) / 2, sn, sel ? sel_dim() : g_theme.text_dim);
			tw -= sw + 12;
		}
		text_draw_fit(F_BODY, tx, y + rh / 2 - font_height(F_BODY) / 2, tw, e->name, tc, g_theme.center_text ? 1 : 0);
	}
	gfx_unclip();
	/* scrollbar */
	if (n > rows) {
		int sx = lx + lw + 6, sh = CONTENT_H - 16;
		gfx_rrect(sx, CONTENT_Y + 8, 3, sh, 1, col_alpha(g_theme.text_dim, 60));
		int th = MAX(24, sh * rows / n);
		int ty = CONTENT_Y + 8 + (int)((sh - th) * (v->cur.atop / (float)MAX(1, n - rows)));
		gfx_rrect(sx, ty, 3, th, 1, g_theme.text_dim);
	}
	if (show_art) draw_art_panel(v, g_theme.art_left ? PAD : SCREEN_W - PAD - art_w, art_w);
}

/* ---- music */

static void draw_note(int x, int y, int h, uint32_t c) {
	/* a quaver: oval head, stem and flag */
	int r = MAX(2, h / 4);
	gfx_circle(x + r, y + h - r, r, c);
	gfx_rect(x + 2 * r - 2, y, 2, h - r, c);
	for (int i = 0; i < h / 3; i++) gfx_rect(x + 2 * r - 1 + i / 2, y + i, 2, 1, c);
}

static void fmt_time(double s, char *out, size_t n) {
	int t = (int)(s + 0.5);
	if (t >= 3600) snfmt(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
	else snfmt(out, n, "%d:%02d", t / 60, t % 60);
}

/* ---- screensaver: the Clove wallpaper, a big clock and what's playing */

static void draw_saver(void) {
	uint32_t *cv = plat_canvas();
	if (!saver_img) {
		/* the chosen lock screen picture, else the theme's own wallpaper, else the
		 * Clove wallpaper, else the theme background */
		char p[600];
		lockpic_path(p, sizeof p);
		Image *src = p[0] ? img_load(p) : NULL;
		if (!src) snfmt(p, sizeof p, "%s/wallpaper.png", g_theme.dir);
		if (!src && file_exists(p)) src = img_load(p);
		if (!src) {
			snfmt(p, sizeof p, "%s/wallpaper.png", g_paths.clove);
			src = img_load(p);
		}
		if (!src && g_theme.background[0]) src = img_load(g_theme.background);
		if (src) {
			saver_img = img_cover(src, SCREEN_W, SCREEN_H);
			img_free(src);
		}
	}
	if (saver_img) memcpy(cv, saver_img->px, sizeof(uint32_t) * SCREEN_W * SCREEN_H);
	else gfx_vgradient(0, 0, SCREEN_W, SCREEN_H, g_theme.bg, g_theme.bg2);
	/* soft dark bands behind the clock and the music box, so they read on a bright picture */
	gfx_vgradient(0, 0, SCREEN_W, 170, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 0));
	gfx_vgradient(0, SCREEN_H - 150, SCREEN_W, 150, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 140));
	time_t t = time(NULL) + g_set.clock_offset;
	struct tm tmv;
	localtime_r(&t, &tmv);
	char clk[16], date[64];
	if (g_set.clock_mode == 1) {
		strftime(clk, sizeof clk, "%I:%M", &tmv);
		if (clk[0] == '0') memmove(clk, clk + 1, strlen(clk));
	} else strftime(clk, sizeof clk, "%H:%M", &tmv);
	Font *big = clock_font(glass() ? 104 : 92), *mid = font_get(24);
	if (!big) big = F_TITLE;
	if (!mid) mid = F_BODY;
	int x = 30, y = 22;
	/* soft shadow behind the text so it reads on any wallpaper */
	text_draw(big, x + 2, y + 3, clk, col_alpha(0x000000, 120));
	text_draw(big, x, y, clk, 0xFFF4EFE6);
	if (g_set.clock_mode == 1) {
		const char *ap = tmv.tm_hour < 12 ? "AM" : "PM";
		text_draw(mid, x + text_width(big, clk) + 8, y + font_height(big) - font_height(mid) - 14, ap, 0xFFF4EFE6);
	}
	y += font_height(big) - 6;
	if (tmv.tm_year + 1900 >= 2024) { /* the RG35XX loses the date if its battery runs flat */
		strftime(date, sizeof date, "%A %e %B", &tmv);
		text_draw(mid, x + 3, y + 2, date, col_alpha(0x000000, 110));
		text_draw(mid, x + 2, y, date, 0xFFE6F2EC);
	}
	{
		char st[48], line[80];
		if (sleep_timer_text(st, sizeof st)) {
			snfmt(line, sizeof line, "Music pauses %s", st);
			int ly = y + font_height(mid) + 4;
			text_draw(F_SMALL, x + 3, ly + 1, line, col_alpha(0x000000, 110));
			text_draw(F_SMALL, x + 2, ly, line, 0xFFC3A2F7);
		}
	}
	/* battery, top right */
	int pct = plat_battery();
	if (pct >= 0) {
		char bt[96], left[64];
		if (!plat_charging() && batt_left_text(left, sizeof left)) snfmt(bt, sizeof bt, "%d%%  \xC2\xB7  %s", pct, left);
		else snfmt(bt, sizeof bt, "%d%%%s", pct, plat_charging() ? " +" : "");
		int bw = text_width(F_SMALL, bt);
		text_draw(F_SMALL, SCREEN_W - 24 - bw + 1, 23, bt, col_alpha(0x000000, 110));
		text_draw(F_SMALL, SCREEN_W - 24 - bw, 22, bt, 0xFFF4EFE6);
	}
	/* now playing, bottom left */
	MusicInfo m;
	music_info(&m);
	if (m.active) {
		/* a little larger than the menu's mini player, so it reads from a distance */
		Font *tf = font_get(27), *af = font_get(20);
		if (!tf) tf = F_BODY;
		if (!af) af = F_SMALL;
		const int bx = 20, bw = glass() ? 352 : 420, bh = glass() ? 76 : 68, cs = glass() ? 60 : 52;
		int by = SCREEN_H - 24 - bh;
		if (glass()) {
			gfx_shadow(bx, by + 4, bw, bh, 18, 12, RGBA(0, 0, 0, 90));
			gfx_rrect(bx, by, bw, bh, 18, RGBA(10, 8, 18, 178));
			gfx_rrect_stroke(bx, by, bw, bh, 18, 1, RGBA(255, 255, 255, 60));
		} else gfx_rrect(bx, by, bw, bh, 16, col_alpha(0x061614, 155));
		int ti = lib_find(m.path);
		const char *title = m.title, *artist = m.artist;
		int cx = bx + 8, cy = by + (bh - cs) / 2;
		if (ti >= 0) draw_track_cover(ti, cx, cy, cs, 10);
		else draw_note(cx + 8, cy + 8, 26, m.paused ? 0xFFFFBF5C : 0xFFC3A2F7);
		int tx = cx + cs + 12, tw = bx + bw - 14 - tx;
		int ty = by + 7;
		text_draw_fit(tf, tx, ty, tw, title, 0xFFF4EFE6, 0);
		text_draw_fit(af, tx, ty + font_height(tf) - 2, tw, m.paused ? "Paused" : (artist[0] ? artist : "Playing"), m.paused ? 0xFFFFBF5C : 0xFFC9DDD6, 0);
		if (m.len > 0) {
			gfx_rect(tx, by + bh - 10, tw, 3, col_alpha(0xffffff, 60));
			gfx_rect(tx, by + bh - 10, (int)(tw * CLAMP(m.pos / m.len, 0, 1)), 3, 0xFFC3A2F7);
		}
	}
}

/* ---- settings menus */

enum { MK_SUB, MK_CHOICE, MK_ACTION, MK_INFO };
typedef struct {
	int id, kind;
	char label[72];
	char value[96];
	const char *help;
} MItem;
#define MAXI 128
static MItem mi[MAXI];
static int nmi;
static StrList core_opts;
static int core_opts_sys = -1;
static char themes[32][64];
static int nthemes;

static void add(int id, int kind, const char *label, const char *value, const char *help) {
	if (nmi >= MAXI) return;
	MItem *m = &mi[nmi++];
	m->id = id;
	m->kind = kind;
	sncpy(m->label, label, sizeof m->label);
	sncpy(m->value, value ? value : "", sizeof m->value);
	m->help = help;
}

static const char *onoff(int v) { return v ? "On" : "Off"; }
static const char *TAB_NAMES[TAB_COUNT] = {"Recent", "Favorites", "Consoles", "Music", "Videos", FW_MENU_TAB, "Settings"};

static int cpu_options(int *out, int max) {
	int f[32];
	int nf = plat_cpu_freqs(f, 32);
	int n = 0;
	out[n++] = -1; /* recommended */
	out[n++] = 0;  /* GarlicOS automatic */
	for (int i = 0; i < nf && n < max; i++)
		if (f[i] >= 600000 && f[i] <= 1488000) out[n++] = f[i];
	return n;
}
static __attribute__((unused)) void cpu_label(int sys, int v, char *out, size_t n) {
	if (v < 0) snfmt(out, n, "Recommended (%d MHz)", g_sys[sys].cpu_rec / 1000);
	else if (v == 0) sncpy(out, "GarlicOS automatic", n);
	else snfmt(out, n, "%d MHz", v / 1000);
	if (v < 0 && g_sys[sys].cpu_rec == 0) sncpy(out, "Recommended (automatic)", n);
}

#ifdef ZAPPO_KNULLI
static const char *LIGHT_MODES[] = {"Follow the section", "One colour", "Knulli's setting", "Off"};
static int ra_checking;       /* a RetroAchievements sign-in is being checked */
static int ra_score = -1;     /* points, once signed in this time */
static char ra_user_typed[64], ra_pass_typed[64];

/* a console's own Knulli setting ("gba.smooth"): -1 when not set */
static int kopt(int s, const char *opt) {
	char k[96];
	snfmt(k, sizeof k, "%s.%s", g_sys[s].dir, opt);
	return plat_conf_int(k, -1);
}
static void kopt_set(int s, const char *opt, int v) {
	char k[96], val[16];
	snfmt(k, sizeof k, "%s.%s", g_sys[s].dir, opt);
	snfmt(val, sizeof val, "%d", v);
	plat_conf_set(k, v < 0 ? NULL : val);
}
static int cycle(int v, int n, int dir);
static void kopt_cycle(int s, const char *opt, int dir) { kopt_set(s, opt, cycle(kopt(s, opt) + 1, 3, dir) - 1); }

/* Settings > Games > Get box art: how it's going */
static const char *art_label(char *b, size_t n) {
	ArtStatus a;
	net_art_status(&a);
	if (a.running) {
		if (a.total) snfmt(b, n, "%d of %d", a.done, a.total);
		else sncpy(b, "Looking", n);
	} else if (!strcmp(a.state, "finished")) {
		if (a.total) snfmt(b, n, "Found %d of %d", a.found, a.total);
		else sncpy(b, "Nothing missing", n);
	} else if (!strcmp(a.state, "failed")) sncpy(b, "Didn't work", n);
	else if (!strcmp(a.state, "stopped")) sncpy(b, "Stopped", n);
	else b[0] = 0;
	return b;
}
static const char *art_help(void) {
	static char h[300];
	ArtStatus a;
	net_art_status(&a);
	if (a.running && a.system[0])
		snfmt(h, sizeof h, "%s%s%s. A stops it; the pictures found so far stay.", a.system, a.game[0] ? ": " : "", a.game);
	else if (a.running) sncpy(h, "Looking for games without a picture. A stops it.", sizeof h);
	else if (!strcmp(a.state, "failed") && a.error[0]) sncpy(h, a.error, sizeof h);
	else sncpy(h, "Finds pictures for games that have none, from the libretro thumbnail library (the one RetroArch uses), over Wi-Fi. "
	              "Pictures already there are kept. It carries on while you play.", sizeof h);
	return h;
}
static void art_start(const char *folders, const char *what) {
	WifiStatus ws;
	net_wifi_status(&ws);
	if (ws.known && (!ws.enabled || !ws.connected)) { toast("Box art comes over Wi-Fi: join a network in Settings > Wi-Fi first"); return; }
	if (net_art_start(folders) != 0) { toast("Could not start getting box art"); return; }
	toast("Getting box art for %s. Carry on, it works in the background", what);
}
#endif

static const char *menu_title(int menu, int arg) {
	static char t[96];
	switch (menu) {
	case M_ROOT: return "Settings";
	case M_APPEAR: return "Look";
	case M_HOME: return "Home screen";
	case M_GAMES: return "Games";
	case M_TABS: return "Tabs";
	case M_SYSTEMS: return "Consoles";
	case M_PICK: return "Move to which console?";
	case M_CLOCK: return "Set clock";
	case M_SENS: return "Sensitivity";
	case M_SYSTEM: snfmt(t, sizeof t, "%s", data_sys_name(arg)); return t;
	case M_PERF: return "Performance";
	case M_CONTROLS: return "Controls";
	case M_POWER: return "Screen & power";
	case M_MUSIC: return "Music";
	case M_ABOUT: return "About Zappo";
	case M_WIFI: return "Wi-Fi";
	case M_BT: return "Bluetooth";
	case M_SEND: return "Send to Zappo";
	case M_LIGHTS: return "Lights";
	case M_ACH: return "Achievements";
	}
	return "";
}

static void menu_build(int menu, int arg) {
	nmi = 0;
	char b[96];
	switch (menu) {
	case M_ROOT:
		add(M_APPEAR, MK_SUB, "Look", "", "Theme, Glass or Classic style, and how each list is laid out.");
		add(M_HOME, MK_SUB, "Home screen", "", "Which tabs show, where Zappo opens, the clock and the battery.");
		add(M_GAMES, MK_SUB, "Games", "", "Box art, tidy names, carrying on where you left off, and each console's settings.");
#ifdef ZAPPO_KNULLI
		{
			char user[64];
			plat_conf_get("global.retroachievements.username", user, sizeof user);
			add(M_ACH, MK_SUB, "Achievements", plat_conf_int("global.retroachievements", 0) && user[0] ? user : "Off",
			    "RetroAchievements: sign in, then unlock achievements as you play.");
		}
#endif
		add(M_MUSIC, MK_SUB, "Music", "", "Volume levelling, picking up where you were, the sleep timer.");
		add(M_CONTROLS, MK_SUB, "Controls", "", "Confirm button, key repeat and an input tester.");
		add(M_POWER, MK_SUB, "Screen & power", "", "Brightness, lock screen, battery, reboot and power off.");
		add(M_PERF, MK_SUB, "Performance", "", "CPU speeds while browsing and while playing.");
#ifdef ZAPPO_KNULLI
		if (plat_lights_available())
			add(M_LIGHTS, MK_SUB, "Lights", LIGHT_MODES[CLAMP(g_set.lights, 0, LIGHTS_COUNT - 1)],
			    "The lights around the sticks: the colour of the section you're in, one colour, Knulli's own setting, or off.");
		{
			WifiStatus ws;
			net_wifi_status(&ws);
			add(M_WIFI, MK_SUB, "Wi-Fi", !ws.known ? "" : !ws.enabled ? "Off" : ws.connected ? ws.ssid : "Not connected",
			    "Networks nearby, passwords, and switching Wi-Fi off to save battery.");
			BtStatus bs;
			net_bt_status(&bs);
			add(M_BT, MK_SUB, "Bluetooth", !bs.known ? "" : bs.enabled ? "On" : "Off",
			    "Pair controllers and headphones, connect and forget them.");
			add(M_SEND, MK_SUB, "Send to Zappo", "", "Send games, songs and films from your phone or PC over Wi-Fi.");
		}
#endif
		add(M_ABOUT, MK_SUB, "About Zappo", "", "Version, device information and switching back to the " FW_NAME " menu.");
		break;
	case M_APPEAR: {
		static const char *HL[] = {"Carousel", "Grid", "List", "Shelf"};
		static const char *GL[] = {"List with art", "List", "Grid", "Shelf"};
		add(1, MK_CHOICE, "Theme", g_theme.name, "Themes live in CFW/clove/themes. \"GarlicOS skin\" uses your current GarlicOS skin.");
		add(16, MK_CHOICE, "Style", glass() ? "Glass" : "Classic",
		    "Glass: your lock screen picture (or the theme's wallpaper), softly blurred, behind every screen, with see-through panels and a tab bar of icons. "
		    "The Zappo element themes keep their own picture, sharp.");
		snfmt(b, sizeof b, "%s%s", g_set.home_layout < 0 ? "Theme: " : "", HL[effective_home_layout()]);
		add(2, MK_CHOICE, "Consoles", b, "How the console list looks. SELECT on the Consoles tab also switches it.");
		snfmt(b, sizeof b, "%s%s", g_set.game_layout < 0 ? "Theme: " : "", GL[effective_game_layout()]);
		add(3, MK_CHOICE, "Game lists", b, "How game lists look. SELECT in a game list also switches it.");
		add(17, MK_CHOICE, "Music", (const char *[]){"List", "Grid", "Shelf"}[CLAMP(g_set.music_layout, 0, MLAY_COUNT - 1)], "How the Music tab's lists look. Also in the Music tab's X options.");
		add(18, MK_CHOICE, "Videos", g_set.video_wall ? "Wall of pictures" : "List", "How the Videos tab looks. SELECT on the Videos tab also switches it.");
		add(20, MK_CHOICE, "Text", (const char *[]){"Regular", "Firm", "Bold"}[CLAMP(g_set.font_weight, 0, 2)],
		    "How heavy the writing is. Firm and Bold draw the theme's font thicker, so it reads better on the small screen.");
		add(6, MK_CHOICE, "Animations", onoff(g_set.animations), "Smooth scrolling and transitions.");
		add(9, MK_CHOICE, "Button guide", g_set.show_guide ? "Shown" : "Hidden", "The row of buttons along the bottom of the screen.");
		break;
	}
	case M_HOME: {
		static const char *CK[] = {"24-hour", "12-hour", "Hidden"};
		static const char *BT[] = {"Icon and %", "Icon", "Hidden"};
		add(11, MK_SUB, "Tabs", "", "Choose which tabs appear on the home screen.");
		add(12, MK_CHOICE, "Open on", TAB_NAMES[g_set.start_tab], "The tab Zappo opens on.");
		add(7, MK_CHOICE, "Clock", CK[g_set.clock_mode], NULL);
		add(8, MK_CHOICE, "Battery", BT[g_set.battery_mode], NULL);
		add(10, MK_CHOICE, "Consoles without games", g_set.empty_consoles ? "Hidden" : "Listed last", "Consoles you have no games for yet are listed after the others, or left out.");
		add(14, MK_SUB, "Set clock", "", "Corrects the time shown at the top of the screen.");
		break;
	}
	case M_GAMES:
#ifdef ZAPPO_KNULLI
		add(4, MK_CHOICE, "Box art", onoff(g_set.show_art), "Shows each game's picture from roms/<console>/images, the same ones Knulli's menu uses.");
		add(21, MK_ACTION, "Get box art", art_label(b, sizeof b), art_help());
#else
		add(4, MK_CHOICE, "Box art", onoff(g_set.show_art), "Shows Roms/<console>/Imgs/<game>.png next to games (the PC kit's \"Get box art\" finds them).");
#endif
		add(5, MK_CHOICE, "Tidy game names", onoff(g_set.clean_names), "Hides tags like (USA) and [!] and shows full arcade titles.");
		add(15, MK_CHOICE, "Continue where you left off", onoff(g_set.resume_games),
		    "Games carry on from the moment you closed them. X on a game: Start over. (Arcade games always start fresh.)");
		add(13, MK_CHOICE, "Return to last game", onoff(g_set.remember), "After playing, come back to the game you just played.");
		add(19, MK_SUB, "Consoles", "", "Show, hide, rename and tune each console: emulator, CPU speed, video.");
		break;
#ifdef ZAPPO_KNULLI
	case M_LIGHTS: {
		static const char *EF[] = {"Steady", "Slow breathing", "Breathing", "Fast breathing"};
		add(1, MK_CHOICE, "Lights", LIGHT_MODES[CLAMP(g_set.lights, 0, LIGHTS_COUNT - 1)],
		    g_set.lights == LIGHTS_FOLLOW ? "Each section in its Zappo's colour: Recent electric yellow, Favorites crystal purple, Consoles fire orange, "
		                                    "Music water blue, Videos ice blue, Settings leaf green."
		    : g_set.lights == LIGHTS_KNULLI ? "The colour and effect chosen in Knulli's own menu."
		    : g_set.lights == LIGHTS_ONE ? "The same colour everywhere." : "No lights.");
		if (g_set.lights == LIGHTS_ONE)
			add(2, MK_CHOICE, "Colour", plat_light_colours[CLAMP(g_set.lights_colour, 0, LIGHT_COLOURS - 1)].name, NULL);
		if (g_set.lights == LIGHTS_FOLLOW || g_set.lights == LIGHTS_ONE)
			add(3, MK_CHOICE, "Effect", EF[CLAMP(g_set.lights_effect, 0, 3)], NULL);
		if (g_set.lights != LIGHTS_OFF) {
			snfmt(b, sizeof b, "%d%%", plat_conf_int("led.brightness", 70));
			add(4, MK_CHOICE, "Brightness", b, "Shared with Knulli's own light setting.");
			add(5, MK_CHOICE, "Dim with the screen", onoff(plat_conf_int("led.brightness.adaptive", 1)),
			    "The lights get dimmer and brighter with the screen.");
		}
		add(0, MK_INFO, "Low battery (yellow, then red) and charging still show on the lights.", "", NULL);
		break;
	}
	case M_ACH: {
		char user[64], pass[8];
		plat_conf_get("global.retroachievements.username", user, sizeof user);
		int have = user[0] && plat_conf_get("global.retroachievements.password", pass, sizeof pass) && pass[0];
		int on = plat_conf_int("global.retroachievements", 0);
		const char *acct = ra_checking ? "Signing in..." : have ? user : "Sign in";
		add(2, MK_ACTION, "Account", acct,
		    have ? "A: sign in again, or as someone else." : "Your RetroAchievements account (free at retroachievements.org): type the name, then the password.");
		add(1, MK_CHOICE, "Achievements", onoff(on && have), "Unlock achievements as you play: RetroArch's emulators, and Knulli's PSP, PlayStation and Dreamcast ones.");
		if (have) {
			static const struct { int id; const char *key, *label, *help; } O[] = {
				{3, "hardcore", "Hardcore mode", "Full points, but no save states, rewind, slow motion or cheats (games start fresh instead of carrying on)."},
				{4, "leaderboards", "Leaderboards", "Sends your times and scores to the game's leaderboards."},
				{5, "richpresence", "Show what you're playing", "Your RetroAchievements profile shows the game and what's happening in it."},
				{6, "screenshot", "Screenshot on unlock", "Takes a screenshot each time you unlock one."},
				{7, "challenge_indicators", "Challenge indicators", "A small icon while a tricky achievement is in reach."},
				{8, "encore", "Encore mode", "Unlocks achievements you already have again, as you play."},
			};
			for (int i = 0; i < ARRAY_LEN(O); i++) {
				char k[96];
				snfmt(k, sizeof k, "global.retroachievements.%s", O[i].key);
				add(O[i].id, MK_CHOICE, O[i].label, onoff(plat_conf_int(k, 0)), O[i].help);
			}
			add(9, MK_ACTION, "Sign out", "", "Forgets the password on this handheld.");
			if (ra_score >= 0) { snfmt(b, sizeof b, "%s: %d points", user, ra_score); add(0, MK_INFO, b, "", NULL); }
		}
		break;
	}
#endif
	case M_TABS:
		for (int i = 0; i < TAB_COUNT; i++) {
			if (i == TAB_CONSOLES) continue;
			add(i, MK_CHOICE, TAB_NAMES[i], g_set.tab_visible[i] ? "Shown" : "Hidden",
			    i == TAB_SETTINGS ? "Settings stay reachable with START even when this tab is hidden." : NULL);
		}
		break;
	case M_SYSTEMS:
		for (int i = 0; i < g_nsys; i++) {
			if (!g_sys[i].on_tf1 && !g_sys[i].on_tf2) continue;
			if (g_sys[i].is_import) continue;
			if (sys_get_int(g_sys[i].code, "hidden", 0)) sncpy(b, "Hidden", sizeof b);
			else sys_count_label(i, b, sizeof b);
			add(i, MK_SUB, data_sys_name(i), b, NULL);
		}
		add(-2, MK_ACTION, "Reset all consoles", "", "Clears every console's settings (names, emulators, speeds, video).");
		break;
	case M_SYSTEM: {
		int s = arg;
		const char *code = g_sys[s].code;
		static const char *NS[] = {"Full", "Short", "Folder"};
		add(1, MK_CHOICE, "Show in menu", onoff(!sys_get_int(code, "hidden", 0)), NULL);
		snfmt(b, sizeof b, "%s (%s)", NS[CLAMP(sys_get_int(code, "name_style", 0), 0, 2)],
		      sys_get_int(code, "name_style", 0) == 1 ? g_sys[s].short_name : sys_get_int(code, "name_style", 0) == 2 ? code : g_sys[s].name);
		add(2, MK_CHOICE, "Name", b, "Custom names can also be set in CFW/clove/settings.json.");
		add(3, MK_ACTION, "Move earlier", "", "Changes this console's position in the list.");
		add(4, MK_ACTION, "Move later", "", NULL);
		if (core_opts_sys != s) { data_cores_for_system(s, &core_opts); core_opts_sys = s; }
		add(5, MK_CHOICE, "Emulator", data_core_label(data_sys_core(s)),
		    core_opts.n > 1 ? "Other installed cores that can open these games." : "Only one installed core matches these games.");
#ifdef ZAPPO_KNULLI
		/* these are Knulli's own per-console settings, so its menu shows the same */
		if (g_sys[s].dir[0] && strcmp(code, "VIDEOS") && strcmp(code, "PORTS")) {
			int v = kopt(s, "smooth");
			add(7, MK_CHOICE, "Screen filter", v < 0 ? "Knulli setting" : v ? "Smooth" : "Sharp pixels",
			    "Smooth softens the picture when it's made bigger; Sharp pixels keeps every pixel crisp.");
			v = kopt(s, "integerscale");
			add(8, MK_CHOICE, "Integer scaling", v < 0 ? "Knulli setting" : onoff(v), "Whole-number scaling: crisp, with borders.");
			v = kopt(s, "video_threaded");
			add(9, MK_CHOICE, "Threaded video", v < 0 ? "Knulli setting" : onoff(v), "Moves drawing to another CPU core. Can smooth heavy games; adds a little lag.");
			v = kopt(s, "audio_latency");
			if (v > 0) snfmt(b, sizeof b, "%d ms", v); else sncpy(b, "Knulli setting", sizeof b);
			add(10, MK_CHOICE, "Audio latency", b, "Higher values stop crackling on demanding games.");
			v = kopt(s, "showFPS");
			add(11, MK_CHOICE, "Show FPS", v < 0 ? "Knulli setting" : onoff(v), NULL);
			add(13, MK_ACTION, "Get box art", "", "Pictures for this console's games that have none, from the libretro thumbnail library, over Wi-Fi.");
		}
#else
		cpu_label(s, sys_get_int(code, "cpu", -1), b, sizeof b);
		add(6, MK_CHOICE, "CPU speed", b, "A fixed speed keeps frame pacing steady. GarlicOS automatic lets the CPU speed up and slow down.");
		add(7, MK_CHOICE, "Screen filter", sys_get_int(code, "filter", 0) ? "Sharp pixels" : "RetroArch setting", "Sharp pixels uses the cheapest scaler.");
		int v = sys_get_int(code, "integer", -1);
		add(8, MK_CHOICE, "Integer scaling", v < 0 ? "RetroArch setting" : onoff(v), "Whole-number scaling: crisp, with borders.");
		v = sys_get_int(code, "threaded", -1);
		add(9, MK_CHOICE, "Threaded video", v < 0 ? "RetroArch setting" : onoff(v), "Moves drawing to another CPU core. Can smooth heavy games; adds a little lag.");
		v = sys_get_int(code, "latency", 0);
		if (v > 0) snfmt(b, sizeof b, "%d ms", v); else sncpy(b, "RetroArch setting", sizeof b);
		add(10, MK_CHOICE, "Audio latency", b, "Higher values stop crackling on demanding games.");
		add(11, MK_CHOICE, "Show FPS", onoff(sys_get_int(code, "fps", 0)), NULL);
#endif
		add(12, MK_ACTION, "Reset this console", "", NULL);
		{
			char files[256], line[300];
			int st = data_bios_status(s, files, sizeof files);
			if (st == BIOS_REQUIRED_MISSING) snfmt(line, sizeof line, "Needs BIOS file: %s (copy it to BIOS or Import)", files);
			else if (st == BIOS_OPTIONAL_MISSING) snfmt(line, sizeof line, "Optional BIOS file: %s", files);
			else sncpy(line, "BIOS: nothing missing", sizeof line);
			add(0, MK_INFO, line, "", NULL);
#ifdef ZAPPO_KNULLI
			if (g_sys[s].dir[0]) { snfmt(line, sizeof line, "Games folder: roms/%s", g_sys[s].dir); add(0, MK_INFO, line, "", NULL); }
#else
			snfmt(line, sizeof line, "Games folder: Roms/%s", code);
			add(0, MK_INFO, line, "", NULL);
#endif
		}
		break;
	}
	case M_PICK: {
		char g[300];
		int guess = import_guess(pick_path, dir_exists(pick_path));
		if (guess >= 0) { snfmt(g, sizeof g, "Suggested: %s", data_sys_name(guess)); add(guess, MK_ACTION, g, "", "Zappo's best guess from the file."); }
		add(IMPORT_TO_BIOS, MK_ACTION, "BIOS folder", "", "For BIOS / firmware files the emulators need.");
		add(IMPORT_TO_MUSIC, MK_ACTION, "Music folder", "", "Songs and albums for the Music tab.");
		for (int i = 0; i < pick_n; i++) {
			int s = pick_vis[i];
			char cnt[32];
			sys_count_label(s, cnt, sizeof cnt);
			add(s, MK_ACTION, data_sys_name(s), cnt, NULL);
		}
		break;
	}
	case M_CLOCK: {
		time_t t = time(NULL) + g_set.clock_offset;
		struct tm tmv;
		localtime_r(&t, &tmv);
		snfmt(b, sizeof b, "%02d", tmv.tm_hour);
		add(1, MK_CHOICE, "Hour", b, "Left / right to change.");
		snfmt(b, sizeof b, "%02d", tmv.tm_min);
		add(2, MK_CHOICE, "Minute", b, NULL);
		snfmt(b, sizeof b, "%d", tmv.tm_mday);
		add(4, MK_CHOICE, "Day", b, NULL);
		static const char *MON[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
		add(5, MK_CHOICE, "Month", MON[tmv.tm_mon], NULL);
		snfmt(b, sizeof b, "%d", tmv.tm_year + 1900);
		add(6, MK_CHOICE, "Year", b, NULL);
		add(3, MK_ACTION, "Use the device clock", "", "Removes Zappo's correction.");
		add(0, MK_INFO, "The handheld has no internet time, so set it once here.", "", NULL);
		break;
	}
	case M_PERF:
		snfmt(b, sizeof b, "%d MHz", g_set.menu_cpu / 1000);
		add(1, MK_CHOICE, "Menu CPU limit", b, "Lower saves battery while browsing. Games use their own speed.");
		add(2, MK_ACTION, "Use recommended speeds", "", "Resets every system's CPU speed to Zappo's recommendation.");
		add(3, MK_ACTION, "Use GarlicOS automatic everywhere", "", "Every game runs with GarlicOS's own interactive governor.");
		add(4, MK_INFO, "Zappo never goes above 1.5 GHz, the top speed GarlicOS itself uses.", "", NULL);
		break;
	case M_CONTROLS: {
		add(1, MK_CHOICE, "Confirm button", g_set.swap_ab ? "B (Xbox style)" : "A (Nintendo style)", NULL);
		add(2, MK_SUB, "Sensitivity", "", "How quickly a held button starts repeating, and how fast lists then scroll.");
		add(3, MK_SUB, "Input test", "", "Shows exactly what each button sends. Useful if a button does nothing.");
		break;
	}
	case M_SENS: {
		static const char *HD[] = {"Very short", "Short", "Normal", "Long", "Very long"};
		static const char *RR[] = {"1  (slowest)", "2", "3", "4", "5  (fastest)"};
		add(1, MK_CHOICE, "Hold before repeating", HD[g_set.repeat_delay], "How long you hold a direction before it starts repeating.");
		add(2, MK_CHOICE, "Scroll speed", RR[g_set.repeat_rate], "How fast lists move while a direction is held.");
		add(3, MK_CHOICE, "Speed up when held", onoff(g_set.repeat_accel), "Scrolling gets faster after holding for a second and a half.");
		add(0, MK_INFO, "Blockbox speeds: START in the game > Sensitivity.", "", NULL);
		break;
	}
	case M_POWER:
		snfmt(b, sizeof b, "%d", plat_brightness_get());
		add(1, MK_CHOICE, "Brightness", b, "Volume buttons also change brightness in Zappo (or the music volume while a song plays).");
#ifdef ZAPPO_KNULLI
		{
			char v[32];
			plat_conf_get("global.videooutput", v, sizeof v);
			snfmt(b, sizeof b, "%s%s", !strcmp(v, "hdmi-720p") ? "720p" : !strcmp(v, "hdmi-480p") ? "480p" : "1080p", plat_tv() ? " (showing now)" : "");
			add(20, MK_CHOICE, "TV picture (HDMI)", b,
			    "With a TV plugged into the HDMI port, everything shows on the TV. 480p keeps Zappo and older games sharpest; "
			    "1080p suits TVs that blur smaller pictures.");
		}
#endif
		add(2, MK_ACTION, "Lock now", "", "Shows the lock screen. Press L1 and R1 together to unlock.");
		if (g_set.saver_min) snfmt(b, sizeof b, "After %d min", g_set.saver_min); else sncpy(b, "Off", sizeof b);
		add(5, MK_CHOICE, "Screensaver", b, "Shows the Zappo wallpaper with a big clock when the handheld is left alone. Music keeps playing.");
		if (g_set.off_min) snfmt(b, sizeof b, "After %d min", g_set.off_min); else sncpy(b, "Never", sizeof b);
		add(6, MK_CHOICE, "Lock after", b, "Locks the handheld after this long with no button pressed. Press L1 and R1 together to unlock.");
		add(8, MK_CHOICE, "Lock screen", g_set.sleep_clock ? "Clock (dimmed)" : "Screen off",
		    "Clock: the wallpaper, time and what's playing on a very dim screen. Screen off: saves the most battery. Either way, any button shows the clock for a few seconds.");
		{
			char nm[128];
			if (nlockpics < 0) lockpics_scan();
			if (!g_set.lock_pic[0]) sncpy(nm, "Theme", sizeof nm);
			else if (!strcmp(g_set.lock_pic, "*")) sncpy(nm, "Shuffle", sizeof nm);
			else lockpic_name(g_set.lock_pic, nm, sizeof nm);
			add(9, MK_CHOICE, "Lock screen picture", nm,
			    "Behind the clock on the lock screen and screensaver. Shuffle: a new one each time. A: preview. "
			    "Add your own 640 x 480 pictures to CFW/clove/lockscreens.");
		}
		add(10, MK_CHOICE, "Battery time left", g_set.battery_time ? "Shown" : "Hidden",
		    "An estimate from how fast the battery has been going down since the last charge. Shown on the lock screen and in the MENU button's menu.");
		{
			char left[64];
			int pct = plat_battery();
			if (pct >= 0) {
				int on = g_set.battery_time;
				g_set.battery_time = 1;
				batt_left_text(left, sizeof left);
				g_set.battery_time = on;
				snfmt(b, sizeof b, "Battery %d%%: %s%s", pct, left, plat_charging() ? "" : batt_measured ? "" : " (a first guess until it's measured)");
				add(0, MK_INFO, b, "", NULL);
				if (batt_measured && batt_rate > 0) {
					/* what a full charge gives at the way it has been used lately */
					int mins = (int)(100.0f / batt_rate * 60.0f);
					snfmt(b, sizeof b, "A full charge lasts about %d h %02d min the way it's been used lately", mins / 60, mins % 60);
					add(0, MK_INFO, b, "", NULL);
				}
			}
		}
		{
			char fn[64];
			if (nclockfonts < 0) clockfonts_scan();
			clock_font_name(fn, sizeof fn);
			add(11, MK_CHOICE, "Clock font", fn, "The big clock on the lock screen and screensaver. A: preview. Add your own .ttf files to CFW/clove/fonts.");
		}
		add(7, MK_ACTION, "Show the screensaver now", "", "Any button brings you back.");
		add(3, MK_ACTION, "Reboot", "", NULL);
		add(4, MK_ACTION, "Power off", "", NULL);
		break;
	case M_MUSIC: {
		add(1, MK_CHOICE, "Volume levelling", onoff(g_set.music_level),
		    "Evens out loud and quiet songs. Each song is measured the first time it plays and starts at the right level after that.");
		add(2, MK_CHOICE, "Remember where I was", onoff(g_set.music_resume),
		    "After a game or a restart, the song you were playing waits, paused, at the same spot. START on the Music tab, then A.");
		char st[48];
		if (sleep_timer_text(st, sizeof st)) snfmt(b, sizeof b, "Pauses %s", st);
		else sncpy(b, "Off", sizeof b);
		add(3, MK_CHOICE, "Sleep timer", b, "Fades the music out and pauses it. Also in X on the Music tab and Now Playing.");
		snfmt(b, sizeof b, "%d%%", g_set.music_volume);
		add(4, MK_CHOICE, "Volume", b, "The volume buttons change this too while music plays.");
		add(0, MK_INFO, "Playlists live in Music/Playlists as .m3u files.", "", NULL);
		break;
	}
	case M_WIFI: {
		WifiStatus ws;
		net_wifi_status(&ws);
		add(1, MK_CHOICE, "Wi-Fi", ws.enabled ? "On" : "Off", "Off saves battery. Bluetooth keeps working either way.");
		/* one line for how things stand (the list below doesn't jump about) */
		if (net_busy()) snfmt(b, sizeof b, "%s...", net_job());
		else if (net_last_error()[0]) sncpy(b, net_last_error(), sizeof b);
		else if (!ws.enabled) sncpy(b, "Wi-Fi is off", sizeof b);
		else if (ws.connected) snfmt(b, sizeof b, "Connected to %s  -  %s", ws.ssid, ws.ip[0] ? ws.ip : "no address yet");
		else sncpy(b, ws.known ? "Not connected" : "Checking...", sizeof b);
		add(0, MK_INFO, b, "", NULL);
		if (!ws.enabled) break;
		WifiNet nets[16];
		int k = net_wifi_list(nets, 16), saved = 0;
		for (int i = 0; i < k; i++) {
			char v[64];
			const char *what = nets[i].connected ? "Connected" : nets[i].saved ? "Saved" : nets[i].secured ? "Password" : "Open";
			snfmt(v, sizeof v, "%s|%d", what, CLAMP((nets[i].strength + 24) / 25, 1, 4));
			add(100 + i, MK_ACTION, nets[i].ssid, v, nets[i].connected ? "The handheld is on this network. A: forget it." :
			    nets[i].saved ? "A: connect (the handheld has its password)." : nets[i].secured ? "A: type its password and connect." : "A: connect (no password).");
		}
		(void)saved;
		add(2, MK_ACTION, "Look again", "", "Searches for the networks nearby.");
		break;
	}
	case M_BT: {
		BtStatus bs;
		net_bt_status(&bs);
		add(1, MK_CHOICE, "Bluetooth", bs.enabled ? "On" : "Off", "Knulli's Bluetooth. Off saves a little battery.");
		BtDev d[16];
		int k = net_bt_list(d, 16), npaired = 0, nconn = 0;
		for (int i = 0; i < k; i++) { npaired += d[i].paired; nconn += d[i].paired && d[i].connected; }
		if (net_busy()) snfmt(b, sizeof b, "%s...", net_job());
		else if (net_last_error()[0]) sncpy(b, net_last_error(), sizeof b);
		else if (!bs.enabled) sncpy(b, "Bluetooth is off", sizeof b);
		else if (!bs.known) sncpy(b, "Checking...", sizeof b);
		else if (!npaired) sncpy(b, "Nothing paired yet", sizeof b);
		else snfmt(b, sizeof b, "%d paired, %d connected", npaired, nconn);
		add(0, MK_INFO, b, "", NULL);
		if (!bs.enabled) break;
		for (int i = 0; i < k; i++) {
			if (!d[i].paired) continue;
			char v[64];
			snfmt(v, sizeof v, "%s%s%s", d[i].kind, d[i].kind[0] ? "  -  " : "", d[i].connected ? "Connected" : "Paired");
			add(200 + i, MK_ACTION, d[i].name, v, "A: connect, disconnect or forget it.");
		}
		add(2, MK_ACTION, "Look for new devices", "", "Put the controller or headphones in pairing mode first, then look.");
		for (int i = 0; i < k; i++) {
			if (d[i].paired) continue;
			char v[64];
			snfmt(v, sizeof v, "%s%sPair", d[i].kind, d[i].kind[0] ? "  -  " : "");
			add(200 + i, MK_ACTION, d[i].name, v, "A: pair it with the handheld.");
		}
		break;
	}
	case M_ABOUT: {
		snfmt(b, sizeof b, "Zappo %s", CLOVE_VERSION);
		add(0, MK_INFO, b, "", NULL);
		add(0, MK_INFO, "Created by CovGNG", "", NULL);
		char d[512];
		plat_describe(d, sizeof d);
		for (char *line = strtok(d, "\n"); line; line = strtok(NULL, "\n")) add(0, MK_INFO, line, "", NULL);
		if (g_paths.mmc_is_tf2) sncpy(b, "Running from the TF2 card (TF1 only starts it)", sizeof b);
		else snfmt(b, sizeof b, "Running from TF1; TF2 card: %s", g_paths.sd_present ? "present" : "not found");
		add(0, MK_INFO, b, "", NULL);
		{
			struct statvfs vf;
			if (statvfs(g_paths.mmc, &vf) == 0) {
				double fr = (double)vf.f_bavail * vf.f_frsize / 1e9, tot = (double)vf.f_blocks * vf.f_frsize / 1e9;
				snfmt(b, sizeof b, "Space: %.1f GB free of %.1f GB", fr, tot);
				add(0, MK_INFO, b, "", NULL);
			}
		}
		add(1, MK_ACTION, "Rescan games", "", "Sorts the Import folder, then re-reads every Roms folder.");
		add(3, MK_ACTION, "Sort the Import folder now", "", "Moves games from Import into their console folders.");
#ifdef ZAPPO_KNULLI
		add(2, MK_ACTION, "Switch to Knulli's menu", "", "Zappo X steps aside and Knulli's own menu starts instead.");
		add(0, MK_INFO, "Runs on Knulli, by the Knulli team (a Batocera fork).", "", NULL);
#else
		add(2, MK_ACTION, "Switch to the GarlicOS menu", "", "Clove steps aside. Run \"Switch to Clove\" from GarlicOS's Apps to come back.");
		add(0, MK_INFO, "Runs on GarlicOS by Black-Seraph.", "", NULL);
#endif
		break;
	}
	}
}

static int cycle(int v, int n, int dir) { return ((v + dir) % n + n) % n; }

/* data_scan() re-sorts the systems, so every open list's system number (and
 * the numbers inside its entries) would go stale: re-find them by code and
 * reload the lists, keeping the cursor where it was. */
static void rescan(void) {
	char codes[ARRAY_LEN(stack) + 1][32];
	View *vs[ARRAY_LEN(stack) + 1];
	int n = 0;
	for (int i = 0; i < depth; i++) if (stack[i].type == V_LIST && stack[i].kind != LK_RECENT && stack[i].kind != LK_FAV && stack[i].sys >= 0 && stack[i].sys < g_nsys) { vs[n] = &stack[i]; sncpy(codes[n], g_sys[stack[i].sys].code, 32); n++; }
	if (v_video.type == V_LIST && v_video.sys >= 0 && v_video.sys < g_nsys) { vs[n] = &v_video; sncpy(codes[n], g_sys[v_video.sys].code, 32); n++; }
	data_scan();
	refresh_visible();
	for (int i = 0; i < n; i++) {
		View *v = vs[i];
		v->sys = data_sys_index(codes[i]);
		if (v->sys < 0) { el_clear(&v->items); v->cur.sel = v->cur.top = 0; continue; }
		int sel = v->cur.sel;
		load_list(v);
		if (v->kind == LK_VIDEO) { video_apply_sort(v); vmeta_for = NULL; }
		v->cur.sel = MIN(sel, MAX(0, v->items.n - 1));
		clamp_cursor(&v->cur, v->items.n, v->kind == LK_VIDEO ? video_rows() : list_rows(), v->kind == LK_VIDEO ? video_cols() : 1);
	}
	video_sys = data_sys_index("VIDEOS");
	if (v_video.type == V_LIST) v_video.sys = video_sys;
	for (int i = 0; i < depth; i++) if (stack[i].type == V_LIST && (stack[i].kind == LK_RECENT || stack[i].kind == LK_FAV)) load_list(&stack[i]);
	load_list(&v_recent);
	load_list(&v_fav);
	clamp_cursor(&v_recent.cur, v_recent.items.n, list_rows(), 1);
	clamp_cursor(&v_fav.cur, v_fav.items.n, list_rows(), 1);
}

static void move_system(int s, int dir) {
	int j = s + dir;
	if (j < 0 || j >= g_nsys) return;
	/* store the whole current order once, then swap the two neighbours */
	cJSON_DeleteItemFromObjectCaseSensitive(g_set.root, "system_order");
	cJSON *order = cJSON_AddArrayToObject(g_set.root, "system_order");
	for (int i = 0; i < g_nsys; i++) {
		int src = i == s ? j : i == j ? s : i;
		cJSON_AddItemToArray(order, cJSON_CreateString(g_sys[src].code));
	}
	char code[32];
	sncpy(code, g_sys[s].code, sizeof code);
	rescan();
	for (int d = 0; d < depth; d++)
		if (stack[d].type == V_MENU && stack[d].menu == M_SYSTEM) stack[d].arg = data_sys_index(code);
	core_opts_sys = -1;
}

static void open_overlay(int kind, const char *title);
static void ov_add(const char *label, int act);

static void menu_change(View *mv, int id, int dir) {
	int menu = mv->menu, arg = mv->arg;
	int activate = dir == 0;
	if (activate) dir = 1;
	switch (menu) {
	case M_ROOT:
		push_menu(id, 0);
		if (id == M_WIFI) net_wifi_refresh(1);
		if (id == M_BT) net_bt_refresh(0);
		if (id == M_SEND) { net_wifi_refresh(0); net_send_start(); }
		return;
	case M_BT: {
		BtStatus bs;
		net_bt_status(&bs);
		if (net_busy()) { toast("One moment: %s", net_job()); return; }
		if (id == 1) { net_bt_power(!bs.enabled); dirty = 1; return; }
		if (!activate) return;
		if (id == 2) { net_bt_refresh(1); dirty = 1; return; }
		if (id >= 200) {
			BtDev d[16];
			int k = net_bt_list(d, 16), i = id - 200;
			if (i >= k) return;
			bt_chosen = d[i];
			if (!d[i].paired) { net_bt_pair(&d[i]); dirty = 1; return; }
			open_overlay(OV_CONFIRM, d[i].name);
			ov_add(d[i].connected ? "Disconnect" : "Connect", d[i].connected ? OA_BT_DISCONNECT : OA_BT_CONNECT);
			ov_add("Forget it", OA_BT_FORGET);
			ov_add("Cancel", OA_CANCEL);
		}
		return;
	}
	case M_WIFI: {
		WifiStatus ws;
		net_wifi_status(&ws);
		if (net_busy() && id != 0) { toast("One moment: %s", net_job()); return; }
		if (id == 1) { net_wifi_power(!ws.enabled); dirty = 1; return; }
		if (!activate) return;
		if (id == 2) { net_wifi_refresh(1); dirty = 1; return; }
		if (id >= 100) {
			WifiNet nets[16];
			int k = net_wifi_list(nets, 16), i = id - 100;
			if (i >= k) return;
			if (nets[i].connected) {
				char q[120];
				wifi_forget = nets[i];
				snfmt(q, sizeof q, "Forget %s? The handheld leaves it and won't join it again by itself.", nets[i].ssid);
				open_overlay(OV_CONFIRM, q);
				ov.confirm_action = 1005;
				ov_add("Cancel", OA_CANCEL);
				ov_add("Forget it", OA_YES);
			} else if (nets[i].saved || !nets[i].secured) {
				net_wifi_connect(&nets[i], "");
			} else {
				char t[96];
				wifi_pending = nets[i];
				snfmt(t, sizeof t, "Password for %s", nets[i].ssid);
				kb_open(KB_WIFI, t, "");
			}
			dirty = 1;
		}
		return;
	}
#ifdef ZAPPO_KNULLI
	case M_LIGHTS:
		switch (id) {
		case 1: g_set.lights = cycle(g_set.lights, LIGHTS_COUNT, dir); break;
		case 2: g_set.lights_colour = cycle(g_set.lights_colour, LIGHT_COLOURS, dir); break;
		case 3: g_set.lights_effect = cycle(g_set.lights_effect, 4, dir); break;
		case 4: {
			char v[8];
			snfmt(v, sizeof v, "%d", CLAMP((plat_conf_int("led.brightness", 70) / 10 + dir) * 10, 10, 100));
			plat_conf_set("led.brightness", v);
			break;
		}
		case 5: plat_conf_set("led.brightness.adaptive", plat_conf_int("led.brightness.adaptive", 1) ? "0" : "1"); break;
		default: return;
		}
		plat_lights_apply();
		mark_settings();
		dirty = 1;
		return;
	case M_ACH: {
		char user[64], pass[8];
		plat_conf_get("global.retroachievements.username", user, sizeof user);
		int have = user[0] && plat_conf_get("global.retroachievements.password", pass, sizeof pass) && pass[0];
		if (id == 2 || (id == 1 && !have)) {
			if (!activate && id == 2) return;
			if (net_busy()) { toast("One moment: %s", net_job()); return; }
			kb_open(KB_RA_USER, "RetroAchievements name", user);
			return;
		}
		if (id == 1) {
			plat_conf_set("global.retroachievements", plat_conf_int("global.retroachievements", 0) ? "0" : "1");
		} else if (id >= 3 && id <= 8) {
			static const char *K[] = {"hardcore", "leaderboards", "richpresence", "screenshot", "challenge_indicators", "encore"};
			char k[96];
			snfmt(k, sizeof k, "global.retroachievements.%s", K[id - 3]);
			plat_conf_set(k, plat_conf_int(k, 0) ? "0" : "1");
		} else if (id == 9) {
			if (!activate) return;
			plat_conf_set("global.retroachievements.password", NULL);
			plat_conf_set("global.retroachievements.token", NULL);
			plat_conf_set("global.retroachievements", "0");
			ra_score = -1;
			toast("Signed out of RetroAchievements");
		}
		dirty = 1;
		return;
	}
#endif
	case M_APPEAR: case M_HOME: case M_GAMES:
		switch (id) {
		case 18: g_set.video_wall = !g_set.video_wall; video_reclamp(); break;
		case 20: g_set.font_weight = cycle(g_set.font_weight, 3, dir); font_set_weight(g_set.font_weight); dirty = 1; break;
		case 19: push_menu(M_SYSTEMS, 0); return;
		case 1: {
			nthemes = theme_list(themes, ARRAY_LEN(themes));
			int cur = 0;
			for (int i = 0; i < nthemes; i++) if (str_ieq(themes[i], g_set.theme)) cur = i;
			sncpy(g_set.theme, themes[cycle(cur, nthemes, dir)], sizeof g_set.theme);
			apply_theme();
			break;
		}
		case 2: g_set.home_layout = cycle(g_set.home_layout + 1, HL_COUNT + 1, dir) - 1; break;
		case 3: g_set.game_layout = cycle(g_set.game_layout + 1, GL_COUNT + 1, dir) - 1; break;
		case 4: g_set.show_art ^= 1; break;
		case 5: g_set.clean_names ^= 1; break;
		case 6: g_set.animations ^= 1; break;
		case 7: g_set.clock_mode = cycle(g_set.clock_mode, 3, dir); break;
		case 8: g_set.battery_mode = cycle(g_set.battery_mode, 3, dir); break;
		case 9: g_set.show_guide ^= 1; break;
		case 10: g_set.empty_consoles ^= 1; refresh_visible(); break;
		case 11: push_menu(M_TABS, 0); return;
		case 12: {
			int t = g_set.start_tab;
			do t = cycle(t, TAB_COUNT, dir); while (!g_set.tab_visible[t] && t != TAB_CONSOLES);
			g_set.start_tab = t;
			break;
		}
		case 13: g_set.remember ^= 1; break;
		case 15: g_set.resume_games ^= 1; break;
		case 16: g_set.style = g_set.style == STYLE_GLASS ? STYLE_CLASSIC : STYLE_GLASS; apply_theme(); break;
		case 17: g_set.music_layout = cycle(g_set.music_layout, MLAY_COUNT, dir); music_relayout(); break;
		case 14: push_menu(M_CLOCK, 0); return;
#ifdef ZAPPO_KNULLI
		case 21: {
			if (!activate) return;
			ArtStatus a;
			net_art_status(&a);
			if (a.running) { net_art_stop(); toast("Stopped. The %d pictures found so far stay", a.found); }
			else art_start("", "every console");
			dirty = 1;
			return;
		}
#endif
		}
		mark_settings();
		return;
	case M_CLOCK: {
		if (id == 3) { if (!activate) return; g_set.clock_offset = 0; toast("Using the device clock"); }
		else if (id == 1 || id == 2) {
			if (activate) return;
			int o = g_set.clock_offset + dir * (id == 1 ? 3600 : 60);
			if (id == 2) {
				/* minutes wrap within the hour instead of changing it */
				time_t t = time(NULL) + g_set.clock_offset;
				struct tm tmv;
				localtime_r(&t, &tmv);
				if (dir > 0 && tmv.tm_min == 59) o -= 3600;
				if (dir < 0 && tmv.tm_min == 0) o += 3600;
			}
			g_set.clock_offset = o;
		} else if (id == 4 || id == 5 || id == 6) {
			if (activate) return;
			time_t now = time(NULL);
			time_t t = now + g_set.clock_offset;
			struct tm tmv;
			localtime_r(&t, &tmv);
			if (id == 4) tmv.tm_mday += dir;
			if (id == 5) tmv.tm_mon += dir;
			if (id == 6) tmv.tm_year = CLAMP(tmv.tm_year + dir, 100, 137);
			tmv.tm_isdst = -1;
			time_t nt = mktime(&tmv);
			if (nt != (time_t)-1) g_set.clock_offset = (int)(nt - now);
		}
		mark_settings();
		return;
	}
	case M_PICK: {
		if (!activate) return;
		char err[256];
		const char *where = id == IMPORT_TO_BIOS ? "BIOS" : id == IMPORT_TO_MUSIC ? "Music" : data_sys_name(id);
		char wherebuf[96];
		sncpy(wherebuf, where, sizeof wherebuf);
		if (import_move(pick_path, id, err, sizeof err) != 0) { toast("%s", err[0] ? err : "Could not move it"); return; }
		pop_view(); /* back to the Import list */
		rescan();
		View *lv = top();
		if (lv->type == V_LIST) {
			load_list(lv);
			clamp_cursor(&lv->cur, lv->items.n, list_rows(), 1);
			if (lv->items.n == 0) pop_view();
		}
		toast("Moved to %s", wherebuf);
		return;
	}
	case M_TABS:
		g_set.tab_visible[id] ^= 1;
		if (!g_set.tab_visible[g_set.start_tab]) g_set.start_tab = TAB_CONSOLES;
		mark_settings();
		return;
	case M_SYSTEMS:
		if (id == -2) {
			if (!activate) return;
			open_overlay(OV_CONFIRM, "Reset every system's settings?");
			ov.confirm_action = 1001;
			ov_add("Cancel", OA_CANCEL);
			ov_add("Reset all", OA_YES);
			return;
		}
		if (activate) { core_opts_sys = -1; push_menu(M_SYSTEM, id); }
		return;
	case M_SYSTEM: {
		const char *code = g_sys[arg].code;
		switch (id) {
		case 1: sys_set_int(code, "hidden", !sys_get_int(code, "hidden", 0)); refresh_visible(); break;
		case 2: sys_set_int(code, "name_style", cycle(sys_get_int(code, "name_style", 0), 3, dir)); break;
		case 3: if (activate) move_system(arg, -1); break;
		case 4: if (activate) move_system(arg, +1); break;
		case 5: {
			if (core_opts.n == 0) break;
			int cur = sl_find(&core_opts, data_sys_core(arg));
			int nx = cycle(cur < 0 ? 0 : cur, core_opts.n, dir);
			sys_set_str(code, "core", nx == 0 ? NULL : core_opts.v[nx]);
			break;
		}
		case 6: {
			int opts[40];
			int n = cpu_options(opts, 40);
			int v = sys_get_int(code, "cpu", -1), cur = 0;
			for (int i = 0; i < n; i++) if (opts[i] == v) cur = i;
			int nx = opts[cycle(cur, n, dir)];
			if (nx < 0) { cJSON *o = settings_system(code, 0); if (o) cJSON_DeleteItemFromObjectCaseSensitive(o, "cpu"); }
			else sys_set_int(code, "cpu", nx);
			break;
		}
#ifdef ZAPPO_KNULLI
		case 7: kopt_cycle(arg, "smooth", dir); break;
		case 8: kopt_cycle(arg, "integerscale", dir); break;
		case 9: kopt_cycle(arg, "video_threaded", dir); break;
		case 10: {
			static const int L[] = {-1, 64, 96, 128};
			int v = kopt(arg, "audio_latency"), cur = 0;
			for (int i = 0; i < 4; i++) if (L[i] == v) cur = i;
			kopt_set(arg, "audio_latency", L[cycle(cur, 4, dir)]);
			break;
		}
		case 11: kopt_cycle(arg, "showFPS", dir); break;
		case 13:
			if (!activate) return;
			art_start(g_sys[arg].dir, data_sys_name(arg));
			dirty = 1;
			return;
#else
		case 7: sys_set_int(code, "filter", !sys_get_int(code, "filter", 0)); break;
		case 8: sys_set_int(code, "integer", cycle(sys_get_int(code, "integer", -1) + 1, 3, dir) - 1); break;
		case 9: sys_set_int(code, "threaded", cycle(sys_get_int(code, "threaded", -1) + 1, 3, dir) - 1); break;
		case 10: {
			static const int L[] = {0, 64, 96, 128};
			int v = sys_get_int(code, "latency", 0), cur = 0;
			for (int i = 0; i < 4; i++) if (L[i] == v) cur = i;
			sys_set_int(code, "latency", L[cycle(cur, 4, dir)]);
			break;
		}
		case 11: sys_set_int(code, "fps", !sys_get_int(code, "fps", 0)); break;
#endif
		case 12:
			if (!activate) return;
#ifdef ZAPPO_KNULLI
			if (g_sys[arg].dir[0]) {
				static const char *K[] = {"smooth", "integerscale", "video_threaded", "audio_latency", "showFPS"};
				for (int i = 0; i < ARRAY_LEN(K); i++) kopt_set(arg, K[i], -1);
			}
#endif
			sys_reset(code);
			core_opts_sys = -1;
			rescan();
			mv->arg = data_sys_index(code);
			toast("%s reset to defaults", data_sys_name(mv->arg));
			break;
		}
		mark_settings();
		return;
	}
	case M_PERF:
		if (id == 1) {
			static const int M[] = {504000, 720000, 1008000};
			int cur = 2;
			for (int i = 0; i < 3; i++) if (M[i] == g_set.menu_cpu) cur = i;
			g_set.menu_cpu = M[cycle(cur, 3, dir)];
			plat_cpu_set(NULL, g_set.menu_cpu, 0);
		} else if (id == 2 && activate) {
			for (int i = 0; i < g_nsys; i++) { cJSON *o = settings_system(g_sys[i].code, 0); if (o) cJSON_DeleteItemFromObjectCaseSensitive(o, "cpu"); }
			toast("Every system now uses its recommended speed");
		} else if (id == 3 && activate) {
			for (int i = 0; i < g_nsys; i++) sys_set_int(g_sys[i].code, "cpu", 0);
			toast("Every system now uses GarlicOS automatic");
		}
		mark_settings();
		return;
	case M_CONTROLS:
		if (id == 1) g_set.swap_ab ^= 1;
		if (id == 2 && activate) { push_menu(M_SENS, 0); return; }
		if (id == 3 && activate) {
			if (depth < ARRAY_LEN(stack)) {
				View *v = &stack[depth++];
				memset(v, 0, sizeof *v);
				v->type = V_INPUT;
				itest_n = 0;
				itest_last[0] = 0;
			}
			dirty = 1;
			return;
		}
		mark_settings();
		return;
	case M_SENS:
		if (id == 1) g_set.repeat_delay = activate ? (g_set.repeat_delay + 1) % 5 : CLAMP(g_set.repeat_delay + dir, 0, 4);
		if (id == 2) g_set.repeat_rate = activate ? (g_set.repeat_rate + 1) % 5 : CLAMP(g_set.repeat_rate + dir, 0, 4);
		if (id == 3) g_set.repeat_accel ^= 1;
		mark_settings();
		return;
	case M_POWER:
		if (id == 5 || id == 6) {
			static const int SV[] = {0, 1, 2, 3, 5, 10, 15, 30};
			static const int OF[] = {0, 5, 10, 15, 30, 60};
			const int *L = id == 5 ? SV : OF;
			int n = id == 5 ? ARRAY_LEN(SV) : ARRAY_LEN(OF);
			int *v = id == 5 ? &g_set.saver_min : &g_set.off_min;
			int cur = 0;
			for (int i = 0; i < n; i++) if (L[i] == *v) cur = i;
			*v = L[cycle(cur, n, dir)];
			mark_settings();
			return;
		}
		if (id == 7 && activate) { saver_start(); return; }
#ifdef ZAPPO_KNULLI
		if (id == 20) {
			static const char *TV[] = {"hdmi-1080p", "hdmi-720p", "hdmi-480p"};
			char v[32];
			plat_conf_get("global.videooutput", v, sizeof v);
			int cur = 0;
			for (int i = 0; i < 3; i++) if (!strcmp(v, TV[i])) cur = i;
			plat_conf_set("global.videooutput", TV[cycle(cur, 3, dir)]);
			if (plat_tv()) g_state = UI_RESTART; /* (shown again at the new size) */
			dirty = 1;
			return;
		}
#endif
		if (id == 8) { g_set.sleep_clock = !g_set.sleep_clock; mark_settings(); dirty = 1; return; }
		if (id == 10) { g_set.battery_time ^= 1; mark_settings(); return; }
		if (id == 11) {
			if (activate) { saver_start(); return; }
			clockfonts_scan();
			int cur = 0;
			for (int i = 0; i < nclockfonts; i++) if (!strcmp(clockfonts[i], g_set.clock_font)) cur = i + 1;
			cur = cycle(cur, nclockfonts + 1, dir);
			if (cur == 0) g_set.clock_font[0] = 0;
			else sncpy(g_set.clock_font, clockfonts[cur - 1], sizeof g_set.clock_font);
			mark_settings();
			dirty = 1;
			return;
		}
		if (id == 9) {
			if (activate) { saver_start(); return; }
			lockpics_scan();
			/* Theme, Shuffle, then each picture */
			int n = nlockpics + 2, cur = 0;
			if (!strcmp(g_set.lock_pic, "*")) cur = 1;
			else for (int i = 0; i < nlockpics; i++) if (!strcmp(lockpics[i], g_set.lock_pic)) cur = i + 2;
			cur = cycle(cur, n, dir);
			if (cur == 0) g_set.lock_pic[0] = 0;
			else if (cur == 1) sncpy(g_set.lock_pic, "*", sizeof g_set.lock_pic);
			else sncpy(g_set.lock_pic, lockpics[cur - 2], sizeof g_set.lock_pic);
			lockpic_cur = -1;
			if (saver_img) { img_free(saver_img); saver_img = NULL; }
			if (glass() && !g_theme.background_sharp) { img_free(bg_img); bg_img = NULL; } /* Glass shows this picture behind everything */
			mark_settings();
			dirty = 1;
			return;
		}
		if (id == 1) {
			int b = CLAMP(plat_brightness_get() + (activate ? 1 : dir), 1, 10);
			if (activate && plat_brightness_get() == 10) b = 1;
			plat_brightness_set(b, 1);
			g_set.brightness = b;
			mark_settings();
		}
		if (!activate) return;
		if (id == 2) sleep_enter();
		if (id == 3 || id == 4) {
			open_overlay(OV_CONFIRM, id == 3 ? "Reboot now?" : "Power off now?");
			ov.confirm_action = id == 3 ? OA_REBOOT : OA_OFF;
			ov_add("Cancel", OA_CANCEL);
			ov_add(id == 3 ? "Reboot" : "Power off", OA_YES);
		}
		dirty = 1;
		return;
	case M_MUSIC:
		if (id == 1) { g_set.music_level ^= 1; music_set_levelling(g_set.music_level); }
		if (id == 2) { g_set.music_resume ^= 1; ui_save_music(); }
		if (id == 3) {
			/* Off, 15, 30, 45, 60, 90 min, end of the song */
			static const int SL[] = {0, 15, 30, 45, 60, 90, -1};
			char st[48];
			int cur = 0;
			if (sleep_timer_text(st, sizeof st)) cur = st[0] == 'a' ? 6 : 0;
			if (sleep_end) {
				int left = (int)((sleep_end - plat_ticks()) / 60000);
				for (int i = 1; i < 6; i++) if (SL[i] <= left + 1) cur = i;
			}
			sleep_timer_set(SL[cycle(cur, 7, dir)]);
		}
		if (id == 4) {
			g_set.music_volume = activate ? (g_set.music_volume + 10 > 100 ? 0 : g_set.music_volume + 10) : CLAMP(g_set.music_volume + dir * 5, 0, 100);
			music_set_volume(g_set.music_volume);
		}
		mark_settings();
		return;
	case M_ABOUT:
		if (!activate) return;
		if (id == 1 || id == 3) {
			ImportResult r;
			import_run(&r);
			rescan();
			core_opts_sys = -1;
			char msg[200];
			import_summary(&r, 1, msg, sizeof msg);
			if (r.music) music_lib_invalidate();
			if (msg[0]) toast("%s", msg);
			else toast(id == 1 ? "Games rescanned" : "Import folder is empty");
		}
		if (id == 2) {
			open_overlay(OV_CONFIRM, "Use the " FW_NAME " menu instead of Zappo?");
			ov.confirm_action = 1002;
			ov_add("Cancel", OA_CANCEL);
			ov_add("Switch to " FW_NAME, OA_YES);
		}
		return;
	}
}

/* Send to Zappo: the address (and its QR code) for a phone or PC on the same
 * Wi-Fi, and what has arrived; leaving sorts it all (Import) */
static void draw_send(void) {
	WifiStatus ws;
	net_wifi_status(&ws);
	int x = PAD, y = CONTENT_Y + 10, w = SCREEN_W - PAD * 2;
	if (!ws.connected || !ws.ip[0]) {
		text_draw_wrap(F_BODY, x + 8, y + 40, w - 16, 3, ws.known ? "Connect the handheld to Wi-Fi first (Settings > Wi-Fi), then come back here." :
		               "Checking the Wi-Fi...", g_theme.text, 1);
		return;
	}
	char url[80];
	snfmt(url, sizeof url, "http://%s:8640", ws.ip);
	static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
	int qs = 0;
	if (qrcodegen_encodeText(url, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true)) {
		int n = qrcodegen_getSize(qr), m = 210 / (n + 6);
		qs = m * (n + 6);
		gfx_rrect(x, y, qs, qs, 12, 0xFFFFFFFF);
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++)
				if (qrcodegen_getModule(qr, i, j)) gfx_rect(x + (i + 3) * m, y + (j + 3) * m, m, m, 0xFF101010);
	}
	int rx = x + qs + 24, rw = w - qs - 24;
	text_draw_wrap(F_SMALL, rx, y + 4, rw, 2, "On a phone or PC on the same Wi-Fi, scan the code or open:", g_theme.text_dim, 0);
	text_draw_fit(F_TITLE, rx, y + 52, rw, url, g_theme.accent, 0);
	text_draw_wrap(F_SMALL, rx, y + 104, rw, 3, net_send_running() ? "Games, songs, albums and films go to Import. Zappo sorts them when you leave this screen." :
	               "The page didn't start (send-to-zappo.py).", g_theme.text_dim, 0);
	char names[6][96];
	int k = net_send_received(names, 6);
	int ly = y + MAX(qs, 170) + 18;
	gfx_rect(x, ly - 8, w, 1, col_alpha(g_theme.text_dim, 50));
	if (!k) text_draw(F_SMALL, x + 4, ly, "Nothing received yet", g_theme.text_dim);
	for (int i = 0; i < k; i++) {
		int ry = ly + i * (font_height(F_SMALL) + 4);
		gfx_rrect(x + 4, ry + font_height(F_SMALL) / 2 - 4, 8, 8, 4, g_theme.accent); /* arrived */
		text_draw_fit(F_SMALL, x + 20, ry, w - 24, names[i], g_theme.text, 0);
	}
}

static void send_done(void) {
	net_send_stop();
	ImportResult r;
	import_run(&r);
	rescan();
	core_opts_sys = -1;
	if (r.music) music_lib_invalidate();
	char msg[200];
	import_summary(&r, 1, msg, sizeof msg);
	if (msg[0]) toast("%s", msg);
}

static void draw_menu(View *mv, int with_title) {
	(void)with_title;
	if (mv->menu == M_SEND) { draw_send(); return; }
	menu_build(mv->menu, mv->arg);
	if (nmi == 0) return;
	mv->cur.sel = CLAMP(mv->cur.sel, 0, nmi - 1);
	int rows = 8;
	int rh = 40;
	int area_h = rows * rh;
	clamp_cursor(&mv->cur, nmi, rows, 1);
	int x = PAD, w = SCREEN_W - PAD * 2;
	int y0 = CONTENT_Y + 4;
	gfx_clip(0, y0, SCREEN_W, area_h);
	float hy = y0 + (mv->cur.asel - mv->cur.atop) * rh;
	MItem *selm = &mi[mv->cur.sel];
	if (selm->kind != MK_INFO) draw_sel(x, (int)hy + 2, w, rh - 4, MIN(g_theme.radius, (rh - 4) / 2));
	for (int i = mv->cur.top - 1; i <= mv->cur.top + rows && i < nmi; i++) {
		if (i < 0) continue;
		MItem *m = &mi[i];
		int y = (int)(y0 + (i - mv->cur.atop) * rh);
		int sel = i == mv->cur.sel && m->kind != MK_INFO;
		uint32_t tc = sel ? sel_fg() : (m->kind == MK_INFO ? g_theme.text_dim : g_theme.text);
		uint32_t vc = sel ? sel_dim() : g_theme.text_dim;
		int ty = y + rh / 2 - font_height(F_BODY) / 2;
		int vw = 0;
		if (m->kind == MK_SUB) {
			int ax = x + w - 22, ay = y + rh / 2;
			for (int k = 0; k < 6; k++) { gfx_rect(ax + k, ay - 6 + k, 2, 1, vc); gfx_rect(ax + k, ay + 6 - k, 2, 1, vc); }
			vw = 30;
			if (m->value[0]) {
				int tw = text_width(F_SMALL, m->value);
				text_draw(F_SMALL, x + w - 34 - tw, y + rh / 2 - font_height(F_SMALL) / 2, m->value, vc);
				vw += tw + 10;
			}
		} else if (m->kind == MK_CHOICE) {
			char b[110];
			snfmt(b, sizeof b, sel ? "\xE2\x80\xB9  %s  \xE2\x80\xBA" : "%s", m->value);
			int tw = MIN(text_width(F_BODY, b), w / 2);
			text_draw_fit(F_BODY, x + w - 14 - tw, ty, tw, b, vc, 2);
			vw = tw + 20;
		} else if (m->kind == MK_ACTION && m->value[0]) {
			/* a note beside an action; "text|N" also draws N of 4 signal bars (Wi-Fi) */
			char note[96];
			sncpy(note, m->value, sizeof note);
			char *bar = strchr(note, '|');
			int bars = -1, bw = 0;
			if (bar) { *bar = 0; bars = CLAMP(atoi(bar + 1), 0, 4); bw = 4 * 6 + 10; }
			int tw = MIN(text_width(F_SMALL, note), w / 2);
			text_draw_fit(F_SMALL, x + w - 14 - bw - tw, y + rh / 2 - font_height(F_SMALL) / 2, tw, note, vc, 2);
			for (int k = 0; k < 4 && bars >= 0; k++) {
				int bh = 5 + k * 4, bx = x + w - 14 - bw + 10 + k * 6, by = y + rh / 2 + 9 - bh;
				gfx_rect(bx, by, 4, bh, k < bars ? vc : col_alpha(vc, 70));
			}
			vw = tw + bw + 20;
		}
		if (m->kind == MK_INFO) text_draw_fit(F_SMALL, x + 14, y + rh / 2 - font_height(F_SMALL) / 2, w - 28, m->label, tc, 0);
		else text_draw_fit(F_BODY, x + 14, ty, w - 28 - vw, m->label, tc, 0);
	}
	gfx_unclip();
	if (selm->help) {
		int hy2 = y0 + area_h + 4;
		gfx_rect(PAD, hy2, SCREEN_W - PAD * 2, 1, col_alpha(g_theme.text_dim, 50));
		text_draw_wrap(F_SMALL, PAD + 4, hy2 + 6, SCREEN_W - PAD * 2 - 8, 2, selm->help, g_theme.text_dim, 0);
	}
}

/* ---- overlays, toast, loading, input test */

static void open_overlay(int kind, const char *title) {
	memset(&ov, 0, sizeof ov);
	ov.kind = kind;
	sncpy(ov.title, title, sizeof ov.title);
	dirty = 1;
}
static void ov_add(const char *label, int act) {
	if (ov.n >= ARRAY_LEN(ov.act)) return;
	sncpy(ov.label[ov.n], label, sizeof ov.label[0]);
	ov.act[ov.n++] = act;
}

static void draw_overlay(void) {
	if (!ov.kind) return;
	gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_alpha(0, 150));
	int rh = 42, w = 400;
	/* a short title on one line; a longer message wraps in the smaller face */
	int tlines = text_width(F_BODY, ov.title) <= w - 40 ? 1 : text_wrap_lines(F_SMALL, w - 40, 5, ov.title);
	int th = (tlines == 1 ? font_height(F_BODY) : font_height(F_SMALL) * tlines) + 22;
	int vis = MIN(ov.n, 7); /* longer lists scroll */
	if (ov.sel < ov.top) ov.top = ov.sel;
	if (ov.sel >= ov.top + vis) ov.top = ov.sel - vis + 1;
	int h = th + vis * rh + 14;
	int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
	gfx_shadow(x, y + 6, w, h, g_theme.radius + 4, 14, g_theme.shadow);
	gfx_rrect(x, y, w, h, g_theme.radius + 4, solid_surface());
	if (tlines == 1) text_draw_fit(F_BODY, x + 20, y + 14, w - 40, ov.title, g_theme.text, 1);
	else text_draw_wrap(F_SMALL, x + 20, y + 14, w - 40, 5, ov.title, g_theme.text, 1);
	gfx_rect(x + 20, y + th, w - 40, 1, col_alpha(g_theme.text_dim, 60));
	int ly = y + th + 6;
	gfx_clip(x, ly, w, vis * rh);
	float hy = ly + (ov.asel - ov.top) * rh;
	draw_sel(x + 10, (int)hy + 2, w - 20, rh - 4, MIN(g_theme.radius, 16));
	for (int i = ov.top; i < ov.n && i < ov.top + vis; i++) {
		int iy = ly + (i - ov.top) * rh;
		text_draw_fit(F_BODY, x + 28, iy + rh / 2 - font_height(F_BODY) / 2, w - 56, ov.label[i], i == ov.sel ? sel_fg() : g_theme.text, 1);
	}
	gfx_unclip();
	if (ov.n > vis) {
		/* a thin scroll bar */
		int sh = MAX(16, vis * rh * vis / ov.n), sy = ly + (vis * rh - sh) * ov.top / MAX(1, ov.n - vis);
		gfx_rrect(x + w - 8, sy, 3, sh, 1, g_theme.text_dim);
	}
}

static void draw_toast(void) {
	if (!toast_msg[0] || plat_ticks() > toast_until) return;
	int tw = MIN(text_width(F_SMALL, toast_msg), SCREEN_W - 80);
	int w = tw + 36, h = 36;
	int x = (SCREEN_W - w) / 2, y = SCREEN_H - GUIDE_H - h - 10;
	gfx_shadow(x, y + 3, w, h, h / 2, 8, g_theme.shadow);
	gfx_rrect(x, y, w, h, h / 2, glass() ? solid_surface() : g_theme.surface_hi);
	if (glass()) gfx_rrect_stroke(x, y, w, h, h / 2, 1, RGBA(255, 255, 255, 40));
	text_draw_fit(F_SMALL, x + 18, y + h / 2 - font_height(F_SMALL) / 2, tw, toast_msg, g_theme.text, 1);
}

static void draw_loading(void) {
	draw_background();
	int y = 100;
	Image *im = NULL;
	if (launch_art[0]) {
		decode_budget = 1;
		im = cached_image(launch_art, 240, 220, IM_FIT);
	}
	if (im) {
		gfx_shadow((SCREEN_W - im->w) / 2, y + 6, im->w, im->h, g_theme.radius, 12, g_theme.shadow);
		gfx_blit_rounded(im, (SCREEN_W - im->w) / 2, y, g_theme.radius, 255);
		y += im->h + 24;
	} else if (launch_sys >= 0) {
		decode_budget = 1;
		draw_sys_icon(launch_sys, SCREEN_W / 2 - 110, y + 20, 220, 150, 255);
		y += 200;
	} else {
		y += 160;
	}
	int lines = text_draw_wrap(F_TITLE, 40, y, SCREEN_W - 80, 2, launch_name[0] ? launch_name : FW_MENU_TAB, g_theme.text, 1);
	text_draw_fit(F_SMALL, 40, y + font_height(F_TITLE) * lines + 8, SCREEN_W - 80, "Starting\xE2\x80\xA6", g_theme.text_dim, 1);
	/* videos: the player's buttons, shown while it opens */
	if (launch_sys >= 0 && strstr(data_sys_core(launch_sys), "ffmpeg")) {
		const Hint ROW1[] = {{"\xE2\x86\x90 \xE2\x86\x92", "Skip 10 s"}, {"L1 R1", "Skip 1 min"}};
		const Hint ROW2[] = {{confirm_btn(), "Pause"}, {back_btn(), "Back to Zappo"}};
		const Hint *rows[2] = {ROW1, ROW2};
		for (int r = 0; r < 2; r++) {
			int w = 0;
			for (int i = 0; i < 2; i++) w += hint_width(&rows[r][i]) + (i ? 22 : 0);
			int x = (SCREEN_W - w) / 2, hy = SCREEN_H - 96 + r * 34;
			for (int i = 0; i < 2; i++) {
				const Hint *h = &rows[r][i];
				draw_hint_button(x, hy, h->btn);
				int bw = (int)strlen(h->btn) > 1 ? text_width(F_SMALL, h->btn) + 14 : 24;
				text_draw(F_SMALL, x + bw + 6, hy + 12 - font_height(F_SMALL) / 2, h->label, g_theme.text_dim);
				x += hint_width(h) + 22;
			}
		}
	}
}

static void draw_video_prep(void) {
	draw_background();
	double f = video_prep_progress(vprep);
	int y = 96;
	if (vprep_sys >= 0) {
		decode_budget = 1;
		draw_sys_icon(vprep_sys, SCREEN_W / 2 - 80, y - 40, 160, 110, 255);
	}
	y += 92;
	text_draw_fit(F_TITLE, 40, y, SCREEN_W - 80, "Getting the video ready", g_theme.text, 1);
	y += font_height(F_TITLE) + 4;
	text_draw_fit(F_BODY, 40, y, SCREEN_W - 80, vprep_name, g_theme.accent, 1);
	y += font_height(F_BODY) + 22;
	int bw = SCREEN_W - 120, bx = 60;
	gfx_rrect(bx, y, bw, 14, 7, g_theme.surface);
	int fw = (int)(bw * CLAMP(f, 0, 1));
	if (fw > 0) gfx_rrect(bx, y, MAX(fw, 14), 14, 7, g_theme.accent);
	y += 28;
	char b[96];
	uint64_t el = plat_ticks() - vprep_t0;
	if (f > 0.03 && el > 3000) {
		int left = (int)((double)el * (1.0 - f) / f / 1000.0) + 1;
		if (left >= 90) snfmt(b, sizeof b, "%d%%  \xC2\xB7  about %d min left", (int)(f * 100), (left + 30) / 60);
		else snfmt(b, sizeof b, "%d%%  \xC2\xB7  under a minute left", (int)(f * 100));
	} else snfmt(b, sizeof b, "%d%%", (int)(f * 100));
	text_draw_fit(F_SMALL, 40, y, SCREEN_W - 80, b, g_theme.text, 1);
	y += font_height(F_SMALL) + 18;
	text_draw_wrap(F_SMALL, 50, y, SCREEN_W - 100, 3,
	               "The video player needs the film's index at the front of the file, so Zappo is moving it there. "
	               "This only happens the first time, and the picture isn't changed.",
	               g_theme.text_dim, 1);
	text_draw_fit(F_SMALL, 40, SCREEN_H - 34, SCREEN_W - 80, g_set.swap_ab ? "A  Stop" : "B  Stop", g_theme.text_dim, 1);
}

static void draw_input_test(void) {
	text_draw_wrap(F_SMALL, PAD + 4, CONTENT_Y + 6, SCREEN_W - PAD * 2, 2,
	               "Press any button to see what it sends. Hold B for one second to leave.", g_theme.text_dim, 0);
	int y = CONTENT_Y + 52;
	gfx_rrect(PAD, y, SCREEN_W - PAD * 2, 54, g_theme.radius, g_theme.surface);
	text_draw_fit(F_TITLE, PAD + 16, y + 27 - font_height(F_TITLE) / 2, SCREEN_W - PAD * 2 - 32,
	              itest_last[0] ? itest_last : "Waiting for a button\xE2\x80\xA6", g_theme.accent, 1);
	y += 66;
	for (int i = 0; i < itest_n; i++) {
		text_draw_fit(F_SMALL, PAD + 6, y, SCREEN_W - PAD * 2 - 12, itest[i], i == 0 ? g_theme.text : g_theme.text_dim, 0);
		y += font_height(F_SMALL) + 4;
	}
}

static void draw_sleep_hint(void) { gfx_clear(0); }

/* the screensaver picture plus how to unlock, top right under the battery */
static void draw_lock(void) {
	draw_saver();
	static const char *K[2] = {"L1", "R1"};
	const char *lbl = glass() ? "" : "together to unlock";
	int fh = font_height(F_SMALL);
	int cw = MAX(text_width(F_SMALL, K[0]), text_width(F_SMALL, K[1])) + 16;
	int plus = text_width(F_SMALL, "+");
	int pre = glass() ? 18 + 6 + text_width(F_SMALL, "Unlock") + 10 : 0;
	int w = pre + cw + 8 + plus + 8 + cw + (lbl[0] ? 10 + text_width(F_SMALL, lbl) : 0);
	int x = SCREEN_W - 24 - w, y = 58;
	if (glass()) {
		gfx_rrect(x - 12, y - 7, w + 24, 38, 19, RGBA(10, 8, 18, 178));
		gfx_rrect_stroke(x - 12, y - 7, w + 24, 38, 19, 1, RGBA(255, 255, 255, 60));
		/* a padlock */
		gfx_rrect_stroke(x + 3, y + 2, 12, 9, 5, 2, 0xFFF4EFE6);
		gfx_rrect(x + 1, y + 9, 16, 12, 3, 0xFFF4EFE6);
		text_draw(F_SMALL, x + 24, y + 12 - fh / 2, "Unlock", 0xFFF4EFE6);
		x += pre;
	} else gfx_rrect(x - 12, y - 7, w + 24, 38, 19, col_alpha(0x061614, 155));
	for (int i = 0; i < 2; i++) {
		int kx = x + i * (cw + 16 + plus);
		gfx_rrect(kx, y, cw, 24, 12, lock_down[i] ? (g_theme.accent | 0xFF000000u) : col_alpha(0xffffff, 70));
		text_draw(F_SMALL, kx + (cw - text_width(F_SMALL, K[i])) / 2, y + 12 - fh / 2, K[i], 0xFFF4EFE6);
	}
	text_draw(F_SMALL, x + cw + 8, y + 12 - fh / 2, "+", 0xFFC9DDD6);
	if (lbl[0]) text_draw(F_SMALL, x + 2 * cw + 16 + plus + 10, y + 12 - fh / 2, lbl, 0xFFC9DDD6);
	/* the song buttons, just above the now-playing box */
	MusicInfo m;
	music_info(&m);
	if (m.active) {
		static const char *A[3] = {"\xE2\x86\x90", "\xE2\x86\x92", "A"};
		const char *sl = m.paused ? "song  \xC2\xB7  A plays" : "song  \xC2\xB7  A pauses";
		int aw = 26, sw = aw * 2 + 6 + 10 + text_width(F_SMALL, sl);
		int sx = 32, sy = SCREEN_H - 24 - 68 - 38;
		if (glass()) { sx = SCREEN_W - 24 - sw - 2; sy = SCREEN_H - 24 - 38 - 12; }
		if (glass()) {
			gfx_rrect(sx - 12, sy - 7, sw + 24, 38, 19, RGBA(10, 8, 18, 178));
			gfx_rrect_stroke(sx - 12, sy - 7, sw + 24, 38, 19, 1, RGBA(255, 255, 255, 60));
		} else gfx_rrect(sx - 12, sy - 7, sw + 24, 38, 19, col_alpha(0x061614, 155));
		for (int i = 0; i < 2; i++) {
			gfx_rrect(sx + i * (aw + 6), sy, aw, 24, 12, col_alpha(0xffffff, 70));
			text_draw(F_SMALL, sx + i * (aw + 6) + (aw - text_width(F_SMALL, A[i])) / 2, sy + 12 - fh / 2, A[i], 0xFFF4EFE6);
		}
		text_draw(F_SMALL, sx + 2 * aw + 6 + 10, sy + 12 - fh / 2, sl, 0xFFC9DDD6);
	}
}

/* the screen showing now is a Shelf (its background is tinted from the art) */
static int shelf_screen(void) {
	if (sleeping || saver || g_state != UI_RUN || vprep) return 0;
	View *v = top();
	if (v->type == V_HOME && tab == TAB_CONSOLES) return nvis && effective_home_layout() == HL_SHELF;
	if (v->type == V_HOME && tab == TAB_MUSIC) return !music_np && !lib_dirty && g_set.music_layout == MLAY_SHELF && g_lib.nt > 0;
	View *lv = active_list();
	if (lv && lv->kind == LK_VIDEO) return 0;
	return lv && lv->items.n && effective_game_layout() == GL_SHELF && !(lv->kind == LK_SYSTEM && lv->sys >= 0 && g_sys[lv->sys].is_import);
}

void ui_render(void) {
	decode_budget = 2;
	pending_loads = 0;
	gfx_bind(plat_canvas(), SCREEN_W, SCREEN_H);
	if (sleeping) { if (g_set.sleep_clock || lock_peek_until) draw_lock(); else draw_sleep_hint(); dirty = 0; return; }
	if (g_state == UI_LAUNCH) { draw_loading(); dirty = 0; return; }
	if (vprep) { draw_video_prep(); dirty = 0; return; }
	if (saver) { draw_saver(); dirty = 0; return; }
	View *v = top();
	if (v->type == V_SHOTS) { draw_shots(); draw_overlay(); draw_toast(); dirty = 0; return; }
	draw_background();
	Hint h[6];
	int nh = 0;
	if (v->type == V_HOME && tab == TAB_MUSIC && music_np) {
		draw_now_playing();
		MusicInfo m;
		music_info(&m);
		h[nh++] = (Hint){"L2 R2", "Prev/next"};
		if (m.active) h[nh++] = (Hint){"Y", fav_song_has(m.path) ? "Unfavorite" : "Favorite"};
		h[nh++] = (Hint){"SELECT", m.shuffle ? "Shuffle off" : "Shuffle"};
		h[nh++] = (Hint){back_btn(), "Back"};
		h[nh++] = (Hint){confirm_btn(), m.paused ? "Play" : "Pause"};
	} else if (v->type == V_HOME) {
		draw_topbar(NULL);
		if (tab == TAB_CONSOLES) {
			draw_consoles();
			h[nh++] = (Hint){"L1 R1", "Tabs"};
			h[nh++] = (Hint){"SELECT", "Layout"};
			if (nvis) h[nh++] = (Hint){"X", "Options"};
			h[nh++] = (Hint){confirm_btn(), "Open"};
		} else if (tab == TAB_MUSIC) {
			draw_music();
			MusicInfo m;
			music_info(&m);
			if (!lib_dirty) {
				h[nh++] = (Hint){"SELECT", "View"};
				h[nh++] = (Hint){"X", "Options"};
				if (m.active) h[nh++] = (Hint){"START", "Playing"};
				const char *yl = NULL;
				if (music_hint_y(&yl)) h[nh++] = (Hint){"Y", yl};
				const char *al = NULL;
				if (music_hint_a(&al)) h[nh++] = (Hint){confirm_btn(), al};
			}
		} else if (tab == TAB_VIDEOS) {
			draw_videos(&v_video);
			nh += video_hints(h + nh, &v_video);
		} else if (tab == TAB_RECENT || tab == TAB_FAVORITES) {
			draw_list(tab == TAB_RECENT ? &v_recent : &v_fav);
			View *lv = tab == TAB_RECENT ? &v_recent : &v_fav;
			if (lv->items.n && effective_game_layout() == GL_SHELF)
				draw_shelf_pair(tab == TAB_RECENT ? "electric" : "crystal", tab == TAB_RECENT ? "fire" : "leaf");
			if (lv->items.n) {
				h[nh++] = (Hint){"X", "Options"};
				h[nh++] = (Hint){"Y", lv->items.v[lv->cur.sel].fav || tab == TAB_FAVORITES ? "Unfavorite" : "Favorite"};
				h[nh++] = (Hint){confirm_btn(), "Play"};
			}
		} else if (tab == TAB_RETROARCH) {
			int w = 360, hh = 200;
			int x = (SCREEN_W - w) / 2, y = CONTENT_Y + 50;
			gfx_shadow(x, y + 4, w, hh, g_theme.radius, 10, g_theme.shadow);
			gfx_rrect(x, y, w, hh, g_theme.radius, g_theme.surface);
			gfx_rrect_stroke(x - 3, y - 3, w + 6, hh + 6, g_theme.radius + 3, 3, g_theme.accent);
			text_draw_fit(F_TITLE, x, y + 50, w, FW_MENU_TAB, g_theme.text, 1);
			text_draw_wrap(F_SMALL, x + 24, y + 100, w - 48, 3, FW_MENU_TEXT, g_theme.text_dim, 1);
			h[nh++] = (Hint){confirm_btn(), "Open"};
		} else if (tab == TAB_SETTINGS) {
			draw_menu(&v_settings, 0);
			h[nh++] = (Hint){confirm_btn(), "Select"};
		}
	} else if (v->type == V_LIST) {
		char title[160];
		if (v->dir[0]) snfmt(title, sizeof title, "%s \xE2\x80\xBA %s", data_sys_name(v->sys), path_base(v->dir));
		else sncpy(title, data_sys_name(v->sys), sizeof title);
		draw_topbar(title);
		if (v->kind == LK_VIDEO) draw_videos(v);
		else draw_list(v);
		h[nh++] = (Hint){back_btn(), "Back"};
		if (v->kind == LK_VIDEO) {
			nh += video_hints(h + nh, v);
		} else if (v->items.n && g_sys[v->sys].is_import) {
			h[nh++] = (Hint){confirm_btn(), "Choose console"};
		} else if (v->items.n) {
			if (!v->items.v[v->cur.sel].is_dir) {
				h[nh++] = (Hint){"X", "Options"};
				h[nh++] = (Hint){"Y", v->items.v[v->cur.sel].fav ? "Unfavorite" : "Favorite"};
			}
			h[nh++] = (Hint){confirm_btn(), v->items.v[v->cur.sel].is_dir ? "Open" : "Play"};
		}
	} else if (v->type == V_MENU) {
		draw_topbar(menu_title(v->menu, v->arg));
		draw_menu(v, 1);
		if (v->menu == M_SEND) {
			h[nh++] = (Hint){back_btn(), "Done"};
		} else {
			h[nh++] = (Hint){back_btn(), "Back"};
			h[nh++] = (Hint){confirm_btn(), "Select"};
		}
	} else if (v->type == V_INPUT) {
		draw_topbar("Input test");
		draw_input_test();
	} else if (v->type == V_SEARCH) {
		draw_topbar("Search");
		draw_search();
		nh += search_hints(h + nh);
	}
	draw_guide(h, nh);
	draw_overlay();
	draw_kb_modal();
	draw_toast();
	dirty = 0;
}

/* ------------------------------------------------------------------ input */

static void move_cursor(Cursor *c, int n, int delta, int rows, int cols, int wrap) {
	if (n <= 0) return;
	int s = c->sel + delta;
	if (wrap && (s < 0 || s >= n) && abs(delta) == 1) s = (s + n) % n;
	else if (wrap && abs(delta) == cols && cols > 1 && (s < 0 || s >= n)) s = s < 0 ? n - 1 : 0;
	c->sel = CLAMP(s, 0, n - 1);
	clamp_cursor(c, n, rows, cols);
	dirty = 1;
}

static void jump_letter(View *v, int dir) {
	int n = v->items.n;
	if (!n) return;
	char cur = (char)tolower((unsigned char)v->items.v[v->cur.sel].name[0]);
	int i = v->cur.sel;
	for (int k = 0; k < n; k++) {
		i += dir;
		if (i < 0 || i >= n) break;
		char c = (char)tolower((unsigned char)v->items.v[i].name[0]);
		if (c != cur) {
			if (dir < 0) {
				/* go to the first entry of that letter */
				while (i > 0 && tolower((unsigned char)v->items.v[i - 1].name[0]) == c) i--;
			}
			v->cur.sel = i;
			break;
		}
	}
	dirty = 1;
}

static void game_options(View *v) {
	if (!v->items.n) return;
	Entry *e = &v->items.v[v->cur.sel];
	if (e->is_dir) return;
	open_overlay(OV_GAME, e->name);
	sncpy(ov.path, e->path, sizeof ov.path);
	sncpy(ov.name, e->name, sizeof ov.name);
	ov.sys = e->sys;
	if (data_resume_point(e, NULL, 0, NULL)) ov_add("Start over (from the beginning)", OA_STARTOVER);
	ov_add(e->fav ? "Remove from favorites" : "Add to favorites", OA_FAV);
	if (v->kind == LK_RECENT) ov_add("Remove from recent", OA_UNRECENT);
	if (e->sys >= 0) ov_add("Console settings", OA_SYSSET);
	ov_add("Cancel", OA_CANCEL);
}

static void toggle_fav(View *v) {
	if (!v->items.n) return;
	Entry *e = &v->items.v[v->cur.sel];
	if (e->is_dir) return;
	e->fav = data_fav_toggle(e);
	toast(e->fav ? "Added to favorites" : "Removed from favorites");
	if (v->kind == LK_FAV && !e->fav) {
		load_list(v);
		clamp_cursor(&v->cur, v->items.n, list_rows(), 1);
	}
}

static void open_pick(const Entry *e) {
	sncpy(pick_path, e->path, sizeof pick_path);
	pick_n = 0;
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < g_nsys && pick_n < ARRAY_LEN(pick_vis); i++) {
			if (g_sys[i].is_import || (!g_sys[i].on_tf1 && !g_sys[i].on_tf2)) continue;
			if ((pass == 0) != (g_sys[i].count > 0)) continue;
			pick_vis[pick_n++] = i;
		}
	push_menu(M_PICK, 0);
}

static void list_button(View *v, int b) {
	if (v->kind == LK_SYSTEM && v->sys >= 0 && g_sys[v->sys].is_import && (b == B_A || b == B_X || b == B_Y || b == B_SELECT)) {
		if ((b == B_A || b == B_X) && v->items.n) open_pick(&v->items.v[v->cur.sel]);
		return;
	}
	int layout = effective_game_layout();
	int grid = layout == GL_GRID;
	int cols = grid ? game_cols() : 1;
	int rows = grid ? 2 : list_rows();
	int n = v->items.n;
	int old = v->cur.sel;
	if (layout == GL_SHELF) {
		/* the shelf: left / right one game, up / down and L1 / R1 five */
		switch (b) {
		case B_LEFT: move_cursor(&v->cur, n, -1, rows, 1, 1); b = -1; break;
		case B_RIGHT: move_cursor(&v->cur, n, 1, rows, 1, 1); b = -1; break;
		case B_UP: case B_L1: move_cursor(&v->cur, n, -5, rows, 1, 0); b = -1; break;
		case B_DOWN: case B_R1: move_cursor(&v->cur, n, 5, rows, 1, 0); b = -1; break;
		}
	}
	switch (b) {
	case B_UP: move_cursor(&v->cur, n, -cols, rows, cols, 1); break;
	case B_DOWN: move_cursor(&v->cur, n, cols, rows, cols, 1); break;
	case B_LEFT: move_cursor(&v->cur, n, grid ? -1 : -rows, rows, cols, grid); break;
	case B_RIGHT: move_cursor(&v->cur, n, grid ? 1 : rows, rows, cols, grid); break;
	case B_L1: move_cursor(&v->cur, n, -rows * cols, rows, cols, 0); break;
	case B_R1: move_cursor(&v->cur, n, rows * cols, rows, cols, 0); break;
	case B_L2: jump_letter(v, -1); clamp_cursor(&v->cur, n, rows, cols); break;
	case B_R2: jump_letter(v, 1); clamp_cursor(&v->cur, n, rows, cols); break;
	case B_A:
		if (!n) break;
		if (v->items.v[v->cur.sel].is_dir) push_list(v->items.v[v->cur.sel].sys, v->items.v[v->cur.sel].path);
		else launch_entry(&v->items.v[v->cur.sel], v->kind);
		break;
	case B_Y: toggle_fav(v); break;
	case B_X: game_options(v); break;
	case B_SELECT:
		g_set.game_layout = (effective_game_layout() + 1) % GL_COUNT;
		clamp_cursor(&v->cur, n, g_set.game_layout == GL_GRID ? 2 : list_rows(), g_set.game_layout == GL_GRID ? game_cols() : 1);
		mark_settings();
		break;
	}
	if (v->cur.sel != old) v->sel_time = plat_ticks();
}

/* the Music tab; music_button returns 1 when it used the button */
#include "ui_music.inc"
#include "ui_extra.inc"
#include "ui_video.inc"

static void consoles_button(int b) {
	int layout = effective_home_layout();
	int cols = layout == HL_GRID ? 4 : 1;
	int rows = layout == HL_GRID ? 3 : list_rows();
	switch (b) {
	case B_LEFT:
		if (layout == HL_LIST) move_cursor(&sys_cur, nvis, -rows, rows, 1, 0);
		else move_cursor(&sys_cur, nvis, -1, rows, cols, 1);
		break;
	case B_RIGHT:
		if (layout == HL_LIST) move_cursor(&sys_cur, nvis, rows, rows, 1, 0);
		else move_cursor(&sys_cur, nvis, 1, rows, cols, 1);
		break;
	case B_UP: if (layout != HL_CAROUSEL && layout != HL_SHELF) move_cursor(&sys_cur, nvis, -cols, rows, cols, 1); break;
	case B_DOWN: if (layout != HL_CAROUSEL && layout != HL_SHELF) move_cursor(&sys_cur, nvis, cols, rows, cols, 1); break;
	case B_L2: move_cursor(&sys_cur, nvis, -5, rows, cols, 0); break;
	case B_R2: move_cursor(&sys_cur, nvis, 5, rows, cols, 0); break;
	case B_A:
		if (nvis) push_list(vis[sys_cur.sel], NULL);
		break;
	case B_SELECT:
		g_set.home_layout = (layout + 1) % HL_COUNT;
		clamp_cursor(&sys_cur, nvis, g_set.home_layout == HL_GRID ? 3 : list_rows(), g_set.home_layout == HL_GRID ? 4 : 1);
		mark_settings();
		break;
	}
}

static void menu_button(View *mv, int b) {
	menu_build(mv->menu, mv->arg);
	if (!nmi) return;
	switch (b) {
	case B_UP:
	case B_DOWN: {
		int d = b == B_UP ? -1 : 1;
		int s = mv->cur.sel;
		for (int k = 0; k < nmi; k++) {
			s = (s + d + nmi) % nmi;
			if (mi[s].kind != MK_INFO) break;
		}
		mv->cur.sel = s;
		clamp_cursor(&mv->cur, nmi, 8, 1);
		dirty = 1;
		break;
	}
	case B_LEFT:
	case B_RIGHT:
		if (mi[mv->cur.sel].kind == MK_CHOICE) menu_change(mv, mi[mv->cur.sel].id, b == B_LEFT ? -1 : 1);
		break;
	case B_A:
		if (mi[mv->cur.sel].kind != MK_INFO) menu_change(mv, mi[mv->cur.sel].id, 0);
		break;
	}
}

static void switch_tab(int dir) {
	int tabs[TAB_COUNT];
	int nt = tab_list(tabs), cur = 0;
	for (int i = 0; i < nt; i++) if (tabs[i] == tab) cur = i;
	enter_tab(tabs[(cur + dir + nt) % nt]);
}

static void overlay_button(int b) {
	if (b == B_UP || b == B_DOWN) {
		ov.sel = (ov.sel + (b == B_UP ? -1 : 1) + ov.n) % ov.n;
		dirty = 1;
		return;
	}
	if (b == B_B) { ov.kind = OV_NONE; dirty = 1; return; }
	if (b != B_A) return;
	int act = ov.act[ov.sel];
	int kind = ov.kind;
	ov.kind = OV_NONE;
	dirty = 1;
	if (music_overlay_action(act)) return;
	if (video_overlay_action(act)) return;
	switch (act) {
	case OA_FAV: {
		Entry e = {ov.name, ov.path, ov.sys, 0, 0};
		int on = data_fav_toggle(&e);
		toast(on ? "Added to favorites" : "Removed from favorites");
		View *lv = active_list();
		if (lv) {
			if (lv->kind == LK_FAV) { load_list(lv); clamp_cursor(&lv->cur, lv->items.n, list_rows(), 1); }
			else for (int i = 0; i < lv->items.n; i++) if (!strcmp(lv->items.v[i].path, ov.path)) lv->items.v[i].fav = (uint8_t)on;
		}
		break;
	}
	case OA_STARTOVER: {
		Entry e = {ov.name, ov.path, ov.sys, 0, 0};
		data_start_over = 1;
		launch_entry(&e, active_list() ? active_list()->kind : LK_SYSTEM);
		if (g_state != UI_LAUNCH && !vprep) data_start_over = 0; /* didn't start */
		break;
	}
	case OA_SEARCH: search_open(); break;
	case OA_SURPRISE: surprise_me(); break;
	case OA_NOWPLAY:
		for (int g = 0; g < 16 && top()->type != V_HOME; g++) {
			if (top()->type == V_SHOTS) shots_free();
			pop_view();
		}
		if (top()->type != V_HOME) break;
		enter_tab(TAB_MUSIC);
		music_np = 1;
		dirty = 1;
		break;
	case OA_MPAUSE: music_toggle_pause(); dirty = 1; break;
	case OA_SHOTS: shots_open(); break;
	case OA_UNRECENT:
		data_recent_remove(ov.path);
		enter_tab(TAB_RECENT);
		toast("Removed from recent");
		break;
	case OA_SYSSET: core_opts_sys = -1; push_menu(M_SYSTEM, ov.sys); break;
	case OA_SETTINGS:
		if (top()->type == V_HOME) enter_tab(TAB_SETTINGS);
		else push_menu(M_ROOT, 0);
		break;
	case OA_SLEEP: sleep_enter(); dirty = 1; break;
	case OA_OFF: case OA_REBOOT:
		save_now();
		ui_save_music();
		plat_power(act == OA_REBOOT);
		break;
	case OA_BT_CONNECT: net_bt_connect(&bt_chosen, 1); break;
	case OA_BT_DISCONNECT: net_bt_connect(&bt_chosen, 0); break;
	case OA_BT_FORGET: net_bt_forget(&bt_chosen); break;
	case OA_YES:
		if (kind != OV_CONFIRM) break;
		if (ov.confirm_action == OA_OFF || ov.confirm_action == OA_REBOOT) { save_now(); ui_save_music(); plat_power(ov.confirm_action == OA_REBOOT); }
		if (ov.confirm_action == 1001) {
			for (int i = 0; i < g_nsys; i++) {
#ifdef ZAPPO_KNULLI
				static const char *K[] = {"smooth", "integerscale", "video_threaded", "audio_latency", "showFPS"};
				if (g_sys[i].dir[0])
					for (int k = 0; k < ARRAY_LEN(K); k++) if (kopt(i, K[k]) >= 0) kopt_set(i, K[k], -1);
#endif
				sys_reset(g_sys[i].code);
			}
			core_opts_sys = -1;
			rescan();
			mark_settings();
			toast("All systems reset");
		}
		if (ov.confirm_action == 1004) music_delete_playlist(ov.sys);
		if (ov.confirm_action == 1005) net_wifi_forget(&wifi_forget);
		if (ov.confirm_action == 1003 && pend_sys >= 0) {
			Entry e = {pend_name, pend_path, pend_sys, 0, 0};
			launch_now(&e, pend_kind);
		}
		if (ov.confirm_action == 1002) {
			char p[512];
			snfmt(p, sizeof p, "%s/disabled", g_paths.clove);
			file_write_all(p, "Delete this file (or run \"Switch to Clove\" in Apps) to use Clove again.\n", 72);
			save_now();
			g_state = UI_QUIT;
		}
		break;
	}
}

/* X on the Consoles tab: the console's options, as X is everywhere else */
static void console_options(void) {
	if (!nvis) { search_open(); return; }
	int si = vis[sys_cur.sel];
	open_overlay(OV_GAME, data_sys_name(si));
	ov.sys = si;
	ov_add("Search\xE2\x80\xA6", OA_SEARCH);
	if (!g_sys[si].is_import) {
		char l[96];
		snfmt(l, sizeof l, "%s settings", data_sys_name(si));
		ov_add(l, OA_SYSSET);
	}
	ov_add("Surprise me (a random game)", OA_SURPRISE);
	ov_add("Cancel", OA_CANCEL);
}

static void handle_button(int b, int repeat) {
	if ((b == B_VOLUP || b == B_VOLDN) && music_is_playing()) {
		g_set.music_volume = CLAMP(g_set.music_volume + (b == B_VOLUP ? 5 : -5), 0, 100);
		music_set_volume(g_set.music_volume);
		mark_settings();
		toast("Music volume %d%%", g_set.music_volume);
		return;
	}
	if (b == B_VOLUP || b == B_VOLDN) {
		int lvl = CLAMP(plat_brightness_get() + (b == B_VOLUP ? 1 : -1), 1, 10);
		plat_brightness_set(lvl, 1);
		g_set.brightness = lvl;
		mark_settings();
		toast("Brightness %d", lvl);
		return;
	}
	if (b == B_POWER && !repeat) {
		open_overlay(OV_POWER, "Power");
		ov_add("Lock screen", OA_SLEEP);
		ov_add("Power off", OA_OFF);
		ov_add("Reboot", OA_REBOOT);
		ov_add("Cancel", OA_CANCEL);
		return;
	}
	if (kb.purpose && kb.purpose != KB_SEARCH) { kb_button(b); return; }
	if (ov.kind) { overlay_button(b); return; }
	if (b == B_MENU && !repeat) {
		char t[96], left[64];
		int pct = plat_battery();
		if (pct >= 0 && batt_left_text(left, sizeof left)) snfmt(t, sizeof t, "Battery %d%%, %s", pct, left);
		else if (pct >= 0) snfmt(t, sizeof t, "Zappo  \xC2\xB7  battery %d%%", pct);
		else sncpy(t, "Zappo", sizeof t);
		open_overlay(OV_QUICK, t);
		{
			MusicInfo mm;
			music_info(&mm);
			if (mm.active) {
				if (!(top()->type == V_HOME && tab == TAB_MUSIC && music_np)) ov_add("Now Playing", OA_NOWPLAY);
				ov_add(mm.paused ? "Play music" : "Pause music", OA_MPAUSE);
			}
		}
		if (top()->type != V_SEARCH) ov_add("Search", OA_SEARCH);
		ov_add("Surprise me (a random game)", OA_SURPRISE);
		if (top()->type != V_SHOTS) ov_add("Screenshots", OA_SHOTS);
		ov_add("Settings", OA_SETTINGS);
		ov_add("Lock screen", OA_SLEEP);
		ov_add("Power off", OA_OFF);
		ov_add("Reboot", OA_REBOOT);
		ov_add("Cancel", OA_CANCEL);
		return;
	}
	View *v = top();
	if (v->type == V_INPUT) return;
	if (v->type == V_SEARCH) { search_button(b); return; }
	if (v->type == V_SHOTS) { shots_button(b); return; }
	if (v->type == V_HOME) {
		if (tab == TAB_MUSIC && music_np) { if (!repeat || b == B_LEFT || b == B_RIGHT) music_button(b); return; }
		if (b == B_L1 || b == B_R1) { switch_tab(b == B_L1 ? -1 : 1); return; }
		if (tab == TAB_MUSIC && b == B_START && !repeat) { if (music_button(b)) return; }
		if (b == B_START && !repeat) { enter_tab(TAB_SETTINGS); if (!g_set.tab_visible[TAB_SETTINGS]) push_menu(M_ROOT, 0); return; }
		if (tab == TAB_MUSIC) { if (music_button(b)) return; }
		if (tab == TAB_CONSOLES && b == B_X && !repeat) { console_options(); return; }
		if (tab == TAB_CONSOLES) consoles_button(b);
		else if (tab == TAB_RECENT) list_button(&v_recent, b);
		else if (tab == TAB_FAVORITES) list_button(&v_fav, b);
		else if (tab == TAB_VIDEOS) video_button(&v_video, b);
		else if (tab == TAB_RETROARCH) {
			if (b == B_A) {
				if (data_launch_retroarch() == 0) { launch_name[0] = 0; launch_art[0] = 0; launch_sys = -1; save_now(); g_state = UI_LAUNCH; dirty = 1; }
				else toast("Could not start " FW_MENU_TAB);
			}
		} else if (tab == TAB_SETTINGS) {
			menu_button(&v_settings, b);
		}
		if (b == B_B && tab != TAB_CONSOLES && !repeat) enter_tab(TAB_CONSOLES);
		return;
	}
	if (b == B_B && !repeat) { pop_view(); return; }
	if (v->type == V_LIST) {
		if (b == B_START && !repeat) { if (!g_sys[v->sys].is_import) { core_opts_sys = -1; push_menu(M_SYSTEM, v->sys); } return; }
		if (v->kind == LK_VIDEO) video_button(v, b);
		else list_button(v, b);
	} else if (v->type == V_MENU) {
		menu_button(v, b);
	}
}

static int repeatable(int b) {
	return b == B_UP || b == B_DOWN || b == B_LEFT || b == B_RIGHT || b == B_L1 || b == B_R1 || b == B_L2 || b == B_R2;
}

void ui_event(const InputEvent *ev) {
	if (ev->button == B_QUIT) { g_state = UI_QUIT; return; }
	if (ev->pressed) last_input = plat_ticks();
	if (sleeping) {
		int lb = ev->button;
		if (lb == B_L1 || lb == B_R1) {
			lock_down[lb == B_R1] = ev->pressed ? 1 : 0;
			if (lock_down[0] && lock_down[1]) {
				sleep_leave();
				saver = 0;
				memset(held, 0, sizeof held);
				dirty = 1;
				return;
			}
		}
		/* previous / next song and pause still work while locked */
		if (ev->pressed && (lb == B_LEFT || lb == B_L2)) music_prev();
		if (ev->pressed && (lb == B_RIGHT || lb == B_R2)) music_next();
		if (ev->pressed && lb == B_A && music_is_active()) music_toggle_pause();
		if (ev->pressed && (lb == B_VOLUP || lb == B_VOLDN) && music_is_playing()) {
			/* the music volume still works while locked */
			g_set.music_volume = CLAMP(g_set.music_volume + (lb == B_VOLUP ? 5 : -5), 0, 100);
			music_set_volume(g_set.music_volume);
			mark_settings();
		}
		if (ev->pressed) {
			/* POWER while lit puts it straight back to sleep; anything else lights it up */
			if (lb == B_POWER && lock_peek_until) lock_dark();
			else lock_peek();
		}
		dirty = 1;
		return;
	}
	if (vprep) {
		if (ev->pressed && ev->button == (g_set.swap_ab ? B_A : B_B)) {
			video_prep_cancel(vprep);
			vprep = NULL;
			toast("Stopped. The video is just as it was.");
			dirty = 1;
		}
		return;
	}
	if (saver) {
		/* any button just wakes the menu; it isn't passed on */
		if (ev->pressed) { saver = 0; memset(held, 0, sizeof held); dirty = 1; }
		return;
	}
	if (top()->type == V_INPUT && ev->raw_type) {
		if (ev->pressed || ev->raw_type == 3) {
			memmove(itest[1], itest[0], sizeof itest[0] * 9);
			snfmt(itest[0], sizeof itest[0], "%s  type %d  code %d  value %d  \xE2\x86\x92 %s", ev->dev, ev->raw_type, ev->raw_code,
			      ev->raw_value, ev->button >= 0 && ev->button < B_COUNT ? BUTTON_NAMES[ev->button] : "(not mapped)");
			if (itest_n < 10) itest_n++;
			if (ev->button >= 0 && ev->button < B_COUNT && ev->pressed) {
				snfmt(itest_last, sizeof itest_last, "%s", BUTTON_NAMES[ev->button]);
				for (char *p = itest_last; *p; p++) *p = (char)toupper((unsigned char)*p);
			}
			dirty = 1;
		}
	}
	int b = ev->button;
	/* Safety net for untested hardware: if buttons are being pressed but none of
	 * them is recognised, hand this session over to the GarlicOS menu. */
	static int unmapped_presses, mapped_presses;
	if (ev->pressed && ev->raw_type == 1) {
		if (b >= 0 && b < B_COUNT) mapped_presses++;
		else if (++unmapped_presses >= 8 && mapped_presses == 0 && top()->type != V_INPUT) {
			LOG("8 button presses but none recognised: handing over to the " FW_NAME " menu (see input.json)");
			g_state = UI_FALLBACK;
			return;
		}
	}
	if (b < 0 || b >= B_COUNT) return;
	if (g_set.swap_ab) { if (b == B_A) b = B_B; else if (b == B_B) b = B_A; }
	uint64_t now = plat_ticks();
	if (ev->pressed) {
		if (held[b]) return;
		held[b] = 1;
		held_at[b] = now;
		static const int DELAY[] = {200, 280, 360, 480, 620};
		rep_at[b] = now + (uint64_t)DELAY[g_set.repeat_delay];
		handle_button(b, 0);
	} else {
		held[b] = 0;
	}
}

static void approach(float *a, float t, float k) {
	if (!g_set.animations) { *a = t; return; }
	float d = t - *a;
	if (fabsf(d) < 0.004f) { *a = t; return; }
	*a += d * k;
	animating = 1;
}
static void anim_cursor(Cursor *c, float k) {
	approach(&c->asel, (float)c->sel, k);
	approach(&c->atop, (float)c->top, k);
}

int ui_update(void) {
	if (net_poll()) dirty = 1; /* a Wi-Fi job finished (Settings > Wi-Fi), box art arrived */
	{
		char m[200];
		if (net_art_finished(m, sizeof m)) toast("%s", m);
	}
#ifdef ZAPPO_KNULLI
	if (plat_display_changed() && g_state == UI_RUN) { g_state = UI_RESTART; return 1; }
	plat_keep_awake(music_is_playing() || net_send_running());
	{
		RaResult rr;
		if (net_ra_take(&rr)) {
			ra_checking = 0;
			if (rr.ok) {
				plat_conf_set("global.retroachievements.username", rr.user[0] ? rr.user : ra_user_typed);
				plat_conf_set("global.retroachievements.password", ra_pass_typed);
				if (rr.token[0]) plat_conf_set("global.retroachievements.token", rr.token);
				plat_conf_set("global.retroachievements", "1");
				ra_score = rr.score;
				toast("Signed in as %s: %d points", rr.user[0] ? rr.user : ra_user_typed, rr.score);
			} else toast("%s", net_last_error());
			memset(ra_pass_typed, 0, sizeof ra_pass_typed);
			dirty = 1;
		}
	}
#endif
	uint64_t now = plat_ticks();
	float dt = last_tick ? (float)(now - last_tick) / 1000.0f : 0.016f;
	if (dt > 0.1f) dt = 0.1f;
	last_tick = now;
	animating = 0;
	/* screensaver, then screen off, after a while with no buttons */
	if (!last_input) last_input = now;
	if (g_state == UI_RUN && !sleeping) {
		uint64_t idle = now - last_input;
		if (!saver && g_set.saver_min > 0 && idle >= (uint64_t)g_set.saver_min * 60000 && top()->type != V_INPUT) {
			lockpic_next();
			saver = 1;
			memset(held, 0, sizeof held);
			dirty = 1;
		}
		if (g_set.off_min > 0 && idle >= (uint64_t)g_set.off_min * 60000) {
			sleep_enter();
			dirty = 1;
		}
	}
	/* moving a video's index, a bit each frame so the screen stays alive */
	if (vprep && !sleeping) {
		last_input = now; /* no screensaver or screen-off meanwhile */
		char err[256];
		static int step_ms;
		if (!step_ms) { const char *e = getenv("CLOVE_VIDEO_STEP_MS"); step_ms = e ? MAX(atoi(e), 1) : 150; }
		int r = video_prep_step(vprep, step_ms, err, sizeof err);
		dirty = 1;
		if (r < 0) { vprep = NULL; toast("%s", err); }
		else if (r > 0) {
			vprep = NULL;
			Entry e = {.name = vprep_name, .path = vprep_path, .sys = vprep_sys};
			launch_now(&e, vprep_kind);
		}
	}
	/* music: redraw when the track changes, and once a second while it plays */
	uint32_t mg = music_generation();
	if (mg != music_gen_seen) { music_gen_seen = mg; dirty = 1; }
	music_update();
	batt_update(now);
	char merr[160];
	if (music_take_error(merr, sizeof merr)) toast("%s", merr);
	if (!sleeping && (saver || (tab == TAB_MUSIC && top()->type == V_HOME)) && music_is_playing() && now / 1000 != last_sec) {
		last_sec = now / 1000;
		dirty = 1;
	}
	if (sleeping) {
		if (lock_peek_until && now >= lock_peek_until) { lock_dark(); dirty = 1; }
		/* the lock screen clock: once a second while music plays, else twice a minute */
		if ((g_set.sleep_clock || lock_peek_until) && (music_is_playing() ? now / 1000 != last_sec : now / 30000 != last_minute)) {
			last_sec = now / 1000;
			last_minute = now / 30000;
			dirty = 1;
		}
		return 0;
	}
	/* key repeat */
	static const int RATE[] = {140, 105, 78, 56, 38};
	for (int b = 0; b < B_COUNT; b++) {
		if (!held[b] || !repeatable(b) || now < rep_at[b]) continue;
		int rate = RATE[g_set.repeat_rate];
		if (g_set.repeat_accel && now - held_at[b] > 1500) rate = rate * 3 / 5;
		rep_at[b] = now + (uint64_t)rate;
		handle_button(b, 1);
	}
	if (top()->type == V_INPUT && held[g_set.swap_ab ? B_A : B_B] && now - held_at[g_set.swap_ab ? B_A : B_B] > 1000) {
		memset(held, 0, sizeof held);
		pop_view();
	}
	float k = 1.0f - expf(-dt * 22.0f);
	anim_cursor(&sys_cur, k);
	anim_cursor(&v_recent.cur, k);
	anim_cursor(&v_fav.cur, k);
	anim_cursor(&v_video.cur, k);
	anim_cursor(&v_settings.cur, k);
	anim_music_cursors(k);
	anim_cursor(&s_cur, k);
	tint_step(k);
	for (int i = 0; i < depth; i++) anim_cursor(&stack[i].cur, k);
	approach(&ov.asel, (float)ov.sel, k);
	View *lv = active_list();
	if (lv && lv->items.n && effective_game_layout() == GL_LIST_ART && plat_ticks() - lv->sel_time < 200) animating = 1;
	if (toast_msg[0] && now <= toast_until + 50) animating = 1;
	if (toast_msg[0] && now > toast_until) { toast_msg[0] = 0; dirty = 1; }
	if (pending_loads || vprep) animating = 1;
	if (now / 30000 != last_minute) { last_minute = now / 30000; dirty = 1; }
	if (settings_dirty && now - settings_changed_at > 1500) save_now();
	if (animating) dirty = 1;
	return animating;
}

void ui_notice(const char *msg) {
	sncpy(toast_msg, msg, sizeof toast_msg);
	toast_until = plat_ticks() + 4500;
	dirty = 1;
}

int ui_dirty(void) { return dirty; }
int ui_state(void) { return g_state; }
int ui_wait_ms(void) {
	if (animating || dirty) return 0;
	if (net_busy()) return 250;
	if (net_send_running()) return 500; /* files arriving (Send to Zappo) */
	for (int b = 0; b < B_COUNT; b++) if (held[b]) return 16;
	if (settings_dirty) return 500;
	return 1000;
}

/* ------------------------------------------------------------------- init */

static void restore_state(void) {
	tab = g_set.start_tab;
	if (!g_set.tab_visible[tab]) tab = TAB_CONSOLES;
	if (!g_set.remember) { enter_tab(tab); return; }
	const char *t = state_get("tab");
	const char *code = state_get("system");
	const char *path = state_get("path");
	const char *dir = state_get("dir");
	if (t && !strcmp(t, "recent") && g_set.tab_visible[TAB_RECENT]) { enter_tab(TAB_RECENT); return; }
	if (t && !strcmp(t, "videos") && g_set.tab_visible[TAB_VIDEOS]) {
		enter_tab(TAB_VIDEOS);
		if (dir && *dir && dir_exists(dir)) push_video_list(dir);
		View *vv = top()->type == V_LIST ? top() : &v_video;
		for (int i = 0; path && i < vv->items.n; i++) if (!strcmp(vv->items.v[i].path, path)) vv->cur.sel = i;
		clamp_cursor(&vv->cur, vv->items.n, video_rows(), video_cols());
		vv->cur.asel = (float)vv->cur.sel; vv->cur.atop = (float)vv->cur.top;
		return;
	}
	if (t && !strcmp(t, "favorites") && g_set.tab_visible[TAB_FAVORITES]) {
		enter_tab(TAB_FAVORITES);
		for (int i = 0; path && i < v_fav.items.n; i++) if (!strcmp(v_fav.items.v[i].path, path)) v_fav.cur.sel = i;
		clamp_cursor(&v_fav.cur, v_fav.items.n, list_rows(), 1);
		v_fav.cur.asel = (float)v_fav.cur.sel; v_fav.cur.atop = (float)v_fav.cur.top;
		return;
	}
	enter_tab(TAB_CONSOLES);
	if (!code) return;
	int s = data_sys_index(code);
	for (int i = 0; i < nvis; i++) if (vis[i] == s) sys_cur.sel = i;
	sys_cur.asel = (float)sys_cur.sel;
	if (t && !strcmp(t, "consoles") && s >= 0 && path) {
		push_list(s, NULL);
		if (dir && *dir && dir_exists(dir)) push_list(s, dir);
		View *v = top();
		for (int i = 0; i < v->items.n; i++) if (!strcmp(v->items.v[i].path, path)) v->cur.sel = i;
		int grid = effective_game_layout() == GL_GRID;
		clamp_cursor(&v->cur, v->items.n, grid ? 2 : list_rows(), grid ? game_cols() : 1);
		v->cur.asel = (float)v->cur.sel;
		v->cur.atop = (float)v->cur.top;
	}
}

void ui_init(void) {
	apply_theme();
	memset(stack, 0, sizeof stack);
	stack[0].type = V_HOME;
	depth = 1;
	refresh_visible();
	restore_state();
	clamp_cursor(&sys_cur, nvis, effective_home_layout() == HL_GRID ? 3 : list_rows(), effective_home_layout() == HL_GRID ? 4 : 1);
	sys_cur.asel = (float)sys_cur.sel;
	sys_cur.atop = (float)sys_cur.top;
	dirty = 1;
}
