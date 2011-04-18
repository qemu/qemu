#ifndef QEMU_HW_SCSI_H
#define QEMU_HW_SCSI_H

#include "qdev.h"
#include "block.h"
#include "block_int.h"

#define MAX_SCSI_DEVS	255

#define SCSI_CMD_BUF_SIZE     16

/* scsi-disk.c */
enum scsi_reason {
    SCSI_REASON_DONE, /* Command complete.  */
    SCSI_REASON_DATA  /* Transfer complete, more data required.  */
};

typedef struct SCSIBus SCSIBus;
typedef struct SCSIBusOps SCSIBusOps;
typedef struct SCSIDevice SCSIDevice;
typedef struct SCSIDeviceInfo SCSIDeviceInfo;
typedef struct SCSIRequest SCSIRequest;

enum SCSIXferMode {
    SCSI_XFER_NONE,      /*  TEST_UNIT_READY, ...            */
    SCSI_XFER_FROM_DEV,  /*  READ, INQUIRY, MODE_SENSE, ...  */
    SCSI_XFER_TO_DEV,    /*  WRITE, MODE_SELECT, ...         */
};

struct SCSIRequest {
    SCSIBus           *bus;
    SCSIDevice        *dev;
    uint32_t          refcount;
    uint32_t          tag;
    uint32_t          lun;
    uint32_t          status;
    struct {
        uint8_t buf[SCSI_CMD_BUF_SIZE];
        int len;
        size_t xfer;
        uint64_t lba;
        enum SCSIXferMode mode;
    } cmd;
    BlockDriverAIOCB  *aiocb;
    bool enqueued;
    QTAILQ_ENTRY(SCSIRequest) next;
};

struct SCSIDevice
{
    DeviceState qdev;
    uint32_t id;
    BlockConf conf;
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
    SCSIRequest *(*alloc_req)(SCSIDevice *s, uint32_t tag, uint32_t lun);
    void (*free_req)(SCSIRequest *req);
    int32_t (*send_command)(SCSIRequest *req, uint8_t *buf);
    void (*read_data)(SCSIRequest *req);
    int (*write_data)(SCSIRequest *req);
    void (*cancel_io)(SCSIRequest *req);
    uint8_t *(*get_buf)(SCSIRequest *req);
};

struct SCSIBusOps {
    void (*complete)(SCSIRequest *req, int reason, uint32_t arg);
};

struct SCSIBus {
    BusState qbus;
    int busnr;

    int tcq, ndev;
    const SCSIBusOps *ops;

    SCSIDevice *devs[MAX_SCSI_DEVS];
};

void scsi_bus_new(SCSIBus *bus, DeviceState *host, int tcq, int ndev,
                  const SCSIBusOps *ops);
void scsi_qdev_register(SCSIDeviceInfo *info);

static inline SCSIBus *scsi_bus_from_device(SCSIDevice *d)
{
    return DO_UPCAST(SCSIBus, qbus, d->qdev.parent_bus);
}

SCSIDevice *scsi_bus_legacy_add_drive(SCSIBus *bus, BlockDriverState *bdrv,
                                      int unit, bool removable);
int scsi_bus_legacy_handle_cmdline(SCSIBus *bus);

SCSIRequest *scsi_req_alloc(size_t size, SCSIDevice *d, uint32_t tag, uint32_t lun);
void scsi_req_enqueue(SCSIRequest *req);
void scsi_req_free(SCSIRequest *req);
void scsi_req_dequeue(SCSIRequest *req);
SCSIRequest *scsi_req_ref(SCSIRequest *req);
void scsi_req_unref(SCSIRequest *req);

int scsi_req_parse(SCSIRequest *req, uint8_t *buf);
void scsi_req_print(SCSIRequest *req);
void scsi_req_data(SCSIRequest *req, int len);
void scsi_req_complete(SCSIRequest *req);

#endif
