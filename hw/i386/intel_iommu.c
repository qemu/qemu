/*
 * QEMU emulation of an Intel IOMMU (VT-d)
 *   (DMA Remapping device)
 *
 * Copyright (C) 2013 Knut Omang, Oracle <knut.omang@oracle.com>
 * Copyright (C) 2014 Le Tan, <tamlokveer@gmail.com>
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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "intel_iommu_internal.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/i386/pc.h"
#include "hw/i386/apic-msidef.h"
#include "hw/i386/x86-iommu.h"
#include "hw/pci-host/q35.h"
#include "system/kvm.h"
#include "system/dma.h"
#include "system/system.h"
#include "hw/i386/apic_internal.h"
#include "kvm/kvm_i386.h"
#include "migration/vmstate.h"
#include "trace.h"

/* context entry operations */
#define VTD_CE_GET_RID2PASID(ce) \
    ((ce)->val[1] & VTD_SM_CONTEXT_ENTRY_RID2PASID_MASK)
#define VTD_CE_GET_PASID_DIR_TABLE(ce) \
    ((ce)->val[0] & VTD_PASID_DIR_BASE_ADDR_MASK)

/* pe operations */
#define VTD_PE_GET_TYPE(pe) ((pe)->val[0] & VTD_SM_PASID_ENTRY_PGTT)
#define VTD_PE_GET_FL_LEVEL(pe) \
    (4 + (((pe)->val[2] >> 2) & VTD_SM_PASID_ENTRY_FLPM))
#define VTD_PE_GET_SL_LEVEL(pe) \
    (2 + (((pe)->val[0] >> 2) & VTD_SM_PASID_ENTRY_AW))

/*
 * PCI bus number (or SID) is not reliable since the device is usaully
 * initialized before guest can configure the PCI bridge
 * (SECONDARY_BUS_NUMBER).
 */
struct vtd_as_key {
    PCIBus *bus;
    uint8_t devfn;
    uint32_t pasid;
};

/* bus/devfn is PCI device's real BDF not the aliased one */
struct vtd_hiod_key {
    PCIBus *bus;
    uint8_t devfn;
};

struct vtd_as_raw_key {
    uint16_t sid;
    uint32_t pasid;
};

struct vtd_iotlb_key {
    uint64_t gfn;
    uint32_t pasid;
    uint16_t sid;
    uint8_t level;
};

static void vtd_address_space_refresh_all(IntelIOMMUState *s);
static void vtd_address_space_unmap(VTDAddressSpace *as, IOMMUNotifier *n);

static void vtd_panic_require_caching_mode(void)
{
    error_report("We need to set caching-mode=on for intel-iommu to enable "
                 "device assignment with IOMMU protection.");
    exit(1);
}

static void vtd_define_quad(IntelIOMMUState *s, hwaddr addr, uint64_t val,
                            uint64_t wmask, uint64_t w1cmask)
{
    stq_le_p(&s->csr[addr], val);
    stq_le_p(&s->wmask[addr], wmask);
    stq_le_p(&s->w1cmask[addr], w1cmask);
}

static void vtd_define_quad_wo(IntelIOMMUState *s, hwaddr addr, uint64_t mask)
{
    stq_le_p(&s->womask[addr], mask);
}

static void vtd_define_long(IntelIOMMUState *s, hwaddr addr, uint32_t val,
                            uint32_t wmask, uint32_t w1cmask)
{
    stl_le_p(&s->csr[addr], val);
    stl_le_p(&s->wmask[addr], wmask);
    stl_le_p(&s->w1cmask[addr], w1cmask);
}

static void vtd_define_long_wo(IntelIOMMUState *s, hwaddr addr, uint32_t mask)
{
    stl_le_p(&s->womask[addr], mask);
}

/* "External" get/set operations */
static void vtd_set_quad(IntelIOMMUState *s, hwaddr addr, uint64_t val)
{
    uint64_t oldval = ldq_le_p(&s->csr[addr]);
    uint64_t wmask = ldq_le_p(&s->wmask[addr]);
    uint64_t w1cmask = ldq_le_p(&s->w1cmask[addr]);
    stq_le_p(&s->csr[addr],
             ((oldval & ~wmask) | (val & wmask)) & ~(w1cmask & val));
}

static void vtd_set_long(IntelIOMMUState *s, hwaddr addr, uint32_t val)
{
    uint32_t oldval = ldl_le_p(&s->csr[addr]);
    uint32_t wmask = ldl_le_p(&s->wmask[addr]);
    uint32_t w1cmask = ldl_le_p(&s->w1cmask[addr]);
    stl_le_p(&s->csr[addr],
             ((oldval & ~wmask) | (val & wmask)) & ~(w1cmask & val));
}

static uint64_t vtd_get_quad(IntelIOMMUState *s, hwaddr addr)
{
    uint64_t val = ldq_le_p(&s->csr[addr]);
    uint64_t womask = ldq_le_p(&s->womask[addr]);
    return val & ~womask;
}

static uint32_t vtd_get_long(IntelIOMMUState *s, hwaddr addr)
{
    uint32_t val = ldl_le_p(&s->csr[addr]);
    uint32_t womask = ldl_le_p(&s->womask[addr]);
    return val & ~womask;
}

/* "Internal" get/set operations */
static uint64_t vtd_get_quad_raw(IntelIOMMUState *s, hwaddr addr)
{
    return ldq_le_p(&s->csr[addr]);
}

static uint32_t vtd_get_long_raw(IntelIOMMUState *s, hwaddr addr)
{
    return ldl_le_p(&s->csr[addr]);
}

static void vtd_set_quad_raw(IntelIOMMUState *s, hwaddr addr, uint64_t val)
{
    stq_le_p(&s->csr[addr], val);
}

static uint32_t vtd_set_clear_mask_long(IntelIOMMUState *s, hwaddr addr,
                                        uint32_t clear, uint32_t mask)
{
    uint32_t new_val = (ldl_le_p(&s->csr[addr]) & ~clear) | mask;
    stl_le_p(&s->csr[addr], new_val);
    return new_val;
}

static uint64_t vtd_set_clear_mask_quad(IntelIOMMUState *s, hwaddr addr,
                                        uint64_t clear, uint64_t mask)
{
    uint64_t new_val = (ldq_le_p(&s->csr[addr]) & ~clear) | mask;
    stq_le_p(&s->csr[addr], new_val);
    return new_val;
}

static inline void vtd_iommu_lock(IntelIOMMUState *s)
{
    qemu_mutex_lock(&s->iommu_lock);
}

static inline void vtd_iommu_unlock(IntelIOMMUState *s)
{
    qemu_mutex_unlock(&s->iommu_lock);
}

static void vtd_update_scalable_state(IntelIOMMUState *s)
{
    uint64_t val = vtd_get_quad_raw(s, DMAR_RTADDR_REG);

    if (s->scalable_mode) {
        s->root_scalable = val & VTD_RTADDR_SMT;
    }
}

static void vtd_update_iq_dw(IntelIOMMUState *s)
{
    uint64_t val = vtd_get_quad_raw(s, DMAR_IQA_REG);

    if (s->ecap & VTD_ECAP_SMTS &&
        val & VTD_IQA_DW_MASK) {
        s->iq_dw = true;
    } else {
        s->iq_dw = false;
    }
}

/* Whether the address space needs to notify new mappings */
static inline gboolean vtd_as_has_map_notifier(VTDAddressSpace *as)
{
    return as->notifier_flags & IOMMU_NOTIFIER_MAP;
}

/* GHashTable functions */
static gboolean vtd_iotlb_equal(gconstpointer v1, gconstpointer v2)
{
    const struct vtd_iotlb_key *key1 = v1;
    const struct vtd_iotlb_key *key2 = v2;

    return key1->sid == key2->sid &&
           key1->pasid == key2->pasid &&
           key1->level == key2->level &&
           key1->gfn == key2->gfn;
}

static guint vtd_iotlb_hash(gconstpointer v)
{
    const struct vtd_iotlb_key *key = v;
    uint64_t hash64 = key->gfn | ((uint64_t)(key->sid) << VTD_IOTLB_SID_SHIFT) |
        (uint64_t)(key->level - 1) << VTD_IOTLB_LVL_SHIFT |
        (uint64_t)(key->pasid) << VTD_IOTLB_PASID_SHIFT;

    return (guint)((hash64 >> 32) ^ (hash64 & 0xffffffffU));
}

static gboolean vtd_as_equal(gconstpointer v1, gconstpointer v2)
{
    const struct vtd_as_key *key1 = v1;
    const struct vtd_as_key *key2 = v2;

    return (key1->bus == key2->bus) && (key1->devfn == key2->devfn) &&
           (key1->pasid == key2->pasid);
}

/*
 * Note that we use pointer to PCIBus as the key, so hashing/shifting
 * based on the pointer value is intended. Note that we deal with
 * collisions through vtd_as_equal().
 */
static guint vtd_as_hash(gconstpointer v)
{
    const struct vtd_as_key *key = v;
    guint value = (guint)(uintptr_t)key->bus;

    return (guint)(value << 8 | key->devfn);
}

/* Same implementation as vtd_as_hash() */
static guint vtd_hiod_hash(gconstpointer v)
{
    return vtd_as_hash(v);
}

static gboolean vtd_hiod_equal(gconstpointer v1, gconstpointer v2)
{
    const struct vtd_hiod_key *key1 = v1;
    const struct vtd_hiod_key *key2 = v2;

    return (key1->bus == key2->bus) && (key1->devfn == key2->devfn);
}

static void vtd_hiod_destroy(gpointer v)
{
    object_unref(v);
}

static gboolean vtd_hash_remove_by_domain(gpointer key, gpointer value,
                                          gpointer user_data)
{
    VTDIOTLBEntry *entry = (VTDIOTLBEntry *)value;
    uint16_t domain_id = *(uint16_t *)user_data;
    return entry->domain_id == domain_id;
}

/* The shift of an addr for a certain level of paging structure */
static inline uint32_t vtd_pt_level_shift(uint32_t level)
{
    assert(level != 0);
    return VTD_PAGE_SHIFT_4K + (level - 1) * VTD_LEVEL_BITS;
}

static inline uint64_t vtd_pt_level_page_mask(uint32_t level)
{
    return ~((1ULL << vtd_pt_level_shift(level)) - 1);
}

static gboolean vtd_hash_remove_by_page(gpointer key, gpointer value,
                                        gpointer user_data)
{
    VTDIOTLBEntry *entry = (VTDIOTLBEntry *)value;
    VTDIOTLBPageInvInfo *info = (VTDIOTLBPageInvInfo *)user_data;
    uint64_t gfn = (info->addr >> VTD_PAGE_SHIFT_4K) & info->mask;
    uint64_t gfn_tlb = (info->addr & entry->mask) >> VTD_PAGE_SHIFT_4K;

    if (entry->domain_id != info->domain_id) {
        return false;
    }

    /*
     * According to spec, IOTLB entries caching first-stage (PGTT=001b) or
     * nested (PGTT=011b) mapping associated with specified domain-id are
     * invalidated. Nested isn't supported yet, so only need to check 001b.
     */
    if (entry->pgtt == VTD_SM_PASID_ENTRY_FLT) {
        return true;
    }

    return (entry->gfn & info->mask) == gfn || entry->gfn == gfn_tlb;
}

static gboolean vtd_hash_remove_by_page_piotlb(gpointer key, gpointer value,
                                               gpointer user_data)
{
    VTDIOTLBEntry *entry = (VTDIOTLBEntry *)value;
    VTDIOTLBPageInvInfo *info = (VTDIOTLBPageInvInfo *)user_data;
    uint64_t gfn = (info->addr >> VTD_PAGE_SHIFT_4K) & info->mask;
    uint64_t gfn_tlb = (info->addr & entry->mask) >> VTD_PAGE_SHIFT_4K;

    /*
     * According to spec, PASID-based-IOTLB Invalidation in page granularity
     * doesn't invalidate IOTLB entries caching second-stage (PGTT=010b)
     * or pass-through (PGTT=100b) mappings. Nested isn't supported yet,
     * so only need to check first-stage (PGTT=001b) mappings.
     */
    if (entry->pgtt != VTD_SM_PASID_ENTRY_FLT) {
        return false;
    }

    return entry->domain_id == info->domain_id && entry->pasid == info->pasid &&
           ((entry->gfn & info->mask) == gfn || entry->gfn == gfn_tlb);
}

/* Reset all the gen of VTDAddressSpace to zero and set the gen of
 * IntelIOMMUState to 1.  Must be called with IOMMU lock held.
 */
static void vtd_reset_context_cache_locked(IntelIOMMUState *s)
{
    VTDAddressSpace *vtd_as;
    GHashTableIter as_it;

    trace_vtd_context_cache_reset();

    g_hash_table_iter_init(&as_it, s->vtd_address_spaces);

    while (g_hash_table_iter_next(&as_it, NULL, (void **)&vtd_as)) {
        vtd_as->context_cache_entry.context_cache_gen = 0;
    }
    s->context_cache_gen = 1;
}

/* Must be called with IOMMU lock held. */
static void vtd_reset_iotlb_locked(IntelIOMMUState *s)
{
    assert(s->iotlb);
    g_hash_table_remove_all(s->iotlb);
}

static void vtd_reset_iotlb(IntelIOMMUState *s)
{
    vtd_iommu_lock(s);
    vtd_reset_iotlb_locked(s);
    vtd_iommu_unlock(s);
}

static void vtd_reset_caches(IntelIOMMUState *s)
{
    vtd_iommu_lock(s);
    vtd_reset_iotlb_locked(s);
    vtd_reset_context_cache_locked(s);
    vtd_iommu_unlock(s);
}

static uint64_t vtd_get_iotlb_gfn(hwaddr addr, uint32_t level)
{
    return (addr & vtd_pt_level_page_mask(level)) >> VTD_PAGE_SHIFT_4K;
}

/* Must be called with IOMMU lock held */
static VTDIOTLBEntry *vtd_lookup_iotlb(IntelIOMMUState *s, uint16_t source_id,
                                       uint32_t pasid, hwaddr addr)
{
    struct vtd_iotlb_key key;
    VTDIOTLBEntry *entry;
    unsigned level;

    for (level = VTD_PT_LEVEL; level < VTD_PML4_LEVEL; level++) {
        key.gfn = vtd_get_iotlb_gfn(addr, level);
        key.level = level;
        key.sid = source_id;
        key.pasid = pasid;
        entry = g_hash_table_lookup(s->iotlb, &key);
        if (entry) {
            goto out;
        }
    }

out:
    return entry;
}

/* Must be with IOMMU lock held */
static void vtd_update_iotlb(IntelIOMMUState *s, uint16_t source_id,
                             uint16_t domain_id, hwaddr addr, uint64_t pte,
                             uint8_t access_flags, uint32_t level,
                             uint32_t pasid, uint8_t pgtt)
{
    VTDIOTLBEntry *entry = g_malloc(sizeof(*entry));
    struct vtd_iotlb_key *key = g_malloc(sizeof(*key));
    uint64_t gfn = vtd_get_iotlb_gfn(addr, level);

    trace_vtd_iotlb_page_update(source_id, addr, pte, domain_id);
    if (g_hash_table_size(s->iotlb) >= VTD_IOTLB_MAX_SIZE) {
        trace_vtd_iotlb_reset("iotlb exceeds size limit");
        vtd_reset_iotlb_locked(s);
    }

    entry->gfn = gfn;
    entry->domain_id = domain_id;
    entry->pte = pte;
    entry->access_flags = access_flags;
    entry->mask = vtd_pt_level_page_mask(level);
    entry->pasid = pasid;
    entry->pgtt = pgtt;

    key->gfn = gfn;
    key->sid = source_id;
    key->level = level;
    key->pasid = pasid;

    g_hash_table_replace(s->iotlb, key, entry);
}

/* Given the reg addr of both the message data and address, generate an
 * interrupt via MSI.
 */
static void vtd_generate_interrupt(IntelIOMMUState *s, hwaddr mesg_addr_reg,
                                   hwaddr mesg_data_reg)
{
    MSIMessage msi;

    assert(mesg_data_reg < DMAR_REG_SIZE);
    assert(mesg_addr_reg < DMAR_REG_SIZE);

    msi.address = vtd_get_long_raw(s, mesg_addr_reg);
    msi.data = vtd_get_long_raw(s, mesg_data_reg);

    trace_vtd_irq_generate(msi.address, msi.data);

    apic_get_class(NULL)->send_msi(&msi);
}

/* Generate a fault event to software via MSI if conditions are met.
 * Notice that the value of FSTS_REG being passed to it should be the one
 * before any update.
 */
static void vtd_generate_fault_event(IntelIOMMUState *s, uint32_t pre_fsts)
{
    if (pre_fsts & VTD_FSTS_PPF || pre_fsts & VTD_FSTS_PFO ||
        pre_fsts & VTD_FSTS_IQE) {
        error_report_once("There are previous interrupt conditions "
                          "to be serviced by software, fault event "
                          "is not generated");
        return;
    }
    vtd_set_clear_mask_long(s, DMAR_FECTL_REG, 0, VTD_FECTL_IP);
    if (vtd_get_long_raw(s, DMAR_FECTL_REG) & VTD_FECTL_IM) {
        error_report_once("Interrupt Mask set, irq is not generated");
    } else {
        vtd_generate_interrupt(s, DMAR_FEADDR_REG, DMAR_FEDATA_REG);
        vtd_set_clear_mask_long(s, DMAR_FECTL_REG, VTD_FECTL_IP, 0);
    }
}

/* Check if the Fault (F) field of the Fault Recording Register referenced by
 * @index is Set.
 */
static bool vtd_is_frcd_set(IntelIOMMUState *s, uint16_t index)
{
    /* Each reg is 128-bit */
    hwaddr addr = DMAR_FRCD_REG_OFFSET + (((uint64_t)index) << 4);
    addr += 8; /* Access the high 64-bit half */

    assert(index < DMAR_FRCD_REG_NR);

    return vtd_get_quad_raw(s, addr) & VTD_FRCD_F;
}

/* Update the PPF field of Fault Status Register.
 * Should be called whenever change the F field of any fault recording
 * registers.
 */
static void vtd_update_fsts_ppf(IntelIOMMUState *s)
{
    uint32_t i;
    uint32_t ppf_mask = 0;

    for (i = 0; i < DMAR_FRCD_REG_NR; i++) {
        if (vtd_is_frcd_set(s, i)) {
            ppf_mask = VTD_FSTS_PPF;
            break;
        }
    }
    vtd_set_clear_mask_long(s, DMAR_FSTS_REG, VTD_FSTS_PPF, ppf_mask);
    trace_vtd_fsts_ppf(!!ppf_mask);
}

static void vtd_set_frcd_and_update_ppf(IntelIOMMUState *s, uint16_t index)
{
    /* Each reg is 128-bit */
    hwaddr addr = DMAR_FRCD_REG_OFFSET + (((uint64_t)index) << 4);
    addr += 8; /* Access the high 64-bit half */

    assert(index < DMAR_FRCD_REG_NR);

    vtd_set_clear_mask_quad(s, addr, 0, VTD_FRCD_F);
    vtd_update_fsts_ppf(s);
}

/* Must not update F field now, should be done later */
static void vtd_record_frcd(IntelIOMMUState *s, uint16_t index,
                            uint64_t hi, uint64_t lo)
{
    hwaddr frcd_reg_addr = DMAR_FRCD_REG_OFFSET + (((uint64_t)index) << 4);

    assert(index < DMAR_FRCD_REG_NR);

    vtd_set_quad_raw(s, frcd_reg_addr, lo);
    vtd_set_quad_raw(s, frcd_reg_addr + 8, hi);

    trace_vtd_frr_new(index, hi, lo);
}

/* Try to collapse multiple pending faults from the same requester */
static bool vtd_try_collapse_fault(IntelIOMMUState *s, uint16_t source_id)
{
    uint32_t i;
    uint64_t frcd_reg;
    hwaddr addr = DMAR_FRCD_REG_OFFSET + 8; /* The high 64-bit half */

    for (i = 0; i < DMAR_FRCD_REG_NR; i++) {
        frcd_reg = vtd_get_quad_raw(s, addr);
        if ((frcd_reg & VTD_FRCD_F) &&
            ((frcd_reg & VTD_FRCD_SID_MASK) == source_id)) {
            return true;
        }
        addr += 16; /* 128-bit for each */
    }
    return false;
}

/* Log and report an DMAR (address translation) fault to software */
static void vtd_report_frcd_fault(IntelIOMMUState *s, uint64_t source_id,
                                  uint64_t hi, uint64_t lo)
{
    uint32_t fsts_reg = vtd_get_long_raw(s, DMAR_FSTS_REG);

    if (fsts_reg & VTD_FSTS_PFO) {
        error_report_once("New fault is not recorded due to "
                          "Primary Fault Overflow");
        return;
    }

    if (vtd_try_collapse_fault(s, source_id)) {
        error_report_once("New fault is not recorded due to "
                          "compression of faults");
        return;
    }

    if (vtd_is_frcd_set(s, s->next_frcd_reg)) {
        error_report_once("Next Fault Recording Reg is used, "
                          "new fault is not recorded, set PFO field");
        vtd_set_clear_mask_long(s, DMAR_FSTS_REG, 0, VTD_FSTS_PFO);
        return;
    }

    vtd_record_frcd(s, s->next_frcd_reg, hi, lo);

    if (fsts_reg & VTD_FSTS_PPF) {
        error_report_once("There are pending faults already, "
                          "fault event is not generated");
        vtd_set_frcd_and_update_ppf(s, s->next_frcd_reg);
        s->next_frcd_reg++;
        if (s->next_frcd_reg == DMAR_FRCD_REG_NR) {
            s->next_frcd_reg = 0;
        }
    } else {
        vtd_set_clear_mask_long(s, DMAR_FSTS_REG, VTD_FSTS_FRI_MASK,
                                VTD_FSTS_FRI(s->next_frcd_reg));
        vtd_set_frcd_and_update_ppf(s, s->next_frcd_reg); /* Will set PPF */
        s->next_frcd_reg++;
        if (s->next_frcd_reg == DMAR_FRCD_REG_NR) {
            s->next_frcd_reg = 0;
        }
        /* This case actually cause the PPF to be Set.
         * So generate fault event (interrupt).
         */
         vtd_generate_fault_event(s, fsts_reg);
    }
}

