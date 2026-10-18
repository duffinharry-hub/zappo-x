/* Zappo X on Knulli: where games live and how they start. Included by data.c
 * in the Knulli build (ZAPPO_KNULLI).
 *
 * Knulli lists its systems in es_systems.cfg: the folder under /userdata/roms,
 * the file extensions, the emulators (default first). Zappo keeps its own
 * console codes - the ones its menus, icons, BIOS list and Import rules use -
 * so each Knulli folder maps to one; folders Zappo doesn't know keep Knulli's
 * name. Games start through Knulli's emulatorlauncher, the same way its own
 * menu starts them, so Knulli's per-system settings, controller setup and
 * RetroArch configuration all apply. */

#define KNULLI_SYSTEMS "/usr/share/emulationstation/es_systems.cfg"
#define KNULLI_VIDEO_EXTS ".mp4 .mkv .avi .mov .webm .m4v .mpg .mpeg .wmv .flv .ts .ogv"
/* the port that takes Knulli's menu back to Zappo X (knulli/install.sh puts
 * it in roms/ports): not a game, so not listed in Zappo X itself */
#define KNULLI_BACK_PORT "Zappo X.sh"

static const struct { const char *dir, *code; } KNULLI_CODES[] = {
	{"gb", "GB"}, {"gbc", "GBC"}, {"gba", "GBA"}, {"sgb", "SGB"}, {"nds", "NDS"},
	{"nes", "FC"}, {"fds", "FDS"}, {"snes", "SFC"}, {"sufami", "SUFAMI"}, {"satellaview", "SATELLAVIEW"},
	{"virtualboy", "VB"}, {"pokemini", "POKE"}, {"gameandwatch", "GW"}, {"n64", "N64"},
	{"megadrive", "MD"}, {"mastersystem", "MS"}, {"gamegear", "GG"}, {"segacd", "SEGACD"},
	{"sega32x", "THIRTYTWOX"}, {"sg1000", "SEGASGONE"}, {"dreamcast", "DC"}, {"saturn", "SATURN"},
	{"psx", "PS"}, {"psp", "PSP"},
	{"pcengine", "PCE"}, {"pcenginecd", "PCECD"}, {"supergrafx", "SGFX"}, {"pcfx", "PCFX"},
	{"neogeo", "NEOGEO"}, {"neogeocd", "NEOCD"}, {"ngp", "NGP"},
	{"mame", "ARCADE"}, {"fbneo", "FBNEO"}, {"cps1", "CPS1"}, {"cps2", "CPS2"}, {"cps3", "CPS3"},
	{"wswan", "WS"}, {"lynx", "LYNX"}, {"atari2600", "ATARI"}, {"atari5200", "FIFTYTWOHUNDRED"},
	{"atari7800", "SEVENTYEIGHTHUNDRED"}, {"atari800", "EIGHTHUNDRED"}, {"jaguar", "JAGUAR"},
	{"atarist", "ATARIST"}, {"3do", "PANASONIC"}, {"colecovision", "COLECO"},
	{"intellivision", "INTELLIVISION"}, {"vectrex", "VECTREX"}, {"o2em", "ODYSSEY"},
	{"videopacplus", "VIDEOPAC"}, {"channelf", "FAIRCHILD"}, {"supervision", "SUPERVISION"},
	{"megaduck", "MEGADUCK"}, {"uzebox", "UZEBOX"},
	{"c64", "COMMODORE"}, {"c128", "C128"}, {"vic20", "VIC20"}, {"pet", "PET"}, {"cplus4", "PLUS4"},
	{"amiga500", "AMIGA"}, {"amigacd32", "AMIGACD"}, {"amstradcpc", "CPC"}, {"zxspectrum", "ZXS"},
	{"zx81", "ZXEIGHTYONE"}, {"msx1", "MSX"}, {"x1", "XONE"}, {"x68000", "X68000"},
	{"pc88", "PCEIGHTYEIGHT"}, {"pc98", "PCNINETYEIGHT"}, {"dos", "DOS"}, {"scummvm", "SCUMMVM"},
	{"pico8", "PICO"}, {"tic80", "TIC"}, {"prboom", "DOOM"}, {"tyrquake", "QUAKE"},
	{"ports", "PORTS"},
};

