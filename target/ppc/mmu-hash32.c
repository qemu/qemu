/*
 *  PowerPC MMU, TLB and BAT emulation helpers for QEMU.
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
#include "cpu.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "system/kvm.h"
#include "kvm_ppc.h"
#include "internal.h"
#include "mmu-hash32.h"
#include "mmu-books.h"
#include "exec/log.h"

/* #define DEBUG_BATS */

#ifdef DEBUG_BATS
#  define LOG_BATS(...) qemu_log_mask(CPU_LOG_MMU, __VA_ARGS__)
#else
#  define LOG_BATS(...) do { } while (0)
#endif

static target_ulong hash32_bat_size(int mmu_idx,
                                    target_ulong batu, target_ulong batl)
{
    if ((mmuidx_pr(mmu_idx) && !(batu & BATU32_VP))
        || (!mmuidx_pr(mmu_idx) && !(batu & BATU32_VS))) {
        return 0;
    }

    return BATU32_BEPI & ~((batu & BATU32_BL) << 15);
}

static hwaddr ppc_hash32_bat_lookup(PowerPCCPU *cpu, target_ulong ea,
                                    MMUAccessType access_type, int *prot,
                                    int mmu_idx)
{
    CPUPPCState *env = &cpu->env;
    target_ulong *BATlt, *BATut;
    bool ifetch = access_type == MMU_INST_FETCH;
    int i;

    LOG_BATS("%s: %cBAT v " TARGET_FMT_lx "\n", __func__,
             ifetch ? 'I' : 'D', ea);
    if (ifetch) {
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

        mask = hash32_bat_size(mmu_idx, batu, batl);
        LOG_BATS("%s: %cBAT%d v " TARGET_FMT_lx " BATu " TARGET_FMT_lx
                 " BATl " TARGET_FMT_lx "\n", __func__,
                 ifetch ? 'I' : 'D', i, ea, batu, batl);

        if (mask && ((ea & mask) == (batu & BATU32_BEPI))) {
            hwaddr raddr = (batl & mask) | (ea & ~mask);

            *prot = ppc_hash32_bat_prot(batu, batl);

            return raddr & TARGET_PAGE_MASK;
        }
    }

    /* No hit */
#if defined(DEBUG_BATS)
    if (qemu_log_enabled()) {
        target_ulong *BATu, *BATl;
        target_ulong BEPIl, BEPIu, bl;

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
                     __func__, ifetch ? 'I' : 'D', i, ea,
                     *BATu, *BATl, BEPIu, BEPIl, bl);
        }
    }
#endif

    return -1;
}

static bool ppc_hash32_direct_store(PowerPCCPU *cpu, target_ulong sr,
                                    target_ulong eaddr,
                                    MMUAccessType access_type,
                                    hwaddr *raddr, int *prot, int mmu_idx,
                                    bool guest_visible)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    qemu_log_mask(CPU_LOG_MMU, "direct store...\n");

    if (access_type == MMU_INST_FETCH) {
        /* No code fetch is allowed in direct-store areas */
        if (guest_visible) {
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x10000000;
        }
        return false;
    }

    /*
     * From ppc_cpu_get_phys_page_debug, env->access_type is not set.
     * Assume ACCESS_INT for that case.
     */
    switch (guest_visible ? env->access_type : ACCESS_INT) {
    case ACCESS_INT:
        /* Integer load/store : only access allowed */
        break;
    case ACCESS_FLOAT:
        /* Floating point load/store */
        cs->exception_index = POWERPC_EXCP_ALIGN;
        env->error_code = POWERPC_EXCP_ALIGN_FP;
        env->spr[SPR_DAR] = eaddr;
        return false;
    case ACCESS_RES:
        /* lwarx, ldarx or srwcx. */
        env->error_code = 0;
        env->spr[SPR_DAR] = eaddr;
        if (access_type == MMU_DATA_STORE) {
            env->spr[SPR_DSISR] = 0x06000000;
        } else {
            env->spr[SPR_DSISR] = 0x04000000;
        }
        return false;
    case ACCESS_CACHE:
        /*
         * dcba, dcbt, dcbtst, dcbf, dcbi, dcbst, dcbz, or icbi
         *
         * Should make the instruction do no-op.  As it already do
         * no-op, it's quite easy :-)
         */
        *raddr = eaddr;
        return true;
    case ACCESS_EXT:
        /* eciwx or ecowx */
        cs->exception_index = POWERPC_EXCP_DSI;
        env->error_code = 0;
        env->spr[SPR_DAR] = eaddr;
        if (access_type == MMU_DATA_STORE) {
            env->spr[SPR_DSISR] = 0x06100000;
        } else {
            env->spr[SPR_DSISR] = 0x04100000;
        }
        return false;
    default:
        cpu_abort(cs, "ERROR: insn should not need address translation\n");
    }

    if (ppc_hash32_key(mmuidx_pr(mmu_idx), sr)) {
        *prot = PAGE_READ | PAGE_WRITE;
    } else {
        *prot = PAGE_READ;
    }
    if (check_prot_access_type(*prot, access_type)) {
        *raddr = eaddr;
        return true;
    }

    if (guest_visible) {
        cs->exception_index = POWERPC_EXCP_DSI;
        env->error_code = 0;
        env->spr[SPR_DAR] = eaddr;
        if (access_type == MMU_DATA_STORE) {
            env->spr[SPR_DSISR] = 0x0a000000;
        } else {
            env->spr[SPR_DSISR] = 0x08000000;
        }
    }
    return false;
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
        /*
         * pte0 contains the valid bit and must be read before pte1,
         * otherwise we might see an old pte1 with a new valid bit and
         * thus an inconsistent hpte value
         */
        smp_rmb();
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

