/*
 * replay-net.c
 *
 * Copyright (c) 2010-2016 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "net/filter.h"
#include "qemu/iov.h"

struct ReplayNetState {
    NetFilterState *nfs;
    int id;
};

typedef struct NetEvent {
    uint8_t id;
    uint32_t flags;
    uint8_t *data;
    size_t size;
} NetEvent;

static NetFilterState **network_filters;
static int network_filters_count;

ReplayNetState *replay_register_net(NetFilterState *nfs)
{
    ReplayNetState *rns = g_new0(ReplayNetState, 1);
    rns->nfs = nfs;
    rns->id = network_filters_count++;
    network_filters = g_realloc(network_filters,
                                network_filters_count
                                    * sizeof(*network_filters));
    network_filters[network_filters_count - 1] = nfs;
    return rns;
}

void replay_unregister_net(ReplayNetState *rns)
{
    network_filters[rns->id] = NULL;
    g_free(rns);
}

void replay_net_packet_event(ReplayNetState *rns, unsigned flags,
                             const struct iovec *iov, int iovcnt)
{
    NetEvent *event = g_new(NetEvent, 1);
    event->flags = flags;
    event->data = g_malloc(iov_size(iov, iovcnt));
    event->size = iov_size(iov, iovcnt);
    event->id = rns->id;
    iov_to_buf(iov, iovcnt, 0, event->data, event->size);

    replay_add_event(REPLAY_ASYNC_EVENT_NET, event, NULL, 0);
}

void replay_event_net_run(void *opaque)
{
    NetEvent *event = opaque;
    struct iovec iov = {
        .iov_base = (void *)event->data,
        .iov_len = event->size
    };

    assert(event->id < network_filters_count);

    qemu_netfilter_pass_to_next(network_filters[event->id]->netdev,
        event->flags, &iov, 1, network_filters[event->id]);

    g_free(event->data);
    g_free(event);
}

void replay_event_net_save(void *opaque)
{
    NetEvent *event = opaque;

    replay_put_byte(event->id);
    replay_put_dword(event->flags);
    replay_put_array(event->data, event->size);
}

void *replay_event_net_load(void)
{
    NetEvent *event = g_new(NetEvent, 1);

    event->id = replay_get_byte();
    event->flags = replay_get_dword();
    replay_get_array_alloc(&event->data, &event->size);

    return event;
}