/* Log and report an DMAR (address translation) fault to software */
static void vtd_report_dmar_fault(IntelIOMMUState *s, uint16_t source_id,
                                  hwaddr addr, VTDFaultReason fault,
                                  bool is_write, bool is_pasid,
                                  uint32_t pasid)
{
    uint64_t hi, lo;

    assert(fault < VTD_FR_MAX);

    trace_vtd_dmar_fault(source_id, fault, addr, is_write);

    lo = VTD_FRCD_FI(addr);
    hi = VTD_FRCD_SID(source_id) | VTD_FRCD_FR(fault) |
         VTD_FRCD_PV(pasid) | VTD_FRCD_PP(is_pasid);
    if (!is_write) {
        hi |= VTD_FRCD_T;
    }

    vtd_report_frcd_fault(s, source_id, hi, lo);
}


static void vtd_report_ir_fault(IntelIOMMUState *s, uint64_t source_id,
                                VTDFaultReason fault, uint16_t index)
{
    uint64_t hi, lo;

    lo = VTD_FRCD_IR_IDX(index);
    hi = VTD_FRCD_SID(source_id) | VTD_FRCD_FR(fault);

    vtd_report_frcd_fault(s, source_id, hi, lo);
}

/* Handle Invalidation Queue Errors of queued invalidation interface error
 * conditions.
 */
static void vtd_handle_inv_queue_error(IntelIOMMUState *s)
{
    uint32_t fsts_reg = vtd_get_long_raw(s, DMAR_FSTS_REG);

    vtd_set_clear_mask_long(s, DMAR_FSTS_REG, 0, VTD_FSTS_IQE);
    vtd_generate_fault_event(s, fsts_reg);
}

/* Set the IWC field and try to generate an invalidation completion interrupt */
static void vtd_generate_completion_event(IntelIOMMUState *s)
{
    if (vtd_get_long_raw(s, DMAR_ICS_REG) & VTD_ICS_IWC) {
        trace_vtd_inv_desc_wait_irq("One pending, skip current");
        return;
    }
    vtd_set_clear_mask_long(s, DMAR_ICS_REG, 0, VTD_ICS_IWC);
    vtd_set_clear_mask_long(s, DMAR_IECTL_REG, 0, VTD_IECTL_IP);
    if (vtd_get_long_raw(s, DMAR_IECTL_REG) & VTD_IECTL_IM) {
        trace_vtd_inv_desc_wait_irq("IM in IECTL_REG is set, "
                                    "new event not generated");
        return;
    } else {
        /* Generate the interrupt event */
        trace_vtd_inv_desc_wait_irq("Generating complete event");
        vtd_generate_interrupt(s, DMAR_IEADDR_REG, DMAR_IEDATA_REG);
        vtd_set_clear_mask_long(s, DMAR_IECTL_REG, VTD_IECTL_IP, 0);
    }
}

static inline bool vtd_root_entry_present(IntelIOMMUState *s,
                                          VTDRootEntry *re,
                                          uint8_t devfn)
{
    if (s->root_scalable && devfn > UINT8_MAX / 2) {
        return re->hi & VTD_ROOT_ENTRY_P;
    }

    return re->lo & VTD_ROOT_ENTRY_P;
}

static int vtd_get_root_entry(IntelIOMMUState *s, uint8_t index,
                              VTDRootEntry *re)
{
    dma_addr_t addr;

    addr = s->root + index * sizeof(*re);
    if (dma_memory_read(&address_space_memory, addr,
                        re, sizeof(*re), MEMTXATTRS_UNSPECIFIED)) {
        re->lo = 0;
        return -VTD_FR_ROOT_TABLE_INV;
    }
    re->lo = le64_to_cpu(re->lo);
    re->hi = le64_to_cpu(re->hi);
    return 0;
}

static inline bool vtd_ce_present(VTDContextEntry *context)
{
    return context->lo & VTD_CONTEXT_ENTRY_P;
}

static int vtd_get_context_entry_from_root(IntelIOMMUState *s,
                                           VTDRootEntry *re,
                                           uint8_t index,
                                           VTDContextEntry *ce)
{
    dma_addr_t addr, ce_size;

    /* we have checked that root entry is present */
    ce_size = s->root_scalable ? VTD_CTX_ENTRY_SCALABLE_SIZE :
              VTD_CTX_ENTRY_LEGACY_SIZE;

    if (s->root_scalable && index > UINT8_MAX / 2) {
        index = index & (~VTD_DEVFN_CHECK_MASK);
        addr = re->hi & VTD_ROOT_ENTRY_CTP;
    } else {
        addr = re->lo & VTD_ROOT_ENTRY_CTP;
    }

    addr = addr + index * ce_size;
    if (dma_memory_read(&address_space_memory, addr,
                        ce, ce_size, MEMTXATTRS_UNSPECIFIED)) {
        return -VTD_FR_CONTEXT_TABLE_INV;
    }

    ce->lo = le64_to_cpu(ce->lo);
    ce->hi = le64_to_cpu(ce->hi);
    if (ce_size == VTD_CTX_ENTRY_SCALABLE_SIZE) {
        ce->val[2] = le64_to_cpu(ce->val[2]);
        ce->val[3] = le64_to_cpu(ce->val[3]);
    }
    return 0;
}

static inline dma_addr_t vtd_ce_get_slpt_base(VTDContextEntry *ce)
{
    return ce->lo & VTD_CONTEXT_ENTRY_SLPTPTR;
}

static inline uint64_t vtd_get_pte_addr(uint64_t pte, uint8_t aw)
{
    return pte & VTD_PT_BASE_ADDR_MASK(aw);
}

/* Whether the pte indicates the address of the page frame */
static inline bool vtd_is_last_pte(uint64_t pte, uint32_t level)
{
    return level == VTD_PT_LEVEL || (pte & VTD_PT_PAGE_SIZE_MASK);
}

/* Get the content of a pte located in @base_addr[@index] */
static uint64_t vtd_get_pte(dma_addr_t base_addr, uint32_t index)
{
    uint64_t pte;

    assert(index < VTD_PT_ENTRY_NR);

    if (dma_memory_read(&address_space_memory,
                        base_addr + index * sizeof(pte),
                        &pte, sizeof(pte), MEMTXATTRS_UNSPECIFIED)) {
        pte = (uint64_t)-1;
        return pte;
    }
    pte = le64_to_cpu(pte);
    return pte;
}

/* Given an iova and the level of paging structure, return the offset
 * of current level.
 */
static inline uint32_t vtd_iova_level_offset(uint64_t iova, uint32_t level)
{
    return (iova >> vtd_pt_level_shift(level)) &
            ((1ULL << VTD_LEVEL_BITS) - 1);
}

/* Check Capability Register to see if the @level of page-table is supported */
static inline bool vtd_is_sl_level_supported(IntelIOMMUState *s, uint32_t level)
{
    return VTD_CAP_SAGAW_MASK & s->cap &
           (1ULL << (level - 2 + VTD_CAP_SAGAW_SHIFT));
}

static inline bool vtd_is_fl_level_supported(IntelIOMMUState *s, uint32_t level)
{
    return level == VTD_PML4_LEVEL;
}

/* Return true if check passed, otherwise false */
static inline bool vtd_pe_type_check(IntelIOMMUState *s, VTDPASIDEntry *pe)
{
    switch (VTD_PE_GET_TYPE(pe)) {
    case VTD_SM_PASID_ENTRY_FLT:
        return !!(s->ecap & VTD_ECAP_FLTS);
    case VTD_SM_PASID_ENTRY_SLT:
        return !!(s->ecap & VTD_ECAP_SLTS);
    case VTD_SM_PASID_ENTRY_NESTED:
        /* Not support NESTED page table type yet */
        return false;
    case VTD_SM_PASID_ENTRY_PT:
        return !!(s->ecap & VTD_ECAP_PT);
    default:
        /* Unknown type */
        return false;
    }
}

static inline bool vtd_pdire_present(VTDPASIDDirEntry *pdire)
{
    return pdire->val & 1;
}

/**
 * Caller of this function should check present bit if wants
 * to use pdir entry for further usage except for fpd bit check.
 */
static int vtd_get_pdire_from_pdir_table(dma_addr_t pasid_dir_base,
                                         uint32_t pasid,
                                         VTDPASIDDirEntry *pdire)
{
    uint32_t index;
    dma_addr_t addr, entry_size;

    index = VTD_PASID_DIR_INDEX(pasid);
    entry_size = VTD_PASID_DIR_ENTRY_SIZE;
    addr = pasid_dir_base + index * entry_size;
    if (dma_memory_read(&address_space_memory, addr,
                        pdire, entry_size, MEMTXATTRS_UNSPECIFIED)) {
        return -VTD_FR_PASID_DIR_ACCESS_ERR;
    }

    pdire->val = le64_to_cpu(pdire->val);

    return 0;
}

static inline bool vtd_pe_present(VTDPASIDEntry *pe)
{
    return pe->val[0] & VTD_PASID_ENTRY_P;
}

static int vtd_get_pe_in_pasid_leaf_table(IntelIOMMUState *s,
                                          uint32_t pasid,
                                          dma_addr_t addr,
                                          VTDPASIDEntry *pe)
{
    uint8_t pgtt;
    uint32_t index;
    dma_addr_t entry_size;

    index = VTD_PASID_TABLE_INDEX(pasid);
    entry_size = VTD_PASID_ENTRY_SIZE;
    addr = addr + index * entry_size;
    if (dma_memory_read(&address_space_memory, addr,
                        pe, entry_size, MEMTXATTRS_UNSPECIFIED)) {
        return -VTD_FR_PASID_TABLE_ACCESS_ERR;
    }
    for (size_t i = 0; i < ARRAY_SIZE(pe->val); i++) {
        pe->val[i] = le64_to_cpu(pe->val[i]);
    }

    /* Do translation type check */
    if (!vtd_pe_type_check(s, pe)) {
        return -VTD_FR_PASID_TABLE_ENTRY_INV;
    }

    pgtt = VTD_PE_GET_TYPE(pe);
    if (pgtt == VTD_SM_PASID_ENTRY_SLT &&
        !vtd_is_sl_level_supported(s, VTD_PE_GET_SL_LEVEL(pe))) {
            return -VTD_FR_PASID_TABLE_ENTRY_INV;
    }

    if (pgtt == VTD_SM_PASID_ENTRY_FLT &&
        !vtd_is_fl_level_supported(s, VTD_PE_GET_FL_LEVEL(pe))) {
            return -VTD_FR_PASID_TABLE_ENTRY_INV;
    }

    return 0;
}

/**
 * Caller of this function should check present bit if wants
 * to use pasid entry for further usage except for fpd bit check.
 */
static int vtd_get_pe_from_pdire(IntelIOMMUState *s,
                                 uint32_t pasid,
                                 VTDPASIDDirEntry *pdire,
                                 VTDPASIDEntry *pe)
{
    dma_addr_t addr = pdire->val & VTD_PASID_TABLE_BASE_ADDR_MASK;

    return vtd_get_pe_in_pasid_leaf_table(s, pasid, addr, pe);
}

/**
 * This function gets a pasid entry from a specified pasid
 * table (includes dir and leaf table) with a specified pasid.
 * Sanity check should be done to ensure return a present
 * pasid entry to caller.
 */
static int vtd_get_pe_from_pasid_table(IntelIOMMUState *s,
                                       dma_addr_t pasid_dir_base,
                                       uint32_t pasid,
                                       VTDPASIDEntry *pe)
{
    int ret;
    VTDPASIDDirEntry pdire;

    ret = vtd_get_pdire_from_pdir_table(pasid_dir_base,
                                        pasid, &pdire);
    if (ret) {
        return ret;
    }

    if (!vtd_pdire_present(&pdire)) {
        return -VTD_FR_PASID_DIR_ENTRY_P;
    }

    ret = vtd_get_pe_from_pdire(s, pasid, &pdire, pe);
    if (ret) {
        return ret;
    }

    if (!vtd_pe_present(pe)) {
        return -VTD_FR_PASID_ENTRY_P;
    }

    return 0;
}

static int vtd_ce_get_rid2pasid_entry(IntelIOMMUState *s,
                                      VTDContextEntry *ce,
                                      VTDPASIDEntry *pe,
                                      uint32_t pasid)
{
    dma_addr_t pasid_dir_base;
    int ret = 0;

    if (pasid == PCI_NO_PASID) {
        pasid = VTD_CE_GET_RID2PASID(ce);
    }
    pasid_dir_base = VTD_CE_GET_PASID_DIR_TABLE(ce);
    ret = vtd_get_pe_from_pasid_table(s, pasid_dir_base, pasid, pe);

    return ret;
}

static int vtd_ce_get_pasid_fpd(IntelIOMMUState *s,
                                VTDContextEntry *ce,
                                bool *pe_fpd_set,
                                uint32_t pasid)
{
    int ret;
    dma_addr_t pasid_dir_base;
    VTDPASIDDirEntry pdire;
    VTDPASIDEntry pe;

    if (pasid == PCI_NO_PASID) {
        pasid = VTD_CE_GET_RID2PASID(ce);
    }
    pasid_dir_base = VTD_CE_GET_PASID_DIR_TABLE(ce);

    /*
     * No present bit check since fpd is meaningful even
     * if the present bit is clear.
     */
    ret = vtd_get_pdire_from_pdir_table(pasid_dir_base, pasid, &pdire);
    if (ret) {
        return ret;
    }

    if (pdire.val & VTD_PASID_DIR_FPD) {
        *pe_fpd_set = true;
        return 0;
    }

    if (!vtd_pdire_present(&pdire)) {
        return -VTD_FR_PASID_DIR_ENTRY_P;
    }

    /*
     * No present bit check since fpd is meaningful even
     * if the present bit is clear.
     */
    ret = vtd_get_pe_from_pdire(s, pasid, &pdire, &pe);
    if (ret) {
        return ret;
    }

    if (pe.val[0] & VTD_PASID_ENTRY_FPD) {
        *pe_fpd_set = true;
    }

    return 0;
}

/* Get the page-table level that hardware should use for the second-level
 * page-table walk from the Address Width field of context-entry.
 */
static inline uint32_t vtd_ce_get_level(VTDContextEntry *ce)
{
    return 2 + (ce->hi & VTD_CONTEXT_ENTRY_AW);
}

static uint32_t vtd_get_iova_level(IntelIOMMUState *s,
                                   VTDContextEntry *ce,
                                   uint32_t pasid)
{
    VTDPASIDEntry pe;

    if (s->root_scalable) {
        vtd_ce_get_rid2pasid_entry(s, ce, &pe, pasid);
        if (s->flts) {
            return VTD_PE_GET_FL_LEVEL(&pe);
        } else {
            return VTD_PE_GET_SL_LEVEL(&pe);
        }
    }

    return vtd_ce_get_level(ce);
}

static inline uint32_t vtd_ce_get_agaw(VTDContextEntry *ce)
{
    return 30 + (ce->hi & VTD_CONTEXT_ENTRY_AW) * 9;
}

static uint32_t vtd_get_iova_agaw(IntelIOMMUState *s,
                                  VTDContextEntry *ce,
                                  uint32_t pasid)
{
    VTDPASIDEntry pe;

    if (s->root_scalable) {
        vtd_ce_get_rid2pasid_entry(s, ce, &pe, pasid);
        return 30 + ((pe.val[0] >> 2) & VTD_SM_PASID_ENTRY_AW) * 9;
    }

    return vtd_ce_get_agaw(ce);
}

static inline uint32_t vtd_ce_get_type(VTDContextEntry *ce)
{
    return ce->lo & VTD_CONTEXT_ENTRY_TT;
}

/* Only for Legacy Mode. Return true if check passed, otherwise false */
static inline bool vtd_ce_type_check(X86IOMMUState *x86_iommu,
                                     VTDContextEntry *ce)
{
    switch (vtd_ce_get_type(ce)) {
    case VTD_CONTEXT_TT_MULTI_LEVEL:
        /* Always supported */
        break;
    case VTD_CONTEXT_TT_DEV_IOTLB:
        if (!x86_iommu->dt_supported) {
            error_report_once("%s: DT specified but not supported", __func__);
            return false;
        }
        break;
    case VTD_CONTEXT_TT_PASS_THROUGH:
        if (!x86_iommu->pt_supported) {
            error_report_once("%s: PT specified but not supported", __func__);
            return false;
        }
        break;
    default:
        /* Unknown type */
        error_report_once("%s: unknown ce type: %"PRIu32, __func__,
                          vtd_ce_get_type(ce));
        return false;
    }
    return true;
}

static inline uint64_t vtd_iova_limit(IntelIOMMUState *s,
                                      VTDContextEntry *ce, uint8_t aw,
                                      uint32_t pasid)
{
    uint32_t ce_agaw = vtd_get_iova_agaw(s, ce, pasid);
    return 1ULL << MIN(ce_agaw, aw);
}

/* Return true if IOVA passes range check, otherwise false. */
static inline bool vtd_iova_sl_range_check(IntelIOMMUState *s,
                                           uint64_t iova, VTDContextEntry *ce,
                                           uint8_t aw, uint32_t pasid)
{
    /*
     * Check if @iova is above 2^X-1, where X is the minimum of MGAW
     * in CAP_REG and AW in context-entry.
     */
    return !(iova & ~(vtd_iova_limit(s, ce, aw, pasid) - 1));
}

static dma_addr_t vtd_get_iova_pgtbl_base(IntelIOMMUState *s,
                                          VTDContextEntry *ce,
                                          uint32_t pasid)
{
    VTDPASIDEntry pe;

    if (s->root_scalable) {
        vtd_ce_get_rid2pasid_entry(s, ce, &pe, pasid);
        if (s->flts) {
            return pe.val[2] & VTD_SM_PASID_ENTRY_FLPTPTR;
        } else {
            return pe.val[0] & VTD_SM_PASID_ENTRY_SLPTPTR;
        }
    }

    return vtd_ce_get_slpt_base(ce);
}

/*
 * Rsvd field masks for spte:
 *     vtd_spte_rsvd 4k pages
 *     vtd_spte_rsvd_large large pages
 *
 * We support only 3-level and 4-level page tables (see vtd_init() which
 * sets only VTD_CAP_SAGAW_39bit and maybe VTD_CAP_SAGAW_48bit bits in s->cap).
 */
#define VTD_SPTE_RSVD_LEN 5
static uint64_t vtd_spte_rsvd[VTD_SPTE_RSVD_LEN];
static uint64_t vtd_spte_rsvd_large[VTD_SPTE_RSVD_LEN];

static bool vtd_slpte_nonzero_rsvd(uint64_t slpte, uint32_t level)
{
    uint64_t rsvd_mask;

    /*
     * We should have caught a guest-mis-programmed level earlier,
     * via vtd_is_sl_level_supported.
     */
    assert(level < VTD_SPTE_RSVD_LEN);
    /*
     * Zero level doesn't exist. The smallest level is VTD_PT_LEVEL=1 and
     * checked by vtd_is_last_pte().
     */
    assert(level);

    if ((level == VTD_PD_LEVEL || level == VTD_PDP_LEVEL) &&
        (slpte & VTD_PT_PAGE_SIZE_MASK)) {
        /* large page */
        rsvd_mask = vtd_spte_rsvd_large[level];
    } else {
        rsvd_mask = vtd_spte_rsvd[level];
    }

    return slpte & rsvd_mask;
}

/* Given the @iova, get relevant @slptep. @slpte_level will be the last level
 * of the translation, can be used for deciding the size of large page.
 */
static int vtd_iova_to_slpte(IntelIOMMUState *s, VTDContextEntry *ce,
                             uint64_t iova, bool is_write,
                             uint64_t *slptep, uint32_t *slpte_level,
                             bool *reads, bool *writes, uint8_t aw_bits,
                             uint32_t pasid)
{
    dma_addr_t addr = vtd_get_iova_pgtbl_base(s, ce, pasid);
    uint32_t level = vtd_get_iova_level(s, ce, pasid);
    uint32_t offset;
    uint64_t slpte;
    uint64_t access_right_check;

    if (!vtd_iova_sl_range_check(s, iova, ce, aw_bits, pasid)) {
        error_report_once("%s: detected IOVA overflow (iova=0x%" PRIx64 ","
                          "pasid=0x%" PRIx32 ")", __func__, iova, pasid);
        return -VTD_FR_ADDR_BEYOND_MGAW;
    }

    /* FIXME: what is the Atomics request here? */
    access_right_check = is_write ? VTD_SL_W : VTD_SL_R;

    while (true) {
        offset = vtd_iova_level_offset(iova, level);
        slpte = vtd_get_pte(addr, offset);

        if (slpte == (uint64_t)-1) {
            error_report_once("%s: detected read error on DMAR slpte "
                              "(iova=0x%" PRIx64 ", pasid=0x%" PRIx32 ")",
                              __func__, iova, pasid);
            if (level == vtd_get_iova_level(s, ce, pasid)) {
                /* Invalid programming of context-entry */
                return -VTD_FR_CONTEXT_ENTRY_INV;
            } else {
                return -VTD_FR_PAGING_ENTRY_INV;
            }
        }
        *reads = (*reads) && (slpte & VTD_SL_R);
        *writes = (*writes) && (slpte & VTD_SL_W);
        if (!(slpte & access_right_check)) {
            error_report_once("%s: detected slpte permission error "
                              "(iova=0x%" PRIx64 ", level=0x%" PRIx32 ", "
                              "slpte=0x%" PRIx64 ", write=%d, pasid=0x%"
                              PRIx32 ")", __func__, iova, level,
                              slpte, is_write, pasid);
            return is_write ? -VTD_FR_WRITE : -VTD_FR_READ;
        }
        if (vtd_slpte_nonzero_rsvd(slpte, level)) {
            error_report_once("%s: detected splte reserve non-zero "
                              "iova=0x%" PRIx64 ", level=0x%" PRIx32
                              "slpte=0x%" PRIx64 ", pasid=0x%" PRIX32 ")",
                              __func__, iova, level, slpte, pasid);
            return -VTD_FR_PAGING_ENTRY_RSVD;
        }

        if (vtd_is_last_pte(slpte, level)) {
            *slptep = slpte;
            *slpte_level = level;
            break;
        }
        addr = vtd_get_pte_addr(slpte, aw_bits);
        level--;
    }

    return 0;
}

typedef int (*vtd_page_walk_hook)(const IOMMUTLBEvent *event, void *private);

