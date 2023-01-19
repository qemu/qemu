/*
 * vhost-user
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-net.h"
#include "chardev/char-fe.h"
#include "io/channel-socket.h"
#include "sysemu/kvm.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "sysemu/runstate.h"
#include "sysemu/cryptodev.h"
#include "migration/migration.h"
#include "migration/postcopy-ram.h"
#include "trace.h"
#include "exec/ramblock.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "standard-headers/linux/vhost_types.h"

#ifdef CONFIG_LINUX
#include <linux/userfaultfd.h>
#endif

#define VHOST_MEMORY_BASELINE_NREGIONS    8
#define VHOST_USER_F_PROTOCOL_FEATURES 30
#define VHOST_USER_SLAVE_MAX_FDS     8

/*
 * Set maximum number of RAM slots supported to
 * the maximum number supported by the target
 * hardware plaform.
 */
#if defined(TARGET_X86) || defined(TARGET_X86_64) || \
    defined(TARGET_ARM) || defined(TARGET_ARM_64)
#include "hw/acpi/acpi.h"
#define VHOST_USER_MAX_RAM_SLOTS ACPI_MAX_RAM_SLOTS

#elif defined(TARGET_PPC) || defined(TARGET_PPC64)
#include "hw/ppc/spapr.h"
#define VHOST_USER_MAX_RAM_SLOTS SPAPR_MAX_RAM_SLOTS

#else
#define VHOST_USER_MAX_RAM_SLOTS 512
#endif

/*
 * Maximum size of virtio device config space
 */
#define VHOST_USER_MAX_CONFIG_SIZE 256

enum VhostUserProtocolFeature {
    VHOST_USER_PROTOCOL_F_MQ = 0,
    VHOST_USER_PROTOCOL_F_LOG_SHMFD = 1,
    VHOST_USER_PROTOCOL_F_RARP = 2,
    VHOST_USER_PROTOCOL_F_REPLY_ACK = 3,
    VHOST_USER_PROTOCOL_F_NET_MTU = 4,
    VHOST_USER_PROTOCOL_F_SLAVE_REQ = 5,
    VHOST_USER_PROTOCOL_F_CROSS_ENDIAN = 6,
    VHOST_USER_PROTOCOL_F_CRYPTO_SESSION = 7,
    VHOST_USER_PROTOCOL_F_PAGEFAULT = 8,
    VHOST_USER_PROTOCOL_F_CONFIG = 9,
    VHOST_USER_PROTOCOL_F_SLAVE_SEND_FD = 10,
    VHOST_USER_PROTOCOL_F_HOST_NOTIFIER = 11,
    VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD = 12,
    VHOST_USER_PROTOCOL_F_RESET_DEVICE = 13,
    /* Feature 14 reserved for VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS. */
    VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS = 15,
    VHOST_USER_PROTOCOL_F_STATUS = 16,
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
    VHOST_USER_NET_SET_MTU = 20,
    VHOST_USER_SET_SLAVE_REQ_FD = 21,
    VHOST_USER_IOTLB_MSG = 22,
    VHOST_USER_SET_VRING_ENDIAN = 23,
    VHOST_USER_GET_CONFIG = 24,
    VHOST_USER_SET_CONFIG = 25,
    VHOST_USER_CREATE_CRYPTO_SESSION = 26,
    VHOST_USER_CLOSE_CRYPTO_SESSION = 27,
    VHOST_USER_POSTCOPY_ADVISE  = 28,
    VHOST_USER_POSTCOPY_LISTEN  = 29,
    VHOST_USER_POSTCOPY_END     = 30,
    VHOST_USER_GET_INFLIGHT_FD = 31,
    VHOST_USER_SET_INFLIGHT_FD = 32,
    VHOST_USER_GPU_SET_SOCKET = 33,
    VHOST_USER_RESET_DEVICE = 34,
    /* Message number 35 reserved for VHOST_USER_VRING_KICK. */
    VHOST_USER_GET_MAX_MEM_SLOTS = 36,
    VHOST_USER_ADD_MEM_REG = 37,
    VHOST_USER_REM_MEM_REG = 38,
    VHOST_USER_SET_STATUS = 39,
    VHOST_USER_GET_STATUS = 40,
    VHOST_USER_MAX
} VhostUserRequest;

typedef enum VhostUserSlaveRequest {
    VHOST_USER_SLAVE_NONE = 0,
    VHOST_USER_SLAVE_IOTLB_MSG = 1,
    VHOST_USER_SLAVE_CONFIG_CHANGE_MSG = 2,
    VHOST_USER_SLAVE_VRING_HOST_NOTIFIER_MSG = 3,
    VHOST_USER_SLAVE_MAX
}  VhostUserSlaveRequest;

typedef struct VhostUserMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    uint64_t mmap_offset;
} VhostUserMemoryRegion;

typedef struct VhostUserMemory {
    uint32_t nregions;
    uint32_t padding;
    VhostUserMemoryRegion regions[VHOST_MEMORY_BASELINE_NREGIONS];
} VhostUserMemory;

typedef struct VhostUserMemRegMsg {
    uint64_t padding;
    VhostUserMemoryRegion region;
} VhostUserMemRegMsg;

typedef struct VhostUserLog {
    uint64_t mmap_size;
    uint64_t mmap_offset;
} VhostUserLog;

typedef struct VhostUserConfig {
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
    uint8_t region[VHOST_USER_MAX_CONFIG_SIZE];
} VhostUserConfig;

#define VHOST_CRYPTO_SYM_HMAC_MAX_KEY_LEN    512
#define VHOST_CRYPTO_SYM_CIPHER_MAX_KEY_LEN  64

typedef struct VhostUserCryptoSession {
    /* session id for success, -1 on errors */
    int64_t session_id;
    CryptoDevBackendSymSessionInfo session_setup_data;
    uint8_t key[VHOST_CRYPTO_SYM_CIPHER_MAX_KEY_LEN];
    uint8_t auth_key[VHOST_CRYPTO_SYM_HMAC_MAX_KEY_LEN];
} VhostUserCryptoSession;

static VhostUserConfig c __attribute__ ((unused));
#define VHOST_USER_CONFIG_HDR_SIZE (sizeof(c.offset) \
                                   + sizeof(c.size) \
                                   + sizeof(c.flags))

typedef struct VhostUserVringArea {
    uint64_t u64;
    uint64_t size;
    uint64_t offset;
} VhostUserVringArea;

typedef struct VhostUserInflight {
    uint64_t mmap_size;
    uint64_t mmap_offset;
    uint16_t num_queues;
    uint16_t queue_size;
} VhostUserInflight;

typedef struct {
    VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1 << 2)
#define VHOST_USER_NEED_REPLY_MASK  (0x1 << 3)
    uint32_t flags;
    uint32_t size; /* the following payload size */
} QEMU_PACKED VhostUserHeader;

typedef union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1 << 8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserMemRegMsg mem_reg;
        VhostUserLog log;
        struct vhost_iotlb_msg iotlb;
        VhostUserConfig config;
        VhostUserCryptoSession session;
        VhostUserVringArea area;
        VhostUserInflight inflight;
} VhostUserPayload;

typedef struct VhostUserMsg {
    VhostUserHeader hdr;
    VhostUserPayload payload;
} QEMU_PACKED VhostUserMsg;

static VhostUserMsg m __attribute__ ((unused));
#define VHOST_USER_HDR_SIZE (sizeof(VhostUserHeader))

#define VHOST_USER_PAYLOAD_SIZE (sizeof(VhostUserPayload))

/* The version of the protocol we support */
#define VHOST_USER_VERSION    (0x1)

struct vhost_user {
    struct vhost_dev *dev;
    /* Shared between vhost devs of the same virtio device */
    VhostUserState *user;
    QIOChannel *slave_ioc;
    GSource *slave_src;
    NotifierWithReturn postcopy_notifier;
    struct PostCopyFD  postcopy_fd;
    uint64_t           postcopy_client_bases[VHOST_USER_MAX_RAM_SLOTS];
    /* Length of the region_rb and region_rb_offset arrays */
    size_t             region_rb_len;
    /* RAMBlock associated with a given region */
    RAMBlock         **region_rb;
    /*
     * The offset from the start of the RAMBlock to the start of the
     * vhost region.
     */
    ram_addr_t        *region_rb_offset;

    /* True once we've entered postcopy_listen */
    bool               postcopy_listen;

    /* Our current regions */
    int num_shadow_regions;
    struct vhost_memory_region shadow_regions[VHOST_USER_MAX_RAM_SLOTS];
};

struct scrub_regions {
    struct vhost_memory_region *region;
    int reg_idx;
    int fd_idx;
};

static bool ioeventfd_enabled(void)
{
    return !kvm_enabled() || kvm_eventfds_enabled();
}

static int vhost_user_read_header(struct vhost_dev *dev, VhostUserMsg *msg)
{
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = u->user->chr;
    uint8_t *p = (uint8_t *) msg;
    int r, size = VHOST_USER_HDR_SIZE;

    r = qemu_chr_fe_read_all(chr, p, size);
    if (r != size) {
        int saved_errno = errno;
        error_report("Failed to read msg header. Read %d instead of %d."
                     " Original request %d.", r, size, msg->hdr.request);
        return r < 0 ? -saved_errno : -EIO;
    }

    /* validate received flags */
    if (msg->hdr.flags != (VHOST_USER_REPLY_MASK | VHOST_USER_VERSION)) {
        error_report("Failed to read msg header."
                " Flags 0x%x instead of 0x%x.", msg->hdr.flags,
                VHOST_USER_REPLY_MASK | VHOST_USER_VERSION);
        return -EPROTO;
    }

    trace_vhost_user_read(msg->hdr.request, msg->hdr.flags);

    return 0;
}

static int vhost_user_read(struct vhost_dev *dev, VhostUserMsg *msg)
{
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = u->user->chr;
    uint8_t *p = (uint8_t *) msg;
    int r, size;

    r = vhost_user_read_header(dev, msg);
    if (r < 0) {
        return r;
    }

    /* validate message size is sane */
    if (msg->hdr.size > VHOST_USER_PAYLOAD_SIZE) {
        error_report("Failed to read msg header."
                " Size %d exceeds the maximum %zu.", msg->hdr.size,
                VHOST_USER_PAYLOAD_SIZE);
        return -EPROTO;
    }

    if (msg->hdr.size) {
        p += VHOST_USER_HDR_SIZE;
        size = msg->hdr.size;
        r = qemu_chr_fe_read_all(chr, p, size);
        if (r != size) {
            int saved_errno = errno;
            error_report("Failed to read msg payload."
                         " Read %d instead of %d.", r, msg->hdr.size);
            return r < 0 ? -saved_errno : -EIO;
        }
    }

    return 0;
}

