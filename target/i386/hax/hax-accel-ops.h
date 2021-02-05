/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HAX_CPUS_H
#define HAX_CPUS_H

#include "sysemu/cpus.h"

#include "hax-interface.h"
#include "hax-i386.h"

int hax_init_vcpu(CPUState *cpu);
int hax_smp_cpu_exec(CPUState *cpu);
int hax_populate_ram(uint64_t va, uint64_t size);

void hax_cpu_synchronize_state(CPUState *cpu);
void hax_cpu_synchronize_post_reset(CPUState *cpu);
void hax_cpu_synchronize_post_init(CPUState *cpu);
void hax_cpu_synchronize_pre_loadvm(CPUState *cpu);

int hax_vcpu_destroy(CPUState *cpu);
void hax_raise_event(CPUState *cpu);
void hax_reset_vcpu_state(void *opaque);

#endif /* HAX_CPUS_H */
