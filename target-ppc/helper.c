/*
 *  PowerPC emulation helpers for qemu.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <assert.h>

#include "cpu.h"
#include "exec-all.h"
#include "helper_regs.h"
#include "qemu-common.h"
#include "kvm.h"

//#define DEBUG_MMU
//#define DEBUG_BATS
//#define DEBUG_SLB
//#define DEBUG_SOFTWARE_TLB
//#define DUMP_PAGE_TABLES
//#define DEBUG_EXCEPTIONS
//#define FLUSH_ALL_TLBS

#ifdef DEBUG_MMU
#  define LOG_MMU(...) qemu_log(__VA_ARGS__)
#  define LOG_MMU_STATE(env) log_cpu_state((env), 0)
#else
#  define LOG_MMU(...) do { } while (0)
#  define LOG_MMU_STATE(...) do { } while (0)
#endif


#ifdef DEBUG_SOFTWARE_TLB
#  define LOG_SWTLB(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_SWTLB(...) do { } while (0)
#endif

#ifdef DEBUG_BATS
#  define LOG_BATS(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_BATS(...) do { } while (0)
#endif

#ifdef DEBUG_SLB
#  define LOG_SLB(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_SLB(...) do { } while (0)
#endif

#ifdef DEBUG_EXCEPTIONS
#  define LOG_EXCP(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_EXCP(...) do { } while (0)
#endif


/*****************************************************************************/
/* PowerPC MMU emulation */

#if defined(CONFIG_USER_ONLY)
int cpu_ppc_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu)
{
    int exception, error_code;

    if (rw == 2) {
        exception = POWERPC_EXCP_ISI;
        error_code = 0x40000000;
    } else {
        exception = POWERPC_EXCP_DSI;
        error_code = 0x40000000;
        if (rw)
            error_code |= 0x02000000;
        env->spr[SPR_DAR] = address;
        env->spr[SPR_DSISR] = error_code;
    }
    env->exception_index = exception;
    env->error_code = error_code;

    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug (CPUState *env, target_ulong addr)
{
    return addr;
}

#else
/* Common routines used by software and hardware TLBs emulation */
static always_inline int pte_is_valid (target_ulong pte0)
{
    return pte0 & 0x80000000 ? 1 : 0;
}

static always_inline void pte_invalidate (target_ulong *pte0)
{
    *pte0 &= ~0x80000000;
}

#if defined(TARGET_PPC64)
static always_inline int pte64_is_valid (target_ulong pte0)
{
    return pte0 & 0x0000000000000001ULL ? 1 : 0;
}

static always_inline void pte64_invalidate (target_ulong *pte0)
{
    *pte0 &= ~0x0000000000000001ULL;
}
#endif

#define PTE_PTEM_MASK 0x7FFFFFBF
#define PTE_CHECK_MASK (TARGET_PAGE_MASK | 0x7B)
#if defined(TARGET_PPC64)
#define PTE64_PTEM_MASK 0xFFFFFFFFFFFFFF80ULL
#define PTE64_CHECK_MASK (TARGET_PAGE_MASK | 0x7F)
#endif

static always_inline int pp_check (int key, int pp, int nx)
{
    int access;

    /* Compute access rights */
    /* When pp is 3/7, the result is undefined. Set it to noaccess */
    access = 0;
    if (key == 0) {
        switch (pp) {
        case 0x0:
        case 0x1:
        case 0x2:
            access |= PAGE_WRITE;
            /* No break here */
        case 0x3:
        case 0x6:
            access |= PAGE_READ;
            break;
        }
    } else {
        switch (pp) {
        case 0x0:
        case 0x6:
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
    if (nx == 0)
        access |= PAGE_EXEC;

    return access;
}

static always_inline int check_prot (int prot, int rw, int access_type)
{
    int ret;

    if (access_type == ACCESS_CODE) {
        if (prot & PAGE_EXEC)
            ret = 0;
        else
            ret = -2;
    } else if (rw) {
        if (prot & PAGE_WRITE)
            ret = 0;
        else
            ret = -2;
    } else {
        if (prot & PAGE_READ)
            ret = 0;
        else
            ret = -2;
    }

    return ret;
}

static always_inline int _pte_check (mmu_ctx_t *ctx, int is_64b,
                                     target_ulong pte0, target_ulong pte1,
                                     int h, int rw, int type)
{
    target_ulong ptem, mmask;
    int access, ret, pteh, ptev, pp;

    access = 0;
    ret = -1;
    /* Check validity and table match */
#if defined(TARGET_PPC64)
    if (is_64b) {
        ptev = pte64_is_valid(pte0);
        pteh = (pte0 >> 1) & 1;
    } else
#endif
    {
        ptev = pte_is_valid(pte0);
        pteh = (pte0 >> 6) & 1;
    }
    if (ptev && h == pteh) {
        /* Check vsid & api */
#if defined(TARGET_PPC64)
        if (is_64b) {
            ptem = pte0 & PTE64_PTEM_MASK;
            mmask = PTE64_CHECK_MASK;
            pp = (pte1 & 0x00000003) | ((pte1 >> 61) & 0x00000004);
            ctx->nx |= (pte1 >> 2) & 1; /* No execute bit */
            ctx->nx |= (pte1 >> 3) & 1; /* Guarded bit    */
        } else
#endif
        {
            ptem = pte0 & PTE_PTEM_MASK;
            mmask = PTE_CHECK_MASK;
            pp = pte1 & 0x00000003;
        }
        if (ptem == ctx->ptem) {
            if (ctx->raddr != (target_phys_addr_t)-1ULL) {
                /* all matches should have equal RPN, WIMG & PP */
                if ((ctx->raddr & mmask) != (pte1 & mmask)) {
                    qemu_log("Bad RPN/WIMG/PP\n");
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
                LOG_MMU("PTE access granted !\n");
            } else {
                /* Access right violation */
                LOG_MMU("PTE access rejected\n");
            }
        }
    }

    return ret;
}

static always_inline int pte32_check (mmu_ctx_t *ctx,
                                      target_ulong pte0, target_ulong pte1,
                                      int h, int rw, int type)
{
    return _pte_check(ctx, 0, pte0, pte1, h, rw, type);
}

#if defined(TARGET_PPC64)
static always_inline int pte64_check (mmu_ctx_t *ctx,
                                      target_ulong pte0, target_ulong pte1,
                                      int h, int rw, int type)
{
    return _pte_check(ctx, 1, pte0, pte1, h, rw, type);
}
#endif

static always_inline int pte_update_flags (mmu_ctx_t *ctx, target_ulong *pte1p,
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
static always_inline int ppc6xx_tlb_getnum (CPUState *env, target_ulong eaddr,
                                            int way, int is_code)
{
    int nr;

    /* Select TLB num in a way from address */
    nr = (eaddr >> TARGET_PAGE_BITS) & (env->tlb_per_way - 1);
    /* Select TLB way */
    nr += env->tlb_per_way * way;
    /* 6xx have separate TLBs for instructions and data */
    if (is_code && env->id_tlbs == 1)
        nr += env->nb_tlb;

    return nr;
}

static always_inline void ppc6xx_tlb_invalidate_all (CPUState *env)
{
    ppc6xx_tlb_t *tlb;
    int nr, max;

    //LOG_SWTLB("Invalidate all TLBs\n");
    /* Invalidate all defined software TLB */
    max = env->nb_tlb;
    if (env->id_tlbs == 1)
        max *= 2;
    for (nr = 0; nr < max; nr++) {
        tlb = &env->tlb[nr].tlb6;
        pte_invalidate(&tlb->pte0);
    }
    tlb_flush(env, 1);
}

static always_inline void __ppc6xx_tlb_invalidate_virt (CPUState *env,
                                                        target_ulong eaddr,
                                                        int is_code,
                                                        int match_epn)
{
#if !defined(FLUSH_ALL_TLBS)
    ppc6xx_tlb_t *tlb;
    int way, nr;

    /* Invalidate ITLB + DTLB, all ways */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way, is_code);
        tlb = &env->tlb[nr].tlb6;
        if (pte_is_valid(tlb->pte0) && (match_epn == 0 || eaddr == tlb->EPN)) {
            LOG_SWTLB("TLB invalidate %d/%d " ADDRX "\n",
                        nr, env->nb_tlb, eaddr);
            pte_invalidate(&tlb->pte0);
            tlb_flush_page(env, tlb->EPN);
        }
    }
#else
    /* XXX: PowerPC specification say this is valid as well */
    ppc6xx_tlb_invalidate_all(env);
#endif
}

static always_inline void ppc6xx_tlb_invalidate_virt (CPUState *env,
                                                      target_ulong eaddr,
                                                      int is_code)
{
    __ppc6xx_tlb_invalidate_virt(env, eaddr, is_code, 0);
}

void ppc6xx_tlb_store (CPUState *env, target_ulong EPN, int way, int is_code,
                       target_ulong pte0, target_ulong pte1)
{
    ppc6xx_tlb_t *tlb;
    int nr;

    nr = ppc6xx_tlb_getnum(env, EPN, way, is_code);
    tlb = &env->tlb[nr].tlb6;
    LOG_SWTLB("Set TLB %d/%d EPN " ADDRX " PTE0 " ADDRX
                " PTE1 " ADDRX "\n", nr, env->nb_tlb, EPN, pte0, pte1);
    /* Invalidate any pending reference in Qemu for this virtual address */
    __ppc6xx_tlb_invalidate_virt(env, EPN, is_code, 1);
    tlb->pte0 = pte0;
    tlb->pte1 = pte1;
    tlb->EPN = EPN;
    /* Store last way for LRU mechanism */
    env->last_way = way;
}

static always_inline int ppc6xx_tlb_check (CPUState *env, mmu_ctx_t *ctx,
                                           target_ulong eaddr, int rw,
                                           int access_type)
{
    ppc6xx_tlb_t *tlb;
    int nr, best, way;
    int ret;

    best = -1;
    ret = -1; /* No TLB found */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way,
                               access_type == ACCESS_CODE ? 1 : 0);
        tlb = &env->tlb[nr].tlb6;
        /* This test "emulates" the PTE index match for hardware TLBs */
        if ((eaddr & TARGET_PAGE_MASK) != tlb->EPN) {
            LOG_SWTLB("TLB %d/%d %s [" ADDRX " " ADDRX
                        "] <> " ADDRX "\n",
                        nr, env->nb_tlb,
                        pte_is_valid(tlb->pte0) ? "valid" : "inval",
                        tlb->EPN, tlb->EPN + TARGET_PAGE_SIZE, eaddr);
            continue;
        }
        LOG_SWTLB("TLB %d/%d %s " ADDRX " <> " ADDRX " " ADDRX
                    " %c %c\n",
                    nr, env->nb_tlb,
                    pte_is_valid(tlb->pte0) ? "valid" : "inval",
                    tlb->EPN, eaddr, tlb->pte1,
                    rw ? 'S' : 'L', access_type == ACCESS_CODE ? 'I' : 'D');
        switch (pte32_check(ctx, tlb->pte0, tlb->pte1, 0, rw, access_type)) {
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
        LOG_SWTLB("found TLB at addr " PADDRX " prot=%01x ret=%d\n",
                    ctx->raddr & TARGET_PAGE_MASK, ctx->prot, ret);
        /* Update page flags */
        pte_update_flags(ctx, &env->tlb[best].tlb6.pte1, ret, rw);
    }

    return ret;
}

/* Perform BAT hit & translation */
static always_inline void bat_size_prot (CPUState *env, target_ulong *blp,
                                         int *validp, int *protp,
                                         target_ulong *BATu, target_ulong *BATl)
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
            if (pp == 0x2)
                prot |= PAGE_WRITE;
        }
    }
    *blp = bl;
    *validp = valid;
    *protp = prot;
}

