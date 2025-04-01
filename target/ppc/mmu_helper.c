/*
 *  PowerPC MMU, TLB, SLB and BAT emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "qemu/units.h"
#include "cpu.h"
#include "system/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "mmu-hash32.h"
#include "exec/cputlb.h"
#include "exec/exec-all.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "exec/log.h"
#include "helper_regs.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "internal.h"
#include "mmu-book3s-v3.h"
#include "mmu-radix64.h"
#include "mmu-booke.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"

/* #define FLUSH_ALL_TLBS */

/*****************************************************************************/
/* PowerPC MMU emulation */

/* Software driven TLB helpers */
static inline void ppc6xx_tlb_invalidate_all(CPUPPCState *env)
{
    ppc6xx_tlb_t *tlb;
    int nr, max = 2 * env->nb_tlb;

    for (nr = 0; nr < max; nr++) {
        tlb = &env->tlb.tlb6[nr];
        pte_invalidate(&tlb->pte0);
    }
    tlb_flush(env_cpu(env));
}

static inline void ppc6xx_tlb_invalidate_virt2(CPUPPCState *env,
                                               target_ulong eaddr,
                                               int is_code, int match_epn)
{
#if !defined(FLUSH_ALL_TLBS)
    CPUState *cs = env_cpu(env);
    ppc6xx_tlb_t *tlb;
    int way, nr;

    /* Invalidate ITLB + DTLB, all ways */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way, is_code);
        tlb = &env->tlb.tlb6[nr];
        if (pte_is_valid(tlb->pte0) && (match_epn == 0 || eaddr == tlb->EPN)) {
            qemu_log_mask(CPU_LOG_MMU, "TLB invalidate %d/%d "
                          TARGET_FMT_lx "\n", nr, env->nb_tlb, eaddr);
            pte_invalidate(&tlb->pte0);
            tlb_flush_page(cs, tlb->EPN);
        }
    }
#else
    /* XXX: PowerPC specification say this is valid as well */
    ppc6xx_tlb_invalidate_all(env);
#endif
}

static inline void ppc6xx_tlb_invalidate_virt(CPUPPCState *env,
                                              target_ulong eaddr, int is_code)
{
    ppc6xx_tlb_invalidate_virt2(env, eaddr, is_code, 0);
}

static void ppc6xx_tlb_store(CPUPPCState *env, target_ulong EPN, int way,
                             int is_code, target_ulong pte0, target_ulong pte1)
{
    ppc6xx_tlb_t *tlb;
    int nr;

    nr = ppc6xx_tlb_getnum(env, EPN, way, is_code);
    tlb = &env->tlb.tlb6[nr];
    qemu_log_mask(CPU_LOG_MMU, "Set TLB %d/%d EPN " TARGET_FMT_lx " PTE0 "
                  TARGET_FMT_lx " PTE1 " TARGET_FMT_lx "\n", nr, env->nb_tlb,
                  EPN, pte0, pte1);
    /* Invalidate any pending reference in QEMU for this virtual address */
    ppc6xx_tlb_invalidate_virt2(env, EPN, is_code, 1);
    tlb->pte0 = pte0;
    tlb->pte1 = pte1;
    tlb->EPN = EPN;
    /* Store last way for LRU mechanism */
    env->last_way = way;
}

/* Helpers specific to PowerPC 40x implementations */
static inline void ppc4xx_tlb_invalidate_all(CPUPPCState *env)
{
    ppcemb_tlb_t *tlb;
    int i;

    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb.tlbe[i];
        tlb->prot &= ~PAGE_VALID;
    }
    tlb_flush(env_cpu(env));
}

static void booke206_flush_tlb(CPUPPCState *env, int flags,
                               const int check_iprot)
{
    int tlb_size;
    int i, j;
    ppcmas_tlb_t *tlb = env->tlb.tlbm;

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        if (flags & (1 << i)) {
            tlb_size = booke206_tlb_size(env, i);
            for (j = 0; j < tlb_size; j++) {
                if (!check_iprot || !(tlb[j].mas1 & MAS1_IPROT)) {
                    tlb[j].mas1 &= ~MAS1_VALID;
                }
            }
        }
        tlb += booke206_tlb_size(env, i);
    }

    tlb_flush(env_cpu(env));
}

/*****************************************************************************/
/* BATs management */
#if !defined(FLUSH_ALL_TLBS)
static inline void do_invalidate_BAT(CPUPPCState *env, target_ulong BATu,
                                     target_ulong mask)
{
    CPUState *cs = env_cpu(env);
    target_ulong base, end, page;

    base = BATu & ~0x0001FFFF;
    end = base + mask + 0x00020000;
    if (((end - base) >> TARGET_PAGE_BITS) > 1024) {
        /* Flushing 1024 4K pages is slower than a complete flush */
        qemu_log_mask(CPU_LOG_MMU, "Flush all BATs\n");
        tlb_flush(cs);
        qemu_log_mask(CPU_LOG_MMU, "Flush done\n");
        return;
    }
    qemu_log_mask(CPU_LOG_MMU, "Flush BAT from " TARGET_FMT_lx
                  " to " TARGET_FMT_lx " (" TARGET_FMT_lx ")\n",
                  base, end, mask);
    for (page = base; page != end; page += TARGET_PAGE_SIZE) {
        tlb_flush_page(cs, page);
    }
    qemu_log_mask(CPU_LOG_MMU, "Flush done\n");
}
#endif

static inline void dump_store_bat(CPUPPCState *env, char ID, int ul, int nr,
                                  target_ulong value)
{
    qemu_log_mask(CPU_LOG_MMU, "Set %cBAT%d%c to " TARGET_FMT_lx " ("
                  TARGET_FMT_lx ")\n", ID, nr, ul == 0 ? 'u' : 'l',
                  value, env->nip);
}

