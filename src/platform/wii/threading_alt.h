/* mbedTLS MBEDTLS_THREADING_ALT mutex type for the Wii (libogc LWP mutexes). */
#ifndef WIITUBE_THREADING_ALT_H
#define WIITUBE_THREADING_ALT_H

#include <gctypes.h>

typedef struct {
    u32 handle; /* libogc mutex_t */
    int valid;
} mbedtls_threading_mutex_t;

/* Install the mutex functions; call once at startup before any other mbedTLS call. */
void wii_mbedtls_threading_init(void);

#endif
