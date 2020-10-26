/*
 * Migration support for VFIO devices
 *
 * Copyright NVIDIA, Inc. 2020
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include <linux/vfio.h>

#include "sysemu/runstate.h"
#include "hw/vfio/vfio-common.h"
#include "cpu.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "migration/qemu-file.h"
#include "migration/register.h"
#include "migration/blocker.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "exec/ramlist.h"
#include "exec/ram_addr.h"
#include "pci.h"
#include "trace.h"
#include "hw/hw.h"

/*
 * Flags to be used as unique delimiters for VFIO devices in the migration
 * stream. These flags are composed as:
 * 0xffffffff => MSB 32-bit all 1s
 * 0xef10     => Magic ID, represents emulated (virtual) function IO
 * 0x0000     => 16-bits reserved for flags
 *
 * The beginning of state information is marked by _DEV_CONFIG_STATE,
 * _DEV_SETUP_STATE, or _DEV_DATA_STATE, respectively. The end of a
 * certain state information is marked by _END_OF_STATE.
 */
#define VFIO_MIG_FLAG_END_OF_STATE      (0xffffffffef100001ULL)
#define VFIO_MIG_FLAG_DEV_CONFIG_STATE  (0xffffffffef100002ULL)
#define VFIO_MIG_FLAG_DEV_SETUP_STATE   (0xffffffffef100003ULL)
#define VFIO_MIG_FLAG_DEV_DATA_STATE    (0xffffffffef100004ULL)

static inline int vfio_mig_access(VFIODevice *vbasedev, void *val, int count,
                                  off_t off, bool iswrite)
{
    int ret;

    ret = iswrite ? pwrite(vbasedev->fd, val, count, off) :
                    pread(vbasedev->fd, val, count, off);
    if (ret < count) {
        error_report("vfio_mig_%s %d byte %s: failed at offset 0x%"
                     HWADDR_PRIx", err: %s", iswrite ? "write" : "read", count,
                     vbasedev->name, off, strerror(errno));
        return (ret < 0) ? ret : -EINVAL;
    }
    return 0;
}

static int vfio_mig_rw(VFIODevice *vbasedev, __u8 *buf, size_t count,
                       off_t off, bool iswrite)
{
    int ret, done = 0;
    __u8 *tbuf = buf;

    while (count) {
        int bytes = 0;

        if (count >= 8 && !(off % 8)) {
            bytes = 8;
        } else if (count >= 4 && !(off % 4)) {
            bytes = 4;
        } else if (count >= 2 && !(off % 2)) {
            bytes = 2;
        } else {
            bytes = 1;
        }

        ret = vfio_mig_access(vbasedev, tbuf, bytes, off, iswrite);
        if (ret) {
            return ret;
        }

        count -= bytes;
        done += bytes;
        off += bytes;
        tbuf += bytes;
    }
    return done;
}

#define vfio_mig_read(f, v, c, o)       vfio_mig_rw(f, (__u8 *)v, c, o, false)
#define vfio_mig_write(f, v, c, o)      vfio_mig_rw(f, (__u8 *)v, c, o, true)

#define VFIO_MIG_STRUCT_OFFSET(f)       \
                                 offsetof(struct vfio_device_migration_info, f)
/*
 * Change the device_state register for device @vbasedev. Bits set in @mask
 * are preserved, bits set in @value are set, and bits not set in either @mask
 * or @value are cleared in device_state. If the register cannot be accessed,
 * the resulting state would be invalid, or the device enters an error state,
 * an error is returned.
 */

