/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch TLB helpers
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 */

#include "qemu/osdep.h"
#include "qemu/guest-random.h"

#include "cpu.h"
#include "cpu-mmu.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/log.h"
#include "cpu-csr.h"
#include "tcg/tcg_loongarch.h"

typedef bool (*tlb_match)(bool global, int asid, int tlb_asid);

static bool tlb_match_any(bool global, int asid, int tlb_asid)
{
    return global || tlb_asid == asid;
}

static bool tlb_match_asid(bool global, int asid, int tlb_asid)
{
    return !global && tlb_asid == asid;
}

bool check_ps(CPULoongArchState *env, uint8_t tlb_ps)
{
    if (tlb_ps >= 64) {
        return false;
    }
    return BIT_ULL(tlb_ps) & (env->CSR_PRCFG2);
}

static void raise_mmu_exception(CPULoongArchState *env, vaddr address,
                                MMUAccessType access_type, TLBRet tlb_error)
{
    CPUState *cs = env_cpu(env);

    switch (tlb_error) {
    default:
    case TLBRET_BADADDR:
        cs->exception_index = access_type == MMU_INST_FETCH
                              ? EXCCODE_ADEF : EXCCODE_ADEM;
        break;
    case TLBRET_NOMATCH:
        /* No TLB match for a mapped address */
        if (access_type == MMU_DATA_LOAD) {
            cs->exception_index = EXCCODE_PIL;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PIS;
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PIF;
        }
        env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR, 1);
        break;
    case TLBRET_INVALID:
        /* TLB match with no valid bit */
        if (access_type == MMU_DATA_LOAD) {
            cs->exception_index = EXCCODE_PIL;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PIS;
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PIF;
        }
        break;
    case TLBRET_DIRTY:
        /* TLB match but 'D' bit is cleared */
        cs->exception_index = EXCCODE_PME;
        break;
    case TLBRET_XI:
        /* Execute-Inhibit Exception */
        cs->exception_index = EXCCODE_PNX;
        break;
    case TLBRET_RI:
        /* Read-Inhibit Exception */
        cs->exception_index = EXCCODE_PNR;
        break;
    case TLBRET_PE:
        /* Privileged Exception */
        cs->exception_index = EXCCODE_PPI;
        break;
    }

    if (tlb_error == TLBRET_NOMATCH) {
        env->CSR_TLBRBADV = address;
        if (is_la64(env)) {
            env->CSR_TLBREHI = FIELD_DP64(env->CSR_TLBREHI, CSR_TLBREHI_64,
                                        VPPN, extract64(address, 13, 35));
        } else {
            env->CSR_TLBREHI = FIELD_DP64(env->CSR_TLBREHI, CSR_TLBREHI_32,
                                        VPPN, extract64(address, 13, 19));
        }
    } else {
        if (!FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)) {
            env->CSR_BADV = address;
        }
        env->CSR_TLBEHI = address & (TARGET_PAGE_MASK << 1);
   }
}

static void invalidate_tlb_entry(CPULoongArchState *env, int index)
{
    target_ulong addr, mask, pagesize;
    uint8_t tlb_ps;
    LoongArchTLB *tlb = &env->tlb[index];
    int idxmap = BIT(MMU_KERNEL_IDX) | BIT(MMU_USER_IDX);
    uint64_t tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
    bool tlb_v;

    tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    pagesize = MAKE_64BIT_MASK(tlb_ps, 1);
    mask = MAKE_64BIT_MASK(0, tlb_ps + 1);
    addr = (tlb_vppn << R_TLB_MISC_VPPN_SHIFT) & ~mask;
    addr = sextract64(addr, 0, TARGET_VIRT_ADDR_SPACE_BITS);

    tlb_v = pte_present(env, tlb->tlb_entry0);
    if (tlb_v) {
        tlb_flush_range_by_mmuidx(env_cpu(env), addr, pagesize,
                                  idxmap, TARGET_LONG_BITS);
    }

    tlb_v = pte_present(env, tlb->tlb_entry1);
    if (tlb_v) {
        tlb_flush_range_by_mmuidx(env_cpu(env), addr + pagesize, pagesize,
                                  idxmap, TARGET_LONG_BITS);
    }
}

