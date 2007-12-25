/*
 *  sparc helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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

/* Sparc MMU emulation */

/* thread support */

spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void cpu_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void cpu_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

#if defined(CONFIG_USER_ONLY)

int cpu_sparc_handle_mmu_fault(CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
    if (rw & 2)
        env->exception_index = TT_TFAULT;
    else
        env->exception_index = TT_DFAULT;
    return 1;
}

#else

#ifndef TARGET_SPARC64
/*
 * Sparc V8 Reference MMU (SRMMU)
 */
static const int access_table[8][8] = {
    { 0, 0, 0, 0, 2, 0, 3, 3 },
    { 0, 0, 0, 0, 2, 0, 0, 0 },
    { 2, 2, 0, 0, 0, 2, 3, 3 },
    { 2, 2, 0, 0, 0, 2, 0, 0 },
    { 2, 0, 2, 0, 2, 2, 3, 3 },
    { 2, 0, 2, 0, 2, 0, 2, 0 },
    { 2, 2, 2, 0, 2, 2, 3, 3 },
    { 2, 2, 2, 0, 2, 2, 2, 0 }
};

static const int perm_table[2][8] = {
    {
        PAGE_READ,
        PAGE_READ | PAGE_WRITE,
        PAGE_READ | PAGE_EXEC,
        PAGE_READ | PAGE_WRITE | PAGE_EXEC,
        PAGE_EXEC,
        PAGE_READ | PAGE_WRITE,
        PAGE_READ | PAGE_EXEC,
        PAGE_READ | PAGE_WRITE | PAGE_EXEC
    },
    {
        PAGE_READ,
        PAGE_READ | PAGE_WRITE,
        PAGE_READ | PAGE_EXEC,
        PAGE_READ | PAGE_WRITE | PAGE_EXEC,
        PAGE_EXEC,
        PAGE_READ,
        0,
        0,
    }
};

int get_physical_address (CPUState *env, target_phys_addr_t *physical, int *prot,
                          int *access_index, target_ulong address, int rw,
                          int mmu_idx)
{
    int access_perms = 0;
    target_phys_addr_t pde_ptr;
    uint32_t pde;
    target_ulong virt_addr;
    int error_code = 0, is_dirty, is_user;
    unsigned long page_offset;

    is_user = mmu_idx == MMU_USER_IDX;
    virt_addr = address & TARGET_PAGE_MASK;

    if ((env->mmuregs[0] & MMU_E) == 0) { /* MMU disabled */
        // Boot mode: instruction fetches are taken from PROM
        if (rw == 2 && (env->mmuregs[0] & env->mmu_bm)) {
            *physical = env->prom_addr | (address & 0x7ffffULL);
            *prot = PAGE_READ | PAGE_EXEC;
            return 0;
        }
        *physical = address;
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return 0;
    }

    *access_index = ((rw & 1) << 2) | (rw & 2) | (is_user? 0 : 1);
    *physical = 0xffffffffffff0000ULL;

    /* SPARC reference MMU table walk: Context table->L1->L2->PTE */
    /* Context base + context number */
    pde_ptr = ((env->mmuregs[1] & ~63)<< 4) + (env->mmuregs[2] << 2);
    pde = ldl_phys(pde_ptr);

    /* Ctx pde */
    switch (pde & PTE_ENTRYTYPE_MASK) {
    default:
    case 0: /* Invalid */
        return 1 << 2;
    case 2: /* L0 PTE, maybe should not happen? */
    case 3: /* Reserved */
        return 4 << 2;
    case 1: /* L0 PDE */
        pde_ptr = ((address >> 22) & ~3) + ((pde & ~3) << 4);
        pde = ldl_phys(pde_ptr);

        switch (pde & PTE_ENTRYTYPE_MASK) {
        default:
        case 0: /* Invalid */
            return (1 << 8) | (1 << 2);
        case 3: /* Reserved */
            return (1 << 8) | (4 << 2);
        case 1: /* L1 PDE */
            pde_ptr = ((address & 0xfc0000) >> 16) + ((pde & ~3) << 4);
            pde = ldl_phys(pde_ptr);

            switch (pde & PTE_ENTRYTYPE_MASK) {
            default:
            case 0: /* Invalid */
                return (2 << 8) | (1 << 2);
            case 3: /* Reserved */
                return (2 << 8) | (4 << 2);
            case 1: /* L2 PDE */
                pde_ptr = ((address & 0x3f000) >> 10) + ((pde & ~3) << 4);
                pde = ldl_phys(pde_ptr);

                switch (pde & PTE_ENTRYTYPE_MASK) {
                default:
                case 0: /* Invalid */
                    return (3 << 8) | (1 << 2);
                case 1: /* PDE, should not happen */
                case 3: /* Reserved */
                    return (3 << 8) | (4 << 2);
                case 2: /* L3 PTE */
                    virt_addr = address & TARGET_PAGE_MASK;
                    page_offset = (address & TARGET_PAGE_MASK) & (TARGET_PAGE_SIZE - 1);
                }
                break;
            case 2: /* L2 PTE */
                virt_addr = address & ~0x3ffff;
                page_offset = address & 0x3ffff;
            }
            break;
        case 2: /* L1 PTE */
            virt_addr = address & ~0xffffff;
            page_offset = address & 0xffffff;
        }
    }

    /* update page modified and dirty bits */
    is_dirty = (rw & 1) && !(pde & PG_MODIFIED_MASK);
    if (!(pde & PG_ACCESSED_MASK) || is_dirty) {
        pde |= PG_ACCESSED_MASK;
        if (is_dirty)
            pde |= PG_MODIFIED_MASK;
        stl_phys_notdirty(pde_ptr, pde);
    }
    /* check access */
    access_perms = (pde & PTE_ACCESS_MASK) >> PTE_ACCESS_SHIFT;
    error_code = access_table[*access_index][access_perms];
    if (error_code && !((env->mmuregs[0] & MMU_NF) && is_user))
        return error_code;

    /* the page can be put in the TLB */
    *prot = perm_table[is_user][access_perms];
    if (!(pde & PG_MODIFIED_MASK)) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
        *prot &= ~PAGE_WRITE;
    }

    /* Even if large ptes, we map only one 4KB page in the cache to
       avoid filling it too fast */
    *physical = ((target_phys_addr_t)(pde & PTE_ADDR_MASK) << 4) + page_offset;
    return error_code;
}

