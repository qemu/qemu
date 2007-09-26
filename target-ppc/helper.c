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
        exception = EXCP_ISI;
        error_code = 0;
    } else {
        exception = EXCP_DSI;
        error_code = 0;
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

void ppc6xx_tlb_invalidate_all (CPUState *env)
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
#if !defined(FLUSH_ALL_TLBS)
        tlb_flush_page(env, tlb->EPN);
#endif
        pte_invalidate(&tlb->pte0);
    }
#if defined(FLUSH_ALL_TLBS)
    tlb_flush(env, 1);
#endif
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

void ppc6xx_tlb_invalidate_virt (CPUState *env, target_ulong eaddr,
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
        } else
#endif
        {
            pte0 = ldl_phys(base + (i * 8));
            pte1 =  ldl_phys(base + (i * 8) + 4);
            r = pte32_check(ctx, pte0, pte1, h, rw);
        }
#if defined (DEBUG_MMU)
        if (loglevel != 0) {
            fprintf(logfile, "Load pte from 0x" ADDRX " => 0x" ADDRX
                    " 0x" ADDRX " %d %d %d 0x" ADDRX "\n",
                    base + (i * 8), pte0, pte1,
                    (int)(pte0 >> 31), h, (int)((pte0 >> 6) & 1), ctx->ptem);
        }
#endif
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
    if (env->mmu_model == POWERPC_MMU_64B ||
        env->mmu_model == POWERPC_MMU_64BRIDGE)
        return find_pte64(ctx, h, rw);
#endif

    return find_pte32(ctx, h, rw);
}

static inline target_phys_addr_t get_pgaddr (target_phys_addr_t sdr1,
                                             int sdr_sh,
                                             target_phys_addr_t hash,
                                             target_phys_addr_t mask)
{
    return (sdr1 & ((target_ulong)(-1ULL) << sdr_sh)) | (hash & mask);
}

