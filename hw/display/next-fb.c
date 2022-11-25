/*
 * NeXT Cube/Station Framebuffer Emulation
 *
 * Copyright (c) 2011 Bryce Lanham
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
#include "qapi/error.h"
#include "ui/console.h"
#include "hw/loader.h"
#include "framebuffer.h"
#include "ui/pixel_ops.h"
#include "hw/m68k/next-cube.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(NeXTFbState, NEXTFB)

struct NeXTFbState {
    SysBusDevice parent_obj;

    MemoryRegion fb_mr;
    MemoryRegionSection fbsection;
    QemuConsole *con;

    uint32_t cols;
    uint32_t rows;
    int invalidate;
};

static void nextfb_draw_line(void *opaque, uint8_t *d, const uint8_t *s,
                             int width, int pitch)
{
    NeXTFbState *nfbstate = NEXTFB(opaque);
    static const uint32_t pal[4] = {
        0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000
    };
    uint32_t *buf = (uint32_t *)d;
    int i = 0;

    for (i = 0; i < nfbstate->cols / 4; i++) {
        int j = i * 4;
        uint8_t src = s[i];
        buf[j + 3] = pal[src & 0x3];
        src >>= 2;
        buf[j + 2] = pal[src & 0x3];
        src >>= 2;
        buf[j + 1] = pal[src & 0x3];
        src >>= 2;
        buf[j + 0] = pal[src & 0x3];
    }
}

static void nextfb_update(void *opaque)
{
    NeXTFbState *s = NEXTFB(opaque);
    int dest_width = 4;
    int src_width;
    int first = 0;
    int last  = 0;
    DisplaySurface *surface = qemu_console_surface(s->con);

    src_width = s->cols / 4 + 8;
    dest_width = s->cols * 4;

    if (s->invalidate) {
        framebuffer_update_memory_section(&s->fbsection, &s->fb_mr, 0,
                                          s->cols, src_width);
        s->invalidate = 0;
    }

    framebuffer_update_display(surface, &s->fbsection, s->cols, s->rows,
                               src_width, dest_width, 0, 1, nextfb_draw_line,
                               s, &first, &last);

    dpy_gfx_update(s->con, 0, 0, s->cols, s->rows);
}

static void nextfb_invalidate(void *opaque)
{
    NeXTFbState *s = NEXTFB(opaque);
    s->invalidate = 1;
}

static const GraphicHwOps nextfb_ops = {
    .invalidate  = nextfb_invalidate,
    .gfx_update  = nextfb_update,
};

static void nextfb_realize(DeviceState *dev, Error **errp)
{
    NeXTFbState *s = NEXTFB(dev);

    memory_region_init_ram(&s->fb_mr, OBJECT(dev), "next-video", 0x1CB100,
                           &error_fatal);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->fb_mr);

    s->invalidate = 1;
    s->cols = 1120;
    s->rows = 832;

    s->con = graphic_console_init(dev, 0, &nextfb_ops, s);
    qemu_console_resize(s->con, s->cols, s->rows);
}

static void nextfb_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->realize = nextfb_realize;

    /* Note: This device does not have any state that we have to reset or migrate */
}

static const TypeInfo nextfb_info = {
    .name          = TYPE_NEXTFB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NeXTFbState),
    .class_init    = nextfb_class_init,
};

static void nextfb_register_types(void)
{
    type_register_static(&nextfb_info);
}

type_init(nextfb_register_types)
