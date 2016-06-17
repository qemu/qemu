/*
 * Vhost User Bridge
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * Authors:
 *  Victor Kaplansky <victork@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

/*
 * TODO:
 *     - main should get parameters from the command line.
 *     - implement all request handlers. Still not implemented:
 *          vubr_get_queue_num_exec()
 *          vubr_send_rarp_exec()
 *     - test for broken requests and virtqueue.
 *     - implement features defined by Virtio 1.0 spec.
 *     - support mergeable buffers and indirect descriptors.
 *     - implement clean shutdown.
 *     - implement non-blocking writes to UDP backend.
 *     - implement polling strategy.
 *     - implement clean starting/stopping of vq processing
 *     - implement clean starting/stopping of used and buffers
 *       dirty page logging.
 */

#define _FILE_OFFSET_BITS 64

#include "qemu/osdep.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/unistd.h>
#include <sys/eventfd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <qemu/osdep.h>

#include <linux/vhost.h>

#include "qemu/atomic.h"
#include "standard-headers/linux/virtio_net.h"
#include "standard-headers/linux/virtio_ring.h"

#define VHOST_USER_BRIDGE_DEBUG 1

#define DPRINT(...) \
    do { \
        if (VHOST_USER_BRIDGE_DEBUG) { \
            printf(__VA_ARGS__); \
        } \
    } while (0)

typedef void (*CallbackFunc)(int sock, void *ctx);

typedef struct Event {
    void *ctx;
    CallbackFunc callback;
} Event;

typedef struct Dispatcher {
    int max_sock;
    fd_set fdset;
    Event events[FD_SETSIZE];
} Dispatcher;

static void
vubr_die(const char *s)
{
    perror(s);
    exit(1);
}

static int
dispatcher_init(Dispatcher *dispr)
{
    FD_ZERO(&dispr->fdset);
    dispr->max_sock = -1;
    return 0;
}

static int
dispatcher_add(Dispatcher *dispr, int sock, void *ctx, CallbackFunc cb)
{
    if (sock >= FD_SETSIZE) {
        fprintf(stderr,
                "Error: Failed to add new event. sock %d should be less than %d\n",
                sock, FD_SETSIZE);
        return -1;
    }

    dispr->events[sock].ctx = ctx;
    dispr->events[sock].callback = cb;

    FD_SET(sock, &dispr->fdset);
    if (sock > dispr->max_sock) {
        dispr->max_sock = sock;
    }
    DPRINT("Added sock %d for watching. max_sock: %d\n",
           sock, dispr->max_sock);
    return 0;
}

/* dispatcher_remove() is not currently in use but may be useful
 * in the future. */
static int
dispatcher_remove(Dispatcher *dispr, int sock)
{
    if (sock >= FD_SETSIZE) {
        fprintf(stderr,
                "Error: Failed to remove event. sock %d should be less than %d\n",
                sock, FD_SETSIZE);
        return -1;
    }

    FD_CLR(sock, &dispr->fdset);
    DPRINT("Sock %d removed from dispatcher watch.\n", sock);
    return 0;
}

/* timeout in us */
static int
dispatcher_wait(Dispatcher *dispr, uint32_t timeout)
{
    struct timeval tv;
    tv.tv_sec = timeout / 1000000;
    tv.tv_usec = timeout % 1000000;

    fd_set fdset = dispr->fdset;

    /* wait until some of sockets become readable. */
    int rc = select(dispr->max_sock + 1, &fdset, 0, 0, &tv);

    if (rc == -1) {
        vubr_die("select");
    }

    /* Timeout */
    if (rc == 0) {
        return 0;
    }

    /* Now call callback for every ready socket. */

    int sock;
    for (sock = 0; sock < dispr->max_sock + 1; sock++) {
        /* The callback on a socket can remove other sockets from the
         * dispatcher, thus we have to check that the socket is
         * still not removed from dispatcher's list
         */
        if (FD_ISSET(sock, &fdset) && FD_ISSET(sock, &dispr->fdset)) {
            Event *e = &dispr->events[sock];
            e->callback(sock, e->ctx);
        }
    }

    return 0;
}

typedef struct VubrVirtq {
    int call_fd;
    int kick_fd;
    uint32_t size;
    uint16_t last_avail_index;
    uint16_t last_used_index;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    uint64_t log_guest_addr;
    int enable;
} VubrVirtq;

/* Based on qemu/hw/virtio/vhost-user.c */

#define VHOST_MEMORY_MAX_NREGIONS    8
#define VHOST_USER_F_PROTOCOL_FEATURES 30
/* v1.0 compliant. */
#define VIRTIO_F_VERSION_1		32

#define VHOST_LOG_PAGE 4096

enum VhostUserProtocolFeature {
    VHOST_USER_PROTOCOL_F_MQ = 0,
    VHOST_USER_PROTOCOL_F_LOG_SHMFD = 1,
    VHOST_USER_PROTOCOL_F_RARP = 2,

    VHOST_USER_PROTOCOL_F_MAX
};

#define VHOST_USER_PROTOCOL_FEATURE_MASK ((1 << VHOST_USER_PROTOCOL_F_MAX) - 1)

