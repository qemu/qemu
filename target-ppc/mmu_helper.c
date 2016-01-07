/*
 *  PowerPC MMU, TLB, SLB and BAT emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "exec/helper-proto.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "mmu-hash32.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"

//#define DEBUG_MMU
//#define DEBUG_BATS
//#define DEBUG_SOFTWARE_TLB
//#define DUMP_PAGE_TABLES
//#define FLUSH_ALL_TLBS

#ifdef DEBUG_MMU
#  define LOG_MMU_STATE(cpu) log_cpu_state_mask(CPU_LOG_MMU, (cpu), 0)
#else
#  define LOG_MMU_STATE(cpu) do { } while (0)
#endif

#ifdef DEBUG_SOFTWARE_TLB
#  define LOG_SWTLB(...) qemu_log_mask(CPU_LOG_MMU, __VA_ARGS__)
#else
#  define LOG_SWTLB(...) do { } while (0)
#endif

#ifdef DEBUG_BATS
#  define LOG_BATS(...) qemu_log_mask(CPU_LOG_MMU, __VA_ARGS__)
#else
#  define LOG_BATS(...) do { } while (0)
#endif

/*****************************************************************************/
/* PowerPC MMU emulation */

/* Context used internally during MMU translations */
typedef struct mmu_ctx_t mmu_ctx_t;
struct mmu_ctx_t {
    hwaddr raddr;      /* Real address              */
    hwaddr eaddr;      /* Effective address         */
    int prot;                      /* Protection bits           */
    hwaddr hash[2];    /* Pagetable hash values     */
    target_ulong ptem;             /* Virtual segment ID | API  */
    int key;                       /* Access key                */
    int nx;                        /* Non-execute area          */
};

/* Common routines used by software and hardware TLBs emulation */
static inline int pte_is_valid(target_ulong pte0)
{
    return pte0 & 0x80000000 ? 1 : 0;
}

static inline void pte_invalidate(target_ulong *pte0)
{
    *pte0 &= ~0x80000000;
}

#define PTE_PTEM_MASK 0x7FFFFFBF
#define PTE_CHECK_MASK (TARGET_PAGE_MASK | 0x7B)

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
            /* No break here */
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

static int check_prot(int prot, int rw, int access_type)
{
    int ret;

    if (access_type == ACCESS_CODE) {
        if (prot & PAGE_EXEC) {
            ret = 0;
        } else {
            ret = -2;
        }
    } else if (rw) {
        if (prot & PAGE_WRITE) {
            ret = 0;
        } else {
            ret = -2;
        }
    } else {
        if (prot & PAGE_READ) {
            ret = 0;
        } else {
            ret = -2;
        }
    }

    return ret;
}

static inline int ppc6xx_tlb_pte_check(mmu_ctx_t *ctx, target_ulong pte0,
                                       target_ulong pte1, int h, int rw, int type)
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
            /* Keep the matching PTE informations */
            ctx->raddr = pte1;
            ctx->prot = access;
            ret = check_prot(ctx->prot, rw, type);
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
                            int ret, int rw)
{
    int store = 0;

    /* Update page flags */
    if (!(*pte1p & 0x00000100)) {
        /* Update accessed flag */
        *pte1p |= 0x00000100;
        store = 1;
    }
    if (!(*pte1p & 0x00000080)) {
        if (rw == 1 && ret == 0) {
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
static inline int ppc6xx_tlb_getnum(CPUPPCState *env, target_ulong eaddr,
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

static inline void ppc6xx_tlb_invalidate_all(CPUPPCState *env)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    ppc6xx_tlb_t *tlb;
    int nr, max;

    /* LOG_SWTLB("Invalidate all TLBs\n"); */
    /* Invalidate all defined software TLB */
    max = env->nb_tlb;
    if (env->id_tlbs == 1) {
        max *= 2;
    }
    for (nr = 0; nr < max; nr++) {
        tlb = &env->tlb.tlb6[nr];
        pte_invalidate(&tlb->pte0);
    }
    tlb_flush(CPU(cpu), 1);
}

static inline void ppc6xx_tlb_invalidate_virt2(CPUPPCState *env,
                                               target_ulong eaddr,
                                               int is_code, int match_epn)
{
#if !defined(FLUSH_ALL_TLBS)
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    ppc6xx_tlb_t *tlb;
    int way, nr;

    /* Invalidate ITLB + DTLB, all ways */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way, is_code);
        tlb = &env->tlb.tlb6[nr];
        if (pte_is_valid(tlb->pte0) && (match_epn == 0 || eaddr == tlb->EPN)) {
            LOG_SWTLB("TLB invalidate %d/%d " TARGET_FMT_lx "\n", nr,
                      env->nb_tlb, eaddr);
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
    LOG_SWTLB("Set TLB %d/%d EPN " TARGET_FMT_lx " PTE0 " TARGET_FMT_lx
              " PTE1 " TARGET_FMT_lx "\n", nr, env->nb_tlb, EPN, pte0, pte1);
    /* Invalidate any pending reference in QEMU for this virtual address */
    ppc6xx_tlb_invalidate_virt2(env, EPN, is_code, 1);
    tlb->pte0 = pte0;
    tlb->pte1 = pte1;
    tlb->EPN = EPN;
    /* Store last way for LRU mechanism */
    env->last_way = way;
}

static inline int ppc6xx_tlb_check(CPUPPCState *env, mmu_ctx_t *ctx,
                                   target_ulong eaddr, int rw, int access_type)
{
    ppc6xx_tlb_t *tlb;
    int nr, best, way;
    int ret;

    best = -1;
    ret = -1; /* No TLB found */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way,
                               access_type == ACCESS_CODE ? 1 : 0);
        tlb = &env->tlb.tlb6[nr];
        /* This test "emulates" the PTE index match for hardware TLBs */
        if ((eaddr & TARGET_PAGE_MASK) != tlb->EPN) {
            LOG_SWTLB("TLB %d/%d %s [" TARGET_FMT_lx " " TARGET_FMT_lx
                      "] <> " TARGET_FMT_lx "\n", nr, env->nb_tlb,
                      pte_is_valid(tlb->pte0) ? "valid" : "inval",
                      tlb->EPN, tlb->EPN + TARGET_PAGE_SIZE, eaddr);
            continue;
        }
        LOG_SWTLB("TLB %d/%d %s " TARGET_FMT_lx " <> " TARGET_FMT_lx " "
                  TARGET_FMT_lx " %c %c\n", nr, env->nb_tlb,
                  pte_is_valid(tlb->pte0) ? "valid" : "inval",
                  tlb->EPN, eaddr, tlb->pte1,
                  rw ? 'S' : 'L', access_type == ACCESS_CODE ? 'I' : 'D');
        switch (ppc6xx_tlb_pte_check(ctx, tlb->pte0, tlb->pte1, 0, rw, access_type)) {
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
            /* XXX: we should go on looping to check all TLBs consistency
             *      but we can speed-up the whole thing as the
             *      result would be undefined if TLBs are not consistent.
             */
            ret = 0;
            best = nr;
            goto done;
        }
    }
    if (best != -1) {
    done:
        LOG_SWTLB("found TLB at addr " TARGET_FMT_plx " prot=%01x ret=%d\n",
                  ctx->raddr & TARGET_PAGE_MASK, ctx->prot, ret);
        /* Update page flags */
        pte_update_flags(ctx, &env->tlb.tlb6[best].pte1, ret, rw);
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
    if (((msr_pr == 0) && (*BATu & 0x00000002)) ||
        ((msr_pr != 0) && (*BATu & 0x00000001))) {
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
                           target_ulong virtual, int rw, int type)
{
    target_ulong *BATlt, *BATut, *BATu, *BATl;
    target_ulong BEPIl, BEPIu, bl;
    int i, valid, prot;
    int ret = -1;

    LOG_BATS("%s: %cBAT v " TARGET_FMT_lx "\n", __func__,
             type == ACCESS_CODE ? 'I' : 'D', virtual);
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
        bat_size_prot(env, &bl, &valid, &prot, BATu, BATl);
        LOG_BATS("%s: %cBAT%d v " TARGET_FMT_lx " BATu " TARGET_FMT_lx
                 " BATl " TARGET_FMT_lx "\n", __func__,
                 type == ACCESS_CODE ? 'I' : 'D', i, virtual, *BATu, *BATl);
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
                ret = check_prot(ctx->prot, rw, type);
                if (ret == 0) {
                    LOG_BATS("BAT %d match: r " TARGET_FMT_plx " prot=%c%c\n",
                             i, ctx->raddr, ctx->prot & PAGE_READ ? 'R' : '-',
                             ctx->prot & PAGE_WRITE ? 'W' : '-');
                }
                break;
            }
        }
    }
    if (ret < 0) {
#if defined(DEBUG_BATS)
        if (qemu_log_enabled()) {
            LOG_BATS("no BAT match for " TARGET_FMT_lx ":\n", virtual);
            for (i = 0; i < 4; i++) {
                BATu = &BATut[i];
                BATl = &BATlt[i];
                BEPIu = *BATu & 0xF0000000;
                BEPIl = *BATu & 0x0FFE0000;
                bl = (*BATu & 0x00001FFC) << 15;
                LOG_BATS("%s: %cBAT%d v " TARGET_FMT_lx " BATu " TARGET_FMT_lx
                         " BATl " TARGET_FMT_lx "\n\t" TARGET_FMT_lx " "
                         TARGET_FMT_lx " " TARGET_FMT_lx "\n",
                         __func__, type == ACCESS_CODE ? 'I' : 'D', i, virtual,
                         *BATu, *BATl, BEPIu, BEPIl, bl);
            }
        }
#endif
    }
    /* No hit */
    return ret;
}

