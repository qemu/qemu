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
#include "ui/qemu-spice.h"
#include "qemu/timer.h"
#include "qemu/queue.h"
#include "monitor/monitor.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"
#include "trace.h"

#include "ui/spice-display.h"

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
        spice_qxl_add_memslot(&ssd->qxl, memslot);
    }
}

void qemu_spice_del_memslot(SimpleSpiceDisplay *ssd, uint32_t gid, uint32_t sid)
{
    trace_qemu_spice_del_memslot(ssd->qxl.id, gid, sid);
    spice_qxl_del_memslot(&ssd->qxl, gid, sid);
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
        spice_qxl_create_primary_surface(&ssd->qxl, id, surface);
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
        spice_qxl_destroy_primary_surface(&ssd->qxl, id);
    }
}

void qemu_spice_wakeup(SimpleSpiceDisplay *ssd)
{
    trace_qemu_spice_wakeup(ssd->qxl.id);
    spice_qxl_wakeup(&ssd->qxl);
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
    drawable->release_info.id = (uintptr_t)(&update->ext);
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
    int blocks = (surface_width(ssd->ds) + blksize - 1) / blksize;
    int dirty_top[blocks];
    int y, yoff, x, xoff, blk, bw;
    int bpp = surface_bytes_per_pixel(ssd->ds);
    uint8_t *guest, *mirror;

    if (qemu_spice_rect_is_empty(&ssd->dirty)) {
        return;
    };

    if (ssd->surface == NULL) {
        ssd->surface = pixman_image_ref(ssd->ds->image);
        ssd->mirror  = qemu_pixman_mirror_create(ssd->ds->format,
                                                 ssd->ds->image);
    }

    for (blk = 0; blk < blocks; blk++) {
        dirty_top[blk] = -1;
    }

    guest = surface_data(ssd->ds);
    mirror = (void *)pixman_image_get_data(ssd->mirror);
    for (y = ssd->dirty.top; y < ssd->dirty.bottom; y++) {
        yoff = y * surface_stride(ssd->ds);
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

static SimpleSpiceCursor*
qemu_spice_create_cursor_update(SimpleSpiceDisplay *ssd,
                                QEMUCursor *c)
{
    size_t size = c ? c->width * c->height * 4 : 0;
    SimpleSpiceCursor *update;
    QXLCursorCmd *ccmd;
    QXLCursor *cursor;
    QXLCommand *cmd;

    update   = g_malloc0(sizeof(*update) + size);
    ccmd     = &update->cmd;
    cursor   = &update->cursor;
    cmd      = &update->ext.cmd;

    if (c) {
        ccmd->type = QXL_CURSOR_SET;
        ccmd->u.set.position.x = ssd->ptr_x;
        ccmd->u.set.position.y = ssd->ptr_y;
        ccmd->u.set.visible    = true;
        ccmd->u.set.shape      = (uintptr_t)cursor;
        cursor->header.unique     = ssd->unique++;
        cursor->header.type       = SPICE_CURSOR_TYPE_ALPHA;
        cursor->header.width      = c->width;
        cursor->header.height     = c->height;
        cursor->header.hot_spot_x = c->hot_x;
        cursor->header.hot_spot_y = c->hot_y;
        cursor->data_size         = size;
        cursor->chunk.data_size   = size;
        memcpy(cursor->chunk.data, c->data, size);
    } else {
        ccmd->type = QXL_CURSOR_MOVE;
        ccmd->u.position.x = ssd->ptr_x;
        ccmd->u.position.y = ssd->ptr_y;
    }
    ccmd->release_info.id = (uintptr_t)(&update->ext);

    cmd->type = QXL_CMD_CURSOR;
    cmd->data = (uintptr_t)ccmd;

    return update;
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

    dprint(1, "%s/%d:\n", __func__, ssd->qxl.id);

    memset(&memslot, 0, sizeof(memslot));
    memslot.slot_group_id = MEMSLOT_GROUP_HOST;
    memslot.virt_end = ~0;
    qemu_spice_add_memslot(ssd, &memslot, QXL_SYNC);
}

void qemu_spice_create_host_primary(SimpleSpiceDisplay *ssd)
{
    QXLDevSurfaceCreate surface;

    memset(&surface, 0, sizeof(surface));

    dprint(1, "%s/%d: %dx%d\n", __func__, ssd->qxl.id,
           surface_width(ssd->ds), surface_height(ssd->ds));

    surface.format     = SPICE_SURFACE_FMT_32_xRGB;
    surface.width      = surface_width(ssd->ds);
    surface.height     = surface_height(ssd->ds);
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
    dprint(1, "%s/%d:\n", __func__, ssd->qxl.id);

    qemu_spice_destroy_primary_surface(ssd, 0, QXL_SYNC);
}

void qemu_spice_display_init_common(SimpleSpiceDisplay *ssd)
{
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

    dprint(2, "%s/%d: x %d y %d w %d h %d\n", __func__,
           ssd->qxl.id, x, y, w, h);
    update_area.left = x,
    update_area.right = x + w;
    update_area.top = y;
    update_area.bottom = y + h;

    if (qemu_spice_rect_is_empty(&ssd->dirty)) {
        ssd->notify++;
    }
    qemu_spice_rect_union(&ssd->dirty, &update_area);
}

void qemu_spice_display_switch(SimpleSpiceDisplay *ssd,
                               DisplaySurface *surface)
{
    SimpleSpiceUpdate *update;
    bool need_destroy;

    dprint(1, "%s/%d:\n", __func__, ssd->qxl.id);

    memset(&ssd->dirty, 0, sizeof(ssd->dirty));
    if (ssd->surface) {
        pixman_image_unref(ssd->surface);
        ssd->surface = NULL;
        pixman_image_unref(ssd->mirror);
        ssd->mirror = NULL;
    }

    qemu_mutex_lock(&ssd->lock);
    need_destroy = (ssd->ds != NULL);
    ssd->ds = surface;
    while ((update = QTAILQ_FIRST(&ssd->updates)) != NULL) {
        QTAILQ_REMOVE(&ssd->updates, update, next);
        qemu_spice_destroy_update(ssd, update);
    }
    qemu_mutex_unlock(&ssd->lock);
    if (need_destroy) {
        qemu_spice_destroy_host_primary(ssd);
    }
    if (ssd->ds) {
        qemu_spice_create_host_primary(ssd);
    }

    memset(&ssd->dirty, 0, sizeof(ssd->dirty));
    ssd->notify++;
}

void qemu_spice_cursor_refresh_unlocked(SimpleSpiceDisplay *ssd)
{
    if (ssd->cursor) {
        assert(ssd->dcl.con);
        dpy_cursor_define(ssd->dcl.con, ssd->cursor);
        cursor_put(ssd->cursor);
        ssd->cursor = NULL;
    }
    if (ssd->mouse_x != -1 && ssd->mouse_y != -1) {
        assert(ssd->dcl.con);
        dpy_mouse_set(ssd->dcl.con, ssd->mouse_x, ssd->mouse_y, 1);
        ssd->mouse_x = -1;
        ssd->mouse_y = -1;
    }
}

void qemu_spice_display_refresh(SimpleSpiceDisplay *ssd)
{
    dprint(3, "%s/%d:\n", __func__, ssd->qxl.id);
    graphic_hw_update(ssd->dcl.con);

    qemu_mutex_lock(&ssd->lock);
    if (QTAILQ_EMPTY(&ssd->updates) && ssd->ds) {
        qemu_spice_create_update(ssd);
        ssd->notify++;
    }
    qemu_spice_cursor_refresh_unlocked(ssd);
    qemu_mutex_unlock(&ssd->lock);

    if (ssd->notify) {
        ssd->notify = 0;
        qemu_spice_wakeup(ssd);
        dprint(2, "%s/%d: notify\n", __func__, ssd->qxl.id);
    }
}

/* spice display interface callbacks */

static void interface_attach_worker(QXLInstance *sin, QXLWorker *qxl_worker)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);

    dprint(1, "%s/%d:\n", __func__, ssd->qxl.id);
    ssd->worker = qxl_worker;
}

