/*
 * QEMU TCX Frame buffer
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

#define TCX_ROM_FILE "QEMU,tcx.bin"
#define FCODE_MAX_ROM_SIZE 0x10000

#define MAXX 1024
#define MAXY 768
#define TCX_DAC_NREGS    16
#define TCX_THC_NREGS    0x1000
#define TCX_DHC_NREGS    0x4000
#define TCX_TEC_NREGS    0x1000
#define TCX_ALT_NREGS    0x8000
#define TCX_STIP_NREGS   0x800000
#define TCX_BLIT_NREGS   0x800000
#define TCX_RSTIP_NREGS  0x800000
#define TCX_RBLIT_NREGS  0x800000

#define TCX_THC_MISC     0x818
#define TCX_THC_CURSXY   0x8fc
#define TCX_THC_CURSMASK 0x900
#define TCX_THC_CURSBITS 0x980

#define TYPE_TCX "SUNW,tcx"
#define TCX(obj) OBJECT_CHECK(TCXState, (obj), TYPE_TCX)

typedef struct TCXState {
    SysBusDevice parent_obj;

    QemuConsole *con;
    qemu_irq irq;
    uint8_t *vram;
    uint32_t *vram24, *cplane;
    hwaddr prom_addr;
    MemoryRegion rom;
    MemoryRegion vram_mem;
    MemoryRegion vram_8bit;
    MemoryRegion vram_24bit;
    MemoryRegion stip;
    MemoryRegion blit;
    MemoryRegion vram_cplane;
    MemoryRegion rstip;
    MemoryRegion rblit;
    MemoryRegion tec;
    MemoryRegion dac;
    MemoryRegion thc;
    MemoryRegion dhc;
    MemoryRegion alt;
    MemoryRegion thc24;

    ram_addr_t vram24_offset, cplane_offset;
    uint32_t tmpblit;
    uint32_t vram_size;
    uint32_t palette[260];
    uint8_t r[260], g[260], b[260];
    uint16_t width, height, depth;
    uint8_t dac_index, dac_state;
    uint32_t thcmisc;
    uint32_t cursmask[32];
    uint32_t cursbits[32];
    uint16_t cursx;
    uint16_t cursy;
} TCXState;

static void tcx_set_dirty(TCXState *s, ram_addr_t addr, int len)
{
    memory_region_set_dirty(&s->vram_mem, addr, len);

    if (s->depth == 24) {
        memory_region_set_dirty(&s->vram_mem, s->vram24_offset + addr * 4,
                                len * 4);
        memory_region_set_dirty(&s->vram_mem, s->cplane_offset + addr * 4,
                                len * 4);
    }
}

static int tcx_check_dirty(TCXState *s, DirtyBitmapSnapshot *snap,
                           ram_addr_t addr, int len)
{
    int ret;

    ret = memory_region_snapshot_get_dirty(&s->vram_mem, snap, addr, len);

    if (s->depth == 24) {
        ret |= memory_region_snapshot_get_dirty(&s->vram_mem, snap,
                                       s->vram24_offset + addr * 4, len * 4);
        ret |= memory_region_snapshot_get_dirty(&s->vram_mem, snap,
                                       s->cplane_offset + addr * 4, len * 4);
    }

    return ret;
}

static void update_palette_entries(TCXState *s, int start, int end)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int i;

    for (i = start; i < end; i++) {
        if (is_surface_bgr(surface)) {
            s->palette[i] = rgb_to_pixel32bgr(s->r[i], s->g[i], s->b[i]);
        } else {
            s->palette[i] = rgb_to_pixel32(s->r[i], s->g[i], s->b[i]);
        }
    }
    tcx_set_dirty(s, 0, memory_region_size(&s->vram_mem));
}

static void tcx_draw_line32(TCXState *s1, uint8_t *d,
                            const uint8_t *s, int width)
{
    int x;
    uint8_t val;
    uint32_t *p = (uint32_t *)d;

    for (x = 0; x < width; x++) {
        val = *s++;
        *p++ = s1->palette[val];
    }
}

static void tcx_draw_cursor32(TCXState *s1, uint8_t *d,
                              int y, int width)
{
    int x, len;
    uint32_t mask, bits;
    uint32_t *p = (uint32_t *)d;

    y = y - s1->cursy;
    mask = s1->cursmask[y];
    bits = s1->cursbits[y];
    len = MIN(width - s1->cursx, 32);
    p = &p[s1->cursx];
    for (x = 0; x < len; x++) {
        if (mask & 0x80000000) {
            if (bits & 0x80000000) {
                *p = s1->palette[259];
            } else {
                *p = s1->palette[258];
            }
        }
        p++;
        mask <<= 1;
        bits <<= 1;
    }
}

/*
  XXX Could be much more optimal:
  * detect if line/page/whole screen is in 24 bit mode
  * if destination is also BGR, use memcpy
  */
