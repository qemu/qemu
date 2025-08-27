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
#include "cpu-mmu.h"
#include "tcg/tcg_loongarch.h"

void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                        uint64_t *dir_width, unsigned int level)
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

static MemTxResult loongarch_cmpxchg_phys(CPUState *cs, hwaddr phys,
                                          uint64_t old, uint64_t new)
{
    hwaddr addr1, l = 8;
    MemoryRegion *mr;
    uint8_t *ram_ptr;
    uint64_t old1;
    MemTxResult ret;

    rcu_read_lock();
    mr = address_space_translate(cs->as, phys, &addr1, &l,
                                 false, MEMTXATTRS_UNSPECIFIED);
    if (!memory_region_is_ram(mr)) {
        /*
         * Misconfigured PTE in ROM (AD bits are not preset) or
         * PTE is in IO space and can't be updated atomically.
         */
         rcu_read_unlock();
         return MEMTX_ACCESS_ERROR;
    }

    ram_ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
    old1 = qatomic_cmpxchg((uint64_t *)ram_ptr, cpu_to_le64(old),
                           cpu_to_le64(new));
    old1 = le64_to_cpu(old1);
    if (old1 == old) {
        ret = MEMTX_OK;
    } else {
        ret = MEMTX_DECODE_ERROR;
    }
    rcu_read_unlock();

    return ret;
}

TLBRet loongarch_ptw(CPULoongArchState *env, MMUContext *context,
                     int access_type, int mmu_idx, int debug)
{
    CPUState *cs = env_cpu(env);
    target_ulong index = 0, phys = 0;
    uint64_t dir_base, dir_width;
    uint64_t base, pte;
    int level;
    vaddr address;
    TLBRet ret;
    MemTxResult ret1;

    address = context->addr;
    if ((address >> 63) & 0x1) {
        base = env->CSR_PGDH;
    } else {
        base = env->CSR_PGDL;
    }
    base &= TARGET_PHYS_MASK;

    for (level = 4; level >= 0; level--) {
        get_dir_base_width(env, &dir_base, &dir_width, level);

        if (dir_width == 0) {
            continue;
        }

        /* get next level page directory */
        index = (address >> dir_base) & ((1 << dir_width) - 1);
        phys = base | index << 3;
        base = ldq_phys(cs->as, phys);
        if (level) {
            if (FIELD_EX64(base, TLBENTRY, HUGE)) {
                /* base is a huge pte */
                index = 0;
                dir_base -= 1;
                break;
            } else {
                /* Discard high bits with page directory table */
                base &= TARGET_PHYS_MASK;
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
        index &= 1;
        context->pte_buddy[index] = base;
        context->pte_buddy[1 - index] = ldq_phys(cs->as,
                                            phys + 8 * (1 - 2 * index));
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
        ret1 = loongarch_cmpxchg_phys(cs, phys, pte, base);
        /* PTE updated by other CPU, reload PTE entry */
        if (ret1 == MEMTX_DECODE_ERROR) {
            base = ldq_phys(cs->as, phys);
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

static hwaddr dmw_va2pa(CPULoongArchState *env, vaddr va, target_ulong dmw)
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
    uint8_t da = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, DA);
    uint8_t pg = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PG);
    vaddr address;

    /* Check PG and DA */
    address = context->addr;
    if (da & !pg) {
        context->physical = address & TARGET_PHYS_MASK;
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
            base_c = FIELD_EX64(env->CSR_DMW[i], CSR_DMW_64, VSEG);
        } else {
            base_c = FIELD_EX64(env->CSR_DMW[i], CSR_DMW_32, VSEG);
        }
        if ((plv & env->CSR_DMW[i]) && (base_c == base_v)) {
            context->physical = dmw_va2pa(env, address, env->CSR_DMW[i]);
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

hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    CPULoongArchState *env = cpu_env(cs);
    MMUContext context;

    context.addr = addr;
    if (get_physical_address(env, &context, MMU_DATA_LOAD,
                             cpu_mmu_index(cs, false), 1) != TLBRET_MATCH) {
        return -1;
    }
    return context.physical;
}
