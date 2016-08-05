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
#include "hw/pci/msi.h"
#include "qemu/error-report.h"

/* #define DEBUG_S390PCI_BUS */
#ifdef DEBUG_S390PCI_BUS
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "S390pci-bus: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static S390pciState *s390_get_phb(void)
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

S390PCIBusDevice *s390_pci_find_next_avail_dev(S390PCIBusDevice *pbdev)
{
    int idx = 0;
    S390PCIBusDevice *dev = NULL;
    S390pciState *s = s390_get_phb();

    if (pbdev) {
        idx = (pbdev->fh & FH_MASK_INDEX) + 1;
    }

    for (; idx < PCI_SLOT_MAX; idx++) {
        dev = s->pbdev[idx];
        if (dev && dev->state != ZPCI_FS_RESERVED) {
            return dev;
        }
    }

    return NULL;
}

S390PCIBusDevice *s390_pci_find_dev_by_fid(uint32_t fid)
{
    S390PCIBusDevice *pbdev;
    int i;
    S390pciState *s = s390_get_phb();

    for (i = 0; i < PCI_SLOT_MAX; i++) {
        pbdev = s->pbdev[i];
        if (pbdev && pbdev->fid == fid) {
            return pbdev;
        }
    }

    return NULL;
}

void s390_pci_sclp_configure(SCCB *sccb)
{
    PciCfgSccb *psccb = (PciCfgSccb *)sccb;
    S390PCIBusDevice *pbdev = s390_pci_find_dev_by_fid(be32_to_cpu(psccb->aid));
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
    S390PCIBusDevice *pbdev = s390_pci_find_dev_by_fid(be32_to_cpu(psccb->aid));
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
        if (pbdev->iommu_enabled) {
            pci_dereg_ioat(pbdev);
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

static S390PCIBusDevice *s390_pci_find_dev_by_uid(uint16_t uid)
{
    int i;
    S390PCIBusDevice *pbdev;
    S390pciState *s = s390_get_phb();

    for (i = 0; i < PCI_SLOT_MAX; i++) {
        pbdev = s->pbdev[i];
        if (!pbdev) {
            continue;
        }

        if (pbdev->uid == uid) {
            return pbdev;
        }
    }

    return NULL;
}

static S390PCIBusDevice *s390_pci_find_dev_by_target(const char *target)
{
    int i;
    S390PCIBusDevice *pbdev;
    S390pciState *s = s390_get_phb();

    if (!target) {
        return NULL;
    }

    for (i = 0; i < PCI_SLOT_MAX; i++) {
        pbdev = s->pbdev[i];
        if (!pbdev) {
            continue;
        }

        if (!strcmp(pbdev->target, target)) {
            return pbdev;
        }
    }

    return NULL;
}

S390PCIBusDevice *s390_pci_find_dev_by_idx(uint32_t idx)
{
    S390pciState *s = s390_get_phb();

    return s->pbdev[idx & FH_MASK_INDEX];
}

S390PCIBusDevice *s390_pci_find_dev_by_fh(uint32_t fh)
{
    S390pciState *s = s390_get_phb();
    S390PCIBusDevice *pbdev;

    pbdev = s->pbdev[fh & FH_MASK_INDEX];
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

    switch (pbdev->state) {
    case ZPCI_FS_ENABLED:
    case ZPCI_FS_BLOCKED:
        if (!pbdev->iommu_enabled) {
            return ret;
        }
        break;
    default:
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

    if (addr < pbdev->pba || addr > pbdev->pal) {
        return ret;
    }

    pte = s390_guest_io_table_walk(s390_pci_get_table_origin(pbdev->g_iota),
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

static const MemoryRegionIOMMUOps s390_iommu_ops = {
    .translate = s390_translate_iommu,
};

static AddressSpace *s390_pci_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    S390pciState *s = opaque;

    return &s->iommu[PCI_SLOT(devfn)]->as;
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
    uint32_t idx = data >> ZPCI_MSI_VEC_BITS;
    uint32_t vec = data & ZPCI_MSI_VEC_MASK;
    uint64_t ind_bit;
    uint32_t sum_bit;
    uint32_t e = 0;

    DPRINTF("write_msix data 0x%" PRIx64 " idx %d vec 0x%x\n", data, idx, vec);

    pbdev = s390_pci_find_dev_by_idx(idx);
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

void s390_pci_iommu_enable(S390PCIBusDevice *pbdev)
{
    memory_region_init_iommu(&pbdev->iommu_mr, OBJECT(&pbdev->iommu->mr),
                             &s390_iommu_ops, "iommu-s390", pbdev->pal + 1);
    memory_region_add_subregion(&pbdev->iommu->mr, 0, &pbdev->iommu_mr);
    pbdev->iommu_enabled = true;
}

void s390_pci_iommu_disable(S390PCIBusDevice *pbdev)
{
    memory_region_del_subregion(&pbdev->iommu->mr, &pbdev->iommu_mr);
    object_unparent(OBJECT(&pbdev->iommu_mr));
    pbdev->iommu_enabled = false;
}

static void s390_pcihost_init_as(S390pciState *s)
{
    int i;
    S390PCIIOMMU *iommu;

    for (i = 0; i < PCI_SLOT_MAX; i++) {
        iommu = g_malloc0(sizeof(S390PCIIOMMU));
        memory_region_init(&iommu->mr, OBJECT(s),
                           "iommu-root-s390", UINT64_MAX);
        address_space_init(&iommu->as, &iommu->mr, "iommu-pci");

        s->iommu[i] = iommu;
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

    s->bus = S390_PCI_BUS(qbus_create(TYPE_S390_PCI_BUS, DEVICE(s), NULL));
    qbus_set_hotplug_handler(BUS(s->bus), DEVICE(s), NULL);

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

static S390PCIBusDevice *s390_pci_device_new(const char *target)
{
    DeviceState *dev = NULL;
    S390pciState *s = s390_get_phb();

    dev = qdev_try_create(BUS(s->bus), TYPE_S390_PCI_DEVICE);
    if (!dev) {
        return NULL;
    }

    qdev_prop_set_string(dev, "target", target);
    qdev_init_nofail(dev);

    return S390_PCI_DEVICE(dev);
}

static void s390_pcihost_hot_plug(HotplugHandler *hotplug_dev,
                                  DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = NULL;
    S390PCIBusDevice *pbdev = NULL;
    S390pciState *s = s390_get_phb();

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        pdev = PCI_DEVICE(dev);

        if (!dev->id) {
            /* In the case the PCI device does not define an id */
            /* we generate one based on the PCI address         */
            dev->id = g_strdup_printf("auto_%02x:%02x.%01x",
                                      pci_bus_num(pdev->bus),
                                      PCI_SLOT(pdev->devfn),
                                      PCI_FUNC(pdev->devfn));
        }

        pbdev = s390_pci_find_dev_by_target(dev->id);
        if (!pbdev) {
            pbdev = s390_pci_device_new(dev->id);
            if (!pbdev) {
                error_setg(errp, "create zpci device failed");
            }
        }

        if (object_dynamic_cast(OBJECT(dev), "vfio-pci")) {
            pbdev->fh |= FH_SHM_VFIO;
        } else {
            pbdev->fh |= FH_SHM_EMUL;
        }

        pbdev->pdev = pdev;
        pbdev->iommu = s->iommu[PCI_SLOT(pdev->devfn)];
        pbdev->state = ZPCI_FS_STANDBY;
        s390_pcihost_setup_msix(pbdev);

        if (dev->hotplugged) {
            s390_pci_generate_plug_event(HP_EVENT_RESERVED_TO_STANDBY,
                                         pbdev->fh, pbdev->fid);
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_S390_PCI_DEVICE)) {
        int idx;

        pbdev = S390_PCI_DEVICE(dev);
        for (idx = 0; idx < PCI_SLOT_MAX; idx++) {
            if (!s->pbdev[idx]) {
                s->pbdev[idx] = pbdev;
                pbdev->fh = idx;
                return;
            }
        }

        error_setg(errp, "no slot for plugging zpci device");
    }
}

static void s390_pcihost_timer_cb(void *opaque)
{
    S390PCIBusDevice *pbdev = opaque;

    if (pbdev->summary_ind) {
        pci_dereg_irqs(pbdev);
    }
    if (pbdev->iommu_enabled) {
        pci_dereg_ioat(pbdev);
    }

    pbdev->state = ZPCI_FS_STANDBY;
    s390_pci_generate_plug_event(HP_EVENT_CONFIGURED_TO_STBRES,
                                 pbdev->fh, pbdev->fid);
    qdev_unplug(DEVICE(pbdev), NULL);
}

static void s390_pcihost_hot_unplug(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    int i;
    PCIDevice *pci_dev = NULL;
    S390PCIBusDevice *pbdev = NULL;
    S390pciState *s = s390_get_phb();

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        pci_dev = PCI_DEVICE(dev);

        for (i = 0 ; i < PCI_SLOT_MAX; i++) {
            if (s->pbdev[i] && s->pbdev[i]->pdev == pci_dev) {
                pbdev = s->pbdev[i];
                break;
            }
        }

        if (!pbdev) {
            object_unparent(OBJECT(pci_dev));
            return;
        }
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
    object_unparent(OBJECT(pci_dev));
    pbdev->pdev = NULL;
    pbdev->state = ZPCI_FS_RESERVED;
out:
    pbdev->fid = 0;
    s->pbdev[pbdev->fh & FH_MASK_INDEX] = NULL;
    object_unparent(OBJECT(pbdev));
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

static uint16_t s390_pci_generate_uid(void)
{
    uint16_t uid = 0;

    do {
        uid++;
        if (!s390_pci_find_dev_by_uid(uid)) {
            return uid;
        }
    } while (uid < ZPCI_MAX_UID);

    return UID_UNDEFINED;
}

static uint32_t s390_pci_generate_fid(Error **errp)
{
    uint32_t fid = 0;

    while (fid <= ZPCI_MAX_FID) {
        if (!s390_pci_find_dev_by_fid(fid)) {
            return fid;
        }

        if (fid == ZPCI_MAX_FID) {
            break;
        }

        fid++;
    }

    error_setg(errp, "no free fid could be found");
    return 0;
}

static void s390_pci_device_realize(DeviceState *dev, Error **errp)
{
    S390PCIBusDevice *zpci = S390_PCI_DEVICE(dev);

    if (!zpci->target) {
        error_setg(errp, "target must be defined");
        return;
    }

    if (s390_pci_find_dev_by_target(zpci->target)) {
        error_setg(errp, "target %s already has an associated zpci device",
                   zpci->target);
        return;
    }

    if (zpci->uid == UID_UNDEFINED) {
        zpci->uid = s390_pci_generate_uid();
        if (!zpci->uid) {
            error_setg(errp, "no free uid could be found");
            return;
        }
    } else if (s390_pci_find_dev_by_uid(zpci->uid)) {
        error_setg(errp, "uid %u already in use", zpci->uid);
        return;
    }

    if (!zpci->fid_defined) {
        Error *local_error = NULL;

        zpci->fid = s390_pci_generate_fid(&local_error);
        if (local_error) {
            error_propagate(errp, local_error);
            return;
        }
    } else if (s390_pci_find_dev_by_fid(zpci->fid)) {
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
    if (pbdev->iommu_enabled) {
        pci_dereg_ioat(pbdev);
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

static PropertyInfo s390_pci_fid_propinfo = {
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

static void s390_pci_register_types(void)
{
    type_register_static(&s390_pcihost_info);
    type_register_static(&s390_pcibus_info);
    type_register_static(&s390_pci_device_info);
}

type_init(s390_pci_register_types)
