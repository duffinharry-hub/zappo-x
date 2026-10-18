/* Clove — the music library behind the Music tab: every song in the Music
 * folder with its title, artist, album, genre and length, grouped into
 * artists and genres.
 *
 * Names come from the file's tags; files without tags (typical of
 * "video to MP3" downloads) get them from a tidied file name. Genres come
 * from Music/genres.txt ("Artist = Genre", editable on a PC or on the
 * handheld), else the file's genre tag, else "Unsorted". */
#ifndef CLOVE_MUSICLIB_H
#define CLOVE_MUSICLIB_H

#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

typedef struct {
	char *path, *title, *artist, *album;
	char *tag_genre;  /* from the file ("" when none) */
	const char *genre; /* resolved: the artist's genre */
	int dur;          /* seconds, 0 = unknown */
	int has_art;      /* the file carries a picture */
	int tagged;       /* the title came from the file's tags */
	int kind;         /* MUSIC_CLOVE / MUSIC_RETROARCH */
	int artist_i;     /* index into g_lib.a */
} Track;

typedef struct {
	char *name;
	char *genre;
	int n, *t;   /* tracks, sorted by album then title */
	int dur;     /* total seconds */
	int albums;
} MArtist;

typedef struct {
	char *name;
	int n, *a;   /* artists in this genre */
	int ntracks, dur;
} MGenre;

typedef struct {
	Track *t;
	int nt;
	MArtist *a;
	int na;
	MGenre *g;
	int ng;
	int *by_title; /* track indexes sorted by title */
	uint32_t gen;  /* changes on every rebuild */
	int scanned;
} MusicLib;

extern MusicLib g_lib;

#define LIB_UNSORTED "Unsorted"

/* Reads the Music folder (and CFW/clove/music-cache.tsv so unchanged files
 * aren't re-read). Returns the number of songs. */
int lib_scan(void);
int lib_find(const char *path);                         /* track index or -1 */
void lib_set_genre(const char *artist, const char *genre); /* saves genres.txt, regroups */
const char *const *lib_genre_presets(int *n);

/* A picture for a song: "<song>.jpg/png" next to it, Music/Covers/<song>.jpg,
 * inside the file, the folder's cover.jpg, or Music/Artists/<Artist>.jpg.
 * NULL when there's none. */
Image *lib_art_for_track(int ti);
Image *lib_art_for_artist(int ai);

/* Music/Artists and Music/Covers hold pictures, not songs */
int lib_is_picture_dir(const char *name);
/* The artist's picture file name, safe for Windows (see Get cover art.bat) */
void lib_safe_name(const char *in, char *out, size_t n);

/* Title / artist from a file name, e.g.
 * "Potter Payper - Inaugural  PotterPayperTV - Potter Payper" ->
 * "Inaugural" by "Potter Payper". Exposed for the tests. */
void lib_tidy_name(const char *stem, char *title, size_t tn, char *artist, size_t an);
void lib_tidy_title(char *title, const char *artist);

#endif
