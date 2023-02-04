/*
 * Human Monitor Interface commands
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "migration/misc.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "net/net.h"
#include "net/hub.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-commands-net.h"
#include "qapi/qapi-visit-net.h"
#include "qapi/qmp/qdict.h"
#include "qemu/config-file.h"
#include "qemu/help_option.h"
#include "qemu/option.h"

void hmp_info_network(Monitor *mon, const QDict *qdict)
{
    NetClientState *nc, *peer;
    NetClientDriver type;

    net_hub_info(mon);

    QTAILQ_FOREACH(nc, &net_clients, next) {
        peer = nc->peer;
        type = nc->info->type;

        /* Skip if already printed in hub info */
        if (net_hub_id_for_client(nc, NULL) == 0) {
            continue;
        }

        if (!peer || type == NET_CLIENT_DRIVER_NIC) {
            print_net_client(mon, nc);
        } /* else it's a netdev connected to a NIC, printed with the NIC */
        if (peer && type == NET_CLIENT_DRIVER_NIC) {
            monitor_printf(mon, " \\ ");
            print_net_client(mon, peer);
        }
    }
}

void hmp_set_link(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_str(qdict, "name");
    bool up = qdict_get_bool(qdict, "up");
    Error *err = NULL;

    qmp_set_link(name, up, &err);
    hmp_handle_error(mon, err);
}


void hmp_announce_self(Monitor *mon, const QDict *qdict)
{
    const char *interfaces_str = qdict_get_try_str(qdict, "interfaces");
    const char *id = qdict_get_try_str(qdict, "id");
    AnnounceParameters *params = QAPI_CLONE(AnnounceParameters,
                                            migrate_announce_params());

    qapi_free_strList(params->interfaces);
    params->interfaces = hmp_split_at_comma(interfaces_str);
    params->has_interfaces = params->interfaces != NULL;
    params->id = g_strdup(id);
    qmp_announce_self(params, NULL);
    qapi_free_AnnounceParameters(params);
}

void hmp_netdev_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    QemuOpts *opts;
    const char *type = qdict_get_try_str(qdict, "type");

    if (type && is_help_option(type)) {
        show_netdevs();
        return;
    }
    opts = qemu_opts_from_qdict(qemu_find_opts("netdev"), qdict, &err);
    if (err) {
        goto out;
    }

    netdev_add(opts, &err);
    if (err) {
        qemu_opts_del(opts);
    }

out:
    hmp_handle_error(mon, err);
}

void hmp_netdev_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    Error *err = NULL;

    qmp_netdev_del(id, &err);
    hmp_handle_error(mon, err);
}


void netdev_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    int i;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);
    for (i = 0; i < NET_CLIENT_DRIVER__MAX; i++) {
        readline_add_completion_of(rs, str, NetClientDriver_str(i));
    }
}

void set_link_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        NetClientState *ncs[MAX_QUEUE_NUM];
        int count, i;
        count = qemu_find_net_clients_except(NULL, ncs,
                                             NET_CLIENT_DRIVER_NONE,
                                             MAX_QUEUE_NUM);
        for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
            readline_add_completion_of(rs, str, ncs[i]->name);
        }
    } else if (nb_args == 3) {
        readline_add_completion_of(rs, str, "on");
        readline_add_completion_of(rs, str, "off");
    }
}

void netdev_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int len, count, i;
    NetClientState *ncs[MAX_QUEUE_NUM];

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    count = qemu_find_net_clients_except(NULL, ncs, NET_CLIENT_DRIVER_NIC,
                                         MAX_QUEUE_NUM);
    for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
        if (ncs[i]->is_netdev) {
            readline_add_completion_of(rs, str, ncs[i]->name);
        }
    }
}
