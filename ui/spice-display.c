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

#include "qemu/osdep.h"
#include "ui/qemu-spice.h"
#include "qemu/timer.h"
#include "qemu/option.h"
#include "qemu/queue.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"
#include "trace.h"

#include "ui/spice-display.h"

bool spice_opengl;

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

    dest = pixman_image_create_bits(PIXMAN_LE_x8r8g8b8, bw, bh,
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
    int blocks = DIV_ROUND_UP(surface_width(ssd->ds), blksize);
    int dirty_top[blocks];
    int y, yoff1, yoff2, x, xoff, blk, bw;
    int bpp = surface_bytes_per_pixel(ssd->ds);
    uint8_t *guest, *mirror;

    if (qemu_spice_rect_is_empty(&ssd->dirty)) {
        return;
    };

    for (blk = 0; blk < blocks; blk++) {
        dirty_top[blk] = -1;
    }

    guest = surface_data(ssd->ds);
    mirror = (void *)pixman_image_get_data(ssd->mirror);
    for (y = ssd->dirty.top; y < ssd->dirty.bottom; y++) {
        yoff1 = y * surface_stride(ssd->ds);
        yoff2 = y * pixman_image_get_stride(ssd->mirror);
        for (x = ssd->dirty.left; x < ssd->dirty.right; x += blksize) {
            xoff = x * bpp;
            blk = x / blksize;
            bw = MIN(blksize, ssd->dirty.right - x);
            if (memcmp(guest + yoff1 + xoff,
                       mirror + yoff2 + xoff,
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
                                QEMUCursor *c,
                                int on)
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
        ccmd->u.set.position.x = ssd->ptr_x + ssd->hot_x;
        ccmd->u.set.position.y = ssd->ptr_y + ssd->hot_y;
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
    } else if (!on) {
        ccmd->type = QXL_CURSOR_HIDE;
    } else {
        ccmd->type = QXL_CURSOR_MOVE;
        ccmd->u.position.x = ssd->ptr_x + ssd->hot_x;
        ccmd->u.position.y = ssd->ptr_y + ssd->hot_y;
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

    memset(&memslot, 0, sizeof(memslot));
    memslot.slot_group_id = MEMSLOT_GROUP_HOST;
    memslot.virt_end = ~0;
    qemu_spice_add_memslot(ssd, &memslot, QXL_SYNC);
}

void qemu_spice_create_host_primary(SimpleSpiceDisplay *ssd)
{
    QXLDevSurfaceCreate surface;
    uint64_t surface_size;

    memset(&surface, 0, sizeof(surface));

    surface_size = (uint64_t) surface_width(ssd->ds) *
        surface_height(ssd->ds) * 4;
    assert(surface_size > 0);
    assert(surface_size < INT_MAX);
    if (ssd->bufsize < surface_size) {
        ssd->bufsize = surface_size;
        g_free(ssd->buf);
        ssd->buf = g_malloc(ssd->bufsize);
    }

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
}

/* display listener callbacks */

void qemu_spice_display_update(SimpleSpiceDisplay *ssd,
                               int x, int y, int w, int h)
{
    QXLRect update_area;

    trace_qemu_spice_display_update(ssd->qxl.id, x, y, w, h);
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

    if (surface && ssd->surface &&
        surface_width(surface) == pixman_image_get_width(ssd->surface) &&
        surface_height(surface) == pixman_image_get_height(ssd->surface) &&
        surface_format(surface) == pixman_image_get_format(ssd->surface)) {
        /* no-resize fast path: just swap backing store */
        trace_qemu_spice_display_surface(ssd->qxl.id,
                                         surface_width(surface),
                                         surface_height(surface),
                                         true);
        qemu_mutex_lock(&ssd->lock);
        ssd->ds = surface;
        pixman_image_unref(ssd->surface);
        ssd->surface = pixman_image_ref(ssd->ds->image);
        qemu_mutex_unlock(&ssd->lock);
        qemu_spice_display_update(ssd, 0, 0,
                                  surface_width(surface),
                                  surface_height(surface));
        return;
    }

    /* full mode switch */
    trace_qemu_spice_display_surface(ssd->qxl.id,
                                     surface ? surface_width(surface)  : 0,
                                     surface ? surface_height(surface) : 0,
                                     false);

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
        ssd->surface = pixman_image_ref(ssd->ds->image);
        ssd->mirror  = qemu_pixman_mirror_create(ssd->ds->format,
                                                 ssd->ds->image);
        qemu_spice_create_host_primary(ssd);
    }

    memset(&ssd->dirty, 0, sizeof(ssd->dirty));
    ssd->notify++;

    qemu_mutex_lock(&ssd->lock);
    if (ssd->cursor) {
        g_free(ssd->ptr_define);
        ssd->ptr_define = qemu_spice_create_cursor_update(ssd, ssd->cursor, 0);
    }
    qemu_mutex_unlock(&ssd->lock);
}

void qemu_spice_cursor_refresh_bh(void *opaque)
{
    SimpleSpiceDisplay *ssd = opaque;

    qemu_mutex_lock(&ssd->lock);
    if (ssd->cursor) {
        QEMUCursor *c = ssd->cursor;
        assert(ssd->dcl.con);
        cursor_get(c);
        qemu_mutex_unlock(&ssd->lock);
        dpy_cursor_define(ssd->dcl.con, c);
        qemu_mutex_lock(&ssd->lock);
        cursor_put(c);
    }

    if (ssd->mouse_x != -1 && ssd->mouse_y != -1) {
        int x, y;
        assert(ssd->dcl.con);
        x = ssd->mouse_x;
        y = ssd->mouse_y;
        ssd->mouse_x = -1;
        ssd->mouse_y = -1;
        qemu_mutex_unlock(&ssd->lock);
        dpy_mouse_set(ssd->dcl.con, x, y, 1);
    } else {
        qemu_mutex_unlock(&ssd->lock);
    }
}

void qemu_spice_display_refresh(SimpleSpiceDisplay *ssd)
{
    graphic_hw_update(ssd->dcl.con);

    qemu_mutex_lock(&ssd->lock);
    if (QTAILQ_EMPTY(&ssd->updates) && ssd->ds) {
        qemu_spice_create_update(ssd);
        ssd->notify++;
    }
    qemu_mutex_unlock(&ssd->lock);

    trace_qemu_spice_display_refresh(ssd->qxl.id, ssd->notify);
    if (ssd->notify) {
        ssd->notify = 0;
        qemu_spice_wakeup(ssd);
    }
}

/* spice display interface callbacks */

static void interface_attach_worker(QXLInstance *sin, QXLWorker *qxl_worker)
{
    /* nothing to do */
}

static void interface_set_compression_level(QXLInstance *sin, int level)
{
    /* nothing to do */
}

#if SPICE_NEEDS_SET_MM_TIME
static void interface_set_mm_time(QXLInstance *sin, uint32_t mm_time)
{
    /* nothing to do */
}
#endif

static void interface_get_init_info(QXLInstance *sin, QXLDevInitInfo *info)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);

    info->memslot_gen_bits = MEMSLOT_GENERATION_BITS;
    info->memslot_id_bits  = MEMSLOT_SLOT_BITS;
    info->num_memslots = NUM_MEMSLOTS;
    info->num_memslots_groups = NUM_MEMSLOTS_GROUPS;
    info->internal_groupslot_id = 0;
    info->qxl_ram_size = 16 * 1024 * 1024;
    info->n_surfaces = ssd->num_surfaces;
}

