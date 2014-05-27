/*
 * vhost-user
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "sysemu/char.h"
#include "sysemu/kvm.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/vhost.h>

#define VHOST_MEMORY_MAX_NREGIONS    8

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
    VHOST_USER_MAX
} VhostUserRequest;

typedef struct VhostUserMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
} VhostUserMemoryRegion;

typedef struct VhostUserMemory {
    uint32_t nregions;
    uint32_t padding;
    VhostUserMemoryRegion regions[VHOST_MEMORY_MAX_NREGIONS];
} VhostUserMemory;

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
    };
} QEMU_PACKED VhostUserMsg;

static VhostUserMsg m __attribute__ ((unused));
#define VHOST_USER_HDR_SIZE (sizeof(m.request) \
                            + sizeof(m.flags) \
                            + sizeof(m.size))

#define VHOST_USER_PAYLOAD_SIZE (sizeof(m) - VHOST_USER_HDR_SIZE)

/* The version of the protocol we support */
#define VHOST_USER_VERSION    (0x1)

static bool ioeventfd_enabled(void)
{
    return kvm_enabled() && kvm_eventfds_enabled();
}

static unsigned long int ioctl_to_vhost_user_request[VHOST_USER_MAX] = {
    -1,                     /* VHOST_USER_NONE */
    VHOST_GET_FEATURES,     /* VHOST_USER_GET_FEATURES */
    VHOST_SET_FEATURES,     /* VHOST_USER_SET_FEATURES */
    VHOST_SET_OWNER,        /* VHOST_USER_SET_OWNER */
    VHOST_RESET_OWNER,      /* VHOST_USER_RESET_OWNER */
    VHOST_SET_MEM_TABLE,    /* VHOST_USER_SET_MEM_TABLE */
    VHOST_SET_LOG_BASE,     /* VHOST_USER_SET_LOG_BASE */
    VHOST_SET_LOG_FD,       /* VHOST_USER_SET_LOG_FD */
    VHOST_SET_VRING_NUM,    /* VHOST_USER_SET_VRING_NUM */
    VHOST_SET_VRING_ADDR,   /* VHOST_USER_SET_VRING_ADDR */
    VHOST_SET_VRING_BASE,   /* VHOST_USER_SET_VRING_BASE */
    VHOST_GET_VRING_BASE,   /* VHOST_USER_GET_VRING_BASE */
    VHOST_SET_VRING_KICK,   /* VHOST_USER_SET_VRING_KICK */
    VHOST_SET_VRING_CALL,   /* VHOST_USER_SET_VRING_CALL */
    VHOST_SET_VRING_ERR     /* VHOST_USER_SET_VRING_ERR */
};

static VhostUserRequest vhost_user_request_translate(unsigned long int request)
{
    VhostUserRequest idx;

    for (idx = 0; idx < VHOST_USER_MAX; idx++) {
        if (ioctl_to_vhost_user_request[idx] == request) {
            break;
        }
    }

    return (idx == VHOST_USER_MAX) ? VHOST_USER_NONE : idx;
}

static int vhost_user_read(struct vhost_dev *dev, VhostUserMsg *msg)
{
    CharDriverState *chr = dev->opaque;
    uint8_t *p = (uint8_t *) msg;
    int r, size = VHOST_USER_HDR_SIZE;

    r = qemu_chr_fe_read_all(chr, p, size);
    if (r != size) {
        error_report("Failed to read msg header. Read %d instead of %d.\n", r,
                size);
        goto fail;
    }

    /* validate received flags */
    if (msg->flags != (VHOST_USER_REPLY_MASK | VHOST_USER_VERSION)) {
        error_report("Failed to read msg header."
                " Flags 0x%x instead of 0x%x.\n", msg->flags,
                VHOST_USER_REPLY_MASK | VHOST_USER_VERSION);
        goto fail;
    }

    /* validate message size is sane */
    if (msg->size > VHOST_USER_PAYLOAD_SIZE) {
        error_report("Failed to read msg header."
                " Size %d exceeds the maximum %zu.\n", msg->size,
                VHOST_USER_PAYLOAD_SIZE);
        goto fail;
    }

    if (msg->size) {
        p += VHOST_USER_HDR_SIZE;
        size = msg->size;
        r = qemu_chr_fe_read_all(chr, p, size);
        if (r != size) {
            error_report("Failed to read msg payload."
                         " Read %d instead of %d.\n", r, msg->size);
            goto fail;
        }
    }

    return 0;

fail:
    return -1;
}

static int vhost_user_write(struct vhost_dev *dev, VhostUserMsg *msg,
                            int *fds, int fd_num)
{
    CharDriverState *chr = dev->opaque;
    int size = VHOST_USER_HDR_SIZE + msg->size;

    if (fd_num) {
        qemu_chr_fe_set_msgfds(chr, fds, fd_num);
    }

    return qemu_chr_fe_write_all(chr, (const uint8_t *) msg, size) == size ?
            0 : -1;
}

