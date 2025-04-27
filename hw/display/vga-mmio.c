/*
 * QEMU MMIO VGA Emulator.
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
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/display/vga.h"
#include "hw/qdev-properties.h"
#include "ui/console.h"
#include "vga_int.h"

/*
 * QEMU interface:
 *  + sysbus MMIO region 0: VGA I/O registers
 *  + sysbus MMIO region 1: VGA MMIO registers
 *  + sysbus MMIO region 2: VGA memory
 */

OBJECT_DECLARE_SIMPLE_TYPE(VGAMmioState, VGA_MMIO)

struct VGAMmioState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    VGACommonState vga;
    MemoryRegion iomem;
    MemoryRegion lowmem;

    uint8_t it_shift;
};

static uint64_t vga_mm_read(void *opaque, hwaddr addr, unsigned size)
{
    VGAMmioState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift) &
        MAKE_64BIT_MASK(0, size * 8);
}

static void vga_mm_write(void *opaque, hwaddr addr, uint64_t value,
                         unsigned size)
{
    VGAMmioState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift,
                     value & MAKE_64BIT_MASK(0, size * 8));
}

static const MemoryRegionOps vga_mm_ctrl_ops = {
    .read = vga_mm_read,
    .write = vga_mm_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void vga_mmio_reset(DeviceState *dev)
{
    VGAMmioState *s = VGA_MMIO(dev);

    vga_common_reset(&s->vga);
}

static void vga_mmio_realizefn(DeviceState *dev, Error **errp)
{
    VGAMmioState *s = VGA_MMIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &vga_mm_ctrl_ops, s,
                          "vga-mmio", 0x100000);
    memory_region_set_flush_coalesced(&s->iomem);
    sysbus_init_mmio(sbd, &s->iomem);

    /* XXX: endianness? */
    memory_region_init_io(&s->lowmem, OBJECT(dev), &vga_mem_ops, &s->vga,
                          "vga-lowmem", 0x20000);
    memory_region_set_coalescing(&s->lowmem);
    sysbus_init_mmio(sbd, &s->lowmem);

    s->vga.bank_offset = 0;
    s->vga.global_vmstate = true;
    if (!vga_common_init(&s->vga, OBJECT(dev), errp)) {
        return;
    }

    sysbus_init_mmio(sbd, &s->vga.vram);
    s->vga.con = graphic_console_init(dev, 0, s->vga.hw_ops, &s->vga);
}

static const Property vga_mmio_properties[] = {
    DEFINE_PROP_UINT8("it_shift", VGAMmioState, it_shift, 0),
    DEFINE_PROP_UINT32("vgamem_mb", VGAMmioState, vga.vram_size_mb, 8),
};

static void vga_mmio_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vga_mmio_realizefn;
    device_class_set_legacy_reset(dc, vga_mmio_reset);
    dc->vmsd = &vmstate_vga_common;
    device_class_set_props(dc, vga_mmio_properties);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo vga_mmio_info = {
    .name          = TYPE_VGA_MMIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VGAMmioState),
    .class_init    = vga_mmio_class_initfn,
};

static void vga_mmio_register_types(void)
{
    type_register_static(&vga_mmio_info);
}

type_init(vga_mmio_register_types)
