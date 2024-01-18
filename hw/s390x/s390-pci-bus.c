/*
 * s390 PCI BUS
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Frank Blaschka <frank.blaschka@de.ibm.com>
 *            Hong Bo Li <lihbbj@cn.ibm.com>
 *            Yi Min Zhao <zyimin@cn.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/s390-pci-inst.h"
#include "hw/s390x/s390-pci-kvm.h"
#include "hw/s390x/s390-pci-vfio.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/msi.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"

#include "trace.h"

S390pciState *s390_get_phb(void)
{
    static S390pciState *phb;

    if (!phb) {
        phb = S390_PCI_HOST_BRIDGE(
            object_resolve_path(TYPE_S390_PCI_HOST_BRIDGE, NULL));
        assert(phb != NULL);
    }

    return phb;
}

int pci_chsc_sei_nt2_get_event(void *res)
{
    ChscSeiNt2Res *nt2_res = (ChscSeiNt2Res *)res;
    PciCcdfAvail *accdf;
    PciCcdfErr *eccdf;
    int rc = 1;
    SeiContainer *sei_cont;
    S390pciState *s = s390_get_phb();

    sei_cont = QTAILQ_FIRST(&s->pending_sei);
    if (sei_cont) {
        QTAILQ_REMOVE(&s->pending_sei, sei_cont, link);
        nt2_res->nt = 2;
        nt2_res->cc = sei_cont->cc;
        nt2_res->length = cpu_to_be16(sizeof(ChscSeiNt2Res));
        switch (sei_cont->cc) {
        case 1: /* error event */
            eccdf = (PciCcdfErr *)nt2_res->ccdf;
            eccdf->fid = cpu_to_be32(sei_cont->fid);
            eccdf->fh = cpu_to_be32(sei_cont->fh);
            eccdf->e = cpu_to_be32(sei_cont->e);
            eccdf->faddr = cpu_to_be64(sei_cont->faddr);
            eccdf->pec = cpu_to_be16(sei_cont->pec);
            break;
        case 2: /* availability event */
            accdf = (PciCcdfAvail *)nt2_res->ccdf;
            accdf->fid = cpu_to_be32(sei_cont->fid);
            accdf->fh = cpu_to_be32(sei_cont->fh);
            accdf->pec = cpu_to_be16(sei_cont->pec);
            break;
        default:
            abort();
        }
        g_free(sei_cont);
        rc = 0;
    }

    return rc;
}

int pci_chsc_sei_nt2_have_event(void)
{
    S390pciState *s = s390_get_phb();

    return !QTAILQ_EMPTY(&s->pending_sei);
}

S390PCIBusDevice *s390_pci_find_next_avail_dev(S390pciState *s,
                                               S390PCIBusDevice *pbdev)
{
    S390PCIBusDevice *ret = pbdev ? QTAILQ_NEXT(pbdev, link) :
        QTAILQ_FIRST(&s->zpci_devs);

    while (ret && ret->state == ZPCI_FS_RESERVED) {
        ret = QTAILQ_NEXT(ret, link);
    }

    return ret;
}

S390PCIBusDevice *s390_pci_find_dev_by_fid(S390pciState *s, uint32_t fid)
{
    S390PCIBusDevice *pbdev;

    QTAILQ_FOREACH(pbdev, &s->zpci_devs, link) {
        if (pbdev->fid == fid) {
            return pbdev;
        }
    }

    return NULL;
}

void s390_pci_sclp_configure(SCCB *sccb)
{
    IoaCfgSccb *psccb = (IoaCfgSccb *)sccb;
    S390PCIBusDevice *pbdev = s390_pci_find_dev_by_fid(s390_get_phb(),
                                                       be32_to_cpu(psccb->aid));
    uint16_t rc;

    if (!pbdev) {
        trace_s390_pci_sclp_nodev("configure", be32_to_cpu(psccb->aid));
        rc = SCLP_RC_ADAPTER_ID_NOT_RECOGNIZED;
        goto out;
    }

    switch (pbdev->state) {
    case ZPCI_FS_RESERVED:
        rc = SCLP_RC_ADAPTER_IN_RESERVED_STATE;
        break;
    case ZPCI_FS_STANDBY:
        pbdev->state = ZPCI_FS_DISABLED;
        rc = SCLP_RC_NORMAL_COMPLETION;
        break;
    default:
        rc = SCLP_RC_NO_ACTION_REQUIRED;
    }
out:
    psccb->header.response_code = cpu_to_be16(rc);
}

static void s390_pci_shutdown_notifier(Notifier *n, void *opaque)
{
    S390PCIBusDevice *pbdev = container_of(n, S390PCIBusDevice,
                                           shutdown_notifier);

    pci_device_reset(pbdev->pdev);
}

static void s390_pci_perform_unplug(S390PCIBusDevice *pbdev)
{
    HotplugHandler *hotplug_ctrl;

    if (pbdev->pft == ZPCI_PFT_ISM) {
        notifier_remove(&pbdev->shutdown_notifier);
    }

    /* Unplug the PCI device */
    if (pbdev->pdev) {
        DeviceState *pdev = DEVICE(pbdev->pdev);

        hotplug_ctrl = qdev_get_hotplug_handler(pdev);
        hotplug_handler_unplug(hotplug_ctrl, pdev, &error_abort);
        object_unparent(OBJECT(pdev));
    }

    /* Unplug the zPCI device */
    hotplug_ctrl = qdev_get_hotplug_handler(DEVICE(pbdev));
    hotplug_handler_unplug(hotplug_ctrl, DEVICE(pbdev), &error_abort);
    object_unparent(OBJECT(pbdev));
}

void s390_pci_sclp_deconfigure(SCCB *sccb)
{
    IoaCfgSccb *psccb = (IoaCfgSccb *)sccb;
    S390PCIBusDevice *pbdev = s390_pci_find_dev_by_fid(s390_get_phb(),
                                                       be32_to_cpu(psccb->aid));
    uint16_t rc;

    if (!pbdev) {
        trace_s390_pci_sclp_nodev("deconfigure", be32_to_cpu(psccb->aid));
        rc = SCLP_RC_ADAPTER_ID_NOT_RECOGNIZED;
        goto out;
    }

    switch (pbdev->state) {
    case ZPCI_FS_RESERVED:
        rc = SCLP_RC_ADAPTER_IN_RESERVED_STATE;
        break;
    case ZPCI_FS_STANDBY:
        rc = SCLP_RC_NO_ACTION_REQUIRED;
        break;
    default:
        if (pbdev->interp && (pbdev->fh & FH_MASK_ENABLE)) {
            /* Interpreted devices were using interrupt forwarding */
            s390_pci_kvm_aif_disable(pbdev);
        } else if (pbdev->summary_ind) {
            pci_dereg_irqs(pbdev);
        }
        if (pbdev->iommu->enabled) {
            pci_dereg_ioat(pbdev->iommu);
        }
        pbdev->state = ZPCI_FS_STANDBY;
        rc = SCLP_RC_NORMAL_COMPLETION;

        if (pbdev->unplug_requested) {
            s390_pci_perform_unplug(pbdev);
        }
    }
out:
    psccb->header.response_code = cpu_to_be16(rc);
}