/* Perform address translation */
int cpu_sparc_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu)
{
    target_phys_addr_t paddr;
    target_ulong vaddr;
    int error_code = 0, prot, ret = 0, access_index;

    error_code = get_physical_address(env, &paddr, &prot, &access_index, address, rw, mmu_idx);
    if (error_code == 0) {
        vaddr = address & TARGET_PAGE_MASK;
        paddr &= TARGET_PAGE_MASK;
#ifdef DEBUG_MMU
        printf("Translate at " TARGET_FMT_lx " -> " TARGET_FMT_plx ", vaddr "
               TARGET_FMT_lx "\n", address, paddr, vaddr);
#endif
        ret = tlb_set_page_exec(env, vaddr, paddr, prot, mmu_idx, is_softmmu);
        return ret;
    }

    if (env->mmuregs[3]) /* Fault status register */
        env->mmuregs[3] = 1; /* overflow (not read before another fault) */
    env->mmuregs[3] |= (access_index << 5) | error_code | 2;
    env->mmuregs[4] = address; /* Fault address register */

    if ((env->mmuregs[0] & MMU_NF) || env->psret == 0)  {
        // No fault mode: if a mapping is available, just override
        // permissions. If no mapping is available, redirect accesses to
        // neverland. Fake/overridden mappings will be flushed when
        // switching to normal mode.
        vaddr = address & TARGET_PAGE_MASK;
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        ret = tlb_set_page_exec(env, vaddr, paddr, prot, mmu_idx, is_softmmu);
        return ret;
    } else {
        if (rw & 2)
            env->exception_index = TT_TFAULT;
        else
            env->exception_index = TT_DFAULT;
        return 1;
    }
}

