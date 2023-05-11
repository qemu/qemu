/*
 * QEMU TCG Single Threaded vCPUs implementation using instruction counting
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_ACCEL_OPS_ICOUNT_H
#define TCG_ACCEL_OPS_ICOUNT_H

void icount_handle_deadline(void);
void icount_prepare_for_run(CPUState *cpu, int64_t cpu_budget);
int64_t icount_percpu_budget(int cpu_count);
void icount_process_data(CPUState *cpu);

void icount_handle_interrupt(CPUState *cpu, int mask);

#endif /* TCG_ACCEL_OPS_ICOUNT_H */