static S390PCIBusDevice *s390_pci_find_dev_by_uid(S390pciState *s, uint16_t uid)
{
    S390PCIBusDevice *pbdev;

    QTAILQ_FOREACH(pbdev, &s->zpci_devs, link) {
        if (pbdev->uid == uid) {
            return pbdev;
        }
    }

    return NULL;
}

S390PCIBusDevice *s390_pci_find_dev_by_target(S390pciState *s,
                                              const char *target)
{
    S390PCIBusDevice *pbdev;

    if (!target) {
        return NULL;
    }

    QTAILQ_FOREACH(pbdev, &s->zpci_devs, link) {
        if (!strcmp(pbdev->target, target)) {
            return pbdev;
        }
    }

    return NULL;
}

static S390PCIBusDevice *s390_pci_find_dev_by_pci(S390pciState *s,
                                                  PCIDevice *pci_dev)
{
    S390PCIBusDevice *pbdev;

    if (!pci_dev) {
        return NULL;
    }

    QTAILQ_FOREACH(pbdev, &s->zpci_devs, link) {
        if (pbdev->pdev == pci_dev) {
            return pbdev;
        }
    }

    return NULL;
}

S390PCIBusDevice *s390_pci_find_dev_by_idx(S390pciState *s, uint32_t idx)
{
    return g_hash_table_lookup(s->zpci_table, &idx);
}

S390PCIBusDevice *s390_pci_find_dev_by_fh(S390pciState *s, uint32_t fh)
{
    uint32_t idx = FH_MASK_INDEX & fh;
    S390PCIBusDevice *pbdev = s390_pci_find_dev_by_idx(s, idx);

    if (pbdev && pbdev->fh == fh) {
        return pbdev;
    }

    return NULL;
}

static void s390_pci_generate_event(uint8_t cc, uint16_t pec, uint32_t fh,
                                    uint32_t fid, uint64_t faddr, uint32_t e)
{
    SeiContainer *sei_cont;
    S390pciState *s = s390_get_phb();

    sei_cont = g_new0(SeiContainer, 1);
    sei_cont->fh = fh;
    sei_cont->fid = fid;
    sei_cont->cc = cc;
    sei_cont->pec = pec;
    sei_cont->faddr = faddr;
    sei_cont->e = e;

    QTAILQ_INSERT_TAIL(&s->pending_sei, sei_cont, link);
    css_generate_css_crws(0);
}

static void s390_pci_generate_plug_event(uint16_t pec, uint32_t fh,
                                         uint32_t fid)
{
    s390_pci_generate_event(2, pec, fh, fid, 0, 0);
}

void s390_pci_generate_error_event(uint16_t pec, uint32_t fh, uint32_t fid,
                                   uint64_t faddr, uint32_t e)
{
    s390_pci_generate_event(1, pec, fh, fid, faddr, e);
}

static void s390_pci_set_irq(void *opaque, int irq, int level)
{
    /* nothing to do */
}

static int s390_pci_map_irq(PCIDevice *pci_dev, int irq_num)
{
    /* nothing to do */
    return 0;
}

static uint64_t s390_pci_get_table_origin(uint64_t iota)
{
    return iota & ~ZPCI_IOTA_RTTO_FLAG;
}

static unsigned int calc_rtx(dma_addr_t ptr)
{
    return ((unsigned long) ptr >> ZPCI_RT_SHIFT) & ZPCI_INDEX_MASK;
}

static unsigned int calc_sx(dma_addr_t ptr)
{
    return ((unsigned long) ptr >> ZPCI_ST_SHIFT) & ZPCI_INDEX_MASK;
}

static unsigned int calc_px(dma_addr_t ptr)
{
    return ((unsigned long) ptr >> TARGET_PAGE_BITS) & ZPCI_PT_MASK;
}

static uint64_t get_rt_sto(uint64_t entry)
{
    return ((entry & ZPCI_TABLE_TYPE_MASK) == ZPCI_TABLE_TYPE_RTX)
                ? (entry & ZPCI_RTE_ADDR_MASK)
                : 0;
}

static uint64_t get_st_pto(uint64_t entry)
{
    return ((entry & ZPCI_TABLE_TYPE_MASK) == ZPCI_TABLE_TYPE_SX)
            ? (entry & ZPCI_STE_ADDR_MASK)
            : 0;
}

static bool rt_entry_isvalid(uint64_t entry)
{
    return (entry & ZPCI_TABLE_VALID_MASK) == ZPCI_TABLE_VALID;
}

static bool pt_entry_isvalid(uint64_t entry)
{
    return (entry & ZPCI_PTE_VALID_MASK) == ZPCI_PTE_VALID;
}

static bool entry_isprotected(uint64_t entry)
{
    return (entry & ZPCI_TABLE_PROT_MASK) == ZPCI_TABLE_PROTECTED;
}

/* ett is expected table type, -1 page table, 0 segment table, 1 region table */
static uint64_t get_table_index(uint64_t iova, int8_t ett)
{
    switch (ett) {
    case ZPCI_ETT_PT:
        return calc_px(iova);
    case ZPCI_ETT_ST:
        return calc_sx(iova);
    case ZPCI_ETT_RT:
        return calc_rtx(iova);
    }

    return -1;
}

static bool entry_isvalid(uint64_t entry, int8_t ett)
{
    switch (ett) {
    case ZPCI_ETT_PT:
        return pt_entry_isvalid(entry);
    case ZPCI_ETT_ST:
    case ZPCI_ETT_RT:
        return rt_entry_isvalid(entry);
    }

    return false;
}

/* Return true if address translation is done */
static bool translate_iscomplete(uint64_t entry, int8_t ett)
{
    switch (ett) {
    case 0:
        return (entry & ZPCI_TABLE_FC) ? true : false;
    case 1:
        return false;
    }

    return true;
}

