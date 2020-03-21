/*
 * QEMU paravirtual RDMA - Device rings
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

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "hw/pci/pci.h"
#include "cpu.h"
#include "qemu/cutils.h"

#include "trace.h"

#include "../rdma_utils.h"
#include "standard-headers/drivers/infiniband/hw/vmw_pvrdma/pvrdma_ring.h"
#include "pvrdma_dev_ring.h"

int pvrdma_ring_init(PvrdmaRing *ring, const char *name, PCIDevice *dev,
                     struct pvrdma_ring *ring_state, uint32_t max_elems,
                     size_t elem_sz, dma_addr_t *tbl, uint32_t npages)
{
    int i;
    int rc = 0;

    pstrcpy(ring->name, MAX_RING_NAME_SZ, name);
    ring->dev = dev;
    ring->ring_state = ring_state;
    ring->max_elems = max_elems;
    ring->elem_sz = elem_sz;
    /* TODO: Give a moment to think if we want to redo driver settings
    atomic_set(&ring->ring_state->prod_tail, 0);
    atomic_set(&ring->ring_state->cons_head, 0);
    */
    ring->npages = npages;
    ring->pages = g_malloc(npages * sizeof(void *));

    for (i = 0; i < npages; i++) {
        if (!tbl[i]) {
            rdma_error_report("npages=%d but tbl[%d] is NULL", npages, i);
            continue;
        }

        ring->pages[i] = rdma_pci_dma_map(dev, tbl[i], TARGET_PAGE_SIZE);
        if (!ring->pages[i]) {
            rc = -ENOMEM;
            rdma_error_report("Failed to map to page %d in ring %s", i, name);
            goto out_free;
        }
        memset(ring->pages[i], 0, TARGET_PAGE_SIZE);
    }

    goto out;

out_free:
    while (i--) {
        rdma_pci_dma_unmap(dev, ring->pages[i], TARGET_PAGE_SIZE);
    }
    g_free(ring->pages);

out:
    return rc;
}

void *pvrdma_ring_next_elem_read(PvrdmaRing *ring)
{
    int e;
    unsigned int idx = 0, offset;

    e = pvrdma_idx_ring_has_data(ring->ring_state, ring->max_elems, &idx);
    if (e <= 0) {
        trace_pvrdma_ring_next_elem_read_no_data(ring->name);
        return NULL;
    }

    offset = idx * ring->elem_sz;
    return ring->pages[offset / TARGET_PAGE_SIZE] + (offset % TARGET_PAGE_SIZE);
}

void pvrdma_ring_read_inc(PvrdmaRing *ring)
{
    pvrdma_idx_ring_inc(&ring->ring_state->cons_head, ring->max_elems);
}

void *pvrdma_ring_next_elem_write(PvrdmaRing *ring)
{
    int idx;
    unsigned int offset, tail;

    idx = pvrdma_idx_ring_has_space(ring->ring_state, ring->max_elems, &tail);
    if (idx <= 0) {
        rdma_error_report("CQ is full");
        return NULL;
    }

    idx = pvrdma_idx(&ring->ring_state->prod_tail, ring->max_elems);
    if (idx < 0 || tail != idx) {
        rdma_error_report("Invalid idx %d", idx);
        return NULL;
    }

    offset = idx * ring->elem_sz;
    return ring->pages[offset / TARGET_PAGE_SIZE] + (offset % TARGET_PAGE_SIZE);
}

void pvrdma_ring_write_inc(PvrdmaRing *ring)
{
    pvrdma_idx_ring_inc(&ring->ring_state->prod_tail, ring->max_elems);
}

void pvrdma_ring_free(PvrdmaRing *ring)
{
    if (!ring) {
        return;
    }

    if (!ring->pages) {
        return;
    }

    while (ring->npages--) {
        rdma_pci_dma_unmap(ring->dev, ring->pages[ring->npages],
                           TARGET_PAGE_SIZE);
    }

    g_free(ring->pages);
    ring->pages = NULL;
}
