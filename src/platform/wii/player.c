/* Playback. A background job looks the video up, picks the streams and opens the decoders;
 * the main thread then plays with the audio clock driving the picture. Seeking, changing the
 * quality and changing the audio track all restart the streams at a position: the seek
 * index (sidx) gives the byte offset of the 5-10 s piece that contains it, and the download
 * sends the stream header followed by the data from there (see yt_stream_start_at).
 * Shorts play in a loop; Up / Down move through the list they came from. */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/captions.h"
#include "../../core/log.h"
#include "../../core/media.h"
#include "../../core/ring.h"
#include "../../core/stream.h"
#include "../../core/vqueue.h"
#include "../../core/yt.h"
#include "../../ui/ui.h"
#include "app.h"
#include "audio.h"
#include "gfx.h"

#define VIDEO_BUFFER (4u << 20)
#define AUDIO_BUFFER (1u << 20)
#define SHORTS_DEFAULT_MAX 360 /* Shorts are shown 270 px wide; 360p is plenty and lighter */

typedef struct {
    char video_id[16];
    yt_video v;
    int have_v;
    int quality;            /* max short side to pick */
    char audio_id[24];      /* chosen audio track, "" = automatic (English / original) */
    const yt_format *vf, *af;
    yt_index vidx, aidx;    /* seek indexes, for formats vidx_of / aidx_of */
    const yt_format *vidx_of, *aidx_of;
    yt_stream *vs, *as;
    media *mv, *ma;
    double start;           /* where the running streams start (seconds) */
    double audio_skip;      /* drop decoded audio before this time */
    cc_track cc;
    int have_cc;
} session;

static session g_s;

/* Rough memory needed to play at this size: download buffers, the decode-ahead frame
 * textures, up to 16 decoder frames (H.264 reference frames + output), and some slack. */
static unsigned needed_kb(int w, int h)
{
    unsigned frame = (unsigned)w * h * 3 / 2;
    return (VIDEO_BUFFER + AUDIO_BUFFER) / 1024 + VQ_SLOTS * frame / 1024 + 16u * frame / 1024 + 4096;
}

static int short_side(const yt_format *f) { return f->width && f->width < f->height ? f->width : f->height; }

/* ---- background work: open / restart ----------------------------------------------------- */

typedef struct {
    job j;
    session *s;
    int load_info;   /* look the video up first (new video) */
    double target;   /* position to start at */
    int ok;
    char err_title[64], err[512];
} start_job;

static start_job g_start;

static int ensure_index(session *s, const yt_format *f, yt_index *idx, const yt_format **of, char *err, size_t errlen)
{
    if (*of == f && idx->n > 0)
        return 0;
    yt_index_free(idx);
    *of = NULL;
    if (yt_fetch_index(&s->v, f, idx, err, errlen) != 0 || idx->n <= 0)
        return -1;
    *of = f;
    return 0;
}

