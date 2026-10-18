/* Clove — hardware layer. plat_dev.c talks to the RG35XX (framebuffer,
 * evdev, sysfs); plat_knulli.c to Knulli handhelds (RG40XX H: SDL2, evdev,
 * sysfs); plat_host.c is a desktop stand-in used for testing. */
#ifndef CLOVE_PLAT_H
#define CLOVE_PLAT_H

#include <stddef.h>
#include <stdint.h>

/* what Zappo X adds on the Knulli handheld and on a tablet (Termux): Send to
 * Zappo, Get box art and the moving backgrounds */
#if defined(ZAPPO_KNULLI) || defined(ZAPPO_TABLET)
#define ZAPPO_APPS 1
#endif

#define SCREEN_W 640
#define SCREEN_H 480

typedef enum {
	B_UP, B_DOWN, B_LEFT, B_RIGHT,
	B_A, B_B, B_X, B_Y,
	B_L1, B_R1, B_L2, B_R2,
	B_SELECT, B_START, B_MENU,
	B_VOLUP, B_VOLDN, B_POWER,
	B_COUNT,
	B_QUIT = 100 /* host test harness only */
} Button;

extern const char *const BUTTON_NAMES[B_COUNT];

typedef struct {
	int button;  /* Button, or -1 when the raw event is not mapped */
	int pressed; /* 1 = down, 0 = up */
	int repeat;  /* synthesised by the UI's key-repeat */
	int raw_type, raw_code, raw_value;
	char dev[48];
} InputEvent;

/* How raw input maps to buttons. Joystick indexes follow SDL's numbering,
 * which is what RetroArch's "RG35XX Gamepad.cfg" autoconfig uses. */
typedef struct {
	int joy_btn[B_COUNT];
	int joy_axis_l2, joy_axis_r2;
	int key[B_COUNT][3]; /* raw evdev key codes, 0 = unused */
} InputMap;

typedef struct {
	char mmc[256];  /* TF1 ROMS partition: /mnt/mmc */
	char sd[256];   /* TF2 card: /mnt/SDCARD */
	char cfw[256];  /* /mnt/mmc/CFW */
	char clove[256];/* /mnt/mmc/CFW/clove */
	char music[256];/* the Music folder, when not <mmc>/Music (Knulli: /userdata/music) */
	char fonts[256];/* the menu fonts, when not GarlicOS's <cfw>/font */
	int sd_present;
	int mmc_is_tf2; /* TF2 boot: the TF2 card is mounted at /mnt/mmc */
} Paths;
extern Paths g_paths;

void input_map_defaults(InputMap *m);

int plat_init(void);
void plat_quit(void);
uint32_t *plat_canvas(void); /* SCREEN_W x SCREEN_H, 0xAARRGGBB, alpha ignored */
void plat_present(void);
int plat_has_vsync(void);
void plat_set_input_map(const InputMap *m);
int plat_poll(InputEvent *ev);
void plat_wait(int timeout_ms);
uint64_t plat_ticks(void);
int plat_is_host(void);

int plat_battery(void);  /* 0..100, -1 unknown */
int plat_charging(void);
int plat_brightness_get(void); /* 0..10 */
void plat_brightness_set(int level, int persist);
void plat_screen_off(int off);
void plat_screen_dim(int dim); /* very low backlight (sleep clock) / back to normal */

int plat_cpu_freqs(int *out, int max); /* available kHz values, ascending */
void plat_cpu_set(const char *governor, int max_khz, int min_khz);
int plat_cpu_cur(void);

void plat_describe(char *out, size_t n); /* diagnostics for the About page */

/* Game controllers present (handheld layers with evdev): what an emulator
 * front end passes on to say which pad is player 1, 2, ... */
typedef struct {
	char name[64], path[64];
	char guid[33];  /* SDL-style GUID (bus, vendor, product, version) */
	int buttons, hats, axes;
} PadInfo;
int plat_pads(PadInfo *out, int max); /* the handheld's own controls first */

#ifdef ZAPPO_APPS
/* The moving backgrounds: the canvas (see-through, premultiplied) goes over
 * `bg` (640 x 480, opaque) and the element's particles (by the GPU on the
 * handheld). bg NULL: the canvas is the whole picture, as before. bg_changed:
 * the picture is new since last time. element: EL_* (scene.h). */
int plat_layers_ok(void);       /* this screen can do it */
void plat_scene(const uint32_t *bg, int bg_changed, int element, int density);
int plat_scene_frame(void);     /* nothing new to draw: move the particles on; ms until the next frame (0: none) */
void plat_transition(void);     /* the menu is about to change section: fade from what's on screen now */
#endif

#ifdef ZAPPO_KNULLI
int plat_mpv_remote(const char *sock_path); /* the buttons for Knulli's mpv (films) */
/* Knulli's own settings (batocera.conf), so a choice made in Zappo X is the
 * one Knulli's menu and its emulators see too */
int plat_conf_get(const char *key, char *out, size_t n);  /* 1 = set ("" otherwise) */
int plat_conf_int(const char *key, int fallback);
void plat_conf_set(const char *key, const char *value);    /* NULL: back to Knulli's default */
/* the stick lights (Settings > Lights), through Knulli's light daemon */
enum { LIGHTS_FOLLOW, LIGHTS_ONE, LIGHTS_KNULLI, LIGHTS_OFF, LIGHTS_COUNT };
#define LIGHT_COLOURS 9
typedef struct { const char *name; uint32_t rgb; } LightColour;
extern const LightColour plat_light_colours[LIGHT_COLOURS];
int plat_lights_available(void);
void plat_lights_apply(void);  /* after a change in Settings > Lights */
/* holds off Knulli's battery saver (dimming, muting, sleeping when no button
 * is pressed for a while): while music plays or files are arriving */
void plat_keep_awake(int on);
int plat_tv(void);              /* a TV is plugged in (HDMI) */
int plat_display_changed(void); /* the TV went in or out since Zappo X started */
#endif
void plat_power(int reboot);           /* sync, then power off / reboot */

#endif
