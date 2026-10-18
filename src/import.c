/* Clove — Import folder sorting.
 *
 * Anything dropped into <card>/Import is moved (a rename: instant, even for
 * big disc images) into Roms/<console> on the same card:
 *   - by file extension (.gba, .sfc, .md, .pce, .ngp ...),
 *   - by looking inside ambiguous files (.bin/.iso/.img headers, .cue track
 *     types, .chd track metadata, the file list inside a .zip),
 *   - arcade .zip sets by name (GarlicOS's mame.csv),
 *   - BIOS files by their well-known names, into BIOS,
 *   - folders named after a console ("GBA", "PS1", "SNES", "Mega Drive" ...)
 *     have their contents moved into that console,
 *   - other folders (multi-disc games, etc.) by what's inside them,
 *   - songs and albums (MP3, FLAC, OGG, M4A ...) into Music.
 * Whatever can't be identified stays in Import, and Clove lists it under an
 * "Import" console where a console can be picked by hand. */
#include "import.h"
#include "data.h"
#include "music.h"
#include "plat.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* Sorting a sent folder of several games one by one (the tablet for now: the
 * handheld's build stays as it is until it's tried on the real RG40XX H). */
#ifdef ZAPPO_TABLET
#define SORT_COLLECTIONS 1
#endif

/* ------------------------------------------------------------------ tables */

static const struct { const char *ext, *code; } EXT[] = {
	{"gb", "GB"}, {"gbc", "GBC"}, {"gba", "GBA"}, {"nes", "FC"}, {"unf", "FC"}, {"unif", "FC"}, {"fds", "FDS"},
	{"sfc", "SFC"}, {"smc", "SFC"}, {"swc", "SFC"}, {"fig", "SFC"}, {"bs", "SATELLAVIEW"},
	{"vb", "VB"}, {"vboy", "VB"}, {"min", "POKE"}, {"mgw", "GW"},
	{"md", "MD"}, {"gen", "MD"}, {"smd", "MD"}, {"sms", "MS"}, {"gg", "GG"}, {"sg", "SEGASGONE"}, {"32x", "THIRTYTWOX"},
	{"pce", "PCE"}, {"sgx", "SGFX"}, {"ngp", "NGP"}, {"ngc", "NGP"}, {"ws", "WS"}, {"wsc", "WS"}, {"pc2", "WS"},
	{"lnx", "LYNX"}, {"a26", "ATARI"}, {"a52", "FIFTYTWOHUNDRED"}, {"a78", "SEVENTYEIGHTHUNDRED"},
	{"xex", "EIGHTHUNDRED"}, {"atr", "EIGHTHUNDRED"}, {"atx", "EIGHTHUNDRED"}, {"xfd", "EIGHTHUNDRED"},
	{"j64", "JAGUAR"}, {"jag", "JAGUAR"}, {"msa", "ATARIST"}, {"stx", "ATARIST"},
	{"col", "COLECO"}, {"int", "INTELLIVISION"}, {"vec", "VECTREX"}, {"chf", "FAIRCHILD"}, {"o2", "ODYSSEY"},
	{"sv", "SUPERVISION"}, {"uze", "UZEBOX"}, {"vms", "VMU"}, {"dci", "VMU"},
	{"d64", "COMMODORE"}, {"d71", "COMMODORE"}, {"d81", "COMMODORE"}, {"t64", "COMMODORE"}, {"g64", "COMMODORE"},
	{"x64", "COMMODORE"}, {"prg", "COMMODORE"}, {"crt", "COMMODORE"},
	{"adf", "AMIGA"}, {"adz", "AMIGA"}, {"dms", "AMIGA"}, {"ipf", "AMIGA"}, {"hdf", "AMIGA"}, {"lha", "AMIGA"},
	{"cdt", "CPC"}, {"tzx", "ZXS"}, {"z80", "ZXS"}, {"sna", "ZXS"}, {"szx", "ZXS"}, {"p", "ZXEIGHTYONE"}, {"81", "ZXEIGHTYONE"},
	{"mx1", "MSX"}, {"mx2", "MSX"}, {"dim", "X68000"}, {"xdf", "X68000"}, {"hdm", "X68000"}, {"2hd", "X68000"},
	{"d88", "PCEIGHTYEIGHT"}, {"88d", "PCEIGHTYEIGHT"}, {"t88", "PCEIGHTYEIGHT"},
	{"fdi", "PCNINETYEIGHT"}, {"hdi", "PCNINETYEIGHT"}, {"nhd", "PCNINETYEIGHT"},
	{"p8", "PICO"}, {"tic", "TIC"}, {"pbp", "PS"},
	{"nds", "NDS"}, {"z64", "N64"}, {"n64", "N64"}, {"v64", "N64"}, {"cso", "PSP"}, {"gdi", "DC"}, {"cdi", "DC"},
	{"mp4", "VIDEOS"}, {"mkv", "VIDEOS"}, {"avi", "VIDEOS"},
	{"world", "BLOCKBOX"},
};

/* Folder names people use for consoles (Anbernic stock, other firmwares,
 * plain English). Compared after removing spaces and punctuation. */
