/* Clove — the Import folder: drop games (or BIOS files) into <card>/Import
 * and Clove moves each one into the right Roms/<console> folder. */
#ifndef CLOVE_IMPORT_H
#define CLOVE_IMPORT_H

#include <stddef.h>

#define IMPORT_TO_BIOS (-2)
#define IMPORT_TO_MUSIC (-3)

typedef struct {
	int moved;    /* games moved into a console folder */
	int bios;     /* files moved into BIOS */
	int music;    /* songs / albums moved into Music */
	int left;     /* items still waiting in Import (console unknown) */
	int failed;   /* moves the file system refused */
	char first_sys[64]; /* console of the first moved game, for the message */
} ImportResult;

/* The Import folder on the card GarlicOS runs from (created if missing). */
void import_dir(char *out, size_t n);
/* Sorts every Import folder. Cheap when they're empty. */
void import_run(ImportResult *r);
/* Best guess for one item in Import: a system index, IMPORT_TO_BIOS,
 * IMPORT_TO_MUSIC, or -1. */
int import_guess(const char *path, int is_dir);
/* Notes, pictures etc. that stay in Import without needing a console. */
int import_ignored(const char *path);
/* Moves one Import item (with its .cue/.m3u companions) to a console or BIOS. */
int import_move(const char *path, int target, char *err, size_t errn);

/* One line describing what import_run did ("" when nothing happened). */
void import_summary(const ImportResult *r, int manual, char *out, size_t n);

#endif
