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
#include "qemu/log.h"
#include "sysemu/vhost-user-backend.h"

#include "standard-headers/linux/virtio_gpu.h"
#include "standard-headers/linux/virtio_ids.h"
#include "qom/object.h"

#define TYPE_VIRTIO_GPU_BASE "virtio-gpu-base"
OBJECT_DECLARE_TYPE(VirtIOGPUBase, VirtIOGPUBaseClass,
                    VIRTIO_GPU_BASE)

#define TYPE_VIRTIO_GPU "virtio-gpu-device"
OBJECT_DECLARE_TYPE(VirtIOGPU, VirtIOGPUClass, VIRTIO_GPU)

#define TYPE_VIRTIO_GPU_GL "virtio-gpu-gl-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOGPUGL, VIRTIO_GPU_GL)

#define TYPE_VHOST_USER_GPU "vhost-user-gpu"
OBJECT_DECLARE_SIMPLE_TYPE(VhostUserGPU, VHOST_USER_GPU)

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

    uint64_t blob_size;
    void *blob;
    int dmabuf_fd;
    uint8_t *remapped;

    QTAILQ_ENTRY(virtio_gpu_simple_resource) next;
};

struct virtio_gpu_framebuffer {
    pixman_format_code_t format;
    uint32_t bytes_pp;
    uint32_t width, height;
    uint32_t stride;
    uint32_t offset;
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
    uint16_t width_mm, height_mm;
    uint32_t width, height;
    uint32_t refresh_rate;
    int x, y;
};

enum virtio_gpu_base_conf_flags {
    VIRTIO_GPU_FLAG_VIRGL_ENABLED = 1,
    VIRTIO_GPU_FLAG_STATS_ENABLED,
    VIRTIO_GPU_FLAG_EDID_ENABLED,
    VIRTIO_GPU_FLAG_DMABUF_ENABLED,
    VIRTIO_GPU_FLAG_BLOB_ENABLED,
};

#define virtio_gpu_virgl_enabled(_cfg) \
    (_cfg.flags & (1 << VIRTIO_GPU_FLAG_VIRGL_ENABLED))
#define virtio_gpu_stats_enabled(_cfg) \
    (_cfg.flags & (1 << VIRTIO_GPU_FLAG_STATS_ENABLED))
#define virtio_gpu_edid_enabled(_cfg) \
    (_cfg.flags & (1 << VIRTIO_GPU_FLAG_EDID_ENABLED))
#define virtio_gpu_dmabuf_enabled(_cfg) \
    (_cfg.flags & (1 << VIRTIO_GPU_FLAG_DMABUF_ENABLED))
#define virtio_gpu_blob_enabled(_cfg) \
    (_cfg.flags & (1 << VIRTIO_GPU_FLAG_BLOB_ENABLED))

struct virtio_gpu_base_conf {
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
    bool finished;
    QTAILQ_ENTRY(virtio_gpu_ctrl_command) next;
};

struct VirtIOGPUBase {
    VirtIODevice parent_obj;

    Error *migration_blocker;

    struct virtio_gpu_base_conf conf;
    struct virtio_gpu_config virtio_config;
    const GraphicHwOps *hw_ops;

    int renderer_blocked;
    int enable;

    struct virtio_gpu_scanout scanout[VIRTIO_GPU_MAX_SCANOUTS];

    int enabled_output_bitmask;
    struct virtio_gpu_requested_state req_state[VIRTIO_GPU_MAX_SCANOUTS];
};

struct VirtIOGPUBaseClass {
    VirtioDeviceClass parent;

    void (*gl_flushed)(VirtIOGPUBase *g);
};

#define VIRTIO_GPU_BASE_PROPERTIES(_state, _conf)                       \
    DEFINE_PROP_UINT32("max_outputs", _state, _conf.max_outputs, 1),    \
    DEFINE_PROP_BIT("edid", _state, _conf.flags, \
                    VIRTIO_GPU_FLAG_EDID_ENABLED, true), \
    DEFINE_PROP_UINT32("xres", _state, _conf.xres, 1280), \
    DEFINE_PROP_UINT32("yres", _state, _conf.yres, 800)

typedef struct VGPUDMABuf {
    QemuDmaBuf buf;
    uint32_t scanout_id;
    QTAILQ_ENTRY(VGPUDMABuf) next;
} VGPUDMABuf;

struct VirtIOGPU {
    VirtIOGPUBase parent_obj;

    uint64_t conf_max_hostmem;

