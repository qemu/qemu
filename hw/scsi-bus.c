#include "hw.h"
#include "qemu-error.h"
#include "scsi.h"
#include "scsi-defs.h"
#include "qdev.h"
#include "blockdev.h"
#include "trace.h"

static char *scsibus_get_fw_dev_path(DeviceState *dev);
static int scsi_req_parse(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf);
static int scsi_build_sense(uint8_t *in_buf, int in_len,
                            uint8_t *buf, int len, bool fixed);

static struct BusInfo scsi_bus_info = {
    .name  = "SCSI",
    .size  = sizeof(SCSIBus),
    .get_fw_dev_path = scsibus_get_fw_dev_path,
    .props = (Property[]) {
        DEFINE_PROP_UINT32("scsi-id", SCSIDevice, id, -1),
        DEFINE_PROP_UINT32("lun", SCSIDevice, lun, 0),
        DEFINE_PROP_END_OF_LIST(),
    },
};
static int next_scsi_bus;

/* Create a scsi bus, and attach devices to it.  */
void scsi_bus_new(SCSIBus *bus, DeviceState *host, int tcq, int ndev,
                  const SCSIBusOps *ops)
{
    qbus_create_inplace(&bus->qbus, &scsi_bus_info, host, NULL);
    bus->busnr = next_scsi_bus++;
    bus->tcq = tcq;
    bus->ndev = ndev;
    bus->ops = ops;
    bus->qbus.allow_hotplug = 1;
}

static int scsi_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    SCSIDevice *dev = DO_UPCAST(SCSIDevice, qdev, qdev);
    SCSIDeviceInfo *info = DO_UPCAST(SCSIDeviceInfo, qdev, base);
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, dev->qdev.parent_bus);
    int rc = -1;

    if (dev->id == -1) {
        for (dev->id = 0; dev->id < bus->ndev; dev->id++) {
            if (bus->devs[dev->id] == NULL)
                break;
        }
    }
    if (dev->id >= bus->ndev) {
        error_report("bad scsi device id: %d", dev->id);
        goto err;
    }

    if (bus->devs[dev->id]) {
        qdev_free(&bus->devs[dev->id]->qdev);
    }
    bus->devs[dev->id] = dev;

    dev->info = info;
    QTAILQ_INIT(&dev->requests);
    rc = dev->info->init(dev);
    if (rc != 0) {
        bus->devs[dev->id] = NULL;
    }

err:
    return rc;
}

static int scsi_qdev_exit(DeviceState *qdev)
{
    SCSIDevice *dev = DO_UPCAST(SCSIDevice, qdev, qdev);
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, dev->qdev.parent_bus);

    assert(bus->devs[dev->id] != NULL);
    if (bus->devs[dev->id]->info->destroy) {
        bus->devs[dev->id]->info->destroy(bus->devs[dev->id]);
    }
    bus->devs[dev->id] = NULL;
    return 0;
}

void scsi_qdev_register(SCSIDeviceInfo *info)
{
    info->qdev.bus_info = &scsi_bus_info;
    info->qdev.init     = scsi_qdev_init;
    info->qdev.unplug   = qdev_simple_unplug_cb;
    info->qdev.exit     = scsi_qdev_exit;
    qdev_register(&info->qdev);
}

/* handle legacy '-drive if=scsi,...' cmd line args */
SCSIDevice *scsi_bus_legacy_add_drive(SCSIBus *bus, BlockDriverState *bdrv,
                                      int unit, bool removable)
{
    const char *driver;
    DeviceState *dev;

    driver = bdrv_is_sg(bdrv) ? "scsi-generic" : "scsi-disk";
    dev = qdev_create(&bus->qbus, driver);
    qdev_prop_set_uint32(dev, "scsi-id", unit);
    if (qdev_prop_exists(dev, "removable")) {
        qdev_prop_set_bit(dev, "removable", removable);
    }
    if (qdev_prop_set_drive(dev, "drive", bdrv) < 0) {
        qdev_free(dev);
        return NULL;
    }
    if (qdev_init(dev) < 0)
        return NULL;
    return DO_UPCAST(SCSIDevice, qdev, dev);
}

int scsi_bus_legacy_handle_cmdline(SCSIBus *bus)
{
    Location loc;
    DriveInfo *dinfo;
    int res = 0, unit;

    loc_push_none(&loc);
    for (unit = 0; unit < bus->ndev; unit++) {
        dinfo = drive_get(IF_SCSI, bus->busnr, unit);
        if (dinfo == NULL) {
            continue;
        }
        qemu_opts_loc_restore(dinfo->opts);
        if (!scsi_bus_legacy_add_drive(bus, dinfo->bdrv, unit, false)) {
            res = -1;
            break;
        }
    }
    loc_pop(&loc);
    return res;
}