void helper_store_ibatu(CPUPPCState *env, uint32_t nr, target_ulong value)
{
    target_ulong mask;

    dump_store_bat(env, 'I', 0, nr, value);
    if (env->IBAT[0][nr] != value) {
        mask = (value << 15) & 0x0FFE0000UL;
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#endif
        /*
         * When storing valid upper BAT, mask BEPI and BRPN and
         * invalidate all TLBs covered by this BAT
         */
        mask = (value << 15) & 0x0FFE0000UL;
        env->IBAT[0][nr] = (value & 0x00001FFFUL) |
            (value & ~0x0001FFFFUL & ~mask);
        env->IBAT[1][nr] = (env->IBAT[1][nr] & 0x0000007B) |
            (env->IBAT[1][nr] & ~0x0001FFFF & ~mask);
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#else
        tlb_flush(env_cpu(env));
#endif
    }
}

void helper_store_ibatl(CPUPPCState *env, uint32_t nr, target_ulong value)
{
    dump_store_bat(env, 'I', 1, nr, value);
    env->IBAT[1][nr] = value;
}

void helper_store_dbatu(CPUPPCState *env, uint32_t nr, target_ulong value)
{
    target_ulong mask;

    dump_store_bat(env, 'D', 0, nr, value);
    if (env->DBAT[0][nr] != value) {
        /*
         * When storing valid upper BAT, mask BEPI and BRPN and
         * invalidate all TLBs covered by this BAT
         */
        mask = (value << 15) & 0x0FFE0000UL;
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->DBAT[0][nr], mask);
#endif
        mask = (value << 15) & 0x0FFE0000UL;
        env->DBAT[0][nr] = (value & 0x00001FFFUL) |
            (value & ~0x0001FFFFUL & ~mask);
        env->DBAT[1][nr] = (env->DBAT[1][nr] & 0x0000007B) |
            (env->DBAT[1][nr] & ~0x0001FFFF & ~mask);
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->DBAT[0][nr], mask);
#else
        tlb_flush(env_cpu(env));
#endif
    }
}

void helper_store_dbatl(CPUPPCState *env, uint32_t nr, target_ulong value)
{
    dump_store_bat(env, 'D', 1, nr, value);
    env->DBAT[1][nr] = value;
}

/*****************************************************************************/
/* TLB management */
void ppc_tlb_invalidate_all(CPUPPCState *env)
{
#if defined(TARGET_PPC64)
    if (mmu_is_64bit(env->mmu_model)) {
        env->tlb_need_flush = 0;
        tlb_flush(env_cpu(env));
    } else
#endif /* defined(TARGET_PPC64) */
    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
        ppc6xx_tlb_invalidate_all(env);
        break;
    case POWERPC_MMU_SOFT_4xx:
        ppc4xx_tlb_invalidate_all(env);
        break;
    case POWERPC_MMU_REAL:
        cpu_abort(env_cpu(env), "No TLB for PowerPC 4xx in real mode\n");
        break;
    case POWERPC_MMU_MPC8xx:
        /* XXX: TODO */
        cpu_abort(env_cpu(env), "MPC8xx MMU model is not implemented\n");
        break;
    case POWERPC_MMU_BOOKE:
        tlb_flush(env_cpu(env));
        break;
    case POWERPC_MMU_BOOKE206:
        booke206_flush_tlb(env, -1, 0);
        break;
    case POWERPC_MMU_32B:
        env->tlb_need_flush = 0;
        tlb_flush(env_cpu(env));
        break;
    default:
        /* XXX: TODO */
        cpu_abort(env_cpu(env), "Unknown MMU model %x\n", env->mmu_model);
        break;
    }
}

void ppc_tlb_invalidate_one(CPUPPCState *env, target_ulong addr)
{
#if !defined(FLUSH_ALL_TLBS)
    addr &= TARGET_PAGE_MASK;
#if defined(TARGET_PPC64)
    if (mmu_is_64bit(env->mmu_model)) {
        /* tlbie invalidate TLBs for all segments */
        /*
         * XXX: given the fact that there are too many segments to invalidate,
         *      and we still don't have a tlb_flush_mask(env, n, mask) in QEMU,
         *      we just invalidate all TLBs
         */
        env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;
    } else
#endif /* defined(TARGET_PPC64) */
    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
        ppc6xx_tlb_invalidate_virt(env, addr, 0);
        ppc6xx_tlb_invalidate_virt(env, addr, 1);
        break;
    case POWERPC_MMU_32B:
        /*
         * Actual CPUs invalidate entire congruence classes based on
         * the geometry of their TLBs and some OSes take that into
         * account, we just mark the TLB to be flushed later (context
         * synchronizing event or sync instruction on 32-bit).
         */
        env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;
        break;
    default:
        /* Should never reach here with other MMU models */
        g_assert_not_reached();
    }
#else
    ppc_tlb_invalidate_all(env);
#endif
}

/*****************************************************************************/
/* Special registers manipulation */

/* Segment registers load and store */
target_ulong helper_load_sr(CPUPPCState *env, target_ulong sr_num)
{
#if defined(TARGET_PPC64)
    if (mmu_is_64bit(env->mmu_model)) {
        /* XXX */
        return 0;
    }
#endif
    return env->sr[sr_num];
}

void helper_store_sr(CPUPPCState *env, target_ulong srnum, target_ulong value)
{
    qemu_log_mask(CPU_LOG_MMU,
            "%s: reg=%d " TARGET_FMT_lx " " TARGET_FMT_lx "\n", __func__,
            (int)srnum, value, env->sr[srnum]);
#if defined(TARGET_PPC64)
    if (mmu_is_64bit(env->mmu_model)) {
        PowerPCCPU *cpu = env_archcpu(env);
        uint64_t esid, vsid;

        /* ESID = srnum */
        esid = ((uint64_t)(srnum & 0xf) << 28) | SLB_ESID_V;

        /* VSID = VSID */
        vsid = (value & 0xfffffff) << 12;
        /* flags = flags */
        vsid |= ((value >> 27) & 0xf) << 8;

        ppc_store_slb(cpu, srnum, esid, vsid);
    } else
#endif
    if (env->sr[srnum] != value) {
        env->sr[srnum] = value;
        /*
         * Invalidating 256MB of virtual memory in 4kB pages is way
         * longer than flushing the whole TLB.
         */
#if !defined(FLUSH_ALL_TLBS) && 0
        {
            target_ulong page, end;
            /* Invalidate 256 MB of virtual memory */
            page = (16 << 20) * srnum;
            end = page + (16 << 20);
            for (; page != end; page += TARGET_PAGE_SIZE) {
                tlb_flush_page(env_cpu(env), page);
            }
        }
#else
        env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;
#endif
    }
}

