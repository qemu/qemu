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
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "qemu/error-report.h"
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
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    int n, do_invalidate;

    do_invalidate = 0;
    /* XXX: Warning: slbia never invalidates the first segment */
    for (n = 1; n < env->slb_nr; n++) {
        ppc_slb_t *slb = &env->slb[n];

        if (slb->esid & SLB_ESID_V) {
            slb->esid &= ~SLB_ESID_V;
            /* XXX: given the fact that segment size is 256 MB or 1TB,
             *      and we still don't have a tlb_flush_mask(env, n, mask)
             *      in QEMU, we just invalidate all TLBs
             */
            do_invalidate = 1;
        }
    }
    if (do_invalidate) {
        tlb_flush(CPU(cpu), 1);
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
        tlb_flush(CPU(cpu), 1);
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

void helper_store_slb(CPUPPCState *env, target_ulong rb, target_ulong rs)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    if (ppc_store_slb(cpu, rb & 0xfff, rb & ~0xfffULL, rs) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
    }
}

target_ulong helper_load_slb_esid(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_esid(cpu, rb, &rt) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
    }
    return rt;
}

target_ulong helper_load_slb_vsid(CPUPPCState *env, target_ulong rb)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    target_ulong rt = 0;

    if (ppc_load_slb_vsid(cpu, rb, &rt) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
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

    cpu_synchronize_state(CPU(cpu));

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

static hwaddr ppc_hash64_pteg_search(PowerPCCPU *cpu, hwaddr hash,
                                     bool secondary, target_ulong ptem,
                                     ppc_hash_pte64_t *pte)
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

        if ((pte0 & HPTE64_V_VALID)
            && (secondary == !!(pte0 & HPTE64_V_SECONDARY))
            && HPTE64_V_COMPARE(pte0, ptem)) {
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
                                     ppc_hash_pte64_t *pte)
{
    CPUPPCState *env = &cpu->env;
    hwaddr pte_offset;
    hwaddr hash;
    uint64_t vsid, epnmask, epn, ptem;

    /* The SLB store path should prevent any bad page size encodings
     * getting in there, so: */
    assert(slb->sps);

    epnmask = ~((1ULL << slb->sps->page_shift) - 1);

    if (slb->vsid & SLB_VSID_B) {
        /* 1TB segment */
        vsid = (slb->vsid & SLB_VSID_VSID) >> SLB_VSID_SHIFT_1T;
        epn = (eaddr & ~SEGMENT_MASK_1T) & epnmask;
        hash = vsid ^ (vsid << 25) ^ (epn >> slb->sps->page_shift);
    } else {
        /* 256M segment */
        vsid = (slb->vsid & SLB_VSID_VSID) >> SLB_VSID_SHIFT;
        epn = (eaddr & ~SEGMENT_MASK_256M) & epnmask;
        hash = vsid ^ (epn >> slb->sps->page_shift);
    }
    ptem = (slb->vsid & SLB_VSID_PTEM) | ((epn >> 16) & HPTE64_V_AVPN);

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
    pte_offset = ppc_hash64_pteg_search(cpu, hash, 0, ptem, pte);

    if (pte_offset == -1) {
        /* Secondary PTEG lookup */
        qemu_log_mask(CPU_LOG_MMU,
                "1 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                " vsid=" TARGET_FMT_lx " api=" TARGET_FMT_lx
                " hash=" TARGET_FMT_plx "\n", env->htab_base,
                env->htab_mask, vsid, ptem, ~hash);

        pte_offset = ppc_hash64_pteg_search(cpu, ~hash, 1, ptem, pte);
    }

    return pte_offset;
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

        if ((pte1 & mask) == (ps->pte_enc << HPTE64_R_RPN_SHIFT)) {
            return ps->page_shift;
        }
    }

    return 0; /* Bad page size encoding */
}

