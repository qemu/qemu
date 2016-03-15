/*
 *  PowerPC MMU, TLB and BAT emulation helpers for QEMU.
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash32.h"
#include "exec/log.h"

//#define DEBUG_BAT

#ifdef DEBUG_BATS
#  define LOG_BATS(...) qemu_log_mask(CPU_LOG_MMU, __VA_ARGS__)
#else
#  define LOG_BATS(...) do { } while (0)
#endif

struct mmu_ctx_hash32 {
    hwaddr raddr;      /* Real address              */
    int prot;                      /* Protection bits           */
    int key;                       /* Access key                */
};

static int ppc_hash32_pp_prot(int key, int pp, int nx)
{
    int prot;

    if (key == 0) {
        switch (pp) {
        case 0x0:
        case 0x1:
        case 0x2:
            prot = PAGE_READ | PAGE_WRITE;
            break;

        case 0x3:
            prot = PAGE_READ;
            break;

        default:
            abort();
        }
    } else {
        switch (pp) {
        case 0x0:
            prot = 0;
            break;

        case 0x1:
        case 0x3:
            prot = PAGE_READ;
            break;

        case 0x2:
            prot = PAGE_READ | PAGE_WRITE;
            break;

        default:
            abort();
        }
    }
    if (nx == 0) {
        prot |= PAGE_EXEC;
    }

    return prot;
}

static int ppc_hash32_pte_prot(PowerPCCPU *cpu,
                               target_ulong sr, ppc_hash_pte32_t pte)
{
    CPUPPCState *env = &cpu->env;
    unsigned pp, key;

    key = !!(msr_pr ? (sr & SR32_KP) : (sr & SR32_KS));
    pp = pte.pte1 & HPTE32_R_PP;

    return ppc_hash32_pp_prot(key, pp, !!(sr & SR32_NX));
}

static target_ulong hash32_bat_size(PowerPCCPU *cpu,
                                    target_ulong batu, target_ulong batl)
{
    CPUPPCState *env = &cpu->env;

    if ((msr_pr && !(batu & BATU32_VP))
        || (!msr_pr && !(batu & BATU32_VS))) {
        return 0;
    }

    return BATU32_BEPI & ~((batu & BATU32_BL) << 15);
}

static int hash32_bat_prot(PowerPCCPU *cpu,
                           target_ulong batu, target_ulong batl)
{
    int pp, prot;

    prot = 0;
    pp = batl & BATL32_PP;
    if (pp != 0) {
        prot = PAGE_READ | PAGE_EXEC;
        if (pp == 0x2) {
            prot |= PAGE_WRITE;
        }
    }
    return prot;
}

static target_ulong hash32_bat_601_size(PowerPCCPU *cpu,
                                target_ulong batu, target_ulong batl)
{
    if (!(batl & BATL32_601_V)) {
        return 0;
    }

    return BATU32_BEPI & ~((batl & BATL32_601_BL) << 17);
}

static int hash32_bat_601_prot(PowerPCCPU *cpu,
                               target_ulong batu, target_ulong batl)
{
    CPUPPCState *env = &cpu->env;
    int key, pp;

    pp = batu & BATU32_601_PP;
    if (msr_pr == 0) {
        key = !!(batu & BATU32_601_KS);
    } else {
        key = !!(batu & BATU32_601_KP);
    }
    return ppc_hash32_pp_prot(key, pp, 0);
}

