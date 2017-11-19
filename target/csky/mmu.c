/*
 * CSKY mmu
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
#include "translate.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"

#if !defined(CONFIG_USER_ONLY)
/**************************** MMU ***************************/
static int get_page_bits(CPUCSKYState *env)
{
    switch ((env->mmu.mpr >> 13) & 0xfff) {
    case 0x0:
        return 12;
    case 0x3:
        return 14;
    case 0xf:
        return 16;
    case 0x3f:
        return 18;
    case 0xff:
        return 20;
    case 0x3ff:
        return 22;
    case 0xfff:
        return 24;
    default:
        qemu_log_mask(CPU_LOG_MMU, "CSKY CPU does not support "
                      "PageMask 0x%x!\n", env->mmu.mpr);
        return 0;
    }
}

void helper_ttlbinv_all(CPUCSKYState *env)
{
    CPUState *cs = CPU(csky_env_get_cpu(env));
    memset(env->tlb_context->t_tlb, 0,
           sizeof(struct csky_tlb_t) * CSKY_TLB_MAX);
    memset(env->tlb_context->nt_tlb, 0,
           sizeof(struct csky_tlb_t) * CSKY_TLB_MAX);
    tlb_flush(cs);
}

void helper_tlbinv_all(CPUCSKYState *env)
{
    CPUState *cs = CPU(csky_env_get_cpu(env));
    memset(env->tlb_context->tlb, 0, sizeof(struct csky_tlb_t) * CSKY_TLB_MAX);
    tlb_flush(cs);
}

void helper_tlbinv(CPUCSKYState *env)
{
    csky_tlb_t *ptlb;
    uint8_t asid;
    int i;
    CPUState *cs = CPU(csky_env_get_cpu(env));

    asid = env->mmu.meh & 0xff;
    ptlb = &env->tlb_context->tlb[0];

    for (i = 0; i < CSKY_TLB_MAX; ++i) {
        if (ptlb->ASID == asid) {
            ptlb->V0 = 0;
            ptlb->V1 = 0;
            int j;
            for (j = ptlb->VPN; j <= (ptlb->VPN | env->mmu.mpr | 0x1000);
                                                             j += 0x1000) {
                tlb_flush_page(cs, j);
            }
        }
        ptlb++;
    }
}

void csky_tlbwi(CPUCSKYState *env)
{
    csky_tlb_t *ptlb;
    CPUState *cs = CPU(csky_env_get_cpu(env));

    ptlb = &env->tlb_context->tlb[env->mmu.mir & 0x7f];

    ptlb->VPN   = env->mmu.meh & ~(env->mmu.mpr | 0x1fff);
    ptlb->ASID  = env->mmu.meh & 0xff;
    ptlb->G     = env->mmu.mel0 & env->mmu.mel1 & 0x1;
    ptlb->C0    = (env->mmu.mel0 >> 3) & 0x7;
    ptlb->C1    = (env->mmu.mel1 >> 3) & 0x7;
    ptlb->V0    = (env->mmu.mel0 >> 1) & 0x1;
    ptlb->V1    = (env->mmu.mel1 >> 1) & 0x1;
    ptlb->D0    = (env->mmu.mel0 >> 2) & 0x1;
    ptlb->D1    = (env->mmu.mel1 >> 2) & 0x1;
#if !defined(TARGET_CSKYV2)
    ptlb->PFN[0] = (env->mmu.mel0 << 6) & ~((env->mmu.mpr >> 1) | 0xfff);
    ptlb->PFN[1] = (env->mmu.mel1 << 6) & ~((env->mmu.mpr >> 1) | 0xfff);
#else
    ptlb->PFN[0] = env->mmu.mel0 & ~((env->mmu.mpr >> 1) | 0xfff);
    ptlb->PFN[1] = env->mmu.mel1 & ~((env->mmu.mpr >> 1) | 0xfff);
#endif
#if !defined(TARGET_CSKYV2)
    ptlb->PageMask = env->mmu.mpr;
#endif

    int j;
    for (j = ptlb->VPN; j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
        tlb_flush_page(cs, j);
    }
}

