/* Clove — music player. A background thread decodes the track (dr_mp3,
 * dr_flac, dr_wav, stb_vorbis — all public domain) and pipes 16-bit PCM to
 * GarlicOS's `aplay`, which paces playback. Nothing here touches the screen,
 * so the menu stays as responsive as without music. */
#include "music.h"
#include "musiclib.h"
#include "plat.h"
#include "util.h"

#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef __wasm__
#include <sys/wait.h>
#endif
#include <time.h>
#include <unistd.h>

#define DR_MP3_NO_STDIO_WIDE
#include "../third_party/dr_mp3.h"
#include "../third_party/dr_flac.h"
#include "../third_party/dr_wav.h"
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"

/* ------------------------------------------------------------------ files */

void music_dir(char *out, size_t n) { snfmt(out, n, "%s/Music", g_paths.mmc); }

int music_kind(const char *path) {
	const char *e = path_ext(path);
	static const char *in_clove[] = {"mp3", "ogg", "oga", "flac", "wav"};
	static const char *in_ra[] = {"m4a", "aac", "opus", "wma", "m4b", "mp4", "mka", "ape", "alac"};
	for (int i = 0; i < ARRAY_LEN(in_clove); i++) if (str_ieq(e, in_clove[i])) return MUSIC_CLOVE;
	for (int i = 0; i < ARRAY_LEN(in_ra); i++) if (str_ieq(e, in_ra[i])) return MUSIC_RETROARCH;
	return MUSIC_NO;
}

/* ------------------------------------------------------------------ tags */

static void latin1_to_utf8(const unsigned char *in, size_t n, char *out, size_t on) {
	size_t o = 0;
	for (size_t i = 0; i < n && in[i] && o + 3 < on; i++) {
		if (in[i] < 0x80) out[o++] = (char)in[i];
		else { out[o++] = (char)(0xC0 | (in[i] >> 6)); out[o++] = (char)(0x80 | (in[i] & 0x3F)); }
	}
	out[o] = 0;
}

