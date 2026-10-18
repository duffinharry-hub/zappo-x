#include "data.h"
#include "config.h"
#include "import.h"
#include "plat.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

System *g_sys;
int g_nsys;

/* ------------------------------------------------------- system catalogue */

typedef struct {
	const char *code, *name, *short_name;
	int cpu;   /* recommended fixed clock in kHz (0 = leave GarlicOS's automatic governor) */
	int rank;  /* default menu position */
	const char *core; /* only for systems GarlicOS's coremapping.json doesn't list */
} Known;

#define C10 1008000
#define C11 1104000
#define C12 1200000
#define C13 1296000
#define C15 1488000

static const Known KNOWN[] = {
	{"IMPORT", "Import", "Import", 0, -100, NULL},
	{"GB", "Game Boy", "GB", C10, 10, NULL},
	{"GBC", "Game Boy Color", "GBC", C10, 11, NULL},
	{"GBA", "Game Boy Advance", "GBA", C13, 12, NULL},
	{"GPSP", "Game Boy Advance (gpSP)", "GBA gpSP", C12, 13, NULL},
	{"SGB", "Super Game Boy", "SGB", C13, 14, NULL},
	{"FC", "NES / Famicom", "NES", C10, 20, NULL},
	{"NESTOPIA", "NES (Nestopia)", "NES Nestopia", C11, 21, NULL},
	{"FDS", "Famicom Disk System", "FDS", C10, 22, NULL},
	{"SFC", "Super Nintendo", "SNES", C13, 23, NULL},
	{"SUFAMI", "Sufami Turbo", "Sufami", C15, 24, NULL},
	{"SATELLAVIEW", "Satellaview", "BS-X", C15, 25, NULL},
	{"VB", "Virtual Boy", "VB", C12, 26, NULL},
	{"POKE", "Pokemon Mini", "PKM", C10, 27, NULL},
	{"GW", "Game & Watch", "G&W", C10, 28, NULL},
	{"MD", "Mega Drive / Genesis", "MD", C12, 30, NULL},
	{"MS", "Master System", "SMS", C10, 31, NULL},
	{"GG", "Game Gear", "GG", C10, 32, NULL},
	{"SEGACD", "Sega CD", "SCD", C13, 33, NULL},
	{"THIRTYTWOX", "Sega 32X", "32X", C15, 34, NULL},
	{"SEGASGONE", "Sega SG-1000", "SG-1000", C10, 35, NULL},
	{"PS", "PlayStation", "PS1", C15, 40, NULL},
	{"PCE", "PC Engine", "PCE", C11, 50, NULL},
	{"PCECD", "PC Engine CD", "PCE-CD", C12, 51, NULL},
	{"SGFX", "PC Engine SuperGrafx", "SGFX", C11, 52, NULL},
	{"PCFX", "PC-FX", "PC-FX", C15, 53, NULL},
	{"NEOGEO", "Neo Geo", "Neo Geo", C13, 60, NULL},
	{"NEOCD", "Neo Geo CD", "NGCD", C15, 61, NULL},
	{"NGP", "Neo Geo Pocket", "NGP", C10, 62, NULL},
	{"ARCADE", "Arcade", "Arcade", C15, 70, NULL},
	{"FBNEO", "FinalBurn Neo", "FBNeo", C15, 71, NULL},
	{"FBA2012", "FinalBurn Alpha 2012", "FBA", C13, 72, NULL},
	{"MAME2000", "MAME 2000", "MAME", C13, 73, NULL},
	{"CPS1", "Capcom CPS-1", "CPS1", C13, 74, NULL},
	{"CPS2", "Capcom CPS-2", "CPS2", C15, 75, NULL},
	{"CPS3", "Capcom CPS-3", "CPS3", C15, 76, NULL},
	{"WS", "WonderSwan", "WS", C10, 80, NULL},
	{"LYNX", "Atari Lynx", "Lynx", C11, 81, NULL},
	{"ATARI", "Atari 2600", "2600", C10, 82, NULL},
	{"FIFTYTWOHUNDRED", "Atari 5200", "5200", C10, 83, NULL},
	{"SEVENTYEIGHTHUNDRED", "Atari 7800", "7800", C10, 84, NULL},
	{"EIGHTHUNDRED", "Atari 800", "800", C10, 85, NULL},
	{"JAGUAR", "Atari Jaguar", "Jaguar", C15, 86, NULL},
	{"ATARIST", "Atari ST", "ST", C15, 87, NULL},
	{"PANASONIC", "3DO", "3DO", C15, 88, NULL},
	{"COLECO", "ColecoVision", "Coleco", C10, 90, NULL},
	{"INTELLIVISION", "Intellivision", "Intv", C10, 91, NULL},
	{"VECTREX", "Vectrex", "Vectrex", C10, 92, NULL},
	{"ODYSSEY", "Magnavox Odyssey 2", "Odyssey", C10, 93, NULL},
	{"VIDEOPAC", "Philips Videopac", "Videopac", C10, 94, NULL},
	{"FAIRCHILD", "Fairchild Channel F", "Chan F", C10, 95, NULL},
	{"SUPERVISION", "Watara Supervision", "SV", C10, 96, NULL},
	{"MEGADUCK", "Mega Duck", "Duck", C10, 97, NULL},
	{"UZEBOX", "Uzebox", "Uzebox", C10, 98, NULL},
	{"VMU", "Dreamcast VMU", "VMU", C10, 99, NULL},
	{"COMMODORE", "Commodore 64", "C64", C13, 100, NULL},
	{"C128", "Commodore 128", "C128", C13, 101, NULL},
	{"VIC20", "Commodore VIC-20", "VIC-20", C12, 102, NULL},
	{"PET", "Commodore PET", "PET", C12, 103, NULL},
	{"PLUS4", "Commodore Plus/4", "Plus/4", C12, 104, NULL},
	{"AMIGA", "Amiga", "Amiga", C15, 105, NULL},
	{"AMIGACD", "Amiga CD32", "CD32", C15, 106, NULL},
	{"CPC", "Amstrad CPC", "CPC", C11, 107, NULL},
	{"ZXS", "ZX Spectrum", "ZX", C10, 108, NULL},
	{"ZXEIGHTYONE", "ZX81", "ZX81", C10, 109, NULL},
	{"MSX", "MSX", "MSX", C11, 110, NULL},
	{"XONE", "Sharp X1", "X1", C13, 111, NULL},
	{"X68000", "Sharp X68000", "X68k", C15, 112, NULL},
	{"PCEIGHTYEIGHT", "NEC PC-8801", "PC-88", C13, 113, NULL},
	{"PCNINETYEIGHT", "NEC PC-9801", "PC-98", C15, 114, NULL},
	{"DOS", "DOS", "DOS", C15, 115, NULL},
	{"SCUMMVM", "ScummVM", "ScummVM", C13, 120, NULL},
	{"PICO", "PICO-8", "PICO-8", C11, 121, NULL},
	{"TIC", "TIC-80", "TIC-80", C11, 122, NULL},
	{"DOOM", "Doom", "Doom", C13, 123, NULL},
	{"QUAKE", "Quake", "Quake", C15, 124, NULL},
	{"BLOCKBOX", "Blockbox", "Blockbox", C13, 125, "blockbox_libretro.so"},
	{"PORTS", "Ports", "Ports", 0, 130, NULL},
	{"APPS", "Apps", "Apps", 0, 131, NULL},
	{"VIDEOS", "Videos", "Videos", C15, 132, NULL},
};

static int known_index(const char *code) {
	for (int i = 0; i < ARRAY_LEN(KNOWN); i++)
		if (strcmp(KNOWN[i].code, code) == 0) return i;
	return -1;
}