typedef enum VhostUserRequest {
    VHOST_USER_NONE = 0,
    VHOST_USER_GET_FEATURES = 1,
    VHOST_USER_SET_FEATURES = 2,
    VHOST_USER_SET_OWNER = 3,
    VHOST_USER_RESET_OWNER = 4,
    VHOST_USER_SET_MEM_TABLE = 5,
    VHOST_USER_SET_LOG_BASE = 6,
    VHOST_USER_SET_LOG_FD = 7,
    VHOST_USER_SET_VRING_NUM = 8,
    VHOST_USER_SET_VRING_ADDR = 9,
    VHOST_USER_SET_VRING_BASE = 10,
    VHOST_USER_GET_VRING_BASE = 11,
    VHOST_USER_SET_VRING_KICK = 12,
    VHOST_USER_SET_VRING_CALL = 13,
    VHOST_USER_SET_VRING_ERR = 14,
    VHOST_USER_GET_PROTOCOL_FEATURES = 15,
    VHOST_USER_SET_PROTOCOL_FEATURES = 16,
    VHOST_USER_GET_QUEUE_NUM = 17,
    VHOST_USER_SET_VRING_ENABLE = 18,
    VHOST_USER_SEND_RARP = 19,
    VHOST_USER_MAX
} VhostUserRequest;

typedef struct VhostUserMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    uint64_t mmap_offset;
} VhostUserMemoryRegion;

typedef struct VhostUserMemory {
    uint32_t nregions;
    uint32_t padding;
    VhostUserMemoryRegion regions[VHOST_MEMORY_MAX_NREGIONS];
} VhostUserMemory;

typedef struct VhostUserLog {
    uint64_t mmap_size;
    uint64_t mmap_offset;
} VhostUserLog;

typedef struct VhostUserMsg {
    VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1<<2)
    uint32_t flags;
    uint32_t size; /* the following payload size */
    union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1<<8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserLog log;
    } payload;
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    int fd_num;
} QEMU_PACKED VhostUserMsg;

#define VHOST_USER_HDR_SIZE offsetof(VhostUserMsg, payload.u64)

/* The version of the protocol we support */
#define VHOST_USER_VERSION    (0x1)

#define MAX_NR_VIRTQUEUE (8)

typedef struct VubrDevRegion {
    /* Guest Physical address. */
    uint64_t gpa;
    /* Memory region size. */
    uint64_t size;
    /* QEMU virtual address (userspace). */
    uint64_t qva;
    /* Starting offset in our mmaped space. */
    uint64_t mmap_offset;
    /* Start address of mmaped space. */
    uint64_t mmap_addr;
} VubrDevRegion;

typedef struct VubrDev {
    int sock;
    Dispatcher dispatcher;
    uint32_t nregions;
    VubrDevRegion regions[VHOST_MEMORY_MAX_NREGIONS];
    VubrVirtq vq[MAX_NR_VIRTQUEUE];
    int log_call_fd;
    uint64_t log_size;
    uint8_t *log_table;
    int backend_udp_sock;
    struct sockaddr_in backend_udp_dest;
    int ready;
    uint64_t features;
    int hdrlen;
} VubrDev;

static const char *vubr_request_str[] = {
    [VHOST_USER_NONE]                   =  "VHOST_USER_NONE",
    [VHOST_USER_GET_FEATURES]           =  "VHOST_USER_GET_FEATURES",
    [VHOST_USER_SET_FEATURES]           =  "VHOST_USER_SET_FEATURES",
    [VHOST_USER_SET_OWNER]              =  "VHOST_USER_SET_OWNER",
    [VHOST_USER_RESET_OWNER]           =  "VHOST_USER_RESET_OWNER",
    [VHOST_USER_SET_MEM_TABLE]          =  "VHOST_USER_SET_MEM_TABLE",
    [VHOST_USER_SET_LOG_BASE]           =  "VHOST_USER_SET_LOG_BASE",
    [VHOST_USER_SET_LOG_FD]             =  "VHOST_USER_SET_LOG_FD",
    [VHOST_USER_SET_VRING_NUM]          =  "VHOST_USER_SET_VRING_NUM",
    [VHOST_USER_SET_VRING_ADDR]         =  "VHOST_USER_SET_VRING_ADDR",
    [VHOST_USER_SET_VRING_BASE]         =  "VHOST_USER_SET_VRING_BASE",
    [VHOST_USER_GET_VRING_BASE]         =  "VHOST_USER_GET_VRING_BASE",
    [VHOST_USER_SET_VRING_KICK]         =  "VHOST_USER_SET_VRING_KICK",
    [VHOST_USER_SET_VRING_CALL]         =  "VHOST_USER_SET_VRING_CALL",
    [VHOST_USER_SET_VRING_ERR]          =  "VHOST_USER_SET_VRING_ERR",
    [VHOST_USER_GET_PROTOCOL_FEATURES]  =  "VHOST_USER_GET_PROTOCOL_FEATURES",
    [VHOST_USER_SET_PROTOCOL_FEATURES]  =  "VHOST_USER_SET_PROTOCOL_FEATURES",
    [VHOST_USER_GET_QUEUE_NUM]          =  "VHOST_USER_GET_QUEUE_NUM",
    [VHOST_USER_SET_VRING_ENABLE]       =  "VHOST_USER_SET_VRING_ENABLE",
    [VHOST_USER_SEND_RARP]              =  "VHOST_USER_SEND_RARP",
    [VHOST_USER_MAX]                    =  "VHOST_USER_MAX",
};

static void
print_buffer(uint8_t *buf, size_t len)
{
    int i;
    printf("Raw buffer:\n");
    for (i = 0; i < len; i++) {
        if (i % 16 == 0) {
            printf("\n");
        }
        if (i % 4 == 0) {
            printf("   ");
        }
        printf("%02x ", buf[i]);
    }
    printf("\n............................................................\n");
}