static int process_message_reply(struct vhost_dev *dev,
                                 const VhostUserMsg *msg)
{
    int ret;
    VhostUserMsg msg_reply;

    if ((msg->hdr.flags & VHOST_USER_NEED_REPLY_MASK) == 0) {
        return 0;
    }

    ret = vhost_user_read(dev, &msg_reply);
    if (ret < 0) {
        return ret;
    }

    if (msg_reply.hdr.request != msg->hdr.request) {
        error_report("Received unexpected msg type. "
                     "Expected %d received %d",
                     msg->hdr.request, msg_reply.hdr.request);
        return -EPROTO;
    }

    return msg_reply.payload.u64 ? -EIO : 0;
}

static bool vhost_user_one_time_request(VhostUserRequest request)
{
    switch (request) {
    case VHOST_USER_SET_OWNER:
    case VHOST_USER_RESET_OWNER:
    case VHOST_USER_SET_MEM_TABLE:
    case VHOST_USER_GET_QUEUE_NUM:
    case VHOST_USER_NET_SET_MTU:
        return true;
    default:
        return false;
    }
}

/* most non-init callers ignore the error */
static int vhost_user_write(struct vhost_dev *dev, VhostUserMsg *msg,
                            int *fds, int fd_num)
{
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = u->user->chr;
    int ret, size = VHOST_USER_HDR_SIZE + msg->hdr.size;

    /*
     * For non-vring specific requests, like VHOST_USER_SET_MEM_TABLE,
     * we just need send it once in the first time. For later such
     * request, we just ignore it.
     */
    if (vhost_user_one_time_request(msg->hdr.request) && dev->vq_index != 0) {
        msg->hdr.flags &= ~VHOST_USER_NEED_REPLY_MASK;
        return 0;
    }

    if (qemu_chr_fe_set_msgfds(chr, fds, fd_num) < 0) {
        error_report("Failed to set msg fds.");
        return -EINVAL;
    }

    ret = qemu_chr_fe_write_all(chr, (const uint8_t *) msg, size);
    if (ret != size) {
        int saved_errno = errno;
        error_report("Failed to write msg."
                     " Wrote %d instead of %d.", ret, size);
        return ret < 0 ? -saved_errno : -EIO;
    }

    trace_vhost_user_write(msg->hdr.request, msg->hdr.flags);

    return 0;
}

int vhost_user_gpu_set_socket(struct vhost_dev *dev, int fd)
{
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_GPU_SET_SOCKET,
        .hdr.flags = VHOST_USER_VERSION,
    };

    return vhost_user_write(dev, &msg, &fd, 1);
}

static int vhost_user_set_log_base(struct vhost_dev *dev, uint64_t base,
                                   struct vhost_log *log)
{
    int fds[VHOST_USER_MAX_RAM_SLOTS];
    size_t fd_num = 0;
    bool shmfd = virtio_has_feature(dev->protocol_features,
                                    VHOST_USER_PROTOCOL_F_LOG_SHMFD);
    int ret;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_SET_LOG_BASE,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.log.mmap_size = log->size * sizeof(*(log->log)),
        .payload.log.mmap_offset = 0,
        .hdr.size = sizeof(msg.payload.log),
    };

    if (shmfd && log->fd != -1) {
        fds[fd_num++] = log->fd;
    }

    ret = vhost_user_write(dev, &msg, fds, fd_num);
    if (ret < 0) {
        return ret;
    }

    if (shmfd) {
        msg.hdr.size = 0;
        ret = vhost_user_read(dev, &msg);
        if (ret < 0) {
            return ret;
        }

        if (msg.hdr.request != VHOST_USER_SET_LOG_BASE) {
            error_report("Received unexpected msg type. "
                         "Expected %d received %d",
                         VHOST_USER_SET_LOG_BASE, msg.hdr.request);
            return -EPROTO;
        }
    }

    return 0;
}

static MemoryRegion *vhost_user_get_mr_data(uint64_t addr, ram_addr_t *offset,
                                            int *fd)
{
    MemoryRegion *mr;

    assert((uintptr_t)addr == addr);
    mr = memory_region_from_host((void *)(uintptr_t)addr, offset);
    *fd = memory_region_get_fd(mr);

    return mr;
}

static void vhost_user_fill_msg_region(VhostUserMemoryRegion *dst,
                                       struct vhost_memory_region *src,
                                       uint64_t mmap_offset)
{
    assert(src != NULL && dst != NULL);
    dst->userspace_addr = src->userspace_addr;
    dst->memory_size = src->memory_size;
    dst->guest_phys_addr = src->guest_phys_addr;
    dst->mmap_offset = mmap_offset;
}

static int vhost_user_fill_set_mem_table_msg(struct vhost_user *u,
                                             struct vhost_dev *dev,
                                             VhostUserMsg *msg,
                                             int *fds, size_t *fd_num,
                                             bool track_ramblocks)
{
    int i, fd;
    ram_addr_t offset;
    MemoryRegion *mr;
    struct vhost_memory_region *reg;
    VhostUserMemoryRegion region_buffer;

    msg->hdr.request = VHOST_USER_SET_MEM_TABLE;

    for (i = 0; i < dev->mem->nregions; ++i) {
        reg = dev->mem->regions + i;

        mr = vhost_user_get_mr_data(reg->userspace_addr, &offset, &fd);
        if (fd > 0) {
            if (track_ramblocks) {
                assert(*fd_num < VHOST_MEMORY_BASELINE_NREGIONS);
                trace_vhost_user_set_mem_table_withfd(*fd_num, mr->name,
                                                      reg->memory_size,
                                                      reg->guest_phys_addr,
                                                      reg->userspace_addr,
                                                      offset);
                u->region_rb_offset[i] = offset;
                u->region_rb[i] = mr->ram_block;
            } else if (*fd_num == VHOST_MEMORY_BASELINE_NREGIONS) {
                error_report("Failed preparing vhost-user memory table msg");
                return -ENOBUFS;
            }
            vhost_user_fill_msg_region(&region_buffer, reg, offset);
            msg->payload.memory.regions[*fd_num] = region_buffer;
            fds[(*fd_num)++] = fd;
        } else if (track_ramblocks) {
            u->region_rb_offset[i] = 0;
            u->region_rb[i] = NULL;
        }
    }

    msg->payload.memory.nregions = *fd_num;

    if (!*fd_num) {
        error_report("Failed initializing vhost-user memory map, "
                     "consider using -object memory-backend-file share=on");
        return -EINVAL;
    }

    msg->hdr.size = sizeof(msg->payload.memory.nregions);
    msg->hdr.size += sizeof(msg->payload.memory.padding);
    msg->hdr.size += *fd_num * sizeof(VhostUserMemoryRegion);

    return 0;
}

static inline bool reg_equal(struct vhost_memory_region *shadow_reg,
                             struct vhost_memory_region *vdev_reg)
{
    return shadow_reg->guest_phys_addr == vdev_reg->guest_phys_addr &&
        shadow_reg->userspace_addr == vdev_reg->userspace_addr &&
        shadow_reg->memory_size == vdev_reg->memory_size;
}

static void scrub_shadow_regions(struct vhost_dev *dev,
                                 struct scrub_regions *add_reg,
                                 int *nr_add_reg,
                                 struct scrub_regions *rem_reg,
                                 int *nr_rem_reg, uint64_t *shadow_pcb,
                                 bool track_ramblocks)
{
    struct vhost_user *u = dev->opaque;
    bool found[VHOST_USER_MAX_RAM_SLOTS] = {};
    struct vhost_memory_region *reg, *shadow_reg;
    int i, j, fd, add_idx = 0, rm_idx = 0, fd_num = 0;
    ram_addr_t offset;
    MemoryRegion *mr;
    bool matching;

    /*
     * Find memory regions present in our shadow state which are not in
     * the device's current memory state.
     *
     * Mark regions in both the shadow and device state as "found".
     */
    for (i = 0; i < u->num_shadow_regions; i++) {
        shadow_reg = &u->shadow_regions[i];
        matching = false;

        for (j = 0; j < dev->mem->nregions; j++) {
            reg = &dev->mem->regions[j];

            mr = vhost_user_get_mr_data(reg->userspace_addr, &offset, &fd);

            if (reg_equal(shadow_reg, reg)) {
                matching = true;
                found[j] = true;
                if (track_ramblocks) {
                    /*
                     * Reset postcopy client bases, region_rb, and
                     * region_rb_offset in case regions are removed.
                     */
                    if (fd > 0) {
                        u->region_rb_offset[j] = offset;
                        u->region_rb[j] = mr->ram_block;
                        shadow_pcb[j] = u->postcopy_client_bases[i];
                    } else {
                        u->region_rb_offset[j] = 0;
                        u->region_rb[j] = NULL;
                    }
                }
                break;
            }
        }

        /*
         * If the region was not found in the current device memory state
         * create an entry for it in the removed list.
         */
        if (!matching) {
            rem_reg[rm_idx].region = shadow_reg;
            rem_reg[rm_idx++].reg_idx = i;
        }
    }

    /*
     * For regions not marked "found", create entries in the added list.
     *
     * Note their indexes in the device memory state and the indexes of their
     * file descriptors.
     */
    for (i = 0; i < dev->mem->nregions; i++) {
        reg = &dev->mem->regions[i];
        vhost_user_get_mr_data(reg->userspace_addr, &offset, &fd);
        if (fd > 0) {
            ++fd_num;
        }

        /*
         * If the region was in both the shadow and device state we don't
         * need to send a VHOST_USER_ADD_MEM_REG message for it.
         */
        if (found[i]) {
            continue;
        }

        add_reg[add_idx].region = reg;
        add_reg[add_idx].reg_idx = i;
        add_reg[add_idx++].fd_idx = fd_num;
    }
    *nr_rem_reg = rm_idx;
    *nr_add_reg = add_idx;

    return;
}

