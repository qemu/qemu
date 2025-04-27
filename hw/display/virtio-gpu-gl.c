/*
 * Virtio GPU Device
 *
 * Copyright Red Hat, Inc. 2013-2014
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/system.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-gpu.h"
#include "hw/virtio/virtio-gpu-bswap.h"
#include "hw/virtio/virtio-gpu-pixman.h"
#include "hw/qdev-properties.h"

#include <virglrenderer.h>

static void virtio_gpu_gl_update_cursor_data(VirtIOGPU *g,
                                             struct virtio_gpu_scanout *s,
                                             uint32_t resource_id)
{
    VirtIOGPUGL *gl = VIRTIO_GPU_GL(g);
    uint32_t width, height;
    uint32_t pixels, *data;

    if (gl->renderer_state != RS_INITED) {
        return;
    }

    data = virgl_renderer_get_cursor_data(resource_id, &width, &height);
    if (!data) {
        return;
    }

    if (width != s->current_cursor->width ||
        height != s->current_cursor->height) {
        free(data);
        return;
    }

    pixels = s->current_cursor->width * s->current_cursor->height;
    memcpy(s->current_cursor->data, data, pixels * sizeof(uint32_t));
    free(data);
}

static void virtio_gpu_gl_flushed(VirtIOGPUBase *b)
{
    VirtIOGPU *g = VIRTIO_GPU(b);

    virtio_gpu_process_cmdq(g);
}

static void virtio_gpu_gl_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOGPU *g = VIRTIO_GPU(vdev);
    VirtIOGPUGL *gl = VIRTIO_GPU_GL(vdev);
    struct virtio_gpu_ctrl_command *cmd;

    if (!virtio_queue_ready(vq)) {
        return;
    }

    switch (gl->renderer_state) {
    case RS_RESET:
        virtio_gpu_virgl_reset(g);
        /* fallthrough */
    case RS_START:
        if (virtio_gpu_virgl_init(g)) {
            gl->renderer_state = RS_INIT_FAILED;
            return;
        }

        gl->renderer_state = RS_INITED;
        break;
    case RS_INIT_FAILED:
        return;
    case RS_INITED:
        break;
    }

    cmd = virtqueue_pop(vq, sizeof(struct virtio_gpu_ctrl_command));
    while (cmd) {
        cmd->vq = vq;
        cmd->error = 0;
        cmd->finished = false;
        QTAILQ_INSERT_TAIL(&g->cmdq, cmd, next);
        cmd = virtqueue_pop(vq, sizeof(struct virtio_gpu_ctrl_command));
    }

    virtio_gpu_process_cmdq(g);
    virtio_gpu_virgl_fence_poll(g);
}

static void virtio_gpu_gl_reset(VirtIODevice *vdev)
{
    VirtIOGPU *g = VIRTIO_GPU(vdev);
    VirtIOGPUGL *gl = VIRTIO_GPU_GL(vdev);

    virtio_gpu_reset(vdev);

    /*
     * GL functions must be called with the associated GL context in main
     * thread, and when the renderer is unblocked.
     */
    if (gl->renderer_state == RS_INITED) {
        virtio_gpu_virgl_reset_scanout(g);
        gl->renderer_state = RS_RESET;
    }
}

static void virtio_gpu_gl_device_realize(DeviceState *qdev, Error **errp)
{
    ERRP_GUARD();
    VirtIOGPU *g = VIRTIO_GPU(qdev);

#if HOST_BIG_ENDIAN
    error_setg(errp, "virgl is not supported on bigendian platforms");
    return;
#endif

    if (!object_resolve_path_type("", TYPE_VIRTIO_GPU_GL, NULL)) {
        error_setg(errp, "at most one %s device is permitted", TYPE_VIRTIO_GPU_GL);
        return;
    }

    if (!display_opengl) {
        error_setg(errp,
                   "The display backend does not have OpenGL support enabled");
        error_append_hint(errp,
                          "It can be enabled with '-display BACKEND,gl=on' "
                          "where BACKEND is the name of the display backend "
                          "to use.\n");
        return;
    }

    g->parent_obj.conf.flags |= (1 << VIRTIO_GPU_FLAG_VIRGL_ENABLED);
    g->capset_ids = virtio_gpu_virgl_get_capsets(g);
    VIRTIO_GPU_BASE(g)->virtio_config.num_capsets = g->capset_ids->len;

#if VIRGL_VERSION_MAJOR >= 1
    g->parent_obj.conf.flags |= 1 << VIRTIO_GPU_FLAG_CONTEXT_INIT_ENABLED;
#endif

    virtio_gpu_device_realize(qdev, errp);
}

static const Property virtio_gpu_gl_properties[] = {
    DEFINE_PROP_BIT("stats", VirtIOGPU, parent_obj.conf.flags,
                    VIRTIO_GPU_FLAG_STATS_ENABLED, false),
    DEFINE_PROP_BIT("venus", VirtIOGPU, parent_obj.conf.flags,
                    VIRTIO_GPU_FLAG_VENUS_ENABLED, false),
};

static void virtio_gpu_gl_device_unrealize(DeviceState *qdev)
{
    VirtIOGPU *g = VIRTIO_GPU(qdev);
    VirtIOGPUGL *gl = VIRTIO_GPU_GL(qdev);

    if (gl->renderer_state >= RS_INITED) {
#if VIRGL_VERSION_MAJOR >= 1
        qemu_bh_delete(gl->cmdq_resume_bh);
#endif
        if (virtio_gpu_stats_enabled(g->parent_obj.conf)) {
            timer_free(gl->print_stats);
        }
        timer_free(gl->fence_poll);
        virgl_renderer_cleanup(NULL);
    }

    gl->renderer_state = RS_START;

    g_array_unref(g->capset_ids);
}

static void virtio_gpu_gl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    VirtIOGPUBaseClass *vbc = VIRTIO_GPU_BASE_CLASS(klass);
    VirtIOGPUClass *vgc = VIRTIO_GPU_CLASS(klass);

    vbc->gl_flushed = virtio_gpu_gl_flushed;
    vgc->handle_ctrl = virtio_gpu_gl_handle_ctrl;
    vgc->process_cmd = virtio_gpu_virgl_process_cmd;
    vgc->update_cursor_data = virtio_gpu_gl_update_cursor_data;

    vdc->realize = virtio_gpu_gl_device_realize;
    vdc->unrealize = virtio_gpu_gl_device_unrealize;
    vdc->reset = virtio_gpu_gl_reset;
    device_class_set_props(dc, virtio_gpu_gl_properties);
}

static const TypeInfo virtio_gpu_gl_info = {
    .name = TYPE_VIRTIO_GPU_GL,
    .parent = TYPE_VIRTIO_GPU,
    .instance_size = sizeof(VirtIOGPUGL),
    .class_init = virtio_gpu_gl_class_init,
};
module_obj(TYPE_VIRTIO_GPU_GL);
module_kconfig(VIRTIO_GPU);

static void virtio_register_types(void)
{
    type_register_static(&virtio_gpu_gl_info);
}

type_init(virtio_register_types)

module_dep("hw-display-virtio-gpu");
module_dep("ui-opengl");
