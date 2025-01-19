/*
 * QEMU MESA GL Pass-Through
 *
 *  Copyright (c) 2020
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library;
 * if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "ui/console.h"

#include "mesagl_impl.h"

#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "glcntx: " fmt "\n" , ## __VA_ARGS__); } while(0)
#define DPRINTF_COND(cond,fmt, ...) \
    if (cond) { fprintf(stderr, "glcntx: " fmt "\n" , ## __VA_ARGS__); }

#if defined(CONFIG_LINUX) || defined(CONFIG_DARWIN)
#define MESAGL_SDLGL 1
#ifdef CONFIG_DARWIN
const char dllname[] = "/System/Library/Frameworks/OpenGL.framework/Libraries/libGL.dylib";
int MGLUpdateGuestBufo(mapbufo_t *bufo, int add) { return 0; }
#define GL_CONTEXTALPHA GetDispTimerMS()
#define GL_RENDER_TEXTURE_STR
#define GL_RENDER_TEXTURE_VAR
#define GL_PBUFFER_CONTEXT(x) { /* Pbuffer unsupported */ }
#define GL_TEXIMAGE_BIND(x) \
    (void)PbufferGLBinding(GL_NONE); \
    (void)PbufferGLAttrib(GL_NONE); \
    (void)x
#define GL_PBUFFER_CREATE(x) \
    DPRINTF("Unsupported %s", "wglCreatePbufferARB"); argsp[0] = 0
#define GL_PBUFFER_DESTROY(x) \
    DPRINTF("Unsupported %s", "wglDestroyPbufferARB"); argsp[0] = 0
#define GL_DELETECONTEXT(x)
#define GL_CONTEXTATTRIB(x)
#define GL_CREATECONTEXT(x)
#endif
#ifdef CONFIG_LINUX
#include <GL/glx.h>
#include "system/kvm.h"

int MGLUpdateGuestBufo(mapbufo_t *bufo, int add)
{
    int ret = GetBufOAccelEN()? kvm_enabled():0;

    if (ret && bufo) {
        bufo->lvl = (add)? MapBufObjGpa(bufo):0;
        kvm_update_guest_pa_range(MBUFO_BASE | (bufo->gpa & ((MBUFO_SIZE - 1) - (qemu_real_host_page_size() - 1))),
            bufo->mapsz + (bufo->hva & (qemu_real_host_page_size() - 1)),
            (void *)(bufo->hva & qemu_real_host_page_mask()),
            (bufo->acc & GL_MAP_WRITE_BIT)? 0:1, add);
    }

    return ret;
}
#define GL_CONTEXTALPHA 1
#define GL_RENDER_TEXTURE_STR \
    "WGL_ARB_pbuffer WGL_ARB_render_texture WGL_NV_render_texture_rectangle "
#define GL_RENDER_TEXTURE_VAR \
    static Display *dpy; \
    static GLXPbuffer PBDC[MAX_PBUFFER]; \
    static GLXContext PBRC[MAX_PBUFFER];
#define GL_PBUFFER_CONTEXT(x) \
    do { \
        SDL_GL_MakeCurrent(window, NULL); \
        if (dpy) glXMakeContextCurrent(dpy, PBDC[x], PBDC[x], PBRC[x]); \
    } while(0)
#define GL_TEXIMAGE_BIND(x) \
    if (PbufferGLBinding(hPbuffer[x].target) && PbufferGLAttrib(hPbuffer[x].format)) { \
        int prev_binded_texture = 0; \
        GLXContext prev_context = glXGetCurrentContext(); \
        GLXDrawable prev_drawable = glXGetCurrentDrawable(); \
        glGetIntegerv(PbufferGLBinding(hPbuffer[x].target), &prev_binded_texture); \
        glXMakeCurrent(dpy, PBDC[x], PBRC[x]); \
        glBindTexture(PbufferGLAttrib(hPbuffer[x].target), prev_binded_texture); \
        glCopyTexImage2D(PbufferGLAttrib(hPbuffer[x].target), hPbuffer[x].level, \
            PbufferGLAttrib(hPbuffer[x].format), 0, 0, hPbuffer[x].width, hPbuffer[x].height, 0); \
        glXMakeCurrent(dpy, prev_drawable, prev_context); \
    }
