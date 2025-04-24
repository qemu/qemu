/*
 * display support for mdev based vgpu devices
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Authors:
 *    Gerd Hoffmann
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "qemu/error-report.h"
#include "hw/display/edid.h"
#include "ui/console.h"
#include "qapi/error.h"
#include "pci.h"
#include "trace.h"

#ifndef DRM_PLANE_TYPE_PRIMARY
# define DRM_PLANE_TYPE_PRIMARY 1
# define DRM_PLANE_TYPE_CURSOR  2
#endif

#define pread_field(_fd, _reg, _ptr, _fld)                              \
    (sizeof(_ptr->_fld) !=                                              \
     pread(_fd, &(_ptr->_fld), sizeof(_ptr->_fld),                      \
           _reg->offset + offsetof(typeof(*_ptr), _fld)))

#define pwrite_field(_fd, _reg, _ptr, _fld)                             \
    (sizeof(_ptr->_fld) !=                                              \
     pwrite(_fd, &(_ptr->_fld), sizeof(_ptr->_fld),                     \
            _reg->offset + offsetof(typeof(*_ptr), _fld)))


static void vfio_display_edid_link_up(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;
    VFIODisplay *dpy = vdev->dpy;
    int fd = vdev->vbasedev.fd;

    dpy->edid_regs->link_state = VFIO_DEVICE_GFX_LINK_STATE_UP;
    if (pwrite_field(fd, dpy->edid_info, dpy->edid_regs, link_state)) {
        goto err;
    }
    trace_vfio_display_edid_link_up();
    return;

err:
    trace_vfio_display_edid_write_error();
}

static void vfio_display_edid_update(VFIOPCIDevice *vdev, bool enabled,
                                     int prefx, int prefy)
{
    VFIODisplay *dpy = vdev->dpy;
    int fd = vdev->vbasedev.fd;
    qemu_edid_info edid = {
        .maxx  = dpy->edid_regs->max_xres,
        .maxy  = dpy->edid_regs->max_yres,
        .prefx = prefx ?: vdev->display_xres,
        .prefy = prefy ?: vdev->display_yres,
    };

    timer_del(dpy->edid_link_timer);
    dpy->edid_regs->link_state = VFIO_DEVICE_GFX_LINK_STATE_DOWN;
    if (pwrite_field(fd, dpy->edid_info, dpy->edid_regs, link_state)) {
        goto err;
    }
    trace_vfio_display_edid_link_down();

    if (!enabled) {
        return;
    }

    if (edid.maxx && edid.prefx > edid.maxx) {
        edid.prefx = edid.maxx;
    }
    if (edid.maxy && edid.prefy > edid.maxy) {
        edid.prefy = edid.maxy;
    }
    qemu_edid_generate(dpy->edid_blob,
                       dpy->edid_regs->edid_max_size,
                       &edid);
    trace_vfio_display_edid_update(edid.prefx, edid.prefy);

    dpy->edid_regs->edid_size = qemu_edid_size(dpy->edid_blob);
    if (pwrite_field(fd, dpy->edid_info, dpy->edid_regs, edid_size)) {
        goto err;
    }
    if (pwrite(fd, dpy->edid_blob, dpy->edid_regs->edid_size,
               dpy->edid_info->offset + dpy->edid_regs->edid_offset)
        != dpy->edid_regs->edid_size) {
        goto err;
    }

    timer_mod(dpy->edid_link_timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 100);
    return;

err:
    trace_vfio_display_edid_write_error();
}

static void vfio_display_edid_ui_info(void *opaque, uint32_t idx,
                                      QemuUIInfo *info)
{
    VFIOPCIDevice *vdev = opaque;
    VFIODisplay *dpy = vdev->dpy;

    if (!dpy->edid_regs) {
        return;
    }

    if (info->width && info->height) {
        vfio_display_edid_update(vdev, true, info->width, info->height);
    } else {
        vfio_display_edid_update(vdev, false, 0, 0);
    }
}

static bool vfio_display_edid_init(VFIOPCIDevice *vdev, Error **errp)
{
    VFIODisplay *dpy = vdev->dpy;
    int fd = vdev->vbasedev.fd;
    int ret;

    ret = vfio_get_dev_region_info(&vdev->vbasedev,
                                   VFIO_REGION_TYPE_GFX,
                                   VFIO_REGION_SUBTYPE_GFX_EDID,
                                   &dpy->edid_info);
    if (ret) {
        /* Failed to get GFX edid info, allow to go through without edid. */
        return true;
    }

    trace_vfio_display_edid_available();
    dpy->edid_regs = g_new0(struct vfio_region_gfx_edid, 1);
    if (pread_field(fd, dpy->edid_info, dpy->edid_regs, edid_offset)) {
        goto err;
    }
    if (pread_field(fd, dpy->edid_info, dpy->edid_regs, edid_max_size)) {
        goto err;
    }
    if (pread_field(fd, dpy->edid_info, dpy->edid_regs, max_xres)) {
        goto err;
    }
    if (pread_field(fd, dpy->edid_info, dpy->edid_regs, max_yres)) {
        goto err;
    }

    dpy->edid_blob = g_malloc0(dpy->edid_regs->edid_max_size);

    /* if xres + yres properties are unset use the maximum resolution */
    if (!vdev->display_xres) {
        vdev->display_xres = dpy->edid_regs->max_xres;
    }
    if (!vdev->display_yres) {
        vdev->display_yres = dpy->edid_regs->max_yres;
    }

    dpy->edid_link_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                        vfio_display_edid_link_up, vdev);

    vfio_display_edid_update(vdev, true, 0, 0);
    return true;