/* SCSIReqOps implementation for invalid commands.  */

static int32_t scsi_invalid_command(SCSIRequest *req, uint8_t *buf)
{
    scsi_req_build_sense(req, SENSE_CODE(INVALID_OPCODE));
    scsi_req_complete(req, CHECK_CONDITION);
    return 0;
}

struct SCSIReqOps reqops_invalid_opcode = {
    .size         = sizeof(SCSIRequest),
    .send_command = scsi_invalid_command
};

/* SCSIReqOps implementation for REPORT LUNS and for commands sent to
   an invalid LUN.  */

typedef struct SCSITargetReq SCSITargetReq;

struct SCSITargetReq {
    SCSIRequest req;
    int len;
    uint8_t buf[64];
};

static void store_lun(uint8_t *outbuf, int lun)
{
    if (lun < 256) {
        outbuf[1] = lun;
        return;
    }
    outbuf[1] = (lun & 255);
    outbuf[0] = (lun >> 8) | 0x40;
}

static bool scsi_target_emulate_report_luns(SCSITargetReq *r)
{
    int len;
    if (r->req.cmd.xfer < 16) {
        return false;
    }
    if (r->req.cmd.buf[2] > 2) {
        return false;
    }
    len = MIN(sizeof r->buf, r->req.cmd.xfer);
    memset(r->buf, 0, len);
    if (r->req.dev->lun != 0) {
        r->buf[3] = 16;
        r->len = 24;
        store_lun(&r->buf[16], r->req.dev->lun);
    } else {
        r->buf[3] = 8;
        r->len = 16;
    }
    return true;
}

static bool scsi_target_emulate_inquiry(SCSITargetReq *r)
{
    assert(r->req.dev->lun != r->req.lun);
    if (r->req.cmd.buf[1] & 0x2) {
        /* Command support data - optional, not implemented */
        return false;
    }

    if (r->req.cmd.buf[1] & 0x1) {
        /* Vital product data */
        uint8_t page_code = r->req.cmd.buf[2];
        if (r->req.cmd.xfer < 4) {
            return false;
        }

        r->buf[r->len++] = page_code ; /* this page */
        r->buf[r->len++] = 0x00;

        switch (page_code) {
        case 0x00: /* Supported page codes, mandatory */
        {
            int pages;
            pages = r->len++;
            r->buf[r->len++] = 0x00; /* list of supported pages (this page) */
            r->buf[pages] = r->len - pages - 1; /* number of pages */
            break;
        }
        default:
            return false;
        }
        /* done with EVPD */
        assert(r->len < sizeof(r->buf));
        r->len = MIN(r->req.cmd.xfer, r->len);
        return true;
    }

    /* Standard INQUIRY data */
    if (r->req.cmd.buf[2] != 0) {
        return false;
    }

    /* PAGE CODE == 0 */
    if (r->req.cmd.xfer < 5) {
        return -1;
    }

    r->len = MIN(r->req.cmd.xfer, 36);
    memset(r->buf, 0, r->len);
    if (r->req.lun != 0) {
        r->buf[0] = TYPE_NO_LUN;
    } else {
        r->buf[0] = TYPE_NOT_PRESENT | TYPE_INACTIVE;
        r->buf[2] = 5; /* Version */
        r->buf[3] = 2 | 0x10; /* HiSup, response data format */
        r->buf[4] = r->len - 5; /* Additional Length = (Len - 1) - 4 */
        r->buf[7] = 0x10 | (r->req.bus->tcq ? 0x02 : 0); /* Sync, TCQ.  */
        memcpy(&r->buf[8], "QEMU    ", 8);
        memcpy(&r->buf[16], "QEMU TARGET     ", 16);
        strncpy((char *) &r->buf[32], QEMU_VERSION, 4);
    }
    return true;
}

static int32_t scsi_target_send_command(SCSIRequest *req, uint8_t *buf)
{
    SCSITargetReq *r = DO_UPCAST(SCSITargetReq, req, req);

    switch (buf[0]) {
    case REPORT_LUNS:
        if (!scsi_target_emulate_report_luns(r)) {
            goto illegal_request;
        }
        break;
    case INQUIRY:
        if (!scsi_target_emulate_inquiry(r)) {
            goto illegal_request;
        }
        break;
    case REQUEST_SENSE:
        if (req->cmd.xfer < 4) {
            goto illegal_request;
        }
        r->len = scsi_device_get_sense(r->req.dev, r->buf, req->cmd.xfer,
                                       (req->cmd.buf[1] & 1) == 0);
        break;
    default:
        scsi_req_build_sense(req, SENSE_CODE(LUN_NOT_SUPPORTED));
        scsi_req_complete(req, CHECK_CONDITION);
        return 0;
    illegal_request:
        scsi_req_build_sense(req, SENSE_CODE(INVALID_FIELD));
        scsi_req_complete(req, CHECK_CONDITION);
        return 0;
    }

    if (!r->len) {
        scsi_req_complete(req, GOOD);
    }
    return r->len;
}

