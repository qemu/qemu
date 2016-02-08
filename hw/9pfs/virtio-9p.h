#ifndef _QEMU_VIRTIO_9P_H
#define _QEMU_VIRTIO_9P_H

#include "standard-headers/linux/virtio_9p.h"
#include "hw/virtio/virtio.h"
#include "9p.h"

typedef struct V9fsVirtioState
{
    VirtIODevice parent_obj;
    VirtQueue *vq;
    size_t config_size;
    V9fsPDU pdus[MAX_REQ];
    VirtQueueElement *elems[MAX_REQ];
    V9fsState state;
} V9fsVirtioState;

extern void virtio_9p_push_and_notify(V9fsPDU *pdu);

ssize_t virtio_pdu_vmarshal(V9fsPDU *pdu, size_t offset,
                            const char *fmt, va_list ap);
ssize_t virtio_pdu_vunmarshal(V9fsPDU *pdu, size_t offset,
                              const char *fmt, va_list ap);
void virtio_init_iov_from_pdu(V9fsPDU *pdu, struct iovec **piov,
                              unsigned int *pniov, bool is_write);

#define TYPE_VIRTIO_9P "virtio-9p-device"
#define VIRTIO_9P(obj) \
        OBJECT_CHECK(V9fsVirtioState, (obj), TYPE_VIRTIO_9P)

#endif