static void add_system(const char *code, const char *core) {
	for (int i = 0; i < g_nsys; i++)
		if (strcmp(g_sys[i].code, code) == 0) {
			if (core && *core) sncpy(g_sys[i].core, core, sizeof g_sys[i].core);
			return;
		}
	g_sys = xrealloc(g_sys, sizeof(System) * (size_t)(g_nsys + 1));
	System *s = &g_sys[g_nsys++];
	memset(s, 0, sizeof *s);
	sncpy(s->code, code, sizeof s->code);
	sncpy(s->core, core ? core : "", sizeof s->core);
	int k = known_index(code);
	if (k >= 0) {
		sncpy(s->name, KNOWN[k].name, sizeof s->name);
		sncpy(s->short_name, KNOWN[k].short_name, sizeof s->short_name);
		s->cpu_rec = KNOWN[k].cpu;
		s->known = 1;
		s->is_import = strcmp(code, "IMPORT") == 0;
	} else {
		sncpy(s->name, code, sizeof s->name);
		sncpy(s->short_name, code, sizeof s->short_name);
		s->cpu_rec = C13;
	}
}

static int sys_rank(const System *s) {
	/* A custom order (Settings > Systems > Move earlier/later) wins. */
	cJSON *order = cJSON_GetObjectItemCaseSensitive(g_set.root, "system_order");
	int i = 0;
	cJSON *it;
	cJSON_ArrayForEach(it, order) {
		if (cJSON_IsString(it) && strcmp(it->valuestring, s->code) == 0) return i * 10;
		i++;
	}
	int k = known_index(s->code);
	return 100000 + (k >= 0 ? KNOWN[k].rank : 1000);
}

static int cmp_sys(const void *a, const void *b) {
	const System *x = a, *y = b;
	int rx = sys_rank(x), ry = sys_rank(y);
	if (rx != ry) return rx < ry ? -1 : 1;
	return str_icmp_natural(x->name, y->name);
}

static void load_json_object(const char *path, void (*each)(const char *k, cJSON *v)) {
	char *txt = file_read_all(path, NULL);
	cJSON *j = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
	if (!cJSON_IsObject(j)) { cJSON_Delete(j); return; }
	for (cJSON *it = j->child; it; it = it->next) each(it->string, it);
	cJSON_Delete(j);
}

static void each_coremap(const char *k, cJSON *v) {
	if (cJSON_IsString(v)) add_system(k, v->valuestring);
}
static void each_extra(const char *k, cJSON *v) {
	/* clove/systems.json: {"CODE": {"name": "...", "core": "x_libretro.so", "cpu": 1296000}} */
	cJSON *core = cJSON_GetObjectItemCaseSensitive(v, "core");
	add_system(k, cJSON_IsString(core) ? core->valuestring : "");
	System *s = &g_sys[data_sys_index(k)];
	cJSON *name = cJSON_GetObjectItemCaseSensitive(v, "name");
	if (cJSON_IsString(name)) { sncpy(s->name, name->valuestring, sizeof s->name); sncpy(s->short_name, name->valuestring, sizeof s->short_name); }
	cJSON *cpu = cJSON_GetObjectItemCaseSensitive(v, "cpu");
	if (cJSON_IsNumber(cpu)) s->cpu_rec = cpu->valueint;
}

static void fav_load(void);

void data_init(void) {
	char p[512];
	snfmt(p, sizeof p, "%s/config/coremapping.json", g_paths.cfw);
	load_json_object(p, each_coremap);
	if (g_nsys == 0) LOG("coremapping.json missing or empty: %s", p);
	add_system("IMPORT", "");
	for (int i = 0; i < ARRAY_LEN(KNOWN); i++)
		if (KNOWN[i].core) add_system(KNOWN[i].code, KNOWN[i].core);
	snfmt(p, sizeof p, "%s/systems.json", g_paths.clove);
	load_json_object(p, each_extra);
	fav_load();
}

int data_sys_index(const char *code) {
	for (int i = 0; i < g_nsys; i++)
		if (strcmp(g_sys[i].code, code) == 0) return i;
	return -1;
}

int data_sys_from_path(const char *path) {
	const char *r = strstr(path, "/Roms/");
	if (!r) return -1;
	r += 6;
	char code[32];
	int n = 0;
	while (r[n] && r[n] != '/' && n < 31) { code[n] = r[n]; n++; }
	code[n] = 0;
	return data_sys_index(code);
}

const char *data_sys_name(int sys) {
	if (sys < 0 || sys >= g_nsys) return "";
	const System *s = &g_sys[sys];
	const char *custom = sys_get_str(s->code, "name");
	if (custom && *custom) return custom;
	int style = sys_get_int(s->code, "name_style", 0);
	return style == 1 ? s->short_name : style == 2 ? s->code : s->name;
}

const char *data_sys_core(int sys) {
	const char *o = sys_get_str(g_sys[sys].code, "core");
	return o && *o ? o : g_sys[sys].core;
}

int data_sys_cpu(int sys) { return sys_get_int(g_sys[sys].code, "cpu", g_sys[sys].cpu_rec); }

static int is_script_system(int sys) { return strcmp(data_sys_core(sys), "/bin/sh") == 0; }

/* ------------------------------------------------------------ list helpers */

void el_clear(EntryList *l) {
	for (int i = 0; i < l->n; i++) { free(l->v[i].name); free(l->v[i].path); }
	free(l->v);
	memset(l, 0, sizeof *l);
}
Entry *el_push(EntryList *l) {
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 64;
		l->v = xrealloc(l->v, sizeof(Entry) * (size_t)l->cap);
	}
	Entry *e = &l->v[l->n++];
	memset(e, 0, sizeof *e);
	return e;
}

static const char *JUNK_EXT[] = {
	"png", "jpg", "jpeg", "bmp", "gif", "txt", "nfo", "xml", "dat", "db", "json", "lpl", "srm", "sav",
	"state", "cfg", "ini", "log", "pdf", "ips", "bps", "ups", "htm", "html", "url", "opt", "rmp",
	"sh~", "bak", "tmp", "ds_store",
};
int data_is_junk(const char *name) {
	size_t l = strlen(name);
	if (l > 7 && str_ieq(name + l - 7, ".p8.png")) return 0; /* PICO-8 cartridges are PNG files */
	const char *ext = path_ext(name);
	for (int i = 0; i < ARRAY_LEN(JUNK_EXT); i++)
		if (str_ieq(ext, JUNK_EXT[i])) return 1;
	return 0;
}

/* Arcade full names from GarlicOS's CFW/config/mame.csv ("zip,Title"). */
static char **arc_keys, **arc_vals;
static int arc_n, arc_loaded;
static int arc_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void arcade_load(void) {
	if (arc_loaded) return;
	arc_loaded = 1;
	char p[512];
	snfmt(p, sizeof p, "%s/config/mame.csv", g_paths.cfw);
	char *txt = file_read_all(p, NULL);
	if (!txt) return;
	int cap = 0;
	char *line = txt;
	char **pairs = NULL;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = 0;
		char *comma = strchr(line, ',');
		if (comma) {
			*comma = 0;
			char *v = comma + 1;
			size_t vl = strlen(v);
			if (vl && v[vl - 1] == '\r') v[vl - 1] = 0;
			if (arc_n == cap) { cap = cap ? cap * 2 : 4096; pairs = xrealloc(pairs, sizeof(char *) * 2 * (size_t)cap); }
			pairs[arc_n * 2] = line;
			pairs[arc_n * 2 + 1] = v;
			arc_n++;
		}
		line = nl ? nl + 1 : NULL;
	}
	/* sort pairs by key */
	if ((arc_n) > 1) qsort(pairs, (size_t)arc_n, sizeof(char *) * 2, arc_cmp);
	arc_keys = xmalloc(sizeof(char *) * (size_t)(arc_n + 1));
	arc_vals = xmalloc(sizeof(char *) * (size_t)(arc_n + 1));
	for (int i = 0; i < arc_n; i++) { arc_keys[i] = pairs[i * 2]; arc_vals[i] = pairs[i * 2 + 1]; }
	free(pairs);
	/* txt stays allocated: the key/value pointers point into it */
}
static const char *arcade_name(const char *stem) {
	arcade_load();
	int lo = 0, hi = arc_n - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int c = strcmp(stem, arc_keys[mid]);
		if (!c) return arc_vals[mid];
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return NULL;
}
int data_arcade_known(const char *stem) { return arcade_name(stem) != NULL; }

