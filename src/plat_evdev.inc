/* Zappo — evdev input shared by the handheld platform layers (plat_dev.c for
 * the RG35XX, plat_knulli.c for Knulli handhelds): finds the gamepad and key
 * devices, numbers buttons the way SDL does, maps them to Zappo's buttons. */
#include <linux/input.h>
#include <poll.h>

/* Test hooks: CLOVE_TEST_FRAMES=<n> quits after n frames; CLOVE_TEST_KEYS
 * presses buttons by name. Never set on a device. */
static int test_frames, frames_presented;
static uint64_t test_started;
static const char *test_keys; /* CLOVE_TEST_KEYS="right,right,a": scripted presses, 250 ms apart */
static uint64_t test_key_at;
static int test_key_release = -1;

/* ------------------------------------------------------------------ input */

#define MAX_DEV 16
#define NBITS(x) ((((x) - 1) / (8 * sizeof(unsigned long))) + 1)
#define TEST_BIT(bit, arr) ((arr[(bit) / (8 * sizeof(unsigned long))] >> ((bit) % (8 * sizeof(unsigned long)))) & 1)

typedef struct {
	int fd;
	char name[48];
	char path[32];
	struct input_id id;
	int nbuttons, nhats, naxes;
	int is_joy;
	int is_stock_keys; /* stock Anbernic kernel "gpio-keys-polled" */
	int16_t btn_index[KEY_CNT];
	int8_t axis_index[ABS_CNT];
	int axis_mid[ABS_CNT];
	int hat_x, hat_y;
	int axis_down[ABS_CNT];
	int by_code;               /* buttons matched by evdev code (Knulli's es_input.cfg) */
	int16_t code_btn[B_COUNT]; /* the code for each Zappo button, 0 = none */
} InDev;

static InDev devs[MAX_DEV];
static int ndevs;
static int have_joy;
static InputMap imap;

#define QSIZE 64
static InputEvent queue[QSIZE];
static int qhead, qtail;

static void push(int button, int pressed, const InDev *d, int type, int code, int value) {
	int next = (qtail + 1) % QSIZE;
	if (next == qhead) return;
	InputEvent *e = &queue[qtail];
	memset(e, 0, sizeof *e);
	e->button = button;
	e->pressed = pressed;
	e->raw_type = type;
	e->raw_code = code;
	e->raw_value = value;
	if (d) sncpy(e->dev, d->name, sizeof e->dev);
	qtail = next;
}

static const int STOCK_KEYS[B_COUNT] = {
	/* MinUI's notes for the stock RG35XX kernel's gpio-keys-polled device */
	90, 91, 92, 93, 94, 95, 96, 97, 100, 101, 102, 103, 99, 98, 104, 108, 109, 116,
};

#ifdef ZAPPO_KNULLI
/* Knulli's own button settings for a pad, by name: the user's es_input.cfg
 * (where Knulli keeps a mapping made in its menu) first, then the one Knulli
 * ships. Each <input name=".." type="button" code=".."> gives the evdev code
 * of one button, so Zappo X presses exactly what Knulli's menu presses -
 * whatever order the pad numbers its buttons in. */
static const struct { const char *name; int b; } ES_BUTTONS[] = {
	{"a", B_A}, {"b", B_B}, {"x", B_X}, {"y", B_Y}, {"pageup", B_L1}, {"pagedown", B_R1},
	{"l2", B_L2}, {"r2", B_R2}, {"select", B_SELECT}, {"start", B_START}, {"hotkey", B_MENU},
};

static int attr_of(const char *tag, const char *end, const char *attr, char *out, size_t n) {
	char key[32];
	snfmt(key, sizeof key, " %s=\"", attr);
	const char *p = strstr(tag, key);
	if (!p || p > end) return 0;
	p += strlen(key);
	const char *q = strchr(p, '"');
	if (!q || q > end) return 0;
	snfmt(out, n, "%.*s", (int)(q - p), p);
	return 1;
}

