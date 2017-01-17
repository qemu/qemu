/*
 * MSI-X device support
 *
 * This module includes support for MSI-X in pci devices.
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 *  Copyright (c) 2009, Red Hat Inc, Michael S. Tsirkin (mst@redhat.com)
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "hw/xen/xen.h"
#include "qemu/range.h"
#include "qapi/error.h"

#define MSIX_CAP_LENGTH 12

/* MSI enable bit and maskall bit are in byte 1 in FLAGS register */
#define MSIX_CONTROL_OFFSET (PCI_MSIX_FLAGS + 1)
#define MSIX_ENABLE_MASK (PCI_MSIX_FLAGS_ENABLE >> 8)
#define MSIX_MASKALL_MASK (PCI_MSIX_FLAGS_MASKALL >> 8)

MSIMessage msix_get_message(PCIDevice *dev, unsigned vector)
{
    uint8_t *table_entry = dev->msix_table + vector * PCI_MSIX_ENTRY_SIZE;
    MSIMessage msg;

    msg.address = pci_get_quad(table_entry + PCI_MSIX_ENTRY_LOWER_ADDR);
    msg.data = pci_get_long(table_entry + PCI_MSIX_ENTRY_DATA);
    return msg;
}

/*
 * Special API for POWER to configure the vectors through
 * a side channel. Should never be used by devices.
 */
void msix_set_message(PCIDevice *dev, int vector, struct MSIMessage msg)
{
    uint8_t *table_entry = dev->msix_table + vector * PCI_MSIX_ENTRY_SIZE;

    pci_set_quad(table_entry + PCI_MSIX_ENTRY_LOWER_ADDR, msg.address);
    pci_set_long(table_entry + PCI_MSIX_ENTRY_DATA, msg.data);
    table_entry[PCI_MSIX_ENTRY_VECTOR_CTRL] &= ~PCI_MSIX_ENTRY_CTRL_MASKBIT;
}

static uint8_t msix_pending_mask(int vector)
{
    return 1 << (vector % 8);
}

static uint8_t *msix_pending_byte(PCIDevice *dev, int vector)
{
    return dev->msix_pba + vector / 8;
}

static int msix_is_pending(PCIDevice *dev, int vector)
{
    return *msix_pending_byte(dev, vector) & msix_pending_mask(vector);
}

void msix_set_pending(PCIDevice *dev, unsigned int vector)
{
    *msix_pending_byte(dev, vector) |= msix_pending_mask(vector);
}

void msix_clr_pending(PCIDevice *dev, int vector)
{
    *msix_pending_byte(dev, vector) &= ~msix_pending_mask(vector);
}

static bool msix_vector_masked(PCIDevice *dev, unsigned int vector, bool fmask)
{
    unsigned offset = vector * PCI_MSIX_ENTRY_SIZE;
    uint8_t *data = &dev->msix_table[offset + PCI_MSIX_ENTRY_DATA];
    /* MSIs on Xen can be remapped into pirqs. In those cases, masking
     * and unmasking go through the PV evtchn path. */
    if (xen_enabled() && xen_is_pirq_msi(pci_get_long(data))) {
        return false;
    }
    return fmask || dev->msix_table[offset + PCI_MSIX_ENTRY_VECTOR_CTRL] &
        PCI_MSIX_ENTRY_CTRL_MASKBIT;
}

bool msix_is_masked(PCIDevice *dev, unsigned int vector)
{
    return msix_vector_masked(dev, vector, dev->msix_function_masked);
}

static void msix_fire_vector_notifier(PCIDevice *dev,
                                      unsigned int vector, bool is_masked)
{
    MSIMessage msg;
    int ret;

    if (!dev->msix_vector_use_notifier) {
        return;
    }
    if (is_masked) {
        dev->msix_vector_release_notifier(dev, vector);
    } else {
        msg = msix_get_message(dev, vector);
        ret = dev->msix_vector_use_notifier(dev, vector, msg);
        assert(ret >= 0);
    }
}

static void msix_handle_mask_update(PCIDevice *dev, int vector, bool was_masked)
{
    bool is_masked = msix_is_masked(dev, vector);

    if (is_masked == was_masked) {
        return;
    }

    msix_fire_vector_notifier(dev, vector, is_masked);

    if (!is_masked && msix_is_pending(dev, vector)) {
        msix_clr_pending(dev, vector);
        msix_notify(dev, vector);
    }
}