#if defined(TARGET_PPC64)
static int slb_lookup (CPUState *env, target_ulong eaddr,
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
    mask = 0x0000000000000000ULL; /* Avoid gcc warning */
#if 0 /* XXX: Fix this */
    slb_nr = env->slb_nr;
#else
    slb_nr = 32;
#endif
    for (n = 0; n < slb_nr; n++) {
        tmp64 = ldq_phys(sr_base);
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
                tmp = ldl_phys(sr_base + 8);
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
#endif /* defined(TARGET_PPC64) */

/* Perform segment based translation */
static int get_segment (CPUState *env, mmu_ctx_t *ctx,
                        target_ulong eaddr, int rw, int type)
{
    target_phys_addr_t sdr, hash, mask, sdr_mask;
    target_ulong sr, vsid, vsid_mask, pgidx, page_mask;
#if defined(TARGET_PPC64)
    int attr;
#endif
    int ds, nx, vsid_sh, sdr_sh;
    int ret, ret2;

#if defined(TARGET_PPC64)
    if (env->mmu_model == POWERPC_MMU_64B) {
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
        if (!ds && loglevel != 0) {
            fprintf(logfile, "pte segment: key=%d n=0x" ADDRX "\n",
                    ctx->key, sr & 0x10000000);
        }
#endif
    }
    ret = -1;
    if (!ds) {
        /* Check if instruction fetch is allowed, if needed */
        if (type != ACCESS_CODE || nx == 0) {
            /* Page address translation */
            pgidx = (eaddr & page_mask) >> TARGET_PAGE_BITS;
            hash = ((vsid ^ pgidx) << vsid_sh) & vsid_mask;
            /* Primary table address */
            sdr = env->sdr1;
            mask = ((sdr & 0x000001FF) << sdr_sh) | sdr_mask;
            ctx->pg_addr[0] = get_pgaddr(sdr, sdr_sh, hash, mask);
            /* Secondary table address */
            hash = (~hash) & vsid_mask;
            ctx->pg_addr[1] = get_pgaddr(sdr, sdr_sh, hash, mask);
#if defined(TARGET_PPC64)
            if (env->mmu_model == POWERPC_MMU_64B ||
                env->mmu_model == POWERPC_MMU_64BRIDGE) {
                /* Only 5 bits of the page index are used in the AVPN */
                ctx->ptem = (vsid << 12) | ((pgidx >> 4) & 0x0F80);
            } else
#endif
            {
                ctx->ptem = (vsid << 7) | (pgidx >> 10);
            }
            /* Initialize real address with an invalid value */
            ctx->raddr = (target_ulong)-1;
            if (unlikely(env->mmu_model == POWERPC_MMU_SOFT_6xx)) {
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
    if (loglevel != 0) {
        fprintf(logfile, "%s: TLB %d address " ADDRX " PID %d <=> "
                ADDRX " " ADDRX " %d\n",
                __func__, i, address, pid, tlb->EPN, mask, (int)tlb->PID);
    }
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

void ppc4xx_tlb_invalidate_virt (CPUState *env, target_ulong eaddr,
                                 uint32_t pid)
{
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
}

/* Helpers specific to PowerPC 40x implementations */
void ppc4xx_tlb_invalidate_all (CPUState *env)
{
    ppcemb_tlb_t *tlb;
    int i;

    for (i = 0; i < env->nb_tlb; i++) {
        tlb = &env->tlb[i].tlbe;
        if (tlb->prot & PAGE_VALID) {
#if 0 // XXX: TLB have variable sizes then we flush all Qemu TLB.
            end = tlb->EPN + tlb->size;
            for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE)
                tlb_flush_page(env, page);
#endif
            tlb->prot &= ~PAGE_VALID;
        }
    }
    tlb_flush(env, 1);
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
        if (loglevel != 0) {
            fprintf(logfile, "%s: TLB %d zsel %d zpr %d rw %d attr %08x\n",
                    __func__, i, zsel, zpr, rw, tlb->attr);
        }
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
            if (loglevel != 0) {
                fprintf(logfile, "%s: access granted " ADDRX " => " REGX
                        " %d %d\n", __func__, address, ctx->raddr, ctx->prot,
                        ret);
            }
            return 0;
        }
    }
    if (loglevel != 0) {
        fprintf(logfile, "%s: access refused " ADDRX " => " REGX
                " %d %d\n", __func__, address, raddr, ctx->prot,
                ret);
    }

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
    case POWERPC_MMU_601:
    case POWERPC_MMU_SOFT_4xx:
    case POWERPC_MMU_REAL_4xx:
        ctx->prot |= PAGE_WRITE;
        break;
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_64BRIDGE:
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
    case POWERPC_MMU_BOOKE:
        ctx->prot |= PAGE_WRITE;
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
            /* Try to find a BAT */
            if (check_BATs)
                ret = get_bat(env, ctx, eaddr, rw, access_type);
            /* No break here */
#if defined(TARGET_PPC64)
        case POWERPC_MMU_64B:
        case POWERPC_MMU_64BRIDGE:
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
    int exception = 0, error_code = 0;
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
            exception = EXCP_ISI;
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                switch (env->mmu_model) {
                case POWERPC_MMU_SOFT_6xx:
                    exception = EXCP_I_TLBMISS;
                    env->spr[SPR_IMISS] = address;
                    env->spr[SPR_ICMP] = 0x80000000 | ctx.ptem;
                    error_code = 1 << 18;
                    goto tlb_miss;
                case POWERPC_MMU_SOFT_4xx:
                case POWERPC_MMU_SOFT_4xx_Z:
                    exception = EXCP_40x_ITLBMISS;
                    error_code = 0;
                    env->spr[SPR_40x_DEAR] = address;
                    env->spr[SPR_40x_ESR] = 0x00000000;
                    break;
                case POWERPC_MMU_32B:
                    error_code = 0x40000000;
                    break;
#if defined(TARGET_PPC64)
                case POWERPC_MMU_64B:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
                case POWERPC_MMU_64BRIDGE:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
#endif
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
                error_code = 0x08000000;
                break;
            case -3:
                /* No execute protection violation */
                error_code = 0x10000000;
                break;
            case -4:
                /* Direct store exception */
                /* No code fetch is allowed in direct-store areas */
                error_code = 0x10000000;
                break;
            case -5:
                /* No match in segment table */
                exception = EXCP_ISEG;
                error_code = 0;
                break;
            }
        } else {
            exception = EXCP_DSI;
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                switch (env->mmu_model) {
                case POWERPC_MMU_SOFT_6xx:
                    if (rw == 1) {
                        exception = EXCP_DS_TLBMISS;
                        error_code = 1 << 16;
                    } else {
                        exception = EXCP_DL_TLBMISS;
                        error_code = 0;
                    }
                    env->spr[SPR_DMISS] = address;
                    env->spr[SPR_DCMP] = 0x80000000 | ctx.ptem;
                tlb_miss:
                    error_code |= ctx.key << 19;
                    env->spr[SPR_HASH1] = ctx.pg_addr[0];
                    env->spr[SPR_HASH2] = ctx.pg_addr[1];
                    /* Do not alter DAR nor DSISR */
                    goto out;
                case POWERPC_MMU_SOFT_4xx:
                case POWERPC_MMU_SOFT_4xx_Z:
                    exception = EXCP_40x_DTLBMISS;
                    error_code = 0;
                    env->spr[SPR_40x_DEAR] = address;
                    if (rw)
                        env->spr[SPR_40x_ESR] = 0x00800000;
                    else
                        env->spr[SPR_40x_ESR] = 0x00000000;
                    break;
                case POWERPC_MMU_32B:
                    error_code = 0x40000000;
                    break;
#if defined(TARGET_PPC64)
                case POWERPC_MMU_64B:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
                case POWERPC_MMU_64BRIDGE:
                    /* XXX: TODO */
                    cpu_abort(env, "MMU model not implemented\n");
                    return -1;
#endif
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
                error_code = 0x08000000;
                break;
            case -4:
                /* Direct store exception */
                switch (access_type) {
                case ACCESS_FLOAT:
                    /* Floating point load/store */
                    exception = EXCP_ALIGN;
                    error_code = EXCP_ALIGN_FP;
                    break;
                case ACCESS_RES:
                    /* lwarx, ldarx or srwcx. */
                    error_code = 0x04000000;
                    break;
                case ACCESS_EXT:
                    /* eciwx or ecowx */
                    error_code = 0x04100000;
                    break;
                default:
                    printf("DSI: invalid exception (%d)\n", ret);
                    exception = EXCP_PROGRAM;
                    error_code = EXCP_INVAL | EXCP_INVAL_INVAL;
                    break;
                }
                break;
            case -5:
                /* No match in segment table */
                exception = EXCP_DSEG;
                error_code = 0;
                break;
            }
            if (exception == EXCP_DSI && rw == 1)
                error_code |= 0x02000000;
            /* Store fault address */
            env->spr[SPR_DAR] = address;
            env->spr[SPR_DSISR] = error_code;
        }
    out:
#if 0
        printf("%s: set exception to %d %02x\n",
               __func__, exception, error_code);
#endif
        env->exception_index = exception;
        env->error_code = error_code;
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
    if (unlikely(env->mmu_model == POWERPC_MMU_SOFT_6xx)) {
        ppc6xx_tlb_invalidate_all(env);
    } else if (unlikely(env->mmu_model == POWERPC_MMU_SOFT_4xx)) {
        ppc4xx_tlb_invalidate_all(env);
    } else {
        tlb_flush(env, 1);
    }
}

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
        env->sdr1 = value;
        tlb_flush(env, 1);
    }
}