/**
 * Constant information used during page walking
 *
 * @hook_fn: hook func to be called when detected page
 * @private: private data to be passed into hook func
 * @notify_unmap: whether we should notify invalid entries
 * @as: VT-d address space of the device
 * @aw: maximum address width
 * @domain: domain ID of the page walk
 */
typedef struct {
    VTDAddressSpace *as;
    vtd_page_walk_hook hook_fn;
    void *private;
    bool notify_unmap;
    uint8_t aw;
    uint16_t domain_id;
} vtd_page_walk_info;

static int vtd_page_walk_one(IOMMUTLBEvent *event, vtd_page_walk_info *info)
{
    VTDAddressSpace *as = info->as;
    vtd_page_walk_hook hook_fn = info->hook_fn;
    void *private = info->private;
    IOMMUTLBEntry *entry = &event->entry;
    DMAMap target = {
        .iova = entry->iova,
        .size = entry->addr_mask,
        .translated_addr = entry->translated_addr,
        .perm = entry->perm,
    };
    const DMAMap *mapped = iova_tree_find(as->iova_tree, &target);

    if (event->type == IOMMU_NOTIFIER_UNMAP && !info->notify_unmap) {
        trace_vtd_page_walk_one_skip_unmap(entry->iova, entry->addr_mask);
        return 0;
    }

    assert(hook_fn);

    /* Update local IOVA mapped ranges */
    if (event->type == IOMMU_NOTIFIER_MAP) {
        if (mapped) {
            /* If it's exactly the same translation, skip */
            if (!memcmp(mapped, &target, sizeof(target))) {
                trace_vtd_page_walk_one_skip_map(entry->iova, entry->addr_mask,
                                                 entry->translated_addr);
                return 0;
            } else {
                /*
                 * Translation changed.  Normally this should not
                 * happen, but it can happen when with buggy guest
                 * OSes.  Note that there will be a small window that
                 * we don't have map at all.  But that's the best
                 * effort we can do.  The ideal way to emulate this is
                 * atomically modify the PTE to follow what has
                 * changed, but we can't.  One example is that vfio
                 * driver only has VFIO_IOMMU_[UN]MAP_DMA but no
                 * interface to modify a mapping (meanwhile it seems
                 * meaningless to even provide one).  Anyway, let's
                 * mark this as a TODO in case one day we'll have
                 * a better solution.
                 */
                IOMMUAccessFlags cache_perm = entry->perm;
                int ret;

                /* Emulate an UNMAP */
                event->type = IOMMU_NOTIFIER_UNMAP;
                entry->perm = IOMMU_NONE;
                trace_vtd_page_walk_one(info->domain_id,
                                        entry->iova,
                                        entry->translated_addr,
                                        entry->addr_mask,
                                        entry->perm);
                ret = hook_fn(event, private);
                if (ret) {
                    return ret;
                }
                /* Drop any existing mapping */
                iova_tree_remove(as->iova_tree, target);
                /* Recover the correct type */
                event->type = IOMMU_NOTIFIER_MAP;
                entry->perm = cache_perm;
            }
        }
        iova_tree_insert(as->iova_tree, &target);
    } else {
        if (!mapped) {
            /* Skip since we didn't map this range at all */
            trace_vtd_page_walk_one_skip_unmap(entry->iova, entry->addr_mask);
            return 0;
        }
        iova_tree_remove(as->iova_tree, target);
    }

    trace_vtd_page_walk_one(info->domain_id, entry->iova,
                            entry->translated_addr, entry->addr_mask,
                            entry->perm);
    return hook_fn(event, private);
}

/**
 * vtd_page_walk_level - walk over specific level for IOVA range
 *
 * @addr: base GPA addr to start the walk
 * @start: IOVA range start address
 * @end: IOVA range end address (start <= addr < end)
 * @read: whether parent level has read permission
 * @write: whether parent level has write permission
 * @info: constant information for the page walk
 */
static int vtd_page_walk_level(dma_addr_t addr, uint64_t start,
                               uint64_t end, uint32_t level, bool read,
                               bool write, vtd_page_walk_info *info)
{
    bool read_cur, write_cur, entry_valid;
    uint32_t offset;
    uint64_t slpte;
    uint64_t subpage_size, subpage_mask;
    IOMMUTLBEvent event;
    uint64_t iova = start;
    uint64_t iova_next;
    int ret = 0;

    trace_vtd_page_walk_level(addr, level, start, end);

    subpage_size = 1ULL << vtd_pt_level_shift(level);
    subpage_mask = vtd_pt_level_page_mask(level);

    while (iova < end) {
        iova_next = (iova & subpage_mask) + subpage_size;

        offset = vtd_iova_level_offset(iova, level);
        slpte = vtd_get_pte(addr, offset);

        if (slpte == (uint64_t)-1) {
            trace_vtd_page_walk_skip_read(iova, iova_next);
            goto next;
        }

        if (vtd_slpte_nonzero_rsvd(slpte, level)) {
            trace_vtd_page_walk_skip_reserve(iova, iova_next);
            goto next;
        }

        /* Permissions are stacked with parents' */
        read_cur = read && (slpte & VTD_SL_R);
        write_cur = write && (slpte & VTD_SL_W);

        /*
         * As long as we have either read/write permission, this is a
         * valid entry. The rule works for both page entries and page
         * table entries.
         */
        entry_valid = read_cur | write_cur;

        if (!vtd_is_last_pte(slpte, level) && entry_valid) {
            /*
             * This is a valid PDE (or even bigger than PDE).  We need
             * to walk one further level.
             */
            ret = vtd_page_walk_level(vtd_get_pte_addr(slpte, info->aw),
                                      iova, MIN(iova_next, end), level - 1,
                                      read_cur, write_cur, info);
        } else {
            /*
             * This means we are either:
             *
             * (1) the real page entry (either 4K page, or huge page)
             * (2) the whole range is invalid
             *
             * In either case, we send an IOTLB notification down.
             */
            event.entry.target_as = &address_space_memory;
            event.entry.iova = iova & subpage_mask;
            event.entry.perm = IOMMU_ACCESS_FLAG(read_cur, write_cur);
            event.entry.addr_mask = ~subpage_mask;
            /* NOTE: this is only meaningful if entry_valid == true */
            event.entry.translated_addr = vtd_get_pte_addr(slpte, info->aw);
            event.type = event.entry.perm ? IOMMU_NOTIFIER_MAP :
                                            IOMMU_NOTIFIER_UNMAP;
            ret = vtd_page_walk_one(&event, info);
        }

        if (ret < 0) {
            return ret;
        }

next:
        iova = iova_next;
    }

    return 0;
}

/**
 * vtd_page_walk - walk specific IOVA range, and call the hook
 *
 * @s: intel iommu state
 * @ce: context entry to walk upon
 * @start: IOVA address to start the walk
 * @end: IOVA range end address (start <= addr < end)
 * @info: page walking information struct
 */
static int vtd_page_walk(IntelIOMMUState *s, VTDContextEntry *ce,
                         uint64_t start, uint64_t end,
                         vtd_page_walk_info *info,
                         uint32_t pasid)
{
    dma_addr_t addr = vtd_get_iova_pgtbl_base(s, ce, pasid);
    uint32_t level = vtd_get_iova_level(s, ce, pasid);

    if (!vtd_iova_sl_range_check(s, start, ce, info->aw, pasid)) {
        return -VTD_FR_ADDR_BEYOND_MGAW;
    }

    if (!vtd_iova_sl_range_check(s, end, ce, info->aw, pasid)) {
        /* Fix end so that it reaches the maximum */
        end = vtd_iova_limit(s, ce, info->aw, pasid);
    }

    return vtd_page_walk_level(addr, start, end, level, true, true, info);
}

static int vtd_root_entry_rsvd_bits_check(IntelIOMMUState *s,
                                          VTDRootEntry *re)
{
    /* Legacy Mode reserved bits check */
    if (!s->root_scalable &&
        (re->hi || (re->lo & VTD_ROOT_ENTRY_RSVD(s->aw_bits))))
        goto rsvd_err;

    /* Scalable Mode reserved bits check */
    if (s->root_scalable &&
        ((re->lo & VTD_ROOT_ENTRY_RSVD(s->aw_bits)) ||
         (re->hi & VTD_ROOT_ENTRY_RSVD(s->aw_bits))))
        goto rsvd_err;

    return 0;

rsvd_err:
    error_report_once("%s: invalid root entry: hi=0x%"PRIx64
                      ", lo=0x%"PRIx64,
                      __func__, re->hi, re->lo);
    return -VTD_FR_ROOT_ENTRY_RSVD;
}

static inline int vtd_context_entry_rsvd_bits_check(IntelIOMMUState *s,
                                                    VTDContextEntry *ce)
{
    if (!s->root_scalable &&
        (ce->hi & VTD_CONTEXT_ENTRY_RSVD_HI ||
         ce->lo & VTD_CONTEXT_ENTRY_RSVD_LO(s->aw_bits))) {
        error_report_once("%s: invalid context entry: hi=%"PRIx64
                          ", lo=%"PRIx64" (reserved nonzero)",
                          __func__, ce->hi, ce->lo);
        return -VTD_FR_CONTEXT_ENTRY_RSVD;
    }

    if (s->root_scalable &&
        (ce->val[0] & VTD_SM_CONTEXT_ENTRY_RSVD_VAL0(s->aw_bits) ||
         ce->val[1] & VTD_SM_CONTEXT_ENTRY_RSVD_VAL1 ||
         ce->val[2] ||
         ce->val[3])) {
        error_report_once("%s: invalid context entry: val[3]=%"PRIx64
                          ", val[2]=%"PRIx64
                          ", val[1]=%"PRIx64
                          ", val[0]=%"PRIx64" (reserved nonzero)",
                          __func__, ce->val[3], ce->val[2],
                          ce->val[1], ce->val[0]);
        return -VTD_FR_CONTEXT_ENTRY_RSVD;
    }

    return 0;
}

static int vtd_ce_rid2pasid_check(IntelIOMMUState *s,
                                  VTDContextEntry *ce)
{
    VTDPASIDEntry pe;

    /*
     * Make sure in Scalable Mode, a present context entry
     * has valid rid2pasid setting, which includes valid
     * rid2pasid field and corresponding pasid entry setting
     */
    return vtd_ce_get_rid2pasid_entry(s, ce, &pe, PCI_NO_PASID);
}

/* Map a device to its corresponding domain (context-entry) */
static int vtd_dev_to_context_entry(IntelIOMMUState *s, uint8_t bus_num,
                                    uint8_t devfn, VTDContextEntry *ce)
{
    VTDRootEntry re;
    int ret_fr;
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(s);

    ret_fr = vtd_get_root_entry(s, bus_num, &re);
    if (ret_fr) {
        return ret_fr;
    }

    if (!vtd_root_entry_present(s, &re, devfn)) {
        /* Not error - it's okay we don't have root entry. */
        trace_vtd_re_not_present(bus_num);
        return -VTD_FR_ROOT_ENTRY_P;
    }

    ret_fr = vtd_root_entry_rsvd_bits_check(s, &re);
    if (ret_fr) {
        return ret_fr;
    }

    ret_fr = vtd_get_context_entry_from_root(s, &re, devfn, ce);
    if (ret_fr) {
        return ret_fr;
    }

    if (!vtd_ce_present(ce)) {
        /* Not error - it's okay we don't have context entry. */
        trace_vtd_ce_not_present(bus_num, devfn);
        return -VTD_FR_CONTEXT_ENTRY_P;
    }

    ret_fr = vtd_context_entry_rsvd_bits_check(s, ce);
    if (ret_fr) {
        return ret_fr;
    }

    /* Check if the programming of context-entry is valid */
    if (!s->root_scalable &&
        !vtd_is_sl_level_supported(s, vtd_ce_get_level(ce))) {
        error_report_once("%s: invalid context entry: hi=%"PRIx64
                          ", lo=%"PRIx64" (level %d not supported)",
                          __func__, ce->hi, ce->lo,
                          vtd_ce_get_level(ce));
        return -VTD_FR_CONTEXT_ENTRY_INV;
    }

    if (!s->root_scalable) {
        /* Do translation type check */
        if (!vtd_ce_type_check(x86_iommu, ce)) {
            /* Errors dumped in vtd_ce_type_check() */
            return -VTD_FR_CONTEXT_ENTRY_INV;
        }
    } else {
        /*
         * Check if the programming of context-entry.rid2pasid
         * and corresponding pasid setting is valid, and thus
         * avoids to check pasid entry fetching result in future
         * helper function calling.
         */
        ret_fr = vtd_ce_rid2pasid_check(s, ce);
        if (ret_fr) {
            return ret_fr;
        }
    }

    return 0;
}

static int vtd_sync_shadow_page_hook(const IOMMUTLBEvent *event,
                                     void *private)
{
    memory_region_notify_iommu(private, 0, *event);
    return 0;
}

static uint16_t vtd_get_domain_id(IntelIOMMUState *s,
                                  VTDContextEntry *ce,
                                  uint32_t pasid)
{
    VTDPASIDEntry pe;

    if (s->root_scalable) {
        vtd_ce_get_rid2pasid_entry(s, ce, &pe, pasid);
        return VTD_SM_PASID_ENTRY_DID(pe.val[1]);
    }

    return VTD_CONTEXT_ENTRY_DID(ce->hi);
}

static int vtd_sync_shadow_page_table_range(VTDAddressSpace *vtd_as,
                                            VTDContextEntry *ce,
                                            hwaddr addr, hwaddr size)
{
    IntelIOMMUState *s = vtd_as->iommu_state;
    vtd_page_walk_info info = {
        .hook_fn = vtd_sync_shadow_page_hook,
        .private = (void *)&vtd_as->iommu,
        .notify_unmap = true,
        .aw = s->aw_bits,
        .as = vtd_as,
        .domain_id = vtd_get_domain_id(s, ce, vtd_as->pasid),
    };

    return vtd_page_walk(s, ce, addr, addr + size, &info, vtd_as->pasid);
}

static int vtd_address_space_sync(VTDAddressSpace *vtd_as)
{
    int ret;
    VTDContextEntry ce;
    IOMMUNotifier *n;

    /* If no MAP notifier registered, we simply invalidate all the cache */
    if (!vtd_as_has_map_notifier(vtd_as)) {
        IOMMU_NOTIFIER_FOREACH(n, &vtd_as->iommu) {
            memory_region_unmap_iommu_notifier_range(n);
        }
        return 0;
    }

    ret = vtd_dev_to_context_entry(vtd_as->iommu_state,
                                   pci_bus_num(vtd_as->bus),
                                   vtd_as->devfn, &ce);
    if (ret) {
        if (ret == -VTD_FR_CONTEXT_ENTRY_P) {
            /*
             * It's a valid scenario to have a context entry that is
             * not present.  For example, when a device is removed
             * from an existing domain then the context entry will be
             * zeroed by the guest before it was put into another
             * domain.  When this happens, instead of synchronizing
             * the shadow pages we should invalidate all existing
             * mappings and notify the backends.
             */
            IOMMU_NOTIFIER_FOREACH(n, &vtd_as->iommu) {
                vtd_address_space_unmap(vtd_as, n);
            }
            ret = 0;
        }
        return ret;
    }

    return vtd_sync_shadow_page_table_range(vtd_as, &ce, 0, UINT64_MAX);
}

/*
 * Check if specific device is configured to bypass address
 * translation for DMA requests. In Scalable Mode, bypass
 * 1st-level translation or 2nd-level translation, it depends
 * on PGTT setting.
 */
static bool vtd_dev_pt_enabled(IntelIOMMUState *s, VTDContextEntry *ce,
                               uint32_t pasid)
{
    VTDPASIDEntry pe;
    int ret;

    if (s->root_scalable) {
        ret = vtd_ce_get_rid2pasid_entry(s, ce, &pe, pasid);
        if (ret) {
            /*
             * This error is guest triggerable. We should assumt PT
             * not enabled for safety.
             */
            return false;
        }
        return (VTD_PE_GET_TYPE(&pe) == VTD_SM_PASID_ENTRY_PT);
    }

    return (vtd_ce_get_type(ce) == VTD_CONTEXT_TT_PASS_THROUGH);

}

static bool vtd_as_pt_enabled(VTDAddressSpace *as)
{
    IntelIOMMUState *s;
    VTDContextEntry ce;

    assert(as);

    s = as->iommu_state;
    if (vtd_dev_to_context_entry(s, pci_bus_num(as->bus), as->devfn,
                                 &ce)) {
        /*
         * Possibly failed to parse the context entry for some reason
         * (e.g., during init, or any guest configuration errors on
         * context entries). We should assume PT not enabled for
         * safety.
         */
        return false;
    }

    return vtd_dev_pt_enabled(s, &ce, as->pasid);
}

/* Return whether the device is using IOMMU translation. */
static bool vtd_switch_address_space(VTDAddressSpace *as)
{
    bool use_iommu, pt;
    /* Whether we need to take the BQL on our own */
    bool take_bql = !bql_locked();

    assert(as);

    use_iommu = as->iommu_state->dmar_enabled && !vtd_as_pt_enabled(as);
    pt = as->iommu_state->dmar_enabled && vtd_as_pt_enabled(as);

    trace_vtd_switch_address_space(pci_bus_num(as->bus),
                                   VTD_PCI_SLOT(as->devfn),
                                   VTD_PCI_FUNC(as->devfn),
                                   use_iommu);

    /*
     * It's possible that we reach here without BQL, e.g., when called
     * from vtd_pt_enable_fast_path(). However the memory APIs need
     * it. We'd better make sure we have had it already, or, take it.
     */
    if (take_bql) {
        bql_lock();
    }

    /* Turn off first then on the other */
    if (use_iommu) {
        memory_region_set_enabled(&as->nodmar, false);
        memory_region_set_enabled(MEMORY_REGION(&as->iommu), true);
        /*
         * vt-d spec v3.4 3.14:
         *
         * """
         * Requests-with-PASID with input address in range 0xFEEx_xxxx
         * are translated normally like any other request-with-PASID
         * through DMA-remapping hardware.
         * """
         *
         * Need to disable ir for as with PASID.
         */
        if (as->pasid != PCI_NO_PASID) {
            memory_region_set_enabled(&as->iommu_ir, false);
        } else {
            memory_region_set_enabled(&as->iommu_ir, true);
        }
    } else {
        memory_region_set_enabled(MEMORY_REGION(&as->iommu), false);
        memory_region_set_enabled(&as->nodmar, true);
    }

    /*
     * vtd-spec v3.4 3.14:
     *
     * """
     * Requests-with-PASID with input address in range 0xFEEx_xxxx are
     * translated normally like any other request-with-PASID through
     * DMA-remapping hardware. However, if such a request is processed
     * using pass-through translation, it will be blocked as described
     * in the paragraph below.
     *
     * Software must not program paging-structure entries to remap any
     * address to the interrupt address range. Untranslated requests
     * and translation requests that result in an address in the
     * interrupt range will be blocked with condition code LGN.4 or
     * SGN.8.
     * """
     *
     * We enable per as memory region (iommu_ir_fault) for catching
     * the translation for interrupt range through PASID + PT.
     */
    if (pt && as->pasid != PCI_NO_PASID) {
        memory_region_set_enabled(&as->iommu_ir_fault, true);
    } else {
        memory_region_set_enabled(&as->iommu_ir_fault, false);
    }

    if (take_bql) {
        bql_unlock();
    }

    return use_iommu;
}

static void vtd_switch_address_space_all(IntelIOMMUState *s)
{
    VTDAddressSpace *vtd_as;
    GHashTableIter iter;

    g_hash_table_iter_init(&iter, s->vtd_address_spaces);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&vtd_as)) {
        vtd_switch_address_space(vtd_as);
    }
}

static const bool vtd_qualified_faults[] = {
    [VTD_FR_RESERVED] = false,
    [VTD_FR_ROOT_ENTRY_P] = false,
    [VTD_FR_CONTEXT_ENTRY_P] = true,
    [VTD_FR_CONTEXT_ENTRY_INV] = true,
    [VTD_FR_ADDR_BEYOND_MGAW] = true,
    [VTD_FR_WRITE] = true,
    [VTD_FR_READ] = true,
    [VTD_FR_PAGING_ENTRY_INV] = true,
    [VTD_FR_ROOT_TABLE_INV] = false,
    [VTD_FR_CONTEXT_TABLE_INV] = false,
    [VTD_FR_INTERRUPT_ADDR] = true,
    [VTD_FR_ROOT_ENTRY_RSVD] = false,
    [VTD_FR_PAGING_ENTRY_RSVD] = true,
    [VTD_FR_CONTEXT_ENTRY_TT] = true,
    [VTD_FR_PASID_DIR_ACCESS_ERR] = false,
    [VTD_FR_PASID_DIR_ENTRY_P] = true,
    [VTD_FR_PASID_TABLE_ACCESS_ERR] = false,
    [VTD_FR_PASID_ENTRY_P] = true,
    [VTD_FR_PASID_TABLE_ENTRY_INV] = true,
    [VTD_FR_FS_PAGING_ENTRY_INV] = true,
    [VTD_FR_FS_PAGING_ENTRY_P] = true,
    [VTD_FR_FS_PAGING_ENTRY_RSVD] = true,
    [VTD_FR_PASID_ENTRY_FSPTPTR_INV] = true,
    [VTD_FR_FS_NON_CANONICAL] = true,
    [VTD_FR_FS_PAGING_ENTRY_US] = true,
    [VTD_FR_SM_WRITE] = true,
    [VTD_FR_SM_INTERRUPT_ADDR] = true,
    [VTD_FR_FS_BIT_UPDATE_FAILED] = true,
    [VTD_FR_MAX] = false,
};

/* To see if a fault condition is "qualified", which is reported to software
 * only if the FPD field in the context-entry used to process the faulting
 * request is 0.
 */
static inline bool vtd_is_qualified_fault(VTDFaultReason fault)
{
    return vtd_qualified_faults[fault];
}

static inline bool vtd_is_interrupt_addr(hwaddr addr)
{
    return VTD_INTERRUPT_ADDR_FIRST <= addr && addr <= VTD_INTERRUPT_ADDR_LAST;
}

