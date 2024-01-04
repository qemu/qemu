#ifndef _ASMARM_SMP_H_
#define _ASMARM_SMP_H_
/*
 * Copyright (C) 2015, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <asm/thread_info.h>
#include <asm/cpumask.h>

#define smp_processor_id()		(current_thread_info()->cpu)

extern void halt(void);

extern cpumask_t cpu_present_mask;
extern cpumask_t cpu_online_mask;
#define cpu_present(cpu)		cpumask_test_cpu(cpu, &cpu_present_mask)
#define cpu_online(cpu)			cpumask_test_cpu(cpu, &cpu_online_mask)
#define for_each_present_cpu(cpu)	for_each_cpu(cpu, &cpu_present_mask)
#define for_each_online_cpu(cpu)	for_each_cpu(cpu, &cpu_online_mask)

static inline void set_cpu_present(int cpu, bool present)
{
	if (present)
		cpumask_set_cpu(cpu, &cpu_present_mask);
	else
		cpumask_clear_cpu(cpu, &cpu_present_mask);
}

static inline void set_cpu_online(int cpu, bool online)
{
	if (online)
		cpumask_set_cpu(cpu, &cpu_online_mask);
	else
		cpumask_clear_cpu(cpu, &cpu_online_mask);
}

typedef void (*secondary_entry_fn)(void);

/* secondary_data is reused for each cpu, so only boot one at a time */
struct secondary_data {
	void *stack;		/* must be first member of struct */
	secondary_entry_fn entry;
};
extern struct secondary_data secondary_data;

extern void smp_boot_secondary(int cpu, secondary_entry_fn entry);

#endif /* _ASMARM_SMP_H_ */
