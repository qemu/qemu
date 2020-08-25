/*
 * QEMU Motorola 680x0 Macintosh Video Card Emulation
 *                 Copyright (c) 2012-2018 Laurent Vivier
 *
 * some parts from QEMU G364 framebuffer Emulator.
 *                 Copyright (c) 2007-2011 Herve Poussineau
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "hw/nubus/nubus.h"
#include "hw/display/macfb.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define VIDEO_BASE 0x00001000
#define DAFB_BASE  0x00800000

#define MACFB_PAGE_SIZE 4096
#define MACFB_VRAM_SIZE (4 * MiB)

#define DAFB_RESET      0x200
#define DAFB_LUT        0x213


typedef void macfb_draw_line_func(MacfbState *s, uint8_t *d, uint32_t addr,
                                  int width);

static inline uint8_t macfb_read_byte(MacfbState *s, uint32_t addr)
{
    return s->vram[addr & s->vram_bit_mask];
}

/* 1-bit color */
static void macfb_draw_line1(MacfbState *s, uint8_t *d, uint32_t addr,
                             int width)
{
    uint8_t r, g, b;
    int x;

    for (x = 0; x < width; x++) {
        int bit = x & 7;
        int idx = (macfb_read_byte(s, addr) >> (7 - bit)) & 1;
        r = g = b  = ((1 - idx) << 7);
        addr += (bit == 7);

        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        d += 4;
    }
}

/* 2-bit color */
static void macfb_draw_line2(MacfbState *s, uint8_t *d, uint32_t addr,
                             int width)
{
    uint8_t r, g, b;
    int x;

    for (x = 0; x < width; x++) {
        int bit = (x & 3);
        int idx = (macfb_read_byte(s, addr) >> ((3 - bit) << 1)) & 3;
        r = s->color_palette[idx * 3];
        g = s->color_palette[idx * 3 + 1];
        b = s->color_palette[idx * 3 + 2];
        addr += (bit == 3);

        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        d += 4;
    }
}

/* 4-bit color */
static void macfb_draw_line4(MacfbState *s, uint8_t *d, uint32_t addr,
                             int width)
{
    uint8_t r, g, b;
    int x;

    for (x = 0; x < width; x++) {
        int bit = x & 1;
        int idx = (macfb_read_byte(s, addr) >> ((1 - bit) << 2)) & 15;
        r = s->color_palette[idx * 3];
        g = s->color_palette[idx * 3 + 1];
        b = s->color_palette[idx * 3 + 2];
        addr += (bit == 1);

        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        d += 4;
    }
}

/* 8-bit color */
static void macfb_draw_line8(MacfbState *s, uint8_t *d, uint32_t addr,
                             int width)
{
    uint8_t r, g, b;
    int x;

    for (x = 0; x < width; x++) {
        r = s->color_palette[macfb_read_byte(s, addr) * 3];
        g = s->color_palette[macfb_read_byte(s, addr) * 3 + 1];
        b = s->color_palette[macfb_read_byte(s, addr) * 3 + 2];
        addr++;

        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        d += 4;
    }
}

/* 16-bit color */
static void macfb_draw_line16(MacfbState *s, uint8_t *d, uint32_t addr,
                              int width)
{
    uint8_t r, g, b;
    int x;

    for (x = 0; x < width; x++) {
        uint16_t pixel;
        pixel = (macfb_read_byte(s, addr) << 8) | macfb_read_byte(s, addr + 1);
        r = ((pixel >> 10) & 0x1f) << 3;
        g = ((pixel >> 5) & 0x1f) << 3;
        b = (pixel & 0x1f) << 3;
        addr += 2;

        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        d += 4;
    }
}

/* 24-bit color */
static void macfb_draw_line24(MacfbState *s, uint8_t *d, uint32_t addr,
                              int width)
{
    uint8_t r, g, b;
    int x;

    for (x = 0; x < width; x++) {
        r = macfb_read_byte(s, addr);
        g = macfb_read_byte(s, addr + 1);
        b = macfb_read_byte(s, addr + 2);
        addr += 3;

        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        d += 4;
    }
}


enum {
    MACFB_DRAW_LINE1,
    MACFB_DRAW_LINE2,
    MACFB_DRAW_LINE4,
    MACFB_DRAW_LINE8,
    MACFB_DRAW_LINE16,
    MACFB_DRAW_LINE24,
    MACFB_DRAW_LINE_NB,
};

static macfb_draw_line_func * const
                              macfb_draw_line_table[MACFB_DRAW_LINE_NB] = {
    macfb_draw_line1,
    macfb_draw_line2,
    macfb_draw_line4,
    macfb_draw_line8,
    macfb_draw_line16,
    macfb_draw_line24,
};

