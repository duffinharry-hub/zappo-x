/* Clove — the music library (see musiclib.h). */
#include "musiclib.h"
#include "music.h"
#include "plat.h"
#include "util.h"

#include "../third_party/dr_flac.h"
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

MusicLib g_lib;

/* ------------------------------------------------------------ text helpers */

static void collapse(char *s) {
	char *o = s;
	int sp = 0;
	for (char *p = s; *p; p++) {
		if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { sp = 1; continue; }
		if (sp && o != s) *o++ = ' ';
		sp = 0;
		*o++ = *p;
	}
	*o = 0;
}

/* lower-case letters and digits only: "PotterPayperTV" -> "potterpaypertv" */
static void norm_key(const char *s, char *out, size_t n) {
	size_t o = 0;
	for (; *s && o + 1 < n; s++) {
		unsigned char c = (unsigned char)*s;
		if (isalnum(c)) out[o++] = (char)tolower(c);
		else if (c >= 0x80) out[o++] = (char)c; /* keep accented letters */
	}
	out[o] = 0;
}

static int ends_with_i(const char *s, const char *suf) {
	size_t a = strlen(s), b = strlen(suf);
	return a >= b && !strcasecmp(s + a - b, suf);
}

static const char *NOISE[] = {
	"official video", "official music video", "official audio", "official lyric video", "official lyrics video",
	"official visualizer", "official visualiser", "official hd video", "official video hd", "official 4k video",
	"lyric video", "lyrics video", "lyrics", "lyric", "audio", "official", "music video", "video", "visualizer",
	"visualiser", "hd", "hq", "4k", "explicit", "clean", "audio only", "full song", "videoclip", "clip officiel",
	"official clip", "out now", "visualiser video", "official dance video", "with lyrics",
};

static void lower_copy(const char *s, char *k, size_t n) {
	size_t o = 0;
	for (; *s && o + 1 < n; s++) k[o++] = (char)tolower((unsigned char)*s);
	k[o] = 0;
	str_trim(k);
	collapse(k);
}

/* "(Official HD Video)", "[OFFICIAL AUDIO]", "(Bass Boosted)" ... */
static int is_noise(const char *s) {
	char k[128];
	lower_copy(s, k, sizeof k);
	if (!k[0]) return 1;
	for (int i = 0; i < ARRAY_LEN(NOISE); i++)
		if (!strcmp(k, NOISE[i])) return 1;
	if (strstr(k, "remix") || strstr(k, "live") || strstr(k, "feat") || strstr(k, "ft.")) return 0;
	static const char *PART[] = {"official", "lyric", "visuali", "bass boost", "best quality", "dl link", "explicit",
	                             "music video", "hd video", "hq audio", "full video", "videoclip"};
	for (int i = 0; i < ARRAY_LEN(PART); i++)
		if (strstr(k, PART[i])) return 1;
	return 0;
}

/* a whole " - " segment that is only noise: "official video", "Best Quality",
 * "( bass boosted )" - but not "Ghetto (Official Music Video)" */
static int is_noise_segment(const char *s) {
	char rest[256];
	size_t o = 0;
	int depth = 0, brackets = 0, all_noise = 1;
	for (const char *p = s; *p && o + 1 < sizeof rest; p++) {
		if (*p == '(' || *p == '[') {
			const char *e = strchr(p, *p == '(' ? ')' : ']');
			if (e) {
				char in[200];
				snfmt(in, sizeof in, "%.*s", (int)MIN((size_t)(e - p - 1), sizeof in - 1), p + 1);
				if (!is_noise(in)) all_noise = 0;
				brackets = 1;
				p = e;
				continue;
			}
		}
		if (!depth) rest[o++] = *p;
	}
	rest[o] = 0;
	str_trim(rest);
	if (!rest[0]) return brackets && all_noise;
	int words = 1;
	for (char *p = rest; *p; p++) if (*p == ' ') words++;
	return !brackets && words <= 3 && is_noise(rest);
}

/* "slowed + reverb", "2023 Remaster", "Instrumental" ... worth keeping, in brackets */
static int is_version(const char *s) {
	char k[128];
	lower_copy(s, k, sizeof k);
	static const char *V[] = {"slowed", "reverb", "sped up", "remix", "remaster", "instrumental", "acoustic", "version", " edit", "mix", "live"};
	for (int i = 0; i < ARRAY_LEN(V); i++)
		if (strstr(k, V[i])) return 1;
	return 0;
}

/* an uploader rather than an artist: "BBC Radio 1Xtra", "AkonVEVO", "Trap Beats" */
static int channel_like(const char *s) {
	char k[128];
	lower_copy(s, k, sizeof k);
	static const char *SUF[] = {"vevo", "music", "radio", "records", "tv", "official", "vault", "topic", "1xtra", "group",
	                            "beats", "bass", "quality", "channel", "entertainment", "media", "uk"};
	for (int i = 0; i < ARRAY_LEN(SUF); i++)
		if (strlen(k) > strlen(SUF[i]) && !strcmp(k + strlen(k) - strlen(SUF[i]), SUF[i])) return 1;
	return strstr(k, "radio") != NULL || strstr(k, "records") != NULL;
}

/* Drop "(Official Video)", "[HD]" ...; shorten long "(Live on ...)" to "(Live)". */
void lib_tidy_title(char *t, const char *artist) {
	(void)artist;
	char out[512];
	size_t o = 0;
	for (size_t i = 0; t[i] && o + 1 < sizeof out;) {
		char open = t[i];
		if (open == '(' || open == '[') {
			char close = open == '(' ? ')' : ']';
			char *end = strchr(t + i + 1, close);
			if (end) {
				char inner[256];
				size_t len = (size_t)(end - (t + i + 1));
				snfmt(inner, sizeof inner, "%.*s", (int)MIN(len, sizeof inner - 1), t + i + 1);
				str_trim(inner);
				if (is_noise(inner)) {
					while (o > 0 && out[o - 1] == ' ') o--;
					i = (size_t)(end - t) + 1;
					continue;
				}
				if (!strncasecmp(inner, "live ", 5) && strlen(inner) > 18) {
					o += (size_t)snfmt(out + o, sizeof out - o, "(Live)");
					i = (size_t)(end - t) + 1;
					continue;
				}
			}
		}
		out[o++] = t[i++];
	}
	out[o] = 0;
	/* "I Got This #P110TheAlbum": drop hashtags after the title */
	for (char *h = strstr(out + 1, " #"); h; h = strstr(h, " #")) {
		char *e = h + 2;
		while (*e && *e != ' ') e++;
		if (e - h > 3) memmove(h, e, strlen(e) + 1);
		else h += 2;
	}
	collapse(out);
	/* a lone lower-case "i" is the word I */
	for (size_t i = 0; out[i]; i++)
		if (out[i] == 'i' && (i == 0 || out[i - 1] == ' ') && (out[i + 1] == ' ' || out[i + 1] == 0 || out[i + 1] == '\''))
			out[i] = 'I';
	/* stray separators left at either end */
	str_trim(out);
	while (out[0] == '-' || out[0] == '|' || out[0] == ':') { memmove(out, out + 1, strlen(out)); str_trim(out); }
	size_t l = strlen(out);
	while (l && (out[l - 1] == '-' || out[l - 1] == '|' || out[l - 1] == ':')) { out[--l] = 0; str_trim(out); l = strlen(out); }
	if (out[0]) strcpy(t, out);
}

/* "JustJackOfficial" -> "Just Jack"; "SomeoneVEVO" -> "Someone" */
static void clean_channel(const char *ch, char *out, size_t n) {
	char b[256];
	sncpy(b, ch, sizeof b);
	str_trim(b);
	static const char *SUF[] = {" - Topic", "VEVO", "Official", "OfficialTV", "TV", "Music", "Channel"};
	for (int pass = 0; pass < 3; pass++)
		for (int i = 0; i < ARRAY_LEN(SUF); i++)
			if (ends_with_i(b, SUF[i]) && strlen(b) > strlen(SUF[i]) + 1) {
				b[strlen(b) - strlen(SUF[i])] = 0;
				str_trim(b);
			}
	size_t o = 0;
	int has_space = strchr(b, ' ') != NULL;
	for (size_t i = 0; b[i] && o + 2 < n; i++) {
		if (!has_space && i > 0 && isupper((unsigned char)b[i]) && islower((unsigned char)b[i - 1])) out[o++] = ' ';
		out[o++] = b[i];
	}
	out[o] = 0;
}