target_ulong do_load_sr (CPUPPCState *env, int srnum)
{
    return env->sr[srnum];
}

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

uint32_t ppc_load_xer (CPUPPCState *env)
{
    return (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC) |
        (xer_cmp << XER_CMP);
}

void ppc_store_xer (CPUPPCState *env, uint32_t value)
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

void do_store_msr (CPUPPCState *env, target_ulong value)
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
    if (enter_pm) {
        if (likely(!env->halted)) {
            /* power save: exit cpu loop */
            env->halted = 1;
            env->exception_index = EXCP_HLT;
            cpu_loop_exit();
        }
    }
}

#if defined(TARGET_PPC64)
void ppc_store_msr_32 (CPUPPCState *env, uint32_t value)
{
    do_store_msr(env,
                 (do_load_msr(env) & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF));
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
    env->exception_index = -1;
}

void ppc_hw_interrupt (CPUState *env)
{
    env->exception_index = -1;
}
#else /* defined (CONFIG_USER_ONLY) */
static void dump_syscall (CPUState *env)
{
    fprintf(logfile, "syscall r0=0x" REGX " r3=0x" REGX " r4=0x" REGX
            " r5=0x" REGX " r6=0x" REGX " nip=0x" ADDRX "\n",
            env->gpr[0], env->gpr[3], env->gpr[4],
            env->gpr[5], env->gpr[6], env->nip);
}

