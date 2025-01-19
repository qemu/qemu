/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch CPU helpers for qemu
 *
 * Copyright (c) 2024 Loongson Technology Corporation Limited
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-csr.h"

#ifdef CONFIG_TCG
static int loongarch_map_tlb_entry(CPULoongArchState *env, hwaddr *physical,
                                   int *prot, target_ulong address,
                                   int access_type, int index, int mmu_idx)
{
    LoongArchTLB *tlb = &env->tlb[index];
    uint64_t plv = mmu_idx;
    uint64_t tlb_entry, tlb_ppn;
    uint8_t tlb_ps, n, tlb_v, tlb_d, tlb_plv, tlb_nx, tlb_nr, tlb_rplv;

    if (index >= LOONGARCH_STLB) {
        tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    } else {
        tlb_ps = FIELD_EX64(env->CSR_STLBPS, CSR_STLBPS, PS);
    }
    n = (address >> tlb_ps) & 0x1;/* Odd or even */

    tlb_entry = n ? tlb->tlb_entry1 : tlb->tlb_entry0;
    tlb_v = FIELD_EX64(tlb_entry, TLBENTRY, V);
    tlb_d = FIELD_EX64(tlb_entry, TLBENTRY, D);
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
    tlb_ppn = tlb_ppn & ~(((0x1UL << (tlb_ps - 12)) -1));

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

    *physical = (tlb_ppn << R_TLBENTRY_64_PPN_SHIFT) |
                (address & MAKE_64BIT_MASK(0, tlb_ps));
    *prot = PAGE_READ;
    if (tlb_d) {
        *prot |= PAGE_WRITE;
    }
    if (!tlb_nx) {
        *prot |= PAGE_EXEC;
    }
    return TLBRET_MATCH;
}

/*
 * One tlb entry holds an adjacent odd/even pair, the vpn is the
 * content of the virtual page number divided by 2. So the
 * compare vpn is bit[47:15] for 16KiB page. while the vppn
 * field in tlb entry contains bit[47:13], so need adjust.
 * virt_vpn = vaddr[47:13]
 */
bool loongarch_tlb_search(CPULoongArchState *env, target_ulong vaddr,
                          int *index)
{
    LoongArchTLB *tlb;
    uint16_t csr_asid, tlb_asid, stlb_idx;
    uint8_t tlb_e, tlb_ps, tlb_g, stlb_ps;
    int i, compare_shift;
    uint64_t vpn, tlb_vppn;

    csr_asid = FIELD_EX64(env->CSR_ASID, CSR_ASID, ASID);
    stlb_ps = FIELD_EX64(env->CSR_STLBPS, CSR_STLBPS, PS);
    vpn = (vaddr & TARGET_VIRT_MASK) >> (stlb_ps + 1);
    stlb_idx = vpn & 0xff; /* VA[25:15] <==> TLBIDX.index for 16KiB Page */
    compare_shift = stlb_ps + 1 - R_TLB_MISC_VPPN_SHIFT;

    /* Search STLB */
    for (i = 0; i < 8; ++i) {
        tlb = &env->tlb[i * 256 + stlb_idx];
        tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
        if (tlb_e) {
            tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);

            if ((tlb_g == 1 || tlb_asid == csr_asid) &&
                (vpn == (tlb_vppn >> compare_shift))) {
                *index = i * 256 + stlb_idx;
                return true;
            }
        }
    }

    /* Search MTLB */
    for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; ++i) {
        tlb = &env->tlb[i];
        tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
        if (tlb_e) {
            tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
            tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            compare_shift = tlb_ps + 1 - R_TLB_MISC_VPPN_SHIFT;
            vpn = (vaddr & TARGET_VIRT_MASK) >> (tlb_ps + 1);
            if ((tlb_g == 1 || tlb_asid == csr_asid) &&
                (vpn == (tlb_vppn >> compare_shift))) {
                *index = i;
                return true;
            }
        }
    }
    return false;
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
    int index, match;

    match = loongarch_tlb_search(env, address, &index);
    if (match) {
        return loongarch_map_tlb_entry(env, physical, prot,
                                       address, access_type, index, mmu_idx);
    } else if (is_debug) {
        /*
         * For debugger memory access, we want to do the map when there is a
         * legal mapping, even if the mapping is not yet in TLB. return 0 if
         * there is a valid map, else none zero.
         */
        return loongarch_page_table_walker(env, physical, prot, address);
    }

    return TLBRET_NOMATCH;
}
#else
static int loongarch_map_address(CPULoongArchState *env, hwaddr *physical,
                                 int *prot, target_ulong address,
                                 MMUAccessType access_type, int mmu_idx,
                                 int is_debug)
{
    return TLBRET_NOMATCH;
}
#endif

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
