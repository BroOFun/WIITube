/* Wii implementation of net.h on libogc's IOS network stack. */
#include <gccore.h>
#include <network.h>
#include <ogc/ipc.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>
#include <time.h>

#include "../../core/net.h"
#include "mbedtls/sha256.h"
#include "wii.h"

/* IOS's network calls go through a 64 KiB shared heap, and recv loses data on calls larger
 * than half of it; keep each call small. */
#define MAX_IO 8192

extern const char wiitube_cacert_pem[]; /* generated from cacert.pem by Makefile.wii */

static int g_ready;
static mutex_t g_dns_lock;
static char g_ip[16];

int plat_net_init(void)
{
    s32 r;
    if (g_ready)
        return 0;
    r = if_config(g_ip, NULL, NULL, true, 20);
    if (r < 0)
        return (int)r;
    LWP_MutexInit(&g_dns_lock, false);
    g_ready = 1;
    return 0;
}

const char *wii_net_ip(void) { return g_ip; }

int plat_tcp_connect(const char *host, int port, int timeout_ms)
{
    struct hostent *he;
    struct sockaddr_in sa;
    s32 s;
    (void)timeout_ms; /* IOS sockets have no usable connect/recv timeout */

    memset(&sa, 0, sizeof sa);
    LWP_MutexLock(g_dns_lock); /* net_gethostbyname returns static storage */
    he = net_gethostbyname(host);
    if (he && he->h_addrtype == AF_INET && he->h_addr_list && he->h_addr_list[0])
        memcpy(&sa.sin_addr, he->h_addr_list[0], 4);
    LWP_MutexUnlock(g_dns_lock);
    if (sa.sin_addr.s_addr == 0)
        return -1;

    s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0)
        return -1;
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)port);
    if (net_connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) {
        net_close(s);
        return -1;
    }
    return (int)s;
}

int plat_send(int sock, const void *buf, size_t len)
{
    return (int)net_send(sock, buf, (s32)(len > MAX_IO ? MAX_IO : len), 0);
}

int plat_recv(int sock, void *buf, size_t len)
{
    return (int)net_recv(sock, buf, (s32)(len > MAX_IO ? MAX_IO : len), 0);
}

void plat_close(int sock)
{
    if (sock >= 0)
        net_close(sock);
}

/* The Wii has no RNG exposed to us. Timing of round trips to IOS (which runs on its own ARM
 * core) jitters at the timebase's resolution; hash many such samples with the clock. */
int plat_entropy(unsigned char *buf, size_t len)
{
    static u32 counter;
    while (len > 0) {
        mbedtls_sha256_context ctx;
        unsigned char out[32];
        size_t n = len < sizeof out ? len : sizeof out;
        time_t now = time(NULL);
        int i;

        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        counter++;
        mbedtls_sha256_update(&ctx, (const unsigned char *)&counter, sizeof counter);
        mbedtls_sha256_update(&ctx, (const unsigned char *)&now, sizeof now);
        for (i = 0; i < 64; i++) {
            u64 t0 = gettime(), t1;
            s32 fd = IOS_Open("/dev/fs", 0);
            if (fd >= 0)
                IOS_Close(fd);
            t1 = gettime();
            mbedtls_sha256_update(&ctx, (const unsigned char *)&t0, sizeof t0);
            mbedtls_sha256_update(&ctx, (const unsigned char *)&t1, sizeof t1);
        }
        mbedtls_sha256_finish(&ctx, out);
        mbedtls_sha256_free(&ctx);
        memcpy(buf, out, n);
        buf += n;
        len -= n;
    }
    return 0;
}

const char *plat_https_proxy(void) { return NULL; }
const char *plat_ca_bundle_pem(void) { return wiitube_cacert_pem; }
const char *plat_ca_bundle_path(void) { return NULL; }
