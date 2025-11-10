/*
 * QEMU emulation of AMD IOMMU (AMD-Vi)
 *
 * Copyright (C) 2011 Eduard - Gabriel Munteanu
 * Copyright (C) 2015, 2016 David Kiarie Kahurani
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
 *
 * Cache implementation inspired by hw/i386/intel_iommu.c
 */

#include "qemu/osdep.h"
#include "hw/i386/pc.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "amd_iommu.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/i386/apic_internal.h"
#include "trace.h"
#include "hw/i386/apic-msidef.h"
#include "hw/qdev-properties.h"
#include "kvm/kvm_i386.h"
#include "qemu/iova-tree.h"

/* used AMD-Vi MMIO registers */
const char *amdvi_mmio_low[] = {
    "AMDVI_MMIO_DEVTAB_BASE",
    "AMDVI_MMIO_CMDBUF_BASE",
    "AMDVI_MMIO_EVTLOG_BASE",
    "AMDVI_MMIO_CONTROL",
    "AMDVI_MMIO_EXCL_BASE",
    "AMDVI_MMIO_EXCL_LIMIT",
    "AMDVI_MMIO_EXT_FEATURES",
    "AMDVI_MMIO_PPR_BASE",
    "UNHANDLED"
};
const char *amdvi_mmio_high[] = {
    "AMDVI_MMIO_COMMAND_HEAD",
    "AMDVI_MMIO_COMMAND_TAIL",
    "AMDVI_MMIO_EVTLOG_HEAD",
    "AMDVI_MMIO_EVTLOG_TAIL",
    "AMDVI_MMIO_STATUS",
    "AMDVI_MMIO_PPR_HEAD",
    "AMDVI_MMIO_PPR_TAIL",
    "UNHANDLED"
};

struct AMDVIAddressSpace {
    PCIBus *bus;                /* PCIBus (for bus number)              */
    uint8_t devfn;              /* device function                      */
    AMDVIState *iommu_state;    /* AMDVI - one per machine              */
    MemoryRegion root;          /* AMDVI Root memory map region         */
    IOMMUMemoryRegion iommu;    /* Device's address translation region  */
    MemoryRegion iommu_nodma;   /* Alias of shared nodma memory region  */
    MemoryRegion iommu_ir;      /* Device's interrupt remapping region  */
    AddressSpace as;            /* device's corresponding address space */

    /* DMA address translation support */
    IOMMUNotifierFlag notifier_flags;
    /* entry in list of Address spaces with registered notifiers */
    QLIST_ENTRY(AMDVIAddressSpace) next;
    /* Record DMA translation ranges */
    IOVATree *iova_tree;
    /* DMA address translation active */
    bool addr_translation;
};

/* AMDVI cache entry */
typedef struct AMDVIIOTLBEntry {
    uint16_t domid;             /* assigned domain id  */
    uint16_t devid;             /* device owning entry */
    uint64_t perms;             /* access permissions  */
    uint64_t translated_addr;   /* translated address  */
    uint64_t page_mask;         /* physical page size  */
} AMDVIIOTLBEntry;

/*
 * These 'fault' reasons have an overloaded meaning since they are not only
 * intended for describing reasons that generate an IO_PAGE_FAULT as per the AMD
 * IOMMU specification, but are also used to signal internal errors in the
 * emulation code.
 */
typedef enum AMDVIFaultReason {
    AMDVI_FR_DTE_RTR_ERR = 1,   /* Failure to retrieve DTE */
    AMDVI_FR_DTE_V,             /* DTE[V] = 0 */
    AMDVI_FR_DTE_TV,            /* DTE[TV] = 0 */
    AMDVI_FR_PT_ROOT_INV,       /* Page Table Root ptr invalid */
    AMDVI_FR_PT_ENTRY_INV,      /* Failure to read PTE from guest memory */
} AMDVIFaultReason;

typedef struct AMDVIAsKey {
    PCIBus *bus;
    uint8_t devfn;
} AMDVIAsKey;

typedef struct AMDVIIOTLBKey {
    uint64_t gfn;
    uint16_t devid;
} AMDVIIOTLBKey;

uint64_t amdvi_extended_feature_register(AMDVIState *s)
{
    uint64_t feature = AMDVI_DEFAULT_EXT_FEATURES;
    if (s->xtsup) {
        feature |= AMDVI_FEATURE_XT;
    }
    if (!s->iommu.dma_translation) {
        feature |= AMDVI_HATS_MODE_RESERVED;
    }

    return feature;
}

/* configure MMIO registers at startup/reset */
static void amdvi_set_quad(AMDVIState *s, hwaddr addr, uint64_t val,
                           uint64_t romask, uint64_t w1cmask)
{
    stq_le_p(&s->mmior[addr], val);
    stq_le_p(&s->romask[addr], romask);
    stq_le_p(&s->w1cmask[addr], w1cmask);
}

static uint16_t amdvi_readw(AMDVIState *s, hwaddr addr)
{
    return lduw_le_p(&s->mmior[addr]);
}

static uint32_t amdvi_readl(AMDVIState *s, hwaddr addr)
{
    return ldl_le_p(&s->mmior[addr]);
}

static uint64_t amdvi_readq(AMDVIState *s, hwaddr addr)
{
    return ldq_le_p(&s->mmior[addr]);
}

/* internal write */
static void amdvi_writeq_raw(AMDVIState *s, hwaddr addr, uint64_t val)
{
    stq_le_p(&s->mmior[addr], val);
}

/* external write */
static void amdvi_writew(AMDVIState *s, hwaddr addr, uint16_t val)
{
    uint16_t romask = lduw_le_p(&s->romask[addr]);
    uint16_t w1cmask = lduw_le_p(&s->w1cmask[addr]);
    uint16_t oldval = lduw_le_p(&s->mmior[addr]);

    uint16_t oldval_preserved = oldval & (romask | w1cmask);
    uint16_t newval_write = val & ~romask;
    uint16_t newval_w1c_set = val & w1cmask;

    stw_le_p(&s->mmior[addr],
             (oldval_preserved | newval_write) & ~newval_w1c_set);
}

static void amdvi_writel(AMDVIState *s, hwaddr addr, uint32_t val)
{
    uint32_t romask = ldl_le_p(&s->romask[addr]);
    uint32_t w1cmask = ldl_le_p(&s->w1cmask[addr]);
    uint32_t oldval = ldl_le_p(&s->mmior[addr]);

    uint32_t oldval_preserved = oldval & (romask | w1cmask);
    uint32_t newval_write = val & ~romask;
    uint32_t newval_w1c_set = val & w1cmask;

    stl_le_p(&s->mmior[addr],
             (oldval_preserved | newval_write) & ~newval_w1c_set);
}

static void amdvi_writeq(AMDVIState *s, hwaddr addr, uint64_t val)
{
    uint64_t romask = ldq_le_p(&s->romask[addr]);
    uint64_t w1cmask = ldq_le_p(&s->w1cmask[addr]);
    uint64_t oldval = ldq_le_p(&s->mmior[addr]);

    uint64_t oldval_preserved = oldval & (romask | w1cmask);
    uint64_t newval_write = val & ~romask;
    uint64_t newval_w1c_set = val & w1cmask;

    stq_le_p(&s->mmior[addr],
             (oldval_preserved | newval_write) & ~newval_w1c_set);
}

/* AND a 64-bit register with a 64-bit value */
static bool amdvi_test_mask(AMDVIState *s, hwaddr addr, uint64_t val)
{
    return amdvi_readq(s, addr) & val;
}

/* OR a 64-bit register with a 64-bit value storing result in the register */
static void amdvi_assign_orq(AMDVIState *s, hwaddr addr, uint64_t val)
{
    amdvi_writeq_raw(s, addr, amdvi_readq(s, addr) | val);
}

/* AND a 64-bit register with a 64-bit value storing result in the register */
static void amdvi_assign_andq(AMDVIState *s, hwaddr addr, uint64_t val)
{
   amdvi_writeq_raw(s, addr, amdvi_readq(s, addr) & val);
}

static void amdvi_generate_msi_interrupt(AMDVIState *s)
{
    MSIMessage msg = {};
    MemTxAttrs attrs = {
        .requester_id = pci_requester_id(&s->pci->dev)
    };

    if (msi_enabled(&s->pci->dev)) {
        msg = msi_get_message(&s->pci->dev, 0);
        address_space_stl_le(&address_space_memory, msg.address, msg.data,
                             attrs, NULL);
    }
}

static uint32_t get_next_eventlog_entry(AMDVIState *s)
{
    uint32_t evtlog_size = s->evtlog_len * AMDVI_EVENT_LEN;
    return (s->evtlog_tail + AMDVI_EVENT_LEN) % evtlog_size;
}

static void amdvi_log_event(AMDVIState *s, uint64_t *evt)
{
    uint32_t evtlog_tail_next;

    /* event logging not enabled */
    if (!s->evtlog_enabled || amdvi_test_mask(s, AMDVI_MMIO_STATUS,
        AMDVI_MMIO_STATUS_EVT_OVF)) {
        return;
    }

    evtlog_tail_next = get_next_eventlog_entry(s);

    /* event log buffer full */
    if (evtlog_tail_next == s->evtlog_head) {
        /* generate overflow interrupt */
        if (s->evtlog_intr) {
            amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_EVT_OVF);
            amdvi_generate_msi_interrupt(s);
        }
        return;
    }

    if (dma_memory_write(&address_space_memory, s->evtlog + s->evtlog_tail,
                         evt, AMDVI_EVENT_LEN, MEMTXATTRS_UNSPECIFIED)) {
        trace_amdvi_evntlog_fail(s->evtlog, s->evtlog_tail);
    }

    s->evtlog_tail = evtlog_tail_next;
    amdvi_writeq_raw(s, AMDVI_MMIO_EVENT_TAIL, s->evtlog_tail);

    if (s->evtlog_intr) {
        amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_EVENT_INT);
        amdvi_generate_msi_interrupt(s);
    }
}

static void amdvi_setevent_bits(uint64_t *buffer, uint64_t value, int start,
                                int length)
{
    int index = start / 64, bitpos = start % 64;
    uint64_t mask = MAKE_64BIT_MASK(start, length);
    buffer[index] &= ~mask;
    buffer[index] |= (value << bitpos) & mask;
}
/*
 * AMDVi event structure
 *    0:15   -> DeviceID
 *    48:63  -> event type + miscellaneous info
 *    64:127 -> related address
 */
static void amdvi_encode_event(uint64_t *evt, uint16_t devid, uint64_t addr,
                               uint16_t info)
{
    evt[0] = 0;
    evt[1] = 0;

    amdvi_setevent_bits(evt, devid, 0, 16);
    amdvi_setevent_bits(evt, info, 48, 16);
    amdvi_setevent_bits(evt, addr, 64, 64);
}
/* log an error encountered during a page walk
 *
 * @addr: virtual address in translation request
 */
static void amdvi_page_fault(AMDVIState *s, uint16_t devid,
                             hwaddr addr, uint16_t info)
{
    uint64_t evt[2];

    info |= AMDVI_EVENT_IOPF_I | AMDVI_EVENT_IOPF;
    amdvi_encode_event(evt, devid, addr, info);
    amdvi_log_event(s, evt);
    pci_word_test_and_set_mask(s->pci->dev.config + PCI_STATUS,
            PCI_STATUS_SIG_TARGET_ABORT);
}
/*
 * log a master abort accessing device table
 *  @devtab : address of device table entry
 *  @info : error flags
 */
static void amdvi_log_devtab_error(AMDVIState *s, uint16_t devid,
                                   hwaddr devtab, uint16_t info)
{
    uint64_t evt[2];

    info |= AMDVI_EVENT_DEV_TAB_HW_ERROR;

    amdvi_encode_event(evt, devid, devtab, info);
    amdvi_log_event(s, evt);
    pci_word_test_and_set_mask(s->pci->dev.config + PCI_STATUS,
            PCI_STATUS_SIG_TARGET_ABORT);
}
/* log an event trying to access command buffer
 *   @addr : address that couldn't be accessed
 */
