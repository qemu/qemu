#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/hw-version.h"
#include "hw/qdev-properties.h"
#include "hw/scsi/scsi.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "scsi/constants.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "trace.h"
#include "sysemu/dma.h"
#include "qemu/cutils.h"

static char *scsibus_get_dev_path(DeviceState *dev);
static char *scsibus_get_fw_dev_path(DeviceState *dev);
static void scsi_req_dequeue(SCSIRequest *req);
static uint8_t *scsi_target_alloc_buf(SCSIRequest *req, size_t len);
static void scsi_target_free_buf(SCSIRequest *req);

static int next_scsi_bus;

static SCSIDevice *do_scsi_device_find(SCSIBus *bus,
                                       int channel, int id, int lun,
                                       bool include_unrealized)
{
    BusChild *kid;
    SCSIDevice *retval = NULL;

    QTAILQ_FOREACH_RCU(kid, &bus->qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        SCSIDevice *dev = SCSI_DEVICE(qdev);

        if (dev->channel == channel && dev->id == id) {
            if (dev->lun == lun) {
                retval = dev;
                break;
            }

            /*
             * If we don't find exact match (channel/bus/lun),
             * we will return the first device which matches channel/bus
             */

            if (!retval) {
                retval = dev;
            }
        }
    }

    /*
     * This function might run on the IO thread and we might race against
     * main thread hot-plugging the device.
     * We assume that as soon as .realized is set to true we can let
     * the user access the device.
     */

    if (retval && !include_unrealized &&
        !qatomic_load_acquire(&retval->qdev.realized)) {
        retval = NULL;
    }

    return retval;
}

SCSIDevice *scsi_device_find(SCSIBus *bus, int channel, int id, int lun)
{
    RCU_READ_LOCK_GUARD();
    return do_scsi_device_find(bus, channel, id, lun, false);
}

SCSIDevice *scsi_device_get(SCSIBus *bus, int channel, int id, int lun)
{
    SCSIDevice *d;
    RCU_READ_LOCK_GUARD();
    d = do_scsi_device_find(bus, channel, id, lun, false);
    if (d) {
        object_ref(d);
    }
    return d;
}

static void scsi_device_realize(SCSIDevice *s, Error **errp)
{
    SCSIDeviceClass *sc = SCSI_DEVICE_GET_CLASS(s);
    if (sc->realize) {
        sc->realize(s, errp);
    }
}

static void scsi_device_unrealize(SCSIDevice *s)
{
    SCSIDeviceClass *sc = SCSI_DEVICE_GET_CLASS(s);
    if (sc->unrealize) {
        sc->unrealize(s);
    }
}

int scsi_bus_parse_cdb(SCSIDevice *dev, SCSICommand *cmd, uint8_t *buf,
                       size_t buf_len, void *hba_private)
{
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, dev->qdev.parent_bus);
    int rc;

    assert(cmd->len == 0);
    rc = scsi_req_parse_cdb(dev, cmd, buf, buf_len);
    if (bus->info->parse_cdb) {
        rc = bus->info->parse_cdb(dev, cmd, buf, buf_len, hba_private);
    }
    return rc;
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

void scsi_device_unit_attention_reported(SCSIDevice *s)
{
    SCSIDeviceClass *sc = SCSI_DEVICE_GET_CLASS(s);
    if (sc->unit_attention_reported) {
        sc->unit_attention_reported(s);
    }
}

/* Create a scsi bus, and attach devices to it.  */
void scsi_bus_init_named(SCSIBus *bus, size_t bus_size, DeviceState *host,
                         const SCSIBusInfo *info, const char *bus_name)
{
    qbus_init(bus, bus_size, TYPE_SCSI_BUS, host, bus_name);
    bus->busnr = next_scsi_bus++;
    bus->info = info;
    qbus_set_bus_hotplug_handler(BUS(bus));
}

static void scsi_dma_restart_bh(void *opaque)
{
    SCSIDevice *s = opaque;
    SCSIRequest *req, *next;

    qemu_bh_delete(s->bh);
    s->bh = NULL;

    aio_context_acquire(blk_get_aio_context(s->conf.blk));
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
                scsi_req_dequeue(req);
                scsi_req_enqueue(req);
                break;
            }
        }
        scsi_req_unref(req);
    }
    aio_context_release(blk_get_aio_context(s->conf.blk));
    /* Drop the reference that was acquired in scsi_dma_restart_cb */
    object_unref(OBJECT(s));
}

void scsi_req_retry(SCSIRequest *req)
{
    /* No need to save a reference, because scsi_dma_restart_bh just
     * looks at the request list.  */
    req->retry = true;
}

static void scsi_dma_restart_cb(void *opaque, bool running, RunState state)
{
    SCSIDevice *s = opaque;

    if (!running) {
        return;
    }
    if (!s->bh) {
        AioContext *ctx = blk_get_aio_context(s->conf.blk);
        /* The reference is dropped in scsi_dma_restart_bh.*/
        object_ref(OBJECT(s));
        s->bh = aio_bh_new(ctx, scsi_dma_restart_bh, s);
        qemu_bh_schedule(s->bh);
    }
}

