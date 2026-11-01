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

#endif
