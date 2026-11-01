/* Minimal HTTP/1.1 client over TLS (mbedTLS 3.6), portable between Linux and the Wii.
 *
 * Features: CONNECT proxy (development only), chunked transfer decoding, optional gzip
 * (when built with WIITUBE_HAVE_ZLIB), redirects, streaming or buffered bodies.
 * One connection per request ("Connection: close") to keep the code simple for now. */
#include "http.h"
#include "net.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#ifdef WIITUBE_HAVE_ZLIB
#include <zlib.h>
#endif

#define HTTP_TIMEOUT_MS 20000
#define HTTP_MAX_HEADER 32768
#define HTTP_DEFAULT_MAX_BUFFER (16u << 20)

/* ---- TLS global state ---------------------------------------------------- */

static int g_inited;
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_x509_crt g_ca;
static mbedtls_ssl_config g_conf;

/* Entropy hook required by MBEDTLS_ENTROPY_HARDWARE_ALT (see wiitube_mbedtls_user.h). */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;
    if (plat_entropy(output, len) != 0)
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

static void set_err(char *err, size_t errlen, const char *fmt, const char *a, int code)
{
    char buf[160];
    if (!err || !errlen)
        return;
    if (code < 0) {
        mbedtls_strerror(code, buf, sizeof buf);
        snprintf(err, errlen, "%s%s%s (%s, -0x%04x)", fmt, a ? ": " : "", a ? a : "", buf, -code);
    } else {
        snprintf(err, errlen, "%s%s%s", fmt, a ? ": " : "", a ? a : "");
    }
}

int http_global_init(char *err, size_t errlen)
{
    int rc;
    const char *ca;

    if (g_inited)
        return 0;
    if (plat_net_init() != 0) {
        set_err(err, errlen, "network init failed", NULL, 0);
        return -1;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        set_err(err, errlen, "psa_crypto_init failed", NULL, 0);
        return -1;
    }
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);
    mbedtls_x509_crt_init(&g_ca);
    mbedtls_ssl_config_init(&g_conf);

    rc = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                               (const unsigned char *)"wiitube", 7);
    if (rc != 0) {
        set_err(err, errlen, "seeding RNG failed", NULL, rc);
        return -1;
    }
    if ((ca = plat_ca_bundle_pem()) != NULL) {
        /* Returns the number of certificates it could not parse; only < 0 is fatal. */
        rc = mbedtls_x509_crt_parse(&g_ca, (const unsigned char *)ca, strlen(ca) + 1);
        ca = "built-in";
    } else {
#ifdef MBEDTLS_FS_IO
        ca = plat_ca_bundle_path();
        rc = mbedtls_x509_crt_parse_file(&g_ca, ca);
#else
        ca = "none";
        rc = MBEDTLS_ERR_X509_FILE_IO_ERROR;
#endif
    }
    if (rc < 0) {
        set_err(err, errlen, "loading CA bundle failed", ca, rc);
        return -1;
    }
    rc = mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        set_err(err, errlen, "TLS config failed", NULL, rc);
        return -1;
    }
    mbedtls_ssl_conf_authmode(&g_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g_conf, &g_ca, NULL);
    mbedtls_ssl_conf_rng(&g_conf, mbedtls_ctr_drbg_random, &g_drbg);
    mbedtls_ssl_conf_read_timeout(&g_conf, HTTP_TIMEOUT_MS);
    g_inited = 1;
    return 0;
}

/* ---- Connection with a small read buffer ---------------------------------- */

typedef struct {
    int sock;
    int tls;
    mbedtls_ssl_context ssl;
    unsigned char rbuf[16384];
    size_t rpos, rlen;
} conn_t;

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int n = plat_send(*(int *)ctx, buf, len);
    return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int n = plat_recv(*(int *)ctx, buf, len);
    return n < 0 ? MBEDTLS_ERR_SSL_TIMEOUT : n;
}

/* Raw read from the transport (bypassing rbuf). Returns >0 bytes, 0 at EOF, <0 on error. */
static int conn_raw_read(conn_t *c, unsigned char *buf, size_t len)
{
    if (!c->tls)
        return plat_recv(c->sock, buf, len);
    for (;;) {
        int n = mbedtls_ssl_read(&c->ssl, buf, len);
        if (n >= 0)
            return n;
        if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
            return 0;
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ||
            n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            continue;
        return n;
    }
}

