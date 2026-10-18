#include "config.h"
#include "gfx.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Settings g_set;
Theme g_theme;
InputMap g_input;

static const char *HL_NAMES[HL_COUNT] = {"carousel", "grid", "list", "shelf"};
static const char *GL_NAMES[GL_COUNT] = {"list_art", "list", "grid", "shelf"};
static const char *STYLE_NAMES[STYLE_COUNT] = {"classic", "glass"};
static const char *ML_NAMES[MLAY_COUNT] = {"list", "grid", "shelf"};
static const char *TAB_KEYS[TAB_COUNT] = {"recent", "favorites", "consoles", "music", "videos", "retroarch", "settings"};

static int jint(cJSON *o, const char *k, int def) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(v)) return v->valueint;
	if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
	return def;
}
static const char *jstr(cJSON *o, const char *k, const char *def) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(v) && v->valuestring ? v->valuestring : def;
}
static void jset_num(cJSON *o, const char *k, int v) {
	cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(it)) cJSON_SetNumberValue(it, v);
	else {
		cJSON_DeleteItemFromObjectCaseSensitive(o, k);
		cJSON_AddNumberToObject(o, k, v);
	}
}
static void jset_str(cJSON *o, const char *k, const char *v) {
	cJSON_DeleteItemFromObjectCaseSensitive(o, k);
	cJSON_AddStringToObject(o, k, v);
}
static int name_index(const char *s, const char *const *names, int n, int def) {
	for (int i = 0; i < n; i++)
		if (s && str_ieq(s, names[i])) return i;
	return def;
}

static void settings_path(char *out, size_t n) { snfmt(out, n, "%s/settings.json", g_paths.clove); }

