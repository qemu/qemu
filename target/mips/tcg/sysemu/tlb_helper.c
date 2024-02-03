/*
 * MIPS TLB (Translation lookaside buffer) helpers.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"

#include "cpu.h"
#include "internal.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"
#include "exec/helper-proto.h"

/* TLB management */
static void r4k_mips_tlb_flush_extra(CPUMIPSState *env, int first)
{
    /* Discard entries from env->tlb[first] onwards.  */
    while (env->tlb->tlb_in_use > first) {
        r4k_invalidate_tlb(env, --env->tlb->tlb_in_use, 0);
    }
}

static inline uint64_t get_tlb_pfn_from_entrylo(uint64_t entrylo)
{
#if defined(TARGET_MIPS64)
    return extract64(entrylo, 6, 54);
#else
    return extract64(entrylo, 6, 24) | /* PFN */
           (extract64(entrylo, 32, 32) << 24); /* PFNX */
#endif
}

static void r4k_fill_tlb(CPUMIPSState *env, int idx)
{
    r4k_tlb_t *tlb;
    uint64_t mask = env->CP0_PageMask >> (TARGET_PAGE_BITS + 1);

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    if (env->CP0_EntryHi & (1 << CP0EnHi_EHINV)) {
        tlb->EHINV = 1;
        return;
    }
    tlb->EHINV = 0;
    tlb->VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    tlb->VPN &= env->SEGMask;
#endif
    tlb->ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    tlb->MMID = env->CP0_MemoryMapID;
    tlb->PageMask = env->CP0_PageMask;
    tlb->G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    tlb->V0 = (env->CP0_EntryLo0 & 2) != 0;
    tlb->D0 = (env->CP0_EntryLo0 & 4) != 0;
    tlb->C0 = (env->CP0_EntryLo0 >> 3) & 0x7;
    tlb->XI0 = (env->CP0_EntryLo0 >> CP0EnLo_XI) & 1;
    tlb->RI0 = (env->CP0_EntryLo0 >> CP0EnLo_RI) & 1;
    tlb->PFN[0] = (get_tlb_pfn_from_entrylo(env->CP0_EntryLo0) & ~mask) << 12;
    tlb->V1 = (env->CP0_EntryLo1 & 2) != 0;
    tlb->D1 = (env->CP0_EntryLo1 & 4) != 0;
    tlb->C1 = (env->CP0_EntryLo1 >> 3) & 0x7;
    tlb->XI1 = (env->CP0_EntryLo1 >> CP0EnLo_XI) & 1;
    tlb->RI1 = (env->CP0_EntryLo1 >> CP0EnLo_RI) & 1;
    tlb->PFN[1] = (get_tlb_pfn_from_entrylo(env->CP0_EntryLo1) & ~mask) << 12;
}

static void r4k_helper_tlbinv(CPUMIPSState *env)
{
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    uint32_t tlb_mmid;
    r4k_tlb_t *tlb;
    int idx;

    MMID = mi ? MMID : (uint32_t) ASID;
    for (idx = 0; idx < env->tlb->nb_tlb; idx++) {
        tlb = &env->tlb->mmu.r4k.tlb[idx];
        tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
        if (!tlb->G && tlb_mmid == MMID) {
            tlb->EHINV = 1;
        }
    }
    cpu_mips_tlb_flush(env);
}

static void r4k_helper_tlbinvf(CPUMIPSState *env)
{
    int idx;

    for (idx = 0; idx < env->tlb->nb_tlb; idx++) {
        env->tlb->mmu.r4k.tlb[idx].EHINV = 1;
    }
    cpu_mips_tlb_flush(env);
}

static void r4k_helper_tlbwi(CPUMIPSState *env)
{
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    target_ulong VPN;
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    uint32_t tlb_mmid;
    bool EHINV, G, V0, D0, V1, D1, XI0, XI1, RI0, RI1;
    r4k_tlb_t *tlb;
    int idx;

    MMID = mi ? MMID : (uint32_t) ASID;

    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    VPN &= env->SEGMask;
#endif
    EHINV = (env->CP0_EntryHi & (1 << CP0EnHi_EHINV)) != 0;
    G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    V0 = (env->CP0_EntryLo0 & 2) != 0;
    D0 = (env->CP0_EntryLo0 & 4) != 0;
    XI0 = (env->CP0_EntryLo0 >> CP0EnLo_XI) &1;
    RI0 = (env->CP0_EntryLo0 >> CP0EnLo_RI) &1;
    V1 = (env->CP0_EntryLo1 & 2) != 0;
    D1 = (env->CP0_EntryLo1 & 4) != 0;
    XI1 = (env->CP0_EntryLo1 >> CP0EnLo_XI) &1;
    RI1 = (env->CP0_EntryLo1 >> CP0EnLo_RI) &1;

    tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
    /*
     * Discard cached TLB entries, unless tlbwi is just upgrading access
     * permissions on the current entry.
     */
    if (tlb->VPN != VPN || tlb_mmid != MMID || tlb->G != G ||
        (!tlb->EHINV && EHINV) ||
        (tlb->V0 && !V0) || (tlb->D0 && !D0) ||
        (!tlb->XI0 && XI0) || (!tlb->RI0 && RI0) ||
        (tlb->V1 && !V1) || (tlb->D1 && !D1) ||
        (!tlb->XI1 && XI1) || (!tlb->RI1 && RI1)) {
        r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);
    }

    r4k_invalidate_tlb(env, idx, 0);
    r4k_fill_tlb(env, idx);
}

static void r4k_helper_tlbwr(CPUMIPSState *env)
{
    int r = cpu_mips_get_random(env);

    r4k_invalidate_tlb(env, r, 1);
    r4k_fill_tlb(env, r);
}

