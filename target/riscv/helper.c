/*
 * RISC-V emulation helpers for qemu.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg-op.h"

#define RISCV_DEBUG_INTERRUPT 0

int riscv_cpu_mmu_index(CPURISCVState *env, bool ifetch)
{
#ifdef CONFIG_USER_ONLY
    return 0;
#else
    return env->priv;
#endif
}

#ifndef CONFIG_USER_ONLY
/*
 * Return RISC-V IRQ number if an interrupt should be taken, else -1.
 * Used in cpu-exec.c
 *
 * Adapted from Spike's processor_t::take_interrupt()
 */
static int riscv_cpu_hw_interrupts_pending(CPURISCVState *env)
{
    target_ulong pending_interrupts = atomic_read(&env->mip) & env->mie;

    target_ulong mie = get_field(env->mstatus, MSTATUS_MIE);
    target_ulong m_enabled = env->priv < PRV_M || (env->priv == PRV_M && mie);
    target_ulong enabled_interrupts = pending_interrupts &
                                      ~env->mideleg & -m_enabled;

    target_ulong sie = get_field(env->mstatus, MSTATUS_SIE);
    target_ulong s_enabled = env->priv < PRV_S || (env->priv == PRV_S && sie);
    enabled_interrupts |= pending_interrupts & env->mideleg &
                          -s_enabled;

    if (enabled_interrupts) {
        return ctz64(enabled_interrupts); /* since non-zero */
    } else {
        return EXCP_NONE; /* indicates no pending interrupt */
    }
}
#endif

bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
#if !defined(CONFIG_USER_ONLY)
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;
        int interruptno = riscv_cpu_hw_interrupts_pending(env);
        if (interruptno >= 0) {
            cs->exception_index = RISCV_EXCP_INT_FLAG | interruptno;
            riscv_cpu_do_interrupt(cs);
            return true;
        }
    }
#endif
    return false;
}

#if !defined(CONFIG_USER_ONLY)

/* get_physical_address - get the physical address for this virtual address
 *
 * Do a page table walk to obtain the physical address corresponding to a
 * virtual address. Returns 0 if the translation was successful
 *
 * Adapted from Spike's mmu_t::translate and mmu_t::walk
 *
 */
