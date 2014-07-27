/*
 * msi.c
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/pci/msi.h"
#include "qemu/range.h"

/* Eventually those constants should go to Linux pci_regs.h */
#define PCI_MSI_PENDING_32      0x10
#define PCI_MSI_PENDING_64      0x14

/* PCI_MSI_ADDRESS_LO */
#define PCI_MSI_ADDRESS_LO_MASK         (~0x3)

/* If we get rid of cap allocator, we won't need those. */
#define PCI_MSI_32_SIZEOF       0x0a
#define PCI_MSI_64_SIZEOF       0x0e
#define PCI_MSI_32M_SIZEOF      0x14
#define PCI_MSI_64M_SIZEOF      0x18

#define PCI_MSI_VECTORS_MAX     32

/* Flag for interrupt controller to declare MSI/MSI-X support */
bool msi_supported;

/* If we get rid of cap allocator, we won't need this. */
static inline uint8_t msi_cap_sizeof(uint16_t flags)
{
    switch (flags & (PCI_MSI_FLAGS_MASKBIT | PCI_MSI_FLAGS_64BIT)) {
    case PCI_MSI_FLAGS_MASKBIT | PCI_MSI_FLAGS_64BIT:
        return PCI_MSI_64M_SIZEOF;
    case PCI_MSI_FLAGS_64BIT:
        return PCI_MSI_64_SIZEOF;
    case PCI_MSI_FLAGS_MASKBIT:
        return PCI_MSI_32M_SIZEOF;
    case 0:
        return PCI_MSI_32_SIZEOF;
    default:
        abort();
        break;
    }
    return 0;
}

//#define MSI_DEBUG

