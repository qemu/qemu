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
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "net/eth.h"
#include "qapi/qapi-commands-rocker.h"
#include "qobject/qdict.h"

void hmp_rocker(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_str(qdict, "name");
    RockerSwitch *rocker;
    Error *err = NULL;

    rocker = qmp_query_rocker(name, &err);
    if (hmp_handle_error(mon, err)) {
        return;
    }

    monitor_printf(mon, "name: %s\n", rocker->name);
    monitor_printf(mon, "id: 0x%" PRIx64 "\n", rocker->id);
    monitor_printf(mon, "ports: %d\n", rocker->ports);

    qapi_free_RockerSwitch(rocker);
}

void hmp_rocker_ports(Monitor *mon, const QDict *qdict)
{
    RockerPortList *list, *port;
    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    list = qmp_query_rocker_ports(name, &err);
    if (hmp_handle_error(mon, err)) {
        return;
    }

    monitor_printf(mon, "            ena/    speed/ auto\n");
    monitor_printf(mon, "      port  link    duplex neg?\n");

    for (port = list; port; port = port->next) {
        monitor_printf(mon, "%10s  %-4s   %-3s  %2s  %s\n",
                       port->value->name,
                       port->value->enabled ? port->value->link_up ?
                       "up" : "down" : "!ena",
                       port->value->speed == 10000 ? "10G" : "??",
                       port->value->duplex ? "FD" : "HD",
                       port->value->autoneg ? "Yes" : "No");
    }

    qapi_free_RockerPortList(list);
}

void hmp_rocker_of_dpa_flows(Monitor *mon, const QDict *qdict)
{
    RockerOfDpaFlowList *list, *info;
    const char *name = qdict_get_str(qdict, "name");
    uint32_t tbl_id = qdict_get_try_int(qdict, "tbl_id", -1);
    Error *err = NULL;

    list = qmp_query_rocker_of_dpa_flows(name, tbl_id != -1, tbl_id, &err);
    if (hmp_handle_error(mon, err)) {
        return;
    }

    monitor_printf(mon, "prio tbl hits key(mask) --> actions\n");

    for (info = list; info; info = info->next) {
        RockerOfDpaFlow *flow = info->value;
        RockerOfDpaFlowKey *key = flow->key;
        RockerOfDpaFlowMask *mask = flow->mask;
        RockerOfDpaFlowAction *action = flow->action;

        if (flow->hits) {
            monitor_printf(mon, "%-4d %-3d %-4" PRIu64,
                           key->priority, key->tbl_id, flow->hits);
        } else {
            monitor_printf(mon, "%-4d %-3d     ",
                           key->priority, key->tbl_id);
        }

        if (key->has_in_pport) {
            monitor_printf(mon, " pport %d", key->in_pport);
            if (mask->has_in_pport) {
                monitor_printf(mon, "(0x%x)", mask->in_pport);
            }
        }

        if (key->has_vlan_id) {
            monitor_printf(mon, " vlan %d",
                           key->vlan_id & VLAN_VID_MASK);
            if (mask->has_vlan_id) {
                monitor_printf(mon, "(0x%x)", mask->vlan_id);
            }
        }

        if (key->has_tunnel_id) {
            monitor_printf(mon, " tunnel %d", key->tunnel_id);
            if (mask->has_tunnel_id) {
                monitor_printf(mon, "(0x%x)", mask->tunnel_id);
            }
        }

        if (key->has_eth_type) {
            switch (key->eth_type) {
            case 0x0806:
                monitor_printf(mon, " ARP");
                break;
            case 0x0800:
                monitor_printf(mon, " IP");
                break;
            case 0x86dd:
                monitor_printf(mon, " IPv6");
                break;
            case 0x8809:
                monitor_printf(mon, " LACP");
                break;
            case 0x88cc:
                monitor_printf(mon, " LLDP");
                break;
            default:
                monitor_printf(mon, " eth type 0x%04x", key->eth_type);
                break;
            }
        }

        if (key->eth_src) {
            if ((strcmp(key->eth_src, "01:00:00:00:00:00") == 0) &&
                mask->eth_src &&
                (strcmp(mask->eth_src, "01:00:00:00:00:00") == 0)) {
                monitor_printf(mon, " src <any mcast/bcast>");
            } else if ((strcmp(key->eth_src, "00:00:00:00:00:00") == 0) &&
                mask->eth_src &&
                (strcmp(mask->eth_src, "01:00:00:00:00:00") == 0)) {
                monitor_printf(mon, " src <any ucast>");
            } else {
                monitor_printf(mon, " src %s", key->eth_src);
                if (mask->eth_src) {
                    monitor_printf(mon, "(%s)", mask->eth_src);
                }
            }
        }

        if (key->eth_dst) {
            if ((strcmp(key->eth_dst, "01:00:00:00:00:00") == 0) &&
                mask->eth_dst &&
                (strcmp(mask->eth_dst, "01:00:00:00:00:00") == 0)) {
                monitor_printf(mon, " dst <any mcast/bcast>");
            } else if ((strcmp(key->eth_dst, "00:00:00:00:00:00") == 0) &&
                mask->eth_dst &&
                (strcmp(mask->eth_dst, "01:00:00:00:00:00") == 0)) {
                monitor_printf(mon, " dst <any ucast>");
            } else {
                monitor_printf(mon, " dst %s", key->eth_dst);
                if (mask->eth_dst) {
                    monitor_printf(mon, "(%s)", mask->eth_dst);
                }
            }
        }

        if (key->has_ip_proto) {
            monitor_printf(mon, " proto %d", key->ip_proto);
            if (mask->has_ip_proto) {
                monitor_printf(mon, "(0x%x)", mask->ip_proto);
            }
        }

        if (key->has_ip_tos) {
            monitor_printf(mon, " TOS %d", key->ip_tos);
            if (mask->has_ip_tos) {
                monitor_printf(mon, "(0x%x)", mask->ip_tos);
            }
        }

        if (key->ip_dst) {
            monitor_printf(mon, " dst %s", key->ip_dst);
        }

        if (action->has_goto_tbl || action->has_group_id ||
            action->has_new_vlan_id) {
            monitor_printf(mon, " -->");
        }

        if (action->has_new_vlan_id) {
            monitor_printf(mon, " apply new vlan %d",
                           ntohs(action->new_vlan_id));
        }

        if (action->has_group_id) {
            monitor_printf(mon, " write group 0x%08x", action->group_id);
        }

        if (action->has_goto_tbl) {
            monitor_printf(mon, " goto tbl %d", action->goto_tbl);
        }

        monitor_printf(mon, "\n");
    }

    qapi_free_RockerOfDpaFlowList(list);
}

