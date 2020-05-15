/*
 * QEMU PCI VGA Emulator.
 *
 * see docs/specs/standard-vga.txt for virtual hardware specs.
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
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "vga_int.h"
#include "ui/pixel_ops.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/loader.h"
#include "hw/display/edid.h"

enum vga_pci_flags {
    PCI_VGA_FLAG_ENABLE_MMIO = 1,
    PCI_VGA_FLAG_ENABLE_QEXT = 2,
    PCI_VGA_FLAG_ENABLE_EDID = 3,
};

typedef struct PCIVGAState {
    PCIDevice dev;
    VGACommonState vga;
    uint32_t flags;
    qemu_edid_info edid_info;
    MemoryRegion mmio;
    MemoryRegion mrs[4];
    uint8_t edid[256];
} PCIVGAState;

#define TYPE_PCI_VGA "pci-vga"
#define PCI_VGA(obj) OBJECT_CHECK(PCIVGAState, (obj), TYPE_PCI_VGA)

static const VMStateDescription vmstate_vga_pci = {
    .name = "vga",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCIVGAState),
        VMSTATE_STRUCT(vga, PCIVGAState, 0, vmstate_vga_common, VGACommonState),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t pci_vga_ioport_read(void *ptr, hwaddr addr,
                                    unsigned size)
{
    VGACommonState *s = ptr;
    uint64_t ret = 0;

    switch (size) {
    case 1:
        ret = vga_ioport_read(s, addr + 0x3c0);
        break;
    case 2:
        ret  = vga_ioport_read(s, addr + 0x3c0);
        ret |= vga_ioport_read(s, addr + 0x3c1) << 8;
        break;
    }
    return ret;
}

static void pci_vga_ioport_write(void *ptr, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    VGACommonState *s = ptr;

    switch (size) {
    case 1:
        vga_ioport_write(s, addr + 0x3c0, val);
        break;
    case 2:
        /*
         * Update bytes in little endian order.  Allows to update
         * indexed registers with a single word write because the
         * index byte is updated first.
         */
        vga_ioport_write(s, addr + 0x3c0, val & 0xff);
        vga_ioport_write(s, addr + 0x3c1, (val >> 8) & 0xff);
        break;
    }
}