static bool scsi_bus_is_address_free(SCSIBus *bus,
				     int channel, int target, int lun,
				     SCSIDevice **p_dev)
{
    SCSIDevice *d;

    RCU_READ_LOCK_GUARD();
    d = do_scsi_device_find(bus, channel, target, lun, true);
    if (d && d->lun == lun) {
        if (p_dev) {
            *p_dev = d;
        }
        return false;
    }
    if (p_dev) {
        *p_dev = NULL;
    }
    return true;
}

static bool scsi_bus_check_address(BusState *qbus, DeviceState *qdev, Error **errp)
{
    SCSIDevice *dev = SCSI_DEVICE(qdev);
    SCSIBus *bus = SCSI_BUS(qbus);

    if (dev->channel > bus->info->max_channel) {
        error_setg(errp, "bad scsi channel id: %d", dev->channel);
        return false;
    }
    if (dev->id != -1 && dev->id > bus->info->max_target) {
        error_setg(errp, "bad scsi device id: %d", dev->id);
        return false;
    }
    if (dev->lun != -1 && dev->lun > bus->info->max_lun) {
        error_setg(errp, "bad scsi device lun: %d", dev->lun);
        return false;
    }

    if (dev->id != -1 && dev->lun != -1) {
        SCSIDevice *d;
        if (!scsi_bus_is_address_free(bus, dev->channel, dev->id, dev->lun, &d)) {
            error_setg(errp, "lun already used by '%s'", d->qdev.id);
            return false;
        }
    }

    return true;
}

static void scsi_qdev_realize(DeviceState *qdev, Error **errp)
{
    SCSIDevice *dev = SCSI_DEVICE(qdev);
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, dev->qdev.parent_bus);
    bool is_free;
    Error *local_err = NULL;

    if (dev->id == -1) {
        int id = -1;
        if (dev->lun == -1) {
            dev->lun = 0;
        }
        do {
            is_free = scsi_bus_is_address_free(bus, dev->channel, ++id, dev->lun, NULL);
        } while (!is_free && id < bus->info->max_target);
        if (!is_free) {
            error_setg(errp, "no free target");
            return;
        }
        dev->id = id;
    } else if (dev->lun == -1) {
        int lun = -1;
        do {
            is_free = scsi_bus_is_address_free(bus, dev->channel, dev->id, ++lun, NULL);
        } while (!is_free && lun < bus->info->max_lun);
        if (!is_free) {
            error_setg(errp, "no free lun");
            return;
        }
        dev->lun = lun;
    }

    QTAILQ_INIT(&dev->requests);
    scsi_device_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    dev->vmsentry = qdev_add_vm_change_state_handler(DEVICE(dev),
            scsi_dma_restart_cb, dev);
}

static void scsi_qdev_unrealize(DeviceState *qdev)
{
    SCSIDevice *dev = SCSI_DEVICE(qdev);

    if (dev->vmsentry) {
        qemu_del_vm_change_state_handler(dev->vmsentry);
    }

    scsi_device_purge_requests(dev, SENSE_CODE(NO_SENSE));

    scsi_device_unrealize(dev);

    blockdev_mark_auto_del(dev->conf.blk);
}

/* handle legacy '-drive if=scsi,...' cmd line args */
SCSIDevice *scsi_bus_legacy_add_drive(SCSIBus *bus, BlockBackend *blk,
                                      int unit, bool removable, int bootindex,
                                      bool share_rw,
                                      BlockdevOnError rerror,
                                      BlockdevOnError werror,
                                      const char *serial, Error **errp)
{
    const char *driver;
    char *name;
    DeviceState *dev;
    DriveInfo *dinfo;

    if (blk_is_sg(blk)) {
        driver = "scsi-generic";
    } else {
        dinfo = blk_legacy_dinfo(blk);
        if (dinfo && dinfo->media_cd) {
            driver = "scsi-cd";
        } else {
            driver = "scsi-hd";
        }
    }
    dev = qdev_new(driver);
    name = g_strdup_printf("legacy[%d]", unit);
    object_property_add_child(OBJECT(bus), name, OBJECT(dev));
    g_free(name);

    qdev_prop_set_uint32(dev, "scsi-id", unit);
    if (bootindex >= 0) {
        object_property_set_int(OBJECT(dev), "bootindex", bootindex,
                                &error_abort);
    }
    if (object_property_find(OBJECT(dev), "removable")) {
        qdev_prop_set_bit(dev, "removable", removable);
    }
    if (serial && object_property_find(OBJECT(dev), "serial")) {
        qdev_prop_set_string(dev, "serial", serial);
    }
    if (!qdev_prop_set_drive_err(dev, "drive", blk, errp)) {
        object_unparent(OBJECT(dev));
        return NULL;
    }
    if (!object_property_set_bool(OBJECT(dev), "share-rw", share_rw, errp)) {
        object_unparent(OBJECT(dev));
        return NULL;
    }

    qdev_prop_set_enum(dev, "rerror", rerror);
    qdev_prop_set_enum(dev, "werror", werror);

    if (!qdev_realize_and_unref(dev, &bus->qbus, errp)) {
        object_unparent(OBJECT(dev));
        return NULL;
    }
    return SCSI_DEVICE(dev);
}

