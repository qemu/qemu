/*
 * QEMU TCG Multi Threaded vCPUs implementation
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_ACCEL_OPS_MTTCG_H
#define TCG_ACCEL_OPS_MTTCG_H

/* start an mttcg vCPU thread */
void mttcg_start_vcpu_thread(CPUState *cpu);

#endif /* TCG_ACCEL_OPS_MTTCG_H */
