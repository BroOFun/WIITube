#include "image.h"
#include "convert.h"

#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* no thread-local storage on the Wii */
#define STBI_NO_SIMD
#define STBI_NO_HDR    /* JPEG only: no float/HDR paths (and no libm) */
#define STBI_NO_LINEAR
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void (*g_flush)(void *p, size_t len);
static uint16_t *alloc_tex(wt_image *img, int w, int h, int fmt);

void image_set_flush(void (*flush)(void *p, size_t len)) { g_flush = flush; }

wt_image *image_from_rgb(const unsigned char *pix, int w, int h, int channels, int max_w)
{
    int f = 1, ow, oh, x, y;
    wt_image *img;

    if (!pix || w <= 0 || h <= 0)
        return NULL;
    while (max_w > 0 && w / f > max_w)
        f++;
    ow = w / f;
    oh = h / f;
    if (ow < 1 || oh < 1)
        return NULL;
    img = calloc(1, sizeof *img);
    if (!img || !alloc_tex(img, ow, oh, IMG_RGB565)) {
        free(img);
        return NULL;
    }

    for (y = 0; y < img->th; y++) {
        for (x = 0; x < img->tw; x++) {
            /* Average an f x f block (edges repeat for the padding). */
            int sx = (x < ow ? x : ow - 1) * f, sy = (y < oh ? y : oh - 1) * f;
            int r = 0, g = 0, b = 0, i, j;
            uint16_t px;
            for (j = 0; j < f; j++)
                for (i = 0; i < f; i++) {
                    const unsigned char *p = pix + ((size_t)(sy + j) * w + (sx + i)) * channels;
                    r += p[0];
                    g += p[channels > 2 ? 1 : 0];
                    b += p[channels > 2 ? 2 : 0];
                }
            r /= f * f;
            g /= f * f;
            b /= f * f;
            px = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            img->tex[((y >> 2) * (img->tw >> 2) + (x >> 2)) * 16 + ((y & 3) << 2) + (x & 3)] = px;
        }
    }
    if (g_flush)
        g_flush(img->tex, (size_t)img->tw * img->th * 2);
    return img;
}

wt_image *image_from_jpeg(const unsigned char *data, size_t len, int max_w)
{
    int w, h, n;
    wt_image *img;
    unsigned char *pix = stbi_load_from_memory(data, (int)len, &w, &h, &n, 3);
    if (!pix)
        return NULL;
    img = image_from_rgb(pix, w, h, 3, max_w);
    stbi_image_free(pix);
    return img;
}

static uint16_t *alloc_tex(wt_image *img, int w, int h, int fmt)
{
    img->w = w;
    img->h = h;
    img->fmt = fmt;
    img->tw = conv_tex_dim(w);
    img->th = conv_tex_dim(h);
    img->raw = malloc((size_t)img->tw * img->th * 2 + 32);
    if (!img->raw)
        return NULL;
    img->tex = (uint16_t *)(((uintptr_t)img->raw + 31) & ~(uintptr_t)31);
    return img->tex;
}

wt_image *image_from_rgba(const unsigned char *rgba, int w, int h)
{
    wt_image *img = calloc(1, sizeof *img);
    int x, y;
    if (!img || !alloc_tex(img, w, h, IMG_RGB5A3)) {
        free(img);
        return NULL;
    }
    for (y = 0; y < img->th; y++)
        for (x = 0; x < img->tw; x++) {
            const unsigned char *p = rgba + ((size_t)(y < h ? y : h - 1) * w + (x < w ? x : w - 1)) * 4;
            uint16_t v;
            if (x >= w || y >= h)
                v = 0; /* transparent padding */
            else if (p[3] >= 0xe0) /* opaque: 1 RRRRR GGGGG BBBBB */
                v = (uint16_t)(0x8000 | ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
            else /* translucent: 0 AAA RRRR GGGG BBBB */
                v = (uint16_t)(((p[3] >> 5) << 12) | ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4));
            img->tex[((y >> 2) * (img->tw >> 2) + (x >> 2)) * 16 + ((y & 3) << 2) + (x & 3)] = v;
        }
    if (g_flush)
        g_flush(img->tex, (size_t)img->tw * img->th * 2);
    return img;
}

void image_wrap_rgb565(wt_image *img, const uint16_t *tex, int w, int h, int tw, int th)
{
    img->w = w;
    img->h = h;
    img->tw = tw;
    img->th = th;
    img->fmt = IMG_RGB565;
    img->tex = (uint16_t *)tex;
    img->raw = NULL;
}

void image_free(wt_image *img)
{
    if (!img)
        return;
    free(img->raw);
    free(img);
}

void image_pixel(const wt_image *img, int x, int y, uint8_t rgb[3])
{
    uint8_t rgba[4];
    image_pixel_rgba(img, x, y, rgba);
    rgb[0] = rgba[0];
    rgb[1] = rgba[1];
    rgb[2] = rgba[2];
}

void image_pixel_rgba(const wt_image *img, int x, int y, uint8_t rgba[4])
{
    uint16_t v = img->tex[((y >> 2) * (img->tw >> 2) + (x >> 2)) * 16 + ((y & 3) << 2) + (x & 3)];
    if (img->fmt == IMG_RGB565) {
        conv_tiled_pixel(img->tex, img->tw, x, y, rgba);
        rgba[3] = 255;
    } else if (v & 0x8000) {
        rgba[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
        rgba[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
        rgba[2] = (uint8_t)((v & 31) * 255 / 31);
        rgba[3] = 255;
    } else {
        rgba[3] = (uint8_t)(((v >> 12) & 7) * 255 / 7);
        rgba[0] = (uint8_t)(((v >> 8) & 15) * 17);
        rgba[1] = (uint8_t)(((v >> 4) & 15) * 17);
        rgba[2] = (uint8_t)((v & 15) * 17);
    }
}
