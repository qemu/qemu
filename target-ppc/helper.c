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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

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
/* PowerPC Hypercall emulation */

void (*cpu_ppc_hypercall)(CPUState *);

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

#else
/* Common routines used by software and hardware TLBs emulation */
static inline int pte_is_valid(target_ulong pte0)
{
    return pte0 & 0x80000000 ? 1 : 0;
}

static inline void pte_invalidate(target_ulong *pte0)
{
    *pte0 &= ~0x80000000;
}

#if defined(TARGET_PPC64)
static inline int pte64_is_valid(target_ulong pte0)
{
    return pte0 & 0x0000000000000001ULL ? 1 : 0;
}

static inline void pte64_invalidate(target_ulong *pte0)
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

static inline int pp_check(int key, int pp, int nx)
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

static inline int check_prot(int prot, int rw, int access_type)
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

static inline int _pte_check(mmu_ctx_t *ctx, int is_64b, target_ulong pte0,
                             target_ulong pte1, int h, int rw, int type)
{
    target_ulong ptem, mmask;
    int access, ret, pteh, ptev, pp;

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
            ctx->nx  = (pte1 >> 2) & 1; /* No execute bit */
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

static inline int pte32_check(mmu_ctx_t *ctx, target_ulong pte0,
                              target_ulong pte1, int h, int rw, int type)
{
    return _pte_check(ctx, 0, pte0, pte1, h, rw, type);
}

#if defined(TARGET_PPC64)
static inline int pte64_check(mmu_ctx_t *ctx, target_ulong pte0,
                              target_ulong pte1, int h, int rw, int type)
{
    return _pte_check(ctx, 1, pte0, pte1, h, rw, type);
}
#endif

static inline int pte_update_flags(mmu_ctx_t *ctx, target_ulong *pte1p,
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
static inline int ppc6xx_tlb_getnum(CPUState *env, target_ulong eaddr, int way,
                                    int is_code)
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

static inline void ppc6xx_tlb_invalidate_all(CPUState *env)
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

static inline void __ppc6xx_tlb_invalidate_virt(CPUState *env,
                                                target_ulong eaddr,
                                                int is_code, int match_epn)
{
#if !defined(FLUSH_ALL_TLBS)
    ppc6xx_tlb_t *tlb;
    int way, nr;

    /* Invalidate ITLB + DTLB, all ways */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way, is_code);
        tlb = &env->tlb[nr].tlb6;
        if (pte_is_valid(tlb->pte0) && (match_epn == 0 || eaddr == tlb->EPN)) {
            LOG_SWTLB("TLB invalidate %d/%d " TARGET_FMT_lx "\n", nr,
                      env->nb_tlb, eaddr);
            pte_invalidate(&tlb->pte0);
            tlb_flush_page(env, tlb->EPN);
        }
    }
#else
    /* XXX: PowerPC specification say this is valid as well */
    ppc6xx_tlb_invalidate_all(env);
#endif
}

static inline void ppc6xx_tlb_invalidate_virt(CPUState *env,
                                              target_ulong eaddr, int is_code)
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
    LOG_SWTLB("Set TLB %d/%d EPN " TARGET_FMT_lx " PTE0 " TARGET_FMT_lx
              " PTE1 " TARGET_FMT_lx "\n", nr, env->nb_tlb, EPN, pte0, pte1);
    /* Invalidate any pending reference in Qemu for this virtual address */
    __ppc6xx_tlb_invalidate_virt(env, EPN, is_code, 1);
    tlb->pte0 = pte0;
    tlb->pte1 = pte1;
    tlb->EPN = EPN;
    /* Store last way for LRU mechanism */
    env->last_way = way;
}

static inline int ppc6xx_tlb_check(CPUState *env, mmu_ctx_t *ctx,
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
        tlb = &env->tlb[nr].tlb6;
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
        LOG_SWTLB("found TLB at addr " TARGET_FMT_plx " prot=%01x ret=%d\n",
                  ctx->raddr & TARGET_PAGE_MASK, ctx->prot, ret);
        /* Update page flags */
        pte_update_flags(ctx, &env->tlb[best].tlb6.pte1, ret, rw);
    }

    return ret;
}

/* Perform BAT hit & translation */
static inline void bat_size_prot(CPUState *env, target_ulong *blp, int *validp,
                                 int *protp, target_ulong *BATu,
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
            if (pp == 0x2)
                prot |= PAGE_WRITE;
        }
    }
    *blp = bl;
    *validp = valid;
    *protp = prot;
}