#define GL_PBUFFER_CREATE(x) \
    const int ia[] = { \
        GLX_X_RENDERABLE    , True, \
        GLX_DRAWABLE_TYPE   , GLX_PBUFFER_BIT, \
        GLX_RENDER_TYPE     , GLX_RGBA_BIT, \
        GLX_DOUBLEBUFFER    , False, \
        GLX_BUFFER_SIZE     , 32, \
        GLX_ALPHA_SIZE      , cAlphaBits, \
        GLX_DEPTH_SIZE      , cDepthBits, \
        None, \
    };\
    int pbcnt, pa[] = { \
        GLX_PBUFFER_WIDTH, hPbuffer[x].width, \
        GLX_PBUFFER_HEIGHT, hPbuffer[x].height, \
        None, \
    }; \
    if (!dpy) dpy = glXGetCurrentDisplay(); \
    GLXFBConfig *pbcnf = glXChooseFBConfig(dpy, DefaultScreen(dpy), ia, &pbcnt); \
    PBDC[x] = glXCreatePbuffer(dpy, pbcnf[0], pa); \
    PBRC[x] = glXCreateNewContext(dpy, pbcnf[0], GLX_RGBA_TYPE, glXGetCurrentContext(), true); \
    XFree(pbcnf); \
    argsp[0] = 1
#define GL_PBUFFER_DESTROY(x) \
    glXDestroyContext(dpy, PBRC[x]);\
    glXDestroyPbuffer(dpy, PBDC[x]);\
    PBRC[x] = 0; PBDC[x] = 0; \
    argsp[0] = 1
#define GL_DELETECONTEXT(x) \
    do { SDL_GL_DeleteContext(x); x = 0; } while(0)
#define GL_CONTEXTATTRIB(x) \
    MGLActivateHandler(0, 0); \
    do { \
        int major, minor, pfmsk, flags; \
        major = LookupAttribArray((const int *)&argsp[2], WGL_CONTEXT_MAJOR_VERSION_ARB); \
        minor = LookupAttribArray((const int *)&argsp[2], WGL_CONTEXT_MINOR_VERSION_ARB); \
        pfmsk = LookupAttribArray((const int *)&argsp[2], WGL_CONTEXT_PROFILE_MASK_ARB); \
        flags = LookupAttribArray((const int *)&argsp[2], WGL_CONTEXT_FLAGS_ARB); \
        if (major) { \
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major); \
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor); \
        } \
        if (pfmsk) \
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, pfmsk); \
        if (flags) \
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, flags); \
    } while(0)
#define GL_CREATECONTEXT(x) \
    do { x = SDL_GL_CreateContext(window); } while(0)
#endif
#else
#define MESAGL_SDLGL 0
#endif

#if MESAGL_SDLGL
#define GL_GLEXT_LEGACY
#include "SDL2/SDL_opengl.h"
#include "SDL2/SDL.h"

typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint8_t BYTE;
typedef struct tagPIXELFORMATDESCRIPTOR {
  WORD  nSize;
  WORD  nVersion;
  DWORD dwFlags;
  BYTE  iPixelType;
  BYTE  cColorBits;
  BYTE  cRedBits;
  BYTE  cRedShift;
  BYTE  cGreenBits;
  BYTE  cGreenShift;
  BYTE  cBlueBits;
  BYTE  cBlueShift;
  BYTE  cAlphaBits;
  BYTE  cAlphaShift;
  BYTE  cAccumBits;
  BYTE  cAccumRedBits;
  BYTE  cAccumGreenBits;
  BYTE  cAccumBlueBits;
  BYTE  cAccumAlphaBits;
  BYTE  cDepthBits;
  BYTE  cStencilBits;
  BYTE  cAuxBuffers;
  BYTE  iLayerType;
  BYTE  bReserved;
  DWORD dwLayerMask;
  DWORD dwVisibleMask;
  DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR, *PPIXELFORMATDESCRIPTOR, *LPPIXELFORMATDESCRIPTOR;

#define WGL_NUMBER_PIXEL_FORMATS_ARB            0x2000
#define WGL_DRAW_TO_WINDOW_ARB                  0x2001
#define WGL_DRAW_TO_BITMAP_ARB                  0x2002
#define WGL_ACCELERATION_ARB                    0x2003
#define WGL_NEED_PALETTE_ARB                    0x2004
#define WGL_NEED_SYSTEM_PALETTE_ARB             0x2005
#define WGL_SWAP_LAYER_BUFFERS_ARB              0x2006
#define WGL_SWAP_METHOD_ARB                     0x2007
#define WGL_NUMBER_OVERLAYS_ARB                 0x2008
#define WGL_NUMBER_UNDERLAYS_ARB                0x2009
#define WGL_TRANSPARENT_ARB                     0x200A
#define WGL_TRANSPARENT_RED_VALUE_ARB           0x2037
#define WGL_TRANSPARENT_GREEN_VALUE_ARB         0x2038
#define WGL_TRANSPARENT_BLUE_VALUE_ARB          0x2039
#define WGL_TRANSPARENT_ALPHA_VALUE_ARB         0x203A
#define WGL_TRANSPARENT_INDEX_VALUE_ARB         0x203B
#define WGL_SHARE_DEPTH_ARB                     0x200C
#define WGL_SHARE_STENCIL_ARB                   0x200D
#define WGL_SHARE_ACCUM_ARB                     0x200E
#define WGL_SUPPORT_GDI_ARB                     0x200F
#define WGL_SUPPORT_OPENGL_ARB                  0x2010
#define WGL_DOUBLE_BUFFER_ARB                   0x2011
#define WGL_STEREO_ARB                          0x2012
#define WGL_PIXEL_TYPE_ARB                      0x2013
#define WGL_COLOR_BITS_ARB                      0x2014
#define WGL_RED_BITS_ARB                        0x2015
#define WGL_RED_SHIFT_ARB                       0x2016
#define WGL_GREEN_BITS_ARB                      0x2017
#define WGL_GREEN_SHIFT_ARB                     0x2018
#define WGL_BLUE_BITS_ARB                       0x2019
#define WGL_BLUE_SHIFT_ARB                      0x201A
#define WGL_ALPHA_BITS_ARB                      0x201B
#define WGL_ALPHA_SHIFT_ARB                     0x201C
#define WGL_ACCUM_BITS_ARB                      0x201D
#define WGL_ACCUM_RED_BITS_ARB                  0x201E
#define WGL_ACCUM_GREEN_BITS_ARB                0x201F
#define WGL_ACCUM_BLUE_BITS_ARB                 0x2020
#define WGL_ACCUM_ALPHA_BITS_ARB                0x2021
#define WGL_DEPTH_BITS_ARB                      0x2022
#define WGL_STENCIL_BITS_ARB                    0x2023
#define WGL_AUX_BUFFERS_ARB                     0x2024
#define WGL_NO_ACCELERATION_ARB                 0x2025
#define WGL_GENERIC_ACCELERATION_ARB            0x2026
#define WGL_FULL_ACCELERATION_ARB               0x2027
#define WGL_SWAP_EXCHANGE_ARB                   0x2028
#define WGL_SWAP_COPY_ARB                       0x2029
#define WGL_SWAP_UNDEFINED_ARB                  0x202A
#define WGL_TYPE_RGBA_ARB                       0x202B
#define WGL_TYPE_COLORINDEX_ARB                 0x202C
#define WGL_SAMPLE_BUFFERS_ARB                  0x2041
#define WGL_SAMPLES_ARB                         0x2042
/*
 * WGL_ARB_create_context
 * WGL_ARB_create_context_profile
 */
#define WGL_CONTEXT_MAJOR_VERSION_ARB           0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB           0x2092
#define WGL_CONTEXT_FLAGS_ARB                   0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB            0x9126
/*
 * WGL_ARB_render_texture
 * WGL_NV_render_texture_rectangle
*/
#define WGL_TEXTURE_FORMAT_ARB                  0x2072
#define WGL_TEXTURE_RGB_ARB                     0x2075
#define WGL_TEXTURE_RGBA_ARB                    0x2076
#define WGL_TEXTURE_TARGET_ARB                  0x2073
#define WGL_TEXTURE_2D_ARB                      0x207A
#define WGL_TEXTURE_RECTANGLE_NV                0x20A2
#define WGL_MIPMAP_LEVEL_ARB                    0x207B

typedef struct tagFakePBuffer {
    int width;
    int height;
    int target, format, level;
} HPBUFFERARB;

static const PIXELFORMATDESCRIPTOR pfd = {
    .nSize = sizeof(PIXELFORMATDESCRIPTOR),
    .nVersion = 1,
    .dwFlags = 0x225,
    .cColorBits = 32,
    .cRedBits = 8, .cGreenBits = 8, .cBlueBits = 8, .cAlphaBits = 8,
    .cRedShift = 16, .cGreenShift = 8, .cBlueShift = 0, .cAlphaShift = 24,
    .cDepthBits = 24,
    .cStencilBits = 8,
    .cAuxBuffers = 0,
};
static const int iAttribs[] = {
    WGL_NUMBER_PIXEL_FORMATS_ARB, 1,
    WGL_DRAW_TO_WINDOW_ARB, 1,
    WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
    WGL_SWAP_METHOD_ARB, WGL_SWAP_EXCHANGE_ARB,
    WGL_SUPPORT_OPENGL_ARB, 1,
    WGL_DOUBLE_BUFFER_ARB, 1,
    WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
    WGL_COLOR_BITS_ARB, 32,
    WGL_RED_BITS_ARB, 8,
    WGL_RED_SHIFT_ARB, 16,
    WGL_GREEN_BITS_ARB, 8,
    WGL_GREEN_SHIFT_ARB, 8,
    WGL_BLUE_BITS_ARB, 8,
    WGL_BLUE_SHIFT_ARB, 0,
    WGL_ALPHA_BITS_ARB, 8,
    WGL_ALPHA_SHIFT_ARB, 24,
    WGL_DEPTH_BITS_ARB, 24,
    WGL_STENCIL_BITS_ARB, 8,
    WGL_AUX_BUFFERS_ARB, 0,
    WGL_SAMPLE_BUFFERS_ARB, 0,
    WGL_SAMPLES_ARB, 0,
    0,0
};

static SDL_Window *window;
static SDL_GLContext ctx[MAX_LVLCNTX];
GL_RENDER_TEXTURE_VAR;

static HPBUFFERARB hPbuffer[MAX_PBUFFER];
static int wnd_ready;
static int cAlphaBits, cDepthBits, cStencilBits;
static int cAuxBuffers, cSampleBuf[2];

int glwnd_ready(void) { return qatomic_read(&wnd_ready); }

int MGLExtIsAvail(const char *xstr, const char *str)
{ return find_xstr(xstr, str); }

struct GammaRamp {
    uint16_t r[256];
    uint16_t g[256];
    uint16_t b[256];
};

static void MesaInitGammaRamp(void)
{
    struct GammaRamp ramp;

    for (int i = 0; i < 256; i++) {
        ramp.r[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
        ramp.g[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
        ramp.b[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
    }
    SDL_SetWindowGammaRamp(window, ramp.r, ramp.g, ramp.b);
}

static void cwnd_mesagl(void *swnd, void *nwnd, void *opaque)
{
    window = (SDL_Window *)swnd;
#ifdef CONFIG_DARWIN
    ctx[0] = SDL_GL_GetCurrentContext();
#endif
    qatomic_set(&wnd_ready, 1);
    DPRINTF("MESAGL window [SDL2 %p] ready", swnd);
}

void SetMesaFuncPtr(void *p)
{
}

void *MesaGLGetProc(const char *proc)
{
    return SDL_GL_GetProcAddress(proc);
}

void MGLTmpContext(void)
{
}

void MGLDeleteContext(int level)
{
    int n = (level)? ((level % MAX_LVLCNTX)? (level % MAX_LVLCNTX):1):level;
    SDL_GL_MakeCurrent(window, NULL);
    if (n == 0) {
        for (int i = MAX_LVLCNTX; i > 1;) {
            if (ctx[--i]) {
                GL_DELETECONTEXT(ctx[i]);
            }
        }
        MesaBlitFree();
    }
    GL_DELETECONTEXT(ctx[n]);
    if (!n)
        MGLActivateHandler(0, 0);
}

void MGLWndRelease(void)
{
    if (window) {
        MesaInitGammaRamp();
        mesa_release_window();
        window = 0;
    }
}

int MGLCreateContext(uint32_t gDC)
{
    int i, ret;
    i = gDC & (MAX_PBUFFER - 1);
    if (gDC == ((MESAGL_HPBDC & 0xFFFFFFF0U) | i)) {
        ret = 0;
    }
    else {
        SDL_GL_MakeCurrent(window, NULL);
        for (i = MAX_LVLCNTX; i > 1;) {
            if (ctx[--i]) {
                GL_DELETECONTEXT(ctx[i]);
            }
        }
        GL_CREATECONTEXT(ctx[0]);
        ret = (ctx[0])? 0:1;
    }
    return ret;
}

int MGLMakeCurrent(uint32_t cntxRC, int level)
{
    int n = (level)? ((level % MAX_LVLCNTX)? (level % MAX_LVLCNTX):1):level;
    uint32_t i = cntxRC & (MAX_PBUFFER - 1);
    if (cntxRC == (MESAGL_MAGIC - n)) {
        SDL_GL_MakeCurrent(window, ctx[n]);
        InitMesaGLExt();
        wrContextSRGB(ContextUseSRGB());
        if (ContextVsyncOff()) {
            const int val = 0;
            SDL_GL_SetSwapInterval(val);
        }
        if (!n)
            MGLActivateHandler(1, 0);
    }
    if (cntxRC == (((MESAGL_MAGIC & 0xFFFFFFFU) << 4) | i))
        GL_PBUFFER_CONTEXT(i);

    return 0;
}

int MGLSwapBuffers(void)
{
    MGLActivateHandler(1, 0);
    MesaBlitScale();
    SDL_GL_SwapWindow(window);
    return 1;
}

static int MGLPresetPixelFormat(void)
{
    qatomic_set(&wnd_ready, 0);
    ImplMesaGLReset();
    mesa_prepare_window(GetContextMSAA(), GL_CONTEXTALPHA, 0, &cwnd_mesagl);

    MesaInitGammaRamp();
    return 1;
}

int MGLChoosePixelFormat(void)
{
    DPRINTF("ChoosePixelFormat()");
    if (!window)
        MGLPresetPixelFormat();
    return 1;
}

int MGLSetPixelFormat(int fmt, const void *p)
{
    DPRINTF("SetPixelFormat()");
    if (!window)
        MGLPresetPixelFormat();
    else {
        ctx[0] = (ctx[0])? ctx[0]:SDL_GL_GetCurrentContext();
        ctx[0] = (ctx[0])? ctx[0]:SDL_GL_CreateContext(window);
        if (ctx[0]) {
            int cColors[3];
            SDL_GL_MakeCurrent(window, ctx[0]);
            SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &cAlphaBits);
            SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &cColors[0]);
            SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &cColors[1]);
            SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &cColors[2]);
            SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &cDepthBits);
            SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &cStencilBits);
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &cSampleBuf[0]);
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &cSampleBuf[1]);
            glGetIntegerv(GL_AUX_BUFFERS, &cAuxBuffers);
            DPRINTF("%s OpenGL %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
            DPRINTF("Pixel Format ABGR%d%d%d%d D%2dS%d nAux %d nSamples %d %d %s",
                    cAlphaBits, cColors[0], cColors[1], cColors[2], cDepthBits, cStencilBits,
                    cAuxBuffers, cSampleBuf[0], cSampleBuf[1], ContextUseSRGB()? "sRGB":"");
        }
    }
    return (ctx[0])? 1:0;
}

