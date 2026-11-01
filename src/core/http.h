/* Minimal HTTP/1.1 client over TLS (mbedTLS), portable between Linux and the Wii. */
#ifndef WIITUBE_HTTP_H
#define WIITUBE_HTTP_H

#include <stddef.h>

/* Receives decoded body bytes. Return nonzero to abort the transfer. */
typedef int (*http_body_cb)(void *ud, const unsigned char *data, size_t len);

typedef struct {
    const char *method;    /* "GET" when NULL */
    const char *url;       /* http:// or https:// */
    const char *headers;   /* extra header lines, each ending in "\r\n"; may be NULL */
    const void *body;
    size_t body_len;
    http_body_cb on_body;  /* NULL: buffer the whole body into the response */
    void *ud;
    size_t max_buffer;     /* buffering limit; 0 means 16 MiB */
    int max_redirects;     /* 0 means 5; negative disables redirects */
} http_request;

typedef struct {
    int status;
    unsigned char *body;          /* buffered body, NUL-terminated (only when on_body is NULL) */
    size_t body_len;
    long long content_length;     /* Content-Length header, or -1 */
    char content_type[128];
} http_response;

/* Initialise TLS state and load the CA bundle. Safe to call more than once. 0 on success. */
int http_global_init(char *err, size_t errlen);

/* Perform a request. Returns 0 when a response was received (check resp->status),
 * nonzero on transport/TLS errors, with a message in err. */
int http_do(const http_request *req, http_response *resp, char *err, size_t errlen);

void http_response_free(http_response *resp);

/* Percent-encode a string for use in a URL query. Caller frees. */
char *http_urlencode(const char *s);

#endif
