/* Checks music resume (paused at a position) and volume levelling (a quiet
 * and a loud song come out at similar levels).  usage: music_level_test DIR */
#include "../src/music.h"
#include "../src/plat.h"
#include "../src/util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double rms_db(const char *raw, double from_s, double secs) {
	FILE *f = fopen(raw, "rb");
	if (!f) return -999;
	fseek(f, (long)(from_s * 44100) * 4, SEEK_SET);
	long n = (long)(secs * 44100) * 2;
	short *b = malloc(sizeof(short) * (size_t)n);
	long got = (long)fread(b, 2, (size_t)n, f);
	fclose(f);
	double ss = 0;
	for (long i = 0; i < got; i++) ss += (b[i] / 32768.0) * (b[i] / 32768.0);
	free(b);
	return got ? 10 * log10(ss / got + 1e-12) : -999;
}
static double second_at(const char *raw, double skip_s) {
	FILE *f = fopen(raw, "rb");
	if (!f) return -1;
	static short b[44100 * 2];
	fseek(f, (long)(skip_s * 44100) * 4, SEEK_SET);
	size_t n = fread(b, 4, 8820, f);
	fclose(f);
	int zc = 0;
	for (size_t i = 1; i < n; i++) if ((b[(i - 1) * 2] < 0) != (b[i * 2] < 0)) zc++;
	return (zc / 2.0 / (n / 44100.0) - 200) / 10.0;
}

int main(int argc, char **argv) {
	const char *d = argv[1];
	char cmd[1024], p1[600], p2[600], out[600];
	snprintf(cmd, sizeof cmd, "n=$(ls %s/out | wc -l); cat > %s/out/$n.raw", d, d);
	setenv("CLOVE_AUDIO_CMD", cmd, 1);
	setenv("CLOVE_AUDIO_PACE", "1", 1);
	snprintf(g_paths.clove, sizeof g_paths.clove, "%s", d);
	music_init(100, 0, 0);
	/* 1. resume paused at 60 s, then play */
	snprintf(p1, sizeof p1, "%s/tone.mp3", d);
	char *l1[1] = {p1};
	music_resume(l1, 1, 0, 60);
	usleep(800000);
	MusicInfo m;
	music_info(&m);
	printf("resumed: pos %.1f paused %d\n", m.pos, m.paused);
	music_toggle_pause();
	usleep(1200000);
	music_info(&m);
	printf("playing: pos %.1f\n", m.pos);
	char state[600];
	snprintf(state, sizeof state, "%s/state.tsv", d);
	music_save_state(state);
	music_stop();
	usleep(300000);
	/* 2. levelling: quiet then loud song, 8 s each */
	snprintf(p1, sizeof p1, "%s/quiet.wav", d);
	snprintf(p2, sizeof p2, "%s/loud.wav", d);
	char *l2[1] = {p1};
	music_play(l2, 1, 0);
	usleep(8000000);
	music_stop();
	usleep(300000);
	char *l3[1] = {p2};
	music_play(l3, 1, 0);
	usleep(8000000);
	music_stop();
	usleep(300000);
	music_shutdown();
	for (int i = 0; i < 6; i++) {
		snprintf(out, sizeof out, "%s/out/%d.raw", d, i);
		if (access(out, R_OK)) break;
		printf("player %d: second %.1f, level at 1 s %.1f dB, at 6 s %.1f dB\n", i, second_at(out, 0.05), rms_db(out, 1, 0.5), rms_db(out, 6, 1));
	}
	printf("--- state file:\n");
	snprintf(cmd, sizeof cmd, "cat %s; echo; cat %s/loudness.tsv", state, d);
	return system(cmd);
}
