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


#if defined(CONFIG_WIN32)
#include <GL/wgl.h>
#include "system/whpx.h"

int MGLUpdateGuestBufo(mapbufo_t *bufo, int add)
{
    int ret = GetBufOAccelEN()? whpx_enabled():0;

    if (ret && bufo) {
        bufo->lvl = (add)? MapBufObjGpa(bufo):0;
        whpx_update_guest_pa_range(MBUFO_BASE | (bufo->gpa & ((MBUFO_SIZE - 1) - (qemu_real_host_page_size() - 1))),
            bufo->mapsz + (bufo->hva & (qemu_real_host_page_size() - 1)),
            (void *)(bufo->hva & qemu_real_host_page_mask()),
            (bufo->acc & GL_MAP_WRITE_BIT)? 0:1, add);
    }

    return ret;
}

static LONG WINAPI MGLWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg) {
	case WM_MOUSEACTIVATE:
	    return MA_NOACTIVATEANDEAT;
        case WM_ACTIVATE:
        case WM_ACTIVATEAPP:
	case WM_NCLBUTTONDOWN:
	    return 0;
	default:
	    break;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static HWND CreateMesaWindow(const char *title, int w, int h, int show)
{
    HWND 	hWnd;
    WNDCLASS 	wc;
    HINSTANCE   hInstance = GetModuleHandle(0);

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.hInstance = hInstance;
    wc.style	= CS_OWNDC;
    wc.lpfnWndProc	= (WNDPROC)MGLWndProc;
    wc.lpszClassName = title;

    if (!RegisterClass(&wc)) {
        DPRINTF("RegisterClass() failed, Error 0x%08lx", GetLastError());
        return NULL;
    }
    
    RECT rect;
    rect.top = 0; rect.left = 0;
    rect.right = w; rect.bottom = h;
    AdjustWindowRectEx(&rect, WS_CAPTION, FALSE, 0);
    rect.right  -= rect.left;
    rect.bottom -= rect.top;
    hWnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
	    title, title,
	    WS_CAPTION | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
	    CW_USEDEFAULT, CW_USEDEFAULT, rect.right, rect.bottom,
	    NULL, NULL, hInstance, NULL);
    if (show) {
        GetClientRect(hWnd, &rect);
        DPRINTF("    window %lux%lu", rect.right, rect.bottom);
        ShowCursor(FALSE);
        ShowWindow(hWnd, SW_SHOW);
    }

    return hWnd;
}

static int *iattribs_fb(const int do_msaa)
{
    static int ia[] = {
        WGL_DRAW_TO_WINDOW_ARB, 1,
        WGL_SUPPORT_OPENGL_ARB, 1,
        WGL_DOUBLE_BUFFER_ARB, 1,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_COLOR_BITS_ARB, 32,
        WGL_DEPTH_BITS_ARB, 24,
        WGL_ALPHA_BITS_ARB, 8,
        WGL_STENCIL_BITS_ARB, 8,
        WGL_SAMPLE_BUFFERS_ARB, 0,
        WGL_SAMPLES_ARB, 0,
        0,0,
    };
    for (int i = 0; ia[i]; i+=2) {
        switch(ia[i]) {
            case WGL_SAMPLE_BUFFERS_ARB:
                ia[i+1] = (do_msaa)? 1:0;
                break;
            case WGL_SAMPLES_ARB:
                ia[i+1] = (do_msaa)? do_msaa:0;
                break;
            default:
                break;
        }
    }
    return ia;
}


static HWND hwnd;
static HDC hDC, hPBDC[MAX_PBUFFER];
static HGLRC hRC[MAX_LVLCNTX], hPBRC[MAX_PBUFFER];
static HPBUFFERARB hPbuffer[MAX_PBUFFER];
static int wnd_ready, GLon12;

