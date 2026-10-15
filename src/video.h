/*
 * video.h - frame-buffer presentation backends.
 *
 * "Frame Buffer" selects how the composed ARGB image reaches the window:
 *   - SDL Software / SDL Accelerated : no extra dependency, all platforms
 *   - OpenGL                         : SDL GL context, all platforms
 *   - Vulkan                         : raw Vulkan swapchain (if built with it)
 *   - GDI / Direct3D 9               : native Windows, no extra runtime files
 */
#ifndef VIDEO_H
#define VIDEO_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Window;

typedef enum {
    VID_AUTO = 0,   /* SDL accelerated, falling back to software */
    VID_SOFTWARE,   /* SDL software renderer                        */
    VID_OPENGL,     /* SDL OpenGL renderer                          */
    VID_VULKAN,     /* native Vulkan                                */
    VID_GDI,        /* native Windows GDI blit                      */
    VID_DIRECT3D,   /* native Windows Direct3D 9                    */
    VID_BACKEND_COUNT
} VidBackend;

const char *vid_name(VidBackend b);
int         vid_available(VidBackend b);   /* usable on this build/platform? */

typedef struct Video Video;

Video *vid_init(struct SDL_Window *win, VidBackend backend);
void   vid_present(Video *v, const uint32_t *argb, int w, int h);
void   vid_shutdown(Video *v);

#ifdef __cplusplus
}
#endif
#endif