/* Translate guest physical address to our virtual address.  */
static uint64_t
gpa_to_va(VubrDev *dev, uint64_t guest_addr)
{
    int i;

    /* Find matching memory region.  */
    for (i = 0; i < dev->nregions; i++) {
        VubrDevRegion *r = &dev->regions[i];

        if ((guest_addr >= r->gpa) && (guest_addr < (r->gpa + r->size))) {
            return guest_addr - r->gpa + r->mmap_addr + r->mmap_offset;
        }
    }

    assert(!"address not found in regions");
    return 0;
}

/* Translate qemu virtual address to our virtual address.  */
static uint64_t
qva_to_va(VubrDev *dev, uint64_t qemu_addr)
{
    int i;

    /* Find matching memory region.  */
    for (i = 0; i < dev->nregions; i++) {
        VubrDevRegion *r = &dev->regions[i];

        if ((qemu_addr >= r->qva) && (qemu_addr < (r->qva + r->size))) {
            return qemu_addr - r->qva + r->mmap_addr + r->mmap_offset;
        }
    }

    assert(!"address not found in regions");
    return 0;
}

static void
vubr_message_read(int conn_fd, VhostUserMsg *vmsg)
{
    char control[CMSG_SPACE(VHOST_MEMORY_MAX_NREGIONS * sizeof(int))] = { };
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = sizeof(control),
    };
    size_t fd_size;
    struct cmsghdr *cmsg;
    int rc;

    rc = recvmsg(conn_fd, &msg, 0);

    if (rc == 0) {
        vubr_die("recvmsg");
        fprintf(stderr, "Peer disconnected.\n");
        exit(1);
    }
    if (rc < 0) {
        vubr_die("recvmsg");
    }

    vmsg->fd_num = 0;
    for (cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != NULL;
         cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            fd_size = cmsg->cmsg_len - CMSG_LEN(0);
            vmsg->fd_num = fd_size / sizeof(int);
            memcpy(vmsg->fds, CMSG_DATA(cmsg), fd_size);
            break;
        }
    }

    if (vmsg->size > sizeof(vmsg->payload)) {
        fprintf(stderr,
                "Error: too big message request: %d, size: vmsg->size: %u, "
                "while sizeof(vmsg->payload) = %zu\n",
                vmsg->request, vmsg->size, sizeof(vmsg->payload));
        exit(1);
    }

    if (vmsg->size) {
        rc = read(conn_fd, &vmsg->payload, vmsg->size);
        if (rc == 0) {
            vubr_die("recvmsg");
            fprintf(stderr, "Peer disconnected.\n");
            exit(1);
        }
        if (rc < 0) {
            vubr_die("recvmsg");
        }

        assert(rc == vmsg->size);
    }
}

static void
vubr_message_write(int conn_fd, VhostUserMsg *vmsg)
{
    int rc;

    do {
        rc = write(conn_fd, vmsg, VHOST_USER_HDR_SIZE + vmsg->size);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        vubr_die("write");
    }
}

static void
vubr_backend_udp_sendbuf(VubrDev *dev, uint8_t *buf, size_t len)
{
    int slen = sizeof(struct sockaddr_in);

    if (sendto(dev->backend_udp_sock, buf, len, 0,
               (struct sockaddr *) &dev->backend_udp_dest, slen) == -1) {
        vubr_die("sendto()");
    }
}

static int
vubr_backend_udp_recvbuf(VubrDev *dev, uint8_t *buf, size_t buflen)
{
    int slen = sizeof(struct sockaddr_in);
    int rc;

    rc = recvfrom(dev->backend_udp_sock, buf, buflen, 0,
                  (struct sockaddr *) &dev->backend_udp_dest,
                  (socklen_t *)&slen);
    if (rc == -1) {
        vubr_die("recvfrom()");
    }

    return rc;
}

static void
vubr_consume_raw_packet(VubrDev *dev, uint8_t *buf, uint32_t len)
{
    int hdrlen = dev->hdrlen;
    DPRINT("    hdrlen = %d\n", dev->hdrlen);

    if (VHOST_USER_BRIDGE_DEBUG) {
        print_buffer(buf, len);
    }
    vubr_backend_udp_sendbuf(dev, buf + hdrlen, len - hdrlen);
}

/* Kick the log_call_fd if required. */
static void
vubr_log_kick(VubrDev *dev)
{
    if (dev->log_call_fd != -1) {
        DPRINT("Kicking the QEMU's log...\n");
        eventfd_write(dev->log_call_fd, 1);
    }
}

