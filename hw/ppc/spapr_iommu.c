/*
 * QEMU sPAPR IOMMU (TCE) code
 *
 * Copyright (c) 2010 David Gibson, IBM Corporation <dwg@au1.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"
#include "trace.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"

#include <libfdt.h>

enum SpaprTceAccess {
    SPAPR_TCE_FAULT = 0,
    SPAPR_TCE_RO = 1,
    SPAPR_TCE_WO = 2,
    SPAPR_TCE_RW = 3,
};

#define IOMMU_PAGE_SIZE(shift)      (1ULL << (shift))
#define IOMMU_PAGE_MASK(shift)      (~(IOMMU_PAGE_SIZE(shift) - 1))

static QLIST_HEAD(, SpaprTceTable) spapr_tce_tables;

SpaprTceTable *spapr_tce_find_by_liobn(target_ulong liobn)
{
    SpaprTceTable *tcet;

    if (liobn & 0xFFFFFFFF00000000ULL) {
        hcall_dprintf("Request for out-of-bounds LIOBN 0x" TARGET_FMT_lx "\n",
                      liobn);
        return NULL;
    }

    QLIST_FOREACH(tcet, &spapr_tce_tables, list) {
        if (tcet->liobn == (uint32_t)liobn) {
            return tcet;
        }
    }

    return NULL;
}

static IOMMUAccessFlags spapr_tce_iommu_access_flags(uint64_t tce)
{
    switch (tce & SPAPR_TCE_RW) {
    case SPAPR_TCE_FAULT:
        return IOMMU_NONE;
    case SPAPR_TCE_RO:
        return IOMMU_RO;
    case SPAPR_TCE_WO:
        return IOMMU_WO;
    default: /* SPAPR_TCE_RW */
        return IOMMU_RW;
    }
}

static uint64_t *spapr_tce_alloc_table(uint32_t liobn,
                                       uint32_t page_shift,
                                       uint64_t bus_offset,
                                       uint32_t nb_table,
                                       int *fd,
                                       bool need_vfio)
{
    uint64_t *table = NULL;

    if (kvm_enabled()) {
        table = kvmppc_create_spapr_tce(liobn, page_shift, bus_offset, nb_table,
                                        fd, need_vfio);
    }

    if (!table) {
        *fd = -1;
        table = g_new0(uint64_t, nb_table);
    }

    trace_spapr_iommu_new_table(liobn, table, *fd);

    return table;
}

static void spapr_tce_free_table(uint64_t *table, int fd, uint32_t nb_table)
{
    if (!kvm_enabled() ||
        (kvmppc_remove_spapr_tce(table, fd, nb_table) != 0)) {
        g_free(table);
    }
}

/* Called from RCU critical section */
static IOMMUTLBEntry spapr_tce_translate_iommu(IOMMUMemoryRegion *iommu,
                                               hwaddr addr,
                                               IOMMUAccessFlags flag,
                                               int iommu_idx)
{
    SpaprTceTable *tcet = container_of(iommu, SpaprTceTable, iommu);
    uint64_t tce;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = 0,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    if ((addr >> tcet->page_shift) < tcet->nb_table) {
        /* Check if we are in bound */
        hwaddr page_mask = IOMMU_PAGE_MASK(tcet->page_shift);

        tce = tcet->table[addr >> tcet->page_shift];
        ret.iova = addr & page_mask;
        ret.translated_addr = tce & page_mask;
        ret.addr_mask = ~page_mask;
        ret.perm = spapr_tce_iommu_access_flags(tce);
    }
    trace_spapr_iommu_xlate(tcet->liobn, addr, ret.translated_addr, ret.perm,
                            ret.addr_mask);

    return ret;
}

static void spapr_tce_replay(IOMMUMemoryRegion *iommu_mr, IOMMUNotifier *n)
{
    MemoryRegion *mr = MEMORY_REGION(iommu_mr);
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_GET_CLASS(iommu_mr);
    hwaddr addr, granularity;
    IOMMUTLBEntry iotlb;
    SpaprTceTable *tcet = container_of(iommu_mr, SpaprTceTable, iommu);

    if (tcet->skipping_replay) {
        return;
    }

    granularity = memory_region_iommu_get_min_page_size(iommu_mr);

    for (addr = 0; addr < memory_region_size(mr); addr += granularity) {
        iotlb = imrc->translate(iommu_mr, addr, IOMMU_NONE, n->iommu_idx);
        if (iotlb.perm != IOMMU_NONE) {
            n->notify(n, &iotlb);
        }

        /*
         * if (2^64 - MR size) < granularity, it's possible to get an
         * infinite loop here.  This should catch such a wraparound.
         */
        if ((addr + granularity) < addr) {
            break;
        }
    }
}