void do_interrupt (CPUState *env)
{
    target_ulong msr, *srr_0, *srr_1, *asrr_0, *asrr_1;
    int excp, idx;

    excp = env->exception_index;
    msr = do_load_msr(env);
    /* The default is to use SRR0 & SRR1 to save the exception context */
    srr_0 = &env->spr[SPR_SRR0];
    srr_1 = &env->spr[SPR_SRR1];
    asrr_0 = NULL;
    asrr_1 = NULL;
#if defined (DEBUG_EXCEPTIONS)
    if ((excp == EXCP_PROGRAM || excp == EXCP_DSI) && msr_pr == 1) {
        if (loglevel != 0) {
            fprintf(logfile,
                    "Raise exception at 0x" ADDRX " => 0x%08x (%02x)\n",
                    env->nip, excp, env->error_code);
            cpu_dump_state(env, logfile, fprintf, 0);
        }
    }
#endif
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "Raise exception at 0x" ADDRX " => 0x%08x (%02x)\n",
                env->nip, excp, env->error_code);
    }
    msr_pow = 0;
    idx = -1;
    /* Generate informations in save/restore registers */
    switch (excp) {
    /* Generic PowerPC exceptions */
    case EXCP_RESET: /* 0x0100 */
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            srr_0 = &env->spr[SPR_40x_SRR2];
            srr_1 = &env->spr[SPR_40x_SRR3];
            break;
        case POWERPC_EXCP_BOOKE:
            idx = 0;
            srr_0 = &env->spr[SPR_BOOKE_CSRR0];
            srr_1 = &env->spr[SPR_BOOKE_CSRR1];
            break;
        default:
            if (msr_ip)
                excp += 0xFFC00;
            excp |= 0xFFC00000;
            break;
        }
        goto store_next;
    case EXCP_MACHINE_CHECK: /* 0x0200 */
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            srr_0 = &env->spr[SPR_40x_SRR2];
            srr_1 = &env->spr[SPR_40x_SRR3];
            break;
        case POWERPC_EXCP_BOOKE:
            idx = 1;
            srr_0 = &env->spr[SPR_BOOKE_MCSRR0];
            srr_1 = &env->spr[SPR_BOOKE_MCSRR1];
            asrr_0 = &env->spr[SPR_BOOKE_CSRR0];
            asrr_1 = &env->spr[SPR_BOOKE_CSRR1];
            msr_ce = 0;
            break;
        default:
            break;
        }
        msr_me = 0;
        break;
    case EXCP_DSI: /* 0x0300 */
        /* Store exception cause */
        /* data location address has been stored
         * when the fault has been detected
         */
        idx = 2;
        msr &= ~0xFFFF0000;
