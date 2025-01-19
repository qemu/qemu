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
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"

#include "glide2x_impl.h"
#include "gllstbuf.h"
#include "vertex3x.h"

#define DEBUG_GLIDEPT

#define TYPE_GLIDELFB "glidelfb"
#define GLIDELFB(obj) \
    OBJECT_CHECK(GlideLfbState, (obj), TYPE_GLIDELFB)
#define GLIDEPT(obj) \
    OBJECT_CHECK(GlidePTState, (obj), TYPE_GLIDEPT)

#ifdef DEBUG_GLIDEPT
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "glidept: " fmt "\n", ## __VA_ARGS__); } while(0)
#define WARNONCE(cond, fmt, ...) \
    do { static int warn; if (cond && !warn) { warn = 1; \
         fprintf(stderr, "     *WARN* " fmt "\n", ## __VA_ARGS__); } } while(0)
#else
#define DPRINTF(fmt, ...)
#define WARNONCE(cond, fmt, ...)
#endif

typedef struct GlideLfbState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint8_t *lfbPtr[2];
    uint32_t stride[2];
    int lock[2];
    mapbufo_t mbufo[2];
    uint32_t guestLfb;
    uint32_t origin;
    uint32_t writeMode;
    uint32_t byPass;
    uint32_t grBuffer;
    uint32_t grLock;
    uint32_t lfbMax;
    int begin;
    int v1Lfb;
    int emu211;
} GlideLfbState;

typedef struct GlidePTState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    MemoryRegion fifo_ram;
    uint8_t *fifo_ptr;
    uint32_t *arg;
    uint32_t *hshm;
    int datacb, fifoMax, dataMax;

    MemoryRegion glfb_ram;
    uint8_t *glfb_ptr;
    int lfb_dirty, lfb_real, lfb_noaux, lfb_merge;
    int lfb_w, lfb_h;

    GlideLfbState *lfbDev;
    uint32_t szGrState;
    uint32_t szVtxLayout;
    uint8_t *vtxCache;
    uintptr_t parg[4];
    window_cb disp_cb;
    char version[80];
    uint32_t FEnum;
    uintptr_t FRet;
    uint32_t initDLL;
    uint32_t GrRes;
    int cfgPushed;
    wrTexStruct GrTex;
    PERFSTAT perfs;
} GlidePTState;

static uint64_t glidept_read(void *opaque, hwaddr addr, unsigned size)
{
    GlidePTState *s = opaque;
    uint64_t val;

    switch (addr) {
	case 0xfb8:
            val = stat_window(s->GrRes, &s->disp_cb);
	    break;
	case 0xfbc:
	    val = s->initDLL;
	    break;
        case 0xfc0:
            val = s->FRet;
            break;
        default:
            val = 0;
            break;
    }
    
    return val;
}

#if 0
static inline int guest_memory_rw_debug(CPUState *cpu, target_ulong addr,
					uint8_t *buf, int len, bool is_write)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->memory_rw_debug) {
	return cc->memory_rw_debug(cpu, addr, buf, len, is_write);
    }
    return cpu_memory_rw_debug(cpu, addr, buf, len, is_write);
}
static int debug_guest_memory_read(hwaddr vaddr, void *buf, int len)
{
    CPUState *cs = current_cpu;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int retval = 0;

    if (guest_memory_rw_debug(cs, vaddr, buf, len, false) == -1) {
	DPRINTF("Guest vaddr 0x%08x Pagefault read addr 0x%08x len 0x%08x", (uint32_t)(env->eip), (uint32_t)vaddr, (uint32_t)len);
        retval = -1;
    }
    return retval;
}
static void guest_memory_read(hwaddr vaddr, void *buf, int len)
{
    CPUState *cs = current_cpu;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (guest_memory_rw_debug(cs, vaddr, buf, len, false) == -1)
	DPRINTF("Guest vaddr 0x%08x Pagefault read addr 0x%08x len 0x%08x", (uint32_t)(env->eip), (uint32_t)vaddr, (uint32_t)len);
}
static void guest_memory_write(hwaddr vaddr, void *buf, int len)
{
    CPUState *cs = current_cpu;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (guest_memory_rw_debug(cs, vaddr, buf, len, true) == -1)
	DPRINTF("Guest vaddr 0x%08x Pagefault write addr 0x%08x len 0x%08x", (uint32_t)(env->eip), (uint32_t)vaddr, (uint32_t)len);
}
#endif

static void vgLfbFlush(GlidePTState *s)
{
    if ((s->lfbDev->emu211 == 0) && glide_mapbufo(0, 0))
        return;
    uint32_t stride = ((s->lfbDev->writeMode & 0x0EU) == 0x04)? 0x1000:0x800;
    uint32_t xwidth = ((s->lfbDev->writeMode & 0x0EU) == 0x04)? (s->lfb_w << 2):(s->lfb_w << 1);
    uint8_t *gLfb = ((s->FEnum == FEnum_grLfbUnlock) && (s->arg[1] & 0xFEU))?
        (s->glfb_ptr + (((s->lfb_h > 0x300)? 0x300:s->lfb_h) * 0x800)):s->glfb_ptr;
    uint8_t *hLfb = s->lfbDev->lfbPtr[1];
    if (hLfb == 0)
        DPRINTF("WARN: LFB write pointer is NULL");
    else {
        for (int y = 0; y < s->lfb_h; y++) {
            memcpy(hLfb, gLfb, xwidth);
            hLfb += s->lfbDev->stride[1];
            gLfb += stride;
        }
    }
}

