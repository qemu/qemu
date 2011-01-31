/*
 * Copyright (C) 2009-2010 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "qemu-error.h"
#include "qemu_socket.h"
#include "block_int.h"

#define SD_PROTO_VER 0x01

#define SD_DEFAULT_ADDR "localhost"
#define SD_DEFAULT_PORT "7000"

#define SD_OP_CREATE_AND_WRITE_OBJ  0x01
#define SD_OP_READ_OBJ       0x02
#define SD_OP_WRITE_OBJ      0x03

#define SD_OP_NEW_VDI        0x11
#define SD_OP_LOCK_VDI       0x12
#define SD_OP_RELEASE_VDI    0x13
#define SD_OP_GET_VDI_INFO   0x14
#define SD_OP_READ_VDIS      0x15

#define SD_FLAG_CMD_WRITE    0x01
#define SD_FLAG_CMD_COW      0x02

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

/*
 * Object ID rules
 *
 *  0 - 19 (20 bits): data object space
 * 20 - 31 (12 bits): reserved data object space
 * 32 - 55 (24 bits): vdi object space
 * 56 - 59 ( 4 bits): reserved vdi object space
 * 60 - 63 ( 4 bits): object type indentifier space
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
#define SECTOR_SIZE 512

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
    uint32_t copies;
    uint32_t rsvd;
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
    uint32_t copies;
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
    uint32_t copies;
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

static inline int is_data_obj_writeable(SheepdogInode *inode, unsigned int idx)
{
    return inode->vdi_id == inode->data_vdi_id[idx];
}

static inline int is_data_obj(uint64_t oid)
{
    return !(VDI_BIT & oid);
}

static inline uint64_t data_oid_to_idx(uint64_t oid)
{
    return oid & (MAX_DATA_OBJS - 1);
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

static inline int is_snapshot(struct SheepdogInode *inode)
{
    return !!inode->snap_ctime;
}

#undef dprintf
#ifdef DEBUG_SDOG
#define dprintf(fmt, args...)                                       \
    do {                                                            \
        fprintf(stdout, "%s %d: " fmt, __func__, __LINE__, ##args); \
    } while (0)
#else
#define dprintf(fmt, args...)
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

    QLIST_ENTRY(AIOReq) outstanding_aio_siblings;
    QLIST_ENTRY(AIOReq) aioreq_siblings;
} AIOReq;

enum AIOCBState {
    AIOCB_WRITE_UDATA,
    AIOCB_READ_UDATA,
};

struct SheepdogAIOCB {
    BlockDriverAIOCB common;

    QEMUIOVector *qiov;

    int64_t sector_num;
    int nb_sectors;

    int ret;
    enum AIOCBState aiocb_type;

    QEMUBH *bh;
    void (*aio_done_func)(SheepdogAIOCB *);

    int canceled;

    QLIST_HEAD(aioreq_head, AIOReq) aioreq_head;
};

typedef struct BDRVSheepdogState {
    SheepdogInode inode;

    uint32_t min_dirty_data_idx;
    uint32_t max_dirty_data_idx;

    char name[SD_MAX_VDI_LEN];
    int is_snapshot;

    char *addr;
    char *port;
    int fd;

    uint32_t aioreq_seq_num;
    QLIST_HEAD(outstanding_aio_head, AIOReq) outstanding_aio_head;
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
 * 1. In the sd_aio_readv/writev, read/write requests are added to the
 *    QEMU Bottom Halves.
 *
 * 2. In sd_readv_writev_bh_cb, the callbacks of BHs, we send the I/O
 *    requests to the server and link the requests to the
 *    outstanding_list in the BDRVSheepdogState.  we exits the
 *    function without waiting for receiving the response.
 *
 * 3. We receive the response in aio_read_response, the fd handler to
 *    the sheepdog connection.  If metadata update is needed, we send
 *    the write request to the vdi object in sd_write_done, the write
 *    completion function.  The AIOCB callback is not called until all
 *    the requests belonging to the AIOCB are finished.
 */

static inline AIOReq *alloc_aio_req(BDRVSheepdogState *s, SheepdogAIOCB *acb,
                                    uint64_t oid, unsigned int data_len,
                                    uint64_t offset, uint8_t flags,
                                    uint64_t base_oid, unsigned int iov_offset)
{
    AIOReq *aio_req;

    aio_req = qemu_malloc(sizeof(*aio_req));
    aio_req->aiocb = acb;
    aio_req->iov_offset = iov_offset;
    aio_req->oid = oid;
    aio_req->base_oid = base_oid;
    aio_req->offset = offset;
    aio_req->data_len = data_len;
    aio_req->flags = flags;
    aio_req->id = s->aioreq_seq_num++;

    QLIST_INSERT_HEAD(&s->outstanding_aio_head, aio_req,
                      outstanding_aio_siblings);
    QLIST_INSERT_HEAD(&acb->aioreq_head, aio_req, aioreq_siblings);

    return aio_req;
}

static inline int free_aio_req(BDRVSheepdogState *s, AIOReq *aio_req)
{
    SheepdogAIOCB *acb = aio_req->aiocb;
    QLIST_REMOVE(aio_req, outstanding_aio_siblings);
    QLIST_REMOVE(aio_req, aioreq_siblings);
    qemu_free(aio_req);

    return !QLIST_EMPTY(&acb->aioreq_head);
}

static void sd_finish_aiocb(SheepdogAIOCB *acb)
{
    if (!acb->canceled) {
        acb->common.cb(acb->common.opaque, acb->ret);
    }
    qemu_aio_release(acb);
}

static void sd_aio_cancel(BlockDriverAIOCB *blockacb)
{
    SheepdogAIOCB *acb = (SheepdogAIOCB *)blockacb;

    /*
     * Sheepdog cannot cancel the requests which are already sent to
     * the servers, so we just complete the request with -EIO here.
     */
    acb->common.cb(acb->common.opaque, -EIO);
    acb->canceled = 1;
}

