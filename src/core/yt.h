/* YouTube "innertube" API client: search, stream lookup, format selection, download. */
#ifndef WIITUBE_YT_H
#define WIITUBE_YT_H

#include <stddef.h>
#include "http.h"

typedef struct {
    char video_id[16];
    char title[256];
    char channel[128];
    char duration[16]; /* "4:13" as shown by YouTube; empty for live */
    char views[48];
    char published[32]; /* "5 months ago" */
} yt_search_result;

typedef struct {
    int itag;
    char mime[48];    /* "video/mp4" */
    char codecs[64];  /* "avc1.4d400c" */
    int width, height, fps;
    int bitrate;
    long long content_length; /* -1 if unknown */
    int has_video, has_audio;
    int ciphered;     /* URL needs signature/n solving (JS); not usable yet */
    char *url;        /* malloc'd; NULL when ciphered */
} yt_format;

typedef struct {
    char video_id[16];
    char title[256];
    char channel[128];
    int length_seconds;
    char status[32];   /* playabilityStatus.status, "OK" when playable */
    char reason[256];
    char client[32];   /* rules client key that produced this info */
    char user_agent[256];
    yt_format *formats;
    int nformats;
    char *hls_url;     /* malloc'd or NULL */
    int sabr_only;     /* server returned only SABR streaming (unsupported) */
} yt_video;

/* Load rules (client identities etc.) from a JSON file; NULL or missing file uses the built-in copy.
 * Returns 0 on success. */
int yt_load_rules(const char *path, char *err, size_t errlen);
const char *yt_rules_source(void);

/* Search YouTube. Fills up to max results. Returns number of results, or -1 on error. */
int yt_search(const char *query, yt_search_result *out, int max, char *err, size_t errlen);

/* Fetch playable streams for a video ID, trying each configured player client in turn.
 * Returns 0 when at least one directly usable format was found. Free with yt_video_free(). */
int yt_get_video(const char *video_id, yt_video *out, char *err, size_t errlen);
void yt_video_free(yt_video *v);

/* Pick H.264 video (height <= max_height) and AAC audio. A muxed format may be returned as
 * *video with *audio == NULL. Returns 0 on success. */
int yt_pick_formats(const yt_video *v, int max_height, const yt_format **video, const yt_format **audio);

/* Download a format, passing bytes to cb. Uses 10 MiB range requests like yt-dlp.
 * Returns 0 on success. */
int yt_download(const yt_video *v, const yt_format *f, http_body_cb cb, void *ud, char *err, size_t errlen);
/* Same, from a stream URL; total is the content length (or -1). */
int yt_download_url(const char *url, long long total, const char *user_agent, http_body_cb cb, void *ud,
                    char *err, size_t errlen);

/* Load rules from a JSON string (e.g. downloaded at startup). source is a label for logs. */
int yt_load_rules_json(const char *json, const char *source, char *err, size_t errlen);

/* Extract an 11-character video ID from an ID or a youtube.com / youtu.be URL. 0 on success. */
int yt_parse_video_id(const char *in, char out[16]);

#endif
