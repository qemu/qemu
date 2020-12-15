/*
 * QEMU model of the USB DWC3 host controller emulation.
 *
 * Copyright (c) 2020 Xilinx Inc.
 *
 * Written by Vikram Garhwal<fnu.vikram@xilinx.com>
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
#ifndef HCD_DWC3_H
#define HCD_DWC3_H

#include "hw/usb/hcd-xhci.h"
#include "hw/usb/hcd-xhci-sysbus.h"

#define TYPE_USB_DWC3 "usb_dwc3"

#define USB_DWC3(obj) \
     OBJECT_CHECK(USBDWC3, (obj), TYPE_USB_DWC3)

#define USB_DWC3_R_MAX ((0x530 / 4) + 1)
#define DWC3_SIZE 0x10000

typedef struct USBDWC3 {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    XHCISysbusState sysbus_xhci;

    uint32_t regs[USB_DWC3_R_MAX];
    RegisterInfo regs_info[USB_DWC3_R_MAX];

    struct {
        uint8_t     mode;
        uint32_t    dwc_usb3_user;
    } cfg;

} USBDWC3;

#endif