static void amdvi_log_command_error(AMDVIState *s, hwaddr addr)
{
    uint64_t evt[2];
    uint16_t info = AMDVI_EVENT_COMMAND_HW_ERROR;

    amdvi_encode_event(evt, 0, addr, info);
    amdvi_log_event(s, evt);
    pci_word_test_and_set_mask(s->pci->dev.config + PCI_STATUS,
            PCI_STATUS_SIG_TARGET_ABORT);
}
/* log an illegal command event
 *   @addr : address of illegal command
 */
static void amdvi_log_illegalcom_error(AMDVIState *s, uint16_t info,
                                       hwaddr addr)
{
    uint64_t evt[2];

    info |= AMDVI_EVENT_ILLEGAL_COMMAND_ERROR;
    amdvi_encode_event(evt, 0, addr, info);
    amdvi_log_event(s, evt);
}
/* log an error accessing device table
 *
 *  @devid : device owning the table entry
 *  @devtab : address of device table entry
 *  @info : error flags
 */
static void amdvi_log_illegaldevtab_error(AMDVIState *s, uint16_t devid,
                                          hwaddr addr, uint16_t info)
{
    uint64_t evt[2];

    info |= AMDVI_EVENT_ILLEGAL_DEVTAB_ENTRY;
    amdvi_encode_event(evt, devid, addr, info);
    amdvi_log_event(s, evt);
}
/* log an error accessing a PTE entry
 * @addr : address that couldn't be accessed
 */
static void amdvi_log_pagetab_error(AMDVIState *s, uint16_t devid,
                                    hwaddr addr, uint16_t info)
{
    uint64_t evt[2];

    info |= AMDVI_EVENT_PAGE_TAB_HW_ERROR;
    amdvi_encode_event(evt, devid, addr, info);
    amdvi_log_event(s, evt);
    pci_word_test_and_set_mask(s->pci->dev.config + PCI_STATUS,
             PCI_STATUS_SIG_TARGET_ABORT);
}

static gboolean amdvi_as_equal(gconstpointer v1, gconstpointer v2)
{
    const AMDVIAsKey *key1 = v1;
    const AMDVIAsKey *key2 = v2;

    return key1->bus == key2->bus && key1->devfn == key2->devfn;
}

static guint amdvi_as_hash(gconstpointer v)
{
    const AMDVIAsKey *key = v;
    guint bus = (guint)(uintptr_t)key->bus;

    return (guint)(bus << 8 | (guint)key->devfn);
}

static AMDVIAddressSpace *amdvi_as_lookup(AMDVIState *s, PCIBus *bus,
                                          uint8_t devfn)
{
    const AMDVIAsKey key = { .bus = bus, .devfn = devfn };
    return g_hash_table_lookup(s->address_spaces, &key);
}

static gboolean amdvi_find_as_by_devid(gpointer key, gpointer value,
                                       gpointer user_data)
{
    const AMDVIAsKey *as = key;
    const uint16_t *devidp = user_data;

    return *devidp == PCI_BUILD_BDF(pci_bus_num(as->bus), as->devfn);
}

static AMDVIAddressSpace *amdvi_get_as_by_devid(AMDVIState *s, uint16_t devid)
{
    return g_hash_table_find(s->address_spaces,
                             amdvi_find_as_by_devid, &devid);
}

static gboolean amdvi_iotlb_equal(gconstpointer v1, gconstpointer v2)
{
    const AMDVIIOTLBKey *key1 = v1;
    const AMDVIIOTLBKey *key2 = v2;

    return key1->devid == key2->devid && key1->gfn == key2->gfn;
}

static guint amdvi_iotlb_hash(gconstpointer v)
{
    const AMDVIIOTLBKey *key = v;
    /* Use GPA and DEVID to find the bucket */
    return (guint)(key->gfn << AMDVI_PAGE_SHIFT_4K |
                   (key->devid & ~AMDVI_PAGE_MASK_4K));
}


static AMDVIIOTLBEntry *amdvi_iotlb_lookup(AMDVIState *s, hwaddr addr,
                                           uint64_t devid)
{
    AMDVIIOTLBKey key = {
        .gfn = AMDVI_GET_IOTLB_GFN(addr),
        .devid = devid,
    };
    return g_hash_table_lookup(s->iotlb, &key);
}

static void amdvi_iotlb_reset(AMDVIState *s)
{
    assert(s->iotlb);
    trace_amdvi_iotlb_reset();
    g_hash_table_remove_all(s->iotlb);
}

static gboolean amdvi_iotlb_remove_by_devid(gpointer key, gpointer value,
                                            gpointer user_data)
{
    AMDVIIOTLBEntry *entry = (AMDVIIOTLBEntry *)value;
    uint16_t devid = *(uint16_t *)user_data;
    return entry->devid == devid;
}

static void amdvi_iotlb_remove_page(AMDVIState *s, hwaddr addr,
                                    uint64_t devid)
{
    AMDVIIOTLBKey key = {
        .gfn = AMDVI_GET_IOTLB_GFN(addr),
        .devid = devid,
    };
    g_hash_table_remove(s->iotlb, &key);
}

static void amdvi_update_iotlb(AMDVIState *s, uint16_t devid,
                               uint64_t gpa, IOMMUTLBEntry to_cache,
                               uint16_t domid)
{
    /* don't cache erroneous translations */
    if (to_cache.perm != IOMMU_NONE) {
        AMDVIIOTLBEntry *entry = g_new(AMDVIIOTLBEntry, 1);
        AMDVIIOTLBKey *key = g_new(AMDVIIOTLBKey, 1);

        key->gfn = AMDVI_GET_IOTLB_GFN(gpa);
        key->devid = devid;

        trace_amdvi_cache_update(domid, PCI_BUS_NUM(devid), PCI_SLOT(devid),
                PCI_FUNC(devid), gpa, to_cache.translated_addr);

        if (g_hash_table_size(s->iotlb) >= AMDVI_IOTLB_MAX_SIZE) {
            amdvi_iotlb_reset(s);
        }

        entry->domid = domid;
        entry->perms = to_cache.perm;
        entry->translated_addr = to_cache.translated_addr;
        entry->page_mask = to_cache.addr_mask;
        entry->devid = devid;

        g_hash_table_replace(s->iotlb, key, entry);
    }
}

static void amdvi_completion_wait(AMDVIState *s, uint64_t *cmd)
{
    /* pad the last 3 bits */
    hwaddr addr = cpu_to_le64(extract64(cmd[0], 3, 49)) << 3;
    uint64_t data = cpu_to_le64(cmd[1]);

    if (extract64(cmd[0], 52, 8)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }
    if (extract64(cmd[0], 0, 1)) {
        if (dma_memory_write(&address_space_memory, addr, &data,
                             AMDVI_COMPLETION_DATA_SIZE,
                             MEMTXATTRS_UNSPECIFIED)) {
            trace_amdvi_completion_wait_fail(addr);
        }
    }
    /* set completion interrupt */
    if (extract64(cmd[0], 1, 1)) {
        amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_COMP_INT);
        /* generate interrupt */
        amdvi_generate_msi_interrupt(s);
    }
    trace_amdvi_completion_wait(addr, data);
}

static inline uint64_t amdvi_get_perms(uint64_t entry)
{
    return (entry & (AMDVI_DEV_PERM_READ | AMDVI_DEV_PERM_WRITE)) >>
           AMDVI_DEV_PERM_SHIFT;
}

/* validate that reserved bits are honoured */
static bool amdvi_validate_dte(AMDVIState *s, uint16_t devid,
                               uint64_t *dte)
{

    uint64_t root;

    if ((dte[0] & AMDVI_DTE_QUAD0_RESERVED) ||
        (dte[1] & AMDVI_DTE_QUAD1_RESERVED) ||
        (dte[2] & AMDVI_DTE_QUAD2_RESERVED) ||
        (dte[3] & AMDVI_DTE_QUAD3_RESERVED)) {
        amdvi_log_illegaldevtab_error(s, devid,
                                      s->devtab +
                                      devid * AMDVI_DEVTAB_ENTRY_SIZE, 0);
        return false;
    }

    /*
     * 1 = Host Address Translation is not supported. Value in MMIO Offset
     * 0030h[HATS] is not meaningful. A non-zero host page table root pointer
     * in the DTE would result in an ILLEGAL_DEV_TABLE_ENTRY event.
     */
    root = (dte[0] & AMDVI_DEV_PT_ROOT_MASK) >> 12;
    if (root && !s->iommu.dma_translation) {
        amdvi_log_illegaldevtab_error(s, devid,
                                      s->devtab +
                                      devid * AMDVI_DEVTAB_ENTRY_SIZE, 0);
        return false;
    }

    return true;
}

/* get a device table entry given the devid */
static bool amdvi_get_dte(AMDVIState *s, int devid, uint64_t *entry)
{
    uint32_t offset = devid * AMDVI_DEVTAB_ENTRY_SIZE;

    if (dma_memory_read(&address_space_memory, s->devtab + offset, entry,
                        AMDVI_DEVTAB_ENTRY_SIZE, MEMTXATTRS_UNSPECIFIED)) {
        trace_amdvi_dte_get_fail(s->devtab, offset);
        /* log error accessing dte */
        amdvi_log_devtab_error(s, devid, s->devtab + offset, 0);
        return false;
    }

    *entry = le64_to_cpu(*entry);
    if (!amdvi_validate_dte(s, devid, entry)) {
        trace_amdvi_invalid_dte(entry[0]);
        return false;
    }

    return true;
}

/* get pte translation mode */
static inline uint8_t get_pte_translation_mode(uint64_t pte)
{
    return (pte >> AMDVI_DEV_MODE_RSHIFT) & AMDVI_DEV_MODE_MASK;
}

static inline uint64_t amdvi_get_pte_entry(AMDVIState *s, uint64_t pte_addr,
                                          uint16_t devid)
{
    uint64_t pte;

    if (dma_memory_read(&address_space_memory, pte_addr,
                        &pte, sizeof(pte), MEMTXATTRS_UNSPECIFIED)) {
        trace_amdvi_get_pte_hwerror(pte_addr);
        amdvi_log_pagetab_error(s, devid, pte_addr, 0);
        pte = (uint64_t)-1;
        return pte;
    }

    pte = le64_to_cpu(pte);
    return pte;
}

static int amdvi_as_to_dte(AMDVIAddressSpace *as, uint64_t *dte)
{
    uint16_t devid = PCI_BUILD_BDF(pci_bus_num(as->bus), as->devfn);
    AMDVIState *s = as->iommu_state;

    if (!amdvi_get_dte(s, devid, dte)) {
        /* Unable to retrieve DTE for devid */
        return -AMDVI_FR_DTE_RTR_ERR;
    }

    if (!(dte[0] & AMDVI_DEV_VALID)) {
        /* DTE[V] not set, address is passed untranslated for devid */
        return -AMDVI_FR_DTE_V;
    }

    if (!(dte[0] & AMDVI_DEV_TRANSLATION_VALID)) {
        /* DTE[TV] not set, host page table not valid for devid */
        return -AMDVI_FR_DTE_TV;
    }
    return 0;
}

/*
 * For a PTE encoding a large page, return the page size it encodes as described
 * by the AMD IOMMU Specification Table 14: Example Page Size Encodings.
 * No need to adjust the value of the PTE to point to the first PTE in the large
 * page since the encoding guarantees all "base" PTEs in the large page are the
 * same.
 */
static uint64_t large_pte_page_size(uint64_t pte)
{
    assert(PTE_NEXT_LEVEL(pte) == 7);

    /* Determine size of the large/contiguous page encoded in the PTE */
    return PTE_LARGE_PAGE_SIZE(pte);
}

/*
 * Helper function to fetch a PTE using AMD v1 pgtable format.
 * On successful page walk, returns 0 and pte parameter points to a valid PTE.
 * On failure, returns:
 * -AMDVI_FR_PT_ROOT_INV: A page walk is not possible due to conditions like DTE
 *      with invalid permissions, Page Table Root can not be read from DTE, or a
 *      larger IOVA than supported by page table level encoded in DTE[Mode].
 * -AMDVI_FR_PT_ENTRY_INV: A PTE could not be read from guest memory during a
 *      page table walk. This means that the DTE has valid data, but one of the
 *      lower level entries in the Page Table could not be read.
 */
