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
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "mmu-hash32.h"
#include "exec/exec-all.h"
#include "exec/log.h"
#include "helper_regs.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/qemu-print.h"
#include "internal.h"
#include "mmu-book3s-v3.h"
#include "mmu-radix64.h"

/* #define DUMP_PAGE_TABLES */

void ppc_store_sdr1(CPUPPCState *env, target_ulong value)
{
    PowerPCCPU *cpu = env_archcpu(env);
    qemu_log_mask(CPU_LOG_MMU, "%s: " TARGET_FMT_lx "\n", __func__, value);
    assert(!cpu->env.has_hv_mode || !cpu->vhyp);
#if defined(TARGET_PPC64)
    if (mmu_is_64bit(env->mmu_model)) {
        target_ulong sdr_mask = SDR_64_HTABORG | SDR_64_HTABSIZE;
        target_ulong htabsize = value & SDR_64_HTABSIZE;

        if (value & ~sdr_mask) {
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid bits 0x"TARGET_FMT_lx
                     " set in SDR1", value & ~sdr_mask);
            value &= sdr_mask;
        }
        if (htabsize > 28) {
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid HTABSIZE 0x" TARGET_FMT_lx
                     " stored in SDR1", htabsize);
            return;
        }
    }
#endif /* defined(TARGET_PPC64) */
    /* FIXME: Should check for valid HTABMASK values in 32-bit case */
    env->spr[SPR_SDR1] = value;
}

/*****************************************************************************/
/* PowerPC MMU emulation */

static int pp_check(int key, int pp, int nx)
{
    int access;

    /* Compute access rights */
    access = 0;
    if (key == 0) {
        switch (pp) {
        case 0x0:
        case 0x1:
        case 0x2:
            access |= PAGE_WRITE;
            /* fall through */
        case 0x3:
            access |= PAGE_READ;
            break;
        }
    } else {
        switch (pp) {
        case 0x0:
            access = 0;
            break;
        case 0x1:
        case 0x3:
            access = PAGE_READ;
            break;
        case 0x2:
            access = PAGE_READ | PAGE_WRITE;
            break;
        }
    }
    if (nx == 0) {
        access |= PAGE_EXEC;
    }

    return access;
}

static int check_prot(int prot, MMUAccessType access_type)
{
    return prot & prot_for_access_type(access_type) ? 0 : -2;
}

int ppc6xx_tlb_getnum(CPUPPCState *env, target_ulong eaddr,
                                    int way, int is_code)
{
    int nr;

    /* Select TLB num in a way from address */
    nr = (eaddr >> TARGET_PAGE_BITS) & (env->tlb_per_way - 1);
    /* Select TLB way */
    nr += env->tlb_per_way * way;
    /* 6xx have separate TLBs for instructions and data */
    if (is_code && env->id_tlbs == 1) {
        nr += env->nb_tlb;
    }

    return nr;
}

static int ppc6xx_tlb_pte_check(mmu_ctx_t *ctx, target_ulong pte0,
                                target_ulong pte1, int h,
                                MMUAccessType access_type)
{
    target_ulong ptem, mmask;
    int access, ret, pteh, ptev, pp;

    ret = -1;
    /* Check validity and table match */
    ptev = pte_is_valid(pte0);
    pteh = (pte0 >> 6) & 1;
    if (ptev && h == pteh) {
        /* Check vsid & api */
        ptem = pte0 & PTE_PTEM_MASK;
        mmask = PTE_CHECK_MASK;
        pp = pte1 & 0x00000003;
        if (ptem == ctx->ptem) {
            if (ctx->raddr != (hwaddr)-1ULL) {
                /* all matches should have equal RPN, WIMG & PP */
                if ((ctx->raddr & mmask) != (pte1 & mmask)) {
                    qemu_log_mask(CPU_LOG_MMU, "Bad RPN/WIMG/PP\n");
                    return -3;
                }
            }
            /* Compute access rights */
            access = pp_check(ctx->key, pp, ctx->nx);
            /* Keep the matching PTE information */
            ctx->raddr = pte1;
            ctx->prot = access;
            ret = check_prot(ctx->prot, access_type);
            if (ret == 0) {
                /* Access granted */
                qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");
            } else {
                /* Access right violation */
                qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
            }
        }
    }

    return ret;
}

static int pte_update_flags(mmu_ctx_t *ctx, target_ulong *pte1p,
                            int ret, MMUAccessType access_type)
{
    int store = 0;

    /* Update page flags */
    if (!(*pte1p & 0x00000100)) {
        /* Update accessed flag */
        *pte1p |= 0x00000100;
        store = 1;
    }
    if (!(*pte1p & 0x00000080)) {
        if (access_type == MMU_DATA_STORE && ret == 0) {
            /* Update changed flag */
            *pte1p |= 0x00000080;
            store = 1;
        } else {
            /* Force page fault for first write access */
            ctx->prot &= ~PAGE_WRITE;
        }
    }

    return store;
}

/* Software driven TLB helpers */

static int ppc6xx_tlb_check(CPUPPCState *env, mmu_ctx_t *ctx,
                            target_ulong eaddr, MMUAccessType access_type)
{
    ppc6xx_tlb_t *tlb;
    int nr, best, way;
    int ret;

    best = -1;
    ret = -1; /* No TLB found */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way, access_type == MMU_INST_FETCH);
        tlb = &env->tlb.tlb6[nr];
        /* This test "emulates" the PTE index match for hardware TLBs */
        if ((eaddr & TARGET_PAGE_MASK) != tlb->EPN) {
            qemu_log_mask(CPU_LOG_MMU, "TLB %d/%d %s [" TARGET_FMT_lx
                          " " TARGET_FMT_lx "] <> " TARGET_FMT_lx "\n",
                          nr, env->nb_tlb,
                          pte_is_valid(tlb->pte0) ? "valid" : "inval",
                          tlb->EPN, tlb->EPN + TARGET_PAGE_SIZE, eaddr);
            continue;
        }
        qemu_log_mask(CPU_LOG_MMU, "TLB %d/%d %s " TARGET_FMT_lx " <> "
                      TARGET_FMT_lx " " TARGET_FMT_lx " %c %c\n",
                      nr, env->nb_tlb,
                      pte_is_valid(tlb->pte0) ? "valid" : "inval",
                      tlb->EPN, eaddr, tlb->pte1,
                      access_type == MMU_DATA_STORE ? 'S' : 'L',
                      access_type == MMU_INST_FETCH ? 'I' : 'D');
        switch (ppc6xx_tlb_pte_check(ctx, tlb->pte0, tlb->pte1,
                                     0, access_type)) {
        case -3:
            /* TLB inconsistency */
            return -1;
        case -2:
            /* Access violation */
            ret = -2;
            best = nr;
            break;
        case -1:
        default:
            /* No match */
            break;
        case 0:
            /* access granted */
            /*
             * XXX: we should go on looping to check all TLBs
             *      consistency but we can speed-up the whole thing as
             *      the result would be undefined if TLBs are not
             *      consistent.
             */
            ret = 0;
            best = nr;
            goto done;
        }
    }
    if (best != -1) {
    done:
        qemu_log_mask(CPU_LOG_MMU, "found TLB at addr " HWADDR_FMT_plx
                      " prot=%01x ret=%d\n",
                      ctx->raddr & TARGET_PAGE_MASK, ctx->prot, ret);
        /* Update page flags */
        pte_update_flags(ctx, &env->tlb.tlb6[best].pte1, ret, access_type);
    }

    return ret;
}