/* Perform segment based translation */
static inline int get_segment_6xx_tlb(CPUPPCState *env, mmu_ctx_t *ctx,
                                      target_ulong eaddr, int rw, int type)
{
    hwaddr hash;
    target_ulong vsid;
    int ds, pr, target_page_bits;
    int ret;
    target_ulong sr, pgidx;

    pr = msr_pr;
    ctx->eaddr = eaddr;

    sr = env->sr[eaddr >> 28];
    ctx->key = (((sr & 0x20000000) && (pr != 0)) ||
                ((sr & 0x40000000) && (pr == 0))) ? 1 : 0;
    ds = sr & 0x80000000 ? 1 : 0;
    ctx->nx = sr & 0x10000000 ? 1 : 0;
    vsid = sr & 0x00FFFFFF;
    target_page_bits = TARGET_PAGE_BITS;
    qemu_log_mask(CPU_LOG_MMU,
            "Check segment v=" TARGET_FMT_lx " %d " TARGET_FMT_lx
            " nip=" TARGET_FMT_lx " lr=" TARGET_FMT_lx
            " ir=%d dr=%d pr=%d %d t=%d\n",
            eaddr, (int)(eaddr >> 28), sr, env->nip, env->lr, (int)msr_ir,
            (int)msr_dr, pr != 0 ? 1 : 0, rw, type);
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
            qemu_log_mask(CPU_LOG_MMU, "htab_base " TARGET_FMT_plx
                    " htab_mask " TARGET_FMT_plx
                    " hash " TARGET_FMT_plx "\n",
                    env->htab_base, env->htab_mask, hash);
            ctx->hash[0] = hash;
            ctx->hash[1] = ~hash;

            /* Initialize real address with an invalid value */
            ctx->raddr = (hwaddr)-1ULL;
            /* Software TLB search */
            ret = ppc6xx_tlb_check(env, ctx, eaddr, rw, type);
#if defined(DUMP_PAGE_TABLES)
            if (qemu_log_mask(CPU_LOG_MMU)) {
                hwaddr curaddr;
                uint32_t a0, a1, a2, a3;

                qemu_log("Page table: " TARGET_FMT_plx " len " TARGET_FMT_plx
                         "\n", sdr, mask + 0x80);
                for (curaddr = sdr; curaddr < (sdr + mask + 0x80);
                     curaddr += 16) {
                    a0 = ldl_phys(curaddr);
                    a1 = ldl_phys(curaddr + 4);
                    a2 = ldl_phys(curaddr + 8);
                    a3 = ldl_phys(curaddr + 12);
                    if (a0 != 0 || a1 != 0 || a2 != 0 || a3 != 0) {
                        qemu_log(TARGET_FMT_plx ": %08x %08x %08x %08x\n",
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
        target_ulong sr;

        qemu_log_mask(CPU_LOG_MMU, "direct store...\n");
        /* Direct-store segment : absolutely *BUGGY* for now */

        /* Direct-store implies a 32-bit MMU.
         * Check the Segment Register's bus unit ID (BUID).
         */
        sr = env->sr[eaddr >> 28];
        if ((sr & 0x1FF00000) >> 20 == 0x07f) {
            /* Memory-forced I/O controller interface access */
            /* If T=1 and BUID=x'07F', the 601 performs a memory access
             * to SR[28-31] LA[4-31], bypassing all protection mechanisms.
             */
            ctx->raddr = ((sr & 0xF) << 28) | (eaddr & 0x0FFFFFFF);
            ctx->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            return 0;
        }

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
            /* dcba, dcbt, dcbtst, dcbf, dcbi, dcbst, dcbz, or icbi */
            /* Should make the instruction do no-op.
             * As it already do no-op, it's quite easy :-)
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
        if ((rw == 1 || ctx->key != 1) && (rw == 0 || ctx->key != 0)) {
            ctx->raddr = eaddr;
            ret = 2;
        } else {
            ret = -2;
        }
    }

    return ret;
}

/* Generic TLB check function for embedded PowerPC implementations */
static int ppcemb_tlb_check(CPUPPCState *env, ppcemb_tlb_t *tlb,
                            hwaddr *raddrp,
                            target_ulong address, uint32_t pid, int ext,
                            int i)
{
    target_ulong mask;

    /* Check valid flag */
    if (!(tlb->prot & PAGE_VALID)) {
        return -1;
    }
    mask = ~(tlb->size - 1);
    LOG_SWTLB("%s: TLB %d address " TARGET_FMT_lx " PID %u <=> " TARGET_FMT_lx
              " " TARGET_FMT_lx " %u %x\n", __func__, i, address, pid, tlb->EPN,
              mask, (uint32_t)tlb->PID, tlb->prot);
    /* Check PID */
    if (tlb->PID != 0 && tlb->PID != pid) {
        return -1;
    }
    /* Check effective address */
    if ((address & mask) != tlb->EPN) {
        return -1;
    }
    *raddrp = (tlb->RPN & mask) | (address & ~mask);
    if (ext) {
        /* Extend the physical address to 36 bits */
        *raddrp |= (uint64_t)(tlb->RPN & 0xF) << 32;
    }

    return 0;
}

/* Generic TLB search function for PowerPC embedded implementations */
static int ppcemb_tlb_search(CPUPPCState *env, target_ulong address,
                             uint32_t pid)
{
    ppcemb_tlb_t *tlb;
    hwaddr raddr;
    int i, ret;

    /* Default return value is no match */
    ret = -1;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb.tlbe[i];
        if (ppcemb_tlb_check(env, tlb, &raddr, address, pid, 0, i) == 0) {
            ret = i;
            break;
        }
    }

    return ret;
}

/* Helpers specific to PowerPC 40x implementations */
static inline void ppc4xx_tlb_invalidate_all(CPUPPCState *env)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    ppcemb_tlb_t *tlb;
    int i;

    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb.tlbe[i];
        tlb->prot &= ~PAGE_VALID;
    }
    tlb_flush(CPU(cpu), 1);
}

static int mmu40x_get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                       target_ulong address, int rw,
                                       int access_type)
{
    ppcemb_tlb_t *tlb;
    hwaddr raddr;
    int i, ret, zsel, zpr, pr;

    ret = -1;
    raddr = (hwaddr)-1ULL;
    pr = msr_pr;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb.tlbe[i];
        if (ppcemb_tlb_check(env, tlb, &raddr, address,
                             env->spr[SPR_40x_PID], 0, i) < 0) {
            continue;
        }
        zsel = (tlb->attr >> 4) & 0xF;
        zpr = (env->spr[SPR_40x_ZPR] >> (30 - (2 * zsel))) & 0x3;
        LOG_SWTLB("%s: TLB %d zsel %d zpr %d rw %d attr %08x\n",
                    __func__, i, zsel, zpr, rw, tlb->attr);
        /* Check execute enable bit */
        switch (zpr) {
        case 0x2:
            if (pr != 0) {
                goto check_perms;
            }
            /* No break here */
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
            /* No break here */
        case 0x1:
        check_perms:
            /* Check from TLB entry */
            ctx->prot = tlb->prot;
            ret = check_prot(ctx->prot, rw, access_type);
            if (ret == -2) {
                env->spr[SPR_40x_ESR] = 0;
            }
            break;
        }
        if (ret >= 0) {
            ctx->raddr = raddr;
            LOG_SWTLB("%s: access granted " TARGET_FMT_lx " => " TARGET_FMT_plx
                      " %d %d\n", __func__, address, ctx->raddr, ctx->prot,
                      ret);
            return 0;
        }
    }
    LOG_SWTLB("%s: access refused " TARGET_FMT_lx " => " TARGET_FMT_plx
              " %d %d\n", __func__, address, raddr, ctx->prot, ret);

    return ret;
}

