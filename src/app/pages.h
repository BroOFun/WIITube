/* What the browse screens show and how it gets there: loading YouTube lists in the
 * background (one page at a time, merged on the main thread), the watch history behind
 * "For you", and the browse actions that change a page. Portable: the Wii app and the
 * desktop app simulation (tests/app_sim.c, run against real YouTube on GitHub Actions)
 * use exactly this code. */
#ifndef WIITUBE_PAGES_H
#define WIITUBE_PAGES_H

#include "../ui/ui.h"

/* Load the watch history (path may be NULL: no history file). */
void pages_init(const char *history_path);
/* After browse_init: Home opens on "For you" when something has been watched. */
void pages_start(browse_state *b);
/* Every frame (also during playback): merge a finished page, then start loading what the
 * current page wants (browse_update sets want_more). */
void pages_tick(browse_state *b);
/* Handle a browse action that changes a page (ACT_CHIP). Returns 1 if it was handled. */
int pages_handle(browse_state *b, int act);
/* Show search results for text (from the keyboard). */
void pages_search(browse_state *b, const char *text);
/* After watching something: the "For you" chip appears once a video has been watched. */
void pages_after_play(browse_state *b);
/* 1 while a page is being loaded. */
int pages_loading(void);
/* Wait for a page that is still loading (before exiting). */
void pages_shutdown(void);

/* Watch history, newest first (sd:/apps/wiitube/history.txt on the Wii). */
void history_add(const char *id, int is_short);
const char *history_last_short(void); /* NULL if no Short has been watched */
int history_has_videos(void);

#endif
