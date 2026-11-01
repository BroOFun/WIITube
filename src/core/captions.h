/* Closed captions: YouTube timedtext tracks in "json3" form. */
#ifndef WIITUBE_CAPTIONS_H
#define WIITUBE_CAPTIONS_H

#include <stddef.h>
#include "yt.h"

typedef struct {
    int start_ms, end_ms;
    char *text; /* UTF-8, may contain '\n' */
} cc_cue;

typedef struct {
    cc_cue *cues; /* sorted by start */
    int n;
    char label[80];
} cc_track;

/* Index of the track to use: English (a real track over auto-generated), otherwise -1 and
 * *translate is set to the index of a track YouTube can translate to English. */
int cc_pick_english(const yt_video *v, int *translate);

/* Download track number i of v; tlang (e.g. "en") asks YouTube to translate it. */
int cc_fetch(const yt_video *v, int i, const char *tlang, cc_track *out, char *err, size_t errlen);
/* Parse json3 text (exposed for tests). */
int cc_parse_json3(const char *json, cc_track *out);
/* The caption text shown at time t (seconds): the newest cue that has started and not
 * ended (with the one before it if both are on screen), or NULL. Returns a pointer into
 * buf. */
const char *cc_text_at(const cc_track *t, double sec, char *buf, size_t buflen);
void cc_free(cc_track *t);

#endif