static void invalidate_tlb(CPULoongArchState *env, int index)
{
    LoongArchTLB *tlb;
    uint16_t csr_asid, tlb_asid, tlb_g;
    uint8_t tlb_e;

    csr_asid = FIELD_EX64(env->CSR_ASID, CSR_ASID, ASID);
    tlb = &env->tlb[index];
    tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
    if (!tlb_e) {
        return;
    }

    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
    tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
    tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
    /* QEMU TLB is flushed when asid is changed */
    if (tlb_g == 0 && tlb_asid != csr_asid) {
        return;
    }
    invalidate_tlb_entry(env, index);
}

/* Prepare tlb entry information in software PTW mode */
static void sptw_prepare_context(CPULoongArchState *env, MMUContext *context)
{
    uint64_t lo0, lo1, csr_vppn;
    uint8_t csr_ps;

    if (FIELD_EX64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR)) {
        csr_ps = FIELD_EX64(env->CSR_TLBREHI, CSR_TLBREHI, PS);
        if (is_la64(env)) {
            csr_vppn = FIELD_EX64(env->CSR_TLBREHI, CSR_TLBREHI_64, VPPN);
        } else {
            csr_vppn = FIELD_EX64(env->CSR_TLBREHI, CSR_TLBREHI_32, VPPN);
        }
        lo0 = env->CSR_TLBRELO0;
        lo1 = env->CSR_TLBRELO1;
    } else {
        csr_ps = FIELD_EX64(env->CSR_TLBIDX, CSR_TLBIDX, PS);
        if (is_la64(env)) {
            csr_vppn = FIELD_EX64(env->CSR_TLBEHI, CSR_TLBEHI_64, VPPN);
        } else {
            csr_vppn = FIELD_EX64(env->CSR_TLBEHI, CSR_TLBEHI_32, VPPN);
        }
        lo0 = env->CSR_TLBELO0;
        lo1 = env->CSR_TLBELO1;
    }

    context->ps = csr_ps;
    context->addr = csr_vppn << R_TLB_MISC_VPPN_SHIFT;
    context->pte_buddy[0] = lo0;
    context->pte_buddy[1] = lo1;
}

static void fill_tlb_entry(CPULoongArchState *env, LoongArchTLB *tlb,
                           MMUContext *context)
{
    uint64_t lo0, lo1, csr_vppn;
    uint16_t csr_asid;
    uint8_t csr_ps;

    csr_vppn = context->addr >> R_TLB_MISC_VPPN_SHIFT;
    csr_ps   = context->ps;
    lo0      = context->pte_buddy[0];
    lo1      = context->pte_buddy[1];

    /* Store page size in field PS */
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, PS, csr_ps);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, VPPN, csr_vppn);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 1);
    csr_asid = FIELD_EX64(env->CSR_ASID, CSR_ASID, ASID);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, ASID, csr_asid);

    tlb->tlb_entry0 = lo0;
    tlb->tlb_entry1 = lo1;
}

/* Return an random value between low and high */
static uint32_t get_random_tlb(uint32_t low, uint32_t high)
{
    uint32_t val;

    qemu_guest_getrandom_nofail(&val, sizeof(val));
    return val % (high - low + 1) + low;
}

/*
 * One tlb entry holds an adjacent odd/even pair, the vpn is the
 * content of the virtual page number divided by 2. So the
 * compare vpn is bit[47:15] for 16KiB page. while the vppn
 * field in tlb entry contains bit[47:13], so need adjust.
 * virt_vpn = vaddr[47:13]
 */