int MGLDescribePixelFormat(int fmt, unsigned int sz, void *p)
{
    //DPRINTF("DescribePixelFormat()");
    cDepthBits = pfd.cDepthBits;
    cStencilBits = pfd.cStencilBits;
    cAuxBuffers = pfd.cAuxBuffers;
    if (!window)
        MGLPresetPixelFormat();
    else {
        ctx[0] = (ctx[0])? ctx[0]:SDL_GL_GetCurrentContext();
        ctx[0] = (ctx[0])? ctx[0]:SDL_GL_CreateContext(window);
        if (ctx[0]) {
            SDL_GL_MakeCurrent(window, ctx[0]);
            SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &cDepthBits);
            SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &cStencilBits);
            glGetIntegerv(GL_AUX_BUFFERS, &cAuxBuffers);
        }
    }
    memcpy(p, &pfd, sizeof(PIXELFORMATDESCRIPTOR));
    ((PIXELFORMATDESCRIPTOR *)p)->cDepthBits = cDepthBits;
    ((PIXELFORMATDESCRIPTOR *)p)->cStencilBits = cStencilBits;
    ((PIXELFORMATDESCRIPTOR *)p)->cAuxBuffers = cAuxBuffers;
    return 1;
}

int NumPbuffer(void)
{
    int i, c;
    for (i = 0, c = 0; i < MAX_PBUFFER;)
        if (hPbuffer[i++].width) c++;
    return c;
}