/* Perform BAT hit & translation */
static inline void bat_size_prot(CPUPPCState *env, target_ulong *blp,
                                 int *validp, int *protp, target_ulong *BATu,
                                 target_ulong *BATl)
{
    target_ulong bl;
    int pp, valid, prot;

    bl = (*BATu & 0x00001FFC) << 15;
    valid = 0;
    prot = 0;
    if ((!FIELD_EX64(env->msr, MSR, PR) && (*BATu & 0x00000002)) ||
        (FIELD_EX64(env->msr, MSR, PR) && (*BATu & 0x00000001))) {
        valid = 1;
        pp = *BATl & 0x00000003;
        if (pp != 0) {
            prot = PAGE_READ | PAGE_EXEC;
            if (pp == 0x2) {
                prot |= PAGE_WRITE;
            }
        }
    }
    *blp = bl;
    *validp = valid;
    *protp = prot;
}

static int get_bat_6xx_tlb(CPUPPCState *env, mmu_ctx_t *ctx,
                           target_ulong virtual, MMUAccessType access_type)
{
    target_ulong *BATlt, *BATut, *BATu, *BATl;
    target_ulong BEPIl, BEPIu, bl;
    int i, valid, prot;
    int ret = -1;
    bool ifetch = access_type == MMU_INST_FETCH;

     qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT v " TARGET_FMT_lx "\n", __func__,
             ifetch ? 'I' : 'D', virtual);
    if (ifetch) {
        BATlt = env->IBAT[1];
        BATut = env->IBAT[0];
    } else {
        BATlt = env->DBAT[1];
        BATut = env->DBAT[0];
    }
    for (i = 0; i < env->nb_BATs; i++) {
        BATu = &BATut[i];
        BATl = &BATlt[i];
        BEPIu = *BATu & 0xF0000000;
        BEPIl = *BATu & 0x0FFE0000;
        bat_size_prot(env, &bl, &valid, &prot, BATu, BATl);
         qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT%d v " TARGET_FMT_lx " BATu "
                       TARGET_FMT_lx " BATl " TARGET_FMT_lx "\n", __func__,
                       ifetch ? 'I' : 'D', i, virtual, *BATu, *BATl);
        if ((virtual & 0xF0000000) == BEPIu &&
            ((virtual & 0x0FFE0000) & ~bl) == BEPIl) {
            /* BAT matches */
            if (valid != 0) {
                /* Get physical address */
                ctx->raddr = (*BATl & 0xF0000000) |
                    ((virtual & 0x0FFE0000 & bl) | (*BATl & 0x0FFE0000)) |
                    (virtual & 0x0001F000);
                /* Compute access rights */
                ctx->prot = prot;
                ret = check_prot(ctx->prot, access_type);
                if (ret == 0) {
                    qemu_log_mask(CPU_LOG_MMU, "BAT %d match: r " HWADDR_FMT_plx
                                  " prot=%c%c\n", i, ctx->raddr,
                                  ctx->prot & PAGE_READ ? 'R' : '-',
                                  ctx->prot & PAGE_WRITE ? 'W' : '-');
                }
                break;
            }
        }
    }
    if (ret < 0) {
        if (qemu_log_enabled()) {
            qemu_log_mask(CPU_LOG_MMU, "no BAT match for "
                          TARGET_FMT_lx ":\n", virtual);
            for (i = 0; i < 4; i++) {
                BATu = &BATut[i];
                BATl = &BATlt[i];
                BEPIu = *BATu & 0xF0000000;
                BEPIl = *BATu & 0x0FFE0000;
                bl = (*BATu & 0x00001FFC) << 15;
                 qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT%d v "
                               TARGET_FMT_lx " BATu " TARGET_FMT_lx
                               " BATl " TARGET_FMT_lx "\n\t" TARGET_FMT_lx " "
                               TARGET_FMT_lx " " TARGET_FMT_lx "\n",
                               __func__, ifetch ? 'I' : 'D', i, virtual,
                               *BATu, *BATl, BEPIu, BEPIl, bl);
            }
        }
    }
    /* No hit */
    return ret;
}