static void scsi_target_read_data(SCSIRequest *req)
{
    SCSITargetReq *r = DO_UPCAST(SCSITargetReq, req, req);
    uint32_t n;

    n = r->len;
    if (n > 0) {
        r->len = 0;
        scsi_req_data(&r->req, n);
    } else {
        scsi_req_complete(&r->req, GOOD);
    }
}

static uint8_t *scsi_target_get_buf(SCSIRequest *req)
{
    SCSITargetReq *r = DO_UPCAST(SCSITargetReq, req, req);

    return r->buf;
}

struct SCSIReqOps reqops_target_command = {
    .size         = sizeof(SCSITargetReq),
    .send_command = scsi_target_send_command,
    .read_data    = scsi_target_read_data,
    .get_buf      = scsi_target_get_buf,
};


SCSIRequest *scsi_req_alloc(SCSIReqOps *reqops, SCSIDevice *d, uint32_t tag,
                            uint32_t lun, void *hba_private)
{
    SCSIRequest *req;

    req = qemu_mallocz(reqops->size);
    req->refcount = 1;
    req->bus = scsi_bus_from_device(d);
    req->dev = d;
    req->tag = tag;
    req->lun = lun;
    req->hba_private = hba_private;
    req->status = -1;
    req->sense_len = 0;
    req->ops = reqops;
    trace_scsi_req_alloc(req->dev->id, req->lun, req->tag);
    return req;
}

SCSIRequest *scsi_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun,
                          uint8_t *buf, void *hba_private)
{
    SCSIRequest *req;
    SCSICommand cmd;

    if (scsi_req_parse(&cmd, d, buf) != 0) {
        trace_scsi_req_parse_bad(d->id, lun, tag, buf[0]);
        req = scsi_req_alloc(&reqops_invalid_opcode, d, tag, lun, hba_private);
    } else {
        trace_scsi_req_parsed(d->id, lun, tag, buf[0],
                              cmd.mode, cmd.xfer);
        if (req->cmd.lba != -1) {
            trace_scsi_req_parsed_lba(d->id, lun, tag, buf[0],
                                      cmd.lba);
        }

        if (lun != d->lun ||
            buf[0] == REPORT_LUNS ||
            buf[0] == REQUEST_SENSE) {
            req = scsi_req_alloc(&reqops_target_command, d, tag, lun,
                                 hba_private);
        } else {
            req = d->info->alloc_req(d, tag, lun, hba_private);
        }
    }

    req->cmd = cmd;
    return req;
}

uint8_t *scsi_req_get_buf(SCSIRequest *req)
{
    return req->ops->get_buf(req);
}

int scsi_req_get_sense(SCSIRequest *req, uint8_t *buf, int len)
{
    assert(len >= 14);
    if (!req->sense_len) {
        return 0;
    }
    return scsi_build_sense(req->sense, req->sense_len, buf, len, true);
}

int scsi_device_get_sense(SCSIDevice *dev, uint8_t *buf, int len, bool fixed)
{
    return scsi_build_sense(dev->sense, dev->sense_len, buf, len, fixed);
}

void scsi_req_build_sense(SCSIRequest *req, SCSISense sense)
{
    trace_scsi_req_build_sense(req->dev->id, req->lun, req->tag,
                               sense.key, sense.asc, sense.ascq);
    memset(req->sense, 0, 18);
    req->sense[0] = 0xf0;
    req->sense[2] = sense.key;
    req->sense[12] = sense.asc;
    req->sense[13] = sense.ascq;
    req->sense_len = 18;
}

int32_t scsi_req_enqueue(SCSIRequest *req)
{
    int32_t rc;

    assert(!req->enqueued);
    scsi_req_ref(req);
    req->enqueued = true;
    QTAILQ_INSERT_TAIL(&req->dev->requests, req, next);

    scsi_req_ref(req);
    rc = req->ops->send_command(req, req->cmd.buf);
    scsi_req_unref(req);
    return rc;
}

static void scsi_req_dequeue(SCSIRequest *req)
{
    trace_scsi_req_dequeue(req->dev->id, req->lun, req->tag);
    if (req->enqueued) {
        QTAILQ_REMOVE(&req->dev->requests, req, next);
        req->enqueued = false;
        scsi_req_unref(req);
    }
}

