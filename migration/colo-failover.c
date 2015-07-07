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
#include "migration/failover.h"
#include "qmp-commands.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "trace.h"

static QEMUBH *failover_bh;
static COLOFailoverStatus failover_state;

static void colo_failover_bh(void *opaque)
{
    int old_state;

    qemu_bh_delete(failover_bh);
    failover_bh = NULL;
    old_state = failover_set_state(FAILOVER_STATUS_REQUEST,
                                   FAILOVER_STATUS_HANDLING);
    if (old_state != FAILOVER_STATUS_REQUEST) {
        error_report("Unkown error for failover, old_state=%d", old_state);
        return;
    }
    /*TODO: Do failover work */
}

void failover_request_active(Error **errp)
{
   if (failover_set_state(FAILOVER_STATUS_NONE, FAILOVER_STATUS_REQUEST)
         != FAILOVER_STATUS_NONE) {
        error_setg(errp, "COLO failover is already actived");
        return;
    }
    failover_bh = qemu_bh_new(colo_failover_bh, NULL);
    qemu_bh_schedule(failover_bh);
}

void failover_init_state(void)
{
    failover_state = FAILOVER_STATUS_NONE;
}

int failover_set_state(int old_state, int new_state)
{
    int old;

    old = atomic_cmpxchg(&failover_state, old_state, new_state);
    if (old == old_state) {
        trace_colo_failover_set_state(new_state);
    }
    return old;
}

int failover_get_state(void)
{
    return atomic_read(&failover_state);
}

void qmp_x_colo_lost_heartbeat(Error **errp)
{
    if (get_colo_mode() == COLO_MODE_UNKNOWN) {
        error_setg(errp, QERR_FEATURE_DISABLED, "colo");
        return;
    }

    failover_request_active(errp);
}
