/*
 * Memory manager for remote device
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "hw/remote/memory.h"
#include "system/ram_addr.h"
#include "qapi/error.h"

static void remote_sysmem_reset(void)
{
    MemoryRegion *sysmem, *subregion, *next;

    sysmem = get_system_memory();

    QTAILQ_FOREACH_SAFE(subregion, &sysmem->subregions, subregions_link, next) {
        if (subregion->ram) {
            memory_region_del_subregion(sysmem, subregion);
            object_unparent(OBJECT(subregion));
        }
    }
}

void remote_sysmem_reconfig(MPQemuMsg *msg, Error **errp)
{
    ERRP_GUARD();
    SyncSysmemMsg *sysmem_info = &msg->data.sync_sysmem;
    MemoryRegion *sysmem, *subregion;
    static unsigned int suffix;
    int region;

    sysmem = get_system_memory();

    remote_sysmem_reset();

    for (region = 0; region < msg->num_fds; region++, suffix++) {
        g_autofree char *name = g_strdup_printf("remote-mem-%u", suffix);
        subregion = g_new(MemoryRegion, 1);
        memory_region_init_ram_from_fd(subregion, NULL,
                                       name, sysmem_info->sizes[region],
                                       RAM_SHARED, msg->fds[region],
                                       sysmem_info->offsets[region],
                                       errp);

        if (*errp) {
            g_free(subregion);
            remote_sysmem_reset();
            return;
        }

        memory_region_add_subregion(sysmem, sysmem_info->gpas[region],
                                    subregion);

    }
}
