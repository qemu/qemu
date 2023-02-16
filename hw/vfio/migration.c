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
#include "qemu/units.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "sysemu/runstate.h"
#include "hw/vfio/vfio-common.h"
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

/*
 * This is an arbitrary size based on migration of mlx5 devices, where typically
 * total device migration size is on the order of 100s of MB. Testing with
 * larger values, e.g. 128MB and 1GB, did not show a performance improvement.
 */
#define VFIO_MIG_DEFAULT_DATA_BUFFER_SIZE (1 * MiB)

static int64_t bytes_transferred;

static const char *mig_state_to_str(enum vfio_device_mig_state state)
{
    switch (state) {
    case VFIO_DEVICE_STATE_ERROR:
        return "ERROR";
    case VFIO_DEVICE_STATE_STOP:
        return "STOP";
    case VFIO_DEVICE_STATE_RUNNING:
        return "RUNNING";
    case VFIO_DEVICE_STATE_STOP_COPY:
        return "STOP_COPY";
    case VFIO_DEVICE_STATE_RESUMING:
        return "RESUMING";
    default:
        return "UNKNOWN STATE";
    }
}

static int vfio_migration_set_state(VFIODevice *vbasedev,
                                    enum vfio_device_mig_state new_state,
                                    enum vfio_device_mig_state recover_state)
{
    VFIOMigration *migration = vbasedev->migration;
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature) +
                              sizeof(struct vfio_device_feature_mig_state),
                              sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
    struct vfio_device_feature_mig_state *mig_state =
        (struct vfio_device_feature_mig_state *)feature->data;
    int ret;

    feature->argsz = sizeof(buf);
    feature->flags =
        VFIO_DEVICE_FEATURE_SET | VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE;
    mig_state->device_state = new_state;
    if (ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature)) {
        /* Try to set the device in some good state */
        ret = -errno;

        if (recover_state == VFIO_DEVICE_STATE_ERROR) {
            error_report("%s: Failed setting device state to %s, err: %s. "
                         "Recover state is ERROR. Resetting device",
                         vbasedev->name, mig_state_to_str(new_state),
                         strerror(errno));

            goto reset_device;
        }

        error_report(
            "%s: Failed setting device state to %s, err: %s. Setting device in recover state %s",
                     vbasedev->name, mig_state_to_str(new_state),
                     strerror(errno), mig_state_to_str(recover_state));

        mig_state->device_state = recover_state;
        if (ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature)) {
            ret = -errno;
            error_report(
                "%s: Failed setting device in recover state, err: %s. Resetting device",
                         vbasedev->name, strerror(errno));

            goto reset_device;
        }

        migration->device_state = recover_state;

        return ret;
    }

    migration->device_state = new_state;
    if (mig_state->data_fd != -1) {
        if (migration->data_fd != -1) {
            /*
             * This can happen if the device is asynchronously reset and
             * terminates a data transfer.
             */
            error_report("%s: data_fd out of sync", vbasedev->name);
            close(mig_state->data_fd);

            return -EBADF;
        }

        migration->data_fd = mig_state->data_fd;
    }

    trace_vfio_migration_set_state(vbasedev->name, mig_state_to_str(new_state));

    return 0;

reset_device:
    if (ioctl(vbasedev->fd, VFIO_DEVICE_RESET)) {
        hw_error("%s: Failed resetting device, err: %s", vbasedev->name,
                 strerror(errno));
    }

    migration->device_state = VFIO_DEVICE_STATE_RUNNING;

    return ret;
}

static int vfio_load_buffer(QEMUFile *f, VFIODevice *vbasedev,
                            uint64_t data_size)
{
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    ret = qemu_file_get_to_fd(f, migration->data_fd, data_size);
    trace_vfio_load_state_device_data(vbasedev->name, data_size, ret);

    return ret;
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

    close(migration->data_fd);
    migration->data_fd = -1;
}

static int vfio_query_stop_copy_size(VFIODevice *vbasedev,
                                     uint64_t *stop_copy_size)
{
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature) +
                              sizeof(struct vfio_device_feature_mig_data_size),
                              sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
    struct vfio_device_feature_mig_data_size *mig_data_size =
        (struct vfio_device_feature_mig_data_size *)feature->data;

    feature->argsz = sizeof(buf);
    feature->flags =
        VFIO_DEVICE_FEATURE_GET | VFIO_DEVICE_FEATURE_MIG_DATA_SIZE;

    if (ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature)) {
        return -errno;
    }

    *stop_copy_size = mig_data_size->stop_copy_length;

    return 0;
}