static int scsi_req_length(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf)
{
    switch (buf[0] >> 5) {
    case 0:
        cmd->xfer = buf[4];
        cmd->len = 6;
        /* length 0 means 256 blocks */
        if (cmd->xfer == 0) {
            cmd->xfer = 256;
        }
        break;
    case 1:
    case 2:
        cmd->xfer = buf[8] | (buf[7] << 8);
        cmd->len = 10;
        break;
    case 4:
        cmd->xfer = buf[13] | (buf[12] << 8) | (buf[11] << 16) | (buf[10] << 24);
        cmd->len = 16;
        break;
    case 5:
        cmd->xfer = buf[9] | (buf[8] << 8) | (buf[7] << 16) | (buf[6] << 24);
        cmd->len = 12;
        break;
    default:
        return -1;
    }

    switch (buf[0]) {
    case TEST_UNIT_READY:
    case REWIND:
    case START_STOP:
    case SEEK_6:
    case WRITE_FILEMARKS:
    case SPACE:
    case RESERVE:
    case RELEASE:
    case ERASE:
    case ALLOW_MEDIUM_REMOVAL:
    case VERIFY_10:
    case SEEK_10:
    case SYNCHRONIZE_CACHE:
    case LOCK_UNLOCK_CACHE:
    case LOAD_UNLOAD:
    case SET_CD_SPEED:
    case SET_LIMITS:
    case WRITE_LONG_10:
    case MOVE_MEDIUM:
    case UPDATE_BLOCK:
        cmd->xfer = 0;
        break;
    case MODE_SENSE:
        break;
    case WRITE_SAME_10:
        cmd->xfer = 1;
        break;
    case READ_CAPACITY_10:
        cmd->xfer = 8;
        break;
    case READ_BLOCK_LIMITS:
        cmd->xfer = 6;
        break;
    case READ_POSITION:
        cmd->xfer = 20;
        break;
    case SEND_VOLUME_TAG:
        cmd->xfer *= 40;
        break;
    case MEDIUM_SCAN:
        cmd->xfer *= 8;
        break;
    case WRITE_10:
    case WRITE_VERIFY_10:
    case WRITE_6:
    case WRITE_12:
    case WRITE_VERIFY_12:
    case WRITE_16:
    case WRITE_VERIFY_16:
        cmd->xfer *= dev->blocksize;
        break;
    case READ_10:
    case READ_6:
    case READ_REVERSE:
    case RECOVER_BUFFERED_DATA:
    case READ_12:
    case READ_16:
        cmd->xfer *= dev->blocksize;
        break;
    case INQUIRY:
        cmd->xfer = buf[4] | (buf[3] << 8);
        break;
    case MAINTENANCE_OUT:
    case MAINTENANCE_IN:
        if (dev->type == TYPE_ROM) {
            /* GPCMD_REPORT_KEY and GPCMD_SEND_KEY from multi media commands */
            cmd->xfer = buf[9] | (buf[8] << 8);
        }
        break;
    }
    return 0;
}

static int scsi_req_stream_length(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf)
{
    switch (buf[0]) {
    /* stream commands */
    case READ_6:
    case READ_REVERSE:
    case RECOVER_BUFFERED_DATA:
    case WRITE_6:
        cmd->len = 6;
        cmd->xfer = buf[4] | (buf[3] << 8) | (buf[2] << 16);
        if (buf[1] & 0x01) { /* fixed */
            cmd->xfer *= dev->blocksize;
        }
        break;
    case REWIND:
    case START_STOP:
        cmd->len = 6;
        cmd->xfer = 0;
        break;
    /* generic commands */
    default:
        return scsi_req_length(cmd, dev, buf);
    }
    return 0;
}

static void scsi_cmd_xfer_mode(SCSICommand *cmd)
{
    switch (cmd->buf[0]) {
    case WRITE_6:
    case WRITE_10:
    case WRITE_VERIFY_10:
    case WRITE_12:
    case WRITE_VERIFY_12:
    case WRITE_16:
    case WRITE_VERIFY_16:
    case COPY:
    case COPY_VERIFY:
    case COMPARE:
    case CHANGE_DEFINITION:
    case LOG_SELECT:
    case MODE_SELECT:
    case MODE_SELECT_10:
    case SEND_DIAGNOSTIC:
    case WRITE_BUFFER:
    case FORMAT_UNIT:
    case REASSIGN_BLOCKS:
    case SEARCH_EQUAL:
    case SEARCH_HIGH:
    case SEARCH_LOW:
    case UPDATE_BLOCK:
    case WRITE_LONG_10:
    case WRITE_SAME_10:
    case SEARCH_HIGH_12:
    case SEARCH_EQUAL_12:
    case SEARCH_LOW_12:
    case MEDIUM_SCAN:
    case SEND_VOLUME_TAG:
    case PERSISTENT_RESERVE_OUT:
    case MAINTENANCE_OUT:
        cmd->mode = SCSI_XFER_TO_DEV;
        break;
    default:
        if (cmd->xfer)
            cmd->mode = SCSI_XFER_FROM_DEV;
        else {
            cmd->mode = SCSI_XFER_NONE;
        }
        break;
    }
}

