/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-types-machine.h"

HvBalloonInfo *qmp_query_hv_balloon_status_report(Error **errp)
{
    error_setg(errp, "hv-balloon device not enabled in this build");
    return NULL;
}