static const char *knulli_code(const char *dir) {
	for (int i = 0; i < ARRAY_LEN(KNULLI_CODES); i++)
		if (strcmp(KNULLI_CODES[i].dir, dir) == 0) return KNULLI_CODES[i].code;
	return NULL;
}

/* the text between <name> and </name> inside [p, end) */
static int xml_tag(const char *p, const char *end, const char *name, char *out, size_t n) {
	char open[48], close[48];
	snfmt(open, sizeof open, "<%s>", name);
	snfmt(close, sizeof close, "</%s>", name);
	const char *a = strstr(p, open);
	if (!a || a >= end) return 0;
	a += strlen(open);
	const char *b = strstr(a, close);
	if (!b || b > end) return 0;
	size_t l = (size_t)(b - a);
	if (l + 1 > n) l = n - 1;
	memcpy(out, a, l);
	out[l] = 0;
	str_trim(out);
	/* the few entities Knulli's names use */
	char *amp;
	while ((amp = strstr(out, "&amp;"))) memmove(amp + 1, amp + 5, strlen(amp + 5) + 1);
	return 1;
}

/* emulators and cores as "emulator/core", the default one first */
static char *knulli_emulators(const char *p, const char *end) {
	char def[96] = "", all[1024] = "";
	const char *e = p;
	while ((e = strstr(e, "<emulator name=\"")) && e < end) {
		char emu[48];
		const char *q = e + 16, *qe = strchr(q, '"');
		if (!qe || qe > end) break;
		snfmt(emu, sizeof emu, "%.*s", (int)(qe - q), q);
		const char *stop = strstr(qe, "</emulator>");
		if (!stop || stop > end) stop = end;
		const char *c = qe;
		int any = 0;
		while ((c = strstr(c, "<core")) && c < stop) {
			const char *gt = strchr(c, '>'), *lt = gt ? strstr(gt, "</core>") : NULL;
			if (!gt || !lt || lt > stop) break;
			char one[96];
			snfmt(one, sizeof one, "%s/%.*s", emu, (int)(lt - gt - 1), gt + 1);
			int is_def = strstr(c, "default=\"true\"") && strstr(c, "default=\"true\"") < gt;
			if (is_def && !def[0]) sncpy(def, one, sizeof def);
			else if (strlen(all) + strlen(one) + 2 < sizeof all) { if (all[0]) strcat(all, ","); strcat(all, one); }
			any = 1;
			c = lt;
		}
		if (!any) {	/* a standalone emulator without cores */
			if (!def[0]) sncpy(def, emu, sizeof def);
			else if (strlen(all) + strlen(emu) + 2 < sizeof all) { if (all[0]) strcat(all, ","); strcat(all, emu); }
		}
		e = stop;
	}
	char out[1200];
	snfmt(out, sizeof out, "%s%s%s", def, def[0] && all[0] ? "," : "", all);
	return xstrdup(out);
}

static void knulli_init_systems(void) {
	char *txt = file_read_all(KNULLI_SYSTEMS, NULL);
	if (!txt) { LOG("Knulli's system list is missing: %s", KNULLI_SYSTEMS); return; }
	int n = 0;
	for (char *p = strstr(txt, "<system>"); p; p = strstr(p, "<system>")) {
		char *end = strstr(p, "</system>");
		if (!end) break;
		char dir[64] = "", full[96] = "", path[256] = "", exts[200] = "";
		xml_tag(p, end, "name", dir, sizeof dir);
		xml_tag(p, end, "fullname", full, sizeof full);
		xml_tag(p, end, "path", path, sizeof path);
		xml_tag(p, end, "extension", exts, sizeof exts);
		const char *blk = p;
		p = end + 9;
		if (!dir[0] || !str_starts_with(path, "/userdata/roms/")) continue;
		sncpy(dir, path + 15, sizeof dir);
		char *slash = strchr(dir, '/');
		if (slash) *slash = 0;
		const char *code = knulli_code(dir);
		char up[32];
		if (!code) {
			sncpy(up, dir, sizeof up);
			for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
			code = up;
		}
		if (data_sys_index(code) >= 0) continue;	/* first folder for a code wins */
		char *emus = knulli_emulators(blk, end);
		char def[96];
		sncpy(def, emus, sizeof def);
		char *comma = strchr(def, ',');
		if (comma) *comma = 0;
		add_system(code, def);
		System *s = &g_sys[data_sys_index(code)];
		sncpy(s->dir, dir, sizeof s->dir);
		sncpy(s->exts, exts, sizeof s->exts);
		s->emus = emus;
		s->cpu_rec = 0;		/* Knulli sets the processor per system itself */
		if (!s->known && full[0]) {
			sncpy(s->name, full, sizeof s->name);
			sncpy(s->short_name, full, sizeof s->short_name);
		}
		n++;
	}
	free(txt);
	LOG("Knulli: %d systems", n);
}

