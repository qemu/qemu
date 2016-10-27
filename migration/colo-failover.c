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
#include "qmp-commands.h"
#include "qapi/qmp/qerror.h"

static QEMUBH *failover_bh;

static void colo_failover_bh(void *opaque)
{
    qemu_bh_delete(failover_bh);
    failover_bh = NULL;
    /* TODO: Do failover work */
}

void failover_request_active(Error **errp)
{
    failover_bh = qemu_bh_new(colo_failover_bh, NULL);
    qemu_bh_schedule(failover_bh);
}

void qmp_x_colo_lost_heartbeat(Error **errp)
{
    if (get_colo_mode() == COLO_MODE_UNKNOWN) {
        error_setg(errp, QERR_FEATURE_DISABLED, "colo");
        return;
    }

    failover_request_active(errp);
}