static void *start_main(void *arg)
{
    start_job *o = arg;
    session *s = o->s;
    long long voff = 0, aoff = 0;
    double vt = 0, at = 0;
    char serr[256] = "";

    if (o->load_info) {
        if (yt_get_video(s->video_id, &s->v, o->err, sizeof o->err) != 0) {
            snprintf(o->err_title, sizeof o->err_title, "Could not load this video.");
            goto done;
        }
        s->have_v = 1;
    }
    if (yt_pick_formats2(&s->v, s->quality, s->audio_id[0] ? s->audio_id : NULL, &s->vf, &s->af) != 0) {
        snprintf(o->err_title, sizeof o->err_title, "Can't play this video.");
        snprintf(o->err, sizeof o->err, "No H.264 stream at %dp or lower. Try another quality (2 button).", s->quality);
        goto done;
    }
    if (!s->af)
        s->af = s->vf; /* muxed stream: read the same URL twice (video + audio) */
    if (needed_kb(s->vf->width, s->vf->height) > free_kb()) {
        snprintf(o->err_title, sizeof o->err_title, "Not enough memory.");
        snprintf(o->err, sizeof o->err, "%dp needs about %u KB but only %u KB are free. Try a lower quality.",
                 short_side(s->vf), needed_kb(s->vf->width, s->vf->height), free_kb());
        goto done;
    }

    /* Seeking: find the pieces of video and audio that contain the target time. */
    if (o->target > 0.5) {
        if (ensure_index(s, s->vf, &s->vidx, &s->vidx_of, serr, sizeof serr) == 0) {
            int k = yt_index_find(&s->vidx, o->target);
            vt = s->vidx.start[k];
            voff = k > 0 ? s->vidx.offset[k] : 0;
            if (s->af == s->vf) {
                aoff = voff;
                at = vt;
            } else if (voff > 0 && ensure_index(s, s->af, &s->aidx, &s->aidx_of, serr, sizeof serr) == 0) {
                int ka = yt_index_find(&s->aidx, vt);
                at = s->aidx.start[ka];
                aoff = ka > 0 ? s->aidx.offset[ka] : 0;
            } else {
                vt = 0; /* no audio index: start over rather than lose sync */
                voff = 0;
            }
        }
        if (voff == 0) {
            vt = at = 0;
            aoff = 0;
            if (serr[0])
                wlog("seek to %.1f s not possible (%s); starting from the beginning", o->target, serr);
        }
    }
    s->start = vt;
    s->audio_skip = vt;

    wlog("play %s via %s from %.1f s: video itag %d %dx%d %s, audio itag %d %s %s", s->v.video_id, s->v.client, vt,
         s->vf->itag, s->vf->width, s->vf->height, s->vf->codecs, s->af->itag, s->af->codecs, s->af->audio_id);
    s->vs = yt_stream_start_at(&s->v, s->vf, VIDEO_BUFFER, voff);
    s->as = yt_stream_start_at(&s->v, s->af, AUDIO_BUFFER, aoff);
    if (!s->vs || !s->as) {
        snprintf(o->err_title, sizeof o->err_title, "Could not start the download.");
        snprintf(o->err, sizeof o->err, "Not enough memory or system resources (details in wiitube.log).");
        goto done;
    }
    s->mv = media_open(yt_stream_ring(s->vs), MEDIA_VIDEO, o->err, sizeof o->err);
    if (!s->mv) {
        yt_stream_error(s->vs, serr, sizeof serr);
        if (serr[0])
            snprintf(o->err, sizeof o->err, "%s", serr);
        snprintf(o->err_title, sizeof o->err_title, "Could not start the video.");
        goto done;
    }
    if (vt > 0)
        media_expect_start(s->mv, vt);
    s->ma = media_open(yt_stream_ring(s->as), MEDIA_AUDIO, serr, sizeof serr);
    if (!s->ma)
        wlog("no audio: %s", serr); /* play silently rather than not at all */
    else if (at > 0)
        media_expect_start(s->ma, at);
    o->ok = 1;
done:
    o->j.done = 1;
    return NULL;
}

/* ---- captions ------------------------------------------------------------------------------ */

typedef struct {
    job j;
    const yt_video *v;
    int track, translate;
    cc_track out;
    int rc;
    char err[256];
} cc_job;

static cc_job g_cc;

static void *cc_main(void *arg)
{
    cc_job *c = arg;
    c->rc = cc_fetch(c->v, c->track, c->translate ? "en" : NULL, &c->out, c->err, sizeof c->err);
    c->j.done = 1;
    return NULL;
}

/* Wait for a job while keeping the screen alive. */
static void wait_job(job *j, const char *msg)
{
    while (job_running(j))
        draw_message(msg, NULL, 1);
    job_collect(j);
}

/* ---- clock / audio ------------------------------------------------------------------------ */

typedef struct {
    int use_audio, started, paused;
    u64 start;
    double base, paused_at;
} play_clock;

static play_clock g_clk;

static double clock_now(void *ud)
{
    play_clock *c = ud;
    if (!c->started)
        return -1;
    if (c->use_audio)
        return audio_clock();
    if (c->paused)
        return c->paused_at;
    return c->base + (double)ticks_to_microsecs(gettime() - c->start) / 1e6;
}

static void clock_pause(play_clock *c, int paused)
{
    if (c->use_audio || !c->started || paused == c->paused)
        return;
    if (paused) {
        c->paused_at = clock_now(c);
    } else {
        c->base = c->paused_at;
        c->start = gettime();
    }
    c->paused = paused;
}

static void flush_texture(void *tex, size_t len) { DCFlushRange(tex, (u32)len); }

static int16_t g_pcm[8192 * 2];

/* Decode audio into the output buffer while that won't block long on the network. After a
 * seek, audio before the video's first frame is dropped. */
