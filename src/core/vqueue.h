/* Video decode worker: decodes and converts frames in its own thread into a small queue of
 * GX textures, so slow frames don't stall the display loop. The display side asks which
 * texture to show for the current clock time. */
#ifndef WIITUBE_VQUEUE_H
#define WIITUBE_VQUEUE_H

#include <stddef.h>
#include <stdint.h>
#include "media.h"

typedef struct vqueue vqueue;

typedef double (*vq_clock_fn)(void *ud);           /* current playback time (s); < 0 = not started */
typedef void (*vq_flush_fn)(void *tex, size_t len); /* make CPU-written texture visible to the GPU */

/* Start decoding m (which must already be open). base_skip is the user's minimum skip level
 * (see media_set_skip). flush may be NULL. */
vqueue *vq_start(media *m, vq_clock_fn clock, void *clock_ud, vq_flush_fn flush, int base_skip);

/* Display side: returns 1 and sets *tex when a newer frame is due at time now; the texture
 * stays valid until the next call that returns 1. Returns 0 when nothing changed. */
int vq_poll(vqueue *q, double now, const uint16_t **tex);

int vq_ready(vqueue *q);    /* number of decoded frames waiting */
int vq_finished(vqueue *q); /* decoder reached the end and every frame was shown or dropped */
int vq_tex_width(const vqueue *q);  /* texture size (multiples of 4) */
int vq_tex_height(const vqueue *q);
int vq_frame_width(const vqueue *q);
int vq_frame_height(const vqueue *q);

typedef struct {
    int decoded, shown, late_drops, skip_level;
} vq_stats;
void vq_get_stats(vqueue *q, vq_stats *out);

/* Stop the worker and free the queue (does not close the media). */
void vq_stop(vqueue *q);

#endif
