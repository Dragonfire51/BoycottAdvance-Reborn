/*
 * video.c - frame-buffer presentation backends (see video.h).
 *
 * The UI is composed into an ARGB8888 image in fb.c; each backend here only
 * presents that image.
 *
 *  - non-Windows : the SDL Software/OpenGL renderer; and when built with
 *                  the Vulkan SDK, the raw Vulkan swapchain.
 *  - Windows     : native GDI blit or Direct3D 9. The window handle is the
 *                  HWND of the native window; no SDL headers are used.
 */
#include "video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <SDL2/SDL.h>
#endif

#ifdef HAVE_VULKAN
extern void *vk_create(void *win);
extern void  vk_present(void *state, const uint32_t *argb, int w, int h);
extern void  vk_destroy(void *state);
#endif

struct Video {
    VidBackend backend;
    void      *win;     /* ownership keeps living in the frontend */
    int w, h;

    void *sdl;    /* SDL renderer/state (non-Windows) */
    void *vk;     /* Vulkan state */
    void *native; /* GDI/D3D state */
};

#if !defined(_WIN32)
typedef struct { void *ren; void *tex; } SdlState;
static void get_drawable(void *win, int *w, int *h) {
    *w = 0; *h = 0;
    SDL_GetWindowSize((SDL_Window *)win, w, h);
    if (*w <= 0) *w = 240;
    if (*h <= 0) *h = 160;
}
#endif

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
#if defined(_WIN32)
    case VID_AUTO: case VID_GDI: case VID_DIRECT3D:
        return 1;
#else
    case VID_AUTO: case VID_SOFTWARE: case VID_OPENGL:
        return 1;
#ifdef HAVE_VULKAN
    case VID_VULKAN:
        return 1;
#endif
#endif
    default:
        return 0;
    }
}

#if defined(_WIN32)
/* Letterboxed 3:2 destination rect inside a cw x ch client area, centred.
 * If the client is at least the native size, snap to an integer scale so the
 * pixels stay square and crisp. */
static void fit_rect(int cw, int ch, int w, int h, int *dx, int *dy, int *dw, int *dh) {
    int sw = cw, sh = cw * h / w;
    if (sh > ch) { sh = ch; sw = ch * w / h; }
    if (cw >= w && ch >= h) {
        int k = cw / w; if (ch / h < k) k = ch / h;
        if (k >= 1) { sw = w * k; sh = h * k; }
    }
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    *dw = sw; *dh = sh;
    *dx = (cw - sw) / 2; *dy = (ch - sh) / 2;
}
#endif

/* ============================ SDL-backed backends ========================= */
/* Non-Windows only: SDL Software / OpenGL. */

#if !defined(_WIN32)
static int sdl_backend_init(Video *v, VidBackend b) {
    SdlState *s = (SdlState *)calloc(1, sizeof(SdlState));
    Uint32 flags = SDL_RENDERER_ACCELERATED;
    if (b == VID_SOFTWARE) {
        flags = SDL_RENDERER_SOFTWARE;
    } else if (b == VID_OPENGL) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");   /* force the GL driver */
    }
    s->ren = SDL_CreateRenderer((SDL_Window *)v->win, -1, flags);
    if (b == VID_OPENGL) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
    if (!s->ren && b != VID_SOFTWARE)
        s->ren = SDL_CreateRenderer((SDL_Window *)v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!s->ren) { free(s); return -1; }
    v->sdl = s;
    return 0;
}

