/*
 * QEMU sPAPR IOMMU (TCE) code
 *
 * Copyright (c) 2010 David Gibson, IBM Corporation <dwg@au1.ibm.com>
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
#include "hw/hw.h"
#include "sysemu/kvm.h"
#include "hw/qdev.h"
#include "kvm_ppc.h"
#include "sysemu/dma.h"
#include "exec/address-spaces.h"

#include "hw/ppc/spapr.h"

#include <libfdt.h>

/* #define DEBUG_TCE */

enum sPAPRTCEAccess {
    SPAPR_TCE_FAULT = 0,
    SPAPR_TCE_RO = 1,
    SPAPR_TCE_WO = 2,
    SPAPR_TCE_RW = 3,
};

struct sPAPRTCETable {
    uint32_t liobn;
    uint32_t window_size;
    sPAPRTCE *table;
    bool bypass;
    int fd;
    MemoryRegion iommu;
    QLIST_ENTRY(sPAPRTCETable) list;
};


QLIST_HEAD(spapr_tce_tables, sPAPRTCETable) spapr_tce_tables;

static sPAPRTCETable *spapr_tce_find_by_liobn(uint32_t liobn)
{
    sPAPRTCETable *tcet;

    if (liobn & 0xFFFFFFFF00000000ULL) {
        hcall_dprintf("Request for out-of-bounds LIOBN 0x" TARGET_FMT_lx "\n",
                      liobn);
        return NULL;
    }

    QLIST_FOREACH(tcet, &spapr_tce_tables, list) {
        if (tcet->liobn == liobn) {
            return tcet;
        }
    }

    return NULL;
}

static IOMMUTLBEntry spapr_tce_translate_iommu(MemoryRegion *iommu, hwaddr addr)
{
    sPAPRTCETable *tcet = container_of(iommu, sPAPRTCETable, iommu);
    uint64_t tce;

#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_tce_translate liobn=0x%" PRIx32 " addr=0x"
            DMA_ADDR_FMT "\n", tcet->liobn, addr);
#endif

    if (tcet->bypass) {
        return (IOMMUTLBEntry) {
            .target_as = &address_space_memory,
            .iova = 0,
            .translated_addr = 0,
            .addr_mask = ~(hwaddr)0,
            .perm = IOMMU_RW,
        };
    }

    /* Check if we are in bound */
    if (addr >= tcet->window_size) {
#ifdef DEBUG_TCE
        fprintf(stderr, "spapr_tce_translate out of bounds\n");
#endif
        return (IOMMUTLBEntry) { .perm = IOMMU_NONE };
    }

    tce = tcet->table[addr >> SPAPR_TCE_PAGE_SHIFT].tce;

#ifdef DEBUG_TCE
    fprintf(stderr, " ->  *paddr=0x%llx, *len=0x%llx\n",
            (tce & ~SPAPR_TCE_PAGE_MASK), SPAPR_TCE_PAGE_MASK + 1);
#endif

    return (IOMMUTLBEntry) {
        .target_as = &address_space_memory,
        .iova = addr & ~SPAPR_TCE_PAGE_MASK,
        .translated_addr = tce & ~SPAPR_TCE_PAGE_MASK,
        .addr_mask = SPAPR_TCE_PAGE_MASK,
        .perm = tce,
    };
}

static MemoryRegionIOMMUOps spapr_iommu_ops = {
    .translate = spapr_tce_translate_iommu,
};

sPAPRTCETable *spapr_tce_new_table(uint32_t liobn, size_t window_size)
{
    sPAPRTCETable *tcet;

    if (spapr_tce_find_by_liobn(liobn)) {
        fprintf(stderr, "Attempted to create TCE table with duplicate"
                " LIOBN 0x%x\n", liobn);
        return NULL;
    }

    if (!window_size) {
        return NULL;
    }

    tcet = g_malloc0(sizeof(*tcet));
    tcet->liobn = liobn;
    tcet->window_size = window_size;

    if (kvm_enabled()) {
        tcet->table = kvmppc_create_spapr_tce(liobn,
                                              window_size,
                                              &tcet->fd);
    }

    if (!tcet->table) {
        size_t table_size = (window_size >> SPAPR_TCE_PAGE_SHIFT)
            * sizeof(sPAPRTCE);
        tcet->table = g_malloc0(table_size);
    }

#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_iommu: New TCE table @ %p, liobn=0x%x, "
            "table @ %p, fd=%d\n", tcet, liobn, tcet->table, tcet->fd);
