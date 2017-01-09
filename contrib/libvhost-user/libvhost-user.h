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
#include <linux/vhost.h>
#include "standard-headers/linux/virtio_ring.h"

/* Based on qemu/hw/virtio/vhost-user.c */
#define VHOST_USER_F_PROTOCOL_FEATURES 30
#define VHOST_LOG_PAGE 4096

#define VHOST_MAX_NR_VIRTQUEUE 8
#define VIRTQUEUE_MAX_SIZE 1024

#define VHOST_MEMORY_MAX_NREGIONS 8

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
    VHOST_USER_INPUT_GET_CONFIG = 20,
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

#if defined(_WIN32)
# define VU_PACKED __attribute__((gcc_struct, packed))
#else
# define VU_PACKED __attribute__((packed))
#endif

typedef struct VhostUserMsg {
    VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1 << 2)
    uint32_t flags;
    uint32_t size; /* the following payload size */

    union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1 << 8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserLog log;
    } payload;

    int fds[VHOST_MEMORY_MAX_NREGIONS];
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
typedef void (*vu_queue_set_started_cb) (VuDev *dev, int qidx, bool started);

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

typedef struct VuVirtq {
    VuRing vring;

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

    int inuse;

    vu_queue_handler_cb handler;

    int call_fd;
    int kick_fd;
    int err_fd;
    unsigned int enable;
    bool started;
} VuVirtq;

enum VuWatchCondtion {
    VU_WATCH_IN = 1 << 0,
    VU_WATCH_OUT = 1 << 1,
    VU_WATCH_PRI = 1 << 2,
    VU_WATCH_ERR = 1 << 3,
    VU_WATCH_HUP = 1 << 4,
};

typedef void (*vu_panic_cb) (VuDev *dev, const char *err);
typedef void (*vu_watch_cb) (VuDev *dev, int condition, void *data);
typedef void (*vu_set_watch_cb) (VuDev *dev, int fd, int condition,
                                 vu_watch_cb cb, void *data);
typedef void (*vu_remove_watch_cb) (VuDev *dev, int fd);

struct VuDev {
    int sock;
    uint32_t nregions;
    VuDevRegion regions[VHOST_MEMORY_MAX_NREGIONS];
    VuVirtq vq[VHOST_MAX_NR_VIRTQUEUE];
    int log_call_fd;
    uint64_t log_size;
    uint8_t *log_table;
    uint64_t features;
    uint64_t protocol_features;
    bool broken;

    /* @set_watch: add or update the given fd to the watch set,
     * call cb when condition is met */
    vu_set_watch_cb set_watch;

    /* @remove_watch: remove the given fd from the watch set */
    vu_remove_watch_cb remove_watch;

    /* @panic: encountered an unrecoverable error, you may try to
     * re-initialize */
    vu_panic_cb panic;
    const VuDevIface *iface;
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
 * @socket: the socket connected to vhost-user master
 * @panic: a panic callback
 * @set_watch: a set_watch callback
 * @remove_watch: a remove_watch callback
 * @iface: a VuDevIface structure with vhost-user device callbacks
 *
 * Intializes a VuDev vhost-user context.
 **/
void vu_init(VuDev *dev,
             int socket,
             vu_panic_cb panic,
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
 * @guest_addr: guest address
 *
 * Translate a guest address to a pointer. Returns NULL on failure.
 */
void *vu_gpa_to_va(VuDev *dev, uint64_t guest_addr);

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
 * vu_queue_enabled:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 *
 * Returns: whether the queue is empty.
 */
int vu_queue_empty(VuDev *dev, VuVirtq *vq);

/**
 * vu_queue_notify:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 *
 * Request to notify the queue via callfd (skipped if unnecessary)
 */
void vu_queue_notify(VuDev *dev, VuVirtq *vq);

/**
 * vu_queue_pop:
 * @dev: a VuDev context
 * @vq: a VuVirtq queue
 * @sz: the size of struct to return (must be >= VuVirtqElement)
 *
 * Returns: a VuVirtqElement filled from the queue or NULL.
 */
void *vu_queue_pop(VuDev *dev, VuVirtq *vq, size_t sz);

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
