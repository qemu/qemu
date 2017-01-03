#ifndef QEMU_VIRTIO_9P_H
#define QEMU_VIRTIO_9P_H

#include "standard-headers/linux/virtio_9p.h"
#include "hw/virtio/virtio.h"
#include "9p.h"

typedef struct V9fsVirtioState
{
    VirtIODevice parent_obj;
    VirtQueue *vq;
    size_t config_size;
    VirtQueueElement *elems[MAX_REQ];
    V9fsState state;
} V9fsVirtioState;

#define TYPE_VIRTIO_9P "virtio-9p-device"
#define VIRTIO_9P(obj) \
        OBJECT_CHECK(V9fsVirtioState, (obj), TYPE_VIRTIO_9P)

#endif
