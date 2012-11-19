/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "qemu-spice.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
#include "monitor.h"
#include "console.h"
#include "sysemu.h"
#include "trace.h"

#include "spice-display.h"

static int debug = 0;

static void GCC_FMT_ATTR(2, 3) dprint(int level, const char *fmt, ...)
{
    va_list args;

    if (level <= debug) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
}

int qemu_spice_rect_is_empty(const QXLRect* r)
{
    return r->top == r->bottom || r->left == r->right;
}

void qemu_spice_rect_union(QXLRect *dest, const QXLRect *r)
{
    if (qemu_spice_rect_is_empty(r)) {
        return;
    }

    if (qemu_spice_rect_is_empty(dest)) {
        *dest = *r;
        return;
    }

    dest->top = MIN(dest->top, r->top);
    dest->left = MIN(dest->left, r->left);
    dest->bottom = MAX(dest->bottom, r->bottom);
    dest->right = MAX(dest->right, r->right);
}

QXLCookie *qxl_cookie_new(int type, uint64_t io)
{
    QXLCookie *cookie;

    cookie = g_malloc0(sizeof(*cookie));
    cookie->type = type;
    cookie->io = io;
    return cookie;
}

void qemu_spice_add_memslot(SimpleSpiceDisplay *ssd, QXLDevMemSlot *memslot,
                            qxl_async_io async)
{
    trace_qemu_spice_add_memslot(ssd->qxl.id, memslot->slot_id,
                                memslot->virt_start, memslot->virt_end,
                                async);

    if (async != QXL_SYNC) {
        spice_qxl_add_memslot_async(&ssd->qxl, memslot,
                (uintptr_t)qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                                          QXL_IO_MEMSLOT_ADD_ASYNC));
    } else {
        ssd->worker->add_memslot(ssd->worker, memslot);
    }
}

void qemu_spice_del_memslot(SimpleSpiceDisplay *ssd, uint32_t gid, uint32_t sid)
{
    trace_qemu_spice_del_memslot(ssd->qxl.id, gid, sid);
    ssd->worker->del_memslot(ssd->worker, gid, sid);
}

void qemu_spice_create_primary_surface(SimpleSpiceDisplay *ssd, uint32_t id,
                                       QXLDevSurfaceCreate *surface,
                                       qxl_async_io async)
{
    trace_qemu_spice_create_primary_surface(ssd->qxl.id, id, surface, async);
    if (async != QXL_SYNC) {
        spice_qxl_create_primary_surface_async(&ssd->qxl, id, surface,
                (uintptr_t)qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                                          QXL_IO_CREATE_PRIMARY_ASYNC));
    } else {
        ssd->worker->create_primary_surface(ssd->worker, id, surface);
    }
}

void qemu_spice_destroy_primary_surface(SimpleSpiceDisplay *ssd,
                                        uint32_t id, qxl_async_io async)
{
    trace_qemu_spice_destroy_primary_surface(ssd->qxl.id, id, async);
    if (async != QXL_SYNC) {
        spice_qxl_destroy_primary_surface_async(&ssd->qxl, id,
                (uintptr_t)qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                                          QXL_IO_DESTROY_PRIMARY_ASYNC));
    } else {
        ssd->worker->destroy_primary_surface(ssd->worker, id);
    }
}

void qemu_spice_wakeup(SimpleSpiceDisplay *ssd)
{
    trace_qemu_spice_wakeup(ssd->qxl.id);
    ssd->worker->wakeup(ssd->worker);
}

static int spice_display_is_running;

void qemu_spice_display_start(void)
{
    spice_display_is_running = true;
}

void qemu_spice_display_stop(void)
{
    spice_display_is_running = false;
}

int qemu_spice_display_is_running(SimpleSpiceDisplay *ssd)
{
    return spice_display_is_running;
}

