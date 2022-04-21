/*
 * Altera Nios II helper routines.
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"
#include "exec/helper-proto.h"
#include "semihosting/semihost.h"


void nios2_cpu_do_interrupt(CPUState *cs)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    switch (cs->exception_index) {
    case EXCP_IRQ:
        assert(env->ctrl[CR_STATUS] & CR_STATUS_PIE);

        qemu_log_mask(CPU_LOG_INT, "interrupt at pc=%x\n", env->pc);

        env->ctrl[CR_ESTATUS] = env->ctrl[CR_STATUS];
        env->ctrl[CR_STATUS] |= CR_STATUS_IH;
        env->ctrl[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->ctrl[CR_EXCEPTION] = FIELD_DP32(env->ctrl[CR_EXCEPTION],
                                             CR_EXCEPTION, CAUSE,
                                             cs->exception_index);

        env->regs[R_EA] = env->pc + 4;
        env->pc = cpu->exception_addr;
        break;

    case EXCP_TLBD:
        if ((env->ctrl[CR_STATUS] & CR_STATUS_EH) == 0) {
            qemu_log_mask(CPU_LOG_INT, "TLB MISS (fast) at pc=%x\n", env->pc);

            /* Fast TLB miss */
            /* Variation from the spec. Table 3-35 of the cpu reference shows
             * estatus not being changed for TLB miss but this appears to
             * be incorrect. */
            env->ctrl[CR_ESTATUS] = env->ctrl[CR_STATUS];
            env->ctrl[CR_STATUS] |= CR_STATUS_EH;
            env->ctrl[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

            env->ctrl[CR_EXCEPTION] = FIELD_DP32(env->ctrl[CR_EXCEPTION],
                                                 CR_EXCEPTION, CAUSE,
                                                 cs->exception_index);

            env->ctrl[CR_TLBMISC] &= ~CR_TLBMISC_DBL;
            env->ctrl[CR_TLBMISC] |= CR_TLBMISC_WE;

            env->regs[R_EA] = env->pc + 4;
            env->pc = cpu->fast_tlb_miss_addr;
        } else {
            qemu_log_mask(CPU_LOG_INT, "TLB MISS (double) at pc=%x\n", env->pc);

            /* Double TLB miss */
            env->ctrl[CR_STATUS] |= CR_STATUS_EH;
            env->ctrl[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

            env->ctrl[CR_EXCEPTION] = FIELD_DP32(env->ctrl[CR_EXCEPTION],
                                                 CR_EXCEPTION, CAUSE,
                                                 cs->exception_index);

            env->ctrl[CR_TLBMISC] |= CR_TLBMISC_DBL;

            env->pc = cpu->exception_addr;
        }
        break;

    case EXCP_TLBR:
    case EXCP_TLBW:
    case EXCP_TLBX:
        qemu_log_mask(CPU_LOG_INT, "TLB PERM at pc=%x\n", env->pc);

        env->ctrl[CR_ESTATUS] = env->ctrl[CR_STATUS];
        env->ctrl[CR_STATUS] |= CR_STATUS_EH;
        env->ctrl[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->ctrl[CR_EXCEPTION] = FIELD_DP32(env->ctrl[CR_EXCEPTION],
                                             CR_EXCEPTION, CAUSE,
                                             cs->exception_index);

        if ((env->ctrl[CR_STATUS] & CR_STATUS_EH) == 0) {
            env->ctrl[CR_TLBMISC] |= CR_TLBMISC_WE;
        }

        env->regs[R_EA] = env->pc + 4;
        env->pc = cpu->exception_addr;
        break;

    case EXCP_SUPERA:
    case EXCP_SUPERI:
    case EXCP_SUPERD:
        qemu_log_mask(CPU_LOG_INT, "SUPERVISOR exception at pc=%x\n", env->pc);

        if ((env->ctrl[CR_STATUS] & CR_STATUS_EH) == 0) {
            env->ctrl[CR_ESTATUS] = env->ctrl[CR_STATUS];
            env->regs[R_EA] = env->pc + 4;
        }

        env->ctrl[CR_STATUS] |= CR_STATUS_EH;
        env->ctrl[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->ctrl[CR_EXCEPTION] = FIELD_DP32(env->ctrl[CR_EXCEPTION],
                                             CR_EXCEPTION, CAUSE,
                                             cs->exception_index);

        env->pc = cpu->exception_addr;
        break;

    case EXCP_ILLEGAL:
    case EXCP_TRAP:
        qemu_log_mask(CPU_LOG_INT, "TRAP exception at pc=%x\n", env->pc);

        if ((env->ctrl[CR_STATUS] & CR_STATUS_EH) == 0) {
            env->ctrl[CR_ESTATUS] = env->ctrl[CR_STATUS];
            env->regs[R_EA] = env->pc + 4;
        }

        env->ctrl[CR_STATUS] |= CR_STATUS_EH;
        env->ctrl[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->ctrl[CR_EXCEPTION] = FIELD_DP32(env->ctrl[CR_EXCEPTION],
                                             CR_EXCEPTION, CAUSE,
                                             cs->exception_index);

        env->pc = cpu->exception_addr;
        break;

    case EXCP_BREAK:
        qemu_log_mask(CPU_LOG_INT, "BREAK exception at pc=%x\n", env->pc);

        if ((env->ctrl[CR_STATUS] & CR_STATUS_EH) == 0) {
            env->ctrl[CR_BSTATUS] = env->ctrl[CR_STATUS];
            env->regs[R_BA] = env->pc + 4;
        }

        env->ctrl[CR_STATUS] |= CR_STATUS_EH;
        env->ctrl[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->ctrl[CR_EXCEPTION] = FIELD_DP32(env->ctrl[CR_EXCEPTION],
                                             CR_EXCEPTION, CAUSE,
                                             cs->exception_index);

        env->pc = cpu->exception_addr;
        break;

    case EXCP_SEMIHOST:
        qemu_log_mask(CPU_LOG_INT, "BREAK semihosting at pc=%x\n", env->pc);
        env->pc += 4;
        do_nios2_semihosting(env);
        break;

    default:
        cpu_abort(cs, "unhandled exception type=%d\n",
                  cs->exception_index);
        break;
    }
}

hwaddr nios2_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    target_ulong vaddr, paddr = 0;
    Nios2MMULookup lu;
    unsigned int hit;

    if (cpu->mmu_present && (addr < 0xC0000000)) {
        hit = mmu_translate(env, &lu, addr, 0, 0);
        if (hit) {
            vaddr = addr & TARGET_PAGE_MASK;
            paddr = lu.paddr + vaddr - lu.vaddr;
        } else {
            paddr = -1;
            qemu_log("cpu_get_phys_page debug MISS: %#" PRIx64 "\n", addr);
        }
    } else {
        paddr = addr & TARGET_PAGE_MASK;
    }

    return paddr;
}

void nios2_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                   MMUAccessType access_type,
                                   int mmu_idx, uintptr_t retaddr)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    env->ctrl[CR_BADADDR] = addr;
    env->ctrl[CR_EXCEPTION] = FIELD_DP32(0, CR_EXCEPTION, CAUSE, EXCP_UNALIGN);
    helper_raise_exception(env, EXCP_UNALIGN);
}