static void r4k_helper_tlbp(CPUMIPSState *env)
{
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    r4k_tlb_t *tlb;
    target_ulong mask;
    target_ulong tag;
    target_ulong VPN;
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    uint32_t tlb_mmid;
    int i;

    MMID = mi ? MMID : (uint32_t) ASID;
    for (i = 0; i < env->tlb->nb_tlb; i++) {
        tlb = &env->tlb->mmu.r4k.tlb[i];
        /* 1k pages are not supported. */
        mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
        tag = env->CP0_EntryHi & ~mask;
        VPN = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
        tag &= env->SEGMask;
#endif
        tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
        /* Check ASID/MMID, virtual page number & size */
        if ((tlb->G == 1 || tlb_mmid == MMID) && VPN == tag && !tlb->EHINV) {
            /* TLB match */
            env->CP0_Index = i;
            break;
        }
    }
    if (i == env->tlb->nb_tlb) {
        /* No match.  Discard any shadow entries, if any of them match.  */
        for (i = env->tlb->nb_tlb; i < env->tlb->tlb_in_use; i++) {
            tlb = &env->tlb->mmu.r4k.tlb[i];
            /* 1k pages are not supported. */
            mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
            tag = env->CP0_EntryHi & ~mask;
            VPN = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
            tag &= env->SEGMask;
#endif
            tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
            /* Check ASID/MMID, virtual page number & size */
            if ((tlb->G == 1 || tlb_mmid == MMID) && VPN == tag) {
                r4k_mips_tlb_flush_extra(env, i);
                break;
            }
        }

        env->CP0_Index |= 0x80000000;
    }
}

static inline uint64_t get_entrylo_pfn_from_tlb(uint64_t tlb_pfn)
{
#if defined(TARGET_MIPS64)
    return tlb_pfn << 6;
#else
    return (extract64(tlb_pfn, 0, 24) << 6) | /* PFN */
           (extract64(tlb_pfn, 24, 32) << 32); /* PFNX */
#endif
}

static void r4k_helper_tlbr(CPUMIPSState *env)
{
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    uint32_t tlb_mmid;
    r4k_tlb_t *tlb;
    int idx;

    MMID = mi ? MMID : (uint32_t) ASID;
    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];

    tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
    /* If this will change the current ASID/MMID, flush qemu's TLB.  */
    if (MMID != tlb_mmid) {
        cpu_mips_tlb_flush(env);
    }

    r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);

    if (tlb->EHINV) {
        env->CP0_EntryHi = 1 << CP0EnHi_EHINV;
        env->CP0_PageMask = 0;
        env->CP0_EntryLo0 = 0;
        env->CP0_EntryLo1 = 0;
    } else {
        env->CP0_EntryHi = mi ? tlb->VPN : tlb->VPN | tlb->ASID;
        env->CP0_MemoryMapID = tlb->MMID;
        env->CP0_PageMask = tlb->PageMask;
        env->CP0_EntryLo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2) |
                        ((uint64_t)tlb->RI0 << CP0EnLo_RI) |
                        ((uint64_t)tlb->XI0 << CP0EnLo_XI) | (tlb->C0 << 3) |
                        get_entrylo_pfn_from_tlb(tlb->PFN[0] >> 12);
        env->CP0_EntryLo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2) |
                        ((uint64_t)tlb->RI1 << CP0EnLo_RI) |
                        ((uint64_t)tlb->XI1 << CP0EnLo_XI) | (tlb->C1 << 3) |
                        get_entrylo_pfn_from_tlb(tlb->PFN[1] >> 12);
    }
}

void helper_tlbwi(CPUMIPSState *env)
{
    env->tlb->helper_tlbwi(env);
}

void helper_tlbwr(CPUMIPSState *env)
{
    env->tlb->helper_tlbwr(env);
}

void helper_tlbp(CPUMIPSState *env)
{
    env->tlb->helper_tlbp(env);
}

void helper_tlbr(CPUMIPSState *env)
{
    env->tlb->helper_tlbr(env);
}

void helper_tlbinv(CPUMIPSState *env)
{
    env->tlb->helper_tlbinv(env);
}

void helper_tlbinvf(CPUMIPSState *env)
{
    env->tlb->helper_tlbinvf(env);
}

static void global_invalidate_tlb(CPUMIPSState *env,
                           uint32_t invMsgVPN2,
                           uint8_t invMsgR,
                           uint32_t invMsgMMid,
                           bool invAll,
                           bool invVAMMid,
                           bool invMMid,
                           bool invVA)
{

    int idx;
    r4k_tlb_t *tlb;
    bool VAMatch;
    bool MMidMatch;

    for (idx = 0; idx < env->tlb->nb_tlb; idx++) {
        tlb = &env->tlb->mmu.r4k.tlb[idx];
        VAMatch =
            (((tlb->VPN & ~tlb->PageMask) == (invMsgVPN2 & ~tlb->PageMask))
#ifdef TARGET_MIPS64
            &&
            (extract64(env->CP0_EntryHi, 62, 2) == invMsgR)
#endif
            );
        MMidMatch = tlb->MMID == invMsgMMid;
        if ((invAll && (idx > env->CP0_Wired)) ||
            (VAMatch && invVAMMid && (tlb->G || MMidMatch)) ||
            (VAMatch && invVA) ||
            (MMidMatch && !(tlb->G) && invMMid)) {
            tlb->EHINV = 1;
        }
    }
    cpu_mips_tlb_flush(env);
}

void helper_ginvt(CPUMIPSState *env, target_ulong arg, uint32_t type)
{
    bool invAll = type == 0;
    bool invVA = type == 1;
    bool invMMid = type == 2;
    bool invVAMMid = type == 3;
    uint32_t invMsgVPN2 = arg & (TARGET_PAGE_MASK << 1);
    uint8_t invMsgR = 0;
    uint32_t invMsgMMid = env->CP0_MemoryMapID;
    CPUState *other_cs = first_cpu;

#ifdef TARGET_MIPS64
    invMsgR = extract64(arg, 62, 2);
#endif

    CPU_FOREACH(other_cs) {
        MIPSCPU *other_cpu = MIPS_CPU(other_cs);
        global_invalidate_tlb(&other_cpu->env, invMsgVPN2, invMsgR, invMsgMMid,
                              invAll, invVAMMid, invMMid, invVA);
    }
}

/* no MMU emulation */
static int no_mmu_map_address(CPUMIPSState *env, hwaddr *physical, int *prot,
                              target_ulong address, MMUAccessType access_type)
{
    *physical = address;
    *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    return TLBRET_MATCH;
}

