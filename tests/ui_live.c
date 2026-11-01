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
    yt_feed videos, shorts, more;

    if (yt_load_rules(argc > 3 ? argv[3] : "rules/youtube.json", err, sizeof err) != 0 ||
        http_global_init(err, sizeof err) != 0) {
        fprintf(stderr, "setup failed: %s\n", err);
        return 1;
    }
    logo = image_from_rgba(logo_small_rgba, logo_small_w, logo_small_h);
    splash = image_from_rgba(logo_splash_rgba, logo_splash_w, logo_splash_h);
    memset(&videos, 0, sizeof videos);
    memset(&shorts, 0, sizeof shorts);
    n = yt_feed_start(&videos, query, YT_FEED_VIDEOS, err, sizeof err);
    if (n <= 0) {
        fprintf(stderr, "search failed: %s\n", n < 0 ? err : "no results");
        return 1;
    }
    printf("videos \"%s\": %d videos, %d Shorts, more: %s\n", query, videos.n, videos.nshorts,
           videos.continuation ? "yes" : "no");
    /* Second page, loaded the way the Wii app does it (cursor + merge). */
    if (yt_feed_cursor(&more, &videos) == 0 && yt_feed_more(&more, err, sizeof err) >= 0) {
        int added = yt_feed_merge(&videos, &more);
        printf("next page: +%d (now %d videos), more: %s\n", added, videos.n, videos.continuation ? "yes" : "no");
        if (added <= 0)
            failures++;
    } else {
        printf("next page failed: %s\n", err);
        failures++;
    }
    n = yt_feed_start(&shorts, query, YT_FEED_SHORTS, err, sizeof err);
    printf("Shorts \"%s\": %d Shorts (%d with a vertical thumbnail URL), more: %s\n", query, shorts.nshorts,
           shorts.nshorts > 0 && shorts.shorts[0].thumb_url[0] ? shorts.nshorts : 0, shorts.continuation ? "yes" : "no");
    if (n <= 0)
        failures++;
    g_thumbs = thumbs_new("https://i.ytimg.com/vi/%s/mqdefault.jpg", 160, 64, 2);

    for (wi = 0; wi < 2; wi++) {
        const int W = widths[wi];
        browse_page *pg;
        int i;
        soft_begin(W);
        browse_init(&b, logo);
        browse_set_page(&b, PAGE_SEARCH);
        pg = &b.pages[PAGE_SEARCH];
        snprintf(pg->query, sizeof pg->query, "%s", query);
        for (i = 0; i < videos.n; i++)
            yt_feed_add(&pg->videos, &videos.items[i]);
        for (i = 0; i < shorts.nshorts; i++)
            yt_feed_add(&pg->shorts, &shorts.shorts[i]);
        pg->started = 3;
        pg->col = 1;
        draw_until_loaded(&b, W);
        save(out, "live-home", W);
        got = count_loaded(pg->videos.items, pg->videos.n < 2 * (W >= 800 ? 4 : 3) ? pg->videos.n : 2 * (W >= 800 ? 4 : 3),
                           0, &total);
        printf("width %d: %d of the first %d video thumbnails loaded\n", W, got, total);
        if (got < total)
            failures++;

        /* Scroll down to the Shorts shelf. */
        {
            ui_input in;
            memset(&in, 0, sizeof in);
            in.pressed = UI_DOWN;
            browse_update(&b, &in);
            browse_update(&b, &in);
            in.pressed = 0;
            for (i = 0; i < 30; i++)
                browse_update(&b, &in);
        }
        draw_until_loaded(&b, W);
        save(out, "live-shorts", W);
        got = count_loaded(pg->shorts.shorts, pg->shorts.nshorts < 5 ? pg->shorts.nshorts : 5, 1, &total);
        printf("width %d: %d of the first %d Shorts thumbnails loaded\n", W, got, total);
        if (got < total)
            failures++;
    }

    /* Player screen using the first video's thumbnail (full size) as the picture. */
    {
        char url[128];
        http_request req;
        http_response resp;
        memset(&req, 0, sizeof req);
        snprintf(url, sizeof url, "https://i.ytimg.com/vi/%s/mqdefault.jpg", videos.items[0].video_id);
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
        snprintf(p.title, sizeof p.title, "%s", videos.items[0].title);
        snprintf(p.channel, sizeof p.channel, "%s", videos.items[0].channel);
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
