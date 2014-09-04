#ifndef _QEMU_VIRTIO_9P_H
#define _QEMU_VIRTIO_9P_H

#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <utime.h>
#include <sys/resource.h>
#include <glib.h>
#include "hw/virtio/virtio.h"
#include "fsdev/file-op-9p.h"
#include "fsdev/virtio-9p-marshal.h"
#include "qemu/thread.h"
#include "block/coroutine.h"

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
    P9_TRENAMEAT = 74,
    P9_RRENAMEAT,
    P9_TUNLINKAT = 76,
    P9_RUNLINKAT,
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

#define FID_REFERENCED          0x1
#define FID_NON_RECLAIMABLE     0x2
static inline char *rpath(FsContext *ctx, const char *path)
{
    return g_strdup_printf("%s/%s", ctx->fs_root, path);
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
    uint8_t cancelled;
    CoQueue complete;
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

/*
 * Filled by fs driver on open and other
 * calls.
 */
union V9fsFidOpenState {
    int fd;
    DIR *dir;
    V9fsXattr xattr;
    /*
     * private pointer for fs drivers, that
     * have its own internal representation of
     * open files.
     */
    void *private;
};

struct V9fsFidState
{
    int fid_type;
    int32_t fid;
    V9fsPath path;
    V9fsFidOpenState fs;
    V9fsFidOpenState fs_reclaim;
    int flags;
    int open_flags;
    uid_t uid;
    int ref;
    int clunked;
    V9fsFidState *next;
    V9fsFidState *rclm_lst;
};

typedef struct V9fsState
{
    VirtIODevice parent_obj;
    VirtQueue *vq;
    V9fsPDU pdus[MAX_REQ];
    QLIST_HEAD(, V9fsPDU) free_list;
    QLIST_HEAD(, V9fsPDU) active_list;
    V9fsFidState *fid_list;
    FileOperations *ops;
    FsContext ctx;
    char *tag;
    size_t config_size;
    enum p9_proto_version proto_version;
    int32_t msize;
    /*
     * lock ensuring atomic path update
     * on rename.
     */
    CoRwlock rename_lock;
    int32_t root_fid;
    Error *migration_blocker;
    V9fsConf fsconf;
} V9fsState;

typedef struct V9fsStatState {
    V9fsPDU *pdu;
    size_t offset;
    V9fsStat v9stat;
    V9fsFidState *fidp;
    struct stat stbuf;
} V9fsStatState;

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

struct virtio_9p_config
{
    /* number of characters in tag */
    uint16_t tag_len;
    /* Variable size tag name */
    uint8_t tag[0];
} QEMU_PACKED;

typedef struct V9fsMkState {
    V9fsPDU *pdu;
    size_t offset;
    V9fsQID qid;
    struct stat stbuf;
    V9fsString name;
    V9fsString fullname;
} V9fsMkState;

/* 9p2000.L open flags */
#define P9_DOTL_RDONLY        00000000
#define P9_DOTL_WRONLY        00000001
#define P9_DOTL_RDWR          00000002
#define P9_DOTL_NOACCESS      00000003
#define P9_DOTL_CREATE        00000100
#define P9_DOTL_EXCL          00000200
#define P9_DOTL_NOCTTY        00000400
#define P9_DOTL_TRUNC         00001000
#define P9_DOTL_APPEND        00002000
#define P9_DOTL_NONBLOCK      00004000
#define P9_DOTL_DSYNC         00010000
#define P9_DOTL_FASYNC        00020000
#define P9_DOTL_DIRECT        00040000
#define P9_DOTL_LARGEFILE     00100000
#define P9_DOTL_DIRECTORY     00200000
#define P9_DOTL_NOFOLLOW      00400000
#define P9_DOTL_NOATIME       01000000
#define P9_DOTL_CLOEXEC       02000000
#define P9_DOTL_SYNC          04000000

/* 9p2000.L at flags */
#define P9_DOTL_AT_REMOVEDIR         0x200

/* 9P2000.L lock type */
#define P9_LOCK_TYPE_RDLCK 0
#define P9_LOCK_TYPE_WRLCK 1
#define P9_LOCK_TYPE_UNLCK 2

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

extern int open_fd_hw;
extern int total_open_fd;

size_t pdu_packunpack(void *addr, struct iovec *sg, int sg_count,
                      size_t offset, size_t size, int pack);

static inline size_t do_pdu_unpack(void *dst, struct iovec *sg, int sg_count,
                        size_t offset, size_t size)
{
    return pdu_packunpack(dst, sg, sg_count, offset, size, 0);
}

static inline void v9fs_path_write_lock(V9fsState *s)
{
    if (s->ctx.export_flags & V9FS_PATHNAME_FSCONTEXT) {
        qemu_co_rwlock_wrlock(&s->rename_lock);
    }
}

static inline void v9fs_path_read_lock(V9fsState *s)
{
    if (s->ctx.export_flags & V9FS_PATHNAME_FSCONTEXT) {
        qemu_co_rwlock_rdlock(&s->rename_lock);
    }
}

static inline void v9fs_path_unlock(V9fsState *s)
{
    if (s->ctx.export_flags & V9FS_PATHNAME_FSCONTEXT) {
        qemu_co_rwlock_unlock(&s->rename_lock);
    }
}

static inline uint8_t v9fs_request_cancelled(V9fsPDU *pdu)
{
    return pdu->cancelled;
}

extern void handle_9p_output(VirtIODevice *vdev, VirtQueue *vq);
extern void v9fs_reclaim_fd(V9fsPDU *pdu);
extern void v9fs_path_init(V9fsPath *path);
extern void v9fs_path_free(V9fsPath *path);
extern void v9fs_path_copy(V9fsPath *lhs, V9fsPath *rhs);
extern int v9fs_name_to_path(V9fsState *s, V9fsPath *dirpath,
                             const char *name, V9fsPath *path);

#define pdu_marshal(pdu, offset, fmt, args...)  \
    v9fs_marshal(pdu->elem.in_sg, pdu->elem.in_num, offset, 1, fmt, ##args)
#define pdu_unmarshal(pdu, offset, fmt, args...)  \
    v9fs_unmarshal(pdu->elem.out_sg, pdu->elem.out_num, offset, 1, fmt, ##args)

#define TYPE_VIRTIO_9P "virtio-9p-device"
#define VIRTIO_9P(obj) \
        OBJECT_CHECK(V9fsState, (obj), TYPE_VIRTIO_9P)

#define DEFINE_VIRTIO_9P_PROPERTIES(_state, _field)             \
        DEFINE_PROP_STRING("mount_tag", _state, _field.tag),    \
        DEFINE_PROP_STRING("fsdev", _state, _field.fsdev_id)

#endif
