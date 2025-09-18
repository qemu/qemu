/*
 * QEMU TCG vCPU common functionality
 *
 * Functionality common to all TCG vcpu variants: mttcg, rr and icount.
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_ACCEL_OPS_H
#define TCG_ACCEL_OPS_H

#include "system/cpus.h"

void tcg_cpu_destroy(CPUState *cpu);
int tcg_cpu_exec(CPUState *cpu);
void tcg_handle_interrupt(CPUState *cpu, int mask);
void tcg_cpu_init_cflags(CPUState *cpu, bool parallel);
void tcg_kick_vcpu_thread(CPUState *cpu);

#endif /* TCG_ACCEL_OPS_H */