static int es_input_codes(const char *file, const char *dev, int16_t *codes) {
	char *txt = file_read_all(file, NULL);
	if (!txt) return 0;
	char want[80];
	snfmt(want, sizeof want, "deviceName=\"%s\"", dev);
	int found = 0;
	for (char *p = strstr(txt, "<inputConfig"); p && !found; p = strstr(p + 1, "<inputConfig")) {
		char *head_end = strchr(p, '>');
		char *end = strstr(p, "</inputConfig>");
		if (!head_end || !end) break;
		if (!strstr(p, want) || strstr(p, want) > head_end) continue;
		char tp[16];
		if (!attr_of(p, head_end, "type", tp, sizeof tp) || strcmp(tp, "joystick")) continue;
		for (char *in = strstr(p, "<input "); in && in < end; in = strstr(in + 1, "<input ")) {
			char *ie = strchr(in, '>');
			char nm[24], ty[16], cd[16];
			if (!ie || !attr_of(in, ie, "name", nm, sizeof nm) || !attr_of(in, ie, "type", ty, sizeof ty)) continue;
			if (strcmp(ty, "button") || !attr_of(in, ie, "code", cd, sizeof cd)) continue;
			for (int k = 0; k < ARRAY_LEN(ES_BUTTONS); k++)
				if (!strcmp(nm, ES_BUTTONS[k].name)) { codes[ES_BUTTONS[k].b] = (int16_t)atoi(cd); found = 1; }
		}
	}
	free(txt);
	return found;
}

/* A gamepad's buttons by code, from Knulli's settings; the RG40XX family's own
 * pads fall back to the codes in the es_input.cfg Knulli ships for them. */
static void knulli_pad_codes(InDev *d) {
	int16_t c[B_COUNT];
	memset(c, 0, sizeof c);
	const char *from = NULL;
	if (es_input_codes("/userdata/system/configs/emulationstation/es_input.cfg", d->name, c)) from = "your Knulli settings";
	else if (es_input_codes("/usr/share/emulationstation/es_input.cfg", d->name, c)) from = "Knulli's settings";
	else if (strstr(d->name, "Anbernic RG")) {
		static const int16_t RG[][2] = {{B_A, 304}, {B_B, 305}, {B_Y, 306}, {B_X, 307}, {B_L1, 308}, {B_R1, 309},
		                                {B_SELECT, 310}, {B_START, 311}, {B_MENU, 312}, {B_L2, 314}, {B_R2, 315}};
		for (int k = 0; k < ARRAY_LEN(RG); k++) c[RG[k][0]] = RG[k][1];
		from = "the built-in RG40XX codes";
	}
	if (!from) return;
	memcpy(d->code_btn, c, sizeof c);
	d->by_code = 1;
	char list[200] = "";
	for (int b = 0; b < B_COUNT; b++)
		if (c[b]) snfmt(list + strlen(list), sizeof list - strlen(list), " %s=%d", BUTTON_NAMES[b], c[b]);
	LOG("input \"%s\": buttons by code, from %s:%s", d->name, from, list);
}
#endif

static void open_inputs(void) {
	for (int i = 0; i < 32 && ndevs < MAX_DEV; i++) {
		char path[64];
		snfmt(path, sizeof path, "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		InDev *d = &devs[ndevs];
		memset(d, 0, sizeof *d);
		d->fd = fd;
		ioctl(fd, EVIOCGNAME(sizeof d->name), d->name);
		d->name[sizeof d->name - 1] = 0;
		sncpy(d->path, path, sizeof d->path);
		ioctl(fd, EVIOCGID, &d->id);
		unsigned long keybit[NBITS(KEY_CNT)], absbit[NBITS(ABS_CNT)];
		memset(keybit, 0, sizeof keybit);
		memset(absbit, 0, sizeof absbit);
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybit), keybit);
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbit), absbit);
		for (int k = 0; k < KEY_CNT; k++) d->btn_index[k] = -1;
		for (int a = 0; a < ABS_CNT; a++) d->axis_index[a] = -1;
		/* SDL numbers joystick buttons BTN_JOYSTICK..KEY_MAX first, then BTN_MISC..BTN_JOYSTICK. */
		int nb = 0;
		for (int k = BTN_JOYSTICK; k < KEY_MAX; k++)
			if (TEST_BIT(k, keybit)) d->btn_index[k] = (int16_t)nb++;
		for (int k = BTN_MISC; k < BTN_JOYSTICK; k++)
			if (TEST_BIT(k, keybit)) d->btn_index[k] = (int16_t)nb++;
		int na = 0;
		for (int a = 0; a < ABS_MAX; a++) {
			if (a >= ABS_HAT0X && a <= ABS_HAT3Y) continue;
			if (!TEST_BIT(a, absbit)) continue;
			struct input_absinfo ai;
			if (ioctl(fd, EVIOCGABS(a), &ai) == 0) d->axis_mid[a] = (ai.minimum + ai.maximum) / 2;
			d->axis_index[a] = (int8_t)na++;
		}
		d->is_joy = nb > 0 || TEST_BIT(ABS_HAT0X, absbit);
		d->nbuttons = nb;
		d->naxes = na;
		for (int h = ABS_HAT0X; h <= ABS_HAT3X; h += 2)
			if (TEST_BIT(h, absbit) || TEST_BIT(h + 1, absbit)) d->nhats++;
		d->is_stock_keys = strcmp(d->name, "gpio-keys-polled") == 0;
		if (d->is_joy) have_joy = 1;
		LOG("input %s: \"%s\" buttons %d axes %d joy %d", path, d->name, nb, na, d->is_joy);