static void qemu_spice_create_one_update(SimpleSpiceDisplay *ssd,
                                         QXLRect *rect)
{
    SimpleSpiceUpdate *update;
    QXLDrawable *drawable;
    QXLImage *image;
    QXLCommand *cmd;
    int bw, bh;
    struct timespec time_space;
    pixman_image_t *dest;

    trace_qemu_spice_create_update(
           rect->left, rect->right,
           rect->top, rect->bottom);

    update   = g_malloc0(sizeof(*update));
    drawable = &update->drawable;
    image    = &update->image;
    cmd      = &update->ext.cmd;

    bw       = rect->right - rect->left;
    bh       = rect->bottom - rect->top;
    update->bitmap = g_malloc(bw * bh * 4);

    drawable->bbox            = *rect;
    drawable->clip.type       = SPICE_CLIP_TYPE_NONE;
    drawable->effect          = QXL_EFFECT_OPAQUE;
    drawable->release_info.id = (uintptr_t)update;
    drawable->type            = QXL_DRAW_COPY;
    drawable->surfaces_dest[0] = -1;
    drawable->surfaces_dest[1] = -1;
    drawable->surfaces_dest[2] = -1;
    clock_gettime(CLOCK_MONOTONIC, &time_space);
    /* time in milliseconds from epoch. */
    drawable->mm_time = time_space.tv_sec * 1000
                      + time_space.tv_nsec / 1000 / 1000;

    drawable->u.copy.rop_descriptor  = SPICE_ROPD_OP_PUT;
    drawable->u.copy.src_bitmap      = (uintptr_t)image;
    drawable->u.copy.src_area.right  = bw;
    drawable->u.copy.src_area.bottom = bh;

    QXL_SET_IMAGE_ID(image, QXL_IMAGE_GROUP_DEVICE, ssd->unique++);
    image->descriptor.type   = SPICE_IMAGE_TYPE_BITMAP;
    image->bitmap.flags      = QXL_BITMAP_DIRECT | QXL_BITMAP_TOP_DOWN;
    image->bitmap.stride     = bw * 4;
    image->descriptor.width  = image->bitmap.x = bw;
    image->descriptor.height = image->bitmap.y = bh;
    image->bitmap.data = (uintptr_t)(update->bitmap);
    image->bitmap.palette = 0;
    image->bitmap.format = SPICE_BITMAP_FMT_32BIT;

    dest = pixman_image_create_bits(PIXMAN_x8r8g8b8, bw, bh,
                                    (void *)update->bitmap, bw * 4);
    pixman_image_composite(PIXMAN_OP_SRC, ssd->surface, NULL, ssd->mirror,
                           rect->left, rect->top, 0, 0,
                           rect->left, rect->top, bw, bh);
    pixman_image_composite(PIXMAN_OP_SRC, ssd->mirror, NULL, dest,
                           rect->left, rect->top, 0, 0,
                           0, 0, bw, bh);
    pixman_image_unref(dest);

    cmd->type = QXL_CMD_DRAW;
    cmd->data = (uintptr_t)drawable;

    QTAILQ_INSERT_TAIL(&ssd->updates, update, next);
}

