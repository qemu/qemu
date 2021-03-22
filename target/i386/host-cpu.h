/*
 * x86 host CPU type initialization and host CPU functions
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HOST_CPU_H
#define HOST_CPU_H

void host_cpu_instance_init(X86CPU *cpu);
void host_cpu_max_instance_init(X86CPU *cpu);
bool host_cpu_realizefn(CPUState *cs, Error **errp);

void host_cpu_vendor_fms(char *vendor, int *family, int *model, int *stepping);

#endif /* HOST_CPU_H */