static int macfb_check_dirty(MacfbState *s, DirtyBitmapSnapshot *snap,
                             ram_addr_t addr, int len)
{
    return memory_region_snapshot_get_dirty(&s->mem_vram, snap, addr, len);
}

static void macfb_draw_graphic(MacfbState *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    DirtyBitmapSnapshot *snap = NULL;
    ram_addr_t page;
    uint32_t v = 0;
    int y, ymin;
    int macfb_stride = (s->depth * s->width + 7) / 8;
    macfb_draw_line_func *macfb_draw_line;

    switch (s->depth) {
    case 1:
        v = MACFB_DRAW_LINE1;
        break;
    case 2:
        v = MACFB_DRAW_LINE2;
        break;
    case 4:
        v = MACFB_DRAW_LINE4;
        break;
    case 8:
        v = MACFB_DRAW_LINE8;
        break;
    case 16:
        v = MACFB_DRAW_LINE16;
        break;
    case 24:
        v = MACFB_DRAW_LINE24;
        break;
    }

    macfb_draw_line = macfb_draw_line_table[v];
    assert(macfb_draw_line != NULL);

    snap = memory_region_snapshot_and_clear_dirty(&s->mem_vram, 0x0,
                                             memory_region_size(&s->mem_vram),
                                             DIRTY_MEMORY_VGA);

    ymin = -1;
    page = 0;
    for (y = 0; y < s->height; y++, page += macfb_stride) {
        if (macfb_check_dirty(s, snap, page, macfb_stride)) {
            uint8_t *data_display;

            data_display = surface_data(surface) + y * surface_stride(surface);
            macfb_draw_line(s, data_display, page, s->width);

            if (ymin < 0) {
                ymin = y;
            }
        } else {
            if (ymin >= 0) {
                dpy_gfx_update(s->con, 0, ymin, s->width, y - ymin);
                ymin = -1;
            }
        }
    }

    if (ymin >= 0) {
        dpy_gfx_update(s->con, 0, ymin, s->width, y - ymin);
    }

    g_free(snap);
}

static void macfb_invalidate_display(void *opaque)
{
    MacfbState *s = opaque;

    memory_region_set_dirty(&s->mem_vram, 0, MACFB_VRAM_SIZE);
}

static void macfb_update_display(void *opaque)
{
    MacfbState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);

    qemu_flush_coalesced_mmio_buffer();

    if (s->width == 0 || s->height == 0) {
        return;
    }

    if (s->width != surface_width(surface) ||
        s->height != surface_height(surface)) {
        qemu_console_resize(s->con, s->width, s->height);
    }

    macfb_draw_graphic(s);
}

static void macfb_reset(MacfbState *s)
{
    int i;

    s->palette_current = 0;
    for (i = 0; i < 256; i++) {
        s->color_palette[i * 3] = 255 - i;
        s->color_palette[i * 3 + 1] = 255 - i;
        s->color_palette[i * 3 + 2] = 255 - i;
    }
    memset(s->vram, 0, MACFB_VRAM_SIZE);
    macfb_invalidate_display(s);
}

static uint64_t macfb_ctrl_read(void *opaque,
                                hwaddr addr,
                                unsigned int size)
{
    return 0;
}

static void macfb_ctrl_write(void *opaque,
                             hwaddr addr,
                             uint64_t val,
                             unsigned int size)
{
    MacfbState *s = opaque;
    switch (addr) {
    case DAFB_RESET:
        s->palette_current = 0;
        break;
    case DAFB_LUT:
        s->color_palette[s->palette_current++] = val;
        if (s->palette_current % 3) {
            macfb_invalidate_display(s);
        }
        break;
    }
}