#ifdef ZAPPO_KNULLI
		if (d->is_joy) {
			char codes[160] = "";
			for (int k = BTN_MISC; k < KEY_MAX && strlen(codes) < sizeof codes - 8; k++)
				if (TEST_BIT(k, keybit)) snfmt(codes + strlen(codes), sizeof codes - strlen(codes), " %d", k);
			LOG("input %s: button codes%s", path, codes);
			knulli_pad_codes(d);
		}
#endif
		ndevs++;
	}
}

static void map_key(InDev *d, int code, int value) {
	if (value == 2) return; /* kernel auto-repeat: the UI does its own */
	int pressed = value != 0;
	if (d->by_code && code >= BTN_MISC) {
		for (int b = 0; b < B_COUNT; b++)
			if (d->code_btn[b] == code) { push(b, pressed, d, EV_KEY, code, value); return; }
		push(-1, pressed, d, EV_KEY, code, value);
		return;
	}
	if (d->is_joy && code >= BTN_MISC && d->btn_index[code] >= 0) {
		int idx = d->btn_index[code];
		for (int b = 0; b < B_COUNT; b++)
			if (imap.joy_btn[b] == idx) { push(b, pressed, d, EV_KEY, code, value); return; }
		push(-1, pressed, d, EV_KEY, code, value);
		return;
	}
	if (d->is_stock_keys) {
		for (int b = 0; b < B_COUNT; b++)
			if (STOCK_KEYS[b] == code) { push(b, pressed, d, EV_KEY, code, value); return; }
	}
	for (int b = 0; b < B_COUNT; b++) {
		/* With a gamepad present, keyboard-style devices only supply power/volume. */
		if (have_joy && b != B_POWER && b != B_VOLUP && b != B_VOLDN) continue;
		for (int k = 0; k < 3; k++)
			if (imap.key[b][k] && imap.key[b][k] == code) { push(b, pressed, d, EV_KEY, code, value); return; }
	}
	push(-1, pressed, d, EV_KEY, code, value);
}

static void map_abs(InDev *d, int code, int value) {
	if (code == ABS_HAT0X || code == ABS_HAT0Y) {
		int *prev = code == ABS_HAT0X ? &d->hat_x : &d->hat_y;
		int neg = code == ABS_HAT0X ? B_LEFT : B_UP;
		int pos = code == ABS_HAT0X ? B_RIGHT : B_DOWN;
		int v = value < 0 ? -1 : value > 0 ? 1 : 0;
		if (v == *prev) return;
		if (*prev < 0) push(neg, 0, d, EV_ABS, code, value);
		if (*prev > 0) push(pos, 0, d, EV_ABS, code, value);
		if (v < 0) push(neg, 1, d, EV_ABS, code, value);
		if (v > 0) push(pos, 1, d, EV_ABS, code, value);
		*prev = v;
		return;
	}
	int idx = d->axis_index[code];
	int b = -1;
	if (idx >= 0 && idx == imap.joy_axis_l2) b = B_L2;
	if (idx >= 0 && idx == imap.joy_axis_r2) b = B_R2;
	if (b < 0) { push(-1, 0, d, EV_ABS, code, value); return; }
	int down = value > d->axis_mid[code] + (d->axis_mid[code] ? d->axis_mid[code] / 2 : 0);
	if (down != d->axis_down[code]) {
		d->axis_down[code] = down;
		push(b, down, d, EV_ABS, code, value);
	}
}

