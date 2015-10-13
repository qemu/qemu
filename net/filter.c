/*
 * Copyright (c) 2015 FUJITSU LIMITED
 * Author: Yang Hongyang <yanghy@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"

#include "net/filter.h"
#include "net/net.h"
#include "net/vhost_net.h"
#include "qom/object_interfaces.h"
#include "qemu/iov.h"
#include "qapi/string-output-visitor.h"

ssize_t qemu_netfilter_receive(NetFilterState *nf,
                               NetFilterDirection direction,
                               NetClientState *sender,
                               unsigned flags,
                               const struct iovec *iov,
                               int iovcnt,
                               NetPacketSent *sent_cb)
{
    if (nf->direction == direction ||
        nf->direction == NET_FILTER_DIRECTION_ALL) {
        return NETFILTER_GET_CLASS(OBJECT(nf))->receive_iov(
                                   nf, sender, flags, iov, iovcnt, sent_cb);
    }

    return 0;
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
    NetFilterState *next = QTAILQ_NEXT(nf, next);

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
        next = QTAILQ_NEXT(next, next);
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

static void netfilter_init(Object *obj)
{
    object_property_add_str(obj, "netdev",
                            netfilter_get_netdev_id, netfilter_set_netdev_id,
                            NULL);
    object_property_add_enum(obj, "queue", "NetFilterDirection",
                             NetFilterDirection_lookup,
                             netfilter_get_direction, netfilter_set_direction,
                             NULL);
}

static void netfilter_complete(UserCreatable *uc, Error **errp)
{
    NetFilterState *nf = NETFILTER(uc);
    NetClientState *ncs[MAX_QUEUE_NUM];
    NetFilterClass *nfc = NETFILTER_GET_CLASS(uc);
    int queues;
    Error *local_err = NULL;
    char *str, *info;
    ObjectProperty *prop;
    ObjectPropertyIterator *iter;
    StringOutputVisitor *ov;

    if (!nf->netdev_id) {
        error_setg(errp, "Parameter 'netdev' is required");
        return;
    }

    queues = qemu_find_net_clients_except(nf->netdev_id, ncs,
                                          NET_CLIENT_OPTIONS_KIND_NIC,
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

    /* generate info str */
    iter = object_property_iter_init(OBJECT(nf));
    while ((prop = object_property_iter_next(iter))) {
        if (!strcmp(prop->name, "type")) {
            continue;
        }
        ov = string_output_visitor_new(false);
        object_property_get(OBJECT(nf), string_output_get_visitor(ov),
                            prop->name, errp);
        str = string_output_get_string(ov);
        string_output_visitor_cleanup(ov);
        info = g_strdup_printf(",%s=%s", prop->name, str);
        g_strlcat(nf->info_str, info, sizeof(nf->info_str));
        g_free(str);
        g_free(info);
    }
    object_property_iter_free(iter);
}

static void netfilter_finalize(Object *obj)
{
    NetFilterState *nf = NETFILTER(obj);
    NetFilterClass *nfc = NETFILTER_GET_CLASS(obj);

    if (nfc->cleanup) {
        nfc->cleanup(nf);
    }

    if (nf->netdev && !QTAILQ_EMPTY(&nf->netdev->filters)) {
        QTAILQ_REMOVE(&nf->netdev->filters, nf, next);
    }
}

static void netfilter_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = netfilter_complete;
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
