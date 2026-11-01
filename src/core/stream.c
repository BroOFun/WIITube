#include "stream.h"
#include "log.h"
#include "thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct yt_stream {
    char *url;
    char user_agent[256];
    long long total;
    long long init_end, offset; /* seeking: bytes [0, init_end] then [offset, total) */
    ring *r;
    wt_thread *thread;
    wt_mutex *mu;
    int refs;          /* owner + thread; whoever drops the last one frees */
    int finished;
    long long received;
    char err[256];
};

static void release(yt_stream *s)
{
    int last;
    wt_mutex_lock(s->mu);
    last = --s->refs == 0;
    wt_mutex_unlock(s->mu);
    if (!last)
        return;
    ring_free(s->r);
    free(s->url);
    wt_mutex_free(s->mu);
    free(s);
}

static int on_data(void *ud, const unsigned char *data, size_t len)
{
    yt_stream *s = ud;
    if (ring_write(s->r, data, len) != 0)
        return -1; /* consumer aborted */
    wt_mutex_lock(s->mu);
    s->received += (long long)len;
    wt_mutex_unlock(s->mu);
    return 0;
}

static void *stream_main(void *arg)
{
    yt_stream *s = arg;
    char err[256] = "";
    int rc;
    if (s->offset > 0)
        rc = yt_download_span(s->url, 0, s->init_end + 1, s->user_agent, on_data, s, err, sizeof err);
    else
        rc = 0;
    if (rc == 0)
        rc = yt_download_span(s->url, s->offset, s->total, s->user_agent, on_data, s, err, sizeof err);

    wt_mutex_lock(s->mu);
    if (rc != 0)
        snprintf(s->err, sizeof s->err, "%s", err[0] ? err : "download failed");
    s->finished = 1;
    wt_mutex_unlock(s->mu);
    ring_finish(s->r, rc != 0);
    release(s);
    return NULL;
}

yt_stream *yt_stream_start(const yt_video *v, const yt_format *f, size_t buffer_size)
{
    return yt_stream_start_at(v, f, buffer_size, 0);
}

yt_stream *yt_stream_start_at(const yt_video *v, const yt_format *f, size_t buffer_size, long long offset)
{
    yt_stream *s;
    if (!f->url)
        return NULL;
    s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->url = strdup(f->url);
    s->total = f->content_length;
    if (offset > 0 && f->init_end > 0 && offset > f->init_end && (f->content_length < 0 || offset < f->content_length)) {
        s->init_end = f->init_end;
        s->offset = offset;
    }
    snprintf(s->user_agent, sizeof s->user_agent, "%s", v->user_agent);
    s->r = ring_new(buffer_size);
    s->mu = wt_mutex_new();
    if (!s->url || !s->r || !s->mu) {
        wlog("stream: setup failed (url %s, %u-byte buffer %s, mutex %s)", s->url ? "ok" : "FAILED",
             (unsigned)buffer_size, s->r ? "ok" : "FAILED", s->mu ? "ok" : "FAILED");
        free(s->url);
        ring_free(s->r);
        wt_mutex_free(s->mu);
        free(s);
        return NULL;
    }
    s->refs = 2;
    /* TLS handshakes need a fair amount of stack. */
    s->thread = wt_thread_start(stream_main, s, 128 * 1024, 1);
    if (!s->thread) {
        wlog("stream: could not start download thread");
        s->refs = 1;
        release(s);
        return NULL;
    }
    return s;
}

ring *yt_stream_ring(yt_stream *s) { return s->r; }

long long yt_stream_received(yt_stream *s)
{
    long long n;
    wt_mutex_lock(s->mu);
    n = s->received;
    wt_mutex_unlock(s->mu);
    return n;
}

long long yt_stream_position(yt_stream *s)
{
    long long n = yt_stream_received(s);
    if (s->offset > 0)
        n = n > s->init_end + 1 ? s->offset + n - (s->init_end + 1) : s->offset;
    return n;
}

void yt_stream_error(yt_stream *s, char *out, size_t outlen)
{
    wt_mutex_lock(s->mu);
    snprintf(out, outlen, "%s", s->err);
    wt_mutex_unlock(s->mu);
}

void yt_stream_stop(yt_stream *s, int timeout_ms)
{
    int waited = 0, finished;
    if (!s)
        return;
    ring_abort(s->r);
    for (;;) {
        wt_mutex_lock(s->mu);
        finished = s->finished;
        wt_mutex_unlock(s->mu);
        if (finished || waited >= timeout_ms)
            break;
        wt_sleep_ms(10);
        waited += 10;
    }
    if (finished)
        wt_thread_join(s->thread);
    else
        wt_thread_detach(s->thread);
    release(s);
}
