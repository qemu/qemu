/*
 * Vhost User library
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Authors:
 *  Victor Kaplansky <victork@redhat.com>
 *  Marc-André Lureau <mlureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef LIBVHOST_USER_H
#define LIBVHOST_USER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <poll.h>
#include <linux/vhost.h>
#include <pthread.h>
#include "standard-headers/linux/virtio_ring.h"

/* Based on qemu/hw/virtio/vhost-user.c */
#define VHOST_USER_F_PROTOCOL_FEATURES 30
#define VHOST_LOG_PAGE 4096

#define VIRTQUEUE_MAX_SIZE 1024

#define VHOST_MEMORY_BASELINE_NREGIONS 8

/*
 * vhost in the kernel usually supports 509 mem slots. 509 used to be the
 * KVM limit, it supported 512, but 3 were used for internal purposes. This
 * limit is sufficient to support many DIMMs and virtio-mem in
 * "dynamic-memslots" mode.
 */
#define VHOST_USER_MAX_RAM_SLOTS 509

#define VHOST_USER_HDR_SIZE offsetof(VhostUserMsg, payload.u64)

typedef enum VhostSetConfigType {
    VHOST_SET_CONFIG_TYPE_FRONTEND = 0,
    VHOST_SET_CONFIG_TYPE_MIGRATION = 1,
} VhostSetConfigType;

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
    VHOST_USER_PROTOCOL_F_BACKEND_REQ = 5,
    VHOST_USER_PROTOCOL_F_CROSS_ENDIAN = 6,
    VHOST_USER_PROTOCOL_F_CRYPTO_SESSION = 7,
    VHOST_USER_PROTOCOL_F_PAGEFAULT = 8,
    VHOST_USER_PROTOCOL_F_CONFIG = 9,
    VHOST_USER_PROTOCOL_F_BACKEND_SEND_FD = 10,
    VHOST_USER_PROTOCOL_F_HOST_NOTIFIER = 11,
    VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD = 12,
    VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS = 14,
    VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS = 15,
    /* Feature 16 is reserved for VHOST_USER_PROTOCOL_F_STATUS. */
    /* Feature 17 reserved for VHOST_USER_PROTOCOL_F_XEN_MMAP. */
    VHOST_USER_PROTOCOL_F_SHARED_OBJECT = 18,
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
    VHOST_USER_SET_BACKEND_REQ_FD = 21,
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
    VHOST_USER_VRING_KICK = 35,
    VHOST_USER_GET_MAX_MEM_SLOTS = 36,
    VHOST_USER_ADD_MEM_REG = 37,
    VHOST_USER_REM_MEM_REG = 38,
    VHOST_USER_GET_SHARED_OBJECT = 41,
    VHOST_USER_MAX
} VhostUserRequest;

typedef enum VhostUserBackendRequest {
    VHOST_USER_BACKEND_NONE = 0,
    VHOST_USER_BACKEND_IOTLB_MSG = 1,
    VHOST_USER_BACKEND_CONFIG_CHANGE_MSG = 2,
    VHOST_USER_BACKEND_VRING_HOST_NOTIFIER_MSG = 3,
    VHOST_USER_BACKEND_VRING_CALL = 4,
    VHOST_USER_BACKEND_VRING_ERR = 5,
    VHOST_USER_BACKEND_SHARED_OBJECT_ADD = 6,
    VHOST_USER_BACKEND_SHARED_OBJECT_REMOVE = 7,
    VHOST_USER_BACKEND_SHARED_OBJECT_LOOKUP = 8,
    VHOST_USER_BACKEND_MAX
}  VhostUserBackendRequest;

typedef struct VhostUserMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    uint64_t mmap_offset;
} VhostUserMemoryRegion;

#define VHOST_USER_MEM_REG_SIZE (sizeof(VhostUserMemoryRegion))

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

#define UUID_LEN 16

typedef struct VhostUserShared {
    unsigned char uuid[UUID_LEN];
} VhostUserShared;

