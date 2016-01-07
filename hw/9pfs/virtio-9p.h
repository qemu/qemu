#ifndef _QEMU_VIRTIO_9P_H
#define _QEMU_VIRTIO_9P_H

#include "standard-headers/linux/virtio_9p.h"
#include "hw/virtio/virtio.h"
#include "9p.h"

extern void handle_9p_output(VirtIODevice *vdev, VirtQueue *vq);
ssize_t virtio_pdu_vmarshal(V9fsPDU *pdu, size_t offset,
                            const char *fmt, va_list ap);
ssize_t virtio_pdu_vunmarshal(V9fsPDU *pdu, size_t offset,
                              const char *fmt, va_list ap);
void virtio_init_iov_from_pdu(V9fsPDU *pdu, struct iovec **piov,
                              unsigned int *pniov, bool is_write);

#define TYPE_VIRTIO_9P "virtio-9p-device"
#define VIRTIO_9P(obj) \
        OBJECT_CHECK(V9fsState, (obj), TYPE_VIRTIO_9P)

#endif