/* Returns 1 if end-of-stream is reached, 0 if more data and -errno if error */
static int vfio_save_block(QEMUFile *f, VFIOMigration *migration)
{
    ssize_t data_size;

    data_size = read(migration->data_fd, migration->data_buffer,
                     migration->data_buffer_size);
    if (data_size < 0) {
        return -errno;
    }
    if (data_size == 0) {
        return 1;
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_DATA_STATE);
    qemu_put_be64(f, data_size);
    qemu_put_buffer(f, migration->data_buffer, data_size);
    bytes_transferred += data_size;

    trace_vfio_save_block(migration->vbasedev->name, data_size);

    return qemu_file_get_error(f);
}

/* ---------------------------------------------------------------------- */

static int vfio_save_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint64_t stop_copy_size = VFIO_MIG_DEFAULT_DATA_BUFFER_SIZE;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_SETUP_STATE);

    vfio_query_stop_copy_size(vbasedev, &stop_copy_size);
    migration->data_buffer_size = MIN(VFIO_MIG_DEFAULT_DATA_BUFFER_SIZE,
                                      stop_copy_size);
    migration->data_buffer = g_try_malloc0(migration->data_buffer_size);
    if (!migration->data_buffer) {
        error_report("%s: Failed to allocate migration data buffer",
                     vbasedev->name);
        return -ENOMEM;
    }

    trace_vfio_save_setup(vbasedev->name, migration->data_buffer_size);

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    return qemu_file_get_error(f);
}

static void vfio_save_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;

    g_free(migration->data_buffer);
    migration->data_buffer = NULL;
    vfio_migration_cleanup(vbasedev);
    trace_vfio_save_cleanup(vbasedev->name);
}

/*
 * Migration size of VFIO devices can be as little as a few KBs or as big as
 * many GBs. This value should be big enough to cover the worst case.
 */
#define VFIO_MIG_STOP_COPY_SIZE (100 * GiB)

/*
 * Only exact function is implemented and not estimate function. The reason is
 * that during pre-copy phase of migration the estimate function is called
 * repeatedly while pending RAM size is over the threshold, thus migration
 * can't converge and querying the VFIO device pending data size is useless.
 */
static void vfio_state_pending_exact(void *opaque, uint64_t *must_precopy,
                                     uint64_t *can_postcopy)
{
    VFIODevice *vbasedev = opaque;
    uint64_t stop_copy_size = VFIO_MIG_STOP_COPY_SIZE;

    /*
     * If getting pending migration size fails, VFIO_MIG_STOP_COPY_SIZE is
     * reported so downtime limit won't be violated.
     */
    vfio_query_stop_copy_size(vbasedev, &stop_copy_size);
    *must_precopy += stop_copy_size;

    trace_vfio_state_pending_exact(vbasedev->name, *must_precopy, *can_postcopy,
                                   stop_copy_size);
}

static int vfio_save_complete_precopy(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    int ret;

    /* We reach here with device state STOP only */
    ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_STOP_COPY,
                                   VFIO_DEVICE_STATE_STOP);
    if (ret) {
        return ret;
    }

    do {
        ret = vfio_save_block(f, vbasedev->migration);
        if (ret < 0) {
            return ret;
        }
    } while (!ret);

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);
    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    /*
     * If setting the device in STOP state fails, the device should be reset.
     * To do so, use ERROR state as a recover state.
     */
    ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_STOP,
                                   VFIO_DEVICE_STATE_ERROR);
    trace_vfio_save_complete_precopy(vbasedev->name, ret);

    return ret;
}

static void vfio_save_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    int ret;

    ret = vfio_save_device_config_state(f, opaque);
    if (ret) {
        error_report("%s: Failed to save device config space",
                     vbasedev->name);
        qemu_file_set_error(f, ret);
    }
}

static int vfio_load_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;

    return vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_RESUMING,
                                   vbasedev->migration->device_state);
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
            return vfio_load_device_config_state(f, opaque);
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

static const SaveVMHandlers savevm_vfio_handlers = {
    .save_setup = vfio_save_setup,
    .save_cleanup = vfio_save_cleanup,
    .state_pending_exact = vfio_state_pending_exact,
    .save_live_complete_precopy = vfio_save_complete_precopy,
    .save_state = vfio_save_state,
    .load_setup = vfio_load_setup,
    .load_cleanup = vfio_load_cleanup,
    .load_state = vfio_load_state,
};

/* ---------------------------------------------------------------------- */

static void vfio_vmstate_change(void *opaque, bool running, RunState state)
{
    VFIODevice *vbasedev = opaque;
    enum vfio_device_mig_state new_state;
    int ret;

    if (running) {
        new_state = VFIO_DEVICE_STATE_RUNNING;
    } else {
        new_state = VFIO_DEVICE_STATE_STOP;
    }

    /*
     * If setting the device in new_state fails, the device should be reset.
     * To do so, use ERROR state as a recover state.
     */
    ret = vfio_migration_set_state(vbasedev, new_state,
                                   VFIO_DEVICE_STATE_ERROR);
    if (ret) {
        /*
         * Migration should be aborted in this case, but vm_state_notify()
         * currently does not support reporting failures.
         */
        if (migrate_get_current()->to_dst_file) {
            qemu_file_set_error(migrate_get_current()->to_dst_file, ret);
        }
    }

    trace_vfio_vmstate_change(vbasedev->name, running, RunState_str(state),
                              mig_state_to_str(new_state));
}

