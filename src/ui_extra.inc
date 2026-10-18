/* Clove — the on-screen keyboard, Search, the screenshot viewer and the
 * battery estimate (included from ui.c after ui_music.inc). */

/* ------------------------------------------------------- on-screen keyboard
 *
 * Ten keys a row; the last row is Shift, Space, Delete and Done.
 * D-pad moves, A types, Y space, X delete, SELECT shift, START done,
 * B deletes (or closes the keyboard when there's nothing typed). */

static const char *const KB_KEYS[4] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm-&."};
/* passwords: Shift goes on to every other symbol on a keyboard */
static const char *const KB_SYMS[4] = {"!@#$%^&*()", "`~-_=+[]{}", "\\|;:'\",.<>", "/?@#&!-_.*"};
#define KB_COLS 10
#define KB_NROWS 5
static struct {
	int purpose; /* KB_* (0 = closed) */
	char title[96];
	char text[64];
	int row, col;
	int shift, auto_caps;
} kb;

static void search_changed(void);

/* passwords: every symbol, one capital at a time */
static int kb_secret(int purpose) { return purpose == KB_WIFI || purpose == KB_RA_PASS; }

static void kb_open(int purpose, const char *title, const char *initial) {
	memset(&kb, 0, sizeof kb);
	kb.purpose = purpose;
	sncpy(kb.title, title, sizeof kb.title);
	sncpy(kb.text, initial ? initial : "", sizeof kb.text);
	kb.row = 1;
	kb.auto_caps = purpose != KB_SEARCH && !kb_secret(purpose) && purpose != KB_RA_USER; /* names start each word with a capital */
	kb.shift = kb.auto_caps && !kb.text[0];
	dirty = 1;
}

/* the last row's four wide keys, by column */
enum { KK_SHIFT, KK_SPACE, KK_DEL, KK_DONE };
static int kb_wide_key(int col) { return col < 2 ? KK_SHIFT : col < 6 ? KK_SPACE : col < 8 ? KK_DEL : KK_DONE; }

