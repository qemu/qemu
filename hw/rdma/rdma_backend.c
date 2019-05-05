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
#include "qapi/qapi-events-rdma.h"

#include <infiniband/verbs.h>

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
    struct ibv_sge sge; /* Used to save MAD recv buffer */
    RdmaBackendQP *backend_qp; /* To maintain recv buffers */
    RdmaBackendSRQ *backend_srq;
} BackendCtx;

struct backend_umad {
    struct ib_user_mad hdr;
    char mad[RDMA_MAX_PRIVATE_DATA];
};

static void (*comp_handler)(void *ctx, struct ibv_wc *wc);

static void dummy_comp_handler(void *ctx, struct ibv_wc *wc)
{
    rdma_error_report("No completion handler is registered");
}

static inline void complete_work(enum ibv_wc_status status, uint32_t vendor_err,
                                 void *ctx)
{
    struct ibv_wc wc = {};

    wc.status = status;
    wc.vendor_err = vendor_err;

    comp_handler(ctx, &wc);
}

static void free_cqe_ctx(gpointer data, gpointer user_data)
{
    BackendCtx *bctx;
    RdmaDeviceResources *rdma_dev_res = user_data;
    unsigned long cqe_ctx_id = GPOINTER_TO_INT(data);

    bctx = rdma_rm_get_cqe_ctx(rdma_dev_res, cqe_ctx_id);
    if (bctx) {
        rdma_rm_dealloc_cqe_ctx(rdma_dev_res, cqe_ctx_id);
        atomic_dec(&rdma_dev_res->stats.missing_cqe);
    }
    g_free(bctx);
}

static void clean_recv_mads(RdmaBackendDev *backend_dev)
{
    unsigned long cqe_ctx_id;

    do {
        cqe_ctx_id = rdma_protected_qlist_pop_int64(&backend_dev->
                                                    recv_mads_list);
        if (cqe_ctx_id != -ENOENT) {
            atomic_inc(&backend_dev->rdma_dev_res->stats.missing_cqe);
            free_cqe_ctx(GINT_TO_POINTER(cqe_ctx_id),
                         backend_dev->rdma_dev_res);
        }
    } while (cqe_ctx_id != -ENOENT);
}

static int rdma_poll_cq(RdmaDeviceResources *rdma_dev_res, struct ibv_cq *ibcq)
{
    int i, ne, total_ne = 0;
    BackendCtx *bctx;
    struct ibv_wc wc[2];
    RdmaProtectedGSList *cqe_ctx_list;

    qemu_mutex_lock(&rdma_dev_res->lock);
    do {
        ne = ibv_poll_cq(ibcq, ARRAY_SIZE(wc), wc);

        trace_rdma_poll_cq(ne, ibcq);

        for (i = 0; i < ne; i++) {
            bctx = rdma_rm_get_cqe_ctx(rdma_dev_res, wc[i].wr_id);
            if (unlikely(!bctx)) {
                rdma_error_report("No matching ctx for req %"PRId64,
                                  wc[i].wr_id);
                continue;
            }

            comp_handler(bctx->up_ctx, &wc[i]);

            if (bctx->backend_qp) {
                cqe_ctx_list = &bctx->backend_qp->cqe_ctx_list;
            } else {
                cqe_ctx_list = &bctx->backend_srq->cqe_ctx_list;
            }

            rdma_protected_gslist_remove_int32(cqe_ctx_list, wc[i].wr_id);
            rdma_rm_dealloc_cqe_ctx(rdma_dev_res, wc[i].wr_id);
            g_free(bctx);
        }
        total_ne += ne;
    } while (ne > 0);
    atomic_sub(&rdma_dev_res->stats.missing_cqe, total_ne);
    qemu_mutex_unlock(&rdma_dev_res->lock);

    if (ne < 0) {
        rdma_error_report("ibv_poll_cq fail, rc=%d, errno=%d", ne, errno);
    }

    rdma_dev_res->stats.completions += total_ne;

    return total_ne;
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
        rdma_error_report("Failed to change backend channel FD to non-blocking");
        return NULL;
    }

    pfds[0].fd = backend_dev->channel->fd;
    pfds[0].events = G_IO_IN | G_IO_HUP | G_IO_ERR;

    backend_dev->comp_thread.is_running = true;

    while (backend_dev->comp_thread.run) {
        do {
            rc = qemu_poll_ns(pfds, 1, THR_POLL_TO * (int64_t)SCALE_MS);
            if (!rc) {
                backend_dev->rdma_dev_res->stats.poll_cq_ppoll_to++;
            }
        } while (!rc && backend_dev->comp_thread.run);

        if (backend_dev->comp_thread.run) {
            rc = ibv_get_cq_event(backend_dev->channel, &ev_cq, &ev_ctx);
            if (unlikely(rc)) {
                rdma_error_report("ibv_get_cq_event fail, rc=%d, errno=%d", rc,
                                  errno);
                continue;
            }

            rc = ibv_req_notify_cq(ev_cq, 0);
            if (unlikely(rc)) {
                rdma_error_report("ibv_req_notify_cq fail, rc=%d, errno=%d", rc,
                                  errno);
            }

            backend_dev->rdma_dev_res->stats.poll_cq_from_bk++;
            rdma_poll_cq(backend_dev->rdma_dev_res, ev_cq);

            ibv_ack_cq_events(ev_cq, 1);
        }
    }

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

