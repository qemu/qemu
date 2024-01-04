/*
 * Secondary cpu support
 *
 * Copyright (C) 2015, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <alloc.h>
#include <asm/thread_info.h>
#include <asm/cpumask.h>
#include <asm/mmu.h>
#include <asm/psci.h>
#include <asm/smp.h>

cpumask_t cpu_present_mask;
cpumask_t cpu_online_mask;
struct secondary_data secondary_data;

secondary_entry_fn secondary_cinit(void)
{
	struct thread_info *ti = current_thread_info();
	secondary_entry_fn entry;

	thread_info_init(ti, 0);

	/*
	 * Save secondary_data.entry locally to avoid opening a race
	 * window between marking ourselves online and calling it.
	 */
	entry = secondary_data.entry;
	set_cpu_online(ti->cpu, true);
	sev();

	/*
	 * Return to the assembly stub, allowing entry to be called
	 * from there with an empty stack.
	 */
	return entry;
}

void smp_boot_secondary(int cpu, secondary_entry_fn entry)
{
	void *stack_base = memalign(THREAD_SIZE, THREAD_SIZE);

	secondary_data.stack = stack_base + THREAD_START_SP;
	secondary_data.entry = entry;
	assert(cpu_psci_cpu_boot(cpu) == 0);

	while (!cpu_online(cpu))
		wfe();
}
