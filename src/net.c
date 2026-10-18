/* Zappo X — Wi-Fi through the firmware's own tools (see net.h). */
#include "net.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ZAPPO_KNULLI

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "plat.h"

#define OUT "/tmp/zappo-x-net.out"
#define MAXNET 32

static pid_t job_pid = -1;
static char job_what[64], last_err[160];
static int job_kind;
enum { J_NONE, J_REFRESH, J_POWER, J_CONNECT, J_FORGET, J_BT_REFRESH, J_BT, J_RA };
static RaResult ra_res;
static int ra_new;
static WifiNet nets[MAXNET];
static int nnets;
static WifiStatus st = {1, 0, 0, "", "", 0};
static void bt_parse(void);
static int queued_refresh = -1; /* a refresh asked for while a job ran: 0/1 = scan */
static int followups;           /* after joining/leaving: look again a few times until it settles */
#define MAXBT 24
static BtDev bts[MAXBT];
static int nbts;
static BtStatus bst = {1, 0, 0, 0};
static int bt_queued = -1;
static uint64_t next_followup;

/* the status and the networks, as lines the parser below reads:
 *   S|<wifi.enabled>|<ip>
 *   N|<connman flags>|<strength>|<id>|<name> */
static const char *REFRESH_SH =
	"[ \"$1\" = 1 ] && connmanctl scan wifi >/dev/null 2>&1\n"
	"en=$(sed -n 's/^wifi.enabled=//p' /userdata/system/batocera.conf | tail -n 1)\n"
	"ip=$(ip -4 addr show wlan0 2>/dev/null | sed -n 's/.*inet \\([0-9.]*\\).*/\\1/p' | head -n 1)\n"
	"echo \"S|$en|$ip\"\n"
	"connmanctl services 2>/dev/null | while IFS= read -r line; do\n"
	"  id=${line##* }\n"
	"  case \"$id\" in wifi_*) ;; *) continue ;; esac\n"
	"  flags=$(printf '%s' \"$line\" | cut -c1-4)\n"
	"  name=$(printf '%s' \"$line\" | cut -c5- | sed 's/[[:space:]]*wifi_[^[:space:]]*$//; s/^[[:space:]]*//')\n"
	"  s=$(connmanctl services \"$id\" 2>/dev/null | sed -n 's/^[[:space:]]*Strength = //p')\n"
	"  echo \"N|$flags|${s:-0}|$id|$name\"\n"
	"done\n";

static void start_job(int kind, const char *what, const char *script, const char *a1, const char *a2) {
	if (job_pid > 0) return;
	if (kind != J_REFRESH) last_err[0] = 0; /* (a refresh keeps the last job's message showing) */
	pid_t pid = fork();
	if (pid < 0) { snfmt(last_err, sizeof last_err, "could not start"); return; }
	if (pid == 0) {
		int fd = open(OUT, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
		int nul = open("/dev/null", O_RDONLY);
		if (nul >= 0) dup2(nul, 0);
		for (int i = 3; i < 1024; i++) close(i);
		setsid();
		execl("/bin/sh", "sh", "-c", script, "zappo-net", a1 ? a1 : "", a2 ? a2 : "", (char *)NULL);
		_exit(127);
	}
	job_pid = pid;
	job_kind = kind;
	sncpy(job_what, what, sizeof job_what);
}

static void parse_refresh(void) {
	char *txt = file_read_all(OUT, NULL);
	if (!txt) return;
	nnets = 0;
	st.connected = 0;
	st.ssid[0] = 0;
	for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[5] = {0};
		int nf = 0;
		for (char *p = line; nf < 5; ) {
			f[nf++] = p;
			char *bar = nf < 5 ? strchr(p, '|') : NULL;
			if (!bar) break;
			*bar = 0;
			p = bar + 1;
		}
		if (!strcmp(f[0], "S") && nf >= 3) {
			st.enabled = atoi(f[1]) == 1;
			sncpy(st.ip, f[2], sizeof st.ip);
		} else if (!strcmp(f[0], "N") && nf == 5 && nnets < MAXNET) {
			WifiNet *n = &nets[nnets];
			memset(n, 0, sizeof *n);
			sncpy(n->ssid, f[4], sizeof n->ssid);
			sncpy(n->id, f[3], sizeof n->id);
			n->strength = atoi(f[2]);
			n->saved = f[1][0] == '*';
			n->connected = strchr(f[1], 'R') || strchr(f[1], 'O');
			n->secured = !strstr(n->id, "_none");
			if (!n->ssid[0]) continue;		/* hidden networks: not offered */
			if (n->connected) { st.connected = 1; sncpy(st.ssid, n->ssid, sizeof st.ssid); }
			nnets++;
		}
	}
	free(txt);
	/* strongest first, the connected one at the top */
	for (int i = 1; i < nnets; i++)
		for (int j = i; j > 0; j--) {
			WifiNet *a = &nets[j - 1], *b = &nets[j];
			int ka = a->connected * 1000 + a->strength, kb = b->connected * 1000 + b->strength;
			if (kb <= ka) break;
			WifiNet t = *a; *a = *b; *b = t;
		}
	st.known = 1;
}