static int rdmacm_mux_check_op_status(CharBackend *mad_chr_be)
{
    RdmaCmMuxMsg msg = {};
    int ret;

    ret = qemu_chr_fe_read_all(mad_chr_be, (uint8_t *)&msg, sizeof(msg));
    if (ret != sizeof(msg)) {
        rdma_error_report("Got invalid message from mux: size %d, expecting %d",
                          ret, (int)sizeof(msg));
        return -EIO;
    }

    trace_rdmacm_mux_check_op_status(msg.hdr.msg_type, msg.hdr.op_code,
                                     msg.hdr.err_code);

    if (msg.hdr.msg_type != RDMACM_MUX_MSG_TYPE_RESP) {
        rdma_error_report("Got invalid message type %d", msg.hdr.msg_type);
        return -EIO;
    }

    if (msg.hdr.err_code != RDMACM_MUX_ERR_CODE_OK) {
        rdma_error_report("Operation failed in mux, error code %d",
                          msg.hdr.err_code);
        return -EIO;
    }

    return 0;
}

static int rdmacm_mux_send(RdmaBackendDev *backend_dev, RdmaCmMuxMsg *msg)
{
    int rc = 0;

    msg->hdr.msg_type = RDMACM_MUX_MSG_TYPE_REQ;
    trace_rdmacm_mux("send", msg->hdr.msg_type, msg->hdr.op_code);
    disable_rdmacm_mux_async(backend_dev);
    rc = qemu_chr_fe_write(backend_dev->rdmacm_mux.chr_be,
                           (const uint8_t *)msg, sizeof(*msg));
    if (rc != sizeof(*msg)) {
        enable_rdmacm_mux_async(backend_dev);
        rdma_error_report("Failed to send request to rdmacm_mux (rc=%d)", rc);
        return -EIO;
    }

    rc = rdmacm_mux_check_op_status(backend_dev->rdmacm_mux.chr_be);
    if (rc) {
        rdma_error_report("Failed to execute rdmacm_mux request %d (rc=%d)",
                          msg->hdr.op_code, rc);
    }

    enable_rdmacm_mux_async(backend_dev);

    return 0;
}

static void stop_backend_thread(RdmaBackendThread *thread)
{
    thread->run = false;
    while (thread->is_running) {
        sleep(THR_POLL_TO / SCALE_US / 2);
    }
}

static void start_comp_thread(RdmaBackendDev *backend_dev)
{
    char thread_name[THR_NAME_LEN] = {};

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
        rdma_error_report("ibv_query_port fail, rc=%d, errno=%d", rc, errno);
        return -EIO;
    }

    return 0;
}

void rdma_backend_poll_cq(RdmaDeviceResources *rdma_dev_res, RdmaBackendCQ *cq)
{
    int polled;

    rdma_dev_res->stats.poll_cq_from_guest++;
    polled = rdma_poll_cq(rdma_dev_res, cq->ibcq);
    if (!polled) {
        rdma_dev_res->stats.poll_cq_from_guest_empty++;
    }
}

static GHashTable *ah_hash;

