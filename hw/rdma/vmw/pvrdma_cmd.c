/*
 * QEMU paravirtual RDMA - Command channel
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
#include "cpu.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"

#include "../rdma_backend.h"
#include "../rdma_rm.h"
#include "../rdma_utils.h"

#include "trace.h"
#include "pvrdma.h"
#include "standard-headers/rdma/vmw_pvrdma-abi.h"

static void *pvrdma_map_to_pdir(PCIDevice *pdev, uint64_t pdir_dma,
                                uint32_t nchunks, size_t length)
{
    uint64_t *dir, *tbl;
    int tbl_idx, dir_idx, addr_idx;
    void *host_virt = NULL, *curr_page;

    if (!nchunks) {
        rdma_error_report("Got nchunks=0");
        return NULL;
    }

    length = ROUND_UP(length, TARGET_PAGE_SIZE);
    if (nchunks * TARGET_PAGE_SIZE != length) {
        rdma_error_report("Invalid nchunks/length (%u, %lu)", nchunks,
                          (unsigned long)length);
        return NULL;
    }

    dir = rdma_pci_dma_map(pdev, pdir_dma, TARGET_PAGE_SIZE);
    if (!dir) {
        rdma_error_report("Failed to map to page directory");
        return NULL;
    }

    tbl = rdma_pci_dma_map(pdev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        rdma_error_report("Failed to map to page table 0");
        goto out_unmap_dir;
    }

    curr_page = rdma_pci_dma_map(pdev, (dma_addr_t)tbl[0], TARGET_PAGE_SIZE);
    if (!curr_page) {
        rdma_error_report("Failed to map the page 0");
        goto out_unmap_tbl;
    }

    host_virt = mremap(curr_page, 0, length, MREMAP_MAYMOVE);
    if (host_virt == MAP_FAILED) {
        host_virt = NULL;
        rdma_error_report("Failed to remap memory for host_virt");
        goto out_unmap_tbl;
    }
    trace_pvrdma_map_to_pdir_host_virt(curr_page, host_virt);

    rdma_pci_dma_unmap(pdev, curr_page, TARGET_PAGE_SIZE);

    dir_idx = 0;
    tbl_idx = 1;
    addr_idx = 1;
    while (addr_idx < nchunks) {
        if (tbl_idx == TARGET_PAGE_SIZE / sizeof(uint64_t)) {
            tbl_idx = 0;
            dir_idx++;
            rdma_pci_dma_unmap(pdev, tbl, TARGET_PAGE_SIZE);
            tbl = rdma_pci_dma_map(pdev, dir[dir_idx], TARGET_PAGE_SIZE);
            if (!tbl) {
                rdma_error_report("Failed to map to page table %d", dir_idx);
                goto out_unmap_host_virt;
            }
        }

        curr_page = rdma_pci_dma_map(pdev, (dma_addr_t)tbl[tbl_idx],
                                     TARGET_PAGE_SIZE);
        if (!curr_page) {
            rdma_error_report("Failed to map to page %d, dir %d", tbl_idx,
                              dir_idx);
            goto out_unmap_host_virt;
        }

        mremap(curr_page, 0, TARGET_PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED,
               host_virt + TARGET_PAGE_SIZE * addr_idx);

        trace_pvrdma_map_to_pdir_next_page(addr_idx, curr_page, host_virt +
                                           TARGET_PAGE_SIZE * addr_idx);

        rdma_pci_dma_unmap(pdev, curr_page, TARGET_PAGE_SIZE);

        addr_idx++;

        tbl_idx++;
    }

    goto out_unmap_tbl;

out_unmap_host_virt:
    munmap(host_virt, length);
    host_virt = NULL;

out_unmap_tbl:
    rdma_pci_dma_unmap(pdev, tbl, TARGET_PAGE_SIZE);

out_unmap_dir:
    rdma_pci_dma_unmap(pdev, dir, TARGET_PAGE_SIZE);

    return host_virt;
}

static int query_port(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_query_port *cmd = &req->query_port;
    struct pvrdma_cmd_query_port_resp *resp = &rsp->query_port_resp;
    struct pvrdma_port_attr attrs = {};

    if (cmd->port_num > MAX_PORTS) {
        return -EINVAL;
    }

    if (rdma_backend_query_port(&dev->backend_dev,
                                (struct ibv_port_attr *)&attrs)) {
        return -ENOMEM;
    }

    memset(resp, 0, sizeof(*resp));

    resp->attrs.state = dev->func0->device_active ? attrs.state :
                                                    PVRDMA_PORT_DOWN;
    resp->attrs.max_mtu = attrs.max_mtu;
    resp->attrs.active_mtu = attrs.active_mtu;
    resp->attrs.phys_state = attrs.phys_state;
    resp->attrs.gid_tbl_len = MIN(MAX_PORT_GIDS, attrs.gid_tbl_len);
    resp->attrs.max_msg_sz = 1024;
    resp->attrs.pkey_tbl_len = MIN(MAX_PORT_PKEYS, attrs.pkey_tbl_len);
    resp->attrs.active_width = 1;
    resp->attrs.active_speed = 1;

    return 0;
}

static int query_pkey(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_query_pkey *cmd = &req->query_pkey;
    struct pvrdma_cmd_query_pkey_resp *resp = &rsp->query_pkey_resp;

    if (cmd->port_num > MAX_PORTS) {
        return -EINVAL;
    }

    if (cmd->index > MAX_PKEYS) {
        return -EINVAL;
    }

    memset(resp, 0, sizeof(*resp));

    resp->pkey = PVRDMA_PKEY;

    return 0;
}

static int create_pd(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_pd *cmd = &req->create_pd;
    struct pvrdma_cmd_create_pd_resp *resp = &rsp->create_pd_resp;

    memset(resp, 0, sizeof(*resp));
    return rdma_rm_alloc_pd(&dev->rdma_dev_res, &dev->backend_dev,
                            &resp->pd_handle, cmd->ctx_handle);
}

static int destroy_pd(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_pd *cmd = &req->destroy_pd;

    rdma_rm_dealloc_pd(&dev->rdma_dev_res, cmd->pd_handle);

    return 0;
}

static int create_mr(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_mr *cmd = &req->create_mr;
    struct pvrdma_cmd_create_mr_resp *resp = &rsp->create_mr_resp;
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    void *host_virt = NULL;
    int rc = 0;

    memset(resp, 0, sizeof(*resp));

    if (!(cmd->flags & PVRDMA_MR_FLAG_DMA)) {
        host_virt = pvrdma_map_to_pdir(pci_dev, cmd->pdir_dma, cmd->nchunks,
                                       cmd->length);
        if (!host_virt) {
            rdma_error_report("Failed to map to pdir");
            return -EINVAL;
        }
    }

    rc = rdma_rm_alloc_mr(&dev->rdma_dev_res, cmd->pd_handle, cmd->start,
                          cmd->length, host_virt, cmd->access_flags,
                          &resp->mr_handle, &resp->lkey, &resp->rkey);
    if (rc && host_virt) {
        munmap(host_virt, cmd->length);
    }

    return rc;
}

static int destroy_mr(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_mr *cmd = &req->destroy_mr;

    rdma_rm_dealloc_mr(&dev->rdma_dev_res, cmd->mr_handle);

    return 0;
}

static int create_cq_ring(PCIDevice *pci_dev , PvrdmaRing **ring,
                          uint64_t pdir_dma, uint32_t nchunks, uint32_t cqe)
{
    uint64_t *dir = NULL, *tbl = NULL;
    PvrdmaRing *r;
    int rc = -EINVAL;
    char ring_name[MAX_RING_NAME_SZ];

    if (!nchunks || nchunks > PVRDMA_MAX_FAST_REG_PAGES) {
        rdma_error_report("Got invalid nchunks: %d", nchunks);
        return rc;
    }

    dir = rdma_pci_dma_map(pci_dev, pdir_dma, TARGET_PAGE_SIZE);
    if (!dir) {
        rdma_error_report("Failed to map to CQ page directory");
        goto out;
    }

    tbl = rdma_pci_dma_map(pci_dev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        rdma_error_report("Failed to map to CQ page table");
        goto out;
    }

    r = g_malloc(sizeof(*r));
    *ring = r;

    r->ring_state = rdma_pci_dma_map(pci_dev, tbl[0], TARGET_PAGE_SIZE);

    if (!r->ring_state) {
        rdma_error_report("Failed to map to CQ ring state");
        goto out_free_ring;
    }

    sprintf(ring_name, "cq_ring_%" PRIx64, pdir_dma);
    rc = pvrdma_ring_init(r, ring_name, pci_dev, &r->ring_state[1],
                          cqe, sizeof(struct pvrdma_cqe),
                          /* first page is ring state */
                          (dma_addr_t *)&tbl[1], nchunks - 1);
    if (rc) {
        goto out_unmap_ring_state;
    }

    goto out;

