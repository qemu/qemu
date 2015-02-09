/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2015 FUJITSU LIMITED
 * Copyright (c) 2015 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "migration/colo.h"

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
