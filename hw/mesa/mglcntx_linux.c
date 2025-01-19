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
#include <GL/glx.h>
#include <X11/extensions/xf86vmode.h>
#ifdef CONFIG_DARWIN
const char dllname[] = "/opt/X11/lib/libGL.dylib";
int MGLUpdateGuestBufo(mapbufo_t *bufo, int add) { return 0; }
#endif
#ifdef CONFIG_LINUX
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
#endif

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

static int *iattribs_fb(Display *dpy, const int do_msaa)
{
    static int ia[] = {
        GLX_X_RENDERABLE    , True,
        GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
        GLX_RENDER_TYPE     , GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
        GLX_BUFFER_SIZE     , 32,
        GLX_DEPTH_SIZE      , 24,
        GLX_STENCIL_SIZE    , 8,
        GLX_DOUBLEBUFFER    , True,
        GLX_SAMPLE_BUFFERS  , 0,
        GLX_SAMPLES         , 0,
        None
    };

    int nElem, cBufsz = 0;
    GLXFBConfig *currFB = glXGetFBConfigs(dpy, DefaultScreen(dpy), &nElem);
    if (currFB && nElem) {
        glXGetFBConfigAttrib(dpy, currFB[0], GLX_BUFFER_SIZE, &cBufsz);
        XFree(currFB);
    }

    for (int i = 0; ia[i]; i+=2) {
        switch(ia[i]) {
            case GLX_BUFFER_SIZE:
                ia[i+1] = (cBufsz >= 24)? cBufsz:ia[i+1];
                break;
            case GLX_SAMPLE_BUFFERS:
                ia[i+1] = (do_msaa)? 1:0;
                break;
            case GLX_SAMPLES:
                ia[i+1] = (do_msaa)? do_msaa:0;
                break;
            default:
                break;
        }
    }
    return ia;
}

static Display     *dpy;
static Window       win;
static XVisualInfo *xvi;
static int          xvidmode;
static const char  *xstr, *xcstr;
static GLXContext   ctx[MAX_LVLCNTX];
static GLXPbuffer PBDC[MAX_PBUFFER];
static GLXContext PBRC[MAX_PBUFFER];

static HPBUFFERARB hPbuffer[MAX_PBUFFER];
static int wnd_ready;
static int cAlphaBits, cDepthBits, cStencilBits;
static int cAuxBuffers, cSampleBuf[2];

static struct {
    int (*SwapIntervalEXT)(unsigned int);
    int (*GetSwapIntervalEXT)(void);
} xglFuncs;

int glwnd_ready(void) { return qatomic_read(&wnd_ready); }

int MGLExtIsAvail(const char *xstr, const char *str)
{ return find_xstr(xstr, str); }

#define MAX_RAMP_SIZE 0x800
struct wgamma {
    uint16_t r[0x100];
    uint16_t g[0x100];
    uint16_t b[0x100];
};
struct xgamma {
    uint16_t r[MAX_RAMP_SIZE];
    uint16_t g[MAX_RAMP_SIZE];
    uint16_t b[MAX_RAMP_SIZE];
};