static const struct { const char *alias, *code; } ALIAS[] = {
	{"NES", "FC"}, {"FAMICOM", "FC"}, {"NINTENDO", "FC"}, {"SNES", "SFC"}, {"SUPERNINTENDO", "SFC"}, {"SUPERFAMICOM", "SFC"},
	{"GENESIS", "MD"}, {"MEGADRIVE", "MD"}, {"SEGAGENESIS", "MD"}, {"SEGAMEGADRIVE", "MD"},
	{"MASTERSYSTEM", "MS"}, {"SMS", "MS"}, {"SEGAMASTERSYSTEM", "MS"},
	{"PS1", "PS"}, {"PSX", "PS"}, {"PLAYSTATION", "PS"}, {"PLAYSTATION1", "PS"}, {"SONYPLAYSTATION", "PS"},
	{"PCENGINE", "PCE"}, {"TURBOGRAFX", "PCE"}, {"TURBOGRAFX16", "PCE"}, {"TG16", "PCE"},
	{"PCENGINECD", "PCECD"}, {"TURBOGRAFXCD", "PCECD"}, {"SUPERGRAFX", "SGFX"},
	{"NGPC", "NGP"}, {"NEOGEOPOCKET", "NGP"}, {"NEOGEOPOCKETCOLOR", "NGP"}, {"NEOGEOCD", "NEOCD"},
	{"WONDERSWAN", "WS"}, {"WONDERSWANCOLOR", "WS"}, {"WSC", "WS"},
	{"MAME", "ARCADE"}, {"VERTICAL", "ARCADE"}, {"MAME2003", "ARCADE"}, {"MAME2003PLUS", "ARCADE"},
	{"CPS", "CPS1"}, {"FBAHACK", "FBA2012"}, {"FBA", "FBA2012"}, {"FINALBURN", "FBNEO"},
	{"GAMEBOY", "GB"}, {"GAMEBOYCOLOR", "GBC"}, {"GAMEBOYADVANCE", "GBA"}, {"GAMEGEAR", "GG"}, {"SEGAGAMEGEAR", "GG"},
	{"SEGACD", "SEGACD"}, {"MEGACD", "SEGACD"}, {"32X", "THIRTYTWOX"}, {"SEGA32X", "THIRTYTWOX"}, {"SG1000", "SEGASGONE"},
	{"ATARI2600", "ATARI"}, {"2600", "ATARI"}, {"ATARI5200", "FIFTYTWOHUNDRED"}, {"5200", "FIFTYTWOHUNDRED"},
	{"ATARI7800", "SEVENTYEIGHTHUNDRED"}, {"7800", "SEVENTYEIGHTHUNDRED"}, {"ATARI800", "EIGHTHUNDRED"},
	{"ATARILYNX", "LYNX"}, {"ATARIJAGUAR", "JAGUAR"},
	{"C64", "COMMODORE"}, {"COMMODORE64", "COMMODORE"}, {"AMIGACD32", "AMIGACD"}, {"CD32", "AMIGACD"},
	{"PICO8", "PICO"}, {"TIC80", "TIC"}, {"3DO", "PANASONIC"},
	{"ZXSPECTRUM", "ZXS"}, {"SPECTRUM", "ZXS"}, {"ZX81", "ZXEIGHTYONE"}, {"AMSTRADCPC", "CPC"}, {"AMSTRAD", "CPC"},
	{"VIRTUALBOY", "VB"}, {"POKEMONMINI", "POKE"}, {"GAMEANDWATCH", "GW"}, {"GAMEWATCH", "GW"},
	{"X68K", "X68000"}, {"PC88", "PCEIGHTYEIGHT"}, {"PC98", "PCNINETYEIGHT"}, {"MSX2", "MSX"},
	{"COLECOVISION", "COLECO"}, {"ODYSSEY2", "ODYSSEY"}, {"CHANNELF", "FAIRCHILD"}, {"DREAMCASTVMU", "VMU"},
	{"VIDEO", "VIDEOS"}, {"MOVIES", "VIDEOS"},
};

/* Well-known BIOS file names (lower case) and the BIOS sub-folder they belong in. */
static const struct { const char *name, *sub; } BIOSF[] = {
	{"scph1001.bin", ""}, {"scph5500.bin", ""}, {"scph5501.bin", ""}, {"scph5502.bin", ""}, {"scph7001.bin", ""},
	{"scph7502.bin", ""}, {"scph101.bin", ""}, {"scph1000.bin", ""}, {"scph3000.bin", ""}, {"scph3500.bin", ""},
	{"scph5000.bin", ""}, {"scph7000.bin", ""}, {"scph7003.bin", ""}, {"scph9002.bin", ""}, {"psxonpsp660.bin", ""},
	{"ps1_rom.bin", ""}, {"gba_bios.bin", ""}, {"gb_bios.bin", ""}, {"gbc_bios.bin", ""}, {"sgb_bios.bin", ""},
	{"disksys.rom", ""}, {"syscard3.pce", ""}, {"syscard2.pce", ""}, {"syscard1.pce", ""}, {"gexpress.pce", ""},
	{"bios_cd_u.bin", ""}, {"bios_cd_e.bin", ""}, {"bios_cd_j.bin", ""}, {"bios_md.bin", ""},
	{"bios_e.sms", ""}, {"bios_u.sms", ""}, {"bios_j.sms", ""}, {"bios.gg", ""},
	{"neogeo.zip", ""}, {"lynxboot.img", ""}, {"5200.rom", ""}, {"7800 bios (u).rom", ""}, {"7800 bios (e).rom", ""},
	{"ataribas.rom", ""}, {"atarixl.rom", ""}, {"atariosa.rom", ""}, {"atariosb.rom", ""},
	{"exec.bin", ""}, {"grom.bin", ""}, {"ivoice.bin", ""}, {"o2rom.bin", ""}, {"c52.bin", ""}, {"g7400.bin", ""}, {"jopac.bin", ""},
	{"sl31253.bin", ""}, {"sl31254.bin", ""}, {"sl90025.bin", ""}, {"bios.min", ""},
	{"kick33180.a500", ""}, {"kick34005.a500", ""}, {"kick37175.a500", ""}, {"kick40063.a600", ""},
	{"kick40068.a1200", ""}, {"kick40068.a4000", ""}, {"kick40060.cd32", ""}, {"kick40060.cd32.ext", ""}, {"kick34005.cdtv", ""},
	{"panafz10.bin", ""}, {"panafz1.bin", ""}, {"panafz10-norsa.bin", ""}, {"panafz10e-anvil.bin", ""},
	{"panafz10e-anvil-norsa.bin", ""}, {"panafz1j.bin", ""}, {"panafz1j-norsa.bin", ""}, {"goldstar.bin", ""},
	{"sanyotry.bin", ""}, {"3do_arcade_saot.bin", ""},
	{"tos.img", ""}, {"stbios.bin", ""}, {"bs-x.bin", ""}, {"pcfx.rom", ""}, {"prboom.wad", ""},
	{"neocd_f.rom", "neocd"}, {"neocd_sf.rom", "neocd"}, {"neocd_t.rom", "neocd"}, {"neocd_st.rom", "neocd"},
	{"neocd_z.rom", "neocd"}, {"neocd_sz.rom", "neocd"}, {"front-sp1.bin", "neocd"}, {"top-sp1.bin", "neocd"},
	{"neocd.bin", "neocd"}, {"uni-bioscd.rom", "neocd"}, {"000-lo.lo", "neocd"}, {"ng-lo.rom", "neocd"},
	{"iplrom.dat", "keropi"}, {"cgrom.dat", "keropi"}, {"iplrom30.dat", "keropi"}, {"iplromco.dat", "keropi"}, {"iplromxv.dat", "keropi"},
};