/* Kick the guest if necessary. */
static void
vubr_virtqueue_kick(VubrVirtq *vq)
{
    if (!(vq->avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
        DPRINT("Kicking the guest...\n");
        eventfd_write(vq->call_fd, 1);
    }
}

static void
vubr_log_page(uint8_t *log_table, uint64_t page)
{
    DPRINT("Logged dirty guest page: %"PRId64"\n", page);
    atomic_or(&log_table[page / 8], 1 << (page % 8));
}

static void
vubr_log_write(VubrDev *dev, uint64_t address, uint64_t length)
{
    uint64_t page;

    if (!(dev->features & (1ULL << VHOST_F_LOG_ALL)) ||
        !dev->log_table || !length) {
        return;
    }

    assert(dev->log_size > ((address + length - 1) / VHOST_LOG_PAGE / 8));

    page = address / VHOST_LOG_PAGE;
    while (page * VHOST_LOG_PAGE < address + length) {
        vubr_log_page(dev->log_table, page);
        page += VHOST_LOG_PAGE;
    }
    vubr_log_kick(dev);
}

static void
vubr_post_buffer(VubrDev *dev, VubrVirtq *vq, uint8_t *buf, int32_t len)
{
    struct vring_desc *desc = vq->desc;
    struct vring_avail *avail = vq->avail;
    struct vring_used *used = vq->used;
    uint64_t log_guest_addr = vq->log_guest_addr;
    int32_t remaining_len = len;

    unsigned int size = vq->size;

    uint16_t avail_index = atomic_mb_read(&avail->idx);

    /* We check the available descriptors before posting the
     * buffer, so here we assume that enough available
     * descriptors. */
    assert(vq->last_avail_index != avail_index);
    uint16_t a_index = vq->last_avail_index % size;
    uint16_t u_index = vq->last_used_index % size;
    uint16_t d_index = avail->ring[a_index];

    int i = d_index;
    uint32_t written_len = 0;

    do {
        DPRINT("Post packet to guest on vq:\n");
        DPRINT("    size             = %d\n", vq->size);
        DPRINT("    last_avail_index = %d\n", vq->last_avail_index);
        DPRINT("    last_used_index  = %d\n", vq->last_used_index);
        DPRINT("    a_index = %d\n", a_index);
        DPRINT("    u_index = %d\n", u_index);
        DPRINT("    d_index = %d\n", d_index);
        DPRINT("    desc[%d].addr    = 0x%016"PRIx64"\n", i, desc[i].addr);
        DPRINT("    desc[%d].len     = %d\n", i, desc[i].len);
        DPRINT("    desc[%d].flags   = %d\n", i, desc[i].flags);
        DPRINT("    avail->idx = %d\n", avail_index);
        DPRINT("    used->idx  = %d\n", used->idx);

        if (!(desc[i].flags & VRING_DESC_F_WRITE)) {
            /* FIXME: we should find writable descriptor. */
            fprintf(stderr, "Error: descriptor is not writable. Exiting.\n");
            exit(1);
        }

        void *chunk_start = (void *)(uintptr_t)gpa_to_va(dev, desc[i].addr);
        uint32_t chunk_len = desc[i].len;
        uint32_t chunk_write_len = MIN(remaining_len, chunk_len);

        memcpy(chunk_start, buf + written_len, chunk_write_len);
        vubr_log_write(dev, desc[i].addr, chunk_write_len);
        remaining_len -= chunk_write_len;
        written_len += chunk_write_len;

        if ((remaining_len == 0) || !(desc[i].flags & VRING_DESC_F_NEXT)) {
            break;
        }

        i = desc[i].next;
    } while (1);

    if (remaining_len > 0) {
            fprintf(stderr,
                    "Too long packet for RX, remaining_len = %d, Dropping...\n",
                    remaining_len);
            return;
    }

    /* Add descriptor to the used ring. */
    used->ring[u_index].id = d_index;
    used->ring[u_index].len = len;
    vubr_log_write(dev,
                   log_guest_addr + offsetof(struct vring_used, ring[u_index]),
                   sizeof(used->ring[u_index]));

    vq->last_avail_index++;
    vq->last_used_index++;

    atomic_mb_set(&used->idx, vq->last_used_index);
    vubr_log_write(dev,
                   log_guest_addr + offsetof(struct vring_used, idx),
                   sizeof(used->idx));

    /* Kick the guest if necessary. */
    vubr_virtqueue_kick(vq);
}

static int
vubr_process_desc(VubrDev *dev, VubrVirtq *vq)
{
    struct vring_desc *desc = vq->desc;
    struct vring_avail *avail = vq->avail;
    struct vring_used *used = vq->used;
    uint64_t log_guest_addr = vq->log_guest_addr;

    unsigned int size = vq->size;

    uint16_t a_index = vq->last_avail_index % size;
    uint16_t u_index = vq->last_used_index % size;
    uint16_t d_index = avail->ring[a_index];

    uint32_t i, len = 0;
    size_t buf_size = 4096;
    uint8_t buf[4096];

    DPRINT("Chunks: ");
    i = d_index;
    do {
        void *chunk_start = (void *)(uintptr_t)gpa_to_va(dev, desc[i].addr);
        uint32_t chunk_len = desc[i].len;

        assert(!(desc[i].flags & VRING_DESC_F_WRITE));

        if (len + chunk_len < buf_size) {
            memcpy(buf + len, chunk_start, chunk_len);
            DPRINT("%d ", chunk_len);
        } else {
            fprintf(stderr, "Error: too long packet. Dropping...\n");
            break;
        }

        len += chunk_len;

        if (!(desc[i].flags & VRING_DESC_F_NEXT)) {
            break;
        }

        i = desc[i].next;
    } while (1);
    DPRINT("\n");

    if (!len) {
        return -1;
    }

    /* Add descriptor to the used ring. */
    used->ring[u_index].id = d_index;
    used->ring[u_index].len = len;
    vubr_log_write(dev,
                   log_guest_addr + offsetof(struct vring_used, ring[u_index]),
                   sizeof(used->ring[u_index]));

    vubr_consume_raw_packet(dev, buf, len);

    return 0;
}

static void
vubr_process_avail(VubrDev *dev, VubrVirtq *vq)
{
    struct vring_avail *avail = vq->avail;
    struct vring_used *used = vq->used;
    uint64_t log_guest_addr = vq->log_guest_addr;

    while (vq->last_avail_index != atomic_mb_read(&avail->idx)) {
        vubr_process_desc(dev, vq);
        vq->last_avail_index++;
        vq->last_used_index++;
    }

    atomic_mb_set(&used->idx, vq->last_used_index);
    vubr_log_write(dev,
                   log_guest_addr + offsetof(struct vring_used, idx),
                   sizeof(used->idx));
}

static void
vubr_backend_recv_cb(int sock, void *ctx)
{
    VubrDev *dev = (VubrDev *) ctx;
    VubrVirtq *rx_vq = &dev->vq[0];
    uint8_t buf[4096];
    struct virtio_net_hdr_v1 *hdr = (struct virtio_net_hdr_v1 *)buf;
    int hdrlen = dev->hdrlen;
    int buflen = sizeof(buf);
    int len;

    if (!dev->ready) {
        return;
    }

    DPRINT("\n\n   ***   IN UDP RECEIVE CALLBACK    ***\n\n");
    DPRINT("    hdrlen = %d\n", hdrlen);

    uint16_t avail_index = atomic_mb_read(&rx_vq->avail->idx);

    /* If there is no available descriptors, just do nothing.
     * The buffer will be handled by next arrived UDP packet,
     * or next kick on receive virtq. */
    if (rx_vq->last_avail_index == avail_index) {
        DPRINT("Got UDP packet, but no available descriptors on RX virtq.\n");
        return;
    }

    memset(buf, 0, hdrlen);
    /* TODO: support mergeable buffers. */
    if (hdrlen == 12)
        hdr->num_buffers = 1;
    len = vubr_backend_udp_recvbuf(dev, buf + hdrlen, buflen - hdrlen);

    vubr_post_buffer(dev, rx_vq, buf, len + hdrlen);
}

static void
vubr_kick_cb(int sock, void *ctx)
{
    VubrDev *dev = (VubrDev *) ctx;
    eventfd_t kick_data;
    ssize_t rc;

    rc = eventfd_read(sock, &kick_data);
    if (rc == -1) {
        vubr_die("eventfd_read()");
    } else {
        DPRINT("Got kick_data: %016"PRIx64"\n", kick_data);
        vubr_process_avail(dev, &dev->vq[1]);
    }
}

static int
vubr_none_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("Function %s() not implemented yet.\n", __func__);
    return 0;
}

