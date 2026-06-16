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

#endif