static uint64_t fetch_pte(AMDVIAddressSpace *as, hwaddr address, uint64_t dte,
                          uint64_t *pte, hwaddr *page_size)
{
    IOMMUAccessFlags perms = amdvi_get_perms(dte);

    uint8_t level, mode;
    uint64_t pte_addr;

    *pte = dte;
    *page_size = 0;

    if (perms == IOMMU_NONE) {
        return -AMDVI_FR_PT_ROOT_INV;
    }

    /*
     * The Linux kernel driver initializes the default mode to 3, corresponding
     * to a 39-bit GPA space, where each entry in the pagetable translates to a
     * 1GB (2^30) page size.
     */
    level = mode = get_pte_translation_mode(dte);
    assert(mode > 0 && mode < 7);

    /*
     * If IOVA is larger than the max supported by the current pgtable level,
     * there is nothing to do.
     */
    if (address > PT_LEVEL_MAX_ADDR(mode - 1)) {
        /* IOVA too large for the current DTE */
        return -AMDVI_FR_PT_ROOT_INV;
    }

    do {
        level -= 1;

        /* Update the page_size */
        *page_size = PTE_LEVEL_PAGE_SIZE(level);

        /* Permission bits are ANDed at every level, including the DTE */
        perms &= amdvi_get_perms(*pte);
        if (perms == IOMMU_NONE) {
            return 0;
        }

        /* Not Present */
        if (!IOMMU_PTE_PRESENT(*pte)) {
            return 0;
        }

        /* Large or Leaf PTE found */
        if (PTE_NEXT_LEVEL(*pte) == 7 || PTE_NEXT_LEVEL(*pte) == 0) {
            /* Leaf PTE found */
            break;
        }

        /*
         * Index the pgtable using the IOVA bits corresponding to current level
         * and walk down to the lower level.
         */
        pte_addr = NEXT_PTE_ADDR(*pte, level, address);
        *pte = amdvi_get_pte_entry(as->iommu_state, pte_addr, as->devfn);

        if (*pte == (uint64_t)-1) {
            /*
             * A returned PTE of -1 indicates a failure to read the page table
             * entry from guest memory.
             */
            if (level == mode - 1) {
                /* Failure to retrieve the Page Table from Root Pointer */
                *page_size = 0;
                return -AMDVI_FR_PT_ROOT_INV;
            } else {
                /* Failure to read PTE. Page walk skips a page_size chunk */
                return -AMDVI_FR_PT_ENTRY_INV;
            }
        }
    } while (level > 0);

    assert(PTE_NEXT_LEVEL(*pte) == 0 || PTE_NEXT_LEVEL(*pte) == 7 ||
           level == 0);
    /*
     * Page walk ends when Next Level field on PTE shows that either a leaf PTE
     * or a series of large PTEs have been reached. In the latter case, even if
     * the range starts in the middle of a contiguous page, the returned PTE
     * must be the first PTE of the series.
     */
    if (PTE_NEXT_LEVEL(*pte) == 7) {
        /* Update page_size with the large PTE page size */
        *page_size = large_pte_page_size(*pte);
    }

    return 0;
}

/*
 * Invoke notifiers registered for the address space. Update record of mapped
 * ranges in IOVA Tree.
 */
static void amdvi_notify_iommu(AMDVIAddressSpace *as, IOMMUTLBEvent *event)
{
    IOMMUTLBEntry *entry = &event->entry;

    DMAMap target = {
        .iova = entry->iova,
        .size = entry->addr_mask,
        .translated_addr = entry->translated_addr,
        .perm = entry->perm,
    };

    /*
     * Search the IOVA Tree for an existing translation for the target, and skip
     * the notification if the mapping is already recorded.
     * When the guest uses large pages, comparing against the record makes it
     * possible to determine the size of the original MAP and adjust the UNMAP
     * request to match it. This avoids failed checks against the mappings kept
     * by the VFIO kernel driver.
     */
    const DMAMap *mapped = iova_tree_find(as->iova_tree, &target);

    if (event->type == IOMMU_NOTIFIER_UNMAP) {
        if (!mapped) {
            /* No record exists of this mapping, nothing to do */
            return;
        }
        /*
         * Adjust the size based on the original record. This is essential to
         * determine when large/contiguous pages are used, since the guest has
         * already cleared the PTE (erasing the pagesize encoded on it) before
         * issuing the invalidation command.
         */
        if (mapped->size != target.size) {
            assert(mapped->size > target.size);
            target.size = mapped->size;
            /* Adjust event to invoke notifier with correct range */
            entry->addr_mask = mapped->size;
        }
        iova_tree_remove(as->iova_tree, target);
    } else { /* IOMMU_NOTIFIER_MAP */
        if (mapped) {
            /*
             * If a mapping is present and matches the request, skip the
             * notification.
             */
            if (!memcmp(mapped, &target, sizeof(DMAMap))) {
                return;
            } else {
                /*
                 * This should never happen unless a buggy guest OS omits or
                 * sends incorrect invalidation(s). Report an error in the event
                 * it does happen.
                 */
                error_report("Found conflicting translation. This could be due "
                             "to an incorrect or missing invalidation command");
            }
        }
        /* Record the new mapping */
        iova_tree_insert(as->iova_tree, &target);
    }

    /* Invoke the notifiers registered for this address space */
    memory_region_notify_iommu(&as->iommu, 0, *event);
}

/*
 * Walk the guest page table for an IOVA and range and signal the registered
 * notifiers to sync the shadow page tables in the host.
 * Must be called with a valid DTE for DMA remapping i.e. V=1,TV=1
 */
static void amdvi_sync_shadow_page_table_range(AMDVIAddressSpace *as,
                                               uint64_t *dte, hwaddr addr,
                                               uint64_t size, bool send_unmap)
{
    IOMMUTLBEvent event;

    hwaddr page_mask, pagesize;
    hwaddr iova = addr;
    hwaddr end = iova + size - 1;

    uint64_t pte;
    int ret;

    while (iova < end) {

        ret = fetch_pte(as, iova, dte[0], &pte, &pagesize);

        if (ret == -AMDVI_FR_PT_ROOT_INV) {
            /*
             * Invalid conditions such as the IOVA being larger than supported
             * by current page table mode as configured in the DTE, or a failure
             * to fetch the Page Table from the Page Table Root Pointer in DTE.
             */
            assert(pagesize == 0);
            return;
        }
        /* PTE has been validated for major errors and pagesize is set */
        assert(pagesize);
        page_mask = ~(pagesize - 1);

        if (ret == -AMDVI_FR_PT_ENTRY_INV) {
            /*
             * Failure to read PTE from memory, the pagesize matches the current
             * level. Unable to determine the region type, so a safe strategy is
             * to skip the range and continue the page walk.
             */
            goto next;
        }

        event.entry.target_as = &address_space_memory;
        event.entry.iova = iova & page_mask;
        /* translated_addr is irrelevant for the unmap case */
        event.entry.translated_addr = (pte & AMDVI_DEV_PT_ROOT_MASK) &
                                      page_mask;
        event.entry.addr_mask = ~page_mask;
        event.entry.perm = amdvi_get_perms(pte);

        /*
         * In cases where the leaf PTE is not found, or it has invalid
         * permissions, an UNMAP type notification is sent, but only if the
         * caller requested it.
         */
        if (!IOMMU_PTE_PRESENT(pte) || (event.entry.perm == IOMMU_NONE)) {
            if (!send_unmap) {
                goto next;
            }
            event.type = IOMMU_NOTIFIER_UNMAP;
        } else {
            event.type = IOMMU_NOTIFIER_MAP;
        }

        /*
         * The following call might need to adjust event.entry.size in cases
         * where the guest unmapped a series of large pages.
         */
        amdvi_notify_iommu(as, &event);
        /*
         * In the special scenario where the guest is unmapping a large page,
         * addr_mask has been adjusted before sending the notification. Update
         * pagesize accordingly in order to correctly compute the next IOVA.
         */
        pagesize = event.entry.addr_mask + 1;

next:
        iova &= ~(pagesize - 1);

        /* Check for 64-bit overflow and terminate walk in such cases */
        if ((iova + pagesize) < iova) {
            break;
        } else {
            iova += pagesize;
        }
    }
}

/*
 * Unmap entire range that the notifier registered for i.e. the full AS.
 *
 * This is seemingly technically equivalent to directly calling
 * memory_region_unmap_iommu_notifier_range(), but it allows to check for
 * notifier boundaries and issue notifications with ranges within those bounds.
 */
static void amdvi_address_space_unmap(AMDVIAddressSpace *as, IOMMUNotifier *n)
{

    hwaddr start = n->start;
    hwaddr end = n->end;
    hwaddr remain;
    DMAMap map;

    assert(start <= end);
    remain = end - start + 1;

    /*
     * Divide the notifier range into chunks that are aligned and do not exceed
     * the notifier boundaries.
     */
    while (remain >= AMDVI_PAGE_SIZE) {

        IOMMUTLBEvent event;

        uint64_t mask = dma_aligned_pow2_mask(start, end, 64);

        event.type = IOMMU_NOTIFIER_UNMAP;

        IOMMUTLBEntry entry = {
            .target_as = &address_space_memory,
            .iova = start,
            .translated_addr = 0,   /* irrelevant for unmap case */
            .addr_mask = mask,
            .perm = IOMMU_NONE,
        };
        event.entry = entry;

        /* Call notifier registered for updates on this address space */
        memory_region_notify_iommu_one(n, &event);

        start += mask + 1;
        remain -= mask + 1;
    }

    assert(!remain);

    map.iova = n->start;
    map.size = n->end - n->start;

    iova_tree_remove(as->iova_tree, map);
}

/*
 * For all the address spaces with notifiers registered, unmap the entire range
 * the notifier registered for i.e. clear all the address spaces managed by the
 * IOMMU.
 */
static void amdvi_address_space_unmap_all(AMDVIState *s)
{
    AMDVIAddressSpace *as;
    IOMMUNotifier *n;

    QLIST_FOREACH(as, &s->amdvi_as_with_notifiers, next) {
        IOMMU_NOTIFIER_FOREACH(n, &as->iommu) {
            amdvi_address_space_unmap(as, n);
        }
    }
}

/*
 * For every translation present in the IOMMU, construct IOMMUTLBEntry data
 * and pass it as parameter to notifier callback.
 */
static void amdvi_iommu_replay(IOMMUMemoryRegion *iommu_mr, IOMMUNotifier *n)
{
    AMDVIAddressSpace *as = container_of(iommu_mr, AMDVIAddressSpace, iommu);
    uint64_t dte[4] = { 0 };

    if (!(n->notifier_flags & IOMMU_NOTIFIER_MAP)) {
        return;
    }

    if (amdvi_as_to_dte(as, dte)) {
        return;
    }

    /* Dropping all mappings for the address space. Also clears the IOVA tree */
    amdvi_address_space_unmap(as, n);

    amdvi_sync_shadow_page_table_range(as, &dte[0], 0, UINT64_MAX, false);
}

static void amdvi_address_space_sync(AMDVIAddressSpace *as)
{
    IOMMUNotifier *n;
    uint64_t dte[4] = { 0 };

    /* If only UNMAP notifiers are registered, drop all existing mappings */
    if (!(as->notifier_flags & IOMMU_NOTIFIER_MAP)) {
        IOMMU_NOTIFIER_FOREACH(n, &as->iommu) {
            /*
             * Directly calling memory_region_unmap_iommu_notifier_range() does
             * not guarantee that the addr_mask eventually passed as parameter
             * to the notifier is valid. Use amdvi_address_space_unmap() which
             * ensures the notifier range is divided into properly aligned
             * regions, and issues notifications for each one.
             */
            amdvi_address_space_unmap(as, n);
        }
        return;
    }

    if (amdvi_as_to_dte(as, dte)) {
        return;
    }

    amdvi_sync_shadow_page_table_range(as, &dte[0], 0, UINT64_MAX, true);
}

