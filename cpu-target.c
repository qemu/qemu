/*
 * Target-specific parts of the CPU object
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "cpu.h"
#include "accel/accel-cpu-ops.h"
#include "system/cpus.h"
#include "exec/cpu-common.h"
#include "exec/replay-core.h"
#include "exec/log.h"
#include "hw/core/cpu.h"
#include "trace/trace-root.h"

/* Validate correct placement of CPUArchState. */
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUState *cpu, int enabled)
{
    if (cpu->singlestep_enabled != enabled) {
        cpu->singlestep_enabled = enabled;

#if !defined(CONFIG_USER_ONLY)
        const AccelOpsClass *ops = cpus_get_accel();
        if (ops->update_guest_debug) {
            ops->update_guest_debug(cpu);
        }
#endif

        trace_breakpoint_singlestep(cpu->cpu_index, enabled);
    }
}

void cpu_abort(CPUState *cpu, const char *fmt, ...)
{
    va_list ap;
    va_list ap2;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    fprintf(stderr, "qemu: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    cpu_dump_state(cpu, stderr, CPU_DUMP_FPU | CPU_DUMP_CCOP);
    if (qemu_log_separate()) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "qemu: fatal: ");
            vfprintf(logfile, fmt, ap2);
            fprintf(logfile, "\n");
            cpu_dump_state(cpu, logfile, CPU_DUMP_FPU | CPU_DUMP_CCOP);
            qemu_log_unlock(logfile);
        }
    }
    va_end(ap2);
    va_end(ap);
    replay_finish();
#if defined(CONFIG_USER_ONLY)
    {
        struct sigaction act;
        sigfillset(&act.sa_mask);
        act.sa_handler = SIG_DFL;
        act.sa_flags = 0;
        sigaction(SIGABRT, &act, NULL);
    }
#endif
    abort();
}
