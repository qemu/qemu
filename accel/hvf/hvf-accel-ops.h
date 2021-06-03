/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HVF_CPUS_H
#define HVF_CPUS_H

#include "sysemu/cpus.h"

int hvf_init_vcpu(CPUState *);
int hvf_vcpu_exec(CPUState *);
void hvf_cpu_synchronize_state(CPUState *);
void hvf_cpu_synchronize_post_reset(CPUState *);
void hvf_cpu_synchronize_post_init(CPUState *);
void hvf_cpu_synchronize_pre_loadvm(CPUState *);
void hvf_vcpu_destroy(CPUState *);

#endif /* HVF_CPUS_H */