#if defined(_WIN32) && (defined(__x86_64__) || defined(__i386__))
# define VU_PACKED __attribute__((gcc_struct, packed))
#else
# define VU_PACKED __attribute__((packed))
#endif

typedef struct VhostUserMsg {
    int request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1 << 2)
#define VHOST_USER_NEED_REPLY_MASK  (0x1 << 3)
    uint32_t flags;
    uint32_t size; /* the following payload size */

    union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1 << 8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserMemRegMsg memreg;
        VhostUserLog log;
        VhostUserConfig config;
        VhostUserVringArea area;
        VhostUserInflight inflight;
        VhostUserShared object;
    } payload;

    int fds[VHOST_MEMORY_BASELINE_NREGIONS];
    int fd_num;
    uint8_t *data;
} VU_PACKED VhostUserMsg;

typedef struct VuDevRegion {
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
} VuDevRegion;

typedef struct VuDev VuDev;

typedef uint64_t (*vu_get_features_cb) (VuDev *dev);
typedef void (*vu_set_features_cb) (VuDev *dev, uint64_t features);
typedef int (*vu_process_msg_cb) (VuDev *dev, VhostUserMsg *vmsg,
                                  int *do_reply);
typedef bool (*vu_read_msg_cb) (VuDev *dev, int sock, VhostUserMsg *vmsg);
typedef void (*vu_queue_set_started_cb) (VuDev *dev, int qidx, bool started);
typedef bool (*vu_queue_is_processed_in_order_cb) (VuDev *dev, int qidx);
typedef int (*vu_get_config_cb) (VuDev *dev, uint8_t *config, uint32_t len);
typedef int (*vu_set_config_cb) (VuDev *dev, const uint8_t *data,
                                 uint32_t offset, uint32_t size,
                                 uint32_t flags);
typedef int (*vu_get_shared_object_cb) (VuDev *dev, const unsigned char *uuid);

typedef struct VuDevIface {
    /* called by VHOST_USER_GET_FEATURES to get the features bitmask */
    vu_get_features_cb get_features;
    /* enable vhost implementation features */
    vu_set_features_cb set_features;
    /* get the protocol feature bitmask from the underlying vhost
     * implementation */
    vu_get_features_cb get_protocol_features;
    /* enable protocol features in the underlying vhost implementation. */
    vu_set_features_cb set_protocol_features;
    /* process_msg is called for each vhost-user message received */
    /* skip libvhost-user processing if return value != 0 */
    vu_process_msg_cb process_msg;
    /* tells when queues can be processed */
    vu_queue_set_started_cb queue_set_started;
    /*
     * If the queue is processed in order, in which case it will be
     * resumed to vring.used->idx. This can help to support resuming
     * on unmanaged exit/crash.
     */
    vu_queue_is_processed_in_order_cb queue_is_processed_in_order;
    /* get the config space of the device */
    vu_get_config_cb get_config;
    /* set the config space of the device */
    vu_set_config_cb set_config;
    /* get virtio shared object from the underlying vhost implementation. */
    vu_get_shared_object_cb get_shared_object;
} VuDevIface;

typedef void (*vu_queue_handler_cb) (VuDev *dev, int qidx);

typedef struct VuRing {
    unsigned int num;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    uint64_t log_guest_addr;
    uint32_t flags;
} VuRing;

typedef struct VuDescStateSplit {
    /* Indicate whether this descriptor is inflight or not.
     * Only available for head-descriptor. */
    uint8_t inflight;

    /* Padding */
    uint8_t padding[5];

    /* Maintain a list for the last batch of used descriptors.
     * Only available when batching is used for submitting */
    uint16_t next;

    /* Used to preserve the order of fetching available descriptors.
     * Only available for head-descriptor. */
    uint64_t counter;
} VuDescStateSplit;

