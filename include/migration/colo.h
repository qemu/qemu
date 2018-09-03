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

#ifndef QEMU_COLO_H
#define QEMU_COLO_H

#include "qemu-common.h"
#include "qapi/qapi-types-migration.h"

enum colo_event {
    COLO_EVENT_NONE,
    COLO_EVENT_CHECKPOINT,
    COLO_EVENT_FAILOVER,
};

void colo_info_init(void);

void migrate_start_colo_process(MigrationState *s);
bool migration_in_colo_state(void);

/* loadvm */
void migration_incoming_enable_colo(void);
void migration_incoming_disable_colo(void);
bool migration_incoming_colo_enabled(void);
void *colo_process_incoming_thread(void *opaque);
bool migration_incoming_in_colo_state(void);

COLOMode get_colo_mode(void);

/* failover */
void colo_do_failover(MigrationState *s);

void colo_checkpoint_notify(void *opaque);
#endif