static int send_remove_regions(struct vhost_dev *dev,
                               struct scrub_regions *remove_reg,
                               int nr_rem_reg, VhostUserMsg *msg,
                               bool reply_supported)
{
    struct vhost_user *u = dev->opaque;
    struct vhost_memory_region *shadow_reg;
    int i, fd, shadow_reg_idx, ret;
    ram_addr_t offset;
    VhostUserMemoryRegion region_buffer;

    /*
     * The regions in remove_reg appear in the same order they do in the
     * shadow table. Therefore we can minimize memory copies by iterating
     * through remove_reg backwards.
     */
    for (i = nr_rem_reg - 1; i >= 0; i--) {
        shadow_reg = remove_reg[i].region;
        shadow_reg_idx = remove_reg[i].reg_idx;

        vhost_user_get_mr_data(shadow_reg->userspace_addr, &offset, &fd);

        if (fd > 0) {
            msg->hdr.request = VHOST_USER_REM_MEM_REG;
            vhost_user_fill_msg_region(&region_buffer, shadow_reg, 0);
            msg->payload.mem_reg.region = region_buffer;

            ret = vhost_user_write(dev, msg, NULL, 0);
            if (ret < 0) {
                return ret;
            }

            if (reply_supported) {
                ret = process_message_reply(dev, msg);
                if (ret) {
                    return ret;
                }
            }
        }

        /*
         * At this point we know the backend has unmapped the region. It is now
         * safe to remove it from the shadow table.
         */
        memmove(&u->shadow_regions[shadow_reg_idx],
                &u->shadow_regions[shadow_reg_idx + 1],
                sizeof(struct vhost_memory_region) *
                (u->num_shadow_regions - shadow_reg_idx - 1));
        u->num_shadow_regions--;
    }

    return 0;
}

static int send_add_regions(struct vhost_dev *dev,
                            struct scrub_regions *add_reg, int nr_add_reg,
                            VhostUserMsg *msg, uint64_t *shadow_pcb,
                            bool reply_supported, bool track_ramblocks)
{
    struct vhost_user *u = dev->opaque;
    int i, fd, ret, reg_idx, reg_fd_idx;
    struct vhost_memory_region *reg;
    MemoryRegion *mr;
    ram_addr_t offset;
    VhostUserMsg msg_reply;
    VhostUserMemoryRegion region_buffer;

    for (i = 0; i < nr_add_reg; i++) {
        reg = add_reg[i].region;
        reg_idx = add_reg[i].reg_idx;
        reg_fd_idx = add_reg[i].fd_idx;

        mr = vhost_user_get_mr_data(reg->userspace_addr, &offset, &fd);

        if (fd > 0) {
            if (track_ramblocks) {
                trace_vhost_user_set_mem_table_withfd(reg_fd_idx, mr->name,
                                                      reg->memory_size,
                                                      reg->guest_phys_addr,
                                                      reg->userspace_addr,
                                                      offset);
                u->region_rb_offset[reg_idx] = offset;
                u->region_rb[reg_idx] = mr->ram_block;
            }
            msg->hdr.request = VHOST_USER_ADD_MEM_REG;
            vhost_user_fill_msg_region(&region_buffer, reg, offset);
            msg->payload.mem_reg.region = region_buffer;

            ret = vhost_user_write(dev, msg, &fd, 1);
            if (ret < 0) {
                return ret;
            }

            if (track_ramblocks) {
                uint64_t reply_gpa;

                ret = vhost_user_read(dev, &msg_reply);
                if (ret < 0) {
                    return ret;
                }

                reply_gpa = msg_reply.payload.mem_reg.region.guest_phys_addr;

                if (msg_reply.hdr.request != VHOST_USER_ADD_MEM_REG) {
                    error_report("%s: Received unexpected msg type."
                                 "Expected %d received %d", __func__,
                                 VHOST_USER_ADD_MEM_REG,
                                 msg_reply.hdr.request);
                    return -EPROTO;
                }

                /*
                 * We're using the same structure, just reusing one of the
                 * fields, so it should be the same size.
                 */
                if (msg_reply.hdr.size != msg->hdr.size) {
                    error_report("%s: Unexpected size for postcopy reply "
                                 "%d vs %d", __func__, msg_reply.hdr.size,
                                 msg->hdr.size);
                    return -EPROTO;
                }

                /* Get the postcopy client base from the backend's reply. */
                if (reply_gpa == dev->mem->regions[reg_idx].guest_phys_addr) {
                    shadow_pcb[reg_idx] =
                        msg_reply.payload.mem_reg.region.userspace_addr;
                    trace_vhost_user_set_mem_table_postcopy(
                        msg_reply.payload.mem_reg.region.userspace_addr,
                        msg->payload.mem_reg.region.userspace_addr,
                        reg_fd_idx, reg_idx);
                } else {
                    error_report("%s: invalid postcopy reply for region. "
                                 "Got guest physical address %" PRIX64 ", expected "
                                 "%" PRIX64, __func__, reply_gpa,
                                 dev->mem->regions[reg_idx].guest_phys_addr);
                    return -EPROTO;
                }
            } else if (reply_supported) {
                ret = process_message_reply(dev, msg);
                if (ret) {
                    return ret;
                }
            }
        } else if (track_ramblocks) {
            u->region_rb_offset[reg_idx] = 0;
            u->region_rb[reg_idx] = NULL;
        }

        /*
         * At this point, we know the backend has mapped in the new
         * region, if the region has a valid file descriptor.
         *
         * The region should now be added to the shadow table.
         */
        u->shadow_regions[u->num_shadow_regions].guest_phys_addr =
            reg->guest_phys_addr;
        u->shadow_regions[u->num_shadow_regions].userspace_addr =
            reg->userspace_addr;
        u->shadow_regions[u->num_shadow_regions].memory_size =
            reg->memory_size;
        u->num_shadow_regions++;
    }

    return 0;
}

