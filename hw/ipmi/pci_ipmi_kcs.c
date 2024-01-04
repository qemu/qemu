/*
 * QEMU PCI IPMI KCS emulation
 *
 * Copyright (c) 2017 Corey Minyard, MontaVista Software, LLC
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
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "hw/ipmi/ipmi_kcs.h"
#include "hw/pci/pci_device.h"
#include "qom/object.h"

#define TYPE_PCI_IPMI_KCS "pci-ipmi-kcs"
OBJECT_DECLARE_SIMPLE_TYPE(PCIIPMIKCSDevice, PCI_IPMI_KCS)

struct PCIIPMIKCSDevice {
    PCIDevice dev;
    IPMIKCS kcs;
    bool irq_enabled;
    uint32_t uuid;
};

static void pci_ipmi_raise_irq(IPMIKCS *ik)
{
    PCIIPMIKCSDevice *pik = ik->opaque;

    pci_set_irq(&pik->dev, true);
}

static void pci_ipmi_lower_irq(IPMIKCS *ik)
{
    PCIIPMIKCSDevice *pik = ik->opaque;

    pci_set_irq(&pik->dev, false);
}

static void pci_ipmi_kcs_realize(PCIDevice *pd, Error **errp)
{
    Error *err = NULL;
    PCIIPMIKCSDevice *pik = PCI_IPMI_KCS(pd);
    IPMIInterface *ii = IPMI_INTERFACE(pd);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);

    if (!pik->kcs.bmc) {
        error_setg(errp, "IPMI device requires a bmc attribute to be set");
        return;
    }

    pik->uuid = ipmi_next_uuid();

    pik->kcs.bmc->intf = ii;
    pik->kcs.opaque = pik;

    pci_config_set_prog_interface(pd->config, 0x01); /* KCS */
    pci_config_set_interrupt_pin(pd->config, 0x01);
    pik->kcs.use_irq = 1;
    pik->kcs.raise_irq = pci_ipmi_raise_irq;
    pik->kcs.lower_irq = pci_ipmi_lower_irq;

    iic->init(ii, 8, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    pci_register_bar(pd, 0, PCI_BASE_ADDRESS_SPACE_IO, &pik->kcs.io);
}

const VMStateDescription vmstate_PCIIPMIKCSDevice = {
    .name = TYPE_IPMI_INTERFACE_PREFIX "pci-kcs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCIIPMIKCSDevice),
        VMSTATE_STRUCT(kcs, PCIIPMIKCSDevice, 1, vmstate_IPMIKCS, IPMIKCS),
        VMSTATE_END_OF_LIST()
    }
};

static void pci_ipmi_kcs_instance_init(Object *obj)
{
    PCIIPMIKCSDevice *pik = PCI_IPMI_KCS(obj);

    ipmi_bmc_find_and_link(obj, (Object **) &pik->kcs.bmc);
}

static void *pci_ipmi_kcs_get_backend_data(IPMIInterface *ii)
{
    PCIIPMIKCSDevice *pik = PCI_IPMI_KCS(ii);

    return &pik->kcs;
}

static void pci_ipmi_kcs_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_CLASS(oc);

    pdc->vendor_id = PCI_VENDOR_ID_QEMU;
    pdc->device_id = PCI_DEVICE_ID_QEMU_IPMI;
    pdc->revision = 1;
    pdc->class_id = PCI_CLASS_SERIAL_IPMI;

    dc->vmsd = &vmstate_PCIIPMIKCSDevice;
    dc->desc = "PCI IPMI KCS";
    pdc->realize = pci_ipmi_kcs_realize;

    iic->get_backend_data = pci_ipmi_kcs_get_backend_data;
    ipmi_kcs_class_init(iic);
}

static const TypeInfo pci_ipmi_kcs_info = {
    .name          = TYPE_PCI_IPMI_KCS,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIIPMIKCSDevice),
    .instance_init = pci_ipmi_kcs_instance_init,
    .class_init    = pci_ipmi_kcs_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IPMI_INTERFACE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

static void pci_ipmi_kcs_register_types(void)
{
    type_register_static(&pci_ipmi_kcs_info);
}

type_init(pci_ipmi_kcs_register_types)
