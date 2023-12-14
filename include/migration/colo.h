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

#include "qapi/qapi-types-migration.h"

enum colo_event {
    COLO_EVENT_NONE,
    COLO_EVENT_CHECKPOINT,
    COLO_EVENT_FAILOVER,
};

void migrate_start_colo_process(MigrationState *s);
bool migration_in_colo_state(void);

/* loadvm */
int migration_incoming_enable_colo(void);
void migration_incoming_disable_colo(void);
bool migration_incoming_colo_enabled(void);
bool migration_incoming_in_colo_state(void);

COLOMode get_colo_mode(void);

/* failover */
void colo_do_failover(void);

/*
 * colo_checkpoint_delay_set
 *
 * Handles change of x-checkpoint-delay migration parameter, called from
 * migrate_params_apply() to notify COLO module about the change.
 */
void colo_checkpoint_delay_set(void);

/*
 * Starts COLO incoming process. Called from process_incoming_migration_co()
 * after loading the state.
 *
 * Called with BQL locked, may temporary release BQL.
 */
int coroutine_fn colo_incoming_co(void);

void colo_shutdown(void);
#endif
