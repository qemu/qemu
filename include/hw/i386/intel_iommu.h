/*
 * QEMU emulation of an Intel IOMMU (VT-d)
 *   (DMA Remapping device)
 *
 * Copyright (C) 2013 Knut Omang, Oracle <knut.omang@oracle.com>
 * Copyright (C) 2014 Le Tan, <tamlokveer@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INTEL_IOMMU_H
#define INTEL_IOMMU_H
#include "hw/qdev.h"
#include "sysemu/dma.h"

#define TYPE_INTEL_IOMMU_DEVICE "intel-iommu"
#define INTEL_IOMMU_DEVICE(obj) \
     OBJECT_CHECK(IntelIOMMUState, (obj), TYPE_INTEL_IOMMU_DEVICE)

/* DMAR Hardware Unit Definition address (IOMMU unit) */
#define Q35_HOST_BRIDGE_IOMMU_ADDR  0xfed90000ULL

#define VTD_PCI_BUS_MAX             256
#define VTD_PCI_SLOT_MAX            32
#define VTD_PCI_FUNC_MAX            8
#define VTD_PCI_DEVFN_MAX           256
#define VTD_PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define VTD_PCI_FUNC(devfn)         ((devfn) & 0x07)
#define VTD_SID_TO_BUS(sid)         (((sid) >> 8) && 0xff)
#define VTD_SID_TO_DEVFN(sid)       ((sid) & 0xff)

#define DMAR_REG_SIZE               0x230
#define VTD_HOST_ADDRESS_WIDTH      39
#define VTD_HAW_MASK                ((1ULL << VTD_HOST_ADDRESS_WIDTH) - 1)

typedef struct VTDContextEntry VTDContextEntry;
typedef struct VTDContextCacheEntry VTDContextCacheEntry;
typedef struct IntelIOMMUState IntelIOMMUState;
typedef struct VTDAddressSpace VTDAddressSpace;
typedef struct VTDIOTLBEntry VTDIOTLBEntry;

/* Context-Entry */
struct VTDContextEntry {
    uint64_t lo;
    uint64_t hi;
};

struct VTDContextCacheEntry {
    /* The cache entry is obsolete if
     * context_cache_gen!=IntelIOMMUState.context_cache_gen
     */
    uint32_t context_cache_gen;
    struct VTDContextEntry context_entry;
};

struct VTDAddressSpace {
    uint8_t bus_num;
    uint8_t devfn;
    AddressSpace as;
    MemoryRegion iommu;
    IntelIOMMUState *iommu_state;
    VTDContextCacheEntry context_cache_entry;
};

struct VTDIOTLBEntry {
    uint64_t gfn;
    uint16_t domain_id;
    uint64_t slpte;
    bool read_flags;
    bool write_flags;
};

/* The iommu (DMAR) device state struct */
struct IntelIOMMUState {
    SysBusDevice busdev;
    MemoryRegion csrmem;
    uint8_t csr[DMAR_REG_SIZE];     /* register values */
    uint8_t wmask[DMAR_REG_SIZE];   /* R/W bytes */
    uint8_t w1cmask[DMAR_REG_SIZE]; /* RW1C(Write 1 to Clear) bytes */
    uint8_t womask[DMAR_REG_SIZE];  /* WO (write only - read returns 0) */
    uint32_t version;

    dma_addr_t root;                /* Current root table pointer */
    bool root_extended;             /* Type of root table (extended or not) */
    bool dmar_enabled;              /* Set if DMA remapping is enabled */

    uint16_t iq_head;               /* Current invalidation queue head */
    uint16_t iq_tail;               /* Current invalidation queue tail */
    dma_addr_t iq;                  /* Current invalidation queue pointer */
    uint16_t iq_size;               /* IQ Size in number of entries */
    bool qi_enabled;                /* Set if the QI is enabled */
    uint8_t iq_last_desc_type;      /* The type of last completed descriptor */

    /* The index of the Fault Recording Register to be used next.
     * Wraps around from N-1 to 0, where N is the number of FRCD_REG.
     */
    uint16_t next_frcd_reg;

    uint64_t cap;                   /* The value of capability reg */
    uint64_t ecap;                  /* The value of extended capability reg */

    uint32_t context_cache_gen;     /* Should be in [1,MAX] */
    GHashTable *iotlb;              /* IOTLB */

    MemoryRegionIOMMUOps iommu_ops;
    VTDAddressSpace **address_spaces[VTD_PCI_BUS_MAX];
};

#endif
