/*
 * Copyright (c) 2007, Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Jiang Yunhong <yunhong.jiang@intel.com>
 *
 * This file implements direct PCI assignment to a HVM guest
 */

#include <sys/mman.h>

#include "hw/xen/xen_backend.h"
#include "xen_pt.h"
#include "hw/i386/apic-msidef.h"


#define XEN_PT_AUTO_ASSIGN -1

/* shift count for gflags */
#define XEN_PT_GFLAGS_SHIFT_DEST_ID        0
#define XEN_PT_GFLAGS_SHIFT_RH             8
#define XEN_PT_GFLAGS_SHIFT_DM             9
#define XEN_PT_GFLAGSSHIFT_DELIV_MODE     12
#define XEN_PT_GFLAGSSHIFT_TRG_MODE       15


/*
 * Helpers
 */

static inline uint8_t msi_vector(uint32_t data)
{
    return (data & MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT;
}

static inline uint8_t msi_dest_id(uint32_t addr)
{
    return (addr & MSI_ADDR_DEST_ID_MASK) >> MSI_ADDR_DEST_ID_SHIFT;
}

static inline uint32_t msi_ext_dest_id(uint32_t addr_hi)
{
    return addr_hi & 0xffffff00;
}

static uint32_t msi_gflags(uint32_t data, uint64_t addr)
{
    uint32_t result = 0;
    int rh, dm, dest_id, deliv_mode, trig_mode;

    rh = (addr >> MSI_ADDR_REDIRECTION_SHIFT) & 0x1;
    dm = (addr >> MSI_ADDR_DEST_MODE_SHIFT) & 0x1;
    dest_id = msi_dest_id(addr);
    deliv_mode = (data >> MSI_DATA_DELIVERY_MODE_SHIFT) & 0x7;
    trig_mode = (data >> MSI_DATA_TRIGGER_SHIFT) & 0x1;

    result = dest_id | (rh << XEN_PT_GFLAGS_SHIFT_RH)
        | (dm << XEN_PT_GFLAGS_SHIFT_DM)
        | (deliv_mode << XEN_PT_GFLAGSSHIFT_DELIV_MODE)
        | (trig_mode << XEN_PT_GFLAGSSHIFT_TRG_MODE);

    return result;
}

static inline uint64_t msi_addr64(XenPTMSI *msi)
{
    return (uint64_t)msi->addr_hi << 32 | msi->addr_lo;
}

static int msi_msix_enable(XenPCIPassthroughState *s,
                           uint32_t address,
                           uint16_t flag,
                           bool enable)
{
    uint16_t val = 0;

    if (!address) {
        return -1;
    }

    xen_host_pci_get_word(&s->real_device, address, &val);
    if (enable) {
        val |= flag;
    } else {
        val &= ~flag;
    }
    xen_host_pci_set_word(&s->real_device, address, val);
    return 0;
}

static int msi_msix_setup(XenPCIPassthroughState *s,
                          uint64_t addr,
                          uint32_t data,
                          int *ppirq,
                          bool is_msix,
                          int msix_entry,
                          bool is_not_mapped)
{
    uint8_t gvec = msi_vector(data);
    int rc = 0;

    assert((!is_msix && msix_entry == 0) || is_msix);

    if (gvec == 0) {
        /* if gvec is 0, the guest is asking for a particular pirq that
         * is passed as dest_id */
        *ppirq = msi_ext_dest_id(addr >> 32) | msi_dest_id(addr);
        if (!*ppirq) {
            /* this probably identifies an misconfiguration of the guest,
             * try the emulated path */
            *ppirq = XEN_PT_UNASSIGNED_PIRQ;
        } else {
            XEN_PT_LOG(&s->dev, "requested pirq %d for MSI%s"
                       " (vec: %#x, entry: %#x)\n",
                       *ppirq, is_msix ? "-X" : "", gvec, msix_entry);
        }
    }

    if (is_not_mapped) {
        uint64_t table_base = 0;

        if (is_msix) {
            table_base = s->msix->table_base;
        }

        rc = xc_physdev_map_pirq_msi(xen_xc, xen_domid, XEN_PT_AUTO_ASSIGN,
                                     ppirq, PCI_DEVFN(s->real_device.dev,
                                                      s->real_device.func),
                                     s->real_device.bus,
                                     msix_entry, table_base);
        if (rc) {
            XEN_PT_ERR(&s->dev,
                       "Mapping of MSI%s (rc: %i, vec: %#x, entry %#x)\n",
                       is_msix ? "-X" : "", rc, gvec, msix_entry);
            return rc;
        }
    }

    return 0;
}
static int msi_msix_update(XenPCIPassthroughState *s,
                           uint64_t addr,
                           uint32_t data,
                           int pirq,
                           bool is_msix,
                           int msix_entry,
                           int *old_pirq)
{
    PCIDevice *d = &s->dev;
    uint8_t gvec = msi_vector(data);
    uint32_t gflags = msi_gflags(data, addr);
    int rc = 0;
    uint64_t table_addr = 0;

    XEN_PT_LOG(d, "Updating MSI%s with pirq %d gvec %#x gflags %#x"
               " (entry: %#x)\n",
               is_msix ? "-X" : "", pirq, gvec, gflags, msix_entry);

    if (is_msix) {
        table_addr = s->msix->mmio_base_addr;
    }

    rc = xc_domain_update_msi_irq(xen_xc, xen_domid, gvec,
                                  pirq, gflags, table_addr);

    if (rc) {
        XEN_PT_ERR(d, "Updating of MSI%s failed. (rc: %d)\n",
                   is_msix ? "-X" : "", rc);

        if (xc_physdev_unmap_pirq(xen_xc, xen_domid, *old_pirq)) {
            XEN_PT_ERR(d, "Unmapping of MSI%s pirq %d failed.\n",
                       is_msix ? "-X" : "", *old_pirq);
        }
        *old_pirq = XEN_PT_UNASSIGNED_PIRQ;
    }
    return rc;
}

static int msi_msix_disable(XenPCIPassthroughState *s,
                            uint64_t addr,
                            uint32_t data,
                            int pirq,
                            bool is_msix,
                            bool is_binded)
{
    PCIDevice *d = &s->dev;
    uint8_t gvec = msi_vector(data);
    uint32_t gflags = msi_gflags(data, addr);
    int rc = 0;

    if (pirq == XEN_PT_UNASSIGNED_PIRQ) {
        return 0;
    }

    if (is_binded) {
        XEN_PT_LOG(d, "Unbind MSI%s with pirq %d, gvec %#x\n",
                   is_msix ? "-X" : "", pirq, gvec);
        rc = xc_domain_unbind_msi_irq(xen_xc, xen_domid, gvec, pirq, gflags);
        if (rc) {
            XEN_PT_ERR(d, "Unbinding of MSI%s failed. (pirq: %d, gvec: %#x)\n",
                       is_msix ? "-X" : "", pirq, gvec);
            return rc;
        }
    }

    XEN_PT_LOG(d, "Unmap MSI%s pirq %d\n", is_msix ? "-X" : "", pirq);
    rc = xc_physdev_unmap_pirq(xen_xc, xen_domid, pirq);
    if (rc) {
        XEN_PT_ERR(d, "Unmapping of MSI%s pirq %d failed. (rc: %i)\n",
                   is_msix ? "-X" : "", pirq, rc);
        return rc;
    }

    return 0;
}

/*
 * MSI virtualization functions
 */

int xen_pt_msi_set_enable(XenPCIPassthroughState *s, bool enable)
{
    XEN_PT_LOG(&s->dev, "%s MSI.\n", enable ? "enabling" : "disabling");

    if (!s->msi) {
        return -1;
    }

    return msi_msix_enable(s, s->msi->ctrl_offset, PCI_MSI_FLAGS_ENABLE,
                           enable);
}

/* setup physical msi, but don't enable it */
int xen_pt_msi_setup(XenPCIPassthroughState *s)
{
    int pirq = XEN_PT_UNASSIGNED_PIRQ;
    int rc = 0;
    XenPTMSI *msi = s->msi;

    if (msi->initialized) {
        XEN_PT_ERR(&s->dev,
                   "Setup physical MSI when it has been properly initialized.\n");
        return -1;
    }

    rc = msi_msix_setup(s, msi_addr64(msi), msi->data, &pirq, false, 0, true);
    if (rc) {
        return rc;
    }

    if (pirq < 0) {
        XEN_PT_ERR(&s->dev, "Invalid pirq number: %d.\n", pirq);
        return -1;
    }

    msi->pirq = pirq;
    XEN_PT_LOG(&s->dev, "MSI mapped with pirq %d.\n", pirq);

    return 0;
}

int xen_pt_msi_update(XenPCIPassthroughState *s)
{
    XenPTMSI *msi = s->msi;
    return msi_msix_update(s, msi_addr64(msi), msi->data, msi->pirq,
                           false, 0, &msi->pirq);
}

void xen_pt_msi_disable(XenPCIPassthroughState *s)
{
    XenPTMSI *msi = s->msi;

    if (!msi) {
        return;
    }

    xen_pt_msi_set_enable(s, false);

    msi_msix_disable(s, msi_addr64(msi), msi->data, msi->pirq, false,
                     msi->initialized);

    /* clear msi info */
    msi->flags &= ~PCI_MSI_FLAGS_ENABLE;
    msi->initialized = false;
    msi->mapped = false;
    msi->pirq = XEN_PT_UNASSIGNED_PIRQ;
}

/*
 * MSI-X virtualization functions
 */

static int msix_set_enable(XenPCIPassthroughState *s, bool enabled)
{
    XEN_PT_LOG(&s->dev, "%s MSI-X.\n", enabled ? "enabling" : "disabling");

    if (!s->msix) {
        return -1;
    }

    return msi_msix_enable(s, s->msix->ctrl_offset, PCI_MSIX_FLAGS_ENABLE,
                           enabled);
}

static int xen_pt_msix_update_one(XenPCIPassthroughState *s, int entry_nr)
{
    XenPTMSIXEntry *entry = NULL;
    int pirq;
    int rc;

    if (entry_nr < 0 || entry_nr >= s->msix->total_entries) {
        return -EINVAL;
    }

    entry = &s->msix->msix_entry[entry_nr];

    if (!entry->updated) {
        return 0;
    }

    pirq = entry->pirq;

    rc = msi_msix_setup(s, entry->addr, entry->data, &pirq, true, entry_nr,
                        entry->pirq == XEN_PT_UNASSIGNED_PIRQ);
    if (rc) {
        return rc;
    }
    if (entry->pirq == XEN_PT_UNASSIGNED_PIRQ) {
        entry->pirq = pirq;
    }

    rc = msi_msix_update(s, entry->addr, entry->data, pirq, true,
                         entry_nr, &entry->pirq);

    if (!rc) {
        entry->updated = false;
    }

    return rc;
}

int xen_pt_msix_update(XenPCIPassthroughState *s)
{
    XenPTMSIX *msix = s->msix;
    int i;

    for (i = 0; i < msix->total_entries; i++) {
        xen_pt_msix_update_one(s, i);
    }

    return 0;
}

void xen_pt_msix_disable(XenPCIPassthroughState *s)
{
    int i = 0;

    msix_set_enable(s, false);

    for (i = 0; i < s->msix->total_entries; i++) {
        XenPTMSIXEntry *entry = &s->msix->msix_entry[i];

        msi_msix_disable(s, entry->addr, entry->data, entry->pirq, true, true);

        /* clear MSI-X info */
        entry->pirq = XEN_PT_UNASSIGNED_PIRQ;
        entry->updated = false;
    }
}

int xen_pt_msix_update_remap(XenPCIPassthroughState *s, int bar_index)
{
    XenPTMSIXEntry *entry;
    int i, ret;

    if (!(s->msix && s->msix->bar_index == bar_index)) {
        return 0;
    }

    for (i = 0; i < s->msix->total_entries; i++) {
        entry = &s->msix->msix_entry[i];
        if (entry->pirq != XEN_PT_UNASSIGNED_PIRQ) {
            ret = xc_domain_unbind_pt_irq(xen_xc, xen_domid, entry->pirq,
                                          PT_IRQ_TYPE_MSI, 0, 0, 0, 0);
            if (ret) {
                XEN_PT_ERR(&s->dev, "unbind MSI-X entry %d failed\n",
                           entry->pirq);
            }
            entry->updated = true;
        }
    }
    return xen_pt_msix_update(s);
}

static uint32_t get_entry_value(XenPTMSIXEntry *e, int offset)
{
    switch (offset) {
    case PCI_MSIX_ENTRY_LOWER_ADDR:
        return e->addr & UINT32_MAX;
    case PCI_MSIX_ENTRY_UPPER_ADDR:
        return e->addr >> 32;
    case PCI_MSIX_ENTRY_DATA:
        return e->data;
    case PCI_MSIX_ENTRY_VECTOR_CTRL:
        return e->vector_ctrl;
    default:
        return 0;
    }
}

static void set_entry_value(XenPTMSIXEntry *e, int offset, uint32_t val)
{
    switch (offset) {
    case PCI_MSIX_ENTRY_LOWER_ADDR:
        e->addr = (e->addr & ((uint64_t)UINT32_MAX << 32)) | val;
        break;
    case PCI_MSIX_ENTRY_UPPER_ADDR:
        e->addr = (uint64_t)val << 32 | (e->addr & UINT32_MAX);
        break;
    case PCI_MSIX_ENTRY_DATA:
        e->data = val;
        break;
    case PCI_MSIX_ENTRY_VECTOR_CTRL:
        e->vector_ctrl = val;
        break;
    }
}

static void pci_msix_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    XenPCIPassthroughState *s = opaque;
    XenPTMSIX *msix = s->msix;
    XenPTMSIXEntry *entry;
    int entry_nr, offset;

    entry_nr = addr / PCI_MSIX_ENTRY_SIZE;
    if (entry_nr < 0 || entry_nr >= msix->total_entries) {
        XEN_PT_ERR(&s->dev, "asked MSI-X entry '%i' invalid!\n", entry_nr);
        return;
    }
    entry = &msix->msix_entry[entry_nr];
    offset = addr % PCI_MSIX_ENTRY_SIZE;

    if (offset != PCI_MSIX_ENTRY_VECTOR_CTRL) {
        const volatile uint32_t *vec_ctrl;

        if (get_entry_value(entry, offset) == val
            && entry->pirq != XEN_PT_UNASSIGNED_PIRQ) {
            return;
        }

        /*
         * If Xen intercepts the mask bit access, entry->vec_ctrl may not be
         * up-to-date. Read from hardware directly.
         */
        vec_ctrl = s->msix->phys_iomem_base + entry_nr * PCI_MSIX_ENTRY_SIZE
            + PCI_MSIX_ENTRY_VECTOR_CTRL;

        if (msix->enabled && !(*vec_ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT)) {
            XEN_PT_ERR(&s->dev, "Can't update msix entry %d since MSI-X is"
                       " already enabled.\n", entry_nr);
            return;
        }

        entry->updated = true;
    }

    set_entry_value(entry, offset, val);

    if (offset == PCI_MSIX_ENTRY_VECTOR_CTRL) {
        if (msix->enabled && !(val & PCI_MSIX_ENTRY_CTRL_MASKBIT)) {
            xen_pt_msix_update_one(s, entry_nr);
        }
    }
}

