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

/* see docs/specs/pci-serial.txt */

#include "hw/char/serial.h"
#include "hw/pci/pci.h"
#include "qapi/qmp/qerror.h"

#define PCI_SERIAL_MAX_PORTS 4

typedef struct PCISerialState {
    PCIDevice dev;
    SerialState state;
    uint8_t prog_if;
} PCISerialState;

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

static int serial_pci_init(PCIDevice *dev)
{
    PCISerialState *pci = DO_UPCAST(PCISerialState, dev, dev);
    SerialState *s = &pci->state;
    Error *err = NULL;

    s->baudbase = 115200;
    serial_realize_core(s, &err);
    if (err != NULL) {
        qerror_report_err(err);
        error_free(err);
        return -1;
    }

    pci->dev.config[PCI_CLASS_PROG] = pci->prog_if;
    pci->dev.config[PCI_INTERRUPT_PIN] = 0x01;
    s->irq = pci_allocate_irq(&pci->dev);

    memory_region_init_io(&s->io, OBJECT(pci), &serial_io_ops, s, "serial", 8);
    pci_register_bar(&pci->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
    return 0;
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

static int multi_serial_pci_init(PCIDevice *dev)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    PCIMultiSerialState *pci = DO_UPCAST(PCIMultiSerialState, dev, dev);
    SerialState *s;
    Error *err = NULL;
    int i;

    switch (pc->device_id) {
    case 0x0003:
        pci->ports = 2;
        break;
    case 0x0004:
        pci->ports = 4;
        break;
    }
    assert(pci->ports > 0);
    assert(pci->ports <= PCI_SERIAL_MAX_PORTS);

    pci->dev.config[PCI_CLASS_PROG] = pci->prog_if;
    pci->dev.config[PCI_INTERRUPT_PIN] = 0x01;
    memory_region_init(&pci->iobar, OBJECT(pci), "multiserial", 8 * pci->ports);
    pci_register_bar(&pci->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &pci->iobar);
    pci->irqs = qemu_allocate_irqs(multi_serial_irq_mux, pci,
                                   pci->ports);

    for (i = 0; i < pci->ports; i++) {
        s = pci->state + i;
        s->baudbase = 115200;
        serial_realize_core(s, &err);
        if (err != NULL) {
            qerror_report_err(err);
            error_free(err);
            return -1;
        }
        s->irq = pci->irqs[i];
        pci->name[i] = g_strdup_printf("uart #%d", i+1);
        memory_region_init_io(&s->io, OBJECT(pci), &serial_io_ops, s,
                              pci->name[i], 8);
        memory_region_add_subregion(&pci->iobar, 8 * i, &s->io);
    }
    return 0;
}

static void serial_pci_exit(PCIDevice *dev)
{
    PCISerialState *pci = DO_UPCAST(PCISerialState, dev, dev);
    SerialState *s = &pci->state;

    serial_exit_core(s);
    memory_region_destroy(&s->io);
    qemu_free_irq(s->irq);
}

static void multi_serial_pci_exit(PCIDevice *dev)
{
    PCIMultiSerialState *pci = DO_UPCAST(PCIMultiSerialState, dev, dev);
    SerialState *s;
    int i;

    for (i = 0; i < pci->ports; i++) {
        s = pci->state + i;
        serial_exit_core(s);
        memory_region_destroy(&s->io);
        g_free(pci->name[i]);
    }
    memory_region_destroy(&pci->iobar);
    qemu_free_irqs(pci->irqs);
}

static const VMStateDescription vmstate_pci_serial = {
    .name = "pci-serial",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCISerialState),
        VMSTATE_STRUCT(state, PCISerialState, 0, vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

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

static Property serial_pci_properties[] = {
    DEFINE_PROP_CHR("chardev",  PCISerialState, state.chr),
    DEFINE_PROP_UINT8("prog_if",  PCISerialState, prog_if, 0x02),
    DEFINE_PROP_END_OF_LIST(),
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

static void serial_pci_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->init = serial_pci_init;
    pc->exit = serial_pci_exit;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_SERIAL;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_COMMUNICATION_SERIAL;
    dc->vmsd = &vmstate_pci_serial;
    dc->props = serial_pci_properties;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static void multi_2x_serial_pci_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->init = multi_serial_pci_init;
    pc->exit = multi_serial_pci_exit;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_SERIAL2;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_COMMUNICATION_SERIAL;
    dc->vmsd = &vmstate_pci_multi_serial;
    dc->props = multi_2x_serial_pci_properties;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static void multi_4x_serial_pci_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->init = multi_serial_pci_init;
    pc->exit = multi_serial_pci_exit;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_SERIAL4;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_COMMUNICATION_SERIAL;
    dc->vmsd = &vmstate_pci_multi_serial;
    dc->props = multi_4x_serial_pci_properties;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo serial_pci_info = {
    .name          = "pci-serial",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCISerialState),
    .class_init    = serial_pci_class_initfn,
};

static const TypeInfo multi_2x_serial_pci_info = {
    .name          = "pci-serial-2x",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIMultiSerialState),
    .class_init    = multi_2x_serial_pci_class_initfn,
};

static const TypeInfo multi_4x_serial_pci_info = {
    .name          = "pci-serial-4x",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIMultiSerialState),
    .class_init    = multi_4x_serial_pci_class_initfn,
};

static void serial_pci_register_types(void)
{
    type_register_static(&serial_pci_info);
    type_register_static(&multi_2x_serial_pci_info);
    type_register_static(&multi_4x_serial_pci_info);
}

type_init(serial_pci_register_types)
