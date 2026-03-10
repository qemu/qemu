/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "cpu.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "qemu/error-report.h"
#include "emulate/x86.h"
#include "emulate/x86_emu.h"
#include "emulate/x86_mmu.h"

#define pte_present(pte) (pte & PT_PRESENT)
#define pte_write_access(pte) (pte & PT_WRITE)
#define pte_user_access(pte) (pte & PT_USER)
#define pte_exec_access(pte) (!(pte & PT_NX))

#define pte_large_page(pte) (pte & PT_PS)
#define pte_global_access(pte) (pte & PT_GLOBAL)

#define mmu_validate_write(flags) (flags & MMU_TRANSLATE_VALIDATE_WRITE)
#define mmu_validate_execute(flags) (flags & MMU_TRANSLATE_VALIDATE_EXECUTE)
#define mmu_priv_checks_exempt(flags) (flags & MMU_TRANSLATE_PRIV_CHECKS_EXEMPT)


#define PAE_CR3_MASK                (~0x1fllu)
#define LEGACY_CR3_MASK             (0xffffffff)

#define LEGACY_PTE_PAGE_MASK        (0xffffffffllu << 12)
#define PAE_PTE_PAGE_MASK           ((-1llu << 12) & ((1llu << 52) - 1))
#define PAE_PTE_LARGE_PAGE_MASK     ((-1llu << (21)) & ((1llu << 52) - 1))
#define PAE_PTE_SUPER_PAGE_MASK     ((-1llu << (30)) & ((1llu << 52) - 1))

static bool is_user(CPUState *cpu)
{
    return false;
}


struct gpt_translation {
    target_ulong  gva;
    uint64_t gpa;
    uint64_t pte[6];
};

static int gpt_top_level(CPUState *cpu, bool pae)
{
    if (!pae) {
        return 2;
    }
    if (x86_is_long_mode(cpu)) {
        if (x86_is_la57(cpu)) {
            return 5;
        }
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


static bool get_pt_entry(CPUState *cpu, struct gpt_translation *pt,
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
    address_space_read(&address_space_memory, gpa + index * pte_size(pae),
                       MEMTXATTRS_UNSPECIFIED, &pte, pte_size(pae));

    pt->pte[level - 1] = pte;

    return true;
}

/* test page table entry */
static MMUTranslateResult test_pt_entry(CPUState *cpu, struct gpt_translation *pt,
                          int level, int *largeness, bool pae, MMUTranslateFlags flags)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint64_t pte = pt->pte[level];

    if (!pte_present(pte)) {
        return MMU_TRANSLATE_PAGE_NOT_MAPPED;
    }

    if (pae && !x86_is_long_mode(cpu) && 2 == level) {
        goto exit;
    }

    if (level && pte_large_page(pte)) {
        *largeness = level;
    }

    uint32_t cr0 = env->cr[0];
    /* check protection */
    if (cr0 & CR0_WP_MASK) {
        if (mmu_validate_write(flags) && !pte_write_access(pte)) {
            return MMU_TRANSLATE_PRIV_VIOLATION;
        }
    }

    if (!mmu_priv_checks_exempt(flags)) {
        if (is_user(cpu) && !pte_user_access(pte)) {
            return MMU_TRANSLATE_PRIV_VIOLATION;
        }
    }

    if (pae && mmu_validate_execute(flags) && !pte_exec_access(pte)) {
        return MMU_TRANSLATE_PRIV_VIOLATION;
    }
    
exit:
    /* TODO: check reserved bits */
    return MMU_TRANSLATE_SUCCESS;
}

static inline uint64_t pse_pte_to_page(uint64_t pte)
{
    return ((pte & 0x1fe000) << 19) | (pte & 0xffc00000);
}

static inline uint64_t large_page_gpa(struct gpt_translation *pt, bool pae,
                                      int largeness)
{
    VM_PANIC_ON(!pte_large_page(pt->pte[largeness]))

    /* 1Gib large page  */
    if (pae && largeness == 2) {
        return (pt->pte[2] & PAE_PTE_SUPER_PAGE_MASK) | (pt->gva & 0x3fffffff);
    }

    VM_PANIC_ON(largeness != 1)

    /* 2Mb large page  */
    if (pae) {
        return (pt->pte[1] & PAE_PTE_LARGE_PAGE_MASK) | (pt->gva & 0x1fffff);
    }

    /* 4Mb large page */
    return pse_pte_to_page(pt->pte[1]) | (pt->gva & 0x3fffff);
}



static MMUTranslateResult walk_gpt(CPUState *cpu, target_ulong addr, MMUTranslateFlags flags,
                     struct gpt_translation *pt, bool pae)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    int top_level, level;
    int largeness = 0;
    target_ulong cr3 = env->cr[3];
    uint64_t page_mask = pae ? PAE_PTE_PAGE_MASK : LEGACY_PTE_PAGE_MASK;
    MMUTranslateResult res;
    
    memset(pt, 0, sizeof(*pt));
    top_level = gpt_top_level(cpu, pae);

    pt->pte[top_level] = pae ? (cr3 & PAE_CR3_MASK) : (cr3 & LEGACY_CR3_MASK);
    pt->gva = addr;
    
    for (level = top_level; level > 0; level--) {
        get_pt_entry(cpu, pt, level, pae);
        res = test_pt_entry(cpu, pt, level - 1, &largeness, pae, flags);

        if (res) {
            return res;
        }

        if (largeness) {
            break;
        }
    }

    if (!largeness) {
        pt->gpa = (pt->pte[0] & page_mask) | (pt->gva & 0xfff);
    } else {
        pt->gpa = large_page_gpa(pt, pae, largeness);
    }

    return res;
}