static AIOPool sd_aio_pool = {
    .aiocb_size = sizeof(SheepdogAIOCB),
    .cancel = sd_aio_cancel,
};

static SheepdogAIOCB *sd_aio_setup(BlockDriverState *bs, QEMUIOVector *qiov,
                                   int64_t sector_num, int nb_sectors,
                                   BlockDriverCompletionFunc *cb, void *opaque)
{
    SheepdogAIOCB *acb;

    acb = qemu_aio_get(&sd_aio_pool, bs, cb, opaque);

    acb->qiov = qiov;

    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;

    acb->aio_done_func = NULL;
    acb->canceled = 0;
    acb->bh = NULL;
    acb->ret = 0;
    QLIST_INIT(&acb->aioreq_head);
    return acb;
}

static int sd_schedule_bh(QEMUBHFunc *cb, SheepdogAIOCB *acb)
{
    if (acb->bh) {
        error_report("bug: %d %d\n", acb->aiocb_type, acb->aiocb_type);
        return -EIO;
    }

    acb->bh = qemu_bh_new(cb, acb);
    if (!acb->bh) {
        error_report("oom: %d %d\n", acb->aiocb_type, acb->aiocb_type);
        return -EIO;
    }

    qemu_bh_schedule(acb->bh);

    return 0;
}

#ifdef _WIN32

struct msghdr {
    struct iovec *msg_iov;
    size_t        msg_iovlen;
};

static ssize_t sendmsg(int s, const struct msghdr *msg, int flags)
{
    size_t size = 0;
    char *buf, *p;
    int i, ret;

    /* count the msg size */
    for (i = 0; i < msg->msg_iovlen; i++) {
        size += msg->msg_iov[i].iov_len;
    }
    buf = qemu_malloc(size);

    p = buf;
    for (i = 0; i < msg->msg_iovlen; i++) {
        memcpy(p, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
        p += msg->msg_iov[i].iov_len;
    }

    ret = send(s, buf, size, flags);

    qemu_free(buf);
    return ret;
}

static ssize_t recvmsg(int s, struct msghdr *msg, int flags)
{
    size_t size = 0;
    char *buf, *p;
    int i, ret;

    /* count the msg size */
    for (i = 0; i < msg->msg_iovlen; i++) {
        size += msg->msg_iov[i].iov_len;
    }
    buf = qemu_malloc(size);

    ret = recv(s, buf, size, flags);
    if (ret < 0) {
        goto out;
    }

    p = buf;
    for (i = 0; i < msg->msg_iovlen; i++) {
        memcpy(msg->msg_iov[i].iov_base, p, msg->msg_iov[i].iov_len);
        p += msg->msg_iov[i].iov_len;
    }
out:
    qemu_free(buf);
    return ret;
}

#endif

/*
 * Send/recv data with iovec buffers
 *
 * This function send/recv data from/to the iovec buffer directly.
 * The first `offset' bytes in the iovec buffer are skipped and next
 * `len' bytes are used.
 *
 * For example,
 *
 *   do_send_recv(sockfd, iov, len, offset, 1);
 *
 * is equals to
 *
 *   char *buf = malloc(size);
 *   iov_to_buf(iov, iovcnt, buf, offset, size);
 *   send(sockfd, buf, size, 0);
 *   free(buf);
 */
static int do_send_recv(int sockfd, struct iovec *iov, int len, int offset,
                        int write)
{
    struct msghdr msg;
    int ret, diff;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    len += offset;

    while (iov->iov_len < len) {
        len -= iov->iov_len;

        iov++;
        msg.msg_iovlen++;
    }

    diff = iov->iov_len - len;
    iov->iov_len -= diff;

    while (msg.msg_iov->iov_len <= offset) {
        offset -= msg.msg_iov->iov_len;

        msg.msg_iov++;
        msg.msg_iovlen--;
    }

    msg.msg_iov->iov_base = (char *) msg.msg_iov->iov_base + offset;
    msg.msg_iov->iov_len -= offset;

    if (write) {
        ret = sendmsg(sockfd, &msg, 0);
    } else {
        ret = recvmsg(sockfd, &msg, 0);
    }

    msg.msg_iov->iov_base = (char *) msg.msg_iov->iov_base - offset;
    msg.msg_iov->iov_len += offset;

    iov->iov_len += diff;
    return ret;
}

static int connect_to_sdog(const char *addr, const char *port)
{
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    int fd, ret;
    struct addrinfo hints, *res, *res0;

    if (!addr) {
        addr = SD_DEFAULT_ADDR;
        port = SD_DEFAULT_PORT;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(addr, port, &hints, &res0);
    if (ret) {
        error_report("unable to get address info %s, %s\n",
                     addr, strerror(errno));
        return -1;
    }

    for (res = res0; res; res = res->ai_next) {
        ret = getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof(hbuf),
                          sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
        if (ret) {
            continue;
        }

        fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) {
            continue;
        }

    reconnect:
        ret = connect(fd, res->ai_addr, res->ai_addrlen);
        if (ret < 0) {
            if (errno == EINTR) {
                goto reconnect;
            }
            break;
        }

        dprintf("connected to %s:%s\n", addr, port);
        goto success;
    }
    fd = -1;
    error_report("failed connect to %s:%s\n", addr, port);
success:
    freeaddrinfo(res0);
    return fd;
}

static int do_readv_writev(int sockfd, struct iovec *iov, int len,
                           int iov_offset, int write)
{
    int ret;
again:
    ret = do_send_recv(sockfd, iov, len, iov_offset, write);
    if (ret < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            goto again;
        }
        error_report("failed to recv a rsp, %s\n", strerror(errno));
        return 1;
    }

    iov_offset += ret;
    len -= ret;
    if (len) {
        goto again;
    }

    return 0;
}

static int do_readv(int sockfd, struct iovec *iov, int len, int iov_offset)
{
    return do_readv_writev(sockfd, iov, len, iov_offset, 0);
}

static int do_writev(int sockfd, struct iovec *iov, int len, int iov_offset)
{
    return do_readv_writev(sockfd, iov, len, iov_offset, 1);
}

