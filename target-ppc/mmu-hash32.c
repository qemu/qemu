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

#include "cpu.h"
#include "helper.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash32.h"

//#define DEBUG_MMU

#ifdef DEBUG_MMU
#  define LOG_MMU(...) qemu_log(__VA_ARGS__)
#  define LOG_MMU_STATE(env) log_cpu_state((env), 0)
#else
#  define LOG_MMU(...) do { } while (0)
#  define LOG_MMU_STATE(...) do { } while (0)
#endif

#define PTE_PTEM_MASK 0x7FFFFFBF
#define PTE_CHECK_MASK (TARGET_PAGE_MASK | 0x7B)

static inline int pte_is_valid_hash32(target_ulong pte0)
{
    return pte0 & 0x80000000 ? 1 : 0;
}

static int pte_check_hash32(mmu_ctx_t *ctx, target_ulong pte0,
                            target_ulong pte1, int h, int rw, int type)
{
    target_ulong ptem, mmask;
    int access, ret, pteh, ptev, pp;

    ret = -1;
    /* Check validity and table match */
    ptev = pte_is_valid_hash32(pte0);
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
static int find_pte32(CPUPPCState *env, mmu_ctx_t *ctx, int h,
                      int rw, int type, int target_page_bits)
{
    hwaddr pteg_off;
    target_ulong pte0, pte1;
    int i, good = -1;
    int ret, r;

    ret = -1; /* No entry found */
    pteg_off = get_pteg_offset(env, ctx->hash[h], HASH_PTE_SIZE_32);
    for (i = 0; i < 8; i++) {
        if (env->external_htab) {
            pte0 = ldl_p(env->external_htab + pteg_off + (i * 8));
            pte1 = ldl_p(env->external_htab + pteg_off + (i * 8) + 4);
        } else {
            pte0 = ldl_phys(env->htab_base + pteg_off + (i * 8));
            pte1 = ldl_phys(env->htab_base + pteg_off + (i * 8) + 4);
        }
        r = pte_check_hash32(ctx, pte0, pte1, h, rw, type);
        LOG_MMU("Load pte from %08" HWADDR_PRIx " => " TARGET_FMT_lx " "
                TARGET_FMT_lx " %d %d %d " TARGET_FMT_lx "\n",
                pteg_off + (i * 8), pte0, pte1, (int)(pte0 >> 31), h,
                (int)((pte0 >> 6) & 1), ctx->ptem);
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
                stl_p(env->external_htab + pteg_off + (good * 8) + 4,
                      pte1);
            } else {
                stl_phys_notdirty(env->htab_base + pteg_off +
                                  (good * 8) + 4, pte1);
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

static int get_segment32(CPUPPCState *env, mmu_ctx_t *ctx,
                         target_ulong eaddr, int rw, int type)
{
    hwaddr hash;
    target_ulong vsid;
    int ds, pr, target_page_bits;
    int ret, ret2;
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
    LOG_MMU("Check segment v=" TARGET_FMT_lx " %d " TARGET_FMT_lx " nip="
            TARGET_FMT_lx " lr=" TARGET_FMT_lx
            " ir=%d dr=%d pr=%d %d t=%d\n",
            eaddr, (int)(eaddr >> 28), sr, env->nip, env->lr, (int)msr_ir,
            (int)msr_dr, pr != 0 ? 1 : 0, rw, type);
    pgidx = (eaddr & ~SEGMENT_MASK_256M) >> target_page_bits;
    hash = vsid ^ pgidx;
    ctx->ptem = (vsid << 7) | (pgidx >> 10);

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
            ctx->raddr = (hwaddr)-1ULL;
            LOG_MMU("0 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                    " vsid=" TARGET_FMT_lx " ptem=" TARGET_FMT_lx
                    " hash=" TARGET_FMT_plx "\n",
                    env->htab_base, env->htab_mask, vsid, ctx->ptem,
                    ctx->hash[0]);
            /* Primary table lookup */
            ret = find_pte32(env, ctx, 0, rw, type, target_page_bits);
            if (ret < 0) {
                /* Secondary table lookup */
                LOG_MMU("1 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                        " vsid=" TARGET_FMT_lx " api=" TARGET_FMT_lx
                        " hash=" TARGET_FMT_plx "\n", env->htab_base,
                        env->htab_mask, vsid, ctx->ptem, ctx->hash[1]);
                ret2 = find_pte32(env, ctx, 1, rw, type,
                                  target_page_bits);
                if (ret2 != -1) {
                    ret = ret2;
                }
            }
#if defined(DUMP_PAGE_TABLES)
            if (qemu_log_enabled()) {
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


static int ppc_hash32_get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                           target_ulong eaddr, int rw,
                                           int access_type)
{
    bool real_mode = (access_type == ACCESS_CODE && msr_ir == 0)
        || (access_type != ACCESS_CODE && msr_dr == 0);

    if (real_mode) {
        ctx->raddr = eaddr;
        ctx->prot = PAGE_READ | PAGE_EXEC | PAGE_WRITE;
        return 0;
    } else {
        int ret = -1;

        /* Try to find a BAT */
        if (env->nb_BATs != 0) {
            ret = get_bat(env, ctx, eaddr, rw, access_type);
        }
        if (ret < 0) {
            /* We didn't match any BAT entry or don't have BATs */
            ret = get_segment32(env, ctx, eaddr, rw, access_type);
        }
        return ret;
    }
}

hwaddr ppc_hash32_get_phys_page_debug(CPUPPCState *env, target_ulong addr)
{
    mmu_ctx_t ctx;

    if (unlikely(ppc_hash32_get_physical_address(env, &ctx, addr, 0, ACCESS_INT)
                 != 0)) {
        return -1;
    }

    return ctx.raddr & TARGET_PAGE_MASK;
}

int ppc_hash32_handle_mmu_fault(CPUPPCState *env, target_ulong address, int rw,
                                int mmu_idx)
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
    ret = ppc_hash32_get_physical_address(env, &ctx, address, rw, access_type);
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
                env->exception_index = POWERPC_EXCP_ISI;
                env->error_code = 0x40000000;
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
            }
        } else {
            switch (ret) {
            case -1:
                /* No matches in page tables or TLB */
                env->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                env->spr[SPR_DAR] = address;
                if (rw == 1) {
                    env->spr[SPR_DSISR] = 0x42000000;
                } else {
                    env->spr[SPR_DSISR] = 0x40000000;
                }
                break;
            case -2:
                /* Access rights violation */
                env->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                env->spr[SPR_DAR] = address;
                if (rw == 1) {
                    env->spr[SPR_DSISR] = 0x0A000000;
                } else {
                    env->spr[SPR_DSISR] = 0x08000000;
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
                    if (rw == 1) {
                        env->spr[SPR_DSISR] = 0x06000000;
                    } else {
                        env->spr[SPR_DSISR] = 0x04000000;
                    }
                    break;
                case ACCESS_EXT:
                    /* eciwx or ecowx */
                    env->exception_index = POWERPC_EXCP_DSI;
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
                    env->exception_index = POWERPC_EXCP_PROGRAM;
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
               env->exception, env->error_code);
#endif
        ret = 1;
    }

    return ret;
}