static void kb_append(char c) {
	size_t l = strlen(kb.text);
	if (l + 1 >= sizeof kb.text) return;
	if (kb.shift == 1 && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
	kb.text[l] = c;
	kb.text[l + 1] = 0;
	if (kb.auto_caps) kb.shift = c == ' ';
	else if (kb.shift == 1 && kb_secret(kb.purpose)) kb.shift = 0; /* one capital at a time */
	if (kb.purpose == KB_SEARCH) search_changed();
	dirty = 1;
}

static void kb_delete(void) {
	size_t l = strlen(kb.text);
	if (l) kb.text[l - 1] = 0;
	if (kb.auto_caps) kb.shift = !kb.text[0] || kb.text[strlen(kb.text) - 1] == ' ';
	if (kb.purpose == KB_SEARCH) search_changed();
	dirty = 1;
}

static void kb_finish(void) {
	int p = kb.purpose;
	char text[64];
	sncpy(text, kb.text, sizeof text);
	str_trim(text);
	if (p == KB_SEARCH) return; /* Search handles START itself */
	kb.purpose = KB_NONE;
	dirty = 1;
	if (p == KB_WIFI) {
		if (strlen(kb.text) < 8) { toast("Wi-Fi passwords have at least 8 characters"); kb.purpose = p; return; }
		net_wifi_connect(&wifi_pending, kb.text); /* (as typed: spaces count in a password) */
		return;
	}
#ifdef ZAPPO_KNULLI
	if (p == KB_RA_USER) {
		if (!text[0]) { toast("Type your RetroAchievements name"); kb.purpose = p; return; }
		char t[120];
		sncpy(ra_user_typed, text, sizeof ra_user_typed);
		snfmt(t, sizeof t, "Password for %s", text);
		kb_open(KB_RA_PASS, t, "");
		return;
	}
	if (p == KB_RA_PASS) {
		if (!kb.text[0]) { toast("Type the password"); kb.purpose = p; return; }
		if (net_busy()) { toast("One moment: %s", net_job()); kb.purpose = p; return; }
		sncpy(ra_pass_typed, kb.text, sizeof ra_pass_typed);
		memset(kb.text, 0, sizeof kb.text);
		ra_checking = 1;
		net_ra_signin(ra_user_typed, ra_pass_typed);
		toast("Signing in to RetroAchievements...");
		return;
	}
#endif
	if (!text[0]) { toast("Type a name first"); kb.purpose = p; return; }
	if (p == KB_NEWPL || p == KB_NEWPL_ADD) music_named_playlist(p == KB_NEWPL_ADD, text);
}

/* returns 1 when the key moved off the top row (Search: up into the results) */
static int kb_button(int b) {
	switch (b) {
	case B_UP:
		if (kb.row == 0) return 1;
		kb.row--;
		if (kb.row == 3 && kb.col >= KB_COLS) kb.col = KB_COLS - 1;
		break;
	case B_DOWN: kb.row = (kb.row + 1) % KB_NROWS; break;
	case B_LEFT:
		if (kb.row == 4) { int k = kb_wide_key(kb.col); kb.col = k == KK_SHIFT ? 8 : k == KK_SPACE ? 0 : k == KK_DEL ? 2 : 6; }
		else kb.col = (kb.col + KB_COLS - 1) % KB_COLS;
		break;
	case B_RIGHT:
		if (kb.row == 4) { int k = kb_wide_key(kb.col); kb.col = k == KK_SHIFT ? 2 : k == KK_SPACE ? 6 : k == KK_DEL ? 8 : 0; }
		else kb.col = (kb.col + 1) % KB_COLS;
		break;
	case B_A:
		if (kb.row < 4) kb_append(kb.shift == 2 ? KB_SYMS[kb.row][kb.col] : KB_KEYS[kb.row][kb.col]);
		else switch (kb_wide_key(kb.col)) {
			case KK_SHIFT: kb.shift = kb_secret(kb.purpose) ? (kb.shift + 1) % 3 : !kb.shift; break;
			case KK_SPACE: if (kb.text[0]) kb_append(' '); break;
			case KK_DEL: kb_delete(); break;
			case KK_DONE: kb_finish(); break;
			}
		break;
	case B_Y: if (kb.text[0]) kb_append(' '); break;
	case B_X: kb_delete(); break;
	case B_SELECT: kb.shift = kb_secret(kb.purpose) ? (kb.shift + 1) % 3 : !kb.shift; break;
	case B_START: kb_finish(); break;
	case B_B:
		if (kb.text[0]) kb_delete();
		else if (kb.purpose != KB_SEARCH) kb.purpose = KB_NONE; /* cancel */
		else return 2; /* Search: leave */
		break;
	}
	dirty = 1;
	return 0;
}

static void draw_kb(int x, int y, int w, int kh, int focused) {
	int gap = 5;
	int kw = (w - gap * (KB_COLS - 1)) / KB_COLS;
	for (int r = 0; r < KB_NROWS; r++) {
		int ky = y + r * (kh + gap);
		if (r < 4) {
			for (int c = 0; c < KB_COLS; c++) {
				int kx = x + c * (kw + gap);
				int sel = focused && kb.row == r && kb.col == c;
				gfx_rrect(kx, ky, kw, kh, 8, sel ? g_theme.accent : g_theme.surface_hi);
				char ch[2] = {kb.shift == 2 ? KB_SYMS[r][c] : KB_KEYS[r][c], 0};
				if (kb.shift == 1 && ch[0] >= 'a' && ch[0] <= 'z') ch[0] = (char)(ch[0] - 'a' + 'A');
				text_draw(F_BODY, kx + (kw - text_width(F_BODY, ch)) / 2, ky + kh / 2 - font_height(F_BODY) / 2, ch, sel ? g_theme.accent_text : g_theme.text);
			}
		} else {
			const char *L[] = {kb.shift == 2 ? "abc" : kb_secret(kb.purpose) && kb.shift == 1 ? "Symbols" : "Shift", "Space", "Delete", "Done"};
			static const int C0[] = {0, 2, 6, 8}, CN[] = {2, 4, 2, 2};
			for (int k = 0; k < 4; k++) {
				int kx = x + C0[k] * (kw + gap), ww = CN[k] * kw + (CN[k] - 1) * gap;
				int sel = focused && kb.row == 4 && kb_wide_key(kb.col) == k;
				uint32_t bg = sel ? g_theme.accent : (k == KK_SHIFT && kb.shift) ? col_alpha(g_theme.accent, 110) : g_theme.surface_hi;
				gfx_rrect(kx, ky, ww, kh, 8, bg);
				text_draw(F_SMALL, kx + (ww - text_width(F_SMALL, L[k])) / 2, ky + kh / 2 - font_height(F_SMALL) / 2, L[k],
				          sel ? g_theme.accent_text : g_theme.text);
			}
		}
	}
}

static void draw_kb_field(int x, int y, int w, int h, const char *placeholder) {
	gfx_rrect(x, y, w, h, MIN(g_theme.radius, h / 2), g_theme.surface);
	gfx_rrect_stroke(x, y, w, h, MIN(g_theme.radius, h / 2), 2, col_alpha(g_theme.accent, 160));
	int tx = x + 14, ty = y + h / 2 - font_height(F_BODY) / 2;
	if (kb.text[0]) {
		/* the end of the text stays in view */
		int tw = text_width(F_BODY, kb.text), room = w - 34;
		gfx_clip(x + 6, y, w - 12, h);
		int sx = tw > room ? tx - (tw - room) : tx;
		text_draw(F_BODY, sx, ty, kb.text, g_theme.text);
		gfx_rect(sx + tw + 2, y + 9, 2, h - 18, g_theme.accent);
		gfx_unclip();
	} else {
		gfx_rect(tx, y + 9, 2, h - 18, g_theme.accent);
		text_draw_fit(F_BODY, tx + 8, ty, w - 40, placeholder, col_alpha(g_theme.text_dim, 200), 0);
	}
}

/* the keyboard as a box over the current screen (naming a playlist) */
static void draw_kb_modal(void) {
	if (!kb.purpose || kb.purpose == KB_SEARCH) return;
	gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_alpha(0, 160));
	int w = SCREEN_W - 40, x = 20, kh = 34;
	int h = 18 + font_height(F_BODY) + 10 + 44 + 14 + KB_NROWS * (kh + 5) + 10;
	int y = (SCREEN_H - GUIDE_H - h) / 2 + 6;
	gfx_shadow(x, y + 6, w, h, g_theme.radius + 4, 14, g_theme.shadow);
	gfx_rrect(x, y, w, h, g_theme.radius + 4, solid_surface());
	text_draw_fit(F_BODY, x + 20, y + 16, w - 40, kb.title, g_theme.text, 0);
	int fy = y + 18 + font_height(F_BODY) + 8;
	draw_kb_field(x + 16, fy, w - 32, 44, kb_secret(kb.purpose) ? "Password" : "Name");
	draw_kb(x + 16, fy + 44 + 14, w - 32, kh, 1);
	static Hint H[] = {{"Y", "Space"}, {"X", "Delete"}, {"START", "Done"}, {"B", "Cancel"}};
	H[3].btn = back_btn();
	H[3].label = kb.text[0] ? "Delete" : "Cancel";
	/* the normal guide was drawn under the dimming: draw ours on top */
	gfx_rect(0, SCREEN_H - GUIDE_H, SCREEN_W, GUIDE_H, col_alpha(g_theme.bg2, 230));
	int sg = g_set.show_guide;
	g_set.show_guide = 1;
	draw_guide(H, ARRAY_LEN(H));
	g_set.show_guide = sg;
}

