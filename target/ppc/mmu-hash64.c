/*
 *  PowerPC MMU, TLB, SLB and BAT emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *  Copyright (c) 2013 David Gibson, IBM Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "sysemu/hw_accel.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "exec/log.h"
#include "hw/hw.h"
#include "mmu-book3s-v3.h"

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

void helper_slbia(CPUPPCState *env, uint32_t ih)
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

void helper_slbie(CPUPPCState *env, target_ulong addr)
{
    __helper_slbie(env, addr, false);
}

void helper_slbieg(CPUPPCState *env, target_ulong addr)
{
    __helper_slbie(env, addr, true);
}

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

void helper_store_slb(CPUPPCState *env, target_ulong rb, target_ulong rs)
{
    PowerPCCPU *cpu = env_archcpu(env);

    if (ppc_store_slb(cpu, rb & 0xfff, rb & ~0xfffULL, rs) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
}

target_ulong helper_load_slb_esid(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_esid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

target_ulong helper_find_slb_vsid(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_find_slb_vsid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

target_ulong helper_load_slb_vsid(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = env_archcpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_vsid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

/* Check No-Execute or Guarded Storage */
static inline int ppc_hash64_pte_noexec_guard(PowerPCCPU *cpu,
                                              ppc_hash_pte64_t pte)
{
    /* Exec permissions CANNOT take away read or write permissions */
    return (pte.pte1 & HPTE64_R_N) || (pte.pte1 & HPTE64_R_G) ?
            PAGE_READ | PAGE_WRITE : PAGE_READ | PAGE_WRITE | PAGE_EXEC;
}

/* Check Basic Storage Protection */
static int ppc_hash64_pte_prot(PowerPCCPU *cpu,
                               ppc_slb_t *slb, ppc_hash_pte64_t pte)
{
    CPUPPCState *env = &cpu->env;
    unsigned pp, key;
    /*
     * Some pp bit combinations have undefined behaviour, so default
     * to no access in those cases
     */
    int prot = 0;

    key = !!(msr_pr ? (slb->vsid & SLB_VSID_KP)
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

const ppc_hash_pte64_t *ppc_hash64_map_hptes(PowerPCCPU *cpu,
                                             hwaddr ptex, int n)
{
    hwaddr pte_offset = ptex * HASH_PTE_SIZE_64;
    hwaddr base;
    hwaddr plen = n * HASH_PTE_SIZE_64;
    const ppc_hash_pte64_t *hptes;

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        return vhc->map_hptes(cpu->vhyp, ptex, n);
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
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->unmap_hptes(cpu->vhyp, hptes, ptex, n);
        return;
    }

    address_space_unmap(CPU(cpu)->as, (void *)hptes, n * HASH_PTE_SIZE_64,
                        false, n * HASH_PTE_SIZE_64);
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
            "htab_base " TARGET_FMT_plx " htab_mask " TARGET_FMT_plx
            " hash " TARGET_FMT_plx "\n",
            ppc_hash64_hpt_base(cpu), ppc_hash64_hpt_mask(cpu), hash);

    /* Primary PTEG lookup */
    qemu_log_mask(CPU_LOG_MMU,
            "0 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
            " vsid=" TARGET_FMT_lx " ptem=" TARGET_FMT_lx
            " hash=" TARGET_FMT_plx "\n",
            ppc_hash64_hpt_base(cpu), ppc_hash64_hpt_mask(cpu),
            vsid, ptem,  hash);
    ptex = ppc_hash64_pteg_search(cpu, hash, sps, ptem, pte, pshift);

    if (ptex == -1) {
        /* Secondary PTEG lookup */
        ptem |= HPTE64_V_SECONDARY;
        qemu_log_mask(CPU_LOG_MMU,
                "1 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                " vsid=" TARGET_FMT_lx " api=" TARGET_FMT_lx
                " hash=" TARGET_FMT_plx "\n", ppc_hash64_hpt_base(cpu),
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

static void ppc_hash64_set_isi(CPUState *cs, uint64_t error_code)
{
    CPUPPCState *env = &POWERPC_CPU(cs)->env;
    bool vpm;

    if (msr_ir) {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM1);
    } else {
        vpm = ppc_hash64_use_vrma(env);
    }
    if (vpm && !msr_hv) {
        cs->exception_index = POWERPC_EXCP_HISI;
    } else {
        cs->exception_index = POWERPC_EXCP_ISI;
    }
    env->error_code = error_code;
}

static void ppc_hash64_set_dsi(CPUState *cs, uint64_t dar, uint64_t dsisr)
{
    CPUPPCState *env = &POWERPC_CPU(cs)->env;
    bool vpm;

    if (msr_dr) {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM1);
    } else {
        vpm = ppc_hash64_use_vrma(env);
    }
    if (vpm && !msr_hv) {
        cs->exception_index = POWERPC_EXCP_HDSI;
        env->spr[SPR_HDAR] = dar;
        env->spr[SPR_HDSISR] = dsisr;
    } else {
        cs->exception_index = POWERPC_EXCP_DSI;
        env->spr[SPR_DAR] = dar;
        env->spr[SPR_DSISR] = dsisr;
   }
    env->error_code = 0;
}


static void ppc_hash64_set_r(PowerPCCPU *cpu, hwaddr ptex, uint64_t pte1)
{
    hwaddr base, offset = ptex * HASH_PTE_SIZE_64 + 16;

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->hpte_set_r(cpu->vhyp, ptex, pte1);
        return;
    }
    base = ppc_hash64_hpt_base(cpu);


    /* The HW performs a non-atomic byte update */
    stb_phys(CPU(cpu)->as, base + offset, ((pte1 >> 8) & 0xff) | 0x01);
}

