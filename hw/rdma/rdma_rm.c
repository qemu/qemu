/*
 * QEMU paravirtual RDMA - Resource Manager Implementation
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
#include "qapi/error.h"
#include "cpu.h"
#include "monitor/monitor.h"

#include "trace.h"
#include "rdma_utils.h"
#include "rdma_backend.h"
#include "rdma_rm.h"

/* Page directory and page tables */
#define PG_DIR_SZ { TARGET_PAGE_SIZE / sizeof(__u64) }
#define PG_TBL_SZ { TARGET_PAGE_SIZE / sizeof(__u64) }

void rdma_dump_device_counters(Monitor *mon, RdmaDeviceResources *dev_res)
{
    monitor_printf(mon, "\ttx               : %" PRId64 "\n",
                   dev_res->stats.tx);
    monitor_printf(mon, "\ttx_len           : %" PRId64 "\n",
                   dev_res->stats.tx_len);
    monitor_printf(mon, "\ttx_err           : %" PRId64 "\n",
                   dev_res->stats.tx_err);
    monitor_printf(mon, "\trx_bufs          : %" PRId64 "\n",
                   dev_res->stats.rx_bufs);
    monitor_printf(mon, "\trx_srq           : %" PRId64 "\n",
                   dev_res->stats.rx_srq);
    monitor_printf(mon, "\trx_bufs_len      : %" PRId64 "\n",
                   dev_res->stats.rx_bufs_len);
    monitor_printf(mon, "\trx_bufs_err      : %" PRId64 "\n",
                   dev_res->stats.rx_bufs_err);
    monitor_printf(mon, "\tcomps            : %" PRId64 "\n",
                   dev_res->stats.completions);
    monitor_printf(mon, "\tmissing_comps    : %" PRId32 "\n",
                   dev_res->stats.missing_cqe);
    monitor_printf(mon, "\tpoll_cq (bk)     : %" PRId64 "\n",
                   dev_res->stats.poll_cq_from_bk);
    monitor_printf(mon, "\tpoll_cq_ppoll_to : %" PRId64 "\n",
                   dev_res->stats.poll_cq_ppoll_to);
    monitor_printf(mon, "\tpoll_cq (fe)     : %" PRId64 "\n",
                   dev_res->stats.poll_cq_from_guest);
    monitor_printf(mon, "\tpoll_cq_empty    : %" PRId64 "\n",
                   dev_res->stats.poll_cq_from_guest_empty);
    monitor_printf(mon, "\tmad_tx           : %" PRId64 "\n",
                   dev_res->stats.mad_tx);
    monitor_printf(mon, "\tmad_tx_err       : %" PRId64 "\n",
                   dev_res->stats.mad_tx_err);
    monitor_printf(mon, "\tmad_rx           : %" PRId64 "\n",
                   dev_res->stats.mad_rx);
    monitor_printf(mon, "\tmad_rx_err       : %" PRId64 "\n",
                   dev_res->stats.mad_rx_err);
    monitor_printf(mon, "\tmad_rx_bufs      : %" PRId64 "\n",
                   dev_res->stats.mad_rx_bufs);
    monitor_printf(mon, "\tmad_rx_bufs_err  : %" PRId64 "\n",
                   dev_res->stats.mad_rx_bufs_err);
    monitor_printf(mon, "\tPDs              : %" PRId32 "\n",
                   dev_res->pd_tbl.used);
    monitor_printf(mon, "\tMRs              : %" PRId32 "\n",
                   dev_res->mr_tbl.used);
    monitor_printf(mon, "\tUCs              : %" PRId32 "\n",
                   dev_res->uc_tbl.used);
    monitor_printf(mon, "\tQPs              : %" PRId32 "\n",
                   dev_res->qp_tbl.used);
    monitor_printf(mon, "\tCQs              : %" PRId32 "\n",
                   dev_res->cq_tbl.used);
    monitor_printf(mon, "\tCEQ_CTXs         : %" PRId32 "\n",
                   dev_res->cqe_ctx_tbl.used);
}

