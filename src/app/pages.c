/* Browse pages: background loading of YouTube lists, the watch history, browse actions.
 * See pages.h. */
#include "pages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/log.h"
#include "../core/thread.h"
#include "../core/yt.h"

/* ---- watch history (for "For you" and the Shorts feed) ---------------------------------- */

#define HISTORY_MAX 40
#define FOR_YOU_SEEDS 8

static struct {
    char id[16];
    int is_short;
} g_history[HISTORY_MAX];
static int g_nhistory;

static char g_history_path[256];

static void history_load(void)
{
    FILE *f;
    char line[64];
    if (!g_history_path[0] || !(f = fopen(g_history_path, "r")))
        return;
    while (g_nhistory < HISTORY_MAX && fgets(line, sizeof line, f)) {
        char kind, id[16];
        if (sscanf(line, "%c %15s", &kind, id) == 2 && (kind == 'v' || kind == 's') && strlen(id) == 11) {
            snprintf(g_history[g_nhistory].id, sizeof g_history[g_nhistory].id, "%s", id);
            g_history[g_nhistory].is_short = kind == 's';
            g_nhistory++;
        }
    }
    fclose(f);
}

void history_add(const char *id, int is_short)
{
    int i, j;
    FILE *f;
    for (i = 0; i < g_nhistory; i++) /* move to the front if already there */
        if (strcmp(g_history[i].id, id) == 0)
            break;
    if (i == g_nhistory && g_nhistory < HISTORY_MAX)
        g_nhistory++;
    if (i >= HISTORY_MAX)
        i = HISTORY_MAX - 1;
    for (j = i; j > 0; j--)
        g_history[j] = g_history[j - 1];
    snprintf(g_history[0].id, sizeof g_history[0].id, "%s", id);
    g_history[0].is_short = is_short;
    if (g_history_path[0] && (f = fopen(g_history_path, "w"))) {
        for (i = 0; i < g_nhistory; i++)
            fprintf(f, "%c %s\n", g_history[i].is_short ? 's' : 'v', g_history[i].id);
        fclose(f);
    }
}

/* The most recently watched Short (to continue the Shorts feed from), or NULL. */
const char *history_last_short(void)
{
    int i;
    for (i = 0; i < g_nhistory; i++)
        if (g_history[i].is_short)
            return g_history[i].id;
    return NULL;
}

/* Recently watched videos, newest first, space separated (YouTube recommends from them). */
static int history_seeds(char *out, size_t outlen)
{
    int i, n = 0;
    out[0] = 0;
    for (i = 0; i < g_nhistory && n < FOR_YOU_SEEDS; i++) {
        size_t len = strlen(out);
        if (g_history[i].is_short || len + 13 >= outlen)
            continue;
        snprintf(out + len, outlen - len, "%s%s", n ? " " : "", g_history[i].id);
        n++;
    }
    return n;
}

/* ---- page loading -----------------------------------------------------------------------
 * One job slot. A job fills its own yt_feed with one page; the main thread merges it into
 * the page's list, so the UI never sees a list that is being changed. */

typedef struct {
    wt_thread *thread;
    volatile int done;
    int page, gen, first;
    yt_feed result;
    int rc;
    char err[256];
} feed_job;

static feed_job g_feed_job;

static void *feed_main(void *arg)
{
    feed_job *fj = arg;
    if (fj->first)
        fj->rc = yt_feed_start(&fj->result, fj->result.query, fj->result.kind, fj->err, sizeof fj->err);
    else
        fj->rc = yt_feed_more(&fj->result, fj->err, sizeof fj->err);
    fj->done = 1;
    return NULL;
}

/* Join the job if it has finished. Returns 1 if it was collected. */
static int job_finished(feed_job *fj)
{
    if (!fj->thread || !fj->done)
        return 0;
    wt_thread_join(fj->thread);
    fj->thread = NULL;
    return 1;
}

/* What a page shows: Home = the chip (For you = recommendations for what was watched, or
 * a topic searched like on YouTube), Search = the query. */
static void page_source(browse_state *b, int page, yt_feed *list)
{
    browse_page *pg = &b->pages[page];
    const char *topic = page == PAGE_HOME ? browse_chip_topic(b, pg->chip) : NULL;
    if (page == PAGE_SEARCH) {
        list->kind = YT_FEED_ALL;
        snprintf(list->query, sizeof list->query, "%s", pg->query);
    } else if (!topic && history_seeds(list->query, sizeof list->query) > 0) {
        list->kind = YT_FEED_RELATED;
    } else {
        list->kind = YT_FEED_ALL;
        snprintf(list->query, sizeof list->query, "%s", topic ? topic : ui_topics[0]);
    }
}