static always_inline void bat_601_size_prot (CPUState *env,target_ulong *blp,
                                             int *validp, int *protp,
                                             target_ulong *BATu,
                                             target_ulong *BATl)
{
    target_ulong bl;
    int key, pp, valid, prot;

    bl = (*BATl & 0x0000003F) << 17;
    LOG_BATS("b %02x ==> bl " ADDRX " msk " ADDRX "\n",
                (uint8_t)(*BATl & 0x0000003F), bl, ~bl);
    prot = 0;
    valid = (*BATl >> 6) & 1;
    if (valid) {
        pp = *BATu & 0x00000003;
        if (msr_pr == 0)
            key = (*BATu >> 3) & 1;
        else
            key = (*BATu >> 2) & 1;
        prot = pp_check(key, pp, 0);
    }
    *blp = bl;
    *validp = valid;
    *protp = prot;
}

static always_inline int get_bat (CPUState *env, mmu_ctx_t *ctx,
                                  target_ulong virtual, int rw, int type)
{
    target_ulong *BATlt, *BATut, *BATu, *BATl;
    target_ulong base, BEPIl, BEPIu, bl;
    int i, valid, prot;
    int ret = -1;

    LOG_BATS("%s: %cBAT v " ADDRX "\n", __func__,
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
    base = virtual & 0xFFFC0000;
    for (i = 0; i < env->nb_BATs; i++) {
        BATu = &BATut[i];
        BATl = &BATlt[i];
        BEPIu = *BATu & 0xF0000000;
        BEPIl = *BATu & 0x0FFE0000;
        if (unlikely(env->mmu_model == POWERPC_MMU_601)) {
            bat_601_size_prot(env, &bl, &valid, &prot, BATu, BATl);
        } else {
            bat_size_prot(env, &bl, &valid, &prot, BATu, BATl);
        }
        LOG_BATS("%s: %cBAT%d v " ADDRX " BATu " ADDRX
                    " BATl " ADDRX "\n", __func__,
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
                if (ret == 0)
                    LOG_BATS("BAT %d match: r " PADDRX " prot=%c%c\n",
                             i, ctx->raddr, ctx->prot & PAGE_READ ? 'R' : '-',
                             ctx->prot & PAGE_WRITE ? 'W' : '-');
                break;
            }
        }
    }
    if (ret < 0) {
#if defined(DEBUG_BATS)
        if (IS_LOGGING) {
            QEMU_LOG0("no BAT match for " ADDRX ":\n", virtual);
            for (i = 0; i < 4; i++) {
                BATu = &BATut[i];
                BATl = &BATlt[i];
                BEPIu = *BATu & 0xF0000000;
                BEPIl = *BATu & 0x0FFE0000;
                bl = (*BATu & 0x00001FFC) << 15;
                QEMU_LOG0("%s: %cBAT%d v " ADDRX " BATu " ADDRX
                        " BATl " ADDRX " \n\t" ADDRX " " ADDRX " " ADDRX "\n",
                        __func__, type == ACCESS_CODE ? 'I' : 'D', i, virtual,
                        *BATu, *BATl, BEPIu, BEPIl, bl);
            }
        }
#endif
    }
    /* No hit */
    return ret;
}

/* PTE table lookup */
static always_inline int _find_pte (mmu_ctx_t *ctx, int is_64b, int h,
                                    int rw, int type)
{
    target_ulong base, pte0, pte1;
    int i, good = -1;
    int ret, r;

    ret = -1; /* No entry found */
    base = ctx->pg_addr[h];
    for (i = 0; i < 8; i++) {
#if defined(TARGET_PPC64)
        if (is_64b) {
            pte0 = ldq_phys(base + (i * 16));
            pte1 =  ldq_phys(base + (i * 16) + 8);
            r = pte64_check(ctx, pte0, pte1, h, rw, type);
            LOG_MMU("Load pte from " ADDRX " => " ADDRX " " ADDRX
                        " %d %d %d " ADDRX "\n",
                        base + (i * 16), pte0, pte1,
                        (int)(pte0 & 1), h, (int)((pte0 >> 1) & 1),
                        ctx->ptem);
        } else
#endif
        {
            pte0 = ldl_phys(base + (i * 8));
            pte1 =  ldl_phys(base + (i * 8) + 4);
            r = pte32_check(ctx, pte0, pte1, h, rw, type);
            LOG_MMU("Load pte from " ADDRX " => " ADDRX " " ADDRX
                        " %d %d %d " ADDRX "\n",
                        base + (i * 8), pte0, pte1,
                        (int)(pte0 >> 31), h, (int)((pte0 >> 6) & 1),
                        ctx->ptem);
        }
        switch (r) {
        case -3:
            /* PTE inconsistency */
            return -1;
        case -2:
            /* Access violation */
            ret = -2;
            good = i;
            break;
        case -1:
        default:
            /* No PTE match */
            break;
        case 0:
            /* access granted */
            /* XXX: we should go on looping to check all PTEs consistency
             *      but if we can speed-up the whole thing as the
             *      result would be undefined if PTEs are not consistent.
             */
            ret = 0;
            good = i;
            goto done;
        }
    }
    if (good != -1) {
    done:
        LOG_MMU("found PTE at addr " PADDRX " prot=%01x ret=%d\n",
                    ctx->raddr, ctx->prot, ret);
        /* Update page flags */
        pte1 = ctx->raddr;
        if (pte_update_flags(ctx, &pte1, ret, rw) == 1) {
#if defined(TARGET_PPC64)
            if (is_64b) {
                stq_phys_notdirty(base + (good * 16) + 8, pte1);
            } else
#endif
            {
                stl_phys_notdirty(base + (good * 8) + 4, pte1);
            }
        }
    }

    return ret;
}

static always_inline int find_pte32 (mmu_ctx_t *ctx, int h, int rw, int type)
{
    return _find_pte(ctx, 0, h, rw, type);
}

#if defined(TARGET_PPC64)
static always_inline int find_pte64 (mmu_ctx_t *ctx, int h, int rw, int type)
{
    return _find_pte(ctx, 1, h, rw, type);
}
#endif

static always_inline int find_pte (CPUState *env, mmu_ctx_t *ctx,
                                   int h, int rw, int type)
{
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64)
        return find_pte64(ctx, h, rw, type);
#endif

    return find_pte32(ctx, h, rw, type);
}

#if defined(TARGET_PPC64)
static always_inline int slb_is_valid (uint64_t slb64)
{
    return slb64 & 0x0000000008000000ULL ? 1 : 0;
}

static always_inline void slb_invalidate (uint64_t *slb64)
{
    *slb64 &= ~0x0000000008000000ULL;
}

static always_inline int slb_lookup (CPUPPCState *env, target_ulong eaddr,
                                     target_ulong *vsid,
                                     target_ulong *page_mask, int *attr)
{
    target_phys_addr_t sr_base;
    target_ulong mask;
    uint64_t tmp64;
    uint32_t tmp;
    int n, ret;

    ret = -5;
    sr_base = env->spr[SPR_ASR];
    LOG_SLB("%s: eaddr " ADDRX " base " PADDRX "\n",
                __func__, eaddr, sr_base);
    mask = 0x0000000000000000ULL; /* Avoid gcc warning */
    for (n = 0; n < env->slb_nr; n++) {
        tmp64 = ldq_phys(sr_base);
        tmp = ldl_phys(sr_base + 8);
        LOG_SLB("%s: seg %d " PADDRX " %016" PRIx64 " %08"
                    PRIx32 "\n", __func__, n, sr_base, tmp64, tmp);
        if (slb_is_valid(tmp64)) {
            /* SLB entry is valid */
            switch (tmp64 & 0x0000000006000000ULL) {
            case 0x0000000000000000ULL:
                /* 256 MB segment */
                mask = 0xFFFFFFFFF0000000ULL;
                break;
            case 0x0000000002000000ULL:
                /* 1 TB segment */
                mask = 0xFFFF000000000000ULL;
                break;
            case 0x0000000004000000ULL:
            case 0x0000000006000000ULL:
                /* Reserved => segment is invalid */
                continue;
            }
            if ((eaddr & mask) == (tmp64 & mask)) {
                /* SLB match */
                *vsid = ((tmp64 << 24) | (tmp >> 8)) & 0x0003FFFFFFFFFFFFULL;
                *page_mask = ~mask;
                *attr = tmp & 0xFF;
                ret = n;
                break;
            }
        }
        sr_base += 12;
    }

    return ret;
}