#define PTR(x,y) (((uint8_t *)x)+y)
#define VAL(x) (uintptr_t)x
static void processArgs(GlidePTState *s)
{
    uint8_t *outshm = s->fifo_ptr + (GRSHM_SIZE - PAGE_SIZE);

    switch (s->FEnum) {
	case FEnum_grDrawLine:
	case FEnum_grAADrawLine:
            do {
                uint32_t szvert = (s->initDLL == 0x301a0)? size_vertex3x():SIZE_GRVERTEX;
                s->datacb = 2*ALIGNED(szvert);
                s->parg[0] = VAL(PTR(s->hshm,0));
                s->parg[1] = VAL(PTR(s->hshm,1*ALIGNED(szvert)));
            } while(0);
	    break;
        case FEnum_grDrawTriangle:
	case FEnum_grAADrawTriangle:
	case FEnum_guDrawTriangleWithClip:
	case FEnum_guAADrawTriangleWithClip:
            do {
                uint32_t szvert = (s->initDLL == 0x301a0)? size_vertex3x():SIZE_GRVERTEX;
                s->datacb = 3*ALIGNED(szvert);
                s->parg[0] = VAL(PTR(s->hshm,0));
                s->parg[1] = VAL(PTR(s->hshm,1*ALIGNED(szvert)));
                s->parg[2] = VAL(PTR(s->hshm,2*ALIGNED(szvert)));
            } while(0);
            break;
	case FEnum_grDrawPoint:
	case FEnum_grAADrawPoint:
            do {
                uint32_t szvert = (s->initDLL == 0x301a0)? size_vertex3x():SIZE_GRVERTEX;
                s->datacb = ALIGNED(szvert);
                s->parg[0] = VAL(PTR(s->hshm,0));
            } while(0);
	    break;
        case FEnum_grGlideSetState:
        case FEnum_grGlideGetState:
	    s->parg[0] = VAL(LookupGrState(s->arg[0], s->szGrState));
	    break;
        case FEnum_grGlideGetVersion:
            s->parg[0] = VAL(outshm);
            break;
        case FEnum_grSstPerfStats:
            s->datacb = ALIGNED(SIZE_GRSSTPERFSTATS);
            memcpy(outshm, s->hshm, SIZE_GRSSTPERFSTATS);
            s->parg[0] = VAL(outshm);
            break;
	case FEnum_grSstQueryBoards:
        case FEnum_grSstQueryHardware:
            s->datacb = ALIGNED(SIZE_GRHWCONFIG);
            memcpy(outshm, s->hshm, SIZE_GRHWCONFIG);
            s->parg[0] = VAL(outshm);
            break;
	case FEnum_grTriStats:
            s->datacb = 2 * ALIGNED(sizeof(uint32_t));
            memcpy(outshm, PTR(s->hshm, 0), ALIGNED(sizeof(uint32_t)));
            memcpy(PTR(outshm, ALIGNED(sizeof(uint32_t))), PTR(s->hshm, ALIGNED(sizeof(uint32_t))), ALIGNED(sizeof(uint32_t)));
	    s->parg[0] = VAL(PTR(outshm, 0));
	    s->parg[1] = VAL(PTR(outshm, ALIGNED(sizeof(uint32_t))));
	    break;
	case FEnum_grSstOpen:
	    if (s->lfbDev->emu211) {
		s->arg[4] = s->arg[3];
		s->arg[3] = s->arg[2];
		s->arg[2] = s->arg[1];
		s->arg[1] = s->arg[0];
		s->arg[7] = s->arg[6];
		s->arg[6] = 1;
		s->FEnum = FEnum_grSstWinOpen;
	    }
	    DPRINTF("grSstOpen called");
	    break;
        case FEnum_grSstWinOpen:
        case FEnum_grSstWinOpenExt:
            if (s->FEnum == FEnum_grSstWinOpenExt) {
                DPRINTF("grSstWinOpenExt called, cf %d org %d pf %d buf %u aux %u gLfb 0x%08x",
                        s->arg[3], s->arg[4], s->arg[5], s->arg[6], s->arg[7], s->arg[8]);
            }
            else {
                DPRINTF("grSstWinOpen called, fmt %d org %d buf %u aux %u gLfb 0x%08x",
                        s->arg[3], s->arg[4], s->arg[5], s->arg[6], s->arg[7]);
            }
	    break;
        case FEnum_grGlideInit:
            if (s->GrTex.fbuf && s->GrTex.flen) {
                int fd = open("glide.cfg", O_BINARY | O_CREAT | O_WRONLY, 0666);
                if (fd > 0) {
                    if (s->GrTex.flen == write(fd, s->GrTex.fbuf, s->GrTex.flen))
                        DPRINTF("Push cfgFile, size = %-8x", s->GrTex.flen);
                    close(fd);
                    s->cfgPushed = 1;
                }
                s->GrTex.flen = 0;
            }
            break;
        case FEnum_grTexSource:
        case FEnum_grTexDownloadMipMap:
            s->datacb = ALIGNED(SIZE_GRTEXINFO);
	    s->GrTex.texInfo = (wrTexInfo *)outshm;
	    (s->GrTex.texInfo)->small = ((wrgTexInfo *)PTR(s->hshm,0))->small;
	    (s->GrTex.texInfo)->large = ((wrgTexInfo *)PTR(s->hshm,0))->large;
	    (s->GrTex.texInfo)->aspect = ((wrgTexInfo *)PTR(s->hshm,0))->aspect;
	    (s->GrTex.texInfo)->format = ((wrgTexInfo *)PTR(s->hshm,0))->format;
	    if (s->FEnum == FEnum_grTexDownloadMipMap) {
                s->datacb += ALIGNED(s->arg[4]);
                (s->GrTex.texInfo)->data = PTR(s->hshm, ALIGNED(SIZE_GRTEXINFO));
	    }
            s->parg[3] = VAL(s->GrTex.texInfo);
            break;
	case FEnum_grTexDownloadMipMapLevel:
	case FEnum_grTexDownloadMipMapLevelPartial:
            s->datacb = (s->FEnum == FEnum_grTexDownloadMipMapLevel)? ALIGNED(s->arg[8]):ALIGNED(s->arg[10]);
	    s->parg[3] = VAL(s->hshm);
	    break;
	case FEnum_grTexDownloadTable:
            s->datacb = (s->arg[1] == GR_TEXTABLE_PALETTE)? SIZE_GUTEXPALETTE:SIZE_GUNCCTABLE;
            s->parg[2] = VAL(s->hshm);
            break;
	case FEnum_grTexDownloadTablePartial:
            s->datacb = (s->arg[1] == GR_TEXTABLE_PALETTE)? ALIGNED((s->arg[4] + 1)*sizeof(uint32_t)):SIZE_GUNCCTABLE;
	    s->parg[2] = VAL(s->hshm);
	    break;
	case FEnum_grTexDownloadTable3x:
            s->datacb = (s->arg[0] >= GR_TEXTABLE_PALETTE)? SIZE_GUTEXPALETTE:SIZE_GUNCCTABLE;
            s->parg[1] = VAL(s->hshm);
            break;
	case FEnum_grTexDownloadTablePartial3x:
            s->datacb = (s->arg[0] >= GR_TEXTABLE_PALETTE)? ALIGNED((s->arg[3] + 1)*sizeof(uint32_t)):SIZE_GUNCCTABLE;
            s->parg[1] = VAL(s->hshm);
            break;
	case FEnum_grTexTextureMemRequired:
            s->datacb = ALIGNED(SIZE_GRTEXINFO);
	    s->GrTex.texInfo = (wrTexInfo *)outshm;
	    s->GrTex.texInfo->small = ((wrgTexInfo *)PTR(s->hshm,0))->small;
	    s->GrTex.texInfo->large = ((wrgTexInfo *)PTR(s->hshm,0))->large;
	    s->GrTex.texInfo->aspect = ((wrgTexInfo *)PTR(s->hshm,0))->aspect;
	    s->GrTex.texInfo->format = ((wrgTexInfo *)PTR(s->hshm,0))->format;
	    s->GrTex.texInfo->data = 0;
	    s->parg[1] = VAL(s->GrTex.texInfo);
	    break;

        case FEnum_grBufferSwap:
            WARNONCE(((s->lfb_real == 0) && (s->lfbDev->lock[0] || s->lfbDev->lock[1])), 
                    "LFB locked on buffer swap, buf %d rd %d wr %d", s->lfbDev->grBuffer,
                    s->lfbDev->lock[0], s->lfbDev->lock[1]);
            if (s->lfb_real == 0) {
                if (s->lfbDev->lock[1] && ((s->lfbDev->grBuffer & 0xFEU) == 0))
                    vgLfbFlush(s);
                if (s->lfb_dirty & 0x80U)
                    wrWriteRegion(1, 0, 0, 0, s->lfb_w, s->lfb_h, 0x800, (uintptr_t)(s->glfb_ptr));
                s->lfb_dirty = 1;
            }
            if (glide_vsyncoff())
                s->arg[0] = 0;
            if (GRFuncTrace() == 2)
                DPRINTF(">>>>>>>> _grBufferSwap <<<<<<<<");
            s->perfs.stat();
            break;
	case FEnum_grLfbLock:
            s->datacb = ALIGNED(SIZE_GRLFBINFO);
            do {
                wrLfbInfo *hLfb = (wrLfbInfo *)PTR(outshm, ALIGNED(sizeof(wrgLfbInfo)));
                hLfb->size = sizeof(wrLfbInfo);
            } while(0);
	    s->parg[1] = VAL(PTR(outshm, ALIGNED(sizeof(wrgLfbInfo))));
            s->lfb_dirty = (s->lfbDev->grBuffer == s->arg[1])? (s->lfb_dirty | glide_lfbdirty()):0x01U;
            s->arg[1] = (s->lfb_noaux && (s->arg[1] & 0xFEU))? (s->arg[1] | 0x80U):s->arg[1];
	    break;
        case FEnum_grLfbUnlock:
            if ((s->lfb_real == 0) && (s->arg[0] & 0x01U)) {
                if (s->lfb_noaux && (s->arg[1] & 0xFEU)) { }
                else if (s->lfb_merge && (s->arg[1] == 1))
                    s->lfb_dirty = 0x80U;
                else {
                    vgLfbFlush(s);
                    s->lfb_dirty = 1;
                }
            }
            s->arg[1] = (s->lfb_noaux && (s->arg[1] & 0xFEU))? (s->arg[1] | 0x80U):s->arg[1];
            break;
	case FEnum_grLfbReadRegion:
	    s->parg[2] = VAL(s->hshm);
	    break;
        case FEnum_grLfbWriteRegion:
            s->datacb = ALIGNED(s->arg[5] * s->arg[6]);
            s->parg[3] = VAL(s->hshm);
            break;
        case FEnum_grLfbWriteRegion3x:
            s->datacb = ALIGNED(s->arg[5] * s->arg[7]);
            s->parg[0] = VAL(s->hshm);
            break;

	case FEnum_grFogTable:
            {
                uint32_t n = *(uint32_t *)PTR(s->hshm, 0);
                s->datacb = ALIGNED(sizeof(uint32_t)) + ALIGNED(n * sizeof(uint8_t));
            }
	    s->parg[0] = VAL(PTR(s->hshm, ALIGNED(sizeof(uint32_t))));
	    break;
	case FEnum_guFogGenerateExp:
	case FEnum_guFogGenerateExp2:
	case FEnum_guFogGenerateLinear:
	    s->parg[0] = VAL(outshm);
	    break;

	case FEnum_gu3dfGetInfo:
        case FEnum_gu3dfLoad:
            s->datacb = sizeof(char[64]);
	    s->GrTex.info3df = (wr3dfInfo *)PTR(outshm, ALIGNED(SIZE_GU3DFINFO));
            if (s->FEnum == FEnum_gu3dfGetInfo) {
                (s->GrTex.info3df)->data = 0;
                (s->GrTex.info3df)->mem_required = 0;
                if (s->GrTex.fbuf && s->GrTex.flen) {
                    int fd = open((char *)(s->hshm), O_BINARY | O_CREAT | O_WRONLY, 0666);
                    if (fd > 0) {
                        if (s->GrTex.flen == write(fd, s->GrTex.fbuf, s->GrTex.flen))
                            DPRINTF("Push texFile %s, size = %-8x", (char *)(s->hshm), s->GrTex.flen);
                        close(fd);
                    }
                    s->GrTex.flen = 0;
                }
            }
            if ((s->GrTex.info3df)->mem_required) {
                (s->GrTex.info3df)->data = s->GrTex.fbuf;
                s->GrTex.fbuf = 0;
            }
            s->parg[0] = VAL(s->hshm);
            s->parg[1] = VAL(s->GrTex.info3df);
            break;

        case FEnum_guTexDownloadMipMap:
            if (s->arg[3])
                s->datacb = ALIGNED(s->arg[3]);
            else
                DPRINTF("Invalid mmid %x", s->arg[0]);
            s->parg[1] = VAL(s->hshm);
            s->parg[2] = s->arg[2];
            if (s->arg[2]) {
                s->datacb += SIZE_GUNCCTABLE;
                s->parg[2] = VAL(PTR(s->hshm, ALIGNED(s->arg[3])));
            }
            break;
        case FEnum_guTexDownloadMipMapLevel:
            if (s->arg[3]) {
                s->datacb = ALIGNED(s->arg[3]);
                s->datacb += ALIGNED(1);
            }
            else
                DPRINTF("Invalid mmid %x", s->arg[0]);
            *(uintptr_t *)PTR(s->hshm, ALIGNED(s->arg[3])) = VAL(s->hshm);
            s->parg[2] = VAL(PTR(s->hshm, ALIGNED(s->arg[3])));
            break;

	case FEnum_grDrawPolygon:
	case FEnum_grAADrawPolygon:
	case FEnum_grDrawPlanarPolygon:
            {
                int i, v = 0, *ilist = (int *)PTR(s->hshm, 0);
                uint8_t *vlist = PTR(s->hshm, ALIGNED(s->arg[0] * sizeof(int)));
                for (i = 0; i < s->arg[0]; i++)
                    v = (ilist[i] > v)? ilist[i]:v;
                s->vtxCache = g_malloc((v + 1) * SIZE_GRVERTEX);
                for (i = 0; i < s->arg[0]; i++) {
                    memcpy(s->vtxCache + (ilist[i] * SIZE_GRVERTEX), vlist, SIZE_GRVERTEX);
                    vlist += ALIGNED(SIZE_GRVERTEX);
                }
            }
            s->datacb = ALIGNED(s->arg[0] * sizeof(int)) + (s->arg[0] * ALIGNED(SIZE_GRVERTEX));
	    s->parg[1] = VAL(PTR(s->hshm, 0));
	    s->parg[2] = VAL(s->vtxCache);
	    break;
	case FEnum_grDrawPolygonVertexList:
	case FEnum_grAADrawPolygonVertexList:
	case FEnum_grDrawPlanarPolygonVertexList:
	case FEnum_guDrawPolygonVertexListWithClip:
            s->datacb = ALIGNED(s->arg[0] * SIZE_GRVERTEX);
	    s->parg[1] = VAL(s->hshm);
	    break;

	case FEnum_guFbReadRegion:
	    if (s->lfbDev->emu211 == 1) {
		s->arg[4] = s->arg[3];
		s->arg[3] = s->arg[2];
		s->arg[2] = s->arg[1];
		s->arg[1] = s->arg[0];
		s->arg[0] = s->lfbDev->grBuffer;
		s->parg[2] = VAL(s->hshm);
		s->FEnum = FEnum_grLfbReadRegion;
	    }
	    else {
		s->parg[0] = VAL(s->hshm);
	    }
	    break;
	case FEnum_guFbWriteRegion:
	    if (s->lfbDev->emu211 == 1) {
                s->datacb = s->arg[3] * s->arg[5];
		s->arg[6] = s->arg[5];
		s->arg[5] = s->arg[3];
		s->arg[4] = s->arg[2];
		s->arg[3] = s->lfbDev->writeMode;
		s->arg[2] = s->arg[1];
		s->arg[1] = s->arg[0];
		s->arg[0] = s->lfbDev->grBuffer;
		s->parg[3] = VAL(s->hshm);
		s->FEnum = FEnum_grLfbWriteRegion;
	    }
	    else {
                s->datacb = s->arg[3] * s->arg[5];
		s->parg[0] = VAL(s->hshm);
	    }
	    break;

	case FEnum_grLfbOrigin:
	    s->lfbDev->origin = s->arg[0];
	    break;
	case FEnum_grLfbWriteMode:
	    s->lfbDev->writeMode = s->arg[0];
	    break;
	case FEnum_grLfbBypassMode:
	    s->lfbDev->byPass = s->arg[0];
	    break;
	case FEnum_grLfbGetReadPtr:
	case FEnum_grLfbGetWritePtr:
            s->lfbDev->grLock = (s->FEnum == FEnum_grLfbGetReadPtr)? 0:1;
	    if (s->lfbDev->emu211 == 1) {
		s->lfbDev->grBuffer = s->arg[0];
		s->FRet = (s->lfb_real)? s->lfbDev->guestLfb:0;
	    }
	    break;
	case FEnum_grLfbBegin:
	    if (s->lfbDev->emu211 == 1) {
		s->lfbDev->begin = 1;
		wrLfbInfo *info = (wrLfbInfo *)PTR(outshm, ALIGNED(sizeof(wrgLfbInfo)));
		s->arg[0] = s->lfbDev->grLock;
		s->arg[1] = s->lfbDev->grBuffer;
		s->arg[2] = s->lfbDev->writeMode;
		s->arg[3] = s->lfbDev->origin;
		s->arg[4] = (s->lfbDev->byPass)? 0:1;
		info->size = sizeof(wrLfbInfo);
		info->lfbPtr = 0;
		info->stride = 0;
		info->writeMode = s->lfbDev->writeMode;
		info->origin = s->lfbDev->origin;
		s->parg[1] = VAL(info);
		s->FEnum = FEnum_grLfbLock;
	    }
	    break;
	case FEnum_grLfbEnd:
	    if (s->lfbDev->emu211 == 1) {
		s->lfbDev->begin = 0;
		s->arg[0] = s->lfbDev->grLock;
		s->arg[1] = s->lfbDev->grBuffer;
		s->FEnum = FEnum_grLfbUnlock;
                if ((s->lfb_real == 0) && (s->arg[0] & 0x01U) && ((s->arg[1] & 0xFEU) == 0)) {
                    vgLfbFlush(s);
                    s->lfb_dirty = 1;
                }
	    }
	    break;
        case FEnum_grSstPassthruMode:
            if (s->lfbDev->emu211 == 1) {
                s->arg[0] = (s->arg[0] == GR_PASSTHRU_SHOW_VGA)?
                    GR_CONTROL_DEACTIVATE:GR_CONTROL_ACTIVATE;
                s->FEnum = FEnum_grSstControl;
            }
            else
                glide_renderer_stat(s->arg[0]);
            break;

        case FEnum_grLoadGammaTable:
            s->datacb = 3 * ALIGNED(s->arg[0] * sizeof(uint32_t));
            s->parg[1] = VAL(s->hshm);
            s->parg[2] = VAL(PTR(s->hshm,   ALIGNED(s->arg[0] * sizeof(uint32_t))));
            s->parg[3] = VAL(PTR(s->hshm, 2*ALIGNED(s->arg[0] * sizeof(uint32_t))));
            break;
        case FEnum_grGetGammaTableExt:
            s->parg[1] = VAL(outshm);
            s->parg[2] = VAL(PTR(outshm,   s->arg[0] * sizeof(uint32_t)));
            s->parg[3] = VAL(PTR(outshm, 2*s->arg[0] * sizeof(uint32_t)));
            break;
	case FEnum_grQueryResolutions:
            s->datacb = SIZE_GRRESOLUTION;
	    s->parg[0] = VAL(s->hshm);
	    s->parg[1] = 0;
	    if (s->arg[1]) {
		s->parg[1] = VAL(outshm);
	    }
	    break;
	case FEnum_grGet:
	    s->parg[2] = VAL(outshm);
	    break;
	case FEnum_grGlideSetVertexLayout:
	case FEnum_grGlideGetVertexLayout:
	    s->parg[0] = VAL(LookupVtxLayout(s->arg[0], s->szVtxLayout));
            break;
        case FEnum_grDrawVertexArray:
            s->datacb = s->arg[1] * ALIGNED(size_vertex3x());
            do {
                uint8_t **np = (uint8_t **)outshm;
                for (int i = 0; i < s->arg[1]; i++)
                    np[i] = PTR(s->hshm, i * ALIGNED(size_vertex3x()));
            } while(0);
            s->parg[2] = VAL(outshm);
            break;
        case FEnum_grDrawVertexArrayContiguous:
            s->datacb = ALIGNED(s->arg[1] * s->arg[3]);
            s->parg[2] = VAL(s->hshm);
            break;

        default:
            break;
    }
    for (int i = 0; i < 4; i++) {
        if (s->parg[i] & (sizeof(uintptr_t) - 1))
            DPRINTF("WARN: FEnum 0x%02X Unaligned parg[%d]", s->FEnum, i);
    }
}

