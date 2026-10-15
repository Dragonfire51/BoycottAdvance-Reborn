/*
 * video.h - frame-buffer presentation backends.
 *
 * "Frame Buffer" selects how the composed ARGB image reaches the window:
 *   - non-Windows : SDL Software / SDL Accelerated, OpenGL and, when the
 *                   build has it, a raw Vulkan swapchain
 *   - Windows     : native GDI blit or Direct3D 9 only (no SDL runtime)
 *
 * vid_init takes a void* "window": on Windows this is the HWND of the
 * native window; everywhere else it is the SDL_Window the frontend owns.
 */
#ifndef VIDEO_H
#define VIDEO_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VID_AUTO = 0,   /* default for the platform (GDI on Windows)      */
    VID_SOFTWARE,   /* SDL software renderer (non-Windows)            */
    VID_OPENGL,     /* SDL OpenGL renderer (non-Windows)              */
    VID_VULKAN,     /* native Vulkan (non-Windows, if built with it)  */
    VID_GDI,        /* native Windows GDI blit                        */
    VID_DIRECT3D,   /* native Windows Direct3D 9                      */
    VID_BACKEND_COUNT
} VidBackend;

const char *vid_name(VidBackend b);
int         vid_available(VidBackend b);   /* usable on this build/platform? */

typedef struct Video Video;

Video *vid_init(void *win, VidBackend backend);
void   vid_present(Video *v, const uint32_t *argb, int w, int h);
void   vid_shutdown(Video *v);

#ifdef __cplusplus
}
#endif
#endif
