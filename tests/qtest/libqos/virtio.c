/*
 * libqos virtio driver
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "../libqtest.h"
#include "virtio.h"
#include "standard-headers/linux/virtio_config.h"
#include "standard-headers/linux/virtio_ring.h"

/*
 * qtest_readX/writeX() functions transfer host endian from/to guest endian.
 * This works great for Legacy VIRTIO devices where we need guest endian
 * accesses.  For VIRTIO 1.0 the vring is little-endian so the automatic guest
 * endianness conversion is not wanted.
 *
 * The following qvirtio_readX/writeX() functions handle Legacy and VIRTIO 1.0
 * accesses seamlessly.
 */
static uint16_t qvirtio_readw(QVirtioDevice *d, QTestState *qts, uint64_t addr)
{
    uint16_t val = qtest_readw(qts, addr);

    if (d->features & (1ull << VIRTIO_F_VERSION_1) && qtest_big_endian(qts)) {
        val = bswap16(val);
    }
    return val;
}

static uint32_t qvirtio_readl(QVirtioDevice *d, QTestState *qts, uint64_t addr)
{
    uint32_t val = qtest_readl(qts, addr);

    if (d->features & (1ull << VIRTIO_F_VERSION_1) && qtest_big_endian(qts)) {
        val = bswap32(val);
    }
    return val;
}

static void qvirtio_writew(QVirtioDevice *d, QTestState *qts,
                           uint64_t addr, uint16_t val)
{
    if (d->features & (1ull << VIRTIO_F_VERSION_1) && qtest_big_endian(qts)) {
        val = bswap16(val);
    }
    qtest_writew(qts, addr, val);
}

static void qvirtio_writel(QVirtioDevice *d, QTestState *qts,
                           uint64_t addr, uint32_t val)
{
    if (d->features & (1ull << VIRTIO_F_VERSION_1) && qtest_big_endian(qts)) {
        val = bswap32(val);
    }
    qtest_writel(qts, addr, val);
}

static void qvirtio_writeq(QVirtioDevice *d, QTestState *qts,
                           uint64_t addr, uint64_t val)
{
    if (d->features & (1ull << VIRTIO_F_VERSION_1) && qtest_big_endian(qts)) {
        val = bswap64(val);
    }
    qtest_writeq(qts, addr, val);
}

uint8_t qvirtio_config_readb(QVirtioDevice *d, uint64_t addr)
{
    g_assert_true(d->features_negotiated);
    return d->bus->config_readb(d, addr);
}

uint16_t qvirtio_config_readw(QVirtioDevice *d, uint64_t addr)
{
    g_assert_true(d->features_negotiated);
    return d->bus->config_readw(d, addr);
}

uint32_t qvirtio_config_readl(QVirtioDevice *d, uint64_t addr)
{
    g_assert_true(d->features_negotiated);
    return d->bus->config_readl(d, addr);
}

uint64_t qvirtio_config_readq(QVirtioDevice *d, uint64_t addr)
{
    g_assert_true(d->features_negotiated);
    return d->bus->config_readq(d, addr);
}

uint64_t qvirtio_get_features(QVirtioDevice *d)
{
    return d->bus->get_features(d);
}

void qvirtio_set_features(QVirtioDevice *d, uint64_t features)
{
    g_assert(!(features & QVIRTIO_F_BAD_FEATURE));

    d->features = features;
    d->bus->set_features(d, features);

    /*
     * This could be a separate function for drivers that want to access
     * configuration space before setting FEATURES_OK, but no existing users
     * need that and it's less code for callers if this is done implicitly.
     */
    if (features & (1ull << VIRTIO_F_VERSION_1)) {
        uint8_t status = d->bus->get_status(d) |
                         VIRTIO_CONFIG_S_FEATURES_OK;

        d->bus->set_status(d, status);
        g_assert_cmphex(d->bus->get_status(d), ==, status);
    }

    d->features_negotiated = true;
}

QVirtQueue *qvirtqueue_setup(QVirtioDevice *d,
                             QGuestAllocator *alloc, uint16_t index)
{
    g_assert_true(d->features_negotiated);
    return d->bus->virtqueue_setup(d, alloc, index);
}

