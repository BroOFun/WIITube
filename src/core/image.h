/* Small RGB images (thumbnails) stored as GX-ready tiled RGB565 textures. */
#ifndef WIITUBE_IMAGE_H
#define WIITUBE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;       /* picture size */
    int tw, th;     /* texture size (multiples of 4) */
    uint16_t *tex;  /* tiled RGB565, 32-byte aligned */
    void *raw;
} wt_image;

/* Decode a JPEG; if it is wider than max_w, shrink it by an integer factor (box filter) so
 * it fits. Returns NULL if the data can't be decoded. */
wt_image *image_from_jpeg(const unsigned char *data, size_t len, int max_w);
/* Wrap RGB(A) pixels (alpha ignored) in the same format. */
wt_image *image_from_rgb(const unsigned char *pix, int w, int h, int channels, int max_w);
void image_free(wt_image *img);
void image_pixel(const wt_image *img, int x, int y, uint8_t rgb[3]);

/* Called after a texture is written, so the GPU sees it (Wii: DCFlushRange). */
void image_set_flush(void (*flush)(void *p, size_t len));

#endif