/* TLB management */
void helper_tlbia(CPUPPCState *env)
{
    ppc_tlb_invalidate_all(env);
}

void helper_tlbie(CPUPPCState *env, target_ulong addr)
{
    ppc_tlb_invalidate_one(env, addr);
}

#if defined(TARGET_PPC64)

/* Invalidation Selector */
#define TLBIE_IS_VA         0
#define TLBIE_IS_PID        1
#define TLBIE_IS_LPID       2
#define TLBIE_IS_ALL        3

/* Radix Invalidation Control */
#define TLBIE_RIC_TLB       0
#define TLBIE_RIC_PWC       1
#define TLBIE_RIC_ALL       2
#define TLBIE_RIC_GRP       3

/* Radix Actual Page sizes */
#define TLBIE_R_AP_4K       0
#define TLBIE_R_AP_64K      5
#define TLBIE_R_AP_2M       1
#define TLBIE_R_AP_1G       2

/* RB field masks */
#define TLBIE_RB_EPN_MASK   PPC_BITMASK(0, 51)
#define TLBIE_RB_IS_MASK    PPC_BITMASK(52, 53)
#define TLBIE_RB_AP_MASK    PPC_BITMASK(56, 58)

void helper_tlbie_isa300(CPUPPCState *env, target_ulong rb, target_ulong rs,
                         uint32_t flags)
{
    unsigned ric = (flags & TLBIE_F_RIC_MASK) >> TLBIE_F_RIC_SHIFT;
    /*
     * With the exception of the checks for invalid instruction forms,
     * PRS is currently ignored, because we don't know if a given TLB entry
     * is process or partition scoped.
     */
    bool prs = flags & TLBIE_F_PRS;
    bool r = flags & TLBIE_F_R;
    bool local = flags & TLBIE_F_LOCAL;
    bool effR;
    unsigned is = extract64(rb, PPC_BIT_NR(53), 2);
    unsigned ap;        /* actual page size */
    target_ulong addr, pgoffs_mask;

    qemu_log_mask(CPU_LOG_MMU,
        "%s: local=%d addr=" TARGET_FMT_lx " ric=%u prs=%d r=%d is=%u\n",
        __func__, local, rb & TARGET_PAGE_MASK, ric, prs, r, is);

    effR = FIELD_EX64(env->msr, MSR, HV) ? r : env->spr[SPR_LPCR] & LPCR_HR;

    /* Partial TLB invalidation is supported for Radix only for now. */
    if (!effR) {
        goto inval_all;
    }

    /* Check for invalid instruction forms (effR=1). */
    if (unlikely(ric == TLBIE_RIC_GRP ||
                 ((ric == TLBIE_RIC_PWC || ric == TLBIE_RIC_ALL) &&
                                           is == TLBIE_IS_VA) ||
                 (!prs && is == TLBIE_IS_PID))) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: invalid instruction form: ric=%u prs=%d r=%d is=%u\n",
            __func__, ric, prs, r, is);
        goto invalid;
    }

    /* We don't cache Page Walks. */
    if (ric == TLBIE_RIC_PWC) {
        if (local) {
            unsigned set = extract64(rb, PPC_BIT_NR(51), 12);
            if (set != 0) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid set: %d\n",
                              __func__, set);
                goto invalid;
            }
        }
        return;
    }

    /*
     * Invalidation by LPID or PID is not supported, so fallback
     * to full TLB flush in these cases.
     */
    if (is != TLBIE_IS_VA) {
        goto inval_all;
    }

    /*
     * The results of an attempt to invalidate a translation outside of
     * quadrant 0 for Radix Tree translation (effR=1, RIC=0, PRS=1, IS=0,
     * and EA 0:1 != 0b00) are boundedly undefined.
     */
    if (unlikely(ric == TLBIE_RIC_TLB && prs && is == TLBIE_IS_VA &&
                 (rb & R_EADDR_QUADRANT) != R_EADDR_QUADRANT0)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: attempt to invalidate a translation outside of quadrant 0\n",
            __func__);
        goto inval_all;
    }

    assert(is == TLBIE_IS_VA);
    assert(ric == TLBIE_RIC_TLB || ric == TLBIE_RIC_ALL);

    ap = extract64(rb, PPC_BIT_NR(58), 3);
    switch (ap) {
    case TLBIE_R_AP_4K:
        pgoffs_mask = 0xfffull;
        break;

    case TLBIE_R_AP_64K:
        pgoffs_mask = 0xffffull;
        break;

    case TLBIE_R_AP_2M:
        pgoffs_mask = 0x1fffffull;
        break;

    case TLBIE_R_AP_1G:
        pgoffs_mask = 0x3fffffffull;
        break;

    default:
        /*
         * If the value specified in RS 0:31, RS 32:63, RB 54:55, RB 56:58,
         * RB 44:51, or RB 56:63, when it is needed to perform the specified
         * operation, is not supported by the implementation, the instruction
         * is treated as if the instruction form were invalid.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid AP: %d\n", __func__, ap);
        goto invalid;
    }

    addr = rb & TLBIE_RB_EPN_MASK & ~pgoffs_mask;

    if (local) {
        tlb_flush_page(env_cpu(env), addr);
    } else {
        tlb_flush_page_all_cpus_synced(env_cpu(env), addr);
    }
    return;

inval_all:
    env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;
    if (!local) {
        env->tlb_need_flush |= TLB_NEED_GLOBAL_FLUSH;
    }
    return;

invalid:
    raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                           POWERPC_EXCP_INVAL |
                           POWERPC_EXCP_INVAL_INVAL, GETPC());
}

#endif

void helper_tlbiva(CPUPPCState *env, target_ulong addr)
{
    /* tlbiva instruction only exists on BookE */
    assert(env->mmu_model == POWERPC_MMU_BOOKE);
    /* XXX: TODO */
    cpu_abort(env_cpu(env), "BookE MMU model is not implemented\n");
}

