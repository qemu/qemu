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
#include "cpu.h"
#include "helper.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"

//#define DEBUG_MMU
//#define DEBUG_SLB

#ifdef DEBUG_MMU
#  define LOG_MMU(...) qemu_log(__VA_ARGS__)
#  define LOG_MMU_STATE(env) log_cpu_state((env), 0)
#else
#  define LOG_MMU(...) do { } while (0)
#  define LOG_MMU_STATE(...) do { } while (0)
#endif

#ifdef DEBUG_SLB
#  define LOG_SLB(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_SLB(...) do { } while (0)
#endif

/*
 * SLB handling
 */

static ppc_slb_t *slb_lookup(CPUPPCState *env, target_ulong eaddr)
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

void dump_slb(FILE *f, fprintf_function cpu_fprintf, CPUPPCState *env)
{
    int i;
    uint64_t slbe, slbv;

    cpu_synchronize_state(env);

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
        tlb_flush(env, 1);
    }
}

void helper_slbie(CPUPPCState *env, target_ulong addr)
{
    ppc_slb_t *slb;

    slb = slb_lookup(env, addr);
    if (!slb) {
        return;
    }

    if (slb->esid & SLB_ESID_V) {
        slb->esid &= ~SLB_ESID_V;

        /* XXX: given the fact that segment size is 256 MB or 1TB,
         *      and we still don't have a tlb_flush_mask(env, n, mask)
         *      in QEMU, we just invalidate all TLBs
         */
        tlb_flush(env, 1);
    }
}

int ppc_store_slb(CPUPPCState *env, target_ulong rb, target_ulong rs)
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

static int ppc_load_slb_esid(CPUPPCState *env, target_ulong rb,
                             target_ulong *rt)
{
    int slot = rb & 0xfff;
    ppc_slb_t *slb = &env->slb[slot];

    if (slot >= env->slb_nr) {
        return -1;
    }

    *rt = slb->esid;
    return 0;
}

static int ppc_load_slb_vsid(CPUPPCState *env, target_ulong rb,
                             target_ulong *rt)
{
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
    if (ppc_store_slb(env, rb, rs) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
    }
}

target_ulong helper_load_slb_esid(CPUPPCState *env, target_ulong rb)
{
    target_ulong rt = 0;

    if (ppc_load_slb_esid(env, rb, &rt) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
    }
    return rt;
}

target_ulong helper_load_slb_vsid(CPUPPCState *env, target_ulong rb)
{
    target_ulong rt = 0;

    if (ppc_load_slb_vsid(env, rb, &rt) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
    }
    return rt;
}

/*
 * 64-bit hash table MMU handling
 */

#define PTE64_PTEM_MASK 0xFFFFFFFFFFFFFF80ULL
#define PTE64_CHECK_MASK (TARGET_PAGE_MASK | 0x7F)

static inline int pte64_is_valid(target_ulong pte0)
{
    return pte0 & 0x0000000000000001ULL ? 1 : 0;
}

