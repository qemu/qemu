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
#include "s390-pci-bus.h"
#include <hw/pci/pci_bus.h>
#include <hw/pci/msi.h>
#include <qemu/error-report.h>

/* #define DEBUG_S390PCI_BUS */
#ifdef DEBUG_S390PCI_BUS
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "S390pci-bus: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

int chsc_sei_nt2_get_event(void *res)
{
    ChscSeiNt2Res *nt2_res = (ChscSeiNt2Res *)res;
    PciCcdfAvail *accdf;
    PciCcdfErr *eccdf;
    int rc = 1;
    SeiContainer *sei_cont;
    S390pciState *s = S390_PCI_HOST_BRIDGE(
        object_resolve_path(TYPE_S390_PCI_HOST_BRIDGE, NULL));

    if (!s) {
        return rc;
    }

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
    S390pciState *s = S390_PCI_HOST_BRIDGE(
        object_resolve_path(TYPE_S390_PCI_HOST_BRIDGE, NULL));

    if (!s) {
        return 0;
    }

    return !QTAILQ_EMPTY(&s->pending_sei);
}

S390PCIBusDevice *s390_pci_find_dev_by_fid(uint32_t fid)
{
    S390PCIBusDevice *pbdev;
    int i;
    S390pciState *s = S390_PCI_HOST_BRIDGE(
        object_resolve_path(TYPE_S390_PCI_HOST_BRIDGE, NULL));

    if (!s) {
        return NULL;
    }

    for (i = 0; i < PCI_SLOT_MAX; i++) {
        pbdev = &s->pbdev[i];
        if ((pbdev->fh != 0) && (pbdev->fid == fid)) {
            return pbdev;
        }
    }

    return NULL;
}

void s390_pci_sclp_configure(int configure, SCCB *sccb)
{
    PciCfgSccb *psccb = (PciCfgSccb *)sccb;
    S390PCIBusDevice *pbdev = s390_pci_find_dev_by_fid(be32_to_cpu(psccb->aid));
    uint16_t rc;

    if (pbdev) {
        if ((configure == 1 && pbdev->configured == true) ||
            (configure == 0 && pbdev->configured == false)) {
            rc = SCLP_RC_NO_ACTION_REQUIRED;
        } else {
            pbdev->configured = !pbdev->configured;
            rc = SCLP_RC_NORMAL_COMPLETION;
        }
    } else {
        DPRINTF("sclp config %d no dev found\n", configure);
        rc = SCLP_RC_ADAPTER_ID_NOT_RECOGNIZED;
    }

    psccb->header.response_code = cpu_to_be16(rc);
}

static uint32_t s390_pci_get_pfid(PCIDevice *pdev)
{
    return PCI_SLOT(pdev->devfn);
}

static uint32_t s390_pci_get_pfh(PCIDevice *pdev)
{
    return PCI_SLOT(pdev->devfn) | FH_VIRT;
}

S390PCIBusDevice *s390_pci_find_dev_by_idx(uint32_t idx)
{
    S390PCIBusDevice *pbdev;
    int i;
    int j = 0;
    S390pciState *s = S390_PCI_HOST_BRIDGE(
        object_resolve_path(TYPE_S390_PCI_HOST_BRIDGE, NULL));

    if (!s) {
        return NULL;
    }

    for (i = 0; i < PCI_SLOT_MAX; i++) {
        pbdev = &s->pbdev[i];

        if (pbdev->fh == 0) {
            continue;
        }

        if (j == idx) {
            return pbdev;
        }
        j++;
    }

    return NULL;
}

S390PCIBusDevice *s390_pci_find_dev_by_fh(uint32_t fh)
{
    S390PCIBusDevice *pbdev;
    int i;
    S390pciState *s = S390_PCI_HOST_BRIDGE(
        object_resolve_path(TYPE_S390_PCI_HOST_BRIDGE, NULL));

    if (!s || !fh) {
        return NULL;
    }

    for (i = 0; i < PCI_SLOT_MAX; i++) {
        pbdev = &s->pbdev[i];
        if (pbdev->fh == fh) {
            return pbdev;
        }
    }

    return NULL;
}

