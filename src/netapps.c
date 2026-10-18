/* Zappo X — Send to Zappo and Get box art (see net.h): two small Python
 * helpers (send-to-zappo.py, get-box-art.py, in Zappo's own folder) that Zappo
 * starts and follows. The same on the Knulli handheld and on a tablet
 * (Termux), where the card is laid out as GarlicOS's and the screen itself is
 * already a web page on port 8640, so Send to Zappo uses the next one. */
#include "net.h"
#include "plat.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ZAPPO_APPS

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "import.h"

/* a file for passing things between Zappo and the helpers (Termux has no /tmp) */
static void tmp_file(char *out, size_t n, const char *name) {
	const char *t = getenv("TMPDIR");
	snfmt(out, n, "%s/%s", t && *t ? t : "/tmp", name);
}

/* python3 running one of the helpers, quiet and in its own session (so it
 * carries on while a game is played); -1 when it couldn't start */
static pid_t start_python(char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
		for (int i = 3; i < 1024; i++) close(i);
		setsid();
		execv("/usr/bin/python3", argv);
		execvp("python3", argv);	/* (Termux keeps it in its own folder) */
		_exit(127);
	}
	return pid;
}

static void trim(char *s) {
	size_t n = strlen(s);
	while (n && (unsigned char)s[n - 1] <= ' ') s[--n] = 0;
}

/* what this device is called, for the page ("Zappo X on ...") */
static void device_name(char *out, size_t n) {
	out[0] = 0;
#ifdef ZAPPO_TABLET
	/* Android: the name it's sold under when the maker says, else the model */
	FILE *p = popen("getprop ro.product.marketname 2>/dev/null; getprop ro.product.model 2>/dev/null", "r");
	if (p) {
		while (!out[0] && fgets(out, (int)n, p)) trim(out);
		pclose(p);
	}
#endif
	if (!out[0]) {
		FILE *f = fopen("/proc/device-tree/model", "r");
		if (f) { if (!fgets(out, (int)n, f)) out[0] = 0; fclose(f); trim(out); }
	}
#ifdef ZAPPO_TABLET
	if (!out[0] && gethostname(out, n) != 0) out[0] = 0;
	if (!out[0] || !strcmp(out, "localhost")) sncpy(out, "this tablet", n);
#else
	if (!out[0]) sncpy(out, "the handheld", n);
#endif
}

/* ------------------------------------------------------- Send to Zappo */

static pid_t send_pid = -1;
static long send_seen = -1;
static char received[300];

static int send_port(void) {
#ifdef ZAPPO_TABLET
	const char *pe = getenv("ZAPPO_PORT");
	return (pe && atoi(pe) > 0 ? atoi(pe) : 8640) + 1;	/* (8640 is Zappo's own screen) */
#else
	return 8640;
#endif
}

int net_send_start(void) {
	if (send_pid > 0) return 0;
	char script[300], model[64], name[96], imp[512], port[16];
	snfmt(script, sizeof script, "%s/send-to-zappo.py", g_paths.clove);
	if (!file_exists(script)) { LOG("send: no %s", script); return -1; }
	device_name(model, sizeof model);
	snfmt(name, sizeof name, "Zappo X on %s", model);
	import_dir(imp, sizeof imp);
	mkdir_p(imp);
	tmp_file(received, sizeof received, "zappo-x-received");
	unlink(received);
	send_seen = 0;
	setenv("ZAPPO_NAME", name, 1);	/* (passed on to the helper, then let go) */
	setenv("ZAPPO_IMPORT", imp, 1);
	setenv("ZAPPO_RECEIVED", received, 1);
	snfmt(port, sizeof port, "%d", send_port());
	char *argv[] = {"python3", script, port, NULL};
	pid_t pid = start_python(argv);
	unsetenv("ZAPPO_NAME");
	unsetenv("ZAPPO_IMPORT");
	unsetenv("ZAPPO_RECEIVED");
	if (pid < 0) return -1;
	send_pid = pid;
	LOG("send: page started on port %s (%s)", port, imp);
	return 0;
}

void net_send_stop(void) {
	if (send_pid <= 0) return;
	kill(send_pid, SIGTERM);
	for (int i = 0; i < 50; i++) {
		if (waitpid(send_pid, NULL, WNOHANG) == send_pid) { send_pid = -1; return; }
		usleep(20000);
	}
	kill(send_pid, SIGKILL);
	waitpid(send_pid, NULL, 0);
	send_pid = -1;
}

int net_send_running(void) {
	if (send_pid > 0 && waitpid(send_pid, NULL, WNOHANG) == send_pid) {
		send_pid = -1;
		LOG("send: the page stopped by itself (is python3 there?)");
	}
	return send_pid > 0;
}

/* something new arrived: redraw */
static int send_check(void) {
	if (send_pid <= 0) return 0;
	struct stat sb;
	long sz = stat(received, &sb) == 0 ? (long)sb.st_size : 0;
	if (sz == send_seen) return 0;
	send_seen = sz;
	return 1;
}

