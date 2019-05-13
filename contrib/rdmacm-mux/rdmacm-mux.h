/*
 * QEMU paravirtual RDMA - rdmacm-mux declarations
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

#ifndef RDMACM_MUX_H
#define RDMACM_MUX_H

#include "linux/if.h"
#include <infiniband/verbs.h>
#include <infiniband/umad.h>
#include <rdma/rdma_user_cm.h>

typedef enum RdmaCmMuxMsgType {
    RDMACM_MUX_MSG_TYPE_REQ   = 0,
    RDMACM_MUX_MSG_TYPE_RESP  = 1,
} RdmaCmMuxMsgType;

typedef enum RdmaCmMuxOpCode {
    RDMACM_MUX_OP_CODE_REG   = 0,
    RDMACM_MUX_OP_CODE_UNREG = 1,
    RDMACM_MUX_OP_CODE_MAD   = 2,
} RdmaCmMuxOpCode;

typedef enum RdmaCmMuxErrCode {
    RDMACM_MUX_ERR_CODE_OK        = 0,
    RDMACM_MUX_ERR_CODE_EINVAL    = 1,
    RDMACM_MUX_ERR_CODE_EEXIST    = 2,
    RDMACM_MUX_ERR_CODE_EACCES    = 3,
    RDMACM_MUX_ERR_CODE_ENOTFOUND = 4,
} RdmaCmMuxErrCode;

typedef struct RdmaCmMuxHdr {
    RdmaCmMuxMsgType msg_type;
    RdmaCmMuxOpCode op_code;
    union ibv_gid sgid;
    RdmaCmMuxErrCode err_code;
} RdmaCmUHdr;

typedef struct RdmaCmUMad {
    struct ib_user_mad hdr;
    char mad[RDMA_MAX_PRIVATE_DATA];
} RdmaCmUMad;

typedef struct RdmaCmMuxMsg {
    RdmaCmUHdr hdr;
    int umad_len;
    RdmaCmUMad umad;
} RdmaCmMuxMsg;

#endif