static void qemu_spice_create_update(SimpleSpiceDisplay *ssd)
{
    static const int blksize = 32;
    int blocks = (ds_get_width(ssd->ds) + blksize - 1) / blksize;
    int dirty_top[blocks];
    int y, yoff, x, xoff, blk, bw;
    int bpp = ds_get_bytes_per_pixel(ssd->ds);
    uint8_t *guest, *mirror;

    if (qemu_spice_rect_is_empty(&ssd->dirty)) {
        return;
    };

    if (ssd->surface == NULL) {
        ssd->surface = pixman_image_ref(ds_get_image(ssd->ds));
        ssd->mirror  = qemu_pixman_mirror_create(ds_get_format(ssd->ds),
                                                 ds_get_image(ssd->ds));
    }

    for (blk = 0; blk < blocks; blk++) {
        dirty_top[blk] = -1;
    }

    guest = ds_get_data(ssd->ds);
    mirror = (void *)pixman_image_get_data(ssd->mirror);
    for (y = ssd->dirty.top; y < ssd->dirty.bottom; y++) {
        yoff = y * ds_get_linesize(ssd->ds);
        for (x = ssd->dirty.left; x < ssd->dirty.right; x += blksize) {
            xoff = x * bpp;
            blk = x / blksize;
            bw = MIN(blksize, ssd->dirty.right - x);
            if (memcmp(guest + yoff + xoff,
                       mirror + yoff + xoff,
                       bw * bpp) == 0) {
                if (dirty_top[blk] != -1) {
                    QXLRect update = {
                        .top    = dirty_top[blk],
                        .bottom = y,
                        .left   = x,
                        .right  = x + bw,
                    };
                    qemu_spice_create_one_update(ssd, &update);
                    dirty_top[blk] = -1;
                }
            } else {
                if (dirty_top[blk] == -1) {
                    dirty_top[blk] = y;
                }
            }
        }
    }

    for (x = ssd->dirty.left; x < ssd->dirty.right; x += blksize) {
        blk = x / blksize;
        bw = MIN(blksize, ssd->dirty.right - x);
        if (dirty_top[blk] != -1) {
            QXLRect update = {
                .top    = dirty_top[blk],
                .bottom = ssd->dirty.bottom,
                .left   = x,
                .right  = x + bw,
            };
            qemu_spice_create_one_update(ssd, &update);
            dirty_top[blk] = -1;
        }
    }

    memset(&ssd->dirty, 0, sizeof(ssd->dirty));
}

/*
 * Called from spice server thread context (via interface_release_resource)
 * We do *not* hold the global qemu mutex here, so extra care is needed
 * when calling qemu functions.  QEMU interfaces used:
 *    - g_free (underlying glibc free is re-entrant).
 */
void qemu_spice_destroy_update(SimpleSpiceDisplay *sdpy, SimpleSpiceUpdate *update)
{
    g_free(update->bitmap);
    g_free(update);
}

void qemu_spice_create_host_memslot(SimpleSpiceDisplay *ssd)
{
    QXLDevMemSlot memslot;

    dprint(1, "%s:\n", __FUNCTION__);

    memset(&memslot, 0, sizeof(memslot));
    memslot.slot_group_id = MEMSLOT_GROUP_HOST;
    memslot.virt_end = ~0;
    qemu_spice_add_memslot(ssd, &memslot, QXL_SYNC);
}

void qemu_spice_create_host_primary(SimpleSpiceDisplay *ssd)
{
    QXLDevSurfaceCreate surface;

    memset(&surface, 0, sizeof(surface));

    dprint(1, "%s: %dx%d\n", __FUNCTION__,
           ds_get_width(ssd->ds), ds_get_height(ssd->ds));

    surface.format     = SPICE_SURFACE_FMT_32_xRGB;
    surface.width      = ds_get_width(ssd->ds);
    surface.height     = ds_get_height(ssd->ds);
    surface.stride     = -surface.width * 4;
    surface.mouse_mode = true;
    surface.flags      = 0;
    surface.type       = 0;
    surface.mem        = (uintptr_t)ssd->buf;
    surface.group_id   = MEMSLOT_GROUP_HOST;

    qemu_spice_create_primary_surface(ssd, 0, &surface, QXL_SYNC);
}

void qemu_spice_destroy_host_primary(SimpleSpiceDisplay *ssd)
{
    dprint(1, "%s:\n", __FUNCTION__);

    qemu_spice_destroy_primary_surface(ssd, 0, QXL_SYNC);
}