static int conn_write_all(conn_t *c, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len > 0) {
        int n = c->tls ? mbedtls_ssl_write(&c->ssl, p, len) : plat_send(c->sock, p, len);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (n <= 0)
            return n < 0 ? n : -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int conn_fill(conn_t *c)
{
    int n;
    if (c->rpos == c->rlen)
        c->rpos = c->rlen = 0;
    if (c->rlen == sizeof c->rbuf) {
        if (c->rpos == 0)
            return -1; /* buffer full of unconsumed data */
        memmove(c->rbuf, c->rbuf + c->rpos, c->rlen - c->rpos);
        c->rlen -= c->rpos;
        c->rpos = 0;
    }
    n = conn_raw_read(c, c->rbuf + c->rlen, sizeof c->rbuf - c->rlen);
    if (n > 0)
        c->rlen += (size_t)n;
    return n;
}

/* Read up to len bytes, using buffered data first. */
static int conn_read(conn_t *c, unsigned char *buf, size_t len)
{
    if (c->rpos < c->rlen) {
        size_t n = c->rlen - c->rpos;
        if (n > len)
            n = len;
        memcpy(buf, c->rbuf + c->rpos, n);
        c->rpos += n;
        return (int)n;
    }
    return conn_raw_read(c, buf, len);
}

/* Read a CRLF-terminated line (without the CRLF). Returns length, or -1. */
static int conn_getline(conn_t *c, char *out, size_t outlen)
{
    size_t n = 0;
    for (;;) {
        while (c->rpos < c->rlen) {
            char ch = (char)c->rbuf[c->rpos++];
            if (ch == '\n') {
                if (n > 0 && out[n - 1] == '\r')
                    n--;
                out[n] = 0;
                return (int)n;
            }
            if (n + 1 >= outlen)
                return -1;
            out[n++] = ch;
        }
        if (conn_fill(c) <= 0)
            return -1;
    }
}

static void conn_close(conn_t *c)
{
    if (c->tls) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
    }
    plat_close(c->sock);
    c->sock = -1;
    c->tls = 0;
}

/* ---- URL parsing ----------------------------------------------------------- */

typedef struct {
    int https;
    char host[256];
    int port;
    const char *path; /* points into the original URL ("/" if empty) */
} url_t;

static int parse_url(const char *url, url_t *u)
{
    const char *p, *host_end, *colon;
    size_t hlen;

    memset(u, 0, sizeof *u);
    if (strncmp(url, "https://", 8) == 0) {
        u->https = 1;
        u->port = 443;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        u->port = 80;
        p = url + 7;
    } else {
        return -1;
    }
    host_end = p + strcspn(p, "/?#");
    colon = memchr(p, ':', (size_t)(host_end - p));
    hlen = (size_t)((colon ? colon : host_end) - p);
    if (hlen == 0 || hlen >= sizeof u->host)
        return -1;
    memcpy(u->host, p, hlen);
    u->host[hlen] = 0;
    if (colon)
        u->port = atoi(colon + 1);
    u->path = *host_end ? host_end : "/";
    return 0;
}

/* ---- Body sinks: chunked decoding -> gzip -> callback or buffer ---------- */

typedef struct {
    const http_request *req;
    http_response *resp;
    size_t max;
    int aborted, overflow;
#ifdef WIITUBE_HAVE_ZLIB
    int gzip;
    z_stream zs;
#endif
} sink_t;

static int sink_out(sink_t *s, const unsigned char *data, size_t len)
{
    http_response *r = s->resp;
    if (len == 0)
        return 0;
    if (s->req->on_body) {
        if (s->req->on_body(s->req->ud, data, len) != 0) {
            s->aborted = 1;
            return -1;
        }
        return 0;
    }
    if (r->body_len + len + 1 > s->max) {
        s->overflow = 1;
        return -1;
    }
    {
        unsigned char *nb = realloc(r->body, r->body_len + len + 1);
        if (!nb)
            return -1;
        r->body = nb;
    }
    memcpy(r->body + r->body_len, data, len);
    r->body_len += len;
    r->body[r->body_len] = 0;
    return 0;
}

