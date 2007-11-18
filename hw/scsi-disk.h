#ifndef SCSI_DISK_H
#define SCSI_DISK_H

/* scsi-disk.c */
enum scsi_reason {
    SCSI_REASON_DONE, /* Command complete.  */
    SCSI_REASON_DATA  /* Transfer complete, more data required.  */
};

typedef struct SCSIDevice SCSIDevice;
typedef void (*scsi_completionfn)(void *opaque, int reason, uint32_t tag,
                                  uint32_t arg);

SCSIDevice *scsi_disk_init(BlockDriverState *bdrv,
                           int tcq,
                           scsi_completionfn completion,
                           void *opaque);
void scsi_disk_destroy(SCSIDevice *s);

int32_t scsi_send_command(SCSIDevice *s, uint32_t tag, uint8_t *buf, int lun);
/* SCSI data transfers are asynchrnonous.  However, unlike the block IO
   layer the completion routine may be called directly by
   scsi_{read,write}_data.  */
void scsi_read_data(SCSIDevice *s, uint32_t tag);
int scsi_write_data(SCSIDevice *s, uint32_t tag);
void scsi_cancel_io(SCSIDevice *s, uint32_t tag);
uint8_t *scsi_get_buf(SCSIDevice *s, uint32_t tag);

/* cdrom.c */
int cdrom_read_toc(int nb_sectors, uint8_t *buf, int msf, int start_track);
int cdrom_read_toc_raw(int nb_sectors, uint8_t *buf, int msf, int session_num);

#endif
