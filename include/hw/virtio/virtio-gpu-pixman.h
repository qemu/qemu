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

#ifndef HW_VIRTIO_GPU_PIXMAN_H
#define HW_VIRTIO_GPU_PIXMAN_H

#include "ui/qemu-pixman.h"
#include "standard-headers/linux/virtio_gpu.h"

static inline pixman_format_code_t
virtio_gpu_get_pixman_format(uint32_t virtio_gpu_format)
{
    switch (virtio_gpu_format) {
    case VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM:
        return PIXMAN_BE_b8g8r8x8;
    case VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM:
        return PIXMAN_BE_b8g8r8a8;
    case VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM:
        return PIXMAN_BE_x8r8g8b8;
    case VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM:
        return PIXMAN_BE_a8r8g8b8;
    case VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM:
        return PIXMAN_BE_r8g8b8x8;
    case VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM:
        return PIXMAN_BE_r8g8b8a8;
    case VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM:
        return PIXMAN_BE_x8b8g8r8;
    case VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM:
        return PIXMAN_BE_a8b8g8r8;
    default:
        return 0;
    }
}

#endif