static long send_seen = -1;
static int send_check(void);

static int art_check(void);

int net_poll(void) {
	if (art_check()) return 1;
	if (send_check()) return 1;
	if (job_pid <= 0) {
		if (queued_refresh >= 0) { int s = queued_refresh; queued_refresh = -1; net_wifi_refresh(s); }
		else if (bt_queued >= 0) { int s = bt_queued; bt_queued = -1; net_bt_refresh(s); }
		else if (followups > 0 && now_ms() >= next_followup) { followups--; net_wifi_refresh(0); }
		return 0;
	}
	int status;
	pid_t r = waitpid(job_pid, &status, WNOHANG);
	if (r == 0) return 0;
	int ok = r == job_pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
	int kind = job_kind;
	job_pid = -1;
	job_kind = J_NONE;
	job_what[0] = 0;
	if (kind == J_BT_REFRESH) {
		bt_parse();
		return 1;
	}
	if (kind == J_RA) {
		char *txt = file_read_all(OUT, NULL);
		memset(&ra_res, 0, sizeof ra_res);
		if (ok && txt && !strncmp(txt, "OK|", 3)) {
			char *f[3] = {NULL, NULL, NULL}, *p = txt + 3;
			for (int i = 0; i < 3 && p; i++) { f[i] = p; p = strchr(p, '|'); if (p) *p++ = 0; }
			for (int i = 0; i < 3; i++) if (f[i]) f[i][strcspn(f[i], "\r\n")] = 0;
			sncpy(ra_res.user, f[0] ? f[0] : "", sizeof ra_res.user);
			sncpy(ra_res.token, f[1] ? f[1] : "", sizeof ra_res.token);
			ra_res.score = f[2] ? atoi(f[2]) : 0;
			ra_res.ok = 1;
		} else {
			const char *m = txt ? strstr(txt, "E|") : NULL;
			snfmt(last_err, sizeof last_err, "%s", m ? m + 2 : "Couldn't reach RetroAchievements: is Wi-Fi connected?");
			last_err[strcspn(last_err, "\r\n")] = 0;
		}
		free(txt);
		ra_new = 1;
		return 1;
	}
	if (kind == J_BT) {
		if (!ok) {
			char *txt = file_read_all(OUT, NULL);
			const char *m = txt ? strrchr(txt, '|') : NULL;	/* the script's last word, if it left one */
			snfmt(last_err, sizeof last_err, "%s", m ? m + 1 : txt && *txt ? txt : "it didn't work");
			free(txt);
			char *nl = strchr(last_err, '\n');
			if (nl) *nl = 0;
		}
		net_bt_refresh(0);
		return 1;
	}
	if (kind == J_REFRESH) {
		parse_refresh();
		if (st.connected || !st.enabled) followups = 0;
		next_followup = now_ms() + 4000;
	} else {
		followups = 10;	/* (a slow network can take a while to hand out an address) */
		if (!ok) {
			char *txt = file_read_all(OUT, NULL);
			snfmt(last_err, sizeof last_err, "%s", txt && *txt ? txt : "it didn't work");
			free(txt);
			char *nl = strchr(last_err, '\n');
			if (nl) *nl = 0;
		}
		net_wifi_refresh(0);	/* show what changed */
	}
	return 1;
}