static int pte64_check(mmu_ctx_t *ctx, target_ulong pte0,
                       target_ulong pte1, int h, int rw, int type)
{
    target_ulong ptem, mmask;
    int access, ret, pteh, ptev, pp;

    ret = -1;
    /* Check validity and table match */
    ptev = pte64_is_valid(pte0);
    pteh = (pte0 >> 1) & 1;
    if (ptev && h == pteh) {
        /* Check vsid & api */
        ptem = pte0 & PTE64_PTEM_MASK;
        mmask = PTE64_CHECK_MASK;
        pp = (pte1 & 0x00000003) | ((pte1 >> 61) & 0x00000004);
        ctx->nx  = (pte1 >> 2) & 1; /* No execute bit */
        ctx->nx |= (pte1 >> 3) & 1; /* Guarded bit    */
        if (ptem == ctx->ptem) {
            if (ctx->raddr != (hwaddr)-1ULL) {
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

/* PTE table lookup */
static int find_pte64(CPUPPCState *env, mmu_ctx_t *ctx, int h,
                      int rw, int type, int target_page_bits)
{
    hwaddr pteg_off;
    target_ulong pte0, pte1;
    int i, good = -1;
    int ret, r;

    ret = -1; /* No entry found */
    pteg_off = get_pteg_offset(env, ctx->hash[h], HASH_PTE_SIZE_64);
    for (i = 0; i < 8; i++) {
        if (env->external_htab) {
            pte0 = ldq_p(env->external_htab + pteg_off + (i * 16));
            pte1 = ldq_p(env->external_htab + pteg_off + (i * 16) + 8);
        } else {
            pte0 = ldq_phys(env->htab_base + pteg_off + (i * 16));
            pte1 = ldq_phys(env->htab_base + pteg_off + (i * 16) + 8);
        }

        r = pte64_check(ctx, pte0, pte1, h, rw, type);
        LOG_MMU("Load pte from %016" HWADDR_PRIx " => " TARGET_FMT_lx " "
                TARGET_FMT_lx " %d %d %d " TARGET_FMT_lx "\n",
                pteg_off + (i * 16), pte0, pte1, (int)(pte0 & 1), h,
                (int)((pte0 >> 1) & 1), ctx->ptem);
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
        LOG_MMU("found PTE at addr %08" HWADDR_PRIx " prot=%01x ret=%d\n",
                ctx->raddr, ctx->prot, ret);
        /* Update page flags */
        pte1 = ctx->raddr;
        if (pte_update_flags(ctx, &pte1, ret, rw) == 1) {
            if (env->external_htab) {
                stq_p(env->external_htab + pteg_off + (good * 16) + 8,
                      pte1);
            } else {
                stq_phys_notdirty(env->htab_base + pteg_off +
                                  (good * 16) + 8, pte1);
            }
        }
    }

    /* We have a TLB that saves 4K pages, so let's
     * split a huge page to 4k chunks */
    if (target_page_bits != TARGET_PAGE_BITS) {
        ctx->raddr |= (ctx->eaddr & ((1 << target_page_bits) - 1))
                      & TARGET_PAGE_MASK;
    }
    return ret;
}

int get_segment64(CPUPPCState *env, mmu_ctx_t *ctx,
                  target_ulong eaddr, int rw, int type)
{
    hwaddr hash;
    target_ulong vsid;
    int pr, target_page_bits;
    int ret, ret2;

    pr = msr_pr;
    ctx->eaddr = eaddr;
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

    LOG_MMU("pte segment: key=%d nx %d vsid " TARGET_FMT_lx "\n",
            ctx->key, ctx->nx, vsid);
    ret = -1;

    /* Check if instruction fetch is allowed, if needed */
    if (type != ACCESS_CODE || ctx->nx == 0) {
        /* Page address translation */
        LOG_MMU("htab_base " TARGET_FMT_plx " htab_mask " TARGET_FMT_plx
                " hash " TARGET_FMT_plx "\n",
                env->htab_base, env->htab_mask, hash);
        ctx->hash[0] = hash;
        ctx->hash[1] = ~hash;

        /* Initialize real address with an invalid value */
        ctx->raddr = (hwaddr)-1ULL;
        LOG_MMU("0 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                " vsid=" TARGET_FMT_lx " ptem=" TARGET_FMT_lx
                " hash=" TARGET_FMT_plx "\n",
                env->htab_base, env->htab_mask, vsid, ctx->ptem,
                ctx->hash[0]);
        /* Primary table lookup */
        ret = find_pte64(env, ctx, 0, rw, type, target_page_bits);
        if (ret < 0) {
            /* Secondary table lookup */
            LOG_MMU("1 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                    " vsid=" TARGET_FMT_lx " api=" TARGET_FMT_lx
                    " hash=" TARGET_FMT_plx "\n", env->htab_base,
                    env->htab_mask, vsid, ctx->ptem, ctx->hash[1]);
            ret2 = find_pte64(env, ctx, 1, rw, type, target_page_bits);
            if (ret2 != -1) {
                ret = ret2;
            }
        }
    } else {
        LOG_MMU("No access allowed\n");
        ret = -3;
    }

    return ret;
}