static void feed_start(browse_state *b, int page)
{
    browse_page *pg = &b->pages[page];
    feed_job *fj = &g_feed_job;
    const int first = !pg->started;

    if (fj->thread)
        return; /* busy; try again next frame */
    memset(&fj->result, 0, sizeof fj->result);
    if (first) {
        page_source(b, page, &pg->feed);
        fj->result.kind = pg->feed.kind;
        snprintf(fj->result.query, sizeof fj->result.query, "%s", pg->feed.query);
    } else if (!pg->feed.continuation || yt_feed_cursor(&fj->result, &pg->feed) != 0) {
        return;
    }
    fj->page = page;
    fj->gen = pg->gen;
    fj->first = first;
    fj->err[0] = 0;
    fj->done = 0;
    wlog("loading %s of \"%s\" (%s)", first ? "first page" : "next page", fj->result.query,
         fj->result.kind == YT_FEED_RELATED ? "recommendations" : "search");
    fj->thread = wt_thread_start(feed_main, fj, 256 * 1024, 0);
    if (!fj->thread) {
        wlog("could not start the page loader");
        yt_feed_free(&fj->result);
        return;
    }
    pg->started = 1;
    pg->loading = 1;
}

static void feed_collect(browse_state *b)
{
    feed_job *fj = &g_feed_job;
    browse_page *pg;
    int added;
    if (!job_finished(fj))
        return;
    pg = &b->pages[fj->page];
    if (fj->gen != pg->gen) { /* the page was reset meanwhile */
        yt_feed_free(&fj->result);
        return;
    }
    pg->loading = 0;
    if (fj->rc < 0) {
        wlog("load \"%s\" (kind %d) failed: %s", fj->result.query, fj->result.kind, fj->err);
        if (fj->first)
            pg->started = 0; /* so "try again" starts over */
        pg->failed = 1;
        snprintf(pg->status, sizeof pg->status, "Couldn't load videos: %s", fj->err);
        yt_feed_free(&fj->result);
        return;
    }
    added = yt_feed_merge(&pg->feed, &fj->result);
    wlog("loaded %d entries for \"%s\" (now %d, %d Shorts in %d shelves)%s", added, pg->feed.query, pg->feed.n,
         pg->feed.nshorts, pg->feed.nshelves, pg->feed.continuation ? "" : ", end of list");
    if (added == 0 && !fj->first && pg->feed.continuation) {
        free(pg->feed.continuation); /* a page with nothing new: stop rather than loop */
        pg->feed.continuation = NULL;
    }
    if (fj->first && pg->feed.n == 0)
        snprintf(pg->status, sizeof pg->status, "No results.");
}

void pages_tick(browse_state *b)
{
    feed_collect(b);
    if (b->want_more)
        feed_start(b, b->page);
}

int pages_loading(void) { return g_feed_job.thread != NULL; }

void pages_shutdown(void)
{
    while (g_feed_job.thread && !g_feed_job.done)
        wt_sleep_ms(20);
    if (job_finished(&g_feed_job))
        yt_feed_free(&g_feed_job.result);
}

void pages_init(const char *history_path)
{
    snprintf(g_history_path, sizeof g_history_path, "%s", history_path ? history_path : "");
    g_nhistory = 0;
    history_load();
}

int history_has_videos(void)
{
    int i;
    for (i = 0; i < g_nhistory; i++)
        if (!g_history[i].is_short)
            return 1;
    return 0;
}

void pages_start(browse_state *b)
{
    b->has_for_you = history_has_videos(); /* then Home opens on "For you" (chip 0) */
}

void pages_after_play(browse_state *b)
{
    if (!b->has_for_you && history_has_videos()) {
        b->has_for_you = 1;
        b->pages[PAGE_HOME].chip++; /* the topic chips moved one to the right */
        b->chip_sel++;
    }
}

int pages_handle(browse_state *b, int act)
{
    if (act == ACT_CHIP) {
        wlog("chip \"%s\"", browse_chip_topic(b, b->pages[b->page].chip) ? browse_chip_topic(b, b->pages[b->page].chip)
                                                                         : "For you");
        browse_reset_page(b, b->page);
        return 1;
    }
    return 0;
}

void pages_search(browse_state *b, const char *text)
{
    snprintf(b->pages[PAGE_SEARCH].query, sizeof b->pages[PAGE_SEARCH].query, "%s", text);
    browse_reset_page(b, PAGE_SEARCH);
    browse_set_page(b, PAGE_SEARCH);
    b->focus = FOCUS_GRID;
    wlog("search \"%s\"", text);
}

