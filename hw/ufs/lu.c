/*
 * QEMU UFS Logical Unit
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Written by Jeuk Kim <jeuk20.kim@samsung.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/memalign.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "system/block-backend.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "ufs.h"

#define SCSI_COMMAND_FAIL (-1)

static void ufs_build_upiu_sense_data(UfsRequest *req, uint8_t *sense,
                                      uint32_t sense_len)
{
    req->rsp_upiu.sr.sense_data_len = cpu_to_be16(sense_len);
    assert(sense_len <= SCSI_SENSE_LEN);
    memcpy(req->rsp_upiu.sr.sense_data, sense, sense_len);
}

static void ufs_build_scsi_response_upiu(UfsRequest *req, uint8_t *sense,
                                         uint32_t sense_len,
                                         uint32_t transfered_len,
                                         int16_t status)
{
    uint32_t expected_len = be32_to_cpu(req->req_upiu.sc.exp_data_transfer_len);
    uint8_t flags = 0, response = UFS_COMMAND_RESULT_SUCCESS;
    uint16_t data_segment_length;

    if (expected_len > transfered_len) {
        req->rsp_upiu.sr.residual_transfer_count =
            cpu_to_be32(expected_len - transfered_len);
        flags |= UFS_UPIU_FLAG_UNDERFLOW;
    } else if (expected_len < transfered_len) {
        req->rsp_upiu.sr.residual_transfer_count =
            cpu_to_be32(transfered_len - expected_len);
        flags |= UFS_UPIU_FLAG_OVERFLOW;
    }

    if (status != 0) {
        ufs_build_upiu_sense_data(req, sense, sense_len);
        response = UFS_COMMAND_RESULT_FAIL;
    }

    data_segment_length =
        cpu_to_be16(sense_len + sizeof(req->rsp_upiu.sr.sense_data_len));
    ufs_build_upiu_header(req, UFS_UPIU_TRANSACTION_RESPONSE, flags, response,
                          status, data_segment_length);
}

static void ufs_scsi_command_complete(SCSIRequest *scsi_req, size_t resid)
{
    UfsRequest *req = scsi_req->hba_private;
    int16_t status = scsi_req->status;

    uint32_t transfered_len = scsi_req->cmd.xfer - resid;

    ufs_build_scsi_response_upiu(req, scsi_req->sense, scsi_req->sense_len,
                                 transfered_len, status);

    ufs_complete_req(req, UFS_REQUEST_SUCCESS);

    scsi_req->hba_private = NULL;
    scsi_req_unref(scsi_req);
}

static QEMUSGList *ufs_get_sg_list(SCSIRequest *scsi_req)
{
    UfsRequest *req = scsi_req->hba_private;
    return req->sg;
}

static const struct SCSIBusInfo ufs_scsi_info = {
    .tcq = true,
    .max_target = 0,
    .max_lun = UFS_MAX_LUS,
    .max_channel = 0,

    .get_sg_list = ufs_get_sg_list,
    .complete = ufs_scsi_command_complete,
};

static int ufs_emulate_report_luns(UfsRequest *req, uint8_t *outbuf,
                                   uint32_t outbuf_len)
{
    UfsHc *u = req->hc;
    int len = 0;

    /* TODO: Support for cases where SELECT REPORT is 1 and 2 */
    if (req->req_upiu.sc.cdb[2] != 0) {
        return SCSI_COMMAND_FAIL;
    }

    len += 8;

    for (uint8_t lun = 0; lun < UFS_MAX_LUS; ++lun) {
        if (u->lus[lun]) {
            if (len + 8 > outbuf_len) {
                break;
            }

            memset(outbuf + len, 0, 8);
            outbuf[len] = 0;
            outbuf[len + 1] = lun;
            len += 8;
        }
    }

    /* store the LUN list length */
    stl_be_p(outbuf, len - 8);

    return len;
}

static int ufs_scsi_emulate_vpd_page(UfsRequest *req, uint8_t *outbuf,
                                     uint32_t outbuf_len)
{
    uint8_t page_code = req->req_upiu.sc.cdb[2];
    int start, buflen = 0;

    outbuf[buflen++] = TYPE_WLUN;
    outbuf[buflen++] = page_code;
    outbuf[buflen++] = 0x00;
    outbuf[buflen++] = 0x00;
    start = buflen;

    switch (page_code) {
    case 0x00: /* Supported page codes, mandatory */
    {
        outbuf[buflen++] = 0x00; /* list of supported pages (this page) */
        outbuf[buflen++] = 0x87; /* mode page policy */
        break;
    }
    case 0x87: /* Mode Page Policy, mandatory */
    {
        outbuf[buflen++] = 0x3f; /* apply to all mode pages and subpages */
        outbuf[buflen++] = 0xff;
        outbuf[buflen++] = 0; /* shared */
        outbuf[buflen++] = 0;
        break;
    }
    default:
        return SCSI_COMMAND_FAIL;
    }
    /* done with EVPD */
    assert(buflen - start <= 255);
    outbuf[start - 1] = buflen - start;
    return buflen;
}

