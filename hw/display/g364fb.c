/*
 * QEMU G364 framebuffer Emulator.
 *
 * Copyright (c) 2007-2011 Herve Poussineau
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qom/object.h"

typedef struct G364State {
    /* hardware */
    uint32_t vram_size;
    qemu_irq irq;
    MemoryRegion mem_vram;
    MemoryRegion mem_ctrl;
    /* registers */
    uint8_t color_palette[256][3];
    uint8_t cursor_palette[3][3];
    uint16_t cursor[512];
    uint32_t cursor_position;
    uint32_t ctla;
    uint32_t top_of_screen;
    uint32_t width, height; /* in pixels */
    /* display refresh support */
    QemuConsole *con;
    int depth;
    int blanked;
} G364State;

#define REG_BOOT     0x000000
#define REG_DISPLAY  0x000118
#define REG_VDISPLAY 0x000150
#define REG_CTLA     0x000300
#define REG_TOP      0x000400
#define REG_CURS_PAL 0x000508
#define REG_CURS_POS 0x000638
#define REG_CLR_PAL  0x000800
#define REG_CURS_PAT 0x001000
#define REG_RESET    0x100000

#define CTLA_FORCE_BLANK 0x00000400
#define CTLA_NO_CURSOR   0x00800000

#define G364_PAGE_SIZE 4096

static inline int check_dirty(G364State *s, DirtyBitmapSnapshot *snap, ram_addr_t page)
{
    return memory_region_snapshot_get_dirty(&s->mem_vram, snap, page, G364_PAGE_SIZE);
}

static void g364fb_draw_graphic8(G364State *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    DirtyBitmapSnapshot *snap;
    int i, w;
    uint8_t *vram;
    uint8_t *data_display, *dd;
    ram_addr_t page;
    int x, y;
    int xmin, xmax;
    int ymin, ymax;
    int xcursor, ycursor;
    unsigned int (*rgb_to_pixel)(unsigned int r, unsigned int g, unsigned int b);

    switch (surface_bits_per_pixel(surface)) {
        case 8:
            rgb_to_pixel = rgb_to_pixel8;
            w = 1;
            break;
        case 15:
            rgb_to_pixel = rgb_to_pixel15;
            w = 2;
            break;
        case 16:
            rgb_to_pixel = rgb_to_pixel16;
            w = 2;
            break;
        case 32:
            rgb_to_pixel = rgb_to_pixel32;
            w = 4;
            break;
        default:
            hw_error("g364: unknown host depth %d",
                     surface_bits_per_pixel(surface));
            return;
    }

    page = 0;

    x = y = 0;
    xmin = s->width;
    xmax = 0;
    ymin = s->height;
    ymax = 0;

    if (!(s->ctla & CTLA_NO_CURSOR)) {
        xcursor = s->cursor_position >> 12;
        ycursor = s->cursor_position & 0xfff;
    } else {
        xcursor = ycursor = -65;
    }

    vram = memory_region_get_ram_ptr(&s->mem_vram) + s->top_of_screen;
    /* XXX: out of range in vram? */
    data_display = dd = surface_data(surface);
    snap = memory_region_snapshot_and_clear_dirty(&s->mem_vram, 0, s->vram_size,
                                                  DIRTY_MEMORY_VGA);
    while (y < s->height) {
        if (check_dirty(s, snap, page)) {
            if (y < ymin)
                ymin = ymax = y;
            if (x < xmin)
                xmin = x;
            for (i = 0; i < G364_PAGE_SIZE; i++) {
                uint8_t index;
                unsigned int color;
                if (unlikely((y >= ycursor && y < ycursor + 64) &&
                    (x >= xcursor && x < xcursor + 64))) {
                    /* pointer area */
                    int xdiff = x - xcursor;
                    uint16_t curs = s->cursor[(y - ycursor) * 8 + xdiff / 8];
                    int op = (curs >> ((xdiff & 7) * 2)) & 3;
                    if (likely(op == 0)) {
                        /* transparent */
                        index = *vram;
                        color = (*rgb_to_pixel)(
                            s->color_palette[index][0],
                            s->color_palette[index][1],
                            s->color_palette[index][2]);
                    } else {
                        /* get cursor color */
                        index = op - 1;
                        color = (*rgb_to_pixel)(
                            s->cursor_palette[index][0],
                            s->cursor_palette[index][1],
                            s->cursor_palette[index][2]);
                    }
                } else {
                    /* normal area */
                    index = *vram;
                    color = (*rgb_to_pixel)(
                        s->color_palette[index][0],
                        s->color_palette[index][1],
                        s->color_palette[index][2]);
                }
                memcpy(dd, &color, w);
                dd += w;
                x++;
                vram++;
                if (x == s->width) {
                    xmax = s->width - 1;
                    y++;
                    if (y == s->height) {
                        ymax = s->height - 1;
                        goto done;
                    }
                    data_display = dd = data_display + surface_stride(surface);
                    xmin = 0;
                    x = 0;
                }
            }
            if (x > xmax)
                xmax = x;
            if (y > ymax)
                ymax = y;
        } else {
            int dy;
            if (xmax || ymax) {
                dpy_gfx_update(s->con, xmin, ymin,
                               xmax - xmin + 1, ymax - ymin + 1);
                xmin = s->width;
                xmax = 0;
                ymin = s->height;
                ymax = 0;
            }
            x += G364_PAGE_SIZE;
            dy = x / s->width;
            x = x % s->width;
            y += dy;
            vram += G364_PAGE_SIZE;
            data_display += dy * surface_stride(surface);
            dd = data_display + x * w;
        }
        page += G364_PAGE_SIZE;
    }

done:
    if (xmax || ymax) {
        dpy_gfx_update(s->con, xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);
    }
    g_free(snap);
}