void store_40x_sler(CPUPPCState *env, uint32_t val)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    /* XXX: TO BE FIXED */
    if (val != 0x00000000) {
        cpu_abort(CPU(cpu), "Little-endian regions are not supported by now\n");
    }
    env->spr[SPR_405_SLER] = val;
}

static inline int mmubooke_check_tlb(CPUPPCState *env, ppcemb_tlb_t *tlb,
                                     hwaddr *raddr, int *prot,
                                     target_ulong address, int rw,
                                     int access_type, int i)
{
    int ret, prot2;

    if (ppcemb_tlb_check(env, tlb, raddr, address,
                         env->spr[SPR_BOOKE_PID],
                         !env->nb_pids, i) >= 0) {
        goto found_tlb;
    }

    if (env->spr[SPR_BOOKE_PID1] &&
        ppcemb_tlb_check(env, tlb, raddr, address,
                         env->spr[SPR_BOOKE_PID1], 0, i) >= 0) {
        goto found_tlb;
    }

    if (env->spr[SPR_BOOKE_PID2] &&
        ppcemb_tlb_check(env, tlb, raddr, address,
                         env->spr[SPR_BOOKE_PID2], 0, i) >= 0) {
        goto found_tlb;
    }

    LOG_SWTLB("%s: TLB entry not found\n", __func__);
    return -1;

found_tlb:

    if (msr_pr != 0) {
        prot2 = tlb->prot & 0xF;
    } else {
        prot2 = (tlb->prot >> 4) & 0xF;
    }

    /* Check the address space */
    if (access_type == ACCESS_CODE) {
        if (msr_ir != (tlb->attr & 1)) {
            LOG_SWTLB("%s: AS doesn't match\n", __func__);
            return -1;
        }

        *prot = prot2;
        if (prot2 & PAGE_EXEC) {
            LOG_SWTLB("%s: good TLB!\n", __func__);
            return 0;
        }

        LOG_SWTLB("%s: no PAGE_EXEC: %x\n", __func__, prot2);
        ret = -3;
    } else {
        if (msr_dr != (tlb->attr & 1)) {
            LOG_SWTLB("%s: AS doesn't match\n", __func__);
            return -1;
        }

        *prot = prot2;
        if ((!rw && prot2 & PAGE_READ) || (rw && (prot2 & PAGE_WRITE))) {
            LOG_SWTLB("%s: found TLB!\n", __func__);
            return 0;
        }

        LOG_SWTLB("%s: PAGE_READ/WRITE doesn't match: %x\n", __func__, prot2);
        ret = -2;
    }

    return ret;
}

static int mmubooke_get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                         target_ulong address, int rw,
                                         int access_type)
{
    ppcemb_tlb_t *tlb;
    hwaddr raddr;
    int i, ret;

    ret = -1;
    raddr = (hwaddr)-1ULL;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb.tlbe[i];
        ret = mmubooke_check_tlb(env, tlb, &raddr, &ctx->prot, address, rw,
                                 access_type, i);
        if (!ret) {
            break;
        }
    }

    if (ret >= 0) {
        ctx->raddr = raddr;
        LOG_SWTLB("%s: access granted " TARGET_FMT_lx " => " TARGET_FMT_plx
                  " %d %d\n", __func__, address, ctx->raddr, ctx->prot,
                  ret);
    } else {
        LOG_SWTLB("%s: access refused " TARGET_FMT_lx " => " TARGET_FMT_plx
                  " %d %d\n", __func__, address, raddr, ctx->prot, ret);
    }

    return ret;
}

static void booke206_flush_tlb(CPUPPCState *env, int flags,
                               const int check_iprot)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
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

    tlb_flush(CPU(cpu), 1);
}

static hwaddr booke206_tlb_to_page_size(CPUPPCState *env,
                                        ppcmas_tlb_t *tlb)
{
    int tlbm_size;

    tlbm_size = (tlb->mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;

    return 1024ULL << tlbm_size;
}

/* TLB check function for MAS based SoftTLBs */
static int ppcmas_tlb_check(CPUPPCState *env, ppcmas_tlb_t *tlb,
                            hwaddr *raddrp, target_ulong address,
                            uint32_t pid)
{
    hwaddr mask;
    uint32_t tlb_pid;

    if (!msr_cm) {
        /* In 32bit mode we can only address 32bit EAs */
        address = (uint32_t)address;
    }

    /* Check valid flag */
    if (!(tlb->mas1 & MAS1_VALID)) {
        return -1;
    }

    mask = ~(booke206_tlb_to_page_size(env, tlb) - 1);
    LOG_SWTLB("%s: TLB ADDR=0x" TARGET_FMT_lx " PID=0x%x MAS1=0x%x MAS2=0x%"
              PRIx64 " mask=0x" TARGET_FMT_lx " MAS7_3=0x%" PRIx64 " MAS8=%x\n",
              __func__, address, pid, tlb->mas1, tlb->mas2, mask, tlb->mas7_3,
              tlb->mas8);

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

static int mmubooke206_check_tlb(CPUPPCState *env, ppcmas_tlb_t *tlb,
                                 hwaddr *raddr, int *prot,
                                 target_ulong address, int rw,
                                 int access_type)
{
    int ret;
    int prot2 = 0;

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

    LOG_SWTLB("%s: TLB entry not found\n", __func__);
    return -1;

found_tlb:

    if (msr_pr != 0) {
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
    if (access_type == ACCESS_CODE) {
        if (msr_ir != ((tlb->mas1 & MAS1_TS) >> MAS1_TS_SHIFT)) {
            LOG_SWTLB("%s: AS doesn't match\n", __func__);
            return -1;
        }

        *prot = prot2;
        if (prot2 & PAGE_EXEC) {
            LOG_SWTLB("%s: good TLB!\n", __func__);
            return 0;
        }

        LOG_SWTLB("%s: no PAGE_EXEC: %x\n", __func__, prot2);
        ret = -3;
    } else {
        if (msr_dr != ((tlb->mas1 & MAS1_TS) >> MAS1_TS_SHIFT)) {
            LOG_SWTLB("%s: AS doesn't match\n", __func__);
            return -1;
        }

        *prot = prot2;
        if ((!rw && prot2 & PAGE_READ) || (rw && (prot2 & PAGE_WRITE))) {
            LOG_SWTLB("%s: found TLB!\n", __func__);
            return 0;
        }

        LOG_SWTLB("%s: PAGE_READ/WRITE doesn't match: %x\n", __func__, prot2);
        ret = -2;
    }

    return ret;
}

static int mmubooke206_get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                            target_ulong address, int rw,
                                            int access_type)
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
                                        rw, access_type);
            if (ret != -1) {
                goto found_tlb;
            }
        }
    }

found_tlb:

    if (ret >= 0) {
        ctx->raddr = raddr;
        LOG_SWTLB("%s: access granted " TARGET_FMT_lx " => " TARGET_FMT_plx
                  " %d %d\n", __func__, address, ctx->raddr, ctx->prot,
                  ret);
    } else {
        LOG_SWTLB("%s: access refused " TARGET_FMT_lx " => " TARGET_FMT_plx
                  " %d %d\n", __func__, address, raddr, ctx->prot, ret);
    }

    return ret;
}

static const char *book3e_tsize_to_str[32] = {
    "1K", "2K", "4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K",
    "1M", "2M", "4M", "8M", "16M", "32M", "64M", "128M", "256M", "512M",
    "1G", "2G", "4G", "8G", "16G", "32G", "64G", "128G", "256G", "512G",
    "1T", "2T"
};

static void mmubooke_dump_mmu(FILE *f, fprintf_function cpu_fprintf,
                                 CPUPPCState *env)
{
    ppcemb_tlb_t *entry;
    int i;

    if (kvm_enabled() && !env->kvm_sw_tlb) {
        cpu_fprintf(f, "Cannot access KVM TLB\n");
        return;
    }

    cpu_fprintf(f, "\nTLB:\n");
    cpu_fprintf(f, "Effective          Physical           Size PID   Prot     "
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
        size /= 1024;
        if (size >= 1024) {
            snprintf(size_buf, sizeof(size_buf), "%3" PRId64 "M", size / 1024);
        } else {
            snprintf(size_buf, sizeof(size_buf), "%3" PRId64 "k", size);
        }
        cpu_fprintf(f, "0x%016" PRIx64 " 0x%016" PRIx64 " %s %-5u %08x %08x\n",
                    (uint64_t)ea, (uint64_t)pa, size_buf, (uint32_t)entry->PID,
                    entry->prot, entry->attr);
    }

}