typedef struct VuVirtqInflight {
    /* The feature flags of this region. Now it's initialized to 0. */
    uint64_t features;

    /* The version of this region. It's 1 currently.
     * Zero value indicates a vm reset happened. */
    uint16_t version;

    /*
     * The size of VuDescStateSplit array. It's equal to the virtqueue size.
     * Backend could get it from queue size field of VhostUserInflight.
     */
    uint16_t desc_num;

    /* The head of list that track the last batch of used descriptors. */
    uint16_t last_batch_head;

    /* Storing the idx value of used ring */
    uint16_t used_idx;

    /* Used to track the state of each descriptor in descriptor table */
    VuDescStateSplit desc[];
} VuVirtqInflight;

typedef struct VuVirtqInflightDesc {
    uint16_t index;
    uint64_t counter;
} VuVirtqInflightDesc;

typedef struct VuVirtq {
    VuRing vring;

    VuVirtqInflight *inflight;

    VuVirtqInflightDesc *resubmit_list;

    uint16_t resubmit_num;

    uint64_t counter;

    /* Next head to pop */
    uint16_t last_avail_idx;

    /* Last avail_idx read from VQ. */
    uint16_t shadow_avail_idx;

    uint16_t used_idx;

    /* Last used index value we have signalled on */
    uint16_t signalled_used;

    /* Last used index value we have signalled on */
    bool signalled_used_valid;

    /* Notification enabled? */
    bool notification;

    unsigned int inuse;

    vu_queue_handler_cb handler;

    int call_fd;
    int kick_fd;
    int err_fd;
    unsigned int enable;
    bool started;

    /* Guest addresses of our ring */
    struct vhost_vring_addr vra;
} VuVirtq;

enum VuWatchCondtion {
    VU_WATCH_IN = POLLIN,
    VU_WATCH_OUT = POLLOUT,
    VU_WATCH_PRI = POLLPRI,
    VU_WATCH_ERR = POLLERR,
    VU_WATCH_HUP = POLLHUP,
};

typedef void (*vu_panic_cb) (VuDev *dev, const char *err);
typedef void (*vu_watch_cb) (VuDev *dev, int condition, void *data);
typedef void (*vu_set_watch_cb) (VuDev *dev, int fd, int condition,
                                 vu_watch_cb cb, void *data);
typedef void (*vu_remove_watch_cb) (VuDev *dev, int fd);

typedef struct VuDevInflightInfo {
    int fd;
    void *addr;
    uint64_t size;
} VuDevInflightInfo;

struct VuDev {
    int sock;
    uint32_t nregions;
    VuDevRegion *regions;
    VuVirtq *vq;
    VuDevInflightInfo inflight_info;
    int log_call_fd;
    /* Must be held while using backend_fd */
    pthread_mutex_t backend_mutex;
    int backend_fd;
    uint64_t log_size;
    uint8_t *log_table;
    uint64_t features;
    uint64_t protocol_features;
    bool broken;
    uint16_t max_queues;

    /*
     * @read_msg: custom method to read vhost-user message
     *
     * Read data from vhost_user socket fd and fill up
     * the passed VhostUserMsg *vmsg struct.
     *
     * If reading fails, it should close the received set of file
     * descriptors as socket message's auxiliary data.
     *
     * For the details, please refer to vu_message_read in libvhost-user.c
     * which will be used by default if not custom method is provided when
     * calling vu_init
     *
     * Returns: true if vhost-user message successfully received,
     *          otherwise return false.
     *
     */
    vu_read_msg_cb read_msg;

    /*
     * @set_watch: add or update the given fd to the watch set,
     * call cb when condition is met.
     */
    vu_set_watch_cb set_watch;

    /* @remove_watch: remove the given fd from the watch set */
    vu_remove_watch_cb remove_watch;

    /*
     * @panic: encountered an unrecoverable error, you may try to re-initialize
     */
    vu_panic_cb panic;
    const VuDevIface *iface;

    /* Postcopy data */
    int postcopy_ufd;
    bool postcopy_listening;
};

typedef struct VuVirtqElement {
    unsigned int index;
    unsigned int out_num;
    unsigned int in_num;
    struct iovec *in_sg;
    struct iovec *out_sg;
} VuVirtqElement;

