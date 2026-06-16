/*
 * QEMU TCG CPU loop API
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ACCEL_TCG_CPU_LOOP_COMMON_H
#define ACCEL_TCG_CPU_LOOP_COMMON_H

#ifndef CONFIG_TCG
#error Can only include this header with TCG
#endif

/**
 * cpu_exec:
 * @cpu: the cpu context
 *
 * Returns one of the EXCP_* definitions (see "exec/cpu-common.h").
 */
int cpu_exec(CPUState *cpu);

void cpu_exec_step_atomic(CPUState *cpu);

/**
 * cpu_unwind_state_data:
 * @cpu: the cpu context
 * @host_pc: the host pc within the translation
 * @data: output data
 *
 * Attempt to load the unwind state for a host pc occurring in
 * translated code.  If @host_pc is not in translated code, the
 * function returns false; otherwise @data is loaded.
 * This is the same unwind info as given to restore_state_to_opc.
 */
bool cpu_unwind_state_data(CPUState *cpu, uintptr_t host_pc, uint64_t *data);

#endif