/* Perform segment based translation */
static int get_segment_6xx_tlb(CPUPPCState *env, mmu_ctx_t *ctx,
                               target_ulong eaddr, MMUAccessType access_type,
                               int type)
{
    PowerPCCPU *cpu = env_archcpu(env);
    hwaddr hash;
    target_ulong vsid;
    int ds, target_page_bits;
    bool pr;
    int ret;
    target_ulong sr, pgidx;

    pr = FIELD_EX64(env->msr, MSR, PR);
    ctx->eaddr = eaddr;

    sr = env->sr[eaddr >> 28];
    ctx->key = (((sr & 0x20000000) && pr) ||
                ((sr & 0x40000000) && !pr)) ? 1 : 0;
    ds = sr & 0x80000000 ? 1 : 0;
    ctx->nx = sr & 0x10000000 ? 1 : 0;
    vsid = sr & 0x00FFFFFF;
    target_page_bits = TARGET_PAGE_BITS;
    qemu_log_mask(CPU_LOG_MMU,
                  "Check segment v=" TARGET_FMT_lx " %d " TARGET_FMT_lx
                  " nip=" TARGET_FMT_lx " lr=" TARGET_FMT_lx
                  " ir=%d dr=%d pr=%d %d t=%d\n",
                  eaddr, (int)(eaddr >> 28), sr, env->nip, env->lr,
                  (int)FIELD_EX64(env->msr, MSR, IR),
                  (int)FIELD_EX64(env->msr, MSR, DR), pr ? 1 : 0,
                  access_type == MMU_DATA_STORE, type);
    pgidx = (eaddr & ~SEGMENT_MASK_256M) >> target_page_bits;
    hash = vsid ^ pgidx;
    ctx->ptem = (vsid << 7) | (pgidx >> 10);

    qemu_log_mask(CPU_LOG_MMU,
            "pte segment: key=%d ds %d nx %d vsid " TARGET_FMT_lx "\n",
            ctx->key, ds, ctx->nx, vsid);
    ret = -1;
    if (!ds) {
        /* Check if instruction fetch is allowed, if needed */
        if (type != ACCESS_CODE || ctx->nx == 0) {
            /* Page address translation */
            qemu_log_mask(CPU_LOG_MMU, "htab_base " HWADDR_FMT_plx
                    " htab_mask " HWADDR_FMT_plx
                    " hash " HWADDR_FMT_plx "\n",
                    ppc_hash32_hpt_base(cpu), ppc_hash32_hpt_mask(cpu), hash);
            ctx->hash[0] = hash;
            ctx->hash[1] = ~hash;

            /* Initialize real address with an invalid value */
            ctx->raddr = (hwaddr)-1ULL;
            /* Software TLB search */
            ret = ppc6xx_tlb_check(env, ctx, eaddr, access_type);
#if defined(DUMP_PAGE_TABLES)
            if (qemu_loglevel_mask(CPU_LOG_MMU)) {
                CPUState *cs = env_cpu(env);
                hwaddr curaddr;
                uint32_t a0, a1, a2, a3;

                qemu_log("Page table: " HWADDR_FMT_plx " len " HWADDR_FMT_plx
                         "\n", ppc_hash32_hpt_base(cpu),
                         ppc_hash32_hpt_mask(cpu) + 0x80);
                for (curaddr = ppc_hash32_hpt_base(cpu);
                     curaddr < (ppc_hash32_hpt_base(cpu)
                                + ppc_hash32_hpt_mask(cpu) + 0x80);
                     curaddr += 16) {
                    a0 = ldl_phys(cs->as, curaddr);
                    a1 = ldl_phys(cs->as, curaddr + 4);
                    a2 = ldl_phys(cs->as, curaddr + 8);
                    a3 = ldl_phys(cs->as, curaddr + 12);
                    if (a0 != 0 || a1 != 0 || a2 != 0 || a3 != 0) {
                        qemu_log(HWADDR_FMT_plx ": %08x %08x %08x %08x\n",
                                 curaddr, a0, a1, a2, a3);
                    }
                }
            }
#endif
        } else {
            qemu_log_mask(CPU_LOG_MMU, "No access allowed\n");
            ret = -3;
        }
    } else {
        qemu_log_mask(CPU_LOG_MMU, "direct store...\n");
        /* Direct-store segment : absolutely *BUGGY* for now */

        switch (type) {
        case ACCESS_INT:
            /* Integer load/store : only access allowed */
            break;
        case ACCESS_CODE:
            /* No code fetch is allowed in direct-store areas */
            return -4;
        case ACCESS_FLOAT:
            /* Floating point load/store */
            return -4;
        case ACCESS_RES:
            /* lwarx, ldarx or srwcx. */
            return -4;
        case ACCESS_CACHE:
            /*
             * dcba, dcbt, dcbtst, dcbf, dcbi, dcbst, dcbz, or icbi
             *
             * Should make the instruction do no-op.  As it already do
             * no-op, it's quite easy :-)
             */
            ctx->raddr = eaddr;
            return 0;
        case ACCESS_EXT:
            /* eciwx or ecowx */
            return -4;
        default:
            qemu_log_mask(CPU_LOG_MMU, "ERROR: instruction should not need "
                          "address translation\n");
            return -4;
        }
        if ((access_type == MMU_DATA_STORE || ctx->key != 1) &&
            (access_type == MMU_DATA_LOAD || ctx->key != 0)) {
            ctx->raddr = eaddr;
            ret = 2;
        } else {
            ret = -2;
        }
    }

    return ret;
}

/* Generic TLB check function for embedded PowerPC implementations */
static bool ppcemb_tlb_check(CPUPPCState *env, ppcemb_tlb_t *tlb,
                             hwaddr *raddrp,
                             target_ulong address, uint32_t pid, int i)
{
    target_ulong mask;

    /* Check valid flag */
    if (!(tlb->prot & PAGE_VALID)) {
        return false;
    }
    mask = ~(tlb->size - 1);
    qemu_log_mask(CPU_LOG_MMU, "%s: TLB %d address " TARGET_FMT_lx
                  " PID %u <=> " TARGET_FMT_lx " " TARGET_FMT_lx " %u %x\n",
                  __func__, i, address, pid, tlb->EPN,
                  mask, (uint32_t)tlb->PID, tlb->prot);
    /* Check PID */
    if (tlb->PID != 0 && tlb->PID != pid) {
        return false;
    }
    /* Check effective address */
    if ((address & mask) != tlb->EPN) {
        return false;
    }
    *raddrp = (tlb->RPN & mask) | (address & ~mask);
    return true;
}

/* Generic TLB search function for PowerPC embedded implementations */
int ppcemb_tlb_search(CPUPPCState *env, target_ulong address, uint32_t pid)
{
    ppcemb_tlb_t *tlb;
    hwaddr raddr;
    int i;

    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb.tlbe[i];
        if (ppcemb_tlb_check(env, tlb, &raddr, address, pid, i)) {
            return i;
        }
    }
    return -1;
}

static int mmu40x_get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                       target_ulong address,
                                       MMUAccessType access_type)
{
    ppcemb_tlb_t *tlb;
    hwaddr raddr;
    int i, ret, zsel, zpr, pr;

    ret = -1;
    raddr = (hwaddr)-1ULL;
    pr = FIELD_EX64(env->msr, MSR, PR);
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb.tlbe[i];
        if (!ppcemb_tlb_check(env, tlb, &raddr, address,
                              env->spr[SPR_40x_PID], i)) {
            continue;
        }
        zsel = (tlb->attr >> 4) & 0xF;
        zpr = (env->spr[SPR_40x_ZPR] >> (30 - (2 * zsel))) & 0x3;
        qemu_log_mask(CPU_LOG_MMU,
                      "%s: TLB %d zsel %d zpr %d ty %d attr %08x\n",
                      __func__, i, zsel, zpr, access_type, tlb->attr);
        /* Check execute enable bit */
        switch (zpr) {
        case 0x2:
            if (pr != 0) {
                goto check_perms;
            }
            /* fall through */
        case 0x3:
            /* All accesses granted */
            ctx->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            ret = 0;
            break;
        case 0x0:
            if (pr != 0) {
                /* Raise Zone protection fault.  */
                env->spr[SPR_40x_ESR] = 1 << 22;
                ctx->prot = 0;
                ret = -2;
                break;
            }
            /* fall through */
        case 0x1:
        check_perms:
            /* Check from TLB entry */
            ctx->prot = tlb->prot;
            ret = check_prot(ctx->prot, access_type);
            if (ret == -2) {
                env->spr[SPR_40x_ESR] = 0;
            }
            break;
        }
        if (ret >= 0) {
            ctx->raddr = raddr;
            qemu_log_mask(CPU_LOG_MMU, "%s: access granted " TARGET_FMT_lx
                          " => " HWADDR_FMT_plx
                          " %d %d\n", __func__, address, ctx->raddr, ctx->prot,
                          ret);
            return 0;
        }
    }
     qemu_log_mask(CPU_LOG_MMU, "%s: access refused " TARGET_FMT_lx
                   " => " HWADDR_FMT_plx
                   " %d %d\n", __func__, address, raddr, ctx->prot, ret);

    return ret;
}