static void utf16_to_utf8(const unsigned char *in, size_t n, int be, char *out, size_t on) {
	size_t o = 0;
	for (size_t i = 0; i + 1 < n && o + 4 < on; i += 2) {
		unsigned c = be ? (unsigned)(in[i] << 8 | in[i + 1]) : (unsigned)(in[i + 1] << 8 | in[i]);
		if (c == 0) break;
		if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) { /* surrogate pair */
			unsigned c2 = be ? (unsigned)(in[i + 2] << 8 | in[i + 3]) : (unsigned)(in[i + 3] << 8 | in[i + 2]);
			c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
			i += 2;
		}
		if (c < 0x80) out[o++] = (char)c;
		else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
		else { out[o++] = (char)(0xF0 | (c >> 18)); out[o++] = (char)(0x80 | ((c >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
	}
	out[o] = 0;
}

void music_id3_text(const unsigned char *p, size_t n, char *out, size_t on) {
	out[0] = 0;
	if (n < 2) return;
	int enc = p[0];
	p++; n--;
	if (enc == 0) latin1_to_utf8(p, n, out, on);
	else if (enc == 3) { size_t k = MIN(n, on - 1); memcpy(out, p, k); out[k] = 0; }
	else if (enc == 1 && n >= 2) utf16_to_utf8(p + 2, n - 2, p[0] == 0xFE, out, on);
	else if (enc == 2) utf16_to_utf8(p, n, 1, out, on);
	str_trim(out);
}

static int read_id3(const char *path, char *title, size_t tn, char *artist, size_t an) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	unsigned char h[10];
	int found = 0;
	if (fread(h, 1, 10, f) == 10 && !memcmp(h, "ID3", 3)) {
		int ver = h[3];
		size_t size = ((size_t)(h[6] & 0x7F) << 21) | ((size_t)(h[7] & 0x7F) << 14) | ((size_t)(h[8] & 0x7F) << 7) | (h[9] & 0x7F);
		if (size > (2u << 20)) size = 2u << 20; /* cover art can be big; titles come first */
		unsigned char *b = malloc(size);
		if (b && fread(b, 1, size, f) == size) {
			size_t p = 0;
			if ((h[5] & 0x40) && ver >= 3 && size > 4) { /* skip the extended header */
				if (ver == 4) p = ((size_t)(b[0] & 0x7F) << 21) | ((size_t)(b[1] & 0x7F) << 14) | ((size_t)(b[2] & 0x7F) << 7) | (b[3] & 0x7F);
				else p = 4 + ((size_t)b[0] << 24 | (size_t)b[1] << 16 | (size_t)b[2] << 8 | b[3]);
			}
			int idlen = ver == 2 ? 3 : 4, hl = ver == 2 ? 6 : 10;
			while (p + (size_t)hl <= size && b[p]) {
				size_t fs;
				if (ver == 2) fs = (size_t)b[p + 3] << 16 | (size_t)b[p + 4] << 8 | b[p + 5];
				else if (ver == 4) fs = ((size_t)(b[p + 4] & 0x7F) << 21) | ((size_t)(b[p + 5] & 0x7F) << 14) | ((size_t)(b[p + 6] & 0x7F) << 7) | (b[p + 7] & 0x7F);
				else fs = (size_t)b[p + 4] << 24 | (size_t)b[p + 5] << 16 | (size_t)b[p + 6] << 8 | b[p + 7];
				if (fs == 0 || p + hl + fs > size) break;
				const unsigned char *d = b + p + hl;
				if ((idlen == 4 && !memcmp(b + p, "TIT2", 4)) || (idlen == 3 && !memcmp(b + p, "TT2", 3))) { music_id3_text(d, fs, title, tn); found = 1; }
				if ((idlen == 4 && !memcmp(b + p, "TPE1", 4)) || (idlen == 3 && !memcmp(b + p, "TP1", 3))) { music_id3_text(d, fs, artist, an); found = 1; }
				p += hl + fs;
			}
		}
		free(b);
	}
	if (!title[0]) { /* ID3v1 at the end */
		unsigned char t[128];
		if (fseeko(f, -128, SEEK_END) == 0 && fread(t, 1, 128, f) == 128 && !memcmp(t, "TAG", 3)) {
			char tb[31], ab[31];
			memcpy(tb, t + 3, 30); tb[30] = 0;
			memcpy(ab, t + 33, 30); ab[30] = 0;
			latin1_to_utf8((unsigned char *)tb, 30, title, tn);
			if (!artist[0]) latin1_to_utf8((unsigned char *)ab, 30, artist, an);
			str_trim(title);
			str_trim(artist);
			found = 1;
		}
	}
	fclose(f);
	return found;
}

static void vorbis_comment_pick(const char *c, size_t len, char *title, size_t tn, char *artist, size_t an) {
	if (len > 6 && !strncasecmp(c, "TITLE=", 6) && !title[0]) { snfmt(title, tn, "%.*s", (int)(len - 6), c + 6); }
	if (len > 7 && !strncasecmp(c, "ARTIST=", 7) && !artist[0]) { snfmt(artist, an, "%.*s", (int)(len - 7), c + 7); }
}

typedef struct { char *title, *artist; size_t tn, an; } FlacTags;
static void flac_meta(void *ud, drflac_metadata *m) {
	FlacTags *t = ud;
	if (m->type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) return;
	drflac_vorbis_comment_iterator it;
	drflac_init_vorbis_comment_iterator(&it, m->data.vorbis_comment.commentCount, m->data.vorbis_comment.pComments);
	drflac_uint32 len;
	const char *c;
	while ((c = drflac_next_vorbis_comment(&it, &len)) != NULL) vorbis_comment_pick(c, len, t->title, t->tn, t->artist, t->an);
}

void music_tags(const char *path, char *title, size_t tn, char *artist, size_t an) {
	title[0] = artist[0] = 0;
	const char *e = path_ext(path);
	if (str_ieq(e, "mp3")) read_id3(path, title, tn, artist, an);
	else if (str_ieq(e, "ogg") || str_ieq(e, "oga")) {
		stb_vorbis *v = stb_vorbis_open_filename(path, NULL, NULL);
		if (v) {
			stb_vorbis_comment c = stb_vorbis_get_comment(v);
			for (int i = 0; i < c.comment_list_length; i++) vorbis_comment_pick(c.comment_list[i], strlen(c.comment_list[i]), title, tn, artist, an);
			stb_vorbis_close(v);
		}
	} else if (str_ieq(e, "flac")) {
		FlacTags t = {title, artist, tn, an};
		drflac *f = drflac_open_file_with_metadata(path, flac_meta, &t, NULL);
		if (f) drflac_close(f);
	}
	if (!title[0]) {
		/* "07 - Artist - Song_name.mp3", "Artist - Song (Official Video) - Channel.mp3" ... */
		char stem[256], t[256], a[256];
		path_stem(path, stem, sizeof stem);
		lib_tidy_name(stem, t, sizeof t, a, sizeof a);
		sncpy(title, t, tn);
		if (!artist[0]) sncpy(artist, a, an);
	} else {
		lib_tidy_title(title, artist);
	}
}

/* ---------------------------------------------------------------- decoding */

enum { D_NONE, D_MP3, D_FLAC, D_WAV, D_OGG };
typedef struct {
	int kind, rate, ch;
	double len;
	drmp3 mp3;
	drflac *flac;
	drwav wav;
	stb_vorbis *ogg;
	int mp3_indexed; /* the jump table below has been built for this file */
} Dec;

static double mp3_length(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	struct stat st;
	fstat(fileno(f), &st);
	unsigned char b[16384];
	size_t n = fread(b, 1, sizeof b, f);
	long long start = 0;
	if (n >= 10 && !memcmp(b, "ID3", 3)) {
		start = 10 + (((long long)(b[6] & 0x7F) << 21) | ((b[7] & 0x7F) << 14) | ((b[8] & 0x7F) << 7) | (b[9] & 0x7F));
		if (fseeko(f, (off_t)start, SEEK_SET) != 0) { fclose(f); return 0; }
		n = fread(b, 1, sizeof b, f);
	}
	fclose(f);
	static const int br1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
	static const int br2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
	static const int sr[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};
	for (size_t i = 0; i + 4 < n; i++) {
		if (b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) continue;
		int ver = (b[i + 1] >> 3) & 3, layer = (b[i + 1] >> 1) & 3, bri = b[i + 2] >> 4, sri = (b[i + 2] >> 2) & 3;
		if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) continue; /* layer III only */
		int mpeg1 = ver == 3;
		int rate = sr[mpeg1 ? 0 : ver == 2 ? 1 : 2][sri];
		int spf = mpeg1 ? 1152 : 576;
		int mono = (b[i + 3] >> 6) == 3;
		size_t xo = i + 4 + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
		if (xo + 12 < n && (!memcmp(b + xo, "Xing", 4) || !memcmp(b + xo, "Info", 4)) && (b[xo + 7] & 1)) {
			unsigned frames = (unsigned)b[xo + 8] << 24 | (unsigned)b[xo + 9] << 16 | (unsigned)b[xo + 10] << 8 | b[xo + 11];
			return (double)frames * spf / rate;
		}
		if (i + 4 + 32 + 18 < n && !memcmp(b + i + 4 + 32, "VBRI", 4)) {
			const unsigned char *v = b + i + 4 + 32;
			unsigned frames = (unsigned)v[14] << 24 | (unsigned)v[15] << 16 | (unsigned)v[16] << 8 | v[17];
			return (double)frames * spf / rate;
		}
		int kbps = mpeg1 ? br1[bri] : br2[bri];
		return kbps ? (double)(st.st_size - start - (long long)i) * 8.0 / (kbps * 1000.0) : 0;
	}
	return 0;
}

static void dec_close(Dec *d);
static int dec_open(Dec *d, const char *path);

double music_length(const char *path) {
	if (str_ieq(path_ext(path), "mp3")) return mp3_length(path);
	if (music_kind(path) != MUSIC_CLOVE) return 0;
	Dec d;
	if (dec_open(&d, path) != 0) return 0;
	double l = d.len;
	dec_close(&d);
	return l;
}

static int dec_open(Dec *d, const char *path) {
	memset(d, 0, sizeof *d);
	const char *e = path_ext(path);
	if (str_ieq(e, "mp3")) {
		if (!drmp3_init_file(&d->mp3, path, NULL)) return -1;
		d->kind = D_MP3;
		d->rate = (int)d->mp3.sampleRate;
		d->ch = (int)d->mp3.channels;
		d->len = mp3_length(path);
	} else if (str_ieq(e, "flac")) {
		d->flac = drflac_open_file(path, NULL);
		if (!d->flac) return -1;
		d->kind = D_FLAC;
		d->rate = (int)d->flac->sampleRate;
		d->ch = d->flac->channels;
		d->len = d->rate ? (double)d->flac->totalPCMFrameCount / d->rate : 0;
	} else if (str_ieq(e, "wav")) {
		if (!drwav_init_file(&d->wav, path, NULL)) return -1;
		d->kind = D_WAV;
		d->rate = (int)d->wav.sampleRate;
		d->ch = d->wav.channels;
		d->len = d->rate ? (double)d->wav.totalPCMFrameCount / d->rate : 0;
	} else if (str_ieq(e, "ogg") || str_ieq(e, "oga")) {
		d->ogg = stb_vorbis_open_filename(path, NULL, NULL);
		if (!d->ogg) return -1;
		d->kind = D_OGG;
		stb_vorbis_info vi = stb_vorbis_get_info(d->ogg);
		d->rate = (int)vi.sample_rate;
		d->ch = vi.channels;
		d->len = stb_vorbis_stream_length_in_seconds(d->ogg);
	} else {
		return -1;
	}
	/* more than two channels are cut down to the front pair in dec_read() */
	if (d->ch < 1 || d->ch > 8 || d->rate < 8000 || d->rate > 192000) { dec_close(d); return -1; }
	return 0;
}

/* Reads up to `frames` frames as interleaved stereo or mono s16. */
static long dec_read(Dec *d, int16_t *buf, int frames, int *out_ch) {
	*out_ch = d->ch > 2 ? 2 : d->ch;
	long got = 0;
	switch (d->kind) {
	case D_MP3: got = (long)drmp3_read_pcm_frames_s16(&d->mp3, (drmp3_uint64)frames, buf); break;
	case D_FLAC:
		if (d->ch <= 2) got = (long)drflac_read_pcm_frames_s16(d->flac, (drflac_uint64)frames, buf);
		else {
			static int16_t tmp[4096 * 8];
			int f = MIN(frames, 4096);
			got = (long)drflac_read_pcm_frames_s16(d->flac, (drflac_uint64)f, tmp);
			for (long i = 0; i < got; i++) { buf[i * 2] = tmp[i * d->ch]; buf[i * 2 + 1] = tmp[i * d->ch + 1]; }
		}
		break;
	case D_WAV:
		if (d->ch <= 2) got = (long)drwav_read_pcm_frames_s16(&d->wav, (drwav_uint64)frames, buf);
		else {
			static int16_t tmp[4096 * 8];
			int f = MIN(frames, 4096);
			got = (long)drwav_read_pcm_frames_s16(&d->wav, (drwav_uint64)f, tmp);
			for (long i = 0; i < got; i++) { buf[i * 2] = tmp[i * d->ch]; buf[i * 2 + 1] = tmp[i * d->ch + 1]; }
		}
		break;
	case D_OGG: got = stb_vorbis_get_samples_short_interleaved(d->ogg, *out_ch, buf, frames * *out_ch); break;
	}
	return got;
}

/* Moves to `frame` (counted from the start of the song); 0 when it can't. */
static int dec_seek(Dec *d, uint64_t frame) {
	switch (d->kind) {
	case D_MP3: {
		/* MP3 has no index of where each moment starts, so the first jump in a
		 * song reads through the file once (headers only, no decoding) to make
		 * one. Later jumps land straight on the nearest point. */
		static drmp3_seek_point pts[1024];
		if (!d->mp3_indexed) {
			d->mp3_indexed = 1;
			drmp3_uint32 n = (drmp3_uint32)(sizeof pts / sizeof pts[0]);
			uint64_t t = now_ms();
			if (drmp3_seek_to_pcm_frame(&d->mp3, 0) && drmp3_calculate_seek_points(&d->mp3, &n, pts) && n > 0)
				drmp3_bind_seek_table(&d->mp3, n, pts);
			LOG("music: indexed the MP3 for skipping (%u points, %d ms)", (unsigned)n, (int)(now_ms() - t));
		}
		return drmp3_seek_to_pcm_frame(&d->mp3, (drmp3_uint64)frame) ? 1 : 0;
	}
	case D_FLAC: return drflac_seek_to_pcm_frame(d->flac, (drflac_uint64)frame) ? 1 : 0;
	case D_WAV: return drwav_seek_to_pcm_frame(&d->wav, (drwav_uint64)frame) ? 1 : 0;
	case D_OGG: return stb_vorbis_seek(d->ogg, (unsigned)frame) ? 1 : 0;
	}
	return 0;
}

static void dec_close(Dec *d) {
	if (d->kind == D_MP3) drmp3_uninit(&d->mp3);
	if (d->kind == D_FLAC) drflac_close(d->flac);
	if (d->kind == D_WAV) drwav_uninit(&d->wav);
	if (d->kind == D_OGG) stb_vorbis_close(d->ogg);
	d->kind = D_NONE;
}

/* ------------------------------------------------------------- the player */

#ifndef __wasm__ /* the browser build plays through the page (music_web.inc) */
static pid_t player_pid = -1;
static int player_fd = -1;

static int player_start(int rate, int ch) {
	int p[2];
	if (pipe(p) != 0) return -1;
	pid_t pid = fork();
	if (pid < 0) { close(p[0]); close(p[1]); return -1; }
	if (pid == 0) {
		dup2(p[0], 0);
		int nul = open("/dev/null", O_WRONLY);
		if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
		for (int fd = 3; fd < 1024; fd++) close(fd);
		signal(SIGPIPE, SIG_DFL);
		char r[16], c[16];
		snprintf(r, sizeof r, "%d", rate);
		snprintf(c, sizeof c, "%d", ch);
		const char *cmd = getenv("CLOVE_AUDIO_CMD");
		if (cmd && *cmd) {
			setenv("RATE", r, 1);
			setenv("CHANNELS", c, 1);
#ifdef __ANDROID__
			execlp("sh", "sh", "-c", cmd, (char *)NULL); /* Termux: no /bin/sh */
#else
			execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
#endif
		} else {
			execl("/usr/bin/aplay", "aplay", "-q", "-t", "raw", "-f", "S16_LE", "-c", c, "-r", r, "-", (char *)NULL);
			execl("/bin/aplay", "aplay", "-q", "-t", "raw", "-f", "S16_LE", "-c", c, "-r", r, "-", (char *)NULL);
		}
		_exit(127);
	}
	close(p[0]);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	player_pid = pid;
	player_fd = p[1];
	return 0;
}

static void player_stop(int now) {
	if (player_fd >= 0) close(player_fd);
	player_fd = -1;
	if (player_pid > 0) {
		if (now) kill(player_pid, SIGTERM);
		int st;
		for (int i = 0; i < 100; i++) { /* up to 1 s for the last buffer to play */
			if (waitpid(player_pid, &st, WNOHANG) == player_pid) { player_pid = -1; return; }
			usleep(10000);
		}
		kill(player_pid, SIGKILL);
		waitpid(player_pid, &st, 0);
	}
	player_pid = -1;
}
#endif

/* --------------------------------------------------------------- the queue */

enum { C_NONE, C_STOP, C_NEXT, C_PREV, C_RESTART, C_QUIT };
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
#ifndef __wasm__
static pthread_t thread;
static int thread_up;
#endif
static char **queue;
static int *order;
static int qn, qpos;
static int want_play, cmd, in_track;
static int active, paused;
static double cur_pos, cur_len;
static double seek_to = -1; /* a jump (seconds into the song) the play thread hasn't made yet */
static double start_at = -1; /* the next track starts here (resuming after a restart) */
static int pause_after;      /* sleep timer "end of this song": pause when the track ends */
static int levelling = 1;    /* even out loudness between songs */
static int volume = 80, shuffle, repeat;
static char cur_path[1024], cur_title[160], cur_artist[160];
static char err_msg[160];
static int err_new;
static volatile uint32_t gen;
static int pace; /* host tests: play at real-time speed without a sound device */

static void build_order(int start) {
	free(order);
	order = xmalloc(sizeof(int) * (size_t)MAX(qn, 1));
	for (int i = 0; i < qn; i++) order[i] = i;
	if (shuffle && qn > 1) {
		/* the chosen track first, the rest shuffled */
		order[0] = start;
		order[start] = 0;
		for (int i = qn - 1; i > 1; i--) {
			int j = 1 + rand() % i;
			int t = order[i]; order[i] = order[j]; order[j] = t;
		}
		qpos = 0;
	} else {
		qpos = start;
	}
}

static void set_error(const char *fmt, const char *arg) {
	snfmt(err_msg, sizeof err_msg, fmt, arg);
	err_new = 1;
	LOG("music: %s", err_msg);
}

/* ------------------------------------------------------- volume levelling */

/* Each song's average loudness (dB of the RMS level, measured while it plays)
 * is kept in CFW/clove/loudness.tsv, so the next time it starts at the right
 * level straight away. Songs not measured yet adjust gently as they play. */
#define LV_TARGET (-16.0)  /* dBFS RMS everything is brought towards */
#define LV_MAX_UP 9.0
#define LV_MAX_DOWN (-12.0)
typedef struct { char *path; float db; } Loud;
static Loud *louds;
static int nlouds, clouds, louds_loaded, louds_dirty;

static void louds_file(char *out, size_t n) { snfmt(out, n, "%s/loudness.tsv", g_paths.clove); }
static void louds_load(void) {
	if (louds_loaded) return;
	louds_loaded = 1;
	char p[600];
	louds_file(p, sizeof p);
	char *txt = file_read_all(p, NULL);
	if (!txt) return;
	for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		if (nlouds == clouds) { clouds = clouds ? clouds * 2 : 128; louds = xrealloc(louds, sizeof *louds * (size_t)clouds); }
		louds[nlouds].path = xstrdup(line);
		louds[nlouds].db = (float)atof(tab + 1);
		nlouds++;
	}
	free(txt);
}
static int louds_find(const char *path) {
	for (int i = 0; i < nlouds; i++) if (!strcmp(louds[i].path, path)) return i;
	return -1;
}
static void louds_set(const char *path, double db) {
	int i = louds_find(path);
	if (i < 0) {
		if (nlouds == clouds) { clouds = clouds ? clouds * 2 : 128; louds = xrealloc(louds, sizeof *louds * (size_t)clouds); }
		i = nlouds++;
		louds[i].path = xstrdup(path);
	}
	louds[i].db = (float)db;
	louds_dirty = 1;
}
static void louds_save(void) {
	if (!louds_dirty) return;
	louds_dirty = 0;
	size_t cap = 64;
	for (int i = 0; i < nlouds; i++) cap += strlen(louds[i].path) + 16;
	char *b = xmalloc(cap);
	size_t o = 0;
	for (int i = 0; i < nlouds; i++) o += (size_t)snfmt(b + o, cap - o, "%s\t%.2f\n", louds[i].path, louds[i].db);
	char p[600];
	louds_file(p, sizeof p);
	file_write_all(p, b, o);
	free(b);
}
static double lv_gain_db(double loud_db) {
	double g = LV_TARGET - loud_db;
	return g > LV_MAX_UP ? LV_MAX_UP : g < LV_MAX_DOWN ? LV_MAX_DOWN : g;
}

