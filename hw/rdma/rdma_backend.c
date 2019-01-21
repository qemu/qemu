/*
 * QEMU paravirtual RDMA - Generic RDMA backend
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
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qapi-events-rdma.h"

#include <infiniband/verbs.h>
#include <infiniband/umad_types.h>
#include <infiniband/umad.h>
#include <rdma/rdma_user_cm.h>

#include "contrib/rdmacm-mux/rdmacm-mux.h"
#include "trace.h"
#include "rdma_utils.h"
#include "rdma_rm.h"
#include "rdma_backend.h"

#define THR_NAME_LEN 16
#define THR_POLL_TO  5000

#define MAD_HDR_SIZE sizeof(struct ibv_grh)

typedef struct BackendCtx {
    void *up_ctx;
    bool is_tx_req;
    struct ibv_sge sge; /* Used to save MAD recv buffer */
} BackendCtx;

struct backend_umad {
    struct ib_user_mad hdr;
    char mad[RDMA_MAX_PRIVATE_DATA];
};

static void (*comp_handler)(void *ctx, struct ibv_wc *wc);

static void dummy_comp_handler(void *ctx, struct ibv_wc *wc)
{
    pr_err("No completion handler is registered\n");
}

static inline void complete_work(enum ibv_wc_status status, uint32_t vendor_err,
                                 void *ctx)
{
    struct ibv_wc wc = {0};

    wc.status = status;
    wc.vendor_err = vendor_err;

    comp_handler(ctx, &wc);
}

static void poll_cq(RdmaDeviceResources *rdma_dev_res, struct ibv_cq *ibcq)
{
    int i, ne;
    BackendCtx *bctx;
    struct ibv_wc wc[2];

    pr_dbg("Entering poll_cq loop on cq %p\n", ibcq);
    do {
        ne = ibv_poll_cq(ibcq, ARRAY_SIZE(wc), wc);

        pr_dbg("Got %d completion(s) from cq %p\n", ne, ibcq);

        for (i = 0; i < ne; i++) {
            pr_dbg("wr_id=0x%" PRIx64 "\n", wc[i].wr_id);
            pr_dbg("status=%d\n", wc[i].status);

            bctx = rdma_rm_get_cqe_ctx(rdma_dev_res, wc[i].wr_id);
            if (unlikely(!bctx)) {
                pr_dbg("Error: Failed to find ctx for req %" PRId64 "\n",
                       wc[i].wr_id);
                continue;
            }
            pr_dbg("Processing %s CQE\n", bctx->is_tx_req ? "send" : "recv");

            comp_handler(bctx->up_ctx, &wc[i]);

            rdma_rm_dealloc_cqe_ctx(rdma_dev_res, wc[i].wr_id);
            g_free(bctx);
        }
    } while (ne > 0);

    if (ne < 0) {
        pr_dbg("Got error %d from ibv_poll_cq\n", ne);
    }
}

static void *comp_handler_thread(void *arg)
{
    RdmaBackendDev *backend_dev = (RdmaBackendDev *)arg;
    int rc;
    struct ibv_cq *ev_cq;
    void *ev_ctx;
    int flags;
    GPollFD pfds[1];

    /* Change to non-blocking mode */
    flags = fcntl(backend_dev->channel->fd, F_GETFL);
    rc = fcntl(backend_dev->channel->fd, F_SETFL, flags | O_NONBLOCK);
    if (rc < 0) {
        pr_dbg("Fail to change to non-blocking mode\n");
        return NULL;
    }

    pr_dbg("Starting\n");

    pfds[0].fd = backend_dev->channel->fd;
    pfds[0].events = G_IO_IN | G_IO_HUP | G_IO_ERR;

    backend_dev->comp_thread.is_running = true;

    while (backend_dev->comp_thread.run) {
        do {
            rc = qemu_poll_ns(pfds, 1, THR_POLL_TO * (int64_t)SCALE_MS);
        } while (!rc && backend_dev->comp_thread.run);

        if (backend_dev->comp_thread.run) {
            pr_dbg("Waiting for completion on channel %p\n", backend_dev->channel);
            rc = ibv_get_cq_event(backend_dev->channel, &ev_cq, &ev_ctx);
            pr_dbg("ibv_get_cq_event=%d\n", rc);
            if (unlikely(rc)) {
                pr_dbg("---> ibv_get_cq_event (%d)\n", rc);
                continue;
            }

            rc = ibv_req_notify_cq(ev_cq, 0);
            if (unlikely(rc)) {
                pr_dbg("Error %d from ibv_req_notify_cq\n", rc);
            }

            poll_cq(backend_dev->rdma_dev_res, ev_cq);

            ibv_ack_cq_events(ev_cq, 1);
        }
    }

    pr_dbg("Going down\n");

    /* TODO: Post cqe for all remaining buffs that were posted */

    backend_dev->comp_thread.is_running = false;

    qemu_thread_exit(0);

    return NULL;
}

static inline void disable_rdmacm_mux_async(RdmaBackendDev *backend_dev)
{
    atomic_set(&backend_dev->rdmacm_mux.can_receive, 0);
}

static inline void enable_rdmacm_mux_async(RdmaBackendDev *backend_dev)
{
    atomic_set(&backend_dev->rdmacm_mux.can_receive, sizeof(RdmaCmMuxMsg));
}

static inline int rdmacm_mux_can_process_async(RdmaBackendDev *backend_dev)
{
    return atomic_read(&backend_dev->rdmacm_mux.can_receive);
}

