/*
 * USB xHCI controller with PCI bus emulation
 *
 * SPDX-FileCopyrightText: 2011 Securiforest
 * SPDX-FileContributor: Hector Martin <hector@marcansoft.com>
 * SPDX-sourceInfo: Based on usb-ohci.c, emulates Renesas NEC USB 3.0
 * SPDX-FileCopyrightText: 2020 Xilinx
 * SPDX-FileContributor: Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
 * SPDX-sourceInfo: Moved the pci specific content for hcd-xhci.c to
 *                  hcd-xhci-pci.c
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
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hcd-xhci.h"
#include "trace.h"
#include "qapi/error.h"

static void qemu_xhci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id    = PCI_VENDOR_ID_REDHAT;
    k->device_id    = PCI_DEVICE_ID_REDHAT_XHCI;
    k->revision     = 0x01;
}

static void qemu_xhci_instance_init(Object *obj)
{
    XHCIState *xhci = XHCI(obj);

    xhci->msi      = ON_OFF_AUTO_OFF;
    xhci->msix     = ON_OFF_AUTO_AUTO;
    xhci->numintrs = MAXINTRS;
    xhci->numslots = MAXSLOTS;
    xhci_set_flag(xhci, XHCI_FLAG_SS_FIRST);
}

static const TypeInfo qemu_xhci_info = {
    .name          = TYPE_QEMU_XHCI,
    .parent        = TYPE_XHCI,
    .class_init    = qemu_xhci_class_init,
    .instance_init = qemu_xhci_instance_init,
};

static void xhci_register_types(void)
{
    type_register_static(&qemu_xhci_info);
}

type_init(xhci_register_types)
