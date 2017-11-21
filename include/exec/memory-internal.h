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

/*
 * This header is for use by exec.c, memory.c and accel/tcg/cputlb.c ONLY,
 * for declarations which are shared between the memory subsystem's
 * internals and the TCG TLB code. Do not include it from elsewhere.
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

/* Opaque struct for passing info from memory_notdirty_write_prepare()
 * to memory_notdirty_write_complete(). Callers should treat all fields
 * as private, with the exception of @active.
 *
 * @active is a field which is not touched by either the prepare or
 * complete functions, but which the caller can use if it wishes to
 * track whether it has called prepare for this struct and so needs
 * to later call the complete function.
 */
typedef struct {
    CPUState *cpu;
    ram_addr_t ram_addr;
    vaddr mem_vaddr;
    unsigned size;
    bool locked;
    bool active;
} NotDirtyInfo;

/**
 * memory_notdirty_write_prepare: call before writing to non-dirty memory
 * @ndi: pointer to opaque NotDirtyInfo struct
 * @cpu: CPU doing the write
 * @mem_vaddr: virtual address of write
 * @ram_addr: the ram address of the write
 * @size: size of write in bytes
 *
 * Any code which writes to the host memory corresponding to
 * guest RAM which has been marked as NOTDIRTY must wrap those
 * writes in calls to memory_notdirty_write_prepare() and
 * memory_notdirty_write_complete():
 *
 *  NotDirtyInfo ndi;
 *  memory_notdirty_write_prepare(&ndi, ....);
 *  ... perform write here ...
 *  memory_notdirty_write_complete(&ndi);
 *
 * These calls will ensure that we flush any TCG translated code for
 * the memory being written, update the dirty bits and (if possible)
 * remove the slowpath callback for writing to the memory.
 *
 * This must only be called if we are using TCG; it will assert otherwise.
 *
 * We may take a lock in the prepare call, so callers must ensure that
 * they don't exit (via longjump or otherwise) without calling complete.
 *
 * This call must only be made inside an RCU critical section.
 * (Note that while we're executing a TCG TB we're always in an
 * RCU critical section, which is likely to be the case for callers
 * of these functions.)
 */
void memory_notdirty_write_prepare(NotDirtyInfo *ndi,
                                   CPUState *cpu,
                                   vaddr mem_vaddr,
                                   ram_addr_t ram_addr,
                                   unsigned size);
/**
 * memory_notdirty_write_complete: finish write to non-dirty memory
 * @ndi: pointer to the opaque NotDirtyInfo struct which was initialized
 * by memory_not_dirty_write_prepare().
 */
void memory_notdirty_write_complete(NotDirtyInfo *ndi);

#endif
#endif
