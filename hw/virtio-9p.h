#ifndef _QEMU_VIRTIO_9P_H
#define _QEMU_VIRTIO_9P_H

#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <utime.h>

#include "file-op-9p.h"

/* The feature bitmap for virtio 9P */
/* The mount point is specified in a config variable */
#define VIRTIO_9P_MOUNT_TAG 0

enum {
    P9_TVERSION = 100,
    P9_RVERSION,
    P9_TAUTH = 102,
    P9_RAUTH,
    P9_TATTACH = 104,
    P9_RATTACH,
    P9_TERROR = 106,
    P9_RERROR,
    P9_TFLUSH = 108,
    P9_RFLUSH,
    P9_TWALK = 110,
    P9_RWALK,
    P9_TOPEN = 112,
    P9_ROPEN,
    P9_TCREATE = 114,
    P9_RCREATE,
    P9_TREAD = 116,
    P9_RREAD,
    P9_TWRITE = 118,
    P9_RWRITE,
    P9_TCLUNK = 120,
    P9_RCLUNK,
    P9_TREMOVE = 122,
    P9_RREMOVE,
    P9_TSTAT = 124,
    P9_RSTAT,
    P9_TWSTAT = 126,
    P9_RWSTAT,
};


/* qid.types */
enum {
    P9_QTDIR = 0x80,
    P9_QTAPPEND = 0x40,
    P9_QTEXCL = 0x20,
    P9_QTMOUNT = 0x10,
    P9_QTAUTH = 0x08,
    P9_QTTMP = 0x04,
    P9_QTSYMLINK = 0x02,
    P9_QTLINK = 0x01,
    P9_QTFILE = 0x00,
};

#define P9_NOTAG    (u16)(~0)
#define P9_NOFID    (u32)(~0)
#define P9_MAXWELEM 16

typedef struct V9fsPDU V9fsPDU;

struct V9fsPDU
{
    uint32_t size;
    uint16_t tag;
    uint8_t id;
    VirtQueueElement elem;
    QLIST_ENTRY(V9fsPDU) next;
};


/* FIXME
 * 1) change user needs to set groups and stuff
 */

/* from Linux's linux/virtio_9p.h */

/* The ID for virtio console */
#define VIRTIO_ID_9P    9
#define MAX_REQ         128
#define MAX_TAG_LEN     32

#define BUG_ON(cond) assert(!(cond))

typedef struct V9fsFidState V9fsFidState;

typedef struct V9fsString
{
    int16_t size;
    char *data;
} V9fsString;

typedef struct V9fsQID
{
    int8_t type;
    int32_t version;
    int64_t path;
} V9fsQID;

typedef struct V9fsStat
{
    int16_t size;
    int16_t type;
    int32_t dev;
    V9fsQID qid;
    int32_t mode;
    int32_t atime;
    int32_t mtime;
    int64_t length;
    V9fsString name;
    V9fsString uid;
    V9fsString gid;
    V9fsString muid;
    /* 9p2000.u */
    V9fsString extension;
   int32_t n_uid;
    int32_t n_gid;
    int32_t n_muid;
} V9fsStat;

struct V9fsFidState
{
    int32_t fid;
    V9fsString path;
    int fd;
    DIR *dir;
    uid_t uid;
    V9fsFidState *next;
};

typedef struct V9fsState
{
    VirtIODevice vdev;
    VirtQueue *vq;
    V9fsPDU pdus[MAX_REQ];
    QLIST_HEAD(, V9fsPDU) free_list;
    V9fsFidState *fid_list;
    FileOperations *ops;
    FsContext ctx;
    uint16_t tag_len;
    uint8_t *tag;
    size_t config_size;
} V9fsState;

typedef struct V9fsCreateState {
    V9fsPDU *pdu;
    size_t offset;
    V9fsFidState *fidp;
    V9fsQID qid;
    int32_t perm;
    int8_t mode;
    struct stat stbuf;
    V9fsString name;
    V9fsString extension;
    V9fsString fullname;
} V9fsCreateState;

typedef struct V9fsStatState {
    V9fsPDU *pdu;
    size_t offset;
    V9fsStat v9stat;
    V9fsFidState *fidp;
    struct stat stbuf;
} V9fsStatState;

typedef struct V9fsWalkState {
    V9fsPDU *pdu;
    size_t offset;
    int16_t nwnames;
    int name_idx;
    V9fsQID *qids;
    V9fsFidState *fidp;
    V9fsFidState *newfidp;
    V9fsString path;
    V9fsString *wnames;
    struct stat stbuf;
} V9fsWalkState;

typedef struct V9fsOpenState {
    V9fsPDU *pdu;
    size_t offset;
    int8_t mode;
    V9fsFidState *fidp;
    V9fsQID qid;
    struct stat stbuf;
} V9fsOpenState;

typedef struct V9fsReadState {
    V9fsPDU *pdu;
    size_t offset;
    int32_t count;
    int32_t total;
    int64_t off;
    V9fsFidState *fidp;
    struct iovec iov[128]; /* FIXME: bad, bad, bad */
    struct iovec *sg;
    off_t dir_pos;
    struct dirent *dent;
    struct stat stbuf;
    V9fsString name;
    V9fsStat v9stat;
    int32_t len;
    int32_t cnt;
    int32_t max_count;
} V9fsReadState;

typedef struct V9fsWriteState {
    V9fsPDU *pdu;
    size_t offset;
    int32_t len;
    int32_t count;
    int32_t total;
    int64_t off;
    V9fsFidState *fidp;
    struct iovec iov[128]; /* FIXME: bad, bad, bad */
    struct iovec *sg;
    int cnt;
} V9fsWriteState;

typedef struct V9fsRemoveState {
    V9fsPDU *pdu;
    size_t offset;
    V9fsFidState *fidp;
} V9fsRemoveState;

typedef struct V9fsWstatState
{
    V9fsPDU *pdu;
    size_t offset;
    int16_t unused;
    V9fsStat v9stat;
    V9fsFidState *fidp;
    struct stat stbuf;
    V9fsString nname;
} V9fsWstatState;

struct virtio_9p_config
{
    /* number of characters in tag */
    uint16_t tag_len;
    /* Variable size tag name */
    uint8_t tag[0];
} __attribute__((packed));

extern size_t pdu_packunpack(void *addr, struct iovec *sg, int sg_count,
                            size_t offset, size_t size, int pack);

static inline size_t do_pdu_unpack(void *dst, struct iovec *sg, int sg_count,
                        size_t offset, size_t size)
{
    return pdu_packunpack(dst, sg, sg_count, offset, size, 0);
}

#endif