void settings_load(void) {
	char path[512];
	settings_path(path, sizeof path);
	char *txt = file_read_all(path, NULL);
	cJSON *root = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
	if (!cJSON_IsObject(root)) {
		if (root) LOG("settings.json is not valid JSON; using defaults");
		cJSON_Delete(root);
		root = cJSON_CreateObject();
	}
	if (g_set.root) cJSON_Delete(g_set.root);
	Settings *s = &g_set;
	s->root = root;
	sncpy(s->theme, jstr(root, "theme", "Zappo"), sizeof s->theme);
	s->home_layout = name_index(jstr(root, "home_layout", ""), HL_NAMES, HL_COUNT, -1);
	s->game_layout = name_index(jstr(root, "game_layout", ""), GL_NAMES, GL_COUNT, -1);
	s->show_art = jint(root, "show_art", 1);
	s->clean_names = jint(root, "clean_names", 1);
	s->animations = jint(root, "animations", 1);
	s->clock_mode = CLAMP(jint(root, "clock_mode", 0), 0, 2);
	s->battery_mode = CLAMP(jint(root, "battery_mode", 0), 0, 2);
	s->show_guide = jint(root, "show_guide", 1);
#if defined(ZAPPO_KNULLI) || defined(ZAPPO_TABLET)
	/* Knulli makes a folder for each of its ~180 systems, and the tablet knows
	 * every console RetroArch for Android can play: list only those with games */
	s->empty_consoles = CLAMP(jint(root, "empty_consoles", 1), 0, 1);
#else
	s->empty_consoles = CLAMP(jint(root, "empty_consoles", 0), 0, 1);
#endif
	s->clock_offset = jint(root, "clock_offset", 0);
	s->swap_ab = jint(root, "swap_ab", 0);
	/* sensitivity (0.3: replaces "repeat_speed" 0/1/2 = slow/normal/fast) */
	int rs = CLAMP(jint(root, "repeat_speed", 1), 0, 2);
	s->repeat_delay = CLAMP(jint(root, "repeat_delay", 3 - rs), 0, 4);
	s->repeat_rate = CLAMP(jint(root, "repeat_rate", rs + 1), 0, 4);
	s->repeat_accel = jint(root, "repeat_accel", 1) ? 1 : 0;
	s->saver_min = CLAMP(jint(root, "saver_min", 2), 0, 60);
	s->off_min = CLAMP(jint(root, "off_min", 10), 0, 120);
	s->sleep_clock = jint(root, "sleep_clock", 1) ? 1 : 0;
	sncpy(s->lock_pic, jstr(root, "lock_pic", ""), sizeof s->lock_pic);
	s->music_volume = CLAMP(jint(root, "music_volume", 80), 0, 100);
	s->music_shuffle = jint(root, "music_shuffle", 0) ? 1 : 0;
	s->music_repeat = CLAMP(jint(root, "music_repeat", 1), 0, 2);
	s->music_level = jint(root, "music_level", 1) ? 1 : 0;
	s->music_resume = jint(root, "music_resume", 1) ? 1 : 0;
	s->resume_games = jint(root, "resume_games", 1) ? 1 : 0;
	s->battery_time = jint(root, "battery_time", 1) ? 1 : 0;
	sncpy(s->clock_font, jstr(root, "clock_font", ""), sizeof s->clock_font);
	s->font_weight = CLAMP(jint(root, "font_weight", 2), 0, 2);
	s->music_layout = name_index(jstr(root, "music_layout", "list"), ML_NAMES, MLAY_COUNT, MLAY_LIST);
	s->video_wall = jint(root, "video_wall", 1);
	s->video_sort = CLAMP(jint(root, "video_sort", 0), 0, VS_COUNT - 1);
	s->lights = CLAMP(jint(root, "lights", 0), 0, 3);
	s->lights_colour = CLAMP(jint(root, "lights_colour", 0), 0, 8);
	s->lights_effect = CLAMP(jint(root, "lights_effect", 0), 0, 3);
	s->motion = CLAMP(jint(root, "motion", 2), 0, 2);
#ifdef ZAPPO_TABLET
	s->full_power = CLAMP(jint(root, "full_power", 0), 0, 1);
#endif
	s->style = name_index(jstr(root, "style", "classic"), STYLE_NAMES, STYLE_COUNT, STYLE_CLASSIC);
	/* 0.3.5 brings the Glass style and the Shelf layouts: switched on once
	 * (Settings > Look changes them back) */
	s->new_looks = 0;
	if (jint(root, "settings_version", 1) < 4) {
		s->style = STYLE_GLASS;
		s->home_layout = HL_SHELF;
		s->game_layout = GL_SHELF;
		s->new_looks = 1;
	}
	/* 0.3.5 "Zappo": the Zappo theme and picture, once (the Clove theme is still there) */
	if (jint(root, "settings_version", 1) < 5) {
		if (str_ieq(s->theme, "Clove")) sncpy(s->theme, "Zappo", sizeof s->theme);
		if (!s->lock_pic[0] || !strcmp(s->lock_pic, "01 Sunset Sea.jpg")) sncpy(s->lock_pic, "09 Zappo Friends.jpg", sizeof s->lock_pic);
		s->new_looks = 1;
	}
	s->brightness = CLAMP(jint(root, "brightness", 0), 0, 10);
	s->menu_cpu = jint(root, "menu_cpu", 1008000);
	s->start_tab = CLAMP(jint(root, "start_tab", TAB_CONSOLES), 0, TAB_COUNT - 1);
	/* 0.3 added the Music tab after Consoles: older saved tab numbers move up */
	if (jint(root, "settings_version", 1) < 3 && s->start_tab >= TAB_MUSIC) s->start_tab = CLAMP(s->start_tab + 1, 0, TAB_COUNT - 1);
	/* 0.3.6 added the Videos tab after Music */
	if (jint(root, "settings_version", 1) < 6 && s->start_tab >= TAB_VIDEOS) s->start_tab = CLAMP(s->start_tab + 1, 0, TAB_COUNT - 1);
	s->remember = jint(root, "remember", 1);
	cJSON *tabs = cJSON_GetObjectItemCaseSensitive(root, "tabs");
	for (int i = 0; i < TAB_COUNT; i++) s->tab_visible[i] = jint(tabs, TAB_KEYS[i], 1);
	s->tab_visible[TAB_CONSOLES] = 1;
	if (!cJSON_GetObjectItemCaseSensitive(root, "systems")) cJSON_AddObjectToObject(root, "systems");
	if (!cJSON_GetObjectItemCaseSensitive(root, "state")) cJSON_AddObjectToObject(root, "state");
}