err:
    error_setg(errp, "vfio: failed to read GFX edid field");
    trace_vfio_display_edid_write_error();
    g_free(dpy->edid_info);
    g_free(dpy->edid_regs);
    dpy->edid_info = NULL;
    dpy->edid_regs = NULL;
    return false;
}

static void vfio_display_edid_exit(VFIODisplay *dpy)
{
    if (!dpy->edid_regs) {
        return;
    }

    g_free(dpy->edid_info);
    g_free(dpy->edid_regs);
    g_free(dpy->edid_blob);
    timer_free(dpy->edid_link_timer);
}

static void vfio_display_update_cursor(VFIODMABuf *dmabuf,
                                       struct vfio_device_gfx_plane_info *plane)
{
    if (dmabuf->pos_x != plane->x_pos || dmabuf->pos_y != plane->y_pos) {
        dmabuf->pos_x      = plane->x_pos;
        dmabuf->pos_y      = plane->y_pos;
        dmabuf->pos_updates++;
    }
    if (dmabuf->hot_x != plane->x_hot || dmabuf->hot_y != plane->y_hot) {
        dmabuf->hot_x      = plane->x_hot;
        dmabuf->hot_y      = plane->y_hot;
        dmabuf->hot_updates++;
    }
}

static VFIODMABuf *vfio_display_get_dmabuf(VFIOPCIDevice *vdev,
                                           uint32_t plane_type)
{
    VFIODisplay *dpy = vdev->dpy;
    struct vfio_device_gfx_plane_info plane;
    VFIODMABuf *dmabuf;
    int fd, ret;

    memset(&plane, 0, sizeof(plane));
    plane.argsz = sizeof(plane);
    plane.flags = VFIO_GFX_PLANE_TYPE_DMABUF;
    plane.drm_plane_type = plane_type;
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &plane);
    if (ret < 0) {
        return NULL;
    }
    if (!plane.drm_format || !plane.size) {
        return NULL;
    }

    QTAILQ_FOREACH(dmabuf, &dpy->dmabuf.bufs, next) {
        if (dmabuf->dmabuf_id == plane.dmabuf_id) {
            /* found in list, move to head, return it */
            QTAILQ_REMOVE(&dpy->dmabuf.bufs, dmabuf, next);
            QTAILQ_INSERT_HEAD(&dpy->dmabuf.bufs, dmabuf, next);
            if (plane_type == DRM_PLANE_TYPE_CURSOR) {
                vfio_display_update_cursor(dmabuf, &plane);
            }
            return dmabuf;
        }
    }

    fd = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_GET_GFX_DMABUF, &plane.dmabuf_id);
    if (fd < 0) {
        return NULL;
    }

    dmabuf = g_new0(VFIODMABuf, 1);
    dmabuf->dmabuf_id  = plane.dmabuf_id;
    dmabuf->buf = qemu_dmabuf_new(plane.width, plane.height,
                                  plane.stride, 0, 0, plane.width,
                                  plane.height, plane.drm_format,
                                  plane.drm_format_mod, fd, false, false);

    if (plane_type == DRM_PLANE_TYPE_CURSOR) {
        vfio_display_update_cursor(dmabuf, &plane);
    }

    QTAILQ_INSERT_HEAD(&dpy->dmabuf.bufs, dmabuf, next);
    return dmabuf;
}