static void msix_update_function_masked(PCIDevice *dev)
{
    dev->msix_function_masked = !msix_enabled(dev) ||
        (dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] & MSIX_MASKALL_MASK);
}

/* Handle MSI-X capability config write. */
void msix_write_config(PCIDevice *dev, uint32_t addr,
                       uint32_t val, int len)
{
    unsigned enable_pos = dev->msix_cap + MSIX_CONTROL_OFFSET;
    int vector;
    bool was_masked;

    if (!msix_present(dev) || !range_covers_byte(addr, len, enable_pos)) {
        return;
    }

    was_masked = dev->msix_function_masked;
    msix_update_function_masked(dev);

    if (!msix_enabled(dev)) {
        return;
    }

    pci_device_deassert_intx(dev);

    if (dev->msix_function_masked == was_masked) {
        return;
    }

    for (vector = 0; vector < dev->msix_entries_nr; ++vector) {
        msix_handle_mask_update(dev, vector,
                                msix_vector_masked(dev, vector, was_masked));
    }
}

static uint64_t msix_table_mmio_read(void *opaque, hwaddr addr,
                                     unsigned size)
{
    PCIDevice *dev = opaque;

    return pci_get_long(dev->msix_table + addr);
}

static void msix_table_mmio_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    PCIDevice *dev = opaque;
    int vector = addr / PCI_MSIX_ENTRY_SIZE;
    bool was_masked;

    was_masked = msix_is_masked(dev, vector);
    pci_set_long(dev->msix_table + addr, val);
    msix_handle_mask_update(dev, vector, was_masked);
}

static const MemoryRegionOps msix_table_mmio_ops = {
    .read = msix_table_mmio_read,
    .write = msix_table_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t msix_pba_mmio_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    PCIDevice *dev = opaque;
    if (dev->msix_vector_poll_notifier) {
        unsigned vector_start = addr * 8;
        unsigned vector_end = MIN(addr + size * 8, dev->msix_entries_nr);
        dev->msix_vector_poll_notifier(dev, vector_start, vector_end);
    }

    return pci_get_long(dev->msix_pba + addr);
}

static void msix_pba_mmio_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
}

