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
#include <virglrenderer.h>
#include "virgl.h"

#include <epoxy/gl.h>

void
vg_virgl_update_cursor_data(VuGpu *g, uint32_t resource_id,
                            gpointer data)
{
    uint32_t width, height;
    uint32_t *cursor;

    cursor = virgl_renderer_get_cursor_data(resource_id, &width, &height);
    g_return_if_fail(cursor != NULL);
    g_return_if_fail(width == 64);
    g_return_if_fail(height == 64);

    memcpy(data, cursor, 64 * 64 * sizeof(uint32_t));
    free(cursor);
}

static void
virgl_cmd_context_create(VuGpu *g,
                         struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_ctx_create cc;

    VUGPU_FILL_CMD(cc);

    virgl_renderer_context_create(cc.hdr.ctx_id, cc.nlen,
                                  cc.debug_name);
}

static void
virgl_cmd_context_destroy(VuGpu *g,
                          struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_ctx_destroy cd;

    VUGPU_FILL_CMD(cd);

    virgl_renderer_context_destroy(cd.hdr.ctx_id);
}

static void
virgl_cmd_create_resource_2d(VuGpu *g,
                             struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_resource_create_2d c2d;
    struct virgl_renderer_resource_create_args args;

    VUGPU_FILL_CMD(c2d);

    args.handle = c2d.resource_id;
    args.target = 2;
    args.format = c2d.format;
    args.bind = (1 << 1);
    args.width = c2d.width;
    args.height = c2d.height;
    args.depth = 1;
    args.array_size = 1;
    args.last_level = 0;
    args.nr_samples = 0;
    args.flags = VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP;
    virgl_renderer_resource_create(&args, NULL, 0);
}

static void
virgl_cmd_create_resource_3d(VuGpu *g,
                             struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_resource_create_3d c3d;
    struct virgl_renderer_resource_create_args args;

    VUGPU_FILL_CMD(c3d);

    args.handle = c3d.resource_id;
    args.target = c3d.target;
    args.format = c3d.format;
    args.bind = c3d.bind;
    args.width = c3d.width;
    args.height = c3d.height;
    args.depth = c3d.depth;
    args.array_size = c3d.array_size;
    args.last_level = c3d.last_level;
    args.nr_samples = c3d.nr_samples;
    args.flags = c3d.flags;
    virgl_renderer_resource_create(&args, NULL, 0);
}

static void
virgl_cmd_resource_unref(VuGpu *g,
                         struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_resource_unref unref;
    struct iovec *res_iovs = NULL;
    int num_iovs = 0;

    VUGPU_FILL_CMD(unref);

    virgl_renderer_resource_detach_iov(unref.resource_id,
                                       &res_iovs,
                                       &num_iovs);
    if (res_iovs != NULL && num_iovs != 0) {
        vg_cleanup_mapping_iov(g, res_iovs, num_iovs);
    }
    virgl_renderer_resource_unref(unref.resource_id);
}

/* Not yet(?) defined in standard-headers, remove when possible */
#ifndef VIRTIO_GPU_CAPSET_VIRGL2
#define VIRTIO_GPU_CAPSET_VIRGL2 2
#endif

static void
virgl_cmd_get_capset_info(VuGpu *g,
                          struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_get_capset_info info;
    struct virtio_gpu_resp_capset_info resp;

    VUGPU_FILL_CMD(info);

    memset(&resp, 0, sizeof(resp));
    if (info.capset_index == 0) {
        resp.capset_id = VIRTIO_GPU_CAPSET_VIRGL;
        virgl_renderer_get_cap_set(resp.capset_id,
                                   &resp.capset_max_version,
                                   &resp.capset_max_size);
    } else if (info.capset_index == 1) {
        resp.capset_id = VIRTIO_GPU_CAPSET_VIRGL2;
        virgl_renderer_get_cap_set(resp.capset_id,
                                   &resp.capset_max_version,
                                   &resp.capset_max_size);
    } else {
        resp.capset_max_version = 0;
        resp.capset_max_size = 0;
    }
    resp.hdr.type = VIRTIO_GPU_RESP_OK_CAPSET_INFO;
    vg_ctrl_response(g, cmd, &resp.hdr, sizeof(resp));
}

uint32_t
vg_virgl_get_num_capsets(void)
{
    uint32_t capset2_max_ver, capset2_max_size;
    virgl_renderer_get_cap_set(VIRTIO_GPU_CAPSET_VIRGL2,
                               &capset2_max_ver,
                               &capset2_max_size);

    return capset2_max_ver ? 2 : 1;
}