static void vfio_display_free_one_dmabuf(VFIODisplay *dpy, VFIODMABuf *dmabuf)
{
    QTAILQ_REMOVE(&dpy->dmabuf.bufs, dmabuf, next);

    qemu_dmabuf_close(dmabuf->buf);
    dpy_gl_release_dmabuf(dpy->con, dmabuf->buf);
    g_clear_pointer(&dmabuf->buf, qemu_dmabuf_free);
    g_free(dmabuf);
}

static void vfio_display_free_dmabufs(VFIOPCIDevice *vdev)
{
    VFIODisplay *dpy = vdev->dpy;
    VFIODMABuf *dmabuf, *tmp;
    uint32_t keep = 5;

    QTAILQ_FOREACH_SAFE(dmabuf, &dpy->dmabuf.bufs, next, tmp) {
        if (keep > 0) {
            keep--;
            continue;
        }
        assert(dmabuf != dpy->dmabuf.primary);
        vfio_display_free_one_dmabuf(dpy, dmabuf);
    }
}

static void vfio_display_dmabuf_update(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;
    VFIODisplay *dpy = vdev->dpy;
    VFIODMABuf *primary, *cursor;
    uint32_t width, height;
    bool free_bufs = false, new_cursor = false;

    primary = vfio_display_get_dmabuf(vdev, DRM_PLANE_TYPE_PRIMARY);
    if (primary == NULL) {
        if (dpy->ramfb) {
            ramfb_display_update(dpy->con, dpy->ramfb);
        }
        return;
    }

    width = qemu_dmabuf_get_width(primary->buf);
    height = qemu_dmabuf_get_height(primary->buf);

    if (dpy->dmabuf.primary != primary) {
        dpy->dmabuf.primary = primary;
        qemu_console_resize(dpy->con, width, height);
        dpy_gl_scanout_dmabuf(dpy->con, primary->buf);
        free_bufs = true;
    }

    cursor = vfio_display_get_dmabuf(vdev, DRM_PLANE_TYPE_CURSOR);
    if (dpy->dmabuf.cursor != cursor) {
        dpy->dmabuf.cursor = cursor;
        new_cursor = true;
        free_bufs = true;
    }

    if (cursor && (new_cursor || cursor->hot_updates)) {
        bool have_hot = (cursor->hot_x != 0xffffffff &&
                         cursor->hot_y != 0xffffffff);
        dpy_gl_cursor_dmabuf(dpy->con, cursor->buf, have_hot,
                             cursor->hot_x, cursor->hot_y);
        cursor->hot_updates = 0;
    } else if (!cursor && new_cursor) {
        dpy_gl_cursor_dmabuf(dpy->con, NULL, false, 0, 0);
    }

    if (cursor && cursor->pos_updates) {
        dpy_gl_cursor_position(dpy->con,
                               cursor->pos_x,
                               cursor->pos_y);
        cursor->pos_updates = 0;
    }

    dpy_gl_update(dpy->con, 0, 0, width, height);

    if (free_bufs) {
        vfio_display_free_dmabufs(vdev);
    }
}