/* fixed mapping MMU emulation */
static int fixed_mmu_map_address(CPUMIPSState *env, hwaddr *physical,
                                 int *prot, target_ulong address,
                                 MMUAccessType access_type)
{
    if (address <= (int32_t)0x7FFFFFFFUL) {
        if (!(env->CP0_Status & (1 << CP0St_ERL))) {
            *physical = address + 0x40000000UL;
        } else {
            *physical = address;
        }
    } else if (address <= (int32_t)0xBFFFFFFFUL) {
        *physical = address & 0x1FFFFFFF;
    } else {
        *physical = address;
    }

    *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    return TLBRET_MATCH;
}

/* MIPS32/MIPS64 R4000-style MMU emulation */
static int r4k_map_address(CPUMIPSState *env, hwaddr *physical, int *prot,
                           target_ulong address, MMUAccessType access_type)
{
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    uint32_t tlb_mmid;
    int i;

    MMID = mi ? MMID : (uint32_t) ASID;

    for (i = 0; i < env->tlb->tlb_in_use; i++) {
        r4k_tlb_t *tlb = &env->tlb->mmu.r4k.tlb[i];
        /* 1k pages are not supported. */
        target_ulong mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
        target_ulong tag = address & ~mask;
        target_ulong VPN = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
        tag &= env->SEGMask;
#endif

        /* Check ASID/MMID, virtual page number & size */
        tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
        if ((tlb->G == 1 || tlb_mmid == MMID) && VPN == tag && !tlb->EHINV) {
            /* TLB match */
            int n = !!(address & mask & ~(mask >> 1));
            /* Check access rights */
            if (!(n ? tlb->V1 : tlb->V0)) {
                return TLBRET_INVALID;
            }
            if (access_type == MMU_INST_FETCH && (n ? tlb->XI1 : tlb->XI0)) {
                return TLBRET_XI;
            }
            if (access_type == MMU_DATA_LOAD && (n ? tlb->RI1 : tlb->RI0)) {
                return TLBRET_RI;
            }
            if (access_type != MMU_DATA_STORE || (n ? tlb->D1 : tlb->D0)) {
                *physical = tlb->PFN[n] | (address & (mask >> 1));
                *prot = PAGE_READ;
                if (n ? tlb->D1 : tlb->D0) {
                    *prot |= PAGE_WRITE;
                }
                if (!(n ? tlb->XI1 : tlb->XI0)) {
                    *prot |= PAGE_EXEC;
                }
                return TLBRET_MATCH;
            }
            return TLBRET_DIRTY;
        }
    }
    return TLBRET_NOMATCH;
}

static void no_mmu_init(CPUMIPSState *env, const mips_def_t *def)
{
    env->tlb->nb_tlb = 1;
    env->tlb->map_address = &no_mmu_map_address;
}

static void fixed_mmu_init(CPUMIPSState *env, const mips_def_t *def)
{
    env->tlb->nb_tlb = 1;
    env->tlb->map_address = &fixed_mmu_map_address;
}

static void r4k_mmu_init(CPUMIPSState *env, const mips_def_t *def)
{
    env->tlb->nb_tlb = 1 + ((def->CP0_Config1 >> CP0C1_MMU) & 63);
    env->tlb->map_address = &r4k_map_address;
    env->tlb->helper_tlbwi = r4k_helper_tlbwi;
    env->tlb->helper_tlbwr = r4k_helper_tlbwr;
    env->tlb->helper_tlbp = r4k_helper_tlbp;
    env->tlb->helper_tlbr = r4k_helper_tlbr;
    env->tlb->helper_tlbinv = r4k_helper_tlbinv;
    env->tlb->helper_tlbinvf = r4k_helper_tlbinvf;
}

void mmu_init(CPUMIPSState *env, const mips_def_t *def)
{
    env->tlb = g_malloc0(sizeof(CPUMIPSTLBContext));

    switch (def->mmu_type) {
    case MMU_TYPE_NONE:
        no_mmu_init(env, def);
        break;
    case MMU_TYPE_R4000:
        r4k_mmu_init(env, def);
        break;
    case MMU_TYPE_FMT:
        fixed_mmu_init(env, def);
        break;
    case MMU_TYPE_R3000:
    case MMU_TYPE_R6000:
    case MMU_TYPE_R8000:
    default:
        cpu_abort(env_cpu(env), "MMU type not supported\n");
    }
}

void cpu_mips_tlb_flush(CPUMIPSState *env)
{
    /* Flush qemu's TLB and discard all shadowed entries.  */
    tlb_flush(env_cpu(env));
    env->tlb->tlb_in_use = env->tlb->nb_tlb;
}

static void raise_mmu_exception(CPUMIPSState *env, target_ulong address,
                                MMUAccessType access_type, int tlb_error)
{
    CPUState *cs = env_cpu(env);
    int exception = 0, error_code = 0;

    if (access_type == MMU_INST_FETCH) {
        error_code |= EXCP_INST_NOTAVAIL;
    }

    switch (tlb_error) {
    default:
    case TLBRET_BADADDR:
        /* Reference to kernel address from user mode or supervisor mode */
        /* Reference to supervisor address from user mode */
        if (access_type == MMU_DATA_STORE) {
            exception = EXCP_AdES;
        } else {
            exception = EXCP_AdEL;
        }
        break;
    case TLBRET_NOMATCH:
        /* No TLB match for a mapped address */
        if (access_type == MMU_DATA_STORE) {
            exception = EXCP_TLBS;
        } else {
            exception = EXCP_TLBL;
        }
        error_code |= EXCP_TLB_NOMATCH;
        break;
    case TLBRET_INVALID:
        /* TLB match with no valid bit */
        if (access_type == MMU_DATA_STORE) {
            exception = EXCP_TLBS;
        } else {
            exception = EXCP_TLBL;
        }
        break;
    case TLBRET_DIRTY:
        /* TLB match but 'D' bit is cleared */
        exception = EXCP_LTLBL;
        break;
    case TLBRET_XI:
        /* Execute-Inhibit Exception */
        if (env->CP0_PageGrain & (1 << CP0PG_IEC)) {
            exception = EXCP_TLBXI;
        } else {
            exception = EXCP_TLBL;
        }
        break;
    case TLBRET_RI:
        /* Read-Inhibit Exception */
        if (env->CP0_PageGrain & (1 << CP0PG_IEC)) {
            exception = EXCP_TLBRI;
        } else {
            exception = EXCP_TLBL;
        }
        break;
    }
    /* Raise exception */
    if (!(env->hflags & MIPS_HFLAG_DM)) {
        env->CP0_BadVAddr = address;
    }
    env->CP0_Context = (env->CP0_Context & ~0x007fffff) |
                       ((address >> 9) & 0x007ffff0);
    env->CP0_EntryHi = (env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask) |
                       (env->CP0_EntryHi & (1 << CP0EnHi_EHINV)) |
                       (address & (TARGET_PAGE_MASK << 1));
#if defined(TARGET_MIPS64)
    env->CP0_EntryHi &= env->SEGMask;
    env->CP0_XContext =
        (env->CP0_XContext & ((~0ULL) << (env->SEGBITS - 7))) | /* PTEBase */
        (extract64(address, 62, 2) << (env->SEGBITS - 9)) |     /* R       */
        (extract64(address, 13, env->SEGBITS - 13) << 4);       /* BadVPN2 */
#endif
    cs->exception_index = exception;
    env->error_code = error_code;
}