static int looks_like_channel(const char *seg, const char *artist, const char *channel) {
	char a[256], s[256], c[256];
	norm_key(seg, s, sizeof s);
	if (!s[0]) return 1;
	norm_key(artist ? artist : "", a, sizeof a);
	norm_key(channel ? channel : "", c, sizeof c);
	if (strlen(a) >= 3 && strstr(s, a)) return 1;
	if (strlen(c) >= 3 && (strstr(s, c) || strstr(c, s))) return 1;
	return ends_with_i(seg, "tv") || ends_with_i(seg, "vevo") || ends_with_i(seg, "official");
}

static void replace_all(char *b, size_t n, const char *from, const char *to) {
	size_t fl = strlen(from), tl = strlen(to);
	for (char *p = b; (p = strstr(p, from));) {
		if (strlen(b) - fl + tl + 1 > n) return;
		memmove(p + tl, p + fl, strlen(p + fl) + 1);
		memcpy(p, to, tl);
		p += tl;
	}
}

/* the first run of 2+ spaces that isn't inside brackets */
static char *gap_outside_brackets(char *s) {
	int depth = 0;
	for (char *p = s; *p; p++) {
		if (*p == '(' || *p == '[') depth++;
		else if ((*p == ')' || *p == ']') && depth) depth--;
		else if (!depth && p[0] == ' ' && p[1] == ' ') return p;
	}
	return NULL;
}

/* *two is set when the name was a plain "X - Y": which is the artist is
 * decided later, across the whole library (see orient_names). */
static void tidy_name_ex(const char *stem, char *title, size_t tn, char *artist, size_t an, int *two) {
	*two = 0;
	char b[512];
	sncpy(b, stem, sizeof b);
	for (char *p = b; *p; p++) if (*p == '_') *p = ' ';
	/* other separators people use -> " - " */
	replace_all(b, sizeof b, " \xE2\x80\x93 ", " - ");          /* en dash */
	replace_all(b, sizeof b, " \xE2\x80\x94 ", " - ");          /* em dash */
	replace_all(b, sizeof b, " \xE2\x97\x8F\xE2\x97\x8F ", " - "); /* ●● */
	replace_all(b, sizeof b, " \xE2\x97\x8F ", " - ");         /* ● */
	replace_all(b, sizeof b, " \xEF\xBE\x89 ", " - ");         /* ﾉ */
	replace_all(b, sizeof b, " \xEF\xBE\x89", " - ");
	replace_all(b, sizeof b, " ~ ", " - ");
	replace_all(b, sizeof b, " | ", " - ");
	replace_all(b, sizeof b, " // ", " - ");
	/* "souls- will" -> "souls - will" */
	for (char *p = b + 1; (p = strstr(p, "- ")); p += 2)
		if (isalnum((unsigned char)p[-1]) && p - b >= 2 && p[-2] != ' ') {
			if (strlen(b) + 2 >= sizeof b) break;
			memmove(p + 1, p, strlen(p) + 1);
			*p = ' ';
			p++;
		}
	/* a leading track number: "07 - ", "07. ", "07 Song" - but not "16 Shots" */
	char *s = b;
	while (isdigit((unsigned char)*s)) s++;
	int nd = (int)(s - b);
	if (nd >= 1 && nd <= 3 && (*s == '.' || !strncmp(s, " - ", 3) || *s == '-' || (nd == 2 && b[0] == '0' && *s == ' ')))
		while (*s == ' ' || *s == '.' || *s == '-') s++;
	else s = b;
	char *seg[10];
	int n = 0;
	seg[n++] = s;
	for (char *p; n < 10 && (p = strstr(seg[n - 1], " - "));) { *p = 0; seg[n++] = p + 3; }
	int m = 0;
	for (int i = 0; i < n; i++) { str_trim(seg[i]); if (seg[i][0]) seg[m++] = seg[i]; }
	n = m;
	/* downloaded videos end with the uploader: "Artist - Song - Channel", "Song - Channel" */
	const char *channel = NULL;
	if (n >= 3) channel = seg[--n];
	else if (n == 2 && channel_like(seg[1])) channel = seg[--n];
	/* "P110 - Aystar - Song - P110 Music": a platform name in front */
	if (channel && n >= 3) {
		char k0[256], kc[256];
		norm_key(seg[0], k0, sizeof k0);
		norm_key(channel, kc, sizeof kc);
		if (strlen(k0) >= 3 && strstr(kc, k0) && strcmp(kc, k0)) { for (int i = 1; i < n; i++) seg[i - 1] = seg[i]; n--; }
	}
	/* drop "official video" style segments */
	m = 0;
	for (int i = 0; i < n; i++)
		if (!(is_noise_segment(seg[i]) && n > 1)) seg[m++] = seg[i];
	n = m;
	artist[0] = title[0] = 0;
	if (n >= 2) {
		int a = 0, t0 = 1;
		/* "Drive (2017 Remaster) - The Cars": the song came first */
		size_t l0 = strlen(seg[0]);
		if (n == 2 && !channel && l0 && seg[0][l0 - 1] == ')') {
			char *op = strrchr(seg[0], '(');
			if (op && is_version(op)) { a = 1; t0 = 0; }
		}
		if (n == 2 && !channel && a == 0) *two = 1;
		sncpy(artist, seg[a], an);
		char t[512] = "";
		sncpy(t, seg[t0], sizeof t);
		for (int i = 2; i < n; i++) {
			if (is_version(seg[i])) snfmt(t + strlen(t), sizeof t - strlen(t), " (%s)", seg[i]);
			else snfmt(t + strlen(t), sizeof t - strlen(t), " - %s", seg[i]);
		}
		sncpy(title, t, tn);
	} else if (n == 1) {
		sncpy(title, seg[0], tn);
		/* "Wretch 32 & Avelino   FITB": the gap separates artist and song */
		char *gap = gap_outside_brackets(title);
		if (gap && channel && channel_like(channel)) {
			*gap = 0;
			sncpy(artist, title, an);
			memmove(title, gap + 1, strlen(gap + 1) + 1);
			str_trim(title);
			str_trim(artist);
		} else if (channel) clean_channel(channel, artist, an);
	} else {
		sncpy(title, stem, tn);
	}
	/* "Song  Something": a separator (often "|") the downloader turned into
	 * two spaces. Drop it when it's the channel, else keep it in brackets. */
	char *dbl = gap_outside_brackets(title);
	if (dbl) {
		char rest[256];
		sncpy(rest, dbl, sizeof rest);
		str_trim(rest);
		*dbl = 0;
		str_trim(title);
		if (rest[0] && !looks_like_channel(rest, artist, channel) && !is_noise(rest)) {
			char t[512];
			snfmt(t, sizeof t, "%s (%s)", title, rest);
			sncpy(title, t, tn);
		}
	}
	/* "Flow Of The Year SBTV - SBTV Music": the uploader's tag at the end */
	if (channel) {
		char *sp = strrchr(title, ' ');
		if (sp && strlen(sp + 1) >= 3) {
			int caps = 1;
			for (char *q = sp + 1; *q; q++) if (islower((unsigned char)*q)) caps = 0;
			char kw[64], kc[256];
			norm_key(sp + 1, kw, sizeof kw);
			norm_key(channel, kc, sizeof kc);
			if (caps && kw[0] && strstr(kc, kw)) { *sp = 0; str_trim(title); }
		}
	}
	collapse(artist);
	collapse(title);
	lib_tidy_title(title, artist);
}

void lib_tidy_name(const char *stem, char *title, size_t tn, char *artist, size_t an) {
	int two;
	tidy_name_ex(stem, title, tn, artist, an, &two);
}

/* the main artist: "Potter Payper ft. Slim" -> "Potter Payper" */
static void primary_artist(char *a) {
	static const char *FT[] = {" ft. ", " ft ", " feat. ", " feat ", " featuring ", " (ft", " (feat", " [feat", " with "};
	for (int i = 0; i < ARRAY_LEN(FT); i++) {
		char *p = strcasestr(a, FT[i]);
		if (p && p != a) *p = 0;
	}
	str_trim(a);
}

/* ---------------------------------------------------------------- metadata */

typedef struct {
	char title[256], artist[256], album[256], genre[96];
	int has_art;
	double dur;
} Meta;

