/* Clove — user settings (settings.json) and themes (themes/<name>/theme.json). */
#ifndef CLOVE_CONFIG_H
#define CLOVE_CONFIG_H

#include <stdint.h>

#include "../third_party/cJSON.h"
#include "plat.h"

enum { HL_CAROUSEL, HL_GRID, HL_LIST, HL_SHELF, HL_COUNT };
enum { GL_LIST_ART, GL_LIST, GL_GRID, GL_SHELF, GL_COUNT };
enum { STYLE_CLASSIC, STYLE_GLASS, STYLE_COUNT };
enum { MLAY_LIST, MLAY_GRID, MLAY_SHELF, MLAY_COUNT };
enum { VS_NAME, VS_ADDED, VS_WATCHED, VS_COUNT };
enum { TAB_RECENT, TAB_FAVORITES, TAB_CONSOLES, TAB_MUSIC, TAB_VIDEOS, TAB_RETROARCH, TAB_SETTINGS, TAB_COUNT };

typedef struct {
	char name[64];
	char dir[512];
	uint32_t bg, bg2, surface, surface_hi, text, text_dim, accent, accent_text, favorite, shadow;
	char font_file[128];
	int font_title, font_body, font_small;
	char background[512]; /* absolute path, or empty */
	int background_dim;   /* 0..255 darkening over the background image */
	int background_sharp; /* the theme's picture, sharp, behind every screen in Glass too (not the blurred lock picture) */
	int radius;
	int home_layout, game_layout;
	int list_rows;
	int art_left;
	int center_text;
	int garlic_skin;
	char label[TAB_COUNT][32];
} Theme;

typedef struct {
	cJSON *root;
	char theme[64];
	int home_layout; /* -1 = theme default */
	int game_layout; /* -1 = theme default */
	int show_art;
	int clean_names;
	int animations;
	int clock_mode;   /* 0 24h, 1 12h, 2 hidden */
	int battery_mode; /* 0 icon + %, 1 icon, 2 hidden */
	int show_guide;
	int empty_consoles; /* 0 listed last, 1 hidden */
	int clock_offset;   /* seconds added to the device clock for display */
	int swap_ab;
	int repeat_delay; /* sensitivity: 0..4, how long a held button waits before repeating */
	int repeat_rate;  /* sensitivity: 0..4, how fast it repeats */
	int repeat_accel; /* repeat faster after a long hold */
	int saver_min;    /* screensaver after N minutes idle, 0 = never */
	int off_min;      /* screen off after N minutes idle, 0 = never */
	int sleep_clock;  /* asleep: 1 = the clock and wallpaper, dimmed; 0 = screen off */
	char lock_pic[128]; /* lock screen / screensaver picture in CFW/clove/lockscreens: "" = the theme's, "*" = shuffle */
	int music_volume, music_shuffle, music_repeat;
	int music_level;  /* even out loudness between songs */
	int music_resume; /* pick the last song up where it was (paused) when Clove starts */
	int resume_games; /* games continue where you left off (RetroArch's auto save state) */
	int battery_time; /* show the estimated time left next to the battery */
	int font_weight; /* 0 regular, 1 firm, 2 bold (Settings > Look > Text) */
	char clock_font[64]; /* a file in CFW/clove/fonts for the lock screen clock; "" = the theme font */
	int music_layout; /* MLAY_LIST / MLAY_GRID / MLAY_SHELF: how the Music tab's lists look */
	int video_wall;   /* 1: the Video tab shows a wall of pictures, 0: a list */
	int video_sort;   /* VS_NAME / VS_ADDED / VS_WATCHED */
	int style;        /* STYLE_CLASSIC, or STYLE_GLASS: the wallpaper behind every screen, see-through panels */
	int new_looks;    /* (not saved) this start switched on Glass and Shelf for the first time */
	int brightness;
	int menu_cpu;     /* kHz cap while browsing */
	int tab_visible[TAB_COUNT];
	int start_tab;
	int remember;     /* return to the last game/screen */
	int lights;        /* the stick lights (Knulli): LIGHTS_FOLLOW, LIGHTS_ONE, LIGHTS_KNULLI, LIGHTS_OFF */
	int lights_colour; /* LIGHTS_ONE: which of the Zappo colours */
	int lights_effect; /* 0 steady, 1 slow breathing, 2 breathing, 3 fast breathing */
	int motion;        /* moving backgrounds (Knulli): 2 on, 1 fewer, 0 off */
	int full_power;    /* tablet: 60 frames a second, drawn by its graphics chip (0 = balanced) */
} Settings;

extern Settings g_set;
extern Theme g_theme;
extern InputMap g_input;

void settings_load(void);
void settings_save(void);
cJSON *settings_system(const char *code, int create); /* per-system overrides */
int sys_get_int(const char *code, const char *key, int fallback);
void sys_set_int(const char *code, const char *key, int value);
const char *sys_get_str(const char *code, const char *key);
void sys_set_str(const char *code, const char *key, const char *value);
void sys_reset(const char *code);
const char *state_get(const char *key);
void state_set(const char *key, const char *value);

int theme_list(char names[][64], int max);
void theme_load(const char *name);
int effective_home_layout(void);
int effective_game_layout(void);

void input_load(void); /* clove/input.json overrides */

#endif