static LoongArchTLB *loongarch_tlb_search_cb(CPULoongArchState *env,
                                             vaddr vaddr, int csr_asid,
                                             tlb_match func)
{
    LoongArchTLB *tlb;
    uint16_t tlb_asid, stlb_idx;
    uint8_t tlb_e, tlb_ps, stlb_ps;
    bool tlb_g;
    int i, compare_shift;
    uint64_t vpn, tlb_vppn;

    stlb_ps = FIELD_EX64(env->CSR_STLBPS, CSR_STLBPS, PS);
    vpn = (vaddr & TARGET_VIRT_MASK) >> (stlb_ps + 1);
    stlb_idx = vpn & 0xff; /* VA[25:15] <==> TLBIDX.index for 16KiB Page */
    compare_shift = stlb_ps + 1 - R_TLB_MISC_VPPN_SHIFT;

    /* Search STLB */
    for (i = 0; i < 8; ++i) {
        tlb = &env->tlb[i * 256 + stlb_idx];
        tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
        if (tlb_e) {
            tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = !!FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);

            if (func(tlb_g, csr_asid, tlb_asid) &&
                (vpn == (tlb_vppn >> compare_shift))) {
                return tlb;
            }
        }
    }

    /* Search MTLB */
    for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; ++i) {
        tlb = &env->tlb[i];
        tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
        if (tlb_e) {
            tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
            tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            compare_shift = tlb_ps + 1 - R_TLB_MISC_VPPN_SHIFT;
            vpn = (vaddr & TARGET_VIRT_MASK) >> (tlb_ps + 1);
            if (func(tlb_g, csr_asid, tlb_asid) &&
                (vpn == (tlb_vppn >> compare_shift))) {
                return tlb;
            }
        }
    }
    return NULL;
}

static bool loongarch_tlb_search(CPULoongArchState *env, vaddr vaddr,
                                 int *index)
{
    int csr_asid;
    tlb_match func;
    LoongArchTLB *tlb;

    func = tlb_match_any;
    csr_asid = FIELD_EX64(env->CSR_ASID, CSR_ASID, ASID);
    tlb = loongarch_tlb_search_cb(env, vaddr, csr_asid, func);
    if (tlb) {
        *index = tlb - env->tlb;
        return true;
    }

    return false;
}

void helper_tlbsrch(CPULoongArchState *env)
{
    int index, match;

    if (FIELD_EX64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR)) {
        match = loongarch_tlb_search(env, env->CSR_TLBREHI, &index);
    } else {
        match = loongarch_tlb_search(env, env->CSR_TLBEHI, &index);
    }

    if (match) {
        env->CSR_TLBIDX = FIELD_DP64(env->CSR_TLBIDX, CSR_TLBIDX, INDEX, index);
        env->CSR_TLBIDX = FIELD_DP64(env->CSR_TLBIDX, CSR_TLBIDX, NE, 0);
        return;
    }

    env->CSR_TLBIDX = FIELD_DP64(env->CSR_TLBIDX, CSR_TLBIDX, NE, 1);
}

void helper_tlbrd(CPULoongArchState *env)
{
    LoongArchTLB *tlb;
    int index;
    uint8_t tlb_ps, tlb_e;

    index = FIELD_EX64(env->CSR_TLBIDX, CSR_TLBIDX, INDEX);
    tlb = &env->tlb[index];
    tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);

    if (!tlb_e) {
        /* Invalid TLB entry */
        env->CSR_TLBIDX = FIELD_DP64(env->CSR_TLBIDX, CSR_TLBIDX, NE, 1);
        env->CSR_ASID  = FIELD_DP64(env->CSR_ASID, CSR_ASID, ASID, 0);
        env->CSR_TLBEHI = 0;
        env->CSR_TLBELO0 = 0;
        env->CSR_TLBELO1 = 0;
        env->CSR_TLBIDX = FIELD_DP64(env->CSR_TLBIDX, CSR_TLBIDX, PS, 0);
    } else {
        /* Valid TLB entry */
        env->CSR_TLBIDX = FIELD_DP64(env->CSR_TLBIDX, CSR_TLBIDX, NE, 0);
        env->CSR_TLBIDX = FIELD_DP64(env->CSR_TLBIDX, CSR_TLBIDX,
                                     PS, (tlb_ps & 0x3f));
        env->CSR_TLBEHI = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN) <<
                                     R_TLB_MISC_VPPN_SHIFT;
        env->CSR_TLBELO0 = tlb->tlb_entry0;
        env->CSR_TLBELO1 = tlb->tlb_entry1;
    }
}

