#include "yuv_tev.h"

/* Pick the 8-bit TEV multiplier closest to x. The hardware uses c + (c >> 7) out of 256,
 * so 255 means exactly 1.0. */
static uint8_t mult8(double x)
{
    int c, best = 0;
    double best_err = 1e9;
    for (c = 0; c < 256; c++) {
        double err = (c + (c >> 7)) / 256.0 - x;
        if (err < 0)
            err = -err;
        if (err < best_err) {
            best_err = err;
            best = c;
        }
    }
    return (uint8_t)best;
}

static int16_t round_s(double x) { return (int16_t)(x >= 0 ? x + 0.5 : x - 0.5); }

void yuv_tev_constants(int bt709, yuv_tev_consts *k)
{
    const double ky = 1.164383561643836; /* 255 / 219 */
    const double y0 = 16.0 / 255.0;
    const double rv = bt709 ? 1.792741 : 1.596027;
    const double gv = bt709 ? 0.532909 : 0.812968;
    const double gu = bt709 ? 0.213249 : 0.391762;
    const double bu = bt709 ? 2.112402 : 2.017232;
    /* Constant terms (from -16 on Y and -128 on U/V), at half scale, in 1/255 units. */
    const double cr = -ky * y0 - rv * 0.5;
    const double cg = -ky * y0 + (gv + gu) * 0.5;
    const double cb = -ky * y0 - bu * 0.5;
    int i;

    k->c0[0] = round_s(cr / 2 * 255);
    k->c0[1] = round_s(cg / 2 * 255);
    k->c0[2] = round_s(cb / 2 * 255);
    k->ky = mult8(ky / 2);
    for (i = 0; i < 3; i++)
        k->k1[i] = k->k2[i] = k->k3[i] = k->k5[i] = 0;
    k->k1[0] = mult8(rv / 2);
    k->k2[1] = mult8(gv / 2);
    k->k3[2] = mult8(bu / 2 - 1.0); /* stage 3 already added 1.0 * U */
    k->k5[1] = mult8(gu / 2);
}

/* One TEV colour channel: result = (d + bias) * scale +/- lerp(a, b, c) * scale, following
 * the integer arithmetic of the hardware (as modelled by the Dolphin emulator). */
static int tev(int a, int b, int c, int d, int sub, int scale_shift, int clamp)
{
    const int cc = c + (c >> 7);
    int lerp = a * (256 - cc) + b * cc;
    int r;
    lerp <<= scale_shift;
    lerp += sub ? 127 : 128;
    lerp >>= 8;
    if (sub)
        lerp = -lerp;
    r = d * (1 << scale_shift) + lerp; /* (a left shift of a negative value is undefined in C) */
    if (clamp)
        r = r < 0 ? 0 : r > 255 ? 255 : r;
    else
        r = r < -1024 ? -1024 : r > 1023 ? 1023 : r;
    return r;
}

void yuv_tev_pixel(const yuv_tev_consts *k, int y, int u, int v, uint8_t rgb[3])
{
    static const uint8_t blue_only[3] = { 0, 0, 255 }; /* vertex colour used in stage 3 */
    int ch;
    for (ch = 0; ch < 3; ch++) {
        int p = tev(0, y, k->ky, k->c0[ch], 0, 0, 0);
        p = tev(0, v, k->k1[ch], p, 0, 0, 0);
        p = tev(0, v, k->k2[ch], p, 1, 0, 0);
        p = tev(0, u, blue_only[ch], p, 0, 0, 0);
        p = tev(0, u, k->k3[ch], p, 0, 0, 0);
        p = tev(0, u, k->k5[ch], p, 1, 1, 1);
        rgb[ch] = (uint8_t)p;
    }
}
