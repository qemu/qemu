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

#include "qemu-common.h"
#include "qemu/uri.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "block/block_int.h"
#include "qemu/bitops.h"

#define SD_PROTO_VER 0x01

#define SD_DEFAULT_ADDR "localhost"
#define SD_DEFAULT_PORT 7000

#define SD_OP_CREATE_AND_WRITE_OBJ  0x01
#define SD_OP_READ_OBJ       0x02
#define SD_OP_WRITE_OBJ      0x03
/* 0x04 is used internally by Sheepdog */
#define SD_OP_DISCARD_OBJ    0x05

#define SD_OP_NEW_VDI        0x11
#define SD_OP_LOCK_VDI       0x12
#define SD_OP_RELEASE_VDI    0x13
#define SD_OP_GET_VDI_INFO   0x14
#define SD_OP_READ_VDIS      0x15
#define SD_OP_FLUSH_VDI      0x16
#define SD_OP_DEL_VDI        0x17

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
    uint8_t reserved[2];
    uint32_t snapid;
    uint32_t pad[3];
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

#undef DPRINTF
#ifdef DEBUG_SDOG
#define DPRINTF(fmt, args...)                                       \
    do {                                                            \
        fprintf(stdout, "%s %d: " fmt, __func__, __LINE__, ##args); \
    } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

typedef struct SheepdogAIOCB SheepdogAIOCB;

typedef struct AIOReq {
    SheepdogAIOCB *aiocb;
    unsigned int iov_offset;

    uint64_t oid;
    uint64_t base_oid;
    uint64_t offset;
    unsigned int data_len;
    uint8_t flags;
    uint32_t id;

    QLIST_ENTRY(AIOReq) aio_siblings;
} AIOReq;

enum AIOCBState {
    AIOCB_WRITE_UDATA,
    AIOCB_READ_UDATA,
    AIOCB_FLUSH_CACHE,
    AIOCB_DISCARD_OBJ,
};

struct SheepdogAIOCB {
    BlockDriverAIOCB common;

    QEMUIOVector *qiov;

    int64_t sector_num;
    int nb_sectors;

    int ret;
    enum AIOCBState aiocb_type;

    Coroutine *coroutine;
    void (*aio_done_func)(SheepdogAIOCB *);

    bool cancelable;
    bool *finished;
    int nr_pending;
};

typedef struct BDRVSheepdogState {
    BlockDriverState *bs;

    SheepdogInode inode;

    uint32_t min_dirty_data_idx;
    uint32_t max_dirty_data_idx;

    char name[SD_MAX_VDI_LEN];
    bool is_snapshot;
    uint32_t cache_flags;
    bool discard_supported;

    char *host_spec;
    bool is_unix;
    int fd;

    CoMutex lock;
    Coroutine *co_send;
    Coroutine *co_recv;

    uint32_t aioreq_seq_num;

    /* Every aio request must be linked to either of these queues. */
    QLIST_HEAD(inflight_aio_head, AIOReq) inflight_aio_head;
    QLIST_HEAD(pending_aio_head, AIOReq) pending_aio_head;
    QLIST_HEAD(failed_aio_head, AIOReq) failed_aio_head;
} BDRVSheepdogState;

static const char * sd_strerror(int err)
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
 *    BDRVSheepdogState.  The function exits without waiting for
 *    receiving the response.
 *
 * 2. We receive the response in aio_read_response, the fd handler to
 *    the sheepdog connection.  If metadata update is needed, we send
 *    the write request to the vdi object in sd_write_done, the write
 *    completion function.  We switch back to sd_co_readv/writev after
 *    all the requests belonging to the AIOCB are finished.
 */

static inline AIOReq *alloc_aio_req(BDRVSheepdogState *s, SheepdogAIOCB *acb,
                                    uint64_t oid, unsigned int data_len,
                                    uint64_t offset, uint8_t flags,
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

    acb->nr_pending++;
    return aio_req;
}

static inline void free_aio_req(BDRVSheepdogState *s, AIOReq *aio_req)
{
    SheepdogAIOCB *acb = aio_req->aiocb;

    acb->cancelable = false;
    QLIST_REMOVE(aio_req, aio_siblings);
    g_free(aio_req);

    acb->nr_pending--;
}

static void coroutine_fn sd_finish_aiocb(SheepdogAIOCB *acb)
{
    qemu_coroutine_enter(acb->coroutine, NULL);
    if (acb->finished) {
        *acb->finished = true;
    }
    qemu_aio_release(acb);
}

/*
 * Check whether the specified acb can be canceled
 *
 * We can cancel aio when any request belonging to the acb is:
 *  - Not processed by the sheepdog server.
 *  - Not linked to the inflight queue.
 */
static bool sd_acb_cancelable(const SheepdogAIOCB *acb)
{
    BDRVSheepdogState *s = acb->common.bs->opaque;
    AIOReq *aioreq;

    if (!acb->cancelable) {
        return false;
    }

    QLIST_FOREACH(aioreq, &s->inflight_aio_head, aio_siblings) {
        if (aioreq->aiocb == acb) {
            return false;
        }
    }

    return true;
}

static void sd_aio_cancel(BlockDriverAIOCB *blockacb)
{
    SheepdogAIOCB *acb = (SheepdogAIOCB *)blockacb;
    BDRVSheepdogState *s = acb->common.bs->opaque;
    AIOReq *aioreq, *next;
    bool finished = false;

    acb->finished = &finished;
    while (!finished) {
        if (sd_acb_cancelable(acb)) {
            /* Remove outstanding requests from pending and failed queues.  */
            QLIST_FOREACH_SAFE(aioreq, &s->pending_aio_head, aio_siblings,
                               next) {
                if (aioreq->aiocb == acb) {
                    free_aio_req(s, aioreq);
                }
            }
            QLIST_FOREACH_SAFE(aioreq, &s->failed_aio_head, aio_siblings,
                               next) {
                if (aioreq->aiocb == acb) {
                    free_aio_req(s, aioreq);
                }
            }

            assert(acb->nr_pending == 0);
            sd_finish_aiocb(acb);
            return;
        }
        qemu_aio_wait();
    }
}

static const AIOCBInfo sd_aiocb_info = {
    .aiocb_size = sizeof(SheepdogAIOCB),
    .cancel = sd_aio_cancel,
};

static SheepdogAIOCB *sd_aio_setup(BlockDriverState *bs, QEMUIOVector *qiov,
                                   int64_t sector_num, int nb_sectors)
{
    SheepdogAIOCB *acb;

    acb = qemu_aio_get(&sd_aiocb_info, bs, NULL, NULL);

    acb->qiov = qiov;

    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;

    acb->aio_done_func = NULL;
    acb->cancelable = true;
    acb->finished = NULL;
    acb->coroutine = qemu_coroutine_self();
    acb->ret = 0;
    acb->nr_pending = 0;
    return acb;
}

static int connect_to_sdog(BDRVSheepdogState *s, Error **errp)
{
    int fd;

    if (s->is_unix) {
        fd = unix_connect(s->host_spec, errp);
    } else {
        fd = inet_connect(s->host_spec, errp);

        if (fd >= 0) {
            int ret = socket_set_nodelay(fd);
            if (ret < 0) {
                error_report("%s", strerror(errno));
            }
        }
    }

    if (fd >= 0) {
        qemu_set_nonblock(fd);
    }

    return fd;
}

static coroutine_fn int send_co_req(int sockfd, SheepdogReq *hdr, void *data,
                                    unsigned int *wlen)
{
    int ret;

    ret = qemu_co_send(sockfd, hdr, sizeof(*hdr));
    if (ret != sizeof(*hdr)) {
        error_report("failed to send a req, %s", strerror(errno));
        return ret;
    }

    ret = qemu_co_send(sockfd, data, *wlen);
    if (ret != *wlen) {
        error_report("failed to send a req, %s", strerror(errno));
    }

    return ret;
}

static void restart_co_req(void *opaque)
{
    Coroutine *co = opaque;

    qemu_coroutine_enter(co, NULL);
}

typedef struct SheepdogReqCo {
    int sockfd;
    SheepdogReq *hdr;
    void *data;
    unsigned int *wlen;
    unsigned int *rlen;
    int ret;
    bool finished;
} SheepdogReqCo;

static coroutine_fn void do_co_req(void *opaque)
{
    int ret;
    Coroutine *co;
    SheepdogReqCo *srco = opaque;
    int sockfd = srco->sockfd;
    SheepdogReq *hdr = srco->hdr;
    void *data = srco->data;
    unsigned int *wlen = srco->wlen;
    unsigned int *rlen = srco->rlen;

    co = qemu_coroutine_self();
    qemu_aio_set_fd_handler(sockfd, NULL, restart_co_req, co);

    ret = send_co_req(sockfd, hdr, data, wlen);
    if (ret < 0) {
        goto out;
    }

    qemu_aio_set_fd_handler(sockfd, restart_co_req, NULL, co);

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
    qemu_aio_set_fd_handler(sockfd, NULL, NULL, NULL);

    srco->ret = ret;
    srco->finished = true;
}

static int do_req(int sockfd, SheepdogReq *hdr, void *data,
                  unsigned int *wlen, unsigned int *rlen)
{
    Coroutine *co;
    SheepdogReqCo srco = {
        .sockfd = sockfd,
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
        co = qemu_coroutine_create(do_co_req);
        qemu_coroutine_enter(co, &srco);
        while (!srco.finished) {
            qemu_aio_wait();
        }
    }

    return srco.ret;
}

static void coroutine_fn add_aio_request(BDRVSheepdogState *s, AIOReq *aio_req,
                           struct iovec *iov, int niov, bool create,
                           enum AIOCBState aiocb_type);
static void coroutine_fn resend_aioreq(BDRVSheepdogState *s, AIOReq *aio_req);
static int reload_inode(BDRVSheepdogState *s, uint32_t snapid, const char *tag);
static int get_sheep_fd(BDRVSheepdogState *s, Error **errp);
static void co_write_request(void *opaque);

static AIOReq *find_pending_req(BDRVSheepdogState *s, uint64_t oid)
{
    AIOReq *aio_req;

    QLIST_FOREACH(aio_req, &s->pending_aio_head, aio_siblings) {
        if (aio_req->oid == oid) {
            return aio_req;
        }
    }

    return NULL;
}

/*
 * This function searchs pending requests to the object `oid', and
 * sends them.
 */
static void coroutine_fn send_pending_req(BDRVSheepdogState *s, uint64_t oid)
{
    AIOReq *aio_req;
    SheepdogAIOCB *acb;

    while ((aio_req = find_pending_req(s, oid)) != NULL) {
        acb = aio_req->aiocb;
        /* move aio_req from pending list to inflight one */
        QLIST_REMOVE(aio_req, aio_siblings);
        QLIST_INSERT_HEAD(&s->inflight_aio_head, aio_req, aio_siblings);
        add_aio_request(s, aio_req, acb->qiov->iov, acb->qiov->niov, false,
                        acb->aiocb_type);
    }
}

static coroutine_fn void reconnect_to_sdog(void *opaque)
{
    Error *local_err = NULL;
    BDRVSheepdogState *s = opaque;
    AIOReq *aio_req, *next;

    qemu_aio_set_fd_handler(s->fd, NULL, NULL, NULL);
    close(s->fd);
    s->fd = -1;

    /* Wait for outstanding write requests to be completed. */
    while (s->co_send != NULL) {
        co_write_request(opaque);
    }

    /* Try to reconnect the sheepdog server every one second. */
    while (s->fd < 0) {
        s->fd = get_sheep_fd(s, &local_err);
        if (s->fd < 0) {
            DPRINTF("Wait for connection to be established\n");
            qerror_report_err(local_err);
            error_free(local_err);
            co_aio_sleep_ns(bdrv_get_aio_context(s->bs), QEMU_CLOCK_REALTIME,
                            1000000000ULL);
        }
    };

    /*
     * Now we have to resend all the request in the inflight queue.  However,
     * resend_aioreq() can yield and newly created requests can be added to the
     * inflight queue before the coroutine is resumed.  To avoid mixing them, we
     * have to move all the inflight requests to the failed queue before
     * resend_aioreq() is called.
     */
    QLIST_FOREACH_SAFE(aio_req, &s->inflight_aio_head, aio_siblings, next) {
        QLIST_REMOVE(aio_req, aio_siblings);
        QLIST_INSERT_HEAD(&s->failed_aio_head, aio_req, aio_siblings);
    }

    /* Resend all the failed aio requests. */
    while (!QLIST_EMPTY(&s->failed_aio_head)) {
        aio_req = QLIST_FIRST(&s->failed_aio_head);
        QLIST_REMOVE(aio_req, aio_siblings);
        QLIST_INSERT_HEAD(&s->inflight_aio_head, aio_req, aio_siblings);
        resend_aioreq(s, aio_req);
    }
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
        /* this coroutine context is no longer suitable for co_recv
         * because we may send data to update vdi objects */
        s->co_recv = NULL;
        if (!is_data_obj(aio_req->oid)) {
            break;
        }
        idx = data_oid_to_idx(aio_req->oid);

        if (s->inode.data_vdi_id[idx] != s->inode.vdi_id) {
            /*
             * If the object is newly created one, we need to update
             * the vdi object (metadata object).  min_dirty_data_idx
             * and max_dirty_data_idx are changed to include updated
             * index between them.
             */
            if (rsp.result == SD_RES_SUCCESS) {
                s->inode.data_vdi_id[idx] = s->inode.vdi_id;
                s->max_dirty_data_idx = MAX(idx, s->max_dirty_data_idx);
                s->min_dirty_data_idx = MIN(idx, s->min_dirty_data_idx);
            }
            /*
             * Some requests may be blocked because simultaneous
             * create requests are not allowed, so we search the
             * pending requests here.
             */
            send_pending_req(s, aio_req->oid);
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
            DPRINTF("disable cache since the server doesn't support it\n");
            s->cache_flags = SD_FLAG_CMD_DIRECT;
            rsp.result = SD_RES_SUCCESS;
        }
        break;
    case AIOCB_DISCARD_OBJ:
        switch (rsp.result) {
        case SD_RES_INVALID_PARMS:
            error_report("sheep(%s) doesn't support discard command",
                         s->host_spec);
            rsp.result = SD_RES_SUCCESS;
            s->discard_supported = false;
            break;
        case SD_RES_SUCCESS:
            idx = data_oid_to_idx(aio_req->oid);
            s->inode.data_vdi_id[idx] = 0;
            break;
        default:
            break;
        }
    }

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
        goto out;
    default:
        acb->ret = -EIO;
        error_report("%s", sd_strerror(rsp.result));
        break;
    }

    free_aio_req(s, aio_req);
    if (!acb->nr_pending) {
        /*
         * We've finished all requests which belong to the AIOCB, so
         * we can switch back to sd_co_readv/writev now.
         */
        acb->aio_done_func(acb);
    }
