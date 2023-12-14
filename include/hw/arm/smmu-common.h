/*
 * ARM SMMU Support
 *
 * Copyright (C) 2015-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef HW_ARM_SMMU_COMMON_H
#define HW_ARM_SMMU_COMMON_H

#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "qom/object.h"

#define SMMU_PCI_BUS_MAX                    256
#define SMMU_PCI_DEVFN_MAX                  256
#define SMMU_PCI_DEVFN(sid)                 (sid & 0xFF)

/* VMSAv8-64 Translation constants and functions */
#define VMSA_LEVELS                         4
#define VMSA_MAX_S2_CONCAT                  16

#define VMSA_STRIDE(gran)                   ((gran) - VMSA_LEVELS + 1)
#define VMSA_BIT_LVL(isz, strd, lvl)        ((isz) - (strd) * \
                                             (VMSA_LEVELS - (lvl)))
#define VMSA_IDXMSK(isz, strd, lvl)         ((1ULL << \
                                             VMSA_BIT_LVL(isz, strd, lvl)) - 1)

/*
 * Page table walk error types
 */
typedef enum {
    SMMU_PTW_ERR_NONE,
    SMMU_PTW_ERR_WALK_EABT,   /* Translation walk external abort */
    SMMU_PTW_ERR_TRANSLATION, /* Translation fault */
    SMMU_PTW_ERR_ADDR_SIZE,   /* Address Size fault */
    SMMU_PTW_ERR_ACCESS,      /* Access fault */
    SMMU_PTW_ERR_PERMISSION,  /* Permission fault */
} SMMUPTWEventType;

typedef struct SMMUPTWEventInfo {
    int stage;
    SMMUPTWEventType type;
    dma_addr_t addr; /* fetched address that induced an abort, if any */
} SMMUPTWEventInfo;

typedef struct SMMUTransTableInfo {
    bool disabled;             /* is the translation table disabled? */
    uint64_t ttb;              /* TT base address */
    uint8_t tsz;               /* input range, ie. 2^(64 -tsz)*/
    uint8_t granule_sz;        /* granule page shift */
    bool had;                  /* hierarchical attribute disable */
} SMMUTransTableInfo;

typedef struct SMMUTLBEntry {
    IOMMUTLBEntry entry;
    uint8_t level;
    uint8_t granule;
} SMMUTLBEntry;

/* Stage-2 configuration. */
typedef struct SMMUS2Cfg {
    uint8_t tsz;            /* Size of IPA input region (S2T0SZ) */
    uint8_t sl0;            /* Start level of translation (S2SL0) */
    bool affd;              /* AF Fault Disable (S2AFFD) */
    bool record_faults;     /* Record fault events (S2R) */
    uint8_t granule_sz;     /* Granule page shift (based on S2TG) */
    uint8_t eff_ps;         /* Effective PA output range (based on S2PS) */
    uint16_t vmid;          /* Virtual Machine ID (S2VMID) */
    uint64_t vttb;          /* Address of translation table base (S2TTB) */
} SMMUS2Cfg;

/*
 * Generic structure populated by derived SMMU devices
 * after decoding the configuration information and used as
 * input to the page table walk
 */
typedef struct SMMUTransCfg {
    /* Shared fields between stage-1 and stage-2. */
    int stage;                 /* translation stage */
    bool disabled;             /* smmu is disabled */
    bool bypassed;             /* translation is bypassed */
    bool aborted;              /* translation is aborted */
    uint32_t iotlb_hits;       /* counts IOTLB hits */
    uint32_t iotlb_misses;     /* counts IOTLB misses*/
    /* Used by stage-1 only. */
    bool aa64;                 /* arch64 or aarch32 translation table */
    bool record_faults;        /* record fault events */
    uint64_t ttb;              /* TT base address */
    uint8_t oas;               /* output address width */
    uint8_t tbi;               /* Top Byte Ignore */
    uint16_t asid;
    SMMUTransTableInfo tt[2];
    /* Used by stage-2 only. */
    struct SMMUS2Cfg s2cfg;
} SMMUTransCfg;