static void
virgl_cmd_get_capset(VuGpu *g,
                     struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_get_capset gc;
    struct virtio_gpu_resp_capset *resp;
    uint32_t max_ver, max_size;

    VUGPU_FILL_CMD(gc);

    virgl_renderer_get_cap_set(gc.capset_id, &max_ver,
                               &max_size);
    if (!max_size) {
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
        return;
    }
    resp = g_malloc0(sizeof(*resp) + max_size);

    resp->hdr.type = VIRTIO_GPU_RESP_OK_CAPSET;
    virgl_renderer_fill_caps(gc.capset_id,
                             gc.capset_version,
                             (void *)resp->capset_data);
    vg_ctrl_response(g, cmd, &resp->hdr, sizeof(*resp) + max_size);
    g_free(resp);
}

static void
virgl_cmd_submit_3d(VuGpu *g,
                    struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_cmd_submit cs;
    void *buf;
    size_t s;

    VUGPU_FILL_CMD(cs);

    buf = g_malloc(cs.size);
    s = iov_to_buf(cmd->elem.out_sg, cmd->elem.out_num,
                   sizeof(cs), buf, cs.size);
    if (s != cs.size) {
        g_critical("%s: size mismatch (%zd/%d)", __func__, s, cs.size);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
        goto out;
    }

    virgl_renderer_submit_cmd(buf, cs.hdr.ctx_id, cs.size / 4);

out:
    g_free(buf);
}

static void
virgl_cmd_transfer_to_host_2d(VuGpu *g,
                              struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_transfer_to_host_2d t2d;
    struct virtio_gpu_box box;

    VUGPU_FILL_CMD(t2d);

    box.x = t2d.r.x;
    box.y = t2d.r.y;
    box.z = 0;
    box.w = t2d.r.width;
    box.h = t2d.r.height;
    box.d = 1;

    virgl_renderer_transfer_write_iov(t2d.resource_id,
                                      0,
                                      0,
                                      0,
                                      0,
                                      (struct virgl_box *)&box,
                                      t2d.offset, NULL, 0);
}

static void
virgl_cmd_transfer_to_host_3d(VuGpu *g,
                              struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_transfer_host_3d t3d;

    VUGPU_FILL_CMD(t3d);

    virgl_renderer_transfer_write_iov(t3d.resource_id,
                                      t3d.hdr.ctx_id,
                                      t3d.level,
                                      t3d.stride,
                                      t3d.layer_stride,
                                      (struct virgl_box *)&t3d.box,
                                      t3d.offset, NULL, 0);
}

static void
virgl_cmd_transfer_from_host_3d(VuGpu *g,
                                struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_transfer_host_3d tf3d;

    VUGPU_FILL_CMD(tf3d);

    virgl_renderer_transfer_read_iov(tf3d.resource_id,
                                     tf3d.hdr.ctx_id,
                                     tf3d.level,
                                     tf3d.stride,
                                     tf3d.layer_stride,
                                     (struct virgl_box *)&tf3d.box,
                                     tf3d.offset, NULL, 0);
}

static void
virgl_resource_attach_backing(VuGpu *g,
                              struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_resource_attach_backing att_rb;
    struct iovec *res_iovs;
    int ret;

    VUGPU_FILL_CMD(att_rb);

    ret = vg_create_mapping_iov(g, &att_rb, cmd, &res_iovs);
    if (ret != 0) {
        cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
        return;
    }

    ret = virgl_renderer_resource_attach_iov(att_rb.resource_id,
                                       res_iovs, att_rb.nr_entries);
    if (ret != 0) {
        vg_cleanup_mapping_iov(g, res_iovs, att_rb.nr_entries);
    }
}

static void
virgl_resource_detach_backing(VuGpu *g,
                              struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_resource_detach_backing detach_rb;
    struct iovec *res_iovs = NULL;
    int num_iovs = 0;

    VUGPU_FILL_CMD(detach_rb);

    virgl_renderer_resource_detach_iov(detach_rb.resource_id,
                                       &res_iovs,
                                       &num_iovs);
    if (res_iovs == NULL || num_iovs == 0) {
        return;
    }
    vg_cleanup_mapping_iov(g, res_iovs, num_iovs);
}

