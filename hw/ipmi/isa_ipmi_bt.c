/*
 * QEMU ISA IPMI BT emulation
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
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
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/ipmi/ipmi_bt.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "hw/acpi/ipmi.h"

#define TYPE_ISA_IPMI_BT "isa-ipmi-bt"
OBJECT_DECLARE_SIMPLE_TYPE(ISAIPMIBTDevice, ISA_IPMI_BT)

struct ISAIPMIBTDevice {
    ISADevice dev;
    int32_t isairq;
    qemu_irq irq;
    IPMIBT bt;
    uint32_t uuid;
};

static void isa_ipmi_bt_get_fwinfo(struct IPMIInterface *ii, IPMIFwInfo *info)
{
    ISAIPMIBTDevice *iib = ISA_IPMI_BT(ii);

    ipmi_bt_get_fwinfo(&iib->bt, info);
    info->interrupt_number = iib->isairq;
    info->i2c_slave_address = iib->bt.bmc->slave_addr;
    info->uuid = iib->uuid;
}

static void isa_ipmi_bt_raise_irq(IPMIBT *ib)
{
    ISAIPMIBTDevice *iib = ib->opaque;

    qemu_irq_raise(iib->irq);
}

static void isa_ipmi_bt_lower_irq(IPMIBT *ib)
{
    ISAIPMIBTDevice *iib = ib->opaque;

    qemu_irq_lower(iib->irq);
}

static void isa_ipmi_bt_realize(DeviceState *dev, Error **errp)
{
    Error *err = NULL;
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAIPMIBTDevice *iib = ISA_IPMI_BT(dev);
    IPMIInterface *ii = IPMI_INTERFACE(dev);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);

    if (!iib->bt.bmc) {
        error_setg(errp, "IPMI device requires a bmc attribute to be set");
        return;
    }

    iib->uuid = ipmi_next_uuid();

    iib->bt.bmc->intf = ii;
    iib->bt.opaque = iib;

    iic->init(ii, 0, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (iib->isairq > 0) {
        iib->irq = isa_get_irq(isadev, iib->isairq);
        iib->bt.use_irq = 1;
        iib->bt.raise_irq = isa_ipmi_bt_raise_irq;
        iib->bt.lower_irq = isa_ipmi_bt_lower_irq;
    }

    qdev_set_legacy_instance_id(dev, iib->bt.io_base, iib->bt.io_length);

    isa_register_ioport(isadev, &iib->bt.io, iib->bt.io_base);
}

static const VMStateDescription vmstate_ISAIPMIBTDevice = {
    .name = TYPE_IPMI_INTERFACE_PREFIX "isa-bt",
    .version_id = 2,
    .minimum_version_id = 2,
    /*
     * Version 1 had messed up the array transfer, it's not even usable
     * because it used VMSTATE_VBUFFER_UINT32, but it did not transfer
     * the buffer length, so random things would happen.
     */
    .fields      = (VMStateField[]) {
        VMSTATE_STRUCT(bt, ISAIPMIBTDevice, 1, vmstate_IPMIBT, IPMIBT),
        VMSTATE_END_OF_LIST()
    }
};

static void isa_ipmi_bt_init(Object *obj)
{
    ISAIPMIBTDevice *iib = ISA_IPMI_BT(obj);

    ipmi_bmc_find_and_link(obj, (Object **) &iib->bt.bmc);

    vmstate_register(NULL, 0, &vmstate_ISAIPMIBTDevice, iib);
}

static void *isa_ipmi_bt_get_backend_data(IPMIInterface *ii)
{
    ISAIPMIBTDevice *iib = ISA_IPMI_BT(ii);

    return &iib->bt;
}

static Property ipmi_isa_properties[] = {
    DEFINE_PROP_UINT32("ioport", ISAIPMIBTDevice, bt.io_base,  0xe4),
    DEFINE_PROP_INT32("irq",   ISAIPMIBTDevice, isairq,  5),
    DEFINE_PROP_END_OF_LIST(),
};

static void isa_ipmi_bt_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_CLASS(oc);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(oc);

    dc->realize = isa_ipmi_bt_realize;
    device_class_set_props(dc, ipmi_isa_properties);

    iic->get_backend_data = isa_ipmi_bt_get_backend_data;
    ipmi_bt_class_init(iic);
    iic->get_fwinfo = isa_ipmi_bt_get_fwinfo;
    adevc->build_dev_aml = build_ipmi_dev_aml;
}

static const TypeInfo isa_ipmi_bt_info = {
    .name          = TYPE_ISA_IPMI_BT,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAIPMIBTDevice),
    .instance_init = isa_ipmi_bt_init,
    .class_init    = isa_ipmi_bt_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IPMI_INTERFACE },
        { TYPE_ACPI_DEV_AML_IF },
        { }
    }
};

static void ipmi_register_types(void)
{
    type_register_static(&isa_ipmi_bt_info);
}

type_init(ipmi_register_types)