/*
 * This differs from the replay() method in that it issues both MAP and UNMAP
 * notifications since it is called after global invalidation events in order to
 * re-sync all address spaces.
 */
static void amdvi_iommu_address_space_sync_all(AMDVIState *s)
{
    AMDVIAddressSpace *as;

    QLIST_FOREACH(as, &s->amdvi_as_with_notifiers, next) {
        amdvi_address_space_sync(as);
    }
}

/*
 * Toggle between address translation and passthrough modes by enabling the
 * corresponding memory regions.
 */
static void amdvi_switch_address_space(AMDVIAddressSpace *amdvi_as)
{
    AMDVIState *s = amdvi_as->iommu_state;

    if (s->dma_remap && amdvi_as->addr_translation) {
        /* Enabling DMA region */
        memory_region_set_enabled(&amdvi_as->iommu_nodma, false);
        memory_region_set_enabled(MEMORY_REGION(&amdvi_as->iommu), true);
    } else {
        /* Disabling DMA region, using passthrough */
        memory_region_set_enabled(MEMORY_REGION(&amdvi_as->iommu), false);
        memory_region_set_enabled(&amdvi_as->iommu_nodma, true);
    }
}

/*
 * For all existing address spaces managed by the IOMMU, enable/disable the
 * corresponding memory regions to reset the address translation mode and
 * use passthrough by default.
 */
static void amdvi_reset_address_translation_all(AMDVIState *s)
{
    AMDVIAddressSpace *iommu_as;
    GHashTableIter as_it;

    g_hash_table_iter_init(&as_it, s->address_spaces);

    while (g_hash_table_iter_next(&as_it, NULL, (void **)&iommu_as)) {
        /* Use passthrough as default mode after reset */
        iommu_as->addr_translation = false;
        amdvi_switch_address_space(iommu_as);
    }
}

static void enable_dma_mode(AMDVIAddressSpace *as, bool inval_current)
{
    /*
     * When enabling DMA mode for the purpose of isolating guest devices on
     * a failure to retrieve or invalid DTE, all existing mappings must be
     * dropped.
     */
    if (inval_current) {
        IOMMUNotifier *n;
        IOMMU_NOTIFIER_FOREACH(n, &as->iommu) {
            amdvi_address_space_unmap(as, n);
        }
    }

    if (as->addr_translation) {
        return;
    }

    /* Installing DTE enabling translation, activate region */
    as->addr_translation = true;
    amdvi_switch_address_space(as);
    /* Sync shadow page tables */
    amdvi_address_space_sync(as);
}

/*
 * If paging was previously in use in the address space
 * - invalidate all existing mappings
 * - switch to no_dma memory region
 */
static void enable_nodma_mode(AMDVIAddressSpace *as)
{
    IOMMUNotifier *n;

    if (!as->addr_translation) {
        /* passthrough is already active, nothing to do */
        return;
    }

    as->addr_translation = false;
    IOMMU_NOTIFIER_FOREACH(n, &as->iommu) {
        /* Drop all mappings for the address space */
        amdvi_address_space_unmap(as, n);
    }
    amdvi_switch_address_space(as);
}

/*
 * A guest driver must issue the INVALIDATE_DEVTAB_ENTRY command to the IOMMU
 * after changing a Device Table entry. We can use this fact to detect when a
 * Device Table entry is created for a device attached to a paging domain and
 * enable the corresponding IOMMU memory region to allow for DMA translation if
 * appropriate.
 */
static void amdvi_update_addr_translation_mode(AMDVIState *s, uint16_t devid)
{
    uint8_t dte_mode;
    AMDVIAddressSpace *as;
    uint64_t dte[4] = { 0 };
    int ret;

    as = amdvi_get_as_by_devid(s, devid);
    if (!as) {
        return;
    }

    ret = amdvi_as_to_dte(as, dte);

    if (!ret) {
        dte_mode = (dte[0] >> AMDVI_DEV_MODE_RSHIFT) & AMDVI_DEV_MODE_MASK;
    }

    switch (ret) {
    case 0:
        /* DTE was successfully retrieved */
        if (!dte_mode) {
            enable_nodma_mode(as); /* DTE[V]=1 && DTE[Mode]=0 => passthrough */
        } else {
            enable_dma_mode(as, false); /* Enable DMA translation */
        }
        break;
    case -AMDVI_FR_DTE_V:
        /* DTE[V]=0, address is passed untranslated */
        enable_nodma_mode(as);
        break;
    case -AMDVI_FR_DTE_RTR_ERR:
    case -AMDVI_FR_DTE_TV:
        /*
         * Enforce isolation by using DMA in rare scenarios where the DTE cannot
         * be retrieved or DTE[TV]=0. Existing mappings are dropped.
         */
        enable_dma_mode(as, true);
        break;
    }
}

/* log error without aborting since linux seems to be using reserved bits */
static void amdvi_inval_devtab_entry(AMDVIState *s, uint64_t *cmd)
{
    uint16_t devid = cpu_to_le16((uint16_t)extract64(cmd[0], 0, 16));

    trace_amdvi_devtab_inval(PCI_BUS_NUM(devid), PCI_SLOT(devid),
                             PCI_FUNC(devid));

    /* This command should invalidate internal caches of which there isn't */
    if (extract64(cmd[0], 16, 44) || cmd[1]) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
        return;
    }

    /*
     * When DMA remapping capability is enabled, check if updated DTE is setup
     * for paging or not, and configure the corresponding memory regions.
     */
    if (s->dma_remap) {
        amdvi_update_addr_translation_mode(s, devid);
    }
}

static void amdvi_complete_ppr(AMDVIState *s, uint64_t *cmd)
{
    if (extract64(cmd[0], 16, 16) ||  extract64(cmd[0], 52, 8) ||
        extract64(cmd[1], 0, 2) || extract64(cmd[1], 3, 29)
        || extract64(cmd[1], 48, 16)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }
    trace_amdvi_ppr_exec();
}

static void amdvi_intremap_inval_notify_all(AMDVIState *s, bool global,
                               uint32_t index, uint32_t mask)
{
    x86_iommu_iec_notify_all(X86_IOMMU_DEVICE(s), global, index, mask);
}

static void amdvi_inval_all(AMDVIState *s, uint64_t *cmd)
{
    if (extract64(cmd[0], 0, 60) || cmd[1]) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }

    /* Notify global invalidation */
    amdvi_intremap_inval_notify_all(s, true, 0, 0);

    amdvi_iotlb_reset(s);

    /*
     * Fully replay the address space i.e. send both UNMAP and MAP events in
     * order to synchronize guest and host IO page tables tables.
     */
    amdvi_iommu_address_space_sync_all(s);

    trace_amdvi_all_inval();
}

static gboolean amdvi_iotlb_remove_by_domid(gpointer key, gpointer value,
                                            gpointer user_data)
{
    AMDVIIOTLBEntry *entry = (AMDVIIOTLBEntry *)value;
    uint16_t domid = *(uint16_t *)user_data;
    return entry->domid == domid;
}

/*
 * Helper to decode the size of the range to invalidate encoded in the
 * INVALIDATE_IOMMU_PAGES Command format.
 * The size of the region to invalidate depends on the S bit and address.
 * S bit value:
 * 0 :  Invalidation size is 4 Kbytes.
 * 1 :  Invalidation size is determined by first zero bit in the address
 *      starting from Address[12].
 *
 * In the AMD IOMMU Linux driver, an invalidation command with address
 * ((1 << 63) - 1) is sent when intending to clear the entire cache.
 * However, Table 14: Example Page Size Encodings shows that an address of
 * ((1ULL << 51) - 1) encodes the entire cache, so effectively any address with
 * first zero at bit 51 or larger is a request to invalidate the entire address
 * space.
 */
static uint64_t amdvi_decode_invalidation_size(hwaddr addr, uint16_t flags)
{
    uint64_t size = AMDVI_PAGE_SIZE;
    uint8_t fzbit = 0;

    if (flags & AMDVI_CMD_INVAL_IOMMU_PAGES_S) {
        fzbit = cto64(addr | 0xFFF);

        if (fzbit >= 51) {
            size = AMDVI_INV_ALL_PAGES;
        } else {
            size = 1ULL << (fzbit + 1);
        }
    }
    return size;
}

/*
 * Synchronize the guest page tables with the shadow page tables kept in the
 * host for the specified range.
 * The invalidation command issued by the guest and intercepted by the VMM
 * does not specify a device, but a domain, since all devices in the same domain
 * share the same page tables. However, vIOMMU emulation creates separate
 * address spaces per device, so it is necessary to traverse the list of all of
 * address spaces (i.e. devices) that have notifiers registered in order to
 * propagate the changes to the host page tables.
 * We cannot return early from this function once a matching domain has been
 * identified and its page tables synced (based on the fact that all devices in
 * the same domain share the page tables). The reason is that different devices
 * (i.e. address spaces) could have different notifiers registered, and by
 * skipping address spaces that appear later on the amdvi_as_with_notifiers list
 * their notifiers (which could differ from the ones registered for the first
 * device/address space) would not be invoked.
 */
static void amdvi_sync_domain(AMDVIState *s, uint16_t domid, uint64_t addr,
                              uint16_t flags)
{
    AMDVIAddressSpace *as;

    uint64_t size = amdvi_decode_invalidation_size(addr, flags);

    if (size == AMDVI_INV_ALL_PAGES) {
        addr = 0;       /* Set start address to 0 and invalidate entire AS */
    } else {
        addr &= ~(size - 1);
    }

    /*
     * Call notifiers that have registered for each address space matching the
     * domain ID, in order to sync the guest pagetable state with the host.
     */
    QLIST_FOREACH(as, &s->amdvi_as_with_notifiers, next) {

        uint64_t dte[4] = { 0 };

        /*
         * Retrieve the Device Table entry for the devid corresponding to the
         * current address space, and verify the DomainID matches i.e. the page
         * tables to be synced belong to devices in the domain.
         */
        if (amdvi_as_to_dte(as, dte)) {
            continue;
        }

        /* Only need to sync the Page Tables for a matching domain */
        if (domid != (dte[1] & AMDVI_DEV_DOMID_ID_MASK)) {
            continue;
        }

        /*
         * We have determined that there is a valid Device Table Entry for a
         * device matching the DomainID in the INV_IOMMU_PAGES command issued by
         * the guest. Walk the guest page table to sync shadow page table.
         */
        if (as->notifier_flags & IOMMU_NOTIFIER_MAP) {
            /* Sync guest IOMMU mappings with host */
            amdvi_sync_shadow_page_table_range(as, &dte[0], addr, size, true);
        }
    }
}

/* we don't have devid - we can't remove pages by address */
static void amdvi_inval_pages(AMDVIState *s, uint64_t *cmd)
{
    uint16_t domid = cpu_to_le16((uint16_t)extract64(cmd[0], 32, 16));
    uint64_t addr = cpu_to_le64(extract64(cmd[1], 12, 52)) << 12;
    uint16_t flags = cpu_to_le16((uint16_t)extract64(cmd[1], 0, 3));

    if (extract64(cmd[0], 20, 12) || extract64(cmd[0], 48, 12) ||
        extract64(cmd[1], 3, 9)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }

    g_hash_table_foreach_remove(s->iotlb, amdvi_iotlb_remove_by_domid,
                                &domid);

    amdvi_sync_domain(s, domid, addr, flags);
    trace_amdvi_pages_inval(domid);
}

static void amdvi_prefetch_pages(AMDVIState *s, uint64_t *cmd)
{
    if (extract64(cmd[0], 16, 8) || extract64(cmd[0], 52, 8) ||
        extract64(cmd[1], 1, 1) || extract64(cmd[1], 3, 1) ||
        extract64(cmd[1], 5, 7)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }

    trace_amdvi_prefetch_pages();
}