static int interface_get_command(QXLInstance *sin, QXLCommandExt *ext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    SimpleSpiceUpdate *update;
    int ret = false;

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
    return 1;
}

static void interface_release_resource(QXLInstance *sin,
                                       QXLReleaseInfoExt rext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    SimpleSpiceUpdate *update;
    SimpleSpiceCursor *cursor;
    QXLCommandExt *ext;

    ext = (void *)(intptr_t)(rext.info->id);
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

static int interface_get_cursor_command(QXLInstance *sin, QXLCommandExt *ext)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    int ret;

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
    return 1;
}

static void interface_notify_update(QXLInstance *sin, uint32_t update_id)
{
    fprintf(stderr, "%s: abort()\n", __func__);
    abort();
}

static int interface_flush_resources(QXLInstance *sin)
{
    fprintf(stderr, "%s: abort()\n", __func__);
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
    QXLCookie *cookie = (QXLCookie *)(uintptr_t)cookie_token;

    switch (cookie->type) {
#ifdef HAVE_SPICE_GL
    case QXL_COOKIE_TYPE_GL_DRAW_DONE:
    {
        SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
        qemu_bh_schedule(ssd->gl_unblock_bh);
        break;
    }
    case QXL_COOKIE_TYPE_IO:
        if (cookie->io == QXL_IO_MONITORS_CONFIG_ASYNC) {
            g_free(cookie->u.data);
        }
        break;
#endif
    default:
        /* should never be called, used in qxl native mode only */
        fprintf(stderr, "%s: abort()\n", __func__);
        abort();
    }
    g_free(cookie);
}

