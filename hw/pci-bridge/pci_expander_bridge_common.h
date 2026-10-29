/*
 * PCI Expander Bridge Device Emulation Common Code
 *
 * Copyright (C) 2015 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

enum BusType { PCI, PCIE, CXL };

typedef struct PXBBus PXBBus;

struct PXBBus {
    /*< private >*/
    PCIBus parent_obj;
    /*< public >*/

    char bus_path[8];
};

int pxb_bus_num(PCIBus *bus);
void pxb_bus_class_init(ObjectClass *class, const void *data);
bool pxb_dev_realize_common(PCIDevice *dev, enum BusType type,
                            Error **errp);
void pxb_dev_exitfn(PCIDevice *pci_dev);
