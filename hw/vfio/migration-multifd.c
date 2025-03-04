/*
 * Multifd VFIO migration
 *
 * Copyright (C) 2024,2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/vfio/vfio-common.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "migration/qemu-file.h"
#include "migration-multifd.h"
#include "trace.h"

#define VFIO_DEVICE_STATE_CONFIG_STATE (1)

#define VFIO_DEVICE_STATE_PACKET_VER_CURRENT (0)

typedef struct VFIODeviceStatePacket {
    uint32_t version;
    uint32_t idx;
    uint32_t flags;
    uint8_t data[0];
} QEMU_PACKED VFIODeviceStatePacket;

typedef struct VFIOMultifd {
} VFIOMultifd;

static VFIOMultifd *vfio_multifd_new(void)
{
    VFIOMultifd *multifd = g_new(VFIOMultifd, 1);

    return multifd;
}

static void vfio_multifd_free(VFIOMultifd *multifd)
{
    g_free(multifd);
}

void vfio_multifd_cleanup(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    g_clear_pointer(&migration->multifd, vfio_multifd_free);
}

bool vfio_multifd_transfer_supported(void)
{
    return multifd_device_state_supported() &&
        migrate_send_switchover_start();
}

bool vfio_multifd_transfer_enabled(VFIODevice *vbasedev)
{
    return false;
}

bool vfio_multifd_setup(VFIODevice *vbasedev, bool alloc_multifd, Error **errp)
{
    VFIOMigration *migration = vbasedev->migration;

    if (!vfio_multifd_transfer_enabled(vbasedev)) {
        /* Nothing further to check or do */
        return true;
    }

    if (alloc_multifd) {
        assert(!migration->multifd);
        migration->multifd = vfio_multifd_new();
    }

    return true;
}