void hmp_rocker_of_dpa_groups(Monitor *mon, const QDict *qdict)
{
    RockerOfDpaGroupList *list, *g;
    const char *name = qdict_get_str(qdict, "name");
    uint8_t type = qdict_get_try_int(qdict, "type", 9);
    Error *err = NULL;

    list = qmp_query_rocker_of_dpa_groups(name, type != 9, type, &err);
    if (hmp_handle_error(mon, err)) {
        return;
    }

    monitor_printf(mon, "id (decode) --> buckets\n");

    for (g = list; g; g = g->next) {
        RockerOfDpaGroup *group = g->value;
        bool set = false;

        monitor_printf(mon, "0x%08x", group->id);

        monitor_printf(mon, " (type %s", group->type == 0 ? "L2 interface" :
                                         group->type == 1 ? "L2 rewrite" :
                                         group->type == 2 ? "L3 unicast" :
                                         group->type == 3 ? "L2 multicast" :
                                         group->type == 4 ? "L2 flood" :
                                         group->type == 5 ? "L3 interface" :
                                         group->type == 6 ? "L3 multicast" :
                                         group->type == 7 ? "L3 ECMP" :
                                         group->type == 8 ? "L2 overlay" :
                                         "unknown");

        if (group->has_vlan_id) {
            monitor_printf(mon, " vlan %d", group->vlan_id);
        }

        if (group->has_pport) {
            monitor_printf(mon, " pport %d", group->pport);
        }

        if (group->has_index) {
            monitor_printf(mon, " index %d", group->index);
        }

        monitor_printf(mon, ") -->");

        if (group->has_set_vlan_id && group->set_vlan_id) {
            set = true;
            monitor_printf(mon, " set vlan %d",
                           group->set_vlan_id & VLAN_VID_MASK);
        }

        if (group->set_eth_src) {
            if (!set) {
                set = true;
                monitor_printf(mon, " set");
            }
            monitor_printf(mon, " src %s", group->set_eth_src);
        }

        if (group->set_eth_dst) {
            if (!set) {
                monitor_printf(mon, " set");
            }
            monitor_printf(mon, " dst %s", group->set_eth_dst);
        }

        if (group->has_ttl_check && group->ttl_check) {
            monitor_printf(mon, " check TTL");
        }

        if (group->has_group_id && group->group_id) {
            monitor_printf(mon, " group id 0x%08x", group->group_id);
        }

        if (group->has_pop_vlan && group->pop_vlan) {
            monitor_printf(mon, " pop vlan");
        }

        if (group->has_out_pport) {
            monitor_printf(mon, " out pport %d", group->out_pport);
        }

        if (group->has_group_ids) {
            struct uint32List *id;

            monitor_printf(mon, " groups [");
            for (id = group->group_ids; id; id = id->next) {
                monitor_printf(mon, "0x%08x", id->value);
                if (id->next) {
                    monitor_printf(mon, ",");
                }
            }
            monitor_printf(mon, "]");
        }

        monitor_printf(mon, "\n");
    }

    qapi_free_RockerOfDpaGroupList(list);
}