static int do_read_write(int sockfd, void *buf, int len, int write)
{
    struct iovec iov;

    iov.iov_base = buf;
    iov.iov_len = len;

    return do_readv_writev(sockfd, &iov, len, 0, write);
}

static int do_read(int sockfd, void *buf, int len)
{
    return do_read_write(sockfd, buf, len, 0);
}

static int do_write(int sockfd, void *buf, int len)
{
    return do_read_write(sockfd, buf, len, 1);
}

static int send_req(int sockfd, SheepdogReq *hdr, void *data,
                    unsigned int *wlen)
{
    int ret;
    struct iovec iov[2];

    iov[0].iov_base = hdr;
    iov[0].iov_len = sizeof(*hdr);

    if (*wlen) {
        iov[1].iov_base = data;
        iov[1].iov_len = *wlen;
    }

    ret = do_writev(sockfd, iov, sizeof(*hdr) + *wlen, 0);
    if (ret) {
        error_report("failed to send a req, %s\n", strerror(errno));
        ret = -1;
    }

    return ret;
}

static int do_req(int sockfd, SheepdogReq *hdr, void *data,
                  unsigned int *wlen, unsigned int *rlen)
{
    int ret;

    ret = send_req(sockfd, hdr, data, wlen);
    if (ret) {
        ret = -1;
        goto out;
    }

    ret = do_read(sockfd, hdr, sizeof(*hdr));
    if (ret) {
        error_report("failed to get a rsp, %s\n", strerror(errno));
        ret = -1;
        goto out;
    }

    if (*rlen > hdr->data_length) {
        *rlen = hdr->data_length;
    }

    if (*rlen) {
        ret = do_read(sockfd, data, *rlen);
        if (ret) {
            error_report("failed to get the data, %s\n", strerror(errno));
            ret = -1;
            goto out;
        }
    }
    ret = 0;
out:
    return ret;
}

static int add_aio_request(BDRVSheepdogState *s, AIOReq *aio_req,
                           struct iovec *iov, int niov, int create,
                           enum AIOCBState aiocb_type);

/*
 * This function searchs pending requests to the object `oid', and
 * sends them.
 */
static void send_pending_req(BDRVSheepdogState *s, uint64_t oid, uint32_t id)
{
    AIOReq *aio_req, *next;
    SheepdogAIOCB *acb;
    int ret;

    QLIST_FOREACH_SAFE(aio_req, &s->outstanding_aio_head,
                       outstanding_aio_siblings, next) {
        if (id == aio_req->id) {
            continue;
        }
        if (aio_req->oid != oid) {
            continue;
        }

        acb = aio_req->aiocb;
        ret = add_aio_request(s, aio_req, acb->qiov->iov,
                              acb->qiov->niov, 0, acb->aiocb_type);
        if (ret < 0) {
            error_report("add_aio_request is failed\n");
            free_aio_req(s, aio_req);
            if (QLIST_EMPTY(&acb->aioreq_head)) {
                sd_finish_aiocb(acb);
            }
        }
    }
}

/*
 * Receive responses of the I/O requests.
 *
 * This function is registered as a fd handler, and called from the
 * main loop when s->fd is ready for reading responses.
 */
static void aio_read_response(void *opaque)
{
    SheepdogObjRsp rsp;
    BDRVSheepdogState *s = opaque;
    int fd = s->fd;
    int ret;
    AIOReq *aio_req = NULL;
    SheepdogAIOCB *acb;
    int rest;
    unsigned long idx;

    if (QLIST_EMPTY(&s->outstanding_aio_head)) {
        return;
    }

    /* read a header */
    ret = do_read(fd, &rsp, sizeof(rsp));
    if (ret) {
        error_report("failed to get the header, %s\n", strerror(errno));
        return;
    }

    /* find the right aio_req from the outstanding_aio list */
    QLIST_FOREACH(aio_req, &s->outstanding_aio_head, outstanding_aio_siblings) {
        if (aio_req->id == rsp.id) {
            break;
        }
    }
    if (!aio_req) {
        error_report("cannot find aio_req %x\n", rsp.id);
        return;
    }

    acb = aio_req->aiocb;

    switch (acb->aiocb_type) {
    case AIOCB_WRITE_UDATA:
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
            s->inode.data_vdi_id[idx] = s->inode.vdi_id;
            s->max_dirty_data_idx = MAX(idx, s->max_dirty_data_idx);
            s->min_dirty_data_idx = MIN(idx, s->min_dirty_data_idx);

            /*
             * Some requests may be blocked because simultaneous
             * create requests are not allowed, so we search the
             * pending requests here.
             */
            send_pending_req(s, vid_to_data_oid(s->inode.vdi_id, idx), rsp.id);
        }
        break;
    case AIOCB_READ_UDATA:
        ret = do_readv(fd, acb->qiov->iov, rsp.data_length,
                       aio_req->iov_offset);
        if (ret) {
            error_report("failed to get the data, %s\n", strerror(errno));
            return;
        }
        break;
    }

    if (rsp.result != SD_RES_SUCCESS) {
        acb->ret = -EIO;
        error_report("%s\n", sd_strerror(rsp.result));
    }

    rest = free_aio_req(s, aio_req);
    if (!rest) {
        /*
         * We've finished all requests which belong to the AIOCB, so
         * we can call the callback now.
         */
        acb->aio_done_func(acb);
    }
}

static int aio_flush_request(void *opaque)
{
    BDRVSheepdogState *s = opaque;

    return !QLIST_EMPTY(&s->outstanding_aio_head);
}

#if !defined(SOL_TCP) || !defined(TCP_CORK)

static int set_cork(int fd, int v)
{
    return 0;
}

#else

static int set_cork(int fd, int v)
{
    return setsockopt(fd, SOL_TCP, TCP_CORK, &v, sizeof(v));
}

#endif

static int set_nodelay(int fd)
{
    int ret, opt;

    opt = 1;
    ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt));
    return ret;
}