/* Plays one track; returns why it stopped. Called without the lock. */
enum { R_END, R_ERROR, R_NEXT, R_PREV, R_STOP, R_RESTART, R_QUIT };
#ifdef __wasm__
#include "music_web.inc"
#else
static int play_track(const char *path) {
	static Dec d;
	if (dec_open(&d, path) != 0) {
		pthread_mutex_lock(&mu);
		set_error("Can't play %s", path_base(path));
		pthread_mutex_unlock(&mu);
		return R_ERROR;
	}
	int ch = d.ch > 2 ? 2 : d.ch;
	if (player_start(d.rate, ch) != 0) {
		dec_close(&d);
		pthread_mutex_lock(&mu);
		set_error("%s", "Couldn't start the sound player");
		pthread_mutex_unlock(&mu);
		return R_STOP;
	}
	pthread_mutex_lock(&mu);
	cur_len = d.len;
	cur_pos = 0;
	seek_to = -1;
	if (start_at > 0) { seek_to = start_at; cur_pos = start_at; }
	start_at = -1;
	int lv_on = levelling;
	gen++;
	pthread_mutex_unlock(&mu);

	/* volume levelling: a known loudness gives a steady gain; otherwise it is
	 * measured as the song plays and the gain follows it slowly */
	louds_load();
	int li = lv_on ? louds_find(path) : -1;
	double lv_db = li >= 0 ? lv_gain_db(louds[li].db) : 0.0; /* current gain, dB */
	double lv_sum = 0, lv_n = 0;     /* mean square of the whole song so far */
	double hp_prev_in = 0, hp_prev_out = 0;

	static int16_t buf[1152 * 2 * 2];
	uint64_t frames_done = 0, t0 = now_ms();
	int reason = R_END, cut = 0;
	for (;;) {
		pthread_mutex_lock(&mu);
		while (paused && cmd == C_NONE && seek_to < 0) pthread_cond_wait(&cv, &mu);
		int c = cmd;
		cmd = C_NONE;
		int vol = volume;
		double sk = seek_to;
		seek_to = -1;
		if (sk >= 0 && c == C_NONE) cur_pos = sk; /* shown straight away; presses made meanwhile add to it */
		pthread_mutex_unlock(&mu);
		if (c != C_NONE) {
			reason = c == C_STOP ? R_STOP : c == C_NEXT ? R_NEXT : c == C_PREV ? R_PREV : c == C_RESTART ? R_RESTART : R_QUIT;
			break;
		}
		if (sk >= 0) {
			/* skipping past the end is the same as the song finishing */
			if (d.len > 0 && sk >= d.len - 0.5) { cut = 1; break; }
			uint64_t target = (uint64_t)(sk * d.rate);
			if (dec_seek(&d, target)) frames_done = target;
			else LOG("music: couldn't skip to %.0f s in %s", sk, path_base(path));
			t0 = now_ms() - frames_done * 1000 / (uint64_t)d.rate;
			/* drop the sound already queued in the player so the jump is heard at once */
			player_stop(1);
			if (player_start(d.rate, ch) != 0) {
				pthread_mutex_lock(&mu);
				set_error("%s", "Couldn't start the sound player");
				pthread_mutex_unlock(&mu);
				reason = R_STOP;
				break;
			}
			pthread_mutex_lock(&mu);
			if (seek_to < 0) cur_pos = (double)frames_done / d.rate;
			gen++;
			pthread_mutex_unlock(&mu);
			continue;
		}
		int out_ch;
		long got = dec_read(&d, buf, 1152, &out_ch);
		if (got <= 0) break;
		long ns = got * out_ch;
		/* volume: a squared curve sounds even across the slider */
		double vgain = (double)vol * vol / 10000.0;
		if (lv_on) {
			/* loudness of this block, with the deep bass filtered out (it
			 * would otherwise make bass-heavy songs too quiet) */
			double ss = 0;
			for (long i = 0; i < ns; i++) {
				double x = buf[i] / 32768.0;
				double y = 0.985 * (hp_prev_out + x - hp_prev_in);
				hp_prev_in = x;
				hp_prev_out = y;
				ss += y * y;
			}
			lv_sum += ss;
			lv_n += (double)ns;
			if (li < 0 && lv_n > 0) {
				/* not measured before: head for the level measured so far,
				 * quickly in the first seconds, then gently */
				double ms = lv_sum / lv_n;
				if (ms > 1e-9) {
					/* until a song has been measured once, boost it only a little
					 * (a quiet intro mustn't make the loud part blare), and turn
					 * down faster than up */
					double want = lv_gain_db(10.0 * log10(ms));
					if (want > 4.0) want = 4.0;
					double secs = lv_n / (double)(d.rate * out_ch);
					double k = want < lv_db ? 0.03 : secs < 3.0 ? 0.05 : 0.004;
					lv_db += (want - lv_db) * k;
				}
			}
			vgain *= pow(10.0, lv_db / 20.0);
		}
		if (vgain < 0.999 || vgain > 1.001) {
			/* soft limit so turned-up quiet songs don't crackle */
			for (long i = 0; i < ns; i++) {
				double v = buf[i] * vgain;
				double a = v < 0 ? -v : v;
				if (a > 26000.0) {
					a = 26000.0 + (a - 26000.0) * 0.3;
					if (a > 32767.0) a = 32767.0;
					v = v < 0 ? -a : a;
				}
				buf[i] = (int16_t)v;
			}
		}
		const char *p = (const char *)buf;
		size_t left = (size_t)ns * 2;
		int dead = 0;
		while (left) {
			ssize_t w = write(player_fd, p, left);
			if (w < 0 && errno == EINTR) continue;
			if (w <= 0) { dead = 1; break; }
			p += w;
			left -= (size_t)w;
		}
		if (dead) {
			pthread_mutex_lock(&mu);
			set_error("%s", "The sound player stopped (is the sound device busy?)");
			pthread_mutex_unlock(&mu);
			reason = R_STOP;
			break;
		}
		frames_done += (uint64_t)got;
		if (pace) {
			uint64_t due = t0 + frames_done * 1000 / (uint64_t)d.rate;
			uint64_t now = now_ms();
			if (due > now + 5) usleep((useconds_t)(due - now) * 1000);
		}
		pthread_mutex_lock(&mu);
		cur_pos = (double)frames_done / d.rate;
		pthread_mutex_unlock(&mu);
	}
	dec_close(&d);
	player_stop(reason != R_END || cut);
	/* keep the song's loudness when enough of it was heard */
	if (lv_on && lv_n > 0 && (reason == R_END || lv_n / (double)(d.rate ? d.rate : 44100) > 120.0)) {
		double ms = lv_sum / lv_n;
		if (ms > 1e-9) { /* (the loudness list is only used on this thread) */
			louds_set(path, 10.0 * log10(ms));
			louds_save();
		}
	}
	return reason;
}

