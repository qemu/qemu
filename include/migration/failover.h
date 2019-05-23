/*
 *  COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 *  (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO.,LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_FAILOVER_H
#define QEMU_FAILOVER_H

#include "qapi/qapi-types-migration.h"

void failover_init_state(void);
FailoverStatus failover_set_state(FailoverStatus old_state,
                                     FailoverStatus new_state);
FailoverStatus failover_get_state(void);
void failover_request_active(Error **errp);
bool failover_request_is_active(void);

#endif