static int
vubr_get_features_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    vmsg->payload.u64 =
            ((1ULL << VIRTIO_NET_F_MRG_RXBUF) |
             (1ULL << VHOST_F_LOG_ALL) |
             (1ULL << VIRTIO_NET_F_GUEST_ANNOUNCE) |
             (1ULL << VHOST_USER_F_PROTOCOL_FEATURES));

    vmsg->size = sizeof(vmsg->payload.u64);

    DPRINT("Sending back to guest u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    /* Reply */
    return 1;
}

static int
vubr_set_features_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    dev->features = vmsg->payload.u64;
    if ((dev->features & (1ULL << VIRTIO_F_VERSION_1)) ||
        (dev->features & (1ULL << VIRTIO_NET_F_MRG_RXBUF))) {
        dev->hdrlen = 12;
    } else {
        dev->hdrlen = 10;
    }

    return 0;
}

static int
vubr_set_owner_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    return 0;
}

static void
vubr_close_log(VubrDev *dev)
{
    if (dev->log_table) {
        if (munmap(dev->log_table, dev->log_size) != 0) {
            vubr_die("munmap()");
        }

        dev->log_table = 0;
    }
    if (dev->log_call_fd != -1) {
        close(dev->log_call_fd);
        dev->log_call_fd = -1;
    }
}

static int
vubr_reset_device_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    vubr_close_log(dev);
    dev->ready = 0;
    dev->features = 0;
    return 0;
}

static int
vubr_set_mem_table_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    int i;
    VhostUserMemory *memory = &vmsg->payload.memory;
    dev->nregions = memory->nregions;

    DPRINT("Nregions: %d\n", memory->nregions);
    for (i = 0; i < dev->nregions; i++) {
        void *mmap_addr;
        VhostUserMemoryRegion *msg_region = &memory->regions[i];
        VubrDevRegion *dev_region = &dev->regions[i];

        DPRINT("Region %d\n", i);
        DPRINT("    guest_phys_addr: 0x%016"PRIx64"\n",
               msg_region->guest_phys_addr);
        DPRINT("    memory_size:     0x%016"PRIx64"\n",
               msg_region->memory_size);
        DPRINT("    userspace_addr   0x%016"PRIx64"\n",
               msg_region->userspace_addr);
        DPRINT("    mmap_offset      0x%016"PRIx64"\n",
               msg_region->mmap_offset);

        dev_region->gpa = msg_region->guest_phys_addr;
        dev_region->size = msg_region->memory_size;
        dev_region->qva = msg_region->userspace_addr;
        dev_region->mmap_offset = msg_region->mmap_offset;

        /* We don't use offset argument of mmap() since the
         * mapped address has to be page aligned, and we use huge
         * pages.  */
        mmap_addr = mmap(0, dev_region->size + dev_region->mmap_offset,
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         vmsg->fds[i], 0);

        if (mmap_addr == MAP_FAILED) {
            vubr_die("mmap");
        }
        dev_region->mmap_addr = (uint64_t)(uintptr_t)mmap_addr;
        DPRINT("    mmap_addr:       0x%016"PRIx64"\n", dev_region->mmap_addr);

        close(vmsg->fds[i]);
    }

    return 0;
}

static int
vubr_set_log_base_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    int fd;
    uint64_t log_mmap_size, log_mmap_offset;
    void *rc;

    assert(vmsg->fd_num == 1);
    fd = vmsg->fds[0];

    assert(vmsg->size == sizeof(vmsg->payload.log));
    log_mmap_offset = vmsg->payload.log.mmap_offset;
    log_mmap_size = vmsg->payload.log.mmap_size;
    DPRINT("Log mmap_offset: %"PRId64"\n", log_mmap_offset);
    DPRINT("Log mmap_size:   %"PRId64"\n", log_mmap_size);

    rc = mmap(0, log_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
              log_mmap_offset);
    if (rc == MAP_FAILED) {
        vubr_die("mmap");
    }
    dev->log_table = rc;
    dev->log_size = log_mmap_size;

    vmsg->size = sizeof(vmsg->payload.u64);
    /* Reply */
    return 1;
}

