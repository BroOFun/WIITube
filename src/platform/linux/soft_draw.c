/* Software implementation of ui/draw.h for desktop previews of the Wii UI. */
#include "soft_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/yuv_tev.h"
#include "../../ui/atlas.h"
#include "../../ui/draw.h"

static int g_w = 640;
static unsigned char *g_fb; /* RGB */
static int g_clip;
static float g_cx0, g_cy0, g_cx1, g_cy1;

void soft_begin(int width)
{
    g_w = width;
    free(g_fb);
    g_fb = calloc((size_t)width * UI_H, 3);
    g_clip = 0;
}

int ui_width(void) { return g_w; }

static void unpack(rgba_t c, float *r, float *g, float *b, float *a)
{
    *r = (float)(c >> 24);
    *g = (float)((c >> 16) & 255);
    *b = (float)((c >> 8) & 255);
    *a = (float)(c & 255) / 255.0f;
}

static void blend(int x, int y, float r, float g, float b, float a)
{
    unsigned char *p;
    if (a <= 0)
        return;
    if (a > 1)
        a = 1;
    p = g_fb + ((size_t)y * g_w + x) * 3;
    p[0] = (unsigned char)(p[0] + (r - p[0]) * a + 0.5f);
    p[1] = (unsigned char)(p[1] + (g - p[1]) * a + 0.5f);
    p[2] = (unsigned char)(p[2] + (b - p[2]) * a + 0.5f);
}

/* Pixels whose centres lie inside the rectangle, limited to the screen and clip. */
static int span(float x, float y, float w, float h, int *x0, int *y0, int *x1, int *y1)
{
    float ax = x, ay = y, bx = x + w, by = y + h;
    if (g_clip) {
        if (ax < g_cx0) ax = g_cx0;
        if (ay < g_cy0) ay = g_cy0;
        if (bx > g_cx1) bx = g_cx1;
        if (by > g_cy1) by = g_cy1;
    }
    *x0 = (int)(ax + 0.5f);
    *y0 = (int)(ay + 0.5f);
    *x1 = (int)(bx + 0.5f);
    *y1 = (int)(by + 0.5f);
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > g_w) *x1 = g_w;
    if (*y1 > UI_H) *y1 = UI_H;
    return *x1 > *x0 && *y1 > *y0;
}

void dr_fill(float x, float y, float w, float h, rgba_t c)
{
    int x0, y0, x1, y1, px, py;
    float r, g, b, a;
    unpack(c, &r, &g, &b, &a);
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1))
        return;
    for (py = y0; py < y1; py++)
        for (px = x0; px < x1; px++)
            blend(px, py, r, g, b, a);
}

void dr_vgradient(float x, float y, float w, float h, rgba_t top, rgba_t bottom)
{
    int x0, y0, x1, y1, px, py;
    float r0, g0, b0, a0, r1, g1, b1, a1;
    unpack(top, &r0, &g0, &b0, &a0);
    unpack(bottom, &r1, &g1, &b1, &a1);
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1))
        return;
    for (py = y0; py < y1; py++) {
        float t = (py + 0.5f - y) / h;
        for (px = x0; px < x1; px++)
            blend(px, py, r0 + (r1 - r0) * t, g0 + (g1 - g0) * t, b0 + (b1 - b0) * t, a0 + (a1 - a0) * t);
    }
}

static float atlas_sample(float u, float v, float minu, float minv, float maxu, float maxv)
{
    int iu, iv;
    float fu, fv, s00, s10, s01, s11;
    u -= 0.5f;
    v -= 0.5f;
    if (u < minu) u = minu;
    if (v < minv) v = minv;
    if (u > maxu) u = maxu;
    if (v > maxv) v = maxv;
    iu = (int)u;
    iv = (int)v;
    fu = u - iu;
    fv = v - iv;
#define AT(X, Y) ((float)atlas_i8[(size_t)((Y) < atlas_h ? (Y) : atlas_h - 1) * atlas_w + ((X) < atlas_w ? (X) : atlas_w - 1)])
    s00 = AT(iu, iv);
    s10 = AT(iu + 1, iv);
    s01 = AT(iu, iv + 1);
    s11 = AT(iu + 1, iv + 1);
#undef AT
    return ((s00 * (1 - fu) + s10 * fu) * (1 - fv) + (s01 * (1 - fu) + s11 * fu) * fv) / 255.0f;
}

void dr_atlas(float x, float y, float w, float h, float ax, float ay, float aw, float ah, rgba_t c)
{
    int x0, y0, x1, y1, px, py;
    float r, g, b, a;
    const float minu = aw < 0 ? ax + aw : ax, maxu = (aw < 0 ? ax : ax + aw) - 1;
    const float minv = ah < 0 ? ay + ah : ay, maxv = (ah < 0 ? ay : ay + ah) - 1;
    if (w <= 0 || h <= 0)
        return;
    unpack(c, &r, &g, &b, &a);
    if (!span(x, y, w, h, &x0, &y0, &x1, &y1))
        return;
    for (py = y0; py < y1; py++) {
        float v = ay + (py + 0.5f - y) / h * ah;
        for (px = x0; px < x1; px++) {
            float u = ax + (px + 0.5f - x) / w * aw;
            blend(px, py, r, g, b, a * atlas_sample(u, v, minu, minv, maxu, maxv));
        }
    }
}