static int sink_write(sink_t *s, const unsigned char *data, size_t len)
{
#ifdef WIITUBE_HAVE_ZLIB
    if (s->gzip) {
        unsigned char out[16384];
        if (s->gzip == 2)
            return 0; /* stream already ended; ignore trailing bytes */
        s->zs.next_in = (unsigned char *)data;
        s->zs.avail_in = (uInt)len;
        do {
            int zr;
            s->zs.next_out = out;
            s->zs.avail_out = sizeof out;
            zr = inflate(&s->zs, Z_NO_FLUSH);
            if (zr == Z_STREAM_ERROR || zr == Z_DATA_ERROR || zr == Z_MEM_ERROR || zr == Z_NEED_DICT)
                return -1;
            if (sink_out(s, out, sizeof out - s->zs.avail_out) != 0)
                return -1;
            if (zr == Z_STREAM_END) {
                s->gzip = 2;
                break;
            }
            if (zr == Z_BUF_ERROR)
                break; /* needs more input */
        } while (s->zs.avail_out == 0 || s->zs.avail_in > 0);
        return 0;
    }
#endif
    return sink_out(s, data, len);
}

static int read_body(conn_t *c, sink_t *s, int chunked, long long length, char *err, size_t errlen)
{
    unsigned char buf[16384];
    char line[256];

    if (chunked) {
        for (;;) {
            unsigned long long size;
            if (conn_getline(c, line, sizeof line) < 0) {
                set_err(err, errlen, "bad chunk header", NULL, 0);
                return -1;
            }
            size = strtoull(line, NULL, 16);
            if (size == 0) {
                while (conn_getline(c, line, sizeof line) > 0) {} /* trailers */
                return 0;
            }
            while (size > 0) {
                int n = conn_read(c, buf, size < sizeof buf ? (size_t)size : sizeof buf);
                if (n <= 0) {
                    set_err(err, errlen, "connection closed in chunk", NULL, n);
                    return -1;
                }
                if (sink_write(s, buf, (size_t)n) != 0)
                    return -1;
                size -= (unsigned long long)n;
            }
            if (conn_getline(c, line, sizeof line) < 0) /* CRLF after chunk */
                return -1;
        }
    }
    while (length != 0) {
        size_t want = sizeof buf;
        int n;
        if (length > 0 && (long long)want > length)
            want = (size_t)length;
        n = conn_read(c, buf, want);
        if (n == 0 && length < 0)
            return 0; /* read-until-close body */
        if (n <= 0) {
            set_err(err, errlen, "connection closed early", NULL, n);
            return -1;
        }
        if (sink_write(s, buf, (size_t)n) != 0)
            return -1;
        if (length > 0)
            length -= n;
    }
    return 0;
}

/* ---- Request ---------------------------------------------------------------- */