int data_is_arcade_set(const char *stem) { return arcade_name(stem) != NULL; }

static int is_arcade(int sys) {
	static const char *A[] = {"ARCADE", "FBNEO", "FBA2012", "MAME2000", "CPS1", "CPS2", "CPS3", "NEOGEO"};
	if (sys < 0) return 0;
	for (int i = 0; i < ARRAY_LEN(A); i++)
		if (strcmp(g_sys[sys].code, A[i]) == 0) return 1;
	return 0;
}

void data_display_name(int sys, const char *path, int is_dir, char *out, size_t n) {
	if (sys >= 0 && g_sys[sys].is_import) { sncpy(out, path_base(path), n); return; }
	char stem[256];
	if (is_dir) sncpy(stem, path_base(path), sizeof stem);
	else path_stem(path, stem, sizeof stem);
	if (!is_dir && is_arcade(sys)) {
		const char *full = arcade_name(stem);
		if (full) {
			sncpy(stem, full, sizeof stem);
			if (!g_set.clean_names) { sncpy(out, stem, n); return; }
		}
	}
	if (!g_set.clean_names) { sncpy(out, stem, n); return; }
	char buf[256];
	size_t o = 0;
	int depth = 0;
	for (const char *p = stem; *p && o + 1 < sizeof buf; p++) {
		if (*p == '(' || *p == '[') { depth++; continue; }
		if ((*p == ')' || *p == ']') && depth) { depth--; continue; }
		if (depth) continue;
		if (*p == '_') { buf[o++] = ' '; continue; }
		if (*p == ' ' && o && buf[o - 1] == ' ') continue;
		buf[o++] = *p;
	}
	buf[o] = 0;
	str_trim(buf);
	sncpy(out, *buf ? buf : stem, n);
}

/* Files referenced by .cue/.m3u playlists are hidden so each game shows once. */
typedef struct { char **v; int n, cap; } NameSet;
static void ns_add(NameSet *s, const char *name) {
	if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 16; s->v = xrealloc(s->v, sizeof(char *) * (size_t)s->cap); }
	s->v[s->n++] = xstrdup(path_base(name));
}
static int ns_has(const NameSet *s, const char *name) {
	for (int i = 0; i < s->n; i++)
		if (str_ieq(s->v[i], name)) return 1;
	return 0;
}
static void ns_free(NameSet *s) {
	for (int i = 0; i < s->n; i++) free(s->v[i]);
	free(s->v);
	memset(s, 0, sizeof *s);
}
static void collect_refs(const char *dir, const char *file, NameSet *hide) {
	char p[1024];
	snfmt(p, sizeof p, "%s/%s", dir, file);
	if (file_size(p) > 65536) return;
	char *txt = file_read_all(p, NULL);
	if (!txt) return;
	int is_cue = str_ieq(path_ext(file), "cue");
	for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		str_trim(line);
		if (!*line || *line == '#') continue;
		if (is_cue) {
			if (strncasecmp(line, "FILE", 4) != 0) continue;
			char *q1 = strchr(line, '"');
			char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
			if (q1 && q2) { *q2 = 0; ns_add(hide, q1 + 1); }
		} else {
			ns_add(hide, line);
		}
	}
	free(txt);
}

typedef struct { char *name; uint8_t is_dir; } RawEnt;
static int reading_import; /* listing the Import folder: hide notes too */

static int read_dir(const char *dir, int scripts_only, RawEnt **out) {
	DIR *d = opendir(dir);
	if (!d) return 0;
	int n = 0, cap = 0;
	RawEnt *v = NULL;
	NameSet hide = {0};
	struct dirent *e;
	while ((e = readdir(d))) {
		const char *nm = e->d_name;
		if (nm[0] == '.') continue;
		int is_dir = e->d_type == DT_DIR;
		if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK) {
			char p[1024];
			snfmt(p, sizeof p, "%s/%s", dir, nm);
			is_dir = dir_exists(p);
		}
		if (is_dir) {
			if (str_ieq(nm, "Imgs") || scripts_only) continue;
		} else {
			if (data_is_junk(nm)) continue;
			if (reading_import) {
				char p[1024];
				snfmt(p, sizeof p, "%s/%s", dir, nm);
				if (import_ignored(p)) continue;
			}
			if (scripts_only && !str_ieq(path_ext(nm), "sh")) continue;
			/* only useful from the GarlicOS menu; pointless inside Clove */
			if (scripts_only && str_ieq(nm, "Switch to Clove.sh")) continue;
			const char *ext = path_ext(nm);
			if (str_ieq(ext, "cue") || str_ieq(ext, "m3u")) collect_refs(dir, nm, &hide);
		}
		if (n == cap) { cap = cap ? cap * 2 : 64; v = xrealloc(v, sizeof(RawEnt) * (size_t)cap); }
		v[n].name = xstrdup(nm);
		v[n].is_dir = (uint8_t)is_dir;
		n++;
	}
	closedir(d);
	if (hide.n) {
		int w = 0;
		for (int i = 0; i < n; i++) {
			if (!v[i].is_dir && ns_has(&hide, v[i].name)) { free(v[i].name); continue; }
			v[w++] = v[i];
		}
		n = w;
	}
	ns_free(&hide);
	*out = v;
	return n;
}

static void free_raw(RawEnt *v, int n) {
	for (int i = 0; i < n; i++) free(v[i].name);
	free(v);
}

static void sys_dir_on(int sys, const char *card, char *out, size_t n) {
	if (g_sys[sys].is_import) snfmt(out, n, "%s/Import", card);
	else snfmt(out, n, "%s/Roms/%s", card, g_sys[sys].code);
}

static void sys_dirs(int sys, char dirs[2][512], int *ndirs) {
	*ndirs = 0;
	sys_dir_on(sys, g_paths.mmc, dirs[*ndirs], 512);
	if (dir_exists(dirs[*ndirs])) (*ndirs)++;
	if (g_paths.sd_present) {
		sys_dir_on(sys, g_paths.sd, dirs[*ndirs], 512);
		if (dir_exists(dirs[*ndirs])) (*ndirs)++;
	}
}

void data_scan(void) {
	uint64_t t0 = now_ms();
	int total = 0;
	for (int i = 0; i < g_nsys; i++) {
		System *s = &g_sys[i];
		char p[512];
		sys_dir_on(i, g_paths.mmc, p, sizeof p);
		s->on_tf1 = dir_exists(p);
		sys_dir_on(i, g_paths.sd, p, sizeof p);
		s->on_tf2 = g_paths.sd_present && dir_exists(p);
		s->count = 0;
		char dirs[2][512];
		int nd;
		sys_dirs(i, dirs, &nd);
		reading_import = s->is_import;
		for (int k = 0; k < nd; k++) {
			RawEnt *v;
			int n = read_dir(dirs[k], is_script_system(i), &v);
			s->count += n;
			free_raw(v, n);
		}
		reading_import = 0;
		total += s->count;
	}
	if (g_nsys > 1) qsort(g_sys, (size_t)g_nsys, sizeof(System), cmp_sys);
	LOG("scanned %d systems, %d entries in %llu ms", g_nsys, total, (unsigned long long)(now_ms() - t0));
}

/* Consoles in menu order: Import first (only while it has things to sort),
 * then consoles with games, then the empty ones (unless hidden). */
int data_visible_systems(int *out, int max) {
	int n = 0;
	for (int pass = 0; pass < 3; pass++) {
		for (int i = 0; i < g_nsys && n < max; i++) {
			System *s = &g_sys[i];
			if (!s->on_tf1 && !s->on_tf2) continue;
			if (s->is_import) { if (pass == 0 && s->count > 0) out[n++] = i; continue; }
			if (sys_get_int(s->code, "hidden", 0)) continue;
			/* films live on the Videos tab while it is shown */
			if (g_set.tab_visible[TAB_VIDEOS] && !strcmp(s->code, "VIDEOS")) continue;
			if (pass == 1 && s->count > 0) out[n++] = i;
			if (pass == 2 && s->count == 0 && g_set.empty_consoles == 0) out[n++] = i;
		}
	}
	return n;
}