static void amdvi_inval_inttable(AMDVIState *s, uint64_t *cmd)
{
    if (extract64(cmd[0], 16, 44) || cmd[1]) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
        return;
    }

    /* Notify global invalidation */
    amdvi_intremap_inval_notify_all(s, true, 0, 0);

    trace_amdvi_intr_inval();
}

/* FIXME: Try to work with the specified size instead of all the pages
 * when the S bit is on
 */
static void iommu_inval_iotlb(AMDVIState *s, uint64_t *cmd)
{

    uint16_t devid = cpu_to_le16(extract64(cmd[0], 0, 16));
    if (extract64(cmd[1], 1, 1) || extract64(cmd[1], 3, 1) ||
        extract64(cmd[1], 6, 6)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
        return;
    }

    if (extract64(cmd[1], 0, 1)) {
        g_hash_table_foreach_remove(s->iotlb, amdvi_iotlb_remove_by_devid,
                                    &devid);
    } else {
        amdvi_iotlb_remove_page(s, cpu_to_le64(extract64(cmd[1], 12, 52)) << 12,
                                devid);
    }
    trace_amdvi_iotlb_inval();
}

/* not honouring reserved bits is regarded as an illegal command */
static void amdvi_cmdbuf_exec(AMDVIState *s)
{
    uint64_t cmd[2];

    if (dma_memory_read(&address_space_memory, s->cmdbuf + s->cmdbuf_head,
                        cmd, AMDVI_COMMAND_SIZE, MEMTXATTRS_UNSPECIFIED)) {
        trace_amdvi_command_read_fail(s->cmdbuf, s->cmdbuf_head);
        amdvi_log_command_error(s, s->cmdbuf + s->cmdbuf_head);
        return;
    }

    switch (extract64(cmd[0], 60, 4)) {
    case AMDVI_CMD_COMPLETION_WAIT:
        amdvi_completion_wait(s, cmd);
        break;
    case AMDVI_CMD_INVAL_DEVTAB_ENTRY:
        amdvi_inval_devtab_entry(s, cmd);
        break;
    case AMDVI_CMD_INVAL_AMDVI_PAGES:
        amdvi_inval_pages(s, cmd);
        break;
    case AMDVI_CMD_INVAL_IOTLB_PAGES:
        iommu_inval_iotlb(s, cmd);
        break;
    case AMDVI_CMD_INVAL_INTR_TABLE:
        amdvi_inval_inttable(s, cmd);
        break;
    case AMDVI_CMD_PREFETCH_AMDVI_PAGES:
        amdvi_prefetch_pages(s, cmd);
        break;
    case AMDVI_CMD_COMPLETE_PPR_REQUEST:
        amdvi_complete_ppr(s, cmd);
        break;
    case AMDVI_CMD_INVAL_AMDVI_ALL:
        amdvi_inval_all(s, cmd);
        break;
    default:
        trace_amdvi_unhandled_command(extract64(cmd[1], 60, 4));
        /* log illegal command */
        amdvi_log_illegalcom_error(s, extract64(cmd[1], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }
}

static void amdvi_cmdbuf_run(AMDVIState *s)
{
    if (!s->cmdbuf_enabled) {
        trace_amdvi_command_error(amdvi_readq(s, AMDVI_MMIO_CONTROL));
        return;
    }

    /* check if there is work to do. */
    while (s->cmdbuf_head != s->cmdbuf_tail) {
        trace_amdvi_command_exec(s->cmdbuf_head, s->cmdbuf_tail, s->cmdbuf);
        amdvi_cmdbuf_exec(s);
        s->cmdbuf_head += AMDVI_COMMAND_SIZE;
        amdvi_writeq_raw(s, AMDVI_MMIO_COMMAND_HEAD, s->cmdbuf_head);

        /* wrap head pointer */
        if (s->cmdbuf_head >= s->cmdbuf_len * AMDVI_COMMAND_SIZE) {
            s->cmdbuf_head = 0;
        }
    }
}

static inline uint8_t amdvi_mmio_get_index(hwaddr addr)
{
    uint8_t index = (addr & ~0x2000) / 8;

    if ((addr & 0x2000)) {
        /* high table */
        index = index >= AMDVI_MMIO_REGS_HIGH ? AMDVI_MMIO_REGS_HIGH : index;
    } else {
        index = index >= AMDVI_MMIO_REGS_LOW ? AMDVI_MMIO_REGS_LOW : index;
    }

    return index;
}

static void amdvi_mmio_trace_read(hwaddr addr, unsigned size)
{
    uint8_t index = amdvi_mmio_get_index(addr);
    trace_amdvi_mmio_read(amdvi_mmio_low[index], addr, size, addr & ~0x07);
}

static void amdvi_mmio_trace_write(hwaddr addr, unsigned size, uint64_t val)
{
    uint8_t index = amdvi_mmio_get_index(addr);
    trace_amdvi_mmio_write(amdvi_mmio_low[index], addr, size, val,
                           addr & ~0x07);
}

static uint64_t amdvi_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    AMDVIState *s = opaque;

    uint64_t val = -1;
    if (addr + size > AMDVI_MMIO_SIZE) {
        trace_amdvi_mmio_read_invalid(AMDVI_MMIO_SIZE, addr, size);
        return (uint64_t)-1;
    }

    if (size == 2) {
        val = amdvi_readw(s, addr);
    } else if (size == 4) {
        val = amdvi_readl(s, addr);
    } else if (size == 8) {
        val = amdvi_readq(s, addr);
    }
    amdvi_mmio_trace_read(addr, size);

    return val;
}

static void amdvi_handle_control_write(AMDVIState *s)
{
    unsigned long control = amdvi_readq(s, AMDVI_MMIO_CONTROL);
    s->enabled = !!(control & AMDVI_MMIO_CONTROL_AMDVIEN);

    s->evtlog_enabled = s->enabled && !!(control &
                        AMDVI_MMIO_CONTROL_EVENTLOGEN);

    s->evtlog_intr = !!(control & AMDVI_MMIO_CONTROL_EVENTINTEN);
    s->completion_wait_intr = !!(control & AMDVI_MMIO_CONTROL_COMWAITINTEN);
    s->cmdbuf_enabled = s->enabled && !!(control &
                        AMDVI_MMIO_CONTROL_CMDBUFLEN);
    s->ga_enabled = !!(control & AMDVI_MMIO_CONTROL_GAEN);

    /* update the flags depending on the control register */
    if (s->cmdbuf_enabled) {
        amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_CMDBUF_RUN);
    } else {
        amdvi_assign_andq(s, AMDVI_MMIO_STATUS, ~AMDVI_MMIO_STATUS_CMDBUF_RUN);
    }
    if (s->evtlog_enabled) {
        amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_EVT_RUN);
    } else {
        amdvi_assign_andq(s, AMDVI_MMIO_STATUS, ~AMDVI_MMIO_STATUS_EVT_RUN);
    }

    trace_amdvi_control_status(control);
    amdvi_cmdbuf_run(s);
}

static inline void amdvi_handle_devtab_write(AMDVIState *s)

{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_DEVICE_TABLE);
    s->devtab = (val & AMDVI_MMIO_DEVTAB_BASE_MASK);

    /* set device table length (i.e. number of entries table can hold) */
    s->devtab_len = (((val & AMDVI_MMIO_DEVTAB_SIZE_MASK) + 1) *
                    (AMDVI_MMIO_DEVTAB_SIZE_UNIT /
                     AMDVI_MMIO_DEVTAB_ENTRY_SIZE));
}

static inline void amdvi_handle_cmdhead_write(AMDVIState *s)
{
    s->cmdbuf_head = amdvi_readq(s, AMDVI_MMIO_COMMAND_HEAD)
                     & AMDVI_MMIO_CMDBUF_HEAD_MASK;
    amdvi_cmdbuf_run(s);
}

static inline void amdvi_handle_cmdbase_write(AMDVIState *s)
{
    s->cmdbuf = amdvi_readq(s, AMDVI_MMIO_COMMAND_BASE)
                & AMDVI_MMIO_CMDBUF_BASE_MASK;
    s->cmdbuf_len = 1UL << (amdvi_readq(s, AMDVI_MMIO_CMDBUF_SIZE_BYTE)
                    & AMDVI_MMIO_CMDBUF_SIZE_MASK);
    s->cmdbuf_head = s->cmdbuf_tail = 0;
}

static inline void amdvi_handle_cmdtail_write(AMDVIState *s)
{
    s->cmdbuf_tail = amdvi_readq(s, AMDVI_MMIO_COMMAND_TAIL)
                     & AMDVI_MMIO_CMDBUF_TAIL_MASK;
    amdvi_cmdbuf_run(s);
}

static inline void amdvi_handle_excllim_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_EXCL_LIMIT);
    s->excl_limit = (val & AMDVI_MMIO_EXCL_LIMIT_MASK) |
                    AMDVI_MMIO_EXCL_LIMIT_LOW;
}

static inline void amdvi_handle_evtbase_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_EVENT_BASE);

    if (amdvi_readq(s, AMDVI_MMIO_STATUS) & AMDVI_MMIO_STATUS_EVENT_INT)
        /* Do not reset if eventlog interrupt bit is set*/
        return;

    s->evtlog = val & AMDVI_MMIO_EVTLOG_BASE_MASK;
    s->evtlog_len = 1UL << (amdvi_readq(s, AMDVI_MMIO_EVTLOG_SIZE_BYTE)
                    & AMDVI_MMIO_EVTLOG_SIZE_MASK);

    /* clear tail and head pointer to 0 when event base is updated */
    s->evtlog_tail = s->evtlog_head = 0;
    amdvi_writeq_raw(s, AMDVI_MMIO_EVENT_HEAD, s->evtlog_head);
    amdvi_writeq_raw(s, AMDVI_MMIO_EVENT_TAIL, s->evtlog_tail);
}

static inline void amdvi_handle_evttail_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_EVENT_TAIL);
    s->evtlog_tail = val & AMDVI_MMIO_EVTLOG_TAIL_MASK;
}

static inline void amdvi_handle_evthead_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_EVENT_HEAD);
    s->evtlog_head = val & AMDVI_MMIO_EVTLOG_HEAD_MASK;
}

static inline void amdvi_handle_pprbase_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_PPR_BASE);
    s->ppr_log = val & AMDVI_MMIO_PPRLOG_BASE_MASK;
    s->pprlog_len = 1UL << (amdvi_readq(s, AMDVI_MMIO_PPRLOG_SIZE_BYTE)
                    & AMDVI_MMIO_PPRLOG_SIZE_MASK);
}

static inline void amdvi_handle_pprhead_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_PPR_HEAD);
    s->pprlog_head = val & AMDVI_MMIO_PPRLOG_HEAD_MASK;
}

static inline void amdvi_handle_pprtail_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_PPR_TAIL);
    s->pprlog_tail = val & AMDVI_MMIO_PPRLOG_TAIL_MASK;
}

/* FIXME: something might go wrong if System Software writes in chunks
 * of one byte but linux writes in chunks of 4 bytes so currently it
 * works correctly with linux but will definitely be busted if software
 * reads/writes 8 bytes
 */
static void amdvi_mmio_reg_write(AMDVIState *s, unsigned size, uint64_t val,
                                 hwaddr addr)
{
    if (size == 2) {
        amdvi_writew(s, addr, val);
    } else if (size == 4) {
        amdvi_writel(s, addr, val);
    } else if (size == 8) {
        amdvi_writeq(s, addr, val);
    }
}

