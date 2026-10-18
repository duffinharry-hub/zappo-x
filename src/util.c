#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

void *xmalloc(size_t n) {
	void *p = malloc(n ? n : 1);
	if (!p) { fprintf(stderr, "clove: out of memory (%u bytes)\n", (unsigned)n); exit(3); }
	return p;
}
void *xcalloc(size_t n, size_t sz) {
	void *p = calloc(n ? n : 1, sz ? sz : 1);
	if (!p) { fprintf(stderr, "clove: out of memory\n"); exit(3); }
	return p;
}
void *xrealloc(void *p, size_t n) {
	void *q = realloc(p, n ? n : 1);
	if (!q) { fprintf(stderr, "clove: out of memory\n"); exit(3); }
	return q;
}
char *xstrdup(const char *s) {
	size_t n = strlen(s ? s : "");
	char *d = xmalloc(n + 1);
	memcpy(d, s ? s : "", n + 1);
	return d;
}

void sncpy(char *dst, const char *src, size_t n) {
	if (!n) return;
	if (!src) src = "";
	size_t l = strlen(src);
	if (l >= n) l = n - 1;
	memcpy(dst, src, l);
	dst[l] = 0;
}
int snfmt(char *dst, size_t n, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintf(dst, n, fmt, ap);
	va_end(ap);
	if (n) dst[n - 1] = 0;
	return r;
}

int file_exists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
int dir_exists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
long file_size(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return -1;
	return (long)st.st_size;
}

char *file_read_all(const char *path, size_t *len_out) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	long n = ftell(f);
	if (n < 0) { fclose(f); return NULL; }
	fseek(f, 0, SEEK_SET);
	char *buf = xmalloc((size_t)n + 1);
	size_t got = fread(buf, 1, (size_t)n, f);
	fclose(f);
	buf[got] = 0;
	if (len_out) *len_out = got;
	return buf;
}

int file_write_all(const char *path, const void *data, size_t len) {
	char tmp[1024];
	snfmt(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) return -1;
	if (len && fwrite(data, 1, len, f) != len) { fclose(f); unlink(tmp); return -1; }
	fflush(f);
	fsync(fileno(f));
	fclose(f);
	if (rename(tmp, path) != 0) {
		/* Some FAT drivers refuse to rename over an existing file. */
		unlink(path);
		if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
	}
	return 0;
}

int mkdir_p(const char *path) {
	char buf[1024];
	sncpy(buf, path, sizeof buf);
	for (char *p = buf + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			mkdir(buf, 0755);
			*p = '/';
		}
	}
	if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
	return 0;
}

const char *path_base(const char *path) {
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}
const char *path_ext(const char *path) {
	const char *b = path_base(path);
	const char *d = strrchr(b, '.');
	return (d && d != b) ? d + 1 : "";
}
void path_stem(const char *path, char *out, size_t n) {
	const char *b = path_base(path);
	sncpy(out, b, n);
	char *d = strrchr(out, '.');
	if (d && d != out) *d = 0;
}
void path_dir(const char *path, char *out, size_t n) {
	sncpy(out, path, n);
	char *s = strrchr(out, '/');
	if (s) *s = 0;
	else sncpy(out, ".", n);
}

int str_ieq(const char *a, const char *b) {
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
		a++; b++;
	}
	return *a == *b;
}

/* Case-insensitive compare where digit runs compare by value ("Game 2" < "Game 10"). */
int str_icmp_natural(const char *a, const char *b) {
	while (*a && *b) {
		if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
			while (*a == '0') a++;
			while (*b == '0') b++;
			const char *sa = a, *sb = b;
			while (isdigit((unsigned char)*a)) a++;
			while (isdigit((unsigned char)*b)) b++;
			long la = a - sa, lb = b - sb;
			if (la != lb) return la < lb ? -1 : 1;
			int c = strncmp(sa, sb, (size_t)la);
			if (c) return c;
			continue;
		}
		int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
		if (ca != cb) return ca < cb ? -1 : 1;
		a++; b++;
	}
	return (*a != 0) - (*b != 0);
}

int str_starts_with(const char *s, const char *prefix) {
	return strncmp(s, prefix, strlen(prefix)) == 0;
}
void str_trim(char *s) {
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
	size_t i = 0;
	while (s[i] && isspace((unsigned char)s[i])) i++;
	if (i) memmove(s, s + i, n - i + 1);
}
void str_lower(char *s) {
	for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}
uint32_t str_hash(const char *s) {
	uint32_t h = 2166136261u;
	for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
	return h;
}

void shell_squote(const char *in, char *out, size_t n) {
	size_t o = 0;
	for (; *in && o + 5 < n; in++) {
		if (*in == '\'') {
			memcpy(out + o, "'\\''", 4);
			o += 4;
		} else {
			out[o++] = *in;
		}
	}
	out[o] = 0;
}

uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void sl_push(StrList *l, const char *s) {
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 16;
		l->v = xrealloc(l->v, sizeof(char *) * (size_t)l->cap);
	}
	l->v[l->n++] = xstrdup(s);
}
void sl_free(StrList *l) {
	for (int i = 0; i < l->n; i++) free(l->v[i]);
	free(l->v);
	l->v = NULL;
	l->n = l->cap = 0;
}
int sl_find(const StrList *l, const char *s) {
	for (int i = 0; i < l->n; i++)
		if (strcmp(l->v[i], s) == 0) return i;
	return -1;
}

static FILE *g_log;
/* what was said before the log file could be opened (the screen, the input
 * devices: found before the card's paths are known) goes in first */
static char early[4096];
static size_t early_len;
void log_open(const char *path) {
	g_log = fopen(path, "w");
	if (g_log && early_len) { fwrite(early, 1, early_len, g_log); fflush(g_log); }
	early_len = 0;
}
void logf_(const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	fprintf(stderr, "clove: %s\n", buf);
	if (g_log) {
		fprintf(g_log, "%s\n", buf);
		fflush(g_log);
	} else {
		size_t l = strlen(buf);
		if (early_len + l + 2 < sizeof early) {
			memcpy(early + early_len, buf, l);
			early_len += l;
			early[early_len++] = '\n';
		}
	}
}
