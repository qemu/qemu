/*
 * Internal memory managment interfaces
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef EXEC_MEMORY_H
#define EXEC_MEMORY_H

/*
 * Internal interfaces between memory.c/exec.c/vl.c.  Do not #include unless
 * you're one of them.
 */

#include "memory.h"

#ifndef CONFIG_USER_ONLY

/* Get the root memory region.  This interface should only be used temporarily
 * until a proper bus interface is available.
 */
MemoryRegion *get_system_memory(void);

/* Set the root memory region.  This region is the system memory map. */
void set_system_memory_map(MemoryRegion *mr);

/* Set the I/O memory region.  This region is the I/O memory map. */
void set_system_io_map(MemoryRegion *mr);

#endif

#endif