/* Software driven TLBs management */
/* PowerPC 602/603 software TLB load instructions helpers */
static void do_6xx_tlb(CPUPPCState *env, target_ulong new_EPN, int is_code)
{
    target_ulong RPN, CMP, EPN;
    int way;

    RPN = env->spr[SPR_RPA];
    if (is_code) {
        CMP = env->spr[SPR_ICMP];
        EPN = env->spr[SPR_IMISS];
    } else {
        CMP = env->spr[SPR_DCMP];
        EPN = env->spr[SPR_DMISS];
    }
    way = (env->spr[SPR_SRR1] >> 17) & 1;
    (void)EPN; /* avoid a compiler warning */
    qemu_log_mask(CPU_LOG_MMU, "%s: EPN " TARGET_FMT_lx " " TARGET_FMT_lx
                  " PTE0 " TARGET_FMT_lx " PTE1 " TARGET_FMT_lx " way %d\n",
                  __func__, new_EPN, EPN, CMP, RPN, way);
    /* Store this TLB */
    ppc6xx_tlb_store(env, (uint32_t)(new_EPN & TARGET_PAGE_MASK),
                     way, is_code, CMP, RPN);
}

void helper_6xx_tlbd(CPUPPCState *env, target_ulong EPN)
{
    do_6xx_tlb(env, EPN, 0);
}

void helper_6xx_tlbi(CPUPPCState *env, target_ulong EPN)
{
    do_6xx_tlb(env, EPN, 1);
}

static inline target_ulong booke_tlb_to_page_size(int size)
{
    return 1024 << (2 * size);
}

static inline int booke_page_size_to_tlb(target_ulong page_size)
{
    int size;

    switch (page_size) {
    case 0x00000400UL:
        size = 0x0;
        break;
    case 0x00001000UL:
        size = 0x1;
        break;
    case 0x00004000UL:
        size = 0x2;
        break;
    case 0x00010000UL:
        size = 0x3;
        break;
    case 0x00040000UL:
        size = 0x4;
        break;
    case 0x00100000UL:
        size = 0x5;
        break;
    case 0x00400000UL:
        size = 0x6;
        break;
    case 0x01000000UL:
        size = 0x7;
        break;
    case 0x04000000UL:
        size = 0x8;
        break;
    case 0x10000000UL:
        size = 0x9;
        break;
    case 0x40000000UL:
        size = 0xA;
        break;
#if defined(TARGET_PPC64)
    case 0x000100000000ULL:
        size = 0xB;
        break;
    case 0x000400000000ULL:
        size = 0xC;
        break;
    case 0x001000000000ULL:
        size = 0xD;
        break;
    case 0x004000000000ULL:
        size = 0xE;
        break;
    case 0x010000000000ULL:
        size = 0xF;
        break;
#endif
    default:
        size = -1;
        break;
    }

    return size;
}

/* Helpers for 4xx TLB management */
#define PPC4XX_TLB_ENTRY_MASK       0x0000003f  /* Mask for 64 TLB entries */

#define PPC4XX_TLBHI_V              0x00000040
#define PPC4XX_TLBHI_E              0x00000020
#define PPC4XX_TLBHI_SIZE_MIN       0
#define PPC4XX_TLBHI_SIZE_MAX       7
#define PPC4XX_TLBHI_SIZE_DEFAULT   1
#define PPC4XX_TLBHI_SIZE_SHIFT     7
#define PPC4XX_TLBHI_SIZE_MASK      0x00000007

#define PPC4XX_TLBLO_EX             0x00000200
#define PPC4XX_TLBLO_WR             0x00000100
#define PPC4XX_TLBLO_ATTR_MASK      0x000000FF
#define PPC4XX_TLBLO_RPN_MASK       0xFFFFFC00

void helper_store_40x_pid(CPUPPCState *env, target_ulong val)
{
    if (env->spr[SPR_40x_PID] != val) {
        env->spr[SPR_40x_PID] = val;
        env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;
    }
}

target_ulong helper_4xx_tlbre_hi(CPUPPCState *env, target_ulong entry)
{
    ppcemb_tlb_t *tlb;
    target_ulong ret;
    int size;

    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    ret = tlb->EPN;
    if (tlb->prot & PAGE_VALID) {
        ret |= PPC4XX_TLBHI_V;
    }
    size = booke_page_size_to_tlb(tlb->size);
    if (size < PPC4XX_TLBHI_SIZE_MIN || size > PPC4XX_TLBHI_SIZE_MAX) {
        size = PPC4XX_TLBHI_SIZE_DEFAULT;
    }
    ret |= size << PPC4XX_TLBHI_SIZE_SHIFT;
    helper_store_40x_pid(env, tlb->PID);
    return ret;
}

target_ulong helper_4xx_tlbre_lo(CPUPPCState *env, target_ulong entry)
{
    ppcemb_tlb_t *tlb;
    target_ulong ret;

    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    ret = tlb->RPN;
    if (tlb->prot & PAGE_EXEC) {
        ret |= PPC4XX_TLBLO_EX;
    }
    if (tlb->prot & PAGE_WRITE) {
        ret |= PPC4XX_TLBLO_WR;
    }
    return ret;
}

static void ppcemb_tlb_flush(CPUState *cs, ppcemb_tlb_t *tlb)
{
    unsigned mmu_idx = 0;

    if (tlb->prot & 0xf) {
        mmu_idx |= 0x1;
    }
    if ((tlb->prot >> 4) & 0xf) {
        mmu_idx |= 0x2;
    }
    if (tlb->attr & 1) {
        mmu_idx <<= 2;
    }

    tlb_flush_range_by_mmuidx(cs, tlb->EPN, tlb->size, mmu_idx,
                              TARGET_LONG_BITS);
}