static bool mmubooke_check_pid(CPUPPCState *env, ppcemb_tlb_t *tlb,
                               hwaddr *raddr, target_ulong addr, int i)
{
    if (ppcemb_tlb_check(env, tlb, raddr, addr, env->spr[SPR_BOOKE_PID], i)) {
        if (!env->nb_pids) {
            /* Extend the physical address to 36 bits */
            *raddr |= (uint64_t)(tlb->RPN & 0xF) << 32;
        }
        return true;
    } else if (!env->nb_pids) {
        return false;
    }
    if (env->spr[SPR_BOOKE_PID1] &&
        ppcemb_tlb_check(env, tlb, raddr, addr, env->spr[SPR_BOOKE_PID1], i)) {
        return true;
    }
    if (env->spr[SPR_BOOKE_PID2] &&
        ppcemb_tlb_check(env, tlb, raddr, addr, env->spr[SPR_BOOKE_PID2], i)) {
        return true;
    }
    return false;
}

static int mmubooke_check_tlb(CPUPPCState *env, ppcemb_tlb_t *tlb,
                              hwaddr *raddr, int *prot, target_ulong address,
                              MMUAccessType access_type, int i)
{
    int prot2;

    if (!mmubooke_check_pid(env, tlb, raddr, address, i)) {
        qemu_log_mask(CPU_LOG_MMU, "%s: TLB entry not found\n", __func__);
        return -1;
    }

    if (FIELD_EX64(env->msr, MSR, PR)) {
        prot2 = tlb->prot & 0xF;
    } else {
        prot2 = (tlb->prot >> 4) & 0xF;
    }

    /* Check the address space */
    if ((access_type == MMU_INST_FETCH ?
        FIELD_EX64(env->msr, MSR, IR) :
        FIELD_EX64(env->msr, MSR, DR)) != (tlb->attr & 1)) {
        qemu_log_mask(CPU_LOG_MMU, "%s: AS doesn't match\n", __func__);
        return -1;
    }

    *prot = prot2;
    if (prot2 & prot_for_access_type(access_type)) {
        qemu_log_mask(CPU_LOG_MMU, "%s: good TLB!\n", __func__);
        return 0;
    }

    qemu_log_mask(CPU_LOG_MMU, "%s: no prot match: %x\n", __func__, prot2);
    return access_type == MMU_INST_FETCH ? -3 : -2;
}

static int mmubooke_get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                         target_ulong address,
                                         MMUAccessType access_type)
{
    ppcemb_tlb_t *tlb;
    hwaddr raddr;
    int i, ret;

    ret = -1;
    raddr = (hwaddr)-1ULL;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb.tlbe[i];
        ret = mmubooke_check_tlb(env, tlb, &raddr, &ctx->prot, address,
                                 access_type, i);
        if (ret != -1) {
            break;
        }
    }

    if (ret >= 0) {
        ctx->raddr = raddr;
        qemu_log_mask(CPU_LOG_MMU, "%s: access granted " TARGET_FMT_lx
                      " => " HWADDR_FMT_plx " %d %d\n", __func__,
                      address, ctx->raddr, ctx->prot, ret);
    } else {
         qemu_log_mask(CPU_LOG_MMU, "%s: access refused " TARGET_FMT_lx
                       " => " HWADDR_FMT_plx " %d %d\n", __func__,
                       address, raddr, ctx->prot, ret);
    }

    return ret;
}

hwaddr booke206_tlb_to_page_size(CPUPPCState *env, ppcmas_tlb_t *tlb)
{
    int tlbm_size;

    tlbm_size = (tlb->mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;

    return 1024ULL << tlbm_size;
}

/* TLB check function for MAS based SoftTLBs */
int ppcmas_tlb_check(CPUPPCState *env, ppcmas_tlb_t *tlb, hwaddr *raddrp,
                     target_ulong address, uint32_t pid)
{
    hwaddr mask;
    uint32_t tlb_pid;

    if (!FIELD_EX64(env->msr, MSR, CM)) {
        /* In 32bit mode we can only address 32bit EAs */
        address = (uint32_t)address;
    }

    /* Check valid flag */
    if (!(tlb->mas1 & MAS1_VALID)) {
        return -1;
    }

    mask = ~(booke206_tlb_to_page_size(env, tlb) - 1);
     qemu_log_mask(CPU_LOG_MMU, "%s: TLB ADDR=0x" TARGET_FMT_lx
                   " PID=0x%x MAS1=0x%x MAS2=0x%" PRIx64 " mask=0x%"
                   HWADDR_PRIx " MAS7_3=0x%" PRIx64 " MAS8=0x%" PRIx32 "\n",
                   __func__, address, pid, tlb->mas1, tlb->mas2, mask,
                   tlb->mas7_3, tlb->mas8);

    /* Check PID */
    tlb_pid = (tlb->mas1 & MAS1_TID_MASK) >> MAS1_TID_SHIFT;
    if (tlb_pid != 0 && tlb_pid != pid) {
        return -1;
    }

    /* Check effective address */
    if ((address & mask) != (tlb->mas2 & MAS2_EPN_MASK)) {
        return -1;
    }

    if (raddrp) {
        *raddrp = (tlb->mas7_3 & mask) | (address & ~mask);
    }

    return 0;
}

static bool is_epid_mmu(int mmu_idx)
{
    return mmu_idx == PPC_TLB_EPID_STORE || mmu_idx == PPC_TLB_EPID_LOAD;
}

static uint32_t mmubooke206_esr(int mmu_idx, MMUAccessType access_type)
{
    uint32_t esr = 0;
    if (access_type == MMU_DATA_STORE) {
        esr |= ESR_ST;
    }
    if (is_epid_mmu(mmu_idx)) {
        esr |= ESR_EPID;
    }
    return esr;
}

/*
 * Get EPID register given the mmu_idx. If this is regular load,
 * construct the EPID access bits from current processor state
 *
 * Get the effective AS and PR bits and the PID. The PID is returned
 * only if EPID load is requested, otherwise the caller must detect
 * the correct EPID.  Return true if valid EPID is returned.
 */
static bool mmubooke206_get_as(CPUPPCState *env,
                               int mmu_idx, uint32_t *epid_out,
                               bool *as_out, bool *pr_out)
{
    if (is_epid_mmu(mmu_idx)) {
        uint32_t epidr;
        if (mmu_idx == PPC_TLB_EPID_STORE) {
            epidr = env->spr[SPR_BOOKE_EPSC];
        } else {
            epidr = env->spr[SPR_BOOKE_EPLC];
        }
        *epid_out = (epidr & EPID_EPID) >> EPID_EPID_SHIFT;
        *as_out = !!(epidr & EPID_EAS);
        *pr_out = !!(epidr & EPID_EPR);
        return true;
    } else {
        *as_out = FIELD_EX64(env->msr, MSR, DS);
        *pr_out = FIELD_EX64(env->msr, MSR, PR);
        return false;
    }
}

/* Check if the tlb found by hashing really matches */
static int mmubooke206_check_tlb(CPUPPCState *env, ppcmas_tlb_t *tlb,
                                 hwaddr *raddr, int *prot,
                                 target_ulong address,
                                 MMUAccessType access_type, int mmu_idx)
{
    int prot2 = 0;
    uint32_t epid;
    bool as, pr;
    bool use_epid = mmubooke206_get_as(env, mmu_idx, &epid, &as, &pr);

    if (!use_epid) {
        if (ppcmas_tlb_check(env, tlb, raddr, address,
                             env->spr[SPR_BOOKE_PID]) >= 0) {
            goto found_tlb;
        }

        if (env->spr[SPR_BOOKE_PID1] &&
            ppcmas_tlb_check(env, tlb, raddr, address,
                             env->spr[SPR_BOOKE_PID1]) >= 0) {
            goto found_tlb;
        }

        if (env->spr[SPR_BOOKE_PID2] &&
            ppcmas_tlb_check(env, tlb, raddr, address,
                             env->spr[SPR_BOOKE_PID2]) >= 0) {
            goto found_tlb;
        }
    } else {
        if (ppcmas_tlb_check(env, tlb, raddr, address, epid) >= 0) {
            goto found_tlb;
        }
    }

    qemu_log_mask(CPU_LOG_MMU, "%s: No TLB entry found for effective address "
                  "0x" TARGET_FMT_lx "\n", __func__, address);
    return -1;

found_tlb:

    if (pr) {
        if (tlb->mas7_3 & MAS3_UR) {
            prot2 |= PAGE_READ;
        }
        if (tlb->mas7_3 & MAS3_UW) {
            prot2 |= PAGE_WRITE;
        }
        if (tlb->mas7_3 & MAS3_UX) {
            prot2 |= PAGE_EXEC;
        }
    } else {
        if (tlb->mas7_3 & MAS3_SR) {
            prot2 |= PAGE_READ;
        }
        if (tlb->mas7_3 & MAS3_SW) {
            prot2 |= PAGE_WRITE;
        }
        if (tlb->mas7_3 & MAS3_SX) {
            prot2 |= PAGE_EXEC;
        }
    }

    /* Check the address space and permissions */
    if (access_type == MMU_INST_FETCH) {
        /* There is no way to fetch code using epid load */
        assert(!use_epid);
        as = FIELD_EX64(env->msr, MSR, IR);
    }

    if (as != ((tlb->mas1 & MAS1_TS) >> MAS1_TS_SHIFT)) {
        qemu_log_mask(CPU_LOG_MMU, "%s: AS doesn't match\n", __func__);
        return -1;
    }

    *prot = prot2;
    if (prot2 & prot_for_access_type(access_type)) {
        qemu_log_mask(CPU_LOG_MMU, "%s: good TLB!\n", __func__);
        return 0;
    }

    qemu_log_mask(CPU_LOG_MMU, "%s: no prot match: %x\n", __func__, prot2);
    return access_type == MMU_INST_FETCH ? -3 : -2;
}

static int mmubooke206_get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                            target_ulong address,
                                            MMUAccessType access_type,
                                            int mmu_idx)
{
    ppcmas_tlb_t *tlb;
    hwaddr raddr;
    int i, j, ret;

    ret = -1;
    raddr = (hwaddr)-1ULL;

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int ways = booke206_tlb_ways(env, i);

        for (j = 0; j < ways; j++) {
            tlb = booke206_get_tlbm(env, i, address, j);
            if (!tlb) {
                continue;
            }
            ret = mmubooke206_check_tlb(env, tlb, &raddr, &ctx->prot, address,
                                        access_type, mmu_idx);
            if (ret != -1) {
                goto found_tlb;
            }
        }
    }

