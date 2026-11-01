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

static const struct {
    const char *title, *views;
} SHORT_SAMPLE[] = {
    { "Wii Sports bowling but it's 2026", "1.2M views" }, { "Perfect 300 game first try?!", "845K views" },
    { "Mii Channel music hits different", "3.4M views" }, { "Wii tennis ace compilation", "220K views" },
    { "Rainbow Road on the Wii in 30 seconds", "9.1M views" }, { "Boxing KO in 4 punches", "77K views" },
    { "Hole in one on every course", "512K views" }, { "Baseball home run derby", "1.9M views" },
};
#define NSHORT ((int)(sizeof SHORT_SAMPLE / sizeof SHORT_SAMPLE[0]))

static wt_image *thumbs[NSAMPLE], *short_thumbs[NSHORT];

static const wt_image *get_thumb(void *ud, const yt_search_result *r, int vertical)
{
    int i = atoi(r->video_id + 1);
    (void)ud;
    (void)vertical;
    if (r->video_id[0] == 's')
        return i >= 0 && i < NSHORT ? short_thumbs[i] : NULL;
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

/* Sample entries; repeat makes a longer list (ids stay unique). */
static void fill_page(browse_page *pg, int repeat, int with_shorts)
{
    int i, k;
    yt_search_result r;
    for (k = 0; k < repeat; k++)
        for (i = 0; i < NSAMPLE; i++) {
            memset(&r, 0, sizeof r);
            snprintf(r.video_id, sizeof r.video_id, "t%d_%d", i, k);
            snprintf(r.title, sizeof r.title, "%s", SAMPLE[i].title);
            snprintf(r.channel, sizeof r.channel, "%s", SAMPLE[i].channel);
            snprintf(r.duration, sizeof r.duration, "%s", SAMPLE[i].duration);
            snprintf(r.views, sizeof r.views, "%s", SAMPLE[i].views);
            snprintf(r.published, sizeof r.published, "%s", SAMPLE[i].published);
            yt_feed_add(&pg->videos, &r);
        }
    for (k = 0; with_shorts && k < 2; k++)
        for (i = 0; i < NSHORT; i++) {
            memset(&r, 0, sizeof r);
            snprintf(r.video_id, sizeof r.video_id, "s%d_%d", i, k);
            snprintf(r.title, sizeof r.title, "%s", SHORT_SAMPLE[i].title);
            snprintf(r.views, sizeof r.views, "%s", SHORT_SAMPLE[i].views);
            r.is_short = 1;
            yt_feed_add(&pg->shorts, &r);
        }
    pg->videos.continuation = strdup("more");
    pg->started = 3;
}

static void frames(browse_state *b, ui_input *in, int n)
{
    int i;
    for (i = 0; i < n; i++)
        browse_update(b, in);
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

static void draw_player(player_view *p, const wt_image *frame, const yuv_frame *yuv, int W)
{
    float x, y, dw, dh;
    soft_begin(W);
    dr_fill(0, 0, W, UI_H, p->is_short ? C_BG : 0x000000ff);
    player_video_rect(p, frame->w, frame->h, &x, &y, &dw, &dh);
    dr_video(yuv, x, y, dw, dh);
    player_draw_overlay(p);
}

int main(int argc, char **argv)
{
    static browse_state b;
    keyboard_state k;
    player_view p;
    ui_input in;
    wt_image *logo, *splash, *frame, *vframe;
    const yuv_frame *yuv;
    yuv_frame fyuv, vyuv;
    const char *dir = argc > 1 ? argv[1] : ".";
    const char *out = argc > 2 ? argv[2] : ".";
    char path[512];
    int widths[2] = { 640, 854 }, wi, i;

    for (i = 0; i < NSAMPLE; i++) {
        snprintf(path, sizeof path, "%s/t%d.jpg", dir, i);
        thumbs[i] = load_jpeg(path, 160);
    }
    for (i = 0; i < NSHORT; i++) {
        snprintf(path, sizeof path, "%s/s%d.jpg", dir, i);
        short_thumbs[i] = load_jpeg(path, 120); /* shrunk like on the Wii (405 -> 101 wide) */
    }
    snprintf(path, sizeof path, "%s/t0.jpg", dir);
    frame = load_jpeg(path, 0); /* stands in for a 320x180 video frame */
    fyuv = *to_yuv(frame); /* to_yuv returns a static frame: keep a copy */
    yuv = &fyuv;
    snprintf(path, sizeof path, "%s/s4.jpg", dir);
    vframe = load_jpeg(path, 0); /* a vertical Short's frame */
    vyuv = *to_yuv(vframe);
    logo = image_from_rgba(logo_small_rgba, logo_small_w, logo_small_h);
    splash = image_from_rgba(logo_splash_rgba, logo_splash_w, logo_splash_h);

    for (wi = 0; wi < 2; wi++) {
        const int W = widths[wi];
        memset(&in, 0, sizeof in);

        /* Home: topic chip results with the Shorts shelf, second card selected. */
        soft_begin(W);
        browse_init(&b, logo);
        b.pages[PAGE_HOME].chip = 1;
        b.chip_sel = 1;
        fill_page(&b.pages[PAGE_HOME], 3, 1);
        b.pages[PAGE_HOME].col = 1;
        frames(&b, &in, 2);
        soft_begin(W);
        browse_draw(&b, get_thumb, NULL);
        save(out, "home", W);

        /* Down to the Shorts shelf with the D-pad, then the pointer on the scrollbar. */
        in.pressed = UI_DOWN;
        frames(&b, &in, 2);
        in.pressed = UI_RIGHT;
        frames(&b, &in, 3);
        in.pressed = 0;
        frames(&b, &in, 30);
        in.pointer = 1;
        in.pointer_moved = 1;
        in.px = W - 12;
        in.py = 200;
        frames(&b, &in, 1);
        soft_begin(W);
        browse_draw(&b, get_thumb, NULL);
        ui_cursor(in.px, in.py);
        save(out, "home-shelf", W);

        /* Further down: more rows loaded, footer spinner while the next page comes. */
        memset(&in, 0, sizeof in);
        in.pressed = UI_DOWN;
        for (i = 0; i < 6; i++)
            frames(&b, &in, 1);
        b.pages[PAGE_HOME].loading_videos = 1;
        in.pressed = 0;
        frames(&b, &in, 30);
        soft_begin(W);
        browse_draw(&b, get_thumb, NULL);
        save(out, "home-more", W);

        /* Shorts page. */
        browse_set_page(&b, PAGE_SHORTS);
        fill_page(&b.pages[PAGE_SHORTS], 0, 1);
        b.pages[PAGE_SHORTS].shorts.continuation = strdup("more");
        b.pages[PAGE_SHORTS].col = 2;
        b.focus = FOCUS_GRID;
        frames(&b, &in, 2);
        soft_begin(W);
        browse_draw(&b, get_thumb, NULL);
        save(out, "shorts", W);

        /* Search results, the rail focused. */
        browse_set_page(&b, PAGE_SEARCH);
        snprintf(b.pages[PAGE_SEARCH].query, sizeof b.pages[PAGE_SEARCH].query, "wii sports");
        fill_page(&b.pages[PAGE_SEARCH], 1, 1);
        b.focus = FOCUS_RAIL;
        b.rail_sel = PAGE_SEARCH;
        frames(&b, &in, 2);
        soft_begin(W);
        browse_draw(&b, get_thumb, NULL);
        save(out, "search", W);

        /* QWERTY keyboard over the results. */
        soft_begin(W);
        browse_draw(&b, get_thumb, NULL);
        keyboard_open(&k, "mario kart wii");
        k.row = 2;
        k.col = 3;
        keyboard_draw(&k);
        save(out, "keyboard", W);

        /* First page loading. */
        browse_set_page(&b, PAGE_HOME);
        browse_reset_page(&b, PAGE_HOME);
        b.pages[PAGE_HOME].loading_videos = 1;
        b.pages[PAGE_HOME].started = 3;
        b.frame = 12;
        soft_begin(W);
        browse_draw(&b, get_thumb, NULL);
        save(out, "loading-page", W);

        /* Player: controls with the pointer over the progress bar. */
        player_view_init(&p);
        snprintf(p.title, sizeof p.title, "%s", SAMPLE[0].title);
        snprintf(p.channel, sizeof p.channel, "%s", SAMPLE[0].channel);
        p.position = 83;
        p.duration = 58 * 60 + 32;
        p.buffered = 0.35f;
        p.volume = 192;
        p.quality = 480;
        p.can_seek = 1;
        p.cc_available = 1;
        memset(&in, 0, sizeof in);
        in.pointer = 1;
        in.pointer_moved = 1;
        in.px = W * 0.55f;
        in.py = UI_H - 57;
        player_update(&p, &in);
        draw_player(&p, frame, yuv, W);
        ui_cursor(in.px, in.py);
        save(out, "player", W);

        /* Paused, captions on, skipping ahead with the D-pad. */
        memset(&in, 0, sizeof in);
        p.paused = 1;
        p.cc_on = 1;
        snprintf(p.cc_label, sizeof p.cc_label, "English (auto-generated)");
        snprintf(p.cc_text, sizeof p.cc_text, "so today we're going to play every single\nWii Sports game ever made");
        in.pressed = UI_RIGHT;
        player_update(&p, &in);
        player_update(&p, &in);
        in.pressed = 0;
        player_update(&p, &in);
        draw_player(&p, frame, yuv, W);
        save(out, "player-paused", W);

        /* Settings menu, then its quality list. */
        p.nudge_target = -1;
        p.nudge_timer = 0;
        p.paused = 0;
        p.qualities[0] = 144;
        p.qualities[1] = 240;
        p.qualities[2] = 360;
        p.qualities[3] = 480;
        p.nqualities = 4;
        p.naudio = 3;
        snprintf(p.audio_names[0], sizeof p.audio_names[0], "English (original)");
        snprintf(p.audio_names[1], sizeof p.audio_names[1], "Spanish");
        snprintf(p.audio_names[2], sizeof p.audio_names[2], "Japanese");
        in.pressed = UI_2;
        player_update(&p, &in);
        in.pressed = 0;
        draw_player(&p, frame, yuv, W);
        save(out, "player-menu", W);
        in.pressed = UI_A;
        player_update(&p, &in);
        in.pressed = 0;
        draw_player(&p, frame, yuv, W);
        save(out, "player-quality", W);

        /* A Short: vertical video, captions, previous/next. */
        player_view_init(&p);
        p.is_short = 1;
        snprintf(p.title, sizeof p.title, "%s", SHORT_SAMPLE[4].title);
        snprintf(p.channel, sizeof p.channel, "@wiiclips");
        p.position = 12;
        p.duration = 30;
        p.buffered = 1;
        p.quality = 360;
        p.can_seek = 1;
        p.has_next = p.has_prev = 1;
        p.cc_available = 1;
        p.cc_on = 1;
        snprintf(p.cc_text, sizeof p.cc_text, "watch this shortcut");
        draw_player(&p, vframe, &vyuv, W);
        save(out, "short", W);

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
