/*
 *  PowerPC MMU, TLB, SLB and BAT emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *  Copyright (c) 2013 David Gibson, IBM Corporation
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
#include "exec/exec-all.h"
#include "exec/page-protection.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "system/hw_accel.h"
#include "system/memory.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "exec/log.h"
#include "hw/hw.h"
#include "internal.h"
#include "mmu-book3s-v3.h"
#include "mmu-books.h"
#include "helper_regs.h"

#ifdef CONFIG_TCG
#include "exec/helper-proto.h"
#endif

/* #define DEBUG_SLB */

#ifdef DEBUG_SLB
#  define LOG_SLB(...) qemu_log_mask(CPU_LOG_MMU, __VA_ARGS__)
#else
#  define LOG_SLB(...) do { } while (0)
#endif

/*
 * SLB handling
 */

static ppc_slb_t *slb_lookup(PowerPCCPU *cpu, target_ulong eaddr)
{
    CPUPPCState *env = &cpu->env;
    uint64_t esid_256M, esid_1T;
    int n;

    LOG_SLB("%s: eaddr " TARGET_FMT_lx "\n", __func__, eaddr);

    esid_256M = (eaddr & SEGMENT_MASK_256M) | SLB_ESID_V;
    esid_1T = (eaddr & SEGMENT_MASK_1T) | SLB_ESID_V;

    for (n = 0; n < cpu->hash64_opts->slb_size; n++) {
        ppc_slb_t *slb = &env->slb[n];

        LOG_SLB("%s: slot %d %016" PRIx64 " %016"
                    PRIx64 "\n", __func__, n, slb->esid, slb->vsid);
        /*
         * We check for 1T matches on all MMUs here - if the MMU
         * doesn't have 1T segment support, we will have prevented 1T
         * entries from being inserted in the slbmte code.
         */
        if (((slb->esid == esid_256M) &&
             ((slb->vsid & SLB_VSID_B) == SLB_VSID_B_256M))
            || ((slb->esid == esid_1T) &&
                ((slb->vsid & SLB_VSID_B) == SLB_VSID_B_1T))) {
            return slb;
        }
    }

    return NULL;
}

void dump_slb(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    int i;
    uint64_t slbe, slbv;

    cpu_synchronize_state(CPU(cpu));

    qemu_printf("SLB\tESID\t\t\tVSID\n");
    for (i = 0; i < cpu->hash64_opts->slb_size; i++) {
        slbe = env->slb[i].esid;
        slbv = env->slb[i].vsid;
        if (slbe == 0 && slbv == 0) {
            continue;
        }
        qemu_printf("%d\t0x%016" PRIx64 "\t0x%016" PRIx64 "\n",
                    i, slbe, slbv);
    }
}

#ifdef CONFIG_TCG
void helper_SLBIA(CPUPPCState *env, uint32_t ih)
{
    PowerPCCPU *cpu = env_archcpu(env);
    int starting_entry;
    int n;

    /*
     * slbia must always flush all TLB (which is equivalent to ERAT in ppc
     * architecture). Matching on SLB_ESID_V is not good enough, because slbmte
     * can overwrite a valid SLB without flushing its lookaside information.
     *
     * It would be possible to keep the TLB in synch with the SLB by flushing
     * when a valid entry is overwritten by slbmte, and therefore slbia would
     * not have to flush unless it evicts a valid SLB entry. However it is
     * expected that slbmte is more common than slbia, and slbia is usually
     * going to evict valid SLB entries, so that tradeoff is unlikely to be a
     * good one.
     *
     * ISA v2.05 introduced IH field with values 0,1,2,6. These all invalidate
     * the same SLB entries (everything but entry 0), but differ in what
     * "lookaside information" is invalidated. TCG can ignore this and flush
     * everything.
     *
     * ISA v3.0 introduced additional values 3,4,7, which change what SLBs are
     * invalidated.
     */

    env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;

    starting_entry = 1; /* default for IH=0,1,2,6 */

    if (env->mmu_model == POWERPC_MMU_3_00) {
        switch (ih) {
        case 0x7:
            /* invalidate no SLBs, but all lookaside information */
            return;

        case 0x3:
        case 0x4:
            /* also considers SLB entry 0 */
            starting_entry = 0;
            break;

        case 0x5:
            /* treat undefined values as ih==0, and warn */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "slbia undefined IH field %u.\n", ih);
            break;

        default:
            /* 0,1,2,6 */
            break;
        }
    }

    for (n = starting_entry; n < cpu->hash64_opts->slb_size; n++) {
        ppc_slb_t *slb = &env->slb[n];

        if (!(slb->esid & SLB_ESID_V)) {
            continue;
        }
        if (env->mmu_model == POWERPC_MMU_3_00) {
            if (ih == 0x3 && (slb->vsid & SLB_VSID_C) == 0) {
                /* preserves entries with a class value of 0 */
                continue;
            }
        }

        slb->esid &= ~SLB_ESID_V;
    }
}

