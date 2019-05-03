/*
 * QEMU USB OHCI Emulation
 * Copyright (c) 2004 Gianni Tedesco
 * Copyright (c) 2006 CodeSourcery
 * Copyright (c) 2006 Openedhand Ltd.
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
#include "hw/hw.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/usb.h"
#include "hw/pci/pci.h"
#include "hw/sysbus.h"
#include "hw/qdev-dma.h"
#include "trace.h"
#include "hcd-ohci.h"

#define TYPE_PCI_OHCI "pci-ohci"
#define PCI_OHCI(obj) OBJECT_CHECK(OHCIPCIState, (obj), TYPE_PCI_OHCI)

typedef struct {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    OHCIState state;
    char *masterbus;
    uint32_t num_ports;
    uint32_t firstport;
} OHCIPCIState;

/**
 * A typical PCI OHCI will additionally set PERR in its configspace to
 * signal that it got an error.
 */
static void ohci_pci_die(struct OHCIState *ohci)
{
    OHCIPCIState *dev = container_of(ohci, OHCIPCIState, state);

    ohci_sysbus_die(ohci);

    pci_set_word(dev->parent_obj.config + PCI_STATUS,
                 PCI_STATUS_DETECTED_PARITY);
}

static void usb_ohci_realize_pci(PCIDevice *dev, Error **errp)
{
    Error *err = NULL;
    OHCIPCIState *ohci = PCI_OHCI(dev);

    dev->config[PCI_CLASS_PROG] = 0x10; /* OHCI */
    dev->config[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */

    usb_ohci_init(&ohci->state, DEVICE(dev), ohci->num_ports, 0,
                  ohci->masterbus, ohci->firstport,
                  pci_get_address_space(dev), ohci_pci_die, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    ohci->state.irq = pci_allocate_irq(dev);
    pci_register_bar(dev, 0, 0, &ohci->state.mem);
}

static void usb_ohci_exit(PCIDevice *dev)
{
    OHCIPCIState *ohci = PCI_OHCI(dev);
    OHCIState *s = &ohci->state;

    trace_usb_ohci_exit(s->name);
    ohci_bus_stop(s);

    if (s->async_td) {
        usb_cancel_packet(&s->usb_packet);
        s->async_td = 0;
    }
    ohci_stop_endpoints(s);

    if (!ohci->masterbus) {
        usb_bus_release(&s->bus);
    }

    timer_del(s->eof_timer);
    timer_free(s->eof_timer);
}

static void usb_ohci_reset_pci(DeviceState *d)
{
    PCIDevice *dev = PCI_DEVICE(d);
    OHCIPCIState *ohci = PCI_OHCI(dev);
    OHCIState *s = &ohci->state;

    ohci_hard_reset(s);
}

static Property ohci_pci_properties[] = {
    DEFINE_PROP_STRING("masterbus", OHCIPCIState, masterbus),
    DEFINE_PROP_UINT32("num-ports", OHCIPCIState, num_ports, 3),
    DEFINE_PROP_UINT32("firstport", OHCIPCIState, firstport, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_ohci = {
    .name = "ohci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, OHCIPCIState),
        VMSTATE_STRUCT(state, OHCIPCIState, 1, vmstate_ohci_state, OHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void ohci_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = usb_ohci_realize_pci;
    k->exit = usb_ohci_exit;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_IPID_USB;
    k->class_id = PCI_CLASS_SERIAL_USB;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    dc->desc = "Apple USB Controller";
    dc->props = ohci_pci_properties;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_ohci;
    dc->reset = usb_ohci_reset_pci;
}

static const TypeInfo ohci_pci_info = {
    .name          = TYPE_PCI_OHCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(OHCIPCIState),
    .class_init    = ohci_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void ohci_pci_register_types(void)
{
    type_register_static(&ohci_pci_info);
}

type_init(ohci_pci_register_types)
