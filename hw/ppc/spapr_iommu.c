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
#include "trace.h"

#include "hw/ppc/spapr.h"

#include <libfdt.h>

enum sPAPRTCEAccess {
    SPAPR_TCE_FAULT = 0,
    SPAPR_TCE_RO = 1,
    SPAPR_TCE_WO = 2,
    SPAPR_TCE_RW = 3,
};

static QLIST_HEAD(spapr_tce_tables, sPAPRTCETable) spapr_tce_tables;

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
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = 0,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    if (tcet->bypass) {
        ret.perm = IOMMU_RW;
    } else if (addr < tcet->window_size) {
        /* Check if we are in bound */
        tce = tcet->table[addr >> SPAPR_TCE_PAGE_SHIFT];
        ret.iova = addr & ~SPAPR_TCE_PAGE_MASK;
        ret.translated_addr = tce & ~SPAPR_TCE_PAGE_MASK;
        ret.addr_mask = SPAPR_TCE_PAGE_MASK;
        ret.perm = tce;
    }
    trace_spapr_iommu_xlate(tcet->liobn, addr, ret.iova, ret.perm,
                            ret.addr_mask);

    return ret;
}

static int spapr_tce_table_pre_load(void *opaque)
{
    sPAPRTCETable *tcet = SPAPR_TCE_TABLE(opaque);

    tcet->nb_table = tcet->window_size >> SPAPR_TCE_PAGE_SHIFT;

    return 0;
}

static const VMStateDescription vmstate_spapr_tce_table = {
    .name = "spapr_iommu",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = spapr_tce_table_pre_load,
    .fields = (VMStateField[]) {
        /* Sanity check */
        VMSTATE_UINT32_EQUAL(liobn, sPAPRTCETable),
        VMSTATE_UINT32_EQUAL(window_size, sPAPRTCETable),

        /* IOMMU state */
        VMSTATE_BOOL(bypass, sPAPRTCETable),
        VMSTATE_VARRAY_UINT32(table, sPAPRTCETable, nb_table, 0, vmstate_info_uint64, uint64_t),

        VMSTATE_END_OF_LIST()
    },
};

static MemoryRegionIOMMUOps spapr_iommu_ops = {
    .translate = spapr_tce_translate_iommu,
};

static int spapr_tce_table_realize(DeviceState *dev)
{
    sPAPRTCETable *tcet = SPAPR_TCE_TABLE(dev);

    if (kvm_enabled()) {
        tcet->table = kvmppc_create_spapr_tce(tcet->liobn,
                                              tcet->window_size,
                                              &tcet->fd);
    }

    if (!tcet->table) {
        size_t table_size = (tcet->window_size >> SPAPR_TCE_PAGE_SHIFT)
            * sizeof(uint64_t);
        tcet->table = g_malloc0(table_size);
    }
    tcet->nb_table = tcet->window_size >> SPAPR_TCE_PAGE_SHIFT;

    trace_spapr_iommu_new_table(tcet->liobn, tcet, tcet->table, tcet->fd);

    memory_region_init_iommu(&tcet->iommu, OBJECT(dev), &spapr_iommu_ops,
                             "iommu-spapr", UINT64_MAX);

    QLIST_INSERT_HEAD(&spapr_tce_tables, tcet, list);

    vmstate_register(DEVICE(tcet), tcet->liobn, &vmstate_spapr_tce_table,
                     tcet);

    return 0;
}

sPAPRTCETable *spapr_tce_new_table(DeviceState *owner, uint32_t liobn, size_t window_size)
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

    tcet = SPAPR_TCE_TABLE(object_new(TYPE_SPAPR_TCE_TABLE));
    tcet->liobn = liobn;
    tcet->window_size = window_size;

    object_property_add_child(OBJECT(owner), "tce-table", OBJECT(tcet), NULL);

    qdev_init_nofail(DEVICE(tcet));

    return tcet;
}

static void spapr_tce_table_finalize(Object *obj)
{
    sPAPRTCETable *tcet = SPAPR_TCE_TABLE(obj);

    QLIST_REMOVE(tcet, list);

    if (!kvm_enabled() ||
        (kvmppc_remove_spapr_tce(tcet->table, tcet->fd,
                                 tcet->window_size) != 0)) {
        g_free(tcet->table);
    }
}

MemoryRegion *spapr_tce_get_iommu(sPAPRTCETable *tcet)
{
    return &tcet->iommu;
}

void spapr_tce_set_bypass(sPAPRTCETable *tcet, bool bypass)
{
    tcet->bypass = bypass;
}

static void spapr_tce_reset(DeviceState *dev)
{
    sPAPRTCETable *tcet = SPAPR_TCE_TABLE(dev);
    size_t table_size = (tcet->window_size >> SPAPR_TCE_PAGE_SHIFT)
        * sizeof(uint64_t);

    tcet->bypass = false;
    memset(tcet->table, 0, table_size);
}