void settings_save(void) {
	Settings *s = &g_set;
	cJSON *r = s->root;
	jset_str(r, "theme", s->theme);
	jset_str(r, "home_layout", s->home_layout < 0 ? "theme" : HL_NAMES[s->home_layout]);
	jset_str(r, "game_layout", s->game_layout < 0 ? "theme" : GL_NAMES[s->game_layout]);
	jset_num(r, "show_art", s->show_art);
	jset_num(r, "clean_names", s->clean_names);
	jset_num(r, "animations", s->animations);
	jset_num(r, "clock_mode", s->clock_mode);
	jset_num(r, "battery_mode", s->battery_mode);
	jset_num(r, "show_guide", s->show_guide);
	cJSON_DeleteItemFromObjectCaseSensitive(r, "hide_empty"); /* replaced by empty_consoles (0.2) */
	jset_num(r, "empty_consoles", s->empty_consoles);
	jset_num(r, "clock_offset", s->clock_offset);
	jset_num(r, "swap_ab", s->swap_ab);
	cJSON_DeleteItemFromObjectCaseSensitive(r, "repeat_speed");
	jset_num(r, "repeat_delay", s->repeat_delay);
	jset_num(r, "repeat_rate", s->repeat_rate);
	jset_num(r, "repeat_accel", s->repeat_accel);
	jset_num(r, "saver_min", s->saver_min);
	jset_num(r, "off_min", s->off_min);
	jset_num(r, "sleep_clock", s->sleep_clock);
	jset_str(r, "lock_pic", s->lock_pic);
	jset_num(r, "music_volume", s->music_volume);
	jset_num(r, "music_shuffle", s->music_shuffle);
	jset_num(r, "music_repeat", s->music_repeat);
	jset_num(r, "music_level", s->music_level);
	jset_num(r, "music_resume", s->music_resume);
	jset_num(r, "resume_games", s->resume_games);
	jset_num(r, "battery_time", s->battery_time);
	jset_str(r, "clock_font", s->clock_font);
	jset_num(r, "font_weight", s->font_weight);
	jset_str(r, "music_layout", ML_NAMES[CLAMP(s->music_layout, 0, MLAY_COUNT - 1)]);
	jset_num(r, "video_wall", s->video_wall);
	jset_num(r, "video_sort", s->video_sort);
	jset_num(r, "lights", s->lights);
	jset_num(r, "lights_colour", s->lights_colour);
	jset_num(r, "lights_effect", s->lights_effect);
	jset_num(r, "motion", s->motion);
#ifdef ZAPPO_TABLET
	jset_num(r, "full_power", s->full_power);
#endif
	jset_str(r, "style", STYLE_NAMES[CLAMP(s->style, 0, STYLE_COUNT - 1)]);
	jset_num(r, "settings_version", 6);
	jset_num(r, "brightness", s->brightness);
	jset_num(r, "menu_cpu", s->menu_cpu);
	jset_num(r, "start_tab", s->start_tab);
	jset_num(r, "remember", s->remember);
	cJSON *tabs = cJSON_GetObjectItemCaseSensitive(r, "tabs");
	if (!tabs) tabs = cJSON_AddObjectToObject(r, "tabs");
	for (int i = 0; i < TAB_COUNT; i++) jset_num(tabs, TAB_KEYS[i], s->tab_visible[i]);
	char *txt = cJSON_Print(r);
	char path[512];
	settings_path(path, sizeof path);
	mkdir_p(g_paths.clove);
	if (file_write_all(path, txt, strlen(txt)) != 0) LOG("could not save %s", path);
	free(txt);
}