#if defined(TARGET_PPC64)
void helper_SLBIAG(CPUPPCState *env, target_ulong rs, uint32_t l)
{
    PowerPCCPU *cpu = env_archcpu(env);
    int n;

    /*
     * slbiag must always flush all TLB (which is equivalent to ERAT in ppc
     * architecture). Matching on SLB_ESID_V is not good enough, because slbmte
     * can overwrite a valid SLB without flushing its lookaside information.
     *
     * It would be possible to keep the TLB in synch with the SLB by flushing
     * when a valid entry is overwritten by slbmte, and therefore slbiag would
     * not have to flush unless it evicts a valid SLB entry. However it is
     * expected that slbmte is more common than slbiag, and slbiag is usually
     * going to evict valid SLB entries, so that tradeoff is unlikely to be a
     * good one.
     */
    env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;

    for (n = 0; n < cpu->hash64_opts->slb_size; n++) {
        ppc_slb_t *slb = &env->slb[n];
        slb->esid &= ~SLB_ESID_V;
    }
}
#endif

static void __helper_slbie(CPUPPCState *env, target_ulong addr,
                           target_ulong global)
{
    PowerPCCPU *cpu = env_archcpu(env);
    ppc_slb_t *slb;

    slb = slb_lookup(cpu, addr);
    if (!slb) {
        return;
    }

    if (slb->esid & SLB_ESID_V) {
        slb->esid &= ~SLB_ESID_V;

        /*
         * XXX: given the fact that segment size is 256 MB or 1TB,
         *      and we still don't have a tlb_flush_mask(env, n, mask)
         *      in QEMU, we just invalidate all TLBs
         */
        env->tlb_need_flush |=
            (global == false ? TLB_NEED_LOCAL_FLUSH : TLB_NEED_GLOBAL_FLUSH);
    }
}

void helper_SLBIE(CPUPPCState *env, target_ulong addr)
{
    __helper_slbie(env, addr, false);
}

void helper_SLBIEG(CPUPPCState *env, target_ulong addr)
{
    __helper_slbie(env, addr, true);
}
#endif

int ppc_store_slb(PowerPCCPU *cpu, target_ulong slot,
                  target_ulong esid, target_ulong vsid)
{
    CPUPPCState *env = &cpu->env;
    ppc_slb_t *slb = &env->slb[slot];
    const PPCHash64SegmentPageSizes *sps = NULL;
    int i;

    if (slot >= cpu->hash64_opts->slb_size) {
        return -1; /* Bad slot number */
    }
    if (esid & ~(SLB_ESID_ESID | SLB_ESID_V)) {
        return -1; /* Reserved bits set */
    }
    if (vsid & (SLB_VSID_B & ~SLB_VSID_B_1T)) {
        return -1; /* Bad segment size */
    }
    if ((vsid & SLB_VSID_B) && !(ppc_hash64_has(cpu, PPC_HASH64_1TSEG))) {
        return -1; /* 1T segment on MMU that doesn't support it */
    }

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const PPCHash64SegmentPageSizes *sps1 = &cpu->hash64_opts->sps[i];

        if (!sps1->page_shift) {
            break;
        }

        if ((vsid & SLB_VSID_LLP_MASK) == sps1->slb_enc) {
            sps = sps1;
            break;
        }
    }

    if (!sps) {
        error_report("Bad page size encoding in SLB store: slot "TARGET_FMT_lu
                     " esid 0x"TARGET_FMT_lx" vsid 0x"TARGET_FMT_lx,
                     slot, esid, vsid);
        return -1;
    }

    slb->esid = esid;
    slb->vsid = vsid;
    slb->sps = sps;

    LOG_SLB("%s: " TARGET_FMT_lu " " TARGET_FMT_lx " - " TARGET_FMT_lx
            " => %016" PRIx64 " %016" PRIx64 "\n", __func__, slot, esid, vsid,
            slb->esid, slb->vsid);

    return 0;
}

#ifdef CONFIG_TCG
static int ppc_load_slb_esid(PowerPCCPU *cpu, target_ulong rb,
                             target_ulong *rt)
{
    CPUPPCState *env = &cpu->env;
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (slot >= cpu->hash64_opts->slb_size) {
        return -1;
    }

    *rt = slb->esid;
    return 0;
}

static int ppc_load_slb_vsid(PowerPCCPU *cpu, target_ulong rb,
                             target_ulong *rt)
{
    CPUPPCState *env = &cpu->env;
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (slot >= cpu->hash64_opts->slb_size) {
        return -1;
    }

    *rt = slb->vsid;
    return 0;
}

static int ppc_find_slb_vsid(PowerPCCPU *cpu, target_ulong rb,
                             target_ulong *rt)
{
    CPUPPCState *env = &cpu->env;
    ppc_slb_t *slb;

    if (!msr_is_64bit(env, env->msr)) {
        rb &= 0xffffffff;
    }
    slb = slb_lookup(cpu, rb);
    if (slb == NULL) {
        *rt = (target_ulong)-1ul;
    } else {
        *rt = slb->vsid;
    }
    return 0;
}

