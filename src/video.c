/*
 * video.c - frame-buffer presentation backends (see video.h).
 *
 * The UI is composed into an ARGB8888 image in fb.c; each backend here only
 * presents that image. SDL backends work everywhere; the GDI and Direct3D 9
 * backends are native Windows and need no extra runtime files; Vulkan is used
 * when the build has the SDK.
 */
#include "video.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <SDL2/SDL_syswm.h>
#endif

#ifdef HAVE_VULKAN
extern void *vk_create(SDL_Window *win);
extern void  vk_present(void *state, const uint32_t *argb, int w, int h);
extern void  vk_destroy(void *state);
#endif

struct Video {
    VidBackend backend;
    SDL_Window *win;
    int w, h;

    SDL_Renderer *ren;
    SDL_Texture  *tex;

    void *vk;         /* Vulkan state */
    void *native;     /* GDI/D3D state */
};

static void get_drawable(SDL_Window *win, int *w, int *h) {
    *w = 0; *h = 0;
    SDL_GetWindowSize(win, w, h);
    if (*w <= 0) *w = 240;
    if (*h <= 0) *h = 160;
}

const char *vid_name(VidBackend b) {
    switch (b) {
    case VID_AUTO:     return "Auto";
    case VID_SOFTWARE: return "Software";
    case VID_OPENGL:   return "OpenGL";
    case VID_VULKAN:   return "Vulkan";
    case VID_GDI:      return "GDI";
    case VID_DIRECT3D: return "Direct3D";
    default:           return "?";
    }
}

int vid_available(VidBackend b) {
    switch (b) {
    case VID_AUTO: case VID_SOFTWARE: case VID_OPENGL:
        return 1;
#if defined(_WIN32)
    case VID_GDI: case VID_DIRECT3D:
        return 1;
#endif
#ifdef HAVE_VULKAN
    case VID_VULKAN:
        return 1;
#endif
    default:
        return 0;
    }
}

/* ============================ SDL-backed backends ========================= */

static int sdl_backend_init(Video *v, VidBackend b) {
    Uint32 flags = SDL_RENDERER_ACCELERATED;
    if (b == VID_SOFTWARE) {
        flags = SDL_RENDERER_SOFTWARE;
    } else if (b == VID_OPENGL) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");   /* force the GL driver */
    }
    v->ren = SDL_CreateRenderer(v->win, -1, flags);
    if (b == VID_OPENGL) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
    if (!v->ren && b != VID_SOFTWARE)
        v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    return v->ren ? 0 : -1;
}

static void sdl_present(Video *v, const uint32_t *argb, int w, int h) {
    if (!v->tex || v->w != w || v->h != h) {
        if (v->tex) SDL_DestroyTexture(v->tex);
        v->tex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
        v->w = w; v->h = h;
    }
    SDL_UpdateTexture(v->tex, NULL, argb, w * 4);
    SDL_SetRenderDrawColor(v->ren, 0, 0, 0, 255);
    SDL_RenderClear(v->ren);
    int dw, dh; SDL_GetRendererOutputSize(v->ren, &dw, &dh);
    SDL_Rect dst = { 0, 0, dw, dh };
    SDL_RenderCopy(v->ren, v->tex, NULL, &dst);
    SDL_RenderPresent(v->ren);
}

/* ================================ GDI ==================================== */

