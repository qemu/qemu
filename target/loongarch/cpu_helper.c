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
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/getpc.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "internals.h"
#include "cpu-csr.h"
#include "cpu-mmu.h"
#include "tcg/tcg_loongarch.h"

void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                        uint64_t *dir_width, unsigned int level)
{
    CPUSysState *sys = env_sys(env);

    switch (level) {
    case 1:
        *dir_base = FIELD_EX64(sys->CSR_PWCL, CSR_PWCL, DIR1_BASE);
        *dir_width = FIELD_EX64(sys->CSR_PWCL, CSR_PWCL, DIR1_WIDTH);
        break;
    case 2:
        *dir_base = FIELD_EX64(sys->CSR_PWCL, CSR_PWCL, DIR2_BASE);
        *dir_width = FIELD_EX64(sys->CSR_PWCL, CSR_PWCL, DIR2_WIDTH);
        break;
    case 3:
        *dir_base = FIELD_EX64(sys->CSR_PWCH, CSR_PWCH, DIR3_BASE);
        *dir_width = FIELD_EX64(sys->CSR_PWCH, CSR_PWCH, DIR3_WIDTH);
        break;
    case 4:
        *dir_base = FIELD_EX64(sys->CSR_PWCH, CSR_PWCH, DIR4_BASE);
        *dir_width = FIELD_EX64(sys->CSR_PWCH, CSR_PWCH, DIR4_WIDTH);
        break;
    default:
        /* level may be zero for ldpte */
        *dir_base = FIELD_EX64(sys->CSR_PWCL, CSR_PWCL, PTBASE);
        *dir_width = FIELD_EX64(sys->CSR_PWCL, CSR_PWCL, PTWIDTH);
        break;
    }
}

TLBRet loongarch_check_pte(CPULoongArchState *env, MMUContext *context,
                           MMUAccessType access_type, int mmu_idx)
{
    uint64_t plv = mmu_idx;
    uint64_t tlb_entry, tlb_ppn;
    uint8_t tlb_ps, tlb_plv, tlb_nx, tlb_nr, tlb_rplv;
    bool tlb_v, tlb_d;

    tlb_entry = context->pte;
    tlb_ps = context->ps;
    tlb_v = pte_present(env, tlb_entry);
    tlb_d = pte_write(env, tlb_entry);
    tlb_plv = FIELD_EX64(tlb_entry, TLBENTRY, PLV);
    if (is_la64(env)) {
        tlb_ppn = FIELD_EX64(tlb_entry, TLBENTRY_64, PPN);
        tlb_nx = FIELD_EX64(tlb_entry, TLBENTRY_64, NX);
        tlb_nr = FIELD_EX64(tlb_entry, TLBENTRY_64, NR);
        tlb_rplv = FIELD_EX64(tlb_entry, TLBENTRY_64, RPLV);
    } else {
        tlb_ppn = FIELD_EX64(tlb_entry, TLBENTRY_32, PPN);
        tlb_nx = 0;
        tlb_nr = 0;
        tlb_rplv = 0;
    }

    /* Remove sw bit between bit12 -- bit PS*/
    tlb_ppn = tlb_ppn & ~(((0x1UL << (tlb_ps - 12)) - 1));

    /* Check access rights */
    if (!tlb_v) {
        return TLBRET_INVALID;
    }

    if (access_type == MMU_INST_FETCH && tlb_nx) {
        return TLBRET_XI;
    }

    if (access_type == MMU_DATA_LOAD && tlb_nr) {
        return TLBRET_RI;
    }

    if (((tlb_rplv == 0) && (plv > tlb_plv)) ||
        ((tlb_rplv == 1) && (plv != tlb_plv))) {
        return TLBRET_PE;
    }

    if ((access_type == MMU_DATA_STORE) && !tlb_d) {
        return TLBRET_DIRTY;
    }

    context->physical = (tlb_ppn << R_TLBENTRY_64_PPN_SHIFT) |
                        (context->addr & MAKE_64BIT_MASK(0, tlb_ps));
    context->prot = PAGE_READ;
    context->mmu_index = tlb_plv;
    if (tlb_d) {
        context->prot |= PAGE_WRITE;
    }
    if (!tlb_nx) {
        context->prot |= PAGE_EXEC;
    }
    return TLBRET_MATCH;
}