static const MemoryRegionOps macfb_ctrl_ops = {
    .read = macfb_ctrl_read,
    .write = macfb_ctrl_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static int macfb_post_load(void *opaque, int version_id)
{
    macfb_invalidate_display(opaque);
    return 0;
}

static const VMStateDescription vmstate_macfb = {
    .name = "macfb",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = macfb_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(color_palette, MacfbState, 256 * 3),
        VMSTATE_UINT32(palette_current, MacfbState),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps macfb_ops = {
    .invalidate = macfb_invalidate_display,
    .gfx_update = macfb_update_display,
};

static void macfb_common_realize(DeviceState *dev, MacfbState *s, Error **errp)
{
    DisplaySurface *surface;

    if (s->depth != 1 && s->depth != 2 && s->depth != 4 && s->depth != 8 &&
        s->depth != 16 && s->depth != 24) {
        error_setg(errp, "unknown guest depth %d", s->depth);
        return;
    }

    s->con = graphic_console_init(dev, 0, &macfb_ops, s);
    surface = qemu_console_surface(s->con);

    if (surface_bits_per_pixel(surface) != 32) {
        error_setg(errp, "unknown host depth %d",
                   surface_bits_per_pixel(surface));
        return;
    }

    memory_region_init_io(&s->mem_ctrl, OBJECT(dev), &macfb_ctrl_ops, s,
                          "macfb-ctrl", 0x1000);

    memory_region_init_ram_nomigrate(&s->mem_vram, OBJECT(s), "macfb-vram",
                                     MACFB_VRAM_SIZE, errp);
    s->vram = memory_region_get_ram_ptr(&s->mem_vram);
    s->vram_bit_mask = MACFB_VRAM_SIZE - 1;
    vmstate_register_ram(&s->mem_vram, dev);
    memory_region_set_coalescing(&s->mem_vram);
}

static void macfb_sysbus_realize(DeviceState *dev, Error **errp)
{
    MacfbSysBusState *s = MACFB(dev);
    MacfbState *ms = &s->macfb;

    macfb_common_realize(dev, ms, errp);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &ms->mem_ctrl);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &ms->mem_vram);
}

const uint8_t macfb_rom[] = {
    255, 0, 0, 0,
};

static void macfb_nubus_realize(DeviceState *dev, Error **errp)
{
    NubusDevice *nd = NUBUS_DEVICE(dev);
    MacfbNubusState *s = NUBUS_MACFB(dev);
    MacfbNubusDeviceClass *ndc = NUBUS_MACFB_GET_CLASS(dev);
    MacfbState *ms = &s->macfb;

    ndc->parent_realize(dev, errp);

    macfb_common_realize(dev, ms, errp);
    memory_region_add_subregion(&nd->slot_mem, DAFB_BASE, &ms->mem_ctrl);
    memory_region_add_subregion(&nd->slot_mem, VIDEO_BASE, &ms->mem_vram);

    nubus_register_rom(nd, macfb_rom, sizeof(macfb_rom), 1, 9, 0xf);
}

static void macfb_sysbus_reset(DeviceState *d)
{
    MacfbSysBusState *s = MACFB(d);
    macfb_reset(&s->macfb);
}

static void macfb_nubus_reset(DeviceState *d)
{
    MacfbNubusState *s = NUBUS_MACFB(d);
    macfb_reset(&s->macfb);
}

static Property macfb_sysbus_properties[] = {
    DEFINE_PROP_UINT32("width", MacfbSysBusState, macfb.width, 640),
    DEFINE_PROP_UINT32("height", MacfbSysBusState, macfb.height, 480),
    DEFINE_PROP_UINT8("depth", MacfbSysBusState, macfb.depth, 8),
    DEFINE_PROP_END_OF_LIST(),
};

static Property macfb_nubus_properties[] = {
    DEFINE_PROP_UINT32("width", MacfbNubusState, macfb.width, 640),
    DEFINE_PROP_UINT32("height", MacfbNubusState, macfb.height, 480),
    DEFINE_PROP_UINT8("depth", MacfbNubusState, macfb.depth, 8),
    DEFINE_PROP_END_OF_LIST(),
};

static void macfb_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = macfb_sysbus_realize;
    dc->desc = "SysBus Macintosh framebuffer";
    dc->reset = macfb_sysbus_reset;
    dc->vmsd = &vmstate_macfb;
    device_class_set_props(dc, macfb_sysbus_properties);
}

static void macfb_nubus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    MacfbNubusDeviceClass *ndc = NUBUS_MACFB_CLASS(klass);

    device_class_set_parent_realize(dc, macfb_nubus_realize,
                                    &ndc->parent_realize);
    dc->desc = "Nubus Macintosh framebuffer";
    dc->reset = macfb_nubus_reset;
    dc->vmsd = &vmstate_macfb;
    device_class_set_props(dc, macfb_nubus_properties);
}

static TypeInfo macfb_sysbus_info = {
    .name          = TYPE_MACFB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MacfbSysBusState),
    .class_init    = macfb_sysbus_class_init,
};

static TypeInfo macfb_nubus_info = {
    .name          = TYPE_NUBUS_MACFB,
    .parent        = TYPE_NUBUS_DEVICE,
    .instance_size = sizeof(MacfbNubusState),
    .class_init    = macfb_nubus_class_init,
    .class_size    = sizeof(MacfbNubusDeviceClass),
};

static void macfb_register_types(void)
{
    type_register_static(&macfb_sysbus_info);
    type_register_static(&macfb_nubus_info);
}

type_init(macfb_register_types)