static int ufs_emulate_wlun_inquiry(UfsRequest *req, uint8_t *outbuf,
                                    uint32_t outbuf_len)
{
    if (outbuf_len < SCSI_INQUIRY_LEN) {
        return 0;
    }

    if (req->req_upiu.sc.cdb[1] & 0x1) {
        /* Vital product data */
        return ufs_scsi_emulate_vpd_page(req, outbuf, outbuf_len);
    }

    /* Standard INQUIRY data */
    if (req->req_upiu.sc.cdb[2] != 0) {
        return SCSI_COMMAND_FAIL;
    }

    outbuf[0] = TYPE_WLUN;
    outbuf[1] = 0;
    outbuf[2] = 0x6; /* SPC-4 */
    outbuf[3] = 0x2;
    outbuf[4] = 31;
    outbuf[5] = 0;
    outbuf[6] = 0;
    outbuf[7] = 0x2;
    strpadcpy((char *)&outbuf[8], 8, "QEMU", ' ');
    strpadcpy((char *)&outbuf[16], 16, "QEMU UFS", ' ');
    memset(&outbuf[32], 0, 4);

    return SCSI_INQUIRY_LEN;
}

static UfsReqResult ufs_emulate_scsi_cmd(UfsLu *lu, UfsRequest *req)
{
    uint8_t lun = lu->lun;
    uint8_t outbuf[4096];
    uint8_t sense_buf[UFS_SENSE_SIZE];
    uint8_t scsi_status;
    int len = 0;

    switch (req->req_upiu.sc.cdb[0]) {
    case REPORT_LUNS:
        len = ufs_emulate_report_luns(req, outbuf, sizeof(outbuf));
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
            scsi_status = CHECK_CONDITION;
        } else {
            scsi_status = GOOD;
        }
        break;
    case INQUIRY:
        len = ufs_emulate_wlun_inquiry(req, outbuf, sizeof(outbuf));
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
            scsi_status = CHECK_CONDITION;
        } else {
            scsi_status = GOOD;
        }
        break;
    case REQUEST_SENSE:
        /* Just return no sense data */
        len = scsi_build_sense_buf(outbuf, sizeof(outbuf), SENSE_CODE(NO_SENSE),
                                   true);
        scsi_status = GOOD;
        break;
    case START_STOP:
        /* TODO: Revisit it when Power Management is implemented */
        if (lun == UFS_UPIU_UFS_DEVICE_WLUN) {
            scsi_status = GOOD;
            break;
        }
        /* fallthrough */
    default:
        scsi_build_sense(sense_buf, SENSE_CODE(INVALID_OPCODE));
        scsi_status = CHECK_CONDITION;
    }

    len = MIN(len, (int)req->data_len);
    if (scsi_status == GOOD && len > 0 &&
        dma_buf_read(outbuf, len, NULL, req->sg, MEMTXATTRS_UNSPECIFIED) !=
            MEMTX_OK) {
        return UFS_REQUEST_FAIL;
    }

    ufs_build_scsi_response_upiu(req, sense_buf, sizeof(sense_buf), len,
                                 scsi_status);
    return UFS_REQUEST_SUCCESS;
}

static UfsReqResult ufs_process_scsi_cmd(UfsLu *lu, UfsRequest *req)
{
    uint8_t task_tag = req->req_upiu.header.task_tag;

    /*
     * Each ufs-lu has its own independent virtual SCSI bus. Therefore, we can't
     * use scsi_target_emulate_report_luns() which gets all lu information over
     * the SCSI bus. Therefore, we use ufs_emulate_scsi_cmd() like the
     * well-known lu.
     */
    if (req->req_upiu.sc.cdb[0] == REPORT_LUNS) {
        return ufs_emulate_scsi_cmd(lu, req);
    }

    SCSIRequest *scsi_req =
        scsi_req_new(lu->scsi_dev, task_tag, lu->lun, req->req_upiu.sc.cdb,
                     UFS_CDB_SIZE, req);

    uint32_t len = scsi_req_enqueue(scsi_req);
    if (len) {
        scsi_req_continue(scsi_req);
    }

    return UFS_REQUEST_NO_COMPLETE;
}

static const Property ufs_lu_props[] = {
    DEFINE_PROP_DRIVE("drive", UfsLu, conf.blk),
    DEFINE_PROP_UINT8("lun", UfsLu, lun, 0),
};

static bool ufs_add_lu(UfsHc *u, UfsLu *lu, Error **errp)
{
    BlockBackend *blk = lu->conf.blk;
    int64_t brdv_len = blk_getlength(blk);
    uint64_t raw_dev_cap =
        be64_to_cpu(u->geometry_desc.total_raw_device_capacity);

    if (u->device_desc.number_lu >= UFS_MAX_LUS) {
        error_setg(errp, "ufs host controller has too many logical units.");
        return false;
    }

    if (u->lus[lu->lun] != NULL) {
        error_setg(errp, "ufs logical unit %d already exists.", lu->lun);
        return false;
    }

    u->lus[lu->lun] = lu;
    u->device_desc.number_lu++;
    raw_dev_cap += (brdv_len >> UFS_GEOMETRY_CAPACITY_SHIFT);
    u->geometry_desc.total_raw_device_capacity = cpu_to_be64(raw_dev_cap);
    return true;
}

