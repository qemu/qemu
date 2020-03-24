/*
 * Copyright (C) 2009-2010 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qemu/uri.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "sysemu/block-backend.h"
#include "qemu/bitops.h"
#include "qemu/cutils.h"
#include "trace.h"

#define SD_PROTO_VER 0x01

#define SD_DEFAULT_ADDR "localhost"
#define SD_DEFAULT_PORT 7000

#define SD_OP_CREATE_AND_WRITE_OBJ  0x01
#define SD_OP_READ_OBJ       0x02
#define SD_OP_WRITE_OBJ      0x03
/* 0x04 is used internally by Sheepdog */

#define SD_OP_NEW_VDI        0x11
#define SD_OP_LOCK_VDI       0x12
#define SD_OP_RELEASE_VDI    0x13
#define SD_OP_GET_VDI_INFO   0x14
#define SD_OP_READ_VDIS      0x15
#define SD_OP_FLUSH_VDI      0x16
#define SD_OP_DEL_VDI        0x17
#define SD_OP_GET_CLUSTER_DEFAULT   0x18

#define SD_FLAG_CMD_WRITE    0x01
#define SD_FLAG_CMD_COW      0x02
#define SD_FLAG_CMD_CACHE    0x04 /* Writeback mode for cache */
#define SD_FLAG_CMD_DIRECT   0x08 /* Don't use cache */

#define SD_RES_SUCCESS       0x00 /* Success */
#define SD_RES_UNKNOWN       0x01 /* Unknown error */
#define SD_RES_NO_OBJ        0x02 /* No object found */
#define SD_RES_EIO           0x03 /* I/O error */
#define SD_RES_VDI_EXIST     0x04 /* Vdi exists already */
#define SD_RES_INVALID_PARMS 0x05 /* Invalid parameters */
#define SD_RES_SYSTEM_ERROR  0x06 /* System error */
#define SD_RES_VDI_LOCKED    0x07 /* Vdi is locked */
#define SD_RES_NO_VDI        0x08 /* No vdi found */
#define SD_RES_NO_BASE_VDI   0x09 /* No base vdi found */
#define SD_RES_VDI_READ      0x0A /* Cannot read requested vdi */
#define SD_RES_VDI_WRITE     0x0B /* Cannot write requested vdi */
#define SD_RES_BASE_VDI_READ 0x0C /* Cannot read base vdi */
#define SD_RES_BASE_VDI_WRITE   0x0D /* Cannot write base vdi */
#define SD_RES_NO_TAG        0x0E /* Requested tag is not found */
#define SD_RES_STARTUP       0x0F /* Sheepdog is on starting up */
#define SD_RES_VDI_NOT_LOCKED   0x10 /* Vdi is not locked */
#define SD_RES_SHUTDOWN      0x11 /* Sheepdog is shutting down */
#define SD_RES_NO_MEM        0x12 /* Cannot allocate memory */
#define SD_RES_FULL_VDI      0x13 /* we already have the maximum vdis */
#define SD_RES_VER_MISMATCH  0x14 /* Protocol version mismatch */
#define SD_RES_NO_SPACE      0x15 /* Server has no room for new objects */
#define SD_RES_WAIT_FOR_FORMAT  0x16 /* Waiting for a format operation */
#define SD_RES_WAIT_FOR_JOIN    0x17 /* Waiting for other nodes joining */
#define SD_RES_JOIN_FAILED   0x18 /* Target node had failed to join sheepdog */
#define SD_RES_HALT          0x19 /* Sheepdog is stopped serving IO request */
#define SD_RES_READONLY      0x1A /* Object is read-only */

/*
 * Object ID rules
 *
 *  0 - 19 (20 bits): data object space
 * 20 - 31 (12 bits): reserved data object space
 * 32 - 55 (24 bits): vdi object space
 * 56 - 59 ( 4 bits): reserved vdi object space
 * 60 - 63 ( 4 bits): object type identifier space
 */

#define VDI_SPACE_SHIFT   32
#define VDI_BIT (UINT64_C(1) << 63)
#define VMSTATE_BIT (UINT64_C(1) << 62)
#define MAX_DATA_OBJS (UINT64_C(1) << 20)
#define MAX_CHILDREN 1024
#define SD_MAX_VDI_LEN 256
#define SD_MAX_VDI_TAG_LEN 256
#define SD_NR_VDIS   (1U << 24)
#define SD_DATA_OBJ_SIZE (UINT64_C(1) << 22)
#define SD_MAX_VDI_SIZE (SD_DATA_OBJ_SIZE * MAX_DATA_OBJS)
#define SD_DEFAULT_BLOCK_SIZE_SHIFT 22
/*
 * For erasure coding, we use at most SD_EC_MAX_STRIP for data strips and
 * (SD_EC_MAX_STRIP - 1) for parity strips
 *
 * SD_MAX_COPIES is sum of number of data strips and parity strips.
 */
#define SD_EC_MAX_STRIP 16
#define SD_MAX_COPIES (SD_EC_MAX_STRIP * 2 - 1)

#define SD_INODE_SIZE (sizeof(SheepdogInode))
#define CURRENT_VDI_ID 0

#define LOCK_TYPE_NORMAL 0
#define LOCK_TYPE_SHARED 1      /* for iSCSI multipath */

typedef struct SheepdogReq {
    uint8_t proto_ver;
    uint8_t opcode;
    uint16_t flags;
    uint32_t epoch;
    uint32_t id;
    uint32_t data_length;
    uint32_t opcode_specific[8];
} SheepdogReq;

typedef struct SheepdogRsp {
    uint8_t proto_ver;
    uint8_t opcode;
    uint16_t flags;
    uint32_t epoch;
    uint32_t id;
    uint32_t data_length;
    uint32_t result;
    uint32_t opcode_specific[7];
} SheepdogRsp;

typedef struct SheepdogObjReq {
    uint8_t proto_ver;
    uint8_t opcode;
    uint16_t flags;
    uint32_t epoch;
    uint32_t id;
    uint32_t data_length;
    uint64_t oid;
    uint64_t cow_oid;
    uint8_t copies;
    uint8_t copy_policy;
    uint8_t reserved[6];
    uint64_t offset;
} SheepdogObjReq;

typedef struct SheepdogObjRsp {
    uint8_t proto_ver;
    uint8_t opcode;
    uint16_t flags;
    uint32_t epoch;
    uint32_t id;
    uint32_t data_length;
    uint32_t result;
    uint8_t copies;
    uint8_t copy_policy;
    uint8_t reserved[2];
    uint32_t pad[6];
} SheepdogObjRsp;

typedef struct SheepdogVdiReq {
    uint8_t proto_ver;
    uint8_t opcode;
    uint16_t flags;
    uint32_t epoch;
    uint32_t id;
    uint32_t data_length;
    uint64_t vdi_size;
    uint32_t base_vdi_id;
    uint8_t copies;
    uint8_t copy_policy;
    uint8_t store_policy;
    uint8_t block_size_shift;
    uint32_t snapid;
    uint32_t type;
    uint32_t pad[2];
} SheepdogVdiReq;

typedef struct SheepdogVdiRsp {
    uint8_t proto_ver;
    uint8_t opcode;
    uint16_t flags;
    uint32_t epoch;
    uint32_t id;
    uint32_t data_length;
    uint32_t result;
    uint32_t rsvd;
    uint32_t vdi_id;
    uint32_t pad[5];
} SheepdogVdiRsp;

typedef struct SheepdogClusterRsp {
    uint8_t proto_ver;
    uint8_t opcode;
    uint16_t flags;
    uint32_t epoch;
    uint32_t id;
    uint32_t data_length;
    uint32_t result;
    uint8_t nr_copies;
    uint8_t copy_policy;
    uint8_t block_size_shift;
    uint8_t __pad1;
    uint32_t __pad2[6];
} SheepdogClusterRsp;

typedef struct SheepdogInode {
    char name[SD_MAX_VDI_LEN];
    char tag[SD_MAX_VDI_TAG_LEN];
    uint64_t ctime;
    uint64_t snap_ctime;
    uint64_t vm_clock_nsec;
    uint64_t vdi_size;
    uint64_t vm_state_size;
    uint16_t copy_policy;
    uint8_t nr_copies;
    uint8_t block_size_shift;
    uint32_t snap_id;
    uint32_t vdi_id;
    uint32_t parent_vdi_id;
    uint32_t child_vdi_id[MAX_CHILDREN];
    uint32_t data_vdi_id[MAX_DATA_OBJS];
} SheepdogInode;

#define SD_INODE_HEADER_SIZE offsetof(SheepdogInode, data_vdi_id)

/*
 * 64 bit FNV-1a non-zero initial basis
 */
#define FNV1A_64_INIT ((uint64_t)0xcbf29ce484222325ULL)

/*
 * 64 bit Fowler/Noll/Vo FNV-1a hash code
 */
static inline uint64_t fnv_64a_buf(void *buf, size_t len, uint64_t hval)
{
    unsigned char *bp = buf;
    unsigned char *be = bp + len;
    while (bp < be) {
        hval ^= (uint64_t) *bp++;
        hval += (hval << 1) + (hval << 4) + (hval << 5) +
            (hval << 7) + (hval << 8) + (hval << 40);
    }
    return hval;
}

static inline bool is_data_obj_writable(SheepdogInode *inode, unsigned int idx)
{
    return inode->vdi_id == inode->data_vdi_id[idx];
}

static inline bool is_data_obj(uint64_t oid)
{
    return !(VDI_BIT & oid);
}

static inline uint64_t data_oid_to_idx(uint64_t oid)
{
    return oid & (MAX_DATA_OBJS - 1);
}

static inline uint32_t oid_to_vid(uint64_t oid)
{
    return (oid & ~VDI_BIT) >> VDI_SPACE_SHIFT;
}

static inline uint64_t vid_to_vdi_oid(uint32_t vid)
{
    return VDI_BIT | ((uint64_t)vid << VDI_SPACE_SHIFT);
}

static inline uint64_t vid_to_vmstate_oid(uint32_t vid, uint32_t idx)
{
    return VMSTATE_BIT | ((uint64_t)vid << VDI_SPACE_SHIFT) | idx;
}

static inline uint64_t vid_to_data_oid(uint32_t vid, uint32_t idx)
{
    return ((uint64_t)vid << VDI_SPACE_SHIFT) | idx;
}

static inline bool is_snapshot(struct SheepdogInode *inode)
{
    return !!inode->snap_ctime;
}

static inline size_t count_data_objs(const struct SheepdogInode *inode)
{
    return DIV_ROUND_UP(inode->vdi_size,
                        (1UL << inode->block_size_shift));
}

typedef struct SheepdogAIOCB SheepdogAIOCB;
typedef struct BDRVSheepdogState BDRVSheepdogState;

typedef struct AIOReq {
    SheepdogAIOCB *aiocb;
    unsigned int iov_offset;

    uint64_t oid;
    uint64_t base_oid;
    uint64_t offset;
    unsigned int data_len;
    uint8_t flags;
    uint32_t id;
    bool create;

    QLIST_ENTRY(AIOReq) aio_siblings;
} AIOReq;

enum AIOCBState {
    AIOCB_WRITE_UDATA,
    AIOCB_READ_UDATA,
    AIOCB_FLUSH_CACHE,
    AIOCB_DISCARD_OBJ,
};

#define AIOCBOverlapping(x, y)                                 \
    (!(x->max_affect_data_idx < y->min_affect_data_idx          \
       || y->max_affect_data_idx < x->min_affect_data_idx))

struct SheepdogAIOCB {
    BDRVSheepdogState *s;

    QEMUIOVector *qiov;

    int64_t sector_num;
    int nb_sectors;

    int ret;
    enum AIOCBState aiocb_type;

    Coroutine *coroutine;
    int nr_pending;

    uint32_t min_affect_data_idx;
    uint32_t max_affect_data_idx;

    /*
     * The difference between affect_data_idx and dirty_data_idx:
     * affect_data_idx represents range of index of all request types.
     * dirty_data_idx represents range of index updated by COW requests.
     * dirty_data_idx is used for updating an inode object.
     */
    uint32_t min_dirty_data_idx;
    uint32_t max_dirty_data_idx;

    QLIST_ENTRY(SheepdogAIOCB) aiocb_siblings;
};

struct BDRVSheepdogState {
    BlockDriverState *bs;
    AioContext *aio_context;

    SheepdogInode inode;

    char name[SD_MAX_VDI_LEN];
    bool is_snapshot;
    uint32_t cache_flags;
    bool discard_supported;

    SocketAddress *addr;
    int fd;

    CoMutex lock;
    Coroutine *co_send;
    Coroutine *co_recv;

    uint32_t aioreq_seq_num;

    /* Every aio request must be linked to either of these queues. */
    QLIST_HEAD(, AIOReq) inflight_aio_head;
    QLIST_HEAD(, AIOReq) failed_aio_head;

    CoMutex queue_lock;
    CoQueue overlapping_queue;
    QLIST_HEAD(, SheepdogAIOCB) inflight_aiocb_head;
};

