#ifndef _QEMU_VIRTIO_9P_H
#define _QEMU_VIRTIO_9P_H

#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <utime.h>
#include "hw/virtio.h"
#include "fsdev/file-op-9p.h"

/* The feature bitmap for virtio 9P */
/* The mount point is specified in a config variable */
#define VIRTIO_9P_MOUNT_TAG 0

enum {
    P9_TLERROR = 6,
    P9_RLERROR,
    P9_TSTATFS = 8,
    P9_RSTATFS,
    P9_TLOPEN = 12,
    P9_RLOPEN,
    P9_TLCREATE = 14,
    P9_RLCREATE,
    P9_TSYMLINK = 16,
    P9_RSYMLINK,
    P9_TMKNOD = 18,
    P9_RMKNOD,
    P9_TRENAME = 20,
    P9_RRENAME,
    P9_TREADLINK = 22,
    P9_RREADLINK,
    P9_TGETATTR = 24,
    P9_RGETATTR,
    P9_TSETATTR = 26,
    P9_RSETATTR,
    P9_TXATTRWALK = 30,
    P9_RXATTRWALK,
    P9_TXATTRCREATE = 32,
    P9_RXATTRCREATE,
    P9_TREADDIR = 40,
    P9_RREADDIR,
    P9_TFSYNC = 50,
    P9_RFSYNC,
    P9_TLOCK = 52,
    P9_RLOCK,
    P9_TGETLOCK = 54,
    P9_RGETLOCK,
    P9_TLINK = 70,
    P9_RLINK,
    P9_TMKDIR = 72,
    P9_RMKDIR,
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

enum p9_proto_version {
    V9FS_PROTO_2000U = 0x01,
    V9FS_PROTO_2000L = 0x02,
};

#define P9_NOTAG    (u16)(~0)
#define P9_NOFID    (u32)(~0)
#define P9_MAXWELEM 16
static inline const char *rpath(FsContext *ctx, const char *path, char *buffer)
{
    snprintf(buffer, PATH_MAX, "%s/%s", ctx->fs_root, path);
    return buffer;
}

/*
 * ample room for Twrite/Rread header
 * size[4] Tread/Twrite tag[2] fid[4] offset[8] count[4]
 */
#define P9_IOHDRSZ 24

typedef struct V9fsPDU V9fsPDU;
struct V9fsState;

struct V9fsPDU
{
    uint32_t size;
    uint16_t tag;
    uint8_t id;
    VirtQueueElement elem;
    struct V9fsState *s;
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

enum {
    P9_FID_NONE = 0,
    P9_FID_FILE,
    P9_FID_DIR,
    P9_FID_XATTR,
};

typedef struct V9fsXattr
{
    int64_t copied_len;
    int64_t len;
    void *value;
    V9fsString name;
    int flags;
} V9fsXattr;

struct V9fsFidState
{
    int fid_type;
    int32_t fid;
    V9fsString path;
    union {
	int fd;
	DIR *dir;
	V9fsXattr xattr;
    } fs;
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
    enum p9_proto_version proto_version;
    int32_t msize;
} V9fsState;

typedef struct V9fsStatState {
    V9fsPDU *pdu;
    size_t offset;
    V9fsStat v9stat;
    V9fsFidState *fidp;
    struct stat stbuf;
} V9fsStatState;

typedef struct V9fsStatDotl {
    uint64_t st_result_mask;
    V9fsQID qid;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_nlink;
    uint64_t st_rdev;
    uint64_t st_size;
    uint64_t st_blksize;
    uint64_t st_blocks;
    uint64_t st_atime_sec;
    uint64_t st_atime_nsec;
    uint64_t st_mtime_sec;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime_sec;
    uint64_t st_ctime_nsec;
    uint64_t st_btime_sec;
    uint64_t st_btime_nsec;
    uint64_t st_gen;
    uint64_t st_data_version;
} V9fsStatDotl;

typedef struct V9fsOpenState {
    V9fsPDU *pdu;
    size_t offset;
    int32_t mode;
    V9fsFidState *fidp;
    V9fsQID qid;
    struct stat stbuf;
    int iounit;
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

typedef struct V9fsIattr
{
    int32_t valid;
    int32_t mode;
    int32_t uid;
    int32_t gid;
    int64_t size;
    int64_t atime_sec;
    int64_t atime_nsec;
    int64_t mtime_sec;
    int64_t mtime_nsec;
} V9fsIattr;

struct virtio_9p_config
{
    /* number of characters in tag */
    uint16_t tag_len;
    /* Variable size tag name */
    uint8_t tag[0];
} __attribute__((packed));

typedef struct V9fsMkState {
    V9fsPDU *pdu;
    size_t offset;
    V9fsQID qid;
    struct stat stbuf;
    V9fsString name;
    V9fsString fullname;
} V9fsMkState;

#define P9_LOCK_SUCCESS 0
#define P9_LOCK_BLOCKED 1
#define P9_LOCK_ERROR 2
#define P9_LOCK_GRACE 3

#define P9_LOCK_FLAGS_BLOCK 1
#define P9_LOCK_FLAGS_RECLAIM 2

typedef struct V9fsFlock
{
    uint8_t type;
    uint32_t flags;
    uint64_t start; /* absolute offset */
    uint64_t length;
    uint32_t proc_id;
    V9fsString client_id;
} V9fsFlock;

typedef struct V9fsGetlock
{
    uint8_t type;
    uint64_t start; /* absolute offset */
    uint64_t length;
    uint32_t proc_id;
    V9fsString client_id;
} V9fsGetlock;

size_t pdu_packunpack(void *addr, struct iovec *sg, int sg_count,
                      size_t offset, size_t size, int pack);

static inline size_t do_pdu_unpack(void *dst, struct iovec *sg, int sg_count,
                        size_t offset, size_t size)
{
    return pdu_packunpack(dst, sg, sg_count, offset, size, 0);
}

extern void handle_9p_output(VirtIODevice *vdev, VirtQueue *vq);
#endif
