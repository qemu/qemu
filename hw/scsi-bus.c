#include "hw.h"
#include "qemu-error.h"
#include "scsi.h"
#include "scsi-defs.h"
#include "qdev.h"
#include "blockdev.h"
#include "trace.h"

static char *scsibus_get_fw_dev_path(DeviceState *dev);

static struct BusInfo scsi_bus_info = {
    .name  = "SCSI",
    .size  = sizeof(SCSIBus),
    .get_fw_dev_path = scsibus_get_fw_dev_path,
    .props = (Property[]) {
        DEFINE_PROP_UINT32("scsi-id", SCSIDevice, id, -1),
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

SCSIRequest *scsi_req_alloc(size_t size, SCSIDevice *d, uint32_t tag, uint32_t lun)
{
    SCSIRequest *req;

    req = qemu_mallocz(size);
    req->refcount = 1;
    req->bus = scsi_bus_from_device(d);
    req->dev = d;
    req->tag = tag;
    req->lun = lun;
    req->status = -1;
    trace_scsi_req_alloc(req->dev->id, req->lun, req->tag);
    return req;
}

SCSIRequest *scsi_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun)
{
    return d->info->alloc_req(d, tag, lun);
}

int32_t scsi_req_enqueue(SCSIRequest *req, uint8_t *buf)
{
    int32_t rc;

    assert(!req->enqueued);
    scsi_req_ref(req);
    req->enqueued = true;
    QTAILQ_INSERT_TAIL(&req->dev->requests, req, next);

    scsi_req_ref(req);
    rc = req->dev->info->send_command(req, buf);
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
        trace_scsi_req_parse_bad(req->dev->id, req->lun, req->tag, cmd[0]);
        return -1;
    }