static int knulli_ext_ok(const char *exts, const char *name) {
	if (!strcmp(name, KNULLI_BACK_PORT)) return 0;
	if (!exts || !*exts) return 1;
	const char *ext = path_ext(name);
	if (!*ext) return 0;
	char buf[200];
	sncpy(buf, exts, sizeof buf);
	for (char *t = strtok(buf, " "); t; t = strtok(NULL, " "))
		if (str_ieq(t[0] == '.' ? t + 1 : t, ext)) return 1;
	return 0;
}

/* folders Knulli keeps next to the games, not games themselves */
static int knulli_media_dir(const char *name) {
	static const char *M[] = {"images", "videos", "manuals", "media", "downloaded_images",
	                          "downloaded_videos", "thumbnails", "marquees", "boxart"};
	for (int i = 0; i < ARRAY_LEN(M); i++)
		if (str_ieq(name, M[i])) return 1;
	return 0;
}

static int knulli_sys_from_path(const char *path) {
	const char *r = strstr(path, "/roms/");
	if (!r) {
		if (strstr(path, "/import/")) return data_sys_index("IMPORT");
		return -1;
	}
	r += 6;
	char dir[32];
	int n = 0;
	while (r[n] && r[n] != '/' && n < 31) { dir[n] = r[n]; n++; }
	dir[n] = 0;
	for (int i = 0; i < g_nsys; i++)
		if (str_ieq(g_sys[i].dir, dir)) return i;
	return -1;
}

/* Box art as Knulli's scraper saves it: roms/<system>/images/<game>-image.png
 * (or -thumb / -boxart), else <game>.png there */
static int knulli_art(const Entry *e, char *out, size_t n) {
	char stem[256], sysdir[512];
	path_stem(e->path, stem, sizeof stem);
	data_sys_dir(e->sys, sysdir, sizeof sysdir);
	static const char *SUF[] = {"-image", "-thumb", "-boxart", ""};
	static const char *EXT[] = {"png", "jpg"};
	for (int s = 0; s < ARRAY_LEN(SUF); s++)
		for (int x = 0; x < 2; x++) {
			snfmt(out, n, "%s/images/%s%s.%s", sysdir, stem, SUF[s], EXT[x]);
			if (file_exists(out)) return 1;
		}
	return 0;
}

/* "libretro/mgba" -> "mGBA"-style labels: the core's name, or the emulator's */
static const char *knulli_core_label(const char *choice) {
	static char out[64];
	const char *slash = strchr(choice, '/');
	if (!slash) { sncpy(out, choice, sizeof out); return out; }
	if (strncmp(choice, "libretro/", 9) == 0) snfmt(out, sizeof out, "%s", slash + 1);
	else snfmt(out, sizeof out, "%s (%.*s)", slash + 1, (int)(slash - choice), choice);
	return out;
}

static int knulli_cores(int sys, StrList *out) {
	sl_free(out);
	const char *e = g_sys[sys].emus;
	if (!e) return 0;
	char buf[1200];
	sncpy(buf, e, sizeof buf);
	for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) sl_push(out, t);
	return out->n;
}

/* player 1 (and more) as Knulli's own menu describes them to emulatorlauncher */
static void knulli_pad_args(char *out, size_t n) {
	PadInfo pads[4];
	int np = plat_pads(pads, 4);
	size_t l = 0;
	out[0] = 0;
	for (int i = 0; i < np; i++) {
		char qn[160];
		shell_squote(pads[i].name, qn, sizeof qn);
		l += (size_t)snfmt(out + l, n - l,
		                   "-p%dindex %d -p%dguid %s -p%dname '%s' -p%ddevicepath %s "
		                   "-p%dnbbuttons %d -p%dnbhats %d -p%dnbaxes %d ",
		                   i + 1, i, i + 1, pads[i].guid, i + 1, qn, i + 1, pads[i].path,
		                   i + 1, pads[i].buttons, i + 1, pads[i].hats, i + 1, pads[i].axes);
		if (l >= n) { out[n - 1] = 0; break; }
	}
}