void csky_tlbwr(CPUCSKYState *env)
{
    csky_tlb_t *ptlb;
    uint32_t index;
    CPUState *cs = CPU(csky_env_get_cpu(env));

    /* index = robin,VPN[18:13]   page size: 4KB*/
    /* index = robin,VPN[20:15]   page size: 16KB*/
    /* index = robin,VPN[22:17]   page size: 64KB*/
    /* ... */
    index = (env->mmu.meh >> (get_page_bits(env) + 1)) & 0x3f;
    if (env->tlb_context->round_robin[index]) {
        env->tlb_context->round_robin[index] = 0;
        index = index + 64;
    } else {
        env->tlb_context->round_robin[index] = 1;
    }
    ptlb =  &env->tlb_context->tlb[index];

    ptlb->VPN   = env->mmu.meh & ~(env->mmu.mpr | 0x1fff);
    ptlb->ASID  = env->mmu.meh & 0xff;
    ptlb->G     = env->mmu.mel0 & env->mmu.mel1 & 0x1;
    ptlb->C0    = (env->mmu.mel0 >> 3) & 0x7;
    ptlb->C1    = (env->mmu.mel1 >> 3) & 0x7;
    ptlb->V0    = (env->mmu.mel0 >> 1) & 0x1;
    ptlb->V1    = (env->mmu.mel1 >> 1) & 0x1;
    ptlb->D0    = (env->mmu.mel0 >> 2) & 0x1;
    ptlb->D1    = (env->mmu.mel1 >> 2) & 0x1;
#if !defined(TARGET_CSKYV2)
    ptlb->PFN[0] = (env->mmu.mel0 << 6) & ~((env->mmu.mpr >> 1) | 0xfff);
    ptlb->PFN[1] = (env->mmu.mel1 << 6) & ~((env->mmu.mpr >> 1) | 0xfff);
#else
    ptlb->PFN[0] = env->mmu.mel0 & ~((env->mmu.mpr >> 1) | 0xfff);
    ptlb->PFN[1] = env->mmu.mel1 & ~((env->mmu.mpr >> 1) | 0xfff);
#endif
#if !defined(TARGET_CSKYV2)
    ptlb->PageMask = env->mmu.mpr;
#endif

    int j;
    for (j = ptlb->VPN; j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
        tlb_flush_page(cs, j);
    }
}

void csky_tlbp(CPUCSKYState *env)
{
    csky_tlb_t *ptlb;
    uint32_t index;

    /* index = robin,VPN[18:13]*/
    index = (env->mmu.meh >> (get_page_bits(env) + 1)) & 0x3f;
    ptlb =  &env->tlb_context->tlb[index];

    if (ptlb->VPN == (env->mmu.meh & ~(env->mmu.mpr | 0x1fff))
            && ptlb->ASID == (env->mmu.meh & 0xff)) {
        env->mmu.mir = index;
        return;
    }

    index += 64;
    ptlb =  &env->tlb_context->tlb[index];
    if (ptlb->VPN == (env->mmu.meh & ~(env->mmu.mpr | 0x1fff))
            && ptlb->ASID == (env->mmu.meh & 0xff)) {
        env->mmu.mir = index;
        return;
    }

    /* if not found, set P bit */
    env->mmu.mir |= (1 << 31);
}

void csky_tlbr(CPUCSKYState *env)
{
    /* FIXME */
    csky_tlb_t *ptlb;

    ptlb = &env->tlb_context->tlb[env->mmu.mir & 0x7f];

    env->mmu.meh = ptlb->VPN | ptlb->ASID;
#if !defined(TARGET_CSKYV2)
    env->mmu.mel0 = ptlb->PFN[0] >> 6 | ptlb->C0 << 3
                    | ptlb->D0 << 2 | ptlb->V0 << 1 | ptlb->G;

    env->mmu.mel1 = ptlb->PFN[1] >> 6 | ptlb->C1 << 3
                    | ptlb->D1 << 2 | ptlb->V1 << 1 | ptlb->G;
#else
    env->mmu.mel0 = ptlb->PFN[0]  | ptlb->C0 << 3
                    | ptlb->D0 << 2 | ptlb->V0 << 1 | ptlb->G;

    env->mmu.mel1 = ptlb->PFN[1]  | ptlb->C1 << 3
                    | ptlb->D1 << 2 | ptlb->V1 << 1 | ptlb->G;
#endif
#if !defined(TARGET_CSKYV2)
    env->mmu.mpr = ptlb->PageMask;
#endif
}