static const char *ID3_GENRES[] = {
	"Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal", "New Age",
	"Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska",
	"Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion",
	"Trance", "Classical", "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
	"Alternative Rock", "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock",
	"Ethnic", "Gothic", "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream",
	"Southern Rock", "Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle",
	"Native American", "Cabaret", "New Wave", "Psychedelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal",
	"Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock", "Folk", "Folk-Rock",
	"National Folk", "Swing", "Fast Fusion", "Bebop", "Latin", "Revival", "Celtic", "Bluegrass", "Avantgarde",
	"Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock", "Slow Rock", "Big Band", "Chorus",
	"Easy Listening", "Acoustic", "Humour", "Speech", "Chanson", "Opera", "Chamber Music", "Sonata", "Symphony",
	"Booty Bass", "Primus", "Porn Groove", "Satire", "Slow Jam", "Club", "Tango", "Samba", "Folklore", "Ballad",
	"Power Ballad", "Rhythmic Soul", "Freestyle", "Duet", "Punk Rock", "Drum Solo", "A Cappella", "Euro-House",
	"Dance Hall", "Goa", "Drum & Bass", "Club-House", "Hardcore", "Terror", "Indie", "Britpop", "Afro-Punk",
	"Polsk Punk", "Beat", "Christian Gangsta Rap", "Heavy Metal", "Black Metal", "Crossover",
	"Contemporary Christian", "Christian Rock", "Merengue", "Salsa", "Thrash Metal", "Anime", "J-Pop", "Synthpop",
};

static void genre_text(char *g, size_t n) {
	/* "(17)", "(17)Rock", "17" -> "Rock" */
	str_trim(g);
	if (g[0] == '(') {
		char *e = strchr(g, ')');
		if (e && e[1]) { memmove(g, e + 1, strlen(e + 1) + 1); str_trim(g); return; }
		if (e) { int k = atoi(g + 1); sncpy(g, k >= 0 && k < ARRAY_LEN(ID3_GENRES) ? ID3_GENRES[k] : "", n); return; }
	}
	int digits = 1;
	for (char *p = g; *p; p++) if (!isdigit((unsigned char)*p)) digits = 0;
	if (digits && g[0]) { int k = atoi(g); sncpy(g, k >= 0 && k < ARRAY_LEN(ID3_GENRES) ? ID3_GENRES[k] : "", n); }
	/* download tools often write a meaningless genre ("Music"): treat it as none */
	static const char *JUNK[] = {"music", "other", "others", "unknown", "genre", "misc", "none", "default", "audio",
	                             "general", "various", "n/a", "na", "-"};
	for (size_t i = 0; i < ARRAY_LEN(JUNK); i++)
		if (str_ieq(g, JUNK[i])) { g[0] = 0; break; }
}

static unsigned syncsafe(const unsigned char *b) {
	return ((unsigned)(b[0] & 0x7F) << 21) | ((unsigned)(b[1] & 0x7F) << 14) | ((unsigned)(b[2] & 0x7F) << 7) | (b[3] & 0x7F);
}

/* the picture inside an APIC / PIC frame body */
static unsigned char *apic_image(unsigned char *d, size_t n, int v22, size_t *outlen) {
	if (n < 4) return NULL;
	int enc = d[0];
	size_t p = 1;
	if (v22) p += 3;
	else { while (p < n && d[p]) p++; p++; } /* MIME type */
	p++;                                    /* picture type */
	if (enc == 1 || enc == 2) {             /* UTF-16 description, ends with 00 00 */
		while (p + 1 < n && (d[p] || d[p + 1])) p += 2;
		p += 2;
	} else {
		while (p < n && d[p]) p++;
		p++;
	}
	if (p >= n) return NULL;
	*outlen = n - p;
	unsigned char *img = xmalloc(*outlen);
	memcpy(img, d + p, *outlen);
	return img;
}

static int read_id3v2(FILE *f, Meta *m, unsigned char **pic, size_t *piclen) {
	unsigned char h[10];
	if (fseeko(f, 0, SEEK_SET) != 0 || fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3)) return 0;
	int ver = h[3];
	if (ver < 2 || ver > 4) return 0;
	long long end = 10 + (long long)syncsafe(h + 6);
	long long pos = 10;
	if ((h[5] & 0x40) && ver >= 3) {
		unsigned char e[4];
		if (fread(e, 1, 4, f) != 4) return 0;
		pos += ver == 4 ? syncsafe(e) : 4 + ((unsigned)e[0] << 24 | (unsigned)e[1] << 16 | (unsigned)e[2] << 8 | e[3]);
	}
	int hl = ver == 2 ? 6 : 10;
	char albart[256] = "";
	while (pos + hl <= end) {
		unsigned char fh[10];
		if (fseeko(f, pos, SEEK_SET) != 0 || fread(fh, 1, (size_t)hl, f) != (size_t)hl || !fh[0]) break;
		size_t fs = ver == 2 ? ((size_t)fh[3] << 16 | (size_t)fh[4] << 8 | fh[5])
		          : ver == 4 ? syncsafe(fh + 4) : ((size_t)fh[4] << 24 | (size_t)fh[5] << 16 | (size_t)fh[6] << 8 | fh[7]);
		if (fs == 0 || pos + hl + (long long)fs > end) break;
		const char *id = (const char *)fh;
		char *dst = NULL;
		size_t dn = 0;
		if (ver == 2) {
			if (!memcmp(id, "TT2", 3)) { dst = m->title; dn = sizeof m->title; }
			else if (!memcmp(id, "TP1", 3)) { dst = m->artist; dn = sizeof m->artist; }
			else if (!memcmp(id, "TP2", 3)) { dst = albart; dn = sizeof albart; }
			else if (!memcmp(id, "TAL", 3)) { dst = m->album; dn = sizeof m->album; }
			else if (!memcmp(id, "TCO", 3)) { dst = m->genre; dn = sizeof m->genre; }
		} else {
			if (!memcmp(id, "TIT2", 4)) { dst = m->title; dn = sizeof m->title; }
			else if (!memcmp(id, "TPE1", 4)) { dst = m->artist; dn = sizeof m->artist; }
			else if (!memcmp(id, "TPE2", 4)) { dst = albart; dn = sizeof albart; }
			else if (!memcmp(id, "TALB", 4)) { dst = m->album; dn = sizeof m->album; }
			else if (!memcmp(id, "TCON", 4)) { dst = m->genre; dn = sizeof m->genre; }
		}
		int is_pic = ver == 2 ? !memcmp(id, "PIC", 3) : !memcmp(id, "APIC", 4);
		if (dst && fs < 4096) {
			unsigned char d[4096];
			if (fread(d, 1, fs, f) == fs) music_id3_text(d, fs, dst, dn);
		} else if (is_pic) {
			m->has_art = 1;
			if (pic && !*pic && fs < (8u << 20)) {
				int unsync = ver == 4 && (fh[9] & 0x02);
				int dli = ver == 4 && (fh[9] & 0x01);
				unsigned char *d = xmalloc(fs);
				if (!unsync && fread(d, 1, fs, f) == fs) {
					size_t off = dli ? 4 : 0;
					if (fs > off) *pic = apic_image(d + off, fs - off, ver == 2, piclen);
				}
				free(d);
			}
		}
		pos += hl + (long long)fs;
	}
	if (!m->artist[0] && albart[0]) sncpy(m->artist, albart, sizeof m->artist);
	return 1;
}

static void read_id3v1(FILE *f, Meta *m) {
	unsigned char t[128];
	if (fseeko(f, -128, SEEK_END) != 0 || fread(t, 1, 128, f) != 128 || memcmp(t, "TAG", 3)) return;
	char b[31];
	struct { char *dst; size_t n; int off; } F[] = {{m->title, sizeof m->title, 3}, {m->artist, sizeof m->artist, 33}, {m->album, sizeof m->album, 63}};
	for (int i = 0; i < 3; i++) {
		if (F[i].dst[0]) continue;
		memcpy(b, t + F[i].off, 30);
		b[30] = 0;
		/* Latin-1 -> UTF-8 */
		size_t o = 0;
		for (int k = 0; b[k] && o + 3 < F[i].n; k++) {
			unsigned char c = (unsigned char)b[k];
			if (c < 0x80) F[i].dst[o++] = (char)c;
			else { F[i].dst[o++] = (char)(0xC0 | (c >> 6)); F[i].dst[o++] = (char)(0x80 | (c & 0x3F)); }
		}
		F[i].dst[o] = 0;
		str_trim(F[i].dst);
	}
	if (!m->genre[0] && t[127] < ARRAY_LEN(ID3_GENRES)) sncpy(m->genre, ID3_GENRES[t[127]], sizeof m->genre);
}

