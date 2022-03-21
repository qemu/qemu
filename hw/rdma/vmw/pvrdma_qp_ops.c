/*
 * QEMU paravirtual RDMA - QP implementation
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

#include "qemu/osdep.h"

#include "../rdma_utils.h"
#include "../rdma_rm.h"
#include "../rdma_backend.h"

#include "trace.h"

#include "pvrdma.h"
#include "standard-headers/rdma/vmw_pvrdma-abi.h"
#include "pvrdma_qp_ops.h"

typedef struct CompHandlerCtx {
    PVRDMADev *dev;
    uint32_t cq_handle;
    struct pvrdma_cqe cqe;
} CompHandlerCtx;

/* Send Queue WQE */
typedef struct PvrdmaSqWqe {
    struct pvrdma_sq_wqe_hdr hdr;
    struct pvrdma_sge sge[];
} PvrdmaSqWqe;

/* Recv Queue WQE */
typedef struct PvrdmaRqWqe {
    struct pvrdma_rq_wqe_hdr hdr;
    struct pvrdma_sge sge[];
} PvrdmaRqWqe;

/*
 * 1. Put CQE on send CQ ring
 * 2. Put CQ number on dsr completion ring
 * 3. Interrupt host
 */
static int pvrdma_post_cqe(PVRDMADev *dev, uint32_t cq_handle,
                           struct pvrdma_cqe *cqe, struct ibv_wc *wc)
{
    struct pvrdma_cqe *cqe1;
    struct pvrdma_cqne *cqne;
    PvrdmaRing *ring;
    RdmaRmCQ *cq = rdma_rm_get_cq(&dev->rdma_dev_res, cq_handle);

    if (unlikely(!cq)) {
        return -EINVAL;
    }

    ring = (PvrdmaRing *)cq->opaque;

    /* Step #1: Put CQE on CQ ring */
    cqe1 = pvrdma_ring_next_elem_write(ring);
    if (unlikely(!cqe1)) {
        return -EINVAL;
    }

    memset(cqe1, 0, sizeof(*cqe1));
    cqe1->wr_id = cqe->wr_id;
    cqe1->qp = cqe->qp ? cqe->qp : wc->qp_num;
    cqe1->opcode = cqe->opcode;
    cqe1->status = wc->status;
    cqe1->byte_len = wc->byte_len;
    cqe1->src_qp = wc->src_qp;
    cqe1->wc_flags = wc->wc_flags;
    cqe1->vendor_err = wc->vendor_err;

    trace_pvrdma_post_cqe(cq_handle, cq->notify, cqe1->wr_id, cqe1->qp,
                          cqe1->opcode, cqe1->status, cqe1->byte_len,
                          cqe1->src_qp, cqe1->wc_flags, cqe1->vendor_err);

    pvrdma_ring_write_inc(ring);

    /* Step #2: Put CQ number on dsr completion ring */
    cqne = pvrdma_ring_next_elem_write(&dev->dsr_info.cq);
    if (unlikely(!cqne)) {
        return -EINVAL;
    }

    cqne->info = cq_handle;
    pvrdma_ring_write_inc(&dev->dsr_info.cq);

    if (cq->notify != CNT_CLEAR) {
        if (cq->notify == CNT_ARM) {
            cq->notify = CNT_CLEAR;
        }
        post_interrupt(dev, INTR_VEC_CMD_COMPLETION_Q);
    }

    return 0;
}

static void pvrdma_qp_ops_comp_handler(void *ctx, struct ibv_wc *wc)
{
    CompHandlerCtx *comp_ctx = (CompHandlerCtx *)ctx;

    pvrdma_post_cqe(comp_ctx->dev, comp_ctx->cq_handle, &comp_ctx->cqe, wc);

    g_free(ctx);
}

static void complete_with_error(uint32_t vendor_err, void *ctx)
{
    struct ibv_wc wc = {};

    wc.status = IBV_WC_GENERAL_ERR;
    wc.vendor_err = vendor_err;

    pvrdma_qp_ops_comp_handler(ctx, &wc);
}

void pvrdma_qp_ops_fini(void)
{
    rdma_backend_unregister_comp_handler();
}

int pvrdma_qp_ops_init(void)
{
    rdma_backend_register_comp_handler(pvrdma_qp_ops_comp_handler);

    return 0;
}

