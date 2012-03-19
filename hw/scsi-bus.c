#include "hw.h"
#include "qemu-error.h"
#include "scsi.h"
#include "scsi-defs.h"
#include "qdev.h"
#include "blockdev.h"
#include "trace.h"
#include "dma.h"

static char *scsibus_get_dev_path(DeviceState *dev);
static char *scsibus_get_fw_dev_path(DeviceState *dev);
static int scsi_req_parse(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf);
static void scsi_req_dequeue(SCSIRequest *req);

static struct BusInfo scsi_bus_info = {
    .name  = "SCSI",
    .size  = sizeof(SCSIBus),
    .get_dev_path = scsibus_get_dev_path,
    .get_fw_dev_path = scsibus_get_fw_dev_path,
    .props = (Property[]) {
        DEFINE_PROP_UINT32("channel", SCSIDevice, channel, 0),
        DEFINE_PROP_UINT32("scsi-id", SCSIDevice, id, -1),
        DEFINE_PROP_UINT32("lun", SCSIDevice, lun, -1),
        DEFINE_PROP_END_OF_LIST(),
    },
};
static int next_scsi_bus;

static int scsi_device_init(SCSIDevice *s)
{
    SCSIDeviceClass *sc = SCSI_DEVICE_GET_CLASS(s);
    if (sc->init) {
        return sc->init(s);
    }
    return 0;
}

static void scsi_device_destroy(SCSIDevice *s)
{
    SCSIDeviceClass *sc = SCSI_DEVICE_GET_CLASS(s);
    if (sc->destroy) {
        sc->destroy(s);
    }
}

static SCSIRequest *scsi_device_alloc_req(SCSIDevice *s, uint32_t tag, uint32_t lun,
                                          uint8_t *buf, void *hba_private)
{
    SCSIDeviceClass *sc = SCSI_DEVICE_GET_CLASS(s);
    if (sc->alloc_req) {
        return sc->alloc_req(s, tag, lun, buf, hba_private);
    }

    return NULL;
}

static void scsi_device_unit_attention_reported(SCSIDevice *s)
{
    SCSIDeviceClass *sc = SCSI_DEVICE_GET_CLASS(s);
    if (sc->unit_attention_reported) {
        sc->unit_attention_reported(s);
    }
}

/* Create a scsi bus, and attach devices to it.  */
void scsi_bus_new(SCSIBus *bus, DeviceState *host, const SCSIBusInfo *info)
{
    qbus_create_inplace(&bus->qbus, &scsi_bus_info, host, NULL);
    bus->busnr = next_scsi_bus++;
    bus->info = info;
    bus->qbus.allow_hotplug = 1;
}

static void scsi_dma_restart_bh(void *opaque)
{
    SCSIDevice *s = opaque;
    SCSIRequest *req, *next;

    qemu_bh_delete(s->bh);
    s->bh = NULL;

    QTAILQ_FOREACH_SAFE(req, &s->requests, next, next) {
        scsi_req_ref(req);
        if (req->retry) {
            req->retry = false;
            switch (req->cmd.mode) {
            case SCSI_XFER_FROM_DEV:
            case SCSI_XFER_TO_DEV:
                scsi_req_continue(req);
                break;
            case SCSI_XFER_NONE:
                assert(!req->sg);
                scsi_req_dequeue(req);
                scsi_req_enqueue(req);
                break;
            }
        }
        scsi_req_unref(req);
    }
}

void scsi_req_retry(SCSIRequest *req)
{
    /* No need to save a reference, because scsi_dma_restart_bh just
     * looks at the request list.  */
    req->retry = true;
}

static void scsi_dma_restart_cb(void *opaque, int running, RunState state)
{
    SCSIDevice *s = opaque;

    if (!running) {
        return;
    }
    if (!s->bh) {
        s->bh = qemu_bh_new(scsi_dma_restart_bh, s);
        qemu_bh_schedule(s->bh);
    }
}

