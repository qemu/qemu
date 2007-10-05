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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

//#define DEBUG_MMU
//#define DEBUG_BATS
//#define DEBUG_SOFTWARE_TLB
//#define DEBUG_EXCEPTIONS
//#define FLUSH_ALL_TLBS

/*****************************************************************************/
/* PowerPC MMU emulation */

#if defined(CONFIG_USER_ONLY)
int cpu_ppc_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int is_user, int is_softmmu)
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
static inline int pte_is_valid (target_ulong pte0)
{
    return pte0 & 0x80000000 ? 1 : 0;
}

static inline void pte_invalidate (target_ulong *pte0)
{
    *pte0 &= ~0x80000000;
}

#if defined(TARGET_PPC64)
static inline int pte64_is_valid (target_ulong pte0)
{
    return pte0 & 0x0000000000000001ULL ? 1 : 0;
}

static inline void pte64_invalidate (target_ulong *pte0)
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

static inline int _pte_check (mmu_ctx_t *ctx, int is_64b,
                              target_ulong pte0, target_ulong pte1,
                              int h, int rw)
{
    target_ulong ptem, mmask;
    int access, ret, pteh, ptev;

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
        } else
#endif
        {
            ptem = pte0 & PTE_PTEM_MASK;
            mmask = PTE_CHECK_MASK;
        }
        if (ptem == ctx->ptem) {
            if (ctx->raddr != (target_ulong)-1) {
                /* all matches should have equal RPN, WIMG & PP */
                if ((ctx->raddr & mmask) != (pte1 & mmask)) {
                    if (loglevel != 0)
                        fprintf(logfile, "Bad RPN/WIMG/PP\n");
                    return -3;
                }
            }
            /* Compute access rights */
            if (ctx->key == 0) {
                access = PAGE_READ;
                if ((pte1 & 0x00000003) != 0x3)
                    access |= PAGE_WRITE;
            } else {
                switch (pte1 & 0x00000003) {
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
            /* Keep the matching PTE informations */
            ctx->raddr = pte1;
            ctx->prot = access;
            if ((rw == 0 && (access & PAGE_READ)) ||
                (rw == 1 && (access & PAGE_WRITE))) {
                /* Access granted */
#if defined (DEBUG_MMU)
                if (loglevel != 0)
                    fprintf(logfile, "PTE access granted !\n");
#endif
                ret = 0;
            } else {
                /* Access right violation */
#if defined (DEBUG_MMU)
                if (loglevel != 0)
                    fprintf(logfile, "PTE access rejected\n");
#endif
                ret = -2;
            }
        }
    }

    return ret;
}

static int pte32_check (mmu_ctx_t *ctx,
                        target_ulong pte0, target_ulong pte1, int h, int rw)
{
    return _pte_check(ctx, 0, pte0, pte1, h, rw);
}

#if defined(TARGET_PPC64)
static int pte64_check (mmu_ctx_t *ctx,
                        target_ulong pte0, target_ulong pte1, int h, int rw)
{
    return _pte_check(ctx, 1, pte0, pte1, h, rw);
}
#endif

static int pte_update_flags (mmu_ctx_t *ctx, target_ulong *pte1p,
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
static int ppc6xx_tlb_getnum (CPUState *env, target_ulong eaddr,
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

static void ppc6xx_tlb_invalidate_all (CPUState *env)
{
    ppc6xx_tlb_t *tlb;
    int nr, max;

#if defined (DEBUG_SOFTWARE_TLB) && 0
    if (loglevel != 0) {
        fprintf(logfile, "Invalidate all TLBs\n");
    }
#endif
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

static inline void __ppc6xx_tlb_invalidate_virt (CPUState *env,
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
#if defined (DEBUG_SOFTWARE_TLB)
            if (loglevel != 0) {
                fprintf(logfile, "TLB invalidate %d/%d " ADDRX "\n",
                        nr, env->nb_tlb, eaddr);
            }
#endif
            pte_invalidate(&tlb->pte0);
            tlb_flush_page(env, tlb->EPN);
        }
    }
#else
    /* XXX: PowerPC specification say this is valid as well */
    ppc6xx_tlb_invalidate_all(env);
#endif
}

static void ppc6xx_tlb_invalidate_virt (CPUState *env, target_ulong eaddr,
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
#if defined (DEBUG_SOFTWARE_TLB)
    if (loglevel != 0) {
        fprintf(logfile, "Set TLB %d/%d EPN " ADDRX " PTE0 " ADDRX
                " PTE1 " ADDRX "\n", nr, env->nb_tlb, EPN, pte0, pte1);
    }
#endif
    /* Invalidate any pending reference in Qemu for this virtual address */
    __ppc6xx_tlb_invalidate_virt(env, EPN, is_code, 1);
    tlb->pte0 = pte0;
    tlb->pte1 = pte1;
    tlb->EPN = EPN;
    /* Store last way for LRU mechanism */
    env->last_way = way;
}

static int ppc6xx_tlb_check (CPUState *env, mmu_ctx_t *ctx,
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
#if defined (DEBUG_SOFTWARE_TLB)
            if (loglevel != 0) {
                fprintf(logfile, "TLB %d/%d %s [" ADDRX " " ADDRX
                        "] <> " ADDRX "\n",
                        nr, env->nb_tlb,
                        pte_is_valid(tlb->pte0) ? "valid" : "inval",
                        tlb->EPN, tlb->EPN + TARGET_PAGE_SIZE, eaddr);
            }
#endif
            continue;
        }
#if defined (DEBUG_SOFTWARE_TLB)
        if (loglevel != 0) {
            fprintf(logfile, "TLB %d/%d %s " ADDRX " <> " ADDRX " " ADDRX
                    " %c %c\n",
                    nr, env->nb_tlb,
                    pte_is_valid(tlb->pte0) ? "valid" : "inval",
                    tlb->EPN, eaddr, tlb->pte1,
                    rw ? 'S' : 'L', access_type == ACCESS_CODE ? 'I' : 'D');
        }
#endif
        switch (pte32_check(ctx, tlb->pte0, tlb->pte1, 0, rw)) {
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
#if defined (DEBUG_SOFTWARE_TLB)
        if (loglevel != 0) {
            fprintf(logfile, "found TLB at addr 0x%08lx prot=0x%01x ret=%d\n",
                    ctx->raddr & TARGET_PAGE_MASK, ctx->prot, ret);
        }
#endif
        /* Update page flags */
        pte_update_flags(ctx, &env->tlb[best].tlb6.pte1, ret, rw);
    }

    return ret;
}