static uint64_t scsi_cmd_lba(SCSICommand *cmd)
{
    uint8_t *buf = cmd->buf;
    uint64_t lba;

    switch (buf[0] >> 5) {
    case 0:
        lba = (uint64_t) buf[3] | ((uint64_t) buf[2] << 8) |
              (((uint64_t) buf[1] & 0x1f) << 16);
        break;
    case 1:
    case 2:
        lba = (uint64_t) buf[5] | ((uint64_t) buf[4] << 8) |
              ((uint64_t) buf[3] << 16) | ((uint64_t) buf[2] << 24);
        break;
    case 4:
        lba = (uint64_t) buf[9] | ((uint64_t) buf[8] << 8) |
              ((uint64_t) buf[7] << 16) | ((uint64_t) buf[6] << 24) |
              ((uint64_t) buf[5] << 32) | ((uint64_t) buf[4] << 40) |
              ((uint64_t) buf[3] << 48) | ((uint64_t) buf[2] << 56);
        break;
    case 5:
        lba = (uint64_t) buf[5] | ((uint64_t) buf[4] << 8) |
              ((uint64_t) buf[3] << 16) | ((uint64_t) buf[2] << 24);
        break;
    default:
        lba = -1;

    }
    return lba;
}

int scsi_req_parse(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf)
{
    int rc;

    if (dev->type == TYPE_TAPE) {
        rc = scsi_req_stream_length(cmd, dev, buf);
    } else {
        rc = scsi_req_length(cmd, dev, buf);
    }
    if (rc != 0)
        return rc;

    memcpy(cmd->buf, buf, cmd->len);
    scsi_cmd_xfer_mode(cmd);
    cmd->lba = scsi_cmd_lba(cmd);
    return 0;
}

/*
 * Predefined sense codes
 */

/* No sense data available */
const struct SCSISense sense_code_NO_SENSE = {
    .key = NO_SENSE , .asc = 0x00 , .ascq = 0x00
};

/* LUN not ready, Manual intervention required */
const struct SCSISense sense_code_LUN_NOT_READY = {
    .key = NOT_READY, .asc = 0x04, .ascq = 0x03
};

/* LUN not ready, Medium not present */
const struct SCSISense sense_code_NO_MEDIUM = {
    .key = NOT_READY, .asc = 0x3a, .ascq = 0x00
};

/* Hardware error, internal target failure */
const struct SCSISense sense_code_TARGET_FAILURE = {
    .key = HARDWARE_ERROR, .asc = 0x44, .ascq = 0x00
};

/* Illegal request, invalid command operation code */
const struct SCSISense sense_code_INVALID_OPCODE = {
    .key = ILLEGAL_REQUEST, .asc = 0x20, .ascq = 0x00
};

/* Illegal request, LBA out of range */
const struct SCSISense sense_code_LBA_OUT_OF_RANGE = {
    .key = ILLEGAL_REQUEST, .asc = 0x21, .ascq = 0x00
};

/* Illegal request, Invalid field in CDB */
const struct SCSISense sense_code_INVALID_FIELD = {
    .key = ILLEGAL_REQUEST, .asc = 0x24, .ascq = 0x00
};

/* Illegal request, LUN not supported */
const struct SCSISense sense_code_LUN_NOT_SUPPORTED = {
    .key = ILLEGAL_REQUEST, .asc = 0x25, .ascq = 0x00
};

/* Illegal request, Saving parameters not supported */
const struct SCSISense sense_code_SAVING_PARAMS_NOT_SUPPORTED = {
    .key = ILLEGAL_REQUEST, .asc = 0x39, .ascq = 0x00
};

/* Illegal request, Incompatible medium installed */
const struct SCSISense sense_code_INCOMPATIBLE_MEDIUM = {
    .key = ILLEGAL_REQUEST, .asc = 0x30, .ascq = 0x00
};

/* Command aborted, I/O process terminated */
const struct SCSISense sense_code_IO_ERROR = {
    .key = ABORTED_COMMAND, .asc = 0x00, .ascq = 0x06
};