void qvirtqueue_cleanup(const QVirtioBus *bus, QVirtQueue *vq,
                        QGuestAllocator *alloc)
{
    return bus->virtqueue_cleanup(vq, alloc);
}

void qvirtio_reset(QVirtioDevice *d)
{
    d->bus->set_status(d, 0);
    g_assert_cmphex(d->bus->get_status(d), ==, 0);
    d->features_negotiated = false;
}

void qvirtio_set_acknowledge(QVirtioDevice *d)
{
    d->bus->set_status(d, d->bus->get_status(d) | VIRTIO_CONFIG_S_ACKNOWLEDGE);
    g_assert_cmphex(d->bus->get_status(d), ==, VIRTIO_CONFIG_S_ACKNOWLEDGE);
}

void qvirtio_set_driver(QVirtioDevice *d)
{
    d->bus->set_status(d, d->bus->get_status(d) | VIRTIO_CONFIG_S_DRIVER);
    g_assert_cmphex(d->bus->get_status(d), ==,
                    VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_ACKNOWLEDGE);
}

void qvirtio_set_driver_ok(QVirtioDevice *d)
{
    d->bus->set_status(d, d->bus->get_status(d) | VIRTIO_CONFIG_S_DRIVER_OK);
    g_assert_cmphex(d->bus->get_status(d), ==, VIRTIO_CONFIG_S_DRIVER_OK |
                    VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_ACKNOWLEDGE |
                    (d->features & (1ull << VIRTIO_F_VERSION_1) ?
                     VIRTIO_CONFIG_S_FEATURES_OK : 0));
}