void helper_SLBMTE(CPUPPCState *env, target_ulong rb, target_ulong rs)
{
    PowerPCCPU *cpu = env_archcpu(env);

    if (ppc_store_slb(cpu, rb & 0xfff, rb & ~0xfffULL, rs) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
}

target_ulong helper_SLBMFEE(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_esid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

target_ulong helper_SLBFEE(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_find_slb_vsid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

target_ulong helper_SLBMFEV(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_vsid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}
#endif

/* Check No-Execute or Guarded Storage */
static inline int ppc_hash64_pte_noexec_guard(PowerPCCPU *cpu,
                                              ppc_hash_pte64_t pte)
{
    /* Exec permissions CANNOT take away read or write permissions */
    return (pte.pte1 & HPTE64_R_N) || (pte.pte1 & HPTE64_R_G) ?
            PAGE_READ | PAGE_WRITE : PAGE_READ | PAGE_WRITE | PAGE_EXEC;
}

/* Check Basic Storage Protection */
static int ppc_hash64_pte_prot(int mmu_idx,
                               ppc_slb_t *slb, ppc_hash_pte64_t pte)
{
    unsigned pp, key;
    /*
     * Some pp bit combinations have undefined behaviour, so default
     * to no access in those cases
     */
    int prot = 0;

    key = !!(mmuidx_pr(mmu_idx) ? (slb->vsid & SLB_VSID_KP)
             : (slb->vsid & SLB_VSID_KS));
    pp = (pte.pte1 & HPTE64_R_PP) | ((pte.pte1 & HPTE64_R_PP0) >> 61);

    if (key == 0) {
        switch (pp) {
        case 0x0:
        case 0x1:
        case 0x2:
            prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            break;

        case 0x3:
        case 0x6:
            prot = PAGE_READ | PAGE_EXEC;
            break;
        }
    } else {
        switch (pp) {
        case 0x0:
        case 0x6:
            break;

        case 0x1:
        case 0x3:
            prot = PAGE_READ | PAGE_EXEC;
            break;

        case 0x2:
            prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            break;
        }
    }

    return prot;
}

/* Check the instruction access permissions specified in the IAMR */
static int ppc_hash64_iamr_prot(PowerPCCPU *cpu, int key)
{
    CPUPPCState *env = &cpu->env;
    int iamr_bits = (env->spr[SPR_IAMR] >> 2 * (31 - key)) & 0x3;

    /*
     * An instruction fetch is permitted if the IAMR bit is 0.
     * If the bit is set, return PAGE_READ | PAGE_WRITE because this bit
     * can only take away EXEC permissions not READ or WRITE permissions.
     * If bit is cleared return PAGE_READ | PAGE_WRITE | PAGE_EXEC since
     * EXEC permissions are allowed.
     */
    return (iamr_bits & 0x1) ? PAGE_READ | PAGE_WRITE :
                               PAGE_READ | PAGE_WRITE | PAGE_EXEC;
}

static int ppc_hash64_amr_prot(PowerPCCPU *cpu, ppc_hash_pte64_t pte)
{
    CPUPPCState *env = &cpu->env;
    int key, amrbits;
    int prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    /* Only recent MMUs implement Virtual Page Class Key Protection */
    if (!ppc_hash64_has(cpu, PPC_HASH64_AMR)) {
        return prot;
    }

    key = HPTE64_R_KEY(pte.pte1);
    amrbits = (env->spr[SPR_AMR] >> 2 * (31 - key)) & 0x3;

    /* fprintf(stderr, "AMR protection: key=%d AMR=0x%" PRIx64 "\n", key, */
    /*         env->spr[SPR_AMR]); */

    /*
     * A store is permitted if the AMR bit is 0. Remove write
     * protection if it is set.
     */
    if (amrbits & 0x2) {
        prot &= ~PAGE_WRITE;
    }
    /*
     * A load is permitted if the AMR bit is 0. Remove read
     * protection if it is set.
     */
    if (amrbits & 0x1) {
        prot &= ~PAGE_READ;
    }

    switch (env->mmu_model) {
    /*
     * MMU version 2.07 and later support IAMR
     * Check if the IAMR allows the instruction access - it will return
     * PAGE_EXEC if it doesn't (and thus that bit will be cleared) or 0
     * if it does (and prot will be unchanged indicating execution support).
     */
    case POWERPC_MMU_2_07:
    case POWERPC_MMU_3_00:
        prot &= ppc_hash64_iamr_prot(cpu, key);
        break;
    default:
        break;
    }

    return prot;
}

static hwaddr ppc_hash64_hpt_base(PowerPCCPU *cpu)
{
    uint64_t base;

    if (cpu->vhyp) {
        return 0;
    }
    if (cpu->env.mmu_model == POWERPC_MMU_3_00) {
        ppc_v3_pate_t pate;

        if (!ppc64_v3_get_pate(cpu, cpu->env.spr[SPR_LPIDR], &pate)) {
            return 0;
        }
        base = pate.dw0;
    } else {
        base = cpu->env.spr[SPR_SDR1];
    }
    return base & SDR_64_HTABORG;
}

static hwaddr ppc_hash64_hpt_mask(PowerPCCPU *cpu)
{
    uint64_t base;

    if (cpu->vhyp) {
        return cpu->vhyp_class->hpt_mask(cpu->vhyp);
    }
    if (cpu->env.mmu_model == POWERPC_MMU_3_00) {
        ppc_v3_pate_t pate;

        if (!ppc64_v3_get_pate(cpu, cpu->env.spr[SPR_LPIDR], &pate)) {
            return 0;
        }
        base = pate.dw0;
    } else {
        base = cpu->env.spr[SPR_SDR1];
    }
    return (1ULL << ((base & SDR_64_HTABSIZE) + 18 - 7)) - 1;
}

const ppc_hash_pte64_t *ppc_hash64_map_hptes(PowerPCCPU *cpu,
                                             hwaddr ptex, int n)
{
    hwaddr pte_offset = ptex * HASH_PTE_SIZE_64;
    hwaddr base;
    hwaddr plen = n * HASH_PTE_SIZE_64;
    const ppc_hash_pte64_t *hptes;

    if (cpu->vhyp) {
        return cpu->vhyp_class->map_hptes(cpu->vhyp, ptex, n);
    }
    base = ppc_hash64_hpt_base(cpu);

    if (!base) {
        return NULL;
    }

    hptes = address_space_map(CPU(cpu)->as, base + pte_offset, &plen, false,
                              MEMTXATTRS_UNSPECIFIED);
    if (plen < (n * HASH_PTE_SIZE_64)) {
        hw_error("%s: Unable to map all requested HPTEs\n", __func__);
    }
    return hptes;
}

void ppc_hash64_unmap_hptes(PowerPCCPU *cpu, const ppc_hash_pte64_t *hptes,
                            hwaddr ptex, int n)
{
    if (cpu->vhyp) {
        cpu->vhyp_class->unmap_hptes(cpu->vhyp, hptes, ptex, n);
        return;
    }

    address_space_unmap(CPU(cpu)->as, (void *)hptes, n * HASH_PTE_SIZE_64,
                        false, n * HASH_PTE_SIZE_64);
}

bool ppc_hash64_valid_ptex(PowerPCCPU *cpu, target_ulong ptex)
{
    /* hash value/pteg group index is normalized by HPT mask */
    if (((ptex & ~7ULL) / HPTES_PER_GROUP) & ~ppc_hash64_hpt_mask(cpu)) {
        return false;
    }
    return true;
}

static unsigned hpte_page_shift(const PPCHash64SegmentPageSizes *sps,
                                uint64_t pte0, uint64_t pte1)
{
    int i;

    if (!(pte0 & HPTE64_V_LARGE)) {
        if (sps->page_shift != 12) {
            /* 4kiB page in a non 4kiB segment */
            return 0;
        }
        /* Normal 4kiB page */
        return 12;
    }

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const PPCHash64PageSize *ps = &sps->enc[i];
        uint64_t mask;

        if (!ps->page_shift) {
            break;
        }

        if (ps->page_shift == 12) {
            /* L bit is set so this can't be a 4kiB page */
            continue;
        }

        mask = ((1ULL << ps->page_shift) - 1) & HPTE64_R_RPN;

        if ((pte1 & mask) == ((uint64_t)ps->pte_enc << HPTE64_R_RPN_SHIFT)) {
            return ps->page_shift;
        }
    }

    return 0; /* Bad page size encoding */
}

static void ppc64_v3_new_to_old_hpte(target_ulong *pte0, target_ulong *pte1)
{
    /* Insert B into pte0 */
    *pte0 = (*pte0 & HPTE64_V_COMMON_BITS) |
            ((*pte1 & HPTE64_R_3_0_SSIZE_MASK) <<
             (HPTE64_V_SSIZE_SHIFT - HPTE64_R_3_0_SSIZE_SHIFT));

    /* Remove B from pte1 */
    *pte1 = *pte1 & ~HPTE64_R_3_0_SSIZE_MASK;
}


static hwaddr ppc_hash64_pteg_search(PowerPCCPU *cpu, hwaddr hash,
                                     const PPCHash64SegmentPageSizes *sps,
                                     target_ulong ptem,
                                     ppc_hash_pte64_t *pte, unsigned *pshift)
{
    int i;
    const ppc_hash_pte64_t *pteg;
    target_ulong pte0, pte1;
    target_ulong ptex;

    ptex = (hash & ppc_hash64_hpt_mask(cpu)) * HPTES_PER_GROUP;
    pteg = ppc_hash64_map_hptes(cpu, ptex, HPTES_PER_GROUP);
    if (!pteg) {
        return -1;
    }
    for (i = 0; i < HPTES_PER_GROUP; i++) {
        pte0 = ppc_hash64_hpte0(cpu, pteg, i);
        /*
         * pte0 contains the valid bit and must be read before pte1,
         * otherwise we might see an old pte1 with a new valid bit and
         * thus an inconsistent hpte value
         */
        smp_rmb();
        pte1 = ppc_hash64_hpte1(cpu, pteg, i);

        /* Convert format if necessary */
        if (cpu->env.mmu_model == POWERPC_MMU_3_00 && !cpu->vhyp) {
            ppc64_v3_new_to_old_hpte(&pte0, &pte1);
        }

        /* This compares V, B, H (secondary) and the AVPN */
        if (HPTE64_V_COMPARE(pte0, ptem)) {
            *pshift = hpte_page_shift(sps, pte0, pte1);
            /*
             * If there is no match, ignore the PTE, it could simply
             * be for a different segment size encoding and the
             * architecture specifies we should not match. Linux will
             * potentially leave behind PTEs for the wrong base page
             * size when demoting segments.
             */
            if (*pshift == 0) {
                continue;
            }
            /*
             * We don't do anything with pshift yet as qemu TLB only
             * deals with 4K pages anyway
             */
            pte->pte0 = pte0;
            pte->pte1 = pte1;
            ppc_hash64_unmap_hptes(cpu, pteg, ptex, HPTES_PER_GROUP);
            return ptex + i;
        }
    }
    ppc_hash64_unmap_hptes(cpu, pteg, ptex, HPTES_PER_GROUP);
    /*
     * We didn't find a valid entry.
     */
    return -1;
}

static hwaddr ppc_hash64_htab_lookup(PowerPCCPU *cpu,
                                     ppc_slb_t *slb, target_ulong eaddr,
                                     ppc_hash_pte64_t *pte, unsigned *pshift)
{
    CPUPPCState *env = &cpu->env;
    hwaddr hash, ptex;
    uint64_t vsid, epnmask, epn, ptem;
    const PPCHash64SegmentPageSizes *sps = slb->sps;

    /*
     * The SLB store path should prevent any bad page size encodings
     * getting in there, so:
     */
    assert(sps);

    /* If ISL is set in LPCR we need to clamp the page size to 4K */
    if (env->spr[SPR_LPCR] & LPCR_ISL) {
        /* We assume that when using TCG, 4k is first entry of SPS */
        sps = &cpu->hash64_opts->sps[0];
        assert(sps->page_shift == 12);
    }

    epnmask = ~((1ULL << sps->page_shift) - 1);

    if (slb->vsid & SLB_VSID_B) {
        /* 1TB segment */
        vsid = (slb->vsid & SLB_VSID_VSID) >> SLB_VSID_SHIFT_1T;
        epn = (eaddr & ~SEGMENT_MASK_1T) & epnmask;
        hash = vsid ^ (vsid << 25) ^ (epn >> sps->page_shift);
    } else {
        /* 256M segment */
        vsid = (slb->vsid & SLB_VSID_VSID) >> SLB_VSID_SHIFT;
        epn = (eaddr & ~SEGMENT_MASK_256M) & epnmask;
        hash = vsid ^ (epn >> sps->page_shift);
    }
    ptem = (slb->vsid & SLB_VSID_PTEM) | ((epn >> 16) & HPTE64_V_AVPN);
    ptem |= HPTE64_V_VALID;

    /* Page address translation */
    qemu_log_mask(CPU_LOG_MMU,
            "htab_base " HWADDR_FMT_plx " htab_mask " HWADDR_FMT_plx
            " hash " HWADDR_FMT_plx "\n",
            ppc_hash64_hpt_base(cpu), ppc_hash64_hpt_mask(cpu), hash);

    /* Primary PTEG lookup */
    qemu_log_mask(CPU_LOG_MMU,
            "0 htab=" HWADDR_FMT_plx "/" HWADDR_FMT_plx
            " vsid=" TARGET_FMT_lx " ptem=" TARGET_FMT_lx
            " hash=" HWADDR_FMT_plx "\n",
            ppc_hash64_hpt_base(cpu), ppc_hash64_hpt_mask(cpu),
            vsid, ptem,  hash);
    ptex = ppc_hash64_pteg_search(cpu, hash, sps, ptem, pte, pshift);

    if (ptex == -1) {
        /* Secondary PTEG lookup */
        ptem |= HPTE64_V_SECONDARY;
        qemu_log_mask(CPU_LOG_MMU,
                "1 htab=" HWADDR_FMT_plx "/" HWADDR_FMT_plx
                " vsid=" TARGET_FMT_lx " api=" TARGET_FMT_lx
                " hash=" HWADDR_FMT_plx "\n", ppc_hash64_hpt_base(cpu),
                ppc_hash64_hpt_mask(cpu), vsid, ptem, ~hash);

        ptex = ppc_hash64_pteg_search(cpu, ~hash, sps, ptem, pte, pshift);
    }

    return ptex;
}

unsigned ppc_hash64_hpte_page_shift_noslb(PowerPCCPU *cpu,
                                          uint64_t pte0, uint64_t pte1)
{
    int i;

    if (!(pte0 & HPTE64_V_LARGE)) {
        return 12;
    }

    /*
     * The encodings in env->sps need to be carefully chosen so that
     * this gives an unambiguous result.
     */
    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const PPCHash64SegmentPageSizes *sps = &cpu->hash64_opts->sps[i];
        unsigned shift;

        if (!sps->page_shift) {
            break;
        }

        shift = hpte_page_shift(sps, pte0, pte1);
        if (shift) {
            return shift;
        }
    }

    return 0;
}

static bool ppc_hash64_use_vrma(CPUPPCState *env)
{
    switch (env->mmu_model) {
    case POWERPC_MMU_3_00:
        /*
         * ISAv3.0 (POWER9) always uses VRMA, the VPM0 field and RMOR
         * register no longer exist
         */
        return true;

    default:
        return !!(env->spr[SPR_LPCR] & LPCR_VPM0);
    }
}

static void ppc_hash64_set_isi(CPUState *cs, int mmu_idx, uint64_t slb_vsid,
                               uint64_t error_code)
{
    CPUPPCState *env = &POWERPC_CPU(cs)->env;
    bool vpm;

    if (!mmuidx_real(mmu_idx)) {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM1);
    } else {
        vpm = ppc_hash64_use_vrma(env);
    }
    if (vpm && !mmuidx_hv(mmu_idx)) {
        cs->exception_index = POWERPC_EXCP_HISI;
        env->spr[SPR_ASDR] = slb_vsid;
    } else {
        cs->exception_index = POWERPC_EXCP_ISI;
    }
    env->error_code = error_code;
}

