/*
 * Virtio RNG Support
 *
 * Copyright Red Hat, Inc. 2012
 * Copyright Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef _QEMU_VIRTIO_RNG_H
#define _QEMU_VIRTIO_RNG_H

#include "sysemu/rng.h"
#include "sysemu/rng-random.h"

#define TYPE_VIRTIO_RNG "virtio-rng-device"
#define VIRTIO_RNG(obj) \
        OBJECT_CHECK(VirtIORNG, (obj), TYPE_VIRTIO_RNG)
#define VIRTIO_RNG_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_RNG)

/* The Virtio ID for the virtio rng device */
#define VIRTIO_ID_RNG    4

struct VirtIORNGConf {
    RngBackend *rng;
    uint64_t max_bytes;
    uint32_t period_ms;
    RndRandom *default_backend;
};

typedef struct VirtIORNG {
    VirtIODevice parent_obj;

    /* Only one vq - guest puts buffer(s) on it when it needs entropy */
    VirtQueue *vq;

    VirtIORNGConf conf;

    RngBackend *rng;

    /* We purposefully don't migrate this state.  The quota will reset on the
     * destination as a result.  Rate limiting is host state, not guest state.
     */
    QEMUTimer *rate_limit_timer;
    int64_t quota_remaining;
} VirtIORNG;

/* Set a default rate limit of 2^47 bytes per minute or roughly 2TB/s.  If
   you have an entropy source capable of generating more entropy than this
   and you can pass it through via virtio-rng, then hats off to you.  Until
   then, this is unlimited for all practical purposes.
*/
#define DEFINE_VIRTIO_RNG_PROPERTIES(_state, _conf_field)                    \
        DEFINE_PROP_UINT64("max-bytes", _state, _conf_field.max_bytes,       \
                           INT64_MAX),                                       \
        DEFINE_PROP_UINT32("period", _state, _conf_field.period_ms, 1 << 16)

#endif
