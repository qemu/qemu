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

#include "qemu-common.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "hw/loader.h"
#include "hw/sysbus.h"

#define TCX_ROM_FILE "QEMU,tcx.bin"
#define FCODE_MAX_ROM_SIZE 0x10000

#define MAXX 1024
#define MAXY 768
#define TCX_DAC_NREGS 16
#define TCX_THC_NREGS_8  0x081c
#define TCX_THC_NREGS_24 0x1000
#define TCX_TEC_NREGS    0x1000

#define TYPE_TCX "SUNW,tcx"
#define TCX(obj) OBJECT_CHECK(TCXState, (obj), TYPE_TCX)

typedef struct TCXState {
    SysBusDevice parent_obj;

    QemuConsole *con;
    uint8_t *vram;
    uint32_t *vram24, *cplane;
    hwaddr prom_addr;
    MemoryRegion rom;
    MemoryRegion vram_mem;
    MemoryRegion vram_8bit;
    MemoryRegion vram_24bit;
    MemoryRegion vram_cplane;
    MemoryRegion dac;
    MemoryRegion tec;
    MemoryRegion thc24;
    MemoryRegion thc8;
    ram_addr_t vram24_offset, cplane_offset;
    uint32_t vram_size;
    uint32_t palette[256];
    uint8_t r[256], g[256], b[256];
    uint16_t width, height, depth;
    uint8_t dac_index, dac_state;
} TCXState;

static void tcx_set_dirty(TCXState *s)
{
    memory_region_set_dirty(&s->vram_mem, 0, MAXX * MAXY);
}

static void tcx24_set_dirty(TCXState *s)
{
    memory_region_set_dirty(&s->vram_mem, s->vram24_offset, MAXX * MAXY * 4);
    memory_region_set_dirty(&s->vram_mem, s->cplane_offset, MAXX * MAXY * 4);
}

static void update_palette_entries(TCXState *s, int start, int end)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int i;

    for (i = start; i < end; i++) {
        switch (surface_bits_per_pixel(surface)) {
        default:
        case 8:
            s->palette[i] = rgb_to_pixel8(s->r[i], s->g[i], s->b[i]);
            break;
        case 15:
            s->palette[i] = rgb_to_pixel15(s->r[i], s->g[i], s->b[i]);
            break;
        case 16:
            s->palette[i] = rgb_to_pixel16(s->r[i], s->g[i], s->b[i]);
            break;
        case 32:
            if (is_surface_bgr(surface)) {
                s->palette[i] = rgb_to_pixel32bgr(s->r[i], s->g[i], s->b[i]);
            } else {
                s->palette[i] = rgb_to_pixel32(s->r[i], s->g[i], s->b[i]);
            }
            break;
        }
    }
    if (s->depth == 24) {
        tcx24_set_dirty(s);
    } else {
        tcx_set_dirty(s);
    }
}

static void tcx_draw_line32(TCXState *s1, uint8_t *d,
                            const uint8_t *s, int width)
{
    int x;
    uint8_t val;
    uint32_t *p = (uint32_t *)d;

    for(x = 0; x < width; x++) {
        val = *s++;
        *p++ = s1->palette[val];
    }
}

static void tcx_draw_line16(TCXState *s1, uint8_t *d,
                            const uint8_t *s, int width)
{
    int x;
    uint8_t val;
    uint16_t *p = (uint16_t *)d;

    for(x = 0; x < width; x++) {
        val = *s++;
        *p++ = s1->palette[val];
    }
}

static void tcx_draw_line8(TCXState *s1, uint8_t *d,
                           const uint8_t *s, int width)
{
    int x;
    uint8_t val;

    for(x = 0; x < width; x++) {
        val = *s++;
        *d++ = s1->palette[val];
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
        if ((be32_to_cpu(*cplane++) & 0xff000000) == 0x03000000) {
            // 24-bit direct, BGR order
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
            val = *s;
            dval = s1->palette[val];
        }
        *p++ = dval;
    }
}

static inline int check_dirty(TCXState *s, ram_addr_t page, ram_addr_t page24,
                              ram_addr_t cpage)
{
    int ret;

    ret = memory_region_get_dirty(&s->vram_mem, page, TARGET_PAGE_SIZE,
                                  DIRTY_MEMORY_VGA);
    ret |= memory_region_get_dirty(&s->vram_mem, page24, TARGET_PAGE_SIZE * 4,
                                   DIRTY_MEMORY_VGA);
    ret |= memory_region_get_dirty(&s->vram_mem, cpage, TARGET_PAGE_SIZE * 4,
                                   DIRTY_MEMORY_VGA);
    return ret;
}

