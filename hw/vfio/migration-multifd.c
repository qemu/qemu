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
#include "hw/vfio/vfio-device.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "io/channel-buffer.h"
#include "migration/qemu-file.h"
#include "migration-multifd.h"
#include "vfio-migration-internal.h"
#include "trace.h"
#include "vfio-helpers.h"

#define VFIO_DEVICE_STATE_CONFIG_STATE (1)

#define VFIO_DEVICE_STATE_PACKET_VER_CURRENT (0)

typedef struct VFIODeviceStatePacket {
    uint32_t version;
    uint32_t idx;
    uint32_t flags;
    uint8_t data[0];
} QEMU_PACKED VFIODeviceStatePacket;

bool vfio_load_config_after_iter(VFIODevice *vbasedev)
{
    if (vbasedev->migration_load_config_after_iter == ON_OFF_AUTO_ON) {
        return true;
    } else if (vbasedev->migration_load_config_after_iter == ON_OFF_AUTO_OFF) {
        return false;
    }

    assert(vbasedev->migration_load_config_after_iter == ON_OFF_AUTO_AUTO);
    return vfio_arch_wants_loading_config_after_iter();
}

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
    bool load_bufs_thread_running;
    bool load_bufs_thread_want_exit;

    bool load_bufs_iter_done;
    QemuCond load_bufs_iter_done_cond;

    VFIOStateBuffers load_bufs;
    QemuCond load_bufs_buffer_ready_cond;
    QemuCond load_bufs_thread_finished_cond;
    QemuMutex load_bufs_mutex; /* Lock order: this lock -> BQL */
    uint32_t load_buf_idx;
    uint32_t load_buf_idx_last;
    size_t load_buf_queued_pending_buffers_size;
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
    size_t data_size = packet_total_size - sizeof(*packet);

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

    multifd->load_buf_queued_pending_buffers_size += data_size;
    if (multifd->load_buf_queued_pending_buffers_size >
        vbasedev->migration_max_queued_buffers_size) {
        error_setg(errp,
                   "%s: queuing state buffer %" PRIu32
                   " would exceed the size max of %" PRIu64,
                   vbasedev->name, packet->idx,
                   vbasedev->migration_max_queued_buffers_size);
        return false;
    }

    lb->data = g_memdup2(&packet->data, data_size);
    lb->len = data_size;
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

    packet->version = be32_to_cpu(packet->version);
    if (packet->version != VFIO_DEVICE_STATE_PACKET_VER_CURRENT) {
        error_setg(errp, "%s: packet has unknown version %" PRIu32,
                   vbasedev->name, packet->version);
        return false;
    }

    packet->idx = be32_to_cpu(packet->idx);
    packet->flags = be32_to_cpu(packet->flags);

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

static bool vfio_load_bufs_thread_load_config(VFIODevice *vbasedev,
                                              Error **errp)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIOMultifd *multifd = migration->multifd;
    VFIOStateBuffer *lb;
    g_autoptr(QIOChannelBuffer) bioc = NULL;
    g_autoptr(QEMUFile) f_out = NULL, f_in = NULL;
    uint64_t mig_header;
    int ret;

    assert(multifd->load_buf_idx == multifd->load_buf_idx_last);
    lb = vfio_state_buffers_at(&multifd->load_bufs, multifd->load_buf_idx);
    assert(lb->is_present);

    bioc = qio_channel_buffer_new(lb->len);
    qio_channel_set_name(QIO_CHANNEL(bioc), "vfio-device-config-load");

    f_out = qemu_file_new_output(QIO_CHANNEL(bioc));
    qemu_put_buffer(f_out, (uint8_t *)lb->data, lb->len);

    ret = qemu_fflush(f_out);
    if (ret) {
        error_setg(errp, "%s: load config state flush failed: %d",
                   vbasedev->name, ret);
        return false;
    }

    qio_channel_io_seek(QIO_CHANNEL(bioc), 0, 0, NULL);
    f_in = qemu_file_new_input(QIO_CHANNEL(bioc));

    mig_header = qemu_get_be64(f_in);
    if (mig_header != VFIO_MIG_FLAG_DEV_CONFIG_STATE) {
        error_setg(errp, "%s: expected FLAG_DEV_CONFIG_STATE but got %" PRIx64,
                   vbasedev->name, mig_header);
        return false;
    }

    bql_lock();
    ret = vfio_load_device_config_state(f_in, vbasedev);
    bql_unlock();

    if (ret < 0) {
        error_setg(errp, "%s: vfio_load_device_config_state() failed: %d",
                   vbasedev->name, ret);
        return false;
    }

    return true;
}

