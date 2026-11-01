/* WiiTube for the Wii / vWii: a YouTube-style browser (search, topic chips, thumbnail grid)
 * and player, all drawn with the GPU (see src/ui). Network work runs on a background job
 * thread so the screen keeps animating. */
#include <gccore.h>
#include <fat.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wiiuse/wpad.h>

#include "../../core/http.h"
#include "../../core/image.h"
#include "../../core/log.h"
#include "../../core/media.h"
#include "../../core/net.h"
#include "../../core/ring.h"
#include "../../core/stream.h"
#include "../../core/thread.h"
#include "../../core/thumbs.h"
#include "../../core/vqueue.h"
#include "../../core/yt.h"
#include "../../ui/ui.h"
#include "audio.h"
#include "gfx.h"
#include "threading_alt.h"
#include "wii.h"

#ifndef APP_VERSION
#define APP_VERSION "0.4"
#endif
#define APP_DIR "sd:/apps/wiitube"
#define RULES_URL "https://raw.githubusercontent.com/BroOFun/wiitube/main/rules/youtube.json"
#define THUMB_URL "https://i.ytimg.com/vi/%s/mqdefault.jpg"
#define VIDEO_BUFFER (4u << 20)
#define AUDIO_BUFFER (1u << 20)

extern const int logo_small_w, logo_small_h, logo_splash_w, logo_splash_h;
extern const unsigned char logo_small_rgba[], logo_splash_rgba[];

static volatile int g_quit, g_poweroff;
static int g_sd;
static wt_image *g_logo, *g_splash;
static thumbs *g_thumbs;
static unsigned g_frame;

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

