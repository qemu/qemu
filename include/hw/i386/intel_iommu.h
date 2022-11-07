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

#include "hw/i386/x86-iommu.h"
#include "qemu/iova-tree.h"
#include "qom/object.h"

#define TYPE_INTEL_IOMMU_DEVICE "intel-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(IntelIOMMUState, INTEL_IOMMU_DEVICE)

#define TYPE_INTEL_IOMMU_MEMORY_REGION "intel-iommu-iommu-memory-region"

/* DMAR Hardware Unit Definition address (IOMMU unit) */
#define Q35_HOST_BRIDGE_IOMMU_ADDR  0xfed90000ULL

#define VTD_PCI_BUS_MAX             256
#define VTD_PCI_SLOT_MAX            32
#define VTD_PCI_FUNC_MAX            8
#define VTD_PCI_SLOT(devfn)         (((devfn) >> 3) & 0x1f)
#define VTD_PCI_FUNC(devfn)         ((devfn) & 0x07)
#define VTD_SID_TO_BUS(sid)         (((sid) >> 8) & 0xff)
#define VTD_SID_TO_DEVFN(sid)       ((sid) & 0xff)

#define DMAR_REG_SIZE               0x230
#define VTD_HOST_AW_39BIT           39
#define VTD_HOST_AW_48BIT           48
#define VTD_HOST_ADDRESS_WIDTH      VTD_HOST_AW_39BIT
#define VTD_HAW_MASK(aw)            ((1ULL << (aw)) - 1)

#define DMAR_REPORT_F_INTR          (1)

#define  VTD_MSI_ADDR_HI_MASK        (0xffffffff00000000ULL)
#define  VTD_MSI_ADDR_HI_SHIFT       (32)
#define  VTD_MSI_ADDR_LO_MASK        (0x00000000ffffffffULL)

typedef struct VTDContextEntry VTDContextEntry;
typedef struct VTDContextCacheEntry VTDContextCacheEntry;
typedef struct VTDAddressSpace VTDAddressSpace;
typedef struct VTDIOTLBEntry VTDIOTLBEntry;
typedef union VTD_IR_TableEntry VTD_IR_TableEntry;
typedef union VTD_IR_MSIAddress VTD_IR_MSIAddress;
typedef struct VTDPASIDDirEntry VTDPASIDDirEntry;
typedef struct VTDPASIDEntry VTDPASIDEntry;

/* Context-Entry */
struct VTDContextEntry {
    union {
        struct {
            uint64_t lo;
            uint64_t hi;
        };
        struct {
            uint64_t val[4];
        };
    };
};

struct VTDContextCacheEntry {
    /* The cache entry is obsolete if
     * context_cache_gen!=IntelIOMMUState.context_cache_gen
     */
    uint32_t context_cache_gen;
    struct VTDContextEntry context_entry;
};

/* PASID Directory Entry */
struct VTDPASIDDirEntry {
    uint64_t val;
};

/* PASID Table Entry */
struct VTDPASIDEntry {
    uint64_t val[8];
};

struct VTDAddressSpace {
    PCIBus *bus;
    uint8_t devfn;
    uint32_t pasid;
    AddressSpace as;
    IOMMUMemoryRegion iommu;
    MemoryRegion root;          /* The root container of the device */
    MemoryRegion nodmar;        /* The alias of shared nodmar MR */
    MemoryRegion iommu_ir;      /* Interrupt region: 0xfeeXXXXX */
    MemoryRegion iommu_ir_fault; /* Interrupt region for catching fault */
    IntelIOMMUState *iommu_state;
    VTDContextCacheEntry context_cache_entry;
    QLIST_ENTRY(VTDAddressSpace) next;
    /* Superset of notifier flags that this address space has */
    IOMMUNotifierFlag notifier_flags;
    IOVATree *iova_tree;          /* Traces mapped IOVA ranges */
};

struct VTDIOTLBEntry {
    uint64_t gfn;
    uint16_t domain_id;
    uint32_t pasid;
    uint64_t slpte;
    uint64_t mask;
    uint8_t access_flags;
};

/* VT-d Source-ID Qualifier types */
enum {
    VTD_SQ_FULL = 0x00,     /* Full SID verification */
    VTD_SQ_IGN_3 = 0x01,    /* Ignore bit 3 */
    VTD_SQ_IGN_2_3 = 0x02,  /* Ignore bits 2 & 3 */
    VTD_SQ_IGN_1_3 = 0x03,  /* Ignore bits 1-3 */
    VTD_SQ_MAX,
};

/* VT-d Source Validation Types */
enum {
    VTD_SVT_NONE = 0x00,    /* No validation */
    VTD_SVT_ALL = 0x01,     /* Do full validation */
    VTD_SVT_BUS = 0x02,     /* Validate bus range */
    VTD_SVT_MAX,
};

