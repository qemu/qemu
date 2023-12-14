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
#include "hw/pci/pci.h"
#include "cpu.h"
#include "qemu/cutils.h"

#include "trace.h"

#include "../rdma_utils.h"
#include "pvrdma_dev_ring.h"

int pvrdma_ring_init(PvrdmaRing *ring, const char *name, PCIDevice *dev,
                     PvrdmaRingState *ring_state, uint32_t max_elems,
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
    qatomic_set(&ring->ring_state->prod_tail, 0);
    qatomic_set(&ring->ring_state->cons_head, 0);
    */
    ring->npages = npages;
    ring->pages = g_new0(void *, npages);

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
    unsigned int idx, offset;
    const uint32_t tail = qatomic_read(&ring->ring_state->prod_tail);
    const uint32_t head = qatomic_read(&ring->ring_state->cons_head);

    if (tail & ~((ring->max_elems << 1) - 1) ||
        head & ~((ring->max_elems << 1) - 1) ||
        tail == head) {
        trace_pvrdma_ring_next_elem_read_no_data(ring->name);
        return NULL;
    }

    idx = head & (ring->max_elems - 1);
    offset = idx * ring->elem_sz;
    return ring->pages[offset / TARGET_PAGE_SIZE] + (offset % TARGET_PAGE_SIZE);
}

void pvrdma_ring_read_inc(PvrdmaRing *ring)
{
    uint32_t idx = qatomic_read(&ring->ring_state->cons_head);

    idx = (idx + 1) & ((ring->max_elems << 1) - 1);
    qatomic_set(&ring->ring_state->cons_head, idx);
}

void *pvrdma_ring_next_elem_write(PvrdmaRing *ring)
{
    unsigned int idx, offset;
    const uint32_t tail = qatomic_read(&ring->ring_state->prod_tail);
    const uint32_t head = qatomic_read(&ring->ring_state->cons_head);

    if (tail & ~((ring->max_elems << 1) - 1) ||
        head & ~((ring->max_elems << 1) - 1) ||
        tail == (head ^ ring->max_elems)) {
        rdma_error_report("CQ is full");
        return NULL;
    }

    idx = tail & (ring->max_elems - 1);
    offset = idx * ring->elem_sz;
    return ring->pages[offset / TARGET_PAGE_SIZE] + (offset % TARGET_PAGE_SIZE);
}

void pvrdma_ring_write_inc(PvrdmaRing *ring)
{
    uint32_t idx = qatomic_read(&ring->ring_state->prod_tail);

    idx = (idx + 1) & ((ring->max_elems << 1) - 1);
    qatomic_set(&ring->ring_state->prod_tail, idx);
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
