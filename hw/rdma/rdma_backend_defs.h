/*
 *  RDMA device: Definitions of Backend Device structures
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

#ifndef RDMA_BACKEND_DEFS_H
#define RDMA_BACKEND_DEFS_H

#include <infiniband/verbs.h>
#include "qemu/thread.h"

typedef struct RdmaDeviceResources RdmaDeviceResources;

typedef struct RdmaBackendThread {
    QemuThread thread;
    QemuMutex mutex;
    bool run; /* Set by thread manager to let thread know it should exit */
    bool is_running; /* Set by the thread to report its status */
} RdmaBackendThread;

typedef struct RdmaBackendDev {
    struct ibv_device_attr dev_attr;
    RdmaBackendThread comp_thread;
    union ibv_gid gid;
    PCIDevice *dev;
    RdmaDeviceResources *rdma_dev_res;
    struct ibv_device *ib_dev;
    struct ibv_context *context;
    struct ibv_comp_channel *channel;
    uint8_t port_num;
    uint8_t backend_gid_idx;
} RdmaBackendDev;

typedef struct RdmaBackendPD {
    struct ibv_pd *ibpd;
} RdmaBackendPD;

typedef struct RdmaBackendMR {
    struct ibv_pd *ibpd;
    struct ibv_mr *ibmr;
} RdmaBackendMR;

typedef struct RdmaBackendCQ {
    RdmaBackendDev *backend_dev;
    struct ibv_cq *ibcq;
} RdmaBackendCQ;

typedef struct RdmaBackendQP {
    struct ibv_pd *ibpd;
    struct ibv_qp *ibqp;
} RdmaBackendQP;

#endif