typedef struct SMMUDevice {
    void               *smmu;
    PCIBus             *bus;
    int                devfn;
    IOMMUMemoryRegion  iommu;
    AddressSpace       as;
    uint32_t           cfg_cache_hits;
    uint32_t           cfg_cache_misses;
    QLIST_ENTRY(SMMUDevice) next;
} SMMUDevice;

typedef struct SMMUPciBus {
    PCIBus       *bus;
    SMMUDevice   *pbdev[]; /* Parent array is sparse, so dynamically alloc */
} SMMUPciBus;

typedef struct SMMUIOTLBKey {
    uint64_t iova;
    uint16_t asid;
    uint16_t vmid;
    uint8_t tg;
    uint8_t level;
} SMMUIOTLBKey;

struct SMMUState {
    /* <private> */
    SysBusDevice  dev;
    const char *mrtypename;
    MemoryRegion iomem;

    GHashTable *smmu_pcibus_by_busptr;
    GHashTable *configs; /* cache for configuration data */
    GHashTable *iotlb;
    SMMUPciBus *smmu_pcibus_by_bus_num[SMMU_PCI_BUS_MAX];
    PCIBus *pci_bus;
    QLIST_HEAD(, SMMUDevice) devices_with_notifiers;
    uint8_t bus_num;
    PCIBus *primary_bus;
};

struct SMMUBaseClass {
    /* <private> */
    SysBusDeviceClass parent_class;

    /*< public >*/

    DeviceRealize parent_realize;

};

#define TYPE_ARM_SMMU "arm-smmu"
OBJECT_DECLARE_TYPE(SMMUState, SMMUBaseClass, ARM_SMMU)

/* Return the SMMUPciBus handle associated to a PCI bus number */
SMMUPciBus *smmu_find_smmu_pcibus(SMMUState *s, uint8_t bus_num);

/* Return the stream ID of an SMMU device */
static inline uint16_t smmu_get_sid(SMMUDevice *sdev)
{
    return PCI_BUILD_BDF(pci_bus_num(sdev->bus), sdev->devfn);
}

/**
 * smmu_ptw - Perform the page table walk for a given iova / access flags
 * pair, according to @cfg translation config
 */
int smmu_ptw(SMMUTransCfg *cfg, dma_addr_t iova, IOMMUAccessFlags perm,
             SMMUTLBEntry *tlbe, SMMUPTWEventInfo *info);

/**
 * select_tt - compute which translation table shall be used according to
 * the input iova and translation config and return the TT specific info
 */
SMMUTransTableInfo *select_tt(SMMUTransCfg *cfg, dma_addr_t iova);

/* Return the iommu mr associated to @sid, or NULL if none */
IOMMUMemoryRegion *smmu_iommu_mr(SMMUState *s, uint32_t sid);

#define SMMU_IOTLB_MAX_SIZE 256

SMMUTLBEntry *smmu_iotlb_lookup(SMMUState *bs, SMMUTransCfg *cfg,
                                SMMUTransTableInfo *tt, hwaddr iova);
void smmu_iotlb_insert(SMMUState *bs, SMMUTransCfg *cfg, SMMUTLBEntry *entry);
SMMUIOTLBKey smmu_get_iotlb_key(uint16_t asid, uint16_t vmid, uint64_t iova,
                                uint8_t tg, uint8_t level);
void smmu_iotlb_inv_all(SMMUState *s);
void smmu_iotlb_inv_asid(SMMUState *s, uint16_t asid);
void smmu_iotlb_inv_vmid(SMMUState *s, uint16_t vmid);
void smmu_iotlb_inv_iova(SMMUState *s, int asid, int vmid, dma_addr_t iova,
                         uint8_t tg, uint64_t num_pages, uint8_t ttl);

/* Unmap the range of all the notifiers registered to any IOMMU mr */
void smmu_inv_notifiers_all(SMMUState *s);

#endif /* HW_ARM_SMMU_COMMON_H */
