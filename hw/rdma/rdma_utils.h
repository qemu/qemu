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

#include "qemu/error-report.h"
#include "hw/pci/pci.h"
#include "sysemu/dma.h"
#include "stdio.h"

#define rdma_error_report(fmt, ...) \
    error_report("%s: " fmt, "rdma", ## __VA_ARGS__)
#define rdma_warn_report(fmt, ...) \
    warn_report("%s: " fmt, "rdma", ## __VA_ARGS__)
#define rdma_info_report(fmt, ...) \
    info_report("%s: " fmt, "rdma", ## __VA_ARGS__)

void *rdma_pci_dma_map(PCIDevice *dev, dma_addr_t addr, dma_addr_t plen);
void rdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len);

static inline void addrconf_addr_eui48(uint8_t *eui, const char *addr)
{
    memcpy(eui, addr, 3);
    eui[3] = 0xFF;
    eui[4] = 0xFE;
    memcpy(eui + 5, addr + 3, 3);
    eui[0] ^= 2;
}

#endif
