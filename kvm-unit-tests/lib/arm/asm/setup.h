#ifndef _ASMARM_SETUP_H_
#define _ASMARM_SETUP_H_
/*
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <asm/page.h>
#include <asm/pgtable-hwdef.h>

#define NR_CPUS			8
extern u32 cpus[NR_CPUS];
extern int nr_cpus;

extern phys_addr_t __phys_offset, __phys_end;

#define PHYS_OFFSET		(__phys_offset)
#define PHYS_END		(__phys_end)
/* mach-virt reserves the first 1G section for I/O */
#define PHYS_IO_OFFSET		(0UL)
#define PHYS_IO_END		(1UL << 30)

#define L1_CACHE_SHIFT		6
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)
#define SMP_CACHE_BYTES		L1_CACHE_BYTES

#endif /* _ASMARM_SETUP_H_ */