int DrawableContext(void)
{
    return SDL_GL_GetCurrentContext()? 1:0;
}

static int PbufferGLBinding(const int target)
{
    int ret;
    switch (target) {
        case WGL_TEXTURE_2D_ARB:
            ret = GL_TEXTURE_BINDING_2D;
            break;
        case WGL_TEXTURE_RECTANGLE_NV:
            ret = GL_TEXTURE_BINDING_RECTANGLE_NV;
            break;
        default:
            return 0;
    }
    return ret;
}
static int PbufferGLAttrib(const int attr)
{
    int ret;
    switch (attr) {
        case WGL_TEXTURE_2D_ARB:
            ret = GL_TEXTURE_2D;
            break;
        case WGL_TEXTURE_RECTANGLE_NV:
            ret = GL_TEXTURE_RECTANGLE_NV;
            break;
        case WGL_TEXTURE_RGB_ARB:
            ret = GL_RGB;
            break;
        case WGL_TEXTURE_RGBA_ARB:
            ret = GL_RGBA;
            break;
        default:
            return 0;
    }
    return ret;
}
static int LookupAttribArray(const int *attrib, const int attr)
{
    int ret = 0;
    for (int i = 0; attrib[i]; i+=2) {
        if (attrib[i] == attr) {
            switch (attr) {
                case WGL_DEPTH_BITS_ARB:
                    ret = cDepthBits;
                    break;
                case WGL_STENCIL_BITS_ARB:
                    ret = cStencilBits;
                    break;
                case WGL_AUX_BUFFERS_ARB:
                    ret = cAuxBuffers;
                    break;
                case WGL_SAMPLE_BUFFERS_ARB:
                    ret = cSampleBuf[0];
                    break;
                case WGL_SAMPLES_ARB:
                    ret = cSampleBuf[1];
                    break;
                default:
                    ret = attrib[i+1];
                    break;
            }
            break;
        }
    }
    return ret;
}