static void update_tlb_index(CPULoongArchState *env, MMUContext *context,
                             int index)
{
    LoongArchTLB *old, new = {};
    bool skip_inv = false, tlb_v0, tlb_v1;

    old = env->tlb + index;
    fill_tlb_entry(env, &new, context);
    /* Check whether ASID/VPPN is the same */
    if (old->tlb_misc == new.tlb_misc) {
        /* Check whether both even/odd pages is the same or invalid */
        tlb_v0 = pte_present(env, old->tlb_entry0);
        tlb_v1 = pte_present(env, old->tlb_entry1);
        if ((!tlb_v0 || new.tlb_entry0 == old->tlb_entry0) &&
            (!tlb_v1 || new.tlb_entry1 == old->tlb_entry1)) {
            skip_inv = true;
        }
    }

    /* flush tlb before updating the entry */
    if (!skip_inv) {
        invalidate_tlb(env, index);
    }

    *old = new;
}

void helper_tlbwr(CPULoongArchState *env)
{
    int index = FIELD_EX64(env->CSR_TLBIDX, CSR_TLBIDX, INDEX);
    MMUContext context;

    if (FIELD_EX64(env->CSR_TLBIDX, CSR_TLBIDX, NE)) {
        invalidate_tlb(env, index);
        return;
    }

    sptw_prepare_context(env, &context);
    update_tlb_index(env, &context, index);
}

static int get_tlb_random_index(CPULoongArchState *env, vaddr addr,
                                int pagesize)
{
    uint64_t address;
    int index, set, i, stlb_idx;
    uint16_t asid, tlb_asid, stlb_ps;
    LoongArchTLB *tlb;
    uint8_t tlb_e, tlb_g;

    /* Validity of stlb_ps is checked in helper_csrwr_stlbps() */
    stlb_ps = FIELD_EX64(env->CSR_STLBPS, CSR_STLBPS, PS);
    asid = FIELD_EX64(env->CSR_ASID, CSR_ASID, ASID);
    if (pagesize == stlb_ps) {
        /* Only write into STLB bits [47:13] */
        address = addr & ~MAKE_64BIT_MASK(0, R_CSR_TLBEHI_64_VPPN_SHIFT);
        set = -1;
        stlb_idx = (address >> (stlb_ps + 1)) & 0xff; /* [0,255] */
        for (i = 0; i < 8; ++i) {
            tlb = &env->tlb[i * 256 + stlb_idx];
            tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
            if (!tlb_e) {
                set = i;
                break;
            }

            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (tlb_g == 0 && asid != tlb_asid) {
                set = i;
            }
        }

        /* Choose one set randomly */
        if (set < 0) {
            set = get_random_tlb(0, 7);
        }
        index = set * 256 + stlb_idx;
    } else {
        /* Only write into MTLB */
        index = -1;
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            tlb = &env->tlb[i];
            tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);

            if (!tlb_e) {
                index = i;
                break;
            }

            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (tlb_g == 0 && asid != tlb_asid) {
                index = i;
            }
        }

        if (index < 0) {
            index = get_random_tlb(LOONGARCH_STLB, LOONGARCH_TLB_MAX - 1);
        }
    }

    return index;
}

