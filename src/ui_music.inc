/* Clove — the Music tab (included from ui.c, shares its helpers).
 *
 * Four ways in, switched with SELECT: Artists, Genres, Songs, Folders.
 * Artists open to their songs with a breakdown (genre, songs, albums,
 * total time); genres open to their artists. START opens Now Playing, a
 * full-screen view with the cover art. Songs without a picture get a cover
 * drawn in the artist's own colours. */

enum { MV_ARTISTS, MV_GENRES, MV_SONGS, MV_PLAYLISTS, MV_FOLDERS, MV_COUNT };
static const char *MV_NAME[] = {"Artists", "Genres", "Songs", "Playlists", "Folders"};
enum { ML_ROOT, ML_ARTIST, ML_GENRE, ML_FOLDER, ML_PLAYLIST };
enum { R_ARTIST, R_GENRE, R_TRACK, R_DIR, R_PLAYALL, R_SHUFFLE, R_PLAYLIST, R_NEWPL };
/* the two playlists Clove keeps itself (R_PLAYLIST / ML_PLAYLIST idx) */
#define PL_RECENT (-1)
#define PL_MOST (-2)
#define PL_LIST_MAX 50

typedef struct {
	int kind, idx;
	char *path; /* R_DIR */
	int n, dur; /* R_PLAYLIST: songs and seconds */
} MRow;
typedef struct {
	int level, idx;
	Cursor cur;
	char dir[1024];
} MLevel;

static int mview = MV_ARTISTS;
static MLevel mst[8];
static int mdep = 1;
static MRow *mrows;
static int nmrows, cmrows;
static uint32_t mrows_gen = 0xFFFFFFFFu;

static void anim_cursor(Cursor *c, float k);

#define MLW 330
static int mrow_h(void) { return CONTENT_H / list_rows(); }
/* Grid: 4 across, 2 down; Shelf: one row; List: rows under the view chips */
#define MG_COLS 4
static int mlayout(void) { return g_set.music_layout; }
static int mcols(void) { return mlayout() == MLAY_GRID ? MG_COLS : 1; }
static int mrows_vis(void) { return mlayout() == MLAY_GRID ? 2 : mlayout() == MLAY_SHELF ? 1 : MAX(3, list_rows() - 1); }

static void music_lib_invalidate(void) { lib_dirty = 1; }

/* ---------------------------------------------------------------- colours */

