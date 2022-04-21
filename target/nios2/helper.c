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


static void do_exception(Nios2CPU *cpu, uint32_t exception_addr,
                         uint32_t tlbmisc_set, bool is_break)
{
    CPUNios2State *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    uint32_t old_status = env->ctrl[CR_STATUS];
    uint32_t new_status = old_status;

    /* With shadow regs, exceptions are always taken into CRS 0. */
    new_status &= ~R_CR_STATUS_CRS_MASK;
    env->regs = env->shadow_regs[0];

    if ((old_status & CR_STATUS_EH) == 0) {
        int r_ea = R_EA, cr_es = CR_ESTATUS;

        if (is_break) {
            r_ea = R_BA;
            cr_es = CR_BSTATUS;
        }
        env->ctrl[cr_es] = old_status;
        env->regs[r_ea] = env->pc;

        if (cpu->mmu_present) {
            new_status |= CR_STATUS_EH;

            /*
             * There are 4 bits that are always written.
             * Explicitly clear them, to be set via the argument.
             */
            env->ctrl[CR_TLBMISC] &= ~(CR_TLBMISC_D |
                                       CR_TLBMISC_PERM |
                                       CR_TLBMISC_BAD |
                                       CR_TLBMISC_DBL);
            env->ctrl[CR_TLBMISC] |= tlbmisc_set;
        }

        /*
         * With shadow regs, and EH == 0, PRS is set from CRS.
         * At least, so says Table 3-9, and some other text,
         * though Table 3-38 says otherwise.
         */
        new_status = FIELD_DP32(new_status, CR_STATUS, PRS,
                                FIELD_EX32(old_status, CR_STATUS, CRS));
    }

    new_status &= ~(CR_STATUS_PIE | CR_STATUS_U);

    env->ctrl[CR_STATUS] = new_status;
    if (!is_break) {
        env->ctrl[CR_EXCEPTION] = FIELD_DP32(0, CR_EXCEPTION, CAUSE,
                                             cs->exception_index);
    }
    env->pc = exception_addr;
}

static void do_iic_irq(Nios2CPU *cpu)
{
    do_exception(cpu, cpu->exception_addr, 0, false);
}

static void do_eic_irq(Nios2CPU *cpu)
{
    CPUNios2State *env = &cpu->env;
    uint32_t old_status = env->ctrl[CR_STATUS];
    uint32_t new_status = old_status;
    uint32_t old_rs = FIELD_EX32(old_status, CR_STATUS, CRS);
    uint32_t new_rs = cpu->rrs;

    new_status = FIELD_DP32(new_status, CR_STATUS, CRS, new_rs);
    new_status = FIELD_DP32(new_status, CR_STATUS, IL, cpu->ril);
    new_status = FIELD_DP32(new_status, CR_STATUS, NMI, cpu->rnmi);
    new_status &= ~(CR_STATUS_RSIE | CR_STATUS_U);
    new_status |= CR_STATUS_IH;

    if (!(new_status & CR_STATUS_EH)) {
        new_status = FIELD_DP32(new_status, CR_STATUS, PRS, old_rs);
        if (new_rs == 0) {
            env->ctrl[CR_ESTATUS] = old_status;
        } else {
            if (new_rs != old_rs) {
                old_status |= CR_STATUS_SRS;
            }
            env->shadow_regs[new_rs][R_SSTATUS] = old_status;
        }
        env->shadow_regs[new_rs][R_EA] = env->pc;
    }

    env->ctrl[CR_STATUS] = new_status;
    nios2_update_crs(env);

    env->pc = cpu->rha;
}

