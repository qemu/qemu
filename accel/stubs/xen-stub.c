/*
 * Copyright (C) 2014       Citrix Systems UK Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/xen.h"
#include "qapi/qapi-commands-migration.h"

bool xen_allowed;

void qmp_xen_set_global_dirty_log(bool enable, Error **errp)
{
}
