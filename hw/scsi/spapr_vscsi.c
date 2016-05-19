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
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/scsi/scsi.h"
#include "block/scsi.h"
#include "srp.h"
#include "hw/qdev.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "viosrp.h"

#include <libfdt.h>

/*#define DEBUG_VSCSI*/

#ifdef DEBUG_VSCSI
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

/*
 * Virtual SCSI device
 */

/* Random numbers */
#define VSCSI_MAX_SECTORS       4096
#define VSCSI_REQ_LIMIT         24

#define SRP_RSP_SENSE_DATA_LEN  18

#define SRP_REPORT_LUNS_WLUN    0xc10100000000000ULL

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
    bool                    active;
    bool                    writing;
    bool                    dma_error;
    uint32_t                data_len;
    uint32_t                senselen;
    uint8_t                 sense[SCSI_SENSE_BUF_SIZE];

    /* RDMA related bits */
    uint8_t                 dma_fmt;
    uint16_t                local_desc;
    uint16_t                total_desc;
    uint16_t                cdb_offset;
    uint16_t                cur_desc_num;
    uint16_t                cur_desc_offset;
} vscsi_req;

#define TYPE_VIO_SPAPR_VSCSI_DEVICE "spapr-vscsi"
#define VIO_SPAPR_VSCSI_DEVICE(obj) \
     OBJECT_CHECK(VSCSIState, (obj), TYPE_VIO_SPAPR_VSCSI_DEVICE)

typedef struct {
    VIOsPAPRDevice vdev;
    SCSIBus bus;
    vscsi_req reqs[VSCSI_REQ_LIMIT];
} VSCSIState;

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

static struct vscsi_req *vscsi_find_req(VSCSIState *s, uint64_t srp_tag)
{
    vscsi_req *req;
    int i;