static struct {
    HGLRC (WINAPI *CreateContext)(HDC);
    HGLRC (WINAPI *GetCurrentContext)(VOID);
    BOOL  (WINAPI *MakeCurrent)(HDC, HGLRC);
    BOOL  (WINAPI *DeleteContext)(HGLRC);
    BOOL  (WINAPI *UseFontBitmapsA)(HDC, DWORD, DWORD, DWORD);
    BOOL  (WINAPI *ShareLists)(HGLRC, HGLRC);
    PROC  (WINAPI *GetProcAddress)(LPCSTR);
    /* WGL extensions */
    BOOL (WINAPI *GetPixelFormatAttribivARB)(HDC, int, int, UINT, const int *, int *);
    BOOL (WINAPI *ChoosePixelFormatARB)(HDC, const int *, const float *, UINT, int *, UINT *);
    const char * (WINAPI *GetExtensionsStringARB)(HDC);
    HGLRC (WINAPI *CreateContextAttribsARB)(HDC, HGLRC, const int *);
    BOOL (WINAPI *SwapIntervalEXT)(int);
    int (WINAPI *GetSwapIntervalEXT)(void);
} wglFuncs;

int glwnd_ready(void) { return qatomic_read(&wnd_ready); }

int MGLExtIsAvail(const char *xstr, const char *str)
{ return find_xstr(xstr, str); }

