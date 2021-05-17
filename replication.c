/*
 * Replication filter
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2016 FUJITSU LIMITED
 *
 * Author:
 *   Changlong Xie <xiecl.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/replication.h"

static QLIST_HEAD(, ReplicationState) replication_states;

ReplicationState *replication_new(void *opaque, ReplicationOps *ops)
{
    ReplicationState *rs;

    assert(ops != NULL);
    rs = g_new0(ReplicationState, 1);
    rs->opaque = opaque;
    rs->ops = ops;
    QLIST_INSERT_HEAD(&replication_states, rs, node);

    return rs;
}

void replication_remove(ReplicationState *rs)
{
    if (rs) {
        QLIST_REMOVE(rs, node);
        g_free(rs);
    }
}

/*
 * The caller of the function MUST make sure vm stopped
 */
void replication_start_all(ReplicationMode mode, Error **errp)
{
    ReplicationState *rs, *next;
    Error *local_err = NULL;

    QLIST_FOREACH_SAFE(rs, &replication_states, node, next) {
        if (rs->ops && rs->ops->start) {
            rs->ops->start(rs, mode, &local_err);
        }
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

void replication_do_checkpoint_all(Error **errp)
{
    ReplicationState *rs, *next;
    Error *local_err = NULL;

    QLIST_FOREACH_SAFE(rs, &replication_states, node, next) {
        if (rs->ops && rs->ops->checkpoint) {
            rs->ops->checkpoint(rs, &local_err);
        }
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

void replication_get_error_all(Error **errp)
{
    ReplicationState *rs, *next;
    Error *local_err = NULL;

    QLIST_FOREACH_SAFE(rs, &replication_states, node, next) {
        if (rs->ops && rs->ops->get_error) {
            rs->ops->get_error(rs, &local_err);
        }
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

void replication_stop_all(bool failover, Error **errp)
{
    ReplicationState *rs, *next;
    Error *local_err = NULL;

    QLIST_FOREACH_SAFE(rs, &replication_states, node, next) {
        if (rs->ops && rs->ops->stop) {
            rs->ops->stop(rs, failover, &local_err);
        }
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}