TLBRet loongarch_ptw(CPULoongArchState *env, MMUContext *context,
                     int access_type, int mmu_idx, int debug)
{
    hwaddr index = 0, phys = 0;
    uint64_t palen_mask = loongarch_palen_mask(env);
    uint64_t dir_base, dir_width;
    uint64_t base, pte, old;
    int level, ptw_idx = MMU_DA_IDX;
    vaddr address;
    TLBRet ret;
    CPUSysState *sys = env_sys(env);
    MemOpIdx oi;

    address = context->addr;
    if ((address >> 63) & 0x1) {
        base = sys->CSR_PGDH;
    } else {
        base = sys->CSR_PGDL;
    }
    base &= palen_mask;

    for (level = 4; level >= 0; level--) {
        get_dir_base_width(env, &dir_base, &dir_width, level);

        if (dir_width == 0) {
            continue;
        }

        /* get next level page directory */
        index = (address >> dir_base) & ((1 << dir_width) - 1);
        phys = base | index << 3;
        base = cpu_ldq_le_mmuidx_ra(env, phys, ptw_idx, GETPC());
        if (level) {
            if (FIELD_EX64(base, TLBENTRY, HUGE)) {
                /* base is a huge pte */
                index = 0;
                dir_base -= 1;
                break;
            } else {
                /* Discard high bits with page directory table */
                base &= palen_mask;
            }
        }
    }

restart:
    /* pte */
    pte = base;
    if (level > 0) {
        /* Huge Page. base is pte */
        base = FIELD_DP64(base, TLBENTRY, LEVEL, 0);
        base = FIELD_DP64(base, TLBENTRY, HUGE, 0);
        if (FIELD_EX64(base, TLBENTRY, HGLOBAL)) {
            base = FIELD_DP64(base, TLBENTRY, HGLOBAL, 0);
            base = FIELD_DP64(base, TLBENTRY, G, 1);
        }

        context->pte_buddy[index] = base;
        context->pte_buddy[1 - index] = base + BIT_ULL(dir_base);
        base += (BIT_ULL(dir_base) & address);
    } else if (cpu_has_ptw(env)) {
        uint64_t val;

        index &= 1;
        context->pte_buddy[index] = base;
        val = cpu_ldq_le_mmuidx_ra(env, phys + 8 * (1 - 2 * index),
                                   ptw_idx, GETPC());
        context->pte_buddy[1 - index] = val;
    }

    context->ps = dir_base;
    context->pte = base;
    ret = loongarch_check_pte(env, context, access_type, mmu_idx);
    if (debug) {
        return ret;
    }

    /*
     * Update bit A/D with hardware PTW supported
     *
     * Need atomic compchxg operation with pte update, other vCPUs may
     * update pte at the same time.
     */
    if (ret == TLBRET_MATCH && cpu_has_ptw(env)) {
        if (access_type == MMU_DATA_STORE && pte_dirty(base)) {
            return ret;
        }

        if (access_type != MMU_DATA_STORE && pte_access(base)) {
            return ret;
        }

        base = pte_mkaccess(pte);
        if (access_type == MMU_DATA_STORE) {
            base = pte_mkdirty(base);
        }

        oi = make_memop_idx(MO_LEUQ | MO_64, ptw_idx);
        old = cpu_atomic_cmpxchgq_le_mmu(env, phys, pte, base, oi, GETPC());
        /* PTE updated by other CPU, reload PTE entry */
        if (le64_to_cpu(old) != pte) {
            base = cpu_ldq_le_mmuidx_ra(env, phys, ptw_idx, GETPC());
            goto restart;
        }

        base = context->pte_buddy[index];
        base = pte_mkaccess(base);
        if (access_type == MMU_DATA_STORE) {
            base = pte_mkdirty(base);
        }
        context->pte_buddy[index] = base;

        /* Bit A/D need be updated with both Even/Odd page with huge pte */
        if (level > 0) {
            index = 1 - index;
            base = context->pte_buddy[index];
            base = pte_mkaccess(base);
            if (access_type == MMU_DATA_STORE) {
                base = pte_mkdirty(base);
            }
            context->pte_buddy[index] = base;
        }
    }

    return ret;
}

