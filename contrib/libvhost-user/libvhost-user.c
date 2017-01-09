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

#include <qemu/osdep.h>
#include <sys/eventfd.h>
#include <linux/vhost.h>

#include "qemu/atomic.h"

#include "libvhost-user.h"

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

static const char *
vu_request_to_string(int req)
{
#define REQ(req) [req] = #req
    static const char *vu_request_str[] = {
        REQ(VHOST_USER_NONE),
        REQ(VHOST_USER_GET_FEATURES),
        REQ(VHOST_USER_SET_FEATURES),
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
        REQ(VHOST_USER_INPUT_GET_CONFIG),
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
    (void)vasprintf(&buf, msg, ap);
    va_end(ap);

    dev->broken = true;
    dev->panic(dev, buf);
    free(buf);

    /* FIXME: find a way to call virtio_error? */
}

/* Translate guest physical address to our virtual address.  */
void *
vu_gpa_to_va(VuDev *dev, uint64_t guest_addr)
{
    int i;

    /* Find matching memory region.  */
    for (i = 0; i < dev->nregions; i++) {
        VuDevRegion *r = &dev->regions[i];

        if ((guest_addr >= r->gpa) && (guest_addr < (r->gpa + r->size))) {
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

static bool
vu_message_read(VuDev *dev, int conn_fd, VhostUserMsg *vmsg)
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

    do {
        rc = recvmsg(conn_fd, &msg, 0);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (rc <= 0) {
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

    /* Set the version in the flags when sending the reply */
    vmsg->flags &= ~VHOST_USER_VERSION_MASK;
    vmsg->flags |= VHOST_USER_VERSION;
    vmsg->flags |= VHOST_USER_REPLY_MASK;

    do {
        rc = write(conn_fd, p, VHOST_USER_HDR_SIZE);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    do {
        if (vmsg->data) {
            rc = write(conn_fd, vmsg->data, vmsg->size);
        } else {
            rc = write(conn_fd, p + VHOST_USER_HDR_SIZE, vmsg->size);
        }
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (rc <= 0) {
        vu_panic(dev, "Error while writing: %s", strerror(errno));
        return false;
    }

    return true;
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
        page += VHOST_LOG_PAGE;
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
        1ULL << VHOST_F_LOG_ALL |
        1ULL << VHOST_USER_F_PROTOCOL_FEATURES;

    if (dev->iface->get_features) {
        vmsg->payload.u64 |= dev->iface->get_features(dev);
    }

    vmsg->size = sizeof(vmsg->payload.u64);

    DPRINT("Sending back to guest u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    return true;
}

static void
vu_set_enable_all_rings(VuDev *dev, bool enabled)
{
    int i;

    for (i = 0; i < VHOST_MAX_NR_VIRTQUEUE; i++) {
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
vu_set_mem_table_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int i;
    VhostUserMemory *memory = &vmsg->payload.memory;
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
    if (rc == MAP_FAILED) {
        perror("log mmap error");
    }
    dev->log_table = rc;
    dev->log_size = log_mmap_size;

    vmsg->size = sizeof(vmsg->payload.u64);

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
    struct vhost_vring_addr *vra = &vmsg->payload.addr;
    unsigned int index = vra->index;
    VuVirtq *vq = &dev->vq[index];

    DPRINT("vhost_vring_addr:\n");
    DPRINT("    index:  %d\n", vra->index);
    DPRINT("    flags:  %d\n", vra->flags);
    DPRINT("    desc_user_addr:   0x%016llx\n", vra->desc_user_addr);
    DPRINT("    used_user_addr:   0x%016llx\n", vra->used_user_addr);
    DPRINT("    avail_user_addr:  0x%016llx\n", vra->avail_user_addr);
    DPRINT("    log_guest_addr:   0x%016llx\n", vra->log_guest_addr);

    vq->vring.flags = vra->flags;
    vq->vring.desc = qva_to_va(dev, vra->desc_user_addr);
    vq->vring.used = qva_to_va(dev, vra->used_user_addr);
    vq->vring.avail = qva_to_va(dev, vra->avail_user_addr);
    vq->vring.log_guest_addr = vra->log_guest_addr;

    DPRINT("Setting virtq addresses:\n");
    DPRINT("    vring_desc  at %p\n", vq->vring.desc);
    DPRINT("    vring_used  at %p\n", vq->vring.used);
    DPRINT("    vring_avail at %p\n", vq->vring.avail);

    if (!(vq->vring.desc && vq->vring.used && vq->vring.avail)) {
        vu_panic(dev, "Invalid vring_addr message");
        return false;
    }

    vq->used_idx = vq->vring.used->idx;

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

    if (index >= VHOST_MAX_NR_VIRTQUEUE) {
        vmsg_close_fds(vmsg);
        vu_panic(dev, "Invalid queue index: %u", index);
        return false;
    }

    if (vmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK ||
        vmsg->fd_num != 1) {
        vmsg_close_fds(vmsg);
        vu_panic(dev, "Invalid fds in request: %d", vmsg->request);
        return false;
    }

    return true;
}

static bool
vu_set_vring_kick_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    if (!vu_check_queue_msg_file(dev, vmsg)) {
        return false;
    }

    if (dev->vq[index].kick_fd != -1) {
        dev->remove_watch(dev, dev->vq[index].kick_fd);
        close(dev->vq[index].kick_fd);
        dev->vq[index].kick_fd = -1;
    }

    if (!(vmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK)) {
        dev->vq[index].kick_fd = vmsg->fds[0];
        DPRINT("Got kick_fd: %d for vq: %d\n", vmsg->fds[0], index);
    }

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

static bool
vu_set_vring_call_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    if (!vu_check_queue_msg_file(dev, vmsg)) {
        return false;
    }

    if (dev->vq[index].call_fd != -1) {
        close(dev->vq[index].call_fd);
        dev->vq[index].call_fd = -1;
    }

    if (!(vmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK)) {
        dev->vq[index].call_fd = vmsg->fds[0];
    }

    DPRINT("Got call_fd: %d for vq: %d\n", vmsg->fds[0], index);

    return false;
}

static bool
vu_set_vring_err_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    int index = vmsg->payload.u64 & VHOST_USER_VRING_IDX_MASK;

    DPRINT("u64: 0x%016"PRIx64"\n", vmsg->payload.u64);

    if (!vu_check_queue_msg_file(dev, vmsg)) {
        return false;
    }

    if (dev->vq[index].err_fd != -1) {
        close(dev->vq[index].err_fd);
        dev->vq[index].err_fd = -1;
    }

    if (!(vmsg->payload.u64 & VHOST_USER_VRING_NOFD_MASK)) {
        dev->vq[index].err_fd = vmsg->fds[0];
    }

    return false;
}

static bool
vu_get_protocol_features_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    uint64_t features = 1ULL << VHOST_USER_PROTOCOL_F_LOG_SHMFD;

    if (dev->iface->get_protocol_features) {
        features |= dev->iface->get_protocol_features(dev);
    }

    vmsg->payload.u64 = features;
    vmsg->size = sizeof(vmsg->payload.u64);

    return true;
}

static bool
vu_set_protocol_features_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    uint64_t features = vmsg->payload.u64;

    DPRINT("u64: 0x%016"PRIx64"\n", features);

    dev->protocol_features = vmsg->payload.u64;

    if (dev->iface->set_protocol_features) {
        dev->iface->set_protocol_features(dev, features);
    }

    return false;
}

static bool
vu_get_queue_num_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    DPRINT("Function %s() not implemented yet.\n", __func__);
    return false;
}

static bool
vu_set_vring_enable_exec(VuDev *dev, VhostUserMsg *vmsg)
{
    unsigned int index = vmsg->payload.state.index;
    unsigned int enable = vmsg->payload.state.num;

    DPRINT("State.index: %d\n", index);
    DPRINT("State.enable:   %d\n", enable);

    if (index >= VHOST_MAX_NR_VIRTQUEUE) {
        vu_panic(dev, "Invalid vring_enable index: %u", index);
        return false;
    }

    dev->vq[index].enable = enable;
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
    bool success = false;

    if (!vu_message_read(dev, dev->sock, &vmsg)) {
        goto end;
    }

    reply_requested = vu_process_message(dev, &vmsg);
    if (!reply_requested) {
        success = true;
        goto end;
    }

    if (!vu_message_write(dev, dev->sock, &vmsg)) {
        goto end;
    }

    success = true;

end:
    g_free(vmsg.data);
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

    for (i = 0; i < VHOST_MAX_NR_VIRTQUEUE; i++) {
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
    }


    vu_close_log(dev);

    if (dev->sock != -1) {
        close(dev->sock);
    }
}

void
vu_init(VuDev *dev,
        int socket,
        vu_panic_cb panic,
        vu_set_watch_cb set_watch,
        vu_remove_watch_cb remove_watch,
        const VuDevIface *iface)
{
    int i;

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
    for (i = 0; i < VHOST_MAX_NR_VIRTQUEUE; i++) {
        dev->vq[i] = (VuVirtq) {
            .call_fd = -1, .kick_fd = -1, .err_fd = -1,
            .notification = true,
        };
    }
}

VuVirtq *
vu_get_queue(VuDev *dev, int qidx)
{
    assert(qidx < VHOST_MAX_NR_VIRTQUEUE);
    return &dev->vq[qidx];
}

bool
vu_queue_enabled(VuDev *dev, VuVirtq *vq)
{
    return vq->enable;
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
        vu_panic(dev, "Guest says index %u is available", head);
        return false;
    }

    return true;
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
        vu_panic(dev, "Desc next is %u", next);
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
    while ((rc = virtqueue_num_heads(dev, vq, idx)) > 0) {
        unsigned int max, num_bufs, indirect = 0;
        struct vring_desc *desc;
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
            max = desc[i].len / sizeof(struct vring_desc);
            desc = vu_gpa_to_va(dev, desc[i].addr);
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
int
vu_queue_empty(VuDev *dev, VuVirtq *vq)
{
    if (vq->shadow_avail_idx != vq->last_avail_idx) {
        return 0;
    }

    return vring_avail_idx(vq) == vq->last_avail_idx;
}

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

void
vu_queue_notify(VuDev *dev, VuVirtq *vq)
{
    if (unlikely(dev->broken)) {
        return;
    }

    if (!vring_notify(dev, vq)) {
        DPRINT("skipped notify...\n");
        return;
    }

    if (eventfd_write(vq->call_fd, 1) < 0) {
        vu_panic(dev, "Error writing eventfd: %s", strerror(errno));
    }
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

    iov[num_sg].iov_base = vu_gpa_to_va(dev, pa);
    iov[num_sg].iov_len = sz;
    num_sg++;

    *p_num_sg = num_sg;
}

/* Round number down to multiple */
#define ALIGN_DOWN(n, m) ((n) / (m) * (m))

/* Round number up to multiple */
#define ALIGN_UP(n, m) ALIGN_DOWN((n) + (m) - 1, (m))

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

void *
vu_queue_pop(VuDev *dev, VuVirtq *vq, size_t sz)
{
    unsigned int i, head, max;
    VuVirtqElement *elem;
    unsigned out_num, in_num;
    struct iovec iov[VIRTQUEUE_MAX_SIZE];
    struct vring_desc *desc;
    int rc;

    if (unlikely(dev->broken)) {
        return NULL;
    }

    if (vu_queue_empty(dev, vq)) {
        return NULL;
    }
    /* Needed after virtio_queue_empty(), see comment in
     * virtqueue_num_heads(). */
    smp_rmb();

    /* When we start there are none of either input nor output. */
    out_num = in_num = 0;

    max = vq->vring.num;
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

    i = head;
    desc = vq->vring.desc;
    if (desc[i].flags & VRING_DESC_F_INDIRECT) {
        if (desc[i].len % sizeof(struct vring_desc)) {
            vu_panic(dev, "Invalid size for indirect buffer table");
        }

        /* loop over the indirect descriptor table */
        max = desc[i].len / sizeof(struct vring_desc);
        desc = vu_gpa_to_va(dev, desc[i].addr);
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
        return NULL;
    }

    /* Now copy what we have collected and mapped */
    elem = virtqueue_alloc_element(sz, out_num, in_num);
    elem->index = head;
    for (i = 0; i < out_num; i++) {
        elem->out_sg[i] = iov[i];
    }
    for (i = 0; i < in_num; i++) {
        elem->in_sg[i] = iov[out_num + i];
    }

    vq->inuse++;

    return elem;
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
    unsigned int i, max, min;
    unsigned num_bufs = 0;

    max = vq->vring.num;
    i = elem->index;

    if (desc[i].flags & VRING_DESC_F_INDIRECT) {
        if (desc[i].len % sizeof(struct vring_desc)) {
            vu_panic(dev, "Invalid size for indirect buffer table");
        }

        /* loop over the indirect descriptor table */
        max = desc[i].len / sizeof(struct vring_desc);
        desc = vu_gpa_to_va(dev, desc[i].addr);
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

    if (unlikely(dev->broken)) {
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

    if (unlikely(dev->broken)) {
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
    vu_queue_flush(dev, vq, 1);
}