void helper_4xx_tlbwe_hi(CPUPPCState *env, target_ulong entry,
                         target_ulong val)
{
    CPUState *cs = env_cpu(env);
    ppcemb_tlb_t *tlb;

    qemu_log_mask(CPU_LOG_MMU, "%s entry %d val " TARGET_FMT_lx "\n",
                  __func__, (int)entry,
              val);
    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    /* Invalidate previous TLB (if it's valid) */
    if ((tlb->prot & PAGE_VALID) && tlb->PID == env->spr[SPR_40x_PID]) {
        qemu_log_mask(CPU_LOG_MMU, "%s: invalidate old TLB %d start "
                      TARGET_FMT_lx " end " TARGET_FMT_lx "\n", __func__,
                      (int)entry, tlb->EPN, tlb->EPN + tlb->size);
        ppcemb_tlb_flush(cs, tlb);
    }
    tlb->size = booke_tlb_to_page_size((val >> PPC4XX_TLBHI_SIZE_SHIFT)
                                       & PPC4XX_TLBHI_SIZE_MASK);
    /*
     * We cannot handle TLB size < TARGET_PAGE_SIZE.
     * If this ever occurs, we should implement TARGET_PAGE_BITS_VARY
     */
    if ((val & PPC4XX_TLBHI_V) && tlb->size < TARGET_PAGE_SIZE) {
        cpu_abort(cs, "TLB size " TARGET_FMT_lu " < %u "
                  "are not supported (%d)\n"
                  "Please implement TARGET_PAGE_BITS_VARY\n",
                  tlb->size, TARGET_PAGE_SIZE, (int)((val >> 7) & 0x7));
    }
    tlb->EPN = val & ~(tlb->size - 1);
    if (val & PPC4XX_TLBHI_V) {
        tlb->prot |= PAGE_VALID;
        if (val & PPC4XX_TLBHI_E) {
            /* XXX: TO BE FIXED */
            cpu_abort(cs,
                      "Little-endian TLB entries are not supported by now\n");
        }
    } else {
        tlb->prot &= ~PAGE_VALID;
    }
    tlb->PID = env->spr[SPR_40x_PID]; /* PID */
    qemu_log_mask(CPU_LOG_MMU, "%s: set up TLB %d RPN " HWADDR_FMT_plx
                  " EPN " TARGET_FMT_lx " size " TARGET_FMT_lx
                  " prot %c%c%c%c PID %d\n", __func__,
                  (int)entry, tlb->RPN, tlb->EPN, tlb->size,
                  tlb->prot & PAGE_READ ? 'r' : '-',
                  tlb->prot & PAGE_WRITE ? 'w' : '-',
                  tlb->prot & PAGE_EXEC ? 'x' : '-',
                  tlb->prot & PAGE_VALID ? 'v' : '-', (int)tlb->PID);
}

void helper_4xx_tlbwe_lo(CPUPPCState *env, target_ulong entry,
                         target_ulong val)
{
    CPUState *cs = env_cpu(env);
    ppcemb_tlb_t *tlb;

    qemu_log_mask(CPU_LOG_MMU, "%s entry %i val " TARGET_FMT_lx "\n",
                  __func__, (int)entry, val);
    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    /* Invalidate previous TLB (if it's valid) */
    if ((tlb->prot & PAGE_VALID) && tlb->PID == env->spr[SPR_40x_PID]) {
        qemu_log_mask(CPU_LOG_MMU, "%s: invalidate old TLB %d start "
                      TARGET_FMT_lx " end " TARGET_FMT_lx "\n", __func__,
                      (int)entry, tlb->EPN, tlb->EPN + tlb->size);
        ppcemb_tlb_flush(cs, tlb);
    }
    tlb->attr = val & PPC4XX_TLBLO_ATTR_MASK;
    tlb->RPN = val & PPC4XX_TLBLO_RPN_MASK;
    tlb->prot = PAGE_READ;
    if (val & PPC4XX_TLBLO_EX) {
        tlb->prot |= PAGE_EXEC;
    }
    if (val & PPC4XX_TLBLO_WR) {
        tlb->prot |= PAGE_WRITE;
    }
    qemu_log_mask(CPU_LOG_MMU, "%s: set up TLB %d RPN " HWADDR_FMT_plx
                  " EPN " TARGET_FMT_lx
                  " size " TARGET_FMT_lx " prot %c%c%c%c PID %d\n", __func__,
                  (int)entry, tlb->RPN, tlb->EPN, tlb->size,
                  tlb->prot & PAGE_READ ? 'r' : '-',
                  tlb->prot & PAGE_WRITE ? 'w' : '-',
                  tlb->prot & PAGE_EXEC ? 'x' : '-',
                  tlb->prot & PAGE_VALID ? 'v' : '-', (int)tlb->PID);
}

target_ulong helper_4xx_tlbsx(CPUPPCState *env, target_ulong address)
{
    return ppcemb_tlb_search(env, address, env->spr[SPR_40x_PID]);
}

static bool mmubooke_pid_match(CPUPPCState *env, ppcemb_tlb_t *tlb)
{
    if (tlb->PID == env->spr[SPR_BOOKE_PID]) {
        return true;
    }
    if (!env->nb_pids) {
        return false;
    }

    if (env->spr[SPR_BOOKE_PID1] && tlb->PID == env->spr[SPR_BOOKE_PID1]) {
        return true;
    }
    if (env->spr[SPR_BOOKE_PID2] && tlb->PID == env->spr[SPR_BOOKE_PID2]) {
        return true;
    }

    return false;
}