static hwaddr ppc_hash32_bat_lookup(PowerPCCPU *cpu, target_ulong ea, int rwx,
                                    int *prot)
{
    CPUPPCState *env = &cpu->env;
    target_ulong *BATlt, *BATut;
    int i;

    LOG_BATS("%s: %cBAT v " TARGET_FMT_lx "\n", __func__,
             rwx == 2 ? 'I' : 'D', ea);
    if (rwx == 2) {
        BATlt = env->IBAT[1];
        BATut = env->IBAT[0];
    } else {
        BATlt = env->DBAT[1];
        BATut = env->DBAT[0];
    }
    for (i = 0; i < env->nb_BATs; i++) {
        target_ulong batu = BATut[i];
        target_ulong batl = BATlt[i];
        target_ulong mask;

        if (unlikely(env->mmu_model == POWERPC_MMU_601)) {
            mask = hash32_bat_601_size(cpu, batu, batl);
        } else {
            mask = hash32_bat_size(cpu, batu, batl);
        }
        LOG_BATS("%s: %cBAT%d v " TARGET_FMT_lx " BATu " TARGET_FMT_lx
                 " BATl " TARGET_FMT_lx "\n", __func__,
                 type == ACCESS_CODE ? 'I' : 'D', i, ea, batu, batl);

        if (mask && ((ea & mask) == (batu & BATU32_BEPI))) {
            hwaddr raddr = (batl & mask) | (ea & ~mask);

            if (unlikely(env->mmu_model == POWERPC_MMU_601)) {
                *prot = hash32_bat_601_prot(cpu, batu, batl);
            } else {
                *prot = hash32_bat_prot(cpu, batu, batl);
            }

            return raddr & TARGET_PAGE_MASK;
        }
    }

    /* No hit */
#if defined(DEBUG_BATS)
    if (qemu_log_enabled()) {
        LOG_BATS("no BAT match for " TARGET_FMT_lx ":\n", ea);
        for (i = 0; i < 4; i++) {
            BATu = &BATut[i];
            BATl = &BATlt[i];
            BEPIu = *BATu & BATU32_BEPIU;
            BEPIl = *BATu & BATU32_BEPIL;
            bl = (*BATu & 0x00001FFC) << 15;
            LOG_BATS("%s: %cBAT%d v " TARGET_FMT_lx " BATu " TARGET_FMT_lx
                     " BATl " TARGET_FMT_lx "\n\t" TARGET_FMT_lx " "
                     TARGET_FMT_lx " " TARGET_FMT_lx "\n",
                     __func__, type == ACCESS_CODE ? 'I' : 'D', i, ea,
                     *BATu, *BATl, BEPIu, BEPIl, bl);
        }
    }
#endif

    return -1;
}

static int ppc_hash32_direct_store(PowerPCCPU *cpu, target_ulong sr,
                                   target_ulong eaddr, int rwx,
                                   hwaddr *raddr, int *prot)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    int key = !!(msr_pr ? (sr & SR32_KP) : (sr & SR32_KS));

    qemu_log_mask(CPU_LOG_MMU, "direct store...\n");

    if ((sr & 0x1FF00000) >> 20 == 0x07f) {
        /* Memory-forced I/O controller interface access */
        /* If T=1 and BUID=x'07F', the 601 performs a memory access
         * to SR[28-31] LA[4-31], bypassing all protection mechanisms.
         */
        *raddr = ((sr & 0xF) << 28) | (eaddr & 0x0FFFFFFF);
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return 0;
    }

    if (rwx == 2) {
        /* No code fetch is allowed in direct-store areas */
        cs->exception_index = POWERPC_EXCP_ISI;
        env->error_code = 0x10000000;
        return 1;
    }

    switch (env->access_type) {
    case ACCESS_INT:
        /* Integer load/store : only access allowed */
        break;
    case ACCESS_FLOAT:
        /* Floating point load/store */
        cs->exception_index = POWERPC_EXCP_ALIGN;
        env->error_code = POWERPC_EXCP_ALIGN_FP;
        env->spr[SPR_DAR] = eaddr;
        return 1;
    case ACCESS_RES:
        /* lwarx, ldarx or srwcx. */
        env->error_code = 0;
        env->spr[SPR_DAR] = eaddr;
        if (rwx == 1) {
            env->spr[SPR_DSISR] = 0x06000000;
        } else {
            env->spr[SPR_DSISR] = 0x04000000;
        }
        return 1;
    case ACCESS_CACHE:
        /* dcba, dcbt, dcbtst, dcbf, dcbi, dcbst, dcbz, or icbi */
        /* Should make the instruction do no-op.
         * As it already do no-op, it's quite easy :-)
         */
        *raddr = eaddr;
        return 0;
    case ACCESS_EXT:
        /* eciwx or ecowx */
        cs->exception_index = POWERPC_EXCP_DSI;
        env->error_code = 0;
        env->spr[SPR_DAR] = eaddr;
        if (rwx == 1) {
            env->spr[SPR_DSISR] = 0x06100000;
        } else {
            env->spr[SPR_DSISR] = 0x04100000;
        }
        return 1;
    default:
        cpu_abort(cs, "ERROR: instruction should not need "
                 "address translation\n");
    }
    if ((rwx == 1 || key != 1) && (rwx == 0 || key != 0)) {
        *raddr = eaddr;
        return 0;
    } else {
        cs->exception_index = POWERPC_EXCP_DSI;
        env->error_code = 0;
        env->spr[SPR_DAR] = eaddr;
        if (rwx == 1) {
            env->spr[SPR_DSISR] = 0x0a000000;
        } else {
            env->spr[SPR_DSISR] = 0x08000000;
        }
        return 1;
    }
}

