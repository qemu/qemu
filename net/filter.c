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
