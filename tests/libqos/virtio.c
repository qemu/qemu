/*
 * libqos virtio driver
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include "libqtest.h"
#include "libqos/virtio.h"

uint8_t qvirtio_config_readb(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr)
{
    return bus->config_readb(d, addr);
}

uint16_t qvirtio_config_readw(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr)
{
    return bus->config_readw(d, addr);
}

uint32_t qvirtio_config_readl(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr)
{
    return bus->config_readl(d, addr);
}

uint64_t qvirtio_config_readq(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr)
{
    return bus->config_readq(d, addr);
}

uint32_t qvirtio_get_features(const QVirtioBus *bus, QVirtioDevice *d)
{
    return bus->get_features(d);
}

void qvirtio_set_features(const QVirtioBus *bus, QVirtioDevice *d,
                                                            uint32_t features)
{
    bus->set_features(d, features);
}

QVirtQueue *qvirtqueue_setup(const QVirtioBus *bus, QVirtioDevice *d,
                                        QGuestAllocator *alloc, uint16_t index)
{
    return bus->virtqueue_setup(d, alloc, index);
}

void qvirtio_reset(const QVirtioBus *bus, QVirtioDevice *d)
{
    bus->set_status(d, QVIRTIO_RESET);
    g_assert_cmphex(bus->get_status(d), ==, QVIRTIO_RESET);
}

void qvirtio_set_acknowledge(const QVirtioBus *bus, QVirtioDevice *d)
{
    bus->set_status(d, bus->get_status(d) | QVIRTIO_ACKNOWLEDGE);
    g_assert_cmphex(bus->get_status(d), ==, QVIRTIO_ACKNOWLEDGE);
}

void qvirtio_set_driver(const QVirtioBus *bus, QVirtioDevice *d)
{
    bus->set_status(d, bus->get_status(d) | QVIRTIO_DRIVER);
    g_assert_cmphex(bus->get_status(d), ==,
                                    QVIRTIO_DRIVER | QVIRTIO_ACKNOWLEDGE);
}

void qvirtio_set_driver_ok(const QVirtioBus *bus, QVirtioDevice *d)
{
    bus->set_status(d, bus->get_status(d) | QVIRTIO_DRIVER_OK);
    g_assert_cmphex(bus->get_status(d), ==,
                QVIRTIO_DRIVER_OK | QVIRTIO_DRIVER | QVIRTIO_ACKNOWLEDGE);
}

bool qvirtio_wait_queue_isr(const QVirtioBus *bus, QVirtioDevice *d,
                                            QVirtQueue *vq, uint64_t timeout)
{
    do {
        clock_step(10);
        if (bus->get_queue_isr_status(d, vq)) {
            break; /* It has ended */
        }
    } while (--timeout);

    return timeout != 0;
}

bool qvirtio_wait_config_isr(const QVirtioBus *bus, QVirtioDevice *d,
                                                            uint64_t timeout)
{
    do {
        clock_step(10);
        if (bus->get_config_isr_status(d)) {
            break; /* It has ended */
        }
    } while (--timeout);

    return timeout != 0;
}

void qvring_init(const QGuestAllocator *alloc, QVirtQueue *vq, uint64_t addr)
{
    int i;

    vq->desc = addr;
    vq->avail = vq->desc + vq->size*sizeof(QVRingDesc);
    vq->used = (uint64_t)((vq->avail + sizeof(uint16_t) * (3 + vq->size)
        + vq->align - 1) & ~(vq->align - 1));

    for (i = 0; i < vq->size - 1; i++) {
        /* vq->desc[i].addr */
        writew(vq->desc + (16 * i), 0);
        /* vq->desc[i].next */
        writew(vq->desc + (16 * i) + 14, i + 1);
    }

    /* vq->avail->flags */
    writew(vq->avail, 0);
    /* vq->avail->idx */
    writew(vq->avail + 2, 0);

    /* vq->used->flags */
    writew(vq->used, 0);
}

QVRingIndirectDesc *qvring_indirect_desc_setup(QVirtioDevice *d,
                                        QGuestAllocator *alloc, uint16_t elem)
{
    int i;
    QVRingIndirectDesc *indirect = g_malloc(sizeof(*indirect));

    indirect->index = 0;
    indirect->elem = elem;
    indirect->desc = guest_alloc(alloc, sizeof(QVRingDesc)*elem);

    for (i = 0; i < elem - 1; ++i) {
        /* indirect->desc[i].addr */
        writeq(indirect->desc + (16 * i), 0);
        /* indirect->desc[i].flags */
        writew(indirect->desc + (16 * i) + 12, QVRING_DESC_F_NEXT);
        /* indirect->desc[i].next */
        writew(indirect->desc + (16 * i) + 14, i + 1);
    }

    return indirect;
}

void qvring_indirect_desc_add(QVRingIndirectDesc *indirect, uint64_t data,
                                                    uint32_t len, bool write)
{
    uint16_t flags;

    g_assert_cmpint(indirect->index, <, indirect->elem);

    flags = readw(indirect->desc + (16 * indirect->index) + 12);

    if (write) {
        flags |= QVRING_DESC_F_WRITE;
    }

    /* indirect->desc[indirect->index].addr */
    writeq(indirect->desc + (16 * indirect->index), data);
    /* indirect->desc[indirect->index].len */
    writel(indirect->desc + (16 * indirect->index) + 8, len);
    /* indirect->desc[indirect->index].flags */
    writew(indirect->desc + (16 * indirect->index) + 12, flags);

    indirect->index++;
}

uint32_t qvirtqueue_add(QVirtQueue *vq, uint64_t data, uint32_t len, bool write,
                                                                    bool next)
{
    uint16_t flags = 0;
    vq->num_free--;

    if (write) {
        flags |= QVRING_DESC_F_WRITE;
    }

    if (next) {
        flags |= QVRING_DESC_F_NEXT;
    }

    /* vq->desc[vq->free_head].addr */
    writeq(vq->desc + (16 * vq->free_head), data);
    /* vq->desc[vq->free_head].len */
    writel(vq->desc + (16 * vq->free_head) + 8, len);
    /* vq->desc[vq->free_head].flags */
    writew(vq->desc + (16 * vq->free_head) + 12, flags);

    return vq->free_head++; /* Return and increase, in this order */
}

uint32_t qvirtqueue_add_indirect(QVirtQueue *vq, QVRingIndirectDesc *indirect)
{
    g_assert(vq->indirect);
    g_assert_cmpint(vq->size, >=, indirect->elem);
    g_assert_cmpint(indirect->index, ==, indirect->elem);

    vq->num_free--;

    /* vq->desc[vq->free_head].addr */
    writeq(vq->desc + (16 * vq->free_head), indirect->desc);
    /* vq->desc[vq->free_head].len */
    writel(vq->desc + (16 * vq->free_head) + 8,
                                        sizeof(QVRingDesc) * indirect->elem);
    /* vq->desc[vq->free_head].flags */
    writew(vq->desc + (16 * vq->free_head) + 12, QVRING_DESC_F_INDIRECT);

    return vq->free_head++; /* Return and increase, in this order */
}

void qvirtqueue_kick(const QVirtioBus *bus, QVirtioDevice *d, QVirtQueue *vq,
                                                            uint32_t free_head)
{
    /* vq->avail->idx */
    uint16_t idx = readl(vq->avail + 2);

    /* vq->avail->ring[idx % vq->size] */
    writel(vq->avail + 4 + (2 * (idx % vq->size)), free_head);
    /* vq->avail->idx */
    writel(vq->avail + 2, idx + 1);

    bus->virtqueue_kick(d, vq);
}