static int check_mux_op_status(CharBackend *mad_chr_be)
{
    RdmaCmMuxMsg msg = {};
    int ret;

    pr_dbg("Reading response\n");
    ret = qemu_chr_fe_read_all(mad_chr_be, (uint8_t *)&msg, sizeof(msg));
    if (ret != sizeof(msg)) {
        pr_dbg("Invalid message size %d, expecting %ld\n", ret, sizeof(msg));
        return -EIO;
    }

    pr_dbg("msg_type=%d\n", msg.hdr.msg_type);
    pr_dbg("op_code=%d\n", msg.hdr.op_code);
    pr_dbg("err_code=%d\n", msg.hdr.err_code);

    if (msg.hdr.msg_type != RDMACM_MUX_MSG_TYPE_RESP) {
        pr_dbg("Invalid message type %d\n", msg.hdr.msg_type);
        return -EIO;
    }

    if (msg.hdr.err_code != RDMACM_MUX_ERR_CODE_OK) {
        pr_dbg("Operation failed in mux, error code %d\n", msg.hdr.err_code);
        return -EIO;
    }

    return 0;
}

static int exec_rdmacm_mux_req(RdmaBackendDev *backend_dev, RdmaCmMuxMsg *msg)
{
    int rc = 0;

    pr_dbg("Executing request %d\n", msg->hdr.op_code);

    msg->hdr.msg_type = RDMACM_MUX_MSG_TYPE_REQ;
    disable_rdmacm_mux_async(backend_dev);
    rc = qemu_chr_fe_write(backend_dev->rdmacm_mux.chr_be,
                           (const uint8_t *)msg, sizeof(*msg));
    if (rc != sizeof(*msg)) {
        enable_rdmacm_mux_async(backend_dev);
        pr_dbg("Fail to send request to rdmacm_mux (rc=%d)\n", rc);
        return -EIO;
    }

    rc = check_mux_op_status(backend_dev->rdmacm_mux.chr_be);
    if (rc) {
        pr_dbg("Fail to execute rdmacm_mux request %d (rc=%d)\n",
               msg->hdr.op_code, rc);
    }

    enable_rdmacm_mux_async(backend_dev);

    return 0;
}

static void stop_backend_thread(RdmaBackendThread *thread)
{
    thread->run = false;
    while (thread->is_running) {
        pr_dbg("Waiting for thread to complete\n");
        sleep(THR_POLL_TO / SCALE_US / 2);
    }
}

static void start_comp_thread(RdmaBackendDev *backend_dev)
{
    char thread_name[THR_NAME_LEN] = {0};

    stop_backend_thread(&backend_dev->comp_thread);

    snprintf(thread_name, sizeof(thread_name), "rdma_comp_%s",
             ibv_get_device_name(backend_dev->ib_dev));
    backend_dev->comp_thread.run = true;
    qemu_thread_create(&backend_dev->comp_thread.thread, thread_name,
                       comp_handler_thread, backend_dev, QEMU_THREAD_DETACHED);
}

void rdma_backend_register_comp_handler(void (*handler)(void *ctx,
                                                         struct ibv_wc *wc))
{
    comp_handler = handler;
}

void rdma_backend_unregister_comp_handler(void)
{
    rdma_backend_register_comp_handler(dummy_comp_handler);
}

int rdma_backend_query_port(RdmaBackendDev *backend_dev,
                            struct ibv_port_attr *port_attr)
{
    int rc;

    rc = ibv_query_port(backend_dev->context, backend_dev->port_num, port_attr);
    if (rc) {
        pr_dbg("Error %d from ibv_query_port\n", rc);
        return -EIO;
    }

    return 0;
}

void rdma_backend_poll_cq(RdmaDeviceResources *rdma_dev_res, RdmaBackendCQ *cq)
{
    poll_cq(rdma_dev_res, cq->ibcq);
}

static GHashTable *ah_hash;

static struct ibv_ah *create_ah(RdmaBackendDev *backend_dev, struct ibv_pd *pd,
                                uint8_t sgid_idx, union ibv_gid *dgid)
{
    GBytes *ah_key = g_bytes_new(dgid, sizeof(*dgid));
    struct ibv_ah *ah = g_hash_table_lookup(ah_hash, ah_key);

    if (ah) {
        trace_create_ah_cache_hit(be64_to_cpu(dgid->global.subnet_prefix),
                                  be64_to_cpu(dgid->global.interface_id));
        g_bytes_unref(ah_key);
    } else {
        struct ibv_ah_attr ah_attr = {
            .is_global     = 1,
            .port_num      = backend_dev->port_num,
            .grh.hop_limit = 1,
        };

        ah_attr.grh.dgid = *dgid;
        ah_attr.grh.sgid_index = sgid_idx;

        ah = ibv_create_ah(pd, &ah_attr);
        if (ah) {
            g_hash_table_insert(ah_hash, ah_key, ah);
        } else {
            g_bytes_unref(ah_key);
            pr_dbg("Fail to create AH for gid <0x%" PRIx64 ", 0x%" PRIx64 ">\n",
                    be64_to_cpu(dgid->global.subnet_prefix),
                    be64_to_cpu(dgid->global.interface_id));
        }

        trace_create_ah_cache_miss(be64_to_cpu(dgid->global.subnet_prefix),
                                   be64_to_cpu(dgid->global.interface_id));
    }

    return ah;
}

static void destroy_ah_hash_key(gpointer data)
{
    g_bytes_unref(data);
}

static void destroy_ah_hast_data(gpointer data)
{
    struct ibv_ah *ah = data;

    ibv_destroy_ah(ah);
}

static void ah_cache_init(void)
{
    ah_hash = g_hash_table_new_full(g_bytes_hash, g_bytes_equal,
                                    destroy_ah_hash_key, destroy_ah_hast_data);
}

