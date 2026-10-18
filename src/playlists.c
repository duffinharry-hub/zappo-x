/* Clove — playlists (.m3u files in Music/Playlists) and play counts. */
#include "playlists.h"
#include "config.h"
#include "music.h"
#include "musiclib.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static Playlist *pls;
static int npls, cpls;

static void pl_dir(char *out, size_t n) {
	char m[512];
	music_dir(m, sizeof m);
	snfmt(out, n, "%s/Playlists", m);
}

static void pl_clear(void) {
	for (int i = 0; i < npls; i++) sl_free(&pls[i].paths);
	npls = 0;
}

/* a line from an .m3u: absolute, or relative to the playlist's folder
 * (PC players write "..\Artist\Song.mp3") */
static void resolve(const char *line, char *out, size_t n) {
	char t[1024];
	sncpy(t, line, sizeof t);
	for (char *p = t; *p; p++) if (*p == '\\') *p = '/';
	if (t[0] == '/') { sncpy(out, t, n); return; }
	char base[1024];
	pl_dir(base, sizeof base);
	const char *r = t;
	while (!strncmp(r, "./", 2)) r += 2;
	while (!strncmp(r, "../", 3)) {
		r += 3;
		char *s = strrchr(base, '/');
		if (s && s != base) *s = 0;
	}
	snfmt(out, n, "%s/%s", base, r);
}

static int pl_read(Playlist *p) {
	sl_free(&p->paths);
	char *txt = file_read_all(p->file, NULL);
	if (!txt) return -1;
	char *s = txt;
	if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
	for (char *line = strtok(s, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		str_trim(line);
		if (!line[0] || line[0] == '#') continue;
		char full[1024];
		resolve(line, full, sizeof full);
		if (sl_find(&p->paths, full) < 0) sl_push(&p->paths, full);
	}
	free(txt);
	return 0;
}

static int pl_write(Playlist *p) {
	char m[512];
	music_dir(m, sizeof m);
	size_t ml = strlen(m), cap = 32;
	for (int i = 0; i < p->paths.n; i++) cap += strlen(p->paths.v[i]) + 8;
	char *b = xmalloc(cap);
	size_t o = (size_t)snfmt(b, cap, "#EXTM3U\n");
	for (int i = 0; i < p->paths.n; i++) {
		const char *q = p->paths.v[i];
		/* songs in the Music folder are written relative ("../Artist/Song.mp3"),
		 * so the playlist also works on a PC */
		if (!strncmp(q, m, ml) && q[ml] == '/') o += (size_t)snfmt(b + o, cap - o, "..%s\n", q + ml);
		else o += (size_t)snfmt(b + o, cap - o, "%s\n", q);
	}
	char d[1024];
	pl_dir(d, sizeof d);
	mkdir_p(d);
	int r = file_write_all(p->file, b, o);
	free(b);
	return r;
}

static int cmp_pl(const void *a, const void *b) { return str_icmp_natural(((const Playlist *)a)->name, ((const Playlist *)b)->name); }

void pl_load(void) {
	pl_clear();
	char d[1024];
	pl_dir(d, sizeof d);
	DIR *dd = opendir(d);
	struct dirent *e;
	while (dd && (e = readdir(dd))) {
		if (e->d_name[0] == '.') continue;
		const char *x = path_ext(e->d_name);
		if (!str_ieq(x, "m3u") && !str_ieq(x, "m3u8")) continue;
		if (npls == cpls) {
			cpls = cpls ? cpls * 2 : 16;
			pls = xrealloc(pls, sizeof *pls * (size_t)cpls);
		}
		Playlist *p = &pls[npls];
		memset(p, 0, sizeof *p);
		path_stem(e->d_name, p->name, sizeof p->name);
		snfmt(p->file, sizeof p->file, "%s/%s", d, e->d_name);
		if (pl_read(p) == 0) npls++;
	}
	if (dd) closedir(dd);
	if (npls > 1) qsort(pls, (size_t)npls, sizeof *pls, cmp_pl);
}

int pl_count(void) { return npls; }
Playlist *pl_get(int i) { return i >= 0 && i < npls ? &pls[i] : NULL; }

int pl_find(const char *name) {
	for (int i = 0; i < npls; i++) if (str_ieq(pls[i].name, name)) return i;
	return -1;
}

int pl_create(const char *name) {
	char safe[96];
	lib_safe_name(name, safe, sizeof safe);
	str_trim(safe);
	if (!safe[0]) return -1;
	int have = pl_find(safe);
	if (have >= 0) return have;
	char d[1024], f[1100];
	pl_dir(d, sizeof d);
	snfmt(f, sizeof f, "%s/%s.m3u", d, safe);
	Playlist tmp;
	memset(&tmp, 0, sizeof tmp);
	sncpy(tmp.name, safe, sizeof tmp.name);
	sncpy(tmp.file, f, sizeof tmp.file);
	if (pl_write(&tmp) != 0) return -1;
	pl_load();
	return pl_find(safe);
}

int pl_contains(int i, const char *path) {
	Playlist *p = pl_get(i);
	return p && sl_find(&p->paths, path) >= 0;
}

int pl_add(int i, const char *path) {
	Playlist *p = pl_get(i);
	if (!p) return -1;
	if (sl_find(&p->paths, path) >= 0) return 0;
	sl_push(&p->paths, path);
	return pl_write(p) == 0 ? 1 : -1;
}

int pl_remove(int i, const char *path) {
	Playlist *p = pl_get(i);
	if (!p) return 0;
	int k = sl_find(&p->paths, path);
	if (k < 0) return 0;
	free(p->paths.v[k]);
	memmove(&p->paths.v[k], &p->paths.v[k + 1], sizeof(char *) * (size_t)(p->paths.n - k - 1));
	p->paths.n--;
	pl_write(p);
	return 1;
}

/* A deleted playlist is moved to CFW/clove/deleted-playlists rather than
 * erased, so a slip of the thumb can be undone on a PC. */
int pl_delete(int i) {
	Playlist *p = pl_get(i);
	if (!p) return -1;
	char d[600], to[1200];
	snfmt(d, sizeof d, "%s/deleted-playlists", g_paths.clove);
	mkdir_p(d);
	snfmt(to, sizeof to, "%s/%s", d, path_base(p->file));
	for (int k = 2; file_exists(to) && k < 100; k++) snfmt(to, sizeof to, "%s/%s (%d).m3u", d, p->name, k);
	if (rename(p->file, to) != 0) return -1;
	pl_load();
	return 0;
}

/* ------------------------------------------------------------ play counts */

typedef struct { char *path; int n; long last; } Count;
static Count *counts;
static int ncounts, ccounts, counts_loaded;

static void pc_file(char *out, size_t n) { snfmt(out, n, "%s/play-counts.tsv", g_paths.clove); }

void pc_load(void) {
	for (int i = 0; i < ncounts; i++) free(counts[i].path);
	ncounts = 0;
	counts_loaded = 1;
	char p[600];
	pc_file(p, sizeof p);
	char *txt = file_read_all(p, NULL);
	if (!txt) return;
	for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
		char *t1 = strchr(line, '\t');
		if (!t1) continue;
		char *t2 = strchr(t1 + 1, '\t');
		if (!t2) continue;
		*t1 = *t2 = 0;
		if (ncounts == ccounts) { ccounts = ccounts ? ccounts * 2 : 128; counts = xrealloc(counts, sizeof *counts * (size_t)ccounts); }
		counts[ncounts].n = atoi(line);
		counts[ncounts].last = atol(t1 + 1);
		counts[ncounts].path = xstrdup(t2 + 1);
		ncounts++;
	}
	free(txt);
}