static const MemoryRegionOps msix_pba_mmio_ops = {
    .read = msix_pba_mmio_read,
    .write = msix_pba_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void msix_mask_all(struct PCIDevice *dev, unsigned nentries)
{
    int vector;

    for (vector = 0; vector < nentries; ++vector) {
        unsigned offset =
            vector * PCI_MSIX_ENTRY_SIZE + PCI_MSIX_ENTRY_VECTOR_CTRL;
        bool was_masked = msix_is_masked(dev, vector);

        dev->msix_table[offset] |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
        msix_handle_mask_update(dev, vector, was_masked);
    }
}

/*
 * Make PCI device @dev MSI-X capable
 * @nentries is the max number of MSI-X vectors that the device support.
 * @table_bar is the MemoryRegion that MSI-X table structure resides.
 * @table_bar_nr is number of base address register corresponding to @table_bar.
 * @table_offset indicates the offset that the MSI-X table structure starts with
 * in @table_bar.
 * @pba_bar is the MemoryRegion that the Pending Bit Array structure resides.
 * @pba_bar_nr is number of base address register corresponding to @pba_bar.
 * @pba_offset indicates the offset that the Pending Bit Array structure
 * starts with in @pba_bar.
 * Non-zero @cap_pos puts capability MSI-X at that offset in PCI config space.
 * @errp is for returning errors.
 *
 * Return 0 on success; set @errp and return -errno on error:
 * -ENOTSUP means lacking msi support for a msi-capable platform.
 * -EINVAL means capability overlap, happens when @cap_pos is non-zero,
 * also means a programming error, except device assignment, which can check
 * if a real HW is broken.
 */
int msix_init(struct PCIDevice *dev, unsigned short nentries,
              MemoryRegion *table_bar, uint8_t table_bar_nr,
              unsigned table_offset, MemoryRegion *pba_bar,
              uint8_t pba_bar_nr, unsigned pba_offset, uint8_t cap_pos,
              Error **errp)
{
    int cap;
    unsigned table_size, pba_size;
    uint8_t *config;

    /* Nothing to do if MSI is not supported by interrupt controller */
    if (!msi_nonbroken) {
        error_setg(errp, "MSI-X is not supported by interrupt controller");
        return -ENOTSUP;
    }

    if (nentries < 1 || nentries > PCI_MSIX_FLAGS_QSIZE + 1) {
        error_setg(errp, "The number of MSI-X vectors is invalid");
        return -EINVAL;
    }

    table_size = nentries * PCI_MSIX_ENTRY_SIZE;
    pba_size = QEMU_ALIGN_UP(nentries, 64) / 8;

    /* Sanity test: table & pba don't overlap, fit within BARs, min aligned */
    if ((table_bar_nr == pba_bar_nr &&
         ranges_overlap(table_offset, table_size, pba_offset, pba_size)) ||
        table_offset + table_size > memory_region_size(table_bar) ||
        pba_offset + pba_size > memory_region_size(pba_bar) ||
        (table_offset | pba_offset) & PCI_MSIX_FLAGS_BIRMASK) {
        error_setg(errp, "table & pba overlap, or they don't fit in BARs,"
                   " or don't align");
        return -EINVAL;
    }

    cap = pci_add_capability2(dev, PCI_CAP_ID_MSIX,
                              cap_pos, MSIX_CAP_LENGTH, errp);
    if (cap < 0) {
        return cap;
    }

    dev->msix_cap = cap;
    dev->cap_present |= QEMU_PCI_CAP_MSIX;
    config = dev->config + cap;

    pci_set_word(config + PCI_MSIX_FLAGS, nentries - 1);
    dev->msix_entries_nr = nentries;
    dev->msix_function_masked = true;

    pci_set_long(config + PCI_MSIX_TABLE, table_offset | table_bar_nr);
    pci_set_long(config + PCI_MSIX_PBA, pba_offset | pba_bar_nr);

    /* Make flags bit writable. */
    dev->wmask[cap + MSIX_CONTROL_OFFSET] |= MSIX_ENABLE_MASK |
                                             MSIX_MASKALL_MASK;

    dev->msix_table = g_malloc0(table_size);
    dev->msix_pba = g_malloc0(pba_size);
    dev->msix_entry_used = g_malloc0(nentries * sizeof *dev->msix_entry_used);

    msix_mask_all(dev, nentries);

    memory_region_init_io(&dev->msix_table_mmio, OBJECT(dev), &msix_table_mmio_ops, dev,
                          "msix-table", table_size);
    memory_region_add_subregion(table_bar, table_offset, &dev->msix_table_mmio);
    memory_region_init_io(&dev->msix_pba_mmio, OBJECT(dev), &msix_pba_mmio_ops, dev,
                          "msix-pba", pba_size);
    memory_region_add_subregion(pba_bar, pba_offset, &dev->msix_pba_mmio);

    return 0;
}

int msix_init_exclusive_bar(PCIDevice *dev, unsigned short nentries,
                            uint8_t bar_nr, Error **errp)
{
    int ret;
    char *name;
    uint32_t bar_size = 4096;
    uint32_t bar_pba_offset = bar_size / 2;
    uint32_t bar_pba_size = (nentries / 8 + 1) * 8;

    /*
     * Migration compatibility dictates that this remains a 4k
     * BAR with the vector table in the lower half and PBA in
     * the upper half for nentries which is lower or equal to 128.
     * No need to care about using more than 65 entries for legacy
     * machine types who has at most 64 queues.
     */
    if (nentries * PCI_MSIX_ENTRY_SIZE > bar_pba_offset) {
        bar_pba_offset = nentries * PCI_MSIX_ENTRY_SIZE;
    }

    if (bar_pba_offset + bar_pba_size > 4096) {
        bar_size = bar_pba_offset + bar_pba_size;
    }

    bar_size = pow2ceil(bar_size);

    name = g_strdup_printf("%s-msix", dev->name);
    memory_region_init(&dev->msix_exclusive_bar, OBJECT(dev), name, bar_size);
    g_free(name);

    ret = msix_init(dev, nentries, &dev->msix_exclusive_bar, bar_nr,
                    0, &dev->msix_exclusive_bar,
                    bar_nr, bar_pba_offset,
                    0, errp);
    if (ret) {
        return ret;
    }

    pci_register_bar(dev, bar_nr, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &dev->msix_exclusive_bar);

    return 0;
}

static void msix_free_irq_entries(PCIDevice *dev)
{
    int vector;

    for (vector = 0; vector < dev->msix_entries_nr; ++vector) {
        dev->msix_entry_used[vector] = 0;
        msix_clr_pending(dev, vector);
    }
}

static void msix_clear_all_vectors(PCIDevice *dev)
{
    int vector;

    for (vector = 0; vector < dev->msix_entries_nr; ++vector) {
        msix_clr_pending(dev, vector);
    }
}

/* Clean up resources for the device. */
void msix_uninit(PCIDevice *dev, MemoryRegion *table_bar, MemoryRegion *pba_bar)
{
    if (!msix_present(dev)) {
        return;
    }
    pci_del_capability(dev, PCI_CAP_ID_MSIX, MSIX_CAP_LENGTH);
    dev->msix_cap = 0;
    msix_free_irq_entries(dev);
    dev->msix_entries_nr = 0;
    memory_region_del_subregion(pba_bar, &dev->msix_pba_mmio);
    g_free(dev->msix_pba);
    dev->msix_pba = NULL;
    memory_region_del_subregion(table_bar, &dev->msix_table_mmio);
    g_free(dev->msix_table);
    dev->msix_table = NULL;
    g_free(dev->msix_entry_used);
    dev->msix_entry_used = NULL;
    dev->cap_present &= ~QEMU_PCI_CAP_MSIX;
}

void msix_uninit_exclusive_bar(PCIDevice *dev)
{
    if (msix_present(dev)) {
        msix_uninit(dev, &dev->msix_exclusive_bar, &dev->msix_exclusive_bar);
    }
}

void msix_save(PCIDevice *dev, QEMUFile *f)
{
    unsigned n = dev->msix_entries_nr;

    if (!msix_present(dev)) {
        return;
    }

    qemu_put_buffer(f, dev->msix_table, n * PCI_MSIX_ENTRY_SIZE);
    qemu_put_buffer(f, dev->msix_pba, (n + 7) / 8);
}

/* Should be called after restoring the config space. */
void msix_load(PCIDevice *dev, QEMUFile *f)
{
    unsigned n = dev->msix_entries_nr;
    unsigned int vector;

    if (!msix_present(dev)) {
        return;
    }

    msix_clear_all_vectors(dev);
    qemu_get_buffer(f, dev->msix_table, n * PCI_MSIX_ENTRY_SIZE);
    qemu_get_buffer(f, dev->msix_pba, (n + 7) / 8);
    msix_update_function_masked(dev);

    for (vector = 0; vector < n; vector++) {
        msix_handle_mask_update(dev, vector, true);
    }
}

/* Does device support MSI-X? */
int msix_present(PCIDevice *dev)
{
    return dev->cap_present & QEMU_PCI_CAP_MSIX;
}

/* Is MSI-X enabled? */
int msix_enabled(PCIDevice *dev)
{
    return (dev->cap_present & QEMU_PCI_CAP_MSIX) &&
        (dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] &
         MSIX_ENABLE_MASK);
}