static void amdvi_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    AMDVIState *s = opaque;
    unsigned long offset = addr & 0x07;

    if (addr + size > AMDVI_MMIO_SIZE) {
        trace_amdvi_mmio_write("error: addr outside region: max ",
                (uint64_t)AMDVI_MMIO_SIZE, size, val, offset);
        return;
    }

    amdvi_mmio_trace_write(addr, size, val);
    switch (addr & ~0x07) {
    case AMDVI_MMIO_CONTROL:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_control_write(s);
        break;
    case AMDVI_MMIO_DEVICE_TABLE:
        amdvi_mmio_reg_write(s, size, val, addr);
       /*  set device table address
        *   This also suffers from inability to tell whether software
        *   is done writing
        */
        if (offset || (size == 8)) {
            amdvi_handle_devtab_write(s);
        }
        break;
    case AMDVI_MMIO_COMMAND_HEAD:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_cmdhead_write(s);
        break;
    case AMDVI_MMIO_COMMAND_BASE:
        amdvi_mmio_reg_write(s, size, val, addr);
        /* FIXME - make sure System Software has finished writing in case
         * it writes in chucks less than 8 bytes in a robust way.As for
         * now, this hacks works for the linux driver
         */
        if (offset || (size == 8)) {
            amdvi_handle_cmdbase_write(s);
        }
        break;
    case AMDVI_MMIO_COMMAND_TAIL:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_cmdtail_write(s);
        break;
    case AMDVI_MMIO_EVENT_BASE:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_evtbase_write(s);
        break;
    case AMDVI_MMIO_EVENT_HEAD:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_evthead_write(s);
        break;
    case AMDVI_MMIO_EVENT_TAIL:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_evttail_write(s);
        break;
    case AMDVI_MMIO_EXCL_LIMIT:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_excllim_write(s);
        break;
        /* PPR log base - unused for now */
    case AMDVI_MMIO_PPR_BASE:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_pprbase_write(s);
        break;
        /* PPR log head - also unused for now */
    case AMDVI_MMIO_PPR_HEAD:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_pprhead_write(s);
        break;
        /* PPR log tail - unused for now */
    case AMDVI_MMIO_PPR_TAIL:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_pprtail_write(s);
        break;
    case AMDVI_MMIO_STATUS:
        amdvi_mmio_reg_write(s, size, val, addr);
        break;
    }
}

static void amdvi_page_walk(AMDVIAddressSpace *as, uint64_t *dte,
                            IOMMUTLBEntry *ret, unsigned perms,
                            hwaddr addr)
{
    hwaddr page_mask, pagesize = 0;
    uint8_t mode;
    uint64_t pte;
    int fetch_ret;

    /* make sure the DTE has TV = 1 */
    if (!(dte[0] & AMDVI_DEV_TRANSLATION_VALID)) {
        /*
         * A DTE with V=1, TV=0 does not have a valid Page Table Root Pointer.
         * An IOMMU processing a request that requires a table walk terminates
         * the walk when it encounters this condition. Do the same and return
         * instead of assuming that the address is forwarded without translation
         * i.e. the passthrough case, as it is done for the case where DTE[V]=0.
         */
        return;
    }

    mode = get_pte_translation_mode(dte[0]);
    if (mode >= 7) {
        trace_amdvi_mode_invalid(mode, addr);
        return;
    }
    if (mode == 0) {
        goto no_remap;
    }

    /* Attempt to fetch the PTE to determine if a valid mapping exists */
    fetch_ret = fetch_pte(as, addr, dte[0], &pte, &pagesize);

    /*
     * If walking the page table results in an error of any type, returns an
     * empty PTE i.e. no mapping, or the permissions do not match, return since
     * there is no translation available.
     */
    if (fetch_ret < 0 || !IOMMU_PTE_PRESENT(pte) ||
        perms != (perms & amdvi_get_perms(pte))) {

        amdvi_page_fault(as->iommu_state, as->devfn, addr, perms);
        trace_amdvi_page_fault(addr);
        return;
    }

    /* A valid PTE and page size has been retrieved */
    assert(pagesize);
    page_mask = ~(pagesize - 1);

    /* get access permissions from pte */
    ret->iova = addr & page_mask;
    ret->translated_addr = (pte & AMDVI_DEV_PT_ROOT_MASK) & page_mask;
    ret->addr_mask = ~page_mask;
    ret->perm = amdvi_get_perms(pte);
    return;

no_remap:
    ret->iova = addr & AMDVI_PAGE_MASK_4K;
    ret->translated_addr = addr & AMDVI_PAGE_MASK_4K;
    ret->addr_mask = ~AMDVI_PAGE_MASK_4K;
    ret->perm = amdvi_get_perms(dte[0]);
}

static void amdvi_do_translate(AMDVIAddressSpace *as, hwaddr addr,
                               bool is_write, IOMMUTLBEntry *ret)
{
    AMDVIState *s = as->iommu_state;
    uint16_t devid = PCI_BUILD_BDF(pci_bus_num(as->bus), as->devfn);
    AMDVIIOTLBEntry *iotlb_entry = amdvi_iotlb_lookup(s, addr, devid);
    uint64_t entry[4];
    int dte_ret;

    if (iotlb_entry) {
        trace_amdvi_iotlb_hit(PCI_BUS_NUM(devid), PCI_SLOT(devid),
                PCI_FUNC(devid), addr, iotlb_entry->translated_addr);
        ret->iova = addr & ~iotlb_entry->page_mask;
        ret->translated_addr = iotlb_entry->translated_addr;
        ret->addr_mask = iotlb_entry->page_mask;
        ret->perm = iotlb_entry->perms;
        return;
    }

    dte_ret = amdvi_as_to_dte(as, entry);

    if (dte_ret < 0) {
        if (dte_ret == -AMDVI_FR_DTE_V) {
            /* DTE[V]=0, address is passed untranslated */
            goto out;
        }
        return;
    }

    amdvi_page_walk(as, entry, ret,
                    is_write ? AMDVI_PERM_WRITE : AMDVI_PERM_READ, addr);

    amdvi_update_iotlb(s, devid, addr, *ret,
                       entry[1] & AMDVI_DEV_DOMID_ID_MASK);
    return;

out:
    ret->iova = addr & AMDVI_PAGE_MASK_4K;
    ret->translated_addr = addr & AMDVI_PAGE_MASK_4K;
    ret->addr_mask = ~AMDVI_PAGE_MASK_4K;
    ret->perm = IOMMU_RW;
}

static inline bool amdvi_is_interrupt_addr(hwaddr addr)
{
    return addr >= AMDVI_INT_ADDR_FIRST && addr <= AMDVI_INT_ADDR_LAST;
}

static IOMMUTLBEntry amdvi_translate(IOMMUMemoryRegion *iommu, hwaddr addr,
                                     IOMMUAccessFlags flag, int iommu_idx)
{
    AMDVIAddressSpace *as = container_of(iommu, AMDVIAddressSpace, iommu);
    AMDVIState *s = as->iommu_state;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE
    };

    if (!s->enabled) {
        /* AMDVI disabled - corresponds to iommu=off not
         * failure to provide any parameter
         */
        ret.iova = addr & AMDVI_PAGE_MASK_4K;
        ret.translated_addr = addr & AMDVI_PAGE_MASK_4K;
        ret.addr_mask = ~AMDVI_PAGE_MASK_4K;
        ret.perm = IOMMU_RW;
        return ret;
    } else if (amdvi_is_interrupt_addr(addr)) {
        ret.iova = addr & AMDVI_PAGE_MASK_4K;
        ret.translated_addr = addr & AMDVI_PAGE_MASK_4K;
        ret.addr_mask = ~AMDVI_PAGE_MASK_4K;
        ret.perm = IOMMU_WO;
        return ret;
    }

    amdvi_do_translate(as, addr, flag & IOMMU_WO, &ret);
    trace_amdvi_translation_result(pci_bus_num(as->bus), PCI_SLOT(as->devfn),
            PCI_FUNC(as->devfn), addr, ret.translated_addr);
    return ret;
}

static int amdvi_get_irte(AMDVIState *s, MSIMessage *origin, uint64_t *dte,
                          union irte *irte, uint16_t devid)
{
    uint64_t irte_root, offset;

    irte_root = dte[2] & AMDVI_IR_PHYS_ADDR_MASK;
    offset = (origin->data & AMDVI_IRTE_OFFSET) << 2;

    trace_amdvi_ir_irte(irte_root, offset);

    if (dma_memory_read(&address_space_memory, irte_root + offset,
                        irte, sizeof(*irte), MEMTXATTRS_UNSPECIFIED)) {
        trace_amdvi_ir_err("failed to get irte");
        return -AMDVI_IR_GET_IRTE;
    }

    trace_amdvi_ir_irte_val(irte->val);

    return 0;
}

static int amdvi_int_remap_legacy(AMDVIState *iommu,
                                  MSIMessage *origin,
                                  MSIMessage *translated,
                                  uint64_t *dte,
                                  X86IOMMUIrq *irq,
                                  uint16_t sid)
{
    int ret;
    union irte irte;

    /* get interrupt remapping table */
    ret = amdvi_get_irte(iommu, origin, dte, &irte, sid);
    if (ret < 0) {
        return ret;
    }

    if (!irte.fields.valid) {
        trace_amdvi_ir_target_abort("RemapEn is disabled");
        return -AMDVI_IR_TARGET_ABORT;
    }

    if (irte.fields.guest_mode) {
        error_report_once("guest mode is not zero");
        return -AMDVI_IR_ERR;
    }

    if (irte.fields.int_type > AMDVI_IOAPIC_INT_TYPE_ARBITRATED) {
        error_report_once("reserved int_type");
        return -AMDVI_IR_ERR;
    }

    irq->delivery_mode = irte.fields.int_type;
    irq->vector = irte.fields.vector;
    irq->dest_mode = irte.fields.dm;
    irq->redir_hint = irte.fields.rq_eoi;
    irq->dest = irte.fields.destination;

    return 0;
}

static int amdvi_get_irte_ga(AMDVIState *s, MSIMessage *origin, uint64_t *dte,
                             struct irte_ga *irte, uint16_t devid)
{
    uint64_t irte_root, offset;

    irte_root = dte[2] & AMDVI_IR_PHYS_ADDR_MASK;
    offset = (origin->data & AMDVI_IRTE_OFFSET) << 4;
    trace_amdvi_ir_irte(irte_root, offset);

    if (dma_memory_read(&address_space_memory, irte_root + offset,
                        irte, sizeof(*irte), MEMTXATTRS_UNSPECIFIED)) {
        trace_amdvi_ir_err("failed to get irte_ga");
        return -AMDVI_IR_GET_IRTE;
    }

    trace_amdvi_ir_irte_ga_val(irte->hi.val, irte->lo.val);
    return 0;
}

static int amdvi_int_remap_ga(AMDVIState *iommu,
                              MSIMessage *origin,
                              MSIMessage *translated,
                              uint64_t *dte,
                              X86IOMMUIrq *irq,
                              uint16_t sid)
{
    int ret;
    struct irte_ga irte;

    /* get interrupt remapping table */
    ret = amdvi_get_irte_ga(iommu, origin, dte, &irte, sid);
    if (ret < 0) {
        return ret;
    }

    if (!irte.lo.fields_remap.valid) {
        trace_amdvi_ir_target_abort("RemapEn is disabled");
        return -AMDVI_IR_TARGET_ABORT;
    }

    if (irte.lo.fields_remap.guest_mode) {
        error_report_once("guest mode is not zero");
        return -AMDVI_IR_ERR;
    }

    if (irte.lo.fields_remap.int_type > AMDVI_IOAPIC_INT_TYPE_ARBITRATED) {
        error_report_once("reserved int_type is set");
        return -AMDVI_IR_ERR;
    }

    irq->delivery_mode = irte.lo.fields_remap.int_type;
    irq->vector = irte.hi.fields.vector;
    irq->dest_mode = irte.lo.fields_remap.dm;
    irq->redir_hint = irte.lo.fields_remap.rq_eoi;
    if (iommu->xtsup) {
        irq->dest = irte.lo.fields_remap.destination |
                    (irte.hi.fields.destination_hi << 24);
    } else {
        irq->dest = irte.lo.fields_remap.destination & 0xff;
    }

    return 0;
}

