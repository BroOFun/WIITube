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
    CHECK(json && yt_feed_parse_json(&f, json, 0) == 0, "parse search_videos");
    printf("videos page: %d videos, %d shorts, token %s\n", f.n, f.nshorts, f.continuation ? "yes" : "no");
    CHECK(f.n >= 20, "expected >= 20 videos, got %d", f.n);
    CHECK(f.continuation && strlen(f.continuation) > 50, "missing continuation token");
    CHECK(f.items[0].video_id[0] && f.items[0].title[0] && f.items[0].channel[0] && f.items[0].duration[0],
          "first video incomplete");
    printf("  first: %s | %s | %s | %s | %s\n", f.items[0].video_id, f.items[0].title, f.items[0].channel,
           f.items[0].views, f.items[0].published);
    free(json);
    yt_feed_free(&f);

    /* Unfiltered search: videos plus a Shorts shelf with vertical thumbnails. */
    snprintf(path, sizeof path, "%s/search_all.json.gz", dir);
    json = read_gz(path);
    CHECK(json && yt_feed_parse_json(&f, json, 0) == 0, "parse search_all");
    printf("mixed page: %d videos, %d shorts, token %s\n", f.n, f.nshorts, f.continuation ? "yes" : "no");
    CHECK(f.nshorts >= 5, "expected Shorts, got %d", f.nshorts);
    CHECK(f.nshorts > 0 && strstr(f.shorts[0].thumb_url, "i.ytimg.com"), "Short without vertical thumbnail");
    if (f.nshorts > 0)
        printf("  first short: %s | %s | %s | %.60s\n", f.shorts[0].video_id, f.shorts[0].title, f.shorts[0].views,
               f.shorts[0].thumb_url);
    free(json);

    /* Continuation page appends. */
    {
        int before = f.n + f.nshorts;
        snprintf(path, sizeof path, "%s/search_cont.json.gz", dir);
        json = read_gz(path);
        CHECK(json && yt_feed_parse_json(&f, json, 1) == 0, "parse search_cont");
        printf("after next page: %d videos, %d shorts (+%d), token %s\n", f.n, f.nshorts,
               f.n + f.nshorts - before, f.continuation ? "yes" : "no");
        CHECK(f.n + f.nshorts > before, "continuation added nothing");
        CHECK(f.continuation != NULL, "continuation page has no next token");
        free(json);
    }
    for (i = 0; i < f.n; i++)
        CHECK(!f.items[i].is_short, "a Short ended up in the video list");
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