/* ----------------------------- */

enum {
    TLBRET_ABORT = -5,
    TLBRET_DIRTY = -4,
    TLBRET_INVALID = -3,
    TLBRET_NOMATCH = -2,
    TLBRET_BADADDR = -1,
    TLBRET_MATCH = 0
};

void tlb_fill(CPUState *cs, target_ulong addr, MMUAccessType access_type,
              int mmu_idx, uintptr_t retaddr)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;
    int ret;

    ret = csky_cpu_handle_mmu_fault(cs, addr, access_type, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            cpu_restore_state(cs, retaddr);
        }
        helper_exception(env, cs->exception_index);
    }
}

int csky_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                              int mmu_idx)
{
    hwaddr physical;
    int prot;
    int ret = 0;
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    rw &= 1;
    ret = env->tlb_context->get_physical_address(env, &physical,
                                                 &prot, address, rw);

    if (ret == TLBRET_MATCH) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                physical & TARGET_PAGE_MASK, prot | PAGE_EXEC,
                mmu_idx, TARGET_PAGE_SIZE);
    } else if (ret < 0) {
        /* Set the BADVPN Register */
        env->mmu.meh &= ~(0xfffff << 12);
        env->mmu.meh |= address & ~((1 << get_page_bits(env)) - 1);

        switch (ret) {
        case TLBRET_ABORT:
            cs->exception_index = EXCP_CSKY_DATA_ABORT;
            break;
        case TLBRET_DIRTY:
            cs->exception_index = EXCP_CSKY_TLB_MODIFY;
            break;
        case TLBRET_INVALID:
            if (rw) {
                cs->exception_index = EXCP_CSKY_TLB_WRITE_INVALID;
            } else {
                cs->exception_index = EXCP_CSKY_TLB_READ_INVALID;
            }
            break;
        case TLBRET_NOMATCH:
            cs->exception_index = EXCP_CSKY_TLB_UNMATCH;
            break;
        case TLBRET_BADADDR:
            cs->exception_index = EXCP_CSKY_DATA_ABORT;
            break;
        default:
            cs->exception_index = EXCP_CSKY_DATA_ABORT;
            break;
        }
        ret = 1;
    }
    return ret;
}