void scsi_bus_legacy_handle_cmdline(SCSIBus *bus)
{
    Location loc;
    DriveInfo *dinfo;
    int unit;

    loc_push_none(&loc);
    for (unit = 0; unit <= bus->info->max_target; unit++) {
        dinfo = drive_get(IF_SCSI, bus->busnr, unit);
        if (dinfo == NULL) {
            continue;
        }
        qemu_opts_loc_restore(dinfo->opts);
        scsi_bus_legacy_add_drive(bus, blk_by_legacy_dinfo(dinfo),
                                  unit, false, -1, false,
                                  BLOCKDEV_ON_ERROR_AUTO,
                                  BLOCKDEV_ON_ERROR_AUTO,
                                  NULL, &error_fatal);
    }
    loc_pop(&loc);
}

static int32_t scsi_invalid_field(SCSIRequest *req, uint8_t *buf)
{
    scsi_req_build_sense(req, SENSE_CODE(INVALID_FIELD));
    scsi_req_complete(req, CHECK_CONDITION);
    return 0;
}

static const struct SCSIReqOps reqops_invalid_field = {
    .size         = sizeof(SCSIRequest),
    .send_command = scsi_invalid_field
};

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
    if (req->dev->unit_attention.key == UNIT_ATTENTION) {
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
    uint8_t *buf;
    int buf_len;
};

static void store_lun(uint8_t *outbuf, int lun)
{
    if (lun < 256) {
        /* Simple logical unit addressing method*/
        outbuf[0] = 0;
        outbuf[1] = lun;
    } else {
        /* Flat space addressing method */
        outbuf[0] = 0x40 | (lun >> 8);
        outbuf[1] = (lun & 255);
    }
}

static bool scsi_target_emulate_report_luns(SCSITargetReq *r)
{
    BusChild *kid;
    int channel, id;
    uint8_t tmp[8] = {0};
    int len = 0;
    GByteArray *buf;

    if (r->req.cmd.xfer < 16) {
        return false;
    }
    if (r->req.cmd.buf[2] > 2) {
        return false;
    }

    /* reserve space for 63 LUNs*/
    buf = g_byte_array_sized_new(512);

    channel = r->req.dev->channel;
    id = r->req.dev->id;

    /* add size (will be updated later to correct value */
    g_byte_array_append(buf, tmp, 8);
    len += 8;

    /* add LUN0 */
    g_byte_array_append(buf, tmp, 8);
    len += 8;

    WITH_RCU_READ_LOCK_GUARD() {
        QTAILQ_FOREACH_RCU(kid, &r->req.bus->qbus.children, sibling) {
            DeviceState *qdev = kid->child;
            SCSIDevice *dev = SCSI_DEVICE(qdev);

            if (dev->channel == channel && dev->id == id && dev->lun != 0) {
                store_lun(tmp, dev->lun);
                g_byte_array_append(buf, tmp, 8);
                len += 8;
            }
        }
    }

    r->buf_len = len;
    r->buf = g_byte_array_free(buf, FALSE);
    r->len = MIN(len, r->req.cmd.xfer & ~7);

    /* store the LUN list length */
    stl_be_p(&r->buf[0], len - 8);
    return true;
}

static bool scsi_target_emulate_inquiry(SCSITargetReq *r)
{
    assert(r->req.dev->lun != r->req.lun);

    scsi_target_alloc_buf(&r->req, SCSI_INQUIRY_LEN);

    if (r->req.cmd.buf[1] & 0x2) {
        /* Command support data - optional, not implemented */
        return false;
    }

    if (r->req.cmd.buf[1] & 0x1) {
        /* Vital product data */
        uint8_t page_code = r->req.cmd.buf[2];
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
        assert(r->len < r->buf_len);
        r->len = MIN(r->req.cmd.xfer, r->len);
        return true;
    }

    /* Standard INQUIRY data */
    if (r->req.cmd.buf[2] != 0) {
        return false;
    }

    /* PAGE CODE == 0 */
    r->len = MIN(r->req.cmd.xfer, SCSI_INQUIRY_LEN);
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
        pstrcpy((char *) &r->buf[32], 4, qemu_hw_version());
    }
    return true;
}

static size_t scsi_sense_len(SCSIRequest *req)
{
    if (req->dev->type == TYPE_SCANNER)
        return SCSI_SENSE_LEN_SCANNER;
    else
        return SCSI_SENSE_LEN;
}