static void vorbis_pick(const char *c, size_t len, Meta *m) {
	struct { const char *key; char *dst; size_t n; } K[] = {
		{"TITLE=", m->title, sizeof m->title}, {"ARTIST=", m->artist, sizeof m->artist},
		{"ALBUM=", m->album, sizeof m->album}, {"GENRE=", m->genre, sizeof m->genre},
	};
	for (int i = 0; i < 4; i++) {
		size_t kl = strlen(K[i].key);
		if (len > kl && !strncasecmp(c, K[i].key, kl) && !K[i].dst[0]) snfmt(K[i].dst, K[i].n, "%.*s", (int)(len - kl), c + kl);
	}
	if (len > 23 && !strncasecmp(c, "METADATA_BLOCK_PICTURE=", 23)) m->has_art = 1;
	if (!m->artist[0] && len > 12 && !strncasecmp(c, "ALBUMARTIST=", 12)) snfmt(m->artist, sizeof m->artist, "%.*s", (int)(len - 12), c + 12);
}

typedef struct { Meta *m; unsigned char **pic; size_t *piclen; } FlacCtx;
static void flac_cb(void *ud, drflac_metadata *md) {
	FlacCtx *c = ud;
	if (md->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
		drflac_vorbis_comment_iterator it;
		drflac_init_vorbis_comment_iterator(&it, md->data.vorbis_comment.commentCount, md->data.vorbis_comment.pComments);
		drflac_uint32 len;
		const char *s;
		while ((s = drflac_next_vorbis_comment(&it, &len)) != NULL) vorbis_pick(s, len, c->m);
	} else if (md->type == DRFLAC_METADATA_BLOCK_TYPE_PICTURE) {
		c->m->has_art = 1;
		if (c->pic && !*c->pic && md->data.picture.pPictureData && md->data.picture.pictureDataSize > 0) {
			*c->piclen = md->data.picture.pictureDataSize;
			*c->pic = xmalloc(*c->piclen);
			memcpy(*c->pic, md->data.picture.pPictureData, *c->piclen);
		}
	}
}

/* MP4 / M4A: moov.mvhd for the length, moov.udta.meta.ilst for the tags */
static uint32_t be32(const unsigned char *b) { return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }

static void mp4_ilst(const unsigned char *b, size_t n, Meta *m, unsigned char **pic, size_t *piclen) {
	for (size_t p = 0; p + 8 <= n;) {
		uint32_t sz = be32(b + p);
		if (sz < 8 || p + sz > n) break;
		const unsigned char *tag = b + p + 4;
		/* first child is the 'data' atom: size, 'data', type(4), locale(4), payload */
		if (sz >= 24 && !memcmp(b + p + 12, "data", 4)) {
			uint32_t dsz = be32(b + p + 8);
			if (dsz >= 16 && 8 + dsz <= sz) {
				const unsigned char *pl = b + p + 24;
				size_t pn = dsz - 16;
				char *dst = NULL;
				size_t dn = 0;
				if (!memcmp(tag, "\xA9nam", 4)) { dst = m->title; dn = sizeof m->title; }
				else if (!memcmp(tag, "\xA9" "ART", 4)) { dst = m->artist; dn = sizeof m->artist; }
				else if (!memcmp(tag, "\xA9" "alb", 4)) { dst = m->album; dn = sizeof m->album; }
				else if (!memcmp(tag, "\xA9gen", 4)) { dst = m->genre; dn = sizeof m->genre; }
				else if (!memcmp(tag, "gnre", 4) && pn >= 2 && !m->genre[0]) {
					int k = (pl[0] << 8 | pl[1]) - 1;
					if (k >= 0 && k < ARRAY_LEN(ID3_GENRES)) sncpy(m->genre, ID3_GENRES[k], sizeof m->genre);
				} else if (!memcmp(tag, "covr", 4)) {
					m->has_art = 1;
					if (pic && !*pic && pn) { *pic = xmalloc(pn); memcpy(*pic, pl, pn); *piclen = pn; }
				}
				if (dst && !dst[0]) snfmt(dst, dn, "%.*s", (int)MIN(pn, dn - 1), (const char *)pl);
			}
		}
		p += sz;
	}
}

static int mp4_walk(const unsigned char *b, size_t n, Meta *m, unsigned char **pic, size_t *piclen, int depth) {
	for (size_t p = 0; p + 8 <= n && depth < 6;) {
		uint32_t sz = be32(b + p);
		if (sz < 8 || p + sz > n) break;
		const unsigned char *t = b + p + 4;
		if (!memcmp(t, "mvhd", 4) && sz >= 32) {
			int v1 = b[p + 8] == 1;
			uint32_t scale = v1 ? be32(b + p + 28) : be32(b + p + 20);
			double dur = v1 ? (double)((uint64_t)be32(b + p + 32) << 32 | be32(b + p + 36)) : be32(b + p + 24);
			if (scale) m->dur = dur / scale;
		} else if (!memcmp(t, "udta", 4)) mp4_walk(b + p + 8, sz - 8, m, pic, piclen, depth + 1);
		else if (!memcmp(t, "meta", 4) && sz > 12) mp4_walk(b + p + 12, sz - 12, m, pic, piclen, depth + 1);
		else if (!memcmp(t, "ilst", 4)) mp4_ilst(b + p + 8, sz - 8, m, pic, piclen);
		p += sz;
	}
	return 0;
}

static void read_mp4(FILE *f, Meta *m, unsigned char **pic, size_t *piclen) {
	struct stat st;
	if (fstat(fileno(f), &st) != 0) return;
	unsigned long long pos = 0, fsize = (unsigned long long)st.st_size;
	for (int guard = 0; guard < 64 && pos + 8 <= fsize; guard++) {
		unsigned char h[16];
		if (fseeko(f, (off_t)pos, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8) return;
		unsigned long long sz = be32(h);
		unsigned hdr = 8;
		if (sz == 1) {
			if (fread(h + 8, 1, 8, f) != 8) return;
			sz = (unsigned long long)be32(h + 8) << 32 | be32(h + 12);
			hdr = 16;
		} else if (sz == 0) sz = fsize - pos;
		if (sz < hdr) return;
		if (!memcmp(h + 4, "moov", 4)) {
			size_t n = (size_t)MIN(sz - hdr, 24ull << 20);
			unsigned char *b = malloc(n ? n : 1);
			if (b && fread(b, 1, n, f) == n) mp4_walk(b, n, m, pic, piclen, 0);
			free(b);
			return;
		}
		pos += sz;
	}
}

/* Reads tags (and, when pic is given, the embedded picture). */
static void read_meta(const char *path, Meta *m, unsigned char **pic, size_t *piclen) {
	memset(m, 0, sizeof *m);
	const char *e = path_ext(path);
	if (str_ieq(e, "mp3") || str_ieq(e, "wav") || str_ieq(e, "aac")) {
		FILE *f = fopen(path, "rb");
		if (f) {
			read_id3v2(f, m, pic, piclen);
			if (str_ieq(e, "mp3")) read_id3v1(f, m);
			fclose(f);
		}
	} else if (str_ieq(e, "flac")) {
		FlacCtx c = {m, pic, piclen};
		drflac *fl = drflac_open_file_with_metadata(path, flac_cb, &c, NULL);
		if (fl) drflac_close(fl);
	} else if (str_ieq(e, "ogg") || str_ieq(e, "oga") || str_ieq(e, "opus")) {
		if (!str_ieq(e, "opus")) {
			stb_vorbis *v = stb_vorbis_open_filename(path, NULL, NULL);
			if (v) {
				stb_vorbis_comment c = stb_vorbis_get_comment(v);
				for (int i = 0; i < c.comment_list_length; i++) vorbis_pick(c.comment_list[i], strlen(c.comment_list[i]), m);
				stb_vorbis_close(v);
			}
		}
	} else if (str_ieq(e, "m4a") || str_ieq(e, "mp4") || str_ieq(e, "m4b") || str_ieq(e, "alac")) {
		FILE *f = fopen(path, "rb");
		if (f) { read_mp4(f, m, pic, piclen); fclose(f); }
	}
	genre_text(m->genre, sizeof m->genre);
	if (!pic && !m->dur) m->dur = music_length(path);
}

/* ------------------------------------------------------------- genres.txt */

typedef struct { char *artist, *genre; } GenreRule;
static GenreRule *rules;
static int nrules;

static void genres_path(char *out, size_t n) {
	char d[512];
	music_dir(d, sizeof d);
	snfmt(out, n, "%s/genres.txt", d);
}

static void rules_free(void) {
	for (int i = 0; i < nrules; i++) { free(rules[i].artist); free(rules[i].genre); }
	free(rules);
	rules = NULL;
	nrules = 0;
}

static void rules_load(void) {
	rules_free();
	char p[600];
	genres_path(p, sizeof p);
	char *txt = file_read_all(p, NULL);
	if (!txt) return;
	for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		str_trim(line);
		if (!line[0] || line[0] == '#') continue;
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		char *a = line, *g = eq + 1;
		str_trim(a);
		str_trim(g);
		if (!a[0] || !g[0]) continue;
		rules = xrealloc(rules, sizeof *rules * (size_t)(nrules + 1));
		rules[nrules].artist = xstrdup(a);
		rules[nrules].genre = xstrdup(g);
		nrules++;
	}
	free(txt);
}