static void read_input(ui_input *in)
{
    static unsigned last_dirs, held_frames;
    static float last_x = -1000, last_y = -1000;
    unsigned held, dirs;
    ir_t ir;

    WPAD_ScanPads();
    PAD_ScanPads();
    memset(in, 0, sizeof *in);
    in->pressed = map_wpad(WPAD_ButtonsDown(0)) | map_pad(PAD_ButtonsDown(0));
    held = map_wpad(WPAD_ButtonsHeld(0)) | map_pad(PAD_ButtonsHeld(0));
    dirs = held & UI_DIRS;
    if (dirs && dirs == last_dirs) {
        if (++held_frames > 24 && held_frames % 5 == 0)
            in->pressed |= dirs; /* auto-repeat */
    } else {
        held_frames = 0;
    }
    last_dirs = dirs;

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

static unsigned free_kb(void)
{
    struct mallinfo mi = mallinfo();
    unsigned mem1 = (unsigned)((u32)SYS_GetArenaHi() - (u32)SYS_GetArenaLo());
    unsigned mem2 = (unsigned)((u32)SYS_GetArena2Hi() - (u32)SYS_GetArena2Lo());
    return ((unsigned)mi.fordblks + mem1 + mem2) / 1024;
}

static void log_memory(const char *where)
{
    struct mallinfo mi = mallinfo();
    wlog("memory (%s): heap used %u KB, available about %u KB", where, (unsigned)mi.uordblks / 1024, free_kb());
}

#define DEFAULT_QUALITY 480
#define SETTINGS_VERSION 2 /* v0.4: 480p became the default; older saved choices are reset */

static int load_quality(void)
{
    FILE *f;
    int q = DEFAULT_QUALITY, version = 0;
    if (!g_sd || !(f = fopen(APP_DIR "/settings.txt", "r")))
        return q;
    if (fscanf(f, "quality=%d\nversion=%d", &q, &version) < 1 || version < SETTINGS_VERSION ||
        (q != 144 && q != 240 && q != 360 && q != 480))
        q = DEFAULT_QUALITY;
    fclose(f);
    return q;
}

static void save_quality(int q)
{
    FILE *f;
    if (g_sd && (f = fopen(APP_DIR "/settings.txt", "w"))) {
        fprintf(f, "quality=%d\nversion=%d\n", q, SETTINGS_VERSION);
        fclose(f);
    }
}

static void flush_cache(void *p, size_t len) { DCFlushRange(p, (u32)len); }

/* ---- background jobs -------------------------------------------------------------------- */

typedef struct {
    wt_thread *thread;
    volatile int done;
} job;

static int job_start(job *j, void *(*fn)(void *), void *arg)
{
    j->done = 0;
    j->thread = wt_thread_start(fn, arg, 256 * 1024, 0);
    return j->thread ? 0 : -1;
}

static int job_running(job *j) { return j->thread && !j->done; }

/* Join a finished job. Returns 1 if it had finished and was collected. */
static int job_collect(job *j)
{
    if (!j->thread || !j->done)
        return 0;
    wt_thread_join(j->thread);
    j->thread = NULL;
    return 1;
}

static void draw_message(const char *title, const char *detail, int busy)
{
    gfx_frame_begin();
    ui_message(g_splash, title, detail, busy, g_frame++);
    gfx_frame_end();
}

/* Show an error until A (or B/HOME) is pressed. */
static void error_screen(const char *title, const char *detail)
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

/* ---- search -------------------------------------------------------------------------------- */

typedef struct {
    job j;
    char query[64];
    yt_search_result results[UI_MAX_RESULTS];
    int n;
    char err[512];
} search_job;

static void *search_main(void *arg)
{
    search_job *s = arg;
    s->n = yt_search(s->query, s->results, UI_MAX_RESULTS, s->err, sizeof s->err);
    s->j.done = 1;
    return NULL;
}

static search_job g_search;
static char g_pending_query[64];
static int g_has_pending;

static void start_search(browse_state *b, const char *query)
{
    if (job_running(&g_search.j)) {
        snprintf(g_pending_query, sizeof g_pending_query, "%s", query);
        g_has_pending = 1;
        return;
    }
    job_collect(&g_search.j);
    memset(g_search.results, 0, sizeof g_search.results);
    snprintf(g_search.query, sizeof g_search.query, "%s", query);
    g_search.err[0] = 0;
    b->status = "Loading...";
    b->loading = 1;
    if (job_start(&g_search.j, search_main, &g_search) != 0) {
        b->status = "Could not start the search.";
        b->loading = 0;
    }
}

static void poll_search(browse_state *b)
{
    static char msg[600];
    if (!job_collect(&g_search.j))
        return;
    if (g_has_pending) {
        g_has_pending = 0;
        start_search(b, g_pending_query);
        return;
    }
    b->loading = 0;
    if (g_search.n < 0) {
        snprintf(msg, sizeof msg, "Search failed: %s", g_search.err);
        b->status = msg;
        b->nresults = 0;
        return;
    }
    memcpy(b->results, g_search.results, sizeof b->results);
    b->nresults = g_search.n;
    b->status = g_search.n == 0 ? "No results." : NULL;
    b->sel = 0;
    b->scroll = 0;
    wlog("search \"%s\": %d results", g_search.query, g_search.n);
}

/* ---- playback ------------------------------------------------------------------------------ */

typedef struct {
    job j;
    char video_id[16];
    int quality;
    yt_video v;
    int have_v;
    const yt_format *vf, *af;
    yt_stream *vs, *as;
    media *mv, *ma;
    char err_title[64], err[512];
    int ok;
} open_job;

/* Rough memory needed to play at this size: download buffers, 3 planar frame textures,
 * up to 16 decoder frames (H.264 reference frames + output), and some slack. */
static unsigned needed_kb(int w, int h)
{
    unsigned frame = (unsigned)w * h * 3 / 2;
    return (VIDEO_BUFFER + AUDIO_BUFFER) / 1024 + 3u * frame / 1024 + 16u * frame / 1024 + 4096;
}

static void *open_main(void *arg)
{
    open_job *o = arg;
    char serr[256] = "";

    if (yt_get_video(o->video_id, &o->v, o->err, sizeof o->err) != 0) {
        snprintf(o->err_title, sizeof o->err_title, "Could not load this video.");
        goto done;
    }
    o->have_v = 1;
    if (yt_pick_formats(&o->v, o->quality, &o->vf, &o->af) != 0) {
        snprintf(o->err_title, sizeof o->err_title, "Can't play this video.");
        snprintf(o->err, sizeof o->err, "No H.264 stream at %dp or lower. Try another quality (1 button).",
                 o->quality);
        goto done;
    }
    if (!o->af)
        o->af = o->vf; /* muxed stream: read the same URL twice (video + audio) */
    if (needed_kb(o->vf->width, o->vf->height) > free_kb()) {
        snprintf(o->err_title, sizeof o->err_title, "Not enough memory.");
        snprintf(o->err, sizeof o->err, "%dp needs about %u KB but only %u KB are free. Try a lower quality.",
                 o->vf->height, needed_kb(o->vf->width, o->vf->height), free_kb());
        goto done;
    }
    wlog("play %s via %s: video itag %d %dx%d %s, audio itag %d %s", o->v.video_id, o->v.client, o->vf->itag,
         o->vf->width, o->vf->height, o->vf->codecs, o->af->itag, o->af->codecs);
    log_memory("before playback");
    o->vs = yt_stream_start(&o->v, o->vf, VIDEO_BUFFER);
    o->as = yt_stream_start(&o->v, o->af, AUDIO_BUFFER);
    if (!o->vs || !o->as) {
        snprintf(o->err_title, sizeof o->err_title, "Could not start the download.");
        snprintf(o->err, sizeof o->err, "Not enough memory or system resources (details in wiitube.log).");
        goto done;
    }
    o->mv = media_open(yt_stream_ring(o->vs), MEDIA_VIDEO, o->err, sizeof o->err);
    if (!o->mv) {
        yt_stream_error(o->vs, serr, sizeof serr);
        if (serr[0])
            snprintf(o->err, sizeof o->err, "%s", serr);
        snprintf(o->err_title, sizeof o->err_title, "Could not start the video.");
        goto done;
    }
    o->ma = media_open(yt_stream_ring(o->as), MEDIA_AUDIO, serr, sizeof serr);
    if (!o->ma)
        wlog("no audio: %s", serr); /* play silently rather than not at all */
    o->ok = 1;
done:
    o->j.done = 1;
    return NULL;
}

static void close_playback(open_job *o)
{
    if (o->vs)
        ring_abort(yt_stream_ring(o->vs));
    if (o->as)
        ring_abort(yt_stream_ring(o->as));
    media_close(o->mv);
    media_close(o->ma);
    yt_stream_stop(o->vs, 3000);
    yt_stream_stop(o->as, 3000);
    if (o->have_v)
        yt_video_free(&o->v);
    o->mv = o->ma = NULL;
    o->vs = o->as = NULL;
    o->have_v = 0;
}

typedef struct {
    int use_audio, started;
    u64 start;
} play_clock;

static double clock_now(void *ud)
{
    play_clock *c = ud;
    if (c->use_audio)
        return audio_clock();
    if (!c->started)
        return -1;
    return (double)ticks_to_microsecs(gettime() - c->start) / 1e6;
}

static void flush_texture(void *tex, size_t len) { DCFlushRange(tex, (u32)len); }

static int16_t g_pcm[8192 * 2];

/* Decode audio into the output buffer while that won't block long on the network. */
static int feed_audio(media *ma, ring *ar)
{
    while (audio_space() >= 8192 && (ring_available(ar) >= 32768 || ring_done(ar))) {
        int frames = 0;
        double pts = 0;
        if (media_next_audio(ma, g_pcm, 8192, &frames, &pts) <= 0)
            return 0;
        audio_push(g_pcm, frames, pts);
    }
    return 1;
}

static void play_video(const yt_search_result *r, int quality)
{
    static open_job o;
    vqueue *q = NULL;
    play_clock clk = { 0, 0, 0 };
    player_view pv;
    ui_input in;
    const yuv_frame *frame = NULL;
    int audio_live, starting = 1, idle = 0, cancelled = 0;
    char loading[300];
    vq_stats st;

    memset(&o, 0, sizeof o);
    snprintf(o.video_id, sizeof o.video_id, "%s", r->video_id);
    o.quality = quality;
    memset(&pv, 0, sizeof pv);
    snprintf(pv.title, sizeof pv.title, "%s", r->title);
    snprintf(pv.channel, sizeof pv.channel, "%s", r->channel);
    pv.volume = 255;
    pv.quality = quality;
    pv.overlay_timer = 240;

    /* 1. Look up the streams and open the decoders in the background. */
    if (job_start(&o.j, open_main, &o) != 0) {
        error_screen("Could not start playback.", NULL);
        return;
    }
    {
        char t[256];
        ui_ascii(t, sizeof t, r->title);
        snprintf(loading, sizeof loading, "Loading \"%s\"", t);
    }
    while (!o.j.done) {
        read_input(&in);
        if (in.pressed & (UI_B | UI_HOME))
            cancelled = 1;
        draw_message(cancelled ? "Cancelling..." : loading, NULL, 1);
    }
    job_collect(&o.j);
    if (!o.ok || cancelled || g_quit) {
        if (!o.ok && !cancelled)
            error_screen(o.err_title[0] ? o.err_title : "Could not play this video.", o.err);
        close_playback(&o);
        log_memory("after failed playback");
        return;
    }
    pv.duration = o.v.length_seconds;
    pv.quality = o.vf->height;

    /* 2. Start decoding; play once a picture and some sound are ready. */
    audio_reset(o.ma ? media_sample_rate(o.ma) : 44100);
    clk.use_audio = o.ma != NULL;
    audio_live = o.ma != NULL;
    q = vq_start(o.mv, clock_now, &clk, flush_texture, 0);
    if (!q) {
        log_memory("video setup failed");
        error_screen("Could not start the video decoder.", "Not enough memory (details in wiitube.log).");
        close_playback(&o);
        return;
    }

    for (;;) {
        int act;
        double now;
        const yuv_frame *next;

        read_input(&in);
        act = player_update(&pv, &in);
        if (g_quit || act == ACT_PLAYER_BACK)
            break;
        if (act == ACT_PLAYER_PAUSE && !starting) {
            pv.paused = !pv.paused;
            audio_pause(pv.paused);
        }
        if (act == ACT_PLAYER_VOLUME)
            audio_set_volume(pv.volume);

        if (audio_live && !pv.paused)
            audio_live = feed_audio(o.ma, yt_stream_ring(o.as));
        if (starting && (vq_ready(q) > 0 || vq_finished(q)) && (!audio_live || audio_buffered() >= 22050)) {
            starting = 0;
            if (clk.use_audio)
                audio_play();
            clk.start = gettime();
            clk.started = 1;
        }

        now = clock_now(&clk);
        if (!starting && !pv.paused && vq_poll(q, now, &next)) {
            frame = next;
            idle = 0;
        } else if (!pv.paused) {
            idle++;
        }
        pv.position = now > 0 ? now : 0;
        pv.buffered = o.vf->content_length > 0 ? (float)yt_stream_received(o.vs) / o.vf->content_length : 0;
        pv.buffering = starting || (idle > 45 && vq_ready(q) == 0 && !vq_finished(q));

        gfx_frame_begin();
        dr_fill(0, 0, ui_width(), UI_H, 0x000000ffu);
        if (frame) {
            float x, y, w, h;
            ui_fit(frame->w, frame->h, &x, &y, &w, &h);
            dr_video(frame, x, y, w, h); /* the GPU converts YUV to RGB */
        }
        player_draw_overlay(&pv);
        if (in.pointer && pv.overlay_timer > 0)
            ui_cursor(in.px, in.py);
        gfx_frame_end();

        if (!starting && !pv.paused && vq_finished(q) && (!clk.use_audio || (!audio_live && audio_buffered() == 0)))
            break;
    }

    audio_stop();
    if (o.vs)
        ring_abort(yt_stream_ring(o.vs)); /* unblock the decoder if it waits for data */
    if (o.as)
        ring_abort(yt_stream_ring(o.as));
    vq_get_stats(q, &st);
    wlog("played %s at %dp: decoded %d, shown %d, dropped %d, audio underruns %d, skip level %d, "
         "avg decode %.1f ms + prepare %.1f ms per frame",
         o.video_id, o.vf->height, st.decoded, st.shown, st.late_drops, audio_underruns(), st.skip_level,
         st.decode_ms, st.convert_ms);
    vq_stop(q);
    close_playback(&o);
    log_memory("after playback");
}

/* ---- main ---------------------------------------------------------------------------------- */

static const wt_image *thumb_cb(void *ud, const char *id)
{
    (void)ud;
    return thumbs_get(g_thumbs, id);
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

int main(int argc, char **argv)
{
    static startup_job su;
    static browse_state b;
    static keyboard_state kb;
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

    g_thumbs = thumbs_new(THUMB_URL, 160, 48, 2);
    browse_init(&b, g_logo);
    b.quality = load_quality();
    b.chip = 0;
    start_search(&b, ui_chips[0]);

    while (!g_quit) {
        int act;
        read_input(&in);
        poll_search(&b);

        if (kb_open) {
            act = keyboard_update(&kb, &in);
            if (act == ACT_KB_SEARCH) {
                kb_open = 0;
                b.chip = -1;
                snprintf(b.query, sizeof b.query, "%s", kb.text);
                b.focus = FOCUS_GRID;
                start_search(&b, kb.text);
            } else if (act == ACT_KB_CANCEL) {
                kb_open = 0;
            }
        } else {
            act = browse_update(&b, &in);
            switch (act) {
            case ACT_QUIT:
                quit();
                break;
            case ACT_OPEN_KEYBOARD:
                keyboard_open(&kb, b.chip < 0 ? b.query : "");
                kb_open = 1;
                break;
            case ACT_CHIP:
                b.query[0] = 0;
                start_search(&b, ui_chips[b.chip]);
                break;
            case ACT_QUALITY:
                save_quality(b.quality);
                break;
            case ACT_PLAY:
                play_video(&b.results[b.sel], b.quality);
                break;
            default:
                break;
            }
        }

        gfx_frame_begin();
        browse_draw(&b, thumb_cb, NULL);
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