static int feed_audio(session *s)
{
    ring *ar = yt_stream_ring(s->as);
    const int rate = media_sample_rate(s->ma) > 0 ? media_sample_rate(s->ma) : 44100;
    while (audio_space() >= 8192 && (ring_available(ar) >= 32768 || ring_done(ar))) {
        int frames = 0;
        double pts = 0;
        if (media_next_audio(s->ma, g_pcm, 8192, &frames, &pts) <= 0)
            return 0;
        if (s->audio_skip > 0) {
            if (pts + (double)frames / rate <= s->audio_skip)
                continue;
            if (pts < s->audio_skip) {
                int drop = (int)((s->audio_skip - pts) * rate);
                if (drop > frames)
                    drop = frames;
                memmove(g_pcm, g_pcm + 2 * drop, (size_t)(frames - drop) * 4);
                frames -= drop;
                pts += (double)drop / rate;
            }
            s->audio_skip = 0;
        }
        if (frames > 0)
            audio_push(g_pcm, frames, pts);
    }
    return 1;
}

/* ---- the player loop ------------------------------------------------------------------------ */

enum { ST_OPENING, ST_PLAYING, ST_FAILED, ST_WAITING /* for the Shorts feed's first page */ };

typedef struct {
    play_request *req;
    int is_short;             /* vertical Shorts player */
    yt_feed shorts;           /* Shorts: the queue (tapped Short, its shelf, then YouTube's feed) */
    int shorts_fails;         /* Shorts feed pages in a row that brought nothing new */
    session *s;
    player_view pv;
    vqueue *q;
    const yuv_frame *frame;
    int state, starting, audio_live, idle;
    int index, loaded_index;  /* entry shown / entry the session belongs to */
    double pending_seek;      /* seek asked for while opening */
    int short_quality;        /* Shorts: max quality for this run */
    const yt_format *tracks[PV_MAX_AUDIO];
} player;

static const yt_feed *list(const player *p) { return p->is_short ? &p->shorts : p->req->feed; }

static const yt_search_result *entry(const player *p, int i)
{
    const yt_feed *f = list(p);
    return f && i >= 0 && i < f->n ? &f->items[i] : NULL;
}

/* ---- the Shorts feed: YouTube's own sequence of Shorts, loaded in the background ---- */

typedef struct {
    job j;
    char seed[16];      /* continue from this Short; "" = start from popular Shorts */
    yt_feed result;
    int rc;
    char err[256];
} reels_job;

static reels_job g_reels;

static void *reels_main(void *arg)
{
    reels_job *r = arg;
    if (r->seed[0])
        r->rc = yt_feed_start(&r->result, r->seed, YT_FEED_REELS, r->err, sizeof r->err);
    else
        r->rc = yt_feed_start(&r->result, "#shorts", YT_FEED_ALL, r->err, sizeof r->err);
    r->j.done = 1;
    return NULL;
}

static void reels_start(player *p, const char *seed)
{
    if (g_reels.j.thread || p->shorts_fails >= 3)
        return;
    memset(&g_reels, 0, sizeof g_reels);
    snprintf(g_reels.seed, sizeof g_reels.seed, "%s", seed ? seed : "");
    if (job_start(&g_reels.j, reels_main, &g_reels) != 0)
        p->shorts_fails = 3;
}

/* Merge a finished page of Shorts into the queue; ask for more when nearing the end. */
static void reels_tick(player *p)
{
    if (job_collect(&g_reels.j)) {
        int i, before = p->shorts.n;
        if (g_reels.rc < 0)
            wlog("Shorts feed: %s", g_reels.err);
        for (i = 0; i < g_reels.result.n; i++)
            if (g_reels.result.items[i].is_short) {
                yt_search_result r = g_reels.result.items[i];
                r.shelf = 0;
                yt_feed_add(&p->shorts, &r);
            }
        yt_feed_free(&g_reels.result);
        p->shorts_fails = p->shorts.n > before ? 0 : p->shorts_fails + 1;
        wlog("Shorts feed: +%d (now %d)", p->shorts.n - before, p->shorts.n);
    }
    if (p->is_short && p->shorts.n > 0 && p->index + 4 >= p->shorts.n) {
        /* Continue from a recent Short; after a page with nothing new, from an earlier one. */
        int k = p->shorts.n - 1 - p->shorts_fails * 2;
        reels_start(p, p->shorts.items[k > 0 ? k : 0].video_id);
    }
}

