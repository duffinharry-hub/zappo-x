/* Zappo X — Wi-Fi (and later the other radios), through the firmware's own
 * tools so its settings stay the one place they live. On Knulli: connman and
 * batocera-wifi, as Knulli's own menu uses them. Everything slow runs in the
 * background (one job at a time); the menu asks how it's going each frame. */
#ifndef ZAPPO_NET_H
#define ZAPPO_NET_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	char ssid[64];
	char id[96];     /* connman's name for it */
	int strength;    /* 0-100 */
	int secured;     /* needs a password */
	int connected;
	int saved;       /* the handheld has its password */
} WifiNet;

typedef struct {
	int available;   /* this firmware/handheld has Wi-Fi Zappo can manage */
	int enabled;
	int connected;
	char ssid[64];
	char ip[48];
	int known;       /* a refresh has finished at least once */
} WifiStatus;

int net_poll(void);                 /* 1 when results changed (redraw) */
int net_busy(void);                 /* a background job is running */
const char *net_job(void);          /* what it's doing, for the screen */
const char *net_last_error(void);   /* why the last job failed ("" if it didn't) */

void net_wifi_refresh(int scan);    /* status + the networks around (scan: look again first) */
void net_wifi_power(int on);
/* Joins a network and remembers it (Knulli keeps up to three: wifi, wifi2,
 * wifi3); a wrong password puts back what was there, so a working network
 * isn't lost to a typo. key "" for an open network or one already saved. */
void net_wifi_connect(const WifiNet *n, const char *key);
void net_wifi_forget(const WifiNet *n);
void net_wifi_status(WifiStatus *out);
int net_wifi_list(WifiNet *out, int max);  /* strongest first */

/* ---------------------------------------------------------- Bluetooth */

typedef struct {
	char addr[18];
	char name[64];
	char kind[24];   /* "Controller", "Headphones", ... from its icon */
	int paired, connected;
} BtDev;

typedef struct {
	int available;
	int enabled;     /* Knulli's setting */
	int powered;     /* the adapter is on */
	int known;
} BtStatus;

void net_bt_refresh(int scan);   /* the paired devices (scan: and look for new ones nearby) */
void net_bt_power(int on);
void net_bt_pair(const BtDev *d);
void net_bt_connect(const BtDev *d, int on);
void net_bt_forget(const BtDev *d);
void net_bt_status(BtStatus *out);
int net_bt_list(BtDev *out, int max);  /* paired first, then the ones found nearby */

/* ------------------------------------------------------- Send to Zappo */

/* the little web page for sending files from a phone or PC (send-to-zappo.py),
 * running only while its screen is open (netapps.c: on the handheld and on a
 * tablet) */
int net_send_start(void);        /* 0 = running */
void net_send_stop(void);
int net_send_running(void);
int net_send_received(char names[][96], int max);  /* newest first */
/* the address to open on the phone or PC: 1; 0 while Zappo is still finding
 * out (Wi-Fi being checked); -1 not on Wi-Fi */
int net_send_url(char *url, size_t n);

/* --------------------------------------------------- RetroAchievements */

typedef struct {
	int ok;          /* 1 signed in, 0 not (net_last_error says why) */
	char user[64];   /* as RetroAchievements spells it */
	char token[64];
	int score;
} RaResult;
void net_ra_signin(const char *user, const char *pass);  /* checks the account (background job) */
int net_ra_take(RaResult *out);  /* 1 once, when a sign-in check has finished */

/* ------------------------------------------------------------- Box art */

/* get-box-art.py: pictures for games without one, from the libretro thumbnail
 * library. Runs on its own (low priority), so it carries on through a game;
 * Zappo X follows it through its status file. */
typedef struct {
	int running;
	char state[16];  /* looking, getting, finished, stopped, failed ("" = never run) */
	int done, total, found, already;
	char system[64], game[128], error[160];
} ArtStatus;

int net_art_start(const char *folders);  /* console folders, comma separated ("" = all); 0 = started */
void net_art_stop(void);
void net_art_status(ArtStatus *out);
int net_art_finished(char *msg, size_t n); /* once, when a job Zappo watched ends: what to say */

int netapps_poll(void);  /* (net_poll's part for these two) */

#endif