/*
 * Return a socket discriptor to read/write objects.
 *
 * We cannot use this discriptor for other operations because
 * the block driver may be on waiting response from the server.
 */
static int get_sheep_fd(BDRVSheepdogState *s)
{
    int ret, fd;

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        error_report("%s\n", strerror(errno));
        return -1;
    }

    socket_set_nonblock(fd);

    ret = set_nodelay(fd);
    if (ret) {
        error_report("%s\n", strerror(errno));
        closesocket(fd);
        return -1;
    }

    qemu_aio_set_fd_handler(fd, aio_read_response, NULL, aio_flush_request,
                            NULL, s);
    return fd;
}

/*
 * Parse a filename
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
    char *p, *q;
    int nr_sep;

    p = q = qemu_strdup(filename);

    /* count the number of separators */
    nr_sep = 0;
    while (*p) {
        if (*p == ':') {
            nr_sep++;
        }
        p++;
    }
    p = q;

    /* use the first two tokens as hostname and port number. */
    if (nr_sep >= 2) {
        s->addr = p;
        p = strchr(p, ':');
        *p++ = '\0';

        s->port = p;
        p = strchr(p, ':');
        *p++ = '\0';
    } else {
        s->addr = NULL;
        s->port = 0;
    }

    strncpy(vdi, p, SD_MAX_VDI_LEN);

    p = strchr(vdi, ':');
    if (p) {
        *p++ = '\0';
        *snapid = strtoul(p, NULL, 10);
        if (*snapid == 0) {
            strncpy(tag, p, SD_MAX_VDI_TAG_LEN);
        }
    } else {
        *snapid = CURRENT_VDI_ID; /* search current vdi */
    }

    if (s->addr == NULL) {
        qemu_free(q);
    }

    return 0;
}

static int find_vdi_name(BDRVSheepdogState *s, char *filename, uint32_t snapid,
                         char *tag, uint32_t *vid, int for_snapshot)
{
    int ret, fd;
    SheepdogVdiReq hdr;
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    unsigned int wlen, rlen = 0;
    char buf[SD_MAX_VDI_LEN + SD_MAX_VDI_TAG_LEN];

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    strncpy(buf, filename, SD_MAX_VDI_LEN);
    strncpy(buf + SD_MAX_VDI_LEN, tag, SD_MAX_VDI_TAG_LEN);

    memset(&hdr, 0, sizeof(hdr));
    if (for_snapshot) {
        hdr.opcode = SD_OP_GET_VDI_INFO;
    } else {
        hdr.opcode = SD_OP_LOCK_VDI;
    }
    wlen = SD_MAX_VDI_LEN + SD_MAX_VDI_TAG_LEN;
    hdr.proto_ver = SD_PROTO_VER;
    hdr.data_length = wlen;
    hdr.snapid = snapid;
    hdr.flags = SD_FLAG_CMD_WRITE;

    ret = do_req(fd, (SheepdogReq *)&hdr, buf, &wlen, &rlen);
    if (ret) {
        ret = -1;
        goto out;
    }

    if (rsp->result != SD_RES_SUCCESS) {
        error_report("cannot get vdi info, %s, %s %d %s\n",
                     sd_strerror(rsp->result), filename, snapid, tag);
        ret = -1;
        goto out;
    }
    *vid = rsp->vdi_id;

    ret = 0;
out:
    closesocket(fd);
    return ret;
}

static int add_aio_request(BDRVSheepdogState *s, AIOReq *aio_req,
                           struct iovec *iov, int niov, int create,
                           enum AIOCBState aiocb_type)
{
    int nr_copies = s->inode.nr_copies;
    SheepdogObjReq hdr;
    unsigned int wlen;
    int ret;
    uint64_t oid = aio_req->oid;
    unsigned int datalen = aio_req->data_len;
    uint64_t offset = aio_req->offset;
    uint8_t flags = aio_req->flags;
    uint64_t old_oid = aio_req->base_oid;

    if (!nr_copies) {
        error_report("bug\n");
    }

    memset(&hdr, 0, sizeof(hdr));

    if (aiocb_type == AIOCB_READ_UDATA) {
        wlen = 0;
        hdr.opcode = SD_OP_READ_OBJ;
        hdr.flags = flags;
    } else if (create) {
        wlen = datalen;
        hdr.opcode = SD_OP_CREATE_AND_WRITE_OBJ;
        hdr.flags = SD_FLAG_CMD_WRITE | flags;
    } else {
        wlen = datalen;
        hdr.opcode = SD_OP_WRITE_OBJ;
        hdr.flags = SD_FLAG_CMD_WRITE | flags;
    }

    hdr.oid = oid;
    hdr.cow_oid = old_oid;
    hdr.copies = s->inode.nr_copies;

    hdr.data_length = datalen;
    hdr.offset = offset;

    hdr.id = aio_req->id;

    set_cork(s->fd, 1);

    /* send a header */
    ret = do_write(s->fd, &hdr, sizeof(hdr));
    if (ret) {
        error_report("failed to send a req, %s\n", strerror(errno));
        return -EIO;
    }

    if (wlen) {
        ret = do_writev(s->fd, iov, wlen, aio_req->iov_offset);
        if (ret) {
            error_report("failed to send a data, %s\n", strerror(errno));
            return -EIO;
        }
    }

    set_cork(s->fd, 0);

    return 0;
}

static int read_write_object(int fd, char *buf, uint64_t oid, int copies,
                             unsigned int datalen, uint64_t offset,
                             int write, int create)
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
    hdr.oid = oid;
    hdr.data_length = datalen;
    hdr.offset = offset;
    hdr.copies = copies;

    ret = do_req(fd, (SheepdogReq *)&hdr, buf, &wlen, &rlen);
    if (ret) {
        error_report("failed to send a request to the sheep\n");
        return -1;
    }

    switch (rsp->result) {
    case SD_RES_SUCCESS:
        return 0;
    default:
        error_report("%s\n", sd_strerror(rsp->result));
        return -1;
    }
}