void qemu_spice_display_init_common(SimpleSpiceDisplay *ssd, DisplayState *ds)
{
    ssd->ds = ds;
    qemu_mutex_init(&ssd->lock);
    QTAILQ_INIT(&ssd->updates);
    ssd->mouse_x = -1;
    ssd->mouse_y = -1;
    if (ssd->num_surfaces == 0) {
        ssd->num_surfaces = 1024;
    }
    ssd->bufsize = (16 * 1024 * 1024);
    ssd->buf = g_malloc(ssd->bufsize);
}

/* display listener callbacks */

void qemu_spice_display_update(SimpleSpiceDisplay *ssd,
                               int x, int y, int w, int h)
{
    QXLRect update_area;

    dprint(2, "%s: x %d y %d w %d h %d\n", __FUNCTION__, x, y, w, h);
    update_area.left = x,
    update_area.right = x + w;
    update_area.top = y;
    update_area.bottom = y + h;

    if (qemu_spice_rect_is_empty(&ssd->dirty)) {
        ssd->notify++;
    }
    qemu_spice_rect_union(&ssd->dirty, &update_area);
}

void qemu_spice_display_resize(SimpleSpiceDisplay *ssd)
{
    SimpleSpiceUpdate *update;

    dprint(1, "%s:\n", __FUNCTION__);

    memset(&ssd->dirty, 0, sizeof(ssd->dirty));
    if (ssd->surface) {
        pixman_image_unref(ssd->surface);
        ssd->surface = NULL;
        pixman_image_unref(ssd->mirror);
        ssd->mirror = NULL;
    }

    qemu_mutex_lock(&ssd->lock);
    while ((update = QTAILQ_FIRST(&ssd->updates)) != NULL) {
        QTAILQ_REMOVE(&ssd->updates, update, next);
        qemu_spice_destroy_update(ssd, update);
    }
    qemu_mutex_unlock(&ssd->lock);
    qemu_spice_destroy_host_primary(ssd);
    qemu_spice_create_host_primary(ssd);

    memset(&ssd->dirty, 0, sizeof(ssd->dirty));
    ssd->notify++;
}

void qemu_spice_cursor_refresh_unlocked(SimpleSpiceDisplay *ssd)
{
    if (ssd->cursor) {
        dpy_cursor_define(ssd->ds, ssd->cursor);
        cursor_put(ssd->cursor);
        ssd->cursor = NULL;
    }
    if (ssd->mouse_x != -1 && ssd->mouse_y != -1) {
        dpy_mouse_set(ssd->ds, ssd->mouse_x, ssd->mouse_y, 1);
        ssd->mouse_x = -1;
        ssd->mouse_y = -1;
    }
}

void qemu_spice_display_refresh(SimpleSpiceDisplay *ssd)
{
    dprint(3, "%s:\n", __func__);
    vga_hw_update();

    qemu_mutex_lock(&ssd->lock);
    if (QTAILQ_EMPTY(&ssd->updates)) {
        qemu_spice_create_update(ssd);
        ssd->notify++;
    }
    qemu_spice_cursor_refresh_unlocked(ssd);
    qemu_mutex_unlock(&ssd->lock);

    if (ssd->notify) {
        ssd->notify = 0;
        qemu_spice_wakeup(ssd);
        dprint(2, "%s: notify\n", __FUNCTION__);
    }
}

/* spice display interface callbacks */

static void interface_attach_worker(QXLInstance *sin, QXLWorker *qxl_worker)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);

    dprint(1, "%s:\n", __FUNCTION__);
    ssd->worker = qxl_worker;
}

static void interface_set_compression_level(QXLInstance *sin, int level)
{
    dprint(1, "%s:\n", __FUNCTION__);
    /* nothing to do */
}

static void interface_set_mm_time(QXLInstance *sin, uint32_t mm_time)
{
    dprint(3, "%s:\n", __FUNCTION__);
    /* nothing to do */
}