/* Perform BAT hit & translation */
static int get_bat (CPUState *env, mmu_ctx_t *ctx,
                    target_ulong virtual, int rw, int type)
{
    target_ulong *BATlt, *BATut, *BATu, *BATl;
    target_ulong base, BEPIl, BEPIu, bl;
    int i;
    int ret = -1;

#if defined (DEBUG_BATS)
    if (loglevel != 0) {
        fprintf(logfile, "%s: %cBAT v 0x" ADDRX "\n", __func__,
                type == ACCESS_CODE ? 'I' : 'D', virtual);
    }
#endif
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
#if defined (DEBUG_BATS)
    if (loglevel != 0) {
        fprintf(logfile, "%s...: %cBAT v 0x" ADDRX "\n", __func__,
                type == ACCESS_CODE ? 'I' : 'D', virtual);
    }
#endif
    base = virtual & 0xFFFC0000;
    for (i = 0; i < 4; i++) {
        BATu = &BATut[i];
        BATl = &BATlt[i];
        BEPIu = *BATu & 0xF0000000;
        BEPIl = *BATu & 0x0FFE0000;
        bl = (*BATu & 0x00001FFC) << 15;
#if defined (DEBUG_BATS)
        if (loglevel != 0) {
            fprintf(logfile, "%s: %cBAT%d v 0x" ADDRX " BATu 0x" ADDRX
                    " BATl 0x" ADDRX "\n",
                    __func__, type == ACCESS_CODE ? 'I' : 'D', i, virtual,
                    *BATu, *BATl);
        }
#endif
        if ((virtual & 0xF0000000) == BEPIu &&
            ((virtual & 0x0FFE0000) & ~bl) == BEPIl) {
            /* BAT matches */
            if ((msr_pr == 0 && (*BATu & 0x00000002)) ||
                (msr_pr == 1 && (*BATu & 0x00000001))) {
                /* Get physical address */
                ctx->raddr = (*BATl & 0xF0000000) |
                    ((virtual & 0x0FFE0000 & bl) | (*BATl & 0x0FFE0000)) |
                    (virtual & 0x0001F000);
                if (*BATl & 0x00000001)
                    ctx->prot = PAGE_READ;
                if (*BATl & 0x00000002)
                    ctx->prot = PAGE_WRITE | PAGE_READ;
#if defined (DEBUG_BATS)
                if (loglevel != 0) {
                    fprintf(logfile, "BAT %d match: r 0x" PADDRX
                            " prot=%c%c\n",
                            i, ctx->raddr, ctx->prot & PAGE_READ ? 'R' : '-',
                            ctx->prot & PAGE_WRITE ? 'W' : '-');
                }
#endif
                ret = 0;
                break;
            }
        }
    }
    if (ret < 0) {
#if defined (DEBUG_BATS)
        if (loglevel != 0) {
            fprintf(logfile, "no BAT match for 0x" ADDRX ":\n", virtual);
            for (i = 0; i < 4; i++) {
                BATu = &BATut[i];
                BATl = &BATlt[i];
                BEPIu = *BATu & 0xF0000000;
                BEPIl = *BATu & 0x0FFE0000;
                bl = (*BATu & 0x00001FFC) << 15;
                fprintf(logfile, "%s: %cBAT%d v 0x" ADDRX " BATu 0x" ADDRX
                        " BATl 0x" ADDRX " \n\t"
                        "0x" ADDRX " 0x" ADDRX " 0x" ADDRX "\n",
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
static inline int _find_pte (mmu_ctx_t *ctx, int is_64b, int h, int rw)
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
            r = pte64_check(ctx, pte0, pte1, h, rw);
#if defined (DEBUG_MMU)
            if (loglevel != 0) {
                fprintf(logfile, "Load pte from 0x" ADDRX " => 0x" ADDRX
                        " 0x" ADDRX " %d %d %d 0x" ADDRX "\n",
                        base + (i * 16), pte0, pte1,
                        (int)(pte0 & 1), h, (int)((pte0 >> 1) & 1),
                        ctx->ptem);
            }
#endif
        } else
#endif
        {
            pte0 = ldl_phys(base + (i * 8));
            pte1 =  ldl_phys(base + (i * 8) + 4);
            r = pte32_check(ctx, pte0, pte1, h, rw);
#if defined (DEBUG_MMU)
            if (loglevel != 0) {
                fprintf(logfile, "Load pte from 0x" ADDRX " => 0x" ADDRX
                        " 0x" ADDRX " %d %d %d 0x" ADDRX "\n",
                        base + (i * 8), pte0, pte1,
                        (int)(pte0 >> 31), h, (int)((pte0 >> 6) & 1),
                        ctx->ptem);
            }
#endif
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
#if defined (DEBUG_MMU)
        if (loglevel != 0) {
            fprintf(logfile, "found PTE at addr 0x" PADDRX " prot=0x%01x "
                    "ret=%d\n",
                    ctx->raddr, ctx->prot, ret);
        }
#endif
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

static int find_pte32 (mmu_ctx_t *ctx, int h, int rw)
{
    return _find_pte(ctx, 0, h, rw);
}

#if defined(TARGET_PPC64)
static int find_pte64 (mmu_ctx_t *ctx, int h, int rw)
{
    return _find_pte(ctx, 1, h, rw);
}
#endif

static inline int find_pte (CPUState *env, mmu_ctx_t *ctx, int h, int rw)
{
#if defined(TARGET_PPC64)
    if (env->mmu_model == POWERPC_MMU_64B)
        return find_pte64(ctx, h, rw);
#endif

    return find_pte32(ctx, h, rw);
}

#if defined(TARGET_PPC64)
static int slb_lookup (CPUPPCState *env, target_ulong eaddr,
                       target_ulong *vsid, target_ulong *page_mask, int *attr)
{
    target_phys_addr_t sr_base;
    target_ulong mask;
    uint64_t tmp64;
    uint32_t tmp;
    int n, ret;
    int slb_nr;

    ret = -5;
    sr_base = env->spr[SPR_ASR];
#if defined(DEBUG_SLB)
    if (loglevel != 0) {
        fprintf(logfile, "%s: eaddr " ADDRX " base " PADDRX "\n",
                __func__, eaddr, sr_base);
    }
#endif
    mask = 0x0000000000000000ULL; /* Avoid gcc warning */
    slb_nr = env->slb_nr;
    for (n = 0; n < slb_nr; n++) {
        tmp64 = ldq_phys(sr_base);
        tmp = ldl_phys(sr_base + 8);
#if defined(DEBUG_SLB)
        if (loglevel != 0) {
        fprintf(logfile, "%s: seg %d " PADDRX " %016" PRIx64 " %08" PRIx32 "\n",
                __func__, n, sr_base, tmp64, tmp);
        }
#endif
        if (tmp64 & 0x0000000008000000ULL) {
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
                ret = 0;
                break;
            }
        }
        sr_base += 12;
    }

    return ret;
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
#if defined(DEBUG_SLB)
    if (loglevel != 0) {
        fprintf(logfile, "%s: " PADDRX " %016" PRIx64 " %08" PRIx32 " => %d "
                ADDRX "\n", __func__, sr_base, tmp64, tmp, slb_nr, rt);
    }
#endif

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
#if defined(DEBUG_SLB)
    if (loglevel != 0) {
        fprintf(logfile, "%s: %d " ADDRX " => " PADDRX " %016" PRIx64 " %08"
                PRIx32 "\n", __func__, slb_nr, rs, sr_base, tmp64, tmp);
    }
#endif
    /* Write SLB entry to memory */
    stq_phys(sr_base, tmp64);
    stl_phys(sr_base + 8, tmp);
}
#endif /* defined(TARGET_PPC64) */

/* Perform segment based translation */
static inline target_phys_addr_t get_pgaddr (target_phys_addr_t sdr1,
                                             int sdr_sh,
                                             target_phys_addr_t hash,
                                             target_phys_addr_t mask)
{
    return (sdr1 & ((target_ulong)(-1ULL) << sdr_sh)) | (hash & mask);
}

static int get_segment (CPUState *env, mmu_ctx_t *ctx,
                        target_ulong eaddr, int rw, int type)
{
    target_phys_addr_t sdr, hash, mask, sdr_mask, htab_mask;
    target_ulong sr, vsid, vsid_mask, pgidx, page_mask;
#if defined(TARGET_PPC64)
    int attr;
#endif
    int ds, nx, vsid_sh, sdr_sh;
    int ret, ret2;

#if defined(TARGET_PPC64)
    if (env->mmu_model == POWERPC_MMU_64B) {
#if defined (DEBUG_MMU)
        if (loglevel != 0) {
            fprintf(logfile, "Check SLBs\n");
        }
#endif
        ret = slb_lookup(env, eaddr, &vsid, &page_mask, &attr);
        if (ret < 0)
            return ret;
        ctx->key = ((attr & 0x40) && msr_pr == 1) ||
            ((attr & 0x80) && msr_pr == 0) ? 1 : 0;
        ds = 0;
        nx = attr & 0x20 ? 1 : 0;
        vsid_mask = 0x00003FFFFFFFFF80ULL;
        vsid_sh = 7;
        sdr_sh = 18;
        sdr_mask = 0x3FF80;
    } else
#endif /* defined(TARGET_PPC64) */
    {
        sr = env->sr[eaddr >> 28];
        page_mask = 0x0FFFFFFF;
        ctx->key = (((sr & 0x20000000) && msr_pr == 1) ||
                    ((sr & 0x40000000) && msr_pr == 0)) ? 1 : 0;
        ds = sr & 0x80000000 ? 1 : 0;
        nx = sr & 0x10000000 ? 1 : 0;
        vsid = sr & 0x00FFFFFF;
        vsid_mask = 0x01FFFFC0;
        vsid_sh = 6;
        sdr_sh = 16;
        sdr_mask = 0xFFC0;
#if defined (DEBUG_MMU)
        if (loglevel != 0) {
            fprintf(logfile, "Check segment v=0x" ADDRX " %d 0x" ADDRX
                    " nip=0x" ADDRX " lr=0x" ADDRX
                    " ir=%d dr=%d pr=%d %d t=%d\n",
                    eaddr, (int)(eaddr >> 28), sr, env->nip,
                    env->lr, msr_ir, msr_dr, msr_pr, rw, type);
        }
#endif
    }
#if defined (DEBUG_MMU)
    if (loglevel != 0) {
        fprintf(logfile, "pte segment: key=%d ds %d nx %d vsid " ADDRX "\n",
                ctx->key, ds, nx, vsid);
    }
#endif
    ret = -1;
    if (!ds) {
        /* Check if instruction fetch is allowed, if needed */
        if (type != ACCESS_CODE || nx == 0) {
            /* Page address translation */
            /* Primary table address */
            sdr = env->sdr1;
            pgidx = (eaddr & page_mask) >> TARGET_PAGE_BITS;
#if defined(TARGET_PPC64)
            if (env->mmu_model == POWERPC_MMU_64B) {
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
#if defined (DEBUG_MMU)
            if (loglevel != 0) {
                fprintf(logfile, "sdr " PADDRX " sh %d hash " PADDRX " mask "
                        PADDRX " " ADDRX "\n", sdr, sdr_sh, hash, mask,
                        page_mask);
            }
#endif
            ctx->pg_addr[0] = get_pgaddr(sdr, sdr_sh, hash, mask);
            /* Secondary table address */
            hash = (~hash) & vsid_mask;
#if defined (DEBUG_MMU)
            if (loglevel != 0) {
                fprintf(logfile, "sdr " PADDRX " sh %d hash " PADDRX " mask "
                        PADDRX "\n", sdr, sdr_sh, hash, mask);
            }
#endif
            ctx->pg_addr[1] = get_pgaddr(sdr, sdr_sh, hash, mask);
#if defined(TARGET_PPC64)
            if (env->mmu_model == POWERPC_MMU_64B) {
                /* Only 5 bits of the page index are used in the AVPN */
                ctx->ptem = (vsid << 12) | ((pgidx >> 4) & 0x0F80);
            } else
#endif
            {
                ctx->ptem = (vsid << 7) | (pgidx >> 10);
            }
            /* Initialize real address with an invalid value */
            ctx->raddr = (target_ulong)-1;
            if (unlikely(env->mmu_model == POWERPC_MMU_SOFT_6xx ||
                         env->mmu_model == POWERPC_MMU_SOFT_74xx)) {
                /* Software TLB search */
                ret = ppc6xx_tlb_check(env, ctx, eaddr, rw, type);
            } else {
#if defined (DEBUG_MMU)
                if (loglevel != 0) {
                    fprintf(logfile, "0 sdr1=0x" PADDRX " vsid=0x%06x "
                            "api=0x%04x hash=0x%07x pg_addr=0x" PADDRX "\n",
                            sdr, (uint32_t)vsid, (uint32_t)pgidx,
                            (uint32_t)hash, ctx->pg_addr[0]);
                }
#endif
                /* Primary table lookup */
                ret = find_pte(env, ctx, 0, rw);
                if (ret < 0) {
                    /* Secondary table lookup */
#if defined (DEBUG_MMU)
                    if (eaddr != 0xEFFFFFFF && loglevel != 0) {
                        fprintf(logfile,
                                "1 sdr1=0x" PADDRX " vsid=0x%06x api=0x%04x "
                                "hash=0x%05x pg_addr=0x" PADDRX "\n",
                                sdr, (uint32_t)vsid, (uint32_t)pgidx,
                                (uint32_t)hash, ctx->pg_addr[1]);
                    }
#endif
                    ret2 = find_pte(env, ctx, 1, rw);
                    if (ret2 != -1)
                        ret = ret2;
                }
            }
#if defined (DEBUG_MMU)
                    if (loglevel != 0) {
                        target_phys_addr_t curaddr;
                        uint32_t a0, a1, a2, a3;
                        fprintf(logfile,
                                "Page table: " PADDRX " len " PADDRX "\n",
                                sdr, mask + 0x80);
                        for (curaddr = sdr; curaddr < (sdr + mask + 0x80);
                             curaddr += 16) {
                            a0 = ldl_phys(curaddr);
                            a1 = ldl_phys(curaddr + 4);
                            a2 = ldl_phys(curaddr + 8);
                            a3 = ldl_phys(curaddr + 12);
                            if (a0 != 0 || a1 != 0 || a2 != 0 || a3 != 0) {
                                fprintf(logfile,
                                        PADDRX ": %08x %08x %08x %08x\n",
                                        curaddr, a0, a1, a2, a3);
                            }
                        }
                    }
#endif
        } else {
#if defined (DEBUG_MMU)
            if (loglevel != 0)
                fprintf(logfile, "No access allowed\n");
#endif
            ret = -3;
        }
    } else {
#if defined (DEBUG_MMU)
        if (loglevel != 0)
            fprintf(logfile, "direct store...\n");
#endif
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
            if (logfile) {
                fprintf(logfile, "ERROR: instruction should not need "
                        "address translation\n");
            }
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
static int ppcemb_tlb_check (CPUState *env, ppcemb_tlb_t *tlb,
                             target_phys_addr_t *raddrp,
                             target_ulong address,
                             uint32_t pid, int ext, int i)
{
    target_ulong mask;

    /* Check valid flag */
    if (!(tlb->prot & PAGE_VALID)) {
        if (loglevel != 0)
            fprintf(logfile, "%s: TLB %d not valid\n", __func__, i);
        return -1;
    }
    mask = ~(tlb->size - 1);
#if defined (DEBUG_SOFTWARE_TLB)
    if (loglevel != 0) {
        fprintf(logfile, "%s: TLB %d address " ADDRX " PID %d <=> "
                ADDRX " " ADDRX " %d\n",
                __func__, i, address, pid, tlb->EPN, mask, (int)tlb->PID);
    }
#endif
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
static void ppc4xx_tlb_invalidate_all (CPUState *env)
{
    ppcemb_tlb_t *tlb;
    int i;

    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        tlb->prot &= ~PAGE_VALID;
    }
    tlb_flush(env, 1);
}

static void ppc4xx_tlb_invalidate_virt (CPUState *env, target_ulong eaddr,
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

int mmu40x_get_physical_address (CPUState *env, mmu_ctx_t *ctx,
                                 target_ulong address, int rw, int access_type)
{
    ppcemb_tlb_t *tlb;
    target_phys_addr_t raddr;
    int i, ret, zsel, zpr;

    ret = -1;
    raddr = -1;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        if (ppcemb_tlb_check(env, tlb, &raddr, address,
                             env->spr[SPR_40x_PID], 0, i) < 0)
            continue;
        zsel = (tlb->attr >> 4) & 0xF;
        zpr = (env->spr[SPR_40x_ZPR] >> (28 - (2 * zsel))) & 0x3;
#if defined (DEBUG_SOFTWARE_TLB)
        if (loglevel != 0) {
            fprintf(logfile, "%s: TLB %d zsel %d zpr %d rw %d attr %08x\n",
                    __func__, i, zsel, zpr, rw, tlb->attr);
        }
#endif
        if (access_type == ACCESS_CODE) {
            /* Check execute enable bit */
            switch (zpr) {
            case 0x2:
                if (msr_pr)
                    goto check_exec_perm;
                goto exec_granted;
            case 0x0:
                if (msr_pr) {
                    ctx->prot = 0;
                    ret = -3;
                    break;
                }
                /* No break here */
            case 0x1:
            check_exec_perm:
                /* Check from TLB entry */
                if (!(tlb->prot & PAGE_EXEC)) {
                    ret = -3;
                } else {
                    if (tlb->prot & PAGE_WRITE) {
                        ctx->prot = PAGE_READ | PAGE_WRITE;
                    } else {
                        ctx->prot = PAGE_READ;
                    }
                    ret = 0;
                }
                break;
            case 0x3:
            exec_granted:
                /* All accesses granted */
                ctx->prot = PAGE_READ | PAGE_WRITE;
                ret = 0;
                break;
            }
        } else {
            switch (zpr) {
            case 0x2:
                if (msr_pr)
                    goto check_rw_perm;
                goto rw_granted;
            case 0x0:
                if (msr_pr) {
                    ctx->prot = 0;
                    ret = -2;
                    break;
                }
                /* No break here */
            case 0x1:
            check_rw_perm:
                /* Check from TLB entry */
                /* Check write protection bit */
                if (tlb->prot & PAGE_WRITE) {
                    ctx->prot = PAGE_READ | PAGE_WRITE;
                    ret = 0;
                } else {
                    ctx->prot = PAGE_READ;
                    if (rw)
                        ret = -2;
                    else
                        ret = 0;
                }
                break;
            case 0x3:
            rw_granted:
                /* All accesses granted */
                ctx->prot = PAGE_READ | PAGE_WRITE;
                ret = 0;
                break;
            }
        }
        if (ret >= 0) {
            ctx->raddr = raddr;
#if defined (DEBUG_SOFTWARE_TLB)
            if (loglevel != 0) {
                fprintf(logfile, "%s: access granted " ADDRX " => " REGX
                        " %d %d\n", __func__, address, ctx->raddr, ctx->prot,
                        ret);
            }
#endif
            return 0;
        }
    }
#if defined (DEBUG_SOFTWARE_TLB)
    if (loglevel != 0) {
        fprintf(logfile, "%s: access refused " ADDRX " => " REGX
                " %d %d\n", __func__, address, raddr, ctx->prot,
                ret);
    }
#endif

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

int mmubooke_get_physical_address (CPUState *env, mmu_ctx_t *ctx,
                                   target_ulong address, int rw,
                                   int access_type)
{
    ppcemb_tlb_t *tlb;
    target_phys_addr_t raddr;
    int i, prot, ret;

    ret = -1;
    raddr = -1;
    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        if (ppcemb_tlb_check(env, tlb, &raddr, address,
                             env->spr[SPR_BOOKE_PID], 1, i) < 0)
            continue;
        if (msr_pr)
            prot = tlb->prot & 0xF;
        else
            prot = (tlb->prot >> 4) & 0xF;
        /* Check the address space */
        if (access_type == ACCESS_CODE) {
            if (msr_is != (tlb->attr & 1))
                continue;
            ctx->prot = prot;
            if (prot & PAGE_EXEC) {
                ret = 0;
                break;
            }
            ret = -3;
        } else {
            if (msr_ds != (tlb->attr & 1))
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

static int check_physical (CPUState *env, mmu_ctx_t *ctx,
                           target_ulong eaddr, int rw)
{
    int in_plb, ret;

    ctx->raddr = eaddr;
    ctx->prot = PAGE_READ;
    ret = 0;
    switch (env->mmu_model) {
    case POWERPC_MMU_32B:
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
    case POWERPC_MMU_601:
    case POWERPC_MMU_SOFT_4xx:
    case POWERPC_MMU_REAL_4xx:
    case POWERPC_MMU_BOOKE:
        ctx->prot |= PAGE_WRITE;
        break;
#if defined(TARGET_PPC64)
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
                          int rw, int access_type, int check_BATs)
{
    int ret;
#if 0
    if (loglevel != 0) {
        fprintf(logfile, "%s\n", __func__);
    }
#endif
    if ((access_type == ACCESS_CODE && msr_ir == 0) ||
        (access_type != ACCESS_CODE && msr_dr == 0)) {
        /* No address translation */
        ret = check_physical(env, ctx, eaddr, rw);
    } else {
        ret = -1;
        switch (env->mmu_model) {
        case POWERPC_MMU_32B:
        case POWERPC_MMU_SOFT_6xx:
        case POWERPC_MMU_SOFT_74xx:
            /* Try to find a BAT */
            if (check_BATs)
                ret = get_bat(env, ctx, eaddr, rw, access_type);
            /* No break here */
#if defined(TARGET_PPC64)
        case POWERPC_MMU_64B:
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
        case POWERPC_MMU_601:
            /* XXX: TODO */
            cpu_abort(env, "601 MMU model not implemented\n");
            return -1;
        case POWERPC_MMU_BOOKE:
            ret = mmubooke_get_physical_address(env, ctx, eaddr,
                                                rw, access_type);
            break;
        case POWERPC_MMU_BOOKE_FSL:
            /* XXX: TODO */
            cpu_abort(env, "BookE FSL MMU model not implemented\n");
            return -1;
        case POWERPC_MMU_REAL_4xx:
            cpu_abort(env, "PowerPC 401 does not do any translation\n");
            return -1;
        default:
            cpu_abort(env, "Unknown or invalid MMU model\n");
            return -1;
        }
    }
#if 0
    if (loglevel != 0) {
        fprintf(logfile, "%s address " ADDRX " => %d " PADDRX "\n",
                __func__, eaddr, ret, ctx->raddr);
    }
#endif

    return ret;
}

target_phys_addr_t cpu_get_phys_page_debug (CPUState *env, target_ulong addr)
{
    mmu_ctx_t ctx;

    if (unlikely(get_physical_address(env, &ctx, addr, 0, ACCESS_INT, 1) != 0))
        return -1;

    return ctx.raddr & TARGET_PAGE_MASK;
}

/* Perform address translation */
int cpu_ppc_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int is_user, int is_softmmu)
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
        /* XXX: put correct access by using cpu_restore_state()
           correctly */
        access_type = ACCESS_INT;
        //        access_type = env->access_type;
    }
    ret = get_physical_address(env, &ctx, address, rw, access_type, 1);
    if (ret == 0) {
        ret = tlb_set_page(env, address & TARGET_PAGE_MASK,
                           ctx.raddr & TARGET_PAGE_MASK, ctx.prot,
                           is_user, is_softmmu);
    } else if (ret < 0) {
#if defined (DEBUG_MMU)
        if (loglevel != 0)
            cpu_dump_state(env, logfile, fprintf, 0);
#endif
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
#if defined(TARGET_PPC64)
                case POWERPC_MMU_64B:
#endif
                    env->exception_index = POWERPC_EXCP_ISI;
                    env->error_code = 0x40000000;
                    break;
                case POWERPC_MMU_601:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
                case POWERPC_MMU_BOOKE:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
                case POWERPC_MMU_BOOKE_FSL:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
                case POWERPC_MMU_REAL_4xx:
                    cpu_abort(env, "PowerPC 401 should never raise any MMU "
                              "exceptions\n");
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
                env->exception_index = POWERPC_EXCP_ISEG;
                env->error_code = 0;
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
#if defined(TARGET_PPC64)
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
                case POWERPC_MMU_601:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
                case POWERPC_MMU_BOOKE:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
                case POWERPC_MMU_BOOKE_FSL:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
                case POWERPC_MMU_REAL_4xx:
                    cpu_abort(env, "PowerPC 401 should never raise any MMU "
                              "exceptions\n");
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
                env->exception_index = POWERPC_EXCP_DSEG;
                env->error_code = 0;
                env->spr[SPR_DAR] = address;
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
static inline void do_invalidate_BAT (CPUPPCState *env,
                                      target_ulong BATu, target_ulong mask)
{
    target_ulong base, end, page;

    base = BATu & ~0x0001FFFF;
    end = base + mask + 0x00020000;
#if defined (DEBUG_BATS)
    if (loglevel != 0) {
        fprintf(logfile, "Flush BAT from " ADDRX " to " ADDRX " (" ADDRX ")\n",
                base, end, mask);
    }
#endif
    for (page = base; page != end; page += TARGET_PAGE_SIZE)
        tlb_flush_page(env, page);
#if defined (DEBUG_BATS)
    if (loglevel != 0)
        fprintf(logfile, "Flush done\n");
#endif
}
#endif

static inline void dump_store_bat (CPUPPCState *env, char ID, int ul, int nr,
                                   target_ulong value)
{
#if defined (DEBUG_BATS)
    if (loglevel != 0) {
        fprintf(logfile, "Set %cBAT%d%c to 0x" ADDRX " (0x" ADDRX ")\n",
                ID, nr, ul == 0 ? 'u' : 'l', value, env->nip);
    }
#endif
}

target_ulong do_load_ibatu (CPUPPCState *env, int nr)
{
    return env->IBAT[0][nr];
}

target_ulong do_load_ibatl (CPUPPCState *env, int nr)
{
    return env->IBAT[1][nr];
}

void do_store_ibatu (CPUPPCState *env, int nr, target_ulong value)
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

void do_store_ibatl (CPUPPCState *env, int nr, target_ulong value)
{
    dump_store_bat(env, 'I', 1, nr, value);
    env->IBAT[1][nr] = value;
}

target_ulong do_load_dbatu (CPUPPCState *env, int nr)
{
    return env->DBAT[0][nr];
}

target_ulong do_load_dbatl (CPUPPCState *env, int nr)
{
    return env->DBAT[1][nr];
}

void do_store_dbatu (CPUPPCState *env, int nr, target_ulong value)
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

void do_store_dbatl (CPUPPCState *env, int nr, target_ulong value)
{
    dump_store_bat(env, 'D', 1, nr, value);
    env->DBAT[1][nr] = value;
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
    case POWERPC_MMU_REAL_4xx:
        cpu_abort(env, "No TLB for PowerPC 4xx in real mode\n");
        break;
    case POWERPC_MMU_BOOKE:
        /* XXX: TODO */
        cpu_abort(env, "MMU model not implemented\n");
        break;
    case POWERPC_MMU_BOOKE_FSL:
        /* XXX: TODO */
        cpu_abort(env, "MMU model not implemented\n");
        break;
    case POWERPC_MMU_601:
        /* XXX: TODO */
        cpu_abort(env, "MMU model not implemented\n");
        break;
    case POWERPC_MMU_32B:
#if defined(TARGET_PPC64)
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
    case POWERPC_MMU_REAL_4xx:
        cpu_abort(env, "No TLB for PowerPC 4xx in real mode\n");
        break;
    case POWERPC_MMU_BOOKE:
        /* XXX: TODO */
        cpu_abort(env, "MMU model not implemented\n");
        break;
    case POWERPC_MMU_BOOKE_FSL:
        /* XXX: TODO */
        cpu_abort(env, "MMU model not implemented\n");
        break;
    case POWERPC_MMU_601:
        /* XXX: TODO */
        cpu_abort(env, "MMU model not implemented\n");
        break;
    case POWERPC_MMU_32B:
        /* tlbie invalidate TLBs for all segments */
        addr &= ~((target_ulong)-1 << 28);
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

#if defined(TARGET_PPC64)
void ppc_slb_invalidate_all (CPUPPCState *env)
{
    /* XXX: TODO */
    tlb_flush(env, 1);
}

void ppc_slb_invalidate_one (CPUPPCState *env, uint64_t T0)
{
    /* XXX: TODO */
    tlb_flush(env, 1);
}
#endif


/*****************************************************************************/
/* Special registers manipulation */
#if defined(TARGET_PPC64)
target_ulong ppc_load_asr (CPUPPCState *env)
{
    return env->asr;
}

void ppc_store_asr (CPUPPCState *env, target_ulong value)
{
    if (env->asr != value) {
        env->asr = value;
        tlb_flush(env, 1);
    }
}
#endif

target_ulong do_load_sdr1 (CPUPPCState *env)
{
    return env->sdr1;
}

void do_store_sdr1 (CPUPPCState *env, target_ulong value)
{
#if defined (DEBUG_MMU)
    if (loglevel != 0) {
        fprintf(logfile, "%s: 0x" ADDRX "\n", __func__, value);
    }
#endif
    if (env->sdr1 != value) {
        /* XXX: for PowerPC 64, should check that the HTABSIZE value
         *      is <= 28
         */
        env->sdr1 = value;
        tlb_flush(env, 1);
    }
}

#if 0 // Unused
target_ulong do_load_sr (CPUPPCState *env, int srnum)
{
    return env->sr[srnum];
}
#endif

void do_store_sr (CPUPPCState *env, int srnum, target_ulong value)
{
#if defined (DEBUG_MMU)
    if (loglevel != 0) {
        fprintf(logfile, "%s: reg=%d 0x" ADDRX " " ADDRX "\n",
                __func__, srnum, value, env->sr[srnum]);
    }
#endif
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

target_ulong ppc_load_xer (CPUPPCState *env)
{
    return (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC) |
        (xer_cmp << XER_CMP);
}

void ppc_store_xer (CPUPPCState *env, target_ulong value)
{
    xer_so = (value >> XER_SO) & 0x01;
    xer_ov = (value >> XER_OV) & 0x01;
    xer_ca = (value >> XER_CA) & 0x01;
    xer_cmp = (value >> XER_CMP) & 0xFF;
    xer_bc = (value >> XER_BC) & 0x7F;
}

/* Swap temporary saved registers with GPRs */
static inline void swap_gpr_tgpr (CPUPPCState *env)
{
    ppc_gpr_t tmp;

    tmp = env->gpr[0];
    env->gpr[0] = env->tgpr[0];
    env->tgpr[0] = tmp;
    tmp = env->gpr[1];
    env->gpr[1] = env->tgpr[1];
    env->tgpr[1] = tmp;
    tmp = env->gpr[2];
    env->gpr[2] = env->tgpr[2];
    env->tgpr[2] = tmp;
    tmp = env->gpr[3];
    env->gpr[3] = env->tgpr[3];
    env->tgpr[3] = tmp;
}

/* GDBstub can read and write MSR... */
target_ulong do_load_msr (CPUPPCState *env)
{
    return
#if defined (TARGET_PPC64)
        ((target_ulong)msr_sf   << MSR_SF)   |
        ((target_ulong)msr_isf  << MSR_ISF)  |
        ((target_ulong)msr_hv   << MSR_HV)   |
#endif
        ((target_ulong)msr_ucle << MSR_UCLE) |
        ((target_ulong)msr_vr   << MSR_VR)   | /* VR / SPE */
        ((target_ulong)msr_ap   << MSR_AP)   |
        ((target_ulong)msr_sa   << MSR_SA)   |
        ((target_ulong)msr_key  << MSR_KEY)  |
        ((target_ulong)msr_pow  << MSR_POW)  | /* POW / WE */
        ((target_ulong)msr_tlb  << MSR_TLB)  | /* TLB / TGPE / CE */
        ((target_ulong)msr_ile  << MSR_ILE)  |
        ((target_ulong)msr_ee   << MSR_EE)   |
        ((target_ulong)msr_pr   << MSR_PR)   |
        ((target_ulong)msr_fp   << MSR_FP)   |
        ((target_ulong)msr_me   << MSR_ME)   |
        ((target_ulong)msr_fe0  << MSR_FE0)  |
        ((target_ulong)msr_se   << MSR_SE)   | /* SE / DWE / UBLE */
        ((target_ulong)msr_be   << MSR_BE)   | /* BE / DE */
        ((target_ulong)msr_fe1  << MSR_FE1)  |
        ((target_ulong)msr_al   << MSR_AL)   |
        ((target_ulong)msr_ip   << MSR_IP)   |
        ((target_ulong)msr_ir   << MSR_IR)   | /* IR / IS */
        ((target_ulong)msr_dr   << MSR_DR)   | /* DR / DS */
        ((target_ulong)msr_pe   << MSR_PE)   | /* PE / EP */
        ((target_ulong)msr_px   << MSR_PX)   | /* PX / PMM */
        ((target_ulong)msr_ri   << MSR_RI)   |
        ((target_ulong)msr_le   << MSR_LE);
}

int do_store_msr (CPUPPCState *env, target_ulong value)
{
    int enter_pm;

    value &= env->msr_mask;
    if (((value >> MSR_IR) & 1) != msr_ir ||
        ((value >> MSR_DR) & 1) != msr_dr) {
        /* Flush all tlb when changing translation mode */
        tlb_flush(env, 1);
        env->interrupt_request |= CPU_INTERRUPT_EXITTB;
    }
#if 0
    if (loglevel != 0) {
        fprintf(logfile, "%s: T0 %08lx\n", __func__, value);
    }
#endif
    switch (env->excp_model) {
    case POWERPC_EXCP_602:
    case POWERPC_EXCP_603:
    case POWERPC_EXCP_603E:
    case POWERPC_EXCP_G2:
        if (((value >> MSR_TGPR) & 1) != msr_tgpr) {
            /* Swap temporary saved registers with GPRs */
            swap_gpr_tgpr(env);
        }
        break;
    default:
        break;
    }
#if defined (TARGET_PPC64)
    msr_sf   = (value >> MSR_SF)   & 1;
    msr_isf  = (value >> MSR_ISF)  & 1;
    msr_hv   = (value >> MSR_HV)   & 1;
#endif
    msr_ucle = (value >> MSR_UCLE) & 1;
    msr_vr   = (value >> MSR_VR)   & 1; /* VR / SPE */
    msr_ap   = (value >> MSR_AP)   & 1;
    msr_sa   = (value >> MSR_SA)   & 1;
    msr_key  = (value >> MSR_KEY)  & 1;
    msr_pow  = (value >> MSR_POW)  & 1; /* POW / WE */
    msr_tlb  = (value >> MSR_TLB)  & 1; /* TLB / TGPR / CE */
    msr_ile  = (value >> MSR_ILE)  & 1;
    msr_ee   = (value >> MSR_EE)   & 1;
    msr_pr   = (value >> MSR_PR)   & 1;
    msr_fp   = (value >> MSR_FP)   & 1;
    msr_me   = (value >> MSR_ME)   & 1;
    msr_fe0  = (value >> MSR_FE0)  & 1;
    msr_se   = (value >> MSR_SE)   & 1; /* SE / DWE / UBLE */
    msr_be   = (value >> MSR_BE)   & 1; /* BE / DE */
    msr_fe1  = (value >> MSR_FE1)  & 1;
    msr_al   = (value >> MSR_AL)   & 1;
    msr_ip   = (value >> MSR_IP)   & 1;
    msr_ir   = (value >> MSR_IR)   & 1; /* IR / IS */
    msr_dr   = (value >> MSR_DR)   & 1; /* DR / DS */
    msr_pe   = (value >> MSR_PE)   & 1; /* PE / EP */
    msr_px   = (value >> MSR_PX)   & 1; /* PX / PMM */
    msr_ri   = (value >> MSR_RI)   & 1;
    msr_le   = (value >> MSR_LE)   & 1;
    do_compute_hflags(env);

    enter_pm = 0;
    switch (env->excp_model) {
    case POWERPC_EXCP_603:
    case POWERPC_EXCP_603E:
    case POWERPC_EXCP_G2:
        /* Don't handle SLEEP mode: we should disable all clocks...
         * No dynamic power-management.
         */
        if (msr_pow == 1 && (env->spr[SPR_HID0] & 0x00C00000) != 0)
            enter_pm = 1;
        break;
    case POWERPC_EXCP_604:
        if (msr_pow == 1)
            enter_pm = 1;
        break;
    case POWERPC_EXCP_7x0:
        if (msr_pow == 1 && (env->spr[SPR_HID0] & 0x00E00000) != 0)
            enter_pm = 1;
        break;
    default:
        break;
    }

    return enter_pm;
}

#if defined(TARGET_PPC64)
int ppc_store_msr_32 (CPUPPCState *env, uint32_t value)
{
    return do_store_msr(env, (do_load_msr(env) & ~0xFFFFFFFFULL) |
                        (value & 0xFFFFFFFF));
}
#endif

void do_compute_hflags (CPUPPCState *env)
{
    /* Compute current hflags */
    env->hflags = (msr_vr << MSR_VR) |
        (msr_ap << MSR_AP) | (msr_sa << MSR_SA) | (msr_pr << MSR_PR) |
        (msr_fp << MSR_FP) | (msr_fe0 << MSR_FE0) | (msr_se << MSR_SE) |
        (msr_be << MSR_BE) | (msr_fe1 << MSR_FE1) | (msr_le << MSR_LE);
#if defined (TARGET_PPC64)
    env->hflags |= msr_cm << MSR_CM;
    env->hflags |= (uint64_t)msr_sf << MSR_SF;
    env->hflags |= (uint64_t)msr_hv << MSR_HV;
#endif
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
static void dump_syscall (CPUState *env)
{
    fprintf(logfile, "syscall r0=0x" REGX " r3=0x" REGX " r4=0x" REGX
            " r5=0x" REGX " r6=0x" REGX " nip=0x" ADDRX "\n",
            env->gpr[0], env->gpr[3], env->gpr[4],
            env->gpr[5], env->gpr[6], env->nip);
}

/* Note that this function should be greatly optimized
 * when called with a constant excp, from ppc_hw_interrupt
 */
static always_inline void powerpc_excp (CPUState *env,
                                        int excp_model, int excp)
{
    target_ulong msr, vector;
    int srr0, srr1, asrr0, asrr1;

    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "Raise exception at 0x" ADDRX " => 0x%08x (%02x)\n",
                env->nip, excp, env->error_code);
    }
    msr = do_load_msr(env);
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
        msr_ri = 0; /* XXX: check this */
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
            /* Machine check exception is not enabled */
            /* XXX: we may just stop the processor here, to allow debugging */
            excp = POWERPC_EXCP_RESET;
            goto excp_reset;
        }
        msr_ri = 0;
        msr_me = 0;
#if defined(TARGET_PPC64H)
        msr_hv = 1;
#endif
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
#if defined (DEBUG_EXCEPTIONS)
        if (loglevel != 0) {
            fprintf(logfile, "DSI exception: DSISR=0x" ADDRX" DAR=0x" ADDRX
                    "\n", env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        }
#endif
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        goto store_next;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
#if defined (DEBUG_EXCEPTIONS)
        if (loglevel != 0) {
            fprintf(logfile, "ISI exception: msr=0x" ADDRX ", nip=0x" ADDRX
                    "\n", msr, env->nip);
        }
#endif
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        msr |= env->error_code;
        goto store_next;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes0 == 1)
            msr_hv = 1;
#endif
        goto store_next;
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        /* XXX: this is false */
        /* Get rS/rD and rA from faulting opcode */
        env->spr[SPR_DSISR] |= (ldl_code((env->nip - 4)) & 0x03FF0000) >> 16;
        goto store_current;
    case POWERPC_EXCP_PROGRAM:   /* Program exception                        */
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if ((msr_fe0 == 0 && msr_fe1 == 0) || msr_fp == 0) {
#if defined (DEBUG_EXCEPTIONS)
                if (loglevel != 0) {
                    fprintf(logfile, "Ignore floating point exception\n");
                }
#endif
                return;
            }
            msr_ri = 0;
#if defined(TARGET_PPC64H)
            if (lpes1 == 0)
                msr_hv = 1;
#endif
            msr |= 0x00100000;
            /* Set FX */
            env->fpscr[7] |= 0x8;
            /* Finally, update FEX */
            if ((((env->fpscr[7] & 0x3) << 3) | (env->fpscr[6] >> 1)) &
                ((env->fpscr[1] << 1) | (env->fpscr[0] >> 3)))
                env->fpscr[7] |= 0x4;
            if (msr_fe0 != msr_fe1) {
                msr |= 0x00010000;
                goto store_current;
            }
            break;
        case POWERPC_EXCP_INVAL:
#if defined (DEBUG_EXCEPTIONS)
            if (loglevel != 0) {
                fprintf(logfile, "Invalid instruction at 0x" ADDRX "\n",
                        env->nip);
            }
#endif
            msr_ri = 0;
#if defined(TARGET_PPC64H)
            if (lpes1 == 0)
                msr_hv = 1;
#endif
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr_ri = 0;
#if defined(TARGET_PPC64H)
            if (lpes1 == 0)
                msr_hv = 1;
#endif
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr_ri = 0;
#if defined(TARGET_PPC64H)
            if (lpes1 == 0)
                msr_hv = 1;
#endif
            msr |= 0x00020000;
            break;
        default:
            /* Should never occur */
            cpu_abort(env, "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        goto store_next;
    case POWERPC_EXCP_FPU:       /* Floating-point unavailable exception     */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        goto store_current;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
        /* NOTE: this is a temporary hack to support graphics OSI
           calls from the MOL driver */
        /* XXX: To be removed */
        if (env->gpr[3] == 0x113724fa && env->gpr[4] == 0x77810f9b &&
            env->osi_call) {
            if (env->osi_call(env) != 0)
                return;
        }
        if (loglevel & CPU_LOG_INT) {
            dump_syscall(env);
        }
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lev == 1 || (lpes0 == 0 && lpes1 == 0))
            msr_hv = 1;
#endif
        goto store_next;
    case POWERPC_EXCP_APU:       /* Auxiliary processor unavailable          */
        msr_ri = 0;
        goto store_current;
    case POWERPC_EXCP_DECR:      /* Decrementer exception                    */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        goto store_next;
    case POWERPC_EXCP_FIT:       /* Fixed-interval timer interrupt           */
        /* FIT on 4xx */
#if defined (DEBUG_EXCEPTIONS)
        if (loglevel != 0)
            fprintf(logfile, "FIT exception\n");
#endif
        msr_ri = 0; /* XXX: check this */
        goto store_next;
    case POWERPC_EXCP_WDT:       /* Watchdog timer interrupt                 */
#if defined (DEBUG_EXCEPTIONS)
        if (loglevel != 0)
            fprintf(logfile, "WDT exception\n");
#endif
        switch (excp_model) {
        case POWERPC_EXCP_BOOKE:
            srr0 = SPR_BOOKE_CSRR0;
            srr1 = SPR_BOOKE_CSRR1;
            break;
        default:
            break;
        }
        msr_ri = 0; /* XXX: check this */
        goto store_next;
    case POWERPC_EXCP_DTLB:      /* Data TLB error                           */
        msr_ri = 0; /* XXX: check this */
        goto store_next;
    case POWERPC_EXCP_ITLB:      /* Instruction TLB error                    */
        msr_ri = 0; /* XXX: check this */
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
#if defined(TARGET_PPCEMB)
    case POWERPC_EXCP_SPEU:      /* SPE/embedded floating-point unavailable  */
        msr_ri = 0; /* XXX: check this */
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
        msr_ri = 0;
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
#endif /* defined(TARGET_PPCEMB) */
    case POWERPC_EXCP_RESET:     /* System reset exception                   */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        msr_hv = 1;
#endif
    excp_reset:
        goto store_next;
#if defined(TARGET_PPC64)
    case POWERPC_EXCP_DSEG:      /* Data segment exception                   */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        goto store_next;
    case POWERPC_EXCP_ISEG:      /* Instruction segment exception            */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        goto store_next;
#endif /* defined(TARGET_PPC64) */
#if defined(TARGET_PPC64H)
    case POWERPC_EXCP_HDECR:     /* Hypervisor decrementer exception         */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSSR1;
        msr_hv = 1;
        goto store_next;
#endif
    case POWERPC_EXCP_TRACE:     /* Trace exception                          */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        goto store_next;
#if defined(TARGET_PPC64H)
    case POWERPC_EXCP_HDSI:      /* Hypervisor data storage exception        */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSSR1;
        msr_hv = 1;
        goto store_next;
    case POWERPC_EXCP_HISI:      /* Hypervisor instruction storage exception */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSSR1;
        msr_hv = 1;
        /* XXX: TODO */
        cpu_abort(env, "Hypervisor instruction storage exception "
                  "is not implemented yet !\n");
        goto store_next;
    case POWERPC_EXCP_HDSEG:     /* Hypervisor data segment exception        */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSSR1;
        msr_hv = 1;
        goto store_next;
    case POWERPC_EXCP_HISEG:     /* Hypervisor instruction segment exception */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSSR1;
        msr_hv = 1;
        goto store_next;
#endif /* defined(TARGET_PPC64H) */
    case POWERPC_EXCP_VPU:       /* Vector unavailable exception             */
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        goto store_current;
    case POWERPC_EXCP_PIT:       /* Programmable interval timer interrupt    */
#if defined (DEBUG_EXCEPTIONS)
        if (loglevel != 0)
            fprintf(logfile, "PIT exception\n");
#endif
        msr_ri = 0; /* XXX: check this */
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
        msr_ri = 0; /* XXX: check this */
#if defined(TARGET_PPC64H) /* XXX: check this */
        if (lpes1 == 0)
            msr_hv = 1;
#endif
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
        msr_ri = 0; /* XXX: check this */
#if defined(TARGET_PPC64H) /* XXX: check this */
        if (lpes1 == 0)
            msr_hv = 1;
#endif
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
        msr_ri = 0; /* XXX: check this */
#if defined(TARGET_PPC64H) /* XXX: check this */
        if (lpes1 == 0)
            msr_hv = 1;
#endif
        switch (excp_model) {
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
        tlb_miss_tgpr:
            /* Swap temporary saved registers with GPRs */
            swap_gpr_tgpr(env);
            msr_tgpr = 1;
            goto tlb_miss;
        case POWERPC_EXCP_7x5:
        tlb_miss:
#if defined (DEBUG_SOFTWARE_TLB)
            if (loglevel != 0) {
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
                fprintf(logfile, "6xx %sTLB miss: %cM " ADDRX " %cC " ADDRX
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
            if (loglevel != 0) {
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
                    miss = &env->spr[SPR_TLBMISS];
                    cmp = &env->spr[SPR_PTEHI];
                }
                fprintf(logfile, "74xx %sTLB miss: %cM " ADDRX " %cC " ADDRX
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
        msr_ri = 0;
#if defined(TARGET_PPC64H)
        if (lpes1 == 0)
            msr_hv = 1;
#endif
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
    if (msr_ir || msr_dr)
        tlb_flush(env, 1);
    /* reload MSR with correct bits */
    msr_ee = 0;
    msr_pr = 0;
    msr_fp = 0;
    msr_fe0 = 0;
    msr_se = 0;
    msr_be = 0;
    msr_fe1 = 0;
    msr_ir = 0;
    msr_dr = 0;
#if 0 /* Fix this: not on all targets */
    msr_pmm = 0;
#endif
    msr_le = msr_ile;
    do_compute_hflags(env);
    /* Jump to handler */
    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1) {
        cpu_abort(env, "Raised an exception without defined vector %d\n",
                  excp);
    }
    vector |= env->excp_prefix;
#if defined(TARGET_PPC64)
    if (excp_model == POWERPC_EXCP_BOOKE) {
        msr_cm = msr_icm;
        if (!msr_cm)
            vector = (uint32_t)vector;
    } else {
        msr_sf = msr_isf;
        if (!msr_sf)
            vector = (uint32_t)vector;
    }
#endif
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
#if 1
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "%s: %p pending %08x req %08x me %d ee %d\n",
                __func__, env, env->pending_interrupts,
                env->interrupt_request, msr_me, msr_ee);
    }
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
#if defined(TARGET_PPC64H)
    if ((msr_ee != 0 || msr_hv == 0 || msr_pr == 1) & hdice != 0) {
        /* Hypervisor decrementer exception */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_HDECR)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_HDECR);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_HDECR);
            return;
        }
    }
#endif
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
#if defined(TARGET_PPCEMB)
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_CDOORBELL)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_CDOORBELL);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_DOORCI);
            return;
        }
#endif
#if defined(TARGET_PPCEMB)
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
#endif
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
#if !defined(TARGET_PPCEMB)
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
#endif
#if defined(TARGET_PPCEMB)
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_DOORBELL)) {
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_DOORBELL);
            powerpc_excp(env, env->excp_model, POWERPC_EXCP_DOORI);
            return;
        }
#endif
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

void cpu_dump_EA (target_ulong EA)
{
    FILE *f;

    if (logfile) {
        f = logfile;
    } else {
        f = stdout;
        return;
    }
    fprintf(f, "Memory access at address " ADDRX "\n", EA);
}

void cpu_dump_rfi (target_ulong RA, target_ulong msr)
{
    FILE *f;

    if (logfile) {
        f = logfile;
    } else {
        f = stdout;
        return;
    }
    fprintf(f, "Return from exception at " ADDRX " with flags " ADDRX "\n",
            RA, msr);
}

void cpu_ppc_reset (void *opaque)
{
    CPUPPCState *env;
    int i;

    env = opaque;
    /* XXX: some of those flags initialisation values could depend
     *      on the actual PowerPC implementation
     */
    for (i = 0; i < 63; i++)
        env->msr[i] = 0;
#if defined(TARGET_PPC64)
    msr_hv = 0; /* Should be 1... */
#endif
    msr_ap = 0; /* TO BE CHECKED */
    msr_sa = 0; /* TO BE CHECKED */
    msr_ip = 0; /* TO BE CHECKED */
#if defined (DO_SINGLE_STEP) && 0
    /* Single step trace mode */
    msr_se = 1;
    msr_be = 1;
#endif
#if defined(CONFIG_USER_ONLY)
    msr_fp = 1; /* Allow floating point exceptions */
    msr_pr = 1;
#else
    env->nip = env->hreset_vector | env->excp_prefix;
    ppc_tlb_invalidate_all(env);
#endif
    do_compute_hflags(env);
    env->reserve = -1;
    /* Be sure no exception or interrupt is pending */
    env->pending_interrupts = 0;
    env->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
    /* Flush all TLBs */
    tlb_flush(env, 1);
}

CPUPPCState *cpu_ppc_init (void)
{
    CPUPPCState *env;

    env = qemu_mallocz(sizeof(CPUPPCState));
    if (!env)
        return NULL;
    cpu_exec_init(env);

    return env;
}

void cpu_ppc_close (CPUPPCState *env)
{
    /* Should also remove all opcode tables... */
    free(env);
}
