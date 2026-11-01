/* Adjustments to mbedTLS's default configuration (included via MBEDTLS_USER_CONFIG_FILE). */
#ifndef WIITUBE_MBEDTLS_USER_H
#define WIITUBE_MBEDTLS_USER_H

/* Entropy always comes from net_entropy() through mbedtls_hardware_poll(),
 * so Linux and the Wii seed the random generator the same way. */
#define MBEDTLS_ENTROPY_HARDWARE_ALT

#ifdef WIITUBE_PLATFORM_WII
/* No /dev/urandom or getrandom() on the Wii. */
#define MBEDTLS_NO_PLATFORM_ENTROPY
/* No POSIX timing API; mbedTLS falls back to its portable code. */
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_NET_C
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