static uint64_t get_frame_size(int8_t ett)
{
    switch (ett) {
    case ZPCI_ETT_PT:
        return 1ULL << 12;
    case ZPCI_ETT_ST:
        return 1ULL << 20;
    case ZPCI_ETT_RT:
        return 1ULL << 31;
    }

    return 0;
}

static uint64_t get_next_table_origin(uint64_t entry, int8_t ett)
{
    switch (ett) {
    case ZPCI_ETT_PT:
        return entry & ZPCI_PTE_ADDR_MASK;
    case ZPCI_ETT_ST:
        return get_st_pto(entry);
    case ZPCI_ETT_RT:
        return get_rt_sto(entry);
    }

    return 0;
}

/**
 * table_translate: do translation within one table and return the following
 *                  table origin
 *
 * @entry: the entry being translated, the result is stored in this.
 * @to: the address of table origin.
 * @ett: expected table type, 1 region table, 0 segment table and -1 page table.
 * @error: error code
 */
static uint64_t table_translate(S390IOTLBEntry *entry, uint64_t to, int8_t ett,
                                uint16_t *error)
{
    uint64_t tx, te, nto = 0;
    uint16_t err = 0;

    tx = get_table_index(entry->iova, ett);
    te = address_space_ldq(&address_space_memory, to + tx * sizeof(uint64_t),
                           MEMTXATTRS_UNSPECIFIED, NULL);

    if (!te) {
        err = ERR_EVENT_INVALTE;
        goto out;
    }

    if (!entry_isvalid(te, ett)) {
        entry->perm &= IOMMU_NONE;
        goto out;
    }

    if (ett == ZPCI_ETT_RT && ((te & ZPCI_TABLE_LEN_RTX) != ZPCI_TABLE_LEN_RTX
                               || te & ZPCI_TABLE_OFFSET_MASK)) {
        err = ERR_EVENT_INVALTL;
        goto out;
    }

    nto = get_next_table_origin(te, ett);
    if (!nto) {
        err = ERR_EVENT_TT;
        goto out;
    }

    if (entry_isprotected(te)) {
        entry->perm &= IOMMU_RO;
    } else {
        entry->perm &= IOMMU_RW;
    }

    if (translate_iscomplete(te, ett)) {
        switch (ett) {
        case ZPCI_ETT_PT:
            entry->translated_addr = te & ZPCI_PTE_ADDR_MASK;
            break;
        case ZPCI_ETT_ST:
            entry->translated_addr = (te & ZPCI_SFAA_MASK) |
                (entry->iova & ~ZPCI_SFAA_MASK);
            break;
        }
        nto = 0;
    }
out:
    if (err) {
        entry->perm = IOMMU_NONE;
        *error = err;
    }
    entry->len = get_frame_size(ett);
    return nto;
}

uint16_t s390_guest_io_table_walk(uint64_t g_iota, hwaddr addr,
                                  S390IOTLBEntry *entry)
{
    uint64_t to = s390_pci_get_table_origin(g_iota);
    int8_t ett = 1;
    uint16_t error = 0;

    entry->iova = addr & TARGET_PAGE_MASK;
    entry->translated_addr = 0;
    entry->perm = IOMMU_RW;

    if (entry_isprotected(g_iota)) {
        entry->perm &= IOMMU_RO;
    }

    while (to) {
        to = table_translate(entry, to, ett--, &error);
    }

    return error;
}

static IOMMUTLBEntry s390_translate_iommu(IOMMUMemoryRegion *mr, hwaddr addr,
                                          IOMMUAccessFlags flag, int iommu_idx)
{
    S390PCIIOMMU *iommu = container_of(mr, S390PCIIOMMU, iommu_mr);
    S390IOTLBEntry *entry;
    uint64_t iova = addr & TARGET_PAGE_MASK;
    uint16_t error = 0;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = 0,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    switch (iommu->pbdev->state) {
    case ZPCI_FS_ENABLED:
    case ZPCI_FS_BLOCKED:
        if (!iommu->enabled) {
            return ret;
        }
        break;
    default:
        return ret;
    }

    trace_s390_pci_iommu_xlate(addr);

    if (addr < iommu->pba || addr > iommu->pal) {
        error = ERR_EVENT_OORANGE;
        goto err;
    }

    entry = g_hash_table_lookup(iommu->iotlb, &iova);
    if (entry) {
        ret.iova = entry->iova;
        ret.translated_addr = entry->translated_addr;
        ret.addr_mask = entry->len - 1;
        ret.perm = entry->perm;
    } else {
        ret.iova = iova;
        ret.addr_mask = ~TARGET_PAGE_MASK;
        ret.perm = IOMMU_NONE;
    }

    if (flag != IOMMU_NONE && !(flag & ret.perm)) {
        error = ERR_EVENT_TPROTE;
    }
err:
    if (error) {
        iommu->pbdev->state = ZPCI_FS_ERROR;
        s390_pci_generate_error_event(error, iommu->pbdev->fh,
                                      iommu->pbdev->fid, addr, 0);
    }
    return ret;
}

static void s390_pci_iommu_replay(IOMMUMemoryRegion *iommu,
                                  IOMMUNotifier *notifier)
{
    /* It's impossible to plug a pci device on s390x that already has iommu
     * mappings which need to be replayed, that is due to the "one iommu per
     * zpci device" construct. But when we support migration of vfio-pci
     * devices in future, we need to revisit this.
     */
    return;
}

static S390PCIIOMMU *s390_pci_get_iommu(S390pciState *s, PCIBus *bus,
                                        int devfn)
{
    uint64_t key = (uintptr_t)bus;
    S390PCIIOMMUTable *table = g_hash_table_lookup(s->iommu_table, &key);
    S390PCIIOMMU *iommu;

    if (!table) {
        table = g_new0(S390PCIIOMMUTable, 1);
        table->key = key;
        g_hash_table_insert(s->iommu_table, &table->key, table);
    }

    iommu = table->iommu[PCI_SLOT(devfn)];
    if (!iommu) {
        iommu = S390_PCI_IOMMU(object_new(TYPE_S390_PCI_IOMMU));

        char *mr_name = g_strdup_printf("iommu-root-%02x:%02x.%01x",
                                        pci_bus_num(bus),
                                        PCI_SLOT(devfn),
                                        PCI_FUNC(devfn));
        char *as_name = g_strdup_printf("iommu-pci-%02x:%02x.%01x",
                                        pci_bus_num(bus),
                                        PCI_SLOT(devfn),
                                        PCI_FUNC(devfn));
        memory_region_init(&iommu->mr, OBJECT(iommu), mr_name, UINT64_MAX);
        address_space_init(&iommu->as, &iommu->mr, as_name);
        iommu->iotlb = g_hash_table_new_full(g_int64_hash, g_int64_equal,
                                             NULL, g_free);
        table->iommu[PCI_SLOT(devfn)] = iommu;

        g_free(mr_name);
        g_free(as_name);
    }

    return iommu;
}

