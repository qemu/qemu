#ifndef VRING_ACCESSORS_H
#define VRING_ACCESSORS_H

#include "standard-headers/linux/virtio_ring.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-access.h"

static inline uint16_t vring_get_used_idx(VirtIODevice *vdev, Vring *vring)
{
    return virtio_tswap16(vdev, vring->vr.used->idx);
}

static inline void vring_set_used_idx(VirtIODevice *vdev, Vring *vring,
                                      uint16_t idx)
{
    vring->vr.used->idx = virtio_tswap16(vdev, idx);
}

static inline uint16_t vring_get_avail_idx(VirtIODevice *vdev, Vring *vring)
{
    return virtio_tswap16(vdev, vring->vr.avail->idx);
}

static inline uint16_t vring_get_avail_ring(VirtIODevice *vdev, Vring *vring,
                                            int i)
{
    return virtio_tswap16(vdev, vring->vr.avail->ring[i]);
}

static inline void vring_set_used_ring_id(VirtIODevice *vdev, Vring *vring,
                                          int i, uint32_t id)
{
    vring->vr.used->ring[i].id = virtio_tswap32(vdev, id);
}

static inline void vring_set_used_ring_len(VirtIODevice *vdev, Vring *vring,
                                          int i, uint32_t len)
{
    vring->vr.used->ring[i].len = virtio_tswap32(vdev, len);
}

static inline uint16_t vring_get_used_flags(VirtIODevice *vdev, Vring *vring)
{
    return virtio_tswap16(vdev, vring->vr.used->flags);
}

static inline uint16_t vring_get_avail_flags(VirtIODevice *vdev, Vring *vring)
{
    return virtio_tswap16(vdev, vring->vr.avail->flags);
}

static inline void vring_set_used_flags(VirtIODevice *vdev, Vring *vring,
                                        uint16_t flags)
{
    vring->vr.used->flags |= virtio_tswap16(vdev, flags);
}

static inline void vring_clear_used_flags(VirtIODevice *vdev, Vring *vring,
                                          uint16_t flags)
{
    vring->vr.used->flags &= virtio_tswap16(vdev, ~flags);
}

static inline unsigned int vring_get_num(Vring *vring)
{
    return vring->vr.num;
}

/* Are there more descriptors available? */
static inline bool vring_more_avail(VirtIODevice *vdev, Vring *vring)
{
    return vring_get_avail_idx(vdev, vring) != vring->last_avail_idx;
}

#endif
