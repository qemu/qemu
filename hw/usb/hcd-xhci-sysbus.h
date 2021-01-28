/*
 * USB xHCI controller for system-bus interface
 *
 * SPDX-FileCopyrightText: 2020 Xilinx
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
 * SPDX-sourceInfo: Based on hcd-echi-sysbus
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_USB_HCD_XHCI_SYSBUS_H
#define HW_USB_HCD_XHCI_SYSBUS_H

#include "hw/usb.h"
#include "hcd-xhci.h"
#include "hw/sysbus.h"

#define XHCI_SYSBUS(obj) \
    OBJECT_CHECK(XHCISysbusState, (obj), TYPE_XHCI_SYSBUS)


typedef struct XHCISysbusState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    XHCIState xhci;
    qemu_irq *irq;
} XHCISysbusState;

void xhci_sysbus_reset(DeviceState *dev);
#endif