void ppc_slb_invalidate_all (CPUPPCState *env)
{
    target_phys_addr_t sr_base;
    uint64_t tmp64;
    int n, do_invalidate;

    do_invalidate = 0;
    sr_base = env->spr[SPR_ASR];
    /* XXX: Warning: slbia never invalidates the first segment */
    for (n = 1; n < env->slb_nr; n++) {
        tmp64 = ldq_phys(sr_base);
        if (slb_is_valid(tmp64)) {
            slb_invalidate(&tmp64);
            stq_phys(sr_base, tmp64);
            /* XXX: given the fact that segment size is 256 MB or 1TB,
             *      and we still don't have a tlb_flush_mask(env, n, mask)
             *      in Qemu, we just invalidate all TLBs
             */
            do_invalidate = 1;
        }
        sr_base += 12;
    }
    if (do_invalidate)
        tlb_flush(env, 1);
}

void ppc_slb_invalidate_one (CPUPPCState *env, uint64_t T0)
{
    target_phys_addr_t sr_base;
    target_ulong vsid, page_mask;
    uint64_t tmp64;
    int attr;
    int n;

    n = slb_lookup(env, T0, &vsid, &page_mask, &attr);
    if (n >= 0) {
        sr_base = env->spr[SPR_ASR];
        sr_base += 12 * n;
        tmp64 = ldq_phys(sr_base);
        if (slb_is_valid(tmp64)) {
            slb_invalidate(&tmp64);
            stq_phys(sr_base, tmp64);
            /* XXX: given the fact that segment size is 256 MB or 1TB,
             *      and we still don't have a tlb_flush_mask(env, n, mask)
             *      in Qemu, we just invalidate all TLBs
             */
            tlb_flush(env, 1);
        }
    }
}

target_ulong ppc_load_slb (CPUPPCState *env, int slb_nr)
{
    target_phys_addr_t sr_base;
    target_ulong rt;
    uint64_t tmp64;
    uint32_t tmp;

    sr_base = env->spr[SPR_ASR];
    sr_base += 12 * slb_nr;
    tmp64 = ldq_phys(sr_base);
    tmp = ldl_phys(sr_base + 8);
    if (tmp64 & 0x0000000008000000ULL) {
        /* SLB entry is valid */
        /* Copy SLB bits 62:88 to Rt 37:63 (VSID 23:49) */
        rt = tmp >> 8;             /* 65:88 => 40:63 */
        rt |= (tmp64 & 0x7) << 24; /* 62:64 => 37:39 */
        /* Copy SLB bits 89:92 to Rt 33:36 (KsKpNL) */
        rt |= ((tmp >> 4) & 0xF) << 27;
    } else {
        rt = 0;
    }
    LOG_SLB("%s: " PADDRX " %016" PRIx64 " %08" PRIx32 " => %d "
                ADDRX "\n", __func__, sr_base, tmp64, tmp, slb_nr, rt);

    return rt;
}

void ppc_store_slb (CPUPPCState *env, int slb_nr, target_ulong rs)
{
    target_phys_addr_t sr_base;
    uint64_t tmp64;
    uint32_t tmp;

    sr_base = env->spr[SPR_ASR];
    sr_base += 12 * slb_nr;
    /* Copy Rs bits 37:63 to SLB 62:88 */
    tmp = rs << 8;
    tmp64 = (rs >> 24) & 0x7;
    /* Copy Rs bits 33:36 to SLB 89:92 */
    tmp |= ((rs >> 27) & 0xF) << 4;
    /* Set the valid bit */
    tmp64 |= 1 << 27;
    /* Set ESID */
    tmp64 |= (uint32_t)slb_nr << 28;
    LOG_SLB("%s: %d " ADDRX " => " PADDRX " %016" PRIx64
                " %08" PRIx32 "\n", __func__,
                slb_nr, rs, sr_base, tmp64, tmp);
    /* Write SLB entry to memory */
    stq_phys(sr_base, tmp64);
    stl_phys(sr_base + 8, tmp);
}
#endif /* defined(TARGET_PPC64) */

/* Perform segment based translation */
static always_inline target_phys_addr_t get_pgaddr (target_phys_addr_t sdr1,
                                                    int sdr_sh,
                                                    target_phys_addr_t hash,
                                                    target_phys_addr_t mask)
{
    return (sdr1 & ((target_phys_addr_t)(-1ULL) << sdr_sh)) | (hash & mask);
}

static always_inline int get_segment (CPUState *env, mmu_ctx_t *ctx,
                                      target_ulong eaddr, int rw, int type)
{
    target_phys_addr_t sdr, hash, mask, sdr_mask, htab_mask;
    target_ulong sr, vsid, vsid_mask, pgidx, page_mask;
#if defined(TARGET_PPC64)
    int attr;
#endif
    int ds, vsid_sh, sdr_sh, pr;
    int ret, ret2;

    pr = msr_pr;
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64) {
        LOG_MMU("Check SLBs\n");
        ret = slb_lookup(env, eaddr, &vsid, &page_mask, &attr);
        if (ret < 0)
            return ret;
        ctx->key = ((attr & 0x40) && (pr != 0)) ||
            ((attr & 0x80) && (pr == 0)) ? 1 : 0;
        ds = 0;
        ctx->nx = attr & 0x20 ? 1 : 0;
        vsid_mask = 0x00003FFFFFFFFF80ULL;
        vsid_sh = 7;
        sdr_sh = 18;
        sdr_mask = 0x3FF80;
    } else
#endif /* defined(TARGET_PPC64) */
    {
        sr = env->sr[eaddr >> 28];
        page_mask = 0x0FFFFFFF;
        ctx->key = (((sr & 0x20000000) && (pr != 0)) ||
                    ((sr & 0x40000000) && (pr == 0))) ? 1 : 0;
        ds = sr & 0x80000000 ? 1 : 0;
        ctx->nx = sr & 0x10000000 ? 1 : 0;
        vsid = sr & 0x00FFFFFF;
        vsid_mask = 0x01FFFFC0;
        vsid_sh = 6;
        sdr_sh = 16;
        sdr_mask = 0xFFC0;
        LOG_MMU("Check segment v=" ADDRX " %d " ADDRX
                    " nip=" ADDRX " lr=" ADDRX " ir=%d dr=%d pr=%d %d t=%d\n",
                    eaddr, (int)(eaddr >> 28), sr, env->nip,
                    env->lr, (int)msr_ir, (int)msr_dr, pr != 0 ? 1 : 0,
                    rw, type);
    }
    LOG_MMU("pte segment: key=%d ds %d nx %d vsid " ADDRX "\n",
                ctx->key, ds, ctx->nx, vsid);
    ret = -1;
    if (!ds) {
        /* Check if instruction fetch is allowed, if needed */
        if (type != ACCESS_CODE || ctx->nx == 0) {
            /* Page address translation */
            /* Primary table address */
            sdr = env->sdr1;
            pgidx = (eaddr & page_mask) >> TARGET_PAGE_BITS;
#if defined(TARGET_PPC64)
            if (env->mmu_model & POWERPC_MMU_64) {
                htab_mask = 0x0FFFFFFF >> (28 - (sdr & 0x1F));
                /* XXX: this is false for 1 TB segments */
                hash = ((vsid ^ pgidx) << vsid_sh) & vsid_mask;
            } else
#endif
            {
                htab_mask = sdr & 0x000001FF;
                hash = ((vsid ^ pgidx) << vsid_sh) & vsid_mask;
            }
            mask = (htab_mask << sdr_sh) | sdr_mask;
            LOG_MMU("sdr " PADDRX " sh %d hash " PADDRX
                        " mask " PADDRX " " ADDRX "\n",
                        sdr, sdr_sh, hash, mask, page_mask);
            ctx->pg_addr[0] = get_pgaddr(sdr, sdr_sh, hash, mask);
            /* Secondary table address */
            hash = (~hash) & vsid_mask;
            LOG_MMU("sdr " PADDRX " sh %d hash " PADDRX
                        " mask " PADDRX "\n",
                        sdr, sdr_sh, hash, mask);
            ctx->pg_addr[1] = get_pgaddr(sdr, sdr_sh, hash, mask);
#if defined(TARGET_PPC64)
            if (env->mmu_model & POWERPC_MMU_64) {
                /* Only 5 bits of the page index are used in the AVPN */
                ctx->ptem = (vsid << 12) | ((pgidx >> 4) & 0x0F80);
            } else
#endif
            {
                ctx->ptem = (vsid << 7) | (pgidx >> 10);
            }
            /* Initialize real address with an invalid value */
            ctx->raddr = (target_phys_addr_t)-1ULL;
            if (unlikely(env->mmu_model == POWERPC_MMU_SOFT_6xx ||
                         env->mmu_model == POWERPC_MMU_SOFT_74xx)) {
                /* Software TLB search */
                ret = ppc6xx_tlb_check(env, ctx, eaddr, rw, type);
            } else {
                LOG_MMU("0 sdr1=" PADDRX " vsid=" ADDRX " "
                            "api=" ADDRX " hash=" PADDRX
                            " pg_addr=" PADDRX "\n",
                            sdr, vsid, pgidx, hash, ctx->pg_addr[0]);
                /* Primary table lookup */
                ret = find_pte(env, ctx, 0, rw, type);
                if (ret < 0) {
                    /* Secondary table lookup */
                    if (eaddr != 0xEFFFFFFF)
                        LOG_MMU("1 sdr1=" PADDRX " vsid=" ADDRX " "
                                "api=" ADDRX " hash=" PADDRX
                                " pg_addr=" PADDRX "\n",
                                sdr, vsid, pgidx, hash, ctx->pg_addr[1]);
                    ret2 = find_pte(env, ctx, 1, rw, type);
                    if (ret2 != -1)
                        ret = ret2;
                }
            }
#if defined (DUMP_PAGE_TABLES)
            if (qemu_log_enabled()) {
                target_phys_addr_t curaddr;
                uint32_t a0, a1, a2, a3;
                qemu_log("Page table: " PADDRX " len " PADDRX "\n",
                          sdr, mask + 0x80);
                for (curaddr = sdr; curaddr < (sdr + mask + 0x80);
                     curaddr += 16) {
                    a0 = ldl_phys(curaddr);
                    a1 = ldl_phys(curaddr + 4);
                    a2 = ldl_phys(curaddr + 8);
                    a3 = ldl_phys(curaddr + 12);
                    if (a0 != 0 || a1 != 0 || a2 != 0 || a3 != 0) {
                        qemu_log(PADDRX ": %08x %08x %08x %08x\n",
                                  curaddr, a0, a1, a2, a3);
                    }
                }
            }
#endif
        } else {
            LOG_MMU("No access allowed\n");
            ret = -3;
        }
    } else {
        LOG_MMU("direct store...\n");
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
            qemu_log("ERROR: instruction should not need "
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
static always_inline int ppcemb_tlb_check (CPUState *env, ppcemb_tlb_t *tlb,
                                           target_phys_addr_t *raddrp,
                                           target_ulong address,
                                           uint32_t pid, int ext, int i)
{
    target_ulong mask;

    /* Check valid flag */
    if (!(tlb->prot & PAGE_VALID)) {
        qemu_log("%s: TLB %d not valid\n", __func__, i);
        return -1;
    }
    mask = ~(tlb->size - 1);
    LOG_SWTLB("%s: TLB %d address " ADDRX " PID %u <=> " ADDRX
                " " ADDRX " %u\n",
                __func__, i, address, pid, tlb->EPN, mask, (uint32_t)tlb->PID);
    /* Check PID */
    if (tlb->PID != 0 && tlb->PID != pid)
        return -1;
    /* Check effective address */
    if ((address & mask) != tlb->EPN)
        return -1;
    *raddrp = (tlb->RPN & mask) | (address & ~mask);
#if (TARGET_PHYS_ADDR_BITS >= 36)
    if (ext) {
        /* Extend the physical address to 36 bits */
        *raddrp |= (target_phys_addr_t)(tlb->RPN & 0xF) << 32;
    }
#endif

    return 0;
}

/* Generic TLB search function for PowerPC embedded implementations */
int ppcemb_tlb_search (CPUPPCState *env, target_ulong address, uint32_t pid)
{
    ppcemb_tlb_t *tlb;
    target_phys_addr_t raddr;
    int i, ret;

    /* Default return value is no match */
    ret = -1;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        if (ppcemb_tlb_check(env, tlb, &raddr, address, pid, 0, i) == 0) {
            ret = i;
            break;
        }
    }

    return ret;
}

/* Helpers specific to PowerPC 40x implementations */
static always_inline void ppc4xx_tlb_invalidate_all (CPUState *env)
{
    ppcemb_tlb_t *tlb;
    int i;

    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        tlb->prot &= ~PAGE_VALID;
    }
    tlb_flush(env, 1);
}

