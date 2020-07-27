/*
 * Vhost User library
 *
 * Copyright IBM, Corp. 2007
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Authors:
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Marc-André Lureau <mlureau@redhat.com>
 *  Victor Kaplansky <victork@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

/* this code avoids GLib dependency */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include "qemu/compiler.h"

#if defined(__linux__)
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/vhost.h>

#ifdef __NR_userfaultfd
#include <linux/userfaultfd.h>
#endif

#endif

#include "qemu/atomic.h"
#include "qemu/osdep.h"
#include "qemu/memfd.h"

#include "libvhost-user.h"

/* usually provided by GLib */
#ifndef MIN
#define MIN(x, y) ({                            \
            typeof(x) _min1 = (x);              \
            typeof(y) _min2 = (y);              \
            (void) (&_min1 == &_min2);          \
            _min1 < _min2 ? _min1 : _min2; })
#endif

/* Round number down to multiple */
#define ALIGN_DOWN(n, m) ((n) / (m) * (m))

/* Round number up to multiple */
#define ALIGN_UP(n, m) ALIGN_DOWN((n) + (m) - 1, (m))

/* Align each region to cache line size in inflight buffer */
#define INFLIGHT_ALIGNMENT 64

/* The version of inflight buffer */
#define INFLIGHT_VERSION 1

#define VHOST_USER_HDR_SIZE offsetof(VhostUserMsg, payload.u64)

/* The version of the protocol we support */
#define VHOST_USER_VERSION 1
#define LIBVHOST_USER_DEBUG 0

#define DPRINT(...)                             \
    do {                                        \
        if (LIBVHOST_USER_DEBUG) {              \
            fprintf(stderr, __VA_ARGS__);        \
        }                                       \
    } while (0)

static inline
bool has_feature(uint64_t features, unsigned int fbit)
{
    assert(fbit < 64);
    return !!(features & (1ULL << fbit));
}

static inline
bool vu_has_feature(VuDev *dev,
                    unsigned int fbit)
{
    return has_feature(dev->features, fbit);
}

static inline bool vu_has_protocol_feature(VuDev *dev, unsigned int fbit)
{
    return has_feature(dev->protocol_features, fbit);
}

static const char *
vu_request_to_string(unsigned int req)
{
#define REQ(req) [req] = #req
    static const char *vu_request_str[] = {
        REQ(VHOST_USER_NONE),
        REQ(VHOST_USER_GET_FEATURES),
        REQ(VHOST_USER_SET_FEATURES),
        REQ(VHOST_USER_SET_OWNER),
        REQ(VHOST_USER_RESET_OWNER),
        REQ(VHOST_USER_SET_MEM_TABLE),
        REQ(VHOST_USER_SET_LOG_BASE),
        REQ(VHOST_USER_SET_LOG_FD),
        REQ(VHOST_USER_SET_VRING_NUM),
        REQ(VHOST_USER_SET_VRING_ADDR),
        REQ(VHOST_USER_SET_VRING_BASE),
        REQ(VHOST_USER_GET_VRING_BASE),
        REQ(VHOST_USER_SET_VRING_KICK),
        REQ(VHOST_USER_SET_VRING_CALL),
        REQ(VHOST_USER_SET_VRING_ERR),
        REQ(VHOST_USER_GET_PROTOCOL_FEATURES),
        REQ(VHOST_USER_SET_PROTOCOL_FEATURES),
        REQ(VHOST_USER_GET_QUEUE_NUM),
        REQ(VHOST_USER_SET_VRING_ENABLE),
        REQ(VHOST_USER_SEND_RARP),
        REQ(VHOST_USER_NET_SET_MTU),
        REQ(VHOST_USER_SET_SLAVE_REQ_FD),
        REQ(VHOST_USER_IOTLB_MSG),
        REQ(VHOST_USER_SET_VRING_ENDIAN),
        REQ(VHOST_USER_GET_CONFIG),
        REQ(VHOST_USER_SET_CONFIG),
        REQ(VHOST_USER_POSTCOPY_ADVISE),
        REQ(VHOST_USER_POSTCOPY_LISTEN),
        REQ(VHOST_USER_POSTCOPY_END),
        REQ(VHOST_USER_GET_INFLIGHT_FD),
        REQ(VHOST_USER_SET_INFLIGHT_FD),
        REQ(VHOST_USER_GPU_SET_SOCKET),
        REQ(VHOST_USER_VRING_KICK),
        REQ(VHOST_USER_GET_MAX_MEM_SLOTS),
        REQ(VHOST_USER_ADD_MEM_REG),
        REQ(VHOST_USER_REM_MEM_REG),
        REQ(VHOST_USER_MAX),
    };
#undef REQ

    if (req < VHOST_USER_MAX) {
        return vu_request_str[req];
    } else {
        return "unknown";
    }
}

static void
vu_panic(VuDev *dev, const char *msg, ...)
{
    char *buf = NULL;
    va_list ap;

    va_start(ap, msg);
    if (vasprintf(&buf, msg, ap) < 0) {
        buf = NULL;
    }
    va_end(ap);

    dev->broken = true;
    dev->panic(dev, buf);
    free(buf);

    /*
     * FIXME:
     * find a way to call virtio_error, or perhaps close the connection?
     */
}

/* Translate guest physical address to our virtual address.  */
void *
vu_gpa_to_va(VuDev *dev, uint64_t *plen, uint64_t guest_addr)
{
    int i;

    if (*plen == 0) {
        return NULL;
    }

    /* Find matching memory region.  */
    for (i = 0; i < dev->nregions; i++) {
        VuDevRegion *r = &dev->regions[i];

        if ((guest_addr >= r->gpa) && (guest_addr < (r->gpa + r->size))) {
            if ((guest_addr + *plen) > (r->gpa + r->size)) {
                *plen = r->gpa + r->size - guest_addr;
            }
            return (void *)(uintptr_t)
                guest_addr - r->gpa + r->mmap_addr + r->mmap_offset;
        }
    }

    return NULL;
}

/* Translate qemu virtual address to our virtual address.  */
static void *
qva_to_va(VuDev *dev, uint64_t qemu_addr)
{
    int i;

    /* Find matching memory region.  */
    for (i = 0; i < dev->nregions; i++) {
        VuDevRegion *r = &dev->regions[i];

        if ((qemu_addr >= r->qva) && (qemu_addr < (r->qva + r->size))) {
            return (void *)(uintptr_t)
                qemu_addr - r->qva + r->mmap_addr + r->mmap_offset;
        }
    }

    return NULL;
}

static void
vmsg_close_fds(VhostUserMsg *vmsg)
{
    int i;

    for (i = 0; i < vmsg->fd_num; i++) {
        close(vmsg->fds[i]);
    }
}

/* Set reply payload.u64 and clear request flags and fd_num */
static void vmsg_set_reply_u64(VhostUserMsg *vmsg, uint64_t val)
{
    vmsg->flags = 0; /* defaults will be set by vu_send_reply() */
    vmsg->size = sizeof(vmsg->payload.u64);
    vmsg->payload.u64 = val;
    vmsg->fd_num = 0;
}

/* A test to see if we have userfault available */
static bool
have_userfault(void)
{
#if defined(__linux__) && defined(__NR_userfaultfd) &&\
        defined(UFFD_FEATURE_MISSING_SHMEM) &&\
        defined(UFFD_FEATURE_MISSING_HUGETLBFS)
    /* Now test the kernel we're running on really has the features */
    int ufd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    struct uffdio_api api_struct;
    if (ufd < 0) {
        return false;
    }

    api_struct.api = UFFD_API;
    api_struct.features = UFFD_FEATURE_MISSING_SHMEM |
                          UFFD_FEATURE_MISSING_HUGETLBFS;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        close(ufd);
        return false;
    }
    close(ufd);
    return true;

#else
    return false;
#endif
}

static bool
vu_message_read(VuDev *dev, int conn_fd, VhostUserMsg *vmsg)
{
    char control[CMSG_SPACE(VHOST_MEMORY_BASELINE_NREGIONS * sizeof(int))] = {};
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

    do {
        rc = recvmsg(conn_fd, &msg, 0);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (rc < 0) {
        vu_panic(dev, "Error while recvmsg: %s", strerror(errno));
        return false;
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
        vu_panic(dev,
                 "Error: too big message request: %d, size: vmsg->size: %u, "
                 "while sizeof(vmsg->payload) = %zu\n",
                 vmsg->request, vmsg->size, sizeof(vmsg->payload));
        goto fail;
    }

    if (vmsg->size) {
        do {
            rc = read(conn_fd, &vmsg->payload, vmsg->size);
        } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

        if (rc <= 0) {
            vu_panic(dev, "Error while reading: %s", strerror(errno));
            goto fail;
        }

        assert(rc == vmsg->size);
    }

    return true;

fail:
    vmsg_close_fds(vmsg);

    return false;
}

static bool
vu_message_write(VuDev *dev, int conn_fd, VhostUserMsg *vmsg)
{
    int rc;
    uint8_t *p = (uint8_t *)vmsg;
    char control[CMSG_SPACE(VHOST_MEMORY_BASELINE_NREGIONS * sizeof(int))] = {};
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
    };
    struct cmsghdr *cmsg;

    memset(control, 0, sizeof(control));
    assert(vmsg->fd_num <= VHOST_MEMORY_BASELINE_NREGIONS);
    if (vmsg->fd_num > 0) {
        size_t fdsize = vmsg->fd_num * sizeof(int);
        msg.msg_controllen = CMSG_SPACE(fdsize);
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(fdsize);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsg), vmsg->fds, fdsize);
    } else {
        msg.msg_controllen = 0;
    }

    do {
        rc = sendmsg(conn_fd, &msg, 0);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (vmsg->size) {
        do {
            if (vmsg->data) {
                rc = write(conn_fd, vmsg->data, vmsg->size);
            } else {
                rc = write(conn_fd, p + VHOST_USER_HDR_SIZE, vmsg->size);
            }
        } while (rc < 0 && (errno == EINTR || errno == EAGAIN));
    }

    if (rc <= 0) {
        vu_panic(dev, "Error while writing: %s", strerror(errno));
        return false;
    }

    return true;
}

