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

/* type safety */
typedef struct VFIOStateBuffers {
    GArray *array;
} VFIOStateBuffers;

typedef struct VFIOStateBuffer {
    bool is_present;
    char *data;
    size_t len;
} VFIOStateBuffer;

typedef struct VFIOMultifd {
    VFIOStateBuffers load_bufs;
    QemuCond load_bufs_buffer_ready_cond;
    QemuMutex load_bufs_mutex; /* Lock order: this lock -> BQL */
    uint32_t load_buf_idx;
    uint32_t load_buf_idx_last;
} VFIOMultifd;

static void vfio_state_buffer_clear(gpointer data)
{
    VFIOStateBuffer *lb = data;

    if (!lb->is_present) {
        return;
    }

    g_clear_pointer(&lb->data, g_free);
    lb->is_present = false;
}

static void vfio_state_buffers_init(VFIOStateBuffers *bufs)
{
    bufs->array = g_array_new(FALSE, TRUE, sizeof(VFIOStateBuffer));
    g_array_set_clear_func(bufs->array, vfio_state_buffer_clear);
}

static void vfio_state_buffers_destroy(VFIOStateBuffers *bufs)
{
    g_clear_pointer(&bufs->array, g_array_unref);
}

static void vfio_state_buffers_assert_init(VFIOStateBuffers *bufs)
{
    assert(bufs->array);
}

static unsigned int vfio_state_buffers_size_get(VFIOStateBuffers *bufs)
{
    return bufs->array->len;
}

static void vfio_state_buffers_size_set(VFIOStateBuffers *bufs,
                                        unsigned int size)
{
    g_array_set_size(bufs->array, size);
}

static VFIOStateBuffer *vfio_state_buffers_at(VFIOStateBuffers *bufs,
                                              unsigned int idx)
{
    return &g_array_index(bufs->array, VFIOStateBuffer, idx);
}

/* called with load_bufs_mutex locked */
static bool vfio_load_state_buffer_insert(VFIODevice *vbasedev,
                                          VFIODeviceStatePacket *packet,
                                          size_t packet_total_size,
                                          Error **errp)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIOMultifd *multifd = migration->multifd;
    VFIOStateBuffer *lb;

    vfio_state_buffers_assert_init(&multifd->load_bufs);
    if (packet->idx >= vfio_state_buffers_size_get(&multifd->load_bufs)) {
        vfio_state_buffers_size_set(&multifd->load_bufs, packet->idx + 1);
    }

    lb = vfio_state_buffers_at(&multifd->load_bufs, packet->idx);
    if (lb->is_present) {
        error_setg(errp, "%s: state buffer %" PRIu32 " already filled",
                   vbasedev->name, packet->idx);
        return false;
    }

    assert(packet->idx >= multifd->load_buf_idx);

    lb->data = g_memdup2(&packet->data, packet_total_size - sizeof(*packet));
    lb->len = packet_total_size - sizeof(*packet);
    lb->is_present = true;

    return true;
}

bool vfio_multifd_load_state_buffer(void *opaque, char *data, size_t data_size,
                                    Error **errp)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    VFIOMultifd *multifd = migration->multifd;
    VFIODeviceStatePacket *packet = (VFIODeviceStatePacket *)data;

    if (!vfio_multifd_transfer_enabled(vbasedev)) {
        error_setg(errp,
                   "%s: got device state packet but not doing multifd transfer",
                   vbasedev->name);
        return false;
    }

    assert(multifd);

    if (data_size < sizeof(*packet)) {
        error_setg(errp, "%s: packet too short at %zu (min is %zu)",
                   vbasedev->name, data_size, sizeof(*packet));
        return false;
    }

    if (packet->version != VFIO_DEVICE_STATE_PACKET_VER_CURRENT) {
        error_setg(errp, "%s: packet has unknown version %" PRIu32,
                   vbasedev->name, packet->version);
        return false;
    }

    if (packet->idx == UINT32_MAX) {
        error_setg(errp, "%s: packet index is invalid", vbasedev->name);
        return false;
    }

    trace_vfio_load_state_device_buffer_incoming(vbasedev->name, packet->idx);

    /*
     * Holding BQL here would violate the lock order and can cause
     * a deadlock once we attempt to lock load_bufs_mutex below.
     */
    assert(!bql_locked());

    WITH_QEMU_LOCK_GUARD(&multifd->load_bufs_mutex) {
        /* config state packet should be the last one in the stream */
        if (packet->flags & VFIO_DEVICE_STATE_CONFIG_STATE) {
            multifd->load_buf_idx_last = packet->idx;
        }

        if (!vfio_load_state_buffer_insert(vbasedev, packet, data_size,
                                           errp)) {
            return false;
        }

        qemu_cond_signal(&multifd->load_bufs_buffer_ready_cond);
    }

    return true;
}

static VFIOMultifd *vfio_multifd_new(void)
{
    VFIOMultifd *multifd = g_new(VFIOMultifd, 1);

    vfio_state_buffers_init(&multifd->load_bufs);

    qemu_mutex_init(&multifd->load_bufs_mutex);

    multifd->load_buf_idx = 0;
    multifd->load_buf_idx_last = UINT32_MAX;
    qemu_cond_init(&multifd->load_bufs_buffer_ready_cond);

    return multifd;
}

static void vfio_multifd_free(VFIOMultifd *multifd)
{
    vfio_state_buffers_destroy(&multifd->load_bufs);
    qemu_cond_destroy(&multifd->load_bufs_buffer_ready_cond);
    qemu_mutex_destroy(&multifd->load_bufs_mutex);

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
