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

void hmp_rocker(MonitorHMP *hmp, const QDict *qdict)
{
    const char *name = qdict_get_str(qdict, "name");
    RockerSwitch *rocker;
    Error *err = NULL;

    rocker = qmp_query_rocker(name, &err);
    if (hmp_handle_error(hmp, err)) {
        return;
    }

    monitor_hmp_printf(hmp, "name: %s\n", rocker->name);
    monitor_hmp_printf(hmp, "id: 0x%" PRIx64 "\n", rocker->id);
    monitor_hmp_printf(hmp, "ports: %d\n", rocker->ports);

    qapi_free_RockerSwitch(rocker);
}

void hmp_rocker_ports(MonitorHMP *hmp, const QDict *qdict)
{
    RockerPortList *list, *port;
    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    list = qmp_query_rocker_ports(name, &err);
    if (hmp_handle_error(hmp, err)) {
        return;
    }

    monitor_hmp_printf(hmp, "            ena/    speed/ auto\n");
    monitor_hmp_printf(hmp, "      port  link    duplex neg?\n");

    for (port = list; port; port = port->next) {
        monitor_hmp_printf(hmp, "%10s  %-4s   %-3s  %2s  %s\n",
                           port->value->name,
                           port->value->enabled ? port->value->link_up ?
                           "up" : "down" : "!ena",
                           port->value->speed == 10000 ? "10G" : "??",
                           port->value->duplex ? "FD" : "HD",
                           port->value->autoneg ? "Yes" : "No");
    }

    qapi_free_RockerPortList(list);
}

void hmp_rocker_of_dpa_flows(MonitorHMP *hmp, const QDict *qdict)
{
    RockerOfDpaFlowList *list, *info;
    const char *name = qdict_get_str(qdict, "name");
    uint32_t tbl_id = qdict_get_try_int(qdict, "tbl_id", -1);
    Error *err = NULL;

    list = qmp_query_rocker_of_dpa_flows(name, tbl_id != -1, tbl_id, &err);
    if (hmp_handle_error(hmp, err)) {
        return;
    }

    monitor_hmp_printf(hmp, "prio tbl hits key(mask) --> actions\n");

    for (info = list; info; info = info->next) {
        RockerOfDpaFlow *flow = info->value;
        RockerOfDpaFlowKey *key = flow->key;
        RockerOfDpaFlowMask *mask = flow->mask;
        RockerOfDpaFlowAction *action = flow->action;

        if (flow->hits) {
            monitor_hmp_printf(hmp, "%-4d %-3d %-4" PRIu64,
                               key->priority, key->tbl_id, flow->hits);
        } else {
            monitor_hmp_printf(hmp, "%-4d %-3d     ",
                               key->priority, key->tbl_id);
        }

        if (key->has_in_pport) {
            monitor_hmp_printf(hmp, " pport %d", key->in_pport);
            if (mask->has_in_pport) {
                monitor_hmp_printf(hmp, "(0x%x)", mask->in_pport);
            }
        }

        if (key->has_vlan_id) {
            monitor_hmp_printf(hmp, " vlan %d",
                               key->vlan_id & VLAN_VID_MASK);
            if (mask->has_vlan_id) {
                monitor_hmp_printf(hmp, "(0x%x)", mask->vlan_id);
            }
        }

        if (key->has_tunnel_id) {
            monitor_hmp_printf(hmp, " tunnel %d", key->tunnel_id);
            if (mask->has_tunnel_id) {
                monitor_hmp_printf(hmp, "(0x%x)", mask->tunnel_id);
            }
        }

        if (key->has_eth_type) {
            switch (key->eth_type) {
            case 0x0806:
                monitor_hmp_printf(hmp, " ARP");
                break;
            case 0x0800:
                monitor_hmp_printf(hmp, " IP");
                break;
            case 0x86dd:
                monitor_hmp_printf(hmp, " IPv6");
                break;
            case 0x8809:
                monitor_hmp_printf(hmp, " LACP");
                break;
            case 0x88cc:
                monitor_hmp_printf(hmp, " LLDP");
                break;
            default:
                monitor_hmp_printf(hmp, " eth type 0x%04x", key->eth_type);
                break;
            }
        }

        if (key->eth_src) {
            if ((strcmp(key->eth_src, "01:00:00:00:00:00") == 0) &&
                mask->eth_src &&
                (strcmp(mask->eth_src, "01:00:00:00:00:00") == 0)) {
                monitor_hmp_printf(hmp, " src <any mcast/bcast>");
            } else if ((strcmp(key->eth_src, "00:00:00:00:00:00") == 0) &&
                mask->eth_src &&
                (strcmp(mask->eth_src, "01:00:00:00:00:00") == 0)) {
                monitor_hmp_printf(hmp, " src <any ucast>");
            } else {
                monitor_hmp_printf(hmp, " src %s", key->eth_src);
                if (mask->eth_src) {
                    monitor_hmp_printf(hmp, "(%s)", mask->eth_src);
                }
            }
        }

        if (key->eth_dst) {
            if ((strcmp(key->eth_dst, "01:00:00:00:00:00") == 0) &&
                mask->eth_dst &&
                (strcmp(mask->eth_dst, "01:00:00:00:00:00") == 0)) {
                monitor_hmp_printf(hmp, " dst <any mcast/bcast>");
            } else if ((strcmp(key->eth_dst, "00:00:00:00:00:00") == 0) &&
                mask->eth_dst &&
                (strcmp(mask->eth_dst, "01:00:00:00:00:00") == 0)) {
                monitor_hmp_printf(hmp, " dst <any ucast>");
            } else {
                monitor_hmp_printf(hmp, " dst %s", key->eth_dst);
                if (mask->eth_dst) {
                    monitor_hmp_printf(hmp, "(%s)", mask->eth_dst);
                }
            }
        }

        if (key->has_ip_proto) {
            monitor_hmp_printf(hmp, " proto %d", key->ip_proto);
            if (mask->has_ip_proto) {
                monitor_hmp_printf(hmp, "(0x%x)", mask->ip_proto);
            }
        }

        if (key->has_ip_tos) {
            monitor_hmp_printf(hmp, " TOS %d", key->ip_tos);
            if (mask->has_ip_tos) {
                monitor_hmp_printf(hmp, "(0x%x)", mask->ip_tos);
            }
        }

        if (key->ip_dst) {
            monitor_hmp_printf(hmp, " dst %s", key->ip_dst);
        }

        if (action->has_goto_tbl || action->has_group_id ||
            action->has_new_vlan_id) {
            monitor_hmp_printf(hmp, " -->");
        }

        if (action->has_new_vlan_id) {
            monitor_hmp_printf(hmp, " apply new vlan %d",
                               ntohs(action->new_vlan_id));
        }

        if (action->has_group_id) {
            monitor_hmp_printf(hmp, " write group 0x%08x", action->group_id);
        }

        if (action->has_goto_tbl) {
            monitor_hmp_printf(hmp, " goto tbl %d", action->goto_tbl);
        }

        monitor_hmp_printf(hmp, "\n");
    }

    qapi_free_RockerOfDpaFlowList(list);
}