static void ppc_hash32_set_r(PowerPCCPU *cpu, hwaddr pte_offset, uint32_t pte1)
{
    target_ulong base = ppc_hash32_hpt_base(cpu);
    hwaddr offset = pte_offset + 6;

    /* The HW performs a non-atomic byte update */
    stb_phys(CPU(cpu)->as, base + offset, ((pte1 >> 8) & 0xff) | 0x01);
}

static void ppc_hash32_set_c(PowerPCCPU *cpu, hwaddr pte_offset, uint64_t pte1)
{
    target_ulong base = ppc_hash32_hpt_base(cpu);
    hwaddr offset = pte_offset + 7;

    /* The HW performs a non-atomic byte update */
    stb_phys(CPU(cpu)->as, base + offset, (pte1 & 0xff) | 0x80);
}

static hwaddr ppc_hash32_htab_lookup(PowerPCCPU *cpu,
                                     target_ulong sr, target_ulong eaddr,
                                     ppc_hash_pte32_t *pte)
{
    hwaddr pteg_off, pte_offset;
    hwaddr hash;
    uint32_t vsid, pgidx, ptem;

    vsid = sr & SR32_VSID;
    pgidx = (eaddr & ~SEGMENT_MASK_256M) >> TARGET_PAGE_BITS;
    hash = vsid ^ pgidx;
    ptem = (vsid << 7) | (pgidx >> 10);

    /* Page address translation */
    qemu_log_mask(CPU_LOG_MMU, "htab_base " HWADDR_FMT_plx
            " htab_mask " HWADDR_FMT_plx
            " hash " HWADDR_FMT_plx "\n",
            ppc_hash32_hpt_base(cpu), ppc_hash32_hpt_mask(cpu), hash);

    /* Primary PTEG lookup */
    qemu_log_mask(CPU_LOG_MMU, "0 htab=" HWADDR_FMT_plx "/" HWADDR_FMT_plx
            " vsid=%" PRIx32 " ptem=%" PRIx32
            " hash=" HWADDR_FMT_plx "\n",
            ppc_hash32_hpt_base(cpu), ppc_hash32_hpt_mask(cpu),
            vsid, ptem, hash);
    pteg_off = get_pteg_offset32(cpu, hash);
    pte_offset = ppc_hash32_pteg_search(cpu, pteg_off, 0, ptem, pte);
    if (pte_offset == -1) {
        /* Secondary PTEG lookup */
        qemu_log_mask(CPU_LOG_MMU, "1 htab=" HWADDR_FMT_plx "/" HWADDR_FMT_plx
                " vsid=%" PRIx32 " api=%" PRIx32
                " hash=" HWADDR_FMT_plx "\n", ppc_hash32_hpt_base(cpu),
                ppc_hash32_hpt_mask(cpu), vsid, ptem, ~hash);
        pteg_off = get_pteg_offset32(cpu, ~hash);
        pte_offset = ppc_hash32_pteg_search(cpu, pteg_off, 1, ptem, pte);
    }

    return pte_offset;
}

