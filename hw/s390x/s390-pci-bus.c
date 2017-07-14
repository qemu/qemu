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
#include "qemu-common.h"
#include "cpu.h"
#include "s390-pci-bus.h"
#include "s390-pci-inst.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/msi.h"
#include "qemu/error-report.h"

#ifndef DEBUG_S390PCI_BUS
#define DEBUG_S390PCI_BUS  0
#endif

#define DPRINTF(fmt, ...)                                         \
    do {                                                          \
        if (DEBUG_S390PCI_BUS) {                                  \
            fprintf(stderr, "S390pci-bus: " fmt, ## __VA_ARGS__); \
        }                                                         \
    } while (0)

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

int chsc_sei_nt2_get_event(void *res)
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

int chsc_sei_nt2_have_event(void)
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
    PciCfgSccb *psccb = (PciCfgSccb *)sccb;
    S390PCIBusDevice *pbdev = s390_pci_find_dev_by_fid(s390_get_phb(),
                                                       be32_to_cpu(psccb->aid));
    uint16_t rc;

    if (be16_to_cpu(sccb->h.length) < 16) {
        rc = SCLP_RC_INSUFFICIENT_SCCB_LENGTH;
        goto out;
    }

    if (!pbdev) {
        DPRINTF("sclp config no dev found\n");
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

void s390_pci_sclp_deconfigure(SCCB *sccb)
{
    PciCfgSccb *psccb = (PciCfgSccb *)sccb;
    S390PCIBusDevice *pbdev = s390_pci_find_dev_by_fid(s390_get_phb(),
                                                       be32_to_cpu(psccb->aid));
    uint16_t rc;

    if (be16_to_cpu(sccb->h.length) < 16) {
        rc = SCLP_RC_INSUFFICIENT_SCCB_LENGTH;
        goto out;
    }

    if (!pbdev) {
        DPRINTF("sclp deconfig no dev found\n");
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
        if (pbdev->summary_ind) {
            pci_dereg_irqs(pbdev);
        }
        if (pbdev->iommu->enabled) {
            pci_dereg_ioat(pbdev->iommu);
        }
        pbdev->state = ZPCI_FS_STANDBY;
        rc = SCLP_RC_NORMAL_COMPLETION;

        if (pbdev->release_timer) {
            qdev_unplug(DEVICE(pbdev->pdev), NULL);
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

static S390PCIBusDevice *s390_pci_find_dev_by_target(S390pciState *s,
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

    sei_cont = g_malloc0(sizeof(SeiContainer));
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
    return ((unsigned long) ptr >> PAGE_SHIFT) & ZPCI_PT_MASK;
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

static uint64_t s390_guest_io_table_walk(uint64_t guest_iota,
                                  uint64_t guest_dma_address)
{
    uint64_t sto_a, pto_a, px_a;
    uint64_t sto, pto, pte;
    uint32_t rtx, sx, px;

    rtx = calc_rtx(guest_dma_address);
    sx = calc_sx(guest_dma_address);
    px = calc_px(guest_dma_address);

    sto_a = guest_iota + rtx * sizeof(uint64_t);
    sto = address_space_ldq(&address_space_memory, sto_a,
                            MEMTXATTRS_UNSPECIFIED, NULL);
    sto = get_rt_sto(sto);
    if (!sto) {
        pte = 0;
        goto out;
    }

    pto_a = sto + sx * sizeof(uint64_t);
    pto = address_space_ldq(&address_space_memory, pto_a,
                            MEMTXATTRS_UNSPECIFIED, NULL);
    pto = get_st_pto(pto);
    if (!pto) {
        pte = 0;
        goto out;
    }

    px_a = pto + px * sizeof(uint64_t);
    pte = address_space_ldq(&address_space_memory, px_a,
                            MEMTXATTRS_UNSPECIFIED, NULL);

out:
    return pte;
}

static IOMMUTLBEntry s390_translate_iommu(IOMMUMemoryRegion *mr, hwaddr addr,
                                          IOMMUAccessFlags flag)
{
    uint64_t pte;
    uint32_t flags;
    S390PCIIOMMU *iommu = container_of(mr, S390PCIIOMMU, iommu_mr);
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

    DPRINTF("iommu trans addr 0x%" PRIx64 "\n", addr);

    if (addr < iommu->pba || addr > iommu->pal) {
        return ret;
    }

    pte = s390_guest_io_table_walk(s390_pci_get_table_origin(iommu->g_iota),
                                   addr);
    if (!pte) {
        return ret;
    }

    flags = pte & ZPCI_PTE_FLAG_MASK;
    ret.iova = addr;
    ret.translated_addr = pte & ZPCI_PTE_ADDR_MASK;
    ret.addr_mask = 0xfff;

    if (flags & ZPCI_PTE_INVALID) {
        ret.perm = IOMMU_NONE;
    } else {
        ret.perm = IOMMU_RW;
    }

    return ret;
}

static S390PCIIOMMU *s390_pci_get_iommu(S390pciState *s, PCIBus *bus,
                                        int devfn)
{
    uint64_t key = (uintptr_t)bus;
    S390PCIIOMMUTable *table = g_hash_table_lookup(s->iommu_table, &key);
    S390PCIIOMMU *iommu;

    if (!table) {
        table = g_malloc0(sizeof(S390PCIIOMMUTable));
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

static uint8_t set_ind_atomic(uint64_t ind_loc, uint8_t to_be_set)
{
    uint8_t ind_old, ind_new;
    hwaddr len = 1;
    uint8_t *ind_addr;

    ind_addr = cpu_physical_memory_map(ind_loc, &len, 1);
    if (!ind_addr) {
        s390_pci_generate_error_event(ERR_EVENT_AIRERR, 0, 0, 0, 0);
        return -1;
    }
    do {
        ind_old = *ind_addr;
        ind_new = ind_old | to_be_set;
    } while (atomic_cmpxchg(ind_addr, ind_old, ind_new) != ind_old);
    cpu_physical_memory_unmap(ind_addr, len, 1, len);

    return ind_old;
}

static void s390_msi_ctrl_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned int size)
{
    S390PCIBusDevice *pbdev = opaque;
    uint32_t idx = data >> ZPCI_MSI_VEC_BITS;
    uint32_t vec = data & ZPCI_MSI_VEC_MASK;
    uint64_t ind_bit;
    uint32_t sum_bit;
    uint32_t e = 0;

    DPRINTF("write_msix data 0x%" PRIx64 " idx %d vec 0x%x\n", data, idx, vec);

    if (!pbdev) {
        e |= (vec << ERR_EVENT_MVN_OFFSET);
        s390_pci_generate_error_event(ERR_EVENT_NOMSI, idx, 0, addr, e);
        return;
    }

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
    address_space_destroy(&iommu->as);
    object_unparent(OBJECT(&iommu->mr));
    object_unparent(OBJECT(iommu));
    object_unref(OBJECT(iommu));
}

static int s390_pcihost_init(SysBusDevice *dev)
{
    PCIBus *b;
    BusState *bus;
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    S390pciState *s = S390_PCI_HOST_BRIDGE(dev);

    DPRINTF("host_init\n");

    b = pci_register_bus(DEVICE(dev), NULL,
                         s390_pci_set_irq, s390_pci_map_irq, NULL,
                         get_system_memory(), get_system_io(), 0, 64,
                         TYPE_PCI_BUS);
    pci_setup_iommu(b, s390_pci_dma_iommu, s);

    bus = BUS(b);
    qbus_set_hotplug_handler(bus, DEVICE(dev), NULL);
    phb->bus = b;

    s->bus = S390_PCI_BUS(qbus_create(TYPE_S390_PCI_BUS, DEVICE(s), NULL));
    qbus_set_hotplug_handler(BUS(s->bus), DEVICE(s), NULL);

    s->iommu_table = g_hash_table_new_full(g_int64_hash, g_int64_equal,
                                           NULL, g_free);
    s->zpci_table = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);
    s->bus_no = 0;
    QTAILQ_INIT(&s->pending_sei);
    QTAILQ_INIT(&s->zpci_devs);

    css_register_io_adapters(CSS_IO_ADAPTER_PCI, true, false,
                             S390_ADAPTER_SUPPRESSIBLE, &error_abort);

    return 0;
}

static int s390_pci_msix_init(S390PCIBusDevice *pbdev)
{
    char *name;
    uint8_t pos;
    uint16_t ctrl;
    uint32_t table, pba;

    pos = pci_find_capability(pbdev->pdev, PCI_CAP_ID_MSIX);
    if (!pos) {
        pbdev->msix.available = false;
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
    pbdev->msix.available = true;

    name = g_strdup_printf("msix-s390-%04x", pbdev->uid);
    memory_region_init_io(&pbdev->msix_notify_mr, OBJECT(pbdev),
                          &s390_msi_ctrl_ops, pbdev, name, PAGE_SIZE);
    memory_region_add_subregion(&pbdev->iommu->mr, ZPCI_MSI_ADDR,
                                &pbdev->msix_notify_mr);
    g_free(name);

    return 0;
}

static void s390_pci_msix_free(S390PCIBusDevice *pbdev)
{
    memory_region_del_subregion(&pbdev->iommu->mr, &pbdev->msix_notify_mr);
    object_unparent(OBJECT(&pbdev->msix_notify_mr));
}

static S390PCIBusDevice *s390_pci_device_new(S390pciState *s,
                                             const char *target)
{
    DeviceState *dev = NULL;

    dev = qdev_try_create(BUS(s->bus), TYPE_S390_PCI_DEVICE);
    if (!dev) {
        return NULL;
    }

    qdev_prop_set_string(dev, "target", target);
    qdev_init_nofail(dev);

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
    s->next_idx = (idx + 1) & FH_MASK_INDEX;

    return true;
}

static void s390_pcihost_hot_plug(HotplugHandler *hotplug_dev,
                                  DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = NULL;
    S390PCIBusDevice *pbdev = NULL;
    S390pciState *s = s390_get_phb();

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)) {
        BusState *bus;
        PCIBridge *pb = PCI_BRIDGE(dev);
        PCIDevice *pdev = PCI_DEVICE(dev);

        pci_bridge_map_irq(pb, dev->id, s390_pci_map_irq);
        pci_setup_iommu(&pb->sec_bus, s390_pci_dma_iommu, s);

        bus = BUS(&pb->sec_bus);
        qbus_set_hotplug_handler(bus, DEVICE(s), errp);

        if (dev->hotplugged) {
            pci_default_write_config(pdev, PCI_PRIMARY_BUS, s->bus_no, 1);
            s->bus_no += 1;
            pci_default_write_config(pdev, PCI_SECONDARY_BUS, s->bus_no, 1);
            do {
                pdev = pdev->bus->parent_dev;
                pci_default_write_config(pdev, PCI_SUBORDINATE_BUS,
                                         s->bus_no, 1);
            } while (pdev->bus && pci_bus_num(pdev->bus));
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        pdev = PCI_DEVICE(dev);

        if (!dev->id) {
            /* In the case the PCI device does not define an id */
            /* we generate one based on the PCI address         */
            dev->id = g_strdup_printf("auto_%02x:%02x.%01x",
                                      pci_bus_num(pdev->bus),
                                      PCI_SLOT(pdev->devfn),
                                      PCI_FUNC(pdev->devfn));
        }

        pbdev = s390_pci_find_dev_by_target(s, dev->id);
        if (!pbdev) {
            pbdev = s390_pci_device_new(s, dev->id);
            if (!pbdev) {
                error_setg(errp, "create zpci device failed");
                return;
            }
        }

        if (object_dynamic_cast(OBJECT(dev), "vfio-pci")) {
            pbdev->fh |= FH_SHM_VFIO;
        } else {
            pbdev->fh |= FH_SHM_EMUL;
        }

        pbdev->pdev = pdev;
        pbdev->iommu = s390_pci_get_iommu(s, pdev->bus, pdev->devfn);
        pbdev->iommu->pbdev = pbdev;
        pbdev->state = ZPCI_FS_STANDBY;

        if (s390_pci_msix_init(pbdev)) {
            error_setg(errp, "MSI-X support is mandatory "
                       "in the S390 architecture");
            return;
        }

        if (dev->hotplugged) {
            s390_pci_generate_plug_event(HP_EVENT_RESERVED_TO_STANDBY,
                                         pbdev->fh, pbdev->fid);
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_S390_PCI_DEVICE)) {
        pbdev = S390_PCI_DEVICE(dev);

        if (!s390_pci_alloc_idx(s, pbdev)) {
            error_setg(errp, "no slot for plugging zpci device");
            return;
        }
        pbdev->fh = pbdev->idx;
        QTAILQ_INSERT_TAIL(&s->zpci_devs, pbdev, link);
        g_hash_table_insert(s->zpci_table, &pbdev->idx, pbdev);
    }
}

static void s390_pcihost_timer_cb(void *opaque)
{
    S390PCIBusDevice *pbdev = opaque;

    if (pbdev->summary_ind) {
        pci_dereg_irqs(pbdev);
    }
    if (pbdev->iommu->enabled) {
        pci_dereg_ioat(pbdev->iommu);
    }

    pbdev->state = ZPCI_FS_STANDBY;
    s390_pci_generate_plug_event(HP_EVENT_CONFIGURED_TO_STBRES,
                                 pbdev->fh, pbdev->fid);
    qdev_unplug(DEVICE(pbdev), NULL);
}

static void s390_pcihost_hot_unplug(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    PCIDevice *pci_dev = NULL;
    PCIBus *bus;
    int32_t devfn;
    S390PCIBusDevice *pbdev = NULL;
    S390pciState *s = s390_get_phb();

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)) {
        error_setg(errp, "PCI bridge hot unplug currently not supported");
        return;
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        pci_dev = PCI_DEVICE(dev);

        QTAILQ_FOREACH(pbdev, &s->zpci_devs, link) {
            if (pbdev->pdev == pci_dev) {
                break;
            }
        }
        assert(pbdev != NULL);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_S390_PCI_DEVICE)) {
        pbdev = S390_PCI_DEVICE(dev);
        pci_dev = pbdev->pdev;
    }

    switch (pbdev->state) {
    case ZPCI_FS_RESERVED:
        goto out;
    case ZPCI_FS_STANDBY:
        break;
    default:
        s390_pci_generate_plug_event(HP_EVENT_DECONFIGURE_REQUEST,
                                     pbdev->fh, pbdev->fid);
        pbdev->release_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                            s390_pcihost_timer_cb,
                                            pbdev);
        timer_mod(pbdev->release_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + HOT_UNPLUG_TIMEOUT);
        return;
    }

    if (pbdev->release_timer && timer_pending(pbdev->release_timer)) {
        timer_del(pbdev->release_timer);
        timer_free(pbdev->release_timer);
        pbdev->release_timer = NULL;
    }

    s390_pci_generate_plug_event(HP_EVENT_STANDBY_TO_RESERVED,
                                 pbdev->fh, pbdev->fid);
    bus = pci_dev->bus;
    devfn = pci_dev->devfn;
    object_unparent(OBJECT(pci_dev));
    s390_pci_msix_free(pbdev);
    s390_pci_iommu_free(s, bus, devfn);
    pbdev->pdev = NULL;
    pbdev->state = ZPCI_FS_RESERVED;
out:
    pbdev->fid = 0;
    QTAILQ_REMOVE(&s->zpci_devs, pbdev, link);
    g_hash_table_remove(s->zpci_table, &pbdev->idx);
    object_unparent(OBJECT(pbdev));
}

static void s390_pci_enumerate_bridge(PCIBus *bus, PCIDevice *pdev,
                                      void *opaque)
{
    S390pciState *s = opaque;
    unsigned int primary = s->bus_no;
    unsigned int subordinate = 0xff;
    PCIBus *sec_bus = NULL;

    if ((pci_default_read_config(pdev, PCI_HEADER_TYPE, 1) !=
         PCI_HEADER_TYPE_BRIDGE)) {
        return;
    }

    (s->bus_no)++;
    pci_default_write_config(pdev, PCI_PRIMARY_BUS, primary, 1);
    pci_default_write_config(pdev, PCI_SECONDARY_BUS, s->bus_no, 1);
    pci_default_write_config(pdev, PCI_SUBORDINATE_BUS, s->bus_no, 1);

    sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));
    if (!sec_bus) {
        return;
    }

    pci_default_write_config(pdev, PCI_SUBORDINATE_BUS, subordinate, 1);
    pci_for_each_device(sec_bus, pci_bus_num(sec_bus),
                        s390_pci_enumerate_bridge, s);
    pci_default_write_config(pdev, PCI_SUBORDINATE_BUS, s->bus_no, 1);
}