static int32_t scsi_target_send_command(SCSIRequest *req, uint8_t *buf)
{
    SCSITargetReq *r = DO_UPCAST(SCSITargetReq, req, req);
    int fixed_sense = (req->cmd.buf[1] & 1) == 0;

    if (req->lun != 0 &&
        buf[0] != INQUIRY && buf[0] != REQUEST_SENSE) {
        scsi_req_build_sense(req, SENSE_CODE(LUN_NOT_SUPPORTED));
        scsi_req_complete(req, CHECK_CONDITION);
        return 0;
    }
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
        scsi_target_alloc_buf(&r->req, scsi_sense_len(req));
        if (req->lun != 0) {
            const struct SCSISense sense = SENSE_CODE(LUN_NOT_SUPPORTED);

            r->len = scsi_build_sense_buf(r->buf, req->cmd.xfer,
                                          sense, fixed_sense);
        } else {
            r->len = scsi_device_get_sense(r->req.dev, r->buf,
                                           MIN(req->cmd.xfer, r->buf_len),
                                           fixed_sense);
        }
        if (r->req.dev->sense_is_ua) {
            scsi_device_unit_attention_reported(req->dev);
            r->req.dev->sense_len = 0;
            r->req.dev->sense_is_ua = false;
        }
        break;
    case TEST_UNIT_READY:
        break;
    default:
        scsi_req_build_sense(req, SENSE_CODE(INVALID_OPCODE));
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

static uint8_t *scsi_target_alloc_buf(SCSIRequest *req, size_t len)
{
    SCSITargetReq *r = DO_UPCAST(SCSITargetReq, req, req);

    r->buf = g_malloc(len);
    r->buf_len = len;

    return r->buf;
}

static void scsi_target_free_buf(SCSIRequest *req)
{
    SCSITargetReq *r = DO_UPCAST(SCSITargetReq, req, req);

    g_free(r->buf);
}

static const struct SCSIReqOps reqops_target_command = {
    .size         = sizeof(SCSITargetReq),
    .send_command = scsi_target_send_command,
    .read_data    = scsi_target_read_data,
    .get_buf      = scsi_target_get_buf,
    .free_req     = scsi_target_free_buf,
};


SCSIRequest *scsi_req_alloc(const SCSIReqOps *reqops, SCSIDevice *d,
                            uint32_t tag, uint32_t lun, void *hba_private)
{
    SCSIRequest *req;
    SCSIBus *bus = scsi_bus_from_device(d);
    BusState *qbus = BUS(bus);
    const int memset_off = offsetof(SCSIRequest, sense)
                           + sizeof(req->sense);

    req = g_malloc(reqops->size);
    memset((uint8_t *)req + memset_off, 0, reqops->size - memset_off);
    req->refcount = 1;
    req->bus = bus;
    req->dev = d;
    req->tag = tag;
    req->lun = lun;
    req->hba_private = hba_private;
    req->status = -1;
    req->host_status = -1;
    req->ops = reqops;
    object_ref(OBJECT(d));
    object_ref(OBJECT(qbus->parent));
    notifier_list_init(&req->cancel_notifiers);
    trace_scsi_req_alloc(req->dev->id, req->lun, req->tag);
    return req;
}

SCSIRequest *scsi_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun,
                          uint8_t *buf, size_t buf_len, void *hba_private)
{
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, d->qdev.parent_bus);
    const SCSIReqOps *ops;
    SCSIDeviceClass *sc = SCSI_DEVICE_GET_CLASS(d);
    SCSIRequest *req;
    SCSICommand cmd = { .len = 0 };
    int ret;

    if (buf_len == 0) {
        trace_scsi_req_parse_bad(d->id, lun, tag, 0);
        goto invalid_opcode;
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
        ops = &reqops_unit_attention;
    } else if (lun != d->lun ||
               buf[0] == REPORT_LUNS ||
               (buf[0] == REQUEST_SENSE && d->sense_len)) {
        ops = &reqops_target_command;
    } else {
        ops = NULL;
    }

    if (ops != NULL || !sc->parse_cdb) {
        ret = scsi_req_parse_cdb(d, &cmd, buf, buf_len);
    } else {
        ret = sc->parse_cdb(d, &cmd, buf, buf_len, hba_private);
    }

    if (ret != 0) {
        trace_scsi_req_parse_bad(d->id, lun, tag, buf[0]);
invalid_opcode:
        req = scsi_req_alloc(&reqops_invalid_opcode, d, tag, lun, hba_private);
    } else {
        assert(cmd.len != 0);
        trace_scsi_req_parsed(d->id, lun, tag, buf[0],
                              cmd.mode, cmd.xfer);
        if (cmd.lba != -1) {
            trace_scsi_req_parsed_lba(d->id, lun, tag, buf[0],
                                      cmd.lba);
        }

        if (cmd.xfer > INT32_MAX) {
            req = scsi_req_alloc(&reqops_invalid_field, d, tag, lun, hba_private);
        } else if (ops) {
            req = scsi_req_alloc(ops, d, tag, lun, hba_private);
        } else {
            req = scsi_device_alloc_req(d, tag, lun, buf, hba_private);
        }
    }

    req->cmd = cmd;
    req->residual = req->cmd.xfer;

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

    ret = scsi_convert_sense(req->sense, req->sense_len, buf, len, true);

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
    return scsi_convert_sense(dev->sense, dev->sense_len, buf, len, fixed);
}