static int vhost_user_add_remove_regions(struct vhost_dev *dev,
                                         VhostUserMsg *msg,
                                         bool reply_supported,
                                         bool track_ramblocks)
{
    struct vhost_user *u = dev->opaque;
    struct scrub_regions add_reg[VHOST_USER_MAX_RAM_SLOTS];
    struct scrub_regions rem_reg[VHOST_USER_MAX_RAM_SLOTS];
    uint64_t shadow_pcb[VHOST_USER_MAX_RAM_SLOTS] = {};
    int nr_add_reg, nr_rem_reg;
    int ret;

    msg->hdr.size = sizeof(msg->payload.mem_reg);

    /* Find the regions which need to be removed or added. */
    scrub_shadow_regions(dev, add_reg, &nr_add_reg, rem_reg, &nr_rem_reg,
                         shadow_pcb, track_ramblocks);

    if (nr_rem_reg) {
        ret = send_remove_regions(dev, rem_reg, nr_rem_reg, msg,
                                  reply_supported);
        if (ret < 0) {
            goto err;
        }
    }

    if (nr_add_reg) {
        ret = send_add_regions(dev, add_reg, nr_add_reg, msg, shadow_pcb,
                               reply_supported, track_ramblocks);
        if (ret < 0) {
            goto err;
        }
    }

    if (track_ramblocks) {
        memcpy(u->postcopy_client_bases, shadow_pcb,
               sizeof(uint64_t) * VHOST_USER_MAX_RAM_SLOTS);
        /*
         * Now we've registered this with the postcopy code, we ack to the
         * client, because now we're in the position to be able to deal with
         * any faults it generates.
         */
        /* TODO: Use this for failure cases as well with a bad value. */
        msg->hdr.size = sizeof(msg->payload.u64);
        msg->payload.u64 = 0; /* OK */

        ret = vhost_user_write(dev, msg, NULL, 0);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;

err:
    if (track_ramblocks) {
        memcpy(u->postcopy_client_bases, shadow_pcb,
               sizeof(uint64_t) * VHOST_USER_MAX_RAM_SLOTS);
    }

    return ret;
}

static int vhost_user_set_mem_table_postcopy(struct vhost_dev *dev,
                                             struct vhost_memory *mem,
                                             bool reply_supported,
                                             bool config_mem_slots)
{
    struct vhost_user *u = dev->opaque;
    int fds[VHOST_MEMORY_BASELINE_NREGIONS];
    size_t fd_num = 0;
    VhostUserMsg msg_reply;
    int region_i, msg_i;
    int ret;

    VhostUserMsg msg = {
        .hdr.flags = VHOST_USER_VERSION,
    };

    if (u->region_rb_len < dev->mem->nregions) {
        u->region_rb = g_renew(RAMBlock*, u->region_rb, dev->mem->nregions);
        u->region_rb_offset = g_renew(ram_addr_t, u->region_rb_offset,
                                      dev->mem->nregions);
        memset(&(u->region_rb[u->region_rb_len]), '\0',
               sizeof(RAMBlock *) * (dev->mem->nregions - u->region_rb_len));
        memset(&(u->region_rb_offset[u->region_rb_len]), '\0',
               sizeof(ram_addr_t) * (dev->mem->nregions - u->region_rb_len));
        u->region_rb_len = dev->mem->nregions;
    }

    if (config_mem_slots) {
        ret = vhost_user_add_remove_regions(dev, &msg, reply_supported, true);
        if (ret < 0) {
            return ret;
        }
    } else {
        ret = vhost_user_fill_set_mem_table_msg(u, dev, &msg, fds, &fd_num,
                                                true);
        if (ret < 0) {
            return ret;
        }

        ret = vhost_user_write(dev, &msg, fds, fd_num);
        if (ret < 0) {
            return ret;
        }

        ret = vhost_user_read(dev, &msg_reply);
        if (ret < 0) {
            return ret;
        }

        if (msg_reply.hdr.request != VHOST_USER_SET_MEM_TABLE) {
            error_report("%s: Received unexpected msg type."
                         "Expected %d received %d", __func__,
                         VHOST_USER_SET_MEM_TABLE, msg_reply.hdr.request);
            return -EPROTO;
        }

        /*
         * We're using the same structure, just reusing one of the
         * fields, so it should be the same size.
         */
        if (msg_reply.hdr.size != msg.hdr.size) {
            error_report("%s: Unexpected size for postcopy reply "
                         "%d vs %d", __func__, msg_reply.hdr.size,
                         msg.hdr.size);
            return -EPROTO;
        }

        memset(u->postcopy_client_bases, 0,
               sizeof(uint64_t) * VHOST_USER_MAX_RAM_SLOTS);

        /*
         * They're in the same order as the regions that were sent
         * but some of the regions were skipped (above) if they
         * didn't have fd's
         */
        for (msg_i = 0, region_i = 0;
             region_i < dev->mem->nregions;
             region_i++) {
            if (msg_i < fd_num &&
                msg_reply.payload.memory.regions[msg_i].guest_phys_addr ==
                dev->mem->regions[region_i].guest_phys_addr) {
                u->postcopy_client_bases[region_i] =
                    msg_reply.payload.memory.regions[msg_i].userspace_addr;
                trace_vhost_user_set_mem_table_postcopy(
                    msg_reply.payload.memory.regions[msg_i].userspace_addr,
                    msg.payload.memory.regions[msg_i].userspace_addr,
                    msg_i, region_i);
                msg_i++;
            }
        }
        if (msg_i != fd_num) {
            error_report("%s: postcopy reply not fully consumed "
                         "%d vs %zd",
                         __func__, msg_i, fd_num);
            return -EIO;
        }

        /*
         * Now we've registered this with the postcopy code, we ack to the
         * client, because now we're in the position to be able to deal
         * with any faults it generates.
         */
        /* TODO: Use this for failure cases as well with a bad value. */
        msg.hdr.size = sizeof(msg.payload.u64);
        msg.payload.u64 = 0; /* OK */
        ret = vhost_user_write(dev, &msg, NULL, 0);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int vhost_user_set_mem_table(struct vhost_dev *dev,
                                    struct vhost_memory *mem)
{
    struct vhost_user *u = dev->opaque;
    int fds[VHOST_MEMORY_BASELINE_NREGIONS];
    size_t fd_num = 0;
    bool do_postcopy = u->postcopy_listen && u->postcopy_fd.handler;
    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);
    bool config_mem_slots =
        virtio_has_feature(dev->protocol_features,
                           VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS);
    int ret;

    if (do_postcopy) {
        /*
         * Postcopy has enough differences that it's best done in it's own
         * version
         */
        return vhost_user_set_mem_table_postcopy(dev, mem, reply_supported,
                                                 config_mem_slots);
    }

    VhostUserMsg msg = {
        .hdr.flags = VHOST_USER_VERSION,
    };

    if (reply_supported) {
        msg.hdr.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    if (config_mem_slots) {
        ret = vhost_user_add_remove_regions(dev, &msg, reply_supported, false);
        if (ret < 0) {
            return ret;
        }
    } else {
        ret = vhost_user_fill_set_mem_table_msg(u, dev, &msg, fds, &fd_num,
                                                false);
        if (ret < 0) {
            return ret;
        }

        ret = vhost_user_write(dev, &msg, fds, fd_num);
        if (ret < 0) {
            return ret;
        }

        if (reply_supported) {
            return process_message_reply(dev, &msg);
        }
    }

    return 0;
}

static int vhost_user_set_vring_endian(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring)
{
    bool cross_endian = virtio_has_feature(dev->protocol_features,
                                           VHOST_USER_PROTOCOL_F_CROSS_ENDIAN);
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_SET_VRING_ENDIAN,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.state = *ring,
        .hdr.size = sizeof(msg.payload.state),
    };

    if (!cross_endian) {
        error_report("vhost-user trying to send unhandled ioctl");
        return -ENOTSUP;
    }

    return vhost_user_write(dev, &msg, NULL, 0);
}

static int vhost_set_vring(struct vhost_dev *dev,
                           unsigned long int request,
                           struct vhost_vring_state *ring)
{
    VhostUserMsg msg = {
        .hdr.request = request,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.state = *ring,
        .hdr.size = sizeof(msg.payload.state),
    };

    return vhost_user_write(dev, &msg, NULL, 0);
}

static int vhost_user_set_vring_num(struct vhost_dev *dev,
                                    struct vhost_vring_state *ring)
{
    return vhost_set_vring(dev, VHOST_USER_SET_VRING_NUM, ring);
}

static void vhost_user_host_notifier_free(VhostUserHostNotifier *n)
{
    assert(n && n->unmap_addr);
    munmap(n->unmap_addr, qemu_real_host_page_size());
    n->unmap_addr = NULL;
}

/*
 * clean-up function for notifier, will finally free the structure
 * under rcu.
 */
static void vhost_user_host_notifier_remove(VhostUserHostNotifier *n,
                                            VirtIODevice *vdev)
{
    if (n->addr) {
        if (vdev) {
            virtio_queue_set_host_notifier_mr(vdev, n->idx, &n->mr, false);
        }
        assert(!n->unmap_addr);
        n->unmap_addr = n->addr;
        n->addr = NULL;
        call_rcu(n, vhost_user_host_notifier_free, rcu);
    }
}

static int vhost_user_set_vring_base(struct vhost_dev *dev,
                                     struct vhost_vring_state *ring)
{
    return vhost_set_vring(dev, VHOST_USER_SET_VRING_BASE, ring);
}

static int vhost_user_set_vring_enable(struct vhost_dev *dev, int enable)
{
    int i;

    if (!virtio_has_feature(dev->features, VHOST_USER_F_PROTOCOL_FEATURES)) {
        return -EINVAL;
    }

    for (i = 0; i < dev->nvqs; ++i) {
        int ret;
        struct vhost_vring_state state = {
            .index = dev->vq_index + i,
            .num   = enable,
        };

        ret = vhost_set_vring(dev, VHOST_USER_SET_VRING_ENABLE, &state);
        if (ret < 0) {
            /*
             * Restoring the previous state is likely infeasible, as well as
             * proceeding regardless the error, so just bail out and hope for
             * the device-level recovery.
             */
            return ret;
        }
    }

    return 0;
}

static VhostUserHostNotifier *fetch_notifier(VhostUserState *u,
                                             int idx)
{
    if (idx >= u->notifiers->len) {
        return NULL;
    }
    return g_ptr_array_index(u->notifiers, idx);
}

static int vhost_user_get_vring_base(struct vhost_dev *dev,
                                     struct vhost_vring_state *ring)
{
    int ret;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_GET_VRING_BASE,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.state = *ring,
        .hdr.size = sizeof(msg.payload.state),
    };
    struct vhost_user *u = dev->opaque;

    VhostUserHostNotifier *n = fetch_notifier(u->user, ring->index);
    if (n) {
        vhost_user_host_notifier_remove(n, dev->vdev);
    }

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        return ret;
    }

    ret = vhost_user_read(dev, &msg);
    if (ret < 0) {
        return ret;
    }

    if (msg.hdr.request != VHOST_USER_GET_VRING_BASE) {
        error_report("Received unexpected msg type. Expected %d received %d",
                     VHOST_USER_GET_VRING_BASE, msg.hdr.request);
        return -EPROTO;
    }

    if (msg.hdr.size != sizeof(msg.payload.state)) {
        error_report("Received bad msg size.");
        return -EPROTO;
    }

    *ring = msg.payload.state;

    return 0;
}

static int vhost_set_vring_file(struct vhost_dev *dev,
                                VhostUserRequest request,
                                struct vhost_vring_file *file)
{
    int fds[VHOST_USER_MAX_RAM_SLOTS];
    size_t fd_num = 0;
    VhostUserMsg msg = {
        .hdr.request = request,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.u64 = file->index & VHOST_USER_VRING_IDX_MASK,
        .hdr.size = sizeof(msg.payload.u64),
    };

    if (ioeventfd_enabled() && file->fd > 0) {
        fds[fd_num++] = file->fd;
    } else {
        msg.payload.u64 |= VHOST_USER_VRING_NOFD_MASK;
    }

    return vhost_user_write(dev, &msg, fds, fd_num);
}

static int vhost_user_set_vring_kick(struct vhost_dev *dev,
                                     struct vhost_vring_file *file)
{
    return vhost_set_vring_file(dev, VHOST_USER_SET_VRING_KICK, file);
}

static int vhost_user_set_vring_call(struct vhost_dev *dev,
                                     struct vhost_vring_file *file)
{
    return vhost_set_vring_file(dev, VHOST_USER_SET_VRING_CALL, file);
}

static int vhost_user_set_vring_err(struct vhost_dev *dev,
                                    struct vhost_vring_file *file)
{
    return vhost_set_vring_file(dev, VHOST_USER_SET_VRING_ERR, file);
}

static int vhost_user_get_u64(struct vhost_dev *dev, int request, uint64_t *u64)
{
    int ret;
    VhostUserMsg msg = {
        .hdr.request = request,
        .hdr.flags = VHOST_USER_VERSION,
    };

    if (vhost_user_one_time_request(request) && dev->vq_index != 0) {
        return 0;
    }

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        return ret;
    }

    ret = vhost_user_read(dev, &msg);
    if (ret < 0) {
        return ret;
    }

    if (msg.hdr.request != request) {
        error_report("Received unexpected msg type. Expected %d received %d",
                     request, msg.hdr.request);
        return -EPROTO;
    }

    if (msg.hdr.size != sizeof(msg.payload.u64)) {
        error_report("Received bad msg size.");
        return -EPROTO;
    }

    *u64 = msg.payload.u64;

    return 0;
}

static int vhost_user_get_features(struct vhost_dev *dev, uint64_t *features)
{
    if (vhost_user_get_u64(dev, VHOST_USER_GET_FEATURES, features) < 0) {
        return -EPROTO;
    }

    return 0;
}

static int enforce_reply(struct vhost_dev *dev,
                         const VhostUserMsg *msg)
{
    uint64_t dummy;

    if (msg->hdr.flags & VHOST_USER_NEED_REPLY_MASK) {
        return process_message_reply(dev, msg);
    }

   /*
    * We need to wait for a reply but the backend does not
    * support replies for the command we just sent.
    * Send VHOST_USER_GET_FEATURES which makes all backends
    * send a reply.
    */
    return vhost_user_get_features(dev, &dummy);
}

static int vhost_user_set_vring_addr(struct vhost_dev *dev,
                                     struct vhost_vring_addr *addr)
{
    int ret;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_SET_VRING_ADDR,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.addr = *addr,
        .hdr.size = sizeof(msg.payload.addr),
    };

    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);

    /*
     * wait for a reply if logging is enabled to make sure
     * backend is actually logging changes
     */
    bool wait_for_reply = addr->flags & (1 << VHOST_VRING_F_LOG);

    if (reply_supported && wait_for_reply) {
        msg.hdr.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        return ret;
    }

    if (wait_for_reply) {
        return enforce_reply(dev, &msg);
    }

    return 0;
}