static int scsi_qdev_init(DeviceState *qdev)
{
    SCSIDevice *dev = SCSI_DEVICE(qdev);
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, dev->qdev.parent_bus);
    SCSIDevice *d;
    int rc = -1;

    if (dev->channel > bus->info->max_channel) {
        error_report("bad scsi channel id: %d", dev->channel);
        goto err;
    }
    if (dev->id != -1 && dev->id > bus->info->max_target) {
        error_report("bad scsi device id: %d", dev->id);
        goto err;
    }
    if (dev->lun != -1 && dev->lun > bus->info->max_lun) {
        error_report("bad scsi device lun: %d", dev->lun);
        goto err;
    }

    if (dev->id == -1) {
        int id = -1;
        if (dev->lun == -1) {
            dev->lun = 0;
        }
        do {
            d = scsi_device_find(bus, dev->channel, ++id, dev->lun);
        } while (d && d->lun == dev->lun && id < bus->info->max_target);
        if (d && d->lun == dev->lun) {
            error_report("no free target");
            goto err;
        }
        dev->id = id;
    } else if (dev->lun == -1) {
        int lun = -1;
        do {
            d = scsi_device_find(bus, dev->channel, dev->id, ++lun);
        } while (d && d->lun == lun && lun < bus->info->max_lun);
        if (d && d->lun == lun) {
            error_report("no free lun");
            goto err;
        }
        dev->lun = lun;
    } else {
        d = scsi_device_find(bus, dev->channel, dev->id, dev->lun);
        assert(d);
        if (d->lun == dev->lun && dev != d) {
            qdev_free(&d->qdev);
        }
    }

    QTAILQ_INIT(&dev->requests);
    rc = scsi_device_init(dev);
    if (rc == 0) {
        dev->vmsentry = qemu_add_vm_change_state_handler(scsi_dma_restart_cb,
                                                         dev);
    }

err:
    return rc;
}

static int scsi_qdev_exit(DeviceState *qdev)
{
    SCSIDevice *dev = SCSI_DEVICE(qdev);

    if (dev->vmsentry) {
        qemu_del_vm_change_state_handler(dev->vmsentry);
    }
    scsi_device_destroy(dev);
    return 0;
}

/* handle legacy '-drive if=scsi,...' cmd line args */
SCSIDevice *scsi_bus_legacy_add_drive(SCSIBus *bus, BlockDriverState *bdrv,
                                      int unit, bool removable, int bootindex)
{
    const char *driver;
    DeviceState *dev;

    driver = bdrv_is_sg(bdrv) ? "scsi-generic" : "scsi-disk";
    dev = qdev_create(&bus->qbus, driver);
    qdev_prop_set_uint32(dev, "scsi-id", unit);
    if (bootindex >= 0) {
        qdev_prop_set_int32(dev, "bootindex", bootindex);
    }
    if (qdev_prop_exists(dev, "removable")) {
        qdev_prop_set_bit(dev, "removable", removable);
    }
    if (qdev_prop_set_drive(dev, "drive", bdrv) < 0) {
        qdev_free(dev);
        return NULL;
    }
    if (qdev_init(dev) < 0)
        return NULL;
    return SCSI_DEVICE(dev);
}