/* Files that are never games: left alone in Import, never listed. */
static const char *IGNORE_EXT[] = {"txt", "nfo", "md5", "sha1", "sfv", "url", "htm", "html", "pdf", "jpg", "jpeg", "png",
                                   "gif", "bmp", "xml", "dat", "db", "ini", "cfg", "log", "srm", "sav", "state", "ds_store"};

static int sys_of_code(const char *code) { return code ? data_sys_index(code) : -1; }

static const char *ext_code(const char *ext) {
	for (int i = 0; i < ARRAY_LEN(EXT); i++)
		if (str_ieq(ext, EXT[i].ext)) return EXT[i].code;
	return NULL;
}

static int is_ignored(const char *name) {
	if (name[0] == '.') return 1;
	size_t l = strlen(name);
	if (l > 7 && str_ieq(name + l - 7, ".p8.png")) return 0; /* PICO-8 carts are PNGs */
	const char *ext = path_ext(name);
	for (int i = 0; i < ARRAY_LEN(IGNORE_EXT); i++)
		if (str_ieq(ext, IGNORE_EXT[i])) return 1;
	return 0;
}

static const char *bios_sub(const char *name) {
	for (int i = 0; i < ARRAY_LEN(BIOSF); i++)
		if (str_ieq(name, BIOSF[i].name)) return BIOSF[i].sub;
	return NULL;
}

static void norm(const char *in, char *out, size_t n) {
	size_t o = 0;
	for (; *in && o + 1 < n; in++)
		if (isalnum((unsigned char)*in)) out[o++] = (char)toupper((unsigned char)*in);
	out[o] = 0;
}

/* A folder called "GBA", "PS1", "Super Nintendo", "Mega Drive" ... */
static int sys_from_folder_name(const char *name) {
	char a[96], b[96];
	norm(name, a, sizeof a);
	if (!*a) return -1;
	for (int i = 0; i < ARRAY_LEN(ALIAS); i++)
		if (strcmp(a, ALIAS[i].alias) == 0) return sys_of_code(ALIAS[i].code);
	for (int i = 0; i < g_nsys; i++) {
		if (!strcmp(g_sys[i].code, "IMPORT")) continue;
		norm(g_sys[i].code, b, sizeof b);
		if (!strcmp(a, b)) return i;
		norm(g_sys[i].name, b, sizeof b);
		if (!strcmp(a, b)) return i;
		norm(g_sys[i].short_name, b, sizeof b);
		if (!strcmp(a, b)) return i;
	}
	return -1;
}

/* ---------------------------------------------------------------- sniffing */

static size_t read_at(const char *path, off_t off, void *buf, size_t n) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	size_t got = 0;
	if (fseeko(f, off, SEEK_SET) == 0) got = fread(buf, 1, n, f);
	fclose(f);
	return got;
}

static long long size_of(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 ? (long long)st.st_size : -1;
}

/* PlayStation / Sega CD / Mega Drive from the first bytes of a .bin/.iso/.img */
static const char *sniff_image(const char *path) {
	unsigned char b[0x200];
	size_t n = read_at(path, 0, b, sizeof b);
	if (n >= 0x20 && (memcmp(b, "SEGADISCSYSTEM", 14) == 0 || memcmp(b + 0x10, "SEGADISCSYSTEM", 14) == 0)) return "SEGACD";
	if (n >= 0x110 && memcmp(b + 0x100, "SEGA", 4) == 0) return memcmp(b + 0x100, "SEGA 32X", 8) == 0 ? "THIRTYTWOX" : "MD";
	/* ISO 9660 volume descriptor: 2048-byte sectors (.iso) or raw 2352-byte (.bin) */
	static const long long pvd[] = {0x8000, 16LL * 2352 + 24, 16LL * 2352 + 16};
	for (int i = 0; i < 3; i++) {
		unsigned char v[48];
		if (read_at(path, (off_t)pvd[i], v, sizeof v) == sizeof v && memcmp(v + 1, "CD001", 5) == 0) {
			if (memcmp(v + 8, "PLAYSTATION", 11) == 0) return "PS";
			return NULL; /* a CD, but not one we can place */
		}
	}
	return NULL;
}

