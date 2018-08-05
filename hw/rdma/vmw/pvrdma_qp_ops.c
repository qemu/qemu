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
    struct pvrdma_sge sge[0];
} PvrdmaSqWqe;

/* Recv Queue WQE */
typedef struct PvrdmaRqWqe {
    struct pvrdma_rq_wqe_hdr hdr;
    struct pvrdma_sge sge[0];
} PvrdmaRqWqe;

/*
 * 1. Put CQE on send CQ ring
 * 2. Put CQ number on dsr completion ring
 * 3. Interrupt host
 */
static int pvrdma_post_cqe(PVRDMADev *dev, uint32_t cq_handle,
                           struct pvrdma_cqe *cqe)
{
    struct pvrdma_cqe *cqe1;
    struct pvrdma_cqne *cqne;
    PvrdmaRing *ring;
    RdmaRmCQ *cq = rdma_rm_get_cq(&dev->rdma_dev_res, cq_handle);

    if (unlikely(!cq)) {
        pr_dbg("Invalid cqn %d\n", cq_handle);
        return -EINVAL;
    }

    ring = (PvrdmaRing *)cq->opaque;
    pr_dbg("ring=%p\n", ring);

    /* Step #1: Put CQE on CQ ring */
    pr_dbg("Writing CQE\n");
    cqe1 = pvrdma_ring_next_elem_write(ring);
    if (unlikely(!cqe1)) {
        return -EINVAL;
    }

    memset(cqe1, 0, sizeof(*cqe1));
    cqe1->wr_id = cqe->wr_id;
    cqe1->qp = cqe->qp;
    cqe1->opcode = cqe->opcode;
    cqe1->status = cqe->status;
    cqe1->vendor_err = cqe->vendor_err;

    pvrdma_ring_write_inc(ring);

    /* Step #2: Put CQ number on dsr completion ring */
    pr_dbg("Writing CQNE\n");
    cqne = pvrdma_ring_next_elem_write(&dev->dsr_info.cq);
    if (unlikely(!cqne)) {
        return -EINVAL;
    }

    cqne->info = cq_handle;
    pvrdma_ring_write_inc(&dev->dsr_info.cq);

    pr_dbg("cq->notify=%d\n", cq->notify);
    if (cq->notify) {
        cq->notify = false;
        post_interrupt(dev, INTR_VEC_CMD_COMPLETION_Q);
    }

    return 0;
}

static void pvrdma_qp_ops_comp_handler(int status, unsigned int vendor_err,
                                       void *ctx)
{
    CompHandlerCtx *comp_ctx = (CompHandlerCtx *)ctx;

    pr_dbg("cq_handle=%d\n", comp_ctx->cq_handle);
    pr_dbg("wr_id=%" PRIx64 "\n", comp_ctx->cqe.wr_id);
    pr_dbg("status=%d\n", status);
    pr_dbg("vendor_err=0x%x\n", vendor_err);
    comp_ctx->cqe.status = status;
    comp_ctx->cqe.vendor_err = vendor_err;
    pvrdma_post_cqe(comp_ctx->dev, comp_ctx->cq_handle, &comp_ctx->cqe);
    g_free(ctx);
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

int pvrdma_qp_send(PVRDMADev *dev, uint32_t qp_handle)
{
    RdmaRmQP *qp;
    PvrdmaSqWqe *wqe;
    PvrdmaRing *ring;

    pr_dbg("qp_handle=0x%x\n", qp_handle);

    qp = rdma_rm_get_qp(&dev->rdma_dev_res, qp_handle);
    if (unlikely(!qp)) {
        return -EINVAL;
    }

    ring = (PvrdmaRing *)qp->opaque;
    pr_dbg("sring=%p\n", ring);

    wqe = (struct PvrdmaSqWqe *)pvrdma_ring_next_elem_read(ring);
    while (wqe) {
        CompHandlerCtx *comp_ctx;

        pr_dbg("wr_id=%" PRIx64 "\n", wqe->hdr.wr_id);

        /* Prepare CQE */
        comp_ctx = g_malloc(sizeof(CompHandlerCtx));
        comp_ctx->dev = dev;
        comp_ctx->cq_handle = qp->send_cq_handle;
        comp_ctx->cqe.wr_id = wqe->hdr.wr_id;
        comp_ctx->cqe.qp = qp_handle;
        comp_ctx->cqe.opcode = wqe->hdr.opcode;

        rdma_backend_post_send(&dev->backend_dev, &qp->backend_qp, qp->qp_type,
                               (struct ibv_sge *)&wqe->sge[0], wqe->hdr.num_sge,
                               (union ibv_gid *)wqe->hdr.wr.ud.av.dgid,
                               wqe->hdr.wr.ud.remote_qpn,
                               wqe->hdr.wr.ud.remote_qkey, comp_ctx);

        pvrdma_ring_read_inc(ring);

        wqe = pvrdma_ring_next_elem_read(ring);
    }

    return 0;
}

int pvrdma_qp_recv(PVRDMADev *dev, uint32_t qp_handle)
{
    RdmaRmQP *qp;
    PvrdmaRqWqe *wqe;
    PvrdmaRing *ring;

    pr_dbg("qp_handle=0x%x\n", qp_handle);

    qp = rdma_rm_get_qp(&dev->rdma_dev_res, qp_handle);
    if (unlikely(!qp)) {
        return -EINVAL;
    }

    ring = &((PvrdmaRing *)qp->opaque)[1];
    pr_dbg("rring=%p\n", ring);

    wqe = (struct PvrdmaRqWqe *)pvrdma_ring_next_elem_read(ring);
    while (wqe) {
        CompHandlerCtx *comp_ctx;

        pr_dbg("wr_id=%" PRIx64 "\n", wqe->hdr.wr_id);

        /* Prepare CQE */
        comp_ctx = g_malloc(sizeof(CompHandlerCtx));
        comp_ctx->dev = dev;
        comp_ctx->cq_handle = qp->recv_cq_handle;
        comp_ctx->cqe.qp = qp_handle;
        comp_ctx->cqe.wr_id = wqe->hdr.wr_id;

        rdma_backend_post_recv(&dev->backend_dev, &dev->rdma_dev_res,
                               &qp->backend_qp, qp->qp_type,
                               (struct ibv_sge *)&wqe->sge[0], wqe->hdr.num_sge,
                               comp_ctx);

        pvrdma_ring_read_inc(ring);

        wqe = pvrdma_ring_next_elem_read(ring);
    }

    return 0;
}

void pvrdma_cq_poll(RdmaDeviceResources *dev_res, uint32_t cq_handle)
{
    RdmaRmCQ *cq;

    cq = rdma_rm_get_cq(dev_res, cq_handle);
    if (!cq) {
        pr_dbg("Invalid CQ# %d\n", cq_handle);
        return;
    }

    rdma_backend_poll_cq(dev_res, &cq->backend_cq);
}