static int ci_prefix(const char *s, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int open_conn(conn_t *c, const url_t *u, char *err, size_t errlen)
{
    const char *proxy = u->https ? plat_https_proxy() : NULL;
    int rc;

    memset(c, 0, sizeof *c);
    c->sock = -1;
    if (proxy) {
        char phost[256], line[512];
        char *colon;
        int pport = 8080, status = 0;
        snprintf(phost, sizeof phost, "%s", proxy);
        colon = strrchr(phost, ':');
        if (colon) {
            *colon = 0;
            pport = atoi(colon + 1);
        }
        c->sock = plat_tcp_connect(phost, pport, HTTP_TIMEOUT_MS);
        if (c->sock < 0) {
            set_err(err, errlen, "cannot connect to proxy", proxy, 0);
            return -1;
        }
        snprintf(line, sizeof line, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n\r\n", u->host, u->port,
                 u->host, u->port);
        if (conn_write_all(c, line, strlen(line)) != 0 || conn_getline(c, line, sizeof line) < 0 ||
            sscanf(line, "HTTP/%*s %d", &status) != 1 || status != 200) {
            set_err(err, errlen, "proxy refused CONNECT", u->host, 0);
            plat_close(c->sock);
            return -1;
        }
        while (conn_getline(c, line, sizeof line) > 0) {}
    } else {
        c->sock = plat_tcp_connect(u->host, u->port, HTTP_TIMEOUT_MS);
        if (c->sock < 0) {
            set_err(err, errlen, "cannot connect", u->host, 0);
            return -1;
        }
    }
    if (!u->https)
        return 0;

    mbedtls_ssl_init(&c->ssl);
    c->tls = 1;
    if ((rc = mbedtls_ssl_setup(&c->ssl, &g_conf)) != 0 ||
        (rc = mbedtls_ssl_set_hostname(&c->ssl, u->host)) != 0) {
        set_err(err, errlen, "TLS setup failed", u->host, rc);
        conn_close(c);
        return -1;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->sock, bio_send, bio_recv, NULL);
    while ((rc = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);
            if (flags != 0 && flags != (uint32_t)-1) {
                char vbuf[256];
                mbedtls_x509_crt_verify_info(vbuf, sizeof vbuf, "", flags);
                vbuf[strcspn(vbuf, "\n")] = 0;
                set_err(err, errlen, "certificate verification failed", vbuf, 0);
            } else {
                set_err(err, errlen, "TLS handshake failed", u->host, rc);
            }
            conn_close(c);
            return -1;
        }
    }
    return 0;
}

static int do_one(const http_request *req, const char *url, const char *method, http_response *resp,
                  char *location, size_t loclen, char *err, size_t errlen)
{
    url_t u;
    conn_t *c;
    char line[4096], hostport[300];
    size_t hlen;
    char *head = NULL;
    int chunked = 0, gzip = 0, rc = -1, header_bytes = 0;
    long long length = -1;
    sink_t sink;
    const int has_body = req->body && req->body_len > 0 && strcmp(method, "GET") != 0 &&
                         strcmp(method, "HEAD") != 0;

    if (parse_url(url, &u) != 0) {
        set_err(err, errlen, "bad URL", url, 0);
        return -1;
    }
    c = malloc(sizeof *c);
    if (!c) {
        set_err(err, errlen, "out of memory", NULL, 0);
        return -1;
    }
    if (open_conn(c, &u, err, errlen) != 0) {
        free(c);
        return -1;
    }

    if ((u.https && u.port == 443) || (!u.https && u.port == 80))
        snprintf(hostport, sizeof hostport, "%s", u.host);
    else
        snprintf(hostport, sizeof hostport, "%s:%d", u.host, u.port);

    hlen = strlen(method) + strlen(u.path) + strlen(hostport) + (req->headers ? strlen(req->headers) : 0) + 256;
    head = malloc(hlen);
    if (!head)
        goto out;
    snprintf(head, hlen,
             "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nAccept-Encoding: %s\r\n%s",
             method, u.path, hostport,
#ifdef WIITUBE_HAVE_ZLIB
             "gzip",
#else
             "identity",
#endif
             req->headers ? req->headers : "");
    if (has_body) {
        size_t l = strlen(head);
        snprintf(head + l, hlen - l, "Content-Length: %zu\r\n", req->body_len);
    }
    strcat(head, "\r\n");
    if (conn_write_all(c, head, strlen(head)) != 0 ||
        (has_body && conn_write_all(c, req->body, req->body_len) != 0)) {
        set_err(err, errlen, "send failed", u.host, 0);
        goto out;
    }

    if (conn_getline(c, line, sizeof line) < 0 || sscanf(line, "HTTP/%*s %d", &resp->status) != 1) {
        set_err(err, errlen, "bad response", u.host, 0);
        goto out;
    }
    for (;;) {
        int n = conn_getline(c, line, sizeof line);
        if (n < 0) {
            set_err(err, errlen, "bad headers", u.host, 0);
            goto out;
        }
        if (n == 0)
            break;
        header_bytes += n;
        if (header_bytes > HTTP_MAX_HEADER) {
            set_err(err, errlen, "headers too large", u.host, 0);
            goto out;
        }
        if (ci_prefix(line, "content-length:"))
            length = strtoll(line + 15, NULL, 10);
        else if (ci_prefix(line, "transfer-encoding:") && strstr(line, "chunked"))
            chunked = 1;
        else if (ci_prefix(line, "content-encoding:") && strstr(line, "gzip"))
            gzip = 1;
        else if (ci_prefix(line, "location:") && location) {
            const char *v = line + 9;
            while (*v == ' ')
                v++;
            snprintf(location, loclen, "%s", v);
        } else if (ci_prefix(line, "content-type:")) {
            const char *v = line + 13;
            while (*v == ' ')
                v++;
            snprintf(resp->content_type, sizeof resp->content_type, "%s", v);
        }
    }
    resp->content_length = length;

    /* Redirect: the caller follows it; the body is not needed. */
    if (location && *location && resp->status >= 300 && resp->status < 400) {
        rc = 0;
        goto out;
    }
    if (strcmp(method, "HEAD") == 0 || resp->status == 204 || resp->status == 304) {
        rc = 0;
        goto out;
    }

    memset(&sink, 0, sizeof sink);
    sink.req = req;
    sink.resp = resp;
    sink.max = req->max_buffer ? req->max_buffer : HTTP_DEFAULT_MAX_BUFFER;
#ifdef WIITUBE_HAVE_ZLIB
    if (gzip) {
        sink.gzip = 1;
        if (inflateInit2(&sink.zs, 15 + 32) != Z_OK)
            goto out;
    }
#else
    if (gzip) {
        set_err(err, errlen, "server sent gzip but zlib support is off", NULL, 0);
        goto out;
    }
#endif
    if (!chunked && length < 0)
        length = -1; /* read until close */
    rc = read_body(c, &sink, chunked, chunked ? 0 : length, err, errlen);
#ifdef WIITUBE_HAVE_ZLIB
    if (sink.gzip)
        inflateEnd(&sink.zs);
#endif
    if (rc != 0) {
        if (sink.aborted)
            set_err(err, errlen, "aborted by callback", NULL, 0);
        else if (sink.overflow)
            set_err(err, errlen, "response too large", NULL, 0);
        else if (!err || !*err)
            set_err(err, errlen, "error reading body", NULL, 0);
    }

out:
    free(head);
    conn_close(c);
    free(c);
    return rc;
}