static const char *sniff_cue(const char *path) {
	char *txt = size_of(path) < 65536 ? file_read_all(path, NULL) : NULL;
	if (!txt) return NULL;
	char first_file[512] = "";
	const char *result = NULL;
	int track = 0, first_audio = 0, mode1 = 0;
	for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		while (*line == ' ' || *line == '\t') line++;
		if (!strncasecmp(line, "FILE", 4) && !first_file[0]) {
			char *q1 = strchr(line, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
			if (q1 && q2) { *q2 = 0; sncpy(first_file, q1 + 1, sizeof first_file); }
		}
		if (!strncasecmp(line, "TRACK", 5)) {
			track++;
			if (strcasestr(line, "MODE2")) { if (track <= 2) result = "PS"; break; }
			if (strcasestr(line, "AUDIO") && track == 1) first_audio = 1;
			if (strcasestr(line, "MODE1")) { mode1 = 1; if (first_audio) { result = "PCECD"; break; } if (track == 1) break; }
		}
	}
	free(txt);
	if (!result && mode1 && first_file[0]) {
		/* A MODE1 data disc: Sega CD discs say so in their first sector */
		char dir[512], bin[1024];
		path_dir(path, dir, sizeof dir);
		snfmt(bin, sizeof bin, "%s/%s", dir, first_file);
		const char *s = sniff_image(bin);
		if (s && !strcmp(s, "SEGACD")) result = s;
	}
	return result;
}

static uint64_t be64(const unsigned char *p) {
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
	return v;
}

/* CHD: read the CD track metadata (CHT2/CHTR) for track 1's type */
static const char *sniff_chd(const char *path) {
	unsigned char h[64];
	if (read_at(path, 0, h, sizeof h) < 56 || memcmp(h, "MComprHD", 8) != 0) return NULL;
	uint32_t ver = ((uint32_t)h[12] << 24) | ((uint32_t)h[13] << 16) | ((uint32_t)h[14] << 8) | h[15];
	uint64_t meta = ver >= 5 ? be64(h + 48) : be64(h + 36);
	for (int hops = 0; meta && hops < 16; hops++) {
		unsigned char m[16 + 256];
		size_t got = read_at(path, (off_t)meta, m, sizeof m);
		if (got < 16) break;
		uint32_t len = ((uint32_t)m[5] << 16) | ((uint32_t)m[6] << 8) | m[7];
		char txt[257];
		size_t tl = MIN((size_t)len, MIN(got - 16, (size_t)256));
		memcpy(txt, m + 16, tl);
		txt[tl] = 0;
		if ((memcmp(m, "CHT2", 4) == 0 || memcmp(m, "CHTR", 4) == 0) && strstr(txt, "TRACK:1 ")) {
			if (strstr(txt, "TYPE:MODE2")) return "PS";
			if (strstr(txt, "TYPE:AUDIO")) return "PCECD";
			return NULL;
		}
		meta = be64(m + 8);
	}
	return NULL;
}

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* The names inside a .zip (from its central directory; nothing is unpacked). */
static int zip_names(const char *path, StrList *out) {
	long long size = size_of(path);
	if (size < 22) return -1;
	size_t tail = (size_t)MIN(size, 66000LL);
	unsigned char *b = xmalloc(tail);
	int rc = -1;
	if (read_at(path, (off_t)(size - (long long)tail), b, tail) == tail) {
		for (long i = (long)tail - 22; i >= 0; i--) {
			if (le32(b + i) != 0x06054b50) continue;
			uint32_t cd_size = le32(b + i + 12), cd_off = le32(b + i + 16);
			if (cd_off == 0xFFFFFFFFu || cd_size > (4u << 20)) break;
			unsigned char *cd = xmalloc(cd_size + 1);
			if (read_at(path, (off_t)cd_off, cd, cd_size) == cd_size) {
				rc = 0;
				for (uint32_t p = 0; p + 46 <= cd_size && out->n < 256;) {
					if (le32(cd + p) != 0x02014b50) break;
					uint16_t nl = le16(cd + p + 28), xl = le16(cd + p + 30), cl = le16(cd + p + 32);
					if (p + 46 + nl > cd_size) break;
					char name[512];
					size_t k = MIN((size_t)nl, sizeof name - 1);
					memcpy(name, cd + p + 46, k);
					name[k] = 0;
					if (k && name[k - 1] != '/') sl_push(out, name);
					p += 46u + nl + xl + cl;
				}
			}
			free(cd);
			break;
		}
	}
	free(b);
	return rc;
}

static const char *sniff_zip(const char *path) {
	StrList names = {0};
	const char *code = NULL;
	int conflict = 0, known = 0;
	if (zip_names(path, &names) == 0) {
		for (int i = 0; i < names.n; i++) {
			const char *base = path_base(names.v[i]);
			if (is_ignored(base)) continue;
			const char *c = ext_code(path_ext(base));
#ifdef SORT_COLLECTIONS
			if (c && !strcmp(c, "VIDEOS")) continue;	/* (films in a .zip: not playable as they are) */
#endif
			if (!c) continue;
			known++;
			if (!code) code = c;
			else if (strcmp(code, c)) conflict = 1;
		}
	}
	sl_free(&names);
	if (code && !conflict && known) return code;
	return NULL;
}

/* -------------------------------------------------------------- guessing */

/* Does an MP4/M4A file hold a video track? Walks the top-level boxes to
 * 'moov' (at the start or the end of the file) and looks for a 'vide'
 * handler. -1 when it can't tell. */
