
/*
 *  QEMU model of the Milkymist VGA framebuffer.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Specification available at:
 *   http://www.milkymist.org/socdoc/vgafb.pdf
 */

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "console.h"
#include "framebuffer.h"
#include "pixel_ops.h"
#include "qemu-error.h"

#define BITS 8
#include "milkymist-vgafb_template.h"
#define BITS 15
#include "milkymist-vgafb_template.h"
#define BITS 16
#include "milkymist-vgafb_template.h"
#define BITS 24
#include "milkymist-vgafb_template.h"
#define BITS 32
#include "milkymist-vgafb_template.h"

enum {
    R_CTRL = 0,
    R_HRES,
    R_HSYNC_START,
    R_HSYNC_END,
    R_HSCAN,
    R_VRES,
    R_VSYNC_START,
    R_VSYNC_END,
    R_VSCAN,
    R_BASEADDRESS,
    R_BASEADDRESS_ACT,
    R_BURST_COUNT,
    R_SOURCE_CLOCK,
    R_MAX
};

enum {
    CTRL_RESET = (1<<0),
};

struct MilkymistVgafbState {
    SysBusDevice busdev;
    DisplayState *ds;

    int invalidate;
    uint32_t fb_offset;
    uint32_t fb_mask;

    uint32_t regs[R_MAX];
};
typedef struct MilkymistVgafbState MilkymistVgafbState;

static int vgafb_enabled(MilkymistVgafbState *s)
{
    return !(s->regs[R_CTRL] & CTRL_RESET);
}

static void vgafb_update_display(void *opaque)
{
    MilkymistVgafbState *s = opaque;
    int first = 0;
    int last = 0;
    drawfn fn;

    if (!vgafb_enabled(s)) {
        return;
    }

    int dest_width = s->regs[R_HRES];

    switch (ds_get_bits_per_pixel(s->ds)) {
    case 0:
        return;
    case 8:
        fn = draw_line_8;
        break;
    case 15:
        fn = draw_line_15;
        dest_width *= 2;
        break;
    case 16:
        fn = draw_line_16;
        dest_width *= 2;
        break;
    case 24:
        fn = draw_line_24;
        dest_width *= 3;
        break;
    case 32:
        fn = draw_line_32;
        dest_width *= 4;
        break;
    default:
        hw_error("milkymist_vgafb: bad color depth\n");
        break;
    }

    framebuffer_update_display(s->ds,
                               s->regs[R_BASEADDRESS] + s->fb_offset,
                               s->regs[R_HRES],
                               s->regs[R_VRES],
                               s->regs[R_HRES] * 2,
                               dest_width,
                               0,
                               s->invalidate,
                               fn,
                               NULL,
                               &first, &last);

    if (first >= 0) {
        dpy_update(s->ds, 0, first, s->regs[R_HRES], last - first + 1);
    }
    s->invalidate = 0;
}

static void vgafb_invalidate_display(void *opaque)
{
    MilkymistVgafbState *s = opaque;
    s->invalidate = 1;
}

static void vgafb_resize(MilkymistVgafbState *s)
{
    if (!vgafb_enabled(s)) {
        return;
    }

    qemu_console_resize(s->ds, s->regs[R_HRES], s->regs[R_VRES]);
    s->invalidate = 1;
}

static uint32_t vgafb_read(void *opaque, target_phys_addr_t addr)
{
    MilkymistVgafbState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_CTRL:
    case R_HRES:
    case R_HSYNC_START:
    case R_HSYNC_END:
    case R_HSCAN:
    case R_VRES:
    case R_VSYNC_START:
    case R_VSYNC_END:
    case R_VSCAN:
    case R_BASEADDRESS:
    case R_BURST_COUNT:
    case R_SOURCE_CLOCK:
        r = s->regs[addr];
    break;
    case R_BASEADDRESS_ACT:
        r = s->regs[R_BASEADDRESS];
    break;

    default:
        error_report("milkymist_vgafb: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_vgafb_memory_read(addr << 2, r);

    return r;
}

static void
vgafb_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    MilkymistVgafbState *s = opaque;

    trace_milkymist_vgafb_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_CTRL:
        s->regs[addr] = value;
        vgafb_resize(s);
        break;
    case R_HSYNC_START:
    case R_HSYNC_END:
    case R_HSCAN:
    case R_VSYNC_START:
    case R_VSYNC_END:
    case R_VSCAN:
    case R_BURST_COUNT:
    case R_SOURCE_CLOCK:
        s->regs[addr] = value;
        break;
    case R_BASEADDRESS:
        if (value & 0x1f) {
            error_report("milkymist_vgafb: framebuffer base address have to "
                     "be 32 byte aligned");
            break;
        }
        s->regs[addr] = value & s->fb_mask;
        s->invalidate = 1;
        break;
    case R_HRES:
    case R_VRES:
        s->regs[addr] = value;
        vgafb_resize(s);
        break;
    case R_BASEADDRESS_ACT:
        error_report("milkymist_vgafb: write to read-only register 0x"
                TARGET_FMT_plx, addr << 2);
        break;

    default:
        error_report("milkymist_vgafb: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static CPUReadMemoryFunc * const vgafb_read_fn[] = {
   NULL,
   NULL,
   &vgafb_read
};

static CPUWriteMemoryFunc * const vgafb_write_fn[] = {
   NULL,
   NULL,
   &vgafb_write
};

static void milkymist_vgafb_reset(DeviceState *d)
{
    MilkymistVgafbState *s = container_of(d, MilkymistVgafbState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }

    /* defaults */
    s->regs[R_CTRL] = CTRL_RESET;
    s->regs[R_HRES] = 640;
    s->regs[R_VRES] = 480;
    s->regs[R_BASEADDRESS] = 0;
}

static int milkymist_vgafb_init(SysBusDevice *dev)
{
    MilkymistVgafbState *s = FROM_SYSBUS(typeof(*s), dev);
    int vgafb_regs;

    vgafb_regs = cpu_register_io_memory(vgafb_read_fn, vgafb_write_fn, s,
            DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4, vgafb_regs);

    s->ds = graphic_console_init(vgafb_update_display,
                                 vgafb_invalidate_display,
                                 NULL, NULL, s);

    return 0;
}

static int vgafb_post_load(void *opaque, int version_id)
{
    vgafb_invalidate_display(opaque);
    return 0;
}

static const VMStateDescription vmstate_milkymist_vgafb = {
    .name = "milkymist-vgafb",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = vgafb_post_load,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistVgafbState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo milkymist_vgafb_info = {
    .init = milkymist_vgafb_init,
    .qdev.name  = "milkymist-vgafb",
    .qdev.size  = sizeof(MilkymistVgafbState),
    .qdev.vmsd  = &vmstate_milkymist_vgafb,
    .qdev.reset = milkymist_vgafb_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("fb_offset", MilkymistVgafbState, fb_offset, 0x0),
        DEFINE_PROP_UINT32("fb_mask", MilkymistVgafbState, fb_mask, 0xffffffff),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void milkymist_vgafb_register(void)
{
    sysbus_register_withprop(&milkymist_vgafb_info);
}

device_init(milkymist_vgafb_register)
