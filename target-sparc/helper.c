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
#include "qemu-common.h"

//#define DEBUG_MMU
//#define DEBUG_FEATURES

static int cpu_sparc_find_by_name(sparc_def_t *cpu_def, const char *cpu_model);

/* Sparc MMU emulation */

/* thread support */

static spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void cpu_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void cpu_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

#if defined(CONFIG_USER_ONLY)

int cpu_sparc_handle_mmu_fault(CPUState *env1, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
    if (rw & 2)
        env1->exception_index = TT_TFAULT;
    else
        env1->exception_index = TT_DFAULT;
    return 1;
}

#else

#ifndef TARGET_SPARC64
/*
 * Sparc V8 Reference MMU (SRMMU)
 */
static const int access_table[8][8] = {
    { 0, 0, 0, 0, 8, 0, 12, 12 },
    { 0, 0, 0, 0, 8, 0, 0, 0 },
    { 8, 8, 0, 0, 0, 8, 12, 12 },
    { 8, 8, 0, 0, 0, 8, 0, 0 },
    { 8, 0, 8, 0, 8, 8, 12, 12 },
    { 8, 0, 8, 0, 8, 0, 8, 0 },
    { 8, 8, 8, 0, 8, 8, 12, 12 },
    { 8, 8, 8, 0, 8, 8, 8, 0 }
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

static int get_physical_address(CPUState *env, target_phys_addr_t *physical,
                                int *prot, int *access_index,
                                target_ulong address, int rw, int mmu_idx)
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
        if (rw == 2 && (env->mmuregs[0] & env->def->mmu_bm)) {
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
    pde_ptr = (env->mmuregs[1] << 4) + (env->mmuregs[2] << 2);
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
                    page_offset = (address & TARGET_PAGE_MASK) &
                        (TARGET_PAGE_SIZE - 1);
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

    error_code = get_physical_address(env, &paddr, &prot, &access_index,
                                      address, rw, mmu_idx);
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
static int get_physical_address_data(CPUState *env,
                                     target_phys_addr_t *physical, int *prot,
                                     target_ulong address, int rw, int is_user)
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
        // ctx match, vaddr match, valid?
        if (env->dmmuregs[1] == (env->dtlb_tag[i] & 0x1fff) &&
            (address & mask) == (env->dtlb_tag[i] & mask) &&
            (env->dtlb_tte[i] & 0x8000000000000000ULL)) {
            // access ok?
            if (((env->dtlb_tte[i] & 0x4) && is_user) ||
                (!(env->dtlb_tte[i] & 0x2) && (rw == 1))) {
                if (env->dmmuregs[3]) /* Fault status register */
                    env->dmmuregs[3] = 2; /* overflow (not read before
                                             another fault) */
                env->dmmuregs[3] |= (is_user << 3) | ((rw == 1) << 2) | 1;
                env->dmmuregs[4] = address; /* Fault address register */
                env->exception_index = TT_DFAULT;
#ifdef DEBUG_MMU
                printf("DFAULT at 0x%" PRIx64 "\n", address);
#endif
                return 1;
            }
            *physical = ((env->dtlb_tte[i] & mask) | (address & ~mask)) &
                        0x1ffffffe000ULL;
            *prot = PAGE_READ;
            if (env->dtlb_tte[i] & 0x2)
                *prot |= PAGE_WRITE;
            return 0;
        }
    }
#ifdef DEBUG_MMU
    printf("DMISS at 0x%" PRIx64 "\n", address);
#endif
    env->dmmuregs[6] = (address & ~0x1fffULL) | (env->dmmuregs[1] & 0x1fff);
    env->exception_index = TT_DMISS;
    return 1;
}