cJSON *settings_system(const char *code, int create) {
	cJSON *systems = cJSON_GetObjectItemCaseSensitive(g_set.root, "systems");
	cJSON *o = cJSON_GetObjectItemCaseSensitive(systems, code);
	if (!o && create) o = cJSON_AddObjectToObject(systems, code);
	return o;
}
int sys_get_int(const char *code, const char *key, int fallback) {
	cJSON *o = settings_system(code, 0);
	return o ? jint(o, key, fallback) : fallback;
}
void sys_set_int(const char *code, const char *key, int value) { jset_num(settings_system(code, 1), key, value); }
const char *sys_get_str(const char *code, const char *key) {
	cJSON *o = settings_system(code, 0);
	return o ? jstr(o, key, NULL) : NULL;
}
void sys_set_str(const char *code, const char *key, const char *value) {
	cJSON *o = settings_system(code, 1);
	if (value) jset_str(o, key, value);
	else cJSON_DeleteItemFromObjectCaseSensitive(o, key);
}
void sys_reset(const char *code) {
	cJSON *systems = cJSON_GetObjectItemCaseSensitive(g_set.root, "systems");
	cJSON_DeleteItemFromObjectCaseSensitive(systems, code);
}
const char *state_get(const char *key) {
	return jstr(cJSON_GetObjectItemCaseSensitive(g_set.root, "state"), key, NULL);
}
void state_set(const char *key, const char *value) {
	cJSON *st = cJSON_GetObjectItemCaseSensitive(g_set.root, "state");
	if (value) jset_str(st, key, value);
	else cJSON_DeleteItemFromObjectCaseSensitive(st, key);
}

/* ------------------------------------------------------------------ themes */

static void theme_defaults(Theme *t) {
	memset(t, 0, sizeof *t);
	sncpy(t->name, "Zappo", sizeof t->name);
	t->bg = col_parse("#121426", 0);
	t->bg2 = col_parse("#0a0b16", 0);
	t->surface = col_parse("#1a1d33", 0);
	t->surface_hi = col_parse("#262a47", 0);
	t->text = col_parse("#f6f3e8", 0);
	t->text_dim = col_parse("#a3a9c9", 0);
	t->accent = col_parse("#f5c842", 0);
	t->accent_text = col_parse("#1a1407", 0);
	t->favorite = col_parse("#c3a2f7", 0);
	t->shadow = col_parse("#00000080", 0);
	sncpy(t->font_file, "FiraSansCondensed-Regular.ttf", sizeof t->font_file);
	t->font_title = 28;
	t->font_body = 23;
	t->font_small = 18;
	t->background_dim = 110;
	t->radius = 12;
	t->home_layout = HL_CAROUSEL;
	t->game_layout = GL_LIST_ART;
	t->list_rows = 9;
	sncpy(t->label[TAB_RECENT], "Recent", 32);
	sncpy(t->label[TAB_FAVORITES], "Favorites", 32);
	sncpy(t->label[TAB_CONSOLES], "Consoles", 32);
	sncpy(t->label[TAB_MUSIC], "Music", 32);
	sncpy(t->label[TAB_VIDEOS], "Videos", 32);
	sncpy(t->label[TAB_RETROARCH], FW_MENU_TAB, 32);
	sncpy(t->label[TAB_SETTINGS], "Settings", 32);
}