void scsi_req_build_sense(SCSIRequest *req, SCSISense sense)
{
    trace_scsi_req_build_sense(req->dev->id, req->lun, req->tag,
                               sense.key, sense.asc, sense.ascq);
    req->sense_len = scsi_build_sense(req->sense, sense);
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

static int ata_passthrough_xfer_unit(SCSIDevice *dev, uint8_t *buf)
{
    int byte_block = (buf[2] >> 2) & 0x1;
    int type = (buf[2] >> 4) & 0x1;
    int xfer_unit;

    if (byte_block) {
        if (type) {
            xfer_unit = dev->blocksize;
        } else {
            xfer_unit = 512;
        }
    } else {
        xfer_unit = 1;
    }

    return xfer_unit;
}

static int ata_passthrough_12_xfer(SCSIDevice *dev, uint8_t *buf)
{
    int length = buf[2] & 0x3;
    int xfer;
    int unit = ata_passthrough_xfer_unit(dev, buf);

    switch (length) {
    case 0:
    case 3: /* USB-specific.  */
    default:
        xfer = 0;
        break;
    case 1:
        xfer = buf[3];
        break;
    case 2:
        xfer = buf[4];
        break;
    }

    return xfer * unit;
}

static int ata_passthrough_16_xfer(SCSIDevice *dev, uint8_t *buf)
{
    int extend = buf[1] & 0x1;
    int length = buf[2] & 0x3;
    int xfer;
    int unit = ata_passthrough_xfer_unit(dev, buf);

    switch (length) {
    case 0:
    case 3: /* USB-specific.  */
    default:
        xfer = 0;
        break;
    case 1:
        xfer = buf[4];
        xfer |= (extend ? buf[3] << 8 : 0);
        break;
    case 2:
        xfer = buf[6];
        xfer |= (extend ? buf[5] << 8 : 0);
        break;
    }

    return xfer * unit;
}

static int scsi_req_xfer(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf)
{
    cmd->xfer = scsi_cdb_xfer(buf);
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
    case SEEK_10:
    case SYNCHRONIZE_CACHE:
    case SYNCHRONIZE_CACHE_16:
    case LOCATE_16:
    case LOCK_UNLOCK_CACHE:
    case SET_CD_SPEED:
    case SET_LIMITS:
    case WRITE_LONG_10:
    case UPDATE_BLOCK:
    case RESERVE_TRACK:
    case SET_READ_AHEAD:
    case PRE_FETCH:
    case PRE_FETCH_16:
    case ALLOW_OVERWRITE:
        cmd->xfer = 0;
        break;
    case VERIFY_10:
    case VERIFY_12:
    case VERIFY_16:
        if ((buf[1] & 2) == 0) {
            cmd->xfer = 0;
        } else if ((buf[1] & 4) != 0) {
            cmd->xfer = 1;
        }
        cmd->xfer *= dev->blocksize;
        break;
    case MODE_SENSE:
        break;
    case WRITE_SAME_10:
    case WRITE_SAME_16:
        cmd->xfer = buf[1] & 1 ? 0 : dev->blocksize;
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
    case WRITE_6:
        /* length 0 means 256 blocks */
        if (cmd->xfer == 0) {
            cmd->xfer = 256;
        }
        /* fall through */
    case WRITE_10:
    case WRITE_VERIFY_10:
    case WRITE_12:
    case WRITE_VERIFY_12:
    case WRITE_16:
    case WRITE_VERIFY_16:
        cmd->xfer *= dev->blocksize;
        break;
    case READ_6:
    case READ_REVERSE:
        /* length 0 means 256 blocks */
        if (cmd->xfer == 0) {
            cmd->xfer = 256;
        }
        /* fall through */
    case READ_10:
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
    case ATA_PASSTHROUGH_12:
        if (dev->type == TYPE_ROM) {
            /* BLANK command of MMC */
            cmd->xfer = 0;
        } else {
            cmd->xfer = ata_passthrough_12_xfer(dev, buf);
        }
        break;
    case ATA_PASSTHROUGH_16:
        cmd->xfer = ata_passthrough_16_xfer(dev, buf);
        break;
    }
    return 0;
}

static int scsi_req_stream_xfer(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf)
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
        cmd->xfer = buf[4] | (buf[3] << 8) | (buf[2] << 16);
        if (buf[1] & 0x01) { /* fixed */
            cmd->xfer *= dev->blocksize;
        }
        break;
    case READ_16:
    case READ_REVERSE_16:
    case VERIFY_16:
    case WRITE_16:
        cmd->xfer = buf[14] | (buf[13] << 8) | (buf[12] << 16);
        if (buf[1] & 0x01) { /* fixed */
            cmd->xfer *= dev->blocksize;
        }
        break;
    case REWIND:
    case LOAD_UNLOAD:
        cmd->xfer = 0;
        break;
    case SPACE_16:
        cmd->xfer = buf[13] | (buf[12] << 8);
        break;
    case READ_POSITION:
        switch (buf[1] & 0x1f) /* operation code */ {
        case SHORT_FORM_BLOCK_ID:
        case SHORT_FORM_VENDOR_SPECIFIC:
            cmd->xfer = 20;
            break;
        case LONG_FORM:
            cmd->xfer = 32;
            break;
        case EXTENDED_FORM:
            cmd->xfer = buf[8] | (buf[7] << 8);
            break;
        default:
            return -1;
        }

        break;
    case FORMAT_UNIT:
        cmd->xfer = buf[4] | (buf[3] << 8);
        break;
    /* generic commands */
    default:
        return scsi_req_xfer(cmd, dev, buf);
    }
    return 0;
}

static int scsi_req_medium_changer_xfer(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf)
{
    switch (buf[0]) {
    /* medium changer commands */
    case EXCHANGE_MEDIUM:
    case INITIALIZE_ELEMENT_STATUS:
    case INITIALIZE_ELEMENT_STATUS_WITH_RANGE:
    case MOVE_MEDIUM:
    case POSITION_TO_ELEMENT:
        cmd->xfer = 0;
        break;
    case READ_ELEMENT_STATUS:
        cmd->xfer = buf[9] | (buf[8] << 8) | (buf[7] << 16);
        break;

    /* generic commands */
    default:
        return scsi_req_xfer(cmd, dev, buf);
    }
    return 0;
}

static int scsi_req_scanner_length(SCSICommand *cmd, SCSIDevice *dev, uint8_t *buf)
{
    switch (buf[0]) {
    /* Scanner commands */
    case OBJECT_POSITION:
        cmd->xfer = 0;
        break;
    case SCAN:
        cmd->xfer = buf[4];
        break;
    case READ_10:
    case SEND:
    case GET_WINDOW:
    case SET_WINDOW:
        cmd->xfer = buf[8] | (buf[7] << 8) | (buf[6] << 16);
        break;
    default:
        /* GET_DATA_BUFFER_STATUS xfer handled by scsi_req_xfer */
        return scsi_req_xfer(cmd, dev, buf);
    }

    return 0;
}

