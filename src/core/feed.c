/* Result lists that grow page by page (no network code, so the UI previews can use it). */
#include "yt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int seen(const yt_search_result *arr, int n, const char *id)
{
    int i, from = n > 300 ? n - 300 : 0; /* pages can repeat entries; check recent ones */
    for (i = from; i < n; i++)
        if (strcmp(arr[i].video_id, id) == 0)
            return 1;
    return 0;
}

static void push(yt_feed *f, const yt_search_result *r)
{
    if (!r->video_id[0] || f->n >= YT_FEED_MAX || seen(f->items, f->n, r->video_id))
        return;
    if (f->n == f->cap) {
        int nc = f->cap ? f->cap * 2 : 32;
        yt_search_result *na = realloc(f->items, sizeof *na * (size_t)nc);
        if (!na)
            return;
        f->items = na;
        f->cap = nc;
    }
    f->items[f->n++] = *r;
    if (r->is_short)
        f->nshorts++;
}

void yt_feed_add(yt_feed *f, const yt_search_result *r) { push(f, r); }

int yt_feed_cursor(yt_feed *dst, const yt_feed *src)
{
    memset(dst, 0, sizeof *dst);
    dst->kind = src->kind;
    dst->pos = src->pos;
    snprintf(dst->query, sizeof dst->query, "%s", src->query);
    if (src->continuation && !(dst->continuation = strdup(src->continuation)))
        return -1;
    return 0;
}

int yt_feed_merge(yt_feed *dst, yt_feed *src)
{
    int i, before = dst->n;
    for (i = 0; i < src->n; i++) {
        yt_search_result r = src->items[i];
        if (r.shelf > 0)
            r.shelf += dst->nshelves; /* keep shelf numbers unique in dst */
        push(dst, &r);
    }
    dst->nshelves += src->nshelves;
    if (src->pos > dst->pos)
        dst->pos = src->pos;
    free(dst->continuation);
    dst->continuation = src->continuation;
    src->continuation = NULL;
    if (dst->n >= YT_FEED_MAX) {
        free(dst->continuation); /* full: stop loading more */
        dst->continuation = NULL;
    }
    yt_feed_free(src);
    return dst->n - before;
}

void yt_feed_free(yt_feed *f)
{
    free(f->items);
    free(f->continuation);
    memset(f, 0, sizeof *f);
}
