/*
 * Virtio-SCSI definitions for s390 machine loader for qemu
 *
 * Copyright 2015 IBM Corp.
 * Author: Eugene "jno" Dvurechenski <jno@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef VIRTIO_SCSI_H
#define VIRTIO_SCSI_H

#include "s390-ccw.h"
#include "virtio.h"
#include "scsi.h"

#define VIRTIO_SCSI_CDB_SIZE   SCSI_DEFAULT_CDB_SIZE
#define VIRTIO_SCSI_SENSE_SIZE SCSI_DEFAULT_SENSE_SIZE

#define VIRTIO_SCSI_MAX_SECTORS 2048

/* command-specific response values */
#define VIRTIO_SCSI_S_OK                     0x00
#define VIRTIO_SCSI_S_BAD_TARGET             0x03

#define QEMU_CDROM_SIGNATURE "QEMU CD-ROM     "

enum virtio_scsi_vq_id {
    VR_CONTROL  = 0,
    VR_EVENT    = 1,
    VR_REQUEST  = 2,
};

struct VirtioScsiCmdReq {
    ScsiLun lun;
    uint64_t id;
    uint8_t task_attr;   /* = 0 = VIRTIO_SCSI_S_SIMPLE */
    uint8_t prio;
    uint8_t crn;         /* = 0 */
    uint8_t cdb[VIRTIO_SCSI_CDB_SIZE];
} __attribute__((packed));
typedef struct VirtioScsiCmdReq VirtioScsiCmdReq;

struct VirtioScsiCmdResp {
        uint32_t sense_len;
        uint32_t residual;
        uint16_t status_qualifier;
        uint8_t status;      /* first check for .response    */
        uint8_t response;    /* then for .status             */
        uint8_t sense[VIRTIO_SCSI_SENSE_SIZE];
} __attribute__((packed));
typedef struct VirtioScsiCmdResp VirtioScsiCmdResp;

static inline const char *virtio_scsi_response_msg(const VirtioScsiCmdResp *r)
{
    static char err_msg[] = "VS RESP=XX";
    uint8_t v = r->response;

    fill_hex_val(err_msg + 8, &v, 1);
    return err_msg;
}

static inline bool virtio_scsi_response_ok(const VirtioScsiCmdResp *r)
{
        return r->response == VIRTIO_SCSI_S_OK && r->status == CDB_STATUS_GOOD;
}

int virtio_scsi_setup(VDev *vdev);
int virtio_scsi_read_many(VDev *vdev,
                          ulong sector, void *load_addr, int sec_num);

#endif /* VIRTIO_SCSI_H */
