#include <stdio.h>
#include <stdlib.h>
#include "../src/musiclib.h"
#include "../src/plat.h"
#include "../src/util.h"
int main(int argc, char **argv) {
	sncpy(g_paths.mmc, argv[1], sizeof g_paths.mmc);
	snfmt(g_paths.clove, sizeof g_paths.clove, "%s/CFW/clove", argv[1]);
	lib_scan();
	for (int i = 0; i < g_lib.na; i++) {
		MArtist *a = &g_lib.a[i];
		printf("%-24s [%s] %d\n", a->name, a->genre, a->n);
		for (int k = 0; k < a->n; k++) printf("      %s\n", g_lib.t[a->t[k]].title);
	}
	return 0;
}
