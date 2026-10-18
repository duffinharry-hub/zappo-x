/* Checks skipping inside songs: plays a test tone whose pitch marks each
 * second (200 Hz + 10 Hz per second), skips, and reads back the pitch of
 * the sound sent to the player after each jump.
 * usage: music_seek_test SONG OUTDIR */
#include "../src/music.h"
#include "../src/util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double second_at(const char *raw, double skip_s) {
	FILE *f = fopen(raw, "rb");
	if (!f) return -1;
	static short b[44100 * 2];
	fseek(f, (long)(skip_s * 44100) * 4, SEEK_SET);
	size_t n = fread(b, 4, 8820, f); /* 0.2 s */
	fclose(f);
	if (n < 4000) return -2;
	int zc = 0;
	for (size_t i = 1; i < n; i++) if ((b[(i - 1) * 2] < 0) != (b[i * 2] < 0)) zc++;
	double hz = zc / 2.0 / (n / 44100.0);
	return (hz - 200) / 10.0;
}

int main(int argc, char **argv) {
	if (argc < 3) return 2;
	char cmd[1024];
	snprintf(cmd, sizeof cmd, "n=$(ls %s | wc -l); cat > %s/$n.raw", argv[2], argv[2]);
	setenv("CLOVE_AUDIO_CMD", cmd, 1);
	setenv("CLOVE_AUDIO_PACE", "1", 1);
	music_init(100, 0, 0);
	char *list[1] = {argv[1]};
	music_play(list, 1, 0);
	usleep(700000);
	MusicInfo m;
	music_info(&m);
	printf("start: pos %.2f len %.2f\n", m.pos, m.len);
	music_seek(60);
	music_info(&m);
	printf("after R1 (shown at once): %.2f\n", m.pos);
	usleep(1500000);
	music_info(&m);
	printf("1.5 s later: %.2f\n", m.pos);
	music_seek(-10);
	music_seek(-10); /* two presses before the player catches up */
	usleep(1000000);
	music_info(&m);
	printf("after two lefts + 1 s: %.2f\n", m.pos);
	music_toggle_pause();
	music_seek(30);
	usleep(300000);
	music_info(&m);
	printf("paused, +30: %.2f paused=%d\n", m.pos, m.paused);
	music_toggle_pause();
	usleep(800000);
	music_seek(600); /* past the end: the queue ends */
	usleep(800000);
	music_info(&m);
	printf("past the end: active=%d\n", m.active);
	music_shutdown();
	for (int i = 0; i < 6; i++) {
		char p[600];
		snprintf(p, sizeof p, "%s/%d.raw", argv[2], i);
		if (access(p, R_OK)) break;
		printf("player %d starts at second %.1f\n", i, second_at(p, 0.05));
	}
	return 0;
}