out:
    s->co_recv = NULL;
    return;
err:
    s->co_recv = NULL;
    reconnect_to_sdog(opaque);
}

static void co_read_response(void *opaque)
{
    BDRVSheepdogState *s = opaque;

    if (!s->co_recv) {
        s->co_recv = qemu_coroutine_create(aio_read_response);
    }

    qemu_coroutine_enter(s->co_recv, opaque);
}

static void co_write_request(void *opaque)
{
    BDRVSheepdogState *s = opaque;

    qemu_coroutine_enter(s->co_send, NULL);
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

    qemu_aio_set_fd_handler(fd, co_read_response, NULL, s);
    return fd;
}

static int sd_parse_uri(BDRVSheepdogState *s, const char *filename,
                        char *vdi, uint32_t *snapid, char *tag)
{
    URI *uri;
    QueryParams *qp = NULL;
    int ret = 0;

    uri = uri_parse(filename);
    if (!uri) {
        return -EINVAL;
    }

    /* transport */
    if (!strcmp(uri->scheme, "sheepdog")) {
        s->is_unix = false;
    } else if (!strcmp(uri->scheme, "sheepdog+tcp")) {
        s->is_unix = false;
    } else if (!strcmp(uri->scheme, "sheepdog+unix")) {
        s->is_unix = true;
    } else {
        ret = -EINVAL;
        goto out;
    }

    if (uri->path == NULL || !strcmp(uri->path, "/")) {
        ret = -EINVAL;
        goto out;
    }
    pstrcpy(vdi, SD_MAX_VDI_LEN, uri->path + 1);

    qp = query_params_parse(uri->query);
    if (qp->n > 1 || (s->is_unix && !qp->n) || (!s->is_unix && qp->n)) {
        ret = -EINVAL;
        goto out;
    }

    if (s->is_unix) {
        /* sheepdog+unix:///vdiname?socket=path */
        if (uri->server || uri->port || strcmp(qp->p[0].name, "socket")) {
            ret = -EINVAL;
            goto out;
        }
        s->host_spec = g_strdup(qp->p[0].value);
    } else {
        /* sheepdog[+tcp]://[host:port]/vdiname */
        s->host_spec = g_strdup_printf("%s:%d", uri->server ?: SD_DEFAULT_ADDR,
                                       uri->port ?: SD_DEFAULT_PORT);
    }

    /* snapshot tag */
    if (uri->fragment) {
        *snapid = strtoul(uri->fragment, NULL, 10);
        if (*snapid == 0) {
            pstrcpy(tag, SD_MAX_VDI_TAG_LEN, uri->fragment);
        }
    } else {
        *snapid = CURRENT_VDI_ID; /* search current vdi */
    }

out:
    if (qp) {
        query_params_free(qp);
    }
    uri_free(uri);
    return ret;
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
static int parse_vdiname(BDRVSheepdogState *s, const char *filename,
                         char *vdi, uint32_t *snapid, char *tag)
{
    char *p, *q, *uri;
    const char *host_spec, *vdi_spec;
    int nr_sep, ret;

    strstart(filename, "sheepdog:", (const char **)&filename);
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

    ret = sd_parse_uri(s, uri, vdi, snapid, tag);

    g_free(q);
    g_free(uri);

    return ret;
}

static int find_vdi_name(BDRVSheepdogState *s, const char *filename,
                         uint32_t snapid, const char *tag, uint32_t *vid,
                         bool lock)
{
    Error *local_err = NULL;
    int ret, fd;
    SheepdogVdiReq hdr;
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    unsigned int wlen, rlen = 0;
    char buf[SD_MAX_VDI_LEN + SD_MAX_VDI_TAG_LEN];

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
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
    } else {
        hdr.opcode = SD_OP_GET_VDI_INFO;
    }
    wlen = SD_MAX_VDI_LEN + SD_MAX_VDI_TAG_LEN;
    hdr.proto_ver = SD_PROTO_VER;
    hdr.data_length = wlen;
    hdr.snapid = snapid;
    hdr.flags = SD_FLAG_CMD_WRITE;

    ret = do_req(fd, (SheepdogReq *)&hdr, buf, &wlen, &rlen);
    if (ret) {
        goto out;
    }

    if (rsp->result != SD_RES_SUCCESS) {
        error_report("cannot get vdi info, %s, %s %" PRIu32 " %s",
                     sd_strerror(rsp->result), filename, snapid, tag);
        if (rsp->result == SD_RES_NO_VDI) {
            ret = -ENOENT;
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
                           struct iovec *iov, int niov, bool create,
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
        hdr.opcode = SD_OP_DISCARD_OBJ;
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
    qemu_aio_set_fd_handler(s->fd, co_read_response, co_write_request, s);
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
    qemu_aio_set_fd_handler(s->fd, co_read_response, NULL, s);
    s->co_send = NULL;
    qemu_co_mutex_unlock(&s->lock);
}

static int read_write_object(int fd, char *buf, uint64_t oid, uint8_t copies,
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

    ret = do_req(fd, (SheepdogReq *)&hdr, buf, &wlen, &rlen);
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

static int read_object(int fd, char *buf, uint64_t oid, uint8_t copies,
                       unsigned int datalen, uint64_t offset,
                       uint32_t cache_flags)
{
    return read_write_object(fd, buf, oid, copies, datalen, offset, false,
                             false, cache_flags);
}

static int write_object(int fd, char *buf, uint64_t oid, uint8_t copies,
                        unsigned int datalen, uint64_t offset, bool create,
                        uint32_t cache_flags)
{
    return read_write_object(fd, buf, oid, copies, datalen, offset, true,
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
        qerror_report_err(local_err);
        error_free(local_err);
        return -EIO;
    }

    inode = g_malloc(sizeof(s->inode));

    ret = find_vdi_name(s, s->name, snapid, tag, &vid, false);
    if (ret) {
        goto out;
    }

    ret = read_object(fd, (char *)inode, vid_to_vdi_oid(vid),
                      s->inode.nr_copies, sizeof(*inode), 0, s->cache_flags);
    if (ret < 0) {
        goto out;
    }

    if (inode->vdi_id != s->inode.vdi_id) {
        memcpy(&s->inode, inode, sizeof(s->inode));
    }

out:
    g_free(inode);
    closesocket(fd);

    return ret;
}

/* Return true if the specified request is linked to the pending list. */
static bool check_simultaneous_create(BDRVSheepdogState *s, AIOReq *aio_req)
{
    AIOReq *areq;
    QLIST_FOREACH(areq, &s->inflight_aio_head, aio_siblings) {
        if (areq != aio_req && areq->oid == aio_req->oid) {
            /*
             * Sheepdog cannot handle simultaneous create requests to the same
             * object, so we cannot send the request until the previous request
             * finishes.
             */
            DPRINTF("simultaneous create to %" PRIx64 "\n", aio_req->oid);
            aio_req->flags = 0;
            aio_req->base_oid = 0;
            QLIST_REMOVE(aio_req, aio_siblings);
            QLIST_INSERT_HEAD(&s->pending_aio_head, aio_req, aio_siblings);
            return true;
        }
    }

    return false;
}

static void coroutine_fn resend_aioreq(BDRVSheepdogState *s, AIOReq *aio_req)
{
    SheepdogAIOCB *acb = aio_req->aiocb;
    bool create = false;

    /* check whether this request becomes a CoW one */
    if (acb->aiocb_type == AIOCB_WRITE_UDATA && is_data_obj(aio_req->oid)) {
        int idx = data_oid_to_idx(aio_req->oid);

        if (is_data_obj_writable(&s->inode, idx)) {
            goto out;
        }

        if (check_simultaneous_create(s, aio_req)) {
            return;
        }

        if (s->inode.data_vdi_id[idx]) {
            aio_req->base_oid = vid_to_data_oid(s->inode.data_vdi_id[idx], idx);
            aio_req->flags |= SD_FLAG_CMD_COW;
        }
        create = true;
    }
out:
    if (is_data_obj(aio_req->oid)) {
        add_aio_request(s, aio_req, acb->qiov->iov, acb->qiov->niov, create,
                        acb->aiocb_type);
    } else {
        struct iovec iov;
        iov.iov_base = &s->inode;
        iov.iov_len = sizeof(s->inode);
        add_aio_request(s, aio_req, &iov, 1, false, AIOCB_WRITE_UDATA);
    }
}

/* TODO Convert to fine grained options */
static QemuOptsList runtime_opts = {
    .name = "sheepdog",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "URL to the sheepdog image",
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
    char vdi[SD_MAX_VDI_LEN], tag[SD_MAX_VDI_TAG_LEN];
    uint32_t snapid;
    char *buf = NULL;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *filename;

    s->bs = bs;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        ret = -EINVAL;
        goto out;
    }

    filename = qemu_opt_get(opts, "filename");

    QLIST_INIT(&s->inflight_aio_head);
    QLIST_INIT(&s->pending_aio_head);
    QLIST_INIT(&s->failed_aio_head);
    s->fd = -1;

    memset(vdi, 0, sizeof(vdi));
    memset(tag, 0, sizeof(tag));

    if (strstr(filename, "://")) {
        ret = sd_parse_uri(s, filename, vdi, &snapid, tag);
    } else {
        ret = parse_vdiname(s, filename, vdi, &snapid, tag);
    }
    if (ret < 0) {
        goto out;
    }
    s->fd = get_sheep_fd(s, &local_err);
    if (s->fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        ret = s->fd;
        goto out;
    }

    ret = find_vdi_name(s, vdi, snapid, tag, &vid, true);
    if (ret) {
        goto out;
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

    if (snapid || tag[0] != '\0') {
        DPRINTF("%" PRIx32 " snapshot inode was open.\n", vid);
        s->is_snapshot = true;
    }

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        ret = fd;
        goto out;
    }

    buf = g_malloc(SD_INODE_SIZE);
    ret = read_object(fd, buf, vid_to_vdi_oid(vid), 0, SD_INODE_SIZE, 0,
                      s->cache_flags);

    closesocket(fd);

    if (ret) {
        goto out;
    }

    memcpy(&s->inode, buf, sizeof(s->inode));
    s->min_dirty_data_idx = UINT32_MAX;
    s->max_dirty_data_idx = 0;

    bs->total_sectors = s->inode.vdi_size / BDRV_SECTOR_SIZE;
    pstrcpy(s->name, sizeof(s->name), vdi);
    qemu_co_mutex_init(&s->lock);
    qemu_opts_del(opts);
    g_free(buf);
    return 0;
out:
    qemu_aio_set_fd_handler(s->fd, NULL, NULL, NULL);
    if (s->fd >= 0) {
        closesocket(s->fd);
    }
    qemu_opts_del(opts);
    g_free(buf);
    return ret;
}

static int do_sd_create(BDRVSheepdogState *s, uint32_t *vdi_id, int snapshot)
{
    Error *local_err = NULL;
    SheepdogVdiReq hdr;
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    int fd, ret;
    unsigned int wlen, rlen = 0;
    char buf[SD_MAX_VDI_LEN];

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
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

    ret = do_req(fd, (SheepdogReq *)&hdr, buf, &wlen, &rlen);

    closesocket(fd);

    if (ret) {
        return ret;
    }

    if (rsp->result != SD_RES_SUCCESS) {
        error_report("%s, %s", sd_strerror(rsp->result), s->inode.name);
        return -EIO;
    }

    if (vdi_id) {
        *vdi_id = rsp->vdi_id;
    }

    return 0;
}

static int sd_prealloc(const char *filename, Error **errp)
{
    BlockDriverState *bs = NULL;
    uint32_t idx, max_idx;
    int64_t vdi_size;
    void *buf = g_malloc0(SD_DATA_OBJ_SIZE);
    int ret;

    ret = bdrv_open(&bs, filename, NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                    NULL, errp);
    if (ret < 0) {
        goto out_with_err_set;
    }

    vdi_size = bdrv_getlength(bs);
    if (vdi_size < 0) {
        ret = vdi_size;
        goto out;
    }
    max_idx = DIV_ROUND_UP(vdi_size, SD_DATA_OBJ_SIZE);

    for (idx = 0; idx < max_idx; idx++) {
        /*
         * The created image can be a cloned image, so we need to read
         * a data from the source image.
         */
        ret = bdrv_pread(bs, idx * SD_DATA_OBJ_SIZE, buf, SD_DATA_OBJ_SIZE);
        if (ret < 0) {
            goto out;
        }
        ret = bdrv_pwrite(bs, idx * SD_DATA_OBJ_SIZE, buf, SD_DATA_OBJ_SIZE);
        if (ret < 0) {
            goto out;
        }
    }

out:
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't pre-allocate");
    }
out_with_err_set:
    if (bs) {
        bdrv_unref(bs);
    }
    g_free(buf);

    return ret;
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
static int parse_redundancy(BDRVSheepdogState *s, const char *opt)
{
    struct SheepdogInode *inode = &s->inode;
    const char *n1, *n2;
    long copy, parity;
    char p[10];

    pstrcpy(p, sizeof(p), opt);
    n1 = strtok(p, ":");
    n2 = strtok(NULL, ":");

    if (!n1) {
        return -EINVAL;
    }

    copy = strtol(n1, NULL, 10);
    if (copy > SD_MAX_COPIES || copy < 1) {
        return -EINVAL;
    }
    if (!n2) {
        inode->copy_policy = 0;
        inode->nr_copies = copy;
        return 0;
    }

    if (copy != 2 && copy != 4 && copy != 8 && copy != 16) {
        return -EINVAL;
    }

    parity = strtol(n2, NULL, 10);
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

static int sd_create(const char *filename, QEMUOptionParameter *options,
                     Error **errp)
{
    int ret = 0;
    uint32_t vid = 0;
    char *backing_file = NULL;
    BDRVSheepdogState *s;
    char tag[SD_MAX_VDI_TAG_LEN];
    uint32_t snapid;
    bool prealloc = false;
    Error *local_err = NULL;

    s = g_malloc0(sizeof(BDRVSheepdogState));

    memset(tag, 0, sizeof(tag));
    if (strstr(filename, "://")) {
        ret = sd_parse_uri(s, filename, s->name, &snapid, tag);
    } else {
        ret = parse_vdiname(s, filename, s->name, &snapid, tag);
    }
    if (ret < 0) {
        goto out;
    }

    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            s->inode.vdi_size = options->value.n;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FILE)) {
            backing_file = options->value.s;
        } else if (!strcmp(options->name, BLOCK_OPT_PREALLOC)) {
            if (!options->value.s || !strcmp(options->value.s, "off")) {
                prealloc = false;
            } else if (!strcmp(options->value.s, "full")) {
                prealloc = true;
            } else {
                error_report("Invalid preallocation mode: '%s'",
                             options->value.s);
                ret = -EINVAL;
                goto out;
            }
        } else if (!strcmp(options->name, BLOCK_OPT_REDUNDANCY)) {
            if (options->value.s) {
                ret = parse_redundancy(s, options->value.s);
                if (ret < 0) {
                    goto out;
                }
            }
        }
        options++;
    }

    if (s->inode.vdi_size > SD_MAX_VDI_SIZE) {
        error_report("too big image size");
        ret = -EINVAL;
        goto out;
    }

    if (backing_file) {
        BlockDriverState *bs;
        BDRVSheepdogState *base;
        BlockDriver *drv;

        /* Currently, only Sheepdog backing image is supported. */
        drv = bdrv_find_protocol(backing_file, true);
        if (!drv || strcmp(drv->protocol_name, "sheepdog") != 0) {
            error_report("backing_file must be a sheepdog image");
            ret = -EINVAL;
            goto out;
        }

        bs = NULL;
        ret = bdrv_open(&bs, backing_file, NULL, NULL, BDRV_O_PROTOCOL, NULL,
                        &local_err);
        if (ret < 0) {
            qerror_report_err(local_err);
            error_free(local_err);
            goto out;
        }

        base = bs->opaque;

        if (!is_snapshot(&base->inode)) {
            error_report("cannot clone from a non snapshot vdi");
            bdrv_unref(bs);
            ret = -EINVAL;
            goto out;
        }
        s->inode.vdi_id = base->inode.vdi_id;
        bdrv_unref(bs);
    }

    ret = do_sd_create(s, &vid, 0);
    if (!prealloc || ret) {
        goto out;
    }

    ret = sd_prealloc(filename, &local_err);
    if (ret < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
    }
out:
    g_free(s);
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

    DPRINTF("%s\n", s->name);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        return;
    }

    memset(&hdr, 0, sizeof(hdr));

    hdr.opcode = SD_OP_RELEASE_VDI;
    hdr.base_vdi_id = s->inode.vdi_id;
    wlen = strlen(s->name) + 1;
    hdr.data_length = wlen;
    hdr.flags = SD_FLAG_CMD_WRITE;

    ret = do_req(fd, (SheepdogReq *)&hdr, s->name, &wlen, &rlen);

    closesocket(fd);

    if (!ret && rsp->result != SD_RES_SUCCESS &&
        rsp->result != SD_RES_VDI_NOT_LOCKED) {
        error_report("%s, %s", sd_strerror(rsp->result), s->name);
    }

    qemu_aio_set_fd_handler(s->fd, NULL, NULL, NULL);
    closesocket(s->fd);
    g_free(s->host_spec);
}