found_tlb:

    if (ret >= 0) {
        ctx->raddr = raddr;
         qemu_log_mask(CPU_LOG_MMU, "%s: access granted " TARGET_FMT_lx
                       " => " HWADDR_FMT_plx " %d %d\n", __func__, address,
                       ctx->raddr, ctx->prot, ret);
    } else {
         qemu_log_mask(CPU_LOG_MMU, "%s: access refused " TARGET_FMT_lx
                       " => " HWADDR_FMT_plx " %d %d\n", __func__, address,
                       raddr, ctx->prot, ret);
    }

    return ret;
}

static const char *book3e_tsize_to_str[32] = {
    "1K", "2K", "4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K",
    "1M", "2M", "4M", "8M", "16M", "32M", "64M", "128M", "256M", "512M",
    "1G", "2G", "4G", "8G", "16G", "32G", "64G", "128G", "256G", "512G",
    "1T", "2T"
};

static void mmubooke_dump_mmu(CPUPPCState *env)
{
    ppcemb_tlb_t *entry;
    int i;

#ifdef CONFIG_KVM
    if (kvm_enabled() && !env->kvm_sw_tlb) {
        qemu_printf("Cannot access KVM TLB\n");
        return;
    }
#endif

    qemu_printf("\nTLB:\n");
    qemu_printf("Effective          Physical           Size PID   Prot     "
                "Attr\n");

    entry = &env->tlb.tlbe[0];
    for (i = 0; i < env->nb_tlb; i++, entry++) {
        hwaddr ea, pa;
        target_ulong mask;
        uint64_t size = (uint64_t)entry->size;
        char size_buf[20];

        /* Check valid flag */
        if (!(entry->prot & PAGE_VALID)) {
            continue;
        }

        mask = ~(entry->size - 1);
        ea = entry->EPN & mask;
        pa = entry->RPN & mask;
        /* Extend the physical address to 36 bits */
        pa |= (hwaddr)(entry->RPN & 0xF) << 32;
        if (size >= 1 * MiB) {
            snprintf(size_buf, sizeof(size_buf), "%3" PRId64 "M", size / MiB);
        } else {
            snprintf(size_buf, sizeof(size_buf), "%3" PRId64 "k", size / KiB);
        }
        qemu_printf("0x%016" PRIx64 " 0x%016" PRIx64 " %s %-5u %08x %08x\n",
                    (uint64_t)ea, (uint64_t)pa, size_buf, (uint32_t)entry->PID,
                    entry->prot, entry->attr);
    }

}