static void *thread_main(void *arg) {
	(void)arg;
	int errors = 0;
	pthread_mutex_lock(&mu);
	for (;;) {
		while (!want_play && cmd != C_QUIT) pthread_cond_wait(&cv, &mu);
		if (cmd == C_QUIT) break;
		want_play = 0;
		if (qn == 0) { active = 0; gen++; continue; }
		char path[1024], t[160], a[160];
		sncpy(path, queue[order[qpos]], sizeof path);
		active = 1;
		in_track = 1;
		cmd = cmd == C_RESTART ? C_NONE : cmd;
		pthread_mutex_unlock(&mu);
		music_tags(path, t, sizeof t, a, sizeof a); /* file reads: not under the lock */
		pthread_mutex_lock(&mu);
		sncpy(cur_path, path, sizeof cur_path);
		sncpy(cur_title, t, sizeof cur_title);
		sncpy(cur_artist, a, sizeof cur_artist);
		gen++;
		pthread_mutex_unlock(&mu);
		int r = play_track(path);
		pthread_mutex_lock(&mu);
		in_track = 0;
		if (r == R_ERROR) errors++;
		else errors = 0;
		switch (r) {
		case R_END:
		case R_ERROR:
			if (r == R_END && pause_after) { pause_after = 0; paused = 1; }
			if (r == R_ERROR && errors >= qn) { active = 0; break; }
			if (repeat == REPEAT_ONE && r == R_END) want_play = 1;
			else if (qpos + 1 < qn) { qpos++; want_play = 1; }
			else if (repeat == REPEAT_ALL) { qpos = 0; want_play = 1; }
			else active = 0;
			break;
		case R_NEXT: qpos = (qpos + 1) % qn; want_play = 1; break;
		case R_PREV:
			if (cur_pos < 3.0) qpos = (qpos + qn - 1) % qn;
			want_play = 1;
			break;
		case R_STOP: active = 0; paused = 0; break;
		case R_RESTART: break; /* music_play() queued the new list */
		case R_QUIT: goto out;
		}
		if (!active) { cur_pos = 0; }
		gen++;
	}
out:
	active = 0;
	pthread_mutex_unlock(&mu);
	return NULL;
}
#endif