static struct ibv_ah *create_ah(RdmaBackendDev *backend_dev, struct ibv_pd *pd,
                                uint8_t sgid_idx, union ibv_gid *dgid)
{
    GBytes *ah_key = g_bytes_new(dgid, sizeof(*dgid));
    struct ibv_ah *ah = g_hash_table_lookup(ah_hash, ah_key);

    if (ah) {
        trace_rdma_create_ah_cache_hit(be64_to_cpu(dgid->global.subnet_prefix),
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
            rdma_error_report("Failed to create AH for gid <0x%" PRIx64", 0x%"PRIx64">",
                              be64_to_cpu(dgid->global.subnet_prefix),
                              be64_to_cpu(dgid->global.interface_id));
        }

        trace_rdma_create_ah_cache_miss(be64_to_cpu(dgid->global.subnet_prefix),
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
                                uint8_t num_sge, uint64_t *total_length)
{
    RdmaRmMR *mr;
    int ssge_idx;

    for (ssge_idx = 0; ssge_idx < num_sge; ssge_idx++) {
        mr = rdma_rm_get_mr(rdma_dev_res, ssge[ssge_idx].lkey);
        if (unlikely(!mr)) {
            rdma_error_report("Invalid lkey 0x%x", ssge[ssge_idx].lkey);
            return VENDOR_ERR_INVLKEY | ssge[ssge_idx].lkey;
        }

        dsge->addr = (uintptr_t)mr->virt + ssge[ssge_idx].addr - mr->start;
        dsge->length = ssge[ssge_idx].length;
        dsge->lkey = rdma_backend_mr_lkey(&mr->backend_mr);

        *total_length += dsge->length;

        dsge++;
    }

    return 0;
}

static void trace_mad_message(const char *title, char *buf, int len)
{
    int i;
    char *b = g_malloc0(len * 3 + 1);
    char b1[4];

    for (i = 0; i < len; i++) {
        sprintf(b1, "%.2X ", buf[i] & 0x000000FF);
        strcat(b, b1);
    }

    trace_rdma_mad_message(title, len, b);

    g_free(b);
}

static int mad_send(RdmaBackendDev *backend_dev, uint8_t sgid_idx,
                    union ibv_gid *sgid, struct ibv_sge *sge, uint32_t num_sge)
{
    RdmaCmMuxMsg msg = {};
    char *hdr, *data;
    int ret;

    if (num_sge != 2) {
        return -EINVAL;
    }

    msg.hdr.op_code = RDMACM_MUX_OP_CODE_MAD;
    memcpy(msg.hdr.sgid.raw, sgid->raw, sizeof(msg.hdr.sgid));

    msg.umad_len = sge[0].length + sge[1].length;

    if (msg.umad_len > sizeof(msg.umad.mad)) {
        return -ENOMEM;
    }

    msg.umad.hdr.addr.qpn = htobe32(1);
    msg.umad.hdr.addr.grh_present = 1;
    msg.umad.hdr.addr.gid_index = sgid_idx;
    memcpy(msg.umad.hdr.addr.gid, sgid->raw, sizeof(msg.umad.hdr.addr.gid));
    msg.umad.hdr.addr.hop_limit = 0xFF;

    hdr = rdma_pci_dma_map(backend_dev->dev, sge[0].addr, sge[0].length);
    if (!hdr) {
        return -ENOMEM;
    }
    data = rdma_pci_dma_map(backend_dev->dev, sge[1].addr, sge[1].length);
    if (!data) {
        rdma_pci_dma_unmap(backend_dev->dev, hdr, sge[0].length);
        return -ENOMEM;
    }

    memcpy(&msg.umad.mad[0], hdr, sge[0].length);
    memcpy(&msg.umad.mad[sge[0].length], data, sge[1].length);

    rdma_pci_dma_unmap(backend_dev->dev, data, sge[1].length);
    rdma_pci_dma_unmap(backend_dev->dev, hdr, sge[0].length);

    trace_mad_message("send", msg.umad.mad, msg.umad_len);

    ret = rdmacm_mux_send(backend_dev, &msg);
    if (ret) {
        rdma_error_report("Failed to send MAD to rdma_umadmux (%d)", ret);
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
    struct ibv_send_wr wr = {}, *bad_wr;

    if (!qp->ibqp) { /* This field is not initialized for QP0 and QP1 */
        if (qp_type == IBV_QPT_SMI) {
            rdma_error_report("Got QP0 request");
            complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_QP0, ctx);
        } else if (qp_type == IBV_QPT_GSI) {
            rc = mad_send(backend_dev, sgid_idx, sgid, sge, num_sge);
            if (rc) {
                complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_MAD_SEND, ctx);
                backend_dev->rdma_dev_res->stats.mad_tx_err++;
            } else {
                complete_work(IBV_WC_SUCCESS, 0, ctx);
                backend_dev->rdma_dev_res->stats.mad_tx++;
            }
        }
        return;
    }

    bctx = g_malloc0(sizeof(*bctx));
    bctx->up_ctx = ctx;
    bctx->backend_qp = qp;

    rc = rdma_rm_alloc_cqe_ctx(backend_dev->rdma_dev_res, &bctx_id, bctx);
    if (unlikely(rc)) {
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
        goto err_free_bctx;
    }

    rdma_protected_gslist_append_int32(&qp->cqe_ctx_list, bctx_id);

    rc = build_host_sge_array(backend_dev->rdma_dev_res, new_sge, sge, num_sge,
                              &backend_dev->rdma_dev_res->stats.tx_len);
    if (rc) {
        complete_work(IBV_WC_GENERAL_ERR, rc, ctx);
        goto err_dealloc_cqe_ctx;
    }

    if (qp_type == IBV_QPT_UD) {
        wr.wr.ud.ah = create_ah(backend_dev, qp->ibpd, sgid_idx, dgid);
        if (!wr.wr.ud.ah) {
            complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_FAIL_BACKEND, ctx);
            goto err_dealloc_cqe_ctx;
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
    if (rc) {
        rdma_error_report("ibv_post_send fail, qpn=0x%x, rc=%d, errno=%d",
                          qp->ibqp->qp_num, rc, errno);
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_FAIL_BACKEND, ctx);
        goto err_dealloc_cqe_ctx;
    }

    atomic_inc(&backend_dev->rdma_dev_res->stats.missing_cqe);
    backend_dev->rdma_dev_res->stats.tx++;

    return;

err_dealloc_cqe_ctx:
    backend_dev->rdma_dev_res->stats.tx_err++;
    rdma_rm_dealloc_cqe_ctx(backend_dev->rdma_dev_res, bctx_id);

err_free_bctx:
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
        rdma_error_report("Invalid num_sge (%d), expecting 1", num_sge);
        return VENDOR_ERR_INV_NUM_SGE;
    }

    if (sge[0].length < RDMA_MAX_PRIVATE_DATA + sizeof(struct ibv_grh)) {
        rdma_error_report("Too small buffer for MAD");
        return VENDOR_ERR_INV_MAD_BUFF;
    }

    bctx = g_malloc0(sizeof(*bctx));

    rc = rdma_rm_alloc_cqe_ctx(backend_dev->rdma_dev_res, &bctx_id, bctx);
    if (unlikely(rc)) {
        g_free(bctx);
        return VENDOR_ERR_NOMEM;
    }

    bctx->up_ctx = ctx;
    bctx->sge = *sge;

    rdma_protected_qlist_append_int64(&backend_dev->recv_mads_list, bctx_id);

    return 0;
}

