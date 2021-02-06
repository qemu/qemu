/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef WHPX_CPUS_H
#define WHPX_CPUS_H

#include "sysemu/cpus.h"

int whpx_init_vcpu(CPUState *cpu);
int whpx_vcpu_exec(CPUState *cpu);
void whpx_destroy_vcpu(CPUState *cpu);
void whpx_vcpu_kick(CPUState *cpu);

void whpx_cpu_synchronize_state(CPUState *cpu);
void whpx_cpu_synchronize_post_reset(CPUState *cpu);
void whpx_cpu_synchronize_post_init(CPUState *cpu);
void whpx_cpu_synchronize_pre_loadvm(CPUState *cpu);

/* state subset only touched by the VCPU itself during runtime */
#define WHPX_SET_RUNTIME_STATE   1
/* state subset modified during VCPU reset */
#define WHPX_SET_RESET_STATE     2
/* full state set, modified during initialization or on vmload */
#define WHPX_SET_FULL_STATE      3

#endif /* WHPX_CPUS_H */