/* ------------------------------------------------------------------- API */

void music_init(int vol, int shuf, int rep) {
	signal(SIGPIPE, SIG_IGN); /* a dead player must not take Clove with it */
	volume = CLAMP(vol, 0, 100);
	shuffle = shuf ? 1 : 0;
	repeat = CLAMP(rep, 0, 2);
	pace = getenv("CLOVE_AUDIO_PACE") != NULL;
	srand((unsigned)time(NULL));
	char d[512];
	music_dir(d, sizeof d);
	if (!dir_exists(d)) mkdir_p(d);
}

static void ensure_thread(void) {
#ifdef __wasm__
	return; /* no thread: music_pump() runs each frame */
#else
	if (thread_up) return;
	pthread_attr_t a;
	pthread_attr_init(&a);
	pthread_attr_setstacksize(&a, 256 * 1024);
	if (pthread_create(&thread, &a, thread_main, NULL) == 0) thread_up = 1;
	pthread_attr_destroy(&a);
#endif
}

void music_shutdown(void) {
#ifdef __wasm__
	if (w_open) { dec_close(&wd); w_open = 0; zx_audio_stop(1); }
#else
	if (!thread_up) return;
	pthread_mutex_lock(&mu);
	cmd = C_QUIT;
	paused = 0;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mu);
	pthread_join(thread, NULL);
	thread_up = 0;
	player_stop(1);
