/*
 * Hexagon TLB QOM Device
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/resettable.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "target/hexagon/cpu.h"
#include "target/hexagon/cpu_bits.h"

/* PTE (TLB entry) field extraction */
#define GET_PTE_PPD(entry)   extract64((entry),  0, 24)
#define GET_PTE_C(entry)     extract64((entry), 24,  4)
#define GET_PTE_U(entry)     extract64((entry), 28,  1)
#define GET_PTE_R(entry)     extract64((entry), 29,  1)
#define GET_PTE_W(entry)     extract64((entry), 30,  1)
#define GET_PTE_X(entry)     extract64((entry), 31,  1)
#define GET_PTE_VPN(entry)   extract64((entry), 32, 20)
#define GET_PTE_ASID(entry)  extract64((entry), 52,  7)
#define GET_PTE_ATR0(entry)  extract64((entry), 59,  1)
#define GET_PTE_ATR1(entry)  extract64((entry), 60,  1)
#define GET_PTE_PA35(entry)  extract64((entry), 61,  1)
#define GET_PTE_G(entry)     extract64((entry), 62,  1)
#define GET_PTE_V(entry)     extract64((entry), 63,  1)

/* PPD (physical page descriptor) */
static inline uint64_t GET_PPD(uint64_t entry)
{
    return GET_PTE_PPD(entry) | (GET_PTE_PA35(entry) << 24);
}

#define NO_ASID      (1 << 8)

typedef enum {
    PGSIZE_4K,
    PGSIZE_16K,
    PGSIZE_64K,
    PGSIZE_256K,
    PGSIZE_1M,
    PGSIZE_4M,
    PGSIZE_16M,
    PGSIZE_64M,
    PGSIZE_256M,
    PGSIZE_1G,
} tlb_pgsize_t;

#define NUM_PGSIZE_TYPES (PGSIZE_1G + 1)

static const char *pgsize_str[NUM_PGSIZE_TYPES] = {
    "4K",
    "16K",
    "64K",
    "256K",
    "1M",
    "4M",
    "16M",
    "64M",
    "256M",
    "1G",
};

#define INVALID_MASK 0xffffffffLL

static const uint64_t encmask_2_mask[] = {
    0x0fffLL,                           /* 4k,   0000 */
    0x3fffLL,                           /* 16k,  0001 */
    0xffffLL,                           /* 64k,  0010 */
    0x3ffffLL,                          /* 256k, 0011 */
    0xfffffLL,                          /* 1m,   0100 */
    0x3fffffLL,                         /* 4m,   0101 */
    0xffffffLL,                         /* 16m,  0110 */
    0x3ffffffLL,                        /* 64m,  0111 */
    0xfffffffLL,                        /* 256m, 1000 */
    0x3fffffffLL,                       /* 1g,   1001 */
    INVALID_MASK,                       /* RSVD, 1010 */
};

static inline tlb_pgsize_t hex_tlb_pgsize_type(uint64_t entry)
{
    if (entry == 0) {
        qemu_log_mask(CPU_LOG_MMU, "%s: Supplied TLB entry was 0!\n",
                      __func__);
        return 0;
    }
    tlb_pgsize_t size = ctz64(entry);
    g_assert(size < NUM_PGSIZE_TYPES);
    return size;
}

static inline uint64_t hex_tlb_page_size_bytes(uint64_t entry)
{
    return 1ull << (qemu_target_page_bits() + 2 * hex_tlb_pgsize_type(entry));
}

static inline uint64_t hex_tlb_phys_page_num(uint64_t entry)
{
    uint32_t ppd = GET_PPD(entry);
    return ppd >> 1;
}

static inline uint64_t hex_tlb_phys_addr(uint64_t entry)
{
    uint64_t pagemask = encmask_2_mask[hex_tlb_pgsize_type(entry)];
    uint64_t pagenum = hex_tlb_phys_page_num(entry);
    uint64_t PA = (pagenum << qemu_target_page_bits()) & (~pagemask);
    return PA;
}