/* Stop decoding and downloading (the video info and seek indexes stay). */
static void stop_streams(player *p)
{
    session *s = p->s;
    audio_stop();
    if (s->vs)
        ring_abort(yt_stream_ring(s->vs)); /* unblock the decoder if it waits for data */
    if (s->as)
        ring_abort(yt_stream_ring(s->as));
    if (p->q) {
        vq_stats st;
        vq_get_stats(p->q, &st);
        wlog("played %s at %dp: decoded %d, shown %d, dropped %d, audio underruns %d, skip level %d, "
             "avg decode %.1f ms + prepare %.1f ms per frame",
             s->video_id, s->vf ? short_side(s->vf) : 0, st.decoded, st.shown, st.late_drops, audio_underruns(),
             st.skip_level, st.decode_ms, st.convert_ms);
        vq_stop(p->q);
        p->q = NULL;
    }
    media_close(s->mv);
    media_close(s->ma);
    yt_stream_stop(s->vs, 3000);
    yt_stream_stop(s->as, 3000);
    s->mv = s->ma = NULL;
    s->vs = s->as = NULL;
    p->frame = NULL;
    memset(&g_clk, 0, sizeof g_clk);
}

/* Forget the video entirely (switching to another Short, or leaving). */
static void close_session(player *p)
{
    session *s = p->s;
    if (job_running(&g_start.j) || g_start.j.thread)
        wait_job(&g_start.j, "Closing...");
    if (job_running(&g_cc.j) || g_cc.j.thread) {
        wait_job(&g_cc.j, "Closing...");
        if (g_cc.rc == 0)
            cc_free(&g_cc.out);
    }
    stop_streams(p);
    if (s->have_cc)
        cc_free(&s->cc);
    yt_index_free(&s->vidx);
    yt_index_free(&s->aidx);
    if (s->have_v)
        yt_video_free(&s->v);
    memset(s, 0, sizeof *s);
}

static void begin_start(player *p, int load_info, double target)
{
    memset(&g_start, 0, sizeof g_start);
    g_start.s = p->s;
    g_start.load_info = load_info;
    g_start.target = target;
    p->state = ST_OPENING;
    p->pv.buffering = 1;
    p->pv.position = target;
    if (job_start(&g_start.j, start_main, &g_start) != 0) {
        p->state = ST_FAILED;
        snprintf(g_start.err_title, sizeof g_start.err_title, "Could not start playback.");
    }
}

/* Open entry i of the list as a new session. */
static void open_entry(player *p, int i)
{
    const yt_search_result *r = entry(p, i);
    session *s = p->s;
    close_session(p);
    p->index = p->loaded_index = i;
    snprintf(s->video_id, sizeof s->video_id, "%s", r->video_id);
    s->quality = p->is_short ? p->short_quality : g_settings.quality;
    {
        player_view keep = p->pv;
        player_view_init(&p->pv);
        p->pv.volume = keep.volume;
        p->pv.is_short = p->is_short;
    }
    snprintf(p->pv.title, sizeof p->pv.title, "%s", r->title);
    snprintf(p->pv.channel, sizeof p->pv.channel, "%s", r->channel);
    p->pv.quality = s->quality;
    p->pv.cc_on = 0;
    p->pv.overlay_timer = p->is_short ? 120 : 240;
    p->pending_seek = -1;
    begin_start(p, 1, 0);
}

static void start_captions(player *p)
{
    session *s = p->s;
    int translate = -1, track;
    if (job_running(&g_cc.j) || g_cc.j.thread || !s->have_v)
        return;
    track = cc_pick_english(&s->v, &translate);
    memset(&g_cc, 0, sizeof g_cc);
    g_cc.v = &s->v;
    g_cc.track = track >= 0 ? track : translate;
    g_cc.translate = track < 0;
    if (g_cc.track < 0)
        return;
    if (job_start(&g_cc.j, cc_main, &g_cc) != 0)
        return;
    snprintf(p->pv.notice, sizeof p->pv.notice, "Loading captions...");
    p->pv.notice_timer = 120;
}

