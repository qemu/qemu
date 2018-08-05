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

#include "rdma_utils.h"
#include "rdma_backend.h"
#include "rdma_rm.h"

/* Page directory and page tables */
#define PG_DIR_SZ { TARGET_PAGE_SIZE / sizeof(__u64) }
#define PG_TBL_SZ { TARGET_PAGE_SIZE / sizeof(__u64) }

static inline void res_tbl_init(const char *name, RdmaRmResTbl *tbl,
                                uint32_t tbl_sz, uint32_t res_sz)
{
    tbl->tbl = g_malloc(tbl_sz * res_sz);

    strncpy(tbl->name, name, MAX_RM_TBL_NAME);
    tbl->name[MAX_RM_TBL_NAME - 1] = 0;

    tbl->bitmap = bitmap_new(tbl_sz);
    tbl->tbl_sz = tbl_sz;
    tbl->res_sz = res_sz;
    qemu_mutex_init(&tbl->lock);
}

static inline void res_tbl_free(RdmaRmResTbl *tbl)
{
    qemu_mutex_destroy(&tbl->lock);
    g_free(tbl->tbl);
    bitmap_zero_extend(tbl->bitmap, tbl->tbl_sz, 0);
}

static inline void *res_tbl_get(RdmaRmResTbl *tbl, uint32_t handle)
{
    pr_dbg("%s, handle=%d\n", tbl->name, handle);

    if ((handle < tbl->tbl_sz) && (test_bit(handle, tbl->bitmap))) {
        return tbl->tbl + handle * tbl->res_sz;
    } else {
        pr_dbg("Invalid handle %d\n", handle);
        return NULL;
    }
}

static inline void *res_tbl_alloc(RdmaRmResTbl *tbl, uint32_t *handle)
{
    qemu_mutex_lock(&tbl->lock);

    *handle = find_first_zero_bit(tbl->bitmap, tbl->tbl_sz);
    if (*handle > tbl->tbl_sz) {
        pr_dbg("Failed to alloc, bitmap is full\n");
        qemu_mutex_unlock(&tbl->lock);
        return NULL;
    }

    set_bit(*handle, tbl->bitmap);

    qemu_mutex_unlock(&tbl->lock);

    memset(tbl->tbl + *handle * tbl->res_sz, 0, tbl->res_sz);

    pr_dbg("%s, handle=%d\n", tbl->name, *handle);

    return tbl->tbl + *handle * tbl->res_sz;
}

static inline void res_tbl_dealloc(RdmaRmResTbl *tbl, uint32_t handle)
{
    pr_dbg("%s, handle=%d\n", tbl->name, handle);

    qemu_mutex_lock(&tbl->lock);

    if (handle < tbl->tbl_sz) {
        clear_bit(handle, tbl->bitmap);
    }

    qemu_mutex_unlock(&tbl->lock);
}

int rdma_rm_alloc_pd(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                     uint32_t *pd_handle, uint32_t ctx_handle)
{
    RdmaRmPD *pd;
    int ret = -ENOMEM;

    pd = res_tbl_alloc(&dev_res->pd_tbl, pd_handle);
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
    res_tbl_dealloc(&dev_res->pd_tbl, *pd_handle);

out:
    return ret;
}

RdmaRmPD *rdma_rm_get_pd(RdmaDeviceResources *dev_res, uint32_t pd_handle)
{
    return res_tbl_get(&dev_res->pd_tbl, pd_handle);
}

void rdma_rm_dealloc_pd(RdmaDeviceResources *dev_res, uint32_t pd_handle)
{
    RdmaRmPD *pd = rdma_rm_get_pd(dev_res, pd_handle);

    if (pd) {
        rdma_backend_destroy_pd(&pd->backend_pd);
        res_tbl_dealloc(&dev_res->pd_tbl, pd_handle);
    }
}

