#include "hw.h"
#include "sysemu.h"
#include "scsi-disk.h"
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