static void interface_set_client_capabilities(QXLInstance *sin,
                                              uint8_t client_present,
                                              uint8_t caps[58])
{
    /* nothing to do */
}

static int interface_client_monitors_config(QXLInstance *sin,
                                            VDAgentMonitorsConfig *mc)
{
    SimpleSpiceDisplay *ssd = container_of(sin, SimpleSpiceDisplay, qxl);
    QemuUIInfo info;
    int head;

    if (!dpy_ui_info_supported(ssd->dcl.con)) {
        return 0; /* == not supported by guest */
    }

    if (!mc) {
        return 1;
    }

    memset(&info, 0, sizeof(info));

    if (mc->num_of_monitors == 1) {
        /*
         * New spice-server version which filters the list of monitors
         * to only include those that belong to our display channel.
         *
         * single-head configuration (where filtering doesn't matter)
         * takes this code path too.
         */
        info.width  = mc->monitors[0].width;
        info.height = mc->monitors[0].height;
    } else {
        /*
         * Old spice-server which gives us all monitors, so we have to
         * figure ourself which entry we need.  Array index is the
         * channel_id, which is the qemu console index, see
         * qemu_spice_add_display_interface().
         */
        head = qemu_console_get_index(ssd->dcl.con);
        if (mc->num_of_monitors > head) {
            info.width  = mc->monitors[head].width;
            info.height = mc->monitors[head].height;
        }
    }

    trace_qemu_spice_ui_info(ssd->qxl.id, info.width, info.height);
    dpy_set_ui_info(ssd->dcl.con, &info);
    return 1;
}

static const QXLInterface dpy_interface = {
    .base.type               = SPICE_INTERFACE_QXL,
    .base.description        = "qemu simple display",
    .base.major_version      = SPICE_INTERFACE_QXL_MAJOR,
    .base.minor_version      = SPICE_INTERFACE_QXL_MINOR,

    .attache_worker          = interface_attach_worker,
    .set_compression_level   = interface_set_compression_level,
#if SPICE_NEEDS_SET_MM_TIME
    .set_mm_time             = interface_set_mm_time,
#endif
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
                           DisplaySurface *surface)
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
    ssd->ptr_y = y;
    g_free(ssd->ptr_move);
    ssd->ptr_move = qemu_spice_create_cursor_update(ssd, NULL, on);
    qemu_mutex_unlock(&ssd->lock);
    qemu_spice_wakeup(ssd);
}

static void display_mouse_define(DisplayChangeListener *dcl,
                                 QEMUCursor *c)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    qemu_mutex_lock(&ssd->lock);
    cursor_get(c);
    cursor_put(ssd->cursor);
    ssd->cursor = c;
    ssd->hot_x = c->hot_x;
    ssd->hot_y = c->hot_y;
    g_free(ssd->ptr_move);
    ssd->ptr_move = NULL;
    g_free(ssd->ptr_define);
    ssd->ptr_define = qemu_spice_create_cursor_update(ssd, c, 0);
    qemu_mutex_unlock(&ssd->lock);
    qemu_spice_wakeup(ssd);
}

static const DisplayChangeListenerOps display_listener_ops = {
    .dpy_name             = "spice",
    .dpy_gfx_update       = display_update,
    .dpy_gfx_switch       = display_switch,
    .dpy_gfx_check_format = qemu_pixman_check_format,
    .dpy_refresh          = display_refresh,
    .dpy_mouse_set        = display_mouse_set,
    .dpy_cursor_define    = display_mouse_define,
};

#ifdef HAVE_SPICE_GL

static void qemu_spice_gl_monitor_config(SimpleSpiceDisplay *ssd,
                                         int x, int y, int w, int h)
{
    QXLMonitorsConfig *config;
    QXLCookie *cookie;

    config = g_malloc0(sizeof(QXLMonitorsConfig) + sizeof(QXLHead));
    config->count = 1;
    config->max_allowed = 1;
    config->heads[0].x = x;
    config->heads[0].y = y;
    config->heads[0].width = w;
    config->heads[0].height = h;
    cookie = qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                            QXL_IO_MONITORS_CONFIG_ASYNC);
    cookie->u.data = config;

    spice_qxl_monitors_config_async(&ssd->qxl,
                                    (uintptr_t)config,
                                    MEMSLOT_GROUP_HOST,
                                    (uintptr_t)cookie);
}