static int cmp_entry(const void *a, const void *b) {
	const Entry *x = a, *y = b;
	if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
	return str_icmp_natural(x->name, y->name);
}

void data_list_system(int sys, const char *dir, EntryList *out) {
	el_clear(out);
	char dirs[2][512];
	int nd = 0;
	if (dir && *dir) { sncpy(dirs[0], dir, 512); nd = 1; }
	else sys_dirs(sys, dirs, &nd);
	reading_import = sys >= 0 && g_sys[sys].is_import;
	for (int k = 0; k < nd; k++) {
		RawEnt *v;
		int n = read_dir(dirs[k], is_script_system(sys), &v);
		for (int i = 0; i < n; i++) {
			Entry *e = el_push(out);
			char full[1024], disp[256];
			snfmt(full, sizeof full, "%s/%s", dirs[k], v[i].name);
			data_display_name(sys, full, v[i].is_dir, disp, sizeof disp);
			e->name = xstrdup(disp);
			e->path = xstrdup(full);
			e->sys = sys;
			e->is_dir = v[i].is_dir;
			e->fav = !e->is_dir && data_is_fav(full);
		}
		free_raw(v, n);
	}
	reading_import = 0;
	if (out->n > 1) qsort(out->v, (size_t)out->n, sizeof(Entry), cmp_entry);
}

/* -------------------------------------------------------------- favourites */

static cJSON *fav_root;
static char fav_path[512];

static void fav_load(void) {
	snfmt(fav_path, sizeof fav_path, "%s/retroarch/.retroarch/content_favorites.lpl", g_paths.cfw);
	char *txt = file_read_all(fav_path, NULL);
	fav_root = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
	if (!cJSON_IsObject(fav_root)) {
		cJSON_Delete(fav_root);
		fav_root = cJSON_CreateObject();
		cJSON_AddStringToObject(fav_root, "version", "1.5");
		cJSON_AddStringToObject(fav_root, "default_core_path", "");
		cJSON_AddStringToObject(fav_root, "default_core_name", "");
		cJSON_AddNumberToObject(fav_root, "label_display_mode", 0);
		cJSON_AddNumberToObject(fav_root, "right_thumbnail_mode", 0);
		cJSON_AddNumberToObject(fav_root, "left_thumbnail_mode", 0);
		cJSON_AddNumberToObject(fav_root, "sort_mode", 0);
	}
	if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(fav_root, "items"))) {
		cJSON_DeleteItemFromObjectCaseSensitive(fav_root, "items");
		cJSON_AddArrayToObject(fav_root, "items");
	}
}

static void fav_save(void) {
	char *txt = cJSON_Print(fav_root);
	if (file_write_all(fav_path, txt, strlen(txt)) != 0) LOG("could not save favourites");
	free(txt);
}

int data_is_fav(const char *path) {
	cJSON *items = cJSON_GetObjectItemCaseSensitive(fav_root, "items");
	cJSON *it;
	cJSON_ArrayForEach(it, items) {
		cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "path");
		if (cJSON_IsString(p) && strcmp(p->valuestring, path) == 0) return 1;
	}
	return 0;
}

static void core_path(const char *core, char *out, size_t n) {
	if (core[0] == '/') sncpy(out, core, n);
	else snfmt(out, n, "%s/retroarch/.retroarch/cores/%s", g_paths.cfw, core);
}

int data_fav_toggle(const Entry *e) {
	cJSON *items = cJSON_GetObjectItemCaseSensitive(fav_root, "items");
	int idx = 0;
	cJSON *it;
	cJSON_ArrayForEach(it, items) {
		cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "path");
		if (cJSON_IsString(p) && strcmp(p->valuestring, e->path) == 0) {
			cJSON_DeleteItemFromArray(items, idx);
			fav_save();
			return 0;
		}
		idx++;
	}
	cJSON *o = cJSON_CreateObject();
	char cp[512];
	core_path(data_sys_core(e->sys), cp, sizeof cp);
	cJSON_AddStringToObject(o, "path", e->path);
	cJSON_AddStringToObject(o, "label", e->name);
	cJSON_AddStringToObject(o, "core_path", cp);
	cJSON_AddStringToObject(o, "core_name", data_core_label(data_sys_core(e->sys)));
	cJSON_AddStringToObject(o, "crc32", "DETECT");
	cJSON_AddStringToObject(o, "db_name", "");
	cJSON_AddItemToArray(items, o);
	fav_save();
	return 1;
}

static void add_listed(EntryList *out, const char *path) {
	for (int i = 0; i < out->n; i++)
		if (strcmp(out->v[i].path, path) == 0) return;
	if (!file_exists(path) && !dir_exists(path)) return;
	int sys = data_sys_from_path(path);
	if (sys < 0) return;
	Entry *e = el_push(out);
	char disp[256];
	data_display_name(sys, path, 0, disp, sizeof disp);
	e->name = xstrdup(disp);
	e->path = xstrdup(path);
	e->sys = sys;
	e->fav = data_is_fav(path);
}

void data_list_favorites(EntryList *out) {
	el_clear(out);
	cJSON *items = cJSON_GetObjectItemCaseSensitive(fav_root, "items");
	cJSON *it;
	cJSON_ArrayForEach(it, items) {
		cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "path");
		if (cJSON_IsString(p)) add_listed(out, p->valuestring);
	}
	for (int i = 0; i < out->n; i++) out->v[i].fav = 1;
	if (out->n > 1) qsort(out->v, (size_t)out->n, sizeof(Entry), cmp_entry);
}

/* ----------------------------------------------------------------- recents */

static void recents_path(char *out, size_t n) { snfmt(out, n, "%s/recents.json", g_paths.clove); }

static cJSON *recents_load(void) {
	char p[512];
	recents_path(p, sizeof p);
	char *txt = file_read_all(p, NULL);
	cJSON *j = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
	if (!cJSON_IsArray(j)) { cJSON_Delete(j); j = cJSON_CreateArray(); }
	return j;
}
static void recents_save(cJSON *j) {
	char p[512];
	recents_path(p, sizeof p);
	char *txt = cJSON_Print(j);
	file_write_all(p, txt, strlen(txt));
	free(txt);
}

static void ra_history(const char *card, cJSON **out) {
	char p[512];
	snfmt(p, sizeof p, "%s/Saves/CurrentProfile/lists/content_history.lpl", card);
	char *txt = file_read_all(p, NULL);
	*out = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
}

void data_list_recents(EntryList *out) {
	el_clear(out);
	cJSON *mine = recents_load();
	cJSON *it;
	cJSON_ArrayForEach(it, mine) {
		cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "path");
		if (cJSON_IsString(p)) add_listed(out, p->valuestring);
	}
	cJSON_Delete(mine);
	/* Also show games started from GarlicOS's own menu or RetroArch. */
	const char *cards[2] = {g_paths.sd, g_paths.mmc};
	for (int c = 0; c < 2; c++) {
		if (c == 0 && !g_paths.sd_present) continue;
		cJSON *h;
		ra_history(cards[c], &h);
		cJSON *items = cJSON_GetObjectItemCaseSensitive(h, "items");
		cJSON_ArrayForEach(it, items) {
			cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "path");
			if (cJSON_IsString(p) && out->n < 60) add_listed(out, p->valuestring);
		}
		cJSON_Delete(h);
	}
}

static void recents_add(const char *path) {
	cJSON *j = recents_load();
	int idx = 0;
	cJSON *it;
	cJSON_ArrayForEach(it, j) {
		cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "path");
		if (cJSON_IsString(p) && strcmp(p->valuestring, path) == 0) { cJSON_DeleteItemFromArray(j, idx); break; }
		idx++;
	}
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, "path", path);
	cJSON_AddNumberToObject(o, "time", (double)time(NULL));
	cJSON_InsertItemInArray(j, 0, o);
	while (cJSON_GetArraySize(j) > 50) cJSON_DeleteItemFromArray(j, cJSON_GetArraySize(j) - 1);
	recents_save(j);
	cJSON_Delete(j);
}