static AddressSpace *s390_pci_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    S390pciState *s = opaque;
    S390PCIIOMMU *iommu = s390_pci_get_iommu(s, bus, devfn);

    return &iommu->as;
}

static const PCIIOMMUOps s390_iommu_ops = {
    .get_address_space = s390_pci_dma_iommu,
};

static uint8_t set_ind_atomic(uint64_t ind_loc, uint8_t to_be_set)
{
    uint8_t expected, actual;
    hwaddr len = 1;
    /* avoid  multiple fetches */
    uint8_t volatile *ind_addr;

    ind_addr = cpu_physical_memory_map(ind_loc, &len, true);
    if (!ind_addr) {
        s390_pci_generate_error_event(ERR_EVENT_AIRERR, 0, 0, 0, 0);
        return -1;
    }
    actual = *ind_addr;
    do {
        expected = actual;
        actual = qatomic_cmpxchg(ind_addr, expected, expected | to_be_set);
    } while (actual != expected);
    cpu_physical_memory_unmap((void *)ind_addr, len, 1, len);

    return actual;
}

static void s390_msi_ctrl_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned int size)
{
    S390PCIBusDevice *pbdev = opaque;
    uint32_t vec = data & ZPCI_MSI_VEC_MASK;
    uint64_t ind_bit;
    uint32_t sum_bit;

    assert(pbdev);

    trace_s390_pci_msi_ctrl_write(data, pbdev->idx, vec);

    if (pbdev->state != ZPCI_FS_ENABLED) {
        return;
    }

    ind_bit = pbdev->routes.adapter.ind_offset;
    sum_bit = pbdev->routes.adapter.summary_offset;

    set_ind_atomic(pbdev->routes.adapter.ind_addr + (ind_bit + vec) / 8,
                   0x80 >> ((ind_bit + vec) % 8));
    if (!set_ind_atomic(pbdev->routes.adapter.summary_addr + sum_bit / 8,
                                       0x80 >> (sum_bit % 8))) {
        css_adapter_interrupt(CSS_IO_ADAPTER_PCI, pbdev->isc);
    }
}

static uint64_t s390_msi_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffff;
}

static const MemoryRegionOps s390_msi_ctrl_ops = {
    .write = s390_msi_ctrl_write,
    .read = s390_msi_ctrl_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void s390_pci_iommu_enable(S390PCIIOMMU *iommu)
{
    /*
     * The iommu region is initialized against a 0-mapped address space,
     * so the smallest IOMMU region we can define runs from 0 to the end
     * of the PCI address space.
     */
    char *name = g_strdup_printf("iommu-s390-%04x", iommu->pbdev->uid);
    memory_region_init_iommu(&iommu->iommu_mr, sizeof(iommu->iommu_mr),
                             TYPE_S390_IOMMU_MEMORY_REGION, OBJECT(&iommu->mr),
                             name, iommu->pal + 1);
    iommu->enabled = true;
    memory_region_add_subregion(&iommu->mr, 0, MEMORY_REGION(&iommu->iommu_mr));
    g_free(name);
}

void s390_pci_iommu_disable(S390PCIIOMMU *iommu)
{
    iommu->enabled = false;
    g_hash_table_remove_all(iommu->iotlb);
    memory_region_del_subregion(&iommu->mr, MEMORY_REGION(&iommu->iommu_mr));
    object_unparent(OBJECT(&iommu->iommu_mr));
}

static void s390_pci_iommu_free(S390pciState *s, PCIBus *bus, int32_t devfn)
{
    uint64_t key = (uintptr_t)bus;
    S390PCIIOMMUTable *table = g_hash_table_lookup(s->iommu_table, &key);
    S390PCIIOMMU *iommu = table ? table->iommu[PCI_SLOT(devfn)] : NULL;

    if (!table || !iommu) {
        return;
    }

    table->iommu[PCI_SLOT(devfn)] = NULL;
    g_hash_table_destroy(iommu->iotlb);
    /*
     * An attached PCI device may have memory listeners, eg. VFIO PCI.
     * The associated subregion will already have been unmapped in
     * s390_pci_iommu_disable in response to the guest deconfigure request.
     * Remove the listeners now before destroying the address space.
     */
    address_space_remove_listeners(&iommu->as);
    address_space_destroy(&iommu->as);
    object_unparent(OBJECT(&iommu->mr));
    object_unparent(OBJECT(iommu));
    object_unref(OBJECT(iommu));
}

S390PCIGroup *s390_group_create(int id, int host_id)
{
    S390PCIGroup *group;
    S390pciState *s = s390_get_phb();

    group = g_new0(S390PCIGroup, 1);
    group->id = id;
    group->host_id = host_id;
    QTAILQ_INSERT_TAIL(&s->zpci_groups, group, link);
    return group;
}

S390PCIGroup *s390_group_find(int id)
{
    S390PCIGroup *group;
    S390pciState *s = s390_get_phb();

    QTAILQ_FOREACH(group, &s->zpci_groups, link) {
        if (group->id == id) {
            return group;
        }
    }
    return NULL;
}

S390PCIGroup *s390_group_find_host_sim(int host_id)
{
    S390PCIGroup *group;
    S390pciState *s = s390_get_phb();

    QTAILQ_FOREACH(group, &s->zpci_groups, link) {
        if (group->id >= ZPCI_SIM_GRP_START && group->host_id == host_id) {
            return group;
        }
    }
    return NULL;
}

static void s390_pci_init_default_group(void)
{
    S390PCIGroup *group;
    ClpRspQueryPciGrp *resgrp;

    group = s390_group_create(ZPCI_DEFAULT_FN_GRP, ZPCI_DEFAULT_FN_GRP);
    resgrp = &group->zpci_group;
    resgrp->fr = 1;
    resgrp->dasm = 0;
    resgrp->msia = ZPCI_MSI_ADDR;
    resgrp->mui = DEFAULT_MUI;
    resgrp->i = 128;
    resgrp->maxstbl = 128;
    resgrp->version = 0;
    resgrp->dtsm = ZPCI_DTSM;
}

static void set_pbdev_info(S390PCIBusDevice *pbdev)
{
    pbdev->zpci_fn.sdma = ZPCI_SDMA_ADDR;
    pbdev->zpci_fn.edma = ZPCI_EDMA_ADDR;
    pbdev->zpci_fn.pchid = 0;
    pbdev->zpci_fn.pfgid = ZPCI_DEFAULT_FN_GRP;
    pbdev->zpci_fn.fid = pbdev->fid;
    pbdev->zpci_fn.uid = pbdev->uid;
    pbdev->pci_group = s390_group_find(ZPCI_DEFAULT_FN_GRP);
}

static void s390_pcihost_realize(DeviceState *dev, Error **errp)
{
    PCIBus *b;
    BusState *bus;
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    S390pciState *s = S390_PCI_HOST_BRIDGE(dev);

    trace_s390_pcihost("realize");

    b = pci_register_root_bus(dev, NULL, s390_pci_set_irq, s390_pci_map_irq,
                              NULL, get_system_memory(), get_system_io(), 0,
                              64, TYPE_PCI_BUS);
    pci_setup_iommu(b, &s390_iommu_ops, s);

    bus = BUS(b);
    qbus_set_hotplug_handler(bus, OBJECT(dev));
    phb->bus = b;

    s->bus = S390_PCI_BUS(qbus_new(TYPE_S390_PCI_BUS, dev, NULL));
    qbus_set_hotplug_handler(BUS(s->bus), OBJECT(dev));

    s->iommu_table = g_hash_table_new_full(g_int64_hash, g_int64_equal,
                                           NULL, g_free);
    s->zpci_table = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);
    s->bus_no = 0;
    s->next_sim_grp = ZPCI_SIM_GRP_START;
    QTAILQ_INIT(&s->pending_sei);
    QTAILQ_INIT(&s->zpci_devs);
    QTAILQ_INIT(&s->zpci_dma_limit);
    QTAILQ_INIT(&s->zpci_groups);

    s390_pci_init_default_group();
    css_register_io_adapters(CSS_IO_ADAPTER_PCI, true, false,
                             S390_ADAPTER_SUPPRESSIBLE, errp);
}

