/*
 * qxl local rendering (aka display on sdl/vnc)
 *
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * maintained by Gerd Hoffmann <kraxel@redhat.com>
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
#include "qxl.h"
#include "system/runstate.h"
#include "trace.h"

static void qxl_blit(PCIQXLDevice *qxl, QXLRect *rect)
{
    DisplaySurface *surface = qemu_console_surface(qxl->vga.con);
    int dst_stride = surface_stride(surface);
    uint8_t *dst = surface_data(surface);
    uint8_t *src;
    int len, i;

    if (!surface_is_allocated(surface)) {
        return;
    }
    trace_qxl_render_blit(qxl->guest_primary.qxl_stride,
            rect->left, rect->right, rect->top, rect->bottom);
    src = qxl->guest_primary.data;
    if (qxl->guest_primary.qxl_stride < 0) {
        /* qxl surface is upside down, walk src scanlines
         * in reverse order to flip it */
        src += (qxl->guest_primary.surface.height - rect->top - 1) *
            qxl->guest_primary.abs_stride;
    } else {
        src += rect->top * qxl->guest_primary.abs_stride;
    }
    dst += rect->top  * dst_stride;
    src += rect->left * qxl->guest_primary.bytes_pp;
    dst += rect->left * qxl->guest_primary.bytes_pp;
    len  = (rect->right - rect->left) * qxl->guest_primary.bytes_pp;

    for (i = rect->top; i < rect->bottom; i++) {
        memcpy(dst, src, len);
        dst += dst_stride;
        src += qxl->guest_primary.qxl_stride;
    }
}

void qxl_render_resize(PCIQXLDevice *qxl)
{
    QXLSurfaceCreate *sc = &qxl->guest_primary.surface;

    qxl->guest_primary.qxl_stride = le32_to_cpu(sc->stride);
    qxl->guest_primary.abs_stride = abs(qxl->guest_primary.qxl_stride);
    qxl->guest_primary.resized++;
    /* fallback to default bpp if format is unknown */
    qxl_format_bpp(qxl, le32_to_cpu(sc->format),
                   &qxl->guest_primary.bytes_pp,
                   &qxl->guest_primary.bits_pp);
}

static void qxl_set_rect_to_surface(PCIQXLDevice *qxl, QXLRect *area)
{
    area->left   = 0;
    area->right  = qxl->guest_primary.surface.width;
    area->top    = 0;
    area->bottom = qxl->guest_primary.surface.height;
}

static void qxl_render_update_area_unlocked(PCIQXLDevice *qxl)
{
    VGACommonState *vga = &qxl->vga;
    DisplaySurface *surface;
    int width = qxl->guest_head0_width ?: qxl->guest_primary.surface.width;
    int height = qxl->guest_head0_height ?: qxl->guest_primary.surface.height;
    uint64_t map_height;
    int i;

    if (width <= 0 || height <= 0) {
        goto end;
    }

    if (qxl->guest_primary.bytes_pp > 0) {
        int max_width = qxl->guest_primary.abs_stride
                        / qxl->guest_primary.bytes_pp;
        width = MIN(width, max_width);
    }

    if (qxl->guest_primary.qxl_stride < 0) {
        /* qxl_blit() uses the primary height to find the first scanline. */
        height = MIN(height, (int)qxl->guest_primary.surface.height);
    }

    if (qxl->guest_primary.abs_stride > 0) {
        int max_height = qxl->vgamem_size / qxl->guest_primary.abs_stride;
        height = MIN(height, max_height);
    }

    /*
     * height limits the visible update, while map_height is the guest memory
     * span validated by qxl_phys2virt().  With a negative stride qxl_blit()
     * addresses scanlines from the declared primary height, so a shorter
     * monitor still requires validating the full primary surface.
     */
    map_height = qxl->guest_primary.qxl_stride < 0 ?
                 qxl->guest_primary.surface.height : height;

    if (qxl->guest_primary.resized) {
        qxl->guest_primary.resized = 0;
        qxl->guest_primary.data = qxl_phys2virt(qxl,
                                                qxl->guest_primary.surface.mem,
                                                MEMSLOT_GROUP_GUEST,
                                                qxl->guest_primary.abs_stride
                                                * map_height);
        if (!qxl->guest_primary.data) {
            goto end;
        }
        qxl_set_rect_to_surface(qxl, &qxl->dirty[0]);
        qxl->num_dirty_rects = 1;
        trace_qxl_render_guest_primary_resized(
               width,
               height,
               qxl->guest_primary.qxl_stride,
               qxl->guest_primary.bytes_pp,
               qxl->guest_primary.bits_pp);
        if (qxl->guest_primary.qxl_stride > 0) {
            pixman_format_code_t format =
                qemu_default_pixman_format(qxl->guest_primary.bits_pp, true);
            surface = qemu_create_displaysurface_from
                (width,
                 height,
                 format,
                 qxl->guest_primary.abs_stride,
                 qxl->guest_primary.data);
        } else {
            surface = qemu_create_displaysurface
                (width,
                 height);
        }
        qemu_console_set_surface(vga->con, surface);
    }

    if (!qxl->guest_primary.data) {
        goto end;
    }
    for (i = 0; i < qxl->num_dirty_rects; i++) {
        if (qemu_spice_rect_is_empty(qxl->dirty+i)) {
            break;
        }
        if (qxl->dirty[i].left < 0 ||
            qxl->dirty[i].top < 0 ||
            qxl->dirty[i].left > qxl->dirty[i].right ||
            qxl->dirty[i].top > qxl->dirty[i].bottom ||
            qxl->dirty[i].right > width ||
            qxl->dirty[i].bottom > height) {
            continue;
        }
        qxl_blit(qxl, qxl->dirty+i);
        qemu_console_update(vga->con,
                            qxl->dirty[i].left, qxl->dirty[i].top,
                            qxl->dirty[i].right - qxl->dirty[i].left,
                            qxl->dirty[i].bottom - qxl->dirty[i].top);
    }
    qxl->num_dirty_rects = 0;

end:
    if (qxl->render_update_cookie_num == 0) {
        qemu_console_hw_update_done(qxl->ssd.dcl.con);
    }
}