#if defined(_WIN32)
static HWND win_hwnd(SDL_Window *win) {
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(win, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
        return info.info.win.window;
    return NULL;
}

typedef struct { HWND hwnd; HDC hdc; BITMAPINFO bmi; } GdiState;

static int gdi_init(Video *v) {
    GdiState *g = (GdiState *)calloc(1, sizeof(GdiState));
    g->hwnd = win_hwnd(v->win);
    if (!g->hwnd) { free(g); return -1; }
    g->hdc = GetDC(g->hwnd);
    if (!g->hdc) { free(g); return -1; }
    memset(&g->bmi, 0, sizeof(g->bmi));
    g->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g->bmi.bmiHeader.biPlanes = 1;
    g->bmi.bmiHeader.biBitCount = 32;
    g->bmi.bmiHeader.biCompression = BI_RGB;
    v->native = g;
    return 0;
}
static void gdi_present(Video *v, const uint32_t *argb, int w, int h) {
    GdiState *g = (GdiState *)v->native;
    RECT rc; GetClientRect(g->hwnd, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    g->bmi.bmiHeader.biWidth = w;
    g->bmi.bmiHeader.biHeight = -h;   /* top-down */
    StretchDIBits(g->hdc, 0, 0, cw, ch, 0, 0, w, h, argb, &g->bmi, DIB_RGB_COLORS, SRCCOPY);
}
static void gdi_shutdown(Video *v) {
    GdiState *g = (GdiState *)v->native;
    if (g) { if (g->hdc) ReleaseDC(g->hwnd, g->hdc); free(g); }
}
#endif /* _WIN32 */

/* ============================= Direct3D 9 =============================== */

#if defined(_WIN32)
#  include <d3d9.h>

typedef struct {
    HWND               hwnd;
    IDirect3D9        *d3d;
    IDirect3DDevice9  *dev;
    IDirect3DTexture9 *tex;
    int                tw, th;
} D3DState;

typedef struct { float x, y, z, rhw; float u, v; } TLVert;

static int d3d_init(Video *v) {
    D3DState *d = (D3DState *)calloc(1, sizeof(D3DState));
    d->hwnd = win_hwnd(v->win);
    if (!d->hwnd) { free(d); return -1; }
    d->d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d->d3d) { free(d); return -1; }
    D3DPRESENT_PARAMETERS pp; memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = d->hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    if (FAILED(IDirect3D9_CreateDevice(d->d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            d->hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &d->dev))) {
        IDirect3D9_Release(d->d3d); free(d); return -1;
    }
    v->native = d;
    return 0;
}
static void d3d_present(Video *v, const uint32_t *argb, int w, int h) {
    D3DState *d = (D3DState *)v->native;
    if (!d->tex || d->tw != w || d->th != h) {
        if (d->tex) IDirect3DTexture9_Release(d->tex);
        if (FAILED(IDirect3DDevice9_CreateTexture(d->dev, w, h, 1, D3DUSAGE_DYNAMIC,
                D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d->tex, NULL))) return;
        d->tw = w; d->th = h;
    }
    D3DLOCKED_RECT lr;
    if (FAILED(IDirect3DTexture9_LockRect(d->tex, 0, &lr, NULL, D3DLOCK_DISCARD))) return;
    for (int y = 0; y < h; y++)
        memcpy((uint8_t *)lr.pBits + y * lr.Pitch, argb + y * w, (size_t)w * 4);
    IDirect3DTexture9_UnlockRect(d->tex, 0);

    RECT rc; GetClientRect(d->hwnd, &rc);
    float cw = (float)(rc.right - rc.left), ch = (float)(rc.bottom - rc.top);
    TLVert quad[4] = {
        { -0.5f,     -0.5f,     0, 1, 0, 0 },
        { cw - 0.5f, -0.5f,     0, 1, 1, 0 },
        { -0.5f,      ch - 0.5f, 0, 1, 0, 1 },
        { cw - 0.5f,  ch - 0.5f, 0, 1, 1, 1 },
    };
    IDirect3DDevice9_BeginScene(d->dev);
    IDirect3DDevice9_Clear(d->dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0,0,0), 1, 0);
    IDirect3DDevice9_SetFVF(d->dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
    IDirect3DDevice9_SetTexture(d->dev, 0, (IDirect3DBaseTexture9 *)d->tex);
    IDirect3DDevice9_SetTextureStageState(d->dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(d->dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_DrawPrimitiveUP(d->dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TLVert));
    IDirect3DDevice9_EndScene(d->dev);
    IDirect3DDevice9_Present(d->dev, NULL, NULL, NULL, NULL);
}
static void d3d_shutdown(Video *v) {
    D3DState *d = (D3DState *)v->native;
    if (d) {
        if (d->tex) IDirect3DTexture9_Release(d->tex);
        if (d->dev) IDirect3DDevice9_Release(d->dev);
        if (d->d3d) IDirect3D9_Release(d->d3d);
        free(d);
    }
}
#endif /* _WIN32 */

/* =============================== dispatch ================================ */

static int fallback_software(Video *v) {
    if (sdl_backend_init(v, VID_SOFTWARE) != 0) return -1;
    v->backend = VID_SOFTWARE;
    return 0;
}

Video *vid_init(SDL_Window *win, VidBackend backend) {
    Video *v = (Video *)calloc(1, sizeof(Video));
    if (!v) return NULL;
    v->win = win; v->backend = backend;
    get_drawable(win, &v->w, &v->h);

    switch (backend) {
    case VID_AUTO: case VID_SOFTWARE: case VID_OPENGL:
        if (sdl_backend_init(v, backend) != 0) { free(v); return NULL; }
        break;
#if defined(_WIN32)
    case VID_GDI:
        if (gdi_init(v) != 0 && fallback_software(v) != 0) { free(v); return NULL; }
        break;
    case VID_DIRECT3D:
        if (d3d_init(v) != 0 && fallback_software(v) != 0) { free(v); return NULL; }
        break;
#endif
#ifdef HAVE_VULKAN
    case VID_VULKAN:
        v->vk = vk_create(win);
        if (!v->vk && fallback_software(v) != 0) { free(v); return NULL; }
        v->backend = v->vk ? VID_VULKAN : VID_SOFTWARE;
        break;
#endif
    default:
        if (sdl_backend_init(v, VID_AUTO) != 0) { free(v); return NULL; }
        break;
    }
    return v;
}

void vid_present(Video *v, const uint32_t *argb, int w, int h) {
    if (!v) return;
    switch (v->backend) {
    case VID_AUTO: case VID_SOFTWARE: case VID_OPENGL:
        sdl_present(v, argb, w, h); break;
#if defined(_WIN32)
    case VID_GDI:      gdi_present(v, argb, w, h); break;
    case VID_DIRECT3D: d3d_present(v, argb, w, h); break;
#endif
#ifdef HAVE_VULKAN
    case VID_VULKAN:   vk_present(v->vk, argb, w, h); break;
#endif
    default:           sdl_present(v, argb, w, h); break;
    }
}

void vid_shutdown(Video *v) {
    if (!v) return;
    if (v->tex) SDL_DestroyTexture(v->tex);
    if (v->ren) SDL_DestroyRenderer(v->ren);
#if defined(_WIN32)
    if (v->backend == VID_GDI) gdi_shutdown(v);
    if (v->backend == VID_DIRECT3D) d3d_shutdown(v);
#endif
#ifdef HAVE_VULKAN
    if (v->backend == VID_VULKAN && v->vk) vk_destroy(v->vk);
#endif
    free(v);
}