int net_busy(void) { return job_pid > 0 || followups > 0; }
const char *net_job(void) { return job_what[0] ? job_what : "Checking"; }
const char *net_last_error(void) { return last_err; }

void net_wifi_refresh(int scan) {
	if (job_pid > 0) { if (queued_refresh < scan) queued_refresh = scan; return; }
	start_job(J_REFRESH, scan ? "Looking for networks" : "Checking", REFRESH_SH, scan ? "1" : "0", NULL);
}

void net_wifi_power(int on) {
	start_job(J_POWER, on ? "Switching Wi-Fi on" : "Switching Wi-Fi off",
	          on ? "batocera-wifi enable" : "batocera-wifi disable", NULL, NULL);
	st.enabled = on;
}

/* the three networks Knulli remembers: wifi.ssid/key, wifi2.*, wifi3.* */
#define SLOTS_SH \
	"C=/userdata/system/batocera.conf\n" \
	"get() { sed -n \"s/^$1=//p\" $C | tail -n 1; }\n" \
	"pre() { [ \"$1\" = 1 ] && echo wifi || echo wifi$1; }\n" \
	"up() { connmanctl services \"$1\" 2>/dev/null | grep -qE 'State = (ready|online)' && \\\n" \
	"  ip -4 addr show wlan0 | grep 'inet ' | grep -qv 'inet 169\\.254'; }\n"

/* leave the network the handheld is on (connman keeps one at a time) */
#define LEAVE_SH \
	"leave() { cur=$(connmanctl services | sed -n 's/^\\*A[RO] .* \\(wifi_[^ ]*\\)$/\\1/p' | head -n 1)\n" \
	"  [ -n \"$cur\" ] && [ \"$cur\" != \"$1\" ] && connmanctl disconnect \"$cur\" >/dev/null 2>&1; }\n" \
	"seen() { connmanctl scan wifi >/dev/null 2>&1\n" \
	"  for i in $(seq 1 20); do connmanctl services | grep -q \"$1\" && return 0; sleep 1; done; return 1; }\n"

