/* Platform network abstraction. Implemented per platform in src/platform/<name>/. */
#ifndef WIITUBE_NET_H
#define WIITUBE_NET_H

#include <stddef.h>

/* One-time platform network init (Wii: brings up the network interface). 0 on success. */
int net_init(void);

/* Resolve host and open a blocking TCP connection. Returns a socket handle >= 0, or < 0 on error. */
int net_tcp_connect(const char *host, int port, int timeout_ms);

/* Send/receive. Return bytes transferred, 0 on orderly close (recv), < 0 on error. */
int net_send(int sock, const void *buf, size_t len);
int net_recv(int sock, void *buf, size_t len);
void net_close(int sock);

/* Fill buf with entropy for seeding the TLS random generator. 0 on success. */
int net_entropy(unsigned char *buf, size_t len);

/* Optional HTTPS proxy ("host:port") used for development behind a CONNECT proxy.
 * Returns NULL when no proxy should be used. The Wii build always returns NULL. */
const char *net_https_proxy(void);

/* Path of the CA certificate bundle (PEM) used to verify servers. */
const char *net_ca_bundle_path(void);

#endif