typedef struct BDRVSheepdogReopenState {
    int fd;
    int cache_flags;
} BDRVSheepdogReopenState;

static const char *sd_strerror(int err)
{
    int i;

    static const struct {
        int err;
        const char *desc;
    } errors[] = {
        {SD_RES_SUCCESS, "Success"},
        {SD_RES_UNKNOWN, "Unknown error"},
        {SD_RES_NO_OBJ, "No object found"},
        {SD_RES_EIO, "I/O error"},
        {SD_RES_VDI_EXIST, "VDI exists already"},
        {SD_RES_INVALID_PARMS, "Invalid parameters"},
        {SD_RES_SYSTEM_ERROR, "System error"},
        {SD_RES_VDI_LOCKED, "VDI is already locked"},
        {SD_RES_NO_VDI, "No vdi found"},
        {SD_RES_NO_BASE_VDI, "No base VDI found"},
        {SD_RES_VDI_READ, "Failed read the requested VDI"},
        {SD_RES_VDI_WRITE, "Failed to write the requested VDI"},
        {SD_RES_BASE_VDI_READ, "Failed to read the base VDI"},
        {SD_RES_BASE_VDI_WRITE, "Failed to write the base VDI"},
        {SD_RES_NO_TAG, "Failed to find the requested tag"},
        {SD_RES_STARTUP, "The system is still booting"},
        {SD_RES_VDI_NOT_LOCKED, "VDI isn't locked"},
        {SD_RES_SHUTDOWN, "The system is shutting down"},
        {SD_RES_NO_MEM, "Out of memory on the server"},
        {SD_RES_FULL_VDI, "We already have the maximum vdis"},
        {SD_RES_VER_MISMATCH, "Protocol version mismatch"},
        {SD_RES_NO_SPACE, "Server has no space for new objects"},
        {SD_RES_WAIT_FOR_FORMAT, "Sheepdog is waiting for a format operation"},
        {SD_RES_WAIT_FOR_JOIN, "Sheepdog is waiting for other nodes joining"},
        {SD_RES_JOIN_FAILED, "Target node had failed to join sheepdog"},
        {SD_RES_HALT, "Sheepdog is stopped serving IO request"},
        {SD_RES_READONLY, "Object is read-only"},
    };

    for (i = 0; i < ARRAY_SIZE(errors); ++i) {
        if (errors[i].err == err) {
            return errors[i].desc;
        }
    }

    return "Invalid error code";
}

/*
 * Sheepdog I/O handling:
 *
 * 1. In sd_co_rw_vector, we send the I/O requests to the server and
 *    link the requests to the inflight_list in the
 *    BDRVSheepdogState.  The function yields while waiting for
 *    receiving the response.
 *
 * 2. We receive the response in aio_read_response, the fd handler to
 *    the sheepdog connection.  We switch back to sd_co_readv/sd_writev
 *    after all the requests belonging to the AIOCB are finished.  If
 *    needed, sd_co_writev will send another requests for the vdi object.
 */

static inline AIOReq *alloc_aio_req(BDRVSheepdogState *s, SheepdogAIOCB *acb,
                                    uint64_t oid, unsigned int data_len,
                                    uint64_t offset, uint8_t flags, bool create,
                                    uint64_t base_oid, unsigned int iov_offset)
{
    AIOReq *aio_req;

    aio_req = g_malloc(sizeof(*aio_req));
    aio_req->aiocb = acb;
    aio_req->iov_offset = iov_offset;
    aio_req->oid = oid;
    aio_req->base_oid = base_oid;
    aio_req->offset = offset;
    aio_req->data_len = data_len;
    aio_req->flags = flags;
    aio_req->id = s->aioreq_seq_num++;
    aio_req->create = create;

    acb->nr_pending++;
    return aio_req;
}

static void wait_for_overlapping_aiocb(BDRVSheepdogState *s, SheepdogAIOCB *acb)
{
    SheepdogAIOCB *cb;

retry:
    QLIST_FOREACH(cb, &s->inflight_aiocb_head, aiocb_siblings) {
        if (AIOCBOverlapping(acb, cb)) {
            qemu_co_queue_wait(&s->overlapping_queue, &s->queue_lock);
            goto retry;
        }
    }
}

static void sd_aio_setup(SheepdogAIOCB *acb, BDRVSheepdogState *s,
                         QEMUIOVector *qiov, int64_t sector_num, int nb_sectors,
                         int type)
{
    uint32_t object_size;

    object_size = (UINT32_C(1) << s->inode.block_size_shift);

    acb->s = s;

    acb->qiov = qiov;

    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;

    acb->coroutine = qemu_coroutine_self();
    acb->ret = 0;
    acb->nr_pending = 0;

    acb->min_affect_data_idx = acb->sector_num * BDRV_SECTOR_SIZE / object_size;
    acb->max_affect_data_idx = (acb->sector_num * BDRV_SECTOR_SIZE +
                              acb->nb_sectors * BDRV_SECTOR_SIZE) / object_size;

    acb->min_dirty_data_idx = UINT32_MAX;
    acb->max_dirty_data_idx = 0;
    acb->aiocb_type = type;

    if (type == AIOCB_FLUSH_CACHE) {
        return;
    }

    qemu_co_mutex_lock(&s->queue_lock);
    wait_for_overlapping_aiocb(s, acb);
    QLIST_INSERT_HEAD(&s->inflight_aiocb_head, acb, aiocb_siblings);
    qemu_co_mutex_unlock(&s->queue_lock);
}

static SocketAddress *sd_server_config(QDict *options, Error **errp)
{
    QDict *server = NULL;
    Visitor *iv = NULL;
    SocketAddress *saddr = NULL;
    Error *local_err = NULL;

    qdict_extract_subqdict(options, &server, "server.");

    iv = qobject_input_visitor_new_flat_confused(server, errp);
    if (!iv) {
        goto done;
    }

    visit_type_SocketAddress(iv, NULL, &saddr, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto done;
    }

done:
    visit_free(iv);
    qobject_unref(server);
    return saddr;
}

/* Return -EIO in case of error, file descriptor on success */
static int connect_to_sdog(BDRVSheepdogState *s, Error **errp)
{
    int fd;

    fd = socket_connect(s->addr, errp);

    if (s->addr->type == SOCKET_ADDRESS_TYPE_INET && fd >= 0) {
        int ret = socket_set_nodelay(fd);
        if (ret < 0) {
            warn_report("can't set TCP_NODELAY: %s", strerror(errno));
        }
    }

    if (fd >= 0) {
        qemu_set_nonblock(fd);
    } else {
        fd = -EIO;
    }

    return fd;
}

/* Return 0 on success and -errno in case of error */
static coroutine_fn int send_co_req(int sockfd, SheepdogReq *hdr, void *data,
                                    unsigned int *wlen)
{
    int ret;

    ret = qemu_co_send(sockfd, hdr, sizeof(*hdr));
    if (ret != sizeof(*hdr)) {
        error_report("failed to send a req, %s", strerror(errno));
        return -errno;
    }

    ret = qemu_co_send(sockfd, data, *wlen);
    if (ret != *wlen) {
        error_report("failed to send a req, %s", strerror(errno));
        return -errno;
    }

    return ret;
}

typedef struct SheepdogReqCo {
    int sockfd;
    BlockDriverState *bs;
    AioContext *aio_context;
    SheepdogReq *hdr;
    void *data;
    unsigned int *wlen;
    unsigned int *rlen;
    int ret;
    bool finished;
    Coroutine *co;
} SheepdogReqCo;

static void restart_co_req(void *opaque)
{
    SheepdogReqCo *srco = opaque;

    aio_co_wake(srco->co);
}

static coroutine_fn void do_co_req(void *opaque)
{
    int ret;
    SheepdogReqCo *srco = opaque;
    int sockfd = srco->sockfd;
    SheepdogReq *hdr = srco->hdr;
    void *data = srco->data;
    unsigned int *wlen = srco->wlen;
    unsigned int *rlen = srco->rlen;

    srco->co = qemu_coroutine_self();
    aio_set_fd_handler(srco->aio_context, sockfd, false,
                       NULL, restart_co_req, NULL, srco);

    ret = send_co_req(sockfd, hdr, data, wlen);
    if (ret < 0) {
        goto out;
    }

    aio_set_fd_handler(srco->aio_context, sockfd, false,
                       restart_co_req, NULL, NULL, srco);

    ret = qemu_co_recv(sockfd, hdr, sizeof(*hdr));
    if (ret != sizeof(*hdr)) {
        error_report("failed to get a rsp, %s", strerror(errno));
        ret = -errno;
        goto out;
    }

    if (*rlen > hdr->data_length) {
        *rlen = hdr->data_length;
    }

    if (*rlen) {
        ret = qemu_co_recv(sockfd, data, *rlen);
        if (ret != *rlen) {
            error_report("failed to get the data, %s", strerror(errno));
            ret = -errno;
            goto out;
        }
    }
    ret = 0;
out:
    /* there is at most one request for this sockfd, so it is safe to
     * set each handler to NULL. */
    aio_set_fd_handler(srco->aio_context, sockfd, false,
                       NULL, NULL, NULL, NULL);

    srco->co = NULL;
    srco->ret = ret;
    /* Set srco->finished before reading bs->wakeup.  */
    atomic_mb_set(&srco->finished, true);
    if (srco->bs) {
        bdrv_wakeup(srco->bs);
    }
}

/*
 * Send the request to the sheep in a synchronous manner.
 *
 * Return 0 on success, -errno in case of error.
 */
static int do_req(int sockfd, BlockDriverState *bs, SheepdogReq *hdr,
                  void *data, unsigned int *wlen, unsigned int *rlen)
{
    Coroutine *co;
    SheepdogReqCo srco = {
        .sockfd = sockfd,
        .aio_context = bs ? bdrv_get_aio_context(bs) : qemu_get_aio_context(),
        .bs = bs,
        .hdr = hdr,
        .data = data,
        .wlen = wlen,
        .rlen = rlen,
        .ret = 0,
        .finished = false,
    };

    if (qemu_in_coroutine()) {
        do_co_req(&srco);
    } else {
        co = qemu_coroutine_create(do_co_req, &srco);
        if (bs) {
            bdrv_coroutine_enter(bs, co);
            BDRV_POLL_WHILE(bs, !srco.finished);
        } else {
            qemu_coroutine_enter(co);
            while (!srco.finished) {
                aio_poll(qemu_get_aio_context(), true);
            }
        }
    }

    return srco.ret;
}

static void coroutine_fn add_aio_request(BDRVSheepdogState *s, AIOReq *aio_req,
                                         struct iovec *iov, int niov,
                                         enum AIOCBState aiocb_type);
static void coroutine_fn resend_aioreq(BDRVSheepdogState *s, AIOReq *aio_req);
static int reload_inode(BDRVSheepdogState *s, uint32_t snapid, const char *tag);
static int get_sheep_fd(BDRVSheepdogState *s, Error **errp);
static void co_write_request(void *opaque);

static coroutine_fn void reconnect_to_sdog(void *opaque)
{
    BDRVSheepdogState *s = opaque;
    AIOReq *aio_req, *next;

    aio_set_fd_handler(s->aio_context, s->fd, false, NULL,
                       NULL, NULL, NULL);
    close(s->fd);
    s->fd = -1;

    /* Wait for outstanding write requests to be completed. */
    while (s->co_send != NULL) {
        co_write_request(opaque);
    }

    /* Try to reconnect the sheepdog server every one second. */
    while (s->fd < 0) {
        Error *local_err = NULL;
        s->fd = get_sheep_fd(s, &local_err);
        if (s->fd < 0) {
            trace_sheepdog_reconnect_to_sdog();
            error_report_err(local_err);
            qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, 1000000000ULL);
        }
    };

    /*
     * Now we have to resend all the request in the inflight queue.  However,
     * resend_aioreq() can yield and newly created requests can be added to the
     * inflight queue before the coroutine is resumed.  To avoid mixing them, we
     * have to move all the inflight requests to the failed queue before
     * resend_aioreq() is called.
     */
    qemu_co_mutex_lock(&s->queue_lock);
    QLIST_FOREACH_SAFE(aio_req, &s->inflight_aio_head, aio_siblings, next) {
        QLIST_REMOVE(aio_req, aio_siblings);
        QLIST_INSERT_HEAD(&s->failed_aio_head, aio_req, aio_siblings);
    }

    /* Resend all the failed aio requests. */
    while (!QLIST_EMPTY(&s->failed_aio_head)) {
        aio_req = QLIST_FIRST(&s->failed_aio_head);
        QLIST_REMOVE(aio_req, aio_siblings);
        qemu_co_mutex_unlock(&s->queue_lock);
        resend_aioreq(s, aio_req);
        qemu_co_mutex_lock(&s->queue_lock);
    }
    qemu_co_mutex_unlock(&s->queue_lock);
}

/*
 * Receive responses of the I/O requests.
 *
 * This function is registered as a fd handler, and called from the
 * main loop when s->fd is ready for reading responses.
 */
