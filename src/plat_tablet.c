/* Zappo — the hardware layer for running natively on a tablet or phone
 * (Termux on Android) or any Linux PC, with the screen in the browser.
 *
 * Zappo draws into its 640x480 canvas as on the handheld; this layer serves
 * the Zappo X page on http://127.0.0.1:8640 and streams the picture to it
 * over a WebSocket, takes button presses back, and plays the music in the
 * page: music.c pipes its sound into a FIFO (CLOVE_AUDIO_CMD) that a small
 * thread here passes on at playing speed.  On an Android tablet a chosen game
 * starts in RetroArch for Android and a film in the tablet's video player;
 * elsewhere (a Linux PC) the page shows what the handheld would run.  Either
 * way Zappo starts again when the page comes back.
 *
 * Environment: ZAPPO_CARD (card folder; default "Zappo X" in the tablet's
 * own storage when Termux can reach it, else ~/zappo-card), ZAPPO_WEB (page
 * folder, default ./web-native), ZAPPO_PORT (default 8640), ZAPPO_RETROARCH
 * (RetroArch's Android package, when it isn't found by itself). */
#include "plat.h"
#include "music.h"
#include "scene.h"
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
#include <limits.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int app_start(void);
int app_frame(int wait);
int app_end(void);

static uint32_t canvas[SCREEN_W * SCREEN_H];
static uint16_t sent[SCREEN_W * SCREEN_H]; /* the picture the page has, as RGB565 */
static int have_sent;
static int gpu_page;           /* the page draws layers itself (WebGL) */
static int inflight;           /* pictures sent that the page hasn't drawn yet */
static void reset_page_state(void);
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
	if (str_ieq(e, "webmanifest")) return "application/manifest+json";
	if (str_ieq(e, "png")) return "image/png";
	if (str_ieq(e, "wasm")) return "application/wasm";
	return "application/octet-stream";
}