static int64_t sd_getlength(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;

    return s->inode.vdi_size;
}

static int sd_truncate(BlockDriverState *bs, int64_t offset)
{
    Error *local_err = NULL;
    BDRVSheepdogState *s = bs->opaque;
    int ret, fd;
    unsigned int datalen;

    if (offset < s->inode.vdi_size) {
        error_report("shrinking is not supported");
        return -EINVAL;
    } else if (offset > SD_MAX_VDI_SIZE) {
        error_report("too big image size");
        return -EINVAL;
    }

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        return fd;
    }

    /* we don't need to update entire object */
    datalen = SD_INODE_SIZE - sizeof(s->inode.data_vdi_id);
    s->inode.vdi_size = offset;
    ret = write_object(fd, (char *)&s->inode, vid_to_vdi_oid(s->inode.vdi_id),
                       s->inode.nr_copies, datalen, 0, false, s->cache_flags);
    close(fd);

    if (ret < 0) {
        error_report("failed to update an inode.");
    }

    return ret;
}

/*
 * This function is called after writing data objects.  If we need to
 * update metadata, this sends a write request to the vdi object.
 * Otherwise, this switches back to sd_co_readv/writev.
 */
static void coroutine_fn sd_write_done(SheepdogAIOCB *acb)
{
    BDRVSheepdogState *s = acb->common.bs->opaque;
    struct iovec iov;
    AIOReq *aio_req;
    uint32_t offset, data_len, mn, mx;

    mn = s->min_dirty_data_idx;
    mx = s->max_dirty_data_idx;
    if (mn <= mx) {
        /* we need to update the vdi object. */
        offset = sizeof(s->inode) - sizeof(s->inode.data_vdi_id) +
            mn * sizeof(s->inode.data_vdi_id[0]);
        data_len = (mx - mn + 1) * sizeof(s->inode.data_vdi_id[0]);

        s->min_dirty_data_idx = UINT32_MAX;
        s->max_dirty_data_idx = 0;

        iov.iov_base = &s->inode;
        iov.iov_len = sizeof(s->inode);
        aio_req = alloc_aio_req(s, acb, vid_to_vdi_oid(s->inode.vdi_id),
                                data_len, offset, 0, 0, offset);
        QLIST_INSERT_HEAD(&s->inflight_aio_head, aio_req, aio_siblings);
        add_aio_request(s, aio_req, &iov, 1, false, AIOCB_WRITE_UDATA);

        acb->aio_done_func = sd_finish_aiocb;
        acb->aiocb_type = AIOCB_WRITE_UDATA;
        return;
    }

    sd_finish_aiocb(acb);
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
        qerror_report_err(local_err);
        error_free(local_err);
        return false;
    }

    ret = do_req(fd, (SheepdogReq *)&hdr, s->name, &wlen, &rlen);
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

    DPRINTF("%" PRIx32 " is snapshot.\n", s->inode.vdi_id);

    buf = g_malloc(SD_INODE_SIZE);

    /*
     * Even If deletion fails, we will just create extra snapshot based on
     * the working VDI which was supposed to be deleted. So no need to
     * false bail out.
     */
    deleted = sd_delete(s);
    ret = do_sd_create(s, &vid, !deleted);
    if (ret) {
        goto out;
    }

    DPRINTF("%" PRIx32 " is created.\n", vid);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        ret = fd;
        goto out;
    }

    ret = read_object(fd, buf, vid_to_vdi_oid(vid), s->inode.nr_copies,
                      SD_INODE_SIZE, 0, s->cache_flags);

    closesocket(fd);

    if (ret < 0) {
        goto out;
    }

    memcpy(&s->inode, buf, sizeof(s->inode));

    s->is_snapshot = false;
    ret = 0;
    DPRINTF("%" PRIx32 " was newly created.\n", s->inode.vdi_id);

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
static int coroutine_fn sd_co_rw_vector(void *p)
{
    SheepdogAIOCB *acb = p;
    int ret = 0;
    unsigned long len, done = 0, total = acb->nb_sectors * BDRV_SECTOR_SIZE;
    unsigned long idx = acb->sector_num * BDRV_SECTOR_SIZE / SD_DATA_OBJ_SIZE;
    uint64_t oid;
    uint64_t offset = (acb->sector_num * BDRV_SECTOR_SIZE) % SD_DATA_OBJ_SIZE;
    BDRVSheepdogState *s = acb->common.bs->opaque;
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
            goto out;
        }
    }

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

        len = MIN(total - done, SD_DATA_OBJ_SIZE - offset);

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
            if (len != SD_DATA_OBJ_SIZE || inode->data_vdi_id[idx] == 0) {
                goto done;
            }
            break;
        default:
            break;
        }

        if (create) {
            DPRINTF("update ino (%" PRIu32 ") %" PRIu64 " %" PRIu64 " %ld\n",
                    inode->vdi_id, oid,
                    vid_to_data_oid(inode->data_vdi_id[idx], idx), idx);
            oid = vid_to_data_oid(inode->vdi_id, idx);
            DPRINTF("new oid %" PRIx64 "\n", oid);
        }

        aio_req = alloc_aio_req(s, acb, oid, len, offset, flags, old_oid, done);
        QLIST_INSERT_HEAD(&s->inflight_aio_head, aio_req, aio_siblings);

        if (create) {
            if (check_simultaneous_create(s, aio_req)) {
                goto done;
            }
        }

        add_aio_request(s, aio_req, acb->qiov->iov, acb->qiov->niov, create,
                        acb->aiocb_type);
    done:
        offset = 0;
        idx++;
        done += len;
    }
