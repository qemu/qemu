/*
 * Multifd UADK compression accelerator implementation
 *
 * Copyright (c) 2024 Huawei Technologies R & D (UK) Ltd
 *
 * Authors:
 *  Shameer Kolothum <shameerali.kolothum.thodi@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"

static void multifd_uadk_register(void)
{
    /* noop for now */
}
migration_init(multifd_uadk_register);
