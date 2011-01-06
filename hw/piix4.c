/*
 * QEMU PIIX4 PCI Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "pc.h"
#include "pci.h"
#include "isa.h"
#include "sysbus.h"

PCIDevice *piix4_dev;

static void piix4_reset(void *opaque)
{
    PCIDevice *d = opaque;
    uint8_t *pci_conf = d->config;

    pci_conf[0x04] = 0x07; // master, memory and I/O
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x0a; // PCI A -> IRQ 10
    pci_conf[0x61] = 0x0a; // PCI B -> IRQ 10
    pci_conf[0x62] = 0x0b; // PCI C -> IRQ 11
    pci_conf[0x63] = 0x0b; // PCI D -> IRQ 11
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;
}

static void piix_save(QEMUFile* f, void *opaque)
{
    PCIDevice *d = opaque;
    pci_device_save(d, f);
}

static int piix_load(QEMUFile* f, void *opaque, int version_id)
{
    PCIDevice *d = opaque;
    if (version_id != 2)
        return -EINVAL;
    return pci_device_load(d, f);
}

static int piix4_initfn(PCIDevice *d)
{
    uint8_t *pci_conf;

    isa_bus_new(&d->qdev);
    register_savevm(&d->qdev, "PIIX4", 0, 2, piix_save, piix_load, d);

    pci_conf = d->config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82371AB_0); // 82371AB/EB/MB PIIX4 PCI-to-ISA bridge
    pci_config_set_class(pci_conf, PCI_CLASS_BRIDGE_ISA);

    piix4_dev = d;
    qemu_register_reset(piix4_reset, d);
    return 0;
}

int piix4_init(PCIBus *bus, int devfn)
{
    PCIDevice *d;

    d = pci_create_simple_multifunction(bus, devfn, true, "PIIX4");
    return d->devfn;
}

static PCIDeviceInfo piix4_info[] = {
    {
        .qdev.name    = "PIIX4",
        .qdev.desc    = "ISA bridge",
        .qdev.size    = sizeof(PCIDevice),
        .qdev.no_user = 1,
        .no_hotplug   = 1,
        .init         = piix4_initfn,
    },{
        /* end of list */
    }
};

static void piix4_register(void)
{
    pci_qdev_register_many(piix4_info);
}
device_init(piix4_register);
