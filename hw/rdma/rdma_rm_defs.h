/*
 * RDMA device: Definitions of Resource Manager structures
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

#ifndef RDMA_RM_DEFS_H
#define RDMA_RM_DEFS_H

#include "rdma_backend_defs.h"

#define MAX_PORTS             1
#define MAX_PORT_GIDS         1
#define MAX_GIDS              MAX_PORT_GIDS
#define MAX_PORT_PKEYS        1
#define MAX_PKEYS             MAX_PORT_PKEYS
#define MAX_UCS               512
#define MAX_MR_SIZE           (1UL << 27)
#define MAX_QP                1024
#define MAX_SGE               4
#define MAX_CQ                2048
#define MAX_MR                1024
#define MAX_PD                1024
#define MAX_QP_RD_ATOM        16
#define MAX_QP_INIT_RD_ATOM   16
#define MAX_AH                64

#define MAX_RM_TBL_NAME 16
typedef struct RdmaRmResTbl {
    char name[MAX_RM_TBL_NAME];
    QemuMutex lock;
    unsigned long *bitmap;
    size_t tbl_sz;
    size_t res_sz;
    void *tbl;
} RdmaRmResTbl;

typedef struct RdmaRmPD {
    RdmaBackendPD backend_pd;
    uint32_t ctx_handle;
} RdmaRmPD;

typedef struct RdmaRmCQ {
    RdmaBackendCQ backend_cq;
    void *opaque;
    bool notify;
} RdmaRmCQ;

typedef struct RdmaRmUserMR {
    void *host_virt;
    uint64_t guest_start;
    size_t length;
} RdmaRmUserMR;

/* MR (DMA region) */
typedef struct RdmaRmMR {
    RdmaBackendMR backend_mr;
    RdmaRmUserMR user_mr;
    uint32_t pd_handle;
    uint32_t lkey;
    uint32_t rkey;
} RdmaRmMR;

typedef struct RdmaRmUC {
    uint64_t uc_handle;
} RdmaRmUC;

typedef struct RdmaRmQP {
    RdmaBackendQP backend_qp;
    void *opaque;
    uint32_t qp_type;
    uint32_t qpn;
    uint32_t send_cq_handle;
    uint32_t recv_cq_handle;
    enum ibv_qp_state qp_state;
} RdmaRmQP;

typedef struct RdmaRmPort {
    union ibv_gid gid_tbl[MAX_PORT_GIDS];
    enum ibv_port_state state;
} RdmaRmPort;

struct RdmaDeviceResources {
    RdmaRmPort ports[MAX_PORTS];
    RdmaRmResTbl pd_tbl;
    RdmaRmResTbl mr_tbl;
    RdmaRmResTbl uc_tbl;
    RdmaRmResTbl qp_tbl;
    RdmaRmResTbl cq_tbl;
    RdmaRmResTbl cqe_ctx_tbl;
    GHashTable *qp_hash; /* Keeps mapping between real and emulated */
};

#endif