void net_wifi_connect(const WifiNet *n, const char *key) {
	char what[96];
	snfmt(what, sizeof what, "Connecting to %s", n->ssid);
	if (n->saved && (!key || !*key)) {	/* its password is already known */
		start_job(J_CONNECT, what, SLOTS_SH LEAVE_SH
		          "seen \"$1\"; leave \"$1\"\n"
		          "connmanctl connect \"$1\" >/dev/null 2>&1\n"
		          "for i in $(seq 1 20); do up \"$1\" && exit 0; sleep 1; done\n"
		          "echo 'No connection'; exit 1\n", n->id, NULL);
		return;
	}
	/* $1 ssid, $2 key, $3 connman id */
	char *script = xstrdup(SLOTS_SH LEAVE_SH
	          "found=; free=\n"
	          "for x in 1 2 3; do s=$(get \"$(pre $x).ssid\")\n"
	          "  [ \"$s\" = \"$1\" ] && found=$x\n"
	          "  [ -z \"$s\" ] && [ -z \"$free\" ] && free=$x\n"
	          "done\n"
	          "slot=${found:-${free:-3}}; P=$(pre $slot)\n"
	          "olds=$(get $P.ssid); oldk=$(get $P.key)\n"
	          "batocera-settings-set wifi.enabled 1\n"
	          "batocera-settings-set $P.ssid \"$1\"; batocera-settings-set $P.key \"$2\"\n"
	          "/etc/init.d/S20connman reload >/dev/null 2>&1; sleep 2\n"
	          "seen \"$3\"; leave \"$3\"\n"
	          "connmanctl connect \"$3\" >/dev/null 2>&1\n"
	          "for i in $(seq 1 25); do up \"$3\" && exit 0; sleep 1; done\n"
	          "# no luck: put back what was there\n"
	          "batocera-settings-set $P.ssid \"$olds\"; batocera-settings-set $P.key \"$oldk\"\n"
	          "connmanctl config \"$3\" --remove >/dev/null 2>&1\n"
	          "/etc/init.d/S20connman reload >/dev/null 2>&1\n"
	          "for i in $(seq 1 25); do connmanctl services | grep -q '^\\*A[RO]' && break; sleep 1; done\n"
	          "[ -n \"$2\" ] && echo \"Couldn't join $1: check the password\" || echo \"Couldn't join $1\"; exit 1\n");
	/* the id goes in as $3: pass it through the ssid/key slots of start_job */
	static char args[2][160];
	sncpy(args[0], n->ssid, sizeof args[0]);
	sncpy(args[1], key ? key : "", sizeof args[1]);
	char *full = xmalloc(strlen(script) + 200);
	snfmt(full, strlen(script) + 200, "set -- \"$1\" \"$2\" '%s'\n%s", n->id, script);
	start_job(J_CONNECT, what, full, args[0], args[1]);
	free(full);
	free(script);
}

void net_wifi_forget(const WifiNet *n) {
	char what[96];
	snfmt(what, sizeof what, "Forgetting %s", n->ssid);
	char *full = xmalloc(1024);
	snfmt(full, 1024, SLOTS_SH
	      "for x in 1 2 3; do P=$(pre $x)\n"
	      "  [ \"$(get $P.ssid)\" = \"$1\" ] && { batocera-settings-set $P.ssid ''; batocera-settings-set $P.key ''; }\n"
	      "done\n"
	      "connmanctl disconnect '%s' >/dev/null 2>&1; connmanctl config '%s' --remove >/dev/null 2>&1\n"
	      "/etc/init.d/S20connman reload >/dev/null 2>&1\n", n->id, n->id);
	start_job(J_FORGET, what, full, n->ssid, NULL);
	free(full);
}

void net_wifi_status(WifiStatus *out) { *out = st; }

/* ---------------------------------------------------------- Bluetooth */

/*   B|<controllers.bluetooth.enabled>|<adapter powered yes/no>
 *   D|<address>|<paired>|<connected>|<icon>|<name> */
static const char *BT_REFRESH_SH =
	"en=$(sed -n 's/^controllers.bluetooth.enabled=//p' /userdata/system/batocera.conf | tail -n 1)\n"
	"pw=$(bluetoothctl show 2>/dev/null | sed -n 's/^[[:space:]]*Powered: //p' | head -n 1)\n"
	"echo \"B|$en|$pw\"\n"
	"[ \"$pw\" = yes ] || exit 0\n"
	"[ \"$1\" = 1 ] && bluetoothctl --timeout 15 scan on >/dev/null 2>&1\n"
	"bluetoothctl devices 2>/dev/null | while read -r w addr name; do\n"
	"  [ \"$w\" = Device ] || continue\n"
	"  info=$(bluetoothctl info \"$addr\" 2>/dev/null)\n"
	"  f() { printf '%s\\n' \"$info\" | sed -n \"s/^[[:space:]]*$1: //p\" | head -n 1; }\n"
	"  echo \"D|$addr|$(f Paired)|$(f Connected)|$(f Icon)|$name\"\n"
	"done\n";

