/* Parsing tests against real YouTube responses saved by "wiitube-cli dump" (tests/fixtures):
 * feeds with Shorts and next-page tokens, the seek index (sidx), and captions (json3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../src/core/captions.h"
#include "../src/core/yt.h"

static int failures;
#define CHECK(cond, ...)                  \
    do {                                  \
        if (!(cond)) {                    \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                 \
            failures++;                   \
        }                                 \
    } while (0)

static char *read_gz(const char *path)
{
    gzFile f = gzopen(path, "rb");
    size_t cap = 1 << 20, len = 0;
    char *buf = malloc(cap);
    int n;
    if (!f || !buf)
        return NULL;
    while ((n = gzread(f, buf + len, (unsigned)(cap - len - 1))) > 0) {
        len += (size_t)n;
        if (cap - len < 2) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    gzclose(f);
    buf[len] = 0;
    return buf;
}

static unsigned char *read_bin(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b = malloc(1 << 16);
    *len = f ? fread(b, 1, 1 << 16, f) : 0;
    if (f)
        fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/fixtures";
    char path[512], *json, buf[512];
    yt_feed f;
    yt_index idx;
    cc_track cc;
    unsigned char *bin;
    size_t len;
    int i;

    /* Videos filter: 20 videos, no Shorts, a next-page token. */
    memset(&f, 0, sizeof f);
    snprintf(path, sizeof path, "%s/search_videos.json.gz", dir);
    json = read_gz(path);
    CHECK(json && yt_feed_parse_json(&f, json, YT_PAGE_SEARCH) == 0, "parse search_videos");
    printf("videos page: %d videos, %d shorts, token %s\n", f.n, f.nshorts, f.continuation ? "yes" : "no");
    CHECK(f.n >= 20, "expected >= 20 videos, got %d", f.n);
    CHECK(f.continuation && strlen(f.continuation) > 50, "missing continuation token");
    CHECK(f.items[0].video_id[0] && f.items[0].title[0] && f.items[0].channel[0] && f.items[0].duration[0],
          "first video incomplete");
    printf("  first: %s | %s | %s | %s | %s\n", f.items[0].video_id, f.items[0].title, f.items[0].channel,
           f.items[0].views, f.items[0].published);
    free(json);
    yt_feed_free(&f);

    /* YouTube's normal search: videos and Shorts shelves, kept in YouTube's order. */
    snprintf(path, sizeof path, "%s/search_all.json.gz", dir);
    json = read_gz(path);
    CHECK(json && yt_feed_parse_json(&f, json, YT_PAGE_SEARCH) == 0, "parse search_all");
    printf("normal search page: %d entries, %d Shorts in %d shelves, token %s\n  order:", f.n, f.nshorts,
           f.nshelves, f.continuation ? "yes" : "no");
    for (i = 0; i < f.n; i++) {
        if (f.items[i].shelf && (i == 0 || f.items[i - 1].shelf != f.items[i].shelf)) {
            int k = i;
            while (k < f.n && f.items[k].shelf == f.items[i].shelf)
                k++;
            printf(" [Shorts x%d]", k - i);
        } else if (!f.items[i].shelf) {
            printf(" %s", f.items[i].is_short ? "s" : "v");
        }
    }
    printf("\n");
    CHECK(f.nshorts >= 5 && f.nshelves >= 1, "expected Shorts shelves, got %d Shorts in %d", f.nshorts, f.nshelves);
    CHECK(f.n > f.nshorts, "expected regular videos too");
    for (i = 0; i < f.n; i++)
        CHECK(!f.items[i].shelf || f.items[i].is_short, "a regular video was put in a Shorts shelf");
    CHECK(f.items[0].shelf > 0, "this page starts with a Shorts shelf on YouTube");
    {
        int first_video = 0;
        while (first_video < f.n && f.items[first_video].is_short)
            first_video++;
        CHECK(first_video < f.n && f.items[first_video].title[0] && f.items[first_video].channel[0],
              "video without title/channel");
        if (first_video < f.n)
            printf("  first video: %s | %s | %s\n", f.items[first_video].video_id, f.items[first_video].title,
                   f.items[first_video].channel);
        printf("  first Short: %s | %s | %s\n", f.items[0].video_id, f.items[0].title, f.items[0].views);
    }
    free(json);

    /* The next page appends, with its own shelf numbers. */
    {
        int before = f.n, shelves = f.nshelves;
        yt_feed page;
        memset(&page, 0, sizeof page);
        snprintf(path, sizeof path, "%s/search_cont.json.gz", dir);
        json = read_gz(path);
        CHECK(json && yt_feed_parse_json(&page, json, YT_PAGE_SEARCH_MORE) == 0, "parse search_cont");
        yt_feed_merge(&f, &page);
        printf("after next page: %d entries (+%d), %d shelves (+%d), token %s\n", f.n, f.n - before, f.nshelves,
               f.nshelves - shelves, f.continuation ? "yes" : "no");
        CHECK(f.n > before, "continuation added nothing");
        CHECK(f.continuation != NULL, "continuation page has no next token");
        for (i = before; i < f.n; i++)
            CHECK(!f.items[i].shelf || f.items[i].shelf > shelves, "shelf numbers of the new page clash");
        free(json);
    }
    yt_feed_free(&f);

    /* YouTube's "Up next" recommendations (the For you feed). The seed itself is dropped. */
    snprintf(path, sizeof path, "%s/next_rick.json.gz", dir);
    json = read_gz(path);
    snprintf(f.query, sizeof f.query, "dQw4w9WgXcQ");
    f.kind = YT_FEED_RELATED;
    CHECK(json && yt_feed_parse_json(&f, json, YT_PAGE_RELATED) == 0, "parse next_rick");
    printf("up next: %d videos, token %s\n", f.n, f.continuation ? "yes" : "no");
    CHECK(f.n >= 15, "expected recommendations, got %d", f.n);
    CHECK(f.continuation != NULL, "recommendations have no next page");
    for (i = 0; i < f.n; i++)
        CHECK(strcmp(f.items[i].video_id, "dQw4w9WgXcQ") != 0, "the watched video is recommended");
    if (f.n > 0) {
        printf("  first: %s | %s | %s | %s | %s | %s\n", f.items[0].video_id, f.items[0].title, f.items[0].channel,
               f.items[0].views, f.items[0].published, f.items[0].duration);
        CHECK(f.items[0].title[0] && f.items[0].channel[0] && f.items[0].views[0] && f.items[0].duration[0],
              "recommendation without title/channel/views/duration");
    }
    free(json);
    yt_feed_free(&f);

    /* YouTube's Shorts feed (reel_watch_sequence). */
    snprintf(path, sizeof path, "%s/reels.json.gz", dir);
    json = read_gz(path);
    CHECK(json && yt_feed_parse_json(&f, json, YT_PAGE_REELS) == 0, "parse reels");
    printf("Shorts feed: %d Shorts\n", f.n);
    CHECK(f.n >= 5 && f.nshorts == f.n, "expected Shorts, got %d of %d", f.nshorts, f.n);
    free(json);
    yt_feed_free(&f);

    /* Seek index: video subsegments ~5 s, audio ~10 s; offsets start right after the sidx. */
    snprintf(path, sizeof path, "%s/rick_video_head.bin", dir);
    bin = read_bin(path, &len);
    CHECK(yt_parse_sidx(bin + 741, len - 741, 1229, &idx) == 0, "parse video sidx");
    printf("video index: %d subsegments, first at %lld, 3rd starts %.2f s at %lld, ends %.2f s\n", idx.n,
           idx.offset[0], idx.start[2], idx.offset[2], idx.start[idx.n]);
    CHECK(idx.n == 38 && idx.offset[0] == 1229 && idx.offset[1] == 1229 + 327841, "video sidx values");
    CHECK(idx.start[idx.n] > 212 && idx.start[idx.n] < 214, "video duration %.2f", idx.start[idx.n]);
    CHECK(yt_index_find(&idx, 0) == 0 && yt_index_find(&idx, 6.0) == 1 && yt_index_find(&idx, 1000) == idx.n - 1,
          "index lookup");
    yt_index_free(&idx);
    free(bin);
    snprintf(path, sizeof path, "%s/rick_audio_head.bin", dir);
    bin = read_bin(path, &len);
    CHECK(yt_parse_sidx(bin + 732, len - 732, 1028, &idx) == 0 && idx.n == 22, "parse audio sidx");
    yt_index_free(&idx);
    free(bin);

    /* Captions. */
    snprintf(path, sizeof path, "%s/rick_captions.json3.gz", dir);
    json = read_gz(path);
    CHECK(json && cc_parse_json3(json, &cc) == 0, "parse captions");
    printf("captions: %d cues; at 20 s: \"%s\"\n", cc.n, cc_text_at(&cc, 20.0, buf, sizeof buf));
    CHECK(cc.n > 40, "too few cues");
    CHECK(cc_text_at(&cc, 20.0, buf, sizeof buf) && strstr(buf, "strangers"), "wrong cue at 20 s");
    CHECK(cc_text_at(&cc, 10.0, buf, sizeof buf) == NULL, "cue shown in a gap");
    cc_free(&cc);
    free(json);

    printf(failures ? "%d FAILURE(S)\n" : "all YouTube data tests passed\n", failures);
    return failures != 0;
}
