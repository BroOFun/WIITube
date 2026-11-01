/* App simulation against real YouTube (run on GitHub Actions): drives the same browse
 * screen, keyboard and page loader as the Wii app (src/ui, src/app/pages.c) with simulated
 * Wii Remote input, waits for the pages like the Wii's main loop does, and checks that
 * every step shows videos. Screenshots go to OUT_DIR.
 * usage: app_sim OUT_DIR [rules.json] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/app/pages.h"
#include "../src/core/http.h"
#include "../src/core/log.h"
#include "../src/core/thread.h"
#include "../src/core/thumbs.h"
#include "../src/core/yt.h"
#include "../src/platform/linux/soft_draw.h"
#include "../src/ui/ui.h"

extern const int logo_small_w, logo_small_h;
extern const unsigned char logo_small_rgba[];

#define W 854

static thumbs *g_thumbs;
static browse_state g_b;
static keyboard_state g_kb;
static int g_kb_open, g_failures;
static const char *g_out;

#define CHECK(cond, ...)                         \
    do {                                         \
        if (!(cond)) {                           \
            printf("FAIL: " __VA_ARGS__);        \
            printf("\n");                        \
            g_failures++;                        \
        }                                        \
    } while (0)

static const wt_image *thumb_cb(void *ud, const yt_search_result *r, int vertical)
{
    (void)ud;
    (void)vertical;
    return thumbs_get(g_thumbs, r->video_id);
}

/* One frame of the Wii main loop (src/platform/wii/main.c), minus video playback. */
static int frame(unsigned pressed, float stick_y)
{
    ui_input in;
    int act;
    memset(&in, 0, sizeof in);
    in.pressed = pressed;
    in.stick_y = stick_y;
    if (g_kb_open) {
        act = keyboard_update(&g_kb, &in);
        if (act == ACT_KB_SEARCH) {
            g_kb_open = 0;
            pages_search(&g_b, g_kb.text);
        } else if (act == ACT_KB_CANCEL) {
            g_kb_open = 0;
        }
        g_b.want_more = 0;
    } else {
        act = browse_update(&g_b, &in);
        if (act == ACT_OPEN_KEYBOARD) {
            keyboard_open(&g_kb, g_b.pages[PAGE_SEARCH].query);
            g_kb_open = 1;
        } else {
            pages_handle(&g_b, act);
        }
    }
    pages_tick(&g_b);
    soft_begin(W);
    browse_draw(&g_b, thumb_cb, NULL);
    if (g_kb_open)
        keyboard_draw(&g_kb);
    thumbs_frame_done(g_thumbs);
    wt_sleep_ms(30);
    return act;
}

/* Run frames until the current page has finished loading (max ~40 s). */
static const browse_page *wait_loaded(void)
{
    const browse_page *pg = &g_b.pages[g_b.page];
    int i;
    for (i = 0; i < 1300; i++) {
        frame(0, 0);
        if ((pg->started && !pg->loading && !pages_loading()) || pg->failed)
            break;
    }
    for (i = 0; i < 40; i++) /* let thumbnails arrive */
        frame(0, 0);
    return pg;
}

static void save(const char *name)
{
    char path[512];
    soft_begin(W);
    browse_draw(&g_b, thumb_cb, NULL);
    if (g_kb_open)
        keyboard_draw(&g_kb);
    snprintf(path, sizeof path, "%s/%s-%d.ppm", g_out, name, W);
    soft_save_ppm(path);
}

static void report(const char *step, const browse_page *pg)
{
    int i, shown = 0;
    printf("%s: \"%s\" kind %d: %d entries, %d Shorts in %d shelves, more: %s%s%s\n", step, pg->feed.query,
           pg->feed.kind, pg->feed.n, pg->feed.nshorts, pg->feed.nshelves, pg->feed.continuation ? "yes" : "no",
           pg->status[0] ? ", status: " : "", pg->status);
    for (i = 0; i < pg->feed.n && shown < 3; i++)
        if (!pg->feed.items[i].is_short) {
            printf("  %s | %.60s | %s\n", pg->feed.items[i].video_id, pg->feed.items[i].title, pg->feed.items[i].channel);
            shown++;
        }
}

/* Type text on the on-screen keyboard with the D-pad, as a Wii Remote would. */
static void kb_type(const char *text)
{
    static const char *const rows[4] = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm-.&" };
    const char *c;
    for (c = text; *c; c++) {
        int r, col = -1;
        if (*c == ' ') {
            frame(UI_1, 0);
            continue;
        }
        for (r = 0; r < 4 && col < 0; r++)
            if (strchr(rows[r], *c))
                col = (int)(strchr(rows[r], *c) - rows[r]);
        r--;
        while (g_kb.row != r)
            frame(g_kb.row < r ? UI_DOWN : UI_UP, 0);
        while (g_kb.col != col)
            frame(g_kb.col < col ? UI_RIGHT : UI_LEFT, 0);
        frame(UI_A, 0);
    }
}