static void kind_of(const char *icon, char *out, size_t n) {
	static const struct { const char *icon, *kind; } K[] = {
		{"input-gaming", "Controller"}, {"audio-headset", "Headphones"}, {"audio-headphones", "Headphones"},
		{"audio-card", "Speaker"}, {"phone", "Phone"}, {"input-keyboard", "Keyboard"},
		{"input-mouse", "Mouse"}, {"computer", "Computer"},
	};
	out[0] = 0;
	for (int i = 0; i < ARRAY_LEN(K); i++)
		if (!strcmp(icon, K[i].icon)) sncpy(out, K[i].kind, n);
}

static void bt_parse(void) {
	char *txt = file_read_all(OUT, NULL);
	if (!txt) return;
	nbts = 0;
	for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[6] = {0};
		int nf = 0;
		for (char *p = line; nf < 6; ) {
			f[nf++] = p;
			char *bar = nf < 6 ? strchr(p, '|') : NULL;
			if (!bar) break;
			*bar = 0;
			p = bar + 1;
		}
		if (!strcmp(f[0], "B") && nf >= 3) {
			bst.enabled = atoi(f[1]) == 1;
			bst.powered = !strcmp(f[2], "yes");
		} else if (!strcmp(f[0], "D") && nf == 6 && nbts < MAXBT) {
			BtDev *d = &bts[nbts];
			memset(d, 0, sizeof *d);
			sncpy(d->addr, f[1], sizeof d->addr);
			sncpy(d->name, f[5], sizeof d->name);
			d->paired = !strcmp(f[2], "yes");
			d->connected = !strcmp(f[3], "yes");
			kind_of(f[4], d->kind, sizeof d->kind);
			/* a device seen without its name: only its address, not worth offering */
			char dashed[18];
			sncpy(dashed, d->addr, sizeof dashed);
			for (char *c = dashed; *c; c++) if (*c == ':') *c = '-';
			if (!d->paired && (!d->name[0] || !strcmp(d->name, dashed))) continue;
			nbts++;
		}
	}
	free(txt);
	/* paired (connected first), then the others */
	for (int i = 1; i < nbts; i++)
		for (int j = i; j > 0; j--) {
			BtDev *a = &bts[j - 1], *b = &bts[j];
			int ka = a->paired * 2 + a->connected, kb = b->paired * 2 + b->connected;
			if (kb <= ka) break;
			BtDev t = *a; *a = *b; *b = t;
		}
	bst.known = 1;
}

void net_bt_refresh(int scan) {
	if (job_pid > 0) { if (bt_queued < scan) bt_queued = scan; return; }
	start_job(J_BT_REFRESH, scan ? "Looking for devices nearby" : "Checking", BT_REFRESH_SH, scan ? "1" : "0", NULL);
}

void net_bt_power(int on) {
	/* Knulli's own switch: its setting, then its Bluetooth services */
	start_job(J_BT, on ? "Switching Bluetooth on" : "Switching Bluetooth off",
	          on ? "batocera-bluetooth enable >/dev/null 2>&1; sleep 2" : "batocera-bluetooth disable >/dev/null 2>&1", NULL, NULL);
	bst.enabled = on;
}

void net_bt_pair(const BtDev *d) {
	/* Knulli's agent pairs, trusts and connects it (and saves the pairing) */
	char what[96];
	snfmt(what, sizeof what, "Pairing %s", d->name);
	/* Knulli's agent pairs a device it sees while looking (as when its own
	 * menu lists devices): ask for it, then have the agent look */
	start_job(J_BT, what,
	          "A=$(cat /var/run/bluetooth-agent.pid 2>/dev/null)\n"
	          "batocera-bluetooth trust \"$1\" >/dev/null 2>&1 &\n"
	          "T=$!; sleep 1; [ -n \"$A\" ] && kill -USR1 \"$A\"\n"
	          "wait $T; [ -n \"$A\" ] && kill -USR2 \"$A\"\n"
	          "bluetoothctl info \"$1\" | grep -q 'Paired: yes' && exit 0\n"
	          "echo \"|Couldn't pair $2: is it in pairing mode?\"; exit 1\n", d->addr, d->name);
}