static void s390_pcihost_unrealize(DeviceState *dev)
{
    S390PCIGroup *group;
    S390pciState *s = S390_PCI_HOST_BRIDGE(dev);

    while (!QTAILQ_EMPTY(&s->zpci_groups)) {
        group = QTAILQ_FIRST(&s->zpci_groups);
        QTAILQ_REMOVE(&s->zpci_groups, group, link);
    }
}

static int s390_pci_msix_init(S390PCIBusDevice *pbdev)
{
    char *name;
    uint8_t pos;
    uint16_t ctrl;
    uint32_t table, pba;

    pos = pci_find_capability(pbdev->pdev, PCI_CAP_ID_MSIX);
    if (!pos) {
        return -1;
    }

    ctrl = pci_host_config_read_common(pbdev->pdev, pos + PCI_MSIX_FLAGS,
             pci_config_size(pbdev->pdev), sizeof(ctrl));
    table = pci_host_config_read_common(pbdev->pdev, pos + PCI_MSIX_TABLE,
             pci_config_size(pbdev->pdev), sizeof(table));
    pba = pci_host_config_read_common(pbdev->pdev, pos + PCI_MSIX_PBA,
             pci_config_size(pbdev->pdev), sizeof(pba));

    pbdev->msix.table_bar = table & PCI_MSIX_FLAGS_BIRMASK;
    pbdev->msix.table_offset = table & ~PCI_MSIX_FLAGS_BIRMASK;
    pbdev->msix.pba_bar = pba & PCI_MSIX_FLAGS_BIRMASK;
    pbdev->msix.pba_offset = pba & ~PCI_MSIX_FLAGS_BIRMASK;
    pbdev->msix.entries = (ctrl & PCI_MSIX_FLAGS_QSIZE) + 1;

    name = g_strdup_printf("msix-s390-%04x", pbdev->uid);
    memory_region_init_io(&pbdev->msix_notify_mr, OBJECT(pbdev),
                          &s390_msi_ctrl_ops, pbdev, name, TARGET_PAGE_SIZE);
    memory_region_add_subregion(&pbdev->iommu->mr,
                                pbdev->pci_group->zpci_group.msia,
                                &pbdev->msix_notify_mr);
    g_free(name);

    return 0;
}

static void s390_pci_msix_free(S390PCIBusDevice *pbdev)
{
    if (pbdev->msix.entries == 0) {
        return;
    }

    memory_region_del_subregion(&pbdev->iommu->mr, &pbdev->msix_notify_mr);
    object_unparent(OBJECT(&pbdev->msix_notify_mr));
}

static S390PCIBusDevice *s390_pci_device_new(S390pciState *s,
                                             const char *target, Error **errp)
{
    Error *local_err = NULL;
    DeviceState *dev;

    dev = qdev_try_new(TYPE_S390_PCI_DEVICE);
    if (!dev) {
        error_setg(errp, "zPCI device could not be created");
        return NULL;
    }

    if (!object_property_set_str(OBJECT(dev), "target", target, &local_err)) {
        object_unparent(OBJECT(dev));
        error_propagate_prepend(errp, local_err,
                                "zPCI device could not be created: ");
        return NULL;
    }
    if (!qdev_realize_and_unref(dev, BUS(s->bus), &local_err)) {
        object_unparent(OBJECT(dev));
        error_propagate_prepend(errp, local_err,
                                "zPCI device could not be created: ");
        return NULL;
    }

    return S390_PCI_DEVICE(dev);
}

static bool s390_pci_alloc_idx(S390pciState *s, S390PCIBusDevice *pbdev)
{
    uint32_t idx;

    idx = s->next_idx;
    while (s390_pci_find_dev_by_idx(s, idx)) {
        idx = (idx + 1) & FH_MASK_INDEX;
        if (idx == s->next_idx) {
            return false;
        }
    }

    pbdev->idx = idx;
    return true;
}