/* PowerPC 440 TLB management */
void helper_440_tlbwe(CPUPPCState *env, uint32_t word, target_ulong entry,
                      target_ulong value)
{
    ppcemb_tlb_t *tlb;

    qemu_log_mask(CPU_LOG_MMU, "%s word %d entry %d value " TARGET_FMT_lx "\n",
                  __func__, word, (int)entry, value);
    entry &= 0x3F;
    tlb = &env->tlb.tlbe[entry];

    /* Invalidate previous TLB (if it's valid) */
    if ((tlb->prot & PAGE_VALID) && mmubooke_pid_match(env, tlb)) {
        qemu_log_mask(CPU_LOG_MMU, "%s: invalidate old TLB %d start "
                      TARGET_FMT_lx " end " TARGET_FMT_lx "\n", __func__,
                      (int)entry, tlb->EPN, tlb->EPN + tlb->size);
        ppcemb_tlb_flush(env_cpu(env), tlb);
    }

    switch (word) {
    default:
        /* Just here to please gcc */
    case 0:
        tlb->EPN = value & 0xFFFFFC00;
        tlb->size = booke_tlb_to_page_size((value >> 4) & 0xF);
        tlb->attr &= ~0x1;
        tlb->attr |= (value >> 8) & 1;
        if (value & 0x200) {
            tlb->prot |= PAGE_VALID;
        } else {
            tlb->prot &= ~PAGE_VALID;
        }
        tlb->PID = env->spr[SPR_440_MMUCR] & 0x000000FF;
        break;
    case 1:
        tlb->RPN = value & 0xFFFFFC0F;
        break;
    case 2:
        tlb->attr = (tlb->attr & 0x1) | (value & 0x0000FF00);
        tlb->prot = tlb->prot & PAGE_VALID;
        if (value & 0x1) {
            tlb->prot |= PAGE_READ << 4;
        }
        if (value & 0x2) {
            tlb->prot |= PAGE_WRITE << 4;
        }
        if (value & 0x4) {
            tlb->prot |= PAGE_EXEC << 4;
        }
        if (value & 0x8) {
            tlb->prot |= PAGE_READ;
        }
        if (value & 0x10) {
            tlb->prot |= PAGE_WRITE;
        }
        if (value & 0x20) {
            tlb->prot |= PAGE_EXEC;
        }
        break;
    }
}

target_ulong helper_440_tlbre(CPUPPCState *env, uint32_t word,
                              target_ulong entry)
{
    ppcemb_tlb_t *tlb;
    target_ulong ret;
    int size;

    entry &= 0x3F;
    tlb = &env->tlb.tlbe[entry];
    switch (word) {
    default:
        /* Just here to please gcc */
    case 0:
        ret = tlb->EPN;
        size = booke_page_size_to_tlb(tlb->size);
        if (size < 0 || size > 0xF) {
            size = 1;
        }
        ret |= size << 4;
        if (tlb->attr & 0x1) {
            ret |= 0x100;
        }
        if (tlb->prot & PAGE_VALID) {
            ret |= 0x200;
        }
        env->spr[SPR_440_MMUCR] &= ~0x000000FF;
        env->spr[SPR_440_MMUCR] |= tlb->PID;
        break;
    case 1:
        ret = tlb->RPN;
        break;
    case 2:
        ret = tlb->attr & ~0x1;
        if (tlb->prot & (PAGE_READ << 4)) {
            ret |= 0x1;
        }
        if (tlb->prot & (PAGE_WRITE << 4)) {
            ret |= 0x2;
        }
        if (tlb->prot & (PAGE_EXEC << 4)) {
            ret |= 0x4;
        }
        if (tlb->prot & PAGE_READ) {
            ret |= 0x8;
        }
        if (tlb->prot & PAGE_WRITE) {
            ret |= 0x10;
        }
        if (tlb->prot & PAGE_EXEC) {
            ret |= 0x20;
        }
        break;
    }
    return ret;
}

target_ulong helper_440_tlbsx(CPUPPCState *env, target_ulong address)
{
    return ppcemb_tlb_search(env, address, env->spr[SPR_440_MMUCR] & 0xFF);
}

/* PowerPC BookE 2.06 TLB management */

static ppcmas_tlb_t *booke206_cur_tlb(CPUPPCState *env)
{
    uint32_t tlbncfg = 0;
    int esel = (env->spr[SPR_BOOKE_MAS0] & MAS0_ESEL_MASK) >> MAS0_ESEL_SHIFT;
    int ea = (env->spr[SPR_BOOKE_MAS2] & MAS2_EPN_MASK);
    int tlb;

    tlb = (env->spr[SPR_BOOKE_MAS0] & MAS0_TLBSEL_MASK) >> MAS0_TLBSEL_SHIFT;
    tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlb];

    if ((tlbncfg & TLBnCFG_HES) && (env->spr[SPR_BOOKE_MAS0] & MAS0_HES)) {
        cpu_abort(env_cpu(env), "we don't support HES yet\n");
    }

    return booke206_get_tlbm(env, tlb, ea, esel);
}

void helper_booke_setpid(CPUPPCState *env, uint32_t pidn, target_ulong pid)
{
    env->spr[pidn] = pid;
    /* changing PIDs mean we're in a different address space now */
    tlb_flush(env_cpu(env));
}

void helper_booke_set_eplc(CPUPPCState *env, target_ulong val)
{
    env->spr[SPR_BOOKE_EPLC] = val & EPID_MASK;
    tlb_flush_by_mmuidx(env_cpu(env), 1 << PPC_TLB_EPID_LOAD);
}
void helper_booke_set_epsc(CPUPPCState *env, target_ulong val)
{
    env->spr[SPR_BOOKE_EPSC] = val & EPID_MASK;
    tlb_flush_by_mmuidx(env_cpu(env), 1 << PPC_TLB_EPID_STORE);
}

static inline void flush_page(CPUPPCState *env, ppcmas_tlb_t *tlb)
{
    if (booke206_tlb_to_page_size(env, tlb) == TARGET_PAGE_SIZE) {
        tlb_flush_page(env_cpu(env), tlb->mas2 & MAS2_EPN_MASK);
    } else {
        tlb_flush(env_cpu(env));
    }
}