static bool
vu_send_reply(VuDev *dev, int conn_fd, VhostUserMsg *vmsg)
{
    /* Set the version in the flags when sending the reply */
    vmsg->flags &= ~VHOST_USER_VERSION_MASK;
    vmsg->flags |= VHOST_USER_VERSION;
    vmsg->flags |= VHOST_USER_REPLY_MASK;

    return vu_message_write(dev, conn_fd, vmsg);
}

/*
 * Processes a reply on the slave channel.
 * Entered with slave_mutex held and releases it before exit.
 * Returns true on success.
 */
static bool
vu_process_message_reply(VuDev *dev, const VhostUserMsg *vmsg)
{
    VhostUserMsg msg_reply;
    bool result = false;

    if ((vmsg->flags & VHOST_USER_NEED_REPLY_MASK) == 0) {
        result = true;
        goto out;
    }

    if (!vu_message_read(dev, dev->slave_fd, &msg_reply)) {
        goto out;
    }

    if (msg_reply.request != vmsg->request) {
        DPRINT("Received unexpected msg type. Expected %d received %d",
               vmsg->request, msg_reply.request);
        goto out;
    }

    result = msg_reply.payload.u64 == 0;

out:
    pthread_mutex_unlock(&dev->slave_mutex);
    return result;
}

/* Kick the log_call_fd if required. */
static void
vu_log_kick(VuDev *dev)
{
    if (dev->log_call_fd != -1) {
        DPRINT("Kicking the QEMU's log...\n");
        if (eventfd_write(dev->log_call_fd, 1) < 0) {
            vu_panic(dev, "Error writing eventfd: %s", strerror(errno));
        }
    }
}

static void
vu_log_page(uint8_t *log_table, uint64_t page)
{
    DPRINT("Logged dirty guest page: %"PRId64"\n", page);
    atomic_or(&log_table[page / 8], 1 << (page % 8));
}

static void
vu_log_write(VuDev *dev, uint64_t address, uint64_t length)
{
    uint64_t page;

    if (!(dev->features & (1ULL << VHOST_F_LOG_ALL)) ||
        !dev->log_table || !length) {
        return;
    }

    assert(dev->log_size > ((address + length - 1) / VHOST_LOG_PAGE / 8));

    page = address / VHOST_LOG_PAGE;
    while (page * VHOST_LOG_PAGE < address + length) {
        vu_log_page(dev->log_table, page);
        page += 1;
    }

    vu_log_kick(dev);
}

static void
vu_kick_cb(VuDev *dev, int condition, void *data)
{
    int index = (intptr_t)data;
    VuVirtq *vq = &dev->vq[index];
    int sock = vq->kick_fd;
    eventfd_t kick_data;
    ssize_t rc;

    rc = eventfd_read(sock, &kick_data);
    if (rc == -1) {
        vu_panic(dev, "kick eventfd_read(): %s", strerror(errno));
        dev->remove_watch(dev, dev->vq[index].kick_fd);
    } else {
        DPRINT("Got kick_data: %016"PRIx64" handler:%p idx:%d\n",
               kick_data, vq->handler, index);
        if (vq->handler) {
            vq->handler(dev, index);
        }
    }
}

static bool
vu_get_features_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    vmsg->payload.u64 =
        /*
         * The following VIRTIO feature bits are supported by our virtqueue
         * implementation:
         */
        1ULL << VIRTIO_F_NOTIFY_ON_EMPTY |
        1ULL << VIRTIO_RING_F_INDIRECT_DESC |
        1ULL << VIRTIO_RING_F_EVENT_IDX |
        1ULL << VIRTIO_F_VERSION_1 |

        /* vhost-user feature bits */
        1ULL << VHOST_F_LOG_ALL |
        1ULL << VHOST_USER_F_PROTOCOL_FEATURES;

    if (dev->iface->get_features) {
        vmsg->payload.u64 |= dev->iface->get_features(dev);
    }

    vmsg->size = sizeof(vmsg->payload.u64);
    vmsg->fd_num = 0;

    DPRINT("Sending back to guest u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    return true;
}

static void
vu_set_enable_all_rings(VuDev *dev, bool enabled)
{
    uint16_t i;

    for (i = 0; i < dev->max_queues; i++) {
        dev->vq[i].enable = enabled;
    }
}

static bool
vu_set_features_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    dev->features = vmsg->payload.u64;

    if (!(dev->features & VHOST_USER_F_PROTOCOL_FEATURES)) {
        vu_set_enable_all_rings(dev, true);
    }

    if (dev->iface->set_features) {
        dev->iface->set_features(dev, dev->features);
    }

    return false;
}

static bool
vu_set_owner_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    return false;
}

static void
vu_close_log(VuDev *dev)
{
    if (dev->log_table) {
        if (munmap(dev->log_table, dev->log_size) != 0) {
            perror("close log munmap() error");
        }

        dev->log_table = NULL;
    }
    if (dev->log_call_fd != -1) {
        close(dev->log_call_fd);
        dev->log_call_fd = -1;
    }
}

static bool
vu_reset_device_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    vu_set_enable_all_rings(dev, false);

    return false;
}

static bool
map_ring(VuDev *dev, VuVirtq *vq)
{
    vq->vring.desc = qva_to_va(dev, vq->vra.desc_user_addr);
    vq->vring.used = qva_to_va(dev, vq->vra.used_user_addr);
    vq->vring.avail = qva_to_va(dev, vq->vra.avail_user_addr);

    DPRINT("Setting virtq addresses:\n");
    DPRINT("    vring_desc  at %p\n", vq->vring.desc);
    DPRINT("    vring_used  at %p\n", vq->vring.used);
    DPRINT("    vring_avail at %p\n", vq->vring.avail);

    return !(vq->vring.desc && vq->vring.used && vq->vring.avail);
}

static bool
generate_faults(VuDev *dev) {
    int i;
    for (i = 0; i < dev->nregions; i++) {
        VuDevRegion *dev_region = &dev->regions[i];
        int ret;
#ifdef UFFDIO_REGISTER
        /*
         * We should already have an open ufd. Mark each memory
         * range as ufd.
         * Discard any mapping we have here; note I can't use MADV_REMOVE
         * or fallocate to make the hole since I don't want to lose
         * data that's already arrived in the shared process.
         * TODO: How to do hugepage
         */
        ret = madvise((void *)(uintptr_t)dev_region->mmap_addr,
                      dev_region->size + dev_region->mmap_offset,
                      MADV_DONTNEED);
        if (ret) {
            fprintf(stderr,
                    "%s: Failed to madvise(DONTNEED) region %d: %s\n",
                    __func__, i, strerror(errno));
        }
        /*
         * Turn off transparent hugepages so we dont get lose wakeups
         * in neighbouring pages.
         * TODO: Turn this backon later.
         */
        ret = madvise((void *)(uintptr_t)dev_region->mmap_addr,
                      dev_region->size + dev_region->mmap_offset,
                      MADV_NOHUGEPAGE);
        if (ret) {
            /*
             * Note: This can happen legally on kernels that are configured
             * without madvise'able hugepages
             */
            fprintf(stderr,
                    "%s: Failed to madvise(NOHUGEPAGE) region %d: %s\n",
                    __func__, i, strerror(errno));
        }
        struct uffdio_register reg_struct;
        reg_struct.range.start = (uintptr_t)dev_region->mmap_addr;
        reg_struct.range.len = dev_region->size + dev_region->mmap_offset;
        reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

        if (ioctl(dev->postcopy_ufd, UFFDIO_REGISTER, &reg_struct)) {
            vu_panic(dev, "%s: Failed to userfault region %d "
                          "@%p + size:%zx offset: %zx: (ufd=%d)%s\n",
                     __func__, i,
                     dev_region->mmap_addr,
                     dev_region->size, dev_region->mmap_offset,
                     dev->postcopy_ufd, strerror(errno));
            return false;
        }
        if (!(reg_struct.ioctls & ((__u64)1 << _UFFDIO_COPY))) {
            vu_panic(dev, "%s Region (%d) doesn't support COPY",
                     __func__, i);
            return false;
        }
        DPRINT("%s: region %d: Registered userfault for %"
               PRIx64 " + %" PRIx64 "\n", __func__, i,
               (uint64_t)reg_struct.range.start,
               (uint64_t)reg_struct.range.len);
        /* Now it's registered we can let the client at it */
        if (mprotect((void *)(uintptr_t)dev_region->mmap_addr,
                     dev_region->size + dev_region->mmap_offset,
                     PROT_READ | PROT_WRITE)) {
            vu_panic(dev, "failed to mprotect region %d for postcopy (%s)",
                     i, strerror(errno));
            return false;
        }
        /* TODO: Stash 'zero' support flags somewhere */
#endif
    }

    return true;
}

static bool
vu_add_mem_reg(VuDev *dev, VhostUserMsg *vmsg) {
    int i;
    bool track_ramblocks = dev->postcopy_listening;
    VhostUserMemoryRegion m = vmsg->payload.memreg.region, *msg_region = &m;
    VuDevRegion *dev_region = &dev->regions[dev->nregions];
    void *mmap_addr;

    /*
     * If we are in postcopy mode and we receive a u64 payload with a 0 value
     * we know all the postcopy client bases have been recieved, and we
     * should start generating faults.
     */
    if (track_ramblocks &&
        vmsg->size == sizeof(vmsg->payload.u64) &&
        vmsg->payload.u64 == 0) {
        (void)generate_faults(dev);
        return false;
    }

    DPRINT("Adding region: %d\n", dev->nregions);
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

    /*
     * We don't use offset argument of mmap() since the
     * mapped address has to be page aligned, and we use huge
     * pages.
     */
    if (track_ramblocks) {
        /*
         * In postcopy we're using PROT_NONE here to catch anyone
         * accessing it before we userfault.
         */
        mmap_addr = mmap(0, dev_region->size + dev_region->mmap_offset,
                         PROT_NONE, MAP_SHARED,
                         vmsg->fds[0], 0);
    } else {
        mmap_addr = mmap(0, dev_region->size + dev_region->mmap_offset,
                         PROT_READ | PROT_WRITE, MAP_SHARED, vmsg->fds[0],
                         0);
    }

    if (mmap_addr == MAP_FAILED) {
        vu_panic(dev, "region mmap error: %s", strerror(errno));
    } else {
        dev_region->mmap_addr = (uint64_t)(uintptr_t)mmap_addr;
        DPRINT("    mmap_addr:       0x%016"PRIx64"\n",
               dev_region->mmap_addr);
    }

    close(vmsg->fds[0]);

    if (track_ramblocks) {
        /*
         * Return the address to QEMU so that it can translate the ufd
         * fault addresses back.
         */
        msg_region->userspace_addr = (uintptr_t)(mmap_addr +
                                                 dev_region->mmap_offset);

        /* Send the message back to qemu with the addresses filled in. */
        vmsg->fd_num = 0;
        if (!vu_send_reply(dev, dev->sock, vmsg)) {
            vu_panic(dev, "failed to respond to add-mem-region for postcopy");
            return false;
        }

        DPRINT("Successfully added new region in postcopy\n");
        dev->nregions++;
        return false;

    } else {
        for (i = 0; i < dev->max_queues; i++) {
            if (dev->vq[i].vring.desc) {
                if (map_ring(dev, &dev->vq[i])) {
                    vu_panic(dev, "remapping queue %d for new memory region",
                             i);
                }
            }
        }

        DPRINT("Successfully added new region\n");
        dev->nregions++;
        vmsg_set_reply_u64(vmsg, 0);
        return true;
    }
}

