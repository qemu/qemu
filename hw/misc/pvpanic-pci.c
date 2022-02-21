/*
 * QEMU simulated PCI pvpanic device.
 *
 * Copyright (C) 2020 Oracle
 *
 * Authors:
 *     Mihai Carabas <mihai.carabas@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "sysemu/runstate.h"

#include "hw/nvram/fw_cfg.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/misc/pvpanic.h"
#include "qom/object.h"
#include "hw/pci/pci.h"
#include "standard-headers/linux/pvpanic.h"

OBJECT_DECLARE_SIMPLE_TYPE(PVPanicPCIState, PVPANIC_PCI_DEVICE)

/*
 * PVPanicPCIState for PCI device
 */
typedef struct PVPanicPCIState {
    PCIDevice dev;
    PVPanicState pvpanic;
} PVPanicPCIState;

static const VMStateDescription vmstate_pvpanic_pci = {
    .name = "pvpanic-pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PVPanicPCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void pvpanic_pci_realizefn(PCIDevice *dev, Error **errp)
{
    PVPanicPCIState *s = PVPANIC_PCI_DEVICE(dev);
    PVPanicState *ps = &s->pvpanic;

    pvpanic_setup_io(&s->pvpanic, DEVICE(s), 2);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &ps->mr);
}

static Property pvpanic_pci_properties[] = {
    DEFINE_PROP_UINT8("events", PVPanicPCIState, pvpanic.events,
                      PVPANIC_PANICKED | PVPANIC_CRASH_LOADED),
    DEFINE_PROP_END_OF_LIST(),
};

static void pvpanic_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    device_class_set_props(dc, pvpanic_pci_properties);

    pc->realize = pvpanic_pci_realizefn;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_PVPANIC;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_SYSTEM_OTHER;
    dc->vmsd = &vmstate_pvpanic_pci;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pvpanic_pci_info = {
    .name          = TYPE_PVPANIC_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PVPanicPCIState),
    .class_init    = pvpanic_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

static void pvpanic_register_types(void)
{
    type_register_static(&pvpanic_pci_info);
}

type_init(pvpanic_register_types);
