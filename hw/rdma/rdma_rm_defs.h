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

#define MAX_PORTS             1 /* Do not change - we support only one port */
#define MAX_PORT_GIDS         255
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
#define MAX_SRQ               512

#define MAX_RM_TBL_NAME             16
#define MAX_CONSEQ_EMPTY_POLL_CQ    4096 /* considered as error above this */

typedef struct RdmaRmResTbl {
    char name[MAX_RM_TBL_NAME];
    QemuMutex lock;
    unsigned long *bitmap;
    size_t tbl_sz;
    size_t res_sz;
    void *tbl;
    uint32_t used; /* number of used entries in the table */
} RdmaRmResTbl;

typedef struct RdmaRmPD {
    RdmaBackendPD backend_pd;
    uint32_t ctx_handle;
} RdmaRmPD;

typedef enum CQNotificationType {
    CNT_CLEAR,
    CNT_ARM,
    CNT_SET,
} CQNotificationType;

typedef struct RdmaRmCQ {
    RdmaBackendCQ backend_cq;
    void *opaque;
    CQNotificationType notify;
} RdmaRmCQ;

/* MR (DMA region) */
typedef struct RdmaRmMR {
    RdmaBackendMR backend_mr;
    void *virt;
    uint64_t start;
    size_t length;
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
    uint8_t is_srq;
} RdmaRmQP;

typedef struct RdmaRmSRQ {
    RdmaBackendSRQ backend_srq;
    uint32_t recv_cq_handle;
    void *opaque;
} RdmaRmSRQ;

typedef struct RdmaRmGid {
    union ibv_gid gid;
    int backend_gid_index;
} RdmaRmGid;

typedef struct RdmaRmPort {
    RdmaRmGid gid_tbl[MAX_PORT_GIDS];
    enum ibv_port_state state;
} RdmaRmPort;

typedef struct RdmaRmStats {
    uint64_t tx;
    uint64_t tx_len;
    uint64_t tx_err;
    uint64_t rx_bufs;
    uint64_t rx_bufs_len;
    uint64_t rx_bufs_err;
    uint64_t rx_srq;
    uint64_t completions;
    uint64_t mad_tx;
    uint64_t mad_tx_err;
    uint64_t mad_rx;
    uint64_t mad_rx_err;
    uint64_t mad_rx_bufs;
    uint64_t mad_rx_bufs_err;
    uint64_t poll_cq_from_bk;
    uint64_t poll_cq_from_guest;
    uint64_t poll_cq_from_guest_empty;
    uint64_t poll_cq_ppoll_to;
    uint32_t missing_cqe;
} RdmaRmStats;

struct RdmaDeviceResources {
    RdmaRmPort port;
    RdmaRmResTbl pd_tbl;
    RdmaRmResTbl mr_tbl;
    RdmaRmResTbl uc_tbl;
    RdmaRmResTbl qp_tbl;
    RdmaRmResTbl cq_tbl;
    RdmaRmResTbl cqe_ctx_tbl;
    RdmaRmResTbl srq_tbl;
    GHashTable *qp_hash; /* Keeps mapping between real and emulated */
    QemuMutex lock;
    RdmaRmStats stats;
};

#endif