static inline bool reg_equal(VuDevRegion *vudev_reg,
                             VhostUserMemoryRegion *msg_reg)
{
    if (vudev_reg->gpa == msg_reg->guest_phys_addr &&
        vudev_reg->qva == msg_reg->userspace_addr &&
        vudev_reg->size == msg_reg->memory_size) {
        return true;
    }

    return false;
}

static bool
vu_rem_mem_reg(VuDev *dev, VhostUserMsg *vmsg) {
    int i, j;
    bool found = false;
    VuDevRegion shadow_regions[VHOST_USER_MAX_RAM_SLOTS] = {};
    VhostUserMemoryRegion m = vmsg->payload.memreg.region, *msg_region = &m;

    DPRINT("Removing region:\n");
    DPRINT("    guest_phys_addr: 0x%016"PRIx64"\n",
           msg_region->guest_phys_addr);
    DPRINT("    memory_size:     0x%016"PRIx64"\n",
           msg_region->memory_size);
    DPRINT("    userspace_addr   0x%016"PRIx64"\n",
           msg_region->userspace_addr);
    DPRINT("    mmap_offset      0x%016"PRIx64"\n",
           msg_region->mmap_offset);

    for (i = 0, j = 0; i < dev->nregions; i++) {
        if (!reg_equal(&dev->regions[i], msg_region)) {
            shadow_regions[j].gpa = dev->regions[i].gpa;
            shadow_regions[j].size = dev->regions[i].size;
            shadow_regions[j].qva = dev->regions[i].qva;
            shadow_regions[j].mmap_offset = dev->regions[i].mmap_offset;
            j++;
        } else {
            found = true;
            VuDevRegion *r = &dev->regions[i];
            void *m = (void *) (uintptr_t) r->mmap_addr;

            if (m) {
                munmap(m, r->size + r->mmap_offset);
            }
        }
    }

    if (found) {
        memcpy(dev->regions, shadow_regions,
               sizeof(VuDevRegion) * VHOST_USER_MAX_RAM_SLOTS);
        DPRINT("Successfully removed a region\n");
        dev->nregions--;
        vmsg_set_reply_u64(vmsg, 0);
    } else {
        vu_panic(dev, "Specified region not found\n");
    }

    return true;
}

static bool
vu_set_mem_table_exec_postcopy(VuDev *dev, VhostUserMsg *vmsg)
{
    int i;
    VhostUserMemory m = vmsg->payload.memory, *memory = &m;
    dev->nregions = memory->nregions;

    DPRINT("Nregions: %d\n", memory->nregions);
    for (i = 0; i < dev->nregions; i++) {
        void *mmap_addr;
        VhostUserMemoryRegion *msg_region = &memory->regions[i];
        VuDevRegion *dev_region = &dev->regions[i];

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
         * pages.
         * In postcopy we're using PROT_NONE here to catch anyone
         * accessing it before we userfault
         */
        mmap_addr = mmap(0, dev_region->size + dev_region->mmap_offset,
                         PROT_NONE, MAP_SHARED,
                         vmsg->fds[i], 0);

        if (mmap_addr == MAP_FAILED) {
            vu_panic(dev, "region mmap error: %s", strerror(errno));
        } else {
            dev_region->mmap_addr = (uint64_t)(uintptr_t)mmap_addr;
            DPRINT("    mmap_addr:       0x%016"PRIx64"\n",
                   dev_region->mmap_addr);
        }

        /* Return the address to QEMU so that it can translate the ufd
         * fault addresses back.
         */
        msg_region->userspace_addr = (uintptr_t)(mmap_addr +
                                                 dev_region->mmap_offset);
        close(vmsg->fds[i]);
    }

    /* Send the message back to qemu with the addresses filled in */
    vmsg->fd_num = 0;
    if (!vu_send_reply(dev, dev->sock, vmsg)) {
        vu_panic(dev, "failed to respond to set-mem-table for postcopy");
        return false;
    }

    /* Wait for QEMU to confirm that it's registered the handler for the
     * faults.
     */
    if (!vu_message_read(dev, dev->sock, vmsg) ||
        vmsg->size != sizeof(vmsg->payload.u64) ||
        vmsg->payload.u64 != 0) {
        vu_panic(dev, "failed to receive valid ack for postcopy set-mem-table");
        return false;
    }

    /* OK, now we can go and register the memory and generate faults */
    (void)generate_faults(dev);

    return false;
}

static bool
vu_set_mem_table_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int i;
    VhostUserMemory m = vmsg->payload.memory, *memory = &m;

    for (i = 0; i < dev->nregions; i++) {
        VuDevRegion *r = &dev->regions[i];
        void *m = (void *) (uintptr_t) r->mmap_addr;

        if (m) {
            munmap(m, r->size + r->mmap_offset);
        }
    }
    dev->nregions = memory->nregions;

    if (dev->postcopy_listening) {
        return vu_set_mem_table_exec_postcopy(dev, vmsg);
    }

    DPRINT("Nregions: %d\n", memory->nregions);
    for (i = 0; i < dev->nregions; i++) {
        void *mmap_addr;
        VhostUserMemoryRegion *msg_region = &memory->regions[i];
        VuDevRegion *dev_region = &dev->regions[i];

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
            vu_panic(dev, "region mmap error: %s", strerror(errno));
        } else {
            dev_region->mmap_addr = (uint64_t)(uintptr_t)mmap_addr;
            DPRINT("    mmap_addr:       0x%016"PRIx64"\n",
                   dev_region->mmap_addr);
        }

        close(vmsg->fds[i]);
    }

    for (i = 0; i < dev->max_queues; i++) {
        if (dev->vq[i].vring.desc) {
            if (map_ring(dev, &dev->vq[i])) {
                vu_panic(dev, "remaping queue %d during setmemtable", i);
            }
        }
    }

    return false;
}

static bool
vu_set_log_base_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int fd;
    uint64_t log_mmap_size, log_mmap_offset;
    void *rc;

    if (vmsg->fd_num != 1 ||
        vmsg->size != sizeof(vmsg->payload.log)) {
        vu_panic(dev, "Invalid log_base message");
        return true;
    }

    fd = vmsg->fds[0];
    log_mmap_offset = vmsg->payload.log.mmap_offset;
    log_mmap_size = vmsg->payload.log.mmap_size;
    DPRINT("Log mmap_offset: %"PRId64"\n", log_mmap_offset);
    DPRINT("Log mmap_size:   %"PRId64"\n", log_mmap_size);

    rc = mmap(0, log_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
              log_mmap_offset);
    close(fd);
    if (rc == MAP_FAILED) {
        perror("log mmap error");
    }

    if (dev->log_table) {
        munmap(dev->log_table, dev->log_size);
    }
    dev->log_table = rc;
    dev->log_size = log_mmap_size;

    vmsg->size = sizeof(vmsg->payload.u64);
    vmsg->fd_num = 0;

    return true;
}

static bool
vu_set_log_fd_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    if (vmsg->fd_num != 1) {
        vu_panic(dev, "Invalid log_fd message");
        return false;
    }

    if (dev->log_call_fd != -1) {
        close(dev->log_call_fd);
    }
    dev->log_call_fd = vmsg->fds[0];
    DPRINT("Got log_call_fd: %d\n", vmsg->fds[0]);

    return false;
}

static bool
vu_set_vring_num_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;
    unsigned int num = vmsg->payload.state.num;

    DPRINT("State.index: %d\n", index);
    DPRINT("State.num:   %d\n", num);
    dev->vq[index].vring.num = num;

    return false;
}

static bool
vu_set_vring_addr_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    struct vhost_vring_addr addr = vmsg->payload.addr, *vra = &addr;
    unsigned int index = vra->index;
    VuVirtq *vq = &dev->vq[index];

    DPRINT("vhost_vring_addr:\n");
    DPRINT("    index:  %d\n", vra->index);
    DPRINT("    flags:  %d\n", vra->flags);
    DPRINT("    desc_user_addr:   0x%016" PRIx64 "\n", vra->desc_user_addr);
    DPRINT("    used_user_addr:   0x%016" PRIx64 "\n", vra->used_user_addr);
    DPRINT("    avail_user_addr:  0x%016" PRIx64 "\n", vra->avail_user_addr);
    DPRINT("    log_guest_addr:   0x%016" PRIx64 "\n", vra->log_guest_addr);

    vq->vra = *vra;
    vq->vring.flags = vra->flags;
    vq->vring.log_guest_addr = vra->log_guest_addr;


    if (map_ring(dev, vq)) {
        vu_panic(dev, "Invalid vring_addr message");
        return false;
    }

    vq->used_idx = vq->vring.used->idx;

    if (vq->last_avail_idx != vq->used_idx) {
        bool resume = dev->iface->queue_is_processed_in_order &&
            dev->iface->queue_is_processed_in_order(dev, index);

        DPRINT("Last avail index != used index: %u != %u%s\n",
               vq->last_avail_idx, vq->used_idx,
               resume ? ", resuming" : "");

        if (resume) {
            vq->shadow_avail_idx = vq->last_avail_idx = vq->used_idx;
        }
    }

    return false;
}