unsigned ppc_hash64_hpte_page_shift_noslb(PowerPCCPU *cpu,
                                          uint64_t pte0, uint64_t pte1,
                                          unsigned *seg_page_shift)
{
    CPUPPCState *env = &cpu->env;
    int i;

    if (!(pte0 & HPTE64_V_LARGE)) {
        *seg_page_shift = 12;
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
            *seg_page_shift = sps->page_shift;
            return shift;
        }
    }

    *seg_page_shift = 0;
    return 0;
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
    uint64_t new_pte1;
    const int need_prot[] = {PAGE_READ, PAGE_WRITE, PAGE_EXEC};
    hwaddr raddr;

    assert((rwx == 0) || (rwx == 1) || (rwx == 2));

    /* 1. Handle real mode accesses */
    if (((rwx == 2) && (msr_ir == 0)) || ((rwx != 2) && (msr_dr == 0))) {
        /* Translation is off */
        /* In real mode the top 4 effective address bits are ignored */
        raddr = eaddr & 0x0FFFFFFFFFFFFFFFULL;
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

    /* 3. Check for segment level no-execute violation */
    if ((rwx == 2) && (slb->vsid & SLB_VSID_N)) {
        cs->exception_index = POWERPC_EXCP_ISI;
        env->error_code = 0x10000000;
        return 1;
    }

    /* 4. Locate the PTE in the hash table */
    pte_offset = ppc_hash64_htab_lookup(cpu, slb, eaddr, &pte);
    if (pte_offset == -1) {
        if (rwx == 2) {
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x40000000;
        } else {
            cs->exception_index = POWERPC_EXCP_DSI;
            env->error_code = 0;
            env->spr[SPR_DAR] = eaddr;
            if (rwx == 1) {
                env->spr[SPR_DSISR] = 0x42000000;
            } else {
                env->spr[SPR_DSISR] = 0x40000000;
            }
        }
        return 1;
    }
    qemu_log_mask(CPU_LOG_MMU,
                "found PTE at offset %08" HWADDR_PRIx "\n", pte_offset);

    /* Validate page size encoding */
    apshift = hpte_page_shift(slb->sps, pte.pte0, pte.pte1);
    if (!apshift) {
        error_report("Bad page size encoding in HPTE 0x%"PRIx64" - 0x%"PRIx64
                     " @ 0x%"HWADDR_PRIx, pte.pte0, pte.pte1, pte_offset);
        /* Not entirely sure what the right action here, but machine
         * check seems reasonable */
        cs->exception_index = POWERPC_EXCP_MCHECK;
        env->error_code = 0;
        return 1;
    }

    /* 5. Check access permissions */

    pp_prot = ppc_hash64_pte_prot(cpu, slb, pte);
    amr_prot = ppc_hash64_amr_prot(cpu, pte);
    prot = pp_prot & amr_prot;

    if ((need_prot[rwx] & ~prot) != 0) {
        /* Access right violation */
        qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
        if (rwx == 2) {
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x08000000;
        } else {
            target_ulong dsisr = 0;

            cs->exception_index = POWERPC_EXCP_DSI;
            env->error_code = 0;
            env->spr[SPR_DAR] = eaddr;
            if (need_prot[rwx] & ~pp_prot) {
                dsisr |= 0x08000000;
            }
            if (rwx == 1) {
                dsisr |= 0x02000000;
            }
            if (need_prot[rwx] & ~amr_prot) {
                dsisr |= 0x00200000;
            }
            env->spr[SPR_DSISR] = dsisr;
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
    hwaddr pte_offset;
    ppc_hash_pte64_t pte;
    unsigned apshift;

    if (msr_dr == 0) {
        /* In real mode the top 4 effective address bits are ignored */
        return addr & 0x0FFFFFFFFFFFFFFFULL;
    }

    slb = slb_lookup(cpu, addr);
    if (!slb) {
        return -1;
    }

    pte_offset = ppc_hash64_htab_lookup(cpu, slb, addr, &pte);
    if (pte_offset == -1) {
        return -1;
    }

    apshift = hpte_page_shift(slb->sps, pte.pte0, pte.pte1);
    if (!apshift) {
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
    tlb_flush(CPU(cpu), 1);
}