/* Command aborted, I_T Nexus loss occurred */
const struct SCSISense sense_code_I_T_NEXUS_LOSS = {
    .key = ABORTED_COMMAND, .asc = 0x29, .ascq = 0x07
};

/* Command aborted, Logical Unit failure */
const struct SCSISense sense_code_LUN_FAILURE = {
    .key = ABORTED_COMMAND, .asc = 0x3e, .ascq = 0x01
};

/* Unit attention, Power on, reset or bus device reset occurred */
const struct SCSISense sense_code_RESET = {
    .key = UNIT_ATTENTION, .asc = 0x29, .ascq = 0x00
};

/* Unit attention, Medium may have changed */
const struct SCSISense sense_code_MEDIUM_CHANGED = {
    .key = UNIT_ATTENTION, .asc = 0x28, .ascq = 0x00
};

/* Unit attention, Reported LUNs data has changed */
const struct SCSISense sense_code_REPORTED_LUNS_CHANGED = {
    .key = UNIT_ATTENTION, .asc = 0x3f, .ascq = 0x0e
};

/* Unit attention, Device internal reset */
const struct SCSISense sense_code_DEVICE_INTERNAL_RESET = {
    .key = UNIT_ATTENTION, .asc = 0x29, .ascq = 0x04
};

/*
 * scsi_build_sense
 *
 * Convert between fixed and descriptor sense buffers
 */
int scsi_build_sense(uint8_t *in_buf, int in_len,
                     uint8_t *buf, int len, bool fixed)
{
    bool fixed_in;
    SCSISense sense;
    if (!fixed && len < 8) {
        return 0;
    }

    if (in_len == 0) {
        sense.key = NO_SENSE;
        sense.asc = 0;
        sense.ascq = 0;
    } else {
        fixed_in = (in_buf[0] & 2) == 0;

        if (fixed == fixed_in) {
            memcpy(buf, in_buf, MIN(len, in_len));
            return MIN(len, in_len);
        }

        if (fixed_in) {
            sense.key = in_buf[2];
            sense.asc = in_buf[12];
            sense.ascq = in_buf[13];
        } else {
            sense.key = in_buf[1];
            sense.asc = in_buf[2];
            sense.ascq = in_buf[3];
        }
    }

    memset(buf, 0, len);
    if (fixed) {
        /* Return fixed format sense buffer */
        buf[0] = 0xf0;
        buf[2] = sense.key;
        buf[7] = 7;
        buf[12] = sense.asc;
        buf[13] = sense.ascq;
        return MIN(len, 18);
    } else {
        /* Return descriptor format sense buffer */
        buf[0] = 0x72;
        buf[1] = sense.key;
        buf[2] = sense.asc;
        buf[3] = sense.ascq;
        return 8;
    }
}

