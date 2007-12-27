#ifndef SCSI_DISK_H
#define SCSI_DISK_H

/* scsi-disk.c */
enum scsi_reason {
    SCSI_REASON_DONE, /* Command complete.  */
    SCSI_REASON_DATA  /* Transfer complete, more data required.  */
};

typedef struct SCSIDeviceState SCSIDeviceState;
typedef struct SCSIDevice SCSIDevice;
typedef void (*scsi_completionfn)(void *opaque, int reason, uint32_t tag,
                                  uint32_t arg);

struct SCSIDevice
{
    SCSIDeviceState *state;
    void (*destroy)(SCSIDevice *s);
    int32_t (*send_command)(SCSIDevice *s, uint32_t tag, uint8_t *buf,
                            int lun);
    void (*read_data)(SCSIDevice *s, uint32_t tag);
    int (*write_data)(SCSIDevice *s, uint32_t tag);
    void (*cancel_io)(SCSIDevice *s, uint32_t tag);
    uint8_t *(*get_buf)(SCSIDevice *s, uint32_t tag);
};

SCSIDevice *scsi_disk_init(BlockDriverState *bdrv, int tcq,
                           scsi_completionfn completion, void *opaque);
SCSIDevice *scsi_generic_init(BlockDriverState *bdrv, int tcq,
                           scsi_completionfn completion, void *opaque);

/* cdrom.c */
int cdrom_read_toc(int nb_sectors, uint8_t *buf, int msf, int start_track);
int cdrom_read_toc_raw(int nb_sectors, uint8_t *buf, int msf, int session_num);

#endif