void rdma_backend_post_recv(RdmaBackendDev *backend_dev,
                            RdmaBackendQP *qp, uint8_t qp_type,
                            struct ibv_sge *sge, uint32_t num_sge, void *ctx)
{
    BackendCtx *bctx;
    struct ibv_sge new_sge[MAX_SGE];
    uint32_t bctx_id;
    int rc;
    struct ibv_recv_wr wr = {}, *bad_wr;

    if (!qp->ibqp) { /* This field does not get initialized for QP0 and QP1 */
        if (qp_type == IBV_QPT_SMI) {
            rdma_error_report("Got QP0 request");
            complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_QP0, ctx);
        }
        if (qp_type == IBV_QPT_GSI) {
            rc = save_mad_recv_buffer(backend_dev, sge, num_sge, ctx);
            if (rc) {
                complete_work(IBV_WC_GENERAL_ERR, rc, ctx);
                backend_dev->rdma_dev_res->stats.mad_rx_bufs_err++;
            } else {
                backend_dev->rdma_dev_res->stats.mad_rx_bufs++;
            }
        }
        return;
    }

    bctx = g_malloc0(sizeof(*bctx));
    bctx->up_ctx = ctx;
    bctx->backend_qp = qp;

    rc = rdma_rm_alloc_cqe_ctx(backend_dev->rdma_dev_res, &bctx_id, bctx);
    if (unlikely(rc)) {
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
        goto err_free_bctx;
    }

    rdma_protected_gslist_append_int32(&qp->cqe_ctx_list, bctx_id);

    rc = build_host_sge_array(backend_dev->rdma_dev_res, new_sge, sge, num_sge,
                              &backend_dev->rdma_dev_res->stats.rx_bufs_len);
    if (rc) {
        complete_work(IBV_WC_GENERAL_ERR, rc, ctx);
        goto err_dealloc_cqe_ctx;
    }

    wr.num_sge = num_sge;
    wr.sg_list = new_sge;
    wr.wr_id = bctx_id;
    rc = ibv_post_recv(qp->ibqp, &wr, &bad_wr);
    if (rc) {
        rdma_error_report("ibv_post_recv fail, qpn=0x%x, rc=%d, errno=%d",
                          qp->ibqp->qp_num, rc, errno);
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_FAIL_BACKEND, ctx);
        goto err_dealloc_cqe_ctx;
    }

    atomic_inc(&backend_dev->rdma_dev_res->stats.missing_cqe);
    backend_dev->rdma_dev_res->stats.rx_bufs++;

    return;

err_dealloc_cqe_ctx:
    backend_dev->rdma_dev_res->stats.rx_bufs_err++;
    rdma_rm_dealloc_cqe_ctx(backend_dev->rdma_dev_res, bctx_id);

err_free_bctx:
    g_free(bctx);
}