static void scsi_cmd_xfer_mode(SCSICommand *cmd)
{
    if (!cmd->xfer) {
        cmd->mode = SCSI_XFER_NONE;
        return;
    }
    switch (cmd->buf[0]) {
    case WRITE_6:
    case WRITE_10:
    case WRITE_VERIFY_10:
    case WRITE_12:
    case WRITE_VERIFY_12:
    case WRITE_16:
    case WRITE_VERIFY_16:
    case VERIFY_10:
    case VERIFY_12:
    case VERIFY_16:
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
    case WRITE_SAME_16:
    case UNMAP:
    case SEARCH_HIGH_12:
    case SEARCH_EQUAL_12:
    case SEARCH_LOW_12:
    case MEDIUM_SCAN:
    case SEND_VOLUME_TAG:
    case SEND_CUE_SHEET:
    case SEND_DVD_STRUCTURE:
    case PERSISTENT_RESERVE_OUT:
    case MAINTENANCE_OUT:
    case SET_WINDOW:
    case SCAN:
        /* SCAN conflicts with START_STOP.  START_STOP has cmd->xfer set to 0 for
         * non-scanner devices, so we only get here for SCAN and not for START_STOP.
         */
        cmd->mode = SCSI_XFER_TO_DEV;
        break;
    case ATA_PASSTHROUGH_12:
    case ATA_PASSTHROUGH_16:
        /* T_DIR */
        cmd->mode = (cmd->buf[2] & 0x8) ?
                   SCSI_XFER_FROM_DEV : SCSI_XFER_TO_DEV;
        break;
    default:
        cmd->mode = SCSI_XFER_FROM_DEV;
        break;
    }
}

int scsi_req_parse_cdb(SCSIDevice *dev, SCSICommand *cmd, uint8_t *buf,
                       size_t buf_len)
{
    int rc;
    int len;

    cmd->lba = -1;
    len = scsi_cdb_length(buf);
    if (len < 0 || len > buf_len) {
        return -1;
    }

    cmd->len = len;
    switch (dev->type) {
    case TYPE_TAPE:
        rc = scsi_req_stream_xfer(cmd, dev, buf);
        break;
    case TYPE_MEDIUM_CHANGER:
        rc = scsi_req_medium_changer_xfer(cmd, dev, buf);
        break;
    case TYPE_SCANNER:
        rc = scsi_req_scanner_length(cmd, dev, buf);
        break;
    default:
        rc = scsi_req_xfer(cmd, dev, buf);
        break;
    }

    if (rc != 0)
        return rc;

    memcpy(cmd->buf, buf, cmd->len);
    scsi_cmd_xfer_mode(cmd);
    cmd->lba = scsi_cmd_lba(cmd);
    return 0;
}

void scsi_device_report_change(SCSIDevice *dev, SCSISense sense)
{
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, dev->qdev.parent_bus);

    scsi_device_set_ua(dev, sense);
    if (bus->info->change) {
        bus->info->change(bus, dev, sense);
    }
}

SCSIRequest *scsi_req_ref(SCSIRequest *req)
{
    assert(req->refcount > 0);
    req->refcount++;
    return req;
}

void scsi_req_unref(SCSIRequest *req)
{
    assert(req->refcount > 0);
    if (--req->refcount == 0) {
        BusState *qbus = req->dev->qdev.parent_bus;
        SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, qbus);

        if (bus->info->free_request && req->hba_private) {
            bus->info->free_request(bus, req->hba_private);
        }
        if (req->ops->free_req) {
            req->ops->free_req(req);
        }
        object_unref(OBJECT(req->dev));
        object_unref(OBJECT(qbus->parent));
        g_free(req);
    }
}

/* Tell the device that we finished processing this chunk of I/O.  It
   will start the next chunk or complete the command.  */