static void apply_colors(Theme *t, cJSON *c) {
	if (!c) return;
	t->bg = col_parse(jstr(c, "background", NULL), t->bg);
	t->bg2 = col_parse(jstr(c, "background2", NULL), t->bg2);
	t->surface = col_parse(jstr(c, "surface", NULL), t->surface);
	t->surface_hi = col_parse(jstr(c, "surface_highlight", NULL), t->surface_hi);
	t->text = col_parse(jstr(c, "text", NULL), t->text);
	t->text_dim = col_parse(jstr(c, "text_dim", NULL), t->text_dim);
	t->accent = col_parse(jstr(c, "accent", NULL), t->accent);
	t->accent_text = col_parse(jstr(c, "accent_text", NULL), t->accent_text);
	t->favorite = col_parse(jstr(c, "favorite", NULL), t->favorite);
	t->shadow = col_parse(jstr(c, "shadow", NULL), t->shadow);
}

/* GarlicOS skin compatibility: read CFW/skin/settings.json + background.png. */
static void apply_garlic_skin(Theme *t) {
	char p[512];
	snfmt(p, sizeof p, "%s/skin/settings.json", g_paths.cfw);
	char *txt = file_read_all(p, NULL);
	cJSON *s = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
	t->garlic_skin = 1;
	t->bg = t->bg2 = col_parse("#0c0c0c", 0);
	t->surface = col_parse("#161616", 0);
	t->surface_hi = col_parse("#202020", 0);
	t->accent_text = col_parse("#0c0c0c", 0);
	sncpy(t->font_file, "Oswald-Regular.otf", sizeof t->font_file);
	t->font_body = 24;
	t->center_text = 0;
	if (s) {
		t->text = col_parse(jstr(s, "color-active", NULL), col_parse("#ffffff", 0));
		t->text_dim = col_parse(jstr(s, "color-inactive", NULL), col_parse("#6b6b6b", 0));
		t->accent = col_parse(jstr(s, "color-favorite-active", NULL), col_parse("#57bbe9", 0));
		t->favorite = t->accent;
		t->center_text = str_ieq(jstr(s, "text-alignment", "left"), "center");
		sncpy(t->label[TAB_RECENT], jstr(s, "recent-label", t->label[TAB_RECENT]), 32);
		sncpy(t->label[TAB_FAVORITES], jstr(s, "favorites-label", t->label[TAB_FAVORITES]), 32);
		sncpy(t->label[TAB_CONSOLES], jstr(s, "consoles-label", t->label[TAB_CONSOLES]), 32);
		sncpy(t->label[TAB_RETROARCH], jstr(s, "retroarch-label", t->label[TAB_RETROARCH]), 32);
		cJSON_Delete(s);
	}
	snfmt(p, sizeof p, "%s/skin/background.png", g_paths.cfw);
	if (file_exists(p)) sncpy(t->background, p, sizeof t->background);
	t->background_dim = 0;
}