void MGLFuncHandler(const char *name)
{
    char fname[64];
    uint32_t *argsp = (uint32_t *)(name + ALIGNED((strnlen(name, sizeof(fname))+1)));
    strncpy(fname, name, sizeof(fname)-1);

#define FUNCP_HANDLER(a) \
    if (!memcmp(fname, a, sizeof(a)))

    FUNCP_HANDLER("wglShareLists") {
        uint32_t i, ret = 0;
        i = argsp[1] & (MAX_PBUFFER - 1);
        if ((argsp[0] == MESAGL_MAGIC) && (argsp[1] == ((MESAGL_MAGIC & 0xFFFFFFFU) << 4 | i)))
            ret = 1;
        else {
            DPRINTF("  *WARN* ShareLists called with unknown contexts, %x %x", argsp[0], argsp[1]);
        }
        argsp[0] = ret;
        return;
    }
    FUNCP_HANDLER("wglUseFontBitmapsA") {
        fgFontGenList(argsp[1], argsp[2], argsp[3]);
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglSwapIntervalEXT") {
        int val = SDL_GL_SetSwapInterval(argsp[0]);
        if (val == -1)
            argsp[0] = 1;
        else {
            DPRINTF("wglSwapIntervalEXT(%u) %s %-24u", argsp[0], ((val)? "err":"ret"), ((val)? val:1));
            argsp[0] = (val)? 0:1;
        }
        return;
    }
    FUNCP_HANDLER("wglGetSwapIntervalEXT") {
        int val = SDL_GL_GetSwapInterval();
        if (val == -1)
            argsp[0] = 1;
        else {
            argsp[0] = val;
            DPRINTF("wglGetSwapIntervalEXT() ret %-24u", argsp[0]);
        }
        return;
    }
    FUNCP_HANDLER("wglGetExtensionsStringARB") {
        if (1 /* wglFuncs.GetExtensionsStringARB */) {
            //const char *str = wglFuncs.GetExtensionsStringARB(hDC);
            const char wglext[] = "WGL_3DFX_gamma_control "
                "WGL_ARB_create_context "
                "WGL_ARB_create_context_profile "
                "WGL_ARB_extensions_string "
                "WGL_ARB_multisample "
                "WGL_ARB_pixel_format "
                GL_RENDER_TEXTURE_STR
                "WGL_EXT_extensions_string "
                "WGL_EXT_swap_control "
                ;
            memcpy((char *)name, wglext, sizeof(wglext));
            *((char *)name + sizeof(wglext) - 2) = '\0';
            //DPRINTF("WGL extensions\nHost: %s [ %d ]\nGuest: %s [ %d ]", str, (uint32_t)strlen(str), name, (uint32_t)strlen(name));
            return;
        }
    }
    FUNCP_HANDLER("wglCreateContextAttribsARB") {
        do {
            uint32_t i, ret;
            for (i = 0; ((i < MAX_LVLCNTX) && ctx[i]); i++);
            argsp[1] = (argsp[0])? i:0;
            if (argsp[1] == 0) {
                SDL_GL_MakeCurrent(window, NULL);
                GL_DELETECONTEXT(ctx[0]);
                GL_CONTEXTATTRIB(ctx[0]);
                GL_CREATECONTEXT(ctx[0]);
                ret = (ctx[0])? 1:0;
            }
            else {
                if (i == MAX_LVLCNTX) {
                    for (i = 1; i < (MAX_LVLCNTX - 1); i++)
                        ctx[i] = ctx[i + 1];
                    argsp[1] = i;
                }
                ret = (ctx[0])? 1:0;
            }
            argsp[0] = ret;
            return;
        } while(0);
    }
    FUNCP_HANDLER("wglGetPixelFormatAttribfvARB") {
        const int *ia = (const int *)&argsp[4], n = argsp[2];
        float pf[64];
        for (int i = 0; i < n; i++)
            pf[i] = (float)LookupAttribArray(iAttribs, ia[i]);
        memcpy(&argsp[2], pf, n*sizeof(float));
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglGetPixelFormatAttribivARB") {
        const int *ia = (const int *)&argsp[4], n = argsp[2];
        int pi[64];
        for (int i = 0; i < n; i++)
            pi[i] = LookupAttribArray(iAttribs, ia[i]);
        memcpy(&argsp[2], pi, n*sizeof(int));
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglChoosePixelFormatARB") {
#define WGL_DRAW_TO_PBUFFER_ARB 0x202D
        const int *ia = (const int *)argsp;
        if (LookupAttribArray(ia, WGL_DRAW_TO_PBUFFER_ARB)) {
            argsp[1] = 0x02;
        }
        else {
            DPRINTF("%-32s", "wglChoosePixelFormatARB()");
            argsp[1] = MGLChoosePixelFormat();
        }
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglBindTexImageARB") {
        uint32_t i = argsp[0] & (MAX_PBUFFER - 1);
        GL_TEXIMAGE_BIND(i);
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglReleaseTexImageARB") {
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglCreatePbufferARB") {
        uint32_t i;
        for (i = 0; ((i < MAX_PBUFFER) && hPbuffer[i].width); i++);
        if (MAX_PBUFFER == i) {
            DPRINTF("MAX_PBUFFER reached %-24u", i);
            argsp[0] = 0;
            return;
        }
        hPbuffer[i].width = argsp[1];
        hPbuffer[i].height = argsp[2];
        const int *pattr = (const int *)&argsp[4];
        hPbuffer[i].target = LookupAttribArray(pattr, WGL_TEXTURE_TARGET_ARB);
        hPbuffer[i].format = LookupAttribArray(pattr, WGL_TEXTURE_FORMAT_ARB);
        hPbuffer[i].level = LookupAttribArray(pattr, WGL_MIPMAP_LEVEL_ARB);
        GL_PBUFFER_CREATE(i);
        argsp[1] = i;
        return;
    }
    FUNCP_HANDLER("wglDestroyPbufferARB") {
        uint32_t i;
        i = argsp[0] & (MAX_PBUFFER - 1);
        GL_PBUFFER_DESTROY(i);
        memset(&hPbuffer[i], 0, sizeof(HPBUFFERARB));
        return;
    }
    FUNCP_HANDLER("wglQueryPbufferARB") {
        uint32_t i = argsp[0] & (MAX_PBUFFER - 1);
#define WGL_PBUFFER_WIDTH_ARB   0x2034
#define WGL_PBUFFER_HEIGHT_ARB  0x2035
        switch(argsp[1]) {
            case WGL_PBUFFER_WIDTH_ARB:
                argsp[2] = hPbuffer[i].width;
                break;
            case WGL_PBUFFER_HEIGHT_ARB:
                argsp[2] = hPbuffer[i].height;
                break;
            case WGL_TEXTURE_TARGET_ARB:
                argsp[2] = hPbuffer[i].target;
                break;
            case WGL_TEXTURE_FORMAT_ARB:
                argsp[2] = hPbuffer[i].format;
                break;
            case WGL_MIPMAP_LEVEL_ARB:
                argsp[2] = hPbuffer[i].level;
                break;
            default:
                argsp[0] = 0;
                return;
        }
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglGetDeviceGammaRamp3DFX") {
        uint32_t ret;
        struct GammaRamp *pRamp = (struct GammaRamp *)&argsp[2];
        ret = ContextUseSRGB()? 0:((SDL_GetWindowGammaRamp(window, pRamp->r, pRamp->g, pRamp->b))? 0:1);
        argsp[0] = ret;
        return;
    }
    FUNCP_HANDLER("wglSetDeviceGammaRamp3DFX") {
        uint32_t ret;
        struct GammaRamp *pRamp = (struct GammaRamp *)&argsp[0];
        ret = ContextUseSRGB()? 0:((SDL_SetWindowGammaRamp(window, pRamp->r, pRamp->g, pRamp->b))? 0:1);
        argsp[0] = ret;
        return;
    }
    FUNCP_HANDLER("wglSetDeviceCursor3DFX") {
        return;
    }

    DPRINTF("  *WARN* Unhandled GLFunc %s", name);
    argsp[0] = 0;
}

#endif //MESAGL_SDLGL
