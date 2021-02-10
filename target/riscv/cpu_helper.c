/*
 * RISC-V CPU helpers for qemu.
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
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "trace.h"
#include "hw/semihosting/common-semi.h"

int riscv_cpu_mmu_index(CPURISCVState *env, bool ifetch)
{
#ifdef CONFIG_USER_ONLY
    return 0;
#else
    return env->priv;
#endif
}

#ifndef CONFIG_USER_ONLY
static int riscv_cpu_local_irq_pending(CPURISCVState *env)
{
    target_ulong irqs;

    target_ulong mstatus_mie = get_field(env->mstatus, MSTATUS_MIE);
    target_ulong mstatus_sie = get_field(env->mstatus, MSTATUS_SIE);
    target_ulong hs_mstatus_sie = get_field(env->mstatus_hs, MSTATUS_SIE);

    target_ulong pending = env->mip & env->mie &
                               ~(MIP_VSSIP | MIP_VSTIP | MIP_VSEIP);
    target_ulong vspending = (env->mip & env->mie &
                              (MIP_VSSIP | MIP_VSTIP | MIP_VSEIP));

    target_ulong mie    = env->priv < PRV_M ||
                          (env->priv == PRV_M && mstatus_mie);
    target_ulong sie    = env->priv < PRV_S ||
                          (env->priv == PRV_S && mstatus_sie);
    target_ulong hs_sie = env->priv < PRV_S ||
                          (env->priv == PRV_S && hs_mstatus_sie);

    if (riscv_cpu_virt_enabled(env)) {
        target_ulong pending_hs_irq = pending & -hs_sie;

        if (pending_hs_irq) {
            riscv_cpu_set_force_hs_excep(env, FORCE_HS_EXCEP);
            return ctz64(pending_hs_irq);
        }

        pending = vspending;
    }

    irqs = (pending & ~env->mideleg & -mie) | (pending &  env->mideleg & -sie);

    if (irqs) {
        return ctz64(irqs); /* since non-zero */
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
        int interruptno = riscv_cpu_local_irq_pending(env);
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

/* Return true is floating point support is currently enabled */
bool riscv_cpu_fp_enabled(CPURISCVState *env)
{
    if (env->mstatus & MSTATUS_FS) {
        if (riscv_cpu_virt_enabled(env) && !(env->mstatus_hs & MSTATUS_FS)) {
            return false;
        }
        return true;
    }

    return false;
}

void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env)
{
    uint64_t mstatus_mask = MSTATUS_MXR | MSTATUS_SUM | MSTATUS_FS |
                            MSTATUS_SPP | MSTATUS_SPIE | MSTATUS_SIE |
                            MSTATUS64_UXL;
    bool current_virt = riscv_cpu_virt_enabled(env);

    g_assert(riscv_has_ext(env, RVH));

    if (current_virt) {
        /* Current V=1 and we are about to change to V=0 */
        env->vsstatus = env->mstatus & mstatus_mask;
        env->mstatus &= ~mstatus_mask;
        env->mstatus |= env->mstatus_hs;

        env->vstvec = env->stvec;
        env->stvec = env->stvec_hs;

        env->vsscratch = env->sscratch;
        env->sscratch = env->sscratch_hs;

        env->vsepc = env->sepc;
        env->sepc = env->sepc_hs;

        env->vscause = env->scause;
        env->scause = env->scause_hs;

        env->vstval = env->sbadaddr;
        env->sbadaddr = env->stval_hs;

        env->vsatp = env->satp;
        env->satp = env->satp_hs;
    } else {
        /* Current V=0 and we are about to change to V=1 */
        env->mstatus_hs = env->mstatus & mstatus_mask;
        env->mstatus &= ~mstatus_mask;
        env->mstatus |= env->vsstatus;

        env->stvec_hs = env->stvec;
        env->stvec = env->vstvec;

        env->sscratch_hs = env->sscratch;
        env->sscratch = env->vsscratch;

        env->sepc_hs = env->sepc;
        env->sepc = env->vsepc;

        env->scause_hs = env->scause;
        env->scause = env->vscause;

        env->stval_hs = env->sbadaddr;
        env->sbadaddr = env->vstval;

        env->satp_hs = env->satp;
        env->satp = env->vsatp;
    }
}