static void processFRet(GlidePTState *s)
{
    uint8_t *outshm = s->fifo_ptr + (GRSHM_SIZE - PAGE_SIZE);

    switch (s->FEnum) {
	case FEnum_grDrawPolygon:
	case FEnum_grAADrawPolygon:
	case FEnum_grDrawPlanarPolygon:
            g_free(s->vtxCache);
            break;
        case FEnum_grGlideGetVersion:
            strncpy(s->version, (const char *)outshm, sizeof(char [80])-1);
            DPRINTF("grGlideGetVersion  %s", s->version);
            break;
        case FEnum_grDisable:
        case FEnum_grEnable:
            if (s->arg[0] == GR_PASSTHRU)
                glide_renderer_stat((s->FEnum == FEnum_grEnable)? 1:0);
            break;
        case FEnum_grSstControl:
        case FEnum_grSstPassthruMode:
            if (s->disp_cb.activate)
                glide_renderer_stat(s->arg[0] & 0x01U);
            break;
	case FEnum_grSstOpen:
            s->disp_cb.arg = s->arg;
            s->disp_cb.FEnum = s->FEnum;
            init_window(s->arg[0], s->version, &s->disp_cb);
            s->lfbDev->guestLfb = s->arg[6];
	    break;
	case FEnum_grSstWinOpen:
        case FEnum_grSstWinOpenExt:
            s->disp_cb.arg = s->arg;
            s->disp_cb.FEnum = s->FEnum;
	    if ((s->arg[1] & 0xFFU) > 0x0FU) {
                s->FRet = 0;
                DPRINTF("grSstWinOpen failed, res %d", s->arg[1]);
            }
            else {
                char strFpsLimit[sizeof(", FpsLimit [ ... FPS ]")];
                init_window(s->arg[1], s->version, &s->disp_cb);
                snprintf(strFpsLimit, sizeof(strFpsLimit), ", FpsLimit [ %d FPS ]", glide_fpslimit());
		s->lfbDev->origin = s->arg[4];
		s->lfbDev->guestLfb = (s->FEnum == FEnum_grSstWinOpenExt)? s->arg[8]:s->arg[7];
                s->GrRes = s->arg[1];
                s->lfb_real = glide_lfbmode();
                s->lfb_noaux = glide_lfbnoaux();
                s->lfb_merge = ((s->initDLL == 0x243a0) && (s->lfb_real == 0))? glide_lfbmerge():0;
                if (s->lfb_real == 0) {
                    s->lfb_dirty = 1;
                    glide_winres(s->arg[1], &s->lfb_w, &s->lfb_h);
                    s->lfb_h = (s->lfb_h > 0x300)? 0x300:s->lfb_h;
                    memset(s->glfb_ptr + (s->lfb_h * 0x800), 0, (s->lfb_h * 0x800));
                }
                DPRINTF("LFB mode is %s%s-copy%s%s%s%s", (s->lfb_real)? "MMIO Handlers (slow)" : "Shared Memory (fast)",
                        (s->lfb_real || glide_mapbufo(0, 0))? ", Zero":", One",
                        (glide_fpslimit())? strFpsLimit:"",
                        (glide_lfbdirty())? ", LfbLockDirty":"",
                        (s->lfb_noaux)? ", LfbNoAux":"", (s->lfb_merge)? ", LfbWriteMerge":"");
	    }
	    break;
	case FEnum_grSstWinClose:
        case FEnum_grSstWinClose3x:
            s->disp_cb.arg = s->arg;
            s->disp_cb.FEnum = s->FEnum;
	    fini_window(&s->disp_cb);
	    s->perfs.last();
	    DPRINTF("%-64s", "grSstWinClose called");
	    break;
	case FEnum_grGlideInit:
            s->szGrState = ALIGNED(SIZE_GRSTATE);
            s->szVtxLayout = ALIGNED(SIZE_GRVERTEX);
            DPRINTF("%sWRAPFX32", (char *)outshm);
            if (s->initDLL == 0x301a0) {
                init_g3ext();
                strncpy((char *)outshm, wrGetString(GR_EXTENSION), sizeof(char[192])-1);
                strncpy((char *)PTR(outshm, sizeof(char[192])), wrGetString(GR_HARDWARE), sizeof(char[16])-1);
                strncpy((char *)PTR(outshm, sizeof(char[208])), wrGetString(GR_VERSION), sizeof(char[32])-1);
                DPRINTF("\n  Extension: %s\n  Hardware: %s\n  Version: %s",
                        outshm, PTR(outshm, sizeof(char[192])), PTR(outshm, sizeof(char[208])));

            }
            glidestat(&s->perfs);
            memset(s->lfbDev->stride, 0, 2 * sizeof(uint32_t));
	    memset(s->lfbDev->lock, 0, 2 * sizeof(int));
	    s->lfbDev->lfbPtr[0] = 0;
            s->lfbDev->lfbPtr[1] = 0;
	    s->lfbDev->begin = 0;
            s->lfbDev->grBuffer = 1;
            s->lfbDev->grLock = 1;
	    s->lfbDev->guestLfb = 0;
            s->lfbDev->lfbMax = 0;
            s->fifoMax = 0; s->dataMax = 0;
	    break;
	case FEnum_grGlideShutdown:
            if (!s->perfs.last && !s->perfs.stat)
                break;
	    s->perfs.last();
	    /* TODO - Window management */
	    DPRINTF("grGlideShutdown called, fifo 0x%04x data 0x%04x shm 0x%07x lfb 0x%07x",
                    s->fifoMax, s->dataMax, (MAX_FIFO + s->dataMax) << 2, GLIDE_LFB_BASE + s->lfbDev->lfbMax);
            DPRINTF("  GrState %d VtxLayout %d", FreeGrState(), FreeVtxLayout());
	    memset(s->arg, 0, sizeof(uint32_t [16]));
	    strncpy(s->version, "Glide2x", sizeof(char [80])-1);
            if (s->cfgPushed) {
                s->cfgPushed = 0;
                DPRINTF("cfgFile remove() ret %d", remove("glide.cfg"));
            }
	    break;

        case FEnum_grBufferSwap:
            s->FRet = glide_fpslimit();
            break;
	case FEnum_grLfbLock:
	    if (s->lfbDev->lock[s->arg[0] & 0x1U] == 1) {
                if (s->lfbDev->grBuffer != s->arg[1]) {
                    DPRINTF("LFB lock contention, buffer %u <> %u, type %u <> %u",
                        s->lfbDev->grBuffer, s->arg[1], s->lfbDev->grLock, s->arg[0]);
                    DPRINTF("  lfbPtr %p <> %p", s->lfbDev->lfbPtr[s->lfbDev->grLock], ((wrLfbInfo *)PTR(outshm, ALIGNED(sizeof(wrgLfbInfo))))->lfbPtr);
                    DPRINTF("  stride %04x <> %04x", s->lfbDev->stride[s->lfbDev->grLock], ((wrLfbInfo *)PTR(outshm, ALIGNED(sizeof(wrgLfbInfo))))->stride);
                }
	    }
            //DPRINTF("LFB   locked, buffer %u type %u, dirty %02x", s->arg[1], s->arg[0] & 0x1U, s->lfb_dirty);
	    s->lfbDev->grLock = s->arg[0] & 0x1U;
	    s->lfbDev->grBuffer = s->arg[1];
	    s->lfbDev->lfbPtr[s->lfbDev->grLock] = ((wrLfbInfo *)PTR(outshm, ALIGNED(sizeof(wrgLfbInfo))))->lfbPtr;
	    s->lfbDev->stride[s->lfbDev->grLock] = ((wrLfbInfo *)PTR(outshm, ALIGNED(sizeof(wrgLfbInfo))))->stride;
	    s->lfbDev->lock[s->lfbDev->grLock] = 1;
            WARNONCE(((s->lfbDev->grBuffer < 2) && s->arg[2] && (s->arg[2] < 0xff)), "LFB writeMode not 565, %d", s->arg[2]);
            WARNONCE((s->lfbDev->grBuffer > 1), "Locked AUX/DEPTH buffer, buf %d lock %d writeMode %02x",
                    s->lfbDev->grBuffer, s->lfbDev->grLock, s->arg[2]);
	    if (s->lfbDev->emu211 == 0) {
		wrgLfbInfo *gLfbInfo = (wrgLfbInfo *)PTR(outshm, 0);
		gLfbInfo->lfbPtr = s->lfbDev->guestLfb;
		gLfbInfo->stride = s->lfbDev->stride[s->lfbDev->grLock];
		gLfbInfo->writeMode = ((wrLfbInfo *)PTR(outshm, ALIGNED(sizeof(wrgLfbInfo))))->writeMode;
		gLfbInfo->origin = ((wrLfbInfo *)PTR(outshm, ALIGNED(sizeof(wrgLfbInfo))))->origin;
		gLfbInfo->size = sizeof(wrgLfbInfo);
                s->lfbDev->writeMode = gLfbInfo->writeMode;
                s->lfbDev->origin = gLfbInfo->origin;
                if (s->lfb_noaux && (s->lfbDev->grBuffer & 0xFEU)) {
                    gLfbInfo->writeMode = s->arg[2];
                    gLfbInfo->origin = s->arg[3];
                }
                if ((s->lfbDev->grBuffer < 2) && (s->arg[2] < 0xff) && (s->arg[2] != gLfbInfo->writeMode))
                    DPRINTF("LFB writeMode mismatch, buf %d %x %x", s->lfbDev->grBuffer, s->arg[2], gLfbInfo->writeMode);
                if ((s->arg[3] < 0xff) && (s->arg[3] != gLfbInfo->origin))
                    DPRINTF("LFB origin mismatch, %x %x", s->arg[3], gLfbInfo->origin);
                if (s->lfb_real == 0) {
                    if ((s->lfb_noaux && (s->lfbDev->grBuffer & 0xFEU)) || !glide_mapbufo(0, 0))
                        gLfbInfo->lfbPtr = (s->lfbDev->grBuffer & 0xFEU)? (s->lfb_h * 0x800):0;
                    else
                        gLfbInfo->lfbPtr = (uint32_t)((uintptr_t)s->lfbDev->lfbPtr[s->lfbDev->grLock] & (MBUFO_SIZE - 1));
                }
	    }
            if (s->lfb_real == 0) {
                int mode = (1 << 4);
                uint8_t *gLfb = (s->lfbDev->grBuffer & 0xFEU)?
                    (s->glfb_ptr + (((s->lfb_h > 0x300)? 0x300:s->lfb_h) * 0x800)):s->glfb_ptr;
                if (s->lfbDev->grLock) {
                    s->lfbDev->mbufo[1].hva = (uintptr_t)s->lfbDev->lfbPtr[1];
                    s->lfbDev->mbufo[1].mapsz = s->lfb_h * s->lfbDev->stride[1];
                    s->lfbDev->mbufo[1].acc = 0;
                    if (s->lfb_noaux && (s->lfbDev->grBuffer & 0xFEU)) { }
                    else if ((s->lfbDev->emu211 == 0) && glide_mapbufo(0, 0)) {
                        if (s->lfbDev->mbufo[1].hva == s->lfbDev->mbufo[0].hva)
                            glide_mapbufo(&s->lfbDev->mbufo[0], 0);
                        glide_mapbufo(&s->lfbDev->mbufo[1], 1);
                        mode <<= 1;
                    }
                    else if (s->lfb_dirty & 0x01U) {
                        s->lfb_dirty = 0;
                        if ((s->lfbDev->writeMode & 0x0EU) != 0x04)
                            wrReadRegion(s->lfbDev->grBuffer, 0, 0, s->lfb_w, s->lfb_h, 0x800, (uintptr_t)gLfb);
                    }
                }
                else {
                    s->lfbDev->mbufo[0].hva = (uintptr_t)s->lfbDev->lfbPtr[0];
                    s->lfbDev->mbufo[0].mapsz = s->lfb_h * s->lfbDev->stride[0];
                    s->lfbDev->mbufo[0].acc = 1;
                    if (s->lfb_noaux && (s->lfbDev->grBuffer & 0xFEU)) { }
                    else if ((s->lfbDev->emu211 == 0) && glide_mapbufo(0, 0)) {
                        if (s->lfbDev->mbufo[0].hva == s->lfbDev->mbufo[1].hva)
                            glide_mapbufo(&s->lfbDev->mbufo[1], 0);
                        glide_mapbufo(&s->lfbDev->mbufo[0], 1);
                        mode <<= 1;
                    }
                    else if (s->lfb_dirty & 0x01U) {
                        uint8_t *hLfb = s->lfbDev->lfbPtr[0];
                        s->lfb_dirty = 0;
                        for (int y = 0; y < s->lfb_h; y++) {
                            memcpy(gLfb, hLfb, (s->lfb_w << 1));
                            hLfb += s->lfbDev->stride[0];
                            gLfb += 0x800;
                        }
                    }
                }
                s->FRet = (s->FRet)? (s->FRet | mode):s->FRet;
            }
            else {
                if (s->lfb_noaux && (s->lfbDev->grBuffer & 0xFEU))
                    s->FRet = (s->FRet)? (s->FRet | (1 << 4)):s->FRet;
            }
	    break;
	case FEnum_grLfbUnlock:
            glide_mapbufo(&s->lfbDev->mbufo[s->arg[0] & 0x01U], 0);
	    s->lfbDev->lock[s->arg[0] & 0x1U] = 0;
	    //DPRINTF("LFB unlocked, buffer %u type %u", s->arg[1], s->arg[0] & 0x1U);
	    break;

	case FEnum_gu3dfGetInfo:
        case FEnum_gu3dfLoad:
            if (s->FRet) {
                memcpy(outshm, (s->GrTex.info3df)->header, SIZE_GU3DFHEADER);
                ((wrg3dfInfo *)outshm)->mem_required = (s->GrTex.info3df)->mem_required;
                if (texTableValid(((wr3dfHeader *)(s->GrTex.info3df)->header)->format))
                    memcpy(PTR(outshm, SIZE_GU3DFHEADER), (s->GrTex.info3df)->table, SIZE_GUTEXTABLE);
                DPRINTF("%s texFile %s, mem_rq = %-8x", (s->FEnum == FEnum_gu3dfLoad)? "Load":"Info", (char *)(s->hshm),
                        ((wrg3dfInfo *)outshm)->mem_required);
            }
            break;

        case FEnum_grGet:
            if (s->FRet) {
                if (s->arg[0] == GR_GLIDE_STATE_SIZE)
                    s->szGrState = ALIGNED((*(uint32_t *)outshm));
                if (s->arg[0] == GR_GLIDE_VERTEXLAYOUT_SIZE)
                    s->szVtxLayout = ALIGNED((*(uint32_t *)outshm));
            }
            break;
        case FEnum_grReset:
            if (s->arg[0] == GR_VERTEX_PARAMETER)
                memset(vlut, 0, sizeof(vlut));
            break;
        case FEnum_grVertexLayout:
            vlut_vvars(s->arg[0], s->arg[1], s->arg[2]);
            break;

	case FEnum_grLfbBegin:
	    s->lfbDev->lock[s->lfbDev->grLock] = 1;
	    break;
	case FEnum_grLfbEnd:
	    s->lfbDev->lock[s->lfbDev->grLock] = 0;
	    break;
	case FEnum_grLfbGetReadPtr:
	case FEnum_grLfbGetWritePtr:
	    if (s->lfbDev->emu211 == 0) {
		s->lfbDev->lfbPtr[s->lfbDev->grLock] = (uint8_t *)(s->FRet);
		s->FRet = s->lfbDev->guestLfb;
	    }
	    if (s->arg[0] > 1) {
		DPRINTF("LFB pointer, buffer %u", s->arg[0]);
	    }
	    break;

        default:
            break;
    }
}