static void
virgl_cmd_set_scanout(VuGpu *g,
                      struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_set_scanout ss;
    struct virgl_renderer_resource_info info;
    int ret;

    VUGPU_FILL_CMD(ss);

    if (ss.scanout_id >= VIRTIO_GPU_MAX_SCANOUTS) {
        g_critical("%s: illegal scanout id specified %d",
                   __func__, ss.scanout_id);
        cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
        return;
    }

    memset(&info, 0, sizeof(info));

    if (ss.resource_id && ss.r.width && ss.r.height) {
        ret = virgl_renderer_resource_get_info(ss.resource_id, &info);
        if (ret == -1) {
            g_critical("%s: illegal resource specified %d\n",
                       __func__, ss.resource_id);
            cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
            return;
        }

        int fd = -1;
        if (virgl_renderer_get_fd_for_texture(info.tex_id, &fd) < 0) {
            g_critical("%s: failed to get fd for texture\n", __func__);
            cmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
            return;
        }
        assert(fd >= 0);
        VhostUserGpuMsg msg = {
            .request = VHOST_USER_GPU_DMABUF_SCANOUT,
            .size = sizeof(VhostUserGpuDMABUFScanout),
            .payload.dmabuf_scanout.scanout_id = ss.scanout_id,
            .payload.dmabuf_scanout.x =  ss.r.x,
            .payload.dmabuf_scanout.y =  ss.r.y,
            .payload.dmabuf_scanout.width = ss.r.width,
            .payload.dmabuf_scanout.height = ss.r.height,
            .payload.dmabuf_scanout.fd_width = info.width,
            .payload.dmabuf_scanout.fd_height = info.height,
            .payload.dmabuf_scanout.fd_stride = info.stride,
            .payload.dmabuf_scanout.fd_flags = info.flags,
            .payload.dmabuf_scanout.fd_drm_fourcc = info.drm_fourcc
        };
        vg_send_msg(g, &msg, fd);
        close(fd);
    } else {
        VhostUserGpuMsg msg = {
            .request = VHOST_USER_GPU_DMABUF_SCANOUT,
            .size = sizeof(VhostUserGpuDMABUFScanout),
            .payload.dmabuf_scanout.scanout_id = ss.scanout_id,
        };
        g_debug("disable scanout");
        vg_send_msg(g, &msg, -1);
    }
    g->scanout[ss.scanout_id].resource_id = ss.resource_id;
}

static void
virgl_cmd_resource_flush(VuGpu *g,
                         struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_resource_flush rf;
    int i;

    VUGPU_FILL_CMD(rf);

    glFlush();
    if (!rf.resource_id) {
        g_debug("bad resource id for flush..?");
        return;
    }
    for (i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        if (g->scanout[i].resource_id != rf.resource_id) {
            continue;
        }
        VhostUserGpuMsg msg = {
            .request = VHOST_USER_GPU_DMABUF_UPDATE,
            .size = sizeof(VhostUserGpuUpdate),
            .payload.update.scanout_id = i,
            .payload.update.x = rf.r.x,
            .payload.update.y = rf.r.y,
            .payload.update.width = rf.r.width,
            .payload.update.height = rf.r.height
        };
        vg_send_msg(g, &msg, -1);
        vg_wait_ok(g);
    }
}

static void
virgl_cmd_ctx_attach_resource(VuGpu *g,
                              struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_ctx_resource att_res;

    VUGPU_FILL_CMD(att_res);

    virgl_renderer_ctx_attach_resource(att_res.hdr.ctx_id, att_res.resource_id);
}

static void
virgl_cmd_ctx_detach_resource(VuGpu *g,
                              struct virtio_gpu_ctrl_command *cmd)
{
    struct virtio_gpu_ctx_resource det_res;

    VUGPU_FILL_CMD(det_res);

    virgl_renderer_ctx_detach_resource(det_res.hdr.ctx_id, det_res.resource_id);
}