static void ppc_hash64_set_dsi(CPUState *cs, int mmu_idx, uint64_t slb_vsid,
                               uint64_t dar, uint64_t dsisr)
{
    CPUPPCState *env = &POWERPC_CPU(cs)->env;
    bool vpm;

    if (!mmuidx_real(mmu_idx)) {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM1);
    } else {
        vpm = ppc_hash64_use_vrma(env);
    }
    if (vpm && !mmuidx_hv(mmu_idx)) {
        cs->exception_index = POWERPC_EXCP_HDSI;
        env->spr[SPR_HDAR] = dar;
        env->spr[SPR_HDSISR] = dsisr;
        env->spr[SPR_ASDR] = slb_vsid;
    } else {
        cs->exception_index = POWERPC_EXCP_DSI;
        env->spr[SPR_DAR] = dar;
        env->spr[SPR_DSISR] = dsisr;
   }
    env->error_code = 0;
}


static void ppc_hash64_set_r(PowerPCCPU *cpu, hwaddr ptex, uint64_t pte1)
{
    hwaddr base, offset = ptex * HASH_PTE_SIZE_64 + HPTE64_DW1_R;

    if (cpu->vhyp) {
        cpu->vhyp_class->hpte_set_r(cpu->vhyp, ptex, pte1);
        return;
    }
    base = ppc_hash64_hpt_base(cpu);


    /* The HW performs a non-atomic byte update */
    stb_phys(CPU(cpu)->as, base + offset, ((pte1 >> 8) & 0xff) | 0x01);
}