static void coroutine_fn aio_read_response(void *opaque)
{
    SheepdogObjRsp rsp;
    BDRVSheepdogState *s = opaque;
    int fd = s->fd;
    int ret;
    AIOReq *aio_req = NULL;
    SheepdogAIOCB *acb;
    uint64_t idx;

    /* read a header */
    ret = qemu_co_recv(fd, &rsp, sizeof(rsp));
    if (ret != sizeof(rsp)) {
        error_report("failed to get the header, %s", strerror(errno));
        goto err;
    }

    /* find the right aio_req from the inflight aio list */
    QLIST_FOREACH(aio_req, &s->inflight_aio_head, aio_siblings) {
        if (aio_req->id == rsp.id) {
            break;
        }
    }
    if (!aio_req) {
        error_report("cannot find aio_req %x", rsp.id);
        goto err;
    }

    acb = aio_req->aiocb;

    switch (acb->aiocb_type) {
    case AIOCB_WRITE_UDATA:
        if (!is_data_obj(aio_req->oid)) {
            break;
        }
        idx = data_oid_to_idx(aio_req->oid);

        if (aio_req->create) {
            /*
             * If the object is newly created one, we need to update
             * the vdi object (metadata object).  min_dirty_data_idx
             * and max_dirty_data_idx are changed to include updated
             * index between them.
             */
            if (rsp.result == SD_RES_SUCCESS) {
                s->inode.data_vdi_id[idx] = s->inode.vdi_id;
                acb->max_dirty_data_idx = MAX(idx, acb->max_dirty_data_idx);
                acb->min_dirty_data_idx = MIN(idx, acb->min_dirty_data_idx);
            }
        }
        break;
    case AIOCB_READ_UDATA:
        ret = qemu_co_recvv(fd, acb->qiov->iov, acb->qiov->niov,
                            aio_req->iov_offset, rsp.data_length);
        if (ret != rsp.data_length) {
            error_report("failed to get the data, %s", strerror(errno));
            goto err;
        }
        break;
    case AIOCB_FLUSH_CACHE:
        if (rsp.result == SD_RES_INVALID_PARMS) {
            trace_sheepdog_aio_read_response();
            s->cache_flags = SD_FLAG_CMD_DIRECT;
            rsp.result = SD_RES_SUCCESS;
        }
        break;
    case AIOCB_DISCARD_OBJ:
        switch (rsp.result) {
        case SD_RES_INVALID_PARMS:
            error_report("server doesn't support discard command");
            rsp.result = SD_RES_SUCCESS;
            s->discard_supported = false;
            break;
        default:
            break;
        }
    }

    /* No more data for this aio_req (reload_inode below uses its own file
     * descriptor handler which doesn't use co_recv).
    */
    s->co_recv = NULL;

    qemu_co_mutex_lock(&s->queue_lock);
    QLIST_REMOVE(aio_req, aio_siblings);
    qemu_co_mutex_unlock(&s->queue_lock);

    switch (rsp.result) {
    case SD_RES_SUCCESS:
        break;
    case SD_RES_READONLY:
        if (s->inode.vdi_id == oid_to_vid(aio_req->oid)) {
            ret = reload_inode(s, 0, "");
            if (ret < 0) {
                goto err;
            }
        }
        if (is_data_obj(aio_req->oid)) {
            aio_req->oid = vid_to_data_oid(s->inode.vdi_id,
                                           data_oid_to_idx(aio_req->oid));
        } else {
            aio_req->oid = vid_to_vdi_oid(s->inode.vdi_id);
        }
        resend_aioreq(s, aio_req);
        return;
    default:
        acb->ret = -EIO;
        error_report("%s", sd_strerror(rsp.result));
        break;
    }

    g_free(aio_req);

    if (!--acb->nr_pending) {
        /*
         * We've finished all requests which belong to the AIOCB, so
         * we can switch back to sd_co_readv/writev now.
         */
        aio_co_wake(acb->coroutine);
    }

    return;

err:
    reconnect_to_sdog(opaque);
}

static void co_read_response(void *opaque)
{
    BDRVSheepdogState *s = opaque;

    if (!s->co_recv) {
        s->co_recv = qemu_coroutine_create(aio_read_response, opaque);
    }

    aio_co_enter(s->aio_context, s->co_recv);
}

static void co_write_request(void *opaque)
{
    BDRVSheepdogState *s = opaque;

    aio_co_wake(s->co_send);
}

/*
 * Return a socket descriptor to read/write objects.
 *
 * We cannot use this descriptor for other operations because
 * the block driver may be on waiting response from the server.
 */
static int get_sheep_fd(BDRVSheepdogState *s, Error **errp)
{
    int fd;

    fd = connect_to_sdog(s, errp);
    if (fd < 0) {
        return fd;
    }

    aio_set_fd_handler(s->aio_context, fd, false,
                       co_read_response, NULL, NULL, s);
    return fd;
}

/*
 * Parse numeric snapshot ID in @str
 * If @str can't be parsed as number, return false.
 * Else, if the number is zero or too large, set *@snapid to zero and
 * return true.
 * Else, set *@snapid to the number and return true.
 */
static bool sd_parse_snapid(const char *str, uint32_t *snapid)
{
    unsigned long ul;
    int ret;

    ret = qemu_strtoul(str, NULL, 10, &ul);
    if (ret == -ERANGE) {
        ul = ret = 0;
    }
    if (ret) {
        return false;
    }
    if (ul > UINT32_MAX) {
        ul = 0;
    }

    *snapid = ul;
    return true;
}

static bool sd_parse_snapid_or_tag(const char *str,
                                   uint32_t *snapid, char tag[])
{
    if (!sd_parse_snapid(str, snapid)) {
        *snapid = 0;
        if (g_strlcpy(tag, str, SD_MAX_VDI_TAG_LEN) >= SD_MAX_VDI_TAG_LEN) {
            return false;
        }
    } else if (!*snapid) {
        return false;
    } else {
        tag[0] = 0;
    }
    return true;
}

typedef struct {
    const char *path;           /* non-null iff transport is tcp */
    const char *host;           /* valid when transport is tcp */
    int port;                   /* valid when transport is tcp */
    char vdi[SD_MAX_VDI_LEN];
    char tag[SD_MAX_VDI_TAG_LEN];
    uint32_t snap_id;
    /* Remainder is only for sd_config_done() */
    URI *uri;
    QueryParams *qp;
} SheepdogConfig;

static void sd_config_done(SheepdogConfig *cfg)
{
    if (cfg->qp) {
        query_params_free(cfg->qp);
    }
    uri_free(cfg->uri);
}

static void sd_parse_uri(SheepdogConfig *cfg, const char *filename,
                         Error **errp)
{
    Error *err = NULL;
    QueryParams *qp = NULL;
    bool is_unix;
    URI *uri;

    memset(cfg, 0, sizeof(*cfg));

    cfg->uri = uri = uri_parse(filename);
    if (!uri) {
        error_setg(&err, "invalid URI '%s'", filename);
        goto out;
    }

    /* transport */
    if (!g_strcmp0(uri->scheme, "sheepdog")) {
        is_unix = false;
    } else if (!g_strcmp0(uri->scheme, "sheepdog+tcp")) {
        is_unix = false;
    } else if (!g_strcmp0(uri->scheme, "sheepdog+unix")) {
        is_unix = true;
    } else {
        error_setg(&err, "URI scheme must be 'sheepdog', 'sheepdog+tcp',"
                   " or 'sheepdog+unix'");
        goto out;
    }

    if (uri->path == NULL || !strcmp(uri->path, "/")) {
        error_setg(&err, "missing file path in URI");
        goto out;
    }
    if (g_strlcpy(cfg->vdi, uri->path + 1, SD_MAX_VDI_LEN)
        >= SD_MAX_VDI_LEN) {
        error_setg(&err, "VDI name is too long");
        goto out;
    }

    cfg->qp = qp = query_params_parse(uri->query);

    if (is_unix) {
        /* sheepdog+unix:///vdiname?socket=path */
        if (uri->server || uri->port) {
            error_setg(&err, "URI scheme %s doesn't accept a server address",
                       uri->scheme);
            goto out;
        }
        if (!qp->n) {
            error_setg(&err,
                       "URI scheme %s requires query parameter 'socket'",
                       uri->scheme);
            goto out;
        }
        if (qp->n != 1 || strcmp(qp->p[0].name, "socket")) {
            error_setg(&err, "unexpected query parameters");
            goto out;
        }
        cfg->path = qp->p[0].value;
    } else {
        /* sheepdog[+tcp]://[host:port]/vdiname */
        if (qp->n) {
            error_setg(&err, "unexpected query parameters");
            goto out;
        }
        cfg->host = uri->server;
        cfg->port = uri->port;
    }

    /* snapshot tag */
    if (uri->fragment) {
        if (!sd_parse_snapid_or_tag(uri->fragment,
                                    &cfg->snap_id, cfg->tag)) {
            error_setg(&err, "'%s' is not a valid snapshot ID",
                       uri->fragment);
            goto out;
        }
    } else {
        cfg->snap_id = CURRENT_VDI_ID; /* search current vdi */
    }

out:
    if (err) {
        error_propagate(errp, err);
        sd_config_done(cfg);
    }
}

/*
 * Parse a filename (old syntax)
 *
 * filename must be one of the following formats:
 *   1. [vdiname]
 *   2. [vdiname]:[snapid]
 *   3. [vdiname]:[tag]
 *   4. [hostname]:[port]:[vdiname]
 *   5. [hostname]:[port]:[vdiname]:[snapid]
 *   6. [hostname]:[port]:[vdiname]:[tag]
 *
 * You can boot from the snapshot images by specifying `snapid` or
 * `tag'.
 *
 * You can run VMs outside the Sheepdog cluster by specifying
 * `hostname' and `port' (experimental).
 */
static void parse_vdiname(SheepdogConfig *cfg, const char *filename,
                          Error **errp)
{
    Error *err = NULL;
    char *p, *q, *uri;
    const char *host_spec, *vdi_spec;
    int nr_sep;

    strstart(filename, "sheepdog:", &filename);
    p = q = g_strdup(filename);

    /* count the number of separators */
    nr_sep = 0;
    while (*p) {
        if (*p == ':') {
            nr_sep++;
        }
        p++;
    }
    p = q;

    /* use the first two tokens as host_spec. */
    if (nr_sep >= 2) {
        host_spec = p;
        p = strchr(p, ':');
        p++;
        p = strchr(p, ':');
        *p++ = '\0';
    } else {
        host_spec = "";
    }

    vdi_spec = p;

    p = strchr(vdi_spec, ':');
    if (p) {
        *p++ = '#';
    }

    uri = g_strdup_printf("sheepdog://%s/%s", host_spec, vdi_spec);

    /*
     * FIXME We to escape URI meta-characters, e.g. "x?y=z"
     * produces "sheepdog://x?y=z".  Because of that ...
     */
    sd_parse_uri(cfg, uri, &err);
    if (err) {
        /*
         * ... this can fail, but the error message is misleading.
         * Replace it by the traditional useless one until the
         * escaping is fixed.
         */
        error_free(err);
        error_setg(errp, "Can't parse filename");
    }

    g_free(q);
    g_free(uri);
}

static void sd_parse_filename(const char *filename, QDict *options,
                              Error **errp)
{
    Error *err = NULL;
    SheepdogConfig cfg;
    char buf[32];

    if (strstr(filename, "://")) {
        sd_parse_uri(&cfg, filename, &err);
    } else {
        parse_vdiname(&cfg, filename, &err);
    }
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (cfg.path) {
        qdict_set_default_str(options, "server.path", cfg.path);
        qdict_set_default_str(options, "server.type", "unix");
    } else {
        qdict_set_default_str(options, "server.type", "inet");
        qdict_set_default_str(options, "server.host",
                              cfg.host ?: SD_DEFAULT_ADDR);
        snprintf(buf, sizeof(buf), "%d", cfg.port ?: SD_DEFAULT_PORT);
        qdict_set_default_str(options, "server.port", buf);
    }
    qdict_set_default_str(options, "vdi", cfg.vdi);
    qdict_set_default_str(options, "tag", cfg.tag);
    if (cfg.snap_id) {
        snprintf(buf, sizeof(buf), "%d", cfg.snap_id);
        qdict_set_default_str(options, "snap-id", buf);
    }

    sd_config_done(&cfg);
}

