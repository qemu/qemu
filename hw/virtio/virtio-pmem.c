/*
 * Virtio PMEM device
 *
 * Copyright (C) 2018-2019 Red Hat, Inc.
 *
 * Authors:
 *  Pankaj Gupta <pagupta@redhat.com>
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-pmem.h"
#include "hw/virtio/virtio-access.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_pmem.h"
#include "block/aio.h"
#include "block/thread-pool.h"

typedef struct VirtIODeviceRequest {
    VirtQueueElement elem;
    int fd;
    VirtIOPMEM *pmem;
    VirtIODevice *vdev;
    struct virtio_pmem_req req;
    struct virtio_pmem_resp resp;
} VirtIODeviceRequest;

static int worker_cb(void *opaque)
{
    VirtIODeviceRequest *req_data = opaque;
    int err = 0;

    /* flush raw backing image */
    err = fsync(req_data->fd);
    if (err != 0) {
        err = 1;
    }

    virtio_stw_p(req_data->vdev, &req_data->resp.ret, err);

    return 0;
}

static void done_cb(void *opaque, int ret)
{
    VirtIODeviceRequest *req_data = opaque;
    int len = iov_from_buf(req_data->elem.in_sg, req_data->elem.in_num, 0,
                              &req_data->resp, sizeof(struct virtio_pmem_resp));

    /* Callbacks are serialized, so no need to use atomic ops. */
    virtqueue_push(req_data->pmem->rq_vq, &req_data->elem, len);
    virtio_notify((VirtIODevice *)req_data->pmem, req_data->pmem->rq_vq);
    g_free(req_data);
}

static void virtio_pmem_flush(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIODeviceRequest *req_data;
    VirtIOPMEM *pmem = VIRTIO_PMEM(vdev);
    HostMemoryBackend *backend = MEMORY_BACKEND(pmem->memdev);
    ThreadPool *pool = aio_get_thread_pool(qemu_get_aio_context());

    req_data = virtqueue_pop(vq, sizeof(VirtIODeviceRequest));
    if (!req_data) {
        virtio_error(vdev, "virtio-pmem missing request data");
        return;
    }

    if (req_data->elem.out_num < 1 || req_data->elem.in_num < 1) {
        virtio_error(vdev, "virtio-pmem request not proper");
        g_free(req_data);
        return;
    }
    req_data->fd   = memory_region_get_fd(&backend->mr);
    req_data->pmem = pmem;
    req_data->vdev = vdev;
    thread_pool_submit_aio(pool, worker_cb, req_data, done_cb, req_data);
}

static void virtio_pmem_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOPMEM *pmem = VIRTIO_PMEM(vdev);
    struct virtio_pmem_config *pmemcfg = (struct virtio_pmem_config *) config;

    virtio_stq_p(vdev, &pmemcfg->start, pmem->start);
    virtio_stq_p(vdev, &pmemcfg->size, memory_region_size(&pmem->memdev->mr));
}

static uint64_t virtio_pmem_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    return features;
}

static void virtio_pmem_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOPMEM *pmem = VIRTIO_PMEM(dev);

    if (!pmem->memdev) {
        error_setg(errp, "virtio-pmem memdev not set");
        return;
    }

    if (host_memory_backend_is_mapped(pmem->memdev)) {
        char *path = object_get_canonical_path_component(OBJECT(pmem->memdev));
        error_setg(errp, "can't use already busy memdev: %s", path);
        g_free(path);
        return;
    }

    host_memory_backend_set_mapped(pmem->memdev, true);
    virtio_init(vdev, TYPE_VIRTIO_PMEM, VIRTIO_ID_PMEM,
                sizeof(struct virtio_pmem_config));
    pmem->rq_vq = virtio_add_queue(vdev, 128, virtio_pmem_flush);
}

static void virtio_pmem_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOPMEM *pmem = VIRTIO_PMEM(dev);

    host_memory_backend_set_mapped(pmem->memdev, false);
    virtio_cleanup(vdev);
}

static void virtio_pmem_fill_device_info(const VirtIOPMEM *pmem,
                                         VirtioPMEMDeviceInfo *vi)
{
    vi->memaddr = pmem->start;
    vi->size    = memory_region_size(&pmem->memdev->mr);
    vi->memdev  = object_get_canonical_path(OBJECT(pmem->memdev));
}

static MemoryRegion *virtio_pmem_get_memory_region(VirtIOPMEM *pmem,
                                                   Error **errp)
{
    if (!pmem->memdev) {
        error_setg(errp, "'%s' property must be set", VIRTIO_PMEM_MEMDEV_PROP);
        return NULL;
    }

    return &pmem->memdev->mr;
}

static Property virtio_pmem_properties[] = {
    DEFINE_PROP_UINT64(VIRTIO_PMEM_ADDR_PROP, VirtIOPMEM, start, 0),
    DEFINE_PROP_LINK(VIRTIO_PMEM_MEMDEV_PROP, VirtIOPMEM, memdev,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_pmem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    VirtIOPMEMClass *vpc = VIRTIO_PMEM_CLASS(klass);

    dc->props = virtio_pmem_properties;

    vdc->realize = virtio_pmem_realize;
    vdc->unrealize = virtio_pmem_unrealize;
    vdc->get_config = virtio_pmem_get_config;
    vdc->get_features = virtio_pmem_get_features;

    vpc->fill_device_info = virtio_pmem_fill_device_info;
    vpc->get_memory_region = virtio_pmem_get_memory_region;
}

static TypeInfo virtio_pmem_info = {
    .name          = TYPE_VIRTIO_PMEM,
    .parent        = TYPE_VIRTIO_DEVICE,
    .class_size    = sizeof(VirtIOPMEMClass),
    .class_init    = virtio_pmem_class_init,
    .instance_size = sizeof(VirtIOPMEM),
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_pmem_info);
}

type_init(virtio_register_types)
