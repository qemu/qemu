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
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/pci/pci.h"
#include "vga_int.h"
#include "ui/pixel_ops.h"
#include "qemu/timer.h"
#include "hw/loader.h"

#define PCI_VGA_IOPORT_OFFSET 0x400
#define PCI_VGA_IOPORT_SIZE   (0x3e0 - 0x3c0)
#define PCI_VGA_BOCHS_OFFSET  0x500
#define PCI_VGA_BOCHS_SIZE    (0x0b * 2)
#define PCI_VGA_MMIO_SIZE     0x1000

enum vga_pci_flags {
    PCI_VGA_FLAG_ENABLE_MMIO = 1,
};

typedef struct PCIVGAState {
    PCIDevice dev;
    VGACommonState vga;
    uint32_t flags;
    MemoryRegion mmio;
    MemoryRegion ioport;
    MemoryRegion bochs;
} PCIVGAState;

static const VMStateDescription vmstate_vga_pci = {
    .name = "vga",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PCIVGAState),
        VMSTATE_STRUCT(vga, PCIVGAState, 0, vmstate_vga_common, VGACommonState),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t pci_vga_ioport_read(void *ptr, hwaddr addr,
                                    unsigned size)
{
    PCIVGAState *d = ptr;
    uint64_t ret = 0;

    switch (size) {
    case 1:
        ret = vga_ioport_read(&d->vga, addr);
        break;
    case 2:
        ret  = vga_ioport_read(&d->vga, addr);
        ret |= vga_ioport_read(&d->vga, addr+1) << 8;
        break;
    }
    return ret;
}

static void pci_vga_ioport_write(void *ptr, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PCIVGAState *d = ptr;

    switch (size) {
    case 1:
        vga_ioport_write(&d->vga, addr + 0x3c0, val);
        break;
    case 2:
        /*
         * Update bytes in little endian order.  Allows to update
         * indexed registers with a single word write because the
         * index byte is updated first.
         */
        vga_ioport_write(&d->vga, addr + 0x3c0, val & 0xff);
        vga_ioport_write(&d->vga, addr + 0x3c1, (val >> 8) & 0xff);
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
    PCIVGAState *d = ptr;
    int index = addr >> 1;

    vbe_ioport_write_index(&d->vga, 0, index);
    return vbe_ioport_read_data(&d->vga, 0);
}

static void pci_vga_bochs_write(void *ptr, hwaddr addr,
                                uint64_t val, unsigned size)
{
    PCIVGAState *d = ptr;
    int index = addr >> 1;

    vbe_ioport_write_index(&d->vga, 0, index);
    vbe_ioport_write_data(&d->vga, 0, val);
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

static int pci_std_vga_initfn(PCIDevice *dev)
{
    PCIVGAState *d = DO_UPCAST(PCIVGAState, dev, dev);
    VGACommonState *s = &d->vga;

    /* vga + console init */
    vga_common_init(s, OBJECT(dev));
    vga_init(s, OBJECT(dev), pci_address_space(dev), pci_address_space_io(dev),
             true);

    s->con = graphic_console_init(DEVICE(dev), 0, s->hw_ops, s);

    /* XXX: VGA_RAM_SIZE must be a power of two */
    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &s->vram);

    /* mmio bar for vga register access */
    if (d->flags & (1 << PCI_VGA_FLAG_ENABLE_MMIO)) {
        memory_region_init(&d->mmio, NULL, "vga.mmio", 4096);
        memory_region_init_io(&d->ioport, NULL, &pci_vga_ioport_ops, d,
                              "vga ioports remapped", PCI_VGA_IOPORT_SIZE);
        memory_region_init_io(&d->bochs, NULL, &pci_vga_bochs_ops, d,
                              "bochs dispi interface", PCI_VGA_BOCHS_SIZE);

        memory_region_add_subregion(&d->mmio, PCI_VGA_IOPORT_OFFSET,
                                    &d->ioport);
        memory_region_add_subregion(&d->mmio, PCI_VGA_BOCHS_OFFSET,
                                    &d->bochs);
        pci_register_bar(&d->dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
    }

    if (!dev->rom_bar) {
        /* compatibility with pc-0.13 and older */
        vga_init_vbe(s, OBJECT(dev), pci_address_space(dev));
    }

    return 0;
}

static Property vga_pci_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", PCIVGAState, vga.vram_size_mb, 16),
    DEFINE_PROP_BIT("mmio", PCIVGAState, flags, PCI_VGA_FLAG_ENABLE_MMIO, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void vga_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = pci_std_vga_initfn;
    k->romfile = "vgabios-stdvga.bin";
    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = PCI_DEVICE_ID_QEMU_VGA;
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    dc->vmsd = &vmstate_vga_pci;
    dc->props = vga_pci_properties;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo vga_info = {
    .name          = "VGA",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIVGAState),
    .class_init    = vga_class_init,
};

static void vga_register_types(void)
{
    type_register_static(&vga_info);
}

type_init(vga_register_types)