static void s390_pci_generate_event(uint8_t cc, uint16_t pec, uint32_t fh,
                                    uint32_t fid, uint64_t faddr, uint32_t e)
{
    SeiContainer *sei_cont;
    S390pciState *s = S390_PCI_HOST_BRIDGE(
        object_resolve_path(TYPE_S390_PCI_HOST_BRIDGE, NULL));

    if (!s) {
        return;
    }

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

static void s390_pci_generate_error_event(uint16_t pec, uint32_t fh,
                                          uint32_t fid, uint64_t faddr,
                                          uint32_t e)
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

static IOMMUTLBEntry s390_translate_iommu(MemoryRegion *iommu, hwaddr addr,
                                          bool is_write)
{
    uint64_t pte;
    uint32_t flags;
    S390PCIBusDevice *pbdev = container_of(iommu, S390PCIBusDevice, iommu_mr);
    S390pciState *s;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = 0,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    if (!pbdev->configured || !pbdev->pdev || !(pbdev->fh & FH_ENABLED)) {
        return ret;
    }

    DPRINTF("iommu trans addr 0x%" PRIx64 "\n", addr);

    s = S390_PCI_HOST_BRIDGE(pci_device_root_bus(pbdev->pdev)->qbus.parent);
    /* s390 does not have an APIC mapped to main storage so we use
     * a separate AddressSpace only for msix notifications
     */
    if (addr == ZPCI_MSI_ADDR) {
        ret.target_as = &s->msix_notify_as;
        ret.iova = addr;
        ret.translated_addr = addr;
        ret.addr_mask = 0xfff;
        ret.perm = IOMMU_RW;
        return ret;
    }

    if (!pbdev->g_iota) {
        pbdev->error_state = true;
        pbdev->lgstg_blocked = true;
        s390_pci_generate_error_event(ERR_EVENT_INVALAS, pbdev->fh, pbdev->fid,
                                      addr, 0);
        return ret;
    }

    if (addr < pbdev->pba || addr > pbdev->pal) {
        pbdev->error_state = true;
        pbdev->lgstg_blocked = true;
        s390_pci_generate_error_event(ERR_EVENT_OORANGE, pbdev->fh, pbdev->fid,
                                      addr, 0);
        return ret;
    }

    pte = s390_guest_io_table_walk(s390_pci_get_table_origin(pbdev->g_iota),
                                   addr);

    if (!pte) {
        pbdev->error_state = true;
        pbdev->lgstg_blocked = true;
        s390_pci_generate_error_event(ERR_EVENT_SERR, pbdev->fh, pbdev->fid,
                                      addr, ERR_EVENT_Q_BIT);
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

static const MemoryRegionIOMMUOps s390_iommu_ops = {
    .translate = s390_translate_iommu,
};

static AddressSpace *s390_pci_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    S390pciState *s = opaque;

    return &s->pbdev[PCI_SLOT(devfn)].as;
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
    S390PCIBusDevice *pbdev;
    uint32_t io_int_word;
    uint32_t fid = data >> ZPCI_MSI_VEC_BITS;
    uint32_t vec = data & ZPCI_MSI_VEC_MASK;
    uint64_t ind_bit;
    uint32_t sum_bit;
    uint32_t e = 0;

    DPRINTF("write_msix data 0x%" PRIx64 " fid %d vec 0x%x\n", data, fid, vec);

    pbdev = s390_pci_find_dev_by_fid(fid);
    if (!pbdev) {
        e |= (vec << ERR_EVENT_MVN_OFFSET);
        s390_pci_generate_error_event(ERR_EVENT_NOMSI, 0, fid, addr, e);
        return;
    }

    if (!(pbdev->fh & FH_ENABLED)) {
        return;
    }

    ind_bit = pbdev->routes.adapter.ind_offset;
    sum_bit = pbdev->routes.adapter.summary_offset;

    set_ind_atomic(pbdev->routes.adapter.ind_addr + (ind_bit + vec) / 8,
                   0x80 >> ((ind_bit + vec) % 8));
    if (!set_ind_atomic(pbdev->routes.adapter.summary_addr + sum_bit / 8,
                                       0x80 >> (sum_bit % 8))) {
        io_int_word = (pbdev->isc << 27) | IO_INT_WORD_AI;
        s390_io_interrupt(0, 0, 0, io_int_word);
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

void s390_pcihost_iommu_configure(S390PCIBusDevice *pbdev, bool enable)
{
    pbdev->configured = false;

    if (enable) {
        uint64_t size = pbdev->pal - pbdev->pba + 1;
        memory_region_init_iommu(&pbdev->iommu_mr, OBJECT(&pbdev->mr),
                                 &s390_iommu_ops, "iommu-s390", size);
        memory_region_add_subregion(&pbdev->mr, pbdev->pba, &pbdev->iommu_mr);
    } else {
        memory_region_del_subregion(&pbdev->mr, &pbdev->iommu_mr);
    }

    pbdev->configured = true;
}

static void s390_pcihost_init_as(S390pciState *s)
{
    int i;
    S390PCIBusDevice *pbdev;

    for (i = 0; i < PCI_SLOT_MAX; i++) {
        pbdev = &s->pbdev[i];
        memory_region_init(&pbdev->mr, OBJECT(s),
                           "iommu-root-s390", UINT64_MAX);
        address_space_init(&pbdev->as, &pbdev->mr, "iommu-pci");
    }

    memory_region_init_io(&s->msix_notify_mr, OBJECT(s),
                          &s390_msi_ctrl_ops, s, "msix-s390", UINT64_MAX);
    address_space_init(&s->msix_notify_as, &s->msix_notify_mr, "msix-pci");
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
    s390_pcihost_init_as(s);
    pci_setup_iommu(b, s390_pci_dma_iommu, s);

    bus = BUS(b);
    qbus_set_hotplug_handler(bus, DEVICE(dev), NULL);
    phb->bus = b;
    QTAILQ_INIT(&s->pending_sei);
    return 0;
}

static int s390_pcihost_setup_msix(S390PCIBusDevice *pbdev)
{
    uint8_t pos;
    uint16_t ctrl;
    uint32_t table, pba;

    pos = pci_find_capability(pbdev->pdev, PCI_CAP_ID_MSIX);
    if (!pos) {
        pbdev->msix.available = false;
        return 0;
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
    return 0;
}

static void s390_pcihost_hot_plug(HotplugHandler *hotplug_dev,
                                  DeviceState *dev, Error **errp)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    S390PCIBusDevice *pbdev;
    S390pciState *s = S390_PCI_HOST_BRIDGE(pci_device_root_bus(pci_dev)
                                           ->qbus.parent);

    pbdev = &s->pbdev[PCI_SLOT(pci_dev->devfn)];

    pbdev->fid = s390_pci_get_pfid(pci_dev);
    pbdev->pdev = pci_dev;
    pbdev->configured = true;
    pbdev->fh = s390_pci_get_pfh(pci_dev);

    s390_pcihost_setup_msix(pbdev);

    if (dev->hotplugged) {
        s390_pci_generate_plug_event(HP_EVENT_RESERVED_TO_STANDBY,
                                     pbdev->fh, pbdev->fid);
        s390_pci_generate_plug_event(HP_EVENT_TO_CONFIGURED,
                                     pbdev->fh, pbdev->fid);
    }
}

static void s390_pcihost_hot_unplug(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    S390pciState *s = S390_PCI_HOST_BRIDGE(pci_device_root_bus(pci_dev)
                                           ->qbus.parent);
    S390PCIBusDevice *pbdev = &s->pbdev[PCI_SLOT(pci_dev->devfn)];

    if (pbdev->configured) {
        pbdev->configured = false;
        s390_pci_generate_plug_event(HP_EVENT_CONFIGURED_TO_STBRES,
                                     pbdev->fh, pbdev->fid);
    }

    s390_pci_generate_plug_event(HP_EVENT_STANDBY_TO_RESERVED,
                                 pbdev->fh, pbdev->fid);
    pbdev->fh = 0;
    pbdev->fid = 0;
    pbdev->pdev = NULL;
    object_unparent(OBJECT(pci_dev));
}

static void s390_pcihost_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    dc->cannot_instantiate_with_device_add_yet = true;
    k->init = s390_pcihost_init;
    hc->plug = s390_pcihost_hot_plug;
    hc->unplug = s390_pcihost_hot_unplug;
    msi_supported = true;
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

static void s390_pci_register_types(void)
{
    type_register_static(&s390_pcihost_info);
}

type_init(s390_pci_register_types)