static int vhost_user_set_u64(struct vhost_dev *dev, int request, uint64_t u64,
                              bool wait_for_reply)
{
    VhostUserMsg msg = {
        .hdr.request = request,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.u64 = u64,
        .hdr.size = sizeof(msg.payload.u64),
    };
    int ret;

    if (wait_for_reply) {
        bool reply_supported = virtio_has_feature(dev->protocol_features,
                                          VHOST_USER_PROTOCOL_F_REPLY_ACK);
        if (reply_supported) {
            msg.hdr.flags |= VHOST_USER_NEED_REPLY_MASK;
        }
    }

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        return ret;
    }

    if (wait_for_reply) {
        return enforce_reply(dev, &msg);
    }

    return 0;
}

static int vhost_user_set_status(struct vhost_dev *dev, uint8_t status)
{
    return vhost_user_set_u64(dev, VHOST_USER_SET_STATUS, status, false);
}

static int vhost_user_get_status(struct vhost_dev *dev, uint8_t *status)
{
    uint64_t value;
    int ret;

    ret = vhost_user_get_u64(dev, VHOST_USER_GET_STATUS, &value);
    if (ret < 0) {
        return ret;
    }
    *status = value;

    return 0;
}

static int vhost_user_add_status(struct vhost_dev *dev, uint8_t status)
{
    uint8_t s;
    int ret;

    ret = vhost_user_get_status(dev, &s);
    if (ret < 0) {
        return ret;
    }

    if ((s & status) == status) {
        return 0;
    }
    s |= status;

    return vhost_user_set_status(dev, s);
}

static int vhost_user_set_features(struct vhost_dev *dev,
                                   uint64_t features)
{
    /*
     * wait for a reply if logging is enabled to make sure
     * backend is actually logging changes
     */
    bool log_enabled = features & (0x1ULL << VHOST_F_LOG_ALL);
    int ret;

    /*
     * We need to include any extra backend only feature bits that
     * might be needed by our device. Currently this includes the
     * VHOST_USER_F_PROTOCOL_FEATURES bit for enabling protocol
     * features.
     */
    ret = vhost_user_set_u64(dev, VHOST_USER_SET_FEATURES,
                              features | dev->backend_features,
                              log_enabled);

    if (virtio_has_feature(dev->protocol_features,
                           VHOST_USER_PROTOCOL_F_STATUS)) {
        if (!ret) {
            return vhost_user_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
        }
    }

    return ret;
}

static int vhost_user_set_protocol_features(struct vhost_dev *dev,
                                            uint64_t features)
{
    return vhost_user_set_u64(dev, VHOST_USER_SET_PROTOCOL_FEATURES, features,
                              false);
}

static int vhost_user_set_owner(struct vhost_dev *dev)
{
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_SET_OWNER,
        .hdr.flags = VHOST_USER_VERSION,
    };

    return vhost_user_write(dev, &msg, NULL, 0);
}

static int vhost_user_get_max_memslots(struct vhost_dev *dev,
                                       uint64_t *max_memslots)
{
    uint64_t backend_max_memslots;
    int err;

    err = vhost_user_get_u64(dev, VHOST_USER_GET_MAX_MEM_SLOTS,
                             &backend_max_memslots);
    if (err < 0) {
        return err;
    }

    *max_memslots = backend_max_memslots;

    return 0;
}

static int vhost_user_reset_device(struct vhost_dev *dev)
{
    VhostUserMsg msg = {
        .hdr.flags = VHOST_USER_VERSION,
    };

    msg.hdr.request = virtio_has_feature(dev->protocol_features,
                                         VHOST_USER_PROTOCOL_F_RESET_DEVICE)
        ? VHOST_USER_RESET_DEVICE
        : VHOST_USER_RESET_OWNER;

    return vhost_user_write(dev, &msg, NULL, 0);
}

static int vhost_user_slave_handle_config_change(struct vhost_dev *dev)
{
    if (!dev->config_ops || !dev->config_ops->vhost_dev_config_notifier) {
        return -ENOSYS;
    }

    return dev->config_ops->vhost_dev_config_notifier(dev);
}

/*
 * Fetch or create the notifier for a given idx. Newly created
 * notifiers are added to the pointer array that tracks them.
 */
static VhostUserHostNotifier *fetch_or_create_notifier(VhostUserState *u,
                                                       int idx)
{
    VhostUserHostNotifier *n = NULL;
    if (idx >= u->notifiers->len) {
        g_ptr_array_set_size(u->notifiers, idx + 1);
    }

    n = g_ptr_array_index(u->notifiers, idx);
    if (!n) {
        /*
         * In case notification arrive out-of-order,
         * make room for current index.
         */
        g_ptr_array_remove_index(u->notifiers, idx);
        n = g_new0(VhostUserHostNotifier, 1);
        n->idx = idx;
        g_ptr_array_insert(u->notifiers, idx, n);
        trace_vhost_user_create_notifier(idx, n);
    }

    return n;
}

static int vhost_user_slave_handle_vring_host_notifier(struct vhost_dev *dev,
                                                       VhostUserVringArea *area,
                                                       int fd)
{
    int queue_idx = area->u64 & VHOST_USER_VRING_IDX_MASK;
    size_t page_size = qemu_real_host_page_size();
    struct vhost_user *u = dev->opaque;
    VhostUserState *user = u->user;
    VirtIODevice *vdev = dev->vdev;
    VhostUserHostNotifier *n;
    void *addr;
    char *name;

    if (!virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_HOST_NOTIFIER) ||
        vdev == NULL || queue_idx >= virtio_get_num_queues(vdev)) {
        return -EINVAL;
    }

    /*
     * Fetch notifier and invalidate any old data before setting up
     * new mapped address.
     */
    n = fetch_or_create_notifier(user, queue_idx);
    vhost_user_host_notifier_remove(n, vdev);

    if (area->u64 & VHOST_USER_VRING_NOFD_MASK) {
        return 0;
    }

    /* Sanity check. */
    if (area->size != page_size) {
        return -EINVAL;
    }

    addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                fd, area->offset);
    if (addr == MAP_FAILED) {
        return -EFAULT;
    }

    name = g_strdup_printf("vhost-user/host-notifier@%p mmaps[%d]",
                           user, queue_idx);
    if (!n->mr.ram) { /* Don't init again after suspend. */
        memory_region_init_ram_device_ptr(&n->mr, OBJECT(vdev), name,
                                          page_size, addr);
    } else {
        n->mr.ram_block->host = addr;
    }
    g_free(name);

    if (virtio_queue_set_host_notifier_mr(vdev, queue_idx, &n->mr, true)) {
        object_unparent(OBJECT(&n->mr));
        munmap(addr, page_size);
        return -ENXIO;
    }

    n->addr = addr;

    return 0;
}

static void close_slave_channel(struct vhost_user *u)
{
    g_source_destroy(u->slave_src);
    g_source_unref(u->slave_src);
    u->slave_src = NULL;
    object_unref(OBJECT(u->slave_ioc));
    u->slave_ioc = NULL;
}

static gboolean slave_read(QIOChannel *ioc, GIOCondition condition,
                           gpointer opaque)
{
    struct vhost_dev *dev = opaque;
    struct vhost_user *u = dev->opaque;
    VhostUserHeader hdr = { 0, };
    VhostUserPayload payload = { 0, };
    Error *local_err = NULL;
    gboolean rc = G_SOURCE_CONTINUE;
    int ret = 0;
    struct iovec iov;
    g_autofree int *fd = NULL;
    size_t fdsize = 0;
    int i;

    /* Read header */
    iov.iov_base = &hdr;
    iov.iov_len = VHOST_USER_HDR_SIZE;

    if (qio_channel_readv_full_all(ioc, &iov, 1, &fd, &fdsize, &local_err)) {
        error_report_err(local_err);
        goto err;
    }

    if (hdr.size > VHOST_USER_PAYLOAD_SIZE) {
        error_report("Failed to read msg header."
                " Size %d exceeds the maximum %zu.", hdr.size,
                VHOST_USER_PAYLOAD_SIZE);
        goto err;
    }

    /* Read payload */
    if (qio_channel_read_all(ioc, (char *) &payload, hdr.size, &local_err)) {
        error_report_err(local_err);
        goto err;
    }

    switch (hdr.request) {
    case VHOST_USER_SLAVE_IOTLB_MSG:
        ret = vhost_backend_handle_iotlb_msg(dev, &payload.iotlb);
        break;
    case VHOST_USER_SLAVE_CONFIG_CHANGE_MSG :
        ret = vhost_user_slave_handle_config_change(dev);
        break;
    case VHOST_USER_SLAVE_VRING_HOST_NOTIFIER_MSG:
        ret = vhost_user_slave_handle_vring_host_notifier(dev, &payload.area,
                                                          fd ? fd[0] : -1);
        break;
    default:
        error_report("Received unexpected msg type: %d.", hdr.request);
        ret = -EINVAL;
    }

    /*
     * REPLY_ACK feature handling. Other reply types has to be managed
     * directly in their request handlers.
     */
    if (hdr.flags & VHOST_USER_NEED_REPLY_MASK) {
        struct iovec iovec[2];


        hdr.flags &= ~VHOST_USER_NEED_REPLY_MASK;
        hdr.flags |= VHOST_USER_REPLY_MASK;

        payload.u64 = !!ret;
        hdr.size = sizeof(payload.u64);

        iovec[0].iov_base = &hdr;
        iovec[0].iov_len = VHOST_USER_HDR_SIZE;
        iovec[1].iov_base = &payload;
        iovec[1].iov_len = hdr.size;

        if (qio_channel_writev_all(ioc, iovec, ARRAY_SIZE(iovec), &local_err)) {
            error_report_err(local_err);
            goto err;
        }
    }

    goto fdcleanup;

err:
    close_slave_channel(u);
    rc = G_SOURCE_REMOVE;

fdcleanup:
    if (fd) {
        for (i = 0; i < fdsize; i++) {
            close(fd[i]);
        }
    }
    return rc;
}