static int mp4_has_video(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	int res = -1;
	unsigned long long pos = 0, fsize = (unsigned long long)size_of(path);
	for (int guard = 0; guard < 64 && pos + 8 <= fsize; guard++) {
		unsigned char h[16];
		if (fseeko(f, (off_t)pos, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8) break;
		unsigned long long sz = ((unsigned long long)h[0] << 24) | (h[1] << 16) | (h[2] << 8) | h[3];
		unsigned hdr = 8;
		if (sz == 1) {
			if (fread(h + 8, 1, 8, f) != 8) break;
			sz = 0;
			for (int i = 8; i < 16; i++) sz = (sz << 8) | h[i];
			hdr = 16;
		} else if (sz == 0) sz = fsize - pos;
		if (sz < hdr) break;
		if (!memcmp(h + 4, "moov", 4)) {
			size_t n = (size_t)MIN(sz - hdr, 16ull << 20);
			unsigned char *buf = malloc(n ? n : 1);
			if (buf && fread(buf, 1, n, f) == n) {
				res = 0;
				for (size_t i = 0; i + 16 <= n; i++)
					if (!memcmp(buf + i, "hdlr", 4) && !memcmp(buf + i + 12, "vide", 4)) { res = 1; break; }
			}
			free(buf);
			break;
		}
		pos += sz;
	}
	fclose(f);
	return res;
}

/* Songs go to Music. An .mp4 is a song only when it has no picture. */
static int is_music_file(const char *path) {
	int k = music_kind(path);
	if (k == MUSIC_NO) return 0;
	const char *ext = path_ext(path);
	if (str_ieq(ext, "mp4") || str_ieq(ext, "m4v")) return mp4_has_video(path) == 0;
	return 1;
}

static int is_music_folder_name(const char *name) {
	static const char *N[] = {"Music", "Songs", "Audio", "MP3", "MP3s", "Albums", "Playlists", "My Music"};
	for (int i = 0; i < ARRAY_LEN(N); i++)
		if (str_ieq(name, N[i])) return 1;
	return 0;
}

static int guess_file(const char *path, int allow_bios) {
	const char *name = path_base(path);
	if (is_ignored(name)) return -1;
	if (allow_bios && bios_sub(name)) return IMPORT_TO_BIOS;
	if (is_music_file(path)) return IMPORT_TO_MUSIC;
	size_t l = strlen(name);
	if (l > 7 && str_ieq(name + l - 7, ".p8.png")) return sys_of_code("PICO");
	const char *ext = path_ext(name);
	char stem[256];
	path_stem(name, stem, sizeof stem);
	if (str_ieq(ext, "md") && size_of(path) < 65536) return -1; /* a Markdown note, not a Mega Drive game */
	const char *c = ext_code(ext);
	if (c) return sys_of_code(c);
	if (str_ieq(ext, "bin") || str_ieq(ext, "iso") || str_ieq(ext, "img")) return sys_of_code(sniff_image(path));
	if (str_ieq(ext, "cue")) return sys_of_code(sniff_cue(path));
	if (str_ieq(ext, "chd")) return sys_of_code(sniff_chd(path));
	if (str_ieq(ext, "m3u")) {
		char *txt = size_of(path) < 65536 ? file_read_all(path, NULL) : NULL;
		int r = -1;
		if (txt) {
			for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
				str_trim(line);
				if (!*line || *line == '#') continue;
				char dir[512], p[1024];
				path_dir(path, dir, sizeof dir);
				snfmt(p, sizeof p, "%s/%s", dir, line);
				r = file_exists(p) ? guess_file(p, 0) : -1;
				break;
			}
			free(txt);
		}
		return r;
	}
	if (str_ieq(ext, "zip")) {
		int s = sys_of_code(sniff_zip(path));
		if (s >= 0) return s;
		if (data_is_arcade_set(stem)) return sys_of_code("ARCADE");
		return -1;
	}
	if (str_ieq(ext, "7z") && data_is_arcade_set(stem)) return sys_of_code("FBNEO");
	return -1;
}

/* A folder that isn't named after a console: a multi-disc game, a ScummVM
 * game, ... Decide by what's inside. */
static int guess_folder_contents_d(const char *dir, int depth) {
	DIR *d = opendir(dir);
	if (!d) return -1;
	int votes[512] = {0}, best = -1, disc = -1, songs = 0, games = 0, sub_music = 0, sub_games = 0, sub_sys = -1, sub_mixed = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[1024];
		snfmt(p, sizeof p, "%s/%s", dir, e->d_name);
		if (dir_exists(p)) {
			/* an artist folder full of album folders */
			if (depth < 1) {
				int s = guess_folder_contents_d(p, depth + 1);
				if (s == IMPORT_TO_MUSIC) sub_music++;
				else if (s >= 0) {
					sub_games++;
					if (sub_sys < 0) sub_sys = s;
					else if (s != sub_sys) sub_mixed = 1;
				}
			}
			continue;
		}
		const char *ext = path_ext(e->d_name);
		int s = guess_file(p, 0);
		if (s == IMPORT_TO_MUSIC) { songs++; continue; }
		if (s < 0 || s >= 512) continue;
		games++;
		if (str_ieq(ext, "cue") || str_ieq(ext, "m3u") || str_ieq(ext, "chd") || str_ieq(ext, "pbp")) disc = s;
		votes[s]++;
		if (best < 0 || votes[s] > votes[best]) best = s;
	}
	closedir(d);
	if (songs > games || (!songs && !games && sub_music && !sub_games)) return IMPORT_TO_MUSIC;
#ifdef SORT_COLLECTIONS
	/* a game in folders of its own ("Disc 1", "Disc 2") */
	if (!games && !songs && sub_games && !sub_mixed) return sub_sys;
#else
	(void)sub_sys; (void)sub_mixed;
#endif
	return disc >= 0 ? disc : best;
}

static int guess_folder_contents(const char *dir) { return guess_folder_contents_d(dir, 0); }

/* where a folder goes by its name alone (BIOS, Music, GBA ...), or -1 */
static int folder_by_name(const char *name) {
	if (str_ieq(name, "BIOS") || str_ieq(name, "Machines") || str_ieq(name, "Databases") || str_ieq(name, "neocd") ||
	    str_ieq(name, "keropi") || str_ieq(name, "np2kai") || str_ieq(name, "quasi88"))
		return IMPORT_TO_BIOS;
	if (is_music_folder_name(name)) return IMPORT_TO_MUSIC;
	return sys_from_folder_name(name);
}

int import_guess(const char *path, int is_dir) {
	if (!is_dir) return guess_file(path, 1);
	int s = folder_by_name(path_base(path));
	if (s != -1) return s;
	return guess_folder_contents(path);
}

/* ----------------------------------------------------------------- moving */

static void card_of(const char *path, char *out, size_t n) {
	if (g_paths.sd_present && str_starts_with(path, g_paths.sd)) sncpy(out, g_paths.sd, n);
	else sncpy(out, g_paths.mmc, n);
}