void helper_tlbfill(CPULoongArchState *env)
{
    vaddr entryhi;
    int index, pagesize;
    MMUContext context;

    if (FIELD_EX64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR)) {
        entryhi = env->CSR_TLBREHI;
        /* Validity of pagesize is checked in helper_ldpte() */
        pagesize = FIELD_EX64(env->CSR_TLBREHI, CSR_TLBREHI, PS);
    } else {
        entryhi = env->CSR_TLBEHI;
        /* Validity of pagesize is checked in helper_tlbrd() */
        pagesize = FIELD_EX64(env->CSR_TLBIDX, CSR_TLBIDX, PS);
    }

    sptw_prepare_context(env, &context);
    index = get_tlb_random_index(env, entryhi, pagesize);
    invalidate_tlb(env, index);
    fill_tlb_entry(env, env->tlb + index, &context);
}

void helper_tlbclr(CPULoongArchState *env)
{
    LoongArchTLB *tlb;
    int i, index;
    uint16_t csr_asid, tlb_asid, tlb_g;

    csr_asid = FIELD_EX64(env->CSR_ASID, CSR_ASID, ASID);
    index = FIELD_EX64(env->CSR_TLBIDX, CSR_TLBIDX, INDEX);

    if (index < LOONGARCH_STLB) {
        /* STLB. One line per operation */
        for (i = 0; i < 8; i++) {
            tlb = &env->tlb[i * 256 + (index % 256)];
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (!tlb_g && tlb_asid == csr_asid) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
            }
        }
    } else if (index < LOONGARCH_TLB_MAX) {
        /* All MTLB entries */
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            tlb = &env->tlb[i];
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (!tlb_g && tlb_asid == csr_asid) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
            }
        }
    }

    tlb_flush(env_cpu(env));
}

void helper_tlbflush(CPULoongArchState *env)
{
    int i, index;

    index = FIELD_EX64(env->CSR_TLBIDX, CSR_TLBIDX, INDEX);

    if (index < LOONGARCH_STLB) {
        /* STLB. One line per operation */
        for (i = 0; i < 8; i++) {
            int s_idx = i * 256 + (index % 256);
            env->tlb[s_idx].tlb_misc = FIELD_DP64(env->tlb[s_idx].tlb_misc,
                                                  TLB_MISC, E, 0);
        }
    } else if (index < LOONGARCH_TLB_MAX) {
        /* All MTLB entries */
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            env->tlb[i].tlb_misc = FIELD_DP64(env->tlb[i].tlb_misc,
                                              TLB_MISC, E, 0);
        }
    }

    tlb_flush(env_cpu(env));
}

void helper_invtlb_all(CPULoongArchState *env)
{
    for (int i = 0; i < LOONGARCH_TLB_MAX; i++) {
        env->tlb[i].tlb_misc = FIELD_DP64(env->tlb[i].tlb_misc,
                                          TLB_MISC, E, 0);
    }
    tlb_flush(env_cpu(env));
}

void helper_invtlb_all_g(CPULoongArchState *env, uint32_t g)
{
    for (int i = 0; i < LOONGARCH_TLB_MAX; i++) {
        LoongArchTLB *tlb = &env->tlb[i];
        uint8_t tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);

        if (tlb_g == g) {
            tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
        }
    }
    tlb_flush(env_cpu(env));
}

void helper_invtlb_all_asid(CPULoongArchState *env, target_ulong info)
{
    uint16_t asid = info & R_CSR_ASID_ASID_MASK;

    for (int i = 0; i < LOONGARCH_TLB_MAX; i++) {
        LoongArchTLB *tlb = &env->tlb[i];
        uint8_t tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
        uint16_t tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);

        if (!tlb_g && (tlb_asid == asid)) {
            tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
        }
    }
    tlb_flush(env_cpu(env));
}

void helper_invtlb_page_asid(CPULoongArchState *env, target_ulong info,
                             target_ulong addr)
{
    int asid = info & 0x3ff;
    LoongArchTLB *tlb;
    tlb_match func;

    func = tlb_match_asid;
    tlb = loongarch_tlb_search_cb(env, addr, asid, func);
    if (tlb) {
        invalidate_tlb(env, tlb - env->tlb);
    }
}

