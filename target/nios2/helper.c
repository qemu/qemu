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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "cpu.h"
#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/exec-all.h"
#include "exec/log.h"
#include "exec/helper-proto.h"

#if defined(CONFIG_USER_ONLY)

void nios2_cpu_do_interrupt(CPUState *cs)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    cs->exception_index = -1;
    env->regs[R_EA] = env->regs[R_PC] + 4;
}

int nios2_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw, int mmu_idx)
{
    cs->exception_index = 0xaa;
    /* Page 0x1000 is kuser helper */
    if (address < 0x1000 || address >= 0x2000) {
        cpu_dump_state(cs, stderr, fprintf, 0);
    }
    return 1;
}

#else /* !CONFIG_USER_ONLY */

void nios2_cpu_do_interrupt(CPUState *cs)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    switch (cs->exception_index) {
    case EXCP_IRQ:
        assert(env->regs[CR_STATUS] & CR_STATUS_PIE);

        qemu_log_mask(CPU_LOG_INT, "interrupt at pc=%x\n", env->regs[R_PC]);

        env->regs[CR_ESTATUS] = env->regs[CR_STATUS];
        env->regs[CR_STATUS] |= CR_STATUS_IH;
        env->regs[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->regs[CR_EXCEPTION] &= ~(0x1F << 2);
        env->regs[CR_EXCEPTION] |= (cs->exception_index & 0x1F) << 2;

        env->regs[R_EA] = env->regs[R_PC] + 4;
        env->regs[R_PC] = cpu->exception_addr;
        break;

    case EXCP_TLBD:
        if ((env->regs[CR_STATUS] & CR_STATUS_EH) == 0) {
            qemu_log_mask(CPU_LOG_INT, "TLB MISS (fast) at pc=%x\n",
                          env->regs[R_PC]);

            /* Fast TLB miss */
            /* Variation from the spec. Table 3-35 of the cpu reference shows
             * estatus not being changed for TLB miss but this appears to
             * be incorrect. */
            env->regs[CR_ESTATUS] = env->regs[CR_STATUS];
            env->regs[CR_STATUS] |= CR_STATUS_EH;
            env->regs[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

            env->regs[CR_EXCEPTION] &= ~(0x1F << 2);
            env->regs[CR_EXCEPTION] |= (cs->exception_index & 0x1F) << 2;

            env->regs[CR_TLBMISC] &= ~CR_TLBMISC_DBL;
            env->regs[CR_TLBMISC] |= CR_TLBMISC_WR;

            env->regs[R_EA] = env->regs[R_PC] + 4;
            env->regs[R_PC] = cpu->fast_tlb_miss_addr;
        } else {
            qemu_log_mask(CPU_LOG_INT, "TLB MISS (double) at pc=%x\n",
                          env->regs[R_PC]);

            /* Double TLB miss */
            env->regs[CR_STATUS] |= CR_STATUS_EH;
            env->regs[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

            env->regs[CR_EXCEPTION] &= ~(0x1F << 2);
            env->regs[CR_EXCEPTION] |= (cs->exception_index & 0x1F) << 2;

            env->regs[CR_TLBMISC] |= CR_TLBMISC_DBL;

            env->regs[R_PC] = cpu->exception_addr;
        }
        break;

    case EXCP_TLBR:
    case EXCP_TLBW:
    case EXCP_TLBX:
        qemu_log_mask(CPU_LOG_INT, "TLB PERM at pc=%x\n", env->regs[R_PC]);

        env->regs[CR_ESTATUS] = env->regs[CR_STATUS];
        env->regs[CR_STATUS] |= CR_STATUS_EH;
        env->regs[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->regs[CR_EXCEPTION] &= ~(0x1F << 2);
        env->regs[CR_EXCEPTION] |= (cs->exception_index & 0x1F) << 2;

        if ((env->regs[CR_STATUS] & CR_STATUS_EH) == 0) {
            env->regs[CR_TLBMISC] |= CR_TLBMISC_WR;
        }

        env->regs[R_EA] = env->regs[R_PC] + 4;
        env->regs[R_PC] = cpu->exception_addr;
        break;

    case EXCP_SUPERA:
    case EXCP_SUPERI:
    case EXCP_SUPERD:
        qemu_log_mask(CPU_LOG_INT, "SUPERVISOR exception at pc=%x\n",
                      env->regs[R_PC]);

        if ((env->regs[CR_STATUS] & CR_STATUS_EH) == 0) {
            env->regs[CR_ESTATUS] = env->regs[CR_STATUS];
            env->regs[R_EA] = env->regs[R_PC] + 4;
        }

        env->regs[CR_STATUS] |= CR_STATUS_EH;
        env->regs[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->regs[CR_EXCEPTION] &= ~(0x1F << 2);
        env->regs[CR_EXCEPTION] |= (cs->exception_index & 0x1F) << 2;

        env->regs[R_PC] = cpu->exception_addr;
        break;

    case EXCP_ILLEGAL:
    case EXCP_TRAP:
        qemu_log_mask(CPU_LOG_INT, "TRAP exception at pc=%x\n",
                      env->regs[R_PC]);

        if ((env->regs[CR_STATUS] & CR_STATUS_EH) == 0) {
            env->regs[CR_ESTATUS] = env->regs[CR_STATUS];
            env->regs[R_EA] = env->regs[R_PC] + 4;
        }

        env->regs[CR_STATUS] |= CR_STATUS_EH;
        env->regs[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->regs[CR_EXCEPTION] &= ~(0x1F << 2);
        env->regs[CR_EXCEPTION] |= (cs->exception_index & 0x1F) << 2;

        env->regs[R_PC] = cpu->exception_addr;
        break;

    case EXCP_BREAK:
        if ((env->regs[CR_STATUS] & CR_STATUS_EH) == 0) {
            env->regs[CR_BSTATUS] = env->regs[CR_STATUS];
            env->regs[R_BA] = env->regs[R_PC] + 4;
        }

        env->regs[CR_STATUS] |= CR_STATUS_EH;
        env->regs[CR_STATUS] &= ~(CR_STATUS_PIE | CR_STATUS_U);

        env->regs[CR_EXCEPTION] &= ~(0x1F << 2);
        env->regs[CR_EXCEPTION] |= (cs->exception_index & 0x1F) << 2;

        env->regs[R_PC] = cpu->exception_addr;
        break;

    default:
        cpu_abort(cs, "unhandled exception type=%d\n",
                  cs->exception_index);
        break;
    }
}

static int cpu_nios2_handle_virtual_page(
    CPUState *cs, target_ulong address, int rw, int mmu_idx)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    target_ulong vaddr, paddr;
    Nios2MMULookup lu;
    unsigned int hit;
    hit = mmu_translate(env, &lu, address, rw, mmu_idx);
    if (hit) {
        vaddr = address & TARGET_PAGE_MASK;
        paddr = lu.paddr + vaddr - lu.vaddr;

        if (((rw == 0) && (lu.prot & PAGE_READ)) ||
            ((rw == 1) && (lu.prot & PAGE_WRITE)) ||
            ((rw == 2) && (lu.prot & PAGE_EXEC))) {

            tlb_set_page(cs, vaddr, paddr, lu.prot,
                         mmu_idx, TARGET_PAGE_SIZE);
            return 0;
        } else {
            /* Permission violation */
            cs->exception_index = (rw == 0) ? EXCP_TLBR :
                                               ((rw == 1) ? EXCP_TLBW :
                                                            EXCP_TLBX);
        }
    } else {
        cs->exception_index = EXCP_TLBD;
    }

    if (rw == 2) {
        env->regs[CR_TLBMISC] &= ~CR_TLBMISC_D;
    } else {
        env->regs[CR_TLBMISC] |= CR_TLBMISC_D;
    }
    env->regs[CR_PTEADDR] &= CR_PTEADDR_PTBASE_MASK;
    env->regs[CR_PTEADDR] |= (address >> 10) & CR_PTEADDR_VPN_MASK;
    env->mmu.pteaddr_wr = env->regs[CR_PTEADDR];
    env->regs[CR_BADADDR] = address;
    return 1;
}

int nios2_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw, int mmu_idx)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    if (cpu->mmu_present) {
        if (MMU_SUPERVISOR_IDX == mmu_idx) {
            if (address >= 0xC0000000) {
                /* Kernel physical page - TLB bypassed */
                address &= TARGET_PAGE_MASK;
                tlb_set_page(cs, address, address, PAGE_BITS,
                             mmu_idx, TARGET_PAGE_SIZE);
            } else if (address >= 0x80000000) {
                /* Kernel virtual page */
                return cpu_nios2_handle_virtual_page(cs, address, rw, mmu_idx);
            } else {
                /* User virtual page */
                return cpu_nios2_handle_virtual_page(cs, address, rw, mmu_idx);
            }
        } else {
            if (address >= 0x80000000) {
                /* Illegal access from user mode */
                cs->exception_index = EXCP_SUPERA;
                env->regs[CR_BADADDR] = address;
                return 1;
            } else {
                /* User virtual page */
                return cpu_nios2_handle_virtual_page(cs, address, rw, mmu_idx);
            }
        }
    } else {
        /* No MMU */
        address &= TARGET_PAGE_MASK;
        tlb_set_page(cs, address, address, PAGE_BITS,
                     mmu_idx, TARGET_PAGE_SIZE);
    }

    return 0;
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

    env->regs[CR_BADADDR] = addr;
    env->regs[CR_EXCEPTION] = EXCP_UNALIGN << 2;
    helper_raise_exception(env, EXCP_UNALIGN);
}
#endif /* !CONFIG_USER_ONLY */
