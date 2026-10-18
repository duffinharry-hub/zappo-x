/* The Videos tab: films and shows from Roms/VIDEOS on either card, as a wall
 * of pictures (the Imgs folder's thumbnails, made by the PC kit) or a plain
 * list. Pressing A opens the video player; anything opened is remembered as
 * watched. Included from ui.c after list_button(). */

/* v_video (declared in ui.c): kind LK_VIDEO, the top of Roms/VIDEOS */
/* (declared in ui.c) */

/* per-entry details for the view being shown: running time (read once) and watched */
typedef struct { int dur; uint8_t watched, dur_known; time_t added, last; } VMeta;
static VMeta *vmeta;
static int nvmeta;
static const Entry *vmeta_for; /* the items array the metas belong to */

static void video_import_watched(void);
#define VW_COLS 3
#define VW_ROWS 2
#define VW_TW 188
#define VW_TH 106
#define VW_PITCH (VW_TH + 50)

static void video_meta_bind(View *v) {
	if (vmeta_for == v->items.v && nvmeta == v->items.n) return;
	free(vmeta);
	nvmeta = v->items.n;
	vmeta = nvmeta ? xcalloc((size_t)nvmeta, sizeof *vmeta) : NULL;
	vmeta_for = v->items.v;
	for (int i = 0; i < nvmeta; i++) {
		Entry *e = &v->items.v[i];
		struct stat st;
		vmeta[i].added = stat(e->path, &st) == 0 ? st.st_mtime : 0;
		vmeta[i].watched = !e->is_dir && pc_get(e->path, &vmeta[i].last) > 0;
		if (!vmeta[i].watched) vmeta[i].last = 0;
	}
}

/* the order the user asked for (folders stay first) */
static int video_sort_cmp(const void *a, const void *b) {
	const Entry *ea = a, *eb = b;
	if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
	if (g_set.video_sort != VS_NAME && !ea->is_dir) {
		struct stat sa, sb;
		if (g_set.video_sort == VS_ADDED) {
			time_t ta = stat(ea->path, &sa) == 0 ? sa.st_mtime : 0, tb = stat(eb->path, &sb) == 0 ? sb.st_mtime : 0;
			if (ta != tb) return ta > tb ? -1 : 1;
		} else {
			time_t la, lb;
			int wa = pc_get(ea->path, &la) > 0, wb = pc_get(eb->path, &lb) > 0;
			if (wa != wb) return wa ? -1 : 1;
			if (wa && la != lb) return la > lb ? -1 : 1;
		}
	}
	return strcasecmp(ea->name, eb->name);
}

static void video_apply_sort(View *v) {
	if (v->items.n > 1) qsort(v->items.v, (size_t)v->items.n, sizeof(Entry), video_sort_cmp);
}

/* a folder's picture: Imgs/<folder>.png beside it, else the first video inside that has one */
static int video_folder_art(const Entry *e, char *out, size_t n) {
	char dir[512];
	path_dir(e->path, dir, sizeof dir);
	static const char *ext[] = {"png", "jpg"};
	for (int i = 0; i < 2; i++) {
		snfmt(out, n, "%s/Imgs/%s.%s", dir, path_base(e->path), ext[i]);
		if (file_exists(out)) return 1;
	}
	for (int i = 0; i < 2; i++) {
		snfmt(out, n, "%s/folder.%s", e->path, ext[i]);
		if (file_exists(out)) return 1;
	}
	EntryList l = {0};
	data_list_system(e->sys, e->path, &l);
	int ok = 0;
	for (int i = 0; i < l.n && !ok; i++) ok = !l.v[i].is_dir && data_art_path(&l.v[i], out, n);
	el_clear(&l);
	return ok;
}

/* the first episode not yet watched, for landing on when a folder opens */
static int video_first_unwatched(View *v) {
	for (int i = 0; i < v->items.n; i++) if (!v->items.v[i].is_dir && pc_get(v->items.v[i].path, NULL) == 0) return i;
	return 0;
}

