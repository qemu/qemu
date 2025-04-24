/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch CPU helpers for qemu
 *
 * Copyright (c) 2024 Loongson Technology Corporation Limited
 *
 */

#include "qemu/osdep.h"
#include "system/tcg.h"
#include "cpu.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/target_page.h"
#include "internals.h"
#include "cpu-csr.h"
#include "tcg/tcg_loongarch.h"

void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                        uint64_t *dir_width, target_ulong level)
{
    switch (level) {
    case 1:
        *dir_base = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, DIR1_BASE);
        *dir_width = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, DIR1_WIDTH);
        break;
    case 2:
        *dir_base = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, DIR2_BASE);
        *dir_width = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, DIR2_WIDTH);
        break;
    case 3:
        *dir_base = FIELD_EX64(env->CSR_PWCH, CSR_PWCH, DIR3_BASE);
        *dir_width = FIELD_EX64(env->CSR_PWCH, CSR_PWCH, DIR3_WIDTH);
        break;
    case 4:
        *dir_base = FIELD_EX64(env->CSR_PWCH, CSR_PWCH, DIR4_BASE);
        *dir_width = FIELD_EX64(env->CSR_PWCH, CSR_PWCH, DIR4_WIDTH);
        break;
    default:
        /* level may be zero for ldpte */
        *dir_base = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, PTBASE);
        *dir_width = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, PTWIDTH);
        break;
    }
}

static int loongarch_page_table_walker(CPULoongArchState *env, hwaddr *physical,
                                 int *prot, target_ulong address)
{
    CPUState *cs = env_cpu(env);
    target_ulong index, phys;
    uint64_t dir_base, dir_width;
    uint64_t base;
    int level;

    if ((address >> 63) & 0x1) {
        base = env->CSR_PGDH;
    } else {
        base = env->CSR_PGDL;
    }
    base &= TARGET_PHYS_MASK;

    for (level = 4; level > 0; level--) {
        get_dir_base_width(env, &dir_base, &dir_width, level);

        if (dir_width == 0) {
            continue;
        }

        /* get next level page directory */
        index = (address >> dir_base) & ((1 << dir_width) - 1);
        phys = base | index << 3;
        base = ldq_phys(cs->as, phys) & TARGET_PHYS_MASK;
        if (FIELD_EX64(base, TLBENTRY, HUGE)) {
            /* base is a huge pte */
            break;
        }
    }

    /* pte */
    if (FIELD_EX64(base, TLBENTRY, HUGE)) {
        /* Huge Page. base is pte */
        base = FIELD_DP64(base, TLBENTRY, LEVEL, 0);
        base = FIELD_DP64(base, TLBENTRY, HUGE, 0);
        if (FIELD_EX64(base, TLBENTRY, HGLOBAL)) {
            base = FIELD_DP64(base, TLBENTRY, HGLOBAL, 0);
            base = FIELD_DP64(base, TLBENTRY, G, 1);
        }
    } else {
        /* Normal Page. base points to pte */
        get_dir_base_width(env, &dir_base, &dir_width, 0);
        index = (address >> dir_base) & ((1 << dir_width) - 1);
        phys = base | index << 3;
        base = ldq_phys(cs->as, phys);
    }

    /* TODO: check plv and other bits? */

    /* base is pte, in normal pte format */
    if (!FIELD_EX64(base, TLBENTRY, V)) {
        return TLBRET_NOMATCH;
    }

    if (!FIELD_EX64(base, TLBENTRY, D)) {
        *prot = PAGE_READ;
    } else {
        *prot = PAGE_READ | PAGE_WRITE;
    }

    /* get TARGET_PAGE_SIZE aligned physical address */
    base += (address & TARGET_PHYS_MASK) & ((1 << dir_base) - 1);
    /* mask RPLV, NX, NR bits */
    base = FIELD_DP64(base, TLBENTRY_64, RPLV, 0);
    base = FIELD_DP64(base, TLBENTRY_64, NX, 0);
    base = FIELD_DP64(base, TLBENTRY_64, NR, 0);
    /* mask other attribute bits */
    *physical = base & TARGET_PAGE_MASK;

    return 0;
}

static int loongarch_map_address(CPULoongArchState *env, hwaddr *physical,
                                 int *prot, target_ulong address,
                                 MMUAccessType access_type, int mmu_idx,
                                 int is_debug)
{
    int ret;

    if (tcg_enabled()) {
        ret = loongarch_get_addr_from_tlb(env, physical, prot, address,
                                          access_type, mmu_idx);
        if (ret != TLBRET_NOMATCH) {
            return ret;
        }
    }

    if (is_debug) {
        /*
         * For debugger memory access, we want to do the map when there is a
         * legal mapping, even if the mapping is not yet in TLB. return 0 if
         * there is a valid map, else none zero.
         */
        return loongarch_page_table_walker(env, physical, prot, address);
    }

    return TLBRET_NOMATCH;
}

static hwaddr dmw_va2pa(CPULoongArchState *env, target_ulong va,
                        target_ulong dmw)
{
    if (is_la64(env)) {
        return va & TARGET_VIRT_MASK;
    } else {
        uint32_t pseg = FIELD_EX32(dmw, CSR_DMW_32, PSEG);
        return (va & MAKE_64BIT_MASK(0, R_CSR_DMW_32_VSEG_SHIFT)) | \
            (pseg << R_CSR_DMW_32_VSEG_SHIFT);
    }
}

int get_physical_address(CPULoongArchState *env, hwaddr *physical,
                         int *prot, target_ulong address,
                         MMUAccessType access_type, int mmu_idx, int is_debug)
{
    int user_mode = mmu_idx == MMU_USER_IDX;
    int kernel_mode = mmu_idx == MMU_KERNEL_IDX;
    uint32_t plv, base_c, base_v;
    int64_t addr_high;
    uint8_t da = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, DA);
    uint8_t pg = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PG);

    /* Check PG and DA */
    if (da & !pg) {
        *physical = address & TARGET_PHYS_MASK;
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TLBRET_MATCH;
    }

    plv = kernel_mode | (user_mode << R_CSR_DMW_PLV3_SHIFT);
    if (is_la64(env)) {
        base_v = address >> R_CSR_DMW_64_VSEG_SHIFT;
    } else {
        base_v = address >> R_CSR_DMW_32_VSEG_SHIFT;
    }
    /* Check direct map window */
    for (int i = 0; i < 4; i++) {
        if (is_la64(env)) {
            base_c = FIELD_EX64(env->CSR_DMW[i], CSR_DMW_64, VSEG);
        } else {
            base_c = FIELD_EX64(env->CSR_DMW[i], CSR_DMW_32, VSEG);
        }
        if ((plv & env->CSR_DMW[i]) && (base_c == base_v)) {
            *physical = dmw_va2pa(env, address, env->CSR_DMW[i]);
            *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            return TLBRET_MATCH;
        }
    }

    /* Check valid extension */
    addr_high = sextract64(address, TARGET_VIRT_ADDR_SPACE_BITS, 16);
    if (!(addr_high == 0 || addr_high == -1)) {
        return TLBRET_BADADDR;
    }

    /* Mapped address */
    return loongarch_map_address(env, physical, prot, address,
                                 access_type, mmu_idx, is_debug);
}

hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    CPULoongArchState *env = cpu_env(cs);
    hwaddr phys_addr;
    int prot;

    if (get_physical_address(env, &phys_addr, &prot, addr, MMU_DATA_LOAD,
                             cpu_mmu_index(cs, false), 1) != 0) {
        return -1;
    }
    return phys_addr;
}