static int get_physical_address_code(CPUState *env,
                                     target_phys_addr_t *physical, int *prot,
                                     target_ulong address, int is_user)
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
        // ctx match, vaddr match, valid?
        if (env->dmmuregs[1] == (env->itlb_tag[i] & 0x1fff) &&
            (address & mask) == (env->itlb_tag[i] & mask) &&
            (env->itlb_tte[i] & 0x8000000000000000ULL)) {
            // access ok?
            if ((env->itlb_tte[i] & 0x4) && is_user) {
                if (env->immuregs[3]) /* Fault status register */
                    env->immuregs[3] = 2; /* overflow (not read before
                                             another fault) */
                env->immuregs[3] |= (is_user << 3) | 1;
                env->exception_index = TT_TFAULT;
#ifdef DEBUG_MMU
                printf("TFAULT at 0x%" PRIx64 "\n", address);
#endif
                return 1;
            }
            *physical = ((env->itlb_tte[i] & mask) | (address & ~mask)) &
                        0x1ffffffe000ULL;
            *prot = PAGE_EXEC;
            return 0;
        }
    }
#ifdef DEBUG_MMU
    printf("TMISS at 0x%" PRIx64 "\n", address);
#endif
    env->immuregs[6] = (address & ~0x1fffULL) | (env->dmmuregs[1] & 0x1fff);
    env->exception_index = TT_TMISS;
    return 1;
}

static int get_physical_address(CPUState *env, target_phys_addr_t *physical,
                                int *prot, int *access_index,
                                target_ulong address, int rw, int mmu_idx)
{
    int is_user = mmu_idx == MMU_USER_IDX;

    if (rw == 2)
        return get_physical_address_code(env, physical, prot, address,
                                         is_user);
    else
        return get_physical_address_data(env, physical, prot, address, rw,
                                         is_user);
}