static int video_cols(void) { return g_set.video_wall ? VW_COLS : 1; }
static int video_rows(void) { return g_set.video_wall ? VW_ROWS : list_rows(); }

/* after wall <-> list: every video list's top is in the new units (rows or items) */
static void video_reclamp(void) {
	clamp_cursor(&v_video.cur, v_video.items.n, video_rows(), video_cols());
	for (int i = 0; i < depth; i++)
		if (stack[i].type == V_LIST && stack[i].kind == LK_VIDEO) clamp_cursor(&stack[i].cur, stack[i].items.n, video_rows(), video_cols());
}

static void video_load(View *v) {
	load_list(v);
	video_apply_sort(v);
	vmeta_for = NULL;
	video_meta_bind(v);
	clamp_cursor(&v->cur, v->items.n, video_rows(), video_cols());
}

static void video_enter(void) {
	video_import_watched();
	video_sys = data_sys_index("VIDEOS");
	v_video.type = V_LIST;
	v_video.kind = LK_VIDEO;
	v_video.sys = video_sys;
	v_video.dir[0] = 0;
	if (video_sys >= 0) video_load(&v_video);
	else { el_clear(&v_video.items); vmeta_for = NULL; }
}

static void push_video_list(const char *dir) {
	if (depth >= ARRAY_LEN(stack) || video_sys < 0) return;
	View *v = &stack[depth++];
	memset(v, 0, sizeof *v);
	v->type = V_LIST;
	v->kind = LK_VIDEO;
	v->sys = video_sys;
	sncpy(v->dir, dir ? dir : "", sizeof v->dir);
	video_load(v);
	v->cur.sel = video_first_unwatched(v);
	clamp_cursor(&v->cur, v->items.n, video_rows(), video_cols());
	v->cur.asel = (float)v->cur.sel;
	v->cur.atop = (float)v->cur.top;
	v->sel_time = plat_ticks();
	dirty = 1;
}

/* the list being shown on the Videos tab or inside one of its folders */
static View *video_view(void) {
	View *v = top();
	if (v->type == V_LIST && v->kind == LK_VIDEO) return v;
	if (v->type == V_HOME && tab == TAB_VIDEOS) return &v_video;
	return NULL;
}

static void fmt_runtime(int s, char *out, size_t n) {
	if (s >= 3600) snfmt(out, n, "%dh %02dm", s / 3600, s / 60 % 60);
	else if (s >= 60) snfmt(out, n, "%d min", (s + 30) / 60);
	else snfmt(out, n, "%d s", s);
}

static void draw_check(int cx, int cy, int s, uint32_t c) {
	/* a tick: a short stroke down-right then a long one up-right */
	for (int i = 0; i <= s / 2; i++) gfx_rect(cx - s / 2 + i, cy + i - 1, 2, 3, c);
	for (int i = 0; i <= s; i++) gfx_rect(cx + i, cy + s / 2 - i - 1, 2, 3, c);
}

static void draw_video_badge_watched(int x, int y) {
	gfx_circle(x, y, 11, RGBA(0, 0, 0, 120));
	gfx_circle(x, y, 10, g_theme.accent);
	draw_check(x - 1, y - 1, 8, g_theme.accent_text);
}

