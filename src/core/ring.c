#include "ring.h"
#include "thread.h"

#include <stdlib.h>
#include <string.h>

struct ring {
    unsigned char *buf;
    size_t cap, head, tail, fill; /* write at head, read at tail */
    int done, error, aborted;
    wt_mutex *mu;
    wt_cond *cv;
};

ring *ring_new(size_t capacity)
{
    ring *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    r->buf = malloc(capacity);
    r->mu = wt_mutex_new();
    r->cv = wt_cond_new();
    if (!r->buf || !r->mu || !r->cv) {
        ring_free(r);
        return NULL;
    }
    r->cap = capacity;
    return r;
}

void ring_free(ring *r)
{
    if (!r)
        return;
    free(r->buf);
    wt_mutex_free(r->mu);
    wt_cond_free(r->cv);
    free(r);
}

int ring_write(ring *r, const void *data, size_t len)
{
    const unsigned char *p = data;
    wt_mutex_lock(r->mu);
    while (len > 0) {
        size_t space, chunk;
        while (r->fill == r->cap && !r->aborted)
            wt_cond_wait(r->cv, r->mu);
        if (r->aborted) {
            wt_mutex_unlock(r->mu);
            return -1;
        }
        space = r->cap - r->fill;
        chunk = len < space ? len : space;
        if (chunk > r->cap - r->head)
            chunk = r->cap - r->head; /* up to the wrap point */
        memcpy(r->buf + r->head, p, chunk);
        r->head = (r->head + chunk) % r->cap;
        r->fill += chunk;
        p += chunk;
        len -= chunk;
        wt_cond_broadcast(r->cv);
    }
    wt_mutex_unlock(r->mu);
    return 0;
}

void ring_finish(ring *r, int error)
{
    wt_mutex_lock(r->mu);
    r->done = 1;
    r->error = error;
    wt_cond_broadcast(r->cv);
    wt_mutex_unlock(r->mu);
}

size_t ring_read(ring *r, void *out, size_t len)
{
    unsigned char *p = out;
    size_t got = 0;
    wt_mutex_lock(r->mu);
    while (r->fill == 0 && !r->done && !r->aborted)
        wt_cond_wait(r->cv, r->mu);
    while (got < len && r->fill > 0 && !r->aborted) {
        size_t chunk = len - got;
        if (chunk > r->fill)
            chunk = r->fill;
        if (chunk > r->cap - r->tail)
            chunk = r->cap - r->tail;
        memcpy(p + got, r->buf + r->tail, chunk);
        r->tail = (r->tail + chunk) % r->cap;
        r->fill -= chunk;
        got += chunk;
    }
    wt_cond_broadcast(r->cv);
    wt_mutex_unlock(r->mu);
    return got;
}

void ring_abort(ring *r)
{
    wt_mutex_lock(r->mu);
    r->aborted = 1;
    wt_cond_broadcast(r->cv);
    wt_mutex_unlock(r->mu);
}

size_t ring_available(ring *r)
{
    size_t n;
    wt_mutex_lock(r->mu);
    n = r->fill;
    wt_mutex_unlock(r->mu);
    return n;
}

int ring_done(ring *r)
{
    int d;
    wt_mutex_lock(r->mu);
    d = r->done;
    wt_mutex_unlock(r->mu);
    return d;
}

int ring_error(ring *r)
{
    int e;
    wt_mutex_lock(r->mu);
    e = r->error;
    wt_mutex_unlock(r->mu);
    return e;
}

size_t ring_capacity(const ring *r) { return r->cap; }