#if !defined(TARGET_MIPS64)

/*
 * Perform hardware page table walk
 *
 * Memory accesses are performed using the KERNEL privilege level.
 * Synchronous exceptions detected on memory accesses cause a silent exit
 * from page table walking, resulting in a TLB or XTLB Refill exception.
 *
 * Implementations are not required to support page table walk memory
 * accesses from mapped memory regions. When an unsupported access is
 * attempted, a silent exit is taken, resulting in a TLB or XTLB Refill
 * exception.
 *
 * Note that if an exception is caused by AddressTranslation or LoadMemory
 * functions, the exception is not taken, a silent exit is taken,
 * resulting in a TLB or XTLB Refill exception.
 */

static bool get_pte(CPUMIPSState *env, uint64_t vaddr, int entry_size,
        uint64_t *pte)
{
    if ((vaddr & ((entry_size >> 3) - 1)) != 0) {
        return false;
    }
    if (entry_size == 64) {
        *pte = cpu_ldq_code(env, vaddr);
    } else {
        *pte = cpu_ldl_code(env, vaddr);
    }
    return true;
}

static uint64_t get_tlb_entry_layout(CPUMIPSState *env, uint64_t entry,
        int entry_size, int ptei)
{
    uint64_t result = entry;
    uint64_t rixi;
    if (ptei > entry_size) {
        ptei -= 32;
    }
    result >>= (ptei - 2);
    rixi = result & 3;
    result >>= 2;
    result |= rixi << CP0EnLo_XI;
    return result;
}

static int walk_directory(CPUMIPSState *env, uint64_t *vaddr,
        int directory_index, bool *huge_page, bool *hgpg_directory_hit,
        uint64_t *pw_entrylo0, uint64_t *pw_entrylo1,
        unsigned directory_shift, unsigned leaf_shift, int ptw_mmu_idx)
{
    int dph = (env->CP0_PWCtl >> CP0PC_DPH) & 0x1;
    int psn = (env->CP0_PWCtl >> CP0PC_PSN) & 0x3F;
    int hugepg = (env->CP0_PWCtl >> CP0PC_HUGEPG) & 0x1;
    int pf_ptew = (env->CP0_PWField >> CP0PF_PTEW) & 0x3F;
    uint32_t direntry_size = 1 << (directory_shift + 3);
    uint32_t leafentry_size = 1 << (leaf_shift + 3);
    uint64_t entry;
    uint64_t paddr;
    int prot;
    uint64_t lsb = 0;
    uint64_t w = 0;

    if (get_physical_address(env, &paddr, &prot, *vaddr, MMU_DATA_LOAD,
                             ptw_mmu_idx) != TLBRET_MATCH) {
        /* wrong base address */
        return 0;
    }
    if (!get_pte(env, *vaddr, direntry_size, &entry)) {
        return 0;
    }

    if ((entry & (1 << psn)) && hugepg) {
        *huge_page = true;
        *hgpg_directory_hit = true;
        entry = get_tlb_entry_layout(env, entry, leafentry_size, pf_ptew);
        w = directory_index - 1;
        if (directory_index & 0x1) {
            /* Generate adjacent page from same PTE for odd TLB page */
            lsb = BIT_ULL(w) >> 6;
            *pw_entrylo0 = entry & ~lsb; /* even page */
            *pw_entrylo1 = entry | lsb; /* odd page */
        } else if (dph) {
            int oddpagebit = 1 << leaf_shift;
            uint64_t vaddr2 = *vaddr ^ oddpagebit;
            if (*vaddr & oddpagebit) {
                *pw_entrylo1 = entry;
            } else {
                *pw_entrylo0 = entry;
            }
            if (get_physical_address(env, &paddr, &prot, vaddr2, MMU_DATA_LOAD,
                                     ptw_mmu_idx) != TLBRET_MATCH) {
                return 0;
            }
            if (!get_pte(env, vaddr2, leafentry_size, &entry)) {
                return 0;
            }
            entry = get_tlb_entry_layout(env, entry, leafentry_size, pf_ptew);
            if (*vaddr & oddpagebit) {
                *pw_entrylo0 = entry;
            } else {
                *pw_entrylo1 = entry;
            }
        } else {
            return 0;
        }
        return 1;
    } else {
        *vaddr = entry;
        return 2;
    }
}