static TLBRet loongarch_map_address(CPULoongArchState *env,
                                    MMUContext *context,
                                    MMUAccessType access_type, int mmu_idx,
                                    int is_debug)
{
    TLBRet ret;

    if (tcg_enabled()) {
        ret = loongarch_get_addr_from_tlb(env, context, access_type, mmu_idx);
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
        return loongarch_ptw(env, context, access_type, mmu_idx, is_debug);
    }

    return TLBRET_NOMATCH;
}

static hwaddr dmw_va2pa(CPULoongArchState *env, vaddr va, uint64_t dmw)
{
    if (is_la64(env)) {
        return va & TARGET_VIRT_MASK;
    } else {
        uint32_t pseg = FIELD_EX32(dmw, CSR_DMW_32, PSEG);
        return (va & MAKE_64BIT_MASK(0, R_CSR_DMW_32_VSEG_SHIFT)) | \
            (pseg << R_CSR_DMW_32_VSEG_SHIFT);
    }
}

TLBRet get_physical_address(CPULoongArchState *env, MMUContext *context,
                            MMUAccessType access_type, int mmu_idx,
                            int is_debug)
{
    int user_mode = mmu_idx == MMU_USER_IDX;
    int kernel_mode = mmu_idx == MMU_KERNEL_IDX;
    uint32_t plv, base_c, base_v;
    int64_t addr_high;
    CPUSysState *sys = env_sys(env);
    vaddr address;

    /* Check PG and DA */
    address = context->addr;
    if (mmu_idx == MMU_DA_IDX) {
        context->physical = address & loongarch_palen_mask(env);
        context->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        context->mmu_index = MMU_DA_IDX;
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
            base_c = FIELD_EX64(sys->CSR_DMW[i], CSR_DMW_64, VSEG);
        } else {
            base_c = FIELD_EX64(sys->CSR_DMW[i], CSR_DMW_32, VSEG);
        }
        if ((plv & sys->CSR_DMW[i]) && (base_c == base_v)) {
            context->physical = dmw_va2pa(env, address, sys->CSR_DMW[i]);
            context->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            context->mmu_index = MMU_DA_IDX;
            return TLBRET_MATCH;
        }
    }

    /* Check valid extension */
    addr_high = (int64_t)address >> (TARGET_VIRT_ADDR_SPACE_BITS - 1);
    if (!(addr_high == 0 || addr_high == -1ULL)) {
        return TLBRET_BADADDR;
    }

    /* Mapped address */
    return loongarch_map_address(env, context, access_type, mmu_idx, is_debug);
}

bool loongarch_cpu_translate_for_debug(CPUState *cs, vaddr addr,
                                       TranslateForDebugResult *result)
{
    CPULoongArchState *env = cpu_env(cs);
    MMUContext context;

    context.addr = addr;
    if (get_physical_address(env, &context, MMU_DATA_LOAD,
                             cpu_mmu_index(cs, false), 1) != TLBRET_MATCH) {
        return false;
    }
    *result = (TranslateForDebugResult) {
        .physaddr = context.physical,
        .attrs.debug = 1,
        .lg_page_size = TARGET_PAGE_BITS,
    };
    return true;
}

uint64_t loongarch_palen_mask(CPULoongArchState *env)
{
    /* PALEN stores physical address bits - 1 */
    uint64_t phys_bits = FIELD_EX32(env->cpucfg[1], CPUCFG1, PALEN) + 1;
    return MAKE_64BIT_MASK(0, phys_bits);
}
