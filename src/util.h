/* Clove — small helpers shared by every module. */
#ifndef CLOVE_UTIL_H
#define CLOVE_UTIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef ZAPPO_KNULLI
#define CLOVE_VERSION "X 0.1"
#define FW_NAME "Knulli"
/* the tab that opens the firmware's own menu */
#define FW_MENU_TAB "Knulli"
#define FW_MENU_TEXT "Open Knulli's own menu for system settings, updates, scraping and everything Zappo X doesn't do yet."
#else
#define CLOVE_VERSION "0.3.5"
#define FW_NAME "GarlicOS"
#define FW_MENU_TAB "RetroArch"
#define FW_MENU_TEXT "Open RetroArch's own menu to change cores, shaders, hotkeys and more."
#endif

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Bounded string copy / format that always terminate. */
void sncpy(char *dst, const char *src, size_t n);
int snfmt(char *dst, size_t n, const char *fmt, ...);

int file_exists(const char *path);
int dir_exists(const char *path);
long file_size(const char *path);
char *file_read_all(const char *path, size_t *len_out);
/* Writes via a temporary file + rename so a power cut never leaves half a file. */
int file_write_all(const char *path, const void *data, size_t len);
int mkdir_p(const char *path);

const char *path_ext(const char *path);           /* "zip" (no dot) or "" */
const char *path_base(const char *path);          /* pointer into path */
void path_stem(const char *path, char *out, size_t n); /* basename without extension */
void path_dir(const char *path, char *out, size_t n);

int str_ieq(const char *a, const char *b);
int str_icmp_natural(const char *a, const char *b);
int str_starts_with(const char *s, const char *prefix);
void str_trim(char *s);
void str_lower(char *s);
uint32_t str_hash(const char *s);

/* Shell-quote a value for use inside single quotes: ' -> '\'' */
void shell_squote(const char *in, char *out, size_t n);

uint64_t now_ms(void);

/* Growable list of owned strings. */
typedef struct {
	char **v;
	int n, cap;
} StrList;
void sl_push(StrList *l, const char *s);
void sl_free(StrList *l);
int sl_find(const StrList *l, const char *s);

/* Logging to CFW/clove/clove.log (and stderr). */
void log_open(const char *path);
void logf_(const char *fmt, ...);
#define LOG(...) logf_(__VA_ARGS__)

#endif