static bool
vu_set_vring_base_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;
    unsigned int num = vmsg->payload.state.num;

    DPRINT("State.index: %d\n", index);
    DPRINT("State.num:   %d\n", num);
    dev->vq[index].shadow_avail_idx = dev->vq[index].last_avail_idx = num;

    return false;
}

static bool
vu_get_vring_base_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;

    DPRINT("State.index: %d\n", index);
    vmsg->payload.state.num = dev->vq[index].last_avail_idx;
    vmsg->size = sizeof(vmsg->payload.state);

    dev->vq[index].started = false;
    if (dev->iface->queue_set_started) {
        dev->iface->queue_set_started(dev, index, false);
    }

    if (dev->vq[index].call_fd != -1) {
        close(dev->vq[index].call_fd);
        dev->vq[index].call_fd = -1;
    }
    if (dev->vq[index].kick_fd != -1) {
        dev->remove_watch(dev, dev->vq[index].kick_fd);
        close(dev->vq[index].kick_fd);
        dev->vq[index].kick_fd = -1;
    }

    return true;
}

static bool
vu_check_queue_msg_file(VuDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
    bool nofd = vmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK;

    if (index >= dev->max_queues) {
        vmsg_close_fds(vmsg);
        vu_panic(dev, "Invalid queue index: %u", index);
        return false;
    }

    if (nofd) {
        vmsg_close_fds(vmsg);
        return true;
    }

    if (vmsg->fd_num != 1) {
        vmsg_close_fds(vmsg);
        vu_panic(dev, "Invalid fds in request: %d", vmsg->request);
        return false;
    }

    return true;
}

static int
inflight_desc_compare(const void *a, const void *b)
{
    VuVirtqInflightDesc *desc0 = (VuVirtqInflightDesc *)a,
                        *desc1 = (VuVirtqInflightDesc *)b;

    if (desc1->counter > desc0->counter &&
        (desc1->counter - desc0->counter) < VIRTQUEUE_MAX_SIZE * 2) {
        return 1;
    }

    return -1;
}

static int
vu_check_queue_inflights(VuDev *dev, VuVirtq *vq)
{
    int i = 0;

    if (!vu_has_protocol_feature(dev, VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD)) {
        return 0;
    }

    if (unlikely(!vq->inflight)) {
        return -1;
    }

    if (unlikely(!vq->inflight->version)) {
        /* initialize the buffer */
        vq->inflight->version = INFLIGHT_VERSION;
        return 0;
    }

    vq->used_idx = vq->vring.used->idx;
    vq->resubmit_num = 0;
    vq->resubmit_list = NULL;
    vq->counter = 0;

    if (unlikely(vq->inflight->used_idx != vq->used_idx)) {
        vq->inflight->desc[vq->inflight->last_batch_head].inflight = 0;

        barrier();

        vq->inflight->used_idx = vq->used_idx;
    }

    for (i = 0; i < vq->inflight->desc_num; i++) {
        if (vq->inflight->desc[i].inflight == 1) {
            vq->inuse++;
        }
    }

    vq->shadow_avail_idx = vq->last_avail_idx = vq->inuse + vq->used_idx;

    if (vq->inuse) {
        vq->resubmit_list = calloc(vq->inuse, sizeof(VuVirtqInflightDesc));
        if (!vq->resubmit_list) {
            return -1;
        }

        for (i = 0; i < vq->inflight->desc_num; i++) {
            if (vq->inflight->desc[i].inflight) {
                vq->resubmit_list[vq->resubmit_num].index = i;
                vq->resubmit_list[vq->resubmit_num].counter =
                                        vq->inflight->desc[i].counter;
                vq->resubmit_num++;
            }
        }

        if (vq->resubmit_num > 1) {
            qsort(vq->resubmit_list, vq->resubmit_num,
                  sizeof(VuVirtqInflightDesc), inflight_desc_compare);
        }
        vq->counter = vq->resubmit_list[0].counter + 1;
    }

    /* in case of I/O hang after reconnecting */
    if (eventfd_write(vq->kick_fd, 1)) {
        return -1;
    }

    return 0;
}

static bool
vu_set_vring_kick_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
    bool nofd = vmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    if (!vu_check_queue_msg_file(dev, vmsg)) {
        return false;
    }

    if (dev->vq[index].kick_fd != -1) {
        dev->remove_watch(dev, dev->vq[index].kick_fd);
        close(dev->vq[index].kick_fd);
        dev->vq[index].kick_fd = -1;
    }

    dev->vq[index].kick_fd = nofd ? -1 : vmsg->fds[0];
    DPRINT("Got kick_fd: %d for vq: %d\n", dev->vq[index].kick_fd, index);

    dev->vq[index].started = true;
    if (dev->iface->queue_set_started) {
        dev->iface->queue_set_started(dev, index, true);
    }

    if (dev->vq[index].kick_fd != -1 && dev->vq[index].handler) {
        dev->set_watch(dev, dev->vq[index].kick_fd, VU_WATCH_IN,
                       vu_kick_cb, (void *)(long)index);

        DPRINT("Waiting for kicks on fd: %d for vq: %d\n",
               dev->vq[index].kick_fd, index);
    }

    if (vu_check_queue_inflights(dev, &dev->vq[index])) {
        vu_panic(dev, "Failed to check inflights for vq: %d\n", index);
    }

    return false;
}

void vu_set_queue_handler(VuDev *dev, VuVirtq *vq,
                          vu_queue_handler_cb handler)
{
    int qidx = vq - dev->vq;

    vq->handler = handler;
    if (vq->kick_fd >= 0) {
        if (handler) {
            dev->set_watch(dev, vq->kick_fd, VU_WATCH_IN,
                           vu_kick_cb, (void *)(long)qidx);
        } else {
            dev->remove_watch(dev, vq->kick_fd);
        }
    }
}

bool vu_set_queue_host_notifier(VuDev *dev, VuVirtq *vq, int fd,
                                int size, int offset)
{
    int qidx = vq - dev->vq;
    int fd_num = 0;
    VhostUserMsg vmsg = {
        .request = VHOST_USER_SLAVE_VRING_HOST_NOTIFIER_MSG,
        .flags = VHOST_USER_VERSION | VHOST_USER_NEED_REPLY_MASK,
        .size = sizeof(vmsg.payload.area),
        .payload.area = {
            .u64 = qidx & VHOST_USER_VRING_IDX_MASK,
            .size = size,
            .offset = offset,
        },
    };

    if (fd == -1) {
        vmsg.payload.area.u64 |= VHOST_USER_VRING_NOFD_MASK;
    } else {
        vmsg.fds[fd_num++] = fd;
    }

    vmsg.fd_num = fd_num;

    if (!vu_has_protocol_feature(dev, VHOST_USER_PROTOCOL_F_SLAVE_SEND_FD)) {
        return false;
    }

    pthread_mutex_lock(&dev->slave_mutex);
    if (!vu_message_write(dev, dev->slave_fd, &vmsg)) {
        pthread_mutex_unlock(&dev->slave_mutex);
        return false;
    }

    /* Also unlocks the slave_mutex */
    return vu_process_message_reply(dev, &vmsg);
}

static bool
vu_set_vring_call_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
    bool nofd = vmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    if (!vu_check_queue_msg_file(dev, vmsg)) {
        return false;
    }

    if (dev->vq[index].call_fd != -1) {
        close(dev->vq[index].call_fd);
        dev->vq[index].call_fd = -1;
    }

    dev->vq[index].call_fd = nofd ? -1 : vmsg->fds[0];

    /* in case of I/O hang after reconnecting */
    if (dev->vq[index].call_fd != -1 && eventfd_write(vmsg->fds[0], 1)) {
        return -1;
    }

    DPRINT("Got call_fd: %d for vq: %d\n", dev->vq[index].call_fd, index);

    return false;
}

static bool
vu_set_vring_err_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;
    bool nofd = vmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    if (!vu_check_queue_msg_file(dev, vmsg)) {
        return false;
    }

    if (dev->vq[index].err_fd != -1) {
        close(dev->vq[index].err_fd);
        dev->vq[index].err_fd = -1;
    }

    dev->vq[index].err_fd = nofd ? -1 : vmsg->fds[0];

    return false;
}

static bool
vu_get_protocol_features_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    /*
     * Note that we support, but intentionally do not set,
     * VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS. This means that
     * a device implementation can return it in its callback
     * (get_protocol_features) if it wants to use this for
     * simulation, but it is otherwise not desirable (if even
     * implemented by the master.)
     */
    uint64_t features = 1ULL << VHOST_USER_PROTOCOL_F_MQ |
                        1ULL << VHOST_USER_PROTOCOL_F_LOG_SHMFD |
                        1ULL << VHOST_USER_PROTOCOL_F_SLAVE_REQ |
                        1ULL << VHOST_USER_PROTOCOL_F_HOST_NOTIFIER |
                        1ULL << VHOST_USER_PROTOCOL_F_SLAVE_SEND_FD |
                        1ULL << VHOST_USER_PROTOCOL_F_REPLY_ACK |
                        1ULL << VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS;

    if (have_userfault()) {
        features |= 1ULL << VHOST_USER_PROTOCOL_F_PAGEFAULT;
    }

    if (dev->iface->get_config && dev->iface->set_config) {
        features |= 1ULL << VHOST_USER_PROTOCOL_F_CONFIG;
    }

    if (dev->iface->get_protocol_features) {
        features |= dev->iface->get_protocol_features(dev);
    }

    vmsg_set_reply_u64(vmsg, features);
    return true;
}

