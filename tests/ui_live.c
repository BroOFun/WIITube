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

static const wt_image *get_thumb(void *ud, const char *id)
{
    (void)ud;
    return thumbs_get(g_thumbs, id);
}

static void save(const char *dir, const char *name, int width)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s-%d.ppm", dir, name, width);
    soft_save_ppm(path);
}

/* Visible cards whose thumbnails are loaded (and how many are visible). */
static int count_loaded(browse_state *b, int *visible)
{
    int i, n = 0, cols = ui_width() >= 800 ? 4 : 3, rows = 2;
    *visible = b->nresults < cols * rows ? b->nresults : cols * rows;
    for (i = 0; i < *visible; i++)
        if (thumbs_get(g_thumbs, b->results[i].video_id))
            n++;
    return n;
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
    static yt_search_result res[UI_MAX_RESULTS];
    const char *query = argc > 1 ? argv[1] : "wii sports";
    const char *out = argc > 2 ? argv[2] : ".";
    char err[512];
    int widths[2] = { 640, 854 }, wi, n, failures = 0;
    wt_image *logo, *splash, *frame = NULL;

    if (yt_load_rules(argc > 3 ? argv[3] : "rules/youtube.json", err, sizeof err) != 0 ||
        http_global_init(err, sizeof err) != 0) {
        fprintf(stderr, "setup failed: %s\n", err);
        return 1;
    }
    logo = image_from_rgba(logo_small_rgba, logo_small_w, logo_small_h);
    splash = image_from_rgba(logo_splash_rgba, logo_splash_w, logo_splash_h);
    n = yt_search(query, res, UI_MAX_RESULTS, err, sizeof err);
    if (n <= 0) {
        fprintf(stderr, "search failed: %s\n", n < 0 ? err : "no results");
        return 1;
    }
    printf("search \"%s\": %d results\n", query, n);
    g_thumbs = thumbs_new("https://i.ytimg.com/vi/%s/mqdefault.jpg", 160, 48, 2);

    for (wi = 0; wi < 2; wi++) {
        int tries, loaded = 0, visible = 0;
        soft_begin(widths[wi]);
        browse_init(&b, logo);
        memcpy(b.results, res, sizeof res);
        b.nresults = n;
        b.chip = -1;
        snprintf(b.query, sizeof b.query, "%s", query);
        b.sel = 1;
        /* Keep "drawing frames" until the visible thumbnails have arrived (max ~30 s). */
        for (tries = 0; tries < 300; tries++) {
            soft_begin(widths[wi]);
            browse_draw(&b, get_thumb, NULL);
            thumbs_frame_done(g_thumbs);
            loaded = count_loaded(&b, &visible);
            if (loaded == visible)
                break;
            wt_sleep_ms(100);
        }
        soft_begin(widths[wi]);
        browse_draw(&b, get_thumb, NULL);
        save(out, "live-home", widths[wi]);
        printf("width %d: %d of %d visible thumbnails loaded\n", widths[wi], loaded, visible);
        if (loaded < visible)
            failures++;
    }

    /* Player screen using the first video's thumbnail (full size) as the picture. */
    {
        char url[128];
        http_request req;
        http_response resp;
        memset(&req, 0, sizeof req);
        snprintf(url, sizeof url, "https://i.ytimg.com/vi/%s/mqdefault.jpg", b.results[0].video_id);
        req.url = url;
        req.headers = "User-Agent: Mozilla/5.0\r\n";
        if (http_do(&req, &resp, err, sizeof err) == 0 && resp.status == 200)
            frame = image_from_jpeg(resp.body, resp.body_len, 0);
        http_response_free(&resp);
    }
    if (frame) {
        player_view p;
        float x, y, w, h;
        memset(&p, 0, sizeof p);
        snprintf(p.title, sizeof p.title, "%s", b.results[0].title);
        snprintf(p.channel, sizeof p.channel, "%s", b.results[0].channel);
        p.position = 83;
        p.duration = 600;
        p.buffered = 0.4f;
        p.volume = 192;
        p.quality = 360;
        p.overlay_timer = 100;
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