/* the picture for a video: its thumbnail, or a dark card with a play mark */
static void draw_video_tile(View *v, int i, int x, int y, int w, int h, int sel) {
	Entry *e = &v->items.v[i];
	int r = g_theme.radius;
	char art[1024];
	Image *im = NULL;
	int has = g_set.show_art && (e->is_dir ? video_folder_art(e, art, sizeof art) : data_art_path(e, art, sizeof art));
	if (has) im = cached_image(art, w, h, IM_FIT); /* a frame fills the tile; a tall poster sits in the middle */
	if (sel) gfx_shadow(x, y + 4, w, h, r, 10, col_alpha(0x000000, 120));
	uint32_t tc = tint_of_name(e->name);
	if (im && im->w >= w - 2 && im->h >= h - 2) gfx_blit_rounded(im, x, y, r, 255);
	else if (im) {
		uint32_t pc;
		if (!tint_cached(art, &pc)) { pc = tint_of_image(im); tint_store(art, pc); }
		gfx_rrect(x, y, w, h, r, RGBA((pc >> 16 & 255) / 4, (pc >> 8 & 255) / 4, (pc & 255) / 4, 255));
		gfx_rrect_stroke(x, y, w, h, r, 1, RGBA(255, 255, 255, 40));
		gfx_blit_rounded(im, x + (w - im->w) / 2, y + (h - im->h) / 2, MIN(r, 6), 255);
		if (e->is_dir) draw_folder(x + 10, y + h - 26, 18, RGBA(255, 255, 255, 200));
	} else {
		gfx_rrect(x, y, w, h, r, RGBA((tc >> 16 & 255) / 3, (tc >> 8 & 255) / 3, (tc & 255) / 3, 255));
		gfx_rrect_stroke(x, y, w, h, r, 1, RGBA(255, 255, 255, 40));
		if (e->is_dir) draw_folder(x + w / 2 - 26, y + h / 2 - 22, 44, RGBA(255, 255, 255, 190));
		else {
			gfx_circle(x + w / 2, y + h / 2, 22, RGBA(255, 255, 255, 46));
			draw_play_icon(x + w / 2 - 7, y + h / 2 - 11, 22, RGBA(255, 255, 255, 220));
		}
	}
	if (sel) gfx_rrect_stroke(x - 3, y - 3, w + 6, h + 6, r + 3, 3, sel_ring());
	if (!e->is_dir) {
		VMeta *m = i < nvmeta ? &vmeta[i] : NULL;
		if (m && !m->dur_known && decode_budget > 0) { m->dur = video_duration(e->path); m->dur_known = 1; decode_budget--; }
		if (m && m->dur > 0) {
			char d[24];
			fmt_runtime(m->dur, d, sizeof d);
			int tw = text_width(F_SMALL, d) + 12, th = font_height(F_SMALL) + 4;
			gfx_rrect(x + w - tw - 6, y + h - th - 6, tw, th, th / 2, RGBA(0, 0, 0, 170));
			text_draw(F_SMALL, x + w - tw, y + h - th - 4, d, RGBA(255, 255, 255, 235));
		}
		if (m && m->watched) draw_video_badge_watched(x + 16, y + 16);
	}
	if (e->fav) draw_star(x + w - 15, y + 15, 8, g_theme.favorite);
}

static void draw_video_empty(void) {
	draw_empty_z("ice", "No videos yet",
	           "Copy films or shows (.mp4 works best) into the Import folder or Roms/VIDEOS on your card. "
	           "Run \"Get box art\" in the PC kit for pictures.");
}