static always_inline void ppc4xx_tlb_invalidate_virt (CPUState *env,
                                                      target_ulong eaddr,
                                                      uint32_t pid)
{
#if !defined(FLUSH_ALL_TLBS)
    ppcemb_tlb_t *tlb;
    target_phys_addr_t raddr;
    target_ulong page, end;
    int i;

    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        if (ppcemb_tlb_check(env, tlb, &raddr, eaddr, pid, 0, i) == 0) {
            end = tlb->EPN + tlb->size;
            for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE)
                tlb_flush_page(env, page);
            tlb->prot &= ~PAGE_VALID;
            break;
        }
    }
#else
    ppc4xx_tlb_invalidate_all(env);
#endif
}

static int mmu40x_get_physical_address (CPUState *env, mmu_ctx_t *ctx,
                                 target_ulong address, int rw, int access_type)
{
    ppcemb_tlb_t *tlb;
    target_phys_addr_t raddr;
    int i, ret, zsel, zpr, pr;

    ret = -1;
    raddr = (target_phys_addr_t)-1ULL;
    pr = msr_pr;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        if (ppcemb_tlb_check(env, tlb, &raddr, address,
                             env->spr[SPR_40x_PID], 0, i) < 0)
            continue;
        zsel = (tlb->attr >> 4) & 0xF;
        zpr = (env->spr[SPR_40x_ZPR] >> (28 - (2 * zsel))) & 0x3;
        LOG_SWTLB("%s: TLB %d zsel %d zpr %d rw %d attr %08x\n",
                    __func__, i, zsel, zpr, rw, tlb->attr);
        /* Check execute enable bit */
        switch (zpr) {
        case 0x2:
            if (pr != 0)
                goto check_perms;
            /* No break here */
        case 0x3:
            /* All accesses granted */
            ctx->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            ret = 0;
            break;
        case 0x0:
            if (pr != 0) {
                ctx->prot = 0;
                ret = -2;
                break;
            }
            /* No break here */
        case 0x1:
        check_perms:
            /* Check from TLB entry */
            /* XXX: there is a problem here or in the TLB fill code... */
            ctx->prot = tlb->prot;
            ctx->prot |= PAGE_EXEC;
            ret = check_prot(ctx->prot, rw, access_type);
            break;
        }
        if (ret >= 0) {
            ctx->raddr = raddr;
            LOG_SWTLB("%s: access granted " ADDRX " => " PADDRX
                        " %d %d\n", __func__, address, ctx->raddr, ctx->prot,
                        ret);
            return 0;
        }
    }
    LOG_SWTLB("%s: access refused " ADDRX " => " PADDRX
                " %d %d\n", __func__, address, raddr, ctx->prot,
                ret);

    return ret;
}

void store_40x_sler (CPUPPCState *env, uint32_t val)
{
    /* XXX: TO BE FIXED */
    if (val != 0x00000000) {
        cpu_abort(env, "Little-endian regions are not supported by now\n");
    }
    env->spr[SPR_405_SLER] = val;
}

static int mmubooke_get_physical_address (CPUState *env, mmu_ctx_t *ctx,
                                          target_ulong address, int rw,
                                          int access_type)
{
    ppcemb_tlb_t *tlb;
    target_phys_addr_t raddr;
    int i, prot, ret;

    ret = -1;
    raddr = (target_phys_addr_t)-1ULL;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        if (ppcemb_tlb_check(env, tlb, &raddr, address,
                             env->spr[SPR_BOOKE_PID], 1, i) < 0)
            continue;
        if (msr_pr != 0)
            prot = tlb->prot & 0xF;
        else
            prot = (tlb->prot >> 4) & 0xF;
        /* Check the address space */
        if (access_type == ACCESS_CODE) {
            if (msr_ir != (tlb->attr & 1))
                continue;
            ctx->prot = prot;
            if (prot & PAGE_EXEC) {
                ret = 0;
                break;
            }
            ret = -3;
        } else {
            if (msr_dr != (tlb->attr & 1))
                continue;
            ctx->prot = prot;
            if ((!rw && prot & PAGE_READ) || (rw && (prot & PAGE_WRITE))) {
                ret = 0;
                break;
            }
            ret = -2;
        }
    }
    if (ret >= 0)
        ctx->raddr = raddr;

    return ret;
}

static always_inline int check_physical (CPUState *env, mmu_ctx_t *ctx,
                                         target_ulong eaddr, int rw)
{
    int in_plb, ret;

    ctx->raddr = eaddr;
    ctx->prot = PAGE_READ | PAGE_EXEC;
    ret = 0;
    switch (env->mmu_model) {
    case POWERPC_MMU_32B:
    case POWERPC_MMU_601:
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
    case POWERPC_MMU_SOFT_4xx:
    case POWERPC_MMU_REAL:
    case POWERPC_MMU_BOOKE:
        ctx->prot |= PAGE_WRITE;
        break;
#if defined(TARGET_PPC64)
    case POWERPC_MMU_620:
    case POWERPC_MMU_64B:
        /* Real address are 60 bits long */
        ctx->raddr &= 0x0FFFFFFFFFFFFFFFULL;
        ctx->prot |= PAGE_WRITE;
        break;
#endif
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
    case POWERPC_MMU_MPC8xx:
        /* XXX: TODO */
        cpu_abort(env, "MPC8xx MMU model is not implemented\n");
        break;
    case POWERPC_MMU_BOOKE_FSL:
        /* XXX: TODO */
        cpu_abort(env, "BookE FSL MMU model not implemented\n");
        break;
    default:
        cpu_abort(env, "Unknown or invalid MMU model\n");
        return -1;
    }

    return ret;
}

int get_physical_address (CPUState *env, mmu_ctx_t *ctx, target_ulong eaddr,
                          int rw, int access_type)
{
    int ret;

#if 0
    qemu_log("%s\n", __func__);
#endif
    if ((access_type == ACCESS_CODE && msr_ir == 0) ||
        (access_type != ACCESS_CODE && msr_dr == 0)) {
        /* No address translation */
        ret = check_physical(env, ctx, eaddr, rw);
    } else {
        ret = -1;
        switch (env->mmu_model) {
        case POWERPC_MMU_32B:
        case POWERPC_MMU_601:
        case POWERPC_MMU_SOFT_6xx:
        case POWERPC_MMU_SOFT_74xx:
#if defined(TARGET_PPC64)
        case POWERPC_MMU_620:
        case POWERPC_MMU_64B:
#endif
            /* Try to find a BAT */
            if (env->nb_BATs != 0)
                ret = get_bat(env, ctx, eaddr, rw, access_type);
            if (ret < 0) {
                /* We didn't match any BAT entry or don't have BATs */
                ret = get_segment(env, ctx, eaddr, rw, access_type);
            }
            break;
        case POWERPC_MMU_SOFT_4xx:
        case POWERPC_MMU_SOFT_4xx_Z:
            ret = mmu40x_get_physical_address(env, ctx, eaddr,
                                              rw, access_type);
            break;
        case POWERPC_MMU_BOOKE:
            ret = mmubooke_get_physical_address(env, ctx, eaddr,
                                                rw, access_type);
            break;
        case POWERPC_MMU_MPC8xx:
            /* XXX: TODO */
            cpu_abort(env, "MPC8xx MMU model is not implemented\n");
            break;
        case POWERPC_MMU_BOOKE_FSL:
            /* XXX: TODO */
            cpu_abort(env, "BookE FSL MMU model not implemented\n");
            return -1;
        case POWERPC_MMU_REAL:
            cpu_abort(env, "PowerPC in real mode do not do any translation\n");
            return -1;
        default:
            cpu_abort(env, "Unknown or invalid MMU model\n");
            return -1;
        }
    }
#if 0
    qemu_log("%s address " ADDRX " => %d " PADDRX "\n",
                __func__, eaddr, ret, ctx->raddr);
#endif

    return ret;
}