static void mmubooke206_dump_one_tlb(FILE *f, fprintf_function cpu_fprintf,
                                     CPUPPCState *env, int tlbn, int offset,
                                     int tlbsize)
{
    ppcmas_tlb_t *entry;
    int i;

    cpu_fprintf(f, "\nTLB%d:\n", tlbn);
    cpu_fprintf(f, "Effective          Physical           Size TID   TS SRWX"
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

        cpu_fprintf(f, "0x%016" PRIx64 " 0x%016" PRIx64 " %4s %-5u %1u  S%c%c%c"
                    "U%c%c%c %c%c%c%c%c U%c%c%c%c\n",
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

static void mmubooke206_dump_mmu(FILE *f, fprintf_function cpu_fprintf,
                                 CPUPPCState *env)
{
    int offset = 0;
    int i;

    if (kvm_enabled() && !env->kvm_sw_tlb) {
        cpu_fprintf(f, "Cannot access KVM TLB\n");
        return;
    }

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int size = booke206_tlb_size(env, i);

        if (size == 0) {
            continue;
        }

        mmubooke206_dump_one_tlb(f, cpu_fprintf, env, i, offset, size);
        offset += size;
    }
}

static void mmu6xx_dump_BATs(FILE *f, fprintf_function cpu_fprintf,
                             CPUPPCState *env, int type)
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
        cpu_fprintf(f, "%s BAT%d BATu " TARGET_FMT_lx
                    " BATl " TARGET_FMT_lx "\n\t" TARGET_FMT_lx " "
                    TARGET_FMT_lx " " TARGET_FMT_lx "\n",
                    type == ACCESS_CODE ? "code" : "data", i,
                    *BATu, *BATl, BEPIu, BEPIl, bl);
    }
}

static void mmu6xx_dump_mmu(FILE *f, fprintf_function cpu_fprintf,
                            CPUPPCState *env)
{
    ppc6xx_tlb_t *tlb;
    target_ulong sr;
    int type, way, entry, i;

    cpu_fprintf(f, "HTAB base = 0x%"HWADDR_PRIx"\n", env->htab_base);
    cpu_fprintf(f, "HTAB mask = 0x%"HWADDR_PRIx"\n", env->htab_mask);

    cpu_fprintf(f, "\nSegment registers:\n");
    for (i = 0; i < 32; i++) {
        sr = env->sr[i];
        if (sr & 0x80000000) {
            cpu_fprintf(f, "%02d T=%d Ks=%d Kp=%d BUID=0x%03x "
                        "CNTLR_SPEC=0x%05x\n", i,
                        sr & 0x80000000 ? 1 : 0, sr & 0x40000000 ? 1 : 0,
                        sr & 0x20000000 ? 1 : 0, (uint32_t)((sr >> 20) & 0x1FF),
                        (uint32_t)(sr & 0xFFFFF));
        } else {
            cpu_fprintf(f, "%02d T=%d Ks=%d Kp=%d N=%d VSID=0x%06x\n", i,
                        sr & 0x80000000 ? 1 : 0, sr & 0x40000000 ? 1 : 0,
                        sr & 0x20000000 ? 1 : 0, sr & 0x10000000 ? 1 : 0,
                        (uint32_t)(sr & 0x00FFFFFF));
        }
    }

    cpu_fprintf(f, "\nBATs:\n");
    mmu6xx_dump_BATs(f, cpu_fprintf, env, ACCESS_INT);
    mmu6xx_dump_BATs(f, cpu_fprintf, env, ACCESS_CODE);

    if (env->id_tlbs != 1) {
        cpu_fprintf(f, "ERROR: 6xx MMU should have separated TLB"
                    " for code and data\n");
    }

    cpu_fprintf(f, "\nTLBs                       [EPN    EPN + SIZE]\n");

    for (type = 0; type < 2; type++) {
        for (way = 0; way < env->nb_ways; way++) {
            for (entry = env->nb_tlb * type + env->tlb_per_way * way;
                 entry < (env->nb_tlb * type + env->tlb_per_way * (way + 1));
                 entry++) {

                tlb = &env->tlb.tlb6[entry];
                cpu_fprintf(f, "%s TLB %02d/%02d way:%d %s ["
                            TARGET_FMT_lx " " TARGET_FMT_lx "]\n",
                            type ? "code" : "data", entry % env->nb_tlb,
                            env->nb_tlb, way,
                            pte_is_valid(tlb->pte0) ? "valid" : "inval",
                            tlb->EPN, tlb->EPN + TARGET_PAGE_SIZE);
            }
        }
    }
}

void dump_mmu(FILE *f, fprintf_function cpu_fprintf, CPUPPCState *env)
{
    switch (env->mmu_model) {
    case POWERPC_MMU_BOOKE:
        mmubooke_dump_mmu(f, cpu_fprintf, env);
        break;
    case POWERPC_MMU_BOOKE206:
        mmubooke206_dump_mmu(f, cpu_fprintf, env);
        break;
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
        mmu6xx_dump_mmu(f, cpu_fprintf, env);
        break;
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_06a:
    case POWERPC_MMU_2_07:
    case POWERPC_MMU_2_07a:
        dump_slb(f, cpu_fprintf, ppc_env_get_cpu(env));
        break;
#endif
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented\n", __func__);
    }
}

static inline int check_physical(CPUPPCState *env, mmu_ctx_t *ctx,
                                 target_ulong eaddr, int rw)
{
    int in_plb, ret;

    ctx->raddr = eaddr;
    ctx->prot = PAGE_READ | PAGE_EXEC;
    ret = 0;
    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
    case POWERPC_MMU_SOFT_4xx:
    case POWERPC_MMU_REAL:
    case POWERPC_MMU_BOOKE:
        ctx->prot |= PAGE_WRITE;
        break;

    case POWERPC_MMU_SOFT_4xx_Z:
        if (unlikely(msr_pe != 0)) {
            /* 403 family add some particular protections,
             * using PBL/PBU registers for accesses with no translation.
             */
            in_plb =
                /* Check PLB validity */
                (env->pb[0] < env->pb[1] &&
                 /* and address in plb area */
                 eaddr >= env->pb[0] && eaddr < env->pb[1]) ||
                (env->pb[2] < env->pb[3] &&
                 eaddr >= env->pb[2] && eaddr < env->pb[3]) ? 1 : 0;
            if (in_plb ^ msr_px) {
                /* Access in protected area */
                if (rw == 1) {
                    /* Access is not allowed */
                    ret = -2;
                }
            } else {
                /* Read-write access is allowed */
                ctx->prot |= PAGE_WRITE;
            }
        }
        break;

    default:
        /* Caller's checks mean we should never get here for other models */
        abort();
        return -1;
    }

    return ret;
}

static int get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                target_ulong eaddr, int rw, int access_type)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    int ret = -1;
    bool real_mode = (access_type == ACCESS_CODE && msr_ir == 0)
        || (access_type != ACCESS_CODE && msr_dr == 0);

#if 0
    qemu_log("%s\n", __func__);
#endif

    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
        if (real_mode) {
            ret = check_physical(env, ctx, eaddr, rw);
        } else {
            /* Try to find a BAT */
            if (env->nb_BATs != 0) {
                ret = get_bat_6xx_tlb(env, ctx, eaddr, rw, access_type);
            }
            if (ret < 0) {
                /* We didn't match any BAT entry or don't have BATs */
                ret = get_segment_6xx_tlb(env, ctx, eaddr, rw, access_type);
            }
        }
        break;

    case POWERPC_MMU_SOFT_4xx:
    case POWERPC_MMU_SOFT_4xx_Z:
        if (real_mode) {
            ret = check_physical(env, ctx, eaddr, rw);
        } else {
            ret = mmu40x_get_physical_address(env, ctx, eaddr,
                                              rw, access_type);
        }
        break;
    case POWERPC_MMU_BOOKE:
        ret = mmubooke_get_physical_address(env, ctx, eaddr,
                                            rw, access_type);
        break;
    case POWERPC_MMU_BOOKE206:
        ret = mmubooke206_get_physical_address(env, ctx, eaddr, rw,
                                               access_type);
        break;
    case POWERPC_MMU_MPC8xx:
        /* XXX: TODO */
        cpu_abort(CPU(cpu), "MPC8xx MMU model is not implemented\n");
        break;
    case POWERPC_MMU_REAL:
        if (real_mode) {
            ret = check_physical(env, ctx, eaddr, rw);
        } else {
            cpu_abort(CPU(cpu), "PowerPC in real mode do not do any translation\n");
        }
        return -1;
    default:
        cpu_abort(CPU(cpu), "Unknown or invalid MMU model\n");
        return -1;
    }