static int find_vdi_name(BDRVSheepdogState *s, const char *filename,
                         uint32_t snapid, const char *tag, uint32_t *vid,
                         bool lock, Error **errp)
{
    int ret, fd;
    SheepdogVdiReq hdr;
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    unsigned int wlen, rlen = 0;
    char buf[SD_MAX_VDI_LEN + SD_MAX_VDI_TAG_LEN] QEMU_NONSTRING;

    fd = connect_to_sdog(s, errp);
    if (fd < 0) {
        return fd;
    }

    /* This pair of strncpy calls ensures that the buffer is zero-filled,
     * which is desirable since we'll soon be sending those bytes, and
     * don't want the send_req to read uninitialized data.
     */
    strncpy(buf, filename, SD_MAX_VDI_LEN);
    strncpy(buf + SD_MAX_VDI_LEN, tag, SD_MAX_VDI_TAG_LEN);

    memset(&hdr, 0, sizeof(hdr));
    if (lock) {
        hdr.opcode = SD_OP_LOCK_VDI;
        hdr.type = LOCK_TYPE_NORMAL;
    } else {
        hdr.opcode = SD_OP_GET_VDI_INFO;
    }
    wlen = SD_MAX_VDI_LEN + SD_MAX_VDI_TAG_LEN;
    hdr.proto_ver = SD_PROTO_VER;
    hdr.data_length = wlen;
    hdr.snapid = snapid;
    hdr.flags = SD_FLAG_CMD_WRITE;

    ret = do_req(fd, s->bs, (SheepdogReq *)&hdr, buf, &wlen, &rlen);
    if (ret) {
        error_setg_errno(errp, -ret, "cannot get vdi info");
        goto out;
    }

    if (rsp->result != SD_RES_SUCCESS) {
        error_setg(errp, "cannot get vdi info, %s, %s %" PRIu32 " %s",
                   sd_strerror(rsp->result), filename, snapid, tag);
        if (rsp->result == SD_RES_NO_VDI) {
            ret = -ENOENT;
        } else if (rsp->result == SD_RES_VDI_LOCKED) {
            ret = -EBUSY;
        } else {
            ret = -EIO;
        }
        goto out;
    }
    *vid = rsp->vdi_id;

    ret = 0;
out:
    closesocket(fd);
    return ret;
}

static void coroutine_fn add_aio_request(BDRVSheepdogState *s, AIOReq *aio_req,
                                         struct iovec *iov, int niov,
                                         enum AIOCBState aiocb_type)
{
    int nr_copies = s->inode.nr_copies;
    SheepdogObjReq hdr;
    unsigned int wlen = 0;
    int ret;
    uint64_t oid = aio_req->oid;
    unsigned int datalen = aio_req->data_len;
    uint64_t offset = aio_req->offset;
    uint8_t flags = aio_req->flags;
    uint64_t old_oid = aio_req->base_oid;
    bool create = aio_req->create;

    qemu_co_mutex_lock(&s->queue_lock);
    QLIST_INSERT_HEAD(&s->inflight_aio_head, aio_req, aio_siblings);
    qemu_co_mutex_unlock(&s->queue_lock);

    if (!nr_copies) {
        error_report("bug");
    }

    memset(&hdr, 0, sizeof(hdr));

    switch (aiocb_type) {
    case AIOCB_FLUSH_CACHE:
        hdr.opcode = SD_OP_FLUSH_VDI;
        break;
    case AIOCB_READ_UDATA:
        hdr.opcode = SD_OP_READ_OBJ;
        hdr.flags = flags;
        break;
    case AIOCB_WRITE_UDATA:
        if (create) {
            hdr.opcode = SD_OP_CREATE_AND_WRITE_OBJ;
        } else {
            hdr.opcode = SD_OP_WRITE_OBJ;
        }
        wlen = datalen;
        hdr.flags = SD_FLAG_CMD_WRITE | flags;
        break;
    case AIOCB_DISCARD_OBJ:
        hdr.opcode = SD_OP_WRITE_OBJ;
        hdr.flags = SD_FLAG_CMD_WRITE | flags;
        s->inode.data_vdi_id[data_oid_to_idx(oid)] = 0;
        offset = offsetof(SheepdogInode,
                          data_vdi_id[data_oid_to_idx(oid)]);
        oid = vid_to_vdi_oid(s->inode.vdi_id);
        wlen = datalen = sizeof(uint32_t);
        break;
    }

    if (s->cache_flags) {
        hdr.flags |= s->cache_flags;
    }

    hdr.oid = oid;
    hdr.cow_oid = old_oid;
    hdr.copies = s->inode.nr_copies;

    hdr.data_length = datalen;
    hdr.offset = offset;

    hdr.id = aio_req->id;

    qemu_co_mutex_lock(&s->lock);
    s->co_send = qemu_coroutine_self();
    aio_set_fd_handler(s->aio_context, s->fd, false,
                       co_read_response, co_write_request, NULL, s);
    socket_set_cork(s->fd, 1);

    /* send a header */
    ret = qemu_co_send(s->fd, &hdr, sizeof(hdr));
    if (ret != sizeof(hdr)) {
        error_report("failed to send a req, %s", strerror(errno));
        goto out;
    }

    if (wlen) {
        ret = qemu_co_sendv(s->fd, iov, niov, aio_req->iov_offset, wlen);
        if (ret != wlen) {
            error_report("failed to send a data, %s", strerror(errno));
        }
    }
out:
    socket_set_cork(s->fd, 0);
    aio_set_fd_handler(s->aio_context, s->fd, false,
                       co_read_response, NULL, NULL, s);
    s->co_send = NULL;
    qemu_co_mutex_unlock(&s->lock);
}

static int read_write_object(int fd, BlockDriverState *bs, char *buf,
                             uint64_t oid, uint8_t copies,
                             unsigned int datalen, uint64_t offset,
                             bool write, bool create, uint32_t cache_flags)
{
    SheepdogObjReq hdr;
    SheepdogObjRsp *rsp = (SheepdogObjRsp *)&hdr;
    unsigned int wlen, rlen;
    int ret;

    memset(&hdr, 0, sizeof(hdr));

    if (write) {
        wlen = datalen;
        rlen = 0;
        hdr.flags = SD_FLAG_CMD_WRITE;
        if (create) {
            hdr.opcode = SD_OP_CREATE_AND_WRITE_OBJ;
        } else {
            hdr.opcode = SD_OP_WRITE_OBJ;
        }
    } else {
        wlen = 0;
        rlen = datalen;
        hdr.opcode = SD_OP_READ_OBJ;
    }

    hdr.flags |= cache_flags;

    hdr.oid = oid;
    hdr.data_length = datalen;
    hdr.offset = offset;
    hdr.copies = copies;

    ret = do_req(fd, bs, (SheepdogReq *)&hdr, buf, &wlen, &rlen);
    if (ret) {
        error_report("failed to send a request to the sheep");
        return ret;
    }

    switch (rsp->result) {
    case SD_RES_SUCCESS:
        return 0;
    default:
        error_report("%s", sd_strerror(rsp->result));
        return -EIO;
    }
}

static int read_object(int fd, BlockDriverState *bs, char *buf,
                       uint64_t oid, uint8_t copies,
                       unsigned int datalen, uint64_t offset,
                       uint32_t cache_flags)
{
    return read_write_object(fd, bs, buf, oid, copies,
                             datalen, offset, false,
                             false, cache_flags);
}

static int write_object(int fd, BlockDriverState *bs, char *buf,
                        uint64_t oid, uint8_t copies,
                        unsigned int datalen, uint64_t offset, bool create,
                        uint32_t cache_flags)
{
    return read_write_object(fd, bs, buf, oid, copies,
                             datalen, offset, true,
                             create, cache_flags);
}

/* update inode with the latest state */
static int reload_inode(BDRVSheepdogState *s, uint32_t snapid, const char *tag)
{
    Error *local_err = NULL;
    SheepdogInode *inode;
    int ret = 0, fd;
    uint32_t vid = 0;

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        error_report_err(local_err);
        return -EIO;
    }

    inode = g_malloc(SD_INODE_HEADER_SIZE);

    ret = find_vdi_name(s, s->name, snapid, tag, &vid, false, &local_err);
    if (ret) {
        error_report_err(local_err);
        goto out;
    }

    ret = read_object(fd, s->bs, (char *)inode, vid_to_vdi_oid(vid),
                      s->inode.nr_copies, SD_INODE_HEADER_SIZE, 0,
                      s->cache_flags);
    if (ret < 0) {
        goto out;
    }

    if (inode->vdi_id != s->inode.vdi_id) {
        memcpy(&s->inode, inode, SD_INODE_HEADER_SIZE);
    }

out:
    g_free(inode);
    closesocket(fd);

    return ret;
}

static void coroutine_fn resend_aioreq(BDRVSheepdogState *s, AIOReq *aio_req)
{
    SheepdogAIOCB *acb = aio_req->aiocb;

    aio_req->create = false;

    /* check whether this request becomes a CoW one */
    if (acb->aiocb_type == AIOCB_WRITE_UDATA && is_data_obj(aio_req->oid)) {
        int idx = data_oid_to_idx(aio_req->oid);

        if (is_data_obj_writable(&s->inode, idx)) {
            goto out;
        }

        if (s->inode.data_vdi_id[idx]) {
            aio_req->base_oid = vid_to_data_oid(s->inode.data_vdi_id[idx], idx);
            aio_req->flags |= SD_FLAG_CMD_COW;
        }
        aio_req->create = true;
    }
out:
    if (is_data_obj(aio_req->oid)) {
        add_aio_request(s, aio_req, acb->qiov->iov, acb->qiov->niov,
                        acb->aiocb_type);
    } else {
        struct iovec iov;
        iov.iov_base = &s->inode;
        iov.iov_len = sizeof(s->inode);
        add_aio_request(s, aio_req, &iov, 1, AIOCB_WRITE_UDATA);
    }
}

static void sd_detach_aio_context(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;

    aio_set_fd_handler(s->aio_context, s->fd, false, NULL,
                       NULL, NULL, NULL);
}

static void sd_attach_aio_context(BlockDriverState *bs,
                                  AioContext *new_context)
{
    BDRVSheepdogState *s = bs->opaque;

    s->aio_context = new_context;
    aio_set_fd_handler(new_context, s->fd, false,
                       co_read_response, NULL, NULL, s);
}

static QemuOptsList runtime_opts = {
    .name = "sheepdog",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "vdi",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "snap-id",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "tag",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static int sd_open(BlockDriverState *bs, QDict *options, int flags,
                   Error **errp)
{
    int ret, fd;
    uint32_t vid = 0;
    BDRVSheepdogState *s = bs->opaque;
    const char *vdi, *snap_id_str, *tag;
    uint64_t snap_id;
    char *buf = NULL;
    QemuOpts *opts;
    Error *local_err = NULL;

    s->bs = bs;
    s->aio_context = bdrv_get_aio_context(bs);

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto err_no_fd;
    }

    s->addr = sd_server_config(options, errp);
    if (!s->addr) {
        ret = -EINVAL;
        goto err_no_fd;
    }

    vdi = qemu_opt_get(opts, "vdi");
    snap_id_str = qemu_opt_get(opts, "snap-id");
    snap_id = qemu_opt_get_number(opts, "snap-id", CURRENT_VDI_ID);
    tag = qemu_opt_get(opts, "tag");

    if (!vdi) {
        error_setg(errp, "parameter 'vdi' is missing");
        ret = -EINVAL;
        goto err_no_fd;
    }
    if (strlen(vdi) >= SD_MAX_VDI_LEN) {
        error_setg(errp, "value of parameter 'vdi' is too long");
        ret = -EINVAL;
        goto err_no_fd;
    }

    if (snap_id > UINT32_MAX) {
        snap_id = 0;
    }
    if (snap_id_str && !snap_id) {
        error_setg(errp, "'snap-id=%s' is not a valid snapshot ID",
                   snap_id_str);
        ret = -EINVAL;
        goto err_no_fd;
    }

    if (!tag) {
        tag = "";
    }
    if (strlen(tag) >= SD_MAX_VDI_TAG_LEN) {
        error_setg(errp, "value of parameter 'tag' is too long");
        ret = -EINVAL;
        goto err_no_fd;
    }

    QLIST_INIT(&s->inflight_aio_head);
    QLIST_INIT(&s->failed_aio_head);
    QLIST_INIT(&s->inflight_aiocb_head);

    s->fd = get_sheep_fd(s, errp);
    if (s->fd < 0) {
        ret = s->fd;
        goto err_no_fd;
    }

    ret = find_vdi_name(s, vdi, (uint32_t)snap_id, tag, &vid, true, errp);
    if (ret) {
        goto err;
    }

    /*
     * QEMU block layer emulates writethrough cache as 'writeback + flush', so
     * we always set SD_FLAG_CMD_CACHE (writeback cache) as default.
     */
    s->cache_flags = SD_FLAG_CMD_CACHE;
    if (flags & BDRV_O_NOCACHE) {
        s->cache_flags = SD_FLAG_CMD_DIRECT;
    }
    s->discard_supported = true;

    if (snap_id || tag[0]) {
        trace_sheepdog_open(vid);
        s->is_snapshot = true;
    }

    fd = connect_to_sdog(s, errp);
    if (fd < 0) {
        ret = fd;
        goto err;
    }

    buf = g_malloc(SD_INODE_SIZE);
    ret = read_object(fd, s->bs, buf, vid_to_vdi_oid(vid),
                      0, SD_INODE_SIZE, 0, s->cache_flags);

    closesocket(fd);

    if (ret) {
        error_setg(errp, "Can't read snapshot inode");
        goto err;
    }

    memcpy(&s->inode, buf, sizeof(s->inode));

    bs->total_sectors = s->inode.vdi_size / BDRV_SECTOR_SIZE;
    pstrcpy(s->name, sizeof(s->name), vdi);
    qemu_co_mutex_init(&s->lock);
    qemu_co_mutex_init(&s->queue_lock);
    qemu_co_queue_init(&s->overlapping_queue);
    qemu_opts_del(opts);
    g_free(buf);
    return 0;

err:
    aio_set_fd_handler(bdrv_get_aio_context(bs), s->fd,
                       false, NULL, NULL, NULL, NULL);
    closesocket(s->fd);
err_no_fd:
    qemu_opts_del(opts);
    g_free(buf);
    return ret;
}