static inline void reset_dirty(TCXState *ts, ram_addr_t page_min,
                               ram_addr_t page_max, ram_addr_t page24,
                              ram_addr_t cpage)
{
    memory_region_reset_dirty(&ts->vram_mem,
                              page_min,
                              (page_max - page_min) + TARGET_PAGE_SIZE,
                              DIRTY_MEMORY_VGA);
    memory_region_reset_dirty(&ts->vram_mem,
                              page24 + page_min * 4,
                              (page_max - page_min) * 4 + TARGET_PAGE_SIZE,
                              DIRTY_MEMORY_VGA);
    memory_region_reset_dirty(&ts->vram_mem,
                              cpage + page_min * 4,
                              (page_max - page_min) * 4 + TARGET_PAGE_SIZE,
                              DIRTY_MEMORY_VGA);
}

/* Fixed line length 1024 allows us to do nice tricks not possible on
   VGA... */
static void tcx_update_display(void *opaque)
{
    TCXState *ts = opaque;
    DisplaySurface *surface = qemu_console_surface(ts->con);
    ram_addr_t page, page_min, page_max;
    int y, y_start, dd, ds;
    uint8_t *d, *s;
    void (*f)(TCXState *s1, uint8_t *dst, const uint8_t *src, int width);

    if (surface_bits_per_pixel(surface) == 0) {
        return;
    }

    page = 0;
    y_start = -1;
    page_min = -1;
    page_max = 0;
    d = surface_data(surface);
    s = ts->vram;
    dd = surface_stride(surface);
    ds = 1024;

    switch (surface_bits_per_pixel(surface)) {
    case 32:
        f = tcx_draw_line32;
        break;
    case 15:
    case 16:
        f = tcx_draw_line16;
        break;
    default:
    case 8:
        f = tcx_draw_line8;
        break;
    case 0:
        return;
    }

    for(y = 0; y < ts->height; y += 4, page += TARGET_PAGE_SIZE) {
        if (memory_region_get_dirty(&ts->vram_mem, page, TARGET_PAGE_SIZE,
                                    DIRTY_MEMORY_VGA)) {
            if (y_start < 0)
                y_start = y;
            if (page < page_min)
                page_min = page;
            if (page > page_max)
                page_max = page;
            f(ts, d, s, ts->width);
            d += dd;
            s += ds;
            f(ts, d, s, ts->width);
            d += dd;
            s += ds;
            f(ts, d, s, ts->width);
            d += dd;
            s += ds;
            f(ts, d, s, ts->width);
            d += dd;
            s += ds;
        } else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_gfx_update(ts->con, 0, y_start,
                               ts->width, y - y_start);
                y_start = -1;
            }
            d += dd * 4;
            s += ds * 4;
        }
    }
    if (y_start >= 0) {
        /* flush to display */
        dpy_gfx_update(ts->con, 0, y_start,
                       ts->width, y - y_start);
    }
    /* reset modified pages */
    if (page_max >= page_min) {
        memory_region_reset_dirty(&ts->vram_mem,
                                  page_min,
                                  (page_max - page_min) + TARGET_PAGE_SIZE,
                                  DIRTY_MEMORY_VGA);
    }
}