/* ------------------------------------------------------------------ search */

enum { SK_GAME, SK_ARTIST, SK_SONG, SK_PLAYLIST };
typedef struct {
	int kind, sys, idx;
	char *name, *low, *path, *sub;
} SItem;
static SItem *sidx;      /* everything searchable */
static int nsidx, csidx;
static int *sres;        /* matches, as indexes into sidx */
static int nsres;
static int s_focus;      /* 0 keyboard, 1 results */
static Cursor s_cur;

static void s_add(int kind, int sys, int idx, const char *name, const char *path, const char *sub) {
	if (nsidx == csidx) { csidx = csidx ? csidx * 2 : 256; sidx = xrealloc(sidx, sizeof *sidx * (size_t)csidx); }
	SItem *s = &sidx[nsidx++];
	s->kind = kind;
	s->sys = sys;
	s->idx = idx;
	s->name = xstrdup(name);
	s->low = xstrdup(name);
	str_lower(s->low);
	s->path = path ? xstrdup(path) : NULL;
	s->sub = sub ? xstrdup(sub) : NULL;
}

static void s_free(void) {
	for (int i = 0; i < nsidx; i++) { free(sidx[i].name); free(sidx[i].low); free(sidx[i].path); free(sidx[i].sub); }
	nsidx = 0;
	free(sres);
	sres = NULL;
	nsres = 0;
}

static void s_add_games(int sys, const char *dir, int depth_left) {
	EntryList l = {0};
	data_list_system(sys, dir, &l);
	for (int i = 0; i < l.n; i++) {
		Entry *e = &l.v[i];
		if (e->is_dir) { if (depth_left > 0) s_add_games(sys, e->path, depth_left - 1); continue; }
		s_add(SK_GAME, sys, 0, e->name, e->path, g_sys[sys].short_name);
	}
	el_clear(&l);
}

static void s_build(void) {
	s_free();
	int v[256];
	int n = data_visible_systems(v, ARRAY_LEN(v));
	for (int i = 0; i < n; i++) {
		if (g_sys[v[i]].is_import) continue;
		s_add_games(v[i], "", 2);
	}
	if (lib_dirty) { lib_scan(); lib_dirty = 0; mart_clear(); }
	for (int i = 0; i < g_lib.na; i++) {
		char sub[64];
		snfmt(sub, sizeof sub, "%d song%s", g_lib.a[i].n, g_lib.a[i].n == 1 ? "" : "s");
		s_add(SK_ARTIST, -1, i, g_lib.a[i].name, NULL, sub);
	}
	for (int i = 0; i < g_lib.nt; i++) s_add(SK_SONG, -1, i, g_lib.t[i].title, g_lib.t[i].path, g_lib.t[i].artist);
	pl_load();
	for (int i = 0; i < pl_count(); i++) s_add(SK_PLAYLIST, -1, i, pl_get(i)->name, NULL, "Playlist");
	LOG("search: %d things to look through", nsidx);
}