out_unmap_ring_state:
    /* ring_state was in slot 1, not 0 so need to jump back */
    rdma_pci_dma_unmap(pci_dev, --r->ring_state, TARGET_PAGE_SIZE);

out_free_ring:
    g_free(r);

out:
    rdma_pci_dma_unmap(pci_dev, tbl, TARGET_PAGE_SIZE);
    rdma_pci_dma_unmap(pci_dev, dir, TARGET_PAGE_SIZE);

    return rc;
}

static void destroy_cq_ring(PvrdmaRing *ring)
{
    pvrdma_ring_free(ring);
    /* ring_state was in slot 1, not 0 so need to jump back */
    rdma_pci_dma_unmap(ring->dev, --ring->ring_state, TARGET_PAGE_SIZE);
    g_free(ring);
}

static int create_cq(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_cq *cmd = &req->create_cq;
    struct pvrdma_cmd_create_cq_resp *resp = &rsp->create_cq_resp;
    PvrdmaRing *ring = NULL;
    int rc;

    memset(resp, 0, sizeof(*resp));

    resp->cqe = cmd->cqe;

    rc = create_cq_ring(PCI_DEVICE(dev), &ring, cmd->pdir_dma, cmd->nchunks,
                        cmd->cqe);
    if (rc) {
        return rc;
    }

    rc = rdma_rm_alloc_cq(&dev->rdma_dev_res, &dev->backend_dev, cmd->cqe,
                          &resp->cq_handle, ring);
    if (rc) {
        destroy_cq_ring(ring);
    }

    resp->cqe = cmd->cqe;

    return rc;
}

