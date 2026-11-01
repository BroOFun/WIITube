/* pthreads implementation of thread.h for the desktop build. */
#include "../../core/thread.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct wt_mutex { pthread_mutex_t m; };
struct wt_cond { pthread_cond_t c; };
struct wt_thread { pthread_t t; };

wt_mutex *wt_mutex_new(void)
{
    wt_mutex *m = malloc(sizeof *m);
    if (m)
        pthread_mutex_init(&m->m, NULL);
    return m;
}
void wt_mutex_free(wt_mutex *m) { if (m) { pthread_mutex_destroy(&m->m); free(m); } }
void wt_mutex_lock(wt_mutex *m) { pthread_mutex_lock(&m->m); }
void wt_mutex_unlock(wt_mutex *m) { pthread_mutex_unlock(&m->m); }

wt_cond *wt_cond_new(void)
{
    wt_cond *c = malloc(sizeof *c);
    if (c)
        pthread_cond_init(&c->c, NULL);
    return c;
}
void wt_cond_free(wt_cond *c) { if (c) { pthread_cond_destroy(&c->c); free(c); } }
void wt_cond_wait(wt_cond *c, wt_mutex *m) { pthread_cond_wait(&c->c, &m->m); }
void wt_cond_broadcast(wt_cond *c) { pthread_cond_broadcast(&c->c); }

wt_thread *wt_thread_start(void *(*fn)(void *), void *arg, size_t stack_size, int high_priority)
{
    wt_thread *t = malloc(sizeof *t);
    pthread_attr_t attr;
    (void)high_priority;
    if (!t)
        return NULL;
    pthread_attr_init(&attr);
    if (stack_size)
        pthread_attr_setstacksize(&attr, stack_size < 65536 ? 65536 : stack_size);
    if (pthread_create(&t->t, &attr, fn, arg) != 0) {
        pthread_attr_destroy(&attr);
        free(t);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    return t;
}

void wt_thread_join(wt_thread *t) { if (t) { pthread_join(t->t, NULL); free(t); } }
void wt_thread_detach(wt_thread *t) { if (t) { pthread_detach(t->t); free(t); } }

uint64_t wt_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void wt_sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