static void tcx24_update_display(void *opaque)
{
    TCXState *ts = opaque;
    DisplaySurface *surface = qemu_console_surface(ts->con);
    ram_addr_t page, page_min, page_max, cpage, page24;
    int y, y_start, dd, ds;
    uint8_t *d, *s;
    uint32_t *cptr, *s24;

    if (surface_bits_per_pixel(surface) != 32) {
            return;
    }

    page = 0;
    page24 = ts->vram24_offset;
    cpage = ts->cplane_offset;
    y_start = -1;
    page_min = -1;
    page_max = 0;
    d = surface_data(surface);
    s = ts->vram;
    s24 = ts->vram24;
    cptr = ts->cplane;
    dd = surface_stride(surface);
    ds = 1024;

    for(y = 0; y < ts->height; y += 4, page += TARGET_PAGE_SIZE,
            page24 += TARGET_PAGE_SIZE, cpage += TARGET_PAGE_SIZE) {
        if (check_dirty(ts, page, page24, cpage)) {
            if (y_start < 0)
                y_start = y;
            if (page < page_min)
                page_min = page;
            if (page > page_max)
                page_max = page;
            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            d += dd;
            s += ds;
            cptr += ds;
            s24 += ds;
            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            d += dd;
            s += ds;
            cptr += ds;
            s24 += ds;
            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            d += dd;
            s += ds;
            cptr += ds;
            s24 += ds;
            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            d += dd;
            s += ds;
            cptr += ds;
            s24 += ds;
        } else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_gfx_update(ts->con, 0, y_start,
                               ts->width, y - y_start);
                y_start = -1;
            }
            d += dd * 4;
            s += ds * 4;
            cptr += ds * 4;
            s24 += ds * 4;
        }
    }
    if (y_start >= 0) {
        /* flush to display */
        dpy_gfx_update(ts->con, 0, y_start,
                       ts->width, y - y_start);
    }
    /* reset modified pages */
    if (page_max >= page_min) {
        reset_dirty(ts, page_min, page_max, page24, cpage);
    }
}

static void tcx_invalidate_display(void *opaque)
{
    TCXState *s = opaque;

    tcx_set_dirty(s);
    qemu_console_resize(s->con, s->width, s->height);
}

static void tcx24_invalidate_display(void *opaque)
{
    TCXState *s = opaque;

    tcx_set_dirty(s);
    tcx24_set_dirty(s);
    qemu_console_resize(s->con, s->width, s->height);
}

static int vmstate_tcx_post_load(void *opaque, int version_id)
{
    TCXState *s = opaque;

    update_palette_entries(s, 0, 256);
    if (s->depth == 24) {
        tcx24_set_dirty(s);
    } else {
        tcx_set_dirty(s);
    }

    return 0;
}