/* how well an item matches: -1 no, 0 starts with it, 1 a word starts with it, 2 inside */
static int s_score(const SItem *s, char words[][32], int nw, const char *whole) {
	for (int w = 0; w < nw; w++) {
		if (!strstr(s->low, words[w])) {
			/* songs also match on the artist's name */
			if (s->kind == SK_SONG && s->sub) {
				char a[160];
				sncpy(a, s->sub, sizeof a);
				str_lower(a);
				if (strstr(a, words[w])) continue;
			}
			return -1;
		}
	}
	if (str_starts_with(s->low, whole)) return 0;
	const char *p = strstr(s->low, words[0]);
	if (p && (p == s->low || p[-1] == ' ' || p[-1] == '(' || p[-1] == '-')) return 1;
	return 2;
}

typedef struct { int i, score; } SRank;
static int cmp_srank(const void *a, const void *b) {
	const SRank *x = a, *y = b;
	if (x->score != y->score) return x->score - y->score;
	const SItem *p = &sidx[x->i], *q = &sidx[y->i];
	if (p->kind != q->kind) return p->kind - q->kind;
	return str_icmp_natural(p->name, q->name);
}

static void search_changed(void) {
	free(sres);
	sres = NULL;
	nsres = 0;
	s_cur.sel = s_cur.top = 0;
	s_cur.asel = s_cur.atop = 0;
	char q[64];
	sncpy(q, kb.text, sizeof q);
	str_trim(q);
	str_lower(q);
	if (!q[0]) return;
	char words[8][32];
	int nw = 0;
	char tmp[64];
	sncpy(tmp, q, sizeof tmp);
	for (char *t = strtok(tmp, " "); t && nw < 8; t = strtok(NULL, " ")) sncpy(words[nw++], t, sizeof words[0]);
	if (!nw) return;
	SRank *r = xmalloc(sizeof(SRank) * (size_t)MAX(nsidx, 1));
	int nr = 0;
	for (int i = 0; i < nsidx; i++) {
		int sc = s_score(&sidx[i], words, nw, q);
		if (sc >= 0) r[nr++] = (SRank){i, sc};
	}
	if ((nr) > 1) qsort(r, (size_t)nr, sizeof *r, cmp_srank);
	nsres = MIN(nr, 200);
	sres = xmalloc(sizeof(int) * (size_t)MAX(nsres, 1));
	for (int i = 0; i < nsres; i++) sres[i] = r[i].i;
	free(r);
}

static void search_open(void) {
	if (depth >= ARRAY_LEN(stack)) return;
	View *v = &stack[depth++];
	memset(v, 0, sizeof *v);
	v->type = V_SEARCH;
	s_build();
	kb_open(KB_SEARCH, "Search", "");
	s_focus = 0;
	memset(&s_cur, 0, sizeof s_cur);
	search_changed();
	dirty = 1;
}

static void search_close(void) {
	kb.purpose = KB_NONE;
	s_free();
	pop_view();
}

static int s_rows(void);

static void search_activate(void) {
	if (s_cur.sel >= nsres) return;
	SItem *s = &sidx[sres[s_cur.sel]];
	if (s->kind == SK_GAME) {
		Entry e = {s->name, s->path, s->sys, 0, 0};
		launch_entry(&e, LK_SYSTEM);
		return;
	}
	int kind = s->kind, idx = s->idx;
	char path[1024];
	sncpy(path, s->path ? s->path : "", sizeof path);
	search_close();
	while (depth > 1) pop_view();
	enter_tab(TAB_MUSIC);
	if (kind == SK_ARTIST) music_open_at(MV_ARTISTS, ML_ARTIST, idx);
	else if (kind == SK_PLAYLIST) music_open_at(MV_PLAYLISTS, ML_PLAYLIST, idx);
	else if (kind == SK_SONG) {
		int ti = lib_find(path);
		if (ti < 0) return;
		MArtist *a = &g_lib.a[g_lib.t[ti].artist_i];
		music_open_at(MV_ARTISTS, ML_ARTIST, g_lib.t[ti].artist_i);
		play_tracks(a->t, a->n, ti, 0);
		music_np = 1;
	}
}

static void search_button(int b) {
	if (s_focus == 1) {
		int rows = s_rows();
		switch (b) {
		case B_UP:
			if (s_cur.sel == 0) { s_focus = 0; kb.row = 0; clamp_cursor(&s_cur, nsres, s_rows(), 1); dirty = 1; break; }
			move_cursor(&s_cur, nsres, -1, rows, 1, 0);
			break;
		case B_DOWN:
			if (s_cur.sel >= nsres - 1) { s_focus = 0; kb.row = 0; clamp_cursor(&s_cur, nsres, s_rows(), 1); dirty = 1; break; }
			move_cursor(&s_cur, nsres, 1, rows, 1, 0);
			break;
		case B_L1: case B_LEFT: move_cursor(&s_cur, nsres, -rows, rows, 1, 0); break;
		case B_R1: case B_RIGHT: move_cursor(&s_cur, nsres, rows, rows, 1, 0); break;
		case B_A: search_activate(); break;
		case B_B: case B_START: s_focus = 0; clamp_cursor(&s_cur, nsres, s_rows(), 1); dirty = 1; break;
		case B_Y: case B_X: s_focus = 0; clamp_cursor(&s_cur, nsres, s_rows(), 1); kb_button(b); break;
		}
		return;
	}
	if (b == B_START) { if (nsres) { s_focus = 1; s_cur.sel = 0; dirty = 1; } return; }
	if (b == B_UP && kb.row == 0) {
		/* up from the keyboard goes into the results, starting with the nearest one */
		if (nsres) { s_focus = 1; s_cur.sel = MIN(nsres - 1, s_cur.top + s_rows() - 1); clamp_cursor(&s_cur, nsres, s_rows(), 1); dirty = 1; }
		return;
	}
	if (kb_button(b) == 2) search_close();
}