static void qemu_spice_gl_block(SimpleSpiceDisplay *ssd, bool block)
{
    uint64_t timeout;

    if (block) {
        timeout = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        timeout += 1000; /* one sec */
        timer_mod(ssd->gl_unblock_timer, timeout);
    } else {
        timer_del(ssd->gl_unblock_timer);
    }
    graphic_hw_gl_block(ssd->dcl.con, block);
}

static void qemu_spice_gl_unblock_bh(void *opaque)
{
    SimpleSpiceDisplay *ssd = opaque;

    qemu_spice_gl_block(ssd, false);
}

static void qemu_spice_gl_block_timer(void *opaque)
{
    warn_report("spice: no gl-draw-done within one second");
}

static void spice_gl_refresh(DisplayChangeListener *dcl)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);
    uint64_t cookie;

    if (!ssd->ds || qemu_console_is_gl_blocked(ssd->dcl.con)) {
        return;
    }

    graphic_hw_update(dcl->con);
    if (ssd->gl_updates && ssd->have_surface) {
        qemu_spice_gl_block(ssd, true);
        cookie = (uintptr_t)qxl_cookie_new(QXL_COOKIE_TYPE_GL_DRAW_DONE, 0);
        spice_qxl_gl_draw_async(&ssd->qxl, 0, 0,
                                surface_width(ssd->ds),
                                surface_height(ssd->ds),
                                cookie);
        ssd->gl_updates = 0;
    }
}

static void spice_gl_update(DisplayChangeListener *dcl,
                            int x, int y, int w, int h)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    surface_gl_update_texture(ssd->gls, ssd->ds, x, y, w, h);
    ssd->gl_updates++;
}

static void spice_gl_switch(DisplayChangeListener *dcl,
                            struct DisplaySurface *new_surface)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);
    EGLint stride, fourcc;
    int fd;

    if (ssd->ds) {
        surface_gl_destroy_texture(ssd->gls, ssd->ds);
    }
    ssd->ds = new_surface;
    if (ssd->ds) {
        surface_gl_create_texture(ssd->gls, ssd->ds);
        fd = egl_get_fd_for_texture(ssd->ds->texture,
                                    &stride, &fourcc);
        if (fd < 0) {
            surface_gl_destroy_texture(ssd->gls, ssd->ds);
            return;
        }

        trace_qemu_spice_gl_surface(ssd->qxl.id,
                                    surface_width(ssd->ds),
                                    surface_height(ssd->ds),
                                    fourcc);

        /* note: spice server will close the fd */
        spice_qxl_gl_scanout(&ssd->qxl, fd,
                             surface_width(ssd->ds),
                             surface_height(ssd->ds),
                             stride, fourcc, false);
        ssd->have_surface = true;
        ssd->have_scanout = false;

        qemu_spice_gl_monitor_config(ssd, 0, 0,
                                     surface_width(ssd->ds),
                                     surface_height(ssd->ds));
    }
}

static QEMUGLContext qemu_spice_gl_create_context(DisplayChangeListener *dcl,
                                                  QEMUGLParams *params)
{
    eglMakeCurrent(qemu_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   qemu_egl_rn_ctx);
    return qemu_egl_create_context(dcl, params);
}

static void qemu_spice_gl_scanout_disable(DisplayChangeListener *dcl)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    trace_qemu_spice_gl_scanout_disable(ssd->qxl.id);
    spice_qxl_gl_scanout(&ssd->qxl, -1, 0, 0, 0, 0, false);
    qemu_spice_gl_monitor_config(ssd, 0, 0, 0, 0);
    ssd->have_surface = false;
    ssd->have_scanout = false;
}

static void qemu_spice_gl_scanout_texture(DisplayChangeListener *dcl,
                                          uint32_t tex_id,
                                          bool y_0_top,
                                          uint32_t backing_width,
                                          uint32_t backing_height,
                                          uint32_t x, uint32_t y,
                                          uint32_t w, uint32_t h)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);
    EGLint stride = 0, fourcc = 0;
    int fd = -1;

    assert(tex_id);
    fd = egl_get_fd_for_texture(tex_id, &stride, &fourcc);
    if (fd < 0) {
        fprintf(stderr, "%s: failed to get fd for texture\n", __func__);
        return;
    }
    trace_qemu_spice_gl_scanout_texture(ssd->qxl.id, w, h, fourcc);

    /* note: spice server will close the fd */
    spice_qxl_gl_scanout(&ssd->qxl, fd, backing_width, backing_height,
                         stride, fourcc, y_0_top);
    qemu_spice_gl_monitor_config(ssd, x, y, w, h);
    ssd->have_surface = false;
    ssd->have_scanout = true;
}