static void interface_set_compression_level(QXLInstance *sin, int level)
{
    dprint(1, "%s/%d:\n", __func__, sin->id);
    /* nothing to do */
}

static void interface_set_mm_time(QXLInstance *sin, uint32_t mm_time)
{
    dprint(3, "%s/%d:\n", __func__, sin->id);
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

    dprint(3, "%s/%d:\n", __func__, ssd->qxl.id);

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
    dprint(1, "%s/%d:\n", __func__, sin->id);
    return 1;
}

static void interface_release_resource(QXLInstance *sin,
                                       struct QXLReleaseInfoExt rext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    SimpleSpiceUpdate *update;
    SimpleSpiceCursor *cursor;
    QXLCommandExt *ext;

    dprint(2, "%s/%d:\n", __func__, ssd->qxl.id);
    ext = (void *)(rext.info->id);
    switch (ext->cmd.type) {
    case QXL_CMD_DRAW:
        update = container_of(ext, SimpleSpiceUpdate, ext);
        qemu_spice_destroy_update(ssd, update);
        break;
    case QXL_CMD_CURSOR:
        cursor = container_of(ext, SimpleSpiceCursor, ext);
        g_free(cursor);
        break;
    default:
        g_assert_not_reached();
    }
}

static int interface_get_cursor_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    int ret;

    dprint(3, "%s/%d:\n", __func__, ssd->qxl.id);

    qemu_mutex_lock(&ssd->lock);
    if (ssd->ptr_define) {
        *ext = ssd->ptr_define->ext;
        ssd->ptr_define = NULL;
        ret = true;
    } else if (ssd->ptr_move) {
        *ext = ssd->ptr_move->ext;
        ssd->ptr_move = NULL;
        ret = true;
    } else {
        ret = false;
    }
    qemu_mutex_unlock(&ssd->lock);
    return ret;
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

