/*
 * fb.h - software framebuffer compositor.
 *
 * The whole UI (scaled emulator image, menu bar, menus, dialogs, FPS
 * overlay) is composed here into a single ARGB8888 buffer. Presentation
 * backends in video.c only have to blit this one image, which keeps the
 * native backends (GDI/Direct3D/Vulkan) independent from the UI code.
 */
#ifndef FB_H
#define FB_H
#include <stdint.h>

typedef struct {
    int       w, h;
    uint32_t *px;      /* 0xAARRGGBB, w*h */
} FB;

void fb_init(FB *fb, int w, int h);
void fb_free(FB *fb);
void fb_fill(FB *fb, uint32_t argb);
void fb_rect(FB *fb, int x, int y, int w, int h, uint32_t argb);      /* filled */
void fb_rect_outline(FB *fb, int x, int y, int w, int h, uint32_t argb);
void fb_hline(FB *fb, int x, int y, int w, uint32_t argb);
void fb_text(FB *fb, int x, int y, const char *s, uint32_t argb);
/* Nearest-neighbour scale of a GBA RGB555 frame into the target rect. */
void fb_blit_frame(FB *fb, int dx, int dy, int dw, int dh,
                   const uint16_t *frame555, int scanline);

#endif
