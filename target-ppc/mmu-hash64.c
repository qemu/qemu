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
#include "qapi/error.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "exec/log.h"

//#define DEBUG_SLB

#ifdef DEBUG_SLB
#  define LOG_SLB(...) qemu_log_mask(CPU_LOG_MMU, __VA_ARGS__)
#else
#  define LOG_SLB(...) do { } while (0)
#endif

/*
 * Used to indicate that a CPU has its hash page table (HPT) managed
 * within the host kernel
 */
#define MMU_HASH64_KVM_MANAGED_HPT      ((void *)-1)

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

    for (n = 0; n < env->slb_nr; n++) {
        ppc_slb_t *slb = &env->slb[n];

        LOG_SLB("%s: slot %d %016" PRIx64 " %016"
                    PRIx64 "\n", __func__, n, slb->esid, slb->vsid);
        /* We check for 1T matches on all MMUs here - if the MMU
         * doesn't have 1T segment support, we will have prevented 1T
         * entries from being inserted in the slbmte code. */
        if (((slb->esid == esid_256M) &&
             ((slb->vsid & SLB_VSID_B) == SLB_VSID_B_256M))
            || ((slb->esid == esid_1T) &&
                ((slb->vsid & SLB_VSID_B) == SLB_VSID_B_1T))) {
            return slb;
        }
    }

    return NULL;
}

void dump_slb(FILE *f, fprintf_function cpu_fprintf, PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    int i;
    uint64_t slbe, slbv;

    cpu_synchronize_state(CPU(cpu));

    cpu_fprintf(f, "SLB\tESID\t\t\tVSID\n");
    for (i = 0; i < env->slb_nr; i++) {
        slbe = env->slb[i].esid;
        slbv = env->slb[i].vsid;
        if (slbe == 0 && slbv == 0) {
            continue;
        }
        cpu_fprintf(f, "%d\t0x%016" PRIx64 "\t0x%016" PRIx64 "\n",
                    i, slbe, slbv);
    }
}

void helper_slbia(CPUPPCState *env)
{
    int n;

    /* XXX: Warning: slbia never invalidates the first segment */
    for (n = 1; n < env->slb_nr; n++) {
        ppc_slb_t *slb = &env->slb[n];

        if (slb->esid & SLB_ESID_V) {
            slb->esid &= ~SLB_ESID_V;
            /* XXX: given the fact that segment size is 256 MB or 1TB,
             *      and we still don't have a tlb_flush_mask(env, n, mask)
             *      in QEMU, we just invalidate all TLBs
             */
            env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;
        }
    }
}

void helper_slbie(CPUPPCState *env, target_ulong addr)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    ppc_slb_t *slb;

    slb = slb_lookup(cpu, addr);
    if (!slb) {
        return;
    }

    if (slb->esid & SLB_ESID_V) {
        slb->esid &= ~SLB_ESID_V;

        /* XXX: given the fact that segment size is 256 MB or 1TB,
         *      and we still don't have a tlb_flush_mask(env, n, mask)
         *      in QEMU, we just invalidate all TLBs
         */
        env->tlb_need_flush |= TLB_NEED_LOCAL_FLUSH;
    }
}

int ppc_store_slb(PowerPCCPU *cpu, target_ulong slot,
                  target_ulong esid, target_ulong vsid)
{
    CPUPPCState *env = &cpu->env;
    ppc_slb_t *slb = &env->slb[slot];
    const struct ppc_one_seg_page_size *sps = NULL;
    int i;

    if (slot >= env->slb_nr) {
        return -1; /* Bad slot number */
    }
    if (esid & ~(SLB_ESID_ESID | SLB_ESID_V)) {
        return -1; /* Reserved bits set */
    }
    if (vsid & (SLB_VSID_B & ~SLB_VSID_B_1T)) {
        return -1; /* Bad segment size */
    }
    if ((vsid & SLB_VSID_B) && !(env->mmu_model & POWERPC_MMU_1TSEG)) {
        return -1; /* 1T segment on MMU that doesn't support it */
    }

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const struct ppc_one_seg_page_size *sps1 = &env->sps.sps[i];

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

    LOG_SLB("%s: %d " TARGET_FMT_lx " - " TARGET_FMT_lx " => %016" PRIx64
            " %016" PRIx64 "\n", __func__, slot, esid, vsid,
            slb->esid, slb->vsid);

    return 0;
}

