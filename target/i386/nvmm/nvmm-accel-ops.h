/*
 * Copyright (c) 2018-2019 Maxime Villard, All rights reserved.
 *
 * NetBSD Virtual Machine Monitor (NVMM) accelerator for QEMU.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef NVMM_CPUS_H
#define NVMM_CPUS_H

#include "sysemu/cpus.h"

int nvmm_init_vcpu(CPUState *cpu);
int nvmm_vcpu_exec(CPUState *cpu);
void nvmm_destroy_vcpu(CPUState *cpu);

void nvmm_cpu_synchronize_state(CPUState *cpu);
void nvmm_cpu_synchronize_post_reset(CPUState *cpu);
void nvmm_cpu_synchronize_post_init(CPUState *cpu);
void nvmm_cpu_synchronize_pre_loadvm(CPUState *cpu);

#endif /* NVMM_CPUS_H */
