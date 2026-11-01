/* Minimal threading API implemented per platform (pthreads on desktop, LWP on the Wii). */
#ifndef WIITUBE_THREAD_H
#define WIITUBE_THREAD_H

#include <stddef.h>
#include <stdint.h>

typedef struct wt_mutex wt_mutex;
typedef struct wt_cond wt_cond;
typedef struct wt_thread wt_thread;

wt_mutex *wt_mutex_new(void);
void wt_mutex_free(wt_mutex *m);
void wt_mutex_lock(wt_mutex *m);
void wt_mutex_unlock(wt_mutex *m);

wt_cond *wt_cond_new(void);
void wt_cond_free(wt_cond *c);
void wt_cond_wait(wt_cond *c, wt_mutex *m);
void wt_cond_broadcast(wt_cond *c);

/* Start a thread. stack_size 0 means a platform default; high_priority asks for a priority
 * above the main thread (used for network threads, which mostly sleep in recv()). */
wt_thread *wt_thread_start(void *(*fn)(void *), void *arg, size_t stack_size, int high_priority);
/* Wait for a thread to finish and release it. */
void wt_thread_join(wt_thread *t);
/* Release a thread handle without waiting (the thread keeps running and cleans up itself). */
void wt_thread_detach(wt_thread *t);

uint64_t wt_time_us(void);
void wt_sleep_ms(int ms);

#endif