static void ppc_hash64_set_c(PowerPCCPU *cpu, hwaddr ptex, uint64_t pte1)
{
    hwaddr base, offset = ptex * HASH_PTE_SIZE_64 + HPTE64_DW1_C;

    if (cpu->vhyp) {
        cpu->vhyp_class->hpte_set_c(cpu->vhyp, ptex, pte1);
        return;
    }
    base = ppc_hash64_hpt_base(cpu);

    /* The HW performs a non-atomic byte update */
    stb_phys(CPU(cpu)->as, base + offset, (pte1 & 0xff) | 0x80);
}

static target_ulong rmls_limit(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    /*
     * In theory the meanings of RMLS values are implementation
     * dependent.  In practice, this seems to have been the set from
     * POWER4+..POWER8, and RMLS is no longer supported in POWER9.
     *
     * Unsupported values mean the OS has shot itself in the
     * foot. Return a 0-sized RMA in this case, which we expect
     * to trigger an immediate DSI or ISI
     */
    static const target_ulong rma_sizes[16] = {
        [0] = 256 * GiB,
        [1] = 16 * GiB,
        [2] = 1 * GiB,
        [3] = 64 * MiB,
        [4] = 256 * MiB,
        [7] = 128 * MiB,
        [8] = 32 * MiB,
    };
    target_ulong rmls = (env->spr[SPR_LPCR] & LPCR_RMLS) >> LPCR_RMLS_SHIFT;

    return rma_sizes[rmls];
}