bool nios2_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    unsigned int excp = EXCP_TLBD;
    target_ulong vaddr, paddr;
    Nios2MMULookup lu;
    unsigned int hit;

    if (!cpu->mmu_present) {
        /* No MMU */
        address &= TARGET_PAGE_MASK;
        tlb_set_page(cs, address, address, PAGE_BITS,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }

    if (MMU_SUPERVISOR_IDX == mmu_idx) {
        if (address >= 0xC0000000) {
            /* Kernel physical page - TLB bypassed */
            address &= TARGET_PAGE_MASK;
            tlb_set_page(cs, address, address, PAGE_BITS,
                         mmu_idx, TARGET_PAGE_SIZE);
            return true;
        }
    } else {
        if (address >= 0x80000000) {
            /* Illegal access from user mode */
            if (probe) {
                return false;
            }
            cs->exception_index = EXCP_SUPERA;
            env->ctrl[CR_BADADDR] = address;
            cpu_loop_exit_restore(cs, retaddr);
        }
    }

    /* Virtual page.  */
    hit = mmu_translate(env, &lu, address, access_type, mmu_idx);
    if (hit) {
        vaddr = address & TARGET_PAGE_MASK;
        paddr = lu.paddr + vaddr - lu.vaddr;

        if (((access_type == MMU_DATA_LOAD) && (lu.prot & PAGE_READ)) ||
            ((access_type == MMU_DATA_STORE) && (lu.prot & PAGE_WRITE)) ||
            ((access_type == MMU_INST_FETCH) && (lu.prot & PAGE_EXEC))) {
            tlb_set_page(cs, vaddr, paddr, lu.prot,
                         mmu_idx, TARGET_PAGE_SIZE);
            return true;
        }

        /* Permission violation */
        excp = (access_type == MMU_DATA_LOAD ? EXCP_TLBR :
                access_type == MMU_DATA_STORE ? EXCP_TLBW : EXCP_TLBX);
    }

    if (probe) {
        return false;
    }

    env->ctrl[CR_TLBMISC] = FIELD_DP32(env->ctrl[CR_TLBMISC], CR_TLBMISC, D,
                                       access_type != MMU_INST_FETCH);
    env->ctrl[CR_PTEADDR] = FIELD_DP32(env->ctrl[CR_PTEADDR], CR_PTEADDR, VPN,
                                       address >> TARGET_PAGE_BITS);
    env->mmu.pteaddr_wr = env->ctrl[CR_PTEADDR];

    cs->exception_index = excp;
    env->ctrl[CR_BADADDR] = address;
    cpu_loop_exit_restore(cs, retaddr);
}