target_phys_addr_t cpu_get_phys_page_debug (CPUState *env, target_ulong addr)
{
    mmu_ctx_t ctx;

    if (unlikely(get_physical_address(env, &ctx, addr, 0, ACCESS_INT) != 0))
        return -1;

    return ctx.raddr & TARGET_PAGE_MASK;
}

/* Perform address translation */
int cpu_ppc_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu)
{
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
        ret = tlb_set_page_exec(env, address & TARGET_PAGE_MASK,
                                ctx.raddr & TARGET_PAGE_MASK, ctx.prot,
                                mmu_idx, is_softmmu);
    } else if (ret < 0) {
        LOG_MMU_STATE(env);
        if (access_type == ACCESS_CODE) {
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                switch (env->mmu_model) {
                case POWERPC_MMU_SOFT_6xx:
                    env->exception_index = POWERPC_EXCP_IFTLB;
                    env->error_code = 1 << 18;
                    env->spr[SPR_IMISS] = address;
                    env->spr[SPR_ICMP] = 0x80000000 | ctx.ptem;
                    goto tlb_miss;
                case POWERPC_MMU_SOFT_74xx:
                    env->exception_index = POWERPC_EXCP_IFTLB;
                    goto tlb_miss_74xx;
                case POWERPC_MMU_SOFT_4xx:
                case POWERPC_MMU_SOFT_4xx_Z:
                    env->exception_index = POWERPC_EXCP_ITLB;
                    env->error_code = 0;
                    env->spr[SPR_40x_DEAR] = address;
                    env->spr[SPR_40x_ESR] = 0x00000000;
                    break;
                case POWERPC_MMU_32B:
                case POWERPC_MMU_601:
#if defined(TARGET_PPC64)
                case POWERPC_MMU_620:
                case POWERPC_MMU_64B:
#endif
                    env->exception_index = POWERPC_EXCP_ISI;
                    env->error_code = 0x40000000;
                    break;
                case POWERPC_MMU_BOOKE:
                    /* XXX: TODO */
                    cpu_abort(env, "BookE MMU model is not implemented\n");
                    return -1;
                case POWERPC_MMU_BOOKE_FSL:
                    /* XXX: TODO */
                    cpu_abort(env, "BookE FSL MMU model is not implemented\n");
                    return -1;
                case POWERPC_MMU_MPC8xx:
                    /* XXX: TODO */
                    cpu_abort(env, "MPC8xx MMU model is not implemented\n");
                    break;
                case POWERPC_MMU_REAL:
                    cpu_abort(env, "PowerPC in real mode should never raise "
                              "any MMU exceptions\n");
                    return -1;
                default:
                    cpu_abort(env, "Unknown or invalid MMU model\n");
                    return -1;
                }
                break;
            case -2:
                /* Access rights violation */
                env->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x08000000;
                break;
            case -3:
                /* No execute protection violation */
                env->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x10000000;
                break;
            case -4:
                /* Direct store exception */
                /* No code fetch is allowed in direct-store areas */
                env->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x10000000;
                break;
#if defined(TARGET_PPC64)
            case -5:
                /* No match in segment table */
                if (env->mmu_model == POWERPC_MMU_620) {
                    env->exception_index = POWERPC_EXCP_ISI;
                    /* XXX: this might be incorrect */
                    env->error_code = 0x40000000;
                } else {
                    env->exception_index = POWERPC_EXCP_ISEG;
                    env->error_code = 0;
                }
                break;
#endif
            }
        } else {
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                switch (env->mmu_model) {
                case POWERPC_MMU_SOFT_6xx:
                    if (rw == 1) {
                        env->exception_index = POWERPC_EXCP_DSTLB;
                        env->error_code = 1 << 16;
                    } else {
                        env->exception_index = POWERPC_EXCP_DLTLB;
                        env->error_code = 0;
                    }
                    env->spr[SPR_DMISS] = address;
                    env->spr[SPR_DCMP] = 0x80000000 | ctx.ptem;
                tlb_miss:
                    env->error_code |= ctx.key << 19;
                    env->spr[SPR_HASH1] = ctx.pg_addr[0];
                    env->spr[SPR_HASH2] = ctx.pg_addr[1];
                    break;
                case POWERPC_MMU_SOFT_74xx:
                    if (rw == 1) {
                        env->exception_index = POWERPC_EXCP_DSTLB;
                    } else {
                        env->exception_index = POWERPC_EXCP_DLTLB;
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
                    env->exception_index = POWERPC_EXCP_DTLB;
                    env->error_code = 0;
                    env->spr[SPR_40x_DEAR] = address;
                    if (rw)
                        env->spr[SPR_40x_ESR] = 0x00800000;
                    else
                        env->spr[SPR_40x_ESR] = 0x00000000;
                    break;
                case POWERPC_MMU_32B:
                case POWERPC_MMU_601:
#if defined(TARGET_PPC64)
                case POWERPC_MMU_620:
                case POWERPC_MMU_64B:
#endif
                    env->exception_index = POWERPC_EXCP_DSI;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = address;
                    if (rw == 1)
                        env->spr[SPR_DSISR] = 0x42000000;
                    else
                        env->spr[SPR_DSISR] = 0x40000000;
                    break;
                case POWERPC_MMU_MPC8xx:
                    /* XXX: TODO */
                    cpu_abort(env, "MPC8xx MMU model is not implemented\n");
                    break;
                case POWERPC_MMU_BOOKE:
                    /* XXX: TODO */
                    cpu_abort(env, "BookE MMU model is not implemented\n");
                    return -1;
                case POWERPC_MMU_BOOKE_FSL:
                    /* XXX: TODO */
                    cpu_abort(env, "BookE FSL MMU model is not implemented\n");
                    return -1;
                case POWERPC_MMU_REAL:
                    cpu_abort(env, "PowerPC in real mode should never raise "
                              "any MMU exceptions\n");
                    return -1;
                default:
                    cpu_abort(env, "Unknown or invalid MMU model\n");
                    return -1;
                }
                break;
            case -2:
                /* Access rights violation */
                env->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                env->spr[SPR_DAR] = address;
                if (rw == 1)
                    env->spr[SPR_DSISR] = 0x0A000000;
                else
                    env->spr[SPR_DSISR] = 0x08000000;
                break;
            case -4:
                /* Direct store exception */
                switch (access_type) {
                case ACCESS_FLOAT:
                    /* Floating point load/store */
                    env->exception_index = POWERPC_EXCP_ALIGN;
                    env->error_code = POWERPC_EXCP_ALIGN_FP;
                    env->spr[SPR_DAR] = address;
                    break;
                case ACCESS_RES:
                    /* lwarx, ldarx or stwcx. */
                    env->exception_index = POWERPC_EXCP_DSI;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = address;
                    if (rw == 1)
                        env->spr[SPR_DSISR] = 0x06000000;
                    else
                        env->spr[SPR_DSISR] = 0x04000000;
                    break;
                case ACCESS_EXT:
                    /* eciwx or ecowx */
                    env->exception_index = POWERPC_EXCP_DSI;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = address;
                    if (rw == 1)
                        env->spr[SPR_DSISR] = 0x06100000;
                    else
                        env->spr[SPR_DSISR] = 0x04100000;
                    break;
                default:
                    printf("DSI: invalid exception (%d)\n", ret);
                    env->exception_index = POWERPC_EXCP_PROGRAM;
                    env->error_code =
                        POWERPC_EXCP_INVAL | POWERPC_EXCP_INVAL_INVAL;
                    env->spr[SPR_DAR] = address;
                    break;
                }
                break;
#if defined(TARGET_PPC64)
            case -5:
                /* No match in segment table */
                if (env->mmu_model == POWERPC_MMU_620) {
                    env->exception_index = POWERPC_EXCP_DSI;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = address;
                    /* XXX: this might be incorrect */
                    if (rw == 1)
                        env->spr[SPR_DSISR] = 0x42000000;
                    else
                        env->spr[SPR_DSISR] = 0x40000000;
                } else {
                    env->exception_index = POWERPC_EXCP_DSEG;
                    env->error_code = 0;
                    env->spr[SPR_DAR] = address;
                }
                break;
#endif
            }
        }
#if 0
        printf("%s: set exception to %d %02x\n", __func__,
               env->exception, env->error_code);
#endif
        ret = 1;
    }

    return ret;
}

/*****************************************************************************/
/* BATs management */
#if !defined(FLUSH_ALL_TLBS)
static always_inline void do_invalidate_BAT (CPUPPCState *env,
                                             target_ulong BATu,
                                             target_ulong mask)
{
    target_ulong base, end, page;

    base = BATu & ~0x0001FFFF;
    end = base + mask + 0x00020000;
    LOG_BATS("Flush BAT from " ADDRX " to " ADDRX " (" ADDRX ")\n",
                base, end, mask);
    for (page = base; page != end; page += TARGET_PAGE_SIZE)
        tlb_flush_page(env, page);
    LOG_BATS("Flush done\n");
}
#endif

static always_inline void dump_store_bat (CPUPPCState *env, char ID,
                                          int ul, int nr, target_ulong value)
{
    LOG_BATS("Set %cBAT%d%c to " ADDRX " (" ADDRX ")\n",
                ID, nr, ul == 0 ? 'u' : 'l', value, env->nip);
}

void ppc_store_ibatu (CPUPPCState *env, int nr, target_ulong value)
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

void ppc_store_ibatl (CPUPPCState *env, int nr, target_ulong value)
{
    dump_store_bat(env, 'I', 1, nr, value);
    env->IBAT[1][nr] = value;
}

void ppc_store_dbatu (CPUPPCState *env, int nr, target_ulong value)
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

void ppc_store_dbatl (CPUPPCState *env, int nr, target_ulong value)
{
    dump_store_bat(env, 'D', 1, nr, value);
    env->DBAT[1][nr] = value;
}

void ppc_store_ibatu_601 (CPUPPCState *env, int nr, target_ulong value)
{
    target_ulong mask;
    int do_inval;

    dump_store_bat(env, 'I', 0, nr, value);
    if (env->IBAT[0][nr] != value) {
        do_inval = 0;
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
        if (do_inval)
            tlb_flush(env, 1);
#endif
    }
}

