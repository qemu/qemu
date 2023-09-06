/*
 * QEMU live migration - functions that need to be compiled target-specific
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/qapi-types-migration.h"
#include "migration.h"
#include CONFIG_DEVICES

#ifdef CONFIG_VFIO
#include "hw/vfio/vfio-common.h"
#endif

#ifdef CONFIG_VFIO
void migration_populate_vfio_info(MigrationInfo *info)
{
    if (vfio_mig_active()) {
        info->vfio = g_malloc0(sizeof(*info->vfio));
        info->vfio->transferred = vfio_mig_bytes_transferred();
    }
}

void migration_reset_vfio_bytes_transferred(void)
{
    vfio_reset_bytes_transferred();
}
#else
void migration_populate_vfio_info(MigrationInfo *info)
{
}

void migration_reset_vfio_bytes_transferred(void)
{
}
#endif