/**
 * vu_init:
 * @dev: a VuDev context
 * @max_queues: maximum number of virtqueues
 * @socket: the socket connected to vhost-user frontend
 * @panic: a panic callback
 * @set_watch: a set_watch callback
 * @remove_watch: a remove_watch callback
 * @iface: a VuDevIface structure with vhost-user device callbacks
 *
 * Initializes a VuDev vhost-user context.
 *
 * Returns: true on success, false on failure.
 **/
bool vu_init(VuDev *dev,
             uint16_t max_queues,
             int socket,
             vu_panic_cb panic,
             vu_read_msg_cb read_msg,
             vu_set_watch_cb set_watch,
             vu_remove_watch_cb remove_watch,
             const VuDevIface *iface);


/**
 * vu_deinit:
 * @dev: a VuDev context
 *
 * Cleans up the VuDev context
 */
void vu_deinit(VuDev *dev);


/**
 * vu_request_to_string: return string for vhost message request
 * @req: VhostUserMsg request
 *
 * Returns a const string, do not free.
 */
const char *vu_request_to_string(unsigned int req);

/**
 * vu_dispatch:
 * @dev: a VuDev context
 *
 * Process one vhost-user message.
 *
 * Returns: TRUE on success, FALSE on failure.
 */
bool vu_dispatch(VuDev *dev);

/**
 * vu_gpa_to_va:
 * @dev: a VuDev context
 * @plen: guest memory size
 * @guest_addr: guest address
 *
 * Translate a guest address to a pointer. Returns NULL on failure.
 */
void *vu_gpa_to_va(VuDev *dev, uint64_t *plen, uint64_t guest_addr);

/**
 * vu_get_queue:
 * @dev: a VuDev context
 * @qidx: queue index
 *
 * Returns the queue number @qidx.
 */
VuVirtq *vu_get_queue(VuDev *dev, int qidx);

/**
 * vu_set_queue_handler:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @handler: the queue handler callback
 *
 * Set the queue handler. This function may be called several times
 * for the same queue. If called with NULL @handler, the handler is
 * removed.
 */
void vu_set_queue_handler(VuDev *dev, VuVirtq *vq,
                          vu_queue_handler_cb handler);

/**
 * vu_set_queue_host_notifier:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @fd: a file descriptor
 * @size: host page size
 * @offset: notifier offset in @fd file
 *
 * Set queue's host notifier. This function may be called several
 * times for the same queue. If called with -1 @fd, the notifier
 * is removed.
 */
bool vu_set_queue_host_notifier(VuDev *dev, VuVirtq *vq, int fd,
                                int size, int offset);

/**
 * vu_lookup_shared_object:
 * @dev: a VuDev context
 * @uuid: UUID of the shared object
 * @dmabuf_fd: output dma-buf file descriptor
 *
 * Lookup for a virtio shared object (i.e., dma-buf fd) associated with the
 * received UUID. Result, if found, is stored in the dmabuf_fd argument.
 *
 * Returns: whether the virtio object was found.
 */
bool vu_lookup_shared_object(VuDev *dev, unsigned char uuid[UUID_LEN],
                             int *dmabuf_fd);

/**
 * vu_add_shared_object:
 * @dev: a VuDev context
 * @uuid: UUID of the shared object
 *
 * Registers this back-end as the exporter for the object associated with
 * the received UUID.
 *
 * Returns: TRUE on success, FALSE on failure.
 */
bool vu_add_shared_object(VuDev *dev, unsigned char uuid[UUID_LEN]);

/**
 * vu_rm_shared_object:
 * @dev: a VuDev context
 * @uuid: UUID of the shared object
 *
 * Removes a shared object entry (i.e., back-end entry) associated with the
 * received UUID key from the hash table.
 *
 * Returns: TRUE on success, FALSE on failure.
 */
bool vu_rm_shared_object(VuDev *dev, unsigned char uuid[UUID_LEN]);

/**
 * vu_queue_set_notification:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @enable: state
 *
 * Set whether the queue notifies (via event index or interrupt)
 */
void vu_queue_set_notification(VuDev *dev, VuVirtq *vq, int enable);

