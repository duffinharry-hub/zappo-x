/* Zappo — small gaps in the WebAssembly C library, filled for the browser
 * build only (passed with -include by `make web`). */
#ifndef ZAPPO_WEB_COMPAT_H
#define ZAPPO_WEB_COMPAT_H
#ifdef __wasm__
static inline void sync(void) {} /* the page's file system has nothing to flush */
#endif
#endif