out:
    if (!--acb->nr_pending) {
        return acb->ret;
    }
    return 1;
}

static coroutine_fn int sd_co_writev(BlockDriverState *bs, int64_t sector_num,
                        int nb_sectors, QEMUIOVector *qiov)
{
    SheepdogAIOCB *acb;
    int ret;
    int64_t offset = (sector_num + nb_sectors) * BDRV_SECTOR_SIZE;
    BDRVSheepdogState *s = bs->opaque;

    if (bs->growable && offset > s->inode.vdi_size) {
        ret = sd_truncate(bs, offset);
        if (ret < 0) {
            return ret;
        }
    }

    acb = sd_aio_setup(bs, qiov, sector_num, nb_sectors);
    acb->aio_done_func = sd_write_done;
    acb->aiocb_type = AIOCB_WRITE_UDATA;

    ret = sd_co_rw_vector(acb);
    if (ret <= 0) {
        qemu_aio_release(acb);
        return ret;
    }

    qemu_coroutine_yield();

    return acb->ret;
}

static coroutine_fn int sd_co_readv(BlockDriverState *bs, int64_t sector_num,
                       int nb_sectors, QEMUIOVector *qiov)
{
    SheepdogAIOCB *acb;
    int ret;

    acb = sd_aio_setup(bs, qiov, sector_num, nb_sectors);
    acb->aiocb_type = AIOCB_READ_UDATA;
    acb->aio_done_func = sd_finish_aiocb;

    ret = sd_co_rw_vector(acb);
    if (ret <= 0) {
        qemu_aio_release(acb);
        return ret;
    }

    qemu_coroutine_yield();

    return acb->ret;
}

