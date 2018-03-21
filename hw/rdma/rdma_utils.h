/*
 * RDMA device: Debug utilities
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef RDMA_UTILS_H
#define RDMA_UTILS_H

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "sysemu/dma.h"

#define pr_info(fmt, ...) \
    fprintf(stdout, "%s: %-20s (%3d): " fmt, "pvrdma",  __func__, __LINE__,\
           ## __VA_ARGS__)

#define pr_err(fmt, ...) \
    fprintf(stderr, "%s: Error at %-20s (%3d): " fmt, "pvrdma", __func__, \
        __LINE__, ## __VA_ARGS__)

#ifdef PVRDMA_DEBUG
#define pr_dbg(fmt, ...) \
    fprintf(stdout, "%s: %-20s (%3d): " fmt, "pvrdma", __func__, __LINE__,\
           ## __VA_ARGS__)
#else
#define pr_dbg(fmt, ...)
#endif

void *rdma_pci_dma_map(PCIDevice *dev, dma_addr_t addr, dma_addr_t plen);
void rdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len);

#endif