void helper_invtlb_page_asid_or_g(CPULoongArchState *env,
                                  target_ulong info, target_ulong addr)
{
    int asid = info & 0x3ff;
    LoongArchTLB *tlb;
    tlb_match func;

    func = tlb_match_any;
    tlb = loongarch_tlb_search_cb(env, addr, asid, func);
    if (tlb) {
        invalidate_tlb(env, tlb - env->tlb);
    }
}

static void ptw_update_tlb(CPULoongArchState *env, MMUContext *context)
{
    int index;

    index = context->tlb_index;
    if (index < 0) {
        index = get_tlb_random_index(env, context->addr, context->ps);
    }

    update_tlb_index(env, context, index);
}

bool loongarch_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                            MMUAccessType access_type, int mmu_idx,
                            bool probe, uintptr_t retaddr)
{
    CPULoongArchState *env = cpu_env(cs);
    hwaddr physical;
    int prot;
    MMUContext context;
    TLBRet ret;

    /* Data access */
    context.addr = address;
    context.tlb_index = -1;
    ret = get_physical_address(env, &context, access_type, mmu_idx, 0);
    if (ret == TLBRET_MATCH && context.mmu_index != MMU_DA_IDX
        && cpu_has_ptw(env)) {
        bool need_update = true;

        if (access_type == MMU_DATA_STORE && pte_dirty(context.pte)) {
            need_update = false;
        } else if (access_type != MMU_DATA_STORE && pte_access(context.pte)) {
            need_update = false;

            /*
             * FIXME: should context.prot be set without PAGE_WRITE with
             * pte_write(context.pte) && !pte_dirty(context.pte)??
             *
             * Otherwise there will be no loongarch_cpu_tlb_fill() function call
             * for MMU_DATA_STORE access_type in future since QEMU TLB with
             * prot PAGE_WRITE is added already
             */
        }

        if (need_update) {
            /* Need update bit A/D in PTE entry, take PTW again */
            ret = TLBRET_NOMATCH;
        }
    }

    if (ret != TLBRET_MATCH && cpu_has_ptw(env)) {
        /* Take HW PTW if TLB missed or bit P is zero */
        if (ret == TLBRET_NOMATCH || ret == TLBRET_INVALID) {
            ret = loongarch_ptw(env, &context, access_type, mmu_idx, 0);
            if (ret == TLBRET_MATCH) {
                ptw_update_tlb(env, &context);
            }
        } else if (context.tlb_index >= 0) {
            invalidate_tlb(env, context.tlb_index);
        }
    }

    if (ret == TLBRET_MATCH) {
        physical = context.physical;
        prot = context.prot;
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " physical " HWADDR_FMT_plx
                      " prot %d\n", __func__, address, physical, prot);
        return true;
    } else {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " ret %d\n", __func__, address,
                      ret);
    }
    if (probe) {
        return false;
    }
    raise_mmu_exception(env, address, access_type, ret);
    cpu_loop_exit_restore(cs, retaddr);
}

target_ulong helper_lddir(CPULoongArchState *env, target_ulong base,
                          uint32_t level, uint32_t mem_idx)
{
    CPUState *cs = env_cpu(env);
    target_ulong badvaddr, index, phys;
    uint64_t dir_base, dir_width;

    if (unlikely((level == 0) || (level > 4))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attepted LDDIR with level %u\n", level);
        return base;
    }

    if (FIELD_EX64(base, TLBENTRY, HUGE)) {
        if (unlikely(level == 4)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Attempted use of level 4 huge page\n");
            return base;
        }

        if (FIELD_EX64(base, TLBENTRY, LEVEL)) {
            return base;
        } else {
            return FIELD_DP64(base, TLBENTRY, LEVEL, level);
        }
    }

    badvaddr = env->CSR_TLBRBADV;
    base = base & TARGET_PHYS_MASK;
    get_dir_base_width(env, &dir_base, &dir_width, level);
    index = (badvaddr >> dir_base) & ((1 << dir_width) - 1);
    phys = base | index << 3;
    return ldq_phys(cs->as, phys) & TARGET_PHYS_MASK;
}