    VirtQueue *ctrl_vq;
    VirtQueue *cursor_vq;

    QEMUBH *ctrl_bh;
    QEMUBH *cursor_bh;

    QTAILQ_HEAD(, virtio_gpu_simple_resource) reslist;
    QTAILQ_HEAD(, virtio_gpu_ctrl_command) cmdq;
    QTAILQ_HEAD(, virtio_gpu_ctrl_command) fenceq;

    uint64_t hostmem;

    bool processing_cmdq;
    QEMUTimer *fence_poll;
    QEMUTimer *print_stats;

    uint32_t inflight;
    struct {
        uint32_t max_inflight;
        uint32_t requests;
        uint32_t req_3d;
        uint32_t bytes_3d;
    } stats;

    struct {
        QTAILQ_HEAD(, VGPUDMABuf) bufs;
        VGPUDMABuf *primary[VIRTIO_GPU_MAX_SCANOUTS];
    } dmabuf;
};

struct VirtIOGPUClass {
    VirtIOGPUBaseClass parent;

    void (*handle_ctrl)(VirtIODevice *vdev, VirtQueue *vq);
    void (*process_cmd)(VirtIOGPU *g, struct virtio_gpu_ctrl_command *cmd);
    void (*update_cursor_data)(VirtIOGPU *g,
                               struct virtio_gpu_scanout *s,
                               uint32_t resource_id);
};

struct VirtIOGPUGL {
    struct VirtIOGPU parent_obj;

    bool renderer_inited;
    bool renderer_reset;
};

struct VhostUserGPU {
    VirtIOGPUBase parent_obj;

    VhostUserBackend *vhost;
    int vhost_gpu_fd; /* closed by the chardev */
    CharBackend vhost_chr;
    QemuDmaBuf dmabuf[VIRTIO_GPU_MAX_SCANOUTS];
    bool backend_blocked;
};

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

/* virtio-gpu-base.c */
bool virtio_gpu_base_device_realize(DeviceState *qdev,
                                    VirtIOHandleOutput ctrl_cb,
                                    VirtIOHandleOutput cursor_cb,
                                    Error **errp);
void virtio_gpu_base_reset(VirtIOGPUBase *g);
void virtio_gpu_base_fill_display_info(VirtIOGPUBase *g,
                        struct virtio_gpu_resp_display_info *dpy_info);

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
void virtio_gpu_get_edid(VirtIOGPU *g,
                         struct virtio_gpu_ctrl_command *cmd);
int virtio_gpu_create_mapping_iov(VirtIOGPU *g,
                                  uint32_t nr_entries, uint32_t offset,
                                  struct virtio_gpu_ctrl_command *cmd,
                                  uint64_t **addr, struct iovec **iov,
                                  uint32_t *niov);
void virtio_gpu_cleanup_mapping_iov(VirtIOGPU *g,
                                    struct iovec *iov, uint32_t count);
void virtio_gpu_process_cmdq(VirtIOGPU *g);
void virtio_gpu_device_realize(DeviceState *qdev, Error **errp);
void virtio_gpu_reset(VirtIODevice *vdev);
void virtio_gpu_simple_process_cmd(VirtIOGPU *g, struct virtio_gpu_ctrl_command *cmd);
void virtio_gpu_update_cursor_data(VirtIOGPU *g,
                                   struct virtio_gpu_scanout *s,
                                   uint32_t resource_id);

/* virtio-gpu-udmabuf.c */
bool virtio_gpu_have_udmabuf(void);
void virtio_gpu_init_udmabuf(struct virtio_gpu_simple_resource *res);
void virtio_gpu_fini_udmabuf(struct virtio_gpu_simple_resource *res);
int virtio_gpu_update_dmabuf(VirtIOGPU *g,
                             uint32_t scanout_id,
                             struct virtio_gpu_simple_resource *res,
                             struct virtio_gpu_framebuffer *fb,
                             struct virtio_gpu_rect *r);

/* virtio-gpu-3d.c */
void virtio_gpu_virgl_process_cmd(VirtIOGPU *g,
                                  struct virtio_gpu_ctrl_command *cmd);
void virtio_gpu_virgl_fence_poll(VirtIOGPU *g);
void virtio_gpu_virgl_reset_scanout(VirtIOGPU *g);
void virtio_gpu_virgl_reset(VirtIOGPU *g);
int virtio_gpu_virgl_init(VirtIOGPU *g);
int virtio_gpu_virgl_get_num_capsets(VirtIOGPU *g);

#endif