bool ppc_hash32_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                      hwaddr *raddrp, int *psizep, int *protp, int mmu_idx,
                      bool guest_visible)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    target_ulong sr;
    hwaddr pte_offset, raddr;
    ppc_hash_pte32_t pte;
    bool key;
    int prot;

    /* There are no hash32 large pages. */
    *psizep = TARGET_PAGE_BITS;

    /* 1. Handle real mode accesses */
    if (mmuidx_real(mmu_idx)) {
        /* Translation is off */
        *raddrp = eaddr;
        *protp = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return true;
    }

    /* 2. Check Block Address Translation entries (BATs) */
    if (env->nb_BATs != 0) {
        raddr = ppc_hash32_bat_lookup(cpu, eaddr, access_type, protp, mmu_idx);
        if (raddr != -1) {
            if (!check_prot_access_type(*protp, access_type)) {
                if (guest_visible) {
                    if (access_type == MMU_INST_FETCH) {
                        cs->exception_index = POWERPC_EXCP_ISI;
                        env->error_code = 0x08000000;
                    } else {
                        cs->exception_index = POWERPC_EXCP_DSI;
                        env->error_code = 0;
                        env->spr[SPR_DAR] = eaddr;
                        if (access_type == MMU_DATA_STORE) {
                            env->spr[SPR_DSISR] = 0x0a000000;
                        } else {
                            env->spr[SPR_DSISR] = 0x08000000;
                        }
                    }
                }
                return false;
            }
            *raddrp = raddr;
            return true;
        }
    }

    /* 3. Look up the Segment Register */
    sr = env->sr[eaddr >> 28];

    /* 4. Handle direct store segments */
    if (sr & SR32_T) {
        return ppc_hash32_direct_store(cpu, sr, eaddr, access_type,
                                       raddrp, protp, mmu_idx, guest_visible);
    }

    /* 5. Check for segment level no-execute violation */
    if (access_type == MMU_INST_FETCH && (sr & SR32_NX)) {
        if (guest_visible) {
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x10000000;
        }
        return false;
    }

    /* 6. Locate the PTE in the hash table */
    pte_offset = ppc_hash32_htab_lookup(cpu, sr, eaddr, &pte);
    if (pte_offset == -1) {
        if (guest_visible) {
            if (access_type == MMU_INST_FETCH) {
                cs->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x40000000;
            } else {
                cs->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                env->spr[SPR_DAR] = eaddr;
                if (access_type == MMU_DATA_STORE) {
                    env->spr[SPR_DSISR] = 0x42000000;
                } else {
                    env->spr[SPR_DSISR] = 0x40000000;
                }
            }
        }
        return false;
    }
    qemu_log_mask(CPU_LOG_MMU,
                "found PTE at offset %08" HWADDR_PRIx "\n", pte_offset);

    /* 7. Check access permissions */
    key = ppc_hash32_key(mmuidx_pr(mmu_idx), sr);
    prot = ppc_hash32_prot(key, pte.pte1 & HPTE32_R_PP, sr & SR32_NX);

    if (!check_prot_access_type(prot, access_type)) {
        /* Access right violation */
        qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
        if (guest_visible) {
            if (access_type == MMU_INST_FETCH) {
                cs->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x08000000;
            } else {
                cs->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                env->spr[SPR_DAR] = eaddr;
                if (access_type == MMU_DATA_STORE) {
                    env->spr[SPR_DSISR] = 0x0a000000;
                } else {
                    env->spr[SPR_DSISR] = 0x08000000;
                }
            }
        }
        return false;
    }

    qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");

    /* 8. Update PTE referenced and changed bits if necessary */

    if (!(pte.pte1 & HPTE32_R_R)) {
        ppc_hash32_set_r(cpu, pte_offset, pte.pte1);
    }
    if (!(pte.pte1 & HPTE32_R_C)) {
        if (access_type == MMU_DATA_STORE) {
            ppc_hash32_set_c(cpu, pte_offset, pte.pte1);
        } else {
            /*
             * Treat the page as read-only for now, so that a later write
             * will pass through this function again to set the C bit
             */
            prot &= ~PAGE_WRITE;
        }
    }
    *protp = prot;

    /* 9. Determine the real address from the PTE */
    *raddrp = pte.pte1 & HPTE32_R_RPN;
    *raddrp &= TARGET_PAGE_MASK;
    *raddrp |= eaddr & ~TARGET_PAGE_MASK;
    return true;
}
