/*
 * QEMU PCI IPMI BT emulation
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
#include "hw/ipmi/ipmi_bt.h"
#include "hw/pci/pci_device.h"
#include "qom/object.h"

#define TYPE_PCI_IPMI_BT "pci-ipmi-bt"
OBJECT_DECLARE_SIMPLE_TYPE(PCIIPMIBTDevice, PCI_IPMI_BT)

struct PCIIPMIBTDevice {
    PCIDevice dev;
    IPMIBT bt;
    bool irq_enabled;
    uint32_t uuid;
};

static void pci_ipmi_bt_get_fwinfo(struct IPMIInterface *ii, IPMIFwInfo *info)
{
    PCIIPMIBTDevice *pib = PCI_IPMI_BT(ii);

    ipmi_bt_get_fwinfo(&pib->bt, info);
    info->irq_source = IPMI_PCI_IRQ;
    info->interrupt_number = pci_intx(&pib->dev);
    info->i2c_slave_address = pib->bt.bmc->slave_addr;
    info->uuid = pib->uuid;
}

static void pci_ipmi_raise_irq(IPMIBT *ib)
{
    PCIIPMIBTDevice *pib = ib->opaque;

    pci_set_irq(&pib->dev, true);
}

static void pci_ipmi_lower_irq(IPMIBT *ib)
{
    PCIIPMIBTDevice *pib = ib->opaque;

    pci_set_irq(&pib->dev, false);
}

static void pci_ipmi_bt_realize(PCIDevice *pd, Error **errp)
{
    Error *err = NULL;
    PCIIPMIBTDevice *pib = PCI_IPMI_BT(pd);
    IPMIInterface *ii = IPMI_INTERFACE(pd);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);

    if (!pib->bt.bmc) {
        error_setg(errp, "IPMI device requires a bmc attribute to be set");
        return;
    }

    pib->uuid = ipmi_next_uuid();

    pib->bt.bmc->intf = ii;
    pib->bt.opaque = pib;

    pci_config_set_prog_interface(pd->config, 0x02); /* BT */
    pci_config_set_interrupt_pin(pd->config, 0x01);
    pib->bt.use_irq = 1;
    pib->bt.raise_irq = pci_ipmi_raise_irq;
    pib->bt.lower_irq = pci_ipmi_lower_irq;

    iic->init(ii, 8, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    pci_register_bar(pd, 0, PCI_BASE_ADDRESS_SPACE_IO, &pib->bt.io);
}

const VMStateDescription vmstate_PCIIPMIBTDevice = {
    .name = TYPE_IPMI_INTERFACE_PREFIX "pci-bt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCIIPMIBTDevice),
        VMSTATE_STRUCT(bt, PCIIPMIBTDevice, 1, vmstate_IPMIBT, IPMIBT),
        VMSTATE_END_OF_LIST()
    }
};

static void pci_ipmi_bt_instance_init(Object *obj)
{
    PCIIPMIBTDevice *pib = PCI_IPMI_BT(obj);

    ipmi_bmc_find_and_link(obj, (Object **) &pib->bt.bmc);
}

static void *pci_ipmi_bt_get_backend_data(IPMIInterface *ii)
{
    PCIIPMIBTDevice *pib = PCI_IPMI_BT(ii);

    return &pib->bt;
}

static void pci_ipmi_bt_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_CLASS(oc);

    pdc->vendor_id = PCI_VENDOR_ID_QEMU;
    pdc->device_id = PCI_DEVICE_ID_QEMU_IPMI;
    pdc->revision = 1;
    pdc->class_id = PCI_CLASS_SERIAL_IPMI;

    dc->vmsd = &vmstate_PCIIPMIBTDevice;
    dc->desc = "PCI IPMI BT";
    pdc->realize = pci_ipmi_bt_realize;

    iic->get_backend_data = pci_ipmi_bt_get_backend_data;
    ipmi_bt_class_init(iic);
    iic->get_fwinfo = pci_ipmi_bt_get_fwinfo;
}

static const TypeInfo pci_ipmi_bt_info = {
    .name          = TYPE_PCI_IPMI_BT,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIIPMIBTDevice),
    .instance_init = pci_ipmi_bt_instance_init,
    .class_init    = pci_ipmi_bt_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IPMI_INTERFACE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

static void pci_ipmi_bt_register_types(void)
{
    type_register_static(&pci_ipmi_bt_info);
}

type_init(pci_ipmi_bt_register_types)