target_ulong mmu_probe(CPUState *env, target_ulong address, int mmulev)
{
    target_phys_addr_t pde_ptr;
    uint32_t pde;

    /* Context base + context number */
    pde_ptr = (target_phys_addr_t)(env->mmuregs[1] << 4) +
        (env->mmuregs[2] << 2);
    pde = ldl_phys(pde_ptr);

    switch (pde & PTE_ENTRYTYPE_MASK) {
    default:
    case 0: /* Invalid */
    case 2: /* PTE, maybe should not happen? */
    case 3: /* Reserved */
        return 0;
    case 1: /* L1 PDE */
        if (mmulev == 3)
            return pde;
        pde_ptr = ((address >> 22) & ~3) + ((pde & ~3) << 4);
        pde = ldl_phys(pde_ptr);

        switch (pde & PTE_ENTRYTYPE_MASK) {
        default:
        case 0: /* Invalid */
        case 3: /* Reserved */
            return 0;
        case 2: /* L1 PTE */
            return pde;
        case 1: /* L2 PDE */
            if (mmulev == 2)
                return pde;
            pde_ptr = ((address & 0xfc0000) >> 16) + ((pde & ~3) << 4);
            pde = ldl_phys(pde_ptr);

            switch (pde & PTE_ENTRYTYPE_MASK) {
            default:
            case 0: /* Invalid */
            case 3: /* Reserved */
                return 0;
            case 2: /* L2 PTE */
                return pde;
            case 1: /* L3 PDE */
                if (mmulev == 1)
                    return pde;
                pde_ptr = ((address & 0x3f000) >> 10) + ((pde & ~3) << 4);
                pde = ldl_phys(pde_ptr);

                switch (pde & PTE_ENTRYTYPE_MASK) {
                default:
                case 0: /* Invalid */
                case 1: /* PDE, should not happen */
                case 3: /* Reserved */
                    return 0;
                case 2: /* L3 PTE */
                    return pde;
                }
            }
        }
    }
    return 0;
}

#ifdef DEBUG_MMU
void dump_mmu(CPUState *env)
{
    target_ulong va, va1, va2;
    unsigned int n, m, o;
    target_phys_addr_t pde_ptr, pa;
    uint32_t pde;

    printf("MMU dump:\n");
    pde_ptr = (env->mmuregs[1] << 4) + (env->mmuregs[2] << 2);
    pde = ldl_phys(pde_ptr);
    printf("Root ptr: " TARGET_FMT_plx ", ctx: %d\n",
           (target_phys_addr_t)env->mmuregs[1] << 4, env->mmuregs[2]);
    for (n = 0, va = 0; n < 256; n++, va += 16 * 1024 * 1024) {
        pde = mmu_probe(env, va, 2);
        if (pde) {
            pa = cpu_get_phys_page_debug(env, va);
            printf("VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_plx
                   " PDE: " TARGET_FMT_lx "\n", va, pa, pde);
            for (m = 0, va1 = va; m < 64; m++, va1 += 256 * 1024) {
                pde = mmu_probe(env, va1, 1);
                if (pde) {
                    pa = cpu_get_phys_page_debug(env, va1);
                    printf(" VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_plx
                           " PDE: " TARGET_FMT_lx "\n", va1, pa, pde);
                    for (o = 0, va2 = va1; o < 64; o++, va2 += 4 * 1024) {
                        pde = mmu_probe(env, va2, 0);
                        if (pde) {
                            pa = cpu_get_phys_page_debug(env, va2);
                            printf("  VA: " TARGET_FMT_lx ", PA: "
                                   TARGET_FMT_plx " PTE: " TARGET_FMT_lx "\n",
                                   va2, pa, pde);
                        }
                    }
                }
            }
        }
    }
    printf("MMU dump ends\n");
}
#endif /* DEBUG_MMU */

#else /* !TARGET_SPARC64 */
/*
 * UltraSparc IIi I/DMMUs
 */