void ppc_store_ibatl_601 (CPUPPCState *env, int nr, target_ulong value)
{
    target_ulong mask;
    int do_inval;

    dump_store_bat(env, 'I', 1, nr, value);
    if (env->IBAT[1][nr] != value) {
        do_inval = 0;
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
        if (do_inval)
            tlb_flush(env, 1);
#endif
    }
}

/*****************************************************************************/
/* TLB management */
void ppc_tlb_invalidate_all (CPUPPCState *env)
{
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
        cpu_abort(env, "No TLB for PowerPC 4xx in real mode\n");
        break;
    case POWERPC_MMU_MPC8xx:
        /* XXX: TODO */
        cpu_abort(env, "MPC8xx MMU model is not implemented\n");
        break;
    case POWERPC_MMU_BOOKE:
        /* XXX: TODO */
        cpu_abort(env, "BookE MMU model is not implemented\n");
        break;
    case POWERPC_MMU_BOOKE_FSL:
        /* XXX: TODO */
        if (!kvm_enabled())
            cpu_abort(env, "BookE MMU model is not implemented\n");
        break;
    case POWERPC_MMU_32B:
    case POWERPC_MMU_601:
#if defined(TARGET_PPC64)
    case POWERPC_MMU_620:
    case POWERPC_MMU_64B:
#endif /* defined(TARGET_PPC64) */
        tlb_flush(env, 1);
        break;
    default:
        /* XXX: TODO */
        cpu_abort(env, "Unknown MMU model\n");
        break;
    }
}

void ppc_tlb_invalidate_one (CPUPPCState *env, target_ulong addr)
{
#if !defined(FLUSH_ALL_TLBS)
    addr &= TARGET_PAGE_MASK;
    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
        ppc6xx_tlb_invalidate_virt(env, addr, 0);
        if (env->id_tlbs == 1)
            ppc6xx_tlb_invalidate_virt(env, addr, 1);
        break;
    case POWERPC_MMU_SOFT_4xx:
    case POWERPC_MMU_SOFT_4xx_Z:
        ppc4xx_tlb_invalidate_virt(env, addr, env->spr[SPR_40x_PID]);
        break;
    case POWERPC_MMU_REAL:
        cpu_abort(env, "No TLB for PowerPC 4xx in real mode\n");
        break;
    case POWERPC_MMU_MPC8xx:
        /* XXX: TODO */
        cpu_abort(env, "MPC8xx MMU model is not implemented\n");
        break;
    case POWERPC_MMU_BOOKE:
        /* XXX: TODO */
        cpu_abort(env, "BookE MMU model is not implemented\n");
        break;
    case POWERPC_MMU_BOOKE_FSL:
        /* XXX: TODO */
        cpu_abort(env, "BookE FSL MMU model is not implemented\n");
        break;
    case POWERPC_MMU_32B:
    case POWERPC_MMU_601:
        /* tlbie invalidate TLBs for all segments */
        addr &= ~((target_ulong)-1ULL << 28);
        /* XXX: this case should be optimized,
         * giving a mask to tlb_flush_page
         */
        tlb_flush_page(env, addr | (0x0 << 28));
        tlb_flush_page(env, addr | (0x1 << 28));
        tlb_flush_page(env, addr | (0x2 << 28));
        tlb_flush_page(env, addr | (0x3 << 28));
        tlb_flush_page(env, addr | (0x4 << 28));
        tlb_flush_page(env, addr | (0x5 << 28));
        tlb_flush_page(env, addr | (0x6 << 28));
        tlb_flush_page(env, addr | (0x7 << 28));
        tlb_flush_page(env, addr | (0x8 << 28));
        tlb_flush_page(env, addr | (0x9 << 28));
        tlb_flush_page(env, addr | (0xA << 28));
        tlb_flush_page(env, addr | (0xB << 28));
        tlb_flush_page(env, addr | (0xC << 28));
        tlb_flush_page(env, addr | (0xD << 28));
        tlb_flush_page(env, addr | (0xE << 28));
        tlb_flush_page(env, addr | (0xF << 28));
        break;
#if defined(TARGET_PPC64)
    case POWERPC_MMU_620:
    case POWERPC_MMU_64B:
        /* tlbie invalidate TLBs for all segments */
        /* XXX: given the fact that there are too many segments to invalidate,
         *      and we still don't have a tlb_flush_mask(env, n, mask) in Qemu,
         *      we just invalidate all TLBs
         */
        tlb_flush(env, 1);
        break;
#endif /* defined(TARGET_PPC64) */
    default:
        /* XXX: TODO */
        cpu_abort(env, "Unknown MMU model\n");
        break;
    }
#else
    ppc_tlb_invalidate_all(env);
#endif
}

/*****************************************************************************/
/* Special registers manipulation */
#if defined(TARGET_PPC64)
void ppc_store_asr (CPUPPCState *env, target_ulong value)
{
    if (env->asr != value) {
        env->asr = value;
        tlb_flush(env, 1);
    }
}
#endif

void ppc_store_sdr1 (CPUPPCState *env, target_ulong value)
{
    LOG_MMU("%s: " ADDRX "\n", __func__, value);
    if (env->sdr1 != value) {
        /* XXX: for PowerPC 64, should check that the HTABSIZE value
         *      is <= 28
         */
        env->sdr1 = value;
        tlb_flush(env, 1);
    }
}

void ppc_store_sr (CPUPPCState *env, int srnum, target_ulong value)
{
    LOG_MMU("%s: reg=%d " ADDRX " " ADDRX "\n",
                __func__, srnum, value, env->sr[srnum]);
    if (env->sr[srnum] != value) {
        env->sr[srnum] = value;
#if !defined(FLUSH_ALL_TLBS) && 0
        {
            target_ulong page, end;
            /* Invalidate 256 MB of virtual memory */
            page = (16 << 20) * srnum;
            end = page + (16 << 20);
            for (; page != end; page += TARGET_PAGE_SIZE)
                tlb_flush_page(env, page);
        }
#else
        tlb_flush(env, 1);
#endif
    }
}
#endif /* !defined (CONFIG_USER_ONLY) */

/* GDBstub can read and write MSR... */
void ppc_store_msr (CPUPPCState *env, target_ulong value)
{
    hreg_store_msr(env, value, 0);
}

/*****************************************************************************/
/* Exception processing */
#if defined (CONFIG_USER_ONLY)
void do_interrupt (CPUState *env)
{
    env->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
}

void ppc_hw_interrupt (CPUState *env)
{
    env->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
}
#else /* defined (CONFIG_USER_ONLY) */
static always_inline void dump_syscall (CPUState *env)
{
    qemu_log_mask(CPU_LOG_INT, "syscall r0=" REGX " r3=" REGX " r4=" REGX
            " r5=" REGX " r6=" REGX " nip=" ADDRX "\n",
            ppc_dump_gpr(env, 0), ppc_dump_gpr(env, 3), ppc_dump_gpr(env, 4),
            ppc_dump_gpr(env, 5), ppc_dump_gpr(env, 6), env->nip);
}

/* Note that this function should be greatly optimized
 * when called with a constant excp, from ppc_hw_interrupt
 */