static int read_object(int fd, char *buf, uint64_t oid, int copies,
                       unsigned int datalen, uint64_t offset)
{
    return read_write_object(fd, buf, oid, copies, datalen, offset, 0, 0);
}

static int write_object(int fd, char *buf, uint64_t oid, int copies,
                        unsigned int datalen, uint64_t offset, int create)
{
    return read_write_object(fd, buf, oid, copies, datalen, offset, 1, create);
}

static int sd_open(BlockDriverState *bs, const char *filename, int flags)
{
    int ret, fd;
    uint32_t vid = 0;
    BDRVSheepdogState *s = bs->opaque;
    char vdi[SD_MAX_VDI_LEN], tag[SD_MAX_VDI_TAG_LEN];
    uint32_t snapid;
    char *buf = NULL;

    strstart(filename, "sheepdog:", (const char **)&filename);

    QLIST_INIT(&s->outstanding_aio_head);
    s->fd = -1;

    memset(vdi, 0, sizeof(vdi));
    memset(tag, 0, sizeof(tag));
    if (parse_vdiname(s, filename, vdi, &snapid, tag) < 0) {
        goto out;
    }
    s->fd = get_sheep_fd(s);
    if (s->fd < 0) {
        goto out;
    }

    ret = find_vdi_name(s, vdi, snapid, tag, &vid, 0);
    if (ret) {
        goto out;
    }

    if (snapid) {
        dprintf("%" PRIx32 " snapshot inode was open.\n", vid);
        s->is_snapshot = 1;
    }

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        error_report("failed to connect\n");
        goto out;
    }

    buf = qemu_malloc(SD_INODE_SIZE);
    ret = read_object(fd, buf, vid_to_vdi_oid(vid), 0, SD_INODE_SIZE, 0);

    closesocket(fd);

    if (ret) {
        goto out;
    }

    memcpy(&s->inode, buf, sizeof(s->inode));
    s->min_dirty_data_idx = UINT32_MAX;
    s->max_dirty_data_idx = 0;

    bs->total_sectors = s->inode.vdi_size / SECTOR_SIZE;
    strncpy(s->name, vdi, sizeof(s->name));
    qemu_free(buf);
    return 0;
out:
    qemu_aio_set_fd_handler(s->fd, NULL, NULL, NULL, NULL, NULL);
    if (s->fd >= 0) {
        closesocket(s->fd);
    }
    qemu_free(buf);
    return -1;
}

static int do_sd_create(char *filename, int64_t vdi_size,
                        uint32_t base_vid, uint32_t *vdi_id, int snapshot,
                        const char *addr, const char *port)
{
    SheepdogVdiReq hdr;
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    int fd, ret;
    unsigned int wlen, rlen = 0;
    char buf[SD_MAX_VDI_LEN];

    fd = connect_to_sdog(addr, port);
    if (fd < 0) {
        return -EIO;
    }

    memset(buf, 0, sizeof(buf));
    strncpy(buf, filename, SD_MAX_VDI_LEN);

    memset(&hdr, 0, sizeof(hdr));
    hdr.opcode = SD_OP_NEW_VDI;
    hdr.base_vdi_id = base_vid;

    wlen = SD_MAX_VDI_LEN;

    hdr.flags = SD_FLAG_CMD_WRITE;
    hdr.snapid = snapshot;

    hdr.data_length = wlen;
    hdr.vdi_size = vdi_size;

    ret = do_req(fd, (SheepdogReq *)&hdr, buf, &wlen, &rlen);

    closesocket(fd);

    if (ret) {
        return -EIO;
    }

    if (rsp->result != SD_RES_SUCCESS) {
        error_report("%s, %s\n", sd_strerror(rsp->result), filename);
        return -EIO;
    }

    if (vdi_id) {
        *vdi_id = rsp->vdi_id;
    }

    return 0;
}

static int sd_create(const char *filename, QEMUOptionParameter *options)
{
    int ret;
    uint32_t vid = 0, base_vid = 0;
    int64_t vdi_size = 0;
    char *backing_file = NULL;
    BDRVSheepdogState s;
    char vdi[SD_MAX_VDI_LEN], tag[SD_MAX_VDI_TAG_LEN];
    uint32_t snapid;

    strstart(filename, "sheepdog:", (const char **)&filename);

    memset(&s, 0, sizeof(s));
    memset(vdi, 0, sizeof(vdi));
    memset(tag, 0, sizeof(tag));
    if (parse_vdiname(&s, filename, vdi, &snapid, tag) < 0) {
        error_report("invalid filename\n");
        return -EINVAL;
    }

    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            vdi_size = options->value.n;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FILE)) {
            backing_file = options->value.s;
        }
        options++;
    }

    if (vdi_size > SD_MAX_VDI_SIZE) {
        error_report("too big image size\n");
        return -EINVAL;
    }

    if (backing_file) {
        BlockDriverState *bs;
        BDRVSheepdogState *s;
        BlockDriver *drv;

        /* Currently, only Sheepdog backing image is supported. */
        drv = bdrv_find_protocol(backing_file);
        if (!drv || strcmp(drv->protocol_name, "sheepdog") != 0) {
            error_report("backing_file must be a sheepdog image\n");
            return -EINVAL;
        }

        ret = bdrv_file_open(&bs, backing_file, 0);
        if (ret < 0)
            return -EIO;

        s = bs->opaque;

        if (!is_snapshot(&s->inode)) {
            error_report("cannot clone from a non snapshot vdi\n");
            bdrv_delete(bs);
            return -EINVAL;
        }

        base_vid = s->inode.vdi_id;
        bdrv_delete(bs);
    }

    return do_sd_create((char *)vdi, vdi_size, base_vid, &vid, 0, s.addr, s.port);
}