static int vhost_setup_slave_channel(struct vhost_dev *dev)
{
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_SET_SLAVE_REQ_FD,
        .hdr.flags = VHOST_USER_VERSION,
    };
    struct vhost_user *u = dev->opaque;
    int sv[2], ret = 0;
    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);
    Error *local_err = NULL;
    QIOChannel *ioc;

    if (!virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_SLAVE_REQ)) {
        return 0;
    }

    if (qemu_socketpair(PF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        int saved_errno = errno;
        error_report("socketpair() failed");
        return -saved_errno;
    }

    ioc = QIO_CHANNEL(qio_channel_socket_new_fd(sv[0], &local_err));
    if (!ioc) {
        error_report_err(local_err);
        return -ECONNREFUSED;
    }
    u->slave_ioc = ioc;
    u->slave_src = qio_channel_add_watch_source(u->slave_ioc,
                                                G_IO_IN | G_IO_HUP,
                                                slave_read, dev, NULL, NULL);

    if (reply_supported) {
        msg.hdr.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    ret = vhost_user_write(dev, &msg, &sv[1], 1);
    if (ret) {
        goto out;
    }

    if (reply_supported) {
        ret = process_message_reply(dev, &msg);
    }

out:
    close(sv[1]);
    if (ret) {
        close_slave_channel(u);
    }

    return ret;
}

#ifdef CONFIG_LINUX
/*
 * Called back from the postcopy fault thread when a fault is received on our
 * ufd.
 * TODO: This is Linux specific
 */
static int vhost_user_postcopy_fault_handler(struct PostCopyFD *pcfd,
                                             void *ufd)
{
    struct vhost_dev *dev = pcfd->data;
    struct vhost_user *u = dev->opaque;
    struct uffd_msg *msg = ufd;
    uint64_t faultaddr = msg->arg.pagefault.address;
    RAMBlock *rb = NULL;
    uint64_t rb_offset;
    int i;

    trace_vhost_user_postcopy_fault_handler(pcfd->idstr, faultaddr,
                                            dev->mem->nregions);
    for (i = 0; i < MIN(dev->mem->nregions, u->region_rb_len); i++) {
        trace_vhost_user_postcopy_fault_handler_loop(i,
                u->postcopy_client_bases[i], dev->mem->regions[i].memory_size);
        if (faultaddr >= u->postcopy_client_bases[i]) {
            /* Ofset of the fault address in the vhost region */
            uint64_t region_offset = faultaddr - u->postcopy_client_bases[i];
            if (region_offset < dev->mem->regions[i].memory_size) {
                rb_offset = region_offset + u->region_rb_offset[i];
                trace_vhost_user_postcopy_fault_handler_found(i,
                        region_offset, rb_offset);
                rb = u->region_rb[i];
                return postcopy_request_shared_page(pcfd, rb, faultaddr,
                                                    rb_offset);
            }
        }
    }
    error_report("%s: Failed to find region for fault %" PRIx64,
                 __func__, faultaddr);
    return -1;
}

static int vhost_user_postcopy_waker(struct PostCopyFD *pcfd, RAMBlock *rb,
                                     uint64_t offset)
{
    struct vhost_dev *dev = pcfd->data;
    struct vhost_user *u = dev->opaque;
    int i;

    trace_vhost_user_postcopy_waker(qemu_ram_get_idstr(rb), offset);

    if (!u) {
        return 0;
    }
    /* Translate the offset into an address in the clients address space */
    for (i = 0; i < MIN(dev->mem->nregions, u->region_rb_len); i++) {
        if (u->region_rb[i] == rb &&
            offset >= u->region_rb_offset[i] &&
            offset < (u->region_rb_offset[i] +
                      dev->mem->regions[i].memory_size)) {
            uint64_t client_addr = (offset - u->region_rb_offset[i]) +
                                   u->postcopy_client_bases[i];
            trace_vhost_user_postcopy_waker_found(client_addr);
            return postcopy_wake_shared(pcfd, client_addr, rb);
        }
    }

    trace_vhost_user_postcopy_waker_nomatch(qemu_ram_get_idstr(rb), offset);
    return 0;
}
#endif

/*
 * Called at the start of an inbound postcopy on reception of the
 * 'advise' command.
 */
static int vhost_user_postcopy_advise(struct vhost_dev *dev, Error **errp)
{
#ifdef CONFIG_LINUX
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = u->user->chr;
    int ufd;
    int ret;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_POSTCOPY_ADVISE,
        .hdr.flags = VHOST_USER_VERSION,
    };

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        error_setg(errp, "Failed to send postcopy_advise to vhost");
        return ret;
    }

    ret = vhost_user_read(dev, &msg);
    if (ret < 0) {
        error_setg(errp, "Failed to get postcopy_advise reply from vhost");
        return ret;
    }

    if (msg.hdr.request != VHOST_USER_POSTCOPY_ADVISE) {
        error_setg(errp, "Unexpected msg type. Expected %d received %d",
                     VHOST_USER_POSTCOPY_ADVISE, msg.hdr.request);
        return -EPROTO;
    }

    if (msg.hdr.size) {
        error_setg(errp, "Received bad msg size.");
        return -EPROTO;
    }
    ufd = qemu_chr_fe_get_msgfd(chr);
    if (ufd < 0) {
        error_setg(errp, "%s: Failed to get ufd", __func__);
        return -EIO;
    }
    qemu_socket_set_nonblock(ufd);

    /* register ufd with userfault thread */
    u->postcopy_fd.fd = ufd;
    u->postcopy_fd.data = dev;
    u->postcopy_fd.handler = vhost_user_postcopy_fault_handler;
    u->postcopy_fd.waker = vhost_user_postcopy_waker;
    u->postcopy_fd.idstr = "vhost-user"; /* Need to find unique name */
    postcopy_register_shared_ufd(&u->postcopy_fd);
    return 0;
#else
    error_setg(errp, "Postcopy not supported on non-Linux systems");
    return -ENOSYS;
#endif
}

/*
 * Called at the switch to postcopy on reception of the 'listen' command.
 */
static int vhost_user_postcopy_listen(struct vhost_dev *dev, Error **errp)
{
    struct vhost_user *u = dev->opaque;
    int ret;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_POSTCOPY_LISTEN,
        .hdr.flags = VHOST_USER_VERSION | VHOST_USER_NEED_REPLY_MASK,
    };
    u->postcopy_listen = true;

    trace_vhost_user_postcopy_listen();

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        error_setg(errp, "Failed to send postcopy_listen to vhost");
        return ret;
    }

    ret = process_message_reply(dev, &msg);
    if (ret) {
        error_setg(errp, "Failed to receive reply to postcopy_listen");
        return ret;
    }

    return 0;
}

/*
 * Called at the end of postcopy
 */
static int vhost_user_postcopy_end(struct vhost_dev *dev, Error **errp)
{
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_POSTCOPY_END,
        .hdr.flags = VHOST_USER_VERSION | VHOST_USER_NEED_REPLY_MASK,
    };
    int ret;
    struct vhost_user *u = dev->opaque;

    trace_vhost_user_postcopy_end_entry();

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        error_setg(errp, "Failed to send postcopy_end to vhost");
        return ret;
    }

    ret = process_message_reply(dev, &msg);
    if (ret) {
        error_setg(errp, "Failed to receive reply to postcopy_end");
        return ret;
    }
    postcopy_unregister_shared_ufd(&u->postcopy_fd);
    close(u->postcopy_fd.fd);
    u->postcopy_fd.handler = NULL;

    trace_vhost_user_postcopy_end_exit();

    return 0;
}

static int vhost_user_postcopy_notifier(NotifierWithReturn *notifier,
                                        void *opaque)
{
    struct PostcopyNotifyData *pnd = opaque;
    struct vhost_user *u = container_of(notifier, struct vhost_user,
                                         postcopy_notifier);
    struct vhost_dev *dev = u->dev;

    switch (pnd->reason) {
    case POSTCOPY_NOTIFY_PROBE:
        if (!virtio_has_feature(dev->protocol_features,
                                VHOST_USER_PROTOCOL_F_PAGEFAULT)) {
            /* TODO: Get the device name into this error somehow */
            error_setg(pnd->errp,
                       "vhost-user backend not capable of postcopy");
            return -ENOENT;
        }
        break;

    case POSTCOPY_NOTIFY_INBOUND_ADVISE:
        return vhost_user_postcopy_advise(dev, pnd->errp);

    case POSTCOPY_NOTIFY_INBOUND_LISTEN:
        return vhost_user_postcopy_listen(dev, pnd->errp);

    case POSTCOPY_NOTIFY_INBOUND_END:
        return vhost_user_postcopy_end(dev, pnd->errp);

    default:
        /* We ignore notifications we don't know */
        break;
    }

    return 0;
}

