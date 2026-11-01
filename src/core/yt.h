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
    int is_short;
    int shelf;          /* > 0: one of the Shorts in shelf number `shelf` of its list */
} yt_search_result;

/* Endless result lists: the first page plus every page loaded since (YouTube's
 * "continuation" tokens), in YouTube's order. Shorts shelves are runs of entries with the
 * same shelf number; Shorts can also appear on their own. */
enum {
    YT_FEED_VIDEOS, YT_FEED_SHORTS, YT_FEED_ALL, /* search: Videos filter, Shorts filter, normal */
    YT_FEED_RELATED, /* YouTube's "Up next" recommendations; query = watched video IDs, newest first */
    YT_FEED_REELS,   /* YouTube's Shorts feed; query = the Short to continue from */
};
#define YT_FEED_MAX 600 /* stop growing a list beyond this many entries */

typedef struct {
    yt_search_result *items;
    int n, cap;
    int nshorts;              /* how many entries are Shorts */
    int nshelves;             /* shelf numbers used so far */
    int pos;                  /* YT_FEED_RELATED: next watched video to get recommendations for */
    char *continuation;       /* token for the next page, NULL at the end */
    char query[128];
    int kind;
} yt_feed;

/* First page of a list (see the YT_FEED_* kinds). YT_FEED_ALL is YouTube's normal search:
 * videos with Shorts shelves, in YouTube's order. Clears f first.
 * Returns the number of entries added, or -1 on error. */
int yt_feed_start(yt_feed *f, const char *query, int kind, char *err, size_t errlen); /* query may be f->query */
/* Append the next page. Returns entries added (0 when there are no more pages), -1 on error. */
int yt_feed_more(yt_feed *f, char *err, size_t errlen);
void yt_feed_free(yt_feed *f);
/* For loading the next page in another thread: dst becomes an empty feed with src's query,
 * kind and continuation token. yt_feed_more(dst) then fetches only the new page, and
 * yt_feed_merge(f, dst) appends it to f (skipping repeats) and frees dst. */
int yt_feed_cursor(yt_feed *dst, const yt_feed *src);
int yt_feed_merge(yt_feed *dst, yt_feed *src);
/* Append one entry, skipping repeats. */
void yt_feed_add(yt_feed *f, const yt_search_result *r);
/* Parse a saved response into f (tests). */
enum { YT_PAGE_SEARCH, YT_PAGE_SEARCH_MORE, YT_PAGE_RELATED, YT_PAGE_REELS };
int yt_feed_parse_json(yt_feed *f, const char *json, int page);

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
    long long init_end;               /* last byte of the init segment (ftyp+moov), -1 if unknown */
    long long index_start, index_end; /* the sidx box (seek index), -1 if unknown */
    char audio_id[24];                /* audio track ("en.4"), empty for single-language videos */
    char audio_name[64];              /* "English (United States) original" */
    int audio_default, is_drc;
} yt_format;

typedef struct {
    char lang[16];   /* "en", "de-DE" */
    char name[64];   /* "English (auto-generated)" */
    int asr;         /* automatic captions */
    char *url;       /* timedtext base URL (malloc'd) */
} yt_caption_track;

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
    yt_caption_track *captions;
    int ncaptions;
} yt_video;

/* Seek index of one stream (from its sidx box): subsegment i starts at time start[i]
 * (seconds) and byte offset[i]. */
typedef struct {
    int n;
    double *start;
    long long *offset;
} yt_index;

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

/* Pick H.264 video (short side <= max_height, so vertical Shorts work too) and AAC audio.
 * A muxed format may be returned as *video with *audio == NULL. Returns 0 on success. */
int yt_pick_formats(const yt_video *v, int max_height, const yt_format **video, const yt_format **audio);
/* Same, with a chosen audio track (NULL: English if available, else the default track). */
int yt_pick_formats2(const yt_video *v, int max_height, const char *audio_id, const yt_format **video,
                     const yt_format **audio);
/* Distinct audio tracks (dubs) of a video; returns how many (0 for single-language videos). */
int yt_audio_tracks(const yt_video *v, const yt_format **out, int max);

/* Download and parse the seek index of format f. Returns 0 on success. */
int yt_fetch_index(const yt_video *v, const yt_format *f, yt_index *idx, char *err, size_t errlen);
/* Subsegment that contains time t (the last one starting at or before t). */
int yt_index_find(const yt_index *idx, double t);
void yt_index_free(yt_index *idx);
/* Parse a sidx box; base = file offset of the byte after the box. (Exposed for tests.) */
int yt_parse_sidx(const unsigned char *box, size_t len, long long base, yt_index *idx);

/* Download a format, passing bytes to cb. Uses 10 MiB range requests like yt-dlp.
 * Returns 0 on success. */
int yt_download(const yt_video *v, const yt_format *f, http_body_cb cb, void *ud, char *err, size_t errlen);
/* Same, from a stream URL; total is the content length (or -1). */
int yt_download_url(const char *url, long long total, const char *user_agent, http_body_cb cb, void *ud,
                    char *err, size_t errlen);
/* Download bytes [start, end) of a stream URL (end = content length) in range chunks. */
int yt_download_span(const char *url, long long start, long long end, const char *user_agent, http_body_cb cb,
                     void *ud, char *err, size_t errlen);

/* Load rules from a JSON string (e.g. downloaded at startup). source is a label for logs. */
int yt_load_rules_json(const char *json, const char *source, char *err, size_t errlen);

/* Raw innertube request (diagnostics): body_json without "context"; returns the response
 * as JSON text (caller frees) or NULL. */
char *yt_raw(const char *endpoint, const char *client_key, const char *body_json, char *err, size_t errlen);

/* Extract an 11-character video ID from an ID or a youtube.com / youtu.be URL. 0 on success. */
int yt_parse_video_id(const char *in, char out[16]);

#endif