static int __amdvi_int_remap_msi(AMDVIState *iommu,
                                 MSIMessage *origin,
                                 MSIMessage *translated,
                                 uint64_t *dte,
                                 X86IOMMUIrq *irq,
                                 uint16_t sid)
{
    int ret;
    uint8_t int_ctl;

    int_ctl = (dte[2] >> AMDVI_IR_INTCTL_SHIFT) & 3;
    trace_amdvi_ir_intctl(int_ctl);

    switch (int_ctl) {
    case AMDVI_IR_INTCTL_PASS:
        memcpy(translated, origin, sizeof(*origin));
        return 0;
    case AMDVI_IR_INTCTL_REMAP:
        break;
    case AMDVI_IR_INTCTL_ABORT:
        trace_amdvi_ir_target_abort("int_ctl abort");
        return -AMDVI_IR_TARGET_ABORT;
    default:
        trace_amdvi_ir_err("int_ctl reserved");
        return -AMDVI_IR_ERR;
    }

    if (iommu->ga_enabled) {
        ret = amdvi_int_remap_ga(iommu, origin, translated, dte, irq, sid);
    } else {
        ret = amdvi_int_remap_legacy(iommu, origin, translated, dte, irq, sid);
    }

    return ret;
}

/* Interrupt remapping for MSI/MSI-X entry */
static int amdvi_int_remap_msi(AMDVIState *iommu,
                               MSIMessage *origin,
                               MSIMessage *translated,
                               uint16_t sid)
{
    int ret = 0;
    uint64_t pass = 0;
    uint64_t dte[4] = { 0 };
    X86IOMMUIrq irq = { 0 };
    uint8_t dest_mode, delivery_mode;

    assert(origin && translated);

    /*
     * When IOMMU is enabled, interrupt remap request will come either from
     * IO-APIC or PCI device. If interrupt is from PCI device then it will
     * have a valid requester id but if the interrupt is from IO-APIC
     * then requester id will be invalid.
     */
    if (sid == X86_IOMMU_SID_INVALID) {
        sid = AMDVI_IOAPIC_SB_DEVID;
    }

    trace_amdvi_ir_remap_msi_req(origin->address, origin->data, sid);

    /* check if device table entry is set before we go further. */
    if (!iommu || !iommu->devtab_len) {
        memcpy(translated, origin, sizeof(*origin));
        goto out;
    }

    if (!amdvi_get_dte(iommu, sid, dte)) {
        return -AMDVI_IR_ERR;
    }

    /* Check if IR is enabled in DTE */
    if (!(dte[2] & AMDVI_IR_REMAP_ENABLE)) {
        memcpy(translated, origin, sizeof(*origin));
        goto out;
    }

    /* validate that we are configure with intremap=on */
    if (!x86_iommu_ir_supported(X86_IOMMU_DEVICE(iommu))) {
        trace_amdvi_err("Interrupt remapping is enabled in the guest but "
                        "not in the host. Use intremap=on to enable interrupt "
                        "remapping in amd-iommu.");
        return -AMDVI_IR_ERR;
    }

    if (origin->address < AMDVI_INT_ADDR_FIRST ||
        origin->address + sizeof(origin->data) > AMDVI_INT_ADDR_LAST + 1) {
        trace_amdvi_err("MSI is not from IOAPIC.");
        return -AMDVI_IR_ERR;
    }

    /*
     * The MSI data register [10:8] are used to get the upstream interrupt type.
     *
     * See MSI/MSI-X format:
     * https://pdfs.semanticscholar.org/presentation/9420/c279e942eca568157711ef5c92b800c40a79.pdf
     * (page 5)
     */
    delivery_mode = (origin->data >> MSI_DATA_DELIVERY_MODE_SHIFT) & 7;

    switch (delivery_mode) {
    case AMDVI_IOAPIC_INT_TYPE_FIXED:
    case AMDVI_IOAPIC_INT_TYPE_ARBITRATED:
        trace_amdvi_ir_delivery_mode("fixed/arbitrated");
        ret = __amdvi_int_remap_msi(iommu, origin, translated, dte, &irq, sid);
        if (ret < 0) {
            goto remap_fail;
        } else {
            /* Translate IRQ to MSI messages */
            x86_iommu_irq_to_msi_message(&irq, translated);
            goto out;
        }
        break;
    case AMDVI_IOAPIC_INT_TYPE_SMI:
        error_report("SMI is not supported!");
        ret = -AMDVI_IR_ERR;
        break;
    case AMDVI_IOAPIC_INT_TYPE_NMI:
        pass = dte[2] & AMDVI_DEV_NMI_PASS_MASK;
        trace_amdvi_ir_delivery_mode("nmi");
        break;
    case AMDVI_IOAPIC_INT_TYPE_INIT:
        pass = dte[2] & AMDVI_DEV_INT_PASS_MASK;
        trace_amdvi_ir_delivery_mode("init");
        break;
    case AMDVI_IOAPIC_INT_TYPE_EINT:
        pass = dte[2] & AMDVI_DEV_EINT_PASS_MASK;
        trace_amdvi_ir_delivery_mode("eint");
        break;
    default:
        trace_amdvi_ir_delivery_mode("unsupported delivery_mode");
        ret = -AMDVI_IR_ERR;
        break;
    }

    if (ret < 0) {
        goto remap_fail;
    }

    /*
     * The MSI address register bit[2] is used to get the destination
     * mode. The dest_mode 1 is valid for fixed and arbitrated interrupts
     * only.
     */
    dest_mode = (origin->address >> MSI_ADDR_DEST_MODE_SHIFT) & 1;
    if (dest_mode) {
        trace_amdvi_ir_err("invalid dest_mode");
        ret = -AMDVI_IR_ERR;
        goto remap_fail;
    }

    if (pass) {
        memcpy(translated, origin, sizeof(*origin));
    } else {
        trace_amdvi_ir_err("passthrough is not enabled");
        ret = -AMDVI_IR_ERR;
        goto remap_fail;
    }

out:
    trace_amdvi_ir_remap_msi(origin->address, origin->data,
                             translated->address, translated->data);
    return 0;

remap_fail:
    return ret;
}

static int amdvi_int_remap(X86IOMMUState *iommu,
                           MSIMessage *origin,
                           MSIMessage *translated,
                           uint16_t sid)
{
    return amdvi_int_remap_msi(AMD_IOMMU_DEVICE(iommu), origin,
                               translated, sid);
}

static MemTxResult amdvi_mem_ir_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    int ret;
    MSIMessage from = { 0, 0 }, to = { 0, 0 };
    uint16_t sid = AMDVI_IOAPIC_SB_DEVID;

    from.address = (uint64_t) addr + AMDVI_INT_ADDR_FIRST;
    from.data = (uint32_t) value;

    trace_amdvi_mem_ir_write_req(addr, value, size);

    if (!attrs.unspecified) {
        /* We have explicit Source ID */
        sid = attrs.requester_id;
    }

    ret = amdvi_int_remap_msi(opaque, &from, &to, sid);
    if (ret < 0) {
        /* TODO: log the event using IOMMU log event interface */
        error_report_once("failed to remap interrupt from devid 0x%x", sid);
        return MEMTX_ERROR;
    }

    apic_get_class(NULL)->send_msi(&to);

    trace_amdvi_mem_ir_write(to.address, to.data);
    return MEMTX_OK;
}

static MemTxResult amdvi_mem_ir_read(void *opaque, hwaddr addr,
                                     uint64_t *data, unsigned size,
                                     MemTxAttrs attrs)
{
    return MEMTX_OK;
}

static const MemoryRegionOps amdvi_ir_ops = {
    .read_with_attrs = amdvi_mem_ir_read,
    .write_with_attrs = amdvi_mem_ir_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static AddressSpace *amdvi_host_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    char name[128];
    AMDVIState *s = opaque;
    AMDVIAddressSpace *amdvi_dev_as;
    AMDVIAsKey *key;

    amdvi_dev_as = amdvi_as_lookup(s, bus, devfn);

    /* allocate memory during the first run */
    if (!amdvi_dev_as) {
        snprintf(name, sizeof(name), "amd_iommu_devfn_%d", devfn);

        amdvi_dev_as = g_new0(AMDVIAddressSpace, 1);
        key = g_new0(AMDVIAsKey, 1);

        amdvi_dev_as->bus = bus;
        amdvi_dev_as->devfn = (uint8_t)devfn;
        amdvi_dev_as->iommu_state = s;
        amdvi_dev_as->notifier_flags = IOMMU_NOTIFIER_NONE;
        amdvi_dev_as->iova_tree = iova_tree_new();
        amdvi_dev_as->addr_translation = false;
        key->bus = bus;
        key->devfn = devfn;

        g_hash_table_insert(s->address_spaces, key, amdvi_dev_as);

        /*
         * Memory region relationships looks like (Address range shows
         * only lower 32 bits to make it short in length...):
         *
         * |--------------------+-------------------+----------|
         * | Name               | Address range     | Priority |
         * |--------------------+-------------------+----------+
         * | amdvi-root         | 00000000-ffffffff |        0 |
         * |  amdvi-iommu_nodma  | 00000000-ffffffff |       0 |
         * |  amdvi-iommu_ir     | fee00000-feefffff |       1 |
         * |--------------------+-------------------+----------|
         */
        memory_region_init_iommu(&amdvi_dev_as->iommu,
                                 sizeof(amdvi_dev_as->iommu),
                                 TYPE_AMD_IOMMU_MEMORY_REGION,
                                 OBJECT(s),
                                 "amd_iommu", UINT64_MAX);
        memory_region_init(&amdvi_dev_as->root, OBJECT(s),
                           "amdvi_root", UINT64_MAX);
        address_space_init(&amdvi_dev_as->as, &amdvi_dev_as->root, name);
        memory_region_add_subregion_overlap(&amdvi_dev_as->root, 0,
                                            MEMORY_REGION(&amdvi_dev_as->iommu),
                                            0);

        /* Build the DMA Disabled alias to shared memory */
        memory_region_init_alias(&amdvi_dev_as->iommu_nodma, OBJECT(s),
                                 "amdvi-sys", &s->mr_sys, 0,
                                 memory_region_size(&s->mr_sys));
        memory_region_add_subregion_overlap(&amdvi_dev_as->root, 0,
                                            &amdvi_dev_as->iommu_nodma,
                                            0);
        /* Build the Interrupt Remapping alias to shared memory */
        memory_region_init_alias(&amdvi_dev_as->iommu_ir, OBJECT(s),
                                 "amdvi-ir", &s->mr_ir, 0,
                                 memory_region_size(&s->mr_ir));
        memory_region_add_subregion_overlap(MEMORY_REGION(&amdvi_dev_as->iommu),
                                            AMDVI_INT_ADDR_FIRST,
                                            &amdvi_dev_as->iommu_ir, 1);

        amdvi_switch_address_space(amdvi_dev_as);
    }
    return &amdvi_dev_as->as;
}

static const PCIIOMMUOps amdvi_iommu_ops = {
    .get_address_space = amdvi_host_dma_iommu,
};

static const MemoryRegionOps mmio_mem_ops = {
    .read = amdvi_mmio_read,
    .write = amdvi_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    }
};

static int amdvi_iommu_notify_flag_changed(IOMMUMemoryRegion *iommu,
                                           IOMMUNotifierFlag old,
                                           IOMMUNotifierFlag new,
                                           Error **errp)
{
    AMDVIAddressSpace *as = container_of(iommu, AMDVIAddressSpace, iommu);
    AMDVIState *s = as->iommu_state;

    /*
     * Accurate synchronization of the vIOMMU page tables required to support
     * MAP notifiers is provided by the dma-remap feature. In addition, this
     * also requires that the vIOMMU presents the NpCache capability, so a guest
     * driver issues invalidations for both map() and unmap() operations. The
     * capability is already set by default as part of AMDVI_CAPAB_FEATURES and
     * written to the configuration in amdvi_pci_realize().
     */
    if (!s->dma_remap && (new & IOMMU_NOTIFIER_MAP)) {
        error_setg_errno(errp, ENOTSUP,
            "device %02x.%02x.%x requires dma-remap=1",
            pci_bus_num(as->bus), PCI_SLOT(as->devfn), PCI_FUNC(as->devfn));
        return -ENOTSUP;
    }

    /*
     * Update notifier flags for address space and the list of address spaces
     * with registered notifiers.
     */
    as->notifier_flags = new;

    if (old == IOMMU_NOTIFIER_NONE) {
        QLIST_INSERT_HEAD(&s->amdvi_as_with_notifiers, as, next);
    } else if (new == IOMMU_NOTIFIER_NONE) {
        QLIST_REMOVE(as, next);
    }

    return 0;
}

