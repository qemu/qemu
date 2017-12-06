/*
 * QEMU ISA IPMI KCS emulation
 *
 * Copyright (c) 2015,2017 Corey Minyard, MontaVista Software, LLC
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
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/ipmi/ipmi_kcs.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_ISA_IPMI_KCS "isa-ipmi-kcs"
#define ISA_IPMI_KCS(obj) OBJECT_CHECK(ISAIPMIKCSDevice, (obj), \
                                       TYPE_ISA_IPMI_KCS)

typedef struct ISAIPMIKCSDevice {
    ISADevice dev;
    int32_t isairq;
    qemu_irq irq;
    IPMIKCS kcs;
    uint32_t uuid;
} ISAIPMIKCSDevice;

static void isa_ipmi_kcs_get_fwinfo(IPMIInterface *ii, IPMIFwInfo *info)
{
    ISAIPMIKCSDevice *iik = ISA_IPMI_KCS(ii);

    ipmi_kcs_get_fwinfo(&iik->kcs, info);
    info->interrupt_number = iik->isairq;
    info->uuid = iik->uuid;
}

static void isa_ipmi_kcs_raise_irq(IPMIKCS *ik)
{
    ISAIPMIKCSDevice *iik = ik->opaque;

    qemu_irq_raise(iik->irq);
}

static void isa_ipmi_kcs_lower_irq(IPMIKCS *ik)
{
    ISAIPMIKCSDevice *iik = ik->opaque;

    qemu_irq_lower(iik->irq);
}

static void ipmi_isa_realize(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAIPMIKCSDevice *iik = ISA_IPMI_KCS(dev);
    IPMIInterface *ii = IPMI_INTERFACE(dev);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);

    if (!iik->kcs.bmc) {
        error_setg(errp, "IPMI device requires a bmc attribute to be set");
        return;
    }

    iik->uuid = ipmi_next_uuid();

    iik->kcs.bmc->intf = ii;
    iik->kcs.opaque = iik;

    iic->init(ii, 0, errp);
    if (*errp)
        return;

    if (iik->isairq > 0) {
        isa_init_irq(isadev, &iik->irq, iik->isairq);
        iik->kcs.use_irq = 1;
        iik->kcs.raise_irq = isa_ipmi_kcs_raise_irq;
        iik->kcs.lower_irq = isa_ipmi_kcs_lower_irq;
    }

    qdev_set_legacy_instance_id(dev, iik->kcs.io_base, iik->kcs.io_length);

    isa_register_ioport(isadev, &iik->kcs.io, iik->kcs.io_base);
}

static bool vmstate_kcs_before_version2(void *opaque, int version)
{
    return version <= 1;
}

static const VMStateDescription vmstate_ISAIPMIKCSDevice = {
    .name = TYPE_IPMI_INTERFACE,
    .version_id = 2,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_VSTRUCT_TEST(kcs, ISAIPMIKCSDevice, vmstate_kcs_before_version2,
                             0, vmstate_IPMIKCS, IPMIKCS, 1),
        VMSTATE_VSTRUCT_V(kcs, ISAIPMIKCSDevice, 2, vmstate_IPMIKCS,
                          IPMIKCS, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void isa_ipmi_kcs_init(Object *obj)
{
    ISAIPMIKCSDevice *iik = ISA_IPMI_KCS(obj);

    ipmi_bmc_find_and_link(obj, (Object **) &iik->kcs.bmc);

    /*
     * Version 1 had an incorrect name, it clashed with the BT
     * IPMI device, so receive it, but transmit a different
     * version.
     */
    vmstate_register(NULL, 0, &vmstate_ISAIPMIKCSDevice, iik);
}

static void *isa_ipmi_kcs_get_backend_data(IPMIInterface *ii)
{
    ISAIPMIKCSDevice *iik = ISA_IPMI_KCS(ii);

    return &iik->kcs;
}

static Property ipmi_isa_properties[] = {
    DEFINE_PROP_UINT32("ioport", ISAIPMIKCSDevice, kcs.io_base,  0xca2),
    DEFINE_PROP_INT32("irq",   ISAIPMIKCSDevice, isairq,  5),
    DEFINE_PROP_END_OF_LIST(),
};

static void isa_ipmi_kcs_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_CLASS(oc);

    dc->realize = ipmi_isa_realize;
    dc->props = ipmi_isa_properties;

    iic->get_backend_data = isa_ipmi_kcs_get_backend_data;
    ipmi_kcs_class_init(iic);
    iic->get_fwinfo = isa_ipmi_kcs_get_fwinfo;
}

static const TypeInfo isa_ipmi_kcs_info = {
    .name          = TYPE_ISA_IPMI_KCS,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAIPMIKCSDevice),
    .instance_init = isa_ipmi_kcs_init,
    .class_init    = isa_ipmi_kcs_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IPMI_INTERFACE },
        { }
    }
};

static void ipmi_register_types(void)
{
    type_register_static(&isa_ipmi_kcs_info);
}

type_init(ipmi_register_types)