bool riscv_cpu_virt_enabled(CPURISCVState *env)
{
    if (!riscv_has_ext(env, RVH)) {
        return false;
    }

    return get_field(env->virt, VIRT_ONOFF);
}

void riscv_cpu_set_virt_enabled(CPURISCVState *env, bool enable)
{
    if (!riscv_has_ext(env, RVH)) {
        return;
    }

    /* Flush the TLB on all virt mode changes. */
    if (get_field(env->virt, VIRT_ONOFF) != enable) {
        tlb_flush(env_cpu(env));
    }

    env->virt = set_field(env->virt, VIRT_ONOFF, enable);
}

bool riscv_cpu_force_hs_excep_enabled(CPURISCVState *env)
{
    if (!riscv_has_ext(env, RVH)) {
        return false;
    }

    return get_field(env->virt, FORCE_HS_EXCEP);
}

void riscv_cpu_set_force_hs_excep(CPURISCVState *env, bool enable)
{
    if (!riscv_has_ext(env, RVH)) {
        return;
    }

    env->virt = set_field(env->virt, FORCE_HS_EXCEP, enable);
}

bool riscv_cpu_two_stage_lookup(int mmu_idx)
{
    return mmu_idx & TB_FLAGS_PRIV_HYP_ACCESS_MASK;
}

int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint32_t interrupts)
{
    CPURISCVState *env = &cpu->env;
    if (env->miclaim & interrupts) {
        return -1;
    } else {
        env->miclaim |= interrupts;
        return 0;
    }
}

uint32_t riscv_cpu_update_mip(RISCVCPU *cpu, uint32_t mask, uint32_t value)
{
    CPURISCVState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    uint32_t old = env->mip;
    bool locked = false;

    if (!qemu_mutex_iothread_locked()) {
        locked = true;
        qemu_mutex_lock_iothread();
    }

    env->mip = (env->mip & ~mask) | (value & mask);

    if (env->mip) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }

    if (locked) {
        qemu_mutex_unlock_iothread();
    }

    return old;
}

void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(uint32_t),
                             uint32_t arg)
{
    env->rdtime_fn = fn;
    env->rdtime_fn_arg = arg;
}

void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv)
{
    if (newpriv > PRV_M) {
        g_assert_not_reached();
    }
    if (newpriv == PRV_H) {
        newpriv = PRV_U;
    }
    /* tlb_flush is unnecessary as mode is contained in mmu_idx */
    env->priv = newpriv;

    /*
     * Clear the load reservation - otherwise a reservation placed in one
     * context/process can be used by another, resulting in an SC succeeding
     * incorrectly. Version 2.2 of the ISA specification explicitly requires
     * this behaviour, while later revisions say that the kernel "should" use
     * an SC instruction to force the yielding of a load reservation on a
     * preemptive context switch. As a result, do both.
     */
    env->load_res = -1;
}

/* get_physical_address - get the physical address for this virtual address
 *
 * Do a page table walk to obtain the physical address corresponding to a
 * virtual address. Returns 0 if the translation was successful
 *
 * Adapted from Spike's mmu_t::translate and mmu_t::walk
 *
 * @env: CPURISCVState
 * @physical: This will be set to the calculated physical address
 * @prot: The returned protection attributes
 * @addr: The virtual address to be translated
 * @fault_pte_addr: If not NULL, this will be set to fault pte address
 *                  when a error occurs on pte address translation.
 *                  This will already be shifted to match htval.
 * @access_type: The type of MMU access
 * @mmu_idx: Indicates current privilege level
 * @first_stage: Are we in first stage translation?
 *               Second stage is used for hypervisor guest translation
 * @two_stage: Are we going to perform two stage translation
 */
