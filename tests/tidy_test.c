#include <stdio.h>
#include <string.h>
#include "../src/musiclib.h"
#include "../src/util.h"
int main(int argc, char **argv) {
	char line[1024];
	while (fgets(line, sizeof line, stdin)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0]) continue;
		char stem[512], t[256], a[256];
		path_stem(line, stem, sizeof stem);
		lib_tidy_name(stem, t, sizeof t, a, sizeof a);
		printf("%-22s | %s\n", a, t);
	}
	return 0;
}