/**
 * vu_queue_enabled:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 *
 * Returns: whether the queue is enabled.
 */
bool vu_queue_enabled(VuDev *dev, VuVirtq *vq);

/**
 * vu_queue_started:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 *
 * Returns: whether the queue is started.
 */
bool vu_queue_started(const VuDev *dev, const VuVirtq *vq);

/**
 * vu_queue_empty:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 *
 * Returns: true if the queue is empty or not ready.
 */
bool vu_queue_empty(VuDev *dev, VuVirtq *vq);

/**
 * vu_queue_notify:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 *
 * Request to notify the queue via callfd (skipped if unnecessary)
 */
void vu_queue_notify(VuDev *dev, VuVirtq *vq);

void vu_config_change_msg(VuDev *dev);

/**
 * vu_queue_notify_sync:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 *
 * Request to notify the queue via callfd (skipped if unnecessary)
 * or sync message if possible.
 */
void vu_queue_notify_sync(VuDev *dev, VuVirtq *vq);

/**
 * vu_queue_pop:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @sz: the size of struct to return (must be >= VuVirtqElement)
 *
 * Returns: a VuVirtqElement filled from the queue or NULL. The
 * returned element must be free()-d by the caller.
 */
void *vu_queue_pop(VuDev *dev, VuVirtq *vq, size_t sz);


/**
 * vu_queue_unpop:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @elem: The #VuVirtqElement
 * @len: number of bytes written
 *
 * Pretend the most recent element wasn't popped from the virtqueue.  The next
 * call to vu_queue_pop() will refetch the element.
 */
void vu_queue_unpop(VuDev *dev, VuVirtq *vq, VuVirtqElement *elem,
                    size_t len);

/**
 * vu_queue_rewind:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @num: number of elements to push back
 *
 * Pretend that elements weren't popped from the virtqueue.  The next
 * virtqueue_pop() will refetch the oldest element.
 *
 * Returns: true on success, false if @num is greater than the number of in use
 * elements.
 */
bool vu_queue_rewind(VuDev *dev, VuVirtq *vq, unsigned int num);

/**
 * vu_queue_fill:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @elem: a VuVirtqElement
 * @len: length in bytes to write
 * @idx: optional offset for the used ring index (0 in general)
 *
 * Fill the used ring with @elem element.
 */
void vu_queue_fill(VuDev *dev, VuVirtq *vq,
                   const VuVirtqElement *elem,
                   unsigned int len, unsigned int idx);

/**
 * vu_queue_push:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @elem: a VuVirtqElement
 * @len: length in bytes to write
 *
 * Helper that combines vu_queue_fill() with a vu_queue_flush().
 */
void vu_queue_push(VuDev *dev, VuVirtq *vq,
                   const VuVirtqElement *elem, unsigned int len);

/**
 * vu_queue_flush:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @num: number of elements to flush
 *
 * Mark the last number of elements as done (used.idx is updated by
 * num elements).
*/
void vu_queue_flush(VuDev *dev, VuVirtq *vq, unsigned int num);

/**
 * vu_queue_get_avail_bytes:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @in_bytes: in bytes
 * @out_bytes: out bytes
 * @max_in_bytes: stop counting after max_in_bytes
 * @max_out_bytes: stop counting after max_out_bytes
 *
 * Count the number of available bytes, up to max_in_bytes/max_out_bytes.
 */
void vu_queue_get_avail_bytes(VuDev *vdev, VuVirtq *vq, unsigned int *in_bytes,
                              unsigned int *out_bytes,
                              unsigned max_in_bytes, unsigned max_out_bytes);

/**
 * vu_queue_avail_bytes:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @in_bytes: expected in bytes
 * @out_bytes: expected out bytes
 *
 * Returns: true if in_bytes <= in_total && out_bytes <= out_total
 */
bool vu_queue_avail_bytes(VuDev *dev, VuVirtq *vq, unsigned int in_bytes,
                          unsigned int out_bytes);

#endif /* LIBVHOST_USER_H */