static bool page_table_walk_refill(CPUMIPSState *env, vaddr address,
                                   int ptw_mmu_idx)
{
    int gdw = (env->CP0_PWSize >> CP0PS_GDW) & 0x3F;
    int udw = (env->CP0_PWSize >> CP0PS_UDW) & 0x3F;
    int mdw = (env->CP0_PWSize >> CP0PS_MDW) & 0x3F;
    int ptw = (env->CP0_PWSize >> CP0PS_PTW) & 0x3F;
    int ptew = (env->CP0_PWSize >> CP0PS_PTEW) & 0x3F;

    /* Initial values */
    bool huge_page = false;
    bool hgpg_bdhit = false;
    bool hgpg_gdhit = false;
    bool hgpg_udhit = false;
    bool hgpg_mdhit = false;

    int32_t pw_pagemask = 0;
    target_ulong pw_entryhi = 0;
    uint64_t pw_entrylo0 = 0;
    uint64_t pw_entrylo1 = 0;

    /* Native pointer size */
    /*For the 32-bit architectures, this bit is fixed to 0.*/
    int native_shift = (((env->CP0_PWSize >> CP0PS_PS) & 1) == 0) ? 2 : 3;

    /* Indices from PWField */
    int pf_gdw = (env->CP0_PWField >> CP0PF_GDW) & 0x3F;
    int pf_udw = (env->CP0_PWField >> CP0PF_UDW) & 0x3F;
    int pf_mdw = (env->CP0_PWField >> CP0PF_MDW) & 0x3F;
    int pf_ptw = (env->CP0_PWField >> CP0PF_PTW) & 0x3F;
    int pf_ptew = (env->CP0_PWField >> CP0PF_PTEW) & 0x3F;

    /* Indices computed from faulting address */
    int gindex = (address >> pf_gdw) & ((1 << gdw) - 1);
    int uindex = (address >> pf_udw) & ((1 << udw) - 1);
    int mindex = (address >> pf_mdw) & ((1 << mdw) - 1);
    int ptindex = (address >> pf_ptw) & ((1 << ptw) - 1);

    /* Other HTW configs */
    int hugepg = (env->CP0_PWCtl >> CP0PC_HUGEPG) & 0x1;
    unsigned directory_shift, leaf_shift;

    /* Offsets into tables */
    unsigned goffset, uoffset, moffset, ptoffset0, ptoffset1;
    uint32_t leafentry_size;

    /* Starting address - Page Table Base */
    uint64_t vaddr = env->CP0_PWBase;

    uint64_t dir_entry;
    uint64_t paddr;
    int prot;
    int m;

    if (!(env->CP0_Config3 & (1 << CP0C3_PW))) {
        /* walker is unimplemented */
        return false;
    }
    if (!(env->CP0_PWCtl & (1 << CP0PC_PWEN))) {
        /* walker is disabled */
        return false;
    }
    if (!(gdw > 0 || udw > 0 || mdw > 0)) {
        /* no structure to walk */
        return false;
    }
    if (ptew > 1) {
        return false;
    }

    /* HTW Shift values (depend on entry size) */
    directory_shift = (hugepg && (ptew == 1)) ? native_shift + 1 : native_shift;
    leaf_shift = (ptew == 1) ? native_shift + 1 : native_shift;

    goffset = gindex << directory_shift;
    uoffset = uindex << directory_shift;
    moffset = mindex << directory_shift;
    ptoffset0 = (ptindex >> 1) << (leaf_shift + 1);
    ptoffset1 = ptoffset0 | (1 << (leaf_shift));

    leafentry_size = 1 << (leaf_shift + 3);

    /* Global Directory */
    if (gdw > 0) {
        vaddr |= goffset;
        switch (walk_directory(env, &vaddr, pf_gdw, &huge_page, &hgpg_gdhit,
                               &pw_entrylo0, &pw_entrylo1,
                               directory_shift, leaf_shift, ptw_mmu_idx))
        {
        case 0:
            return false;
        case 1:
            goto refill;
        case 2:
        default:
            break;
        }
    }

    /* Upper directory */
    if (udw > 0) {
        vaddr |= uoffset;
        switch (walk_directory(env, &vaddr, pf_udw, &huge_page, &hgpg_udhit,
                               &pw_entrylo0, &pw_entrylo1,
                               directory_shift, leaf_shift, ptw_mmu_idx))
        {
        case 0:
            return false;
        case 1:
            goto refill;
        case 2:
        default:
            break;
        }
    }

    /* Middle directory */
    if (mdw > 0) {
        vaddr |= moffset;
        switch (walk_directory(env, &vaddr, pf_mdw, &huge_page, &hgpg_mdhit,
                               &pw_entrylo0, &pw_entrylo1,
                               directory_shift, leaf_shift, ptw_mmu_idx))
        {
        case 0:
            return false;
        case 1:
            goto refill;
        case 2:
        default:
            break;
        }
    }

    /* Leaf Level Page Table - First half of PTE pair */
    vaddr |= ptoffset0;
    if (get_physical_address(env, &paddr, &prot, vaddr, MMU_DATA_LOAD,
                             ptw_mmu_idx) != TLBRET_MATCH) {
        return false;
    }
    if (!get_pte(env, vaddr, leafentry_size, &dir_entry)) {
        return false;
    }
    dir_entry = get_tlb_entry_layout(env, dir_entry, leafentry_size, pf_ptew);
    pw_entrylo0 = dir_entry;

    /* Leaf Level Page Table - Second half of PTE pair */
    vaddr |= ptoffset1;
    if (get_physical_address(env, &paddr, &prot, vaddr, MMU_DATA_LOAD,
                             ptw_mmu_idx) != TLBRET_MATCH) {
        return false;
    }
    if (!get_pte(env, vaddr, leafentry_size, &dir_entry)) {
        return false;
    }
    dir_entry = get_tlb_entry_layout(env, dir_entry, leafentry_size, pf_ptew);
    pw_entrylo1 = dir_entry;

refill:

    m = (1 << pf_ptw) - 1;

    if (huge_page) {
        switch (hgpg_bdhit << 3 | hgpg_gdhit << 2 | hgpg_udhit << 1 |
                hgpg_mdhit)
        {
        case 4:
            m = (1 << pf_gdw) - 1;
            if (pf_gdw & 1) {
                m >>= 1;
            }
            break;
        case 2:
            m = (1 << pf_udw) - 1;
            if (pf_udw & 1) {
                m >>= 1;
            }
            break;
        case 1:
            m = (1 << pf_mdw) - 1;
            if (pf_mdw & 1) {
                m >>= 1;
            }
            break;
        }
    }
    pw_pagemask = m >> TARGET_PAGE_BITS_MIN;
    update_pagemask(env, pw_pagemask << CP0PM_MASK, &pw_pagemask);
    pw_entryhi = (address & ~0x1fff) | (env->CP0_EntryHi & 0xFF);
    {
        target_ulong tmp_entryhi = env->CP0_EntryHi;
        int32_t tmp_pagemask = env->CP0_PageMask;
        uint64_t tmp_entrylo0 = env->CP0_EntryLo0;
        uint64_t tmp_entrylo1 = env->CP0_EntryLo1;

        env->CP0_EntryHi = pw_entryhi;
        env->CP0_PageMask = pw_pagemask;
        env->CP0_EntryLo0 = pw_entrylo0;
        env->CP0_EntryLo1 = pw_entrylo1;

        /*
         * The hardware page walker inserts a page into the TLB in a manner
         * identical to a TLBWR instruction as executed by the software refill
         * handler.
         */
        r4k_helper_tlbwr(env);

        env->CP0_EntryHi = tmp_entryhi;
        env->CP0_PageMask = tmp_pagemask;
        env->CP0_EntryLo0 = tmp_entrylo0;
        env->CP0_EntryLo1 = tmp_entrylo1;
    }
    return true;
}
#endif