static inline void res_tbl_init(const char *name, RdmaRmResTbl *tbl,
                                uint32_t tbl_sz, uint32_t res_sz)
{
    tbl->tbl = g_malloc(tbl_sz * res_sz);

    strncpy(tbl->name, name, MAX_RM_TBL_NAME);
    tbl->name[MAX_RM_TBL_NAME - 1] = 0;

    tbl->bitmap = bitmap_new(tbl_sz);
    tbl->tbl_sz = tbl_sz;
    tbl->res_sz = res_sz;
    tbl->used = 0;
    qemu_mutex_init(&tbl->lock);
}

static inline void res_tbl_free(RdmaRmResTbl *tbl)
{
    if (!tbl->bitmap) {
        return;
    }
    qemu_mutex_destroy(&tbl->lock);
    g_free(tbl->tbl);
    g_free(tbl->bitmap);
}

static inline void *rdma_res_tbl_get(RdmaRmResTbl *tbl, uint32_t handle)
{
    trace_rdma_res_tbl_get(tbl->name, handle);

    if ((handle < tbl->tbl_sz) && (test_bit(handle, tbl->bitmap))) {
        return tbl->tbl + handle * tbl->res_sz;
    } else {
        rdma_error_report("Table %s, invalid handle %d", tbl->name, handle);
        return NULL;
    }
}

static inline void *rdma_res_tbl_alloc(RdmaRmResTbl *tbl, uint32_t *handle)
{
    qemu_mutex_lock(&tbl->lock);

    *handle = find_first_zero_bit(tbl->bitmap, tbl->tbl_sz);
    if (*handle > tbl->tbl_sz) {
        rdma_error_report("Table %s, failed to allocate, bitmap is full",
                          tbl->name);
        qemu_mutex_unlock(&tbl->lock);
        return NULL;
    }

    set_bit(*handle, tbl->bitmap);

    tbl->used++;

    qemu_mutex_unlock(&tbl->lock);

    memset(tbl->tbl + *handle * tbl->res_sz, 0, tbl->res_sz);

    trace_rdma_res_tbl_alloc(tbl->name, *handle);

    return tbl->tbl + *handle * tbl->res_sz;
}

static inline void rdma_res_tbl_dealloc(RdmaRmResTbl *tbl, uint32_t handle)
{
    trace_rdma_res_tbl_dealloc(tbl->name, handle);

    qemu_mutex_lock(&tbl->lock);

    if (handle < tbl->tbl_sz) {
        clear_bit(handle, tbl->bitmap);
        tbl->used--;
    }

    qemu_mutex_unlock(&tbl->lock);
}

int rdma_rm_alloc_pd(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                     uint32_t *pd_handle, uint32_t ctx_handle)
{
    RdmaRmPD *pd;
    int ret = -ENOMEM;

    pd = rdma_res_tbl_alloc(&dev_res->pd_tbl, pd_handle);
    if (!pd) {
        goto out;
    }

    ret = rdma_backend_create_pd(backend_dev, &pd->backend_pd);
    if (ret) {
        ret = -EIO;
        goto out_tbl_dealloc;
    }

    pd->ctx_handle = ctx_handle;

    return 0;

out_tbl_dealloc:
    rdma_res_tbl_dealloc(&dev_res->pd_tbl, *pd_handle);

out:
    return ret;
}

RdmaRmPD *rdma_rm_get_pd(RdmaDeviceResources *dev_res, uint32_t pd_handle)
{
    return rdma_res_tbl_get(&dev_res->pd_tbl, pd_handle);
}

void rdma_rm_dealloc_pd(RdmaDeviceResources *dev_res, uint32_t pd_handle)
{
    RdmaRmPD *pd = rdma_rm_get_pd(dev_res, pd_handle);

    if (pd) {
        rdma_backend_destroy_pd(&pd->backend_pd);
        rdma_res_tbl_dealloc(&dev_res->pd_tbl, pd_handle);
    }
}