static int vfio_migration_set_state(VFIODevice *vbasedev, uint32_t mask,
                                    uint32_t value)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
    off_t dev_state_off = region->fd_offset +
                          VFIO_MIG_STRUCT_OFFSET(device_state);
    uint32_t device_state;
    int ret;

    ret = vfio_mig_read(vbasedev, &device_state, sizeof(device_state),
                        dev_state_off);
    if (ret < 0) {
        return ret;
    }

    device_state = (device_state & mask) | value;

    if (!VFIO_DEVICE_STATE_VALID(device_state)) {
        return -EINVAL;
    }

    ret = vfio_mig_write(vbasedev, &device_state, sizeof(device_state),
                         dev_state_off);
    if (ret < 0) {
        int rret;

        rret = vfio_mig_read(vbasedev, &device_state, sizeof(device_state),
                             dev_state_off);

        if ((rret < 0) || (VFIO_DEVICE_STATE_IS_ERROR(device_state))) {
            hw_error("%s: Device in error state 0x%x", vbasedev->name,
                     device_state);
            return rret ? rret : -EIO;
        }
        return ret;
    }

    migration->device_state = device_state;
    trace_vfio_migration_set_state(vbasedev->name, device_state);
    return 0;
}

static void *get_data_section_size(VFIORegion *region, uint64_t data_offset,
                                   uint64_t data_size, uint64_t *size)
{
    void *ptr = NULL;
    uint64_t limit = 0;
    int i;

    if (!region->mmaps) {
        if (size) {
            *size = MIN(data_size, region->size - data_offset);
        }
        return ptr;
    }

    for (i = 0; i < region->nr_mmaps; i++) {
        VFIOMmap *map = region->mmaps + i;

        if ((data_offset >= map->offset) &&
            (data_offset < map->offset + map->size)) {

            /* check if data_offset is within sparse mmap areas */
            ptr = map->mmap + data_offset - map->offset;
            if (size) {
                *size = MIN(data_size, map->offset + map->size - data_offset);
            }
            break;
        } else if ((data_offset < map->offset) &&
                   (!limit || limit > map->offset)) {
            /*
             * data_offset is not within sparse mmap areas, find size of
             * non-mapped area. Check through all list since region->mmaps list
             * is not sorted.
             */
            limit = map->offset;
        }
    }

    if (!ptr && size) {
        *size = limit ? MIN(data_size, limit - data_offset) : data_size;
    }
    return ptr;
}

static int vfio_save_buffer(QEMUFile *f, VFIODevice *vbasedev, uint64_t *size)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
    uint64_t data_offset = 0, data_size = 0, sz;
    int ret;

    ret = vfio_mig_read(vbasedev, &data_offset, sizeof(data_offset),
                      region->fd_offset + VFIO_MIG_STRUCT_OFFSET(data_offset));
    if (ret < 0) {
        return ret;
    }

    ret = vfio_mig_read(vbasedev, &data_size, sizeof(data_size),
                        region->fd_offset + VFIO_MIG_STRUCT_OFFSET(data_size));
    if (ret < 0) {
        return ret;
    }

    trace_vfio_save_buffer(vbasedev->name, data_offset, data_size,
                           migration->pending_bytes);

    qemu_put_be64(f, data_size);
    sz = data_size;

    while (sz) {
        void *buf;
        uint64_t sec_size;
        bool buf_allocated = false;

        buf = get_data_section_size(region, data_offset, sz, &sec_size);

        if (!buf) {
            buf = g_try_malloc(sec_size);
            if (!buf) {
                error_report("%s: Error allocating buffer ", __func__);
                return -ENOMEM;
            }
            buf_allocated = true;

            ret = vfio_mig_read(vbasedev, buf, sec_size,
                                region->fd_offset + data_offset);
            if (ret < 0) {
                g_free(buf);
                return ret;
            }
        }

        qemu_put_buffer(f, buf, sec_size);

        if (buf_allocated) {
            g_free(buf);
        }
        sz -= sec_size;
        data_offset += sec_size;
    }

    ret = qemu_file_get_error(f);

    if (!ret && size) {
        *size = data_size;
    }

    return ret;
}