static void sd_close(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;
    SheepdogVdiReq hdr;
    SheepdogVdiRsp *rsp = (SheepdogVdiRsp *)&hdr;
    unsigned int wlen, rlen = 0;
    int fd, ret;

    dprintf("%s\n", s->name);

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        return;
    }

    memset(&hdr, 0, sizeof(hdr));

    hdr.opcode = SD_OP_RELEASE_VDI;
    wlen = strlen(s->name) + 1;
    hdr.data_length = wlen;
    hdr.flags = SD_FLAG_CMD_WRITE;

    ret = do_req(fd, (SheepdogReq *)&hdr, s->name, &wlen, &rlen);

    closesocket(fd);

    if (!ret && rsp->result != SD_RES_SUCCESS &&
        rsp->result != SD_RES_VDI_NOT_LOCKED) {
        error_report("%s, %s\n", sd_strerror(rsp->result), s->name);
    }

    qemu_aio_set_fd_handler(s->fd, NULL, NULL, NULL, NULL, NULL);
    closesocket(s->fd);
    qemu_free(s->addr);
}

static int64_t sd_getlength(BlockDriverState *bs)
{
    BDRVSheepdogState *s = bs->opaque;

    return s->inode.vdi_size;
}

static int sd_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVSheepdogState *s = bs->opaque;
    int ret, fd;
    unsigned int datalen;

    if (offset < s->inode.vdi_size) {
        error_report("shrinking is not supported\n");
        return -EINVAL;
    } else if (offset > SD_MAX_VDI_SIZE) {
        error_report("too big image size\n");
        return -EINVAL;
    }

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        return -EIO;
    }

    /* we don't need to update entire object */
    datalen = SD_INODE_SIZE - sizeof(s->inode.data_vdi_id);
    s->inode.vdi_size = offset;
    ret = write_object(fd, (char *)&s->inode, vid_to_vdi_oid(s->inode.vdi_id),
                       s->inode.nr_copies, datalen, 0, 0);
    close(fd);

    if (ret < 0) {
        error_report("failed to update an inode.\n");
        return -EIO;
    }

    return 0;
}

/*
 * This function is called after writing data objects.  If we need to
 * update metadata, this sends a write request to the vdi object.
 * Otherwise, this calls the AIOCB callback.
 */
static void sd_write_done(SheepdogAIOCB *acb)
{
    int ret;
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
        ret = add_aio_request(s, aio_req, &iov, 1, 0, AIOCB_WRITE_UDATA);
        if (ret) {
            free_aio_req(s, aio_req);
            acb->ret = -EIO;
            goto out;
        }

        acb->aio_done_func = sd_finish_aiocb;
        acb->aiocb_type = AIOCB_WRITE_UDATA;
        return;
    }
out:
    sd_finish_aiocb(acb);
}

/*
 * Create a writable VDI from a snapshot
 */
static int sd_create_branch(BDRVSheepdogState *s)
{
    int ret, fd;
    uint32_t vid;
    char *buf;

    dprintf("%" PRIx32 " is snapshot.\n", s->inode.vdi_id);

    buf = qemu_malloc(SD_INODE_SIZE);

    ret = do_sd_create(s->name, s->inode.vdi_size, s->inode.vdi_id, &vid, 1,
                       s->addr, s->port);
    if (ret) {
        goto out;
    }

    dprintf("%" PRIx32 " is created.\n", vid);

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        error_report("failed to connect\n");
        goto out;
    }

    ret = read_object(fd, buf, vid_to_vdi_oid(vid), s->inode.nr_copies,
                      SD_INODE_SIZE, 0);

    closesocket(fd);

    if (ret < 0) {
        goto out;
    }

    memcpy(&s->inode, buf, sizeof(s->inode));

    s->is_snapshot = 0;
    ret = 0;
    dprintf("%" PRIx32 " was newly created.\n", s->inode.vdi_id);

out:
    qemu_free(buf);

    return ret;
}

/*
 * Send I/O requests to the server.
 *
 * This function sends requests to the server, links the requests to
 * the outstanding_list in BDRVSheepdogState, and exits without
 * waiting the response.  The responses are received in the
 * `aio_read_response' function which is called from the main loop as
 * a fd handler.
 */
static void sd_readv_writev_bh_cb(void *p)
{
    SheepdogAIOCB *acb = p;
    int ret = 0;
    unsigned long len, done = 0, total = acb->nb_sectors * SECTOR_SIZE;
    unsigned long idx = acb->sector_num * SECTOR_SIZE / SD_DATA_OBJ_SIZE;
    uint64_t oid;
    uint64_t offset = (acb->sector_num * SECTOR_SIZE) % SD_DATA_OBJ_SIZE;
    BDRVSheepdogState *s = acb->common.bs->opaque;
    SheepdogInode *inode = &s->inode;
    AIOReq *aio_req;

    qemu_bh_delete(acb->bh);
    acb->bh = NULL;

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

    while (done != total) {
        uint8_t flags = 0;
        uint64_t old_oid = 0;
        int create = 0;

        oid = vid_to_data_oid(inode->data_vdi_id[idx], idx);

        len = MIN(total - done, SD_DATA_OBJ_SIZE - offset);

        if (!inode->data_vdi_id[idx]) {
            if (acb->aiocb_type == AIOCB_READ_UDATA) {
                goto done;
            }

            create = 1;
        } else if (acb->aiocb_type == AIOCB_WRITE_UDATA
                   && !is_data_obj_writeable(inode, idx)) {
            /* Copy-On-Write */
            create = 1;
            old_oid = oid;
            flags = SD_FLAG_CMD_COW;
        }

        if (create) {
            dprintf("update ino (%" PRIu32") %" PRIu64 " %" PRIu64
                    " %" PRIu64 "\n", inode->vdi_id, oid,
                    vid_to_data_oid(inode->data_vdi_id[idx], idx), idx);
            oid = vid_to_data_oid(inode->vdi_id, idx);
            dprintf("new oid %lx\n", oid);
        }

        aio_req = alloc_aio_req(s, acb, oid, len, offset, flags, old_oid, done);

        if (create) {
            AIOReq *areq;
            QLIST_FOREACH(areq, &s->outstanding_aio_head,
                          outstanding_aio_siblings) {
                if (areq == aio_req) {
                    continue;
                }
                if (areq->oid == oid) {
                    /*
                     * Sheepdog cannot handle simultaneous create
                     * requests to the same object.  So we cannot send
                     * the request until the previous request
                     * finishes.
                     */
                    aio_req->flags = 0;
                    aio_req->base_oid = 0;
                    goto done;
                }
            }
        }

        ret = add_aio_request(s, aio_req, acb->qiov->iov, acb->qiov->niov,
                              create, acb->aiocb_type);
        if (ret < 0) {
            error_report("add_aio_request is failed\n");
            free_aio_req(s, aio_req);
            acb->ret = -EIO;
            goto out;
        }
    done:
        offset = 0;
        idx++;
        done += len;
    }
out:
    if (QLIST_EMPTY(&acb->aioreq_head)) {
        sd_finish_aiocb(acb);
    }
}

