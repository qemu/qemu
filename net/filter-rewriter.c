/*
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "net/colo.h"
#include "net/filter.h"
#include "net/net.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qapi-visit.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/iov.h"
#include "net/checksum.h"

#define FILTER_COLO_REWRITER(obj) \
    OBJECT_CHECK(RewriterState, (obj), TYPE_FILTER_REWRITER)

#define TYPE_FILTER_REWRITER "filter-rewriter"

typedef struct RewriterState {
    NetFilterState parent_obj;
    NetQueue *incoming_queue;
    /* hashtable to save connection */
    GHashTable *connection_track_table;
} RewriterState;

static void filter_rewriter_flush(NetFilterState *nf)
{
    RewriterState *s = FILTER_COLO_REWRITER(nf);

    if (!qemu_net_queue_flush(s->incoming_queue)) {
        /* Unable to empty the queue, purge remaining packets */
        qemu_net_queue_purge(s->incoming_queue, nf->netdev);
    }
}

static ssize_t colo_rewriter_receive_iov(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    /*
     * if we get tcp packet
     * we will rewrite it to make secondary guest's
     * connection established successfully
     */
    return 0;
}

static void colo_rewriter_cleanup(NetFilterState *nf)
{
    RewriterState *s = FILTER_COLO_REWRITER(nf);

    /* flush packets */
    if (s->incoming_queue) {
        filter_rewriter_flush(nf);
        g_free(s->incoming_queue);
    }
}

static void colo_rewriter_setup(NetFilterState *nf, Error **errp)
{
    RewriterState *s = FILTER_COLO_REWRITER(nf);

    s->connection_track_table = g_hash_table_new_full(connection_key_hash,
                                                      connection_key_equal,
                                                      g_free,
                                                      connection_destroy);
    s->incoming_queue = qemu_new_net_queue(qemu_netfilter_pass_to_next, nf);
}

static void colo_rewriter_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    nfc->setup = colo_rewriter_setup;
    nfc->cleanup = colo_rewriter_cleanup;
    nfc->receive_iov = colo_rewriter_receive_iov;
}

static const TypeInfo colo_rewriter_info = {
    .name = TYPE_FILTER_REWRITER,
    .parent = TYPE_NETFILTER,
    .class_init = colo_rewriter_class_init,
    .instance_size = sizeof(RewriterState),
};

static void register_types(void)
{
    type_register_static(&colo_rewriter_info);
}

type_init(register_types);
