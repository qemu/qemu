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
#include "migration/migration.h"
#include "qemu/coroutine_int.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"

bool colo_supported(void);
void colo_info_init(void);

void migrate_start_colo_process(MigrationState *s);
bool migration_in_colo_state(void);

/* loadvm */
bool migration_incoming_enable_colo(void);
void migration_incoming_exit_colo(void);
void *colo_process_incoming_thread(void *opaque);
bool migration_incoming_in_colo_state(void);

COLOMode get_colo_mode(void);

/* failover */
void colo_do_failover(MigrationState *s);

void colo_checkpoint_notify(void *opaque);
#endif