static int get_physical_address(CPURISCVState *env, hwaddr *physical,
                                int *prot, target_ulong addr,
                                int access_type, int mmu_idx)
{
    /* NOTE: the env->pc value visible here will not be
     * correct, but the value visible to the exception handler
     * (riscv_cpu_do_interrupt) is correct */

    int mode = mmu_idx;

    if (mode == PRV_M && access_type != MMU_INST_FETCH) {
        if (get_field(env->mstatus, MSTATUS_MPRV)) {
            mode = get_field(env->mstatus, MSTATUS_MPP);
        }
    }

    if (mode == PRV_M || !riscv_feature(env, RISCV_FEATURE_MMU)) {
        *physical = addr;
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TRANSLATE_SUCCESS;
    }

    *prot = 0;

    target_ulong base;
    int levels, ptidxbits, ptesize, vm, sum;
    int mxr = get_field(env->mstatus, MSTATUS_MXR);

    if (env->priv_ver >= PRIV_VERSION_1_10_0) {
        base = get_field(env->satp, SATP_PPN) << PGSHIFT;
        sum = get_field(env->mstatus, MSTATUS_SUM);
        vm = get_field(env->satp, SATP_MODE);
        switch (vm) {
        case VM_1_10_SV32:
          levels = 2; ptidxbits = 10; ptesize = 4; break;
        case VM_1_10_SV39:
          levels = 3; ptidxbits = 9; ptesize = 8; break;
        case VM_1_10_SV48:
          levels = 4; ptidxbits = 9; ptesize = 8; break;
        case VM_1_10_SV57:
          levels = 5; ptidxbits = 9; ptesize = 8; break;
        case VM_1_10_MBARE:
            *physical = addr;
            *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            return TRANSLATE_SUCCESS;
        default:
          g_assert_not_reached();
        }
    } else {
        base = env->sptbr << PGSHIFT;
        sum = !get_field(env->mstatus, MSTATUS_PUM);
        vm = get_field(env->mstatus, MSTATUS_VM);
        switch (vm) {
        case VM_1_09_SV32:
          levels = 2; ptidxbits = 10; ptesize = 4; break;
        case VM_1_09_SV39:
          levels = 3; ptidxbits = 9; ptesize = 8; break;
        case VM_1_09_SV48:
          levels = 4; ptidxbits = 9; ptesize = 8; break;
        case VM_1_09_MBARE:
            *physical = addr;
            *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            return TRANSLATE_SUCCESS;
        default:
          g_assert_not_reached();
        }
    }

    CPUState *cs = CPU(riscv_env_get_cpu(env));
    int va_bits = PGSHIFT + levels * ptidxbits;
    target_ulong mask = (1L << (TARGET_LONG_BITS - (va_bits - 1))) - 1;
    target_ulong masked_msbs = (addr >> (va_bits - 1)) & mask;
    if (masked_msbs != 0 && masked_msbs != mask) {
        return TRANSLATE_FAIL;
    }

    int ptshift = (levels - 1) * ptidxbits;
    int i;

#if !TCG_OVERSIZED_GUEST
restart:
#endif
    for (i = 0; i < levels; i++, ptshift -= ptidxbits) {
        target_ulong idx = (addr >> (PGSHIFT + ptshift)) &
                           ((1 << ptidxbits) - 1);

        /* check that physical address of PTE is legal */
        target_ulong pte_addr = base + idx * ptesize;
#if defined(TARGET_RISCV32)
        target_ulong pte = ldl_phys(cs->as, pte_addr);
#elif defined(TARGET_RISCV64)
        target_ulong pte = ldq_phys(cs->as, pte_addr);
#endif
        target_ulong ppn = pte >> PTE_PPN_SHIFT;

        if (PTE_TABLE(pte)) { /* next level of page table */
            base = ppn << PGSHIFT;
        } else if ((pte & PTE_U) ? (mode == PRV_S) && !sum : !(mode == PRV_S)) {
            break;
        } else if (!(pte & PTE_V) || (!(pte & PTE_R) && (pte & PTE_W))) {
            break;
        } else if (access_type == MMU_INST_FETCH ? !(pte & PTE_X) :
                  access_type == MMU_DATA_LOAD ?  !(pte & PTE_R) &&
                  !(mxr && (pte & PTE_X)) : !((pte & PTE_R) && (pte & PTE_W))) {
            break;
        } else {
            /* if necessary, set accessed and dirty bits. */
            target_ulong updated_pte = pte | PTE_A |
                (access_type == MMU_DATA_STORE ? PTE_D : 0);

            /* Page table updates need to be atomic with MTTCG enabled */
            if (updated_pte != pte) {
                /* if accessed or dirty bits need updating, and the PTE is
                 * in RAM, then we do so atomically with a compare and swap.
                 * if the PTE is in IO space, then it can't be updated.
                 * if the PTE changed, then we must re-walk the page table
                   as the PTE is no longer valid */
                MemoryRegion *mr;
                hwaddr l = sizeof(target_ulong), addr1;
                mr = address_space_translate(cs->as, pte_addr,
                    &addr1, &l, false);
                if (memory_access_is_direct(mr, true)) {
                    target_ulong *pte_pa =
                        qemu_map_ram_ptr(mr->ram_block, addr1);
#if TCG_OVERSIZED_GUEST
                    /* MTTCG is not enabled on oversized TCG guests so
                     * page table updates do not need to be atomic */
                    *pte_pa = pte = updated_pte;
#else
                    target_ulong old_pte =
                        atomic_cmpxchg(pte_pa, pte, updated_pte);
                    if (old_pte != pte) {
                        goto restart;
                    } else {
                        pte = updated_pte;
                    }
#endif
                } else {
                    /* misconfigured PTE in ROM (AD bits are not preset) or
                     * PTE is in IO space and can't be updated atomically */
                    return TRANSLATE_FAIL;
                }
            }

            /* for superpage mappings, make a fake leaf PTE for the TLB's
               benefit. */
            target_ulong vpn = addr >> PGSHIFT;
            *physical = (ppn | (vpn & ((1L << ptshift) - 1))) << PGSHIFT;

            if ((pte & PTE_R)) {
                *prot |= PAGE_READ;
            }
            if ((pte & PTE_X)) {
                *prot |= PAGE_EXEC;
            }
           /* only add write permission on stores or if the page
              is already dirty, so that we don't miss further
              page table walks to update the dirty bit */
            if ((pte & PTE_W) &&
                    (access_type == MMU_DATA_STORE || (pte & PTE_D))) {
                *prot |= PAGE_WRITE;
            }
            return TRANSLATE_SUCCESS;
        }
    }
    return TRANSLATE_FAIL;
}