static void g364fb_draw_blank(G364State *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int i, w;
    uint8_t *d;

    if (s->blanked) {
        /* Screen is already blank. No need to redraw it */
        return;
    }

    w = s->width * surface_bytes_per_pixel(surface);
    d = surface_data(surface);
    for (i = 0; i < s->height; i++) {
        memset(d, 0, w);
        d += surface_stride(surface);
    }

    dpy_gfx_update_full(s->con);
    s->blanked = 1;
}

static void g364fb_update_display(void *opaque)
{
    G364State *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);

    qemu_flush_coalesced_mmio_buffer();

    if (s->width == 0 || s->height == 0)
        return;

    if (s->width != surface_width(surface) ||
        s->height != surface_height(surface)) {
        qemu_console_resize(s->con, s->width, s->height);
    }

    if (s->ctla & CTLA_FORCE_BLANK) {
        g364fb_draw_blank(s);
    } else if (s->depth == 8) {
        g364fb_draw_graphic8(s);
    } else {
        error_report("g364: unknown guest depth %d", s->depth);
    }

    qemu_irq_raise(s->irq);
}

static inline void g364fb_invalidate_display(void *opaque)
{
    G364State *s = opaque;

    s->blanked = 0;
    memory_region_set_dirty(&s->mem_vram, 0, s->vram_size);
}

static void g364fb_reset(G364State *s)
{
    uint8_t *vram = memory_region_get_ram_ptr(&s->mem_vram);

    qemu_irq_lower(s->irq);

    memset(s->color_palette, 0, sizeof(s->color_palette));
    memset(s->cursor_palette, 0, sizeof(s->cursor_palette));
    memset(s->cursor, 0, sizeof(s->cursor));
    s->cursor_position = 0;
    s->ctla = 0;
    s->top_of_screen = 0;
    s->width = s->height = 0;
    memset(vram, 0, s->vram_size);
    g364fb_invalidate_display(s);
}

