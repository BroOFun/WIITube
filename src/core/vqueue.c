#include "vqueue.h"
#include "convert.h"
#include "log.h"
#include "thread.h"

#include <stdlib.h>
#include <string.h>

#define NSLOTS 3
#define LATE_DROP 0.25  /* seconds behind the clock: don't bother converting */
#define LATE_SKIP1 0.10 /* start skipping deblocking */
#define LATE_SKIP2 0.40 /* also skip non-reference frames */
#define CAUGHT_UP 0.02

enum { SLOT_FREE, SLOT_READY, SLOT_SHOWN };

typedef struct {
    void *raw;
    yuv_frame f;   /* planes point into raw, each 32-byte aligned for the GPU */
    double pts;
    int state;
} slot;

struct vqueue {
    media *m;
    vq_clock_fn clock;
    void *clock_ud;
    vq_flush_fn flush;
    int base_skip, skip;
    int fw, fh;
    size_t ybytes, cbytes;
    uint64_t decode_us, convert_us; /* totals, for the averages in vq_stats */
    int converted;
    slot slots[NSLOTS];
    int wr, rd, shown;
    int stop, eof;
    wt_mutex *mu;
    wt_cond *cv;
    wt_thread *thread;
    vq_stats st;
};

static void set_skip(vqueue *q, int level)
{
    if (level < q->base_skip)
        level = q->base_skip;
    if (level != q->skip) {
        media_set_skip(q->m, level);
        wlog("video: skip level %d -> %d", q->skip, level);
        q->skip = level;
    }
}

static void *worker(void *arg)
{
    vqueue *q = arg;
    media_frame fr;

    for (;;) {
        slot *s;
        double now, late;
        int rc;
        uint64_t t0, t1, t2;

        wt_mutex_lock(q->mu);
        while (!q->stop && q->slots[q->wr].state != SLOT_FREE)
            wt_cond_wait(q->cv, q->mu);
        if (q->stop) {
            wt_mutex_unlock(q->mu);
            break;
        }
        s = &q->slots[q->wr];
        wt_mutex_unlock(q->mu);

        t0 = wt_time_us();
        rc = media_next_video(q->m, &fr);
        t1 = wt_time_us();
        if (rc <= 0) {
            if (rc < 0)
                wlog("video: decode failed (%d)", rc);
            break;
        }
        now = q->clock(q->clock_ud);
        late = now >= 0 ? now - fr.pts : 0;
        wt_mutex_lock(q->mu);
        q->st.decoded++;
        q->decode_us += t1 - t0;
        if (late > LATE_DROP)
            q->st.late_drops++;
        wt_mutex_unlock(q->mu);
        if (late > LATE_SKIP2)
            set_skip(q, 2);
        else if (late > LATE_SKIP1)
            set_skip(q, q->skip > 1 ? q->skip : 1);
        else if (late < CAUGHT_UP)
            set_skip(q, 0);
        if (late > LATE_DROP)
            continue;
        if (fr.width != q->fw || fr.height != q->fh) {
            wlog("video: frame size changed to %dx%d, skipping", fr.width, fr.height);
            continue;
        }
        /* Only rearrange the planes into GPU tiles; the GPU converts YUV to RGB. */
        conv_plane_to_i8(fr.plane[0], fr.stride[0], fr.width, fr.height, (uint8_t *)s->f.y);
        conv_plane_to_i8(fr.plane[1], fr.stride[1], s->f.cw, s->f.ch, (uint8_t *)s->f.u);
        conv_plane_to_i8(fr.plane[2], fr.stride[2], s->f.cw, s->f.ch, (uint8_t *)s->f.v);
        s->f.bt709 = fr.bt709;
        if (q->flush) {
            q->flush((void *)s->f.y, q->ybytes);
            q->flush((void *)s->f.u, q->cbytes);
            q->flush((void *)s->f.v, q->cbytes);
        }
        t2 = wt_time_us();

        wt_mutex_lock(q->mu);
        q->convert_us += t2 - t1;
        q->converted++;
        s->pts = fr.pts;
        s->state = SLOT_READY;
        q->wr = (q->wr + 1) % NSLOTS;
        wt_cond_broadcast(q->cv);
        wt_mutex_unlock(q->mu);
    }
    wt_mutex_lock(q->mu);
    q->eof = 1;
    q->st.skip_level = q->skip;
    wt_cond_broadcast(q->cv);
    wt_mutex_unlock(q->mu);
    return NULL;
}

