/*
 * QEMU VMWARE paravirtual RDMA device definitions
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

#ifndef PVRDMA_PVRDMA_H
#define PVRDMA_PVRDMA_H

#include "qemu/units.h"
#include "qemu/notify.h"
#include "hw/pci/pci.h"
#include "hw/pci/msix.h"
#include "chardev/char-fe.h"
#include "hw/net/vmxnet3_defs.h"

#include "../rdma_backend_defs.h"
#include "../rdma_rm_defs.h"

#include "standard-headers/drivers/infiniband/hw/vmw_pvrdma/pvrdma_ring.h"
#include "standard-headers/drivers/infiniband/hw/vmw_pvrdma/pvrdma_dev_api.h"
#include "pvrdma_dev_ring.h"

/* BARs */
#define RDMA_MSIX_BAR_IDX    0
#define RDMA_REG_BAR_IDX     1
#define RDMA_UAR_BAR_IDX     2
#define RDMA_BAR0_MSIX_SIZE  (16 * KiB)
#define RDMA_BAR1_REGS_SIZE  64
#define RDMA_BAR2_UAR_SIZE   (0x1000 * MAX_UCS) /* each uc gets page */

/* MSIX */
#define RDMA_MAX_INTRS       3
#define RDMA_MSIX_TABLE      0x0000
#define RDMA_MSIX_PBA        0x2000

/* Interrupts Vectors */
#define INTR_VEC_CMD_RING            0
#define INTR_VEC_CMD_ASYNC_EVENTS    1
#define INTR_VEC_CMD_COMPLETION_Q    2

/* HW attributes */
#define PVRDMA_HW_NAME       "pvrdma"
#define PVRDMA_HW_VERSION    17
#define PVRDMA_FW_VERSION    14

/* Some defaults */
#define PVRDMA_PKEY          0xFFFF

typedef struct DSRInfo {
    dma_addr_t dma;
    struct pvrdma_device_shared_region *dsr;

    union pvrdma_cmd_req *req;
    union pvrdma_cmd_resp *rsp;

    struct pvrdma_ring *async_ring_state;
    PvrdmaRing async;

    struct pvrdma_ring *cq_ring_state;
    PvrdmaRing cq;
} DSRInfo;

typedef struct PVRDMADevStats {
    uint64_t commands;
    uint64_t regs_reads;
    uint64_t regs_writes;
    uint64_t uar_writes;
    uint64_t interrupts;
} PVRDMADevStats;

typedef struct PVRDMADev {
    PCIDevice parent_obj;
    MemoryRegion msix;
    MemoryRegion regs;
    uint32_t regs_data[RDMA_BAR1_REGS_SIZE];
    MemoryRegion uar;
    uint32_t uar_data[RDMA_BAR2_UAR_SIZE];
    DSRInfo dsr_info;
    int interrupt_mask;
    struct ibv_device_attr dev_attr;
    uint64_t node_guid;
    char *backend_eth_device_name;
    char *backend_device_name;
    uint8_t backend_port_num;
    RdmaBackendDev backend_dev;
    RdmaDeviceResources rdma_dev_res;
    CharBackend mad_chr;
    VMXNET3State *func0;
    Notifier shutdown_notifier;
    PVRDMADevStats stats;
} PVRDMADev;
#define PVRDMA_DEV(dev) OBJECT_CHECK(PVRDMADev, (dev), PVRDMA_HW_NAME)

static inline int get_reg_val(PVRDMADev *dev, hwaddr addr, uint32_t *val)
{
    int idx = addr >> 2;

    if (idx >= RDMA_BAR1_REGS_SIZE) {
        return -EINVAL;
    }

    *val = dev->regs_data[idx];

    return 0;
}

static inline int set_reg_val(PVRDMADev *dev, hwaddr addr, uint32_t val)
{
    int idx = addr >> 2;

    if (idx >= RDMA_BAR1_REGS_SIZE) {
        return -EINVAL;
    }

    dev->regs_data[idx] = val;

    return 0;
}

static inline void post_interrupt(PVRDMADev *dev, unsigned vector)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);

    if (likely(!dev->interrupt_mask)) {
        dev->stats.interrupts++;
        msix_notify(pci_dev, vector);
    }
}

int pvrdma_exec_cmd(PVRDMADev *dev);

#endif
