/*
 * Virtio Accessor Support: In case your target can change endian.
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Rusty Russell   <rusty@au.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#ifndef _QEMU_VIRTIO_ACCESS_H
#define _QEMU_VIRTIO_ACCESS_H
#include "hw/virtio/virtio.h"
#include "exec/address-spaces.h"

static inline bool virtio_access_is_big_endian(VirtIODevice *vdev)
{
#if defined(TARGET_IS_BIENDIAN)
    return virtio_is_big_endian(vdev);
#elif defined(TARGET_WORDS_BIGENDIAN)
    return true;
#else
    return false;
#endif
}

static inline uint16_t virtio_lduw_phys(VirtIODevice *vdev, hwaddr pa)
{
    if (virtio_access_is_big_endian(vdev)) {
        return lduw_be_phys(&address_space_memory, pa);
    }
    return lduw_le_phys(&address_space_memory, pa);
}

static inline uint32_t virtio_ldl_phys(VirtIODevice *vdev, hwaddr pa)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldl_be_phys(&address_space_memory, pa);
    }
    return ldl_le_phys(&address_space_memory, pa);
}

static inline uint64_t virtio_ldq_phys(VirtIODevice *vdev, hwaddr pa)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldq_be_phys(&address_space_memory, pa);
    }
    return ldq_le_phys(&address_space_memory, pa);
}

static inline void virtio_stw_phys(VirtIODevice *vdev, hwaddr pa,
                                   uint16_t value)
{
    if (virtio_access_is_big_endian(vdev)) {
        stw_be_phys(&address_space_memory, pa, value);
    } else {
        stw_le_phys(&address_space_memory, pa, value);
    }
}

static inline void virtio_stl_phys(VirtIODevice *vdev, hwaddr pa,
                                   uint32_t value)
{
    if (virtio_access_is_big_endian(vdev)) {
        stl_be_phys(&address_space_memory, pa, value);
    } else {
        stl_le_phys(&address_space_memory, pa, value);
    }
}

static inline void virtio_stw_p(VirtIODevice *vdev, void *ptr, uint16_t v)
{
    if (virtio_access_is_big_endian(vdev)) {
        stw_be_p(ptr, v);
    } else {
        stw_le_p(ptr, v);
    }
}

static inline void virtio_stl_p(VirtIODevice *vdev, void *ptr, uint32_t v)
{
    if (virtio_access_is_big_endian(vdev)) {
        stl_be_p(ptr, v);
    } else {
        stl_le_p(ptr, v);
    }
}

static inline void virtio_stq_p(VirtIODevice *vdev, void *ptr, uint64_t v)
{
    if (virtio_access_is_big_endian(vdev)) {
        stq_be_p(ptr, v);
    } else {
        stq_le_p(ptr, v);
    }
}

static inline int virtio_lduw_p(VirtIODevice *vdev, const void *ptr)
{
    if (virtio_access_is_big_endian(vdev)) {
        return lduw_be_p(ptr);
    } else {
        return lduw_le_p(ptr);
    }
}

static inline int virtio_ldl_p(VirtIODevice *vdev, const void *ptr)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldl_be_p(ptr);
    } else {
        return ldl_le_p(ptr);
    }
}

static inline uint64_t virtio_ldq_p(VirtIODevice *vdev, const void *ptr)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldq_be_p(ptr);
    } else {
        return ldq_le_p(ptr);
    }
}

static inline uint16_t virtio_tswap16(VirtIODevice *vdev, uint16_t s)
{
#ifdef HOST_WORDS_BIGENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap16(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap16(s) : s;
#endif
}

static inline void virtio_tswap16s(VirtIODevice *vdev, uint16_t *s)
{
    *s = virtio_tswap16(vdev, *s);
}

static inline uint32_t virtio_tswap32(VirtIODevice *vdev, uint32_t s)
{
#ifdef HOST_WORDS_BIGENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap32(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap32(s) : s;
#endif
}

static inline void virtio_tswap32s(VirtIODevice *vdev, uint32_t *s)
{
    *s = virtio_tswap32(vdev, *s);
}

static inline uint64_t virtio_tswap64(VirtIODevice *vdev, uint64_t s)
{
#ifdef HOST_WORDS_BIGENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap64(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap64(s) : s;
#endif
}

static inline void virtio_tswap64s(VirtIODevice *vdev, uint64_t *s)
{
    *s = virtio_tswap64(vdev, *s);
}
#endif /* _QEMU_VIRTIO_ACCESS_H */
