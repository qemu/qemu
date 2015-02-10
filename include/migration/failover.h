/*
 *  COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 *  (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO.,LTD.
 * Copyright (c) 2015 FUJITSU LIMITED
 * Copyright (c) 2015 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_FAILOVER_H
#define QEMU_FAILOVER_H

#include "qemu-common.h"

typedef enum COLOFailoverStatus {
    FAILOVER_STATUS_NONE = 0,
    FAILOVER_STATUS_REQUEST = 1, /* Request but not handled */
    FAILOVER_STATUS_HANDLING = 2, /* In the process of handling failover */
    FAILOVER_STATUS_COMPLETED = 3, /* Finish the failover process */
    /* Optional, Relaunch the failover process, again 'NONE' -> 'COMPLETED' */
    FAILOVER_STATUS_RELAUNCH = 4,
} COLOFailoverStatus;

void failover_init_state(void);
int failover_set_state(int old_state, int new_state);
int failover_get_state(void);
void failover_request_active(Error **errp);
bool failover_request_is_active(void);

#endif
