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

static void push(yt_search_result **arr, int *n, int *cap, const yt_search_result *r)
{
    if (!r->video_id[0] || *n >= YT_FEED_MAX || seen(*arr, *n, r->video_id))
        return;
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 32;
        yt_search_result *na = realloc(*arr, sizeof *na * (size_t)nc);
        if (!na)
            return;
        *arr = na;
        *cap = nc;
    }
    (*arr)[(*n)++] = *r;
}

void yt_feed_add(yt_feed *f, const yt_search_result *r)
{
    if (r->is_short)
        push(&f->shorts, &f->nshorts, &f->shorts_cap, r);
    else
        push(&f->items, &f->n, &f->cap, r);
}

int yt_feed_cursor(yt_feed *dst, const yt_feed *src)
{
    memset(dst, 0, sizeof *dst);
    dst->kind = src->kind;
    snprintf(dst->query, sizeof dst->query, "%s", src->query);
    if (src->continuation && !(dst->continuation = strdup(src->continuation)))
        return -1;
    return 0;
}

int yt_feed_merge(yt_feed *dst, yt_feed *src)
{
    int i, before = dst->n + dst->nshorts;
    for (i = 0; i < src->n; i++)
        push(&dst->items, &dst->n, &dst->cap, &src->items[i]);
    for (i = 0; i < src->nshorts; i++)
        push(&dst->shorts, &dst->nshorts, &dst->shorts_cap, &src->shorts[i]);
    free(dst->continuation);
    dst->continuation = src->continuation;
    src->continuation = NULL;
    if (dst->n >= YT_FEED_MAX || dst->nshorts >= YT_FEED_MAX) {
        free(dst->continuation); /* full: stop loading more */
        dst->continuation = NULL;
    }
    yt_feed_free(src);
    return dst->n + dst->nshorts - before;
}

void yt_feed_free(yt_feed *f)
{
    free(f->items);
    free(f->shorts);
    free(f->continuation);
    memset(f, 0, sizeof *f);
}