static uint32_t hsv(float h, float s, float v) {
	h = fmodf(h, 1.0f) * 6.0f;
	int i = (int)h;
	float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
	float r, g, b;
	switch (i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	return RGBA((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

static float name_hue(const char *s) {
	uint32_t h = 2166136261u;
	for (; *s; s++) h = (h ^ (uint8_t)tolower((unsigned char)*s)) * 16777619u;
	return (h % 360) / 360.0f;
}

/* "Potter Payper" -> "PP", "The Streets" -> "S" */
static void initials(const char *name, char *out, size_t n) {
	size_t o = 0;
	int words = 0;
	const char *p = name;
	if (!strncasecmp(p, "the ", 4) && p[4]) p += 4;
	while (*p && words < 2 && o + 5 < n) {
		while (*p == ' ' || *p == '-' || *p == '&' || *p == '(') p++;
		if (!*p) break;
		unsigned char c = (unsigned char)*p;
		int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
		if (len == 1) out[o++] = (char)toupper(c);
		else { memcpy(out + o, p, (size_t)len); o += (size_t)len; }
		words++;
		while (*p && *p != ' ') p++;
	}
	out[o] = 0;
}

/* --------------------------------------------------------------- pictures */

typedef struct {
	char key[320];
	Image *im; /* NULL = no picture */
	uint64_t used;
	int set;
} MArt;
static MArt mart[40];

static MArt *mart_slot(const char *key) {
	for (int i = 0; i < ARRAY_LEN(mart); i++)
		if (mart[i].set && !strcmp(mart[i].key, key)) { mart[i].used = plat_ticks(); return &mart[i]; }
	return NULL;
}

static MArt *mart_new(const char *key) {
	int slot = 0;
	for (int i = 0; i < ARRAY_LEN(mart); i++) {
		if (!mart[i].set) { slot = i; break; }
		if (mart[i].used < mart[slot].used) slot = i;
	}
	img_free(mart[slot].im);
	memset(&mart[slot], 0, sizeof mart[slot]);
	sncpy(mart[slot].key, key, sizeof mart[slot].key);
	mart[slot].used = plat_ticks();
	mart[slot].set = 1;
	return &mart[slot];
}

static void mart_clear(void) {
	for (int i = 0; i < ARRAY_LEN(mart); i++) img_free(mart[i].im);
	memset(mart, 0, sizeof mart);
}

/* The real picture for a song (artist = 0) or an artist (artist = 1), cropped
 * square. *known says whether we've looked yet (pictures load one per frame). */
static Image *music_art(int artist, int idx, int size, int *known) {
	*known = 0;
	if (idx < 0 || (artist ? idx >= g_lib.na : idx >= g_lib.nt)) { *known = 1; return NULL; }
	char key[320];
	snfmt(key, sizeof key, "%c%d|%s", artist ? 'a' : 't', size, artist ? g_lib.a[idx].name : g_lib.t[idx].path);
	MArt *m = mart_slot(key);
	if (m) { *known = 1; return m->im; }
	if (decode_budget <= 0) { pending_loads = 1; return NULL; }
	decode_budget--;
	Image *src = artist ? lib_art_for_artist(idx) : lib_art_for_track(idx);
	m = mart_new(key);
	if (src) { m->im = img_cover(src, size, size); img_free(src); }
	*known = 1;
	return m->im;
}

/* A cover for something without a picture: a gradient in the name's colours
 * with soft record grooves and the initials. */
static Image *gen_cover_img(const char *name, int size) {
	char key[320];
	snfmt(key, sizeof key, "g%d|%s", size, name);
	MArt *m = mart_slot(key);
	if (m) return m->im;
	float h = name_hue(name);
	uint32_t c1 = hsv(h, 0.55f, 0.88f), c2 = hsv(h + 0.11f, 0.75f, 0.42f);
	Image *im = img_new(size, size);
	float cx = size * 0.78f, cy = size * 0.22f;
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++) {
			float t = (x * 0.35f + y * 0.65f) / size;
			uint32_t c = col_mix(c1, c2, CLAMP(t, 0, 1));
			float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) / size;
			float groove = 0.035f * sinf(d * 95.0f) * (d < 0.95f ? 1.0f : 0.0f);
			float glow = 0.22f * expf(-d * d * 7.0f);
			int r = (int)(((c >> 16) & 255) * (1 + groove) + 255 * glow), g = (int)(((c >> 8) & 255) * (1 + groove) + 255 * glow),
			    b = (int)((c & 255) * (1 + groove) + 255 * glow);
			im->px[y * size + x] = RGBA(CLAMP(r, 0, 255), CLAMP(g, 0, 255), CLAMP(b, 0, 255), 255);
		}
	m = mart_new(key);
	m->im = im;
	return im;
}

static void draw_gen_cover(int x, int y, int size, const char *name, const char *label, int radius) {
	Image *im = gen_cover_img(name, size);
	gfx_blit_rounded(im, x, y, radius, 255);
	char ini[16];
	if (label) sncpy(ini, label, sizeof ini);
	else initials(name, ini, sizeof ini);
	if (!ini[0]) return;
	Font *f = font_get(MAX(10, size * 2 / 5));
	if (!f) f = F_TITLE;
	int tw = text_width(f, ini), th = font_height(f);
	text_draw(f, x + (size - tw) / 2 + 1, y + (size - th) / 2 + 2, ini, col_alpha(0x000000, 70));
	text_draw(f, x + (size - tw) / 2, y + (size - th) / 2, ini, 0xF0FFFFFF);
}

/* the cover of a song: its picture, else one drawn for its artist */
static void draw_track_cover(int ti, int x, int y, int size, int radius) {
	int known;
	Image *im = music_art(0, ti, size, &known);
	if (im) { gfx_blit_rounded(im, x, y, radius, 255); return; }
	const char *who = ti >= 0 && ti < g_lib.nt ? g_lib.t[ti].artist : "?";
	draw_gen_cover(x, y, size, who, NULL, radius);
}

static void draw_artist_cover(int ai, int x, int y, int size, int radius) {
	int known;
	Image *im = music_art(1, ai, size, &known);
	if (im) { gfx_blit_rounded(im, x, y, radius, 255); return; }
	draw_gen_cover(x, y, size, ai >= 0 && ai < g_lib.na ? g_lib.a[ai].name : "?", NULL, radius);
}

static void draw_genre_tile(int gi, int x, int y, int size, int radius) {
	const char *name = gi >= 0 && gi < g_lib.ng ? g_lib.g[gi].name : "?";
	Image *im = gen_cover_img(name, size);
	gfx_blit_rounded(im, x, y, radius, 255);
	if (size >= 80) {
		/* the genre's name on the tile, like a playlist cover */
		text_draw_wrap(F_BODY, x + 12, y + size - font_height(F_BODY) * 2 - 12, size - 24, 2, name, 0xF8FFFFFF, 0);
	} else {
		char ini[16];
		initials(name, ini, sizeof ini);
		Font *f = font_get(MAX(10, size * 2 / 5));
		if (!f) f = F_SMALL;
		int tw = text_width(f, ini), th = font_height(f);
		text_draw(f, x + (size - tw) / 2, y + (size - th) / 2, ini, 0xF0FFFFFF);
	}
}

/* ------------------------------------------------------------------- rows */

static void mrow_add(int kind, int idx, const char *path) {
	if (nmrows == cmrows) { cmrows = cmrows ? cmrows * 2 : 64; mrows = xrealloc(mrows, sizeof *mrows * (size_t)cmrows); }
	mrows[nmrows].kind = kind;
	mrows[nmrows].idx = idx;
	mrows[nmrows].path = path ? xstrdup(path) : NULL;
	mrows[nmrows].n = mrows[nmrows].dur = 0;
	nmrows++;
}

static int cmp_str_nat(const void *a, const void *b) { return str_icmp_natural(*(char *const *)a, *(char *const *)b); }

/* ------------------------------------------------------------- playlists */

static const char *pl_title(int idx) {
	if (idx == PL_RECENT) return "Recently added";
	if (idx == PL_MOST) return "Most played";
	Playlist *p = pl_get(idx);
	return p ? p->name : "Playlist";
}

typedef struct { int t; long key, key2; } TKey;
static int cmp_tkey(const void *a, const void *b) {
	const TKey *x = a, *y = b;
	if (x->key != y->key) return x->key > y->key ? -1 : 1;
	if (x->key2 != y->key2) return x->key2 > y->key2 ? -1 : 1;
	return str_icmp_natural(g_lib.t[x->t].title, g_lib.t[y->t].title);
}

/* the songs in a playlist, as library track indexes (songs no longer in
 * the Music folder are left out) */
static int pl_tracks(int idx, int **out) {
	int n = 0;
	int *v = xmalloc(sizeof(int) * (size_t)MAX(g_lib.nt, 1));
	if (idx == PL_RECENT || idx == PL_MOST) {
		TKey *k = xmalloc(sizeof(TKey) * (size_t)MAX(g_lib.nt, 1));
		int nk = 0;
		for (int i = 0; i < g_lib.nt; i++) {
			if (idx == PL_RECENT) {
				/* when the file arrived on the card */
				struct stat st;
				if (stat(g_lib.t[i].path, &st) != 0) continue;
				k[nk++] = (TKey){i, (long)st.st_mtime, 0};
			} else {
				time_t last;
				int c = pc_get(g_lib.t[i].path, &last);
				if (c > 0) k[nk++] = (TKey){i, c, (long)last};
			}
		}
		if ((nk) > 1) qsort(k, (size_t)nk, sizeof *k, cmp_tkey);
		for (int i = 0; i < nk && i < PL_LIST_MAX; i++) v[n++] = k[i].t;
		free(k);
	} else {
		Playlist *p = pl_get(idx);
		for (int i = 0; p && i < p->paths.n && n < g_lib.nt; i++) {
			int t = lib_find(p->paths.v[i]);
			if (t >= 0) v[n++] = t;
		}
	}
	*out = v;
	return n;
}

static MLevel *mlev(void) { return &mst[mdep - 1]; }

static int folder_level(void) { return mlev()->level == ML_FOLDER || (mlev()->level == ML_ROOT && mview == MV_FOLDERS); }

static void mrows_build(void) {
	for (int i = 0; i < nmrows; i++) free(mrows[i].path);
	nmrows = 0;
	MLevel *L = mlev();
	if (folder_level()) {
		char root[512];
		music_dir(root, sizeof root);
		if (L->level == ML_ROOT) sncpy(L->dir, root, sizeof L->dir);
		StrList dirs = {0};
		DIR *d = opendir(L->dir);
		struct dirent *de;
		while (d && (de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			if (L->level == ML_ROOT && lib_is_picture_dir(de->d_name)) continue;
			char p[1024];
			snfmt(p, sizeof p, "%s/%s", L->dir, de->d_name);
			if (dir_exists(p)) sl_push(&dirs, p);
		}
		if (d) closedir(d);
		if ((dirs.n) > 1) qsort(dirs.v, (size_t)dirs.n, sizeof(char *), cmp_str_nat);
		for (int i = 0; i < dirs.n; i++) mrow_add(R_DIR, i, dirs.v[i]);
		sl_free(&dirs);
		int start = nmrows;
		for (int i = 0; i < g_lib.nt; i++) {
			char dir[1024];
			path_dir(g_lib.t[i].path, dir, sizeof dir);
			if (!strcmp(dir, L->dir)) mrow_add(R_TRACK, i, NULL);
		}
		/* in file-name order, like the folder itself */
		for (int i = start; i < nmrows; i++)
			for (int j = i + 1; j < nmrows; j++)
				if (str_icmp_natural(path_base(g_lib.t[mrows[j].idx].path), path_base(g_lib.t[mrows[i].idx].path)) < 0) {
					MRow t = mrows[i]; mrows[i] = mrows[j]; mrows[j] = t;
				}
	} else if (L->level == ML_ROOT && mview == MV_PLAYLISTS) {
		pl_load();
		mrow_add(R_PLAYLIST, PL_RECENT, NULL);
		mrow_add(R_PLAYLIST, PL_MOST, NULL);
		for (int i = 0; i < pl_count(); i++) mrow_add(R_PLAYLIST, i, NULL);
		mrow_add(R_NEWPL, 0, NULL);
		for (int i = 0; i < nmrows; i++) {
			if (mrows[i].kind != R_PLAYLIST) continue;
			if (mrows[i].idx == PL_RECENT) {
				/* (counting these needs every file's date: just the cap) */
				mrows[i].n = MIN(g_lib.nt, PL_LIST_MAX);
				continue;
			}
			int *t;
			mrows[i].n = pl_tracks(mrows[i].idx, &t);
			for (int k = 0; k < mrows[i].n; k++) mrows[i].dur += g_lib.t[t[k]].dur;
			free(t);
		}
	} else if (L->level == ML_PLAYLIST) {
		int *t;
		int n = pl_tracks(L->idx, &t);
		if (n > 1) { mrow_add(R_PLAYALL, 0, NULL); mrow_add(R_SHUFFLE, 0, NULL); }
		for (int i = 0; i < n; i++) mrow_add(R_TRACK, t[i], NULL);
		free(t);
	} else if (L->level == ML_ROOT) {
		if (mview == MV_ARTISTS) for (int i = 0; i < g_lib.na; i++) mrow_add(R_ARTIST, i, NULL);
		else if (mview == MV_GENRES) for (int i = 0; i < g_lib.ng; i++) mrow_add(R_GENRE, i, NULL);
		else {
			if (g_lib.nt > 1) { mrow_add(R_PLAYALL, 0, NULL); mrow_add(R_SHUFFLE, 0, NULL); }
			for (int i = 0; i < g_lib.nt; i++) mrow_add(R_TRACK, g_lib.by_title[i], NULL);
		}
	} else if (L->level == ML_ARTIST && L->idx < g_lib.na) {
		MArtist *a = &g_lib.a[L->idx];
		if (a->n > 1) { mrow_add(R_PLAYALL, 0, NULL); mrow_add(R_SHUFFLE, 0, NULL); }
		for (int i = 0; i < a->n; i++) mrow_add(R_TRACK, a->t[i], NULL);
	} else if (L->level == ML_GENRE && L->idx < g_lib.ng) {
		MGenre *g = &g_lib.g[L->idx];
		mrow_add(R_PLAYALL, 0, NULL);
		mrow_add(R_SHUFFLE, 0, NULL);
		for (int i = 0; i < g->n; i++) mrow_add(R_ARTIST, g->a[i], NULL);
	}
	clamp_cursor(&L->cur, nmrows, mrows_vis(), mcols());
	mrows_gen = g_lib.gen;
}

/* the songs "Play all" plays, in order */
static int ctx_tracks(int **out) {
	MLevel *L = mlev();
	int n = 0, cap = 64;
	int *v = xmalloc(sizeof(int) * (size_t)cap);
#define PUSH(t) do { if (n == cap) { cap *= 2; v = xrealloc(v, sizeof(int) * (size_t)cap); } v[n++] = (t); } while (0)
	if (L->level == ML_GENRE && L->idx < g_lib.ng) {
		MGenre *g = &g_lib.g[L->idx];
		for (int i = 0; i < g->n; i++)
			for (int k = 0; k < g_lib.a[g->a[i]].n; k++) PUSH(g_lib.a[g->a[i]].t[k]);
	} else {
		for (int i = 0; i < nmrows; i++) if (mrows[i].kind == R_TRACK) PUSH(mrows[i].idx);
	}
#undef PUSH
	*out = v;
	return n;
}

static void play_tracks(const int *t, int n, int start_track, int shuffle) {
	char **paths = xmalloc(sizeof(char *) * (size_t)(n ? n : 1));
	int cnt = 0, start = 0;
	for (int i = 0; i < n; i++) {
		Track *tr = &g_lib.t[t[i]];
		if (tr->kind != MUSIC_CLOVE) continue;
		if (t[i] == start_track) start = cnt;
		paths[cnt++] = tr->path;
	}
	if (!cnt) { free(paths); toast("These songs open in RetroArch: pick one to play it"); return; }
	if (shuffle) {
		g_set.music_shuffle = 1;
		music_set_shuffle(1);
		mark_settings();
		start = (int)(plat_ticks() % (uint64_t)cnt);
	}
	music_play(paths, cnt, start);
	free(paths);
	dirty = 1;
}

static void music_relayout(void) {
	for (int i = 0; i < mdep; i++) { mst[i].cur.top = 0; clamp_cursor(&mst[i].cur, i == mdep - 1 ? nmrows : 1 << 20, mrows_vis(), mcols()); }
	dirty = 1;
}
static int music_is_active(void) {
	MusicInfo m;
	music_info(&m);
	return m.active;
}

static void music_enter(void) {
	if (mdep < 1) mdep = 1;
	if (!lib_dirty) mrows_build();
}

static void anim_music_cursors(float k) {
	for (int i = 0; i < mdep; i++) anim_cursor(&mst[i].cur, k);
}

/* what A does on the selected row, for the button guide */
static int music_hint_a(const char **label) {
	if (!nmrows) return 0;
	MRow *r = &mrows[mlev()->cur.sel];
	if (r->kind == R_TRACK) {
		MusicInfo m;
		music_info(&m);
		*label = m.active && !strcmp(m.path, g_lib.t[r->idx].path) ? (m.paused ? "Resume" : "Pause") : "Play";
	} else if (r->kind == R_PLAYALL || r->kind == R_SHUFFLE) *label = "Play";
	else if (r->kind == R_NEWPL) *label = "Make";
	else *label = "Open";
	return 1;
}

/* ------------------------------------------------------------ sleep timer */

static uint64_t sleep_end;   /* ticks when the music pauses (0 = no timer) */
static int sleep_eos;        /* "end of this song" is set */
static int sleep_fade_vol = -1;

static void sleep_timer_set(int minutes) {
	/* minutes: 0 off, -1 end of this song */
	if (sleep_fade_vol >= 0) { music_set_volume(g_set.music_volume); sleep_fade_vol = -1; }
	sleep_end = 0;
	sleep_eos = 0;
	music_pause_after_song(0);
	if (minutes > 0) {
		sleep_end = plat_ticks() + (uint64_t)minutes * 60000;
		toast("Music pauses in %d min", minutes);
	} else if (minutes < 0) {
		sleep_eos = 1;
		music_pause_after_song(1);
		toast("Music pauses after this song");
	} else {
		toast("Sleep timer off");
	}
	dirty = 1;
}

/* "23 min", "after this song", or "" */
static int sleep_timer_text(char *out, size_t n) {
	out[0] = 0;
	if (sleep_eos) { sncpy(out, "after this song", n); return 1; }
	if (!sleep_end) return 0;
	uint64_t now = plat_ticks();
	int left = now >= sleep_end ? 0 : (int)((sleep_end - now + 59999) / 60000);
	snfmt(out, n, "in %d min", MAX(left, 1));
	return 1;
}

static void sleep_timer_update(uint64_t now) {
	if (sleep_eos && !music_pausing_after_song()) {
		sleep_eos = 0;
		toast("Sleep timer: music paused");
	}
	if (!sleep_end) return;
	if (now >= sleep_end) {
		if (music_is_playing()) music_toggle_pause();
		music_set_volume(g_set.music_volume);
		sleep_fade_vol = -1;
		sleep_end = 0;
		toast("Sleep timer: music paused");
		dirty = 1;
		return;
	}
	/* the last 10 seconds fade out */
	uint64_t left = sleep_end - now;
	if (left < 10000 && music_is_playing()) {
		int v = (int)(g_set.music_volume * (int64_t)left / 10000);
		if (v != sleep_fade_vol) { sleep_fade_vol = v; music_set_volume(v); }
	}
}

/* --------------------------------------------- resume, play counts, ticks */

void ui_save_music(void) {
	char p[600];
	snfmt(p, sizeof p, "%s/music-state.tsv", g_paths.clove);
	if (g_set.music_resume) music_save_state(p);
	else unlink(p);
}

static void play_count_update(void) {
	/* a song counts as played once 30 seconds of it (or half, if shorter) have played */
	static char path[1024];
	static double last;
	static int done;
	MusicInfo m;
	music_info(&m);
	if (!m.active || m.paused || !m.path[0]) return;
	double need = m.len > 0 ? MIN(30.0, m.len * 0.5) : 30.0;
	if (strcmp(m.path, path)) {
		sncpy(path, m.path, sizeof path);
		done = m.pos >= need; /* picked up halfway (resumed): not a new play */
	} else if (m.pos + 3 < last) {
		done = 0; /* started again */
	}
	last = m.pos;
	if (!done && m.pos >= need) {
		done = 1;
		pc_bump(m.path);
	}
}

/* called from ui_update: read the library once a "Reading" frame is up */
static void music_update(void) {
	uint64_t now = plat_ticks();
	static uint64_t last_save;
	sleep_timer_update(now);
	play_count_update();
	if (g_set.music_resume && music_is_playing() && now - last_save > 30000) {
		last_save = now;
		ui_save_music();
	}
	if (!lib_scan_req) return;
	lib_scan_req = 0;
	lib_scan();
	lib_dirty = 0;
	mart_clear();
	mrows_build();
	dirty = 1;
}

/* ------------------------------------------------------------------ input */

enum { OA_M_PLAYSEL = 38, OA_M_SHUFSEL, OA_M_STOP = 40, OA_M_SHUF, OA_M_REP, OA_M_RESCAN, OA_M_GENRE, OA_M_ADDPL, OA_M_RMPL, OA_M_DELPL, OA_M_SLEEP, OA_M_ADDNP,
       OA_M_NEWPL_ADD, OA_M_LAYOUT, OA_M_SLEEPSET = 60, OA_M_SETGENRE = 200, OA_M_PICKPL = 300 };
static char genre_opts[64][64];
static int ngenre_opts;
static StrList pl_pend; /* songs waiting for "Add to playlist" to pick one */

static int sel_artist(void) {
	MLevel *L = mlev();
	if (nmrows && L->cur.sel < nmrows && mrows[L->cur.sel].kind == R_ARTIST) return mrows[L->cur.sel].idx;
	if (L->level == ML_ARTIST) return L->idx;
	if (nmrows && L->cur.sel < nmrows && mrows[L->cur.sel].kind == R_TRACK) return g_lib.t[mrows[L->cur.sel].idx].artist_i;
	return -1;
}

static MRow *sel_row(void) {
	MLevel *L = mlev();
	return nmrows && L->cur.sel < nmrows ? &mrows[L->cur.sel] : NULL;
}

/* the songs the selected row stands for, into pl_pend; returns how many */
static int pend_from_selection(void) {
	sl_free(&pl_pend);
	MRow *r = sel_row();
	if (!r) return 0;
	if (r->kind == R_TRACK) {
		sl_push(&pl_pend, g_lib.t[r->idx].path);
	} else if (r->kind == R_ARTIST) {
		MArtist *a = &g_lib.a[r->idx];
		for (int i = 0; i < a->n; i++) sl_push(&pl_pend, g_lib.t[a->t[i]].path);
	} else if (r->kind == R_GENRE) {
		MGenre *g = &g_lib.g[r->idx];
		for (int i = 0; i < g->n; i++)
			for (int k = 0; k < g_lib.a[g->a[i]].n; k++) sl_push(&pl_pend, g_lib.t[g_lib.a[g->a[i]].t[k]].path);
	} else if (r->kind == R_PLAYLIST) {
		int *t;
		int n = pl_tracks(r->idx, &t);
		for (int i = 0; i < n; i++) sl_push(&pl_pend, g_lib.t[t[i]].path);
		free(t);
	} else if (r->kind == R_PLAYALL || r->kind == R_SHUFFLE) {
		int *t;
		int n = ctx_tracks(&t);
		for (int i = 0; i < n; i++) sl_push(&pl_pend, g_lib.t[t[i]].path);
		free(t);
	}
	return pl_pend.n;
}

static void playlist_picker(void) {
	pl_load();
	char t[96];
	if (pl_pend.n == 1) {
		int ti = lib_find(pl_pend.v[0]);
		snfmt(t, sizeof t, "Add \"%s\" to", ti >= 0 ? g_lib.t[ti].title : path_base(pl_pend.v[0]));
	} else {
		snfmt(t, sizeof t, "Add %d songs to", pl_pend.n);
	}
	open_overlay(OV_QUICK, t);
	ov_add("New playlist\xE2\x80\xA6", OA_M_NEWPL_ADD);
	for (int i = 0; i < pl_count() && i < 36; i++) ov_add(pl_get(i)->name, OA_M_PICKPL + i);
	if (pl_count()) ov.sel = 1; /* the first existing playlist */
	ov.asel = (float)ov.sel;
	ov_add("Cancel", OA_CANCEL);
}

/* adds pl_pend to playlist i and says what happened */
static void pend_add_to(int i) {
	Playlist *p = pl_get(i);
	if (!p) return;
	char name[96];
	sncpy(name, p->name, sizeof name);
	int added = 0, err = 0;
	for (int k = 0; k < pl_pend.n; k++) {
		int r = pl_add(i, pl_pend.v[k]);
		if (r > 0) added++;
		if (r < 0) err = 1;
	}
	if (err && !added) toast("Couldn't save the playlist (is the card full or read-only?)");
	else if (!added) toast("Already in %s", name);
	else if (added == 1) toast("Added to %s", name);
	else toast("Added %d songs to %s", added, name);
	sl_free(&pl_pend);
	if (mview == MV_PLAYLISTS) mrows_build();
}

static void sleep_picker(void) {
	char cur[48];
	sleep_timer_text(cur, sizeof cur);
	char t[96];
	if (cur[0]) snfmt(t, sizeof t, "Sleep timer: pauses %s", cur);
	else sncpy(t, "Sleep timer", sizeof t);
	open_overlay(OV_QUICK, t);
	static const char *L[] = {"Off", "15 minutes", "30 minutes", "45 minutes", "1 hour", "1 hour 30 minutes", "At the end of this song"};
	for (int i = 0; i < ARRAY_LEN(L); i++) ov_add(L[i], OA_M_SLEEPSET + i);
	ov.sel = sleep_eos ? 6 : sleep_end ? 0 : 1;
	ov.asel = (float)ov.sel;
	ov_add("Cancel", OA_CANCEL);
}

static void music_options(void) {
	MusicInfo m;
	music_info(&m);
	static const char *RP[] = {"Repeat: off", "Repeat: all", "Repeat: one song"};
	open_overlay(OV_QUICK, "Music");
	MRow *r = music_np ? NULL : sel_row();
	MLevel *L = mlev();
	if (music_np && m.active) ov_add("Add this song to a playlist\xE2\x80\xA6", OA_M_ADDNP);
	if (r && (r->kind == R_ARTIST || r->kind == R_GENRE || r->kind == R_PLAYLIST)) {
		/* the whole artist / genre / playlist from here, without going in */
		char l[80];
		const char *nm = r->kind == R_ARTIST ? g_lib.a[r->idx].name : r->kind == R_GENRE ? g_lib.g[r->idx].name : "this playlist";
		snfmt(l, sizeof l, "Play all of %s", nm);
		ov_add(l, OA_M_PLAYSEL);
		snfmt(l, sizeof l, "Shuffle all of %s", nm);
		ov_add(l, OA_M_SHUFSEL);
	}
	if (r && r->kind != R_DIR && r->kind != R_NEWPL)
		ov_add(r->kind == R_TRACK ? "Add to a playlist\xE2\x80\xA6" : "Add these songs to a playlist\xE2\x80\xA6", OA_M_ADDPL);
	if (r && r->kind == R_TRACK && L->level == ML_PLAYLIST && L->idx >= 0) ov_add("Remove from this playlist", OA_M_RMPL);
	if (r && r->kind == R_PLAYLIST && r->idx >= 0) ov_add("Delete this playlist", OA_M_DELPL);
	int ai = music_np ? -1 : sel_artist();
	if (ai >= 0 && !(r && (r->kind == R_PLAYLIST || r->kind == R_NEWPL))) {
		char l[64];
		snfmt(l, sizeof l, "Set genre for %s", g_lib.a[ai].name);
		ov_add(l, OA_M_GENRE);
		sncpy(ov.name, g_lib.a[ai].name, sizeof ov.name);
	}
	{
		char st[48], l[64];
		if (sleep_timer_text(st, sizeof st)) snfmt(l, sizeof l, "Sleep timer: pauses %s", st);
		else sncpy(l, "Sleep timer: off", sizeof l);
		ov_add(l, OA_M_SLEEP);
	}
	if (m.active) ov_add("Stop music", OA_M_STOP);
	if (!music_np) {
		static const char *MLN[] = {"Layout: list", "Layout: grid", "Layout: shelf"};
		ov_add(MLN[CLAMP(g_set.music_layout, 0, 2)], OA_M_LAYOUT);
	}
	ov_add(g_set.music_shuffle ? "Shuffle: on" : "Shuffle: off", OA_M_SHUF);
	ov_add(RP[g_set.music_repeat % 3], OA_M_REP);
	ov_add("Read the Music folder again", OA_M_RESCAN);
	ov_add("Cancel", OA_CANCEL);
}

static void genre_picker(const char *artist) {
	ngenre_opts = 0;
	char cur[64] = "";
	for (int i = 0; i < g_lib.na; i++) if (!strcmp(g_lib.a[i].name, artist)) sncpy(cur, g_lib.a[i].genre, sizeof cur);
	/* genres already in use first, then the usual ones */
	for (int i = 0; i < g_lib.ng && ngenre_opts < 63; i++)
		if (strcmp(g_lib.g[i].name, LIB_UNSORTED)) sncpy(genre_opts[ngenre_opts++], g_lib.g[i].name, 64);
	int np;
	const char *const *pre = lib_genre_presets(&np);
	for (int i = 0; i < np && ngenre_opts < 63; i++) {
		int dup = 0;
		for (int k = 0; k < ngenre_opts; k++) if (str_ieq(genre_opts[k], pre[i])) dup = 1;
		if (!dup) sncpy(genre_opts[ngenre_opts++], pre[i], 64);
	}
	sncpy(genre_opts[ngenre_opts++], LIB_UNSORTED, 64);
	char t[96];
	snfmt(t, sizeof t, "Genre for %s", artist);
	char keep[256];
	sncpy(keep, artist, sizeof keep);
	open_overlay(OV_QUICK, t);
	sncpy(ov.name, keep, sizeof ov.name);
	for (int i = 0; i < ngenre_opts; i++) {
		ov_add(genre_opts[i], OA_M_SETGENRE + i);
		if (str_ieq(genre_opts[i], cur)) ov.sel = i;
	}
	ov.asel = (float)ov.sel;
}

/* overlay actions for the Music tab; returns 1 if handled */
static int music_overlay_action(int act) {
	if (act >= OA_M_SETGENRE && act < OA_M_SETGENRE + ngenre_opts) {
		const char *g = genre_opts[act - OA_M_SETGENRE];
		lib_set_genre(ov.name, g);
		toast("%s: %s", ov.name, g);
		mrows_build();
		return 1;
	}
	if (act >= OA_M_PICKPL && act < OA_M_PICKPL + 40) { pend_add_to(act - OA_M_PICKPL); return 1; }
	if (act >= OA_M_SLEEPSET && act < OA_M_SLEEPSET + 7) {
		static const int MIN_[] = {0, 15, 30, 45, 60, 90, -1};
		sleep_timer_set(MIN_[act - OA_M_SLEEPSET]);
		return 1;
	}
	switch (act) {
	case OA_M_GENRE: { char a[256]; sncpy(a, ov.name, sizeof a); genre_picker(a); return 1; }
	case OA_M_ADDPL: if (pend_from_selection()) playlist_picker(); return 1;
	case OA_M_PLAYSEL: case OA_M_SHUFSEL: {
		/* the selected artist / genre / playlist as a queue */
		if (!pend_from_selection()) return 1;
		int *t = xmalloc(sizeof(int) * (size_t)pl_pend.n), n = 0;
		for (int i = 0; i < pl_pend.n; i++)
			for (int k = 0; k < g_lib.nt; k++) if (!strcmp(g_lib.t[k].path, pl_pend.v[i])) { t[n++] = k; break; }
		if (n) play_tracks(t, n, t[0], act == OA_M_SHUFSEL);
		free(t);
		sl_free(&pl_pend);
		return 1;
	}
	case OA_M_ADDNP: {
		MusicInfo m;
		music_info(&m);
		sl_free(&pl_pend);
		if (m.active) { sl_push(&pl_pend, m.path); playlist_picker(); }
		return 1;
	}
	case OA_M_NEWPL_ADD: kb_open(KB_NEWPL_ADD, "Name the new playlist", ""); return 1;
	case OA_M_RMPL: {
		MRow *r = sel_row();
		MLevel *L = mlev();
		if (r && r->kind == R_TRACK && L->level == ML_PLAYLIST && pl_remove(L->idx, g_lib.t[r->idx].path)) {
			toast("Removed from %s", pl_title(L->idx));
			mrows_build();
		}
		return 1;
	}
	case OA_M_DELPL: {
		MRow *r = sel_row();
		if (!r || r->kind != R_PLAYLIST || r->idx < 0) return 1;
		char t[128];
		snfmt(t, sizeof t, "Delete the playlist \"%s\"?", pl_title(r->idx));
		int idx = r->idx;
		open_overlay(OV_CONFIRM, t);
		ov.confirm_action = 1004;
		ov.sys = idx;
		ov_add("Cancel", OA_CANCEL);
		ov_add("Delete playlist", OA_YES);
		return 1;
	}
	case OA_M_SLEEP: sleep_picker(); return 1;
	case OA_M_LAYOUT: {
		static const char *MLN[] = {"List", "Grid", "Shelf"};
		g_set.music_layout = (g_set.music_layout + 1) % MLAY_COUNT;
		music_relayout();
		mark_settings();
		toast("Music layout: %s", MLN[g_set.music_layout]);
		return 1;
	}
	case OA_M_STOP: sleep_timer_set(0); music_stop(); music_np = 0; toast("Music stopped"); return 1;
	case OA_M_SHUF:
		g_set.music_shuffle ^= 1;
		music_set_shuffle(g_set.music_shuffle);
		mark_settings();
		toast(g_set.music_shuffle ? "Shuffle on" : "Shuffle off");
		return 1;
	case OA_M_REP: {
		static const char *RP[] = {"Repeat off", "Repeat all", "Repeat one song"};
		g_set.music_repeat = (g_set.music_repeat + 1) % 3;
		music_set_repeat(g_set.music_repeat);
		mark_settings();
		toast("%s", RP[g_set.music_repeat]);
		return 1;
	}
	case OA_M_RESCAN: lib_dirty = 1; mart_clear(); return 1;
	}
	return 0;
}

static void music_delete_playlist(int idx) {
	char n[96];
	sncpy(n, pl_title(idx), sizeof n);
	if (pl_delete(idx) == 0) toast("Deleted %s (a copy is kept in CFW/clove/deleted-playlists)", n);
	else toast("Couldn't delete %s", n);
	mrows_build();
}

/* opens the Music tab at a view and level (used by Search and new playlists) */
static void music_open_at(int view, int level, int idx) {
	mview = view;
	mdep = 1;
	memset(&mst[0], 0, sizeof mst[0]);
	music_np = 0;
	if (level != ML_ROOT && mdep < ARRAY_LEN(mst)) {
		MLevel *n = &mst[mdep++];
		memset(n, 0, sizeof *n);
		n->level = level;
		n->idx = idx;
	}
	if (!lib_dirty) mrows_build();
	dirty = 1;
}

/* the keyboard finished naming a playlist */
static void music_named_playlist(int adding, const char *name) {
	int idx = pl_create(name);
	if (idx < 0) { toast("Couldn't make that playlist"); return; }
	if (adding) { pend_add_to(idx); return; }
	char n[96];
	sncpy(n, pl_title(idx), sizeof n);
	toast("Made %s. Press X on songs to add them", n);
	if (mview == MV_PLAYLISTS && mdep == 1) {
		mrows_build();
		for (int i = 0; i < nmrows; i++)
			if (mrows[i].kind == R_PLAYLIST && mrows[i].idx >= 0 && !strcmp(pl_title(mrows[i].idx), n)) mlev()->cur.sel = i;
		clamp_cursor(&mlev()->cur, nmrows, mrows_vis(), 1);
	}
}

/* Y on a song: in or out of the Favorites playlist (an ordinary .m3u like the
 * others), the way Y favourites a game or a film */
#define FAV_SONGS "Favorites"
static void mrow_label(MRow *r, char *out, size_t n, char *sub, size_t sn);
static int fav_song_has(const char *path) {
	int i = pl_find(FAV_SONGS);
	return i >= 0 && path && path[0] && pl_contains(i, path);
}
static void fav_song_toggle(const char *path, const char *title) {
	if (!path || !path[0]) return;
	int i = pl_find(FAV_SONGS);
	if (i < 0) i = pl_create(FAV_SONGS);
	if (i < 0) { toast("Couldn't make the Favorites playlist"); return; }
	if (pl_contains(i, path)) { pl_remove(i, path); toast("Took %s out of Favorites", title && title[0] ? title : "it"); }
	else if (pl_add(i, path) > 0) toast("Added %s to Favorites", title && title[0] ? title : "it");
	else { toast("Couldn't change the Favorites playlist"); return; }
	if (!music_np && (mview == MV_PLAYLISTS || mlev()->level == ML_PLAYLIST)) {
		mrows_build();
		clamp_cursor(&mlev()->cur, nmrows, mrows_vis(), mcols());
	}
	dirty = 1;
}

/* the Y hint in the lists: only on a song */
static int music_hint_y(const char **label) {
	if (music_np || lib_dirty || !nmrows) return 0;
	MRow *r = sel_row();
	if (!r || r->kind != R_TRACK) return 0;
	*label = fav_song_has(g_lib.t[r->idx].path) ? "Unfavorite" : "Favorite";
	return 1;
}

/* L2 / R2 in the lists: to the previous / next letter, as in the game lists */
static void mjump_letter(int dir) {
	if (!nmrows) return;
	MLevel *L = mlev();
	char a[160], sub[160];
	mrow_label(&mrows[L->cur.sel], a, sizeof a, sub, sizeof sub);
	char cur = (char)tolower((unsigned char)a[0]);
	int i = L->cur.sel;
	for (int k = 0; k < nmrows; k++) {
		i += dir;
		if (i < 0 || i >= nmrows) break;
		mrow_label(&mrows[i], a, sizeof a, sub, sizeof sub);
		char c = (char)tolower((unsigned char)a[0]);
		if (c != cur) {
			if (dir < 0)
				while (i > 0) {
					mrow_label(&mrows[i - 1], a, sizeof a, sub, sizeof sub);
					if ((char)tolower((unsigned char)a[0]) != c) break;
					i--;
				}
			L->cur.sel = i;
			break;
		}
	}
	clamp_cursor(&L->cur, nmrows, mrows_vis(), mcols());
	dirty = 1;
}

static int now_playing_button(int b) {
	switch (b) {
	case B_B: case B_START: music_np = 0; dirty = 1; return 1;
	case B_A: music_toggle_pause(); dirty = 1; return 1;
	case B_LEFT: music_seek(-10); dirty = 1; return 1;
	case B_RIGHT: music_seek(10); dirty = 1; return 1;
	case B_L1: music_seek(-60); dirty = 1; return 1;
	case B_R1: music_seek(60); dirty = 1; return 1;
	case B_L2: music_prev(); return 1;
	case B_R2: music_next(); return 1;
	case B_Y: {
		MusicInfo m;
		music_info(&m);
		if (m.active) fav_song_toggle(m.path, m.title);
		return 1;
	}
	case B_SELECT: music_overlay_action(OA_M_SHUF); return 1;
	case B_X: music_options(); return 1;
	}
	return 1; /* nothing else leaks through to the tab */
}

static int music_button(int b) {
	if (music_np) return now_playing_button(b);
	if (lib_dirty) return b != B_B; /* still reading */
	MLevel *L = mlev();
	int rows = mrows_vis(), cols = mcols();
	if (mlayout() == MLAY_GRID) {
		switch (b) {
		case B_UP: move_cursor(&L->cur, nmrows, -cols, rows, cols, 1); return 1;
		case B_DOWN: move_cursor(&L->cur, nmrows, cols, rows, cols, 1); return 1;
		case B_LEFT: move_cursor(&L->cur, nmrows, -1, rows, cols, 1); return 1;
		case B_RIGHT: move_cursor(&L->cur, nmrows, 1, rows, cols, 1); return 1;
		}
	} else if (mlayout() == MLAY_SHELF) {
		switch (b) {
		case B_LEFT: move_cursor(&L->cur, nmrows, -1, 1, 1, 1); return 1;
		case B_RIGHT: move_cursor(&L->cur, nmrows, 1, 1, 1, 1); return 1;
		case B_UP: move_cursor(&L->cur, nmrows, -5, 1, 1, 0); return 1;
		case B_DOWN: move_cursor(&L->cur, nmrows, 5, 1, 1, 0); return 1;
		}
	}
	switch (b) {
	case B_UP: move_cursor(&L->cur, nmrows, -1, rows, 1, 1); return 1;
	case B_DOWN: move_cursor(&L->cur, nmrows, 1, rows, 1, 1); return 1;
	case B_LEFT: move_cursor(&L->cur, nmrows, -rows, rows, 1, 0); return 1;
	case B_RIGHT: move_cursor(&L->cur, nmrows, rows, rows, 1, 0); return 1;
	case B_L2: mjump_letter(-1); return 1;
	case B_R2: mjump_letter(1); return 1;
	case B_Y: {
		MRow *r = sel_row();
		if (r && r->kind == R_TRACK) fav_song_toggle(g_lib.t[r->idx].path, g_lib.t[r->idx].title);
		return 1;
	}
	case B_X: music_options(); return 1;
	case B_SELECT:
		mview = (mview + 1) % MV_COUNT;
		mdep = 1;
		memset(&mst[0], 0, sizeof mst[0]);
		mrows_build();
		dirty = 1;
		return 1;
	case B_START: {
		MusicInfo m;
		music_info(&m);
		if (!m.active) return 0; /* START: settings, as on the other tabs */
		music_np = 1;
		dirty = 1;
		return 1;
	}
	case B_B:
		if (mdep > 1) { mdep--; mrows_build(); dirty = 1; return 1; }
		return 0;
	case B_A: {
		if (!nmrows) return 1;
		MRow *r = &mrows[L->cur.sel];
		if (r->kind == R_NEWPL) { kb_open(KB_NEWPL, "Name the new playlist", ""); return 1; }
		if (r->kind == R_ARTIST || r->kind == R_GENRE || r->kind == R_DIR || r->kind == R_PLAYLIST) {
			if (mdep >= ARRAY_LEN(mst)) return 1;
			MLevel *n = &mst[mdep++];
			memset(n, 0, sizeof *n);
			n->level = r->kind == R_ARTIST ? ML_ARTIST : r->kind == R_GENRE ? ML_GENRE : r->kind == R_PLAYLIST ? ML_PLAYLIST : ML_FOLDER;
			n->idx = r->idx;
			if (r->path) sncpy(n->dir, r->path, sizeof n->dir);
			mrows_build();
			dirty = 1;
			return 1;
		}
		int *t;
		int n = ctx_tracks(&t);
		if (r->kind == R_PLAYALL || r->kind == R_SHUFFLE) {
			if (r->kind == R_PLAYALL && g_set.music_shuffle) { g_set.music_shuffle = 0; music_set_shuffle(0); mark_settings(); }
			play_tracks(t, n, n ? t[0] : -1, r->kind == R_SHUFFLE);
			free(t);
			return 1;
		}
		Track *tr = &g_lib.t[r->idx];
		MusicInfo m;
		music_info(&m);
		if (m.active && !strcmp(m.path, tr->path)) { music_toggle_pause(); free(t); return 1; }
		if (tr->kind == MUSIC_RETROARCH) {
			/* M4A, AAC, OPUS ...: RetroArch's FFmpeg player handles these */
			free(t);
			int vs = data_sys_index("VIDEOS");
			if (vs < 0) { toast("RetroArch's media player isn't installed"); return 1; }
			music_stop();
			Entry le = {tr->title, tr->path, vs, 0, 0};
			launch_entry(&le, LK_SYSTEM);
			return 1;
		}
		play_tracks(t, n, r->idx, 0);
		free(t);
		return 1;
	}
	}
	return 0;
}

/* ---------------------------------------------------------------- drawing */

static void draw_play_icon(int x, int y, int h, uint32_t c) {
	for (int i = 0; i < h; i++) {
		int w = (i < h / 2 ? i : h - 1 - i) * 2 / 2 + 1;
		gfx_rect(x, y + i, w, 1, c);
	}
}

static void draw_shuffle_icon(int x, int y, int h, uint32_t c) {
	/* two crossing lines with arrow heads */
	for (int i = 0; i < h; i++) {
		gfx_rect(x + i, y + i, 2, 2, c);
		gfx_rect(x + i, y + h - 1 - i, 2, 2, c);
	}
	gfx_rect(x + h - 4, y - 1, 5, 2, c);
	gfx_rect(x + h - 1, y - 1, 2, 5, c);
	gfx_rect(x + h - 4, y + h - 1, 5, 2, c);
	gfx_rect(x + h - 1, y + h - 4, 2, 5, c);
}

static void fmt_dur_long(int s, char *out, size_t n) {
	if (s >= 3600) snfmt(out, n, "%d hr %d min", s / 3600, s / 60 % 60);
	else if (s >= 60) snfmt(out, n, "%d min", (s + 30) / 60);
	else snfmt(out, n, "%d sec", s);
}

static void draw_music_header(int y, int h) {
	MLevel *L = mlev();
	if (mdep == 1) {
		int x = PAD;
		if (glass()) { /* centred */
			int tot = 0;
			for (int i = 0; i < MV_COUNT; i++) tot += text_width(F_SMALL, MV_NAME[i]) + 22 + 8;
			x = (SCREEN_W - (tot - 8)) / 2;
		}
		for (int i = 0; i < MV_COUNT; i++) {
			int tw = text_width(F_SMALL, MV_NAME[i]);
			int cw = tw + 22, ch = 28, cy = y + (h - ch) / 2;
			if (i == mview) gfx_rrect(x, cy, cw, ch, ch / 2, g_theme.accent);
			else gfx_rrect(x, cy, cw, ch, ch / 2, col_alpha(g_theme.surface, 200));
			text_draw(F_SMALL, x + 11, cy + ch / 2 - font_height(F_SMALL) / 2, MV_NAME[i], i == mview ? g_theme.accent_text : g_theme.text_dim);
			x += cw + 8;
		}
		return;
	}
	/* breadcrumb: Genres > UK Rap & Grime > Potter Payper */
	char crumb[512] = "";
	sncpy(crumb, MV_NAME[mview], sizeof crumb);
	for (int i = 1; i < mdep - 1; i++) {
		const char *part = mst[i].level == ML_ARTIST && mst[i].idx < g_lib.na ? g_lib.a[mst[i].idx].name
		                 : mst[i].level == ML_GENRE && mst[i].idx < g_lib.ng ? g_lib.g[mst[i].idx].name
		                 : mst[i].level == ML_PLAYLIST ? pl_title(mst[i].idx)
		                 : path_base(mst[i].dir);
		snfmt(crumb + strlen(crumb), sizeof crumb - strlen(crumb), "  \xE2\x80\xBA  %s", part);
	}
	const char *last = L->level == ML_ARTIST && L->idx < g_lib.na ? g_lib.a[L->idx].name
	                 : L->level == ML_GENRE && L->idx < g_lib.ng ? g_lib.g[L->idx].name
	                 : L->level == ML_PLAYLIST ? pl_title(L->idx)
	                 : path_base(L->dir);
	const char *sep = "  \xE2\x80\xBA  ";
	int sw = text_width(F_SMALL, sep);
	int cw = MIN(text_width(F_SMALL, crumb), MLW / 2);
	int x = PAD + 4, ty = y + h / 2 - font_height(F_SMALL) / 2;
	text_draw_fit(F_SMALL, x, ty, cw, crumb, g_theme.text_dim, 0);
	text_draw(F_SMALL, x + cw, ty, sep, g_theme.text_dim);
	text_draw_fit(F_BODY, x + cw + sw, y + h / 2 - font_height(F_BODY) / 2, SCREEN_W - PAD * 2 - cw - sw - 8, last, g_theme.text, 0);
}

/* a playlist's tile: its colours from the name, with a mark for Clove's own two */
static void draw_playlist_tile(int idx, int x, int y, int size, int radius) {
	const char *name = idx == PL_RECENT ? "Recently added" : idx == PL_MOST ? "Most played" : pl_title(idx);
	Image *im = gen_cover_img(idx == PL_RECENT ? "zz recent" : idx == PL_MOST ? "most played!" : name, size);
	gfx_blit_rounded(im, x, y, radius, 255);
	uint32_t c = 0xF0FFFFFF;
	int cx = x + size / 2, cy = y + size / 2;
	if (idx == PL_RECENT) {
		/* a clock face */
		int r = size * 3 / 10, t = MAX(2, size / 22);
		gfx_circle(cx, cy, r, c);
		gfx_circle(cx, cy, r - t, col_alpha(0x000000, 90));
		gfx_rect(cx - t / 2, cy - r * 3 / 5, t, r * 3 / 5, c);
		gfx_rect(cx - t / 2, cy - t / 2, r / 2, t, c);
	} else if (idx == PL_MOST) {
		draw_star(cx, cy, size * 3 / 10, c);
	} else {
		/* three lines and a note: a list of songs */
		int lw = size / 2, lh = MAX(2, size / 18), lx = x + size / 5;
		for (int i = 0; i < 3; i++) gfx_rrect(lx, y + size * (30 + i * 14) / 100, i == 2 ? lw * 2 / 3 : lw, lh, lh / 2, c);
		draw_note(x + size * 62 / 100, y + size * 52 / 100, size / 3, c);
	}
}

static void draw_music_row(MRow *r, int x, int y, int w, int rh, int sel, const MusicInfo *m) {
	uint32_t tc = sel ? sel_fg() : g_theme.text, dc = sel ? sel_dim() : g_theme.text_dim;
	int th = rh - 12, tx = x + 10, tw = w - 20;
	int ty = y + rh / 2 - font_height(F_BODY) / 2;
	char right[64] = "";
	const char *label = "";
	switch (r->kind) {
	case R_ARTIST: {
		MArtist *a = &g_lib.a[r->idx];
		draw_artist_cover(r->idx, tx, y + 6, th, th / 2);
		tx += th + 10; tw -= th + 10;
		label = a->name;
		snfmt(right, sizeof right, "%d", a->n);
		break;
	}
	case R_GENRE: {
		MGenre *g = &g_lib.g[r->idx];
		draw_genre_tile(r->idx, tx, y + 6, th, MIN(8, th / 3));
		tx += th + 10; tw -= th + 10;
		label = g->name;
		snfmt(right, sizeof right, "%d", g->n);
		break;
	}
	case R_TRACK: {
		Track *t = &g_lib.t[r->idx];
		if (m->active && !strcmp(m->path, t->path)) {
			draw_note(tx, y + rh / 2 - 9, 18, sel && !glass() ? g_theme.accent_text : g_theme.accent);
			tx += 22; tw -= 22;
		} else if (mview == MV_SONGS && mlev()->level == ML_ROOT) {
			draw_track_cover(r->idx, tx, y + 6, th, 6);
			tx += th + 10; tw -= th + 10;
		}
		label = t->title;
		if (t->kind == MUSIC_RETROARCH) {
			sncpy(right, path_ext(t->path), sizeof right);
			for (char *q = right; *q; q++) *q = (char)toupper((unsigned char)*q);
		} else if (t->dur) fmt_time(t->dur, right, sizeof right);
		break;
	}
	case R_DIR:
		draw_folder(tx, y + rh / 2 - 9, 18, sel ? sel_fg() : g_theme.text_dim);
		tx += 30; tw -= 30;
		label = path_base(r->path);
		break;
	case R_PLAYALL:
		draw_play_icon(tx + 3, y + rh / 2 - 8, 16, sel && !glass() ? g_theme.accent_text : g_theme.accent);
		tx += 26; tw -= 26;
		label = "Play all";
		break;
	case R_SHUFFLE:
		draw_shuffle_icon(tx + 1, y + rh / 2 - 7, 14, sel && !glass() ? g_theme.accent_text : g_theme.accent);
		tx += 26; tw -= 26;
		label = "Shuffle";
		break;
	case R_PLAYLIST:
		draw_playlist_tile(r->idx, tx, y + 6, th, MIN(8, th / 3));
		tx += th + 10; tw -= th + 10;
		label = pl_title(r->idx);
		if (r->idx != PL_RECENT || r->n) snfmt(right, sizeof right, "%d", r->n);
		break;
	case R_NEWPL: {
		uint32_t c = sel && !glass() ? g_theme.accent_text : g_theme.accent;
		int cx = tx + th / 2, cy = y + 6 + th / 2;
		gfx_rrect_stroke(tx, y + 6, th, th, MIN(8, th / 3), 2, col_alpha(c, 200));
		gfx_rect(cx - th / 4, cy - 1, th / 2, 3, c);
		gfx_rect(cx - 1, cy - th / 4, 3, th / 2, c);
		tx += th + 10; tw -= th + 10;
		label = "New playlist";
		break;
	}
	}
	if (right[0]) {
		int rw = text_width(F_SMALL, right);
		text_draw(F_SMALL, x + w - 12 - rw, y + rh / 2 - font_height(F_SMALL) / 2, right, dc);
		tw -= rw + 12;
	}
	text_draw_fit(F_BODY, tx, ty, tw, label, tc, 0);
}

/* the card on the right: what's selected, in numbers */
static void draw_music_info(int px, int y, int pw, int h, const MusicInfo *m) {
	int r = g_theme.radius;
	draw_panel(px, y, pw, h, r);
	MLevel *L = mlev();
	MRow *sr = nmrows ? &mrows[L->cur.sel] : NULL;
	int cs = MIN(124, h - 120), cx = px + 14, cy = y + 14;
	int tx = px + 14, tw = pw - 28;
	char line[160], d[48];
	int ai = -1, gi = -1, ti = -1;
	if ((sr && (sr->kind == R_PLAYLIST || sr->kind == R_NEWPL)) || (L->level == ML_PLAYLIST && !(sr && sr->kind == R_TRACK))) {
		int pi = sr && sr->kind == R_PLAYLIST ? sr->idx : L->level == ML_PLAYLIST ? L->idx : 0;
		int ty = cy + cs + 10;
		if (sr && sr->kind == R_NEWPL) {
			gfx_rrect_stroke(cx, cy, cs, cs, MIN(r, 12), 3, col_alpha(g_theme.accent, 200));
			gfx_rect(cx + cs / 4, cy + cs / 2 - 2, cs / 2, 5, g_theme.accent);
			gfx_rect(cx + cs / 2 - 2, cy + cs / 4, 5, cs / 2, g_theme.accent);
			text_draw_fit(F_TITLE, tx, ty, tw, "New playlist", g_theme.text, 0);
			ty += font_height(F_TITLE);
			text_draw_wrap(F_SMALL, tx, ty, tw, 4, "Name it, then press X on any song, artist or genre to add it.", g_theme.text_dim, 0);
			return;
		}
		draw_playlist_tile(pi, cx, cy, cs, MIN(r, 12));
		text_draw_fit(F_TITLE, tx, ty, tw, pl_title(pi), g_theme.text, 0);
		ty += font_height(F_TITLE);
		int n = 0, dur = 0;
		if (L->level == ML_PLAYLIST) {
			for (int i = 0; i < nmrows; i++) if (mrows[i].kind == R_TRACK) { n++; dur += g_lib.t[mrows[i].idx].dur; }
		} else if (sr) {
			n = sr->n;
			dur = sr->dur;
		}
		fmt_dur_long(dur, d, sizeof d);
		if (pi == PL_RECENT && L->level != ML_PLAYLIST) snfmt(line, sizeof line, "The newest %d songs on the card", n);
		else if (dur > 0) snfmt(line, sizeof line, "%d song%s \xC2\xB7 %s", n, n == 1 ? "" : "s", d);
		else snfmt(line, sizeof line, "%d song%s", n, n == 1 ? "" : "s");
		text_draw_fit(F_SMALL, tx, ty, tw, line, g_theme.accent, 0);
		ty += font_height(F_SMALL) + 4;
		const char *about = pi == PL_RECENT ? "Songs newest to the card first."
		                  : pi == PL_MOST ? "The songs you play most. A song counts once 30 seconds of it have played."
		                  : n ? "Saved in Music/Playlists. X: add or remove songs." : "Empty for now: press X on any song, artist or genre to add it.";
		text_draw_wrap(F_SMALL, tx, ty, tw, 4, about, g_theme.text_dim, 0);
		return;
	}
	if (sr && sr->kind == R_ARTIST) ai = sr->idx;
	else if (sr && sr->kind == R_GENRE) gi = sr->idx;
	else if (sr && sr->kind == R_TRACK) ti = sr->idx;
	else if (L->level == ML_ARTIST) ai = L->idx;
	else if (L->level == ML_GENRE) gi = L->idx;
	if (ai >= 0) {
		MArtist *a = &g_lib.a[ai];
		draw_artist_cover(ai, cx, cy, cs, cs / 2);
		int ty = cy + cs + 10;
		text_draw_fit(F_TITLE, tx, ty, tw, a->name, g_theme.text, 0);
		ty += font_height(F_TITLE);
		text_draw_fit(F_SMALL, tx, ty, tw, a->genre, g_theme.accent, 0);
		ty += font_height(F_SMALL) + 4;
		fmt_dur_long(a->dur, d, sizeof d);
		if (a->albums > 0) snfmt(line, sizeof line, "%d song%s \xC2\xB7 %d album%s \xC2\xB7 %s", a->n, a->n == 1 ? "" : "s", a->albums, a->albums == 1 ? "" : "s", d);
		else snfmt(line, sizeof line, "%d song%s \xC2\xB7 %s", a->n, a->n == 1 ? "" : "s", d);
		text_draw_wrap(F_SMALL, tx, ty, tw, 2, line, g_theme.text_dim, 0);
		return;
	}
	if (gi >= 0) {
		MGenre *g = &g_lib.g[gi];
		draw_genre_tile(gi, cx, cy, cs, MIN(r, 12));
		int ty = cy + cs + 10;
		text_draw_fit(F_TITLE, tx, ty, tw, g->name, g_theme.text, 0);
		ty += font_height(F_TITLE);
		fmt_dur_long(g->dur, d, sizeof d);
		snfmt(line, sizeof line, "%d artist%s \xC2\xB7 %d song%s \xC2\xB7 %s", g->n, g->n == 1 ? "" : "s", g->ntracks, g->ntracks == 1 ? "" : "s", d);
		text_draw_wrap(F_SMALL, tx, ty, tw, 2, line, g_theme.text_dim, 0);
		ty += font_height(F_SMALL) * 2 + 4;
		/* who's in it */
		line[0] = 0;
		for (int i = 0; i < g->n && i < 4; i++) snfmt(line + strlen(line), sizeof line - strlen(line), "%s%s", i ? ", " : "", g_lib.a[g->a[i]].name);
		if (g->n > 4) snfmt(line + strlen(line), sizeof line - strlen(line), " +%d", g->n - 4);
		text_draw_wrap(F_SMALL, tx, ty, tw, 2, line, g_theme.text, 0);
		return;
	}
	if (ti >= 0) {
		Track *t = &g_lib.t[ti];
		draw_track_cover(ti, cx, cy, cs, MIN(r, 10));
		int ty = cy + cs + 10;
		int lines = text_draw_wrap(F_BODY, tx, ty, tw, 2, t->title, g_theme.text, 0);
		ty += lines * font_height(F_BODY) + 2;
		text_draw_fit(F_SMALL, tx, ty, tw, t->artist, g_theme.text_dim, 0);
		ty += font_height(F_SMALL) + 2;
		if (t->dur) fmt_time(t->dur, d, sizeof d);
		else d[0] = 0;
		snfmt(line, sizeof line, "%s%s%s", t->genre ? t->genre : "", d[0] ? " \xC2\xB7 " : "", d);
		text_draw_fit(F_SMALL, tx, ty, tw, line, g_theme.accent, 0);
		return;
	}
	/* "Play all" / a folder / the whole library */
	int n = 0, dur = 0;
	int *t;
	n = ctx_tracks(&t);
	for (int i = 0; i < n; i++) dur += g_lib.t[t[i]].dur;
	free(t);
	draw_gen_cover(cx, cy, cs, mview == MV_FOLDERS ? path_base(L->dir) : MV_NAME[mview], "\xE2\x99\xAA", MIN(r, 12));
	int ty = cy + cs + 10;
	text_draw_fit(F_TITLE, tx, ty, tw, L->level == ML_ROOT ? (mview == MV_FOLDERS ? "Music" : "All songs") : path_base(L->dir), g_theme.text, 0);
	ty += font_height(F_TITLE);
	fmt_dur_long(dur, d, sizeof d);
	snfmt(line, sizeof line, "%d song%s \xC2\xB7 %s", n, n == 1 ? "" : "s", d);
	text_draw_fit(F_SMALL, tx, ty, tw, line, g_theme.text_dim, 0);
}

static void draw_mini_player(int px, int y, int pw, int h, const MusicInfo *m) {
	if (glass()) draw_panel(px, y, pw, h, g_theme.radius);
	else gfx_rrect(px, y, pw, h, g_theme.radius, g_theme.surface_hi);
	int ti = lib_find(m->path);
	int cs = h - 16;
	draw_track_cover(ti, px + 8, y + 8, cs, 6);
	int tx = px + cs + 18, tw = pw - cs - 28;
	text_draw_fit(F_SMALL, tx, y + 8, tw, m->title, g_theme.text, 0);
	text_draw_fit(F_SMALL, tx, y + 8 + font_height(F_SMALL), tw, m->paused ? "Paused" : m->artist, m->paused ? g_theme.favorite : g_theme.text_dim, 0);
	int by = y + h - 12;
	gfx_rrect(tx, by, tw, 4, 2, col_alpha(g_theme.text_dim, 70));
	if (m->len > 0) gfx_rrect(tx, by, MAX(4, (int)(tw * CLAMP(m->pos / m->len, 0, 1))), 4, 2, g_theme.accent);
}

/* one row's picture as a square tile (artist, genre, song, playlist, folder ...) */
static void draw_mrow_tile(MRow *r, int x, int y, int size, int radius, int alpha) {
	switch (r->kind) {
	case R_ARTIST: draw_artist_cover(r->idx, x, y, size, radius); break;
	case R_GENRE: draw_genre_tile(r->idx, x, y, size, radius); break;
	case R_TRACK: draw_track_cover(r->idx, x, y, size, radius); break;
	case R_PLAYLIST: draw_playlist_tile(r->idx, x, y, size, radius); break;
	default: {
		const char *nm = r->kind == R_DIR ? path_base(r->path) : r->kind == R_PLAYALL ? "Play all" : r->kind == R_SHUFFLE ? "Shuffle" : "New playlist";
		Image *im = gen_cover_img(nm, size);
		gfx_blit_rounded(im, x, y, radius, 255);
		uint32_t c = 0xF0FFFFFF;
		if (r->kind == R_DIR) draw_folder(x + size / 2 - size / 5, y + size / 2 - size / 6, size / 3, c);
		else if (r->kind == R_PLAYALL) draw_play_icon(x + size / 2 - size / 8, y + size / 2 - size / 6, size / 3, c);
		else if (r->kind == R_SHUFFLE) draw_shuffle_icon(x + size / 2 - size / 6, y + size / 2 - size / 7, size / 3, c);
		else { gfx_rect(x + size / 4, y + size / 2 - 2, size / 2, 5, c); gfx_rect(x + size / 2 - 2, y + size / 4, 5, size / 2, c); }
	}
	}
	(void)alpha;
}

static void mrow_label(MRow *r, char *out, size_t n, char *sub, size_t sn) {
	sub[0] = 0;
	switch (r->kind) {
	case R_ARTIST: sncpy(out, g_lib.a[r->idx].name, n); snfmt(sub, sn, "%d song%s", g_lib.a[r->idx].n, g_lib.a[r->idx].n == 1 ? "" : "s"); break;
	case R_GENRE: sncpy(out, g_lib.g[r->idx].name, n); snfmt(sub, sn, "%d artist%s", g_lib.g[r->idx].n, g_lib.g[r->idx].n == 1 ? "" : "s"); break;
	case R_TRACK: sncpy(out, g_lib.t[r->idx].title, n); sncpy(sub, g_lib.t[r->idx].artist, sn); break;
	case R_PLAYLIST: sncpy(out, pl_title(r->idx), n); snfmt(sub, sn, "%d song%s", r->n, r->n == 1 ? "" : "s"); break;
	case R_DIR: sncpy(out, path_base(r->path), n); sncpy(sub, "Folder", sn); break;
	case R_PLAYALL: sncpy(out, "Play all", n); break;
	case R_SHUFFLE: sncpy(out, "Shuffle", n); break;
	default: sncpy(out, "New playlist", n); break;
	}
}

static void draw_music_grid(int ly, const MusicInfo *m) {
	MLevel *L = mlev();
	int cols = MG_COLS, rows = 2;
	int tw = 112, th = tw + 34, gap = 14;
	int x0 = (SCREEN_W - (cols * tw + (cols - 1) * gap)) / 2, y0 = ly + 6;
	int area_h = CONTENT_Y + CONTENT_H - 40 - y0; /* the status line sits under the grid */
	float off = L->cur.atop * (th + gap);
	gfx_clip(0, ly, SCREEN_W, area_h);
	int first_row = MAX(0, (int)floorf(L->cur.atop) - 1);
	for (int i = first_row * cols; i < nmrows && i < (first_row + rows + 2) * cols; i++) {
		int x = x0 + (i % cols) * (tw + gap);
		int y = (int)(y0 + (i / cols) * (th + gap) - off);
		int sel = i == L->cur.sel;
		if (sel) gfx_shadow(x, y + 3, tw, tw, g_theme.radius, 8, g_theme.shadow);
		draw_mrow_tile(&mrows[i], x, y, tw, g_theme.radius, 255);
		if (sel) gfx_rrect_stroke(x - 3, y - 3, tw + 6, tw + 6, g_theme.radius + 3, 3, sel_ring());
		if (m->active && mrows[i].kind == R_TRACK && !strcmp(m->path, g_lib.t[mrows[i].idx].path)) draw_note(x + tw - 20, y + 8, 16, 0xFFFFFFFF);
		char lab[160], sub[96];
		mrow_label(&mrows[i], lab, sizeof lab, sub, sizeof sub);
		text_draw_fit(F_SMALL, x - 6, y + tw + 6, tw + 12, lab, sel ? g_theme.text : g_theme.text_dim, 1);
	}
	gfx_unclip();
	/* the selected one, in words, at the bottom; the song playing, bottom right */
	if (nmrows) {
		char lab[160], sub[96];
		mrow_label(&mrows[L->cur.sel], lab, sizeof lab, sub, sizeof sub);
		int by = CONTENT_Y + CONTENT_H - 28;
		gfx_rect(PAD, by - 8, SCREEN_W - PAD * 2, 1, col_alpha(g_theme.text_dim, 50));
		int w = m->active ? SCREEN_W / 2 - PAD : SCREEN_W - PAD * 2;
		char line[260];
		snfmt(line, sizeof line, "%s%s%s", lab, sub[0] ? "  \xC2\xB7  " : "", sub);
		text_draw_fit(F_SMALL, PAD + 4, by, w, line, g_theme.text, 0);
		if (m->active) {
			snfmt(line, sizeof line, "%s: %s", m->paused ? "Paused" : "Playing", m->title);
			text_draw_fit(F_SMALL, SCREEN_W / 2 + 8, by, SCREEN_W / 2 - PAD - 8, line, m->paused ? g_theme.favorite : g_theme.accent, 2);
		}
	}
}

static void draw_music_shelf(int ly, const MusicInfo *m) {
	MLevel *L = mlev();
	const int bw = 196, sw = 124, gap = 16;
	int cyc = ly + 10 + bw / 2;
	int lo = MAX(0, (int)floorf(L->cur.asel) - 4), hi = MIN(nmrows - 1, (int)ceilf(L->cur.asel) + 4);
	for (int pass = 4; pass >= 0; pass--)
		for (int i = lo; i <= hi; i++) {
			float off = i - L->cur.asel, ao = fabsf(off);
			if ((int)(ao + 0.5f) != pass) continue;
			int big = ao < 0.5f, w = big ? bw : sw;
			int x = (int)(shelf_x(off, bw, sw, gap) - w / 2.0f), y = cyc - w / 2;
			if (x > SCREEN_W || x + w < 0) continue;
			if (big) gfx_shadow(x, y + 8, w, w, g_theme.radius + 2, 14, col_alpha(0x000000, 110));
			draw_mrow_tile(&mrows[i], x, y, w, g_theme.radius + 2, 255);
			if (!big) gfx_rrect(x, y, w, w, g_theme.radius + 2, col_alpha(0x000000, (int)(60 + fminf(ao - 1, 1.0f) * 60)));
			if (big) gfx_rrect_stroke(x - 4, y - 4, w + 8, w + 8, g_theme.radius + 6, 3, RGBA(255, 255, 255, 235));
		}
	if (!nmrows) return;
	char lab[160], sub[96];
	mrow_label(&mrows[L->cur.sel], lab, sizeof lab, sub, sizeof sub);
	int ty = cyc + bw / 2 + 16;
	text_draw_fit(F_TITLE, PAD + 2, ty + 2, SCREEN_W - PAD * 2, lab, col_alpha(0x000000, 140), 1);
	text_draw_fit(F_TITLE, PAD, ty, SCREEN_W - PAD * 2, lab, 0xFFFFFFFF, 1);
	char line[160];
	snfmt(line, sizeof line, "%s%s%d / %d", sub, sub[0] ? "  \xC2\xB7  " : "", L->cur.sel + 1, nmrows);
	text_draw_fit(F_SMALL, PAD, ty + font_height(F_TITLE) + 1, SCREEN_W - PAD * 2, line, RGBA(230, 226, 240, 255), 1);
	if (m->active) {
		snfmt(line, sizeof line, "%s: %s  \xE2\x80\x94  %s", m->paused ? "Paused" : "Playing", m->title, m->artist);
		text_draw_fit(F_SMALL, PAD, CONTENT_Y + CONTENT_H - 26, SCREEN_W - PAD * 2, line, m->paused ? g_theme.favorite : g_theme.accent, 1);
	}
}

static void draw_music(void) {
	MusicInfo m;
	music_info(&m);
	if (lib_dirty) {
		text_draw_fit(F_BODY, PAD, CONTENT_Y + CONTENT_H / 2 - 20, SCREEN_W - PAD * 2, "Reading your music\xE2\x80\xA6", g_theme.text, 1);
		lib_scan_req = 1;
		return;
	}
	if (mrows_gen != g_lib.gen) mrows_build();
	int rh = mrow_h(), rows = mrows_vis();
	draw_music_header(CONTENT_Y, rh);
	int ly = CONTENT_Y + rh;
	MLevel *L = mlev();
	if (!g_lib.nt) {
		Image *z = zappo_pic("fire-m");
		int ty = ly + 60;
		if (z) { gfx_blit(z, (SCREEN_W - z->w) / 2, ly + 6, 255); ty = ly + 6 + z->h + 12; }
		text_draw_wrap(F_BODY, PAD + 10, ty, SCREEN_W - PAD * 2 - 20, 2, "No music yet", g_theme.text, 1);
		text_draw_wrap(F_SMALL, PAD + 10, ty + 40, SCREEN_W - PAD * 2 - 20, 5,
		               "Copy MP3, FLAC, OGG, WAV or M4A files (or album folders) into the Music or Import folder on your card.",
		               g_theme.text_dim, 1);
		return;
	}
	if (!nmrows) text_draw_fit(F_BODY, PAD + 10, ly + 40, MLW - 20, "Nothing here", g_theme.text_dim, 0);
	if (mlayout() == MLAY_GRID) { draw_music_grid(ly, &m); return; }
	if (mlayout() == MLAY_SHELF) { draw_music_shelf(ly, &m); return; }
	gfx_clip(0, ly, SCREEN_W, rows * rh);
	float hy = ly + (L->cur.asel - L->cur.atop) * rh;
	if (nmrows) draw_sel(PAD, (int)hy + 3, MLW, rh - 6, MIN(g_theme.radius, (rh - 6) / 2));
	for (int i = L->cur.top - 1; i <= L->cur.top + rows && i < nmrows; i++) {
		if (i < 0) continue;
		int y = (int)(ly + (i - L->cur.atop) * rh);
		draw_music_row(&mrows[i], PAD, y + 3, MLW, rh - 6, i == L->cur.sel, &m);
	}
	gfx_unclip();
	/* right side: details, and what's playing */
	int px = PAD + MLW + 16, pw = SCREEN_W - px - PAD;
	int top = CONTENT_Y + rh + 3, bottom = CONTENT_Y + CONTENT_H - 4; /* under the view chips */
	int mini = m.active ? 66 : 0;
	draw_music_info(px, top, pw, bottom - top - (mini ? mini + 8 : 0), &m);
	if (mini) draw_mini_player(px, bottom - mini, pw, mini, &m);
}

/* ------------------------------------------------------------ now playing */

static Image *np_bg;
static unsigned np_gen = 0x40000000u; /* np_bg made again */
static char np_key[1024];

static void draw_now_playing(void) {
	MusicInfo m;
	music_info(&m);
	if (!m.active) { music_np = 0; draw_background(); return; }
	int ti = lib_find(m.path);
	const char *artist = ti >= 0 ? g_lib.t[ti].artist : m.artist;
	/* background: the cover blurred and darkened (or the artist's colours) */
	if (strcmp(np_key, m.path)) {
		int known;
		Image *art = music_art(0, ti, 256, &known);
		if (known) {
			img_free(np_bg);
			np_bg = NULL;
			Image *src = art ? art : gen_cover_img(artist && artist[0] ? artist : "?", 64);
			Image *tiny = img_scale(src, 16, 12);
			np_bg = tiny ? img_scale(tiny, SCREEN_W, SCREEN_H) : NULL;
			np_gen++;
			img_free(tiny);
			if (np_bg)
				for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
					uint32_t c = np_bg->px[i];
					int k = glass() ? 3 : 2; /* Glass: a touch darker, the real screen is bright */
					np_bg->px[i] = RGBA(((c >> 16) & 255) * k / (glass() ? 10 : 5), ((c >> 8) & 255) * k / (glass() ? 10 : 5), (c & 255) * k / (glass() ? 10 : 5), 255);
				}
			sncpy(np_key, m.path, sizeof np_key);
		}
	}
	if (np_bg && layer_mode) layer_background(np_bg->px, np_gen);	/* behind the moving drops */
	else if (np_bg) memcpy(plat_canvas(), np_bg->px, sizeof(uint32_t) * SCREEN_W * SCREEN_H);
	else gfx_vgradient(0, 0, SCREEN_W, SCREEN_H, g_theme.bg, g_theme.bg2);
	/* the cover */
	int cs = 252, cx = 34, cy = 82;
	gfx_shadow(cx, cy + 8, cs, cs, 14, 18, col_alpha(0x000000, 150));
	if (ti >= 0) draw_track_cover(ti, cx, cy, cs, 14);
	else draw_gen_cover(cx, cy, cs, m.title, NULL, 14);
	/* clock, top right */
	char clk[16];
	time_t now = time(NULL) + g_set.clock_offset;
	struct tm tmv;
	localtime_r(&now, &tmv);
	strftime(clk, sizeof clk, g_set.clock_mode == 1 ? "%I:%M" : "%H:%M", &tmv);
	text_draw(F_SMALL, SCREEN_W - PAD - text_width(F_SMALL, clk), 18, clk, 0xFFE6ECEA);
	text_draw(F_SMALL, cx, 26, "NOW PLAYING", col_alpha(g_theme.accent, 255));
	char pos[48];
	if (m.count > 1) {
		snfmt(pos, sizeof pos, "%d of %d", m.index + 1, m.count);
		text_draw(F_SMALL, cx + text_width(F_SMALL, "NOW PLAYING") + 14, 26, pos, 0xB0FFFFFF);
	}
	/* words, right */
	int tx = cx + cs + 26, tw = SCREEN_W - tx - PAD - 4;
	int y = cy + 6;
	Font *big = font_get(34);
	if (!big) big = F_TITLE;
	int lines = text_draw_wrap(big, tx, y, tw, 3, m.title, 0xFFFFFFFF, 0);
	y += lines * font_height(big) + 6;
	text_draw_fit(F_BODY, tx, y, tw, artist && artist[0] ? artist : "Unknown artist", 0xFFE0E8E4, 0);
	y += font_height(F_BODY) + 4;
	if (ti >= 0) {
		Track *t = &g_lib.t[ti];
		char info[300];
		snfmt(info, sizeof info, "%s%s%s", t->genre ? t->genre : "", t->album[0] ? "  \xC2\xB7  " : "", t->album);
		text_draw_wrap(F_SMALL, tx, y, tw, 2, info, col_alpha(g_theme.accent, 255), 0);
	}
	/* progress */
	int by = cy + cs - 56;
	gfx_rrect(tx, by, tw, 6, 3, 0x40FFFFFF);
	if (m.len > 0) gfx_rrect(tx, by, MAX(6, (int)(tw * CLAMP(m.pos / m.len, 0, 1))), 6, 3, g_theme.accent);
	char a[16], b[16];
	fmt_time(m.pos, a, sizeof a);
	text_draw(F_SMALL, tx, by + 12, a, 0xD0FFFFFF);
	if (m.len > 0) {
		fmt_time(m.len, b, sizeof b);
		text_draw(F_SMALL, tx + tw - text_width(F_SMALL, b), by + 12, b, 0xD0FFFFFF);
	}
	static const char *RP[] = {"", "  \xC2\xB7  repeat", "  \xC2\xB7  repeat one"};
	char st[160];
	char slp[48], slp2[64] = "";
	if (sleep_timer_text(slp, sizeof slp)) snfmt(slp2, sizeof slp2, "  \xC2\xB7  pauses %s", slp);
	snfmt(st, sizeof st, "%s%s%s  \xC2\xB7  %d%%%s", m.paused ? "paused" : "playing", m.shuffle ? "  \xC2\xB7  shuffle" : "", RP[m.repeat % 3], g_set.music_volume, slp2);
	int sty = by + 36, hy = by + 70;
	if (glass()) {
		/* round previous / pause / next buttons, as on a phone */
		int bsz = 46, gapb = 18, bx0 = tx;
		for (int i = 0; i < 3; i++) {
			int bxx = bx0 + i * (bsz + gapb), byy = by + 32, big = i == 1;
			int rr = big ? bsz / 2 + 4 : bsz / 2;
			int ccx = bxx + bsz / 2, ccy = byy + bsz / 2;
			gfx_circle(ccx, ccy, rr, big ? 0xFFF4EFE6 : RGBA(255, 255, 255, 56));
			if (!big) gfx_circle(ccx, ccy, rr - 1, RGBA(255, 255, 255, 0)), gfx_rrect_stroke(ccx - rr, ccy - rr, rr * 2, rr * 2, rr, 1, RGBA(255, 255, 255, 90));
			uint32_t ic = big ? 0xFF17151C : 0xFFF4EFE6;
			if (i == 1) {
				if (m.paused) draw_play_icon(ccx - 5, ccy - 9, 18, ic);
				else { gfx_rrect(ccx - 8, ccy - 9, 5, 18, 2, ic); gfx_rrect(ccx + 3, ccy - 9, 5, 18, 2, ic); }
			} else {
				/* a bar and a triangle pointing at it */
				int dir = i == 0 ? -1 : 1;
				gfx_rect(ccx + dir * 8 - 1, ccy - 7, 3, 14, ic);
				for (int k = 0; k < 14; k++) {
					int wdt = (k < 7 ? k : 13 - k) + 1;
					gfx_rect(dir < 0 ? ccx + 7 - wdt : ccx - 7, ccy - 7 + k, wdt, 1, ic);
				}
			}
		}
		sty = by + 92;
		hy = by + 118;
	}
	text_draw_fit(F_SMALL, tx, sty, tw, st, m.paused ? g_theme.favorite : 0xB0FFFFFF, 0);
	/* skipping inside the song */
	static const Hint SK[] = {{"\xE2\x86\x90 \xE2\x86\x92", "10 s"}, {"L1 R1", "1 min"}};
	int hx = tx;
	text_draw(F_SMALL, hx, hy + 12 - font_height(F_SMALL) / 2, "Skip", 0xB0FFFFFF);
	hx += text_width(F_SMALL, "Skip") + 12;
	for (int i = 0; i < 2; i++) {
		draw_hint_button(hx, hy, SK[i].btn);
		int bw = (int)strlen(SK[i].btn) > 1 ? text_width(F_SMALL, SK[i].btn) + 14 : 24;
		text_draw(F_SMALL, hx + bw + 6, hy + 12 - font_height(F_SMALL) / 2, SK[i].label, 0xD0FFFFFF);
		hx += hint_width(&SK[i]) + 20;
	}
}
