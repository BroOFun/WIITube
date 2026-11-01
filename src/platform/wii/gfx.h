/* Wii display: a text console for menus, and GX for video playback. */
#ifndef WIITUBE_GFX_H
#define WIITUBE_GFX_H

#include <stdint.h>

void gfx_init(void);
int gfx_widescreen(void);
int gfx_console_cols(void);
int gfx_console_rows(void);

/* Show the text console (menus). printf() draws on it. */
void gfx_show_console(void);
int gfx_screen_width(void);
/* Pixel origin of the console text area (row 1, column 1). */
int gfx_console_x(void);
int gfx_console_y(void);
/* Draw an RGBA image onto the menu screen at framebuffer pixel (x, y), blended over black.
 * On 16:9 TVs it is narrowed by 3/4 so it keeps its shape. Returns the drawn width. */
int gfx_console_image(const unsigned char *rgba, int w, int h, int x, int y);

/* Video mode. frame_w/h is the picture size, tex_w/h the texture size (multiples of 4). */
void gfx_video_begin(int frame_w, int frame_h, int tex_w, int tex_h);
/* Use this tiled RGB565 texture from now on (already flushed from the CPU cache). */
void gfx_video_set_texture(const uint16_t *tex);

typedef struct {
    int visible;     /* draw the progress bar */
    int paused;      /* draw a pause symbol */
    float progress;  /* 0..1 played */
    float buffered;  /* 0..1 downloaded */
} gfx_overlay;

/* Draw the current frame (+ overlay), then show it at the next vertical blank. */
void gfx_video_present(const gfx_overlay *ov);
void gfx_video_end(void);

#endif