static void sdl_present(Video *v, const uint32_t *argb, int w, int h) {
    SdlState *s = (SdlState *)v->sdl;
    if (!s->tex || v->w != w || v->h != h) {
        if (s->tex) SDL_DestroyTexture((SDL_Texture *)s->tex);
        s->tex = SDL_CreateTexture((SDL_Renderer *)s->ren, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
        v->w = w; v->h = h;
    }
    SDL_UpdateTexture((SDL_Texture *)s->tex, NULL, argb, w * 4);
    SDL_SetRenderDrawColor((SDL_Renderer *)s->ren, 0, 0, 0, 255);
    SDL_RenderClear((SDL_Renderer *)s->ren);
    int dw, dh; SDL_GetRendererOutputSize((SDL_Renderer *)s->ren, &dw, &dh);
    SDL_Rect dst = { 0, 0, dw, dh };
    SDL_RenderCopy((SDL_Renderer *)s->ren, (SDL_Texture *)s->tex, NULL, &dst);
    SDL_RenderPresent((SDL_Renderer *)s->ren);
}
#endif /* !_WIN32 */

/* ================================ GDI ==================================== */
/* Native Windows: the framebuffer is StretchDIBits'd into the window DC. */

#if defined(_WIN32)
typedef struct { HWND hwnd; HDC hdc; BITMAPINFO bmi; } GdiState;

static int gdi_init(Video *v) {
    GdiState *g = (GdiState *)calloc(1, sizeof(GdiState));
    g->hwnd = (HWND)v->win;   /* native frontend passes its HWND */
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
    if (cw <= 0 || ch <= 0) return;
    g->bmi.bmiHeader.biWidth = w;
    g->bmi.bmiHeader.biHeight = -h;   /* top-down */
    /* The frontend already composes the 3:2 picture centred inside the
       framebuffer, so normally w == cw and h == ch and this is a 1:1 blit.
       If the window was resized before the framebuffer was rebuilt, letterbox
       into a 3:2 rect instead of stretching the image across the client. */
    if (w == cw && h == ch) {
        StretchDIBits(g->hdc, 0, 0, cw, ch, 0, 0, w, h, argb, &g->bmi, DIB_RGB_COLORS, SRCCOPY);
        return;
    }
    PatBlt(g->hdc, 0, 0, cw, ch, BLACKNESS);
    int dx, dy, dw, dh;
    fit_rect(cw, ch, w, h, &dx, &dy, &dw, &dh);
    StretchDIBits(g->hdc, dx, dy, dw, dh, 0, 0, w, h, argb, &g->bmi, DIB_RGB_COLORS, SRCCOPY);
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
    D3DPRESENT_PARAMETERS pp;   /* kept so the device can be Reset on resize */
    int                cw, ch;  /* client size the back buffer was created for */
} D3DState;

typedef struct { float x, y, z, rhw; float u, v; } TLVert;

static int d3d_reset(D3DState *d, int cw, int ch) {
    if (d->tex) { IDirect3DTexture9_Release(d->tex); d->tex = NULL; d->tw = d->th = 0; }
    d->pp.BackBufferWidth  = cw;
    d->pp.BackBufferHeight = ch;
    if (FAILED(IDirect3DDevice9_Reset(d->dev, &d->pp))) return -1;
    d->cw = cw; d->ch = ch;
    return 0;
}

static int d3d_init(Video *v) {
    D3DState *d = (D3DState *)calloc(1, sizeof(D3DState));
    d->hwnd = (HWND)v->win;   /* native frontend passes its HWND */
    if (!d->hwnd) { free(d); return -1; }
    d->d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d->d3d) { free(d); return -1; }
    RECT rc; GetClientRect(d->hwnd, &rc);
    d->cw = rc.right - rc.left; if (d->cw <= 0) d->cw = 240;
    d->ch = rc.bottom - rc.top; if (d->ch <= 0) d->ch = 160;
    D3DPRESENT_PARAMETERS pp; memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth  = d->cw;
    pp.BackBufferHeight = d->ch;
    pp.hDeviceWindow = d->hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    if (FAILED(IDirect3D9_CreateDevice(d->d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            d->hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &d->dev))) {
        IDirect3D9_Release(d->d3d); free(d); return -1;
    }
    d->pp = pp;
    v->native = d;
    return 0;
}
static void d3d_present(Video *v, const uint32_t *argb, int w, int h) {
    D3DState *d = (D3DState *)v->native;
    if (!d->dev) return;

    RECT rc; GetClientRect(d->hwnd, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;

    /* Device lost / needs reset (minimised, resized, mode change, alt-tab). */
    HRESULT coop = IDirect3DDevice9_TestCooperativeLevel(d->dev);
    if (coop == D3DERR_DEVICELOST) return;
    if (coop == D3DERR_DEVICENOTRESET || cw != d->cw || ch != d->ch) {
        if (d3d_reset(d, cw, ch) != 0) return;
    }

    if (!d->tex || d->tw != w || d->th != h) {
        if (d->tex) { IDirect3DTexture9_Release(d->tex); d->tex = NULL; }
        if (FAILED(IDirect3DDevice9_CreateTexture(d->dev, w, h, 1, D3DUSAGE_DYNAMIC,
                D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d->tex, NULL))) return;
        d->tw = w; d->th = h;
    }
    D3DLOCKED_RECT lr;
    if (FAILED(IDirect3DTexture9_LockRect(d->tex, 0, &lr, NULL, D3DLOCK_DISCARD))) return;
    for (int y = 0; y < h; y++)
        memcpy((uint8_t *)lr.pBits + y * lr.Pitch, argb + y * w, (size_t)w * 4);
    IDirect3DTexture9_UnlockRect(d->tex, 0);

    int dx, dy, dw, dh;
    fit_rect(cw, ch, w, h, &dx, &dy, &dw, &dh);
    float x0 = (float)dx - 0.5f, y0 = (float)dy - 0.5f;
    float x1 = (float)(dx + dw) - 0.5f, y1 = (float)(dy + dh) - 0.5f;
    TLVert quad[4] = {
        { x0, y0, 0, 1, 0, 0 },
        { x1, y0, 0, 1, 1, 0 },
        { x0, y1, 0, 1, 0, 1 },
        { x1, y1, 0, 1, 1, 1 },
    };
    IDirect3DDevice9_BeginScene(d->dev);
    IDirect3DDevice9_Clear(d->dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0,0,0), 1, 0);
    IDirect3DDevice9_SetFVF(d->dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
    IDirect3DDevice9_SetTexture(d->dev, 0, (IDirect3DBaseTexture9 *)d->tex);
    IDirect3DDevice9_SetTextureStageState(d->dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(d->dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    /* Nearest-neighbour filtering keeps the GBA pixels sharp. */
    IDirect3DDevice9_SetSamplerState(d->dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(d->dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_DrawPrimitiveUP(d->dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TLVert));
    IDirect3DDevice9_EndScene(d->dev);
    HRESULT hr = IDirect3DDevice9_Present(d->dev, NULL, NULL, NULL, NULL);
    if (hr == D3DERR_DEVICELOST) {
        /* Next present will see D3DERR_DEVICENOTRESET and Reset. */
    }
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

Video *vid_init(void *win, VidBackend backend) {
    Video *v = (Video *)calloc(1, sizeof(Video));
    if (!v) return NULL;
    v->win = win; v->backend = backend;

#if defined(_WIN32)
    /* Native Windows: GDI blit by default, Direct3D 9 when asked. There is
     * no software fallback inside video.c on Windows: GDI is always there. */
    switch (backend) {
    case VID_GDI: case VID_AUTO:
        if (gdi_init(v) != 0) { free(v); return NULL; }
        v->backend = VID_GDI;
        break;
    case VID_DIRECT3D:
        if (d3d_init(v) != 0) {
            if (gdi_init(v) != 0) { free(v); return NULL; }
            v->backend = VID_GDI;
        }
        break;
    default:
        if (gdi_init(v) != 0) { free(v); return NULL; }
        v->backend = VID_GDI;
        break;
    }
#else
    get_drawable(win, &v->w, &v->h);
    switch (backend) {
    case VID_AUTO: case VID_SOFTWARE: case VID_OPENGL:
        if (sdl_backend_init(v, backend) != 0) { free(v); return NULL; }
        break;
#ifdef HAVE_VULKAN
    case VID_VULKAN:
        v->vk = vk_create(win);
        if (!v->vk && sdl_backend_init(v, VID_SOFTWARE) != 0) { free(v); return NULL; }
        v->backend = v->vk ? VID_VULKAN : VID_SOFTWARE;
        break;
#endif
    default:
        if (sdl_backend_init(v, VID_AUTO) != 0) { free(v); return NULL; }
        break;
    }
#endif
    return v;
}

void vid_present(Video *v, const uint32_t *argb, int w, int h) {
    if (!v) return;
    switch (v->backend) {
#if defined(_WIN32)
    case VID_AUTO: case VID_GDI:
        gdi_present(v, argb, w, h); break;
    case VID_DIRECT3D:
        d3d_present(v, argb, w, h); break;
#else
    case VID_AUTO: case VID_SOFTWARE: case VID_OPENGL:
        sdl_present(v, argb, w, h); break;
#ifdef HAVE_VULKAN
    case VID_VULKAN:
        vk_present(v->vk, argb, w, h); break;
#endif
#endif
    default:
        break;
    }
}

void vid_shutdown(Video *v) {
    if (!v) return;
#if !defined(_WIN32)
    if (v->backend != VID_VULKAN) {
        SdlState *s = (SdlState *)v->sdl;
        if (s) {
            if (s->tex) SDL_DestroyTexture((SDL_Texture *)s->tex);
            if (s->ren) SDL_DestroyRenderer((SDL_Renderer *)s->ren);
            free(s);
        }
    }
#elif defined(_WIN32)
    if (v->backend == VID_GDI) gdi_shutdown(v);
    if (v->backend == VID_DIRECT3D) d3d_shutdown(v);
#endif
#ifdef HAVE_VULKAN
    if (v->backend == VID_VULKAN && v->vk) vk_destroy(v->vk);
#endif
    free(v);
}
