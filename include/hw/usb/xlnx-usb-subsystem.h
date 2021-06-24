/*
 * QEMU model of the Xilinx usb subsystem
 *
 * Copyright (c) 2020 Xilinx Inc. Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
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

#ifndef XLNX_VERSAL_USB_SUBSYSTEM_H
#define XLNX_VERSAL_USB_SUBSYSTEM_H

#include "hw/usb/xlnx-versal-usb2-ctrl-regs.h"
#include "hw/usb/hcd-dwc3.h"

#define TYPE_XILINX_VERSAL_USB2 "xlnx.versal-usb2"

#define VERSAL_USB2(obj) \
     OBJECT_CHECK(VersalUsb2, (obj), TYPE_XILINX_VERSAL_USB2)

typedef struct VersalUsb2 {
    SysBusDevice parent_obj;
    MemoryRegion dwc3_mr;
    MemoryRegion usb2Ctrl_mr;

    VersalUsb2CtrlRegs usb2Ctrl;
    USBDWC3 dwc3;
} VersalUsb2;

#endif
