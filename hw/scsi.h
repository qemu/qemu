#ifndef QEMU_HW_SCSI_H
#define QEMU_HW_SCSI_H

#include "qdev.h"
#include "block.h"
#include "block_int.h"

#define MAX_SCSI_DEVS	255

#define SCSI_CMD_BUF_SIZE     16

typedef struct SCSIBus SCSIBus;
typedef struct SCSIBusOps SCSIBusOps;
typedef struct SCSIDevice SCSIDevice;
typedef struct SCSIDeviceInfo SCSIDeviceInfo;
typedef struct SCSIRequest SCSIRequest;
typedef struct SCSIReqOps SCSIReqOps;

enum SCSIXferMode {
    SCSI_XFER_NONE,      /*  TEST_UNIT_READY, ...            */
    SCSI_XFER_FROM_DEV,  /*  READ, INQUIRY, MODE_SENSE, ...  */
    SCSI_XFER_TO_DEV,    /*  WRITE, MODE_SELECT, ...         */
};

typedef struct SCSISense {
    uint8_t key;
    uint8_t asc;
    uint8_t ascq;
} SCSISense;

#define SCSI_SENSE_BUF_SIZE 96

struct SCSIRequest {
    SCSIBus           *bus;
    SCSIDevice        *dev;
    SCSIReqOps        *ops;
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
    uint8_t sense[SCSI_SENSE_BUF_SIZE];
    uint32_t sense_len;
    bool enqueued;
    void *hba_private;
    QTAILQ_ENTRY(SCSIRequest) next;
};

struct SCSIDevice
{
    DeviceState qdev;
    uint32_t id;
    BlockConf conf;
    SCSIDeviceInfo *info;
    uint8_t sense[SCSI_SENSE_BUF_SIZE];
    uint32_t sense_len;
    QTAILQ_HEAD(, SCSIRequest) requests;
    int blocksize;
    int type;
};

/* cdrom.c */
int cdrom_read_toc(int nb_sectors, uint8_t *buf, int msf, int start_track);
int cdrom_read_toc_raw(int nb_sectors, uint8_t *buf, int msf, int session_num);

/* scsi-bus.c */
struct SCSIReqOps {
    size_t size;
    void (*free_req)(SCSIRequest *req);
    int32_t (*send_command)(SCSIRequest *req, uint8_t *buf);
    void (*read_data)(SCSIRequest *req);
    void (*write_data)(SCSIRequest *req);
    void (*cancel_io)(SCSIRequest *req);
    uint8_t *(*get_buf)(SCSIRequest *req);
};

typedef int (*scsi_qdev_initfn)(SCSIDevice *dev);
struct SCSIDeviceInfo {
    DeviceInfo qdev;
    scsi_qdev_initfn init;
    void (*destroy)(SCSIDevice *s);
    SCSIRequest *(*alloc_req)(SCSIDevice *s, uint32_t tag, uint32_t lun,
                              void *hba_private);
    SCSIReqOps reqops;
};

struct SCSIBusOps {
    void (*transfer_data)(SCSIRequest *req, uint32_t arg);
    void (*complete)(SCSIRequest *req, uint32_t arg);
    void (*cancel)(SCSIRequest *req);
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

/*
 * Predefined sense codes
 */

/* No sense data available */
extern const struct SCSISense sense_code_NO_SENSE;
/* LUN not ready, Manual intervention required */
extern const struct SCSISense sense_code_LUN_NOT_READY;
/* LUN not ready, Medium not present */
extern const struct SCSISense sense_code_NO_MEDIUM;
/* Hardware error, internal target failure */
extern const struct SCSISense sense_code_TARGET_FAILURE;
/* Illegal request, invalid command operation code */
extern const struct SCSISense sense_code_INVALID_OPCODE;
/* Illegal request, LBA out of range */
extern const struct SCSISense sense_code_LBA_OUT_OF_RANGE;
/* Illegal request, Invalid field in CDB */
extern const struct SCSISense sense_code_INVALID_FIELD;
/* Illegal request, LUN not supported */
extern const struct SCSISense sense_code_LUN_NOT_SUPPORTED;
/* Command aborted, I/O process terminated */
extern const struct SCSISense sense_code_IO_ERROR;
/* Command aborted, I_T Nexus loss occurred */
extern const struct SCSISense sense_code_I_T_NEXUS_LOSS;
/* Command aborted, Logical Unit failure */
extern const struct SCSISense sense_code_LUN_FAILURE;

#define SENSE_CODE(x) sense_code_ ## x

int scsi_sense_valid(SCSISense sense);

SCSIRequest *scsi_req_alloc(SCSIReqOps *reqops, SCSIDevice *d, uint32_t tag,
                            uint32_t lun, void *hba_private);
SCSIRequest *scsi_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun,
                          void *hba_private);
int32_t scsi_req_enqueue(SCSIRequest *req, uint8_t *buf);
void scsi_req_free(SCSIRequest *req);
SCSIRequest *scsi_req_ref(SCSIRequest *req);
void scsi_req_unref(SCSIRequest *req);

void scsi_req_build_sense(SCSIRequest *req, SCSISense sense);
int scsi_req_parse(SCSIRequest *req, uint8_t *buf);
void scsi_req_print(SCSIRequest *req);
void scsi_req_continue(SCSIRequest *req);
void scsi_req_data(SCSIRequest *req, int len);
void scsi_req_complete(SCSIRequest *req, int status);
uint8_t *scsi_req_get_buf(SCSIRequest *req);
int scsi_req_get_sense(SCSIRequest *req, uint8_t *buf, int len);
void scsi_req_abort(SCSIRequest *req, int status);
void scsi_req_cancel(SCSIRequest *req);
void scsi_device_purge_requests(SCSIDevice *sdev);
int scsi_device_get_sense(SCSIDevice *dev, uint8_t *buf, int len, bool fixed);

#endif
