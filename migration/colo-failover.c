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
#include "migration/failover.h"
#include "qemu/main-loop.h"
#include "migration.h"
#include "qmp-commands.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "trace.h"

static QEMUBH *failover_bh;
static FailoverStatus failover_state;

static void colo_failover_bh(void *opaque)
{
    int old_state;

    qemu_bh_delete(failover_bh);
    failover_bh = NULL;

    old_state = failover_set_state(FAILOVER_STATUS_REQUIRE,
                                   FAILOVER_STATUS_ACTIVE);
    if (old_state != FAILOVER_STATUS_REQUIRE) {
        error_report("Unknown error for failover, old_state = %s",
                    FailoverStatus_lookup[old_state]);
        return;
    }

    colo_do_failover(NULL);
}

void failover_request_active(Error **errp)
{
   if (failover_set_state(FAILOVER_STATUS_NONE,
        FAILOVER_STATUS_REQUIRE) != FAILOVER_STATUS_NONE) {
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

FailoverStatus failover_set_state(FailoverStatus old_state,
                    FailoverStatus new_state)
{
    FailoverStatus old;

    old = atomic_cmpxchg(&failover_state, old_state, new_state);
    if (old == old_state) {
        trace_colo_failover_set_state(FailoverStatus_lookup[new_state]);
    }
    return old;
}

FailoverStatus failover_get_state(void)
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
