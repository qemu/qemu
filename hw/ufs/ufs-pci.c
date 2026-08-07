/*
 * QEMU Universal Flash Storage (UFS) PCI Controller
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Written by Jeuk Kim <jeuk20.kim@samsung.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * Usage
 * -----
 *
 * Add options:
 *      -drive file=<file>,if=none,id=<drive_id>
 *      -device ufs,serial=<serial>,id=<bus_name>, \
 *              nutrs=<N[optional]>,nutmrs=<N[optional]>
 *      -device ufs-lu,drive=<drive_id>,bus=<bus_name>
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "ufs.h"

#define TYPE_UFS_PCI "ufs"
OBJECT_DECLARE_SIMPLE_TYPE(UfsPciState, UFS_PCI)

struct UfsPciState {
    PCIDevice parent_obj;
    UfsHc ufs;
};

static void ufs_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    UfsPciState *s = UFS_PCI(pci_dev);
    UfsHc *u = &s->ufs;
    uint8_t *pci_conf = pci_dev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_config_set_prog_interface(pci_conf, 0x1);
    u->irq = pci_allocate_irq(pci_dev);
    if (!ufs_realize(u, DEVICE(pci_dev), pci_get_address_space(pci_dev),
                     errp)) {
        qemu_free_irq(u->irq);
        u->irq = NULL;
        return;
    }

    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &u->iomem);
}

static void ufs_pci_exit(PCIDevice *pci_dev)
{
    UfsPciState *s = UFS_PCI(pci_dev);

    ufs_unrealize(&s->ufs);
    qemu_free_irq(s->ufs.irq);
}

static const Property ufs_pci_props[] = {
    DEFINE_PROP_STRING("serial", UfsPciState, ufs.params.serial),
    DEFINE_PROP_UINT8("nutrs", UfsPciState, ufs.params.nutrs, 32),
    DEFINE_PROP_UINT8("nutmrs", UfsPciState, ufs.params.nutmrs, 8),
    DEFINE_PROP_BOOL("mcq", UfsPciState, ufs.params.mcq, false),
    DEFINE_PROP_UINT8("mcq-maxq", UfsPciState, ufs.params.mcq_maxq, 2),
    DEFINE_PROP_UINT32("wb-max-size", UfsPciState, ufs.params.wb_max_size,
                       0x400),
    DEFINE_PROP_UINT32("wb-min-size", UfsPciState, ufs.params.wb_min_size,
                       0x100),
};

static const VMStateDescription ufs_pci_vmstate = {
    .name = "ufs",
    .unmigratable = 1,
};

static void ufs_pci_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = ufs_pci_realize;
    pc->exit = ufs_pci_exit;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_UFS;
    pc->class_id = PCI_CLASS_STORAGE_UFS;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Universal Flash Storage";
    device_class_set_props(dc, ufs_pci_props);
    dc->vmsd = &ufs_pci_vmstate;
}

static const TypeInfo ufs_pci_info = {
    .name = TYPE_UFS_PCI,
    .parent = TYPE_PCI_DEVICE,
    .class_init = ufs_pci_class_init,
    .instance_size = sizeof(UfsPciState),
    .interfaces = (const InterfaceInfo[]){ { INTERFACE_PCIE_DEVICE }, {} },
};

static void ufs_pci_register_types(void)
{
    type_register_static(&ufs_pci_info);
}

type_init(ufs_pci_register_types)
