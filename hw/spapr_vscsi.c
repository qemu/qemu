/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Virtual SCSI, aka ibmvscsi
 *
 * Copyright (c) 2010,2011 Benjamin Herrenschmidt, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * TODO:
 *
 *  - Cleanups :-)
 *  - Sort out better how to assign devices to VSCSI instances
 *  - Fix residual counts
 *  - Add indirect descriptors support
 *  - Maybe do autosense (PAPR seems to mandate it, linux doesn't care)
 */
#include "hw.h"
#include "scsi.h"
#include "scsi-defs.h"
#include "net.h" /* Remove that when we can */
#include "srp.h"
#include "hw/qdev.h"
#include "hw/spapr.h"
#include "hw/spapr_vio.h"
#include "hw/ppc-viosrp.h"

#include <libfdt.h>

/*#define DEBUG_VSCSI*/

#ifdef DEBUG_VSCSI
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

/*
 * Virtual SCSI device
 */

/* Random numbers */
#define VSCSI_MAX_SECTORS       4096
#define VSCSI_REQ_LIMIT         24

#define SCSI_SENSE_BUF_SIZE     96
#define SRP_RSP_SENSE_DATA_LEN  18

typedef union vscsi_crq {
    struct viosrp_crq s;
    uint8_t raw[16];
} vscsi_crq;

typedef struct vscsi_req {
    vscsi_crq               crq;
    union viosrp_iu         iu;

    /* SCSI request tracking */
    SCSIRequest             *sreq;
    uint32_t                qtag; /* qemu tag != srp tag */
    int                     lun;
    int                     active;
    long                    data_len;
    int                     writing;
    int                     senselen;
    uint8_t                 sense[SCSI_SENSE_BUF_SIZE];

    /* RDMA related bits */
    uint8_t                 dma_fmt;
    struct srp_direct_buf   ext_desc;
    struct srp_direct_buf   *cur_desc;
    struct srp_indirect_buf *ind_desc;
    int                     local_desc;
    int                     total_desc;
} vscsi_req;


typedef struct {
    VIOsPAPRDevice vdev;
    SCSIBus bus;
    vscsi_req reqs[VSCSI_REQ_LIMIT];
} VSCSIState;

/* XXX Debug only */
static VSCSIState *dbg_vscsi_state;


static struct vscsi_req *vscsi_get_req(VSCSIState *s)
{
    vscsi_req *req;
    int i;

    for (i = 0; i < VSCSI_REQ_LIMIT; i++) {
        req = &s->reqs[i];
        if (!req->active) {
            memset(req, 0, sizeof(*req));
            req->qtag = i;
            req->active = 1;
            return req;
        }
    }
    return NULL;
}

static void vscsi_put_req(vscsi_req *req)
{
    if (req->sreq != NULL) {
        scsi_req_unref(req->sreq);
    }
    req->sreq = NULL;
    req->active = 0;
}

static void vscsi_decode_id_lun(uint64_t srp_lun, int *id, int *lun)
{
    /* XXX Figure that one out properly ! This is crackpot */
    *id = (srp_lun >> 56) & 0x7f;
    *lun = (srp_lun >> 48) & 0xff;
}

static int vscsi_send_iu(VSCSIState *s, vscsi_req *req,
                         uint64_t length, uint8_t format)
{
    long rc, rc1;

    /* First copy the SRP */
    rc = spapr_tce_dma_write(&s->vdev, req->crq.s.IU_data_ptr,
                             &req->iu, length);
    if (rc) {
        fprintf(stderr, "vscsi_send_iu: DMA write failure !\n");
    }

    req->crq.s.valid = 0x80;
    req->crq.s.format = format;
    req->crq.s.reserved = 0x00;
    req->crq.s.timeout = cpu_to_be16(0x0000);
    req->crq.s.IU_length = cpu_to_be16(length);
    req->crq.s.IU_data_ptr = req->iu.srp.rsp.tag; /* right byte order */

    if (rc == 0) {
        req->crq.s.status = 0x99; /* Just needs to be non-zero */
    } else {
        req->crq.s.status = 0x00;
    }

    rc1 = spapr_vio_send_crq(&s->vdev, req->crq.raw);
    if (rc1) {
        fprintf(stderr, "vscsi_send_iu: Error sending response\n");
        return rc1;
    }

    return rc;
}

static void vscsi_makeup_sense(VSCSIState *s, vscsi_req *req,
                               uint8_t key, uint8_t asc, uint8_t ascq)
{
    req->senselen = SRP_RSP_SENSE_DATA_LEN;

    /* Valid bit and 'current errors' */
    req->sense[0] = (0x1 << 7 | 0x70);
    /* Sense key */
    req->sense[2] = key;
    /* Additional sense length */
    req->sense[7] = 0xa; /* 10 bytes */
    /* Additional sense code */
    req->sense[12] = asc;
    req->sense[13] = ascq;
}

static int vscsi_send_rsp(VSCSIState *s, vscsi_req *req,
                          uint8_t status, int32_t res_in, int32_t res_out)
{
    union viosrp_iu *iu = &req->iu;
    uint64_t tag = iu->srp.rsp.tag;
    int total_len = sizeof(iu->srp.rsp);

    dprintf("VSCSI: Sending resp status: 0x%x, "
            "res_in: %d, res_out: %d\n", status, res_in, res_out);

    memset(iu, 0, sizeof(struct srp_rsp));
    iu->srp.rsp.opcode = SRP_RSP;
    iu->srp.rsp.req_lim_delta = cpu_to_be32(1);
    iu->srp.rsp.tag = tag;

    /* Handle residuals */
    if (res_in < 0) {
        iu->srp.rsp.flags |= SRP_RSP_FLAG_DIUNDER;
        res_in = -res_in;
    } else if (res_in) {
        iu->srp.rsp.flags |= SRP_RSP_FLAG_DIOVER;
    }
    if (res_out < 0) {
        iu->srp.rsp.flags |= SRP_RSP_FLAG_DOUNDER;
        res_out = -res_out;
    } else if (res_out) {
        iu->srp.rsp.flags |= SRP_RSP_FLAG_DOOVER;
    }
    iu->srp.rsp.data_in_res_cnt = cpu_to_be32(res_in);
    iu->srp.rsp.data_out_res_cnt = cpu_to_be32(res_out);

    /* We don't do response data */
    /* iu->srp.rsp.flags &= ~SRP_RSP_FLAG_RSPVALID; */
    iu->srp.rsp.resp_data_len = cpu_to_be32(0);

    /* Handle success vs. failure */
    iu->srp.rsp.status = status;
    if (status) {
        iu->srp.rsp.sol_not = (iu->srp.cmd.sol_not & 0x04) >> 2;
        if (req->senselen) {
            req->iu.srp.rsp.flags |= SRP_RSP_FLAG_SNSVALID;
            req->iu.srp.rsp.sense_data_len = cpu_to_be32(req->senselen);
            memcpy(req->iu.srp.rsp.data, req->sense, req->senselen);
            total_len += req->senselen;
        }
    } else {
        iu->srp.rsp.sol_not = (iu->srp.cmd.sol_not & 0x02) >> 1;
    }

    vscsi_send_iu(s, req, total_len, VIOSRP_SRP_FORMAT);
    return 0;
}

static inline void vscsi_swap_desc(struct srp_direct_buf *desc)
{
    desc->va = be64_to_cpu(desc->va);
    desc->len = be32_to_cpu(desc->len);
}

static int vscsi_srp_direct_data(VSCSIState *s, vscsi_req *req,
                                 uint8_t *buf, uint32_t len)
{
    struct srp_direct_buf *md = req->cur_desc;
    uint32_t llen;
    int rc = 0;

    dprintf("VSCSI: direct segment 0x%x bytes, va=0x%llx desc len=0x%x\n",
            len, (unsigned long long)md->va, md->len);

    llen = MIN(len, md->len);
    if (llen) {
        if (req->writing) { /* writing = to device = reading from memory */
            rc = spapr_tce_dma_read(&s->vdev, md->va, buf, llen);
        } else {
            rc = spapr_tce_dma_write(&s->vdev, md->va, buf, llen);
        }
    }
    md->len -= llen;
    md->va += llen;

    if (rc) {
        return -1;
    }
    return llen;
}

static int vscsi_srp_indirect_data(VSCSIState *s, vscsi_req *req,
                                   uint8_t *buf, uint32_t len)
{
    struct srp_direct_buf *td = &req->ind_desc->table_desc;
    struct srp_direct_buf *md = req->cur_desc;
    int rc = 0;
    uint32_t llen, total = 0;

    dprintf("VSCSI: indirect segment 0x%x bytes, td va=0x%llx len=0x%x\n",
            len, (unsigned long long)td->va, td->len);

    /* While we have data ... */
    while (len) {
        /* If we have a descriptor but it's empty, go fetch a new one */
        if (md && md->len == 0) {
            /* More local available, use one */
            if (req->local_desc) {
                md = ++req->cur_desc;
                --req->local_desc;
                --req->total_desc;
                td->va += sizeof(struct srp_direct_buf);
            } else {
                md = req->cur_desc = NULL;
            }
        }
        /* No descriptor at hand, fetch one */
        if (!md) {
            if (!req->total_desc) {
                dprintf("VSCSI:   Out of descriptors !\n");
                break;
            }
            md = req->cur_desc = &req->ext_desc;
            dprintf("VSCSI:   Reading desc from 0x%llx\n",
                    (unsigned long long)td->va);
            rc = spapr_tce_dma_read(&s->vdev, td->va, md,
                                    sizeof(struct srp_direct_buf));
            if (rc) {
                dprintf("VSCSI: tce_dma_read -> %d reading ext_desc\n", rc);
                break;
            }
            vscsi_swap_desc(md);
            td->va += sizeof(struct srp_direct_buf);
            --req->total_desc;
        }
        dprintf("VSCSI:   [desc va=0x%llx,len=0x%x] remaining=0x%x\n",
                (unsigned long long)md->va, md->len, len);

        /* Perform transfer */
        llen = MIN(len, md->len);
        if (req->writing) { /* writing = to device = reading from memory */
            rc = spapr_tce_dma_read(&s->vdev, md->va, buf, llen);
        } else {
            rc = spapr_tce_dma_write(&s->vdev, md->va, buf, llen);
        }
        if (rc) {
            dprintf("VSCSI: tce_dma_r/w(%d) -> %d\n", req->writing, rc);
            break;
        }
        dprintf("VSCSI:     data: %02x %02x %02x %02x...\n",
                buf[0], buf[1], buf[2], buf[3]);

        len -= llen;
        buf += llen;
        total += llen;
        md->va += llen;
        md->len -= llen;
    }
    return rc ? -1 : total;
}

static int vscsi_srp_transfer_data(VSCSIState *s, vscsi_req *req,
                                   int writing, uint8_t *buf, uint32_t len)
{
    int err = 0;

    switch (req->dma_fmt) {
    case SRP_NO_DATA_DESC:
        dprintf("VSCSI: no data desc transfer, skipping 0x%x bytes\n", len);
        break;
    case SRP_DATA_DESC_DIRECT:
        err = vscsi_srp_direct_data(s, req, buf, len);
        break;
    case SRP_DATA_DESC_INDIRECT:
        err = vscsi_srp_indirect_data(s, req, buf, len);
        break;
    }
    return err;
}

/* Bits from linux srp */
static int data_out_desc_size(struct srp_cmd *cmd)
{
    int size = 0;
    uint8_t fmt = cmd->buf_fmt >> 4;

    switch (fmt) {
    case SRP_NO_DATA_DESC:
        break;
    case SRP_DATA_DESC_DIRECT:
        size = sizeof(struct srp_direct_buf);
        break;
    case SRP_DATA_DESC_INDIRECT:
        size = sizeof(struct srp_indirect_buf) +
            sizeof(struct srp_direct_buf)*cmd->data_out_desc_cnt;
        break;
    default:
        break;
    }
    return size;
}

static int vscsi_preprocess_desc(vscsi_req *req)
{
    struct srp_cmd *cmd = &req->iu.srp.cmd;
    int offset, i;

    offset = cmd->add_cdb_len & ~3;

    if (req->writing) {
        req->dma_fmt = cmd->buf_fmt >> 4;
    } else {
        offset += data_out_desc_size(cmd);
        req->dma_fmt = cmd->buf_fmt & ((1U << 4) - 1);
    }

    switch (req->dma_fmt) {
    case SRP_NO_DATA_DESC:
        break;
    case SRP_DATA_DESC_DIRECT:
        req->cur_desc = (struct srp_direct_buf *)(cmd->add_data + offset);
        req->total_desc = req->local_desc = 1;
        vscsi_swap_desc(req->cur_desc);
        dprintf("VSCSI: using direct RDMA %s, 0x%x bytes MD: 0x%llx\n",
                req->writing ? "write" : "read",
                req->cur_desc->len, (unsigned long long)req->cur_desc->va);
        break;
    case SRP_DATA_DESC_INDIRECT:
        req->ind_desc = (struct srp_indirect_buf *)(cmd->add_data + offset);
        vscsi_swap_desc(&req->ind_desc->table_desc);
        req->total_desc = req->ind_desc->table_desc.len /
            sizeof(struct srp_direct_buf);
        req->local_desc = req->writing ? cmd->data_out_desc_cnt :
            cmd->data_in_desc_cnt;
        for (i = 0; i < req->local_desc; i++) {
            vscsi_swap_desc(&req->ind_desc->desc_list[i]);
        }
        req->cur_desc = req->local_desc ? &req->ind_desc->desc_list[0] : NULL;
        dprintf("VSCSI: using indirect RDMA %s, 0x%x bytes %d descs "
                "(%d local) VA: 0x%llx\n",
                req->writing ? "read" : "write",
                be32_to_cpu(req->ind_desc->len),
                req->total_desc, req->local_desc,
                (unsigned long long)req->ind_desc->table_desc.va);
        break;
    default:
        fprintf(stderr,
                "vscsi_preprocess_desc: Unknown format %x\n", req->dma_fmt);
        return -1;
    }

    return 0;
}

/* Callback to indicate that the SCSI layer has completed a transfer.  */
static void vscsi_transfer_data(SCSIRequest *sreq, uint32_t len)
{
    VSCSIState *s = DO_UPCAST(VSCSIState, vdev.qdev, sreq->bus->qbus.parent);
    vscsi_req *req = sreq->hba_private;
    uint8_t *buf;
    int rc = 0;

    dprintf("VSCSI: SCSI xfer complete tag=0x%x len=0x%x, req=%p\n",
            sreq->tag, len, req);
    if (req == NULL) {
        fprintf(stderr, "VSCSI: Can't find request for tag 0x%x\n", sreq->tag);
        return;
    }

    if (len) {
        buf = scsi_req_get_buf(sreq);
        rc = vscsi_srp_transfer_data(s, req, req->writing, buf, len);
    }
    if (rc < 0) {
        fprintf(stderr, "VSCSI: RDMA error rc=%d!\n", rc);
        vscsi_makeup_sense(s, req, HARDWARE_ERROR, 0, 0);
        scsi_req_abort(req->sreq, CHECK_CONDITION);
        return;
    }

    /* Start next chunk */
    req->data_len -= rc;
    scsi_req_continue(sreq);
}

/* Callback to indicate that the SCSI layer has completed a transfer.  */
static void vscsi_command_complete(SCSIRequest *sreq, uint32_t status)
{
    VSCSIState *s = DO_UPCAST(VSCSIState, vdev.qdev, sreq->bus->qbus.parent);
    vscsi_req *req = sreq->hba_private;
    int32_t res_in = 0, res_out = 0;

    dprintf("VSCSI: SCSI cmd complete, r=0x%x tag=0x%x status=0x%x, req=%p\n",
            reason, sreq->tag, status, req);
    if (req == NULL) {
        fprintf(stderr, "VSCSI: Can't find request for tag 0x%x\n", sreq->tag);
        return;
    }

    if (status == CHECK_CONDITION) {
        req->senselen = scsi_req_get_sense(req->sreq, req->sense,
                                           sizeof(req->sense));
        status = 0;
        dprintf("VSCSI: Sense data, %d bytes:\n", len);
        dprintf("       %02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
                req->sense[0], req->sense[1], req->sense[2], req->sense[3],
                req->sense[4], req->sense[5], req->sense[6], req->sense[7]);
        dprintf("       %02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
                req->sense[8], req->sense[9], req->sense[10], req->sense[11],
                req->sense[12], req->sense[13], req->sense[14], req->sense[15]);
    }

    dprintf("VSCSI: Command complete err=%d\n", status);
    if (status == 0) {
        /* We handle overflows, not underflows for normal commands,
         * but hopefully nobody cares
         */
        if (req->writing) {
            res_out = req->data_len;
        } else {
            res_in = req->data_len;
        }
    }
    vscsi_send_rsp(s, req, status, res_in, res_out);
    vscsi_put_req(req);
}

static void vscsi_request_cancelled(SCSIRequest *sreq)
{
    vscsi_req *req = sreq->hba_private;

    vscsi_put_req(req);
}

static void vscsi_process_login(VSCSIState *s, vscsi_req *req)
{
    union viosrp_iu *iu = &req->iu;
    struct srp_login_rsp *rsp = &iu->srp.login_rsp;
    uint64_t tag = iu->srp.rsp.tag;

    dprintf("VSCSI: Got login, sendin response !\n");

    /* TODO handle case that requested size is wrong and
     * buffer format is wrong
     */
    memset(iu, 0, sizeof(struct srp_login_rsp));
    rsp->opcode = SRP_LOGIN_RSP;
    /* Don't advertise quite as many request as we support to
     * keep room for management stuff etc...
     */
    rsp->req_lim_delta = cpu_to_be32(VSCSI_REQ_LIMIT-2);
    rsp->tag = tag;
    rsp->max_it_iu_len = cpu_to_be32(sizeof(union srp_iu));
    rsp->max_ti_iu_len = cpu_to_be32(sizeof(union srp_iu));
    /* direct and indirect */
    rsp->buf_fmt = cpu_to_be16(SRP_BUF_FORMAT_DIRECT | SRP_BUF_FORMAT_INDIRECT);

    vscsi_send_iu(s, req, sizeof(*rsp), VIOSRP_SRP_FORMAT);
}

static void vscsi_inquiry_no_target(VSCSIState *s, vscsi_req *req)
{
    uint8_t *cdb = req->iu.srp.cmd.cdb;
    uint8_t resp_data[36];
    int rc, len, alen;

    /* We dont do EVPD. Also check that page_code is 0 */
    if ((cdb[1] & 0x01) || (cdb[1] & 0x01) || cdb[2] != 0) {
        /* Send INVALID FIELD IN CDB */
        vscsi_makeup_sense(s, req, ILLEGAL_REQUEST, 0x24, 0);
        vscsi_send_rsp(s, req, CHECK_CONDITION, 0, 0);
        return;
    }
    alen = cdb[3];
    alen = (alen << 8) | cdb[4];
    len = MIN(alen, 36);

    /* Fake up inquiry using PQ=3 */
    memset(resp_data, 0, 36);
    resp_data[0] = 0x7f;   /* Not capable of supporting a device here */
    resp_data[2] = 0x06;   /* SPS-4 */
    resp_data[3] = 0x02;   /* Resp data format */
    resp_data[4] = 36 - 5; /* Additional length */
    resp_data[7] = 0x10;   /* Sync transfers */
    memcpy(&resp_data[16], "QEMU EMPTY      ", 16);
    memcpy(&resp_data[8], "QEMU    ", 8);

    req->writing = 0;
    vscsi_preprocess_desc(req);
    rc = vscsi_srp_transfer_data(s, req, 0, resp_data, len);
    if (rc < 0) {
        vscsi_makeup_sense(s, req, HARDWARE_ERROR, 0, 0);
        vscsi_send_rsp(s, req, CHECK_CONDITION, 0, 0);
    } else {
        vscsi_send_rsp(s, req, 0, 36 - rc, 0);
    }
}

static int vscsi_queue_cmd(VSCSIState *s, vscsi_req *req)
{
    union srp_iu *srp = &req->iu.srp;
    SCSIDevice *sdev;
    int n, id, lun;

    vscsi_decode_id_lun(be64_to_cpu(srp->cmd.lun), &id, &lun);

    /* Qemu vs. linux issue with LUNs to be sorted out ... */
    sdev = (id < 8 && lun < 16) ? s->bus.devs[id] : NULL;
    if (!sdev) {
        dprintf("VSCSI: Command for id %d with no drive\n", id);
        if (srp->cmd.cdb[0] == INQUIRY) {
            vscsi_inquiry_no_target(s, req);
        } else {
            vscsi_makeup_sense(s, req, ILLEGAL_REQUEST, 0x24, 0x00);
            vscsi_send_rsp(s, req, CHECK_CONDITION, 0, 0);
        } return 1;
    }

    req->lun = lun;
    req->sreq = scsi_req_new(sdev, req->qtag, lun, req);
    n = scsi_req_enqueue(req->sreq, srp->cmd.cdb);

    dprintf("VSCSI: Queued command tag 0x%x CMD 0x%x ID %d LUN %d ret: %d\n",
            req->qtag, srp->cmd.cdb[0], id, lun, n);

    if (n) {
        /* Transfer direction must be set before preprocessing the
         * descriptors
         */
        req->writing = (n < 1);

        /* Preprocess RDMA descriptors */
        vscsi_preprocess_desc(req);

        /* Get transfer direction and initiate transfer */
        if (n > 0) {
            req->data_len = n;
        } else if (n < 0) {
            req->data_len = -n;
        }
        scsi_req_continue(req->sreq);
    }
    /* Don't touch req here, it may have been recycled already */

    return 0;
}

static int vscsi_process_tsk_mgmt(VSCSIState *s, vscsi_req *req)
{
    union viosrp_iu *iu = &req->iu;
    int fn;

    fprintf(stderr, "vscsi_process_tsk_mgmt %02x\n",
            iu->srp.tsk_mgmt.tsk_mgmt_func);

    switch (iu->srp.tsk_mgmt.tsk_mgmt_func) {
#if 0 /* We really don't deal with these for now */
    case SRP_TSK_ABORT_TASK:
        fn = ABORT_TASK;
        break;
    case SRP_TSK_ABORT_TASK_SET:
        fn = ABORT_TASK_SET;
        break;
    case SRP_TSK_CLEAR_TASK_SET:
        fn = CLEAR_TASK_SET;
        break;
    case SRP_TSK_LUN_RESET:
        fn = LOGICAL_UNIT_RESET;
        break;
    case SRP_TSK_CLEAR_ACA:
        fn = CLEAR_ACA;
        break;
#endif
    default:
        fn = 0;
    }
    if (fn) {
        /* XXX Send/Handle target task management */
        ;
    } else {
        vscsi_makeup_sense(s, req, ILLEGAL_REQUEST, 0x20, 0);
        vscsi_send_rsp(s, req, CHECK_CONDITION, 0, 0);
    }
    return !fn;
}

static int vscsi_handle_srp_req(VSCSIState *s, vscsi_req *req)
{
    union srp_iu *srp = &req->iu.srp;
    int done = 1;
    uint8_t opcode = srp->rsp.opcode;

    switch (opcode) {
    case SRP_LOGIN_REQ:
        vscsi_process_login(s, req);
        break;
    case SRP_TSK_MGMT:
        done = vscsi_process_tsk_mgmt(s, req);
        break;
    case SRP_CMD:
        done = vscsi_queue_cmd(s, req);
        break;
    case SRP_LOGIN_RSP:
    case SRP_I_LOGOUT:
    case SRP_T_LOGOUT:
    case SRP_RSP:
    case SRP_CRED_REQ:
    case SRP_CRED_RSP:
    case SRP_AER_REQ:
    case SRP_AER_RSP:
        fprintf(stderr, "VSCSI: Unsupported opcode %02x\n", opcode);
        break;
    default:
        fprintf(stderr, "VSCSI: Unknown type %02x\n", opcode);
    }

    return done;
}

static int vscsi_send_adapter_info(VSCSIState *s, vscsi_req *req)
{
    struct viosrp_adapter_info *sinfo;
    struct mad_adapter_info_data info;
    int rc;

    sinfo = &req->iu.mad.adapter_info;

#if 0 /* What for ? */
    rc = spapr_tce_dma_read(&s->vdev, be64_to_cpu(sinfo->buffer),
                            &info, be16_to_cpu(sinfo->common.length));
    if (rc) {
        fprintf(stderr, "vscsi_send_adapter_info: DMA read failure !\n");
    }
#endif
    memset(&info, 0, sizeof(info));
    strcpy(info.srp_version, SRP_VERSION);
    strncpy(info.partition_name, "qemu", sizeof("qemu"));
    info.partition_number = cpu_to_be32(0);
    info.mad_version = cpu_to_be32(1);
    info.os_type = cpu_to_be32(2);
    info.port_max_txu[0] = cpu_to_be32(VSCSI_MAX_SECTORS << 9);

    rc = spapr_tce_dma_write(&s->vdev, be64_to_cpu(sinfo->buffer),
                             &info, be16_to_cpu(sinfo->common.length));
    if (rc)  {
        fprintf(stderr, "vscsi_send_adapter_info: DMA write failure !\n");
    }

    sinfo->common.status = rc ? cpu_to_be32(1) : 0;

    return vscsi_send_iu(s, req, sizeof(*sinfo), VIOSRP_MAD_FORMAT);
}

static int vscsi_handle_mad_req(VSCSIState *s, vscsi_req *req)
{
    union mad_iu *mad = &req->iu.mad;

    switch (be32_to_cpu(mad->empty_iu.common.type)) {
    case VIOSRP_EMPTY_IU_TYPE:
        fprintf(stderr, "Unsupported EMPTY MAD IU\n");
        break;
    case VIOSRP_ERROR_LOG_TYPE:
        fprintf(stderr, "Unsupported ERROR LOG MAD IU\n");
        mad->error_log.common.status = cpu_to_be16(1);
        vscsi_send_iu(s, req, sizeof(mad->error_log), VIOSRP_MAD_FORMAT);
        break;
    case VIOSRP_ADAPTER_INFO_TYPE:
        vscsi_send_adapter_info(s, req);
        break;
    case VIOSRP_HOST_CONFIG_TYPE:
        mad->host_config.common.status = cpu_to_be16(1);
        vscsi_send_iu(s, req, sizeof(mad->host_config), VIOSRP_MAD_FORMAT);
        break;
    default:
        fprintf(stderr, "VSCSI: Unknown MAD type %02x\n",
                be32_to_cpu(mad->empty_iu.common.type));
    }

    return 1;
}

static void vscsi_got_payload(VSCSIState *s, vscsi_crq *crq)
{
    vscsi_req *req;
    int done;

    req = vscsi_get_req(s);
    if (req == NULL) {
        fprintf(stderr, "VSCSI: Failed to get a request !\n");
        return;
    }

    /* We only support a limited number of descriptors, we know
     * the ibmvscsi driver uses up to 10 max, so it should fit
     * in our 256 bytes IUs. If not we'll have to increase the size
     * of the structure.
     */
    if (crq->s.IU_length > sizeof(union viosrp_iu)) {
        fprintf(stderr, "VSCSI: SRP IU too long (%d bytes) !\n",
                crq->s.IU_length);
        return;
    }

    /* XXX Handle failure differently ? */
    if (spapr_tce_dma_read(&s->vdev, crq->s.IU_data_ptr, &req->iu,
                           crq->s.IU_length)) {
        fprintf(stderr, "vscsi_got_payload: DMA read failure !\n");
        qemu_free(req);
    }
    memcpy(&req->crq, crq, sizeof(vscsi_crq));

    if (crq->s.format == VIOSRP_MAD_FORMAT) {
        done = vscsi_handle_mad_req(s, req);
    } else {
        done = vscsi_handle_srp_req(s, req);
    }

    if (done) {
        vscsi_put_req(req);
    }
}


static int vscsi_do_crq(struct VIOsPAPRDevice *dev, uint8_t *crq_data)
{
    VSCSIState *s = DO_UPCAST(VSCSIState, vdev, dev);
    vscsi_crq crq;

    memcpy(crq.raw, crq_data, 16);
    crq.s.timeout = be16_to_cpu(crq.s.timeout);
    crq.s.IU_length = be16_to_cpu(crq.s.IU_length);
    crq.s.IU_data_ptr = be64_to_cpu(crq.s.IU_data_ptr);

    dprintf("VSCSI: do_crq %02x %02x ...\n", crq.raw[0], crq.raw[1]);

    switch (crq.s.valid) {
    case 0xc0: /* Init command/response */

        /* Respond to initialization request */
        if (crq.s.format == 0x01) {
            memset(crq.raw, 0, 16);
            crq.s.valid = 0xc0;
            crq.s.format = 0x02;
            spapr_vio_send_crq(dev, crq.raw);
        }

        /* Note that in hotplug cases, we might get a 0x02
         * as a result of us emitting the init request
         */

        break;
    case 0xff: /* Link event */

        /* Not handled for now */

        break;
    case 0x80: /* Payloads */
        switch (crq.s.format) {
        case VIOSRP_SRP_FORMAT: /* AKA VSCSI request */
        case VIOSRP_MAD_FORMAT: /* AKA VSCSI response */
            vscsi_got_payload(s, &crq);
            break;
        case VIOSRP_OS400_FORMAT:
        case VIOSRP_AIX_FORMAT:
        case VIOSRP_LINUX_FORMAT:
        case VIOSRP_INLINE_FORMAT:
            fprintf(stderr, "vscsi_do_srq: Unsupported payload format %02x\n",
                    crq.s.format);
            break;
        default:
            fprintf(stderr, "vscsi_do_srq: Unknown payload format %02x\n",
                    crq.s.format);
        }
        break;
    default:
        fprintf(stderr, "vscsi_do_crq: unknown CRQ %02x %02x ...\n",
                crq.raw[0], crq.raw[1]);
    };

    return 0;
}

static const struct SCSIBusOps vscsi_scsi_ops = {
    .transfer_data = vscsi_transfer_data,
    .complete = vscsi_command_complete,
    .cancel = vscsi_request_cancelled
};

static int spapr_vscsi_init(VIOsPAPRDevice *dev)
{
    VSCSIState *s = DO_UPCAST(VSCSIState, vdev, dev);
    int i;

    dbg_vscsi_state = s;

    /* Initialize qemu request tags */
    memset(s->reqs, 0, sizeof(s->reqs));
    for (i = 0; i < VSCSI_REQ_LIMIT; i++) {
        s->reqs[i].qtag = i;
    }

    dev->crq.SendFunc = vscsi_do_crq;

    scsi_bus_new(&s->bus, &dev->qdev, 1, VSCSI_REQ_LIMIT,
                 &vscsi_scsi_ops);
    if (!dev->qdev.hotplugged) {
        scsi_bus_legacy_handle_cmdline(&s->bus);
    }

    return 0;
}

void spapr_vscsi_create(VIOsPAPRBus *bus, uint32_t reg,
                        qemu_irq qirq, uint32_t vio_irq_num)
{
    DeviceState *dev;
    VIOsPAPRDevice *sdev;

    dev = qdev_create(&bus->bus, "spapr-vscsi");
    qdev_prop_set_uint32(dev, "reg", reg);

    qdev_init_nofail(dev);

    sdev = (VIOsPAPRDevice *)dev;
    sdev->qirq = qirq;
    sdev->vio_irq_num = vio_irq_num;
}

static int spapr_vscsi_devnode(VIOsPAPRDevice *dev, void *fdt, int node_off)
{
    int ret;

    ret = fdt_setprop_cell(fdt, node_off, "#address-cells", 2);
    if (ret < 0) {
        return ret;
    }

    ret = fdt_setprop_cell(fdt, node_off, "#size-cells", 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static VIOsPAPRDeviceInfo spapr_vscsi = {
    .init = spapr_vscsi_init,
    .devnode = spapr_vscsi_devnode,
    .dt_name = "v-scsi",
    .dt_type = "vscsi",
    .dt_compatible = "IBM,v-scsi",
    .signal_mask = 0x00000001,
    .qdev.name = "spapr-vscsi",
    .qdev.size = sizeof(VSCSIState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("reg", VIOsPAPRDevice, reg, 0x2000),
        DEFINE_PROP_UINT32("dma-window", VIOsPAPRDevice,
                           rtce_window_size, 0x10000000),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void spapr_vscsi_register(void)
{
    spapr_vio_bus_register_withprop(&spapr_vscsi);
}
device_init(spapr_vscsi_register);
