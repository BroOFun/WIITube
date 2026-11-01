/* Blocking single-producer / single-consumer byte ring buffer between a download thread
 * and the decoder. */
#ifndef WIITUBE_RING_H
#define WIITUBE_RING_H

#include <stddef.h>

typedef struct ring ring;

ring *ring_new(size_t capacity);
void ring_free(ring *r);

/* Producer: blocks until everything is written. Returns 0, or -1 if the consumer aborted. */
int ring_write(ring *r, const void *data, size_t len);
/* Producer: no more data. error != 0 marks a failed download. */
void ring_finish(ring *r, int error);

/* Consumer: blocks until at least one byte is available. Returns bytes read, or 0 at the
 * end of the stream (check ring_error) or after ring_abort. */
size_t ring_read(ring *r, void *out, size_t len);
/* Consumer: stop; wakes the producer, whose ring_write then fails. */
void ring_abort(ring *r);

size_t ring_available(ring *r);  /* bytes ready to read */
int ring_done(ring *r);          /* producer finished */
int ring_error(ring *r);         /* error passed to ring_finish */
size_t ring_capacity(const ring *r);

#endif