static int build_host_sge_array(RdmaDeviceResources *rdma_dev_res,
                                struct ibv_sge *dsge, struct ibv_sge *ssge,
                                uint8_t num_sge)
{
    RdmaRmMR *mr;
    int ssge_idx;

    pr_dbg("num_sge=%d\n", num_sge);

    for (ssge_idx = 0; ssge_idx < num_sge; ssge_idx++) {
        mr = rdma_rm_get_mr(rdma_dev_res, ssge[ssge_idx].lkey);
        if (unlikely(!mr)) {
            pr_dbg("Invalid lkey 0x%x\n", ssge[ssge_idx].lkey);
            return VENDOR_ERR_INVLKEY | ssge[ssge_idx].lkey;
        }

        dsge->addr = (uintptr_t)mr->virt + ssge[ssge_idx].addr - mr->start;
        dsge->length = ssge[ssge_idx].length;
        dsge->lkey = rdma_backend_mr_lkey(&mr->backend_mr);

        pr_dbg("ssge->addr=0x%" PRIx64 "\n", ssge[ssge_idx].addr);
        pr_dbg("dsge->addr=0x%" PRIx64 "\n", dsge->addr);
        pr_dbg("dsge->length=%d\n", dsge->length);
        pr_dbg("dsge->lkey=0x%x\n", dsge->lkey);

        dsge++;
    }

    return 0;
}

static int mad_send(RdmaBackendDev *backend_dev, uint8_t sgid_idx,
                    union ibv_gid *sgid, struct ibv_sge *sge, uint32_t num_sge)
{
    RdmaCmMuxMsg msg = {};
    char *hdr, *data;
    int ret;

    pr_dbg("num_sge=%d\n", num_sge);

    if (num_sge != 2) {
        return -EINVAL;
    }

    msg.hdr.op_code = RDMACM_MUX_OP_CODE_MAD;
    memcpy(msg.hdr.sgid.raw, sgid->raw, sizeof(msg.hdr.sgid));

    msg.umad_len = sge[0].length + sge[1].length;
    pr_dbg("umad_len=%d\n", msg.umad_len);

    if (msg.umad_len > sizeof(msg.umad.mad)) {
        return -ENOMEM;
    }

    msg.umad.hdr.addr.qpn = htobe32(1);
    msg.umad.hdr.addr.grh_present = 1;
    pr_dbg("sgid_idx=%d\n", sgid_idx);
    pr_dbg("sgid=0x%llx\n", sgid->global.interface_id);
    msg.umad.hdr.addr.gid_index = sgid_idx;
    memcpy(msg.umad.hdr.addr.gid, sgid->raw, sizeof(msg.umad.hdr.addr.gid));
    msg.umad.hdr.addr.hop_limit = 0xFF;

    hdr = rdma_pci_dma_map(backend_dev->dev, sge[0].addr, sge[0].length);
    if (!hdr) {
        pr_dbg("Fail to map to sge[0]\n");
        return -ENOMEM;
    }
    data = rdma_pci_dma_map(backend_dev->dev, sge[1].addr, sge[1].length);
    if (!data) {
        pr_dbg("Fail to map to sge[1]\n");
        rdma_pci_dma_unmap(backend_dev->dev, hdr, sge[0].length);
        return -ENOMEM;
    }

    pr_dbg_buf("mad_hdr", hdr, sge[0].length);
    pr_dbg_buf("mad_data", data, sge[1].length);

    memcpy(&msg.umad.mad[0], hdr, sge[0].length);
    memcpy(&msg.umad.mad[sge[0].length], data, sge[1].length);

    rdma_pci_dma_unmap(backend_dev->dev, data, sge[1].length);
    rdma_pci_dma_unmap(backend_dev->dev, hdr, sge[0].length);

    ret = exec_rdmacm_mux_req(backend_dev, &msg);
    if (ret) {
        pr_dbg("Fail to send MAD to rdma_umadmux (%d)\n", ret);
        return -EIO;
    }

    return 0;
}

void rdma_backend_post_send(RdmaBackendDev *backend_dev,
                            RdmaBackendQP *qp, uint8_t qp_type,
                            struct ibv_sge *sge, uint32_t num_sge,
                            uint8_t sgid_idx, union ibv_gid *sgid,
                            union ibv_gid *dgid, uint32_t dqpn, uint32_t dqkey,
                            void *ctx)
{
    BackendCtx *bctx;
    struct ibv_sge new_sge[MAX_SGE];
    uint32_t bctx_id;
    int rc;
    struct ibv_send_wr wr = {0}, *bad_wr;

    if (!qp->ibqp) { /* This field does not get initialized for QP0 and QP1 */
        if (qp_type == IBV_QPT_SMI) {
            pr_dbg("QP0 unsupported\n");
            complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_QP0, ctx);
        } else if (qp_type == IBV_QPT_GSI) {
            pr_dbg("QP1\n");
            rc = mad_send(backend_dev, sgid_idx, sgid, sge, num_sge);
            if (rc) {
                complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_MAD_SEND, ctx);
            } else {
                complete_work(IBV_WC_SUCCESS, 0, ctx);
            }
        }
        return;
    }

    pr_dbg("num_sge=%d\n", num_sge);

    bctx = g_malloc0(sizeof(*bctx));
    bctx->up_ctx = ctx;
    bctx->is_tx_req = 1;

    rc = rdma_rm_alloc_cqe_ctx(backend_dev->rdma_dev_res, &bctx_id, bctx);
    if (unlikely(rc)) {
        pr_dbg("Failed to allocate cqe_ctx\n");
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
        goto out_free_bctx;
    }

    rc = build_host_sge_array(backend_dev->rdma_dev_res, new_sge, sge, num_sge);
    if (rc) {
        pr_dbg("Error: Failed to build host SGE array\n");
        complete_work(IBV_WC_GENERAL_ERR, rc, ctx);
        goto out_dealloc_cqe_ctx;
    }

    if (qp_type == IBV_QPT_UD) {
        wr.wr.ud.ah = create_ah(backend_dev, qp->ibpd, sgid_idx, dgid);
        if (!wr.wr.ud.ah) {
            complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_FAIL_BACKEND, ctx);
            goto out_dealloc_cqe_ctx;
        }
        wr.wr.ud.remote_qpn = dqpn;
        wr.wr.ud.remote_qkey = dqkey;
    }

    wr.num_sge = num_sge;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.sg_list = new_sge;
    wr.wr_id = bctx_id;

    rc = ibv_post_send(qp->ibqp, &wr, &bad_wr);
    pr_dbg("ibv_post_send=%d\n", rc);
    if (rc) {
        pr_dbg("Fail (%d, %d) to post send WQE to qpn %d\n", rc, errno,
                qp->ibqp->qp_num);
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_FAIL_BACKEND, ctx);
        goto out_dealloc_cqe_ctx;
    }

    return;