static int get_physical_address_data(CPUState *env, target_phys_addr_t *physical, int *prot,
                          int *access_index, target_ulong address, int rw,
                          int is_user)
{
    target_ulong mask;
    unsigned int i;

    if ((env->lsu & DMMU_E) == 0) { /* DMMU disabled */
        *physical = address;
        *prot = PAGE_READ | PAGE_WRITE;
        return 0;
    }

    for (i = 0; i < 64; i++) {
        switch ((env->dtlb_tte[i] >> 61) & 3) {
        default:
        case 0x0: // 8k
            mask = 0xffffffffffffe000ULL;
            break;
        case 0x1: // 64k
            mask = 0xffffffffffff0000ULL;
            break;
        case 0x2: // 512k
            mask = 0xfffffffffff80000ULL;
            break;
        case 0x3: // 4M
            mask = 0xffffffffffc00000ULL;
            break;
        }
        // ctx match, vaddr match?
        if (env->dmmuregs[1] == (env->dtlb_tag[i] & 0x1fff) &&
            (address & mask) == (env->dtlb_tag[i] & ~0x1fffULL)) {
            // valid, access ok?
            if ((env->dtlb_tte[i] & 0x8000000000000000ULL) == 0 ||
                ((env->dtlb_tte[i] & 0x4) && is_user) ||
                (!(env->dtlb_tte[i] & 0x2) && (rw == 1))) {
                if (env->dmmuregs[3]) /* Fault status register */
                    env->dmmuregs[3] = 2; /* overflow (not read before another fault) */
                env->dmmuregs[3] |= (is_user << 3) | ((rw == 1) << 2) | 1;
                env->dmmuregs[4] = address; /* Fault address register */
                env->exception_index = TT_DFAULT;
#ifdef DEBUG_MMU
                printf("DFAULT at 0x%" PRIx64 "\n", address);
#endif
                return 1;
            }
            *physical = (env->dtlb_tte[i] & mask & 0x1fffffff000ULL) + (address & ~mask & 0x1fffffff000ULL);
            *prot = PAGE_READ;
            if (env->dtlb_tte[i] & 0x2)
                *prot |= PAGE_WRITE;
            return 0;
        }
    }
#ifdef DEBUG_MMU
    printf("DMISS at 0x%" PRIx64 "\n", address);
#endif
    env->exception_index = TT_DMISS;
    return 1;
}

static int get_physical_address_code(CPUState *env, target_phys_addr_t *physical, int *prot,
                          int *access_index, target_ulong address, int rw,
                          int is_user)
{
    target_ulong mask;
    unsigned int i;

    if ((env->lsu & IMMU_E) == 0) { /* IMMU disabled */
        *physical = address;
        *prot = PAGE_EXEC;
        return 0;
    }

    for (i = 0; i < 64; i++) {
        switch ((env->itlb_tte[i] >> 61) & 3) {
        default:
        case 0x0: // 8k
            mask = 0xffffffffffffe000ULL;
            break;
        case 0x1: // 64k
            mask = 0xffffffffffff0000ULL;
            break;
        case 0x2: // 512k
            mask = 0xfffffffffff80000ULL;
            break;
        case 0x3: // 4M
            mask = 0xffffffffffc00000ULL;
                break;
        }
        // ctx match, vaddr match?
        if (env->dmmuregs[1] == (env->itlb_tag[i] & 0x1fff) &&
            (address & mask) == (env->itlb_tag[i] & ~0x1fffULL)) {
            // valid, access ok?
            if ((env->itlb_tte[i] & 0x8000000000000000ULL) == 0 ||
                ((env->itlb_tte[i] & 0x4) && is_user)) {
                if (env->immuregs[3]) /* Fault status register */
                    env->immuregs[3] = 2; /* overflow (not read before another fault) */
                env->immuregs[3] |= (is_user << 3) | 1;
                env->exception_index = TT_TFAULT;
#ifdef DEBUG_MMU
                printf("TFAULT at 0x%" PRIx64 "\n", address);
#endif
                return 1;
            }
            *physical = (env->itlb_tte[i] & mask & 0x1fffffff000ULL) + (address & ~mask & 0x1fffffff000ULL);
            *prot = PAGE_EXEC;
            return 0;
        }
    }
#ifdef DEBUG_MMU
    printf("TMISS at 0x%" PRIx64 "\n", address);
#endif
    env->exception_index = TT_TMISS;
    return 1;
}

int get_physical_address(CPUState *env, target_phys_addr_t *physical, int *prot,
                          int *access_index, target_ulong address, int rw,
                          int mmu_idx)
{
    int is_user = mmu_idx == MMU_USER_IDX;

    if (rw == 2)
        return get_physical_address_code(env, physical, prot, access_index, address, rw, is_user);
    else
        return get_physical_address_data(env, physical, prot, access_index, address, rw, is_user);
}