#if 0
    qemu_log("%s address " TARGET_FMT_lx " => %d " TARGET_FMT_plx "\n",
             __func__, eaddr, ret, ctx->raddr);
#endif

    return ret;
}

hwaddr ppc_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    mmu_ctx_t ctx;

    switch (env->mmu_model) {
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_06a:
    case POWERPC_MMU_2_07:
    case POWERPC_MMU_2_07a:
        return ppc_hash64_get_phys_page_debug(cpu, addr);
#endif

    case POWERPC_MMU_32B:
    case POWERPC_MMU_601:
        return ppc_hash32_get_phys_page_debug(cpu, addr);

    default:
        ;
    }

    if (unlikely(get_physical_address(env, &ctx, addr, 0, ACCESS_INT) != 0)) {

        /* Some MMUs have separate TLBs for code and data. If we only try an
         * ACCESS_INT, we may not be able to read instructions mapped by code
         * TLBs, so we also try a ACCESS_CODE.
         */
        if (unlikely(get_physical_address(env, &ctx, addr, 0,
                                          ACCESS_CODE) != 0)) {
            return -1;
        }
    }

    return ctx.raddr & TARGET_PAGE_MASK;
}

static void booke206_update_mas_tlb_miss(CPUPPCState *env, target_ulong address,
                                     int rw)
{
    env->spr[SPR_BOOKE_MAS0] = env->spr[SPR_BOOKE_MAS4] & MAS4_TLBSELD_MASK;
    env->spr[SPR_BOOKE_MAS1] = env->spr[SPR_BOOKE_MAS4] & MAS4_TSIZED_MASK;
    env->spr[SPR_BOOKE_MAS2] = env->spr[SPR_BOOKE_MAS4] & MAS4_WIMGED_MASK;
    env->spr[SPR_BOOKE_MAS3] = 0;
    env->spr[SPR_BOOKE_MAS6] = 0;
    env->spr[SPR_BOOKE_MAS7] = 0;

    /* AS */
    if (((rw == 2) && msr_ir) || ((rw != 2) && msr_dr)) {
        env->spr[SPR_BOOKE_MAS1] |= MAS1_TS;
        env->spr[SPR_BOOKE_MAS6] |= MAS6_SAS;
    }

    env->spr[SPR_BOOKE_MAS1] |= MAS1_VALID;
    env->spr[SPR_BOOKE_MAS2] |= address & MAS2_EPN_MASK;

    switch (env->spr[SPR_BOOKE_MAS4] & MAS4_TIDSELD_PIDZ) {
    case MAS4_TIDSELD_PID0:
        env->spr[SPR_BOOKE_MAS1] |= env->spr[SPR_BOOKE_PID] << MAS1_TID_SHIFT;
        break;
    case MAS4_TIDSELD_PID1:
        env->spr[SPR_BOOKE_MAS1] |= env->spr[SPR_BOOKE_PID1] << MAS1_TID_SHIFT;
        break;
    case MAS4_TIDSELD_PID2:
        env->spr[SPR_BOOKE_MAS1] |= env->spr[SPR_BOOKE_PID2] << MAS1_TID_SHIFT;
        break;
    }

    env->spr[SPR_BOOKE_MAS6] |= env->spr[SPR_BOOKE_PID] << 16;

    /* next victim logic */
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_ESEL_SHIFT;
    env->last_way++;
    env->last_way &= booke206_tlb_ways(env, 0) - 1;
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_NV_SHIFT;
}

/* Perform address translation */
static int cpu_ppc_handle_mmu_fault(CPUPPCState *env, target_ulong address,
                                    int rw, int mmu_idx)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    mmu_ctx_t ctx;
    int access_type;
    int ret = 0;

    if (rw == 2) {
        /* code access */
        rw = 0;
        access_type = ACCESS_CODE;
    } else {
        /* data access */
        access_type = env->access_type;
    }
    ret = get_physical_address(env, &ctx, address, rw, access_type);
    if (ret == 0) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     ctx.raddr & TARGET_PAGE_MASK, ctx.prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        ret = 0;
    } else if (ret < 0) {
        LOG_MMU_STATE(cs);
        if (access_type == ACCESS_CODE) {
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                switch (env->mmu_model) {
                case POWERPC_MMU_SOFT_6xx:
                    cs->exception_index = POWERPC_EXCP_IFTLB;
                    env->error_code = 1 << 18;
                    env->spr[SPR_IMISS] = address;
                    env->spr[SPR_ICMP] = 0x80000000 | ctx.ptem;
                    goto tlb_miss;
                case POWERPC_MMU_SOFT_74xx:
                    cs->exception_index = POWERPC_EXCP_IFTLB;
                    goto tlb_miss_74xx;
                case POWERPC_MMU_SOFT_4xx:
                case POWERPC_MMU_SOFT_4xx_Z:
                    cs->exception_index = POWERPC_EXCP_ITLB;
                    env->error_code = 0;
                    env->spr[SPR_40x_DEAR] = address;
                    env->spr[SPR_40x_ESR] = 0x00000000;
                    break;
                case POWERPC_MMU_BOOKE206:
                    booke206_update_mas_tlb_miss(env, address, rw);
                    /* fall through */
                case POWERPC_MMU_BOOKE:
                    cs->exception_index = POWERPC_EXCP_ITLB;
                    env->error_code = 0;
                    env->spr[SPR_BOOKE_DEAR] = address;
                    return -1;
                case POWERPC_MMU_MPC8xx:
                    /* XXX: TODO */
                    cpu_abort(cs, "MPC8xx MMU model is not implemented\n");
                    break;
                case POWERPC_MMU_REAL:
                    cpu_abort(cs, "PowerPC in real mode should never raise "
                              "any MMU exceptions\n");
                    return -1;
                default:
                    cpu_abort(cs, "Unknown or invalid MMU model\n");
                    return -1;
                }
                break;
            case -2:
                /* Access rights violation */
                cs->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x08000000;
                break;
            case -3:
                /* No execute protection violation */
                if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
                    (env->mmu_model == POWERPC_MMU_BOOKE206)) {
                    env->spr[SPR_BOOKE_ESR] = 0x00000000;
                }
                cs->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x10000000;
                break;
            case -4:
                /* Direct store exception */
                /* No code fetch is allowed in direct-store areas */
                cs->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x10000000;
                break;
            }
        } else {
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                switch (env->mmu_model) {
                case POWERPC_MMU_SOFT_6xx:
                    if (rw == 1) {
                        cs->exception_index = POWERPC_EXCP_DSTLB;
                        env->error_code = 1 << 16;
                    } else {
                        cs->exception_index = POWERPC_EXCP_DLTLB;
                        env->error_code = 0;
                    }
                    env->spr[SPR_DMISS] = address;
                    env->spr[SPR_DCMP] = 0x80000000 | ctx.ptem;
                tlb_miss:
                    env->error_code |= ctx.key << 19;
                    env->spr[SPR_HASH1] = env->htab_base +
                        get_pteg_offset32(cpu, ctx.hash[0]);
                    env->spr[SPR_HASH2] = env->htab_base +
                        get_pteg_offset32(cpu, ctx.hash[1]);
                    break;
                case POWERPC_MMU_SOFT_74xx:
                    if (rw == 1) {
                        cs->exception_index = POWERPC_EXCP_DSTLB;
                    } else {
                        cs->exception_index = POWERPC_EXCP_DLTLB;
                    }
                tlb_miss_74xx:
                    /* Implement LRU algorithm */
                    env->error_code = ctx.key << 19;
                    env->spr[SPR_TLBMISS] = (address & ~((target_ulong)0x3)) |
                        ((env->last_way + 1) & (env->nb_ways - 1));
                    env->spr[SPR_PTEHI] = 0x80000000 | ctx.ptem;
                    break;
                case POWERPC_MMU_SOFT_4xx:
                case POWERPC_MMU_SOFT_4xx_Z:
                    cs->exception_index = POWERPC_EXCP_DTLB;
                    env->error_code = 0;
                    env->spr[SPR_40x_DEAR] = address;
                    if (rw) {
                        env->spr[SPR_40x_ESR] = 0x00800000;
                    } else {
                        env->spr[SPR_40x_ESR] = 0x00000000;
                    }
                    break;
                case POWERPC_MMU_MPC8xx:
                    /* XXX: TODO */
                    cpu_abort(cs, "MPC8xx MMU model is not implemented\n");
                    break;
                case POWERPC_MMU_BOOKE206:
                    booke206_update_mas_tlb_miss(env, address, rw);
                    /* fall through */
                case POWERPC_MMU_BOOKE:
                    cs->exception_index = POWERPC_EXCP_DTLB;
                    env->error_code = 0;
                    env->spr[SPR_BOOKE_DEAR] = address;
                    env->spr[SPR_BOOKE_ESR] = rw ? ESR_ST : 0;
                    return -1;
                case POWERPC_MMU_REAL:
                    cpu_abort(cs, "PowerPC in real mode should never raise "
                              "any MMU exceptions\n");
                    return -1;
                default:
                    cpu_abort(cs, "Unknown or invalid MMU model\n");
                    return -1;
                }
                break;
            case -2:
                /* Access rights violation */
                cs->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                if (env->mmu_model == POWERPC_MMU_SOFT_4xx
                    || env->mmu_model == POWERPC_MMU_SOFT_4xx_Z) {
                    env->spr[SPR_40x_DEAR] = address;
                    if (rw) {
                        env->spr[SPR_40x_ESR] |= 0x00800000;
                    }
                } else if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
                           (env->mmu_model == POWERPC_MMU_BOOKE206)) {
                    env->spr[SPR_BOOKE_DEAR] = address;
                    env->spr[SPR_BOOKE_ESR] = rw ? ESR_ST : 0;
                } else {
                    env->spr[SPR_DAR] = address;
                    if (rw == 1) {
                        env->spr[SPR_DSISR] = 0x0A000000;
                    } else {
                        env->spr[SPR_DSISR] = 0x08000000;
                    }
                }
                break;
            case -4:
                /* Direct store exception */
                switch (access_type) {
                case ACCESS_FLOAT:
                    /* Floating point load/store */
                    cs->exception_index = POWERPC_EXCP_ALIGN;
                    env->error_code = POWERPC_EXCP_ALIGN_FP;
                    env->spr[SPR_DAR] = address;
                    break;
                case ACCESS_RES:
                    /* lwarx, ldarx or stwcx. */
                    cs->exception_index = POWERPC_EXCP_DSI;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = address;
                    if (rw == 1) {
                        env->spr[SPR_DSISR] = 0x06000000;
                    } else {
                        env->spr[SPR_DSISR] = 0x04000000;
                    }
                    break;
                case ACCESS_EXT:
                    /* eciwx or ecowx */
                    cs->exception_index = POWERPC_EXCP_DSI;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = address;
                    if (rw == 1) {
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
                    env->spr[SPR_DAR] = address;
                    break;
                }
                break;
            }
        }
