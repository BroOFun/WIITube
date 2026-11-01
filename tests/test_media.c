/* Desktop test of the player pipeline: file -> (simulated network thread) -> ring ->
 * FFmpeg demux/decode -> tiled RGB565 conversion.
 *
 * usage: test_media VIDEO.mp4 AUDIO.m4a REF_FIRST_FRAME.rgb
 *   REF is the first video frame as raw RGB24 from:
 *   ffmpeg -i VIDEO.mp4 -frames:v 1 -f rawvideo -pix_fmt rgb24 REF.rgb */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/convert.h"
#include "../src/core/media.h"
#include "../src/core/ring.h"
#include "../src/core/thread.h"

static int failures;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL: " __VA_ARGS__);                   \
            printf("\n");                                   \
            failures++;                                     \
        }                                                   \
    } while (0)

typedef struct {
    const char *path;
    ring *r;
    int result; /* 0 ok, -1 aborted */
} feeder;

/* Writes the file in uneven chunks with short pauses, like a network download. */
static void *feed_main(void *arg)
{
    feeder *fd = arg;
    FILE *f = fopen(fd->path, "rb");
    unsigned char buf[7000];
    unsigned seed = 1;
    size_t n;
    fd->result = 0;
    if (!f) {
        ring_finish(fd->r, 1);
        return NULL;
    }
    for (;;) {
        size_t want = 100 + (seed = seed * 1103515245u + 12345u) % sizeof buf;
        if (want > sizeof buf)
            want = sizeof buf;
        n = fread(buf, 1, want, f);
        if (n == 0)
            break;
        if (ring_write(fd->r, buf, n) != 0) {
            fd->result = -1;
            break;
        }
        if (seed % 7 == 0)
            wt_sleep_ms(1);
    }
    fclose(f);
    ring_finish(fd->r, 0);
    return NULL;
}

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void test_video(const char *path, const char *ref_path)
{
    feeder fd = { path, ring_new(64 * 1024), 0 };
    wt_thread *t = wt_thread_start(feed_main, &fd, 0, 1);
    char err[256];
    media *m = media_open(fd.r, MEDIA_VIDEO, err, sizeof err);
    media_frame fr;
    int frames = 0, rc;
    double last = -1;
    uint16_t *tex = NULL;

    CHECK(m != NULL, "media_open(video): %s", err);
    if (!m) {
        ring_abort(fd.r);
        wt_thread_join(t);
        ring_free(fd.r);
        return;
    }
    printf("video: %dx%d\n", media_width(m), media_height(m));
    while ((rc = media_next_video(m, &fr)) == 1) {
        CHECK(fr.pts > last, "video pts not increasing: %f after %f", fr.pts, last);
        last = fr.pts;
        if (frames == 0) {
            int tw = conv_tex_dim(fr.width), th = conv_tex_dim(fr.height);
            size_t ref_len = 0;
            unsigned char *ref = read_file(ref_path, &ref_len);
            tex = malloc((size_t)tw * th * 2);
            conv_yuv420_to_rgb565_tiled(fr.plane[0], fr.stride[0], fr.plane[1], fr.stride[1], fr.plane[2],
                                        fr.stride[2], fr.width, fr.height, fr.bt709, tex);
            CHECK(ref && ref_len == (size_t)fr.width * fr.height * 3, "reference frame missing or wrong size");
            if (ref && ref_len == (size_t)fr.width * fr.height * 3) {
                double sum = 0;
                int x, y, c;
                for (y = 0; y < fr.height; y++)
                    for (x = 0; x < fr.width; x++) {
                        uint8_t px[3];
                        conv_tiled_pixel(tex, tw, x, y, px);
                        for (c = 0; c < 3; c++)
                            sum += fabs((double)px[c] - ref[(y * fr.width + x) * 3 + c]);
                    }
                sum /= (double)fr.width * fr.height * 3;
                printf("first frame: bt709=%d, mean abs error vs ffmpeg RGB = %.2f\n", fr.bt709, sum);
                CHECK(sum < 6.0, "colour conversion error too high (%.2f)", sum);
                /* Padding columns repeat the last real column. */
                if (tw > fr.width) {
                    uint8_t a[3], b[3];
                    conv_tiled_pixel(tex, tw, fr.width - 1, 0, a);
                    conv_tiled_pixel(tex, tw, tw - 1, 0, b);
                    CHECK(memcmp(a, b, 3) == 0, "padding does not repeat the edge");
                }
            }
            free(ref);
        }
        frames++;
    }
    CHECK(rc == 0, "video ended with error %d", rc);
    printf("video: %d frames, last pts %.3f\n", frames, last);
    CHECK(frames == 75, "expected 75 frames, got %d", frames);
    media_close(m);
    wt_thread_join(t);
    ring_free(fd.r);
    free(tex);
}

static void test_audio(const char *path)
{
    feeder fd = { path, ring_new(16 * 1024), 0 };
    wt_thread *t = wt_thread_start(feed_main, &fd, 0, 1);
    char err[256];
    media *m = media_open(fd.r, MEDIA_AUDIO, err, sizeof err);
    int16_t pcm[8192 * 2];
    long long total = 0;
    double pts, last = -1, peak = 0;
    int frames, rc, i;

    CHECK(m != NULL, "media_open(audio): %s", err);
    if (!m) {
        ring_abort(fd.r);
        wt_thread_join(t);
        ring_free(fd.r);
        return;
    }
    while ((rc = media_next_audio(m, pcm, 8192, &frames, &pts)) == 1) {
        CHECK(pts > last, "audio pts not increasing");
        last = pts;
        total += frames;
        for (i = 0; i < frames * 2; i++)
            if (abs(pcm[i]) > peak)
                peak = abs(pcm[i]);
    }
    CHECK(rc == 0, "audio ended with error %d", rc);
    printf("audio: %d Hz, %lld frames (%.3f s), peak %.0f\n", media_sample_rate(m), total,
           (double)total / media_sample_rate(m), peak);
    CHECK(fabs((double)total / media_sample_rate(m) - 3.0) < 0.1, "audio length is not ~3 s");
    CHECK(peak > 5000, "audio is silent");
    media_close(m);
    wt_thread_join(t);
    ring_free(fd.r);
}

/* The consumer gives up part-way: the producer must stop instead of blocking forever. */
static void test_abort(const char *path)
{
    feeder fd = { path, ring_new(4096), 0 };
    wt_thread *t = wt_thread_start(feed_main, &fd, 0, 1);
    unsigned char buf[1000];
    size_t n = ring_read(fd.r, buf, sizeof buf);
    CHECK(n > 0, "ring_read returned nothing");
    ring_abort(fd.r);
    wt_thread_join(t);
    CHECK(fd.result == -1, "producer did not see the abort");
    CHECK(ring_read(fd.r, buf, sizeof buf) == 0, "read after abort returned data");
    ring_free(fd.r);
    printf("abort: ok\n");
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: test_media VIDEO.mp4 AUDIO.m4a REF.rgb\n");
        return 2;
    }
    test_video(argv[1], argv[3]);
    test_audio(argv[2]);
    test_abort(argv[1]);
    printf(failures ? "%d FAILURE(S)\n" : "all media tests passed\n", failures);
    return failures != 0;
}
