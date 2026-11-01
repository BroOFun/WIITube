/* Checks the GPU (TEV) YUV->RGB constants against the exact formulas, over the whole
 * limited-range YUV cube, for BT.601 and BT.709. */
#include <math.h>
#include <stdio.h>

#include "../src/core/yuv_tev.h"

static double clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

int main(void)
{
    int failures = 0, bt;
    for (bt = 0; bt < 2; bt++) {
        const double rv = bt ? 1.792741 : 1.596027, gv = bt ? 0.532909 : 0.812968;
        const double gu = bt ? 0.213249 : 0.391762, bu = bt ? 2.112402 : 2.017232;
        yuv_tev_consts k;
        double sum = 0, maxerr = 0;
        long n = 0;
        int y, u, v, c;
        yuv_tev_constants(bt, &k);
        for (y = 16; y <= 235; y += 3)
            for (u = 16; u <= 240; u += 4)
                for (v = 16; v <= 240; v += 4) {
                    const double Y = 1.164383561643836 * (y - 16);
                    const double ref[3] = { clamp255(Y + rv * (v - 128)), clamp255(Y - gu * (u - 128) - gv * (v - 128)),
                                            clamp255(Y + bu * (u - 128)) };
                    uint8_t rgb[3];
                    yuv_tev_pixel(&k, y, u, v, rgb);
                    for (c = 0; c < 3; c++) {
                        double e = fabs(rgb[c] - ref[c]);
                        sum += e;
                        if (e > maxerr)
                            maxerr = e;
                    }
                    n += 3;
                }
        printf("%s: mean error %.2f, max error %.1f (of 255)  c0=(%d,%d,%d) ky=%d k1=%d k2=%d k3=%d k5=%d\n",
               bt ? "BT.709" : "BT.601", sum / n, maxerr, k.c0[0], k.c0[1], k.c0[2], k.ky, k.k1[0], k.k2[1],
               k.k3[2], k.k5[1]);
        if (sum / n > 1.0 || maxerr > 4.0) {
            printf("FAIL: TEV conversion too far from the formula\n");
            failures++;
        }
    }
    printf(failures ? "%d FAILURE(S)\n" : "all TEV conversion tests passed\n", failures);
    return failures != 0;
}