static const char *rule_for(const char *artist) {
	char a[256], b[256];
	norm_key(artist, a, sizeof a);
	for (int i = 0; i < nrules; i++) {
		norm_key(rules[i].artist, b, sizeof b);
		if (!strcmp(a, b)) return rules[i].genre;
	}
	return NULL;
}

static int cmp_rule(const void *x, const void *y) {
	const GenreRule *a = x, *b = y;
	return str_icmp_natural(a->artist, b->artist);
}

static void rules_save(void) {
	char p[600];
	genres_path(p, sizeof p);
	if ((nrules) > 1) qsort(rules, (size_t)nrules, sizeof *rules, cmp_rule);
	size_t cap = 512;
	for (int i = 0; i < nrules; i++) cap += strlen(rules[i].artist) + strlen(rules[i].genre) + 8;
	char *b = xmalloc(cap);
	size_t o = (size_t)snfmt(b, cap,
	                         "# Zappo music genres - one artist per line: Artist = Genre\r\n"
	                         "# The Music tab groups artists by these. Edit here or on the handheld\r\n"
	                         "# (Music tab > select an artist > X > Set genre).\r\n\r\n");
	for (int i = 0; i < nrules; i++) o += (size_t)snfmt(b + o, cap - o, "%s = %s\r\n", rules[i].artist, rules[i].genre);
	char d[512];
	path_dir(p, d, sizeof d);
	mkdir_p(d);
	file_write_all(p, b, o);
	free(b);
}

static const char *PRESETS[] = {
	"Hip-Hop & Rap", "UK Rap", "Grime", "UK Rap & Grime", "UK Garage", "Drill", "R&B & Soul", "Pop", "Rock", "80s & New Wave",
	"Indie & Britpop", "Indie & Alternative", "Dance & Electronic", "House", "Drum & Bass", "Reggae & Dancehall",
	"Afrobeats", "Metal", "Punk", "Jazz", "Classical", "Country", "Folk", "Soundtrack", "Game Music", "Chill", "Other",
};
const char *const *lib_genre_presets(int *n) {
	*n = ARRAY_LEN(PRESETS);
	return PRESETS;
}

/* -------------------------------------------------------------- songs.txt */

/* Music/songs.txt: "file name = Artist | Title" for songs whose file name
 * can't be read automatically. */
typedef struct { char *file, *artist, *title; } SongRule;
static SongRule *srules;
static int nsrules;

static void srules_load(void) {
	for (int i = 0; i < nsrules; i++) { free(srules[i].file); free(srules[i].artist); free(srules[i].title); }
	free(srules);
	srules = NULL;
	nsrules = 0;
	char d[512], p[600];
	music_dir(d, sizeof d);
	snfmt(p, sizeof p, "%s/songs.txt", d);
	char *txt = file_read_all(p, NULL);
	if (!txt) return;
	for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		str_trim(line);
		if (!line[0] || line[0] == '#') continue;
		char *eq = strstr(line, " = ");
		if (!eq) continue;
		*eq = 0;
		char *rhs = eq + 3, *bar = strstr(rhs, " | ");
		char *a = bar ? rhs : NULL, *t = bar ? bar + 3 : rhs;
		if (bar) *bar = 0;
		str_trim(line);
		if (a) str_trim(a);
		str_trim(t);
		srules = xrealloc(srules, sizeof *srules * (size_t)(nsrules + 1));
		srules[nsrules].file = xstrdup(line);
		srules[nsrules].artist = xstrdup(a ? a : "");
		srules[nsrules].title = xstrdup(t);
		nsrules++;
	}
	free(txt);
}

static void srules_apply(Track *t) {
	const char *base = path_base(t->path);
	char stem[256];
	path_stem(t->path, stem, sizeof stem);
	for (int i = 0; i < nsrules; i++)
		if (str_ieq(srules[i].file, base) || str_ieq(srules[i].file, stem)) {
			if (srules[i].title[0]) { free(t->title); t->title = xstrdup(srules[i].title); }
			if (srules[i].artist[0]) {
				char a[256];
				sncpy(a, srules[i].artist, sizeof a);
				primary_artist(a);
				free(t->artist);
				t->artist = xstrdup(a);
			}
			return;
		}
}

/* ------------------------------------------------------------------ cache */

typedef struct {
	char *path;
	long long size, mtime;
	char *title, *artist, *album, *genre;
	int dur, art, tagged;
} CacheRow;

static int cmp_cache(const void *a, const void *b) { return strcmp(((const CacheRow *)a)->path, ((const CacheRow *)b)->path); }

static void cache_path(char *out, size_t n) { snfmt(out, n, "%s/music-cache.tsv", g_paths.clove); }

static CacheRow *cache_load(int *n) {
	*n = 0;
	char p[600];
	cache_path(p, sizeof p);
	char *txt = file_read_all(p, NULL);
	if (!txt) return NULL;
	CacheRow *rows = NULL;
	int cap = 0;
	char *save = NULL;
	char *line = strtok_r(txt, "\n", &save);
	if (!line || strcmp(line, "clove-music-cache 2")) { free(txt); return NULL; }
	while ((line = strtok_r(NULL, "\n", &save))) {
		char *f[10];
		int k = 0;
		f[k++] = line;
		for (char *q = line; *q && k < 10; q++)
			if (*q == '\t') { *q = 0; f[k++] = q + 1; }
		if (k != 10) continue;
		if (*n == cap) { cap = cap ? cap * 2 : 256; rows = xrealloc(rows, sizeof *rows * (size_t)cap); }
		CacheRow *r = &rows[(*n)++];
		r->path = xstrdup(f[0]);
		r->size = atoll(f[1]);
		r->mtime = atoll(f[2]);
		r->title = xstrdup(f[3]);
		r->artist = xstrdup(f[4]);
		r->album = xstrdup(f[5]);
		r->genre = xstrdup(f[6]);
		r->dur = atoi(f[7]);
		r->art = atoi(f[8]);
		r->tagged = atoi(f[9]);
	}
	free(txt);
	if ((*n) > 1) qsort(rows, (size_t)*n, sizeof *rows, cmp_cache);
	return rows;
}

static void cache_free(CacheRow *rows, int n) {
	for (int i = 0; i < n; i++) { free(rows[i].path); free(rows[i].title); free(rows[i].artist); free(rows[i].album); free(rows[i].genre); }
	free(rows);
}

static void tsv_clean(char *s) {
	for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

/* CFW/clove/music-list.tsv: every song's file (inside Music), artist, title
 * and album as Clove shows them, for "Get cover art.bat" on a PC. Only
 * rewritten when it changes. */
static void write_music_list(const char *root) {
	size_t cap = 256 + (size_t)g_lib.nt * 300, o = 0;
	char *b = xmalloc(cap);
	o += (size_t)snfmt(b, cap, "clove-music-list 1\tfile\tartist\ttitle\talbum\n");
	size_t rl = strlen(root);
	for (int i = 0; i < g_lib.nt; i++) {
		Track *t = &g_lib.t[i];
		const char *rel = t->path;
		if (!strncmp(rel, root, rl) && rel[rl] == '/') rel += rl + 1;
		char f[1024], ar[256], ti[256], al[256], line[2048];
		sncpy(f, rel, sizeof f); tsv_clean(f);
		sncpy(ar, t->artist, sizeof ar); tsv_clean(ar);
		sncpy(ti, t->title, sizeof ti); tsv_clean(ti);
		sncpy(al, t->album, sizeof al); tsv_clean(al);
		int l = snfmt(line, sizeof line, "song\t%s\t%s\t%s\t%s\n", f, ar, ti, al);
		if (o + (size_t)l + 1 > cap) { cap = cap * 2 + (size_t)l; b = xrealloc(b, cap); }
		memcpy(b + o, line, (size_t)l);
		o += (size_t)l;
	}
	for (int i = 0; i < g_lib.na; i++) {
		char ar[256], line[600];
		sncpy(ar, g_lib.a[i].name, sizeof ar); tsv_clean(ar);
		int l = snfmt(line, sizeof line, "artist\t\t%s\t\t\n", ar);
		if (o + (size_t)l + 1 > cap) { cap = cap * 2 + (size_t)l; b = xrealloc(b, cap); }
		memcpy(b + o, line, (size_t)l);
		o += (size_t)l;
	}
	char p[600];
	snfmt(p, sizeof p, "%s/music-list.tsv", g_paths.clove);
	size_t ol = 0;
	char *old = file_read_all(p, &ol);
	if (!old || ol != o || memcmp(old, b, o)) file_write_all(p, b, o);
	free(old);
	free(b);
}

/* ------------------------------------------------------------------- scan */

int lib_is_picture_dir(const char *name) {
	return !strcasecmp(name, "Artists") || !strcasecmp(name, "Covers") || !strcasecmp(name, "Playlists");
}

/* A name as Windows would save it: no \\ / : * ? " < > | and no dot or space
 * at the end. The cover-art tool on the PC uses the same rule. */
void lib_safe_name(const char *in, char *out, size_t n) {
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < n; p++) {
		unsigned char c = *p;
		if (c < 32 || strchr("\\/:*?\"<>|", c)) c = '_';
		out[o++] = (char)c;
	}
	while (o > 0 && (out[o - 1] == '.' || out[o - 1] == ' ')) o--;
	out[o] = 0;
	if (!o) sncpy(out, "_", n);
}