void vg_virgl_process_cmd(VuGpu *g, struct virtio_gpu_ctrl_command *cmd)
{
    virgl_renderer_force_ctx_0();
    switch (cmd->cmd_hdr.type) {
    case VIRTIO_GPU_CMD_CTX_CREATE:
        virgl_cmd_context_create(g, cmd);
        break;
    case VIRTIO_GPU_CMD_CTX_DESTROY:
        virgl_cmd_context_destroy(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
        virgl_cmd_create_resource_2d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_3D:
        virgl_cmd_create_resource_3d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_SUBMIT_3D:
        virgl_cmd_submit_3d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
        virgl_cmd_transfer_to_host_2d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D:
        virgl_cmd_transfer_to_host_3d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D:
        virgl_cmd_transfer_from_host_3d(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
        virgl_resource_attach_backing(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
        virgl_resource_detach_backing(g, cmd);
        break;
    case VIRTIO_GPU_CMD_SET_SCANOUT:
        virgl_cmd_set_scanout(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
        virgl_cmd_resource_flush(g, cmd);
        break;
    case VIRTIO_GPU_CMD_RESOURCE_UNREF:
        virgl_cmd_resource_unref(g, cmd);
        break;
    case VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE:
        /* TODO add security */
        virgl_cmd_ctx_attach_resource(g, cmd);
        break;
    case VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE:
        /* TODO add security */
        virgl_cmd_ctx_detach_resource(g, cmd);
        break;
    case VIRTIO_GPU_CMD_GET_CAPSET_INFO:
        virgl_cmd_get_capset_info(g, cmd);
        break;
    case VIRTIO_GPU_CMD_GET_CAPSET:
        virgl_cmd_get_capset(g, cmd);
        break;
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
        vg_get_display_info(g, cmd);
        break;
    case VIRTIO_GPU_CMD_GET_EDID:
        vg_get_edid(g, cmd);
        break;
    default:
        g_debug("TODO handle ctrl %x\n", cmd->cmd_hdr.type);
        cmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
        break;
    }

    if (cmd->state != VG_CMD_STATE_NEW) {
        return;
    }

    if (cmd->error) {
        g_warning("%s: ctrl 0x%x, error 0x%x\n", __func__,
                  cmd->cmd_hdr.type, cmd->error);
        vg_ctrl_response_nodata(g, cmd, cmd->error);
        return;
    }

    if (!(cmd->cmd_hdr.flags & VIRTIO_GPU_FLAG_FENCE)) {
        vg_ctrl_response_nodata(g, cmd, VIRTIO_GPU_RESP_OK_NODATA);
        return;
    }

    g_debug("Creating fence id:%" PRId64 " type:%d",
            cmd->cmd_hdr.fence_id, cmd->cmd_hdr.type);
    virgl_renderer_create_fence(cmd->cmd_hdr.fence_id, cmd->cmd_hdr.type);
}

static void
virgl_write_fence(void *opaque, uint32_t fence)
{
    VuGpu *g = opaque;
    struct virtio_gpu_ctrl_command *cmd, *tmp;

    QTAILQ_FOREACH_SAFE(cmd, &g->fenceq, next, tmp) {
        /*
         * the guest can end up emitting fences out of order
         * so we should check all fenced cmds not just the first one.
         */
        if (cmd->cmd_hdr.fence_id > fence) {
            continue;
        }
        g_debug("FENCE %" PRIu64, cmd->cmd_hdr.fence_id);
        vg_ctrl_response_nodata(g, cmd, VIRTIO_GPU_RESP_OK_NODATA);
        QTAILQ_REMOVE(&g->fenceq, cmd, next);
        free(cmd);
        g->inflight--;
    }
}

#if defined(VIRGL_RENDERER_CALLBACKS_VERSION) && \
    VIRGL_RENDERER_CALLBACKS_VERSION >= 2
static int
virgl_get_drm_fd(void *opaque)
{
    VuGpu *g = opaque;

    return g->drm_rnode_fd;
}
#endif

static struct virgl_renderer_callbacks virgl_cbs = {
#if defined(VIRGL_RENDERER_CALLBACKS_VERSION) &&    \
    VIRGL_RENDERER_CALLBACKS_VERSION >= 2
    .get_drm_fd  = virgl_get_drm_fd,
    .version     = 2,
#else
    .version     = 1,
#endif
    .write_fence = virgl_write_fence,
};

static void
vg_virgl_poll(VuDev *dev, int condition, void *data)
{
    virgl_renderer_poll();
}

bool
vg_virgl_init(VuGpu *g)
{
    int ret;

    if (g->drm_rnode_fd && virgl_cbs.version == 1) {
        g_warning("virgl will use the default rendernode");
    }

    ret = virgl_renderer_init(g,
                              VIRGL_RENDERER_USE_EGL |
                              VIRGL_RENDERER_THREAD_SYNC,
                              &virgl_cbs);
    if (ret != 0) {
        return false;
    }

    ret = virgl_renderer_get_poll_fd();
    if (ret != -1) {
        g->renderer_source =
            vug_source_new(&g->dev, ret, G_IO_IN, vg_virgl_poll, g);
    }

    return true;
}
