/* Zappo — the hardware layer for running natively on a tablet or phone
 * (Termux on Android) or any Linux PC, with the screen in the browser.
 *
 * Zappo draws into its 640x480 canvas as on the handheld; this layer serves
 * the Zappo X page on http://127.0.0.1:8640 and streams the picture to it
 * over a WebSocket, takes button presses back, and plays the music in the
 * page: music.c pipes its sound into a FIFO (CLOVE_AUDIO_CMD) that a small
 * thread here passes on at playing speed.  Games don't start - the page
 * shows what the handheld would run, then Zappo starts again.
 *
 * Environment: ZAPPO_CARD (card folder, default ~/zappo-card), ZAPPO_WEB
 * (page folder, default ./web-native), ZAPPO_PORT (default 8640). */
#include "plat.h"
#include "music.h"
#include "ui.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int app_start(void);
int app_frame(int wait);
int app_end(void);

static uint32_t canvas[SCREEN_W * SCREEN_H];
static uint16_t sent[SCREEN_W * SCREEN_H]; /* the picture the page has, as RGB565 */
static int have_sent;
static int brightness = 7;
static int cpu_max = 1512000;
static char cpu_gov[32] = "ondemand";
static char web_dir[512];
static int lsock = -1, csock = -1;            /* listening socket, the page's WebSocket */
static pthread_mutex_t wlock = PTHREAD_MUTEX_INITIALIZER;
static char launched[6000];
static int back_pressed;
static double render_ms[600];
static int render_n;

#define QN 64
static struct { int b, down; } q[QN];
static int qh, qt;

