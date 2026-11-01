/* YUV 4:2:0 -> RGB565 in the Wii GPU's 4x4-tiled texture layout (GX_TF_RGB565). */
#ifndef WIITUBE_CONVERT_H
#define WIITUBE_CONVERT_H

#include <stdint.h>

/* Texture dimensions for a frame: width/height rounded up to multiples of 4. */
static inline int conv_tex_dim(int n) { return (n + 3) & ~3; }

/* Convert a frame. dst holds conv_tex_dim(w) * conv_tex_dim(h) 16-bit pixels (big-endian on
 * the Wii, native endian elsewhere). Padding is filled by repeating the edge pixels. */
void conv_yuv420_to_rgb565_tiled(const uint8_t *y, int ystride, const uint8_t *u, int ustride,
                                 const uint8_t *v, int vstride, int w, int h, int bt709, uint16_t *dst);

/* Test helper: read pixel (x, y) of a tiled RGB565 texture as 8-bit R, G, B. */
void conv_tiled_pixel(const uint16_t *tex, int tex_w, int x, int y, uint8_t rgb[3]);

/* ---- planar path: the GPU does the colour conversion ---- */

/* I8 textures are stored in 8x4-texel tiles: width rounds up to 8, height to 4. */
static inline int conv_i8_w(int n) { return (n + 7) & ~7; }
static inline int conv_i8_h(int n) { return (n + 3) & ~3; }

/* A decoded 4:2:0 frame as three I8-tiled planes, ready to be textures. */
typedef struct {
    const uint8_t *y, *u, *v;
    int w, h;          /* picture (luma) size */
    int yw, yh;        /* luma texture size */
    int cw, ch;        /* chroma size ((w + 1) / 2 x (h + 1) / 2) */
    int cwt, cht;      /* chroma texture size */
    int bt709;         /* colour matrix */
} yuv_frame;

/* Rearrange one 8-bit plane (w x h, stride bytes per row) into I8 tiles of dst, which holds
 * conv_i8_w(w) * conv_i8_h(h) bytes. Padding repeats the last column / row so the GPU's
 * bilinear filter has no dark edge. */
void conv_plane_to_i8(const uint8_t *src, int stride, int w, int h, uint8_t *dst);
/* Read texel (x, y) of an I8-tiled plane whose texture width is tw. */
static inline uint8_t conv_i8_texel(const uint8_t *tex, int tw, int x, int y)
{
    return tex[((y >> 2) * (tw >> 3) + (x >> 3)) * 32 + ((y & 3) << 3) + (x & 7)];
}

#endif
