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
#include "qemu/osdep.h"
#include "qemu/drm.h"
#include "qapi/error.h"
#include "qemu/sockets.h"

#include <pixman.h>
#include <glib-unix.h>

#include "vugpu.h"
#include "hw/virtio/virtio-gpu-bswap.h"
#include "hw/virtio/virtio-gpu-pixman.h"
#include "virgl.h"
#include "vugbm.h"

struct virtio_gpu_simple_resource {
    uint32_t resource_id;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    struct iovec *iov;
    unsigned int iov_cnt;
    uint32_t scanout_bitmask;
    pixman_image_t *image;
    struct vugbm_buffer buffer;
    QTAILQ_ENTRY(virtio_gpu_simple_resource) next;
};

static gboolean opt_print_caps;
static int opt_fdnum = -1;
static char *opt_socket_path;
static char *opt_render_node;
static gboolean opt_virgl;

static void vg_handle_ctrl(VuDev *dev, int qidx);

static const char *
vg_cmd_to_string(int cmd)
{
#define CMD(cmd) [cmd] = #cmd
    static const char *vg_cmd_str[] = {
        CMD(VIRTIO_GPU_UNDEFINED),

        /* 2d commands */
        CMD(VIRTIO_GPU_CMD_GET_DISPLAY_INFO),
        CMD(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D),
        CMD(VIRTIO_GPU_CMD_RESOURCE_UNREF),
        CMD(VIRTIO_GPU_CMD_SET_SCANOUT),
        CMD(VIRTIO_GPU_CMD_RESOURCE_FLUSH),
        CMD(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D),
        CMD(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING),
        CMD(VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING),
        CMD(VIRTIO_GPU_CMD_GET_CAPSET_INFO),
        CMD(VIRTIO_GPU_CMD_GET_CAPSET),

        /* 3d commands */
        CMD(VIRTIO_GPU_CMD_CTX_CREATE),
        CMD(VIRTIO_GPU_CMD_CTX_DESTROY),
        CMD(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE),
        CMD(VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE),
        CMD(VIRTIO_GPU_CMD_RESOURCE_CREATE_3D),
        CMD(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D),
        CMD(VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D),
        CMD(VIRTIO_GPU_CMD_SUBMIT_3D),

        /* cursor commands */
        CMD(VIRTIO_GPU_CMD_UPDATE_CURSOR),
        CMD(VIRTIO_GPU_CMD_MOVE_CURSOR),
    };
#undef REQ

    if (cmd >= 0 && cmd < G_N_ELEMENTS(vg_cmd_str)) {
        return vg_cmd_str[cmd];
    } else {
        return "unknown";
    }
}

static int
vg_sock_fd_read(int sock, void *buf, ssize_t buflen)
{
    int ret;

    do {
        ret = read(sock, buf, buflen);
    } while (ret < 0 && (errno == EINTR || errno == EAGAIN));

    g_warn_if_fail(ret == buflen);
    return ret;
}

static void
vg_sock_fd_close(VuGpu *g)
{
    if (g->sock_fd >= 0) {
        close(g->sock_fd);
        g->sock_fd = -1;
    }
}

static gboolean
source_wait_cb(gint fd, GIOCondition condition, gpointer user_data)
{
    VuGpu *g = user_data;

    if (!vg_recv_msg(g, VHOST_USER_GPU_DMABUF_UPDATE, 0, NULL)) {
        return G_SOURCE_CONTINUE;
    }

    /* resume */
    g->wait_ok = 0;
    vg_handle_ctrl(&g->dev.parent, 0);

    return G_SOURCE_REMOVE;
}

void
vg_wait_ok(VuGpu *g)
{
    assert(g->wait_ok == 0);
    g->wait_ok = g_unix_fd_add(g->sock_fd, G_IO_IN | G_IO_HUP,
                               source_wait_cb, g);
}

static int
vg_sock_fd_write(int sock, const void *buf, ssize_t buflen, int fd)
{
    ssize_t ret;
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = buflen,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };
    union {
        struct cmsghdr cmsghdr;
        char control[CMSG_SPACE(sizeof(int))];
    } cmsgu;
    struct cmsghdr *cmsg;

    if (fd != -1) {
        msg.msg_control = cmsgu.control;
        msg.msg_controllen = sizeof(cmsgu.control);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;

        *((int *)CMSG_DATA(cmsg)) = fd;
    }

    do {
        ret = sendmsg(sock, &msg, 0);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

    g_warn_if_fail(ret == buflen);
    return ret;
}