bool mips_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    hwaddr physical;
    int prot;
    int ret = TLBRET_BADADDR;

    /* data access */
    /* XXX: put correct access by using cpu_restore_state() correctly */
    ret = get_physical_address(env, &physical, &prot, address,
                               access_type, mmu_idx);
    switch (ret) {
    case TLBRET_MATCH:
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " physical " HWADDR_FMT_plx
                      " prot %d\n", __func__, address, physical, prot);
        break;
    default:
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " ret %d\n", __func__, address,
                      ret);
        break;
    }
    if (ret == TLBRET_MATCH) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }
#if !defined(TARGET_MIPS64)
    if ((ret == TLBRET_NOMATCH) && (env->tlb->nb_tlb > 1)) {
        /*
         * Memory reads during hardware page table walking are performed
         * as if they were kernel-mode load instructions.
         */
        int ptw_mmu_idx = (env->hflags & MIPS_HFLAG_ERL ?
                           MMU_ERL_IDX : MMU_KERNEL_IDX);

        if (page_table_walk_refill(env, address, ptw_mmu_idx)) {
            ret = get_physical_address(env, &physical, &prot, address,
                                       access_type, mmu_idx);
            if (ret == TLBRET_MATCH) {
                tlb_set_page(cs, address & TARGET_PAGE_MASK,
                             physical & TARGET_PAGE_MASK, prot,
                             mmu_idx, TARGET_PAGE_SIZE);
                return true;
            }
        }
    }
#endif
    if (probe) {
        return false;
    }

    raise_mmu_exception(env, address, access_type, ret);
    do_raise_exception_err(env, cs->exception_index, env->error_code, retaddr);
}

hwaddr cpu_mips_translate_address(CPUMIPSState *env, target_ulong address,
                                  MMUAccessType access_type, uintptr_t retaddr)
{
    hwaddr physical;
    int prot;
    int ret = 0;
    CPUState *cs = env_cpu(env);

    /* data access */
    ret = get_physical_address(env, &physical, &prot, address, access_type,
                               mips_env_mmu_index(env));
    if (ret == TLBRET_MATCH) {
        return physical;
    }

    raise_mmu_exception(env, address, access_type, ret);
    cpu_loop_exit_restore(cs, retaddr);
}

static void set_hflags_for_handler(CPUMIPSState *env)
{
    /* Exception handlers are entered in 32-bit mode.  */
    env->hflags &= ~(MIPS_HFLAG_M16);
    /* ...except that microMIPS lets you choose.  */
    if (env->insn_flags & ASE_MICROMIPS) {
        env->hflags |= (!!(env->CP0_Config3 &
                           (1 << CP0C3_ISA_ON_EXC))
                        << MIPS_HFLAG_M16_SHIFT);
    }
}

static inline void set_badinstr_registers(CPUMIPSState *env)
{
    if (env->insn_flags & ISA_NANOMIPS32) {
        if (env->CP0_Config3 & (1 << CP0C3_BI)) {
            uint32_t instr = (cpu_lduw_code(env, env->active_tc.PC)) << 16;
            if ((instr & 0x10000000) == 0) {
                instr |= cpu_lduw_code(env, env->active_tc.PC + 2);
            }
            env->CP0_BadInstr = instr;

            if ((instr & 0xFC000000) == 0x60000000) {
                instr = cpu_lduw_code(env, env->active_tc.PC + 4) << 16;
                env->CP0_BadInstrX = instr;
            }
        }
        return;
    }

    if (env->hflags & MIPS_HFLAG_M16) {
        /* TODO: add BadInstr support for microMIPS */
        return;
    }
    if (env->CP0_Config3 & (1 << CP0C3_BI)) {
        env->CP0_BadInstr = cpu_ldl_code(env, env->active_tc.PC);
    }
    if ((env->CP0_Config3 & (1 << CP0C3_BP)) &&
        (env->hflags & MIPS_HFLAG_BMASK)) {
        env->CP0_BadInstrP = cpu_ldl_code(env, env->active_tc.PC - 4);
    }
}