static BlockDriverAIOCB *sd_aio_writev(BlockDriverState *bs, int64_t sector_num,
                                       QEMUIOVector *qiov, int nb_sectors,
                                       BlockDriverCompletionFunc *cb,
                                       void *opaque)
{
    SheepdogAIOCB *acb;

    if (bs->growable && sector_num + nb_sectors > bs->total_sectors) {
        /* TODO: shouldn't block here */
        if (sd_truncate(bs, (sector_num + nb_sectors) * SECTOR_SIZE) < 0) {
            return NULL;
        }
        bs->total_sectors = sector_num + nb_sectors;
    }

    acb = sd_aio_setup(bs, qiov, sector_num, nb_sectors, cb, opaque);
    acb->aio_done_func = sd_write_done;
    acb->aiocb_type = AIOCB_WRITE_UDATA;

    sd_schedule_bh(sd_readv_writev_bh_cb, acb);
    return &acb->common;
}

static BlockDriverAIOCB *sd_aio_readv(BlockDriverState *bs, int64_t sector_num,
                                      QEMUIOVector *qiov, int nb_sectors,
                                      BlockDriverCompletionFunc *cb,
                                      void *opaque)
{
    SheepdogAIOCB *acb;
    int i;

    acb = sd_aio_setup(bs, qiov, sector_num, nb_sectors, cb, opaque);
    acb->aiocb_type = AIOCB_READ_UDATA;
    acb->aio_done_func = sd_finish_aiocb;

    /*
     * TODO: we can do better; we don't need to initialize
     * blindly.
     */
    for (i = 0; i < qiov->niov; i++) {
        memset(qiov->iov[i].iov_base, 0, qiov->iov[i].iov_len);
    }

    sd_schedule_bh(sd_readv_writev_bh_cb, acb);
    return &acb->common;
}

static int sd_snapshot_create(BlockDriverState *bs, QEMUSnapshotInfo *sn_info)
{
    BDRVSheepdogState *s = bs->opaque;
    int ret, fd;
    uint32_t new_vid;
    SheepdogInode *inode;
    unsigned int datalen;

    dprintf("sn_info: name %s id_str %s s: name %s vm_state_size %d "
            "is_snapshot %d\n", sn_info->name, sn_info->id_str,
            s->name, sn_info->vm_state_size, s->is_snapshot);

    if (s->is_snapshot) {
        error_report("You can't create a snapshot of a snapshot VDI, "
                     "%s (%" PRIu32 ").\n", s->name, s->inode.vdi_id);

        return -EINVAL;
    }

    dprintf("%s %s\n", sn_info->name, sn_info->id_str);

    s->inode.vm_state_size = sn_info->vm_state_size;
    s->inode.vm_clock_nsec = sn_info->vm_clock_nsec;
    strncpy(s->inode.tag, sn_info->name, sizeof(s->inode.tag));
    /* we don't need to update entire object */
    datalen = SD_INODE_SIZE - sizeof(s->inode.data_vdi_id);

    /* refresh inode. */
    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        ret = -EIO;
        goto cleanup;
    }

    ret = write_object(fd, (char *)&s->inode, vid_to_vdi_oid(s->inode.vdi_id),
                       s->inode.nr_copies, datalen, 0, 0);
    if (ret < 0) {
        error_report("failed to write snapshot's inode.\n");
        ret = -EIO;
        goto cleanup;
    }

    ret = do_sd_create(s->name, s->inode.vdi_size, s->inode.vdi_id, &new_vid, 1,
                       s->addr, s->port);
    if (ret < 0) {
        error_report("failed to create inode for snapshot. %s\n",
                     strerror(errno));
        ret = -EIO;
        goto cleanup;
    }

    inode = (SheepdogInode *)qemu_malloc(datalen);

    ret = read_object(fd, (char *)inode, vid_to_vdi_oid(new_vid),
                      s->inode.nr_copies, datalen, 0);

    if (ret < 0) {
        error_report("failed to read new inode info. %s\n", strerror(errno));
        ret = -EIO;
        goto cleanup;
    }

    memcpy(&s->inode, inode, datalen);
    dprintf("s->inode: name %s snap_id %x oid %x\n",
            s->inode.name, s->inode.snap_id, s->inode.vdi_id);

cleanup:
    closesocket(fd);
    return ret;
}

static int sd_snapshot_goto(BlockDriverState *bs, const char *snapshot_id)
{
    BDRVSheepdogState *s = bs->opaque;
    BDRVSheepdogState *old_s;
    char vdi[SD_MAX_VDI_LEN], tag[SD_MAX_VDI_TAG_LEN];
    char *buf = NULL;
    uint32_t vid;
    uint32_t snapid = 0;
    int ret = -ENOENT, fd;

    old_s = qemu_malloc(sizeof(BDRVSheepdogState));

    memcpy(old_s, s, sizeof(BDRVSheepdogState));

    memset(vdi, 0, sizeof(vdi));
    strncpy(vdi, s->name, sizeof(vdi));

    memset(tag, 0, sizeof(tag));
    snapid = strtoul(snapshot_id, NULL, 10);
    if (!snapid) {
        strncpy(tag, s->name, sizeof(tag));
    }

    ret = find_vdi_name(s, vdi, snapid, tag, &vid, 1);
    if (ret) {
        error_report("Failed to find_vdi_name\n");
        ret = -ENOENT;
        goto out;
    }

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        error_report("failed to connect\n");
        goto out;
    }

    buf = qemu_malloc(SD_INODE_SIZE);
    ret = read_object(fd, buf, vid_to_vdi_oid(vid), s->inode.nr_copies,
                      SD_INODE_SIZE, 0);

    closesocket(fd);

    if (ret) {
        ret = -ENOENT;
        goto out;
    }

    memcpy(&s->inode, buf, sizeof(s->inode));

    if (!s->inode.vm_state_size) {
        error_report("Invalid snapshot\n");
        ret = -ENOENT;
        goto out;
    }

    s->is_snapshot = 1;

    qemu_free(buf);
    qemu_free(old_s);

    return 0;
