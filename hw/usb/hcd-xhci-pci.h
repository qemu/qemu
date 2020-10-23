/*
 * USB xHCI controller emulation
 *
 * Copyright (c) 2011 Securiforest
 * Date: 2011-05-11 ;  Author: Hector Martin <hector@marcansoft.com>
 * Based on usb-ohci.c, emulates Renesas NEC USB 3.0
 * Date: 2020-01-1; Author: Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
 * PCI hooks are moved from XHCIState to XHCIPciState
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

#ifndef HW_USB_HCD_XHCI_PCI_H
#define HW_USB_HCD_XHCI_PCI_H

#include "hw/usb.h"
#include "hcd-xhci.h"

#define TYPE_XHCI_PCI "pci-xhci"
#define XHCI_PCI(obj) \
    OBJECT_CHECK(XHCIPciState, (obj), TYPE_XHCI_PCI)


typedef struct XHCIPciState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/
    XHCIState xhci;
    OnOffAuto msi;
    OnOffAuto msix;
} XHCIPciState;

#endif