static int destroy_cq(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_cq *cmd = &req->destroy_cq;
    RdmaRmCQ *cq;
    PvrdmaRing *ring;

    cq = rdma_rm_get_cq(&dev->rdma_dev_res, cmd->cq_handle);
    if (!cq) {
        rdma_error_report("Got invalid CQ handle");
        return -EINVAL;
    }

    ring = (PvrdmaRing *)cq->opaque;
    destroy_cq_ring(ring);

    rdma_rm_dealloc_cq(&dev->rdma_dev_res, cmd->cq_handle);

    return 0;
}

static int create_qp_rings(PCIDevice *pci_dev, uint64_t pdir_dma,
                           PvrdmaRing **rings, uint32_t scqe, uint32_t smax_sge,
                           uint32_t spages, uint32_t rcqe, uint32_t rmax_sge,
                           uint32_t rpages, uint8_t is_srq)
{
    uint64_t *dir = NULL, *tbl = NULL;
    PvrdmaRing *sr, *rr;
    int rc = -EINVAL;
    char ring_name[MAX_RING_NAME_SZ];
    uint32_t wqe_sz;

    if (!spages || spages > PVRDMA_MAX_FAST_REG_PAGES) {
        rdma_error_report("Got invalid send page count for QP ring: %d",
                          spages);
        return rc;
    }

    if (!is_srq && (!rpages || rpages > PVRDMA_MAX_FAST_REG_PAGES)) {
        rdma_error_report("Got invalid recv page count for QP ring: %d",
                          rpages);
        return rc;
    }

    dir = rdma_pci_dma_map(pci_dev, pdir_dma, TARGET_PAGE_SIZE);
    if (!dir) {
        rdma_error_report("Failed to map to QP page directory");
        goto out;
    }

    tbl = rdma_pci_dma_map(pci_dev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        rdma_error_report("Failed to map to QP page table");
        goto out;
    }

    if (!is_srq) {
        sr = g_malloc(2 * sizeof(*rr));
        rr = &sr[1];
    } else {
        sr = g_malloc(sizeof(*sr));
    }

    *rings = sr;

    /* Create send ring */
    sr->ring_state = rdma_pci_dma_map(pci_dev, tbl[0], TARGET_PAGE_SIZE);
    if (!sr->ring_state) {
        rdma_error_report("Failed to map to QP ring state");
        goto out_free_sr_mem;
    }

    wqe_sz = pow2ceil(sizeof(struct pvrdma_sq_wqe_hdr) +
                      sizeof(struct pvrdma_sge) * smax_sge - 1);

    sprintf(ring_name, "qp_sring_%" PRIx64, pdir_dma);
    rc = pvrdma_ring_init(sr, ring_name, pci_dev, sr->ring_state,
                          scqe, wqe_sz, (dma_addr_t *)&tbl[1], spages);
    if (rc) {
        goto out_unmap_ring_state;
    }

    if (!is_srq) {
        /* Create recv ring */
        rr->ring_state = &sr->ring_state[1];
        wqe_sz = pow2ceil(sizeof(struct pvrdma_rq_wqe_hdr) +
                          sizeof(struct pvrdma_sge) * rmax_sge - 1);
        sprintf(ring_name, "qp_rring_%" PRIx64, pdir_dma);
        rc = pvrdma_ring_init(rr, ring_name, pci_dev, rr->ring_state,
                              rcqe, wqe_sz, (dma_addr_t *)&tbl[1 + spages],
                              rpages);
        if (rc) {
            goto out_free_sr;
        }
    }

    goto out;

out_free_sr:
    pvrdma_ring_free(sr);

out_unmap_ring_state:
    rdma_pci_dma_unmap(pci_dev, sr->ring_state, TARGET_PAGE_SIZE);

out_free_sr_mem:
    g_free(sr);

out:
    rdma_pci_dma_unmap(pci_dev, tbl, TARGET_PAGE_SIZE);
    rdma_pci_dma_unmap(pci_dev, dir, TARGET_PAGE_SIZE);

    return rc;
}