typedef struct { char *path; long long size, mtime; } FileRef;

static void walk(const char *dir, int depth, FileRef **v, int *n, int *cap) {
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[1024];
		snfmt(p, sizeof p, "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(p, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth == 0 && lib_is_picture_dir(e->d_name)) continue; /* Artists, Covers */
			if (depth < 5) walk(p, depth + 1, v, n, cap);
			continue;
		}
		if (music_kind(p) == MUSIC_NO) continue;
		if (*n == *cap) { *cap = *cap ? *cap * 2 : 256; *v = xrealloc(*v, sizeof **v * (size_t)*cap); }
		(*v)[*n].path = xstrdup(p);
		(*v)[*n].size = (long long)st.st_size;
		(*v)[*n].mtime = (long long)st.st_mtime;
		(*n)++;
	}
	closedir(d);
}

static void lib_free(void) {
	for (int i = 0; i < g_lib.nt; i++) {
		Track *t = &g_lib.t[i];
		free(t->path); free(t->title); free(t->artist); free(t->album); free(t->tag_genre);
	}
	for (int i = 0; i < g_lib.na; i++) { free(g_lib.a[i].name); free(g_lib.a[i].genre); free(g_lib.a[i].t); }
	for (int i = 0; i < g_lib.ng; i++) { free(g_lib.g[i].name); free(g_lib.g[i].a); }
	free(g_lib.t); free(g_lib.a); free(g_lib.g); free(g_lib.by_title);
	uint32_t gen = g_lib.gen;
	memset(&g_lib, 0, sizeof g_lib);
	g_lib.gen = gen;
}

/* "The Streets" sorts under S */
static const char *sort_name(const char *s) { return !strncasecmp(s, "the ", 4) && s[4] ? s + 4 : s; }

static int cmp_artist_idx(const void *a, const void *b) {
	const MArtist *x = &g_lib.a[*(const int *)a], *y = &g_lib.a[*(const int *)b];
	return str_icmp_natural(sort_name(x->name), sort_name(y->name));
}

static int cmp_track_title(const void *a, const void *b) {
	const Track *x = &g_lib.t[*(const int *)a], *y = &g_lib.t[*(const int *)b];
	int c = str_icmp_natural(x->title, y->title);
	return c ? c : str_icmp_natural(x->artist, y->artist);
}

static int cmp_track_album(const void *a, const void *b) {
	const Track *x = &g_lib.t[*(const int *)a], *y = &g_lib.t[*(const int *)b];
	/* loose songs A-Z first, then each album in its track order */
	if (!x->album[0] || !y->album[0]) {
		if (!x->album[0] && !y->album[0]) return str_icmp_natural(x->title, y->title);
		return x->album[0] ? 1 : -1;
	}
	int c = str_icmp_natural(x->album, y->album);
	if (c) return c;
	return str_icmp_natural(path_base(x->path), path_base(y->path)); /* keeps track numbers in order */
}

static int cmp_genre(const void *a, const void *b) {
	const MGenre *x = a, *y = b;
	int ux = !strcmp(x->name, LIB_UNSORTED), uy = !strcmp(y->name, LIB_UNSORTED);
	if (ux != uy) return ux - uy;
	return str_icmp_natural(x->name, y->name);
}

/* artists and genres from the tracks */
static void build_groups(void) {
	for (int i = 0; i < g_lib.na; i++) { free(g_lib.a[i].name); free(g_lib.a[i].genre); free(g_lib.a[i].t); }
	for (int i = 0; i < g_lib.ng; i++) { free(g_lib.g[i].name); free(g_lib.g[i].a); }
	free(g_lib.a); free(g_lib.g);
	g_lib.a = NULL; g_lib.g = NULL; g_lib.na = g_lib.ng = 0;

	/* artists */
	char key[256], k2[256];
	for (int i = 0; i < g_lib.nt; i++) {
		Track *t = &g_lib.t[i];
		norm_key(t->artist, key, sizeof key);
		int ai = -1;
		for (int j = 0; j < g_lib.na; j++) {
			norm_key(g_lib.a[j].name, k2, sizeof k2);
			if (!strcmp(key, k2)) { ai = j; break; }
		}
		if (ai < 0) {
			g_lib.a = xrealloc(g_lib.a, sizeof *g_lib.a * (size_t)(g_lib.na + 1));
			ai = g_lib.na++;
			memset(&g_lib.a[ai], 0, sizeof g_lib.a[ai]);
			g_lib.a[ai].name = xstrdup(t->artist);
		} else {
			/* "akon" and "Akon" are one artist: show the capitalised spelling */
			int up_old = 0, up_new = 0;
			for (const char *q = g_lib.a[ai].name; *q; q++) if (isupper((unsigned char)*q)) up_old = 1;
			for (const char *q = t->artist; *q; q++) if (isupper((unsigned char)*q)) up_new = 1;
			if (!up_old && up_new) { free(g_lib.a[ai].name); g_lib.a[ai].name = xstrdup(t->artist); }
		}
		MArtist *a = &g_lib.a[ai];
		a->t = xrealloc(a->t, sizeof(int) * (size_t)(a->n + 1));
		a->t[a->n++] = i;
		a->dur += t->dur;
		t->artist_i = ai;
	}
	/* each artist's genre: genres.txt, else the most common tag, else Unsorted */
	for (int j = 0; j < g_lib.na; j++) {
		MArtist *a = &g_lib.a[j];
		const char *g = rule_for(a->name);
		if (!g) {
			int best = 0;
			for (int x = 0; x < a->n; x++) {
				const char *tg = g_lib.t[a->t[x]].tag_genre;
				if (!tg[0]) continue;
				int c = 0;
				for (int y = 0; y < a->n; y++) if (str_ieq(g_lib.t[a->t[y]].tag_genre, tg)) c++;
				if (c > best) { best = c; g = tg; }
			}
		}
		a->genre = xstrdup(g && g[0] ? g : LIB_UNSORTED);
		/* albums */
		int albums = 0;
		for (int x = 0; x < a->n; x++) {
			const char *al = g_lib.t[a->t[x]].album;
			if (!al[0]) continue;
			int seen = 0;
			for (int y = 0; y < x; y++) if (str_ieq(g_lib.t[a->t[y]].album, al)) { seen = 1; break; }
			if (!seen) albums++;
		}
		a->albums = albums;
		if ((a->n) > 1) qsort(a->t, (size_t)a->n, sizeof(int), cmp_track_album);
		for (int x = 0; x < a->n; x++) g_lib.t[a->t[x]].genre = a->genre;
	}
	/* sort artists by name, keeping the tracks' artist_i right */
	int *order = xmalloc(sizeof(int) * (size_t)(g_lib.na ? g_lib.na : 1));
	for (int j = 0; j < g_lib.na; j++) order[j] = j;
	if ((g_lib.na) > 1) qsort(order, (size_t)g_lib.na, sizeof(int), cmp_artist_idx);
	MArtist *sorted = xmalloc(sizeof *sorted * (size_t)(g_lib.na ? g_lib.na : 1));
	for (int j = 0; j < g_lib.na; j++) {
		sorted[j] = g_lib.a[order[j]];
		for (int x = 0; x < sorted[j].n; x++) g_lib.t[sorted[j].t[x]].artist_i = j;
	}
	free(g_lib.a);
	g_lib.a = sorted;
	free(order);
	/* genres */
	for (int j = 0; j < g_lib.na; j++) {
		MArtist *a = &g_lib.a[j];
		int gi = -1;
		for (int k = 0; k < g_lib.ng; k++) if (str_ieq(g_lib.g[k].name, a->genre)) { gi = k; break; }
		if (gi < 0) {
			g_lib.g = xrealloc(g_lib.g, sizeof *g_lib.g * (size_t)(g_lib.ng + 1));
			gi = g_lib.ng++;
			memset(&g_lib.g[gi], 0, sizeof g_lib.g[gi]);
			g_lib.g[gi].name = xstrdup(a->genre);
		}
		MGenre *g = &g_lib.g[gi];
		g->a = xrealloc(g->a, sizeof(int) * (size_t)(g->n + 1));
		g->a[g->n++] = j;
		g->ntracks += a->n;
		g->dur += a->dur;
	}
	if (g_lib.ng > 1) qsort(g_lib.g, (size_t)g_lib.ng, sizeof *g_lib.g, cmp_genre);
	/* songs A-Z */
	free(g_lib.by_title);
	g_lib.by_title = xmalloc(sizeof(int) * (size_t)(g_lib.nt ? g_lib.nt : 1));
	for (int i = 0; i < g_lib.nt; i++) g_lib.by_title[i] = i;
	if ((g_lib.nt) > 1) qsort(g_lib.by_title, (size_t)g_lib.nt, sizeof(int), cmp_track_title);
	g_lib.gen++;
}

