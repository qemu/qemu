/*
 * Declarations for functions which are internal to the memory subsystem.
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

#ifndef MEMORY_INTERNAL_H
#define MEMORY_INTERNAL_H

#ifndef CONFIG_USER_ONLY
static inline AddressSpaceDispatch *flatview_to_dispatch(FlatView *fv)
{
    return fv->dispatch;
}

static inline AddressSpaceDispatch *address_space_to_dispatch(AddressSpace *as)
{
    return flatview_to_dispatch(address_space_to_flatview(as));
}

FlatView *address_space_get_flatview(AddressSpace *as);
void flatview_unref(FlatView *view);

extern const MemoryRegionOps unassigned_mem_ops;

void flatview_add_to_dispatch(FlatView *fv, MemoryRegionSection *section);
AddressSpaceDispatch *address_space_dispatch_new(FlatView *fv);
void address_space_dispatch_compact(AddressSpaceDispatch *d);
void address_space_dispatch_free(AddressSpaceDispatch *d);

void mtree_print_dispatch(struct AddressSpaceDispatch *d,
                          MemoryRegion *root);

/* returns true if end is big endian. */
static inline bool devend_big_endian(enum device_endian end)
{
    if (end == DEVICE_NATIVE_ENDIAN) {
        return target_big_endian();
    }
    return end == DEVICE_BIG_ENDIAN;
}

/* enum device_endian to MemOp.  */
static inline MemOp devend_memop(enum device_endian end)
{
    return devend_big_endian(end) ? MO_BE : MO_LE;
}

#endif
#endif