int scsi_bus_legacy_handle_cmdline(SCSIBus *bus)
{
    Location loc;
    DriveInfo *dinfo;
    int res = 0, unit;

    loc_push_none(&loc);
    for (unit = 0; unit <= bus->info->max_target; unit++) {
        dinfo = drive_get(IF_SCSI, bus->busnr, unit);
        if (dinfo == NULL) {
            continue;
        }
        qemu_opts_loc_restore(dinfo->opts);
        if (!scsi_bus_legacy_add_drive(bus, dinfo->bdrv, unit, false, -1)) {
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

static const struct SCSIReqOps reqops_invalid_opcode = {
    .size         = sizeof(SCSIRequest),
    .send_command = scsi_invalid_command
};

/* SCSIReqOps implementation for unit attention conditions.  */

static int32_t scsi_unit_attention(SCSIRequest *req, uint8_t *buf)
{
    if (req->dev && req->dev->unit_attention.key == UNIT_ATTENTION) {
        scsi_req_build_sense(req, req->dev->unit_attention);
    } else if (req->bus->unit_attention.key == UNIT_ATTENTION) {
        scsi_req_build_sense(req, req->bus->unit_attention);
    }
    scsi_req_complete(req, CHECK_CONDITION);
    return 0;
}

static const struct SCSIReqOps reqops_unit_attention = {
    .size         = sizeof(SCSIRequest),
    .send_command = scsi_unit_attention
};

/* SCSIReqOps implementation for REPORT LUNS and for commands sent to
   an invalid LUN.  */

typedef struct SCSITargetReq SCSITargetReq;

struct SCSITargetReq {
    SCSIRequest req;
    int len;
    uint8_t buf[2056];
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
    DeviceState *qdev;
    int i, len, n;
    int channel, id;
    bool found_lun0;

    if (r->req.cmd.xfer < 16) {
        return false;
    }
    if (r->req.cmd.buf[2] > 2) {
        return false;
    }
    channel = r->req.dev->channel;
    id = r->req.dev->id;
    found_lun0 = false;
    n = 0;
    QTAILQ_FOREACH(qdev, &r->req.bus->qbus.children, sibling) {
        SCSIDevice *dev = SCSI_DEVICE(qdev);

        if (dev->channel == channel && dev->id == id) {
            if (dev->lun == 0) {
                found_lun0 = true;
            }
            n += 8;
        }
    }
    if (!found_lun0) {
        n += 8;
    }
    len = MIN(n + 8, r->req.cmd.xfer & ~7);
    if (len > sizeof(r->buf)) {
        /* TODO: > 256 LUNs? */
        return false;
    }

    memset(r->buf, 0, len);
    stl_be_p(&r->buf, n);
    i = found_lun0 ? 8 : 16;
    QTAILQ_FOREACH(qdev, &r->req.bus->qbus.children, sibling) {
        SCSIDevice *dev = SCSI_DEVICE(qdev);

        if (dev->channel == channel && dev->id == id) {
            store_lun(&r->buf[i], dev->lun);
            i += 8;
        }
    }
    assert(i == n + 8);
    r->len = len;
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
        return false;
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
        r->buf[7] = 0x10 | (r->req.bus->info->tcq ? 0x02 : 0); /* Sync, TCQ.  */
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
        r->len = scsi_device_get_sense(r->req.dev, r->buf,
                                       MIN(req->cmd.xfer, sizeof r->buf),
                                       (req->cmd.buf[1] & 1) == 0);
        if (r->req.dev->sense_is_ua) {
            scsi_device_unit_attention_reported(req->dev);
            r->req.dev->sense_len = 0;
            r->req.dev->sense_is_ua = false;
        }
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

static const struct SCSIReqOps reqops_target_command = {
    .size         = sizeof(SCSITargetReq),
    .send_command = scsi_target_send_command,
    .read_data    = scsi_target_read_data,
    .get_buf      = scsi_target_get_buf,
};


SCSIRequest *scsi_req_alloc(const SCSIReqOps *reqops, SCSIDevice *d,
                            uint32_t tag, uint32_t lun, void *hba_private)
{
    SCSIRequest *req;

    req = g_malloc0(reqops->size);
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
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, d->qdev.parent_bus);
    SCSIRequest *req;
    SCSICommand cmd;

    if (scsi_req_parse(&cmd, d, buf) != 0) {
        trace_scsi_req_parse_bad(d->id, lun, tag, buf[0]);
        req = scsi_req_alloc(&reqops_invalid_opcode, d, tag, lun, hba_private);
    } else {
        trace_scsi_req_parsed(d->id, lun, tag, buf[0],
                              cmd.mode, cmd.xfer);
        if (cmd.lba != -1) {
            trace_scsi_req_parsed_lba(d->id, lun, tag, buf[0],
                                      cmd.lba);
        }

        if ((d->unit_attention.key == UNIT_ATTENTION ||
             bus->unit_attention.key == UNIT_ATTENTION) &&
            (buf[0] != INQUIRY &&
             buf[0] != REPORT_LUNS &&
             buf[0] != GET_CONFIGURATION &&
             buf[0] != GET_EVENT_STATUS_NOTIFICATION &&

             /*
              * If we already have a pending unit attention condition,
              * report this one before triggering another one.
              */
             !(buf[0] == REQUEST_SENSE && d->sense_is_ua))) {
            req = scsi_req_alloc(&reqops_unit_attention, d, tag, lun,
                                 hba_private);
        } else if (lun != d->lun ||
            buf[0] == REPORT_LUNS ||
            (buf[0] == REQUEST_SENSE && (d->sense_len || cmd.xfer < 4))) {
            req = scsi_req_alloc(&reqops_target_command, d, tag, lun,
                                 hba_private);
        } else {
            req = scsi_device_alloc_req(d, tag, lun, buf, hba_private);
        }
    }

    req->cmd = cmd;
    req->resid = req->cmd.xfer;

    switch (buf[0]) {
    case INQUIRY:
        trace_scsi_inquiry(d->id, lun, tag, cmd.buf[1], cmd.buf[2]);
        break;
    case TEST_UNIT_READY:
        trace_scsi_test_unit_ready(d->id, lun, tag);
        break;
    case REPORT_LUNS:
        trace_scsi_report_luns(d->id, lun, tag);
        break;
    case REQUEST_SENSE:
        trace_scsi_request_sense(d->id, lun, tag);
        break;
    default:
        break;
    }

    return req;
}

uint8_t *scsi_req_get_buf(SCSIRequest *req)
{
    return req->ops->get_buf(req);
}

static void scsi_clear_unit_attention(SCSIRequest *req)
{
    SCSISense *ua;
    if (req->dev->unit_attention.key != UNIT_ATTENTION &&
        req->bus->unit_attention.key != UNIT_ATTENTION) {
        return;
    }

    /*
     * If an INQUIRY command enters the enabled command state,
     * the device server shall [not] clear any unit attention condition;
     * See also MMC-6, paragraphs 6.5 and 6.6.2.
     */
    if (req->cmd.buf[0] == INQUIRY ||
        req->cmd.buf[0] == GET_CONFIGURATION ||
        req->cmd.buf[0] == GET_EVENT_STATUS_NOTIFICATION) {
        return;
    }

    if (req->dev->unit_attention.key == UNIT_ATTENTION) {
        ua = &req->dev->unit_attention;
    } else {
        ua = &req->bus->unit_attention;
    }

    /*
     * If a REPORT LUNS command enters the enabled command state, [...]
     * the device server shall clear any pending unit attention condition
     * with an additional sense code of REPORTED LUNS DATA HAS CHANGED.
     */
    if (req->cmd.buf[0] == REPORT_LUNS &&
        !(ua->asc == SENSE_CODE(REPORTED_LUNS_CHANGED).asc &&
          ua->ascq == SENSE_CODE(REPORTED_LUNS_CHANGED).ascq)) {
        return;
    }

    *ua = SENSE_CODE(NO_SENSE);
}

int scsi_req_get_sense(SCSIRequest *req, uint8_t *buf, int len)
{
    int ret;

    assert(len >= 14);
    if (!req->sense_len) {
        return 0;
    }

    ret = scsi_build_sense(req->sense, req->sense_len, buf, len, true);

    /*
     * FIXME: clearing unit attention conditions upon autosense should be done
     * only if the UA_INTLCK_CTRL field in the Control mode page is set to 00b
     * (SAM-5, 5.14).
     *
     * We assume UA_INTLCK_CTRL to be 00b for HBAs that support autosense, and
     * 10b for HBAs that do not support it (do not call scsi_req_get_sense).
     * Here we handle unit attention clearing for UA_INTLCK_CTRL == 00b.
     */
    if (req->dev->sense_is_ua) {
        scsi_device_unit_attention_reported(req->dev);
        req->dev->sense_len = 0;
        req->dev->sense_is_ua = false;
    }
    return ret;
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
    req->sense[7] = 10;
    req->sense[12] = sense.asc;
    req->sense[13] = sense.ascq;
    req->sense_len = 18;
}

static void scsi_req_enqueue_internal(SCSIRequest *req)
{
    assert(!req->enqueued);
    scsi_req_ref(req);
    if (req->bus->info->get_sg_list) {
        req->sg = req->bus->info->get_sg_list(req);
    } else {
        req->sg = NULL;
    }
    req->enqueued = true;
    QTAILQ_INSERT_TAIL(&req->dev->requests, req, next);
}

int32_t scsi_req_enqueue(SCSIRequest *req)
{
    int32_t rc;

    assert(!req->retry);
    scsi_req_enqueue_internal(req);
    scsi_req_ref(req);
    rc = req->ops->send_command(req, req->cmd.buf);
    scsi_req_unref(req);
    return rc;
}

static void scsi_req_dequeue(SCSIRequest *req)
{
    trace_scsi_req_dequeue(req->dev->id, req->lun, req->tag);
    req->retry = false;
    if (req->enqueued) {
        QTAILQ_REMOVE(&req->dev->requests, req, next);
        req->enqueued = false;
        scsi_req_unref(req);
    }
}

static int scsi_get_performance_length(int num_desc, int type, int data_type)
{
    /* MMC-6, paragraph 6.7.  */
    switch (type) {
    case 0:
        if ((data_type & 3) == 0) {
            /* Each descriptor is as in Table 295 - Nominal performance.  */
            return 16 * num_desc + 8;
        } else {
            /* Each descriptor is as in Table 296 - Exceptions.  */
            return 6 * num_desc + 8;
        }
    case 1:
    case 4:
    case 5:
        return 8 * num_desc + 8;
    case 2:
        return 2048 * num_desc + 8;
    case 3:
        return 16 * num_desc + 8;
    default:
        return 8;
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
        cmd->xfer = lduw_be_p(&buf[7]);
        cmd->len = 10;
        break;
    case 4:
        cmd->xfer = ldl_be_p(&buf[10]) & 0xffffffffULL;
        cmd->len = 16;
        break;
    case 5:
        cmd->xfer = ldl_be_p(&buf[6]) & 0xffffffffULL;
        cmd->len = 12;
        break;
    default:
        return -1;
    }

    switch (buf[0]) {
    case TEST_UNIT_READY:
    case REWIND:
    case START_STOP:
    case SET_CAPACITY:
    case WRITE_FILEMARKS:
    case WRITE_FILEMARKS_16:
    case SPACE:
    case RESERVE:
    case RELEASE:
    case ERASE:
    case ALLOW_MEDIUM_REMOVAL:
    case VERIFY_10:
    case SEEK_10:
    case SYNCHRONIZE_CACHE:
    case SYNCHRONIZE_CACHE_16:
    case LOCATE_16:
    case LOCK_UNLOCK_CACHE:
    case LOAD_UNLOAD:
    case SET_CD_SPEED:
    case SET_LIMITS:
    case WRITE_LONG_10:
    case MOVE_MEDIUM:
    case UPDATE_BLOCK:
    case RESERVE_TRACK:
    case SET_READ_AHEAD:
    case PRE_FETCH:
    case PRE_FETCH_16:
    case ALLOW_OVERWRITE:
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
    case SEND_VOLUME_TAG:
        /* GPCMD_SET_STREAMING from multimedia commands.  */
        if (dev->type == TYPE_ROM) {
            cmd->xfer = buf[10] | (buf[9] << 8);
        } else {
            cmd->xfer = buf[9] | (buf[8] << 8);
        }
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
    case FORMAT_UNIT:
        /* MMC mandates the parameter list to be 12-bytes long.  Parameters
         * for block devices are restricted to the header right now.  */
        if (dev->type == TYPE_ROM && (buf[1] & 16)) {
            cmd->xfer = 12;
        } else {
            cmd->xfer = (buf[1] & 16) == 0 ? 0 : (buf[1] & 32 ? 8 : 4);
        }
        break;
    case INQUIRY:
    case RECEIVE_DIAGNOSTIC:
    case SEND_DIAGNOSTIC:
        cmd->xfer = buf[4] | (buf[3] << 8);
        break;
    case READ_CD:
    case READ_BUFFER:
    case WRITE_BUFFER:
    case SEND_CUE_SHEET:
        cmd->xfer = buf[8] | (buf[7] << 8) | (buf[6] << 16);
        break;
    case PERSISTENT_RESERVE_OUT:
        cmd->xfer = ldl_be_p(&buf[5]) & 0xffffffffULL;
        break;
    case ERASE_12:
        if (dev->type == TYPE_ROM) {
            /* MMC command GET PERFORMANCE.  */
            cmd->xfer = scsi_get_performance_length(buf[9] | (buf[8] << 8),
                                                    buf[10], buf[1] & 0x1f);
        }
        break;
    case MECHANISM_STATUS:
    case READ_DVD_STRUCTURE:
    case SEND_DVD_STRUCTURE:
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
    case ERASE_12:
    case ERASE_16:
        cmd->xfer = 0;
        break;
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
    case SPACE_16:
        cmd->xfer = buf[13] | (buf[12] << 8);
        break;
    case READ_POSITION:
        cmd->xfer = buf[8] | (buf[7] << 8);
        break;
    case FORMAT_UNIT:
        cmd->xfer = buf[4] | (buf[3] << 8);
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
    case SEND_CUE_SHEET:
    case SEND_DVD_STRUCTURE:
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
        lba = ldl_be_p(&buf[0]) & 0x1fffff;
        break;
    case 1:
    case 2:
    case 5:
        lba = ldl_be_p(&buf[2]) & 0xffffffffULL;
        break;
    case 4:
        lba = ldq_be_p(&buf[2]);
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

/* LUN not ready, medium removal prevented */
const struct SCSISense sense_code_NOT_READY_REMOVAL_PREVENTED = {
    .key = NOT_READY, .asc = 0x53, .ascq = 0x00
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
const struct SCSISense sense_code_INCOMPATIBLE_FORMAT = {
    .key = ILLEGAL_REQUEST, .asc = 0x30, .ascq = 0x00
};

/* Illegal request, medium removal prevented */
const struct SCSISense sense_code_ILLEGAL_REQ_REMOVAL_PREVENTED = {
    .key = ILLEGAL_REQUEST, .asc = 0x53, .ascq = 0x00
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

/* Unit attention, No medium */
const struct SCSISense sense_code_UNIT_ATTENTION_NO_MEDIUM = {
    .key = UNIT_ATTENTION, .asc = 0x3a, .ascq = 0x00
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
        buf[7] = 10;
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
        [ SET_CAPACITY             ] = "SET_CAPACITY",
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
        [ PRE_FETCH                ] = "PRE_FETCH/READ_POSITION",
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
        [ PRE_FETCH_16             ] = "PRE_FETCH_16",
        [ SYNCHRONIZE_CACHE_16     ] = "SPACE_16/SYNCHRONIZE_CACHE_16",
        /* SPACE_16 and SYNCHRONIZE_CACHE_16 use the same operation code */
        [ LOCATE_16                ] = "LOCATE_16",
        [ WRITE_SAME_16            ] = "ERASE_16/WRITE_SAME_16",
        /* ERASE_16 and WRITE_SAME_16 use the same operation code */
        [ SERVICE_ACTION_IN_16     ] = "SERVICE_ACTION_IN_16",
        [ WRITE_LONG_16            ] = "WRITE_LONG_16",
        [ REPORT_LUNS              ] = "REPORT_LUNS",
        [ BLANK                    ] = "BLANK",
        [ MOVE_MEDIUM              ] = "MOVE_MEDIUM",
        [ LOAD_UNLOAD              ] = "LOAD_UNLOAD",
        [ READ_12                  ] = "READ_12",
        [ WRITE_12                 ] = "WRITE_12",
        [ ERASE_12                 ] = "ERASE_12/GET_PERFORMANCE",
        /* ERASE_12 and GET_PERFORMANCE use the same operation code */
        [ SERVICE_ACTION_IN_12     ] = "SERVICE_ACTION_IN_12",
        [ WRITE_VERIFY_12          ] = "WRITE_VERIFY_12",
        [ VERIFY_12                ] = "VERIFY_12",
        [ SEARCH_HIGH_12           ] = "SEARCH_HIGH_12",
        [ SEARCH_EQUAL_12          ] = "SEARCH_EQUAL_12",
        [ SEARCH_LOW_12            ] = "SEARCH_LOW_12",
        [ READ_ELEMENT_STATUS      ] = "READ_ELEMENT_STATUS",
        [ SEND_VOLUME_TAG          ] = "SEND_VOLUME_TAG/SET_STREAMING",
        /* SEND_VOLUME_TAG and SET_STREAMING use the same operation code */
        [ READ_CD                  ] = "READ_CD",
        [ READ_DEFECT_DATA_12      ] = "READ_DEFECT_DATA_12",
        [ READ_DVD_STRUCTURE       ] = "READ_DVD_STRUCTURE",
        [ RESERVE_TRACK            ] = "RESERVE_TRACK",
        [ SEND_CUE_SHEET           ] = "SEND_CUE_SHEET",
        [ SEND_DVD_STRUCTURE       ] = "SEND_DVD_STRUCTURE",
        [ SET_CD_SPEED             ] = "SET_CD_SPEED",
        [ SET_READ_AHEAD           ] = "SET_READ_AHEAD",
        [ ALLOW_OVERWRITE          ] = "ALLOW_OVERWRITE",
        [ MECHANISM_STATUS         ] = "MECHANISM_STATUS",
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
        g_free(req);
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
    uint8_t *buf;
    if (req->io_canceled) {
        trace_scsi_req_data_canceled(req->dev->id, req->lun, req->tag, len);
        return;
    }
    trace_scsi_req_data(req->dev->id, req->lun, req->tag, len);
    assert(req->cmd.mode != SCSI_XFER_NONE);
    if (!req->sg) {
        req->resid -= len;
        req->bus->info->transfer_data(req, len);
        return;
    }

    /* If the device calls scsi_req_data and the HBA specified a
     * scatter/gather list, the transfer has to happen in a single
     * step.  */
    assert(!req->dma_started);
    req->dma_started = true;

    buf = scsi_req_get_buf(req);
    if (req->cmd.mode == SCSI_XFER_FROM_DEV) {
        req->resid = dma_buf_read(buf, len, req->sg);
    } else {
        req->resid = dma_buf_write(buf, len, req->sg);
    }
    scsi_req_continue(req);
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
        req->dev->sense_len = req->sense_len;
        req->dev->sense_is_ua = (req->ops == &reqops_unit_attention);
    } else {
        req->dev->sense_len = 0;
        req->dev->sense_is_ua = false;
    }

    /*
     * Unit attention state is now stored in the device's sense buffer
     * if the HBA didn't do autosense.  Clear the pending unit attention
     * flags.
     */
    scsi_clear_unit_attention(req);

    scsi_req_ref(req);
    scsi_req_dequeue(req);
    req->bus->info->complete(req, req->status, req->resid);
    scsi_req_unref(req);
}

void scsi_req_cancel(SCSIRequest *req)
{
    if (!req->enqueued) {
        return;
    }
    scsi_req_ref(req);
    scsi_req_dequeue(req);
    req->io_canceled = true;
    if (req->ops->cancel_io) {
        req->ops->cancel_io(req);
    }
    if (req->bus->info->cancel) {
        req->bus->info->cancel(req);
    }
    scsi_req_unref(req);
}

void scsi_req_abort(SCSIRequest *req, int status)
{
    if (!req->enqueued) {
        return;
    }
    scsi_req_ref(req);
    scsi_req_dequeue(req);
    req->io_canceled = true;
    if (req->ops->cancel_io) {
        req->ops->cancel_io(req);
    }
    scsi_req_complete(req, status);
    scsi_req_unref(req);
}

void scsi_device_purge_requests(SCSIDevice *sdev, SCSISense sense)
{
    SCSIRequest *req;

    while (!QTAILQ_EMPTY(&sdev->requests)) {
        req = QTAILQ_FIRST(&sdev->requests);
        scsi_req_cancel(req);
    }
    sdev->unit_attention = sense;
}

static char *scsibus_get_dev_path(DeviceState *dev)
{
    SCSIDevice *d = DO_UPCAST(SCSIDevice, qdev, dev);
    DeviceState *hba = dev->parent_bus->parent;
    char *id = NULL;

    if (hba && hba->parent_bus && hba->parent_bus->info->get_dev_path) {
        id = hba->parent_bus->info->get_dev_path(hba);
    }
    if (id) {
        return g_strdup_printf("%s/%d:%d:%d", id, d->channel, d->id, d->lun);
    } else {
        return g_strdup_printf("%d:%d:%d", d->channel, d->id, d->lun);
    }
}

static char *scsibus_get_fw_dev_path(DeviceState *dev)
{
    SCSIDevice *d = SCSI_DEVICE(dev);
    char path[100];

    snprintf(path, sizeof(path), "channel@%x/%s@%x,%x", d->channel,
             qdev_fw_name(dev), d->id, d->lun);

    return strdup(path);
}

SCSIDevice *scsi_device_find(SCSIBus *bus, int channel, int id, int lun)
{
    DeviceState *qdev;
    SCSIDevice *target_dev = NULL;

    QTAILQ_FOREACH_REVERSE(qdev, &bus->qbus.children, ChildrenHead, sibling) {
        SCSIDevice *dev = SCSI_DEVICE(qdev);

        if (dev->channel == channel && dev->id == id) {
            if (dev->lun == lun) {
                return dev;
            }
            target_dev = dev;
        }
    }
    return target_dev;
}

/* SCSI request list.  For simplicity, pv points to the whole device */

static void put_scsi_requests(QEMUFile *f, void *pv, size_t size)
{
    SCSIDevice *s = pv;
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, s->qdev.parent_bus);
    SCSIRequest *req;

    QTAILQ_FOREACH(req, &s->requests, next) {
        assert(!req->io_canceled);
        assert(req->status == -1);
        assert(req->retry);
        assert(req->enqueued);

        qemu_put_sbyte(f, 1);
        qemu_put_buffer(f, req->cmd.buf, sizeof(req->cmd.buf));
        qemu_put_be32s(f, &req->tag);
        qemu_put_be32s(f, &req->lun);
        if (bus->info->save_request) {
            bus->info->save_request(f, req);
        }
        if (req->ops->save_request) {
            req->ops->save_request(f, req);
        }
    }
    qemu_put_sbyte(f, 0);
}

static int get_scsi_requests(QEMUFile *f, void *pv, size_t size)
{
    SCSIDevice *s = pv;
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, s->qdev.parent_bus);

    while (qemu_get_sbyte(f)) {
        uint8_t buf[SCSI_CMD_BUF_SIZE];
        uint32_t tag;
        uint32_t lun;
        SCSIRequest *req;

        qemu_get_buffer(f, buf, sizeof(buf));
        qemu_get_be32s(f, &tag);
        qemu_get_be32s(f, &lun);
        req = scsi_req_new(s, tag, lun, buf, NULL);
        if (bus->info->load_request) {
            req->hba_private = bus->info->load_request(f, req);
        }
        if (req->ops->load_request) {
            req->ops->load_request(f, req);
        }

        /* Just restart it later.  */
        req->retry = true;
        scsi_req_enqueue_internal(req);

        /* At this point, the request will be kept alive by the reference
         * added by scsi_req_enqueue_internal, so we can release our reference.
         * The HBA of course will add its own reference in the load_request
         * callback if it needs to hold on the SCSIRequest.
         */
        scsi_req_unref(req);
    }

    return 0;
}

const VMStateInfo vmstate_info_scsi_requests = {
    .name = "scsi-requests",
    .get  = get_scsi_requests,
    .put  = put_scsi_requests,
};

const VMStateDescription vmstate_scsi_device = {
    .name = "SCSIDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(unit_attention.key, SCSIDevice),
        VMSTATE_UINT8(unit_attention.asc, SCSIDevice),
        VMSTATE_UINT8(unit_attention.ascq, SCSIDevice),
        VMSTATE_BOOL(sense_is_ua, SCSIDevice),
        VMSTATE_UINT8_ARRAY(sense, SCSIDevice, SCSI_SENSE_BUF_SIZE),
        VMSTATE_UINT32(sense_len, SCSIDevice),
        {
            .name         = "requests",
            .version_id   = 0,
            .field_exists = NULL,
            .size         = 0,   /* ouch */
            .info         = &vmstate_info_scsi_requests,
            .flags        = VMS_SINGLE,
            .offset       = 0,
        },
        VMSTATE_END_OF_LIST()
    }
};

static void scsi_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->bus_info = &scsi_bus_info;
    k->init     = scsi_qdev_init;
    k->unplug   = qdev_simple_unplug_cb;
    k->exit     = scsi_qdev_exit;
}

static TypeInfo scsi_device_type_info = {
    .name = TYPE_SCSI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SCSIDevice),
    .abstract = true,
    .class_size = sizeof(SCSIDeviceClass),
    .class_init = scsi_device_class_init,
};

static void scsi_register_types(void)
{
    type_register_static(&scsi_device_type_info);
}

type_init(scsi_register_types)
