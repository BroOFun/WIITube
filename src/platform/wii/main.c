/* WiiTube for the Wii / vWii: a YouTube-style browser (Home with topic chips, Shorts, search;
 * endless lists) and player, all drawn with the GPU (see src/ui). Network work runs on
 * background job threads so the screen keeps animating. Playback is in player.c. */
#include <gccore.h>
#include <fat.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wiiuse/wpad.h>

#include "../../core/http.h"
#include "../../core/image.h"
#include "../../core/log.h"
#include "../../core/net.h"
#include "../../core/thread.h"
#include "../../core/thumbs.h"
#include "../../core/yt.h"
#include "../../ui/ui.h"
#include "../../app/pages.h"
#include "app.h"
#include "audio.h"
#include "gfx.h"
#include "threading_alt.h"
#include "wii.h"

#ifndef APP_VERSION
#define APP_VERSION "0.6.1"
#endif
#define RULES_URL "https://raw.githubusercontent.com/BroOFun/wiitube/main/rules/youtube.json"
#define THUMB_URL "https://i.ytimg.com/vi/%s/mqdefault.jpg"

extern const int logo_small_w, logo_small_h, logo_splash_w, logo_splash_h;
extern const unsigned char logo_small_rgba[], logo_splash_rgba[];

volatile int g_quit;
static volatile int g_poweroff;
int g_sd;
wt_image *g_logo, *g_splash;
unsigned g_frame;
app_settings g_settings;
static thumbs *g_thumbs;

/* ---- input ---------------------------------------------------------------------------- */

static unsigned map_wpad(u32 b)
{
    unsigned k = 0;
    if (b & WPAD_BUTTON_UP) k |= UI_UP;
    if (b & WPAD_BUTTON_DOWN) k |= UI_DOWN;
    if (b & WPAD_BUTTON_LEFT) k |= UI_LEFT;
    if (b & WPAD_BUTTON_RIGHT) k |= UI_RIGHT;
    if (b & WPAD_BUTTON_A) k |= UI_A;
    if (b & WPAD_BUTTON_B) k |= UI_B;
    if (b & WPAD_BUTTON_PLUS) k |= UI_PLUS;
    if (b & WPAD_BUTTON_MINUS) k |= UI_MINUS;
    if (b & WPAD_BUTTON_HOME) k |= UI_HOME;
    if (b & WPAD_BUTTON_1) k |= UI_1;
    if (b & WPAD_BUTTON_2) k |= UI_2;
    if (b & WPAD_NUNCHUK_BUTTON_C) k |= UI_C;
    if (b & WPAD_NUNCHUK_BUTTON_Z) k |= UI_Z;
    return k;
}

static unsigned map_pad(u32 b) /* GameCube controller (original Wii only) */
{
    unsigned k = 0;
    if (b & PAD_BUTTON_UP) k |= UI_UP;
    if (b & PAD_BUTTON_DOWN) k |= UI_DOWN;
    if (b & PAD_BUTTON_LEFT) k |= UI_LEFT;
    if (b & PAD_BUTTON_RIGHT) k |= UI_RIGHT;
    if (b & PAD_BUTTON_A) k |= UI_A;
    if (b & PAD_BUTTON_B) k |= UI_B;
    if (b & PAD_BUTTON_START) k |= UI_PLUS;
    if (b & PAD_BUTTON_X) k |= UI_1;
    if (b & PAD_BUTTON_Y) k |= UI_2;
    if (b & PAD_TRIGGER_Z) k |= UI_HOME;
    return k;
}

/* One stick axis to -1..1 from its calibration (raw values are 0..255). */
static float stick_axis(int pos, int min, int center, int max)
{
    float v;
    if (max <= min + 20 || center <= min || center >= max) {
        min = 30;
        center = 128;
        max = 226;
    }
    v = pos >= center ? (float)(pos - center) / (max - center) : (float)(pos - center) / (center - min);
    v = v > 1 ? 1 : v < -1 ? -1 : v;
    return v > -0.18f && v < 0.18f ? 0 : v; /* dead zone */
}