static int vhost_user_backend_init(struct vhost_dev *dev, void *opaque,
                                   Error **errp)
{
    uint64_t features, ram_slots;
    struct vhost_user *u;
    VhostUserState *vus = (VhostUserState *) opaque;
    int err;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    u = g_new0(struct vhost_user, 1);
    u->user = vus;
    u->dev = dev;
    dev->opaque = u;

    err = vhost_user_get_features(dev, &features);
    if (err < 0) {
        error_setg_errno(errp, -err, "vhost_backend_init failed");
        return err;
    }

    if (virtio_has_feature(features, VHOST_USER_F_PROTOCOL_FEATURES)) {
        bool supports_f_config = vus->supports_config ||
            (dev->config_ops && dev->config_ops->vhost_dev_config_notifier);
        uint64_t protocol_features;

        dev->backend_features |= 1ULL << VHOST_USER_F_PROTOCOL_FEATURES;

        err = vhost_user_get_u64(dev, VHOST_USER_GET_PROTOCOL_FEATURES,
                                 &protocol_features);
        if (err < 0) {
            error_setg_errno(errp, EPROTO, "vhost_backend_init failed");
            return -EPROTO;
        }

        /*
         * We will use all the protocol features we support - although
         * we suppress F_CONFIG if we know QEMUs internal code can not support
         * it.
         */
        protocol_features &= VHOST_USER_PROTOCOL_FEATURE_MASK;

        if (supports_f_config) {
            if (!virtio_has_feature(protocol_features,
                                    VHOST_USER_PROTOCOL_F_CONFIG)) {
                error_setg(errp, "vhost-user device expecting "
                           "VHOST_USER_PROTOCOL_F_CONFIG but the vhost-user backend does "
                           "not support it.");
                return -EPROTO;
            }
        } else {
            if (virtio_has_feature(protocol_features,
                                   VHOST_USER_PROTOCOL_F_CONFIG)) {
                warn_report("vhost-user backend supports "
                            "VHOST_USER_PROTOCOL_F_CONFIG but QEMU does not.");
                protocol_features &= ~(1ULL << VHOST_USER_PROTOCOL_F_CONFIG);
            }
        }

        /* final set of protocol features */
        dev->protocol_features = protocol_features;
        err = vhost_user_set_protocol_features(dev, dev->protocol_features);
        if (err < 0) {
            error_setg_errno(errp, EPROTO, "vhost_backend_init failed");
            return -EPROTO;
        }

        /* query the max queues we support if backend supports Multiple Queue */
        if (dev->protocol_features & (1ULL << VHOST_USER_PROTOCOL_F_MQ)) {
            err = vhost_user_get_u64(dev, VHOST_USER_GET_QUEUE_NUM,
                                     &dev->max_queues);
            if (err < 0) {
                error_setg_errno(errp, EPROTO, "vhost_backend_init failed");
                return -EPROTO;
            }
        } else {
            dev->max_queues = 1;
        }

        if (dev->num_queues && dev->max_queues < dev->num_queues) {
            error_setg(errp, "The maximum number of queues supported by the "
                       "backend is %" PRIu64, dev->max_queues);
            return -EINVAL;
        }

        if (virtio_has_feature(features, VIRTIO_F_IOMMU_PLATFORM) &&
                !(virtio_has_feature(dev->protocol_features,
                    VHOST_USER_PROTOCOL_F_SLAVE_REQ) &&
                 virtio_has_feature(dev->protocol_features,
                    VHOST_USER_PROTOCOL_F_REPLY_ACK))) {
            error_setg(errp, "IOMMU support requires reply-ack and "
                       "slave-req protocol features.");
            return -EINVAL;
        }

        /* get max memory regions if backend supports configurable RAM slots */
        if (!virtio_has_feature(dev->protocol_features,
                                VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS)) {
            u->user->memory_slots = VHOST_MEMORY_BASELINE_NREGIONS;
        } else {
            err = vhost_user_get_max_memslots(dev, &ram_slots);
            if (err < 0) {
                error_setg_errno(errp, EPROTO, "vhost_backend_init failed");
                return -EPROTO;
            }

            if (ram_slots < u->user->memory_slots) {
                error_setg(errp, "The backend specified a max ram slots limit "
                           "of %" PRIu64", when the prior validated limit was "
                           "%d. This limit should never decrease.", ram_slots,
                           u->user->memory_slots);
                return -EINVAL;
            }

            u->user->memory_slots = MIN(ram_slots, VHOST_USER_MAX_RAM_SLOTS);
        }
    }

    if (dev->migration_blocker == NULL &&
        !virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_LOG_SHMFD)) {
        error_setg(&dev->migration_blocker,
                   "Migration disabled: vhost-user backend lacks "
                   "VHOST_USER_PROTOCOL_F_LOG_SHMFD feature.");
    }

    if (dev->vq_index == 0) {
        err = vhost_setup_slave_channel(dev);
        if (err < 0) {
            error_setg_errno(errp, EPROTO, "vhost_backend_init failed");
            return -EPROTO;
        }
    }

    u->postcopy_notifier.notify = vhost_user_postcopy_notifier;
    postcopy_add_notifier(&u->postcopy_notifier);

    return 0;
}

static int vhost_user_backend_cleanup(struct vhost_dev *dev)
{
    struct vhost_user *u;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    u = dev->opaque;
    if (u->postcopy_notifier.notify) {
        postcopy_remove_notifier(&u->postcopy_notifier);
        u->postcopy_notifier.notify = NULL;
    }
    u->postcopy_listen = false;
    if (u->postcopy_fd.handler) {
        postcopy_unregister_shared_ufd(&u->postcopy_fd);
        close(u->postcopy_fd.fd);
        u->postcopy_fd.handler = NULL;
    }
    if (u->slave_ioc) {
        close_slave_channel(u);
    }
    g_free(u->region_rb);
    u->region_rb = NULL;
    g_free(u->region_rb_offset);
    u->region_rb_offset = NULL;
    u->region_rb_len = 0;
    g_free(u);
    dev->opaque = 0;

    return 0;
}

static int vhost_user_get_vq_index(struct vhost_dev *dev, int idx)
{
    assert(idx >= dev->vq_index && idx < dev->vq_index + dev->nvqs);

    return idx;
}

static int vhost_user_memslots_limit(struct vhost_dev *dev)
{
    struct vhost_user *u = dev->opaque;

    return u->user->memory_slots;
}

static bool vhost_user_requires_shm_log(struct vhost_dev *dev)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    return virtio_has_feature(dev->protocol_features,
                              VHOST_USER_PROTOCOL_F_LOG_SHMFD);
}

static int vhost_user_migration_done(struct vhost_dev *dev, char* mac_addr)
{
    VhostUserMsg msg = { };

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    /* If guest supports GUEST_ANNOUNCE do nothing */
    if (virtio_has_feature(dev->acked_features, VIRTIO_NET_F_GUEST_ANNOUNCE)) {
        return 0;
    }

    /* if backend supports VHOST_USER_PROTOCOL_F_RARP ask it to send the RARP */
    if (virtio_has_feature(dev->protocol_features,
                           VHOST_USER_PROTOCOL_F_RARP)) {
        msg.hdr.request = VHOST_USER_SEND_RARP;
        msg.hdr.flags = VHOST_USER_VERSION;
        memcpy((char *)&msg.payload.u64, mac_addr, 6);
        msg.hdr.size = sizeof(msg.payload.u64);

        return vhost_user_write(dev, &msg, NULL, 0);
    }
    return -ENOTSUP;
}

static bool vhost_user_can_merge(struct vhost_dev *dev,
                                 uint64_t start1, uint64_t size1,
                                 uint64_t start2, uint64_t size2)
{
    ram_addr_t offset;
    int mfd, rfd;

    (void)vhost_user_get_mr_data(start1, &offset, &mfd);
    (void)vhost_user_get_mr_data(start2, &offset, &rfd);

    return mfd == rfd;
}

static int vhost_user_net_set_mtu(struct vhost_dev *dev, uint16_t mtu)
{
    VhostUserMsg msg;
    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);
    int ret;

    if (!(dev->protocol_features & (1ULL << VHOST_USER_PROTOCOL_F_NET_MTU))) {
        return 0;
    }

    msg.hdr.request = VHOST_USER_NET_SET_MTU;
    msg.payload.u64 = mtu;
    msg.hdr.size = sizeof(msg.payload.u64);
    msg.hdr.flags = VHOST_USER_VERSION;
    if (reply_supported) {
        msg.hdr.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        return ret;
    }

    /* If reply_ack supported, slave has to ack specified MTU is valid */
    if (reply_supported) {
        return process_message_reply(dev, &msg);
    }

    return 0;
}

static int vhost_user_send_device_iotlb_msg(struct vhost_dev *dev,
                                            struct vhost_iotlb_msg *imsg)
{
    int ret;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_IOTLB_MSG,
        .hdr.size = sizeof(msg.payload.iotlb),
        .hdr.flags = VHOST_USER_VERSION | VHOST_USER_NEED_REPLY_MASK,
        .payload.iotlb = *imsg,
    };

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        return ret;
    }

    return process_message_reply(dev, &msg);
}


static void vhost_user_set_iotlb_callback(struct vhost_dev *dev, int enabled)
{
    /* No-op as the receive channel is not dedicated to IOTLB messages. */
}

static int vhost_user_get_config(struct vhost_dev *dev, uint8_t *config,
                                 uint32_t config_len, Error **errp)
{
    int ret;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_GET_CONFIG,
        .hdr.flags = VHOST_USER_VERSION,
        .hdr.size = VHOST_USER_CONFIG_HDR_SIZE + config_len,
    };

    if (!virtio_has_feature(dev->protocol_features,
                VHOST_USER_PROTOCOL_F_CONFIG)) {
        error_setg(errp, "VHOST_USER_PROTOCOL_F_CONFIG not supported");
        return -EINVAL;
    }

    assert(config_len <= VHOST_USER_MAX_CONFIG_SIZE);

    msg.payload.config.offset = 0;
    msg.payload.config.size = config_len;
    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost_get_config failed");
        return ret;
    }

    ret = vhost_user_read(dev, &msg);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost_get_config failed");
        return ret;
    }

    if (msg.hdr.request != VHOST_USER_GET_CONFIG) {
        error_setg(errp,
                   "Received unexpected msg type. Expected %d received %d",
                   VHOST_USER_GET_CONFIG, msg.hdr.request);
        return -EPROTO;
    }

    if (msg.hdr.size != VHOST_USER_CONFIG_HDR_SIZE + config_len) {
        error_setg(errp, "Received bad msg size.");
        return -EPROTO;
    }

    memcpy(config, msg.payload.config.region, config_len);

    return 0;
}

static int vhost_user_set_config(struct vhost_dev *dev, const uint8_t *data,
                                 uint32_t offset, uint32_t size, uint32_t flags)
{
    int ret;
    uint8_t *p;
    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);

    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_SET_CONFIG,
        .hdr.flags = VHOST_USER_VERSION,
        .hdr.size = VHOST_USER_CONFIG_HDR_SIZE + size,
    };

    if (!virtio_has_feature(dev->protocol_features,
                VHOST_USER_PROTOCOL_F_CONFIG)) {
        return -ENOTSUP;
    }

    if (reply_supported) {
        msg.hdr.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    if (size > VHOST_USER_MAX_CONFIG_SIZE) {
        return -EINVAL;
    }

    msg.payload.config.offset = offset,
    msg.payload.config.size = size,
    msg.payload.config.flags = flags,
    p = msg.payload.config.region;
    memcpy(p, data, size);

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        return ret;
    }

    if (reply_supported) {
        return process_message_reply(dev, &msg);
    }

    return 0;
}

static int vhost_user_crypto_create_session(struct vhost_dev *dev,
                                            void *session_info,
                                            uint64_t *session_id)
{
    int ret;
    bool crypto_session = virtio_has_feature(dev->protocol_features,
                                       VHOST_USER_PROTOCOL_F_CRYPTO_SESSION);
    CryptoDevBackendSymSessionInfo *sess_info = session_info;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_CREATE_CRYPTO_SESSION,
        .hdr.flags = VHOST_USER_VERSION,
        .hdr.size = sizeof(msg.payload.session),
    };

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    if (!crypto_session) {
        error_report("vhost-user trying to send unhandled ioctl");
        return -ENOTSUP;
    }

    memcpy(&msg.payload.session.session_setup_data, sess_info,
              sizeof(CryptoDevBackendSymSessionInfo));
    if (sess_info->key_len) {
        memcpy(&msg.payload.session.key, sess_info->cipher_key,
               sess_info->key_len);
    }
    if (sess_info->auth_key_len > 0) {
        memcpy(&msg.payload.session.auth_key, sess_info->auth_key,
               sess_info->auth_key_len);
    }
    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        error_report("vhost_user_write() return %d, create session failed",
                     ret);
        return ret;
    }

    ret = vhost_user_read(dev, &msg);
    if (ret < 0) {
        error_report("vhost_user_read() return %d, create session failed",
                     ret);
        return ret;
    }

    if (msg.hdr.request != VHOST_USER_CREATE_CRYPTO_SESSION) {
        error_report("Received unexpected msg type. Expected %d received %d",
                     VHOST_USER_CREATE_CRYPTO_SESSION, msg.hdr.request);
        return -EPROTO;
    }

    if (msg.hdr.size != sizeof(msg.payload.session)) {
        error_report("Received bad msg size.");
        return -EPROTO;
    }

    if (msg.payload.session.session_id < 0) {
        error_report("Bad session id: %" PRId64 "",
                              msg.payload.session.session_id);
        return -EINVAL;
    }
    *session_id = msg.payload.session.session_id;

    return 0;
}

