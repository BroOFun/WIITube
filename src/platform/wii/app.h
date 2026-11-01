/* Shared parts of the Wii app (main.c: startup, input, browsing; player.c: playback). */
#ifndef WIITUBE_APP_H
#define WIITUBE_APP_H

#include "../../core/image.h"
#include "../../core/thread.h"
#include "../../core/yt.h"
#include "../../ui/ui.h"

#define APP_DIR "sd:/apps/wiitube"

extern volatile int g_quit;
extern int g_sd;
extern wt_image *g_logo, *g_splash;
extern unsigned g_frame;

/* Saved in APP_DIR/settings.txt. */
typedef struct {
    int quality;   /* 144/240/360/480 */
    int captions;  /* 1 = turn captions on when a video has them */
} app_settings;
extern app_settings g_settings;
void settings_save(void);

void read_input(ui_input *in);
unsigned free_kb(void);
void log_memory(const char *where);

/* Work on a background thread while the screen keeps animating. */
typedef struct {
    wt_thread *thread;
    volatile int done;
} job;
int job_start(job *j, void *(*fn)(void *), void *arg);
int job_running(job *j);
int job_collect(job *j); /* join a finished job; 1 if it was collected */

void draw_message(const char *title, const char *detail, int busy);
void error_screen(const char *title, const char *detail);

/* ---- watch history (main.c) ---- */
void history_add(const char *id, int is_short);
const char *history_last_short(void); /* NULL if no Short has been watched */

/* ---- player.c ---- */
typedef struct {
    const yt_feed *feed;     /* the list the video came from (NULL for the Shorts tab) */
    int index;               /* its entry */
    const char *shorts_from; /* Shorts tab: continue YouTube's Shorts feed from this Short (or NULL) */
    /* Called every frame during playback so page loading in the background continues. */
    void (*tick)(void *ud);
    void *tick_ud;
} play_request;

/* Play until the user goes back. A Short opens the vertical Shorts player, where Up/Down
 * move through YouTube's Shorts feed. Returns the entry of feed played last (or -1). */
int player_run(play_request *req);

#endif
