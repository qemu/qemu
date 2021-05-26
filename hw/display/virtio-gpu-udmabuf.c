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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu-common.h"
#include "qemu/iov.h"
#include "ui/console.h"
#include "hw/virtio/virtio-gpu.h"
#include "hw/virtio/virtio-gpu-pixman.h"
#include "trace.h"
#include "exec/ramblock.h"
#include "sysemu/hostmem.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include "qemu/memfd.h"
#include "standard-headers/linux/udmabuf.h"

static void virtio_gpu_create_udmabuf(struct virtio_gpu_simple_resource *res)
{
    struct udmabuf_create_list *list;
    RAMBlock *rb;
    ram_addr_t offset;
    int udmabuf, i;

    udmabuf = udmabuf_fd();
    if (udmabuf < 0) {
        return;
    }

    list = g_malloc0(sizeof(struct udmabuf_create_list) +
                     sizeof(struct udmabuf_create_item) * res->iov_cnt);

    for (i = 0; i < res->iov_cnt; i++) {
        rcu_read_lock();
        rb = qemu_ram_block_from_host(res->iov[i].iov_base, false, &offset);
        rcu_read_unlock();

        if (!rb || rb->fd < 0) {
            g_free(list);
            return;
        }

        list->list[i].memfd  = rb->fd;
        list->list[i].offset = offset;
        list->list[i].size   = res->iov[i].iov_len;
    }

    list->count = res->iov_cnt;
    list->flags = UDMABUF_FLAGS_CLOEXEC;

    res->dmabuf_fd = ioctl(udmabuf, UDMABUF_CREATE_LIST, list);
    if (res->dmabuf_fd < 0) {
        warn_report("%s: UDMABUF_CREATE_LIST: %s", __func__,
                    strerror(errno));
    }
    g_free(list);
}

static void virtio_gpu_remap_udmabuf(struct virtio_gpu_simple_resource *res)
{
    res->remapped = mmap(NULL, res->blob_size, PROT_READ,
                         MAP_SHARED, res->dmabuf_fd, 0);
    if (res->remapped == MAP_FAILED) {
        warn_report("%s: dmabuf mmap failed: %s", __func__,
                    strerror(errno));
        res->remapped = NULL;
    }
}

static void virtio_gpu_destroy_udmabuf(struct virtio_gpu_simple_resource *res)
{
    if (res->remapped) {
        munmap(res->remapped, res->blob_size);
        res->remapped = NULL;
    }
    if (res->dmabuf_fd >= 0) {
        close(res->dmabuf_fd);
        res->dmabuf_fd = -1;
    }
}

static int find_memory_backend_type(Object *obj, void *opaque)
{
    bool *memfd_backend = opaque;
    int ret;

    if (object_dynamic_cast(obj, TYPE_MEMORY_BACKEND)) {
        HostMemoryBackend *backend = MEMORY_BACKEND(obj);
        RAMBlock *rb = backend->mr.ram_block;

        if (rb && rb->fd > 0) {
            ret = fcntl(rb->fd, F_GET_SEALS);
            if (ret > 0) {
                *memfd_backend = true;
            }
        }
    }

    return 0;
}

bool virtio_gpu_have_udmabuf(void)
{
    Object *memdev_root;
    int udmabuf;
    bool memfd_backend = false;

    udmabuf = udmabuf_fd();
    if (udmabuf < 0) {
        return false;
    }

    memdev_root = object_resolve_path("/objects", NULL);
    object_child_foreach(memdev_root, find_memory_backend_type, &memfd_backend);

    return memfd_backend;
}

void virtio_gpu_init_udmabuf(struct virtio_gpu_simple_resource *res)
{
    void *pdata = NULL;

    res->dmabuf_fd = -1;
    if (res->iov_cnt == 1) {
        pdata = res->iov[0].iov_base;
    } else {
        virtio_gpu_create_udmabuf(res);
        if (res->dmabuf_fd < 0) {
            return;
        }
        virtio_gpu_remap_udmabuf(res);
        if (!res->remapped) {
            return;
        }
        pdata = res->remapped;
    }

    res->blob = pdata;
}

void virtio_gpu_fini_udmabuf(struct virtio_gpu_simple_resource *res)
{
    if (res->remapped) {
        virtio_gpu_destroy_udmabuf(res);
    }
}
