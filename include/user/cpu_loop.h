/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
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

#ifndef USER_CPU_LOOP_H
#define USER_CPU_LOOP_H

#include "exec/abi_ptr.h"
#include "exec/mmu-access-type.h"
#include "exec/log.h"
#include "exec/target_long.h"
#include "special-errno.h"

/**
 * adjust_signal_pc:
 * @pc: raw pc from the host signal ucontext_t.
 * @is_write: host memory operation was write, or read-modify-write.
 *
 * Alter @pc as required for unwinding.  Return the type of the
 * guest memory access -- host reads may be for guest execution.
 */
MMUAccessType adjust_signal_pc(uintptr_t *pc, bool is_write);

/**
 * handle_sigsegv_accerr_write:
 * @cpu: the cpu context
 * @old_set: the sigset_t from the signal ucontext_t
 * @host_pc: the host pc, adjusted for the signal
 * @host_addr: the host address of the fault
 *
 * Return true if the write fault has been handled, and should be re-tried.
 */
bool handle_sigsegv_accerr_write(CPUState *cpu, sigset_t *old_set,
                                 uintptr_t host_pc, abi_ptr guest_addr);

/**
 * cpu_loop_exit_sigsegv:
 * @cpu: the cpu context
 * @addr: the guest address of the fault
 * @access_type: access was read/write/execute
 * @maperr: true for invalid page, false for permission fault
 * @ra: host pc for unwinding
 *
 * Use the TCGCPUOps hook to record cpu state, do guest operating system
 * specific things to raise SIGSEGV, and jump to the main cpu loop.
 */
G_NORETURN void cpu_loop_exit_sigsegv(CPUState *cpu, target_ulong addr,
                                      MMUAccessType access_type,
                                      bool maperr, uintptr_t ra);

/**
 * cpu_loop_exit_sigbus:
 * @cpu: the cpu context
 * @addr: the guest address of the alignment fault
 * @access_type: access was read/write/execute
 * @ra: host pc for unwinding
 *
 * Use the TCGCPUOps hook to record cpu state, do guest operating system
 * specific things to raise SIGBUS, and jump to the main cpu loop.
 */
G_NORETURN void cpu_loop_exit_sigbus(CPUState *cpu, target_ulong addr,
                                     MMUAccessType access_type,
                                     uintptr_t ra);

G_NORETURN void cpu_loop(CPUArchState *env);

void target_exception_dump(CPUArchState *env, const char *fmt, int code);
#define EXCP_DUMP(env, fmt, code) \
    target_exception_dump(env, fmt, code)

typedef struct target_pt_regs target_pt_regs;

void target_cpu_copy_regs(CPUArchState *env, target_pt_regs *regs);

#endif