void data_recent_remove(const char *path) {
	cJSON *j = recents_load();
	int idx = 0;
	cJSON *it;
	cJSON_ArrayForEach(it, j) {
		cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "path");
		if (cJSON_IsString(p) && strcmp(p->valuestring, path) == 0) { cJSON_DeleteItemFromArray(j, idx); break; }
		idx++;
	}
	recents_save(j);
	cJSON_Delete(j);
	const char *cards[2] = {g_paths.sd, g_paths.mmc};
	for (int c = 0; c < 2; c++) {
		cJSON *h;
		ra_history(cards[c], &h);
		cJSON *items = cJSON_GetObjectItemCaseSensitive(h, "items");
		int i = 0, changed = 0;
		cJSON_ArrayForEach(it, items) {
			cJSON *p = cJSON_GetObjectItemCaseSensitive(it, "path");
			if (cJSON_IsString(p) && strcmp(p->valuestring, path) == 0) { cJSON_DeleteItemFromArray(items, i); changed = 1; break; }
			i++;
		}
		if (changed) {
			char p[512];
			snfmt(p, sizeof p, "%s/Saves/CurrentProfile/lists/content_history.lpl", cards[c]);
			char *txt = cJSON_Print(h);
			file_write_all(p, txt, strlen(txt));
			free(txt);
		}
		cJSON_Delete(h);
	}
}

/* -------------------------------------------------------------------- art */

int data_art_path(const Entry *e, char *out, size_t n) {
	if (e->is_dir) return 0;
	char dir[512], stem[256];
	path_dir(e->path, dir, sizeof dir);
	path_stem(e->path, stem, sizeof stem);
	static const char *ext[] = {"png", "jpg"};
	for (int i = 0; i < 2; i++) {
		snfmt(out, n, "%s/Imgs/%s.%s", dir, stem, ext[i]);
		if (file_exists(out)) return 1;
	}
	/* Games in sub-folders: GarlicOS keeps art in the system's top Imgs folder. */
	const char *r = strstr(e->path, "/Roms/");
	if (r && e->sys >= 0) {
		char card[512];
		sncpy(card, e->path, (size_t)(r - e->path) + 1);
		for (int i = 0; i < 2; i++) {
			snfmt(out, n, "%s/Roms/%s/Imgs/%s.%s", card, g_sys[e->sys].code, stem, ext[i]);
			if (file_exists(out)) return 1;
		}
	}
	return 0;
}

int data_icon_path(int sys, char *out, size_t n) {
	snfmt(out, n, "%s/system/%s.png", g_theme.dir, g_sys[sys].code);
	if (file_exists(out)) return 1;
	snfmt(out, n, "%s/skin/system/%s.png", g_paths.cfw, g_sys[sys].code);
	if (file_exists(out)) return 1;
	snfmt(out, n, "%s/icons/%s.png", g_paths.clove, g_sys[sys].code);
	return file_exists(out);
}

/* ------------------------------------------------------------------ cores */

typedef struct { char file[128]; char label[64]; char exts[256]; } CoreInfo;
static CoreInfo *cinfo;
static int ncinfo;

static CoreInfo *core_info(const char *file) {
	for (int i = 0; i < ncinfo; i++)
		if (strcmp(cinfo[i].file, file) == 0) return &cinfo[i];
	cinfo = xrealloc(cinfo, sizeof(CoreInfo) * (size_t)(ncinfo + 1));
	CoreInfo *c = &cinfo[ncinfo++];
	memset(c, 0, sizeof *c);
	sncpy(c->file, file, sizeof c->file);
	char stem[128];
	path_stem(file, stem, sizeof stem);
	sncpy(c->label, stem, sizeof c->label);
	char *suffix = strstr(c->label, "_libretro");
	if (suffix) *suffix = 0;
	char p[512];
	snfmt(p, sizeof p, "%s/retroarch/.retroarch/core_info/%s.info", g_paths.cfw, stem);
	char *txt = file_read_all(p, NULL);
	if (txt) {
		for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
			char *eq = strchr(line, '=');
			if (!eq) continue;
			*eq = 0;
			char key[64], val[256];
			sncpy(key, line, sizeof key);
			str_trim(key);
			sncpy(val, eq + 1, sizeof val);
			str_trim(val);
			size_t l = strlen(val);
			if (l >= 2 && val[0] == '"' && val[l - 1] == '"') { val[l - 1] = 0; memmove(val, val + 1, l - 1); }
			if (strcmp(key, "corename") == 0) sncpy(c->label, val, sizeof c->label);
			if (strcmp(key, "supported_extensions") == 0) sncpy(c->exts, val, sizeof c->exts);
		}
		free(txt);
	}
	return c;
}

const char *data_core_label(const char *core_file) {
	if (strcmp(core_file, "/bin/sh") == 0) return "Shell script";
	return core_info(core_file)->label;
}

static int ext_in(const char *ext, const char *list) {
	char buf[256];
	sncpy(buf, list, sizeof buf);
	for (char *t = strtok(buf, "|"); t; t = strtok(NULL, "|"))
		if (str_ieq(t, ext)) return 1;
	return 0;
}

int data_cores_for_system(int sys, StrList *out) {
	sl_free(out);
	const char *def = g_sys[sys].core;
	if (*def) sl_push(out, def);
	if (is_script_system(sys) || g_sys[sys].is_import) return out->n;
	/* gather the file extensions present in this system's folders */
	StrList exts = {0};
	char dirs[2][512];
	int nd;
	sys_dirs(sys, dirs, &nd);
	for (int k = 0; k < nd; k++) {
		RawEnt *v;
		int n = read_dir(dirs[k], 0, &v);
		for (int i = 0; i < n && exts.n < 16; i++) {
			if (v[i].is_dir) continue;
			char e[32];
			sncpy(e, path_ext(v[i].name), sizeof e);
			str_lower(e);
			if (*e && sl_find(&exts, e) < 0) sl_push(&exts, e);
		}
		free_raw(v, n);
	}
	char cdir[512];
	snfmt(cdir, sizeof cdir, "%s/retroarch/.retroarch/cores", g_paths.cfw);
	DIR *d = opendir(cdir);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (!strstr(e->d_name, "_libretro.so") || sl_find(out, e->d_name) >= 0) continue;
			CoreInfo *ci = core_info(e->d_name);
			int ok = 0;
			for (int i = 0; i < exts.n && !ok; i++)
				if (strcmp(exts.v[i], "zip") != 0 && strcmp(exts.v[i], "7z") != 0 && ext_in(exts.v[i], ci->exts)) ok = 1;
			if (ok) sl_push(out, e->d_name);
		}
		closedir(d);
	}
	sl_free(&exts);
	return out->n;
}

/* ----------------------------------------------------------- retroarch.cfg */

typedef struct { StrList lines; char path[512]; int dirty; } Cfg;