static target_ulong put_tce_emu(sPAPRTCETable *tcet, target_ulong ioba,
                                target_ulong tce)
{
    IOMMUTLBEntry entry;

    if (ioba >= tcet->window_size) {
        hcall_dprintf("spapr_vio_put_tce on out-of-bounds IOBA 0x"
                      TARGET_FMT_lx "\n", ioba);
        return H_PARAMETER;
    }

    tcet->table[ioba >> SPAPR_TCE_PAGE_SHIFT] = tce;

    entry.target_as = &address_space_memory,
    entry.iova = ioba & ~SPAPR_TCE_PAGE_MASK;
    entry.translated_addr = tce & ~SPAPR_TCE_PAGE_MASK;
    entry.addr_mask = SPAPR_TCE_PAGE_MASK;
    entry.perm = tce;
    memory_region_notify_iommu(&tcet->iommu, entry);

    return H_SUCCESS;
}

static target_ulong h_put_tce_indirect(PowerPCCPU *cpu,
                                       sPAPREnvironment *spapr,
                                       target_ulong opcode, target_ulong *args)
{
    int i;
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong ioba1 = ioba;
    target_ulong tce_list = args[2];
    target_ulong npages = args[3];
    target_ulong ret = H_PARAMETER;
    sPAPRTCETable *tcet = spapr_tce_find_by_liobn(liobn);
    CPUState *cs = CPU(cpu);

    if (!tcet) {
        return H_PARAMETER;
    }

    if (npages > 512) {
        return H_PARAMETER;
    }

    ioba &= ~SPAPR_TCE_PAGE_MASK;
    tce_list &= ~SPAPR_TCE_PAGE_MASK;

    for (i = 0; i < npages; ++i, ioba += SPAPR_TCE_PAGE_SIZE) {
        target_ulong tce = ldq_phys(cs->as, tce_list +
                                    i * sizeof(target_ulong));
        ret = put_tce_emu(tcet, ioba, tce);
        if (ret) {
            break;
        }
    }

    /* Trace last successful or the first problematic entry */
    i = i ? (i - 1) : 0;
    trace_spapr_iommu_indirect(liobn, ioba1, tce_list, i,
                               ldq_phys(cs->as,
                               tce_list + i * sizeof(target_ulong)),
                               ret);

    return ret;
}

static target_ulong h_stuff_tce(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                              target_ulong opcode, target_ulong *args)
{
    int i;
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce_value = args[2];
    target_ulong npages = args[3];
    target_ulong ret = H_PARAMETER;
    sPAPRTCETable *tcet = spapr_tce_find_by_liobn(liobn);

    if (!tcet) {
        return H_PARAMETER;
    }

    if (npages > tcet->nb_table) {
        return H_PARAMETER;
    }

    ioba &= ~SPAPR_TCE_PAGE_MASK;

    for (i = 0; i < npages; ++i, ioba += SPAPR_TCE_PAGE_SIZE) {
        ret = put_tce_emu(tcet, ioba, tce_value);
        if (ret) {
            break;
        }
    }
    trace_spapr_iommu_stuff(liobn, ioba, tce_value, npages, ret);

    return ret;
}

static target_ulong h_put_tce(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce = args[2];
    target_ulong ret = H_PARAMETER;
    sPAPRTCETable *tcet = spapr_tce_find_by_liobn(liobn);

    ioba &= ~(SPAPR_TCE_PAGE_SIZE - 1);

    if (tcet) {
        ret = put_tce_emu(tcet, ioba, tce);
    }
    trace_spapr_iommu_put(liobn, ioba, tce, ret);

    return ret;
}

static target_ulong get_tce_emu(sPAPRTCETable *tcet, target_ulong ioba,
                                target_ulong *tce)
{
    if (ioba >= tcet->window_size) {
        hcall_dprintf("spapr_iommu_get_tce on out-of-bounds IOBA 0x"
                      TARGET_FMT_lx "\n", ioba);
        return H_PARAMETER;
    }

    *tce = tcet->table[ioba >> SPAPR_TCE_PAGE_SHIFT];

    return H_SUCCESS;
}

static target_ulong h_get_tce(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce = 0;
    target_ulong ret = H_PARAMETER;
    sPAPRTCETable *tcet = spapr_tce_find_by_liobn(liobn);

    ioba &= ~(SPAPR_TCE_PAGE_SIZE - 1);

    if (tcet) {
        ret = get_tce_emu(tcet, ioba, &tce);
        if (!ret) {
            args[0] = tce;
        }
    }
    trace_spapr_iommu_get(liobn, ioba, ret, tce);

    return ret;
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

static void spapr_tce_table_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->init = spapr_tce_table_realize;
    dc->reset = spapr_tce_reset;

    QLIST_INIT(&spapr_tce_tables);

    /* hcall-tce */
    spapr_register_hypercall(H_PUT_TCE, h_put_tce);
    spapr_register_hypercall(H_GET_TCE, h_get_tce);
    spapr_register_hypercall(H_PUT_TCE_INDIRECT, h_put_tce_indirect);
    spapr_register_hypercall(H_STUFF_TCE, h_stuff_tce);
}

static TypeInfo spapr_tce_table_info = {
    .name = TYPE_SPAPR_TCE_TABLE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(sPAPRTCETable),
    .class_init = spapr_tce_table_class_init,
    .instance_finalize = spapr_tce_table_finalize,
};

static void register_types(void)
{
    type_register_static(&spapr_tce_table_info);
}

type_init(register_types);