vqueue *vq_start(media *m, vq_clock_fn clock, void *clock_ud, vq_flush_fn flush, int base_skip)
{
    vqueue *q = calloc(1, sizeof *q);
    int i;
    if (!q)
        return NULL;
    q->m = m;
    q->clock = clock;
    q->clock_ud = clock_ud;
    q->flush = flush;
    q->base_skip = base_skip;
    q->skip = -1;
    q->fw = media_width(m);
    q->fh = media_height(m);
    q->shown = -1;
    q->mu = wt_mutex_new();
    q->cv = wt_cond_new();
    for (i = 0; i < NSLOTS; i++) {
        yuv_frame *f = &q->slots[i].f;
        uintptr_t p;
        f->w = q->fw;
        f->h = q->fh;
        f->yw = conv_i8_w(f->w);
        f->yh = conv_i8_h(f->h);
        f->cw = (f->w + 1) / 2;
        f->ch = (f->h + 1) / 2;
        f->cwt = conv_i8_w(f->cw);
        f->cht = conv_i8_h(f->ch);
        q->ybytes = (size_t)f->yw * f->yh;
        q->cbytes = (size_t)f->cwt * f->cht;
        q->slots[i].raw = malloc(q->ybytes + 2 * q->cbytes + 3 * 32);
        if (!q->slots[i].raw)
            break;
        p = ((uintptr_t)q->slots[i].raw + 31) & ~(uintptr_t)31;
        f->y = (const uint8_t *)p;
        p = (p + q->ybytes + 31) & ~(uintptr_t)31;
        f->u = (const uint8_t *)p;
        p = (p + q->cbytes + 31) & ~(uintptr_t)31;
        f->v = (const uint8_t *)p;
    }
    if (i < NSLOTS || !q->mu || !q->cv) {
        wlog("video: setup failed (%d/%d frame buffers of %u bytes, mutex %s, cond %s)", i, NSLOTS,
             (unsigned)(q->ybytes + 2 * q->cbytes), q->mu ? "ok" : "FAILED", q->cv ? "ok" : "FAILED");
        q->thread = NULL;
        vq_stop(q);
        return NULL;
    }
    set_skip(q, base_skip);
    q->thread = wt_thread_start(worker, q, 256 * 1024, 0);
    if (!q->thread) {
        wlog("video: could not start decoder thread");
        vq_stop(q);
        return NULL;
    }
    return q;
}

int vq_poll(vqueue *q, double now, const yuv_frame **frame)
{
    int changed = 0;
    wt_mutex_lock(q->mu);
    while (q->slots[q->rd].state == SLOT_READY && q->slots[q->rd].pts <= now) {
        int next = (q->rd + 1) % NSLOTS;
        if (q->slots[next].state == SLOT_READY && q->slots[next].pts <= now) {
            /* A newer frame is also due: skip this one. */
            q->slots[q->rd].state = SLOT_FREE;
            q->st.late_drops++;
            q->rd = next;
            continue;
        }
        if (q->shown >= 0)
            q->slots[q->shown].state = SLOT_FREE;
        q->shown = q->rd;
        q->slots[q->rd].state = SLOT_SHOWN;
        *frame = &q->slots[q->rd].f;
        q->rd = next;
        q->st.shown++;
        changed = 1;
        break;
    }
    if (changed)
        wt_cond_broadcast(q->cv);
    wt_mutex_unlock(q->mu);
    return changed;
}

int vq_ready(vqueue *q)
{
    int i, n = 0;
    wt_mutex_lock(q->mu);
    for (i = 0; i < NSLOTS; i++)
        n += q->slots[i].state == SLOT_READY;
    wt_mutex_unlock(q->mu);
    return n;
}

int vq_finished(vqueue *q)
{
    int i, done;
    wt_mutex_lock(q->mu);
    done = q->eof;
    for (i = 0; i < NSLOTS; i++)
        if (q->slots[i].state == SLOT_READY)
            done = 0;
    wt_mutex_unlock(q->mu);
    return done;
}

int vq_frame_width(const vqueue *q) { return q->fw; }
int vq_frame_height(const vqueue *q) { return q->fh; }

void vq_get_stats(vqueue *q, vq_stats *out)
{
    wt_mutex_lock(q->mu);
    *out = q->st;
    out->skip_level = q->skip;
    out->decode_ms = q->st.decoded ? q->decode_us / 1000.0 / q->st.decoded : 0;
    out->convert_ms = q->converted ? q->convert_us / 1000.0 / q->converted : 0;
    wt_mutex_unlock(q->mu);
}

void vq_stop(vqueue *q)
{
    int i;
    if (!q)
        return;
    if (q->thread) {
        wt_mutex_lock(q->mu);
        q->stop = 1;
        wt_cond_broadcast(q->cv);
        wt_mutex_unlock(q->mu);
        /* The worker may be blocked reading the network inside media_next_video; the
         * caller aborts the stream's ring first so that read returns. */
        wt_thread_join(q->thread);
    }
    for (i = 0; i < NSLOTS; i++)
        free(q->slots[i].raw);
    wt_mutex_free(q->mu);
    wt_cond_free(q->cv);
    free(q);
}
