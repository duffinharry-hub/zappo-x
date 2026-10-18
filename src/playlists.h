/* Clove — playlists and play counts for the Music tab.
 *
 * Playlists are ordinary .m3u files in Music/Playlists (one song per line,
 * relative to that folder), so they can be made or edited on a PC too.
 * Play counts are kept in CFW/clove/play-counts.tsv. */
#ifndef CLOVE_PLAYLISTS_H
#define CLOVE_PLAYLISTS_H

#include <time.h>

#include "util.h"

typedef struct {
	char name[96];
	char file[1024];
	StrList paths; /* full paths of the songs, in order */
} Playlist;

void pl_load(void);                         /* (re)reads Music/Playlists */
int pl_count(void);
Playlist *pl_get(int i);
int pl_find(const char *name);              /* index or -1 (case-insensitive) */
int pl_create(const char *name);            /* index, or -1 */
int pl_add(int i, const char *path);        /* 1 added, 0 already there, -1 error */
int pl_remove(int i, const char *path);     /* 1 removed */
int pl_delete(int i);                       /* removes the .m3u file */
int pl_contains(int i, const char *path);

void pc_load(void);
void pc_bump(const char *path);             /* one more play, now */
int pc_get(const char *path, time_t *last); /* plays (0 when never) */
void pc_forget(const char *path);           /* back to never played */

#endif
