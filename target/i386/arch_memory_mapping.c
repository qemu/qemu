/*
 * i386 memory mapping
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "sysemu/memory_mapping.h"

/* PAE Paging or IA-32e Paging */
static void walk_pte(MemoryMappingList *list, AddressSpace *as,
                     hwaddr pte_start_addr,
                     int32_t a20_mask, target_ulong start_line_addr)
{
    hwaddr pte_addr, start_paddr;
    uint64_t pte;
    target_ulong start_vaddr;
    int i;

    for (i = 0; i < 512; i++) {
        pte_addr = (pte_start_addr + i * 8) & a20_mask;
        pte = address_space_ldq(as, pte_addr, MEMTXATTRS_UNSPECIFIED, NULL);
        if (!(pte & PG_PRESENT_MASK)) {
            /* not present */
            continue;
        }

        start_paddr = (pte & ~0xfff) & ~(0x1ULL << 63);
        if (cpu_physical_memory_is_io(start_paddr)) {
            /* I/O region */
            continue;
        }

        start_vaddr = start_line_addr | ((i & 0x1ff) << 12);
        memory_mapping_list_add_merge_sorted(list, start_paddr,
                                             start_vaddr, 1 << 12);
    }
}

/* 32-bit Paging */
static void walk_pte2(MemoryMappingList *list, AddressSpace *as,
                      hwaddr pte_start_addr, int32_t a20_mask,
                      target_ulong start_line_addr)
{
    hwaddr pte_addr, start_paddr;
    uint32_t pte;
    target_ulong start_vaddr;
    int i;

    for (i = 0; i < 1024; i++) {
        pte_addr = (pte_start_addr + i * 4) & a20_mask;
        pte = address_space_ldl(as, pte_addr, MEMTXATTRS_UNSPECIFIED, NULL);
        if (!(pte & PG_PRESENT_MASK)) {
            /* not present */
            continue;
        }

        start_paddr = pte & ~0xfff;
        if (cpu_physical_memory_is_io(start_paddr)) {
            /* I/O region */
            continue;
        }

        start_vaddr = start_line_addr | ((i & 0x3ff) << 12);
        memory_mapping_list_add_merge_sorted(list, start_paddr,
                                             start_vaddr, 1 << 12);
    }
}

/* PAE Paging or IA-32e Paging */
#define PLM4_ADDR_MASK 0xffffffffff000ULL /* selects bits 51:12 */

static void walk_pde(MemoryMappingList *list, AddressSpace *as,
                     hwaddr pde_start_addr,
                     int32_t a20_mask, target_ulong start_line_addr)
{
    hwaddr pde_addr, pte_start_addr, start_paddr;
    uint64_t pde;
    target_ulong line_addr, start_vaddr;
    int i;

    for (i = 0; i < 512; i++) {
        pde_addr = (pde_start_addr + i * 8) & a20_mask;
        pde = address_space_ldq(as, pde_addr, MEMTXATTRS_UNSPECIFIED, NULL);
        if (!(pde & PG_PRESENT_MASK)) {
            /* not present */
            continue;
        }

        line_addr = start_line_addr | ((i & 0x1ff) << 21);
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            start_paddr = (pde & ~0x1fffff) & ~(0x1ULL << 63);
            if (cpu_physical_memory_is_io(start_paddr)) {
                /* I/O region */
                continue;
            }
            start_vaddr = line_addr;
            memory_mapping_list_add_merge_sorted(list, start_paddr,
                                                 start_vaddr, 1 << 21);
            continue;
        }

        pte_start_addr = (pde & PLM4_ADDR_MASK) & a20_mask;
        walk_pte(list, as, pte_start_addr, a20_mask, line_addr);
    }
}

/* 32-bit Paging */
static void walk_pde2(MemoryMappingList *list, AddressSpace *as,
                      hwaddr pde_start_addr, int32_t a20_mask,
                      bool pse)
{
    hwaddr pde_addr, pte_start_addr, start_paddr, high_paddr;
    uint32_t pde;
    target_ulong line_addr, start_vaddr;
    int i;

    for (i = 0; i < 1024; i++) {
        pde_addr = (pde_start_addr + i * 4) & a20_mask;
        pde = address_space_ldl(as, pde_addr, MEMTXATTRS_UNSPECIFIED, NULL);
        if (!(pde & PG_PRESENT_MASK)) {
            /* not present */
            continue;
        }

        line_addr = (((unsigned int)i & 0x3ff) << 22);
        if ((pde & PG_PSE_MASK) && pse) {
            /*
             * 4 MB page:
             * bits 39:32 are bits 20:13 of the PDE
             * bit3 31:22 are bits 31:22 of the PDE
             */
            high_paddr = ((hwaddr)(pde & 0x1fe000) << 19);
            start_paddr = (pde & ~0x3fffff) | high_paddr;
            if (cpu_physical_memory_is_io(start_paddr)) {
                /* I/O region */
                continue;
            }
            start_vaddr = line_addr;
            memory_mapping_list_add_merge_sorted(list, start_paddr,
                                                 start_vaddr, 1 << 22);
            continue;
        }

        pte_start_addr = (pde & ~0xfff) & a20_mask;
        walk_pte2(list, as, pte_start_addr, a20_mask, line_addr);
    }
}