static void ppc_hash64_set_c(PowerPCCPU *cpu, hwaddr ptex, uint64_t pte1)
{
    hwaddr base, offset = ptex * HASH_PTE_SIZE_64 + 15;

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->hpte_set_c(cpu->vhyp, ptex, pte1);
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

static int build_vrma_slbe(PowerPCCPU *cpu, ppc_slb_t *slb)
{
    CPUPPCState *env = &cpu->env;
    target_ulong lpcr = env->spr[SPR_LPCR];
    uint32_t vrmasd = (lpcr & LPCR_VRMASD) >> LPCR_VRMASD_SHIFT;
    target_ulong vsid = SLB_VSID_VRMA | ((vrmasd << 4) & SLB_VSID_LLP_MASK);
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

    error_report("Bad page size encoding in LPCR[VRMASD]; LPCR=0x"
                 TARGET_FMT_lx"\n", lpcr);

    return -1;
}

int ppc_hash64_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr,
                                int rwx, int mmu_idx)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    ppc_slb_t vrma_slbe;
    ppc_slb_t *slb;
    unsigned apshift;
    hwaddr ptex;
    ppc_hash_pte64_t pte;
    int exec_prot, pp_prot, amr_prot, prot;
    const int need_prot[] = {PAGE_READ, PAGE_WRITE, PAGE_EXEC};
    hwaddr raddr;

    assert((rwx == 0) || (rwx == 1) || (rwx == 2));

    /*
     * Note on LPCR usage: 970 uses HID4, but our special variant of
     * store_spr copies relevant fields into env->spr[SPR_LPCR].
     * Similarily we filter unimplemented bits when storing into LPCR
     * depending on the MMU version. This code can thus just use the
     * LPCR "as-is".
     */

    /* 1. Handle real mode accesses */
    if (((rwx == 2) && (msr_ir == 0)) || ((rwx != 2) && (msr_dr == 0))) {
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
        } else if (msr_hv || !env->has_hv_mode) {
            /* In HV mode, add HRMOR if top EA bit is clear */
            if (!(eaddr >> 63)) {
                raddr |= env->spr[SPR_HRMOR];
            }
        } else if (ppc_hash64_use_vrma(env)) {
            /* Emulated VRMA mode */
            slb = &vrma_slbe;
            if (build_vrma_slbe(cpu, slb) != 0) {
                /* Invalid VRMA setup, machine check */
                cs->exception_index = POWERPC_EXCP_MCHECK;
                env->error_code = 0;
                return 1;
            }

            goto skip_slb_search;
        } else {
            target_ulong limit = rmls_limit(cpu);

            /* Emulated old-style RMO mode, bounds check against RMLS */
            if (raddr >= limit) {
                if (rwx == 2) {
                    ppc_hash64_set_isi(cs, SRR1_PROTFAULT);
                } else {
                    int dsisr = DSISR_PROTFAULT;
                    if (rwx == 1) {
                        dsisr |= DSISR_ISSTORE;
                    }
                    ppc_hash64_set_dsi(cs, eaddr, dsisr);
                }
                return 1;
            }

            raddr |= env->spr[SPR_RMOR];
        }
        tlb_set_page(cs, eaddr & TARGET_PAGE_MASK, raddr & TARGET_PAGE_MASK,
                     PAGE_READ | PAGE_WRITE | PAGE_EXEC, mmu_idx,
                     TARGET_PAGE_SIZE);
        return 0;
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
        if (rwx == 2) {
            cs->exception_index = POWERPC_EXCP_ISEG;
            env->error_code = 0;
        } else {
            cs->exception_index = POWERPC_EXCP_DSEG;
            env->error_code = 0;
            env->spr[SPR_DAR] = eaddr;
        }
        return 1;
    }