int rdma_rm_alloc_mr(RdmaDeviceResources *dev_res, uint32_t pd_handle,
                     uint64_t guest_start, uint64_t guest_length,
                     void *host_virt, int access_flags, uint32_t *mr_handle,
                     uint32_t *lkey, uint32_t *rkey)
{
    RdmaRmMR *mr;
    int ret = 0;
    RdmaRmPD *pd;

    pd = rdma_rm_get_pd(dev_res, pd_handle);
    if (!pd) {
        return -EINVAL;
    }

    mr = rdma_res_tbl_alloc(&dev_res->mr_tbl, mr_handle);
    if (!mr) {
        return -ENOMEM;
    }
    trace_rdma_rm_alloc_mr(*mr_handle, host_virt, guest_start, guest_length,
                           access_flags);

    if (host_virt) {
        mr->virt = host_virt;
        mr->start = guest_start;
        mr->length = guest_length;
        mr->virt += (mr->start & (TARGET_PAGE_SIZE - 1));

#ifdef LEGACY_RDMA_REG_MR
        ret = rdma_backend_create_mr(&mr->backend_mr, &pd->backend_pd, mr->virt,
                                     mr->length, access_flags);
#else
        ret = rdma_backend_create_mr(&mr->backend_mr, &pd->backend_pd, mr->virt,
                                     mr->length, guest_start, access_flags);
#endif
        if (ret) {
            ret = -EIO;
            goto out_dealloc_mr;
        }
    }

    /* We keep mr_handle in lkey so send and recv get get mr ptr */
    *lkey = *mr_handle;
    *rkey = -1;

    mr->pd_handle = pd_handle;

    return 0;

out_dealloc_mr:
    rdma_res_tbl_dealloc(&dev_res->mr_tbl, *mr_handle);

    return ret;
}

RdmaRmMR *rdma_rm_get_mr(RdmaDeviceResources *dev_res, uint32_t mr_handle)
{
    return rdma_res_tbl_get(&dev_res->mr_tbl, mr_handle);
}

void rdma_rm_dealloc_mr(RdmaDeviceResources *dev_res, uint32_t mr_handle)
{
    RdmaRmMR *mr = rdma_rm_get_mr(dev_res, mr_handle);

    if (mr) {
        rdma_backend_destroy_mr(&mr->backend_mr);
        trace_rdma_rm_dealloc_mr(mr_handle, mr->start);
        if (mr->start) {
            mr->virt -= (mr->start & (TARGET_PAGE_SIZE - 1));
            munmap(mr->virt, mr->length);
        }
        rdma_res_tbl_dealloc(&dev_res->mr_tbl, mr_handle);
    }
}

int rdma_rm_alloc_uc(RdmaDeviceResources *dev_res, uint32_t pfn,
                     uint32_t *uc_handle)
{
    RdmaRmUC *uc;

    /* TODO: Need to make sure pfn is between bar start address and
     * bsd+RDMA_BAR2_UAR_SIZE
    if (pfn > RDMA_BAR2_UAR_SIZE) {
        rdma_error_report("pfn out of range (%d > %d)", pfn,
                          RDMA_BAR2_UAR_SIZE);
        return -ENOMEM;
    }
    */

    uc = rdma_res_tbl_alloc(&dev_res->uc_tbl, uc_handle);
    if (!uc) {
        return -ENOMEM;
    }

    return 0;
}

RdmaRmUC *rdma_rm_get_uc(RdmaDeviceResources *dev_res, uint32_t uc_handle)
{
    return rdma_res_tbl_get(&dev_res->uc_tbl, uc_handle);
}

void rdma_rm_dealloc_uc(RdmaDeviceResources *dev_res, uint32_t uc_handle)
{
    RdmaRmUC *uc = rdma_rm_get_uc(dev_res, uc_handle);

    if (uc) {
        rdma_res_tbl_dealloc(&dev_res->uc_tbl, uc_handle);
    }
}

RdmaRmCQ *rdma_rm_get_cq(RdmaDeviceResources *dev_res, uint32_t cq_handle)
{
    return rdma_res_tbl_get(&dev_res->cq_tbl, cq_handle);
}

int rdma_rm_alloc_cq(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                     uint32_t cqe, uint32_t *cq_handle, void *opaque)
{
    int rc;
    RdmaRmCQ *cq;

    cq = rdma_res_tbl_alloc(&dev_res->cq_tbl, cq_handle);
    if (!cq) {
        return -ENOMEM;
    }

    cq->opaque = opaque;
    cq->notify = CNT_CLEAR;

    rc = rdma_backend_create_cq(backend_dev, &cq->backend_cq, cqe);
    if (rc) {
        rc = -EIO;
        goto out_dealloc_cq;
    }

    return 0;

out_dealloc_cq:
    rdma_rm_dealloc_cq(dev_res, *cq_handle);

    return rc;
}

