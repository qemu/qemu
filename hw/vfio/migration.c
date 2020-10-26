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

static SaveVMHandlers savevm_vfio_handlers = {
    .save_setup = vfio_save_setup,
    .save_cleanup = vfio_save_cleanup,
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