static void poll_captions(player *p)
{
    session *s = p->s;
    if (!job_collect(&g_cc.j))
        return;
    if (g_cc.rc != 0) {
        wlog("captions failed: %s", g_cc.err);
        snprintf(p->pv.notice, sizeof p->pv.notice, "Couldn't load captions");
        p->pv.notice_timer = 120;
        p->pv.cc_on = 0;
        return;
    }
    if (s->have_cc)
        cc_free(&s->cc);
    s->cc = g_cc.out;
    s->have_cc = 1;
    ui_ascii(p->pv.cc_label, sizeof p->pv.cc_label, s->cc.label);
    p->pv.cc_on = g_settings.captions; /* unless turned off again while loading */
    p->pv.notice_timer = 0;
    wlog("captions: %s, %d cues", s->cc.label, s->cc.n);
}

/* The video info has arrived: fill in what the menus offer. */
static void info_ready(player *p)
{
    static const int qs[4] = { 144, 240, 360, 480 };
    session *s = p->s;
    player_view *pv = &p->pv;
    int i, last = 0, n;
    if (s->v.title[0])
        snprintf(pv->title, sizeof pv->title, "%s", s->v.title);
    if (s->v.channel[0])
        snprintf(pv->channel, sizeof pv->channel, "%s", s->v.channel);
    if (s->v.length_seconds > 0)
        pv->duration = s->v.length_seconds;
    pv->nqualities = 0;
    for (i = 0; i < 4; i++) {
        const yt_format *vf, *af;
        if (yt_pick_formats2(&s->v, qs[i], NULL, &vf, &af) == 0 && short_side(vf) > last) {
            pv->qualities[pv->nqualities++] = qs[i];
            last = short_side(vf);
        }
    }
    n = yt_audio_tracks(&s->v, p->tracks, PV_MAX_AUDIO);
    pv->naudio = n > 1 ? n : 0;
    for (i = 0; i < pv->naudio; i++)
        ui_ascii(pv->audio_names[i], sizeof pv->audio_names[i],
                 p->tracks[i]->audio_name[0] ? p->tracks[i]->audio_name : p->tracks[i]->audio_id);
    {
        int translate = -1;
        pv->cc_available = cc_pick_english(&s->v, &translate) >= 0 || translate >= 0;
    }
    if (pv->cc_available && g_settings.captions && !s->have_cc)
        start_captions(p);
}

/* Streams are open: start decoding. */
static void streams_ready(player *p)
{
    session *s = p->s;
    player_view *pv = &p->pv;
    int i;
    pv->quality = short_side(s->vf);
    pv->can_seek = s->vf->init_end > 0 && s->vf->index_end > s->vf->index_start && s->vf->content_length > 0 &&
                   (s->af == s->vf || (s->af->init_end > 0 && s->af->index_end > s->af->index_start));
    for (i = 0; i < pv->naudio; i++)
        if (strcmp(p->tracks[i]->audio_id, s->af->audio_id) == 0)
            pv->audio_sel = i;
    audio_reset(s->ma ? media_sample_rate(s->ma) : 44100);
    memset(&g_clk, 0, sizeof g_clk);
    g_clk.use_audio = s->ma != NULL;
    g_clk.base = s->start;
    p->audio_live = s->ma != NULL;
    p->q = vq_start(s->mv, clock_now, &g_clk, flush_texture, 0);
    p->starting = 1;
    p->idle = 0;
    p->state = ST_PLAYING;
    if (!p->q) {
        log_memory("video setup failed");
        p->state = ST_FAILED;
        snprintf(g_start.err_title, sizeof g_start.err_title, "Could not start the video decoder.");
        snprintf(g_start.err, sizeof g_start.err, "Not enough memory (details in wiitube.log).");
    }
}

static double current_time(player *p)
{
    double t = p->state == ST_PLAYING && !p->starting ? clock_now(&g_clk) : p->pv.position;
    return t > 0 ? t : 0;
}

static void restart_at(player *p, double t)
{
    stop_streams(p);
    p->pv.paused = 0;
    begin_start(p, 0, t);
}