static int vfio_load_buffer(QEMUFile *f, VFIODevice *vbasedev,
                            uint64_t data_size)
{
    VFIORegion *region = &vbasedev->migration->region;
    uint64_t data_offset = 0, size, report_size;
    int ret;

    do {
        ret = vfio_mig_read(vbasedev, &data_offset, sizeof(data_offset),
                      region->fd_offset + VFIO_MIG_STRUCT_OFFSET(data_offset));
        if (ret < 0) {
            return ret;
        }

        if (data_offset + data_size > region->size) {
            /*
             * If data_size is greater than the data section of migration region
             * then iterate the write buffer operation. This case can occur if
             * size of migration region at destination is smaller than size of
             * migration region at source.
             */
            report_size = size = region->size - data_offset;
            data_size -= size;
        } else {
            report_size = size = data_size;
            data_size = 0;
        }

        trace_vfio_load_state_device_data(vbasedev->name, data_offset, size);

        while (size) {
            void *buf;
            uint64_t sec_size;
            bool buf_alloc = false;

            buf = get_data_section_size(region, data_offset, size, &sec_size);

            if (!buf) {
                buf = g_try_malloc(sec_size);
                if (!buf) {
                    error_report("%s: Error allocating buffer ", __func__);
                    return -ENOMEM;
                }
                buf_alloc = true;
            }

            qemu_get_buffer(f, buf, sec_size);

            if (buf_alloc) {
                ret = vfio_mig_write(vbasedev, buf, sec_size,
                        region->fd_offset + data_offset);
                g_free(buf);

                if (ret < 0) {
                    return ret;
                }
            }
            size -= sec_size;
            data_offset += sec_size;
        }

        ret = vfio_mig_write(vbasedev, &report_size, sizeof(report_size),
                        region->fd_offset + VFIO_MIG_STRUCT_OFFSET(data_size));
        if (ret < 0) {
            return ret;
        }
    } while (data_size);

    return 0;
}

static int vfio_update_pending(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
    uint64_t pending_bytes = 0;
    int ret;

    ret = vfio_mig_read(vbasedev, &pending_bytes, sizeof(pending_bytes),
                    region->fd_offset + VFIO_MIG_STRUCT_OFFSET(pending_bytes));
    if (ret < 0) {
        migration->pending_bytes = 0;
        return ret;
    }

    migration->pending_bytes = pending_bytes;
    trace_vfio_update_pending(vbasedev->name, pending_bytes);
    return 0;
}

static int vfio_save_device_config_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_CONFIG_STATE);

    if (vbasedev->ops && vbasedev->ops->vfio_save_config) {
        vbasedev->ops->vfio_save_config(vbasedev, f);
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    trace_vfio_save_device_config_state(vbasedev->name);

    return qemu_file_get_error(f);
}

static int vfio_load_device_config_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    uint64_t data;

    if (vbasedev->ops && vbasedev->ops->vfio_load_config) {
        int ret;

        ret = vbasedev->ops->vfio_load_config(vbasedev, f);
        if (ret) {
            error_report("%s: Failed to load device config space",
                         vbasedev->name);
            return ret;
        }
    }

    data = qemu_get_be64(f);
    if (data != VFIO_MIG_FLAG_END_OF_STATE) {
        error_report("%s: Failed loading device config space, "
                     "end flag incorrect 0x%"PRIx64, vbasedev->name, data);
        return -EINVAL;
    }

    trace_vfio_load_device_config_state(vbasedev->name);
    return qemu_file_get_error(f);
}

static void vfio_migration_cleanup(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    if (migration->region.mmaps) {
        vfio_region_unmap(&migration->region);
    }
}

/* ---------------------------------------------------------------------- */

static int vfio_save_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    trace_vfio_save_setup(vbasedev->name);

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_SETUP_STATE);

    if (migration->region.mmaps) {
        /*
         * Calling vfio_region_mmap() from migration thread. Memory API called
         * from this function require locking the iothread when called from
         * outside the main loop thread.
         */
        qemu_mutex_lock_iothread();
        ret = vfio_region_mmap(&migration->region);
        qemu_mutex_unlock_iothread();
        if (ret) {
            error_report("%s: Failed to mmap VFIO migration region: %s",
                         vbasedev->name, strerror(-ret));
            error_report("%s: Falling back to slow path", vbasedev->name);
        }
    }

    ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_MASK,
                                   VFIO_DEVICE_STATE_SAVING);
    if (ret) {
        error_report("%s: Failed to set state SAVING", vbasedev->name);
        return ret;
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    return 0;
}

