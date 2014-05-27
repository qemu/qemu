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
#if !defined(__HW_SPAPR_H__)
#error Please include spapr.h before this file!
#endif

#if !defined(__HW_SPAPR_PCI_H__)
#define __HW_SPAPR_PCI_H__

#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/ppc/xics.h"

#define SPAPR_MSIX_MAX_DEVS 32

#define TYPE_SPAPR_PCI_HOST_BRIDGE "spapr-pci-host-bridge"

#define SPAPR_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(sPAPRPHBState, (obj), TYPE_SPAPR_PCI_HOST_BRIDGE)

#define SPAPR_PCI_HOST_BRIDGE_CLASS(klass) \
     OBJECT_CLASS_CHECK(sPAPRPHBClass, (klass), TYPE_SPAPR_PCI_HOST_BRIDGE)
#define SPAPR_PCI_HOST_BRIDGE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(sPAPRPHBClass, (obj), TYPE_SPAPR_PCI_HOST_BRIDGE)

typedef struct sPAPRPHBClass sPAPRPHBClass;
typedef struct sPAPRPHBState sPAPRPHBState;

struct sPAPRPHBClass {
    PCIHostBridgeClass parent_class;

    void (*finish_realize)(sPAPRPHBState *sphb, Error **errp);
};

struct sPAPRPHBState {
    PCIHostState parent_obj;

    int32_t index;
    uint64_t buid;
    char *dtbusname;

    MemoryRegion memspace, iospace;
    hwaddr mem_win_addr, mem_win_size, io_win_addr, io_win_size;
    MemoryRegion memwindow, iowindow;

    uint32_t dma_liobn;
    AddressSpace iommu_as;
    MemoryRegion iommu_root;

    struct spapr_pci_lsi {
        uint32_t irq;
    } lsi_table[PCI_NUM_PINS];

    struct spapr_pci_msi {
        uint32_t config_addr;
        uint32_t irq;
        uint32_t nvec;
    } msi_table[SPAPR_MSIX_MAX_DEVS];

    QLIST_ENTRY(sPAPRPHBState) list;
};

#define SPAPR_PCI_BASE_BUID          0x800000020000000ULL

#define SPAPR_PCI_WINDOW_BASE        0x10000000000ULL
#define SPAPR_PCI_WINDOW_SPACING     0x1000000000ULL
#define SPAPR_PCI_MMIO_WIN_OFF       0xA0000000
#define SPAPR_PCI_MMIO_WIN_SIZE      0x20000000
#define SPAPR_PCI_IO_WIN_OFF         0x80000000
#define SPAPR_PCI_IO_WIN_SIZE        0x10000

#define SPAPR_PCI_MSI_WINDOW         0x40000000000ULL

#define SPAPR_PCI_MEM_WIN_BUS_OFFSET 0x80000000ULL

static inline qemu_irq spapr_phb_lsi_qirq(struct sPAPRPHBState *phb, int pin)
{
    return xics_get_qirq(spapr->icp, phb->lsi_table[pin].irq);
}

PCIHostState *spapr_create_phb(sPAPREnvironment *spapr, int index);

int spapr_populate_pci_dt(sPAPRPHBState *phb,
                          uint32_t xics_phandle,
                          void *fdt);

void spapr_pci_msi_init(sPAPREnvironment *spapr, hwaddr addr);

void spapr_pci_rtas_init(void);

#endif /* __HW_SPAPR_PCI_H__ */