static int cmp_fileref(const void *a, const void *b) { return strcmp(((const FileRef *)a)->path, ((const FileRef *)b)->path); }

/* "Song (1).mp3" next to "Song.mp3" of the same size: a second download */
static void skip_duplicates(FileRef *f, int *n) {
	if (*n > 1) qsort(f, (size_t)*n, sizeof *f, cmp_fileref);
	char *dup = calloc((size_t)(*n ? *n : 1), 1);
	for (int i = 0; i < *n; i++) {
		char stem[256], dir[1024], orig[1300];
		path_stem(f[i].path, stem, sizeof stem);
		size_t l = strlen(stem);
		if (l <= 4 || stem[l - 1] != ')') continue;
		char *op = strrchr(stem, '(');
		if (!op || op == stem || op[-1] != ' ' || op[1] == ')') continue;
		int digits = 1;
		for (char *q = op + 1; *q != ')'; q++) if (!isdigit((unsigned char)*q)) digits = 0;
		if (!digits) continue;
		op[-1] = 0;
		path_dir(f[i].path, dir, sizeof dir);
		snfmt(orig, sizeof orig, "%s/%s.%s", dir, stem, path_ext(f[i].path));
		FileRef key = {.path = orig}, *hit = bsearch(&key, f, (size_t)*n, sizeof *f, cmp_fileref);
		if (hit && hit->size == f[i].size) dup[i] = 1;
	}
	int m = 0;
	for (int i = 0; i < *n; i++) {
		if (dup[i]) { free(f[i].path); continue; }
		f[m++] = f[i];
	}
	free(dup);
	*n = m;
}

static int cmp_strp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int count_sorted(char **v, int n, const char *k) {
	int lo = 0, hi = n;
	while (lo < hi) { int mid = (lo + hi) / 2; if (strcmp(v[mid], k) < 0) lo = mid + 1; else hi = mid; }
	int c = 0;
	while (lo + c < n && !strcmp(v[lo + c], k)) c++;
	return c;
}

/* Files named "X - Y" without tags: is X the artist or the song? Downloaders
 * differ ("Artist - Song" vs "Song - Channel"), so look at the whole library:
 * a name that turns up as Y in many files, or that is an artist elsewhere, is
 * the artist. Undecided ones follow the majority. */
typedef struct { int ti; char a[256], t[256], ka[128], kt[128]; int dec; } Cand;
static void orient_names(void) {
	int n = g_lib.nt;
	if (!n) return;
	Cand *c = xmalloc(sizeof *c * (size_t)n);
	int nc = 0;
	char *is_cand = calloc((size_t)n, 1);
	for (int i = 0; i < n; i++) {
		Track *t = &g_lib.t[i];
		if (t->tagged) continue;
		char stem[256];
		path_stem(t->path, stem, sizeof stem);
		int two;
		Cand *x = &c[nc];
		tidy_name_ex(stem, x->t, sizeof x->t, x->a, sizeof x->a, &two);
		if (!two) continue;
		char pa[256];
		sncpy(pa, x->a, sizeof pa);
		primary_artist(pa);
		norm_key(pa, x->ka, sizeof x->ka);
		char pt[256];
		sncpy(pt, x->t, sizeof pt);
		primary_artist(pt);
		norm_key(pt, x->kt, sizeof x->kt);
		x->ti = i;
		is_cand[i] = 1;
		nc++;
	}
	if (!nc) { free(c); free(is_cand); return; }
	char **known = xmalloc(sizeof(char *) * (size_t)n), **sa = xmalloc(sizeof(char *) * (size_t)nc), **st = xmalloc(sizeof(char *) * (size_t)nc);
	int nk = 0;
	for (int i = 0; i < n; i++) {
		if (is_cand[i]) continue;
		char k[128];
		norm_key(g_lib.t[i].artist, k, sizeof k);
		known[nk++] = xstrdup(k);
	}
	for (int i = 0; i < nc; i++) { sa[i] = c[i].ka; st[i] = c[i].kt; }
	if ((nk) > 1) qsort(known, (size_t)nk, sizeof(char *), cmp_strp);
	if ((nc) > 1) qsort(sa, (size_t)nc, sizeof(char *), cmp_strp);
	if ((nc) > 1) qsort(st, (size_t)nc, sizeof(char *), cmp_strp);
	int ta = 0, at = 0;
	for (int i = 0; i < nc; i++) {
		int score = (count_sorted(known, nk, c[i].kt) ? 2 : 0) + (count_sorted(st, nc, c[i].kt) - 1)
		          - (count_sorted(known, nk, c[i].ka) ? 2 : 0) - (count_sorted(sa, nc, c[i].ka) - 1);
		c[i].dec = score > 0 ? 1 : score < 0 ? -1 : 0;
		if (c[i].dec > 0) ta++;
		if (c[i].dec < 0) at++;
	}
	int def = ta > at ? 1 : -1;
	for (int i = 0; i < nc; i++) {
		if ((c[i].dec ? c[i].dec : def) <= 0) continue;
		Track *t = &g_lib.t[c[i].ti];
		char artist[256], title[256];
		sncpy(artist, c[i].t, sizeof artist);
		primary_artist(artist);
		sncpy(title, c[i].a, sizeof title);
		lib_tidy_title(title, artist);
		free(t->artist);
		free(t->title);
		t->artist = xstrdup(artist);
		t->title = xstrdup(title);
	}
	for (int i = 0; i < nk; i++) free(known[i]);
	free(known); free(sa); free(st); free(c); free(is_cand);
}

