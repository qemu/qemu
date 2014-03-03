/*
 * Virtio Block Device
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_VIRTIO_BLK_H
#define _QEMU_VIRTIO_BLK_H

#include "hw/virtio/virtio.h"
#include "hw/block/block.h"
#include "sysemu/iothread.h"

#define TYPE_VIRTIO_BLK "virtio-blk-device"
#define VIRTIO_BLK(obj) \
        OBJECT_CHECK(VirtIOBlock, (obj), TYPE_VIRTIO_BLK)

/* from Linux's linux/virtio_blk.h */

/* The ID for virtio_block */
#define VIRTIO_ID_BLOCK 2

/* Feature bits */
#define VIRTIO_BLK_F_BARRIER    0       /* Does host support barriers? */
#define VIRTIO_BLK_F_SIZE_MAX   1       /* Indicates maximum segment size */
#define VIRTIO_BLK_F_SEG_MAX    2       /* Indicates maximum # of segments */
#define VIRTIO_BLK_F_GEOMETRY   4       /* Indicates support of legacy geometry */
#define VIRTIO_BLK_F_RO         5       /* Disk is read-only */
#define VIRTIO_BLK_F_BLK_SIZE   6       /* Block size of disk is available*/
#define VIRTIO_BLK_F_SCSI       7       /* Supports scsi command passthru */
/* #define VIRTIO_BLK_F_IDENTIFY   8       ATA IDENTIFY supported, DEPRECATED */
#define VIRTIO_BLK_F_WCE        9       /* write cache enabled */
#define VIRTIO_BLK_F_TOPOLOGY   10      /* Topology information is available */
#define VIRTIO_BLK_F_CONFIG_WCE 11      /* write cache configurable */

#define VIRTIO_BLK_ID_BYTES     20      /* ID string length */

struct virtio_blk_config
{
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
    uint32_t blk_size;
    uint8_t physical_block_exp;
    uint8_t alignment_offset;
    uint16_t min_io_size;
    uint32_t opt_io_size;
    uint8_t wce;
} QEMU_PACKED;

/* These two define direction. */
#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1

/* This bit says it's a scsi command, not an actual read or write. */
#define VIRTIO_BLK_T_SCSI_CMD   2

/* Flush the volatile write cache */
#define VIRTIO_BLK_T_FLUSH      4

/* return the device ID string */
#define VIRTIO_BLK_T_GET_ID     8

/* Barrier before this op. */
#define VIRTIO_BLK_T_BARRIER    0x80000000

/* This is the first element of the read scatter-gather list. */
struct virtio_blk_outhdr
{
    /* VIRTIO_BLK_T* */
    uint32_t type;
    /* io priority. */
    uint32_t ioprio;
    /* Sector (ie. 512 byte offset) */
    uint64_t sector;
};

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

/* This is the last element of the write scatter-gather list */
struct virtio_blk_inhdr
{
    unsigned char status;
};

/* SCSI pass-through header */
struct virtio_scsi_inhdr
{
    uint32_t errors;
    uint32_t data_len;
    uint32_t sense_len;
    uint32_t residual;
};

struct VirtIOBlkConf
{
    BlockConf conf;
    IOThread *iothread;
    char *serial;
    uint32_t scsi;
    uint32_t config_wce;
    uint32_t data_plane;
};

struct VirtIOBlockDataPlane;

typedef struct VirtIOBlock {
    VirtIODevice parent_obj;
    BlockDriverState *bs;
    VirtQueue *vq;
    void *rq;
    QEMUBH *bh;
    BlockConf *conf;
    VirtIOBlkConf blk;
    unsigned short sector_mask;
    bool original_wce;
    VMChangeStateEntry *change;
#ifdef CONFIG_VIRTIO_BLK_DATA_PLANE
    Notifier migration_state_notifier;
    struct VirtIOBlockDataPlane *dataplane;
#endif
} VirtIOBlock;

#define DEFINE_VIRTIO_BLK_FEATURES(_state, _field) \
        DEFINE_VIRTIO_COMMON_FEATURES(_state, _field)

#ifdef __linux__
#define DEFINE_VIRTIO_BLK_PROPERTIES(_state, _field)                          \
        DEFINE_BLOCK_PROPERTIES(_state, _field.conf),                         \
        DEFINE_BLOCK_CHS_PROPERTIES(_state, _field.conf),                     \
        DEFINE_PROP_STRING("serial", _state, _field.serial),                  \
        DEFINE_PROP_BIT("config-wce", _state, _field.config_wce, 0, true),    \
        DEFINE_PROP_BIT("scsi", _state, _field.scsi, 0, true),                \
        DEFINE_PROP_IOTHREAD("x-iothread", _state, _field.iothread)
#else
#define DEFINE_VIRTIO_BLK_PROPERTIES(_state, _field)                          \
        DEFINE_BLOCK_PROPERTIES(_state, _field.conf),                         \
        DEFINE_BLOCK_CHS_PROPERTIES(_state, _field.conf),                     \
        DEFINE_PROP_STRING("serial", _state, _field.serial),                  \
        DEFINE_PROP_BIT("config-wce", _state, _field.config_wce, 0, true),    \
        DEFINE_PROP_IOTHREAD("x-iothread", _state, _field.iothread)
#endif /* __linux__ */

void virtio_blk_set_conf(DeviceState *dev, VirtIOBlkConf *blk);

#endif
