#ifndef QEMU_HW_SCSI_H
#define QEMU_HW_SCSI_H

#include "hw/qdev.h"
#include "hw/block/block.h"
#include "sysemu/sysemu.h"
#include "scsi/utils.h"
#include "qemu/notify.h"

#define MAX_SCSI_DEVS	255

typedef struct SCSIBus SCSIBus;
typedef struct SCSIBusInfo SCSIBusInfo;
typedef struct SCSIDevice SCSIDevice;
typedef struct SCSIRequest SCSIRequest;
typedef struct SCSIReqOps SCSIReqOps;

#define SCSI_SENSE_BUF_SIZE_OLD 96
#define SCSI_SENSE_BUF_SIZE 252

struct SCSIRequest {
    SCSIBus           *bus;
    SCSIDevice        *dev;
    const SCSIReqOps  *ops;
    uint32_t          refcount;
    uint32_t          tag;
    uint32_t          lun;
    uint32_t          status;
    void              *hba_private;
    size_t            resid;
    SCSICommand       cmd;
    NotifierList      cancel_notifiers;

    /* Note:
     * - fields before sense are initialized by scsi_req_alloc;
     * - sense[] is uninitialized;
     * - fields after sense are memset to 0 by scsi_req_alloc.
     * */

    uint8_t           sense[SCSI_SENSE_BUF_SIZE];
    uint32_t          sense_len;
    bool              enqueued;
    bool              io_canceled;
    bool              retry;
    bool              dma_started;
    BlockAIOCB        *aiocb;
    QEMUSGList        *sg;
    QTAILQ_ENTRY(SCSIRequest) next;
};

#define TYPE_SCSI_DEVICE "scsi-device"
#define SCSI_DEVICE(obj) \
     OBJECT_CHECK(SCSIDevice, (obj), TYPE_SCSI_DEVICE)
#define SCSI_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(SCSIDeviceClass, (klass), TYPE_SCSI_DEVICE)
#define SCSI_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SCSIDeviceClass, (obj), TYPE_SCSI_DEVICE)

typedef struct SCSIDeviceClass {
    DeviceClass parent_class;
    void (*realize)(SCSIDevice *dev, Error **errp);
    int (*parse_cdb)(SCSIDevice *dev, SCSICommand *cmd, uint8_t *buf,
                     void *hba_private);
    SCSIRequest *(*alloc_req)(SCSIDevice *s, uint32_t tag, uint32_t lun,
                              uint8_t *buf, void *hba_private);
    void (*unit_attention_reported)(SCSIDevice *s);
} SCSIDeviceClass;

struct SCSIDevice
{
    DeviceState qdev;
    VMChangeStateEntry *vmsentry;
    QEMUBH *bh;
    uint32_t id;
    BlockConf conf;
    SCSISense unit_attention;
    bool sense_is_ua;
    uint8_t sense[SCSI_SENSE_BUF_SIZE];
    uint32_t sense_len;
    QTAILQ_HEAD(, SCSIRequest) requests;
    uint32_t channel;
    uint32_t lun;
    int blocksize;
    int type;
    uint64_t max_lba;
    uint64_t wwn;
    uint64_t port_wwn;
    int scsi_version;
    int default_scsi_version;
    bool needs_vpd_bl_emulation;
};

extern const VMStateDescription vmstate_scsi_device;

#define VMSTATE_SCSI_DEVICE(_field, _state) {                        \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(SCSIDevice),                                \
    .vmsd       = &vmstate_scsi_device,                              \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, SCSIDevice),  \
}

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
    uint8_t *(*get_buf)(SCSIRequest *req);

    void (*save_request)(QEMUFile *f, SCSIRequest *req);
    void (*load_request)(QEMUFile *f, SCSIRequest *req);
};