#define S_FIELD_Y (CONTENT_Y + 2)
#define S_KEY_H 28
static int s_kb_y(void) { return SCREEN_H - GUIDE_H - KB_NROWS * (S_KEY_H + 5) - 2; }
static int s_list_y(void) { return S_FIELD_Y + 44 + 8; }
static int s_row_h(void) { return 34; }
/* typing: a few results above the keyboard; browsing them: the keyboard folds away */
static int s_rows(void) { return MAX(1, ((s_focus == 1 ? SCREEN_H - GUIDE_H : s_kb_y() - 6) - s_list_y()) / s_row_h()); }

static void draw_search(void) {
	draw_kb_field(PAD, S_FIELD_Y, SCREEN_W - PAD * 2, 44, "Games, artists, songs, playlists");
	int ly = s_list_y(), rh = s_row_h(), rows = s_rows();
	int lw = SCREEN_W - PAD * 2;
	if (!kb.text[0]) {
		text_draw_wrap(F_SMALL, PAD + 8, ly + 10, lw - 16, 3, "Type a few letters of a game, artist, song or playlist. Press START (or up) to go to the results.",
		               g_theme.text_dim, 0);
	} else if (!nsres) {
		char t[128];
		snfmt(t, sizeof t, "Nothing called \xE2\x80\x9C%s\xE2\x80\x9D", kb.text);
		text_draw_fit(F_BODY, PAD + 8, ly + 10, lw - 16, t, g_theme.text_dim, 0);
	} else {
		clamp_cursor(&s_cur, nsres, rows, 1);
		gfx_clip(0, ly, SCREEN_W, rows * rh);
		if (s_focus == 1) {
			float hy = ly + (s_cur.asel - s_cur.atop) * rh;
			draw_sel(PAD, (int)hy + 2, lw, rh - 4, MIN(g_theme.radius, (rh - 4) / 2));
		}
		for (int i = s_cur.top - 1; i <= s_cur.top + rows && i < nsres; i++) {
			if (i < 0) continue;
			SItem *s = &sidx[sres[i]];
			int y = (int)(ly + (i - s_cur.atop) * rh);
			int sel = s_focus == 1 && i == s_cur.sel;
			uint32_t tc = sel ? sel_fg() : g_theme.text, dc = sel ? sel_dim() : g_theme.text_dim;
			/* what it is, as a small tag */
			static const char *KIND[] = {"", "Artist", "Song", "Playlist"};
			const char *tag = s->kind == SK_GAME ? g_sys[s->sys].short_name : KIND[s->kind];
			int tgw = text_width(F_SMALL, tag) + 16, th = 24;
			int tx = PAD + 10;
			gfx_rrect(tx, y + rh / 2 - th / 2, tgw, th, th / 2, sel ? col_alpha(sel_fg(), 40) : g_theme.surface_hi);
			text_draw(F_SMALL, tx + 8, y + rh / 2 - font_height(F_SMALL) / 2, tag, sel ? sel_fg() : g_theme.text_dim);
			int nx = tx + MAX(tgw, 74) + 10;
			int right_w = 0;
			if (s->kind == SK_SONG && s->sub) {
				right_w = MIN(text_width(F_SMALL, s->sub), 190);
				text_draw_fit(F_SMALL, SCREEN_W - PAD - 12 - right_w, y + rh / 2 - font_height(F_SMALL) / 2, right_w, s->sub, dc, 2);
				right_w += 14;
			}
			text_draw_fit(F_BODY, nx, y + rh / 2 - font_height(F_BODY) / 2, SCREEN_W - PAD - 12 - right_w - nx, s->name, tc, 0);
		}
		gfx_unclip();
		if (nsres > rows) {
			char c[32];
			snfmt(c, sizeof c, "%d found", nsres);
			text_draw(F_SMALL, SCREEN_W - PAD - text_width(F_SMALL, c) - 4, S_FIELD_Y + 22 - font_height(F_SMALL) / 2, c, g_theme.text_dim);
		}
	}
	if (s_focus == 0) draw_kb(PAD, s_kb_y(), SCREEN_W - PAD * 2, S_KEY_H, 1);
}