static void destroy_qp_rings(PvrdmaRing *ring, uint8_t is_srq)
{
    pvrdma_ring_free(&ring[0]);
    if (!is_srq) {
        pvrdma_ring_free(&ring[1]);
    }

    rdma_pci_dma_unmap(ring->dev, ring->ring_state, TARGET_PAGE_SIZE);
    g_free(ring);
}

static int create_qp(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_qp *cmd = &req->create_qp;
    struct pvrdma_cmd_create_qp_resp *resp = &rsp->create_qp_resp;
    PvrdmaRing *rings = NULL;
    int rc;

    memset(resp, 0, sizeof(*resp));

    rc = create_qp_rings(PCI_DEVICE(dev), cmd->pdir_dma, &rings,
                         cmd->max_send_wr, cmd->max_send_sge, cmd->send_chunks,
                         cmd->max_recv_wr, cmd->max_recv_sge,
                         cmd->total_chunks - cmd->send_chunks - 1, cmd->is_srq);
    if (rc) {
        return rc;
    }

    rc = rdma_rm_alloc_qp(&dev->rdma_dev_res, cmd->pd_handle, cmd->qp_type,
                          cmd->max_send_wr, cmd->max_send_sge,
                          cmd->send_cq_handle, cmd->max_recv_wr,
                          cmd->max_recv_sge, cmd->recv_cq_handle, rings,
                          &resp->qpn, cmd->is_srq, cmd->srq_handle);
    if (rc) {
        destroy_qp_rings(rings, cmd->is_srq);
        return rc;
    }

    resp->max_send_wr = cmd->max_send_wr;
    resp->max_recv_wr = cmd->max_recv_wr;
    resp->max_send_sge = cmd->max_send_sge;
    resp->max_recv_sge = cmd->max_recv_sge;
    resp->max_inline_data = cmd->max_inline_data;

    return 0;
}