static VFIOStateBuffer *vfio_load_state_buffer_get(VFIOMultifd *multifd)
{
    VFIOStateBuffer *lb;
    unsigned int bufs_len;

    bufs_len = vfio_state_buffers_size_get(&multifd->load_bufs);
    if (multifd->load_buf_idx >= bufs_len) {
        assert(multifd->load_buf_idx == bufs_len);
        return NULL;
    }

    lb = vfio_state_buffers_at(&multifd->load_bufs,
                               multifd->load_buf_idx);
    if (!lb->is_present) {
        return NULL;
    }

    return lb;
}

static bool vfio_load_state_buffer_write(VFIODevice *vbasedev,
                                         VFIOStateBuffer *lb,
                                         Error **errp)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIOMultifd *multifd = migration->multifd;
    g_autofree char *buf = NULL;
    char *buf_cur;
    size_t buf_len;

    if (!lb->len) {
        return true;
    }

    trace_vfio_load_state_device_buffer_load_start(vbasedev->name,
                                                   multifd->load_buf_idx);

    /* lb might become re-allocated when we drop the lock */
    buf = g_steal_pointer(&lb->data);
    buf_cur = buf;
    buf_len = lb->len;
    while (buf_len > 0) {
        ssize_t wr_ret;
        int errno_save;

        /*
         * Loading data to the device takes a while,
         * drop the lock during this process.
         */
        qemu_mutex_unlock(&multifd->load_bufs_mutex);
        wr_ret = write(migration->data_fd, buf_cur, buf_len);
        errno_save = errno;
        qemu_mutex_lock(&multifd->load_bufs_mutex);

        if (wr_ret < 0) {
            error_setg(errp,
                       "%s: writing state buffer %" PRIu32 " failed: %d",
                       vbasedev->name, multifd->load_buf_idx, errno_save);
            return false;
        }

        assert(wr_ret <= buf_len);
        buf_len -= wr_ret;
        buf_cur += wr_ret;

        assert(multifd->load_buf_queued_pending_buffers_size >= wr_ret);
        multifd->load_buf_queued_pending_buffers_size -= wr_ret;
    }

    trace_vfio_load_state_device_buffer_load_end(vbasedev->name,
                                                 multifd->load_buf_idx);

    return true;
}

static bool vfio_load_bufs_thread_want_exit(VFIOMultifd *multifd,
                                            bool *should_quit)
{
    return multifd->load_bufs_thread_want_exit || qatomic_read(should_quit);
}

/*
 * This thread is spawned by vfio_multifd_switchover_start() which gets
 * called upon encountering the switchover point marker in main migration
 * stream.
 *
 * It exits after either:
 * * completing loading the remaining device state and device config, OR:
 * * encountering some error while doing the above, OR:
 * * being forcefully aborted by the migration core by it setting should_quit
 *   or by vfio_load_cleanup_load_bufs_thread() setting
 *   multifd->load_bufs_thread_want_exit.
 */