static bool
vu_set_protocol_features_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    uint64_t features = vmsg->payload.u64;

    DPRINT("u64: 0x%016"PRIx64"\n", features);

    dev->protocol_features = vmsg->payload.u64;

    if (vu_has_protocol_feature(dev,
                                VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS) &&
        (!vu_has_protocol_feature(dev, VHOST_USER_PROTOCOL_F_SLAVE_REQ) ||
         !vu_has_protocol_feature(dev, VHOST_USER_PROTOCOL_F_REPLY_ACK))) {
        /*
         * The use case for using messages for kick/call is simulation, to make
         * the kick and call synchronous. To actually get that behaviour, both
         * of the other features are required.
         * Theoretically, one could use only kick messages, or do them without
         * having F_REPLY_ACK, but too many (possibly pending) messages on the
         * socket will eventually cause the master to hang, to avoid this in
         * scenarios where not desired enforce that the settings are in a way
         * that actually enables the simulation case.
         */
        vu_panic(dev,
                 "F_IN_BAND_NOTIFICATIONS requires F_SLAVE_REQ && F_REPLY_ACK");
        return false;
    }

    if (dev->iface->set_protocol_features) {
        dev->iface->set_protocol_features(dev, features);
    }

    return false;
}

static bool
vu_get_queue_num_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    vmsg_set_reply_u64(vmsg, dev->max_queues);
    return true;
}

static bool
vu_set_vring_enable_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;
    unsigned int enable = vmsg->payload.state.num;

    DPRINT("State.index: %d\n", index);
    DPRINT("State.enable:   %d\n", enable);

    if (index >= dev->max_queues) {
        vu_panic(dev, "Invalid vring_enable index: %u", index);
        return false;
    }

    dev->vq[index].enable = enable;
    return false;
}

static bool
vu_set_slave_req_fd(VuDev *dev, VhostUserMsg *vmsg)
{
    if (vmsg->fd_num != 1) {
        vu_panic(dev, "Invalid slave_req_fd message (%d fd's)", vmsg->fd_num);
        return false;
    }

    if (dev->slave_fd != -1) {
        close(dev->slave_fd);
    }
    dev->slave_fd = vmsg->fds[0];
    DPRINT("Got slave_fd: %d\n", vmsg->fds[0]);

    return false;
}

static bool
vu_get_config(VuDev *dev, VhostUserMsg *vmsg)
{
    int ret = -1;

    if (dev->iface->get_config) {
        ret = dev->iface->get_config(dev, vmsg->payload.config.region,
                                     vmsg->payload.config.size);
    }

    if (ret) {
        /* resize to zero to indicate an error to master */
        vmsg->size = 0;
    }

    return true;
}

static bool
vu_set_config(VuDev *dev, VhostUserMsg *vmsg)
{
    int ret = -1;

    if (dev->iface->set_config) {
        ret = dev->iface->set_config(dev, vmsg->payload.config.region,
                                     vmsg->payload.config.offset,
                                     vmsg->payload.config.size,
                                     vmsg->payload.config.flags);
        if (ret) {
            vu_panic(dev, "Set virtio configuration space failed");
        }
    }

    return false;
}

static bool
vu_set_postcopy_advise(VuDev *dev, VhostUserMsg *vmsg)
{
    dev->postcopy_ufd = -1;
#ifdef UFFDIO_API
    struct uffdio_api api_struct;

    dev->postcopy_ufd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    vmsg->size = 0;
#endif

    if (dev->postcopy_ufd == -1) {
        vu_panic(dev, "Userfaultfd not available: %s", strerror(errno));
        goto out;
    }

#ifdef UFFDIO_API
    api_struct.api = UFFD_API;
    api_struct.features = 0;
    if (ioctl(dev->postcopy_ufd, UFFDIO_API, &api_struct)) {
        vu_panic(dev, "Failed UFFDIO_API: %s", strerror(errno));
        close(dev->postcopy_ufd);
        dev->postcopy_ufd = -1;
        goto out;
    }
    /* TODO: Stash feature flags somewhere */
#endif

out:
    /* Return a ufd to the QEMU */
    vmsg->fd_num = 1;
    vmsg->fds[0] = dev->postcopy_ufd;
    return true; /* = send a reply */
}

static bool
vu_set_postcopy_listen(VuDev *dev, VhostUserMsg *vmsg)
{
    if (dev->nregions) {
        vu_panic(dev, "Regions already registered at postcopy-listen");
        vmsg_set_reply_u64(vmsg, -1);
        return true;
    }
    dev->postcopy_listening = true;

    vmsg_set_reply_u64(vmsg, 0);
    return true;
}

static bool
vu_set_postcopy_end(VuDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("%s: Entry\n", __func__);
    dev->postcopy_listening = false;
    if (dev->postcopy_ufd > 0) {
        close(dev->postcopy_ufd);
        dev->postcopy_ufd = -1;
        DPRINT("%s: Done close\n", __func__);
    }

    vmsg_set_reply_u64(vmsg, 0);
    DPRINT("%s: exit\n", __func__);
    return true;
}

static inline uint64_t
vu_inflight_queue_size(uint16_t queue_size)
{
    return ALIGN_UP(sizeof(VuDescStateSplit) * queue_size +
           sizeof(uint16_t), INFLIGHT_ALIGNMENT);
}

static bool
vu_get_inflight_fd(VuDev *dev, VhostUserMsg *vmsg)
{
    int fd;
    void *addr;
    uint64_t mmap_size;
    uint16_t num_queues, queue_size;

    if (vmsg->size != sizeof(vmsg->payload.inflight)) {
        vu_panic(dev, "Invalid get_inflight_fd message:%d", vmsg->size);
        vmsg->payload.inflight.mmap_size = 0;
        return true;
    }

    num_queues = vmsg->payload.inflight.num_queues;
    queue_size = vmsg->payload.inflight.queue_size;

    DPRINT("set_inflight_fd num_queues: %"PRId16"\n", num_queues);
    DPRINT("set_inflight_fd queue_size: %"PRId16"\n", queue_size);

    mmap_size = vu_inflight_queue_size(queue_size) * num_queues;

    addr = qemu_memfd_alloc("vhost-inflight", mmap_size,
                            F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL,
                            &fd, NULL);

    if (!addr) {
        vu_panic(dev, "Failed to alloc vhost inflight area");
        vmsg->payload.inflight.mmap_size = 0;
        return true;
    }

    memset(addr, 0, mmap_size);

    dev->inflight_info.addr = addr;
    dev->inflight_info.size = vmsg->payload.inflight.mmap_size = mmap_size;
    dev->inflight_info.fd = vmsg->fds[0] = fd;
    vmsg->fd_num = 1;
    vmsg->payload.inflight.mmap_offset = 0;

    DPRINT("send inflight mmap_size: %"PRId64"\n",
           vmsg->payload.inflight.mmap_size);
    DPRINT("send inflight mmap offset: %"PRId64"\n",
           vmsg->payload.inflight.mmap_offset);

    return true;
}

static bool
vu_set_inflight_fd(VuDev *dev, VhostUserMsg *vmsg)
{
    int fd, i;
    uint64_t mmap_size, mmap_offset;
    uint16_t num_queues, queue_size;
    void *rc;

    if (vmsg->fd_num != 1 ||
        vmsg->size != sizeof(vmsg->payload.inflight)) {
        vu_panic(dev, "Invalid set_inflight_fd message size:%d fds:%d",
                 vmsg->size, vmsg->fd_num);
        return false;
    }

    fd = vmsg->fds[0];
    mmap_size = vmsg->payload.inflight.mmap_size;
    mmap_offset = vmsg->payload.inflight.mmap_offset;
    num_queues = vmsg->payload.inflight.num_queues;
    queue_size = vmsg->payload.inflight.queue_size;

    DPRINT("set_inflight_fd mmap_size: %"PRId64"\n", mmap_size);
    DPRINT("set_inflight_fd mmap_offset: %"PRId64"\n", mmap_offset);
    DPRINT("set_inflight_fd num_queues: %"PRId16"\n", num_queues);
    DPRINT("set_inflight_fd queue_size: %"PRId16"\n", queue_size);

    rc = mmap(0, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
              fd, mmap_offset);

    if (rc == MAP_FAILED) {
        vu_panic(dev, "set_inflight_fd mmap error: %s", strerror(errno));
        return false;
    }

    if (dev->inflight_info.fd) {
        close(dev->inflight_info.fd);
    }

    if (dev->inflight_info.addr) {
        munmap(dev->inflight_info.addr, dev->inflight_info.size);
    }

    dev->inflight_info.fd = fd;
    dev->inflight_info.addr = rc;
    dev->inflight_info.size = mmap_size;

    for (i = 0; i < num_queues; i++) {
        dev->vq[i].inflight = (VuVirtqInflight *)rc;
        dev->vq[i].inflight->desc_num = queue_size;
        rc = (void *)((char *)rc + vu_inflight_queue_size(queue_size));
    }

    return false;
}

static bool
vu_handle_vring_kick(VuDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;

    if (index >= dev->max_queues) {
        vu_panic(dev, "Invalid queue index: %u", index);
        return false;
    }

    DPRINT("Got kick message: handler:%p idx:%d\n",
           dev->vq[index].handler, index);

    if (!dev->vq[index].started) {
        dev->vq[index].started = true;

        if (dev->iface->queue_set_started) {
            dev->iface->queue_set_started(dev, index, true);
        }
    }

    if (dev->vq[index].handler) {
        dev->vq[index].handler(dev, index);
    }

    return false;
}

static bool vu_handle_get_max_memslots(VuDev *dev, VhostUserMsg *vmsg)
{
    vmsg->flags = VHOST_USER_REPLY_MASK | VHOST_USER_VERSION;
    vmsg->size  = sizeof(vmsg->payload.u64);
    vmsg->payload.u64 = VHOST_USER_MAX_RAM_SLOTS;
    vmsg->fd_num = 0;

    if (!vu_message_write(dev, dev->sock, vmsg)) {
        vu_panic(dev, "Failed to send max ram slots: %s\n", strerror(errno));
    }

    DPRINT("u64: 0x%016"PRIx64"\n", (uint64_t) VHOST_USER_MAX_RAM_SLOTS);

    return false;
}

