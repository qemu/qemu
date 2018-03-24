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

#include "../rdma_utils.h"
#include "standard-headers/drivers/infiniband/hw/vmw_pvrdma/pvrdma_ring.h"
#include "pvrdma_dev_ring.h"

int pvrdma_ring_init(PvrdmaRing *ring, const char *name, PCIDevice *dev,
                     struct pvrdma_ring *ring_state, uint32_t max_elems,
                     size_t elem_sz, dma_addr_t *tbl, uint32_t npages)
{
    int i;
    int rc = 0;

    strncpy(ring->name, name, MAX_RING_NAME_SZ);
    ring->name[MAX_RING_NAME_SZ - 1] = 0;
    pr_dbg("Initializing %s ring\n", ring->name);
    ring->dev = dev;
    ring->ring_state = ring_state;
    ring->max_elems = max_elems;
    ring->elem_sz = elem_sz;
    pr_dbg("ring->elem_sz=%zu\n", ring->elem_sz);
    pr_dbg("npages=%d\n", npages);
    /* TODO: Give a moment to think if we want to redo driver settings
    atomic_set(&ring->ring_state->prod_tail, 0);
    atomic_set(&ring->ring_state->cons_head, 0);
    */
    ring->npages = npages;
    ring->pages = g_malloc(npages * sizeof(void *));

    for (i = 0; i < npages; i++) {
        if (!tbl[i]) {
            pr_err("npages=%ld but tbl[%d] is NULL\n", (long)npages, i);
            continue;
        }

        ring->pages[i] = rdma_pci_dma_map(dev, tbl[i], TARGET_PAGE_SIZE);
        if (!ring->pages[i]) {
            rc = -ENOMEM;
            pr_dbg("Failed to map to page %d\n", i);
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
    unsigned int idx = 0, offset;

    /*
    pr_dbg("%s: t=%d, h=%d\n", ring->name, ring->ring_state->prod_tail,
           ring->ring_state->cons_head);
    */

    if (!pvrdma_idx_ring_has_data(ring->ring_state, ring->max_elems, &idx)) {
        pr_dbg("No more data in ring\n");
        return NULL;
    }

    offset = idx * ring->elem_sz;
    /*
    pr_dbg("idx=%d\n", idx);
    pr_dbg("offset=%d\n", offset);
    */
    return ring->pages[offset / TARGET_PAGE_SIZE] + (offset % TARGET_PAGE_SIZE);
}

void pvrdma_ring_read_inc(PvrdmaRing *ring)
{
    pvrdma_idx_ring_inc(&ring->ring_state->cons_head, ring->max_elems);
    /*
    pr_dbg("%s: t=%d, h=%d, m=%ld\n", ring->name,
           ring->ring_state->prod_tail, ring->ring_state->cons_head,
           ring->max_elems);
    */
}

void *pvrdma_ring_next_elem_write(PvrdmaRing *ring)
{
    unsigned int idx, offset, tail;

    /*
    pr_dbg("%s: t=%d, h=%d\n", ring->name, ring->ring_state->prod_tail,
           ring->ring_state->cons_head);
    */

    if (!pvrdma_idx_ring_has_space(ring->ring_state, ring->max_elems, &tail)) {
        pr_dbg("CQ is full\n");
        return NULL;
    }

    idx = pvrdma_idx(&ring->ring_state->prod_tail, ring->max_elems);
    /* TODO: tail == idx */

    offset = idx * ring->elem_sz;
    return ring->pages[offset / TARGET_PAGE_SIZE] + (offset % TARGET_PAGE_SIZE);
}

void pvrdma_ring_write_inc(PvrdmaRing *ring)
{
    pvrdma_idx_ring_inc(&ring->ring_state->prod_tail, ring->max_elems);
    /*
    pr_dbg("%s: t=%d, h=%d, m=%ld\n", ring->name,
           ring->ring_state->prod_tail, ring->ring_state->cons_head,
           ring->max_elems);
    */
}

void pvrdma_ring_free(PvrdmaRing *ring)
{
    if (!ring) {
        return;
    }

    if (!ring->pages) {
        return;
    }

    pr_dbg("ring->npages=%d\n", ring->npages);
    while (ring->npages--) {
        rdma_pci_dma_unmap(ring->dev, ring->pages[ring->npages],
                           TARGET_PAGE_SIZE);
    }

    g_free(ring->pages);
    ring->pages = NULL;
}