    for (i = 0; i < VSCSI_REQ_LIMIT; i++) {
        req = &s->reqs[i];
        if (req->iu.srp.cmd.tag == srp_tag) {
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

static SCSIDevice *vscsi_device_find(SCSIBus *bus, uint64_t srp_lun, int *lun)
{
    int channel = 0, id = 0;

retry:
    switch (srp_lun >> 62) {
    case 0:
        if ((srp_lun >> 56) != 0) {
            channel = (srp_lun >> 56) & 0x3f;
            id = (srp_lun >> 48) & 0xff;
            srp_lun <<= 16;
            goto retry;
        }
        *lun = (srp_lun >> 48) & 0xff;
        break;

    case 1:
        *lun = (srp_lun >> 48) & 0x3fff;
        break;
    case 2:
        channel = (srp_lun >> 53) & 0x7;
        id = (srp_lun >> 56) & 0x3f;
        *lun = (srp_lun >> 48) & 0x1f;
        break;
    case 3:
        *lun = -1;
        return NULL;
    default:
        abort();
    }

    return scsi_device_find(bus, channel, id, *lun);
}

static int vscsi_send_iu(VSCSIState *s, vscsi_req *req,
                         uint64_t length, uint8_t format)
{
    long rc, rc1;

    /* First copy the SRP */
    rc = spapr_vio_dma_write(&s->vdev, req->crq.s.IU_data_ptr,
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
        req->crq.s.status = VIOSRP_OK;
    } else {
        req->crq.s.status = VIOSRP_ADAPTER_FAIL;
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
    uint8_t sol_not = iu->srp.cmd.sol_not;

    DPRINTF("VSCSI: Sending resp status: 0x%x, "
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
        iu->srp.rsp.sol_not = (sol_not & 0x04) >> 2;
        if (req->senselen) {
            req->iu.srp.rsp.flags |= SRP_RSP_FLAG_SNSVALID;
            req->iu.srp.rsp.sense_data_len = cpu_to_be32(req->senselen);
            memcpy(req->iu.srp.rsp.data, req->sense, req->senselen);
            total_len += req->senselen;
        }
    } else {
        iu->srp.rsp.sol_not = (sol_not & 0x02) >> 1;
    }

    vscsi_send_iu(s, req, total_len, VIOSRP_SRP_FORMAT);
    return 0;
}

static inline struct srp_direct_buf vscsi_swap_desc(struct srp_direct_buf desc)
{
    desc.va = be64_to_cpu(desc.va);
    desc.len = be32_to_cpu(desc.len);
    return desc;
}

static int vscsi_fetch_desc(VSCSIState *s, struct vscsi_req *req,
                            unsigned n, unsigned buf_offset,
                            struct srp_direct_buf *ret)
{
    struct srp_cmd *cmd = &req->iu.srp.cmd;

    switch (req->dma_fmt) {
    case SRP_NO_DATA_DESC: {
        DPRINTF("VSCSI: no data descriptor\n");
        return 0;
    }
    case SRP_DATA_DESC_DIRECT: {
        memcpy(ret, cmd->add_data + req->cdb_offset, sizeof(*ret));
        assert(req->cur_desc_num == 0);
        DPRINTF("VSCSI: direct segment\n");
        break;
    }
    case SRP_DATA_DESC_INDIRECT: {
        struct srp_indirect_buf *tmp = (struct srp_indirect_buf *)
                                       (cmd->add_data + req->cdb_offset);
        if (n < req->local_desc) {
            *ret = tmp->desc_list[n];
            DPRINTF("VSCSI: indirect segment local tag=0x%x desc#%d/%d\n",
                    req->qtag, n, req->local_desc);

        } else if (n < req->total_desc) {
            int rc;
            struct srp_direct_buf tbl_desc = vscsi_swap_desc(tmp->table_desc);
            unsigned desc_offset = n * sizeof(struct srp_direct_buf);

            if (desc_offset >= tbl_desc.len) {
                DPRINTF("VSCSI:   #%d is ouf of range (%d bytes)\n",
                        n, desc_offset);
                return -1;
            }
            rc = spapr_vio_dma_read(&s->vdev, tbl_desc.va + desc_offset,
                                    ret, sizeof(struct srp_direct_buf));
            if (rc) {
                DPRINTF("VSCSI: spapr_vio_dma_read -> %d reading ext_desc\n",
                        rc);
                return -1;
            }
            DPRINTF("VSCSI: indirect segment ext. tag=0x%x desc#%d/%d { va=%"PRIx64" len=%x }\n",
                    req->qtag, n, req->total_desc, tbl_desc.va, tbl_desc.len);
        } else {
            DPRINTF("VSCSI:   Out of descriptors !\n");
            return 0;
        }
        break;
    }
    default:
        fprintf(stderr, "VSCSI:   Unknown format %x\n", req->dma_fmt);
        return -1;
    }

    *ret = vscsi_swap_desc(*ret);
    if (buf_offset > ret->len) {
        DPRINTF("   offset=%x is out of a descriptor #%d boundary=%x\n",
                buf_offset, req->cur_desc_num, ret->len);
        return -1;
    }
    ret->va += buf_offset;
    ret->len -= buf_offset;

    DPRINTF("   cur=%d offs=%x ret { va=%"PRIx64" len=%x }\n",
            req->cur_desc_num, req->cur_desc_offset, ret->va, ret->len);

    return ret->len ? 1 : 0;
}

static int vscsi_srp_direct_data(VSCSIState *s, vscsi_req *req,
                                 uint8_t *buf, uint32_t len)
{
    struct srp_direct_buf md;
    uint32_t llen;
    int rc = 0;

    rc = vscsi_fetch_desc(s, req, req->cur_desc_num, req->cur_desc_offset, &md);
    if (rc < 0) {
        return -1;
    } else if (rc == 0) {
        return 0;
    }

    llen = MIN(len, md.len);
    if (llen) {
        if (req->writing) { /* writing = to device = reading from memory */
            rc = spapr_vio_dma_read(&s->vdev, md.va, buf, llen);
        } else {
            rc = spapr_vio_dma_write(&s->vdev, md.va, buf, llen);
        }
    }

    if (rc) {
        return -1;
    }
    req->cur_desc_offset += llen;

    return llen;
}

static int vscsi_srp_indirect_data(VSCSIState *s, vscsi_req *req,
                                   uint8_t *buf, uint32_t len)
{
    struct srp_direct_buf md;
    int rc = 0;
    uint32_t llen, total = 0;

    DPRINTF("VSCSI: indirect segment 0x%x bytes\n", len);

    /* While we have data ... */
    while (len) {
        rc = vscsi_fetch_desc(s, req, req->cur_desc_num, req->cur_desc_offset, &md);
        if (rc < 0) {
            return -1;
        } else if (rc == 0) {
            break;
        }

        /* Perform transfer */
        llen = MIN(len, md.len);
        if (req->writing) { /* writing = to device = reading from memory */
            rc = spapr_vio_dma_read(&s->vdev, md.va, buf, llen);
        } else {
            rc = spapr_vio_dma_write(&s->vdev, md.va, buf, llen);
        }
        if (rc) {
            DPRINTF("VSCSI: spapr_vio_dma_r/w(%d) -> %d\n", req->writing, rc);
            break;
        }
        DPRINTF("VSCSI:     data: %02x %02x %02x %02x...\n",
                buf[0], buf[1], buf[2], buf[3]);

        len -= llen;
        buf += llen;

        total += llen;

        /* Update current position in the current descriptor */
        req->cur_desc_offset += llen;
        if (md.len == llen) {
            /* Go to the next descriptor if the current one finished */
            ++req->cur_desc_num;
            req->cur_desc_offset = 0;
        }
    }

    return rc ? -1 : total;
}

static int vscsi_srp_transfer_data(VSCSIState *s, vscsi_req *req,
                                   int writing, uint8_t *buf, uint32_t len)
{
    int err = 0;

    switch (req->dma_fmt) {
    case SRP_NO_DATA_DESC:
        DPRINTF("VSCSI: no data desc transfer, skipping 0x%x bytes\n", len);
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

    req->cdb_offset = cmd->add_cdb_len & ~3;

    if (req->writing) {
        req->dma_fmt = cmd->buf_fmt >> 4;
    } else {
        req->cdb_offset += data_out_desc_size(cmd);
        req->dma_fmt = cmd->buf_fmt & ((1U << 4) - 1);
    }

    switch (req->dma_fmt) {
    case SRP_NO_DATA_DESC:
        break;
    case SRP_DATA_DESC_DIRECT:
        req->total_desc = req->local_desc = 1;
        break;
    case SRP_DATA_DESC_INDIRECT: {
        struct srp_indirect_buf *ind_tmp = (struct srp_indirect_buf *)
                (cmd->add_data + req->cdb_offset);

        req->total_desc = be32_to_cpu(ind_tmp->table_desc.len) /
                          sizeof(struct srp_direct_buf);
        req->local_desc = req->writing ? cmd->data_out_desc_cnt :
                          cmd->data_in_desc_cnt;
        break;
    }
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
    VSCSIState *s = VIO_SPAPR_VSCSI_DEVICE(sreq->bus->qbus.parent);
    vscsi_req *req = sreq->hba_private;
    uint8_t *buf;
    int rc = 0;

    DPRINTF("VSCSI: SCSI xfer complete tag=0x%x len=0x%x, req=%p\n",
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
        req->dma_error = true;
        scsi_req_cancel(req->sreq);
        return;
    }

    /* Start next chunk */
    req->data_len -= rc;
    scsi_req_continue(sreq);
}

/* Callback to indicate that the SCSI layer has completed a transfer.  */
static void vscsi_command_complete(SCSIRequest *sreq, uint32_t status, size_t resid)
{
    VSCSIState *s = VIO_SPAPR_VSCSI_DEVICE(sreq->bus->qbus.parent);
    vscsi_req *req = sreq->hba_private;
    int32_t res_in = 0, res_out = 0;

    DPRINTF("VSCSI: SCSI cmd complete, tag=0x%x status=0x%x, req=%p\n",
            sreq->tag, status, req);
    if (req == NULL) {
        fprintf(stderr, "VSCSI: Can't find request for tag 0x%x\n", sreq->tag);
        return;
    }

    if (status == CHECK_CONDITION) {
        req->senselen = scsi_req_get_sense(req->sreq, req->sense,
                                           sizeof(req->sense));
        DPRINTF("VSCSI: Sense data, %d bytes:\n", req->senselen);
        DPRINTF("       %02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
                req->sense[0], req->sense[1], req->sense[2], req->sense[3],
                req->sense[4], req->sense[5], req->sense[6], req->sense[7]);
        DPRINTF("       %02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
                req->sense[8], req->sense[9], req->sense[10], req->sense[11],
                req->sense[12], req->sense[13], req->sense[14], req->sense[15]);
    }

    DPRINTF("VSCSI: Command complete err=%d\n", status);
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

    if (req->dma_error) {
        VSCSIState *s = VIO_SPAPR_VSCSI_DEVICE(sreq->bus->qbus.parent);

        vscsi_makeup_sense(s, req, HARDWARE_ERROR, 0, 0);
        vscsi_send_rsp(s, req, CHECK_CONDITION, 0, 0);
    }
    vscsi_put_req(req);
}

static const VMStateDescription vmstate_spapr_vscsi_req = {
    .name = "spapr_vscsi_req",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(crq.raw, vscsi_req),
        VMSTATE_BUFFER(iu.srp.reserved, vscsi_req),
        VMSTATE_UINT32(qtag, vscsi_req),
        VMSTATE_BOOL(active, vscsi_req),
        VMSTATE_UINT32(data_len, vscsi_req),
        VMSTATE_BOOL(writing, vscsi_req),
        VMSTATE_UINT32(senselen, vscsi_req),
        VMSTATE_BUFFER(sense, vscsi_req),
        VMSTATE_UINT8(dma_fmt, vscsi_req),
        VMSTATE_UINT16(local_desc, vscsi_req),
        VMSTATE_UINT16(total_desc, vscsi_req),
        VMSTATE_UINT16(cdb_offset, vscsi_req),
      /*Restart SCSI request from the beginning for now */
      /*VMSTATE_UINT16(cur_desc_num, vscsi_req),
        VMSTATE_UINT16(cur_desc_offset, vscsi_req),*/
        VMSTATE_END_OF_LIST()
    },
};

static void vscsi_save_request(QEMUFile *f, SCSIRequest *sreq)
{
    vscsi_req *req = sreq->hba_private;
    assert(req->active);

    vmstate_save_state(f, &vmstate_spapr_vscsi_req, req, NULL);

    DPRINTF("VSCSI: saving tag=%u, current desc#%d, offset=%x\n",
            req->qtag, req->cur_desc_num, req->cur_desc_offset);
}

static void *vscsi_load_request(QEMUFile *f, SCSIRequest *sreq)
{
    SCSIBus *bus = sreq->bus;
    VSCSIState *s = VIO_SPAPR_VSCSI_DEVICE(bus->qbus.parent);
    vscsi_req *req;
    int rc;

    assert(sreq->tag < VSCSI_REQ_LIMIT);
    req = &s->reqs[sreq->tag];
    assert(!req->active);

    memset(req, 0, sizeof(*req));
    rc = vmstate_load_state(f, &vmstate_spapr_vscsi_req, req, 1);
    if (rc) {
        fprintf(stderr, "VSCSI: failed loading request tag#%u\n", sreq->tag);
        return NULL;
    }
    assert(req->active);

    req->sreq = scsi_req_ref(sreq);

    DPRINTF("VSCSI: restoring tag=%u, current desc#%d, offset=%x\n",
            req->qtag, req->cur_desc_num, req->cur_desc_offset);

    return req;
}

static void vscsi_process_login(VSCSIState *s, vscsi_req *req)
{
    union viosrp_iu *iu = &req->iu;
    struct srp_login_rsp *rsp = &iu->srp.login_rsp;
    uint64_t tag = iu->srp.rsp.tag;

    DPRINTF("VSCSI: Got login, sendin response !\n");

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

    /* We don't do EVPD. Also check that page_code is 0 */
    if ((cdb[1] & 0x01) || cdb[2] != 0) {
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

static void vscsi_report_luns(VSCSIState *s, vscsi_req *req)
{
    BusChild *kid;
    int i, len, n, rc;
    uint8_t *resp_data;
    bool found_lun0;

    n = 0;
    found_lun0 = false;
    QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
        SCSIDevice *dev = SCSI_DEVICE(kid->child);

        n += 8;
        if (dev->channel == 0 && dev->id == 0 && dev->lun == 0) {
            found_lun0 = true;
        }
    }
    if (!found_lun0) {
        n += 8;
    }
    len = n+8;

    resp_data = g_malloc0(len);
    stl_be_p(resp_data, n);
    i = found_lun0 ? 8 : 16;
    QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        SCSIDevice *dev = SCSI_DEVICE(qdev);

        if (dev->id == 0 && dev->channel == 0) {
            resp_data[i] = 0;         /* Use simple LUN for 0 (SAM5 4.7.7.1) */
        } else {
            resp_data[i] = (2 << 6);  /* Otherwise LUN addressing (4.7.7.4)  */
        }
        resp_data[i] |= dev->id;
        resp_data[i+1] = (dev->channel << 5);
        resp_data[i+1] |= dev->lun;
        i += 8;
    }

    vscsi_preprocess_desc(req);
    rc = vscsi_srp_transfer_data(s, req, 0, resp_data, len);
    g_free(resp_data);
    if (rc < 0) {
        vscsi_makeup_sense(s, req, HARDWARE_ERROR, 0, 0);
        vscsi_send_rsp(s, req, CHECK_CONDITION, 0, 0);
    } else {
        vscsi_send_rsp(s, req, 0, len - rc, 0);
    }
}

static int vscsi_queue_cmd(VSCSIState *s, vscsi_req *req)
{
    union srp_iu *srp = &req->iu.srp;
    SCSIDevice *sdev;
    int n, lun;

    if ((srp->cmd.lun == 0 || be64_to_cpu(srp->cmd.lun) == SRP_REPORT_LUNS_WLUN)
      && srp->cmd.cdb[0] == REPORT_LUNS) {
        vscsi_report_luns(s, req);
        return 0;
    }

    sdev = vscsi_device_find(&s->bus, be64_to_cpu(srp->cmd.lun), &lun);
    if (!sdev) {
        DPRINTF("VSCSI: Command for lun %08" PRIx64 " with no drive\n",
                be64_to_cpu(srp->cmd.lun));
        if (srp->cmd.cdb[0] == INQUIRY) {
            vscsi_inquiry_no_target(s, req);
        } else {
            vscsi_makeup_sense(s, req, ILLEGAL_REQUEST, 0x24, 0x00);
            vscsi_send_rsp(s, req, CHECK_CONDITION, 0, 0);
        } return 1;
    }

    req->sreq = scsi_req_new(sdev, req->qtag, lun, srp->cmd.cdb, req);
    n = scsi_req_enqueue(req->sreq);

    DPRINTF("VSCSI: Queued command tag 0x%x CMD 0x%x=%s LUN %d ret: %d\n",
            req->qtag, srp->cmd.cdb[0], scsi_command_name(srp->cmd.cdb[0]),
            lun, n);

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
    vscsi_req *tmpreq;
    int i, lun = 0, resp = SRP_TSK_MGMT_COMPLETE;
    SCSIDevice *d;
    uint64_t tag = iu->srp.rsp.tag;
    uint8_t sol_not = iu->srp.cmd.sol_not;

    fprintf(stderr, "vscsi_process_tsk_mgmt %02x\n",
            iu->srp.tsk_mgmt.tsk_mgmt_func);

    d = vscsi_device_find(&s->bus, be64_to_cpu(req->iu.srp.tsk_mgmt.lun), &lun);
    if (!d) {
        resp = SRP_TSK_MGMT_FIELDS_INVALID;
    } else {
        switch (iu->srp.tsk_mgmt.tsk_mgmt_func) {
        case SRP_TSK_ABORT_TASK:
            if (d->lun != lun) {
                resp = SRP_TSK_MGMT_FIELDS_INVALID;
                break;
            }

            tmpreq = vscsi_find_req(s, req->iu.srp.tsk_mgmt.task_tag);
            if (tmpreq && tmpreq->sreq) {
                assert(tmpreq->sreq->hba_private);
                scsi_req_cancel(tmpreq->sreq);
            }
            break;

        case SRP_TSK_LUN_RESET:
            if (d->lun != lun) {
                resp = SRP_TSK_MGMT_FIELDS_INVALID;
                break;
            }

            qdev_reset_all(&d->qdev);
            break;

        case SRP_TSK_ABORT_TASK_SET:
        case SRP_TSK_CLEAR_TASK_SET:
            if (d->lun != lun) {
                resp = SRP_TSK_MGMT_FIELDS_INVALID;
                break;
            }

            for (i = 0; i < VSCSI_REQ_LIMIT; i++) {
                tmpreq = &s->reqs[i];
                if (tmpreq->iu.srp.cmd.lun != req->iu.srp.tsk_mgmt.lun) {
                    continue;
                }
                if (!tmpreq->active || !tmpreq->sreq) {
                    continue;
                }
                assert(tmpreq->sreq->hba_private);
                scsi_req_cancel(tmpreq->sreq);
            }
            break;

        case SRP_TSK_CLEAR_ACA:
            resp = SRP_TSK_MGMT_NOT_SUPPORTED;
            break;

        default:
            resp = SRP_TSK_MGMT_FIELDS_INVALID;
            break;
        }
    }

    /* Compose the response here as  */
    memset(iu, 0, sizeof(struct srp_rsp) + 4);
    iu->srp.rsp.opcode = SRP_RSP;
    iu->srp.rsp.req_lim_delta = cpu_to_be32(1);
    iu->srp.rsp.tag = tag;
    iu->srp.rsp.flags |= SRP_RSP_FLAG_RSPVALID;
    iu->srp.rsp.resp_data_len = cpu_to_be32(4);
    if (resp) {
        iu->srp.rsp.sol_not = (sol_not & 0x04) >> 2;
    } else {
        iu->srp.rsp.sol_not = (sol_not & 0x02) >> 1;
    }

    iu->srp.rsp.status = GOOD;
    iu->srp.rsp.data[3] = resp;

    vscsi_send_iu(s, req, sizeof(iu->srp.rsp) + 4, VIOSRP_SRP_FORMAT);

    return 1;
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
    rc = spapr_vio_dma_read(&s->vdev, be64_to_cpu(sinfo->buffer),
                            &info, be16_to_cpu(sinfo->common.length));
    if (rc) {
        fprintf(stderr, "vscsi_send_adapter_info: DMA read failure !\n");
    }
#endif
    memset(&info, 0, sizeof(info));
    strcpy(info.srp_version, SRP_VERSION);
    memcpy(info.partition_name, "qemu", sizeof("qemu"));
    info.partition_number = cpu_to_be32(0);
    info.mad_version = cpu_to_be32(1);
    info.os_type = cpu_to_be32(2);
    info.port_max_txu[0] = cpu_to_be32(VSCSI_MAX_SECTORS << 9);

    rc = spapr_vio_dma_write(&s->vdev, be64_to_cpu(sinfo->buffer),
                             &info, be16_to_cpu(sinfo->common.length));
    if (rc)  {
        fprintf(stderr, "vscsi_send_adapter_info: DMA write failure !\n");
    }

    sinfo->common.status = rc ? cpu_to_be32(1) : 0;

    return vscsi_send_iu(s, req, sizeof(*sinfo), VIOSRP_MAD_FORMAT);
}

static int vscsi_send_capabilities(VSCSIState *s, vscsi_req *req)
{
    struct viosrp_capabilities *vcap;
    struct capabilities cap = { };
    uint16_t len, req_len;
    uint64_t buffer;
    int rc;

    vcap = &req->iu.mad.capabilities;
    req_len = len = be16_to_cpu(vcap->common.length);
    buffer = be64_to_cpu(vcap->buffer);
    if (len > sizeof(cap)) {
        fprintf(stderr, "vscsi_send_capabilities: capabilities size mismatch !\n");

        /*
         * Just read and populate the structure that is known.
         * Zero rest of the structure.
         */
        len = sizeof(cap);
    }
    rc = spapr_vio_dma_read(&s->vdev, buffer, &cap, len);
    if (rc)  {
        fprintf(stderr, "vscsi_send_capabilities: DMA read failure !\n");
    }

    /*
     * Current implementation does not suppport any migration or
     * reservation capabilities. Construct the response telling the
     * guest not to use them.
     */
    cap.flags = 0;
    cap.migration.ecl = 0;
    cap.reserve.type = 0;
    cap.migration.common.server_support = 0;
    cap.reserve.common.server_support = 0;

    rc = spapr_vio_dma_write(&s->vdev, buffer, &cap, len);
    if (rc)  {
        fprintf(stderr, "vscsi_send_capabilities: DMA write failure !\n");
    }
    if (req_len > len) {
        /*
         * Being paranoid and lets not worry about the error code
         * here. Actual write of the cap is done above.
         */
        spapr_vio_dma_set(&s->vdev, (buffer + len), 0, (req_len - len));
    }
    vcap->common.status = rc ? cpu_to_be32(1) : 0;
    return vscsi_send_iu(s, req, sizeof(*vcap), VIOSRP_MAD_FORMAT);
}

static int vscsi_handle_mad_req(VSCSIState *s, vscsi_req *req)
{
    union mad_iu *mad = &req->iu.mad;
    bool request_handled = false;
    uint64_t retlen = 0;

    switch (be32_to_cpu(mad->empty_iu.common.type)) {
    case VIOSRP_EMPTY_IU_TYPE:
        fprintf(stderr, "Unsupported EMPTY MAD IU\n");
        retlen = sizeof(mad->empty_iu);
        break;
    case VIOSRP_ERROR_LOG_TYPE:
        fprintf(stderr, "Unsupported ERROR LOG MAD IU\n");
        retlen = sizeof(mad->error_log);
        break;
    case VIOSRP_ADAPTER_INFO_TYPE:
        vscsi_send_adapter_info(s, req);
        request_handled = true;
        break;
    case VIOSRP_HOST_CONFIG_TYPE:
        retlen = sizeof(mad->host_config);
        break;
    case VIOSRP_CAPABILITIES_TYPE:
        vscsi_send_capabilities(s, req);
        request_handled = true;
        break;
    default:
        fprintf(stderr, "VSCSI: Unknown MAD type %02x\n",
                be32_to_cpu(mad->empty_iu.common.type));
        /*
         * PAPR+ says that "The length field is set to the length
         * of the data structure(s) used in the command".
         * As we did not recognize the request type, put zero there.
         */
        retlen = 0;
    }

    if (!request_handled) {
        mad->empty_iu.common.status = cpu_to_be16(VIOSRP_MAD_NOT_SUPPORTED);
        vscsi_send_iu(s, req, retlen, VIOSRP_MAD_FORMAT);
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
        vscsi_put_req(req);
        return;
    }

    /* XXX Handle failure differently ? */
    if (spapr_vio_dma_read(&s->vdev, crq->s.IU_data_ptr, &req->iu,
                           crq->s.IU_length)) {
        fprintf(stderr, "vscsi_got_payload: DMA read failure !\n");
        vscsi_put_req(req);
        return;
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
    VSCSIState *s = VIO_SPAPR_VSCSI_DEVICE(dev);
    vscsi_crq crq;

    memcpy(crq.raw, crq_data, 16);
    crq.s.timeout = be16_to_cpu(crq.s.timeout);
    crq.s.IU_length = be16_to_cpu(crq.s.IU_length);
    crq.s.IU_data_ptr = be64_to_cpu(crq.s.IU_data_ptr);

    DPRINTF("VSCSI: do_crq %02x %02x ...\n", crq.raw[0], crq.raw[1]);

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

static const struct SCSIBusInfo vscsi_scsi_info = {
    .tcq = true,
    .max_channel = 7, /* logical unit addressing format */
    .max_target = 63,
    .max_lun = 31,

    .transfer_data = vscsi_transfer_data,
    .complete = vscsi_command_complete,
    .cancel = vscsi_request_cancelled,
    .save_request = vscsi_save_request,
    .load_request = vscsi_load_request,
};

static void spapr_vscsi_reset(VIOsPAPRDevice *dev)
{
    VSCSIState *s = VIO_SPAPR_VSCSI_DEVICE(dev);
    int i;

    memset(s->reqs, 0, sizeof(s->reqs));
    for (i = 0; i < VSCSI_REQ_LIMIT; i++) {
        s->reqs[i].qtag = i;
    }
}

static void spapr_vscsi_realize(VIOsPAPRDevice *dev, Error **errp)
{
    VSCSIState *s = VIO_SPAPR_VSCSI_DEVICE(dev);

    dev->crq.SendFunc = vscsi_do_crq;

    scsi_bus_new(&s->bus, sizeof(s->bus), DEVICE(dev),
                 &vscsi_scsi_info, NULL);
    if (!dev->qdev.hotplugged) {
        scsi_bus_legacy_handle_cmdline(&s->bus, errp);
    }
}

void spapr_vscsi_create(VIOsPAPRBus *bus)
{
    DeviceState *dev;

    dev = qdev_create(&bus->bus, "spapr-vscsi");

    qdev_init_nofail(dev);
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

static Property spapr_vscsi_properties[] = {
    DEFINE_SPAPR_PROPERTIES(VSCSIState, vdev),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_spapr_vscsi = {
    .name = "spapr_vscsi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_SPAPR_VIO(vdev, VSCSIState),
        /* VSCSI state */
        /* ???? */

        VMSTATE_END_OF_LIST()
    },
};

static void spapr_vscsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VIOsPAPRDeviceClass *k = VIO_SPAPR_DEVICE_CLASS(klass);

    k->realize = spapr_vscsi_realize;
    k->reset = spapr_vscsi_reset;
    k->devnode = spapr_vscsi_devnode;
    k->dt_name = "v-scsi";
    k->dt_type = "vscsi";
    k->dt_compatible = "IBM,v-scsi";
    k->signal_mask = 0x00000001;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->props = spapr_vscsi_properties;
    k->rtce_window_size = 0x10000000;
    dc->vmsd = &vmstate_spapr_vscsi;
}

static const TypeInfo spapr_vscsi_info = {
    .name          = TYPE_VIO_SPAPR_VSCSI_DEVICE,
    .parent        = TYPE_VIO_SPAPR_DEVICE,
    .instance_size = sizeof(VSCSIState),
    .class_init    = spapr_vscsi_class_init,
};

static void spapr_vscsi_register_types(void)
{
    type_register_static(&spapr_vscsi_info);
}

type_init(spapr_vscsi_register_types)
