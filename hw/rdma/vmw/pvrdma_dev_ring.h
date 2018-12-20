/*
 * QEMU VMWARE paravirtual RDMA ring utilities
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef PVRDMA_DEV_RING_H
#define PVRDMA_DEV_RING_H


#define MAX_RING_NAME_SZ 32

typedef struct PvrdmaRing {
    char name[MAX_RING_NAME_SZ];
    PCIDevice *dev;
    uint32_t max_elems;
    size_t elem_sz;
    struct pvrdma_ring *ring_state; /* used only for unmap */
    int npages;
    void **pages;
} PvrdmaRing;

int pvrdma_ring_init(PvrdmaRing *ring, const char *name, PCIDevice *dev,
                     struct pvrdma_ring *ring_state, uint32_t max_elems,
                     size_t elem_sz, dma_addr_t *tbl, uint32_t npages);
void *pvrdma_ring_next_elem_read(PvrdmaRing *ring);
void pvrdma_ring_read_inc(PvrdmaRing *ring);
void *pvrdma_ring_next_elem_write(PvrdmaRing *ring);
void pvrdma_ring_write_inc(PvrdmaRing *ring);
void pvrdma_ring_free(PvrdmaRing *ring);

#endif