void read_input(ui_input *in)
{
    static unsigned last_dirs, held_frames;
    static float last_x = -1000, last_y = -1000;
    unsigned dirs;
    WPADData *wd;
    ir_t ir;

    WPAD_ScanPads();
    PAD_ScanPads();
    memset(in, 0, sizeof *in);
    in->pressed = map_wpad(WPAD_ButtonsDown(0)) | map_pad(PAD_ButtonsDown(0));
    in->held = map_wpad(WPAD_ButtonsHeld(0)) | map_pad(PAD_ButtonsHeld(0));
    dirs = in->held & UI_DIRS;
    if (dirs && dirs == last_dirs) {
        if (++held_frames > 24 && held_frames % 5 == 0)
            in->pressed |= dirs; /* auto-repeat */
    } else {
        held_frames = 0;
    }
    last_dirs = dirs;

    wd = WPAD_Data(0);
    if (wd && wd->exp.type == WPAD_EXP_NUNCHUK) {
        const joystick_t *js = &wd->exp.nunchuk.js;
        in->stick_x = stick_axis(js->pos.x, js->min.x, js->center.x, js->max.x);
        in->stick_y = -stick_axis(js->pos.y, js->min.y, js->center.y, js->max.y); /* up = negative */
    } else {
        int sx = PAD_StickX(0), sy = PAD_StickY(0);
        if (sx > 18 || sx < -18)
            in->stick_x = sx / 80.0f > 1 ? 1 : sx / 80.0f < -1 ? -1 : sx / 80.0f;
        if (sy > 18 || sy < -18)
            in->stick_y = -sy / 80.0f > 1 ? 1 : -sy / 80.0f < -1 ? -1 : -sy / 80.0f;
    }

    WPAD_IR(0, &ir);
    if (ir.valid) {
        in->pointer = 1;
        in->px = ir.x;
        in->py = ir.y;
        if ((in->px - last_x) * (in->px - last_x) + (in->py - last_y) * (in->py - last_y) > 9) {
            in->pointer_moved = 1;
            last_x = in->px;
            last_y = in->py;
        }
    }
}

static void on_reset(u32 irq, void *ctx)
{
    (void)irq;
    (void)ctx;
    g_quit = 1;
}

static void on_power(void)
{
    g_poweroff = 1;
    g_quit = 1;
}

/* ---- diagnostics / settings ------------------------------------------------------------- */

unsigned free_kb(void)
{
    struct mallinfo mi = mallinfo();
    unsigned mem1 = (unsigned)((u32)SYS_GetArenaHi() - (u32)SYS_GetArenaLo());
    unsigned mem2 = (unsigned)((u32)SYS_GetArena2Hi() - (u32)SYS_GetArena2Lo());
    return ((unsigned)mi.fordblks + mem1 + mem2) / 1024;
}

void log_memory(const char *where)
{
    struct mallinfo mi = mallinfo();
    wlog("memory (%s): heap used %u KB, available about %u KB", where, (unsigned)mi.uordblks / 1024, free_kb());
}

#define DEFAULT_QUALITY 480
#define SETTINGS_VERSION 2 /* v0.4: 480p became the default; older saved choices are reset */

