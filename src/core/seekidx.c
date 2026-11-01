/* Seek index of a DASH stream: parses the sidx box (no network or library dependencies, so
 * the desktop player test can use it). Declared in yt.h. */
#include "yt.h"

#include <stdlib.h>
#include <string.h>

static unsigned long long be(const unsigned char *p, int n)
{
    unsigned long long v = 0;
    while (n-- > 0)
        v = (v << 8) | *p++;
    return v;
}

int yt_parse_sidx(const unsigned char *b, size_t len, long long base, yt_index *idx)
{
    unsigned long long size, timescale, ept, first, t;
    long long off;
    size_t pos;
    int version, count, i;

    memset(idx, 0, sizeof *idx);
    if (len < 32 || memcmp(b + 4, "sidx", 4) != 0)
        return -1;
    size = be(b, 4);
    version = b[8];
    timescale = be(b + 16, 4);
    if (version == 0) {
        ept = be(b + 20, 4);
        first = be(b + 24, 4);
        pos = 28;
    } else {
        ept = be(b + 20, 8);
        first = be(b + 28, 8);
        pos = 36;
    }
    if (timescale == 0 || pos + 4 > len || size > len)
        return -1;
    count = (int)be(b + pos + 2, 2);
    pos += 4;
    if (pos + (size_t)count * 12 > len)
        return -1;
    idx->start = malloc(sizeof(double) * (size_t)(count + 1));
    idx->offset = malloc(sizeof(long long) * (size_t)(count + 1));
    if (!idx->start || !idx->offset) {
        yt_index_free(idx);
        return -1;
    }
    off = base + (long long)first;
    t = ept;
    for (i = 0; i < count; i++, pos += 12) {
        unsigned long long ref = be(b + pos, 4), dur = be(b + pos + 4, 4);
        idx->start[i] = (double)t / (double)timescale;
        idx->offset[i] = off;
        off += (long long)(ref & 0x7fffffff);
        t += dur;
    }
    idx->start[count] = (double)t / (double)timescale; /* end */
    idx->offset[count] = off;
    idx->n = count;
    return 0;
}

int yt_index_find(const yt_index *idx, double t)
{
    int lo = 0, hi = idx->n - 1;
    if (idx->n <= 0)
        return -1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (idx->start[mid] <= t)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

void yt_index_free(yt_index *idx)
{
    free(idx->start);
    free(idx->offset);
    memset(idx, 0, sizeof *idx);
}