void pvrdma_qp_send(PVRDMADev *dev, uint32_t qp_handle)
{
    RdmaRmQP *qp;
    PvrdmaSqWqe *wqe;
    PvrdmaRing *ring;
    int sgid_idx;
    union ibv_gid *sgid;

    qp = rdma_rm_get_qp(&dev->rdma_dev_res, qp_handle);
    if (unlikely(!qp)) {
        return;
    }

    ring = (PvrdmaRing *)qp->opaque;

    wqe = (struct PvrdmaSqWqe *)pvrdma_ring_next_elem_read(ring);
    while (wqe) {
        CompHandlerCtx *comp_ctx;

        /* Prepare CQE */
        comp_ctx = g_new(CompHandlerCtx, 1);
        comp_ctx->dev = dev;
        comp_ctx->cq_handle = qp->send_cq_handle;
        comp_ctx->cqe.wr_id = wqe->hdr.wr_id;
        comp_ctx->cqe.qp = qp_handle;
        comp_ctx->cqe.opcode = IBV_WC_SEND;

        sgid = rdma_rm_get_gid(&dev->rdma_dev_res, wqe->hdr.wr.ud.av.gid_index);
        if (!sgid) {
            rdma_error_report("Failed to get gid for idx %d",
                              wqe->hdr.wr.ud.av.gid_index);
            complete_with_error(VENDOR_ERR_INV_GID_IDX, comp_ctx);
            continue;
        }

        sgid_idx = rdma_rm_get_backend_gid_index(&dev->rdma_dev_res,
                                                 &dev->backend_dev,
                                                 wqe->hdr.wr.ud.av.gid_index);
        if (sgid_idx <= 0) {
            rdma_error_report("Failed to get bk sgid_idx for sgid_idx %d",
                              wqe->hdr.wr.ud.av.gid_index);
            complete_with_error(VENDOR_ERR_INV_GID_IDX, comp_ctx);
            continue;
        }

        if (wqe->hdr.num_sge > dev->dev_attr.max_sge) {
            rdma_error_report("Invalid num_sge=%d (max %d)", wqe->hdr.num_sge,
                              dev->dev_attr.max_sge);
            complete_with_error(VENDOR_ERR_INV_NUM_SGE, comp_ctx);
            continue;
        }

        rdma_backend_post_send(&dev->backend_dev, &qp->backend_qp, qp->qp_type,
                               (struct ibv_sge *)&wqe->sge[0], wqe->hdr.num_sge,
                               sgid_idx, sgid,
                               (union ibv_gid *)wqe->hdr.wr.ud.av.dgid,
                               wqe->hdr.wr.ud.remote_qpn,
                               wqe->hdr.wr.ud.remote_qkey, comp_ctx);

        pvrdma_ring_read_inc(ring);

        wqe = pvrdma_ring_next_elem_read(ring);
    }
}

void pvrdma_qp_recv(PVRDMADev *dev, uint32_t qp_handle)
{
    RdmaRmQP *qp;
    PvrdmaRqWqe *wqe;
    PvrdmaRing *ring;

    qp = rdma_rm_get_qp(&dev->rdma_dev_res, qp_handle);
    if (unlikely(!qp)) {
        return;
    }

    ring = &((PvrdmaRing *)qp->opaque)[1];

    wqe = (struct PvrdmaRqWqe *)pvrdma_ring_next_elem_read(ring);
    while (wqe) {
        CompHandlerCtx *comp_ctx;

        /* Prepare CQE */
        comp_ctx = g_new(CompHandlerCtx, 1);
        comp_ctx->dev = dev;
        comp_ctx->cq_handle = qp->recv_cq_handle;
        comp_ctx->cqe.wr_id = wqe->hdr.wr_id;
        comp_ctx->cqe.qp = qp_handle;
        comp_ctx->cqe.opcode = IBV_WC_RECV;

        if (wqe->hdr.num_sge > dev->dev_attr.max_sge) {
            rdma_error_report("Invalid num_sge=%d (max %d)", wqe->hdr.num_sge,
                              dev->dev_attr.max_sge);
            complete_with_error(VENDOR_ERR_INV_NUM_SGE, comp_ctx);
            continue;
        }

        rdma_backend_post_recv(&dev->backend_dev, &qp->backend_qp, qp->qp_type,
                               (struct ibv_sge *)&wqe->sge[0], wqe->hdr.num_sge,
                               comp_ctx);

        pvrdma_ring_read_inc(ring);

        wqe = pvrdma_ring_next_elem_read(ring);
    }
}

void pvrdma_srq_recv(PVRDMADev *dev, uint32_t srq_handle)
{
    RdmaRmSRQ *srq;
    PvrdmaRqWqe *wqe;
    PvrdmaRing *ring;

    srq = rdma_rm_get_srq(&dev->rdma_dev_res, srq_handle);
    if (unlikely(!srq)) {
        return;
    }

    ring = (PvrdmaRing *)srq->opaque;

    wqe = (struct PvrdmaRqWqe *)pvrdma_ring_next_elem_read(ring);
    while (wqe) {
        CompHandlerCtx *comp_ctx;

        /* Prepare CQE */
        comp_ctx = g_new(CompHandlerCtx, 1);
        comp_ctx->dev = dev;
        comp_ctx->cq_handle = srq->recv_cq_handle;
        comp_ctx->cqe.wr_id = wqe->hdr.wr_id;
        comp_ctx->cqe.qp = 0;
        comp_ctx->cqe.opcode = IBV_WC_RECV;

        if (wqe->hdr.num_sge > dev->dev_attr.max_sge) {
            rdma_error_report("Invalid num_sge=%d (max %d)", wqe->hdr.num_sge,
                              dev->dev_attr.max_sge);
            complete_with_error(VENDOR_ERR_INV_NUM_SGE, comp_ctx);
            continue;
        }

        rdma_backend_post_srq_recv(&dev->backend_dev, &srq->backend_srq,
                                   (struct ibv_sge *)&wqe->sge[0],
                                   wqe->hdr.num_sge,
                                   comp_ctx);

        pvrdma_ring_read_inc(ring);

        wqe = pvrdma_ring_next_elem_read(ring);
    }

}

void pvrdma_cq_poll(RdmaDeviceResources *dev_res, uint32_t cq_handle)
{
    RdmaRmCQ *cq;

    cq = rdma_rm_get_cq(dev_res, cq_handle);
    if (!cq) {
        return;
    }

    rdma_backend_poll_cq(dev_res, &cq->backend_cq);
}