static void qemu_spice_gl_scanout_dmabuf(DisplayChangeListener *dcl,
                                         QemuDmaBuf *dmabuf)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    ssd->guest_dmabuf = dmabuf;
    ssd->guest_dmabuf_refresh = true;

    ssd->have_surface = false;
    ssd->have_scanout = true;
}

static void qemu_spice_gl_cursor_dmabuf(DisplayChangeListener *dcl,
                                        QemuDmaBuf *dmabuf, bool have_hot,
                                        uint32_t hot_x, uint32_t hot_y)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    ssd->have_hot = have_hot;
    ssd->hot_x = hot_x;
    ssd->hot_y = hot_y;

    trace_qemu_spice_gl_cursor(ssd->qxl.id, dmabuf != NULL, have_hot);
    if (dmabuf) {
        egl_dmabuf_import_texture(dmabuf);
        if (!dmabuf->texture) {
            return;
        }
        egl_fb_setup_for_tex(&ssd->cursor_fb, dmabuf->width, dmabuf->height,
                             dmabuf->texture, false);
    } else {
        egl_fb_destroy(&ssd->cursor_fb);
    }
}

static void qemu_spice_gl_cursor_position(DisplayChangeListener *dcl,
                                          uint32_t pos_x, uint32_t pos_y)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    qemu_mutex_lock(&ssd->lock);
    ssd->ptr_x = pos_x;
    ssd->ptr_y = pos_y;
    qemu_mutex_unlock(&ssd->lock);
}

static void qemu_spice_gl_release_dmabuf(DisplayChangeListener *dcl,
                                         QemuDmaBuf *dmabuf)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);

    if (ssd->guest_dmabuf == dmabuf) {
        ssd->guest_dmabuf = NULL;
        ssd->guest_dmabuf_refresh = false;
    }
    egl_dmabuf_release_texture(dmabuf);
}

static void qemu_spice_gl_update(DisplayChangeListener *dcl,
                                 uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    SimpleSpiceDisplay *ssd = container_of(dcl, SimpleSpiceDisplay, dcl);
    EGLint stride = 0, fourcc = 0;
    bool render_cursor = false;
    bool y_0_top = false; /* FIXME */
    uint64_t cookie;
    int fd;

    if (!ssd->have_scanout) {
        return;
    }

    if (ssd->cursor_fb.texture) {
        render_cursor = true;
    }
    if (ssd->render_cursor != render_cursor) {
        ssd->render_cursor = render_cursor;
        ssd->guest_dmabuf_refresh = true;
        egl_fb_destroy(&ssd->blit_fb);
    }

    if (ssd->guest_dmabuf_refresh) {
        QemuDmaBuf *dmabuf = ssd->guest_dmabuf;
        if (render_cursor) {
            egl_dmabuf_import_texture(dmabuf);
            if (!dmabuf->texture) {
                return;
            }

            /* source framebuffer */
            egl_fb_setup_for_tex(&ssd->guest_fb,
                                 dmabuf->width, dmabuf->height,
                                 dmabuf->texture, false);

            /* dest framebuffer */
            if (ssd->blit_fb.width  != dmabuf->width ||
                ssd->blit_fb.height != dmabuf->height) {
                trace_qemu_spice_gl_render_dmabuf(ssd->qxl.id, dmabuf->width,
                                                  dmabuf->height);
                egl_fb_destroy(&ssd->blit_fb);
                egl_fb_setup_new_tex(&ssd->blit_fb,
                                     dmabuf->width, dmabuf->height);
                fd = egl_get_fd_for_texture(ssd->blit_fb.texture,
                                            &stride, &fourcc);
                spice_qxl_gl_scanout(&ssd->qxl, fd,
                                     dmabuf->width, dmabuf->height,
                                     stride, fourcc, false);
            }
        } else {
            trace_qemu_spice_gl_forward_dmabuf(ssd->qxl.id,
                                               dmabuf->width, dmabuf->height);
            /* note: spice server will close the fd, so hand over a dup */
            spice_qxl_gl_scanout(&ssd->qxl, dup(dmabuf->fd),
                                 dmabuf->width, dmabuf->height,
                                 dmabuf->stride, dmabuf->fourcc,
                                 dmabuf->y0_top);
        }
        qemu_spice_gl_monitor_config(ssd, 0, 0, dmabuf->width, dmabuf->height);
        ssd->guest_dmabuf_refresh = false;
    }

    if (render_cursor) {
        int x, y;
        qemu_mutex_lock(&ssd->lock);
        x = ssd->ptr_x;
        y = ssd->ptr_y;
        qemu_mutex_unlock(&ssd->lock);
        egl_texture_blit(ssd->gls, &ssd->blit_fb, &ssd->guest_fb,
                         !y_0_top);
        egl_texture_blend(ssd->gls, &ssd->blit_fb, &ssd->cursor_fb,
                          !y_0_top, x, y);
        glFlush();
    }

    trace_qemu_spice_gl_update(ssd->qxl.id, w, h, x, y);
    qemu_spice_gl_block(ssd, true);
    cookie = (uintptr_t)qxl_cookie_new(QXL_COOKIE_TYPE_GL_DRAW_DONE, 0);
    spice_qxl_gl_draw_async(&ssd->qxl, x, y, w, h, cookie);
}