void theme_load(const char *name) {
	Theme *t = &g_theme;
	theme_defaults(t);
	snfmt(t->dir, sizeof t->dir, "%s/themes/%s", g_paths.clove, name);
	char p[640];
	snfmt(p, sizeof p, "%s/theme.json", t->dir);
	char *txt = file_read_all(p, NULL);
	cJSON *j = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
	if (!j) {
		if (strcmp(name, "Zappo") != 0) LOG("theme '%s' not found or invalid; using built-in", name);
		return;
	}
	sncpy(t->name, jstr(j, "name", name), sizeof t->name);
	if (str_ieq(jstr(j, "base", ""), "garlic")) apply_garlic_skin(t);
	apply_colors(t, cJSON_GetObjectItemCaseSensitive(j, "colors"));
	cJSON *f = cJSON_GetObjectItemCaseSensitive(j, "font");
	if (f) {
		sncpy(t->font_file, jstr(f, "file", t->font_file), sizeof t->font_file);
		t->font_title = CLAMP(jint(f, "title", t->font_title), 12, 60);
		t->font_body = CLAMP(jint(f, "body", t->font_body), 10, 48);
		t->font_small = CLAMP(jint(f, "small", t->font_small), 8, 40);
	}
	const char *bg = jstr(j, "background", "");
	if (*bg) snfmt(t->background, sizeof t->background, "%s/%s", t->dir, bg);
	t->background_dim = CLAMP(jint(j, "background_dim", t->background_dim), 0, 255);
	t->background_sharp = t->background[0] && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "background_sharp"));
	t->radius = CLAMP(jint(j, "radius", t->radius), 0, 40);
	t->home_layout = name_index(jstr(j, "home_layout", ""), HL_NAMES, HL_COUNT, t->home_layout);
	t->game_layout = name_index(jstr(j, "game_layout", ""), GL_NAMES, GL_COUNT, t->game_layout);
	t->list_rows = CLAMP(jint(j, "list_rows", t->list_rows), 5, 14);
	t->art_left = str_ieq(jstr(j, "art_side", "right"), "left");
	if (cJSON_GetObjectItemCaseSensitive(j, "text_align")) t->center_text = str_ieq(jstr(j, "text_align", "left"), "center");
	cJSON *labels = cJSON_GetObjectItemCaseSensitive(j, "labels");
	for (int i = 0; i < TAB_COUNT; i++) sncpy(t->label[i], jstr(labels, TAB_KEYS[i], t->label[i]), 32);
	cJSON_Delete(j);
}

int theme_list(char names[][64], int max) {
	char p[512];
	snfmt(p, sizeof p, "%s/themes", g_paths.clove);
	DIR *d = opendir(p);
	int n = 0;
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) && n < max) {
			if (e->d_name[0] == '.') continue;
			char tj[1024];
			snfmt(tj, sizeof tj, "%s/%s/theme.json", p, e->d_name);
			if (file_exists(tj)) sncpy(names[n++], e->d_name, 64);
		}
		closedir(d);
	}
	if (n == 0) sncpy(names[n++], "Zappo", 64);
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && str_icmp_natural(names[j - 1], names[j]) > 0; j--) {
			char tmp[64];
			memcpy(tmp, names[j], 64);
			memcpy(names[j], names[j - 1], 64);
			memcpy(names[j - 1], tmp, 64);
		}
	return n;
}

int effective_home_layout(void) { return g_set.home_layout >= 0 ? g_set.home_layout : g_theme.home_layout; }
int effective_game_layout(void) { return g_set.game_layout >= 0 ? g_set.game_layout : g_theme.game_layout; }

/* --------------------------------------------------------------- controls */

void input_load(void) {
	input_map_defaults(&g_input);
	char p[512];
	snfmt(p, sizeof p, "%s/input.json", g_paths.clove);
	char *txt = file_read_all(p, NULL);
	cJSON *j = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
	if (j) {
		cJSON *joy = cJSON_GetObjectItemCaseSensitive(j, "joystick");
		for (int b = 0; b < B_COUNT; b++) g_input.joy_btn[b] = jint(joy, BUTTON_NAMES[b], g_input.joy_btn[b]);
		g_input.joy_axis_l2 = jint(joy, "l2_axis", g_input.joy_axis_l2);
		g_input.joy_axis_r2 = jint(joy, "r2_axis", g_input.joy_axis_r2);
		cJSON *keys = cJSON_GetObjectItemCaseSensitive(j, "keys");
		for (int b = 0; b < B_COUNT; b++) {
			cJSON *arr = cJSON_GetObjectItemCaseSensitive(keys, BUTTON_NAMES[b]);
			if (!cJSON_IsArray(arr)) continue;
			memset(g_input.key[b], 0, sizeof g_input.key[b]);
			for (int k = 0; k < 3 && k < cJSON_GetArraySize(arr); k++)
				g_input.key[b][k] = cJSON_GetArrayItem(arr, k)->valueint;
		}
		cJSON_Delete(j);
	}
	plat_set_input_map(&g_input);
}