out_dealloc_cqe_ctx:
    rdma_rm_dealloc_cqe_ctx(backend_dev->rdma_dev_res, bctx_id);

out_free_bctx:
    g_free(bctx);
}

static unsigned int save_mad_recv_buffer(RdmaBackendDev *backend_dev,
                                         struct ibv_sge *sge, uint32_t num_sge,
                                         void *ctx)
{
    BackendCtx *bctx;
    int rc;
    uint32_t bctx_id;

    if (num_sge != 1) {
        pr_dbg("Invalid num_sge (%d), expecting 1\n", num_sge);
        return VENDOR_ERR_INV_NUM_SGE;
    }

    if (sge[0].length < RDMA_MAX_PRIVATE_DATA + sizeof(struct ibv_grh)) {
        pr_dbg("Too small buffer for MAD\n");
        return VENDOR_ERR_INV_MAD_BUFF;
    }

    pr_dbg("addr=0x%" PRIx64"\n", sge[0].addr);
    pr_dbg("length=%d\n", sge[0].length);
    pr_dbg("lkey=%d\n", sge[0].lkey);

    bctx = g_malloc0(sizeof(*bctx));

    rc = rdma_rm_alloc_cqe_ctx(backend_dev->rdma_dev_res, &bctx_id, bctx);
    if (unlikely(rc)) {
        g_free(bctx);
        pr_dbg("Fail to allocate cqe_ctx\n");
        return VENDOR_ERR_NOMEM;
    }

    pr_dbg("bctx_id %d, bctx %p, ctx %p\n", bctx_id, bctx, ctx);
    bctx->up_ctx = ctx;
    bctx->sge = *sge;

    qemu_mutex_lock(&backend_dev->recv_mads_list.lock);
    qlist_append_int(backend_dev->recv_mads_list.list, bctx_id);
    qemu_mutex_unlock(&backend_dev->recv_mads_list.lock);

    return 0;
}

void rdma_backend_post_recv(RdmaBackendDev *backend_dev,
                            RdmaDeviceResources *rdma_dev_res,
                            RdmaBackendQP *qp, uint8_t qp_type,
                            struct ibv_sge *sge, uint32_t num_sge, void *ctx)
{
    BackendCtx *bctx;
    struct ibv_sge new_sge[MAX_SGE];
    uint32_t bctx_id;
    int rc;
    struct ibv_recv_wr wr = {0}, *bad_wr;

    if (!qp->ibqp) { /* This field does not get initialized for QP0 and QP1 */
        if (qp_type == IBV_QPT_SMI) {
            pr_dbg("QP0 unsupported\n");
            complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_QP0, ctx);
        }
        if (qp_type == IBV_QPT_GSI) {
            pr_dbg("QP1\n");
            rc = save_mad_recv_buffer(backend_dev, sge, num_sge, ctx);
            if (rc) {
                complete_work(IBV_WC_GENERAL_ERR, rc, ctx);
            }
        }
        return;
    }

    pr_dbg("num_sge=%d\n", num_sge);

    bctx = g_malloc0(sizeof(*bctx));
    bctx->up_ctx = ctx;
    bctx->is_tx_req = 0;

    rc = rdma_rm_alloc_cqe_ctx(rdma_dev_res, &bctx_id, bctx);
    if (unlikely(rc)) {
        pr_dbg("Failed to allocate cqe_ctx\n");
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
        goto out_free_bctx;
    }

    rc = build_host_sge_array(rdma_dev_res, new_sge, sge, num_sge);
    if (rc) {
        pr_dbg("Error: Failed to build host SGE array\n");
        complete_work(IBV_WC_GENERAL_ERR, rc, ctx);
        goto out_dealloc_cqe_ctx;
    }

    wr.num_sge = num_sge;
    wr.sg_list = new_sge;
    wr.wr_id = bctx_id;
    rc = ibv_post_recv(qp->ibqp, &wr, &bad_wr);
    pr_dbg("ibv_post_recv=%d\n", rc);
    if (rc) {
        pr_dbg("Fail (%d, %d) to post recv WQE to qpn %d\n", rc, errno,
                qp->ibqp->qp_num);
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_FAIL_BACKEND, ctx);
        goto out_dealloc_cqe_ctx;
    }

    return;

out_dealloc_cqe_ctx:
    rdma_rm_dealloc_cqe_ctx(rdma_dev_res, bctx_id);

out_free_bctx:
    g_free(bctx);
}