#if 0
        printf("%s: set exception to %d %02x\n", __func__,
               cs->exception, env->error_code);
#endif
        ret = 1;
    }

    return ret;
}

/*****************************************************************************/
/* BATs management */
#if !defined(FLUSH_ALL_TLBS)
static inline void do_invalidate_BAT(CPUPPCState *env, target_ulong BATu,
                                     target_ulong mask)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    target_ulong base, end, page;

    base = BATu & ~0x0001FFFF;
    end = base + mask + 0x00020000;
    LOG_BATS("Flush BAT from " TARGET_FMT_lx " to " TARGET_FMT_lx " ("
             TARGET_FMT_lx ")\n", base, end, mask);
    for (page = base; page != end; page += TARGET_PAGE_SIZE) {
        tlb_flush_page(cs, page);
    }
    LOG_BATS("Flush done\n");
}
#endif

static inline void dump_store_bat(CPUPPCState *env, char ID, int ul, int nr,
                                  target_ulong value)
{
    LOG_BATS("Set %cBAT%d%c to " TARGET_FMT_lx " (" TARGET_FMT_lx ")\n", ID,
             nr, ul == 0 ? 'u' : 'l', value, env->nip);
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
        /* When storing valid upper BAT, mask BEPI and BRPN
         * and invalidate all TLBs covered by this BAT
         */
        mask = (value << 15) & 0x0FFE0000UL;
        env->IBAT[0][nr] = (value & 0x00001FFFUL) |
            (value & ~0x0001FFFFUL & ~mask);
        env->IBAT[1][nr] = (env->IBAT[1][nr] & 0x0000007B) |
            (env->IBAT[1][nr] & ~0x0001FFFF & ~mask);
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#else
        tlb_flush(env, 1);
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
        /* When storing valid upper BAT, mask BEPI and BRPN
         * and invalidate all TLBs covered by this BAT
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
        tlb_flush(env, 1);
#endif
    }
}

void helper_store_dbatl(CPUPPCState *env, uint32_t nr, target_ulong value)
{
    dump_store_bat(env, 'D', 1, nr, value);
    env->DBAT[1][nr] = value;
}

void helper_store_601_batu(CPUPPCState *env, uint32_t nr, target_ulong value)
{
    target_ulong mask;
#if defined(FLUSH_ALL_TLBS)
    int do_inval;
#endif

    dump_store_bat(env, 'I', 0, nr, value);
    if (env->IBAT[0][nr] != value) {
#if defined(FLUSH_ALL_TLBS)
        do_inval = 0;
#endif
        mask = (env->IBAT[1][nr] << 17) & 0x0FFE0000UL;
        if (env->IBAT[1][nr] & 0x40) {
            /* Invalidate BAT only if it is valid */
#if !defined(FLUSH_ALL_TLBS)
            do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#else
            do_inval = 1;
#endif
        }
        /* When storing valid upper BAT, mask BEPI and BRPN
         * and invalidate all TLBs covered by this BAT
         */
        env->IBAT[0][nr] = (value & 0x00001FFFUL) |
            (value & ~0x0001FFFFUL & ~mask);
        env->DBAT[0][nr] = env->IBAT[0][nr];
        if (env->IBAT[1][nr] & 0x40) {
#if !defined(FLUSH_ALL_TLBS)
            do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#else
            do_inval = 1;
#endif
        }
#if defined(FLUSH_ALL_TLBS)
        if (do_inval) {
            tlb_flush(env, 1);
        }
#endif
    }
}

void helper_store_601_batl(CPUPPCState *env, uint32_t nr, target_ulong value)
{
#if !defined(FLUSH_ALL_TLBS)
    target_ulong mask;
#else
    int do_inval;
#endif

    dump_store_bat(env, 'I', 1, nr, value);
    if (env->IBAT[1][nr] != value) {
#if defined(FLUSH_ALL_TLBS)
        do_inval = 0;
#endif
        if (env->IBAT[1][nr] & 0x40) {
#if !defined(FLUSH_ALL_TLBS)
            mask = (env->IBAT[1][nr] << 17) & 0x0FFE0000UL;
            do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#else
            do_inval = 1;
#endif
        }
        if (value & 0x40) {
#if !defined(FLUSH_ALL_TLBS)
            mask = (value << 17) & 0x0FFE0000UL;
            do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#else
            do_inval = 1;
#endif
        }
        env->IBAT[1][nr] = value;
        env->DBAT[1][nr] = value;
#if defined(FLUSH_ALL_TLBS)
        if (do_inval) {
            tlb_flush(env, 1);
        }
#endif
    }
}

/*****************************************************************************/
/* TLB management */
void ppc_tlb_invalidate_all(CPUPPCState *env)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
        ppc6xx_tlb_invalidate_all(env);
        break;
    case POWERPC_MMU_SOFT_4xx:
    case POWERPC_MMU_SOFT_4xx_Z:
        ppc4xx_tlb_invalidate_all(env);
        break;
    case POWERPC_MMU_REAL:
        cpu_abort(CPU(cpu), "No TLB for PowerPC 4xx in real mode\n");
        break;
    case POWERPC_MMU_MPC8xx:
        /* XXX: TODO */
        cpu_abort(CPU(cpu), "MPC8xx MMU model is not implemented\n");
        break;
    case POWERPC_MMU_BOOKE:
        tlb_flush(CPU(cpu), 1);
        break;
    case POWERPC_MMU_BOOKE206:
        booke206_flush_tlb(env, -1, 0);
        break;
    case POWERPC_MMU_32B:
    case POWERPC_MMU_601:
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_06a:
    case POWERPC_MMU_2_07:
    case POWERPC_MMU_2_07a:
#endif /* defined(TARGET_PPC64) */
        tlb_flush(CPU(cpu), 1);
        break;
    default:
        /* XXX: TODO */
        cpu_abort(CPU(cpu), "Unknown MMU model\n");
        break;
    }
}

