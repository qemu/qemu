/*
 * Copyright (c) 2015 FUJITSU LIMITED
 * Author: Yang Hongyang <yanghy@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"

#include "net/filter.h"
#include "net/net.h"
#include "net/vhost_net.h"
#include "qom/object_interfaces.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "net/colo.h"
#include "migration/colo.h"

static inline bool qemu_can_skip_netfilter(NetFilterState *nf)
{
    return !nf->on;
}

ssize_t qemu_netfilter_receive(NetFilterState *nf,
                               NetFilterDirection direction,
                               NetClientState *sender,
                               unsigned flags,
                               const struct iovec *iov,
                               int iovcnt,
                               NetPacketSent *sent_cb)
{
    if (qemu_can_skip_netfilter(nf)) {
        return 0;
    }
    if (nf->direction == direction ||
        nf->direction == NET_FILTER_DIRECTION_ALL) {
        return NETFILTER_GET_CLASS(OBJECT(nf))->receive_iov(
                                   nf, sender, flags, iov, iovcnt, sent_cb);
    }

    return 0;
}

static NetFilterState *netfilter_next(NetFilterState *nf,
                                      NetFilterDirection dir)
{
    NetFilterState *next;

    if (dir == NET_FILTER_DIRECTION_TX) {
        /* forward walk through filters */
        next = QTAILQ_NEXT(nf, next);
    } else {
        /* reverse order */
        next = QTAILQ_PREV(nf, next);
    }

    return next;
}

ssize_t qemu_netfilter_pass_to_next(NetClientState *sender,
                                    unsigned flags,
                                    const struct iovec *iov,
                                    int iovcnt,
                                    void *opaque)
{
    int ret = 0;
    int direction;
    NetFilterState *nf = opaque;
    NetFilterState *next = NULL;

    if (!sender || !sender->peer) {
        /* no receiver, or sender been deleted, no need to pass it further */
        goto out;
    }

    if (nf->direction == NET_FILTER_DIRECTION_ALL) {
        if (sender == nf->netdev) {
            /* This packet is sent by netdev itself */
            direction = NET_FILTER_DIRECTION_TX;
        } else {
            direction = NET_FILTER_DIRECTION_RX;
        }
    } else {
        direction = nf->direction;
    }

    next = netfilter_next(nf, direction);
    while (next) {
        /*
         * if qemu_netfilter_pass_to_next been called, means that
         * the packet has been hold by filter and has already retured size
         * to the sender, so sent_cb shouldn't be called later, just
         * pass NULL to next.
         */
        ret = qemu_netfilter_receive(next, direction, sender, flags, iov,
                                     iovcnt, NULL);
        if (ret) {
            return ret;
        }
        next = netfilter_next(next, direction);
    }

    /*
     * We have gone through all filters, pass it to receiver.
     * Do the valid check again incase sender or receiver been
     * deleted while we go through filters.
     */
    if (sender && sender->peer) {
        qemu_net_queue_send_iov(sender->peer->incoming_queue,
                                sender, flags, iov, iovcnt, NULL);
    }

out:
    /* no receiver, or sender been deleted */
    return iov_size(iov, iovcnt);
}

static char *netfilter_get_netdev_id(Object *obj, Error **errp)
{
    NetFilterState *nf = NETFILTER(obj);

    return g_strdup(nf->netdev_id);
}

static void netfilter_set_netdev_id(Object *obj, const char *str, Error **errp)
{
    NetFilterState *nf = NETFILTER(obj);

    nf->netdev_id = g_strdup(str);
}

static int netfilter_get_direction(Object *obj, Error **errp G_GNUC_UNUSED)
{
    NetFilterState *nf = NETFILTER(obj);
    return nf->direction;
}

static void netfilter_set_direction(Object *obj, int direction, Error **errp)
{
    NetFilterState *nf = NETFILTER(obj);
    nf->direction = direction;
}

static char *netfilter_get_status(Object *obj, Error **errp)
{
    NetFilterState *nf = NETFILTER(obj);

    return nf->on ? g_strdup("on") : g_strdup("off");
}

static void netfilter_set_status(Object *obj, const char *str, Error **errp)
{
    NetFilterState *nf = NETFILTER(obj);
    NetFilterClass *nfc = NETFILTER_GET_CLASS(obj);

    if (strcmp(str, "on") && strcmp(str, "off")) {
        error_setg(errp, "Invalid value for netfilter status, "
                         "should be 'on' or 'off'");
        return;
    }
    if (nf->on == !strcmp(str, "on")) {
        return;
    }
    nf->on = !nf->on;
    if (nf->netdev && nfc->status_changed) {
        nfc->status_changed(nf, errp);
    }
}

static void netfilter_init(Object *obj)
{
    NetFilterState *nf = NETFILTER(obj);

    nf->on = true;

    object_property_add_str(obj, "netdev",
                            netfilter_get_netdev_id, netfilter_set_netdev_id,
                            NULL);
    object_property_add_enum(obj, "queue", "NetFilterDirection",
                             &NetFilterDirection_lookup,
                             netfilter_get_direction, netfilter_set_direction,
                             NULL);
    object_property_add_str(obj, "status",
                            netfilter_get_status, netfilter_set_status,
                            NULL);
}

static void netfilter_complete(UserCreatable *uc, Error **errp)
{
    NetFilterState *nf = NETFILTER(uc);
    NetClientState *ncs[MAX_QUEUE_NUM];
    NetFilterClass *nfc = NETFILTER_GET_CLASS(uc);
    int queues;
    Error *local_err = NULL;

    if (!nf->netdev_id) {
        error_setg(errp, "Parameter 'netdev' is required");
        return;
    }

    queues = qemu_find_net_clients_except(nf->netdev_id, ncs,
                                          NET_CLIENT_DRIVER_NIC,
                                          MAX_QUEUE_NUM);
    if (queues < 1) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "netdev",
                   "a network backend id");
        return;
    } else if (queues > 1) {
        error_setg(errp, "multiqueue is not supported");
        return;
    }

    if (get_vhost_net(ncs[0])) {
        error_setg(errp, "Vhost is not supported");
        return;
    }

    nf->netdev = ncs[0];

    if (nfc->setup) {
        nfc->setup(nf, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
    QTAILQ_INSERT_TAIL(&nf->netdev->filters, nf, next);
}

static void netfilter_finalize(Object *obj)
{
    NetFilterState *nf = NETFILTER(obj);
    NetFilterClass *nfc = NETFILTER_GET_CLASS(obj);

    if (nfc->cleanup) {
        nfc->cleanup(nf);
    }

    if (nf->netdev && !QTAILQ_EMPTY(&nf->netdev->filters) &&
        QTAILQ_IN_USE(nf, next)) {
        QTAILQ_REMOVE(&nf->netdev->filters, nf, next);
    }
    g_free(nf->netdev_id);
}

static void default_handle_event(NetFilterState *nf, int event, Error **errp)
{
    switch (event) {
    case COLO_EVENT_CHECKPOINT:
        break;
    case COLO_EVENT_FAILOVER:
        object_property_set_str(OBJECT(nf), "off", "status", errp);
        break;
    default:
        break;
    }
}

static void netfilter_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    ucc->complete = netfilter_complete;
    nfc->handle_event = default_handle_event;
}

static const TypeInfo netfilter_info = {
    .name = TYPE_NETFILTER,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(NetFilterClass),
    .class_init = netfilter_class_init,
    .instance_size = sizeof(NetFilterState),
    .instance_init = netfilter_init,
    .instance_finalize = netfilter_finalize,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&netfilter_info);
}

type_init(register_types);
