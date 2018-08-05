/*
 * QEMU paravirtual RDMA - Generic RDMA backend
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

#include "rdma_utils.h"

#ifdef PVRDMA_DEBUG
unsigned long pr_dbg_cnt;
#endif

void *rdma_pci_dma_map(PCIDevice *dev, dma_addr_t addr, dma_addr_t plen)
{
    void *p;
    hwaddr len = plen;

    if (!addr) {
        pr_dbg("addr is NULL\n");
        return NULL;
    }

    p = pci_dma_map(dev, addr, &len, DMA_DIRECTION_TO_DEVICE);
    if (!p) {
        pr_dbg("Fail in pci_dma_map, addr=0x%" PRIx64 ", len=%" PRId64 "\n",
               addr, len);
        return NULL;
    }

    if (len != plen) {
        rdma_pci_dma_unmap(dev, p, len);
        return NULL;
    }

    pr_dbg("0x%" PRIx64 " -> %p (len=% " PRId64 ")\n", addr, p, len);

    return p;
}

void rdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len)
{
    pr_dbg("%p\n", buffer);
    if (buffer) {
        pci_dma_unmap(dev, buffer, len, DMA_DIRECTION_TO_DEVICE, 0);
    }
}