int net_send_received(char names[][96], int max) {
	if (!received[0]) return 0;
	char *txt = file_read_all(received, NULL);
	if (!txt) return 0;
	char *lines[256];
	int n = 0;
	for (char *line = strtok(txt, "\n"); line && n < 256; line = strtok(NULL, "\n")) lines[n++] = line;
	int k = 0;
	for (int i = n - 1; i >= 0 && k < max; i--) {
		char *t = strrchr(lines[i], '\t');
		sncpy(names[k++], t ? t + 1 : lines[i], 96);
	}
	free(txt);
	return k;
}

#ifdef ZAPPO_TABLET
/* the tablet's address on the Wi-Fi: its wireless (or wired) connection,
 * not mobile data or a VPN */
static int is_private(uint32_t a) {
	return (a >> 24) == 10 || (a >> 20) == (172 << 4 | 1) || (a >> 16) == (192 << 8 | 168);
}
static int local_ip(char *out, size_t n) {
	struct ifaddrs *ifs = NULL;
	int best = -1;
	out[0] = 0;
	if (getifaddrs(&ifs) == 0) {
		for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
			const char *nm = i->ifa_name ? i->ifa_name : "";
			static const char *NOT[] = {"rmnet", "ccmni", "pdp", "tun", "ppp", "dummy", "v4-", "clat", "ipsec", "docker", "veth", "br-"};
			int skip = 0;
			for (int k = 0; k < ARRAY_LEN(NOT); k++) if (!strncmp(nm, NOT[k], strlen(NOT[k]))) skip = 1;
			if (skip) continue;
			uint32_t a = ntohl(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
			int score = is_private(a) * 2 + (!strncmp(nm, "wlan", 4) || !strncmp(nm, "wl", 2) || !strncmp(nm, "swlan", 5) || !strncmp(nm, "ap", 2) || !strncmp(nm, "eth", 3) || !strncmp(nm, "en", 2));
			if (score > best) {
				best = score;
				inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, (socklen_t)n);
			}
		}
		freeifaddrs(ifs);
	}
	if (out[0] && best >= 2) return 1;
	/* (some phones hide their connections: ask which way the network goes) */
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) return out[0] != 0;
	struct sockaddr_in to = {0};
	to.sin_family = AF_INET;
	to.sin_port = htons(53);
	inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);	/* (nothing is sent) */
	struct sockaddr_in me;
	socklen_t ml = sizeof me;
	if (connect(s, (struct sockaddr *)&to, sizeof to) == 0 && getsockname(s, (struct sockaddr *)&me, &ml) == 0 &&
	    is_private(ntohl(me.sin_addr.s_addr)))
		inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)n);
	close(s);
	return out[0] != 0;
}
#endif

int net_send_url(char *url, size_t n) {
	char ip[64] = "";
#ifdef ZAPPO_TABLET
	if (!local_ip(ip, sizeof ip)) { url[0] = 0; return -1; }
#else
	WifiStatus ws;
	net_wifi_status(&ws);
	if (!ws.known) { url[0] = 0; return 0; }
	if (!ws.connected || !ws.ip[0]) { url[0] = 0; return -1; }
	sncpy(ip, ws.ip, sizeof ip);
#endif
	snfmt(url, n, "http://%s:%d", ip, send_port());
	return 1;
}

/* ------------------------------------------------------------- Box art */

static char art_status_path[300];
static pid_t art_pid = -1;   /* started by this Zappo X */
static ArtStatus art;
static long long art_seen = -1; /* the status file's mtime+size last read */
static int art_watching;        /* seen running: say something when it ends */
static int art_said = 1;

static const char *art_file(void) {
	if (!art_status_path[0]) tmp_file(art_status_path, sizeof art_status_path, "zappo-x-art.status");
	return art_status_path;
}

static int alive(int pid) {
	if (pid <= 0 || kill(pid, 0) != 0) return 0;
	char p[64], cmd[512] = "";
	snfmt(p, sizeof p, "/proc/%d/cmdline", pid);
	FILE *f = fopen(p, "r");
	if (!f) return 0;
	size_t n = fread(cmd, 1, sizeof cmd - 1, f);
	fclose(f);
	for (size_t i = 0; i < n; i++) if (!cmd[i]) cmd[i] = ' ';
	cmd[n] = 0;
	return strstr(cmd, "get-box-art") != NULL;
}

