/*
 * Virtio vhost-user GPU Device
 *
 * GBM helpers
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef VHOST_USER_GPU_VUGBM_H
#define VHOST_USER_GPU_VUGBM_H

#include "qemu/osdep.h"

#ifdef CONFIG_MEMFD
#include <sys/mman.h>
#include <sys/ioctl.h>
#endif

#ifdef CONFIG_GBM
#include <gbm.h>
#endif

struct vugbm_buffer;

struct vugbm_device {
    bool inited;
    int fd;
#ifdef CONFIG_GBM
    struct gbm_device *dev;
#endif

    bool (*alloc_bo)(struct vugbm_buffer *buf);
    void (*free_bo)(struct vugbm_buffer *buf);
    bool (*get_fd)(struct vugbm_buffer *buf, int *fd);
    bool (*map_bo)(struct vugbm_buffer *buf);
    void (*unmap_bo)(struct vugbm_buffer *buf);
    void (*device_destroy)(struct vugbm_device *dev);
};

struct vugbm_buffer {
    struct vugbm_device *dev;

#ifdef CONFIG_MEMFD
    int memfd;
#endif
#ifdef CONFIG_GBM
    struct gbm_bo *bo;
    void *mmap_data;
#endif

    uint8_t *mmap;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
};

bool vugbm_device_init(struct vugbm_device *dev, int fd);
void vugbm_device_destroy(struct vugbm_device *dev);

bool vugbm_buffer_create(struct vugbm_buffer *buffer, struct vugbm_device *dev,
                         uint32_t width, uint32_t height);
bool vugbm_buffer_can_get_dmabuf_fd(struct vugbm_buffer *buffer);
bool vugbm_buffer_get_dmabuf_fd(struct vugbm_buffer *buffer, int *fd);
void vugbm_buffer_destroy(struct vugbm_buffer *buffer);

#endif