static int
vhost_user_crypto_close_session(struct vhost_dev *dev, uint64_t session_id)
{
    int ret;
    bool crypto_session = virtio_has_feature(dev->protocol_features,
                                       VHOST_USER_PROTOCOL_F_CRYPTO_SESSION);
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_CLOSE_CRYPTO_SESSION,
        .hdr.flags = VHOST_USER_VERSION,
        .hdr.size = sizeof(msg.payload.u64),
    };
    msg.payload.u64 = session_id;

    if (!crypto_session) {
        error_report("vhost-user trying to send unhandled ioctl");
        return -ENOTSUP;
    }

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        error_report("vhost_user_write() return %d, close session failed",
                     ret);
        return ret;
    }

    return 0;
}

static bool vhost_user_mem_section_filter(struct vhost_dev *dev,
                                          MemoryRegionSection *section)
{
    bool result;

    result = memory_region_get_fd(section->mr) >= 0;

    return result;
}

static int vhost_user_get_inflight_fd(struct vhost_dev *dev,
                                      uint16_t queue_size,
                                      struct vhost_inflight *inflight)
{
    void *addr;
    int fd;
    int ret;
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = u->user->chr;
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_GET_INFLIGHT_FD,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.inflight.num_queues = dev->nvqs,
        .payload.inflight.queue_size = queue_size,
        .hdr.size = sizeof(msg.payload.inflight),
    };

    if (!virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD)) {
        return 0;
    }

    ret = vhost_user_write(dev, &msg, NULL, 0);
    if (ret < 0) {
        return ret;
    }

    ret = vhost_user_read(dev, &msg);
    if (ret < 0) {
        return ret;
    }

    if (msg.hdr.request != VHOST_USER_GET_INFLIGHT_FD) {
        error_report("Received unexpected msg type. "
                     "Expected %d received %d",
                     VHOST_USER_GET_INFLIGHT_FD, msg.hdr.request);
        return -EPROTO;
    }

    if (msg.hdr.size != sizeof(msg.payload.inflight)) {
        error_report("Received bad msg size.");
        return -EPROTO;
    }

    if (!msg.payload.inflight.mmap_size) {
        return 0;
    }

    fd = qemu_chr_fe_get_msgfd(chr);
    if (fd < 0) {
        error_report("Failed to get mem fd");
        return -EIO;
    }

    addr = mmap(0, msg.payload.inflight.mmap_size, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, msg.payload.inflight.mmap_offset);

    if (addr == MAP_FAILED) {
        error_report("Failed to mmap mem fd");
        close(fd);
        return -EFAULT;
    }

    inflight->addr = addr;
    inflight->fd = fd;
    inflight->size = msg.payload.inflight.mmap_size;
    inflight->offset = msg.payload.inflight.mmap_offset;
    inflight->queue_size = queue_size;

    return 0;
}

static int vhost_user_set_inflight_fd(struct vhost_dev *dev,
                                      struct vhost_inflight *inflight)
{
    VhostUserMsg msg = {
        .hdr.request = VHOST_USER_SET_INFLIGHT_FD,
        .hdr.flags = VHOST_USER_VERSION,
        .payload.inflight.mmap_size = inflight->size,
        .payload.inflight.mmap_offset = inflight->offset,
        .payload.inflight.num_queues = dev->nvqs,
        .payload.inflight.queue_size = inflight->queue_size,
        .hdr.size = sizeof(msg.payload.inflight),
    };

    if (!virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD)) {
        return 0;
    }

    return vhost_user_write(dev, &msg, &inflight->fd, 1);
}

static void vhost_user_state_destroy(gpointer data)
{
    VhostUserHostNotifier *n = (VhostUserHostNotifier *) data;
    if (n) {
        vhost_user_host_notifier_remove(n, NULL);
        object_unparent(OBJECT(&n->mr));
        /*
         * We can't free until vhost_user_host_notifier_remove has
         * done it's thing so schedule the free with RCU.
         */
        g_free_rcu(n, rcu);
    }
}

bool vhost_user_init(VhostUserState *user, CharBackend *chr, Error **errp)
{
    if (user->chr) {
        error_setg(errp, "Cannot initialize vhost-user state");
        return false;
    }
    user->chr = chr;
    user->memory_slots = 0;
    user->notifiers = g_ptr_array_new_full(VIRTIO_QUEUE_MAX / 4,
                                           &vhost_user_state_destroy);
    return true;
}

void vhost_user_cleanup(VhostUserState *user)
{
    if (!user->chr) {
        return;
    }
    memory_region_transaction_begin();
    user->notifiers = (GPtrArray *) g_ptr_array_free(user->notifiers, true);
    memory_region_transaction_commit();
    user->chr = NULL;
}


typedef struct {
    vu_async_close_fn cb;
    DeviceState *dev;
    CharBackend *cd;
    struct vhost_dev *vhost;
} VhostAsyncCallback;

static void vhost_user_async_close_bh(void *opaque)
{
    VhostAsyncCallback *data = opaque;
    struct vhost_dev *vhost = data->vhost;

    /*
     * If the vhost_dev has been cleared in the meantime there is
     * nothing left to do as some other path has completed the
     * cleanup.
     */
    if (vhost->vdev) {
        data->cb(data->dev);
    }

    g_free(data);
}

/*
 * We only schedule the work if the machine is running. If suspended
 * we want to keep all the in-flight data as is for migration
 * purposes.
 */
void vhost_user_async_close(DeviceState *d,
                            CharBackend *chardev, struct vhost_dev *vhost,
                            vu_async_close_fn cb)
{
    if (!runstate_check(RUN_STATE_SHUTDOWN)) {
        /*
         * A close event may happen during a read/write, but vhost
         * code assumes the vhost_dev remains setup, so delay the
         * stop & clear.
         */
        AioContext *ctx = qemu_get_current_aio_context();
        VhostAsyncCallback *data = g_new0(VhostAsyncCallback, 1);

        /* Save data for the callback */
        data->cb = cb;
        data->dev = d;
        data->cd = chardev;
        data->vhost = vhost;

        /* Disable any further notifications on the chardev */
        qemu_chr_fe_set_handlers(chardev,
                                 NULL, NULL, NULL, NULL, NULL, NULL,
                                 false);

        aio_bh_schedule_oneshot(ctx, vhost_user_async_close_bh, data);

        /*
         * Move vhost device to the stopped state. The vhost-user device
         * will be clean up and disconnected in BH. This can be useful in
         * the vhost migration code. If disconnect was caught there is an
         * option for the general vhost code to get the dev state without
         * knowing its type (in this case vhost-user).
         *
         * Note if the vhost device is fully cleared by the time we
         * execute the bottom half we won't continue with the cleanup.
         */
        vhost->started = false;
    }
}

static int vhost_user_dev_start(struct vhost_dev *dev, bool started)
{
    if (!virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_STATUS)) {
        return 0;
    }

    /* Set device status only for last queue pair */
    if (dev->vq_index + dev->nvqs != dev->vq_index_end) {
        return 0;
    }

    if (started) {
        return vhost_user_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                                          VIRTIO_CONFIG_S_DRIVER |
                                          VIRTIO_CONFIG_S_DRIVER_OK);
    } else {
        return vhost_user_set_status(dev, 0);
    }
}

const VhostOps user_ops = {
        .backend_type = VHOST_BACKEND_TYPE_USER,
        .vhost_backend_init = vhost_user_backend_init,
        .vhost_backend_cleanup = vhost_user_backend_cleanup,
        .vhost_backend_memslots_limit = vhost_user_memslots_limit,
        .vhost_set_log_base = vhost_user_set_log_base,
        .vhost_set_mem_table = vhost_user_set_mem_table,
        .vhost_set_vring_addr = vhost_user_set_vring_addr,
        .vhost_set_vring_endian = vhost_user_set_vring_endian,
        .vhost_set_vring_num = vhost_user_set_vring_num,
        .vhost_set_vring_base = vhost_user_set_vring_base,
        .vhost_get_vring_base = vhost_user_get_vring_base,
        .vhost_set_vring_kick = vhost_user_set_vring_kick,
        .vhost_set_vring_call = vhost_user_set_vring_call,
        .vhost_set_vring_err = vhost_user_set_vring_err,
        .vhost_set_features = vhost_user_set_features,
        .vhost_get_features = vhost_user_get_features,
        .vhost_set_owner = vhost_user_set_owner,
        .vhost_reset_device = vhost_user_reset_device,
        .vhost_get_vq_index = vhost_user_get_vq_index,
        .vhost_set_vring_enable = vhost_user_set_vring_enable,
        .vhost_requires_shm_log = vhost_user_requires_shm_log,
        .vhost_migration_done = vhost_user_migration_done,
        .vhost_backend_can_merge = vhost_user_can_merge,
        .vhost_net_set_mtu = vhost_user_net_set_mtu,
        .vhost_set_iotlb_callback = vhost_user_set_iotlb_callback,
        .vhost_send_device_iotlb_msg = vhost_user_send_device_iotlb_msg,
        .vhost_get_config = vhost_user_get_config,
        .vhost_set_config = vhost_user_set_config,
        .vhost_crypto_create_session = vhost_user_crypto_create_session,
        .vhost_crypto_close_session = vhost_user_crypto_close_session,
        .vhost_backend_mem_section_filter = vhost_user_mem_section_filter,
        .vhost_get_inflight_fd = vhost_user_get_inflight_fd,
        .vhost_set_inflight_fd = vhost_user_set_inflight_fd,
        .vhost_dev_start = vhost_user_dev_start,
};