static int coroutine_fn sd_co_flush_to_disk(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;
    SheepdogAIOCB *acb;
    AIOReq *aio_req;

    if (s->cache_flags != SD_FLAG_CMD_CACHE) {
        return 0;
    }

    acb = sd_aio_setup(bs, NULL, 0, 0);
    acb->aiocb_type = AIOCB_FLUSH_CACHE;
    acb->aio_done_func = sd_finish_aiocb;

    aio_req = alloc_aio_req(s, acb, vid_to_vdi_oid(s->inode.vdi_id),
                            0, 0, 0, 0, 0);
    QLIST_INSERT_HEAD(&s->inflight_aio_head, aio_req, aio_siblings);
    add_aio_request(s, aio_req, NULL, 0, false, acb->aiocb_type);

    qemu_coroutine_yield();
    return acb->ret;
}

static int sd_snapshot_create(BlockDriverState *bs, QEMUSnapshotInfo *sn_info)
{
    Error *local_err = NULL;
    BDRVSheepdogState *s = bs->opaque;
    int ret, fd;
    uint32_t new_vid;
    SheepdogInode *inode;
    unsigned int datalen;

    DPRINTF("sn_info: name %s id_str %s s: name %s vm_state_size %" PRId64 " "
            "is_snapshot %d\n", sn_info->name, sn_info->id_str,
            s->name, sn_info->vm_state_size, s->is_snapshot);

    if (s->is_snapshot) {
        error_report("You can't create a snapshot of a snapshot VDI, "
                     "%s (%" PRIu32 ").", s->name, s->inode.vdi_id);

        return -EINVAL;
    }

    DPRINTF("%s %s\n", sn_info->name, sn_info->id_str);

    s->inode.vm_state_size = sn_info->vm_state_size;
    s->inode.vm_clock_nsec = sn_info->vm_clock_nsec;
    /* It appears that inode.tag does not require a NUL terminator,
     * which means this use of strncpy is ok.
     */
    strncpy(s->inode.tag, sn_info->name, sizeof(s->inode.tag));
    /* we don't need to update entire object */
    datalen = SD_INODE_SIZE - sizeof(s->inode.data_vdi_id);

    /* refresh inode. */
    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        ret = fd;
        goto cleanup;
    }

    ret = write_object(fd, (char *)&s->inode, vid_to_vdi_oid(s->inode.vdi_id),
                       s->inode.nr_copies, datalen, 0, false, s->cache_flags);
    if (ret < 0) {
        error_report("failed to write snapshot's inode.");
        goto cleanup;
    }

    ret = do_sd_create(s, &new_vid, 1);
    if (ret < 0) {
        error_report("failed to create inode for snapshot. %s",
                     strerror(errno));
        goto cleanup;
    }

    inode = (SheepdogInode *)g_malloc(datalen);

    ret = read_object(fd, (char *)inode, vid_to_vdi_oid(new_vid),
                      s->inode.nr_copies, datalen, 0, s->cache_flags);

    if (ret < 0) {
        error_report("failed to read new inode info. %s", strerror(errno));
        goto cleanup;
    }

    memcpy(&s->inode, inode, datalen);
    DPRINTF("s->inode: name %s snap_id %x oid %x\n",
            s->inode.name, s->inode.snap_id, s->inode.vdi_id);