static int
vubr_set_log_fd_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    assert(vmsg->fd_num == 1);
    dev->log_call_fd = vmsg->fds[0];
    DPRINT("Got log_call_fd: %d\n", vmsg->fds[0]);
    return 0;
}

static int
vubr_set_vring_num_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;
    unsigned int num = vmsg->payload.state.num;

    DPRINT("State.index: %d\n", index);
    DPRINT("State.num:   %d\n", num);
    dev->vq[index].size = num;
    return 0;
}

static int
vubr_set_vring_addr_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    struct vhost_vring_addr *vra = &vmsg->payload.addr;
    unsigned int index = vra->index;
    VubrVirtq *vq = &dev->vq[index];

    DPRINT("vhost_vring_addr:\n");
    DPRINT("    index:  %d\n", vra->index);
    DPRINT("    flags:  %d\n", vra->flags);
    DPRINT("    desc_user_addr:   0x%016llx\n", vra->desc_user_addr);
    DPRINT("    used_user_addr:   0x%016llx\n", vra->used_user_addr);
    DPRINT("    avail_user_addr:  0x%016llx\n", vra->avail_user_addr);
    DPRINT("    log_guest_addr:   0x%016llx\n", vra->log_guest_addr);

    vq->desc = (struct vring_desc *)(uintptr_t)qva_to_va(dev, vra->desc_user_addr);
    vq->used = (struct vring_used *)(uintptr_t)qva_to_va(dev, vra->used_user_addr);
    vq->avail = (struct vring_avail *)(uintptr_t)qva_to_va(dev, vra->avail_user_addr);
    vq->log_guest_addr = vra->log_guest_addr;

    DPRINT("Setting virtq addresses:\n");
    DPRINT("    vring_desc  at %p\n", vq->desc);
    DPRINT("    vring_used  at %p\n", vq->used);
    DPRINT("    vring_avail at %p\n", vq->avail);

    vq->last_used_index = vq->used->idx;

    if (vq->last_avail_index != vq->used->idx) {
        DPRINT("Last avail index != used index: %d != %d, resuming",
               vq->last_avail_index, vq->used->idx);
        vq->last_avail_index = vq->used->idx;
    }

    return 0;
}

static int
vubr_set_vring_base_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;
    unsigned int num = vmsg->payload.state.num;

    DPRINT("State.index: %d\n", index);
    DPRINT("State.num:   %d\n", num);
    dev->vq[index].last_avail_index = num;

    return 0;
}

static int
vubr_get_vring_base_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;

    DPRINT("State.index: %d\n", index);
    vmsg->payload.state.num = dev->vq[index].last_avail_index;
    vmsg->size = sizeof(vmsg->payload.state);
    /* FIXME: this is a work-around for a bug in QEMU enabling
     * too early vrings. When protocol features are enabled,
     * we have to respect * VHOST_USER_SET_VRING_ENABLE request. */
    dev->ready = 0;

    if (dev->vq[index].call_fd != -1) {
        close(dev->vq[index].call_fd);
        dispatcher_remove(&dev->dispatcher, dev->vq[index].call_fd);
        dev->vq[index].call_fd = -1;
    }
    if (dev->vq[index].kick_fd != -1) {
        close(dev->vq[index].kick_fd);
        dispatcher_remove(&dev->dispatcher, dev->vq[index].kick_fd);
        dev->vq[index].kick_fd = -1;
    }

    /* Reply */
    return 1;
}

static int
vubr_set_vring_kick_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    uint64_t u64_arg = vmsg->payload.u64;
    int index = u64_arg & VHOST_USER_VRING_IDX_MASK;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    assert((u64_arg & VHOST_USER_VRING_NOFD_MASK) == 0);
    assert(vmsg->fd_num == 1);

    if (dev->vq[index].kick_fd != -1) {
        close(dev->vq[index].kick_fd);
        dispatcher_remove(&dev->dispatcher, dev->vq[index].kick_fd);
    }
    dev->vq[index].kick_fd = vmsg->fds[0];
    DPRINT("Got kick_fd: %d for vq: %d\n", vmsg->fds[0], index);

    if (index % 2 == 1) {
        /* TX queue. */
        dispatcher_add(&dev->dispatcher, dev->vq[index].kick_fd,
                       dev, vubr_kick_cb);

        DPRINT("Waiting for kicks on fd: %d for vq: %d\n",
               dev->vq[index].kick_fd, index);
    }
    /* We temporarily use this hack to determine that both TX and RX
     * queues are set up and ready for processing.
     * FIXME: we need to rely in VHOST_USER_SET_VRING_ENABLE and
     * actual kicks. */
    if (dev->vq[0].kick_fd != -1 &&
        dev->vq[1].kick_fd != -1) {
        dev->ready = 1;
        DPRINT("vhost-user-bridge is ready for processing queues.\n");
    }
    return 0;

}

static int
vubr_set_vring_call_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    uint64_t u64_arg = vmsg->payload.u64;
    int index = u64_arg & VHOST_USER_VRING_IDX_MASK;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);
    assert((u64_arg & VHOST_USER_VRING_NOFD_MASK) == 0);
    assert(vmsg->fd_num == 1);

    if (dev->vq[index].call_fd != -1) {
        close(dev->vq[index].call_fd);
        dispatcher_remove(&dev->dispatcher, dev->vq[index].call_fd);
    }
    dev->vq[index].call_fd = vmsg->fds[0];
    DPRINT("Got call_fd: %d for vq: %d\n", vmsg->fds[0], index);

    return 0;
}