static void cfg_load(Cfg *c, const char *path) {
	memset(c, 0, sizeof *c);
	sncpy(c->path, path, sizeof c->path);
	char *txt = file_read_all(path, NULL);
	if (!txt) return;
	for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
		size_t l = strlen(line);
		if (l && line[l - 1] == '\r') line[l - 1] = 0;
		sl_push(&c->lines, line);
	}
	free(txt);
}
static int cfg_key_match(const char *line, const char *key) {
	size_t kl = strlen(key);
	if (strncmp(line, key, kl) != 0) return 0;
	const char *p = line + kl;
	while (*p == ' ') p++;
	return *p == '=';
}
static int cfg_get(Cfg *c, const char *key, char *out, size_t n) {
	for (int i = 0; i < c->lines.n; i++) {
		if (!cfg_key_match(c->lines.v[i], key)) continue;
		const char *q1 = strchr(c->lines.v[i], '"');
		const char *q2 = q1 ? strrchr(q1 + 1, '"') : NULL;
		if (q1 && q2) {
			size_t l = (size_t)(q2 - q1 - 1);
			sncpy(out, q1 + 1, MIN(n, l + 1));
		} else {
			const char *eq = strchr(c->lines.v[i], '=');
			sncpy(out, eq + 1, n);
			str_trim(out);
		}
		return 1;
	}
	return 0;
}
static void cfg_set(Cfg *c, const char *key, const char *value) {
	char line[1024];
	snfmt(line, sizeof line, "%s = \"%s\"", key, value);
	for (int i = 0; i < c->lines.n; i++) {
		if (!cfg_key_match(c->lines.v[i], key)) continue;
		if (strcmp(c->lines.v[i], line) != 0) {
			free(c->lines.v[i]);
			c->lines.v[i] = xstrdup(line);
			c->dirty = 1;
		}
		return;
	}
	sl_push(&c->lines, line);
	c->dirty = 1;
}
static void cfg_save(Cfg *c) {
	if (c->dirty) {
		size_t total = 0;
		for (int i = 0; i < c->lines.n; i++) total += strlen(c->lines.v[i]) + 1;
		char *buf = xmalloc(total + 1);
		size_t o = 0;
		for (int i = 0; i < c->lines.n; i++) {
			size_t l = strlen(c->lines.v[i]);
			memcpy(buf + o, c->lines.v[i], l);
			buf[o + l] = '\n';
			o += l + 1;
		}
		if (file_write_all(c->path, buf, o) != 0) LOG("could not write %s", c->path);
		free(buf);
	}
	sl_free(&c->lines);
}

/* Settings Clove can change per system. Values are written into retroarch.cfg
 * at each launch; the user's own global value is remembered as a baseline
 * and restored for systems without an override. */
typedef struct { const char *sys_key, *ra_key; } Managed;
static const Managed MANAGED[] = {
	{"filter", "video_dingux_rs90_softfilter_type"},
	{"integer", "video_scale_integer"},
	{"threaded", "video_threaded"},
	{"latency", "audio_latency"},
	{"fps", "fps_show"},
	/* GarlicOS's RetroArch manages the CPU itself ("Balanced" switches back to the
	 * interactive governor). For a fixed clock, tell it to leave the CPU alone. */
	{"cpu_mode", "cpu_scaling_mode"},
};

static int sys_override(int sys, const char *key, char *out, size_t n) {
	const char *code = g_sys[sys].code;
	if (strcmp(key, "cpu_mode") == 0) {
		if (data_sys_cpu(sys) > 0) { sncpy(out, "5", n); return 1; } /* 5 = manual */
		return 0;
	}
	int v = sys_get_int(code, key, -1);
	if (strcmp(key, "filter") == 0) { if (v == 1) { sncpy(out, "0", n); return 1; } return 0; }
	if (strcmp(key, "latency") == 0) { if (v > 0) { snfmt(out, n, "%d", v); return 1; } return 0; }
	if (strcmp(key, "fps") == 0) { if (v == 1) { sncpy(out, "true", n); return 1; } return 0; }
	if (v == 0 || v == 1) { sncpy(out, v ? "true" : "false", n); return 1; }
	return 0;
}

static void apply_managed(Cfg *c, int sys) {
	cJSON *ra = cJSON_GetObjectItemCaseSensitive(g_set.root, "retroarch");
	if (!ra) ra = cJSON_AddObjectToObject(g_set.root, "retroarch");
	cJSON *base = cJSON_GetObjectItemCaseSensitive(ra, "baseline");
	if (!base) base = cJSON_AddObjectToObject(ra, "baseline");
	cJSON *last = cJSON_GetObjectItemCaseSensitive(ra, "last");
	if (!last) last = cJSON_AddObjectToObject(ra, "last");
	for (int i = 0; i < ARRAY_LEN(MANAGED); i++) {
		const char *k = MANAGED[i].ra_key;
		char cur[128] = "";
		int has = cfg_get(c, k, cur, sizeof cur);
		cJSON *b = cJSON_GetObjectItemCaseSensitive(base, k);
		cJSON *l = cJSON_GetObjectItemCaseSensitive(last, k);
		/* The user changed it in RetroArch since our last launch: adopt it. */
		if (has && (!b || (cJSON_IsString(l) && strcmp(l->valuestring, cur) != 0))) {
			cJSON_DeleteItemFromObjectCaseSensitive(base, k);
			cJSON_AddStringToObject(base, k, cur);
			b = cJSON_GetObjectItemCaseSensitive(base, k);
		}
		char want[128];
		if (!sys_override(sys, MANAGED[i].sys_key, want, sizeof want)) {
			if (!cJSON_IsString(b)) continue;
			sncpy(want, b->valuestring, sizeof want);
		}
		cfg_set(c, k, want);
		cJSON_DeleteItemFromObjectCaseSensitive(last, k);
		cJSON_AddStringToObject(last, k, want);
	}
}

static void prepare_retroarch(const char *rom_path, int sys) {
	char p[512];
	snfmt(p, sizeof p, "%s/retroarch/.retroarch/retroarch.cfg", g_paths.cfw);
	Cfg c;
	cfg_load(&c, p);
	if (!c.lines.n) { LOG("retroarch.cfg not found at %s", p); sl_free(&c.lines); return; }
	/* Same rule as GarlicOS: BIOS, saves and states live on the card the game is on. */
	const char *card = (g_paths.sd_present && rom_path && str_starts_with(rom_path, g_paths.sd)) ? g_paths.sd : g_paths.mmc;
	char v[600];
	snfmt(v, sizeof v, "%s/BIOS", card); cfg_set(&c, "system_directory", v);
	snfmt(v, sizeof v, "%s/Saves/CurrentProfile/saves", card); cfg_set(&c, "savefile_directory", v); mkdir_p(v);
	snfmt(v, sizeof v, "%s/Saves/CurrentProfile/states", card); cfg_set(&c, "savestate_directory", v); mkdir_p(v);
	snfmt(v, sizeof v, "%s/Saves/CurrentProfile/lists", card); cfg_set(&c, "content_history_directory", v); mkdir_p(v);
	snfmt(v, sizeof v, "%s/Saves/CurrentProfile/lists/content_history.lpl", card); cfg_set(&c, "content_history_path", v);
	snfmt(v, sizeof v, "%s/Screenshots", card); cfg_set(&c, "screenshot_directory", v);
	if (sys >= 0) apply_managed(&c, sys);
	/* Continue where you left off: RetroArch saves a state when the game is
	 * closed and loads it at the next start ("Start over" skips it once). */
	if (rom_path) {
		cfg_set(&c, "savestate_auto_save", "true");
		cfg_set(&c, "savestate_auto_load", g_set.resume_games && !data_start_over ? "true" : "false");
	}
	data_start_over = 0;
	cfg_save(&c);
}

int data_start_over;

/* The state RetroArch saved when this game was last closed:
 * <card>/Saves/CurrentProfile/states/<core>/<game>.state.auto (GarlicOS sorts
 * states into a folder per core). Cores set never to load it (the arcade ones
 * in GarlicOS) don't count. */