static bool vfio_load_bufs_thread(void *opaque, bool *should_quit, Error **errp)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    VFIOMultifd *multifd = migration->multifd;
    bool ret = false;

    trace_vfio_load_bufs_thread_start(vbasedev->name);

    assert(multifd);
    QEMU_LOCK_GUARD(&multifd->load_bufs_mutex);

    assert(multifd->load_bufs_thread_running);

    while (true) {
        VFIOStateBuffer *lb;

        /*
         * Always check cancellation first after the buffer_ready wait below in
         * case that cond was signalled by vfio_load_cleanup_load_bufs_thread().
         */
        if (vfio_load_bufs_thread_want_exit(multifd, should_quit)) {
            error_setg(errp, "operation cancelled");
            goto thread_exit;
        }

        assert(multifd->load_buf_idx <= multifd->load_buf_idx_last);

        lb = vfio_load_state_buffer_get(multifd);
        if (!lb) {
            trace_vfio_load_state_device_buffer_starved(vbasedev->name,
                                                        multifd->load_buf_idx);
            qemu_cond_wait(&multifd->load_bufs_buffer_ready_cond,
                           &multifd->load_bufs_mutex);
            continue;
        }

        if (multifd->load_buf_idx == multifd->load_buf_idx_last) {
            break;
        }

        if (multifd->load_buf_idx == 0) {
            trace_vfio_load_state_device_buffer_start(vbasedev->name);
        }

        if (!vfio_load_state_buffer_write(vbasedev, lb, errp)) {
            goto thread_exit;
        }

        if (multifd->load_buf_idx == multifd->load_buf_idx_last - 1) {
            trace_vfio_load_state_device_buffer_end(vbasedev->name);
        }

        multifd->load_buf_idx++;
    }

    if (vfio_load_config_after_iter(vbasedev)) {
        while (!multifd->load_bufs_iter_done) {
            qemu_cond_wait(&multifd->load_bufs_iter_done_cond,
                           &multifd->load_bufs_mutex);

            /*
             * Need to re-check cancellation immediately after wait in case
             * cond was signalled by vfio_load_cleanup_load_bufs_thread().
             */
            if (vfio_load_bufs_thread_want_exit(multifd, should_quit)) {
                error_setg(errp, "operation cancelled");
                goto thread_exit;
            }
        }
    }

    if (!vfio_load_bufs_thread_load_config(vbasedev, errp)) {
        goto thread_exit;
    }

    ret = true;

thread_exit:
    /*
     * Notify possibly waiting vfio_load_cleanup_load_bufs_thread() that
     * this thread is exiting.
     */
    multifd->load_bufs_thread_running = false;
    qemu_cond_signal(&multifd->load_bufs_thread_finished_cond);

    trace_vfio_load_bufs_thread_end(vbasedev->name);

    return ret;
}

int vfio_load_state_config_load_ready(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIOMultifd *multifd = migration->multifd;
    int ret = 0;

    if (!vfio_multifd_transfer_enabled(vbasedev)) {
        error_report("%s: got DEV_CONFIG_LOAD_READY outside multifd transfer",
                     vbasedev->name);
        return -EINVAL;
    }

    if (!vfio_load_config_after_iter(vbasedev)) {
        error_report("%s: got DEV_CONFIG_LOAD_READY but was disabled",
                     vbasedev->name);
        return -EINVAL;
    }

    assert(multifd);

    /* The lock order is load_bufs_mutex -> BQL so unlock BQL here first */
    bql_unlock();
    WITH_QEMU_LOCK_GUARD(&multifd->load_bufs_mutex) {
        if (multifd->load_bufs_iter_done) {
            /* Can't print error here as we're outside BQL */
            ret = -EINVAL;
            break;
        }

        multifd->load_bufs_iter_done = true;
        qemu_cond_signal(&multifd->load_bufs_iter_done_cond);
    }
    bql_lock();

    if (ret) {
        error_report("%s: duplicate DEV_CONFIG_LOAD_READY",
                     vbasedev->name);
    }

    return ret;
}

static VFIOMultifd *vfio_multifd_new(void)
{
    VFIOMultifd *multifd = g_new(VFIOMultifd, 1);

    vfio_state_buffers_init(&multifd->load_bufs);

    qemu_mutex_init(&multifd->load_bufs_mutex);

    multifd->load_buf_idx = 0;
    multifd->load_buf_idx_last = UINT32_MAX;
    multifd->load_buf_queued_pending_buffers_size = 0;
    qemu_cond_init(&multifd->load_bufs_buffer_ready_cond);

    multifd->load_bufs_iter_done = false;
    qemu_cond_init(&multifd->load_bufs_iter_done_cond);

    multifd->load_bufs_thread_running = false;
    multifd->load_bufs_thread_want_exit = false;
    qemu_cond_init(&multifd->load_bufs_thread_finished_cond);

    return multifd;
}