static void raise_mmu_exception(CPURISCVState *env, target_ulong address,
                                MMUAccessType access_type)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));
    int page_fault_exceptions =
        (env->priv_ver >= PRIV_VERSION_1_10_0) &&
        get_field(env->satp, SATP_MODE) != VM_1_10_MBARE;
    switch (access_type) {
    case MMU_INST_FETCH:
        cs->exception_index = page_fault_exceptions ?
            RISCV_EXCP_INST_PAGE_FAULT : RISCV_EXCP_INST_ACCESS_FAULT;
        break;
    case MMU_DATA_LOAD:
        cs->exception_index = page_fault_exceptions ?
            RISCV_EXCP_LOAD_PAGE_FAULT : RISCV_EXCP_LOAD_ACCESS_FAULT;
        break;
    case MMU_DATA_STORE:
        cs->exception_index = page_fault_exceptions ?
            RISCV_EXCP_STORE_PAGE_FAULT : RISCV_EXCP_STORE_AMO_ACCESS_FAULT;
        break;
    default:
        g_assert_not_reached();
    }
    env->badaddr = address;
}

hwaddr riscv_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    hwaddr phys_addr;
    int prot;
    int mmu_idx = cpu_mmu_index(&cpu->env, false);

    if (get_physical_address(&cpu->env, &phys_addr, &prot, addr, 0, mmu_idx)) {
        return -1;
    }
    return phys_addr;
}

void riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                   MMUAccessType access_type, int mmu_idx,
                                   uintptr_t retaddr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    switch (access_type) {
    case MMU_INST_FETCH:
        cs->exception_index = RISCV_EXCP_INST_ADDR_MIS;
        break;
    case MMU_DATA_LOAD:
        cs->exception_index = RISCV_EXCP_LOAD_ADDR_MIS;
        break;
    case MMU_DATA_STORE:
        cs->exception_index = RISCV_EXCP_STORE_AMO_ADDR_MIS;
        break;
    default:
        g_assert_not_reached();
    }
    env->badaddr = addr;
    do_raise_exception_err(env, cs->exception_index, retaddr);
}

/* called by qemu's softmmu to fill the qemu tlb */
void tlb_fill(CPUState *cs, target_ulong addr, int size,
        MMUAccessType access_type, int mmu_idx, uintptr_t retaddr)
{
    int ret;
    ret = riscv_cpu_handle_mmu_fault(cs, addr, size, access_type, mmu_idx);
    if (ret == TRANSLATE_FAIL) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;
        do_raise_exception_err(env, cs->exception_index, retaddr);
    }
}

#endif

int riscv_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int size,
        int rw, int mmu_idx)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
#if !defined(CONFIG_USER_ONLY)
    hwaddr pa = 0;
    int prot;
#endif
    int ret = TRANSLATE_FAIL;

    qemu_log_mask(CPU_LOG_MMU,
            "%s pc " TARGET_FMT_lx " ad %" VADDR_PRIx " rw %d mmu_idx \
             %d\n", __func__, env->pc, address, rw, mmu_idx);

#if !defined(CONFIG_USER_ONLY)
    ret = get_physical_address(env, &pa, &prot, address, rw, mmu_idx);
    qemu_log_mask(CPU_LOG_MMU,
            "%s address=%" VADDR_PRIx " ret %d physical " TARGET_FMT_plx
             " prot %d\n", __func__, address, ret, pa, prot);
    if (!pmp_hart_has_privs(env, pa, TARGET_PAGE_SIZE, 1 << rw)) {
        ret = TRANSLATE_FAIL;
    }
    if (ret == TRANSLATE_SUCCESS) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK, pa & TARGET_PAGE_MASK,
                     prot, mmu_idx, TARGET_PAGE_SIZE);
    } else if (ret == TRANSLATE_FAIL) {
        raise_mmu_exception(env, address, rw);
    }
#else
    switch (rw) {
    case MMU_INST_FETCH:
        cs->exception_index = RISCV_EXCP_INST_PAGE_FAULT;
        break;
    case MMU_DATA_LOAD:
        cs->exception_index = RISCV_EXCP_LOAD_PAGE_FAULT;
        break;
    case MMU_DATA_STORE:
        cs->exception_index = RISCV_EXCP_STORE_PAGE_FAULT;
        break;
    }
#endif
    return ret;
}

/*
 * Handle Traps
 *
 * Adapted from Spike's processor_t::take_trap.
 *
 */
