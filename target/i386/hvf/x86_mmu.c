/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "panic.h"
#include "qemu-common.h"
#include "cpu.h"
#include "x86.h"
#include "x86_mmu.h"
#include "vmcs.h"
#include "vmx.h"
#include "exec/address-spaces.h"

#define pte_present(pte) (pte & PT_PRESENT)
#define pte_write_access(pte) (pte & PT_WRITE)
#define pte_user_access(pte) (pte & PT_USER)
#define pte_exec_access(pte) (!(pte & PT_NX))

#define pte_large_page(pte) (pte & PT_PS)
#define pte_global_access(pte) (pte & PT_GLOBAL)

#define PAE_CR3_MASK                (~0x1fllu)
#define LEGACY_CR3_MASK             (0xffffffff)

#define LEGACY_PTE_PAGE_MASK        (0xffffffffllu << 12)
#define PAE_PTE_PAGE_MASK           ((-1llu << 12) & ((1llu << 52) - 1))
#define PAE_PTE_LARGE_PAGE_MASK     ((-1llu << (21)) & ((1llu << 52) - 1))

struct gpt_translation {
    target_ulong  gva;
    uint64_t gpa;
    int    err_code;
    uint64_t pte[5];
    bool write_access;
    bool user_access;
    bool exec_access;
};

static int gpt_top_level(struct CPUState *cpu, bool pae)
{
    if (!pae) {
        return 2;
    }
    if (x86_is_long_mode(cpu)) {
        return 4;
    }

    return 3;
}

static inline int gpt_entry(target_ulong addr, int level, bool pae)
{
    int level_shift = pae ? 9 : 10;
    return (addr >> (level_shift * (level - 1) + 12)) & ((1 << level_shift) - 1);
}

static inline int pte_size(bool pae)
{
    return pae ? 8 : 4;
}


static bool get_pt_entry(struct CPUState *cpu, struct gpt_translation *pt,
                         int level, bool pae)
{
    int index;
    uint64_t pte = 0;
    uint64_t page_mask = pae ? PAE_PTE_PAGE_MASK : LEGACY_PTE_PAGE_MASK;
    uint64_t gpa = pt->pte[level] & page_mask;

    if (level == 3 && !x86_is_long_mode(cpu)) {
        gpa = pt->pte[level];
    }

    index = gpt_entry(pt->gva, level, pae);
    address_space_rw(&address_space_memory, gpa + index * pte_size(pae),
                     MEMTXATTRS_UNSPECIFIED, (uint8_t *)&pte, pte_size(pae), 0);

    pt->pte[level - 1] = pte;

    return true;
}

/* test page table entry */
static bool test_pt_entry(struct CPUState *cpu, struct gpt_translation *pt,
                          int level, bool *is_large, bool pae)
{
    uint64_t pte = pt->pte[level];

    if (pt->write_access) {
        pt->err_code |= MMU_PAGE_WT;
    }
    if (pt->user_access) {
        pt->err_code |= MMU_PAGE_US;
    }
    if (pt->exec_access) {
        pt->err_code |= MMU_PAGE_NX;
    }

    if (!pte_present(pte)) {
        return false;
    }

    if (pae && !x86_is_long_mode(cpu) && 2 == level) {
        goto exit;
    }

    if (1 == level && pte_large_page(pte)) {
        pt->err_code |= MMU_PAGE_PT;
        *is_large = true;
    }
    if (!level) {
        pt->err_code |= MMU_PAGE_PT;
    }

    uint32_t cr0 = rvmcs(cpu->hvf_fd, VMCS_GUEST_CR0);
    /* check protection */
    if (cr0 & CR0_WP) {
        if (pt->write_access && !pte_write_access(pte)) {
            return false;
        }
    }

    if (pt->user_access && !pte_user_access(pte)) {
        return false;
    }

    if (pae && pt->exec_access && !pte_exec_access(pte)) {
        return false;
    }
    
exit:
    /* TODO: check reserved bits */
    return true;
}