static inline void tcx24_draw_line32(TCXState *s1, uint8_t *d,
                                     const uint8_t *s, int width,
                                     const uint32_t *cplane,
                                     const uint32_t *s24)
{
    DisplaySurface *surface = qemu_console_surface(s1->con);
    int x, bgr, r, g, b;
    uint8_t val, *p8;
    uint32_t *p = (uint32_t *)d;
    uint32_t dval;
    bgr = is_surface_bgr(surface);
    for(x = 0; x < width; x++, s++, s24++) {
        if (be32_to_cpu(*cplane) & 0x03000000) {
            /* 24-bit direct, BGR order */
            p8 = (uint8_t *)s24;
            p8++;
            b = *p8++;
            g = *p8++;
            r = *p8;
            if (bgr)
                dval = rgb_to_pixel32bgr(r, g, b);
            else
                dval = rgb_to_pixel32(r, g, b);
        } else {
            /* 8-bit pseudocolor */
            val = *s;
            dval = s1->palette[val];
        }
        *p++ = dval;
        cplane++;
    }
}

/* Fixed line length 1024 allows us to do nice tricks not possible on
   VGA... */

static void tcx_update_display(void *opaque)
{
    TCXState *ts = opaque;
    DisplaySurface *surface = qemu_console_surface(ts->con);
    ram_addr_t page;
    DirtyBitmapSnapshot *snap = NULL;
    int y, y_start, dd, ds;
    uint8_t *d, *s;

    if (surface_bits_per_pixel(surface) != 32) {
        return;
    }

    page = 0;
    y_start = -1;
    d = surface_data(surface);
    s = ts->vram;
    dd = surface_stride(surface);
    ds = 1024;

    snap = memory_region_snapshot_and_clear_dirty(&ts->vram_mem, 0x0,
                                             memory_region_size(&ts->vram_mem),
                                             DIRTY_MEMORY_VGA);

    for (y = 0; y < ts->height; y++, page += ds) {
        if (tcx_check_dirty(ts, snap, page, ds)) {
            if (y_start < 0)
                y_start = y;

            tcx_draw_line32(ts, d, s, ts->width);
            if (y >= ts->cursy && y < ts->cursy + 32 && ts->cursx < ts->width) {
                tcx_draw_cursor32(ts, d, y, ts->width);
            }
        } else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_gfx_update(ts->con, 0, y_start,
                               ts->width, y - y_start);
                y_start = -1;
            }
        }
        s += ds;
        d += dd;
    }
    if (y_start >= 0) {
        /* flush to display */
        dpy_gfx_update(ts->con, 0, y_start,
                       ts->width, y - y_start);
    }
    g_free(snap);
}

static void tcx24_update_display(void *opaque)
{
    TCXState *ts = opaque;
    DisplaySurface *surface = qemu_console_surface(ts->con);
    ram_addr_t page;
    DirtyBitmapSnapshot *snap = NULL;
    int y, y_start, dd, ds;
    uint8_t *d, *s;
    uint32_t *cptr, *s24;

    if (surface_bits_per_pixel(surface) != 32) {
            return;
    }

    page = 0;
    y_start = -1;
    d = surface_data(surface);
    s = ts->vram;
    s24 = ts->vram24;
    cptr = ts->cplane;
    dd = surface_stride(surface);
    ds = 1024;

    snap = memory_region_snapshot_and_clear_dirty(&ts->vram_mem, 0x0,
                                             memory_region_size(&ts->vram_mem),
                                             DIRTY_MEMORY_VGA);

    for (y = 0; y < ts->height; y++, page += ds) {
        if (tcx_check_dirty(ts, snap, page, ds)) {
            if (y_start < 0)
                y_start = y;

            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            if (y >= ts->cursy && y < ts->cursy+32 && ts->cursx < ts->width) {
                tcx_draw_cursor32(ts, d, y, ts->width);
            }
        } else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_gfx_update(ts->con, 0, y_start,
                               ts->width, y - y_start);
                y_start = -1;
            }
        }
        d += dd;
        s += ds;
        cptr += ds;
        s24 += ds;
    }
    if (y_start >= 0) {
        /* flush to display */
        dpy_gfx_update(ts->con, 0, y_start,
                       ts->width, y - y_start);
    }
    g_free(snap);
}

