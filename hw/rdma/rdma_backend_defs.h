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

#include "qemu/thread.h"
#include "chardev/char-fe.h"
#include <infiniband/verbs.h>
#include "contrib/rdmacm-mux/rdmacm-mux.h"
#include "rdma_utils.h"

typedef struct RdmaDeviceResources RdmaDeviceResources;

typedef struct RdmaBackendThread {
    QemuThread thread;
    bool run; /* Set by thread manager to let thread know it should exit */
    bool is_running; /* Set by the thread to report its status */
} RdmaBackendThread;

typedef struct RdmaCmMux {
    CharBackend *chr_be;
    int can_receive;
} RdmaCmMux;

typedef struct RdmaBackendDev {
    RdmaBackendThread comp_thread;
    PCIDevice *dev;
    RdmaDeviceResources *rdma_dev_res;
    struct ibv_device *ib_dev;
    struct ibv_context *context;
    struct ibv_comp_channel *channel;
    uint8_t port_num;
    RdmaProtectedQList recv_mads_list;
    RdmaCmMux rdmacm_mux;
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
    uint8_t sgid_idx;
    RdmaProtectedGSList cqe_ctx_list;
} RdmaBackendQP;

typedef struct RdmaBackendSRQ {
    struct ibv_srq *ibsrq;
    RdmaProtectedGSList cqe_ctx_list;
} RdmaBackendSRQ;

#endif