static void serve_file(int fd, const char *url_in) {
	char path[1024], hdr[256], url[1024];
	sncpy(url, url_in, sizeof url);
	char *q = strpbrk(url, "?#");
	if (q) *q = 0;
	if (!strcmp(url, "/") || strstr(url, "..")) sncpy(url, "/index.html", sizeof url);
	snfmt(path, sizeof path, "%s%s", web_dir, url);
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
	gpu_page = 0;	/* (until it says) */
	reset_page_state(); /* send it the whole picture */
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
		reset_page_state();
	} else if (sscanf(m, "gl %d", &b) == 1) {
		gpu_page = b != 0;
		LOG("tablet: the page %s", gpu_page ? "draws with the graphics chip (WebGL)" : "has no WebGL: pictures put together here");
		reset_page_state();
	} else if (sscanf(m, "d %d", &b) == 1) {
		inflight = inflight > b ? inflight - b : 0;
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
/* What the page gets, as binary messages (little-endian; the pixels start
 * where the page can hand them to its graphics chip as they are):
 *   'G' 0 y0 y1          + rows of RGB565: the whole picture
 *   'B' 0 y0 y1          + rows of RGB565: the moving background's picture
 *   'M' 0 0 0 y0 y1      + rows of 0xAARRGGBB (premultiplied): the menu over it
 *   'P' 0 n              + n x 8 floats: the particles (x y w h angle alpha
 *                          tint sprite+256*glow)
 *   'S' 0 aw ah n        + n x 4 floats (where each sprite sits) + the atlas,
 *                          0xAARRGGBB
 * and the text "L 1" / "L 0": layers on or off. A page with WebGL says
 * "gl 1": the background, particles and menu are then put together by the
 * tablet's graphics chip (the handheld's way); without it, here on the CPU.
 * The page answers "d N" when it has drawn N pictures, and no more than
 * three go out ahead of that: a slow moment drops pictures instead of
 * piling them up. */
#include "config.h"
static int page_layers = -1;   /* what the page is showing: 0 one picture, 1 layers */
static int pending;
static int atlas_sent, bg_sent, have_sent_m;
static uint32_t sent_m[SCREEN_W * SCREEN_H];
static int scene_on;
static uint64_t scene_last, scene_next;
static uint32_t scene_bg[SCREEN_W * SCREEN_H], mixed[SCREEN_W * SCREEN_H];
#define AHEAD 3

static void reset_page_state(void) {
	have_sent = have_sent_m = atlas_sent = bg_sent = 0;
	page_layers = -1;
	inflight = 0;
	pending = 1;
}

static int scene_ms(void) {
	if (gpu_page) return g_set.full_power ? 16 : 33;	/* 60 or 30 a second */
	return g_set.full_power ? 33 : 50;
}

static void send_pic(const uint8_t *msg, size_t n) {
	ws_send(1, msg, n);
	inflight++;
}

/* rows of px that changed since the page's copy, as RGB565 */
static void send_565(char kind, const uint32_t *px, uint16_t *copy, int *have) {
	static uint16_t cur[SCREEN_W * SCREEN_H];
	static uint8_t msg[6 + SCREEN_W * SCREEN_H * 2];
	int y0 = -1, y1 = -1;
	for (int y = 0; y < SCREEN_H; y++) {
		const uint32_t *sp = px + y * SCREEN_W;
		uint16_t *d = cur + y * SCREEN_W;
		for (int x = 0; x < SCREEN_W; x++) {
			uint32_t p = sp[x];
			d[x] = (uint16_t)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
		}
		if (!*have || memcmp(d, copy + y * SCREEN_W, SCREEN_W * 2)) { if (y0 < 0) y0 = y; y1 = y; }
	}
	if (y0 < 0) return;
	size_t n = (size_t)(y1 - y0 + 1) * SCREEN_W * 2;
	msg[0] = (uint8_t)kind; msg[1] = 0;
	msg[2] = (uint8_t)y0; msg[3] = (uint8_t)(y0 >> 8);
	msg[4] = (uint8_t)y1; msg[5] = (uint8_t)(y1 >> 8);
	memcpy(msg + 6, cur + y0 * SCREEN_W, n);
	memcpy(copy + y0 * SCREEN_W, cur + y0 * SCREEN_W, n);
	*have = 1;
	send_pic(msg, n + 6);
}

/* the menu (see-through): the rows that changed, as they are */
static void send_menu(void) {
	static uint8_t msg[8 + SCREEN_W * SCREEN_H * 4];
	int y0 = -1, y1 = -1;
	for (int y = 0; y < SCREEN_H; y++)
		if (!have_sent_m || memcmp(canvas + y * SCREEN_W, sent_m + y * SCREEN_W, SCREEN_W * 4)) { if (y0 < 0) y0 = y; y1 = y; }
	if (y0 < 0) return;
	size_t n = (size_t)(y1 - y0 + 1) * SCREEN_W * 4;
	memset(msg, 0, 8);
	msg[0] = 'M';
	msg[4] = (uint8_t)y0; msg[5] = (uint8_t)(y0 >> 8);
	msg[6] = (uint8_t)y1; msg[7] = (uint8_t)(y1 >> 8);
	memcpy(msg + 8, canvas + y0 * SCREEN_W, n);
	memcpy(sent_m + y0 * SCREEN_W, canvas + y0 * SCREEN_W, n);
	have_sent_m = 1;
	send_pic(msg, n + 8);
}

/* the particle pictures, side by side in one picture */
static void send_atlas(void) {
	int aw = 0, ah = 0;
	for (int i = 0; i < SP_COUNT; i++) {
		int w, h;
		scene_sprite(i, &w, &h);
		aw += w + 2;
		if (h + 2 > ah) ah = h + 2;
	}
	size_t head = 8 + SP_COUNT * 16, n = head + (size_t)aw * (size_t)ah * 4;
	uint8_t *msg = calloc(1, n);
	if (!msg) return;
	msg[0] = 'S';
	msg[2] = (uint8_t)aw; msg[3] = (uint8_t)(aw >> 8);
	msg[4] = (uint8_t)ah; msg[5] = (uint8_t)(ah >> 8);
	msg[6] = SP_COUNT;
	float *uv = (float *)(msg + 8);
	uint32_t *apx = (uint32_t *)(msg + head);
	for (int i = 0, x = 1; i < SP_COUNT; i++) {
		int w, h;
		const uint32_t *px = scene_sprite(i, &w, &h);
		for (int y = 0; y < h; y++) memcpy(apx + (y + 1) * aw + x, px + y * w, (size_t)w * 4);
		uv[i * 4] = (float)x / aw; uv[i * 4 + 1] = 1.0f / ah; uv[i * 4 + 2] = (float)w / aw; uv[i * 4 + 3] = (float)h / ah;
		x += w + 2;
	}
	send_pic(msg, n);
	free(msg);
	atlas_sent = 1;
}

static float scene_dt(void) {
	uint64_t now = now_ms_();
	float dt = scene_last ? (now - scene_last) / 1000.0f : 0.0f;
	if (dt > 0.25f) dt = 0.25f;	/* (after a pause, carry on rather than jump) */
	scene_last = now;
	return dt;
}

static void send_particles(void) {
	static SceneQuad qd[256];
	static uint8_t msg[4 + 256 * 32];
	int n = scene_update(scene_dt(), qd, 256);
	msg[0] = 'P'; msg[1] = 0;
	msg[2] = (uint8_t)n; msg[3] = (uint8_t)(n >> 8);
	float *f = (float *)(msg + 4);
	for (int i = 0; i < n; i++, f += 8) {
		f[0] = qd[i].x; f[1] = qd[i].y; f[2] = qd[i].w; f[3] = qd[i].h;
		f[4] = qd[i].angle; f[5] = qd[i].alpha; f[6] = (float)(qd[i].tint & 0xFFFFFF);
		f[7] = (float)(qd[i].sprite + (qd[i].additive ? 256 : 0));
	}
	send_pic(msg, 4 + (size_t)n * 32);
}

/* brings the page up to date: the menu, and the moving background if it's on */
static void sync_page(int particles) {
	if (csock < 0) return;
	if (inflight >= AHEAD) { pending = 1; return; }
	pending = 0;
	int layers = scene_on && gpu_page;
	if (layers != page_layers) {
		ws_text("L %d", layers);
		page_layers = layers;
		have_sent = have_sent_m = bg_sent = 0;
	}
	if (!layers) {
		if (scene_on) {
			scene_compose_soft(mixed, scene_bg, canvas, SCREEN_W, SCREEN_H, scene_dt());
			send_565('G', mixed, sent, &have_sent);
		} else send_565('G', canvas, sent, &have_sent);
	} else {
		static uint16_t sent_bg[SCREEN_W * SCREEN_H];
		if (!atlas_sent) send_atlas();
		if (!bg_sent) { int h = 0; send_565('B', scene_bg, sent_bg, &h); bg_sent = 1; }
		send_menu();
		if (particles || !scene_last) send_particles();
	}
	if (scene_on) scene_next = now_ms_() + (uint64_t)scene_ms();
}

int plat_layers_ok(void) { return 1; }

void plat_scene(const uint32_t *bg, int bg_changed, int element, int density) {
	int on = bg != NULL;
	if (on && bg_changed) { memcpy(scene_bg, bg, sizeof scene_bg); bg_sent = 0; }
	if (on != scene_on) scene_last = 0;
	scene_on = on;
	scene_set(on ? element : EL_NONE, density);
}

/* called when there's nothing new to draw: the particles move on, and a
 * picture held back while the page caught up goes now */
int plat_scene_frame(void) {
	if (csock < 0) return scene_on ? 500 : 0;	/* nobody watching */
	if (pending && inflight < AHEAD) { sync_page(scene_on); return scene_on ? scene_ms() : 0; }
	if (!scene_on) return pending ? 16 : 0;
	uint64_t now = now_ms_();
	if (now < scene_next) return (int)(scene_next - now);
	sync_page(1);
	return scene_ms();
}

void plat_transition(void) {}

extern double app_render_ms;
void plat_present(void) {
	double t = app_render_ms;
	if (t > 0 && t < 1000) { render_ms[render_n % 600] = t; render_n++; }
	/* (the particles go with the menu when they're due anyway) */
	sync_page(scene_on && now_ms_() + 4 >= scene_next);
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

/* Where the card is.  On Android it goes in the tablet's own storage
 * ("Zappo X" beside Download, Pictures ...): RetroArch can open games there,
 * and a PC plugged in by USB sees it.  `intended`: where it should be (for
 * the installer), even before it's made. */
static void card_dir(char *out, size_t n, int intended) {
	const char *home = getenv("HOME");
	const char *card = getenv("ZAPPO_CARD");
	if (card && *card) { sncpy(out, card, n); return; }
	char shared[512], c[512];
	snfmt(shared, sizeof shared, "%s/storage/shared", home ? home : ".");
	snfmt(c, sizeof c, "%s/Zappo X", shared);
	if (dir_exists(shared) && (intended || dir_exists(c))) sncpy(out, c, n);
	else snfmt(out, n, "%s/zappo-card", home ? home : ".");
}

int plat_init(void) {
	char c[512], real[PATH_MAX];
	card_dir(c, sizeof c, 0);
	/* (the real path: ~/storage/shared is a link, and RetroArch is given paths) */
	const char *card = realpath(c, real) ? real : c;
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

/* ------------------------------------------ real games (Android) */

static int on_android(void) { return access("/system/bin/app_process", F_OK) == 0; }

/* runs a program (no shell) and keeps what it says; its exit code */
static int run_capture(char *const argv[], char *out, size_t n) {
	int fd[2];
	out[0] = 0;
	if (pipe(fd) != 0) return -1;
	pid_t pid = fork();
	if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
	if (pid == 0) {
		dup2(fd[1], 1);
		dup2(fd[1], 2);
		int nul = open("/dev/null", O_RDONLY);
		if (nul >= 0) dup2(nul, 0);
		for (int i = 3; i < 1024; i++) close(i);	/* (not the page's connection) */
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fd[1]);
	size_t got = 0;
	ssize_t r;
	char sink[512];
	while ((r = read(fd[0], got < n - 1 ? out + got : sink, got < n - 1 ? n - 1 - got : sizeof sink)) > 0)
		if (got < n - 1) got += (size_t)r;
	out[got] = 0;
	close(fd[0]);
	int st = 0;
	if (waitpid(pid, &st, 0) != pid) return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* RetroArch's package: retroarch.com's 64-bit one first (it has the core
 * downloader), then the others */
static int retroarch_pkg(char *out, size_t n) {
	const char *e = getenv("ZAPPO_RETROARCH");
	if (e && *e) { sncpy(out, e, n); return 1; }
	static const char *P[] = {"com.retroarch.aarch64", "com.retroarch", "com.retroarch.ra32"};
	char list[16384];
	char *argv[] = {"pm", "list", "packages", "retroarch", NULL};
	run_capture(argv, list, sizeof list);
	for (int i = 0; i < ARRAY_LEN(P); i++) {
		char want[80];
		snfmt(want, sizeof want, "package:%s", P[i]);
		for (const char *h = strstr(list, want); h; h = strstr(h + 1, want)) {
			char after = h[strlen(want)];
			if (after == 0 || after == '\n' || after == '\r') { sncpy(out, P[i], n); return 1; }
		}
	}
	return 0;
}

/* the game (or film) the launch is for, from the command it wrote:
 *   .../retroarch -L "<core>" '<rom>'   or   /bin/sh '<script>' */
static int parse_launch(const char *body, char *core, size_t cn, char *rom, size_t rn) {
	core[0] = rom[0] = 0;
	const char *q = NULL, *L = strstr(body, " -L \"");
	if (L) {
		const char *c = L + 5, *ce = strchr(c, '"');
		if (!ce) return 0;
		size_t k = MIN((size_t)(ce - c), cn - 1);
		memcpy(core, c, k);
		core[k] = 0;
		q = strstr(ce, " '");
	} else if ((q = strstr(body, "/bin/sh '")) != NULL) {
		sncpy(core, "/bin/sh", cn);
		q += 7;
	}
	if (!q) return 0;
	q += 2;
	size_t o = 0;
	while (*q && o < rn - 1) {
		if (!strncmp(q, "'\\''", 4)) { rom[o++] = '\''; q += 4; continue; }
		if (*q == '\'') break;
		rom[o++] = *q++;
	}
	rom[o] = 0;
	return rom[0] != 0;
}

/* Starts the chosen game in RetroArch for Android (or a film in the video
 * player).  1: it's starting; 0: not (msg says why, for the page). */
static int android_play(const char *body, char *name, size_t nn, char *core_name, size_t cnn, char *msg, size_t mn) {
	char core[512], rom[2048], out[4096];
	name[0] = core_name[0] = msg[0] = 0;
	if (!parse_launch(body, core, sizeof core, rom, sizeof rom)) { sncpy(msg, "Zappo couldn't tell which game that is.", mn); return 0; }
	sncpy(name, path_base(rom), nn);
	if (!strcmp(core, "/bin/sh")) { sncpy(msg, "Ports are made for the handheld: they don't run on the tablet.", mn); return 0; }
	char so[160];
	sncpy(so, path_base(core), sizeof so);
	char *ext = strstr(so, "_libretro");
	if (ext) *ext = 0;
	sncpy(core_name, so, cnn);
	if (!strcmp(so, "ffmpeg")) {	/* a film: the tablet's own player */
		char *argv[] = {"termux-open", "--content-type", "video/*", rom, NULL};
		int rc = run_capture(argv, out, sizeof out);
		LOG("android: film %s -> %d %s", rom, rc, out);
		if (rc != 0) { snfmt(msg, mn, "The tablet's video player didn't open (%s).", out[0] ? out : "termux-open"); return 0; }
		return 1;
	}
	/* the package Android lists; if it won't say, each likely one in turn */
	static const char *TRY[] = {"com.retroarch.aarch64", "com.retroarch", "com.retroarch.ra32"};
	char found[64];
	int listed = retroarch_pkg(found, sizeof found);
	for (int i = 0; i < (listed ? 1 : ARRAY_LEN(TRY)); i++) {
		const char *pkg = listed ? found : TRY[i];
		char cmp[160], lib[300], cfg[300];
		snfmt(cmp, sizeof cmp, "%s/com.retroarch.browser.retroactivity.RetroActivityFuture", pkg);
		snfmt(lib, sizeof lib, "/data/data/%s/cores/%s_libretro_android.so", pkg, so);
		snfmt(cfg, sizeof cfg, "/storage/emulated/0/Android/data/%s/files/retroarch.cfg", pkg);
		char *argv[] = {"am", "start", "-n", cmp, "-a", "android.intent.action.MAIN", "--activity-clear-task",
		                "--es", "ROM", rom, "--es", "LIBRETRO", lib, "--es", "CONFIGFILE", cfg, NULL};
		int rc = run_capture(argv, out, sizeof out);
		LOG("android: %s with %s (%s) -> %d %s", rom, lib, pkg, rc, out);
		if (strstr(out, "does not exist")) continue;	/* (not this one) */
		if (strstr(out, "Error") || (!strstr(out, "Starting") && rc != 0)) {
			snfmt(msg, mn, "RetroArch didn't start: %.200s", out[0] ? out : "no answer from Android");
			return 0;
		}
		return 1;
	}
	sncpy(msg, "To play games here, install RetroArch for Android (retroarch.com, the 64-bit version), open it once and allow it to see your files.", mn);
	return 0;
}

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

/* --unpack CARD_DIR DEST [--no-stand-ins]: lays out the test card
 * (tools/webcard.py's card/index.json) under DEST, keeping files that are
 * already there. --no-stand-ins: without the pretend games and emulators
 * (a card RetroArch plays real games from) */
#include "../third_party/cJSON.h"
static int b64v(int c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	return c == '+' ? 62 : c == '/' ? 63 : -1;
}
static int unpack_card(const char *src, const char *dest, int stand_ins) {
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
		if (!stand_ins && ((str_starts_with(path, "/mmc/Roms/") && !str_starts_with(path, "/mmc/Roms/VIDEOS/")) ||
		                   str_starts_with(path, "/mmc/CFW/retroarch/")))
			continue;
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
	if ((argc == 4 || argc == 5) && !strcmp(argv[1], "--unpack"))
		return unpack_card(argv[2], argv[3], !(argc == 5 && !strcmp(argv[4], "--no-stand-ins")));
	if (argc == 2 && !strcmp(argv[1], "--card-path")) { char c[512]; card_dir(c, sizeof c, 1); puts(c); return 0; }
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
	/* on Android: the game in RetroArch (the page waits, and says "back" when
	 * it's seen again); elsewhere, or if it can't: the page shows what would
	 * start. Then Zappo starts afresh - as the handheld does after a game -
	 * keeping the page connected */
	char gname[256], gcore[96], why[400];
	if (on_android() && android_play(launched, gname, sizeof gname, gcore, sizeof gcore, why, sizeof why))
		ws_text("g playing\t%s\t%s", gname, gcore);
	else if (on_android())
		ws_text("g failed\t%s\t%s\t%s", gname, gcore, why);
	else
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
