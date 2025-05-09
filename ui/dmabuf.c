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
    int       fd[DMABUF_MAX_PLANES];
    uint32_t  width;
    uint32_t  height;
    uint32_t  offset[DMABUF_MAX_PLANES];
    uint32_t  stride[DMABUF_MAX_PLANES];
    uint32_t  num_planes;
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
                            const uint32_t *offset, const uint32_t *stride,
                            uint32_t x, uint32_t y,
                            uint32_t backing_width, uint32_t backing_height,
                            uint32_t fourcc, uint64_t modifier,
                            const int32_t *dmabuf_fd, uint32_t num_planes,
                            bool allow_fences, bool y0_top) {
    QemuDmaBuf *dmabuf;

    assert(num_planes > 0 && num_planes <= DMABUF_MAX_PLANES);

    dmabuf = g_new0(QemuDmaBuf, 1);

    dmabuf->width = width;
    dmabuf->height = height;
    memcpy(dmabuf->offset, offset, num_planes * sizeof(*offset));
    memcpy(dmabuf->stride, stride, num_planes * sizeof(*stride));
    dmabuf->x = x;
    dmabuf->y = y;
    dmabuf->backing_width = backing_width;
    dmabuf->backing_height = backing_height;
    dmabuf->fourcc = fourcc;
    dmabuf->modifier = modifier;
    memcpy(dmabuf->fd, dmabuf_fd, num_planes * sizeof(*dmabuf_fd));
    dmabuf->allow_fences = allow_fences;
    dmabuf->y0_top = y0_top;
    dmabuf->fence_fd = -1;
    dmabuf->num_planes = num_planes;

    return dmabuf;
}

void qemu_dmabuf_free(QemuDmaBuf *dmabuf)
{
    if (dmabuf == NULL) {
        return;
    }

    g_free(dmabuf);
}

const int *qemu_dmabuf_get_fds(QemuDmaBuf *dmabuf, int *nfds)
{
    assert(dmabuf != NULL);

    if (nfds) {
        *nfds = ARRAY_SIZE(dmabuf->fd);
    }

    return dmabuf->fd;
}

void qemu_dmabuf_dup_fds(QemuDmaBuf *dmabuf, int *fds, int nfds)
{
    int i;

    assert(dmabuf != NULL);
    assert(nfds >= dmabuf->num_planes);

    for (i = 0; i < dmabuf->num_planes; i++) {
        fds[i] = dmabuf->fd[i] >= 0 ? dup(dmabuf->fd[i]) : -1;
    }
}

void qemu_dmabuf_close(QemuDmaBuf *dmabuf)
{
    int i;

    assert(dmabuf != NULL);

    for (i = 0; i < dmabuf->num_planes; i++) {
        if (dmabuf->fd[i] >= 0) {
            close(dmabuf->fd[i]);
            dmabuf->fd[i] = -1;
        }
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

const uint32_t *qemu_dmabuf_get_offsets(QemuDmaBuf *dmabuf, int *noffsets)
{
    assert(dmabuf != NULL);

    if (noffsets) {
        *noffsets = ARRAY_SIZE(dmabuf->offset);
    }

    return dmabuf->offset;
}

const uint32_t *qemu_dmabuf_get_strides(QemuDmaBuf *dmabuf, int *nstrides)
{
    assert(dmabuf != NULL);

    if (nstrides) {
        *nstrides = ARRAY_SIZE(dmabuf->stride);
    }

    return dmabuf->stride;
}

uint32_t qemu_dmabuf_get_num_planes(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->num_planes;
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
