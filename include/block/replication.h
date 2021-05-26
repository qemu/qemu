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

#ifndef REPLICATION_H
#define REPLICATION_H

#include "qapi/qapi-types-block-core.h"
#include "qemu/module.h"
#include "qemu/queue.h"

typedef struct ReplicationOps ReplicationOps;
typedef struct ReplicationState ReplicationState;

/**
 * SECTION:block/replication.h
 * @title:Base Replication System
 * @short_description: interfaces for handling replication
 *
 * The Replication Model provides a framework for handling Replication
 *
 * <example>
 *   <title>How to use replication interfaces</title>
 *   <programlisting>
 * #include "block/replication.h"
 *
 * typedef struct BDRVReplicationState {
 *     ReplicationState *rs;
 * } BDRVReplicationState;
 *
 * static void replication_start(ReplicationState *rs, ReplicationMode mode,
 *                               Error **errp);
 * static void replication_do_checkpoint(ReplicationState *rs, Error **errp);
 * static void replication_get_error(ReplicationState *rs, Error **errp);
 * static void replication_stop(ReplicationState *rs, bool failover,
 *                              Error **errp);
 *
 * static ReplicationOps replication_ops = {
 *     .start = replication_start,
 *     .checkpoint = replication_do_checkpoint,
 *     .get_error = replication_get_error,
 *     .stop = replication_stop,
 * }
 *
 * static int replication_open(BlockDriverState *bs, QDict *options,
 *                             int flags, Error **errp)
 * {
 *     BDRVReplicationState *s = bs->opaque;
 *     s->rs = replication_new(bs, &replication_ops);
 *     return 0;
 * }
 *
 * static void replication_close(BlockDriverState *bs)
 * {
 *     BDRVReplicationState *s = bs->opaque;
 *     replication_remove(s->rs);
 * }
 *
 * BlockDriver bdrv_replication = {
 *     .format_name                = "replication",
 *     .instance_size              = sizeof(BDRVReplicationState),
 *
 *     .bdrv_open                  = replication_open,
 *     .bdrv_close                 = replication_close,
 * };
 *
 * static void bdrv_replication_init(void)
 * {
 *     bdrv_register(&bdrv_replication);
 * }
 *
 * block_init(bdrv_replication_init);
 *   </programlisting>
 * </example>
 *
 * We create an example about how to use replication interfaces in above.
 * Then in migration, we can use replication_(start/stop/do_checkpoint/
 * get_error)_all to handle all replication operations.
 */

/**
 * ReplicationState:
 * @opaque: opaque pointer value passed to this ReplicationState
 * @ops: replication operation of this ReplicationState
 * @node: node that we will insert into @replication_states QLIST
 */
struct ReplicationState {
    void *opaque;
    ReplicationOps *ops;
    QLIST_ENTRY(ReplicationState) node;
};

/**
 * ReplicationOps:
 * @start: callback to start replication
 * @stop: callback to stop replication
 * @checkpoint: callback to do checkpoint
 * @get_error: callback to check if error occurred during replication
 */
struct ReplicationOps {
    void (*start)(ReplicationState *rs, ReplicationMode mode, Error **errp);
    void (*stop)(ReplicationState *rs, bool failover, Error **errp);
    void (*checkpoint)(ReplicationState *rs, Error **errp);
    void (*get_error)(ReplicationState *rs, Error **errp);
};

/**
 * replication_new:
 * @opaque: opaque pointer value passed to ReplicationState
 * @ops: replication operation of the new relevant ReplicationState
 *
 * Called to create a new ReplicationState instance, and then insert it
 * into @replication_states QLIST
 *
 * Returns: the new ReplicationState instance
 */
ReplicationState *replication_new(void *opaque, ReplicationOps *ops);

/**
 * replication_remove:
 * @rs: the ReplicationState instance to remove
 *
 * Called to remove a ReplicationState instance, and then delete it from
 * @replication_states QLIST
 */
void replication_remove(ReplicationState *rs);

/**
 * replication_start_all:
 * @mode: replication mode that could be "primary" or "secondary"
 * @errp: returns an error if this function fails
 *
 * Start replication, called in migration/checkpoint thread
 *
 * Note: the caller of the function MUST make sure vm stopped
 */
void replication_start_all(ReplicationMode mode, Error **errp);

/**
 * replication_do_checkpoint_all:
 * @errp: returns an error if this function fails
 *
 * This interface is called after all VM state is transferred to Secondary QEMU
 */
void replication_do_checkpoint_all(Error **errp);

/**
 * replication_get_error_all:
 * @errp: returns an error if this function fails
 *
 * This interface is called to check if error occurred during replication
 */
void replication_get_error_all(Error **errp);

/**
 * replication_stop_all:
 * @failover: boolean value that indicates if we need do failover or not
 * @errp: returns an error if this function fails
 *
 * It is called on failover. The vm should be stopped before calling it, if you
 * use this API to shutdown the guest, or other things except failover
 */
void replication_stop_all(bool failover, Error **errp);

#endif /* REPLICATION_H */
