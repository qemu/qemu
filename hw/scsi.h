#ifndef QEMU_HW_SCSI_H
#define QEMU_HW_SCSI_H

#include "qdev.h"
#include "block.h"

#define SCSI_CMD_BUF_SIZE     16

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

typedef struct SCSIRequest {
    SCSIBus           *bus;
    SCSIDevice        *dev;
    uint32_t          tag;
    uint32_t          lun;
    struct {
        uint8_t buf[SCSI_CMD_BUF_SIZE];
        int len;
        size_t xfer;
        uint64_t lba;
    } cmd;
    BlockDriverAIOCB  *aiocb;
    QTAILQ_ENTRY(SCSIRequest) next;
} SCSIRequest;

struct SCSIDevice
{
    DeviceState qdev;
    uint32_t id;
    SCSIDeviceInfo *info;
    QTAILQ_HEAD(, SCSIRequest) requests;
    int blocksize;
    int type;
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

SCSIRequest *scsi_req_alloc(size_t size, SCSIDevice *d, uint32_t tag, uint32_t lun);
SCSIRequest *scsi_req_find(SCSIDevice *d, uint32_t tag);
void scsi_req_free(SCSIRequest *req);
int scsi_req_parse(SCSIRequest *req, uint8_t *buf);

#endif
