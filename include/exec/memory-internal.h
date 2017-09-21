/*
 * Declarations for obsolete exec.c functions
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

/*
 * This header is for use by exec.c and memory.c ONLY.  Do not include it.
 * The functions declared here will be removed soon.
 */

#ifndef MEMORY_INTERNAL_H
#define MEMORY_INTERNAL_H

#ifndef CONFIG_USER_ONLY
typedef struct AddressSpaceDispatch AddressSpaceDispatch;

extern const MemoryRegionOps unassigned_mem_ops;

bool memory_region_access_valid(MemoryRegion *mr, hwaddr addr,
                                unsigned size, bool is_write);

void flatview_add_to_dispatch(FlatView *fv, MemoryRegionSection *section);
AddressSpaceDispatch *address_space_dispatch_new(FlatView *fv);
void address_space_dispatch_compact(AddressSpaceDispatch *d);

AddressSpaceDispatch *address_space_to_dispatch(AddressSpace *as);
AddressSpaceDispatch *flatview_to_dispatch(FlatView *fv);
void address_space_dispatch_free(AddressSpaceDispatch *d);

void mtree_print_dispatch(fprintf_function mon, void *f,
                          struct AddressSpaceDispatch *d,
                          MemoryRegion *root);

#endif
#endif