int mmu_get_physical_address(struct CPUCSKYState *env,
                             hwaddr *physical,
                             int *prot, target_ulong address, int rw)
{
    uint32_t index;
    uint32_t super_mode;
    csky_tlb_t *ptlb;
    uint8_t odd;
    uint8_t ASID;
    int page_bits;
    CPUState *cs = CPU(csky_env_get_cpu(env));

    page_bits = get_page_bits(env);

    super_mode = env->psr_s;

    if (address >= 0x80000000 && address < 0xa0000000) {
        if (super_mode) {
#if !defined(TARGET_CSKYV2)
            *physical = address - 0x80000000;
            *prot = PAGE_READ | PAGE_WRITE;
            return TLBRET_MATCH;
#else
            if (!(env->mmu.msa0 & 0x2)) {
                return TLBRET_INVALID;
            }
            if (rw == 0 || env->mmu.msa0 & 0x4) {
                *physical = address - 0x80000000 + (env->mmu.msa0 & 0xe0000000);
                *prot = PAGE_READ;
                if (env->mmu.msa0 & 0x4) {
                    *prot |= PAGE_WRITE;
                }
                return TLBRET_MATCH;
            }
            return TLBRET_DIRTY;
#endif
        }
        return TLBRET_BADADDR;
    } else if (address >= 0xa0000000 && address < 0xc0000000) {
        if (super_mode) {
#if !defined(TARGET_CSKYV2)
            *physical = address - 0xa0000000;
            *prot = PAGE_READ | PAGE_WRITE;
            return TLBRET_MATCH;
#else
            if (!(env->mmu.msa1 & 0x2)) {
                return TLBRET_INVALID;
            }
            if (rw == 0 || env->mmu.msa1 & 0x4) {
                *physical = address - 0xa0000000 + (env->mmu.msa1 & 0xe0000000);
                *prot = PAGE_READ;
                if (env->mmu.msa1 & 0x4) {
                    *prot |= PAGE_WRITE;
                }
                return TLBRET_MATCH;
            }
            return TLBRET_DIRTY;
#endif
        }
        return TLBRET_BADADDR;
    } else if (address >= 0xc0000000) {
        if (super_mode) {
            goto do_map;
        }
        return TLBRET_BADADDR;
    }

do_map:
    /* MMU is enable */
    ASID = env->mmu.meh & 0xff;
    odd = (address >> page_bits) & 0x1;
    /* */
    index = (address >> (page_bits + 1)) & 0x3f;
    ptlb =  &env->tlb_context->tlb[index];

    if (((address & ~(env->mmu.mpr | 0x1fff)) == 0) &&
        !(odd ? ptlb->V1 : ptlb->V0)) {
        goto hard_refill;
    }

    if ((ptlb->G == 1 || ptlb->ASID == ASID) &&
       ptlb->VPN == (address & ~(env->mmu.mpr | 0x1fff))) {
        if (!(odd ? ptlb->V1 : ptlb->V0)) {
            return TLBRET_INVALID;
        }
        if (rw == 0 || (odd ? ptlb->D1 : ptlb->D0)) {
            *physical = ptlb->PFN[odd] | (address & ((1 << page_bits) - 1));
            *prot = PAGE_READ;
            if ((odd ? ptlb->D1 : ptlb->D0)) {
                *prot |= PAGE_WRITE;
            }
            return TLBRET_MATCH;
        }
        return TLBRET_DIRTY;
    }

    index += 64;
    ptlb = &env->tlb_context->tlb[index];

    if ((ptlb->G == 1 || ptlb->ASID == ASID) &&
        ptlb->VPN == (address & ~(env->mmu.mpr | 0x1fff))) {
        if (!(odd ? ptlb->V1 : ptlb->V0)) {
            return TLBRET_INVALID;
        }
        if (rw == 0 || (odd ? ptlb->D1 : ptlb->D0)) {
            *physical = ptlb->PFN[odd] | (address & ((1 << page_bits) - 1));
            *prot = PAGE_READ;
            if ((odd ? ptlb->D1 : ptlb->D0)) {
                *prot |= PAGE_WRITE;
            }
            return TLBRET_MATCH;
        }
        return TLBRET_DIRTY;
    }
    /* FIXME add cskyv2 hard_tlb_refill*/
hard_refill:
    if (((env->mmu.mpr >> 13) & 0xfff) == 0x0) {
        uint32_t tlb_phy_addr_0, tlb_phy_addr_1;
        /* Get current pgd table base */
        tlb_phy_addr_0 = env->mmu.mpar & (~0xfff);

#define PGDIR_SHIFT    22
#define PTE_INDX_SHIFT  10

        tlb_phy_addr_0 += (address >> PGDIR_SHIFT) << 2;
        /* Get pte table base */
        tlb_phy_addr_0 = ldl_phys(cs->as, tlb_phy_addr_0);

        tlb_phy_addr_0 += (address >> PTE_INDX_SHIFT) & 0xff8;

        tlb_phy_addr_1 = ldl_phys(cs->as, tlb_phy_addr_0 + 4);

        tlb_phy_addr_0 = ldl_phys(cs->as, tlb_phy_addr_0);

        odd = (address >> 12) & 0x1;
        index = (address >> 13) & 0x3f;

        if (env->tlb_context->round_robin[index]) {
            env->tlb_context->round_robin[index] = 0;
            index = index + 64;
        } else {
            env->tlb_context->round_robin[index] = 1;
        }
        ptlb = &env->tlb_context->tlb[index];

        ptlb->VPN   = address & ~0x1fff;
        ptlb->ASID  = env->mmu.meh & 0xff;
#if !defined(TARGET_CSKYV2)
        ptlb->G     = (tlb_phy_addr_0 >> 6) & (tlb_phy_addr_1 >> 6) & 0X1;
        ptlb->C0    = (tlb_phy_addr_0 >> 9) & 0x7;
        ptlb->C1    = (tlb_phy_addr_1 >> 9) & 0x7;
        ptlb->V0    = (tlb_phy_addr_0 >> 7) & 0x1;
        ptlb->V1    = (tlb_phy_addr_1 >> 7) & 0x1;
        ptlb->D0    = (tlb_phy_addr_0 >> 8) & 0x1;
        ptlb->D1    = (tlb_phy_addr_1 >> 8) & 0x1;
#else
        ptlb->G     = tlb_phy_addr_0 & tlb_phy_addr_1 & 0X1;
        ptlb->C0    = (tlb_phy_addr_0 >> 3) & 0x7;
        ptlb->C1    = (tlb_phy_addr_1 >> 3) & 0x7;
        ptlb->V0    = (tlb_phy_addr_0 >> 1) & 0x1;
        ptlb->V1    = (tlb_phy_addr_1 >> 1) & 0x1;
        ptlb->D0    = (tlb_phy_addr_0 >> 2) & 0x1;
        ptlb->D1    = (tlb_phy_addr_1 >> 2) & 0x1;
#endif

        ptlb->PFN[0] = tlb_phy_addr_0 & ~0xfff;
        ptlb->PFN[1] = tlb_phy_addr_1 & ~0xfff;

        ptlb->PageMask = env->mmu.mpr;

        if (!(odd ? ptlb->V1 : ptlb->V0)) {
            return TLBRET_INVALID;
        }
        if (rw == 0 || (odd ? ptlb->D1 : ptlb->D0)) {
            *physical = ptlb->PFN[odd] | (address & 0xfff);
            *prot = PAGE_READ;
            if ((odd ? ptlb->D1 : ptlb->D0)) {
                *prot |= PAGE_WRITE;
            }
            return TLBRET_MATCH;
        }
        return TLBRET_DIRTY;
    }

    return TLBRET_NOMATCH;
}