void
vg_send_msg(VuGpu *vg, const VhostUserGpuMsg *msg, int fd)
{
    if (vg_sock_fd_write(vg->sock_fd, msg,
                         VHOST_USER_GPU_HDR_SIZE + msg->size, fd) < 0) {
        vg_sock_fd_close(vg);
    }
}

bool
vg_recv_msg(VuGpu *g, uint32_t expect_req, uint32_t expect_size,
            gpointer payload)
{
    uint32_t req, flags, size;

    if (vg_sock_fd_read(g->sock_fd, &req, sizeof(req)) < 0 ||
        vg_sock_fd_read(g->sock_fd, &flags, sizeof(flags)) < 0 ||
        vg_sock_fd_read(g->sock_fd, &size, sizeof(size)) < 0) {
        goto err;
    }

    g_return_val_if_fail(req == expect_req, false);
    g_return_val_if_fail(flags & VHOST_USER_GPU_MSG_FLAG_REPLY, false);
    g_return_val_if_fail(size == expect_size, false);

    if (size && vg_sock_fd_read(g->sock_fd, payload, size) != size) {
        goto err;
    }

    return true;

err:
    vg_sock_fd_close(g);
    return false;
}

static struct virtio_gpu_simple_resource *
virtio_gpu_find_resource(VuGpu *g, uint32_t resource_id)
{
    struct virtio_gpu_simple_resource *res;

    QTAILQ_FOREACH(res, &g->reslist, next) {
        if (res->resource_id == resource_id) {
            return res;
        }
    }
    return NULL;
}

void
vg_ctrl_response(VuGpu *g,
                 struct virtio_gpu_ctrl_command *cmd,
                 struct virtio_gpu_ctrl_hdr *resp,
                 size_t resp_len)
{
    size_t s;

    if (cmd->cmd_hdr.flags & VIRTIO_GPU_FLAG_FENCE) {
        resp->flags |= VIRTIO_GPU_FLAG_FENCE;
        resp->fence_id = cmd->cmd_hdr.fence_id;
        resp->ctx_id = cmd->cmd_hdr.ctx_id;
    }
    virtio_gpu_ctrl_hdr_bswap(resp);
    s = iov_from_buf(cmd->elem.in_sg, cmd->elem.in_num, 0, resp, resp_len);
    if (s != resp_len) {
        g_critical("%s: response size incorrect %zu vs %zu",
                   __func__, s, resp_len);
    }
    vu_queue_push(&g->dev.parent, cmd->vq, &cmd->elem, s);
    vu_queue_notify(&g->dev.parent, cmd->vq);
    cmd->finished = true;
}

void
vg_ctrl_response_nodata(VuGpu *g,
                        struct virtio_gpu_ctrl_command *cmd,
                        enum virtio_gpu_ctrl_type type)
{
    struct virtio_gpu_ctrl_hdr resp = {
        .type = type,
    };

    vg_ctrl_response(g, cmd, &resp, sizeof(resp));
}

void
vg_get_display_info(VuGpu *vg, struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_resp_display_info dpy_info = { {} };
    VhostUserGpuMsg msg = {
        .request = VHOST_USER_GPU_GET_DISPLAY_INFO,
        .size = 0,
    };

    assert(vg->wait_ok == 0);

    vg_send_msg(vg, &msg, -1);
    if (!vg_recv_msg(vg, msg.request, sizeof(dpy_info), &dpy_info)) {
        return;
    }

    vg_ctrl_response(vg, cmd, &dpy_info.hdr, sizeof(dpy_info));
}

static void
vg_resource_create_2d(VuGpu *g,
                      struct virtio_gpu_ctrl_command *cmd)
{
    pixman_format_code_t pformat;
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_create_2d c2d;

    VUGPU_FILL_CMD(c2d);
    virtio_gpu_bswap_32(&c2d, sizeof(c2d));

    if (c2d.resource_id == 0) {
        g_critical("%s: resource id 0 is not allowed", __func__);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }

    res = virtio_gpu_find_resource(g, c2d.resource_id);
    if (res) {
        g_critical("%s: resource already exists %d", __func__, c2d.resource_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }

    res = g_new0(struct virtio_gpu_simple_resource, 1);
    res->width = c2d.width;
    res->height = c2d.height;
    res->format = c2d.format;
    res->resource_id = c2d.resource_id;

    pformat = virtio_gpu_get_pixman_format(c2d.format);
    if (!pformat) {
        g_critical("%s: host couldn't handle guest format %d",
                   __func__, c2d.format);
        g_free(res);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
        return;
    }
    vugbm_buffer_create(&res->buffer, &g->gdev, c2d.width, c2d.height);
    res->image = pixman_image_create_bits(pformat,
                                          c2d.width,
                                          c2d.height,
                                          (uint32_t *)res->buffer.mmap,
                                          res->buffer.stride);
    if (!res->image) {
        g_critical("%s: resource creation failed %d %d %d",
                   __func__, c2d.resource_id, c2d.width, c2d.height);
        g_free(res);
        cmd->error = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
        return;
    }

    QTAILQ_INSERT_HEAD(&g->reslist, res, next);
}

static void
vg_disable_scanout(VuGpu *g, int scanout_id)
{
    struct virtio_gpu_scanout *scanout = &g->scanout[scanout_id];
    struct virtio_gpu_simple_resource *res;

    if (scanout->resource_id == 0) {
        return;
    }

    res = virtio_gpu_find_resource(g, scanout->resource_id);
    if (res) {
        res->scanout_bitmask &= ~(1 << scanout_id);
    }

    scanout->width = 0;
    scanout->height = 0;

    if (g->sock_fd >= 0) {
        VhostUserGpuMsg msg = {
            .request = VHOST_USER_GPU_SCANOUT,
            .size = sizeof(VhostUserGpuScanout),
            .payload.scanout.scanout_id = scanout_id,
        };
        vg_send_msg(g, &msg, -1);
    }
}

static void
vg_resource_destroy(VuGpu *g,
                    struct virtio_gpu_simple_resource *res)
{
    int i;

    if (res->scanout_bitmask) {
        for (i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
            if (res->scanout_bitmask & (1 << i)) {
                vg_disable_scanout(g, i);
            }
        }
    }