static void target_dir(const char *item, int target, const char *bios_subdir, char *out, size_t n) {
	char card[256];
	card_of(item, card, sizeof card);
#ifdef ZAPPO_KNULLI
	/* one card on Knulli: its own folder names */
	if (target == IMPORT_TO_BIOS) {
		char b[512];
		data_bios_dir(b, sizeof b);
		if (bios_subdir && *bios_subdir) snfmt(out, n, "%s/%s", b, bios_subdir);
		else sncpy(out, b, n);
	} else if (target == IMPORT_TO_MUSIC) {
		music_dir(out, n);
	} else {
		data_sys_dir(target, out, n);
	}
	return;
#endif
	if (target == IMPORT_TO_BIOS) {
		if (bios_subdir && *bios_subdir) snfmt(out, n, "%s/BIOS/%s", card, bios_subdir);
		else snfmt(out, n, "%s/BIOS", card);
	} else if (target == IMPORT_TO_MUSIC) {
		snfmt(out, n, "%s/Music", card);
	} else {
		snfmt(out, n, "%s/Roms/%s", card, g_sys[target].code);
	}
}

/* Arcade sets must keep their exact names, so they are never renamed. */
static int renamable(int sys) {
	static const char *A[] = {"ARCADE", "FBNEO", "FBA2012", "MAME2000", "CPS1", "CPS2", "CPS3", "NEOGEO"};
	for (int i = 0; i < ARRAY_LEN(A); i++)
		if (sys >= 0 && !strcmp(g_sys[sys].code, A[i])) return 0;
	return 1;
}

/* Things in Import that are left alone and not listed: notes, pictures,
 * a small README.md (".md" is also Mega Drive, so size decides). */
int import_ignored(const char *path) {
	const char *name = path_base(path);
	if (is_ignored(name)) return 1;
	return str_ieq(path_ext(name), "md") && size_of(path) < 65536;
}

/* rename() with a " (2)" style suffix when the name is already taken */
static int move_one(const char *src, const char *dstdir, int may_rename, char *err, size_t errn) {
	mkdir_p(dstdir);
	char dst[1024];
	const char *base = path_base(src);
	snfmt(dst, sizeof dst, "%s/%s", dstdir, base);
	if (file_exists(dst) || dir_exists(dst)) {
		if (!may_rename) { snfmt(err, errn, "%s is already there", base); return -1; }
		char stem[256];
		const char *ext = dir_exists(src) ? "" : path_ext(base);
		if (*ext) path_stem(base, stem, sizeof stem);
		else sncpy(stem, base, sizeof stem);
		for (int k = 2; k < 100; k++) {
			if (*ext) snfmt(dst, sizeof dst, "%s/%s (%d).%s", dstdir, stem, k, ext);
			else snfmt(dst, sizeof dst, "%s/%s (%d)", dstdir, stem, k);
			if (!file_exists(dst) && !dir_exists(dst)) break;
		}
	}
	if (rename(src, dst) != 0) {
		snfmt(err, errn, "Could not move %s (%s)", base, strerror(errno));
		LOG("import: rename %s -> %s failed: %s", src, dst, strerror(errno));
		return -1;
	}
	LOG("import: %s -> %s", src, dst);
	return 0;
}

/* .cue -> its FILEs; .m3u -> its discs (and their FILEs) */
static void companions(const char *path, StrList *out, int depth) {
	const char *ext = path_ext(path);
	int cue = str_ieq(ext, "cue"), m3u = str_ieq(ext, "m3u");
	if ((!cue && !m3u) || depth > 2 || size_of(path) > 65536) return;
	char *txt = file_read_all(path, NULL);
	if (!txt) return;
	char dir[512];
	path_dir(path, dir, sizeof dir);
	int start = out->n;
	for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		str_trim(line);
		char ref[512] = "";
		if (cue) {
			if (strncasecmp(line, "FILE", 4) != 0) continue;
			char *q1 = strchr(line, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
			if (!q1 || !q2) continue;
			*q2 = 0;
			sncpy(ref, q1 + 1, sizeof ref);
		} else {
			if (!*line || *line == '#') continue;
			sncpy(ref, line, sizeof ref);
		}
		if (strchr(ref, '/')) continue; /* only files right next to it */
		char p[1024];
		snfmt(p, sizeof p, "%s/%s", dir, ref);
		if (!file_exists(p) || sl_find(out, p) >= 0) continue;
		sl_push(out, p);
		/* strtok is not re-entrant: collect nested ones after this loop */
	}
	free(txt);
	if (m3u)
		for (int i = start, n = out->n; i < n; i++) companions(out->v[i], out, depth + 1);
}