struct SCSIBusInfo {
    int tcq;
    int max_channel, max_target, max_lun;
    int (*parse_cdb)(SCSIDevice *dev, SCSICommand *cmd, uint8_t *buf,
                     void *hba_private);
    void (*transfer_data)(SCSIRequest *req, uint32_t arg);
    void (*complete)(SCSIRequest *req, uint32_t arg, size_t resid);
    void (*cancel)(SCSIRequest *req);
    void (*change)(SCSIBus *bus, SCSIDevice *dev, SCSISense sense);
    QEMUSGList *(*get_sg_list)(SCSIRequest *req);

    void (*save_request)(QEMUFile *f, SCSIRequest *req);
    void *(*load_request)(QEMUFile *f, SCSIRequest *req);
    void (*free_request)(SCSIBus *bus, void *priv);
};

#define TYPE_SCSI_BUS "SCSI"
#define SCSI_BUS(obj) OBJECT_CHECK(SCSIBus, (obj), TYPE_SCSI_BUS)

struct SCSIBus {
    BusState qbus;
    int busnr;

    SCSISense unit_attention;
    const SCSIBusInfo *info;
};

void scsi_bus_new(SCSIBus *bus, size_t bus_size, DeviceState *host,
                  const SCSIBusInfo *info, const char *bus_name);

static inline SCSIBus *scsi_bus_from_device(SCSIDevice *d)
{
    return DO_UPCAST(SCSIBus, qbus, d->qdev.parent_bus);
}

SCSIDevice *scsi_bus_legacy_add_drive(SCSIBus *bus, BlockBackend *blk,
                                      int unit, bool removable, int bootindex,
                                      bool share_rw,
                                      BlockdevOnError rerror,
                                      BlockdevOnError werror,
                                      const char *serial, Error **errp);
void scsi_bus_legacy_handle_cmdline(SCSIBus *bus);
void scsi_legacy_handle_cmdline(void);

SCSIRequest *scsi_req_alloc(const SCSIReqOps *reqops, SCSIDevice *d,
                            uint32_t tag, uint32_t lun, void *hba_private);
SCSIRequest *scsi_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun,
                          uint8_t *buf, void *hba_private);
int32_t scsi_req_enqueue(SCSIRequest *req);
SCSIRequest *scsi_req_ref(SCSIRequest *req);
void scsi_req_unref(SCSIRequest *req);

int scsi_bus_parse_cdb(SCSIDevice *dev, SCSICommand *cmd, uint8_t *buf,
                       void *hba_private);
int scsi_req_parse_cdb(SCSIDevice *dev, SCSICommand *cmd, uint8_t *buf);
void scsi_req_build_sense(SCSIRequest *req, SCSISense sense);
void scsi_req_print(SCSIRequest *req);
void scsi_req_continue(SCSIRequest *req);
void scsi_req_data(SCSIRequest *req, int len);
void scsi_req_complete(SCSIRequest *req, int status);
uint8_t *scsi_req_get_buf(SCSIRequest *req);
int scsi_req_get_sense(SCSIRequest *req, uint8_t *buf, int len);
void scsi_req_cancel_complete(SCSIRequest *req);
void scsi_req_cancel(SCSIRequest *req);
void scsi_req_cancel_async(SCSIRequest *req, Notifier *notifier);
void scsi_req_retry(SCSIRequest *req);
void scsi_device_purge_requests(SCSIDevice *sdev, SCSISense sense);
void scsi_device_set_ua(SCSIDevice *sdev, SCSISense sense);
void scsi_device_report_change(SCSIDevice *dev, SCSISense sense);
void scsi_device_unit_attention_reported(SCSIDevice *dev);
void scsi_generic_read_device_inquiry(SCSIDevice *dev);
int scsi_device_get_sense(SCSIDevice *dev, uint8_t *buf, int len, bool fixed);
int scsi_SG_IO_FROM_DEV(BlockBackend *blk, uint8_t *cmd, uint8_t cmd_size,
                        uint8_t *buf, uint8_t buf_size);
SCSIDevice *scsi_device_find(SCSIBus *bus, int channel, int target, int lun);

/* scsi-generic.c. */
extern const SCSIReqOps scsi_generic_req_ops;

#endif
