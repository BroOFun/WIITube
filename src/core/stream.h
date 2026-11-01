/* Downloads one YouTube stream in a background thread into a ring buffer. */
#ifndef WIITUBE_STREAM_H
#define WIITUBE_STREAM_H

#include <stddef.h>
#include "ring.h"
#include "yt.h"

typedef struct yt_stream yt_stream;

/* Start downloading format f (the URL and user agent are copied). */
yt_stream *yt_stream_start(const yt_video *v, const yt_format *f, size_t buffer_size);
/* Same, but for playing from byte offset (a subsegment start from the seek index): the
 * stream's header (init segment) is sent first, then the data from offset on, so the
 * decoder sees a valid file that begins at the seek point. offset 0 = from the start. */
yt_stream *yt_stream_start_at(const yt_video *v, const yt_format *f, size_t buffer_size, long long offset);
ring *yt_stream_ring(yt_stream *s);
long long yt_stream_received(yt_stream *s);
/* File offset the download has reached (for the "buffered" part of the progress bar). */
long long yt_stream_position(yt_stream *s);
/* Copy of the error message once the download has failed ("" otherwise). */
void yt_stream_error(yt_stream *s, char *out, size_t outlen);
/* Abort, wait up to timeout_ms for the thread, and release. If the thread is stuck in a
 * network call it is left to finish on its own and frees everything itself. */
void yt_stream_stop(yt_stream *s, int timeout_ms);

#endif