static void MesaInitGammaRamp(void)
{
    struct {
        uint16_t r[256];
        uint16_t g[256];
        uint16_t b[256];
    } GammaRamp;

    for (int i = 0; i < 256; i++) {
        GammaRamp.r[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
        GammaRamp.g[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
        GammaRamp.b[i] = (uint16_t)(((i << 8) | i) & 0xFFFFU);
    }
    SetDeviceGammaRamp(hDC, &GammaRamp);
}

static void cwnd_mesagl(void *swnd, void *nwnd, void *opaque)
{
    ReleaseDC(hwnd, hDC);
    hwnd = (HWND)nwnd;
    hDC = GetDC(hwnd);
    qatomic_set(&wnd_ready, 1);
    DPRINTF("MESAGL window [native %p] ready", nwnd);
}

static void TmpContextPurge(void)
{
    HWND tmpWin = FindWindow("dummy", "dummy");
    if (tmpWin) {
        DestroyWindow(tmpWin);
        if (!UnregisterClass("dummy", GetModuleHandle(0)))
            DPRINTF("UnregisterClass() failed, Error 0x%08lx", GetLastError());
    }
}

void SetMesaFuncPtr(void *p)
{
    HINSTANCE hDLL = (HINSTANCE)p;
    wglFuncs.GetProcAddress = (PROC (WINAPI *)(LPCSTR))GetProcAddress(hDLL, "wglGetProcAddress");
    wglFuncs.GetCurrentContext = (HGLRC (WINAPI *)(VOID))GetProcAddress(hDLL, "wglGetCurrentContext");
    wglFuncs.CreateContext = (HGLRC (WINAPI *)(HDC))GetProcAddress(hDLL, "wglCreateContext");
    wglFuncs.MakeCurrent   = (BOOL (WINAPI *)(HDC, HGLRC))GetProcAddress(hDLL, "wglMakeCurrent");
    wglFuncs.DeleteContext = (BOOL (WINAPI *)(HGLRC))GetProcAddress(hDLL, "wglDeleteContext");
    wglFuncs.UseFontBitmapsA = (BOOL (WINAPI *)(HDC, DWORD, DWORD, DWORD))GetProcAddress(hDLL, "wglUseFontBitmapsA");
    wglFuncs.ShareLists = (BOOL (WINAPI *)(HGLRC, HGLRC))GetProcAddress(hDLL, "wglShareLists");
}

void *MesaGLGetProc(const char *proc)
{
    return (void *)wglFuncs.GetProcAddress(proc);
}

void MGLTmpContext(void)
{
    HWND tmpWin = CreateMesaWindow("dummy", 640, 480, 0);
    HDC  tmpDC = GetDC(tmpWin);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.iLayerType = PFD_MAIN_PLANE,
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cAlphaBits = 8;
    pfd.cStencilBits = 8;
    if (tmpWin && tmpDC &&
        SetPixelFormat(tmpDC, ChoosePixelFormat(tmpDC, &pfd), &pfd)) {
        HGLRC tmpGL = wglFuncs.CreateContext(tmpDC);
        if (!tmpGL)
            DPRINTF("CreateContext() failed, Error 0x%08lx", GetLastError());
        else {
            wglFuncs.MakeCurrent(tmpDC, tmpGL);

            wglFuncs.GetPixelFormatAttribivARB = (BOOL (WINAPI *)(HDC, int, int, UINT, const int *, int *))
                MesaGLGetProc("wglGetPixelFormatAttribivARB");
            wglFuncs.ChoosePixelFormatARB = (BOOL (WINAPI *)(HDC, const int *, const float *, UINT, int *, UINT *))
                MesaGLGetProc("wglChoosePixelFormatARB");
            wglFuncs.GetExtensionsStringARB =  (const char * (WINAPI *)(HDC))
                MesaGLGetProc("wglGetExtensionsStringARB");
            wglFuncs.CreateContextAttribsARB = (HGLRC (WINAPI *)(HDC, HGLRC, const int *))
                MesaGLGetProc("wglCreateContextAttribsARB");
            wglFuncs.SwapIntervalEXT = (BOOL (WINAPI *)(int))
                MesaGLGetProc("wglSwapIntervalEXT");
            wglFuncs.GetSwapIntervalEXT = (int (WINAPI *)(void))
                MesaGLGetProc("wglGetSwapIntervalEXT");

            GLon12 = GLIsD3D12();
            wglFuncs.MakeCurrent(NULL, NULL);
            wglFuncs.DeleteContext(tmpGL);
        }
        ReleaseDC(tmpWin, tmpDC);
        hwnd = tmpWin;
    }
}

#define GLWINDOW_INIT() \
    if (hDC == 0) { if (0) \
    CreateMesaWindow("MesaGL", 640, 480, 1); \
    qatomic_set(&wnd_ready, 0); \
    ImplMesaGLReset(); \
    mesa_prepare_window(GetContextMSAA(), GLon12, 0, &cwnd_mesagl); hDC = GetDC(hwnd); }

#define GLWINDOW_FINI() \
    if (0) { } \
    else mesa_release_window()

void MGLDeleteContext(int level)
{
    int n = (level)? ((level % MAX_LVLCNTX)? (level % MAX_LVLCNTX):1):level;
    wglFuncs.MakeCurrent(NULL, NULL);
    if (n == 0) {
        for (int i = MAX_LVLCNTX; i > 1;) {
            if (hRC[--i]) {
                wglFuncs.DeleteContext(hRC[i]);
                hRC[i] = 0;
            }
        }
        MesaBlitFree();
    }
    wglFuncs.DeleteContext(hRC[n]);
    hRC[n] = 0;
    if (!n)
        MGLActivateHandler(0, 0);
}

void MGLWndRelease(void)
{
    if (hwnd) {
        MesaInitGammaRamp();
        ReleaseDC(hwnd, hDC);
        TmpContextPurge();
        GLWINDOW_FINI();
        hDC = 0;
        hwnd = 0;
    }
}

int MGLCreateContext(uint32_t gDC)
{
    int i, ret;
    i = gDC & (MAX_PBUFFER - 1);
    if (gDC == ((MESAGL_HPBDC & 0xFFFFFFF0U) | i)) {
        hPBRC[i] = wglFuncs.CreateContext(hPBDC[i]);
        ret = (hPBRC[i])? 0:1;
    }
    else {
        wglFuncs.MakeCurrent(NULL, NULL);
        for (i = MAX_LVLCNTX; i > 0;) {
            if (hRC[--i]) {
                wglFuncs.DeleteContext(hRC[i]);
                hRC[i] = 0;
            }
        }
        hRC[0] = wglFuncs.CreateContext(hDC);
        ret = (hRC[0])? 0:1;
    }
    return ret;
}

int MGLMakeCurrent(uint32_t cntxRC, int level)
{
    int n = (level)? ((level % MAX_LVLCNTX)? (level % MAX_LVLCNTX):1):level;
    uint32_t i = cntxRC & (MAX_PBUFFER - 1);
    if (cntxRC == (MESAGL_MAGIC - n)) {
        wglFuncs.MakeCurrent(hDC, hRC[n]);
        InitMesaGLExt();
        wrContextSRGB(ContextUseSRGB());
        if (ContextVsyncOff()) {
            const int val = 0;
            if (wglFuncs.SwapIntervalEXT)
                wglFuncs.SwapIntervalEXT(val);
        }
        if (!n)
            MGLActivateHandler(1, 0);
    }
    if (cntxRC == (((MESAGL_MAGIC & 0xFFFFFFFU) << 4) | i))
        wglFuncs.MakeCurrent(hPBDC[i], hPBRC[i]);

    return 0;
}

int MGLSwapBuffers(void)
{
    MGLActivateHandler(1, 0);
    MesaBlitScale();
    return SwapBuffers(hDC);
}

static int MGLPresetPixelFormat(void)
{
    int ipixfmt = 0;

    if (wglFuncs.ChoosePixelFormatARB) {
        static const float fa[] = {0, 0};
        int *ia = iattribs_fb(GetContextMSAA());
        int pi[64]; UINT nFmts = 0;
        BOOL status = wglFuncs.ChoosePixelFormatARB(hDC, ia, fa, 64, pi, &nFmts);
        if (GetContextMSAA() && !nFmts) {
            ia = iattribs_fb(0);
            status = wglFuncs.ChoosePixelFormatARB(hDC, ia, fa, 64, pi, &nFmts);
        }
        ipixfmt = (status && nFmts)? pi[0]:0;
    }

    if (ipixfmt == 0) {
        DPRINTF("Fallback to legacy OpenGL context creation");
        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
        pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion = 1;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.iLayerType = PFD_MAIN_PLANE,
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cAlphaBits = 8;
        pfd.cStencilBits = 8;
        ipixfmt = ChoosePixelFormat(hDC, &pfd);
    }

    MesaInitGammaRamp();
    return ipixfmt;
}

int MGLChoosePixelFormat(void)
{
    int fmt, curr;
    GLWINDOW_INIT();
    curr = GetPixelFormat(hDC);
    if (curr == 0)
        curr = MGLPresetPixelFormat();
    fmt = curr;
    DPRINTF("ChoosePixelFormat() fmt 0x%02x", fmt);
    return fmt;
}

int MGLSetPixelFormat(int fmt, const void *p)
{
    const PIXELFORMATDESCRIPTOR *ppfd = (const PIXELFORMATDESCRIPTOR *)p;
    int curr, ret;
    GLWINDOW_INIT();
    curr = GetPixelFormat(hDC);
    if (curr == 0) {
        curr = MGLPresetPixelFormat();
        ret = SetPixelFormat(hDC, curr, (ppfd->nSize)? ppfd:0);
    }
    else {
        ret = 1;
        TmpContextPurge();
    }

    if (wglFuncs.GetPixelFormatAttribivARB) {
        static const int iattr[] = {
            WGL_AUX_BUFFERS_ARB,
            WGL_SAMPLE_BUFFERS_ARB,
            WGL_SAMPLES_ARB,
            WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB,
        };
        int cattr[4];
        wglFuncs.GetPixelFormatAttribivARB(hDC, curr, 0, 4, iattr, cattr);
        cattr[3] = (cattr[3] && ContextUseSRGB())? 1:0;
        DPRINTF("PixFmt 0x%02x nAux %d nSamples %d %d %s", curr,
            cattr[0], cattr[1], cattr[2], (cattr[3])? "sRGB":"");
    }
    DPRINTF("SetPixelFormat() fmt 0x%02x ret %d", curr, (ret)? 1:0);
    return ret;
}

int MGLDescribePixelFormat(int fmt, unsigned int sz, void *p)
{
    LPPIXELFORMATDESCRIPTOR ppfd = (LPPIXELFORMATDESCRIPTOR)p;
    int curr;
    GLWINDOW_INIT();
    curr = GetPixelFormat(hDC);
    if (curr == 0)
        curr = MGLPresetPixelFormat();
    if (sz == sizeof(PIXELFORMATDESCRIPTOR)) {
        int cattr[2];
        if (wglFuncs.GetPixelFormatAttribivARB) {
            static const int iattr[] = {
                WGL_SUPPORT_OPENGL_ARB,
                WGL_ACCELERATION_ARB,
            };
            wglFuncs.GetPixelFormatAttribivARB(hDC, curr, 0, 2, iattr, cattr);
        }
        DescribePixelFormat(hDC, curr, sizeof(PIXELFORMATDESCRIPTOR), ppfd);
        ppfd->dwFlags |= (cattr[0] && (cattr[1] == WGL_FULL_ACCELERATION_ARB))? PFD_SUPPORT_OPENGL:0;
        DPRINTF_COND(GLFuncTrace(), "DescribePixelFormat() dwFlags:%08lx\n"
            "  cColorbits:%02d cDepthBits:%02d cStencilBits:%02d ARGB%d%d%d%d\n"
            "  cAlphaShift:%02d cRedShift:%02d cGreenShift:%02d cBlueShift:%02d",
            ppfd->dwFlags,
            ppfd->cColorBits, ppfd->cDepthBits, ppfd->cStencilBits,
            ppfd->cRedBits, ppfd->cGreenBits, ppfd->cBlueBits, ppfd->cAlphaBits,
            ppfd->cAlphaShift, ppfd->cRedShift, ppfd->cGreenShift, ppfd->cBlueShift);
    }
    return curr;
}

int NumPbuffer(void)
{
    int i, c;
    for (i = 0, c = 0; i < MAX_PBUFFER;)
        if (hPbuffer[i++]) c++;
    return c;
}

int DrawableContext(void)
{
    return (hRC[0] == wglFuncs.GetCurrentContext());
}

static int LookupAttribArray(const int *attrib, const int attr)
{
    int ret = 0;
    for (int i = 0; attrib[i]; i+=2) {
        if (attrib[i] == attr) {
            ret = attrib[i+1];
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
        if (((argsp[0] == MESAGL_MAGIC) && (argsp[1] == ((MESAGL_MAGIC & 0xFFFFFFFU) << 4 | i))) &&
            (hRC[0] && hPBRC[i]))
            ret = wglFuncs.ShareLists(hRC[0], hPBRC[i]);
        else {
            DPRINTF("  *WARN* ShareLists called with unknown contexts, %x %x", argsp[0], argsp[1]);
        }
        argsp[0] = ret;
        return;
    }
    FUNCP_HANDLER("wglUseFontBitmapsA") {
        uint32_t ret;
        ret = wglFuncs.UseFontBitmapsA(hDC, argsp[1], argsp[2], argsp[3]);
        argsp[0] = ret;
        return;
    }
    FUNCP_HANDLER("wglSwapIntervalEXT") {
        if (wglFuncs.SwapIntervalEXT) {
            uint32_t ret, err;
            int curr = wglFuncs.GetSwapIntervalEXT();
            if (curr != argsp[0]) {
                ret =  wglFuncs.SwapIntervalEXT(argsp[0]);
                err = (ret)? 0:GetLastError();
                DPRINTF("wglSwapIntervalEXT(%u) %s %-24u", argsp[0], ((ret)? "ret":"err"), ((ret)? ret:err));
            }
            else {
                ret = 1;
                DPRINTF("wglSwapIntervalEXT(%u) curr %d ret %-24u", argsp[0], curr, ret);
            }
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglGetSwapIntervalEXT") {
        if (wglFuncs.GetSwapIntervalEXT) {
            uint32_t ret;
            ret = wglFuncs.GetSwapIntervalEXT();
            DPRINTF("wglGetSwapIntervalEXT() ret %-24u", ret);
            argsp[0] = ret;
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
                "WGL_ARB_pbuffer WGL_ARB_render_texture "
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
        if (wglFuncs.CreateContextAttribsARB) {
            uint32_t i, ret;
            for (i = 0; ((i < MAX_LVLCNTX) && hRC[i]); i++);
            argsp[1] = (argsp[0])? i:0;
            if (argsp[1] == 0) {
                wglFuncs.MakeCurrent(NULL, NULL);
                for (i = MAX_LVLCNTX; i > 0;) {
                    if (hRC[--i]) {
                        wglFuncs.DeleteContext(hRC[i]);
                        hRC[i] = 0;
                    }
                }
                MGLActivateHandler(0, 0);
                hRC[0] = wglFuncs.CreateContextAttribsARB(hDC, 0, (const int *)&argsp[2]);
                ret = (hRC[0])? 1:0;
            }
            else {
                if (i == MAX_LVLCNTX) {
                    wglFuncs.DeleteContext(hRC[1]);
                    for (i = 1; i < (MAX_LVLCNTX - 1); i++)
                        hRC[i] = hRC[i + 1];
                    argsp[1] = i;
                }
                hRC[i] = wglFuncs.CreateContextAttribsARB(hDC, hRC[i-1], (const int *)&argsp[2]);
                ret = (hRC[i])? 1:0;
            }
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglGetPixelFormatAttribfvARB") {
        BOOL (__stdcall *fp)(HDC, int, int, UINT, const int *, float *) =
            (BOOL (__stdcall *)(HDC, int, int, UINT, const int *, float *)) MesaGLGetProc(fname);
        if (fp) {
            uint32_t ret;
            float pf[64], n = argsp[2];
            ret = fp(hDC, argsp[0], argsp[1], argsp[2], (const int *)&argsp[4], pf);
            if (ret)
                memcpy(&argsp[2], pf, n*sizeof(float));
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglGetPixelFormatAttribivARB") {
        if (wglFuncs.GetPixelFormatAttribivARB) {
            uint32_t ret;
            int pi[64], n = argsp[2];
            ret = wglFuncs.GetPixelFormatAttribivARB(hDC, argsp[0], argsp[1], argsp[2], (const int *)&argsp[4], pi);
            if (ret)
                memcpy(&argsp[2], pi, n*sizeof(int));
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglChoosePixelFormatARB") {
        if (wglFuncs.ChoosePixelFormatARB) {
            const int *ia = (const int *)argsp;
            if (LookupAttribArray(ia, WGL_DRAW_TO_PBUFFER_ARB)) {
                int piFormats[64]; UINT nNumFormats;
                float fa[] = {0,0};
                wglFuncs.ChoosePixelFormatARB(hDC, ia, fa, 64, piFormats, &nNumFormats);
                argsp[1] = (nNumFormats)? piFormats[0]:0;
            }
            else {
                DPRINTF("%-32s", "wglChoosePixelFormatARB()");
                argsp[1] = MGLChoosePixelFormat();
            }
            argsp[0] = 1;
            return;
        }
    }
    FUNCP_HANDLER("wglBindTexImageARB") {
        BOOL (__stdcall *fp)(HPBUFFERARB, int) =
            (BOOL (__stdcall *)(HPBUFFERARB, int)) MesaGLGetProc(fname);
        if (fp) {
            uint32_t i, ret;
            i = argsp[0] & (MAX_PBUFFER - 1);
            ret = (hPbuffer[i])? fp(hPbuffer[i], argsp[1]):0;
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglReleaseTexImageARB") {
        BOOL (__stdcall *fp)(HPBUFFERARB, int) =
            (BOOL (__stdcall *)(HPBUFFERARB, int)) MesaGLGetProc(fname);
        if (fp) {
            uint32_t i, ret;
            i = argsp[0] & (MAX_PBUFFER - 1);
            ret = (hPbuffer[i])? fp(hPbuffer[i], argsp[1]):0;
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglSetPbufferAttribARB") {
        BOOL (__stdcall *fp)(HPBUFFERARB, const int *) =
            (BOOL (__stdcall *)(HPBUFFERARB, const int *)) MesaGLGetProc(fname);
        if (fp) {
            uint32_t i, ret;
            i = argsp[0] & (MAX_PBUFFER - 1);
            ret = (hPbuffer[i])? fp(hPbuffer[i], (const int *)&argsp[2]):0;
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglCreatePbufferARB") {
        HPBUFFERARB (__stdcall *fp)(HDC, int, int, int, const int *) =
            (HPBUFFERARB (__stdcall *)(HDC, int, int, int, const int *)) MesaGLGetProc(fname);
        HDC (__stdcall *fpDC)(HPBUFFERARB) =
            (HDC (__stdcall *)(HPBUFFERARB)) MesaGLGetProc("wglGetPbufferDCARB");
        if (fp && fpDC) {
            uint32_t i;
            for (i = 0; ((i < MAX_PBUFFER) && hPbuffer[i]); i++);
            if (MAX_PBUFFER == i) {
                DPRINTF("MAX_PBUFFER reached %-24u", i);
                argsp[0] = 0;
                return;
            }
            hPbuffer[i] = fp(hDC, argsp[0], argsp[1], argsp[2], (const int *)&argsp[4]);
            hPBDC[i] = fpDC(hPbuffer[i]);
            argsp[0] = (hPbuffer[i] && hPBDC[i])? 1:0;
            argsp[1] = i;
            return;
        }
    }
    FUNCP_HANDLER("wglDestroyPbufferARB") {
        BOOL (__stdcall *fp)(HPBUFFERARB) =
            (BOOL (__stdcall *)(HPBUFFERARB)) MesaGLGetProc(fname);
        int (__stdcall *fpDC)(HPBUFFERARB, HDC) =
            (int (__stdcall *)(HPBUFFERARB, HDC)) MesaGLGetProc("wglReleasePbufferDCARB");
        if (fp && fpDC) {
            uint32_t i, ret;
            i = argsp[0] & (MAX_PBUFFER - 1);
            wglFuncs.DeleteContext(hPBRC[i]);
            fpDC(hPbuffer[i], hPBDC[i]);
            ret = fp(hPbuffer[i]);
            hPbuffer[i] = 0; hPBDC[i] = 0; hPBRC[i] = 0;
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglQueryPbufferARB") {
        BOOL (__stdcall *fp)(HPBUFFERARB, int, int *) =
            (BOOL (__stdcall *)(HPBUFFERARB, int, int *)) MesaGLGetProc(fname);
        if (fp) {
            uint32_t i, ret;
            i = argsp[0] & (MAX_PBUFFER - 1);
            ret = (hPbuffer[i])? fp(hPbuffer[i], argsp[1], (int *)&argsp[2]):0;
            argsp[0] = ret;
            return;
        }
    }
    FUNCP_HANDLER("wglGetDeviceGammaRamp3DFX") {
        uint32_t ret;
        ret = ContextUseSRGB()? 0:GetDeviceGammaRamp(hDC, &argsp[2]);
        argsp[0] = ret;
        return;
    }
    FUNCP_HANDLER("wglSetDeviceGammaRamp3DFX") {
        uint32_t ret;
        ret = ContextUseSRGB()? 0:SetDeviceGammaRamp(hDC, &argsp[0]);
        argsp[0] = ret;
        return;
    }
    FUNCP_HANDLER("wglSetDeviceCursor3DFX") {
        return;
    }

    DPRINTF("  *WARN* Unhandled GLFunc %s", name);
    argsp[0] = 0;
}

#endif //CONFIG_WIN32

void MGLActivateHandler(const int i, const int d)
{
    static int last;

    if (i != last) {
        last = i;
        DPRINTF_COND(GLFuncTrace(), "wm_activate %-32d", i);
        if (i) {
            deactivateGuiRefSched();
            mesa_renderer_stat(i);
        }
        else
            deactivateSched(d);
    }
}

void MGLCursorDefine(int hot_x, int hot_y, int width, int height,
                        const void *data)
{
    mesa_cursor_define(hot_x, hot_y, width, height, data);
}

void MGLMouseWarp(const uint32_t ci)
{
    static uint32_t last_ci = 0;

    if (ci != last_ci) {
        last_ci = ci;
        int x = ((ci >> 16) & 0x7FFFU),
            y = (ci & 0x7FFFU), on = (ci)? 1:0;
        mesa_mouse_warp(x, y, on);
    }
}

static QEMUTimer *ts;
static void deactivateOnce(void)
{
    MGLMouseWarp(0);
    mesa_renderer_stat(0);
}
static void deactivateOneshot(void *opaque)
{
    deactivateCancel();
    deactivateOnce();
}
void deactivateCancel(void)
{
    if (ts) {
        timer_del(ts);
        timer_free(ts);
        ts = 0;
    }
}
void deactivateSched(const int deferred)
{
    if (!deferred)
        deactivateOneshot(0);
    else {
        deactivateCancel();
        ts = timer_new_ms(QEMU_CLOCK_VIRTUAL, deactivateOneshot, 0);
        timer_mod(ts, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + GetDispTimerMS());
    }
}
static void deactivateGuiRefOneshot(void *opaque)
{
    deactivateCancel();
    graphic_hw_passthrough(qemu_console_lookup_by_index(0), 1);
}
void deactivateGuiRefSched(void)
{
    deactivateCancel();
    ts = timer_new_ms(QEMU_CLOCK_VIRTUAL, deactivateGuiRefOneshot, 0);
    timer_mod(ts, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + GUI_REFRESH_INTERVAL_DEFAULT);
}

int find_xstr(const char *xstr, const char *str)
{
#define MAX_XSTR 128
    int nbuf, ret = 0;
    char *xbuf, *stok;
    if (xstr) {
        size_t slen = strnlen(str, MAX_XSTR);
        nbuf = strnlen(xstr, 3*PAGE_SIZE);
        xbuf = g_new(char, nbuf + 1);
        strncpy(xbuf, xstr, nbuf + 1);
        stok = strtok(xbuf, " ");
        while (stok) {
            size_t xlen = strnlen(stok, MAX_XSTR);
            if ((slen == xlen) && !strncmp(stok, str, xlen)) {
                ret = 1;
                break;
            }
            stok = strtok(NULL, " ");
        }
        g_free(xbuf);
    }
    return ret;
}

typedef struct {
    uint64_t last;
    uint32_t fcount;
    float ftime;
} STATSFX, * PSTATSFX;

static STATSFX fxstats = { .last = 0 };

static void profile_dump(void)
{
    PSTATSFX p = &fxstats;
    if (p->last) {
	p->last = 0;
	fprintf(stderr, "%-4u frames in %-4.1f seconds, %-4.1f FPS%-8s\r", p->fcount, p->ftime, (p->fcount / p->ftime), " ");
        fflush(stderr);
    }
}

static void profile_last(void)
{
    PSTATSFX p = &fxstats;
    if (p->last) {
	p->last = 0;
	fprintf(stderr, "%-64s\r", " ");
    }
}

#ifndef NANOSECONDS_PER_SECOND
#define NANOSECONDS_PER_SECOND get_ticks_per_sec()
#endif

static void profile_stat(void)
{
    uint64_t curr;
    int i;

    PSTATSFX p = &fxstats;

    if (p->last == 0) {
	p->fcount = 0;
	p->ftime = 0;
	p->last = (mesa_gui_fullscreen(0))? 0:get_clock();
	return;
    }

    curr = get_clock();
    p->fcount++;
    p->ftime += (curr - p->last) * (1.0f /  NANOSECONDS_PER_SECOND);
    p->last = curr;

    i = (GLFifoTrace() || GLFuncTrace() || GLShaderDump() || GLCheckError())? 0:((int) p->ftime);
    if (i && ((i % 5) == 0))
	profile_dump();
}

void mesastat(PPERFSTAT s)
{
    s->stat = &profile_stat;
    s->last = &profile_last;
}
