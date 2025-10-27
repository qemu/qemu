/*
 * OpenRISC interrupt.
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "gdbstub/helpers.h"
#include "qemu/host-utils.h"
#ifndef CONFIG_USER_ONLY
#include "hw/loader.h"
#endif
#include "qemu/plugin.h"

void openrisc_cpu_do_interrupt(CPUState *cs)
{
    CPUOpenRISCState *env = cpu_env(cs);
    int exception = cs->exception_index;
    uint64_t last_pc = env->pc;

    env->epcr = env->pc;

    /* When we have an illegal instruction the error effective address
       shall be set to the illegal instruction address.  */
    if (exception == EXCP_ILLEGAL) {
        env->eear = env->pc;
    }

    /* During exceptions esr is populared with the pre-exception sr.  */
    env->esr = cpu_get_sr(env);
    /* In parallel sr is updated to disable mmu, interrupts, timers and
       set the delay slot exception flag.  */
    env->sr &= ~SR_DME;
    env->sr &= ~SR_IME;
    env->sr |= SR_SM;
    env->sr &= ~SR_IEE;
    env->sr &= ~SR_TEE;
    env->pmr &= ~PMR_DME;
    env->pmr &= ~PMR_SME;
    env->lock_addr = -1;

    /* Set/clear dsx to indicate if we are in a delay slot exception.  */
    if (env->dflag) {
        env->dflag = 0;
        env->sr |= SR_DSX;
        env->epcr -= 4;
    } else {
        env->sr &= ~SR_DSX;
        if (exception == EXCP_SYSCALL || exception == EXCP_FPE) {
            env->epcr += 4;
        }
    }

    if (exception > 0 && exception < EXCP_NR) {
        static const char * const int_name[EXCP_NR] = {
            [EXCP_RESET]    = "RESET",
            [EXCP_BUSERR]   = "BUSERR (bus error)",
            [EXCP_DPF]      = "DFP (data protection fault)",
            [EXCP_IPF]      = "IPF (code protection fault)",
            [EXCP_TICK]     = "TICK (timer interrupt)",
            [EXCP_ALIGN]    = "ALIGN",
            [EXCP_ILLEGAL]  = "ILLEGAL",
            [EXCP_INT]      = "INT (device interrupt)",
            [EXCP_DTLBMISS] = "DTLBMISS (data tlb miss)",
            [EXCP_ITLBMISS] = "ITLBMISS (code tlb miss)",
            [EXCP_RANGE]    = "RANGE",
            [EXCP_SYSCALL]  = "SYSCALL",
            [EXCP_FPE]      = "FPE",
            [EXCP_TRAP]     = "TRAP",
        };

        qemu_log_mask(CPU_LOG_INT, "CPU: %d INT: %s\n",
                      cs->cpu_index,
                      int_name[exception]);

        hwaddr vect_pc = exception << 8;
        if (env->cpucfgr & CPUCFGR_EVBARP) {
            vect_pc |= env->evbar;
        }
        if (env->sr & SR_EPH) {
            vect_pc |= 0xf0000000;
        }
        env->pc = vect_pc;
    } else {
        cpu_abort(cs, "Unhandled exception 0x%x\n", exception);
    }

    switch (exception) {
    case EXCP_RESET:
        /* Resets are already exposed to plugins through a dedicated callback */
        break;
    case EXCP_TICK:
    case EXCP_INT:
        qemu_plugin_vcpu_interrupt_cb(cs, last_pc);
        break;
    default:
        qemu_plugin_vcpu_exception_cb(cs, last_pc);
        break;
    }

    cs->exception_index = -1;
}

bool openrisc_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUOpenRISCState *env = cpu_env(cs);
    int idx = -1;

    if ((interrupt_request & CPU_INTERRUPT_HARD) && (env->sr & SR_IEE)) {
        idx = EXCP_INT;
    }
    if ((interrupt_request & CPU_INTERRUPT_TIMER) && (env->sr & SR_TEE)) {
        idx = EXCP_TICK;
    }
    if (idx >= 0) {
        cs->exception_index = idx;
        openrisc_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}