#endif
}

static void play_list(char *const *paths, int n, int start, int start_paused, double pos) {
	if (n <= 0) return;
	ensure_thread();
	pthread_mutex_lock(&mu);
	for (int i = 0; i < qn; i++) free(queue[i]);
	free(queue);
	queue = xmalloc(sizeof(char *) * (size_t)n);
	for (int i = 0; i < n; i++) queue[i] = xstrdup(paths[i]);
	qn = n;
	build_order(CLAMP(start, 0, n - 1));
	want_play = 1;
	paused = start_paused ? 1 : 0;
	start_at = pos > 1.0 ? pos : -1;
	active = 1;
	if (in_track) cmd = C_RESTART;
	gen++;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mu);
}

void music_play(char *const *paths, int n, int start) { play_list(paths, n, start, 0, -1); }

static void send_cmd(int c) {
	pthread_mutex_lock(&mu);
	if (in_track) { cmd = c; paused = 0; }
	else if (c == C_NEXT && active && qn) { qpos = (qpos + 1) % qn; want_play = 1; }
	gen++;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mu);
}

void music_toggle_pause(void) {
	pthread_mutex_lock(&mu);
	if (active) paused = !paused;
	gen++;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mu);
}
void music_resume(char *const *paths, int n, int start, double pos) { play_list(paths, n, start, 1, pos); }

