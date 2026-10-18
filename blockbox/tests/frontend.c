/* Minimal libretro frontend for testing Blockbox: runs scripted frames,
 * times rendering and writes PNG screenshots. Links the core directly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/libretro.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb_image_write.h"

void retro_set_environment(retro_environment_t);
void retro_set_video_refresh(retro_video_refresh_t);
void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void retro_set_input_poll(retro_input_poll_t);
void retro_set_input_state(retro_input_state_t);
void retro_init(void);
bool retro_load_game(const struct retro_game_info *);
void retro_run(void);
void *retro_get_memory_data(unsigned);
size_t retro_get_memory_size(unsigned);
size_t retro_serialize_size(void);
bool retro_serialize(void *, size_t);

static const uint16_t *last_fb; static unsigned fw, fh; static size_t fpitch;
static unsigned held; static long audio_frames;
static int64_t now_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000; }
static retro_time_t get_time(void) { return now_us(); }
static bool env(unsigned cmd, void *data) {
	if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
	if (cmd == RETRO_ENVIRONMENT_GET_PERF_INTERFACE) { struct retro_perf_callback *p = data; memset(p, 0, sizeof *p); p->get_time_usec = get_time; return getenv("NO_PERF") == NULL; }
	if (cmd == RETRO_ENVIRONMENT_GET_CAN_DUPE) { *(bool *)data = true; return true; }
	if (cmd == RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS || cmd == RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME) return true;
	return false;
}
static void video(const void *d, unsigned w, unsigned h, size_t pitch) { if (!d) return; last_fb = d; fw = w; fh = h; fpitch = pitch; }
static size_t audio(const int16_t *d, size_t n) { (void)d; audio_frames += (long)n; return n; }
static void poll_(void) {}
static int16_t state(unsigned port, unsigned dev, unsigned idx, unsigned id) {
	(void)port; (void)idx;
	if (dev == RETRO_DEVICE_JOYPAD) return (held >> id) & 1;
	return 0;
}
static void shot(const char *path) {
	unsigned char *rgb = malloc(fw * fh * 3);
	for (unsigned y = 0; y < fh; y++) for (unsigned x = 0; x < fw; x++) {
		uint16_t c = ((const uint16_t *)((const char *)last_fb + y * fpitch))[x];
		unsigned char *o = rgb + (y * fw + x) * 3;
		o[0] = ((c >> 11) & 31) * 255 / 31; o[1] = ((c >> 5) & 63) * 255 / 63; o[2] = (c & 31) * 255 / 31;
	}
	stbi_write_png(path, fw, fh, 3, rgb, fw * 3);
	free(rgb);
	printf("shot %s\n", path);
}
static int bit(const char *n) {
	static const char *names[] = {"b","y","select","start","up","down","left","right","a","x","l","r","l2","r2","l3","r3"};
	for (int i = 0; i < 16; i++) if (!strcmp(n, names[i])) return i;
	return -1;
}
int main(int argc, char **argv) {
	/* argv: world-path script ; script tokens: hold:btn,btn:frames  shot:file  run:frames  sram-save:file sram-load:file */
	retro_set_environment(env); retro_set_video_refresh(video); retro_set_audio_sample_batch(audio);
	retro_set_input_poll(poll_); retro_set_input_state(state);
	retro_init();
	struct retro_game_info gi = {argv[1], NULL, 0, NULL};
	retro_load_game(&gi);
	char *script = strdup(argc > 2 ? argv[2] : "run:5");
	int64_t render_total = 0; long frames = 0;
	for (char *tok = strtok(script, " "); tok; tok = strtok(NULL, " ")) {
		if (!strncmp(tok, "shot:", 5)) { shot(tok + 5); continue; }
		if (!strncmp(tok, "sram-load:", 10)) {
			FILE *f = fopen(tok + 10, "rb"); if (f) { size_t n = fread(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 1, retro_get_memory_size(RETRO_MEMORY_SAVE_RAM), f); fclose(f); printf("loaded sram %zu bytes\n", n); }
			continue;
		}
		if (!strncmp(tok, "sram-save:", 10)) {
			FILE *f = fopen(tok + 10, "wb"); fwrite(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 1, retro_get_memory_size(RETRO_MEMORY_SAVE_RAM), f); fclose(f); continue;
		}
		int n = 1; held = 0;
		char *colon = strrchr(tok, ':');
		if (colon) n = atoi(colon + 1);
		if (!strncmp(tok, "hold:", 5)) {
			char buf[128]; snprintf(buf, sizeof buf, "%.*s", (int)(colon - tok - 5), tok + 5);
			char *save;
			for (char *p = strtok_r(buf, ",", &save); p; p = strtok_r(NULL, ",", &save)) { int k = bit(p); if (k >= 0) held |= 1u << k; }
		}
		for (int i = 0; i < n; i++) { int64_t t0 = now_us(); retro_run(); render_total += now_us() - t0; frames++; }
	}
	printf("frames %ld, avg frame %.2f ms, audio frames %ld\n", frames, render_total / 1000.0 / (frames ? frames : 1), audio_frames);
	return 0;
}