int player_run(play_request *req)
{
    static player pl;
    player *p = &pl;
    ui_input in;

    memset(p, 0, sizeof *p);
    p->req = req;
    p->s = &g_s;
    memset(&g_s, 0, sizeof g_s);
    memset(&g_reels, 0, sizeof g_reels);
    p->short_quality = g_settings.quality < SHORTS_DEFAULT_MAX ? g_settings.quality : SHORTS_DEFAULT_MAX;
    player_view_init(&p->pv);
    if (!req->feed) {
        /* Shorts tab: YouTube's Shorts feed, from the last Short watched or popular Shorts. */
        p->is_short = 1;
        p->pv.is_short = 1;
        p->state = ST_WAITING;
        p->pv.buffering = 1;
        reels_start(p, req->shorts_from);
    } else {
        const yt_search_result *r = req->index >= 0 && req->index < req->feed->n ? &req->feed->items[req->index] : NULL;
        if (!r)
            return req->index;
        p->is_short = r->is_short;
        if (p->is_short) {
            /* Like YouTube: the Short, the rest of its shelf, then the Shorts feed after it. */
            int i;
            yt_search_result first = *r;
            first.shelf = 0;
            yt_feed_add(&p->shorts, &first);
            for (i = req->index + 1; r->shelf > 0 && i < req->feed->n && req->feed->items[i].shelf == r->shelf; i++) {
                yt_search_result e = req->feed->items[i];
                e.shelf = 0;
                yt_feed_add(&p->shorts, &e);
            }
            reels_start(p, r->video_id);
            open_entry(p, 0);
        } else {
            open_entry(p, req->index);
        }
    }

    while (!g_quit) {
        player_view *pv = &p->pv;
        session *s = p->s;
        int act, leave = 0;
        double now;

        read_input(&in);
        pv->has_prev = p->is_short && p->index > 0;
        pv->has_next = p->is_short && p->index + 1 < p->shorts.n;
        act = player_update(pv, &in);

        switch (act) {
        case ACT_PLAYER_BACK:
            leave = 1;
            break;
        case ACT_PLAYER_PAUSE:
            if (p->state == ST_PLAYING && !p->starting) {
                pv->paused = !pv->paused;
                audio_pause(pv->paused);
                clock_pause(&g_clk, pv->paused);
            }
            break;
        case ACT_PLAYER_VOLUME:
            audio_set_volume(pv->volume);
            break;
        case ACT_PLAYER_SEEK:
            if (p->state == ST_PLAYING)
                restart_at(p, pv->seek_to);
            else if (p->state == ST_OPENING)
                p->pending_seek = pv->seek_to;
            break;
        case ACT_PLAYER_CAPTIONS:
            if (pv->cc_on) {
                pv->cc_on = 0;
                pv->cc_text[0] = 0;
                snprintf(pv->notice, sizeof pv->notice, "Captions off");
                pv->notice_timer = 90;
                g_settings.captions = 0;
            } else {
                g_settings.captions = 1;
                if (s->have_cc) {
                    pv->cc_on = 1;
                    snprintf(pv->notice, sizeof pv->notice, "Captions on");
                    pv->notice_timer = 90;
                } else {
                    start_captions(p);
                }
            }
            settings_save();
            break;
        case ACT_PLAYER_SET_QUALITY:
            if (p->state == ST_OPENING) {
                snprintf(pv->notice, sizeof pv->notice, "Wait for the video to start");
                pv->notice_timer = 90;
                break;
            }
            if (p->is_short) {
                p->short_quality = pv->want_quality;
            } else {
                g_settings.quality = pv->want_quality;
                settings_save();
            }
            s->quality = pv->want_quality;
            snprintf(pv->notice, sizeof pv->notice, "Quality: %dp", pv->want_quality);
            pv->notice_timer = 90;
            if (p->state == ST_PLAYING)
                restart_at(p, current_time(p));
            break;
        case ACT_PLAYER_SET_AUDIO:
            if (p->state == ST_OPENING) {
                snprintf(pv->notice, sizeof pv->notice, "Wait for the video to start");
                pv->notice_timer = 90;
            } else if (pv->want_audio >= 0 && pv->want_audio < pv->naudio) {
                snprintf(s->audio_id, sizeof s->audio_id, "%s", p->tracks[pv->want_audio]->audio_id);
                snprintf(pv->notice, sizeof pv->notice, "Audio: %.48s", pv->audio_names[pv->want_audio]);
                pv->notice_timer = 90;
                if (p->state == ST_PLAYING)
                    restart_at(p, current_time(p));
            }
            break;
        case ACT_PLAYER_NEXT:
        case ACT_PLAYER_PREV: {
            const int to = p->index + (act == ACT_PLAYER_NEXT ? 1 : -1);
            const yt_search_result *r = entry(p, to);
            if (!r)
                break;
            p->index = to;
            if (p->state == ST_WAITING)
                break;
            if (p->state == ST_OPENING) { /* switch once the current lookup finishes */
                snprintf(pv->title, sizeof pv->title, "%s", r->title);
                snprintf(pv->channel, sizeof pv->channel, "%s", r->channel);
            } else {
                open_entry(p, to);
            }
            break;
        }
        default:
            break;
        }
        if (leave)
            break;

        /* Background work. */
        if (req->tick)
            req->tick(req->tick_ud);
        reels_tick(p);
        if (p->state == ST_WAITING) {
            if (p->shorts.n > 0) {
                open_entry(p, 0);
            } else if (!g_reels.j.thread && p->shorts_fails > 0) {
                error_screen("Couldn't load Shorts.", "Check the connection and try again.");
                break;
            }
        }
        poll_captions(p);
        if (p->state == ST_OPENING && job_collect(&g_start.j)) {
            if (p->index != p->loaded_index) {
                open_entry(p, p->index);
            } else if (!g_start.ok) {
                p->state = ST_FAILED;
                stop_streams(p);
                wlog("error: %s: %s", g_start.err_title, g_start.err);
                if (!p->is_short) {
                    error_screen(g_start.err_title[0] ? g_start.err_title : "Could not play this video.", g_start.err);
                    break;
                }
            } else {
                if (g_start.load_info) {
                    info_ready(p);
                    history_add(s->video_id, p->is_short);
                }
                streams_ready(p);
                if (p->state == ST_PLAYING && p->pending_seek >= 0) {
                    double t = p->pending_seek;
                    p->pending_seek = -1;
                    restart_at(p, t);
                }
            }
        }

        /* Playing. */
        now = -1;
        if (p->state == ST_PLAYING) {
            const yuv_frame *next;
            if (p->audio_live && !pv->paused)
                p->audio_live = feed_audio(s);
            if (p->starting && (vq_ready(p->q) > 0 || vq_finished(p->q)) &&
                (!p->audio_live || audio_buffered() >= 22050)) {
                p->starting = 0;
                if (g_clk.use_audio)
                    audio_play();
                g_clk.start = gettime();
                g_clk.started = 1;
            }
            now = clock_now(&g_clk);
            if (!p->starting && !pv->paused && vq_poll(p->q, now, &next)) {
                p->frame = next;
                p->idle = 0;
            } else if (!pv->paused) {
                p->idle++;
            }
            if (now > 0)
                pv->position = now;
            pv->buffered = s->vf->content_length > 0 ? (float)yt_stream_position(s->vs) / s->vf->content_length : 0;
            pv->buffering = p->starting || (p->idle > 45 && vq_ready(p->q) == 0 && !vq_finished(p->q));
            if (s->have_cc && pv->cc_on) {
                char raw[512];
                const char *t = cc_text_at(&s->cc, pv->position, raw, sizeof raw);
                ui_ascii(pv->cc_text, sizeof pv->cc_text, t ? t : "");
            }
            /* The end. */
            if (!p->starting && !pv->paused && vq_finished(p->q) &&
                (!g_clk.use_audio || (!p->audio_live && audio_buffered() == 0))) {
                if (!p->is_short)
                    break;
                restart_at(p, 0); /* Shorts loop */
            }
        } else if (p->state == ST_FAILED) {
            pv->buffering = 0;
            snprintf(pv->notice, sizeof pv->notice, "%s", g_start.err_title[0] ? g_start.err_title : "Can't play this video.");
            pv->notice_timer = 40;
        }

        gfx_frame_begin();
        dr_fill(0, 0, ui_width(), UI_H, p->is_short ? C_BG : 0x000000ffu);
        if (p->is_short) {
            const float cw = (float)(int)(UI_H * 9 / 16);
            dr_fill((ui_width() - cw) / 2, 0, cw, UI_H, 0x000000ffu);
        }
        if (p->frame) {
            float x, y, w, h;
            player_video_rect(pv, p->frame->w, p->frame->h, &x, &y, &w, &h);
            dr_video(p->frame, x, y, w, h); /* the GPU converts YUV to RGB */
        }
        player_draw_overlay(pv);
        if (in.pointer && (pv->overlay_timer > 0 || pv->menu != MENU_CLOSED || p->is_short))
            ui_cursor(in.px, in.py);
        gfx_frame_end();
        g_frame++;
    }

    close_session(p);
    if (g_reels.j.thread)
        wait_job(&g_reels.j, "Closing...");
    yt_feed_free(&g_reels.result);
    yt_feed_free(&p->shorts);
    log_memory("after playback");
    if (!req->feed)
        return -1;
    return p->is_short ? req->index : p->index;
}