/*
 * Terminates vfio_load_bufs_thread by setting
 * multifd->load_bufs_thread_want_exit and signalling all the conditions
 * the thread could be blocked on.
 *
 * Waits for the thread to signal that it had finished.
 */
static void vfio_load_cleanup_load_bufs_thread(VFIOMultifd *multifd)
{
    /* The lock order is load_bufs_mutex -> BQL so unlock BQL here first */
    bql_unlock();
    WITH_QEMU_LOCK_GUARD(&multifd->load_bufs_mutex) {
        while (multifd->load_bufs_thread_running) {
            multifd->load_bufs_thread_want_exit = true;

            qemu_cond_signal(&multifd->load_bufs_buffer_ready_cond);
            qemu_cond_signal(&multifd->load_bufs_iter_done_cond);
            qemu_cond_wait(&multifd->load_bufs_thread_finished_cond,
                           &multifd->load_bufs_mutex);
        }
    }
    bql_lock();
}

static void vfio_multifd_free(VFIOMultifd *multifd)
{
    vfio_load_cleanup_load_bufs_thread(multifd);

    qemu_cond_destroy(&multifd->load_bufs_thread_finished_cond);
    qemu_cond_destroy(&multifd->load_bufs_iter_done_cond);
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
    VFIOMigration *migration = vbasedev->migration;

    return migration->multifd_transfer;
}

bool vfio_multifd_setup(VFIODevice *vbasedev, bool alloc_multifd, Error **errp)
{
    VFIOMigration *migration = vbasedev->migration;

    /*
     * Make a copy of this setting at the start in case it is changed
     * mid-migration.
     */
    if (vbasedev->migration_multifd_transfer == ON_OFF_AUTO_AUTO) {
        migration->multifd_transfer = vfio_multifd_transfer_supported();
    } else {
        migration->multifd_transfer =
            vbasedev->migration_multifd_transfer == ON_OFF_AUTO_ON;
    }

    if (!vfio_multifd_transfer_enabled(vbasedev)) {
        /* Nothing further to check or do */
        return true;
    }

    if (!vfio_multifd_transfer_supported()) {
        error_setg(errp,
                   "%s: Multifd device transfer requested but unsupported in the current config",
                   vbasedev->name);
        return false;
    }

    if (alloc_multifd) {
        assert(!migration->multifd);
        migration->multifd = vfio_multifd_new();
    }

    return true;
}

void vfio_multifd_emit_dummy_eos(VFIODevice *vbasedev, QEMUFile *f)
{
    assert(vfio_multifd_transfer_enabled(vbasedev));

    /*
     * Emit dummy NOP data on the main migration channel since the actual
     * device state transfer is done via multifd channels.
     */
    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);
}

static bool
vfio_save_complete_precopy_thread_config_state(VFIODevice *vbasedev,
                                               char *idstr,
                                               uint32_t instance_id,
                                               uint32_t idx,
                                               Error **errp)
{
    g_autoptr(QIOChannelBuffer) bioc = NULL;
    g_autoptr(QEMUFile) f = NULL;
    int ret;
    g_autofree VFIODeviceStatePacket *packet = NULL;
    size_t packet_len;

    bioc = qio_channel_buffer_new(0);
    qio_channel_set_name(QIO_CHANNEL(bioc), "vfio-device-config-save");

    f = qemu_file_new_output(QIO_CHANNEL(bioc));

    if (vfio_save_device_config_state(f, vbasedev, errp)) {
        return false;
    }

    ret = qemu_fflush(f);
    if (ret) {
        error_setg(errp, "%s: save config state flush failed: %d",
                   vbasedev->name, ret);
        return false;
    }

    packet_len = sizeof(*packet) + bioc->usage;
    packet = g_malloc0(packet_len);
    packet->version = cpu_to_be32(VFIO_DEVICE_STATE_PACKET_VER_CURRENT);
    packet->idx = cpu_to_be32(idx);
    packet->flags = cpu_to_be32(VFIO_DEVICE_STATE_CONFIG_STATE);
    memcpy(&packet->data, bioc->data, bioc->usage);

    if (!multifd_queue_device_state(idstr, instance_id,
                                    (char *)packet, packet_len)) {
        error_setg(errp, "%s: multifd config data queuing failed",
                   vbasedev->name);
        return false;
    }

    vfio_migration_add_bytes_transferred(packet_len);

    return true;
}