static int sd_reopen_prepare(BDRVReopenState *state, BlockReopenQueue *queue,
                             Error **errp)
{
    BDRVSheepdogState *s = state->bs->opaque;
    BDRVSheepdogReopenState *re_s;
    int ret = 0;

    re_s = state->opaque = g_new0(BDRVSheepdogReopenState, 1);

    re_s->cache_flags = SD_FLAG_CMD_CACHE;
    if (state->flags & BDRV_O_NOCACHE) {
        re_s->cache_flags = SD_FLAG_CMD_DIRECT;
    }

    re_s->fd = get_sheep_fd(s, errp);
    if (re_s->fd < 0) {
        ret = re_s->fd;
        return ret;
    }

    return ret;
}

static void sd_reopen_commit(BDRVReopenState *state)
{
    BDRVSheepdogReopenState *re_s = state->opaque;
    BDRVSheepdogState *s = state->bs->opaque;

    if (s->fd) {
        aio_set_fd_handler(s->aio_context, s->fd, false,
                           NULL, NULL, NULL, NULL);
        closesocket(s->fd);
    }

    s->fd = re_s->fd;
    s->cache_flags = re_s->cache_flags;

    g_free(state->opaque);
    state->opaque = NULL;

    return;
}

static void sd_reopen_abort(BDRVReopenState *state)
{
    BDRVSheepdogReopenState *re_s = state->opaque;
    BDRVSheepdogState *s = state->bs->opaque;

    if (re_s == NULL) {
        return;
    }

    if (re_s->fd) {
        aio_set_fd_handler(s->aio_context, re_s->fd, false,
                           NULL, NULL, NULL, NULL);
        closesocket(re_s->fd);
    }

    g_free(state->opaque);
    state->opaque = NULL;

    return;
}

static int do_sd_create(BDRVSheepdogState *s, uint32_t *vdi_id, int snapshot,
                        Error **errp)
{
    SheepdogVdiReq hdr;
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    int fd, ret;
    unsigned int wlen, rlen = 0;
    char buf[SD_MAX_VDI_LEN];

    fd = connect_to_sdog(s, errp);
    if (fd < 0) {
        return fd;
    }

    /* FIXME: would it be better to fail (e.g., return -EIO) when filename
     * does not fit in buf?  For now, just truncate and avoid buffer overrun.
     */
    memset(buf, 0, sizeof(buf));
    pstrcpy(buf, sizeof(buf), s->name);

    memset(&hdr, 0, sizeof(hdr));
    hdr.opcode = SD_OP_NEW_VDI;
    hdr.base_vdi_id = s->inode.vdi_id;

    wlen = SD_MAX_VDI_LEN;

    hdr.flags = SD_FLAG_CMD_WRITE;
    hdr.snapid = snapshot;

    hdr.data_length = wlen;
    hdr.vdi_size = s->inode.vdi_size;
    hdr.copy_policy = s->inode.copy_policy;
    hdr.copies = s->inode.nr_copies;
    hdr.block_size_shift = s->inode.block_size_shift;

    ret = do_req(fd, NULL, (SheepdogReq *)&hdr, buf, &wlen, &rlen);

    closesocket(fd);

    if (ret) {
        error_setg_errno(errp, -ret, "create failed");
        return ret;
    }

    if (rsp->result != SD_RES_SUCCESS) {
        error_setg(errp, "%s, %s", sd_strerror(rsp->result), s->inode.name);
        return -EIO;
    }

    if (vdi_id) {
        *vdi_id = rsp->vdi_id;
    }

    return 0;
}

static int sd_prealloc(BlockDriverState *bs, int64_t old_size, int64_t new_size,
                       Error **errp)
{
    BlockBackend *blk = NULL;
    BDRVSheepdogState *base = bs->opaque;
    unsigned long buf_size;
    uint32_t idx, max_idx;
    uint32_t object_size;
    void *buf = NULL;
    int ret;

    blk = blk_new(bdrv_get_aio_context(bs),
                  BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE | BLK_PERM_RESIZE,
                  BLK_PERM_ALL);

    ret = blk_insert_bs(blk, bs, errp);
    if (ret < 0) {
        goto out_with_err_set;
    }

    blk_set_allow_write_beyond_eof(blk, true);

    object_size = (UINT32_C(1) << base->inode.block_size_shift);
    buf_size = MIN(object_size, SD_DATA_OBJ_SIZE);
    buf = g_malloc0(buf_size);

    max_idx = DIV_ROUND_UP(new_size, buf_size);

    for (idx = old_size / buf_size; idx < max_idx; idx++) {
        /*
         * The created image can be a cloned image, so we need to read
         * a data from the source image.
         */
        ret = blk_pread(blk, idx * buf_size, buf, buf_size);
        if (ret < 0) {
            goto out;
        }
        ret = blk_pwrite(blk, idx * buf_size, buf, buf_size, 0);
        if (ret < 0) {
            goto out;
        }
    }

    ret = 0;
out:
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't pre-allocate");
    }
out_with_err_set:
    blk_unref(blk);
    g_free(buf);

    return ret;
}

static int sd_create_prealloc(BlockdevOptionsSheepdog *location, int64_t size,
                              Error **errp)
{
    BlockDriverState *bs;
    Visitor *v;
    QObject *obj = NULL;
    QDict *qdict;
    Error *local_err = NULL;
    int ret;

    v = qobject_output_visitor_new(&obj);
    visit_type_BlockdevOptionsSheepdog(v, NULL, &location, &local_err);
    visit_free(v);

    if (local_err) {
        error_propagate(errp, local_err);
        qobject_unref(obj);
        return -EINVAL;
    }

    qdict = qobject_to(QDict, obj);
    qdict_flatten(qdict);

    qdict_put_str(qdict, "driver", "sheepdog");

    bs = bdrv_open(NULL, NULL, qdict, BDRV_O_PROTOCOL | BDRV_O_RDWR, errp);
    if (bs == NULL) {
        ret = -EIO;
        goto fail;
    }

    ret = sd_prealloc(bs, 0, size, errp);
fail:
    bdrv_unref(bs);
    qobject_unref(qdict);
    return ret;
}

static int parse_redundancy(BDRVSheepdogState *s, SheepdogRedundancy *opt)
{
    struct SheepdogInode *inode = &s->inode;

    switch (opt->type) {
    case SHEEPDOG_REDUNDANCY_TYPE_FULL:
        if (opt->u.full.copies > SD_MAX_COPIES || opt->u.full.copies < 1) {
            return -EINVAL;
        }
        inode->copy_policy = 0;
        inode->nr_copies = opt->u.full.copies;
        return 0;

    case SHEEPDOG_REDUNDANCY_TYPE_ERASURE_CODED:
    {
        int64_t copy = opt->u.erasure_coded.data_strips;
        int64_t parity = opt->u.erasure_coded.parity_strips;

        if (copy != 2 && copy != 4 && copy != 8 && copy != 16) {
            return -EINVAL;
        }

        if (parity >= SD_EC_MAX_STRIP || parity < 1) {
            return -EINVAL;
        }

        /*
         * 4 bits for parity and 4 bits for data.
         * We have to compress upper data bits because it can't represent 16
         */
        inode->copy_policy = ((copy / 2) << 4) + parity;
        inode->nr_copies = copy + parity;
        return 0;
    }

    default:
        g_assert_not_reached();
    }

    return -EINVAL;
}

/*
 * Sheepdog support two kinds of redundancy, full replication and erasure
 * coding.
 *
 * # create a fully replicated vdi with x copies
 * -o redundancy=x (1 <= x <= SD_MAX_COPIES)
 *
 * # create a erasure coded vdi with x data strips and y parity strips
 * -o redundancy=x:y (x must be one of {2,4,8,16} and 1 <= y < SD_EC_MAX_STRIP)
 */
static SheepdogRedundancy *parse_redundancy_str(const char *opt)
{
    SheepdogRedundancy *redundancy;
    const char *n1, *n2;
    long copy, parity;
    char p[10];
    int ret;

    pstrcpy(p, sizeof(p), opt);
    n1 = strtok(p, ":");
    n2 = strtok(NULL, ":");

    if (!n1) {
        return NULL;
    }

    ret = qemu_strtol(n1, NULL, 10, &copy);
    if (ret < 0) {
        return NULL;
    }

    redundancy = g_new0(SheepdogRedundancy, 1);
    if (!n2) {
        *redundancy = (SheepdogRedundancy) {
            .type               = SHEEPDOG_REDUNDANCY_TYPE_FULL,
            .u.full.copies      = copy,
        };
    } else {
        ret = qemu_strtol(n2, NULL, 10, &parity);
        if (ret < 0) {
            g_free(redundancy);
            return NULL;
        }

        *redundancy = (SheepdogRedundancy) {
            .type               = SHEEPDOG_REDUNDANCY_TYPE_ERASURE_CODED,
            .u.erasure_coded    = {
                .data_strips    = copy,
                .parity_strips  = parity,
            },
        };
    }

    return redundancy;
}

static int parse_block_size_shift(BDRVSheepdogState *s,
                                  BlockdevCreateOptionsSheepdog *opts)
{
    struct SheepdogInode *inode = &s->inode;
    uint64_t object_size;
    int obj_order;

    if (opts->has_object_size) {
        object_size = opts->object_size;

        if ((object_size - 1) & object_size) {    /* not a power of 2? */
            return -EINVAL;
        }
        obj_order = ctz32(object_size);
        if (obj_order < 20 || obj_order > 31) {
            return -EINVAL;
        }
        inode->block_size_shift = (uint8_t)obj_order;
    }

    return 0;
}

static int sd_co_create(BlockdevCreateOptions *options, Error **errp)
{
    BlockdevCreateOptionsSheepdog *opts = &options->u.sheepdog;
    int ret = 0;
    uint32_t vid = 0;
    char *backing_file = NULL;
    char *buf = NULL;
    BDRVSheepdogState *s;
    uint64_t max_vdi_size;
    bool prealloc = false;

    assert(options->driver == BLOCKDEV_DRIVER_SHEEPDOG);

    s = g_new0(BDRVSheepdogState, 1);

    /* Steal SocketAddress from QAPI, set NULL to prevent double free */
    s->addr = opts->location->server;
    opts->location->server = NULL;

    if (strlen(opts->location->vdi) >= sizeof(s->name)) {
        error_setg(errp, "'vdi' string too long");
        ret = -EINVAL;
        goto out;
    }
    pstrcpy(s->name, sizeof(s->name), opts->location->vdi);

    s->inode.vdi_size = opts->size;
    backing_file = opts->backing_file;

    if (!opts->has_preallocation) {
        opts->preallocation = PREALLOC_MODE_OFF;
    }
    switch (opts->preallocation) {
    case PREALLOC_MODE_OFF:
        prealloc = false;
        break;
    case PREALLOC_MODE_FULL:
        prealloc = true;
        break;
    default:
        error_setg(errp, "Preallocation mode not supported for Sheepdog");
        ret = -EINVAL;
        goto out;
    }

    if (opts->has_redundancy) {
        ret = parse_redundancy(s, opts->redundancy);
        if (ret < 0) {
            error_setg(errp, "Invalid redundancy mode");
            goto out;
        }
    }
    ret = parse_block_size_shift(s, opts);
    if (ret < 0) {
        error_setg(errp, "Invalid object_size."
                         " obect_size needs to be power of 2"
                         " and be limited from 2^20 to 2^31");
        goto out;
    }

    if (opts->has_backing_file) {
        BlockBackend *blk;
        BDRVSheepdogState *base;
        BlockDriver *drv;

        /* Currently, only Sheepdog backing image is supported. */
        drv = bdrv_find_protocol(opts->backing_file, true, NULL);
        if (!drv || strcmp(drv->protocol_name, "sheepdog") != 0) {
            error_setg(errp, "backing_file must be a sheepdog image");
            ret = -EINVAL;
            goto out;
        }

        blk = blk_new_open(opts->backing_file, NULL, NULL,
                           BDRV_O_PROTOCOL, errp);
        if (blk == NULL) {
            ret = -EIO;
            goto out;
        }

        base = blk_bs(blk)->opaque;

        if (!is_snapshot(&base->inode)) {
            error_setg(errp, "cannot clone from a non snapshot vdi");
            blk_unref(blk);
            ret = -EINVAL;
            goto out;
        }
        s->inode.vdi_id = base->inode.vdi_id;
        blk_unref(blk);
    }

    s->aio_context = qemu_get_aio_context();

    /* if block_size_shift is not specified, get cluster default value */
    if (s->inode.block_size_shift == 0) {
        SheepdogVdiReq hdr;
        SheepdogClusterRsp *rsp = (SheepdogClusterRsp *)&hdr;
        int fd;
        unsigned int wlen = 0, rlen = 0;

        fd = connect_to_sdog(s, errp);
        if (fd < 0) {
            ret = fd;
            goto out;
        }

        memset(&hdr, 0, sizeof(hdr));
        hdr.opcode = SD_OP_GET_CLUSTER_DEFAULT;
        hdr.proto_ver = SD_PROTO_VER;

        ret = do_req(fd, NULL, (SheepdogReq *)&hdr,
                     NULL, &wlen, &rlen);
        closesocket(fd);
        if (ret) {
            error_setg_errno(errp, -ret, "failed to get cluster default");
            goto out;
        }
        if (rsp->result == SD_RES_SUCCESS) {
            s->inode.block_size_shift = rsp->block_size_shift;
        } else {
            s->inode.block_size_shift = SD_DEFAULT_BLOCK_SIZE_SHIFT;
        }
    }

    max_vdi_size = (UINT64_C(1) << s->inode.block_size_shift) * MAX_DATA_OBJS;

    if (s->inode.vdi_size > max_vdi_size) {
        error_setg(errp, "An image is too large."
                         " The maximum image size is %"PRIu64 "GB",
                         max_vdi_size / 1024 / 1024 / 1024);
        ret = -EINVAL;
        goto out;
    }

    ret = do_sd_create(s, &vid, 0, errp);
    if (ret) {
        goto out;
    }

    if (prealloc) {
        ret = sd_create_prealloc(opts->location, opts->size, errp);
    }
out:
    g_free(backing_file);
    g_free(buf);
    g_free(s->addr);
    g_free(s);
    return ret;
}