void qvirtio_wait_queue_isr(QTestState *qts, QVirtioDevice *d,
                            QVirtQueue *vq, gint64 timeout_us)
{
    gint64 start_time = g_get_monotonic_time();

    for (;;) {
        qtest_clock_step(qts, 100);
        if (d->bus->get_queue_isr_status(d, vq)) {
            return;
        }
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
}

/* Wait for the status byte at given guest memory address to be set
 *
 * The virtqueue interrupt must not be raised, making this useful for testing
 * event_index functionality.
 */
uint8_t qvirtio_wait_status_byte_no_isr(QTestState *qts, QVirtioDevice *d,
                                        QVirtQueue *vq,
                                        uint64_t addr,
                                        gint64 timeout_us)
{
    gint64 start_time = g_get_monotonic_time();
    uint8_t val;

    while ((val = qtest_readb(qts, addr)) == 0xff) {
        qtest_clock_step(qts, 100);
        g_assert(!d->bus->get_queue_isr_status(d, vq));
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
    return val;
}

/*
 * qvirtio_wait_used_elem:
 * @desc_idx: The next expected vq->desc[] index in the used ring
 * @len: A pointer that is filled with the length written into the buffer, may
 *       be NULL
 * @timeout_us: How many microseconds to wait before failing
 *
 * This function waits for the next completed request on the used ring.
 */
void qvirtio_wait_used_elem(QTestState *qts, QVirtioDevice *d,
                            QVirtQueue *vq,
                            uint32_t desc_idx,
                            uint32_t *len,
                            gint64 timeout_us)
{
    gint64 start_time = g_get_monotonic_time();

    for (;;) {
        uint32_t got_desc_idx;

        qtest_clock_step(qts, 100);

        if (d->bus->get_queue_isr_status(d, vq) &&
            qvirtqueue_get_buf(qts, vq, &got_desc_idx, len)) {
            g_assert_cmpint(got_desc_idx, ==, desc_idx);
            return;
        }

        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
}

void qvirtio_wait_config_isr(QVirtioDevice *d, gint64 timeout_us)
{
    d->bus->wait_config_isr_status(d, timeout_us);
}

void qvring_init(QTestState *qts, const QGuestAllocator *alloc, QVirtQueue *vq,
                 uint64_t addr)
{
    int i;

    vq->desc = addr;
    vq->avail = vq->desc + vq->size * sizeof(struct vring_desc);
    vq->used = (uint64_t)((vq->avail + sizeof(uint16_t) * (3 + vq->size)
        + vq->align - 1) & ~(vq->align - 1));

    for (i = 0; i < vq->size - 1; i++) {
        /* vq->desc[i].addr */
        qvirtio_writeq(vq->vdev, qts, vq->desc + (16 * i), 0);
        /* vq->desc[i].next */
        qvirtio_writew(vq->vdev, qts, vq->desc + (16 * i) + 14, i + 1);
    }

    /* vq->avail->flags */
    qvirtio_writew(vq->vdev, qts, vq->avail, 0);
    /* vq->avail->idx */
    qvirtio_writew(vq->vdev, qts, vq->avail + 2, 0);
    /* vq->avail->used_event */
    qvirtio_writew(vq->vdev, qts, vq->avail + 4 + (2 * vq->size), 0);

    /* vq->used->flags */
    qvirtio_writew(vq->vdev, qts, vq->used, 0);
    /* vq->used->idx */
    qvirtio_writew(vq->vdev, qts, vq->used + 2, 0);
    /* vq->used->avail_event */
    qvirtio_writew(vq->vdev, qts, vq->used + 4 +
                   sizeof(struct vring_used_elem) * vq->size, 0);
}

QVRingIndirectDesc *qvring_indirect_desc_setup(QTestState *qs, QVirtioDevice *d,
                                               QGuestAllocator *alloc,
                                               uint16_t elem)
{
    int i;
    QVRingIndirectDesc *indirect = g_malloc(sizeof(*indirect));

    indirect->index = 0;
    indirect->elem = elem;
    indirect->desc = guest_alloc(alloc, sizeof(struct vring_desc) * elem);

    for (i = 0; i < elem; ++i) {
        /* indirect->desc[i].addr */
        qvirtio_writeq(d, qs, indirect->desc + (16 * i), 0);

        /*
         * If it's not the last element of the ring, set
         * the chain (VRING_DESC_F_NEXT) flag and
         * desc->next. Clear the last element - there's
         * no guarantee that guest_alloc() will do it.
         */
        if (i != elem - 1) {
            /* indirect->desc[i].flags */
            qvirtio_writew(d, qs, indirect->desc + (16 * i) + 12,
                           VRING_DESC_F_NEXT);

            /* indirect->desc[i].next */
            qvirtio_writew(d, qs, indirect->desc + (16 * i) + 14, i + 1);
        } else {
            qvirtio_writew(d, qs, indirect->desc + (16 * i) + 12, 0);
            qvirtio_writew(d, qs, indirect->desc + (16 * i) + 14, 0);
        }
    }

    return indirect;
}

void qvring_indirect_desc_add(QVirtioDevice *d, QTestState *qts,
                              QVRingIndirectDesc *indirect,
                              uint64_t data, uint32_t len, bool write)
{
    uint16_t flags;

    g_assert_cmpint(indirect->index, <, indirect->elem);

    flags = qvirtio_readw(d, qts, indirect->desc +
                                  (16 * indirect->index) + 12);

    if (write) {
        flags |= VRING_DESC_F_WRITE;
    }

    /* indirect->desc[indirect->index].addr */
    qvirtio_writeq(d, qts, indirect->desc + (16 * indirect->index), data);
    /* indirect->desc[indirect->index].len */
    qvirtio_writel(d, qts, indirect->desc + (16 * indirect->index) + 8, len);
    /* indirect->desc[indirect->index].flags */
    qvirtio_writew(d, qts, indirect->desc + (16 * indirect->index) + 12,
                   flags);

    indirect->index++;
}

uint32_t qvirtqueue_add(QTestState *qts, QVirtQueue *vq, uint64_t data,
                        uint32_t len, bool write, bool next)
{
    uint16_t flags = 0;
    vq->num_free--;

    if (write) {
        flags |= VRING_DESC_F_WRITE;
    }

    if (next) {
        flags |= VRING_DESC_F_NEXT;
    }

    /* vq->desc[vq->free_head].addr */
    qvirtio_writeq(vq->vdev, qts, vq->desc + (16 * vq->free_head), data);
    /* vq->desc[vq->free_head].len */
    qvirtio_writel(vq->vdev, qts, vq->desc + (16 * vq->free_head) + 8, len);
    /* vq->desc[vq->free_head].flags */
    qvirtio_writew(vq->vdev, qts, vq->desc + (16 * vq->free_head) + 12, flags);

    return vq->free_head++; /* Return and increase, in this order */
}

uint32_t qvirtqueue_add_indirect(QTestState *qts, QVirtQueue *vq,
                                 QVRingIndirectDesc *indirect)
{
    g_assert(vq->indirect);
    g_assert_cmpint(vq->size, >=, indirect->elem);
    g_assert_cmpint(indirect->index, ==, indirect->elem);

    vq->num_free--;

    /* vq->desc[vq->free_head].addr */
    qvirtio_writeq(vq->vdev, qts, vq->desc + (16 * vq->free_head),
                   indirect->desc);
    /* vq->desc[vq->free_head].len */
    qvirtio_writel(vq->vdev, qts, vq->desc + (16 * vq->free_head) + 8,
                   sizeof(struct vring_desc) * indirect->elem);
    /* vq->desc[vq->free_head].flags */
    qvirtio_writew(vq->vdev, qts, vq->desc + (16 * vq->free_head) + 12,
                   VRING_DESC_F_INDIRECT);

    return vq->free_head++; /* Return and increase, in this order */
}

void qvirtqueue_kick(QTestState *qts, QVirtioDevice *d, QVirtQueue *vq,
                     uint32_t free_head)
{
    /* vq->avail->idx */
    uint16_t idx = qvirtio_readw(d, qts, vq->avail + 2);
    /* vq->used->flags */
    uint16_t flags;
    /* vq->used->avail_event */
    uint16_t avail_event;

    /* vq->avail->ring[idx % vq->size] */
    qvirtio_writew(d, qts, vq->avail + 4 + (2 * (idx % vq->size)), free_head);
    /* vq->avail->idx */
    qvirtio_writew(d, qts, vq->avail + 2, idx + 1);

    /* Must read after idx is updated */
    flags = qvirtio_readw(d, qts, vq->avail);
    avail_event = qvirtio_readw(d, qts, vq->used + 4 +
                                sizeof(struct vring_used_elem) * vq->size);

    /* < 1 because we add elements to avail queue one by one */
    if ((flags & VRING_USED_F_NO_NOTIFY) == 0 &&
                            (!vq->event || (uint16_t)(idx-avail_event) < 1)) {
        d->bus->virtqueue_kick(d, vq);
    }
}

/*
 * qvirtqueue_get_buf:
 * @desc_idx: A pointer that is filled with the vq->desc[] index, may be NULL
 * @len: A pointer that is filled with the length written into the buffer, may
 *       be NULL
 *
 * This function gets the next used element if there is one ready.
 *
 * Returns: true if an element was ready, false otherwise
 */
bool qvirtqueue_get_buf(QTestState *qts, QVirtQueue *vq, uint32_t *desc_idx,
                        uint32_t *len)
{
    uint16_t idx;
    uint64_t elem_addr, addr;

    idx = qvirtio_readw(vq->vdev, qts,
                        vq->used + offsetof(struct vring_used, idx));
    if (idx == vq->last_used_idx) {
        return false;
    }

    elem_addr = vq->used +
        offsetof(struct vring_used, ring) +
        (vq->last_used_idx % vq->size) *
        sizeof(struct vring_used_elem);

    if (desc_idx) {
        addr = elem_addr + offsetof(struct vring_used_elem, id);
        *desc_idx = qvirtio_readl(vq->vdev, qts, addr);
    }

    if (len) {
        addr = elem_addr + offsetof(struct vring_used_elem, len);
        *len = qvirtio_readw(vq->vdev, qts, addr);
    }

    vq->last_used_idx++;
    return true;
}

void qvirtqueue_set_used_event(QTestState *qts, QVirtQueue *vq, uint16_t idx)
{
    g_assert(vq->event);

    /* vq->avail->used_event */
    qvirtio_writew(vq->vdev, qts, vq->avail + 4 + (2 * vq->size), idx);
}

void qvirtio_start_device(QVirtioDevice *vdev)
{
    qvirtio_reset(vdev);
    qvirtio_set_acknowledge(vdev);
    qvirtio_set_driver(vdev);
}

bool qvirtio_is_big_endian(QVirtioDevice *d)
{
    return d->big_endian;
}