static int ppc_load_slb_esid(PowerPCCPU *cpu, target_ulong rb,
                             target_ulong *rt)
{
    CPUPPCState *env = &cpu->env;
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (slot >= env->slb_nr) {
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

    if (slot >= env->slb_nr) {
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
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    if (ppc_store_slb(cpu, rb & 0xfff, rb & ~0xfffULL, rs) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
}

target_ulong helper_load_slb_esid(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_esid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

target_ulong helper_find_slb_vsid(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    target_ulong rt = 0;

    if (ppc_find_slb_vsid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

target_ulong helper_load_slb_vsid(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_vsid(cpu, rb, &rt) < 0) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL, GETPC());
    }
    return rt;
}

/*
 * 64-bit hash table MMU handling
 */
void ppc_hash64_set_sdr1(PowerPCCPU *cpu, target_ulong value,
                         Error **errp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong htabsize = value & SDR_64_HTABSIZE;

    env->spr[SPR_SDR1] = value;
    if (htabsize > 28) {
        error_setg(errp,
                   "Invalid HTABSIZE 0x" TARGET_FMT_lx" stored in SDR1",
                   htabsize);
        htabsize = 28;
    }
    env->htab_mask = (1ULL << (htabsize + 18 - 7)) - 1;
    env->htab_base = value & SDR_64_HTABORG;
}

void ppc_hash64_set_external_hpt(PowerPCCPU *cpu, void *hpt, int shift,
                                 Error **errp)
{
    CPUPPCState *env = &cpu->env;
    Error *local_err = NULL;

    if (hpt) {
        env->external_htab = hpt;
    } else {
        env->external_htab = MMU_HASH64_KVM_MANAGED_HPT;
    }
    ppc_hash64_set_sdr1(cpu, (target_ulong)(uintptr_t)hpt | (shift - 18),
                        &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Not strictly necessary, but makes it clearer that an external
     * htab is in use when debugging */
    env->htab_base = -1;

    if (kvm_enabled()) {
        if (kvmppc_put_books_sregs(cpu) < 0) {
            error_setg(errp, "Unable to update SDR1 in KVM");
        }
    }
}

static int ppc_hash64_pte_prot(PowerPCCPU *cpu,
                               ppc_slb_t *slb, ppc_hash_pte64_t pte)
{
    CPUPPCState *env = &cpu->env;
    unsigned pp, key;
    /* Some pp bit combinations have undefined behaviour, so default
     * to no access in those cases */
    int prot = 0;

    key = !!(msr_pr ? (slb->vsid & SLB_VSID_KP)
             : (slb->vsid & SLB_VSID_KS));
    pp = (pte.pte1 & HPTE64_R_PP) | ((pte.pte1 & HPTE64_R_PP0) >> 61);

    if (key == 0) {
        switch (pp) {
        case 0x0:
        case 0x1:
        case 0x2:
            prot = PAGE_READ | PAGE_WRITE;
            break;

        case 0x3:
        case 0x6:
            prot = PAGE_READ;
            break;
        }
    } else {
        switch (pp) {
        case 0x0:
        case 0x6:
            prot = 0;
            break;

        case 0x1:
        case 0x3:
            prot = PAGE_READ;
            break;

        case 0x2:
            prot = PAGE_READ | PAGE_WRITE;
            break;
        }
    }

    /* No execute if either noexec or guarded bits set */
    if (!(pte.pte1 & HPTE64_R_N) || (pte.pte1 & HPTE64_R_G)
        || (slb->vsid & SLB_VSID_N)) {
        prot |= PAGE_EXEC;
    }

    return prot;
}

static int ppc_hash64_amr_prot(PowerPCCPU *cpu, ppc_hash_pte64_t pte)
{
    CPUPPCState *env = &cpu->env;
    int key, amrbits;
    int prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    /* Only recent MMUs implement Virtual Page Class Key Protection */
    if (!(env->mmu_model & POWERPC_MMU_AMR)) {
        return prot;
    }

    key = HPTE64_R_KEY(pte.pte1);
    amrbits = (env->spr[SPR_AMR] >> 2*(31 - key)) & 0x3;

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

    return prot;
}

uint64_t ppc_hash64_start_access(PowerPCCPU *cpu, target_ulong pte_index)
{
    uint64_t token = 0;
    hwaddr pte_offset;

    pte_offset = pte_index * HASH_PTE_SIZE_64;
    if (cpu->env.external_htab == MMU_HASH64_KVM_MANAGED_HPT) {
        /*
         * HTAB is controlled by KVM. Fetch the PTEG into a new buffer.
         */
        token = kvmppc_hash64_read_pteg(cpu, pte_index);
    } else if (cpu->env.external_htab) {
        /*
         * HTAB is controlled by QEMU. Just point to the internally
         * accessible PTEG.
         */
        token = (uint64_t)(uintptr_t) cpu->env.external_htab + pte_offset;
    } else if (cpu->env.htab_base) {
        token = cpu->env.htab_base + pte_offset;
    }
    return token;
}

void ppc_hash64_stop_access(PowerPCCPU *cpu, uint64_t token)
{
    if (cpu->env.external_htab == MMU_HASH64_KVM_MANAGED_HPT) {
        kvmppc_hash64_free_pteg(token);
    }
}

static unsigned hpte_page_shift(const struct ppc_one_seg_page_size *sps,
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
        const struct ppc_one_page_size *ps = &sps->enc[i];
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

static hwaddr ppc_hash64_pteg_search(PowerPCCPU *cpu, hwaddr hash,
                                     const struct ppc_one_seg_page_size *sps,
                                     target_ulong ptem,
                                     ppc_hash_pte64_t *pte, unsigned *pshift)
{
    CPUPPCState *env = &cpu->env;
    int i;
    uint64_t token;
    target_ulong pte0, pte1;
    target_ulong pte_index;

    pte_index = (hash & env->htab_mask) * HPTES_PER_GROUP;
    token = ppc_hash64_start_access(cpu, pte_index);
    if (!token) {
        return -1;
    }
    for (i = 0; i < HPTES_PER_GROUP; i++) {
        pte0 = ppc_hash64_load_hpte0(cpu, token, i);
        pte1 = ppc_hash64_load_hpte1(cpu, token, i);

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
            /* We don't do anything with pshift yet as qemu TLB only deals
             * with 4K pages anyway
             */
            pte->pte0 = pte0;
            pte->pte1 = pte1;
            ppc_hash64_stop_access(cpu, token);
            return (pte_index + i) * HASH_PTE_SIZE_64;
        }
    }
    ppc_hash64_stop_access(cpu, token);
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
    hwaddr pte_offset;
    hwaddr hash;
    uint64_t vsid, epnmask, epn, ptem;
    const struct ppc_one_seg_page_size *sps = slb->sps;

    /* The SLB store path should prevent any bad page size encodings
     * getting in there, so: */
    assert(sps);

    /* If ISL is set in LPCR we need to clamp the page size to 4K */
    if (env->spr[SPR_LPCR] & LPCR_ISL) {
        /* We assume that when using TCG, 4k is first entry of SPS */
        sps = &env->sps.sps[0];
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
            env->htab_base, env->htab_mask, hash);

    /* Primary PTEG lookup */
    qemu_log_mask(CPU_LOG_MMU,
            "0 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
            " vsid=" TARGET_FMT_lx " ptem=" TARGET_FMT_lx
            " hash=" TARGET_FMT_plx "\n",
            env->htab_base, env->htab_mask, vsid, ptem,  hash);
    pte_offset = ppc_hash64_pteg_search(cpu, hash, sps, ptem, pte, pshift);

    if (pte_offset == -1) {
        /* Secondary PTEG lookup */
        ptem |= HPTE64_V_SECONDARY;
        qemu_log_mask(CPU_LOG_MMU,
                "1 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                " vsid=" TARGET_FMT_lx " api=" TARGET_FMT_lx
                " hash=" TARGET_FMT_plx "\n", env->htab_base,
                env->htab_mask, vsid, ptem, ~hash);

        pte_offset = ppc_hash64_pteg_search(cpu, ~hash, sps, ptem, pte, pshift);
    }

    return pte_offset;
}

unsigned ppc_hash64_hpte_page_shift_noslb(PowerPCCPU *cpu,
                                          uint64_t pte0, uint64_t pte1)
{
    CPUPPCState *env = &cpu->env;
    int i;

    if (!(pte0 & HPTE64_V_LARGE)) {
        return 12;
    }

    /*
     * The encodings in env->sps need to be carefully chosen so that
     * this gives an unambiguous result.
     */
    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const struct ppc_one_seg_page_size *sps = &env->sps.sps[i];
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

static void ppc_hash64_set_isi(CPUState *cs, CPUPPCState *env,
                               uint64_t error_code)
{
    bool vpm;

    if (msr_ir) {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM1);
    } else {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM0);
    }
    if (vpm && !msr_hv) {
        cs->exception_index = POWERPC_EXCP_HISI;
    } else {
        cs->exception_index = POWERPC_EXCP_ISI;
    }
    env->error_code = error_code;
}

static void ppc_hash64_set_dsi(CPUState *cs, CPUPPCState *env, uint64_t dar,
                               uint64_t dsisr)
{
    bool vpm;

    if (msr_dr) {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM1);
    } else {
        vpm = !!(env->spr[SPR_LPCR] & LPCR_VPM0);
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


int ppc_hash64_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr,
                                int rwx, int mmu_idx)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    ppc_slb_t *slb;
    unsigned apshift;
    hwaddr pte_offset;
    ppc_hash_pte64_t pte;
    int pp_prot, amr_prot, prot;
    uint64_t new_pte1, dsisr;
    const int need_prot[] = {PAGE_READ, PAGE_WRITE, PAGE_EXEC};
    hwaddr raddr;

    assert((rwx == 0) || (rwx == 1) || (rwx == 2));

    /* Note on LPCR usage: 970 uses HID4, but our special variant
     * of store_spr copies relevant fields into env->spr[SPR_LPCR].
     * Similarily we filter unimplemented bits when storing into
     * LPCR depending on the MMU version. This code can thus just
     * use the LPCR "as-is".
     */

    /* 1. Handle real mode accesses */
    if (((rwx == 2) && (msr_ir == 0)) || ((rwx != 2) && (msr_dr == 0))) {
        /* Translation is supposedly "off"  */
        /* In real mode the top 4 effective address bits are (mostly) ignored */
        raddr = eaddr & 0x0FFFFFFFFFFFFFFFULL;

        /* In HV mode, add HRMOR if top EA bit is clear */
        if (msr_hv || !env->has_hv_mode) {
            if (!(eaddr >> 63)) {
                raddr |= env->spr[SPR_HRMOR];
            }
        } else {
            /* Otherwise, check VPM for RMA vs VRMA */
            if (env->spr[SPR_LPCR] & LPCR_VPM0) {
                slb = &env->vrma_slb;
                if (slb->sps) {
                    goto skip_slb_search;
                }
                /* Not much else to do here */
                cs->exception_index = POWERPC_EXCP_MCHECK;
                env->error_code = 0;
                return 1;
            } else if (raddr < env->rmls) {
                /* RMA. Check bounds in RMLS */
                raddr |= env->spr[SPR_RMOR];
            } else {
                /* The access failed, generate the approriate interrupt */
                if (rwx == 2) {
                    ppc_hash64_set_isi(cs, env, 0x08000000);
                } else {
                    dsisr = 0x08000000;
                    if (rwx == 1) {
                        dsisr |= 0x02000000;
                    }
                    ppc_hash64_set_dsi(cs, env, eaddr, dsisr);
                }
                return 1;
            }
        }
        tlb_set_page(cs, eaddr & TARGET_PAGE_MASK, raddr & TARGET_PAGE_MASK,
                     PAGE_READ | PAGE_WRITE | PAGE_EXEC, mmu_idx,
                     TARGET_PAGE_SIZE);
        return 0;
    }

    /* 2. Translation is on, so look up the SLB */
    slb = slb_lookup(cpu, eaddr);
    if (!slb) {
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
        ppc_hash64_set_isi(cs, env, 0x10000000);
        return 1;
    }

    /* 4. Locate the PTE in the hash table */
    pte_offset = ppc_hash64_htab_lookup(cpu, slb, eaddr, &pte, &apshift);
    if (pte_offset == -1) {
        dsisr = 0x40000000;
        if (rwx == 2) {
            ppc_hash64_set_isi(cs, env, dsisr);
        } else {
            if (rwx == 1) {
                dsisr |= 0x02000000;
            }
            ppc_hash64_set_dsi(cs, env, eaddr, dsisr);
        }
        return 1;
    }
    qemu_log_mask(CPU_LOG_MMU,
                "found PTE at offset %08" HWADDR_PRIx "\n", pte_offset);

    /* 5. Check access permissions */

    pp_prot = ppc_hash64_pte_prot(cpu, slb, pte);
    amr_prot = ppc_hash64_amr_prot(cpu, pte);
    prot = pp_prot & amr_prot;

    if ((need_prot[rwx] & ~prot) != 0) {
        /* Access right violation */
        qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
        if (rwx == 2) {
            ppc_hash64_set_isi(cs, env, 0x08000000);
        } else {
            dsisr = 0;
            if (need_prot[rwx] & ~pp_prot) {
                dsisr |= 0x08000000;
            }
            if (rwx == 1) {
                dsisr |= 0x02000000;
            }
            if (need_prot[rwx] & ~amr_prot) {
                dsisr |= 0x00200000;
            }
            ppc_hash64_set_dsi(cs, env, eaddr, dsisr);
        }
        return 1;
    }

    qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");

    /* 6. Update PTE referenced and changed bits if necessary */

    new_pte1 = pte.pte1 | HPTE64_R_R; /* set referenced bit */
    if (rwx == 1) {
        new_pte1 |= HPTE64_R_C; /* set changed (dirty) bit */
    } else {
        /* Treat the page as read-only for now, so that a later write
         * will pass through this function again to set the C bit */
        prot &= ~PAGE_WRITE;
    }

    if (new_pte1 != pte.pte1) {
        ppc_hash64_store_hpte(cpu, pte_offset / HASH_PTE_SIZE_64,
                              pte.pte0, new_pte1);
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
    ppc_slb_t *slb;
    hwaddr pte_offset, raddr;
    ppc_hash_pte64_t pte;
    unsigned apshift;

    /* Handle real mode */
    if (msr_dr == 0) {
        /* In real mode the top 4 effective address bits are ignored */
        raddr = addr & 0x0FFFFFFFFFFFFFFFULL;

        /* In HV mode, add HRMOR if top EA bit is clear */
        if ((msr_hv || !env->has_hv_mode) && !(addr >> 63)) {
            return raddr | env->spr[SPR_HRMOR];
        }

        /* Otherwise, check VPM for RMA vs VRMA */
        if (env->spr[SPR_LPCR] & LPCR_VPM0) {
            slb = &env->vrma_slb;
            if (!slb->sps) {
                return -1;
            }
        } else if (raddr < env->rmls) {
            /* RMA. Check bounds in RMLS */
            return raddr | env->spr[SPR_RMOR];
        } else {
            return -1;
        }
    } else {
        slb = slb_lookup(cpu, addr);
        if (!slb) {
            return -1;
        }
    }

    pte_offset = ppc_hash64_htab_lookup(cpu, slb, addr, &pte, &apshift);
    if (pte_offset == -1) {
        return -1;
    }

    return deposit64(pte.pte1 & HPTE64_R_RPN, 0, apshift, addr)
        & TARGET_PAGE_MASK;
}

void ppc_hash64_store_hpte(PowerPCCPU *cpu,
                           target_ulong pte_index,
                           target_ulong pte0, target_ulong pte1)
{
    CPUPPCState *env = &cpu->env;

    if (env->external_htab == MMU_HASH64_KVM_MANAGED_HPT) {
        kvmppc_hash64_write_pte(env, pte_index, pte0, pte1);
        return;
    }

    pte_index *= HASH_PTE_SIZE_64;
    if (env->external_htab) {
        stq_p(env->external_htab + pte_index, pte0);
        stq_p(env->external_htab + pte_index + HASH_PTE_SIZE_64 / 2, pte1);
    } else {
        stq_phys(CPU(cpu)->as, env->htab_base + pte_index, pte0);
        stq_phys(CPU(cpu)->as,
                 env->htab_base + pte_index + HASH_PTE_SIZE_64 / 2, pte1);
    }
}

void ppc_hash64_tlb_flush_hpte(PowerPCCPU *cpu,
                               target_ulong pte_index,
                               target_ulong pte0, target_ulong pte1)
{
    /*
     * XXX: given the fact that there are too many segments to
     * invalidate, and we still don't have a tlb_flush_mask(env, n,
     * mask) in QEMU, we just invalidate all TLBs
     */
    cpu->env.tlb_need_flush = TLB_NEED_GLOBAL_FLUSH | TLB_NEED_LOCAL_FLUSH;
}

void ppc_hash64_update_rmls(CPUPPCState *env)
{
    uint64_t lpcr = env->spr[SPR_LPCR];

    /*
     * This is the full 4 bits encoding of POWER8. Previous
     * CPUs only support a subset of these but the filtering
     * is done when writing LPCR
     */
    switch ((lpcr & LPCR_RMLS) >> LPCR_RMLS_SHIFT) {
    case 0x8: /* 32MB */
        env->rmls = 0x2000000ull;
        break;
    case 0x3: /* 64MB */
        env->rmls = 0x4000000ull;
        break;
    case 0x7: /* 128MB */
        env->rmls = 0x8000000ull;
        break;
    case 0x4: /* 256MB */
        env->rmls = 0x10000000ull;
        break;
    case 0x2: /* 1GB */
        env->rmls = 0x40000000ull;
        break;
    case 0x1: /* 16GB */
        env->rmls = 0x400000000ull;
        break;
    default:
        /* What to do here ??? */
        env->rmls = 0;
    }
}

void ppc_hash64_update_vrma(CPUPPCState *env)
{
    const struct ppc_one_seg_page_size *sps = NULL;
    target_ulong esid, vsid, lpcr;
    ppc_slb_t *slb = &env->vrma_slb;
    uint32_t vrmasd;
    int i;

    /* First clear it */
    slb->esid = slb->vsid = 0;
    slb->sps = NULL;

    /* Is VRMA enabled ? */
    lpcr = env->spr[SPR_LPCR];
    if (!(lpcr & LPCR_VPM0)) {
        return;
    }

    /* Make one up. Mostly ignore the ESID which will not be
     * needed for translation
     */
    vsid = SLB_VSID_VRMA;
    vrmasd = (lpcr & LPCR_VRMASD) >> LPCR_VRMASD_SHIFT;
    vsid |= (vrmasd << 4) & (SLB_VSID_L | SLB_VSID_LP);
    esid = SLB_ESID_V;

   for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        const struct ppc_one_seg_page_size *sps1 = &env->sps.sps[i];

        if (!sps1->page_shift) {
            break;
        }

        if ((vsid & SLB_VSID_LLP_MASK) == sps1->slb_enc) {
            sps = sps1;
            break;
        }
    }

    if (!sps) {
        error_report("Bad page size encoding esid 0x"TARGET_FMT_lx
                     " vsid 0x"TARGET_FMT_lx, esid, vsid);
        return;
    }

    slb->vsid = vsid;
    slb->esid = esid;
    slb->sps = sps;
}

void helper_store_lpcr(CPUPPCState *env, target_ulong val)
{
    uint64_t lpcr = 0;

    /* Filter out bits */
    switch (env->mmu_model) {
    case POWERPC_MMU_64B: /* 970 */
        if (val & 0x40) {
            lpcr |= LPCR_LPES0;
        }
        if (val & 0x8000000000000000ull) {
            lpcr |= LPCR_LPES1;
        }
        if (val & 0x20) {
            lpcr |= (0x4ull << LPCR_RMLS_SHIFT);
        }
        if (val & 0x4000000000000000ull) {
            lpcr |= (0x2ull << LPCR_RMLS_SHIFT);
        }
        if (val & 0x2000000000000000ull) {
            lpcr |= (0x1ull << LPCR_RMLS_SHIFT);
        }
        env->spr[SPR_RMOR] = ((lpcr >> 41) & 0xffffull) << 26;

        /* XXX We could also write LPID from HID4 here
         * but since we don't tag any translation on it
         * it doesn't actually matter
         */
        /* XXX For proper emulation of 970 we also need
         * to dig HRMOR out of HID5
         */
        break;
    case POWERPC_MMU_2_03: /* P5p */
        lpcr = val & (LPCR_RMLS | LPCR_ILE |
                      LPCR_LPES0 | LPCR_LPES1 |
                      LPCR_RMI | LPCR_HDICE);
        break;
    case POWERPC_MMU_2_06: /* P7 */
        lpcr = val & (LPCR_VPM0 | LPCR_VPM1 | LPCR_ISL | LPCR_DPFD |
                      LPCR_VRMASD | LPCR_RMLS | LPCR_ILE |
                      LPCR_P7_PECE0 | LPCR_P7_PECE1 | LPCR_P7_PECE2 |
                      LPCR_MER | LPCR_TC |
                      LPCR_LPES0 | LPCR_LPES1 | LPCR_HDICE);
        break;
    case POWERPC_MMU_2_07: /* P8 */
        lpcr = val & (LPCR_VPM0 | LPCR_VPM1 | LPCR_ISL | LPCR_KBV |
                      LPCR_DPFD | LPCR_VRMASD | LPCR_RMLS | LPCR_ILE |
                      LPCR_AIL | LPCR_ONL | LPCR_P8_PECE0 | LPCR_P8_PECE1 |
                      LPCR_P8_PECE2 | LPCR_P8_PECE3 | LPCR_P8_PECE4 |
                      LPCR_MER | LPCR_TC | LPCR_LPES0 | LPCR_HDICE);
        break;
    default:
        ;
    }
    env->spr[SPR_LPCR] = lpcr;
    ppc_hash64_update_rmls(env);
    ppc_hash64_update_vrma(env);
}