/* Return the LLP in SLB_VSID format */
static uint64_t get_vrma_llp(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    uint64_t llp;

    if (env->mmu_model == POWERPC_MMU_3_00) {
        ppc_v3_pate_t pate;
        uint64_t ps, l, lp;

        /*
         * ISA v3.0 removes the LPCR[VRMASD] field and puts the VRMA base
         * page size (L||LP equivalent) in the PS field in the HPT partition
         * table entry.
         */
        if (!ppc64_v3_get_pate(cpu, cpu->env.spr[SPR_LPIDR], &pate)) {
            error_report("Bad VRMA with no partition table entry");
            return 0;
        }
        ps = PATE0_GET_PS(pate.dw0);
        /* PS has L||LP in 3 consecutive bits, put them into SLB LLP format */
        l = (ps >> 2) & 0x1;
        lp = ps & 0x3;
        llp = (l << SLB_VSID_L_SHIFT) | (lp << SLB_VSID_LP_SHIFT);

    } else {
        uint64_t lpcr = env->spr[SPR_LPCR];
        target_ulong vrmasd = (lpcr & LPCR_VRMASD) >> LPCR_VRMASD_SHIFT;

        /* VRMASD LLP matches SLB format, just shift and mask it */
        llp = (vrmasd << SLB_VSID_LP_SHIFT) & SLB_VSID_LLP_MASK;
    }

    return llp;
}

static int build_vrma_slbe(PowerPCCPU *cpu, ppc_slb_t *slb)
{
    uint64_t llp = get_vrma_llp(cpu);
    target_ulong vsid = SLB_VSID_VRMA | llp;
    int i;

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const PPCHash64SegmentPageSizes *sps = &cpu->hash64_opts->sps[i];

        if (!sps->page_shift) {
            break;
        }

        if ((vsid & SLB_VSID_LLP_MASK) == sps->slb_enc) {
            slb->esid = SLB_ESID_V;
            slb->vsid = vsid;
            slb->sps = sps;
            return 0;
        }
    }

    error_report("Bad VRMA page size encoding 0x" TARGET_FMT_lx, llp);

    return -1;
}