void rdma_rm_req_notify_cq(RdmaDeviceResources *dev_res, uint32_t cq_handle,
                           bool notify)
{
    RdmaRmCQ *cq;

    cq = rdma_rm_get_cq(dev_res, cq_handle);
    if (!cq) {
        return;
    }

    if (cq->notify != CNT_SET) {
        cq->notify = notify ? CNT_ARM : CNT_CLEAR;
    }
}

void rdma_rm_dealloc_cq(RdmaDeviceResources *dev_res, uint32_t cq_handle)
{
    RdmaRmCQ *cq;

    cq = rdma_rm_get_cq(dev_res, cq_handle);
    if (!cq) {
        return;
    }

    rdma_backend_destroy_cq(&cq->backend_cq);

    rdma_res_tbl_dealloc(&dev_res->cq_tbl, cq_handle);
}

RdmaRmQP *rdma_rm_get_qp(RdmaDeviceResources *dev_res, uint32_t qpn)
{
    GBytes *key = g_bytes_new(&qpn, sizeof(qpn));

    RdmaRmQP *qp = g_hash_table_lookup(dev_res->qp_hash, key);

    g_bytes_unref(key);

    if (!qp) {
        rdma_error_report("Invalid QP handle %d", qpn);
    }

    return qp;
}

int rdma_rm_alloc_qp(RdmaDeviceResources *dev_res, uint32_t pd_handle,
                     uint8_t qp_type, uint32_t max_send_wr,
                     uint32_t max_send_sge, uint32_t send_cq_handle,
                     uint32_t max_recv_wr, uint32_t max_recv_sge,
                     uint32_t recv_cq_handle, void *opaque, uint32_t *qpn,
                     uint8_t is_srq, uint32_t srq_handle)
{
    int rc;
    RdmaRmQP *qp;
    RdmaRmCQ *scq, *rcq;
    RdmaRmPD *pd;
    RdmaRmSRQ *srq = NULL;
    uint32_t rm_qpn;

    pd = rdma_rm_get_pd(dev_res, pd_handle);
    if (!pd) {
        return -EINVAL;
    }

    scq = rdma_rm_get_cq(dev_res, send_cq_handle);
    rcq = rdma_rm_get_cq(dev_res, recv_cq_handle);

    if (!scq || !rcq) {
        rdma_error_report("Invalid send_cqn or recv_cqn (%d, %d)",
                          send_cq_handle, recv_cq_handle);
        return -EINVAL;
    }

    if (is_srq) {
        srq = rdma_rm_get_srq(dev_res, srq_handle);
        if (!srq) {
            rdma_error_report("Invalid srqn %d", srq_handle);
            return -EINVAL;
        }

        srq->recv_cq_handle = recv_cq_handle;
    }

    if (qp_type == IBV_QPT_GSI) {
        scq->notify = CNT_SET;
        rcq->notify = CNT_SET;
    }

    qp = rdma_res_tbl_alloc(&dev_res->qp_tbl, &rm_qpn);
    if (!qp) {
        return -ENOMEM;
    }

    qp->qpn = rm_qpn;
    qp->qp_state = IBV_QPS_RESET;
    qp->qp_type = qp_type;
    qp->send_cq_handle = send_cq_handle;
    qp->recv_cq_handle = recv_cq_handle;
    qp->opaque = opaque;
    qp->is_srq = is_srq;

    rc = rdma_backend_create_qp(&qp->backend_qp, qp_type, &pd->backend_pd,
                                &scq->backend_cq, &rcq->backend_cq,
                                is_srq ? &srq->backend_srq : NULL,
                                max_send_wr, max_recv_wr, max_send_sge,
                                max_recv_sge);

    if (rc) {
        rc = -EIO;
        goto out_dealloc_qp;
    }

    *qpn = rdma_backend_qpn(&qp->backend_qp);
    trace_rdma_rm_alloc_qp(rm_qpn, *qpn, qp_type);
    g_hash_table_insert(dev_res->qp_hash, g_bytes_new(qpn, sizeof(*qpn)), qp);

    return 0;

out_dealloc_qp:
    rdma_res_tbl_dealloc(&dev_res->qp_tbl, qp->qpn);

    return rc;
}

