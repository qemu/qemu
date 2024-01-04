/*
 * PSCI API
 * From arch/arm[64]/kernel/psci.c
 *
 * Copyright (C) 2015, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <asm/psci.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/mmu-api.h>

#define T PSCI_INVOKE_ARG_TYPE
__attribute__((noinline))
int psci_invoke(T function_id, T arg0, T arg1, T arg2)
{
	asm volatile(
		"hvc #0"
	: "+r" (function_id)
	: "r" (arg0), "r" (arg1), "r" (arg2));
	return function_id;
}

int psci_cpu_on(unsigned long cpuid, unsigned long entry_point)
{
	return psci_invoke(PSCI_FN_CPU_ON, cpuid, entry_point, 0);
}

extern void secondary_entry(void);
int cpu_psci_cpu_boot(unsigned int cpu)
{
	mmu_mark_disabled(cpu);
	int err = psci_cpu_on(cpus[cpu], __pa(secondary_entry));
	if (err)
		printf("failed to boot CPU%d (%d)\n", cpu, err);
	return err;
}

#define PSCI_POWER_STATE_TYPE_POWER_DOWN (1U << 16)
void cpu_psci_cpu_die(unsigned int cpu)
{
	int err = psci_invoke(PSCI_0_2_FN_CPU_OFF,
			PSCI_POWER_STATE_TYPE_POWER_DOWN, 0, 0);
	printf("unable to power off CPU%d (%d)\n", cpu, err);
}

void psci_sys_reset(void)
{
	psci_invoke(PSCI_0_2_FN_SYSTEM_RESET, 0, 0, 0);
}