static int modify_qp(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_modify_qp *cmd = &req->modify_qp;

    /* No need to verify sgid_index since it is u8 */

    return rdma_rm_modify_qp(&dev->rdma_dev_res, &dev->backend_dev,
                             cmd->qp_handle, cmd->attr_mask,
                             cmd->attrs.ah_attr.grh.sgid_index,
                             (union ibv_gid *)&cmd->attrs.ah_attr.grh.dgid,
                             cmd->attrs.dest_qp_num,
                             (enum ibv_qp_state)cmd->attrs.qp_state,
                             cmd->attrs.qkey, cmd->attrs.rq_psn,
                             cmd->attrs.sq_psn);
}

static int query_qp(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_query_qp *cmd = &req->query_qp;
    struct pvrdma_cmd_query_qp_resp *resp = &rsp->query_qp_resp;
    struct ibv_qp_init_attr init_attr;

    memset(resp, 0, sizeof(*resp));

    return rdma_rm_query_qp(&dev->rdma_dev_res, &dev->backend_dev,
                            cmd->qp_handle,
                            (struct ibv_qp_attr *)&resp->attrs,
                            cmd->attr_mask,
                            &init_attr);
}

static int destroy_qp(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_qp *cmd = &req->destroy_qp;
    RdmaRmQP *qp;
    PvrdmaRing *ring;

    qp = rdma_rm_get_qp(&dev->rdma_dev_res, cmd->qp_handle);
    if (!qp) {
        return -EINVAL;
    }

    ring = (PvrdmaRing *)qp->opaque;
    destroy_qp_rings(ring, qp->is_srq);
    rdma_rm_dealloc_qp(&dev->rdma_dev_res, cmd->qp_handle);

    return 0;
}

static int create_bind(PVRDMADev *dev, union pvrdma_cmd_req *req,
                       union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_bind *cmd = &req->create_bind;
    union ibv_gid *gid = (union ibv_gid *)&cmd->new_gid;

    if (cmd->index >= MAX_PORT_GIDS) {
        return -EINVAL;
    }

    return rdma_rm_add_gid(&dev->rdma_dev_res, &dev->backend_dev,
                           dev->backend_eth_device_name, gid, cmd->index);
}

static int destroy_bind(PVRDMADev *dev, union pvrdma_cmd_req *req,
                        union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_bind *cmd = &req->destroy_bind;

    if (cmd->index >= MAX_PORT_GIDS) {
        return -EINVAL;
    }

    return rdma_rm_del_gid(&dev->rdma_dev_res, &dev->backend_dev,
                           dev->backend_eth_device_name, cmd->index);
}

static int create_uc(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_uc *cmd = &req->create_uc;
    struct pvrdma_cmd_create_uc_resp *resp = &rsp->create_uc_resp;

    memset(resp, 0, sizeof(*resp));
    return rdma_rm_alloc_uc(&dev->rdma_dev_res, cmd->pfn, &resp->ctx_handle);
}

static int destroy_uc(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_uc *cmd = &req->destroy_uc;

    rdma_rm_dealloc_uc(&dev->rdma_dev_res, cmd->ctx_handle);

    return 0;
}

static int create_srq_ring(PCIDevice *pci_dev, PvrdmaRing **ring,
                           uint64_t pdir_dma, uint32_t max_wr,
                           uint32_t max_sge, uint32_t nchunks)
{
    uint64_t *dir = NULL, *tbl = NULL;
    PvrdmaRing *r;
    int rc = -EINVAL;
    char ring_name[MAX_RING_NAME_SZ];
    uint32_t wqe_sz;

    if (!nchunks || nchunks > PVRDMA_MAX_FAST_REG_PAGES) {
        rdma_error_report("Got invalid page count for SRQ ring: %d",
                          nchunks);
        return rc;
    }

    dir = rdma_pci_dma_map(pci_dev, pdir_dma, TARGET_PAGE_SIZE);
    if (!dir) {
        rdma_error_report("Failed to map to SRQ page directory");
        goto out;
    }

    tbl = rdma_pci_dma_map(pci_dev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        rdma_error_report("Failed to map to SRQ page table");
        goto out;
    }

    r = g_malloc(sizeof(*r));
    *ring = r;

    r->ring_state = rdma_pci_dma_map(pci_dev, tbl[0], TARGET_PAGE_SIZE);
    if (!r->ring_state) {
        rdma_error_report("Failed to map tp SRQ ring state");
        goto out_free_ring_mem;
    }

    wqe_sz = pow2ceil(sizeof(struct pvrdma_rq_wqe_hdr) +
                      sizeof(struct pvrdma_sge) * max_sge - 1);
    sprintf(ring_name, "srq_ring_%" PRIx64, pdir_dma);
    rc = pvrdma_ring_init(r, ring_name, pci_dev, &r->ring_state[1], max_wr,
                          wqe_sz, (dma_addr_t *)&tbl[1], nchunks - 1);
    if (rc) {
        goto out_unmap_ring_state;
    }

    goto out;

out_unmap_ring_state:
    rdma_pci_dma_unmap(pci_dev, r->ring_state, TARGET_PAGE_SIZE);

out_free_ring_mem:
    g_free(r);

out:
    rdma_pci_dma_unmap(pci_dev, tbl, TARGET_PAGE_SIZE);
    rdma_pci_dma_unmap(pci_dev, dir, TARGET_PAGE_SIZE);

    return rc;
}