static bool
vu_process_message(VuDev *dev, VhostUserMsg *vmsg)
{
    int do_reply = 0;

    /* Print out generic part of the request. */
    DPRINT("================ Vhost user message ================\n");
    DPRINT("Request: %s (%d)\n", vu_request_to_string(vmsg->request),
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

    if (dev->iface->process_msg &&
        dev->iface->process_msg(dev, vmsg, &do_reply)) {
        return do_reply;
    }

    switch (vmsg->request) {
    case VHOST_USER_GET_FEATURES:
        return vu_get_features_exec(dev, vmsg);
    case VHOST_USER_SET_FEATURES:
        return vu_set_features_exec(dev, vmsg);
    case VHOST_USER_GET_PROTOCOL_FEATURES:
        return vu_get_protocol_features_exec(dev, vmsg);
    case VHOST_USER_SET_PROTOCOL_FEATURES:
        return vu_set_protocol_features_exec(dev, vmsg);
    case VHOST_USER_SET_OWNER:
        return vu_set_owner_exec(dev, vmsg);
    case VHOST_USER_RESET_OWNER:
        return vu_reset_device_exec(dev, vmsg);
    case VHOST_USER_SET_MEM_TABLE:
        return vu_set_mem_table_exec(dev, vmsg);
    case VHOST_USER_SET_LOG_BASE:
        return vu_set_log_base_exec(dev, vmsg);
    case VHOST_USER_SET_LOG_FD:
        return vu_set_log_fd_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_NUM:
        return vu_set_vring_num_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_ADDR:
        return vu_set_vring_addr_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_BASE:
        return vu_set_vring_base_exec(dev, vmsg);
    case VHOST_USER_GET_VRING_BASE:
        return vu_get_vring_base_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_KICK:
        return vu_set_vring_kick_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_CALL:
        return vu_set_vring_call_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_ERR:
        return vu_set_vring_err_exec(dev, vmsg);
    case VHOST_USER_GET_QUEUE_NUM:
        return vu_get_queue_num_exec(dev, vmsg);
    case VHOST_USER_SET_VRING_ENABLE:
        return vu_set_vring_enable_exec(dev, vmsg);
    case VHOST_USER_SET_SLAVE_REQ_FD:
        return vu_set_slave_req_fd(dev, vmsg);
    case VHOST_USER_GET_CONFIG:
        return vu_get_config(dev, vmsg);
    case VHOST_USER_SET_CONFIG:
        return vu_set_config(dev, vmsg);
    case VHOST_USER_NONE:
        /* if you need processing before exit, override iface->process_msg */
        exit(0);
    case VHOST_USER_POSTCOPY_ADVISE:
        return vu_set_postcopy_advise(dev, vmsg);
    case VHOST_USER_POSTCOPY_LISTEN:
        return vu_set_postcopy_listen(dev, vmsg);
    case VHOST_USER_POSTCOPY_END:
        return vu_set_postcopy_end(dev, vmsg);
    case VHOST_USER_GET_INFLIGHT_FD:
        return vu_get_inflight_fd(dev, vmsg);
    case VHOST_USER_SET_INFLIGHT_FD:
        return vu_set_inflight_fd(dev, vmsg);
    case VHOST_USER_VRING_KICK:
        return vu_handle_vring_kick(dev, vmsg);
    case VHOST_USER_GET_MAX_MEM_SLOTS:
        return vu_handle_get_max_memslots(dev, vmsg);
    case VHOST_USER_ADD_MEM_REG:
        return vu_add_mem_reg(dev, vmsg);
    case VHOST_USER_REM_MEM_REG:
        return vu_rem_mem_reg(dev, vmsg);
    default:
        vmsg_close_fds(vmsg);
        vu_panic(dev, "Unhandled request: %d", vmsg->request);
    }

    return false;
}

bool
vu_dispatch(VuDev *dev)
{
    VhostUserMsg vmsg = { 0, };
    int reply_requested;
    bool need_reply, success = false;

    if (!vu_message_read(dev, dev->sock, &vmsg)) {
        goto end;
    }

    need_reply = vmsg.flags & VHOST_USER_NEED_REPLY_MASK;

    reply_requested = vu_process_message(dev, &vmsg);
    if (!reply_requested && need_reply) {
        vmsg_set_reply_u64(&vmsg, 0);
        reply_requested = 1;
    }

    if (!reply_requested) {
        success = true;
        goto end;
    }

    if (!vu_send_reply(dev, dev->sock, &vmsg)) {
        goto end;
    }

    success = true;

end:
    free(vmsg.data);
    return success;
}

void
vu_deinit(VuDev *dev)
{
    int i;

    for (i = 0; i < dev->nregions; i++) {
        VuDevRegion *r = &dev->regions[i];
        void *m = (void *) (uintptr_t) r->mmap_addr;
        if (m != MAP_FAILED) {
            munmap(m, r->size + r->mmap_offset);
        }
    }
    dev->nregions = 0;

    for (i = 0; i < dev->max_queues; i++) {
        VuVirtq *vq = &dev->vq[i];

        if (vq->call_fd != -1) {
            close(vq->call_fd);
            vq->call_fd = -1;
        }

        if (vq->kick_fd != -1) {
            close(vq->kick_fd);
            vq->kick_fd = -1;
        }

        if (vq->err_fd != -1) {
            close(vq->err_fd);
            vq->err_fd = -1;
        }

        if (vq->resubmit_list) {
            free(vq->resubmit_list);
            vq->resubmit_list = NULL;
        }

        vq->inflight = NULL;
    }

    if (dev->inflight_info.addr) {
        munmap(dev->inflight_info.addr, dev->inflight_info.size);
        dev->inflight_info.addr = NULL;
    }

    if (dev->inflight_info.fd > 0) {
        close(dev->inflight_info.fd);
        dev->inflight_info.fd = -1;
    }

    vu_close_log(dev);
    if (dev->slave_fd != -1) {
        close(dev->slave_fd);
        dev->slave_fd = -1;
    }
    pthread_mutex_destroy(&dev->slave_mutex);

    if (dev->sock != -1) {
        close(dev->sock);
    }

    free(dev->vq);
    dev->vq = NULL;
}

bool
vu_init(VuDev *dev,
        uint16_t max_queues,
        int socket,
        vu_panic_cb panic,
        vu_set_watch_cb set_watch,
        vu_remove_watch_cb remove_watch,
        const VuDevIface *iface)
{
    uint16_t i;

    assert(max_queues > 0);
    assert(socket >= 0);
    assert(set_watch);
    assert(remove_watch);
    assert(iface);
    assert(panic);

    memset(dev, 0, sizeof(*dev));

    dev->sock = socket;
    dev->panic = panic;
    dev->set_watch = set_watch;
    dev->remove_watch = remove_watch;
    dev->iface = iface;
    dev->log_call_fd = -1;
    pthread_mutex_init(&dev->slave_mutex, NULL);
    dev->slave_fd = -1;
    dev->max_queues = max_queues;

    dev->vq = malloc(max_queues * sizeof(dev->vq[0]));
    if (!dev->vq) {
        DPRINT("%s: failed to malloc virtqueues\n", __func__);
        return false;
    }

    for (i = 0; i < max_queues; i++) {
        dev->vq[i] = (VuVirtq) {
            .call_fd = -1, .kick_fd = -1, .err_fd = -1,
            .notification = true,
        };
    }

    return true;
}

VuVirtq *
vu_get_queue(VuDev *dev, int qidx)
{
    assert(qidx < dev->max_queues);
    return &dev->vq[qidx];
}

bool
vu_queue_enabled(VuDev *dev, VuVirtq *vq)
{
    return vq->enable;
}

bool
vu_queue_started(const VuDev *dev, const VuVirtq *vq)
{
    return vq->started;
}

static inline uint16_t
vring_avail_flags(VuVirtq *vq)
{
    return vq->vring.avail->flags;
}

static inline uint16_t
vring_avail_idx(VuVirtq *vq)
{
    vq->shadow_avail_idx = vq->vring.avail->idx;

    return vq->shadow_avail_idx;
}

static inline uint16_t
vring_avail_ring(VuVirtq *vq, int i)
{
    return vq->vring.avail->ring[i];
}

static inline uint16_t
vring_get_used_event(VuVirtq *vq)
{
    return vring_avail_ring(vq, vq->vring.num);
}

static int
virtqueue_num_heads(VuDev *dev, VuVirtq *vq, unsigned int idx)
{
    uint16_t num_heads = vring_avail_idx(vq) - idx;

    /* Check it isn't doing very strange things with descriptor numbers. */
    if (num_heads > vq->vring.num) {
        vu_panic(dev, "Guest moved used index from %u to %u",
                 idx, vq->shadow_avail_idx);
        return -1;
    }
    if (num_heads) {
        /* On success, callers read a descriptor at vq->last_avail_idx.
         * Make sure descriptor read does not bypass avail index read. */
        smp_rmb();
    }

    return num_heads;
}

static bool
virtqueue_get_head(VuDev *dev, VuVirtq *vq,
                   unsigned int idx, unsigned int *head)
{
    /* Grab the next descriptor number they're advertising, and increment
     * the index we've seen. */
    *head = vring_avail_ring(vq, idx % vq->vring.num);

    /* If their number is silly, that's a fatal mistake. */
    if (*head >= vq->vring.num) {
        vu_panic(dev, "Guest says index %u is available", *head);
        return false;
    }

    return true;
}

static int
virtqueue_read_indirect_desc(VuDev *dev, struct vring_desc *desc,
                             uint64_t addr, size_t len)
{
    struct vring_desc *ori_desc;
    uint64_t read_len;

    if (len > (VIRTQUEUE_MAX_SIZE * sizeof(struct vring_desc))) {
        return -1;
    }

    if (len == 0) {
        return -1;
    }

    while (len) {
        read_len = len;
        ori_desc = vu_gpa_to_va(dev, &read_len, addr);
        if (!ori_desc) {
            return -1;
        }

        memcpy(desc, ori_desc, read_len);
        len -= read_len;
        addr += read_len;
        desc += read_len;
    }

    return 0;
}

enum {
    VIRTQUEUE_READ_DESC_ERROR = -1,
    VIRTQUEUE_READ_DESC_DONE = 0,   /* end of chain */
    VIRTQUEUE_READ_DESC_MORE = 1,   /* more buffers in chain */
};

static int
virtqueue_read_next_desc(VuDev *dev, struct vring_desc *desc,
                         int i, unsigned int max, unsigned int *next)
{
    /* If this descriptor says it doesn't chain, we're done. */
    if (!(desc[i].flags & VRING_DESC_F_NEXT)) {
        return VIRTQUEUE_READ_DESC_DONE;
    }

    /* Check they're not leading us off end of descriptors. */
    *next = desc[i].next;
    /* Make sure compiler knows to grab that: we don't want it changing! */
    smp_wmb();

    if (*next >= max) {
        vu_panic(dev, "Desc next is %u", *next);
        return VIRTQUEUE_READ_DESC_ERROR;
    }

    return VIRTQUEUE_READ_DESC_MORE;
}

void
vu_queue_get_avail_bytes(VuDev *dev, VuVirtq *vq, unsigned int *in_bytes,
                         unsigned int *out_bytes,
                         unsigned max_in_bytes, unsigned max_out_bytes)
{
    unsigned int idx;
    unsigned int total_bufs, in_total, out_total;
    int rc;

    idx = vq->last_avail_idx;

    total_bufs = in_total = out_total = 0;
    if (unlikely(dev->broken) ||
        unlikely(!vq->vring.avail)) {
        goto done;
    }

    while ((rc = virtqueue_num_heads(dev, vq, idx)) > 0) {
        unsigned int max, desc_len, num_bufs, indirect = 0;
        uint64_t desc_addr, read_len;
        struct vring_desc *desc;
        struct vring_desc desc_buf[VIRTQUEUE_MAX_SIZE];
        unsigned int i;

        max = vq->vring.num;
        num_bufs = total_bufs;
        if (!virtqueue_get_head(dev, vq, idx++, &i)) {
            goto err;
        }
        desc = vq->vring.desc;

        if (desc[i].flags & VRING_DESC_F_INDIRECT) {
            if (desc[i].len % sizeof(struct vring_desc)) {
                vu_panic(dev, "Invalid size for indirect buffer table");
                goto err;
            }

            /* If we've got too many, that implies a descriptor loop. */
            if (num_bufs >= max) {
                vu_panic(dev, "Looped descriptor");
                goto err;
            }

            /* loop over the indirect descriptor table */
            indirect = 1;
            desc_addr = desc[i].addr;
            desc_len = desc[i].len;
            max = desc_len / sizeof(struct vring_desc);
            read_len = desc_len;
            desc = vu_gpa_to_va(dev, &read_len, desc_addr);
            if (unlikely(desc && read_len != desc_len)) {
                /* Failed to use zero copy */
                desc = NULL;
                if (!virtqueue_read_indirect_desc(dev, desc_buf,
                                                  desc_addr,
                                                  desc_len)) {
                    desc = desc_buf;
                }
            }
            if (!desc) {
                vu_panic(dev, "Invalid indirect buffer table");
                goto err;
            }
            num_bufs = i = 0;
        }

        do {
            /* If we've got too many, that implies a descriptor loop. */
            if (++num_bufs > max) {
                vu_panic(dev, "Looped descriptor");
                goto err;
            }

            if (desc[i].flags & VRING_DESC_F_WRITE) {
                in_total += desc[i].len;
            } else {
                out_total += desc[i].len;
            }
            if (in_total >= max_in_bytes && out_total >= max_out_bytes) {
                goto done;
            }
            rc = virtqueue_read_next_desc(dev, desc, i, max, &i);
        } while (rc == VIRTQUEUE_READ_DESC_MORE);

        if (rc == VIRTQUEUE_READ_DESC_ERROR) {
            goto err;
        }

        if (!indirect) {
            total_bufs = num_bufs;
        } else {
            total_bufs++;
        }
    }
    if (rc < 0) {
        goto err;
    }
done:
    if (in_bytes) {
        *in_bytes = in_total;
    }
    if (out_bytes) {
        *out_bytes = out_total;
    }
    return;

err:
    in_total = out_total = 0;
    goto done;
}

bool
vu_queue_avail_bytes(VuDev *dev, VuVirtq *vq, unsigned int in_bytes,
                     unsigned int out_bytes)
{
    unsigned int in_total, out_total;

    vu_queue_get_avail_bytes(dev, vq, &in_total, &out_total,
                             in_bytes, out_bytes);

    return in_bytes <= in_total && out_bytes <= out_total;
}

/* Fetch avail_idx from VQ memory only when we really need to know if
 * guest has added some buffers. */
bool
vu_queue_empty(VuDev *dev, VuVirtq *vq)
{
    if (unlikely(dev->broken) ||
        unlikely(!vq->vring.avail)) {
        return true;
    }

    if (vq->shadow_avail_idx != vq->last_avail_idx) {
        return false;
    }

    return vring_avail_idx(vq) == vq->last_avail_idx;
}

static bool
vring_notify(VuDev *dev, VuVirtq *vq)
{
    uint16_t old, new;
    bool v;

    /* We need to expose used array entries before checking used event. */
    smp_mb();

    /* Always notify when queue is empty (when feature acknowledge) */
    if (vu_has_feature(dev, VIRTIO_F_NOTIFY_ON_EMPTY) &&
        !vq->inuse && vu_queue_empty(dev, vq)) {
        return true;
    }

    if (!vu_has_feature(dev, VIRTIO_RING_F_EVENT_IDX)) {
        return !(vring_avail_flags(vq) & VRING_AVAIL_F_NO_INTERRUPT);
    }

    v = vq->signalled_used_valid;
    vq->signalled_used_valid = true;
    old = vq->signalled_used;
    new = vq->signalled_used = vq->used_idx;
    return !v || vring_need_event(vring_get_used_event(vq), new, old);
}

static void _vu_queue_notify(VuDev *dev, VuVirtq *vq, bool sync)
{
    if (unlikely(dev->broken) ||
        unlikely(!vq->vring.avail)) {
        return;
    }

    if (!vring_notify(dev, vq)) {
        DPRINT("skipped notify...\n");
        return;
    }

    if (vq->call_fd < 0 &&
        vu_has_protocol_feature(dev,
                                VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS) &&
        vu_has_protocol_feature(dev, VHOST_USER_PROTOCOL_F_SLAVE_REQ)) {
        VhostUserMsg vmsg = {
            .request = VHOST_USER_SLAVE_VRING_CALL,
            .flags = VHOST_USER_VERSION,
            .size = sizeof(vmsg.payload.state),
            .payload.state = {
                .index = vq - dev->vq,
            },
        };
        bool ack = sync &&
                   vu_has_protocol_feature(dev,
                                           VHOST_USER_PROTOCOL_F_REPLY_ACK);

        if (ack) {
            vmsg.flags |= VHOST_USER_NEED_REPLY_MASK;
        }

        vu_message_write(dev, dev->slave_fd, &vmsg);
        if (ack) {
            vu_message_read(dev, dev->slave_fd, &vmsg);
        }
        return;
    }

    if (eventfd_write(vq->call_fd, 1) < 0) {
        vu_panic(dev, "Error writing eventfd: %s", strerror(errno));
    }
}

void vu_queue_notify(VuDev *dev, VuVirtq *vq)
{
    _vu_queue_notify(dev, vq, false);
}

void vu_queue_notify_sync(VuDev *dev, VuVirtq *vq)
{
    _vu_queue_notify(dev, vq, true);
}

static inline void
vring_used_flags_set_bit(VuVirtq *vq, int mask)
{
    uint16_t *flags;

    flags = (uint16_t *)((char*)vq->vring.used +
                         offsetof(struct vring_used, flags));
    *flags |= mask;
}

static inline void
vring_used_flags_unset_bit(VuVirtq *vq, int mask)
{
    uint16_t *flags;

    flags = (uint16_t *)((char*)vq->vring.used +
                         offsetof(struct vring_used, flags));
    *flags &= ~mask;
}

static inline void
vring_set_avail_event(VuVirtq *vq, uint16_t val)
{
    if (!vq->notification) {
        return;
    }

    *((uint16_t *) &vq->vring.used->ring[vq->vring.num]) = val;
}

void
vu_queue_set_notification(VuDev *dev, VuVirtq *vq, int enable)
{
    vq->notification = enable;
    if (vu_has_feature(dev, VIRTIO_RING_F_EVENT_IDX)) {
        vring_set_avail_event(vq, vring_avail_idx(vq));
    } else if (enable) {
        vring_used_flags_unset_bit(vq, VRING_USED_F_NO_NOTIFY);
    } else {
        vring_used_flags_set_bit(vq, VRING_USED_F_NO_NOTIFY);
    }
    if (enable) {
        /* Expose avail event/used flags before caller checks the avail idx. */
        smp_mb();
    }
}

static void
virtqueue_map_desc(VuDev *dev,
                   unsigned int *p_num_sg, struct iovec *iov,
                   unsigned int max_num_sg, bool is_write,
                   uint64_t pa, size_t sz)
{
    unsigned num_sg = *p_num_sg;

    assert(num_sg <= max_num_sg);

    if (!sz) {
        vu_panic(dev, "virtio: zero sized buffers are not allowed");
        return;
    }

    while (sz) {
        uint64_t len = sz;

        if (num_sg == max_num_sg) {
            vu_panic(dev, "virtio: too many descriptors in indirect table");
            return;
        }

        iov[num_sg].iov_base = vu_gpa_to_va(dev, &len, pa);
        if (iov[num_sg].iov_base == NULL) {
            vu_panic(dev, "virtio: invalid address for buffers");
            return;
        }
        iov[num_sg].iov_len = len;
        num_sg++;
        sz -= len;
        pa += len;
    }

    *p_num_sg = num_sg;
}

static void *
virtqueue_alloc_element(size_t sz,
                                     unsigned out_num, unsigned in_num)
{
    VuVirtqElement *elem;
    size_t in_sg_ofs = ALIGN_UP(sz, __alignof__(elem->in_sg[0]));
    size_t out_sg_ofs = in_sg_ofs + in_num * sizeof(elem->in_sg[0]);
    size_t out_sg_end = out_sg_ofs + out_num * sizeof(elem->out_sg[0]);

    assert(sz >= sizeof(VuVirtqElement));
    elem = malloc(out_sg_end);
    elem->out_num = out_num;
    elem->in_num = in_num;
    elem->in_sg = (void *)elem + in_sg_ofs;
    elem->out_sg = (void *)elem + out_sg_ofs;
    return elem;
}

static void *
vu_queue_map_desc(VuDev *dev, VuVirtq *vq, unsigned int idx, size_t sz)
{
    struct vring_desc *desc = vq->vring.desc;
    uint64_t desc_addr, read_len;
    unsigned int desc_len;
    unsigned int max = vq->vring.num;
    unsigned int i = idx;
    VuVirtqElement *elem;
    unsigned int out_num = 0, in_num = 0;
    struct iovec iov[VIRTQUEUE_MAX_SIZE];
    struct vring_desc desc_buf[VIRTQUEUE_MAX_SIZE];
    int rc;

    if (desc[i].flags & VRING_DESC_F_INDIRECT) {
        if (desc[i].len % sizeof(struct vring_desc)) {
            vu_panic(dev, "Invalid size for indirect buffer table");
        }

        /* loop over the indirect descriptor table */
        desc_addr = desc[i].addr;
        desc_len = desc[i].len;
        max = desc_len / sizeof(struct vring_desc);
        read_len = desc_len;
        desc = vu_gpa_to_va(dev, &read_len, desc_addr);
        if (unlikely(desc && read_len != desc_len)) {
            /* Failed to use zero copy */
            desc = NULL;
            if (!virtqueue_read_indirect_desc(dev, desc_buf,
                                              desc_addr,
                                              desc_len)) {
                desc = desc_buf;
            }
        }
        if (!desc) {
            vu_panic(dev, "Invalid indirect buffer table");
            return NULL;
        }
        i = 0;
    }

    /* Collect all the descriptors */
    do {
        if (desc[i].flags & VRING_DESC_F_WRITE) {
            virtqueue_map_desc(dev, &in_num, iov + out_num,
                               VIRTQUEUE_MAX_SIZE - out_num, true,
                               desc[i].addr, desc[i].len);
        } else {
            if (in_num) {
                vu_panic(dev, "Incorrect order for descriptors");
                return NULL;
            }
            virtqueue_map_desc(dev, &out_num, iov,
                               VIRTQUEUE_MAX_SIZE, false,
                               desc[i].addr, desc[i].len);
        }

        /* If we've got too many, that implies a descriptor loop. */
        if ((in_num + out_num) > max) {
            vu_panic(dev, "Looped descriptor");
        }
        rc = virtqueue_read_next_desc(dev, desc, i, max, &i);
    } while (rc == VIRTQUEUE_READ_DESC_MORE);

    if (rc == VIRTQUEUE_READ_DESC_ERROR) {
        vu_panic(dev, "read descriptor error");
        return NULL;
    }

    /* Now copy what we have collected and mapped */
    elem = virtqueue_alloc_element(sz, out_num, in_num);
    elem->index = idx;
    for (i = 0; i < out_num; i++) {
        elem->out_sg[i] = iov[i];
    }
    for (i = 0; i < in_num; i++) {
        elem->in_sg[i] = iov[out_num + i];
    }

    return elem;
}

static int
vu_queue_inflight_get(VuDev *dev, VuVirtq *vq, int desc_idx)
{
    if (!vu_has_protocol_feature(dev, VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD)) {
        return 0;
    }

    if (unlikely(!vq->inflight)) {
        return -1;
    }

    vq->inflight->desc[desc_idx].counter = vq->counter++;
    vq->inflight->desc[desc_idx].inflight = 1;

    return 0;
}

static int
vu_queue_inflight_pre_put(VuDev *dev, VuVirtq *vq, int desc_idx)
{
    if (!vu_has_protocol_feature(dev, VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD)) {
        return 0;
    }

    if (unlikely(!vq->inflight)) {
        return -1;
    }

    vq->inflight->last_batch_head = desc_idx;

    return 0;
}

static int
vu_queue_inflight_post_put(VuDev *dev, VuVirtq *vq, int desc_idx)
{
    if (!vu_has_protocol_feature(dev, VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD)) {
        return 0;
    }

    if (unlikely(!vq->inflight)) {
        return -1;
    }

    barrier();

    vq->inflight->desc[desc_idx].inflight = 0;

    barrier();

    vq->inflight->used_idx = vq->used_idx;

    return 0;
}

void *
vu_queue_pop(VuDev *dev, VuVirtq *vq, size_t sz)
{
    int i;
    unsigned int head;
    VuVirtqElement *elem;

    if (unlikely(dev->broken) ||
        unlikely(!vq->vring.avail)) {
        return NULL;
    }

    if (unlikely(vq->resubmit_list && vq->resubmit_num > 0)) {
        i = (--vq->resubmit_num);
        elem = vu_queue_map_desc(dev, vq, vq->resubmit_list[i].index, sz);

        if (!vq->resubmit_num) {
            free(vq->resubmit_list);
            vq->resubmit_list = NULL;
        }

        return elem;
    }

    if (vu_queue_empty(dev, vq)) {
        return NULL;
    }
    /*
     * Needed after virtio_queue_empty(), see comment in
     * virtqueue_num_heads().
     */
    smp_rmb();

    if (vq->inuse >= vq->vring.num) {
        vu_panic(dev, "Virtqueue size exceeded");
        return NULL;
    }

    if (!virtqueue_get_head(dev, vq, vq->last_avail_idx++, &head)) {
        return NULL;
    }

    if (vu_has_feature(dev, VIRTIO_RING_F_EVENT_IDX)) {
        vring_set_avail_event(vq, vq->last_avail_idx);
    }

    elem = vu_queue_map_desc(dev, vq, head, sz);

    if (!elem) {
        return NULL;
    }

    vq->inuse++;

    vu_queue_inflight_get(dev, vq, head);

    return elem;
}

static void
vu_queue_detach_element(VuDev *dev, VuVirtq *vq, VuVirtqElement *elem,
                        size_t len)
{
    vq->inuse--;
    /* unmap, when DMA support is added */
}

void
vu_queue_unpop(VuDev *dev, VuVirtq *vq, VuVirtqElement *elem,
               size_t len)
{
    vq->last_avail_idx--;
    vu_queue_detach_element(dev, vq, elem, len);
}

bool
vu_queue_rewind(VuDev *dev, VuVirtq *vq, unsigned int num)
{
    if (num > vq->inuse) {
        return false;
    }
    vq->last_avail_idx -= num;
    vq->inuse -= num;
    return true;
}

static inline
void vring_used_write(VuDev *dev, VuVirtq *vq,
                      struct vring_used_elem *uelem, int i)
{
    struct vring_used *used = vq->vring.used;

    used->ring[i] = *uelem;
    vu_log_write(dev, vq->vring.log_guest_addr +
                 offsetof(struct vring_used, ring[i]),
                 sizeof(used->ring[i]));
}


static void
vu_log_queue_fill(VuDev *dev, VuVirtq *vq,
                  const VuVirtqElement *elem,
                  unsigned int len)
{
    struct vring_desc *desc = vq->vring.desc;
    unsigned int i, max, min, desc_len;
    uint64_t desc_addr, read_len;
    struct vring_desc desc_buf[VIRTQUEUE_MAX_SIZE];
    unsigned num_bufs = 0;

    max = vq->vring.num;
    i = elem->index;

    if (desc[i].flags & VRING_DESC_F_INDIRECT) {
        if (desc[i].len % sizeof(struct vring_desc)) {
            vu_panic(dev, "Invalid size for indirect buffer table");
        }

        /* loop over the indirect descriptor table */
        desc_addr = desc[i].addr;
        desc_len = desc[i].len;
        max = desc_len / sizeof(struct vring_desc);
        read_len = desc_len;
        desc = vu_gpa_to_va(dev, &read_len, desc_addr);
        if (unlikely(desc && read_len != desc_len)) {
            /* Failed to use zero copy */
            desc = NULL;
            if (!virtqueue_read_indirect_desc(dev, desc_buf,
                                              desc_addr,
                                              desc_len)) {
                desc = desc_buf;
            }
        }
        if (!desc) {
            vu_panic(dev, "Invalid indirect buffer table");
            return;
        }
        i = 0;
    }

    do {
        if (++num_bufs > max) {
            vu_panic(dev, "Looped descriptor");
            return;
        }

        if (desc[i].flags & VRING_DESC_F_WRITE) {
            min = MIN(desc[i].len, len);
            vu_log_write(dev, desc[i].addr, min);
            len -= min;
        }

    } while (len > 0 &&
             (virtqueue_read_next_desc(dev, desc, i, max, &i)
              == VIRTQUEUE_READ_DESC_MORE));
}

void
vu_queue_fill(VuDev *dev, VuVirtq *vq,
              const VuVirtqElement *elem,
              unsigned int len, unsigned int idx)
{
    struct vring_used_elem uelem;

    if (unlikely(dev->broken) ||
        unlikely(!vq->vring.avail)) {
        return;
    }

    vu_log_queue_fill(dev, vq, elem, len);

    idx = (idx + vq->used_idx) % vq->vring.num;

    uelem.id = elem->index;
    uelem.len = len;
    vring_used_write(dev, vq, &uelem, idx);
}

static inline
void vring_used_idx_set(VuDev *dev, VuVirtq *vq, uint16_t val)
{
    vq->vring.used->idx = val;
    vu_log_write(dev,
                 vq->vring.log_guest_addr + offsetof(struct vring_used, idx),
                 sizeof(vq->vring.used->idx));

    vq->used_idx = val;
}

void
vu_queue_flush(VuDev *dev, VuVirtq *vq, unsigned int count)
{
    uint16_t old, new;

    if (unlikely(dev->broken) ||
        unlikely(!vq->vring.avail)) {
        return;
    }

    /* Make sure buffer is written before we update index. */
    smp_wmb();

    old = vq->used_idx;
    new = old + count;
    vring_used_idx_set(dev, vq, new);
    vq->inuse -= count;
    if (unlikely((int16_t)(new - vq->signalled_used) < (uint16_t)(new - old))) {
        vq->signalled_used_valid = false;
    }
}

void
vu_queue_push(VuDev *dev, VuVirtq *vq,
              const VuVirtqElement *elem, unsigned int len)
{
    vu_queue_fill(dev, vq, elem, len, 0);
    vu_queue_inflight_pre_put(dev, vq, elem->index);
    vu_queue_flush(dev, vq, 1);
    vu_queue_inflight_post_put(dev, vq, elem->index);
}
