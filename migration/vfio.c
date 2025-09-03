/*
 * QEMU live migration - VFIO
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/qapi-types-migration.h"
#include "migration.h"
#include "hw/vfio/vfio-migration.h"

void migration_populate_vfio_info(MigrationInfo *info)
{
    if (vfio_migration_active()) {
        info->vfio = g_malloc0(sizeof(*info->vfio));
        info->vfio->transferred = vfio_migration_bytes_transferred();
    }
}

void migration_reset_vfio_bytes_transferred(void)
{
    vfio_migration_reset_bytes_transferred();
}