void scsi_req_continue(SCSIRequest *req)
{
    if (req->io_canceled) {
        trace_scsi_req_continue_canceled(req->dev->id, req->lun, req->tag);
        return;
    }
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
        req->residual -= len;
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
        dma_buf_read(buf, len, &req->residual, req->sg,
                     MEMTXATTRS_UNSPECIFIED);
    } else {
        dma_buf_write(buf, len, &req->residual, req->sg,
                      MEMTXATTRS_UNSPECIFIED);
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

void scsi_req_complete_failed(SCSIRequest *req, int host_status)
{
    SCSISense sense;
    int status;

    assert(req->status == -1 && req->host_status == -1);
    assert(req->ops != &reqops_unit_attention);

    if (!req->bus->info->fail) {
        status = scsi_sense_from_host_status(req->host_status, &sense);
        if (status == CHECK_CONDITION) {
            scsi_req_build_sense(req, sense);
        }
        scsi_req_complete(req, status);
        return;
    }

    req->host_status = host_status;
    scsi_req_ref(req);
    scsi_req_dequeue(req);
    req->bus->info->fail(req);

    /* Cancelled requests might end up being completed instead of cancelled */
    notifier_list_notify(&req->cancel_notifiers, req);
    scsi_req_unref(req);
}

void scsi_req_complete(SCSIRequest *req, int status)
{
    assert(req->status == -1 && req->host_status == -1);
    req->status = status;
    req->host_status = SCSI_HOST_OK;

    assert(req->sense_len <= sizeof(req->sense));
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
    req->bus->info->complete(req, req->residual);

    /* Cancelled requests might end up being completed instead of cancelled */
    notifier_list_notify(&req->cancel_notifiers, req);
    scsi_req_unref(req);
}

/* Called by the devices when the request is canceled. */
void scsi_req_cancel_complete(SCSIRequest *req)
{
    assert(req->io_canceled);
    if (req->bus->info->cancel) {
        req->bus->info->cancel(req);
    }
    notifier_list_notify(&req->cancel_notifiers, req);
    scsi_req_unref(req);
}

/* Cancel @req asynchronously. @notifier is added to @req's cancellation
 * notifier list, the bus will be notified the requests cancellation is
 * completed.
 * */
void scsi_req_cancel_async(SCSIRequest *req, Notifier *notifier)
{
    trace_scsi_req_cancel(req->dev->id, req->lun, req->tag);
    if (notifier) {
        notifier_list_add(&req->cancel_notifiers, notifier);
    }
    if (req->io_canceled) {
        /* A blk_aio_cancel_async is pending; when it finishes,
         * scsi_req_cancel_complete will be called and will
         * call the notifier we just added.  Just wait for that.
         */
        assert(req->aiocb);
        return;
    }
    /* Dropped in scsi_req_cancel_complete.  */
    scsi_req_ref(req);
    scsi_req_dequeue(req);
    req->io_canceled = true;
    if (req->aiocb) {
        blk_aio_cancel_async(req->aiocb);
    } else {
        scsi_req_cancel_complete(req);
    }
}

void scsi_req_cancel(SCSIRequest *req)
{
    trace_scsi_req_cancel(req->dev->id, req->lun, req->tag);
    if (!req->enqueued) {
        return;
    }
    assert(!req->io_canceled);
    /* Dropped in scsi_req_cancel_complete.  */
    scsi_req_ref(req);
    scsi_req_dequeue(req);
    req->io_canceled = true;
    if (req->aiocb) {
        blk_aio_cancel(req->aiocb);
    } else {
        scsi_req_cancel_complete(req);
    }
}

static int scsi_ua_precedence(SCSISense sense)
{
    if (sense.key != UNIT_ATTENTION) {
        return INT_MAX;
    }
    if (sense.asc == 0x29 && sense.ascq == 0x04) {
        /* DEVICE INTERNAL RESET goes with POWER ON OCCURRED */
        return 1;
    } else if (sense.asc == 0x3F && sense.ascq == 0x01) {
        /* MICROCODE HAS BEEN CHANGED goes with SCSI BUS RESET OCCURRED */
        return 2;
    } else if (sense.asc == 0x29 && (sense.ascq == 0x05 || sense.ascq == 0x06)) {
        /* These two go with "all others". */
        ;
    } else if (sense.asc == 0x29 && sense.ascq <= 0x07) {
        /* POWER ON, RESET OR BUS DEVICE RESET OCCURRED = 0
         * POWER ON OCCURRED = 1
         * SCSI BUS RESET OCCURRED = 2
         * BUS DEVICE RESET FUNCTION OCCURRED = 3
         * I_T NEXUS LOSS OCCURRED = 7
         */
        return sense.ascq;
    } else if (sense.asc == 0x2F && sense.ascq == 0x01) {
        /* COMMANDS CLEARED BY POWER LOSS NOTIFICATION  */
        return 8;
    }
    return (sense.asc << 8) | sense.ascq;
}

void scsi_bus_set_ua(SCSIBus *bus, SCSISense sense)
{
    int prec1, prec2;
    if (sense.key != UNIT_ATTENTION) {
        return;
    }

    /*
     * Override a pre-existing unit attention condition, except for a more
     * important reset condition.
     */
    prec1 = scsi_ua_precedence(bus->unit_attention);
    prec2 = scsi_ua_precedence(sense);
    if (prec2 < prec1) {
        bus->unit_attention = sense;
    }
}

void scsi_device_set_ua(SCSIDevice *sdev, SCSISense sense)
{
    int prec1, prec2;
    if (sense.key != UNIT_ATTENTION) {
        return;
    }
    trace_scsi_device_set_ua(sdev->id, sdev->lun, sense.key,
                             sense.asc, sense.ascq);

    /*
     * Override a pre-existing unit attention condition, except for a more
     * important reset condition.
    */
    prec1 = scsi_ua_precedence(sdev->unit_attention);
    prec2 = scsi_ua_precedence(sense);
    if (prec2 < prec1) {
        sdev->unit_attention = sense;
    }
}

void scsi_device_purge_requests(SCSIDevice *sdev, SCSISense sense)
{
    SCSIRequest *req;

    aio_context_acquire(blk_get_aio_context(sdev->conf.blk));
    while (!QTAILQ_EMPTY(&sdev->requests)) {
        req = QTAILQ_FIRST(&sdev->requests);
        scsi_req_cancel_async(req, NULL);
    }
    blk_drain(sdev->conf.blk);
    aio_context_release(blk_get_aio_context(sdev->conf.blk));
    scsi_device_set_ua(sdev, sense);
}

static char *scsibus_get_dev_path(DeviceState *dev)
{
    SCSIDevice *d = SCSI_DEVICE(dev);
    DeviceState *hba = dev->parent_bus->parent;
    char *id;
    char *path;

    id = qdev_get_dev_path(hba);
    if (id) {
        path = g_strdup_printf("%s/%d:%d:%d", id, d->channel, d->id, d->lun);
    } else {
        path = g_strdup_printf("%d:%d:%d", d->channel, d->id, d->lun);
    }
    g_free(id);
    return path;
}

static char *scsibus_get_fw_dev_path(DeviceState *dev)
{
    SCSIDevice *d = SCSI_DEVICE(dev);
    return g_strdup_printf("channel@%x/%s@%x,%x", d->channel,
                           qdev_fw_name(dev), d->id, d->lun);
}

/* SCSI request list.  For simplicity, pv points to the whole device */

static int put_scsi_requests(QEMUFile *f, void *pv, size_t size,
                             const VMStateField *field, JSONWriter *vmdesc)
{
    SCSIDevice *s = pv;
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, s->qdev.parent_bus);
    SCSIRequest *req;

    QTAILQ_FOREACH(req, &s->requests, next) {
        assert(!req->io_canceled);
        assert(req->status == -1 && req->host_status == -1);
        assert(req->enqueued);

        qemu_put_sbyte(f, req->retry ? 1 : 2);
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

    return 0;
}

static int get_scsi_requests(QEMUFile *f, void *pv, size_t size,
                             const VMStateField *field)
{
    SCSIDevice *s = pv;
    SCSIBus *bus = DO_UPCAST(SCSIBus, qbus, s->qdev.parent_bus);
    int8_t sbyte;

    while ((sbyte = qemu_get_sbyte(f)) > 0) {
        uint8_t buf[SCSI_CMD_BUF_SIZE];
        uint32_t tag;
        uint32_t lun;
        SCSIRequest *req;

        qemu_get_buffer(f, buf, sizeof(buf));
        qemu_get_be32s(f, &tag);
        qemu_get_be32s(f, &lun);
        /*
         * A too-short CDB would have been rejected by scsi_req_new, so just use
         * SCSI_CMD_BUF_SIZE as the CDB length.
         */
        req = scsi_req_new(s, tag, lun, buf, sizeof(buf), NULL);
        req->retry = (sbyte == 1);
        if (bus->info->load_request) {
            req->hba_private = bus->info->load_request(f, req);
        }
        if (req->ops->load_request) {
            req->ops->load_request(f, req);
        }

        /* Just restart it later.  */
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

static const VMStateInfo vmstate_info_scsi_requests = {
    .name = "scsi-requests",
    .get  = get_scsi_requests,
    .put  = put_scsi_requests,
};

static bool scsi_sense_state_needed(void *opaque)
{
    SCSIDevice *s = opaque;

    return s->sense_len > SCSI_SENSE_BUF_SIZE_OLD;
}

static const VMStateDescription vmstate_scsi_sense_state = {
    .name = "SCSIDevice/sense",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = scsi_sense_state_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_SUB_ARRAY(sense, SCSIDevice,
                                SCSI_SENSE_BUF_SIZE_OLD,
                                SCSI_SENSE_BUF_SIZE - SCSI_SENSE_BUF_SIZE_OLD),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_scsi_device = {
    .name = "SCSIDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(unit_attention.key, SCSIDevice),
        VMSTATE_UINT8(unit_attention.asc, SCSIDevice),
        VMSTATE_UINT8(unit_attention.ascq, SCSIDevice),
        VMSTATE_BOOL(sense_is_ua, SCSIDevice),
        VMSTATE_UINT8_SUB_ARRAY(sense, SCSIDevice, 0, SCSI_SENSE_BUF_SIZE_OLD),
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
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_scsi_sense_state,
        NULL
    }
};

static Property scsi_props[] = {
    DEFINE_PROP_UINT32("channel", SCSIDevice, channel, 0),
    DEFINE_PROP_UINT32("scsi-id", SCSIDevice, id, -1),
    DEFINE_PROP_UINT32("lun", SCSIDevice, lun, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void scsi_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_STORAGE, k->categories);
    k->bus_type  = TYPE_SCSI_BUS;
    k->realize   = scsi_qdev_realize;
    k->unrealize = scsi_qdev_unrealize;
    device_class_set_props(k, scsi_props);
}

static void scsi_dev_instance_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    SCSIDevice *s = SCSI_DEVICE(dev);

    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", NULL,
                                  &s->qdev);
}

static const TypeInfo scsi_device_type_info = {
    .name = TYPE_SCSI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SCSIDevice),
    .abstract = true,
    .class_size = sizeof(SCSIDeviceClass),
    .class_init = scsi_device_class_init,
    .instance_init = scsi_dev_instance_init,
};

static void scsi_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    k->get_dev_path = scsibus_get_dev_path;
    k->get_fw_dev_path = scsibus_get_fw_dev_path;
    k->check_address = scsi_bus_check_address;
    hc->unplug = qdev_simple_device_unplug_cb;
}

static const TypeInfo scsi_bus_info = {
    .name = TYPE_SCSI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(SCSIBus),
    .class_init = scsi_bus_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void scsi_register_types(void)
{
    type_register_static(&scsi_bus_info);
    type_register_static(&scsi_device_type_info);
}

type_init(scsi_register_types)