cleanup:
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
    int ret = 0;

    old_s = g_malloc(sizeof(BDRVSheepdogState));

    memcpy(old_s, s, sizeof(BDRVSheepdogState));

    snapid = strtoul(snapshot_id, NULL, 10);
    if (snapid) {
        tag[0] = 0;
    } else {
        pstrcpy(tag, sizeof(tag), snapshot_id);
    }

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

static int sd_snapshot_delete(BlockDriverState *bs,
                              const char *snapshot_id,
                              const char *name,
                              Error **errp)
{
    /* FIXME: Delete specified snapshot id.  */
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
    static SheepdogInode inode;
    unsigned long *vdi_inuse;
    unsigned int start_nr;
    uint64_t hval;
    uint32_t vid;

    vdi_inuse = g_malloc(max);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        ret = fd;
        goto out;
    }

    rlen = max;
    wlen = 0;

    memset(&req, 0, sizeof(req));

    req.opcode = SD_OP_READ_VDIS;
    req.data_length = max;

    ret = do_req(fd, (SheepdogReq *)&req, vdi_inuse, &wlen, &rlen);

    closesocket(fd);
    if (ret) {
        goto out;
    }

    sn_tab = g_malloc0(nr * sizeof(*sn_tab));

    /* calculate a vdi id with hash function */
    hval = fnv_64a_buf(s->name, strlen(s->name), FNV1A_64_INIT);
    start_nr = hval & (SD_NR_VDIS - 1);

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        ret = fd;
        goto out;
    }

    for (vid = start_nr; found < nr; vid = (vid + 1) % SD_NR_VDIS) {
        if (!test_bit(vid, vdi_inuse)) {
            break;
        }

        /* we don't need to read entire object */
        ret = read_object(fd, (char *)&inode, vid_to_vdi_oid(vid),
                          0, SD_INODE_SIZE - sizeof(inode.data_vdi_id), 0,
                          s->cache_flags);

        if (ret) {
            continue;
        }

        if (!strcmp(inode.name, s->name) && is_snapshot(&inode)) {
            sn_tab[found].date_sec = inode.snap_ctime >> 32;
            sn_tab[found].date_nsec = inode.snap_ctime & 0xffffffff;
            sn_tab[found].vm_state_size = inode.vm_state_size;
            sn_tab[found].vm_clock_nsec = inode.vm_clock_nsec;

            snprintf(sn_tab[found].id_str, sizeof(sn_tab[found].id_str),
                     "%" PRIu32, inode.snap_id);
            pstrcpy(sn_tab[found].name,
                    MIN(sizeof(sn_tab[found].name), sizeof(inode.tag)),
                    inode.tag);
            found++;
        }
    }

    closesocket(fd);