static int coroutine_fn sd_co_create_opts(BlockDriver *drv,
                                          const char *filename,
                                          QemuOpts *opts,
                                          Error **errp)
{
    BlockdevCreateOptions *create_options = NULL;
    QDict *qdict, *location_qdict;
    Visitor *v;
    char *redundancy;
    Error *local_err = NULL;
    int ret;

    redundancy = qemu_opt_get_del(opts, BLOCK_OPT_REDUNDANCY);

    qdict = qemu_opts_to_qdict(opts, NULL);
    qdict_put_str(qdict, "driver", "sheepdog");

    location_qdict = qdict_new();
    qdict_put(qdict, "location", location_qdict);

    sd_parse_filename(filename, location_qdict, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    qdict_flatten(qdict);

    /* Change legacy command line options into QMP ones */
    static const QDictRenames opt_renames[] = {
        { BLOCK_OPT_BACKING_FILE,       "backing-file" },
        { BLOCK_OPT_OBJECT_SIZE,        "object-size" },
        { NULL, NULL },
    };

    if (!qdict_rename_keys(qdict, opt_renames, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    /* Get the QAPI object */
    v = qobject_input_visitor_new_flat_confused(qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto fail;
    }

    visit_type_BlockdevCreateOptions(v, NULL, &create_options, &local_err);
    visit_free(v);

    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    assert(create_options->driver == BLOCKDEV_DRIVER_SHEEPDOG);
    create_options->u.sheepdog.size =
        ROUND_UP(create_options->u.sheepdog.size, BDRV_SECTOR_SIZE);

    if (redundancy) {
        create_options->u.sheepdog.has_redundancy = true;
        create_options->u.sheepdog.redundancy =
            parse_redundancy_str(redundancy);
        if (create_options->u.sheepdog.redundancy == NULL) {
            error_setg(errp, "Invalid redundancy mode");
            ret = -EINVAL;
            goto fail;
        }
    }

    ret = sd_co_create(create_options, errp);
fail:
    qapi_free_BlockdevCreateOptions(create_options);
    qobject_unref(qdict);
    g_free(redundancy);
    return ret;
}

static void sd_close(BlockDriverState *bs)
{
    Error *local_err = NULL;
    BDRVSheepdogState *s = bs->opaque;
    SheepdogVdiReq hdr;
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    unsigned int wlen, rlen = 0;
    int fd, ret;

    trace_sheepdog_close(s->name);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        error_report_err(local_err);
        return;
    }

    memset(&hdr, 0, sizeof(hdr));

    hdr.opcode = SD_OP_RELEASE_VDI;
    hdr.type = LOCK_TYPE_NORMAL;
    hdr.base_vdi_id = s->inode.vdi_id;
    wlen = strlen(s->name) + 1;
    hdr.data_length = wlen;
    hdr.flags = SD_FLAG_CMD_WRITE;

    ret = do_req(fd, s->bs, (SheepdogReq *)&hdr,
                 s->name, &wlen, &rlen);

    closesocket(fd);

    if (!ret && rsp->result != SD_RES_SUCCESS &&
        rsp->result != SD_RES_VDI_NOT_LOCKED) {
        error_report("%s, %s", sd_strerror(rsp->result), s->name);
    }

    aio_set_fd_handler(bdrv_get_aio_context(bs), s->fd,
                       false, NULL, NULL, NULL, NULL);
    closesocket(s->fd);
    qapi_free_SocketAddress(s->addr);
}

static int64_t sd_getlength(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;

    return s->inode.vdi_size;
}

static int coroutine_fn sd_co_truncate(BlockDriverState *bs, int64_t offset,
                                       bool exact, PreallocMode prealloc,
                                       Error **errp)
{
    BDRVSheepdogState *s = bs->opaque;
    int ret, fd;
    unsigned int datalen;
    uint64_t max_vdi_size;
    int64_t old_size = s->inode.vdi_size;

    if (prealloc != PREALLOC_MODE_OFF && prealloc != PREALLOC_MODE_FULL) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    max_vdi_size = (UINT64_C(1) << s->inode.block_size_shift) * MAX_DATA_OBJS;
    if (offset < old_size) {
        error_setg(errp, "shrinking is not supported");
        return -EINVAL;
    } else if (offset > max_vdi_size) {
        error_setg(errp, "too big image size");
        return -EINVAL;
    }

    fd = connect_to_sdog(s, errp);
    if (fd < 0) {
        return fd;
    }

    /* we don't need to update entire object */
    datalen = SD_INODE_HEADER_SIZE;
    s->inode.vdi_size = offset;
    ret = write_object(fd, s->bs, (char *)&s->inode,
                       vid_to_vdi_oid(s->inode.vdi_id), s->inode.nr_copies,
                       datalen, 0, false, s->cache_flags);
    close(fd);

    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to update an inode");
        return ret;
    }

    if (prealloc == PREALLOC_MODE_FULL) {
        ret = sd_prealloc(bs, old_size, offset, errp);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/*
 * This function is called after writing data objects.  If we need to
 * update metadata, this sends a write request to the vdi object.
 */
static void coroutine_fn sd_write_done(SheepdogAIOCB *acb)
{
    BDRVSheepdogState *s = acb->s;
    struct iovec iov;
    AIOReq *aio_req;
    uint32_t offset, data_len, mn, mx;

    mn = acb->min_dirty_data_idx;
    mx = acb->max_dirty_data_idx;
    if (mn <= mx) {
        /* we need to update the vdi object. */
        ++acb->nr_pending;
        offset = sizeof(s->inode) - sizeof(s->inode.data_vdi_id) +
            mn * sizeof(s->inode.data_vdi_id[0]);
        data_len = (mx - mn + 1) * sizeof(s->inode.data_vdi_id[0]);

        acb->min_dirty_data_idx = UINT32_MAX;
        acb->max_dirty_data_idx = 0;

        iov.iov_base = &s->inode;
        iov.iov_len = sizeof(s->inode);
        aio_req = alloc_aio_req(s, acb, vid_to_vdi_oid(s->inode.vdi_id),
                                data_len, offset, 0, false, 0, offset);
        add_aio_request(s, aio_req, &iov, 1, AIOCB_WRITE_UDATA);
        if (--acb->nr_pending) {
            qemu_coroutine_yield();
        }
    }
}

/* Delete current working VDI on the snapshot chain */
static bool sd_delete(BDRVSheepdogState *s)
{
    Error *local_err = NULL;
    unsigned int wlen = SD_MAX_VDI_LEN, rlen = 0;
    SheepdogVdiReq hdr = {
        .opcode = SD_OP_DEL_VDI,
        .base_vdi_id = s->inode.vdi_id,
        .data_length = wlen,
        .flags = SD_FLAG_CMD_WRITE,
    };
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    int fd, ret;

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        error_report_err(local_err);
        return false;
    }

    ret = do_req(fd, s->bs, (SheepdogReq *)&hdr,
                 s->name, &wlen, &rlen);
    closesocket(fd);
    if (ret) {
        return false;
    }
    switch (rsp->result) {
    case SD_RES_NO_VDI:
        error_report("%s was already deleted", s->name);
        /* fall through */
    case SD_RES_SUCCESS:
        break;
    default:
        error_report("%s, %s", sd_strerror(rsp->result), s->name);
        return false;
    }

    return true;
}

/*
 * Create a writable VDI from a snapshot
 */
static int sd_create_branch(BDRVSheepdogState *s)
{
    Error *local_err = NULL;
    int ret, fd;
    uint32_t vid;
    char *buf;
    bool deleted;

    trace_sheepdog_create_branch_snapshot(s->inode.vdi_id);

    buf = g_malloc(SD_INODE_SIZE);

    /*
     * Even If deletion fails, we will just create extra snapshot based on
     * the working VDI which was supposed to be deleted. So no need to
     * false bail out.
     */
    deleted = sd_delete(s);
    ret = do_sd_create(s, &vid, !deleted, &local_err);
    if (ret) {
        error_report_err(local_err);
        goto out;
    }

    trace_sheepdog_create_branch_created(vid);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        error_report_err(local_err);
        ret = fd;
        goto out;
    }

    ret = read_object(fd, s->bs, buf, vid_to_vdi_oid(vid),
                      s->inode.nr_copies, SD_INODE_SIZE, 0, s->cache_flags);

    closesocket(fd);

    if (ret < 0) {
        goto out;
    }

    memcpy(&s->inode, buf, sizeof(s->inode));

    s->is_snapshot = false;
    ret = 0;
    trace_sheepdog_create_branch_new(s->inode.vdi_id);

out:
    g_free(buf);

    return ret;
}

/*
 * Send I/O requests to the server.
 *
 * This function sends requests to the server, links the requests to
 * the inflight_list in BDRVSheepdogState, and exits without
 * waiting the response.  The responses are received in the
 * `aio_read_response' function which is called from the main loop as
 * a fd handler.
 *
 * Returns 1 when we need to wait a response, 0 when there is no sent
 * request and -errno in error cases.
 */
static void coroutine_fn sd_co_rw_vector(SheepdogAIOCB *acb)
{
    int ret = 0;
    unsigned long len, done = 0, total = acb->nb_sectors * BDRV_SECTOR_SIZE;
    unsigned long idx;
    uint32_t object_size;
    uint64_t oid;
    uint64_t offset;
    BDRVSheepdogState *s = acb->s;
    SheepdogInode *inode = &s->inode;
    AIOReq *aio_req;

    if (acb->aiocb_type == AIOCB_WRITE_UDATA && s->is_snapshot) {
        /*
         * In the case we open the snapshot VDI, Sheepdog creates the
         * writable VDI when we do a write operation first.
         */
        ret = sd_create_branch(s);
        if (ret) {
            acb->ret = -EIO;
            return;
        }
    }

    object_size = (UINT32_C(1) << inode->block_size_shift);
    idx = acb->sector_num * BDRV_SECTOR_SIZE / object_size;
    offset = (acb->sector_num * BDRV_SECTOR_SIZE) % object_size;

    /*
     * Make sure we don't free the aiocb before we are done with all requests.
     * This additional reference is dropped at the end of this function.
     */
    acb->nr_pending++;

    while (done != total) {
        uint8_t flags = 0;
        uint64_t old_oid = 0;
        bool create = false;

        oid = vid_to_data_oid(inode->data_vdi_id[idx], idx);

        len = MIN(total - done, object_size - offset);

        switch (acb->aiocb_type) {
        case AIOCB_READ_UDATA:
            if (!inode->data_vdi_id[idx]) {
                qemu_iovec_memset(acb->qiov, done, 0, len);
                goto done;
            }
            break;
        case AIOCB_WRITE_UDATA:
            if (!inode->data_vdi_id[idx]) {
                create = true;
            } else if (!is_data_obj_writable(inode, idx)) {
                /* Copy-On-Write */
                create = true;
                old_oid = oid;
                flags = SD_FLAG_CMD_COW;
            }
            break;
        case AIOCB_DISCARD_OBJ:
            /*
             * We discard the object only when the whole object is
             * 1) allocated 2) trimmed. Otherwise, simply skip it.
             */
            if (len != object_size || inode->data_vdi_id[idx] == 0) {
                goto done;
            }
            break;
        default:
            break;
        }

        if (create) {
            trace_sheepdog_co_rw_vector_update(inode->vdi_id, oid,
                                  vid_to_data_oid(inode->data_vdi_id[idx], idx),
                                  idx);
            oid = vid_to_data_oid(inode->vdi_id, idx);
            trace_sheepdog_co_rw_vector_new(oid);
        }

        aio_req = alloc_aio_req(s, acb, oid, len, offset, flags, create,
                                old_oid,
                                acb->aiocb_type == AIOCB_DISCARD_OBJ ?
                                0 : done);
        add_aio_request(s, aio_req, acb->qiov->iov, acb->qiov->niov,
                        acb->aiocb_type);
    done:
        offset = 0;
        idx++;
        done += len;
    }
    if (--acb->nr_pending) {
        qemu_coroutine_yield();
    }
}

static void sd_aio_complete(SheepdogAIOCB *acb)
{
    BDRVSheepdogState *s;
    if (acb->aiocb_type == AIOCB_FLUSH_CACHE) {
        return;
    }

    s = acb->s;
    qemu_co_mutex_lock(&s->queue_lock);
    QLIST_REMOVE(acb, aiocb_siblings);
    qemu_co_queue_restart_all(&s->overlapping_queue);
    qemu_co_mutex_unlock(&s->queue_lock);
}

static coroutine_fn int sd_co_writev(BlockDriverState *bs, int64_t sector_num,
                                     int nb_sectors, QEMUIOVector *qiov,
                                     int flags)
{
    SheepdogAIOCB acb;
    int ret;
    int64_t offset = (sector_num + nb_sectors) * BDRV_SECTOR_SIZE;
    BDRVSheepdogState *s = bs->opaque;

    assert(!flags);
    if (offset > s->inode.vdi_size) {
        ret = sd_co_truncate(bs, offset, false, PREALLOC_MODE_OFF, NULL);
        if (ret < 0) {
            return ret;
        }
    }

    sd_aio_setup(&acb, s, qiov, sector_num, nb_sectors, AIOCB_WRITE_UDATA);
    sd_co_rw_vector(&acb);
    sd_write_done(&acb);
    sd_aio_complete(&acb);

    return acb.ret;
}

static coroutine_fn int sd_co_readv(BlockDriverState *bs, int64_t sector_num,
                       int nb_sectors, QEMUIOVector *qiov)
{
    SheepdogAIOCB acb;
    BDRVSheepdogState *s = bs->opaque;

    sd_aio_setup(&acb, s, qiov, sector_num, nb_sectors, AIOCB_READ_UDATA);
    sd_co_rw_vector(&acb);
    sd_aio_complete(&acb);

    return acb.ret;
}

static int coroutine_fn sd_co_flush_to_disk(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;
    SheepdogAIOCB acb;
    AIOReq *aio_req;

    if (s->cache_flags != SD_FLAG_CMD_CACHE) {
        return 0;
    }

    sd_aio_setup(&acb, s, NULL, 0, 0, AIOCB_FLUSH_CACHE);

    acb.nr_pending++;
    aio_req = alloc_aio_req(s, &acb, vid_to_vdi_oid(s->inode.vdi_id),
                            0, 0, 0, false, 0, 0);
    add_aio_request(s, aio_req, NULL, 0, acb.aiocb_type);

    if (--acb.nr_pending) {
        qemu_coroutine_yield();
    }

    sd_aio_complete(&acb);
    return acb.ret;
}

static int sd_snapshot_create(BlockDriverState *bs, QEMUSnapshotInfo *sn_info)
{
    Error *local_err = NULL;
    BDRVSheepdogState *s = bs->opaque;
    int ret, fd;
    uint32_t new_vid;
    SheepdogInode *inode;
    unsigned int datalen;

    trace_sheepdog_snapshot_create_info(sn_info->name, sn_info->id_str, s->name,
                                        sn_info->vm_state_size, s->is_snapshot);

    if (s->is_snapshot) {
        error_report("You can't create a snapshot of a snapshot VDI, "
                     "%s (%" PRIu32 ").", s->name, s->inode.vdi_id);

        return -EINVAL;
    }

    trace_sheepdog_snapshot_create(sn_info->name, sn_info->id_str);

    s->inode.vm_state_size = sn_info->vm_state_size;
    s->inode.vm_clock_nsec = sn_info->vm_clock_nsec;
    /* It appears that inode.tag does not require a NUL terminator,
     * which means this use of strncpy is ok.
     */
    strncpy(s->inode.tag, sn_info->name, sizeof(s->inode.tag));
    /* we don't need to update entire object */
    datalen = SD_INODE_HEADER_SIZE;
    inode = g_malloc(datalen);

    /* refresh inode. */
    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        error_report_err(local_err);
        ret = fd;
        goto cleanup;
    }

    ret = write_object(fd, s->bs, (char *)&s->inode,
                       vid_to_vdi_oid(s->inode.vdi_id), s->inode.nr_copies,
                       datalen, 0, false, s->cache_flags);
    if (ret < 0) {
        error_report("failed to write snapshot's inode.");
        goto cleanup;
    }

    ret = do_sd_create(s, &new_vid, 1, &local_err);
    if (ret < 0) {
        error_reportf_err(local_err,
                          "failed to create inode for snapshot: ");
        goto cleanup;
    }

    ret = read_object(fd, s->bs, (char *)inode,
                      vid_to_vdi_oid(new_vid), s->inode.nr_copies, datalen, 0,
                      s->cache_flags);

    if (ret < 0) {
        error_report("failed to read new inode info. %s", strerror(errno));
        goto cleanup;
    }

    memcpy(&s->inode, inode, datalen);
    trace_sheepdog_snapshot_create_inode(s->inode.name, s->inode.snap_id,
                                         s->inode.vdi_id);

cleanup:
    g_free(inode);
    closesocket(fd);
    return ret;
}