void net_bt_connect(const BtDev *d, int on) {
	char what[96];
	snfmt(what, sizeof what, "%s %s", on ? "Connecting" : "Disconnecting", d->name);
	start_job(J_BT, what, on ? "batocera-bluetooth connect \"$1\" >/dev/null 2>&1 || { echo \"|$2 didn't connect: is it switched on?\"; exit 1; }"
	                         : "batocera-bluetooth disconnect \"$1\" >/dev/null 2>&1; exit 0", d->addr, d->name);
}

void net_bt_forget(const BtDev *d) {
	char what[96];
	snfmt(what, sizeof what, "Forgetting %s", d->name);
	start_job(J_BT, what, "batocera-bluetooth remove \"$1\" >/dev/null 2>&1; exit 0", d->addr, NULL);
}

void net_bt_status(BtStatus *out) { *out = bst; }

/* ------------------------------------------------------- Send to Zappo */

#define RECEIVED "/tmp/zappo-x-received"
static pid_t send_pid = -1;

int net_send_start(void) {
	if (send_pid > 0) return 0;
	char script[300], model[64] = "the handheld";
	snfmt(script, sizeof script, "%s/send-to-zappo.py", g_paths.clove);
	FILE *f = fopen("/proc/device-tree/model", "r");
	if (f) { if (!fgets(model, sizeof model, f)) sncpy(model, "the handheld", sizeof model); fclose(f); }
	unlink(RECEIVED);
	send_seen = 0;
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
		for (int i = 3; i < 1024; i++) close(i);
		setsid();
		char name[96];
		snfmt(name, sizeof name, "Zappo X on %s", model);
		setenv("ZAPPO_NAME", name, 1);
		execl("/usr/bin/python3", "python3", script, "8640", (char *)NULL);
		_exit(127);
	}
	send_pid = pid;
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
	if (send_pid > 0 && waitpid(send_pid, NULL, WNOHANG) == send_pid) send_pid = -1;
	return send_pid > 0;
}

/* something new arrived: redraw */
static int send_check(void) {
	if (send_pid <= 0) return 0;
	struct stat sb;
	long sz = stat(RECEIVED, &sb) == 0 ? (long)sb.st_size : 0;
	if (sz == send_seen) return 0;
	send_seen = sz;
	return 1;
}

int net_send_received(char names[][96], int max) {
	char *txt = file_read_all(RECEIVED, NULL);
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

/* --------------------------------------------------- RetroAchievements */

/* The same sign-in RetroArch makes (rcheevos' login2), sent as a form so the
 * password isn't in an address; the answer is JSON. */
static const char *RA_SH =
	"r=$(curl -s -m 25 -A 'Zappo X' --data-urlencode r=login2 --data-urlencode \"u=$1\" --data-urlencode \"p=$2\" "
	"https://retroachievements.org/dorequest.php) || { echo \"E|Couldn't reach RetroAchievements: is Wi-Fi connected?\"; exit 1; }\n"
	"printf '%s' \"$r\" | python3 -c '\n"
	"import json, sys\n"
	"try:\n"
	"    j = json.load(sys.stdin)\n"
	"except Exception:\n"
	"    print(\"E|RetroAchievements answered with something Zappo X does not understand\"); sys.exit(1)\n"
	"if j.get(\"Success\"):\n"
	"    print(\"OK|%s|%s|%s\" % (j.get(\"User\", \"\"), j.get(\"Token\", \"\"), j.get(\"Score\", 0)))\n"
	"else:\n"
	"    print(\"E|%s\" % (j.get(\"Error\") or \"That did not work\")); sys.exit(1)\n"
	"'\n";

void net_ra_signin(const char *user, const char *pass) {
	ra_new = 0;
	start_job(J_RA, "Signing in", RA_SH, user, pass);
}

int net_ra_take(RaResult *out) {
	if (!ra_new) return 0;
	ra_new = 0;
	*out = ra_res;
	return 1;
}

/* ------------------------------------------------------------- Box art */

#define ART_STATUS "/tmp/zappo-x-art.status"
static pid_t art_pid = -1;   /* started by this Zappo X */
static ArtStatus art;
static long long art_seen = -1; /* the status file's mtime+size last read */
static int art_watching;        /* seen running: say something when it ends */
static int art_said = 1;

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
	char *txt = file_read_all(ART_STATUS, NULL);
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
	if (going && !a.running) sncpy(a.state, "stopped", sizeof a.state); /* (the handheld was switched off) */
	if (a.running) { art_watching = 1; art_said = 0; }
	art = a;
}