/* Send an MSI-X message */
void msix_notify(PCIDevice *dev, unsigned vector)
{
    MSIMessage msg;

    if (vector >= dev->msix_entries_nr || !dev->msix_entry_used[vector]) {
        return;
    }

    if (msix_is_masked(dev, vector)) {
        msix_set_pending(dev, vector);
        return;
    }

    msg = msix_get_message(dev, vector);

    msi_send_message(dev, msg);
}

void msix_reset(PCIDevice *dev)
{
    if (!msix_present(dev)) {
        return;
    }
    msix_clear_all_vectors(dev);
    dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] &=
	    ~dev->wmask[dev->msix_cap + MSIX_CONTROL_OFFSET];
    memset(dev->msix_table, 0, dev->msix_entries_nr * PCI_MSIX_ENTRY_SIZE);
    memset(dev->msix_pba, 0, QEMU_ALIGN_UP(dev->msix_entries_nr, 64) / 8);
    msix_mask_all(dev, dev->msix_entries_nr);
}

/* PCI spec suggests that devices make it possible for software to configure
 * less vectors than supported by the device, but does not specify a standard
 * mechanism for devices to do so.
 *
 * We support this by asking devices to declare vectors software is going to
 * actually use, and checking this on the notification path. Devices that
 * don't want to follow the spec suggestion can declare all vectors as used. */

