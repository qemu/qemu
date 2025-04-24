/*
 * Internal memory management interfaces
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

#ifndef SYSTEM_ADDRESS_SPACES_H
#define SYSTEM_ADDRESS_SPACES_H

/*
 * Internal interfaces between memory.c/exec.c/vl.c.  Do not #include unless
 * you're one of them.
 */

/* Get the root memory region.  This interface should only be used temporarily
 * until a proper bus interface is available.
 */
MemoryRegion *get_system_memory(void);

/* Get the root I/O port region.  This interface should only be used
 * temporarily until a proper bus interface is available.
 */
MemoryRegion *get_system_io(void);

extern AddressSpace address_space_memory;
extern AddressSpace address_space_io;

#endif