static void vfio_save_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;

    vfio_migration_cleanup(vbasedev);
    trace_vfio_save_cleanup(vbasedev->name);
}

static void vfio_save_pending(QEMUFile *f, void *opaque,
                              uint64_t threshold_size,
                              uint64_t *res_precopy_only,
                              uint64_t *res_compatible,
                              uint64_t *res_postcopy_only)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    ret = vfio_update_pending(vbasedev);
    if (ret) {
        return;
    }

    *res_precopy_only += migration->pending_bytes;

    trace_vfio_save_pending(vbasedev->name, *res_precopy_only,
                            *res_postcopy_only, *res_compatible);
}

static int vfio_save_iterate(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint64_t data_size;
    int ret;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_DATA_STATE);

    if (migration->pending_bytes == 0) {
        ret = vfio_update_pending(vbasedev);
        if (ret) {
            return ret;
        }

        if (migration->pending_bytes == 0) {
            qemu_put_be64(f, 0);
            qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);
            /* indicates data finished, goto complete phase */
            return 1;
        }
    }

    ret = vfio_save_buffer(f, vbasedev, &data_size);
    if (ret) {
        error_report("%s: vfio_save_buffer failed %s", vbasedev->name,
                     strerror(errno));
        return ret;
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    /*
     * Reset pending_bytes as .save_live_pending is not called during savevm or
     * snapshot case, in such case vfio_update_pending() at the start of this
     * function updates pending_bytes.
     */
    migration->pending_bytes = 0;
    trace_vfio_save_iterate(vbasedev->name, data_size);
    return 0;
}

static int vfio_save_complete_precopy(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint64_t data_size;
    int ret;

    ret = vfio_migration_set_state(vbasedev, ~VFIO_DEVICE_STATE_RUNNING,
                                   VFIO_DEVICE_STATE_SAVING);
    if (ret) {
        error_report("%s: Failed to set state STOP and SAVING",
                     vbasedev->name);
        return ret;
    }

    ret = vfio_save_device_config_state(f, opaque);
    if (ret) {
        return ret;
    }

    ret = vfio_update_pending(vbasedev);
    if (ret) {
        return ret;
    }

    while (migration->pending_bytes > 0) {
        qemu_put_be64(f, VFIO_MIG_FLAG_DEV_DATA_STATE);
        ret = vfio_save_buffer(f, vbasedev, &data_size);
        if (ret < 0) {
            error_report("%s: Failed to save buffer", vbasedev->name);
            return ret;
        }

        if (data_size == 0) {
            break;
        }

        ret = vfio_update_pending(vbasedev);
        if (ret) {
            return ret;
        }
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    ret = vfio_migration_set_state(vbasedev, ~VFIO_DEVICE_STATE_SAVING, 0);
    if (ret) {
        error_report("%s: Failed to set state STOPPED", vbasedev->name);
        return ret;
    }

    trace_vfio_save_complete_precopy(vbasedev->name);
    return ret;
}

static int vfio_load_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret = 0;

    if (migration->region.mmaps) {
        ret = vfio_region_mmap(&migration->region);
        if (ret) {
            error_report("%s: Failed to mmap VFIO migration region %d: %s",
                         vbasedev->name, migration->region.nr,
                         strerror(-ret));
            error_report("%s: Falling back to slow path", vbasedev->name);
        }
    }

    ret = vfio_migration_set_state(vbasedev, ~VFIO_DEVICE_STATE_MASK,
                                   VFIO_DEVICE_STATE_RESUMING);
    if (ret) {
        error_report("%s: Failed to set state RESUMING", vbasedev->name);
        if (migration->region.mmaps) {
            vfio_region_unmap(&migration->region);
        }
    }
    return ret;
}

static int vfio_load_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;

    vfio_migration_cleanup(vbasedev);
    trace_vfio_load_cleanup(vbasedev->name);
    return 0;
}

