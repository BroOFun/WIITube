/* Linux implementation of net.h, used by the desktop prototype. */
#define _GNU_SOURCE
#include "../../core/net.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int net_init(void) { return 0; }

int net_tcp_connect(const char *host, int port, int timeout_ms)
{
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    int fd = -1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof portstr, "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0)
        return -1;

    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (timeout_ms > 0) {
            struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int net_send(int sock, const void *buf, size_t len) { return (int)send(sock, buf, len, MSG_NOSIGNAL); }
int net_recv(int sock, void *buf, size_t len) { return (int)recv(sock, buf, len, 0); }
void net_close(int sock) { if (sock >= 0) close(sock); }

int net_entropy(unsigned char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = getrandom(buf, len, 0);
        if (n <= 0)
            return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

const char *net_https_proxy(void)
{
    static char hostport[256];
    const char *p = getenv("HTTPS_PROXY");
    if (!p || !*p)
        p = getenv("https_proxy");
    if (!p || !*p)
        return NULL;
    if (strncmp(p, "http://", 7) == 0)
        p += 7;
    snprintf(hostport, sizeof hostport, "%s", p);
    hostport[strcspn(hostport, "/")] = 0;
    return hostport;
}

const char *net_ca_bundle_path(void)
{
    static const char *candidates[] = {
        "/etc/ssl/certs/ca-certificates.crt", /* Debian/Ubuntu */
        "/etc/pki/tls/certs/ca-bundle.crt",   /* Fedora */
        "/etc/ssl/cert.pem",                  /* macOS, Alpine */
        NULL,
    };
    const char *env = getenv("WIITUBE_CA_BUNDLE");
    int i;
    if (!env || !*env)
        env = getenv("SSL_CERT_FILE");
    if (env && *env)
        return env;
    for (i = 0; candidates[i]; i++)
        if (access(candidates[i], R_OK) == 0)
            return candidates[i];
    return "cacert.pem";
}
