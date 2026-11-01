/* Adjustments to mbedTLS's default configuration (included via MBEDTLS_USER_CONFIG_FILE). */
#ifndef WIITUBE_MBEDTLS_USER_H
#define WIITUBE_MBEDTLS_USER_H

/* Entropy always comes from plat_entropy() through mbedtls_hardware_poll(),
 * so Linux and the Wii seed the random generator the same way. */
#define MBEDTLS_ENTROPY_HARDWARE_ALT

#ifdef WIITUBE_PLATFORM_WII
/* No /dev/urandom or getrandom() on the Wii. */
#define MBEDTLS_NO_PLATFORM_ENTROPY
/* No POSIX timing or BSD socket API in mbedTLS's sense; we bring our own I/O. */
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_NET_C
/* Certificates are built in, so no file access (and no PSA key storage on files). */
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
/* Video and audio download at the same time, each with its own TLS connection; the shared
 * RNG and PSA key store need locking. Mutexes come from libogc (see thread_wii.c). */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT
/* mbedTLS can't find a millisecond clock on this target; thread_wii.c provides one. */
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#endif

/* Not needed by an HTTPS client; trims code size. */
#undef MBEDTLS_SSL_SRV_C
#undef MBEDTLS_SSL_CACHE_C
#undef MBEDTLS_SSL_COOKIE_C
#undef MBEDTLS_SSL_TICKET_C
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_DTLS_CONNECTION_ID
#undef MBEDTLS_SSL_PROTO_DTLS

#endif