static void tcx_invalidate_display(void *opaque)
{
    TCXState *s = opaque;

    tcx_set_dirty(s, 0, memory_region_size(&s->vram_mem));
    qemu_console_resize(s->con, s->width, s->height);
}

static void tcx24_invalidate_display(void *opaque)
{
    TCXState *s = opaque;

    tcx_set_dirty(s, 0, memory_region_size(&s->vram_mem));
    qemu_console_resize(s->con, s->width, s->height);
}

static int vmstate_tcx_post_load(void *opaque, int version_id)
{
    TCXState *s = opaque;

    update_palette_entries(s, 0, 256);
    tcx_set_dirty(s, 0, memory_region_size(&s->vram_mem));
    return 0;
}

static const VMStateDescription vmstate_tcx = {
    .name ="tcx",
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = vmstate_tcx_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(height, TCXState),
        VMSTATE_UINT16(width, TCXState),
        VMSTATE_UINT16(depth, TCXState),
        VMSTATE_BUFFER(r, TCXState),
        VMSTATE_BUFFER(g, TCXState),
        VMSTATE_BUFFER(b, TCXState),
        VMSTATE_UINT8(dac_index, TCXState),
        VMSTATE_UINT8(dac_state, TCXState),
        VMSTATE_END_OF_LIST()
    }
};

static void tcx_reset(DeviceState *d)
{
    TCXState *s = TCX(d);

    /* Initialize palette */
    memset(s->r, 0, 260);
    memset(s->g, 0, 260);
    memset(s->b, 0, 260);
    s->r[255] = s->g[255] = s->b[255] = 255;
    s->r[256] = s->g[256] = s->b[256] = 255;
    s->r[258] = s->g[258] = s->b[258] = 255;
    update_palette_entries(s, 0, 260);
    memset(s->vram, 0, MAXX*MAXY);
    memory_region_reset_dirty(&s->vram_mem, 0, MAXX * MAXY * (1 + 4 + 4),
                              DIRTY_MEMORY_VGA);
    s->dac_index = 0;
    s->dac_state = 0;
    s->cursx = 0xf000; /* Put cursor off screen */
    s->cursy = 0xf000;
}

static uint64_t tcx_dac_readl(void *opaque, hwaddr addr,
                              unsigned size)
{
    TCXState *s = opaque;
    uint32_t val = 0;

    switch (s->dac_state) {
    case 0:
        val = s->r[s->dac_index] << 24;
        s->dac_state++;
        break;
    case 1:
        val = s->g[s->dac_index] << 24;
        s->dac_state++;
        break;
    case 2:
        val = s->b[s->dac_index] << 24;
        s->dac_index = (s->dac_index + 1) & 0xff; /* Index autoincrement */
    default:
        s->dac_state = 0;
        break;
    }

    return val;
}

static void tcx_dac_writel(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    TCXState *s = opaque;
    unsigned index;

    switch (addr) {
    case 0: /* Address */
        s->dac_index = val >> 24;
        s->dac_state = 0;
        break;
    case 4:  /* Pixel colours */
    case 12: /* Overlay (cursor) colours */
        if (addr & 8) {
            index = (s->dac_index & 3) + 256;
        } else {
            index = s->dac_index;
        }
        switch (s->dac_state) {
        case 0:
            s->r[index] = val >> 24;
            update_palette_entries(s, index, index + 1);
            s->dac_state++;
            break;
        case 1:
            s->g[index] = val >> 24;
            update_palette_entries(s, index, index + 1);
            s->dac_state++;
            break;
        case 2:
            s->b[index] = val >> 24;
            update_palette_entries(s, index, index + 1);
            s->dac_index = (s->dac_index + 1) & 0xff; /* Index autoincrement */
        default:
            s->dac_state = 0;
            break;
        }
        break;
    default: /* Control registers */
        break;
    }
}