void ufs_init_wlu(UfsLu *wlu, uint8_t wlun)
{
    wlu->lun = wlun;
    wlu->scsi_op = &ufs_emulate_scsi_cmd;
}

static void ufs_init_lu(UfsLu *lu)
{
    BlockBackend *blk = lu->conf.blk;
    int64_t brdv_len = blk_getlength(blk);

    memset(&lu->unit_desc, 0, sizeof(lu->unit_desc));
    lu->unit_desc.length = sizeof(UnitDescriptor);
    lu->unit_desc.descriptor_idn = UFS_QUERY_DESC_IDN_UNIT;
    lu->unit_desc.lu_enable = 0x01;
    lu->unit_desc.logical_block_size = UFS_BLOCK_SIZE_SHIFT;
    lu->unit_desc.unit_index = lu->lun;
    lu->unit_desc.logical_block_count =
        cpu_to_be64(brdv_len / (1 << lu->unit_desc.logical_block_size));

    lu->scsi_op = &ufs_process_scsi_cmd;
}

static bool ufs_lu_check_constraints(UfsLu *lu, Error **errp)
{
    if (!lu->conf.blk) {
        error_setg(errp, "drive property not set");
        return false;
    }

    if (lu->lun >= UFS_MAX_LUS) {
        error_setg(errp, "lun must be between 0 and %d", UFS_MAX_LUS - 1);
        return false;
    }

    return true;
}

static void ufs_init_scsi_device(UfsLu *lu, BlockBackend *blk, Error **errp)
{
    DeviceState *scsi_dev;

    scsi_bus_init(&lu->bus, sizeof(lu->bus), DEVICE(lu), &ufs_scsi_info);

    blk_ref(blk);
    blk_detach_dev(blk, DEVICE(lu));
    lu->conf.blk = NULL;

    /*
     * The ufs-lu is the device that is wrapping the scsi-hd. It owns a virtual
     * SCSI bus that serves the scsi-hd.
     */
    scsi_dev = qdev_new("scsi-hd");
    object_property_add_child(OBJECT(&lu->bus), "ufs-scsi", OBJECT(scsi_dev));

    qdev_prop_set_uint32(scsi_dev, "physical_block_size", UFS_BLOCK_SIZE);
    qdev_prop_set_uint32(scsi_dev, "logical_block_size", UFS_BLOCK_SIZE);
    qdev_prop_set_uint32(scsi_dev, "scsi-id", 0);
    qdev_prop_set_uint32(scsi_dev, "lun", lu->lun);
    if (!qdev_prop_set_drive_err(scsi_dev, "drive", blk, errp)) {
        object_unparent(OBJECT(scsi_dev));
        return;
    }

    if (!qdev_realize_and_unref(scsi_dev, &lu->bus.qbus, errp)) {
        object_unparent(OBJECT(scsi_dev));
        return;
    }

    blk_unref(blk);
    lu->scsi_dev = SCSI_DEVICE(scsi_dev);
}

static void ufs_lu_realize(DeviceState *dev, Error **errp)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, dev);
    BusState *s = qdev_get_parent_bus(dev);
    UfsHc *u = UFS(s->parent);
    BlockBackend *blk = lu->conf.blk;

    if (!ufs_lu_check_constraints(lu, errp)) {
        return;
    }

    if (!blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (!blkconf_blocksizes(&lu->conf, errp)) {
        return;
    }

    if (!blkconf_apply_backend_options(&lu->conf, !blk_supports_write_perm(blk),
                                       true, errp)) {
        return;
    }

    ufs_init_lu(lu);
    if (!ufs_add_lu(u, lu, errp)) {
        return;
    }

    ufs_init_scsi_device(lu, blk, errp);
}

static void ufs_lu_unrealize(DeviceState *dev)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, dev);

    if (lu->scsi_dev) {
        object_unref(OBJECT(lu->scsi_dev));
        lu->scsi_dev = NULL;
    }
}

static void ufs_lu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ufs_lu_realize;
    dc->unrealize = ufs_lu_unrealize;
    dc->bus_type = TYPE_UFS_BUS;
    device_class_set_props(dc, ufs_lu_props);
    dc->desc = "Virtual UFS logical unit";
}

static const TypeInfo ufs_lu_info = {
    .name = TYPE_UFS_LU,
    .parent = TYPE_DEVICE,
    .class_init = ufs_lu_class_init,
    .instance_size = sizeof(UfsLu),
};

static void ufs_lu_register_types(void)
{
    type_register_static(&ufs_lu_info);
}

type_init(ufs_lu_register_types)
