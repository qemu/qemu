/*
 * Sparc32 interrupt helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "qemu/main-loop.h"
#include "cpu.h"
#include "trace.h"
#include "exec/log.h"
#include "sysemu/runstate.h"


static const char * const excp_names[0x80] = {
    [TT_TFAULT] = "Instruction Access Fault",
    [TT_ILL_INSN] = "Illegal Instruction",
    [TT_PRIV_INSN] = "Privileged Instruction",
    [TT_NFPU_INSN] = "FPU Disabled",
    [TT_WIN_OVF] = "Window Overflow",
    [TT_WIN_UNF] = "Window Underflow",
    [TT_UNALIGNED] = "Unaligned Memory Access",
    [TT_FP_EXCP] = "FPU Exception",
    [TT_DFAULT] = "Data Access Fault",
    [TT_TOVF] = "Tag Overflow",
    [TT_EXTINT | 0x1] = "External Interrupt 1",
    [TT_EXTINT | 0x2] = "External Interrupt 2",
    [TT_EXTINT | 0x3] = "External Interrupt 3",
    [TT_EXTINT | 0x4] = "External Interrupt 4",
    [TT_EXTINT | 0x5] = "External Interrupt 5",
    [TT_EXTINT | 0x6] = "External Interrupt 6",
    [TT_EXTINT | 0x7] = "External Interrupt 7",
    [TT_EXTINT | 0x8] = "External Interrupt 8",
    [TT_EXTINT | 0x9] = "External Interrupt 9",
    [TT_EXTINT | 0xa] = "External Interrupt 10",
    [TT_EXTINT | 0xb] = "External Interrupt 11",
    [TT_EXTINT | 0xc] = "External Interrupt 12",
    [TT_EXTINT | 0xd] = "External Interrupt 13",
    [TT_EXTINT | 0xe] = "External Interrupt 14",
    [TT_EXTINT | 0xf] = "External Interrupt 15",
    [TT_CODE_ACCESS] = "Instruction Access Error",
    [TT_DATA_ACCESS] = "Data Access Error",
    [TT_DIV_ZERO] = "Division By Zero",
    [TT_NCP_INSN] = "Coprocessor Disabled",
};

static const char *excp_name_str(int32_t exception_index)
{
    if (exception_index < 0 || exception_index >= ARRAY_SIZE(excp_names)) {
        return "Unknown";
    }
    return excp_names[exception_index];
}

void cpu_check_irqs(CPUSPARCState *env)
{
    CPUState *cs;

    /* We should be holding the BQL before we mess with IRQs */
    g_assert(bql_locked());

    if (env->pil_in && (env->interrupt_index == 0 ||
                        (env->interrupt_index & ~15) == TT_EXTINT)) {
        unsigned int i;

        for (i = 15; i > 0; i--) {
            if (env->pil_in & (1 << i)) {
                int old_interrupt = env->interrupt_index;

                env->interrupt_index = TT_EXTINT | i;
                if (old_interrupt != env->interrupt_index) {
                    cs = env_cpu(env);
                    trace_sun4m_cpu_interrupt(i);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (!env->pil_in && (env->interrupt_index & ~15) == TT_EXTINT) {
        cs = env_cpu(env);
        trace_sun4m_cpu_reset_interrupt(env->interrupt_index & 15);
        env->interrupt_index = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

void sparc_cpu_do_interrupt(CPUState *cs)
{
    SPARCCPU *cpu = SPARC_CPU(cs);
    CPUSPARCState *env = &cpu->env;
    int cwp, intno = cs->exception_index;

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static int count;
        const char *name;

        if (intno < 0 || intno >= 0x100) {
            name = "Unknown";
        } else if (intno >= 0x80) {
            name = "Trap Instruction";
        } else {
            name = excp_name_str(intno);
        }

        qemu_log("%6d: %s (v=%02x)\n", count, name, intno);
        log_cpu_state(cs, 0);
#if 0
        {
            int i;
            uint8_t *ptr;

            qemu_log("       code=");
            ptr = (uint8_t *)env->pc;
            for (i = 0; i < 16; i++) {
                qemu_log(" %02x", ldub(ptr + i));
            }
            qemu_log("\n");
        }
#endif
        count++;
    }
#if !defined(CONFIG_USER_ONLY)
    if (env->psret == 0) {
        if (cs->exception_index == 0x80 &&
            env->def.features & CPU_FEATURE_TA0_SHUTDOWN) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        } else {
            cpu_abort(cs, "Trap 0x%02x (%s) while interrupts disabled, "
                          "Error state",
                      cs->exception_index, excp_name_str(cs->exception_index));
        }
        return;
    }
#endif
    env->psret = 0;
    cwp = cpu_cwp_dec(env, env->cwp - 1);
    cpu_set_cwp(env, cwp);
    env->regwptr[9] = env->pc;
    env->regwptr[10] = env->npc;
    env->psrps = env->psrs;
    env->psrs = 1;
    env->tbr = (env->tbr & TBR_BASE_MASK) | (intno << 4);
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    cs->exception_index = -1;

#if !defined(CONFIG_USER_ONLY)
    /* IRQ acknowledgment */
    if ((intno & ~15) == TT_EXTINT && env->qemu_irq_ack != NULL) {
        env->qemu_irq_ack(env, intno);
    }
#endif
}