    switch(cmd[0]) {
    case TEST_UNIT_READY:
    case REZERO_UNIT:
    case START_STOP:
    case SEEK_6:
    case WRITE_FILEMARKS:
    case SPACE:
    case RESERVE:
    case RELEASE:
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
    case WRITE_16:
    case WRITE_VERIFY_16:
        req->cmd.xfer *= req->dev->blocksize;
        break;
    case READ_10:
    case READ_6:
    case READ_REVERSE:
    case RECOVER_BUFFERED_DATA:
    case READ_12:
    case READ_16:
        req->cmd.xfer *= req->dev->blocksize;
        break;
    case INQUIRY:
        req->cmd.xfer = cmd[4] | (cmd[3] << 8);
        break;
    case MAINTENANCE_OUT:
    case MAINTENANCE_IN:
        if (req->dev->type == TYPE_ROM) {
            /* GPCMD_REPORT_KEY and GPCMD_SEND_KEY from multi media commands */
            req->cmd.xfer = cmd[9] | (cmd[8] << 8);
        }
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
    case WRITE_LONG:
    case WRITE_SAME:
    case SEARCH_HIGH_12:
    case SEARCH_EQUAL_12:
    case SEARCH_LOW_12:
    case SET_WINDOW:
    case MEDIUM_SCAN:
    case SEND_VOLUME_TAG:
    case WRITE_LONG_2:
    case PERSISTENT_RESERVE_OUT:
    case MAINTENANCE_OUT:
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
    trace_scsi_req_parsed(req->dev->id, req->lun, req->tag, buf[0],
                          req->cmd.mode, req->cmd.xfer, req->cmd.lba);
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

/*
 * scsi_build_sense
 *
 * Build a sense buffer
 */
int scsi_build_sense(SCSISense sense, uint8_t *buf, int len, int fixed)
{
    if (!fixed && len < 8) {
        return 0;
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
        [ REZERO_UNIT              ] = "REZERO_UNIT",
        /* REWIND and REZERO_UNIT use the same operation code */
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

        [ SET_WINDOW               ] = "SET_WINDOW",
        [ READ_CAPACITY            ] = "READ_CAPACITY",
        [ READ_10                  ] = "READ_10",
        [ WRITE_10                 ] = "WRITE_10",
        [ SEEK_10                  ] = "SEEK_10",
        [ WRITE_VERIFY             ] = "WRITE_VERIFY",
        [ VERIFY                   ] = "VERIFY",
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
        [ READ_LONG                ] = "READ_LONG",
        [ WRITE_LONG               ] = "WRITE_LONG",
        [ CHANGE_DEFINITION        ] = "CHANGE_DEFINITION",
        [ WRITE_SAME               ] = "WRITE_SAME",
        [ READ_TOC                 ] = "READ_TOC",
        [ LOG_SELECT               ] = "LOG_SELECT",
        [ LOG_SENSE                ] = "LOG_SENSE",
        [ MODE_SELECT_10           ] = "MODE_SELECT_10",
        [ RESERVE_10               ] = "RESERVE_10",
        [ RELEASE_10               ] = "RELEASE_10",
        [ MODE_SENSE_10            ] = "MODE_SENSE_10",
        [ PERSISTENT_RESERVE_IN    ] = "PERSISTENT_RESERVE_IN",
        [ PERSISTENT_RESERVE_OUT   ] = "PERSISTENT_RESERVE_OUT",
        [ MOVE_MEDIUM              ] = "MOVE_MEDIUM",
        [ READ_12                  ] = "READ_12",
        [ WRITE_12                 ] = "WRITE_12",
        [ WRITE_VERIFY_12          ] = "WRITE_VERIFY_12",
        [ SEARCH_HIGH_12           ] = "SEARCH_HIGH_12",
        [ SEARCH_EQUAL_12          ] = "SEARCH_EQUAL_12",
        [ SEARCH_LOW_12            ] = "SEARCH_LOW_12",
        [ READ_ELEMENT_STATUS      ] = "READ_ELEMENT_STATUS",
        [ SEND_VOLUME_TAG          ] = "SEND_VOLUME_TAG",
        [ WRITE_LONG_2             ] = "WRITE_LONG_2",

        [ REPORT_DENSITY_SUPPORT   ] = "REPORT_DENSITY_SUPPORT",
        [ GET_CONFIGURATION        ] = "GET_CONFIGURATION",
        [ READ_16                  ] = "READ_16",
        [ WRITE_16                 ] = "WRITE_16",
        [ WRITE_VERIFY_16          ] = "WRITE_VERIFY_16",
        [ SERVICE_ACTION_IN        ] = "SERVICE_ACTION_IN",
        [ REPORT_LUNS              ] = "REPORT_LUNS",
        [ LOAD_UNLOAD              ] = "LOAD_UNLOAD",
        [ SET_CD_SPEED             ] = "SET_CD_SPEED",
        [ BLANK                    ] = "BLANK",
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
        if (req->dev->info->free_req) {
            req->dev->info->free_req(req);
        }
        qemu_free(req);
    }
}

/* Called by the devices when data is ready for the HBA.  The HBA should
   start a DMA operation to read or fill the device's data buffer.
   Once it completes, calling one of req->dev->info->read_data or
   req->dev->info->write_data (depending on the direction of the
   transfer) will restart I/O.  */
void scsi_req_data(SCSIRequest *req, int len)
{
    trace_scsi_req_data(req->dev->id, req->lun, req->tag, len);
    req->bus->ops->complete(req, SCSI_REASON_DATA, len);
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

void scsi_req_complete(SCSIRequest *req)
{
    assert(req->status != -1);
    scsi_req_ref(req);
    scsi_req_dequeue(req);
    req->bus->ops->complete(req, SCSI_REASON_DONE, req->status);
    scsi_req_unref(req);
}

void scsi_req_cancel(SCSIRequest *req)
{
    if (req->dev && req->dev->info->cancel_io) {
        req->dev->info->cancel_io(req);
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
    req->status = status;
    if (req->dev && req->dev->info->cancel_io) {
        req->dev->info->cancel_io(req);
    }
    scsi_req_complete(req);
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
    SCSIDevice *d = (SCSIDevice*)dev;
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