/*
 * use ssd.lock to protect render_update_cookie_num.
 * qxl_render_update is called by io thread or vcpu thread, and the completion
 * callbacks are called by spice_server thread, deferring to bh called from the
 * io thread.
 */
bool qxl_render_update(PCIQXLDevice *qxl)
{
    QXLCookie *cookie;

    qemu_mutex_lock(&qxl->ssd.lock);

    if (!runstate_is_running() || !qxl->guest_primary.commands ||
        qxl->mode == QXL_MODE_UNDEFINED) {
        qxl_render_update_area_unlocked(qxl);
        qemu_mutex_unlock(&qxl->ssd.lock);
        return true;
    }

    qxl->guest_primary.commands = 0;
    qxl->render_update_cookie_num++;
    qemu_mutex_unlock(&qxl->ssd.lock);
    cookie = qxl_cookie_new(QXL_COOKIE_TYPE_RENDER_UPDATE_AREA,
                            0);
    qxl_set_rect_to_surface(qxl, &cookie->u.render.area);
    qxl_spice_update_area(qxl, 0, &cookie->u.render.area, NULL,
                          0, 1 /* clear_dirty_region */, QXL_ASYNC, cookie);
    return false;
}

void qxl_render_update_area_bh(void *opaque)
{
    PCIQXLDevice *qxl = opaque;

    qemu_mutex_lock(&qxl->ssd.lock);
    qxl_render_update_area_unlocked(qxl);
    qemu_mutex_unlock(&qxl->ssd.lock);
}

void qxl_render_update_area_done(PCIQXLDevice *qxl, QXLCookie *cookie)
{
    qemu_mutex_lock(&qxl->ssd.lock);
    trace_qxl_render_update_area_done(cookie);
    qemu_bh_schedule(qxl->update_area_bh);
    qxl->render_update_cookie_num--;
    qemu_mutex_unlock(&qxl->ssd.lock);
    g_free(cookie);
}

static void qxl_unpack_chunks(void *dest, size_t size, PCIQXLDevice *qxl,
                              QXLDataChunk *chunk, uint32_t group_id,
                              uint32_t chunk_data_size)
{
    uint32_t max_chunks = 32;
    size_t offset = 0;
    size_t bytes;
    QXLPHYSICAL next_chunk_phys = 0;

    for (;;) {
        bytes = MIN(size - offset, chunk_data_size);
        memcpy(dest + offset, chunk->data, bytes);
        offset += bytes;
        if (offset == size) {
            return;
        }
        next_chunk_phys = chunk->next_chunk;
        chunk = qxl_phys2virt(qxl, next_chunk_phys, group_id,
                              sizeof(QXLDataChunk));
        if (!chunk) {
            return;
        }
        chunk_data_size = chunk->data_size;
        chunk = qxl_phys2virt(qxl, next_chunk_phys, group_id,
                              sizeof(QXLDataChunk) + chunk_data_size);
        if (!chunk) {
            return;
        }
        max_chunks--;
        if (max_chunks == 0) {
            return;
        }
    }
}

