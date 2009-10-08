/*
 * QEMU PCI VGA Emulator.
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
#include "hw.h"
#include "console.h"
#include "pc.h"
#include "pci.h"
#include "vga_int.h"
#include "pixel_ops.h"
#include "qemu-timer.h"

typedef struct PCIVGAState {
    PCIDevice dev;
    VGACommonState vga;
} PCIVGAState;

static void pci_vga_save(QEMUFile *f, void *opaque)
{
    PCIVGAState *s = opaque;

    pci_device_save(&s->dev, f);
    vga_common_save(f, &s->vga);
}

static int pci_vga_load(QEMUFile *f, void *opaque, int version_id)
{
    PCIVGAState *s = opaque;
    int ret;

    if (version_id > 2)
        return -EINVAL;

    if (version_id >= 2) {
        ret = pci_device_load(&s->dev, f);
        if (ret < 0)
            return ret;
    }
    return vga_common_load(f, &s->vga, version_id);
}

static void vga_map(PCIDevice *pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    PCIVGAState *d = (PCIVGAState *)pci_dev;
    VGACommonState *s = &d->vga;
    if (region_num == PCI_ROM_SLOT) {
        cpu_register_physical_memory(addr, s->bios_size, s->bios_offset);
    } else {
        cpu_register_physical_memory(addr, s->vram_size, s->vram_offset);
        s->map_addr = addr;
        s->map_end = addr + s->vram_size;
        vga_dirty_log_start(s);
    }
}

static void pci_vga_write_config(PCIDevice *d,
                                 uint32_t address, uint32_t val, int len)
{
    PCIVGAState *pvs = container_of(d, PCIVGAState, dev);
    VGACommonState *s = &pvs->vga;

    pci_default_write_config(d, address, val, len);
    if (s->map_addr && pvs->dev.io_regions[0].addr == -1)
        s->map_addr = 0;
}

static int pci_vga_initfn(PCIDevice *dev)
{
     PCIVGAState *d = DO_UPCAST(PCIVGAState, dev, dev);
     VGACommonState *s = &d->vga;
     uint8_t *pci_conf = d->dev.config;

     // vga + console init
     vga_common_init(s, VGA_RAM_SIZE);
     vga_init(s);
     register_savevm("vga", 0, 2, pci_vga_save, pci_vga_load, d);

     s->ds = graphic_console_init(s->update, s->invalidate,
                                  s->screen_dump, s->text_update, s);

     // dummy VGA (same as Bochs ID)
     pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_QEMU);
     pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_QEMU_VGA);
     pci_config_set_class(pci_conf, PCI_CLASS_DISPLAY_VGA);
     pci_conf[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type

     /* XXX: VGA_RAM_SIZE must be a power of two */
     pci_register_bar(&d->dev, 0, VGA_RAM_SIZE,
                      PCI_ADDRESS_SPACE_MEM_PREFETCH, vga_map);

     if (s->bios_size) {
        unsigned int bios_total_size;
        /* must be a power of two */
        bios_total_size = 1;
        while (bios_total_size < s->bios_size)
            bios_total_size <<= 1;
        pci_register_bar(&d->dev, PCI_ROM_SLOT, bios_total_size,
                         PCI_ADDRESS_SPACE_MEM_PREFETCH, vga_map);
     }
     return 0;
}

int pci_vga_init(PCIBus *bus,
                 unsigned long vga_bios_offset, int vga_bios_size)
{
    PCIDevice *dev;

    dev = pci_create(bus, -1, "VGA");
    qdev_prop_set_uint32(&dev->qdev, "bios-offset", vga_bios_offset);
    qdev_prop_set_uint32(&dev->qdev, "bios-size", vga_bios_offset);
    qdev_init_nofail(&dev->qdev);

    return 0;
}

static PCIDeviceInfo vga_info = {
    .qdev.name    = "VGA",
    .qdev.size    = sizeof(PCIVGAState),
    .init         = pci_vga_initfn,
    .config_write = pci_vga_write_config,
    .qdev.props   = (Property[]) {
        DEFINE_PROP_HEX32("bios-offset", PCIVGAState, vga.bios_offset, 0),
        DEFINE_PROP_HEX32("bios-size",   PCIVGAState, vga.bios_size,   0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void vga_register(void)
{
    pci_qdev_register(&vga_info);
}
device_init(vga_register);