out:
    /* recover bdrv_sd_state */
    memcpy(s, old_s, sizeof(BDRVSheepdogState));
    qemu_free(buf);
    qemu_free(old_s);

    error_report("failed to open. recover old bdrv_sd_state.\n");

    return ret;
}

static int sd_snapshot_delete(BlockDriverState *bs, const char *snapshot_id)
{
    /* FIXME: Delete specified snapshot id.  */
    return 0;
}

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define BITS_PER_BYTE        8
#define BITS_TO_LONGS(nr)    DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))
#define DECLARE_BITMAP(name,bits)               \
    unsigned long name[BITS_TO_LONGS(bits)]

#define BITS_PER_LONG (BITS_PER_BYTE * sizeof(long))

static inline int test_bit(unsigned int nr, const unsigned long *addr)
{
    return ((1UL << (nr % BITS_PER_LONG)) &
            (((unsigned long *)addr)[nr / BITS_PER_LONG])) != 0;
}

static int sd_snapshot_list(BlockDriverState *bs, QEMUSnapshotInfo **psn_tab)
{
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

    vdi_inuse = qemu_malloc(max);

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
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

    sn_tab = qemu_mallocz(nr * sizeof(*sn_tab));

    /* calculate a vdi id with hash function */
    hval = fnv_64a_buf(s->name, strlen(s->name), FNV1A_64_INIT);
    start_nr = hval & (SD_NR_VDIS - 1);

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        error_report("failed to connect\n");
        goto out;
    }

    for (vid = start_nr; found < nr; vid = (vid + 1) % SD_NR_VDIS) {
        if (!test_bit(vid, vdi_inuse)) {
            break;
        }

        /* we don't need to read entire object */
        ret = read_object(fd, (char *)&inode, vid_to_vdi_oid(vid),
                          0, SD_INODE_SIZE - sizeof(inode.data_vdi_id), 0);

        if (ret) {
            continue;
        }

        if (!strcmp(inode.name, s->name) && is_snapshot(&inode)) {
            sn_tab[found].date_sec = inode.snap_ctime >> 32;
            sn_tab[found].date_nsec = inode.snap_ctime & 0xffffffff;
            sn_tab[found].vm_state_size = inode.vm_state_size;
            sn_tab[found].vm_clock_nsec = inode.vm_clock_nsec;

            snprintf(sn_tab[found].id_str, sizeof(sn_tab[found].id_str), "%u",
                     inode.snap_id);
            strncpy(sn_tab[found].name, inode.tag,
                    MIN(sizeof(sn_tab[found].name), sizeof(inode.tag)));
            found++;
        }
    }

    closesocket(fd);
out:
    *psn_tab = sn_tab;

    qemu_free(vdi_inuse);

    return found;
}

static int do_load_save_vmstate(BDRVSheepdogState *s, uint8_t *data,
                                int64_t pos, int size, int load)
{
    int fd, create;
    int ret = 0;
    unsigned int data_len;
    uint64_t vmstate_oid;
    uint32_t vdi_index;
    uint64_t offset;

    fd = connect_to_sdog(s->addr, s->port);
    if (fd < 0) {
        ret = -EIO;
        goto cleanup;
    }

    while (size) {
        vdi_index = pos / SD_DATA_OBJ_SIZE;
        offset = pos % SD_DATA_OBJ_SIZE;

        data_len = MIN(size, SD_DATA_OBJ_SIZE);

        vmstate_oid = vid_to_vmstate_oid(s->inode.vdi_id, vdi_index);

        create = (offset == 0);
        if (load) {
            ret = read_object(fd, (char *)data, vmstate_oid,
                              s->inode.nr_copies, data_len, offset);
        } else {
            ret = write_object(fd, (char *)data, vmstate_oid,
                               s->inode.nr_copies, data_len, offset, create);
        }

        if (ret < 0) {
            error_report("failed to save vmstate %s\n", strerror(errno));
            ret = -EIO;
            goto cleanup;
        }

        pos += data_len;
        size -= data_len;
        ret += data_len;
    }
cleanup:
    closesocket(fd);
    return ret;
}

static int sd_save_vmstate(BlockDriverState *bs, const uint8_t *data,
                           int64_t pos, int size)
{
    BDRVSheepdogState *s = bs->opaque;

    return do_load_save_vmstate(s, (uint8_t *)data, pos, size, 0);
}

static int sd_load_vmstate(BlockDriverState *bs, uint8_t *data,
                           int64_t pos, int size)
{
    BDRVSheepdogState *s = bs->opaque;

    return do_load_save_vmstate(s, data, pos, size, 1);
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
    { NULL }
};

BlockDriver bdrv_sheepdog = {
    .format_name    = "sheepdog",
    .protocol_name  = "sheepdog",
    .instance_size  = sizeof(BDRVSheepdogState),
    .bdrv_file_open = sd_open,
    .bdrv_close     = sd_close,
    .bdrv_create    = sd_create,
    .bdrv_getlength = sd_getlength,
    .bdrv_truncate  = sd_truncate,

    .bdrv_aio_readv     = sd_aio_readv,
    .bdrv_aio_writev    = sd_aio_writev,

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
}
block_init(bdrv_sheepdog_init);
