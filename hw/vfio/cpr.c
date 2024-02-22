/*
 * Copyright (c) 2021-2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vfio/vfio-common.h"
#include "qapi/error.h"

int vfio_cpr_register_container(VFIOContainerBase *bcontainer, Error **errp)
{
    return 0;
}

void vfio_cpr_unregister_container(VFIOContainerBase *bcontainer)
{
}
