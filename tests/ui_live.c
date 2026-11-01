/* Live preview: real YouTube search results and real thumbnails (fetched with the same
 * code the Wii uses) rendered with the software UI backend. Needs access to YouTube, so it
 * runs on GitHub Actions. usage: ui_live "QUERY" OUT_DIR [rules.json] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/http.h"
#include "../src/core/thread.h"
#include "../src/core/thumbs.h"
#include "../src/core/yt.h"
#include "../src/platform/linux/soft_draw.h"
#include "../src/ui/ui.h"

extern const int logo_small_w, logo_small_h, logo_splash_w, logo_splash_h;
extern const unsigned char logo_small_rgba[], logo_splash_rgba[];

static thumbs *g_thumbs;

/* Shorts use the normal 16:9 thumbnail, cropped to 9:16 when drawn: the vertical picture
 * URLs YouTube lists (oardefault/oar2 with signed parameters) are served as WebP, which
 * stb_image can't decode, and the plain oar2.jpg is 1080x1920 (too big to decode on a Wii). */
static const wt_image *get_thumb(void *ud, const yt_search_result *r, int vertical)
{
    (void)ud;
    (void)vertical;
    return thumbs_get(g_thumbs, r->video_id);
}

static void save(const char *dir, const char *name, int width)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s-%d.ppm", dir, name, width);
    soft_save_ppm(path);
}

/* Keep "drawing frames" until the thumbnails asked for have arrived (max ~30 s). */
static void draw_until_loaded(browse_state *b, int width)
{
    int tries, before = 0;
    for (tries = 0; tries < 300; tries++) {
        soft_begin(width);
        browse_draw(b, get_thumb, NULL);
        if (thumbs_frame_done(g_thumbs))
            before = tries;
        if (tries - before > 30) /* nothing new for 3 s */
            break;
        wt_sleep_ms(100);
    }
}


/* How many visible entries have thumbnails (videos or Shorts). */
static int count_loaded(const yt_search_result *r, int n, int vertical, int *total)
{
    int i, got = 0;
    *total = n;
    for (i = 0; i < n; i++)
        if (get_thumb(NULL, &r[i], vertical))
            got++;
    return got;
}

/* Turn an RGB image into a planar BT.709 YUV frame (what the decoder produces), so the
 * player screen exercises the same GPU colour path as real video. */
static yuv_frame *to_yuv(const wt_image *img)
{
    static yuv_frame f;
    uint8_t *y, *u, *v, *ty, *tu, *tv;
    int x, yy;
    f.w = img->w;
    f.h = img->h;
    f.yw = conv_i8_w(f.w);
    f.yh = conv_i8_h(f.h);
    f.cw = (f.w + 1) / 2;
    f.ch = (f.h + 1) / 2;
    f.cwt = conv_i8_w(f.cw);
    f.cht = conv_i8_h(f.ch);
    f.bt709 = 1;
    y = malloc((size_t)f.w * f.h);
    u = malloc((size_t)f.cw * f.ch);
    v = malloc((size_t)f.cw * f.ch);
    for (yy = 0; yy < f.h; yy++)
        for (x = 0; x < f.w; x++) {
            uint8_t p[3];
            image_pixel(img, x, yy, p);
            y[yy * f.w + x] = (uint8_t)(16 + (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) * 219 / 255 + 0.5);
            if (!(x & 1) && !(yy & 1)) {
                u[(yy / 2) * f.cw + x / 2] = (uint8_t)(128 + (-0.1146 * p[0] - 0.3854 * p[1] + 0.5 * p[2]) * 224 / 255 + 0.5);
                v[(yy / 2) * f.cw + x / 2] = (uint8_t)(128 + (0.5 * p[0] - 0.4542 * p[1] - 0.0458 * p[2]) * 224 / 255 + 0.5);
            }
        }
    ty = malloc((size_t)f.yw * f.yh);
    tu = malloc((size_t)f.cwt * f.cht);
    tv = malloc((size_t)f.cwt * f.cht);
    conv_plane_to_i8(y, f.w, f.w, f.h, ty);
    conv_plane_to_i8(u, f.cw, f.cw, f.ch, tu);
    conv_plane_to_i8(v, f.cw, f.cw, f.ch, tv);
    free(y);
    free(u);
    free(v);
    f.y = ty;
    f.u = tu;
    f.v = tv;
    return &f;
}