static void processFifo(GlidePTState *s)
{
    uint32_t *fifoptr = (uint32_t *)s->fifo_ptr;
    uint32_t *dataptr = (uint32_t *)(s->fifo_ptr + (MAX_FIFO << 2));
    int FEnum = s->FEnum, i = FIRST_FIFO, j = ALIGNED(1) >> 2;
    struct {
        uint32_t fifo;
        uint32_t data;
    } fifostat = { .fifo = 0, .data = 0 };

    if (fifoptr[0] - FIRST_FIFO) {
        fifostat.fifo = fifoptr[0];
        fifostat.data = dataptr[0];
#define DEBUG_FIFO 0
#if DEBUG_FIFO
        if (dataptr[0] >= MAX_DATA) {
            DPRINTF("  *WARN* Data bound overlapped 0x%02x dataptr %06X", s->FEnum, dataptr[0]);
        }
#endif
        while (i < fifoptr[0]) {
            int numArgs, numData;
            s->FEnum = fifoptr[i++];
            numArgs = GRFEnumArgsCnt(s->FEnum);
#if DEBUG_FIFO
            if (i == (FIRST_FIFO + 1))
                fprintf(stderr, "FIFO { [%02X] fifo %04x data %04x\n%02X ", FEnum, fifoptr[0], dataptr[0], s->FEnum);
            else
                fprintf(stderr, "%02X ", s->FEnum);
#endif
            s->datacb = 0;
            s->arg = &fifoptr[i];
            s->hshm = &dataptr[j];
            processArgs(s);
            doGlideFunc(s->FEnum, s->arg, s->parg, &s->FRet, s->lfbDev->emu211);
            processFRet(s);
            numData = (s->datacb & 0x03)? ((s->datacb >> 2) + 1):(s->datacb >> 2);
            i += numArgs;
            j += numData;
        }
#if DEBUG_FIFO
        if (i != FIRST_FIFO)
            fprintf(stderr, "\n} [%02X] fifo %04x data %d/%d\n", FEnum, i, j, dataptr[0]);
#endif
        s->fifoMax = (s->fifoMax < i)? i:s->fifoMax;
        fifoptr[0] = FIRST_FIFO;
        s->FEnum = FEnum;
    }
    if (GRFifoTrace()) {
        const char *fstr = getGRFuncStr(s->FEnum);
        if (fstr)
            DPRINTF("FIFO depth %s fifoptr %06x dataptr %06x", fstr, fifostat.fifo, fifostat.data);
    }

    s->datacb = 0;
    s->arg = &fifoptr[2];
    s->hshm = &dataptr[j];
    if (j > (ALIGNED(1) >> 2)){
        s->dataMax = (s->dataMax < dataptr[0])? dataptr[0]:s->dataMax;
        dataptr[0] -= j;
    }
}