int rdma_rm_modify_qp(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                      uint32_t qp_handle, uint32_t attr_mask, uint8_t sgid_idx,
                      union ibv_gid *dgid, uint32_t dqpn,
                      enum ibv_qp_state qp_state, uint32_t qkey,
                      uint32_t rq_psn, uint32_t sq_psn)
{
    RdmaRmQP *qp;
    int ret;

    qp = rdma_rm_get_qp(dev_res, qp_handle);
    if (!qp) {
        return -EINVAL;
    }

    if (qp->qp_type == IBV_QPT_SMI) {
        rdma_error_report("Got QP0 request");
        return -EPERM;
    } else if (qp->qp_type == IBV_QPT_GSI) {
        return 0;
    }

    trace_rdma_rm_modify_qp(qp_handle, attr_mask, qp_state, sgid_idx);

    if (attr_mask & IBV_QP_STATE) {
        qp->qp_state = qp_state;

        if (qp->qp_state == IBV_QPS_INIT) {
            ret = rdma_backend_qp_state_init(backend_dev, &qp->backend_qp,
                                             qp->qp_type, qkey);
            if (ret) {
                return -EIO;
            }
        }

        if (qp->qp_state == IBV_QPS_RTR) {
            /* Get backend gid index */
            sgid_idx = rdma_rm_get_backend_gid_index(dev_res, backend_dev,
                                                     sgid_idx);
            if (sgid_idx <= 0) { /* TODO check also less than bk.max_sgid */
                rdma_error_report("Failed to get bk sgid_idx for sgid_idx %d",
                                  sgid_idx);
                return -EIO;
            }

            ret = rdma_backend_qp_state_rtr(backend_dev, &qp->backend_qp,
                                            qp->qp_type, sgid_idx, dgid, dqpn,
                                            rq_psn, qkey,
                                            attr_mask & IBV_QP_QKEY);
            if (ret) {
                return -EIO;
            }
        }

        if (qp->qp_state == IBV_QPS_RTS) {
            ret = rdma_backend_qp_state_rts(&qp->backend_qp, qp->qp_type,
                                            sq_psn, qkey,
                                            attr_mask & IBV_QP_QKEY);
            if (ret) {
                return -EIO;
            }
        }
    }

    return 0;
}

int rdma_rm_query_qp(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                     uint32_t qp_handle, struct ibv_qp_attr *attr,
                     int attr_mask, struct ibv_qp_init_attr *init_attr)
{
    RdmaRmQP *qp;

    qp = rdma_rm_get_qp(dev_res, qp_handle);
    if (!qp) {
        return -EINVAL;
    }

    return rdma_backend_query_qp(&qp->backend_qp, attr, attr_mask, init_attr);
}

void rdma_rm_dealloc_qp(RdmaDeviceResources *dev_res, uint32_t qp_handle)
{
    RdmaRmQP *qp;
    GBytes *key;

    key = g_bytes_new(&qp_handle, sizeof(qp_handle));
    qp = g_hash_table_lookup(dev_res->qp_hash, key);
    g_hash_table_remove(dev_res->qp_hash, key);
    g_bytes_unref(key);

    if (!qp) {
        return;
    }

    rdma_backend_destroy_qp(&qp->backend_qp, dev_res);

    rdma_res_tbl_dealloc(&dev_res->qp_tbl, qp->qpn);
}

RdmaRmSRQ *rdma_rm_get_srq(RdmaDeviceResources *dev_res, uint32_t srq_handle)
{
    return rdma_res_tbl_get(&dev_res->srq_tbl, srq_handle);
}