void nios2_cpu_do_interrupt(CPUState *cs)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    uint32_t tlbmisc_set = 0;

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        const char *name = NULL;

        switch (cs->exception_index) {
        case EXCP_IRQ:
            name = "interrupt";
            break;
        case EXCP_TLB_X:
        case EXCP_TLB_D:
            if (env->ctrl[CR_STATUS] & CR_STATUS_EH) {
                name = "TLB MISS (double)";
            } else {
                name = "TLB MISS (fast)";
            }
            break;
        case EXCP_PERM_R:
        case EXCP_PERM_W:
        case EXCP_PERM_X:
            name = "TLB PERM";
            break;
        case EXCP_SUPERA_X:
        case EXCP_SUPERA_D:
            name = "SUPERVISOR (address)";
            break;
        case EXCP_SUPERI:
            name = "SUPERVISOR (insn)";
            break;
        case EXCP_ILLEGAL:
            name = "ILLEGAL insn";
            break;
        case EXCP_UNALIGN:
            name = "Misaligned (data)";
            break;
        case EXCP_UNALIGND:
            name = "Misaligned (destination)";
            break;
        case EXCP_DIV:
            name = "DIV error";
            break;
        case EXCP_TRAP:
            name = "TRAP insn";
            break;
        case EXCP_BREAK:
            name = "BREAK insn";
            break;
        case EXCP_SEMIHOST:
            name = "SEMIHOST insn";
            break;
        }
        if (name) {
            qemu_log("%s at pc=0x%08x\n", name, env->pc);
        } else {
            qemu_log("Unknown exception %d at pc=0x%08x\n",
                     cs->exception_index, env->pc);
        }
    }

    switch (cs->exception_index) {
    case EXCP_IRQ:
        /* Note that PC is advanced for interrupts as well. */
        env->pc += 4;
        if (cpu->eic_present) {
            do_eic_irq(cpu);
        } else {
            do_iic_irq(cpu);
        }
        break;

    case EXCP_TLB_D:
        tlbmisc_set = CR_TLBMISC_D;
        /* fall through */
    case EXCP_TLB_X:
        if (env->ctrl[CR_STATUS] & CR_STATUS_EH) {
            tlbmisc_set |= CR_TLBMISC_DBL;
            /*
             * Normally, we don't write to tlbmisc unless !EH,
             * so do it manually for the double-tlb miss exception.
             */
            env->ctrl[CR_TLBMISC] &= ~(CR_TLBMISC_D |
                                       CR_TLBMISC_PERM |
                                       CR_TLBMISC_BAD);
            env->ctrl[CR_TLBMISC] |= tlbmisc_set;
            do_exception(cpu, cpu->exception_addr, 0, false);
        } else {
            tlbmisc_set |= CR_TLBMISC_WE;
            do_exception(cpu, cpu->fast_tlb_miss_addr, tlbmisc_set, false);
        }
        break;

    case EXCP_PERM_R:
    case EXCP_PERM_W:
        tlbmisc_set = CR_TLBMISC_D;
        /* fall through */
    case EXCP_PERM_X:
        tlbmisc_set |= CR_TLBMISC_PERM;
        if (!(env->ctrl[CR_STATUS] & CR_STATUS_EH)) {
            tlbmisc_set |= CR_TLBMISC_WE;
        }
        do_exception(cpu, cpu->exception_addr, tlbmisc_set, false);
        break;

    case EXCP_SUPERA_D:
    case EXCP_UNALIGN:
        tlbmisc_set = CR_TLBMISC_D;
        /* fall through */
    case EXCP_SUPERA_X:
    case EXCP_UNALIGND:
        tlbmisc_set |= CR_TLBMISC_BAD;
        do_exception(cpu, cpu->exception_addr, tlbmisc_set, false);
        break;

    case EXCP_SUPERI:
    case EXCP_ILLEGAL:
    case EXCP_DIV:
    case EXCP_TRAP:
        do_exception(cpu, cpu->exception_addr, 0, false);
        break;

    case EXCP_BREAK:
        do_exception(cpu, cpu->exception_addr, 0, true);
        break;

    case EXCP_SEMIHOST:
        do_nios2_semihosting(env);
        break;

    default:
        cpu_abort(cs, "unhandled exception type=%d\n", cs->exception_index);
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
    cs->exception_index = EXCP_UNALIGN;
    nios2_cpu_loop_exit_advance(env, retaddr);
}

bool nios2_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    unsigned int excp;
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
            cs->exception_index = (access_type == MMU_INST_FETCH
                                   ? EXCP_SUPERA_X : EXCP_SUPERA_D);
            env->ctrl[CR_BADADDR] = address;
            nios2_cpu_loop_exit_advance(env, retaddr);
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
        excp = (access_type == MMU_DATA_LOAD ? EXCP_PERM_R :
                access_type == MMU_DATA_STORE ? EXCP_PERM_W : EXCP_PERM_X);
    } else {
        excp = (access_type == MMU_INST_FETCH ? EXCP_TLB_X: EXCP_TLB_D);
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
    nios2_cpu_loop_exit_advance(env, retaddr);
}