static void glidept_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    COMMIT_SIGN;
    GlidePTState *s = opaque;

    switch (addr) {
	case 0xfb0:
            s->GrTex.fbuf = s->fifo_ptr + (GRSHM_SIZE - val);
            s->GrTex.flen = ((uint32_t *)s->GrTex.fbuf)[0];
            s->GrTex.fbuf += ALIGNED(1);
	    break;

	case 0xfbc:
            if ((val == 0xa0243) || (val == 0xa0211) || (val == 0xa0301)) {
                s->initDLL = 0;
                if (memcmp(s->glfb_ptr + SHLFB_SIZE - ALIGNBO(1), rev_, ALIGNED(1)))
                    break;
            }
	    if (val == 0xa0243) {
		strncpy(s->version, "Glide2x", sizeof(char [80])-1);
		if (init_glide2x("glide2x.dll") == 0) {
		    s->initDLL = 0x243a0;
		    s->lfbDev->v1Lfb = 0;
		    s->lfbDev->emu211 = 0;
		    DPRINTF("DLL loaded - glide2x.dll");
		}
	    }
            else if (val == 0xa0211) {
		strncpy(s->version, "Glide", sizeof(char [80])-1);
		if (init_glide2x("glide.dll") == 0) {
		    s->initDLL = 0x211a0;
                    s->lfb_real = 1;
		    s->lfbDev->v1Lfb = 1;
		    s->lfbDev->emu211 = 0;
		    DPRINTF("DLL loaded - glide.dll");
		}
		else if (init_glide2x("glide2x.dll") == 0) {
		    s->initDLL = 0x211a0;
		    s->lfbDev->v1Lfb = 1;
		    s->lfbDev->emu211 = 1;
		    DPRINTF("DLL loaded - glide2x.dll, emulating API 2.11");
		}
	    }
            else if (val == 0xa0301) {
                strncpy(s->version, "Glide3x", sizeof(char [80])-1);
                if (init_glide2x("glide3x.dll") == 0) {
                    s->initDLL = 0x301a0;
                    s->lfbDev->v1Lfb = 0;
                    s->lfbDev->emu211 = 0;
                    DPRINTF("DLL loaded - glide3x.dll");
                }
            }
	    if ((val == 0xd0243) || (val == 0xd0211) || (val == 0xd0301)) {
                if (s->initDLL) {
                    s->initDLL = 0;
                    s->disp_cb.FEnum = 0;
                    fini_window(&s->disp_cb);
                    fini_glide2x();
                    memset(s->version, 0, sizeof(char [80]));
                    DPRINTF("DLL unloaded");
                }
	    }
	    break;
		
        case 0xfc0:
            s->FEnum = val;
            processFifo(s);
            processArgs(s);
            doGlideFunc(s->FEnum, s->arg, s->parg, &s->FRet, s->lfbDev->emu211);
            processFRet(s);
            do {
                uint32_t *dataptr = (uint32_t *)(s->fifo_ptr + (MAX_FIFO << 2));
                uint32_t numData = (s->datacb & 0x03)? ((s->datacb >> 2) + 1):(s->datacb >> 2);
                dataptr[0] -= numData;
                if (dataptr[0] > (ALIGNED(1) >> 2))
                    DPRINTF("WARN: FIFO data leak 0x%02x %d", s->FEnum, dataptr[0]);
                dataptr[0] = ALIGNED(1) >> 2;
            } while (0);
            break;

        default:
            break;
    }
}