static int
vubr_set_vring_err_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);
    return 0;
}

static int
vubr_get_protocol_features_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    vmsg->payload.u64 = 1ULL << VHOST_USER_PROTOCOL_F_LOG_SHMFD;
    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);
    vmsg->size = sizeof(vmsg->payload.u64);

    /* Reply */
    return 1;
}

static int
vubr_set_protocol_features_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    /* FIXME: unimplented */
    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);
    return 0;
}

static int
vubr_get_queue_num_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("Function %s() not implemented yet.\n", __func__);
    return 0;
}

static int
vubr_set_vring_enable_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;
    unsigned int enable = vmsg->payload.state.num;

    DPRINT("State.index: %d\n", index);
    DPRINT("State.enable:   %d\n", enable);
    dev->vq[index].enable = enable;
    return 0;
}

static int
vubr_send_rarp_exec(VubrDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("Function %s() not implemented yet.\n", __func__);
    return 0;
}

static int
vubr_execute_request(VubrDev *dev, VhostUserMsg *vmsg)
{
    /* Print out generic part of the request. */
    DPRINT(
           "==================   Vhost user message from QEMU   ==================\n");
    DPRINT("Request: %s (%d)\n", vubr_request_str[vmsg->request],
           vmsg->request);
    DPRINT("Flags:   0x%x\n", vmsg->flags);
    DPRINT("Size:    %d\n", vmsg->size);

    if (vmsg->fd_num) {
        int i;
        DPRINT("Fds:");
        for (i = 0; i < vmsg->fd_num; i++) {
            DPRINT(" %d", vmsg->fds[i]);
        }
        DPRINT("\n");
    }

    switch (vmsg->request) {
    case VHOST_USER_NONE:
        return vubr_none_exec(dev, vmsg);
    case VHOST_USER_GET_FEATURES:
        return vubr_get_features_exec(dev, vmsg);
    case VHOST_USER_SET_FEATURES:
        return vubr_set_features_exec(dev, vmsg);
    case VHOST_USER_SET_OWNER:
        return vubr_set_owner_exec(dev, vmsg);
    case VHOST_USER_RESET_OWNER:
        return vubr_reset_device_exec(dev, vmsg);
    case VHOST_USER_SET_MEM_TABLE:
        return vubr_set_mem_table_exec(dev, vmsg);
    case VHOST_USER_SET_LOG_BASE:
        return vubr_set_log_base_exec(dev, vmsg);
    case VHOST_USER_SET_LOG_FD:
        return vubr_set_log_fd_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_NUM:
        return vubr_set_vring_num_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_ADDR:
        return vubr_set_vring_addr_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_BASE:
        return vubr_set_vring_base_exec(dev, vmsg);
    case VHOST_USER_GET_VRING_BASE:
        return vubr_get_vring_base_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_KICK:
        return vubr_set_vring_kick_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_CALL:
        return vubr_set_vring_call_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_ERR:
        return vubr_set_vring_err_exec(dev, vmsg);
    case VHOST_USER_GET_PROTOCOL_FEATURES:
        return vubr_get_protocol_features_exec(dev, vmsg);
    case VHOST_USER_SET_PROTOCOL_FEATURES:
        return vubr_set_protocol_features_exec(dev, vmsg);
    case VHOST_USER_GET_QUEUE_NUM:
        return vubr_get_queue_num_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_ENABLE:
        return vubr_set_vring_enable_exec(dev, vmsg);
    case VHOST_USER_SEND_RARP:
        return vubr_send_rarp_exec(dev, vmsg);

    case VHOST_USER_MAX:
        assert(vmsg->request != VHOST_USER_MAX);
    }
    return 0;
}

static void
vubr_receive_cb(int sock, void *ctx)
{
    VubrDev *dev = (VubrDev *) ctx;
    VhostUserMsg vmsg;
    int reply_requested;

    vubr_message_read(sock, &vmsg);
    reply_requested = vubr_execute_request(dev, &vmsg);
    if (reply_requested) {
        /* Set the version in the flags when sending the reply */
        vmsg.flags &= ~VHOST_USER_VERSION_MASK;
        vmsg.flags |= VHOST_USER_VERSION;
        vmsg.flags |= VHOST_USER_REPLY_MASK;
        vubr_message_write(sock, &vmsg);
    }
}

static void
vubr_accept_cb(int sock, void *ctx)
{
    VubrDev *dev = (VubrDev *)ctx;
    int conn_fd;
    struct sockaddr_un un;
    socklen_t len = sizeof(un);

    conn_fd = accept(sock, (struct sockaddr *) &un, &len);
    if (conn_fd == -1) {
        vubr_die("accept()");
    }
    DPRINT("Got connection from remote peer on sock %d\n", conn_fd);
    dispatcher_add(&dev->dispatcher, conn_fd, ctx, vubr_receive_cb);
}