static QEMUCursor *qxl_cursor(PCIQXLDevice *qxl, QXLCursor *cursor,
                              uint32_t group_id, uint32_t chunk_data_size)
{
    QEMUCursor *c;
    uint8_t *and_mask, *xor_mask;
    size_t size;

    c = cursor_alloc(cursor->header.width, cursor->header.height);

    if (!c) {
        qxl_set_guest_bug(qxl, "%s: cursor %ux%u alloc error", __func__,
                cursor->header.width, cursor->header.height);
        goto fail;
    }

    c->hot_x = cursor->header.hot_spot_x;
    c->hot_y = cursor->header.hot_spot_y;
    switch (cursor->header.type) {
    case SPICE_CURSOR_TYPE_MONO:
        /* Assume that the full cursor is available in a single chunk. */
        size = 2 * cursor_get_mono_bpl(c) * c->height;
        if (size != cursor->data_size || chunk_data_size < size) {
            qxl_set_guest_bug(qxl, "%s: bad monochrome cursor %ux%u"
                              " data_size %u chunk_size %u",
                              __func__, c->width, c->height,
                              cursor->data_size, chunk_data_size);
            goto fail;
        }
        and_mask = cursor->chunk.data;
        xor_mask = and_mask + cursor_get_mono_bpl(c) * c->height;
        cursor_set_mono(c, 0xffffff, 0x000000, xor_mask, 1, and_mask);
        if (qxl->debug > 2) {
            cursor_print_ascii_art(c, "qxl/mono");
        }
        break;
    case SPICE_CURSOR_TYPE_ALPHA:
        size = sizeof(uint32_t) * c->width * c->height;
        qxl_unpack_chunks(c->data, size, qxl, &cursor->chunk, group_id,
                          chunk_data_size);
        if (qxl->debug > 2) {
            cursor_print_ascii_art(c, "qxl/alpha");
        }
        break;
    default:
        fprintf(stderr, "%s: not implemented: type %d\n",
                __func__, cursor->header.type);
        goto fail;
    }
    return c;

fail:
    cursor_unref(c);
    return NULL;
}


/* called from spice server thread context only */
int qxl_render_cursor(PCIQXLDevice *qxl, QXLCommandExt *ext)
{
    QXLCursorCmd *cmd = qxl_phys2virt(qxl, ext->cmd.data, ext->group_id,
                                      sizeof(QXLCursorCmd));
    QXLCursor *cursor;
    QEMUCursor *c;

    if (!cmd) {
        return 1;
    }

    if (qxl->debug > 1 && cmd->type != QXL_CURSOR_MOVE) {
        fprintf(stderr, "%s", __func__);
        qxl_log_cmd_cursor(qxl, cmd, ext->group_id);
        fprintf(stderr, "\n");
    }
    switch (cmd->type) {
    case QXL_CURSOR_SET:
    {
        uint32_t chunk_data_size;

        /* First read the QXLCursor to get QXLDataChunk::data_size ... */
        cursor = qxl_phys2virt(qxl, cmd->u.set.shape, ext->group_id,
                               sizeof(QXLCursor));
        if (!cursor) {
            return 1;
        }
        chunk_data_size = cursor->chunk.data_size;
        /* Then read including the chunked data following QXLCursor. */
        cursor = qxl_phys2virt(qxl, cmd->u.set.shape, ext->group_id,
                               sizeof(QXLCursor) + chunk_data_size);
        if (!cursor) {
            return 1;
        }
        c = qxl_cursor(qxl, cursor, ext->group_id, chunk_data_size);
        if (c == NULL) {
            c = cursor_builtin_left_ptr();
        }
        qemu_mutex_lock(&qxl->ssd.lock);
        if (qxl->ssd.cursor) {
            cursor_unref(qxl->ssd.cursor);
        }
        qxl->ssd.cursor = c;
        qxl->ssd.mouse_x = cmd->u.set.position.x;
        qxl->ssd.mouse_y = cmd->u.set.position.y;
        qemu_mutex_unlock(&qxl->ssd.lock);
        qemu_bh_schedule(qxl->ssd.cursor_bh);
        break;
    }
    case QXL_CURSOR_MOVE:
        qemu_mutex_lock(&qxl->ssd.lock);
        qxl->ssd.mouse_x = cmd->u.position.x;
        qxl->ssd.mouse_y = cmd->u.position.y;
        qemu_mutex_unlock(&qxl->ssd.lock);
        qemu_bh_schedule(qxl->ssd.cursor_bh);
        break;
    }
    return 0;
}