static void vfio_migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;
    VFIOMigration *migration = container_of(notifier, VFIOMigration,
                                            migration_state);
    VFIODevice *vbasedev = migration->vbasedev;

    trace_vfio_migration_state_notifier(vbasedev->name,
                                        MigrationStatus_str(s->state));

    switch (s->state) {
    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_FAILED:
        bytes_transferred = 0;
        /*
         * If setting the device in RUNNING state fails, the device should
         * be reset. To do so, use ERROR state as a recover state.
         */
        vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_RUNNING,
                                 VFIO_DEVICE_STATE_ERROR);
    }
}

static void vfio_migration_exit(VFIODevice *vbasedev)
{
    g_free(vbasedev->migration);
    vbasedev->migration = NULL;
}

static int vfio_migration_query_flags(VFIODevice *vbasedev, uint64_t *mig_flags)
{
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature) +
                                  sizeof(struct vfio_device_feature_migration),
                              sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
    struct vfio_device_feature_migration *mig =
        (struct vfio_device_feature_migration *)feature->data;

    feature->argsz = sizeof(buf);
    feature->flags = VFIO_DEVICE_FEATURE_GET | VFIO_DEVICE_FEATURE_MIGRATION;
    if (ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature)) {
        if (errno == ENOTTY) {
            error_report("%s: VFIO migration is not supported in kernel",
                         vbasedev->name);
        } else {
            error_report("%s: Failed to query VFIO migration support, err: %s",
                         vbasedev->name, strerror(errno));
        }

        return -errno;
    }

    *mig_flags = mig->flags;

    return 0;
}

static int vfio_migration_init(VFIODevice *vbasedev)
{
    int ret;
    Object *obj;
    VFIOMigration *migration;
    char id[256] = "";
    g_autofree char *path = NULL, *oid = NULL;
    uint64_t mig_flags = 0;

    if (!vbasedev->ops->vfio_get_object) {
        return -EINVAL;
    }

    obj = vbasedev->ops->vfio_get_object(vbasedev);
    if (!obj) {
        return -EINVAL;
    }

    ret = vfio_migration_query_flags(vbasedev, &mig_flags);
    if (ret) {
        return ret;
    }

    /* Basic migration functionality must be supported */
    if (!(mig_flags & VFIO_MIGRATION_STOP_COPY)) {
        return -EOPNOTSUPP;
    }

    vbasedev->migration = g_new0(VFIOMigration, 1);
    migration = vbasedev->migration;
    migration->vbasedev = vbasedev;
    migration->device_state = VFIO_DEVICE_STATE_RUNNING;
    migration->data_fd = -1;

    oid = vmstate_if_get_id(VMSTATE_IF(DEVICE(obj)));
    if (oid) {
        path = g_strdup_printf("%s/vfio", oid);
    } else {
        path = g_strdup("vfio");
    }
    strpadcpy(id, sizeof(id), path, '\0');

    register_savevm_live(id, VMSTATE_INSTANCE_ID_ANY, 1, &savevm_vfio_handlers,
                         vbasedev);

    migration->vm_state = qdev_add_vm_change_state_handler(vbasedev->dev,
                                                           vfio_vmstate_change,
                                                           vbasedev);
    migration->migration_state.notify = vfio_migration_state_notifier;
    add_migration_state_change_notifier(&migration->migration_state);

    return 0;
}

/* ---------------------------------------------------------------------- */

int64_t vfio_mig_bytes_transferred(void)
{
    return bytes_transferred;
}

int vfio_migration_probe(VFIODevice *vbasedev, Error **errp)
{
    int ret = -ENOTSUP;

    if (!vbasedev->enable_migration) {
        goto add_blocker;
    }

    ret = vfio_migration_init(vbasedev);
    if (ret) {
        goto add_blocker;
    }

    ret = vfio_block_multiple_devices_migration(errp);
    if (ret) {
        return ret;
    }

    trace_vfio_migration_probe(vbasedev->name);
    return 0;

add_blocker:
    error_setg(&vbasedev->migration_blocker,
               "VFIO device doesn't support migration");

    ret = migrate_add_blocker(vbasedev->migration_blocker, errp);
    if (ret < 0) {
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
        unregister_savevm(VMSTATE_IF(vbasedev->dev), "vfio", vbasedev);
        vfio_migration_exit(vbasedev);
        vfio_unblock_multiple_devices_migration();
    }

    if (vbasedev->migration_blocker) {
        migrate_del_blocker(vbasedev->migration_blocker);
        error_free(vbasedev->migration_blocker);
        vbasedev->migration_blocker = NULL;
    }
}