static void interface_get_init_info(QXLInstance *sin, QXLDevInitInfo *info)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);

    info->memslot_gen_bits = MEMSLOT_GENERATION_BITS;
    info->memslot_id_bits  = MEMSLOT_SLOT_BITS;
    info->num_memslots = NUM_MEMSLOTS;
    info->num_memslots_groups = NUM_MEMSLOTS_GROUPS;
    info->internal_groupslot_id = 0;
    info->qxl_ram_size = ssd->bufsize;
    info->n_surfaces = ssd->num_surfaces;
}

static int interface_get_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    SimpleSpiceUpdate *update;
    int ret = false;

    dprint(3, "%s:\n", __FUNCTION__);

    qemu_mutex_lock(&ssd->lock);
    update = QTAILQ_FIRST(&ssd->updates);
    if (update != NULL) {
        QTAILQ_REMOVE(&ssd->updates, update, next);
        *ext = update->ext;
        ret = true;
    }
    qemu_mutex_unlock(&ssd->lock);

    return ret;
}

static int interface_req_cmd_notification(QXLInstance *sin)
{
    dprint(1, "%s:\n", __FUNCTION__);
    return 1;
}

static void interface_release_resource(QXLInstance *sin,
                                       struct QXLReleaseInfoExt ext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    uintptr_t id;

    dprint(2, "%s:\n", __FUNCTION__);
    id = ext.info->id;
    qemu_spice_destroy_update(ssd, (void*)id);
}

static int interface_get_cursor_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    dprint(3, "%s:\n", __FUNCTION__);
    return false;
}

static int interface_req_cursor_notification(QXLInstance *sin)
{
    dprint(1, "%s:\n", __FUNCTION__);
    return 1;
}

static void interface_notify_update(QXLInstance *sin, uint32_t update_id)
{
    fprintf(stderr, "%s: abort()\n", __FUNCTION__);
    abort();
}

static int interface_flush_resources(QXLInstance *sin)
{
    fprintf(stderr, "%s: abort()\n", __FUNCTION__);
    abort();
    return 0;
}

static const QXLInterface dpy_interface = {
    .base.type               = SPICE_INTERFACE_QXL,
    .base.description        = "qemu simple display",
    .base.major_version      = SPICE_INTERFACE_QXL_MAJOR,
    .base.minor_version      = SPICE_INTERFACE_QXL_MINOR,

    .attache_worker          = interface_attach_worker,
    .set_compression_level   = interface_set_compression_level,
    .set_mm_time             = interface_set_mm_time,
    .get_init_info           = interface_get_init_info,

    /* the callbacks below are called from spice server thread context */
    .get_command             = interface_get_command,
    .req_cmd_notification    = interface_req_cmd_notification,
    .release_resource        = interface_release_resource,
    .get_cursor_command      = interface_get_cursor_command,
    .req_cursor_notification = interface_req_cursor_notification,
    .notify_update           = interface_notify_update,
    .flush_resources         = interface_flush_resources,
};

static SimpleSpiceDisplay sdpy;

static void display_update(struct DisplayState *ds, int x, int y, int w, int h)
{
    qemu_spice_display_update(&sdpy, x, y, w, h);
}

static void display_resize(struct DisplayState *ds)
{
    qemu_spice_display_resize(&sdpy);
}

static void display_refresh(struct DisplayState *ds)
{
    qemu_spice_display_refresh(&sdpy);
}

static DisplayChangeListener display_listener = {
    .dpy_gfx_update  = display_update,
    .dpy_gfx_resize  = display_resize,
    .dpy_refresh = display_refresh,
};

void qemu_spice_display_init(DisplayState *ds)
{
    assert(sdpy.ds == NULL);
    qemu_spice_display_init_common(&sdpy, ds);

    sdpy.qxl.base.sif = &dpy_interface.base;
    qemu_spice_add_interface(&sdpy.qxl.base);
    assert(sdpy.worker);

    qemu_spice_create_host_memslot(&sdpy);
    qemu_spice_create_host_primary(&sdpy);
    register_displaychangelistener(ds, &display_listener);
}
