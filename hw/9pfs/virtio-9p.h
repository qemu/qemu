#ifndef QEMU_VIRTIO_9P_H
#define QEMU_VIRTIO_9P_H

#include "standard-headers/linux/virtio_9p.h"
#include "hw/virtio/virtio.h"
#include "9p.h"
#include "qom/object.h"

struct V9fsVirtioState {
    VirtIODevice parent_obj;
    VirtQueue *vq;
    size_t config_size;
    VirtQueueElement *elems[MAX_REQ];
    V9fsState state;
};
typedef struct V9fsVirtioState V9fsVirtioState;

#define TYPE_VIRTIO_9P "virtio-9p-device"
DECLARE_INSTANCE_CHECKER(V9fsVirtioState, VIRTIO_9P,
                         TYPE_VIRTIO_9P)

#endif