static hwaddr translateLfb(const hwaddr offset_in, const int stride)
{
    uint32_t x, y;
    hwaddr offset_out;
    y = offset_in / 0x800;
    x = offset_in - (y * 0x800);
    offset_out = x + (y * stride);

    return offset_out;
}

static uint64_t glideLfb_read(void *opaque, hwaddr addr, unsigned size)
{
    GlideLfbState *s = opaque;
    s->lfbMax = (s->lfbMax < addr)? addr:s->lfbMax;
    uint32_t val = 0;

    if (s->lfbPtr[0]) {
	if (!s->v1Lfb && !s->lock[0]) {
	    DPRINTF("LFB read without lock!");
	}

	if (s->emu211)
	    addr = translateLfb(addr, s->stride[0]);

	switch (size) {
	    case 2:
		val = *(uint16_t *)(s->lfbPtr[0] + addr);
		break;
	    case 4:
		val = *(uint32_t *)(s->lfbPtr[0] + addr);
		break;
	    case 8:
		val = *(uint64_t *)(s->lfbPtr[0] + addr);
		break;
	    default:
		DPRINTF("WARN: Unsupported LFB read size");
		break;
	}
    }
    return val;
}

static void glideLfb_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    GlideLfbState *s = opaque;
    s->lfbMax = (s->lfbMax < addr)? addr:s->lfbMax;

    if (s->lfbPtr[1]) {
	if (!s->v1Lfb && !s->lock[1] && false) {
	   DPRINTF("LFB write without lock!");
	}

	if (s->emu211)
	    addr = translateLfb(addr, s->stride[1]);

	switch (size) {
	    case 2:
		*(uint16_t *)(s->lfbPtr[1] + addr) = (uint16_t)val;
		break;
	    case 4:
		*(uint32_t *)(s->lfbPtr[1] + addr) = (uint32_t)val;
		break;
	    case 8:
		*(uint64_t *)(s->lfbPtr[1] + addr) = val;
		break;
	    default:
		DPRINTF("WARN: Unsupported LFB write size");
		break;
	}
    }
}