#if defined (DEBUG_EXCEPTIONS)
        if (loglevel != 0) {
            fprintf(logfile, "DSI exception: DSISR=0x" ADDRX" DAR=0x" ADDRX
                    "\n", env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        }
#endif
        goto store_next;
    case EXCP_ISI: /* 0x0400 */
        /* Store exception cause */
        idx = 3;
        msr &= ~0xFFFF0000;
        msr |= env->error_code;
#if defined (DEBUG_EXCEPTIONS)
        if (loglevel != 0) {
            fprintf(logfile, "ISI exception: msr=0x" ADDRX ", nip=0x" ADDRX
                    "\n", msr, env->nip);
        }
#endif
        goto store_next;
    case EXCP_EXTERNAL: /* 0x0500 */
        idx = 4;
        goto store_next;
    case EXCP_ALIGN: /* 0x0600 */
        if (likely(env->excp_model != POWERPC_EXCP_601)) {
            /* Store exception cause */
            idx = 5;
            /* Get rS/rD and rA from faulting opcode */
            env->spr[SPR_DSISR] |=
                (ldl_code((env->nip - 4)) & 0x03FF0000) >> 16;
            /* data location address has been stored
             * when the fault has been detected
             */
        } else {
            /* IO error exception on PowerPC 601 */
            /* XXX: TODO */
            cpu_abort(env,
                      "601 IO error exception is not implemented yet !\n");
        }
        goto store_current;
    case EXCP_PROGRAM: /* 0x0700 */
        idx = 6;
        msr &= ~0xFFFF0000;
        switch (env->error_code & ~0xF) {
        case EXCP_FP:
            if (msr_fe0 == 0 && msr_fe1 == 0) {
#if defined (DEBUG_EXCEPTIONS)
                if (loglevel != 0) {
                    fprintf(logfile, "Ignore floating point exception\n");
                }
#endif
                return;
            }
            msr |= 0x00100000;
            /* Set FX */
            env->fpscr[7] |= 0x8;
            /* Finally, update FEX */
            if ((((env->fpscr[7] & 0x3) << 3) | (env->fpscr[6] >> 1)) &
                ((env->fpscr[1] << 1) | (env->fpscr[0] >> 3)))
                env->fpscr[7] |= 0x4;
            break;
        case EXCP_INVAL:
#if defined (DEBUG_EXCEPTIONS)
            if (loglevel != 0) {
                fprintf(logfile, "Invalid instruction at 0x" ADDRX "\n",
                        env->nip);
            }
#endif
            msr |= 0x00080000;
            break;
        case EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case EXCP_TRAP:
            idx = 15;
            msr |= 0x00020000;
            break;
        default:
            /* Should never occur */
            break;
        }
        msr |= 0x00010000;
        goto store_current;
    case EXCP_NO_FP: /* 0x0800 */
        idx = 7;
        msr &= ~0xFFFF0000;
        goto store_current;
    case EXCP_DECR:
        goto store_next;
    case EXCP_SYSCALL: /* 0x0C00 */
        idx = 8;
        /* NOTE: this is a temporary hack to support graphics OSI
           calls from the MOL driver */
        if (env->gpr[3] == 0x113724fa && env->gpr[4] == 0x77810f9b &&
            env->osi_call) {
            if (env->osi_call(env) != 0)
                return;
        }
        if (loglevel & CPU_LOG_INT) {
            dump_syscall(env);
        }
        goto store_next;
    case EXCP_TRACE: /* 0x0D00 */
        goto store_next;
    case EXCP_PERF: /* 0x0F00 */
        /* XXX: TODO */
        cpu_abort(env,
                  "Performance counter exception is not implemented yet !\n");
        goto store_next;
    /* 32 bits PowerPC specific exceptions */
    case EXCP_FP_ASSIST: /* 0x0E00 */
        /* XXX: TODO */
        cpu_abort(env, "Floating point assist exception "
                  "is not implemented yet !\n");
        goto store_next;
    /* 64 bits PowerPC exceptions */
    case EXCP_DSEG: /* 0x0380 */
        /* XXX: TODO */
        cpu_abort(env, "Data segment exception is not implemented yet !\n");
        goto store_next;
    case EXCP_ISEG: /* 0x0480 */
        /* XXX: TODO */
        cpu_abort(env,
                  "Instruction segment exception is not implemented yet !\n");
        goto store_next;
    case EXCP_HDECR: /* 0x0980 */
        /* XXX: TODO */
        cpu_abort(env, "Hypervisor decrementer exception is not implemented "
                  "yet !\n");
        goto store_next;
    /* Implementation specific exceptions */
    case 0x0A00:
        switch (env->excp_model) {
        case POWERPC_EXCP_G2:
            /* Critical interrupt on G2 */
            /* XXX: TODO */
            cpu_abort(env, "G2 critical interrupt is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x0A00 !\n");
            break;
        }
        return;
    case 0x0F20:
        idx = 9;
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            /* APU unavailable on 405 */
            /* XXX: TODO */
            cpu_abort(env,
                      "APU unavailable exception is not implemented yet !\n");
            goto store_next;
        case POWERPC_EXCP_74xx:
            /* Altivec unavailable */
            /* XXX: TODO */
            cpu_abort(env, "Altivec unavailable exception "
                      "is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x0F20 !\n");
            break;
        }
        return;
    case 0x1000:
        idx = 10;
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            /* PIT on 4xx */
            msr &= ~0xFFFF0000;