int import_move(const char *path, int target, char *err, size_t errn) {
	if (err && errn) err[0] = 0;
	char dstdir[600];
	int is_dir = dir_exists(path);
	if (target == IMPORT_TO_BIOS) {
		const char *sub = is_dir ? NULL : bios_sub(path_base(path));
		target_dir(path, target, sub, dstdir, sizeof dstdir);
		/* a "BIOS" folder dropped in Import: its contents go into BIOS */
		if (is_dir && str_ieq(path_base(path), "BIOS")) {
			DIR *d = opendir(path);
			int bad = 0;
			struct dirent *e;
			StrList kids = {0};
			while (d && (e = readdir(d)))
				if (e->d_name[0] != '.') {
					char p[1024];
					snfmt(p, sizeof p, "%s/%s", path, e->d_name);
					sl_push(&kids, p);
				}
			if (d) closedir(d);
			for (int i = 0; i < kids.n; i++)
				if (move_one(kids.v[i], dstdir, 0, err, errn) != 0) bad = 1;
			sl_free(&kids);
			if (!bad) rmdir(path);
			return bad ? -1 : 0;
		}
		return move_one(path, dstdir, 0, err, errn);
	}
	if (target == IMPORT_TO_MUSIC) {
		target_dir(path, target, NULL, dstdir, sizeof dstdir);
		/* "Import/Music/..." -> everything inside goes into Music;
		 * any other folder is an album and moves in whole */
		if (is_dir && is_music_folder_name(path_base(path))) {
			DIR *d = opendir(path);
			StrList kids = {0};
			struct dirent *e;
			while (d && (e = readdir(d)))
				if (e->d_name[0] != '.') {
					char p[1024];
					snfmt(p, sizeof p, "%s/%s", path, e->d_name);
					sl_push(&kids, p);
				}
			if (d) closedir(d);
			int bad = 0;
			for (int i = 0; i < kids.n; i++)
				if (move_one(kids.v[i], dstdir, 1, err, errn) != 0) bad = 1;
			sl_free(&kids);
			rmdir(path);
			return bad ? -1 : 0;
		}
		return move_one(path, dstdir, 1, err, errn);
	}
	if (target < 0 || target >= g_nsys) { snfmt(err, errn, "No console chosen"); return -1; }
	target_dir(path, target, NULL, dstdir, sizeof dstdir);
	if (is_dir && sys_from_folder_name(path_base(path)) == target) {
		/* "Import/GBA/..." -> everything inside goes into Roms/GBA */
		DIR *d = opendir(path);
		StrList kids = {0};
		struct dirent *e;
		while (d && (e = readdir(d)))
			if (e->d_name[0] != '.') {
				/* notes and art go along too, so the folder empties out */
				char p[1024];
				snfmt(p, sizeof p, "%s/%s", path, e->d_name);
				sl_push(&kids, p);
			}
		if (d) closedir(d);
		int bad = 0;
		for (int i = 0; i < kids.n; i++)
			if (move_one(kids.v[i], dstdir, renamable(target), err, errn) != 0) bad = 1;
		sl_free(&kids);
		rmdir(path); /* only succeeds once it's empty */
		return bad ? -1 : 0;
	}
	if (is_dir) return move_one(path, dstdir, renamable(target), err, errn);
	/* a disc set moves together and keeps its names, so the .cue/.m3u still work */
	StrList group = {0};
	companions(path, &group, 0);
	if (group.n) {
		for (int i = 0; i < group.n; i++) {
			char d[1024];
			snfmt(d, sizeof d, "%s/%s", dstdir, path_base(group.v[i]));
			if (file_exists(d)) { snfmt(err, errn, "%s is already in %s", path_base(group.v[i]), g_sys[target].code); sl_free(&group); return -1; }
		}
		int bad = move_one(path, dstdir, 0, err, errn);
		for (int i = 0; i < group.n && !bad; i++) if (move_one(group.v[i], dstdir, 0, err, errn) != 0) bad = 1;
		sl_free(&group);
		return bad ? -1 : 0;
	}
	sl_free(&group);
	return move_one(path, dstdir, renamable(target), err, errn);
}

/* ------------------------------------------------------------------- run */

void import_dir(char *out, size_t n) {
#ifdef ZAPPO_KNULLI
	snfmt(out, n, "%s/import", g_paths.mmc);
#else
	snfmt(out, n, "%s/Import", g_paths.mmc);
#endif
}

static const char README[] =
	"IMPORT - drop games and music here\r\n"
	"==================================\r\n"
#ifdef ZAPPO_KNULLI
	"Copy games, music, films and BIOS files into this folder from your PC, or\r\n"
	"send them from your phone (Settings > Send to Zappo). Zappo X moves each one\r\n"
	"into the right place: games to their console in roms, songs to music,\r\n"
	"films to videos, BIOS files to bios.\r\n"
#else
	"Copy games, music and BIOS files into this folder from your PC. The next time\r\n"
	"Zappo opens, it moves each one into the right console folder in Roms.\r\n"
#endif
	"\r\n"
	"* Most games are recognised by their file type: .gba .gbc .gb .nes .sfc\r\n"
	"  .smc .md .sms .gg .pce .ngp .ws .lnx .a26 and many more.\r\n"
	"* PlayStation discs (.cue + .bin, .chd, .pbp, .m3u) are recognised too.\r\n"
	"* Arcade .zip files are recognised by their MAME name.\r\n"
	"* BIOS files (scph1001.bin, gba_bios.bin, disksys.rom, neogeo.zip ...)\r\n"
	"  go into the BIOS folder.\r\n"
	"* Music (.mp3 .flac .ogg .wav .m4a .aac .opus ...) goes into the Music\r\n"
	"  folder - single songs or whole album folders - and plays from the\r\n"
	"  Music tab. Music videos (.mp4 with a picture) go to Videos.\r\n"
	"* Not sure it'll be recognised? Make a folder in here named after the\r\n"
	"  console (GBA, SNES, PS1, Mega Drive, Arcade ...) and put it inside.\r\n"
	"\r\n"
	"Anything Zappo can't place stays here and appears as \"Import\" in the\r\n"
	"Consoles list on the handheld, where you can choose the console.\r\n";

#ifdef SORT_COLLECTIONS
/* A folder that holds several games (a whole collection sent at once, maybe
 * for different consoles) rather than one game (a disc set, a ScummVM game,
 * an album): its games are sorted one by one instead of moving it whole. */