static int search_hints(Hint *h) {
	int n = 0;
	if (s_focus == 1) {
		h[n++] = (Hint){back_btn(), "Keyboard"};
		h[n++] = (Hint){confirm_btn(), nsres && sidx[sres[s_cur.sel]].kind == SK_GAME ? "Play" : "Open"};
	} else {
		h[n++] = (Hint){"Y", "Space"};
		h[n++] = (Hint){"X", "Delete"};
		if (nsres) h[n++] = (Hint){"START", "Results"};
		h[n++] = (Hint){back_btn(), kb.text[0] ? "Delete" : "Back"};
	}
	return n;
}

/* ------------------------------------------------------- screenshot viewer */

typedef struct { char *path; time_t when; } Shot;
static Shot *shots;
static int nshots, cshots, shot_i;
static Image *shot_img;
static int shot_img_i = -1;
static uint64_t shot_info_until;

static void shots_scan_dir(const char *dir) {
	DIR *d = opendir(dir);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		const char *x = path_ext(e->d_name);
		if (!str_ieq(x, "png") && !str_ieq(x, "jpg") && !str_ieq(x, "jpeg") && !str_ieq(x, "bmp")) continue;
		char p[1024];
		snfmt(p, sizeof p, "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		if (nshots == cshots) { cshots = cshots ? cshots * 2 : 64; shots = xrealloc(shots, sizeof *shots * (size_t)cshots); }
		shots[nshots].path = xstrdup(p);
		shots[nshots].when = st.st_mtime;
		nshots++;
	}
	if (d) closedir(d);
}

static int cmp_shot(const void *a, const void *b) {
	const Shot *x = a, *y = b;
	if (x->when != y->when) return x->when > y->when ? -1 : 1;
	return strcmp(y->path, x->path);
}

static void shots_free(void) {
	for (int i = 0; i < nshots; i++) free(shots[i].path);
	nshots = 0;
	img_free(shot_img);
	shot_img = NULL;
	shot_img_i = -1;
}

static void shots_open(void) {
	shots_free();
	char d[600];
	snfmt(d, sizeof d, "%s/Screenshots", g_paths.mmc);
	shots_scan_dir(d);
	if (g_paths.sd_present && strcmp(g_paths.sd, g_paths.mmc)) {
		snfmt(d, sizeof d, "%s/Screenshots", g_paths.sd);
		shots_scan_dir(d);
	}
	if (!nshots) { toast("No screenshots yet. In a game, hold MENU and press up to take one."); return; }
	if ((nshots) > 1) qsort(shots, (size_t)nshots, sizeof *shots, cmp_shot);
	if (depth >= ARRAY_LEN(stack)) return;
	View *v = &stack[depth++];
	memset(v, 0, sizeof *v);
	v->type = V_SHOTS;
	shot_i = 0;
	shot_info_until = plat_ticks() + 3000;
	dirty = 1;
}

/* "Tobu Tobu Girl-241012-183005.png" -> "Tobu Tobu Girl" (RetroArch's names) */
static void shot_title(const char *path, char *out, size_t n) {
	path_stem(path, out, n);
	size_t l = strlen(out);
	if (l > 14 && out[l - 14] == '-' && out[l - 7] == '-') {
		int ok = 1;
		for (size_t i = l - 13; i < l; i++) if (i != l - 7 && (out[i] < '0' || out[i] > '9')) ok = 0;
		if (ok) out[l - 14] = 0;
	}
}

static void shots_button(int b) {
	int old = shot_i;
	switch (b) {
	case B_LEFT: case B_UP: if (shot_i > 0) shot_i--; break;
	case B_RIGHT: case B_DOWN: if (shot_i < nshots - 1) shot_i++; break;
	case B_L1: shot_i = MAX(0, shot_i - 10); break;
	case B_R1: shot_i = MIN(nshots - 1, shot_i + 10); break;
	case B_A: case B_X: case B_Y:
		shot_info_until = plat_ticks() < shot_info_until ? 0 : plat_ticks() + 600000;
		dirty = 1;
		return;
	case B_B: shots_free(); pop_view(); return;
	}
	if (shot_i != old) { shot_info_until = MAX(shot_info_until, plat_ticks() + 2500); dirty = 1; }
}

