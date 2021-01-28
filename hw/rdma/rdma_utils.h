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

#define rdma_error_report(fmt, ...) \
    error_report("%s: " fmt, "rdma", ## __VA_ARGS__)
#define rdma_warn_report(fmt, ...) \
    warn_report("%s: " fmt, "rdma", ## __VA_ARGS__)
#define rdma_info_report(fmt, ...) \
    info_report("%s: " fmt, "rdma", ## __VA_ARGS__)

typedef struct RdmaProtectedGQueue {
    QemuMutex lock;
    GQueue *list;
} RdmaProtectedGQueue;

typedef struct RdmaProtectedGSList {
    QemuMutex lock;
    GSList *list;
} RdmaProtectedGSList;

void *rdma_pci_dma_map(PCIDevice *dev, dma_addr_t addr, dma_addr_t plen);
void rdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len);
void rdma_protected_gqueue_init(RdmaProtectedGQueue *list);
void rdma_protected_gqueue_destroy(RdmaProtectedGQueue *list);
void rdma_protected_gqueue_append_int64(RdmaProtectedGQueue *list,
                                        int64_t value);
int64_t rdma_protected_gqueue_pop_int64(RdmaProtectedGQueue *list);
void rdma_protected_gslist_init(RdmaProtectedGSList *list);
void rdma_protected_gslist_destroy(RdmaProtectedGSList *list);
void rdma_protected_gslist_append_int32(RdmaProtectedGSList *list,
                                        int32_t value);
void rdma_protected_gslist_remove_int32(RdmaProtectedGSList *list,
                                        int32_t value);

static inline void addrconf_addr_eui48(uint8_t *eui, const char *addr)
{
    memcpy(eui, addr, 3);
    eui[3] = 0xFF;
    eui[4] = 0xFE;
    memcpy(eui + 5, addr + 3, 3);
    eui[0] ^= 2;
}

#endif