static void MesaInitGammaRamp(void)
{
    struct xgamma GammaRamp;
    int rampsz;
    if (xvidmode)
        XF86VidModeGetGammaRampSize(dpy, DefaultScreen(dpy), &rampsz);
    else rampsz = 0;
    switch(rampsz) {
        case 0x100:
            for (int i = 0; i < rampsz; i++) {
                GammaRamp.r[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
                GammaRamp.g[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
                GammaRamp.b[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
            }
            break;
        case 0x400:
            for (int i = 0; i < rampsz; i++) {
                GammaRamp.r[i] = (uint16_t)(((i << 6) | (((i << 6) & 0xFC00U) >> 10)) & 0xFFFFU);
                GammaRamp.g[i] = (uint16_t)(((i << 6) | (((i << 6) & 0xFC00U) >> 10)) & 0xFFFFU);
                GammaRamp.b[i] = (uint16_t)(((i << 6) | (((i << 6) & 0xFC00U) >> 10)) & 0xFFFFU);
            }
            break;
        case 0x800:
            for (int i = 0; i < rampsz; i++) {
                GammaRamp.r[i] = (uint16_t)(((i << 5) | (((i << 5) & 0xF800U) >> 11)) & 0xFFFFU);
                GammaRamp.g[i] = (uint16_t)(((i << 5) | (((i << 5) & 0xF800U) >> 11)) & 0xFFFFU);
                GammaRamp.b[i] = (uint16_t)(((i << 5) | (((i << 5) & 0xF800U) >> 11)) & 0xFFFFU);
            }
            break;

        default:
            return;
    }
    XF86VidModeSetGammaRamp(dpy, DefaultScreen(dpy), rampsz,
        GammaRamp.r, GammaRamp.g, GammaRamp.b);
}

static void cwnd_mesagl(void *swnd, void *nwnd, void *opaque)
{
    win = (Window)nwnd;
    qatomic_set(&wnd_ready, 1);
    DPRINTF("MESAGL window [native %p] ready", nwnd);
}

void SetMesaFuncPtr(void *p)
{
}

void *MesaGLGetProc(const char *proc)
{
    return (void *)glXGetProcAddress((const GLubyte *)proc);
}

void MGLTmpContext(void)
{
    Display *tmpDisp = XOpenDisplay(NULL);
    xcstr = glXGetClientString(tmpDisp, GLX_VENDOR);
    xstr = glXQueryExtensionsString(tmpDisp, DefaultScreen(tmpDisp));
    if (find_xstr(xstr, "GLX_MESA_swap_control")) {
        xglFuncs.SwapIntervalEXT = (int (*)(unsigned int))
            MesaGLGetProc("glXSwapIntervalMESA");
        xglFuncs.GetSwapIntervalEXT = (int (*)(void))
            MesaGLGetProc("glXGetSwapIntervalMESA");
    }
    else {
        xglFuncs.SwapIntervalEXT = 0;
        xglFuncs.GetSwapIntervalEXT = 0;
    }
    XCloseDisplay(tmpDisp);
}

void MGLDeleteContext(int level)
{
    int n = (level)? ((level % MAX_LVLCNTX)? (level % MAX_LVLCNTX):1):level;
    glXMakeContextCurrent(dpy, None, None, NULL);
    if (n == 0) {
        for (int i = MAX_LVLCNTX; i > 1;) {
            if (ctx[--i]) {
                glXDestroyContext(dpy, ctx[i]);
                ctx[i] = 0;
            }
        }
        MesaBlitFree();
    }
    glXDestroyContext(dpy, ctx[n]);
    ctx[n] = 0;
    if (!n)
        MGLActivateHandler(0, 0);
}

void MGLWndRelease(void)
{
    if (win) {
        MesaInitGammaRamp();
        XFree(xvi);
        XCloseDisplay(dpy);
        mesa_release_window();
        xvi = 0;
        dpy = 0;
        win = 0;
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
        glXMakeContextCurrent(dpy, None, None, NULL);
        for (i = MAX_LVLCNTX; i > 0;) {
            if (ctx[--i]) {
                glXDestroyContext(dpy, ctx[i]);
                ctx[i] = 0;
            }
        }
        ctx[0] = glXCreateContext(dpy, xvi, NULL, true);
        ret = (ctx[0])? 0:1;
    }
    return ret;
}

int MGLMakeCurrent(uint32_t cntxRC, int level)
{
    int n = (level)? ((level % MAX_LVLCNTX)? (level % MAX_LVLCNTX):1):level;
    uint32_t i = cntxRC & (MAX_PBUFFER - 1);
    if (cntxRC == (MESAGL_MAGIC - n)) {
        glXMakeContextCurrent(dpy, win, win, ctx[n]);
        InitMesaGLExt();
        wrContextSRGB(ContextUseSRGB());
        if (ContextVsyncOff()) {
            const int val = 0;
            if (xglFuncs.SwapIntervalEXT)
                xglFuncs.SwapIntervalEXT(val);
            else if (find_xstr(xstr, "GLX_EXT_swap_control")) {
                void (*p_glXSwapIntervalEXT)(Display *, GLXDrawable, int) =
                    (void (*)(Display *, GLXDrawable, int)) MesaGLGetProc("glXSwapIntervalEXT");
                if (p_glXSwapIntervalEXT)
                    p_glXSwapIntervalEXT(dpy, win, val);
            }
        }
        if (!n)
            MGLActivateHandler(1, 0);
    }
    if (cntxRC == (((MESAGL_MAGIC & 0xFFFFFFFU) << 4) | i))
        glXMakeContextCurrent(dpy, PBDC[i], PBDC[i], PBRC[i]);

    return 0;
}

int MGLSwapBuffers(void)
{
    MGLActivateHandler(1, 0);
    MesaBlitScale();
    glXSwapBuffers(dpy, win);
    return 1;
}

static int MGLPresetPixelFormat(void)
{
    const char nvstr[] = "NVIDIA ";
    dpy = XOpenDisplay(NULL);
    qatomic_set(&wnd_ready, 0);
    ImplMesaGLReset();
    mesa_prepare_window(GetContextMSAA(), memcmp(xcstr, nvstr, sizeof(nvstr) - 1), 0, &cwnd_mesagl);

    int fbid, fbcnt, *attrib = iattribs_fb(dpy, GetContextMSAA());
    GLXFBConfig *fbcnf = glXChooseFBConfig(dpy, DefaultScreen(dpy), attrib, &fbcnt);
    if (GetContextMSAA() && !fbcnt && !fbcnf) {
        attrib = iattribs_fb(dpy, 0);
        fbcnf = glXChooseFBConfig(dpy, DefaultScreen(dpy), attrib, &fbcnt);
    }
    xvi = glXGetVisualFromFBConfig(dpy, fbcnf[0]);
    glXGetFBConfigAttrib(dpy, fbcnf[0], GLX_FBCONFIG_ID, &fbid);
    glXGetFBConfigAttrib(dpy, fbcnf[0], GLX_ALPHA_SIZE, &cAlphaBits);
    glXGetFBConfigAttrib(dpy, fbcnf[0], GLX_DEPTH_SIZE, &cDepthBits);
    glXGetFBConfigAttrib(dpy, fbcnf[0], GLX_STENCIL_SIZE, &cStencilBits);
    glXGetFBConfigAttrib(dpy, fbcnf[0], GLX_AUX_BUFFERS, &cAuxBuffers);
    glXGetFBConfigAttrib(dpy, fbcnf[0], GLX_SAMPLE_BUFFERS, &cSampleBuf[0]);
    glXGetFBConfigAttrib(dpy, fbcnf[0], GLX_SAMPLES, &cSampleBuf[1]);
    int major, minor;
    xvidmode = XF86VidModeQueryExtension(dpy, &major, &minor)? 1:0;
    DPRINTF("FBConfig 0x%03x visual 0x%03lx nAux %d nSamples %d %d vidMode %d %s",
        fbid, xvi->visualid, cAuxBuffers, cSampleBuf[0], cSampleBuf[1], xvidmode,
        ContextUseSRGB()? "sRGB":"");
    MesaInitGammaRamp();
    XFree(fbcnf);
    XFlush(dpy);
    return 1;
}

int MGLChoosePixelFormat(void)
{
    DPRINTF("ChoosePixelFormat()");
    if (xvi == 0)
        return MGLPresetPixelFormat();
    return 1;
}

int MGLSetPixelFormat(int fmt, const void *p)
{
    DPRINTF("SetPixelFormat()");
    if (xvi == 0)
        return MGLPresetPixelFormat();
    return 1;
}

int MGLDescribePixelFormat(int fmt, unsigned int sz, void *p)
{
    //DPRINTF("DescribePixelFormat()");
    if (xvi == 0)
        MGLPresetPixelFormat();
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
    return (ctx[0] == glXGetCurrentContext());
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
        uint32_t ret = 0;
        XFontStruct *fi = XLoadQueryFont(dpy, "fixed");
        if (fi) {
            int minchar = fi->min_char_or_byte2;
            int maxchar = fi->max_char_or_byte2;
            glXUseXFont(fi->fid, minchar, maxchar - minchar + 1, argsp[3] + minchar);
            XFreeFont(dpy, fi);
            ret = 1;
        }
        argsp[0] = ret;
        return;
    }
    FUNCP_HANDLER("wglSwapIntervalEXT") {
        int val = -1;
        if (xglFuncs.SwapIntervalEXT)
            val = xglFuncs.SwapIntervalEXT(argsp[0]);
        else if (find_xstr(xstr, "GLX_EXT_swap_control")) {
            void (*p_glXSwapIntervalEXT)(Display *, GLXDrawable, int) =
                (void (*)(Display *, GLXDrawable, int)) MesaGLGetProc("glXSwapIntervalEXT");
            if (p_glXSwapIntervalEXT) {
                p_glXSwapIntervalEXT(dpy, win, argsp[0]);
                val = 0;
            }
        }
        if (val != -1) {
            DPRINTF("wglSwapIntervalEXT(%u) %s %-24u", argsp[0], ((val)? "err":"ret"), ((val)? val:1));
            argsp[0] = (val)? 0:1;
            return;
        }
        /* XQuartz/GLX missing swap_control */
        if (!find_xstr(xstr, "GLX_MESA_swap_control") &&
            !find_xstr(xstr, "GLX_EXT_swap_control")) {
            argsp[0] = 1;
            return;
        }
    }
    FUNCP_HANDLER("wglGetSwapIntervalEXT") {
        int val = -1;
        if (xglFuncs.GetSwapIntervalEXT)
            val = xglFuncs.GetSwapIntervalEXT();
        else if (find_xstr(xstr, "GLX_EXT_swap_control"))
            glXQueryDrawable(dpy, win, GLX_SWAP_INTERVAL_EXT, (unsigned int *)&val);
        if (val != -1) {
            argsp[0] = val;
            DPRINTF("wglGetSwapIntervalEXT() ret %-24u", argsp[0]);
            return;
        }
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
                "WGL_ARB_pbuffer WGL_ARB_render_texture WGL_NV_render_texture_rectangle "
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
        strncpy(fname, "glXCreateContextAttribsARB", sizeof(fname)-1);
        GLXContext (*fp)(Display *, GLXFBConfig, GLXContext, Bool, const int *) =
            (GLXContext (*)(Display *, GLXFBConfig, GLXContext, Bool, const int *)) MesaGLGetProc(fname);
        if (fp) {
            uint32_t i, ret;
            int fbcnt, *attrib = iattribs_fb(dpy, GetContextMSAA());
            GLXFBConfig *fbcnf = glXChooseFBConfig(dpy, DefaultScreen(dpy), attrib, &fbcnt);
            if (GetContextMSAA() && !fbcnt && !fbcnf) {
                attrib = iattribs_fb(dpy, 0);
                fbcnf = glXChooseFBConfig(dpy, DefaultScreen(dpy), attrib, &fbcnt);
            }
            for (i = 0; ((i < MAX_LVLCNTX) && ctx[i]); i++);
            argsp[1] = (argsp[0])? i:0;
            if (argsp[1] == 0) {
                glXMakeContextCurrent(dpy, None, None, NULL);
                for (i = MAX_LVLCNTX; i > 0;) {
                    if (ctx[--i]) {
                        glXDestroyContext(dpy, ctx[i]);
                        ctx[i] = 0;
                    }
                }
                MGLActivateHandler(0, 0);
                ctx[0] = fp(dpy, fbcnf[0], 0, True, (const int *)&argsp[2]);
                ret = (ctx[0])? 1:0;
            }
            else {
                if (i == MAX_LVLCNTX) {
                    glXDestroyContext(dpy, ctx[1]);
                    for (i = 1; i < (MAX_LVLCNTX - 1); i++)
                        ctx[i] = ctx[i + 1];
                    argsp[1] = i;
                }
                ctx[i] = fp(dpy, fbcnf[0], ctx[i-1], True, (const int *)&argsp[2]);
                ret = (ctx[i])? 1:0;
            }
            XFree(fbcnf);
            XFlush(dpy);
            argsp[0] = ret;
            return;
        }
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
        if (PbufferGLBinding(hPbuffer[i].target) && PbufferGLAttrib(hPbuffer[i].format)) {
            int prev_binded_texture = 0;
            GLXContext prev_context = glXGetCurrentContext();
            GLXDrawable prev_drawable = glXGetCurrentDrawable();
            glGetIntegerv(PbufferGLBinding(hPbuffer[i].target), &prev_binded_texture);
            glXMakeCurrent(dpy, PBDC[i], PBRC[i]);
            glBindTexture(PbufferGLAttrib(hPbuffer[i].target), prev_binded_texture);
            glCopyTexImage2D(PbufferGLAttrib(hPbuffer[i].target), hPbuffer[i].level,
                PbufferGLAttrib(hPbuffer[i].format), 0, 0, hPbuffer[i].width, hPbuffer[i].height, 0);
            glXMakeCurrent(dpy, prev_drawable, prev_context);
        }
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
        const int ia[] = {
            GLX_X_RENDERABLE    , True,
            GLX_DRAWABLE_TYPE   , GLX_PBUFFER_BIT,
            GLX_RENDER_TYPE     , GLX_RGBA_BIT,
            GLX_DOUBLEBUFFER    , False,
            GLX_BUFFER_SIZE     , 32,
            GLX_ALPHA_SIZE      , cAlphaBits,
            GLX_DEPTH_SIZE      , cDepthBits,
            None,
        };
        int pbcnt, pa[] = {
            GLX_PBUFFER_WIDTH, hPbuffer[i].width,
            GLX_PBUFFER_HEIGHT, hPbuffer[i].height,
            None,
        };
        GLXFBConfig *pbcnf = glXChooseFBConfig(dpy, DefaultScreen(dpy), ia, &pbcnt);
        PBDC[i] = glXCreatePbuffer(dpy, pbcnf[0], pa);
        PBRC[i] = glXCreateNewContext(dpy, pbcnf[0], GLX_RGBA_TYPE, glXGetCurrentContext(), true);
        XFree(pbcnf);
        argsp[0] = 1;
        argsp[1] = i;
        return;
    }
    FUNCP_HANDLER("wglDestroyPbufferARB") {
        uint32_t i;
        i = argsp[0] & (MAX_PBUFFER - 1);
        glXDestroyContext(dpy, PBRC[i]);
        glXDestroyPbuffer(dpy, PBDC[i]);
        PBRC[i] = 0; PBDC[i] = 0;
        argsp[0] = 1;
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
        struct xgamma xRamp;
        struct wgamma *wRamp = (struct wgamma *)&argsp[2];
        int rampsz;
        if (xvidmode && !ContextUseSRGB())
            XF86VidModeGetGammaRampSize(dpy, DefaultScreen(dpy), &rampsz);
        else rampsz = 0;
        if (rampsz)
            XF86VidModeGetGammaRamp(dpy, DefaultScreen(dpy), rampsz,
                xRamp.r, xRamp.g, xRamp.b);
        switch (rampsz) {
            case 0x100:
                memcpy(wRamp->r, xRamp.r, rampsz);
                memcpy(wRamp->g, xRamp.g, rampsz);
                memcpy(wRamp->b, xRamp.b, rampsz);
                break;
            case 0x400:
                for (int i = 0; i < 0x100; i++) {
                    wRamp->r[i] = (xRamp.r[i << 2] & 0xFF00U) | i;
                    wRamp->g[i] = (xRamp.g[i << 2] & 0xFF00U) | i;
                    wRamp->b[i] = (xRamp.b[i << 2] & 0xFF00U) | i;
                }
                break;
            case 0x800:
                for (int i = 0; i < 0x100; i++) {
                    wRamp->r[i] = (xRamp.r[i << 3] & 0xFF00U) | i;
                    wRamp->g[i] = (xRamp.g[i << 3] & 0xFF00U) | i;
                    wRamp->b[i] = (xRamp.b[i << 3] & 0xFF00U) | i;
                }
                break;

            default:
                argsp[0] = 0;
                return;
        }
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglSetDeviceGammaRamp3DFX") {
        struct xgamma xRamp;
        struct wgamma *wRamp = (struct wgamma *)&argsp[0];
        int rampsz;
        if (xvidmode && !ContextUseSRGB())
            XF86VidModeGetGammaRampSize(dpy, DefaultScreen(dpy), &rampsz);
        else rampsz = 0;
        switch(rampsz) {
            case 0x100:
                memcpy(xRamp.r, wRamp->r, rampsz);
                memcpy(xRamp.g, wRamp->g, rampsz);
                memcpy(xRamp.b, wRamp->b, rampsz);
                break;
            case 0x400:
                for (int i = 0; (i + 1) < 0x100; i++) {
                    for (int j = 0; j < 4; j++) {
                        xRamp.r[(i << 2) + j] = wRamp->r[i] + (j * ((wRamp->r[i + 1] - wRamp->r[i]) >> 2));
                        xRamp.r[(i << 2) + j] |= (xRamp.r[(i << 2) + j] & 0xFF00U) >> 8;
                        xRamp.g[(i << 2) + j] = wRamp->g[i] + (j * ((wRamp->g[i + 1] - wRamp->g[i]) >> 2));
                        xRamp.g[(i << 2) + j] |= (xRamp.g[(i << 2) + j] & 0xFF00U) >> 8;
                        xRamp.b[(i << 2) + j] = wRamp->b[i] + (j * ((wRamp->b[i + 1] - wRamp->b[i]) >> 2));
                        xRamp.b[(i << 2) + j] |= (xRamp.b[(i << 2) + j] & 0xFF00U) >> 8;
                    }
                }
                for (int k = (rampsz - 4); k < rampsz; k++) {
                    xRamp.r[k] = 0xFFFFU;
                    xRamp.g[k] = 0xFFFFU;
                    xRamp.b[k] = 0xFFFFU;
                }
                break;
            case 0x800:
                for (int i = 0; (i + 1) < 0x100; i++) {
                    for (int j = 0; j < 8; j++) {
                        xRamp.r[(i << 3) + j] = wRamp->r[i] + (j * ((wRamp->r[i + 1] - wRamp->r[i]) >> 3));
                        xRamp.r[(i << 3) + j] |= (xRamp.r[(i << 3) + j] & 0xFF00U) >> 8;
                        xRamp.g[(i << 3) + j] = wRamp->g[i] + (j * ((wRamp->g[i + 1] - wRamp->g[i]) >> 3));
                        xRamp.g[(i << 3) + j] |= (xRamp.g[(i << 3) + j] & 0xFF00U) >> 8;
                        xRamp.b[(i << 3) + j] = wRamp->b[i] + (j * ((wRamp->b[i + 1] - wRamp->b[i]) >> 3));
                        xRamp.b[(i << 3) + j] |= (xRamp.b[(i << 3) + j] & 0xFF00U) >> 8;
                    }
                }
                for (int k = (rampsz - 8); k < rampsz; k++) {
                    xRamp.r[k] = 0xFFFFU;
                    xRamp.g[k] = 0xFFFFU;
                    xRamp.b[k] = 0xFFFFU;
                }
                break;

            default:
                argsp[0] = 0;
                return;
        }
        XF86VidModeSetGammaRamp(dpy, DefaultScreen(dpy), rampsz,
            xRamp.r, xRamp.g, xRamp.b);
        argsp[0] = 1;
        return;
    }
    FUNCP_HANDLER("wglSetDeviceCursor3DFX") {
        return;
    }

    DPRINTF("  *WARN* Unhandled GLFunc %s", name);
    argsp[0] = 0;
}

#endif //CONFIG_LINUX