static void draw_video_wall(View *v) {
	int n = v->items.n;
	if (n == 0) { draw_video_empty(); return; }
	int gap = 16;
	int x0 = (SCREEN_W - (VW_COLS * VW_TW + (VW_COLS - 1) * gap)) / 2;
	int y0 = CONTENT_Y + 14;
	float off = v->cur.atop * VW_PITCH;
	gfx_clip(0, CONTENT_Y, SCREEN_W, CONTENT_H);
	int first_row = MAX(0, (int)floorf(v->cur.atop) - 1);
	for (int i = first_row * VW_COLS; i < n && i < (first_row + VW_ROWS + 2) * VW_COLS; i++) {
		Entry *e = &v->items.v[i];
		int x = x0 + (i % VW_COLS) * (VW_TW + gap);
		int y = (int)(y0 + (i / VW_COLS) * VW_PITCH - off);
		int sel = i == v->cur.sel;
		draw_video_tile(v, i, x, y, VW_TW, VW_TH, sel);
		uint32_t tc = sel ? g_theme.text : g_theme.text_dim;
		if (glass()) {
			text_draw_fit(F_SMALL, x + 1, y + VW_TH + 10, VW_TW, e->name, col_alpha(0x000000, 150), 1);
			tc = sel ? 0xFFFFFFFF : RGBA(225, 225, 235, 255);
		}
		text_draw_fit(F_SMALL, x, y + VW_TH + 9, VW_TW, e->name, tc, 1);
	}
	gfx_unclip();
	/* scroll hint */
	int rows = (n + VW_COLS - 1) / VW_COLS;
	if (rows > VW_ROWS) {
		int sx = SCREEN_W - 10, sh = CONTENT_H - 24;
		gfx_rrect(sx, CONTENT_Y + 12, 3, sh, 1, col_alpha(g_theme.text_dim, 60));
		int th = MAX(20, sh * VW_ROWS / rows);
		int ty = CONTENT_Y + 12 + (int)((sh - th) * (v->cur.atop / MAX(1, rows - VW_ROWS)));
		gfx_rrect(sx, ty, 3, th, 1, g_theme.accent);
	}
}

static void draw_videos(View *v) {
	video_meta_bind(v);
	if (g_set.video_wall) { draw_video_wall(v); return; }
	if (!v->items.n) { draw_video_empty(); return; }
	int save = g_set.game_layout;
	g_set.game_layout = GL_LIST_ART;
	draw_list(v);
	g_set.game_layout = save;
	/* the list: watched mark beside the selected name's art box */
	if (v->cur.sel < nvmeta && vmeta[v->cur.sel].watched && !v->items.v[v->cur.sel].is_dir)
		draw_video_badge_watched(g_theme.art_left ? PAD + 20 : SCREEN_W - PAD - 20, CONTENT_Y + 20);
}

static int video_hints(Hint *h, View *v) {
	int nh = 0;
	h[nh++] = (Hint){"SELECT", g_set.video_wall ? "List" : "Wall"};
	if (v->items.n) {
		Entry *e = &v->items.v[v->cur.sel];
		h[nh++] = (Hint){"X", "Options"};
		if (!e->is_dir) h[nh++] = (Hint){"Y", e->fav ? "Unfavorite" : "Favorite"};
		h[nh++] = (Hint){confirm_btn(), e->is_dir ? "Open" : "Watch"};
	}
	return nh;
}

/* Up next: inside a series folder the following episodes are queued so they
 * play on by themselves when one ends (data_launch writes them) */
static void video_queue_next(View *v, int i) {
	data_next_video[0][0] = data_next_video[1][0] = 0;
	if (!v->dir[0] || g_set.video_sort != VS_NAME) return;
	int k = 0;
	for (int j = i + 1; j < v->items.n && k < 2; j++)
		if (!v->items.v[j].is_dir) sncpy(data_next_video[k++], v->items.v[j].path, sizeof data_next_video[0]);
}

/* Continue watching: a series folder with some episodes watched and the next
 * one not; fills *folder / *next and returns 1 (the most recently watched first) */
static int video_continue(const View *v, char *folder, size_t fn, char *next, size_t nn) {
	time_t best = 0;
	int found = 0;
	for (int i = 0; i < v->items.n; i++) {
		if (!v->items.v[i].is_dir) continue;
		EntryList l = {0};
		data_list_system(v->items.v[i].sys, v->items.v[i].path, &l);
		if (l.n > 1) qsort(l.v, (size_t)l.n, sizeof(Entry), video_sort_cmp);
		time_t last = 0;
		int nxt = -1;
		for (int j = 0; j < l.n; j++) {
			if (l.v[j].is_dir) continue;
			time_t t;
			if (pc_get(l.v[j].path, &t) > 0) { if (t > last) last = t; nxt = -1; }
			else if (nxt < 0 && last) nxt = j;
		}
		if (nxt >= 0 && last > best) {
			best = last;
			found = 1;
			sncpy(folder, v->items.v[i].name, fn);
			sncpy(next, l.v[nxt].path, nn);
		}
		el_clear(&l);
	}
	return found;
}

