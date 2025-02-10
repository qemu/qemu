/*
 * QEMU ISA VGA Emulator.
 *
 * see docs/specs/standard-vga.rst for virtual hardware specs.
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include "hw/isa/isa.h"
#include "vga_int.h"
#include "ui/pixel_ops.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "ui/console.h"
#include "qom/object.h"

#define TYPE_ISA_VGA "isa-vga"
OBJECT_DECLARE_SIMPLE_TYPE(ISAVGAState, ISA_VGA)

struct ISAVGAState {
    ISADevice parent_obj;

    struct VGACommonState state;
    PortioList portio_vga;
    PortioList portio_vbe;
};

static void vga_isa_reset(DeviceState *dev)
{
    ISAVGAState *d = ISA_VGA(dev);
    VGACommonState *s = &d->state;

    vga_common_reset(s);
}

static void vga_isa_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAVGAState *d = ISA_VGA(dev);
    VGACommonState *s = &d->state;
    MemoryRegion *vga_io_memory;
    const MemoryRegionPortio *vga_ports, *vbe_ports;

    s->global_vmstate = true;
    if (!vga_common_init(s, OBJECT(dev), errp)) {
        return;
    }

    s->legacy_address_space = isa_address_space(isadev);
    vga_io_memory = vga_init_io(s, OBJECT(dev), &vga_ports, &vbe_ports);
    isa_register_portio_list(isadev, &d->portio_vga,
                             0x3b0, vga_ports, s, "vga");
    if (vbe_ports) {
        isa_register_portio_list(isadev, &d->portio_vbe,
                                 0x1ce, vbe_ports, s, "vbe");
    }
    memory_region_add_subregion_overlap(isa_address_space(isadev),
                                        0x000a0000,
                                        vga_io_memory, 1);
    memory_region_set_coalescing(vga_io_memory);
    s->con = graphic_console_init(dev, 0, s->hw_ops, s);

    memory_region_add_subregion(isa_address_space(isadev),
                                VBE_DISPI_LFB_PHYSICAL_ADDRESS,
                                &s->vram);
    /* ROM BIOS */
    rom_add_vga(VGABIOS_FILENAME);
}

static const Property vga_isa_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", ISAVGAState, state.vram_size_mb, 8),
};

static void vga_isa_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vga_isa_realizefn;
    device_class_set_legacy_reset(dc, vga_isa_reset);
    dc->vmsd = &vmstate_vga_common;
    device_class_set_props(dc, vga_isa_properties);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo vga_isa_info = {
    .name          = TYPE_ISA_VGA,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAVGAState),
    .class_init    = vga_isa_class_initfn,
};

static void vga_isa_register_types(void)
{
    type_register_static(&vga_isa_info);
}

type_init(vga_isa_register_types)