static void mmubooke206_dump_one_tlb(CPUPPCState *env, int tlbn, int offset,
                                     int tlbsize)
{
    ppcmas_tlb_t *entry;
    int i;

    qemu_printf("\nTLB%d:\n", tlbn);
    qemu_printf("Effective          Physical           Size TID   TS SRWX"
                " URWX WIMGE U0123\n");

    entry = &env->tlb.tlbm[offset];
    for (i = 0; i < tlbsize; i++, entry++) {
        hwaddr ea, pa, size;
        int tsize;

        if (!(entry->mas1 & MAS1_VALID)) {
            continue;
        }

        tsize = (entry->mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
        size = 1024ULL << tsize;
        ea = entry->mas2 & ~(size - 1);
        pa = entry->mas7_3 & ~(size - 1);

        qemu_printf("0x%016" PRIx64 " 0x%016" PRIx64 " %4s %-5u %1u  S%c%c%c"
                    " U%c%c%c %c%c%c%c%c U%c%c%c%c\n",
                    (uint64_t)ea, (uint64_t)pa,
                    book3e_tsize_to_str[tsize],
                    (entry->mas1 & MAS1_TID_MASK) >> MAS1_TID_SHIFT,
                    (entry->mas1 & MAS1_TS) >> MAS1_TS_SHIFT,
                    entry->mas7_3 & MAS3_SR ? 'R' : '-',
                    entry->mas7_3 & MAS3_SW ? 'W' : '-',
                    entry->mas7_3 & MAS3_SX ? 'X' : '-',
                    entry->mas7_3 & MAS3_UR ? 'R' : '-',
                    entry->mas7_3 & MAS3_UW ? 'W' : '-',
                    entry->mas7_3 & MAS3_UX ? 'X' : '-',
                    entry->mas2 & MAS2_W ? 'W' : '-',
                    entry->mas2 & MAS2_I ? 'I' : '-',
                    entry->mas2 & MAS2_M ? 'M' : '-',
                    entry->mas2 & MAS2_G ? 'G' : '-',
                    entry->mas2 & MAS2_E ? 'E' : '-',
                    entry->mas7_3 & MAS3_U0 ? '0' : '-',
                    entry->mas7_3 & MAS3_U1 ? '1' : '-',
                    entry->mas7_3 & MAS3_U2 ? '2' : '-',
                    entry->mas7_3 & MAS3_U3 ? '3' : '-');
    }
}

static void mmubooke206_dump_mmu(CPUPPCState *env)
{
    int offset = 0;
    int i;

#ifdef CONFIG_KVM
    if (kvm_enabled() && !env->kvm_sw_tlb) {
        qemu_printf("Cannot access KVM TLB\n");
        return;
    }
#endif

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int size = booke206_tlb_size(env, i);

        if (size == 0) {
            continue;
        }

        mmubooke206_dump_one_tlb(env, i, offset, size);
        offset += size;
    }
}

static void mmu6xx_dump_BATs(CPUPPCState *env, int type)
{
    target_ulong *BATlt, *BATut, *BATu, *BATl;
    target_ulong BEPIl, BEPIu, bl;
    int i;

    switch (type) {
    case ACCESS_CODE:
        BATlt = env->IBAT[1];
        BATut = env->IBAT[0];
        break;
    default:
        BATlt = env->DBAT[1];
        BATut = env->DBAT[0];
        break;
    }

    for (i = 0; i < env->nb_BATs; i++) {
        BATu = &BATut[i];
        BATl = &BATlt[i];
        BEPIu = *BATu & 0xF0000000;
        BEPIl = *BATu & 0x0FFE0000;
        bl = (*BATu & 0x00001FFC) << 15;
        qemu_printf("%s BAT%d BATu " TARGET_FMT_lx
                    " BATl " TARGET_FMT_lx "\n\t" TARGET_FMT_lx " "
                    TARGET_FMT_lx " " TARGET_FMT_lx "\n",
                    type == ACCESS_CODE ? "code" : "data", i,
                    *BATu, *BATl, BEPIu, BEPIl, bl);
    }
}

static void mmu6xx_dump_mmu(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    ppc6xx_tlb_t *tlb;
    target_ulong sr;
    int type, way, entry, i;

    qemu_printf("HTAB base = 0x%"HWADDR_PRIx"\n", ppc_hash32_hpt_base(cpu));
    qemu_printf("HTAB mask = 0x%"HWADDR_PRIx"\n", ppc_hash32_hpt_mask(cpu));

    qemu_printf("\nSegment registers:\n");
    for (i = 0; i < 32; i++) {
        sr = env->sr[i];
        if (sr & 0x80000000) {
            qemu_printf("%02d T=%d Ks=%d Kp=%d BUID=0x%03x "
                        "CNTLR_SPEC=0x%05x\n", i,
                        sr & 0x80000000 ? 1 : 0, sr & 0x40000000 ? 1 : 0,
                        sr & 0x20000000 ? 1 : 0, (uint32_t)((sr >> 20) & 0x1FF),
                        (uint32_t)(sr & 0xFFFFF));
        } else {
            qemu_printf("%02d T=%d Ks=%d Kp=%d N=%d VSID=0x%06x\n", i,
                        sr & 0x80000000 ? 1 : 0, sr & 0x40000000 ? 1 : 0,
                        sr & 0x20000000 ? 1 : 0, sr & 0x10000000 ? 1 : 0,
                        (uint32_t)(sr & 0x00FFFFFF));
        }
    }

    qemu_printf("\nBATs:\n");
    mmu6xx_dump_BATs(env, ACCESS_INT);
    mmu6xx_dump_BATs(env, ACCESS_CODE);

    if (env->id_tlbs != 1) {
        qemu_printf("ERROR: 6xx MMU should have separated TLB"
                    " for code and data\n");
    }

    qemu_printf("\nTLBs                       [EPN    EPN + SIZE]\n");

    for (type = 0; type < 2; type++) {
        for (way = 0; way < env->nb_ways; way++) {
            for (entry = env->nb_tlb * type + env->tlb_per_way * way;
                 entry < (env->nb_tlb * type + env->tlb_per_way * (way + 1));
                 entry++) {

                tlb = &env->tlb.tlb6[entry];
                qemu_printf("%s TLB %02d/%02d way:%d %s ["
                            TARGET_FMT_lx " " TARGET_FMT_lx "]\n",
                            type ? "code" : "data", entry % env->nb_tlb,
                            env->nb_tlb, way,
                            pte_is_valid(tlb->pte0) ? "valid" : "inval",
                            tlb->EPN, tlb->EPN + TARGET_PAGE_SIZE);
            }
        }
    }
}

void dump_mmu(CPUPPCState *env)
{
    switch (env->mmu_model) {
    case POWERPC_MMU_BOOKE:
        mmubooke_dump_mmu(env);
        break;
    case POWERPC_MMU_BOOKE206:
        mmubooke206_dump_mmu(env);
        break;
    case POWERPC_MMU_SOFT_6xx:
        mmu6xx_dump_mmu(env);
        break;
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_07:
        dump_slb(env_archcpu(env));
        break;
    case POWERPC_MMU_3_00:
        if (ppc64_v3_radix(env_archcpu(env))) {
            qemu_log_mask(LOG_UNIMP, "%s: the PPC64 MMU is unsupported\n",
                          __func__);
        } else {
            dump_slb(env_archcpu(env));
        }
        break;
#endif
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented\n", __func__);
    }
}

static int check_physical(CPUPPCState *env, mmu_ctx_t *ctx, target_ulong eaddr,
                          MMUAccessType access_type)
{
    ctx->raddr = eaddr;
    ctx->prot = PAGE_READ | PAGE_EXEC;

    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_4xx:
    case POWERPC_MMU_REAL:
    case POWERPC_MMU_BOOKE:
        ctx->prot |= PAGE_WRITE;
        break;

    default:
        /* Caller's checks mean we should never get here for other models */
        g_assert_not_reached();
    }

    return 0;
}