/* Perform address translation */
int cpu_sparc_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu)
{
    target_ulong virt_addr, vaddr;
    target_phys_addr_t paddr;
    int error_code = 0, prot, ret = 0, access_index;

    error_code = get_physical_address(env, &paddr, &prot, &access_index,
                                      address, rw, mmu_idx);
    if (error_code == 0) {
        virt_addr = address & TARGET_PAGE_MASK;
        vaddr = virt_addr + ((address & TARGET_PAGE_MASK) &
                             (TARGET_PAGE_SIZE - 1));
#ifdef DEBUG_MMU
        printf("Translate at 0x%" PRIx64 " -> 0x%" PRIx64 ", vaddr 0x%" PRIx64
               "\n", address, paddr, vaddr);
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

    printf("MMU contexts: Primary: %" PRId64 ", Secondary: %" PRId64 "\n",
           env->dmmuregs[1], env->dmmuregs[2]);
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
                printf("VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_lx
                       ", %s, %s, %s, %s, ctx %" PRId64 "\n",
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
                printf("VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_lx
                       ", %s, %s, %s, ctx %" PRId64 "\n",
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


#if defined(CONFIG_USER_ONLY)
target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

#else
target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    target_phys_addr_t phys_addr;
    int prot, access_index;

    if (get_physical_address(env, &phys_addr, &prot, &access_index, addr, 2,
                             MMU_KERNEL_IDX) != 0)
        if (get_physical_address(env, &phys_addr, &prot, &access_index, addr,
                                 0, MMU_KERNEL_IDX) != 0)
            return -1;
    if (cpu_get_physical_page_desc(phys_addr) == IO_MEM_UNASSIGNED)
        return -1;
    return phys_addr;
}
#endif

void cpu_reset(CPUSPARCState *env)
{
    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, 0);
    }

    tlb_flush(env, 1);
    env->cwp = 0;
    env->wim = 1;
    env->regwptr = env->regbase + (env->cwp * 16);
#if defined(CONFIG_USER_ONLY)
#ifdef TARGET_SPARC64
    env->cleanwin = env->nwindows - 2;
    env->cansave = env->nwindows - 2;
    env->pstate = PS_RMO | PS_PEF | PS_IE;
    env->asi = 0x82; // Primary no-fault
#endif
#else
    env->psret = 0;
    env->psrs = 1;
    env->psrps = 1;
#ifdef TARGET_SPARC64
    env->pstate = PS_PRIV;
    env->hpstate = HS_PRIV;
    env->tsptr = &env->ts[env->tl & MAXTL_MASK];
    env->lsu = 0;
#else
    env->mmuregs[0] &= ~(MMU_E | MMU_NF);
    env->mmuregs[0] |= env->def->mmu_bm;
#endif
    env->pc = 0;
    env->npc = env->pc + 4;
#endif
}

static int cpu_sparc_register(CPUSPARCState *env, const char *cpu_model)
{
    sparc_def_t def1, *def = &def1;

    if (cpu_sparc_find_by_name(def, cpu_model) < 0)
        return -1;

    env->def = qemu_mallocz(sizeof(*def));
    memcpy(env->def, def, sizeof(*def));
#if defined(CONFIG_USER_ONLY)
    if ((env->def->features & CPU_FEATURE_FLOAT))
        env->def->features |= CPU_FEATURE_FLOAT128;
#endif
    env->cpu_model_str = cpu_model;
    env->version = def->iu_version;
    env->fsr = def->fpu_version;
    env->nwindows = def->nwindows;
#if !defined(TARGET_SPARC64)
    env->mmuregs[0] |= def->mmu_version;
    cpu_sparc_set_id(env, 0);
    env->mxccregs[7] |= def->mxcc_version;
#else
    env->mmu_version = def->mmu_version;
    env->maxtl = def->maxtl;
    env->version |= def->maxtl << 8;
    env->version |= def->nwindows - 1;
#endif
    return 0;
}

static void cpu_sparc_close(CPUSPARCState *env)
{
    free(env->def);
    free(env);
}

CPUSPARCState *cpu_sparc_init(const char *cpu_model)
{
    CPUSPARCState *env;

    env = qemu_mallocz(sizeof(CPUSPARCState));
    cpu_exec_init(env);

    gen_intermediate_code_init(env);

    if (cpu_sparc_register(env, cpu_model) < 0) {
        cpu_sparc_close(env);
        return NULL;
    }
    cpu_reset(env);
    qemu_init_vcpu(env);

    return env;
}

void cpu_sparc_set_id(CPUSPARCState *env, unsigned int cpu)
{
#if !defined(TARGET_SPARC64)
    env->mxccregs[7] = ((cpu + 8) & 0xf) << 24;
#endif
}

static const sparc_def_t sparc_defs[] = {
#ifdef TARGET_SPARC64
    {
        .name = "Fujitsu Sparc64",
        .iu_version = ((0x04ULL << 48) | (0x02ULL << 32) | (0ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 4,
        .maxtl = 4,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Fujitsu Sparc64 III",
        .iu_version = ((0x04ULL << 48) | (0x03ULL << 32) | (0ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 5,
        .maxtl = 4,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Fujitsu Sparc64 IV",
        .iu_version = ((0x04ULL << 48) | (0x04ULL << 32) | (0ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Fujitsu Sparc64 V",
        .iu_version = ((0x04ULL << 48) | (0x05ULL << 32) | (0x51ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI UltraSparc I",
        .iu_version = ((0x17ULL << 48) | (0x10ULL << 32) | (0x40ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI UltraSparc II",
        .iu_version = ((0x17ULL << 48) | (0x11ULL << 32) | (0x20ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI UltraSparc IIi",
        .iu_version = ((0x17ULL << 48) | (0x12ULL << 32) | (0x91ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI UltraSparc IIe",
        .iu_version = ((0x17ULL << 48) | (0x13ULL << 32) | (0x14ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun UltraSparc III",
        .iu_version = ((0x3eULL << 48) | (0x14ULL << 32) | (0x34ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun UltraSparc III Cu",
        .iu_version = ((0x3eULL << 48) | (0x15ULL << 32) | (0x41ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_3,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun UltraSparc IIIi",
        .iu_version = ((0x3eULL << 48) | (0x16ULL << 32) | (0x34ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun UltraSparc IV",
        .iu_version = ((0x3eULL << 48) | (0x18ULL << 32) | (0x31ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_4,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun UltraSparc IV+",
        .iu_version = ((0x3eULL << 48) | (0x19ULL << 32) | (0x22ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES | CPU_FEATURE_CMT,
    },
    {
        .name = "Sun UltraSparc IIIi+",
        .iu_version = ((0x3eULL << 48) | (0x22ULL << 32) | (0ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_3,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Sun UltraSparc T1",
        // defined in sparc_ifu_fdp.v and ctu.h
        .iu_version = ((0x3eULL << 48) | (0x23ULL << 32) | (0x02ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_sun4v,
        .nwindows = 8,
        .maxtl = 6,
        .features = CPU_DEFAULT_FEATURES | CPU_FEATURE_HYPV | CPU_FEATURE_CMT
        | CPU_FEATURE_GL,
    },
    {
        .name = "Sun UltraSparc T2",
        // defined in tlu_asi_ctl.v and n2_revid_cust.v
        .iu_version = ((0x3eULL << 48) | (0x24ULL << 32) | (0x02ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_sun4v,
        .nwindows = 8,
        .maxtl = 6,
        .features = CPU_DEFAULT_FEATURES | CPU_FEATURE_HYPV | CPU_FEATURE_CMT
        | CPU_FEATURE_GL,
    },
    {
        .name = "NEC UltraSparc I",
        .iu_version = ((0x22ULL << 48) | (0x10ULL << 32) | (0x40ULL << 24)),
        .fpu_version = 0x00000000,
        .mmu_version = mmu_us_12,
        .nwindows = 8,
        .maxtl = 5,
        .features = CPU_DEFAULT_FEATURES,
    },
#else
    {
        .name = "Fujitsu MB86900",
        .iu_version = 0x00 << 24, /* Impl 0, ver 0 */
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0x00 << 24, /* Impl 0, ver 0 */
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 7,
        .features = CPU_FEATURE_FLOAT | CPU_FEATURE_FSMULD,
    },
    {
        .name = "Fujitsu MB86904",
        .iu_version = 0x04 << 24, /* Impl 0, ver 4 */
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0x04 << 24, /* Impl 0, ver 4 */
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x00ffffc0,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0x00016fff,
        .mmu_trcr_mask = 0x00ffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Fujitsu MB86907",
        .iu_version = 0x05 << 24, /* Impl 0, ver 5 */
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0x05 << 24, /* Impl 0, ver 5 */
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0x00016fff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "LSI L64811",
        .iu_version = 0x10 << 24, /* Impl 1, ver 0 */
        .fpu_version = 1 << 17, /* FPU version 1 (LSI L64814) */
        .mmu_version = 0x10 << 24,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_FEATURE_FLOAT | CPU_FEATURE_SWAP | CPU_FEATURE_FSQRT |
        CPU_FEATURE_FSMULD,
    },
    {
        .name = "Cypress CY7C601",
        .iu_version = 0x11 << 24, /* Impl 1, ver 1 */
        .fpu_version = 3 << 17, /* FPU version 3 (Cypress CY7C602) */
        .mmu_version = 0x10 << 24,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_FEATURE_FLOAT | CPU_FEATURE_SWAP | CPU_FEATURE_FSQRT |
        CPU_FEATURE_FSMULD,
    },
    {
        .name = "Cypress CY7C611",
        .iu_version = 0x13 << 24, /* Impl 1, ver 3 */
        .fpu_version = 3 << 17, /* FPU version 3 (Cypress CY7C602) */
        .mmu_version = 0x10 << 24,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_FEATURE_FLOAT | CPU_FEATURE_SWAP | CPU_FEATURE_FSQRT |
        CPU_FEATURE_FSMULD,
    },
    {
        .name = "TI MicroSparc I",
        .iu_version = 0x41000000,
        .fpu_version = 4 << 17,
        .mmu_version = 0x41000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0x00016fff,
        .mmu_trcr_mask = 0x0000003f,
        .nwindows = 7,
        .features = CPU_FEATURE_FLOAT | CPU_FEATURE_SWAP | CPU_FEATURE_MUL |
        CPU_FEATURE_DIV | CPU_FEATURE_FLUSH | CPU_FEATURE_FSQRT |
        CPU_FEATURE_FMUL,
    },
    {
        .name = "TI MicroSparc II",
        .iu_version = 0x42000000,
        .fpu_version = 4 << 17,
        .mmu_version = 0x02000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x00ffffc0,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0x00016fff,
        .mmu_trcr_mask = 0x00ffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI MicroSparc IIep",
        .iu_version = 0x42000000,
        .fpu_version = 4 << 17,
        .mmu_version = 0x04000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x00ffffc0,
        .mmu_cxr_mask = 0x000000ff,
        .mmu_sfsr_mask = 0x00016bff,
        .mmu_trcr_mask = 0x00ffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI SuperSparc 40", // STP1020NPGA
        .iu_version = 0x41000000, // SuperSPARC 2.x
        .fpu_version = 0 << 17,
        .mmu_version = 0x00000800, // SuperSPARC 2.x, no MXCC
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI SuperSparc 50", // STP1020PGA
        .iu_version = 0x40000000, // SuperSPARC 3.x
        .fpu_version = 0 << 17,
        .mmu_version = 0x01000800, // SuperSPARC 3.x, no MXCC
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI SuperSparc 51",
        .iu_version = 0x40000000, // SuperSPARC 3.x
        .fpu_version = 0 << 17,
        .mmu_version = 0x01000000, // SuperSPARC 3.x, MXCC
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .mxcc_version = 0x00000104,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI SuperSparc 60", // STP1020APGA
        .iu_version = 0x40000000, // SuperSPARC 3.x
        .fpu_version = 0 << 17,
        .mmu_version = 0x01000800, // SuperSPARC 3.x, no MXCC
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI SuperSparc 61",
        .iu_version = 0x44000000, // SuperSPARC 3.x
        .fpu_version = 0 << 17,
        .mmu_version = 0x01000000, // SuperSPARC 3.x, MXCC
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .mxcc_version = 0x00000104,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "TI SuperSparc II",
        .iu_version = 0x40000000, // SuperSPARC II 1.x
        .fpu_version = 0 << 17,
        .mmu_version = 0x08000000, // SuperSPARC II 1.x, MXCC
        .mmu_bm = 0x00002000,
        .mmu_ctpr_mask = 0xffffffc0,
        .mmu_cxr_mask = 0x0000ffff,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .mxcc_version = 0x00000104,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Ross RT625",
        .iu_version = 0x1e000000,
        .fpu_version = 1 << 17,
        .mmu_version = 0x1e000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "Ross RT620",
        .iu_version = 0x1f000000,
        .fpu_version = 1 << 17,
        .mmu_version = 0x1f000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "BIT B5010",
        .iu_version = 0x20000000,
        .fpu_version = 0 << 17, /* B5010/B5110/B5120/B5210 */
        .mmu_version = 0x20000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_FEATURE_FLOAT | CPU_FEATURE_SWAP | CPU_FEATURE_FSQRT |
        CPU_FEATURE_FSMULD,
    },
    {
        .name = "Matsushita MN10501",
        .iu_version = 0x50000000,
        .fpu_version = 0 << 17,
        .mmu_version = 0x50000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_FEATURE_FLOAT | CPU_FEATURE_MUL | CPU_FEATURE_FSQRT |
        CPU_FEATURE_FSMULD,
    },
    {
        .name = "Weitek W8601",
        .iu_version = 0x90 << 24, /* Impl 9, ver 0 */
        .fpu_version = 3 << 17, /* FPU version 3 (Weitek WTL3170/2) */
        .mmu_version = 0x10 << 24,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "LEON2",
        .iu_version = 0xf2000000,
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0xf2000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
    {
        .name = "LEON3",
        .iu_version = 0xf3000000,
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0xf3000000,
        .mmu_bm = 0x00004000,
        .mmu_ctpr_mask = 0x007ffff0,
        .mmu_cxr_mask = 0x0000003f,
        .mmu_sfsr_mask = 0xffffffff,
        .mmu_trcr_mask = 0xffffffff,
        .nwindows = 8,
        .features = CPU_DEFAULT_FEATURES,
    },
#endif
};

static const char * const feature_name[] = {
    "float",
    "float128",
    "swap",
    "mul",
    "div",
    "flush",
    "fsqrt",
    "fmul",
    "vis1",
    "vis2",
    "fsmuld",
    "hypv",
    "cmt",
    "gl",
};

static void print_features(FILE *f,
                           int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                           uint32_t features, const char *prefix)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(feature_name); i++)
        if (feature_name[i] && (features & (1 << i))) {
            if (prefix)
                (*cpu_fprintf)(f, "%s", prefix);
            (*cpu_fprintf)(f, "%s ", feature_name[i]);
        }
}

static void add_flagname_to_bitmaps(const char *flagname, uint32_t *features)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(feature_name); i++)
        if (feature_name[i] && !strcmp(flagname, feature_name[i])) {
            *features |= 1 << i;
            return;
        }
    fprintf(stderr, "CPU feature %s not found\n", flagname);
}

static int cpu_sparc_find_by_name(sparc_def_t *cpu_def, const char *cpu_model)
{
    unsigned int i;
    const sparc_def_t *def = NULL;
    char *s = strdup(cpu_model);
    char *featurestr, *name = strtok(s, ",");
    uint32_t plus_features = 0;
    uint32_t minus_features = 0;
    long long iu_version;
    uint32_t fpu_version, mmu_version, nwindows;

    for (i = 0; i < ARRAY_SIZE(sparc_defs); i++) {
        if (strcasecmp(name, sparc_defs[i].name) == 0) {
            def = &sparc_defs[i];
        }
    }
    if (!def)
        goto error;
    memcpy(cpu_def, def, sizeof(*def));

    featurestr = strtok(NULL, ",");
    while (featurestr) {
        char *val;

        if (featurestr[0] == '+') {
            add_flagname_to_bitmaps(featurestr + 1, &plus_features);
        } else if (featurestr[0] == '-') {
            add_flagname_to_bitmaps(featurestr + 1, &minus_features);
        } else if ((val = strchr(featurestr, '='))) {
            *val = 0; val++;
            if (!strcmp(featurestr, "iu_version")) {
                char *err;

                iu_version = strtoll(val, &err, 0);
                if (!*val || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                cpu_def->iu_version = iu_version;
#ifdef DEBUG_FEATURES
                fprintf(stderr, "iu_version %llx\n", iu_version);
#endif
            } else if (!strcmp(featurestr, "fpu_version")) {
                char *err;

                fpu_version = strtol(val, &err, 0);
                if (!*val || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                cpu_def->fpu_version = fpu_version;
#ifdef DEBUG_FEATURES
                fprintf(stderr, "fpu_version %llx\n", fpu_version);
#endif
            } else if (!strcmp(featurestr, "mmu_version")) {
                char *err;

                mmu_version = strtol(val, &err, 0);
                if (!*val || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                cpu_def->mmu_version = mmu_version;
#ifdef DEBUG_FEATURES
                fprintf(stderr, "mmu_version %llx\n", mmu_version);
#endif
            } else if (!strcmp(featurestr, "nwindows")) {
                char *err;

                nwindows = strtol(val, &err, 0);
                if (!*val || *err || nwindows > MAX_NWINDOWS ||
                    nwindows < MIN_NWINDOWS) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                cpu_def->nwindows = nwindows;
#ifdef DEBUG_FEATURES
                fprintf(stderr, "nwindows %d\n", nwindows);
#endif
            } else {
                fprintf(stderr, "unrecognized feature %s\n", featurestr);
                goto error;
            }
        } else {
            fprintf(stderr, "feature string `%s' not in format "
                    "(+feature|-feature|feature=xyz)\n", featurestr);
            goto error;
        }
        featurestr = strtok(NULL, ",");
    }
    cpu_def->features |= plus_features;
    cpu_def->features &= ~minus_features;
#ifdef DEBUG_FEATURES
    print_features(stderr, fprintf, cpu_def->features, NULL);
#endif
    free(s);
    return 0;

 error:
    free(s);
    return -1;
}

void sparc_cpu_list(FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...))
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(sparc_defs); i++) {
        (*cpu_fprintf)(f, "Sparc %16s IU " TARGET_FMT_lx " FPU %08x MMU %08x NWINS %d ",
                       sparc_defs[i].name,
                       sparc_defs[i].iu_version,
                       sparc_defs[i].fpu_version,
                       sparc_defs[i].mmu_version,
                       sparc_defs[i].nwindows);
        print_features(f, cpu_fprintf, CPU_DEFAULT_FEATURES &
                       ~sparc_defs[i].features, "-");
        print_features(f, cpu_fprintf, ~CPU_DEFAULT_FEATURES &
                       sparc_defs[i].features, "+");
        (*cpu_fprintf)(f, "\n");
    }
    (*cpu_fprintf)(f, "Default CPU feature flags (use '-' to remove): ");
    print_features(f, cpu_fprintf, CPU_DEFAULT_FEATURES, NULL);
    (*cpu_fprintf)(f, "\n");
    (*cpu_fprintf)(f, "Available CPU feature flags (use '+' to add): ");
    print_features(f, cpu_fprintf, ~CPU_DEFAULT_FEATURES, NULL);
    (*cpu_fprintf)(f, "\n");
    (*cpu_fprintf)(f, "Numerical features (use '=' to set): iu_version "
                   "fpu_version mmu_version nwindows\n");
}

void cpu_dump_state(CPUState *env, FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
    int i, x;

    cpu_fprintf(f, "pc: " TARGET_FMT_lx "  npc: " TARGET_FMT_lx "\n", env->pc,
                env->npc);
    cpu_fprintf(f, "General Registers:\n");
    for (i = 0; i < 4; i++)
        cpu_fprintf(f, "%%g%c: " TARGET_FMT_lx "\t", i + '0', env->gregs[i]);
    cpu_fprintf(f, "\n");
    for (; i < 8; i++)
        cpu_fprintf(f, "%%g%c: " TARGET_FMT_lx "\t", i + '0', env->gregs[i]);
    cpu_fprintf(f, "\nCurrent Register Window:\n");
    for (x = 0; x < 3; x++) {
        for (i = 0; i < 4; i++)
            cpu_fprintf(f, "%%%c%d: " TARGET_FMT_lx "\t",
                    (x == 0 ? 'o' : (x == 1 ? 'l' : 'i')), i,
                    env->regwptr[i + x * 8]);
        cpu_fprintf(f, "\n");
        for (; i < 8; i++)
            cpu_fprintf(f, "%%%c%d: " TARGET_FMT_lx "\t",
                    (x == 0 ? 'o' : x == 1 ? 'l' : 'i'), i,
                    env->regwptr[i + x * 8]);
        cpu_fprintf(f, "\n");
    }
    cpu_fprintf(f, "\nFloating Point Registers:\n");
    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0)
            cpu_fprintf(f, "%%f%02d:", i);
        cpu_fprintf(f, " %016f", *(float *)&env->fpr[i]);
        if ((i & 3) == 3)
            cpu_fprintf(f, "\n");
    }
#ifdef TARGET_SPARC64
    cpu_fprintf(f, "pstate: 0x%08x ccr: 0x%02x asi: 0x%02x tl: %d fprs: %d\n",
                env->pstate, GET_CCR(env), env->asi, env->tl, env->fprs);
    cpu_fprintf(f, "cansave: %d canrestore: %d otherwin: %d wstate %d "
                "cleanwin %d cwp %d\n",
                env->cansave, env->canrestore, env->otherwin, env->wstate,
                env->cleanwin, env->nwindows - 1 - env->cwp);
#else

#define GET_FLAG(a,b) ((env->psr & a)?b:'-')

    cpu_fprintf(f, "psr: 0x%08x -> %c%c%c%c %c%c%c wim: 0x%08x\n",
                GET_PSR(env), GET_FLAG(PSR_ZERO, 'Z'), GET_FLAG(PSR_OVF, 'V'),
                GET_FLAG(PSR_NEG, 'N'), GET_FLAG(PSR_CARRY, 'C'),
                env->psrs?'S':'-', env->psrps?'P':'-',
                env->psret?'E':'-', env->wim);
#endif
    cpu_fprintf(f, "fsr: 0x%08x\n", env->fsr);
}