static int spapr_tce_table_pre_save(void *opaque)
{
    SpaprTceTable *tcet = SPAPR_TCE_TABLE(opaque);

    tcet->mig_table = tcet->table;
    tcet->mig_nb_table = tcet->nb_table;

    trace_spapr_iommu_pre_save(tcet->liobn, tcet->mig_nb_table,
                               tcet->bus_offset, tcet->page_shift);

    return 0;
}

static uint64_t spapr_tce_get_min_page_size(IOMMUMemoryRegion *iommu)
{
    SpaprTceTable *tcet = container_of(iommu, SpaprTceTable, iommu);

    return 1ULL << tcet->page_shift;
}

static int spapr_tce_get_attr(IOMMUMemoryRegion *iommu,
                              enum IOMMUMemoryRegionAttr attr, void *data)
{
    SpaprTceTable *tcet = container_of(iommu, SpaprTceTable, iommu);

    if (attr == IOMMU_ATTR_SPAPR_TCE_FD && kvmppc_has_cap_spapr_vfio()) {
        *(int *) data = tcet->fd;
        return 0;
    }

    return -EINVAL;
}

static int spapr_tce_notify_flag_changed(IOMMUMemoryRegion *iommu,
                                         IOMMUNotifierFlag old,
                                         IOMMUNotifierFlag new,
                                         Error **errp)
{
    struct SpaprTceTable *tbl = container_of(iommu, SpaprTceTable, iommu);

    if (new & IOMMU_NOTIFIER_DEVIOTLB_UNMAP) {
        error_setg(errp, "spart_tce does not support dev-iotlb yet");
        return -EINVAL;
    }

    if (old == IOMMU_NOTIFIER_NONE && new != IOMMU_NOTIFIER_NONE) {
        spapr_tce_set_need_vfio(tbl, true);
    } else if (old != IOMMU_NOTIFIER_NONE && new == IOMMU_NOTIFIER_NONE) {
        spapr_tce_set_need_vfio(tbl, false);
    }
    return 0;
}

static int spapr_tce_table_post_load(void *opaque, int version_id)
{
    SpaprTceTable *tcet = SPAPR_TCE_TABLE(opaque);
    uint32_t old_nb_table = tcet->nb_table;
    uint64_t old_bus_offset = tcet->bus_offset;
    uint32_t old_page_shift = tcet->page_shift;

    if (tcet->vdev) {
        spapr_vio_set_bypass(tcet->vdev, tcet->bypass);
    }

    if (tcet->mig_nb_table != tcet->nb_table) {
        spapr_tce_table_disable(tcet);
    }

    if (tcet->mig_nb_table) {
        if (!tcet->nb_table) {
            spapr_tce_table_enable(tcet, old_page_shift, old_bus_offset,
                                   tcet->mig_nb_table);
        }

        memcpy(tcet->table, tcet->mig_table,
               tcet->nb_table * sizeof(tcet->table[0]));

        free(tcet->mig_table);
        tcet->mig_table = NULL;
    }

    trace_spapr_iommu_post_load(tcet->liobn, old_nb_table, tcet->nb_table,
                                tcet->bus_offset, tcet->page_shift);

    return 0;
}

static bool spapr_tce_table_ex_needed(void *opaque)
{
    SpaprTceTable *tcet = opaque;

    return tcet->bus_offset || tcet->page_shift != 0xC;
}