static int vfio_display_get_flags(void *opaque)
{
    return GRAPHIC_FLAGS_GL | GRAPHIC_FLAGS_DMABUF;
}

static const GraphicHwOps vfio_display_dmabuf_ops = {
    .get_flags  = vfio_display_get_flags,
    .gfx_update = vfio_display_dmabuf_update,
    .ui_info    = vfio_display_edid_ui_info,
};

static bool vfio_display_dmabuf_init(VFIOPCIDevice *vdev, Error **errp)
{
    if (!display_opengl) {
        error_setg(errp, "vfio-display-dmabuf: opengl not available");
        return false;
    }

    vdev->dpy = g_new0(VFIODisplay, 1);
    vdev->dpy->con = graphic_console_init(DEVICE(vdev), 0,
                                          &vfio_display_dmabuf_ops,
                                          vdev);
    if (vdev->enable_ramfb) {
        vdev->dpy->ramfb = ramfb_setup(errp);
        if (!vdev->dpy->ramfb) {
            return false;
        }
    }
    return vfio_display_edid_init(vdev, errp);
}

static void vfio_display_dmabuf_exit(VFIODisplay *dpy)
{
    VFIODMABuf *dmabuf;

    if (QTAILQ_EMPTY(&dpy->dmabuf.bufs)) {
        return;
    }

    while ((dmabuf = QTAILQ_FIRST(&dpy->dmabuf.bufs)) != NULL) {
        vfio_display_free_one_dmabuf(dpy, dmabuf);
    }
}

/* ---------------------------------------------------------------------- */
void vfio_display_reset(VFIOPCIDevice *vdev)
{
    if (!vdev || !vdev->dpy || !vdev->dpy->con ||
        !vdev->dpy->dmabuf.primary) {
        return;
    }

    dpy_gl_scanout_disable(vdev->dpy->con);
    vfio_display_dmabuf_exit(vdev->dpy);
    dpy_gfx_update_full(vdev->dpy->con);
}

static void vfio_display_region_update(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;
    VFIODisplay *dpy = vdev->dpy;
    struct vfio_device_gfx_plane_info plane = {
        .argsz = sizeof(plane),
        .flags = VFIO_GFX_PLANE_TYPE_REGION
    };
    pixman_format_code_t format;
    int ret;

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &plane);
    if (ret < 0) {
        error_report("ioctl VFIO_DEVICE_QUERY_GFX_PLANE: %s",
                     strerror(errno));
        return;
    }
    if (!plane.drm_format || !plane.size) {
        if (dpy->ramfb) {
            ramfb_display_update(dpy->con, dpy->ramfb);
            dpy->region.surface = NULL;
        }
        return;
    }
    format = qemu_drm_format_to_pixman(plane.drm_format);
    if (!format) {
        return;
    }

    if (dpy->region.buffer.size &&
        dpy->region.buffer.nr != plane.region_index) {
        /* region changed */
        vfio_region_exit(&dpy->region.buffer);
        vfio_region_finalize(&dpy->region.buffer);
        dpy->region.surface = NULL;
    }

    if (dpy->region.surface &&
        (surface_width(dpy->region.surface) != plane.width ||
         surface_height(dpy->region.surface) != plane.height ||
         surface_format(dpy->region.surface) != format)) {
        /* size changed */
        dpy->region.surface = NULL;
    }

    if (!dpy->region.buffer.size) {
        /* mmap region */
        ret = vfio_region_setup(OBJECT(vdev), &vdev->vbasedev,
                                &dpy->region.buffer,
                                plane.region_index,
                                "display");
        if (ret != 0) {
            error_report("%s: vfio_region_setup(%d): %s",
                         __func__, plane.region_index, strerror(-ret));
            goto err;
        }
        ret = vfio_region_mmap(&dpy->region.buffer);
        if (ret != 0) {
            error_report("%s: vfio_region_mmap(%d): %s", __func__,
                         plane.region_index, strerror(-ret));
            goto err;
        }
        assert(dpy->region.buffer.mmaps[0].mmap != NULL);
    }

    if (dpy->region.surface == NULL) {
        /* create surface */
        dpy->region.surface = qemu_create_displaysurface_from
            (plane.width, plane.height, format,
             plane.stride, dpy->region.buffer.mmaps[0].mmap);
        dpy_gfx_replace_surface(dpy->con, dpy->region.surface);
    }

    /* full screen update */
    dpy_gfx_update(dpy->con, 0, 0,
                   surface_width(dpy->region.surface),
                   surface_height(dpy->region.surface));
    return;