int rdma_rm_alloc_mr(RdmaDeviceResources *dev_res, uint32_t pd_handle,
                     uint64_t guest_start, size_t guest_length, void *host_virt,
                     int access_flags, uint32_t *mr_handle, uint32_t *lkey,
                     uint32_t *rkey)
{
    RdmaRmMR *mr;
    int ret = 0;
    RdmaRmPD *pd;

    pd = rdma_rm_get_pd(dev_res, pd_handle);
    if (!pd) {
        pr_dbg("Invalid PD\n");
        return -EINVAL;
    }

    mr = res_tbl_alloc(&dev_res->mr_tbl, mr_handle);
    if (!mr) {
        pr_dbg("Failed to allocate obj in table\n");
        return -ENOMEM;
    }
    pr_dbg("mr_handle=%d\n", *mr_handle);

    pr_dbg("host_virt=0x%p\n", host_virt);
    pr_dbg("guest_start=0x%" PRIx64 "\n", guest_start);
    pr_dbg("length=%zu\n", guest_length);

    if (host_virt) {
        mr->virt = host_virt;
        mr->start = guest_start;
        mr->length = guest_length;
        mr->virt += (mr->start & (TARGET_PAGE_SIZE - 1));

        ret = rdma_backend_create_mr(&mr->backend_mr, &pd->backend_pd, mr->virt,
                                     mr->length, access_flags);
        if (ret) {
            pr_dbg("Fail in rdma_backend_create_mr, err=%d\n", ret);
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
    res_tbl_dealloc(&dev_res->mr_tbl, *mr_handle);

    return ret;
}

RdmaRmMR *rdma_rm_get_mr(RdmaDeviceResources *dev_res, uint32_t mr_handle)
{
    return res_tbl_get(&dev_res->mr_tbl, mr_handle);
}

void rdma_rm_dealloc_mr(RdmaDeviceResources *dev_res, uint32_t mr_handle)
{
    RdmaRmMR *mr = rdma_rm_get_mr(dev_res, mr_handle);

    if (mr) {
        rdma_backend_destroy_mr(&mr->backend_mr);
        pr_dbg("start=0x%" PRIx64 "\n", mr->start);
        if (mr->start) {
            mr->virt -= (mr->start & (TARGET_PAGE_SIZE - 1));
            munmap(mr->virt, mr->length);
        }
        res_tbl_dealloc(&dev_res->mr_tbl, mr_handle);
    }
}

int rdma_rm_alloc_uc(RdmaDeviceResources *dev_res, uint32_t pfn,
                     uint32_t *uc_handle)
{
    RdmaRmUC *uc;

    /* TODO: Need to make sure pfn is between bar start address and
     * bsd+RDMA_BAR2_UAR_SIZE
    if (pfn > RDMA_BAR2_UAR_SIZE) {
        pr_err("pfn out of range (%d > %d)\n", pfn, RDMA_BAR2_UAR_SIZE);
        return -ENOMEM;
    }
    */

    uc = res_tbl_alloc(&dev_res->uc_tbl, uc_handle);
    if (!uc) {
        return -ENOMEM;
    }

    return 0;
}

RdmaRmUC *rdma_rm_get_uc(RdmaDeviceResources *dev_res, uint32_t uc_handle)
{
    return res_tbl_get(&dev_res->uc_tbl, uc_handle);
}

void rdma_rm_dealloc_uc(RdmaDeviceResources *dev_res, uint32_t uc_handle)
{
    RdmaRmUC *uc = rdma_rm_get_uc(dev_res, uc_handle);

    if (uc) {
        res_tbl_dealloc(&dev_res->uc_tbl, uc_handle);
    }
}

RdmaRmCQ *rdma_rm_get_cq(RdmaDeviceResources *dev_res, uint32_t cq_handle)
{
    return res_tbl_get(&dev_res->cq_tbl, cq_handle);
}

int rdma_rm_alloc_cq(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                     uint32_t cqe, uint32_t *cq_handle, void *opaque)
{
    int rc;
    RdmaRmCQ *cq;

    cq = res_tbl_alloc(&dev_res->cq_tbl, cq_handle);
    if (!cq) {
        return -ENOMEM;
    }

    cq->opaque = opaque;
    cq->notify = false;

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

    pr_dbg("cq_handle=%d, notify=0x%x\n", cq_handle, notify);

    cq = rdma_rm_get_cq(dev_res, cq_handle);
    if (!cq) {
        return;
    }

    cq->notify = notify;
    pr_dbg("notify=%d\n", cq->notify);
}

void rdma_rm_dealloc_cq(RdmaDeviceResources *dev_res, uint32_t cq_handle)
{
    RdmaRmCQ *cq;

    cq = rdma_rm_get_cq(dev_res, cq_handle);
    if (!cq) {
        return;
    }

    rdma_backend_destroy_cq(&cq->backend_cq);

    res_tbl_dealloc(&dev_res->cq_tbl, cq_handle);
}

RdmaRmQP *rdma_rm_get_qp(RdmaDeviceResources *dev_res, uint32_t qpn)
{
    GBytes *key = g_bytes_new(&qpn, sizeof(qpn));

    RdmaRmQP *qp = g_hash_table_lookup(dev_res->qp_hash, key);

    g_bytes_unref(key);

    return qp;
}

int rdma_rm_alloc_qp(RdmaDeviceResources *dev_res, uint32_t pd_handle,
                     uint8_t qp_type, uint32_t max_send_wr,
                     uint32_t max_send_sge, uint32_t send_cq_handle,
                     uint32_t max_recv_wr, uint32_t max_recv_sge,
                     uint32_t recv_cq_handle, void *opaque, uint32_t *qpn)
{
    int rc;
    RdmaRmQP *qp;
    RdmaRmCQ *scq, *rcq;
    RdmaRmPD *pd;
    uint32_t rm_qpn;

    pr_dbg("qp_type=%d\n", qp_type);

    pd = rdma_rm_get_pd(dev_res, pd_handle);
    if (!pd) {
        pr_err("Invalid pd handle (%d)\n", pd_handle);
        return -EINVAL;
    }

    scq = rdma_rm_get_cq(dev_res, send_cq_handle);
    rcq = rdma_rm_get_cq(dev_res, recv_cq_handle);

    if (!scq || !rcq) {
        pr_err("Invalid send_cqn or recv_cqn (%d, %d)\n",
               send_cq_handle, recv_cq_handle);
        return -EINVAL;
    }

    qp = res_tbl_alloc(&dev_res->qp_tbl, &rm_qpn);
    if (!qp) {
        return -ENOMEM;
    }
    pr_dbg("rm_qpn=%d\n", rm_qpn);

    qp->qpn = rm_qpn;
    qp->qp_state = IBV_QPS_RESET;
    qp->qp_type = qp_type;
    qp->send_cq_handle = send_cq_handle;
    qp->recv_cq_handle = recv_cq_handle;
    qp->opaque = opaque;

    rc = rdma_backend_create_qp(&qp->backend_qp, qp_type, &pd->backend_pd,
                                &scq->backend_cq, &rcq->backend_cq, max_send_wr,
                                max_recv_wr, max_send_sge, max_recv_sge);
    if (rc) {
        rc = -EIO;
        goto out_dealloc_qp;
    }

    *qpn = rdma_backend_qpn(&qp->backend_qp);
    pr_dbg("rm_qpn=%d, backend_qpn=0x%x\n", rm_qpn, *qpn);
    g_hash_table_insert(dev_res->qp_hash, g_bytes_new(qpn, sizeof(*qpn)), qp);

    return 0;

out_dealloc_qp:
    res_tbl_dealloc(&dev_res->qp_tbl, qp->qpn);

    return rc;
}

int rdma_rm_modify_qp(RdmaDeviceResources *dev_res, RdmaBackendDev *backend_dev,
                      uint32_t qp_handle, uint32_t attr_mask,
                      union ibv_gid *dgid, uint32_t dqpn,
                      enum ibv_qp_state qp_state, uint32_t qkey,
                      uint32_t rq_psn, uint32_t sq_psn)
{
    RdmaRmQP *qp;
    int ret;

    pr_dbg("qpn=0x%x\n", qp_handle);

    qp = rdma_rm_get_qp(dev_res, qp_handle);
    if (!qp) {
        return -EINVAL;
    }

    pr_dbg("qp_type=%d\n", qp->qp_type);
    pr_dbg("attr_mask=0x%x\n", attr_mask);

    if (qp->qp_type == IBV_QPT_SMI) {
        pr_dbg("QP0 unsupported\n");
        return -EPERM;
    } else if (qp->qp_type == IBV_QPT_GSI) {
        pr_dbg("QP1\n");
        return 0;
    }

    if (attr_mask & IBV_QP_STATE) {
        qp->qp_state = qp_state;
        pr_dbg("qp_state=%d\n", qp->qp_state);

        if (qp->qp_state == IBV_QPS_INIT) {
            ret = rdma_backend_qp_state_init(backend_dev, &qp->backend_qp,
                                             qp->qp_type, qkey);
            if (ret) {
                return -EIO;
            }
        }

        if (qp->qp_state == IBV_QPS_RTR) {
            ret = rdma_backend_qp_state_rtr(backend_dev, &qp->backend_qp,
                                            qp->qp_type, dgid, dqpn, rq_psn,
                                            qkey, attr_mask & IBV_QP_QKEY);
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

    pr_dbg("qpn=0x%x\n", qp_handle);

    qp = rdma_rm_get_qp(dev_res, qp_handle);
    if (!qp) {
        return -EINVAL;
    }

    pr_dbg("qp_type=%d\n", qp->qp_type);

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

    rdma_backend_destroy_qp(&qp->backend_qp);

    res_tbl_dealloc(&dev_res->qp_tbl, qp->qpn);
}

void *rdma_rm_get_cqe_ctx(RdmaDeviceResources *dev_res, uint32_t cqe_ctx_id)
{
    void **cqe_ctx;

    cqe_ctx = res_tbl_get(&dev_res->cqe_ctx_tbl, cqe_ctx_id);
    if (!cqe_ctx) {
        return NULL;
    }

    pr_dbg("ctx=%p\n", *cqe_ctx);

    return *cqe_ctx;
}

int rdma_rm_alloc_cqe_ctx(RdmaDeviceResources *dev_res, uint32_t *cqe_ctx_id,
                          void *ctx)
{
    void **cqe_ctx;

    cqe_ctx = res_tbl_alloc(&dev_res->cqe_ctx_tbl, cqe_ctx_id);
    if (!cqe_ctx) {
        return -ENOMEM;
    }

    pr_dbg("ctx=%p\n", ctx);
    *cqe_ctx = ctx;

    return 0;
}

void rdma_rm_dealloc_cqe_ctx(RdmaDeviceResources *dev_res, uint32_t cqe_ctx_id)
{
    res_tbl_dealloc(&dev_res->cqe_ctx_tbl, cqe_ctx_id);
}

static void destroy_qp_hash_key(gpointer data)
{
    g_bytes_unref(data);
}

int rdma_rm_init(RdmaDeviceResources *dev_res, struct ibv_device_attr *dev_attr,
                 Error **errp)
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

    return 0;
}

void rdma_rm_fini(RdmaDeviceResources *dev_res)
{
    res_tbl_free(&dev_res->uc_tbl);
    res_tbl_free(&dev_res->cqe_ctx_tbl);
    res_tbl_free(&dev_res->qp_tbl);
    res_tbl_free(&dev_res->mr_tbl);
    res_tbl_free(&dev_res->cq_tbl);
    res_tbl_free(&dev_res->pd_tbl);

    g_hash_table_destroy(dev_res->qp_hash);
}