int http_do(const http_request *req, http_response *resp, char *err, size_t errlen)
{
    char *url, *location;
    const char *method = req->method ? req->method : "GET";
    int redirects = req->max_redirects == 0 ? 5 : req->max_redirects;
    int rc;

    memset(resp, 0, sizeof *resp);
    resp->content_length = -1;
    if (err && errlen)
        err[0] = 0;
    if (http_global_init(err, errlen) != 0)
        return -1;

    url = strdup(req->url);
    location = malloc(4096);
    if (!url || !location) {
        free(url);
        free(location);
        return -1;
    }
    for (;;) {
        location[0] = 0;
        rc = do_one(req, url, method, resp, redirects > 0 ? location : NULL, 4096, err, errlen);
        if (rc != 0 || !location[0] || resp->status < 300 || resp->status >= 400)
            break;
        if (redirects-- <= 0)
            break;
        /* Resolve relative Location against the current URL. */
        if (location[0] == '/') {
            url_t u;
            char *abs = malloc(strlen(location) + 300);
            parse_url(url, &u);
            sprintf(abs, "%s://%s:%d%s", u.https ? "https" : "http", u.host, u.port, location);
            free(url);
            url = abs;
        } else {
            free(url);
            url = strdup(location);
        }
        if (resp->status == 303 || ((resp->status == 301 || resp->status == 302) && strcmp(method, "POST") == 0))
            method = "GET";
        http_response_free(resp);
        resp->content_length = -1;
    }
    free(url);
    free(location);
    return rc;
}

void http_response_free(http_response *resp)
{
    free(resp->body);
    memset(resp, 0, sizeof *resp);
}

char *http_urlencode(const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(s);
    char *out = malloc(n * 3 + 1), *p = out;
    if (!out)
        return NULL;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            *p++ = (char)ch;
        } else {
            *p++ = '%';
            *p++ = hex[ch >> 4];
            *p++ = hex[ch & 15];
        }
    }
    *p = 0;
    return out;
}
