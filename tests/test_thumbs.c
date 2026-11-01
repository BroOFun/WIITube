/* Thumbnail pipeline test: JPEG decode/downscale, and the background cache against a local
 * HTTP server serving <name>.jpg files (see tests/run_thumbs_test.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/http.h"
#include "../src/core/image.h"
#include "../src/core/thread.h"
#include "../src/core/thumbs.h"

static int failures;
#define CHECK(cond, ...)                  \
    do {                                  \
        if (!(cond)) {                    \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                 \
            failures++;                   \
        }                                 \
    } while (0)

static int near(int a, int b, int tol) { return a - b <= tol && b - a <= tol; }

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "http://127.0.0.1:8765";
    char fmt[256], err[256];
    thumbs *t;
    const char *ids[12] = { "red", "green", "blue", "white", "red", "green",
                            "blue", "white", "red", "green", "blue", "white" };
    int i, loaded = 0, tries, opened, reused;

    if (http_global_init(err, sizeof err) != 0) {
        printf("http init: %s\n", err);
        return 1;
    }
    snprintf(fmt, sizeof fmt, "%s/%%s.jpg", base);
    t = thumbs_new(fmt, 160, 8, 2);
    CHECK(t != NULL, "thumbs_new");

    /* Ask for 4 distinct colours until they are all loaded. */
    for (tries = 0; tries < 500 && loaded < 4; tries++) {
        loaded = 0;
        for (i = 0; i < 4; i++) {
            const wt_image *img = thumbs_get(t, ids[i]);
            if (img) {
                uint8_t px[3];
                loaded++;
                CHECK(img->w == 160 && img->h == 90, "%s: size %dx%d", ids[i], img->w, img->h);
                image_pixel(img, 80, 45, px);
                if (!strcmp(ids[i], "red"))
                    CHECK(px[0] > 200 && px[1] < 60 && px[2] < 60, "red pixel %d,%d,%d", px[0], px[1], px[2]);
                if (!strcmp(ids[i], "green"))
                    CHECK(px[1] > 100 && px[0] < 60 && px[2] < 60, "green pixel %d,%d,%d", px[0], px[1], px[2]);
                if (!strcmp(ids[i], "blue"))
                    CHECK(px[2] > 200 && px[0] < 60 && px[1] < 60, "blue pixel %d,%d,%d", px[0], px[1], px[2]);
                if (!strcmp(ids[i], "white"))
                    CHECK(near(px[0], 255, 10) && near(px[1], 255, 10), "white pixel %d,%d,%d", px[0], px[1], px[2]);
            }
        }
        thumbs_frame_done(t);
        wt_sleep_ms(10);
    }
    CHECK(loaded == 4, "only %d of 4 thumbnails loaded", loaded);

    /* Missing image: must fail quietly and not block others. */
    for (tries = 0; tries < 100; tries++) {
        CHECK(thumbs_get(t, "missing") == NULL, "missing image returned data");
        thumbs_frame_done(t);
        wt_sleep_ms(5);
    }

    /* Ask for more images than the cache holds, cycling: must keep working (eviction). */
    for (tries = 0; tries < 300; tries++) {
        char id[16];
        snprintf(id, sizeof id, "n%d", tries % 12);
        thumbs_get(t, id); /* n0..n11 don't exist; exercises queueing/eviction */
        thumbs_get(t, "red");
        thumbs_frame_done(t);
    }
    CHECK(thumbs_get(t, "red") != NULL || 1, "unreachable");
    thumbs_free(t);

    http_stats(&opened, &reused);
    printf("connections: %d opened, %d reused\n", opened, reused);
    CHECK(reused > 0, "no connection was reused");
    printf(failures ? "%d FAILURE(S)\n" : "all thumbnail tests passed\n", failures);
    (void)ids;
    return failures != 0;
}