static void s390_pcihost_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                                   Error **errp)
{
    S390pciState *s = S390_PCI_HOST_BRIDGE(hotplug_dev);

    if (!s390_has_feat(S390_FEAT_ZPCI)) {
        warn_report("Plugging a PCI/zPCI device without the 'zpci' CPU "
                    "feature enabled; the guest will not be able to see/use "
                    "this device");
    }

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        PCIDevice *pdev = PCI_DEVICE(dev);

        if (pdev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
            error_setg(errp, "multifunction not supported in s390");
            return;
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_S390_PCI_DEVICE)) {
        S390PCIBusDevice *pbdev = S390_PCI_DEVICE(dev);

        if (!s390_pci_alloc_idx(s, pbdev)) {
            error_setg(errp, "no slot for plugging zpci device");
            return;
        }
    }
}

static void s390_pci_update_subordinate(PCIDevice *dev, uint32_t nr)
{
    uint32_t old_nr;

    pci_default_write_config(dev, PCI_SUBORDINATE_BUS, nr, 1);
    while (!pci_bus_is_root(pci_get_bus(dev))) {
        dev = pci_get_bus(dev)->parent_dev;

        old_nr = pci_default_read_config(dev, PCI_SUBORDINATE_BUS, 1);
        if (old_nr < nr) {
            pci_default_write_config(dev, PCI_SUBORDINATE_BUS, nr, 1);
        }
    }
}

static int s390_pci_interp_plug(S390pciState *s, S390PCIBusDevice *pbdev)
{
    uint32_t idx, fh;

    if (!s390_pci_get_host_fh(pbdev, &fh)) {
        return -EPERM;
    }

    /*
     * The host device is already in an enabled state, but we always present
     * the initial device state to the guest as disabled (ZPCI_FS_DISABLED).
     * Therefore, mask off the enable bit from the passthrough handle until
     * the guest issues a CLP SET PCI FN later to enable the device.
     */
    pbdev->fh = fh & ~FH_MASK_ENABLE;

    /* Next, see if the idx is already in-use */
    idx = pbdev->fh & FH_MASK_INDEX;
    if (pbdev->idx != idx) {
        if (s390_pci_find_dev_by_idx(s, idx)) {
            return -EINVAL;
        }
        /*
         * Update the idx entry with the passed through idx
         * If the relinquished idx is lower than next_idx, use it
         * to replace next_idx
         */
        g_hash_table_remove(s->zpci_table, &pbdev->idx);
        if (idx < s->next_idx) {
            s->next_idx = idx;
        }
        pbdev->idx = idx;
        g_hash_table_insert(s->zpci_table, &pbdev->idx, pbdev);
    }

    return 0;
}

static void s390_pcihost_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                              Error **errp)
{
    S390pciState *s = S390_PCI_HOST_BRIDGE(hotplug_dev);
    PCIDevice *pdev = NULL;
    S390PCIBusDevice *pbdev = NULL;
    int rc;

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)) {
        PCIBridge *pb = PCI_BRIDGE(dev);

        pdev = PCI_DEVICE(dev);
        pci_bridge_map_irq(pb, dev->id, s390_pci_map_irq);
        pci_setup_iommu(&pb->sec_bus, &s390_iommu_ops, s);

        qbus_set_hotplug_handler(BUS(&pb->sec_bus), OBJECT(s));

        if (dev->hotplugged) {
            pci_default_write_config(pdev, PCI_PRIMARY_BUS,
                                     pci_dev_bus_num(pdev), 1);
            s->bus_no += 1;
            pci_default_write_config(pdev, PCI_SECONDARY_BUS, s->bus_no, 1);

            s390_pci_update_subordinate(pdev, s->bus_no);
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        pdev = PCI_DEVICE(dev);

        if (!dev->id) {
            /* In the case the PCI device does not define an id */
            /* we generate one based on the PCI address         */
            dev->id = g_strdup_printf("auto_%02x:%02x.%01x",
                                      pci_dev_bus_num(pdev),
                                      PCI_SLOT(pdev->devfn),
                                      PCI_FUNC(pdev->devfn));
        }

        pbdev = s390_pci_find_dev_by_target(s, dev->id);
        if (!pbdev) {
            pbdev = s390_pci_device_new(s, dev->id, errp);
            if (!pbdev) {
                return;
            }
        }

        pbdev->pdev = pdev;
        pbdev->iommu = s390_pci_get_iommu(s, pci_get_bus(pdev), pdev->devfn);
        pbdev->iommu->pbdev = pbdev;
        pbdev->state = ZPCI_FS_DISABLED;
        set_pbdev_info(pbdev);

        if (object_dynamic_cast(OBJECT(dev), "vfio-pci")) {
            /*
             * By default, interpretation is always requested; if the available
             * facilities indicate it is not available, fallback to the
             * interception model.
             */
            if (pbdev->interp) {
                if (s390_pci_kvm_interp_allowed()) {
                    rc = s390_pci_interp_plug(s, pbdev);
                    if (rc) {
                        error_setg(errp, "Plug failed for zPCI device in "
                                   "interpretation mode: %d", rc);
                        return;
                    }
                } else {
                    trace_s390_pcihost("zPCI interpretation missing");
                    pbdev->interp = false;
                    pbdev->forwarding_assist = false;
                }
            }
            pbdev->iommu->dma_limit = s390_pci_start_dma_count(s, pbdev);
            /* Fill in CLP information passed via the vfio region */
            s390_pci_get_clp_info(pbdev);
            if (!pbdev->interp) {
                /* Do vfio passthrough but intercept for I/O */
                pbdev->fh |= FH_SHM_VFIO;
                pbdev->forwarding_assist = false;
            }
            /* Register shutdown notifier and reset callback for ISM devices */
            if (pbdev->pft == ZPCI_PFT_ISM) {
                pbdev->shutdown_notifier.notify = s390_pci_shutdown_notifier;
                qemu_register_shutdown_notifier(&pbdev->shutdown_notifier);
            }
        } else {
            pbdev->fh |= FH_SHM_EMUL;
            /* Always intercept emulated devices */
            pbdev->interp = false;
            pbdev->forwarding_assist = false;
        }

        if (s390_pci_msix_init(pbdev) && !pbdev->interp) {
            error_setg(errp, "MSI-X support is mandatory "
                       "in the S390 architecture");
            return;
        }

        if (dev->hotplugged) {
            s390_pci_generate_plug_event(HP_EVENT_TO_CONFIGURED ,
                                         pbdev->fh, pbdev->fid);
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_S390_PCI_DEVICE)) {
        pbdev = S390_PCI_DEVICE(dev);

        /* the allocated idx is actually getting used */
        s->next_idx = (pbdev->idx + 1) & FH_MASK_INDEX;
        pbdev->fh = pbdev->idx;
        QTAILQ_INSERT_TAIL(&s->zpci_devs, pbdev, link);
        g_hash_table_insert(s->zpci_table, &pbdev->idx, pbdev);
    } else {
        g_assert_not_reached();
    }
}