int rdma_backend_create_pd(RdmaBackendDev *backend_dev, RdmaBackendPD *pd)
{
    pd->ibpd = ibv_alloc_pd(backend_dev->context);

    return pd->ibpd ? 0 : -EIO;
}

void rdma_backend_destroy_pd(RdmaBackendPD *pd)
{
    if (pd->ibpd) {
        ibv_dealloc_pd(pd->ibpd);
    }
}

int rdma_backend_create_mr(RdmaBackendMR *mr, RdmaBackendPD *pd, void *addr,
                           size_t length, int access)
{
    pr_dbg("addr=0x%p\n", addr);
    pr_dbg("len=%zu\n", length);
    mr->ibmr = ibv_reg_mr(pd->ibpd, addr, length, access);
    if (mr->ibmr) {
        pr_dbg("lkey=0x%x\n", mr->ibmr->lkey);
        pr_dbg("rkey=0x%x\n", mr->ibmr->rkey);
        mr->ibpd = pd->ibpd;
    }

    return mr->ibmr ? 0 : -EIO;
}

void rdma_backend_destroy_mr(RdmaBackendMR *mr)
{
    if (mr->ibmr) {
        ibv_dereg_mr(mr->ibmr);
    }
}

int rdma_backend_create_cq(RdmaBackendDev *backend_dev, RdmaBackendCQ *cq,
                           int cqe)
{
    int rc;

    pr_dbg("cqe=%d\n", cqe);

    pr_dbg("dev->channel=%p\n", backend_dev->channel);
    cq->ibcq = ibv_create_cq(backend_dev->context, cqe + 1, NULL,
                             backend_dev->channel, 0);

    if (cq->ibcq) {
        rc = ibv_req_notify_cq(cq->ibcq, 0);
        if (rc) {
            pr_dbg("Error %d from ibv_req_notify_cq\n", rc);
        }
        cq->backend_dev = backend_dev;
    }

    return cq->ibcq ? 0 : -EIO;
}

void rdma_backend_destroy_cq(RdmaBackendCQ *cq)
{
    if (cq->ibcq) {
        ibv_destroy_cq(cq->ibcq);
    }
}

int rdma_backend_create_qp(RdmaBackendQP *qp, uint8_t qp_type,
                           RdmaBackendPD *pd, RdmaBackendCQ *scq,
                           RdmaBackendCQ *rcq, uint32_t max_send_wr,
                           uint32_t max_recv_wr, uint32_t max_send_sge,
                           uint32_t max_recv_sge)
{
    struct ibv_qp_init_attr attr = {0};

    qp->ibqp = 0;
    pr_dbg("qp_type=%d\n", qp_type);

    switch (qp_type) {
    case IBV_QPT_GSI:
        return 0;

    case IBV_QPT_RC:
        /* fall through */
    case IBV_QPT_UD:
        /* do nothing */
        break;

    default:
        pr_dbg("Unsupported QP type %d\n", qp_type);
        return -EIO;
    }

    attr.qp_type = qp_type;
    attr.send_cq = scq->ibcq;
    attr.recv_cq = rcq->ibcq;
    attr.cap.max_send_wr = max_send_wr;
    attr.cap.max_recv_wr = max_recv_wr;
    attr.cap.max_send_sge = max_send_sge;
    attr.cap.max_recv_sge = max_recv_sge;

    pr_dbg("max_send_wr=%d\n", max_send_wr);
    pr_dbg("max_recv_wr=%d\n", max_recv_wr);
    pr_dbg("max_send_sge=%d\n", max_send_sge);
    pr_dbg("max_recv_sge=%d\n", max_recv_sge);

    qp->ibqp = ibv_create_qp(pd->ibpd, &attr);
    if (likely(!qp->ibqp)) {
        pr_dbg("Error from ibv_create_qp\n");
        return -EIO;
    }

    qp->ibpd = pd->ibpd;

    /* TODO: Query QP to get max_inline_data and save it to be used in send */

    pr_dbg("qpn=0x%x\n", qp->ibqp->qp_num);

    return 0;
}

int rdma_backend_qp_state_init(RdmaBackendDev *backend_dev, RdmaBackendQP *qp,
                               uint8_t qp_type, uint32_t qkey)
{
    struct ibv_qp_attr attr = {0};
    int rc, attr_mask;

    pr_dbg("qpn=0x%x\n", qp->ibqp->qp_num);
    pr_dbg("sport_num=%d\n", backend_dev->port_num);

    attr_mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT;
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = backend_dev->port_num;

    switch (qp_type) {
    case IBV_QPT_RC:
        attr_mask |= IBV_QP_ACCESS_FLAGS;
        break;

    case IBV_QPT_UD:
        attr.qkey = qkey;
        attr_mask |= IBV_QP_QKEY;
        break;

    default:
        pr_dbg("Unsupported QP type %d\n", qp_type);
        return -EIO;
    }

    rc = ibv_modify_qp(qp->ibqp, &attr, attr_mask);
    if (rc) {
        pr_dbg("Error %d from ibv_modify_qp\n", rc);
        return -EIO;
    }

    return 0;
}

