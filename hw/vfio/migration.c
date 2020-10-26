/*
 * Migration support for VFIO devices
 *
 * Copyright NVIDIA, Inc. 2020
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>

#include "hw/vfio/vfio-common.h"
#include "cpu.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "migration/register.h"
#include "migration/blocker.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "exec/ramlist.h"
#include "exec/ram_addr.h"
#include "pci.h"
#include "trace.h"

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
        vfio_migration_exit(vbasedev);
    }

    if (vbasedev->migration_blocker) {
        migrate_del_blocker(vbasedev->migration_blocker);
        error_free(vbasedev->migration_blocker);
        vbasedev->migration_blocker = NULL;
    }
}