static const VMStateDescription vmstate_tcx = {
    .name ="tcx",
    .version_id = 4,
    .minimum_version_id = 4,
    .minimum_version_id_old = 4,
    .post_load = vmstate_tcx_post_load,
    .fields      = (VMStateField []) {
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
    memset(s->r, 0, 256);
    memset(s->g, 0, 256);
    memset(s->b, 0, 256);
    s->r[255] = s->g[255] = s->b[255] = 255;
    update_palette_entries(s, 0, 256);
    memset(s->vram, 0, MAXX*MAXY);
    memory_region_reset_dirty(&s->vram_mem, 0, MAXX * MAXY * (1 + 4 + 4),
                              DIRTY_MEMORY_VGA);
    s->dac_index = 0;
    s->dac_state = 0;
}

static uint64_t tcx_dac_readl(void *opaque, hwaddr addr,
                              unsigned size)
{
    return 0;
}

static void tcx_dac_writel(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    TCXState *s = opaque;

    switch (addr) {
    case 0:
        s->dac_index = val >> 24;
        s->dac_state = 0;
        break;
    case 4:
        switch (s->dac_state) {
        case 0:
            s->r[s->dac_index] = val >> 24;
            update_palette_entries(s, s->dac_index, s->dac_index + 1);
            s->dac_state++;
            break;
        case 1:
            s->g[s->dac_index] = val >> 24;
            update_palette_entries(s, s->dac_index, s->dac_index + 1);
            s->dac_state++;
            break;
        case 2:
            s->b[s->dac_index] = val >> 24;
            update_palette_entries(s, s->dac_index, s->dac_index + 1);
            s->dac_index = (s->dac_index + 1) & 255; // Index autoincrement
        default:
            s->dac_state = 0;
            break;
        }
        break;
    default:
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

static uint64_t dummy_readl(void *opaque, hwaddr addr,
                            unsigned size)
{
    return 0;
}

static void dummy_writel(void *opaque, hwaddr addr,
                         uint64_t val, unsigned size)
{
}

static const MemoryRegionOps dummy_ops = {
    .read = dummy_readl,
    .write = dummy_writel,
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

static int tcx_init1(SysBusDevice *dev)
{
    TCXState *s = TCX(dev);
    ram_addr_t vram_offset = 0;
    int size, ret;
    uint8_t *vram_base;
    char *fcode_filename;

    memory_region_init_ram(&s->vram_mem, OBJECT(s), "tcx.vram",
                           s->vram_size * (1 + 4 + 4));
    vmstate_register_ram_global(&s->vram_mem);
    vram_base = memory_region_get_ram_ptr(&s->vram_mem);

    /* FCode ROM */
    memory_region_init_ram(&s->rom, NULL, "tcx.prom", FCODE_MAX_ROM_SIZE);
    vmstate_register_ram_global(&s->rom);
    memory_region_set_readonly(&s->rom, true);
    sysbus_init_mmio(dev, &s->rom);

    fcode_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, TCX_ROM_FILE);
    if (fcode_filename) {
        ret = load_image_targphys(fcode_filename, s->prom_addr,
                                  FCODE_MAX_ROM_SIZE);
        if (ret < 0 || ret > FCODE_MAX_ROM_SIZE) {
            fprintf(stderr, "tcx: could not load prom '%s'\n", TCX_ROM_FILE);
            return -1;
        }
    }

    /* 8-bit plane */
    s->vram = vram_base;
    size = s->vram_size;
    memory_region_init_alias(&s->vram_8bit, OBJECT(s), "tcx.vram.8bit",
                             &s->vram_mem, vram_offset, size);
    sysbus_init_mmio(dev, &s->vram_8bit);
    vram_offset += size;
    vram_base += size;

    /* DAC */
    memory_region_init_io(&s->dac, OBJECT(s), &tcx_dac_ops, s,
                          "tcx.dac", TCX_DAC_NREGS);
    sysbus_init_mmio(dev, &s->dac);

    /* TEC (dummy) */
    memory_region_init_io(&s->tec, OBJECT(s), &dummy_ops, s,
                          "tcx.tec", TCX_TEC_NREGS);
    sysbus_init_mmio(dev, &s->tec);
    /* THC: NetBSD writes here even with 8-bit display: dummy */
    memory_region_init_io(&s->thc24, OBJECT(s), &dummy_ops, s, "tcx.thc24",
                          TCX_THC_NREGS_24);
    sysbus_init_mmio(dev, &s->thc24);

    if (s->depth == 24) {
        /* 24-bit plane */
        size = s->vram_size * 4;
        s->vram24 = (uint32_t *)vram_base;
        s->vram24_offset = vram_offset;
        memory_region_init_alias(&s->vram_24bit, OBJECT(s), "tcx.vram.24bit",
                                 &s->vram_mem, vram_offset, size);
        sysbus_init_mmio(dev, &s->vram_24bit);
        vram_offset += size;
        vram_base += size;

        /* Control plane */
        size = s->vram_size * 4;
        s->cplane = (uint32_t *)vram_base;
        s->cplane_offset = vram_offset;
        memory_region_init_alias(&s->vram_cplane, OBJECT(s), "tcx.vram.cplane",
                                 &s->vram_mem, vram_offset, size);
        sysbus_init_mmio(dev, &s->vram_cplane);

        s->con = graphic_console_init(DEVICE(dev), 0, &tcx24_ops, s);
    } else {
        /* THC 8 bit (dummy) */
        memory_region_init_io(&s->thc8, OBJECT(s), &dummy_ops, s, "tcx.thc8",
                              TCX_THC_NREGS_8);
        sysbus_init_mmio(dev, &s->thc8);

        s->con = graphic_console_init(DEVICE(dev), 0, &tcx_ops, s);
    }

    qemu_console_resize(s->con, s->width, s->height);
    return 0;
}

static Property tcx_properties[] = {
    DEFINE_PROP_UINT32("vram_size", TCXState, vram_size, -1),
    DEFINE_PROP_UINT16("width",    TCXState, width,     -1),
    DEFINE_PROP_UINT16("height",   TCXState, height,    -1),
    DEFINE_PROP_UINT16("depth",    TCXState, depth,     -1),
    DEFINE_PROP_UINT64("prom_addr", TCXState, prom_addr, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void tcx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = tcx_init1;
    dc->reset = tcx_reset;
    dc->vmsd = &vmstate_tcx;
    dc->props = tcx_properties;
}

static const TypeInfo tcx_info = {
    .name          = TYPE_TCX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TCXState),
    .class_init    = tcx_class_init,
};

static void tcx_register_types(void)
{
    type_register_static(&tcx_info);
}

type_init(tcx_register_types)