static void interface_update_area_complete(QXLInstance *sin,
        uint32_t surface_id,
        QXLRect *dirty, uint32_t num_updated_rects)
{
    /* should never be called, used in qxl native mode only */
    fprintf(stderr, "%s: abort()\n", __func__);
    abort();
}

/* called from spice server thread context only */
static void interface_async_complete(QXLInstance *sin, uint64_t cookie_token)
{
    /* should never be called, used in qxl native mode only */
    fprintf(stderr, "%s: abort()\n", __func__);
    abort();
}

static void interface_set_client_capabilities(QXLInstance *sin,
                                              uint8_t client_present,
                                              uint8_t caps[58])
{
    dprint(3, "%s:\n", __func__);
}

static int interface_client_monitors_config(QXLInstance *sin,
                                            VDAgentMonitorsConfig *mc)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    QemuUIInfo info;
    int rc;

    if (!mc) {
        return 1;
    }

    /*
     * FIXME: multihead is tricky due to the way
     * spice has multihead implemented.
     */
    memset(&info, 0, sizeof(info));
    if (mc->num_of_monitors > 0) {
        info.width  = mc->monitors[0].width;
        info.height = mc->monitors[0].height;
    }
    rc = dpy_set_ui_info(ssd->dcl.con, &info);
    dprint(1, "%s/%d: size %dx%d, rc %d   <---   ==========================\n",
           __func__, ssd->qxl.id, info.width, info.height, rc);
    if (rc != 0) {
        return 0; /* == not supported by guest */
    } else {
        return 1;
    }
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
    .async_complete          = interface_async_complete,
    .update_area_complete    = interface_update_area_complete,
    .set_client_capabilities = interface_set_client_capabilities,
    .client_monitors_config  = interface_client_monitors_config,
};

static void display_update(DisplayChangeListener *dcl,
                           int x, int y, int w, int h)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);
    qemu_spice_display_update(ssd, x, y, w, h);
}

static void display_switch(DisplayChangeListener *dcl,
                           struct DisplaySurface *surface)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);
    qemu_spice_display_switch(ssd, surface);
}

static void display_refresh(DisplayChangeListener *dcl)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);
    qemu_spice_display_refresh(ssd);
}

static void display_mouse_set(DisplayChangeListener *dcl,
                              int x, int y, int on)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    qemu_mutex_lock(&ssd->lock);
    ssd->ptr_x = x;
    ssd->ptr_y = x;
    if (ssd->ptr_move) {
        g_free(ssd->ptr_move);
    }
    ssd->ptr_move = qemu_spice_create_cursor_update(ssd, NULL);
    qemu_mutex_unlock(&ssd->lock);
}

static void display_mouse_define(DisplayChangeListener *dcl,
                                 QEMUCursor *c)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    qemu_mutex_lock(&ssd->lock);
    if (ssd->ptr_move) {
        g_free(ssd->ptr_move);
        ssd->ptr_move = NULL;
    }
    if (ssd->ptr_define) {
        g_free(ssd->ptr_define);
    }
    ssd->ptr_define = qemu_spice_create_cursor_update(ssd, c);
    qemu_mutex_unlock(&ssd->lock);
}

static const DisplayChangeListenerOps display_listener_ops = {
    .dpy_name          = "spice",
    .dpy_gfx_update    = display_update,
    .dpy_gfx_switch    = display_switch,
    .dpy_refresh       = display_refresh,
    .dpy_mouse_set     = display_mouse_set,
    .dpy_cursor_define = display_mouse_define,
};

static void qemu_spice_display_init_one(QemuConsole *con)
{
    SimpleSpiceDisplay *ssd = g_new0(SimpleSpiceDisplay, 1);

    qemu_spice_display_init_common(ssd);

    ssd->qxl.base.sif = &dpy_interface.base;
    qemu_spice_add_display_interface(&ssd->qxl, con);
    assert(ssd->worker);

    qemu_spice_create_host_memslot(ssd);

    ssd->dcl.ops = &display_listener_ops;
    ssd->dcl.con = con;
    register_displaychangelistener(&ssd->dcl);
}

void qemu_spice_display_init(void)
{
    QemuConsole *con;
    int i;

    for (i = 0;; i++) {
        con = qemu_console_lookup_by_index(i);
        if (!con || !qemu_console_is_graphic(con)) {
            break;
        }
        if (qemu_spice_have_display_interface(con)) {
            continue;
        }
        qemu_spice_display_init_one(con);
    }
}