/* episodes the player went on to by itself (data_launch noted them) count as watched */
static void video_import_watched(void) {
	char p[600];
	snfmt(p, sizeof p, "%s/watched-next.txt", g_paths.clove);
	char *txt = file_read_all(p, NULL);
	if (!txt) return;
	for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
		size_t l = strlen(line);
		while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
		if (l) pc_bump(line);
	}
	free(txt);
	unlink(p);
}

static char cont_next[1024];
static void video_options(View *v) {
	if (!v->items.n) return;
	Entry *e = &v->items.v[v->cur.sel];
	open_overlay(OV_GAME, e->name);
	if (!v->dir[0]) {
		char folder[128];
		if (video_continue(v, folder, sizeof folder, cont_next, sizeof cont_next)) {
			char b[64];
			snfmt(b, sizeof b, "Continue %s", folder);
			ov_add(b, OA_VCONT);
		}
	}
	sncpy(ov.path, e->path, sizeof ov.path);
	sncpy(ov.name, e->name, sizeof ov.name);
	ov.sys = e->sys;
	int watched = v->cur.sel < nvmeta && vmeta[v->cur.sel].watched;
	if (!e->is_dir) ov_add(e->fav ? "Remove from favorites" : "Add to favorites", OA_FAV);
	if (!e->is_dir) ov_add(watched ? "Mark as not watched" : "Mark as watched", OA_VWATCH);
	ov_add("Player buttons", OA_VHELP);
	{
		static const char *SN[VS_COUNT] = {"by name", "recently added first", "last watched first"};
		char b[64];
		snfmt(b, sizeof b, "Sort: %s", SN[CLAMP(g_set.video_sort, 0, VS_COUNT - 1)]);
		ov_add(b, OA_VSORT);
	}
	ov_add("Video settings", OA_SYSSET);
	ov_add("Cancel", OA_CANCEL);
}

static void video_resort_all(void) {
	View *vs[2] = {&v_video, top()->type == V_LIST && top()->kind == LK_VIDEO ? top() : NULL};
	for (int k = 0; k < 2; k++) {
		View *v = vs[k];
		if (!v || !v->items.n) continue;
		char keep[1024];
		sncpy(keep, v->items.v[v->cur.sel].path, sizeof keep);
		video_apply_sort(v);
		vmeta_for = NULL;
		video_meta_bind(v);
		for (int i = 0; i < v->items.n; i++) if (!strcmp(v->items.v[i].path, keep)) v->cur.sel = i;
		clamp_cursor(&v->cur, v->items.n, video_rows(), video_cols());
	}
	mark_settings();
}

/* the overlay actions that belong to videos; 1 when handled */
static int video_overlay_action(int act) {
	if (act == OA_VWATCH) {
		View *v = video_view();
		int on = pc_get(ov.path, NULL) > 0;
		if (on) pc_forget(ov.path);
		else pc_bump(ov.path);
		if (v) for (int i = 0; i < v->items.n && i < nvmeta; i++) if (!strcmp(v->items.v[i].path, ov.path)) vmeta[i].watched = !on;
		toast(on ? "Marked as not watched" : "Marked as watched");
		return 1;
	}
	if (act == OA_VCONT) {
		/* open the series and start the next episode */
		char dir[1024];
		path_dir(cont_next, dir, sizeof dir);
		if (top()->type != V_LIST || top()->kind != LK_VIDEO) push_video_list(dir);
		View *v = top();
		for (int i = 0; i < v->items.n; i++) if (!strcmp(v->items.v[i].path, cont_next)) { v->cur.sel = i; video_queue_next(v, i); launch_entry(&v->items.v[i], LK_VIDEO); break; }
		return 1;
	}
	if (act == OA_VSORT) {
		g_set.video_sort = (g_set.video_sort + 1) % VS_COUNT;
		video_resort_all();
		static const char *SN[VS_COUNT] = {"Sorted by name", "Recently added first", "Last watched first"};
		toast("%s", SN[g_set.video_sort]);
		return 1;
	}
	if (act == OA_VHELP) {
		{
			char t[220];
			snfmt(t, sizeof t, "In the player: %s pauses, %s comes back to Zappo. Left/right skip 10 seconds, L1/R1 a minute (the same as the music player). MENU shows the player's own menu.",
			       confirm_btn(), back_btn());
			open_overlay(OV_CONFIRM, t);
		}
		ov.confirm_action = 0;
		ov_add("OK", OA_CANCEL);
		return 1;
	}
	return 0;
}