int data_resume_point(const Entry *e, char *thumb, size_t n, time_t *when) {
	static char last_path[1024], last_thumb[1024];
	static time_t last_when;
	static int last_r;
	static uint64_t last_at;
	if (thumb && n) thumb[0] = 0;
	if (when) *when = 0;
	if (!e || e->is_dir || !g_set.resume_games) return 0;
	uint64_t now = plat_ticks();
	if (strcmp(last_path, e->path) || now - last_at > 5000) {
		sncpy(last_path, e->path, sizeof last_path);
		last_at = now;
		last_r = 0;
		last_thumb[0] = 0;
		last_when = 0;
		const char *card = (g_paths.sd_present && str_starts_with(e->path, g_paths.sd)) ? g_paths.sd : g_paths.mmc;
		char dir[600], stem[256];
		snfmt(dir, sizeof dir, "%s/Saves/CurrentProfile/states", card);
		path_stem(e->path, stem, sizeof stem);
		DIR *d = opendir(dir);
		struct dirent *de;
		while (d && (de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			char sp[1200];
			snfmt(sp, sizeof sp, "%s/%s/%s.state.auto", dir, de->d_name, stem);
			struct stat st;
			if (stat(sp, &st) != 0 || !S_ISREG(st.st_mode)) continue;
			char cfgp[800], buf[64];
			snfmt(cfgp, sizeof cfgp, "%s/retroarch/.retroarch/config/%s/%s.cfg", g_paths.cfw, de->d_name, de->d_name);
			Cfg oc;
			cfg_load(&oc, cfgp);
			int off = cfg_get(&oc, "savestate_auto_load", buf, sizeof buf) && !strcmp(buf, "false");
			sl_free(&oc.lines);
			if (off) continue;
			if (st.st_mtime > last_when) {
				last_when = st.st_mtime;
				last_r = 1;
				snfmt(last_thumb, sizeof last_thumb, "%s.png", sp);
				if (!file_exists(last_thumb)) last_thumb[0] = 0;
			}
		}
		if (d) closedir(d);
	}
	if (thumb && n) sncpy(thumb, last_thumb, n);
	if (when) *when = last_when;
	return last_r;
}

/* The video player (RetroArch's FFmpeg core) gets simple buttons of its own,
 * written to its override and remap files each time a video starts (nothing
 * else in RetroArch changes):
 *   A pause / play, B back to Clove, MENU RetroArch's menu,
 *   L1 / R1 skip back / forward 10 s, L2 / R2 a minute (the D-pad still works).
 * RetroArch's other hotkeys are switched off for videos, so nothing needs MENU
 * held and no button saves states, changes slots or toggles fast-forward. */
static int ends_with(const char *s, const char *suf) {
	size_t a = strlen(s), b = strlen(suf);
	return a >= b && !strcmp(s + a - b, suf);
}
static void video_controls(void) {
	char rp[512], dir[600], p[700], a[32], b[32], menu[32];
	snfmt(rp, sizeof rp, "%s/retroarch/.retroarch/retroarch.cfg", g_paths.cfw);
	Cfg mainc;
	cfg_load(&mainc, rp);
	if (!cfg_get(&mainc, "input_player1_a_btn", a, sizeof a) || !strcmp(a, "nul")) sncpy(a, "0", sizeof a);
	if (!cfg_get(&mainc, "input_player1_b_btn", b, sizeof b) || !strcmp(b, "nul")) sncpy(b, "1", sizeof b);
	if (!cfg_get(&mainc, "input_enable_hotkey_btn", menu, sizeof menu) || !strcmp(menu, "nul")) sncpy(menu, "9", sizeof menu);

	snfmt(dir, sizeof dir, "%s/retroarch/.retroarch/config/FFmpeg", g_paths.cfw);
	mkdir_p(dir);
	snfmt(p, sizeof p, "%s/FFmpeg.cfg", dir);
	Cfg o;
	cfg_load(&o, p);
	for (int i = 0; i < mainc.lines.n; i++) {
		const char *l = mainc.lines.v[i];
		if (strncmp(l, "input_", 6) != 0 || !strncmp(l, "input_player", 12)) continue;
		char key[96];
		size_t k = 0;
		while (l[k] && l[k] != ' ' && l[k] != '=' && k < sizeof key - 1) { key[k] = l[k]; k++; }
		key[k] = 0;
		if (ends_with(key, "_btn") || ends_with(key, "_axis") || ends_with(key, "_mbtn")) cfg_set(&o, key, "nul");
	}
	cfg_set(&o, "input_enable_hotkey_btn", "nul");
	/* the same buttons as in Zappo: its confirm button pauses, its back button
	 * leaves (they follow Settings > Controls > Confirm button) */
	const char *confirm = g_set.swap_ab ? b : a, *back = g_set.swap_ab ? a : b;
	cfg_set(&o, "input_pause_toggle_btn", confirm);
	/* back closes the film the way the player closes it itself at the end
	 * (RetroArch then quits, quit_on_close_content). Quitting RetroArch
	 * outright with the film still running restarted the handheld. */
	cfg_set(&o, "input_close_content_btn", back);
	cfg_set(&o, "input_exit_emulator_btn", "nul");
	cfg_set(&o, "load_dummy_on_core_shutdown", "false");
	cfg_set(&o, "input_menu_toggle_btn", menu);
	cfg_set(&o, "quit_press_twice", "false");
	cfg_set(&o, "quit_on_close_content", "1");
	/* leaving a video should be quick and quiet: nothing to write back, no states */
	cfg_set(&o, "config_save_on_exit", "false");
	cfg_set(&o, "savestate_auto_save", "false");
	cfg_set(&o, "savestate_auto_load", "false");
	cfg_save(&o);
	/* RetroArch drops this override before it decides whether to quit after
	 * closing the film, so the main settings must say so too (it only matters
	 * when content is closed, which games leave by their exit button) */
	cfg_set(&mainc, "quit_on_close_content", "1");
	cfg_save(&mainc);
	sl_free(&mainc.lines);

	/* the same skips as the music player: LEFT / RIGHT 10 s (the core's own),
	 * L1 / R1 a minute: RetroPad L -> DOWN (-60 s), R -> UP (+60 s). L2 / R2 stay
	 * themselves (the player doesn't use them). */
	snfmt(dir, sizeof dir, "%s/retroarch/.retroarch/config/remaps/FFmpeg", g_paths.cfw);
	mkdir_p(dir);
	snfmt(p, sizeof p, "%s/FFmpeg.rmp", dir);
	Cfg r;
	cfg_load(&r, p);
	cfg_set(&r, "input_player1_btn_l", "5");
	cfg_set(&r, "input_player1_btn_r", "4");
	cfg_set(&r, "input_player1_btn_l2", "12");
	cfg_set(&r, "input_player1_btn_r2", "13");
	cfg_save(&r);
}

static int write_command(const char *body) {
	char p[512];
	snfmt(p, sizeof p, "%s/retroarch/command.sh", g_paths.cfw);
	if (file_write_all(p, body, strlen(body)) != 0) return -1;
	chmod(p, 0755);
#if defined(__wasm__) || defined(ZAPPO_TABLET)
	{ void web_launch(const char *body); web_launch(body); } /* the page shows what would start */
#endif
#ifndef __wasm__
	sync();
#endif
	return 0;
}

static void cpu_lines(int khz, char *out, size_t n) {
	int f[32];
	int nf = plat_cpu_freqs(f, 32);
	int top = nf ? f[nf - 1] : 1488000;
	if (top > 1488000) {
		/* never go above what GarlicOS's own governor table uses */
		for (int i = nf - 1; i >= 0; i--) if (f[i] <= 1488000) { top = f[i]; break; }
	}
	if (khz <= 0) {
		snfmt(out, n,
		      "# CPU: GarlicOS automatic (interactive governor)\n"
		      "echo interactive > $CPU/scaling_governor\n"
		      "echo %d > $CPU/scaling_max_freq\n", top);
		return;
	}
	int pick = 0;
	for (int i = 0; i < nf; i++) if (f[i] <= khz) pick = f[i];
	if (!pick) pick = MIN(khz, top);
	snfmt(out, n,
	      "# CPU: fixed %d MHz for steady frame pacing\n"
	      "echo performance > $CPU/scaling_governor\n"
	      "echo %d > $CPU/scaling_max_freq\n", pick / 1000, pick);
}

char data_next_video[2][1024];

int data_launch(const Entry *e, char *err, size_t errn) {
	int sys = e->sys;
	if (sys < 0) { snfmt(err, errn, "Unknown system for this game"); return -1; }
	if (g_sys[sys].is_import) { snfmt(err, errn, "Choose a console for this game first"); return -1; }
	const char *core = data_sys_core(sys);
	if (!*core) { snfmt(err, errn, "No emulator set for %s", g_sys[sys].code); return -1; }
	char cp[512], qrom[1200], cpu[256], body[6000];
	core_path(core, cp, sizeof cp);
	if (!file_exists(cp)) { snfmt(err, errn, "Emulator not installed: %s", core); return -1; }
	shell_squote(e->path, qrom, sizeof qrom);
	cpu_lines(data_sys_cpu(sys), cpu, sizeof cpu);
	if (strcmp(core, "/bin/sh") == 0) {
		snfmt(body, sizeof body,
		      "#!/bin/sh\n# Written by Clove %s\nCPU=/sys/devices/system/cpu/cpu0/cpufreq\n%s"
		      "cd \"$(dirname '%s')\"\n/bin/sh '%s'\n",
		      CLOVE_VERSION, cpu, qrom, qrom);
	} else {
		prepare_retroarch(e->path, sys);
		int video = strstr(core, "ffmpeg") != NULL;
		if (video) video_controls();
		/* Videos: the player is known to fall over as it closes (after the film
		 * has finished), and GarlicOS answers a player that died by restarting
		 * the handheld. Reporting success instead keeps it on. */
		/* A film is not picked up again at the next switch-on the way a game
		 * is (GarlicOS re-runs this file after a clean power-off): a video's
		 * command removes itself as it starts. */
		int len = snfmt(body, sizeof body,
		      "#!/bin/sh\n# Written by Clove %s\n%sCPU=/sys/devices/system/cpu/cpu0/cpufreq\n%s"
		      "%s/retroarch/retroarch -L \"%s\" '%s'\n",
		      CLOVE_VERSION, video ? "rm -f \"$0\"\n" : "", cpu, g_paths.cfw, cp, qrom);
		if (video) {
			/* Up next: when the film plays to its end (the player says SHUTDOWN
			 * in its log; B to leave does not), the next episode follows. Each
			 * one is noted so it counts as watched. */
			for (int k = 0; k < 2 && data_next_video[k][0] && len < (int)sizeof body - 1600; k++) {
				char qn[1200];
				shell_squote(data_next_video[k], qn, sizeof qn);
				len += snfmt(body + len, sizeof body - (size_t)len,
				             "grep -q 'Environ\\]: SHUTDOWN' %s/retroarch/.retroarch/logs/retroarch.log || exit 0\n"
				             "echo '%s' >> %s/watched-next.txt\n"
				             "%s/retroarch/retroarch -L \"%s\" '%s'\n",
				             g_paths.cfw, qn, g_paths.clove, g_paths.cfw, cp, qn);
			}
			if (len < (int)sizeof body - 20) snfmt(body + len, sizeof body - (size_t)len, "sync\nexit 0\n");
		}
	}
	data_next_video[0][0] = data_next_video[1][0] = 0;
	if (write_command(body) != 0) { snfmt(err, errn, "Could not write command.sh"); return -1; }
	recents_add(e->path);
	return 0;
}

int data_launch_retroarch(void) {
	char cpu[256], body[1024];
	prepare_retroarch(NULL, -1);
	cpu_lines(0, cpu, sizeof cpu);
	snfmt(body, sizeof body, "#!/bin/sh\n# Written by Clove %s\nCPU=/sys/devices/system/cpu/cpu0/cpufreq\n%s%s/retroarch/retroarch\n",
	      CLOVE_VERSION, cpu, g_paths.cfw);
	return write_command(body);
}

/* ------------------------------------------------------------------- BIOS */

/* Files each emulator looks for in BIOS. "a|b" = either one, "a+b" = both,
 * "dir/" = a folder. Only the ones GarlicOS's cores really need are marked
 * required; optional ones improve accuracy. */
typedef struct { const char *code, *files; int required; } BiosReq;
static const BiosReq BIOS_REQ[] = {
	{"PS", "scph1001.bin|scph5501.bin|scph7001.bin|scph5500.bin|scph5502.bin|scph101.bin|psxonpsp660.bin", 0},
	{"GBA", "gba_bios.bin", 0},
	{"GPSP", "gba_bios.bin", 0},
	{"SGB", "sgb_bios.bin", 0},
	{"POKE", "bios.min", 0},
	{"SEVENTYEIGHTHUNDRED", "7800 BIOS (U).rom", 0},
	{"AMIGA", "kick34005.A500|kick40063.A600|kick40068.A1200", 0},
	{"FDS", "disksys.rom", 1},
	{"SEGACD", "bios_CD_U.bin|bios_CD_E.bin|bios_CD_J.bin", 1},
	{"PCECD", "syscard3.pce", 1},
	{"NEOGEO", "neogeo.zip", 1},
	{"NEOCD", "neocd/", 1},
	{"LYNX", "lynxboot.img", 1},
	{"FIFTYTWOHUNDRED", "5200.rom", 1},
	{"INTELLIVISION", "exec.bin+grom.bin", 1},
	{"ODYSSEY", "o2rom.bin", 1},
	{"VIDEOPAC", "o2rom.bin", 1},
	{"FAIRCHILD", "sl31253.bin+sl31254.bin", 1},
	{"AMIGACD", "kick40060.CD32", 1},
	{"PANASONIC", "panafz10.bin|panafz1.bin|panafz10-norsa.bin|panafz10e-anvil.bin|goldstar.bin|sanyotry.bin", 1},
	{"ATARIST", "tos.img", 1},
	{"SUFAMI", "STBIOS.bin", 1},
	{"SATELLAVIEW", "BS-X.bin", 1},
	{"PCFX", "pcfx.rom", 1},
	{"X68000", "keropi/iplrom.dat", 1},
	{"COLECO", "Machines/", 1},
	{"MSX", "Machines/", 1},
};

static int bios_present(const char *name, int sys) {
	const char *cards[2] = {g_paths.mmc, g_paths.sd};
	for (int c = 0; c < 2; c++) {
		if (c == 1 && !g_paths.sd_present) continue;
		char p[768];
		size_t l = strlen(name);
		if (l && name[l - 1] == '/') {
			snfmt(p, sizeof p, "%s/BIOS/%.*s", cards[c], (int)(l - 1), name);
			if (dir_exists(p)) return 1;
			continue;
		}
		snfmt(p, sizeof p, "%s/BIOS/%s", cards[c], name);
		if (file_exists(p)) return 1;
		/* Neo Geo: neogeo.zip next to the games works too */
		snfmt(p, sizeof p, "%s/Roms/%s/%s", cards[c], g_sys[sys].code, name);
		if (file_exists(p)) return 1;
	}
	return 0;
}

int data_bios_status(int sys, char *files, size_t n) {
	if (files && n) files[0] = 0;
	if (sys < 0 || sys >= g_nsys) return BIOS_OK;
	for (int i = 0; i < ARRAY_LEN(BIOS_REQ); i++) {
		if (strcmp(BIOS_REQ[i].code, g_sys[sys].code) != 0) continue;
		/* every '+' group needs one of its '|' alternatives */
		char buf[256], missing[256] = "";
		sncpy(buf, BIOS_REQ[i].files, sizeof buf);
		char *save1 = NULL;
		for (char *grp = strtok_r(buf, "+", &save1); grp; grp = strtok_r(NULL, "+", &save1)) {
			char g2[256];
			sncpy(g2, grp, sizeof g2);
			int ok = 0;
			char *save2 = NULL, *first = NULL;
			for (char *alt = strtok_r(g2, "|", &save2); alt; alt = strtok_r(NULL, "|", &save2)) {
				if (!first) first = alt;
				if (bios_present(alt, sys)) { ok = 1; break; }
			}
			if (!ok && first) {
				size_t l = strlen(missing);
				snfmt(missing + l, sizeof missing - l, "%s%s", l ? " + " : "", first);
			}
		}
		if (!missing[0]) return BIOS_OK;
		if (files) sncpy(files, missing, n);
		return BIOS_REQ[i].required ? BIOS_REQUIRED_MISSING : BIOS_OPTIONAL_MISSING;
	}
	return BIOS_OK;
}