/* Films play in Knulli's mpv (through SDL, like everything on Knulli's
 * screen). The handheld's buttons reach it through zappo-x --mpv-remote: A
 * pause, B back, left/right 10 s, up/down a minute, L1/R1 the previous/next
 * film, SELECT how far. It remembers where each one stopped. The next
 * episodes queue up behind. */
static int knulli_play_video(const Entry *e, char *err, size_t errn) {
	char body[6000], q[1200];
	int len = snfmt(body, sizeof body,
	      "#!/bin/sh\n# Written by Zappo %s\n"
	      "D=%s\n"
	      "S=/tmp/zappo-x-mpv.sock\n"
	      "rm -f $S\n"
	      "$D/zappo-x --mpv-remote $S &\n"	/* the buttons (see plat_mpv_remote) */
	      "mpv --vo=sdl --fs --really-quiet --input-ipc-server=$S --input-default-bindings=no "
	      "--input-conf=$D/mpv-input.conf --save-position-on-quit --watch-later-dir=$D/watch-later "
	      "--osd-font-size=28 --osd-duration=1500",
	      CLOVE_VERSION, g_paths.clove);
	shell_squote(e->path, q, sizeof q);
	len += snfmt(body + len, sizeof body - (size_t)len, " '%s'", q);
	for (int k = 0; k < 2 && data_next_video[k][0]; k++) {
		shell_squote(data_next_video[k], q, sizeof q);
		len += snfmt(body + len, sizeof body - (size_t)len, " '%s'", q);
	}
	snfmt(body + len, sizeof body - (size_t)len, "\n");
	data_next_video[0][0] = data_next_video[1][0] = 0;
	if (write_command(body) != 0) { snfmt(err, errn, "Could not write the start command"); return -1; }
	recents_add(e->path);
	return 0;
}

static int knulli_launch(const Entry *e, char *err, size_t errn) {
	const System *s = &g_sys[e->sys];
	if (!strcmp(s->code, "VIDEOS")) return knulli_play_video(e, err, errn);
	if (!s->dir[0]) { snfmt(err, errn, "Knulli has no emulator for this"); return -1; }
	char qrom[1200], qname[160], pads[1024], force[160] = "", resume[64] = "", body[4096];
	shell_squote(e->path, qrom, sizeof qrom);
	shell_squote(s->name, qname, sizeof qname);
	knulli_pad_args(pads, sizeof pads);
	/* an emulator chosen in Zappo; otherwise Knulli's own choice for the system */
	const char *o = sys_get_str(s->code, "core");
	if (o && *o) {
		char emu[64];
		sncpy(emu, o, sizeof emu);
		char *slash = strchr(emu, '/');
		if (slash) { *slash = 0; snfmt(force, sizeof force, "-emulator %s -core %s ", emu, slash + 1); }
		else snfmt(force, sizeof force, "-emulator %s ", emu);
	}
	/* Continue where you left off: RetroArch saves a state on closing and
	 * loads it next time. "Start over" puts the old one aside (not deleted)
	 * so this start begins fresh. */
	snfmt(resume, sizeof resume, "-autosave %d ", g_set.resume_games ? 1 : 0);
	if (data_start_over) {
		char stem[256], sp[1200], aside[1300];
		path_stem(e->path, stem, sizeof stem);
		snfmt(sp, sizeof sp, "%s/saves/%s/%s.state.auto", g_paths.mmc, s->dir, stem);
		snfmt(aside, sizeof aside, "%s.before-start-over", sp);
		if (file_exists(sp)) rename(sp, aside);
		data_start_over = 0;
	}
	snfmt(body, sizeof body,
	      "#!/bin/sh\n# Written by Zappo %s\n"
	      "emulatorlauncher %s-system %s -rom '%s' -systemname '%s' %s%s-gameinfoxml /dev/null\n",
	      CLOVE_VERSION, pads, s->dir, qrom, qname, force, resume);
	if (write_command(body) != 0) { snfmt(err, errn, "Could not write the start command"); return -1; }
	recents_add(e->path);
	return 0;
}