static void s390_pcihost_unplug(HotplugHandler *hotplug_dev, DeviceState *dev,
                                Error **errp)
{
    S390pciState *s = S390_PCI_HOST_BRIDGE(hotplug_dev);
    S390PCIBusDevice *pbdev = NULL;

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        PCIDevice *pci_dev = PCI_DEVICE(dev);
        PCIBus *bus;
        int32_t devfn;

        pbdev = s390_pci_find_dev_by_pci(s, PCI_DEVICE(dev));
        g_assert(pbdev);

        s390_pci_generate_plug_event(HP_EVENT_STANDBY_TO_RESERVED,
                                     pbdev->fh, pbdev->fid);
        bus = pci_get_bus(pci_dev);
        devfn = pci_dev->devfn;
        qdev_unrealize(dev);

        s390_pci_msix_free(pbdev);
        s390_pci_iommu_free(s, bus, devfn);
        pbdev->pdev = NULL;
        pbdev->state = ZPCI_FS_RESERVED;
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_S390_PCI_DEVICE)) {
        pbdev = S390_PCI_DEVICE(dev);
        pbdev->fid = 0;
        QTAILQ_REMOVE(&s->zpci_devs, pbdev, link);
        g_hash_table_remove(s->zpci_table, &pbdev->idx);
        if (pbdev->iommu->dma_limit) {
            s390_pci_end_dma_count(s, pbdev->iommu->dma_limit);
        }
        qdev_unrealize(dev);
    }
}

static void s390_pcihost_unplug_request(HotplugHandler *hotplug_dev,
                                        DeviceState *dev,
                                        Error **errp)
{
    S390pciState *s = S390_PCI_HOST_BRIDGE(hotplug_dev);
    S390PCIBusDevice *pbdev;

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)) {
        error_setg(errp, "PCI bridge hot unplug currently not supported");
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        /*
         * Redirect the unplug request to the zPCI device and remember that
         * we've checked the PCI device already (to prevent endless recursion).
         */
        pbdev = s390_pci_find_dev_by_pci(s, PCI_DEVICE(dev));
        g_assert(pbdev);
        pbdev->pci_unplug_request_processed = true;
        qdev_unplug(DEVICE(pbdev), errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_S390_PCI_DEVICE)) {
        pbdev = S390_PCI_DEVICE(dev);

        /*
         * If unplug was initially requested for the zPCI device, we
         * first have to redirect to the PCI device, which will in return
         * redirect back to us after performing its checks (if the request
         * is not blocked, e.g. because it's a PCI bridge).
         */
        if (pbdev->pdev && !pbdev->pci_unplug_request_processed) {
            qdev_unplug(DEVICE(pbdev->pdev), errp);
            return;
        }
        pbdev->pci_unplug_request_processed = false;

        switch (pbdev->state) {
        case ZPCI_FS_STANDBY:
        case ZPCI_FS_RESERVED:
            s390_pci_perform_unplug(pbdev);
            break;
        default:
            /*
             * Allow to send multiple requests, e.g. if the guest crashed
             * before releasing the device, we would not be able to send
             * another request to the same VM (e.g. fresh OS).
             */
            pbdev->unplug_requested = true;
            s390_pci_generate_plug_event(HP_EVENT_DECONFIGURE_REQUEST,
                                         pbdev->fh, pbdev->fid);
        }
    } else {
        g_assert_not_reached();
    }
}

static void s390_pci_enumerate_bridge(PCIBus *bus, PCIDevice *pdev,
                                      void *opaque)
{
    S390pciState *s = opaque;
    PCIBus *sec_bus = NULL;

    if ((pci_default_read_config(pdev, PCI_HEADER_TYPE, 1) !=
         PCI_HEADER_TYPE_BRIDGE)) {
        return;
    }

    (s->bus_no)++;
    pci_default_write_config(pdev, PCI_PRIMARY_BUS, pci_dev_bus_num(pdev), 1);
    pci_default_write_config(pdev, PCI_SECONDARY_BUS, s->bus_no, 1);
    pci_default_write_config(pdev, PCI_SUBORDINATE_BUS, s->bus_no, 1);

    sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));
    if (!sec_bus) {
        return;
    }

    /* Assign numbers to all child bridges. The last is the highest number. */
    pci_for_each_device_under_bus(sec_bus, s390_pci_enumerate_bridge, s);
    pci_default_write_config(pdev, PCI_SUBORDINATE_BUS, s->bus_no, 1);
}

void s390_pci_ism_reset(void)
{
    S390pciState *s = s390_get_phb();

    S390PCIBusDevice *pbdev, *next;

    /* Trigger reset event for each passthrough ISM device currently in-use */
    QTAILQ_FOREACH_SAFE(pbdev, &s->zpci_devs, link, next) {
        if (pbdev->interp && pbdev->pft == ZPCI_PFT_ISM &&
            pbdev->fh & FH_MASK_ENABLE) {
            s390_pci_kvm_aif_disable(pbdev);

            pci_device_reset(pbdev->pdev);
        }
    }
}

static void s390_pcihost_reset(DeviceState *dev)
{
    S390pciState *s = S390_PCI_HOST_BRIDGE(dev);
    PCIBus *bus = s->parent_obj.bus;
    S390PCIBusDevice *pbdev, *next;

    /* Process all pending unplug requests */
    QTAILQ_FOREACH_SAFE(pbdev, &s->zpci_devs, link, next) {
        if (pbdev->unplug_requested) {
            if (pbdev->interp && (pbdev->fh & FH_MASK_ENABLE)) {
                /* Interpreted devices were using interrupt forwarding */
                s390_pci_kvm_aif_disable(pbdev);
            } else if (pbdev->summary_ind) {
                pci_dereg_irqs(pbdev);
            }
            if (pbdev->iommu->enabled) {
                pci_dereg_ioat(pbdev->iommu);
            }
            pbdev->state = ZPCI_FS_STANDBY;
            s390_pci_perform_unplug(pbdev);
        }
    }

    /*
     * When resetting a PCI bridge, the assigned numbers are set to 0. So
     * on every system reset, we also have to reassign numbers.
     */
    s->bus_no = 0;
    pci_for_each_device_under_bus(bus, s390_pci_enumerate_bridge, s);
}