hwaddr get_pteg_offset32(PowerPCCPU *cpu, hwaddr hash)
{
    CPUPPCState *env = &cpu->env;

    return (hash * HASH_PTEG_SIZE_32) & env->htab_mask;
}

static hwaddr ppc_hash32_pteg_search(PowerPCCPU *cpu, hwaddr pteg_off,
                                     bool secondary, target_ulong ptem,
                                     ppc_hash_pte32_t *pte)
{
    hwaddr pte_offset = pteg_off;
    target_ulong pte0, pte1;
    int i;

    for (i = 0; i < HPTES_PER_GROUP; i++) {
        pte0 = ppc_hash32_load_hpte0(cpu, pte_offset);
        pte1 = ppc_hash32_load_hpte1(cpu, pte_offset);

        if ((pte0 & HPTE32_V_VALID)
            && (secondary == !!(pte0 & HPTE32_V_SECONDARY))
            && HPTE32_V_COMPARE(pte0, ptem)) {
            pte->pte0 = pte0;
            pte->pte1 = pte1;
            return pte_offset;
        }

        pte_offset += HASH_PTE_SIZE_32;
    }

    return -1;
}

static hwaddr ppc_hash32_htab_lookup(PowerPCCPU *cpu,
                                     target_ulong sr, target_ulong eaddr,
                                     ppc_hash_pte32_t *pte)
{
    CPUPPCState *env = &cpu->env;
    hwaddr pteg_off, pte_offset;
    hwaddr hash;
    uint32_t vsid, pgidx, ptem;

    vsid = sr & SR32_VSID;
    pgidx = (eaddr & ~SEGMENT_MASK_256M) >> TARGET_PAGE_BITS;
    hash = vsid ^ pgidx;
    ptem = (vsid << 7) | (pgidx >> 10);

    /* Page address translation */
    qemu_log_mask(CPU_LOG_MMU, "htab_base " TARGET_FMT_plx
            " htab_mask " TARGET_FMT_plx
            " hash " TARGET_FMT_plx "\n",
            env->htab_base, env->htab_mask, hash);

    /* Primary PTEG lookup */
    qemu_log_mask(CPU_LOG_MMU, "0 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
            " vsid=%" PRIx32 " ptem=%" PRIx32
            " hash=" TARGET_FMT_plx "\n",
            env->htab_base, env->htab_mask, vsid, ptem, hash);
    pteg_off = get_pteg_offset32(cpu, hash);
    pte_offset = ppc_hash32_pteg_search(cpu, pteg_off, 0, ptem, pte);
    if (pte_offset == -1) {
        /* Secondary PTEG lookup */
        qemu_log_mask(CPU_LOG_MMU, "1 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                " vsid=%" PRIx32 " api=%" PRIx32
                " hash=" TARGET_FMT_plx "\n", env->htab_base,
                env->htab_mask, vsid, ptem, ~hash);
        pteg_off = get_pteg_offset32(cpu, ~hash);
        pte_offset = ppc_hash32_pteg_search(cpu, pteg_off, 1, ptem, pte);
    }

    return pte_offset;
}

static hwaddr ppc_hash32_pte_raddr(target_ulong sr, ppc_hash_pte32_t pte,
                                   target_ulong eaddr)
{
    hwaddr rpn = pte.pte1 & HPTE32_R_RPN;
    hwaddr mask = ~TARGET_PAGE_MASK;

    return (rpn & ~mask) | (eaddr & mask);
}