int main(int argc, char **argv)
{
    static browse_state b;
    const char *query = argc > 1 ? argv[1] : "wii sports";
    const char *out = argc > 2 ? argv[2] : ".";
    char err[512];
    int widths[2] = { 640, 854 }, wi, failures = 0, n, got, total;
    wt_image *logo, *splash, *frame = NULL;
    yt_feed results, more, foryou, reels;
    int first_video = 0;

    if (yt_load_rules(argc > 3 ? argv[3] : "rules/youtube.json", err, sizeof err) != 0 ||
        http_global_init(err, sizeof err) != 0) {
        fprintf(stderr, "setup failed: %s\n", err);
        return 1;
    }
    logo = image_from_rgba(logo_small_rgba, logo_small_w, logo_small_h);
    splash = image_from_rgba(logo_splash_rgba, logo_splash_w, logo_splash_h);
    memset(&results, 0, sizeof results);
    memset(&foryou, 0, sizeof foryou);
    memset(&reels, 0, sizeof reels);

    /* YouTube's normal search: videos with Shorts shelves, in YouTube's order. */
    n = yt_feed_start(&results, query, YT_FEED_ALL, err, sizeof err);
    if (n <= 0) {
        fprintf(stderr, "search failed: %s\n", n < 0 ? err : "no results");
        return 1;
    }
    printf("search \"%s\": %d entries, %d Shorts in %d shelves, more: %s\n", query, results.n, results.nshorts,
           results.nshelves, results.continuation ? "yes" : "no");
    if (results.nshorts == 0)
        failures++;
    /* Second page, loaded the way the Wii app does it (cursor + merge). */
    if (yt_feed_cursor(&more, &results) == 0 && yt_feed_more(&more, err, sizeof err) >= 0) {
        int added = yt_feed_merge(&results, &more);
        printf("next page: +%d (now %d entries, %d shelves), more: %s\n", added, results.n, results.nshelves,
               results.continuation ? "yes" : "no");
        if (added <= 0)
            failures++;
    } else {
        printf("next page failed: %s\n", err);
        failures++;
    }
    while (first_video < results.n && results.items[first_video].is_short)
        first_video++;

    /* "For you": YouTube's recommendations for a watched video, two pages. */
    n = yt_feed_start(&foryou, "dQw4w9WgXcQ", YT_FEED_RELATED, err, sizeof err);
    printf("for you: %d videos%s%s\n", foryou.n, n < 0 ? " - " : "", n < 0 ? err : "");
    if (n > 0 && yt_feed_cursor(&more, &foryou) == 0 && yt_feed_more(&more, err, sizeof err) >= 0)
        printf("for you, next page: +%d\n", yt_feed_merge(&foryou, &more));
    if (foryou.n < 10)
        failures++;

    /* The Shorts feed continuing from the first Short of the search. */
    {
        int i;
        for (i = 0; i < results.n && !results.items[i].is_short; i++)
            ;
        if (i < results.n) {
            n = yt_feed_start(&reels, results.items[i].video_id, YT_FEED_REELS, err, sizeof err);
            printf("Shorts feed after %s: %d Shorts%s%s\n", results.items[i].video_id, reels.n, n < 0 ? " - " : "",
                   n < 0 ? err : "");
            if (n > 0 && yt_feed_cursor(&more, &reels) == 0 && yt_feed_more(&more, err, sizeof err) >= 0)
                printf("Shorts feed, next page: +%d\n", yt_feed_merge(&reels, &more));
            if (reels.n < 3)
                failures++;
        }
    }
    g_thumbs = thumbs_new("https://i.ytimg.com/vi/%s/mqdefault.jpg", 160, 64, 2);

    for (wi = 0; wi < 2; wi++) {
        const int W = widths[wi], cols = W >= 800 ? 4 : 3;
        browse_page *pg;
        int i;
        soft_begin(W);
        browse_init(&b, logo);
        browse_set_page(&b, PAGE_SEARCH);
        pg = &b.pages[PAGE_SEARCH];
        snprintf(pg->query, sizeof pg->query, "%s", query);
        pg->feed.nshelves = results.nshelves;
        for (i = 0; i < results.n; i++)
            yt_feed_add(&pg->feed, &results.items[i]);
        pg->feed.continuation = strdup("more");
        pg->started = 1;
        draw_until_loaded(&b, W);
        save(out, "live-search", W);
        got = count_loaded(pg->feed.items, pg->feed.n < 5 ? pg->feed.n : 5, 1, &total);
        printf("width %d: %d of the first %d thumbnails loaded\n", W, got, total);
        if (got < total)
            failures++;

        /* The first regular videos. */
        browse_select(&b, first_video);
        draw_until_loaded(&b, W);
        save(out, "live-search-videos", W);
        got = count_loaded(pg->feed.items + first_video, pg->feed.n - first_video < cols ? pg->feed.n - first_video : cols,
                           0, &total);
        printf("width %d: %d of %d video thumbnails loaded\n", W, got, total);
        if (got < total)
            failures++;

        /* Home with "For you". */
        browse_set_page(&b, PAGE_HOME);
        b.has_for_you = 1;
        pg = &b.pages[PAGE_HOME];
        for (i = 0; i < foryou.n; i++)
            yt_feed_add(&pg->feed, &foryou.items[i]);
        pg->started = 1;
        draw_until_loaded(&b, W);
        save(out, "live-foryou", W);
    }

    /* Player screen using the first video's thumbnail (full size) as the picture. */
    {
        char url[128];
        http_request req;
        http_response resp;
        memset(&req, 0, sizeof req);
        snprintf(url, sizeof url, "https://i.ytimg.com/vi/%s/mqdefault.jpg", results.items[first_video].video_id);
        req.url = url;
        req.headers = "User-Agent: Mozilla/5.0\r\n";
        if (http_do(&req, &resp, err, sizeof err) == 0 && resp.status == 200)
            frame = image_from_jpeg(resp.body, resp.body_len, 0);
        http_response_free(&resp);
    }
    if (frame) {
        player_view p;
        float x, y, w, h;
        player_view_init(&p);
        snprintf(p.title, sizeof p.title, "%s", results.items[first_video].title);
        snprintf(p.channel, sizeof p.channel, "%s", results.items[first_video].channel);
        p.position = 83;
        p.duration = 600;
        p.buffered = 0.4f;
        p.volume = 192;
        p.quality = 480;
        p.can_seek = 1;
        p.cc_available = 1;
        soft_begin(854);
        dr_fill(0, 0, 854, UI_H, 0x000000ff);
        ui_fit(frame->w, frame->h, &x, &y, &w, &h);
        dr_video(to_yuv(frame), x, y, w, h);
        player_draw_overlay(&p);
        save(out, "live-player", 854);
        printf("thumbnail as frame: %dx%d\n", frame->w, frame->h);
    } else {
        printf("could not fetch the first thumbnail: %s\n", err);
        failures++;
    }
    {
        int opened, reused;
        http_stats(&opened, &reused);
        printf("connections: %d opened, %d reused\n", opened, reused);
    }
    (void)splash;
    return failures ? 1 : 0;
}