void rdma_backend_post_srq_recv(RdmaBackendDev *backend_dev,
                                RdmaBackendSRQ *srq, struct ibv_sge *sge,
                                uint32_t num_sge, void *ctx)
{
    BackendCtx *bctx;
    struct ibv_sge new_sge[MAX_SGE];
    uint32_t bctx_id;
    int rc;
    struct ibv_recv_wr wr = {}, *bad_wr;

    bctx = g_malloc0(sizeof(*bctx));
    bctx->up_ctx = ctx;
    bctx->backend_srq = srq;

    rc = rdma_rm_alloc_cqe_ctx(backend_dev->rdma_dev_res, &bctx_id, bctx);
    if (unlikely(rc)) {
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
        goto err_free_bctx;
    }

    rdma_protected_gslist_append_int32(&srq->cqe_ctx_list, bctx_id);

    rc = build_host_sge_array(backend_dev->rdma_dev_res, new_sge, sge, num_sge,
                              &backend_dev->rdma_dev_res->stats.rx_bufs_len);
    if (rc) {
        complete_work(IBV_WC_GENERAL_ERR, rc, ctx);
        goto err_dealloc_cqe_ctx;
    }

    wr.num_sge = num_sge;
    wr.sg_list = new_sge;
    wr.wr_id = bctx_id;
    rc = ibv_post_srq_recv(srq->ibsrq, &wr, &bad_wr);
    if (rc) {
        rdma_error_report("ibv_post_srq_recv fail, srqn=0x%x, rc=%d, errno=%d",
                          srq->ibsrq->handle, rc, errno);
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_FAIL_BACKEND, ctx);
        goto err_dealloc_cqe_ctx;
    }

    atomic_inc(&backend_dev->rdma_dev_res->stats.missing_cqe);
    backend_dev->rdma_dev_res->stats.rx_bufs++;
    backend_dev->rdma_dev_res->stats.rx_srq++;

    return;

err_dealloc_cqe_ctx:
    backend_dev->rdma_dev_res->stats.rx_bufs_err++;
    rdma_rm_dealloc_cqe_ctx(backend_dev->rdma_dev_res, bctx_id);

err_free_bctx:
    g_free(bctx);
}

int rdma_backend_create_pd(RdmaBackendDev *backend_dev, RdmaBackendPD *pd)
{
    pd->ibpd = ibv_alloc_pd(backend_dev->context);

    if (!pd->ibpd) {
        rdma_error_report("ibv_alloc_pd fail, errno=%d", errno);
        return -EIO;
    }

    return 0;
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
    mr->ibmr = ibv_reg_mr(pd->ibpd, addr, length, access);
    if (!mr->ibmr) {
        rdma_error_report("ibv_reg_mr fail, errno=%d", errno);
        return -EIO;
    }

    mr->ibpd = pd->ibpd;

    return 0;
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

    cq->ibcq = ibv_create_cq(backend_dev->context, cqe + 1, NULL,
                             backend_dev->channel, 0);
    if (!cq->ibcq) {
        rdma_error_report("ibv_create_cq fail, errno=%d", errno);
        return -EIO;
    }

    rc = ibv_req_notify_cq(cq->ibcq, 0);
    if (rc) {
        rdma_warn_report("ibv_req_notify_cq fail, rc=%d, errno=%d", rc, errno);
    }

    cq->backend_dev = backend_dev;

    return 0;
}

void rdma_backend_destroy_cq(RdmaBackendCQ *cq)
{
    if (cq->ibcq) {
        ibv_destroy_cq(cq->ibcq);
    }
}

int rdma_backend_create_qp(RdmaBackendQP *qp, uint8_t qp_type,
                           RdmaBackendPD *pd, RdmaBackendCQ *scq,
                           RdmaBackendCQ *rcq, RdmaBackendSRQ *srq,
                           uint32_t max_send_wr, uint32_t max_recv_wr,
                           uint32_t max_send_sge, uint32_t max_recv_sge)
{
    struct ibv_qp_init_attr attr = {};

    qp->ibqp = 0;

    switch (qp_type) {
    case IBV_QPT_GSI:
        return 0;

    case IBV_QPT_RC:
        /* fall through */
    case IBV_QPT_UD:
        /* do nothing */
        break;

    default:
        rdma_error_report("Unsupported QP type %d", qp_type);
        return -EIO;
    }

    attr.qp_type = qp_type;
    attr.send_cq = scq->ibcq;
    attr.recv_cq = rcq->ibcq;
    attr.cap.max_send_wr = max_send_wr;
    attr.cap.max_recv_wr = max_recv_wr;
    attr.cap.max_send_sge = max_send_sge;
    attr.cap.max_recv_sge = max_recv_sge;
    if (srq) {
        attr.srq = srq->ibsrq;
    }

    qp->ibqp = ibv_create_qp(pd->ibpd, &attr);
    if (!qp->ibqp) {
        rdma_error_report("ibv_create_qp fail, errno=%d", errno);
        return -EIO;
    }

    rdma_protected_gslist_init(&qp->cqe_ctx_list);

    qp->ibpd = pd->ibpd;

    /* TODO: Query QP to get max_inline_data and save it to be used in send */

    return 0;
}