#if defined (DEBUG_EXCEPTIONS)
            if (loglevel != 0)
                fprintf(logfile, "PIT exception\n");
#endif
            goto store_next;
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
            /* ITLBMISS on 602/603 */
            goto store_gprs;
        case POWERPC_EXCP_7x5:
            /* ITLBMISS on 745/755 */
            goto tlb_miss;
        default:
            cpu_abort(env, "Invalid exception 0x1000 !\n");
            break;
        }
        return;
    case 0x1010:
        idx = 11;
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            /* FIT on 4xx */
            msr &= ~0xFFFF0000;
#if defined (DEBUG_EXCEPTIONS)
            if (loglevel != 0)
                fprintf(logfile, "FIT exception\n");
#endif
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1010 !\n");
            break;
        }
        return;
    case 0x1020:
        idx = 12;
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            /* Watchdog on 4xx */
            msr &= ~0xFFFF0000;
#if defined (DEBUG_EXCEPTIONS)
            if (loglevel != 0)
                fprintf(logfile, "WDT exception\n");
#endif
            goto store_next;
        case POWERPC_EXCP_BOOKE:
            srr_0 = &env->spr[SPR_BOOKE_CSRR0];
            srr_1 = &env->spr[SPR_BOOKE_CSRR1];
            break;
        default:
            cpu_abort(env, "Invalid exception 0x1020 !\n");
            break;
        }
        return;
    case 0x1100:
        idx = 13;
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            /* DTLBMISS on 4xx */
            msr &= ~0xFFFF0000;
            goto store_next;
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
            /* DLTLBMISS on 602/603 */
            goto store_gprs;
        case POWERPC_EXCP_7x5:
            /* DLTLBMISS on 745/755 */
            goto tlb_miss;
        default:
            cpu_abort(env, "Invalid exception 0x1100 !\n");
            break;
        }
        return;
    case 0x1200:
        idx = 14;
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            /* ITLBMISS on 4xx */
            msr &= ~0xFFFF0000;
            goto store_next;
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
            /* DSTLBMISS on 602/603 */
        store_gprs:
            /* Swap temporary saved registers with GPRs */
            swap_gpr_tgpr(env);
            msr_tgpr = 1;
