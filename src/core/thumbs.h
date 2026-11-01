/* Thumbnail cache: downloads and decodes video thumbnails in background threads. */
#ifndef WIITUBE_THUMBS_H
#define WIITUBE_THUMBS_H

#include "image.h"

typedef struct thumbs thumbs;

/* url_fmt has one %s for the video ID, e.g. "https://i.ytimg.com/vi/%s/mqdefault.jpg".
 * Images wider than max_w are shrunk. capacity = how many thumbnails to keep. */
thumbs *thumbs_new(const char *url_fmt, int max_w, int capacity, int nthreads);
void thumbs_free(thumbs *t);

/* Main thread only. Returns the thumbnail if loaded, else NULL (and queues it; the most
 * recently requested thumbnails load first). Images stay valid until two frames after
 * they were last requested. */
const wt_image *thumbs_get(thumbs *t, const char *video_id);
/* Same for any image URL (Shorts' vertical thumbnails); key identifies it in the cache
 * (at most 23 characters) and max_w overrides the default size (0 = default). */
const wt_image *thumbs_get_url(thumbs *t, const char *key, const char *url, int max_w);
/* Call once per frame after drawing. Returns 1 if new thumbnails arrived since the last
 * call (the GPU texture cache must then be invalidated). */
int thumbs_frame_done(thumbs *t);

#endif