static void destroy_srq_ring(PvrdmaRing *ring)
{
    pvrdma_ring_free(ring);
    rdma_pci_dma_unmap(ring->dev, ring->ring_state, TARGET_PAGE_SIZE);
    g_free(ring);
}

static int create_srq(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_srq *cmd = &req->create_srq;
    struct pvrdma_cmd_create_srq_resp *resp = &rsp->create_srq_resp;
    PvrdmaRing *ring = NULL;
    int rc;

    memset(resp, 0, sizeof(*resp));

    rc = create_srq_ring(PCI_DEVICE(dev), &ring, cmd->pdir_dma,
                         cmd->attrs.max_wr, cmd->attrs.max_sge,
                         cmd->nchunks);
    if (rc) {
        return rc;
    }

    rc = rdma_rm_alloc_srq(&dev->rdma_dev_res, cmd->pd_handle,
                           cmd->attrs.max_wr, cmd->attrs.max_sge,
                           cmd->attrs.srq_limit, &resp->srqn, ring);
    if (rc) {
        destroy_srq_ring(ring);
        return rc;
    }

    return 0;
}

static int query_srq(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_query_srq *cmd = &req->query_srq;
    struct pvrdma_cmd_query_srq_resp *resp = &rsp->query_srq_resp;

    memset(resp, 0, sizeof(*resp));

    return rdma_rm_query_srq(&dev->rdma_dev_res, cmd->srq_handle,
                             (struct ibv_srq_attr *)&resp->attrs);
}

static int modify_srq(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_modify_srq *cmd = &req->modify_srq;

    /* Only support SRQ limit */
    if (!(cmd->attr_mask & IBV_SRQ_LIMIT) ||
        (cmd->attr_mask & IBV_SRQ_MAX_WR))
            return -EINVAL;

    return rdma_rm_modify_srq(&dev->rdma_dev_res, cmd->srq_handle,
                              (struct ibv_srq_attr *)&cmd->attrs,
                              cmd->attr_mask);
}

static int destroy_srq(PVRDMADev *dev, union pvrdma_cmd_req *req,
                       union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_srq *cmd = &req->destroy_srq;
    RdmaRmSRQ *srq;
    PvrdmaRing *ring;

    srq = rdma_rm_get_srq(&dev->rdma_dev_res, cmd->srq_handle);
    if (!srq) {
        return -EINVAL;
    }

    ring = (PvrdmaRing *)srq->opaque;
    destroy_srq_ring(ring);
    rdma_rm_dealloc_srq(&dev->rdma_dev_res, cmd->srq_handle);

    return 0;
}

struct cmd_handler {
    uint32_t cmd;
    uint32_t ack;
    int (*exec)(PVRDMADev *dev, union pvrdma_cmd_req *req,
            union pvrdma_cmd_resp *rsp);
};