#if defined (DEBUG_SOFTWARE_TLB)
            if (loglevel != 0) {
                const unsigned char *es;
                target_ulong *miss, *cmp;
                int en;
                if (excp == 0x1000) {
                    es = "I";
                    en = 'I';
                    miss = &env->spr[SPR_IMISS];
                    cmp = &env->spr[SPR_ICMP];
                } else {
                    if (excp == 0x1100)
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
            goto tlb_miss;
        case POWERPC_EXCP_7x5:
            /* DSTLBMISS on 745/755 */
        tlb_miss:
            msr &= ~0xF83F0000;
            msr |= env->crf[0] << 28;
            msr |= env->error_code; /* key, D/I, S/L bits */
            /* Set way using a LRU mechanism */
            msr |= ((env->last_way + 1) & (env->nb_ways - 1)) << 17;
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1200 !\n");
            break;
        }
        return;
    case 0x1300:
        switch (env->excp_model) {
        case POWERPC_EXCP_601:
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
        case POWERPC_EXCP_604:
        case POWERPC_EXCP_7x0:
        case POWERPC_EXCP_7x5:
            /* IABR on 6xx/7xx */
            /* XXX: TODO */
            cpu_abort(env, "IABR exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1300 !\n");
            break;
        }
        return;
    case 0x1400:
        switch (env->excp_model) {
        case POWERPC_EXCP_601:
        case POWERPC_EXCP_602:
        case POWERPC_EXCP_603:
        case POWERPC_EXCP_603E:
        case POWERPC_EXCP_G2:
        case POWERPC_EXCP_604:
        case POWERPC_EXCP_7x0:
        case POWERPC_EXCP_7x5:
            /* SMI on 6xx/7xx */
            /* XXX: TODO */
            cpu_abort(env, "SMI exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1400 !\n");
            break;
        }
        return;
    case 0x1500:
        switch (env->excp_model) {
        case POWERPC_EXCP_602:
            /* Watchdog on 602 */
            /* XXX: TODO */
            cpu_abort(env,
                      "602 watchdog exception is not implemented yet !\n");
            goto store_next;
        case POWERPC_EXCP_970:
            /* Soft patch exception on 970 */
            /* XXX: TODO */
            cpu_abort(env,
                      "970 soft-patch exception is not implemented yet !\n");
            goto store_next;
        case POWERPC_EXCP_74xx:
            /* VPU assist on 74xx */
            /* XXX: TODO */
            cpu_abort(env, "VPU assist exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1500 !\n");
            break;
        }
        return;
    case 0x1600:
        switch (env->excp_model) {
        case POWERPC_EXCP_602:
            /* Emulation trap on 602 */
            /* XXX: TODO */
            cpu_abort(env, "602 emulation trap exception "
                      "is not implemented yet !\n");
            goto store_next;
        case POWERPC_EXCP_970:
            /* Maintenance exception on 970 */
            /* XXX: TODO */
            cpu_abort(env,
                      "970 maintenance exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1600 !\n");
            break;
        }
        return;
    case 0x1700:
        switch (env->excp_model) {
        case POWERPC_EXCP_7x0:
        case POWERPC_EXCP_7x5:
            /* Thermal management interrupt on G3 */
            /* XXX: TODO */
            cpu_abort(env, "G3 thermal management exception "
                      "is not implemented yet !\n");
            goto store_next;
        case POWERPC_EXCP_970:
            /* VPU assist on 970 */
            /* XXX: TODO */
            cpu_abort(env,
                      "970 VPU assist exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1700 !\n");
            break;
        }
        return;
    case 0x1800:
        switch (env->excp_model) {
        case POWERPC_EXCP_970:
            /* Thermal exception on 970 */
            /* XXX: TODO */
            cpu_abort(env, "970 thermal management exception "
                      "is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1800 !\n");
            break;
        }
        return;
    case 0x2000:
        switch (env->excp_model) {
        case POWERPC_EXCP_40x:
            /* DEBUG on 4xx */
            /* XXX: TODO */
            cpu_abort(env, "40x debug exception is not implemented yet !\n");
            goto store_next;
        case POWERPC_EXCP_601:
            /* Run mode exception on 601 */
            /* XXX: TODO */
            cpu_abort(env,
                      "601 run mode exception is not implemented yet !\n");
            goto store_next;
        case POWERPC_EXCP_BOOKE:
            srr_0 = &env->spr[SPR_BOOKE_CSRR0];
            srr_1 = &env->spr[SPR_BOOKE_CSRR1];
            break;
        default:
            cpu_abort(env, "Invalid exception 0x1800 !\n");
            break;
        }
        return;
    /* Other exceptions */
    /* Qemu internal exceptions:
     * we should never come here with those values: abort execution
     */
    default:
        cpu_abort(env, "Invalid exception: code %d (%04x)\n", excp, excp);
        return;
    store_current:
        /* save current instruction location */
        *srr_0 = env->nip - 4;
        break;
    store_next:
        /* save next instruction location */
        *srr_0 = env->nip;
        break;
    }
    /* Save msr */
    *srr_1 = msr;
    if (asrr_0 != NULL)
        *asrr_0 = *srr_0;
    if (asrr_1 != NULL)
        *asrr_1 = *srr_1;
    /* If we disactivated any translation, flush TLBs */
    if (msr_ir || msr_dr) {
        tlb_flush(env, 1);
    }
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
    msr_ri = 0;
    msr_le = msr_ile;
    if (env->excp_model == POWERPC_EXCP_BOOKE) {
        msr_cm = msr_icm;
        if (idx == -1 || (idx >= 16 && idx < 32)) {
            cpu_abort(env, "Invalid exception index for excp %d %08x idx %d\n",
                      excp, excp, idx);
        }
#if defined(TARGET_PPC64)
        if (msr_cm)
            env->nip = (uint64_t)env->spr[SPR_BOOKE_IVPR];
        else
#endif
            env->nip = (uint32_t)env->spr[SPR_BOOKE_IVPR];
        if (idx < 16)
            env->nip |= env->spr[SPR_BOOKE_IVOR0 + idx];
        else if (idx < 38)
            env->nip |= env->spr[SPR_BOOKE_IVOR32 + idx - 32];
    } else {
        msr_sf = msr_isf;
        env->nip = excp;
    }
    do_compute_hflags(env);
    /* Jump to handler */
    env->exception_index = EXCP_NONE;
}

