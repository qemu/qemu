/*
 *  Emulation of BSD signals
 *
 *  Copyright (c) 2003 - 2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "signal-common.h"
#include "hw/core/tcg-cpu-ops.h"

/*
 * Stubbed out routines until we merge signal support from bsd-user
 * fork.
 */

/*
 * Queue a signal so that it will be send to the virtual CPU as soon as
 * possible.
 */
void queue_signal(CPUArchState *env, int sig, target_siginfo_t *info)
{
    qemu_log_mask(LOG_UNIMP, "No signal queueing, dropping signal %d\n", sig);
}

/*
 * Force a synchronously taken QEMU_SI_FAULT signal. For QEMU the
 * 'force' part is handled in process_pending_signals().
 */
void force_sig_fault(int sig, int code, abi_ulong addr)
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    target_siginfo_t info = {};

    info.si_signo = sig;
    info.si_errno = 0;
    info.si_code = code;
    info.si_addr = addr;
    queue_signal(env, sig, &info);
}

void signal_init(void)
{
}

void process_pending_signals(CPUArchState *cpu_env)
{
}

void cpu_loop_exit_sigsegv(CPUState *cpu, target_ulong addr,
                           MMUAccessType access_type, bool maperr, uintptr_t ra)
{
    const struct TCGCPUOps *tcg_ops = CPU_GET_CLASS(cpu)->tcg_ops;

    if (tcg_ops->record_sigsegv) {
        tcg_ops->record_sigsegv(cpu, addr, access_type, maperr, ra);
    }

    force_sig_fault(TARGET_SIGSEGV,
                    maperr ? TARGET_SEGV_MAPERR : TARGET_SEGV_ACCERR,
                    addr);
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}

void cpu_loop_exit_sigbus(CPUState *cpu, target_ulong addr,
                          MMUAccessType access_type, uintptr_t ra)
{
    const struct TCGCPUOps *tcg_ops = CPU_GET_CLASS(cpu)->tcg_ops;

    if (tcg_ops->record_sigbus) {
        tcg_ops->record_sigbus(cpu, addr, access_type, ra);
    }

    force_sig_fault(TARGET_SIGBUS, TARGET_BUS_ADRALN, addr);
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}
