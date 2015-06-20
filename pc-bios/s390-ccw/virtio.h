/*
 * Virtio driver bits
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef VIRTIO_H
#define VIRTIO_H

#include "s390-ccw.h"

/* Status byte for guest to report progress, and synchronize features. */
/* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE     1
/* We have found a driver for the device. */
#define VIRTIO_CONFIG_S_DRIVER          2
/* Driver has used its parts of the config, and is happy */
#define VIRTIO_CONFIG_S_DRIVER_OK       4
/* We've given up on this device. */
#define VIRTIO_CONFIG_S_FAILED          0x80

enum virtio_dev_type {
    VIRTIO_ID_NET = 1,
    VIRTIO_ID_BLOCK = 2,
    VIRTIO_ID_CONSOLE = 3,
    VIRTIO_ID_BALLOON = 5,
};

struct virtio_dev_header {
    enum virtio_dev_type type : 8;
    u8  num_vq;
    u8  feature_len;
    u8  config_len;
    u8  status;
    u8  vqconfig[];
} __attribute__((packed));

struct virtio_vqconfig {
    u64 token;
    u64 address;
    u16 num;
    u8  pad[6];
} __attribute__((packed));

struct vq_info_block {
    u64 queue;
    u32 align;
    u16 index;
    u16 num;
} __attribute__((packed));

struct vq_config_block {
    u16 index;
    u16 num;
} __attribute__((packed));

struct virtio_dev {
    struct virtio_dev_header *header;
    struct virtio_vqconfig *vqconfig;
    char *host_features;
    char *guest_features;
    char *config;
};

#define KVM_S390_VIRTIO_RING_ALIGN	4096

#define VRING_USED_F_NO_NOTIFY  1

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT       1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE      2
/* This means the buffer contains a list of buffer descriptors. */
#define VRING_DESC_F_INDIRECT   4

/* Internal flag to mark follow-up segments as such */
#define VRING_HIDDEN_IS_CHAIN   256

/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
struct vring_desc {
    /* Address (guest-physical). */
    u64 addr;
    /* Length. */
    u32 len;
    /* The flags as indicated above. */
    u16 flags;
    /* We chain unused descriptors via this, too */
    u16 next;
} __attribute__((packed));

struct vring_avail {
    u16 flags;
    u16 idx;
    u16 ring[];
} __attribute__((packed));

/* u32 is used here for ids for padding reasons. */
struct vring_used_elem {
    /* Index of start of used descriptor chain. */
    u32 id;
    /* Total length of the descriptor chain which was used (written to) */
    u32 len;
} __attribute__((packed));

struct vring_used {
    u16 flags;
    u16 idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

struct vring {
    unsigned int num;
    int next_idx;
    int used_idx;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    struct subchannel_id schid;
};


/***********************************************
 *               Virtio block                  *
 ***********************************************/

/*
 * Command types
 *
 * Usage is a bit tricky as some bits are used as flags and some are not.
 *
 * Rules:
 *   VIRTIO_BLK_T_OUT may be combined with VIRTIO_BLK_T_SCSI_CMD or
 *   VIRTIO_BLK_T_BARRIER.  VIRTIO_BLK_T_FLUSH is a command of its own
 *   and may not be combined with any of the other flags.
 */

/* These two define direction. */
#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1

/* This bit says it's a scsi command, not an actual read or write. */
#define VIRTIO_BLK_T_SCSI_CMD   2

/* Cache flush command */
#define VIRTIO_BLK_T_FLUSH      4

/* Barrier before this op. */
#define VIRTIO_BLK_T_BARRIER    0x80000000

/* This is the first element of the read scatter-gather list. */
struct virtio_blk_outhdr {
        /* VIRTIO_BLK_T* */
        u32 type;
        /* io priority. */
        u32 ioprio;
        /* Sector (ie. 512 byte offset) */
        u64 sector;
};

#endif /* VIRTIO_H */
