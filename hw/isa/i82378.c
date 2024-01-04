/*
 * QEMU Intel i82378 emulation (PCI to ISA bridge)
 *
 * Copyright (c) 2010-2011 Hervé Poussineau
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/irq.h"
#include "hw/intc/i8259.h"
#include "hw/timer/i8254.h"
#include "migration/vmstate.h"
#include "hw/audio/pcspk.h"
#include "qom/object.h"

#define TYPE_I82378 "i82378"
OBJECT_DECLARE_SIMPLE_TYPE(I82378State, I82378)

struct I82378State {
    PCIDevice parent_obj;

    qemu_irq cpu_intr;
    qemu_irq *isa_irqs_in;
};

static const VMStateDescription vmstate_i82378 = {
    .name = "pci-i82378",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, I82378State),
        VMSTATE_END_OF_LIST()
    },
};

static void i82378_request_out0_irq(void *opaque, int irq, int level)
{
    I82378State *s = opaque;
    qemu_set_irq(s->cpu_intr, level);
}

static void i82378_request_pic_irq(void *opaque, int irq, int level)
{
    DeviceState *dev = opaque;
    I82378State *s = I82378(dev);

    qemu_set_irq(s->isa_irqs_in[irq], level);
}

static void i82378_realize(PCIDevice *pci, Error **errp)
{
    DeviceState *dev = DEVICE(pci);
    I82378State *s = I82378(dev);
    uint8_t *pci_conf;
    ISABus *isabus;
    ISADevice *pit;
    ISADevice *pcspk;

    pci_conf = pci->config;
    pci_set_word(pci_conf + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    pci_set_word(pci_conf + PCI_STATUS,
                 PCI_STATUS_DEVSEL_MEDIUM);

    pci_config_set_interrupt_pin(pci_conf, 1); /* interrupt pin 0 */

    isabus = isa_bus_new(dev, get_system_memory(),
                         pci_address_space_io(pci), errp);
    if (!isabus) {
        return;
    }

    /* This device has:
       2 82C59 (irq)
       1 82C54 (pit)
       2 82C37 (dma)
       NMI
       Utility Bus Support Registers

       All devices accept byte access only, except timer
     */

    /* 2 82C59 (irq) */
    s->isa_irqs_in = i8259_init(isabus,
                                qemu_allocate_irq(i82378_request_out0_irq,
                                                  s, 0));
    isa_bus_register_input_irqs(isabus, s->isa_irqs_in);

    /* 1 82C54 (pit) */
    pit = i8254_pit_init(isabus, 0x40, 0, NULL);

    /* speaker */
    pcspk = isa_new(TYPE_PC_SPEAKER);
    object_property_set_link(OBJECT(pcspk), "pit", OBJECT(pit), &error_fatal);
    if (!isa_realize_and_unref(pcspk, isabus, errp)) {
        return;
    }

    /* 2 82C37 (dma) */
    isa_create_simple(isabus, "i82374");
}

static void i82378_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    I82378State *s = I82378(obj);

    qdev_init_gpio_out(dev, &s->cpu_intr, 1);
    qdev_init_gpio_in(dev, i82378_request_pic_irq, 16);
}

static void i82378_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = i82378_realize;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82378;
    k->revision = 0x03;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    dc->vmsd = &vmstate_i82378;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo i82378_type_info = {
    .name = TYPE_I82378,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(I82378State),
    .instance_init = i82378_init,
    .class_init = i82378_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void i82378_register_types(void)
{
    type_register_static(&i82378_type_info);
}

type_init(i82378_register_types)