static int vfio_load_state(QEMUFile *f, void *opaque, int version_id)
{
    VFIODevice *vbasedev = opaque;
    int ret = 0;
    uint64_t data;

    data = qemu_get_be64(f);
    while (data != VFIO_MIG_FLAG_END_OF_STATE) {

        trace_vfio_load_state(vbasedev->name, data);

        switch (data) {
        case VFIO_MIG_FLAG_DEV_CONFIG_STATE:
        {
            ret = vfio_load_device_config_state(f, opaque);
            if (ret) {
                return ret;
            }
            break;
        }
        case VFIO_MIG_FLAG_DEV_SETUP_STATE:
        {
            data = qemu_get_be64(f);
            if (data == VFIO_MIG_FLAG_END_OF_STATE) {
                return ret;
            } else {
                error_report("%s: SETUP STATE: EOS not found 0x%"PRIx64,
                             vbasedev->name, data);
                return -EINVAL;
            }
            break;
        }
        case VFIO_MIG_FLAG_DEV_DATA_STATE:
        {
            uint64_t data_size = qemu_get_be64(f);

            if (data_size) {
                ret = vfio_load_buffer(f, vbasedev, data_size);
                if (ret < 0) {
                    return ret;
                }
            }
            break;
        }
        default:
            error_report("%s: Unknown tag 0x%"PRIx64, vbasedev->name, data);
            return -EINVAL;
        }

        data = qemu_get_be64(f);
        ret = qemu_file_get_error(f);
        if (ret) {
            return ret;
        }
    }
    return ret;
}

static SaveVMHandlers savevm_vfio_handlers = {
    .save_setup = vfio_save_setup,
    .save_cleanup = vfio_save_cleanup,
    .save_live_pending = vfio_save_pending,
    .save_live_iterate = vfio_save_iterate,
    .save_live_complete_precopy = vfio_save_complete_precopy,
    .load_setup = vfio_load_setup,
    .load_cleanup = vfio_load_cleanup,
    .load_state = vfio_load_state,
};

/* ---------------------------------------------------------------------- */

static void vfio_vmstate_change(void *opaque, int running, RunState state)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint32_t value, mask;
    int ret;

    if (vbasedev->migration->vm_running == running) {
        return;
    }

    if (running) {
        /*
         * Here device state can have one of _SAVING, _RESUMING or _STOP bit.
         * Transition from _SAVING to _RUNNING can happen if there is migration
         * failure, in that case clear _SAVING bit.
         * Transition from _RESUMING to _RUNNING occurs during resuming
         * phase, in that case clear _RESUMING bit.
         * In both the above cases, set _RUNNING bit.
         */
        mask = ~VFIO_DEVICE_STATE_MASK;
        value = VFIO_DEVICE_STATE_RUNNING;
    } else {
        /*
         * Here device state could be either _RUNNING or _SAVING|_RUNNING. Reset
         * _RUNNING bit
         */
        mask = ~VFIO_DEVICE_STATE_RUNNING;
        value = 0;
    }

    ret = vfio_migration_set_state(vbasedev, mask, value);
    if (ret) {
        /*
         * Migration should be aborted in this case, but vm_state_notify()
         * currently does not support reporting failures.
         */
        error_report("%s: Failed to set device state 0x%x", vbasedev->name,
                     (migration->device_state & mask) | value);
        qemu_file_set_error(migrate_get_current()->to_dst_file, ret);
    }
    vbasedev->migration->vm_running = running;
    trace_vfio_vmstate_change(vbasedev->name, running, RunState_str(state),
            (migration->device_state & mask) | value);
}

