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
 * version 2 of the License, or (at your option) any later version.
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

#include "hcd-xhci.h"

static Property nec_xhci_properties[] = {
    DEFINE_PROP_ON_OFF_AUTO("msi", XHCIState, msi, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_ON_OFF_AUTO("msix", XHCIState, msix, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BIT("superspeed-ports-first",
                    XHCIState, flags, XHCI_FLAG_SS_FIRST, true),
    DEFINE_PROP_BIT("force-pcie-endcap", XHCIState, flags,
                    XHCI_FLAG_FORCE_PCIE_ENDCAP, false),
    DEFINE_PROP_UINT32("intrs", XHCIState, numintrs, MAXINTRS),
    DEFINE_PROP_UINT32("slots", XHCIState, numslots, MAXSLOTS),
    DEFINE_PROP_END_OF_LIST(),
};

static void nec_xhci_class_init(ObjectClass *klass, void *data)
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
    .parent        = TYPE_XHCI,
    .class_init    = nec_xhci_class_init,
};

static void nec_xhci_register_types(void)
{
    type_register_static(&nec_xhci_info);
}

type_init(nec_xhci_register_types)