static int vhost_user_call(struct vhost_dev *dev, unsigned long int request,
        void *arg)
{
    VhostUserMsg msg;
    VhostUserRequest msg_request;
    RAMBlock *block = 0;
    struct vhost_vring_file *file = 0;
    int need_reply = 0;
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    size_t fd_num = 0;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    msg_request = vhost_user_request_translate(request);
    msg.request = msg_request;
    msg.flags = VHOST_USER_VERSION;
    msg.size = 0;

    switch (request) {
    case VHOST_GET_FEATURES:
        need_reply = 1;
        break;

    case VHOST_SET_FEATURES:
    case VHOST_SET_LOG_BASE:
        msg.u64 = *((__u64 *) arg);
        msg.size = sizeof(m.u64);
        break;

    case VHOST_SET_OWNER:
    case VHOST_RESET_OWNER:
        break;

    case VHOST_SET_MEM_TABLE:
        QTAILQ_FOREACH(block, &ram_list.blocks, next)
        {
            if (block->fd > 0) {
                msg.memory.regions[fd_num].userspace_addr =
                    (uintptr_t) block->host;
                msg.memory.regions[fd_num].memory_size = block->length;
                msg.memory.regions[fd_num].guest_phys_addr = block->offset;
                fds[fd_num++] = block->fd;
            }
        }

        msg.memory.nregions = fd_num;

        if (!fd_num) {
            error_report("Failed initializing vhost-user memory map\n"
                    "consider using -object memory-backend-file share=on\n");
            return -1;
        }

        msg.size = sizeof(m.memory.nregions);
        msg.size += sizeof(m.memory.padding);
        msg.size += fd_num * sizeof(VhostUserMemoryRegion);

        break;

    case VHOST_SET_LOG_FD:
        fds[fd_num++] = *((int *) arg);
        break;

    case VHOST_SET_VRING_NUM:
    case VHOST_SET_VRING_BASE:
        memcpy(&msg.state, arg, sizeof(struct vhost_vring_state));
        msg.size = sizeof(m.state);
        break;

    case VHOST_GET_VRING_BASE:
        memcpy(&msg.state, arg, sizeof(struct vhost_vring_state));
        msg.size = sizeof(m.state);
        need_reply = 1;
        break;

    case VHOST_SET_VRING_ADDR:
        memcpy(&msg.addr, arg, sizeof(struct vhost_vring_addr));
        msg.size = sizeof(m.addr);
        break;

    case VHOST_SET_VRING_KICK:
    case VHOST_SET_VRING_CALL:
    case VHOST_SET_VRING_ERR:
        file = arg;
        msg.u64 = file->index & VHOST_USER_VRING_IDX_MASK;
        msg.size = sizeof(m.u64);
        if (ioeventfd_enabled() && file->fd > 0) {
            fds[fd_num++] = file->fd;
        } else {
            msg.u64 |= VHOST_USER_VRING_NOFD_MASK;
        }
        break;
    default:
        error_report("vhost-user trying to send unhandled ioctl\n");
        return -1;
        break;
    }

    if (vhost_user_write(dev, &msg, fds, fd_num) < 0) {
        return 0;
    }

    if (need_reply) {
        if (vhost_user_read(dev, &msg) < 0) {
            return 0;
        }

        if (msg_request != msg.request) {
            error_report("Received unexpected msg type."
                    " Expected %d received %d\n", msg_request, msg.request);
            return -1;
        }

        switch (msg_request) {
        case VHOST_USER_GET_FEATURES:
            if (msg.size != sizeof(m.u64)) {
                error_report("Received bad msg size.\n");
                return -1;
            }
            *((__u64 *) arg) = msg.u64;
            break;
        case VHOST_USER_GET_VRING_BASE:
            if (msg.size != sizeof(m.state)) {
                error_report("Received bad msg size.\n");
                return -1;
            }
            memcpy(arg, &msg.state, sizeof(struct vhost_vring_state));
            break;
        default:
            error_report("Received unexpected msg type.\n");
            return -1;
            break;
        }
    }

    return 0;
}

static int vhost_user_init(struct vhost_dev *dev, void *opaque)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    dev->opaque = opaque;

    return 0;
}

static int vhost_user_cleanup(struct vhost_dev *dev)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    dev->opaque = 0;

    return 0;
}

const VhostOps user_ops = {
        .backend_type = VHOST_BACKEND_TYPE_USER,
        .vhost_call = vhost_user_call,
        .vhost_backend_init = vhost_user_init,
        .vhost_backend_cleanup = vhost_user_cleanup
        };