void helper_ldpte(CPULoongArchState *env, target_ulong base, target_ulong odd,
                  uint32_t mem_idx)
{
    CPUState *cs = env_cpu(env);
    target_ulong phys, tmp0, ptindex, ptoffset0, ptoffset1, badv;
    uint64_t ptbase = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, PTBASE);
    uint64_t ptwidth = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, PTWIDTH);
    uint64_t dir_base, dir_width;
    uint8_t  ps;

    /*
     * The parameter "base" has only two types,
     * one is the page table base address,
     * whose bit 6 should be 0,
     * and the other is the huge page entry,
     * whose bit 6 should be 1.
     */
    base = base & TARGET_PHYS_MASK;
    if (FIELD_EX64(base, TLBENTRY, HUGE)) {
        /*
         * Gets the huge page level and Gets huge page size.
         * Clears the huge page level information in the entry.
         * Clears huge page bit.
         * Move HGLOBAL bit to GLOBAL bit.
         */
        get_dir_base_width(env, &dir_base, &dir_width,
                           FIELD_EX64(base, TLBENTRY, LEVEL));

        base = FIELD_DP64(base, TLBENTRY, LEVEL, 0);
        base = FIELD_DP64(base, TLBENTRY, HUGE, 0);
        if (FIELD_EX64(base, TLBENTRY, HGLOBAL)) {
            base = FIELD_DP64(base, TLBENTRY, HGLOBAL, 0);
            base = FIELD_DP64(base, TLBENTRY, G, 1);
        }

        ps = dir_base + dir_width - 1;
        /*
         * Huge pages are evenly split into parity pages
         * when loaded into the tlb,
         * so the tlb page size needs to be divided by 2.
         */
        tmp0 = base;
        if (odd) {
            tmp0 += MAKE_64BIT_MASK(ps, 1);
        }

        if (!check_ps(env, ps)) {
            qemu_log_mask(LOG_GUEST_ERROR, "Illegal huge pagesize %d\n", ps);
            return;
        }
    } else {
        badv = env->CSR_TLBRBADV;

        ptindex = (badv >> ptbase) & ((1 << ptwidth) - 1);
        ptindex = ptindex & ~0x1;   /* clear bit 0 */
        ptoffset0 = ptindex << 3;
        ptoffset1 = (ptindex + 1) << 3;
        phys = base | (odd ? ptoffset1 : ptoffset0);
        tmp0 = ldq_phys(cs->as, phys) & TARGET_PHYS_MASK;
        ps = ptbase;
    }

    if (odd) {
        env->CSR_TLBRELO1 = tmp0;
    } else {
        env->CSR_TLBRELO0 = tmp0;
    }
    env->CSR_TLBREHI = FIELD_DP64(env->CSR_TLBREHI, CSR_TLBREHI, PS, ps);
}

static TLBRet loongarch_map_tlb_entry(CPULoongArchState *env,
                                      MMUContext *context,
                                      MMUAccessType access_type, int index,
                                      int mmu_idx)
{
    LoongArchTLB *tlb = &env->tlb[index];
    uint8_t tlb_ps, n;

    tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    n = (context->addr >> tlb_ps) & 0x1;/* Odd or even */
    context->pte = n ? tlb->tlb_entry1 : tlb->tlb_entry0;
    context->ps = tlb_ps;
    context->tlb_index = index;
    return loongarch_check_pte(env, context, access_type, mmu_idx);
}

TLBRet loongarch_get_addr_from_tlb(CPULoongArchState *env,
                                   MMUContext *context,
                                   MMUAccessType access_type, int mmu_idx)
{
    int index, match;

    match = loongarch_tlb_search(env, context->addr, &index);
    if (match) {
        return loongarch_map_tlb_entry(env, context, access_type, index,
                                       mmu_idx);
    }

    return TLBRET_NOMATCH;
}
