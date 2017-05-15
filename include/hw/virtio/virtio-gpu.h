/*
 * Virtio GPU Device
 *
 * Copyright Red Hat, Inc. 2013-2014
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_GPU_H
#define HW_VIRTIO_GPU_H

#include "qemu/queue.h"
#include "ui/qemu-pixman.h"
#include "ui/console.h"
#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"
#include "qemu/log.h"

#include "standard-headers/linux/virtio_gpu.h"
#define TYPE_VIRTIO_GPU "virtio-gpu-device"
#define VIRTIO_GPU(obj)                                        \
        OBJECT_CHECK(VirtIOGPU, (obj), TYPE_VIRTIO_GPU)

#define VIRTIO_ID_GPU 16

struct virtio_gpu_simple_resource {
    uint32_t resource_id;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint64_t *addrs;
    struct iovec *iov;
    unsigned int iov_cnt;
    uint32_t scanout_bitmask;
    pixman_image_t *image;
    uint64_t hostmem;
    QTAILQ_ENTRY(virtio_gpu_simple_resource) next;
};

struct virtio_gpu_scanout {
    QemuConsole *con;
    DisplaySurface *ds;
    uint32_t width, height;
    int x, y;
    int invalidate;
    uint32_t resource_id;
    struct virtio_gpu_update_cursor cursor;
    QEMUCursor *current_cursor;
};

struct virtio_gpu_requested_state {
    uint32_t width, height;
    int x, y;
};

enum virtio_gpu_conf_flags {
    VIRTIO_GPU_FLAG_VIRGL_ENABLED = 1,
    VIRTIO_GPU_FLAG_STATS_ENABLED,
};

#define virtio_gpu_virgl_enabled(_cfg) \
    (_cfg.flags & (1 << VIRTIO_GPU_FLAG_VIRGL_ENABLED))
#define virtio_gpu_stats_enabled(_cfg) \
    (_cfg.flags & (1 << VIRTIO_GPU_FLAG_STATS_ENABLED))

struct virtio_gpu_conf {
    uint64_t max_hostmem;
    uint32_t max_outputs;
    uint32_t flags;
    uint32_t xres;
    uint32_t yres;
};

struct virtio_gpu_ctrl_command {
    VirtQueueElement elem;
    VirtQueue *vq;
    struct virtio_gpu_ctrl_hdr cmd_hdr;
    uint32_t error;
    bool waiting;
    bool finished;
    QTAILQ_ENTRY(virtio_gpu_ctrl_command) next;
};

typedef struct VirtIOGPU {
    VirtIODevice parent_obj;

    QEMUBH *ctrl_bh;
    QEMUBH *cursor_bh;
    VirtQueue *ctrl_vq;
    VirtQueue *cursor_vq;

    int enable;

    int config_size;
    DeviceState *qdev;

    QTAILQ_HEAD(, virtio_gpu_simple_resource) reslist;
    QTAILQ_HEAD(, virtio_gpu_ctrl_command) cmdq;
    QTAILQ_HEAD(, virtio_gpu_ctrl_command) fenceq;

    struct virtio_gpu_scanout scanout[VIRTIO_GPU_MAX_SCANOUTS];
    struct virtio_gpu_requested_state req_state[VIRTIO_GPU_MAX_SCANOUTS];

    struct virtio_gpu_conf conf;
    uint64_t hostmem;
    int enabled_output_bitmask;
    struct virtio_gpu_config virtio_config;

    bool use_virgl_renderer;
    bool renderer_inited;
    int renderer_blocked;
    QEMUTimer *fence_poll;
    QEMUTimer *print_stats;

    uint32_t inflight;
    struct {
        uint32_t max_inflight;
        uint32_t requests;
        uint32_t req_3d;
        uint32_t bytes_3d;
    } stats;

    Error *migration_blocker;
} VirtIOGPU;

extern const GraphicHwOps virtio_gpu_ops;

/* to share between PCI and VGA */
#define DEFINE_VIRTIO_GPU_PCI_PROPERTIES(_state)               \
    DEFINE_PROP_BIT("ioeventfd", _state, flags,                \
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, false), \
    DEFINE_PROP_UINT32("vectors", _state, nvectors, 3)

#define VIRTIO_GPU_FILL_CMD(out) do {                                   \
        size_t s;                                                       \
        s = iov_to_buf(cmd->elem.out_sg, cmd->elem.out_num, 0,          \
                       &out, sizeof(out));                              \
        if (s != sizeof(out)) {                                         \
            qemu_log_mask(LOG_GUEST_ERROR,                              \
                          "%s: command size incorrect %zu vs %zu\n",    \
                          __func__, s, sizeof(out));                    \
            return;                                                     \
        }                                                               \
    } while (0)

/* virtio-gpu.c */
void virtio_gpu_ctrl_response(VirtIOGPU *g,
                              struct virtio_gpu_ctrl_command *cmd,
                              struct virtio_gpu_ctrl_hdr *resp,
                              size_t resp_len);
void virtio_gpu_ctrl_response_nodata(VirtIOGPU *g,
                                     struct virtio_gpu_ctrl_command *cmd,
                                     enum virtio_gpu_ctrl_type type);
void virtio_gpu_get_display_info(VirtIOGPU *g,
                                 struct virtio_gpu_ctrl_command *cmd);
int virtio_gpu_create_mapping_iov(struct virtio_gpu_resource_attach_backing *ab,
                                  struct virtio_gpu_ctrl_command *cmd,
                                  uint64_t **addr, struct iovec **iov);
void virtio_gpu_cleanup_mapping_iov(struct iovec *iov, uint32_t count);
void virtio_gpu_process_cmdq(VirtIOGPU *g);

/* virtio-gpu-3d.c */
void virtio_gpu_virgl_process_cmd(VirtIOGPU *g,
                                  struct virtio_gpu_ctrl_command *cmd);
void virtio_gpu_virgl_fence_poll(VirtIOGPU *g);
void virtio_gpu_virgl_reset(VirtIOGPU *g);
void virtio_gpu_gl_block(void *opaque, bool block);
int virtio_gpu_virgl_init(VirtIOGPU *g);

#endif
