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
#include "exec/cpu-all.h"
#include "sysemu/memory_mapping.h"

int qemu_get_guest_memory_mapping(MemoryMappingList *list)
{
    return -2;
}