void hmp_rocker_of_dpa_groups(MonitorHMP *hmp, const QDict *qdict)
{
    RockerOfDpaGroupList *list, *g;
    const char *name = qdict_get_str(qdict, "name");
    uint8_t type = qdict_get_try_int(qdict, "type", 9);
    Error *err = NULL;

    list = qmp_query_rocker_of_dpa_groups(name, type != 9, type, &err);
    if (hmp_handle_error(hmp, err)) {
        return;
    }

    monitor_hmp_printf(hmp, "id (decode) --> buckets\n");

    for (g = list; g; g = g->next) {
        RockerOfDpaGroup *group = g->value;
        bool set = false;

        monitor_hmp_printf(hmp, "0x%08x", group->id);

        monitor_hmp_printf(hmp, " (type %s", group->type == 0 ? "L2 interface" :
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
            monitor_hmp_printf(hmp, " vlan %d", group->vlan_id);
        }

        if (group->has_pport) {
            monitor_hmp_printf(hmp, " pport %d", group->pport);
        }

        if (group->has_index) {
            monitor_hmp_printf(hmp, " index %d", group->index);
        }

        monitor_hmp_printf(hmp, ") -->");

        if (group->has_set_vlan_id && group->set_vlan_id) {
            set = true;
            monitor_hmp_printf(hmp, " set vlan %d",
                               group->set_vlan_id & VLAN_VID_MASK);
        }

        if (group->set_eth_src) {
            if (!set) {
                set = true;
                monitor_hmp_printf(hmp, " set");
            }
            monitor_hmp_printf(hmp, " src %s", group->set_eth_src);
        }

        if (group->set_eth_dst) {
            if (!set) {
                monitor_hmp_printf(hmp, " set");
            }
            monitor_hmp_printf(hmp, " dst %s", group->set_eth_dst);
        }

        if (group->has_ttl_check && group->ttl_check) {
            monitor_hmp_printf(hmp, " check TTL");
        }

        if (group->has_group_id && group->group_id) {
            monitor_hmp_printf(hmp, " group id 0x%08x", group->group_id);
        }

        if (group->has_pop_vlan && group->pop_vlan) {
            monitor_hmp_printf(hmp, " pop vlan");
        }

        if (group->has_out_pport) {
            monitor_hmp_printf(hmp, " out pport %d", group->out_pport);
        }

        if (group->has_group_ids) {
            struct uint32List *id;

            monitor_hmp_printf(hmp, " groups [");
            for (id = group->group_ids; id; id = id->next) {
                monitor_hmp_printf(hmp, "0x%08x", id->value);
                if (id->next) {
                    monitor_hmp_printf(hmp, ",");
                }
            }
            monitor_hmp_printf(hmp, "]");
        }

        monitor_hmp_printf(hmp, "\n");
    }

    qapi_free_RockerOfDpaGroupList(list);
}
