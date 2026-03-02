/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSTEM_WHPX_ACCEL_OPS_H
#define SYSTEM_WHPX_ACCEL_OPS_H

#include "system/cpus.h"

int whpx_init_vcpu(CPUState *cpu);
int whpx_vcpu_exec(CPUState *cpu);
void whpx_destroy_vcpu(CPUState *cpu);
void whpx_vcpu_kick(CPUState *cpu);

void whpx_cpu_synchronize_state(CPUState *cpu);
void whpx_cpu_synchronize_post_reset(CPUState *cpu);
void whpx_cpu_synchronize_post_init(CPUState *cpu);
void whpx_cpu_synchronize_pre_loadvm(CPUState *cpu);

typedef enum WHPXStateLevel {
    /* subset of runtime state for faster returns from vmexit */
    WHPX_LEVEL_FAST_RUNTIME_STATE,
    /* state subset only touched by the VCPU itself during runtime */
    WHPX_LEVEL_RUNTIME_STATE,
    /* state subset modified during VCPU reset */
    WHPX_LEVEL_RESET_STATE,
    /* full state set, modified during initialization or on vmload */
    WHPX_LEVEL_FULL_STATE
} WHPXStateLevel;

#endif /* TARGET_I386_WHPX_ACCEL_OPS_H */
