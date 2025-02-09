/*
 * QEMU model of the Xilinx usb subsystem
 *
 * Copyright (c) 2020 Xilinx Inc. Sai Pavan Boddu <sai.pava.boddu@xilinx.com>
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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/usb/xlnx-usb-subsystem.h"

static void versal_usb2_realize(DeviceState *dev, Error **errp)
{
    VersalUsb2 *s = VERSAL_USB2(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *err = NULL;

    sysbus_realize(SYS_BUS_DEVICE(&s->dwc3), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_realize(SYS_BUS_DEVICE(&s->usb2Ctrl), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_init_mmio(sbd, &s->dwc3_mr);
    sysbus_init_mmio(sbd, &s->usb2Ctrl_mr);
    qdev_pass_gpios(DEVICE(&s->dwc3.sysbus_xhci), dev, SYSBUS_DEVICE_GPIO_IRQ);
}

static void versal_usb2_init(Object *obj)
{
    VersalUsb2 *s = VERSAL_USB2(obj);

    object_initialize_child(obj, "versal.dwc3", &s->dwc3,
                            TYPE_USB_DWC3);
    object_initialize_child(obj, "versal.usb2-ctrl", &s->usb2Ctrl,
                            TYPE_XILINX_VERSAL_USB2_CTRL_REGS);
    memory_region_init_alias(&s->dwc3_mr, obj, "versal.dwc3_alias",
                             &s->dwc3.iomem, 0, DWC3_SIZE);
    memory_region_init_alias(&s->usb2Ctrl_mr, obj, "versal.usb2Ctrl_alias",
                             &s->usb2Ctrl.iomem, 0, USB2_REGS_R_MAX * 4);
    qdev_alias_all_properties(DEVICE(&s->dwc3), obj);
    qdev_alias_all_properties(DEVICE(&s->dwc3.sysbus_xhci), obj);
    object_property_add_alias(obj, "dma", OBJECT(&s->dwc3.sysbus_xhci), "dma");
}

static void versal_usb2_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = versal_usb2_realize;
}

static const TypeInfo versal_usb2_info = {
    .name          = TYPE_XILINX_VERSAL_USB2,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VersalUsb2),
    .class_init    = versal_usb2_class_init,
    .instance_init = versal_usb2_init,
};

static void versal_usb_types(void)
{
    type_register_static(&versal_usb2_info);
}

type_init(versal_usb_types)
