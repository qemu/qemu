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
//#define DEBUG_BAT

#ifdef DEBUG_MMU
#  define LOG_MMU(...) qemu_log(__VA_ARGS__)
#  define LOG_MMU_STATE(env) log_cpu_state((env), 0)
#else
#  define LOG_MMU(...) do { } while (0)
#  define LOG_MMU_STATE(...) do { } while (0)
#endif

#ifdef DEBUG_BATS
#  define LOG_BATS(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_BATS(...) do { } while (0)
#endif

struct mmu_ctx_hash32 {
    hwaddr raddr;      /* Real address              */
    int prot;                      /* Protection bits           */
    int key;                       /* Access key                */
    int nx;                        /* Non-execute area          */
};

static int ppc_hash32_pp_check(int key, int pp, int nx)
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

static int ppc_hash32_check_prot(int prot, int rwx)
{
    int ret;

    if (rwx == 2) {
        if (prot & PAGE_EXEC) {
            ret = 0;
        } else {
            ret = -2;
        }
    } else if (rwx) {
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

static target_ulong hash32_bat_size(CPUPPCState *env,
                                    target_ulong batu, target_ulong batl)
{
    if ((msr_pr && !(batu & BATU32_VP))
        || (!msr_pr && !(batu & BATU32_VS))) {
        return 0;
    }

    return BATU32_BEPI & ~((batu & BATU32_BL) << 15);
}

static int hash32_bat_prot(CPUPPCState *env,
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

static target_ulong hash32_bat_601_size(CPUPPCState *env,
                                target_ulong batu, target_ulong batl)
{
    if (!(batl & BATL32_601_V)) {
        return 0;
    }

    return BATU32_BEPI & ~((batl & BATL32_601_BL) << 17);
}

static int hash32_bat_601_prot(CPUPPCState *env,
                               target_ulong batu, target_ulong batl)
{
    int key, pp;

    pp = batu & BATU32_601_PP;
    if (msr_pr == 0) {
        key = !!(batu & BATU32_601_KS);
    } else {
        key = !!(batu & BATU32_601_KP);
    }
    return ppc_hash32_pp_check(key, pp, 0);
}

static hwaddr ppc_hash32_bat_lookup(CPUPPCState *env, target_ulong ea, int rwx,
                                    int *prot)
{
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
            mask = hash32_bat_601_size(env, batu, batl);
        } else {
            mask = hash32_bat_size(env, batu, batl);
        }
        LOG_BATS("%s: %cBAT%d v " TARGET_FMT_lx " BATu " TARGET_FMT_lx
                 " BATl " TARGET_FMT_lx "\n", __func__,
                 type == ACCESS_CODE ? 'I' : 'D', i, ea, batu, batl);

        if (mask && ((ea & mask) == (batu & BATU32_BEPI))) {
            hwaddr raddr = (batl & mask) | (ea & ~mask);

            if (unlikely(env->mmu_model == POWERPC_MMU_601)) {
                *prot = hash32_bat_601_prot(env, batu, batl);
            } else {
                *prot = hash32_bat_prot(env, batu, batl);
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

static int ppc_hash32_direct_store(CPUPPCState *env, target_ulong sr,
                                   target_ulong eaddr, int rwx,
                                   hwaddr *raddr, int *prot)
{
    int key = !!(msr_pr ? (sr & SR32_KP) : (sr & SR32_KS));

    LOG_MMU("direct store...\n");

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
        return -4;
    }

    switch (env->access_type) {
    case ACCESS_INT:
        /* Integer load/store : only access allowed */
        break;
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
        *raddr = eaddr;
        return 0;
    case ACCESS_EXT:
        /* eciwx or ecowx */
        return -4;
    default:
        qemu_log("ERROR: instruction should not need "
                 "address translation\n");
        return -4;
    }
    if ((rwx == 1 || key != 1) && (rwx == 0 || key != 0)) {
        *raddr = eaddr;
        return 2;
    } else {
        return -2;
    }
}

static int ppc_hash32_pte_update_flags(struct mmu_ctx_hash32 *ctx, uint32_t *pte1p,
                                       int ret, int rwx)
{
    int store = 0;

    /* Update page flags */
    if (!(*pte1p & HPTE32_R_R)) {
        /* Update accessed flag */
        *pte1p |= HPTE32_R_R;
        store = 1;
    }
    if (!(*pte1p & HPTE32_R_C)) {
        if (rwx == 1 && ret == 0) {
            /* Update changed flag */
            *pte1p |= HPTE32_R_C;
            store = 1;
        } else {
            /* Force page fault for first write access */
            ctx->prot &= ~PAGE_WRITE;
        }
    }

    return store;
}

hwaddr get_pteg_offset32(CPUPPCState *env, hwaddr hash)
{
    return (hash * HASH_PTEG_SIZE_32) & env->htab_mask;
}

static hwaddr ppc_hash32_pteg_search(CPUPPCState *env, hwaddr pteg_off,
                                     bool secondary, target_ulong ptem,
                                     ppc_hash_pte32_t *pte)
{
    hwaddr pte_offset = pteg_off;
    target_ulong pte0, pte1;
    int i;

    for (i = 0; i < HPTES_PER_GROUP; i++) {
        pte0 = ppc_hash32_load_hpte0(env, pte_offset);
        pte1 = ppc_hash32_load_hpte1(env, pte_offset);

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

static hwaddr ppc_hash32_htab_lookup(CPUPPCState *env,
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
    LOG_MMU("htab_base " TARGET_FMT_plx " htab_mask " TARGET_FMT_plx
            " hash " TARGET_FMT_plx "\n",
            env->htab_base, env->htab_mask, hash);

    /* Primary PTEG lookup */
    LOG_MMU("0 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
            " vsid=%" PRIx32 " ptem=%" PRIx32
            " hash=" TARGET_FMT_plx "\n",
            env->htab_base, env->htab_mask, vsid, ptem, hash);
    pteg_off = get_pteg_offset32(env, hash);
    pte_offset = ppc_hash32_pteg_search(env, pteg_off, 0, ptem, pte);
    if (pte_offset == -1) {
        /* Secondary PTEG lookup */
        LOG_MMU("1 htab=" TARGET_FMT_plx "/" TARGET_FMT_plx
                " vsid=%" PRIx32 " api=%" PRIx32
                " hash=" TARGET_FMT_plx "\n", env->htab_base,
                env->htab_mask, vsid, ptem, ~hash);
        pteg_off = get_pteg_offset32(env, ~hash);
        pte_offset = ppc_hash32_pteg_search(env, pteg_off, 1, ptem, pte);
    }

    return pte_offset;
}

static int ppc_hash32_translate(CPUPPCState *env, struct mmu_ctx_hash32 *ctx,
                                target_ulong eaddr, int rwx)
{
    int ret;
    target_ulong sr;
    hwaddr pte_offset;
    ppc_hash_pte32_t pte;

    assert((rwx == 0) || (rwx == 1) || (rwx == 2));

    /* 1. Handle real mode accesses */
    if (((rwx == 2) && (msr_ir == 0)) || ((rwx != 2) && (msr_dr == 0))) {
        /* Translation is off */
        ctx->raddr = eaddr;
        ctx->prot = PAGE_READ | PAGE_EXEC | PAGE_WRITE;
        return 0;
    }

    /* 2. Check Block Address Translation entries (BATs) */
    if (env->nb_BATs != 0) {
        ctx->raddr = ppc_hash32_bat_lookup(env, eaddr, rwx, &ctx->prot);
        if (ctx->raddr != -1) {
            ret = ppc_hash32_check_prot(ctx->prot, rwx);
            if (ret == 0) {
                return 0;
            }
        }
    }

    /* 3. Look up the Segment Register */
    sr = env->sr[eaddr >> 28];

    /* 4. Handle direct store segments */
    if (sr & SR32_T) {
        return ppc_hash32_direct_store(env, sr, eaddr, rwx,
                                       &ctx->raddr, &ctx->prot);
    }

    /* 5. Check for segment level no-execute violation */
    ctx->nx = !!(sr & SR32_NX);
    if ((rwx == 2) && ctx->nx) {
        return -3;
    }

    /* 6. Locate the PTE in the hash table */
    pte_offset = ppc_hash32_htab_lookup(env, sr, eaddr, &pte);
    if (pte_offset == -1) {
        return -1;
    }
    LOG_MMU("found PTE at offset %08" HWADDR_PRIx "\n", pte_offset);

    /* 7. Check access permissions */
    ctx->key = (((sr & SR32_KP) && (msr_pr != 0)) ||
                ((sr & SR32_KS) && (msr_pr == 0))) ? 1 : 0;

    int access, pp;

    pp = pte.pte1 & HPTE32_R_PP;
    /* Compute access rights */
    access = ppc_hash32_pp_check(ctx->key, pp, ctx->nx);
    /* Keep the matching PTE informations */
    ctx->raddr = pte.pte1;
    ctx->prot = access;
    ret = ppc_hash32_check_prot(ctx->prot, rwx);
    if (ret == 0) {
        /* Access granted */
        LOG_MMU("PTE access granted !\n");
    } else {
        /* Access right violation */
        LOG_MMU("PTE access rejected\n");
    }

    /* Update page flags */
    if (ppc_hash32_pte_update_flags(ctx, &pte.pte1, ret, rwx) == 1) {
        ppc_hash32_store_hpte1(env, pte_offset, pte.pte1);
    }

    return ret;
}

hwaddr ppc_hash32_get_phys_page_debug(CPUPPCState *env, target_ulong addr)
{
    struct mmu_ctx_hash32 ctx;

    /* FIXME: Will not behave sanely for direct store segments, but
     * they're almost never used */
    if (unlikely(ppc_hash32_translate(env, &ctx, addr, 0)
                 != 0)) {
        return -1;
    }

    return ctx.raddr & TARGET_PAGE_MASK;
}

int ppc_hash32_handle_mmu_fault(CPUPPCState *env, target_ulong address, int rwx,
                                int mmu_idx)
{
    struct mmu_ctx_hash32 ctx;
    int ret = 0;

    ret = ppc_hash32_translate(env, &ctx, address, rwx);
    if (ret == 0) {
        tlb_set_page(env, address & TARGET_PAGE_MASK,
                     ctx.raddr & TARGET_PAGE_MASK, ctx.prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        ret = 0;
    } else if (ret < 0) {
        LOG_MMU_STATE(env);
        if (rwx == 2) {
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
                if (rwx == 1) {
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
                if (rwx == 1) {
                    env->spr[SPR_DSISR] = 0x0A000000;
                } else {
                    env->spr[SPR_DSISR] = 0x08000000;
                }
                break;
            case -4:
                /* Direct store exception */
                switch (env->access_type) {
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
                    if (rwx == 1) {
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
                    if (rwx == 1) {
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
