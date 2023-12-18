/*
 * QEMU Generic PCI Express Bridge Emulation
 *
 * Copyright (C) 2015 Alexander Graf <agraf@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HW_GPEX_H
#define HW_GPEX_H

#include "exec/hwaddr.h"
#include "hw/sysbus.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie_host.h"
#include "qom/object.h"

#define TYPE_GPEX_HOST "gpex-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(GPEXHost, GPEX_HOST)

#define TYPE_GPEX_ROOT_DEVICE "gpex-root"
OBJECT_DECLARE_SIMPLE_TYPE(GPEXRootState, GPEX_ROOT_DEVICE)

#define GPEX_NUM_IRQS 4

struct GPEXRootState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/
};

struct GPEXConfig {
    MemMapEntry ecam;
    MemMapEntry mmio32;
    MemMapEntry mmio64;
    MemMapEntry pio;
    int         irq;
    PCIBus      *bus;
};

struct GPEXHost {
    /*< private >*/
    PCIExpressHost parent_obj;
    /*< public >*/

    GPEXRootState gpex_root;

    MemoryRegion io_ioport;
    MemoryRegion io_mmio;
    MemoryRegion io_ioport_window;
    MemoryRegion io_mmio_window;
    qemu_irq irq[GPEX_NUM_IRQS];
    int irq_num[GPEX_NUM_IRQS];

    bool allow_unmapped_accesses;

    struct GPEXConfig gpex_cfg;
};

int gpex_set_irq_num(GPEXHost *s, int index, int gsi);

void acpi_dsdt_add_gpex(Aml *scope, struct GPEXConfig *cfg);
void acpi_dsdt_add_gpex_host(Aml *scope, uint32_t irq);

#define PCI_HOST_PIO_BASE               "x-pio-base"
#define PCI_HOST_PIO_SIZE               "x-pio-size"
#define PCI_HOST_ECAM_BASE              "x-ecam-base"
#define PCI_HOST_ECAM_SIZE              "x-ecam-size"
#define PCI_HOST_BELOW_4G_MMIO_BASE     "x-below-4g-mmio-base"
#define PCI_HOST_BELOW_4G_MMIO_SIZE     "x-below-4g-mmio-size"
#define PCI_HOST_ABOVE_4G_MMIO_BASE     "x-above-4g-mmio-base"
#define PCI_HOST_ABOVE_4G_MMIO_SIZE     "x-above-4g-mmio-size"

#endif /* HW_GPEX_H */