skip_slb_search:

    /* 3. Check for segment level no-execute violation */
    if ((rwx == 2) && (slb->vsid & SLB_VSID_N)) {
        ppc_hash64_set_isi(cs, SRR1_NOEXEC_GUARD);
        return 1;
    }

    /* 4. Locate the PTE in the hash table */
    ptex = ppc_hash64_htab_lookup(cpu, slb, eaddr, &pte, &apshift);
    if (ptex == -1) {
        if (rwx == 2) {
            ppc_hash64_set_isi(cs, SRR1_NOPTE);
        } else {
            int dsisr = DSISR_NOPTE;
            if (rwx == 1) {
                dsisr |= DSISR_ISSTORE;
            }
            ppc_hash64_set_dsi(cs, eaddr, dsisr);
        }
        return 1;
    }
    qemu_log_mask(CPU_LOG_MMU,
                  "found PTE at index %08" HWADDR_PRIx "\n", ptex);

    /* 5. Check access permissions */

    exec_prot = ppc_hash64_pte_noexec_guard(cpu, pte);
    pp_prot = ppc_hash64_pte_prot(cpu, slb, pte);
    amr_prot = ppc_hash64_amr_prot(cpu, pte);
    prot = exec_prot & pp_prot & amr_prot;

    if ((need_prot[rwx] & ~prot) != 0) {
        /* Access right violation */
        qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
        if (rwx == 2) {
            int srr1 = 0;
            if (PAGE_EXEC & ~exec_prot) {
                srr1 |= SRR1_NOEXEC_GUARD; /* Access violates noexec or guard */
            } else if (PAGE_EXEC & ~pp_prot) {
                srr1 |= SRR1_PROTFAULT; /* Access violates access authority */
            }
            if (PAGE_EXEC & ~amr_prot) {
                srr1 |= SRR1_IAMR; /* Access violates virt pg class key prot */
            }
            ppc_hash64_set_isi(cs, srr1);
        } else {
            int dsisr = 0;
            if (need_prot[rwx] & ~pp_prot) {
                dsisr |= DSISR_PROTFAULT;
            }
            if (rwx == 1) {
                dsisr |= DSISR_ISSTORE;
            }
            if (need_prot[rwx] & ~amr_prot) {
                dsisr |= DSISR_AMR;
            }
            ppc_hash64_set_dsi(cs, eaddr, dsisr);
        }
        return 1;
    }

    qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");

    /* 6. Update PTE referenced and changed bits if necessary */

    if (!(pte.pte1 & HPTE64_R_R)) {
        ppc_hash64_set_r(cpu, ptex, pte.pte1);
    }
    if (!(pte.pte1 & HPTE64_R_C)) {
        if (rwx == 1) {
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

    raddr = deposit64(pte.pte1 & HPTE64_R_RPN, 0, apshift, eaddr);

    tlb_set_page(cs, eaddr & TARGET_PAGE_MASK, raddr & TARGET_PAGE_MASK,
                 prot, mmu_idx, 1ULL << apshift);

    return 0;
}

hwaddr ppc_hash64_get_phys_page_debug(PowerPCCPU *cpu, target_ulong addr)
{
    CPUPPCState *env = &cpu->env;
    ppc_slb_t vrma_slbe;
    ppc_slb_t *slb;
    hwaddr ptex, raddr;
    ppc_hash_pte64_t pte;
    unsigned apshift;

    /* Handle real mode */
    if (msr_dr == 0) {
        /* In real mode the top 4 effective address bits are ignored */
        raddr = addr & 0x0FFFFFFFFFFFFFFFULL;

        if (cpu->vhyp) {
            /*
             * In virtual hypervisor mode, there's nothing to do:
             *   EA == GPA == qemu guest address
             */
            return raddr;
        } else if ((msr_hv || !env->has_hv_mode) && !(addr >> 63)) {
            /* In HV mode, add HRMOR if top EA bit is clear */
            return raddr | env->spr[SPR_HRMOR];
        } else if (ppc_hash64_use_vrma(env)) {
            /* Emulated VRMA mode */
            slb = &vrma_slbe;
            if (build_vrma_slbe(cpu, slb) != 0) {
                return -1;
            }
        } else {
            target_ulong limit = rmls_limit(cpu);

            /* Emulated old-style RMO mode, bounds check against RMLS */
            if (raddr >= limit) {
                return -1;
            }
            return raddr | env->spr[SPR_RMOR];
        }
    } else {
        slb = slb_lookup(cpu, addr);
        if (!slb) {
            return -1;
        }
    }

    ptex = ppc_hash64_htab_lookup(cpu, slb, addr, &pte, &apshift);
    if (ptex == -1) {
        return -1;
    }

    return deposit64(pte.pte1 & HPTE64_R_RPN, 0, apshift, addr)
        & TARGET_PAGE_MASK;
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

void ppc_store_lpcr(PowerPCCPU *cpu, target_ulong val)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;

    env->spr[SPR_LPCR] = val & pcc->lpcr_mask;
}

void helper_store_lpcr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);

    ppc_store_lpcr(cpu, val);
}