static void draw_shots(void) {
	gfx_clear(0xFF000000);
	if (!nshots) return;
	if (shot_img_i != shot_i) {
		img_free(shot_img);
		shot_img = NULL;
		Image *src = img_load(shots[shot_i].path);
		if (src) {
			shot_img = (src->w == SCREEN_W && src->h == SCREEN_H) ? src : img_fit(src, SCREEN_W, SCREEN_H);
			if (shot_img != src) img_free(src);
		}
		shot_img_i = shot_i;
	}
	if (shot_img) gfx_blit(shot_img, (SCREEN_W - shot_img->w) / 2, (SCREEN_H - shot_img->h) / 2, 255);
	else text_draw_fit(F_BODY, 40, SCREEN_H / 2 - 20, SCREEN_W - 80, "Couldn't open this picture", 0xFFC0C0C0, 1);
	if (plat_ticks() >= shot_info_until) return;
	int bh = 74, by = SCREEN_H - bh;
	gfx_vgradient(0, by - 30, SCREEN_W, 30, 0x00000000, 0xA0000000);
	gfx_rect(0, by, SCREEN_W, bh, 0xA0000000);
	char t[256], d[64], c[32];
	shot_title(shots[shot_i].path, t, sizeof t);
	time_t w = shots[shot_i].when + g_set.clock_offset;
	struct tm tmv;
	localtime_r(&w, &tmv);
	if (tmv.tm_year + 1900 >= 2024) strftime(d, sizeof d, "%e %B %Y, %H:%M", &tmv);
	else d[0] = 0;
	snfmt(c, sizeof c, "%d of %d", shot_i + 1, nshots);
	int cw = text_width(F_SMALL, c);
	text_draw_fit(F_BODY, PAD + 4, by + 10, SCREEN_W - PAD * 2 - cw - 20, t, 0xFFF4EFE6, 0);
	text_draw(F_SMALL, SCREEN_W - PAD - cw, by + 14, c, 0xFFC9C3D3);
	text_draw_fit(F_SMALL, PAD + 4, by + 12 + font_height(F_BODY), 300, d[0] == ' ' ? d + 1 : d, 0xFFC9C3D3, 0);
	static const char *G = "\xE2\x86\x90 \xE2\x86\x92  Browse    A  Hide    B  Back";
	text_draw(F_SMALL, SCREEN_W - PAD - text_width(F_SMALL, G), by + 12 + font_height(F_BODY), G, 0xFFC9C3D3);
}

/* --------------------------------------------------------- battery estimate
 *
 * Clove notes the battery level now and then (CFW/clove/battery-log.tsv, with
 * the time since the handheld was switched on), works out how fast it has
 * been going down since the last charge, and from that how long is left.
 * Games keep the clock running, so their drain is included. */

#define BATT_DEFAULT_RATE 16.0f /* % an hour, until there's a measurement */
static float batt_rate = -1;    /* measured % an hour (-1: none yet; declared in ui.c) */
#define BATT_DEFAULT_CRATE 40.0f /* % an hour while charging, until measured */
static float batt_crate = -1;   /* measured charging rate, % an hour */
static int batt_measured;
static uint64_t batt_checked;
static int batt_logged_pct = -2, batt_logged_chg = -1;
static double batt_logged_up;

static double uptime_s(void) {
	/* (read directly: /proc files report a size of 0) */
	double u = -1;
	FILE *f = fopen("/proc/uptime", "r");
	if (f) {
		if (fscanf(f, "%lf", &u) != 1) u = -1;
		fclose(f);
	}
	return u >= 0 ? u : (double)plat_ticks() / 1000.0;
}

static void batt_files(char *log, char *rate, size_t n) {
	snfmt(log, n, "%s/battery-log.tsv", g_paths.clove);
	snfmt(rate, n, "%s/battery-rate.txt", g_paths.clove);
}

static void batt_estimate(void) {
	char lp[600], rp[600];
	batt_files(lp, rp, sizeof lp);
	char *rt = file_read_all(rp, NULL);
	float saved = rt ? (float)atof(rt) : -1;
	free(rt);
	batt_rate = saved > 0.5f && saved < 80 ? saved : -1;
	batt_measured = batt_rate > 0;
	char *txt = file_read_all(lp, NULL);
	if (!txt) return;
	enum { MAXS = 400 };
	static double up[MAXS];
	static int pc[MAXS], ch[MAXS];
	int n = 0;
	for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
		double u;
		long ep;
		int p, c;
		if (sscanf(line, "%lf\t%ld\t%d\t%d", &u, &ep, &p, &c) != 4) continue;
		if (n == MAXS) { memmove(up, up + 1, sizeof up[0] * (MAXS - 1)); memmove(pc, pc + 1, sizeof pc[0] * (MAXS - 1)); memmove(ch, ch + 1, sizeof ch[0] * (MAXS - 1)); n--; }
		up[n] = u; pc[n] = p; ch[n] = c; n++;
	}
	free(txt);
	if (n < 2) return;
	if (ch[n - 1]) {
		/* on the charger: how fast it has been filling since it was plugged in */
		int i = n - 1;
		while (i > 0 && ch[i - 1] && up[i - 1] < up[i] && pc[i - 1] <= pc[i] && up[n - 1] - up[i - 1] < 4 * 3600) i--;
		double span = up[n - 1] - up[i];
		int rise = pc[n - 1] - pc[i];
		if (span >= 10 * 60 && rise >= 2) {
			float r = (float)(rise / (span / 3600.0));
			if (r > 3 && r < 200) batt_crate = r;
		}
		return;
	}
	/* back from the newest reading while it's the same switch-on, not charging,
	 * and within the last four hours */
	int i = n - 1;
	while (i > 0 && !ch[i - 1] && up[i - 1] < up[i] && pc[i - 1] >= pc[i] - 1 && up[n - 1] - up[i - 1] < 4 * 3600) i--;
	double span = up[n - 1] - up[i];
	int drop = pc[i] - pc[n - 1];
	if (span >= 20 * 60 && drop >= 3) {
		float r = (float)(drop / (span / 3600.0));
		if (r > 0.5f && r < 80) {
			batt_rate = saved > 0 ? r * 0.7f + saved * 0.3f : r;
			batt_measured = 1;
			if (span >= 30 * 60 && drop >= 5) {
				char b[32];
				int l = snfmt(b, sizeof b, "%.2f\n", batt_rate);
				file_write_all(rp, b, (size_t)l);
			}
		}
	}
}