static void settings_load(void)
{
    FILE *f;
    char line[64];
    int version = 0;
    g_settings.quality = DEFAULT_QUALITY;
    g_settings.captions = 0;
    if (!g_sd || !(f = fopen(APP_DIR "/settings.txt", "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        int v;
        if (sscanf(line, "quality=%d", &v) == 1)
            g_settings.quality = v;
        else if (sscanf(line, "version=%d", &v) == 1)
            version = v;
        else if (sscanf(line, "captions=%d", &v) == 1)
            g_settings.captions = v != 0;
    }
    fclose(f);
    if (version < SETTINGS_VERSION || (g_settings.quality != 144 && g_settings.quality != 240 &&
                                       g_settings.quality != 360 && g_settings.quality != 480))
        g_settings.quality = DEFAULT_QUALITY;
}

void settings_save(void)
{
    FILE *f;
    if (g_sd && (f = fopen(APP_DIR "/settings.txt", "w"))) {
        fprintf(f, "quality=%d\nversion=%d\ncaptions=%d\n", g_settings.quality, SETTINGS_VERSION, g_settings.captions);
        fclose(f);
    }
}

static void flush_cache(void *p, size_t len) { DCFlushRange(p, (u32)len); }

/* ---- background jobs -------------------------------------------------------------------- */

int job_start(job *j, void *(*fn)(void *), void *arg)
{
    j->done = 0;
    j->thread = wt_thread_start(fn, arg, 256 * 1024, 0);
    return j->thread ? 0 : -1;
}

int job_running(job *j) { return j->thread && !j->done; }

int job_collect(job *j)
{
    if (!j->thread || !j->done)
        return 0;
    wt_thread_join(j->thread);
    j->thread = NULL;
    return 1;
}

void draw_message(const char *title, const char *detail, int busy)
{
    gfx_frame_begin();
    ui_message(g_splash, title, detail, busy, g_frame++);
    gfx_frame_end();
}

void error_screen(const char *title, const char *detail)
{
    ui_input in;
    wlog("error: %s: %s", title, detail ? detail : "");
    do {
        draw_message(title, detail, 0);
        read_input(&in);
    } while (!g_quit && !(in.pressed & (UI_A | UI_B | UI_HOME)));
}

/* ---- startup ----------------------------------------------------------------------------- */

typedef struct {
    job j;
    int ok;
    char err[512];
} startup_job;

static void load_rules(void)
{
    http_request req;
    http_response resp;
    char err[256];

    memset(&req, 0, sizeof req);
    req.url = RULES_URL;
    req.headers = "User-Agent: WiiTube/" APP_VERSION "\r\n";
    req.max_buffer = 256 * 1024;
    if (http_do(&req, &resp, err, sizeof err) == 0 && resp.status == 200 && resp.body &&
        yt_load_rules_json((const char *)resp.body, "GitHub", err, sizeof err) == 0) {
        if (g_sd) {
            FILE *f = fopen(APP_DIR "/rules.json", "wb");
            if (f) {
                fwrite(resp.body, 1, resp.body_len, f);
                fclose(f);
            }
        }
    } else {
        wlog("rules: GitHub copy unavailable (%s, HTTP %d)", err, resp.status);
        yt_load_rules(g_sd ? APP_DIR "/rules.json" : NULL, err, sizeof err);
    }
    http_response_free(&resp);
    wlog("rules: using %s", yt_rules_source());
}

static void *startup_main(void *arg)
{
    startup_job *s = arg;
    int rc = plat_net_init();
    if (rc < 0) {
        snprintf(s->err, sizeof s->err,
                 "Network error %d. Check that the Wii is connected (Wii Settings > Internet).", rc);
    } else {
        wlog("network up, IP %s", wii_net_ip());
        if (http_global_init(s->err, sizeof s->err) == 0) {
            load_rules();
            s->ok = 1;
        }
    }
    s->j.done = 1;
    return NULL;
}

static browse_state g_browse;

/* During playback (player.c): keep merging pages that arrive. */
static void play_tick(void *ud)
{
    browse_state *b = ud;
    b->want_more = 0;
    pages_tick(b);
}

/* ---- main ---------------------------------------------------------------------------------- */

/* Shorts use the normal 16:9 thumbnail, cropped to 9:16 when drawn: the vertical picture
 * URLs YouTube lists (oardefault/oar2 with signed parameters) are served as WebP, which
 * stb_image can't decode, and the plain oar2.jpg is 1080x1920 (too big to decode on a Wii). */
static const wt_image *thumb_cb(void *ud, const yt_search_result *r, int vertical)
{
    (void)ud;
    (void)vertical;
    return thumbs_get(g_thumbs, r->video_id);
}

static void quit(void)
{
    int opened, reused;
    http_stats(&opened, &reused);
    wlog("exiting (connections: %d opened, %d reused)", opened, reused);
    wlog_close();
    if (g_poweroff)
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    exit(0);
}

static void play(browse_state *b)
{
    play_request req;
    int last;
    memset(&req, 0, sizeof req);
    req.feed = b->play_feed;
    req.index = b->play_index;
    req.tick = play_tick;
    req.tick_ud = b;
    last = player_run(&req);
    if (req.feed == &b->pages[b->page].feed && last >= 0 && last < req.feed->n)
        browse_select(b, last);
    pages_after_play(b);
}

/* The Shorts tab: YouTube's Shorts feed, continuing from the last Short watched. */
static void play_shorts_tab(browse_state *b)
{
    play_request req;
    memset(&req, 0, sizeof req);
    req.shorts_from = history_last_short();
    req.index = -1;
    req.tick = play_tick;
    req.tick_ud = b;
    player_run(&req);
    pages_after_play(b);
}

int main(int argc, char **argv)
{
    static startup_job su;
    static keyboard_state kb;
    browse_state *b = &g_browse;
    int kb_open = 0;
    ui_input in;
    (void)argc;
    (void)argv;

    gfx_init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, (u32)ui_width(), UI_H);
    PAD_Init();
    audio_init();
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);
    image_set_flush(flush_cache);

    wlog_quiet(1);
    g_sd = fatInitDefault();
    if (g_sd) {
        mkdir("sd:/apps", 0777);
        mkdir(APP_DIR, 0777);
        wlog_open(APP_DIR "/wiitube.log");
    } else {
        wlog_open(NULL);
    }
    wlog("WiiTube " APP_VERSION " starting (SD card %s, %s TV, canvas %dx%d)", g_sd ? "found" : "missing",
         gfx_widescreen() ? "16:9" : "4:3", ui_width(), UI_H);
    wii_mbedtls_threading_init();
    g_logo = image_from_rgba(logo_small_rgba, logo_small_w, logo_small_h);
    g_splash = image_from_rgba(logo_splash_rgba, logo_splash_w, logo_splash_h);
    settings_load();
    pages_init(g_sd ? APP_DIR "/history.txt" : NULL);
    log_memory("startup");

    /* Connect while showing the splash screen. */
    if (job_start(&su.j, startup_main, &su) == 0) {
        while (!su.j.done)
            draw_message("Connecting to the internet...", NULL, 1);
        job_collect(&su.j);
    }
    if (!su.ok) {
        error_screen("Could not connect.", su.err[0] ? su.err : "Unknown error.");
        quit();
    }

    g_thumbs = thumbs_new(THUMB_URL, 160, 64, 2);
    browse_init(b, g_logo);
    b->quality = g_settings.quality;
    pages_start(b);

    while (!g_quit) {
        int act;
        read_input(&in);

        if (kb_open) {
            act = keyboard_update(&kb, &in);
            if (act == ACT_KB_SEARCH) {
                kb_open = 0;
                pages_search(b, kb.text);
            } else if (act == ACT_KB_CANCEL) {
                kb_open = 0;
            }
            b->want_more = 0;
        } else {
            act = browse_update(b, &in);
            switch (act) {
            case ACT_QUIT:
                quit();
                break;
            case ACT_OPEN_KEYBOARD:
                keyboard_open(&kb, b->pages[PAGE_SEARCH].query);
                kb_open = 1;
                break;
            case ACT_CHIP:
                pages_handle(b, act);
                break;
            case ACT_QUALITY:
                g_settings.quality = b->quality;
                settings_save();
                break;
            case ACT_PLAY:
                play(b);
                b->quality = g_settings.quality; /* may have been changed in the player */
                break;
            case ACT_SHORTS:
                play_shorts_tab(b);
                b->quality = g_settings.quality;
                break;
            default:
                break;
            }
        }
        pages_tick(b);

        gfx_frame_begin();
        browse_draw(b, thumb_cb, NULL);
        if (kb_open)
            keyboard_draw(&kb);
        if (in.pointer)
            ui_cursor(in.px, in.py);
        gfx_frame_end();
        thumbs_frame_done(g_thumbs);
        g_frame++;
    }
    quit();
    return 0;
}
