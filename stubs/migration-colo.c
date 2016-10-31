/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/colo.h"
#include "qmp-commands.h"

bool colo_supported(void)
{
    return false;
}

bool migration_in_colo_state(void)
{
    return false;
}

bool migration_incoming_in_colo_state(void)
{
    return false;
}

void migrate_start_colo_process(MigrationState *s)
{
}

void *colo_process_incoming_thread(void *opaque)
{
    return NULL;
}

void qmp_x_colo_lost_heartbeat(Error **errp)
{
    error_setg(errp, "COLO is not supported, please rerun configure"
                     " with --enable-colo option in order to support"
                     " COLO feature");
}
