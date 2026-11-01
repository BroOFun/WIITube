/* Drawing primitives for the UI. Implemented by the Wii GX backend (src/platform/wii/
 * gx_draw.c) and by a software rasterizer for desktop previews (src/platform/linux/
 * soft_draw.c). Coordinates are in a virtual canvas of ui_width() x 480 pixels, where
 * ui_width() is 854 on 16:9 TVs and 640 on 4:3, so shapes keep their proportions. */
#ifndef WIITUBE_DRAW_H
#define WIITUBE_DRAW_H

#include <stdint.h>
#include "../core/convert.h"
#include "../core/image.h"

typedef uint32_t rgba_t; /* 0xRRGGBBAA */

#define UI_H 480
int ui_width(void);

/* Solid rectangle. */
void dr_fill(float x, float y, float w, float h, rgba_t c);
/* Vertical gradient. */
void dr_vgradient(float x, float y, float w, float h, rgba_t top, rgba_t bottom);
/* Atlas region (ax, ay, aw, ah) tinted with c, stretched to x, y, w, h. A negative aw/ah
 * mirrors the region (used for the four corners of rounded shapes). */
void dr_atlas(float x, float y, float w, float h, float ax, float ay, float aw, float ah, rgba_t c);
/* Image (thumbnail, video frame, logo). Only the alpha of tint is used. */
void dr_image(const wt_image *img, float x, float y, float w, float h, uint8_t alpha);
/* Video frame: planar YUV textures, converted to RGB by the GPU (see core/yuv_tev.h). */
void dr_video(const yuv_frame *f, float x, float y, float w, float h);
/* Restrict drawing to a rectangle (w <= 0 turns clipping off). */
void dr_clip(float x, float y, float w, float h);

#endif