static gboolean vtd_find_as_by_sid_and_pasid(gpointer key, gpointer value,
                                             gpointer user_data)
{
    struct vtd_as_key *as_key = (struct vtd_as_key *)key;
    struct vtd_as_raw_key *target = (struct vtd_as_raw_key *)user_data;
    uint16_t sid = PCI_BUILD_BDF(pci_bus_num(as_key->bus), as_key->devfn);

    return (as_key->pasid == target->pasid) && (sid == target->sid);
}

static VTDAddressSpace *vtd_get_as_by_sid_and_pasid(IntelIOMMUState *s,
                                                    uint16_t sid,
                                                    uint32_t pasid)
{
    struct vtd_as_raw_key key = {
        .sid = sid,
        .pasid = pasid
    };

    return g_hash_table_find(s->vtd_address_spaces,
                             vtd_find_as_by_sid_and_pasid, &key);
}

static VTDAddressSpace *vtd_get_as_by_sid(IntelIOMMUState *s, uint16_t sid)
{
    return vtd_get_as_by_sid_and_pasid(s, sid, PCI_NO_PASID);
}

static void vtd_pt_enable_fast_path(IntelIOMMUState *s, uint16_t source_id)
{
    VTDAddressSpace *vtd_as;
    bool success = false;

    vtd_as = vtd_get_as_by_sid(s, source_id);
    if (!vtd_as) {
        goto out;
    }

    if (vtd_switch_address_space(vtd_as) == false) {
        /* We switched off IOMMU region successfully. */
        success = true;
    }

out:
    trace_vtd_pt_enable_fast_path(source_id, success);
}

/*
 * Rsvd field masks for fpte:
 *     vtd_fpte_rsvd 4k pages
 *     vtd_fpte_rsvd_large large pages
 *
 * We support only 4-level page tables.
 */
#define VTD_FPTE_RSVD_LEN 5
static uint64_t vtd_fpte_rsvd[VTD_FPTE_RSVD_LEN];
static uint64_t vtd_fpte_rsvd_large[VTD_FPTE_RSVD_LEN];

static bool vtd_flpte_nonzero_rsvd(uint64_t flpte, uint32_t level)
{
    uint64_t rsvd_mask;

    /*
     * We should have caught a guest-mis-programmed level earlier,
     * via vtd_is_fl_level_supported.
     */
    assert(level < VTD_FPTE_RSVD_LEN);
    /*
     * Zero level doesn't exist. The smallest level is VTD_PT_LEVEL=1 and
     * checked by vtd_is_last_pte().
     */
    assert(level);

    if ((level == VTD_PD_LEVEL || level == VTD_PDP_LEVEL) &&
        (flpte & VTD_PT_PAGE_SIZE_MASK)) {
        /* large page */
        rsvd_mask = vtd_fpte_rsvd_large[level];
    } else {
        rsvd_mask = vtd_fpte_rsvd[level];
    }

    return flpte & rsvd_mask;
}

static inline bool vtd_flpte_present(uint64_t flpte)
{
    return !!(flpte & VTD_FL_P);
}

/* Return true if IOVA is canonical, otherwise false. */
static bool vtd_iova_fl_check_canonical(IntelIOMMUState *s, uint64_t iova,
                                        VTDContextEntry *ce, uint32_t pasid)
{
    uint64_t iova_limit = vtd_iova_limit(s, ce, s->aw_bits, pasid);
    uint64_t upper_bits_mask = ~(iova_limit - 1);
    uint64_t upper_bits = iova & upper_bits_mask;
    bool msb = ((iova & (iova_limit >> 1)) != 0);

    if (msb) {
        return upper_bits == upper_bits_mask;
    } else {
        return !upper_bits;
    }
}

static MemTxResult vtd_set_flag_in_pte(dma_addr_t base_addr, uint32_t index,
                                       uint64_t pte, uint64_t flag)
{
    if (pte & flag) {
        return MEMTX_OK;
    }
    pte |= flag;
    pte = cpu_to_le64(pte);
    return dma_memory_write(&address_space_memory,
                            base_addr + index * sizeof(pte),
                            &pte, sizeof(pte),
                            MEMTXATTRS_UNSPECIFIED);
}

/*
 * Given the @iova, get relevant @flptep. @flpte_level will be the last level
 * of the translation, can be used for deciding the size of large page.
 */
static int vtd_iova_to_flpte(IntelIOMMUState *s, VTDContextEntry *ce,
                             uint64_t iova, bool is_write,
                             uint64_t *flptep, uint32_t *flpte_level,
                             bool *reads, bool *writes, uint8_t aw_bits,
                             uint32_t pasid)
{
    dma_addr_t addr = vtd_get_iova_pgtbl_base(s, ce, pasid);
    uint32_t level = vtd_get_iova_level(s, ce, pasid);
    uint32_t offset;
    uint64_t flpte, flag_ad = VTD_FL_A;

    if (!vtd_iova_fl_check_canonical(s, iova, ce, pasid)) {
        error_report_once("%s: detected non canonical IOVA (iova=0x%" PRIx64 ","
                          "pasid=0x%" PRIx32 ")", __func__, iova, pasid);
        return -VTD_FR_FS_NON_CANONICAL;
    }

    while (true) {
        offset = vtd_iova_level_offset(iova, level);
        flpte = vtd_get_pte(addr, offset);

        if (flpte == (uint64_t)-1) {
            if (level == vtd_get_iova_level(s, ce, pasid)) {
                /* Invalid programming of pasid-entry */
                return -VTD_FR_PASID_ENTRY_FSPTPTR_INV;
            } else {
                return -VTD_FR_FS_PAGING_ENTRY_INV;
            }
        }

        if (!vtd_flpte_present(flpte)) {
            *reads = false;
            *writes = false;
            return -VTD_FR_FS_PAGING_ENTRY_P;
        }

        /* No emulated device supports supervisor privilege request yet */
        if (!(flpte & VTD_FL_US)) {
            *reads = false;
            *writes = false;
            return -VTD_FR_FS_PAGING_ENTRY_US;
        }

        *reads = true;
        *writes = (*writes) && (flpte & VTD_FL_RW);
        if (is_write && !(flpte & VTD_FL_RW)) {
            return -VTD_FR_SM_WRITE;
        }
        if (vtd_flpte_nonzero_rsvd(flpte, level)) {
            error_report_once("%s: detected flpte reserved non-zero "
                              "iova=0x%" PRIx64 ", level=0x%" PRIx32
                              "flpte=0x%" PRIx64 ", pasid=0x%" PRIX32 ")",
                              __func__, iova, level, flpte, pasid);
            return -VTD_FR_FS_PAGING_ENTRY_RSVD;
        }

        if (vtd_is_last_pte(flpte, level) && is_write) {
            flag_ad |= VTD_FL_D;
        }

        if (vtd_set_flag_in_pte(addr, offset, flpte, flag_ad) != MEMTX_OK) {
            return -VTD_FR_FS_BIT_UPDATE_FAILED;
        }

        if (vtd_is_last_pte(flpte, level)) {
            *flptep = flpte;
            *flpte_level = level;
            return 0;
        }

        addr = vtd_get_pte_addr(flpte, aw_bits);
        level--;
    }
}

static void vtd_report_fault(IntelIOMMUState *s,
                             int err, bool is_fpd_set,
                             uint16_t source_id,
                             hwaddr addr,
                             bool is_write,
                             bool is_pasid,
                             uint32_t pasid)
{
    if (is_fpd_set && vtd_is_qualified_fault(err)) {
        trace_vtd_fault_disabled();
    } else {
        vtd_report_dmar_fault(s, source_id, addr, err, is_write,
                              is_pasid, pasid);
    }
}

/* Map dev to context-entry then do a paging-structures walk to do a iommu
 * translation.
 *
 * Called from RCU critical section.
 *
 * @bus_num: The bus number
 * @devfn: The devfn, which is the  combined of device and function number
 * @is_write: The access is a write operation
 * @entry: IOMMUTLBEntry that contain the addr to be translated and result
 *
 * Returns true if translation is successful, otherwise false.
 */
static bool vtd_do_iommu_translate(VTDAddressSpace *vtd_as, PCIBus *bus,
                                   uint8_t devfn, hwaddr addr, bool is_write,
                                   IOMMUTLBEntry *entry)
{
    IntelIOMMUState *s = vtd_as->iommu_state;
    VTDContextEntry ce;
    uint8_t bus_num = pci_bus_num(bus);
    VTDContextCacheEntry *cc_entry;
    uint64_t pte, page_mask;
    uint32_t level, pasid = vtd_as->pasid;
    uint16_t source_id = PCI_BUILD_BDF(bus_num, devfn);
    int ret_fr;
    bool is_fpd_set = false;
    bool reads = true;
    bool writes = true;
    uint8_t access_flags, pgtt;
    bool rid2pasid = (pasid == PCI_NO_PASID) && s->root_scalable;
    VTDIOTLBEntry *iotlb_entry;
    uint64_t xlat, size;

    /*
     * We have standalone memory region for interrupt addresses, we
     * should never receive translation requests in this region.
     */
    assert(!vtd_is_interrupt_addr(addr));

    vtd_iommu_lock(s);

    cc_entry = &vtd_as->context_cache_entry;

    /* Try to fetch pte from IOTLB, we don't need RID2PASID logic */
    if (!rid2pasid) {
        iotlb_entry = vtd_lookup_iotlb(s, source_id, pasid, addr);
        if (iotlb_entry) {
            trace_vtd_iotlb_page_hit(source_id, addr, iotlb_entry->pte,
                                     iotlb_entry->domain_id);
            pte = iotlb_entry->pte;
            access_flags = iotlb_entry->access_flags;
            page_mask = iotlb_entry->mask;
            goto out;
        }
    }

    /* Try to fetch context-entry from cache first */
    if (cc_entry->context_cache_gen == s->context_cache_gen) {
        trace_vtd_iotlb_cc_hit(bus_num, devfn, cc_entry->context_entry.hi,
                               cc_entry->context_entry.lo,
                               cc_entry->context_cache_gen);
        ce = cc_entry->context_entry;
        is_fpd_set = ce.lo & VTD_CONTEXT_ENTRY_FPD;
        if (!is_fpd_set && s->root_scalable) {
            ret_fr = vtd_ce_get_pasid_fpd(s, &ce, &is_fpd_set, pasid);
            if (ret_fr) {
                vtd_report_fault(s, -ret_fr, is_fpd_set,
                                 source_id, addr, is_write,
                                 false, 0);
                goto error;
            }
        }
    } else {
        ret_fr = vtd_dev_to_context_entry(s, bus_num, devfn, &ce);
        is_fpd_set = ce.lo & VTD_CONTEXT_ENTRY_FPD;
        if (!ret_fr && !is_fpd_set && s->root_scalable) {
            ret_fr = vtd_ce_get_pasid_fpd(s, &ce, &is_fpd_set, pasid);
        }
        if (ret_fr) {
            vtd_report_fault(s, -ret_fr, is_fpd_set,
                             source_id, addr, is_write,
                             false, 0);
            goto error;
        }
        /* Update context-cache */
        trace_vtd_iotlb_cc_update(bus_num, devfn, ce.hi, ce.lo,
                                  cc_entry->context_cache_gen,
                                  s->context_cache_gen);
        cc_entry->context_entry = ce;
        cc_entry->context_cache_gen = s->context_cache_gen;
    }

    if (rid2pasid) {
        pasid = VTD_CE_GET_RID2PASID(&ce);
    }

    /*
     * We don't need to translate for pass-through context entries.
     * Also, let's ignore IOTLB caching as well for PT devices.
     */
    if (vtd_dev_pt_enabled(s, &ce, pasid)) {
        entry->iova = addr & VTD_PAGE_MASK_4K;
        entry->translated_addr = entry->iova;
        entry->addr_mask = ~VTD_PAGE_MASK_4K;
        entry->perm = IOMMU_RW;
        trace_vtd_translate_pt(source_id, entry->iova);

        /*
         * When this happens, it means firstly caching-mode is not
         * enabled, and this is the first passthrough translation for
         * the device. Let's enable the fast path for passthrough.
         *
         * When passthrough is disabled again for the device, we can
         * capture it via the context entry invalidation, then the
         * IOMMU region can be swapped back.
         */
        vtd_pt_enable_fast_path(s, source_id);
        vtd_iommu_unlock(s);
        return true;
    }

    /* Try to fetch pte from IOTLB for RID2PASID slow path */
    if (rid2pasid) {
        iotlb_entry = vtd_lookup_iotlb(s, source_id, pasid, addr);
        if (iotlb_entry) {
            trace_vtd_iotlb_page_hit(source_id, addr, iotlb_entry->pte,
                                     iotlb_entry->domain_id);
            pte = iotlb_entry->pte;
            access_flags = iotlb_entry->access_flags;
            page_mask = iotlb_entry->mask;
            goto out;
        }
    }

    if (s->flts && s->root_scalable) {
        ret_fr = vtd_iova_to_flpte(s, &ce, addr, is_write, &pte, &level,
                                   &reads, &writes, s->aw_bits, pasid);
        pgtt = VTD_SM_PASID_ENTRY_FLT;
    } else {
        ret_fr = vtd_iova_to_slpte(s, &ce, addr, is_write, &pte, &level,
                                   &reads, &writes, s->aw_bits, pasid);
        pgtt = VTD_SM_PASID_ENTRY_SLT;
    }
    if (!ret_fr) {
        xlat = vtd_get_pte_addr(pte, s->aw_bits);
        size = ~vtd_pt_level_page_mask(level) + 1;

        /*
         * Per VT-d spec 4.1 section 3.15: Untranslated requests and translation
         * requests that result in an address in the interrupt range will be
         * blocked with condition code LGN.4 or SGN.8.
         */
        if ((xlat <= VTD_INTERRUPT_ADDR_LAST &&
             xlat + size - 1 >= VTD_INTERRUPT_ADDR_FIRST)) {
            error_report_once("%s: xlat address is in interrupt range "
                              "(iova=0x%" PRIx64 ", level=0x%" PRIx32 ", "
                              "pte=0x%" PRIx64 ", write=%d, "
                              "xlat=0x%" PRIx64 ", size=0x%" PRIx64 ", "
                              "pasid=0x%" PRIx32 ")",
                              __func__, addr, level, pte, is_write,
                              xlat, size, pasid);
            ret_fr = s->scalable_mode ? -VTD_FR_SM_INTERRUPT_ADDR :
                                        -VTD_FR_INTERRUPT_ADDR;
        }
    }

    if (ret_fr) {
        vtd_report_fault(s, -ret_fr, is_fpd_set, source_id,
                         addr, is_write, pasid != PCI_NO_PASID, pasid);
        goto error;
    }

    page_mask = vtd_pt_level_page_mask(level);
    access_flags = IOMMU_ACCESS_FLAG(reads, writes);
    vtd_update_iotlb(s, source_id, vtd_get_domain_id(s, &ce, pasid),
                     addr, pte, access_flags, level, pasid, pgtt);
out:
    vtd_iommu_unlock(s);
    entry->iova = addr & page_mask;
    entry->translated_addr = vtd_get_pte_addr(pte, s->aw_bits) & page_mask;
    entry->addr_mask = ~page_mask;
    entry->perm = access_flags;
    return true;

error:
    vtd_iommu_unlock(s);
    entry->iova = 0;
    entry->translated_addr = 0;
    entry->addr_mask = 0;
    entry->perm = IOMMU_NONE;
    return false;
}

static void vtd_root_table_setup(IntelIOMMUState *s)
{
    s->root = vtd_get_quad_raw(s, DMAR_RTADDR_REG);
    s->root &= VTD_RTADDR_ADDR_MASK(s->aw_bits);

    vtd_update_scalable_state(s);

    trace_vtd_reg_dmar_root(s->root, s->root_scalable);
}

static void vtd_iec_notify_all(IntelIOMMUState *s, bool global,
                               uint32_t index, uint32_t mask)
{
    x86_iommu_iec_notify_all(X86_IOMMU_DEVICE(s), global, index, mask);
}

static void vtd_interrupt_remap_table_setup(IntelIOMMUState *s)
{
    uint64_t value = 0;
    value = vtd_get_quad_raw(s, DMAR_IRTA_REG);
    s->intr_size = 1UL << ((value & VTD_IRTA_SIZE_MASK) + 1);
    s->intr_root = value & VTD_IRTA_ADDR_MASK(s->aw_bits);
    s->intr_eime = value & VTD_IRTA_EIME;

    /* Notify global invalidation */
    vtd_iec_notify_all(s, true, 0, 0);

    trace_vtd_reg_ir_root(s->intr_root, s->intr_size);
}

static void vtd_iommu_replay_all(IntelIOMMUState *s)
{
    VTDAddressSpace *vtd_as;

    QLIST_FOREACH(vtd_as, &s->vtd_as_with_notifiers, next) {
        vtd_address_space_sync(vtd_as);
    }
}

static void vtd_context_global_invalidate(IntelIOMMUState *s)
{
    trace_vtd_inv_desc_cc_global();
    /* Protects context cache */
    vtd_iommu_lock(s);
    s->context_cache_gen++;
    if (s->context_cache_gen == VTD_CONTEXT_CACHE_GEN_MAX) {
        vtd_reset_context_cache_locked(s);
    }
    vtd_iommu_unlock(s);
    vtd_address_space_refresh_all(s);
    /*
     * From VT-d spec 6.5.2.1, a global context entry invalidation
     * should be followed by a IOTLB global invalidation, so we should
     * be safe even without this. Hoewever, let's replay the region as
     * well to be safer, and go back here when we need finer tunes for
     * VT-d emulation codes.
     */
    vtd_iommu_replay_all(s);
}

/* Do a context-cache device-selective invalidation.
 * @func_mask: FM field after shifting
 */
static void vtd_context_device_invalidate(IntelIOMMUState *s,
                                          uint16_t source_id,
                                          uint16_t func_mask)
{
    GHashTableIter as_it;
    uint16_t mask;
    VTDAddressSpace *vtd_as;
    uint8_t bus_n, devfn;

    trace_vtd_inv_desc_cc_devices(source_id, func_mask);

    switch (func_mask & 3) {
    case 0:
        mask = 0;   /* No bits in the SID field masked */
        break;
    case 1:
        mask = 4;   /* Mask bit 2 in the SID field */
        break;
    case 2:
        mask = 6;   /* Mask bit 2:1 in the SID field */
        break;
    case 3:
        mask = 7;   /* Mask bit 2:0 in the SID field */
        break;
    default:
        g_assert_not_reached();
    }
    mask = ~mask;

    bus_n = VTD_SID_TO_BUS(source_id);
    devfn = VTD_SID_TO_DEVFN(source_id);

    g_hash_table_iter_init(&as_it, s->vtd_address_spaces);
    while (g_hash_table_iter_next(&as_it, NULL, (void **)&vtd_as)) {
        if ((pci_bus_num(vtd_as->bus) == bus_n) &&
            (vtd_as->devfn & mask) == (devfn & mask)) {
            trace_vtd_inv_desc_cc_device(bus_n, VTD_PCI_SLOT(vtd_as->devfn),
                                         VTD_PCI_FUNC(vtd_as->devfn));
            vtd_iommu_lock(s);
            vtd_as->context_cache_entry.context_cache_gen = 0;
            vtd_iommu_unlock(s);
            /*
             * Do switch address space when needed, in case if the
             * device passthrough bit is switched.
             */
            vtd_switch_address_space(vtd_as);
            /*
             * So a device is moving out of (or moving into) a
             * domain, resync the shadow page table.
             * This won't bring bad even if we have no such
             * notifier registered - the IOMMU notification
             * framework will skip MAP notifications if that
             * happened.
             */
            vtd_address_space_sync(vtd_as);
        }
    }
}

/* Context-cache invalidation
 * Returns the Context Actual Invalidation Granularity.
 * @val: the content of the CCMD_REG
 */
static uint64_t vtd_context_cache_invalidate(IntelIOMMUState *s, uint64_t val)
{
    uint64_t caig;
    uint64_t type = val & VTD_CCMD_CIRG_MASK;

    switch (type) {
    case VTD_CCMD_DOMAIN_INVL:
        /* Fall through */
    case VTD_CCMD_GLOBAL_INVL:
        caig = VTD_CCMD_GLOBAL_INVL_A;
        vtd_context_global_invalidate(s);
        break;

    case VTD_CCMD_DEVICE_INVL:
        caig = VTD_CCMD_DEVICE_INVL_A;
        vtd_context_device_invalidate(s, VTD_CCMD_SID(val), VTD_CCMD_FM(val));
        break;

    default:
        error_report_once("%s: invalid context: 0x%" PRIx64,
                          __func__, val);
        caig = 0;
    }
    return caig;
}

static void vtd_iotlb_global_invalidate(IntelIOMMUState *s)
{
    trace_vtd_inv_desc_iotlb_global();
    vtd_reset_iotlb(s);
    vtd_iommu_replay_all(s);
}

static void vtd_iotlb_domain_invalidate(IntelIOMMUState *s, uint16_t domain_id)
{
    VTDContextEntry ce;
    VTDAddressSpace *vtd_as;

    trace_vtd_inv_desc_iotlb_domain(domain_id);

    vtd_iommu_lock(s);
    g_hash_table_foreach_remove(s->iotlb, vtd_hash_remove_by_domain,
                                &domain_id);
    vtd_iommu_unlock(s);

    QLIST_FOREACH(vtd_as, &s->vtd_as_with_notifiers, next) {
        if (!vtd_dev_to_context_entry(s, pci_bus_num(vtd_as->bus),
                                      vtd_as->devfn, &ce) &&
            domain_id == vtd_get_domain_id(s, &ce, vtd_as->pasid)) {
            vtd_address_space_sync(vtd_as);
        }
    }
}

/*
 * There is no pasid field in iotlb invalidation descriptor, so PCI_NO_PASID
 * is passed as parameter. Piotlb invalidation supports pasid, pasid in its
 * descriptor is passed which should not be PCI_NO_PASID.
 */