void helper_booke206_tlbwe(CPUPPCState *env)
{
    uint32_t tlbncfg, tlbn;
    ppcmas_tlb_t *tlb;
    uint32_t size_tlb, size_ps;
    target_ulong mask;


    switch (env->spr[SPR_BOOKE_MAS0] & MAS0_WQ_MASK) {
    case MAS0_WQ_ALWAYS:
        /* good to go, write that entry */
        break;
    case MAS0_WQ_COND:
        /* XXX check if reserved */
        if (0) {
            return;
        }
        break;
    case MAS0_WQ_CLR_RSRV:
        /* XXX clear entry */
        return;
    default:
        /* no idea what to do */
        return;
    }

    if (((env->spr[SPR_BOOKE_MAS0] & MAS0_ATSEL) == MAS0_ATSEL_LRAT) &&
        !FIELD_EX64(env->msr, MSR, GS)) {
        /* XXX we don't support direct LRAT setting yet */
        fprintf(stderr, "cpu: don't support LRAT setting yet\n");
        return;
    }

    tlbn = (env->spr[SPR_BOOKE_MAS0] & MAS0_TLBSEL_MASK) >> MAS0_TLBSEL_SHIFT;
    tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlbn];

    tlb = booke206_cur_tlb(env);

    if (!tlb) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL |
                               POWERPC_EXCP_INVAL_INVAL, GETPC());
    }

    /* check that we support the targeted size */
    size_tlb = (env->spr[SPR_BOOKE_MAS1] & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
    size_ps = booke206_tlbnps(env, tlbn);
    if ((env->spr[SPR_BOOKE_MAS1] & MAS1_VALID) && (tlbncfg & TLBnCFG_AVAIL) &&
        !(size_ps & (1 << size_tlb))) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL |
                               POWERPC_EXCP_INVAL_INVAL, GETPC());
    }

    if (FIELD_EX64(env->msr, MSR, GS)) {
        cpu_abort(env_cpu(env), "missing HV implementation\n");
    }

    if (tlb->mas1 & MAS1_VALID) {
        /*
         * Invalidate the page in QEMU TLB if it was a valid entry.
         *
         * In "PowerPC e500 Core Family Reference Manual, Rev. 1",
         * Section "12.4.2 TLB Write Entry (tlbwe) Instruction":
         * (https://www.nxp.com/docs/en/reference-manual/E500CORERM.pdf)
         *
         * "Note that when an L2 TLB entry is written, it may be displacing an
         * already valid entry in the same L2 TLB location (a victim). If a
         * valid L1 TLB entry corresponds to the L2 MMU victim entry, that L1
         * TLB entry is automatically invalidated."
         */
        flush_page(env, tlb);
    }

    tlb->mas7_3 = ((uint64_t)env->spr[SPR_BOOKE_MAS7] << 32) |
        env->spr[SPR_BOOKE_MAS3];
    tlb->mas1 = env->spr[SPR_BOOKE_MAS1];

    if ((env->spr[SPR_MMUCFG] & MMUCFG_MAVN) == MMUCFG_MAVN_V2) {
        /* For TLB which has a fixed size TSIZE is ignored with MAV2 */
        booke206_fixed_size_tlbn(env, tlbn, tlb);
    } else {
        if (!(tlbncfg & TLBnCFG_AVAIL)) {
            /* force !AVAIL TLB entries to correct page size */
            tlb->mas1 &= ~MAS1_TSIZE_MASK;
            /* XXX can be configured in MMUCSR0 */
            tlb->mas1 |= (tlbncfg & TLBnCFG_MINSIZE) >> 12;
        }
    }

    /* Make a mask from TLB size to discard invalid bits in EPN field */
    mask = ~(booke206_tlb_to_page_size(env, tlb) - 1);
    /* Add a mask for page attributes */
    mask |= MAS2_ACM | MAS2_VLE | MAS2_W | MAS2_I | MAS2_M | MAS2_G | MAS2_E;

    if (!FIELD_EX64(env->msr, MSR, CM)) {
        /*
         * Executing a tlbwe instruction in 32-bit mode will set bits
         * 0:31 of the TLB EPN field to zero.
         */
        mask &= 0xffffffff;
    }

    tlb->mas2 = env->spr[SPR_BOOKE_MAS2] & mask;

    if (!(tlbncfg & TLBnCFG_IPROT)) {
        /* no IPROT supported by TLB */
        tlb->mas1 &= ~MAS1_IPROT;
    }

    flush_page(env, tlb);
}

static inline void booke206_tlb_to_mas(CPUPPCState *env, ppcmas_tlb_t *tlb)
{
    int tlbn = booke206_tlbm_to_tlbn(env, tlb);
    int way = booke206_tlbm_to_way(env, tlb);

    env->spr[SPR_BOOKE_MAS0] = tlbn << MAS0_TLBSEL_SHIFT;
    env->spr[SPR_BOOKE_MAS0] |= way << MAS0_ESEL_SHIFT;
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_NV_SHIFT;

    env->spr[SPR_BOOKE_MAS1] = tlb->mas1;
    env->spr[SPR_BOOKE_MAS2] = tlb->mas2;
    env->spr[SPR_BOOKE_MAS3] = tlb->mas7_3;
    env->spr[SPR_BOOKE_MAS7] = tlb->mas7_3 >> 32;
}

void helper_booke206_tlbre(CPUPPCState *env)
{
    ppcmas_tlb_t *tlb = NULL;

    tlb = booke206_cur_tlb(env);
    if (!tlb) {
        env->spr[SPR_BOOKE_MAS1] = 0;
    } else {
        booke206_tlb_to_mas(env, tlb);
    }
}

void helper_booke206_tlbsx(CPUPPCState *env, target_ulong address)
{
    ppcmas_tlb_t *tlb = NULL;
    int i, j;
    hwaddr raddr;
    uint32_t spid, sas;

    spid = (env->spr[SPR_BOOKE_MAS6] & MAS6_SPID_MASK) >> MAS6_SPID_SHIFT;
    sas = env->spr[SPR_BOOKE_MAS6] & MAS6_SAS;

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int ways = booke206_tlb_ways(env, i);

        for (j = 0; j < ways; j++) {
            tlb = booke206_get_tlbm(env, i, address, j);

            if (!tlb) {
                continue;
            }

            if (ppcmas_tlb_check(env, tlb, &raddr, address, spid)) {
                continue;
            }

            if (sas != ((tlb->mas1 & MAS1_TS) >> MAS1_TS_SHIFT)) {
                continue;
            }

            booke206_tlb_to_mas(env, tlb);
            return;
        }
    }

    /* no entry found, fill with defaults */
    env->spr[SPR_BOOKE_MAS0] = env->spr[SPR_BOOKE_MAS4] & MAS4_TLBSELD_MASK;
    env->spr[SPR_BOOKE_MAS1] = env->spr[SPR_BOOKE_MAS4] & MAS4_TSIZED_MASK;
    env->spr[SPR_BOOKE_MAS2] = env->spr[SPR_BOOKE_MAS4] & MAS4_WIMGED_MASK;
    env->spr[SPR_BOOKE_MAS3] = 0;
    env->spr[SPR_BOOKE_MAS7] = 0;

    if (env->spr[SPR_BOOKE_MAS6] & MAS6_SAS) {
        env->spr[SPR_BOOKE_MAS1] |= MAS1_TS;
    }

    env->spr[SPR_BOOKE_MAS1] |= (env->spr[SPR_BOOKE_MAS6] >> 16)
        << MAS1_TID_SHIFT;

    /* next victim logic */
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_ESEL_SHIFT;
    env->last_way++;
    env->last_way &= booke206_tlb_ways(env, 0) - 1;
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_NV_SHIFT;
}