#ifdef MSI_DEBUG
# define MSI_DPRINTF(fmt, ...)                                          \
    fprintf(stderr, "%s:%d " fmt, __func__, __LINE__, ## __VA_ARGS__)
#else
# define MSI_DPRINTF(fmt, ...)  do { } while (0)
#endif
#define MSI_DEV_PRINTF(dev, fmt, ...)                                   \
    MSI_DPRINTF("%s:%x " fmt, (dev)->name, (dev)->devfn, ## __VA_ARGS__)

static inline unsigned int msi_nr_vectors(uint16_t flags)
{
    return 1U <<
        ((flags & PCI_MSI_FLAGS_QSIZE) >> (ffs(PCI_MSI_FLAGS_QSIZE) - 1));
}

static inline uint8_t msi_flags_off(const PCIDevice* dev)
{
    return dev->msi_cap + PCI_MSI_FLAGS;
}

static inline uint8_t msi_address_lo_off(const PCIDevice* dev)
{
    return dev->msi_cap + PCI_MSI_ADDRESS_LO;
}

static inline uint8_t msi_address_hi_off(const PCIDevice* dev)
{
    return dev->msi_cap + PCI_MSI_ADDRESS_HI;
}

static inline uint8_t msi_data_off(const PCIDevice* dev, bool msi64bit)
{
    return dev->msi_cap + (msi64bit ? PCI_MSI_DATA_64 : PCI_MSI_DATA_32);
}

static inline uint8_t msi_mask_off(const PCIDevice* dev, bool msi64bit)
{
    return dev->msi_cap + (msi64bit ? PCI_MSI_MASK_64 : PCI_MSI_MASK_32);
}

static inline uint8_t msi_pending_off(const PCIDevice* dev, bool msi64bit)
{
    return dev->msi_cap + (msi64bit ? PCI_MSI_PENDING_64 : PCI_MSI_PENDING_32);
}

/*
 * Special API for POWER to configure the vectors through
 * a side channel. Should never be used by devices.
 */
void msi_set_message(PCIDevice *dev, MSIMessage msg)
{
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    bool msi64bit = flags & PCI_MSI_FLAGS_64BIT;

    if (msi64bit) {
        pci_set_quad(dev->config + msi_address_lo_off(dev), msg.address);
    } else {
        pci_set_long(dev->config + msi_address_lo_off(dev), msg.address);
    }
    pci_set_word(dev->config + msi_data_off(dev, msi64bit), msg.data);
}

MSIMessage msi_get_message(PCIDevice *dev, unsigned int vector)
{
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    bool msi64bit = flags & PCI_MSI_FLAGS_64BIT;
    unsigned int nr_vectors = msi_nr_vectors(flags);
    MSIMessage msg;

    assert(vector < nr_vectors);

    if (msi64bit) {
        msg.address = pci_get_quad(dev->config + msi_address_lo_off(dev));
    } else {
        msg.address = pci_get_long(dev->config + msi_address_lo_off(dev));
    }

    /* upper bit 31:16 is zero */
    msg.data = pci_get_word(dev->config + msi_data_off(dev, msi64bit));
    if (nr_vectors > 1) {
        msg.data &= ~(nr_vectors - 1);
        msg.data |= vector;
    }

    return msg;
}

bool msi_enabled(const PCIDevice *dev)
{
    return msi_present(dev) &&
        (pci_get_word(dev->config + msi_flags_off(dev)) &
         PCI_MSI_FLAGS_ENABLE);
}

int msi_init(struct PCIDevice *dev, uint8_t offset,
             unsigned int nr_vectors, bool msi64bit, bool msi_per_vector_mask)
{
    unsigned int vectors_order;
    uint16_t flags;
    uint8_t cap_size;
    int config_offset;

    if (!msi_supported) {
        return -ENOTSUP;
    }

    MSI_DEV_PRINTF(dev,
                   "init offset: 0x%"PRIx8" vector: %"PRId8
                   " 64bit %d mask %d\n",
                   offset, nr_vectors, msi64bit, msi_per_vector_mask);

    assert(!(nr_vectors & (nr_vectors - 1)));   /* power of 2 */
    assert(nr_vectors > 0);
    assert(nr_vectors <= PCI_MSI_VECTORS_MAX);
    /* the nr of MSI vectors is up to 32 */
    vectors_order = ffs(nr_vectors) - 1;

    flags = vectors_order << (ffs(PCI_MSI_FLAGS_QMASK) - 1);
    if (msi64bit) {
        flags |= PCI_MSI_FLAGS_64BIT;
    }
    if (msi_per_vector_mask) {
        flags |= PCI_MSI_FLAGS_MASKBIT;
    }

    cap_size = msi_cap_sizeof(flags);
    config_offset = pci_add_capability(dev, PCI_CAP_ID_MSI, offset, cap_size);
    if (config_offset < 0) {
        return config_offset;
    }

    dev->msi_cap = config_offset;
    dev->cap_present |= QEMU_PCI_CAP_MSI;

    pci_set_word(dev->config + msi_flags_off(dev), flags);
    pci_set_word(dev->wmask + msi_flags_off(dev),
                 PCI_MSI_FLAGS_QSIZE | PCI_MSI_FLAGS_ENABLE);
    pci_set_long(dev->wmask + msi_address_lo_off(dev),
                 PCI_MSI_ADDRESS_LO_MASK);
    if (msi64bit) {
        pci_set_long(dev->wmask + msi_address_hi_off(dev), 0xffffffff);
    }
    pci_set_word(dev->wmask + msi_data_off(dev, msi64bit), 0xffff);

    if (msi_per_vector_mask) {
        /* Make mask bits 0 to nr_vectors - 1 writable. */
        pci_set_long(dev->wmask + msi_mask_off(dev, msi64bit),
                     0xffffffff >> (PCI_MSI_VECTORS_MAX - nr_vectors));
    }
    return config_offset;
}

void msi_uninit(struct PCIDevice *dev)
{
    uint16_t flags;
    uint8_t cap_size;

    if (!msi_present(dev)) {
        return;
    }
    flags = pci_get_word(dev->config + msi_flags_off(dev));
    cap_size = msi_cap_sizeof(flags);
    pci_del_capability(dev, PCI_CAP_ID_MSI, cap_size);
    dev->cap_present &= ~QEMU_PCI_CAP_MSI;

    MSI_DEV_PRINTF(dev, "uninit\n");
}

void msi_reset(PCIDevice *dev)
{
    uint16_t flags;
    bool msi64bit;

    if (!msi_present(dev)) {
        return;
    }

    flags = pci_get_word(dev->config + msi_flags_off(dev));
    flags &= ~(PCI_MSI_FLAGS_QSIZE | PCI_MSI_FLAGS_ENABLE);
    msi64bit = flags & PCI_MSI_FLAGS_64BIT;

    pci_set_word(dev->config + msi_flags_off(dev), flags);
    pci_set_long(dev->config + msi_address_lo_off(dev), 0);
    if (msi64bit) {
        pci_set_long(dev->config + msi_address_hi_off(dev), 0);
    }
    pci_set_word(dev->config + msi_data_off(dev, msi64bit), 0);
    if (flags & PCI_MSI_FLAGS_MASKBIT) {
        pci_set_long(dev->config + msi_mask_off(dev, msi64bit), 0);
        pci_set_long(dev->config + msi_pending_off(dev, msi64bit), 0);
    }
    MSI_DEV_PRINTF(dev, "reset\n");
}

static bool msi_is_masked(const PCIDevice *dev, unsigned int vector)
{
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    uint32_t mask;
    assert(vector < PCI_MSI_VECTORS_MAX);

    if (!(flags & PCI_MSI_FLAGS_MASKBIT)) {
        return false;
    }

    mask = pci_get_long(dev->config +
                        msi_mask_off(dev, flags & PCI_MSI_FLAGS_64BIT));
    return mask & (1U << vector);
}

void msi_notify(PCIDevice *dev, unsigned int vector)
{
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    bool msi64bit = flags & PCI_MSI_FLAGS_64BIT;
    unsigned int nr_vectors = msi_nr_vectors(flags);
    MSIMessage msg;

    assert(vector < nr_vectors);
    if (msi_is_masked(dev, vector)) {
        assert(flags & PCI_MSI_FLAGS_MASKBIT);
        pci_long_test_and_set_mask(
            dev->config + msi_pending_off(dev, msi64bit), 1U << vector);
        MSI_DEV_PRINTF(dev, "pending vector 0x%x\n", vector);
        return;
    }

    msg = msi_get_message(dev, vector);

    MSI_DEV_PRINTF(dev,
                   "notify vector 0x%x"
                   " address: 0x%"PRIx64" data: 0x%"PRIx32"\n",
                   vector, msg.address, msg.data);
    stl_le_phys(&dev->bus_master_as, msg.address, msg.data);
}

/* Normally called by pci_default_write_config(). */
void msi_write_config(PCIDevice *dev, uint32_t addr, uint32_t val, int len)
{
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    bool msi64bit = flags & PCI_MSI_FLAGS_64BIT;
    bool msi_per_vector_mask = flags & PCI_MSI_FLAGS_MASKBIT;
    unsigned int nr_vectors;
    uint8_t log_num_vecs;
    uint8_t log_max_vecs;
    unsigned int vector;
    uint32_t pending;

    if (!msi_present(dev) ||
        !ranges_overlap(addr, len, dev->msi_cap, msi_cap_sizeof(flags))) {
        return;
    }

#ifdef MSI_DEBUG
    MSI_DEV_PRINTF(dev, "addr 0x%"PRIx32" val 0x%"PRIx32" len %d\n",
                   addr, val, len);
    MSI_DEV_PRINTF(dev, "ctrl: 0x%"PRIx16" address: 0x%"PRIx32,
                   flags,
                   pci_get_long(dev->config + msi_address_lo_off(dev)));
    if (msi64bit) {
        fprintf(stderr, " address-hi: 0x%"PRIx32,
                pci_get_long(dev->config + msi_address_hi_off(dev)));
    }
    fprintf(stderr, " data: 0x%"PRIx16,
            pci_get_word(dev->config + msi_data_off(dev, msi64bit)));
    if (flags & PCI_MSI_FLAGS_MASKBIT) {
        fprintf(stderr, " mask 0x%"PRIx32" pending 0x%"PRIx32,
                pci_get_long(dev->config + msi_mask_off(dev, msi64bit)),
                pci_get_long(dev->config + msi_pending_off(dev, msi64bit)));
    }
    fprintf(stderr, "\n");
#endif

    if (!(flags & PCI_MSI_FLAGS_ENABLE)) {
        return;
    }

    /*
     * Now MSI is enabled, clear INTx# interrupts.
     * the driver is prohibited from writing enable bit to mask
     * a service request. But the guest OS could do this.
     * So we just discard the interrupts as moderate fallback.
     *
     * 6.8.3.3. Enabling Operation
     *   While enabled for MSI or MSI-X operation, a function is prohibited
     *   from using its INTx# pin (if implemented) to request
     *   service (MSI, MSI-X, and INTx# are mutually exclusive).
     */
    pci_device_deassert_intx(dev);

    /*
     * nr_vectors might be set bigger than capable. So clamp it.
     * This is not legal by spec, so we can do anything we like,
     * just don't crash the host
     */
    log_num_vecs =
        (flags & PCI_MSI_FLAGS_QSIZE) >> (ffs(PCI_MSI_FLAGS_QSIZE) - 1);
    log_max_vecs =
        (flags & PCI_MSI_FLAGS_QMASK) >> (ffs(PCI_MSI_FLAGS_QMASK) - 1);
    if (log_num_vecs > log_max_vecs) {
        flags &= ~PCI_MSI_FLAGS_QSIZE;
        flags |= log_max_vecs << (ffs(PCI_MSI_FLAGS_QSIZE) - 1);
        pci_set_word(dev->config + msi_flags_off(dev), flags);
    }

    if (!msi_per_vector_mask) {
        /* if per vector masking isn't supported,
           there is no pending interrupt. */
        return;
    }

    nr_vectors = msi_nr_vectors(flags);

    /* This will discard pending interrupts, if any. */
    pending = pci_get_long(dev->config + msi_pending_off(dev, msi64bit));
    pending &= 0xffffffff >> (PCI_MSI_VECTORS_MAX - nr_vectors);
    pci_set_long(dev->config + msi_pending_off(dev, msi64bit), pending);

    /* deliver pending interrupts which are unmasked */
    for (vector = 0; vector < nr_vectors; ++vector) {
        if (msi_is_masked(dev, vector) || !(pending & (1U << vector))) {
            continue;
        }

        pci_long_test_and_clear_mask(
            dev->config + msi_pending_off(dev, msi64bit), 1U << vector);
        msi_notify(dev, vector);
    }
}

unsigned int msi_nr_vectors_allocated(const PCIDevice *dev)
{
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    return msi_nr_vectors(flags);
}