int get_physical_address_wtlb(CPUPPCState *env, mmu_ctx_t *ctx,
                                     target_ulong eaddr,
                                     MMUAccessType access_type, int type,
                                     int mmu_idx)
{
    int ret = -1;
    bool real_mode = (type == ACCESS_CODE && !FIELD_EX64(env->msr, MSR, IR)) ||
                     (type != ACCESS_CODE && !FIELD_EX64(env->msr, MSR, DR));

    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
        if (real_mode) {
            ret = check_physical(env, ctx, eaddr, access_type);
        } else {
            /* Try to find a BAT */
            if (env->nb_BATs != 0) {
                ret = get_bat_6xx_tlb(env, ctx, eaddr, access_type);
            }
            if (ret < 0) {
                /* We didn't match any BAT entry or don't have BATs */
                ret = get_segment_6xx_tlb(env, ctx, eaddr, access_type, type);
            }
        }
        break;

    case POWERPC_MMU_SOFT_4xx:
        if (real_mode) {
            ret = check_physical(env, ctx, eaddr, access_type);
        } else {
            ret = mmu40x_get_physical_address(env, ctx, eaddr, access_type);
        }
        break;
    case POWERPC_MMU_BOOKE:
        ret = mmubooke_get_physical_address(env, ctx, eaddr, access_type);
        break;
    case POWERPC_MMU_BOOKE206:
        ret = mmubooke206_get_physical_address(env, ctx, eaddr, access_type,
                                               mmu_idx);
        break;
    case POWERPC_MMU_MPC8xx:
        /* XXX: TODO */
        cpu_abort(env_cpu(env), "MPC8xx MMU model is not implemented\n");
        break;
    case POWERPC_MMU_REAL:
        if (real_mode) {
            ret = check_physical(env, ctx, eaddr, access_type);
        } else {
            cpu_abort(env_cpu(env),
                      "PowerPC in real mode do not do any translation\n");
        }
        return -1;
    default:
        cpu_abort(env_cpu(env), "Unknown or invalid MMU model\n");
        return -1;
    }

    return ret;
}

static void booke206_update_mas_tlb_miss(CPUPPCState *env, target_ulong address,
                                         MMUAccessType access_type, int mmu_idx)
{
    uint32_t epid;
    bool as, pr;
    uint32_t missed_tid = 0;
    bool use_epid = mmubooke206_get_as(env, mmu_idx, &epid, &as, &pr);

    if (access_type == MMU_INST_FETCH) {
        as = FIELD_EX64(env->msr, MSR, IR);
    }
    env->spr[SPR_BOOKE_MAS0] = env->spr[SPR_BOOKE_MAS4] & MAS4_TLBSELD_MASK;
    env->spr[SPR_BOOKE_MAS1] = env->spr[SPR_BOOKE_MAS4] & MAS4_TSIZED_MASK;
    env->spr[SPR_BOOKE_MAS2] = env->spr[SPR_BOOKE_MAS4] & MAS4_WIMGED_MASK;
    env->spr[SPR_BOOKE_MAS3] = 0;
    env->spr[SPR_BOOKE_MAS6] = 0;
    env->spr[SPR_BOOKE_MAS7] = 0;

    /* AS */
    if (as) {
        env->spr[SPR_BOOKE_MAS1] |= MAS1_TS;
        env->spr[SPR_BOOKE_MAS6] |= MAS6_SAS;
    }

    env->spr[SPR_BOOKE_MAS1] |= MAS1_VALID;
    env->spr[SPR_BOOKE_MAS2] |= address & MAS2_EPN_MASK;

    if (!use_epid) {
        switch (env->spr[SPR_BOOKE_MAS4] & MAS4_TIDSELD_PIDZ) {
        case MAS4_TIDSELD_PID0:
            missed_tid = env->spr[SPR_BOOKE_PID];
            break;
        case MAS4_TIDSELD_PID1:
            missed_tid = env->spr[SPR_BOOKE_PID1];
            break;
        case MAS4_TIDSELD_PID2:
            missed_tid = env->spr[SPR_BOOKE_PID2];
            break;
        }
        env->spr[SPR_BOOKE_MAS6] |= env->spr[SPR_BOOKE_PID] << 16;
    } else {
        missed_tid = epid;
        env->spr[SPR_BOOKE_MAS6] |= missed_tid << 16;
    }
    env->spr[SPR_BOOKE_MAS1] |= (missed_tid << MAS1_TID_SHIFT);


    /* next victim logic */
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_ESEL_SHIFT;
    env->last_way++;
    env->last_way &= booke206_tlb_ways(env, 0) - 1;
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_NV_SHIFT;
}

