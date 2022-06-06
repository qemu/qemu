/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch TLB helpers
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"
#include "cpu-csr.h"

enum {
    TLBRET_MATCH = 0,
    TLBRET_BADADDR = 1,
    TLBRET_NOMATCH = 2,
    TLBRET_INVALID = 3,
    TLBRET_DIRTY = 4,
    TLBRET_RI = 5,
    TLBRET_XI = 6,
    TLBRET_PE = 7,
};

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
    tlb_ppn = FIELD_EX64(tlb_entry, TLBENTRY, PPN);
    tlb_nx = FIELD_EX64(tlb_entry, TLBENTRY, NX);
    tlb_nr = FIELD_EX64(tlb_entry, TLBENTRY, NR);
    tlb_rplv = FIELD_EX64(tlb_entry, TLBENTRY, RPLV);

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

    /*
     * tlb_entry contains ppn[47:12] while 16KiB ppn is [47:15]
     * need adjust.
     */
    *physical = (tlb_ppn << R_TLBENTRY_PPN_SHIFT) |
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
static bool loongarch_tlb_search(CPULoongArchState *env, target_ulong vaddr,
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

static int loongarch_map_address(CPULoongArchState *env, hwaddr *physical,
                                 int *prot, target_ulong address,
                                 MMUAccessType access_type, int mmu_idx)
{
    int index, match;

    match = loongarch_tlb_search(env, address, &index);
    if (match) {
        return loongarch_map_tlb_entry(env, physical, prot,
                                       address, access_type, index, mmu_idx);
    }

    return TLBRET_NOMATCH;
}

static int get_physical_address(CPULoongArchState *env, hwaddr *physical,
                                int *prot, target_ulong address,
                                MMUAccessType access_type, int mmu_idx)
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
    base_v = address >> TARGET_VIRT_ADDR_SPACE_BITS;
    /* Check direct map window */
    for (int i = 0; i < 4; i++) {
        base_c = env->CSR_DMW[i] >> TARGET_VIRT_ADDR_SPACE_BITS;
        if ((plv & env->CSR_DMW[i]) && (base_c == base_v)) {
            *physical = dmw_va2pa(address);
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
                                 access_type, mmu_idx);
}

hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    hwaddr phys_addr;
    int prot;

    if (get_physical_address(env, &phys_addr, &prot, addr, MMU_DATA_LOAD,
                             cpu_mmu_index(env, false)) != 0) {
        return -1;
    }
    return phys_addr;
}

static void raise_mmu_exception(CPULoongArchState *env, target_ulong address,
                                MMUAccessType access_type, int tlb_error)
{
    CPUState *cs = env_cpu(env);

    switch (tlb_error) {
    default:
    case TLBRET_BADADDR:
        cs->exception_index = EXCCODE_ADEM;
        break;
    case TLBRET_NOMATCH:
        /* No TLB match for a mapped address */
        if (access_type == MMU_DATA_LOAD) {
            cs->exception_index = EXCCODE_PIL;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PIS;
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PIF;
        }
        env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR, 1);
        break;
    case TLBRET_INVALID:
        /* TLB match with no valid bit */
        if (access_type == MMU_DATA_LOAD) {
            cs->exception_index = EXCCODE_PIL;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PIS;
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PIF;
        }
        break;
    case TLBRET_DIRTY:
        /* TLB match but 'D' bit is cleared */
        cs->exception_index = EXCCODE_PME;
        break;
    case TLBRET_XI:
        /* Execute-Inhibit Exception */
        cs->exception_index = EXCCODE_PNX;
        break;
    case TLBRET_RI:
        /* Read-Inhibit Exception */
        cs->exception_index = EXCCODE_PNR;
        break;
    case TLBRET_PE:
        /* Privileged Exception */
        cs->exception_index = EXCCODE_PPI;
        break;
    }

    if (tlb_error == TLBRET_NOMATCH) {
        env->CSR_TLBRBADV = address;
        env->CSR_TLBREHI = FIELD_DP64(env->CSR_TLBREHI, CSR_TLBREHI, VPPN,
                                      extract64(address, 13, 35));
    } else {
        if (!FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)) {
            env->CSR_BADV = address;
        }
        env->CSR_TLBEHI = address & (TARGET_PAGE_MASK << 1);
   }
}

bool loongarch_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                            MMUAccessType access_type, int mmu_idx,
                            bool probe, uintptr_t retaddr)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    hwaddr physical;
    int prot;
    int ret = TLBRET_BADADDR;

    /* Data access */
    ret = get_physical_address(env, &physical, &prot, address,
                               access_type, mmu_idx);

    if (ret == TLBRET_MATCH) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " physical " TARGET_FMT_plx
                      " prot %d\n", __func__, address, physical, prot);
        return true;
    } else {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " ret %d\n", __func__, address,
                      ret);
    }
    if (probe) {
        return false;
    }
    raise_mmu_exception(env, address, access_type, ret);
    cpu_loop_exit_restore(cs, retaddr);
}