#endif

    memory_region_init_iommu(&tcet->iommu, &spapr_iommu_ops,
                             "iommu-spapr", UINT64_MAX);

    QLIST_INSERT_HEAD(&spapr_tce_tables, tcet, list);

    return tcet;
}

void spapr_tce_free(sPAPRTCETable *tcet)
{
    QLIST_REMOVE(tcet, list);

    if (!kvm_enabled() ||
        (kvmppc_remove_spapr_tce(tcet->table, tcet->fd,
                                 tcet->window_size) != 0)) {
        g_free(tcet->table);
    }

    g_free(tcet);
}

MemoryRegion *spapr_tce_get_iommu(sPAPRTCETable *tcet)
{
    return &tcet->iommu;
}

void spapr_tce_set_bypass(sPAPRTCETable *tcet, bool bypass)
{
    tcet->bypass = bypass;
}

void spapr_tce_reset(sPAPRTCETable *tcet)
{
    size_t table_size = (tcet->window_size >> SPAPR_TCE_PAGE_SHIFT)
        * sizeof(sPAPRTCE);

    tcet->bypass = false;
    memset(tcet->table, 0, table_size);
}

static target_ulong put_tce_emu(sPAPRTCETable *tcet, target_ulong ioba,
                                target_ulong tce)
{
    sPAPRTCE *tcep;
    IOMMUTLBEntry entry;

    if (ioba >= tcet->window_size) {
        hcall_dprintf("spapr_vio_put_tce on out-of-bounds IOBA 0x"
                      TARGET_FMT_lx "\n", ioba);
        return H_PARAMETER;
    }

    tcep = tcet->table + (ioba >> SPAPR_TCE_PAGE_SHIFT);
    tcep->tce = tce;

    entry.target_as = &address_space_memory,
    entry.iova = ioba & ~SPAPR_TCE_PAGE_MASK;
    entry.translated_addr = tce & ~SPAPR_TCE_PAGE_MASK;
    entry.addr_mask = SPAPR_TCE_PAGE_MASK;
    entry.perm = tce;
    memory_region_notify_iommu(&tcet->iommu, entry);

    return H_SUCCESS;
}

static target_ulong h_put_tce(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce = args[2];
    sPAPRTCETable *tcet = spapr_tce_find_by_liobn(liobn);

    ioba &= ~(SPAPR_TCE_PAGE_SIZE - 1);

    if (tcet) {
        return put_tce_emu(tcet, ioba, tce);
    }
#ifdef DEBUG_TCE
    fprintf(stderr, "%s on liobn=" TARGET_FMT_lx /*%s*/
            "  ioba 0x" TARGET_FMT_lx "  TCE 0x" TARGET_FMT_lx "\n",
            __func__, liobn, /*dev->qdev.id, */ioba, tce);
#endif

    return H_PARAMETER;
}

void spapr_iommu_init(void)
{
    QLIST_INIT(&spapr_tce_tables);

    /* hcall-tce */
    spapr_register_hypercall(H_PUT_TCE, h_put_tce);
}

int spapr_dma_dt(void *fdt, int node_off, const char *propname,
                 uint32_t liobn, uint64_t window, uint32_t size)
{
    uint32_t dma_prop[5];
    int ret;

    dma_prop[0] = cpu_to_be32(liobn);
    dma_prop[1] = cpu_to_be32(window >> 32);
    dma_prop[2] = cpu_to_be32(window & 0xFFFFFFFF);
    dma_prop[3] = 0; /* window size is 32 bits */
    dma_prop[4] = cpu_to_be32(size);

    ret = fdt_setprop_cell(fdt, node_off, "ibm,#dma-address-cells", 2);
    if (ret < 0) {
        return ret;
    }

    ret = fdt_setprop_cell(fdt, node_off, "ibm,#dma-size-cells", 2);
    if (ret < 0) {
        return ret;
    }

    ret = fdt_setprop(fdt, node_off, propname, dma_prop, sizeof(dma_prop));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int spapr_tcet_dma_dt(void *fdt, int node_off, const char *propname,
                      sPAPRTCETable *tcet)
{
    if (!tcet) {
        return 0;
    }

    return spapr_dma_dt(fdt, node_off, propname,
                        tcet->liobn, 0, tcet->window_size);
}
