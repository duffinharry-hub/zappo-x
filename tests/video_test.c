#include <stdio.h>
#include "../src/video.h"
#include "../src/util.h"
int main(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		printf("%s: needs prep %d\n", argv[i], video_needs_prep(argv[i]));
		if (!video_needs_prep(argv[i])) continue;
		char err[256] = "";
		VideoPrep *p = video_prep_start(argv[i], err, sizeof err);
		if (!p) { printf("  start failed: %s\n", err); continue; }
		int r, steps = 0;
		while ((r = video_prep_step(p, 5, err, sizeof err)) == 0) steps++;
		printf("  result %d after %d steps %s\n", r, steps, err);
		printf("  needs prep now %d\n", video_needs_prep(argv[i]));
	}
	return 0;
}
