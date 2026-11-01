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
    uint16_t *tex; /* 32-byte aligned for the GPU */
    double pts;
    int state;
} slot;

struct vqueue {
    media *m;
    vq_clock_fn clock;
    void *clock_ud;
    vq_flush_fn flush;
    int base_skip, skip;
    int fw, fh, tw, th;
    size_t tex_bytes;
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

        wt_mutex_lock(q->mu);
        while (!q->stop && q->slots[q->wr].state != SLOT_FREE)
            wt_cond_wait(q->cv, q->mu);
        if (q->stop) {
            wt_mutex_unlock(q->mu);
            break;
        }
        s = &q->slots[q->wr];
        wt_mutex_unlock(q->mu);

        rc = media_next_video(q->m, &fr);
        if (rc <= 0) {
            if (rc < 0)
                wlog("video: decode failed (%d)", rc);
            break;
        }
        now = q->clock(q->clock_ud);
        late = now >= 0 ? now - fr.pts : 0;
        wt_mutex_lock(q->mu);
        q->st.decoded++;
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
        conv_yuv420_to_rgb565_tiled(fr.plane[0], fr.stride[0], fr.plane[1], fr.stride[1], fr.plane[2],
                                    fr.stride[2], fr.width, fr.height, fr.bt709, s->tex);
        if (q->flush)
            q->flush(s->tex, q->tex_bytes);

        wt_mutex_lock(q->mu);
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
    q->tw = conv_tex_dim(q->fw);
    q->th = conv_tex_dim(q->fh);
    q->tex_bytes = (size_t)q->tw * q->th * 2;
    q->shown = -1;
    q->mu = wt_mutex_new();
    q->cv = wt_cond_new();
    for (i = 0; i < NSLOTS; i++) {
        q->slots[i].raw = malloc(q->tex_bytes + 32);
        if (!q->slots[i].raw)
            break;
        q->slots[i].tex = (uint16_t *)(((uintptr_t)q->slots[i].raw + 31) & ~(uintptr_t)31);
    }
    if (i < NSLOTS || !q->mu || !q->cv) {
        wlog("video: setup failed (%d/%d textures of %u bytes, mutex %s, cond %s)", i, NSLOTS,
             (unsigned)q->tex_bytes, q->mu ? "ok" : "FAILED", q->cv ? "ok" : "FAILED");
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

int vq_poll(vqueue *q, double now, const uint16_t **tex)
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
        *tex = q->slots[q->rd].tex;
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

int vq_tex_width(const vqueue *q) { return q->tw; }
int vq_tex_height(const vqueue *q) { return q->th; }
int vq_frame_width(const vqueue *q) { return q->fw; }
int vq_frame_height(const vqueue *q) { return q->fh; }

void vq_get_stats(vqueue *q, vq_stats *out)
{
    wt_mutex_lock(q->mu);
    *out = q->st;
    out->skip_level = q->skip;
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