static inline void booke206_invalidate_ea_tlb(CPUPPCState *env, int tlbn,
                                              vaddr ea)
{
    int i;
    int ways = booke206_tlb_ways(env, tlbn);
    target_ulong mask;

    for (i = 0; i < ways; i++) {
        ppcmas_tlb_t *tlb = booke206_get_tlbm(env, tlbn, ea, i);
        if (!tlb) {
            continue;
        }
        mask = ~(booke206_tlb_to_page_size(env, tlb) - 1);
        if (((tlb->mas2 & MAS2_EPN_MASK) == (ea & mask)) &&
            !(tlb->mas1 & MAS1_IPROT)) {
            tlb->mas1 &= ~MAS1_VALID;
        }
    }
}

void helper_booke206_tlbivax(CPUPPCState *env, target_ulong address)
{
    CPUState *cs;

    if (address & 0x4) {
        /* flush all entries */
        if (address & 0x8) {
            /* flush all of TLB1 */
            booke206_flush_tlb(env, BOOKE206_FLUSH_TLB1, 1);
        } else {
            /* flush all of TLB0 */
            booke206_flush_tlb(env, BOOKE206_FLUSH_TLB0, 0);
        }
        return;
    }

    if (address & 0x8) {
        /* flush TLB1 entries */
        booke206_invalidate_ea_tlb(env, 1, address);
        CPU_FOREACH(cs) {
            tlb_flush(cs);
        }
    } else {
        /* flush TLB0 entries */
        booke206_invalidate_ea_tlb(env, 0, address);
        CPU_FOREACH(cs) {
            tlb_flush_page(cs, address & MAS2_EPN_MASK);
        }
    }
}

void helper_booke206_tlbilx0(CPUPPCState *env, target_ulong address)
{
    /* XXX missing LPID handling */
    booke206_flush_tlb(env, -1, 1);
}

void helper_booke206_tlbilx1(CPUPPCState *env, target_ulong address)
{
    int i, j;
    int tid = (env->spr[SPR_BOOKE_MAS6] & MAS6_SPID);
    ppcmas_tlb_t *tlb = env->tlb.tlbm;
    int tlb_size;

    /* XXX missing LPID handling */
    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        tlb_size = booke206_tlb_size(env, i);
        for (j = 0; j < tlb_size; j++) {
            if (!(tlb[j].mas1 & MAS1_IPROT) &&
                ((tlb[j].mas1 & MAS1_TID_MASK) == tid)) {
                tlb[j].mas1 &= ~MAS1_VALID;
            }
        }
        tlb += booke206_tlb_size(env, i);
    }
    tlb_flush(env_cpu(env));
}

void helper_booke206_tlbilx3(CPUPPCState *env, target_ulong address)
{
    int i, j;
    ppcmas_tlb_t *tlb;
    int tid = (env->spr[SPR_BOOKE_MAS6] & MAS6_SPID);
    int pid = tid >> MAS6_SPID_SHIFT;
    int sgs = env->spr[SPR_BOOKE_MAS5] & MAS5_SGS;
    int ind = (env->spr[SPR_BOOKE_MAS6] & MAS6_SIND) ? MAS1_IND : 0;
    /* XXX check for unsupported isize and raise an invalid opcode then */
    int size = env->spr[SPR_BOOKE_MAS6] & MAS6_ISIZE_MASK;
    /* XXX implement MAV2 handling */
    bool mav2 = false;

    /* XXX missing LPID handling */
    /* flush by pid and ea */
    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int ways = booke206_tlb_ways(env, i);

        for (j = 0; j < ways; j++) {
            tlb = booke206_get_tlbm(env, i, address, j);
            if (!tlb) {
                continue;
            }
            if ((ppcmas_tlb_check(env, tlb, NULL, address, pid) != 0) ||
                (tlb->mas1 & MAS1_IPROT) ||
                ((tlb->mas1 & MAS1_IND) != ind) ||
                ((tlb->mas8 & MAS8_TGS) != sgs)) {
                continue;
            }
            if (mav2 && ((tlb->mas1 & MAS1_TSIZE_MASK) != size)) {
                /* XXX only check when MMUCFG[TWC] || TLBnCFG[HES] */
                continue;
            }
            /* XXX e500mc doesn't match SAS, but other cores might */
            tlb->mas1 &= ~MAS1_VALID;
        }
    }
    tlb_flush(env_cpu(env));
}

void helper_booke206_tlbflush(CPUPPCState *env, target_ulong type)
{
    int flags = 0;

    if (type & 2) {
        flags |= BOOKE206_FLUSH_TLB1;
    }

    if (type & 4) {
        flags |= BOOKE206_FLUSH_TLB0;
    }

    booke206_flush_tlb(env, flags, 1);
}


void helper_check_tlb_flush_local(CPUPPCState *env)
{
    check_tlb_flush(env, false);
}

void helper_check_tlb_flush_global(CPUPPCState *env)
{
    check_tlb_flush(env, true);
}


bool ppc_cpu_tlb_fill(CPUState *cs, vaddr eaddr, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    hwaddr raddr;
    int page_size, prot;

    if (ppc_xlate(cpu, eaddr, access_type, &raddr,
                  &page_size, &prot, mmu_idx, !probe)) {
        tlb_set_page(cs, eaddr & TARGET_PAGE_MASK, raddr & TARGET_PAGE_MASK,
                     prot, mmu_idx, 1UL << page_size);
        return true;
    }
    if (probe) {
        return false;
    }
    raise_exception_err_ra(&cpu->env, cs->exception_index,
                           cpu->env.error_code, retaddr);
}