out:
    *psn_tab = sn_tab;

    g_free(vdi_inuse);

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

    fd = connect_to_sdog(s, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        return fd;
    }

    while (remaining) {
        vdi_index = pos / SD_DATA_OBJ_SIZE;
        offset = pos % SD_DATA_OBJ_SIZE;

        data_len = MIN(remaining, SD_DATA_OBJ_SIZE - offset);

        vmstate_oid = vid_to_vmstate_oid(vdi_id, vdi_index);

        create = (offset == 0);
        if (load) {
            ret = read_object(fd, (char *)data, vmstate_oid,
                              s->inode.nr_copies, data_len, offset,
                              s->cache_flags);
        } else {
            ret = write_object(fd, (char *)data, vmstate_oid,
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

static int sd_load_vmstate(BlockDriverState *bs, uint8_t *data,
                           int64_t pos, int size)
{
    BDRVSheepdogState *s = bs->opaque;

    return do_load_save_vmstate(s, data, pos, size, 1);
}


static coroutine_fn int sd_co_discard(BlockDriverState *bs, int64_t sector_num,
                                      int nb_sectors)
{
    SheepdogAIOCB *acb;
    QEMUIOVector dummy;
    BDRVSheepdogState *s = bs->opaque;
    int ret;

    if (!s->discard_supported) {
            return 0;
    }

    acb = sd_aio_setup(bs, &dummy, sector_num, nb_sectors);
    acb->aiocb_type = AIOCB_DISCARD_OBJ;
    acb->aio_done_func = sd_finish_aiocb;

    ret = sd_co_rw_vector(acb);
    if (ret <= 0) {
        qemu_aio_release(acb);
        return ret;
    }

    qemu_coroutine_yield();

    return acb->ret;
}

static coroutine_fn int64_t
sd_co_get_block_status(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                       int *pnum)
{
    BDRVSheepdogState *s = bs->opaque;
    SheepdogInode *inode = &s->inode;
    uint64_t offset = sector_num * BDRV_SECTOR_SIZE;
    unsigned long start = offset / SD_DATA_OBJ_SIZE,
                  end = DIV_ROUND_UP((sector_num + nb_sectors) *
                                     BDRV_SECTOR_SIZE, SD_DATA_OBJ_SIZE);
    unsigned long idx;
    int64_t ret = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID | offset;

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

    *pnum = (idx - start) * SD_DATA_OBJ_SIZE / BDRV_SECTOR_SIZE;
    if (*pnum > nb_sectors) {
        *pnum = nb_sectors;
    }
    return ret;
}

static int64_t sd_get_allocated_file_size(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;
    SheepdogInode *inode = &s->inode;
    unsigned long i, last = DIV_ROUND_UP(inode->vdi_size, SD_DATA_OBJ_SIZE);
    uint64_t size = 0;

    for (i = 0; i < last; i++) {
        if (inode->data_vdi_id[i] == 0) {
            continue;
        }
        size += SD_DATA_OBJ_SIZE;
    }
    return size;
}

static QEMUOptionParameter sd_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    {
        .name = BLOCK_OPT_BACKING_FILE,
        .type = OPT_STRING,
        .help = "File name of a base image"
    },
    {
        .name = BLOCK_OPT_PREALLOC,
        .type = OPT_STRING,
        .help = "Preallocation mode (allowed values: off, full)"
    },
    {
        .name = BLOCK_OPT_REDUNDANCY,
        .type = OPT_STRING,
        .help = "Redundancy of the image"
    },
    { NULL }
};

static BlockDriver bdrv_sheepdog = {
    .format_name    = "sheepdog",
    .protocol_name  = "sheepdog",
    .instance_size  = sizeof(BDRVSheepdogState),
    .bdrv_needs_filename = true,
    .bdrv_file_open = sd_open,
    .bdrv_close     = sd_close,
    .bdrv_create    = sd_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_getlength = sd_getlength,
    .bdrv_get_allocated_file_size = sd_get_allocated_file_size,
    .bdrv_truncate  = sd_truncate,

    .bdrv_co_readv  = sd_co_readv,
    .bdrv_co_writev = sd_co_writev,
    .bdrv_co_flush_to_disk  = sd_co_flush_to_disk,
    .bdrv_co_discard = sd_co_discard,
    .bdrv_co_get_block_status = sd_co_get_block_status,

    .bdrv_snapshot_create   = sd_snapshot_create,
    .bdrv_snapshot_goto     = sd_snapshot_goto,
    .bdrv_snapshot_delete   = sd_snapshot_delete,
    .bdrv_snapshot_list     = sd_snapshot_list,

    .bdrv_save_vmstate  = sd_save_vmstate,
    .bdrv_load_vmstate  = sd_load_vmstate,

    .create_options = sd_create_options,
};

static BlockDriver bdrv_sheepdog_tcp = {
    .format_name    = "sheepdog",
    .protocol_name  = "sheepdog+tcp",
    .instance_size  = sizeof(BDRVSheepdogState),
    .bdrv_needs_filename = true,
    .bdrv_file_open = sd_open,
    .bdrv_close     = sd_close,
    .bdrv_create    = sd_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_getlength = sd_getlength,
    .bdrv_get_allocated_file_size = sd_get_allocated_file_size,
    .bdrv_truncate  = sd_truncate,

    .bdrv_co_readv  = sd_co_readv,
    .bdrv_co_writev = sd_co_writev,
    .bdrv_co_flush_to_disk  = sd_co_flush_to_disk,
    .bdrv_co_discard = sd_co_discard,
    .bdrv_co_get_block_status = sd_co_get_block_status,

    .bdrv_snapshot_create   = sd_snapshot_create,
    .bdrv_snapshot_goto     = sd_snapshot_goto,
    .bdrv_snapshot_delete   = sd_snapshot_delete,
    .bdrv_snapshot_list     = sd_snapshot_list,

    .bdrv_save_vmstate  = sd_save_vmstate,
    .bdrv_load_vmstate  = sd_load_vmstate,

    .create_options = sd_create_options,
};

static BlockDriver bdrv_sheepdog_unix = {
    .format_name    = "sheepdog",
    .protocol_name  = "sheepdog+unix",
    .instance_size  = sizeof(BDRVSheepdogState),
    .bdrv_needs_filename = true,
    .bdrv_file_open = sd_open,
    .bdrv_close     = sd_close,
    .bdrv_create    = sd_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_getlength = sd_getlength,
    .bdrv_get_allocated_file_size = sd_get_allocated_file_size,
    .bdrv_truncate  = sd_truncate,

    .bdrv_co_readv  = sd_co_readv,
    .bdrv_co_writev = sd_co_writev,
    .bdrv_co_flush_to_disk  = sd_co_flush_to_disk,
    .bdrv_co_discard = sd_co_discard,
    .bdrv_co_get_block_status = sd_co_get_block_status,

    .bdrv_snapshot_create   = sd_snapshot_create,
    .bdrv_snapshot_goto     = sd_snapshot_goto,
    .bdrv_snapshot_delete   = sd_snapshot_delete,
    .bdrv_snapshot_list     = sd_snapshot_list,

    .bdrv_save_vmstate  = sd_save_vmstate,
    .bdrv_load_vmstate  = sd_load_vmstate,

    .create_options = sd_create_options,
};

static void bdrv_sheepdog_init(void)
{
    bdrv_register(&bdrv_sheepdog);
    bdrv_register(&bdrv_sheepdog_tcp);
    bdrv_register(&bdrv_sheepdog_unix);
}
block_init(bdrv_sheepdog_init);
