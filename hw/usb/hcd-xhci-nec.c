/*
 * USB xHCI controller emulation
 *
 * Copyright (c) 2011 Securiforest
 * Date: 2011-05-11 ;  Author: Hector Martin <hector@marcansoft.com>
 * Based on usb-ohci.c, emulates Renesas NEC USB 3.0
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
#include "hw/usb.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"

#include "hcd-xhci-pci.h"

OBJECT_DECLARE_SIMPLE_TYPE(XHCINecState, NEC_XHCI)

struct XHCINecState {
    XHCIPciState parent_obj;

    uint32_t intrs;
    uint32_t slots;
};

static const Property nec_xhci_properties[] = {
    DEFINE_PROP_UINT32("intrs", XHCINecState, intrs, XHCI_MAXINTRS),
    DEFINE_PROP_UINT32("slots", XHCINecState, slots, XHCI_MAXSLOTS),
};

static void nec_xhci_instance_init(Object *obj)
{
    XHCIPciState *pci = XHCI_PCI(obj);
    XHCINecState *nec = NEC_XHCI(obj);

    pci->xhci.numintrs = nec->intrs;
    pci->xhci.numslots = nec->slots;
}

static void nec_xhci_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nec_xhci_properties);
    k->vendor_id    = PCI_VENDOR_ID_NEC;
    k->device_id    = PCI_DEVICE_ID_NEC_UPD720200;
    k->revision     = 0x03;
}

static const TypeInfo nec_xhci_info = {
    .name          = TYPE_NEC_XHCI,
    .parent        = TYPE_XHCI_PCI,
    .instance_size = sizeof(XHCINecState),
    .instance_init = nec_xhci_instance_init,
    .class_init    = nec_xhci_class_init,
};

static void nec_xhci_register_types(void)
{
    type_register_static(&nec_xhci_info);
}

type_init(nec_xhci_register_types)
