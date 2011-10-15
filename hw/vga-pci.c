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
#include "loader.h"

typedef struct PCIVGAState {
    PCIDevice dev;
    VGACommonState vga;
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

static int pci_vga_initfn(PCIDevice *dev)
{
     PCIVGAState *d = DO_UPCAST(PCIVGAState, dev, dev);
     VGACommonState *s = &d->vga;

     // vga + console init
     vga_common_init(s, VGA_RAM_SIZE);
     vga_init(s, pci_address_space(dev), pci_address_space_io(dev), true);

     s->ds = graphic_console_init(s->update, s->invalidate,
                                  s->screen_dump, s->text_update, s);

     /* XXX: VGA_RAM_SIZE must be a power of two */
     pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &s->vram);

     if (!dev->rom_bar) {
         /* compatibility with pc-0.13 and older */
         vga_init_vbe(s, pci_address_space(dev));
     }

     return 0;
}

int pci_vga_init(PCIBus *bus)
{
    pci_create_simple(bus, -1, "VGA");
    return 0;
}

static PCIDeviceInfo vga_info = {
    .qdev.name    = "VGA",
    .qdev.size    = sizeof(PCIVGAState),
    .qdev.vmsd    = &vmstate_vga_pci,
    .no_hotplug   = 1,
    .init         = pci_vga_initfn,
    .romfile      = "vgabios-stdvga.bin",

    /* dummy VGA (same as Bochs ID) */
    .vendor_id    = PCI_VENDOR_ID_QEMU,
    .device_id    = PCI_DEVICE_ID_QEMU_VGA,
    .class_id     = PCI_CLASS_DISPLAY_VGA,
};

static void vga_register(void)
{
    pci_qdev_register(&vga_info);
}
device_init(vga_register);
