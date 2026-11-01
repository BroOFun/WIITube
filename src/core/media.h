/* Demux + decode one YouTube DASH stream (fragmented MP4) from a ring buffer with FFmpeg. */
#ifndef WIITUBE_MEDIA_H
#define WIITUBE_MEDIA_H

#include <stddef.h>
#include <stdint.h>
#include "ring.h"

typedef struct media media;

typedef struct {
    int width, height;         /* luma size */
    const uint8_t *plane[3];   /* Y, U, V (4:2:0) */
    int stride[3];
    int bt709;                 /* colour matrix: 1 = BT.709, 0 = BT.601 */
    double pts;                /* seconds */
} media_frame;

enum { MEDIA_VIDEO = 0, MEDIA_AUDIO = 1 };

/* Open the first video or audio stream found in the data read from r (blocks until the
 * header has arrived). Returns NULL with a message in err on failure. */
media *media_open(ring *r, int kind, char *err, size_t errlen);
void media_close(media *m);

int media_width(const media *m);
int media_height(const media *m);
int media_sample_rate(const media *m);
double media_duration(const media *m); /* seconds, 0 if unknown */

/* After opening a stream that starts at a seek point: the first timestamp should be about t
 * seconds; if the file's timestamps are relative instead, they are shifted to match. */
void media_expect_start(media *m, double t);

/* Trade quality for speed when decoding falls behind (call from the decoding thread):
 * 0 = full quality, 1 = skip H.264 deblocking (slightly blockier),
 * 2 = also skip non-reference frames (fewer frames shown). */
void media_set_skip(media *m, int level);

/* Decode the next video frame. Returns 1 with *out filled (valid until the next call),
 * 0 at end of stream, < 0 on error. */
int media_next_video(media *m, media_frame *out);

/* Decode the next chunk of audio as interleaved stereo signed 16-bit (native endian).
 * out must hold max_frames * 2 samples. Returns 1 with *frames and *pts set, 0 at end
 * of stream, < 0 on error. */
int media_next_audio(media *m, int16_t *out, int max_frames, int *frames, double *pts);

#endif