int rdma_rm_alloc_srq(RdmaDeviceResources *dev_res, uint32_t pd_handle,
                      uint32_t max_wr, uint32_t max_sge, uint32_t srq_limit,
                      uint32_t *srq_handle, void *opaque)
{
    RdmaRmSRQ *srq;
    RdmaRmPD *pd;
    int rc;

    pd = rdma_rm_get_pd(dev_res, pd_handle);
    if (!pd) {
        return -EINVAL;
    }

    srq = rdma_res_tbl_alloc(&dev_res->srq_tbl, srq_handle);
    if (!srq) {
        return -ENOMEM;
    }

    rc = rdma_backend_create_srq(&srq->backend_srq, &pd->backend_pd,
                                 max_wr, max_sge, srq_limit);
    if (rc) {
        rc = -EIO;
        goto out_dealloc_srq;
    }

    srq->opaque = opaque;

    return 0;

out_dealloc_srq:
    rdma_res_tbl_dealloc(&dev_res->srq_tbl, *srq_handle);

    return rc;
}

int rdma_rm_query_srq(RdmaDeviceResources *dev_res, uint32_t srq_handle,
                      struct ibv_srq_attr *srq_attr)
{
    RdmaRmSRQ *srq;

    srq = rdma_rm_get_srq(dev_res, srq_handle);
    if (!srq) {
        return -EINVAL;
    }

    return rdma_backend_query_srq(&srq->backend_srq, srq_attr);
}

int rdma_rm_modify_srq(RdmaDeviceResources *dev_res, uint32_t srq_handle,
                       struct ibv_srq_attr *srq_attr, int srq_attr_mask)
{
    RdmaRmSRQ *srq;

    srq = rdma_rm_get_srq(dev_res, srq_handle);
    if (!srq) {
        return -EINVAL;
    }

    if ((srq_attr_mask & IBV_SRQ_LIMIT) &&
        (srq_attr->srq_limit == 0)) {
        return -EINVAL;
    }

    if ((srq_attr_mask & IBV_SRQ_MAX_WR) &&
        (srq_attr->max_wr == 0)) {
        return -EINVAL;
    }

    return rdma_backend_modify_srq(&srq->backend_srq, srq_attr,
                                   srq_attr_mask);
}

void rdma_rm_dealloc_srq(RdmaDeviceResources *dev_res, uint32_t srq_handle)
{
    RdmaRmSRQ *srq;

    srq = rdma_rm_get_srq(dev_res, srq_handle);
    if (!srq) {
        return;
    }

    rdma_backend_destroy_srq(&srq->backend_srq, dev_res);
    rdma_res_tbl_dealloc(&dev_res->srq_tbl, srq_handle);
}

void *rdma_rm_get_cqe_ctx(RdmaDeviceResources *dev_res, uint32_t cqe_ctx_id)
{
    void **cqe_ctx;

    cqe_ctx = rdma_res_tbl_get(&dev_res->cqe_ctx_tbl, cqe_ctx_id);
    if (!cqe_ctx) {
        return NULL;
    }

    return *cqe_ctx;
}

int rdma_rm_alloc_cqe_ctx(RdmaDeviceResources *dev_res, uint32_t *cqe_ctx_id,
                          void *ctx)
{
    void **cqe_ctx;

    cqe_ctx = rdma_res_tbl_alloc(&dev_res->cqe_ctx_tbl, cqe_ctx_id);
    if (!cqe_ctx) {
        return -ENOMEM;
    }

    *cqe_ctx = ctx;

    return 0;
}

void rdma_rm_dealloc_cqe_ctx(RdmaDeviceResources *dev_res, uint32_t cqe_ctx_id)
{
    rdma_res_tbl_dealloc(&dev_res->cqe_ctx_tbl, cqe_ctx_id);
}

int rdma_rm_add_gid(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                    const char *ifname, union ibv_gid *gid, int gid_idx)
{
    int rc;

    rc = rdma_backend_add_gid(backend_dev, ifname, gid);
    if (rc) {
        return -EINVAL;
    }

    memcpy(&dev_res->port.gid_tbl[gid_idx].gid, gid, sizeof(*gid));

    return 0;
}

int rdma_rm_del_gid(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                    const char *ifname, int gid_idx)
{
    int rc;

    if (!dev_res->port.gid_tbl[gid_idx].gid.global.interface_id) {
        return 0;
    }

    rc = rdma_backend_del_gid(backend_dev, ifname,
                              &dev_res->port.gid_tbl[gid_idx].gid);
    if (rc) {
        return -EINVAL;
    }

    memset(dev_res->port.gid_tbl[gid_idx].gid.raw, 0,
           sizeof(dev_res->port.gid_tbl[gid_idx].gid));
    dev_res->port.gid_tbl[gid_idx].backend_gid_index = -1;

    return 0;
}

