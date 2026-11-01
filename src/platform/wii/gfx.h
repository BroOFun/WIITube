/* Wii display: GX implementation of ui/draw.h (everything, menus included, is drawn by the
 * GPU into a 640x480 framebuffer; on 16:9 TVs the UI's 854-wide canvas is squeezed into it
 * so the TV's stretch restores the proportions). */
#ifndef WIITUBE_GFX_H
#define WIITUBE_GFX_H

void gfx_init(void);
int gfx_widescreen(void);
/* Start a frame (clears texture caches so changed textures are re-read). */
void gfx_frame_begin(void);
/* Finish the frame and show it at the next vertical blank. */
void gfx_frame_end(void);

#endif