    vugbm_buffer_destroy(&res->buffer);
    pixman_image_unref(res->image);
    QTAILQ_REMOVE(&g->reslist, res, next);
    g_free(res);
}

static void
vg_resource_unref(VuGpu *g,
                  struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_unref unref;

    VUGPU_FILL_CMD(unref);
    virtio_gpu_bswap_32(&unref, sizeof(unref));

    res = virtio_gpu_find_resource(g, unref.resource_id);
    if (!res) {
        g_critical("%s: illegal resource specified %d",
                   __func__, unref.resource_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }
    vg_resource_destroy(g, res);
}

int
vg_create_mapping_iov(VuGpu *g,
                      struct virtio_gpu_resource_attach_backing *ab,
                      struct virtio_gpu_ctrl_command *cmd,
                      struct iovec **iov)
{
    struct virtio_gpu_mem_entry *ents;
    size_t esize, s;
    int i;

    if (ab->nr_entries > 16384) {
        g_critical("%s: nr_entries is too big (%d > 16384)",
                   __func__, ab->nr_entries);
        return -1;
    }

    esize = sizeof(*ents) * ab->nr_entries;
    ents = g_malloc(esize);
    s = iov_to_buf(cmd->elem.out_sg, cmd->elem.out_num,
                   sizeof(*ab), ents, esize);
    if (s != esize) {
        g_critical("%s: command data size incorrect %zu vs %zu",
                   __func__, s, esize);
        g_free(ents);
        return -1;
    }

    *iov = g_malloc0(sizeof(struct iovec) * ab->nr_entries);
    for (i = 0; i < ab->nr_entries; i++) {
        uint64_t len = ents[i].length;
        (*iov)[i].iov_len = ents[i].length;
        (*iov)[i].iov_base = vu_gpa_to_va(&g->dev.parent, &len, ents[i].addr);
        if (!(*iov)[i].iov_base || len != ents[i].length) {
            g_critical("%s: resource %d element %d",
                       __func__, ab->resource_id, i);
            g_free(*iov);
            g_free(ents);
            *iov = NULL;
            return -1;
        }
    }
    g_free(ents);
    return 0;
}

static void
vg_resource_attach_backing(VuGpu *g,
                           struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_attach_backing ab;
    int ret;

    VUGPU_FILL_CMD(ab);
    virtio_gpu_bswap_32(&ab, sizeof(ab));

    res = virtio_gpu_find_resource(g, ab.resource_id);
    if (!res) {
        g_critical("%s: illegal resource specified %d",
                   __func__, ab.resource_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }

    ret = vg_create_mapping_iov(g, &ab, cmd, &res->iov);
    if (ret != 0) {
        cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
        return;
    }

    res->iov_cnt = ab.nr_entries;
}

static void
vg_resource_detach_backing(VuGpu *g,
                           struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_detach_backing detach;

    VUGPU_FILL_CMD(detach);
    virtio_gpu_bswap_32(&detach, sizeof(detach));

    res = virtio_gpu_find_resource(g, detach.resource_id);
    if (!res || !res->iov) {
        g_critical("%s: illegal resource specified %d",
                   __func__, detach.resource_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }

    g_free(res->iov);
    res->iov = NULL;
    res->iov_cnt = 0;
}

static void
vg_transfer_to_host_2d(VuGpu *g,
                       struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_simple_resource *res;
    int h;
    uint32_t src_offset, dst_offset, stride;
    int bpp;
    pixman_format_code_t format;
    struct virtio_gpu_transfer_to_host_2d t2d;

    VUGPU_FILL_CMD(t2d);
    virtio_gpu_t2d_bswap(&t2d);

    res = virtio_gpu_find_resource(g, t2d.resource_id);
    if (!res || !res->iov) {
        g_critical("%s: illegal resource specified %d",
                   __func__, t2d.resource_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }

    if (t2d.r.x > res->width ||
        t2d.r.y > res->height ||
        t2d.r.width > res->width ||
        t2d.r.height > res->height ||
        t2d.r.x + t2d.r.width > res->width ||
        t2d.r.y + t2d.r.height > res->height) {
        g_critical("%s: transfer bounds outside resource"
                   " bounds for resource %d: %d %d %d %d vs %d %d",
                   __func__, t2d.resource_id, t2d.r.x, t2d.r.y,
                   t2d.r.width, t2d.r.height, res->width, res->height);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
        return;
    }

    format = pixman_image_get_format(res->image);
    bpp = (PIXMAN_FORMAT_BPP(format) + 7) / 8;
    stride = pixman_image_get_stride(res->image);

    if (t2d.offset || t2d.r.x || t2d.r.y ||
        t2d.r.width != pixman_image_get_width(res->image)) {
        void *img_data = pixman_image_get_data(res->image);
        for (h = 0; h < t2d.r.height; h++) {
            src_offset = t2d.offset + stride * h;
            dst_offset = (t2d.r.y + h) * stride + (t2d.r.x * bpp);

            iov_to_buf(res->iov, res->iov_cnt, src_offset,
                       img_data
                       + dst_offset, t2d.r.width * bpp);
        }
    } else {
        iov_to_buf(res->iov, res->iov_cnt, 0,
                   pixman_image_get_data(res->image),
                   pixman_image_get_stride(res->image)
                   * pixman_image_get_height(res->image));
    }
}

static void
vg_set_scanout(VuGpu *g,
               struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_simple_resource *res, *ores;
    struct virtio_gpu_scanout *scanout;
    struct virtio_gpu_set_scanout ss;
    int fd;

    VUGPU_FILL_CMD(ss);
    virtio_gpu_bswap_32(&ss, sizeof(ss));

    if (ss.scanout_id >= VIRTIO_GPU_MAX_SCANOUTS) {
        g_critical("%s: illegal scanout id specified %d",
                   __func__, ss.scanout_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
        return;
    }

    if (ss.resource_id == 0) {
        vg_disable_scanout(g, ss.scanout_id);
        return;
    }

    /* create a surface for this scanout */
    res = virtio_gpu_find_resource(g, ss.resource_id);
    if (!res) {
        g_critical("%s: illegal resource specified %d",
                      __func__, ss.resource_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }

    if (ss.r.x > res->width ||
        ss.r.y > res->height ||
        ss.r.width > res->width ||
        ss.r.height > res->height ||
        ss.r.x + ss.r.width > res->width ||
        ss.r.y + ss.r.height > res->height) {
        g_critical("%s: illegal scanout %d bounds for"
                   " resource %d, (%d,%d)+%d,%d vs %d %d",
                   __func__, ss.scanout_id, ss.resource_id, ss.r.x, ss.r.y,
                   ss.r.width, ss.r.height, res->width, res->height);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
        return;
    }

    scanout = &g->scanout[ss.scanout_id];

    ores = virtio_gpu_find_resource(g, scanout->resource_id);
    if (ores) {
        ores->scanout_bitmask &= ~(1 << ss.scanout_id);
    }

    res->scanout_bitmask |= (1 << ss.scanout_id);
    scanout->resource_id = ss.resource_id;
    scanout->x = ss.r.x;
    scanout->y = ss.r.y;
    scanout->width = ss.r.width;
    scanout->height = ss.r.height;

    struct vugbm_buffer *buffer = &res->buffer;

    if (vugbm_buffer_can_get_dmabuf_fd(buffer)) {
        VhostUserGpuMsg msg = {
            .request = VHOST_USER_GPU_DMABUF_SCANOUT,
            .size = sizeof(VhostUserGpuDMABUFScanout),
            .payload.dmabuf_scanout = (VhostUserGpuDMABUFScanout) {
                .scanout_id = ss.scanout_id,
                .x = ss.r.x,
                .y = ss.r.y,
                .width = ss.r.width,
                .height = ss.r.height,
                .fd_width = buffer->width,
                .fd_height = buffer->height,
                .fd_stride = buffer->stride,
                .fd_drm_fourcc = buffer->format
            }
        };

        if (vugbm_buffer_get_dmabuf_fd(buffer, &fd)) {
            vg_send_msg(g, &msg, fd);
            close(fd);
        }
    } else {
        VhostUserGpuMsg msg = {
            .request = VHOST_USER_GPU_SCANOUT,
            .size = sizeof(VhostUserGpuScanout),
            .payload.scanout = (VhostUserGpuScanout) {
                .scanout_id = ss.scanout_id,
                .width = scanout->width,
                .height = scanout->height
            }
        };
        vg_send_msg(g, &msg, -1);
    }
}

static void
vg_resource_flush(VuGpu *g,
                  struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_simple_resource *res;
    struct virtio_gpu_resource_flush rf;
    pixman_region16_t flush_region;
    int i;

    VUGPU_FILL_CMD(rf);
    virtio_gpu_bswap_32(&rf, sizeof(rf));

    res = virtio_gpu_find_resource(g, rf.resource_id);
    if (!res) {
        g_critical("%s: illegal resource specified %d\n",
                   __func__, rf.resource_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        return;
    }

    if (rf.r.x > res->width ||
        rf.r.y > res->height ||
        rf.r.width > res->width ||
        rf.r.height > res->height ||
        rf.r.x + rf.r.width > res->width ||
        rf.r.y + rf.r.height > res->height) {
        g_critical("%s: flush bounds outside resource"
                   " bounds for resource %d: %d %d %d %d vs %d %d\n",
                   __func__, rf.resource_id, rf.r.x, rf.r.y,
                   rf.r.width, rf.r.height, res->width, res->height);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
        return;
    }

    pixman_region_init_rect(&flush_region,
                            rf.r.x, rf.r.y, rf.r.width, rf.r.height);
    for (i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        struct virtio_gpu_scanout *scanout;
        pixman_region16_t region, finalregion;
        pixman_box16_t *extents;

        if (!(res->scanout_bitmask & (1 << i))) {
            continue;
        }
        scanout = &g->scanout[i];

        pixman_region_init(&finalregion);
        pixman_region_init_rect(&region, scanout->x, scanout->y,
                                scanout->width, scanout->height);

        pixman_region_intersect(&finalregion, &flush_region, &region);

        extents = pixman_region_extents(&finalregion);
        size_t width = extents->x2 - extents->x1;
        size_t height = extents->y2 - extents->y1;

        if (vugbm_buffer_can_get_dmabuf_fd(&res->buffer)) {
            VhostUserGpuMsg vmsg = {
                .request = VHOST_USER_GPU_DMABUF_UPDATE,
                .size = sizeof(VhostUserGpuUpdate),
                .payload.update = (VhostUserGpuUpdate) {
                    .scanout_id = i,
                    .x = extents->x1,
                    .y = extents->y1,
                    .width = width,
                    .height = height,
                }
            };
            vg_send_msg(g, &vmsg, -1);
            vg_wait_ok(g);
        } else {
            size_t bpp =
                PIXMAN_FORMAT_BPP(pixman_image_get_format(res->image)) / 8;
            size_t size = width * height * bpp;

            void *p = g_malloc(VHOST_USER_GPU_HDR_SIZE +
                               sizeof(VhostUserGpuUpdate) + size);
            VhostUserGpuMsg *msg = p;
            msg->request = VHOST_USER_GPU_UPDATE;
            msg->size = sizeof(VhostUserGpuUpdate) + size;
            msg->payload.update = (VhostUserGpuUpdate) {
                .scanout_id = i,
                .x = extents->x1,
                .y = extents->y1,
                .width = width,
                .height = height,
            };
            pixman_image_t *i =
                pixman_image_create_bits(pixman_image_get_format(res->image),
                                         msg->payload.update.width,
                                         msg->payload.update.height,
                                         p + offsetof(VhostUserGpuMsg,
                                                      payload.update.data),
                                         width * bpp);
            pixman_image_composite(PIXMAN_OP_SRC,
                                   res->image, NULL, i,
                                   extents->x1, extents->y1,
                                   0, 0, 0, 0,
                                   width, height);
            pixman_image_unref(i);
            vg_send_msg(g, msg, -1);
            g_free(msg);
        }
        pixman_region_fini(&region);
        pixman_region_fini(&finalregion);
    }
    pixman_region_fini(&flush_region);
}

static void
vg_process_cmd(VuGpu *vg, struct virtio_gpu_ctrl_command *cmd)
{
    switch (cmd->cmd_hdr.type) {
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
        vg_get_display_info(vg, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
        vg_resource_create_2d(vg, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_UNREF:
        vg_resource_unref(vg, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
        vg_resource_flush(vg, cmd);
        break;
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
        vg_transfer_to_host_2d(vg, cmd);
        break;
    case VIRTIO_GPU_CMD_SET_SCANOUT:
        vg_set_scanout(vg, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
        vg_resource_attach_backing(vg, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
        vg_resource_detach_backing(vg, cmd);
        break;
    /* case VIRTIO_GPU_CMD_GET_EDID: */
    /*     break */
    default:
        g_warning("TODO handle ctrl %x\n", cmd->cmd_hdr.type);
        cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
        break;
    }
    if (!cmd->finished) {
        vg_ctrl_response_nodata(vg, cmd, cmd->error ? cmd->error :
                                VIRTIO_GPU_RESP_OK_NODATA);
    }
}

static void
vg_handle_ctrl(VuDev *dev, int qidx)
{
    VuGpu *vg = container_of(dev, VuGpu, dev.parent);
    VuVirtq *vq = vu_get_queue(dev, qidx);
    struct virtio_gpu_ctrl_command *cmd = NULL;
    size_t len;

    for (;;) {
        if (vg->wait_ok != 0) {
            return;
        }

        cmd = vu_queue_pop(dev, vq, sizeof(struct virtio_gpu_ctrl_command));
        if (!cmd) {
            break;
        }
        cmd->vq = vq;
        cmd->error = 0;
        cmd->finished = false;

        len = iov_to_buf(cmd->elem.out_sg, cmd->elem.out_num,
                         0, &cmd->cmd_hdr, sizeof(cmd->cmd_hdr));
        if (len != sizeof(cmd->cmd_hdr)) {
            g_warning("%s: command size incorrect %zu vs %zu\n",
                      __func__, len, sizeof(cmd->cmd_hdr));
        }

        virtio_gpu_ctrl_hdr_bswap(&cmd->cmd_hdr);
        g_debug("%d %s\n", cmd->cmd_hdr.type,
                vg_cmd_to_string(cmd->cmd_hdr.type));

        if (vg->virgl) {
            vg_virgl_process_cmd(vg, cmd);
        } else {
            vg_process_cmd(vg, cmd);
        }

        if (!cmd->finished) {
            QTAILQ_INSERT_TAIL(&vg->fenceq, cmd, next);
            vg->inflight++;
        } else {
            g_free(cmd);
        }
    }
}

static void
update_cursor_data_simple(VuGpu *g, uint32_t resource_id, gpointer data)
{
    struct virtio_gpu_simple_resource *res;

    res = virtio_gpu_find_resource(g, resource_id);
    g_return_if_fail(res != NULL);
    g_return_if_fail(pixman_image_get_width(res->image) == 64);
    g_return_if_fail(pixman_image_get_height(res->image) == 64);
    g_return_if_fail(
        PIXMAN_FORMAT_BPP(pixman_image_get_format(res->image)) == 32);

    memcpy(data, pixman_image_get_data(res->image), 64 * 64 * sizeof(uint32_t));
}

static void
vg_process_cursor_cmd(VuGpu *g, struct virtio_gpu_update_cursor *cursor)
{
    bool move = cursor->hdr.type != VIRTIO_GPU_CMD_MOVE_CURSOR;

    g_debug("%s move:%d\n", G_STRFUNC, move);

    if (move) {
        VhostUserGpuMsg msg = {
            .request = cursor->resource_id ?
                VHOST_USER_GPU_CURSOR_POS : VHOST_USER_GPU_CURSOR_POS_HIDE,
            .size = sizeof(VhostUserGpuCursorPos),
            .payload.cursor_pos = {
                .scanout_id = cursor->pos.scanout_id,
                .x = cursor->pos.x,
                .y = cursor->pos.y,
            }
        };
        vg_send_msg(g, &msg, -1);
    } else {
        VhostUserGpuMsg msg = {
            .request = VHOST_USER_GPU_CURSOR_UPDATE,
            .size = sizeof(VhostUserGpuCursorUpdate),
            .payload.cursor_update = {
                .pos = {
                    .scanout_id = cursor->pos.scanout_id,
                    .x = cursor->pos.x,
                    .y = cursor->pos.y,
                },
                .hot_x = cursor->hot_x,
                .hot_y = cursor->hot_y,
            }
        };
        if (g->virgl) {
            vg_virgl_update_cursor_data(g, cursor->resource_id,
                                        msg.payload.cursor_update.data);
        } else {
            update_cursor_data_simple(g, cursor->resource_id,
                                      msg.payload.cursor_update.data);
        }
        vg_send_msg(g, &msg, -1);
    }
}

static void
vg_handle_cursor(VuDev *dev, int qidx)
{
    VuGpu *g = container_of(dev, VuGpu, dev.parent);
    VuVirtq *vq = vu_get_queue(dev, qidx);
    VuVirtqElement *elem;
    size_t len;
    struct virtio_gpu_update_cursor cursor;

    for (;;) {
        elem = vu_queue_pop(dev, vq, sizeof(VuVirtqElement));
        if (!elem) {
            break;
        }
        g_debug("cursor out:%d in:%d\n", elem->out_num, elem->in_num);

        len = iov_to_buf(elem->out_sg, elem->out_num,
                         0, &cursor, sizeof(cursor));
        if (len != sizeof(cursor)) {
            g_warning("%s: cursor size incorrect %zu vs %zu\n",
                      __func__, len, sizeof(cursor));
        } else {
            virtio_gpu_bswap_32(&cursor, sizeof(cursor));
            vg_process_cursor_cmd(g, &cursor);
        }
        vu_queue_push(dev, vq, elem, 0);
        vu_queue_notify(dev, vq);
        g_free(elem);
    }
}

static void
vg_panic(VuDev *dev, const char *msg)
{
    g_critical("%s\n", msg);
    exit(1);
}

static void
vg_queue_set_started(VuDev *dev, int qidx, bool started)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);

    g_debug("queue started %d:%d\n", qidx, started);

    switch (qidx) {
    case 0:
        vu_set_queue_handler(dev, vq, started ? vg_handle_ctrl : NULL);
        break;
    case 1:
        vu_set_queue_handler(dev, vq, started ? vg_handle_cursor : NULL);
        break;
    default:
        break;
    }
}

static void
set_gpu_protocol_features(VuGpu *g)
{
    uint64_t u64;
    VhostUserGpuMsg msg = {
        .request = VHOST_USER_GPU_GET_PROTOCOL_FEATURES
    };

    assert(g->wait_ok == 0);
    vg_send_msg(g, &msg, -1);
    if (!vg_recv_msg(g, msg.request, sizeof(u64), &u64)) {
        return;
    }

    msg = (VhostUserGpuMsg) {
        .request = VHOST_USER_GPU_SET_PROTOCOL_FEATURES,
        .size = sizeof(uint64_t),
        .payload.u64 = 0
    };
    vg_send_msg(g, &msg, -1);
}

static int
vg_process_msg(VuDev *dev, VhostUserMsg *msg, int *do_reply)
{
    VuGpu *g = container_of(dev, VuGpu, dev.parent);

    switch (msg->request) {
    case VHOST_USER_GPU_SET_SOCKET: {
        g_return_val_if_fail(msg->fd_num == 1, 1);
        g_return_val_if_fail(g->sock_fd == -1, 1);
        g->sock_fd = msg->fds[0];
        set_gpu_protocol_features(g);
        return 1;
    }
    default:
        return 0;
    }

    return 0;
}

static uint64_t
vg_get_features(VuDev *dev)
{
    uint64_t features = 0;

    if (opt_virgl) {
        features |= 1 << VIRTIO_GPU_F_VIRGL;
    }

    return features;
}

static void
vg_set_features(VuDev *dev, uint64_t features)
{
    VuGpu *g = container_of(dev, VuGpu, dev.parent);
    bool virgl = features & (1 << VIRTIO_GPU_F_VIRGL);

    if (virgl && !g->virgl_inited) {
        if (!vg_virgl_init(g)) {
            vg_panic(dev, "Failed to initialize virgl");
        }
        g->virgl_inited = true;
    }

    g->virgl = virgl;
}

static int
vg_get_config(VuDev *dev, uint8_t *config, uint32_t len)
{
    VuGpu *g = container_of(dev, VuGpu, dev.parent);

    g_return_val_if_fail(len <= sizeof(struct virtio_gpu_config), -1);

    if (opt_virgl) {
        g->virtio_config.num_capsets = vg_virgl_get_num_capsets();
    }

    memcpy(config, &g->virtio_config, len);

    return 0;
}

static int
vg_set_config(VuDev *dev, const uint8_t *data,
              uint32_t offset, uint32_t size,
              uint32_t flags)
{
    VuGpu *g = container_of(dev, VuGpu, dev.parent);
    struct virtio_gpu_config *config = (struct virtio_gpu_config *)data;

    if (config->events_clear) {
        g->virtio_config.events_read &= ~config->events_clear;
    }

    return 0;
}

static const VuDevIface vuiface = {
    .set_features = vg_set_features,
    .get_features = vg_get_features,
    .queue_set_started = vg_queue_set_started,
    .process_msg = vg_process_msg,
    .get_config = vg_get_config,
    .set_config = vg_set_config,
};

static void
vg_destroy(VuGpu *g)
{
    struct virtio_gpu_simple_resource *res, *tmp;

    vug_deinit(&g->dev);

    vg_sock_fd_close(g);

    QTAILQ_FOREACH_SAFE(res, &g->reslist, next, tmp) {
        vg_resource_destroy(g, res);
    }

    vugbm_device_destroy(&g->gdev);
}

static GOptionEntry entries[] = {
    { "print-capabilities", 'c', 0, G_OPTION_ARG_NONE, &opt_print_caps,
      "Print capabilities", NULL },
    { "fd", 'f', 0, G_OPTION_ARG_INT, &opt_fdnum,
      "Use inherited fd socket", "FDNUM" },
    { "socket-path", 's', 0, G_OPTION_ARG_FILENAME, &opt_socket_path,
      "Use UNIX socket path", "PATH" },
    { "render-node", 'r', 0, G_OPTION_ARG_FILENAME, &opt_render_node,
      "Specify DRM render node", "PATH" },
    { "virgl", 'v', 0, G_OPTION_ARG_NONE, &opt_virgl,
      "Turn virgl rendering on", NULL },
    { NULL, }
};

int
main(int argc, char *argv[])
{
    GOptionContext *context;
    GError *error = NULL;
    GMainLoop *loop = NULL;
    int fd;
    VuGpu g = { .sock_fd = -1, .drm_rnode_fd = -1 };

    QTAILQ_INIT(&g.reslist);
    QTAILQ_INIT(&g.fenceq);

    context = g_option_context_new("QEMU vhost-user-gpu");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Option parsing failed: %s\n", error->message);
        exit(EXIT_FAILURE);
    }
    g_option_context_free(context);

    if (opt_print_caps) {
        g_print("{\n");
        g_print("  \"type\": \"gpu\",\n");
        g_print("  \"features\": [\n");
        g_print("    \"render-node\",\n");
        g_print("    \"virgl\"\n");
        g_print("  ]\n");
        g_print("}\n");
        exit(EXIT_SUCCESS);
    }

    g.drm_rnode_fd = qemu_drm_rendernode_open(opt_render_node);
    if (opt_render_node && g.drm_rnode_fd == -1) {
        g_printerr("Failed to open DRM rendernode.\n");
        exit(EXIT_FAILURE);
    }

    if (g.drm_rnode_fd >= 0) {
        if (!vugbm_device_init(&g.gdev, g.drm_rnode_fd)) {
            g_warning("Failed to init DRM device, using fallback path");
        }
    }

    if ((!!opt_socket_path + (opt_fdnum != -1)) != 1) {
        g_printerr("Please specify either --fd or --socket-path\n");
        exit(EXIT_FAILURE);
    }

    if (opt_socket_path) {
        int lsock = unix_listen(opt_socket_path, &error_fatal);
        if (lsock < 0) {
            g_printerr("Failed to listen on %s.\n", opt_socket_path);
            exit(EXIT_FAILURE);
        }
        fd = accept(lsock, NULL, NULL);
        close(lsock);
    } else {
        fd = opt_fdnum;
    }
    if (fd == -1) {
        g_printerr("Invalid vhost-user socket.\n");
        exit(EXIT_FAILURE);
    }

    vug_init(&g.dev, fd, vg_panic, &vuiface);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);

    vg_destroy(&g);
    if (g.drm_rnode_fd >= 0) {
        close(g.drm_rnode_fd);
    }

    return 0;
}