static always_inline void powerpc_excp (CPUState *env,
                                        int excp_model, int excp)
{
    target_ulong msr, new_msr, vector;
    int srr0, srr1, asrr0, asrr1;
    int lpes0, lpes1, lev;

    if (0) {
        /* XXX: find a suitable condition to enable the hypervisor mode */
        lpes0 = (env->spr[SPR_LPCR] >> 1) & 1;
        lpes1 = (env->spr[SPR_LPCR] >> 2) & 1;
    } else {
        /* Those values ensure we won't enter the hypervisor mode */
        lpes0 = 0;
        lpes1 = 1;
    }

    qemu_log_mask(CPU_LOG_INT, "Raise exception at " ADDRX " => %08x (%02x)\n",
                 env->nip, excp, env->error_code);
    msr = env->msr;
    new_msr = msr;
    srr0 = SPR_SRR0;
    srr1 = SPR_SRR1;
    asrr0 = -1;
    asrr1 = -1;
    msr &= ~((target_ulong)0x783F0000);
    switch (excp) {
    case POWERPC_EXCP_NONE:
        /* Should never happen */
        return;
    case POWERPC_EXCP_CRITICAL:    /* Critical input                         */
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        switch (excp_model) {
        case POWERPC_EXCP_40x:
            srr0 = SPR_40x_SRR2;
            srr1 = SPR_40x_SRR3;
            break;
        case POWERPC_EXCP_BOOKE:
            srr0 = SPR_BOOKE_CSRR0;
            srr1 = SPR_BOOKE_CSRR1;
            break;
        case POWERPC_EXCP_G2:
            break;
        default:
            goto excp_invalid;
        }
        goto store_next;
    case POWERPC_EXCP_MCHECK:    /* Machine check exception                  */
        if (msr_me == 0) {
            /* Machine check exception is not enabled.
             * Enter checkstop state.
             */
            if (qemu_log_enabled()) {
                qemu_log("Machine check while not allowed. "
                        "Entering checkstop state\n");
            } else {
                fprintf(stderr, "Machine check while not allowed. "
                        "Entering checkstop state\n");
            }
            env->halted = 1;
            env->interrupt_request |= CPU_INTERRUPT_EXITTB;
        }
        new_msr &= ~((target_ulong)1 << MSR_RI);
        new_msr &= ~((target_ulong)1 << MSR_ME);
        if (0) {
            /* XXX: find a suitable condition to enable the hypervisor mode */
            new_msr |= (target_ulong)MSR_HVB;
        }
        /* XXX: should also have something loaded in DAR / DSISR */
        switch (excp_model) {
        case POWERPC_EXCP_40x:
            srr0 = SPR_40x_SRR2;
            srr1 = SPR_40x_SRR3;
            break;
        case POWERPC_EXCP_BOOKE:
            srr0 = SPR_BOOKE_MCSRR0;
            srr1 = SPR_BOOKE_MCSRR1;
            asrr0 = SPR_BOOKE_CSRR0;
            asrr1 = SPR_BOOKE_CSRR1;
            break;
        default:
            break;
        }
        goto store_next;
    case POWERPC_EXCP_DSI:       /* Data storage exception                   */
        LOG_EXCP("DSI exception: DSISR=" ADDRX" DAR=" ADDRX "\n",
                    env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
        LOG_EXCP("ISI exception: msr=" ADDRX ", nip=" ADDRX "\n",
                    msr, env->nip);
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        msr |= env->error_code;
        goto store_next;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes0 == 1)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        /* XXX: this is false */
        /* Get rS/rD and rA from faulting opcode */
        env->spr[SPR_DSISR] |= (ldl_code((env->nip - 4)) & 0x03FF0000) >> 16;
        goto store_current;
    case POWERPC_EXCP_PROGRAM:   /* Program exception                        */
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if ((msr_fe0 == 0 && msr_fe1 == 0) || msr_fp == 0) {
                LOG_EXCP("Ignore floating point exception\n");
                env->exception_index = POWERPC_EXCP_NONE;
                env->error_code = 0;
                return;
            }
            new_msr &= ~((target_ulong)1 << MSR_RI);
            if (lpes1 == 0)
                new_msr |= (target_ulong)MSR_HVB;
            msr |= 0x00100000;
            if (msr_fe0 == msr_fe1)
                goto store_next;
            msr |= 0x00010000;
            break;
        case POWERPC_EXCP_INVAL:
            LOG_EXCP("Invalid instruction at " ADDRX "\n",
                        env->nip);
            new_msr &= ~((target_ulong)1 << MSR_RI);
            if (lpes1 == 0)
                new_msr |= (target_ulong)MSR_HVB;
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            new_msr &= ~((target_ulong)1 << MSR_RI);
            if (lpes1 == 0)
                new_msr |= (target_ulong)MSR_HVB;
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            new_msr &= ~((target_ulong)1 << MSR_RI);
            if (lpes1 == 0)
                new_msr |= (target_ulong)MSR_HVB;
            msr |= 0x00020000;
            break;
        default:
            /* Should never occur */
            cpu_abort(env, "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        goto store_current;
    case POWERPC_EXCP_FPU:       /* Floating-point unavailable exception     */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_current;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
        /* NOTE: this is a temporary hack to support graphics OSI
           calls from the MOL driver */
        /* XXX: To be removed */
        if (env->gpr[3] == 0x113724fa && env->gpr[4] == 0x77810f9b &&
            env->osi_call) {
            if (env->osi_call(env) != 0) {
                env->exception_index = POWERPC_EXCP_NONE;
                env->error_code = 0;
                return;
            }
        }
        dump_syscall(env);
        new_msr &= ~((target_ulong)1 << MSR_RI);
        lev = env->error_code;
        if (lev == 1 || (lpes0 == 0 && lpes1 == 0))
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_APU:       /* Auxiliary processor unavailable          */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        goto store_current;
    case POWERPC_EXCP_DECR:      /* Decrementer exception                    */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_FIT:       /* Fixed-interval timer interrupt           */
        /* FIT on 4xx */
        LOG_EXCP("FIT exception\n");
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        goto store_next;
    case POWERPC_EXCP_WDT:       /* Watchdog timer interrupt                 */
        LOG_EXCP("WDT exception\n");
        switch (excp_model) {
        case POWERPC_EXCP_BOOKE:
            srr0 = SPR_BOOKE_CSRR0;
            srr1 = SPR_BOOKE_CSRR1;
            break;
        default:
            break;
        }
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        goto store_next;
    case POWERPC_EXCP_DTLB:      /* Data TLB error                           */
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        goto store_next;
    case POWERPC_EXCP_ITLB:      /* Instruction TLB error                    */
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        goto store_next;
    case POWERPC_EXCP_DEBUG:     /* Debug interrupt                          */
        switch (excp_model) {
        case POWERPC_EXCP_BOOKE:
            srr0 = SPR_BOOKE_DSRR0;
            srr1 = SPR_BOOKE_DSRR1;
            asrr0 = SPR_BOOKE_CSRR0;
            asrr1 = SPR_BOOKE_CSRR1;
            break;
        default:
            break;
        }
        /* XXX: TODO */
        cpu_abort(env, "Debug exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_SPEU:      /* SPE/embedded floating-point unavailable  */
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        goto store_current;
    case POWERPC_EXCP_EFPDI:     /* Embedded floating-point data interrupt   */
        /* XXX: TODO */
        cpu_abort(env, "Embedded floating point data exception "
                  "is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_EFPRI:     /* Embedded floating-point round interrupt  */
        /* XXX: TODO */
        cpu_abort(env, "Embedded floating point round exception "
                  "is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_EPERFM:    /* Embedded performance monitor interrupt   */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        /* XXX: TODO */
        cpu_abort(env,
                  "Performance counter exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_DOORI:     /* Embedded doorbell interrupt              */
        /* XXX: TODO */
        cpu_abort(env,
                  "Embedded doorbell interrupt is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_DOORCI:    /* Embedded doorbell critical interrupt     */
        switch (excp_model) {
        case POWERPC_EXCP_BOOKE:
            srr0 = SPR_BOOKE_CSRR0;
            srr1 = SPR_BOOKE_CSRR1;
            break;
        default:
            break;
        }
        /* XXX: TODO */
        cpu_abort(env, "Embedded doorbell critical interrupt "
                  "is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_RESET:     /* System reset exception                   */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (0) {
            /* XXX: find a suitable condition to enable the hypervisor mode */
            new_msr |= (target_ulong)MSR_HVB;
        }
        goto store_next;
    case POWERPC_EXCP_DSEG:      /* Data segment exception                   */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_ISEG:      /* Instruction segment exception            */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_HDECR:     /* Hypervisor decrementer exception         */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_TRACE:     /* Trace exception                          */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_HDSI:      /* Hypervisor data storage exception        */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_HISI:      /* Hypervisor instruction storage exception */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_HDSEG:     /* Hypervisor data segment exception        */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_HISEG:     /* Hypervisor instruction segment exception */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_VPU:       /* Vector unavailable exception             */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_current;
    case POWERPC_EXCP_PIT:       /* Programmable interval timer interrupt    */
        LOG_EXCP("PIT exception\n");
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        goto store_next;
    case POWERPC_EXCP_IO:        /* IO error exception                       */
        /* XXX: TODO */
        cpu_abort(env, "601 IO error exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_RUNM:      /* Run mode exception                       */
        /* XXX: TODO */
        cpu_abort(env, "601 run mode exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_EMUL:      /* Emulation trap exception                 */
        /* XXX: TODO */
        cpu_abort(env, "602 emulation trap exception "
                  "is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_IFTLB:     /* Instruction fetch TLB error              */
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        if (lpes1 == 0) /* XXX: check this */
            new_msr |= (target_ulong)MSR_HVB;
        switch (excp_model) {
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
            goto tlb_miss_tgpr;
        case POWERPC_EXCP_7x5:
            goto tlb_miss;
        case POWERPC_EXCP_74xx:
            goto tlb_miss_74xx;
        default:
            cpu_abort(env, "Invalid instruction TLB miss exception\n");
            break;
        }
        break;
    case POWERPC_EXCP_DLTLB:     /* Data load TLB miss                       */
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        if (lpes1 == 0) /* XXX: check this */
            new_msr |= (target_ulong)MSR_HVB;
        switch (excp_model) {
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
            goto tlb_miss_tgpr;
        case POWERPC_EXCP_7x5:
            goto tlb_miss;
        case POWERPC_EXCP_74xx:
            goto tlb_miss_74xx;
        default:
            cpu_abort(env, "Invalid data load TLB miss exception\n");
            break;
        }
        break;
    case POWERPC_EXCP_DSTLB:     /* Data store TLB miss                      */
        new_msr &= ~((target_ulong)1 << MSR_RI); /* XXX: check this */
        if (lpes1 == 0) /* XXX: check this */
            new_msr |= (target_ulong)MSR_HVB;
        switch (excp_model) {
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
        tlb_miss_tgpr:
            /* Swap temporary saved registers with GPRs */
            if (!(new_msr & ((target_ulong)1 << MSR_TGPR))) {
                new_msr |= (target_ulong)1 << MSR_TGPR;
                hreg_swap_gpr_tgpr(env);
            }
            goto tlb_miss;
        case POWERPC_EXCP_7x5:
        tlb_miss:
#if defined (DEBUG_SOFTWARE_TLB)
            if (qemu_log_enabled()) {
                const unsigned char *es;
                target_ulong *miss, *cmp;
                int en;
                if (excp == POWERPC_EXCP_IFTLB) {
                    es = "I";
                    en = 'I';
                    miss = &env->spr[SPR_IMISS];
                    cmp = &env->spr[SPR_ICMP];
                } else {
                    if (excp == POWERPC_EXCP_DLTLB)
                        es = "DL";
                    else
                        es = "DS";
                    en = 'D';
                    miss = &env->spr[SPR_DMISS];
                    cmp = &env->spr[SPR_DCMP];
                }
                qemu_log("6xx %sTLB miss: %cM " ADDRX " %cC " ADDRX
                        " H1 " ADDRX " H2 " ADDRX " %08x\n",
                        es, en, *miss, en, *cmp,
                        env->spr[SPR_HASH1], env->spr[SPR_HASH2],
                        env->error_code);
            }
#endif
            msr |= env->crf[0] << 28;
            msr |= env->error_code; /* key, D/I, S/L bits */
            /* Set way using a LRU mechanism */
            msr |= ((env->last_way + 1) & (env->nb_ways - 1)) << 17;
            break;
        case POWERPC_EXCP_74xx:
        tlb_miss_74xx:
#if defined (DEBUG_SOFTWARE_TLB)
            if (qemu_log_enabled()) {
                const unsigned char *es;
                target_ulong *miss, *cmp;
                int en;
                if (excp == POWERPC_EXCP_IFTLB) {
                    es = "I";
                    en = 'I';
                    miss = &env->spr[SPR_TLBMISS];
                    cmp = &env->spr[SPR_PTEHI];
                } else {
                    if (excp == POWERPC_EXCP_DLTLB)
                        es = "DL";
                    else
                        es = "DS";
                    en = 'D';
                    miss = &env->spr[SPR_TLBMISS];
                    cmp = &env->spr[SPR_PTEHI];
                }
                qemu_log("74xx %sTLB miss: %cM " ADDRX " %cC " ADDRX
                        " %08x\n",
                        es, en, *miss, en, *cmp, env->error_code);
            }
#endif
            msr |= env->error_code; /* key bit */
            break;
        default:
            cpu_abort(env, "Invalid data store TLB miss exception\n");
            break;
        }
        goto store_next;
    case POWERPC_EXCP_FPA:       /* Floating-point assist exception          */
        /* XXX: TODO */
        cpu_abort(env, "Floating point assist exception "
                  "is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_DABR:      /* Data address breakpoint                  */
        /* XXX: TODO */
        cpu_abort(env, "DABR exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_IABR:      /* Instruction address breakpoint           */
        /* XXX: TODO */
        cpu_abort(env, "IABR exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_SMI:       /* System management interrupt              */
        /* XXX: TODO */
        cpu_abort(env, "SMI exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_THERM:     /* Thermal interrupt                        */
        /* XXX: TODO */
        cpu_abort(env, "Thermal management exception "
                  "is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_PERFM:     /* Embedded performance monitor interrupt   */
        new_msr &= ~((target_ulong)1 << MSR_RI);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        /* XXX: TODO */
        cpu_abort(env,
                  "Performance counter exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_VPUA:      /* Vector assist exception                  */
        /* XXX: TODO */
        cpu_abort(env, "VPU assist exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_SOFTP:     /* Soft patch exception                     */
        /* XXX: TODO */
        cpu_abort(env,
                  "970 soft-patch exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_MAINT:     /* Maintenance exception                    */
        /* XXX: TODO */
        cpu_abort(env,
                  "970 maintenance exception is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_MEXTBR:    /* Maskable external breakpoint             */
        /* XXX: TODO */
        cpu_abort(env, "Maskable external exception "
                  "is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_NMEXTBR:   /* Non maskable external breakpoint         */
        /* XXX: TODO */
        cpu_abort(env, "Non maskable external exception "
                  "is not implemented yet !\n");
        goto store_next;
    default:
    excp_invalid:
        cpu_abort(env, "Invalid PowerPC exception %d. Aborting\n", excp);
        break;
    store_current:
        /* save current instruction location */
        env->spr[srr0] = env->nip - 4;
        break;
    store_next:
        /* save next instruction location */
        env->spr[srr0] = env->nip;
        break;
    }
    /* Save MSR */
    env->spr[srr1] = msr;
    /* If any alternate SRR register are defined, duplicate saved values */
    if (asrr0 != -1)
        env->spr[asrr0] = env->spr[srr0];
    if (asrr1 != -1)
        env->spr[asrr1] = env->spr[srr1];
    /* If we disactivated any translation, flush TLBs */
    if (new_msr & ((1 << MSR_IR) | (1 << MSR_DR)))
        tlb_flush(env, 1);
    /* reload MSR with correct bits */
    new_msr &= ~((target_ulong)1 << MSR_EE);
    new_msr &= ~((target_ulong)1 << MSR_PR);
    new_msr &= ~((target_ulong)1 << MSR_FP);
    new_msr &= ~((target_ulong)1 << MSR_FE0);
    new_msr &= ~((target_ulong)1 << MSR_SE);
    new_msr &= ~((target_ulong)1 << MSR_BE);
    new_msr &= ~((target_ulong)1 << MSR_FE1);
    new_msr &= ~((target_ulong)1 << MSR_IR);
    new_msr &= ~((target_ulong)1 << MSR_DR);
#if 0 /* Fix this: not on all targets */
    new_msr &= ~((target_ulong)1 << MSR_PMM);
#endif
    new_msr &= ~((target_ulong)1 << MSR_LE);
    if (msr_ile)
        new_msr |= (target_ulong)1 << MSR_LE;
    else
        new_msr &= ~((target_ulong)1 << MSR_LE);
    /* Jump to handler */
    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env, "Raised an exception without defined vector %d\n",
                  excp);
    }
    vector |= env->excp_prefix;
#if defined(TARGET_PPC64)
    if (excp_model == POWERPC_EXCP_BOOKE) {
        if (!msr_icm) {
            new_msr &= ~((target_ulong)1 << MSR_CM);
            vector = (uint32_t)vector;
        } else {
            new_msr |= (target_ulong)1 << MSR_CM;
        }
    } else {
        if (!msr_isf) {
            new_msr &= ~((target_ulong)1 << MSR_SF);
            vector = (uint32_t)vector;
        } else {
            new_msr |= (target_ulong)1 << MSR_SF;
        }
    }
#endif
    /* XXX: we don't use hreg_store_msr here as already have treated
     *      any special case that could occur. Just store MSR and update hflags
     */
    env->msr = new_msr & env->msr_mask;
    hreg_compute_hflags(env);
    env->nip = vector;
    /* Reset exception state */
    env->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
}

void do_interrupt (CPUState *env)
{
    powerpc_excp(env, env->excp_model, env->exception_index);
}

void ppc_hw_interrupt (CPUPPCState *env)
{
    int hdice;

#if 0
    qemu_log_mask(CPU_LOG_INT, "%s: %p pending %08x req %08x me %d ee %d\n",
                __func__, env, env->pending_interrupts,
                env->interrupt_request, (int)msr_me, (int)msr_ee);
#endif
    /* External reset */
    if (env->pending_interrupts & (1 << PPC_INTERRUPT_RESET)) {
        env->pending_interrupts &= ~(1 << PPC_INTERRUPT_RESET);
        powerpc_excp(env, env->excp_model, POWERPC_EXCP_RESET);
        return;
    }
    /* Machine check exception */
    if (env->pending_interrupts & (1 << PPC_INTERRUPT_MCK)) {
        env->pending_interrupts &= ~(1 << PPC_INTERRUPT_MCK);
        powerpc_excp(env, env->excp_model, POWERPC_EXCP_MCHECK);
        return;
    }
#if 0 /* TODO */
    /* External debug exception */
    if (env->pending_interrupts & (1 << PPC_INTERRUPT_DEBUG)) {
        env->pending_interrupts &= ~(1 << PPC_INTERRUPT_DEBUG);
        powerpc_excp(env, env->excp_model, POWERPC_EXCP_DEBUG);
        return;
    }
#endif
    if (0) {
        /* XXX: find a suitable condition to enable the hypervisor mode */
        hdice = env->spr[SPR_LPCR] & 1;
    } else {
        hdice = 0;
    }
    if ((msr_ee != 0 || msr_hv == 0 || msr_pr != 0) && hdice != 0) {
        /* Hypervisor decrementer exception */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_HDECR)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_HDECR);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_HDECR);
            return;
        }
    }
    if (msr_ce != 0) {
        /* External critical interrupt */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_CEXT)) {
            /* Taking a critical external interrupt does not clear the external
             * critical interrupt status
             */
#if 0
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_CEXT);
#endif
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_CRITICAL);
            return;
        }
    }
    if (msr_ee != 0) {
        /* Watchdog timer on embedded PowerPC */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_WDT)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_WDT);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_WDT);
            return;
        }
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_CDOORBELL)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_CDOORBELL);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_DOORCI);
            return;
        }
        /* Fixed interval timer on embedded PowerPC */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_FIT)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_FIT);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_FIT);
            return;
        }
        /* Programmable interval timer on embedded PowerPC */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_PIT)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_PIT);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_PIT);
            return;
        }
        /* Decrementer exception */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_DECR)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_DECR);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_DECR);
            return;
        }
        /* External interrupt */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_EXT)) {
            /* Taking an external interrupt does not clear the external
             * interrupt status
             */
#if 0
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_EXT);
#endif
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_EXTERNAL);
            return;
        }
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_DOORBELL)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_DOORBELL);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_DOORI);
            return;
        }
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_PERFM)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_PERFM);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_PERFM);
            return;
        }
        /* Thermal interrupt */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_THERM)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_THERM);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_THERM);
            return;
        }
    }
}
#endif /* !CONFIG_USER_ONLY */