static void s390_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    dc->reset = s390_pcihost_reset;
    dc->realize = s390_pcihost_realize;
    dc->unrealize = s390_pcihost_unrealize;
    hc->pre_plug = s390_pcihost_pre_plug;
    hc->plug = s390_pcihost_plug;
    hc->unplug_request = s390_pcihost_unplug_request;
    hc->unplug = s390_pcihost_unplug;
    msi_nonbroken = true;
}

static const TypeInfo s390_pcihost_info = {
    .name          = TYPE_S390_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(S390pciState),
    .class_init    = s390_pcihost_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static const TypeInfo s390_pcibus_info = {
    .name = TYPE_S390_PCI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(S390PCIBus),
};

static uint16_t s390_pci_generate_uid(S390pciState *s)
{
    uint16_t uid = 0;

    do {
        uid++;
        if (!s390_pci_find_dev_by_uid(s, uid)) {
            return uid;
        }
    } while (uid < ZPCI_MAX_UID);

    return UID_UNDEFINED;
}

static uint32_t s390_pci_generate_fid(S390pciState *s, Error **errp)
{
    uint32_t fid = 0;

    do {
        if (!s390_pci_find_dev_by_fid(s, fid)) {
            return fid;
        }
    } while (fid++ != ZPCI_MAX_FID);

    error_setg(errp, "no free fid could be found");
    return 0;
}

static void s390_pci_device_realize(DeviceState *dev, Error **errp)
{
    S390PCIBusDevice *zpci = S390_PCI_DEVICE(dev);
    S390pciState *s = s390_get_phb();

    if (!zpci->target) {
        error_setg(errp, "target must be defined");
        return;
    }

    if (s390_pci_find_dev_by_target(s, zpci->target)) {
        error_setg(errp, "target %s already has an associated zpci device",
                   zpci->target);
        return;
    }

    if (zpci->uid == UID_UNDEFINED) {
        zpci->uid = s390_pci_generate_uid(s);
        if (!zpci->uid) {
            error_setg(errp, "no free uid could be found");
            return;
        }
    } else if (s390_pci_find_dev_by_uid(s, zpci->uid)) {
        error_setg(errp, "uid %u already in use", zpci->uid);
        return;
    }

    if (!zpci->fid_defined) {
        Error *local_error = NULL;

        zpci->fid = s390_pci_generate_fid(s, &local_error);
        if (local_error) {
            error_propagate(errp, local_error);
            return;
        }
    } else if (s390_pci_find_dev_by_fid(s, zpci->fid)) {
        error_setg(errp, "fid %u already in use", zpci->fid);
        return;
    }

    zpci->state = ZPCI_FS_RESERVED;
    zpci->fmb.format = ZPCI_FMB_FORMAT;
}

static void s390_pci_device_reset(DeviceState *dev)
{
    S390PCIBusDevice *pbdev = S390_PCI_DEVICE(dev);

    switch (pbdev->state) {
    case ZPCI_FS_RESERVED:
        return;
    case ZPCI_FS_STANDBY:
        break;
    default:
        pbdev->fh &= ~FH_MASK_ENABLE;
        pbdev->state = ZPCI_FS_DISABLED;
        break;
    }

    if (pbdev->interp && (pbdev->fh & FH_MASK_ENABLE)) {
        /* Interpreted devices were using interrupt forwarding */
        s390_pci_kvm_aif_disable(pbdev);
    } else if (pbdev->summary_ind) {
        pci_dereg_irqs(pbdev);
    }
    if (pbdev->iommu->enabled) {
        pci_dereg_ioat(pbdev->iommu);
    }

    fmb_timer_free(pbdev);
}

static void s390_pci_get_fid(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint32(v, name, ptr, errp);
}

static void s390_pci_set_fid(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    S390PCIBusDevice *zpci = S390_PCI_DEVICE(obj);
    Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);

    if (!visit_type_uint32(v, name, ptr, errp)) {
        return;
    }
    zpci->fid_defined = true;
}

static const PropertyInfo s390_pci_fid_propinfo = {
    .name = "zpci_fid",
    .get = s390_pci_get_fid,
    .set = s390_pci_set_fid,
};

#define DEFINE_PROP_S390_PCI_FID(_n, _s, _f) \
    DEFINE_PROP(_n, _s, _f, s390_pci_fid_propinfo, uint32_t)

static Property s390_pci_device_properties[] = {
    DEFINE_PROP_UINT16("uid", S390PCIBusDevice, uid, UID_UNDEFINED),
    DEFINE_PROP_S390_PCI_FID("fid", S390PCIBusDevice, fid),
    DEFINE_PROP_STRING("target", S390PCIBusDevice, target),
    DEFINE_PROP_BOOL("interpret", S390PCIBusDevice, interp, true),
    DEFINE_PROP_BOOL("forwarding-assist", S390PCIBusDevice, forwarding_assist,
                     true),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription s390_pci_device_vmstate = {
    .name = TYPE_S390_PCI_DEVICE,
    /*
     * TODO: add state handling here, so migration works at least with
     * emulated pci devices on s390x
     */
    .unmigratable = 1,
};

static void s390_pci_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "zpci device";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = s390_pci_device_reset;
    dc->bus_type = TYPE_S390_PCI_BUS;
    dc->realize = s390_pci_device_realize;
    device_class_set_props(dc, s390_pci_device_properties);
    dc->vmsd = &s390_pci_device_vmstate;
}

static const TypeInfo s390_pci_device_info = {
    .name = TYPE_S390_PCI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(S390PCIBusDevice),
    .class_init = s390_pci_device_class_init,
};

static const TypeInfo s390_pci_iommu_info = {
    .name = TYPE_S390_PCI_IOMMU,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(S390PCIIOMMU),
};

static void s390_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = s390_translate_iommu;
    imrc->replay = s390_pci_iommu_replay;
}

static const TypeInfo s390_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_S390_IOMMU_MEMORY_REGION,
    .class_init = s390_iommu_memory_region_class_init,
};

static void s390_pci_register_types(void)
{
    type_register_static(&s390_pcihost_info);
    type_register_static(&s390_pcibus_info);
    type_register_static(&s390_pci_device_info);
    type_register_static(&s390_pci_iommu_info);
    type_register_static(&s390_iommu_memory_region_info);
}

type_init(s390_pci_register_types)
