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

#include "serial.h"
#include "pci.h"

typedef struct PCISerialState {
    PCIDevice dev;
    SerialState state;
} PCISerialState;

static int serial_pci_init(PCIDevice *dev)
{
    PCISerialState *pci = DO_UPCAST(PCISerialState, dev, dev);
    SerialState *s = &pci->state;

    s->baudbase = 115200;
    serial_init_core(s);

    pci->dev.config[PCI_INTERRUPT_PIN] = 0x01;
    s->irq = pci->dev.irq[0];

    memory_region_init_io(&s->io, &serial_io_ops, s, "serial", 8);
    pci_register_bar(&pci->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
    return 0;
}

static void serial_pci_exit(PCIDevice *dev)
{
    PCISerialState *pci = DO_UPCAST(PCISerialState, dev, dev);
    SerialState *s = &pci->state;

    serial_exit_core(s);
    memory_region_destroy(&s->io);
}

static const VMStateDescription vmstate_pci_serial = {
    .name = "pci-serial",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCISerialState),
        VMSTATE_STRUCT(state, PCISerialState, 0, vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

static Property serial_pci_properties[] = {
    DEFINE_PROP_CHR("chardev",  PCISerialState, state.chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void serial_pci_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->init = serial_pci_init;
    pc->exit = serial_pci_exit;
    pc->vendor_id = 0x1b36; /* Red Hat */
    pc->device_id = 0x0002;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_COMMUNICATION_SERIAL;
    dc->vmsd = &vmstate_pci_serial;
    dc->props = serial_pci_properties;
}

static TypeInfo serial_pci_info = {
    .name          = "pci-serial",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCISerialState),
    .class_init    = serial_pci_class_initfn,
};

static void serial_pci_register_types(void)
{
    type_register_static(&serial_pci_info);
}

type_init(serial_pci_register_types)