static void amdvi_init(AMDVIState *s)
{
    amdvi_iotlb_reset(s);

    s->devtab_len = 0;
    s->cmdbuf_len = 0;
    s->cmdbuf_head = 0;
    s->cmdbuf_tail = 0;
    s->evtlog_head = 0;
    s->evtlog_tail = 0;
    s->excl_enabled = false;
    s->excl_allow = false;
    s->mmio_enabled = false;
    s->enabled = false;
    s->cmdbuf_enabled = false;

    /* reset MMIO */
    memset(s->mmior, 0, AMDVI_MMIO_SIZE);
    amdvi_set_quad(s, AMDVI_MMIO_EXT_FEATURES,
                   amdvi_extended_feature_register(s),
                   0xffffffffffffffef, 0);
    amdvi_set_quad(s, AMDVI_MMIO_STATUS, 0, 0x98, 0x67);
}

static void amdvi_pci_realize(PCIDevice *pdev, Error **errp)
{
    AMDVIPCIState *s = AMD_IOMMU_PCI(pdev);
    int ret;

    ret = pci_add_capability(pdev, AMDVI_CAPAB_ID_SEC, 0,
                             AMDVI_CAPAB_SIZE, errp);
    if (ret < 0) {
        return;
    }
    s->capab_offset = ret;

    ret = pci_add_capability(pdev, PCI_CAP_ID_MSI, 0,
                             AMDVI_CAPAB_REG_SIZE, errp);
    if (ret < 0) {
        return;
    }
    ret = pci_add_capability(pdev, PCI_CAP_ID_HT, 0,
                             AMDVI_CAPAB_REG_SIZE, errp);
    if (ret < 0) {
        return;
    }

    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* reset device ident */
    pci_config_set_prog_interface(pdev->config, 0);

    /* reset AMDVI specific capabilities, all r/o */
    pci_set_long(pdev->config + s->capab_offset, AMDVI_CAPAB_FEATURES);
    pci_set_long(pdev->config + s->capab_offset + AMDVI_CAPAB_BAR_LOW,
                 AMDVI_BASE_ADDR & MAKE_64BIT_MASK(14, 18));
    pci_set_long(pdev->config + s->capab_offset + AMDVI_CAPAB_BAR_HIGH,
                AMDVI_BASE_ADDR >> 32);
    pci_set_long(pdev->config + s->capab_offset + AMDVI_CAPAB_RANGE,
                 0xff000000);
    pci_set_long(pdev->config + s->capab_offset + AMDVI_CAPAB_MISC, 0);
    pci_set_long(pdev->config + s->capab_offset + AMDVI_CAPAB_MISC,
            AMDVI_MAX_PH_ADDR | AMDVI_MAX_GVA_ADDR | AMDVI_MAX_VA_ADDR);
}

static void amdvi_sysbus_reset(DeviceState *dev)
{
    AMDVIState *s = AMD_IOMMU_DEVICE(dev);

    msi_reset(&s->pci->dev);
    amdvi_init(s);

    /* Discard all mappings on device reset */
    amdvi_address_space_unmap_all(s);
    amdvi_reset_address_translation_all(s);
}

static const VMStateDescription vmstate_amdvi_sysbus_migratable = {
    .name = "amd-iommu",
    .version_id = 1,
    .minimum_version_id = 1,
    .priority = MIG_PRI_IOMMU,
    .fields = (VMStateField[]) {
      /* Updated in  amdvi_handle_control_write() */
      VMSTATE_BOOL(enabled, AMDVIState),
      VMSTATE_BOOL(ga_enabled, AMDVIState),
      /* bool ats_enabled is obsolete */
      VMSTATE_UNUSED(1), /* was ats_enabled */
      VMSTATE_BOOL(cmdbuf_enabled, AMDVIState),
      VMSTATE_BOOL(completion_wait_intr, AMDVIState),
      VMSTATE_BOOL(evtlog_enabled, AMDVIState),
      VMSTATE_BOOL(evtlog_intr, AMDVIState),
      /* Updated in amdvi_handle_devtab_write() */
      VMSTATE_UINT64(devtab, AMDVIState),
      VMSTATE_UINT64(devtab_len, AMDVIState),
      /* Updated in amdvi_handle_cmdbase_write() */
      VMSTATE_UINT64(cmdbuf, AMDVIState),
      VMSTATE_UINT64(cmdbuf_len, AMDVIState),
      /* Updated in amdvi_handle_cmdhead_write() */
      VMSTATE_UINT32(cmdbuf_head, AMDVIState),
      /* Updated in amdvi_handle_cmdtail_write() */
      VMSTATE_UINT32(cmdbuf_tail, AMDVIState),
      /* Updated in amdvi_handle_evtbase_write() */
      VMSTATE_UINT64(evtlog, AMDVIState),
      VMSTATE_UINT32(evtlog_len, AMDVIState),
      /* Updated in amdvi_handle_evthead_write() */
      VMSTATE_UINT32(evtlog_head, AMDVIState),
      /* Updated in amdvi_handle_evttail_write() */
      VMSTATE_UINT32(evtlog_tail, AMDVIState),
      /* Updated in amdvi_handle_pprbase_write() */
      VMSTATE_UINT64(ppr_log, AMDVIState),
      VMSTATE_UINT32(pprlog_len, AMDVIState),
      /* Updated in amdvi_handle_pprhead_write() */
      VMSTATE_UINT32(pprlog_head, AMDVIState),
      /* Updated in amdvi_handle_tailhead_write() */
      VMSTATE_UINT32(pprlog_tail, AMDVIState),
      /* MMIO registers */
      VMSTATE_UINT8_ARRAY(mmior, AMDVIState, AMDVI_MMIO_SIZE),
      VMSTATE_UINT8_ARRAY(romask, AMDVIState, AMDVI_MMIO_SIZE),
      VMSTATE_UINT8_ARRAY(w1cmask, AMDVIState, AMDVI_MMIO_SIZE),
      VMSTATE_END_OF_LIST()
    }
};

static void amdvi_sysbus_realize(DeviceState *dev, Error **errp)
{
    DeviceClass *dc = (DeviceClass *) object_get_class(OBJECT(dev));
    AMDVIState *s = AMD_IOMMU_DEVICE(dev);
    MachineState *ms = MACHINE(qdev_get_machine());
    PCMachineState *pcms = PC_MACHINE(ms);
    X86MachineState *x86ms = X86_MACHINE(ms);
    PCIBus *bus = pcms->pcibus;

    if (s->pci_id) {
        PCIDevice *pdev = NULL;
        int ret = pci_qdev_find_device(s->pci_id, &pdev);

        if (ret) {
            error_report("Cannot find PCI device '%s'", s->pci_id);
            return;
        }

        if (!object_dynamic_cast(OBJECT(pdev), TYPE_AMD_IOMMU_PCI)) {
            error_report("Device '%s' must be an AMDVI-PCI device type", s->pci_id);
            return;
        }

        s->pci = AMD_IOMMU_PCI(pdev);
        dc->vmsd = &vmstate_amdvi_sysbus_migratable;
    } else {
        s->pci = AMD_IOMMU_PCI(object_new(TYPE_AMD_IOMMU_PCI));
        /* This device should take care of IOMMU PCI properties */
        if (!qdev_realize(DEVICE(s->pci), &bus->qbus, errp)) {
            return;
        }
    }

    s->iotlb = g_hash_table_new_full(amdvi_iotlb_hash,
                                     amdvi_iotlb_equal, g_free, g_free);

    s->address_spaces = g_hash_table_new_full(amdvi_as_hash,
                                     amdvi_as_equal, g_free, g_free);

    /* set up MMIO */
    memory_region_init_io(&s->mr_mmio, OBJECT(s), &mmio_mem_ops, s,
                          "amdvi-mmio", AMDVI_MMIO_SIZE);
    memory_region_add_subregion(get_system_memory(), AMDVI_BASE_ADDR,
                                &s->mr_mmio);

    /* Create the share memory regions by all devices */
    memory_region_init(&s->mr_sys, OBJECT(s), "amdvi-sys", UINT64_MAX);

    /* set up the DMA disabled memory region */
    memory_region_init_alias(&s->mr_nodma, OBJECT(s),
                             "amdvi-nodma", get_system_memory(), 0,
                             memory_region_size(get_system_memory()));
    memory_region_add_subregion_overlap(&s->mr_sys, 0,
                                        &s->mr_nodma, 0);

    /* set up the Interrupt Remapping memory region */
    memory_region_init_io(&s->mr_ir, OBJECT(s), &amdvi_ir_ops,
                          s, "amdvi-ir", AMDVI_INT_ADDR_SIZE);
    memory_region_add_subregion_overlap(&s->mr_sys, AMDVI_INT_ADDR_FIRST,
                                        &s->mr_ir, 1);

    /* Pseudo address space under root PCI bus. */
    x86ms->ioapic_as = amdvi_host_dma_iommu(bus, s, AMDVI_IOAPIC_SB_DEVID);

    if (kvm_enabled() && x86ms->apic_id_limit > 255 && !s->xtsup) {
        error_report("AMD IOMMU with x2APIC configuration requires xtsup=on");
        exit(EXIT_FAILURE);
    }

    if (s->xtsup) {
        if (kvm_irqchip_is_split() && !kvm_enable_x2apic()) {
            error_report("AMD IOMMU xtsup=on requires x2APIC support on "
                          "the KVM side");
            exit(EXIT_FAILURE);
        }
    }

    pci_setup_iommu(bus, &amdvi_iommu_ops, s);
    amdvi_init(s);
}

static const Property amdvi_properties[] = {
    DEFINE_PROP_BOOL("xtsup", AMDVIState, xtsup, false),
    DEFINE_PROP_STRING("pci-id", AMDVIState, pci_id),
    DEFINE_PROP_BOOL("dma-remap", AMDVIState, dma_remap, false),
};

static const VMStateDescription vmstate_amdvi_sysbus = {
    .name = "amd-iommu",
    .unmigratable = 1
};

static void amdvi_sysbus_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    X86IOMMUClass *dc_class = X86_IOMMU_DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, amdvi_sysbus_reset);
    dc->vmsd = &vmstate_amdvi_sysbus;
    dc->hotpluggable = false;
    dc_class->realize = amdvi_sysbus_realize;
    dc_class->int_remap = amdvi_int_remap;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "AMD IOMMU (AMD-Vi) DMA Remapping device";
    device_class_set_props(dc, amdvi_properties);
}

static const TypeInfo amdvi_sysbus = {
    .name = TYPE_AMD_IOMMU_DEVICE,
    .parent = TYPE_X86_IOMMU_DEVICE,
    .instance_size = sizeof(AMDVIState),
    .class_init = amdvi_sysbus_class_init
};

static void amdvi_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_AMD;
    k->device_id = 0x1419;
    k->class_id = 0x0806;
    k->realize = amdvi_pci_realize;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "AMD IOMMU (AMD-Vi) DMA Remapping device";
}

static const TypeInfo amdvi_pci = {
    .name = TYPE_AMD_IOMMU_PCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(AMDVIPCIState),
    .class_init = amdvi_pci_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void amdvi_iommu_memory_region_class_init(ObjectClass *klass,
                                                 const void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = amdvi_translate;
    imrc->notify_flag_changed = amdvi_iommu_notify_flag_changed;
    imrc->replay = amdvi_iommu_replay;
}

static const TypeInfo amdvi_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_AMD_IOMMU_MEMORY_REGION,
    .class_init = amdvi_iommu_memory_region_class_init,
};

static void amdvi_register_types(void)
{
    type_register_static(&amdvi_pci);
    type_register_static(&amdvi_sysbus);
    type_register_static(&amdvi_iommu_memory_region_info);
}

type_init(amdvi_register_types);