static void vtd_iotlb_page_invalidate_notify(IntelIOMMUState *s,
                                             uint16_t domain_id, hwaddr addr,
                                             uint8_t am, uint32_t pasid)
{
    VTDAddressSpace *vtd_as;
    VTDContextEntry ce;
    int ret;
    hwaddr size = (1 << am) * VTD_PAGE_SIZE;

    QLIST_FOREACH(vtd_as, &(s->vtd_as_with_notifiers), next) {
        ret = vtd_dev_to_context_entry(s, pci_bus_num(vtd_as->bus),
                                       vtd_as->devfn, &ce);
        if (!ret && domain_id == vtd_get_domain_id(s, &ce, vtd_as->pasid)) {
            uint32_t rid2pasid = PCI_NO_PASID;

            if (s->root_scalable) {
                rid2pasid = VTD_CE_GET_RID2PASID(&ce);
            }

            /*
             * In legacy mode, vtd_as->pasid == pasid is always true.
             * In scalable mode, for vtd address space backing a PCI
             * device without pasid, needs to compare pasid with
             * rid2pasid of this device.
             */
            if (!(vtd_as->pasid == pasid ||
                  (vtd_as->pasid == PCI_NO_PASID && pasid == rid2pasid))) {
                continue;
            }

            if (vtd_as_has_map_notifier(vtd_as)) {
                /*
                 * When stage-1 translation is off, as long as we have MAP
                 * notifications registered in any of our IOMMU notifiers,
                 * we need to sync the shadow page table. Otherwise VFIO
                 * device attaches to nested page table instead of shadow
                 * page table, so no need to sync.
                 */
                if (!s->flts || !s->root_scalable) {
                    vtd_sync_shadow_page_table_range(vtd_as, &ce, addr, size);
                }
            } else {
                /*
                 * For UNMAP-only notifiers, we don't need to walk the
                 * page tables.  We just deliver the PSI down to
                 * invalidate caches.
                 */
                const IOMMUTLBEvent event = {
                    .type = IOMMU_NOTIFIER_UNMAP,
                    .entry = {
                        .target_as = &address_space_memory,
                        .iova = addr,
                        .translated_addr = 0,
                        .addr_mask = size - 1,
                        .perm = IOMMU_NONE,
                    },
                };
                memory_region_notify_iommu(&vtd_as->iommu, 0, event);
            }
        }
    }
}

static void vtd_iotlb_page_invalidate(IntelIOMMUState *s, uint16_t domain_id,
                                      hwaddr addr, uint8_t am)
{
    VTDIOTLBPageInvInfo info;

    trace_vtd_inv_desc_iotlb_pages(domain_id, addr, am);

    assert(am <= VTD_MAMV);
    info.domain_id = domain_id;
    info.addr = addr;
    info.mask = ~((1 << am) - 1);
    vtd_iommu_lock(s);
    g_hash_table_foreach_remove(s->iotlb, vtd_hash_remove_by_page, &info);
    vtd_iommu_unlock(s);
    vtd_iotlb_page_invalidate_notify(s, domain_id, addr, am, PCI_NO_PASID);
}

/* Flush IOTLB
 * Returns the IOTLB Actual Invalidation Granularity.
 * @val: the content of the IOTLB_REG
 */
static uint64_t vtd_iotlb_flush(IntelIOMMUState *s, uint64_t val)
{
    uint64_t iaig;
    uint64_t type = val & VTD_TLB_FLUSH_GRANU_MASK;
    uint16_t domain_id;
    hwaddr addr;
    uint8_t am;

    switch (type) {
    case VTD_TLB_GLOBAL_FLUSH:
        iaig = VTD_TLB_GLOBAL_FLUSH_A;
        vtd_iotlb_global_invalidate(s);
        break;

    case VTD_TLB_DSI_FLUSH:
        domain_id = VTD_TLB_DID(val);
        iaig = VTD_TLB_DSI_FLUSH_A;
        vtd_iotlb_domain_invalidate(s, domain_id);
        break;

    case VTD_TLB_PSI_FLUSH:
        domain_id = VTD_TLB_DID(val);
        addr = vtd_get_quad_raw(s, DMAR_IVA_REG);
        am = VTD_IVA_AM(addr);
        addr = VTD_IVA_ADDR(addr);
        if (am > VTD_MAMV) {
            error_report_once("%s: address mask overflow: 0x%" PRIx64,
                              __func__, vtd_get_quad_raw(s, DMAR_IVA_REG));
            iaig = 0;
            break;
        }
        iaig = VTD_TLB_PSI_FLUSH_A;
        vtd_iotlb_page_invalidate(s, domain_id, addr, am);
        break;

    default:
        error_report_once("%s: invalid granularity: 0x%" PRIx64,
                          __func__, val);
        iaig = 0;
    }
    return iaig;
}

static void vtd_fetch_inv_desc(IntelIOMMUState *s);

static inline bool vtd_queued_inv_disable_check(IntelIOMMUState *s)
{
    return s->qi_enabled && (s->iq_tail == s->iq_head) &&
           (s->iq_last_desc_type == VTD_INV_DESC_WAIT);
}

static void vtd_handle_gcmd_qie(IntelIOMMUState *s, bool en)
{
    uint64_t iqa_val = vtd_get_quad_raw(s, DMAR_IQA_REG);

    trace_vtd_inv_qi_enable(en);

    if (en) {
        s->iq = iqa_val & VTD_IQA_IQA_MASK(s->aw_bits);
        /* 2^(x+8) entries */
        s->iq_size = 1UL << ((iqa_val & VTD_IQA_QS) + 8 - (s->iq_dw ? 1 : 0));
        s->qi_enabled = true;
        trace_vtd_inv_qi_setup(s->iq, s->iq_size);
        /* Ok - report back to driver */
        vtd_set_clear_mask_long(s, DMAR_GSTS_REG, 0, VTD_GSTS_QIES);

        if (s->iq_tail != 0) {
            /*
             * This is a spec violation but Windows guests are known to set up
             * Queued Invalidation this way so we allow the write and process
             * Invalidation Descriptors right away.
             */
            trace_vtd_warn_invalid_qi_tail(s->iq_tail);
            if (!(vtd_get_long_raw(s, DMAR_FSTS_REG) & VTD_FSTS_IQE)) {
                vtd_fetch_inv_desc(s);
            }
        }
    } else {
        if (vtd_queued_inv_disable_check(s)) {
            /* disable Queued Invalidation */
            vtd_set_quad_raw(s, DMAR_IQH_REG, 0);
            s->iq_head = 0;
            s->qi_enabled = false;
            /* Ok - report back to driver */
            vtd_set_clear_mask_long(s, DMAR_GSTS_REG, VTD_GSTS_QIES, 0);
        } else {
            error_report_once("%s: detected improper state when disable QI "
                              "(head=0x%x, tail=0x%x, last_type=%d)",
                              __func__,
                              s->iq_head, s->iq_tail, s->iq_last_desc_type);
        }
    }
}

/* Set Root Table Pointer */
static void vtd_handle_gcmd_srtp(IntelIOMMUState *s)
{
    vtd_root_table_setup(s);
    /* Ok - report back to driver */
    vtd_set_clear_mask_long(s, DMAR_GSTS_REG, 0, VTD_GSTS_RTPS);
    vtd_reset_caches(s);
    vtd_address_space_refresh_all(s);
}

/* Set Interrupt Remap Table Pointer */
static void vtd_handle_gcmd_sirtp(IntelIOMMUState *s)
{
    vtd_interrupt_remap_table_setup(s);
    /* Ok - report back to driver */
    vtd_set_clear_mask_long(s, DMAR_GSTS_REG, 0, VTD_GSTS_IRTPS);
}

/* Handle Translation Enable/Disable */
static void vtd_handle_gcmd_te(IntelIOMMUState *s, bool en)
{
    if (s->dmar_enabled == en) {
        return;
    }

    trace_vtd_dmar_enable(en);

    if (en) {
        s->dmar_enabled = true;
        /* Ok - report back to driver */
        vtd_set_clear_mask_long(s, DMAR_GSTS_REG, 0, VTD_GSTS_TES);
    } else {
        s->dmar_enabled = false;

        /* Clear the index of Fault Recording Register */
        s->next_frcd_reg = 0;
        /* Ok - report back to driver */
        vtd_set_clear_mask_long(s, DMAR_GSTS_REG, VTD_GSTS_TES, 0);
    }

    vtd_reset_caches(s);
    vtd_address_space_refresh_all(s);
}

/* Handle Interrupt Remap Enable/Disable */
static void vtd_handle_gcmd_ire(IntelIOMMUState *s, bool en)
{
    trace_vtd_ir_enable(en);

    if (en) {
        s->intr_enabled = true;
        /* Ok - report back to driver */
        vtd_set_clear_mask_long(s, DMAR_GSTS_REG, 0, VTD_GSTS_IRES);
    } else {
        s->intr_enabled = false;
        /* Ok - report back to driver */
        vtd_set_clear_mask_long(s, DMAR_GSTS_REG, VTD_GSTS_IRES, 0);
    }
}

/* Handle write to Global Command Register */
static void vtd_handle_gcmd_write(IntelIOMMUState *s)
{
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(s);
    uint32_t status = vtd_get_long_raw(s, DMAR_GSTS_REG);
    uint32_t val = vtd_get_long_raw(s, DMAR_GCMD_REG);
    uint32_t changed = status ^ val;

    trace_vtd_reg_write_gcmd(status, val);
    if ((changed & VTD_GCMD_TE) && s->dma_translation) {
        /* Translation enable/disable */
        vtd_handle_gcmd_te(s, val & VTD_GCMD_TE);
    }
    if (val & VTD_GCMD_SRTP) {
        /* Set/update the root-table pointer */
        vtd_handle_gcmd_srtp(s);
    }
    if (changed & VTD_GCMD_QIE) {
        /* Queued Invalidation Enable */
        vtd_handle_gcmd_qie(s, val & VTD_GCMD_QIE);
    }
    if (val & VTD_GCMD_SIRTP) {
        /* Set/update the interrupt remapping root-table pointer */
        vtd_handle_gcmd_sirtp(s);
    }
    if ((changed & VTD_GCMD_IRE) &&
        x86_iommu_ir_supported(x86_iommu)) {
        /* Interrupt remap enable/disable */
        vtd_handle_gcmd_ire(s, val & VTD_GCMD_IRE);
    }
}

/* Handle write to Context Command Register */
static void vtd_handle_ccmd_write(IntelIOMMUState *s)
{
    uint64_t ret;
    uint64_t val = vtd_get_quad_raw(s, DMAR_CCMD_REG);

    /* Context-cache invalidation request */
    if (val & VTD_CCMD_ICC) {
        if (s->qi_enabled) {
            error_report_once("Queued Invalidation enabled, "
                              "should not use register-based invalidation");
            return;
        }
        ret = vtd_context_cache_invalidate(s, val);
        /* Invalidation completed. Change something to show */
        vtd_set_clear_mask_quad(s, DMAR_CCMD_REG, VTD_CCMD_ICC, 0ULL);
        ret = vtd_set_clear_mask_quad(s, DMAR_CCMD_REG, VTD_CCMD_CAIG_MASK,
                                      ret);
    }
}

/* Handle write to IOTLB Invalidation Register */
static void vtd_handle_iotlb_write(IntelIOMMUState *s)
{
    uint64_t ret;
    uint64_t val = vtd_get_quad_raw(s, DMAR_IOTLB_REG);

    /* IOTLB invalidation request */
    if (val & VTD_TLB_IVT) {
        if (s->qi_enabled) {
            error_report_once("Queued Invalidation enabled, "
                              "should not use register-based invalidation");
            return;
        }
        ret = vtd_iotlb_flush(s, val);
        /* Invalidation completed. Change something to show */
        vtd_set_clear_mask_quad(s, DMAR_IOTLB_REG, VTD_TLB_IVT, 0ULL);
        ret = vtd_set_clear_mask_quad(s, DMAR_IOTLB_REG,
                                      VTD_TLB_FLUSH_GRANU_MASK_A, ret);
    }
}

/* Fetch an Invalidation Descriptor from the Invalidation Queue */
static bool vtd_get_inv_desc(IntelIOMMUState *s,
                             VTDInvDesc *inv_desc)
{
    dma_addr_t base_addr = s->iq;
    uint32_t offset = s->iq_head;
    uint32_t dw = s->iq_dw ? 32 : 16;
    dma_addr_t addr = base_addr + offset * dw;

    if (dma_memory_read(&address_space_memory, addr,
                        inv_desc, dw, MEMTXATTRS_UNSPECIFIED)) {
        error_report_once("Read INV DESC failed.");
        return false;
    }
    inv_desc->lo = le64_to_cpu(inv_desc->lo);
    inv_desc->hi = le64_to_cpu(inv_desc->hi);
    if (dw == 32) {
        inv_desc->val[2] = le64_to_cpu(inv_desc->val[2]);
        inv_desc->val[3] = le64_to_cpu(inv_desc->val[3]);
    }
    return true;
}

static bool vtd_inv_desc_reserved_check(IntelIOMMUState *s,
                                        VTDInvDesc *inv_desc,
                                        uint64_t mask[4], bool dw,
                                        const char *func_name,
                                        const char *desc_type)
{
    if (s->iq_dw) {
        if (inv_desc->val[0] & mask[0] || inv_desc->val[1] & mask[1] ||
            inv_desc->val[2] & mask[2] || inv_desc->val[3] & mask[3]) {
            error_report("%s: invalid %s desc val[3]: 0x%"PRIx64
                         " val[2]: 0x%"PRIx64" val[1]=0x%"PRIx64
                         " val[0]=0x%"PRIx64" (reserved nonzero)",
                         func_name, desc_type, inv_desc->val[3],
                         inv_desc->val[2], inv_desc->val[1],
                         inv_desc->val[0]);
            return false;
        }
    } else {
        if (dw) {
            error_report("%s: 256-bit %s desc in 128-bit invalidation queue",
                         func_name, desc_type);
            return false;
        }

        if (inv_desc->lo & mask[0] || inv_desc->hi & mask[1]) {
            error_report("%s: invalid %s desc: hi=%"PRIx64", lo=%"PRIx64
                         " (reserved nonzero)", func_name, desc_type,
                         inv_desc->hi, inv_desc->lo);
            return false;
        }
    }

    return true;
}

static bool vtd_process_wait_desc(IntelIOMMUState *s, VTDInvDesc *inv_desc)
{
    uint64_t mask[4] = {VTD_INV_DESC_WAIT_RSVD_LO, VTD_INV_DESC_WAIT_RSVD_HI,
                        VTD_INV_DESC_ALL_ONE, VTD_INV_DESC_ALL_ONE};

    if (!vtd_inv_desc_reserved_check(s, inv_desc, mask, false,
                                     __func__, "wait")) {
        return false;
    }

    if (inv_desc->lo & VTD_INV_DESC_WAIT_SW) {
        /* Status Write */
        uint32_t status_data = (uint32_t)(inv_desc->lo >>
                               VTD_INV_DESC_WAIT_DATA_SHIFT);

        assert(!(inv_desc->lo & VTD_INV_DESC_WAIT_IF));

        /* FIXME: need to be masked with HAW? */
        dma_addr_t status_addr = inv_desc->hi;
        trace_vtd_inv_desc_wait_sw(status_addr, status_data);
        status_data = cpu_to_le32(status_data);
        if (dma_memory_write(&address_space_memory, status_addr,
                             &status_data, sizeof(status_data),
                             MEMTXATTRS_UNSPECIFIED)) {
            trace_vtd_inv_desc_wait_write_fail(inv_desc->hi, inv_desc->lo);
            return false;
        }
    } else if (inv_desc->lo & VTD_INV_DESC_WAIT_IF) {
        /* Interrupt flag */
        vtd_generate_completion_event(s);
    } else {
        error_report_once("%s: invalid wait desc: hi=%"PRIx64", lo=%"PRIx64
                          " (unknown type)", __func__, inv_desc->hi,
                          inv_desc->lo);
        return false;
    }
    return true;
}

static bool vtd_process_context_cache_desc(IntelIOMMUState *s,
                                           VTDInvDesc *inv_desc)
{
    uint16_t sid, fmask;
    uint64_t mask[4] = {VTD_INV_DESC_CC_RSVD, VTD_INV_DESC_ALL_ONE,
                        VTD_INV_DESC_ALL_ONE, VTD_INV_DESC_ALL_ONE};

    if (!vtd_inv_desc_reserved_check(s, inv_desc, mask, false,
                                     __func__, "cc inv")) {
        return false;
    }

    switch (inv_desc->lo & VTD_INV_DESC_CC_G) {
    case VTD_INV_DESC_CC_DOMAIN:
        trace_vtd_inv_desc_cc_domain(
            (uint16_t)VTD_INV_DESC_CC_DID(inv_desc->lo));
        /* Fall through */
    case VTD_INV_DESC_CC_GLOBAL:
        vtd_context_global_invalidate(s);
        break;

    case VTD_INV_DESC_CC_DEVICE:
        sid = VTD_INV_DESC_CC_SID(inv_desc->lo);
        fmask = VTD_INV_DESC_CC_FM(inv_desc->lo);
        vtd_context_device_invalidate(s, sid, fmask);
        break;

    default:
        error_report_once("%s: invalid cc inv desc: hi=%"PRIx64", lo=%"PRIx64
                          " (invalid type)", __func__, inv_desc->hi,
                          inv_desc->lo);
        return false;
    }
    return true;
}

static bool vtd_process_iotlb_desc(IntelIOMMUState *s, VTDInvDesc *inv_desc)
{
    uint16_t domain_id;
    uint8_t am;
    hwaddr addr;
    uint64_t mask[4] = {VTD_INV_DESC_IOTLB_RSVD_LO, VTD_INV_DESC_IOTLB_RSVD_HI,
                        VTD_INV_DESC_ALL_ONE, VTD_INV_DESC_ALL_ONE};

    if (!vtd_inv_desc_reserved_check(s, inv_desc, mask, false,
                                     __func__, "iotlb inv")) {
        return false;
    }

    switch (inv_desc->lo & VTD_INV_DESC_IOTLB_G) {
    case VTD_INV_DESC_IOTLB_GLOBAL:
        vtd_iotlb_global_invalidate(s);
        break;

    case VTD_INV_DESC_IOTLB_DOMAIN:
        domain_id = VTD_INV_DESC_IOTLB_DID(inv_desc->lo);
        vtd_iotlb_domain_invalidate(s, domain_id);
        break;

    case VTD_INV_DESC_IOTLB_PAGE:
        domain_id = VTD_INV_DESC_IOTLB_DID(inv_desc->lo);
        addr = VTD_INV_DESC_IOTLB_ADDR(inv_desc->hi);
        am = VTD_INV_DESC_IOTLB_AM(inv_desc->hi);
        if (am > VTD_MAMV) {
            error_report_once("%s: invalid iotlb inv desc: hi=0x%"PRIx64
                              ", lo=0x%"PRIx64" (am=%u > VTD_MAMV=%u)",
                              __func__, inv_desc->hi, inv_desc->lo,
                              am, (unsigned)VTD_MAMV);
            return false;
        }
        vtd_iotlb_page_invalidate(s, domain_id, addr, am);
        break;

    default:
        error_report_once("%s: invalid iotlb inv desc: hi=0x%"PRIx64
                          ", lo=0x%"PRIx64" (type mismatch: 0x%llx)",
                          __func__, inv_desc->hi, inv_desc->lo,
                          inv_desc->lo & VTD_INV_DESC_IOTLB_G);
        return false;
    }
    return true;
}

static gboolean vtd_hash_remove_by_pasid(gpointer key, gpointer value,
                                         gpointer user_data)
{
    VTDIOTLBEntry *entry = (VTDIOTLBEntry *)value;
    VTDIOTLBPageInvInfo *info = (VTDIOTLBPageInvInfo *)user_data;

    return ((entry->domain_id == info->domain_id) &&
            (entry->pasid == info->pasid));
}

static void vtd_piotlb_pasid_invalidate(IntelIOMMUState *s,
                                        uint16_t domain_id, uint32_t pasid)
{
    VTDIOTLBPageInvInfo info;
    VTDAddressSpace *vtd_as;
    VTDContextEntry ce;

    info.domain_id = domain_id;
    info.pasid = pasid;

    vtd_iommu_lock(s);
    g_hash_table_foreach_remove(s->iotlb, vtd_hash_remove_by_pasid,
                                &info);
    vtd_iommu_unlock(s);

    QLIST_FOREACH(vtd_as, &s->vtd_as_with_notifiers, next) {
        if (!vtd_dev_to_context_entry(s, pci_bus_num(vtd_as->bus),
                                      vtd_as->devfn, &ce) &&
            domain_id == vtd_get_domain_id(s, &ce, vtd_as->pasid)) {
            uint32_t rid2pasid = VTD_CE_GET_RID2PASID(&ce);

            if ((vtd_as->pasid != PCI_NO_PASID || pasid != rid2pasid) &&
                vtd_as->pasid != pasid) {
                continue;
            }

            if (!s->flts || !vtd_as_has_map_notifier(vtd_as)) {
                vtd_address_space_sync(vtd_as);
            }
        }
    }
}

static void vtd_piotlb_page_invalidate(IntelIOMMUState *s, uint16_t domain_id,
                                       uint32_t pasid, hwaddr addr, uint8_t am)
{
    VTDIOTLBPageInvInfo info;

    info.domain_id = domain_id;
    info.pasid = pasid;
    info.addr = addr;
    info.mask = ~((1 << am) - 1);

    vtd_iommu_lock(s);
    g_hash_table_foreach_remove(s->iotlb,
                                vtd_hash_remove_by_page_piotlb, &info);
    vtd_iommu_unlock(s);

    vtd_iotlb_page_invalidate_notify(s, domain_id, addr, am, pasid);
}

static bool vtd_process_piotlb_desc(IntelIOMMUState *s,
                                    VTDInvDesc *inv_desc)
{
    uint16_t domain_id;
    uint32_t pasid;
    hwaddr addr;
    uint8_t am;
    uint64_t mask[4] = {VTD_INV_DESC_PIOTLB_RSVD_VAL0,
                        VTD_INV_DESC_PIOTLB_RSVD_VAL1,
                        VTD_INV_DESC_ALL_ONE, VTD_INV_DESC_ALL_ONE};

    if (!vtd_inv_desc_reserved_check(s, inv_desc, mask, true,
                                     __func__, "piotlb inv")) {
        return false;
    }

    domain_id = VTD_INV_DESC_PIOTLB_DID(inv_desc->val[0]);
    pasid = VTD_INV_DESC_PIOTLB_PASID(inv_desc->val[0]);
    switch (inv_desc->val[0] & VTD_INV_DESC_PIOTLB_G) {
    case VTD_INV_DESC_PIOTLB_ALL_IN_PASID:
        vtd_piotlb_pasid_invalidate(s, domain_id, pasid);
        break;

    case VTD_INV_DESC_PIOTLB_PSI_IN_PASID:
        am = VTD_INV_DESC_PIOTLB_AM(inv_desc->val[1]);
        addr = (hwaddr) VTD_INV_DESC_PIOTLB_ADDR(inv_desc->val[1]);
        vtd_piotlb_page_invalidate(s, domain_id, pasid, addr, am);
        break;

    default:
        error_report_once("%s: invalid piotlb inv desc: hi=0x%"PRIx64
                          ", lo=0x%"PRIx64" (type mismatch: 0x%llx)",
                          __func__, inv_desc->val[1], inv_desc->val[0],
                          inv_desc->val[0] & VTD_INV_DESC_IOTLB_G);
        return false;
    }
    return true;
}

