/*
 * QEMU emulation of an RISC-V IOMMU
 *
 * Copyright (C) 2022-2023 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_IOMMU_STATE_H
#define HW_RISCV_IOMMU_STATE_H

#include "qom/object.h"
#include "hw/qdev-properties.h"
#include "system/dma.h"
#include "hw/riscv/iommu.h"
#include "hw/riscv/riscv-iommu-bits.h"

typedef enum riscv_iommu_igs_modes riscv_iommu_igs_mode;

struct RISCVIOMMUState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    uint32_t version;     /* Reported interface version number */
    uint32_t pid_bits;    /* process identifier width */
    uint32_t bus;         /* PCI bus mapping for non-root endpoints */

    uint64_t cap;         /* IOMMU supported capabilities */
    uint64_t fctl;        /* IOMMU enabled features */
    uint64_t icvec_avail_vectors;  /* Available interrupt vectors in ICVEC */

    bool enable_off;      /* Enable out-of-reset OFF mode (DMA disabled) */
    bool enable_msi;      /* Enable MSI remapping */
    bool enable_ats;      /* Enable ATS support */
    bool enable_s_stage;  /* Enable S/VS-Stage translation */
    bool enable_g_stage;  /* Enable G-Stage translation */

    /* IOMMU Internal State */
    uint64_t ddtp;        /* Validated Device Directory Tree Root Pointer */

    dma_addr_t cq_addr;   /* Command queue base physical address */
    dma_addr_t fq_addr;   /* Fault/event queue base physical address */
    dma_addr_t pq_addr;   /* Page request queue base physical address */

    uint32_t cq_mask;     /* Command queue index bit mask */
    uint32_t fq_mask;     /* Fault/event queue index bit mask */
    uint32_t pq_mask;     /* Page request queue index bit mask */

    /* interrupt notifier */
    void (*notify)(RISCVIOMMUState *iommu, unsigned vector);

    /* IOMMU target address space */
    AddressSpace *target_as;
    MemoryRegion *target_mr;

    /* MSI / MRIF access trap */
    AddressSpace trap_as;
    MemoryRegion trap_mr;

    GHashTable *ctx_cache;          /* Device translation Context Cache */

    GHashTable *iot_cache;          /* IO Translated Address Cache */
    unsigned iot_limit;             /* IO Translation Cache size limit */

    /* MMIO Hardware Interface */
    MemoryRegion regs_mr;
    uint8_t *regs_rw;  /* register state (user write) */
    uint8_t *regs_wc;  /* write-1-to-clear mask */
    uint8_t *regs_ro;  /* read-only mask */

    QLIST_ENTRY(RISCVIOMMUState) iommus;
    QLIST_HEAD(, RISCVIOMMUSpace) spaces;

    /* HPM cycle counter */
    QEMUTimer *hpm_timer;
    uint64_t hpmcycle_val;      /* Current value of cycle register */
    uint64_t hpmcycle_prev;     /* Saved value of QEMU_CLOCK_VIRTUAL clock */
    uint64_t irq_overflow_left; /* Value beyond INT64_MAX after overflow */

    /* HPM event counters */
    GHashTable *hpm_event_ctr_map; /* Mapping of events to counters */
    uint8_t hpm_cntrs;
};

void riscv_iommu_pci_setup_iommu(RISCVIOMMUState *iommu, PCIBus *bus,
         Error **errp);
void riscv_iommu_set_cap_igs(RISCVIOMMUState *s, riscv_iommu_igs_mode mode);
void riscv_iommu_reset(RISCVIOMMUState *s);
void riscv_iommu_notify(RISCVIOMMUState *s, int vec_type);

typedef struct RISCVIOMMUContext RISCVIOMMUContext;
/* Device translation context state. */
struct RISCVIOMMUContext {
    uint64_t devid:24;          /* Requester Id, AKA device_id */
    uint64_t process_id:20;     /* Process ID. PASID for PCIe */
    uint64_t tc;                /* Translation Control */
    uint64_t ta;                /* Translation Attributes */
    uint64_t satp;              /* S-Stage address translation and protection */
    uint64_t gatp;              /* G-Stage address translation and protection */
    uint64_t msi_addr_mask;     /* MSI filtering - address mask */
    uint64_t msi_addr_pattern;  /* MSI filtering - address pattern */
    uint64_t msiptp;            /* MSI redirection page table pointer */
};

/* private helpers */

/* Register helper functions */
static inline uint32_t riscv_iommu_reg_mod32(RISCVIOMMUState *s,
    unsigned idx, uint32_t set, uint32_t clr)
{
    uint32_t val = ldl_le_p(s->regs_rw + idx);
    stl_le_p(s->regs_rw + idx, (val & ~clr) | set);
    return val;
}

static inline void riscv_iommu_reg_set32(RISCVIOMMUState *s, unsigned idx,
                                         uint32_t set)
{
    stl_le_p(s->regs_rw + idx, set);
}

static inline uint32_t riscv_iommu_reg_get32(RISCVIOMMUState *s, unsigned idx)
{
    return ldl_le_p(s->regs_rw + idx);
}

static inline uint64_t riscv_iommu_reg_mod64(RISCVIOMMUState *s, unsigned idx,
                                             uint64_t set, uint64_t clr)
{
    uint64_t val = ldq_le_p(s->regs_rw + idx);
    stq_le_p(s->regs_rw + idx, (val & ~clr) | set);
    return val;
}

static inline void riscv_iommu_reg_set64(RISCVIOMMUState *s, unsigned idx,
                                         uint64_t set)
{
    stq_le_p(s->regs_rw + idx, set);
}

static inline uint64_t riscv_iommu_reg_get64(RISCVIOMMUState *s,
    unsigned idx)
{
    return ldq_le_p(s->regs_rw + idx);
}
#endif
