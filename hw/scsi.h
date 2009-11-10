#ifndef SCSI_DISK_H
#define SCSI_DISK_H

#include "qdev.h"

/* scsi-disk.c */
enum scsi_reason {
    SCSI_REASON_DONE, /* Command complete.  */
    SCSI_REASON_DATA  /* Transfer complete, more data required.  */
};

typedef struct SCSIBus SCSIBus;
typedef struct SCSIDevice SCSIDevice;
typedef struct SCSIDeviceInfo SCSIDeviceInfo;
typedef void (*scsi_completionfn)(SCSIBus *bus, int reason, uint32_t tag,
                                  uint32_t arg);

struct SCSIDevice
{
    DeviceState qdev;
    uint32_t id;
    SCSIDeviceInfo *info;
};

/* cdrom.c */
int cdrom_read_toc(int nb_sectors, uint8_t *buf, int msf, int start_track);
int cdrom_read_toc_raw(int nb_sectors, uint8_t *buf, int msf, int session_num);

/* scsi-bus.c */
typedef int (*scsi_qdev_initfn)(SCSIDevice *dev);
struct SCSIDeviceInfo {
    DeviceInfo qdev;
    scsi_qdev_initfn init;
    void (*destroy)(SCSIDevice *s);
    int32_t (*send_command)(SCSIDevice *s, uint32_t tag, uint8_t *buf,
                            int lun);
    void (*read_data)(SCSIDevice *s, uint32_t tag);
    int (*write_data)(SCSIDevice *s, uint32_t tag);
    void (*cancel_io)(SCSIDevice *s, uint32_t tag);
    uint8_t *(*get_buf)(SCSIDevice *s, uint32_t tag);
};

typedef void (*SCSIAttachFn)(DeviceState *host, BlockDriverState *bdrv,
              int unit);
struct SCSIBus {
    BusState qbus;
    int busnr;

    int tcq, ndev;
    scsi_completionfn complete;

    SCSIDevice *devs[8];
};

void scsi_bus_new(SCSIBus *bus, DeviceState *host, int tcq, int ndev,
                  scsi_completionfn complete);
void scsi_qdev_register(SCSIDeviceInfo *info);

static inline SCSIBus *scsi_bus_from_device(SCSIDevice *d)
{
    return DO_UPCAST(SCSIBus, qbus, d->qdev.parent_bus);
}

SCSIDevice *scsi_bus_legacy_add_drive(SCSIBus *bus, DriveInfo *dinfo, int unit);
void scsi_bus_legacy_handle_cmdline(SCSIBus *bus);

#endif