static bool vtd_process_inv_iec_desc(IntelIOMMUState *s,
                                     VTDInvDesc *inv_desc)
{
    uint64_t mask[4] = {VTD_INV_DESC_IEC_RSVD, VTD_INV_DESC_ALL_ONE,
                        VTD_INV_DESC_ALL_ONE, VTD_INV_DESC_ALL_ONE};

    if (!vtd_inv_desc_reserved_check(s, inv_desc, mask, false,
                                     __func__, "iec inv")) {
        return false;
    }

    trace_vtd_inv_desc_iec(inv_desc->iec.granularity,
                           inv_desc->iec.index,
                           inv_desc->iec.index_mask);

    vtd_iec_notify_all(s, !inv_desc->iec.granularity,
                       inv_desc->iec.index,
                       inv_desc->iec.index_mask);
    return true;
}

static void do_invalidate_device_tlb(VTDAddressSpace *vtd_dev_as,
                                     bool size, hwaddr addr)
{
    /*
     * According to ATS spec table 2.4:
     * S = 0, bits 15:12 = xxxx     range size: 4K
     * S = 1, bits 15:12 = xxx0     range size: 8K
     * S = 1, bits 15:12 = xx01     range size: 16K
     * S = 1, bits 15:12 = x011     range size: 32K
     * S = 1, bits 15:12 = 0111     range size: 64K
     * ...
     */

    IOMMUTLBEvent event;
    uint64_t sz;

    if (size) {
        sz = (VTD_PAGE_SIZE * 2) << cto64(addr >> VTD_PAGE_SHIFT);
        addr &= ~(sz - 1);
    } else {
        sz = VTD_PAGE_SIZE;
    }

    event.type = IOMMU_NOTIFIER_DEVIOTLB_UNMAP;
    event.entry.target_as = &vtd_dev_as->as;
    event.entry.addr_mask = sz - 1;
    event.entry.iova = addr;
    event.entry.perm = IOMMU_NONE;
    event.entry.translated_addr = 0;
    memory_region_notify_iommu(&vtd_dev_as->iommu, 0, event);
}

static bool vtd_process_device_piotlb_desc(IntelIOMMUState *s,
                                           VTDInvDesc *inv_desc)
{
    uint16_t sid;
    VTDAddressSpace *vtd_dev_as;
    bool size;
    bool global;
    hwaddr addr;
    uint32_t pasid;
    uint64_t mask[4] = {VTD_INV_DESC_PASID_DEVICE_IOTLB_RSVD_VAL0,
                        VTD_INV_DESC_PASID_DEVICE_IOTLB_RSVD_VAL1,
                        VTD_INV_DESC_ALL_ONE, VTD_INV_DESC_ALL_ONE};

    if (!vtd_inv_desc_reserved_check(s, inv_desc, mask, true,
                                     __func__, "device piotlb inv")) {
        return false;
    }

    global = VTD_INV_DESC_PASID_DEVICE_IOTLB_GLOBAL(inv_desc->hi);
    size = VTD_INV_DESC_PASID_DEVICE_IOTLB_SIZE(inv_desc->hi);
    addr = VTD_INV_DESC_PASID_DEVICE_IOTLB_ADDR(inv_desc->hi);
    sid = VTD_INV_DESC_PASID_DEVICE_IOTLB_SID(inv_desc->lo);
    if (global) {
        QLIST_FOREACH(vtd_dev_as, &s->vtd_as_with_notifiers, next) {
            if ((vtd_dev_as->pasid != PCI_NO_PASID) &&
                (PCI_BUILD_BDF(pci_bus_num(vtd_dev_as->bus),
                                           vtd_dev_as->devfn) == sid)) {
                do_invalidate_device_tlb(vtd_dev_as, size, addr);
            }
        }
    } else {
        pasid = VTD_INV_DESC_PASID_DEVICE_IOTLB_PASID(inv_desc->lo);
        vtd_dev_as = vtd_get_as_by_sid_and_pasid(s, sid, pasid);
        if (!vtd_dev_as) {
            return true;
        }

        do_invalidate_device_tlb(vtd_dev_as, size, addr);
    }

    return true;
}

static bool vtd_process_device_iotlb_desc(IntelIOMMUState *s,
                                          VTDInvDesc *inv_desc)
{
    VTDAddressSpace *vtd_dev_as;
    hwaddr addr;
    uint16_t sid;
    bool size;
    uint64_t mask[4] = {VTD_INV_DESC_DEVICE_IOTLB_RSVD_LO,
                        VTD_INV_DESC_DEVICE_IOTLB_RSVD_HI,
                        VTD_INV_DESC_ALL_ONE, VTD_INV_DESC_ALL_ONE};

    if (!vtd_inv_desc_reserved_check(s, inv_desc, mask, false,
                                     __func__, "dev-iotlb inv")) {
        return false;
    }

    addr = VTD_INV_DESC_DEVICE_IOTLB_ADDR(inv_desc->hi);
    sid = VTD_INV_DESC_DEVICE_IOTLB_SID(inv_desc->lo);
    size = VTD_INV_DESC_DEVICE_IOTLB_SIZE(inv_desc->hi);

    /*
     * Using sid is OK since the guest should have finished the
     * initialization of both the bus and device.
     */
    vtd_dev_as = vtd_get_as_by_sid(s, sid);
    if (!vtd_dev_as) {
        goto done;
    }

    do_invalidate_device_tlb(vtd_dev_as, size, addr);

done:
    return true;
}

static bool vtd_process_inv_desc(IntelIOMMUState *s)
{
    VTDInvDesc inv_desc;
    uint8_t desc_type;

    trace_vtd_inv_qi_head(s->iq_head);
    if (!vtd_get_inv_desc(s, &inv_desc)) {
        s->iq_last_desc_type = VTD_INV_DESC_NONE;
        return false;
    }

    desc_type = VTD_INV_DESC_TYPE(inv_desc.lo);
    /* FIXME: should update at first or at last? */
    s->iq_last_desc_type = desc_type;

    switch (desc_type) {
    case VTD_INV_DESC_CC:
        trace_vtd_inv_desc("context-cache", inv_desc.hi, inv_desc.lo);
        if (!vtd_process_context_cache_desc(s, &inv_desc)) {
            return false;
        }
        break;

    case VTD_INV_DESC_IOTLB:
        trace_vtd_inv_desc("iotlb", inv_desc.hi, inv_desc.lo);
        if (!vtd_process_iotlb_desc(s, &inv_desc)) {
            return false;
        }
        break;

    case VTD_INV_DESC_PIOTLB:
        trace_vtd_inv_desc("p-iotlb", inv_desc.val[1], inv_desc.val[0]);
        if (!vtd_process_piotlb_desc(s, &inv_desc)) {
            return false;
        }
        break;

    case VTD_INV_DESC_WAIT:
        trace_vtd_inv_desc("wait", inv_desc.hi, inv_desc.lo);
        if (!vtd_process_wait_desc(s, &inv_desc)) {
            return false;
        }
        break;

    case VTD_INV_DESC_IEC:
        trace_vtd_inv_desc("iec", inv_desc.hi, inv_desc.lo);
        if (!vtd_process_inv_iec_desc(s, &inv_desc)) {
            return false;
        }
        break;

    case VTD_INV_DESC_DEV_PIOTLB:
        trace_vtd_inv_desc("device-piotlb", inv_desc.hi, inv_desc.lo);
        if (!vtd_process_device_piotlb_desc(s, &inv_desc)) {
            return false;
        }
        break;

    case VTD_INV_DESC_DEVICE:
        trace_vtd_inv_desc("device", inv_desc.hi, inv_desc.lo);
        if (!vtd_process_device_iotlb_desc(s, &inv_desc)) {
            return false;
        }
        break;

    /*
     * TODO: the entity of below two cases will be implemented in future series.
     * To make guest (which integrates scalable mode support patch set in
     * iommu driver) work, just return true is enough so far.
     */
    case VTD_INV_DESC_PC:
        if (s->scalable_mode) {
            break;
        }
    /* fallthrough */
    default:
        error_report_once("%s: invalid inv desc: hi=%"PRIx64", lo=%"PRIx64
                          " (unknown type)", __func__, inv_desc.hi,
                          inv_desc.lo);
        return false;
    }
    s->iq_head++;
    if (s->iq_head == s->iq_size) {
        s->iq_head = 0;
    }
    return true;
}

/* Try to fetch and process more Invalidation Descriptors */
static void vtd_fetch_inv_desc(IntelIOMMUState *s)
{
    int qi_shift;

    /* Refer to 10.4.23 of VT-d spec 3.0 */
    qi_shift = s->iq_dw ? VTD_IQH_QH_SHIFT_5 : VTD_IQH_QH_SHIFT_4;

    trace_vtd_inv_qi_fetch();

    if (s->iq_tail >= s->iq_size) {
        /* Detects an invalid Tail pointer */
        error_report_once("%s: detected invalid QI tail "
                          "(tail=0x%x, size=0x%x)",
                          __func__, s->iq_tail, s->iq_size);
        vtd_handle_inv_queue_error(s);
        return;
    }
    while (s->iq_head != s->iq_tail) {
        if (!vtd_process_inv_desc(s)) {
            /* Invalidation Queue Errors */
            vtd_handle_inv_queue_error(s);
            break;
        }
        /* Must update the IQH_REG in time */
        vtd_set_quad_raw(s, DMAR_IQH_REG,
                         (((uint64_t)(s->iq_head)) << qi_shift) &
                         VTD_IQH_QH_MASK);
    }
}

/* Handle write to Invalidation Queue Tail Register */
static void vtd_handle_iqt_write(IntelIOMMUState *s)
{
    uint64_t val = vtd_get_quad_raw(s, DMAR_IQT_REG);

    if (s->iq_dw && (val & VTD_IQT_QT_256_RSV_BIT)) {
        error_report_once("%s: RSV bit is set: val=0x%"PRIx64,
                          __func__, val);
        vtd_handle_inv_queue_error(s);
        return;
    }
    s->iq_tail = VTD_IQT_QT(s->iq_dw, val);
    trace_vtd_inv_qi_tail(s->iq_tail);

    if (s->qi_enabled && !(vtd_get_long_raw(s, DMAR_FSTS_REG) & VTD_FSTS_IQE)) {
        /* Process Invalidation Queue here */
        vtd_fetch_inv_desc(s);
    }
}

static void vtd_handle_fsts_write(IntelIOMMUState *s)
{
    uint32_t fsts_reg = vtd_get_long_raw(s, DMAR_FSTS_REG);
    uint32_t fectl_reg = vtd_get_long_raw(s, DMAR_FECTL_REG);
    uint32_t status_fields = VTD_FSTS_PFO | VTD_FSTS_PPF | VTD_FSTS_IQE;

    if ((fectl_reg & VTD_FECTL_IP) && !(fsts_reg & status_fields)) {
        vtd_set_clear_mask_long(s, DMAR_FECTL_REG, VTD_FECTL_IP, 0);
        trace_vtd_fsts_clear_ip();
    }
    /* FIXME: when IQE is Clear, should we try to fetch some Invalidation
     * Descriptors if there are any when Queued Invalidation is enabled?
     */
}

static void vtd_handle_fectl_write(IntelIOMMUState *s)
{
    uint32_t fectl_reg;
    /* FIXME: when software clears the IM field, check the IP field. But do we
     * need to compare the old value and the new value to conclude that
     * software clears the IM field? Or just check if the IM field is zero?
     */
    fectl_reg = vtd_get_long_raw(s, DMAR_FECTL_REG);

    trace_vtd_reg_write_fectl(fectl_reg);

    if ((fectl_reg & VTD_FECTL_IP) && !(fectl_reg & VTD_FECTL_IM)) {
        vtd_generate_interrupt(s, DMAR_FEADDR_REG, DMAR_FEDATA_REG);
        vtd_set_clear_mask_long(s, DMAR_FECTL_REG, VTD_FECTL_IP, 0);
    }
}

static void vtd_handle_ics_write(IntelIOMMUState *s)
{
    uint32_t ics_reg = vtd_get_long_raw(s, DMAR_ICS_REG);
    uint32_t iectl_reg = vtd_get_long_raw(s, DMAR_IECTL_REG);

    if ((iectl_reg & VTD_IECTL_IP) && !(ics_reg & VTD_ICS_IWC)) {
        trace_vtd_reg_ics_clear_ip();
        vtd_set_clear_mask_long(s, DMAR_IECTL_REG, VTD_IECTL_IP, 0);
    }
}

static void vtd_handle_iectl_write(IntelIOMMUState *s)
{
    uint32_t iectl_reg;
    /* FIXME: when software clears the IM field, check the IP field. But do we
     * need to compare the old value and the new value to conclude that
     * software clears the IM field? Or just check if the IM field is zero?
     */
    iectl_reg = vtd_get_long_raw(s, DMAR_IECTL_REG);

    trace_vtd_reg_write_iectl(iectl_reg);

    if ((iectl_reg & VTD_IECTL_IP) && !(iectl_reg & VTD_IECTL_IM)) {
        vtd_generate_interrupt(s, DMAR_IEADDR_REG, DMAR_IEDATA_REG);
        vtd_set_clear_mask_long(s, DMAR_IECTL_REG, VTD_IECTL_IP, 0);
    }
}

static uint64_t vtd_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    IntelIOMMUState *s = opaque;
    uint64_t val;

    trace_vtd_reg_read(addr, size);

    if (addr + size > DMAR_REG_SIZE) {
        error_report_once("%s: MMIO over range: addr=0x%" PRIx64
                          " size=0x%x", __func__, addr, size);
        return (uint64_t)-1;
    }

    switch (addr) {
    /* Root Table Address Register, 64-bit */
    case DMAR_RTADDR_REG:
        val = vtd_get_quad_raw(s, DMAR_RTADDR_REG);
        if (size == 4) {
            val = val & ((1ULL << 32) - 1);
        }
        break;

    case DMAR_RTADDR_REG_HI:
        assert(size == 4);
        val = vtd_get_quad_raw(s, DMAR_RTADDR_REG) >> 32;
        break;

    /* Invalidation Queue Address Register, 64-bit */
    case DMAR_IQA_REG:
        val = s->iq |
              (vtd_get_quad(s, DMAR_IQA_REG) &
              (VTD_IQA_QS | VTD_IQA_DW_MASK));
        if (size == 4) {
            val = val & ((1ULL << 32) - 1);
        }
        break;

    case DMAR_IQA_REG_HI:
        assert(size == 4);
        val = s->iq >> 32;
        break;

    default:
        if (size == 4) {
            val = vtd_get_long(s, addr);
        } else {
            val = vtd_get_quad(s, addr);
        }
    }

    return val;
}

static void vtd_mem_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    IntelIOMMUState *s = opaque;

    trace_vtd_reg_write(addr, size, val);

    if (addr + size > DMAR_REG_SIZE) {
        error_report_once("%s: MMIO over range: addr=0x%" PRIx64
                          " size=0x%x", __func__, addr, size);
        return;
    }

    switch (addr) {
    /* Global Command Register, 32-bit */
    case DMAR_GCMD_REG:
        vtd_set_long(s, addr, val);
        vtd_handle_gcmd_write(s);
        break;

    /* Context Command Register, 64-bit */
    case DMAR_CCMD_REG:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
            vtd_handle_ccmd_write(s);
        }
        break;

    case DMAR_CCMD_REG_HI:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        vtd_handle_ccmd_write(s);
        break;

    /* IOTLB Invalidation Register, 64-bit */
    case DMAR_IOTLB_REG:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
            vtd_handle_iotlb_write(s);
        }
        break;

    case DMAR_IOTLB_REG_HI:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        vtd_handle_iotlb_write(s);
        break;

    /* Invalidate Address Register, 64-bit */
    case DMAR_IVA_REG:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
        }
        break;

    case DMAR_IVA_REG_HI:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Fault Status Register, 32-bit */
    case DMAR_FSTS_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        vtd_handle_fsts_write(s);
        break;

    /* Fault Event Control Register, 32-bit */
    case DMAR_FECTL_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        vtd_handle_fectl_write(s);
        break;

    /* Fault Event Data Register, 32-bit */
    case DMAR_FEDATA_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Fault Event Address Register, 32-bit */
    case DMAR_FEADDR_REG:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            /*
             * While the register is 32-bit only, some guests (Xen...) write to
             * it with 64-bit.
             */
            vtd_set_quad(s, addr, val);
        }
        break;

    /* Fault Event Upper Address Register, 32-bit */
    case DMAR_FEUADDR_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Protected Memory Enable Register, 32-bit */
    case DMAR_PMEN_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Root Table Address Register, 64-bit */
    case DMAR_RTADDR_REG:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
        }
        break;

    case DMAR_RTADDR_REG_HI:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Invalidation Queue Tail Register, 64-bit */
    case DMAR_IQT_REG:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
        }
        vtd_handle_iqt_write(s);
        break;

    case DMAR_IQT_REG_HI:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        /* 19:63 of IQT_REG is RsvdZ, do nothing here */
        break;

    /* Invalidation Queue Address Register, 64-bit */
    case DMAR_IQA_REG:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
        }
        vtd_update_iq_dw(s);
        break;

    case DMAR_IQA_REG_HI:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Invalidation Completion Status Register, 32-bit */
    case DMAR_ICS_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        vtd_handle_ics_write(s);
        break;

    /* Invalidation Event Control Register, 32-bit */
    case DMAR_IECTL_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        vtd_handle_iectl_write(s);
        break;

    /* Invalidation Event Data Register, 32-bit */
    case DMAR_IEDATA_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Invalidation Event Address Register, 32-bit */
    case DMAR_IEADDR_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Invalidation Event Upper Address Register, 32-bit */
    case DMAR_IEUADDR_REG:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    /* Fault Recording Registers, 128-bit */
    case DMAR_FRCD_REG_0_0:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
        }
        break;

    case DMAR_FRCD_REG_0_1:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    case DMAR_FRCD_REG_0_2:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
            /* May clear bit 127 (Fault), update PPF */
            vtd_update_fsts_ppf(s);
        }
        break;

    case DMAR_FRCD_REG_0_3:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        /* May clear bit 127 (Fault), update PPF */
        vtd_update_fsts_ppf(s);
        break;

    case DMAR_IRTA_REG:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
        }
        break;

    case DMAR_IRTA_REG_HI:
        assert(size == 4);
        vtd_set_long(s, addr, val);
        break;

    default:
        if (size == 4) {
            vtd_set_long(s, addr, val);
        } else {
            vtd_set_quad(s, addr, val);
        }
    }
}

static IOMMUTLBEntry vtd_iommu_translate(IOMMUMemoryRegion *iommu, hwaddr addr,
                                         IOMMUAccessFlags flag, int iommu_idx)
{
    VTDAddressSpace *vtd_as = container_of(iommu, VTDAddressSpace, iommu);
    IntelIOMMUState *s = vtd_as->iommu_state;
    IOMMUTLBEntry iotlb = {
        /* We'll fill in the rest later. */
        .target_as = &address_space_memory,
    };
    bool success;

    if (likely(s->dmar_enabled)) {
        success = vtd_do_iommu_translate(vtd_as, vtd_as->bus, vtd_as->devfn,
                                         addr, flag & IOMMU_WO, &iotlb);
    } else {
        /* DMAR disabled, passthrough, use 4k-page*/
        iotlb.iova = addr & VTD_PAGE_MASK_4K;
        iotlb.translated_addr = addr & VTD_PAGE_MASK_4K;
        iotlb.addr_mask = ~VTD_PAGE_MASK_4K;
        iotlb.perm = IOMMU_RW;
        success = true;
    }

    if (likely(success)) {
        trace_vtd_dmar_translate(pci_bus_num(vtd_as->bus),
                                 VTD_PCI_SLOT(vtd_as->devfn),
                                 VTD_PCI_FUNC(vtd_as->devfn),
                                 iotlb.iova, iotlb.translated_addr,
                                 iotlb.addr_mask);
    } else {
        error_report_once("%s: detected translation failure "
                          "(dev=%02x:%02x:%02x, iova=0x%" PRIx64 ")",
                          __func__, pci_bus_num(vtd_as->bus),
                          VTD_PCI_SLOT(vtd_as->devfn),
                          VTD_PCI_FUNC(vtd_as->devfn),
                          addr);
    }

    return iotlb;
}

static int vtd_iommu_notify_flag_changed(IOMMUMemoryRegion *iommu,
                                         IOMMUNotifierFlag old,
                                         IOMMUNotifierFlag new,
                                         Error **errp)
{
    VTDAddressSpace *vtd_as = container_of(iommu, VTDAddressSpace, iommu);
    IntelIOMMUState *s = vtd_as->iommu_state;
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(s);

    /* TODO: add support for VFIO and vhost users */
    if (s->snoop_control) {
        error_setg_errno(errp, ENOTSUP,
                         "Snoop Control with vhost or VFIO is not supported");
        return -ENOTSUP;
    }
    if (!s->caching_mode && (new & IOMMU_NOTIFIER_MAP)) {
        error_setg_errno(errp, ENOTSUP,
                         "device %02x.%02x.%x requires caching mode",
                         pci_bus_num(vtd_as->bus), PCI_SLOT(vtd_as->devfn),
                         PCI_FUNC(vtd_as->devfn));
        return -ENOTSUP;
    }
    if (!x86_iommu->dt_supported && (new & IOMMU_NOTIFIER_DEVIOTLB_UNMAP)) {
        error_setg_errno(errp, ENOTSUP,
                         "device %02x.%02x.%x requires device IOTLB mode",
                         pci_bus_num(vtd_as->bus), PCI_SLOT(vtd_as->devfn),
                         PCI_FUNC(vtd_as->devfn));
        return -ENOTSUP;
    }

    /* Update per-address-space notifier flags */
    vtd_as->notifier_flags = new;

    if (old == IOMMU_NOTIFIER_NONE) {
        QLIST_INSERT_HEAD(&s->vtd_as_with_notifiers, vtd_as, next);
    } else if (new == IOMMU_NOTIFIER_NONE) {
        QLIST_REMOVE(vtd_as, next);
    }
    return 0;
}