void music_pause_after_song(int on) {
	pthread_mutex_lock(&mu);
	pause_after = on ? 1 : 0;
	pthread_mutex_unlock(&mu);
}
int music_pausing_after_song(void) {
	pthread_mutex_lock(&mu);
	int r = pause_after;
	pthread_mutex_unlock(&mu);
	return r;
}

void music_set_levelling(int on) {
	pthread_mutex_lock(&mu);
	levelling = on ? 1 : 0;
	pthread_mutex_unlock(&mu);
}

/* the queue, the song and where in it, for picking up after a restart */
void music_save_state(const char *file) {
	pthread_mutex_lock(&mu);
	if (!active || qn <= 0 || !order) { pthread_mutex_unlock(&mu); unlink(file); return; }
	size_t cap = 128;
	for (int i = 0; i < qn; i++) cap += strlen(queue[i]) + 2;
	char *b = xmalloc(cap);
	size_t o = (size_t)snfmt(b, cap, "clove-music-state 1\t%d\t%.1f\n", order[qpos], seek_to >= 0 ? seek_to : cur_pos);
	for (int i = 0; i < qn; i++) o += (size_t)snfmt(b + o, cap - o, "%s\n", queue[i]);
	pthread_mutex_unlock(&mu);
	file_write_all(file, b, o);
	free(b);
}