void mips_cpu_do_interrupt(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    bool update_badinstr = 0;
    target_ulong offset;
    int cause = -1;

    if (qemu_loglevel_mask(CPU_LOG_INT)
        && cs->exception_index != EXCP_EXT_INTERRUPT) {
        qemu_log("%s enter: PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx
                 " %s exception\n",
                 __func__, env->active_tc.PC, env->CP0_EPC,
                 mips_exception_name(cs->exception_index));
    }
    if (cs->exception_index == EXCP_EXT_INTERRUPT &&
        (env->hflags & MIPS_HFLAG_DM)) {
        cs->exception_index = EXCP_DINT;
    }
    offset = 0x180;
    switch (cs->exception_index) {
    case EXCP_SEMIHOST:
        cs->exception_index = EXCP_NONE;
        mips_semihosting(env);
        env->active_tc.PC += env->error_code;
        return;
    case EXCP_DSS:
        env->CP0_Debug |= 1 << CP0DB_DSS;
        /*
         * Debug single step cannot be raised inside a delay slot and
         * resume will always occur on the next instruction
         * (but we assume the pc has always been updated during
         * code translation).
         */
        env->CP0_DEPC = env->active_tc.PC | !!(env->hflags & MIPS_HFLAG_M16);
        goto enter_debug_mode;
    case EXCP_DINT:
        env->CP0_Debug |= 1 << CP0DB_DINT;
        goto set_DEPC;
    case EXCP_DIB:
        env->CP0_Debug |= 1 << CP0DB_DIB;
        goto set_DEPC;
    case EXCP_DBp:
        env->CP0_Debug |= 1 << CP0DB_DBp;
        /* Setup DExcCode - SDBBP instruction */
        env->CP0_Debug = (env->CP0_Debug & ~(0x1fULL << CP0DB_DEC)) |
                         (9 << CP0DB_DEC);
        goto set_DEPC;
    case EXCP_DDBS:
        env->CP0_Debug |= 1 << CP0DB_DDBS;
        goto set_DEPC;
    case EXCP_DDBL:
        env->CP0_Debug |= 1 << CP0DB_DDBL;
    set_DEPC:
        env->CP0_DEPC = exception_resume_pc(env);
        env->hflags &= ~MIPS_HFLAG_BMASK;
 enter_debug_mode:
        if (env->insn_flags & ISA_MIPS3) {
            env->hflags |= MIPS_HFLAG_64;
            if (!(env->insn_flags & ISA_MIPS_R6) ||
                env->CP0_Status & (1 << CP0St_KX)) {
                env->hflags &= ~MIPS_HFLAG_AWRAP;
            }
        }
        env->hflags |= MIPS_HFLAG_DM | MIPS_HFLAG_CP0;
        env->hflags &= ~(MIPS_HFLAG_KSU);
        /* EJTAG probe trap enable is not implemented... */
        if (!(env->CP0_Status & (1 << CP0St_EXL))) {
            env->CP0_Cause &= ~(1U << CP0Ca_BD);
        }
        env->active_tc.PC = env->exception_base + 0x480;
        set_hflags_for_handler(env);
        break;
    case EXCP_RESET:
        cpu_reset(CPU(cpu));
        break;
    case EXCP_SRESET:
        env->CP0_Status |= (1 << CP0St_SR);
        memset(env->CP0_WatchLo, 0, sizeof(env->CP0_WatchLo));
        goto set_error_EPC;
    case EXCP_NMI:
        env->CP0_Status |= (1 << CP0St_NMI);
 set_error_EPC:
        env->CP0_ErrorEPC = exception_resume_pc(env);
        env->hflags &= ~MIPS_HFLAG_BMASK;
        env->CP0_Status |= (1 << CP0St_ERL) | (1 << CP0St_BEV);
        if (env->insn_flags & ISA_MIPS3) {
            env->hflags |= MIPS_HFLAG_64;
            if (!(env->insn_flags & ISA_MIPS_R6) ||
                env->CP0_Status & (1 << CP0St_KX)) {
                env->hflags &= ~MIPS_HFLAG_AWRAP;
            }
        }
        env->hflags |= MIPS_HFLAG_CP0;
        env->hflags &= ~(MIPS_HFLAG_KSU);
        if (!(env->CP0_Status & (1 << CP0St_EXL))) {
            env->CP0_Cause &= ~(1U << CP0Ca_BD);
        }
        env->active_tc.PC = env->exception_base;
        set_hflags_for_handler(env);
        break;
    case EXCP_EXT_INTERRUPT:
        cause = 0;
        if (env->CP0_Cause & (1 << CP0Ca_IV)) {
            uint32_t spacing = (env->CP0_IntCtl >> CP0IntCtl_VS) & 0x1f;

            if ((env->CP0_Status & (1 << CP0St_BEV)) || spacing == 0) {
                offset = 0x200;
            } else {
                uint32_t vector = 0;
                uint32_t pending = (env->CP0_Cause & CP0Ca_IP_mask) >> CP0Ca_IP;

                if (env->CP0_Config3 & (1 << CP0C3_VEIC)) {
                    /*
                     * For VEIC mode, the external interrupt controller feeds
                     * the vector through the CP0Cause IP lines.
                     */
                    vector = pending;
                } else {
                    /*
                     * Vectored Interrupts
                     * Mask with Status.IM7-IM0 to get enabled interrupts.
                     */
                    pending &= (env->CP0_Status >> CP0St_IM) & 0xff;
                    /* Find the highest-priority interrupt. */
                    while (pending >>= 1) {
                        vector++;
                    }
                }
                offset = 0x200 + (vector * (spacing << 5));
            }
        }
        goto set_EPC;
    case EXCP_LTLBL:
        cause = 1;
        update_badinstr = !(env->error_code & EXCP_INST_NOTAVAIL);
        goto set_EPC;
    case EXCP_TLBL:
        cause = 2;
        update_badinstr = !(env->error_code & EXCP_INST_NOTAVAIL);
        if ((env->error_code & EXCP_TLB_NOMATCH) &&
            !(env->CP0_Status & (1 << CP0St_EXL))) {
#if defined(TARGET_MIPS64)
            int R = env->CP0_BadVAddr >> 62;
            int UX = (env->CP0_Status & (1 << CP0St_UX)) != 0;
            int KX = (env->CP0_Status & (1 << CP0St_KX)) != 0;

            if ((R != 0 || UX) && (R != 3 || KX) &&
                (!(env->insn_flags & (INSN_LOONGSON2E | INSN_LOONGSON2F)))) {
                offset = 0x080;
            } else {
#endif
                offset = 0x000;
#if defined(TARGET_MIPS64)
            }
#endif
        }
        goto set_EPC;
    case EXCP_TLBS:
        cause = 3;
        update_badinstr = 1;
        if ((env->error_code & EXCP_TLB_NOMATCH) &&
            !(env->CP0_Status & (1 << CP0St_EXL))) {
#if defined(TARGET_MIPS64)
            int R = env->CP0_BadVAddr >> 62;
            int UX = (env->CP0_Status & (1 << CP0St_UX)) != 0;
            int KX = (env->CP0_Status & (1 << CP0St_KX)) != 0;

            if ((R != 0 || UX) && (R != 3 || KX) &&
                (!(env->insn_flags & (INSN_LOONGSON2E | INSN_LOONGSON2F)))) {
                offset = 0x080;
            } else {
#endif
                offset = 0x000;
#if defined(TARGET_MIPS64)
            }
#endif
        }
        goto set_EPC;
    case EXCP_AdEL:
        cause = 4;
        update_badinstr = !(env->error_code & EXCP_INST_NOTAVAIL);
        goto set_EPC;
    case EXCP_AdES:
        cause = 5;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_IBE:
        cause = 6;
        goto set_EPC;
    case EXCP_DBE:
        cause = 7;
        goto set_EPC;
    case EXCP_SYSCALL:
        cause = 8;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_BREAK:
        cause = 9;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_RI:
        cause = 10;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_CpU:
        cause = 11;
        update_badinstr = 1;
        env->CP0_Cause = (env->CP0_Cause & ~(0x3 << CP0Ca_CE)) |
                         (env->error_code << CP0Ca_CE);
        goto set_EPC;
    case EXCP_OVERFLOW:
        cause = 12;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_TRAP:
        cause = 13;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_MSAFPE:
        cause = 14;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_FPE:
        cause = 15;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_C2E:
        cause = 18;
        goto set_EPC;
    case EXCP_TLBRI:
        cause = 19;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_TLBXI:
        cause = 20;
        goto set_EPC;
    case EXCP_MSADIS:
        cause = 21;
        update_badinstr = 1;
        goto set_EPC;
    case EXCP_MDMX:
        cause = 22;
        goto set_EPC;
    case EXCP_DWATCH:
        cause = 23;
        /* XXX: TODO: manage deferred watch exceptions */
        goto set_EPC;
    case EXCP_MCHECK:
        cause = 24;
        goto set_EPC;
    case EXCP_THREAD:
        cause = 25;
        goto set_EPC;
    case EXCP_DSPDIS:
        cause = 26;
        goto set_EPC;
    case EXCP_CACHE:
        cause = 30;
        offset = 0x100;
 set_EPC:
        if (!(env->CP0_Status & (1 << CP0St_EXL))) {
            env->CP0_EPC = exception_resume_pc(env);
            if (update_badinstr) {
                set_badinstr_registers(env);
            }
            if (env->hflags & MIPS_HFLAG_BMASK) {
                env->CP0_Cause |= (1U << CP0Ca_BD);
            } else {
                env->CP0_Cause &= ~(1U << CP0Ca_BD);
            }
            env->CP0_Status |= (1 << CP0St_EXL);
            if (env->insn_flags & ISA_MIPS3) {
                env->hflags |= MIPS_HFLAG_64;
                if (!(env->insn_flags & ISA_MIPS_R6) ||
                    env->CP0_Status & (1 << CP0St_KX)) {
                    env->hflags &= ~MIPS_HFLAG_AWRAP;
                }
            }
            env->hflags |= MIPS_HFLAG_CP0;
            env->hflags &= ~(MIPS_HFLAG_KSU);
        }
        env->hflags &= ~MIPS_HFLAG_BMASK;
        if (env->CP0_Status & (1 << CP0St_BEV)) {
            env->active_tc.PC = env->exception_base + 0x200;
        } else if (cause == 30 && !(env->CP0_Config3 & (1 << CP0C3_SC) &&
                                    env->CP0_Config5 & (1 << CP0C5_CV))) {
            /* Force KSeg1 for cache errors */
            env->active_tc.PC = KSEG1_BASE | (env->CP0_EBase & 0x1FFFF000);
        } else {
            env->active_tc.PC = env->CP0_EBase & ~0xfff;
        }

        env->active_tc.PC += offset;
        set_hflags_for_handler(env);
        env->CP0_Cause = (env->CP0_Cause & ~(0x1f << CP0Ca_EC)) |
                         (cause << CP0Ca_EC);
        break;
    default:
        abort();
    }
    if (qemu_loglevel_mask(CPU_LOG_INT)
        && cs->exception_index != EXCP_EXT_INTERRUPT) {
        qemu_log("%s: PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx " cause %d\n"
                 "    S %08x C %08x A " TARGET_FMT_lx " D " TARGET_FMT_lx "\n",
                 __func__, env->active_tc.PC, env->CP0_EPC, cause,
                 env->CP0_Status, env->CP0_Cause, env->CP0_BadVAddr,
                 env->CP0_DEPC);
    }
    cs->exception_index = EXCP_NONE;
}

