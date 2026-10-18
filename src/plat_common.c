/* Parts of the hardware layer shared by the device and host builds. */
#include "plat.h"
#include "util.h"

#include <string.h>

Paths g_paths;

const char *const BUTTON_NAMES[B_COUNT] = {
	"up", "down", "left", "right",
	"a", "b", "x", "y",
	"l1", "r1", "l2", "r2",
	"select", "start", "menu",
	"volup", "voldown", "power",
};

void input_map_defaults(InputMap *m) {
	memset(m, 0, sizeof *m);
	for (int i = 0; i < B_COUNT; i++) m->joy_btn[i] = -1;
	/* From GarlicOS's RetroArch autoconfig ("RG35XX Gamepad.cfg") and hotkeys. */
	m->joy_btn[B_A] = 0;
	m->joy_btn[B_B] = 1;
	m->joy_btn[B_X] = 2;
	m->joy_btn[B_Y] = 3;
	m->joy_btn[B_L1] = 5;
	m->joy_btn[B_R1] = 6;
	m->joy_btn[B_SELECT] = 7;
	m->joy_btn[B_START] = 8;
	m->joy_btn[B_MENU] = 9;
	m->joy_btn[B_VOLUP] = 10;
	m->joy_btn[B_VOLDN] = 11;
	m->joy_axis_l2 = 2;
	m->joy_axis_r2 = 5;
	/* Plain keyboard-style codes: power and volume keys (linux/input.h). */
	m->key[B_POWER][0] = 116;  /* KEY_POWER */
	m->key[B_VOLUP][0] = 115;  /* KEY_VOLUMEUP */
	m->key[B_VOLDN][0] = 114;  /* KEY_VOLUMEDOWN */
	/* Desktop keyboard (host testing, USB keyboards). */
	m->key[B_UP][0] = 103;    /* KEY_UP */
	m->key[B_DOWN][0] = 108;  /* KEY_DOWN */
	m->key[B_LEFT][0] = 105;  /* KEY_LEFT */
	m->key[B_RIGHT][0] = 106; /* KEY_RIGHT */
	m->key[B_A][0] = 28;      /* KEY_ENTER */
	m->key[B_B][0] = 14;      /* KEY_BACKSPACE */
	m->key[B_START][1] = 57;  /* KEY_SPACE */
	m->key[B_MENU][0] = 1;    /* KEY_ESC */
}