static const char *scsi_command_name(uint8_t cmd)
{
    static const char *names[] = {
        [ TEST_UNIT_READY          ] = "TEST_UNIT_READY",
        [ REWIND                   ] = "REWIND",
        [ REQUEST_SENSE            ] = "REQUEST_SENSE",
        [ FORMAT_UNIT              ] = "FORMAT_UNIT",
        [ READ_BLOCK_LIMITS        ] = "READ_BLOCK_LIMITS",
        [ REASSIGN_BLOCKS          ] = "REASSIGN_BLOCKS",
        [ READ_6                   ] = "READ_6",
        [ WRITE_6                  ] = "WRITE_6",
        [ SEEK_6                   ] = "SEEK_6",
        [ READ_REVERSE             ] = "READ_REVERSE",
        [ WRITE_FILEMARKS          ] = "WRITE_FILEMARKS",
        [ SPACE                    ] = "SPACE",
        [ INQUIRY                  ] = "INQUIRY",
        [ RECOVER_BUFFERED_DATA    ] = "RECOVER_BUFFERED_DATA",
        [ MAINTENANCE_IN           ] = "MAINTENANCE_IN",
        [ MAINTENANCE_OUT          ] = "MAINTENANCE_OUT",
        [ MODE_SELECT              ] = "MODE_SELECT",
        [ RESERVE                  ] = "RESERVE",
        [ RELEASE                  ] = "RELEASE",
        [ COPY                     ] = "COPY",
        [ ERASE                    ] = "ERASE",
        [ MODE_SENSE               ] = "MODE_SENSE",
        [ START_STOP               ] = "START_STOP",
        [ RECEIVE_DIAGNOSTIC       ] = "RECEIVE_DIAGNOSTIC",
        [ SEND_DIAGNOSTIC          ] = "SEND_DIAGNOSTIC",
        [ ALLOW_MEDIUM_REMOVAL     ] = "ALLOW_MEDIUM_REMOVAL",
        [ READ_CAPACITY_10         ] = "READ_CAPACITY_10",
        [ READ_10                  ] = "READ_10",
        [ WRITE_10                 ] = "WRITE_10",
        [ SEEK_10                  ] = "SEEK_10",
        [ WRITE_VERIFY_10          ] = "WRITE_VERIFY_10",
        [ VERIFY_10                ] = "VERIFY_10",
        [ SEARCH_HIGH              ] = "SEARCH_HIGH",
        [ SEARCH_EQUAL             ] = "SEARCH_EQUAL",
        [ SEARCH_LOW               ] = "SEARCH_LOW",
        [ SET_LIMITS               ] = "SET_LIMITS",
        [ PRE_FETCH                ] = "PRE_FETCH",
        /* READ_POSITION and PRE_FETCH use the same operation code */
        [ SYNCHRONIZE_CACHE        ] = "SYNCHRONIZE_CACHE",
        [ LOCK_UNLOCK_CACHE        ] = "LOCK_UNLOCK_CACHE",
        [ READ_DEFECT_DATA         ] = "READ_DEFECT_DATA",
        [ MEDIUM_SCAN              ] = "MEDIUM_SCAN",
        [ COMPARE                  ] = "COMPARE",
        [ COPY_VERIFY              ] = "COPY_VERIFY",
        [ WRITE_BUFFER             ] = "WRITE_BUFFER",
        [ READ_BUFFER              ] = "READ_BUFFER",
        [ UPDATE_BLOCK             ] = "UPDATE_BLOCK",
        [ READ_LONG_10             ] = "READ_LONG_10",
        [ WRITE_LONG_10            ] = "WRITE_LONG_10",
        [ CHANGE_DEFINITION        ] = "CHANGE_DEFINITION",
        [ WRITE_SAME_10            ] = "WRITE_SAME_10",
        [ UNMAP                    ] = "UNMAP",
        [ READ_TOC                 ] = "READ_TOC",
        [ REPORT_DENSITY_SUPPORT   ] = "REPORT_DENSITY_SUPPORT",
        [ GET_CONFIGURATION        ] = "GET_CONFIGURATION",
        [ LOG_SELECT               ] = "LOG_SELECT",
        [ LOG_SENSE                ] = "LOG_SENSE",
        [ MODE_SELECT_10           ] = "MODE_SELECT_10",
        [ RESERVE_10               ] = "RESERVE_10",
        [ RELEASE_10               ] = "RELEASE_10",
        [ MODE_SENSE_10            ] = "MODE_SENSE_10",
        [ PERSISTENT_RESERVE_IN    ] = "PERSISTENT_RESERVE_IN",
        [ PERSISTENT_RESERVE_OUT   ] = "PERSISTENT_RESERVE_OUT",
        [ WRITE_FILEMARKS_16       ] = "WRITE_FILEMARKS_16",
        [ EXTENDED_COPY            ] = "EXTENDED_COPY",
        [ ATA_PASSTHROUGH          ] = "ATA_PASSTHROUGH",
        [ ACCESS_CONTROL_IN        ] = "ACCESS_CONTROL_IN",
        [ ACCESS_CONTROL_OUT       ] = "ACCESS_CONTROL_OUT",
        [ READ_16                  ] = "READ_16",
        [ COMPARE_AND_WRITE        ] = "COMPARE_AND_WRITE",
        [ WRITE_16                 ] = "WRITE_16",
        [ WRITE_VERIFY_16          ] = "WRITE_VERIFY_16",
        [ VERIFY_16                ] = "VERIFY_16",
        [ SYNCHRONIZE_CACHE_16     ] = "SYNCHRONIZE_CACHE_16",
        [ LOCATE_16                ] = "LOCATE_16",
        [ WRITE_SAME_16            ] = "WRITE_SAME_16",
        [ ERASE_16                 ] = "ERASE_16",
        [ SERVICE_ACTION_IN        ] = "SERVICE_ACTION_IN",
        [ WRITE_LONG_16            ] = "WRITE_LONG_16",
        [ REPORT_LUNS              ] = "REPORT_LUNS",
        [ BLANK                    ] = "BLANK",
        [ MAINTENANCE_IN           ] = "MAINTENANCE_IN",
        [ MAINTENANCE_OUT          ] = "MAINTENANCE_OUT",
        [ MOVE_MEDIUM              ] = "MOVE_MEDIUM",
        [ LOAD_UNLOAD              ] = "LOAD_UNLOAD",
        [ READ_12                  ] = "READ_12",
        [ WRITE_12                 ] = "WRITE_12",
        [ WRITE_VERIFY_12          ] = "WRITE_VERIFY_12",
        [ VERIFY_12                ] = "VERIFY_12",
        [ SEARCH_HIGH_12           ] = "SEARCH_HIGH_12",
        [ SEARCH_EQUAL_12          ] = "SEARCH_EQUAL_12",
        [ SEARCH_LOW_12            ] = "SEARCH_LOW_12",
        [ READ_ELEMENT_STATUS      ] = "READ_ELEMENT_STATUS",
        [ SEND_VOLUME_TAG          ] = "SEND_VOLUME_TAG",
        [ READ_DEFECT_DATA_12      ] = "READ_DEFECT_DATA_12",
        [ SET_CD_SPEED             ] = "SET_CD_SPEED",
    };

    if (cmd >= ARRAY_SIZE(names) || names[cmd] == NULL)
        return "*UNKNOWN*";
    return names[cmd];
}

