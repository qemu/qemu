/*
 * Virtio vhost-user GPU Device
 *
 * Copyright Red Hat, Inc. 2013-2018
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *     Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef VUGPU_H
#define VUGPU_H

#include "qemu/osdep.h"

#include "contrib/libvhost-user/libvhost-user-glib.h"
#include "standard-headers/linux/virtio_gpu.h"

#include "qemu/queue.h"
#include "qemu/iov.h"
#include "qemu/bswap.h"
#include "vugbm.h"

typedef enum VhostUserGpuRequest {
    VHOST_USER_GPU_NONE = 0,
    VHOST_USER_GPU_GET_PROTOCOL_FEATURES,
    VHOST_USER_GPU_SET_PROTOCOL_FEATURES,
    VHOST_USER_GPU_GET_DISPLAY_INFO,
    VHOST_USER_GPU_CURSOR_POS,
    VHOST_USER_GPU_CURSOR_POS_HIDE,
    VHOST_USER_GPU_CURSOR_UPDATE,
    VHOST_USER_GPU_SCANOUT,
    VHOST_USER_GPU_UPDATE,
    VHOST_USER_GPU_DMABUF_SCANOUT,
    VHOST_USER_GPU_DMABUF_UPDATE,
} VhostUserGpuRequest;

typedef struct VhostUserGpuDisplayInfoReply {
    struct virtio_gpu_resp_display_info info;
} VhostUserGpuDisplayInfoReply;

typedef struct VhostUserGpuCursorPos {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
} QEMU_PACKED VhostUserGpuCursorPos;

typedef struct VhostUserGpuCursorUpdate {
    VhostUserGpuCursorPos pos;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t data[64 * 64];
} QEMU_PACKED VhostUserGpuCursorUpdate;

typedef struct VhostUserGpuScanout {
    uint32_t scanout_id;
    uint32_t width;
    uint32_t height;
} QEMU_PACKED VhostUserGpuScanout;

typedef struct VhostUserGpuUpdate {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t data[];
} QEMU_PACKED VhostUserGpuUpdate;

typedef struct VhostUserGpuDMABUFScanout {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t fd_width;
    uint32_t fd_height;
    uint32_t fd_stride;
    uint32_t fd_flags;
    int fd_drm_fourcc;
} QEMU_PACKED VhostUserGpuDMABUFScanout;

typedef struct VhostUserGpuMsg {
    uint32_t request; /* VhostUserGpuRequest */
    uint32_t flags;
    uint32_t size; /* the following payload size */
    union {
        VhostUserGpuCursorPos cursor_pos;
        VhostUserGpuCursorUpdate cursor_update;
        VhostUserGpuScanout scanout;
        VhostUserGpuUpdate update;
        VhostUserGpuDMABUFScanout dmabuf_scanout;
        struct virtio_gpu_resp_display_info display_info;
        uint64_t u64;
    } payload;
} QEMU_PACKED VhostUserGpuMsg;

static VhostUserGpuMsg m __attribute__ ((unused));
#define VHOST_USER_GPU_HDR_SIZE \
    (sizeof(m.request) + sizeof(m.flags) + sizeof(m.size))

#define VHOST_USER_GPU_MSG_FLAG_REPLY 0x4

struct virtio_gpu_scanout {
    uint32_t width, height;
    int x, y;
    int invalidate;
    uint32_t resource_id;
};

typedef struct VuGpu {
    VugDev dev;
    struct virtio_gpu_config virtio_config;
    struct vugbm_device gdev;
    int sock_fd;
    int drm_rnode_fd;
    GSource *renderer_source;
    guint wait_ok;

    bool virgl;
    bool virgl_inited;
    uint32_t inflight;

    struct virtio_gpu_scanout scanout[VIRTIO_GPU_MAX_SCANOUTS];
    QTAILQ_HEAD(, virtio_gpu_simple_resource) reslist;
    QTAILQ_HEAD(, virtio_gpu_ctrl_command) fenceq;
} VuGpu;

struct virtio_gpu_ctrl_command {
    VuVirtqElement elem;
    VuVirtq *vq;
    struct virtio_gpu_ctrl_hdr cmd_hdr;
    uint32_t error;
    bool finished;
    QTAILQ_ENTRY(virtio_gpu_ctrl_command) next;
};

#define VUGPU_FILL_CMD(out) do {                                \
        size_t s;                                               \
        s = iov_to_buf(cmd->elem.out_sg, cmd->elem.out_num, 0,  \
                       &out, sizeof(out));                      \
        if (s != sizeof(out)) {                                 \
            g_critical("%s: command size incorrect %zu vs %zu", \
                       __func__, s, sizeof(out));               \
            return;                                             \
        }                                                       \
    } while (0)


void    vg_ctrl_response(VuGpu *g,
                         struct virtio_gpu_ctrl_command *cmd,
                         struct virtio_gpu_ctrl_hdr *resp,
                         size_t resp_len);

void    vg_ctrl_response_nodata(VuGpu *g,
                                struct virtio_gpu_ctrl_command *cmd,
                                enum virtio_gpu_ctrl_type type);

int     vg_create_mapping_iov(VuGpu *g,
                              struct virtio_gpu_resource_attach_backing *ab,
                              struct virtio_gpu_ctrl_command *cmd,
                              struct iovec **iov);

void    vg_get_display_info(VuGpu *vg, struct virtio_gpu_ctrl_command *cmd);

void    vg_wait_ok(VuGpu *g);

void    vg_send_msg(VuGpu *g, const VhostUserGpuMsg *msg, int fd);

bool    vg_recv_msg(VuGpu *g, uint32_t expect_req, uint32_t expect_size,
                    gpointer payload);


#endif