static void pc_save(void) {
	size_t cap = 64;
	for (int i = 0; i < ncounts; i++) cap += strlen(counts[i].path) + 32;
	char *b = xmalloc(cap);
	size_t o = 0;
	for (int i = 0; i < ncounts; i++) o += (size_t)snfmt(b + o, cap - o, "%d\t%ld\t%s\n", counts[i].n, counts[i].last, counts[i].path);
	char p[600];
	pc_file(p, sizeof p);
	file_write_all(p, b, o);
	free(b);
}

void pc_bump(const char *path) {
	if (!counts_loaded) pc_load();
	long now = (long)time(NULL);
	for (int i = 0; i < ncounts; i++)
		if (!strcmp(counts[i].path, path)) { counts[i].n++; counts[i].last = now; pc_save(); return; }
	if (ncounts == ccounts) { ccounts = ccounts ? ccounts * 2 : 128; counts = xrealloc(counts, sizeof *counts * (size_t)ccounts); }
	counts[ncounts].path = xstrdup(path);
	counts[ncounts].n = 1;
	counts[ncounts].last = now;
	ncounts++;
	pc_save();
}

int pc_get(const char *path, time_t *last) {
	if (!counts_loaded) pc_load();
	for (int i = 0; i < ncounts; i++)
		if (!strcmp(counts[i].path, path)) { if (last) *last = (time_t)counts[i].last; return counts[i].n; }
	if (last) *last = 0;
	return 0;
}

void pc_forget(const char *path) {
	if (!counts_loaded) pc_load();
	for (int i = 0; i < ncounts; i++)
		if (!strcmp(counts[i].path, path)) {
			free(counts[i].path);
			counts[i] = counts[--ncounts];
			pc_save();
			return;
		}
}