bool ppc_hash64_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                      hwaddr *raddrp, int *psizep, int *protp, int mmu_idx,
                      bool guest_visible)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    ppc_slb_t vrma_slbe;
    ppc_slb_t *slb;
    unsigned apshift;
    hwaddr ptex;
    ppc_hash_pte64_t pte;
    int exec_prot, pp_prot, amr_prot, prot;
    int need_prot;
    hwaddr raddr;
    bool vrma = false;

    /*
     * Note on LPCR usage: 970 uses HID4, but our special variant of
     * store_spr copies relevant fields into env->spr[SPR_LPCR].
     * Similarly we filter unimplemented bits when storing into LPCR
     * depending on the MMU version. This code can thus just use the
     * LPCR "as-is".
     */

    /* 1. Handle real mode accesses */
    if (mmuidx_real(mmu_idx)) {
        /*
         * Translation is supposedly "off", but in real mode the top 4
         * effective address bits are (mostly) ignored
         */
        raddr = eaddr & 0x0FFFFFFFFFFFFFFFULL;

        if (cpu->vhyp) {
            /*
             * In virtual hypervisor mode, there's nothing to do:
             *   EA == GPA == qemu guest address
             */
        } else if (mmuidx_hv(mmu_idx) || !env->has_hv_mode) {
            /* In HV mode, add HRMOR if top EA bit is clear */
            if (!(eaddr >> 63)) {
                raddr |= env->spr[SPR_HRMOR];
            }
        } else if (ppc_hash64_use_vrma(env)) {
            /* Emulated VRMA mode */
            vrma = true;
            slb = &vrma_slbe;
            if (build_vrma_slbe(cpu, slb) != 0) {
                /* Invalid VRMA setup, machine check */
                if (guest_visible) {
                    cs->exception_index = POWERPC_EXCP_MCHECK;
                    env->error_code = 0;
                }
                return false;
            }

            goto skip_slb_search;
        } else {
            target_ulong limit = rmls_limit(cpu);

            /* Emulated old-style RMO mode, bounds check against RMLS */
            if (raddr >= limit) {
                if (!guest_visible) {
                    return false;
                }
                switch (access_type) {
                case MMU_INST_FETCH:
                    ppc_hash64_set_isi(cs, mmu_idx, 0, SRR1_PROTFAULT);
                    break;
                case MMU_DATA_LOAD:
                    ppc_hash64_set_dsi(cs, mmu_idx, 0, eaddr, DSISR_PROTFAULT);
                    break;
                case MMU_DATA_STORE:
                    ppc_hash64_set_dsi(cs, mmu_idx, 0, eaddr,
                                       DSISR_PROTFAULT | DSISR_ISSTORE);
                    break;
                default:
                    g_assert_not_reached();
                }
                return false;
            }

            raddr |= env->spr[SPR_RMOR];
        }

        *raddrp = raddr;
        *protp = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        *psizep = TARGET_PAGE_BITS;
        return true;
    }

    /* 2. Translation is on, so look up the SLB */
    slb = slb_lookup(cpu, eaddr);
    if (!slb) {
        /* No entry found, check if in-memory segment tables are in use */
        if (ppc64_use_proc_tbl(cpu)) {
            /* TODO - Unsupported */
            error_report("Segment Table Support Unimplemented");
            exit(1);
        }
        /* Segment still not found, generate the appropriate interrupt */
        if (!guest_visible) {
            return false;
        }
        switch (access_type) {
        case MMU_INST_FETCH:
            cs->exception_index = POWERPC_EXCP_ISEG;
            env->error_code = 0;
            break;
        case MMU_DATA_LOAD:
        case MMU_DATA_STORE:
            cs->exception_index = POWERPC_EXCP_DSEG;
            env->error_code = 0;
            env->spr[SPR_DAR] = eaddr;
            break;
        default:
            g_assert_not_reached();
        }
        return false;
    }

 skip_slb_search:

    /* 3. Check for segment level no-execute violation */
    if (access_type == MMU_INST_FETCH && (slb->vsid & SLB_VSID_N)) {
        if (guest_visible) {
            ppc_hash64_set_isi(cs, mmu_idx, slb->vsid, SRR1_NOEXEC_GUARD);
        }
        return false;
    }

    /* 4. Locate the PTE in the hash table */
    ptex = ppc_hash64_htab_lookup(cpu, slb, eaddr, &pte, &apshift);
    if (ptex == -1) {
        if (!guest_visible) {
            return false;
        }
        switch (access_type) {
        case MMU_INST_FETCH:
            ppc_hash64_set_isi(cs, mmu_idx, slb->vsid, SRR1_NOPTE);
            break;
        case MMU_DATA_LOAD:
            ppc_hash64_set_dsi(cs, mmu_idx, slb->vsid, eaddr, DSISR_NOPTE);
            break;
        case MMU_DATA_STORE:
            ppc_hash64_set_dsi(cs, mmu_idx, slb->vsid, eaddr,
                               DSISR_NOPTE | DSISR_ISSTORE);
            break;
        default:
            g_assert_not_reached();
        }
        return false;
    }
    qemu_log_mask(CPU_LOG_MMU,
                  "found PTE at index %08" HWADDR_PRIx "\n", ptex);

    /* 5. Check access permissions */

    exec_prot = ppc_hash64_pte_noexec_guard(cpu, pte);
    pp_prot = ppc_hash64_pte_prot(mmu_idx, slb, pte);
    if (vrma) {
        /* VRMA does not check keys */
        amr_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    } else {
        amr_prot = ppc_hash64_amr_prot(cpu, pte);
    }
    prot = exec_prot & pp_prot & amr_prot;

    need_prot = check_prot_access_type(PAGE_RWX, access_type);
    if (need_prot & ~prot) {
        /* Access right violation */
        qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
        if (!guest_visible) {
            return false;
        }
        if (access_type == MMU_INST_FETCH) {
            int srr1 = 0;
            if (PAGE_EXEC & ~exec_prot) {
                srr1 |= SRR1_NOEXEC_GUARD; /* Access violates noexec or guard */
            } else if (PAGE_EXEC & ~pp_prot) {
                srr1 |= SRR1_PROTFAULT; /* Access violates access authority */
            }
            if (PAGE_EXEC & ~amr_prot) {
                srr1 |= SRR1_IAMR; /* Access violates virt pg class key prot */
            }
            ppc_hash64_set_isi(cs, mmu_idx, slb->vsid, srr1);
        } else {
            int dsisr = 0;
            if (need_prot & ~pp_prot) {
                dsisr |= DSISR_PROTFAULT;
            }
            if (access_type == MMU_DATA_STORE) {
                dsisr |= DSISR_ISSTORE;
            }
            if (need_prot & ~amr_prot) {
                dsisr |= DSISR_AMR;
            }
            ppc_hash64_set_dsi(cs, mmu_idx, slb->vsid, eaddr, dsisr);
        }
        return false;
    }

    qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");

    /* 6. Update PTE referenced and changed bits if necessary */

    if (!(pte.pte1 & HPTE64_R_R)) {
        ppc_hash64_set_r(cpu, ptex, pte.pte1);
    }
    if (!(pte.pte1 & HPTE64_R_C)) {
        if (access_type == MMU_DATA_STORE) {
            ppc_hash64_set_c(cpu, ptex, pte.pte1);
        } else {
            /*
             * Treat the page as read-only for now, so that a later write
             * will pass through this function again to set the C bit
             */
            prot &= ~PAGE_WRITE;
        }
    }

    /* 7. Determine the real address from the PTE */

    *raddrp = deposit64(pte.pte1 & HPTE64_R_RPN, 0, apshift, eaddr);
    *protp = prot;
    *psizep = apshift;
    return true;
}