/* 1 when the status changed (redraw) */
static uint64_t art_next_look;
static int art_check(void) {
	struct stat sb;
	long long k = stat(ART_STATUS, &sb) == 0 ? (long long)sb.st_mtime * 1000003LL + sb.st_size : 0;
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
	if (!file_exists(script)) return -1;
	FILE *f = fopen(ART_STATUS, "w");
	if (f) { fprintf(f, "state=looking\n"); fclose(f); }
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
		for (int i = 3; i < 1024; i++) close(i);
		setsid();	/* (keeps going while a game is played) */
		execl("/usr/bin/python3", "python3", script, "--systems", folders ? folders : "", (char *)NULL);
		_exit(127);
	}
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
	char *txt = file_read_all(ART_STATUS, NULL);
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

int net_bt_list(BtDev *out, int max) {
	int n = MIN(max, nbts);
	memcpy(out, bts, sizeof(BtDev) * (size_t)n);
	return n;
}

int net_wifi_list(WifiNet *out, int max) {
	int n = MIN(max, nnets);
	memcpy(out, nets, sizeof(WifiNet) * (size_t)n);
	return n;
}

#else /* other firmwares: nothing to manage from Zappo */

int net_poll(void) { return 0; }
int net_busy(void) { return 0; }
const char *net_job(void) { return ""; }
const char *net_last_error(void) { return ""; }
void net_wifi_refresh(int scan) { (void)scan; }
void net_wifi_power(int on) { (void)on; }
void net_wifi_connect(const WifiNet *n, const char *key) { (void)n; (void)key; }
void net_wifi_forget(const WifiNet *n) { (void)n; }
void net_wifi_status(WifiStatus *out) { memset(out, 0, sizeof *out); }
void net_bt_refresh(int scan) { (void)scan; }
void net_bt_power(int on) { (void)on; }
void net_bt_pair(const BtDev *d) { (void)d; }
void net_bt_connect(const BtDev *d, int on) { (void)d; (void)on; }
void net_bt_forget(const BtDev *d) { (void)d; }
void net_bt_status(BtStatus *out) { memset(out, 0, sizeof *out); }
int net_bt_list(BtDev *out, int max) { (void)out; (void)max; return 0; }
int net_send_start(void) { return -1; }
void net_send_stop(void) {}
int net_send_running(void) { return 0; }
int net_send_received(char names[][96], int max) { (void)names; (void)max; return 0; }
int net_wifi_list(WifiNet *out, int max) { (void)out; (void)max; return 0; }
void net_ra_signin(const char *user, const char *pass) { (void)user; (void)pass; }
int net_ra_take(RaResult *out) { (void)out; return 0; }
int net_art_start(const char *folders) { (void)folders; return -1; }
void net_art_stop(void) {}
void net_art_status(ArtStatus *out) { memset(out, 0, sizeof *out); }
int net_art_finished(char *msg, size_t n) { (void)msg; (void)n; return 0; }

#endif
