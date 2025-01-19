/*
 * QEMU 3Dfx Glide Pass-Through
 *
 *  Copyright (c) 2018-2020
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

#include "glide2x_impl.h"

#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, " " fmt "\n", ## __VA_ARGS__); } while(0)

#define GLIDECFG "glide.cfg"

struct tblGlideResolution {
    int w;
    int	h;
};

static struct tblGlideResolution tblRes[] = {
  { .w = 320, .h = 200   }, //0x0
  { .w = 320, .h = 240   }, //0x1
  { .w = 400, .h = 256   }, //0x2
  { .w = 512, .h = 384   }, //0x3
  { .w = 640, .h = 200   }, //0x4
  { .w = 640, .h = 350   }, //0x5
  { .w = 640, .h = 400   }, //0x6
  { .w = 640, .h = 480   }, //0x7
  { .w = 800, .h = 600   }, //0x8
  { .w = 960, .h = 720   }, //0x9
  { .w = 856, .h = 480   }, //0xa
  { .w = 512, .h = 256   }, //0xb
  { .w = 1024, .h = 768  }, //0xC
  { .w = 1280, .h = 1024 }, //0xD
  { .w = 1600, .h = 1200 }, //0xE
  { .w = 400, .h = 300   }, //0xF
  { .w = 0, .h = 0},
};

static int cfg_scaleGuiOff;
static int cfg_scaleX;
static int cfg_cntxMSAA;
static int cfg_cntxSRGB;
static int cfg_cntxVsyncOff;
static int cfg_fpsLimit;
static int cfg_lfbHandler;
static int cfg_lfbNoAux;
static int cfg_lfbLockDirty;
static int cfg_lfbWriteMerge;
static int cfg_lfbMapBufo;
static int cfg_Annotate;
static int cfg_MipMaps;
static int cfg_traceFifo;
static int cfg_traceFunc;
static void *hwnd;

#ifdef CONFIG_DARWIN
int glide_mapbufo(mapbufo_t *bufo, int add) { return 0; }
#endif
#ifdef CONFIG_LINUX
#include "sysemu/kvm.h"

int glide_mapbufo(mapbufo_t *bufo, int add)
{
    int ret = (!cfg_lfbHandler && !cfg_lfbWriteMerge && cfg_lfbMapBufo)? kvm_enabled():0;

    if (ret && bufo && bufo->hva) {
        kvm_update_guest_pa_range(MBUFO_BASE | (bufo->hva & ((MBUFO_SIZE - 1) - (qemu_real_host_page_size() - 1))),
            bufo->mapsz + (bufo->hva & (qemu_real_host_page_size() - 1)),
            (void *)(bufo->hva & qemu_real_host_page_mask()),
            bufo->acc, add);
        bufo->hva = (add)? bufo->hva:0;
    }

    return ret;
}
#endif
#ifdef CONFIG_WIN32
#include "system/whpx.h"

int glide_mapbufo(mapbufo_t *bufo, int add)
{
    int ret = (!cfg_lfbHandler && !cfg_lfbWriteMerge && cfg_lfbMapBufo)? whpx_enabled():0;

    if (ret && bufo && bufo->hva) {
        whpx_update_guest_pa_range(MBUFO_BASE | (bufo->hva & ((MBUFO_SIZE - 1) - (qemu_real_host_page_size() - 1))),
            bufo->mapsz + (bufo->hva & (qemu_real_host_page_size() - 1)),
            (void *)(bufo->hva & qemu_real_host_page_mask()),
            bufo->acc, add);
        bufo->hva = (add)? bufo->hva:0;
    }

    return ret;
}

static int cfg_createWnd;
static LONG WINAPI GlideWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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

static HWND CreateGlideWindow(const char *title, int w, int h)
{
    HWND 	hWnd;
    WNDCLASS 	wc;
    static HINSTANCE hInstance = 0;

    if (!hInstance) {
	memset(&wc, 0, sizeof(WNDCLASS));
	hInstance = GetModuleHandle(NULL);
	wc.style	= CS_OWNDC;
	wc.lpfnWndProc	= (WNDPROC)GlideWndProc;
	wc.lpszClassName = "GlideWnd";

	if (!RegisterClass(&wc)) {
	    DPRINTF("RegisterClass() faled, Error %08lx", GetLastError());
	    return NULL;
	}
    }
    
    RECT rect;
    rect.top = 0; rect.left = 0;
    rect.right = w; rect.bottom = h;
    AdjustWindowRectEx(&rect, WS_CAPTION, FALSE, 0);
    rect.right  -= rect.left;
    rect.bottom -= rect.top;
    hWnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
	    "GlideWnd", title, 
	    WS_CAPTION | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
	    CW_USEDEFAULT, CW_USEDEFAULT, rect.right, rect.bottom,
	    NULL, NULL, hInstance, NULL);
    GetClientRect(hWnd, &rect);
    DPRINTF("    window %lux%lu", rect.right, rect.bottom);
    ShowCursor(FALSE);
    ShowWindow(hWnd, SW_SHOW);

    return hWnd;
}
#endif // CONFIG_WIN32

static int scaledRes(int w, float r)
{
    int i;
    for (i = 0xE; i > 0x7; i--)
	if ((tblRes[i].w == w) && (((float)tblRes[i].h) / tblRes[i].w == r))
	    break;
    if (i == 0x7) {
        i = 0x10;
        tblRes[i].w = w; tblRes[i].h = (w * r);
    }
    return i;
}

int GRFifoTrace(void) { return cfg_traceFifo; }
int GRFuncTrace(void) { return (cfg_traceFifo)? 0:cfg_traceFunc; }
int glide_fpslimit(void) { return cfg_fpsLimit; }
int glide_vsyncoff(void) { return cfg_cntxVsyncOff; }
int glide_lfbmerge(void) { return (cfg_lfbMapBufo)? 0:cfg_lfbWriteMerge; }
int glide_lfbdirty(void) { return (cfg_lfbMapBufo)? 0:cfg_lfbLockDirty; }
int glide_lfbnoaux(void) { return cfg_lfbNoAux; }
int glide_lfbmode(void) { return cfg_lfbHandler; }
void glide_winres(const int res, int *w, int *h)
{
    *w = tblRes[res].w;
    *h = tblRes[res].h;
}

int stat_window(const int res, void *opaque)
{
    int stat, sel, glide_fullscreen;
    window_cb *disp_cb = opaque;
    sel = (cfg_scaleX)? scaledRes(cfg_scaleX, ((float)tblRes[res].h) / tblRes[res].w):res;
    stat = 1;
    glide_fullscreen = glide_gui_fullscreen(0, 0);

    if (stat) {
	int wndStat = glide_window_stat(disp_cb->activate);
	if (disp_cb->activate) {
            wndStat = (wndStat > 1)? (((tblRes[sel].h & 0x7FFFU) << 0x10) | tblRes[sel].w):wndStat;
	    if (wndStat == (((tblRes[sel].h & 0x7FFFU) << 0x10) | tblRes[sel].w)) {
		DPRINTF("    %s %ux%u %s", (glide_fullscreen)? "fullscreen":"window",
                    (wndStat & 0xFFFFU), (wndStat >> 0x10), (cfg_scaleX)? "(scaled)":"");
		stat = 0;
	    }
	}
	else
	    stat = wndStat;
    }
    return stat;
}

void fini_window(void *opaque)
{
    window_cb *disp_cb = opaque;
    disp_cb->activate = 0;
#ifdef CONFIG_WIN32
    if (cfg_createWnd)
        DestroyWindow(hwnd);
    if (hwnd)
        glide_release_window(disp_cb, &cwnd_glide2x);
#endif
#if defined(CONFIG_LINUX) || defined(CONFIG_DARWIN)
    if (hwnd)
        glide_release_window(disp_cb, &cwnd_glide2x);
#endif	    
    hwnd = 0;
    cfg_traceFifo = 0;
    cfg_traceFunc = 0;
}

void init_window(const int res, const char *wndTitle, void *opaque)
{
    window_cb *disp_cb = opaque;

    cfg_scaleGuiOff = 0;
    cfg_scaleX = 0;
    cfg_cntxMSAA = 0;
    cfg_cntxSRGB = 0;
    cfg_cntxVsyncOff = 0;
    cfg_fpsLimit = 0;
    cfg_lfbHandler = 0;
    cfg_lfbNoAux = 0;
    cfg_lfbLockDirty = 0;
    cfg_lfbWriteMerge = 0;
    cfg_lfbMapBufo = 0;
    cfg_Annotate = 0;
    cfg_MipMaps = 0;
    cfg_traceFifo = 0;
    cfg_traceFunc = 0;

    FILE *fp = fopen(GLIDECFG, "r");
    if (fp != NULL) {
        char line[32];
        int i, c;
        while (fgets(line, 32, fp)) {
            i = sscanf(line, "ScaleGuiOff,%d", &c);
            cfg_scaleGuiOff = ((i == 1) && c)? 1:cfg_scaleGuiOff;
            i = sscanf(line, "ScaleWidth,%d", &c);
            cfg_scaleX = ((i == 1) && c)? c:cfg_scaleX;
            i = sscanf(line, "ContextMSAA,%d", &c);
            cfg_cntxMSAA = (i == 1)? ((c & 0x03U) << 2):cfg_cntxMSAA;
            i = sscanf(line, "ContextSRGB,%d", &c);
            cfg_cntxSRGB = ((i == 1) && c)? 1:cfg_cntxSRGB;
            i = sscanf(line, "ContextVsyncOff,%d", &c);
            cfg_cntxVsyncOff = ((i == 1) && c)? 1:cfg_cntxVsyncOff;
            i = sscanf(line, "FpsLimit,%d", &c);
            cfg_fpsLimit = (i == 1)? (c & 0x7FU):cfg_fpsLimit;
            i = sscanf(line, "LfbHandler,%d", &c);
            cfg_lfbHandler = ((i == 1) && c)? 1:cfg_lfbHandler;
            i = sscanf(line, "LfbNoAux,%d", &c);
            cfg_lfbNoAux = ((i == 1) && c)? 1:cfg_lfbNoAux;
            i = sscanf(line, "LfbLockDirty,%d", &c);
            cfg_lfbLockDirty = ((i == 1) && c)? 1:cfg_lfbLockDirty;
            i = sscanf(line, "LfbWriteMerge,%d", &c);
            cfg_lfbWriteMerge = ((i == 1) && c)? 1:cfg_lfbWriteMerge;
            i = sscanf(line, "LfbMapBufo,%d", &c);
            cfg_lfbMapBufo = ((i == 1) && c)? 1:cfg_lfbMapBufo;
            i = sscanf(line, "Annotate,%d", &c);
            cfg_Annotate = ((i == 1) && c)? 1:cfg_Annotate;
            i = sscanf(line, "MipMaps,%d", &c);
            cfg_MipMaps = ((i == 1) && c)? 1:cfg_MipMaps;
            i = sscanf(line, "FifoTrace,%d", &c);
            cfg_traceFifo = ((i == 1) && c)? 1:cfg_traceFifo;
            i = sscanf(line, "FuncTrace,%d", &c);
            cfg_traceFunc = ((i == 1) && c)? (c % 3):cfg_traceFunc;
	}
        fclose(fp);
    }

    int gui_height, glide_fullscreen = glide_gui_fullscreen(0, &gui_height);
    cfg_scaleGuiOff = (glide_fullscreen || cfg_scaleX)? 1:cfg_scaleGuiOff;
    cfg_scaleX = (!cfg_scaleGuiOff && (gui_height > 480) && (gui_height > tblRes[res].h))?
        (int)((1.f * tblRes[res].w * gui_height) / tblRes[res].h):cfg_scaleX;

#define WRAPPER_FLAG_WINDOWED           0x01
#define WRAPPER_FLAG_MIPMAPS            0x02
#define WRAPPER_FLAG_ANNOTATE           0x10
#define WRAPPER_FLAG_FRAMEBUFFER_SRGB   0x20
#define WRAPPER_FLAG_VSYNCOFF           0x40
#define WRAPPER_FLAG_QEMU               0x80
    uint32_t flags = (glide_fullscreen)? WRAPPER_FLAG_QEMU:
        (WRAPPER_FLAG_QEMU | WRAPPER_FLAG_WINDOWED);
    flags |= (cfg_MipMaps)? WRAPPER_FLAG_MIPMAPS:0;
    flags |= (cfg_Annotate)? WRAPPER_FLAG_ANNOTATE:0;
    flags |= (cfg_cntxVsyncOff)? WRAPPER_FLAG_VSYNCOFF:0;
    flags |= (cfg_cntxSRGB)? WRAPPER_FLAG_FRAMEBUFFER_SRGB:0;
    flags |= cfg_cntxMSAA;

    int sel = res;
    if (cfg_scaleX) {
        sel = scaledRes(cfg_scaleX, ((float)tblRes[res].h) / tblRes[res].w);
        conf_glide2x(flags, tblRes[sel].w);
    }
    else
        conf_glide2x(flags, 0);

    disp_cb->activate = 1;
    hwnd = (void *)(uintptr_t)(((tblRes[sel].h & 0x7FFFU) << 0x10) | tblRes[sel].w);
#ifdef CONFIG_WIN32
    if (cfg_createWnd)
        hwnd = CreateGlideWindow(wndTitle, tblRes[sel].w, tblRes[sel].h);
    glide_prepare_window(((tblRes[sel].h & 0x7FFFU) << 0x10) | tblRes[sel].w,
            (cfg_cntxMSAA > 8)? 16:cfg_cntxMSAA, disp_cb, &cwnd_glide2x);
#endif
#if defined(CONFIG_LINUX) || defined(CONFIG_DARWIN)
    glide_prepare_window(((tblRes[sel].h & 0x7FFFU) << 0x10) | tblRes[sel].w,
            (cfg_cntxMSAA > 8)? 16:cfg_cntxMSAA, disp_cb, &cwnd_glide2x);
#endif	
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
	fprintf(stderr, "%-4u frames in %-4.1f seconds, %-4.1f FPS%-8s\r", p->fcount, p->ftime, (p->fcount / p->ftime)," ");
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
	p->last = (glide_gui_fullscreen(0, 0))? 0:get_clock();
	return;
    }

    curr = get_clock();
    p->fcount++;
    p->ftime += (curr - p->last) * (1.0f /  NANOSECONDS_PER_SECOND);
    p->last = curr;

    i = (GRFifoTrace() || GRFuncTrace())? 0:((int) p->ftime);
    if (i && ((i % 5) == 0))
	profile_dump();
}

void glidestat(PPERFSTAT s)
{
    cfg_traceFunc = 1;
    s->stat = &profile_stat;
    s->last = &profile_last;
}