static int vtd_post_load(void *opaque, int version_id)
{
    IntelIOMMUState *iommu = opaque;

    /*
     * We don't need to migrate the root_scalable because we can
     * simply do the calculation after the loading is complete.  We
     * can actually do similar things with root, dmar_enabled, etc.
     * however since we've had them already so we'd better keep them
     * for compatibility of migration.
     */
    vtd_update_scalable_state(iommu);

    vtd_update_iq_dw(iommu);

    /*
     * Memory regions are dynamically turned on/off depending on
     * context entry configurations from the guest. After migration,
     * we need to make sure the memory regions are still correct.
     */
    vtd_switch_address_space_all(iommu);

    return 0;
}

static const VMStateDescription vtd_vmstate = {
    .name = "iommu-intel",
    .version_id = 1,
    .minimum_version_id = 1,
    .priority = MIG_PRI_IOMMU,
    .post_load = vtd_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(root, IntelIOMMUState),
        VMSTATE_UINT64(intr_root, IntelIOMMUState),
        VMSTATE_UINT64(iq, IntelIOMMUState),
        VMSTATE_UINT32(intr_size, IntelIOMMUState),
        VMSTATE_UINT16(iq_head, IntelIOMMUState),
        VMSTATE_UINT16(iq_tail, IntelIOMMUState),
        VMSTATE_UINT16(iq_size, IntelIOMMUState),
        VMSTATE_UINT16(next_frcd_reg, IntelIOMMUState),
        VMSTATE_UINT8_ARRAY(csr, IntelIOMMUState, DMAR_REG_SIZE),
        VMSTATE_UINT8(iq_last_desc_type, IntelIOMMUState),
        VMSTATE_UNUSED(1),      /* bool root_extended is obsolete by VT-d */
        VMSTATE_BOOL(dmar_enabled, IntelIOMMUState),
        VMSTATE_BOOL(qi_enabled, IntelIOMMUState),
        VMSTATE_BOOL(intr_enabled, IntelIOMMUState),
        VMSTATE_BOOL(intr_eime, IntelIOMMUState),
        VMSTATE_END_OF_LIST()
    }
};