void ppc_hash64_tlb_flush_hpte(PowerPCCPU *cpu, target_ulong ptex,
                               target_ulong pte0, target_ulong pte1)
{
    /*
     * XXX: given the fact that there are too many segments to
     * invalidate, and we still don't have a tlb_flush_mask(env, n,
     * mask) in QEMU, we just invalidate all TLBs
     */
    cpu->env.tlb_need_flush = TLB_NEED_GLOBAL_FLUSH | TLB_NEED_LOCAL_FLUSH;
}

#ifdef CONFIG_TCG
void helper_store_lpcr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);

    ppc_store_lpcr(cpu, val);
}
#endif

void ppc_hash64_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    if (!pcc->hash64_opts) {
        assert(!mmu_is_64bit(env->mmu_model));
        return;
    }

    cpu->hash64_opts = g_memdup2(pcc->hash64_opts, sizeof(*cpu->hash64_opts));
}

void ppc_hash64_finalize(PowerPCCPU *cpu)
{
    g_free(cpu->hash64_opts);
}

const PPCHash64Options ppc_hash64_opts_basic = {
    .flags = 0,
    .slb_size = 64,
    .sps = {
        { .page_shift = 12, /* 4K */
          .slb_enc = 0,
          .enc = { { .page_shift = 12, .pte_enc = 0 } }
        },
        { .page_shift = 24, /* 16M */
          .slb_enc = 0x100,
          .enc = { { .page_shift = 24, .pte_enc = 0 } }
        },
    },
};

const PPCHash64Options ppc_hash64_opts_POWER7 = {
    .flags = PPC_HASH64_1TSEG | PPC_HASH64_AMR | PPC_HASH64_CI_LARGEPAGE,
    .slb_size = 32,
    .sps = {
        {
            .page_shift = 12, /* 4K */
            .slb_enc = 0,
            .enc = { { .page_shift = 12, .pte_enc = 0 },
                     { .page_shift = 16, .pte_enc = 0x7 },
                     { .page_shift = 24, .pte_enc = 0x38 }, },
        },
        {
            .page_shift = 16, /* 64K */
            .slb_enc = SLB_VSID_64K,
            .enc = { { .page_shift = 16, .pte_enc = 0x1 },
                     { .page_shift = 24, .pte_enc = 0x8 }, },
        },
        {
            .page_shift = 24, /* 16M */
            .slb_enc = SLB_VSID_16M,
            .enc = { { .page_shift = 24, .pte_enc = 0 }, },
        },
        {
            .page_shift = 34, /* 16G */
            .slb_enc = SLB_VSID_16G,
            .enc = { { .page_shift = 34, .pte_enc = 0x3 }, },
        },
    }
};


