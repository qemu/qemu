/*
 *  emulator main execution loop
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
#include "exec/log.h"
#include "system/tcg.h"
#include "qemu/plugin.h"
#include "qemu/main-loop.h"
#include "accel/tcg/cpu-loop.h"
#include "internal-common.h"

bool tcg_allowed;

/*
 * The bits of CPUState::tcg_cflags that tcg_cflags_set() never sets, because
 * they are derived from gdb single-step, one-insn-per-tb and -d nochain.
 */
#define CF_DERIVED  (CF_COUNT_MASK | CF_NO_GOTO_TB | CF_NO_GOTO_PTR | \
                     CF_SINGLE_STEP)

bool tcg_cflags_has(CPUState *cpu, uint32_t flags)
{
    return cpu->tcg_cflags & flags;
}

void tcg_cflags_set(CPUState *cpu, uint32_t flags)
{
    assert((flags & CF_DERIVED) == 0);
    cpu->tcg_cflags |= flags;
}

void tcg_update_cflags(CPUState *cpu)
{
    uint32_t cflags = cpu->tcg_cflags & ~CF_DERIVED;

    /*
     * Record gdb single-step.  We should be exiting the TB by raising
     * EXCP_DEBUG, but to simplify other tests, disable chaining too.
     *
     * For singlestep and -d nochain, suppress goto_tb so that
     * we can log -d cpu,exec after every TB.
     */
    if (unlikely(cpu_single_stepping(cpu))) {
        cflags |= CF_NO_GOTO_TB | CF_NO_GOTO_PTR | CF_SINGLE_STEP | 1;
    } else if (qatomic_read(&one_insn_per_tb)) {
        cflags |= CF_NO_GOTO_TB | 1;
    } else if (qemu_loglevel_mask(CPU_LOG_TB_NOCHAIN)) {
        cflags |= CF_NO_GOTO_TB;
    }

    cpu->tcg_cflags = cflags;
}

static void tcg_update_cflags_work(CPUState *cpu, run_on_cpu_data data)
{
    tcg_update_cflags(cpu);
}

void tcg_update_all_cflags(void)
{
    CPUState *cpu;

    g_assert(bql_locked());

    /*
     * one-insn-per-tb and -d nochain can both be changed from the monitor
     * while the vCPUs are running.  Queue the update onto each CPU rather
     * than writing tcg_cflags from here, so that the field is only ever
     * written by the CPU that owns it.
     */
    CPU_FOREACH(cpu) {
        async_run_on_cpu(cpu, tcg_update_cflags_work, RUN_ON_CPU_NULL);
    }
}

/* exit the current TB, but without causing any exception to be raised */
void cpu_loop_exit_noexc(CPUState *cpu)
{
    cpu->exception_index = -1;
    cpu_loop_exit(cpu);
}

void cpu_loop_exit(CPUState *cpu)
{
    /* Undo the setting in cpu_tb_exec.  */
    cpu->neg.can_do_io = true;
    /* Undo any setting in generated code.  */
    qemu_plugin_disable_mem_helpers(cpu);
    siglongjmp(cpu->jmp_env, 1);
}

void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc)
{
    if (pc) {
        cpu_restore_state(cpu, pc);
    }
    cpu_loop_exit(cpu);
}

void cpu_loop_exit_atomic(CPUState *cpu, uintptr_t pc)
{
    /* Prevent looping if already executing in a serial context. */
    g_assert(!cpu_in_serial_context(cpu));
    cpu->exception_index = EXCP_ATOMIC;
    cpu_loop_exit_restore(cpu, pc);
}