/* adds a reading when the level changed, charging started or stopped, or every 10 min */
static void batt_sample(int force) {
	int pct = plat_battery();
	if (pct < 0) return;
	int chg = plat_charging();
	double u = uptime_s();
	if (!force && pct == batt_logged_pct && chg == batt_logged_chg && u - batt_logged_up < 600) return;
	char lp[600], rp[600];
	batt_files(lp, rp, sizeof lp);
	FILE *f = fopen(lp, "a");
	if (f) {
		fprintf(f, "%.0f\t%ld\t%d\t%d\n", u, (long)time(NULL), pct, chg);
		fclose(f);
	}
	batt_logged_pct = pct;
	batt_logged_chg = chg;
	batt_logged_up = u;
	batt_estimate();
}

/* keeps the log short: the newest 200 readings */
static void batt_trim(void) {
	char lp[600], rp[600];
	batt_files(lp, rp, sizeof lp);
	size_t len;
	char *txt = file_read_all(lp, &len);
	if (!txt) return;
	int lines = 0;
	for (size_t i = 0; i < len; i++) if (txt[i] == '\n') lines++;
	if (lines > 400) {
		int skip = lines - 200;
		char *p = txt;
		while (skip > 0 && (p = strchr(p, '\n'))) { p++; skip--; }
		if (p) file_write_all(lp, p, strlen(p));
	}
	free(txt);
}

/* "about 4 h 20 min left", "charging", or "" (unknown / switched off) */
static int batt_left_text(char *out, size_t n) {
	out[0] = 0;
	if (!g_set.battery_time) return 0;
	int pct = plat_battery();
	if (pct < 0) return 0;
	if (plat_charging()) {
		if (pct >= 100) { sncpy(out, "charged", n); return 1; }
		float cr = batt_crate > 0 ? batt_crate : BATT_DEFAULT_CRATE;
		int cm = (int)((100 - pct) / cr * 60.0f);
		if (cm >= 60) { cm = (cm + 5) / 10 * 10; if (cm % 60) snfmt(out, n, "charging \xC2\xB7 about %d h %d min to full", cm / 60, cm % 60); else snfmt(out, n, "charging \xC2\xB7 about %d h to full", cm / 60); }
		else snfmt(out, n, "charging \xC2\xB7 about %d min to full", MAX(5, (cm + 2) / 5 * 5));
		return 1;
	}
	float rate = batt_rate > 0 ? batt_rate : BATT_DEFAULT_RATE;
	int mins = (int)(pct / rate * 60.0f);
	if (mins >= 60) {
		mins = (mins + 5) / 10 * 10;
		if (mins % 60) snfmt(out, n, "about %d h %d min left", mins / 60, mins % 60);
		else snfmt(out, n, "about %d h left", mins / 60);
	} else {
		snfmt(out, n, "about %d min left", MAX(5, (mins + 2) / 5 * 5));
	}
	return 1;
}

/* the warning at 15% and 5%, once each per switch-on (the note lives in /tmp) */
static void batt_warn(void) {
	int pct = plat_battery();
	if (pct < 0) return;
	const char *wf = "/tmp/clove-battery-warned";
	char *t = file_read_all(wf, NULL);
	int warned = t ? atoi(t) : 100;
	free(t);
	if (plat_charging()) {
		if (warned != 100) file_write_all(wf, "100\n", 4);
		return;
	}
	int level = pct <= 5 ? 5 : pct <= 15 ? 15 : 100;
	if (level >= warned || level == 100) return;
	char b[8];
	int l = snfmt(b, sizeof b, "%d\n", level);
	file_write_all(wf, b, (size_t)l);
	char left[64], msg[160];
	batt_left_text(left, sizeof left);
	if (level == 5) snfmt(msg, sizeof msg, "Battery %d%%%s%s. Save your game and charge soon.", pct, left[0] ? ": " : "", left);
	else snfmt(msg, sizeof msg, "Battery low: %d%%%s%s", pct, left[0] ? ", " : "", left);
	ui_notice(msg);
}

static void batt_update(uint64_t now) {
	if (batt_checked && now - batt_checked < 60000) return;
	if (!batt_checked) { batt_trim(); batt_sample(1); }
	else batt_sample(0);
	batt_checked = now;
	batt_warn();
}
