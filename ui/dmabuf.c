/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QemuDmaBuf struct and helpers used for accessing its data
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "ui/dmabuf.h"

struct QemuDmaBuf {
    int       fd;
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;
    uint32_t  fourcc;
    uint64_t  modifier;
    uint32_t  texture;
    uint32_t  x;
    uint32_t  y;
    uint32_t  backing_width;
    uint32_t  backing_height;
    bool      y0_top;
    void      *sync;
    int       fence_fd;
    bool      allow_fences;
    bool      draw_submitted;
};

QemuDmaBuf *qemu_dmabuf_new(uint32_t width, uint32_t height,
                            uint32_t stride, uint32_t x,
                            uint32_t y, uint32_t backing_width,
                            uint32_t backing_height, uint32_t fourcc,
                            uint64_t modifier, int32_t dmabuf_fd,
                            bool allow_fences, bool y0_top) {
    QemuDmaBuf *dmabuf;

    dmabuf = g_new0(QemuDmaBuf, 1);

    dmabuf->width = width;
    dmabuf->height = height;
    dmabuf->stride = stride;
    dmabuf->x = x;
    dmabuf->y = y;
    dmabuf->backing_width = backing_width;
    dmabuf->backing_height = backing_height;
    dmabuf->fourcc = fourcc;
    dmabuf->modifier = modifier;
    dmabuf->fd = dmabuf_fd;
    dmabuf->allow_fences = allow_fences;
    dmabuf->y0_top = y0_top;
    dmabuf->fence_fd = -1;

    return dmabuf;
}

void qemu_dmabuf_free(QemuDmaBuf *dmabuf)
{
    if (dmabuf == NULL) {
        return;
    }

    g_free(dmabuf);
}

int qemu_dmabuf_get_fd(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->fd;
}

int qemu_dmabuf_dup_fd(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    if (dmabuf->fd >= 0) {
        return dup(dmabuf->fd);
    } else {
        return -1;
    }
}

void qemu_dmabuf_close(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    if (dmabuf->fd >= 0) {
        close(dmabuf->fd);
        dmabuf->fd = -1;
    }
}

uint32_t qemu_dmabuf_get_width(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->width;
}

uint32_t qemu_dmabuf_get_height(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->height;
}

uint32_t qemu_dmabuf_get_stride(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->stride;
}

uint32_t qemu_dmabuf_get_fourcc(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->fourcc;
}

uint64_t qemu_dmabuf_get_modifier(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->modifier;
}

uint32_t qemu_dmabuf_get_texture(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->texture;
}

uint32_t qemu_dmabuf_get_x(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->x;
}

uint32_t qemu_dmabuf_get_y(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->y;
}

uint32_t qemu_dmabuf_get_backing_width(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->backing_width;
}

uint32_t qemu_dmabuf_get_backing_height(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->backing_height;
}

bool qemu_dmabuf_get_y0_top(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->y0_top;
}

void *qemu_dmabuf_get_sync(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->sync;
}

int32_t qemu_dmabuf_get_fence_fd(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->fence_fd;
}

bool qemu_dmabuf_get_allow_fences(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->allow_fences;
}

bool qemu_dmabuf_get_draw_submitted(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->draw_submitted;
}

void qemu_dmabuf_set_texture(QemuDmaBuf *dmabuf, uint32_t texture)
{
    assert(dmabuf != NULL);
    dmabuf->texture = texture;
}

void qemu_dmabuf_set_fence_fd(QemuDmaBuf *dmabuf, int32_t fence_fd)
{
    assert(dmabuf != NULL);
    dmabuf->fence_fd = fence_fd;
}

void qemu_dmabuf_set_sync(QemuDmaBuf *dmabuf, void *sync)
{
    assert(dmabuf != NULL);
    dmabuf->sync = sync;
}

void qemu_dmabuf_set_draw_submitted(QemuDmaBuf *dmabuf, bool draw_submitted)
{
    assert(dmabuf != NULL);
    dmabuf->draw_submitted = draw_submitted;
}

void qemu_dmabuf_set_fd(QemuDmaBuf *dmabuf, int32_t fd)
{
    assert(dmabuf != NULL);
    dmabuf->fd = fd;
}