void dr_image(const wt_image *img, float x, float y, float w, float h, uint8_t alpha)
{
    dr_image_part(img, x, y, w, h, 0, 0, 1, 1, alpha);
}

void dr_image_part(const wt_image *img, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                   uint8_t alpha)
{
    int x0, y0, x1, y1, px, py;
    if (!img || w <= 0 || h <= 0 || !span(x, y, w, h, &x0, &y0, &x1, &y1))
        return;
    for (py = y0; py < y1; py++) {
        float v = (v0 + (py + 0.5f - y) / h * (v1 - v0)) * img->h - 0.5f;
        int iv = v < 0 ? 0 : (int)v;
        float fv = v < 0 ? 0 : v - iv;
        int iv1 = iv + 1 < img->h ? iv + 1 : img->h - 1;
        if (iv >= img->h)
            iv = iv1 = img->h - 1;
        for (px = x0; px < x1; px++) {
            float u = (u0 + (px + 0.5f - x) / w * (u1 - u0)) * img->w - 0.5f;
            int iu = u < 0 ? 0 : (int)u, iu1;
            float fu = u < 0 ? 0 : u - iu, out[4];
            uint8_t p00[4], p10[4], p01[4], p11[4];
            int k;
            iu1 = iu + 1 < img->w ? iu + 1 : img->w - 1;
            if (iu >= img->w)
                iu = iu1 = img->w - 1;
            image_pixel_rgba(img, iu, iv, p00);
            image_pixel_rgba(img, iu1, iv, p10);
            image_pixel_rgba(img, iu, iv1, p01);
            image_pixel_rgba(img, iu1, iv1, p11);
            for (k = 0; k < 4; k++)
                out[k] = (p00[k] * (1 - fu) + p10[k] * fu) * (1 - fv) + (p01[k] * (1 - fu) + p11[k] * fu) * fv;
            blend(px, py, out[0], out[1], out[2], out[3] / 255.0f * alpha / 255.0f);
        }
    }
}

/* Bilinear sample of an I8-tiled plane at texel coordinate (u, v), like the GPU's filter. */
static int plane_sample(const uint8_t *tex, int tw, int w, int h, float u, float v)
{
    int iu, iv, iu1, iv1;
    float fu, fv, s;
    u -= 0.5f;
    v -= 0.5f;
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    iu = (int)u;
    iv = (int)v;
    fu = u - iu;
    fv = v - iv;
    if (iu > w - 1) { iu = w - 1; fu = 0; }
    if (iv > h - 1) { iv = h - 1; fv = 0; }
    iu1 = iu + 1 < w ? iu + 1 : w - 1;
    iv1 = iv + 1 < h ? iv + 1 : h - 1;
    s = (conv_i8_texel(tex, tw, iu, iv) * (1 - fu) + conv_i8_texel(tex, tw, iu1, iv) * fu) * (1 - fv) +
        (conv_i8_texel(tex, tw, iu, iv1) * (1 - fu) + conv_i8_texel(tex, tw, iu1, iv1) * fu) * fv;
    return (int)(s + 0.5f);
}

void dr_video(const yuv_frame *f, float x, float y, float w, float h)
{
    yuv_tev_consts k;
    int x0, y0, x1, y1, px, py;
    if (!f || w <= 0 || h <= 0 || !span(x, y, w, h, &x0, &y0, &x1, &y1))
        return;
    yuv_tev_constants(f->bt709, &k);
    for (py = y0; py < y1; py++) {
        float fy = (py + 0.5f - y) / h;
        for (px = x0; px < x1; px++) {
            float fx = (px + 0.5f - x) / w;
            uint8_t rgb[3];
            int Y = plane_sample(f->y, f->yw, f->w, f->h, fx * f->w, fy * f->h);
            int U = plane_sample(f->u, f->cwt, f->cw, f->ch, fx * f->cw, fy * f->ch);
            int V = plane_sample(f->v, f->cwt, f->cw, f->ch, fx * f->cw, fy * f->ch);
            yuv_tev_pixel(&k, Y, U, V, rgb);
            blend(px, py, rgb[0], rgb[1], rgb[2], 1.0f);
        }
    }
}

void dr_clip(float x, float y, float w, float h)
{
    g_clip = w > 0 && h > 0;
    g_cx0 = x;
    g_cy0 = y;
    g_cx1 = x + w;
    g_cy1 = y + h;
}

int soft_save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", g_w, UI_H);
    fwrite(g_fb, 3, (size_t)g_w * UI_H, f);
    fclose(f);
    return 0;
}
