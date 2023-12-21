/*
 * QEMU 16550A UART emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
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

/* see docs/specs/pci-serial.rst */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/char/serial.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qom/object.h"

struct PCISerialState {
    PCIDevice dev;
    SerialState state;
    uint8_t prog_if;
};

#define TYPE_PCI_SERIAL "pci-serial"
OBJECT_DECLARE_SIMPLE_TYPE(PCISerialState, PCI_SERIAL)

static void serial_pci_realize(PCIDevice *dev, Error **errp)
{
    PCISerialState *pci = DO_UPCAST(PCISerialState, dev, dev);
    SerialState *s = &pci->state;

    if (!qdev_realize(DEVICE(s), NULL, errp)) {
        return;
    }

    pci->dev.config[PCI_CLASS_PROG] = pci->prog_if;
    pci->dev.config[PCI_INTERRUPT_PIN] = 0x01;
    s->irq = pci_allocate_irq(&pci->dev);

    memory_region_init_io(&s->io, OBJECT(pci), &serial_io_ops, s, "serial", 8);
    pci_register_bar(&pci->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
}

static void serial_pci_exit(PCIDevice *dev)
{
    PCISerialState *pci = DO_UPCAST(PCISerialState, dev, dev);
    SerialState *s = &pci->state;

    qdev_unrealize(DEVICE(s));
    qemu_free_irq(s->irq);
}

static const VMStateDescription vmstate_pci_serial = {
    .name = "pci-serial",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCISerialState),
        VMSTATE_STRUCT(state, PCISerialState, 0, vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

static Property serial_pci_properties[] = {
    DEFINE_PROP_UINT8("prog_if",  PCISerialState, prog_if, 0x02),
    DEFINE_PROP_END_OF_LIST(),
};

static void serial_pci_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->realize = serial_pci_realize;
    pc->exit = serial_pci_exit;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_SERIAL;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_COMMUNICATION_SERIAL;
    dc->vmsd = &vmstate_pci_serial;
    device_class_set_props(dc, serial_pci_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static void serial_pci_init(Object *o)
{
    PCISerialState *ps = PCI_SERIAL(o);

    object_initialize_child(o, "serial", &ps->state, TYPE_SERIAL);

    qdev_alias_all_properties(DEVICE(&ps->state), o);
}

static const TypeInfo serial_pci_info = {
    .name          = TYPE_PCI_SERIAL,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCISerialState),
    .instance_init = serial_pci_init,
    .class_init    = serial_pci_class_initfn,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void serial_pci_register_types(void)
{
    type_register_static(&serial_pci_info);
}

type_init(serial_pci_register_types)