/* Perform address translation */
int cpu_sparc_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu)
{
    target_ulong virt_addr, vaddr;
    target_phys_addr_t paddr;
    int error_code = 0, prot, ret = 0, access_index;

    error_code = get_physical_address(env, &paddr, &prot, &access_index, address, rw, mmu_idx);
    if (error_code == 0) {
        virt_addr = address & TARGET_PAGE_MASK;
        vaddr = virt_addr + ((address & TARGET_PAGE_MASK) & (TARGET_PAGE_SIZE - 1));
#ifdef DEBUG_MMU
        printf("Translate at 0x%" PRIx64 " -> 0x%" PRIx64 ", vaddr 0x%" PRIx64 "\n", address, paddr, vaddr);
#endif
        ret = tlb_set_page_exec(env, vaddr, paddr, prot, mmu_idx, is_softmmu);
        return ret;
    }
    // XXX
    return 1;
}

#ifdef DEBUG_MMU
void dump_mmu(CPUState *env)
{
    unsigned int i;
    const char *mask;

    printf("MMU contexts: Primary: %" PRId64 ", Secondary: %" PRId64 "\n", env->dmmuregs[1], env->dmmuregs[2]);
    if ((env->lsu & DMMU_E) == 0) {
        printf("DMMU disabled\n");
    } else {
        printf("DMMU dump:\n");
        for (i = 0; i < 64; i++) {
            switch ((env->dtlb_tte[i] >> 61) & 3) {
            default:
            case 0x0:
                mask = "  8k";
                break;
            case 0x1:
                mask = " 64k";
                break;
            case 0x2:
                mask = "512k";
                break;
            case 0x3:
                mask = "  4M";
                break;
            }
            if ((env->dtlb_tte[i] & 0x8000000000000000ULL) != 0) {
                printf("VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_lx ", %s, %s, %s, %s, ctx %" PRId64 "\n",
                       env->dtlb_tag[i] & ~0x1fffULL,
                       env->dtlb_tte[i] & 0x1ffffffe000ULL,
                       mask,
                       env->dtlb_tte[i] & 0x4? "priv": "user",
                       env->dtlb_tte[i] & 0x2? "RW": "RO",
                       env->dtlb_tte[i] & 0x40? "locked": "unlocked",
                       env->dtlb_tag[i] & 0x1fffULL);
            }
        }
    }
    if ((env->lsu & IMMU_E) == 0) {
        printf("IMMU disabled\n");
    } else {
        printf("IMMU dump:\n");
        for (i = 0; i < 64; i++) {
            switch ((env->itlb_tte[i] >> 61) & 3) {
            default:
            case 0x0:
                mask = "  8k";
                break;
            case 0x1:
                mask = " 64k";
                break;
            case 0x2:
                mask = "512k";
                break;
            case 0x3:
                mask = "  4M";
                break;
            }
            if ((env->itlb_tte[i] & 0x8000000000000000ULL) != 0) {
                printf("VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_lx ", %s, %s, %s, ctx %" PRId64 "\n",
                       env->itlb_tag[i] & ~0x1fffULL,
                       env->itlb_tte[i] & 0x1ffffffe000ULL,
                       mask,
                       env->itlb_tte[i] & 0x4? "priv": "user",
                       env->itlb_tte[i] & 0x40? "locked": "unlocked",
                       env->itlb_tag[i] & 0x1fffULL);
            }
        }
    }
}
#endif /* DEBUG_MMU */

#endif /* TARGET_SPARC64 */
#endif /* !CONFIG_USER_ONLY */

void memcpy32(target_ulong *dst, const target_ulong *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
    dst[5] = src[5];
    dst[6] = src[6];
    dst[7] = src[7];
}

#ifdef TARGET_SPARC64
#if !defined(CONFIG_USER_ONLY)
#include "qemu-common.h"
#include "hw/irq.h"
#include "qemu-timer.h"
#endif

void do_tick_set_count(void *opaque, uint64_t count)
{
#if !defined(CONFIG_USER_ONLY)
    ptimer_set_count(opaque, -count);
#endif
}

uint64_t do_tick_get_count(void *opaque)
{
#if !defined(CONFIG_USER_ONLY)
    return -ptimer_get_count(opaque);
#else
    return 0;
#endif
}

void do_tick_set_limit(void *opaque, uint64_t limit)
{
#if !defined(CONFIG_USER_ONLY)
    ptimer_set_limit(opaque, -limit, 0);
#endif
}
#endif
