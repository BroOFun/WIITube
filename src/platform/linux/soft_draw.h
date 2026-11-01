/* Software UI renderer for desktop previews. */
#ifndef WIITUBE_SOFT_DRAW_H
#define WIITUBE_SOFT_DRAW_H

/* Start a new frame: width is 640 (4:3 TV) or 854 (16:9 TV); height is always 480. */
void soft_begin(int width);
int soft_save_ppm(const char *path);

#endif