int rdma_backend_qp_state_rtr(RdmaBackendDev *backend_dev, RdmaBackendQP *qp,
                              uint8_t qp_type, uint8_t sgid_idx,
                              union ibv_gid *dgid, uint32_t dqpn,
                              uint32_t rq_psn, uint32_t qkey, bool use_qkey)
{
    struct ibv_qp_attr attr = {0};
    union ibv_gid ibv_gid = {
        .global.interface_id = dgid->global.interface_id,
        .global.subnet_prefix = dgid->global.subnet_prefix
    };
    int rc, attr_mask;

    attr.qp_state = IBV_QPS_RTR;
    attr_mask = IBV_QP_STATE;

    qp->sgid_idx = sgid_idx;

    switch (qp_type) {
    case IBV_QPT_RC:
        pr_dbg("dgid=0x%" PRIx64 ",%" PRIx64 "\n",
               be64_to_cpu(ibv_gid.global.subnet_prefix),
               be64_to_cpu(ibv_gid.global.interface_id));
        pr_dbg("dqpn=0x%x\n", dqpn);
        pr_dbg("sgid_idx=%d\n", qp->sgid_idx);
        pr_dbg("sport_num=%d\n", backend_dev->port_num);
        pr_dbg("rq_psn=0x%x\n", rq_psn);

        attr.path_mtu               = IBV_MTU_1024;
        attr.dest_qp_num            = dqpn;
        attr.max_dest_rd_atomic     = 1;
        attr.min_rnr_timer          = 12;
        attr.ah_attr.port_num       = backend_dev->port_num;
        attr.ah_attr.is_global      = 1;
        attr.ah_attr.grh.hop_limit  = 1;
        attr.ah_attr.grh.dgid       = ibv_gid;
        attr.ah_attr.grh.sgid_index = qp->sgid_idx;
        attr.rq_psn                 = rq_psn;

        attr_mask |= IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                     IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                     IBV_QP_MIN_RNR_TIMER;
        break;

    case IBV_QPT_UD:
        pr_dbg("qkey=0x%x\n", qkey);
        if (use_qkey) {
            attr.qkey = qkey;
            attr_mask |= IBV_QP_QKEY;
        }
        break;
    }

    rc = ibv_modify_qp(qp->ibqp, &attr, attr_mask);
    if (rc) {
        pr_dbg("Error %d from ibv_modify_qp\n", rc);
        return -EIO;
    }

    return 0;
}

int rdma_backend_qp_state_rts(RdmaBackendQP *qp, uint8_t qp_type,
                              uint32_t sq_psn, uint32_t qkey, bool use_qkey)
{
    struct ibv_qp_attr attr = {0};
    int rc, attr_mask;

    pr_dbg("qpn=0x%x\n", qp->ibqp->qp_num);
    pr_dbg("sq_psn=0x%x\n", sq_psn);

    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = sq_psn;
    attr_mask = IBV_QP_STATE | IBV_QP_SQ_PSN;

    switch (qp_type) {
    case IBV_QPT_RC:
        attr.timeout       = 14;
        attr.retry_cnt     = 7;
        attr.rnr_retry     = 7;
        attr.max_rd_atomic = 1;

        attr_mask |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                     IBV_QP_MAX_QP_RD_ATOMIC;
        break;

    case IBV_QPT_UD:
        if (use_qkey) {
            pr_dbg("qkey=0x%x\n", qkey);
            attr.qkey = qkey;
            attr_mask |= IBV_QP_QKEY;
        }
        break;
    }

    rc = ibv_modify_qp(qp->ibqp, &attr, attr_mask);
    if (rc) {
        pr_dbg("Error %d from ibv_modify_qp\n", rc);
        return -EIO;
    }

    return 0;
}

int rdma_backend_query_qp(RdmaBackendQP *qp, struct ibv_qp_attr *attr,
                          int attr_mask, struct ibv_qp_init_attr *init_attr)
{
    if (!qp->ibqp) {
        pr_dbg("QP1\n");
        attr->qp_state = IBV_QPS_RTS;
        return 0;
    }

    return ibv_query_qp(qp->ibqp, attr, attr_mask, init_attr);
}

void rdma_backend_destroy_qp(RdmaBackendQP *qp)
{
    if (qp->ibqp) {
        ibv_destroy_qp(qp->ibqp);
    }
}