static inline void bat_601_size_prot(CPUState *env, target_ulong *blp,
                                     int *validp, int *protp,
                                     target_ulong *BATu, target_ulong *BATl)
{
    target_ulong bl;
    int key, pp, valid, prot;

    bl = (*BATl & 0x0000003F) << 17;
    LOG_BATS("b %02x ==> bl " TARGET_FMT_lx " msk " TARGET_FMT_lx "\n",
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

static inline int get_bat(CPUState *env, mmu_ctx_t *ctx, target_ulong virtual,
                          int rw, int type)
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
        if (unlikely(env->mmu_model == POWERPC_MMU_601)) {
            bat_601_size_prot(env, &bl, &valid, &prot, BATu, BATl);
        } else {
            bat_size_prot(env, &bl, &valid, &prot, BATu, BATl);
        }
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
                if (ret == 0)
                    LOG_BATS("BAT %d match: r " TARGET_FMT_plx " prot=%c%c\n",
                             i, ctx->raddr, ctx->prot & PAGE_READ ? 'R' : '-',
                             ctx->prot & PAGE_WRITE ? 'W' : '-');
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
                         " BATl " TARGET_FMT_lx " \n\t" TARGET_FMT_lx " "
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

static inline target_phys_addr_t get_pteg_offset(CPUState *env,
                                                 target_phys_addr_t hash,
                                                 int pte_size)
{
    return (hash * pte_size * 8) & env->htab_mask;
}

/* PTE table lookup */
static inline int _find_pte(CPUState *env, mmu_ctx_t *ctx, int is_64b, int h,
                            int rw, int type, int target_page_bits)
{
    target_phys_addr_t pteg_off;
    target_ulong pte0, pte1;
    int i, good = -1;
    int ret, r;

    ret = -1; /* No entry found */
    pteg_off = get_pteg_offset(env, ctx->hash[h],
                               is_64b ? HASH_PTE_SIZE_64 : HASH_PTE_SIZE_32);
    for (i = 0; i < 8; i++) {
#if defined(TARGET_PPC64)
        if (is_64b) {
            if (env->external_htab) {
                pte0 = ldq_p(env->external_htab + pteg_off + (i * 16));
                pte1 = ldq_p(env->external_htab + pteg_off + (i * 16) + 8);
            } else {
                pte0 = ldq_phys(env->htab_base + pteg_off + (i * 16));
                pte1 = ldq_phys(env->htab_base + pteg_off + (i * 16) + 8);
            }

            /* We have a TLB that saves 4K pages, so let's
             * split a huge page to 4k chunks */
            if (target_page_bits != TARGET_PAGE_BITS)
                pte1 |= (ctx->eaddr & (( 1 << target_page_bits ) - 1))
                        & TARGET_PAGE_MASK;

            r = pte64_check(ctx, pte0, pte1, h, rw, type);
            LOG_MMU("Load pte from " TARGET_FMT_lx " => " TARGET_FMT_lx " "
                    TARGET_FMT_lx " %d %d %d " TARGET_FMT_lx "\n",
                    pteg_off + (i * 16), pte0, pte1, (int)(pte0 & 1), h,
                    (int)((pte0 >> 1) & 1), ctx->ptem);
        } else
#endif
        {
            if (env->external_htab) {
                pte0 = ldl_p(env->external_htab + pteg_off + (i * 8));
                pte1 = ldl_p(env->external_htab + pteg_off + (i * 8) + 4);
            } else {
                pte0 = ldl_phys(env->htab_base + pteg_off + (i * 8));
                pte1 = ldl_phys(env->htab_base + pteg_off + (i * 8) + 4);
            }
            r = pte32_check(ctx, pte0, pte1, h, rw, type);
            LOG_MMU("Load pte from " TARGET_FMT_lx " => " TARGET_FMT_lx " "
                    TARGET_FMT_lx " %d %d %d " TARGET_FMT_lx "\n",
                    pteg_off + (i * 8), pte0, pte1, (int)(pte0 >> 31), h,
                    (int)((pte0 >> 6) & 1), ctx->ptem);
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
        LOG_MMU("found PTE at addr " TARGET_FMT_lx " prot=%01x ret=%d\n",
                ctx->raddr, ctx->prot, ret);
        /* Update page flags */
        pte1 = ctx->raddr;
        if (pte_update_flags(ctx, &pte1, ret, rw) == 1) {
#if defined(TARGET_PPC64)
            if (is_64b) {
                if (env->external_htab) {
                    stq_p(env->external_htab + pteg_off + (good * 16) + 8,
                          pte1);
                } else {
                    stq_phys_notdirty(env->htab_base + pteg_off +
                                      (good * 16) + 8, pte1);
                }
            } else
#endif
            {
                if (env->external_htab) {
                    stl_p(env->external_htab + pteg_off + (good * 8) + 4,
                          pte1);
                } else {
                    stl_phys_notdirty(env->htab_base + pteg_off +
                                      (good * 8) + 4, pte1);
                }
            }
        }
    }

    return ret;
}

static inline int find_pte(CPUState *env, mmu_ctx_t *ctx, int h, int rw,
                           int type, int target_page_bits)
{
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64)
        return _find_pte(env, ctx, 1, h, rw, type, target_page_bits);
#endif

    return _find_pte(env, ctx, 0, h, rw, type, target_page_bits);
}

#if defined(TARGET_PPC64)
static inline ppc_slb_t *slb_lookup(CPUPPCState *env, target_ulong eaddr)
{
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

void ppc_slb_invalidate_all (CPUPPCState *env)
{
    int n, do_invalidate;

    do_invalidate = 0;
    /* XXX: Warning: slbia never invalidates the first segment */
    for (n = 1; n < env->slb_nr; n++) {
        ppc_slb_t *slb = &env->slb[n];

        if (slb->esid & SLB_ESID_V) {
            slb->esid &= ~SLB_ESID_V;
            /* XXX: given the fact that segment size is 256 MB or 1TB,
             *      and we still don't have a tlb_flush_mask(env, n, mask)
             *      in Qemu, we just invalidate all TLBs
             */
            do_invalidate = 1;
        }
    }
    if (do_invalidate)
        tlb_flush(env, 1);
}

void ppc_slb_invalidate_one (CPUPPCState *env, uint64_t T0)
{
    ppc_slb_t *slb;

    slb = slb_lookup(env, T0);
    if (!slb) {
        return;
    }

    if (slb->esid & SLB_ESID_V) {
        slb->esid &= ~SLB_ESID_V;

        /* XXX: given the fact that segment size is 256 MB or 1TB,
         *      and we still don't have a tlb_flush_mask(env, n, mask)
         *      in Qemu, we just invalidate all TLBs
         */
        tlb_flush(env, 1);
    }
}

int ppc_store_slb (CPUPPCState *env, target_ulong rb, target_ulong rs)
{
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (rb & (0x1000 - env->slb_nr)) {
        return -1; /* Reserved bits set or slot too high */
    }
    if (rs & (SLB_VSID_B & ~SLB_VSID_B_1T)) {
        return -1; /* Bad segment size */
    }
    if ((rs & SLB_VSID_B) && !(env->mmu_model & POWERPC_MMU_1TSEG)) {
        return -1; /* 1T segment on MMU that doesn't support it */
    }

    /* Mask out the slot number as we store the entry */
    slb->esid = rb & (SLB_ESID_ESID | SLB_ESID_V);
    slb->vsid = rs;

    LOG_SLB("%s: %d " TARGET_FMT_lx " - " TARGET_FMT_lx " => %016" PRIx64
            " %016" PRIx64 "\n", __func__, slot, rb, rs,
            slb->esid, slb->vsid);

    return 0;
}

int ppc_load_slb_esid (CPUPPCState *env, target_ulong rb, target_ulong *rt)
{
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (slot >= env->slb_nr) {
        return -1;
    }

    *rt = slb->esid;
    return 0;
}

int ppc_load_slb_vsid (CPUPPCState *env, target_ulong rb, target_ulong *rt)
{
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (slot >= env->slb_nr) {
        return -1;
    }

    *rt = slb->vsid;
    return 0;
}
#endif /* defined(TARGET_PPC64) */

/* Perform segment based translation */
static inline int get_segment(CPUState *env, mmu_ctx_t *ctx,
                              target_ulong eaddr, int rw, int type)
{
    target_phys_addr_t hash;
    target_ulong vsid;
    int ds, pr, target_page_bits;
    int ret, ret2;

    pr = msr_pr;
    ctx->eaddr = eaddr;
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64) {
        ppc_slb_t *slb;
        target_ulong pageaddr;
        int segment_bits;

        LOG_MMU("Check SLBs\n");
        slb = slb_lookup(env, eaddr);
        if (!slb) {
            return -5;
        }

        if (slb->vsid & SLB_VSID_B) {
            vsid = (slb->vsid & SLB_VSID_VSID) >> SLB_VSID_SHIFT_1T;
            segment_bits = 40;
        } else {
            vsid = (slb->vsid & SLB_VSID_VSID) >> SLB_VSID_SHIFT;
            segment_bits = 28;
        }

        target_page_bits = (slb->vsid & SLB_VSID_L)
            ? TARGET_PAGE_BITS_16M : TARGET_PAGE_BITS;
        ctx->key = !!(pr ? (slb->vsid & SLB_VSID_KP)
                      : (slb->vsid & SLB_VSID_KS));
        ds = 0;
        ctx->nx = !!(slb->vsid & SLB_VSID_N);

        pageaddr = eaddr & ((1ULL << segment_bits)
                            - (1ULL << target_page_bits));
        if (slb->vsid & SLB_VSID_B) {
            hash = vsid ^ (vsid << 25) ^ (pageaddr >> target_page_bits);
        } else {
            hash = vsid ^ (pageaddr >> target_page_bits);
        }
        /* Only 5 bits of the page index are used in the AVPN */
        ctx->ptem = (slb->vsid & SLB_VSID_PTEM) |
            ((pageaddr >> 16) & ((1ULL << segment_bits) - 0x80));
    } else
#endif /* defined(TARGET_PPC64) */
    {
        target_ulong sr, pgidx;

        sr = env->sr[eaddr >> 28];
        ctx->key = (((sr & 0x20000000) && (pr != 0)) ||
                    ((sr & 0x40000000) && (pr == 0))) ? 1 : 0;
        ds = sr & 0x80000000 ? 1 : 0;
        ctx->nx = sr & 0x10000000 ? 1 : 0;
        vsid = sr & 0x00FFFFFF;
        target_page_bits = TARGET_PAGE_BITS;
        LOG_MMU("Check segment v=" TARGET_FMT_lx " %d " TARGET_FMT_lx " nip="
                TARGET_FMT_lx " lr=" TARGET_FMT_lx
                " ir=%d dr=%d pr=%d %d t=%d\n",
                eaddr, (int)(eaddr >> 28), sr, env->nip, env->lr, (int)msr_ir,
                (int)msr_dr, pr != 0 ? 1 : 0, rw, type);
        pgidx = (eaddr & ~SEGMENT_MASK_256M) >> target_page_bits;
        hash = vsid ^ pgidx;
        ctx->ptem = (vsid << 7) | (pgidx >> 10);
    }
    LOG_MMU("pte segment: key=%d ds %d nx %d vsid " TARGET_FMT_lx "\n",
            ctx->key, ds, ctx->nx, vsid);
    ret = -1;
    if (!ds) {
        /* Check if instruction fetch is allowed, if needed */
        if (type != ACCESS_CODE || ctx->nx == 0) {
            /* Page address translation */
            LOG_MMU("htab_base " TARGET_FMT_plx " htab_mask " TARGET_FMT_plx
                    " hash " TARGET_FMT_plx "\n",
                    env->htab_base, env->htab_mask, hash);
            ctx->hash[0] = hash;
            ctx->hash[1] = ~hash;

            /* Initialize real address with an invalid value */
            ctx->raddr = (target_phys_addr_t)-1ULL;
            if (unlikely(env->mmu_model == POWERPC_MMU_SOFT_6xx ||
                         env->mmu_model == POWERPC_MMU_SOFT_74xx)) {
                /* Software TLB search */
                ret = ppc6xx_tlb_check(env, ctx, eaddr, rw, type);
            } else {
                LOG_MMU("0 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                        " vsid=" TARGET_FMT_lx " ptem=" TARGET_FMT_lx
                        " hash=" TARGET_FMT_plx "\n",
                        env->htab_base, env->htab_mask, vsid, ctx->ptem,
                        ctx->hash[0]);
                /* Primary table lookup */
                ret = find_pte(env, ctx, 0, rw, type, target_page_bits);
                if (ret < 0) {
                    /* Secondary table lookup */
                    if (eaddr != 0xEFFFFFFF)
                        LOG_MMU("1 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                                " vsid=" TARGET_FMT_lx " api=" TARGET_FMT_lx
                                " hash=" TARGET_FMT_plx "\n", env->htab_base,
                                env->htab_mask, vsid, ctx->ptem, ctx->hash[1]);
                    ret2 = find_pte(env, ctx, 1, rw, type,
                                    target_page_bits);
                    if (ret2 != -1)
                        ret = ret2;
                }
            }
#if defined (DUMP_PAGE_TABLES)
            if (qemu_log_enabled()) {
                target_phys_addr_t curaddr;
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
            LOG_MMU("No access allowed\n");
            ret = -3;
        }
    } else {
        target_ulong sr;
        LOG_MMU("direct store...\n");
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
int ppcemb_tlb_check(CPUState *env, ppcemb_tlb_t *tlb,
                     target_phys_addr_t *raddrp,
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
static inline void ppc4xx_tlb_invalidate_all(CPUState *env)
{
    ppcemb_tlb_t *tlb;
    int i;

    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        tlb->prot &= ~PAGE_VALID;
    }
    tlb_flush(env, 1);
}

static inline void ppc4xx_tlb_invalidate_virt(CPUState *env,
                                              target_ulong eaddr, uint32_t pid)
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
        zpr = (env->spr[SPR_40x_ZPR] >> (30 - (2 * zsel))) & 0x3;
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
            if (ret == -2)
                env->spr[SPR_40x_ESR] = 0;
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

void store_40x_sler (CPUPPCState *env, uint32_t val)
{
    /* XXX: TO BE FIXED */
    if (val != 0x00000000) {
        cpu_abort(env, "Little-endian regions are not supported by now\n");
    }
    env->spr[SPR_405_SLER] = val;
}

static inline int mmubooke_check_tlb (CPUState *env, ppcemb_tlb_t *tlb,
                                      target_phys_addr_t *raddr, int *prot,
                                      target_ulong address, int rw,
                                      int access_type, int i)
{
    int ret, _prot;

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
        _prot = tlb->prot & 0xF;
    } else {
        _prot = (tlb->prot >> 4) & 0xF;
    }

    /* Check the address space */
    if (access_type == ACCESS_CODE) {
        if (msr_ir != (tlb->attr & 1)) {
            LOG_SWTLB("%s: AS doesn't match\n", __func__);
            return -1;
        }

        *prot = _prot;
        if (_prot & PAGE_EXEC) {
            LOG_SWTLB("%s: good TLB!\n", __func__);
            return 0;
        }

        LOG_SWTLB("%s: no PAGE_EXEC: %x\n", __func__, _prot);
        ret = -3;
    } else {
        if (msr_dr != (tlb->attr & 1)) {
            LOG_SWTLB("%s: AS doesn't match\n", __func__);
            return -1;
        }

        *prot = _prot;
        if ((!rw && _prot & PAGE_READ) || (rw && (_prot & PAGE_WRITE))) {
            LOG_SWTLB("%s: found TLB!\n", __func__);
            return 0;
        }

        LOG_SWTLB("%s: PAGE_READ/WRITE doesn't match: %x\n", __func__, _prot);
        ret = -2;
    }

    return ret;
}

static int mmubooke_get_physical_address (CPUState *env, mmu_ctx_t *ctx,
                                          target_ulong address, int rw,
                                          int access_type)
{
    ppcemb_tlb_t *tlb;
    target_phys_addr_t raddr;
    int i, ret;

    ret = -1;
    raddr = (target_phys_addr_t)-1ULL;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
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

void booke206_flush_tlb(CPUState *env, int flags, const int check_iprot)
{
    int tlb_size;
    int i, j;
    ppc_tlb_t *tlb = env->tlb;

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        if (flags & (1 << i)) {
            tlb_size = booke206_tlb_size(env, i);
            for (j = 0; j < tlb_size; j++) {
                if (!check_iprot || !(tlb[j].tlbm.mas1 & MAS1_IPROT)) {
                    tlb[j].tlbm.mas1 &= ~MAS1_VALID;
                }
            }
        }
        tlb += booke206_tlb_size(env, i);
    }

    tlb_flush(env, 1);
}

target_phys_addr_t booke206_tlb_to_page_size(CPUState *env, ppcmas_tlb_t *tlb)
{
    uint32_t tlbncfg;
    int tlbn = booke206_tlbm_to_tlbn(env, tlb);
    target_phys_addr_t tlbm_size;

    tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlbn];

    if (tlbncfg & TLBnCFG_AVAIL) {
        tlbm_size = (tlb->mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
    } else {
        tlbm_size = (tlbncfg & TLBnCFG_MINSIZE) >> TLBnCFG_MINSIZE_SHIFT;
    }

    return (1 << (tlbm_size << 1)) << 10;
}

/* TLB check function for MAS based SoftTLBs */
int ppcmas_tlb_check(CPUState *env, ppcmas_tlb_t *tlb,
                     target_phys_addr_t *raddrp,
                     target_ulong address, uint32_t pid)
{
    target_ulong mask;
    uint32_t tlb_pid;

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
    *raddrp = (tlb->mas7_3 & mask) | (address & ~mask);

    return 0;
}

static int mmubooke206_check_tlb(CPUState *env, ppcmas_tlb_t *tlb,
                                 target_phys_addr_t *raddr, int *prot,
                                 target_ulong address, int rw,
                                 int access_type)
{
    int ret;
    int _prot = 0;

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
            _prot |= PAGE_READ;
        }
        if (tlb->mas7_3 & MAS3_UW) {
            _prot |= PAGE_WRITE;
        }
        if (tlb->mas7_3 & MAS3_UX) {
            _prot |= PAGE_EXEC;
        }
    } else {
        if (tlb->mas7_3 & MAS3_SR) {
            _prot |= PAGE_READ;
        }
        if (tlb->mas7_3 & MAS3_SW) {
            _prot |= PAGE_WRITE;
        }
        if (tlb->mas7_3 & MAS3_SX) {
            _prot |= PAGE_EXEC;
        }
    }

    /* Check the address space and permissions */
    if (access_type == ACCESS_CODE) {
        if (msr_ir != ((tlb->mas1 & MAS1_TS) >> MAS1_TS_SHIFT)) {
            LOG_SWTLB("%s: AS doesn't match\n", __func__);
            return -1;
        }

        *prot = _prot;
        if (_prot & PAGE_EXEC) {
            LOG_SWTLB("%s: good TLB!\n", __func__);
            return 0;
        }

        LOG_SWTLB("%s: no PAGE_EXEC: %x\n", __func__, _prot);
        ret = -3;
    } else {
        if (msr_dr != ((tlb->mas1 & MAS1_TS) >> MAS1_TS_SHIFT)) {
            LOG_SWTLB("%s: AS doesn't match\n", __func__);
            return -1;
        }

        *prot = _prot;
        if ((!rw && _prot & PAGE_READ) || (rw && (_prot & PAGE_WRITE))) {
            LOG_SWTLB("%s: found TLB!\n", __func__);
            return 0;
        }

        LOG_SWTLB("%s: PAGE_READ/WRITE doesn't match: %x\n", __func__, _prot);
        ret = -2;
    }

    return ret;
}

static int mmubooke206_get_physical_address(CPUState *env, mmu_ctx_t *ctx,
                                            target_ulong address, int rw,
                                            int access_type)
{
    ppcmas_tlb_t *tlb;
    target_phys_addr_t raddr;
    int i, j, ret;

    ret = -1;
    raddr = (target_phys_addr_t)-1ULL;

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int ways = booke206_tlb_ways(env, i);

        for (j = 0; j < ways; j++) {
            tlb = booke206_get_tlbm(env, i, address, j);
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

static inline int check_physical(CPUState *env, mmu_ctx_t *ctx,
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
    case POWERPC_MMU_2_06:
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
    case POWERPC_MMU_BOOKE206:
        cpu_abort(env, "BookE 2.06 MMU doesn't have physical real mode\n");
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
        if (env->mmu_model == POWERPC_MMU_BOOKE) {
            /* The BookE MMU always performs address translation. The
               IS and DS bits only affect the address space.  */
            ret = mmubooke_get_physical_address(env, ctx, eaddr,
                                                rw, access_type);
        } else if (env->mmu_model == POWERPC_MMU_BOOKE206) {
            ret = mmubooke206_get_physical_address(env, ctx, eaddr, rw,
                                                   access_type);
        } else {
            /* No address translation.  */
            ret = check_physical(env, ctx, eaddr, rw);
        }
    } else {
        ret = -1;
        switch (env->mmu_model) {
        case POWERPC_MMU_32B:
        case POWERPC_MMU_601:
        case POWERPC_MMU_SOFT_6xx:
        case POWERPC_MMU_SOFT_74xx:
            /* Try to find a BAT */
            if (env->nb_BATs != 0)
                ret = get_bat(env, ctx, eaddr, rw, access_type);
#if defined(TARGET_PPC64)
        case POWERPC_MMU_620:
        case POWERPC_MMU_64B:
        case POWERPC_MMU_2_06:
#endif
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
        case POWERPC_MMU_BOOKE206:
            ret = mmubooke206_get_physical_address(env, ctx, eaddr, rw,
                                               access_type);
            break;
        case POWERPC_MMU_MPC8xx:
            /* XXX: TODO */
            cpu_abort(env, "MPC8xx MMU model is not implemented\n");
            break;
        case POWERPC_MMU_REAL:
            cpu_abort(env, "PowerPC in real mode do not do any translation\n");
            return -1;
        default:
            cpu_abort(env, "Unknown or invalid MMU model\n");
            return -1;
        }
    }
#if 0
    qemu_log("%s address " TARGET_FMT_lx " => %d " TARGET_FMT_plx "\n",
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

static void booke206_update_mas_tlb_miss(CPUState *env, target_ulong address,
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
        tlb_set_page(env, address & TARGET_PAGE_MASK,
                     ctx.raddr & TARGET_PAGE_MASK, ctx.prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        ret = 0;
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
                case POWERPC_MMU_2_06:
#endif
                    env->exception_index = POWERPC_EXCP_ISI;
                    env->error_code = 0x40000000;
                    break;
                case POWERPC_MMU_BOOKE206:
                    booke206_update_mas_tlb_miss(env, address, rw);
                    /* fall through */
                case POWERPC_MMU_BOOKE:
                    env->exception_index = POWERPC_EXCP_ITLB;
                    env->error_code = 0;
                    env->spr[SPR_BOOKE_DEAR] = address;
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
                if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
                    (env->mmu_model == POWERPC_MMU_BOOKE206)) {
                    env->spr[SPR_BOOKE_ESR] = 0x00000000;
                }
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
                    env->spr[SPR_HASH1] = env->htab_base +
                        get_pteg_offset(env, ctx.hash[0], HASH_PTE_SIZE_32);
                    env->spr[SPR_HASH2] = env->htab_base +
                        get_pteg_offset(env, ctx.hash[1], HASH_PTE_SIZE_32);
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
                case POWERPC_MMU_2_06:
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
                case POWERPC_MMU_BOOKE206:
                    booke206_update_mas_tlb_miss(env, address, rw);
                    /* fall through */
                case POWERPC_MMU_BOOKE:
                    env->exception_index = POWERPC_EXCP_DTLB;
                    env->error_code = 0;
                    env->spr[SPR_BOOKE_DEAR] = address;
                    env->spr[SPR_BOOKE_ESR] = rw ? 1 << ESR_ST : 0;
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
                if (env->mmu_model == POWERPC_MMU_SOFT_4xx
                    || env->mmu_model == POWERPC_MMU_SOFT_4xx_Z) {
                    env->spr[SPR_40x_DEAR] = address;
                    if (rw) {
                        env->spr[SPR_40x_ESR] |= 0x00800000;
                    }
                } else if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
                           (env->mmu_model == POWERPC_MMU_BOOKE206)) {
                    env->spr[SPR_BOOKE_DEAR] = address;
                    env->spr[SPR_BOOKE_ESR] = rw ? 1 << ESR_ST : 0;
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
static inline void do_invalidate_BAT(CPUPPCState *env, target_ulong BATu,
                                     target_ulong mask)
{
    target_ulong base, end, page;

    base = BATu & ~0x0001FFFF;
    end = base + mask + 0x00020000;
    LOG_BATS("Flush BAT from " TARGET_FMT_lx " to " TARGET_FMT_lx " ("
             TARGET_FMT_lx ")\n", base, end, mask);
    for (page = base; page != end; page += TARGET_PAGE_SIZE)
        tlb_flush_page(env, page);
    LOG_BATS("Flush done\n");
}
#endif

static inline void dump_store_bat(CPUPPCState *env, char ID, int ul, int nr,
                                  target_ulong value)
{
    LOG_BATS("Set %cBAT%d%c to " TARGET_FMT_lx " (" TARGET_FMT_lx ")\n", ID,
             nr, ul == 0 ? 'u' : 'l', value, env->nip);
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
        if (do_inval)
            tlb_flush(env, 1);
#endif
    }
}

void ppc_store_ibatl_601 (CPUPPCState *env, int nr, target_ulong value)
{
    target_ulong mask;
#if defined(FLUSH_ALL_TLBS)
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
        tlb_flush(env, 1);
        break;
    case POWERPC_MMU_BOOKE206:
        booke206_flush_tlb(env, -1, 0);
        break;
    case POWERPC_MMU_32B:
    case POWERPC_MMU_601:
#if defined(TARGET_PPC64)
    case POWERPC_MMU_620:
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_06:
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
    case POWERPC_MMU_BOOKE206:
        /* XXX: TODO */
        cpu_abort(env, "BookE 2.06 MMU model is not implemented\n");
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
    case POWERPC_MMU_2_06:
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
    LOG_MMU("%s: " TARGET_FMT_lx "\n", __func__, value);
    if (env->spr[SPR_SDR1] != value) {
        env->spr[SPR_SDR1] = value;
#if defined(TARGET_PPC64)
        if (env->mmu_model & POWERPC_MMU_64) {
            target_ulong htabsize = value & SDR_64_HTABSIZE;

            if (htabsize > 28) {
                fprintf(stderr, "Invalid HTABSIZE 0x" TARGET_FMT_lx
                        " stored in SDR1\n", htabsize);
                htabsize = 28;
            }
            env->htab_mask = (1ULL << (htabsize + 18)) - 1;
            env->htab_base = value & SDR_64_HTABORG;
        } else
#endif /* defined(TARGET_PPC64) */
        {
            /* FIXME: Should check for valid HTABMASK values */
            env->htab_mask = ((value & SDR_32_HTABMASK) << 16) | 0xFFFF;
            env->htab_base = value & SDR_32_HTABORG;
        }
        tlb_flush(env, 1);
    }
}

#if defined(TARGET_PPC64)
target_ulong ppc_load_sr (CPUPPCState *env, int slb_nr)
{
    // XXX
    return 0;
}
#endif

void ppc_store_sr (CPUPPCState *env, int srnum, target_ulong value)
{
    LOG_MMU("%s: reg=%d " TARGET_FMT_lx " " TARGET_FMT_lx "\n", __func__,
            srnum, value, env->sr[srnum]);
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64) {
        uint64_t rb = 0, rs = 0;

        /* ESID = srnum */
        rb |= ((uint32_t)srnum & 0xf) << 28;
        /* Set the valid bit */
        rb |= 1 << 27;
        /* Index = ESID */
        rb |= (uint32_t)srnum;

        /* VSID = VSID */
        rs |= (value & 0xfffffff) << 12;
        /* flags = flags */
        rs |= ((value >> 27) & 0xf) << 8;

        ppc_store_slb(env, rb, rs);
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
static inline void dump_syscall(CPUState *env)
{
    qemu_log_mask(CPU_LOG_INT, "syscall r0=%016" PRIx64 " r3=%016" PRIx64
                  " r4=%016" PRIx64 " r5=%016" PRIx64 " r6=%016" PRIx64
                  " nip=" TARGET_FMT_lx "\n",
                  ppc_dump_gpr(env, 0), ppc_dump_gpr(env, 3),
                  ppc_dump_gpr(env, 4), ppc_dump_gpr(env, 5),
                  ppc_dump_gpr(env, 6), env->nip);
}

/* Note that this function should be greatly optimized
 * when called with a constant excp, from ppc_hw_interrupt
 */
static inline void powerpc_excp(CPUState *env, int excp_model, int excp)
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

    qemu_log_mask(CPU_LOG_INT, "Raise exception at " TARGET_FMT_lx
                  " => %08x (%02x)\n", env->nip, excp, env->error_code);

    /* new srr1 value excluding must-be-zero bits */
    msr = env->msr & ~0x783f0000ULL;

    /* new interrupt handler msr */
    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    /* target registers */
    srr0 = SPR_SRR0;
    srr1 = SPR_SRR1;
    asrr0 = -1;
    asrr1 = -1;

    switch (excp) {
    case POWERPC_EXCP_NONE:
        /* Should never happen */
        return;
    case POWERPC_EXCP_CRITICAL:    /* Critical input                         */
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
        if (0) {
            /* XXX: find a suitable condition to enable the hypervisor mode */
            new_msr |= (target_ulong)MSR_HVB;
        }

        /* machine check exceptions don't have ME set */
        new_msr &= ~((target_ulong)1 << MSR_ME);

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
        LOG_EXCP("DSI exception: DSISR=" TARGET_FMT_lx" DAR=" TARGET_FMT_lx
                 "\n", env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
        LOG_EXCP("ISI exception: msr=" TARGET_FMT_lx ", nip=" TARGET_FMT_lx
                 "\n", msr, env->nip);
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        msr |= env->error_code;
        goto store_next;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
        if (lpes0 == 1)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
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
            if (lpes1 == 0)
                new_msr |= (target_ulong)MSR_HVB;
            msr |= 0x00100000;
            if (msr_fe0 == msr_fe1)
                goto store_next;
            msr |= 0x00010000;
            break;
        case POWERPC_EXCP_INVAL:
            LOG_EXCP("Invalid instruction at " TARGET_FMT_lx "\n", env->nip);
            if (lpes1 == 0)
                new_msr |= (target_ulong)MSR_HVB;
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            if (lpes1 == 0)
                new_msr |= (target_ulong)MSR_HVB;
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
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
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_current;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
        dump_syscall(env);
        lev = env->error_code;
        if ((lev == 1) && cpu_ppc_hypercall) {
            cpu_ppc_hypercall(env);
            return;
        }
        if (lev == 1 || (lpes0 == 0 && lpes1 == 0))
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_APU:       /* Auxiliary processor unavailable          */
        goto store_current;
    case POWERPC_EXCP_DECR:      /* Decrementer exception                    */
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_FIT:       /* Fixed-interval timer interrupt           */
        /* FIT on 4xx */
        LOG_EXCP("FIT exception\n");
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
        goto store_next;
    case POWERPC_EXCP_DTLB:      /* Data TLB error                           */
        goto store_next;
    case POWERPC_EXCP_ITLB:      /* Instruction TLB error                    */
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
        if (msr_pow) {
            /* indicate that we resumed from power save mode */
            msr |= 0x10000;
        } else {
            new_msr &= ~((target_ulong)1 << MSR_ME);
        }

        if (0) {
            /* XXX: find a suitable condition to enable the hypervisor mode */
            new_msr |= (target_ulong)MSR_HVB;
        }
        goto store_next;
    case POWERPC_EXCP_DSEG:      /* Data segment exception                   */
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_ISEG:      /* Instruction segment exception            */
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_HDECR:     /* Hypervisor decrementer exception         */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        goto store_next;
    case POWERPC_EXCP_TRACE:     /* Trace exception                          */
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_next;
    case POWERPC_EXCP_HDSI:      /* Hypervisor data storage exception        */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        goto store_next;
    case POWERPC_EXCP_HISI:      /* Hypervisor instruction storage exception */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        goto store_next;
    case POWERPC_EXCP_HDSEG:     /* Hypervisor data segment exception        */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        goto store_next;
    case POWERPC_EXCP_HISEG:     /* Hypervisor instruction segment exception */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        goto store_next;
    case POWERPC_EXCP_VPU:       /* Vector unavailable exception             */
        if (lpes1 == 0)
            new_msr |= (target_ulong)MSR_HVB;
        goto store_current;
    case POWERPC_EXCP_PIT:       /* Programmable interval timer interrupt    */
        LOG_EXCP("PIT exception\n");
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
                const char *es;
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
                qemu_log("6xx %sTLB miss: %cM " TARGET_FMT_lx " %cC "
                         TARGET_FMT_lx " H1 " TARGET_FMT_lx " H2 "
                         TARGET_FMT_lx " %08x\n", es, en, *miss, en, *cmp,
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
                const char *es;
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
                qemu_log("74xx %sTLB miss: %cM " TARGET_FMT_lx " %cC "
                         TARGET_FMT_lx " %08x\n", es, en, *miss, en, *cmp,
                         env->error_code);
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

    if (msr_ile) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }

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
            vector = (uint32_t)vector;
        } else {
            new_msr |= (target_ulong)1 << MSR_CM;
        }
    } else {
        if (!msr_isf && !(env->mmu_model & POWERPC_MMU_64)) {
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

    if ((env->mmu_model == POWERPC_MMU_BOOKE) ||
        (env->mmu_model == POWERPC_MMU_BOOKE206)) {
        /* XXX: The BookE changes address space when switching modes,
                we should probably implement that as different MMU indexes,
                but for the moment we do it the slow way and flush all.  */
        tlb_flush(env, 1);
    }
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
    qemu_log("Return from exception at " TARGET_FMT_lx " with flags "
             TARGET_FMT_lx "\n", RA, msr);
}

void cpu_reset(CPUPPCState *env)
{
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
#else
    env->excp_prefix = env->hreset_excp_prefix;
    env->nip = env->hreset_vector | env->excp_prefix;
    if (env->mmu_model != POWERPC_MMU_REAL)
        ppc_tlb_invalidate_all(env);
#endif
    env->msr = msr & env->msr_mask;
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64)
        env->msr |= (1ULL << MSR_SF);
#endif
    hreg_compute_hflags(env);
    env->reserve_addr = (target_ulong)-1ULL;
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
    cpu_exec_init(env);
    ppc_translate_init();
    env->cpu_model_str = cpu_model;
    cpu_ppc_register_internal(env, def);

    qemu_init_vcpu(env);

    return env;
}

void cpu_ppc_close (CPUPPCState *env)
{
    /* Should also remove all opcode tables... */
    qemu_free(env);
}
