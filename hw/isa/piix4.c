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

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/isa/isa.h"
#include "hw/sysbus.h"

PCIDevice *piix4_dev;

typedef struct PIIX4State {
    PCIDevice dev;
} PIIX4State;

#define TYPE_PIIX4_PCI_DEVICE "PIIX4"
#define PIIX4_PCI_DEVICE(obj) \
    OBJECT_CHECK(PIIX4State, (obj), TYPE_PIIX4_PCI_DEVICE)

static void piix4_reset(void *opaque)
{
    PIIX4State *d = opaque;
    uint8_t *pci_conf = d->dev.config;

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

static const VMStateDescription vmstate_piix4 = {
    .name = "PIIX4",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PIIX4State),
        VMSTATE_END_OF_LIST()
    }
};

static void piix4_realize(PCIDevice *dev, Error **errp)
{
    PIIX4State *d = PIIX4_PCI_DEVICE(dev);

    if (!isa_bus_new(DEVICE(d), pci_address_space(dev),
                     pci_address_space_io(dev), errp)) {
        return;
    }
    piix4_dev = &d->dev;
    qemu_register_reset(piix4_reset, d);
}

int piix4_init(PCIBus *bus, ISABus **isa_bus, int devfn)
{
    PCIDevice *d;

    d = pci_create_simple_multifunction(bus, devfn, true, "PIIX4");
    *isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(d), "isa.0"));
    return d->devfn;
}

static void piix4_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = piix4_realize;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82371AB_0;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    dc->desc = "ISA bridge";
    dc->vmsd = &vmstate_piix4;
    /*
     * Reason: part of PIIX4 southbridge, needs to be wired up,
     * e.g. by mips_malta_init()
     */
    dc->cannot_instantiate_with_device_add_yet = true;
    dc->hotpluggable = false;
}

static const TypeInfo piix4_info = {
    .name          = TYPE_PIIX4_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIX4State),
    .class_init    = piix4_class_init,
};

static void piix4_register_types(void)
{
    type_register_static(&piix4_info);
}

type_init(piix4_register_types)
