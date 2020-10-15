/*
 * QEMU TCG Single Threaded vCPUs implementation
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_CPUS_RR_H
#define TCG_CPUS_RR_H

#define TCG_KICK_PERIOD (NANOSECONDS_PER_SECOND / 10)

/* Kick all RR vCPUs. */
void qemu_cpu_kick_rr_cpus(CPUState *unused);

void *tcg_rr_cpu_thread_fn(void *arg);

#endif /* TCG_CPUS_RR_H */