err:
    vfio_region_exit(&dpy->region.buffer);
    vfio_region_finalize(&dpy->region.buffer);
}

static const GraphicHwOps vfio_display_region_ops = {
    .gfx_update = vfio_display_region_update,
};

static bool vfio_display_region_init(VFIOPCIDevice *vdev, Error **errp)
{
    vdev->dpy = g_new0(VFIODisplay, 1);
    vdev->dpy->con = graphic_console_init(DEVICE(vdev), 0,
                                          &vfio_display_region_ops,
                                          vdev);
    if (vdev->enable_ramfb) {
        vdev->dpy->ramfb = ramfb_setup(errp);
        if (!vdev->dpy->ramfb) {
            return false;
        }
    }
    return true;
}

static void vfio_display_region_exit(VFIODisplay *dpy)
{
    if (!dpy->region.buffer.size) {
        return;
    }

    vfio_region_exit(&dpy->region.buffer);
    vfio_region_finalize(&dpy->region.buffer);
}

/* ---------------------------------------------------------------------- */

bool vfio_display_probe(VFIOPCIDevice *vdev, Error **errp)
{
    struct vfio_device_gfx_plane_info probe;
    int ret;

    memset(&probe, 0, sizeof(probe));
    probe.argsz = sizeof(probe);
    probe.flags = VFIO_GFX_PLANE_TYPE_PROBE | VFIO_GFX_PLANE_TYPE_DMABUF;
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &probe);
    if (ret == 0) {
        return vfio_display_dmabuf_init(vdev, errp);
    }

    memset(&probe, 0, sizeof(probe));
    probe.argsz = sizeof(probe);
    probe.flags = VFIO_GFX_PLANE_TYPE_PROBE | VFIO_GFX_PLANE_TYPE_REGION;
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &probe);
    if (ret == 0) {
        return vfio_display_region_init(vdev, errp);
    }

    if (vdev->display == ON_OFF_AUTO_AUTO) {
        /* not an error in automatic mode */
        return true;
    }

    error_setg(errp, "vfio: device doesn't support any (known) display method");
    return false;
}

void vfio_display_finalize(VFIOPCIDevice *vdev)
{
    if (!vdev->dpy) {
        return;
    }

    graphic_console_close(vdev->dpy->con);
    vfio_display_dmabuf_exit(vdev->dpy);
    vfio_display_region_exit(vdev->dpy);
    vfio_display_edid_exit(vdev->dpy);
    g_free(vdev->dpy);
}

static bool migrate_needed(void *opaque)
{
    VFIODisplay *dpy = opaque;
    bool ramfb_exists = dpy->ramfb != NULL;

    /* see vfio_display_migration_needed() */
    assert(ramfb_exists);
    return ramfb_exists;
}

const VMStateDescription vfio_display_vmstate = {
    .name = "VFIODisplay",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = migrate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_POINTER(ramfb, VFIODisplay, ramfb_vmstate, RAMFBState),
        VMSTATE_END_OF_LIST(),
    }
};