static int get_physical_address(CPURISCVState *env, hwaddr *physical,
                                int *prot, target_ulong addr,
                                target_ulong *fault_pte_addr,
                                int access_type, int mmu_idx,
                                bool first_stage, bool two_stage)
{
    /* NOTE: the env->pc value visible here will not be
     * correct, but the value visible to the exception handler
     * (riscv_cpu_do_interrupt) is correct */
    MemTxResult res;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    int mode = mmu_idx & TB_FLAGS_PRIV_MMU_MASK;
    bool use_background = false;

    /*
     * Check if we should use the background registers for the two
     * stage translation. We don't need to check if we actually need
     * two stage translation as that happened before this function
     * was called. Background registers will be used if the guest has
     * forced a two stage translation to be on (in HS or M mode).
     */
    if (!riscv_cpu_virt_enabled(env) && riscv_cpu_two_stage_lookup(mmu_idx)) {
        use_background = true;
    }

    if (mode == PRV_M && access_type != MMU_INST_FETCH) {
        if (get_field(env->mstatus, MSTATUS_MPRV)) {
            mode = get_field(env->mstatus, MSTATUS_MPP);
        }
    }

    if (first_stage == false) {
        /* We are in stage 2 translation, this is similar to stage 1. */
        /* Stage 2 is always taken as U-mode */
        mode = PRV_U;
    }

    if (mode == PRV_M || !riscv_feature(env, RISCV_FEATURE_MMU)) {
        *physical = addr;
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TRANSLATE_SUCCESS;
    }

    *prot = 0;

    hwaddr base;
    int levels, ptidxbits, ptesize, vm, sum, mxr, widened;

    if (first_stage == true) {
        mxr = get_field(env->mstatus, MSTATUS_MXR);
    } else {
        mxr = get_field(env->vsstatus, MSTATUS_MXR);
    }

    if (first_stage == true) {
        if (use_background) {
            base = (hwaddr)get_field(env->vsatp, SATP_PPN) << PGSHIFT;
            vm = get_field(env->vsatp, SATP_MODE);
        } else {
            base = (hwaddr)get_field(env->satp, SATP_PPN) << PGSHIFT;
            vm = get_field(env->satp, SATP_MODE);
        }
        widened = 0;
    } else {
        base = (hwaddr)get_field(env->hgatp, HGATP_PPN) << PGSHIFT;
        vm = get_field(env->hgatp, HGATP_MODE);
        widened = 2;
    }
    /* status.SUM will be ignored if execute on background */
    sum = get_field(env->mstatus, MSTATUS_SUM) || use_background;
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

    CPUState *cs = env_cpu(env);
    int va_bits = PGSHIFT + levels * ptidxbits + widened;
    target_ulong mask, masked_msbs;

    if (TARGET_LONG_BITS > (va_bits - 1)) {
        mask = (1L << (TARGET_LONG_BITS - (va_bits - 1))) - 1;
    } else {
        mask = 0;
    }
    masked_msbs = (addr >> (va_bits - 1)) & mask;

    if (masked_msbs != 0 && masked_msbs != mask) {
        return TRANSLATE_FAIL;
    }

    int ptshift = (levels - 1) * ptidxbits;
    int i;

#if !TCG_OVERSIZED_GUEST
restart:
#endif
    for (i = 0; i < levels; i++, ptshift -= ptidxbits) {
        target_ulong idx;
        if (i == 0) {
            idx = (addr >> (PGSHIFT + ptshift)) &
                           ((1 << (ptidxbits + widened)) - 1);
        } else {
            idx = (addr >> (PGSHIFT + ptshift)) &
                           ((1 << ptidxbits) - 1);
        }

        /* check that physical address of PTE is legal */
        hwaddr pte_addr;

        if (two_stage && first_stage) {
            int vbase_prot;
            hwaddr vbase;

            /* Do the second stage translation on the base PTE address. */
            int vbase_ret = get_physical_address(env, &vbase, &vbase_prot,
                                                 base, NULL, MMU_DATA_LOAD,
                                                 mmu_idx, false, true);

            if (vbase_ret != TRANSLATE_SUCCESS) {
                if (fault_pte_addr) {
                    *fault_pte_addr = (base + idx * ptesize) >> 2;
                }
                return TRANSLATE_G_STAGE_FAIL;
            }

            pte_addr = vbase + idx * ptesize;
        } else {
            pte_addr = base + idx * ptesize;
        }

        if (riscv_feature(env, RISCV_FEATURE_PMP) &&
            !pmp_hart_has_privs(env, pte_addr, sizeof(target_ulong),
            1 << MMU_DATA_LOAD, PRV_S)) {
            return TRANSLATE_PMP_FAIL;
        }

        target_ulong pte;
        if (riscv_cpu_is_32bit(env)) {
            pte = address_space_ldl(cs->as, pte_addr, attrs, &res);
        } else {
            pte = address_space_ldq(cs->as, pte_addr, attrs, &res);
        }

        if (res != MEMTX_OK) {
            return TRANSLATE_FAIL;
        }

        hwaddr ppn = pte >> PTE_PPN_SHIFT;

        if (!(pte & PTE_V)) {
            /* Invalid PTE */
            return TRANSLATE_FAIL;
        } else if (!(pte & (PTE_R | PTE_W | PTE_X))) {
            /* Inner PTE, continue walking */
            base = ppn << PGSHIFT;
        } else if ((pte & (PTE_R | PTE_W | PTE_X)) == PTE_W) {
            /* Reserved leaf PTE flags: PTE_W */
            return TRANSLATE_FAIL;
        } else if ((pte & (PTE_R | PTE_W | PTE_X)) == (PTE_W | PTE_X)) {
            /* Reserved leaf PTE flags: PTE_W + PTE_X */
            return TRANSLATE_FAIL;
        } else if ((pte & PTE_U) && ((mode != PRV_U) &&
                   (!sum || access_type == MMU_INST_FETCH))) {
            /* User PTE flags when not U mode and mstatus.SUM is not set,
               or the access type is an instruction fetch */
            return TRANSLATE_FAIL;
        } else if (!(pte & PTE_U) && (mode != PRV_S)) {
            /* Supervisor PTE flags when not S mode */
            return TRANSLATE_FAIL;
        } else if (ppn & ((1ULL << ptshift) - 1)) {
            /* Misaligned PPN */
            return TRANSLATE_FAIL;
        } else if (access_type == MMU_DATA_LOAD && !((pte & PTE_R) ||
                   ((pte & PTE_X) && mxr))) {
            /* Read access check failed */
            return TRANSLATE_FAIL;
        } else if (access_type == MMU_DATA_STORE && !(pte & PTE_W)) {
            /* Write access check failed */
            return TRANSLATE_FAIL;
        } else if (access_type == MMU_INST_FETCH && !(pte & PTE_X)) {
            /* Fetch access check failed */
            return TRANSLATE_FAIL;
        } else {
            /* if necessary, set accessed and dirty bits. */
            target_ulong updated_pte = pte | PTE_A |
                (access_type == MMU_DATA_STORE ? PTE_D : 0);

            /* Page table updates need to be atomic with MTTCG enabled */
            if (updated_pte != pte) {
                /*
                 * - if accessed or dirty bits need updating, and the PTE is
                 *   in RAM, then we do so atomically with a compare and swap.
                 * - if the PTE is in IO space or ROM, then it can't be updated
                 *   and we return TRANSLATE_FAIL.
                 * - if the PTE changed by the time we went to update it, then
                 *   it is no longer valid and we must re-walk the page table.
                 */
                MemoryRegion *mr;
                hwaddr l = sizeof(target_ulong), addr1;
                mr = address_space_translate(cs->as, pte_addr,
                    &addr1, &l, false, MEMTXATTRS_UNSPECIFIED);
                if (memory_region_is_ram(mr)) {
                    target_ulong *pte_pa =
                        qemu_map_ram_ptr(mr->ram_block, addr1);
#if TCG_OVERSIZED_GUEST
                    /* MTTCG is not enabled on oversized TCG guests so
                     * page table updates do not need to be atomic */
                    *pte_pa = pte = updated_pte;
#else
                    target_ulong old_pte =
                        qatomic_cmpxchg(pte_pa, pte, updated_pte);
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
            *physical = ((ppn | (vpn & ((1L << ptshift) - 1))) << PGSHIFT) |
                        (addr & ~TARGET_PAGE_MASK);

            /* set permissions on the TLB entry */
            if ((pte & PTE_R) || ((pte & PTE_X) && mxr)) {
                *prot |= PAGE_READ;
            }
            if ((pte & PTE_X)) {
                *prot |= PAGE_EXEC;
            }
            /* add write permission on stores or if the page is already dirty,
               so that we TLB miss on later writes to update the dirty bit */
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
                                MMUAccessType access_type, bool pmp_violation,
                                bool first_stage, bool two_stage)
{
    CPUState *cs = env_cpu(env);
    int page_fault_exceptions;
    if (first_stage) {
        page_fault_exceptions =
            get_field(env->satp, SATP_MODE) != VM_1_10_MBARE &&
            !pmp_violation;
    } else {
        page_fault_exceptions =
            get_field(env->hgatp, HGATP_MODE) != VM_1_10_MBARE &&
            !pmp_violation;
    }
    switch (access_type) {
    case MMU_INST_FETCH:
        if (riscv_cpu_virt_enabled(env) && !first_stage) {
            cs->exception_index = RISCV_EXCP_INST_GUEST_PAGE_FAULT;
        } else {
            cs->exception_index = page_fault_exceptions ?
                RISCV_EXCP_INST_PAGE_FAULT : RISCV_EXCP_INST_ACCESS_FAULT;
        }
        break;
    case MMU_DATA_LOAD:
        if (two_stage && !first_stage) {
            cs->exception_index = RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT;
        } else {
            cs->exception_index = page_fault_exceptions ?
                RISCV_EXCP_LOAD_PAGE_FAULT : RISCV_EXCP_LOAD_ACCESS_FAULT;
        }
        break;
    case MMU_DATA_STORE:
        if (two_stage && !first_stage) {
            cs->exception_index = RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT;
        } else {
            cs->exception_index = page_fault_exceptions ?
                RISCV_EXCP_STORE_PAGE_FAULT : RISCV_EXCP_STORE_AMO_ACCESS_FAULT;
        }
        break;
    default:
        g_assert_not_reached();
    }
    env->badaddr = address;
}

hwaddr riscv_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    hwaddr phys_addr;
    int prot;
    int mmu_idx = cpu_mmu_index(&cpu->env, false);

    if (get_physical_address(env, &phys_addr, &prot, addr, NULL, 0, mmu_idx,
                             true, riscv_cpu_virt_enabled(env))) {
        return -1;
    }

    if (riscv_cpu_virt_enabled(env)) {
        if (get_physical_address(env, &phys_addr, &prot, phys_addr, NULL,
                                 0, mmu_idx, false, true)) {
            return -1;
        }
    }

    return phys_addr & TARGET_PAGE_MASK;
}

void riscv_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (access_type == MMU_DATA_STORE) {
        cs->exception_index = RISCV_EXCP_STORE_AMO_ACCESS_FAULT;
    } else {
        cs->exception_index = RISCV_EXCP_LOAD_ACCESS_FAULT;
    }

    env->badaddr = addr;
    riscv_raise_exception(&cpu->env, cs->exception_index, retaddr);
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
    riscv_raise_exception(env, cs->exception_index, retaddr);
}
#endif /* !CONFIG_USER_ONLY */

bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
#ifndef CONFIG_USER_ONLY
    vaddr im_address;
    hwaddr pa = 0;
    int prot, prot2;
    bool pmp_violation = false;
    bool first_stage_error = true;
    bool two_stage_lookup = false;
    int ret = TRANSLATE_FAIL;
    int mode = mmu_idx;
    target_ulong tlb_size = 0;

    env->guest_phys_fault_addr = 0;

    qemu_log_mask(CPU_LOG_MMU, "%s ad %" VADDR_PRIx " rw %d mmu_idx %d\n",
                  __func__, address, access_type, mmu_idx);

    if (mode == PRV_M && access_type != MMU_INST_FETCH) {
        if (get_field(env->mstatus, MSTATUS_MPRV)) {
            mode = get_field(env->mstatus, MSTATUS_MPP);
        }
    }

    if (riscv_has_ext(env, RVH) && env->priv == PRV_M &&
        access_type != MMU_INST_FETCH &&
        get_field(env->mstatus, MSTATUS_MPRV) &&
        get_field(env->mstatus, MSTATUS_MPV)) {
        two_stage_lookup = true;
    }

    if (riscv_cpu_virt_enabled(env) ||
        ((riscv_cpu_two_stage_lookup(mmu_idx) || two_stage_lookup) &&
         access_type != MMU_INST_FETCH)) {
        /* Two stage lookup */
        ret = get_physical_address(env, &pa, &prot, address,
                                   &env->guest_phys_fault_addr, access_type,
                                   mmu_idx, true, true);

        /*
         * A G-stage exception may be triggered during two state lookup.
         * And the env->guest_phys_fault_addr has already been set in
         * get_physical_address().
         */
        if (ret == TRANSLATE_G_STAGE_FAIL) {
            first_stage_error = false;
            access_type = MMU_DATA_LOAD;
        }

        qemu_log_mask(CPU_LOG_MMU,
                      "%s 1st-stage address=%" VADDR_PRIx " ret %d physical "
                      TARGET_FMT_plx " prot %d\n",
                      __func__, address, ret, pa, prot);

        if (ret == TRANSLATE_SUCCESS) {
            /* Second stage lookup */
            im_address = pa;

            ret = get_physical_address(env, &pa, &prot2, im_address, NULL,
                                       access_type, mmu_idx, false, true);

            qemu_log_mask(CPU_LOG_MMU,
                    "%s 2nd-stage address=%" VADDR_PRIx " ret %d physical "
                    TARGET_FMT_plx " prot %d\n",
                    __func__, im_address, ret, pa, prot2);

            prot &= prot2;

            if (riscv_feature(env, RISCV_FEATURE_PMP) &&
                (ret == TRANSLATE_SUCCESS) &&
                !pmp_hart_has_privs(env, pa, size, 1 << access_type, mode)) {
                ret = TRANSLATE_PMP_FAIL;
            }

            if (ret != TRANSLATE_SUCCESS) {
                /*
                 * Guest physical address translation failed, this is a HS
                 * level exception
                 */
                first_stage_error = false;
                env->guest_phys_fault_addr = (im_address |
                                              (address &
                                               (TARGET_PAGE_SIZE - 1))) >> 2;
            }
        }
    } else {
        /* Single stage lookup */
        ret = get_physical_address(env, &pa, &prot, address, NULL,
                                   access_type, mmu_idx, true, false);

        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " ret %d physical "
                      TARGET_FMT_plx " prot %d\n",
                      __func__, address, ret, pa, prot);
    }

    if (riscv_feature(env, RISCV_FEATURE_PMP) &&
        (ret == TRANSLATE_SUCCESS) &&
        !pmp_hart_has_privs(env, pa, size, 1 << access_type, mode)) {
        ret = TRANSLATE_PMP_FAIL;
    }
    if (ret == TRANSLATE_PMP_FAIL) {
        pmp_violation = true;
    }

    if (ret == TRANSLATE_SUCCESS) {
        if (pmp_is_range_in_tlb(env, pa & TARGET_PAGE_MASK, &tlb_size)) {
            tlb_set_page(cs, address & ~(tlb_size - 1), pa & ~(tlb_size - 1),
                         prot, mmu_idx, tlb_size);
        } else {
            tlb_set_page(cs, address & TARGET_PAGE_MASK, pa & TARGET_PAGE_MASK,
                         prot, mmu_idx, TARGET_PAGE_SIZE);
        }
        return true;
    } else if (probe) {
        return false;
    } else {
        raise_mmu_exception(env, address, access_type, pmp_violation,
                            first_stage_error,
                            riscv_cpu_virt_enabled(env) ||
                                riscv_cpu_two_stage_lookup(mmu_idx));
        riscv_raise_exception(env, cs->exception_index, retaddr);
    }

    return true;

#else
    switch (access_type) {
    case MMU_INST_FETCH:
        cs->exception_index = RISCV_EXCP_INST_PAGE_FAULT;
        break;
    case MMU_DATA_LOAD:
        cs->exception_index = RISCV_EXCP_LOAD_PAGE_FAULT;
        break;
    case MMU_DATA_STORE:
        cs->exception_index = RISCV_EXCP_STORE_PAGE_FAULT;
        break;
    default:
        g_assert_not_reached();
    }
    env->badaddr = address;
    cpu_loop_exit_restore(cs, retaddr);
#endif
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
    bool force_hs_execp = riscv_cpu_force_hs_excep_enabled(env);
    uint64_t s;

    /* cs->exception is 32-bits wide unlike mcause which is XLEN-bits wide
     * so we mask off the MSB and separate into trap type and cause.
     */
    bool async = !!(cs->exception_index & RISCV_EXCP_INT_FLAG);
    target_ulong cause = cs->exception_index & RISCV_EXCP_INT_MASK;
    target_ulong deleg = async ? env->mideleg : env->medeleg;
    bool write_tval = false;
    target_ulong tval = 0;
    target_ulong htval = 0;
    target_ulong mtval2 = 0;

    if  (cause == RISCV_EXCP_SEMIHOST) {
        if (env->priv >= PRV_S) {
            env->gpr[xA0] = do_common_semihosting(cs);
            env->pc += 4;
            return;
        }
        cause = RISCV_EXCP_BREAKPOINT;
    }

    if (!async) {
        /* set tval to badaddr for traps with address information */
        switch (cause) {
        case RISCV_EXCP_INST_GUEST_PAGE_FAULT:
        case RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT:
        case RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT:
            force_hs_execp = true;
            /* fallthrough */
        case RISCV_EXCP_INST_ADDR_MIS:
        case RISCV_EXCP_INST_ACCESS_FAULT:
        case RISCV_EXCP_LOAD_ADDR_MIS:
        case RISCV_EXCP_STORE_AMO_ADDR_MIS:
        case RISCV_EXCP_LOAD_ACCESS_FAULT:
        case RISCV_EXCP_STORE_AMO_ACCESS_FAULT:
        case RISCV_EXCP_INST_PAGE_FAULT:
        case RISCV_EXCP_LOAD_PAGE_FAULT:
        case RISCV_EXCP_STORE_PAGE_FAULT:
            write_tval  = true;
            tval = env->badaddr;
            break;
        default:
            break;
        }
        /* ecall is dispatched as one cause so translate based on mode */
        if (cause == RISCV_EXCP_U_ECALL) {
            assert(env->priv <= 3);

            if (env->priv == PRV_M) {
                cause = RISCV_EXCP_M_ECALL;
            } else if (env->priv == PRV_S && riscv_cpu_virt_enabled(env)) {
                cause = RISCV_EXCP_VS_ECALL;
            } else if (env->priv == PRV_S && !riscv_cpu_virt_enabled(env)) {
                cause = RISCV_EXCP_S_ECALL;
            } else if (env->priv == PRV_U) {
                cause = RISCV_EXCP_U_ECALL;
            }
        }
    }

    trace_riscv_trap(env->mhartid, async, cause, env->pc, tval,
                     riscv_cpu_get_trap_name(cause, async));

    qemu_log_mask(CPU_LOG_INT,
                  "%s: hart:"TARGET_FMT_ld", async:%d, cause:"TARGET_FMT_lx", "
                  "epc:0x"TARGET_FMT_lx", tval:0x"TARGET_FMT_lx", desc=%s\n",
                  __func__, env->mhartid, async, cause, env->pc, tval,
                  riscv_cpu_get_trap_name(cause, async));

    if (env->priv <= PRV_S &&
            cause < TARGET_LONG_BITS && ((deleg >> cause) & 1)) {
        /* handle the trap in S-mode */
        if (riscv_has_ext(env, RVH)) {
            target_ulong hdeleg = async ? env->hideleg : env->hedeleg;
            bool two_stage_lookup = false;

            if (env->priv == PRV_M ||
                (env->priv == PRV_S && !riscv_cpu_virt_enabled(env)) ||
                (env->priv == PRV_U && !riscv_cpu_virt_enabled(env) &&
                    get_field(env->hstatus, HSTATUS_HU))) {
                    two_stage_lookup = true;
            }

            if ((riscv_cpu_virt_enabled(env) || two_stage_lookup) && write_tval) {
                /*
                 * If we are writing a guest virtual address to stval, set
                 * this to 1. If we are trapping to VS we will set this to 0
                 * later.
                 */
                env->hstatus = set_field(env->hstatus, HSTATUS_GVA, 1);
            } else {
                /* For other HS-mode traps, we set this to 0. */
                env->hstatus = set_field(env->hstatus, HSTATUS_GVA, 0);
            }

            if (riscv_cpu_virt_enabled(env) && ((hdeleg >> cause) & 1) &&
                !force_hs_execp) {
                /* Trap to VS mode */
                /*
                 * See if we need to adjust cause. Yes if its VS mode interrupt
                 * no if hypervisor has delegated one of hs mode's interrupt
                 */
                if (cause == IRQ_VS_TIMER || cause == IRQ_VS_SOFT ||
                    cause == IRQ_VS_EXT) {
                    cause = cause - 1;
                }
                env->hstatus = set_field(env->hstatus, HSTATUS_GVA, 0);
            } else if (riscv_cpu_virt_enabled(env)) {
                /* Trap into HS mode, from virt */
                riscv_cpu_swap_hypervisor_regs(env);
                env->hstatus = set_field(env->hstatus, HSTATUS_SPVP,
                                         env->priv);
                env->hstatus = set_field(env->hstatus, HSTATUS_SPV,
                                         riscv_cpu_virt_enabled(env));

                htval = env->guest_phys_fault_addr;

                riscv_cpu_set_virt_enabled(env, 0);
                riscv_cpu_set_force_hs_excep(env, 0);
            } else {
                /* Trap into HS mode */
                if (!two_stage_lookup) {
                    env->hstatus = set_field(env->hstatus, HSTATUS_SPV,
                                             riscv_cpu_virt_enabled(env));
                }
                htval = env->guest_phys_fault_addr;
            }
        }

        s = env->mstatus;
        s = set_field(s, MSTATUS_SPIE, get_field(s, MSTATUS_SIE));
        s = set_field(s, MSTATUS_SPP, env->priv);
        s = set_field(s, MSTATUS_SIE, 0);
        env->mstatus = s;
        env->scause = cause | ((target_ulong)async << (TARGET_LONG_BITS - 1));
        env->sepc = env->pc;
        env->sbadaddr = tval;
        env->htval = htval;
        env->pc = (env->stvec >> 2 << 2) +
            ((async && (env->stvec & 3) == 1) ? cause * 4 : 0);
        riscv_cpu_set_mode(env, PRV_S);
    } else {
        /* handle the trap in M-mode */
        if (riscv_has_ext(env, RVH)) {
            if (riscv_cpu_virt_enabled(env)) {
                riscv_cpu_swap_hypervisor_regs(env);
            }
            env->mstatus = set_field(env->mstatus, MSTATUS_MPV,
                                     riscv_cpu_virt_enabled(env));
            if (riscv_cpu_virt_enabled(env) && tval) {
                env->mstatus = set_field(env->mstatus, MSTATUS_GVA, 1);
            }

            mtval2 = env->guest_phys_fault_addr;

            /* Trapping to M mode, virt is disabled */
            riscv_cpu_set_virt_enabled(env, 0);
            riscv_cpu_set_force_hs_excep(env, 0);
        }

        s = env->mstatus;
        s = set_field(s, MSTATUS_MPIE, get_field(s, MSTATUS_MIE));
        s = set_field(s, MSTATUS_MPP, env->priv);
        s = set_field(s, MSTATUS_MIE, 0);
        env->mstatus = s;
        env->mcause = cause | ~(((target_ulong)-1) >> async);
        env->mepc = env->pc;
        env->mbadaddr = tval;
        env->mtval2 = mtval2;
        env->pc = (env->mtvec >> 2 << 2) +
            ((async && (env->mtvec & 3) == 1) ? cause * 4 : 0);
        riscv_cpu_set_mode(env, PRV_M);
    }

    /* NOTE: it is not necessary to yield load reservations here. It is only
     * necessary for an SC from "another hart" to cause a load reservation
     * to be yielded. Refer to the memory consistency model section of the
     * RISC-V ISA Specification.
     */

#endif
    cs->exception_index = EXCP_NONE; /* mark handled to qemu */
}