static void s390_pcihost_reset(DeviceState *dev)
{
    S390pciState *s = S390_PCI_HOST_BRIDGE(dev);
    PCIBus *bus = s->parent_obj.bus;

    s->bus_no = 0;
    pci_for_each_device(bus, pci_bus_num(bus), s390_pci_enumerate_bridge, s);
}

static void s390_pcihost_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    dc->reset = s390_pcihost_reset;
    k->init = s390_pcihost_init;
    hc->plug = s390_pcihost_hot_plug;
    hc->unplug = s390_pcihost_hot_unplug;
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

    if (pbdev->summary_ind) {
        pci_dereg_irqs(pbdev);
    }
    if (pbdev->iommu->enabled) {
        pci_dereg_ioat(pbdev->iommu);
    }

    pbdev->fmb_addr = 0;
}

static void s390_pci_get_fid(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint32_t *ptr = qdev_get_prop_ptr(DEVICE(obj), prop);

    visit_type_uint32(v, name, ptr, errp);
}

static void s390_pci_set_fid(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    S390PCIBusDevice *zpci = S390_PCI_DEVICE(obj);
    Property *prop = opaque;
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_uint32(v, name, ptr, errp);
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
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_pci_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "zpci device";
    dc->reset = s390_pci_device_reset;
    dc->bus_type = TYPE_S390_PCI_BUS;
    dc->realize = s390_pci_device_realize;
    dc->props = s390_pci_device_properties;
}

static const TypeInfo s390_pci_device_info = {
    .name = TYPE_S390_PCI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(S390PCIBusDevice),
    .class_init = s390_pci_device_class_init,
};

static TypeInfo s390_pci_iommu_info = {
    .name = TYPE_S390_PCI_IOMMU,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(S390PCIIOMMU),
};

static void s390_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = s390_translate_iommu;
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