static const MemoryRegionOps glideLfb_ops = {
    .read	= glideLfb_read,
    .write	= glideLfb_write,
    .valid = {
	.min_access_size = 2,
	.max_access_size = 8,
    },
    .impl = {
	.min_access_size = 2,
	.max_access_size = 8,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void glidelfb_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    GlideLfbState *s = GLIDELFB(obj);

    memory_region_init_io(&s->iomem, obj, &glideLfb_ops, s, TYPE_GLIDELFB, GRLFB_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const MemoryRegionOps glidept_ops = {
    .read       = glidept_read,
    .write      = glidept_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void glidelfb_reset(DeviceState *d)
{
//    GlideLfbState *s = GLIDELFB(d);
}

static void glidept_reset(DeviceState *d)
{
//    GlidePTState *s = GLIDEPT(d);
}

static void glidept_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    GlidePTState *s = GLIDEPT(obj);
    MemoryRegion *sysmem = get_system_memory();

    memory_region_init_ram(&s->glfb_ram, NULL, "grlfbshm", SHLFB_SIZE, &error_fatal);
    memory_region_init_ram(&s->fifo_ram, NULL, "glideshm", GRSHM_SIZE, &error_fatal);
    s->glfb_ptr = memory_region_get_ram_ptr(&s->glfb_ram);
    s->fifo_ptr = memory_region_get_ram_ptr(&s->fifo_ram);
    memory_region_add_subregion(sysmem, (GLIDE_LFB_BASE + GRLFB_SIZE), &s->glfb_ram);
    memory_region_add_subregion(sysmem, GLIDE_FIFO_BASE, &s->fifo_ram);

    memory_region_init_io(&s->iomem, obj, &glidept_ops, s, TYPE_GLIDEPT, PAGE_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void glidept_realize(DeviceState *dev, Error **errp)
{
    GlidePTState *s = GLIDEPT(dev);
    DeviceState *lfb = NULL;

    lfb = qdev_new(TYPE_GLIDELFB);
    sysbus_realize(SYS_BUS_DEVICE(lfb), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(lfb), 0, GLIDE_LFB_BASE);

    s->lfbDev = GLIDELFB(lfb);
    s->initDLL = 0;
}

static void glidept_finalize(Object *obj)
{
  //  GlidePTState *s = GLIDEPT(obj);
}

static void glidelfb_realize(DeviceState *dev, Error **errp)
{
    GlideLfbState *s = GLIDELFB(dev);
    s->guestLfb = 0;
    s->lfbPtr[0] = 0;
    s->lfbPtr[1] = 0;
    memset(s->stride, 0, 2 * sizeof(uint32_t));
    memset(s->lock, 0, 2 * sizeof(int));
}
static void glidelfb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = glidelfb_realize;
    device_class_set_legacy_reset(dc, glidelfb_reset);
}

static void glidept_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = glidept_realize;
    device_class_set_legacy_reset(dc, glidept_reset);
}

static const TypeInfo glidelfb_info = {
    .name	= TYPE_GLIDELFB,
    .parent	= TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GlideLfbState),
    .instance_init = glidelfb_init,
    .class_init = glidelfb_class_init,
};

static const TypeInfo glidept_info = {
    .name       = TYPE_GLIDEPT,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GlidePTState),
    .instance_init = glidept_init,
    .instance_finalize = glidept_finalize,
    .class_init = glidept_class_init,
};

static void glidept_register_type(void)
{
    type_register_static(&glidelfb_info);
    type_register_static(&glidept_info);
}

type_init(glidept_register_type)