/* PAE Paging */
static void walk_pdpe2(MemoryMappingList *list, AddressSpace *as,
                       hwaddr pdpe_start_addr, int32_t a20_mask)
{
    hwaddr pdpe_addr, pde_start_addr;
    uint64_t pdpe;
    target_ulong line_addr;
    int i;

    for (i = 0; i < 4; i++) {
        pdpe_addr = (pdpe_start_addr + i * 8) & a20_mask;
        pdpe = address_space_ldq(as, pdpe_addr, MEMTXATTRS_UNSPECIFIED, NULL);
        if (!(pdpe & PG_PRESENT_MASK)) {
            /* not present */
            continue;
        }

        line_addr = (((unsigned int)i & 0x3) << 30);
        pde_start_addr = (pdpe & ~0xfff) & a20_mask;
        walk_pde(list, as, pde_start_addr, a20_mask, line_addr);
    }
}

#ifdef TARGET_X86_64
/* IA-32e Paging */
static void walk_pdpe(MemoryMappingList *list, AddressSpace *as,
                      hwaddr pdpe_start_addr, int32_t a20_mask,
                      target_ulong start_line_addr)
{
    hwaddr pdpe_addr, pde_start_addr, start_paddr;
    uint64_t pdpe;
    target_ulong line_addr, start_vaddr;
    int i;

    for (i = 0; i < 512; i++) {
        pdpe_addr = (pdpe_start_addr + i * 8) & a20_mask;
        pdpe = address_space_ldq(as, pdpe_addr, MEMTXATTRS_UNSPECIFIED, NULL);
        if (!(pdpe & PG_PRESENT_MASK)) {
            /* not present */
            continue;
        }

        line_addr = start_line_addr | ((i & 0x1ffULL) << 30);
        if (pdpe & PG_PSE_MASK) {
            /* 1 GB page */
            start_paddr = (pdpe & ~0x3fffffff) & ~(0x1ULL << 63);
            if (cpu_physical_memory_is_io(start_paddr)) {
                /* I/O region */
                continue;
            }
            start_vaddr = line_addr;
            memory_mapping_list_add_merge_sorted(list, start_paddr,
                                                 start_vaddr, 1 << 30);
            continue;
        }

        pde_start_addr = (pdpe & PLM4_ADDR_MASK) & a20_mask;
        walk_pde(list, as, pde_start_addr, a20_mask, line_addr);
    }
}

/* IA-32e Paging */
static void walk_pml4e(MemoryMappingList *list, AddressSpace *as,
                       hwaddr pml4e_start_addr, int32_t a20_mask,
                       target_ulong start_line_addr)
{
    hwaddr pml4e_addr, pdpe_start_addr;
    uint64_t pml4e;
    target_ulong line_addr;
    int i;

    for (i = 0; i < 512; i++) {
        pml4e_addr = (pml4e_start_addr + i * 8) & a20_mask;
        pml4e = address_space_ldq(as, pml4e_addr, MEMTXATTRS_UNSPECIFIED,
                                  NULL);
        if (!(pml4e & PG_PRESENT_MASK)) {
            /* not present */
            continue;
        }

        line_addr = start_line_addr | ((i & 0x1ffULL) << 39);
        pdpe_start_addr = (pml4e & PLM4_ADDR_MASK) & a20_mask;
        walk_pdpe(list, as, pdpe_start_addr, a20_mask, line_addr);
    }
}

static void walk_pml5e(MemoryMappingList *list, AddressSpace *as,
                       hwaddr pml5e_start_addr, int32_t a20_mask)
{
    hwaddr pml5e_addr, pml4e_start_addr;
    uint64_t pml5e;
    target_ulong line_addr;
    int i;

    for (i = 0; i < 512; i++) {
        pml5e_addr = (pml5e_start_addr + i * 8) & a20_mask;
        pml5e = address_space_ldq(as, pml5e_addr, MEMTXATTRS_UNSPECIFIED,
                                  NULL);
        if (!(pml5e & PG_PRESENT_MASK)) {
            /* not present */
            continue;
        }

        line_addr = (0x7fULL << 57) | ((i & 0x1ffULL) << 48);
        pml4e_start_addr = (pml5e & PLM4_ADDR_MASK) & a20_mask;
        walk_pml4e(list, as, pml4e_start_addr, a20_mask, line_addr);
    }
}
#endif

bool x86_cpu_get_memory_mapping(CPUState *cs, MemoryMappingList *list,
                                Error **errp)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int32_t a20_mask;

    if (!cpu_paging_enabled(cs)) {
        /* paging is disabled */
        return true;
    }

    a20_mask = x86_get_a20_mask(env);
    if (env->cr[4] & CR4_PAE_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            if (env->cr[4] & CR4_LA57_MASK) {
                hwaddr pml5e_addr;

                pml5e_addr = (env->cr[3] & PLM4_ADDR_MASK) & a20_mask;
                walk_pml5e(list, cs->as, pml5e_addr, a20_mask);
            } else {
                hwaddr pml4e_addr;

                pml4e_addr = (env->cr[3] & PLM4_ADDR_MASK) & a20_mask;
                walk_pml4e(list, cs->as, pml4e_addr, a20_mask,
                        0xffffULL << 48);
            }
        } else
#endif
        {
            hwaddr pdpe_addr;

            pdpe_addr = (env->cr[3] & ~0x1f) & a20_mask;
            walk_pdpe2(list, cs->as, pdpe_addr, a20_mask);
        }
    } else {
        hwaddr pde_addr;
        bool pse;

        pde_addr = (env->cr[3] & ~0xfff) & a20_mask;
        pse = !!(env->cr[4] & CR4_PSE_MASK);
        walk_pde2(list, cs->as, pde_addr, a20_mask, pse);
    }

    return true;
}

