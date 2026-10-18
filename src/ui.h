/* Clove — screens, navigation and drawing. */
#ifndef CLOVE_UI_H
#define CLOVE_UI_H

#include "plat.h"

enum { UI_RUN, UI_LAUNCH, UI_QUIT, UI_RESTART, UI_FALLBACK };

void ui_init(void);
void ui_event(const InputEvent *ev);
int ui_update(void);    /* 1 while something is animating */
int ui_dirty(void);
void ui_render(void);
int ui_state(void);     /* UI_RUN / UI_LAUNCH / UI_QUIT / UI_RESTART */
int ui_wait_ms(void);   /* how long the main loop may sleep */
void ui_notice(const char *msg); /* a longer toast, e.g. after sorting Import */
void ui_save_music(void); /* remembers the song and where in it (Settings > Music) */
int ui_tab(void);          /* the section showing: Recent, Favorites, Consoles... (see ui.c) */

#endif
