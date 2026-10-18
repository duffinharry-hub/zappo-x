/* Clove — moving an MP4's index to the front so GarlicOS's FFmpeg core can
 * open it. See video.h. */
#include "video.h"
#include "util.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define MAX_MOOV (96u << 20) /* a 3-hour film's index is a few MB */
#define COPY_BUF (1u << 20)

typedef struct {
	long long pos, size;
	int found;
} Box;

typedef struct {
	long long fsize;
	Box ftyp, moov, mdat; /* mdat = the first one */
} Layout;

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static void put64(unsigned char *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }

static int is_mp4_name(const char *path) {
	const char *dot = strrchr(path, '.');
	if (!dot) return 0;
	return !strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".m4v") || !strcasecmp(dot, ".mov");
}

long long video_file_size(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return -1;
	return (long long)st.st_size;
}

/* the top-level boxes; 0 when it looks like a sound MP4 file */
static int read_layout(FILE *f, Layout *L) {
	memset(L, 0, sizeof *L);
	if (fseeko(f, 0, SEEK_END) != 0) return 0;
	L->fsize = (long long)ftello(f);
	long long pos = 0;
	int n = 0;
	while (pos + 8 <= L->fsize && n++ < 4096) {
		unsigned char h[16];
		if (fseeko(f, (off_t)pos, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8) return 0;
		long long size = be32(h);
		int hdr = 8;
		if (size == 1) {
			if (fread(h + 8, 1, 8, f) != 8) return 0;
			size = (long long)be64(h + 8);
			hdr = 16;
		} else if (size == 0) {
			size = L->fsize - pos;
		}
		if (size < hdr || pos + size > L->fsize) {
			/* a broken last box: tolerate only after the parts we need */
			break;
		}
		Box *b = NULL;
		if (!memcmp(h + 4, "ftyp", 4)) b = &L->ftyp;
		else if (!memcmp(h + 4, "moov", 4)) b = &L->moov;
		else if (!memcmp(h + 4, "mdat", 4)) b = &L->mdat;
		if (b && !b->found) { b->pos = pos; b->size = size; b->found = 1; }
		pos += size;
	}
	return L->moov.found && L->mdat.found;
}

int video_needs_prep(const char *path) {
	if (!is_mp4_name(path)) return 0;
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	Layout L;
	int ok = read_layout(f, &L);
	fclose(f);
	return ok && L.moov.pos > L.mdat.pos && L.moov.size <= (long long)MAX_MOOV;
}

int video_duration(const char *path) {
	if (!is_mp4_name(path)) return 0;
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	Layout L;
	int secs = 0;
	if (read_layout(f, &L)) {
		/* mvhd is a direct child of moov, normally the first one */
		long long pos = L.moov.pos + 8, end = L.moov.pos + L.moov.size;
		int n = 0;
		while (pos + 8 <= end && n++ < 64) {
			unsigned char h[40];
			if (fseeko(f, (off_t)pos, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8) break;
			long long size = be32(h);
			if (size == 1 || size < 8) break;
			if (!memcmp(h + 4, "mvhd", 4)) {
				if (fread(h + 8, 1, 32, f) != 32) break;
				int ver = h[8];
				uint64_t scale, dur;
				if (ver == 0) { scale = be32(h + 20); dur = be32(h + 24); }
				else { if (fseeko(f, (off_t)pos + 8, SEEK_SET) != 0 || fread(h, 1, 40, f) != 40) break; scale = be32(h + 20); dur = be64(h + 24); }
				if (scale) secs = (int)(dur / scale);
				break;
			}
			pos += size;
		}
	}
	fclose(f);
	return secs;
}

/* add delta to every chunk offset in [lo, hi) inside the moov box */
static int patch_offsets(unsigned char *b, size_t start, size_t end, uint64_t lo, uint64_t hi, uint64_t delta, int depth) {
	size_t p = start;
	while (p + 8 <= end) {
		uint64_t size = be32(b + p);
		size_t hdr = 8;
		if (size == 1) {
			if (p + 16 > end) return -1;
			size = be64(b + p + 8);
			hdr = 16;
		} else if (size == 0) {
			size = end - p;
		}
		if (size < hdr || size > end - p) return -1;
		const unsigned char *t = b + p + 4;
		if (depth < 8 && (!memcmp(t, "trak", 4) || !memcmp(t, "mdia", 4) || !memcmp(t, "minf", 4) || !memcmp(t, "stbl", 4))) {
			if (patch_offsets(b, p + hdr, p + (size_t)size, lo, hi, delta, depth + 1) != 0) return -1;
		} else if (!memcmp(t, "stco", 4) || !memcmp(t, "co64", 4)) {
			int wide = t[0] == 'c';
			size_t q = p + hdr + 4; /* version + flags */
			if (q + 4 > p + size) return -1;
			uint32_t count = be32(b + q);
			q += 4;
			size_t w = wide ? 8 : 4;
			if ((uint64_t)count * w > p + size - q) return -1;
			for (uint32_t i = 0; i < count; i++, q += w) {
				uint64_t off = wide ? be64(b + q) : be32(b + q);
				if (off < lo || off >= hi) continue;
				off += delta;
				if (wide) put64(b + q, off);
				else {
					if (off > 0xFFFFFFFFull) return -2; /* would need 64-bit offsets */
					put32(b + q, (uint32_t)off);
				}
			}
		}
		p += (size_t)size;
	}
	return 0;
}

typedef struct {
	long long from, len; /* -1 from = the patched moov */
} Seg;

struct VideoPrep {
	char path[1024], tmp[1100];
	FILE *in, *out;
	unsigned char *moov, *buf;
	size_t moov_len;
	Seg seg[4];
	int nseg, cur;
	long long seg_done, done, total;
};

static void prep_free(VideoPrep *p, int remove_tmp) {
	if (!p) return;
	if (p->in) fclose(p->in);
	if (p->out) fclose(p->out);
	if (remove_tmp && p->tmp[0]) unlink(p->tmp);
	free(p->moov);
	free(p->buf);
	free(p);
}

VideoPrep *video_prep_start(const char *path, char *err, size_t errn) {
	VideoPrep *p = calloc(1, sizeof *p);
	if (!p) { snfmt(err, errn, "Out of memory"); return NULL; }
	sncpy(p->path, path, sizeof p->path);
	snfmt(p->tmp, sizeof p->tmp, "%s.clove-part", path);
	unlink(p->tmp); /* left over from a job that was stopped by power-off */
	p->in = fopen(path, "rb");
	if (!p->in) { snfmt(err, errn, "Could not open the video (%s)", strerror(errno)); prep_free(p, 0); return NULL; }
	Layout L;
	if (!read_layout(p->in, &L) || L.moov.pos < L.mdat.pos) { snfmt(err, errn, "This video doesn't need getting ready"); prep_free(p, 0); return NULL; }
	if (L.moov.size > (long long)MAX_MOOV) { snfmt(err, errn, "This video's index is too big to move"); prep_free(p, 0); return NULL; }
	struct statvfs vf;
	char dir[1024];
	path_dir(path, dir, sizeof dir);
	if (statvfs(dir, &vf) == 0 && (long long)vf.f_bavail * (long long)vf.f_frsize < L.fsize + (32LL << 20)) {
		snfmt(err, errn, "Not enough free space on the card to get this video ready");
		prep_free(p, 0);
		return NULL;
	}
	p->moov_len = (size_t)L.moov.size;
	p->moov = malloc(p->moov_len);
	p->buf = malloc(COPY_BUF);
	if (!p->moov || !p->buf) { snfmt(err, errn, "Out of memory"); prep_free(p, 0); return NULL; }
	if (fseeko(p->in, (off_t)L.moov.pos, SEEK_SET) != 0 || fread(p->moov, 1, p->moov_len, p->in) != p->moov_len) {
		snfmt(err, errn, "Could not read the video");
		prep_free(p, 0);
		return NULL;
	}
	/* new order: ftyp, moov, everything that was between them, then anything after the old moov */
	long long head = (L.ftyp.found && L.ftyp.pos == 0) ? L.ftyp.size : 0;
	size_t hdr = be32(p->moov) == 1 ? 16 : 8;
	int r = patch_offsets(p->moov, hdr, p->moov_len, (uint64_t)head, (uint64_t)L.moov.pos, (uint64_t)L.moov.size, 0);
	if (r != 0) {
		snfmt(err, errn, r == -2 ? "This video is too big to get ready (over 4 GB)" : "This video's index looks damaged");
		prep_free(p, 0);
		return NULL;
	}
	if (head) p->seg[p->nseg++] = (Seg){0, head};
	p->seg[p->nseg++] = (Seg){-1, L.moov.size};
	if (L.moov.pos > head) p->seg[p->nseg++] = (Seg){head, L.moov.pos - head};
	long long after = L.moov.pos + L.moov.size;
	if (after < L.fsize) p->seg[p->nseg++] = (Seg){after, L.fsize - after};
	for (int i = 0; i < p->nseg; i++) p->total += p->seg[i].len;
	p->out = fopen(p->tmp, "wb");
	if (!p->out) { snfmt(err, errn, "Could not write on the card (%s)", strerror(errno)); prep_free(p, 0); return NULL; }
	LOG("video: moving the index of %s to the front (%lld bytes, index %lld)", path, L.fsize, L.moov.size);
	return p;
}

double video_prep_progress(const VideoPrep *p) {
	return p && p->total > 0 ? (double)p->done / (double)p->total : 0.0;
}

void video_prep_cancel(VideoPrep *p) {
	if (p) LOG("video: stopped getting %s ready", p->path);
	prep_free(p, 1);
}

int video_prep_step(VideoPrep *p, int budget_ms, char *err, size_t errn) {
	uint64_t t0 = now_ms();
	while (p->cur < p->nseg) {
		Seg *s = &p->seg[p->cur];
		if (s->from < 0) {
			if (fwrite(p->moov, 1, p->moov_len, p->out) != p->moov_len) goto write_fail;
			p->done += (long long)p->moov_len;
			p->cur++;
			p->seg_done = 0;
			continue;
		}
		if (p->seg_done == 0 && fseeko(p->in, (off_t)s->from, SEEK_SET) != 0) goto read_fail;
		long long left = s->len - p->seg_done;
		size_t n = left > (long long)COPY_BUF ? COPY_BUF : (size_t)left;
		if (fread(p->buf, 1, n, p->in) != n) goto read_fail;
		if (fwrite(p->buf, 1, n, p->out) != n) goto write_fail;
		p->seg_done += (long long)n;
		p->done += (long long)n;
		if (p->seg_done >= s->len) { p->cur++; p->seg_done = 0; }
		if ((int)(now_ms() - t0) >= budget_ms) return 0;
	}
	/* all written: make sure it's on the card, then swap it in */
	if (fflush(p->out) != 0 || fsync(fileno(p->out)) != 0) goto write_fail;
	fclose(p->out);
	p->out = NULL;
	fclose(p->in);
	p->in = NULL;
	{
		FILE *f = fopen(p->tmp, "rb");
		Layout L;
		int ok = f && read_layout(f, &L) && L.moov.pos < L.mdat.pos && L.fsize == p->total;
		if (f) fclose(f);
		if (!ok) { snfmt(err, errn, "The new copy didn't check out, so the video was left as it was"); prep_free(p, 1); return -1; }
	}
	if (rename(p->tmp, p->path) != 0) {
		/* some card drivers won't rename over a file: swap them by hand */
		char old[1100];
		snfmt(old, sizeof old, "%s.clove-old", p->path);
		if (rename(p->path, old) != 0 || rename(p->tmp, p->path) != 0) {
			snfmt(err, errn, "Could not replace the video (%s)", strerror(errno));
			if (!file_exists(p->path)) rename(old, p->path);
			prep_free(p, 1);
			return -1;
		}
		unlink(old); /* the same film in the old order */
	}
	sync();
	LOG("video: %s is ready", p->path);
	p->tmp[0] = 0;
	prep_free(p, 0);
	return 1;
read_fail:
	snfmt(err, errn, "Could not read the video");
	prep_free(p, 1);
	return -1;
write_fail:
	snfmt(err, errn, "Could not write on the card (%s)", strerror(errno));
	prep_free(p, 1);
	return -1;
}