static inline uint64_t hex_tlb_virt_addr(uint64_t entry)
{
    return (uint64_t)GET_PTE_VPN(entry) << qemu_target_page_bits();
}

bool hexagon_tlb_dump_entry(Monitor *mon, uint64_t entry)
{
    if (GET_PTE_V(entry)) {
        uint64_t PA = hex_tlb_phys_addr(entry);
        uint64_t VA = hex_tlb_virt_addr(entry);
        monitor_printf(mon, "0x%016" PRIx64 ": ", entry);
        monitor_printf(mon, "V:%" PRId64 " G:%" PRId64
                       " A1:%" PRId64 " A0:%" PRId64,
                       GET_PTE_V(entry),
                       GET_PTE_G(entry),
                       GET_PTE_ATR1(entry),
                       GET_PTE_ATR0(entry));
        monitor_printf(mon, " ASID:0x%02" PRIx64 " VA:0x%08" PRIx64,
                       GET_PTE_ASID(entry), VA);
        monitor_printf(mon,
                       " X:%" PRId64 " W:%" PRId64 " R:%" PRId64
                       " U:%" PRId64 " C:%" PRId64,
                       GET_PTE_X(entry),
                       GET_PTE_W(entry),
                       GET_PTE_R(entry),
                       GET_PTE_U(entry),
                       GET_PTE_C(entry));
        monitor_printf(mon, " PA:0x%09" PRIx64 " SZ:%s (0x%" PRIx64 ")",
                       PA, pgsize_str[hex_tlb_pgsize_type(entry)],
                       hex_tlb_page_size_bytes(entry));
        monitor_printf(mon, "\n");
        return true;
    }

    /* Not valid */
    return false;
}

static inline bool hex_tlb_entry_match_noperm(uint64_t entry, uint32_t asid,
                                              uint64_t VA)
{
    if (GET_PTE_V(entry)) {
        if (GET_PTE_G(entry)) {
            /* Global entry - ignore ASID */
        } else if (asid != NO_ASID) {
            uint32_t tlb_asid = GET_PTE_ASID(entry);
            if (tlb_asid != asid) {
                return false;
            }
        }

        uint64_t page_size = hex_tlb_page_size_bytes(entry);
        uint64_t page_start =
            ROUND_DOWN(hex_tlb_virt_addr(entry), page_size);
        if (page_start <= VA && VA < page_start + page_size) {
            return true;
        }
    }
    return false;
}

static inline void hex_tlb_entry_get_perm(uint64_t entry,
                                          MMUAccessType access_type,
                                          int mmu_idx, int *prot,
                                          int32_t *excp, int *cause_code)
{
    bool perm_x = GET_PTE_X(entry);
    bool perm_w = GET_PTE_W(entry);
    bool perm_r = GET_PTE_R(entry);
    bool perm_u = GET_PTE_U(entry);
    bool user_idx = mmu_idx == MMU_USER_IDX;

    if (mmu_idx == MMU_KERNEL_IDX) {
        *prot = PAGE_VALID | PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return;
    }

    *prot = PAGE_VALID;
    switch (access_type) {
    case MMU_INST_FETCH:
        if (user_idx && !perm_u) {
            *excp = HEX_EVENT_PRECISE;
            *cause_code = HEX_CAUSE_FETCH_NO_UPAGE;
        } else if (!perm_x) {
            *excp = HEX_EVENT_PRECISE;
            *cause_code = HEX_CAUSE_FETCH_NO_XPAGE;
        }
        break;
    case MMU_DATA_LOAD:
        if (user_idx && !perm_u) {
            *excp = HEX_EVENT_PRECISE;
            *cause_code = HEX_CAUSE_PRIV_NO_UREAD;
        } else if (!perm_r) {
            *excp = HEX_EVENT_PRECISE;
            *cause_code = HEX_CAUSE_PRIV_NO_READ;
        }
        break;
    case MMU_DATA_STORE:
        if (user_idx && !perm_u) {
            *excp = HEX_EVENT_PRECISE;
            *cause_code = HEX_CAUSE_PRIV_NO_UWRITE;
        } else if (!perm_w) {
            *excp = HEX_EVENT_PRECISE;
            *cause_code = HEX_CAUSE_PRIV_NO_WRITE;
        }
        break;
    }

    if (!user_idx || perm_u) {
        if (perm_x) {
            *prot |= PAGE_EXEC;
        }
        if (perm_r) {
            *prot |= PAGE_READ;
        }
        if (perm_w) {
            *prot |= PAGE_WRITE;
        }
    }
}