static const VMStateDescription vmstate_spapr_tce_table_ex = {
    .name = "spapr_iommu_ex",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_tce_table_ex_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(bus_offset, SpaprTceTable),
        VMSTATE_UINT32(page_shift, SpaprTceTable),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr_tce_table = {
    .name = "spapr_iommu",
    .version_id = 2,
    .minimum_version_id = 2,
    .pre_save = spapr_tce_table_pre_save,
    .post_load = spapr_tce_table_post_load,
    .fields      = (VMStateField []) {
        /* Sanity check */
        VMSTATE_UINT32_EQUAL(liobn, SpaprTceTable, NULL),

        /* IOMMU state */
        VMSTATE_UINT32(mig_nb_table, SpaprTceTable),
        VMSTATE_BOOL(bypass, SpaprTceTable),
        VMSTATE_VARRAY_UINT32_ALLOC(mig_table, SpaprTceTable, mig_nb_table, 0,
                                    vmstate_info_uint64, uint64_t),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_spapr_tce_table_ex,
        NULL
    }
};

static void spapr_tce_table_realize(DeviceState *dev, Error **errp)
{
    SpaprTceTable *tcet = SPAPR_TCE_TABLE(dev);
    Object *tcetobj = OBJECT(tcet);
    gchar *tmp;

    tcet->fd = -1;
    tcet->need_vfio = false;
    tmp = g_strdup_printf("tce-root-%x", tcet->liobn);
    memory_region_init(&tcet->root, tcetobj, tmp, UINT64_MAX);
    g_free(tmp);

    tmp = g_strdup_printf("tce-iommu-%x", tcet->liobn);
    memory_region_init_iommu(&tcet->iommu, sizeof(tcet->iommu),
                             TYPE_SPAPR_IOMMU_MEMORY_REGION,
                             tcetobj, tmp, 0);
    g_free(tmp);

    QLIST_INSERT_HEAD(&spapr_tce_tables, tcet, list);

    vmstate_register(VMSTATE_IF(tcet), tcet->liobn, &vmstate_spapr_tce_table,
                     tcet);
}

void spapr_tce_set_need_vfio(SpaprTceTable *tcet, bool need_vfio)
{
    size_t table_size = tcet->nb_table * sizeof(uint64_t);
    uint64_t *oldtable;
    int newfd = -1;

    g_assert(need_vfio != tcet->need_vfio);

    tcet->need_vfio = need_vfio;

    if (!need_vfio || (tcet->fd != -1 && kvmppc_has_cap_spapr_vfio())) {
        return;
    }

    oldtable = tcet->table;

    tcet->table = spapr_tce_alloc_table(tcet->liobn,
                                        tcet->page_shift,
                                        tcet->bus_offset,
                                        tcet->nb_table,
                                        &newfd,
                                        need_vfio);
    memcpy(tcet->table, oldtable, table_size);

    spapr_tce_free_table(oldtable, tcet->fd, tcet->nb_table);

    tcet->fd = newfd;
}

SpaprTceTable *spapr_tce_new_table(DeviceState *owner, uint32_t liobn)
{
    SpaprTceTable *tcet;
    gchar *tmp;

    if (spapr_tce_find_by_liobn(liobn)) {
        error_report("Attempted to create TCE table with duplicate"
                " LIOBN 0x%x", liobn);
        return NULL;
    }

    tcet = SPAPR_TCE_TABLE(object_new(TYPE_SPAPR_TCE_TABLE));
    tcet->liobn = liobn;

    tmp = g_strdup_printf("tce-table-%x", liobn);
    object_property_add_child(OBJECT(owner), tmp, OBJECT(tcet));
    g_free(tmp);
    object_unref(OBJECT(tcet));

    qdev_realize(DEVICE(tcet), NULL, NULL);

    return tcet;
}

void spapr_tce_table_enable(SpaprTceTable *tcet,
                            uint32_t page_shift, uint64_t bus_offset,
                            uint32_t nb_table)
{
    if (tcet->nb_table) {
        warn_report("trying to enable already enabled TCE table");
        return;
    }

    tcet->bus_offset = bus_offset;
    tcet->page_shift = page_shift;
    tcet->nb_table = nb_table;
    tcet->table = spapr_tce_alloc_table(tcet->liobn,
                                        tcet->page_shift,
                                        tcet->bus_offset,
                                        tcet->nb_table,
                                        &tcet->fd,
                                        tcet->need_vfio);

    memory_region_set_size(MEMORY_REGION(&tcet->iommu),
                           (uint64_t)tcet->nb_table << tcet->page_shift);
    memory_region_add_subregion(&tcet->root, tcet->bus_offset,
                                MEMORY_REGION(&tcet->iommu));
}

void spapr_tce_table_disable(SpaprTceTable *tcet)
{
    if (!tcet->nb_table) {
        return;
    }

    memory_region_del_subregion(&tcet->root, MEMORY_REGION(&tcet->iommu));
    memory_region_set_size(MEMORY_REGION(&tcet->iommu), 0);

    spapr_tce_free_table(tcet->table, tcet->fd, tcet->nb_table);
    tcet->fd = -1;
    tcet->table = NULL;
    tcet->bus_offset = 0;
    tcet->page_shift = 0;
    tcet->nb_table = 0;
}

static void spapr_tce_table_unrealize(DeviceState *dev)
{
    SpaprTceTable *tcet = SPAPR_TCE_TABLE(dev);

    vmstate_unregister(VMSTATE_IF(tcet), &vmstate_spapr_tce_table, tcet);

    QLIST_REMOVE(tcet, list);

    spapr_tce_table_disable(tcet);
}

MemoryRegion *spapr_tce_get_iommu(SpaprTceTable *tcet)
{
    return &tcet->root;
}

static void spapr_tce_reset(DeviceState *dev)
{
    SpaprTceTable *tcet = SPAPR_TCE_TABLE(dev);
    size_t table_size = tcet->nb_table * sizeof(uint64_t);

    if (tcet->nb_table) {
        memset(tcet->table, 0, table_size);
    }
}

static target_ulong put_tce_emu(SpaprTceTable *tcet, target_ulong ioba,
                                target_ulong tce)
{
    IOMMUTLBEvent event;
    hwaddr page_mask = IOMMU_PAGE_MASK(tcet->page_shift);
    unsigned long index = (ioba - tcet->bus_offset) >> tcet->page_shift;

    if (index >= tcet->nb_table) {
        hcall_dprintf("spapr_vio_put_tce on out-of-bounds IOBA 0x"
                      TARGET_FMT_lx "\n", ioba);
        return H_PARAMETER;
    }

    tcet->table[index] = tce;

    event.entry.target_as = &address_space_memory,
    event.entry.iova = (ioba - tcet->bus_offset) & page_mask;
    event.entry.translated_addr = tce & page_mask;
    event.entry.addr_mask = ~page_mask;
    event.entry.perm = spapr_tce_iommu_access_flags(tce);
    event.type = event.entry.perm ? IOMMU_NOTIFIER_MAP : IOMMU_NOTIFIER_UNMAP;
    memory_region_notify_iommu(&tcet->iommu, 0, event);

    return H_SUCCESS;
}

static target_ulong h_put_tce_indirect(PowerPCCPU *cpu,
                                       SpaprMachineState *spapr,
                                       target_ulong opcode, target_ulong *args)
{
    int i;
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong ioba1 = ioba;
    target_ulong tce_list = args[2];
    target_ulong npages = args[3];
    target_ulong ret = H_PARAMETER, tce = 0;
    SpaprTceTable *tcet = spapr_tce_find_by_liobn(liobn);
    CPUState *cs = CPU(cpu);
    hwaddr page_mask, page_size;

    if (!tcet) {
        return H_PARAMETER;
    }

    if ((npages > 512) || (tce_list & SPAPR_TCE_PAGE_MASK)) {
        return H_PARAMETER;
    }

    page_mask = IOMMU_PAGE_MASK(tcet->page_shift);
    page_size = IOMMU_PAGE_SIZE(tcet->page_shift);
    ioba &= page_mask;

    for (i = 0; i < npages; ++i, ioba += page_size) {
        tce = ldq_be_phys(cs->as, tce_list + i * sizeof(target_ulong));

        ret = put_tce_emu(tcet, ioba, tce);
        if (ret) {
            break;
        }
    }

    /* Trace last successful or the first problematic entry */
    i = i ? (i - 1) : 0;
    if (SPAPR_IS_PCI_LIOBN(liobn)) {
        trace_spapr_iommu_pci_indirect(liobn, ioba1, tce_list, i, tce, ret);
    } else {
        trace_spapr_iommu_indirect(liobn, ioba1, tce_list, i, tce, ret);
    }
    return ret;
}

static target_ulong h_stuff_tce(PowerPCCPU *cpu, SpaprMachineState *spapr,
                              target_ulong opcode, target_ulong *args)
{
    int i;
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce_value = args[2];
    target_ulong npages = args[3];
    target_ulong ret = H_PARAMETER;
    SpaprTceTable *tcet = spapr_tce_find_by_liobn(liobn);
    hwaddr page_mask, page_size;

    if (!tcet) {
        return H_PARAMETER;
    }

    if (npages > tcet->nb_table) {
        return H_PARAMETER;
    }

    page_mask = IOMMU_PAGE_MASK(tcet->page_shift);
    page_size = IOMMU_PAGE_SIZE(tcet->page_shift);
    ioba &= page_mask;

    for (i = 0; i < npages; ++i, ioba += page_size) {
        ret = put_tce_emu(tcet, ioba, tce_value);
        if (ret) {
            break;
        }
    }
    if (SPAPR_IS_PCI_LIOBN(liobn)) {
        trace_spapr_iommu_pci_stuff(liobn, ioba, tce_value, npages, ret);
    } else {
        trace_spapr_iommu_stuff(liobn, ioba, tce_value, npages, ret);
    }

    return ret;
}

static target_ulong h_put_tce(PowerPCCPU *cpu, SpaprMachineState *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce = args[2];
    target_ulong ret = H_PARAMETER;
    SpaprTceTable *tcet = spapr_tce_find_by_liobn(liobn);

    if (tcet) {
        hwaddr page_mask = IOMMU_PAGE_MASK(tcet->page_shift);

        ioba &= page_mask;

        ret = put_tce_emu(tcet, ioba, tce);
    }
    if (SPAPR_IS_PCI_LIOBN(liobn)) {
        trace_spapr_iommu_pci_put(liobn, ioba, tce, ret);
    } else {
        trace_spapr_iommu_put(liobn, ioba, tce, ret);
    }

    return ret;
}

static target_ulong get_tce_emu(SpaprTceTable *tcet, target_ulong ioba,
                                target_ulong *tce)
{
    unsigned long index = (ioba - tcet->bus_offset) >> tcet->page_shift;

    if (index >= tcet->nb_table) {
        hcall_dprintf("spapr_iommu_get_tce on out-of-bounds IOBA 0x"
                      TARGET_FMT_lx "\n", ioba);
        return H_PARAMETER;
    }

    *tce = tcet->table[index];

    return H_SUCCESS;
}

static target_ulong h_get_tce(PowerPCCPU *cpu, SpaprMachineState *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce = 0;
    target_ulong ret = H_PARAMETER;
    SpaprTceTable *tcet = spapr_tce_find_by_liobn(liobn);

    if (tcet) {
        hwaddr page_mask = IOMMU_PAGE_MASK(tcet->page_shift);

        ioba &= page_mask;

        ret = get_tce_emu(tcet, ioba, &tce);
        if (!ret) {
            args[0] = tce;
        }
    }
    if (SPAPR_IS_PCI_LIOBN(liobn)) {
        trace_spapr_iommu_pci_get(liobn, ioba, ret, tce);
    } else {
        trace_spapr_iommu_get(liobn, ioba, ret, tce);
    }

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
                      SpaprTceTable *tcet)
{
    if (!tcet) {
        return 0;
    }

    return spapr_dma_dt(fdt, node_off, propname,
                        tcet->liobn, 0, tcet->nb_table << tcet->page_shift);
}

static void spapr_tce_table_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = spapr_tce_table_realize;
    dc->reset = spapr_tce_reset;
    dc->unrealize = spapr_tce_table_unrealize;
    /* Reason: This is just an internal device for handling the hypercalls */
    dc->user_creatable = false;

    QLIST_INIT(&spapr_tce_tables);

    /* hcall-tce */
    spapr_register_hypercall(H_PUT_TCE, h_put_tce);
    spapr_register_hypercall(H_GET_TCE, h_get_tce);
    spapr_register_hypercall(H_PUT_TCE_INDIRECT, h_put_tce_indirect);
    spapr_register_hypercall(H_STUFF_TCE, h_stuff_tce);
}

static const TypeInfo spapr_tce_table_info = {
    .name = TYPE_SPAPR_TCE_TABLE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SpaprTceTable),
    .class_init = spapr_tce_table_class_init,
};

static void spapr_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = spapr_tce_translate_iommu;
    imrc->replay = spapr_tce_replay;
    imrc->get_min_page_size = spapr_tce_get_min_page_size;
    imrc->notify_flag_changed = spapr_tce_notify_flag_changed;
    imrc->get_attr = spapr_tce_get_attr;
}

static const TypeInfo spapr_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_SPAPR_IOMMU_MEMORY_REGION,
    .class_init = spapr_iommu_memory_region_class_init,
};

static void register_types(void)
{
    type_register_static(&spapr_tce_table_info);
    type_register_static(&spapr_iommu_memory_region_info);
}

type_init(register_types);