int main(int argc, char **argv)
{
    char err[512], path[512];
    const browse_page *pg;
    int i, before;
    wt_image *logo;

    g_out = argc > 1 ? argv[1] : ".";
    if (yt_load_rules(argc > 2 ? argv[2] : "rules/youtube.json", err, sizeof err) != 0 ||
        http_global_init(err, sizeof err) != 0) {
        fprintf(stderr, "setup failed: %s\n", err);
        return 1;
    }
    snprintf(path, sizeof path, "%s/history.txt", g_out);
    remove(path);
    pages_init(path);
    g_thumbs = thumbs_new("https://i.ytimg.com/vi/%s/mqdefault.jpg", 160, 64, 2);
    logo = image_from_rgba(logo_small_rgba, logo_small_w, logo_small_h);
    soft_begin(W);
    browse_init(&g_b, logo);
    pages_start(&g_b);

    /* 1. Start: Home loads the first topic chip, like on the Wii. */
    pg = wait_loaded();
    report("home", pg);
    CHECK(pg->feed.n > 0, "Home shows no videos");
    save("app-home");

    /* 2. Up to the chips, Right to the next topic, A. */
    frame(UI_UP, 0);
    frame(UI_RIGHT, 0);
    CHECK(frame(UI_A, 0) == ACT_CHIP, "A on a chip did not change the topic");
    pg = wait_loaded();
    report("chip", pg);
    CHECK(pg->feed.n > 0, "the topic chip shows no videos");
    CHECK(strcmp(pg->feed.query, browse_chip_topic(&g_b, g_b.pages[PAGE_HOME].chip)) == 0, "chip loaded \"%s\"",
          pg->feed.query);
    save("app-chip");

    /* 3. + opens the keyboard; type a search with the D-pad; + searches. */
    CHECK(frame(UI_PLUS, 0) == ACT_OPEN_KEYBOARD && g_kb_open, "+ did not open the keyboard");
    kb_type("wii sports");
    CHECK(strcmp(g_kb.text, "wii sports") == 0, "typed \"%s\"", g_kb.text);
    save("app-keyboard");
    frame(UI_PLUS, 0);
    CHECK(!g_kb_open && g_b.page == PAGE_SEARCH, "+ on the keyboard did not search");
    pg = wait_loaded();
    report("search", pg);
    CHECK(pg->feed.n > 0, "search shows no videos");
    CHECK(pg->feed.nshorts > 0 && pg->feed.nshelves > 0, "search shows no Shorts shelf");
    save("app-search");

    /* 4. Scroll down with the Nunchuk stick: the next page loads. */
    before = pg->feed.n;
    for (i = 0; i < 1300 && pg->feed.n == before && !pg->failed; i++)
        frame(0, 1.0f);
    for (i = 0; i < 60; i++)
        frame(0, 0);
    report("search, scrolled", pg);
    CHECK(pg->feed.n > before, "scrolling did not load more results");
    save("app-search-more");

    /* 5. After watching a video, Home has "For you" (YouTube's recommendations). */
    {
        const char *watched = NULL;
        for (i = 0; i < pg->feed.n && !watched; i++)
            if (!pg->feed.items[i].is_short)
                watched = pg->feed.items[i].video_id;
        CHECK(watched != NULL, "no video to watch");
        if (watched)
            history_add(watched, 0);
        pages_after_play(&g_b);
    }
    frame(UI_B, 0); /* back Home */
    CHECK(g_b.page == PAGE_HOME && g_b.has_for_you, "B did not go Home / no For you chip");
    frame(UI_UP, 0);
    while (g_b.chip_sel > 0)
        frame(UI_LEFT, 0);
    CHECK(frame(UI_A, 0) == ACT_CHIP, "A on For you did not load it");
    pg = wait_loaded();
    report("for you", pg);
    CHECK(pg->feed.kind == YT_FEED_RELATED && pg->feed.n > 0, "For you shows no recommendations");
    save("app-foryou");

    pages_shutdown();
    {
        int opened, reused;
        http_stats(&opened, &reused);
        printf("connections: %d opened, %d reused\n", opened, reused);
    }
    printf(g_failures ? "%d FAILURE(S)\n" : "app simulation passed\n", g_failures);
    return g_failures != 0;
}
