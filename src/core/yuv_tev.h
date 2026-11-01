/* YUV -> RGB on the Wii GPU: constants for a 6-stage TEV (texture environment) setup, plus
 * a bit-exact software model of those stages used by tests and the desktop preview.
 *
 * The planes are three I8 textures. With y, u, v in [0,1] and the usual limited-range
 * formulas (BT.601 or BT.709), the stages compute, at half scale so every coefficient
 * fits the TEV's 8-bit multiplier:
 *   0: P = C0 + ky*Y                (C0 = per-channel constants / 2, ky = 1.164 / 2)
 *   1: P = P + (k1, 0, 0) * V        (red from V)
 *   2: P = P - (0, k2, 0) * V        (green from V)
 *   3: P = P + (0, 0, 1) * U         (blue from U, first 1.0 via the vertex colour)
 *   4: P = P + (0, 0, k3) * U        (blue from U, the rest)
 *   5: P = 2 * (P - (0, k5, 0) * U), clamped to 0..255   (green from U, back to full scale)
 */
#ifndef WIITUBE_YUV_TEV_H
#define WIITUBE_YUV_TEV_H

#include <stdint.h>

typedef struct {
    int16_t c0[3];   /* signed register constants (TEVREG0), 1/255 units */
    uint8_t ky;      /* stage 0 multiplier (all channels) */
    uint8_t k1[3];   /* stage 1 konst colour */
    uint8_t k2[3];   /* stage 2 */
    uint8_t k3[3];   /* stage 4 */
    uint8_t k5[3];   /* stage 5 */
} yuv_tev_consts;

void yuv_tev_constants(int bt709, yuv_tev_consts *k);

/* Run the six stages on one pixel (y, u, v as 0..255 texel values). */
void yuv_tev_pixel(const yuv_tev_consts *k, int y, int u, int v, uint8_t rgb[3]);

#endif