int nommu_get_physical_address(struct CPUCSKYState *env,
                               hwaddr *physical,
                               int *prot, target_ulong address, int rw)
{
    *physical = address;
    *prot = PAGE_READ | PAGE_WRITE;
    return TLBRET_MATCH;
}

int mgu_get_physical_address(struct CPUCSKYState *env,
                             hwaddr *physical,
                             int *prot, target_ulong address, int rw)
{
    int i, t, base, size;
    int j = 0;

    for (i = 7; i >= 0; i--) {
        if (env->cp0.pacr[i] & 0x1) {
            if (((env->cp0.pacr[i] >> 1) & 0x1f) == 0x1f) {
                size = 0xffffffff;
            } else {
                size = (1 << (((env->cp0.pacr[i] >> 1) & 0x1f) + 1)) - 1;
            }
            base = env->cp0.pacr[i] & ~size; /* page align */
            if ((base <= address) && (address <= (base + size))) {
                t = (env->cp0.capr >> (8 + i * 2)) & 0x3;
                if (env->features & ABIV2_TEE) {
                    /* if the area config Trusted, but now world is Non-Trust
                     * can not access the area. */
                    if (!env->psr_t && (env->cp0.capr & (1 << (i + 24)))) {
                        j = 2;
                        break;
                    }
                }
                if (env->cp0.psr & 0x80000000) {
                    if (t == 0) {
                        j = 2;
                        break;
                    } else {
                        j = 0;
                        break;
                    }
                } else {
                    if (t < 2) {
                        j = 2;
                        break;
                    } else if (t == 2) {
                        if (rw == 0) {
                            j = 1;
                            break;
                        } else {
                            j = 2;
                            break;
                        }
                    } else {
                        j = 0;
                        break;
                    }
                }
            } else {
                j = 2;
                continue;
            }
        }
    }
    if (j == 0) {
        *physical = address;
        *prot = PAGE_READ | PAGE_WRITE;
        return TLBRET_MATCH;
    } else if (j == 1) {
        *physical = address;
        *prot = PAGE_READ;
        return TLBRET_MATCH;
    } else {
        return TLBRET_ABORT;
    }
}

#endif