static const MemoryRegionOps pci_vga_ioport_ops = {
    .read = pci_vga_ioport_read,
    .write = pci_vga_ioport_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t pci_vga_bochs_read(void *ptr, hwaddr addr,
                                   unsigned size)
{
    VGACommonState *s = ptr;
    int index = addr >> 1;

    vbe_ioport_write_index(s, 0, index);
    return vbe_ioport_read_data(s, 0);
}

static void pci_vga_bochs_write(void *ptr, hwaddr addr,
                                uint64_t val, unsigned size)
{
    VGACommonState *s = ptr;
    int index = addr >> 1;

    vbe_ioport_write_index(s, 0, index);
    vbe_ioport_write_data(s, 0, val);
}

static const MemoryRegionOps pci_vga_bochs_ops = {
    .read = pci_vga_bochs_read,
    .write = pci_vga_bochs_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 2,
    .impl.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t pci_vga_qext_read(void *ptr, hwaddr addr, unsigned size)
{
    VGACommonState *s = ptr;

    switch (addr) {
    case PCI_VGA_QEXT_REG_SIZE:
        return PCI_VGA_QEXT_SIZE;
    case PCI_VGA_QEXT_REG_BYTEORDER:
        return s->big_endian_fb ?
            PCI_VGA_QEXT_BIG_ENDIAN : PCI_VGA_QEXT_LITTLE_ENDIAN;
    default:
        return 0;
    }
}

static void pci_vga_qext_write(void *ptr, hwaddr addr,
                               uint64_t val, unsigned size)
{
    VGACommonState *s = ptr;

    switch (addr) {
    case PCI_VGA_QEXT_REG_BYTEORDER:
        if (val == PCI_VGA_QEXT_BIG_ENDIAN) {
            s->big_endian_fb = true;
        }
        if (val == PCI_VGA_QEXT_LITTLE_ENDIAN) {
            s->big_endian_fb = false;
        }
        break;
    }
}

static bool vga_get_big_endian_fb(Object *obj, Error **errp)
{
    PCIVGAState *d = PCI_VGA(PCI_DEVICE(obj));

    return d->vga.big_endian_fb;
}

static void vga_set_big_endian_fb(Object *obj, bool value, Error **errp)
{
    PCIVGAState *d = PCI_VGA(PCI_DEVICE(obj));

    d->vga.big_endian_fb = value;
}

static const MemoryRegionOps pci_vga_qext_ops = {
    .read = pci_vga_qext_read,
    .write = pci_vga_qext_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void pci_std_vga_mmio_region_init(VGACommonState *s,
                                  Object *owner,
                                  MemoryRegion *parent,
                                  MemoryRegion *subs,
                                  bool qext, bool edid)
{
    PCIVGAState *d = container_of(s, PCIVGAState, vga);

    memory_region_init_io(&subs[0], owner, &pci_vga_ioport_ops, s,
                          "vga ioports remapped", PCI_VGA_IOPORT_SIZE);
    memory_region_add_subregion(parent, PCI_VGA_IOPORT_OFFSET,
                                &subs[0]);

    memory_region_init_io(&subs[1], owner, &pci_vga_bochs_ops, s,
                          "bochs dispi interface", PCI_VGA_BOCHS_SIZE);
    memory_region_add_subregion(parent, PCI_VGA_BOCHS_OFFSET,
                                &subs[1]);

    if (qext) {
        memory_region_init_io(&subs[2], owner, &pci_vga_qext_ops, s,
                              "qemu extended regs", PCI_VGA_QEXT_SIZE);
        memory_region_add_subregion(parent, PCI_VGA_QEXT_OFFSET,
                                    &subs[2]);
    }

    if (edid) {
        qemu_edid_generate(d->edid, sizeof(d->edid), &d->edid_info);
        qemu_edid_region_io(&subs[3], owner, d->edid, sizeof(d->edid));
        memory_region_add_subregion(parent, 0, &subs[3]);
    }
}

static void pci_std_vga_realize(PCIDevice *dev, Error **errp)
{
    PCIVGAState *d = PCI_VGA(dev);
    VGACommonState *s = &d->vga;
    bool qext = false;
    bool edid = false;

    /* vga + console init */
    vga_common_init(s, OBJECT(dev));
    vga_init(s, OBJECT(dev), pci_address_space(dev), pci_address_space_io(dev),
             true);

    s->con = graphic_console_init(DEVICE(dev), 0, s->hw_ops, s);

    /* XXX: VGA_RAM_SIZE must be a power of two */
    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &s->vram);

    /* mmio bar for vga register access */
    if (d->flags & (1 << PCI_VGA_FLAG_ENABLE_MMIO)) {
        memory_region_init_io(&d->mmio, OBJECT(dev), &unassigned_io_ops, NULL,
                              "vga.mmio", PCI_VGA_MMIO_SIZE);

        if (d->flags & (1 << PCI_VGA_FLAG_ENABLE_QEXT)) {
            qext = true;
            pci_set_byte(&d->dev.config[PCI_REVISION_ID], 2);
        }
        if (d->flags & (1 << PCI_VGA_FLAG_ENABLE_EDID)) {
            edid = true;
        }
        pci_std_vga_mmio_region_init(s, OBJECT(dev), &d->mmio, d->mrs,
                                     qext, edid);

        pci_register_bar(&d->dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
    }
}

static void pci_std_vga_init(Object *obj)
{
    /* Expose framebuffer byteorder via QOM */
    object_property_add_bool(obj, "big-endian-framebuffer",
                             vga_get_big_endian_fb, vga_set_big_endian_fb);
}

static void pci_secondary_vga_realize(PCIDevice *dev, Error **errp)
{
    PCIVGAState *d = PCI_VGA(dev);
    VGACommonState *s = &d->vga;
    bool qext = false;
    bool edid = false;

    /* vga + console init */
    vga_common_init(s, OBJECT(dev));
    s->con = graphic_console_init(DEVICE(dev), 0, s->hw_ops, s);

    /* mmio bar */
    memory_region_init_io(&d->mmio, OBJECT(dev), &unassigned_io_ops, NULL,
                          "vga.mmio", PCI_VGA_MMIO_SIZE);

    if (d->flags & (1 << PCI_VGA_FLAG_ENABLE_QEXT)) {
        qext = true;
        pci_set_byte(&d->dev.config[PCI_REVISION_ID], 2);
    }
    if (d->flags & (1 << PCI_VGA_FLAG_ENABLE_EDID)) {
        edid = true;
    }
    pci_std_vga_mmio_region_init(s, OBJECT(dev), &d->mmio, d->mrs, qext, edid);

    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &s->vram);
    pci_register_bar(&d->dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
}

static void pci_secondary_vga_exit(PCIDevice *dev)
{
    PCIVGAState *d = PCI_VGA(dev);
    VGACommonState *s = &d->vga;

    graphic_console_close(s->con);
    memory_region_del_subregion(&d->mmio, &d->mrs[0]);
    memory_region_del_subregion(&d->mmio, &d->mrs[1]);
    if (d->flags & (1 << PCI_VGA_FLAG_ENABLE_QEXT)) {
        memory_region_del_subregion(&d->mmio, &d->mrs[2]);
    }
    if (d->flags & (1 << PCI_VGA_FLAG_ENABLE_EDID)) {
        memory_region_del_subregion(&d->mmio, &d->mrs[3]);
    }
}

static void pci_secondary_vga_init(Object *obj)
{
    /* Expose framebuffer byteorder via QOM */
    object_property_add_bool(obj, "big-endian-framebuffer",
                             vga_get_big_endian_fb, vga_set_big_endian_fb);
}

static void pci_secondary_vga_reset(DeviceState *dev)
{
    PCIVGAState *d = PCI_VGA(PCI_DEVICE(dev));
    vga_common_reset(&d->vga);
}

static Property vga_pci_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", PCIVGAState, vga.vram_size_mb, 16),
    DEFINE_PROP_BIT("mmio", PCIVGAState, flags, PCI_VGA_FLAG_ENABLE_MMIO, true),
    DEFINE_PROP_BIT("qemu-extended-regs",
                    PCIVGAState, flags, PCI_VGA_FLAG_ENABLE_QEXT, true),
    DEFINE_PROP_BIT("edid",
                    PCIVGAState, flags, PCI_VGA_FLAG_ENABLE_EDID, true),
    DEFINE_EDID_PROPERTIES(PCIVGAState, edid_info),
    DEFINE_PROP_BOOL("global-vmstate", PCIVGAState, vga.global_vmstate, false),
    DEFINE_PROP_END_OF_LIST(),
};

static Property secondary_pci_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", PCIVGAState, vga.vram_size_mb, 16),
    DEFINE_PROP_BIT("qemu-extended-regs",
                    PCIVGAState, flags, PCI_VGA_FLAG_ENABLE_QEXT, true),
    DEFINE_PROP_BIT("edid",
                    PCIVGAState, flags, PCI_VGA_FLAG_ENABLE_EDID, true),
    DEFINE_EDID_PROPERTIES(PCIVGAState, edid_info),
    DEFINE_PROP_END_OF_LIST(),
};

static void vga_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = PCI_DEVICE_ID_QEMU_VGA;
    dc->vmsd = &vmstate_vga_pci;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo vga_pci_type_info = {
    .name = TYPE_PCI_VGA,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIVGAState),
    .abstract = true,
    .class_init = vga_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void vga_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_std_vga_realize;
    k->romfile = "vgabios-stdvga.bin";
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    device_class_set_props(dc, vga_pci_properties);
    dc->hotpluggable = false;
}

static void secondary_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_secondary_vga_realize;
    k->exit = pci_secondary_vga_exit;
    k->class_id = PCI_CLASS_DISPLAY_OTHER;
    device_class_set_props(dc, secondary_pci_properties);
    dc->reset = pci_secondary_vga_reset;
}

static const TypeInfo vga_info = {
    .name          = "VGA",
    .parent        = TYPE_PCI_VGA,
    .instance_init = pci_std_vga_init,
    .class_init    = vga_class_init,
};

static const TypeInfo secondary_info = {
    .name          = "secondary-vga",
    .parent        = TYPE_PCI_VGA,
    .instance_init = pci_secondary_vga_init,
    .class_init    = secondary_class_init,
};

static void vga_register_types(void)
{
    type_register_static(&vga_pci_type_info);
    type_register_static(&vga_info);
    type_register_static(&secondary_info);
}

type_init(vga_register_types)