/* Perform address translation */
/* TODO: Split this by mmu_model. */
static bool ppc_jumbo_xlate(PowerPCCPU *cpu, vaddr eaddr,
                            MMUAccessType access_type,
                            hwaddr *raddrp, int *psizep, int *protp,
                            int mmu_idx, bool guest_visible)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    mmu_ctx_t ctx;
    int type;
    int ret;

    if (access_type == MMU_INST_FETCH) {
        /* code access */
        type = ACCESS_CODE;
    } else if (guest_visible) {
        /* data access */
        type = env->access_type;
    } else {
        type = ACCESS_INT;
    }

    ret = get_physical_address_wtlb(env, &ctx, eaddr, access_type,
                                    type, mmu_idx);
    if (ret == 0) {
        *raddrp = ctx.raddr;
        *protp = ctx.prot;
        *psizep = TARGET_PAGE_BITS;
        return true;
    }

    if (guest_visible) {
        log_cpu_state_mask(CPU_LOG_MMU, cs, 0);
        if (type == ACCESS_CODE) {
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                switch (env->mmu_model) {
                case POWERPC_MMU_SOFT_6xx:
                    cs->exception_index = POWERPC_EXCP_IFTLB;
                    env->error_code = 1 << 18;
                    env->spr[SPR_IMISS] = eaddr;
                    env->spr[SPR_ICMP] = 0x80000000 | ctx.ptem;
                    goto tlb_miss;
                case POWERPC_MMU_SOFT_4xx:
                    cs->exception_index = POWERPC_EXCP_ITLB;
                    env->error_code = 0;
                    env->spr[SPR_40x_DEAR] = eaddr;
                    env->spr[SPR_40x_ESR] = 0x00000000;
                    break;
                case POWERPC_MMU_BOOKE206:
                    booke206_update_mas_tlb_miss(env, eaddr, 2, mmu_idx);
                    /* fall through */
                case POWERPC_MMU_BOOKE:
                    cs->exception_index = POWERPC_EXCP_ITLB;
                    env->error_code = 0;
                    env->spr[SPR_BOOKE_DEAR] = eaddr;
                    env->spr[SPR_BOOKE_ESR] = mmubooke206_esr(mmu_idx, MMU_DATA_LOAD);
                    break;
                case POWERPC_MMU_MPC8xx:
                    cpu_abort(cs, "MPC8xx MMU model is not implemented\n");
                case POWERPC_MMU_REAL:
                    cpu_abort(cs, "PowerPC in real mode should never raise "
                              "any MMU exceptions\n");
                default:
                    cpu_abort(cs, "Unknown or invalid MMU model\n");
                }
                break;
            case -2:
                /* Access rights violation */
                cs->exception_index = POWERPC_EXCP_ISI;
                if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
                    (env->mmu_model == POWERPC_MMU_BOOKE206)) {
                    env->error_code = 0;
                } else {
                    env->error_code = 0x08000000;
                }
                break;
            case -3:
                /* No execute protection violation */
                if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
                    (env->mmu_model == POWERPC_MMU_BOOKE206)) {
                    env->spr[SPR_BOOKE_ESR] = 0x00000000;
                    env->error_code = 0;
                } else {
                    env->error_code = 0x10000000;
                }
                cs->exception_index = POWERPC_EXCP_ISI;
                break;
            case -4:
                /* Direct store exception */
                /* No code fetch is allowed in direct-store areas */
                cs->exception_index = POWERPC_EXCP_ISI;
                if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
                    (env->mmu_model == POWERPC_MMU_BOOKE206)) {
                    env->error_code = 0;
                } else {
                    env->error_code = 0x10000000;
                }
                break;
            }
        } else {
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                switch (env->mmu_model) {
                case POWERPC_MMU_SOFT_6xx:
                    if (access_type == MMU_DATA_STORE) {
                        cs->exception_index = POWERPC_EXCP_DSTLB;
                        env->error_code = 1 << 16;
                    } else {
                        cs->exception_index = POWERPC_EXCP_DLTLB;
                        env->error_code = 0;
                    }
                    env->spr[SPR_DMISS] = eaddr;
                    env->spr[SPR_DCMP] = 0x80000000 | ctx.ptem;
                tlb_miss:
                    env->error_code |= ctx.key << 19;
                    env->spr[SPR_HASH1] = ppc_hash32_hpt_base(cpu) +
                        get_pteg_offset32(cpu, ctx.hash[0]);
                    env->spr[SPR_HASH2] = ppc_hash32_hpt_base(cpu) +
                        get_pteg_offset32(cpu, ctx.hash[1]);
                    break;
                case POWERPC_MMU_SOFT_4xx:
                    cs->exception_index = POWERPC_EXCP_DTLB;
                    env->error_code = 0;
                    env->spr[SPR_40x_DEAR] = eaddr;
                    if (access_type == MMU_DATA_STORE) {
                        env->spr[SPR_40x_ESR] = 0x00800000;
                    } else {
                        env->spr[SPR_40x_ESR] = 0x00000000;
                    }
                    break;
                case POWERPC_MMU_MPC8xx:
                    /* XXX: TODO */
                    cpu_abort(cs, "MPC8xx MMU model is not implemented\n");
                case POWERPC_MMU_BOOKE206:
                    booke206_update_mas_tlb_miss(env, eaddr, access_type, mmu_idx);
                    /* fall through */
                case POWERPC_MMU_BOOKE:
                    cs->exception_index = POWERPC_EXCP_DTLB;
                    env->error_code = 0;
                    env->spr[SPR_BOOKE_DEAR] = eaddr;
                    env->spr[SPR_BOOKE_ESR] = mmubooke206_esr(mmu_idx, access_type);
                    break;
                case POWERPC_MMU_REAL:
                    cpu_abort(cs, "PowerPC in real mode should never raise "
                              "any MMU exceptions\n");
                default:
                    cpu_abort(cs, "Unknown or invalid MMU model\n");
                }
                break;
            case -2:
                /* Access rights violation */
                cs->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                if (env->mmu_model == POWERPC_MMU_SOFT_4xx) {
                    env->spr[SPR_40x_DEAR] = eaddr;
                    if (access_type == MMU_DATA_STORE) {
                        env->spr[SPR_40x_ESR] |= 0x00800000;
                    }
                } else if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
                           (env->mmu_model == POWERPC_MMU_BOOKE206)) {
                    env->spr[SPR_BOOKE_DEAR] = eaddr;
                    env->spr[SPR_BOOKE_ESR] = mmubooke206_esr(mmu_idx, access_type);
                } else {
                    env->spr[SPR_DAR] = eaddr;
                    if (access_type == MMU_DATA_STORE) {
                        env->spr[SPR_DSISR] = 0x0A000000;
                    } else {
                        env->spr[SPR_DSISR] = 0x08000000;
                    }
                }
                break;
            case -4:
                /* Direct store exception */
                switch (type) {
                case ACCESS_FLOAT:
                    /* Floating point load/store */
                    cs->exception_index = POWERPC_EXCP_ALIGN;
                    env->error_code = POWERPC_EXCP_ALIGN_FP;
                    env->spr[SPR_DAR] = eaddr;
                    break;
                case ACCESS_RES:
                    /* lwarx, ldarx or stwcx. */
                    cs->exception_index = POWERPC_EXCP_DSI;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = eaddr;
                    if (access_type == MMU_DATA_STORE) {
                        env->spr[SPR_DSISR] = 0x06000000;
                    } else {
                        env->spr[SPR_DSISR] = 0x04000000;
                    }
                    break;
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
                    break;
                default:
                    printf("DSI: invalid exception (%d)\n", ret);
                    cs->exception_index = POWERPC_EXCP_PROGRAM;
                    env->error_code =
                        POWERPC_EXCP_INVAL | POWERPC_EXCP_INVAL_INVAL;
                    env->spr[SPR_DAR] = eaddr;
                    break;
                }
                break;
            }
        }
    }
    return false;
}

/*****************************************************************************/

bool ppc_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                      hwaddr *raddrp, int *psizep, int *protp,
                      int mmu_idx, bool guest_visible)
{
    switch (cpu->env.mmu_model) {
#if defined(TARGET_PPC64)
    case POWERPC_MMU_3_00:
        if (ppc64_v3_radix(cpu)) {
            return ppc_radix64_xlate(cpu, eaddr, access_type, raddrp,
                                     psizep, protp, mmu_idx, guest_visible);
        }
        /* fall through */
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_07:
        return ppc_hash64_xlate(cpu, eaddr, access_type,
                                raddrp, psizep, protp, mmu_idx, guest_visible);
#endif

    case POWERPC_MMU_32B:
        return ppc_hash32_xlate(cpu, eaddr, access_type, raddrp,
                               psizep, protp, mmu_idx, guest_visible);

    default:
        return ppc_jumbo_xlate(cpu, eaddr, access_type, raddrp,
                               psizep, protp, mmu_idx, guest_visible);
    }
}

hwaddr ppc_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    hwaddr raddr;
    int s, p;

    /*
     * Some MMUs have separate TLBs for code and data. If we only
     * try an MMU_DATA_LOAD, we may not be able to read instructions
     * mapped by code TLBs, so we also try a MMU_INST_FETCH.
     */
    if (ppc_xlate(cpu, addr, MMU_DATA_LOAD, &raddr, &s, &p,
                  cpu_mmu_index(&cpu->env, false), false) ||
        ppc_xlate(cpu, addr, MMU_INST_FETCH, &raddr, &s, &p,
                  cpu_mmu_index(&cpu->env, true), false)) {
        return raddr & TARGET_PAGE_MASK;
    }
    return -1;
}