static void video_button(View *v, int b) {
	int cols = video_cols(), rows = video_rows();
	int n = v->items.n;
	int old = v->cur.sel;
	int wall = g_set.video_wall;
	switch (b) {
	case B_UP: move_cursor(&v->cur, n, -cols, rows, cols, 1); break;
	case B_DOWN: move_cursor(&v->cur, n, cols, rows, cols, 1); break;
	case B_LEFT: move_cursor(&v->cur, n, wall ? -1 : -rows, rows, cols, wall); break;
	case B_RIGHT: move_cursor(&v->cur, n, wall ? 1 : rows, rows, cols, wall); break;
	case B_L1: move_cursor(&v->cur, n, -rows * cols, rows, cols, 0); break;
	case B_R1: move_cursor(&v->cur, n, rows * cols, rows, cols, 0); break;
	case B_L2: jump_letter(v, -1); clamp_cursor(&v->cur, n, rows, cols); break;
	case B_R2: jump_letter(v, 1); clamp_cursor(&v->cur, n, rows, cols); break;
	case B_A:
		if (!n) break;
		if (v->items.v[v->cur.sel].is_dir) push_video_list(v->items.v[v->cur.sel].path);
		else { video_queue_next(v, v->cur.sel); launch_entry(&v->items.v[v->cur.sel], LK_VIDEO); }
		break;
	case B_Y: toggle_fav(v); break;
	case B_X: video_options(v); break;
	case B_SELECT:
		g_set.video_wall = !g_set.video_wall;
		video_reclamp();
		mark_settings();
		break;
	}
	if (v->cur.sel != old) v->sel_time = plat_ticks();
}

/* Surprise me: a random game from the consoles (not videos or apps) */
static void surprise_me(void) {
	int cands[256], nc = 0;
	for (int i = 0; i < g_nsys && nc < 256; i++) {
		if (!g_sys[i].count || g_sys[i].is_import) continue;
		if (!strcmp(g_sys[i].code, "VIDEOS") || !strcmp(g_sys[i].code, "APPS") || !strcmp(g_sys[i].code, "PORTS")) continue;
		if (sys_get_int(g_sys[i].code, "hidden", 0)) continue;
		for (int k = 0; k < g_sys[i].count && nc < 256; k++) cands[nc++] = i; /* weighted by how many games it has */
	}
	if (!nc) { toast("No games to pick from yet"); return; }
	srand((unsigned)(plat_ticks() ^ (uint64_t)time(NULL)));
	for (int tries = 0; tries < 6; tries++) {
		int sys = cands[rand() % nc];
		EntryList l = {0};
		data_list_system(sys, NULL, &l);
		int files = 0;
		for (int i = 0; i < l.n; i++) files += !l.v[i].is_dir;
		if (!files) { el_clear(&l); continue; }
		int pick = rand() % files;
		for (int i = 0; i < l.n; i++) {
			if (l.v[i].is_dir) continue;
			if (pick-- == 0) {
				toast("Surprise: %s", l.v[i].name);
				launch_entry(&l.v[i], LK_SYSTEM);
				el_clear(&l);
				return;
			}
		}
		el_clear(&l);
	}
	toast("Couldn't pick one this time");
}