/* called for accesses to io ports */
static uint64_t g364fb_ctrl_read(void *opaque,
                                 hwaddr addr,
                                 unsigned int size)
{
    G364State *s = opaque;
    uint32_t val;

    if (addr >= REG_CURS_PAT && addr < REG_CURS_PAT + 0x1000) {
        /* cursor pattern */
        int idx = (addr - REG_CURS_PAT) >> 3;
        val = s->cursor[idx];
    } else if (addr >= REG_CURS_PAL && addr < REG_CURS_PAL + 0x18) {
        /* cursor palette */
        int idx = (addr - REG_CURS_PAL) >> 3;
        val = ((uint32_t)s->cursor_palette[idx][0] << 16);
        val |= ((uint32_t)s->cursor_palette[idx][1] << 8);
        val |= ((uint32_t)s->cursor_palette[idx][2] << 0);
    } else {
        switch (addr) {
            case REG_DISPLAY:
                val = s->width / 4;
                break;
            case REG_VDISPLAY:
                val = s->height * 2;
                break;
            case REG_CTLA:
                val = s->ctla;
                break;
            default:
            {
                error_report("g364: invalid read at [" HWADDR_FMT_plx "]",
                             addr);
                val = 0;
                break;
            }
        }
    }

    trace_g364fb_read(addr, val);

    return val;
}

static void g364fb_update_depth(G364State *s)
{
    static const int depths[8] = { 1, 2, 4, 8, 15, 16, 0 };
    s->depth = depths[(s->ctla & 0x00700000) >> 20];
}

static void g364_invalidate_cursor_position(G364State *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int ymin, ymax, start, end;

    /* invalidate only near the cursor */
    ymin = s->cursor_position & 0xfff;
    ymax = MIN(s->height, ymin + 64);
    start = ymin * surface_stride(surface);
    end = (ymax + 1) * surface_stride(surface);

    memory_region_set_dirty(&s->mem_vram, start, end - start);
}

static void g364fb_ctrl_write(void *opaque,
                              hwaddr addr,
                              uint64_t val,
                              unsigned int size)
{
    G364State *s = opaque;

    trace_g364fb_write(addr, val);

    if (addr >= REG_CLR_PAL && addr < REG_CLR_PAL + 0x800) {
        /* color palette */
        int idx = (addr - REG_CLR_PAL) >> 3;
        s->color_palette[idx][0] = (val >> 16) & 0xff;
        s->color_palette[idx][1] = (val >> 8) & 0xff;
        s->color_palette[idx][2] = val & 0xff;
        g364fb_invalidate_display(s);
    } else if (addr >= REG_CURS_PAT && addr < REG_CURS_PAT + 0x1000) {
        /* cursor pattern */
        int idx = (addr - REG_CURS_PAT) >> 3;
        s->cursor[idx] = val;
        g364fb_invalidate_display(s);
    } else if (addr >= REG_CURS_PAL && addr < REG_CURS_PAL + 0x18) {
        /* cursor palette */
        int idx = (addr - REG_CURS_PAL) >> 3;
        s->cursor_palette[idx][0] = (val >> 16) & 0xff;
        s->cursor_palette[idx][1] = (val >> 8) & 0xff;
        s->cursor_palette[idx][2] = val & 0xff;
        g364fb_invalidate_display(s);
    } else {
        switch (addr) {
        case REG_BOOT: /* Boot timing */
        case 0x00108: /* Line timing: half sync */
        case 0x00110: /* Line timing: back porch */
        case 0x00120: /* Line timing: short display */
        case 0x00128: /* Frame timing: broad pulse */
        case 0x00130: /* Frame timing: v sync */
        case 0x00138: /* Frame timing: v preequalise */
        case 0x00140: /* Frame timing: v postequalise */
        case 0x00148: /* Frame timing: v blank */
        case 0x00158: /* Line timing: line time */
        case 0x00160: /* Frame store: line start */
        case 0x00168: /* vram cycle: mem init */
        case 0x00170: /* vram cycle: transfer delay */
        case 0x00200: /* vram cycle: mask register */
            /* ignore */
            break;
        case REG_TOP:
            s->top_of_screen = val;
            g364fb_invalidate_display(s);
            break;
        case REG_DISPLAY:
            s->width = val * 4;
            break;
        case REG_VDISPLAY:
            s->height = val / 2;
            break;
        case REG_CTLA:
            s->ctla = val;
            g364fb_update_depth(s);
            g364fb_invalidate_display(s);
            break;
        case REG_CURS_POS:
            g364_invalidate_cursor_position(s);
            s->cursor_position = val;
            g364_invalidate_cursor_position(s);
            break;
        case REG_RESET:
            g364fb_reset(s);
            break;
        default:
            error_report("g364: invalid write of 0x%" PRIx64
                         " at [" HWADDR_FMT_plx "]", val, addr);
            break;
        }
    }
    qemu_irq_lower(s->irq);
}

