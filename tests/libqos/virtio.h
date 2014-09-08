/*
 * libqos virtio definitions
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_VIRTIO_H
#define LIBQOS_VIRTIO_H

#include "libqos/malloc.h"

#define QVIRTIO_VENDOR_ID       0x1AF4

#define QVIRTIO_RESET           0x0
#define QVIRTIO_ACKNOWLEDGE     0x1
#define QVIRTIO_DRIVER          0x2
#define QVIRTIO_DRIVER_OK       0x4

#define QVIRTIO_NET_DEVICE_ID   0x1
#define QVIRTIO_BLK_DEVICE_ID   0x2

#define QVIRTIO_F_NOTIFY_ON_EMPTY       0x01000000
#define QVIRTIO_F_ANY_LAYOUT            0x08000000
#define QVIRTIO_F_RING_INDIRECT_DESC    0x10000000
#define QVIRTIO_F_RING_EVENT_IDX        0x20000000
#define QVIRTIO_F_BAD_FEATURE           0x40000000

#define QVRING_DESC_F_NEXT      0x1
#define QVRING_DESC_F_WRITE     0x2
#define QVRING_DESC_F_INDIRECT  0x4

#define QVIRTIO_F_NOTIFY_ON_EMPTY       0x01000000
#define QVIRTIO_F_ANY_LAYOUT            0x08000000
#define QVIRTIO_F_RING_INDIRECT_DESC    0x10000000
#define QVIRTIO_F_RING_EVENT_IDX        0x20000000
#define QVIRTIO_F_BAD_FEATURE           0x40000000

#define QVRING_AVAIL_F_NO_INTERRUPT     1

#define QVRING_USED_F_NO_NOTIFY     1

typedef struct QVirtioDevice {
    /* Device type */
    uint16_t device_type;
} QVirtioDevice;

typedef struct QVRingDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} QVRingDesc;

typedef struct QVRingAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[0]; /* This is an array of uint16_t */
    uint16_t used_event;
} QVRingAvail;

typedef struct QVRingUsedElem {
    uint32_t id;
    uint32_t len;
} QVRingUsedElem;

typedef struct QVRingUsed {
    uint16_t flags;
    uint16_t idx;
    QVRingUsedElem ring[0]; /* This is an array of QVRingUsedElem structs */
    uint16_t avail_event;
} QVRingUsed;

typedef struct QVirtQueue {
    uint64_t desc; /* This points to an array of QVRingDesc */
    uint64_t avail; /* This points to a QVRingAvail */
    uint64_t used; /* This points to a QVRingDesc */
    uint16_t index;
    uint32_t size;
    uint32_t free_head;
    uint32_t num_free;
    uint32_t align;
    bool indirect;
    bool event;
} QVirtQueue;

typedef struct QVRingIndirectDesc {
    uint64_t desc; /* This points to an array fo QVRingDesc */
    uint16_t index;
    uint16_t elem;
} QVRingIndirectDesc;

typedef struct QVirtioBus {
    uint8_t (*config_readb)(QVirtioDevice *d, void *addr);
    uint16_t (*config_readw)(QVirtioDevice *d, void *addr);
    uint32_t (*config_readl)(QVirtioDevice *d, void *addr);
    uint64_t (*config_readq)(QVirtioDevice *d, void *addr);

    /* Get features of the device */
    uint32_t (*get_features)(QVirtioDevice *d);

    /* Set features of the device */
    void (*set_features)(QVirtioDevice *d, uint32_t features);

    /* Get features of the guest */
    uint32_t (*get_guest_features)(QVirtioDevice *d);

    /* Get status of the device */
    uint8_t (*get_status)(QVirtioDevice *d);

    /* Set status of the device  */
    void (*set_status)(QVirtioDevice *d, uint8_t status);

    /* Get the queue ISR status of the device */
    bool (*get_queue_isr_status)(QVirtioDevice *d, QVirtQueue *vq);

    /* Get the configuration ISR status of the device */
    bool (*get_config_isr_status)(QVirtioDevice *d);

    /* Select a queue to work on */
    void (*queue_select)(QVirtioDevice *d, uint16_t index);

    /* Get the size of the selected queue */
    uint16_t (*get_queue_size)(QVirtioDevice *d);

    /* Set the address of the selected queue */
    void (*set_queue_address)(QVirtioDevice *d, uint32_t pfn);

    /* Setup the virtqueue specified by index */
    QVirtQueue *(*virtqueue_setup)(QVirtioDevice *d, QGuestAllocator *alloc,
                                                                uint16_t index);

    /* Notify changes in virtqueue */
    void (*virtqueue_kick)(QVirtioDevice *d, QVirtQueue *vq);
} QVirtioBus;

static inline uint32_t qvring_size(uint32_t num, uint32_t align)
{
    return ((sizeof(struct QVRingDesc) * num + sizeof(uint16_t) * (3 + num)
        + align - 1) & ~(align - 1))
        + sizeof(uint16_t) * 3 + sizeof(struct QVRingUsedElem) * num;
}

uint8_t qvirtio_config_readb(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr);
uint16_t qvirtio_config_readw(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr);
uint32_t qvirtio_config_readl(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr);
uint64_t qvirtio_config_readq(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr);
uint32_t qvirtio_get_features(const QVirtioBus *bus, QVirtioDevice *d);
void qvirtio_set_features(const QVirtioBus *bus, QVirtioDevice *d,
                                                            uint32_t features);

void qvirtio_reset(const QVirtioBus *bus, QVirtioDevice *d);
void qvirtio_set_acknowledge(const QVirtioBus *bus, QVirtioDevice *d);
void qvirtio_set_driver(const QVirtioBus *bus, QVirtioDevice *d);
void qvirtio_set_driver_ok(const QVirtioBus *bus, QVirtioDevice *d);

bool qvirtio_wait_queue_isr(const QVirtioBus *bus, QVirtioDevice *d,
                                            QVirtQueue *vq, uint64_t timeout);
bool qvirtio_wait_config_isr(const QVirtioBus *bus, QVirtioDevice *d,
                                                            uint64_t timeout);
QVirtQueue *qvirtqueue_setup(const QVirtioBus *bus, QVirtioDevice *d,
                                        QGuestAllocator *alloc, uint16_t index);

void qvring_init(const QGuestAllocator *alloc, QVirtQueue *vq, uint64_t addr);
QVRingIndirectDesc *qvring_indirect_desc_setup(QVirtioDevice *d,
                                        QGuestAllocator *alloc, uint16_t elem);
void qvring_indirect_desc_add(QVRingIndirectDesc *indirect, uint64_t data,
                                                    uint32_t len, bool write);
uint32_t qvirtqueue_add(QVirtQueue *vq, uint64_t data, uint32_t len, bool write,
                                                                    bool next);
uint32_t qvirtqueue_add_indirect(QVirtQueue *vq, QVRingIndirectDesc *indirect);
void qvirtqueue_kick(const QVirtioBus *bus, QVirtioDevice *d, QVirtQueue *vq,
                                                            uint32_t free_head);

void qvirtqueue_set_used_event(QVirtQueue *vq, uint16_t idx);
#endif