static const DisplayChangeListenerOps display_listener_gl_ops = {
    .dpy_name                = "spice-egl",
    .dpy_gfx_update          = spice_gl_update,
    .dpy_gfx_switch          = spice_gl_switch,
    .dpy_gfx_check_format    = console_gl_check_format,
    .dpy_refresh             = spice_gl_refresh,
    .dpy_mouse_set           = display_mouse_set,
    .dpy_cursor_define       = display_mouse_define,

    .dpy_gl_ctx_create       = qemu_spice_gl_create_context,
    .dpy_gl_ctx_destroy      = qemu_egl_destroy_context,
    .dpy_gl_ctx_make_current = qemu_egl_make_context_current,
    .dpy_gl_ctx_get_current  = qemu_egl_get_current_context,

    .dpy_gl_scanout_disable  = qemu_spice_gl_scanout_disable,
    .dpy_gl_scanout_texture  = qemu_spice_gl_scanout_texture,
    .dpy_gl_scanout_dmabuf   = qemu_spice_gl_scanout_dmabuf,
    .dpy_gl_cursor_dmabuf    = qemu_spice_gl_cursor_dmabuf,
    .dpy_gl_cursor_position  = qemu_spice_gl_cursor_position,
    .dpy_gl_release_dmabuf   = qemu_spice_gl_release_dmabuf,
    .dpy_gl_update           = qemu_spice_gl_update,
};

#endif /* HAVE_SPICE_GL */

static void qemu_spice_display_init_one(QemuConsole *con)
{
    SimpleSpiceDisplay *ssd = g_new0(SimpleSpiceDisplay, 1);

    qemu_spice_display_init_common(ssd);

    ssd->dcl.ops = &display_listener_ops;
#ifdef HAVE_SPICE_GL
    if (spice_opengl) {
        ssd->dcl.ops = &display_listener_gl_ops;
        ssd->gl_unblock_bh = qemu_bh_new(qemu_spice_gl_unblock_bh, ssd);
        ssd->gl_unblock_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                             qemu_spice_gl_block_timer, ssd);
        ssd->gls = qemu_gl_init_shader();
        ssd->have_surface = false;
        ssd->have_scanout = false;
    }
#endif
    ssd->dcl.con = con;

    ssd->qxl.base.sif = &dpy_interface.base;
    qemu_spice_add_display_interface(&ssd->qxl, con);
    qemu_spice_create_host_memslot(ssd);

    register_displaychangelistener(&ssd->dcl);
}

void qemu_spice_display_init(void)
{
    QemuOptsList *olist = qemu_find_opts("spice");
    QemuOpts *opts = QTAILQ_FIRST(&olist->head);
    QemuConsole *spice_con, *con;
    const char *str;
    int i;

    str = qemu_opt_get(opts, "display");
    if (str) {
        int head = qemu_opt_get_number(opts, "head", 0);
        Error *err = NULL;

        spice_con = qemu_console_lookup_by_device_name(str, head, &err);
        if (err) {
            error_report("Failed to lookup display/head");
            exit(1);
        }
    } else {
        spice_con = NULL;
    }

    for (i = 0;; i++) {
        con = qemu_console_lookup_by_index(i);
        if (!con || !qemu_console_is_graphic(con)) {
            break;
        }
        if (qemu_spice_have_display_interface(con)) {
            continue;
        }
        if (spice_con != NULL && spice_con != con) {
            continue;
        }
        qemu_spice_display_init_one(con);
    }
}