bool mips_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        MIPSCPU *cpu = MIPS_CPU(cs);
        CPUMIPSState *env = &cpu->env;

        if (cpu_mips_hw_interrupts_enabled(env) &&
            cpu_mips_hw_interrupts_pending(env)) {
            /* Raise it */
            cs->exception_index = EXCP_EXT_INTERRUPT;
            env->error_code = 0;
            mips_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}

void r4k_invalidate_tlb(CPUMIPSState *env, int idx, int use_extra)
{
    CPUState *cs = env_cpu(env);
    r4k_tlb_t *tlb;
    target_ulong addr;
    target_ulong end;
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    uint32_t tlb_mmid;
    target_ulong mask;

    MMID = mi ? MMID : (uint32_t) ASID;

    tlb = &env->tlb->mmu.r4k.tlb[idx];
    /*
     * The qemu TLB is flushed when the ASID/MMID changes, so no need to
     * flush these entries again.
     */
    tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
    if (tlb->G == 0 && tlb_mmid != MMID) {
        return;
    }

    if (use_extra && env->tlb->tlb_in_use < MIPS_TLB_MAX) {
        /*
         * For tlbwr, we can shadow the discarded entry into
         * a new (fake) TLB entry, as long as the guest can not
         * tell that it's there.
         */
        env->tlb->mmu.r4k.tlb[env->tlb->tlb_in_use] = *tlb;
        env->tlb->tlb_in_use++;
        return;
    }

    /* 1k pages are not supported. */
    mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
    if (tlb->V0) {
        addr = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
        if (addr >= (0xFFFFFFFF80000000ULL & env->SEGMask)) {
            addr |= 0x3FFFFF0000000000ULL;
        }
#endif
        end = addr | (mask >> 1);
        while (addr < end) {
            tlb_flush_page(cs, addr);
            addr += TARGET_PAGE_SIZE;
        }
    }
    if (tlb->V1) {
        addr = (tlb->VPN & ~mask) | ((mask >> 1) + 1);
#if defined(TARGET_MIPS64)
        if (addr >= (0xFFFFFFFF80000000ULL & env->SEGMask)) {
            addr |= 0x3FFFFF0000000000ULL;
        }
#endif
        end = addr | mask;
        while (addr - 1 < end) {
            tlb_flush_page(cs, addr);
            addr += TARGET_PAGE_SIZE;
        }
    }
}