static inline uint64_t pse_pte_to_page(uint64_t pte)
{
    return ((pte & 0x1fe000) << 19) | (pte & 0xffc00000);
}

static inline uint64_t large_page_gpa(struct gpt_translation *pt, bool pae)
{
    VM_PANIC_ON(!pte_large_page(pt->pte[1]))
    /* 2Mb large page  */
    if (pae) {
        return (pt->pte[1] & PAE_PTE_LARGE_PAGE_MASK) | (pt->gva & 0x1fffff);
    }

    /* 4Mb large page */
    return pse_pte_to_page(pt->pte[1]) | (pt->gva & 0x3fffff);
}



static bool walk_gpt(struct CPUState *cpu, target_ulong addr, int err_code,
                     struct gpt_translation *pt, bool pae)
{
    int top_level, level;
    bool is_large = false;
    target_ulong cr3 = rvmcs(cpu->hvf_fd, VMCS_GUEST_CR3);
    uint64_t page_mask = pae ? PAE_PTE_PAGE_MASK : LEGACY_PTE_PAGE_MASK;
    
    memset(pt, 0, sizeof(*pt));
    top_level = gpt_top_level(cpu, pae);

    pt->pte[top_level] = pae ? (cr3 & PAE_CR3_MASK) : (cr3 & LEGACY_CR3_MASK);
    pt->gva = addr;
    pt->user_access = (err_code & MMU_PAGE_US);
    pt->write_access = (err_code & MMU_PAGE_WT);
    pt->exec_access = (err_code & MMU_PAGE_NX);
    
    for (level = top_level; level > 0; level--) {
        get_pt_entry(cpu, pt, level, pae);

        if (!test_pt_entry(cpu, pt, level - 1, &is_large, pae)) {
            return false;
        }

        if (is_large) {
            break;
        }
    }

    if (!is_large) {
        pt->gpa = (pt->pte[0] & page_mask) | (pt->gva & 0xfff);
    } else {
        pt->gpa = large_page_gpa(pt, pae);
    }

    return true;
}


bool mmu_gva_to_gpa(struct CPUState *cpu, target_ulong gva, uint64_t *gpa)
{
    bool res;
    struct gpt_translation pt;
    int err_code = 0;

    if (!x86_is_paging_mode(cpu)) {
        *gpa = gva;
        return true;
    }

    res = walk_gpt(cpu, gva, err_code, &pt, x86_is_pae_enabled(cpu));
    if (res) {
        *gpa = pt.gpa;
        return true;
    }

    return false;
}

void vmx_write_mem(struct CPUState *cpu, target_ulong gva, void *data, int bytes)
{
    uint64_t gpa;

    while (bytes > 0) {
        /* copy page */
        int copy = MIN(bytes, 0x1000 - (gva & 0xfff));

        if (!mmu_gva_to_gpa(cpu, gva, &gpa)) {
            VM_PANIC_EX("%s: mmu_gva_to_gpa %llx failed\n", __func__, gva);
        } else {
            address_space_rw(&address_space_memory, gpa, MEMTXATTRS_UNSPECIFIED,
                             data, copy, 1);
        }

        bytes -= copy;
        gva += copy;
        data += copy;
    }
}

void vmx_read_mem(struct CPUState *cpu, void *data, target_ulong gva, int bytes)
{
    uint64_t gpa;

    while (bytes > 0) {
        /* copy page */
        int copy = MIN(bytes, 0x1000 - (gva & 0xfff));

        if (!mmu_gva_to_gpa(cpu, gva, &gpa)) {
            VM_PANIC_EX("%s: mmu_gva_to_gpa %llx failed\n", __func__, gva);
        }
        address_space_rw(&address_space_memory, gpa, MEMTXATTRS_UNSPECIFIED,
                         data, copy, 0);

        bytes -= copy;
        gva += copy;
        data += copy;
    }
}