int lib_scan(void) {
	uint64_t t0 = now_ms();
	char root[512];
	music_dir(root, sizeof root);
	rules_load();
	FileRef *files = NULL;
	int nf = 0, cap = 0;
	walk(root, 0, &files, &nf, &cap);
	skip_duplicates(files, &nf);
	int nc = 0;
	CacheRow *cache = cache_load(&nc);
	lib_free();
	g_lib.t = xmalloc(sizeof *g_lib.t * (size_t)(nf ? nf : 1));
	int changed = 0, read = 0;
	for (int i = 0; i < nf; i++) {
		Track *t = &g_lib.t[g_lib.nt++];
		memset(t, 0, sizeof *t);
		t->path = files[i].path;
		t->kind = music_kind(t->path);
		CacheRow key = {.path = t->path}, *hit = nc ? bsearch(&key, cache, (size_t)nc, sizeof *cache, cmp_cache) : NULL;
		if (hit && hit->size == files[i].size && hit->mtime == files[i].mtime) {
			t->title = xstrdup(hit->title);
			t->artist = xstrdup(hit->artist);
			t->album = xstrdup(hit->album);
			t->tag_genre = xstrdup(hit->genre);
			t->dur = hit->dur;
			t->has_art = hit->art;
			t->tagged = hit->tagged;
			continue;
		}
		changed = 1;
		read++;
		Meta m;
		read_meta(t->path, &m, NULL, NULL);
		char title[256], artist[256], stem[256];
		path_stem(t->path, stem, sizeof stem);
		lib_tidy_name(stem, title, sizeof title, artist, sizeof artist);
		if (m.title[0]) {
			sncpy(title, m.title, sizeof title);
			/* a tag title like "Artist - Song (Official Video)" */
			char tt[256] = "", ta[256] = "";
			if (strstr(m.title, " - ") && (!m.artist[0])) {
				lib_tidy_name(m.title, tt, sizeof tt, ta, sizeof ta);
				if (ta[0]) { sncpy(title, tt, sizeof title); sncpy(artist, ta, sizeof artist); }
			}
			lib_tidy_title(title, artist);
		}
		if (m.artist[0]) sncpy(artist, m.artist, sizeof artist);
		primary_artist(artist);
		if (!artist[0]) sncpy(artist, "Unknown artist", sizeof artist);
		if (!m.album[0]) {
			/* songs in a folder inside Music: the folder is the album */
			char dir[1024];
			path_dir(t->path, dir, sizeof dir);
			if (strcmp(dir, root) && strncmp(dir, root, strlen(root)) == 0) {
				const char *fb = path_base(dir);
				char k1[256], k2[256];
				norm_key(fb, k1, sizeof k1);
				norm_key(artist, k2, sizeof k2);
				if (strcmp(k1, k2)) sncpy(m.album, fb, sizeof m.album);
			}
		}
		t->title = xstrdup(title);
		t->artist = xstrdup(artist);
		t->album = xstrdup(m.album);
		t->tag_genre = xstrdup(m.genre);
		t->dur = (int)(m.dur + 0.5);
		t->has_art = m.has_art;
		t->tagged = m.title[0] != 0;
	}
	free(files);
	if (cache) cache_free(cache, nc);
	if (nc != g_lib.nt) changed = 1;
	g_lib.scanned = 1;
	if (changed) {
		/* write the cache */
		size_t bcap = 64 + (size_t)g_lib.nt * 256, o = 0;
		char *b = xmalloc(bcap);
		o += (size_t)snfmt(b, bcap, "clove-music-cache 2\n");
		for (int i = 0; i < g_lib.nt; i++) {
			Track *t = &g_lib.t[i];
			struct stat st;
			if (stat(t->path, &st) != 0) continue;
			char line[2048], ti[256], ar[256], al[256], ge[96];
			sncpy(ti, t->title, sizeof ti); tsv_clean(ti);
			sncpy(ar, t->artist, sizeof ar); tsv_clean(ar);
			sncpy(al, t->album, sizeof al); tsv_clean(al);
			sncpy(ge, t->tag_genre, sizeof ge); tsv_clean(ge);
			int l = snfmt(line, sizeof line, "%s\t%lld\t%lld\t%s\t%s\t%s\t%s\t%d\t%d\t%d\n", t->path, (long long)st.st_size,
			              (long long)st.st_mtime, ti, ar, al, ge, t->dur, t->has_art, t->tagged);
			if (o + (size_t)l + 1 > bcap) { bcap = bcap * 2 + (size_t)l; b = xrealloc(b, bcap); }
			memcpy(b + o, line, (size_t)l);
			o += (size_t)l;
		}
		char p[600];
		cache_path(p, sizeof p);
		file_write_all(p, b, o);
		free(b);
	}
	orient_names();
	/* names fixed by hand win, then group */
	srules_load();
	for (int i = 0; i < g_lib.nt; i++) srules_apply(&g_lib.t[i]);
	build_groups();
	write_music_list(root);
	LOG("music: %d songs, %d artists, %d genres (%d read, %llu ms)", g_lib.nt, g_lib.na, g_lib.ng, read,
	    (unsigned long long)(now_ms() - t0));
	return g_lib.nt;
}

int lib_find(const char *path) {
	if (!path || !path[0]) return -1;
	for (int i = 0; i < g_lib.nt; i++)
		if (!strcmp(g_lib.t[i].path, path)) return i;
	return -1;
}

void lib_set_genre(const char *artist, const char *genre) {
	rules_load();
	char a[256], b[256];
	norm_key(artist, a, sizeof a);
	int found = -1;
	for (int i = 0; i < nrules; i++) {
		norm_key(rules[i].artist, b, sizeof b);
		if (!strcmp(a, b)) { found = i; break; }
	}
	if (found < 0) {
		rules = xrealloc(rules, sizeof *rules * (size_t)(nrules + 1));
		found = nrules++;
		rules[found].artist = xstrdup(artist);
		rules[found].genre = NULL;
	}
	free(rules[found].genre);
	rules[found].genre = xstrdup(genre);
	rules_save();
	build_groups();
}

/* ------------------------------------------------------------------ art */

static Image *try_image(const char *base, const char *const *exts, int n) {
	char p[1100];
	for (int i = 0; i < n; i++) {
		snfmt(p, sizeof p, "%s%s", base, exts[i]);
		if (file_exists(p)) {
			Image *im = img_load(p);
			if (im) return im;
		}
	}
	return NULL;
}

static const char *IMG_EXT[] = {".jpg", ".jpeg", ".png", ".JPG", ".PNG", ".webp_not_supported"};

static Image *artist_image(const char *name) {
	char root[512], base[900], safe[256];
	music_dir(root, sizeof root);
	snfmt(base, sizeof base, "%s/Artists/%s", root, name);
	Image *im = try_image(base, IMG_EXT, 5);
	if (im) return im;
	lib_safe_name(name, safe, sizeof safe);
	if (strcmp(safe, name)) {
		snfmt(base, sizeof base, "%s/Artists/%s", root, safe);
		im = try_image(base, IMG_EXT, 5);
	}
	return im;
}

/* Music/Covers/<song file name>.jpg (where the PC cover-art tool saves) */
static Image *covers_image(const char *stem) {
	char root[512], base[900];
	music_dir(root, sizeof root);
	snfmt(base, sizeof base, "%s/Covers/%s", root, stem);
	return try_image(base, IMG_EXT, 5);
}

Image *lib_art_for_track(int ti) {
	if (ti < 0 || ti >= g_lib.nt) return NULL;
	Track *t = &g_lib.t[ti];
	char dir[1024], stem[256], base[1100];
	path_dir(t->path, dir, sizeof dir);
	path_stem(t->path, stem, sizeof stem);
	/* 1. a picture with the song's name */
	snfmt(base, sizeof base, "%s/%s", dir, stem);
	Image *im = try_image(base, IMG_EXT, 5);
	if (im) return im;
	/* 1b. Music/Covers/<song> */
	if ((im = covers_image(stem))) return im;
	/* 2. inside the file */
	if (t->has_art) {
		Meta m;
		unsigned char *pic = NULL;
		size_t n = 0;
		read_meta(t->path, &m, &pic, &n);
		if (pic) {
			im = img_load_mem(pic, (int)MIN(n, (size_t)0x7FFFFFFF));
			free(pic);
			if (im) return im;
		}
	}
	/* 3. the folder's cover (not at the top of Music: that would be every loose song) */
	char root[512];
	music_dir(root, sizeof root);
	if (strcmp(dir, root)) {
		static const char *NAMES[] = {"cover", "folder", "front", "Cover", "Folder", "AlbumArt"};
		for (int i = 0; i < ARRAY_LEN(NAMES); i++) {
			snfmt(base, sizeof base, "%s/%s", dir, NAMES[i]);
			if ((im = try_image(base, IMG_EXT, 3))) return im;
		}
	}
	/* 4. the artist's picture */
	return artist_image(t->artist_i >= 0 && t->artist_i < g_lib.na ? g_lib.a[t->artist_i].name : t->artist);
}

Image *lib_art_for_artist(int ai) {
	if (ai < 0 || ai >= g_lib.na) return NULL;
	Image *im = artist_image(g_lib.a[ai].name);
	if (im) return im;
	MArtist *a = &g_lib.a[ai];
	for (int x = 0; x < a->n && x < 12; x++) {
		Track *t = &g_lib.t[a->t[x]];
		char dir[1024], stem[256], base[1100];
		path_dir(t->path, dir, sizeof dir);
		path_stem(t->path, stem, sizeof stem);
		snfmt(base, sizeof base, "%s/%s", dir, stem);
		if ((im = try_image(base, IMG_EXT, 5))) return im;
		if ((im = covers_image(stem))) return im;
		if (t->has_art) {
			Meta m;
			unsigned char *pic = NULL;
			size_t n = 0;
			read_meta(t->path, &m, &pic, &n);
			if (pic) { im = img_load_mem(pic, (int)n); free(pic); if (im) return im; }
		}
	}
	return NULL;
}