void ppc_hw_interrupt (CPUPPCState *env)
{
    int raised = 0;

#if 1
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "%s: %p pending %08x req %08x me %d ee %d\n",
                __func__, env, env->pending_interrupts,
                env->interrupt_request, msr_me, msr_ee);
    }
#endif
    /* Raise it */
    if (env->pending_interrupts & (1 << PPC_INTERRUPT_RESET)) {
        /* External reset / critical input */
        /* XXX: critical input should be handled another way.
         *      This code is not correct !
         */
        env->exception_index = EXCP_RESET;
        env->pending_interrupts &= ~(1 << PPC_INTERRUPT_RESET);
        raised = 1;
    }
    if (raised == 0 && msr_me != 0) {
        /* Machine check exception */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_MCK)) {
            env->exception_index = EXCP_MACHINE_CHECK;
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_MCK);
            raised = 1;
        }
    }
    if (raised == 0 && msr_ee != 0) {
#if defined(TARGET_PPC64H) /* PowerPC 64 with hypervisor mode support */
        /* Hypervisor decrementer exception */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_HDECR)) {
            env->exception_index = EXCP_HDECR;
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_HDECR);
            raised = 1;
        } else
#endif
        /* Decrementer exception */
        if (env->pending_interrupts & (1 << PPC_INTERRUPT_DECR)) {
            env->exception_index = EXCP_DECR;
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_DECR);
            raised = 1;
        /* Programmable interval timer on embedded PowerPC */
        } else if (env->pending_interrupts & (1 << PPC_INTERRUPT_PIT)) {
            env->exception_index = EXCP_40x_PIT;
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_PIT);
            raised = 1;
        /* Fixed interval timer on embedded PowerPC */
        } else if (env->pending_interrupts & (1 << PPC_INTERRUPT_FIT)) {
            env->exception_index = EXCP_40x_FIT;
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_FIT);
            raised = 1;
        /* Watchdog timer on embedded PowerPC */
        } else if (env->pending_interrupts & (1 << PPC_INTERRUPT_WDT)) {
            env->exception_index = EXCP_40x_WATCHDOG;
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_WDT);
            raised = 1;
        /* External interrupt */
        } else if (env->pending_interrupts & (1 << PPC_INTERRUPT_EXT)) {
            env->exception_index = EXCP_EXTERNAL;
            /* Taking an external interrupt does not clear the external
             * interrupt status
             */
#if 0
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_EXT);
#endif
            raised = 1;
#if 0 // TODO
        /* Thermal interrupt */
        } else if (env->pending_interrupts & (1 << PPC_INTERRUPT_THERM)) {
            env->exception_index = EXCP_970_THRM;
            env->pending_interrupts &= ~(1 << PPC_INTERRUPT_THERM);
            raised = 1;
#endif
        }
#if 0 // TODO
    /* External debug exception */
    } else if (env->pending_interrupts & (1 << PPC_INTERRUPT_DEBUG)) {
        env->exception_index = EXCP_xxx;
        env->pending_interrupts &= ~(1 << PPC_INTERRUPT_DEBUG);
        raised = 1;
#endif
    }
    if (raised != 0) {
        env->error_code = 0;
        do_interrupt(env);
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
    env->nip = 0xFFFFFFFC;
    ppc_tlb_invalidate_all(env);
#endif
    do_compute_hflags(env);
    env->reserve = -1;
    /* Be sure no exception or interrupt is pending */
    env->pending_interrupts = 0;
    env->exception_index = EXCP_NONE;
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
    cpu_ppc_reset(env);

    return env;
}

void cpu_ppc_close (CPUPPCState *env)
{
    /* Should also remove all opcode tables... */
    free(env);
}
