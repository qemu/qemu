/*
 * Virtio vhost-user GPU Device
 *
 * DRM helpers
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "vugbm.h"

static bool
mem_alloc_bo(struct vugbm_buffer *buf)
{
    buf->mmap = g_malloc(buf->width * buf->height * 4);
    buf->stride = buf->width * 4;
    return true;
}

static void
mem_free_bo(struct vugbm_buffer *buf)
{
    g_free(buf->mmap);
}

static bool
mem_map_bo(struct vugbm_buffer *buf)
{
    return buf->mmap != NULL;
}

static void
mem_unmap_bo(struct vugbm_buffer *buf)
{
}

static void
mem_device_destroy(struct vugbm_device *dev)
{
}

#ifdef CONFIG_MEMFD
struct udmabuf_create {
        uint32_t memfd;
        uint32_t flags;
        uint64_t offset;
        uint64_t size;
};

#define UDMABUF_CREATE _IOW('u', 0x42, struct udmabuf_create)

static size_t
udmabuf_get_size(struct vugbm_buffer *buf)
{
    return ROUND_UP(buf->width * buf->height * 4, getpagesize());
}

static bool
udmabuf_alloc_bo(struct vugbm_buffer *buf)
{
    int ret;

    buf->memfd = memfd_create("udmabuf-bo", MFD_ALLOW_SEALING);
    if (buf->memfd < 0) {
        return false;
    }

    ret = ftruncate(buf->memfd, udmabuf_get_size(buf));
    if (ret < 0) {
        close(buf->memfd);
        return false;
    }

    ret = fcntl(buf->memfd, F_ADD_SEALS, F_SEAL_SHRINK);
    if (ret < 0) {
        close(buf->memfd);
        return false;
    }

    buf->stride = buf->width * 4;

    return true;
}

static void
udmabuf_free_bo(struct vugbm_buffer *buf)
{
    close(buf->memfd);
}

static bool
udmabuf_map_bo(struct vugbm_buffer *buf)
{
    buf->mmap = mmap(NULL, udmabuf_get_size(buf),
                     PROT_READ | PROT_WRITE, MAP_SHARED, buf->memfd, 0);
    if (buf->mmap == MAP_FAILED) {
        return false;
    }

    return true;
}

static bool
udmabuf_get_fd(struct vugbm_buffer *buf, int *fd)
{
    struct udmabuf_create create = {
        .memfd = buf->memfd,
        .offset = 0,
        .size = udmabuf_get_size(buf),
    };

    *fd = ioctl(buf->dev->fd, UDMABUF_CREATE, &create);

    return *fd >= 0;
}

static void
udmabuf_unmap_bo(struct vugbm_buffer *buf)
{
    munmap(buf->mmap, udmabuf_get_size(buf));
}

static void
udmabuf_device_destroy(struct vugbm_device *dev)
{
    close(dev->fd);
}
#endif

#ifdef CONFIG_GBM
static bool
alloc_bo(struct vugbm_buffer *buf)
{
    struct gbm_device *dev = buf->dev->dev;

    assert(!buf->bo);

    buf->bo = gbm_bo_create(dev, buf->width, buf->height,
                            buf->format,
                            GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);

    if (buf->bo) {
        buf->stride = gbm_bo_get_stride(buf->bo);
        return true;
    }

    return false;
}

static void
free_bo(struct vugbm_buffer *buf)
{
    gbm_bo_destroy(buf->bo);
}

static bool
map_bo(struct vugbm_buffer *buf)
{
    uint32_t stride;

    buf->mmap = gbm_bo_map(buf->bo, 0, 0, buf->width, buf->height,
                           GBM_BO_TRANSFER_READ_WRITE, &stride,
                           &buf->mmap_data);

    assert(stride == buf->stride);

    return buf->mmap != NULL;
}

static void
unmap_bo(struct vugbm_buffer *buf)
{
    gbm_bo_unmap(buf->bo, buf->mmap_data);
}

static bool
get_fd(struct vugbm_buffer *buf, int *fd)
{
    *fd = gbm_bo_get_fd(buf->bo);

    return *fd >= 0;
}

static void
device_destroy(struct vugbm_device *dev)
{
    gbm_device_destroy(dev->dev);
}
#endif

void
vugbm_device_destroy(struct vugbm_device *dev)
{
    if (!dev->inited) {
        return;
    }

    dev->device_destroy(dev);
}

bool
vugbm_device_init(struct vugbm_device *dev, int fd)
{
    dev->fd = fd;

#ifdef CONFIG_GBM
    dev->dev = gbm_create_device(fd);
#endif

    if (0) {
        /* nothing */
    }