void ppc_hash64_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    if (!pcc->hash64_opts) {
        assert(!(env->mmu_model & POWERPC_MMU_64));
        return;
    }

    cpu->hash64_opts = g_memdup(pcc->hash64_opts, sizeof(*cpu->hash64_opts));
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

void ppc_hash64_filter_pagesizes(PowerPCCPU *cpu,
                                 bool (*cb)(void *, uint32_t, uint32_t),
                                 void *opaque)
{
    PPCHash64Options *opts = cpu->hash64_opts;
    int i;
    int n = 0;
    bool ci_largepage = false;

    assert(opts);

    n = 0;
    for (i = 0; i < ARRAY_SIZE(opts->sps); i++) {
        PPCHash64SegmentPageSizes *sps = &opts->sps[i];
        int j;
        int m = 0;

        assert(n <= i);

        if (!sps->page_shift) {
            break;
        }

        for (j = 0; j < ARRAY_SIZE(sps->enc); j++) {
            PPCHash64PageSize *ps = &sps->enc[j];

            assert(m <= j);
            if (!ps->page_shift) {
                break;
            }

            if (cb(opaque, sps->page_shift, ps->page_shift)) {
                if (ps->page_shift >= 16) {
                    ci_largepage = true;
                }
                sps->enc[m++] = *ps;
            }
        }

        /* Clear rest of the row */
        for (j = m; j < ARRAY_SIZE(sps->enc); j++) {
            memset(&sps->enc[j], 0, sizeof(sps->enc[j]));
        }

        if (m) {
            n++;
        }
    }

    /* Clear the rest of the table */
    for (i = n; i < ARRAY_SIZE(opts->sps); i++) {
        memset(&opts->sps[i], 0, sizeof(opts->sps[i]));
    }

    if (!ci_largepage) {
        opts->flags &= ~PPC_HASH64_CI_LARGEPAGE;
    }
}