int ppc_hash32_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr, int rwx,
                                int mmu_idx)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    target_ulong sr;
    hwaddr pte_offset;
    ppc_hash_pte32_t pte;
    int prot;
    uint32_t new_pte1;
    const int need_prot[] = {PAGE_READ, PAGE_WRITE, PAGE_EXEC};
    hwaddr raddr;

    assert((rwx == 0) || (rwx == 1) || (rwx == 2));

    /* 1. Handle real mode accesses */
    if (((rwx == 2) && (msr_ir == 0)) || ((rwx != 2) && (msr_dr == 0))) {
        /* Translation is off */
        raddr = eaddr;
        tlb_set_page(cs, eaddr & TARGET_PAGE_MASK, raddr & TARGET_PAGE_MASK,
                     PAGE_READ | PAGE_WRITE | PAGE_EXEC, mmu_idx,
                     TARGET_PAGE_SIZE);
        return 0;
    }

    /* 2. Check Block Address Translation entries (BATs) */
    if (env->nb_BATs != 0) {
        raddr = ppc_hash32_bat_lookup(cpu, eaddr, rwx, &prot);
        if (raddr != -1) {
            if (need_prot[rwx] & ~prot) {
                if (rwx == 2) {
                    cs->exception_index = POWERPC_EXCP_ISI;
                    env->error_code = 0x08000000;
                } else {
                    cs->exception_index = POWERPC_EXCP_DSI;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = eaddr;
                    if (rwx == 1) {
                        env->spr[SPR_DSISR] = 0x0a000000;
                    } else {
                        env->spr[SPR_DSISR] = 0x08000000;
                    }
                }
                return 1;
            }

            tlb_set_page(cs, eaddr & TARGET_PAGE_MASK,
                         raddr & TARGET_PAGE_MASK, prot, mmu_idx,
                         TARGET_PAGE_SIZE);
            return 0;
        }
    }

    /* 3. Look up the Segment Register */
    sr = env->sr[eaddr >> 28];

    /* 4. Handle direct store segments */
    if (sr & SR32_T) {
        if (ppc_hash32_direct_store(cpu, sr, eaddr, rwx,
                                    &raddr, &prot) == 0) {
            tlb_set_page(cs, eaddr & TARGET_PAGE_MASK,
                         raddr & TARGET_PAGE_MASK, prot, mmu_idx,
                         TARGET_PAGE_SIZE);
            return 0;
        } else {
            return 1;
        }
    }

    /* 5. Check for segment level no-execute violation */
    if ((rwx == 2) && (sr & SR32_NX)) {
        cs->exception_index = POWERPC_EXCP_ISI;
        env->error_code = 0x10000000;
        return 1;
    }

    /* 6. Locate the PTE in the hash table */
    pte_offset = ppc_hash32_htab_lookup(cpu, sr, eaddr, &pte);
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

    /* 7. Check access permissions */

    prot = ppc_hash32_pte_prot(cpu, sr, pte);

    if (need_prot[rwx] & ~prot) {
        /* Access right violation */
        qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
        if (rwx == 2) {
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x08000000;
        } else {
            cs->exception_index = POWERPC_EXCP_DSI;
            env->error_code = 0;
            env->spr[SPR_DAR] = eaddr;
            if (rwx == 1) {
                env->spr[SPR_DSISR] = 0x0a000000;
            } else {
                env->spr[SPR_DSISR] = 0x08000000;
            }
        }
        return 1;
    }

    qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");

    /* 8. Update PTE referenced and changed bits if necessary */

    new_pte1 = pte.pte1 | HPTE32_R_R; /* set referenced bit */
    if (rwx == 1) {
        new_pte1 |= HPTE32_R_C; /* set changed (dirty) bit */
    } else {
        /* Treat the page as read-only for now, so that a later write
         * will pass through this function again to set the C bit */
        prot &= ~PAGE_WRITE;
    }

    if (new_pte1 != pte.pte1) {
        ppc_hash32_store_hpte1(cpu, pte_offset, new_pte1);
    }

    /* 9. Determine the real address from the PTE */

    raddr = ppc_hash32_pte_raddr(sr, pte, eaddr);

    tlb_set_page(cs, eaddr & TARGET_PAGE_MASK, raddr & TARGET_PAGE_MASK,
                 prot, mmu_idx, TARGET_PAGE_SIZE);

    return 0;
}

hwaddr ppc_hash32_get_phys_page_debug(PowerPCCPU *cpu, target_ulong eaddr)
{
    CPUPPCState *env = &cpu->env;
    target_ulong sr;
    hwaddr pte_offset;
    ppc_hash_pte32_t pte;
    int prot;

    if (msr_dr == 0) {
        /* Translation is off */
        return eaddr;
    }

    if (env->nb_BATs != 0) {
        hwaddr raddr = ppc_hash32_bat_lookup(cpu, eaddr, 0, &prot);
        if (raddr != -1) {
            return raddr;
        }
    }

    sr = env->sr[eaddr >> 28];

    if (sr & SR32_T) {
        /* FIXME: Add suitable debug support for Direct Store segments */
        return -1;
    }

    pte_offset = ppc_hash32_htab_lookup(cpu, sr, eaddr, &pte);
    if (pte_offset == -1) {
        return -1;
    }

    return ppc_hash32_pte_raddr(sr, pte, eaddr) & TARGET_PAGE_MASK;
}
