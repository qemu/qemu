/*
 * QEMU i440FX North Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_PCI_I440FX_H
#define HW_PCI_I440FX_H

#include "hw/pci/pci_device.h"
#include "hw/pci-host/pam.h"
#include "qom/object.h"

#define TYPE_I440FX_PCI_HOST_BRIDGE "i440FX-pcihost"
#define TYPE_I440FX_PCI_DEVICE "i440FX"

OBJECT_DECLARE_SIMPLE_TYPE(PCII440FXState, I440FX_PCI_DEVICE)

struct PCII440FXState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion *system_memory;
    MemoryRegion *pci_address_space;
    MemoryRegion *ram_memory;
    PAMMemoryRegion pam_regions[PAM_REGIONS_COUNT];
    MemoryRegion smram_region;
    MemoryRegion smram, low_smram;
};

#define TYPE_IGD_PASSTHROUGH_I440FX_PCI_DEVICE "igd-passthrough-i440FX"

PCIBus *i440fx_init(const char *pci_type,
                    DeviceState *dev,
                    MemoryRegion *address_space_mem,
                    MemoryRegion *address_space_io,
                    ram_addr_t ram_size,
                    ram_addr_t below_4g_mem_size,
                    ram_addr_t above_4g_mem_size,
                    MemoryRegion *pci_memory,
                    MemoryRegion *ram_memory);


#endif