static uint64_t now_ms_(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
static double now_us(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/* ------------------------------------------------------------ SHA-1 */
/* (for the WebSocket handshake only; after Steve Reid's public-domain code) */
typedef struct { uint32_t s[5]; uint64_t n; uint8_t buf[64]; } Sha1;
#define ROL(v, b) (((v) << (b)) | ((v) >> (32 - (b))))
static void sha1_block(uint32_t s[5], const uint8_t *p) {
	uint32_t w[80], a = s[0], b = s[1], c = s[2], d = s[3], e = s[4];
	for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (int i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
		else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
		else { f = b ^ c ^ d; k = 0xCA62C1D6; }
		uint32_t t = ROL(a, 5) + f + e + k + w[i];
		e = d; d = c; c = ROL(b, 30); b = a; a = t;
	}
	s[0] += a; s[1] += b; s[2] += c; s[3] += d; s[4] += e;
}
static void sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
	uint32_t s[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
	size_t i = 0;
	for (; i + 64 <= len; i += 64) sha1_block(s, data + i);
	uint8_t tail[128] = {0};
	size_t r = len - i;
	memcpy(tail, data + i, r);
	tail[r] = 0x80;
	size_t tl = r + 9 <= 64 ? 64 : 128;
	uint64_t bits = (uint64_t)len * 8;
	for (int k = 0; k < 8; k++) tail[tl - 1 - k] = (uint8_t)(bits >> (8 * k));
	sha1_block(s, tail);
	if (tl == 128) sha1_block(s, tail + 64);
	for (int k = 0; k < 5; k++) { out[k * 4] = (uint8_t)(s[k] >> 24); out[k * 4 + 1] = (uint8_t)(s[k] >> 16); out[k * 4 + 2] = (uint8_t)(s[k] >> 8); out[k * 4 + 3] = (uint8_t)s[k]; }
}
static void base64(const uint8_t *in, size_t n, char *out) {
	static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t o = 0;
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
		out[o++] = T[v >> 18 & 63];
		out[o++] = T[v >> 12 & 63];
		out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
		out[o++] = i + 2 < n ? T[v & 63] : '=';
	}
	out[o] = 0;
}

/* ------------------------------------------------------- the server */

static int write_all(int fd, const void *p, size_t n) {
	const char *c = p;
	while (n) {
		ssize_t w = send(fd, c, n, MSG_NOSIGNAL);
		if (w < 0 && errno == EINTR) continue;
		if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { struct pollfd pf = {fd, POLLOUT, 0}; poll(&pf, 1, 200); continue; }
		if (w <= 0) return -1;
		c += w;
		n -= (size_t)w;
	}
	return 0;
}

/* one WebSocket message (binary when bin) to the page */
static void ws_send(int bin, const void *data, size_t n) {
	pthread_mutex_lock(&wlock);
	if (csock >= 0) {
		uint8_t h[10];
		size_t hl;
		h[0] = 0x80 | (bin ? 2 : 1);
		if (n < 126) { h[1] = (uint8_t)n; hl = 2; }
		else if (n < 65536) { h[1] = 126; h[2] = (uint8_t)(n >> 8); h[3] = (uint8_t)n; hl = 4; }
		else { h[1] = 127; for (int i = 0; i < 8; i++) h[2 + i] = (uint8_t)((uint64_t)n >> (56 - 8 * i)); hl = 10; }
		if (write_all(csock, h, hl) != 0 || write_all(csock, data, n) != 0) { close(csock); csock = -1; }
	}
	pthread_mutex_unlock(&wlock);
}
static void ws_text(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ws_text(const char *fmt, ...) {
	char b[8192];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	if (n > 0) ws_send(0, b, (size_t)MIN(n, (int)sizeof b - 1));
}

static const char *mime(const char *p) {
	const char *e = path_ext(p);
	if (str_ieq(e, "html")) return "text/html; charset=utf-8";
	if (str_ieq(e, "js")) return "text/javascript";
	if (str_ieq(e, "json")) return "application/json";
	if (str_ieq(e, "png")) return "image/png";
	if (str_ieq(e, "wasm")) return "application/wasm";
	return "application/octet-stream";
}

static void serve_file(int fd, const char *url) {
	char path[1024], hdr[256];
	if (!strcmp(url, "/")) url = "/index.html";
	if (strstr(url, "..")) url = "/index.html";
	snfmt(path, sizeof path, "%s%s", web_dir, url);
	char *q = strchr(path, '?');
	if (q) *q = 0;
	size_t n = 0;
	char *body = file_read_all(path, &n);
	if (!body) {
		const char *m = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		write_all(fd, m, strlen(m));
	} else {
		int hl = snprintf(hdr, sizeof hdr, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", mime(path), n);
		write_all(fd, hdr, (size_t)hl);
		write_all(fd, body, n);
		free(body);
	}
	close(fd);
}

/* a new connection: a page file, or the page's WebSocket */
static void accept_one(void) {
	int fd = accept(lsock, NULL, NULL);
	if (fd < 0) return;
	char req[4096];
	size_t got = 0;
	struct pollfd pf = {fd, POLLIN, 0};
	while (got < sizeof req - 1 && poll(&pf, 1, 1000) > 0) {
		ssize_t r = recv(fd, req + got, sizeof req - 1 - got, 0);
		if (r <= 0) break;
		got += (size_t)r;
		req[got] = 0;
		if (strstr(req, "\r\n\r\n")) break;
	}
	req[got] = 0;
	char url[1024] = "/";
	sscanf(req, "GET %1023s", url);
	char *key = strcasestr(req, "Sec-WebSocket-Key:");
	if (!key || strcmp(url, "/ws") != 0) { serve_file(fd, url); return; }
	key += 18;
	while (*key == ' ') key++;
	char k[128];
	size_t kl = strcspn(key, "\r\n");
	snfmt(k, sizeof k, "%.*s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", (int)MIN(kl, 64), key);
	uint8_t dg[20];
	sha1((const uint8_t *)k, strlen(k), dg);
	char acc[40], resp[300];
	base64(dg, 20, acc);
	int rl = snprintf(resp, sizeof resp, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
	write_all(fd, resp, (size_t)rl);
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	pthread_mutex_lock(&wlock);
	if (csock >= 0) close(csock); /* the newest page wins */
	csock = fd;
	pthread_mutex_unlock(&wlock);
	have_sent = 0; /* send it the whole picture */
	LOG("tablet: page connected");
}

/* messages from the page: "k BUTTON DOWN", "back" */
static uint8_t rbuf[8192];
static size_t rlen;
static void page_message(char *m) {
	int b, d;
	if (sscanf(m, "k %d %d", &b, &d) == 2 && b >= 0 && b < B_COUNT) {
		int nt = (qt + 1) % QN;
		if (nt != qh) { q[qt].b = b; q[qt].down = d; qt = nt; }
	} else if (!strcmp(m, "back")) {
		back_pressed = 1;
	} else if (!strcmp(m, "full")) {
		have_sent = 0;
	} else if (!strcmp(m, "reset")) {
		render_n = 0; /* the page's speed test starts */
	}
}
static void read_page(void) {
	if (csock < 0) return;
	for (;;) {
		ssize_t r = recv(csock, rbuf + rlen, sizeof rbuf - rlen, 0);
		if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
			pthread_mutex_lock(&wlock); close(csock); csock = -1; pthread_mutex_unlock(&wlock);
			rlen = 0;
			return;
		}
		if (r < 0) break;
		rlen += (size_t)r;
		for (;;) { /* whole frames only; the page masks what it sends */
			if (rlen < 2) break;
			int op = rbuf[0] & 15;
			size_t n = rbuf[1] & 127, hl = 2;
			if (n == 126) { if (rlen < 4) break; n = (size_t)rbuf[2] << 8 | rbuf[3]; hl = 4; }
			else if (n == 127) { rlen = 0; break; } /* never this big */
			int masked = rbuf[1] & 128;
			if (rlen < hl + (masked ? 4 : 0) + n) break;
			uint8_t *mk = rbuf + hl, *pl = rbuf + hl + (masked ? 4 : 0);
			if (masked) for (size_t i = 0; i < n; i++) pl[i] ^= mk[i & 3];
			if (op == 8) { pthread_mutex_lock(&wlock); close(csock); csock = -1; pthread_mutex_unlock(&wlock); rlen = 0; return; }
			if (op == 1 && n < 256) { char m[257]; memcpy(m, pl, n); m[n] = 0; page_message(m); }
			size_t used = hl + (masked ? 4 : 0) + n;
			memmove(rbuf, rbuf + used, rlen - used);
			rlen -= used;
		}
	}
}

static void pump_network(int wait_ms) {
	struct pollfd pf[2] = {{lsock, POLLIN, 0}, {csock, POLLIN, 0}};
	int n = csock >= 0 ? 2 : 1;
	if (poll(pf, (nfds_t)n, wait_ms) <= 0) return;
	if (pf[0].revents & POLLIN) accept_one();
	if (n == 2 && (pf[1].revents & (POLLIN | POLLHUP | POLLERR))) read_page();
}

/* ------------------------------------------------- sound to the page */
/* music.c writes "ZXA rate channels\n" then 16-bit PCM into the FIFO (see
 * CLOVE_AUDIO_CMD below); this passes it on no faster than it plays, plus
 * a little ahead, so the FIFO's back-pressure paces the player as aplay
 * would on the handheld. */
static char fifo_path[512];
static void *audio_thread(void *arg) {
	(void)arg;
	signal(SIGPIPE, SIG_IGN);
	for (;;) {
		int fd = open(fifo_path, O_RDONLY);
		if (fd < 0) { usleep(200000); continue; }
		char head[64];
		size_t hn = 0;
		while (hn < sizeof head - 1 && read(fd, head + hn, 1) == 1 && head[hn] != '\n') hn++;
		head[hn] = 0;
		int rate = 44100, ch = 2;
		sscanf(head, "ZXA %d %d", &rate, &ch);
		if (rate < 8000 || rate > 192000) rate = 44100;
		if (ch < 1 || ch > 2) ch = 2;
		ws_text("a %d %d", rate, ch);
		static uint8_t buf[1 + 8192];
		double t0 = now_us(), sent_s = 0;
		for (;;) {
			ssize_t r = read(fd, buf + 1, 8192);
			if (r <= 0) break;
			buf[0] = 'A';
			ws_send(1, buf, (size_t)r + 1);
			sent_s += (double)r / (2.0 * ch * rate);
			double ahead = sent_s - (now_us() - t0) / 1e6;
			if (ahead > 0.35) usleep((useconds_t)((ahead - 0.3) * 1e6));
		}
		close(fd);
		ws_text("a end");
	}
	return NULL;
}

/* ----------------------------------------------------------- picture */

extern double app_render_ms;
void plat_present(void) {
	double t = app_render_ms;
	if (t > 0 && t < 1000) { render_ms[render_n % 600] = t; render_n++; }
	if (csock < 0) return;
	/* rows that changed since the page's copy, as RGB565: "F" y0 y1 then pixels */
	int y0 = -1, y1 = -1;
	static uint16_t cur[SCREEN_W * SCREEN_H];
	for (int y = 0; y < SCREEN_H; y++) {
		const uint32_t *s = canvas + y * SCREEN_W;
		uint16_t *d = cur + y * SCREEN_W;
		for (int x = 0; x < SCREEN_W; x++) {
			uint32_t p = s[x];
			d[x] = (uint16_t)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
		}
		if (!have_sent || memcmp(d, sent + y * SCREEN_W, SCREEN_W * 2)) { if (y0 < 0) y0 = y; y1 = y; }
	}
	if (y0 < 0) return;
	size_t n = (size_t)(y1 - y0 + 1) * SCREEN_W * 2;
	static uint8_t msg[5 + SCREEN_W * SCREEN_H * 2];
	msg[0] = 'F';
	msg[1] = (uint8_t)(y0 >> 8); msg[2] = (uint8_t)y0;
	msg[3] = (uint8_t)(y1 >> 8); msg[4] = (uint8_t)y1;
	memcpy(msg + 5, cur + y0 * SCREEN_W, n);
	memcpy(sent + y0 * SCREEN_W, cur + y0 * SCREEN_W, n);
	have_sent = 1;
	ws_send(1, msg, n + 5);
}

uint32_t *plat_canvas(void) { return canvas; }
int plat_has_vsync(void) { return 0; }
void plat_set_input_map(const InputMap *m) { (void)m; }

int plat_poll(InputEvent *ev) {
	pump_network(0);
	if (qh == qt) return 0;
	memset(ev, 0, sizeof *ev);
	ev->button = q[qh].b;
	ev->pressed = q[qh].down;
	ev->raw_type = 1;
	ev->raw_code = 300 + q[qh].b;
	ev->raw_value = q[qh].down;
	sncpy(ev->dev, "page", sizeof ev->dev);
	qh = (qh + 1) % QN;
	return 1;
}

void plat_wait(int timeout_ms) { pump_network(timeout_ms > 0 ? MIN(timeout_ms, 100) : 16); }
uint64_t plat_ticks(void) { return now_ms_(); }
int plat_is_host(void) { return 0; }

static int read_int_file(const char *p, int dflt) {
	char *t = file_read_all(p, NULL);
	int v = t ? atoi(t) : dflt;
	free(t);
	return v;
}
int plat_battery(void) { return read_int_file("/sys/class/power_supply/battery/capacity", 76); }
int plat_charging(void) {
	char *t = file_read_all("/sys/class/power_supply/battery/status", NULL);
	int c = t && !strncmp(t, "Charging", 8);
	free(t);
	return c;
}
int plat_brightness_get(void) { return brightness; }
void plat_brightness_set(int level, int persist) { (void)persist; brightness = CLAMP(level, 1, 10); ws_text("b %d", brightness); }
void plat_screen_off(int off) { ws_text("b %d", off ? 0 : brightness); }
void plat_screen_dim(int dim) { ws_text("b %d", dim ? 1 : brightness); }

int plat_cpu_freqs(int *out, int max) {
	static const int f[] = {480000, 720000, 1008000, 1200000, 1416000, 1512000};
	int n = MIN(max, ARRAY_LEN(f));
	memcpy(out, f, sizeof(int) * (size_t)n);
	return n;
}
void plat_cpu_set(const char *governor, int max_khz, int min_khz) {
	(void)min_khz;
	if (governor && *governor) sncpy(cpu_gov, governor, sizeof cpu_gov);
	if (max_khz > 0) cpu_max = max_khz;
}
int plat_cpu_cur(void) { return read_int_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", cpu_max); }

void plat_describe(char *out, size_t n) {
	long cores = sysconf(_SC_NPROCESSORS_ONLN);
	int maxf = read_int_file("/sys/devices/system/cpu/cpu7/cpufreq/cpuinfo_max_freq", read_int_file("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", 0));
	snfmt(out, n, "Zappo X, full speed on this device\nCPU: %ld cores%s%d MHz\nScreen: 640x480 in the browser", cores, maxf ? ", up to " : "", maxf / 1000);
}

void plat_power(int reboot) { ws_text("p %d", reboot); }

int plat_init(void) {
	const char *home = getenv("HOME");
	const char *card = getenv("ZAPPO_CARD");
	char c[512];
	snfmt(c, sizeof c, "%s/zappo-card", home ? home : ".");
	if (!card) card = c;
	snfmt(g_paths.mmc, sizeof g_paths.mmc, "%s/mmc", card);
	snfmt(g_paths.sd, sizeof g_paths.sd, "%s/sdcard", card);
	snfmt(g_paths.cfw, sizeof g_paths.cfw, "%s/CFW", g_paths.mmc);
	snfmt(g_paths.clove, sizeof g_paths.clove, "%s/clove", g_paths.cfw);
	g_paths.sd_present = 0;
	g_paths.mmc_is_tf2 = 0;
	const char *w = getenv("ZAPPO_WEB");
	sncpy(web_dir, w ? w : "web-native", sizeof web_dir);
	return 0;
}

void plat_quit(void) {}

/* what the handheld would have run (data.c's command.sh) */
void web_launch(const char *body) { sncpy(launched, body, sizeof launched); }

static void start_server(void) {
	/* started again after a game: the server and the page's connection carry over */
	const char *ls = getenv("ZAPPO_LSOCK"), *cs = getenv("ZAPPO_CSOCK");
	if (ls) {
		lsock = atoi(ls);
		csock = cs ? atoi(cs) : -1;
		unsetenv("ZAPPO_LSOCK");
		unsetenv("ZAPPO_CSOCK");
		return;
	}
	const char *pe = getenv("ZAPPO_PORT");
	int port = pe ? atoi(pe) : 8640;
	lsock = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET;
	a.sin_port = htons((uint16_t)port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* this device only */
	if (bind(lsock, (struct sockaddr *)&a, sizeof a) != 0 || listen(lsock, 8) != 0) {
		fprintf(stderr, "zappo: can't use port %d (%s) - is Zappo already running?\n", port, strerror(errno));
		exit(1);
	}
	printf("Zappo X is running: open http://127.0.0.1:%d in Chrome\n", port);
	fflush(stdout);
}

static void report(void) {
	/* the section (for the stick lights), music, and how long pictures take */
	static uint64_t last;
	uint64_t t = now_ms_();
	if (t - last < 250) return;
	last = t;
	int n = MIN(render_n, 600);
	double s[600];
	memcpy(s, render_ms, sizeof(double) * (size_t)n);
	for (int i = 1; i < n; i++) { double v = s[i]; int j = i - 1; while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; } s[j + 1] = v; }
	ws_text("t %d %d %.2f %.2f %d", ui_tab(), music_is_playing(), n ? s[n / 2] : 0.0, n ? s[n * 95 / 100] : 0.0, n);
}

/* --unpack CARD_DIR DEST: lays out the test card (tools/webcard.py's
 * card/index.json) under DEST, keeping files that are already there */
#include "../third_party/cJSON.h"
static int b64v(int c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	return c == '+' ? 62 : c == '/' ? 63 : -1;
}
static int unpack_card(const char *src, const char *dest) {
	char p[1024];
	snfmt(p, sizeof p, "%s/index.json", src);
	char *txt = file_read_all(p, NULL);
	cJSON *idx = txt ? cJSON_Parse(txt) : NULL;
	free(txt);
	if (!cJSON_IsArray(idx)) { fprintf(stderr, "zappo: no card index at %s\n", p); return 1; }
	int made = 0, kept = 0;
	for (cJSON *it = idx->child; it; it = it->next) {
		const char *path = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "p"));
		if (!path) continue;
		char out[1200];
		snfmt(out, sizeof out, "%s%s", dest, path);
		if (file_exists(out)) { kept++; continue; }
		char dir[1200];
		sncpy(dir, out, sizeof dir);
		char *sl = strrchr(dir, '/');
		if (sl) { *sl = 0; mkdir_p(dir); }
		const char *f = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "f"));
		const char *b = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "b"));
		size_t n = 0;
		char *data = NULL;
		if (f) { snfmt(p, sizeof p, "%s/%s", src, f); data = file_read_all(p, &n); }
		else if (b) {
			data = xmalloc(strlen(b) + 1);
			uint32_t acc = 0; int bits = 0;
			for (const char *c = b; *c; c++) {
				int v = b64v((unsigned char)*c);
				if (v < 0) continue;
				acc = acc << 6 | (uint32_t)v; bits += 6;
				if (bits >= 8) { bits -= 8; data[n++] = (char)(acc >> bits); }
			}
		}
		if (!data) { fprintf(stderr, "zappo: missing %s\n", f ? f : path); continue; }
		file_write_all(out, data, n);
		free(data);
		made++;
	}
	cJSON_Delete(idx);
	printf("card ready in %s (%d new files, %d kept)\n", dest, made, kept);
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 4 && !strcmp(argv[1], "--unpack")) return unpack_card(argv[2], argv[3]);
	signal(SIGPIPE, SIG_IGN);
	plat_init();
	start_server();
	/* music into the FIFO, out to the page */
	const char *tmp = getenv("TMPDIR");
	snfmt(fifo_path, sizeof fifo_path, "%s/zappo-audio.fifo", tmp ? tmp : "/tmp");
	unlink(fifo_path);
	mkfifo(fifo_path, 0600);
	char full[800];
	snfmt(full, sizeof full, "{ printf 'ZXA %%s %%s\\n' \"$RATE\" \"$CHANNELS\"; exec cat; } > '%s'", fifo_path);
	setenv("CLOVE_AUDIO_CMD", full, 1);
	pthread_t at;
	pthread_create(&at, NULL, audio_thread, NULL);

	launched[0] = 0;
	int r = app_start();
	if (r) return r;
	while (app_frame(1)) report();
	int st = ui_state();
	app_end();
	if (st != UI_LAUNCH) { ws_text("q"); return 0; }
	/* show what would start, wait for "Back to Zappo", then start afresh -
	 * as the handheld does after a game - keeping the page connected */
	ws_text("l %s", launched);
	while (!back_pressed && csock >= 0) pump_network(100);
	while (csock < 0) pump_network(500); /* the page went away: wait for it */
	char e[32];
	snfmt(e, sizeof e, "%d", lsock); setenv("ZAPPO_LSOCK", e, 1);
	snfmt(e, sizeof e, "%d", csock); setenv("ZAPPO_CSOCK", e, 1);
	execv("/proc/self/exe", argv);
	perror("zappo: restart");
	return 1;
}