void ppc_tlb_invalidate_one(CPUPPCState *env, target_ulong addr)
{
#if !defined(FLUSH_ALL_TLBS)
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    CPUState *cs;

    addr &= TARGET_PAGE_MASK;
    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
        ppc6xx_tlb_invalidate_virt(env, addr, 0);
        if (env->id_tlbs == 1) {
            ppc6xx_tlb_invalidate_virt(env, addr, 1);
        }
        break;
    case POWERPC_MMU_32B:
    case POWERPC_MMU_601:
        /* tlbie invalidate TLBs for all segments */
        addr &= ~((target_ulong)-1ULL << 28);
        cs = CPU(cpu);
        /* XXX: this case should be optimized,
         * giving a mask to tlb_flush_page
         */
        tlb_flush_page(cs, addr | (0x0 << 28));
        tlb_flush_page(cs, addr | (0x1 << 28));
        tlb_flush_page(cs, addr | (0x2 << 28));
        tlb_flush_page(cs, addr | (0x3 << 28));
        tlb_flush_page(cs, addr | (0x4 << 28));
        tlb_flush_page(cs, addr | (0x5 << 28));
        tlb_flush_page(cs, addr | (0x6 << 28));
        tlb_flush_page(cs, addr | (0x7 << 28));
        tlb_flush_page(cs, addr | (0x8 << 28));
        tlb_flush_page(cs, addr | (0x9 << 28));
        tlb_flush_page(cs, addr | (0xA << 28));
        tlb_flush_page(cs, addr | (0xB << 28));
        tlb_flush_page(cs, addr | (0xC << 28));
        tlb_flush_page(cs, addr | (0xD << 28));
        tlb_flush_page(cs, addr | (0xE << 28));
        tlb_flush_page(cs, addr | (0xF << 28));
        break;
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_06a:
    case POWERPC_MMU_2_07:
    case POWERPC_MMU_2_07a:
        /* tlbie invalidate TLBs for all segments */
        /* XXX: given the fact that there are too many segments to invalidate,
         *      and we still don't have a tlb_flush_mask(env, n, mask) in QEMU,
         *      we just invalidate all TLBs
         */
        tlb_flush(CPU(cpu), 1);
        break;
#endif /* defined(TARGET_PPC64) */
    default:
        /* Should never reach here with other MMU models */
        assert(0);
    }
#else
    ppc_tlb_invalidate_all(env);
#endif
}

/*****************************************************************************/
/* Special registers manipulation */
void ppc_store_sdr1(CPUPPCState *env, target_ulong value)
{
    qemu_log_mask(CPU_LOG_MMU, "%s: " TARGET_FMT_lx "\n", __func__, value);
    assert(!env->external_htab);
    env->spr[SPR_SDR1] = value;
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64) {
        target_ulong htabsize = value & SDR_64_HTABSIZE;

        if (htabsize > 28) {
            fprintf(stderr, "Invalid HTABSIZE 0x" TARGET_FMT_lx
                    " stored in SDR1\n", htabsize);
            htabsize = 28;
        }
        env->htab_mask = (1ULL << (htabsize + 18 - 7)) - 1;
        env->htab_base = value & SDR_64_HTABORG;
    } else
#endif /* defined(TARGET_PPC64) */
    {
        /* FIXME: Should check for valid HTABMASK values */
        env->htab_mask = ((value & SDR_32_HTABMASK) << 16) | 0xFFFF;
        env->htab_base = value & SDR_32_HTABORG;
    }
}

/* Segment registers load and store */
target_ulong helper_load_sr(CPUPPCState *env, target_ulong sr_num)
{
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64) {
        /* XXX */
        return 0;
    }
#endif
    return env->sr[sr_num];
}