static void art_read(void) {
	char *txt = file_read_all(art_file(), NULL);
	ArtStatus a;
	memset(&a, 0, sizeof a);
	int pid = 0;
	if (txt) {
		for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
			char *eq = strchr(line, '=');
			if (!eq) continue;
			*eq++ = 0;
			if (!strcmp(line, "state")) sncpy(a.state, eq, sizeof a.state);
			else if (!strcmp(line, "done")) a.done = atoi(eq);
			else if (!strcmp(line, "total")) a.total = atoi(eq);
			else if (!strcmp(line, "found")) a.found = atoi(eq);
			else if (!strcmp(line, "already")) a.already = atoi(eq);
			else if (!strcmp(line, "system")) sncpy(a.system, eq, sizeof a.system);
			else if (!strcmp(line, "game")) sncpy(a.game, eq, sizeof a.game);
			else if (!strcmp(line, "error")) sncpy(a.error, eq, sizeof a.error);
			else if (!strcmp(line, "pid")) pid = atoi(eq);
		}
		free(txt);
	}
	if (art_pid > 0 && waitpid(art_pid, NULL, WNOHANG) == art_pid) art_pid = -1;
	int going = !strcmp(a.state, "looking") || !strcmp(a.state, "getting");
	a.running = art_pid > 0 || (going && alive(pid));
	if (going && !a.running) sncpy(a.state, "stopped", sizeof a.state); /* (switched off part way) */
	if (a.running) { art_watching = 1; art_said = 0; }
	art = a;
}

/* 1 when the status changed (redraw) */
static uint64_t art_next_look;
static int art_check(void) {
	struct stat sb;
	long long k = stat(art_file(), &sb) == 0 ? (long long)sb.st_mtime * 1000003LL + sb.st_size : 0;
	int look = k != art_seen;
	/* a job can end without a last word (power off, killed): check now and then */
	if (!look && art.running && now_ms() >= art_next_look) look = 1;
	if (!look) return 0;
	art_next_look = now_ms() + 2000;
	art_seen = k;
	ArtStatus before = art;
	art_read();
	return memcmp(&before, &art, sizeof art) != 0;
}

int net_art_start(const char *folders) {
	art_check();
	if (art.running) return 0;
	char script[300];
	snfmt(script, sizeof script, "%s/get-box-art.py", g_paths.clove);
	if (!file_exists(script)) { LOG("box art: no %s", script); return -1; }
	FILE *f = fopen(art_file(), "w");
	if (f) { fprintf(f, "state=looking\n"); fclose(f); }
	char status[320];
	sncpy(status, art_file(), sizeof status);
#ifdef ZAPPO_TABLET
	char roms[600];
	snfmt(roms, sizeof roms, "%s/Roms", g_paths.mmc);
	char *argv[] = {"python3", script, "--layout", "garlic", "--roms", roms, "--home", g_paths.clove,
	                "--status", status, "--systems", (char *)(folders ? folders : ""), NULL};
#else
	char *argv[] = {"python3", script, "--status", status, "--systems", (char *)(folders ? folders : ""), NULL};
#endif
	pid_t pid = start_python(argv);
	if (pid < 0) return -1;
	art_pid = pid;
	art_seen = -1;
	art_check();
	art.running = 1;
	sncpy(art.state, "looking", sizeof art.state);
	art_watching = 1;
	art_said = 0;
	return 0;
}

void net_art_stop(void) {
	art_check();
	int pid = art_pid;
	char *txt = file_read_all(art_file(), NULL);
	const char *p = txt ? strstr(txt, "\npid=") : NULL;
	if (pid <= 0 && p && alive(atoi(p + 5))) pid = atoi(p + 5);
	free(txt);
	if (pid > 0) kill(pid, SIGTERM);
	art_said = 1;	/* (the menu says so itself) */
	art_watching = 0;
	art.running = 0;
	sncpy(art.state, "stopped", sizeof art.state);
}

void net_art_status(ArtStatus *out) {
	if (art_seen < 0) art_check();
	*out = art;
}

int net_art_finished(char *msg, size_t n) {
	if (!art_watching || art.running || art_said) return 0;
	art_said = 1;
	art_watching = 0;
	if (!strcmp(art.state, "failed")) snfmt(msg, n, "Box art: %s", art.error[0] ? art.error : "it didn't work");
	else if (!strcmp(art.state, "stopped")) snfmt(msg, n, "Box art stopped after %d pictures", art.found);
	else if (art.total == 0) snfmt(msg, n, "Every game already has box art");
	else if (art.found == art.total) snfmt(msg, n, "Box art: found pictures for all %d games", art.total);
	else snfmt(msg, n, "Box art: found %d of %d. The rest aren't in the library", art.found, art.total);
	return 1;
}

int netapps_poll(void) {
	if (art_check()) return 1;
	return send_check();
}

#else /* GarlicOS on the handheld: neither */

int net_send_start(void) { return -1; }
void net_send_stop(void) {}
int net_send_running(void) { return 0; }
int net_send_received(char names[][96], int max) { (void)names; (void)max; return 0; }
int net_send_url(char *url, size_t n) { if (n) url[0] = 0; return -1; }
int net_art_start(const char *folders) { (void)folders; return -1; }
void net_art_stop(void) {}
void net_art_status(ArtStatus *out) { memset(out, 0, sizeof *out); }
int net_art_finished(char *msg, size_t n) { (void)msg; (void)n; return 0; }
int netapps_poll(void) { return 0; }

#endif
