/* Clove — systems, game lists, favourites, recents and launching. */
#ifndef CLOVE_DATA_H
#define CLOVE_DATA_H

#include <stdint.h>
#include <time.h>

#include "util.h"

typedef struct {
	char code[32];      /* ROM folder name, e.g. "PS" */
	char name[64];      /* full display name */
	char short_name[24];
	char core[128];     /* core from GarlicOS coremapping.json */
	int cpu_rec;        /* recommended fixed clock, kHz */
	int count;          /* top-level entries across both cards */
	int on_tf1, on_tf2; /* folder exists on each card */
	int known;
	int is_import;      /* the Import folder, shown as a console while it has things to sort */
	char dir[32];       /* folder name when it isn't the code (Knulli: "gba" under /userdata/roms) */
	char exts[200];     /* game file extensions, " .gba .zip" (Knulli lists them; empty = any) */
	char *emus;         /* Knulli: "libretro/mgba,libretro/gpsp,..." the default first */
} System;

typedef struct {
	char *name;
	char *path;
	int sys;
	uint8_t is_dir;
	uint8_t fav;
} Entry;

typedef struct {
	Entry *v;
	int n, cap;
} EntryList;

extern System *g_sys;
extern int g_nsys;

void data_init(void);
void data_scan(void);
int data_visible_systems(int *out, int max);
int data_sys_index(const char *code);
int data_sys_from_path(const char *path);
const char *data_sys_name(int sys);       /* honours the per-system name style */
const char *data_sys_core(int sys);       /* effective core file name */
int data_sys_cpu(int sys);                /* effective clock: >0 kHz, 0 = GarlicOS automatic */

void el_clear(EntryList *l);
Entry *el_push(EntryList *l);
void data_list_system(int sys, const char *dir, EntryList *out);
void data_list_recents(EntryList *out);
void data_list_favorites(EntryList *out);
void data_display_name(int sys, const char *path, int is_dir, char *out, size_t n);

int data_is_fav(const char *path);
int data_fav_toggle(const Entry *e); /* returns new state */
void data_recent_remove(const char *path);

int data_art_path(const Entry *e, char *out, size_t n);
int data_icon_path(int sys, char *out, size_t n);
int core_is_script(const char *core);   /* a .sh set as a console's emulator (a stand-alone port's launcher) */

int data_cores_for_system(int sys, StrList *out);
const char *data_core_label(const char *core_file); /* display name from core_info */

/* Launching writes CFW/retroarch/command.sh for GarlicOS's start loop to run. */
int data_launch(const Entry *e, char *err, size_t errn);
/* Continue where you left off: whether a game has a saved resume point
 * (and its picture, when RetroArch saved one). Set data_start_over before
 * data_launch to start the next game from the beginning instead. */
int data_resume_point(const Entry *e, char *thumb, size_t n, time_t *when);
extern int data_start_over;
/* Videos: up to two films to play after the launched one when it plays to
 * the end (the next episodes); cleared after each launch. */
extern char data_next_video[2][1024];
int data_launch_retroarch(void);

/* Where a console's games and the BIOS files go on the main card (Import uses these) */
void data_sys_dir(int sys, char *out, size_t n);
void data_bios_dir(char *out, size_t n);

/* Arcade set names from GarlicOS's CFW/config/mame.csv */
int data_is_arcade_set(const char *stem);

/* BIOS files a console needs. Returns 0 = nothing needed / all present,
 * 1 = an optional BIOS is missing, 2 = a required BIOS is missing.
 * `files` gets the file names to show ("disksys.rom"). */
enum { BIOS_OK, BIOS_OPTIONAL_MISSING, BIOS_REQUIRED_MISSING };
int data_bios_status(int sys, char *files, size_t n);

#endif