int rdma_backend_qp_state_init(RdmaBackendDev *backend_dev, RdmaBackendQP *qp,
                               uint8_t qp_type, uint32_t qkey)
{
    struct ibv_qp_attr attr = {};
    int rc, attr_mask;

    attr_mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT;
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = backend_dev->port_num;

    switch (qp_type) {
    case IBV_QPT_RC:
        attr_mask |= IBV_QP_ACCESS_FLAGS;
        trace_rdma_backend_rc_qp_state_init(qp->ibqp->qp_num);
        break;

    case IBV_QPT_UD:
        attr.qkey = qkey;
        attr_mask |= IBV_QP_QKEY;
        trace_rdma_backend_ud_qp_state_init(qp->ibqp->qp_num, qkey);
        break;

    default:
        rdma_error_report("Unsupported QP type %d", qp_type);
        return -EIO;
    }

    rc = ibv_modify_qp(qp->ibqp, &attr, attr_mask);
    if (rc) {
        rdma_error_report("ibv_modify_qp fail, rc=%d, errno=%d", rc, errno);
        return -EIO;
    }

    return 0;
}

int rdma_backend_qp_state_rtr(RdmaBackendDev *backend_dev, RdmaBackendQP *qp,
                              uint8_t qp_type, uint8_t sgid_idx,
                              union ibv_gid *dgid, uint32_t dqpn,
                              uint32_t rq_psn, uint32_t qkey, bool use_qkey)
{
    struct ibv_qp_attr attr = {};
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

        trace_rdma_backend_rc_qp_state_rtr(qp->ibqp->qp_num,
                                           be64_to_cpu(ibv_gid.global.
                                                       subnet_prefix),
                                           be64_to_cpu(ibv_gid.global.
                                                       interface_id),
                                           qp->sgid_idx, dqpn, rq_psn);
        break;

    case IBV_QPT_UD:
        if (use_qkey) {
            attr.qkey = qkey;
            attr_mask |= IBV_QP_QKEY;
        }
        trace_rdma_backend_ud_qp_state_rtr(qp->ibqp->qp_num, use_qkey ? qkey :
                                           0);
        break;
    }

    rc = ibv_modify_qp(qp->ibqp, &attr, attr_mask);
    if (rc) {
        rdma_error_report("ibv_modify_qp fail, rc=%d, errno=%d", rc, errno);
        return -EIO;
    }

    return 0;
}

int rdma_backend_qp_state_rts(RdmaBackendQP *qp, uint8_t qp_type,
                              uint32_t sq_psn, uint32_t qkey, bool use_qkey)
{
    struct ibv_qp_attr attr = {};
    int rc, attr_mask;

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
        trace_rdma_backend_rc_qp_state_rts(qp->ibqp->qp_num, sq_psn);
        break;

    case IBV_QPT_UD:
        if (use_qkey) {
            attr.qkey = qkey;
            attr_mask |= IBV_QP_QKEY;
        }
        trace_rdma_backend_ud_qp_state_rts(qp->ibqp->qp_num, sq_psn,
                                           use_qkey ? qkey : 0);
        break;
    }

    rc = ibv_modify_qp(qp->ibqp, &attr, attr_mask);
    if (rc) {
        rdma_error_report("ibv_modify_qp fail, rc=%d, errno=%d", rc, errno);
        return -EIO;
    }

    return 0;
}

int rdma_backend_query_qp(RdmaBackendQP *qp, struct ibv_qp_attr *attr,
                          int attr_mask, struct ibv_qp_init_attr *init_attr)
{
    if (!qp->ibqp) {
        attr->qp_state = IBV_QPS_RTS;
        return 0;
    }

    return ibv_query_qp(qp->ibqp, attr, attr_mask, init_attr);
}

void rdma_backend_destroy_qp(RdmaBackendQP *qp, RdmaDeviceResources *dev_res)
{
    if (qp->ibqp) {
        ibv_destroy_qp(qp->ibqp);
    }
    g_slist_foreach(qp->cqe_ctx_list.list, free_cqe_ctx, dev_res);
    rdma_protected_gslist_destroy(&qp->cqe_ctx_list);
}

int rdma_backend_create_srq(RdmaBackendSRQ *srq, RdmaBackendPD *pd,
                            uint32_t max_wr, uint32_t max_sge,
                            uint32_t srq_limit)
{
    struct ibv_srq_init_attr srq_init_attr = {};

    srq_init_attr.attr.max_wr = max_wr;
    srq_init_attr.attr.max_sge = max_sge;
    srq_init_attr.attr.srq_limit = srq_limit;

    srq->ibsrq = ibv_create_srq(pd->ibpd, &srq_init_attr);
    if (!srq->ibsrq) {
        rdma_error_report("ibv_create_srq failed, errno=%d", errno);
        return -EIO;
    }

    rdma_protected_gslist_init(&srq->cqe_ctx_list);

    return 0;
}

