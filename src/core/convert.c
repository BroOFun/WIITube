#include "convert.h"

#include <string.h>

/* Limited-range YUV -> RGB in 8.8 fixed point:
 *   R = 1.164 (Y-16) + cr_r (V-128)
 *   G = 1.164 (Y-16) - cb_g (U-128) - cr_g (V-128)
 *   B = 1.164 (Y-16) + cb_b (U-128)                                   */
typedef struct { int cr_r, cb_g, cr_g, cb_b; } coeffs;
static const coeffs k601 = { 409, 100, 208, 516 };
static const coeffs k709 = { 459, 55, 136, 541 };

static inline int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static inline uint16_t pack(int yv, int rv, int gv, int bv)
{
    int r = clamp8((yv + rv) >> 8), g = clamp8((yv + gv) >> 8), b = clamp8((yv + bv) >> 8);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void conv_yuv420_to_rgb565_tiled(const uint8_t *y, int ystride, const uint8_t *u, int ustride,
                                 const uint8_t *v, int vstride, int w, int h, int bt709, uint16_t *dst)
{
    const coeffs *k = bt709 ? &k709 : &k601;
    const int tw = conv_tex_dim(w), th = conv_tex_dim(h);
    const int tiles_per_row = tw >> 2;
    int px, py;

    /* Walk 2x2 pixel blocks; each shares one chroma sample and lies inside one 4x4 tile. */
    for (py = 0; py < th; py += 2) {
        const int sy0 = py < h ? py : h - 1;
        const int sy1 = py + 1 < h ? py + 1 : h - 1;
        const uint8_t *yrow0 = y + sy0 * ystride;
        const uint8_t *yrow1 = y + sy1 * ystride;
        const uint8_t *urow = u + (sy0 >> 1) * ustride;
        const uint8_t *vrow = v + (sy0 >> 1) * vstride;
        uint16_t *tile_row = dst + (py >> 2) * tiles_per_row * 16 + ((py & 3) << 2);

        for (px = 0; px < tw; px += 2) {
            const int sx0 = px < w ? px : w - 1;
            const int sx1 = px + 1 < w ? px + 1 : w - 1;
            const int cu = urow[sx0 >> 1] - 128, cv = vrow[sx0 >> 1] - 128;
            const int rv = k->cr_r * cv + 128;
            const int gv = -k->cb_g * cu - k->cr_g * cv + 128;
            const int bv = k->cb_b * cu + 128;
            uint16_t *p = tile_row + (px >> 2) * 16 + (px & 3);

            p[0] = pack(298 * (yrow0[sx0] - 16), rv, gv, bv);
            p[1] = pack(298 * (yrow0[sx1] - 16), rv, gv, bv);
            p[4] = pack(298 * (yrow1[sx0] - 16), rv, gv, bv);
            p[5] = pack(298 * (yrow1[sx1] - 16), rv, gv, bv);
        }
    }
}

void conv_tiled_pixel(const uint16_t *tex, int tex_w, int x, int y, uint8_t rgb[3])
{
    uint16_t p = tex[((y >> 2) * (tex_w >> 2) + (x >> 2)) * 16 + ((y & 3) << 2) + (x & 3)];
    rgb[0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
    rgb[1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
    rgb[2] = (uint8_t)((p & 31) * 255 / 31);
}

void conv_plane_to_i8(const uint8_t *src, int stride, int w, int h, uint8_t *dst)
{
    const int tw = conv_i8_w(w), th = conv_i8_h(h);
    const int full = w >> 3; /* tiles that lie completely inside the picture */
    int ty, tx, r;

    for (ty = 0; ty < th; ty += 4) {
        const uint8_t *row[4];
        for (r = 0; r < 4; r++)
            row[r] = src + (size_t)(ty + r < h ? ty + r : h - 1) * stride;
        /* Whole tiles: four 8-byte copies each (aligned, so the compiler uses word moves). */
        for (tx = 0; tx < full; tx++) {
            const int x = tx << 3;
            memcpy(dst, row[0] + x, 8);
            memcpy(dst + 8, row[1] + x, 8);
            memcpy(dst + 16, row[2] + x, 8);
            memcpy(dst + 24, row[3] + x, 8);
            dst += 32;
        }
        /* Last, partial tile: repeat the edge pixel. */
        for (tx = full << 3; tx < tw; tx += 8)
            for (r = 0; r < 4; r++) {
                int i;
                for (i = 0; i < 8; i++)
                    *dst++ = row[r][tx + i < w ? tx + i : w - 1];
            }
    }
}
