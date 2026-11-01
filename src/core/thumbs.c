#include "thumbs.h"
#include "http.h"
#include "log.h"
#include "thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { T_EMPTY, T_QUEUED, T_LOADING, T_READY, T_FAILED };

typedef struct {
    char id[24];      /* cache key */
    char url[320];    /* explicit URL, "" = url_fmt with the key */
    int max_w;
    int state;
    unsigned last_used; /* frame number */
    wt_image *img;
} entry;

#define MAX_WORKERS 4

struct thumbs {
    char url_fmt[128];
    int max_w, cap, nthreads;
    entry *e;
    unsigned frame;
    int stop, arrived;
    wt_mutex *mu;
    wt_cond *cv;
    wt_thread *workers[MAX_WORKERS];
};

static entry *find(thumbs *t, const char *id)
{
    int i;
    for (i = 0; i < t->cap; i++)
        if (t->e[i].state != T_EMPTY && strcmp(t->e[i].id, id) == 0)
            return &t->e[i];
    return NULL;
}

static void *worker(void *arg)
{
    thumbs *t = arg;
    for (;;) {
        entry *job = NULL;
        char id[24], url[320], err[256];
        int max_w;
        http_request req;
        http_response resp;
        wt_image *img = NULL;
        int i;

        wt_mutex_lock(t->mu);
        while (!t->stop) {
            for (i = 0; i < t->cap; i++)
                if (t->e[i].state == T_QUEUED && (!job || t->e[i].last_used > job->last_used))
                    job = &t->e[i];
            if (job)
                break;
            wt_cond_wait(t->cv, t->mu);
        }
        if (t->stop) {
            wt_mutex_unlock(t->mu);
            break;
        }
        job->state = T_LOADING;
        memcpy(id, job->id, sizeof id);
        if (job->url[0])
            memcpy(url, job->url, sizeof url);
        else
            snprintf(url, sizeof url, t->url_fmt, id);
        max_w = job->max_w;
        wt_mutex_unlock(t->mu);

        memset(&req, 0, sizeof req);
        req.url = url;
        req.headers = "User-Agent: Mozilla/5.0\r\n";
        req.max_buffer = 512 * 1024;
        if (http_do(&req, &resp, err, sizeof err) == 0 && resp.status == 200 && resp.body)
            img = image_from_jpeg(resp.body, resp.body_len, max_w);
        else
            wlog("thumbs: %s failed (%s, HTTP %d)", id, err, resp.status);
        http_response_free(&resp);

        wt_mutex_lock(t->mu);
        job->img = img;
        job->state = img ? T_READY : T_FAILED;
        t->arrived = 1;
        wt_mutex_unlock(t->mu);
    }
    return NULL;
}

thumbs *thumbs_new(const char *url_fmt, int max_w, int capacity, int nthreads)
{
    thumbs *t = calloc(1, sizeof *t);
    int i;
    if (!t)
        return NULL;
    snprintf(t->url_fmt, sizeof t->url_fmt, "%s", url_fmt);
    t->max_w = max_w;
    t->cap = capacity;
    t->frame = 2;
    t->e = calloc((size_t)capacity, sizeof *t->e);
    t->mu = wt_mutex_new();
    t->cv = wt_cond_new();
    if (!t->e || !t->mu || !t->cv) {
        thumbs_free(t);
        return NULL;
    }
    if (nthreads > MAX_WORKERS)
        nthreads = MAX_WORKERS;
    for (i = 0; i < nthreads; i++)
        if ((t->workers[i] = wt_thread_start(worker, t, 128 * 1024, 0)) != NULL)
            t->nthreads++;
    return t;
}

void thumbs_free(thumbs *t)
{
    int i;
    if (!t)
        return;
    if (t->mu) {
        wt_mutex_lock(t->mu);
        t->stop = 1;
        wt_cond_broadcast(t->cv);
        wt_mutex_unlock(t->mu);
    }
    for (i = 0; i < MAX_WORKERS; i++)
        wt_thread_join(t->workers[i]);
    if (t->e)
        for (i = 0; i < t->cap; i++)
            image_free(t->e[i].img);
    free(t->e);
    wt_mutex_free(t->mu);
    wt_cond_free(t->cv);
    free(t);
}

const wt_image *thumbs_get(thumbs *t, const char *video_id)
{
    return thumbs_get_url(t, video_id, NULL, 0);
}

const wt_image *thumbs_get_url(thumbs *t, const char *key, const char *url, int max_w)
{
    entry *e;
    const wt_image *img = NULL;
    if (!t || !key || !key[0])
        return NULL;
    wt_mutex_lock(t->mu);
    e = find(t, key);
    if (!e) {
        /* Use an empty slot, else evict the least recently used finished thumbnail that
         * was not drawn in the last two frames. */
        int i;
        for (i = 0; i < t->cap; i++) {
            entry *c = &t->e[i];
            if (c->state == T_EMPTY) {
                e = c;
                break;
            }
            if ((c->state == T_READY || c->state == T_FAILED || c->state == T_QUEUED) &&
                c->last_used + 2 < t->frame && (!e || c->last_used < e->last_used))
                e = c;
        }
        if (e) {
            image_free(e->img);
            memset(e, 0, sizeof *e);
            snprintf(e->id, sizeof e->id, "%s", key);
            snprintf(e->url, sizeof e->url, "%s", url ? url : "");
            e->max_w = max_w > 0 ? max_w : t->max_w;
            e->state = T_QUEUED;
            wt_cond_broadcast(t->cv);
        }
    }
    if (e) {
        e->last_used = t->frame;
        if (e->state == T_READY)
            img = e->img;
    }
    wt_mutex_unlock(t->mu);
    return img;
}

int thumbs_frame_done(thumbs *t)
{
    int arrived;
    if (!t)
        return 0;
    wt_mutex_lock(t->mu);
    t->frame++;
    arrived = t->arrived;
    t->arrived = 0;
    wt_mutex_unlock(t->mu);
    return arrived;
}