int rdma_backend_query_srq(RdmaBackendSRQ *srq, struct ibv_srq_attr *srq_attr)
{
    if (!srq->ibsrq) {
        return -EINVAL;
    }

    return ibv_query_srq(srq->ibsrq, srq_attr);
}

int rdma_backend_modify_srq(RdmaBackendSRQ *srq, struct ibv_srq_attr *srq_attr,
                int srq_attr_mask)
{
    if (!srq->ibsrq) {
        return -EINVAL;
    }

    return ibv_modify_srq(srq->ibsrq, srq_attr, srq_attr_mask);
}

void rdma_backend_destroy_srq(RdmaBackendSRQ *srq, RdmaDeviceResources *dev_res)
{
    if (srq->ibsrq) {
        ibv_destroy_srq(srq->ibsrq);
    }
    g_slist_foreach(srq->cqe_ctx_list.list, free_cqe_ctx, dev_res);
    rdma_protected_gslist_destroy(&srq->cqe_ctx_list);
}

#define CHK_ATTR(req, dev, member, fmt) ({ \
    trace_rdma_check_dev_attr(#member, dev.member, req->member); \
    if (req->member > dev.member) { \
        rdma_warn_report("%s = "fmt" is higher than host device capability "fmt, \
                         #member, req->member, dev.member); \
        req->member = dev.member; \
    } \
})

static int init_device_caps(RdmaBackendDev *backend_dev,
                            struct ibv_device_attr *dev_attr)
{
    struct ibv_device_attr bk_dev_attr;
    int rc;

    rc = ibv_query_device(backend_dev->context, &bk_dev_attr);
    if (rc) {
        rdma_error_report("ibv_query_device fail, rc=%d, errno=%d", rc, errno);
        return -EIO;
    }

    dev_attr->max_sge = MAX_SGE;
    dev_attr->max_srq_sge = MAX_SGE;

    CHK_ATTR(dev_attr, bk_dev_attr, max_mr_size, "%" PRId64);
    CHK_ATTR(dev_attr, bk_dev_attr, max_qp, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_sge, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_cq, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_mr, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_pd, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_qp_rd_atom, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_qp_init_rd_atom, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_ah, "%d");
    CHK_ATTR(dev_attr, bk_dev_attr, max_srq, "%d");

    return 0;
}

static inline void build_mad_hdr(struct ibv_grh *grh, union ibv_gid *sgid,
                                 union ibv_gid *my_gid, int paylen)
{
    grh->paylen = htons(paylen);
    grh->sgid = *sgid;
    grh->dgid = *my_gid;
}