SCSIRequest *scsi_req_ref(SCSIRequest *req)
{
    req->refcount++;
    return req;
}

void scsi_req_unref(SCSIRequest *req)
{
    if (--req->refcount == 0) {
        if (req->ops->free_req) {
            req->ops->free_req(req);
        }
        qemu_free(req);
    }
}

/* Tell the device that we finished processing this chunk of I/O.  It
   will start the next chunk or complete the command.  */
void scsi_req_continue(SCSIRequest *req)
{
    trace_scsi_req_continue(req->dev->id, req->lun, req->tag);
    if (req->cmd.mode == SCSI_XFER_TO_DEV) {
        req->ops->write_data(req);
    } else {
        req->ops->read_data(req);
    }
}

/* Called by the devices when data is ready for the HBA.  The HBA should
   start a DMA operation to read or fill the device's data buffer.
   Once it completes, calling scsi_req_continue will restart I/O.  */
void scsi_req_data(SCSIRequest *req, int len)
{
    trace_scsi_req_data(req->dev->id, req->lun, req->tag, len);
    req->bus->ops->transfer_data(req, len);
}

void scsi_req_print(SCSIRequest *req)
{
    FILE *fp = stderr;
    int i;

    fprintf(fp, "[%s id=%d] %s",
            req->dev->qdev.parent_bus->name,
            req->dev->id,
            scsi_command_name(req->cmd.buf[0]));
    for (i = 1; i < req->cmd.len; i++) {
        fprintf(fp, " 0x%02x", req->cmd.buf[i]);
    }
    switch (req->cmd.mode) {
    case SCSI_XFER_NONE:
        fprintf(fp, " - none\n");
        break;
    case SCSI_XFER_FROM_DEV:
        fprintf(fp, " - from-dev len=%zd\n", req->cmd.xfer);
        break;
    case SCSI_XFER_TO_DEV:
        fprintf(fp, " - to-dev len=%zd\n", req->cmd.xfer);
        break;
    default:
        fprintf(fp, " - Oops\n");
        break;
    }
}

void scsi_req_complete(SCSIRequest *req, int status)
{
    assert(req->status == -1);
    req->status = status;

    assert(req->sense_len < sizeof(req->sense));
    if (status == GOOD) {
        req->sense_len = 0;
    }

    if (req->sense_len) {
        memcpy(req->dev->sense, req->sense, req->sense_len);
    }
    req->dev->sense_len = req->sense_len;

    scsi_req_ref(req);
    scsi_req_dequeue(req);
    req->bus->ops->complete(req, req->status);
    scsi_req_unref(req);
}

void scsi_req_cancel(SCSIRequest *req)
{
    if (req->ops->cancel_io) {
        req->ops->cancel_io(req);
    }
    scsi_req_ref(req);
    scsi_req_dequeue(req);
    if (req->bus->ops->cancel) {
        req->bus->ops->cancel(req);
    }
    scsi_req_unref(req);
}

void scsi_req_abort(SCSIRequest *req, int status)
{
    if (req->ops->cancel_io) {
        req->ops->cancel_io(req);
    }
    scsi_req_complete(req, status);
}

void scsi_device_purge_requests(SCSIDevice *sdev)
{
    SCSIRequest *req;

    while (!QTAILQ_EMPTY(&sdev->requests)) {
        req = QTAILQ_FIRST(&sdev->requests);
        scsi_req_cancel(req);
    }
}

static char *scsibus_get_fw_dev_path(DeviceState *dev)
{
    SCSIDevice *d = DO_UPCAST(SCSIDevice, qdev, dev);
    SCSIBus *bus = scsi_bus_from_device(d);
    char path[100];
    int i;

    for (i = 0; i < bus->ndev; i++) {
        if (bus->devs[i] == d) {
            break;
        }
    }

    assert(i != bus->ndev);

    snprintf(path, sizeof(path), "%s@%x", qdev_fw_name(dev), i);

    return strdup(path);
}