/*
 * We implement rollback(loadvm) operation to the specified snapshot by
 * 1) switch to the snapshot
 * 2) rely on sd_create_branch to delete working VDI and
 * 3) create a new working VDI based on the specified snapshot
 */
static int sd_snapshot_goto(BlockDriverState *bs, const char *snapshot_id)
{
    BDRVSheepdogState *s = bs->opaque;
    BDRVSheepdogState *old_s;
    char tag[SD_MAX_VDI_TAG_LEN];
    uint32_t snapid = 0;
    int ret;

    if (!sd_parse_snapid_or_tag(snapshot_id, &snapid, tag)) {
        return -EINVAL;
    }

    old_s = g_new(BDRVSheepdogState, 1);

    memcpy(old_s, s, sizeof(BDRVSheepdogState));

    ret = reload_inode(s, snapid, tag);
    if (ret) {
        goto out;
    }

    ret = sd_create_branch(s);
    if (ret) {
        goto out;
    }

    g_free(old_s);

    return 0;
out:
    /* recover bdrv_sd_state */
    memcpy(s, old_s, sizeof(BDRVSheepdogState));
    g_free(old_s);

    error_report("failed to open. recover old bdrv_sd_state.");

    return ret;
}

#define NR_BATCHED_DISCARD 128

static int remove_objects(BDRVSheepdogState *s, Error **errp)
{
    int fd, i = 0, nr_objs = 0;
    int ret;
    SheepdogInode *inode = &s->inode;

    fd = connect_to_sdog(s, errp);
    if (fd < 0) {
        return fd;
    }

    nr_objs = count_data_objs(inode);
    while (i < nr_objs) {
        int start_idx, nr_filled_idx;

        while (i < nr_objs && !inode->data_vdi_id[i]) {
            i++;
        }
        start_idx = i;

        nr_filled_idx = 0;
        while (i < nr_objs && nr_filled_idx < NR_BATCHED_DISCARD) {
            if (inode->data_vdi_id[i]) {
                inode->data_vdi_id[i] = 0;
                nr_filled_idx++;
            }

            i++;
        }

        ret = write_object(fd, s->bs,
                           (char *)&inode->data_vdi_id[start_idx],
                           vid_to_vdi_oid(s->inode.vdi_id), inode->nr_copies,
                           (i - start_idx) * sizeof(uint32_t),
                           offsetof(struct SheepdogInode,
                                    data_vdi_id[start_idx]),
                           false, s->cache_flags);
        if (ret < 0) {
            error_setg(errp, "Failed to discard snapshot inode");
            goto out;
        }
    }

    ret = 0;
out:
    closesocket(fd);
    return ret;
}

static int sd_snapshot_delete(BlockDriverState *bs,
                              const char *snapshot_id,
                              const char *name,
                              Error **errp)
{
    /*
     * FIXME should delete the snapshot matching both @snapshot_id and
     * @name, but @name not used here
     */
    unsigned long snap_id = 0;
    char snap_tag[SD_MAX_VDI_TAG_LEN];
    int fd, ret;
    char buf[SD_MAX_VDI_LEN + SD_MAX_VDI_TAG_LEN];
    BDRVSheepdogState *s = bs->opaque;
    unsigned int wlen = SD_MAX_VDI_LEN + SD_MAX_VDI_TAG_LEN, rlen = 0;
    uint32_t vid;
    SheepdogVdiReq hdr = {
        .opcode = SD_OP_DEL_VDI,
        .data_length = wlen,
        .flags = SD_FLAG_CMD_WRITE,
    };
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;

    ret = remove_objects(s, errp);
    if (ret) {
        return ret;
    }

    memset(buf, 0, sizeof(buf));
    memset(snap_tag, 0, sizeof(snap_tag));
    pstrcpy(buf, SD_MAX_VDI_LEN, s->name);
    /* TODO Use sd_parse_snapid() once this mess is cleaned up */
    ret = qemu_strtoul(snapshot_id, NULL, 10, &snap_id);
    if (ret || snap_id > UINT32_MAX) {
        /*
         * FIXME Since qemu_strtoul() returns -EINVAL when
         * @snapshot_id is null, @snapshot_id is mandatory.  Correct
         * would be to require at least one of @snapshot_id and @name.
         */
        error_setg(errp, "Invalid snapshot ID: %s",
                         snapshot_id ? snapshot_id : "<null>");
        return -EINVAL;
    }

    if (snap_id) {
        hdr.snapid = (uint32_t) snap_id;
    } else {
        /* FIXME I suspect we should use @name here */
        /* FIXME don't truncate silently */
        pstrcpy(snap_tag, sizeof(snap_tag), snapshot_id);
        pstrcpy(buf + SD_MAX_VDI_LEN, SD_MAX_VDI_TAG_LEN, snap_tag);
    }

    ret = find_vdi_name(s, s->name, snap_id, snap_tag, &vid, true, errp);
    if (ret) {
        return ret;
    }

    fd = connect_to_sdog(s, errp);
    if (fd < 0) {
        return fd;
    }

    ret = do_req(fd, s->bs, (SheepdogReq *)&hdr,
                 buf, &wlen, &rlen);
    closesocket(fd);
    if (ret) {
        error_setg_errno(errp, -ret, "Couldn't send request to server");
        return ret;
    }

    switch (rsp->result) {
    case SD_RES_NO_VDI:
        error_setg(errp, "Can't find the snapshot");
        return -ENOENT;
    case SD_RES_SUCCESS:
        break;
    default:
        error_setg(errp, "%s", sd_strerror(rsp->result));
        return -EIO;
    }

    return 0;
}

static int sd_snapshot_list(BlockDriverState *bs, QEMUSnapshotInfo **psn_tab)
{
    Error *local_err = NULL;
    BDRVSheepdogState *s = bs->opaque;
    SheepdogReq req;
    int fd, nr = 1024, ret, max = BITS_TO_LONGS(SD_NR_VDIS) * sizeof(long);
    QEMUSnapshotInfo *sn_tab = NULL;
    unsigned wlen, rlen;
    int found = 0;
    SheepdogInode *inode;
    unsigned long *vdi_inuse;
    unsigned int start_nr;
    uint64_t hval;
    uint32_t vid;

    vdi_inuse = g_malloc(max);
    inode = g_malloc(SD_INODE_HEADER_SIZE);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        error_report_err(local_err);
        ret = fd;
        goto out;
    }

    rlen = max;
    wlen = 0;

    memset(&req, 0, sizeof(req));

    req.opcode = SD_OP_READ_VDIS;
    req.data_length = max;

    ret = do_req(fd, s->bs, &req, vdi_inuse, &wlen, &rlen);

    closesocket(fd);
    if (ret) {
        goto out;
    }

    sn_tab = g_new0(QEMUSnapshotInfo, nr);

    /* calculate a vdi id with hash function */
    hval = fnv_64a_buf(s->name, strlen(s->name), FNV1A_64_INIT);
    start_nr = hval & (SD_NR_VDIS - 1);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        error_report_err(local_err);
        ret = fd;
        goto out;
    }

    for (vid = start_nr; found < nr; vid = (vid + 1) % SD_NR_VDIS) {
        if (!test_bit(vid, vdi_inuse)) {
            break;
        }

        /* we don't need to read entire object */
        ret = read_object(fd, s->bs, (char *)inode,
                          vid_to_vdi_oid(vid),
                          0, SD_INODE_HEADER_SIZE, 0,
                          s->cache_flags);

        if (ret) {
            continue;
        }

        if (!strcmp(inode->name, s->name) && is_snapshot(inode)) {
            sn_tab[found].date_sec = inode->snap_ctime >> 32;
            sn_tab[found].date_nsec = inode->snap_ctime & 0xffffffff;
            sn_tab[found].vm_state_size = inode->vm_state_size;
            sn_tab[found].vm_clock_nsec = inode->vm_clock_nsec;

            snprintf(sn_tab[found].id_str, sizeof(sn_tab[found].id_str),
                     "%" PRIu32, inode->snap_id);
            pstrcpy(sn_tab[found].name,
                    MIN(sizeof(sn_tab[found].name), sizeof(inode->tag)),
                    inode->tag);
            found++;
        }
    }

    closesocket(fd);