static struct cmd_handler cmd_handlers[] = {
    {PVRDMA_CMD_QUERY_PORT,   PVRDMA_CMD_QUERY_PORT_RESP,        query_port},
    {PVRDMA_CMD_QUERY_PKEY,   PVRDMA_CMD_QUERY_PKEY_RESP,        query_pkey},
    {PVRDMA_CMD_CREATE_PD,    PVRDMA_CMD_CREATE_PD_RESP,         create_pd},
    {PVRDMA_CMD_DESTROY_PD,   PVRDMA_CMD_DESTROY_PD_RESP_NOOP,   destroy_pd},
    {PVRDMA_CMD_CREATE_MR,    PVRDMA_CMD_CREATE_MR_RESP,         create_mr},
    {PVRDMA_CMD_DESTROY_MR,   PVRDMA_CMD_DESTROY_MR_RESP_NOOP,   destroy_mr},
    {PVRDMA_CMD_CREATE_CQ,    PVRDMA_CMD_CREATE_CQ_RESP,         create_cq},
    {PVRDMA_CMD_RESIZE_CQ,    PVRDMA_CMD_RESIZE_CQ_RESP,         NULL},
    {PVRDMA_CMD_DESTROY_CQ,   PVRDMA_CMD_DESTROY_CQ_RESP_NOOP,   destroy_cq},
    {PVRDMA_CMD_CREATE_QP,    PVRDMA_CMD_CREATE_QP_RESP,         create_qp},
    {PVRDMA_CMD_MODIFY_QP,    PVRDMA_CMD_MODIFY_QP_RESP,         modify_qp},
    {PVRDMA_CMD_QUERY_QP,     PVRDMA_CMD_QUERY_QP_RESP,          query_qp},
    {PVRDMA_CMD_DESTROY_QP,   PVRDMA_CMD_DESTROY_QP_RESP,        destroy_qp},
    {PVRDMA_CMD_CREATE_UC,    PVRDMA_CMD_CREATE_UC_RESP,         create_uc},
    {PVRDMA_CMD_DESTROY_UC,   PVRDMA_CMD_DESTROY_UC_RESP_NOOP,   destroy_uc},
    {PVRDMA_CMD_CREATE_BIND,  PVRDMA_CMD_CREATE_BIND_RESP_NOOP,  create_bind},
    {PVRDMA_CMD_DESTROY_BIND, PVRDMA_CMD_DESTROY_BIND_RESP_NOOP, destroy_bind},
    {PVRDMA_CMD_CREATE_SRQ,   PVRDMA_CMD_CREATE_SRQ_RESP,        create_srq},
    {PVRDMA_CMD_QUERY_SRQ,    PVRDMA_CMD_QUERY_SRQ_RESP,         query_srq},
    {PVRDMA_CMD_MODIFY_SRQ,   PVRDMA_CMD_MODIFY_SRQ_RESP,        modify_srq},
    {PVRDMA_CMD_DESTROY_SRQ,  PVRDMA_CMD_DESTROY_SRQ_RESP,       destroy_srq},
};

int pvrdma_exec_cmd(PVRDMADev *dev)
{
    int err = 0xFFFF;
    DSRInfo *dsr_info;

    dsr_info = &dev->dsr_info;

    if (dsr_info->req->hdr.cmd >= sizeof(cmd_handlers) /
                      sizeof(struct cmd_handler)) {
        rdma_error_report("Unsupported command");
        goto out;
    }

    if (!cmd_handlers[dsr_info->req->hdr.cmd].exec) {
        rdma_error_report("Unsupported command (not implemented yet)");
        goto out;
    }

    err = cmd_handlers[dsr_info->req->hdr.cmd].exec(dev, dsr_info->req,
                                                    dsr_info->rsp);
    dsr_info->rsp->hdr.response = dsr_info->req->hdr.response;
    dsr_info->rsp->hdr.ack = cmd_handlers[dsr_info->req->hdr.cmd].ack;
    dsr_info->rsp->hdr.err = err < 0 ? -err : 0;

    trace_pvrdma_exec_cmd(dsr_info->req->hdr.cmd, dsr_info->rsp->hdr.err);

    dev->stats.commands++;

out:
    set_reg_val(dev, PVRDMA_REG_ERR, err);
    post_interrupt(dev, INTR_VEC_CMD_RING);

    return (err == 0) ? 0 : -EINVAL;
}