void plat_set_input_map(const InputMap *m) { imap = *m; }

int plat_pads(PadInfo *out, int max) {
	int n = 0;
	/* gamepads with buttons; the handheld's own (on its internal bus) first */
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < ndevs && n < max; i++) {
			const InDev *d = &devs[i];
			int builtin = d->id.bustype == 0x19; /* BUS_HOST */
			if (!d->is_joy || d->nbuttons < 4 || builtin != (pass == 0)) continue;
			PadInfo *p = &out[n++];
			memset(p, 0, sizeof *p);
			sncpy(p->name, d->name, sizeof p->name);
			sncpy(p->path, d->path, sizeof p->path);
			snfmt(p->guid, sizeof p->guid, "%02x%02x0000%02x%02x0000%02x%02x0000%02x%02x0000",
			      d->id.bustype & 255, d->id.bustype >> 8, d->id.vendor & 255, d->id.vendor >> 8,
			      d->id.product & 255, d->id.product >> 8, d->id.version & 255, d->id.version >> 8);
			p->buttons = d->nbuttons;
			p->hats = d->nhats;
			p->axes = d->naxes;
		}
	return n;
}

int plat_poll(InputEvent *ev) {
	if (test_frames && (frames_presented >= test_frames || now_ms() - test_started > (uint64_t)test_frames * 50)) {
		memset(ev, 0, sizeof *ev);
		ev->button = B_QUIT;
		ev->pressed = 1;
		test_frames = 0;
		return 1;
	}
	if (test_keys && now_ms() >= test_key_at) {
		memset(ev, 0, sizeof *ev);
		sncpy(ev->dev, "test keys", sizeof ev->dev);
		test_key_at = now_ms() + 125;
		if (test_key_release >= 0) {
			ev->button = test_key_release;
			test_key_release = -1;
			return 1;
		}
		char name[16];
		int n = 0;
		while (*test_keys && *test_keys != ',' && n < 15) name[n++] = *test_keys++;
		name[n] = 0;
		if (*test_keys == ',') test_keys++;
		if (!*test_keys) test_keys = NULL;
		for (int b = 0; b < B_COUNT; b++)
			if (str_ieq(name, BUTTON_NAMES[b])) {
				ev->button = b;
				ev->pressed = 1;
				test_key_release = b;
				return 1;
			}
	}
	if (qhead == qtail) {
		struct input_event raw[32];
		for (int i = 0; i < ndevs; i++) {
			for (;;) {
				ssize_t n = read(devs[i].fd, raw, sizeof raw);
				if (n <= 0) break;
				int cnt = (int)(n / (ssize_t)sizeof raw[0]);
				for (int j = 0; j < cnt; j++) {
					if (raw[j].type == EV_KEY && raw[j].code < KEY_CNT) map_key(&devs[i], raw[j].code, raw[j].value);
					else if (raw[j].type == EV_ABS && raw[j].code < ABS_CNT) map_abs(&devs[i], raw[j].code, raw[j].value);
				}
			}
		}
	}
	if (qhead == qtail) return 0;
	*ev = queue[qhead];
	qhead = (qhead + 1) % QSIZE;
	return 1;
}

void plat_wait(int timeout_ms) {
#ifdef PLAT_IDLE_HOOK
	timeout_ms = PLAT_IDLE_HOOK(timeout_ms);	/* the platform's own upkeep (it can ask to be back sooner) */
#endif
	if (timeout_ms <= 0) return;
	struct pollfd pfd[MAX_DEV];
	for (int i = 0; i < ndevs; i++) { pfd[i].fd = devs[i].fd; pfd[i].events = POLLIN; pfd[i].revents = 0; }
	if (ndevs) poll(pfd, (nfds_t)ndevs, timeout_ms);
	else usleep((useconds_t)timeout_ms * 1000);
}

uint64_t plat_ticks(void) { return now_ms(); }
int plat_is_host(void) { return 0; }