#ifdef CONFIG_GBM
    else if (dev->dev != NULL) {
        dev->alloc_bo = alloc_bo;
        dev->free_bo = free_bo;
        dev->get_fd = get_fd;
        dev->map_bo = map_bo;
        dev->unmap_bo = unmap_bo;
        dev->device_destroy = device_destroy;
    }
#endif
#ifdef CONFIG_MEMFD
    else if (g_file_test("/dev/udmabuf", G_FILE_TEST_EXISTS)) {
        dev->fd = open("/dev/udmabuf", O_RDWR);
        if (dev->fd < 0) {
            return false;
        }
        g_debug("Using experimental udmabuf backend");
        dev->alloc_bo = udmabuf_alloc_bo;
        dev->free_bo = udmabuf_free_bo;
        dev->get_fd = udmabuf_get_fd;
        dev->map_bo = udmabuf_map_bo;
        dev->unmap_bo = udmabuf_unmap_bo;
        dev->device_destroy = udmabuf_device_destroy;
    }
#endif
    else {
        g_debug("Using mem fallback");
        dev->alloc_bo = mem_alloc_bo;
        dev->free_bo = mem_free_bo;
        dev->map_bo = mem_map_bo;
        dev->unmap_bo = mem_unmap_bo;
        dev->device_destroy = mem_device_destroy;
        return false;
    }

    dev->inited = true;
    return true;
}

static bool
vugbm_buffer_map(struct vugbm_buffer *buf)
{
    struct vugbm_device *dev = buf->dev;

    return dev->map_bo(buf);
}

static void
vugbm_buffer_unmap(struct vugbm_buffer *buf)
{
    struct vugbm_device *dev = buf->dev;

    dev->unmap_bo(buf);
}

bool
vugbm_buffer_can_get_dmabuf_fd(struct vugbm_buffer *buffer)
{
    if (!buffer->dev->get_fd) {
        return false;
    }

    return true;
}

bool
vugbm_buffer_get_dmabuf_fd(struct vugbm_buffer *buffer, int *fd)
{
    if (!vugbm_buffer_can_get_dmabuf_fd(buffer) ||
        !buffer->dev->get_fd(buffer, fd)) {
        g_warning("Failed to get dmabuf");
        return false;
    }

    if (*fd < 0) {
        g_warning("error: dmabuf_fd < 0");
        return false;
    }

    return true;
}

bool
vugbm_buffer_create(struct vugbm_buffer *buffer, struct vugbm_device *dev,
                    uint32_t width, uint32_t height)
{
    buffer->dev = dev;
    buffer->width = width;
    buffer->height = height;
    buffer->format = GBM_FORMAT_XRGB8888;
    buffer->stride = 0; /* modified during alloc */
    if (!dev->alloc_bo(buffer)) {
        g_warning("alloc_bo failed");
        return false;
    }

    if (!vugbm_buffer_map(buffer)) {
        g_warning("map_bo failed");
        goto err;
    }

    return true;

err:
    dev->free_bo(buffer);
    return false;
}

void
vugbm_buffer_destroy(struct vugbm_buffer *buffer)
{
    struct vugbm_device *dev = buffer->dev;

    vugbm_buffer_unmap(buffer);
    dev->free_bo(buffer);
}
