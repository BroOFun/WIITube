/* Renders the Wii UI screens with the software backend so they can be checked without a
 * Wii. usage: ui_preview THUMB_DIR OUT_DIR  (writes OUT_DIR/<screen>-<width>.ppm)
 * The video entries are sample data in the shape of real search results. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/platform/linux/soft_draw.h"
#include "../src/ui/ui.h"

extern const int logo_small_w, logo_small_h, logo_splash_w, logo_splash_h;
extern const unsigned char logo_small_rgba[], logo_splash_rgba[];

static const struct {
    const char *title, *channel, *duration, *views, *published;
} SAMPLE[] = {
    { "I Played EVERY Wii Sports Game", "Xoai", "58:32", "2.1M views", "4 months ago" },
    { "Nintendo Switch Sports Resort \xe2\x80\x93 Overview Trailer \xe2\x80\x93 Nintendo Switch",
      "Nintendo of America", "7:33", "1.4M views", "5 months ago" },
    { "Nintendo Switch Sports Resort Review", "IGN", "9:05", "389K views", "5 months ago" },
    { "Wii Sports Resort Deserved Better", "Burback", "20:18", "912K views", "1 year ago" },
    { "Wii Sports - Longplay | Wii", "Project Longplay", "1:17:13", "105K views", "3 years ago" },
    { "Can you LOSE in All 15 Wii Sports?", "RedFalcon", "15:40", "4.3M views", "2 years ago" },
    { "Why Lucia Is Wii Sports' Perfect Champion", "Rajeem", "20:34", "650K views", "8 months ago" },
    { "Wii Sports 20 years later", "Mati", "39:38", "77K views", "1 hour ago" },
    { "Wii Sports Obscurities", "RB77", "19:34", "1.1M views", "2 years ago" },
    { "Wii Sports - All Bosses", "NintenU", "13:41", "3.2M views", "6 years ago" },
    { "Beta64 - Wii Sports Resort / Wii MotionPlus", "Beta64", "41:33", "2.4M views", "4 years ago" },
    { "The Death of Wii Sports", "MewZe", "23:51", "1.8M views", "1 year ago" },
};
#define NSAMPLE ((int)(sizeof SAMPLE / sizeof SAMPLE[0]))

static wt_image *thumbs[NSAMPLE];

static const wt_image *get_thumb(void *ud, const char *id)
{
    int i = atoi(id + 1);
    (void)ud;
    return i >= 0 && i < NSAMPLE ? thumbs[i] : NULL;
}

static wt_image *load_jpeg(const char *path, int max_w)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    wt_image *img;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    img = image_from_jpeg(buf, (size_t)n, max_w);
    free(buf);
    return img;
}

static void save(const char *dir, const char *name, int width)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s-%d.ppm", dir, name, width);
    soft_save_ppm(path);
}

static void fill_results(browse_state *b)
{
    int i;
    for (i = 0; i < NSAMPLE; i++) {
        yt_search_result *r = &b->results[i];
        snprintf(r->video_id, sizeof r->video_id, "t%d", i);
        snprintf(r->title, sizeof r->title, "%s", SAMPLE[i].title);
        snprintf(r->channel, sizeof r->channel, "%s", SAMPLE[i].channel);
        snprintf(r->duration, sizeof r->duration, "%s", SAMPLE[i].duration);
        snprintf(r->views, sizeof r->views, "%s", SAMPLE[i].views);
        snprintf(r->published, sizeof r->published, "%s", SAMPLE[i].published);
    }
    b->nresults = NSAMPLE;
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
    keyboard_state k;
    player_view p;
    ui_input in;
    wt_image *logo, *splash, *frame;
    const yuv_frame *yuv;
    const char *out = argc > 2 ? argv[2] : ".";
    char path[512];
    int widths[2] = { 640, 854 }, wi, i;

    for (i = 0; i < NSAMPLE; i++) {
        snprintf(path, sizeof path, "%s/t%d.jpg", argc > 1 ? argv[1] : ".", i);
        thumbs[i] = load_jpeg(path, 160);
    }
    snprintf(path, sizeof path, "%s/t0.jpg", argc > 1 ? argv[1] : ".");
    frame = load_jpeg(path, 0); /* stands in for a 320x180 video frame */
    yuv = to_yuv(frame);
    logo = image_from_rgba(logo_small_rgba, logo_small_w, logo_small_h);
    splash = image_from_rgba(logo_splash_rgba, logo_splash_w, logo_splash_h);

    for (wi = 0; wi < 2; wi++) {
        const int W = widths[wi];
        memset(&in, 0, sizeof in);

        /* Home: topic chip results, second card selected. */
        soft_begin(W);
        browse_init(&b, logo);
        fill_results(&b);
        b.chip = 1;
        b.chip_sel = 1;
        b.sel = 1;
        browse_draw(&b, get_thumb, NULL);
        save(out, "home", W);

        /* Scrolled down to the second row, with the Wii Remote pointer. */
        b.sel = W >= 800 ? 5 : 4;
        for (i = 0; i < 30; i++)
            browse_update(&b, &in);
        soft_begin(W);
        browse_draw(&b, get_thumb, NULL);
        ui_cursor(W * 0.62f, 330);
        save(out, "scrolled", W);

        /* Search keyboard over the results. */
        soft_begin(W);
        b.focus = FOCUS_SEARCH;
        browse_draw(&b, get_thumb, NULL);
        keyboard_open(&k, "mario kart wii");
        k.row = 1;
        k.col = 4;
        keyboard_draw(&k);
        save(out, "keyboard", W);

        /* Loading a search. */
        soft_begin(W);
        b.chip = -1;
        snprintf(b.query, sizeof b.query, "mario kart wii");
        b.status = "Searching for \"mario kart wii\"...";
        b.loading = 1;
        b.frame = 12;
        browse_draw(&b, get_thumb, NULL);
        save(out, "searching", W);
        b.status = NULL;
        b.loading = 0;

        /* Player with controls showing, and paused. */
        memset(&p, 0, sizeof p);
        snprintf(p.title, sizeof p.title, "%s", SAMPLE[0].title);
        snprintf(p.channel, sizeof p.channel, "%s", SAMPLE[0].channel);
        p.position = 83;
        p.duration = 58 * 60 + 32;
        p.buffered = 0.35f;
        p.volume = 192;
        p.quality = 360;
        p.overlay_timer = 100;
        for (i = 0; i < 2; i++) {
            float x, y, dw, dh;
            soft_begin(W);
            dr_fill(0, 0, W, UI_H, 0x000000ff);
            ui_fit(frame->w, frame->h, &x, &y, &dw, &dh);
            dr_video(yuv, x, y, dw, dh);
            p.paused = i;
            player_draw_overlay(&p);
            save(out, i ? "player-paused" : "player", W);
        }

        /* Loading and error messages. */
        soft_begin(W);
        ui_message(splash, "Loading video...", NULL, 1, 7);
        save(out, "loading", W);
        soft_begin(W);
        ui_message(splash, "Can't play this video.",
                   "No H.264 stream at 480p or lower for this video. Try a lower quality with the 1 button.", 0, 0);
        save(out, "error", W);
    }
    printf("previews written to %s\n", out);
    return 0;
}
