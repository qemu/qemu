/*
 * QEMU memory mapping
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "cpu.h"
#include "cpu-all.h"
#include "memory_mapping.h"

int qemu_get_guest_memory_mapping(MemoryMappingList *list)
{
    return -2;
}

int cpu_get_memory_mapping(MemoryMappingList *list,
					                                          CPUArchState *env)
{
    return -1;
}

bool cpu_paging_enabled(CPUArchState *env)
{
    return true;
}

