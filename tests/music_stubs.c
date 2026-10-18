/* the few things the music player borrows from the rest of Clove */
#include "../src/plat.h"
#include "../src/gfx.h"
Paths g_paths;
Image *img_load(const char *p) { (void)p; return 0; }
Image *img_load_mem(const unsigned char *d, int n) { (void)d; (void)n; return 0; }
