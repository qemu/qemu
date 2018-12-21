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

#include "hw/pci/pci.h"
#include "sysemu/dma.h"
#include "stdio.h"

#define pr_info(fmt, ...) \
    fprintf(stdout, "%s: %-20s (%3d): " fmt, "rdma",  __func__, __LINE__,\
           ## __VA_ARGS__)

#define pr_err(fmt, ...) \
    fprintf(stderr, "%s: Error at %-20s (%3d): " fmt, "rdma", __func__, \
        __LINE__, ## __VA_ARGS__)

#ifdef PVRDMA_DEBUG
extern unsigned long pr_dbg_cnt;

#define init_pr_dbg(void) \
{ \
    pr_dbg_cnt = 0; \
}

#define pr_dbg(fmt, ...) \
    fprintf(stdout, "%lx %ld: %-20s (%3d): " fmt, pthread_self(), pr_dbg_cnt++, \
            __func__, __LINE__, ## __VA_ARGS__)

#define pr_dbg_buf(title, buf, len) \
{ \
    int i; \
    char *b = g_malloc0(len * 3 + 1); \
    char b1[4]; \
    for (i = 0; i < len; i++) { \
        sprintf(b1, "%.2X ", buf[i] & 0x000000FF); \
        strcat(b, b1); \
    } \
    pr_dbg("%s (%d): %s\n", title, len, b); \
    g_free(b); \
}

#else
#define init_pr_dbg(void)
#define pr_dbg(fmt, ...)
#define pr_dbg_buf(title, buf, len)
#endif

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