int music_load_state(const char *file) {
	char *txt = file_read_all(file, NULL);
	if (!txt) return 0;
	int start = 0, n = 0, cap = 64, ok = 0;
	double pos = 0;
	char **v = xmalloc(sizeof(char *) * (size_t)cap);
	char *line = strtok(txt, "\n");
	if (line && !strncmp(line, "clove-music-state 1\t", 20)) {
		start = atoi(line + 20);
		char *t = strchr(line + 20, '\t');
		pos = t ? atof(t + 1) : 0;
		int skipped = 0, want = start;
		for (int i = 0; (line = strtok(NULL, "\n")); i++) {
			if (!file_exists(line)) { if (i < want) skipped++; if (i == want) start = -1; continue; }
			if (n == cap) { cap *= 2; v = xrealloc(v, sizeof(char *) * (size_t)cap); }
			v[n++] = xstrdup(line);
		}
		if (start < 0) { start = 0; pos = 0; } /* the song itself has gone */
		else start -= skipped;
		if (n > 0) { music_resume(v, n, CLAMP(start, 0, n - 1), pos); ok = 1; }
	}
	for (int i = 0; i < n; i++) free(v[i]);
	free(v);
	free(txt);
	return ok;
}

void music_seek(double delta) {
	pthread_mutex_lock(&mu);
	if (active && in_track) {
		double t = (seek_to >= 0 ? seek_to : cur_pos) + delta;
		if (t < 0) t = 0;
		if (cur_len > 0 && t > cur_len) t = cur_len;
		seek_to = t;
		gen++;
		pthread_cond_broadcast(&cv);
	}
	pthread_mutex_unlock(&mu);
}

void music_stop(void) { send_cmd(C_STOP); }
void music_next(void) { send_cmd(C_NEXT); }
void music_prev(void) { send_cmd(C_PREV); }

void music_set_volume(int v) {
	pthread_mutex_lock(&mu);
	volume = CLAMP(v, 0, 100);
	gen++;
	pthread_mutex_unlock(&mu);
}
void music_set_shuffle(int on) {
	pthread_mutex_lock(&mu);
	shuffle = on ? 1 : 0;
	if (qn > 1) {
		int cur = order ? order[qpos] : 0;
		build_order(cur);
	}
	gen++;
	pthread_mutex_unlock(&mu);
}
void music_set_repeat(int mode) {
	pthread_mutex_lock(&mu);
	repeat = CLAMP(mode, 0, 2);
	gen++;
	pthread_mutex_unlock(&mu);
}

void music_info(MusicInfo *o) {
	pthread_mutex_lock(&mu);
	memset(o, 0, sizeof *o);
	o->active = active;
	o->paused = paused;
	sncpy(o->title, cur_title, sizeof o->title);
	sncpy(o->artist, cur_artist, sizeof o->artist);
	sncpy(o->path, active ? cur_path : "", sizeof o->path);
	o->pos = seek_to >= 0 ? seek_to : cur_pos;
	o->len = cur_len;
	o->index = qpos;
	o->count = qn;
	o->volume = volume;
	o->shuffle = shuffle;
	o->repeat = repeat;
	pthread_mutex_unlock(&mu);
	/* the names the Music tab shows (tidied, songs.txt fixes) when the song is in the library */
	int ti = o->path[0] && g_lib.scanned ? lib_find(o->path) : -1;
	if (ti >= 0) {
		sncpy(o->title, g_lib.t[ti].title, sizeof o->title);
		sncpy(o->artist, g_lib.t[ti].artist, sizeof o->artist);
	}
}

int music_is_playing(void) {
	pthread_mutex_lock(&mu);
	int r = active && !paused;
	pthread_mutex_unlock(&mu);
	return r;
}

uint32_t music_generation(void) {
	pthread_mutex_lock(&mu);
	uint32_t g = gen;
	pthread_mutex_unlock(&mu);
	return g;
}

int music_take_error(char *out, size_t n) {
	pthread_mutex_lock(&mu);
	int r = err_new;
	if (r) sncpy(out, err_msg, n);
	err_new = 0;
	pthread_mutex_unlock(&mu);
	return r;
}
