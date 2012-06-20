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

#include "qemu/rng.h"

/* The Virtio ID for the virtio rng device */
#define VIRTIO_ID_RNG    4

struct VirtIORNGConf {
    RngBackend *rng;
};

#endif