static int is_disc(const char *path) {
	const char *e = path_ext(path);
	return str_ieq(e, "cue") || str_ieq(e, "chd") || str_ieq(e, "m3u") || str_ieq(e, "pbp") || str_ieq(e, "iso") || str_ieq(e, "bin");
}
/* "Disc 2", "CD1", "Disk 3 of 3": one disc of a game, in a folder of its own */
static int is_disc_folder(const char *name) {
	static const char *W[] = {"disc", "disk", "cd"};
	for (int i = 0; i < ARRAY_LEN(W); i++) {
		size_t l = strlen(W[i]);
		if (strncasecmp(name, W[i], l)) continue;
		const char *q = name + l;
		while (*q == ' ' || *q == '_' || *q == '-') q++;
		if (*q >= '0' && *q <= '9') return 1;
	}
	return 0;
}
static int is_collection(const char *dir) {
	DIR *d = opendir(dir);
	if (!d) return 0;
	StrList items = {0}, refs = {0};
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[1024];
		snfmt(p, sizeof p, "%s/%s", dir, e->d_name);
		if (!import_ignored(p)) sl_push(&items, p);
	}
	closedir(d);
	for (int i = 0; i < items.n; i++) companions(items.v[i], &refs, 0);
	int games = 0, discs = 0, songs = 0, subs = 0, first = -9, mixed = 0;
	for (int i = 0; i < items.n; i++) {
		const char *p = items.v[i];
		if (sl_find(&refs, p) >= 0) continue;
		int t;
		if (dir_exists(p)) {
			t = import_guess(p, 1);
			if (t == -1) continue;
			if (is_disc_folder(path_base(p))) { games++; discs++; }
			else subs++;
		} else {
			t = guess_file(p, 0);
			if (t == -1 || t == IMPORT_TO_BIOS) continue;
			if (t == IMPORT_TO_MUSIC) { songs++; continue; }
			games++;
			if (is_disc(p)) discs++;
		}
		if (first == -9) first = t;
		else if (t != first) mixed = 1;
	}
	sl_free(&items);
	sl_free(&refs);
	if (subs) return 1;                       /* folders of games inside */
	if (songs && games) return 1;             /* songs and games together */
	if (mixed) return 1;                      /* games for different consoles */
	if (games >= 2 && discs < games) return 1;/* several games (not the discs of one) */
	return 0;
}

static void sort_dir(const char *dir, ImportResult *r);
static void sort_collection(const char *dir, ImportResult *r) {
	sort_dir(dir, r);
	rmdir(dir);	/* (only if it's empty now: anything left stays to be sorted) */
}
#endif

static void sort_dir(const char *dir, ImportResult *r) {
	DIR *d = opendir(dir);
	if (!d) return;
	StrList items = {0}, refs = {0};
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[1024];
		snfmt(p, sizeof p, "%s/%s", dir, e->d_name);
		if (import_ignored(p)) continue;
		sl_push(&items, p);
	}
	closedir(d);
	/* .bin files that belong to a .cue move with it, not on their own */
	for (int i = 0; i < items.n; i++) companions(items.v[i], &refs, 0);
	for (int i = 0; i < items.n; i++) {
		const char *p = items.v[i];
		if (sl_find(&refs, p) >= 0 || (!file_exists(p) && !dir_exists(p))) continue;
		int is_dir = dir_exists(p);
#ifdef SORT_COLLECTIONS
		if (is_dir && folder_by_name(path_base(p)) == -1 && is_collection(p)) { sort_collection(p, r); continue; }
#endif
		int t = import_guess(p, is_dir);
		if (t == -1) { r->left++; continue; }
		char err[256];
		if (import_move(p, t, err, sizeof err) != 0) { r->failed++; r->left++; continue; }
		if (t == IMPORT_TO_BIOS) r->bios++;
		else if (t == IMPORT_TO_MUSIC) r->music++;
		else {
			r->moved++;
			if (!r->first_sys[0]) sncpy(r->first_sys, data_sys_name(t), sizeof r->first_sys);
		}
	}
	sl_free(&items);
	sl_free(&refs);
}

void import_run(ImportResult *r) {
	memset(r, 0, sizeof *r);
	char dir[512];
	import_dir(dir, sizeof dir);
	if (!dir_exists(dir)) mkdir_p(dir);
	char readme[600];
	snfmt(readme, sizeof readme, "%s/How to use Import.txt", dir);
	{
		/* write the guide, or refresh one an older Clove wrote */
		size_t len = 0;
		char *cur = file_exists(readme) ? file_read_all(readme, &len) : NULL;
		if (!cur || (!strncmp(cur, "IMPORT - drop games", 19) && (len != sizeof README - 1 || memcmp(cur, README, len))))
			file_write_all(readme, README, sizeof README - 1);
		free(cur);
	}
	uint64_t t0 = now_ms();
	sort_dir(dir, r);
	if (g_paths.sd_present) {
		snfmt(dir, sizeof dir, "%s/Import", g_paths.sd);
		sort_dir(dir, r);
	}
	if (r->moved || r->bios || r->music || r->left)
		LOG("import: %d games sorted, %d BIOS files, %d to Music, %d left to sort (%d failed) in %llu ms", r->moved, r->bios, r->music, r->left, r->failed,
		    (unsigned long long)(now_ms() - t0));
}

void import_summary(const ImportResult *r, int manual, char *out, size_t n) {
	char parts[3][80];
	int np = 0;
	if (r->moved == 1) snfmt(parts[np++], sizeof parts[0], "1 game into %s", r->first_sys);
	else if (r->moved) snfmt(parts[np++], sizeof parts[0], "%d games", r->moved);
	if (r->music) snfmt(parts[np++], sizeof parts[0], "%d to Music", r->music);
	if (r->bios) snfmt(parts[np++], sizeof parts[0], "%d BIOS file%s", r->bios, r->bios == 1 ? "" : "s");
	out[0] = 0;
	if (np) {
		char list[200];
		if (np == 1) sncpy(list, parts[0], sizeof list);
		else if (np == 2) snfmt(list, sizeof list, "%s and %s", parts[0], parts[1]);
		else snfmt(list, sizeof list, "%s, %s and %s", parts[0], parts[1], parts[2]);
		snfmt(out, n, "Sorted %s from Import%s", list, r->left ? " (some still need a console)" : "");
	} else if (r->left == 1) sncpy(out, "1 item in Import needs a console", n);
	else if (r->left) snfmt(out, n, "%d items in Import need a console", r->left);
	(void)manual;
}