#define CHK_ATTR(req, dev, member, fmt) ({ \
    pr_dbg("%s="fmt","fmt"\n", #member, dev.member, req->member); \
    if (req->member > dev.member) { \
        warn_report("%s = "fmt" is higher than host device capability "fmt, \
                    #member, req->member, dev.member); \
        req->member = dev.member; \
    } \
    pr_dbg("%s="fmt"\n", #member, req->member); })

static int init_device_caps(RdmaBackendDev *backend_dev,
                            struct ibv_device_attr *dev_attr)
{
    struct ibv_device_attr bk_dev_attr;

    if (ibv_query_device(backend_dev->context, &bk_dev_attr)) {
        return -EIO;
    }

    dev_attr->max_sge = MAX_SGE;

    CHK_ATTR(dev_attr, bk_dev_attr, max_mr_size, "%" PRId64);
    CHK_ATTR(dev_attr, bk_dev_attr, max_qp, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_sge, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_qp_wr, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_cq, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_cqe, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_mr, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_pd, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_qp_rd_atom, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_qp_init_rd_atom, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_ah, "%d");

    return 0;
}

static inline void build_mad_hdr(struct ibv_grh *grh, union ibv_gid *sgid,
                                 union ibv_gid *my_gid, int paylen)
{
    grh->paylen = htons(paylen);
    grh->sgid = *sgid;
    grh->dgid = *my_gid;

    pr_dbg("paylen=%d (net=0x%x)\n", paylen, grh->paylen);
    pr_dbg("dgid=0x%llx\n", my_gid->global.interface_id);
    pr_dbg("sgid=0x%llx\n", sgid->global.interface_id);
}

static void process_incoming_mad_req(RdmaBackendDev *backend_dev,
                                     RdmaCmMuxMsg *msg)
{
    QObject *o_ctx_id;
    unsigned long cqe_ctx_id;
    BackendCtx *bctx;
    char *mad;

    pr_dbg("umad_len=%d\n", msg->umad_len);

#ifdef PVRDMA_DEBUG
    struct umad_hdr *hdr = (struct umad_hdr *)&msg->umad.mad;
    pr_dbg("bv %x cls %x cv %x mtd %x st %d tid %" PRIx64 " at %x atm %x\n",
           hdr->base_version, hdr->mgmt_class, hdr->class_version,
           hdr->method, hdr->status, be64toh(hdr->tid),
           hdr->attr_id, hdr->attr_mod);
#endif

    qemu_mutex_lock(&backend_dev->recv_mads_list.lock);
    o_ctx_id = qlist_pop(backend_dev->recv_mads_list.list);
    qemu_mutex_unlock(&backend_dev->recv_mads_list.lock);
    if (!o_ctx_id) {
        pr_dbg("No more free MADs buffers, waiting for a while\n");
        sleep(THR_POLL_TO);
        return;
    }

    cqe_ctx_id = qnum_get_uint(qobject_to(QNum, o_ctx_id));
    bctx = rdma_rm_get_cqe_ctx(backend_dev->rdma_dev_res, cqe_ctx_id);
    if (unlikely(!bctx)) {
        pr_dbg("Error: Fail to find ctx for %ld\n", cqe_ctx_id);
        return;
    }

    pr_dbg("id %ld, bctx %p, ctx %p\n", cqe_ctx_id, bctx, bctx->up_ctx);

    mad = rdma_pci_dma_map(backend_dev->dev, bctx->sge.addr,
                           bctx->sge.length);
    if (!mad || bctx->sge.length < msg->umad_len + MAD_HDR_SIZE) {
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_INV_MAD_BUFF,
                      bctx->up_ctx);
    } else {
        struct ibv_wc wc = {0};
        pr_dbg_buf("mad", msg->umad.mad, msg->umad_len);
        memset(mad, 0, bctx->sge.length);
        build_mad_hdr((struct ibv_grh *)mad,
                      (union ibv_gid *)&msg->umad.hdr.addr.gid, &msg->hdr.sgid,
                      msg->umad_len);
        memcpy(&mad[MAD_HDR_SIZE], msg->umad.mad, msg->umad_len);
        rdma_pci_dma_unmap(backend_dev->dev, mad, bctx->sge.length);

        wc.byte_len = msg->umad_len;
        wc.status = IBV_WC_SUCCESS;
        wc.wc_flags = IBV_WC_GRH;
        comp_handler(bctx->up_ctx, &wc);
    }

    g_free(bctx);
    rdma_rm_dealloc_cqe_ctx(backend_dev->rdma_dev_res, cqe_ctx_id);
}

static inline int rdmacm_mux_can_receive(void *opaque)
{
    RdmaBackendDev *backend_dev = (RdmaBackendDev *)opaque;

    return rdmacm_mux_can_process_async(backend_dev);
}

static void rdmacm_mux_read(void *opaque, const uint8_t *buf, int size)
{
    RdmaBackendDev *backend_dev = (RdmaBackendDev *)opaque;
    RdmaCmMuxMsg *msg = (RdmaCmMuxMsg *)buf;

    pr_dbg("Got %d bytes\n", size);
    pr_dbg("msg_type=%d\n", msg->hdr.msg_type);
    pr_dbg("op_code=%d\n", msg->hdr.op_code);

    if (msg->hdr.msg_type != RDMACM_MUX_MSG_TYPE_REQ &&
        msg->hdr.op_code != RDMACM_MUX_OP_CODE_MAD) {
            pr_dbg("Error: Not a MAD request, skipping\n");
            return;
    }
    process_incoming_mad_req(backend_dev, msg);
}

static int mad_init(RdmaBackendDev *backend_dev, CharBackend *mad_chr_be)
{
    int ret;

    backend_dev->rdmacm_mux.chr_be = mad_chr_be;

    ret = qemu_chr_fe_backend_connected(backend_dev->rdmacm_mux.chr_be);
    if (!ret) {
        pr_dbg("Missing chardev for MAD multiplexer\n");
        return -EIO;
    }

    qemu_mutex_init(&backend_dev->recv_mads_list.lock);
    backend_dev->recv_mads_list.list = qlist_new();

    enable_rdmacm_mux_async(backend_dev);

    qemu_chr_fe_set_handlers(backend_dev->rdmacm_mux.chr_be,
                             rdmacm_mux_can_receive, rdmacm_mux_read, NULL,
                             NULL, backend_dev, NULL, true);

    return 0;
}

static void mad_fini(RdmaBackendDev *backend_dev)
{
    pr_dbg("Stopping MAD\n");
    disable_rdmacm_mux_async(backend_dev);
    qemu_chr_fe_disconnect(backend_dev->rdmacm_mux.chr_be);
    if (backend_dev->recv_mads_list.list) {
        qlist_destroy_obj(QOBJECT(backend_dev->recv_mads_list.list));
        qemu_mutex_destroy(&backend_dev->recv_mads_list.lock);
    }
}

int rdma_backend_get_gid_index(RdmaBackendDev *backend_dev,
                               union ibv_gid *gid)
{
    union ibv_gid sgid;
    int ret;
    int i = 0;

    pr_dbg("0x%llx, 0x%llx\n",
           (long long unsigned int)be64_to_cpu(gid->global.subnet_prefix),
           (long long unsigned int)be64_to_cpu(gid->global.interface_id));

    do {
        ret = ibv_query_gid(backend_dev->context, backend_dev->port_num, i,
                            &sgid);
        i++;
    } while (!ret && (memcmp(&sgid, gid, sizeof(*gid))));

    pr_dbg("gid_index=%d\n", i - 1);

    return ret ? ret : i - 1;
}

int rdma_backend_add_gid(RdmaBackendDev *backend_dev, const char *ifname,
                         union ibv_gid *gid)
{
    RdmaCmMuxMsg msg = {};
    int ret;

    pr_dbg("0x%llx, 0x%llx\n",
           (long long unsigned int)be64_to_cpu(gid->global.subnet_prefix),
           (long long unsigned int)be64_to_cpu(gid->global.interface_id));

    msg.hdr.op_code = RDMACM_MUX_OP_CODE_REG;
    memcpy(msg.hdr.sgid.raw, gid->raw, sizeof(msg.hdr.sgid));

    ret = exec_rdmacm_mux_req(backend_dev, &msg);
    if (ret) {
        pr_dbg("Fail to register GID to rdma_umadmux (%d)\n", ret);
        return -EIO;
    }

    qapi_event_send_rdma_gid_status_changed(ifname, true,
                                            gid->global.subnet_prefix,
                                            gid->global.interface_id);

    return ret;
}

int rdma_backend_del_gid(RdmaBackendDev *backend_dev, const char *ifname,
                         union ibv_gid *gid)
{
    RdmaCmMuxMsg msg = {};
    int ret;

    pr_dbg("0x%llx, 0x%llx\n",
           (long long unsigned int)be64_to_cpu(gid->global.subnet_prefix),
           (long long unsigned int)be64_to_cpu(gid->global.interface_id));

    msg.hdr.op_code = RDMACM_MUX_OP_CODE_UNREG;
    memcpy(msg.hdr.sgid.raw, gid->raw, sizeof(msg.hdr.sgid));

    ret = exec_rdmacm_mux_req(backend_dev, &msg);
    if (ret) {
        pr_dbg("Fail to unregister GID from rdma_umadmux (%d)\n", ret);
        return -EIO;
    }

    qapi_event_send_rdma_gid_status_changed(ifname, false,
                                            gid->global.subnet_prefix,
                                            gid->global.interface_id);

    return 0;
}

int rdma_backend_init(RdmaBackendDev *backend_dev, PCIDevice *pdev,
                      RdmaDeviceResources *rdma_dev_res,
                      const char *backend_device_name, uint8_t port_num,
                      struct ibv_device_attr *dev_attr, CharBackend *mad_chr_be,
                      Error **errp)
{
    int i;
    int ret = 0;
    int num_ibv_devices;
    struct ibv_device **dev_list;

    memset(backend_dev, 0, sizeof(*backend_dev));

    backend_dev->dev = pdev;
    backend_dev->port_num = port_num;
    backend_dev->rdma_dev_res = rdma_dev_res;

    rdma_backend_register_comp_handler(dummy_comp_handler);

    dev_list = ibv_get_device_list(&num_ibv_devices);
    if (!dev_list) {
        error_setg(errp, "Failed to get IB devices list");
        return -EIO;
    }

    if (num_ibv_devices == 0) {
        error_setg(errp, "No IB devices were found");
        ret = -ENXIO;
        goto out_free_dev_list;
    }

    if (backend_device_name) {
        for (i = 0; dev_list[i]; ++i) {
            if (!strcmp(ibv_get_device_name(dev_list[i]),
                        backend_device_name)) {
                break;
            }
        }

        backend_dev->ib_dev = dev_list[i];
        if (!backend_dev->ib_dev) {
            error_setg(errp, "Failed to find IB device %s",
                       backend_device_name);
            ret = -EIO;
            goto out_free_dev_list;
        }
    } else {
        backend_dev->ib_dev = *dev_list;
    }

    pr_dbg("Using backend device %s, port %d\n",
           ibv_get_device_name(backend_dev->ib_dev), backend_dev->port_num);
    pr_dbg("uverb device %s\n", backend_dev->ib_dev->dev_name);

    backend_dev->context = ibv_open_device(backend_dev->ib_dev);
    if (!backend_dev->context) {
        error_setg(errp, "Failed to open IB device");
        ret = -EIO;
        goto out;
    }

    backend_dev->channel = ibv_create_comp_channel(backend_dev->context);
    if (!backend_dev->channel) {
        error_setg(errp, "Failed to create IB communication channel");
        ret = -EIO;
        goto out_close_device;
    }
    pr_dbg("dev->backend_dev.channel=%p\n", backend_dev->channel);

    ret = init_device_caps(backend_dev, dev_attr);
    if (ret) {
        error_setg(errp, "Failed to initialize device capabilities");
        ret = -EIO;
        goto out_destroy_comm_channel;
    }


    ret = mad_init(backend_dev, mad_chr_be);
    if (ret) {
        error_setg(errp, "Fail to initialize mad");
        ret = -EIO;
        goto out_destroy_comm_channel;
    }

    backend_dev->comp_thread.run = false;
    backend_dev->comp_thread.is_running = false;

    ah_cache_init();

    goto out_free_dev_list;

out_destroy_comm_channel:
    ibv_destroy_comp_channel(backend_dev->channel);

out_close_device:
    ibv_close_device(backend_dev->context);

out_free_dev_list:
    ibv_free_device_list(dev_list);

out:
    return ret;
}


void rdma_backend_start(RdmaBackendDev *backend_dev)
{
    pr_dbg("Starting rdma_backend\n");
    start_comp_thread(backend_dev);
}

void rdma_backend_stop(RdmaBackendDev *backend_dev)
{
    pr_dbg("Stopping rdma_backend\n");
    stop_backend_thread(&backend_dev->comp_thread);
}

void rdma_backend_fini(RdmaBackendDev *backend_dev)
{
    rdma_backend_stop(backend_dev);
    mad_fini(backend_dev);
    g_hash_table_destroy(ah_hash);
    ibv_destroy_comp_channel(backend_dev->channel);
    ibv_close_device(backend_dev->context);
}