static inline bool hex_tlb_entry_match(uint64_t entry, uint8_t asid,
                                       uint32_t VA,
                                       MMUAccessType access_type, hwaddr *PA,
                                       int *prot, uint64_t *size,
                                       int32_t *excp, int *cause_code,
                                       int mmu_idx)
{
    if (hex_tlb_entry_match_noperm(entry, asid, VA)) {
        hex_tlb_entry_get_perm(entry, access_type, mmu_idx, prot, excp,
                               cause_code);
        *PA = hex_tlb_phys_addr(entry);
        *size = hex_tlb_page_size_bytes(entry);
        return true;
    }
    return false;
}

static bool hex_tlb_is_match(uint64_t entry1, uint64_t entry2,
                             bool consider_gbit)
{
    bool valid1 = GET_PTE_V(entry1);
    bool valid2 = GET_PTE_V(entry2);
    uint64_t size1 = hex_tlb_page_size_bytes(entry1);
    uint64_t vaddr1 = ROUND_DOWN(hex_tlb_virt_addr(entry1), size1);
    uint64_t size2 = hex_tlb_page_size_bytes(entry2);
    uint64_t vaddr2 = ROUND_DOWN(hex_tlb_virt_addr(entry2), size2);
    int asid1 = GET_PTE_ASID(entry1);
    int asid2 = GET_PTE_ASID(entry2);
    bool gbit1 = GET_PTE_G(entry1);
    bool gbit2 = GET_PTE_G(entry2);

    if (!valid1 || !valid2) {
        return false;
    }

    if (((vaddr1 <= vaddr2) && (vaddr2 < (vaddr1 + size1))) ||
        ((vaddr2 <= vaddr1) && (vaddr1 < (vaddr2 + size2)))) {
        if (asid1 == asid2) {
            return true;
        }
        if ((consider_gbit && gbit1) || gbit2) {
            return true;
        }
    }
    return false;
}

/* Public API */

uint64_t hexagon_tlb_read(HexagonTLBState *tlb, uint32_t index)
{
    g_assert(index < tlb->num_entries);
    return tlb->entries[index];
}

void hexagon_tlb_write(HexagonTLBState *tlb, uint32_t index, uint64_t value)
{
    g_assert(index < tlb->num_entries);
    tlb->entries[index] = value;
}

bool hexagon_tlb_find_match(HexagonTLBState *tlb, uint32_t asid,
                            uint32_t VA, MMUAccessType access_type,
                            hwaddr *PA, int *prot, uint64_t *size,
                            int32_t *excp, int *cause_code, int mmu_idx)
{
    *PA = 0;
    *prot = 0;
    *size = 0;
    *excp = 0;
    *cause_code = 0;

    for (uint32_t i = 0; i < tlb->num_entries; i++) {
        if (hex_tlb_entry_match(tlb->entries[i], asid, VA, access_type,
                                PA, prot, size, excp, cause_code, mmu_idx)) {
            return true;
        }
    }
    return false;
}

