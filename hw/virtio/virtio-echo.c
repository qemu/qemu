/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-access.h"

#define TYPE_VIRTIO_ECHO2 "virtio-echo2"
#define VIRTIO_ID_ECHO2   0xFF10

OBJECT_DECLARE_SIMPLE_TYPE(VirtIOEcho2, VIRTIO_ECHO2)

typedef struct VirtIOEcho2 {
    VirtIODevice vdev;
    VirtQueue *vq;
} VirtIOEcho2;

static void echo2_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;

    while ((elem = virtqueue_pop(vq, sizeof(*elem)))) {
        /* OUT -> IN echo */
        size_t out_len = iov_size(elem->out_sg, elem->out_num);
        size_t in_len  = iov_size(elem->in_sg,  elem->in_num);
        size_t n = MIN(out_len, in_len);
        if (n) {
            uint8_t *buf = g_malloc(n);
            iov_to_buf(elem->out_sg, elem->out_num, 0, buf, n);
            iov_from_buf(elem->in_sg,  elem->in_num,  0, buf, n);
            g_free(buf);
        }
        virtqueue_push(vq, elem, n);
        virtio_notify(vdev, vq);
    }
}

static uint64_t echo2_get_features(VirtIODevice *vdev, uint64_t f, Error **errp)
{
    /* Start simple: no optional features */
    return 0;
}

static void virtio_echo2_realize(DeviceState *dev, Error **errp)
{
    VirtIOEcho2 *s = VIRTIO_ECHO2(dev);
    virtio_init(VIRTIO_DEVICE(dev), TYPE_VIRTIO_ECHO2, VIRTIO_ID_ECHO2, sizeof(*s));
    s->vq = virtio_add_queue(VIRTIO_DEVICE(dev), 256, echo2_handle_output);
}

static void virtio_echo2_unrealize(DeviceState *dev)
{
    VirtIOEcho2 *s = VIRTIO_ECHO2(dev);
    virtio_del_queue(VIRTIO_DEVICE(dev), s->vq);
    virtio_cleanup(VIRTIO_DEVICE(dev));
}

static void virtio_echo2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    dc->realize   = virtio_echo2_realize;
    dc->unrealize = virtio_echo2_unrealize;
    vdc->get_features = echo2_get_features;
}

static const TypeInfo virtio_echo2_info = {
    .name          = TYPE_VIRTIO_ECHO2,
    .parent        = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOEcho2),
    .class_init    = virtio_echo2_class_init,
};

static void virtio_echo2_register_types(void)
{
    type_register_static(&virtio_echo2_info);
}
type_init(virtio_echo2_register_types);