void cpu_dump_rfi (target_ulong RA, target_ulong msr)
{
    qemu_log("Return from exception at " ADDRX " with flags " ADDRX "\n",
             RA, msr);
}

void cpu_ppc_reset (void *opaque)
{
    CPUPPCState *env = opaque;
    target_ulong msr;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, 0);
    }

    msr = (target_ulong)0;
    if (0) {
        /* XXX: find a suitable condition to enable the hypervisor mode */
        msr |= (target_ulong)MSR_HVB;
    }
    msr |= (target_ulong)0 << MSR_AP; /* TO BE CHECKED */
    msr |= (target_ulong)0 << MSR_SA; /* TO BE CHECKED */
    msr |= (target_ulong)1 << MSR_EP;
#if defined (DO_SINGLE_STEP) && 0
    /* Single step trace mode */
    msr |= (target_ulong)1 << MSR_SE;
    msr |= (target_ulong)1 << MSR_BE;
#endif
#if defined(CONFIG_USER_ONLY)
    msr |= (target_ulong)1 << MSR_FP; /* Allow floating point usage */
    msr |= (target_ulong)1 << MSR_VR; /* Allow altivec usage */
    msr |= (target_ulong)1 << MSR_SPE; /* Allow SPE usage */
    msr |= (target_ulong)1 << MSR_PR;
    env->msr = msr & env->msr_mask;
#else
    env->nip = env->hreset_vector | env->excp_prefix;
    if (env->mmu_model != POWERPC_MMU_REAL)
        ppc_tlb_invalidate_all(env);
#endif
    hreg_compute_hflags(env);
    env->reserve = (target_ulong)-1ULL;
    /* Be sure no exception or interrupt is pending */
    env->pending_interrupts = 0;
    env->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
    /* Flush all TLBs */
    tlb_flush(env, 1);
}

CPUPPCState *cpu_ppc_init (const char *cpu_model)
{
    CPUPPCState *env;
    const ppc_def_t *def;

    def = cpu_ppc_find_by_name(cpu_model);
    if (!def)
        return NULL;

    env = qemu_mallocz(sizeof(CPUPPCState));
    if (!env)
        return NULL;
    cpu_exec_init(env);
    ppc_translate_init();
    env->cpu_model_str = cpu_model;
    cpu_ppc_register_internal(env, def);
    cpu_ppc_reset(env);

    if (kvm_enabled())
        kvm_init_vcpu(env);

    return env;
}

void cpu_ppc_close (CPUPPCState *env)
{
    /* Should also remove all opcode tables... */
    qemu_free(env);
}
