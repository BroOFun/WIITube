/* libogc (LWP) implementation of thread.h, plus mbedTLS mutex glue. */
#include <gccore.h>
#include <ogc/cond.h>
#include <ogc/lwp_watchdog.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../core/thread.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/threading.h"
#include "threading_alt.h"

#define PRIO_NORMAL 64
#define PRIO_NETWORK 80

struct wt_mutex { mutex_t m; };
struct wt_cond { cond_t c; };
struct wt_thread { lwp_t t; };

wt_mutex *wt_mutex_new(void)
{
    wt_mutex *m = malloc(sizeof *m);
    if (m && LWP_MutexInit(&m->m, false) != 0) {
        free(m);
        m = NULL;
    }
    return m;
}
void wt_mutex_free(wt_mutex *m) { if (m) { LWP_MutexDestroy(m->m); free(m); } }
void wt_mutex_lock(wt_mutex *m) { LWP_MutexLock(m->m); }
void wt_mutex_unlock(wt_mutex *m) { LWP_MutexUnlock(m->m); }

wt_cond *wt_cond_new(void)
{
    wt_cond *c = malloc(sizeof *c);
    if (c && LWP_CondInit(&c->c) != 0) {
        free(c);
        c = NULL;
    }
    return c;
}
void wt_cond_free(wt_cond *c) { if (c) { LWP_CondDestroy(c->c); free(c); } }
void wt_cond_wait(wt_cond *c, wt_mutex *m) { LWP_CondWait(c->c, m->m); }
void wt_cond_broadcast(wt_cond *c) { LWP_CondBroadcast(c->c); }

wt_thread *wt_thread_start(void *(*fn)(void *), void *arg, size_t stack_size, int high_priority)
{
    wt_thread *t = malloc(sizeof *t);
    if (!t)
        return NULL;
    if (LWP_CreateThread(&t->t, fn, arg, NULL, stack_size ? (u32)stack_size : 64 * 1024,
                         high_priority ? PRIO_NETWORK : PRIO_NORMAL) != 0) {
        free(t);
        return NULL;
    }
    return t;
}

void wt_thread_join(wt_thread *t) { if (t) { LWP_JoinThread(t->t, NULL); free(t); } }
/* libogc has no detach; the thread's memory is only reclaimed by a join, so a detached
 * thread leaks its stack. This only happens when a download is stuck at exit. */
void wt_thread_detach(wt_thread *t) { free(t); }

uint64_t wt_time_us(void) { return ticks_to_microsecs(gettime()); }

/* Milliseconds since boot, for mbedTLS (MBEDTLS_PLATFORM_MS_TIME_ALT). */
mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)ticks_to_millisecs(gettime()); }
void wt_sleep_ms(int ms) { usleep((useconds_t)ms * 1000u); }

/* ---- mbedTLS mutexes ----
 * libogc has a fixed pool of 64 mutexes for the whole program, and mbedTLS creates one per
 * RSA key, so loading the ~145 built-in CA certificates used them all up (later mutex
 * creation failed, which surfaced as "Out of memory" when starting a video). All mbedTLS
 * "mutexes" therefore share one recursive LWP mutex. mbedTLS only holds them for short,
 * non-blocking sections, and with a single lock there is no lock-order deadlock. */

static mutex_t g_tls_lock;
static int g_tls_lock_ok;

static void mt_init(mbedtls_threading_mutex_t *m)
{
    m->handle = 0;
    m->valid = 1;
}

static void mt_free(mbedtls_threading_mutex_t *m)
{
    m->valid = 0;
}

static int mt_lock(mbedtls_threading_mutex_t *m)
{
    if (!m->valid || !g_tls_lock_ok || LWP_MutexLock(g_tls_lock) != 0)
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    return 0;
}

static int mt_unlock(mbedtls_threading_mutex_t *m)
{
    if (!m->valid || !g_tls_lock_ok || LWP_MutexUnlock(g_tls_lock) != 0)
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    return 0;
}

void wii_mbedtls_threading_init(void)
{
    g_tls_lock_ok = LWP_MutexInit(&g_tls_lock, true) == 0;
    mbedtls_threading_set_alt(mt_init, mt_free, mt_lock, mt_unlock);
}