uint32_t hexagon_tlb_lookup(HexagonTLBState *tlb, uint32_t asid,
                            uint32_t VA, int *cause_code)
{
    uint32_t not_found = 0x80000000;
    uint32_t idx = not_found;

    for (uint32_t i = 0; i < tlb->num_entries; i++) {
        uint64_t entry = tlb->entries[i];
        if (hex_tlb_entry_match_noperm(entry, asid, VA)) {
            if (idx != not_found) {
                *cause_code = HEX_CAUSE_IMPRECISE_MULTI_TLB_MATCH;
                break;
            }
            idx = i;
        }
    }

    if (idx == not_found) {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s: 0x%" PRIx32 ", 0x%08" PRIx32 " => NOT FOUND\n",
                      __func__, asid, VA);
    } else {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s: 0x%" PRIx32 ", 0x%08" PRIx32 " => %d\n",
                      __func__, asid, VA, idx);
    }

    return idx;
}

/*
 * Return codes:
 * 0 or positive             index of match
 * -1                        multiple matches
 * -2                        no match
 */
int hexagon_tlb_check_overlap(HexagonTLBState *tlb, uint64_t entry,
                              uint64_t index)
{
    int matches = 0;
    int last_match = 0;

    for (uint32_t i = 0; i < tlb->num_entries; i++) {
        if (hex_tlb_is_match(entry, tlb->entries[i], false)) {
            matches++;
            last_match = i;
        }
    }

    if (matches == 1) {
        return last_match;
    }
    if (matches == 0) {
        return -2;
    }
    return -1;
}

void hexagon_tlb_dump(Monitor *mon, HexagonTLBState *tlb)
{
    for (uint32_t i = 0; i < tlb->num_entries; i++) {
        hexagon_tlb_dump_entry(mon, tlb->entries[i]);
    }
}

uint32_t hexagon_tlb_get_num_entries(HexagonTLBState *tlb)
{
    return tlb->num_entries;
}

/* QOM lifecycle */

static void hexagon_tlb_init(Object *obj)
{
}

static void hexagon_tlb_realize(DeviceState *dev, Error **errp)
{
    HexagonTLBState *s = HEXAGON_TLB(dev);

    if (s->num_entries == 0 || s->num_entries > MAX_TLB_ENTRIES) {
        error_setg(errp, "Invalid TLB num-entries: %" PRIu32,
                   s->num_entries);
        return;
    }
    s->entries = g_new0(uint64_t, s->num_entries);
}

static void hexagon_tlb_unrealize(DeviceState *dev)
{
    HexagonTLBState *s = HEXAGON_TLB(dev);
    g_free(s->entries);
    s->entries = NULL;
}

static void hexagon_tlb_reset_hold(Object *obj, ResetType type)
{
    HexagonTLBState *s = HEXAGON_TLB(obj);
    if (s->entries) {
        memset(s->entries, 0, sizeof(uint64_t) * s->num_entries);
    }
}

static const VMStateDescription vmstate_hexagon_tlb = {
    .name = "hexagon-tlb",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(num_entries, HexagonTLBState),
        VMSTATE_VARRAY_UINT32_ALLOC(entries, HexagonTLBState, num_entries,
                                    0, vmstate_info_uint64, uint64_t),
        VMSTATE_END_OF_LIST()
    },
};

static const Property hexagon_tlb_properties[] = {
    DEFINE_PROP_UINT32("num-entries", HexagonTLBState, num_entries,
                       MAX_TLB_ENTRIES),
};

static void hexagon_tlb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = hexagon_tlb_realize;
    dc->unrealize = hexagon_tlb_unrealize;
    rc->phases.hold = hexagon_tlb_reset_hold;
    dc->vmsd = &vmstate_hexagon_tlb;
    dc->user_creatable = false;
    device_class_set_props(dc, hexagon_tlb_properties);
}

static const TypeInfo hexagon_tlb_info = {
    .name = TYPE_HEXAGON_TLB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HexagonTLBState),
    .instance_init = hexagon_tlb_init,
    .class_init = hexagon_tlb_class_init,
};

static void hexagon_tlb_register_types(void)
{
    type_register_static(&hexagon_tlb_info);
}

type_init(hexagon_tlb_register_types)