static void process_incoming_mad_req(RdmaBackendDev *backend_dev,
                                     RdmaCmMuxMsg *msg)
{
    unsigned long cqe_ctx_id;
    BackendCtx *bctx;
    char *mad;

    trace_mad_message("recv", msg->umad.mad, msg->umad_len);

    cqe_ctx_id = rdma_protected_qlist_pop_int64(&backend_dev->recv_mads_list);
    if (cqe_ctx_id == -ENOENT) {
        rdma_warn_report("No more free MADs buffers, waiting for a while");
        sleep(THR_POLL_TO);
        return;
    }

    bctx = rdma_rm_get_cqe_ctx(backend_dev->rdma_dev_res, cqe_ctx_id);
    if (unlikely(!bctx)) {
        rdma_error_report("No matching ctx for req %ld", cqe_ctx_id);
        backend_dev->rdma_dev_res->stats.mad_rx_err++;
        return;
    }

    mad = rdma_pci_dma_map(backend_dev->dev, bctx->sge.addr,
                           bctx->sge.length);
    if (!mad || bctx->sge.length < msg->umad_len + MAD_HDR_SIZE) {
        backend_dev->rdma_dev_res->stats.mad_rx_err++;
        complete_work(IBV_WC_GENERAL_ERR, VENDOR_ERR_INV_MAD_BUFF,
                      bctx->up_ctx);
    } else {
        struct ibv_wc wc = {};
        memset(mad, 0, bctx->sge.length);
        build_mad_hdr((struct ibv_grh *)mad,
                      (union ibv_gid *)&msg->umad.hdr.addr.gid, &msg->hdr.sgid,
                      msg->umad_len);
        memcpy(&mad[MAD_HDR_SIZE], msg->umad.mad, msg->umad_len);
        rdma_pci_dma_unmap(backend_dev->dev, mad, bctx->sge.length);

        wc.byte_len = msg->umad_len;
        wc.status = IBV_WC_SUCCESS;
        wc.wc_flags = IBV_WC_GRH;
        backend_dev->rdma_dev_res->stats.mad_rx++;
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

    trace_rdmacm_mux("read", msg->hdr.msg_type, msg->hdr.op_code);

    if (msg->hdr.msg_type != RDMACM_MUX_MSG_TYPE_REQ &&
        msg->hdr.op_code != RDMACM_MUX_OP_CODE_MAD) {
            rdma_error_report("Error: Not a MAD request, skipping");
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
        rdma_error_report("Missing chardev for MAD multiplexer");
        return -EIO;
    }

    rdma_protected_qlist_init(&backend_dev->recv_mads_list);

    enable_rdmacm_mux_async(backend_dev);

    qemu_chr_fe_set_handlers(backend_dev->rdmacm_mux.chr_be,
                             rdmacm_mux_can_receive, rdmacm_mux_read, NULL,
                             NULL, backend_dev, NULL, true);

    return 0;
}

static void mad_stop(RdmaBackendDev *backend_dev)
{
    clean_recv_mads(backend_dev);
}

static void mad_fini(RdmaBackendDev *backend_dev)
{
    disable_rdmacm_mux_async(backend_dev);
    qemu_chr_fe_disconnect(backend_dev->rdmacm_mux.chr_be);
    rdma_protected_qlist_destroy(&backend_dev->recv_mads_list);
}

int rdma_backend_get_gid_index(RdmaBackendDev *backend_dev,
                               union ibv_gid *gid)
{
    union ibv_gid sgid;
    int ret;
    int i = 0;

    do {
        ret = ibv_query_gid(backend_dev->context, backend_dev->port_num, i,
                            &sgid);
        i++;
    } while (!ret && (memcmp(&sgid, gid, sizeof(*gid))));

    trace_rdma_backend_get_gid_index(be64_to_cpu(gid->global.subnet_prefix),
                                     be64_to_cpu(gid->global.interface_id),
                                     i - 1);

    return ret ? ret : i - 1;
}

int rdma_backend_add_gid(RdmaBackendDev *backend_dev, const char *ifname,
                         union ibv_gid *gid)
{
    RdmaCmMuxMsg msg = {};
    int ret;

    trace_rdma_backend_gid_change("add", be64_to_cpu(gid->global.subnet_prefix),
                                  be64_to_cpu(gid->global.interface_id));

    msg.hdr.op_code = RDMACM_MUX_OP_CODE_REG;
    memcpy(msg.hdr.sgid.raw, gid->raw, sizeof(msg.hdr.sgid));

    ret = rdmacm_mux_send(backend_dev, &msg);
    if (ret) {
        rdma_error_report("Failed to register GID to rdma_umadmux (%d)", ret);
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

    trace_rdma_backend_gid_change("del", be64_to_cpu(gid->global.subnet_prefix),
                                  be64_to_cpu(gid->global.interface_id));

    msg.hdr.op_code = RDMACM_MUX_OP_CODE_UNREG;
    memcpy(msg.hdr.sgid.raw, gid->raw, sizeof(msg.hdr.sgid));

    ret = rdmacm_mux_send(backend_dev, &msg);
    if (ret) {
        rdma_error_report("Failed to unregister GID from rdma_umadmux (%d)",
                          ret);
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
                      struct ibv_device_attr *dev_attr, CharBackend *mad_chr_be)
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
        rdma_error_report("Failed to get IB devices list");
        return -EIO;
    }

    if (num_ibv_devices == 0) {
        rdma_error_report("No IB devices were found");
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
            rdma_error_report("Failed to find IB device %s",
                              backend_device_name);
            ret = -EIO;
            goto out_free_dev_list;
        }
    } else {
        backend_dev->ib_dev = *dev_list;
    }

    rdma_info_report("uverb device %s", backend_dev->ib_dev->dev_name);

    backend_dev->context = ibv_open_device(backend_dev->ib_dev);
    if (!backend_dev->context) {
        rdma_error_report("Failed to open IB device %s",
                          ibv_get_device_name(backend_dev->ib_dev));
        ret = -EIO;
        goto out;
    }

    backend_dev->channel = ibv_create_comp_channel(backend_dev->context);
    if (!backend_dev->channel) {
        rdma_error_report("Failed to create IB communication channel");
        ret = -EIO;
        goto out_close_device;
    }

    ret = init_device_caps(backend_dev, dev_attr);
    if (ret) {
        rdma_error_report("Failed to initialize device capabilities");
        ret = -EIO;
        goto out_destroy_comm_channel;
    }


    ret = mad_init(backend_dev, mad_chr_be);
    if (ret) {
        rdma_error_report("Failed to initialize mad");
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
    start_comp_thread(backend_dev);
}

void rdma_backend_stop(RdmaBackendDev *backend_dev)
{
    mad_stop(backend_dev);
    stop_backend_thread(&backend_dev->comp_thread);
}

void rdma_backend_fini(RdmaBackendDev *backend_dev)
{
    mad_fini(backend_dev);
    g_hash_table_destroy(ah_hash);
    ibv_destroy_comp_channel(backend_dev->channel);
    ibv_close_device(backend_dev->context);
}