static void vfio_migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;
    VFIOMigration *migration = container_of(notifier, VFIOMigration,
                                            migration_state);
    VFIODevice *vbasedev = migration->vbasedev;
    int ret;

    trace_vfio_migration_state_notifier(vbasedev->name,
                                        MigrationStatus_str(s->state));

    switch (s->state) {
    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_FAILED:
        ret = vfio_migration_set_state(vbasedev,
                      ~(VFIO_DEVICE_STATE_SAVING | VFIO_DEVICE_STATE_RESUMING),
                      VFIO_DEVICE_STATE_RUNNING);
        if (ret) {
            error_report("%s: Failed to set state RUNNING", vbasedev->name);
        }
    }
}

static void vfio_migration_exit(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    vfio_region_exit(&migration->region);
    vfio_region_finalize(&migration->region);
    g_free(vbasedev->migration);
    vbasedev->migration = NULL;
}

static int vfio_migration_init(VFIODevice *vbasedev,
                               struct vfio_region_info *info)
{
    int ret;
    Object *obj;
    VFIOMigration *migration;
    char id[256] = "";
    g_autofree char *path = NULL, *oid = NULL;

    if (!vbasedev->ops->vfio_get_object) {
        return -EINVAL;
    }

    obj = vbasedev->ops->vfio_get_object(vbasedev);
    if (!obj) {
        return -EINVAL;
    }

    vbasedev->migration = g_new0(VFIOMigration, 1);

    ret = vfio_region_setup(obj, vbasedev, &vbasedev->migration->region,
                            info->index, "migration");
    if (ret) {
        error_report("%s: Failed to setup VFIO migration region %d: %s",
                     vbasedev->name, info->index, strerror(-ret));
        goto err;
    }

    if (!vbasedev->migration->region.size) {
        error_report("%s: Invalid zero-sized VFIO migration region %d",
                     vbasedev->name, info->index);
        ret = -EINVAL;
        goto err;
    }

    migration = vbasedev->migration;
    migration->vbasedev = vbasedev;

    oid = vmstate_if_get_id(VMSTATE_IF(DEVICE(obj)));
    if (oid) {
        path = g_strdup_printf("%s/vfio", oid);
    } else {
        path = g_strdup("vfio");
    }
    strpadcpy(id, sizeof(id), path, '\0');

    register_savevm_live(id, VMSTATE_INSTANCE_ID_ANY, 1, &savevm_vfio_handlers,
                         vbasedev);

    migration->vm_state = qemu_add_vm_change_state_handler(vfio_vmstate_change,
                                                           vbasedev);
    migration->migration_state.notify = vfio_migration_state_notifier;
    add_migration_state_change_notifier(&migration->migration_state);
    return 0;

err:
    vfio_migration_exit(vbasedev);
    return ret;
}

/* ---------------------------------------------------------------------- */

int vfio_migration_probe(VFIODevice *vbasedev, Error **errp)
{
    struct vfio_region_info *info = NULL;
    Error *local_err = NULL;
    int ret;

    ret = vfio_get_dev_region_info(vbasedev, VFIO_REGION_TYPE_MIGRATION,
                                   VFIO_REGION_SUBTYPE_MIGRATION, &info);
    if (ret) {
        goto add_blocker;
    }

    ret = vfio_migration_init(vbasedev, info);
    if (ret) {
        goto add_blocker;
    }

    g_free(info);
    trace_vfio_migration_probe(vbasedev->name, info->index);
    return 0;

add_blocker:
    error_setg(&vbasedev->migration_blocker,
               "VFIO device doesn't support migration");
    g_free(info);

    ret = migrate_add_blocker(vbasedev->migration_blocker, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_free(vbasedev->migration_blocker);
        vbasedev->migration_blocker = NULL;
    }
    return ret;
}

void vfio_migration_finalize(VFIODevice *vbasedev)
{
    if (vbasedev->migration) {
        VFIOMigration *migration = vbasedev->migration;

        remove_migration_state_change_notifier(&migration->migration_state);
        qemu_del_vm_change_state_handler(migration->vm_state);
        vfio_migration_exit(vbasedev);
    }

    if (vbasedev->migration_blocker) {
        migrate_del_blocker(vbasedev->migration_blocker);
        error_free(vbasedev->migration_blocker);
        vbasedev->migration_blocker = NULL;
    }
}
