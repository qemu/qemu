/*
 * QEMU 16550A multi UART emulation
 *
 * SPDX-License-Identifier: MIT
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

/* see docs/specs/pci-serial.txt */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/char/serial.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"

#define PCI_SERIAL_MAX_PORTS 4

typedef struct PCIMultiSerialState {
    PCIDevice    dev;
    MemoryRegion iobar;
    uint32_t     ports;
    char         *name[PCI_SERIAL_MAX_PORTS];
    SerialState  state[PCI_SERIAL_MAX_PORTS];
    uint32_t     level[PCI_SERIAL_MAX_PORTS];
    qemu_irq     *irqs;
    uint8_t      prog_if;
} PCIMultiSerialState;

static void multi_serial_pci_exit(PCIDevice *dev)
{
    PCIMultiSerialState *pci = DO_UPCAST(PCIMultiSerialState, dev, dev);
    SerialState *s;
    int i;

    for (i = 0; i < pci->ports; i++) {
        s = pci->state + i;
        qdev_unrealize(DEVICE(s));
        memory_region_del_subregion(&pci->iobar, &s->io);
        g_free(pci->name[i]);
    }
    qemu_free_irqs(pci->irqs, pci->ports);
}

static void multi_serial_irq_mux(void *opaque, int n, int level)
{
    PCIMultiSerialState *pci = opaque;
    int i, pending = 0;

    pci->level[n] = level;
    for (i = 0; i < pci->ports; i++) {
        if (pci->level[i]) {
            pending = 1;
        }
    }
    pci_set_irq(&pci->dev, pending);
}

static size_t multi_serial_get_port_count(PCIDeviceClass *pc)
{
    switch (pc->device_id) {
    case 0x0003:
        return 2;
    case 0x0004:
        return 4;
    }

    g_assert_not_reached();
}


static void multi_serial_pci_realize(PCIDevice *dev, Error **errp)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    PCIMultiSerialState *pci = DO_UPCAST(PCIMultiSerialState, dev, dev);
    SerialState *s;
    size_t i, nports = multi_serial_get_port_count(pc);

    pci->dev.config[PCI_CLASS_PROG] = pci->prog_if;
    pci->dev.config[PCI_INTERRUPT_PIN] = 0x01;
    memory_region_init(&pci->iobar, OBJECT(pci), "multiserial", 8 * nports);
    pci_register_bar(&pci->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &pci->iobar);
    pci->irqs = qemu_allocate_irqs(multi_serial_irq_mux, pci, nports);

    for (i = 0; i < nports; i++) {
        s = pci->state + i;
        if (!qdev_realize(DEVICE(s), NULL, errp)) {
            multi_serial_pci_exit(dev);
            return;
        }
        s->irq = pci->irqs[i];
        pci->name[i] = g_strdup_printf("uart #%zu", i + 1);
        memory_region_init_io(&s->io, OBJECT(pci), &serial_io_ops, s,
                              pci->name[i], 8);
        memory_region_add_subregion(&pci->iobar, 8 * i, &s->io);
        pci->ports++;
    }
}

static const VMStateDescription vmstate_pci_multi_serial = {
    .name = "pci-serial-multi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCIMultiSerialState),
        VMSTATE_STRUCT_ARRAY(state, PCIMultiSerialState, PCI_SERIAL_MAX_PORTS,
                             0, vmstate_serial, SerialState),
        VMSTATE_UINT32_ARRAY(level, PCIMultiSerialState, PCI_SERIAL_MAX_PORTS),
        VMSTATE_END_OF_LIST()
    }
};

static Property multi_2x_serial_pci_properties[] = {
    DEFINE_PROP_CHR("chardev1",  PCIMultiSerialState, state[0].chr),
    DEFINE_PROP_CHR("chardev2",  PCIMultiSerialState, state[1].chr),
    DEFINE_PROP_UINT8("prog_if",  PCIMultiSerialState, prog_if, 0x02),
    DEFINE_PROP_END_OF_LIST(),
};

static Property multi_4x_serial_pci_properties[] = {
    DEFINE_PROP_CHR("chardev1",  PCIMultiSerialState, state[0].chr),
    DEFINE_PROP_CHR("chardev2",  PCIMultiSerialState, state[1].chr),
    DEFINE_PROP_CHR("chardev3",  PCIMultiSerialState, state[2].chr),
    DEFINE_PROP_CHR("chardev4",  PCIMultiSerialState, state[3].chr),
    DEFINE_PROP_UINT8("prog_if",  PCIMultiSerialState, prog_if, 0x02),
    DEFINE_PROP_END_OF_LIST(),
};

static void multi_2x_serial_pci_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->realize = multi_serial_pci_realize;
    pc->exit = multi_serial_pci_exit;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_SERIAL2;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_COMMUNICATION_SERIAL;
    dc->vmsd = &vmstate_pci_multi_serial;
    device_class_set_props(dc, multi_2x_serial_pci_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static void multi_4x_serial_pci_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->realize = multi_serial_pci_realize;
    pc->exit = multi_serial_pci_exit;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_SERIAL4;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_COMMUNICATION_SERIAL;
    dc->vmsd = &vmstate_pci_multi_serial;
    device_class_set_props(dc, multi_4x_serial_pci_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static void multi_serial_init(Object *o)
{
    PCIDevice *dev = PCI_DEVICE(o);
    PCIMultiSerialState *pms = DO_UPCAST(PCIMultiSerialState, dev, dev);
    size_t i, nports = multi_serial_get_port_count(PCI_DEVICE_GET_CLASS(dev));

    for (i = 0; i < nports; i++) {
        object_initialize_child(o, "serial[*]", &pms->state[i], TYPE_SERIAL);
    }
}

static const TypeInfo multi_2x_serial_pci_info = {
    .name          = "pci-serial-2x",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIMultiSerialState),
    .instance_init = multi_serial_init,
    .class_init    = multi_2x_serial_pci_class_initfn,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const TypeInfo multi_4x_serial_pci_info = {
    .name          = "pci-serial-4x",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIMultiSerialState),
    .instance_init = multi_serial_init,
    .class_init    = multi_4x_serial_pci_class_initfn,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void multi_serial_pci_register_types(void)
{
    type_register_static(&multi_2x_serial_pci_info);
    type_register_static(&multi_4x_serial_pci_info);
}

type_init(multi_serial_pci_register_types)
