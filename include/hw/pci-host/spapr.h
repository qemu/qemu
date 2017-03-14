/*
 * QEMU SPAPR PCI BUS definitions
 *
 * Copyright (c) 2011 Alexey Kardashevskiy <aik@au1.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PCI_HOST_SPAPR_H
#define PCI_HOST_SPAPR_H

#include "hw/ppc/spapr.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/ppc/xics.h"

#define TYPE_SPAPR_PCI_HOST_BRIDGE "spapr-pci-host-bridge"

#define SPAPR_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(sPAPRPHBState, (obj), TYPE_SPAPR_PCI_HOST_BRIDGE)

#define SPAPR_PCI_DMA_MAX_WINDOWS    2

typedef struct sPAPRPHBState sPAPRPHBState;

typedef struct spapr_pci_msi {
    uint32_t first_irq;
    uint32_t num;
} spapr_pci_msi;

typedef struct spapr_pci_msi_mig {
    uint32_t key;
    spapr_pci_msi value;
} spapr_pci_msi_mig;

struct sPAPRPHBState {
    PCIHostState parent_obj;

    uint32_t index;
    uint64_t buid;
    char *dtbusname;
    bool dr_enabled;

    MemoryRegion memspace, iospace;
    hwaddr mem_win_addr, mem_win_size, mem64_win_addr, mem64_win_size;
    uint64_t mem64_win_pciaddr;
    hwaddr io_win_addr, io_win_size;
    MemoryRegion mem32window, mem64window, iowindow, msiwindow;

    uint32_t dma_liobn[SPAPR_PCI_DMA_MAX_WINDOWS];
    hwaddr dma_win_addr, dma_win_size;
    AddressSpace iommu_as;
    MemoryRegion iommu_root;

    struct spapr_pci_lsi {
        uint32_t irq;
    } lsi_table[PCI_NUM_PINS];

    GHashTable *msi;
    /* Temporary cache for migration purposes */
    int32_t msi_devs_num;
    spapr_pci_msi_mig *msi_devs;

    QLIST_ENTRY(sPAPRPHBState) list;

    bool ddw_enabled;
    uint64_t page_size_mask;
    uint64_t dma64_win_addr;

    uint32_t numa_node;

    bool pcie_ecs; /* Allow access to PCIe extended config space? */

    /* Fields for migration compatibility hacks */
    bool pre_2_8_migration;
    uint32_t mig_liobn;
    hwaddr mig_mem_win_addr, mig_mem_win_size;
    hwaddr mig_io_win_addr, mig_io_win_size;
};

#define SPAPR_PCI_MEM_WIN_BUS_OFFSET 0x80000000ULL
#define SPAPR_PCI_MEM32_WIN_SIZE     \
    ((1ULL << 32) - SPAPR_PCI_MEM_WIN_BUS_OFFSET)
#define SPAPR_PCI_MEM64_WIN_SIZE     0x10000000000ULL /* 1 TiB */

/* Without manual configuration, all PCI outbound windows will be
 * within this range */
#define SPAPR_PCI_BASE               (1ULL << 45) /* 32 TiB */
#define SPAPR_PCI_LIMIT              (1ULL << 46) /* 64 TiB */

#define SPAPR_PCI_2_7_MMIO_WIN_SIZE  0xf80000000
#define SPAPR_PCI_IO_WIN_SIZE        0x10000

#define SPAPR_PCI_MSI_WINDOW         0x40000000000ULL

static inline qemu_irq spapr_phb_lsi_qirq(struct sPAPRPHBState *phb, int pin)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    return xics_get_qirq(XICS_FABRIC(spapr), phb->lsi_table[pin].irq);
}

PCIHostState *spapr_create_phb(sPAPRMachineState *spapr, int index);

int spapr_populate_pci_dt(sPAPRPHBState *phb,
                          uint32_t xics_phandle,
                          void *fdt);

void spapr_pci_rtas_init(void);

sPAPRPHBState *spapr_pci_find_phb(sPAPRMachineState *spapr, uint64_t buid);
PCIDevice *spapr_pci_find_dev(sPAPRMachineState *spapr, uint64_t buid,
                              uint32_t config_addr);

/* VFIO EEH hooks */
#ifdef CONFIG_LINUX
bool spapr_phb_eeh_available(sPAPRPHBState *sphb);
int spapr_phb_vfio_eeh_set_option(sPAPRPHBState *sphb,
                                  unsigned int addr, int option);
int spapr_phb_vfio_eeh_get_state(sPAPRPHBState *sphb, int *state);
int spapr_phb_vfio_eeh_reset(sPAPRPHBState *sphb, int option);
int spapr_phb_vfio_eeh_configure(sPAPRPHBState *sphb);
void spapr_phb_vfio_reset(DeviceState *qdev);
#else
static inline bool spapr_phb_eeh_available(sPAPRPHBState *sphb)
{
    return false;
}
static inline int spapr_phb_vfio_eeh_set_option(sPAPRPHBState *sphb,
                                                unsigned int addr, int option)
{
    return RTAS_OUT_HW_ERROR;
}
static inline int spapr_phb_vfio_eeh_get_state(sPAPRPHBState *sphb,
                                               int *state)
{
    return RTAS_OUT_HW_ERROR;
}
static inline int spapr_phb_vfio_eeh_reset(sPAPRPHBState *sphb, int option)
{
    return RTAS_OUT_HW_ERROR;
}
static inline int spapr_phb_vfio_eeh_configure(sPAPRPHBState *sphb)
{
    return RTAS_OUT_HW_ERROR;
}
static inline void spapr_phb_vfio_reset(DeviceState *qdev)
{
}
#endif

void spapr_phb_dma_reset(sPAPRPHBState *sphb);

#endif /* PCI_HOST_SPAPR_H */
