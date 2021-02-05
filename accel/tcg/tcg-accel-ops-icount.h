/*
 * QEMU TCG Single Threaded vCPUs implementation using instruction counting
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_CPUS_ICOUNT_H
#define TCG_CPUS_ICOUNT_H

void icount_handle_deadline(void);
void icount_prepare_for_run(CPUState *cpu);
void icount_process_data(CPUState *cpu);

void icount_handle_interrupt(CPUState *cpu, int mask);

#endif /* TCG_CPUS_ICOUNT_H */