/* Mark vector as used. */
int msix_vector_use(PCIDevice *dev, unsigned vector)
{
    if (vector >= dev->msix_entries_nr) {
        return -EINVAL;
    }

    dev->msix_entry_used[vector]++;
    return 0;
}

/* Mark vector as unused. */
void msix_vector_unuse(PCIDevice *dev, unsigned vector)
{
    if (vector >= dev->msix_entries_nr || !dev->msix_entry_used[vector]) {
        return;
    }
    if (--dev->msix_entry_used[vector]) {
        return;
    }
    msix_clr_pending(dev, vector);
}

void msix_unuse_all_vectors(PCIDevice *dev)
{
    if (!msix_present(dev)) {
        return;
    }
    msix_free_irq_entries(dev);
}

unsigned int msix_nr_vectors_allocated(const PCIDevice *dev)
{
    return dev->msix_entries_nr;
}

static int msix_set_notifier_for_vector(PCIDevice *dev, unsigned int vector)
{
    MSIMessage msg;

    if (msix_is_masked(dev, vector)) {
        return 0;
    }
    msg = msix_get_message(dev, vector);
    return dev->msix_vector_use_notifier(dev, vector, msg);
}

static void msix_unset_notifier_for_vector(PCIDevice *dev, unsigned int vector)
{
    if (msix_is_masked(dev, vector)) {
        return;
    }
    dev->msix_vector_release_notifier(dev, vector);
}

int msix_set_vector_notifiers(PCIDevice *dev,
                              MSIVectorUseNotifier use_notifier,
                              MSIVectorReleaseNotifier release_notifier,
                              MSIVectorPollNotifier poll_notifier)
{
    int vector, ret;

    assert(use_notifier && release_notifier);

    dev->msix_vector_use_notifier = use_notifier;
    dev->msix_vector_release_notifier = release_notifier;
    dev->msix_vector_poll_notifier = poll_notifier;

    if ((dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] &
        (MSIX_ENABLE_MASK | MSIX_MASKALL_MASK)) == MSIX_ENABLE_MASK) {
        for (vector = 0; vector < dev->msix_entries_nr; vector++) {
            ret = msix_set_notifier_for_vector(dev, vector);
            if (ret < 0) {
                goto undo;
            }
        }
    }
    if (dev->msix_vector_poll_notifier) {
        dev->msix_vector_poll_notifier(dev, 0, dev->msix_entries_nr);
    }
    return 0;

undo:
    while (--vector >= 0) {
        msix_unset_notifier_for_vector(dev, vector);
    }
    dev->msix_vector_use_notifier = NULL;
    dev->msix_vector_release_notifier = NULL;
    return ret;
}

void msix_unset_vector_notifiers(PCIDevice *dev)
{
    int vector;

    assert(dev->msix_vector_use_notifier &&
           dev->msix_vector_release_notifier);

    if ((dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] &
        (MSIX_ENABLE_MASK | MSIX_MASKALL_MASK)) == MSIX_ENABLE_MASK) {
        for (vector = 0; vector < dev->msix_entries_nr; vector++) {
            msix_unset_notifier_for_vector(dev, vector);
        }
    }
    dev->msix_vector_use_notifier = NULL;
    dev->msix_vector_release_notifier = NULL;
    dev->msix_vector_poll_notifier = NULL;
}

static int put_msix_state(QEMUFile *f, void *pv, size_t size,
                          VMStateField *field, QJSON *vmdesc)
{
    msix_save(pv, f);

    return 0;
}

static int get_msix_state(QEMUFile *f, void *pv, size_t size,
                          VMStateField *field)
{
    msix_load(pv, f);
    return 0;
}

static VMStateInfo vmstate_info_msix = {
    .name = "msix state",
    .get  = get_msix_state,
    .put  = put_msix_state,
};

const VMStateDescription vmstate_msix = {
    .name = "msix",
    .fields = (VMStateField[]) {
        {
            .name         = "msix",
            .version_id   = 0,
            .field_exists = NULL,
            .size         = 0,   /* ouch */
            .info         = &vmstate_info_msix,
            .flags        = VMS_SINGLE,
            .offset       = 0,
        },
        VMSTATE_END_OF_LIST()
    }
};