/* Interrupt Remapping Table Entry Definition */
union VTD_IR_TableEntry {
    struct {
#if HOST_BIG_ENDIAN
        uint32_t __reserved_1:8;     /* Reserved 1 */
        uint32_t vector:8;           /* Interrupt Vector */
        uint32_t irte_mode:1;        /* IRTE Mode */
        uint32_t __reserved_0:3;     /* Reserved 0 */
        uint32_t __avail:4;          /* Available spaces for software */
        uint32_t delivery_mode:3;    /* Delivery Mode */
        uint32_t trigger_mode:1;     /* Trigger Mode */
        uint32_t redir_hint:1;       /* Redirection Hint */
        uint32_t dest_mode:1;        /* Destination Mode */
        uint32_t fault_disable:1;    /* Fault Processing Disable */
        uint32_t present:1;          /* Whether entry present/available */
#else
        uint32_t present:1;          /* Whether entry present/available */
        uint32_t fault_disable:1;    /* Fault Processing Disable */
        uint32_t dest_mode:1;        /* Destination Mode */
        uint32_t redir_hint:1;       /* Redirection Hint */
        uint32_t trigger_mode:1;     /* Trigger Mode */
        uint32_t delivery_mode:3;    /* Delivery Mode */
        uint32_t __avail:4;          /* Available spaces for software */
        uint32_t __reserved_0:3;     /* Reserved 0 */
        uint32_t irte_mode:1;        /* IRTE Mode */
        uint32_t vector:8;           /* Interrupt Vector */
        uint32_t __reserved_1:8;     /* Reserved 1 */
#endif
        uint32_t dest_id;            /* Destination ID */
        uint16_t source_id;          /* Source-ID */
#if HOST_BIG_ENDIAN
        uint64_t __reserved_2:44;    /* Reserved 2 */
        uint64_t sid_vtype:2;        /* Source-ID Validation Type */
        uint64_t sid_q:2;            /* Source-ID Qualifier */
#else
        uint64_t sid_q:2;            /* Source-ID Qualifier */
        uint64_t sid_vtype:2;        /* Source-ID Validation Type */
        uint64_t __reserved_2:44;    /* Reserved 2 */
#endif
    } QEMU_PACKED irte;
    uint64_t data[2];
};

#define VTD_IR_INT_FORMAT_COMPAT     (0) /* Compatible Interrupt */
#define VTD_IR_INT_FORMAT_REMAP      (1) /* Remappable Interrupt */

/* Programming format for MSI/MSI-X addresses */
union VTD_IR_MSIAddress {
    struct {
#if HOST_BIG_ENDIAN
        uint32_t __head:12;          /* Should always be: 0x0fee */
        uint32_t index_l:15;         /* Interrupt index bit 14-0 */
        uint32_t int_mode:1;         /* Interrupt format */
        uint32_t sub_valid:1;        /* SHV: Sub-Handle Valid bit */
        uint32_t index_h:1;          /* Interrupt index bit 15 */
        uint32_t __not_care:2;
#else
        uint32_t __not_care:2;
        uint32_t index_h:1;          /* Interrupt index bit 15 */
        uint32_t sub_valid:1;        /* SHV: Sub-Handle Valid bit */
        uint32_t int_mode:1;         /* Interrupt format */
        uint32_t index_l:15;         /* Interrupt index bit 14-0 */
        uint32_t __head:12;          /* Should always be: 0x0fee */
#endif
    } QEMU_PACKED addr;
    uint32_t data;
};

/* When IR is enabled, all MSI/MSI-X data bits should be zero */
#define VTD_IR_MSI_DATA          (0)

/* The iommu (DMAR) device state struct */
struct IntelIOMMUState {
    X86IOMMUState x86_iommu;
    MemoryRegion csrmem;
    MemoryRegion mr_nodmar;
    MemoryRegion mr_ir;
    MemoryRegion mr_sys_alias;
    uint8_t csr[DMAR_REG_SIZE];     /* register values */
    uint8_t wmask[DMAR_REG_SIZE];   /* R/W bytes */
    uint8_t w1cmask[DMAR_REG_SIZE]; /* RW1C(Write 1 to Clear) bytes */
    uint8_t womask[DMAR_REG_SIZE];  /* WO (write only - read returns 0) */
    uint32_t version;

    bool caching_mode;              /* RO - is cap CM enabled? */
    bool scalable_mode;             /* RO - is Scalable Mode supported? */
    bool snoop_control;             /* RO - is SNP filed supported? */

    dma_addr_t root;                /* Current root table pointer */
    bool root_scalable;             /* Type of root table (scalable or not) */
    bool dmar_enabled;              /* Set if DMA remapping is enabled */

    uint16_t iq_head;               /* Current invalidation queue head */
    uint16_t iq_tail;               /* Current invalidation queue tail */
    dma_addr_t iq;                  /* Current invalidation queue pointer */
    uint16_t iq_size;               /* IQ Size in number of entries */
    bool iq_dw;                     /* IQ descriptor width 256bit or not */
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

    GHashTable *vtd_address_spaces;             /* VTD address spaces */
    VTDAddressSpace *vtd_as_cache[VTD_PCI_BUS_MAX]; /* VTD address space cache */
    /* list of registered notifiers */
    QLIST_HEAD(, VTDAddressSpace) vtd_as_with_notifiers;

    /* interrupt remapping */
    bool intr_enabled;              /* Whether guest enabled IR */
    dma_addr_t intr_root;           /* Interrupt remapping table pointer */
    uint32_t intr_size;             /* Number of IR table entries */
    bool intr_eime;                 /* Extended interrupt mode enabled */
    OnOffAuto intr_eim;             /* Toggle for EIM cabability */
    bool buggy_eim;                 /* Force buggy EIM unless eim=off */
    uint8_t aw_bits;                /* Host/IOVA address width (in bits) */
    bool dma_drain;                 /* Whether DMA r/w draining enabled */
    bool dma_translation;           /* Whether DMA translation supported */
    bool pasid;                     /* Whether to support PASID */

    /*
     * Protects IOMMU states in general.  Currently it protects the
     * per-IOMMU IOTLB cache, and context entry cache in VTDAddressSpace.
     */
    QemuMutex iommu_lock;
};

/* Find the VTD Address space associated with the given bus pointer,
 * create a new one if none exists
 */
VTDAddressSpace *vtd_find_add_as(IntelIOMMUState *s, PCIBus *bus,
                                 int devfn, unsigned int pasid);

#endif