out:
    *psn_tab = sn_tab;

    g_free(vdi_inuse);
    g_free(inode);

    if (ret < 0) {
        return ret;
    }

    return found;
}

static int do_load_save_vmstate(BDRVSheepdogState *s, uint8_t *data,
                                int64_t pos, int size, int load)
{
    Error *local_err = NULL;
    bool create;
    int fd, ret = 0, remaining = size;
    unsigned int data_len;
    uint64_t vmstate_oid;
    uint64_t offset;
    uint32_t vdi_index;
    uint32_t vdi_id = load ? s->inode.parent_vdi_id : s->inode.vdi_id;
    uint32_t object_size = (UINT32_C(1) << s->inode.block_size_shift);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        error_report_err(local_err);
        return fd;
    }

    while (remaining) {
        vdi_index = pos / object_size;
        offset = pos % object_size;

        data_len = MIN(remaining, object_size - offset);

        vmstate_oid = vid_to_vmstate_oid(vdi_id, vdi_index);

        create = (offset == 0);
        if (load) {
            ret = read_object(fd, s->bs, (char *)data, vmstate_oid,
                              s->inode.nr_copies, data_len, offset,
                              s->cache_flags);
        } else {
            ret = write_object(fd, s->bs, (char *)data, vmstate_oid,
                               s->inode.nr_copies, data_len, offset, create,
                               s->cache_flags);
        }

        if (ret < 0) {
            error_report("failed to save vmstate %s", strerror(errno));
            goto cleanup;
        }

        pos += data_len;
        data += data_len;
        remaining -= data_len;
    }
    ret = size;
cleanup:
    closesocket(fd);
    return ret;
}

static int sd_save_vmstate(BlockDriverState *bs, QEMUIOVector *qiov,
                           int64_t pos)
{
    BDRVSheepdogState *s = bs->opaque;
    void *buf;
    int ret;

    buf = qemu_blockalign(bs, qiov->size);
    qemu_iovec_to_buf(qiov, 0, buf, qiov->size);
    ret = do_load_save_vmstate(s, (uint8_t *) buf, pos, qiov->size, 0);
    qemu_vfree(buf);

    return ret;
}

static int sd_load_vmstate(BlockDriverState *bs, QEMUIOVector *qiov,
                           int64_t pos)
{
    BDRVSheepdogState *s = bs->opaque;
    void *buf;
    int ret;

    buf = qemu_blockalign(bs, qiov->size);
    ret = do_load_save_vmstate(s, buf, pos, qiov->size, 1);
    qemu_iovec_from_buf(qiov, 0, buf, qiov->size);
    qemu_vfree(buf);

    return ret;
}


static coroutine_fn int sd_co_pdiscard(BlockDriverState *bs, int64_t offset,
                                      int bytes)
{
    SheepdogAIOCB acb;
    BDRVSheepdogState *s = bs->opaque;
    QEMUIOVector discard_iov;
    struct iovec iov;
    uint32_t zero = 0;

    if (!s->discard_supported) {
        return 0;
    }

    memset(&discard_iov, 0, sizeof(discard_iov));
    memset(&iov, 0, sizeof(iov));
    iov.iov_base = &zero;
    iov.iov_len = sizeof(zero);
    discard_iov.iov = &iov;
    discard_iov.niov = 1;
    if (!QEMU_IS_ALIGNED(offset | bytes, BDRV_SECTOR_SIZE)) {
        return -ENOTSUP;
    }
    sd_aio_setup(&acb, s, &discard_iov, offset >> BDRV_SECTOR_BITS,
                 bytes >> BDRV_SECTOR_BITS, AIOCB_DISCARD_OBJ);
    sd_co_rw_vector(&acb);
    sd_aio_complete(&acb);

    return acb.ret;
}

static coroutine_fn int
sd_co_block_status(BlockDriverState *bs, bool want_zero, int64_t offset,
                   int64_t bytes, int64_t *pnum, int64_t *map,
                   BlockDriverState **file)
{
    BDRVSheepdogState *s = bs->opaque;
    SheepdogInode *inode = &s->inode;
    uint32_t object_size = (UINT32_C(1) << inode->block_size_shift);
    unsigned long start = offset / object_size,
                  end = DIV_ROUND_UP(offset + bytes, object_size);
    unsigned long idx;
    *map = offset;
    int ret = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;

    for (idx = start; idx < end; idx++) {
        if (inode->data_vdi_id[idx] == 0) {
            break;
        }
    }
    if (idx == start) {
        /* Get the longest length of unallocated sectors */
        ret = 0;
        for (idx = start + 1; idx < end; idx++) {
            if (inode->data_vdi_id[idx] != 0) {
                break;
            }
        }
    }

    *pnum = (idx - start) * object_size;
    if (*pnum > bytes) {
        *pnum = bytes;
    }
    if (ret > 0 && ret & BDRV_BLOCK_OFFSET_VALID) {
        *file = bs;
    }
    return ret;
}

static int64_t sd_get_allocated_file_size(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;
    SheepdogInode *inode = &s->inode;
    uint32_t object_size = (UINT32_C(1) << inode->block_size_shift);
    unsigned long i, last = DIV_ROUND_UP(inode->vdi_size, object_size);
    uint64_t size = 0;

    for (i = 0; i < last; i++) {
        if (inode->data_vdi_id[i] == 0) {
            continue;
        }
        size += object_size;
    }
    return size;
}

static QemuOptsList sd_create_opts = {
    .name = "sheepdog-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(sd_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_BACKING_FILE,
            .type = QEMU_OPT_STRING,
            .help = "File name of a base image"
        },
        {
            .name = BLOCK_OPT_PREALLOC,
            .type = QEMU_OPT_STRING,
            .help = "Preallocation mode (allowed values: off, full)"
        },
        {
            .name = BLOCK_OPT_REDUNDANCY,
            .type = QEMU_OPT_STRING,
            .help = "Redundancy of the image"
        },
        {
            .name = BLOCK_OPT_OBJECT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Object size of the image"
        },
        { /* end of list */ }
    }
};

static const char *const sd_strong_runtime_opts[] = {
    "vdi",
    "snap-id",
    "tag",
    "server.",

    NULL
};

static BlockDriver bdrv_sheepdog = {
    .format_name                  = "sheepdog",
    .protocol_name                = "sheepdog",
    .instance_size                = sizeof(BDRVSheepdogState),
    .bdrv_parse_filename          = sd_parse_filename,
    .bdrv_file_open               = sd_open,
    .bdrv_reopen_prepare          = sd_reopen_prepare,
    .bdrv_reopen_commit           = sd_reopen_commit,
    .bdrv_reopen_abort            = sd_reopen_abort,
    .bdrv_close                   = sd_close,
    .bdrv_co_create               = sd_co_create,
    .bdrv_co_create_opts          = sd_co_create_opts,
    .bdrv_has_zero_init           = bdrv_has_zero_init_1,
    .bdrv_has_zero_init_truncate  = bdrv_has_zero_init_1,
    .bdrv_getlength               = sd_getlength,
    .bdrv_get_allocated_file_size = sd_get_allocated_file_size,
    .bdrv_co_truncate             = sd_co_truncate,

    .bdrv_co_readv                = sd_co_readv,
    .bdrv_co_writev               = sd_co_writev,
    .bdrv_co_flush_to_disk        = sd_co_flush_to_disk,
    .bdrv_co_pdiscard             = sd_co_pdiscard,
    .bdrv_co_block_status         = sd_co_block_status,

    .bdrv_snapshot_create         = sd_snapshot_create,
    .bdrv_snapshot_goto           = sd_snapshot_goto,
    .bdrv_snapshot_delete         = sd_snapshot_delete,
    .bdrv_snapshot_list           = sd_snapshot_list,

    .bdrv_save_vmstate            = sd_save_vmstate,
    .bdrv_load_vmstate            = sd_load_vmstate,

    .bdrv_detach_aio_context      = sd_detach_aio_context,
    .bdrv_attach_aio_context      = sd_attach_aio_context,

    .create_opts                  = &sd_create_opts,
    .strong_runtime_opts          = sd_strong_runtime_opts,
};

static BlockDriver bdrv_sheepdog_tcp = {
    .format_name                  = "sheepdog",
    .protocol_name                = "sheepdog+tcp",
    .instance_size                = sizeof(BDRVSheepdogState),
    .bdrv_parse_filename          = sd_parse_filename,
    .bdrv_file_open               = sd_open,
    .bdrv_reopen_prepare          = sd_reopen_prepare,
    .bdrv_reopen_commit           = sd_reopen_commit,
    .bdrv_reopen_abort            = sd_reopen_abort,
    .bdrv_close                   = sd_close,
    .bdrv_co_create               = sd_co_create,
    .bdrv_co_create_opts          = sd_co_create_opts,
    .bdrv_has_zero_init           = bdrv_has_zero_init_1,
    .bdrv_has_zero_init_truncate  = bdrv_has_zero_init_1,
    .bdrv_getlength               = sd_getlength,
    .bdrv_get_allocated_file_size = sd_get_allocated_file_size,
    .bdrv_co_truncate             = sd_co_truncate,

    .bdrv_co_readv                = sd_co_readv,
    .bdrv_co_writev               = sd_co_writev,
    .bdrv_co_flush_to_disk        = sd_co_flush_to_disk,
    .bdrv_co_pdiscard             = sd_co_pdiscard,
    .bdrv_co_block_status         = sd_co_block_status,

    .bdrv_snapshot_create         = sd_snapshot_create,
    .bdrv_snapshot_goto           = sd_snapshot_goto,
    .bdrv_snapshot_delete         = sd_snapshot_delete,
    .bdrv_snapshot_list           = sd_snapshot_list,

    .bdrv_save_vmstate            = sd_save_vmstate,
    .bdrv_load_vmstate            = sd_load_vmstate,

    .bdrv_detach_aio_context      = sd_detach_aio_context,
    .bdrv_attach_aio_context      = sd_attach_aio_context,

    .create_opts                  = &sd_create_opts,
    .strong_runtime_opts          = sd_strong_runtime_opts,
};

static BlockDriver bdrv_sheepdog_unix = {
    .format_name                  = "sheepdog",
    .protocol_name                = "sheepdog+unix",
    .instance_size                = sizeof(BDRVSheepdogState),
    .bdrv_parse_filename          = sd_parse_filename,
    .bdrv_file_open               = sd_open,
    .bdrv_reopen_prepare          = sd_reopen_prepare,
    .bdrv_reopen_commit           = sd_reopen_commit,
    .bdrv_reopen_abort            = sd_reopen_abort,
    .bdrv_close                   = sd_close,
    .bdrv_co_create               = sd_co_create,
    .bdrv_co_create_opts          = sd_co_create_opts,
    .bdrv_has_zero_init           = bdrv_has_zero_init_1,
    .bdrv_has_zero_init_truncate  = bdrv_has_zero_init_1,
    .bdrv_getlength               = sd_getlength,
    .bdrv_get_allocated_file_size = sd_get_allocated_file_size,
    .bdrv_co_truncate             = sd_co_truncate,

    .bdrv_co_readv                = sd_co_readv,
    .bdrv_co_writev               = sd_co_writev,
    .bdrv_co_flush_to_disk        = sd_co_flush_to_disk,
    .bdrv_co_pdiscard             = sd_co_pdiscard,
    .bdrv_co_block_status         = sd_co_block_status,

    .bdrv_snapshot_create         = sd_snapshot_create,
    .bdrv_snapshot_goto           = sd_snapshot_goto,
    .bdrv_snapshot_delete         = sd_snapshot_delete,
    .bdrv_snapshot_list           = sd_snapshot_list,

    .bdrv_save_vmstate            = sd_save_vmstate,
    .bdrv_load_vmstate            = sd_load_vmstate,

    .bdrv_detach_aio_context      = sd_detach_aio_context,
    .bdrv_attach_aio_context      = sd_attach_aio_context,

    .create_opts                  = &sd_create_opts,
    .strong_runtime_opts          = sd_strong_runtime_opts,
};

static void bdrv_sheepdog_init(void)
{
    bdrv_register(&bdrv_sheepdog);
    bdrv_register(&bdrv_sheepdog_tcp);
    bdrv_register(&bdrv_sheepdog_unix);
}
block_init(bdrv_sheepdog_init);
