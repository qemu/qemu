/*
 * Initialize machine setup information and I/O.
 *
 * After running setup() unit tests may query how many cpus they have
 * (nr_cpus), how much memory they have (PHYS_END - PHYS_OFFSET), may
 * use dynamic memory allocation (malloc, etc.), printf, and exit.
 * Finally, argc and argv are also ready to be passed to main().
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <libfdt/libfdt.h>
#include <devicetree.h>
#include <alloc.h>
#include <asm/thread_info.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/smp.h>

extern unsigned long stacktop;
extern void io_init(void);
extern void setup_args(const char *args);

u32 cpus[NR_CPUS] = { [0 ... NR_CPUS-1] = (~0U) };
int nr_cpus;

phys_addr_t __phys_offset, __phys_end;

static void cpu_set(int fdtnode __unused, u32 regval, void *info __unused)
{
	int cpu = nr_cpus++;
	assert(cpu < NR_CPUS);
	cpus[cpu] = regval;
	set_cpu_present(cpu, true);
}

static void cpu_init(void)
{
	nr_cpus = 0;
	assert(dt_for_each_cpu_node(cpu_set, NULL) == 0);
	set_cpu_online(0, true);
}

static void mem_init(phys_addr_t freemem_start)
{
	/* we only expect one membank to be defined in the DT */
	struct dt_pbus_reg regs[1];
	phys_addr_t mem_start, mem_end;

	assert(dt_get_memory_params(regs, 1));

	mem_start = regs[0].addr;
	mem_end = mem_start + regs[0].size;

	assert(!(mem_start & ~PHYS_MASK) && !((mem_end-1) & ~PHYS_MASK));
	assert(freemem_start >= mem_start && freemem_start < mem_end);

	__phys_offset = mem_start;	/* PHYS_OFFSET */
	__phys_end = mem_end;		/* PHYS_END */

	phys_alloc_init(freemem_start, mem_end - freemem_start);
	phys_alloc_set_minimum_alignment(SMP_CACHE_BYTES);

	mmu_enable_idmap();
}

void setup(const void *fdt)
{
	const char *bootargs;
	u32 fdt_size;

	/*
	 * Move the fdt to just above the stack. The free memory
	 * then starts just after the fdt.
	 */
	fdt_size = fdt_totalsize(fdt);
	assert(fdt_move(fdt, &stacktop, fdt_size) == 0);
	assert(dt_init(&stacktop) == 0);

	mem_init(PAGE_ALIGN((unsigned long)&stacktop + fdt_size));
	io_init();
	cpu_init();

	thread_info_init(current_thread_info(), 0);

	assert(dt_get_bootargs(&bootargs) == 0);
	setup_args(bootargs);
}