void helper_store_sr(CPUPPCState *env, target_ulong srnum, target_ulong value)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    qemu_log_mask(CPU_LOG_MMU,
            "%s: reg=%d " TARGET_FMT_lx " " TARGET_FMT_lx "\n", __func__,
            (int)srnum, value, env->sr[srnum]);
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64) {
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
/* Invalidating 256MB of virtual memory in 4kB pages is way longer than
   flusing the whole TLB. */
#if !defined(FLUSH_ALL_TLBS) && 0
        {
            target_ulong page, end;
            /* Invalidate 256 MB of virtual memory */
            page = (16 << 20) * srnum;
            end = page + (16 << 20);
            for (; page != end; page += TARGET_PAGE_SIZE) {
                tlb_flush_page(CPU(cpu), page);
            }
        }
#else
        tlb_flush(CPU(cpu), 1);
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

void helper_tlbiva(CPUPPCState *env, target_ulong addr)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    /* tlbiva instruction only exists on BookE */
    assert(env->mmu_model == POWERPC_MMU_BOOKE);
    /* XXX: TODO */
    cpu_abort(CPU(cpu), "BookE MMU model is not implemented\n");
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
    LOG_SWTLB("%s: EPN " TARGET_FMT_lx " " TARGET_FMT_lx " PTE0 " TARGET_FMT_lx
              " PTE1 " TARGET_FMT_lx " way %d\n", __func__, new_EPN, EPN, CMP,
              RPN, way);
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

/* PowerPC 74xx software TLB load instructions helpers */
static void do_74xx_tlb(CPUPPCState *env, target_ulong new_EPN, int is_code)
{
    target_ulong RPN, CMP, EPN;
    int way;

    RPN = env->spr[SPR_PTELO];
    CMP = env->spr[SPR_PTEHI];
    EPN = env->spr[SPR_TLBMISS] & ~0x3;
    way = env->spr[SPR_TLBMISS] & 0x3;
    (void)EPN; /* avoid a compiler warning */
    LOG_SWTLB("%s: EPN " TARGET_FMT_lx " " TARGET_FMT_lx " PTE0 " TARGET_FMT_lx
              " PTE1 " TARGET_FMT_lx " way %d\n", __func__, new_EPN, EPN, CMP,
              RPN, way);
    /* Store this TLB */
    ppc6xx_tlb_store(env, (uint32_t)(new_EPN & TARGET_PAGE_MASK),
                     way, is_code, CMP, RPN);
}

void helper_74xx_tlbd(CPUPPCState *env, target_ulong EPN)
{
    do_74xx_tlb(env, EPN, 0);
}

void helper_74xx_tlbi(CPUPPCState *env, target_ulong EPN)
{
    do_74xx_tlb(env, EPN, 1);
}

/*****************************************************************************/
/* PowerPC 601 specific instructions (POWER bridge) */

target_ulong helper_rac(CPUPPCState *env, target_ulong addr)
{
    mmu_ctx_t ctx;
    int nb_BATs;
    target_ulong ret = 0;

    /* We don't have to generate many instances of this instruction,
     * as rac is supervisor only.
     */
    /* XXX: FIX THIS: Pretend we have no BAT */
    nb_BATs = env->nb_BATs;
    env->nb_BATs = 0;
    if (get_physical_address(env, &ctx, addr, 0, ACCESS_INT) == 0) {
        ret = ctx.raddr;
    }
    env->nb_BATs = nb_BATs;
    return ret;
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
    env->spr[SPR_40x_PID] = tlb->PID;
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

void helper_4xx_tlbwe_hi(CPUPPCState *env, target_ulong entry,
                         target_ulong val)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    ppcemb_tlb_t *tlb;
    target_ulong page, end;

    LOG_SWTLB("%s entry %d val " TARGET_FMT_lx "\n", __func__, (int)entry,
              val);
    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    /* Invalidate previous TLB (if it's valid) */
    if (tlb->prot & PAGE_VALID) {
        end = tlb->EPN + tlb->size;
        LOG_SWTLB("%s: invalidate old TLB %d start " TARGET_FMT_lx " end "
                  TARGET_FMT_lx "\n", __func__, (int)entry, tlb->EPN, end);
        for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE) {
            tlb_flush_page(cs, page);
        }
    }
    tlb->size = booke_tlb_to_page_size((val >> PPC4XX_TLBHI_SIZE_SHIFT)
                                       & PPC4XX_TLBHI_SIZE_MASK);
    /* We cannot handle TLB size < TARGET_PAGE_SIZE.
     * If this ever occurs, one should use the ppcemb target instead
     * of the ppc or ppc64 one
     */
    if ((val & PPC4XX_TLBHI_V) && tlb->size < TARGET_PAGE_SIZE) {
        cpu_abort(cs, "TLB size " TARGET_FMT_lu " < %u "
                  "are not supported (%d)\n",
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
    LOG_SWTLB("%s: set up TLB %d RPN " TARGET_FMT_plx " EPN " TARGET_FMT_lx
              " size " TARGET_FMT_lx " prot %c%c%c%c PID %d\n", __func__,
              (int)entry, tlb->RPN, tlb->EPN, tlb->size,
              tlb->prot & PAGE_READ ? 'r' : '-',
              tlb->prot & PAGE_WRITE ? 'w' : '-',
              tlb->prot & PAGE_EXEC ? 'x' : '-',
              tlb->prot & PAGE_VALID ? 'v' : '-', (int)tlb->PID);
    /* Invalidate new TLB (if valid) */
    if (tlb->prot & PAGE_VALID) {
        end = tlb->EPN + tlb->size;
        LOG_SWTLB("%s: invalidate TLB %d start " TARGET_FMT_lx " end "
                  TARGET_FMT_lx "\n", __func__, (int)entry, tlb->EPN, end);
        for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE) {
            tlb_flush_page(cs, page);
        }
    }
}

void helper_4xx_tlbwe_lo(CPUPPCState *env, target_ulong entry,
                         target_ulong val)
{
    ppcemb_tlb_t *tlb;

    LOG_SWTLB("%s entry %i val " TARGET_FMT_lx "\n", __func__, (int)entry,
              val);
    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    tlb->attr = val & PPC4XX_TLBLO_ATTR_MASK;
    tlb->RPN = val & PPC4XX_TLBLO_RPN_MASK;
    tlb->prot = PAGE_READ;
    if (val & PPC4XX_TLBLO_EX) {
        tlb->prot |= PAGE_EXEC;
    }
    if (val & PPC4XX_TLBLO_WR) {
        tlb->prot |= PAGE_WRITE;
    }
    LOG_SWTLB("%s: set up TLB %d RPN " TARGET_FMT_plx " EPN " TARGET_FMT_lx
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

/* PowerPC 440 TLB management */
void helper_440_tlbwe(CPUPPCState *env, uint32_t word, target_ulong entry,
                      target_ulong value)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    ppcemb_tlb_t *tlb;
    target_ulong EPN, RPN, size;
    int do_flush_tlbs;

    LOG_SWTLB("%s word %d entry %d value " TARGET_FMT_lx "\n",
              __func__, word, (int)entry, value);
    do_flush_tlbs = 0;
    entry &= 0x3F;
    tlb = &env->tlb.tlbe[entry];
    switch (word) {
    default:
        /* Just here to please gcc */
    case 0:
        EPN = value & 0xFFFFFC00;
        if ((tlb->prot & PAGE_VALID) && EPN != tlb->EPN) {
            do_flush_tlbs = 1;
        }
        tlb->EPN = EPN;
        size = booke_tlb_to_page_size((value >> 4) & 0xF);
        if ((tlb->prot & PAGE_VALID) && tlb->size < size) {
            do_flush_tlbs = 1;
        }
        tlb->size = size;
        tlb->attr &= ~0x1;
        tlb->attr |= (value >> 8) & 1;
        if (value & 0x200) {
            tlb->prot |= PAGE_VALID;
        } else {
            if (tlb->prot & PAGE_VALID) {
                tlb->prot &= ~PAGE_VALID;
                do_flush_tlbs = 1;
            }
        }
        tlb->PID = env->spr[SPR_440_MMUCR] & 0x000000FF;
        if (do_flush_tlbs) {
            tlb_flush(CPU(cpu), 1);
        }
        break;
    case 1:
        RPN = value & 0xFFFFFC0F;
        if ((tlb->prot & PAGE_VALID) && tlb->RPN != RPN) {
            tlb_flush(CPU(cpu), 1);
        }
        tlb->RPN = RPN;
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
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    uint32_t tlbncfg = 0;
    int esel = (env->spr[SPR_BOOKE_MAS0] & MAS0_ESEL_MASK) >> MAS0_ESEL_SHIFT;
    int ea = (env->spr[SPR_BOOKE_MAS2] & MAS2_EPN_MASK);
    int tlb;

    tlb = (env->spr[SPR_BOOKE_MAS0] & MAS0_TLBSEL_MASK) >> MAS0_TLBSEL_SHIFT;
    tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlb];

    if ((tlbncfg & TLBnCFG_HES) && (env->spr[SPR_BOOKE_MAS0] & MAS0_HES)) {
        cpu_abort(CPU(cpu), "we don't support HES yet\n");
    }

    return booke206_get_tlbm(env, tlb, ea, esel);
}

void helper_booke_setpid(CPUPPCState *env, uint32_t pidn, target_ulong pid)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    env->spr[pidn] = pid;
    /* changing PIDs mean we're in a different address space now */
    tlb_flush(CPU(cpu), 1);
}

void helper_booke206_tlbwe(CPUPPCState *env)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
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
        !msr_gs) {
        /* XXX we don't support direct LRAT setting yet */
        fprintf(stderr, "cpu: don't support LRAT setting yet\n");
        return;
    }

    tlbn = (env->spr[SPR_BOOKE_MAS0] & MAS0_TLBSEL_MASK) >> MAS0_TLBSEL_SHIFT;
    tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlbn];

    tlb = booke206_cur_tlb(env);

    if (!tlb) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_INVAL);
    }

    /* check that we support the targeted size */
    size_tlb = (env->spr[SPR_BOOKE_MAS1] & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
    size_ps = booke206_tlbnps(env, tlbn);
    if ((env->spr[SPR_BOOKE_MAS1] & MAS1_VALID) && (tlbncfg & TLBnCFG_AVAIL) &&
        !(size_ps & (1 << size_tlb))) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_INVAL);
    }

    if (msr_gs) {
        cpu_abort(CPU(cpu), "missing HV implementation\n");
    }
    tlb->mas7_3 = ((uint64_t)env->spr[SPR_BOOKE_MAS7] << 32) |
        env->spr[SPR_BOOKE_MAS3];
    tlb->mas1 = env->spr[SPR_BOOKE_MAS1];

    /* MAV 1.0 only */
    if (!(tlbncfg & TLBnCFG_AVAIL)) {
        /* force !AVAIL TLB entries to correct page size */
        tlb->mas1 &= ~MAS1_TSIZE_MASK;
        /* XXX can be configured in MMUCSR0 */
        tlb->mas1 |= (tlbncfg & TLBnCFG_MINSIZE) >> 12;
    }

    /* Make a mask from TLB size to discard invalid bits in EPN field */
    mask = ~(booke206_tlb_to_page_size(env, tlb) - 1);
    /* Add a mask for page attributes */
    mask |= MAS2_ACM | MAS2_VLE | MAS2_W | MAS2_I | MAS2_M | MAS2_G | MAS2_E;

    if (!msr_cm) {
        /* Executing a tlbwe instruction in 32-bit mode will set
         * bits 0:31 of the TLB EPN field to zero.
         */
        mask &= 0xffffffff;
    }

    tlb->mas2 = env->spr[SPR_BOOKE_MAS2] & mask;

    if (!(tlbncfg & TLBnCFG_IPROT)) {
        /* no IPROT supported by TLB */
        tlb->mas1 &= ~MAS1_IPROT;
    }

    if (booke206_tlb_to_page_size(env, tlb) == TARGET_PAGE_SIZE) {
        tlb_flush_page(CPU(cpu), tlb->mas2 & MAS2_EPN_MASK);
    } else {
        tlb_flush(CPU(cpu), 1);
    }
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
                                              uint32_t ea)
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
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

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
        tlb_flush(CPU(cpu), 1);
    } else {
        /* flush TLB0 entries */
        booke206_invalidate_ea_tlb(env, 0, address);
        tlb_flush_page(CPU(cpu), address & MAS2_EPN_MASK);
    }
}

void helper_booke206_tlbilx0(CPUPPCState *env, target_ulong address)
{
    /* XXX missing LPID handling */
    booke206_flush_tlb(env, -1, 1);
}

void helper_booke206_tlbilx1(CPUPPCState *env, target_ulong address)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
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
    tlb_flush(CPU(cpu), 1);
}

void helper_booke206_tlbilx3(CPUPPCState *env, target_ulong address)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
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
    tlb_flush(CPU(cpu), 1);
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


/*****************************************************************************/

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    CPUPPCState *env = &cpu->env;
    int ret;

    if (pcc->handle_mmu_fault) {
        ret = pcc->handle_mmu_fault(cpu, addr, is_write, mmu_idx);
    } else {
        ret = cpu_ppc_handle_mmu_fault(env, addr, is_write, mmu_idx);
    }
    if (unlikely(ret != 0)) {
        if (likely(retaddr)) {
            /* now we have a real cpu fault */
            cpu_restore_state(cs, retaddr);
        }
        helper_raise_exception_err(env, cs->exception_index, env->error_code);
    }
}
