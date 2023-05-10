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

/* Status byte for guest to report progress, and synchronize features. */
/* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE     1
/* We have found a driver for the device. */
#define VIRTIO_CONFIG_S_DRIVER          2
/* Driver has used its parts of the config, and is happy */
#define VIRTIO_CONFIG_S_DRIVER_OK       4
/* We've given up on this device. */
#define VIRTIO_CONFIG_S_FAILED          0x80

enum VirtioDevType {
    VIRTIO_ID_NET = 1,
    VIRTIO_ID_BLOCK = 2,
    VIRTIO_ID_CONSOLE = 3,
    VIRTIO_ID_BALLOON = 5,
    VIRTIO_ID_SCSI = 8,
};
typedef enum VirtioDevType VirtioDevType;

struct VqInfo {
    uint64_t queue;
    uint32_t align;
    uint16_t index;
    uint16_t num;
} __attribute__((packed));
typedef struct VqInfo VqInfo;

struct VqConfig {
    uint16_t index;
    uint16_t num;
} __attribute__((packed));
typedef struct VqConfig VqConfig;

#define VIRTIO_RING_SIZE            (PAGE_SIZE * 8)
#define VIRTIO_MAX_VQS              3
#define KVM_S390_VIRTIO_RING_ALIGN  4096

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
struct VRingDesc {
    /* Address (guest-physical). */
    uint64_t addr;
    /* Length. */
    uint32_t len;
    /* The flags as indicated above. */
    uint16_t flags;
    /* We chain unused descriptors via this, too */
    uint16_t next;
} __attribute__((packed));
typedef struct VRingDesc VRingDesc;

struct VRingAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));
typedef struct VRingAvail VRingAvail;

/* uint32_t is used here for ids for padding reasons. */
struct VRingUsedElem {
    /* Index of start of used descriptor chain. */
    uint32_t id;
    /* Total length of the descriptor chain which was used (written to) */
    uint32_t len;
} __attribute__((packed));
typedef struct VRingUsedElem VRingUsedElem;

struct VRingUsed {
    uint16_t flags;
    uint16_t idx;
    VRingUsedElem ring[];
} __attribute__((packed));
typedef struct VRingUsed VRingUsed;

struct VRing {
    unsigned int num;
    int next_idx;
    int used_idx;
    VRingDesc *desc;
    VRingAvail *avail;
    VRingUsed *used;
    SubChannelId schid;
    long cookie;
    int id;
};
typedef struct VRing VRing;


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
struct VirtioBlkOuthdr {
        /* VIRTIO_BLK_T* */
        uint32_t type;
        /* io priority. */
        uint32_t ioprio;
        /* Sector (ie. 512 byte offset) */
        uint64_t sector;
};
typedef struct VirtioBlkOuthdr VirtioBlkOuthdr;

struct VirtioBlkConfig {
    uint64_t capacity; /* in 512-byte sectors */
    uint32_t size_max; /* max segment size (if VIRTIO_BLK_F_SIZE_MAX) */
    uint32_t seg_max;  /* max number of segments (if VIRTIO_BLK_F_SEG_MAX) */

    struct VirtioBlkGeometry {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    } geometry; /* (if VIRTIO_BLK_F_GEOMETRY) */

    uint32_t blk_size; /* block size of device (if VIRTIO_BLK_F_BLK_SIZE) */

    /* the next 4 entries are guarded by VIRTIO_BLK_F_TOPOLOGY  */
    uint8_t physical_block_exp; /* exponent for physical blk per logical blk */
    uint8_t alignment_offset;   /* alignment offset in logical blocks */
    uint16_t min_io_size;       /* min I/O size without performance penalty
                              in logical blocks */
    uint32_t opt_io_size;       /* optimal sustained I/O size in logical blks */

    uint8_t wce; /* writeback mode (if VIRTIO_BLK_F_CONFIG_WCE) */
} __attribute__((packed));
typedef struct VirtioBlkConfig VirtioBlkConfig;

enum guessed_disk_nature_type {
    VIRTIO_GDN_NONE     = 0,
    VIRTIO_GDN_DASD     = 1,
    VIRTIO_GDN_CDROM    = 2,
    VIRTIO_GDN_SCSI     = 3,
};
typedef enum guessed_disk_nature_type VirtioGDN;

VirtioGDN virtio_guessed_disk_nature(void);
void virtio_assume_eckd(void);
void virtio_assume_iso9660(void);

bool virtio_ipl_disk_is_valid(void);
int virtio_get_block_size(void);
uint8_t virtio_get_heads(void);
uint8_t virtio_get_sectors(void);
uint64_t virtio_get_blocks(void);
int virtio_read_many(unsigned long sector, void *load_addr, int sec_num);

#define VIRTIO_SECTOR_SIZE 512
#define VIRTIO_ISO_BLOCK_SIZE 2048
#define VIRTIO_SCSI_BLOCK_SIZE 512
#define VIRTIO_DASD_DEFAULT_BLOCK_SIZE 4096

static inline unsigned long virtio_sector_adjust(unsigned long sector)
{
    return sector * (virtio_get_block_size() / VIRTIO_SECTOR_SIZE);
}

struct VirtioScsiConfig {
    uint32_t num_queues;
    uint32_t seg_max;
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
    uint32_t event_info_size;
    uint32_t sense_size;
    uint32_t cdb_size;
    uint16_t max_channel;
    uint16_t max_target;
    uint32_t max_lun;
} __attribute__((packed));
typedef struct VirtioScsiConfig VirtioScsiConfig;

struct ScsiDevice {
    uint16_t channel;   /* Always 0 in QEMU     */
    uint16_t target;    /* will be scanned over */
    uint32_t lun;       /* will be reported     */
};
typedef struct ScsiDevice ScsiDevice;

struct VirtioNetConfig {
    uint8_t mac[6];
    /* uint16_t status; */               /* Only with VIRTIO_NET_F_STATUS */
    /* uint16_t max_virtqueue_pairs; */  /* Only with VIRTIO_NET_F_MQ */
};
typedef struct VirtioNetConfig VirtioNetConfig;

struct VDev {
    int nr_vqs;
    VRing *vrings;
    int cmd_vr_idx;
    void *ring_area;
    long wait_reply_timeout;
    VirtioGDN guessed_disk_nature;
    SubChannelId schid;
    SenseId senseid;
    union {
        VirtioBlkConfig blk;
        VirtioScsiConfig scsi;
        VirtioNetConfig net;
    } config;
    ScsiDevice *scsi_device;
    bool is_cdrom;
    int scsi_block_size;
    int blk_factor;
    uint64_t scsi_last_block;
    uint32_t scsi_dev_cyls;
    uint8_t scsi_dev_heads;
    bool scsi_device_selected;
    ScsiDevice selected_scsi_device;
    uint64_t netboot_start_addr;
    uint32_t max_transfer;
    uint32_t guest_features[2];
};
typedef struct VDev VDev;

VDev *virtio_get_device(void);
VirtioDevType virtio_get_device_type(void);

struct VirtioCmd {
    void *data;
    int size;
    int flags;
};
typedef struct VirtioCmd VirtioCmd;

bool vring_notify(VRing *vr);
int drain_irqs(SubChannelId schid);
void vring_send_buf(VRing *vr, void *p, int len, int flags);
int vr_poll(VRing *vr);
int vring_wait_reply(void);
int virtio_run(VDev *vdev, int vqid, VirtioCmd *cmd);
void virtio_setup_ccw(VDev *vdev);

int virtio_net_init(void *mac_addr);

#endif /* VIRTIO_H */