/*
 * This thread is spawned by the migration core directly via
 * .save_complete_precopy_thread SaveVMHandler.
 *
 * It exits after either:
 * * completing saving the remaining device state and device config, OR:
 * * encountering some error while doing the above, OR:
 * * being forcefully aborted by the migration core by
 *   multifd_device_state_save_thread_should_exit() returning true.
 */
bool
vfio_multifd_save_complete_precopy_thread(SaveCompletePrecopyThreadData *d,
                                          Error **errp)
{
    VFIODevice *vbasedev = d->handler_opaque;
    VFIOMigration *migration = vbasedev->migration;
    bool ret = false;
    g_autofree VFIODeviceStatePacket *packet = NULL;
    uint32_t idx;

    if (!vfio_multifd_transfer_enabled(vbasedev)) {
        /* Nothing to do, vfio_save_complete_precopy() does the transfer. */
        return true;
    }

    trace_vfio_save_complete_precopy_thread_start(vbasedev->name,
                                                  d->idstr, d->instance_id);

    /* We reach here with device state STOP or STOP_COPY only */
    if (vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_STOP_COPY,
                                 VFIO_DEVICE_STATE_STOP, errp)) {
        goto thread_exit;
    }

    packet = g_malloc0(sizeof(*packet) + migration->data_buffer_size);
    packet->version = cpu_to_be32(VFIO_DEVICE_STATE_PACKET_VER_CURRENT);

    for (idx = 0; ; idx++) {
        ssize_t data_size;
        size_t packet_size;

        if (multifd_device_state_save_thread_should_exit()) {
            error_setg(errp, "operation cancelled");
            goto thread_exit;
        }

        data_size = read(migration->data_fd, &packet->data,
                         migration->data_buffer_size);
        if (data_size < 0) {
            error_setg(errp, "%s: reading state buffer %" PRIu32 " failed: %d",
                       vbasedev->name, idx, errno);
            goto thread_exit;
        } else if (data_size == 0) {
            break;
        }

        packet->idx = cpu_to_be32(idx);
        packet_size = sizeof(*packet) + data_size;

        if (!multifd_queue_device_state(d->idstr, d->instance_id,
                                        (char *)packet, packet_size)) {
            error_setg(errp, "%s: multifd data queuing failed", vbasedev->name);
            goto thread_exit;
        }

        vfio_migration_add_bytes_transferred(packet_size);
    }

    if (!vfio_save_complete_precopy_thread_config_state(vbasedev,
                                                        d->idstr,
                                                        d->instance_id,
                                                        idx, errp)) {
        goto thread_exit;
   }

    ret = true;

thread_exit:
    trace_vfio_save_complete_precopy_thread_end(vbasedev->name, ret);

    return ret;
}

int vfio_multifd_switchover_start(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIOMultifd *multifd = migration->multifd;

    assert(multifd);

    /* The lock order is load_bufs_mutex -> BQL so unlock BQL here first */
    bql_unlock();
    WITH_QEMU_LOCK_GUARD(&multifd->load_bufs_mutex) {
        assert(!multifd->load_bufs_thread_running);
        multifd->load_bufs_thread_running = true;
    }
    bql_lock();

    qemu_loadvm_start_load_thread(vfio_load_bufs_thread, vbasedev);

    return 0;
}