static const MemoryRegionOps tcx_dac_ops = {
    .read = tcx_dac_readl,
    .write = tcx_dac_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t tcx_stip_readl(void *opaque, hwaddr addr,
                               unsigned size)
{
    return 0;
}

static void tcx_stip_writel(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    TCXState *s = opaque;
    int i;
    uint32_t col;

    if (!(addr & 4)) {
        s->tmpblit = val;
    } else {
        addr = (addr >> 3) & 0xfffff;
        col = cpu_to_be32(s->tmpblit);
        if (s->depth == 24) {
            for (i = 0; i < 32; i++)  {
                if (val & 0x80000000) {
                    s->vram[addr + i] = s->tmpblit;
                    s->vram24[addr + i] = col;
                }
                val <<= 1;
            }
        } else {
            for (i = 0; i < 32; i++)  {
                if (val & 0x80000000) {
                    s->vram[addr + i] = s->tmpblit;
                }
                val <<= 1;
            }
        }
        tcx_set_dirty(s, addr, 32);
    }
}

static void tcx_rstip_writel(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    TCXState *s = opaque;
    int i;
    uint32_t col;

    if (!(addr & 4)) {
        s->tmpblit = val;
    } else {
        addr = (addr >> 3) & 0xfffff;
        col = cpu_to_be32(s->tmpblit);
        if (s->depth == 24) {
            for (i = 0; i < 32; i++) {
                if (val & 0x80000000) {
                    s->vram[addr + i] = s->tmpblit;
                    s->vram24[addr + i] = col;
                    s->cplane[addr + i] = col;
                }
                val <<= 1;
            }
        } else {
            for (i = 0; i < 32; i++)  {
                if (val & 0x80000000) {
                    s->vram[addr + i] = s->tmpblit;
                }
                val <<= 1;
            }
        }
        tcx_set_dirty(s, addr, 32);
    }
}

static const MemoryRegionOps tcx_stip_ops = {
    .read = tcx_stip_readl,
    .write = tcx_stip_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps tcx_rstip_ops = {
    .read = tcx_stip_readl,
    .write = tcx_rstip_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t tcx_blit_readl(void *opaque, hwaddr addr,
                               unsigned size)
{
    return 0;
}

static void tcx_blit_writel(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    TCXState *s = opaque;
    uint32_t adsr, len;
    int i;

    if (!(addr & 4)) {
        s->tmpblit = val;
    } else {
        addr = (addr >> 3) & 0xfffff;
        adsr = val & 0xffffff;
        len = ((val >> 24) & 0x1f) + 1;
        if (adsr == 0xffffff) {
            memset(&s->vram[addr], s->tmpblit, len);
            if (s->depth == 24) {
                val = s->tmpblit & 0xffffff;
                val = cpu_to_be32(val);
                for (i = 0; i < len; i++) {
                    s->vram24[addr + i] = val;
                }
            }
        } else {
            memcpy(&s->vram[addr], &s->vram[adsr], len);
            if (s->depth == 24) {
                memcpy(&s->vram24[addr], &s->vram24[adsr], len * 4);
            }
        }
        tcx_set_dirty(s, addr, len);
    }
}

static void tcx_rblit_writel(void *opaque, hwaddr addr,
                         uint64_t val, unsigned size)
{
    TCXState *s = opaque;
    uint32_t adsr, len;
    int i;

    if (!(addr & 4)) {
        s->tmpblit = val;
    } else {
        addr = (addr >> 3) & 0xfffff;
        adsr = val & 0xffffff;
        len = ((val >> 24) & 0x1f) + 1;
        if (adsr == 0xffffff) {
            memset(&s->vram[addr], s->tmpblit, len);
            if (s->depth == 24) {
                val = s->tmpblit & 0xffffff;
                val = cpu_to_be32(val);
                for (i = 0; i < len; i++) {
                    s->vram24[addr + i] = val;
                    s->cplane[addr + i] = val;
                }
            }
        } else {
            memcpy(&s->vram[addr], &s->vram[adsr], len);
            if (s->depth == 24) {
                memcpy(&s->vram24[addr], &s->vram24[adsr], len * 4);
                memcpy(&s->cplane[addr], &s->cplane[adsr], len * 4);
            }
        }
        tcx_set_dirty(s, addr, len);
    }
}

static const MemoryRegionOps tcx_blit_ops = {
    .read = tcx_blit_readl,
    .write = tcx_blit_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps tcx_rblit_ops = {
    .read = tcx_blit_readl,
    .write = tcx_rblit_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void tcx_invalidate_cursor_position(TCXState *s)
{
    int ymin, ymax, start, end;

    /* invalidate only near the cursor */
    ymin = s->cursy;
    if (ymin >= s->height) {
        return;
    }
    ymax = MIN(s->height, ymin + 32);
    start = ymin * 1024;
    end   = ymax * 1024;

    tcx_set_dirty(s, start, end - start);
}

static uint64_t tcx_thc_readl(void *opaque, hwaddr addr,
                            unsigned size)
{
    TCXState *s = opaque;
    uint64_t val;

    if (addr == TCX_THC_MISC) {
        val = s->thcmisc | 0x02000000;
    } else {
        val = 0;
    }
    return val;
}

static void tcx_thc_writel(void *opaque, hwaddr addr,
                         uint64_t val, unsigned size)
{
    TCXState *s = opaque;

    if (addr == TCX_THC_CURSXY) {
        tcx_invalidate_cursor_position(s);
        s->cursx = val >> 16;
        s->cursy = val;
        tcx_invalidate_cursor_position(s);
    } else if (addr >= TCX_THC_CURSMASK && addr < TCX_THC_CURSMASK + 128) {
        s->cursmask[(addr - TCX_THC_CURSMASK) >> 2] = val;
        tcx_invalidate_cursor_position(s);
    } else if (addr >= TCX_THC_CURSBITS && addr < TCX_THC_CURSBITS + 128) {
        s->cursbits[(addr - TCX_THC_CURSBITS) >> 2] = val;
        tcx_invalidate_cursor_position(s);
    } else if (addr == TCX_THC_MISC) {
        s->thcmisc = val;
    }

}

static const MemoryRegionOps tcx_thc_ops = {
    .read = tcx_thc_readl,
    .write = tcx_thc_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t tcx_dummy_readl(void *opaque, hwaddr addr,
                            unsigned size)
{
    return 0;
}

static void tcx_dummy_writel(void *opaque, hwaddr addr,
                         uint64_t val, unsigned size)
{
    return;
}

static const MemoryRegionOps tcx_dummy_ops = {
    .read = tcx_dummy_readl,
    .write = tcx_dummy_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const GraphicHwOps tcx_ops = {
    .invalidate = tcx_invalidate_display,
    .gfx_update = tcx_update_display,
};

static const GraphicHwOps tcx24_ops = {
    .invalidate = tcx24_invalidate_display,
    .gfx_update = tcx24_update_display,
};

static void tcx_initfn(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    TCXState *s = TCX(obj);

    memory_region_init_ram_nomigrate(&s->rom, obj, "tcx.prom", FCODE_MAX_ROM_SIZE,
                           &error_fatal);
    memory_region_set_readonly(&s->rom, true);
    sysbus_init_mmio(sbd, &s->rom);

    /* 2/STIP : Stippler */
    memory_region_init_io(&s->stip, obj, &tcx_stip_ops, s, "tcx.stip",
                          TCX_STIP_NREGS);
    sysbus_init_mmio(sbd, &s->stip);

    /* 3/BLIT : Blitter */
    memory_region_init_io(&s->blit, obj, &tcx_blit_ops, s, "tcx.blit",
                          TCX_BLIT_NREGS);
    sysbus_init_mmio(sbd, &s->blit);

    /* 5/RSTIP : Raw Stippler */
    memory_region_init_io(&s->rstip, obj, &tcx_rstip_ops, s, "tcx.rstip",
                          TCX_RSTIP_NREGS);
    sysbus_init_mmio(sbd, &s->rstip);

    /* 6/RBLIT : Raw Blitter */
    memory_region_init_io(&s->rblit, obj, &tcx_rblit_ops, s, "tcx.rblit",
                          TCX_RBLIT_NREGS);
    sysbus_init_mmio(sbd, &s->rblit);

    /* 7/TEC : ??? */
    memory_region_init_io(&s->tec, obj, &tcx_dummy_ops, s, "tcx.tec",
                          TCX_TEC_NREGS);
    sysbus_init_mmio(sbd, &s->tec);

    /* 8/CMAP : DAC */
    memory_region_init_io(&s->dac, obj, &tcx_dac_ops, s, "tcx.dac",
                          TCX_DAC_NREGS);
    sysbus_init_mmio(sbd, &s->dac);

    /* 9/THC : Cursor */
    memory_region_init_io(&s->thc, obj, &tcx_thc_ops, s, "tcx.thc",
                          TCX_THC_NREGS);
    sysbus_init_mmio(sbd, &s->thc);

    /* 11/DHC : ??? */
    memory_region_init_io(&s->dhc, obj, &tcx_dummy_ops, s, "tcx.dhc",
                          TCX_DHC_NREGS);
    sysbus_init_mmio(sbd, &s->dhc);

    /* 12/ALT : ??? */
    memory_region_init_io(&s->alt, obj, &tcx_dummy_ops, s, "tcx.alt",
                          TCX_ALT_NREGS);
    sysbus_init_mmio(sbd, &s->alt);
}

static void tcx_realizefn(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    TCXState *s = TCX(dev);
    ram_addr_t vram_offset = 0;
    int size, ret;
    uint8_t *vram_base;
    char *fcode_filename;

    memory_region_init_ram_nomigrate(&s->vram_mem, OBJECT(s), "tcx.vram",
                           s->vram_size * (1 + 4 + 4), &error_fatal);
    vmstate_register_ram_global(&s->vram_mem);
    memory_region_set_log(&s->vram_mem, true, DIRTY_MEMORY_VGA);
    vram_base = memory_region_get_ram_ptr(&s->vram_mem);

    /* 10/ROM : FCode ROM */
    vmstate_register_ram_global(&s->rom);
    fcode_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, TCX_ROM_FILE);
    if (fcode_filename) {
        ret = load_image_mr(fcode_filename, &s->rom);
        g_free(fcode_filename);
        if (ret < 0 || ret > FCODE_MAX_ROM_SIZE) {
            warn_report("tcx: could not load prom '%s'", TCX_ROM_FILE);
        }
    }

    /* 0/DFB8 : 8-bit plane */
    s->vram = vram_base;
    size = s->vram_size;
    memory_region_init_alias(&s->vram_8bit, OBJECT(s), "tcx.vram.8bit",
                             &s->vram_mem, vram_offset, size);
    sysbus_init_mmio(sbd, &s->vram_8bit);
    vram_offset += size;
    vram_base += size;

    /* 1/DFB24 : 24bit plane */
    size = s->vram_size * 4;
    s->vram24 = (uint32_t *)vram_base;
    s->vram24_offset = vram_offset;
    memory_region_init_alias(&s->vram_24bit, OBJECT(s), "tcx.vram.24bit",
                             &s->vram_mem, vram_offset, size);
    sysbus_init_mmio(sbd, &s->vram_24bit);
    vram_offset += size;
    vram_base += size;

    /* 4/RDFB32 : Raw Framebuffer */
    size = s->vram_size * 4;
    s->cplane = (uint32_t *)vram_base;
    s->cplane_offset = vram_offset;
    memory_region_init_alias(&s->vram_cplane, OBJECT(s), "tcx.vram.cplane",
                             &s->vram_mem, vram_offset, size);
    sysbus_init_mmio(sbd, &s->vram_cplane);

    /* 9/THC24bits : NetBSD writes here even with 8-bit display: dummy */
    if (s->depth == 8) {
        memory_region_init_io(&s->thc24, OBJECT(s), &tcx_dummy_ops, s,
                              "tcx.thc24", TCX_THC_NREGS);
        sysbus_init_mmio(sbd, &s->thc24);
    }

    sysbus_init_irq(sbd, &s->irq);

    if (s->depth == 8) {
        s->con = graphic_console_init(DEVICE(dev), 0, &tcx_ops, s);
    } else {
        s->con = graphic_console_init(DEVICE(dev), 0, &tcx24_ops, s);
    }
    s->thcmisc = 0;

    qemu_console_resize(s->con, s->width, s->height);
}

static Property tcx_properties[] = {
    DEFINE_PROP_UINT32("vram_size", TCXState, vram_size, -1),
    DEFINE_PROP_UINT16("width",    TCXState, width,     -1),
    DEFINE_PROP_UINT16("height",   TCXState, height,    -1),
    DEFINE_PROP_UINT16("depth",    TCXState, depth,     -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void tcx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tcx_realizefn;
    dc->reset = tcx_reset;
    dc->vmsd = &vmstate_tcx;
    dc->props = tcx_properties;
}

static const TypeInfo tcx_info = {
    .name          = TYPE_TCX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TCXState),
    .instance_init = tcx_initfn,
    .class_init    = tcx_class_init,
};

static void tcx_register_types(void)
{
    type_register_static(&tcx_info);
}

type_init(tcx_register_types)