static const MemoryRegionOps vtd_mem_ops = {
    .read = vtd_mem_read,
    .write = vtd_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static const Property vtd_properties[] = {
    DEFINE_PROP_UINT32("version", IntelIOMMUState, version, 0),
    DEFINE_PROP_ON_OFF_AUTO("eim", IntelIOMMUState, intr_eim,
                            ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BOOL("x-buggy-eim", IntelIOMMUState, buggy_eim, false),
    DEFINE_PROP_UINT8("aw-bits", IntelIOMMUState, aw_bits,
                      VTD_HOST_ADDRESS_WIDTH),
    DEFINE_PROP_BOOL("caching-mode", IntelIOMMUState, caching_mode, FALSE),
    DEFINE_PROP_BOOL("x-scalable-mode", IntelIOMMUState, scalable_mode, FALSE),
    DEFINE_PROP_BOOL("x-flts", IntelIOMMUState, flts, FALSE),
    DEFINE_PROP_BOOL("snoop-control", IntelIOMMUState, snoop_control, false),
    DEFINE_PROP_BOOL("x-pasid-mode", IntelIOMMUState, pasid, false),
    DEFINE_PROP_BOOL("dma-drain", IntelIOMMUState, dma_drain, true),
    DEFINE_PROP_BOOL("dma-translation", IntelIOMMUState, dma_translation, true),
    DEFINE_PROP_BOOL("stale-tm", IntelIOMMUState, stale_tm, false),
    DEFINE_PROP_BOOL("fs1gp", IntelIOMMUState, fs1gp, true),
};

/* Read IRTE entry with specific index */
static bool vtd_irte_get(IntelIOMMUState *iommu, uint16_t index,
                         VTD_IR_TableEntry *entry, uint16_t sid,
                         bool do_fault)
{
    static const uint16_t vtd_svt_mask[VTD_SQ_MAX] = \
        {0xffff, 0xfffb, 0xfff9, 0xfff8};
    dma_addr_t addr = 0x00;
    uint16_t mask, source_id;
    uint8_t bus, bus_max, bus_min;

    if (index >= iommu->intr_size) {
        error_report_once("%s: index too large: ind=0x%x",
                          __func__, index);
        if (do_fault) {
            vtd_report_ir_fault(iommu, sid, VTD_FR_IR_INDEX_OVER, index);
        }
        return false;
    }

    addr = iommu->intr_root + index * sizeof(*entry);
    if (dma_memory_read(&address_space_memory, addr,
                        entry, sizeof(*entry), MEMTXATTRS_UNSPECIFIED)) {
        error_report_once("%s: read failed: ind=0x%x addr=0x%" PRIx64,
                          __func__, index, addr);
        if (do_fault) {
            vtd_report_ir_fault(iommu, sid, VTD_FR_IR_ROOT_INVAL, index);
        }
        return false;
    }

    entry->data[0] = le64_to_cpu(entry->data[0]);
    entry->data[1] = le64_to_cpu(entry->data[1]);

    trace_vtd_ir_irte_get(index, entry->data[1], entry->data[0]);

    /*
     * The remaining potential fault conditions are "qualified" by the
     * Fault Processing Disable bit in the IRTE. Even "not present".
     * So just clear the do_fault flag if PFD is set, which will
     * prevent faults being raised.
     */
    if (entry->irte.fault_disable) {
        do_fault = false;
    }

    if (!entry->irte.present) {
        error_report_once("%s: detected non-present IRTE "
                          "(index=%u, high=0x%" PRIx64 ", low=0x%" PRIx64 ")",
                          __func__, index, entry->data[1], entry->data[0]);
        if (do_fault) {
            vtd_report_ir_fault(iommu, sid, VTD_FR_IR_ENTRY_P, index);
        }
        return false;
    }

    if (entry->irte.__reserved_0 || entry->irte.__reserved_1 ||
        entry->irte.__reserved_2) {
        error_report_once("%s: detected non-zero reserved IRTE "
                          "(index=%u, high=0x%" PRIx64 ", low=0x%" PRIx64 ")",
                          __func__, index, entry->data[1], entry->data[0]);
        if (do_fault) {
            vtd_report_ir_fault(iommu, sid, VTD_FR_IR_IRTE_RSVD, index);
        }
        return false;
    }

    if (sid != X86_IOMMU_SID_INVALID) {
        /* Validate IRTE SID */
        source_id = entry->irte.source_id;
        switch (entry->irte.sid_vtype) {
        case VTD_SVT_NONE:
            break;

        case VTD_SVT_ALL:
            mask = vtd_svt_mask[entry->irte.sid_q];
            if ((source_id & mask) != (sid & mask)) {
                error_report_once("%s: invalid IRTE SID "
                                  "(index=%u, sid=%u, source_id=%u)",
                                  __func__, index, sid, source_id);
                if (do_fault) {
                    vtd_report_ir_fault(iommu, sid, VTD_FR_IR_SID_ERR, index);
                }
                return false;
            }
            break;

        case VTD_SVT_BUS:
            bus_max = source_id >> 8;
            bus_min = source_id & 0xff;
            bus = sid >> 8;
            if (bus > bus_max || bus < bus_min) {
                error_report_once("%s: invalid SVT_BUS "
                                  "(index=%u, bus=%u, min=%u, max=%u)",
                                  __func__, index, bus, bus_min, bus_max);
                if (do_fault) {
                    vtd_report_ir_fault(iommu, sid, VTD_FR_IR_SID_ERR, index);
                }
                return false;
            }
            break;

        default:
            error_report_once("%s: detected invalid IRTE SVT "
                              "(index=%u, type=%d)", __func__,
                              index, entry->irte.sid_vtype);
            /* Take this as verification failure. */
            if (do_fault) {
                vtd_report_ir_fault(iommu, sid, VTD_FR_IR_SID_ERR, index);
            }
            return false;
        }
    }

    return true;
}

/* Fetch IRQ information of specific IR index */
static bool vtd_remap_irq_get(IntelIOMMUState *iommu, uint16_t index,
                              X86IOMMUIrq *irq, uint16_t sid, bool do_fault)
{
    VTD_IR_TableEntry irte = {};

    if (!vtd_irte_get(iommu, index, &irte, sid, do_fault)) {
        return false;
    }

    irq->trigger_mode = irte.irte.trigger_mode;
    irq->vector = irte.irte.vector;
    irq->delivery_mode = irte.irte.delivery_mode;
    irq->dest = irte.irte.dest_id;
    if (!iommu->intr_eime) {
#define  VTD_IR_APIC_DEST_MASK         (0xff00ULL)
#define  VTD_IR_APIC_DEST_SHIFT        (8)
        irq->dest = (irq->dest & VTD_IR_APIC_DEST_MASK) >>
            VTD_IR_APIC_DEST_SHIFT;
    }
    irq->dest_mode = irte.irte.dest_mode;
    irq->redir_hint = irte.irte.redir_hint;

    trace_vtd_ir_remap(index, irq->trigger_mode, irq->vector,
                       irq->delivery_mode, irq->dest, irq->dest_mode);

    return true;
}

/* Interrupt remapping for MSI/MSI-X entry */
static int vtd_interrupt_remap_msi(IntelIOMMUState *iommu,
                                   MSIMessage *origin,
                                   MSIMessage *translated,
                                   uint16_t sid, bool do_fault)
{
    VTD_IR_MSIAddress addr;
    uint16_t index;
    X86IOMMUIrq irq = {};

    assert(origin && translated);

    trace_vtd_ir_remap_msi_req(origin->address, origin->data);

    if (!iommu || !iommu->intr_enabled) {
        memcpy(translated, origin, sizeof(*origin));
        goto out;
    }

    if (origin->address & VTD_MSI_ADDR_HI_MASK) {
        error_report_once("%s: MSI address high 32 bits non-zero detected: "
                          "address=0x%" PRIx64, __func__, origin->address);
        if (do_fault) {
            vtd_report_ir_fault(iommu, sid, VTD_FR_IR_REQ_RSVD, 0);
        }
        return -EINVAL;
    }

    addr.data = origin->address & VTD_MSI_ADDR_LO_MASK;
    if (addr.addr.__head != 0xfee) {
        error_report_once("%s: MSI address low 32 bit invalid: 0x%" PRIx32,
                          __func__, addr.data);
        if (do_fault) {
            vtd_report_ir_fault(iommu, sid, VTD_FR_IR_REQ_RSVD, 0);
        }
        return -EINVAL;
    }

    /* This is compatible mode. */
    if (addr.addr.int_mode != VTD_IR_INT_FORMAT_REMAP) {
        memcpy(translated, origin, sizeof(*origin));
        goto out;
    }

    index = addr.addr.index_h << 15 | addr.addr.index_l;

#define  VTD_IR_MSI_DATA_SUBHANDLE       (0x0000ffff)
#define  VTD_IR_MSI_DATA_RESERVED        (0xffff0000)

    if (addr.addr.sub_valid) {
        /* See VT-d spec 5.1.2.2 and 5.1.3 on subhandle */
        index += origin->data & VTD_IR_MSI_DATA_SUBHANDLE;
    }

    if (!vtd_remap_irq_get(iommu, index, &irq, sid, do_fault)) {
        return -EINVAL;
    }

    if (addr.addr.sub_valid) {
        trace_vtd_ir_remap_type("MSI");
        if (origin->data & VTD_IR_MSI_DATA_RESERVED) {
            error_report_once("%s: invalid IR MSI "
                              "(sid=%u, address=0x%" PRIx64
                              ", data=0x%" PRIx32 ")",
                              __func__, sid, origin->address, origin->data);
            if (do_fault) {
                vtd_report_ir_fault(iommu, sid, VTD_FR_IR_REQ_RSVD, 0);
            }
            return -EINVAL;
        }
    } else {
        uint8_t vector = origin->data & 0xff;
        uint8_t trigger_mode = (origin->data >> MSI_DATA_TRIGGER_SHIFT) & 0x1;

        trace_vtd_ir_remap_type("IOAPIC");
        /* IOAPIC entry vector should be aligned with IRTE vector
         * (see vt-d spec 5.1.5.1). */
        if (vector != irq.vector) {
            trace_vtd_warn_ir_vector(sid, index, vector, irq.vector);
        }

        /* The Trigger Mode field must match the Trigger Mode in the IRTE.
         * (see vt-d spec 5.1.5.1). */
        if (trigger_mode != irq.trigger_mode) {
            trace_vtd_warn_ir_trigger(sid, index, trigger_mode,
                                      irq.trigger_mode);
        }
    }

    /*
     * We'd better keep the last two bits, assuming that guest OS
     * might modify it. Keep it does not hurt after all.
     */
    irq.msi_addr_last_bits = addr.addr.__not_care;

    /* Translate X86IOMMUIrq to MSI message */
    x86_iommu_irq_to_msi_message(&irq, translated);

out:
    trace_vtd_ir_remap_msi(origin->address, origin->data,
                           translated->address, translated->data);
    return 0;
}

static int vtd_int_remap(X86IOMMUState *iommu, MSIMessage *src,
                         MSIMessage *dst, uint16_t sid)
{
    return vtd_interrupt_remap_msi(INTEL_IOMMU_DEVICE(iommu),
                                   src, dst, sid, false);
}

static MemTxResult vtd_mem_ir_read(void *opaque, hwaddr addr,
                                   uint64_t *data, unsigned size,
                                   MemTxAttrs attrs)
{
    return MEMTX_OK;
}

static MemTxResult vtd_mem_ir_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    int ret = 0;
    MSIMessage from = {}, to = {};
    uint16_t sid = X86_IOMMU_SID_INVALID;

    from.address = (uint64_t) addr + VTD_INTERRUPT_ADDR_FIRST;
    from.data = (uint32_t) value;

    if (!attrs.unspecified) {
        /* We have explicit Source ID */
        sid = attrs.requester_id;
    }

    ret = vtd_interrupt_remap_msi(opaque, &from, &to, sid, true);
    if (ret) {
        /* Drop this interrupt */
        return MEMTX_ERROR;
    }

    apic_get_class(NULL)->send_msi(&to);

    return MEMTX_OK;
}

static const MemoryRegionOps vtd_mem_ir_ops = {
    .read_with_attrs = vtd_mem_ir_read,
    .write_with_attrs = vtd_mem_ir_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void vtd_report_ir_illegal_access(VTDAddressSpace *vtd_as,
                                         hwaddr addr, bool is_write)
{
    IntelIOMMUState *s = vtd_as->iommu_state;
    uint8_t bus_n = pci_bus_num(vtd_as->bus);
    uint16_t sid = PCI_BUILD_BDF(bus_n, vtd_as->devfn);
    bool is_fpd_set = false;
    VTDContextEntry ce;

    assert(vtd_as->pasid != PCI_NO_PASID);

    /* Try out best to fetch FPD, we can't do anything more */
    if (vtd_dev_to_context_entry(s, bus_n, vtd_as->devfn, &ce) == 0) {
        is_fpd_set = ce.lo & VTD_CONTEXT_ENTRY_FPD;
        if (!is_fpd_set && s->root_scalable) {
            vtd_ce_get_pasid_fpd(s, &ce, &is_fpd_set, vtd_as->pasid);
        }
    }

    vtd_report_fault(s, VTD_FR_SM_INTERRUPT_ADDR,
                     is_fpd_set, sid, addr, is_write,
                     true, vtd_as->pasid);
}

static MemTxResult vtd_mem_ir_fault_read(void *opaque, hwaddr addr,
                                         uint64_t *data, unsigned size,
                                         MemTxAttrs attrs)
{
    vtd_report_ir_illegal_access(opaque, addr, false);

    return MEMTX_ERROR;
}

static MemTxResult vtd_mem_ir_fault_write(void *opaque, hwaddr addr,
                                          uint64_t value, unsigned size,
                                          MemTxAttrs attrs)
{
    vtd_report_ir_illegal_access(opaque, addr, true);

    return MEMTX_ERROR;
}

static const MemoryRegionOps vtd_mem_ir_fault_ops = {
    .read_with_attrs = vtd_mem_ir_fault_read,
    .write_with_attrs = vtd_mem_ir_fault_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

VTDAddressSpace *vtd_find_add_as(IntelIOMMUState *s, PCIBus *bus,
                                 int devfn, unsigned int pasid)
{
    /*
     * We can't simply use sid here since the bus number might not be
     * initialized by the guest.
     */
    struct vtd_as_key key = {
        .bus = bus,
        .devfn = devfn,
        .pasid = pasid,
    };
    VTDAddressSpace *vtd_dev_as;
    char name[128];

    vtd_dev_as = g_hash_table_lookup(s->vtd_address_spaces, &key);
    if (!vtd_dev_as) {
        struct vtd_as_key *new_key = g_malloc(sizeof(*new_key));

        new_key->bus = bus;
        new_key->devfn = devfn;
        new_key->pasid = pasid;

        if (pasid == PCI_NO_PASID) {
            snprintf(name, sizeof(name), "vtd-%02x.%x", PCI_SLOT(devfn),
                     PCI_FUNC(devfn));
        } else {
            snprintf(name, sizeof(name), "vtd-%02x.%x-pasid-%x", PCI_SLOT(devfn),
                     PCI_FUNC(devfn), pasid);
        }

        vtd_dev_as = g_new0(VTDAddressSpace, 1);

        vtd_dev_as->bus = bus;
        vtd_dev_as->devfn = (uint8_t)devfn;
        vtd_dev_as->pasid = pasid;
        vtd_dev_as->iommu_state = s;
        vtd_dev_as->context_cache_entry.context_cache_gen = 0;
        vtd_dev_as->iova_tree = iova_tree_new();

        memory_region_init(&vtd_dev_as->root, OBJECT(s), name, UINT64_MAX);
        address_space_init(&vtd_dev_as->as, &vtd_dev_as->root, "vtd-root");

        /*
         * Build the DMAR-disabled container with aliases to the
         * shared MRs.  Note that aliasing to a shared memory region
         * could help the memory API to detect same FlatViews so we
         * can have devices to share the same FlatView when DMAR is
         * disabled (either by not providing "intel_iommu=on" or with
         * "iommu=pt").  It will greatly reduce the total number of
         * FlatViews of the system hence VM runs faster.
         */
        memory_region_init_alias(&vtd_dev_as->nodmar, OBJECT(s),
                                 "vtd-nodmar", &s->mr_nodmar, 0,
                                 memory_region_size(&s->mr_nodmar));

        /*
         * Build the per-device DMAR-enabled container.
         *
         * TODO: currently we have per-device IOMMU memory region only
         * because we have per-device IOMMU notifiers for devices.  If
         * one day we can abstract the IOMMU notifiers out of the
         * memory regions then we can also share the same memory
         * region here just like what we've done above with the nodmar
         * region.
         */
        strcat(name, "-dmar");
        memory_region_init_iommu(&vtd_dev_as->iommu, sizeof(vtd_dev_as->iommu),
                                 TYPE_INTEL_IOMMU_MEMORY_REGION, OBJECT(s),
                                 name, UINT64_MAX);
        memory_region_init_alias(&vtd_dev_as->iommu_ir, OBJECT(s), "vtd-ir",
                                 &s->mr_ir, 0, memory_region_size(&s->mr_ir));
        memory_region_add_subregion_overlap(MEMORY_REGION(&vtd_dev_as->iommu),
                                            VTD_INTERRUPT_ADDR_FIRST,
                                            &vtd_dev_as->iommu_ir, 1);

        /*
         * This region is used for catching fault to access interrupt
         * range via passthrough + PASID. See also
         * vtd_switch_address_space(). We can't use alias since we
         * need to know the sid which is valid for MSI who uses
         * bus_master_as (see msi_send_message()).
         */
        memory_region_init_io(&vtd_dev_as->iommu_ir_fault, OBJECT(s),
                              &vtd_mem_ir_fault_ops, vtd_dev_as, "vtd-no-ir",
                              VTD_INTERRUPT_ADDR_SIZE);
        /*
         * Hook to root since when PT is enabled vtd_dev_as->iommu
         * will be disabled.
         */
        memory_region_add_subregion_overlap(MEMORY_REGION(&vtd_dev_as->root),
                                            VTD_INTERRUPT_ADDR_FIRST,
                                            &vtd_dev_as->iommu_ir_fault, 2);

        /*
         * Hook both the containers under the root container, we
         * switch between DMAR & noDMAR by enable/disable
         * corresponding sub-containers
         */
        memory_region_add_subregion_overlap(&vtd_dev_as->root, 0,
                                            MEMORY_REGION(&vtd_dev_as->iommu),
                                            0);
        memory_region_add_subregion_overlap(&vtd_dev_as->root, 0,
                                            &vtd_dev_as->nodmar, 0);

        vtd_switch_address_space(vtd_dev_as);

        g_hash_table_insert(s->vtd_address_spaces, new_key, vtd_dev_as);
    }
    return vtd_dev_as;
}

static bool vtd_check_hiod(IntelIOMMUState *s, HostIOMMUDevice *hiod,
                           Error **errp)
{
    HostIOMMUDeviceClass *hiodc = HOST_IOMMU_DEVICE_GET_CLASS(hiod);
    int ret;

    if (!hiodc->get_cap) {
        error_setg(errp, ".get_cap() not implemented");
        return false;
    }

    /* Common checks */
    ret = hiodc->get_cap(hiod, HOST_IOMMU_DEVICE_CAP_AW_BITS, errp);
    if (ret < 0) {
        return false;
    }
    if (s->aw_bits > ret) {
        error_setg(errp, "aw-bits %d > host aw-bits %d", s->aw_bits, ret);
        return false;
    }

    if (!s->flts) {
        /* All checks requested by VTD stage-2 translation pass */
        return true;
    }

    error_setg(errp, "host device is uncompatible with stage-1 translation");
    return false;
}

static bool vtd_dev_set_iommu_device(PCIBus *bus, void *opaque, int devfn,
                                     HostIOMMUDevice *hiod, Error **errp)
{
    IntelIOMMUState *s = opaque;
    struct vtd_as_key key = {
        .bus = bus,
        .devfn = devfn,
    };
    struct vtd_as_key *new_key;

    assert(hiod);

    vtd_iommu_lock(s);

    if (g_hash_table_lookup(s->vtd_host_iommu_dev, &key)) {
        error_setg(errp, "Host IOMMU device already exist");
        vtd_iommu_unlock(s);
        return false;
    }

    if (!vtd_check_hiod(s, hiod, errp)) {
        vtd_iommu_unlock(s);
        return false;
    }

    new_key = g_malloc(sizeof(*new_key));
    new_key->bus = bus;
    new_key->devfn = devfn;

    object_ref(hiod);
    g_hash_table_insert(s->vtd_host_iommu_dev, new_key, hiod);

    vtd_iommu_unlock(s);

    return true;
}

static void vtd_dev_unset_iommu_device(PCIBus *bus, void *opaque, int devfn)
{
    IntelIOMMUState *s = opaque;
    struct vtd_as_key key = {
        .bus = bus,
        .devfn = devfn,
    };

    vtd_iommu_lock(s);

    if (!g_hash_table_lookup(s->vtd_host_iommu_dev, &key)) {
        vtd_iommu_unlock(s);
        return;
    }

    g_hash_table_remove(s->vtd_host_iommu_dev, &key);

    vtd_iommu_unlock(s);
}

/* Unmap the whole range in the notifier's scope. */
static void vtd_address_space_unmap(VTDAddressSpace *as, IOMMUNotifier *n)
{
    hwaddr total, remain;
    hwaddr start = n->start;
    hwaddr end = n->end;
    IntelIOMMUState *s = as->iommu_state;
    DMAMap map;

    /*
     * Note: all the codes in this function has a assumption that IOVA
     * bits are no more than VTD_MGAW bits (which is restricted by
     * VT-d spec), otherwise we need to consider overflow of 64 bits.
     */

    if (end > VTD_ADDRESS_SIZE(s->aw_bits) - 1) {
        /*
         * Don't need to unmap regions that is bigger than the whole
         * VT-d supported address space size
         */
        end = VTD_ADDRESS_SIZE(s->aw_bits) - 1;
    }

    assert(start <= end);
    total = remain = end - start + 1;

    while (remain >= VTD_PAGE_SIZE) {
        IOMMUTLBEvent event;
        uint64_t mask = dma_aligned_pow2_mask(start, end, s->aw_bits);
        uint64_t size = mask + 1;

        assert(size);

        event.type = IOMMU_NOTIFIER_UNMAP;
        event.entry.iova = start;
        event.entry.addr_mask = mask;
        event.entry.target_as = &address_space_memory;
        event.entry.perm = IOMMU_NONE;
        /* This field is meaningless for unmap */
        event.entry.translated_addr = 0;

        memory_region_notify_iommu_one(n, &event);

        start += size;
        remain -= size;
    }

    assert(!remain);

    trace_vtd_as_unmap_whole(pci_bus_num(as->bus),
                             VTD_PCI_SLOT(as->devfn),
                             VTD_PCI_FUNC(as->devfn),
                             n->start, total);

    map.iova = n->start;
    map.size = total - 1; /* Inclusive */
    iova_tree_remove(as->iova_tree, map);
}

static void vtd_address_space_unmap_all(IntelIOMMUState *s)
{
    VTDAddressSpace *vtd_as;
    IOMMUNotifier *n;

    QLIST_FOREACH(vtd_as, &s->vtd_as_with_notifiers, next) {
        IOMMU_NOTIFIER_FOREACH(n, &vtd_as->iommu) {
            vtd_address_space_unmap(vtd_as, n);
        }
    }
}

static void vtd_address_space_refresh_all(IntelIOMMUState *s)
{
    vtd_address_space_unmap_all(s);
    vtd_switch_address_space_all(s);
}

static int vtd_replay_hook(const IOMMUTLBEvent *event, void *private)
{
    memory_region_notify_iommu_one(private, event);
    return 0;
}

static void vtd_iommu_replay(IOMMUMemoryRegion *iommu_mr, IOMMUNotifier *n)
{
    VTDAddressSpace *vtd_as = container_of(iommu_mr, VTDAddressSpace, iommu);
    IntelIOMMUState *s = vtd_as->iommu_state;
    uint8_t bus_n = pci_bus_num(vtd_as->bus);
    VTDContextEntry ce;
    DMAMap map = { .iova = 0, .size = HWADDR_MAX };

    /* replay is protected by BQL, page walk will re-setup it safely */
    iova_tree_remove(vtd_as->iova_tree, map);

    if (vtd_dev_to_context_entry(s, bus_n, vtd_as->devfn, &ce) == 0) {
        trace_vtd_replay_ce_valid(s->root_scalable ? "scalable mode" :
                                  "legacy mode",
                                  bus_n, PCI_SLOT(vtd_as->devfn),
                                  PCI_FUNC(vtd_as->devfn),
                                  vtd_get_domain_id(s, &ce, vtd_as->pasid),
                                  ce.hi, ce.lo);
        if (n->notifier_flags & IOMMU_NOTIFIER_MAP) {
            /* This is required only for MAP typed notifiers */
            vtd_page_walk_info info = {
                .hook_fn = vtd_replay_hook,
                .private = (void *)n,
                .notify_unmap = false,
                .aw = s->aw_bits,
                .as = vtd_as,
                .domain_id = vtd_get_domain_id(s, &ce, vtd_as->pasid),
            };

            vtd_page_walk(s, &ce, 0, ~0ULL, &info, vtd_as->pasid);
        }
    } else {
        trace_vtd_replay_ce_invalid(bus_n, PCI_SLOT(vtd_as->devfn),
                                    PCI_FUNC(vtd_as->devfn));
    }
}

static void vtd_cap_init(IntelIOMMUState *s)
{
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(s);

    s->cap = VTD_CAP_FRO | VTD_CAP_NFR | VTD_CAP_ND |
             VTD_CAP_MAMV | VTD_CAP_PSI | VTD_CAP_SLLPS |
             VTD_CAP_MGAW(s->aw_bits);
    if (s->dma_drain) {
        s->cap |= VTD_CAP_DRAIN;
    }
    if (s->dma_translation) {
            if (s->aw_bits >= VTD_HOST_AW_39BIT) {
                    s->cap |= VTD_CAP_SAGAW_39bit;
            }
            if (s->aw_bits >= VTD_HOST_AW_48BIT) {
                    s->cap |= VTD_CAP_SAGAW_48bit;
            }
    }
    s->ecap = VTD_ECAP_QI | VTD_ECAP_IRO;

    if (x86_iommu_ir_supported(x86_iommu)) {
        s->ecap |= VTD_ECAP_IR | VTD_ECAP_MHMV;
        if (s->intr_eim == ON_OFF_AUTO_ON) {
            s->ecap |= VTD_ECAP_EIM;
        }
        assert(s->intr_eim != ON_OFF_AUTO_AUTO);
    }

    if (x86_iommu->dt_supported) {
        s->ecap |= VTD_ECAP_DT;
    }

    if (x86_iommu->pt_supported) {
        s->ecap |= VTD_ECAP_PT;
    }

    if (s->caching_mode) {
        s->cap |= VTD_CAP_CM;
    }

    /* TODO: read cap/ecap from host to decide which cap to be exposed. */
    if (s->flts) {
        s->ecap |= VTD_ECAP_SMTS | VTD_ECAP_FLTS;
        if (s->fs1gp) {
            s->cap |= VTD_CAP_FS1GP;
        }
    } else if (s->scalable_mode) {
        s->ecap |= VTD_ECAP_SMTS | VTD_ECAP_SRS | VTD_ECAP_SLTS;
    }

    if (s->snoop_control) {
        s->ecap |= VTD_ECAP_SC;
    }

    if (s->pasid) {
        s->ecap |= VTD_ECAP_PASID;
    }
}

/*
 * Do the initialization. It will also be called when reset, so pay
 * attention when adding new initialization stuff.
 */
static void vtd_init(IntelIOMMUState *s)
{
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(s);

    memset(s->csr, 0, DMAR_REG_SIZE);
    memset(s->wmask, 0, DMAR_REG_SIZE);
    memset(s->w1cmask, 0, DMAR_REG_SIZE);
    memset(s->womask, 0, DMAR_REG_SIZE);

    s->root = 0;
    s->root_scalable = false;
    s->dmar_enabled = false;
    s->intr_enabled = false;
    s->iq_head = 0;
    s->iq_tail = 0;
    s->iq = 0;
    s->iq_size = 0;
    s->qi_enabled = false;
    s->iq_last_desc_type = VTD_INV_DESC_NONE;
    s->iq_dw = false;
    s->next_frcd_reg = 0;

    vtd_cap_init(s);

    /*
     * Rsvd field masks for spte
     */
    vtd_spte_rsvd[0] = ~0ULL;
    vtd_spte_rsvd[1] = VTD_SPTE_PAGE_L1_RSVD_MASK(s->aw_bits,
                                        x86_iommu->dt_supported && s->stale_tm);
    vtd_spte_rsvd[2] = VTD_SPTE_PAGE_L2_RSVD_MASK(s->aw_bits);
    vtd_spte_rsvd[3] = VTD_SPTE_PAGE_L3_RSVD_MASK(s->aw_bits);
    vtd_spte_rsvd[4] = VTD_SPTE_PAGE_L4_RSVD_MASK(s->aw_bits);

    vtd_spte_rsvd_large[2] = VTD_SPTE_LPAGE_L2_RSVD_MASK(s->aw_bits,
                                        x86_iommu->dt_supported && s->stale_tm);
    vtd_spte_rsvd_large[3] = VTD_SPTE_LPAGE_L3_RSVD_MASK(s->aw_bits,
                                        x86_iommu->dt_supported && s->stale_tm);

    /*
     * Rsvd field masks for fpte
     */
    vtd_fpte_rsvd[0] = ~0ULL;
    vtd_fpte_rsvd[1] = VTD_FPTE_PAGE_L1_RSVD_MASK(s->aw_bits);
    vtd_fpte_rsvd[2] = VTD_FPTE_PAGE_L2_RSVD_MASK(s->aw_bits);
    vtd_fpte_rsvd[3] = VTD_FPTE_PAGE_L3_RSVD_MASK(s->aw_bits);
    vtd_fpte_rsvd[4] = VTD_FPTE_PAGE_L4_RSVD_MASK(s->aw_bits);

    vtd_fpte_rsvd_large[2] = VTD_FPTE_LPAGE_L2_RSVD_MASK(s->aw_bits);
    vtd_fpte_rsvd_large[3] = VTD_FPTE_LPAGE_L3_RSVD_MASK(s->aw_bits);

    if (s->scalable_mode || s->snoop_control) {
        vtd_spte_rsvd[1] &= ~VTD_SPTE_SNP;
        vtd_spte_rsvd_large[2] &= ~VTD_SPTE_SNP;
        vtd_spte_rsvd_large[3] &= ~VTD_SPTE_SNP;
    }

    vtd_reset_caches(s);

    /* Define registers with default values and bit semantics */
    vtd_define_long(s, DMAR_VER_REG, 0x10UL, 0, 0);
    vtd_define_quad(s, DMAR_CAP_REG, s->cap, 0, 0);
    vtd_define_quad(s, DMAR_ECAP_REG, s->ecap, 0, 0);
    vtd_define_long(s, DMAR_GCMD_REG, 0, 0xff800000UL, 0);
    vtd_define_long_wo(s, DMAR_GCMD_REG, 0xff800000UL);
    vtd_define_long(s, DMAR_GSTS_REG, 0, 0, 0);
    vtd_define_quad(s, DMAR_RTADDR_REG, 0, 0xfffffffffffffc00ULL, 0);
    vtd_define_quad(s, DMAR_CCMD_REG, 0, 0xe0000003ffffffffULL, 0);
    vtd_define_quad_wo(s, DMAR_CCMD_REG, 0x3ffff0000ULL);

    /* Advanced Fault Logging not supported */
    vtd_define_long(s, DMAR_FSTS_REG, 0, 0, 0x11UL);
    vtd_define_long(s, DMAR_FECTL_REG, 0x80000000UL, 0x80000000UL, 0);
    vtd_define_long(s, DMAR_FEDATA_REG, 0, 0x0000ffffUL, 0);
    vtd_define_long(s, DMAR_FEADDR_REG, 0, 0xfffffffcUL, 0);

    /* Treated as RsvdZ when EIM in ECAP_REG is not supported
     * vtd_define_long(s, DMAR_FEUADDR_REG, 0, 0xffffffffUL, 0);
     */
    vtd_define_long(s, DMAR_FEUADDR_REG, 0, 0, 0);

    /* Treated as RO for implementations that PLMR and PHMR fields reported
     * as Clear in the CAP_REG.
     * vtd_define_long(s, DMAR_PMEN_REG, 0, 0x80000000UL, 0);
     */
    vtd_define_long(s, DMAR_PMEN_REG, 0, 0, 0);

    vtd_define_quad(s, DMAR_IQH_REG, 0, 0, 0);
    vtd_define_quad(s, DMAR_IQT_REG, 0, 0x7fff0ULL, 0);
    vtd_define_quad(s, DMAR_IQA_REG, 0, 0xfffffffffffff807ULL, 0);
    vtd_define_long(s, DMAR_ICS_REG, 0, 0, 0x1UL);
    vtd_define_long(s, DMAR_IECTL_REG, 0x80000000UL, 0x80000000UL, 0);
    vtd_define_long(s, DMAR_IEDATA_REG, 0, 0xffffffffUL, 0);
    vtd_define_long(s, DMAR_IEADDR_REG, 0, 0xfffffffcUL, 0);
    /* Treadted as RsvdZ when EIM in ECAP_REG is not supported */
    vtd_define_long(s, DMAR_IEUADDR_REG, 0, 0, 0);

    /* IOTLB registers */
    vtd_define_quad(s, DMAR_IOTLB_REG, 0, 0Xb003ffff00000000ULL, 0);
    vtd_define_quad(s, DMAR_IVA_REG, 0, 0xfffffffffffff07fULL, 0);
    vtd_define_quad_wo(s, DMAR_IVA_REG, 0xfffffffffffff07fULL);

    /* Fault Recording Registers, 128-bit */
    vtd_define_quad(s, DMAR_FRCD_REG_0_0, 0, 0, 0);
    vtd_define_quad(s, DMAR_FRCD_REG_0_2, 0, 0, 0x8000000000000000ULL);

    /*
     * Interrupt remapping registers.
     */
    vtd_define_quad(s, DMAR_IRTA_REG, 0, 0xfffffffffffff80fULL, 0);
}

/* Should not reset address_spaces when reset because devices will still use
 * the address space they got at first (won't ask the bus again).
 */
static void vtd_reset_exit(Object *obj, ResetType type)
{
    IntelIOMMUState *s = INTEL_IOMMU_DEVICE(obj);

    trace_vtd_reset_exit();
    vtd_init(s);
    vtd_address_space_refresh_all(s);
}

static AddressSpace *vtd_host_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    IntelIOMMUState *s = opaque;
    VTDAddressSpace *vtd_as;

    assert(0 <= devfn && devfn < PCI_DEVFN_MAX);

    vtd_as = vtd_find_add_as(s, bus, devfn, PCI_NO_PASID);
    return &vtd_as->as;
}

static PCIIOMMUOps vtd_iommu_ops = {
    .get_address_space = vtd_host_dma_iommu,
    .set_iommu_device = vtd_dev_set_iommu_device,
    .unset_iommu_device = vtd_dev_unset_iommu_device,
};

static bool vtd_decide_config(IntelIOMMUState *s, Error **errp)
{
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(s);

    if (s->intr_eim == ON_OFF_AUTO_ON && !x86_iommu_ir_supported(x86_iommu)) {
        error_setg(errp, "eim=on cannot be selected without intremap=on");
        return false;
    }

    if (s->intr_eim == ON_OFF_AUTO_AUTO) {
        s->intr_eim = (kvm_irqchip_in_kernel() || s->buggy_eim)
                      && x86_iommu_ir_supported(x86_iommu) ?
                                              ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
    }
    if (s->intr_eim == ON_OFF_AUTO_ON && !s->buggy_eim) {
        if (kvm_irqchip_is_split() && !kvm_enable_x2apic()) {
            error_setg(errp, "eim=on requires support on the KVM side"
                             "(X2APIC_API, first shipped in v4.7)");
            return false;
        }
    }

    if (!s->scalable_mode && s->flts) {
        error_setg(errp, "x-flts is only available in scalable mode");
        return false;
    }

    if (!s->flts && s->aw_bits != VTD_HOST_AW_39BIT &&
        s->aw_bits != VTD_HOST_AW_48BIT) {
        error_setg(errp, "%s: supported values for aw-bits are: %d, %d",
                   s->scalable_mode ? "Scalable mode(flts=off)" : "Legacy mode",
                   VTD_HOST_AW_39BIT, VTD_HOST_AW_48BIT);
        return false;
    }

    if (s->flts && s->aw_bits != VTD_HOST_AW_48BIT) {
        error_setg(errp,
                   "Scalable mode(flts=on): supported value for aw-bits is: %d",
                   VTD_HOST_AW_48BIT);
        return false;
    }

    if (s->scalable_mode && !s->dma_drain) {
        error_setg(errp, "Need to set dma_drain for scalable mode");
        return false;
    }

    if (s->pasid && !s->scalable_mode) {
        error_setg(errp, "Need to set scalable mode for PASID");
        return false;
    }

    return true;
}

static int vtd_machine_done_notify_one(Object *child, void *unused)
{
    IntelIOMMUState *iommu = INTEL_IOMMU_DEVICE(x86_iommu_get_default());

    /*
     * We hard-coded here because vfio-pci is the only special case
     * here.  Let's be more elegant in the future when we can, but so
     * far there seems to be no better way.
     */
    if (object_dynamic_cast(child, "vfio-pci") && !iommu->caching_mode) {
        vtd_panic_require_caching_mode();
    }

    return 0;
}

static void vtd_machine_done_hook(Notifier *notifier, void *unused)
{
    object_child_foreach_recursive(object_get_root(),
                                   vtd_machine_done_notify_one, NULL);
}

static Notifier vtd_machine_done_notify = {
    .notify = vtd_machine_done_hook,
};

static void vtd_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    PCMachineState *pcms = PC_MACHINE(ms);
    X86MachineState *x86ms = X86_MACHINE(ms);
    PCIBus *bus = pcms->pcibus;
    IntelIOMMUState *s = INTEL_IOMMU_DEVICE(dev);
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(s);

    if (s->pasid && x86_iommu->dt_supported) {
        /*
         * PASID-based-Device-TLB Invalidate Descriptor is not
         * implemented and it requires support from vhost layer which
         * needs to be implemented in the future.
         */
        error_setg(errp, "PASID based device IOTLB is not supported");
        return;
    }

    if (!vtd_decide_config(s, errp)) {
        return;
    }

    QLIST_INIT(&s->vtd_as_with_notifiers);
    qemu_mutex_init(&s->iommu_lock);
    memory_region_init_io(&s->csrmem, OBJECT(s), &vtd_mem_ops, s,
                          "intel_iommu", DMAR_REG_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                Q35_HOST_BRIDGE_IOMMU_ADDR, &s->csrmem);

    /* Create the shared memory regions by all devices */
    memory_region_init(&s->mr_nodmar, OBJECT(s), "vtd-nodmar",
                       UINT64_MAX);
    memory_region_init_io(&s->mr_ir, OBJECT(s), &vtd_mem_ir_ops,
                          s, "vtd-ir", VTD_INTERRUPT_ADDR_SIZE);
    memory_region_init_alias(&s->mr_sys_alias, OBJECT(s),
                             "vtd-sys-alias", get_system_memory(), 0,
                             memory_region_size(get_system_memory()));
    memory_region_add_subregion_overlap(&s->mr_nodmar, 0,
                                        &s->mr_sys_alias, 0);
    memory_region_add_subregion_overlap(&s->mr_nodmar,
                                        VTD_INTERRUPT_ADDR_FIRST,
                                        &s->mr_ir, 1);
    /* No corresponding destroy */
    s->iotlb = g_hash_table_new_full(vtd_iotlb_hash, vtd_iotlb_equal,
                                     g_free, g_free);
    s->vtd_address_spaces = g_hash_table_new_full(vtd_as_hash, vtd_as_equal,
                                      g_free, g_free);
    s->vtd_host_iommu_dev = g_hash_table_new_full(vtd_hiod_hash, vtd_hiod_equal,
                                                  g_free, vtd_hiod_destroy);
    vtd_init(s);
    pci_setup_iommu(bus, &vtd_iommu_ops, dev);
    /* Pseudo address space under root PCI bus. */
    x86ms->ioapic_as = vtd_host_dma_iommu(bus, s, Q35_PSEUDO_DEVFN_IOAPIC);
    qemu_add_machine_init_done_notifier(&vtd_machine_done_notify);
}

static void vtd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    X86IOMMUClass *x86_class = X86_IOMMU_DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    /*
     * Use 'exit' reset phase to make sure all DMA requests
     * have been quiesced during 'enter' or 'hold' phase
     */
    rc->phases.exit = vtd_reset_exit;
    dc->vmsd = &vtd_vmstate;
    device_class_set_props(dc, vtd_properties);
    dc->hotpluggable = false;
    x86_class->realize = vtd_realize;
    x86_class->int_remap = vtd_int_remap;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Intel IOMMU (VT-d) DMA Remapping device";
}

static const TypeInfo vtd_info = {
    .name          = TYPE_INTEL_IOMMU_DEVICE,
    .parent        = TYPE_X86_IOMMU_DEVICE,
    .instance_size = sizeof(IntelIOMMUState),
    .class_init    = vtd_class_init,
};

static void vtd_iommu_memory_region_class_init(ObjectClass *klass,
                                                     void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = vtd_iommu_translate;
    imrc->notify_flag_changed = vtd_iommu_notify_flag_changed;
    imrc->replay = vtd_iommu_replay;
}

static const TypeInfo vtd_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_INTEL_IOMMU_MEMORY_REGION,
    .class_init = vtd_iommu_memory_region_class_init,
};

static void vtd_register_types(void)
{
    type_register_static(&vtd_info);
    type_register_static(&vtd_iommu_memory_region_info);
}

type_init(vtd_register_types)