static uint64_t pci_msix_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    XenPCIPassthroughState *s = opaque;
    XenPTMSIX *msix = s->msix;
    int entry_nr, offset;

    entry_nr = addr / PCI_MSIX_ENTRY_SIZE;
    if (entry_nr < 0) {
        XEN_PT_ERR(&s->dev, "asked MSI-X entry '%i' invalid!\n", entry_nr);
        return 0;
    }

    offset = addr % PCI_MSIX_ENTRY_SIZE;

    if (addr < msix->total_entries * PCI_MSIX_ENTRY_SIZE) {
        return get_entry_value(&msix->msix_entry[entry_nr], offset);
    } else {
        /* Pending Bit Array (PBA) */
        return *(uint32_t *)(msix->phys_iomem_base + addr);
    }
}

static const MemoryRegionOps pci_msix_ops = {
    .read = pci_msix_read,
    .write = pci_msix_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

int xen_pt_msix_init(XenPCIPassthroughState *s, uint32_t base)
{
    uint8_t id = 0;
    uint16_t control = 0;
    uint32_t table_off = 0;
    int i, total_entries, bar_index;
    XenHostPCIDevice *hd = &s->real_device;
    PCIDevice *d = &s->dev;
    int fd = -1;
    XenPTMSIX *msix = NULL;
    int rc = 0;

    rc = xen_host_pci_get_byte(hd, base + PCI_CAP_LIST_ID, &id);
    if (rc) {
        return rc;
    }

    if (id != PCI_CAP_ID_MSIX) {
        XEN_PT_ERR(d, "Invalid id %#x base %#x\n", id, base);
        return -1;
    }

    xen_host_pci_get_word(hd, base + PCI_MSIX_FLAGS, &control);
    total_entries = control & PCI_MSIX_FLAGS_QSIZE;
    total_entries += 1;

    s->msix = g_malloc0(sizeof (XenPTMSIX)
                        + total_entries * sizeof (XenPTMSIXEntry));
    msix = s->msix;

    msix->total_entries = total_entries;
    for (i = 0; i < total_entries; i++) {
        msix->msix_entry[i].pirq = XEN_PT_UNASSIGNED_PIRQ;
    }

    memory_region_init_io(&msix->mmio, OBJECT(s), &pci_msix_ops,
                          s, "xen-pci-pt-msix",
                          (total_entries * PCI_MSIX_ENTRY_SIZE
                           + XC_PAGE_SIZE - 1)
                          & XC_PAGE_MASK);

    xen_host_pci_get_long(hd, base + PCI_MSIX_TABLE, &table_off);
    bar_index = msix->bar_index = table_off & PCI_MSIX_FLAGS_BIRMASK;
    table_off = table_off & ~PCI_MSIX_FLAGS_BIRMASK;
    msix->table_base = s->real_device.io_regions[bar_index].base_addr;
    XEN_PT_LOG(d, "get MSI-X table BAR base 0x%"PRIx64"\n", msix->table_base);

    fd = open("/dev/mem", O_RDWR);
    if (fd == -1) {
        rc = -errno;
        XEN_PT_ERR(d, "Can't open /dev/mem: %s\n", strerror(errno));
        goto error_out;
    }
    XEN_PT_LOG(d, "table_off = %#x, total_entries = %d\n",
               table_off, total_entries);
    msix->table_offset_adjust = table_off & 0x0fff;
    msix->phys_iomem_base =
        mmap(NULL,
             total_entries * PCI_MSIX_ENTRY_SIZE + msix->table_offset_adjust,
             PROT_READ,
             MAP_SHARED | MAP_LOCKED,
             fd,
             msix->table_base + table_off - msix->table_offset_adjust);
    close(fd);
    if (msix->phys_iomem_base == MAP_FAILED) {
        rc = -errno;
        XEN_PT_ERR(d, "Can't map physical MSI-X table: %s\n", strerror(errno));
        goto error_out;
    }
    msix->phys_iomem_base = (char *)msix->phys_iomem_base
        + msix->table_offset_adjust;

    XEN_PT_LOG(d, "mapping physical MSI-X table to %p\n",
               msix->phys_iomem_base);

    memory_region_add_subregion_overlap(&s->bar[bar_index], table_off,
                                        &msix->mmio,
                                        2); /* Priority: pci default + 1 */

    return 0;

error_out:
    memory_region_destroy(&msix->mmio);
    g_free(s->msix);
    s->msix = NULL;
    return rc;
}

void xen_pt_msix_delete(XenPCIPassthroughState *s)
{
    XenPTMSIX *msix = s->msix;

    if (!msix) {
        return;
    }

    /* unmap the MSI-X memory mapped register area */
    if (msix->phys_iomem_base) {
        XEN_PT_LOG(&s->dev, "unmapping physical MSI-X table from %p\n",
                   msix->phys_iomem_base);
        munmap(msix->phys_iomem_base, msix->total_entries * PCI_MSIX_ENTRY_SIZE
               + msix->table_offset_adjust);
    }

    memory_region_del_subregion(&s->bar[msix->bar_index], &msix->mmio);
    memory_region_destroy(&msix->mmio);

    g_free(s->msix);
    s->msix = NULL;
}
