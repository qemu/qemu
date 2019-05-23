/*
 * filter-replay.c
 *
 * Copyright (c) 2010-2016 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "clients.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/visitor.h"
#include "net/filter.h"
#include "sysemu/replay.h"

#define TYPE_FILTER_REPLAY "filter-replay"

#define FILTER_REPLAY(obj) \
    OBJECT_CHECK(NetFilterReplayState, (obj), TYPE_FILTER_REPLAY)

struct NetFilterReplayState {
    NetFilterState nfs;
    ReplayNetState *rns;
};
typedef struct NetFilterReplayState NetFilterReplayState;

static ssize_t filter_replay_receive_iov(NetFilterState *nf,
                                         NetClientState *sndr,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt, NetPacketSent *sent_cb)
{
    NetFilterReplayState *nfrs = FILTER_REPLAY(nf);
    switch (replay_mode) {
    case REPLAY_MODE_RECORD:
        if (nf->netdev == sndr) {
            replay_net_packet_event(nfrs->rns, flags, iov, iovcnt);
            return iov_size(iov, iovcnt);
        }
        return 0;
    case REPLAY_MODE_PLAY:
        /* Drop all packets in replay mode.
           Packets from the log will be injected by the replay module. */
        return iov_size(iov, iovcnt);
    default:
        /* Pass all the packets. */
        return 0;
    }
}

static void filter_replay_instance_init(Object *obj)
{
    NetFilterReplayState *nfrs = FILTER_REPLAY(obj);
    nfrs->rns = replay_register_net(&nfrs->nfs);
}

static void filter_replay_instance_finalize(Object *obj)
{
    NetFilterReplayState *nfrs = FILTER_REPLAY(obj);
    replay_unregister_net(nfrs->rns);
}

static void filter_replay_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    nfc->receive_iov = filter_replay_receive_iov;
}

static const TypeInfo filter_replay_info = {
    .name = TYPE_FILTER_REPLAY,
    .parent = TYPE_NETFILTER,
    .class_init = filter_replay_class_init,
    .instance_init = filter_replay_instance_init,
    .instance_finalize = filter_replay_instance_finalize,
    .instance_size = sizeof(NetFilterReplayState),
};

static void filter_replay_register_types(void)
{
    type_register_static(&filter_replay_info);
}

type_init(filter_replay_register_types);
