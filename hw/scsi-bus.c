#include "hw.h"
#include "sysemu.h"
#include "scsi.h"
#include "scsi-defs.h"
#include "block.h"
#include "qdev.h"

static struct BusInfo scsi_bus_info = {
    .name  = "SCSI",
    .size  = sizeof(SCSIBus),
    .props = (Property[]) {
        DEFINE_PROP_UINT32("scsi-id", SCSIDevice, id, -1),
        DEFINE_PROP_END_OF_LIST(),
    },
};
static int next_scsi_bus;

/* Create a scsi bus, and attach devices to it.  */
void scsi_bus_new(SCSIBus *bus, DeviceState *host, int tcq, int ndev,
                  scsi_completionfn complete)
{
    qbus_create_inplace(&bus->qbus, &scsi_bus_info, host, NULL);
    bus->busnr = next_scsi_bus++;
    bus->tcq = tcq;
    bus->ndev = ndev;
    bus->complete = complete;
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
        qemu_error("bad scsi device id: %d\n", dev->id);
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
/* FIXME callers should check for failure, but don't */
SCSIDevice *scsi_bus_legacy_add_drive(SCSIBus *bus, DriveInfo *dinfo, int unit)
{
    const char *driver;
    DeviceState *dev;

    driver = bdrv_is_sg(dinfo->bdrv) ? "scsi-generic" : "scsi-disk";
    dev = qdev_create(&bus->qbus, driver);
    qdev_prop_set_uint32(dev, "scsi-id", unit);
    qdev_prop_set_drive(dev, "drive", dinfo);
    if (qdev_init(dev) < 0)
        return NULL;
    return DO_UPCAST(SCSIDevice, qdev, dev);
}

void scsi_bus_legacy_handle_cmdline(SCSIBus *bus)
{
    DriveInfo *dinfo;
    int unit;

    for (unit = 0; unit < MAX_SCSI_DEVS; unit++) {
        dinfo = drive_get(IF_SCSI, bus->busnr, unit);
        if (dinfo == NULL) {
            continue;
        }
        scsi_bus_legacy_add_drive(bus, dinfo, unit);
    }
}

void scsi_dev_clear_sense(SCSIDevice *dev)
{
    memset(&dev->sense, 0, sizeof(dev->sense));
}

void scsi_dev_set_sense(SCSIDevice *dev, uint8_t key)
{
    dev->sense.key = key;
}

SCSIRequest *scsi_req_alloc(size_t size, SCSIDevice *d, uint32_t tag, uint32_t lun)
{
    SCSIRequest *req;

    req = qemu_mallocz(size);
    req->bus = scsi_bus_from_device(d);
    req->dev = d;
    req->tag = tag;
    req->lun = lun;
    req->status = -1;
    QTAILQ_INSERT_TAIL(&d->requests, req, next);
    return req;
}

SCSIRequest *scsi_req_find(SCSIDevice *d, uint32_t tag)
{
    SCSIRequest *req;

    QTAILQ_FOREACH(req, &d->requests, next) {
        if (req->tag == tag) {
            return req;
        }
    }
    return NULL;
}

void scsi_req_free(SCSIRequest *req)
{
    QTAILQ_REMOVE(&req->dev->requests, req, next);
    qemu_free(req);
}

static int scsi_req_length(SCSIRequest *req, uint8_t *cmd)
{
    switch (cmd[0] >> 5) {
    case 0:
        req->cmd.xfer = cmd[4];
        req->cmd.len = 6;
        /* length 0 means 256 blocks */
        if (req->cmd.xfer == 0)
            req->cmd.xfer = 256;
        break;
    case 1:
    case 2:
        req->cmd.xfer = cmd[8] | (cmd[7] << 8);
        req->cmd.len = 10;
        break;
    case 4:
        req->cmd.xfer = cmd[13] | (cmd[12] << 8) | (cmd[11] << 16) | (cmd[10] << 24);
        req->cmd.len = 16;
        break;
    case 5:
        req->cmd.xfer = cmd[9] | (cmd[8] << 8) | (cmd[7] << 16) | (cmd[6] << 24);
        req->cmd.len = 12;
        break;
    default:
        return -1;
    }

    switch(cmd[0]) {
    case TEST_UNIT_READY:
    case REZERO_UNIT:
    case START_STOP:
    case SEEK_6:
    case WRITE_FILEMARKS:
    case SPACE:
    case ERASE:
    case ALLOW_MEDIUM_REMOVAL:
    case VERIFY:
    case SEEK_10:
    case SYNCHRONIZE_CACHE:
    case LOCK_UNLOCK_CACHE:
    case LOAD_UNLOAD:
    case SET_CD_SPEED:
    case SET_LIMITS:
    case WRITE_LONG:
    case MOVE_MEDIUM:
    case UPDATE_BLOCK:
        req->cmd.xfer = 0;
        break;
    case MODE_SENSE:
        break;
    case WRITE_SAME:
        req->cmd.xfer = 1;
        break;
    case READ_CAPACITY:
        req->cmd.xfer = 8;
        break;
    case READ_BLOCK_LIMITS:
        req->cmd.xfer = 6;
        break;
    case READ_POSITION:
        req->cmd.xfer = 20;
        break;
    case SEND_VOLUME_TAG:
        req->cmd.xfer *= 40;
        break;
    case MEDIUM_SCAN:
        req->cmd.xfer *= 8;
        break;
    case WRITE_10:
    case WRITE_VERIFY:
    case WRITE_6:
    case WRITE_12:
    case WRITE_VERIFY_12:
        req->cmd.xfer *= req->dev->blocksize;
        break;
    case READ_10:
    case READ_6:
    case READ_REVERSE:
    case RECOVER_BUFFERED_DATA:
    case READ_12:
        req->cmd.xfer *= req->dev->blocksize;
        break;
    case INQUIRY:
        req->cmd.xfer = cmd[4] | (cmd[3] << 8);
        break;
    }
    return 0;
}

static int scsi_req_stream_length(SCSIRequest *req, uint8_t *cmd)
{
    switch(cmd[0]) {
    /* stream commands */
    case READ_6:
    case READ_REVERSE:
    case RECOVER_BUFFERED_DATA:
    case WRITE_6:
        req->cmd.len = 6;
        req->cmd.xfer = cmd[4] | (cmd[3] << 8) | (cmd[2] << 16);
        if (cmd[1] & 0x01) /* fixed */
            req->cmd.xfer *= req->dev->blocksize;
        break;
    case REWIND:
    case START_STOP:
        req->cmd.len = 6;
        req->cmd.xfer = 0;
        break;
    /* generic commands */
    default:
        return scsi_req_length(req, cmd);
    }
    return 0;
}

static void scsi_req_xfer_mode(SCSIRequest *req)
{
    switch (req->cmd.buf[0]) {
    case WRITE_6:
    case WRITE_10:
    case WRITE_VERIFY:
    case WRITE_12:
    case WRITE_VERIFY_12:
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
    case RESERVE:
    case SEARCH_EQUAL:
    case SEARCH_HIGH:
    case SEARCH_LOW:
    case UPDATE_BLOCK:
    case WRITE_LONG:
    case WRITE_SAME:
    case SEARCH_HIGH_12:
    case SEARCH_EQUAL_12:
    case SEARCH_LOW_12:
    case SET_WINDOW:
    case MEDIUM_SCAN:
    case SEND_VOLUME_TAG:
    case WRITE_LONG_2:
        req->cmd.mode = SCSI_XFER_TO_DEV;
        break;
    default:
        if (req->cmd.xfer)
            req->cmd.mode = SCSI_XFER_FROM_DEV;
        else {
            req->cmd.mode = SCSI_XFER_NONE;
        }
        break;
    }
}

static uint64_t scsi_req_lba(SCSIRequest *req)
{
    uint8_t *buf = req->cmd.buf;
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

int scsi_req_parse(SCSIRequest *req, uint8_t *buf)
{
    int rc;

    if (req->dev->type == TYPE_TAPE) {
        rc = scsi_req_stream_length(req, buf);
    } else {
        rc = scsi_req_length(req, buf);
    }
    if (rc != 0)
        return rc;

    memcpy(req->cmd.buf, buf, req->cmd.len);
    scsi_req_xfer_mode(req);
    req->cmd.lba = scsi_req_lba(req);
    return 0;
}

void scsi_req_complete(SCSIRequest *req)
{
    assert(req->status != -1);
    req->bus->complete(req->bus, SCSI_REASON_DONE,
                       req->tag,
                       req->status);
}