MMUTranslateResult mmu_gva_to_gpa(CPUState *cpu, target_ulong gva, uint64_t *gpa, MMUTranslateFlags flags)
{
    if (emul_ops->mmu_gva_to_gpa) {
        return emul_ops->mmu_gva_to_gpa(cpu, gva, gpa, flags);
    }

    bool res;
    struct gpt_translation pt;

    if (!x86_is_paging_mode(cpu)) {
        *gpa = gva;
        return MMU_TRANSLATE_SUCCESS;
    }

    res = walk_gpt(cpu, gva, flags, &pt, x86_is_pae_enabled(cpu));
    if (res == MMU_TRANSLATE_SUCCESS) {
        *gpa = pt.gpa;
    }

    return res;
}

static int translate_res_to_error_code(MMUTranslateResult res, bool is_write, bool is_user)
{
    int error_code = 0;
    if (is_user) {
        error_code |= PG_ERROR_U_MASK;
    }
    if (!(res & MMU_TRANSLATE_PAGE_NOT_MAPPED)) {
        error_code |= PG_ERROR_P_MASK;
    }
    if (is_write && (res & MMU_TRANSLATE_PRIV_VIOLATION)) {
        error_code |= PG_ERROR_W_MASK;
    }
    if (res & MMU_TRANSLATE_INVALID_PT_FLAGS) {
        error_code |= PG_ERROR_RSVD_MASK;
    }
    return error_code;
}

static MMUTranslateResult x86_write_mem_ex(CPUState *cpu, void *data, target_ulong gva, int bytes, bool priv_check_exempt)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    MMUTranslateResult translate_res = MMU_TRANSLATE_SUCCESS;
    MemTxResult mem_tx_res;
    uint64_t gpa;

    while (bytes > 0) {
        /* copy page */
        int copy = MIN(bytes, 0x1000 - (gva & 0xfff));

        translate_res = mmu_gva_to_gpa(cpu, gva, &gpa, MMU_TRANSLATE_VALIDATE_WRITE);
        if (translate_res) {
            int error_code = translate_res_to_error_code(translate_res, true, is_user(cpu));
            env->cr[2] = gva;
            x86_emul_raise_exception(env, EXCP0E_PAGE, error_code);
            return translate_res;
        }

        mem_tx_res = address_space_write(&address_space_memory, gpa,
                            MEMTXATTRS_UNSPECIFIED, data, copy);

        if (mem_tx_res == MEMTX_DECODE_ERROR) {
            warn_report("write to unmapped mmio region gpa=0x%" PRIx64 " size=%i", gpa, bytes);
            return MMU_TRANSLATE_GPA_UNMAPPED;
        } else if (mem_tx_res == MEMTX_ACCESS_ERROR) {
            return MMU_TRANSLATE_GPA_NO_WRITE_ACCESS;
        }

        bytes -= copy;
        gva += copy;
        data += copy;
    }
    return translate_res;
}

MMUTranslateResult x86_write_mem(CPUState *cpu, void *data, target_ulong gva, int bytes)
{
    return x86_write_mem_ex(cpu, data, gva, bytes, false);
}

MMUTranslateResult x86_write_mem_priv(CPUState *cpu, void *data, target_ulong gva, int bytes)
{
    return x86_write_mem_ex(cpu, data, gva, bytes, true);
}

static MMUTranslateResult x86_read_mem_ex(CPUState *cpu, void *data, target_ulong gva, int bytes, bool priv_check_exempt)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    MMUTranslateResult translate_res = MMU_TRANSLATE_SUCCESS;
    MemTxResult mem_tx_res;
    uint64_t gpa;

    while (bytes > 0) {
        /* copy page */
        int copy = MIN(bytes, 0x1000 - (gva & 0xfff));

        translate_res = mmu_gva_to_gpa(cpu, gva, &gpa, 0);
        if (translate_res) {
            int error_code = translate_res_to_error_code(translate_res, false, is_user(cpu));
            env->cr[2] = gva;
            x86_emul_raise_exception(env, EXCP0E_PAGE, error_code);
            return translate_res;
        }
        mem_tx_res = address_space_read(&address_space_memory, gpa, MEMTXATTRS_UNSPECIFIED,
                           data, copy);

        if (mem_tx_res == MEMTX_DECODE_ERROR) {
            warn_report("read from unmapped mmio region gpa=0x%" PRIx64 " size=%i", gpa, bytes);
            return MMU_TRANSLATE_GPA_UNMAPPED;
        } else if (mem_tx_res == MEMTX_ACCESS_ERROR) {
            return MMU_TRANSLATE_GPA_NO_READ_ACCESS;
        }

        bytes -= copy;
        gva += copy;
        data += copy;
    }
    return translate_res;
}

MMUTranslateResult x86_read_mem(CPUState *cpu, void *data, target_ulong gva, int bytes)
{
    return x86_read_mem_ex(cpu, data, gva, bytes, false);
}

MMUTranslateResult x86_read_mem_priv(CPUState *cpu, void *data, target_ulong gva, int bytes)
{
    return x86_read_mem_ex(cpu, data, gva, bytes, true);
}