static const MemoryRegionOps g364fb_ctrl_ops = {
    .read = g364fb_ctrl_read,
    .write = g364fb_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static int g364fb_post_load(void *opaque, int version_id)
{
    G364State *s = opaque;

    /* force refresh */
    g364fb_update_depth(s);
    g364fb_invalidate_display(s);

    return 0;
}

static const VMStateDescription vmstate_g364fb = {
    .name = "g364fb",
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = g364fb_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_BUFFER_UNSAFE(color_palette, G364State, 0, 256 * 3),
        VMSTATE_BUFFER_UNSAFE(cursor_palette, G364State, 0, 9),
        VMSTATE_UINT16_ARRAY(cursor, G364State, 512),
        VMSTATE_UINT32(cursor_position, G364State),
        VMSTATE_UINT32(ctla, G364State),
        VMSTATE_UINT32(top_of_screen, G364State),
        VMSTATE_UINT32(width, G364State),
        VMSTATE_UINT32(height, G364State),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps g364fb_ops = {
    .invalidate  = g364fb_invalidate_display,
    .gfx_update  = g364fb_update_display,
};

static void g364fb_init(DeviceState *dev, G364State *s)
{
    s->con = graphic_console_init(dev, 0, &g364fb_ops, s);

    memory_region_init_io(&s->mem_ctrl, OBJECT(dev), &g364fb_ctrl_ops, s,
                          "ctrl", 0x180000);
    memory_region_init_ram(&s->mem_vram, NULL, "g364fb.vram", s->vram_size,
                           &error_fatal);
    memory_region_set_log(&s->mem_vram, true, DIRTY_MEMORY_VGA);
}

#define TYPE_G364 "sysbus-g364"
OBJECT_DECLARE_SIMPLE_TYPE(G364SysBusState, G364)

struct G364SysBusState {
    SysBusDevice parent_obj;

    G364State g364;
};

static void g364fb_sysbus_realize(DeviceState *dev, Error **errp)
{
    G364SysBusState *sbs = G364(dev);
    G364State *s = &sbs->g364;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    g364fb_init(dev, s);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_mmio(sbd, &s->mem_ctrl);
    sysbus_init_mmio(sbd, &s->mem_vram);
}

static void g364fb_sysbus_reset(DeviceState *d)
{
    G364SysBusState *s = G364(d);

    g364fb_reset(&s->g364);
}

static const Property g364fb_sysbus_properties[] = {
    DEFINE_PROP_UINT32("vram_size", G364SysBusState, g364.vram_size, 8 * MiB),
};

static const VMStateDescription vmstate_g364fb_sysbus = {
    .name = "g364fb-sysbus",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(g364, G364SysBusState, 2, vmstate_g364fb, G364State),
        VMSTATE_END_OF_LIST()
    }
};

static void g364fb_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g364fb_sysbus_realize;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->desc = "G364 framebuffer";
    device_class_set_legacy_reset(dc, g364fb_sysbus_reset);
    dc->vmsd = &vmstate_g364fb_sysbus;
    device_class_set_props(dc, g364fb_sysbus_properties);
}

static const TypeInfo g364fb_sysbus_info = {
    .name          = TYPE_G364,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G364SysBusState),
    .class_init    = g364fb_sysbus_class_init,
};

static void g364fb_register_types(void)
{
    type_register_static(&g364fb_sysbus_info);
}

type_init(g364fb_register_types)