static VubrDev *
vubr_new(const char *path, bool client)
{
    VubrDev *dev = (VubrDev *) calloc(1, sizeof(VubrDev));
    dev->nregions = 0;
    int i;
    struct sockaddr_un un;
    CallbackFunc cb;
    size_t len;

    for (i = 0; i < MAX_NR_VIRTQUEUE; i++) {
        dev->vq[i] = (VubrVirtq) {
            .call_fd = -1, .kick_fd = -1,
            .size = 0,
            .last_avail_index = 0, .last_used_index = 0,
            .desc = 0, .avail = 0, .used = 0,
            .enable = 0,
        };
    }

    /* Init log */
    dev->log_call_fd = -1;
    dev->log_size = 0;
    dev->log_table = 0;
    dev->ready = 0;
    dev->features = 0;

    /* Get a UNIX socket. */
    dev->sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (dev->sock == -1) {
        vubr_die("socket");
    }

    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, path);
    len = sizeof(un.sun_family) + strlen(path);

    if (!client) {
        unlink(path);

        if (bind(dev->sock, (struct sockaddr *) &un, len) == -1) {
            vubr_die("bind");
        }

        if (listen(dev->sock, 1) == -1) {
            vubr_die("listen");
        }
        cb = vubr_accept_cb;

        DPRINT("Waiting for connections on UNIX socket %s ...\n", path);
    } else {
        if (connect(dev->sock, (struct sockaddr *)&un, len) == -1) {
            vubr_die("connect");
        }
        cb = vubr_receive_cb;
    }

    dispatcher_init(&dev->dispatcher);
    dispatcher_add(&dev->dispatcher, dev->sock, (void *)dev, cb);

    return dev;
}

static void
vubr_set_host(struct sockaddr_in *saddr, const char *host)
{
    if (isdigit(host[0])) {
        if (!inet_aton(host, &saddr->sin_addr)) {
            fprintf(stderr, "inet_aton() failed.\n");
            exit(1);
        }
    } else {
        struct hostent *he = gethostbyname(host);

        if (!he) {
            fprintf(stderr, "gethostbyname() failed.\n");
            exit(1);
        }
        saddr->sin_addr = *(struct in_addr *)he->h_addr;
    }
}

static void
vubr_backend_udp_setup(VubrDev *dev,
                       const char *local_host,
                       const char *local_port,
                       const char *remote_host,
                       const char *remote_port)
{
    int sock;
    const char *r;

    int lport, rport;

    lport = strtol(local_port, (char **)&r, 0);
    if (r == local_port) {
        fprintf(stderr, "lport parsing failed.\n");
        exit(1);
    }

    rport = strtol(remote_port, (char **)&r, 0);
    if (r == remote_port) {
        fprintf(stderr, "rport parsing failed.\n");
        exit(1);
    }

    struct sockaddr_in si_local = {
        .sin_family = AF_INET,
        .sin_port = htons(lport),
    };

    vubr_set_host(&si_local, local_host);

    /* setup destination for sends */
    dev->backend_udp_dest = (struct sockaddr_in) {
        .sin_family = AF_INET,
        .sin_port = htons(rport),
    };
    vubr_set_host(&dev->backend_udp_dest, remote_host);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        vubr_die("socket");
    }

    if (bind(sock, (struct sockaddr *)&si_local, sizeof(si_local)) == -1) {
        vubr_die("bind");
    }

    dev->backend_udp_sock = sock;
    dispatcher_add(&dev->dispatcher, sock, dev, vubr_backend_recv_cb);
    DPRINT("Waiting for data from udp backend on %s:%d...\n",
           local_host, lport);
}

static void
vubr_run(VubrDev *dev)
{
    while (1) {
        /* timeout 200ms */
        dispatcher_wait(&dev->dispatcher, 200000);
        /* Here one can try polling strategy. */
    }
}

static int
vubr_parse_host_port(const char **host, const char **port, const char *buf)
{
    char *p = strchr(buf, ':');

    if (!p) {
        return -1;
    }
    *p = '\0';
    *host = strdup(buf);
    *port = strdup(p + 1);
    return 0;
}

#define DEFAULT_UD_SOCKET "/tmp/vubr.sock"
#define DEFAULT_LHOST "127.0.0.1"
#define DEFAULT_LPORT "4444"
#define DEFAULT_RHOST "127.0.0.1"
#define DEFAULT_RPORT "5555"

static const char *ud_socket_path = DEFAULT_UD_SOCKET;
static const char *lhost = DEFAULT_LHOST;
static const char *lport = DEFAULT_LPORT;
static const char *rhost = DEFAULT_RHOST;
static const char *rport = DEFAULT_RPORT;

int
main(int argc, char *argv[])
{
    VubrDev *dev;
    int opt;
    bool client = false;

    while ((opt = getopt(argc, argv, "l:r:u:c")) != -1) {

        switch (opt) {
        case 'l':
            if (vubr_parse_host_port(&lhost, &lport, optarg) < 0) {
                goto out;
            }
            break;
        case 'r':
            if (vubr_parse_host_port(&rhost, &rport, optarg) < 0) {
                goto out;
            }
            break;
        case 'u':
            ud_socket_path = strdup(optarg);
            break;
        case 'c':
            client = true;
            break;
        default:
            goto out;
        }
    }

    DPRINT("ud socket: %s (%s)\n", ud_socket_path,
           client ? "client" : "server");
    DPRINT("local:     %s:%s\n", lhost, lport);
    DPRINT("remote:    %s:%s\n", rhost, rport);

    dev = vubr_new(ud_socket_path, client);
    if (!dev) {
        return 1;
    }

    vubr_backend_udp_setup(dev, lhost, lport, rhost, rport);
    vubr_run(dev);
    return 0;

out:
    fprintf(stderr, "Usage: %s ", argv[0]);
    fprintf(stderr, "[-c] [-u ud_socket_path] [-l lhost:lport] [-r rhost:rport]\n");
    fprintf(stderr, "\t-u path to unix doman socket. default: %s\n",
            DEFAULT_UD_SOCKET);
    fprintf(stderr, "\t-l local host and port. default: %s:%s\n",
            DEFAULT_LHOST, DEFAULT_LPORT);
    fprintf(stderr, "\t-r remote host and port. default: %s:%s\n",
            DEFAULT_RHOST, DEFAULT_RPORT);
    fprintf(stderr, "\t-c client mode\n");

    return 1;
}