int rdma_rm_get_backend_gid_index(RdmaDeviceResources *dev_res,
                                  RdmaBackendDev *backend_dev, int sgid_idx)
{
    if (unlikely(sgid_idx < 0 || sgid_idx >= MAX_PORT_GIDS)) {
        rdma_error_report("Got invalid sgid_idx %d", sgid_idx);
        return -EINVAL;
    }

    if (unlikely(dev_res->port.gid_tbl[sgid_idx].backend_gid_index == -1)) {
        dev_res->port.gid_tbl[sgid_idx].backend_gid_index =
        rdma_backend_get_gid_index(backend_dev,
                                   &dev_res->port.gid_tbl[sgid_idx].gid);
    }

    return dev_res->port.gid_tbl[sgid_idx].backend_gid_index;
}

static void destroy_qp_hash_key(gpointer data)
{
    g_bytes_unref(data);
}

static void init_ports(RdmaDeviceResources *dev_res)
{
    int i;

    memset(&dev_res->port, 0, sizeof(dev_res->port));

    dev_res->port.state = IBV_PORT_DOWN;
    for (i = 0; i < MAX_PORT_GIDS; i++) {
        dev_res->port.gid_tbl[i].backend_gid_index = -1;
    }
}

static void fini_ports(RdmaDeviceResources *dev_res,
                       RdmaBackendDev *backend_dev, const char *ifname)
{
    int i;

    dev_res->port.state = IBV_PORT_DOWN;
    for (i = 0; i < MAX_PORT_GIDS; i++) {
        rdma_rm_del_gid(dev_res, backend_dev, ifname, i);
    }
}

int rdma_rm_init(RdmaDeviceResources *dev_res, struct ibv_device_attr *dev_attr)
{
    dev_res->qp_hash = g_hash_table_new_full(g_bytes_hash, g_bytes_equal,
                                             destroy_qp_hash_key, NULL);
    if (!dev_res->qp_hash) {
        return -ENOMEM;
    }

    res_tbl_init("PD", &dev_res->pd_tbl, dev_attr->max_pd, sizeof(RdmaRmPD));
    res_tbl_init("CQ", &dev_res->cq_tbl, dev_attr->max_cq, sizeof(RdmaRmCQ));
    res_tbl_init("MR", &dev_res->mr_tbl, dev_attr->max_mr, sizeof(RdmaRmMR));
    res_tbl_init("QP", &dev_res->qp_tbl, dev_attr->max_qp, sizeof(RdmaRmQP));
    res_tbl_init("CQE_CTX", &dev_res->cqe_ctx_tbl, dev_attr->max_qp *
                       dev_attr->max_qp_wr, sizeof(void *));
    res_tbl_init("UC", &dev_res->uc_tbl, MAX_UCS, sizeof(RdmaRmUC));
    res_tbl_init("SRQ", &dev_res->srq_tbl, dev_attr->max_srq,
                 sizeof(RdmaRmSRQ));

    init_ports(dev_res);

    qemu_mutex_init(&dev_res->lock);

    memset(&dev_res->stats, 0, sizeof(dev_res->stats));
    atomic_set(&dev_res->stats.missing_cqe, 0);

    return 0;
}

void rdma_rm_fini(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                  const char *ifname)
{
    qemu_mutex_destroy(&dev_res->lock);

    fini_ports(dev_res, backend_dev, ifname);

    res_tbl_free(&dev_res->srq_tbl);
    res_tbl_free(&dev_res->uc_tbl);
    res_tbl_free(&dev_res->cqe_ctx_tbl);
    res_tbl_free(&dev_res->qp_tbl);
    res_tbl_free(&dev_res->mr_tbl);
    res_tbl_free(&dev_res->cq_tbl);
    res_tbl_free(&dev_res->pd_tbl);

    if (dev_res->qp_hash) {
        g_hash_table_destroy(dev_res->qp_hash);
    }
}