void riscv_cpu_do_interrupt(CPUState *cs)
{
#if !defined(CONFIG_USER_ONLY)

    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (RISCV_DEBUG_INTERRUPT) {
        int log_cause = cs->exception_index & RISCV_EXCP_INT_MASK;
        if (cs->exception_index & RISCV_EXCP_INT_FLAG) {
            qemu_log_mask(LOG_TRACE, "core   0: trap %s, epc 0x" TARGET_FMT_lx,
                riscv_intr_names[log_cause], env->pc);
        } else {
            qemu_log_mask(LOG_TRACE, "core   0: intr %s, epc 0x" TARGET_FMT_lx,
                riscv_excp_names[log_cause], env->pc);
        }
    }

    target_ulong fixed_cause = 0;
    if (cs->exception_index & (RISCV_EXCP_INT_FLAG)) {
        /* hacky for now. the MSB (bit 63) indicates interrupt but cs->exception
           index is only 32 bits wide */
        fixed_cause = cs->exception_index & RISCV_EXCP_INT_MASK;
        fixed_cause |= ((target_ulong)1) << (TARGET_LONG_BITS - 1);
    } else {
        /* fixup User ECALL -> correct priv ECALL */
        if (cs->exception_index == RISCV_EXCP_U_ECALL) {
            switch (env->priv) {
            case PRV_U:
                fixed_cause = RISCV_EXCP_U_ECALL;
                break;
            case PRV_S:
                fixed_cause = RISCV_EXCP_S_ECALL;
                break;
            case PRV_H:
                fixed_cause = RISCV_EXCP_H_ECALL;
                break;
            case PRV_M:
                fixed_cause = RISCV_EXCP_M_ECALL;
                break;
            }
        } else {
            fixed_cause = cs->exception_index;
        }
    }

    target_ulong backup_epc = env->pc;

    target_ulong bit = fixed_cause;
    target_ulong deleg = env->medeleg;

    int hasbadaddr =
        (fixed_cause == RISCV_EXCP_INST_ADDR_MIS) ||
        (fixed_cause == RISCV_EXCP_INST_ACCESS_FAULT) ||
        (fixed_cause == RISCV_EXCP_LOAD_ADDR_MIS) ||
        (fixed_cause == RISCV_EXCP_STORE_AMO_ADDR_MIS) ||
        (fixed_cause == RISCV_EXCP_LOAD_ACCESS_FAULT) ||
        (fixed_cause == RISCV_EXCP_STORE_AMO_ACCESS_FAULT) ||
        (fixed_cause == RISCV_EXCP_INST_PAGE_FAULT) ||
        (fixed_cause == RISCV_EXCP_LOAD_PAGE_FAULT) ||
        (fixed_cause == RISCV_EXCP_STORE_PAGE_FAULT);

    if (bit & ((target_ulong)1 << (TARGET_LONG_BITS - 1))) {
        deleg = env->mideleg;
        bit &= ~((target_ulong)1 << (TARGET_LONG_BITS - 1));
    }

    if (env->priv <= PRV_S && bit < 64 && ((deleg >> bit) & 1)) {
        /* handle the trap in S-mode */
        /* No need to check STVEC for misaligned - lower 2 bits cannot be set */
        env->pc = env->stvec;
        env->scause = fixed_cause;
        env->sepc = backup_epc;

        if (hasbadaddr) {
            if (RISCV_DEBUG_INTERRUPT) {
                qemu_log_mask(LOG_TRACE, "core " TARGET_FMT_ld
                    ": badaddr 0x" TARGET_FMT_lx, env->mhartid, env->badaddr);
            }
            env->sbadaddr = env->badaddr;
        }

        target_ulong s = env->mstatus;
        s = set_field(s, MSTATUS_SPIE, env->priv_ver >= PRIV_VERSION_1_10_0 ?
            get_field(s, MSTATUS_SIE) : get_field(s, MSTATUS_UIE << env->priv));
        s = set_field(s, MSTATUS_SPP, env->priv);
        s = set_field(s, MSTATUS_SIE, 0);
        csr_write_helper(env, s, CSR_MSTATUS);
        riscv_set_mode(env, PRV_S);
    } else {
        /* No need to check MTVEC for misaligned - lower 2 bits cannot be set */
        env->pc = env->mtvec;
        env->mepc = backup_epc;
        env->mcause = fixed_cause;

        if (hasbadaddr) {
            if (RISCV_DEBUG_INTERRUPT) {
                qemu_log_mask(LOG_TRACE, "core " TARGET_FMT_ld
                    ": badaddr 0x" TARGET_FMT_lx, env->mhartid, env->badaddr);
            }
            env->mbadaddr = env->badaddr;
        }

        target_ulong s = env->mstatus;
        s = set_field(s, MSTATUS_MPIE, env->priv_ver >= PRIV_VERSION_1_10_0 ?
            get_field(s, MSTATUS_MIE) : get_field(s, MSTATUS_UIE << env->priv));
        s = set_field(s, MSTATUS_MPP, env->priv);
        s = set_field(s, MSTATUS_MIE, 0);
        csr_write_helper(env, s, CSR_MSTATUS);
        riscv_set_mode(env, PRV_M);
    }
    /* TODO yield load reservation  */
#endif
    cs->exception_index = EXCP_NONE; /* mark handled to qemu */
}
