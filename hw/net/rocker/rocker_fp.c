/*
 * QEMU rocker switch emulation - front-panel ports
 *
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "net/clients.h"

#include "rocker.h"
#include "rocker_hw.h"
#include "rocker_fp.h"
#include "rocker_world.h"

enum duplex {
    DUPLEX_HALF = 0,
    DUPLEX_FULL
};

struct fp_port {
    Rocker *r;
    World *world;
    unsigned int index;
    char *name;
    uint32_t pport;
    bool enabled;
    uint32_t speed;
    uint8_t duplex;
    uint8_t autoneg;
    uint8_t learning;
    NICState *nic;
    NICConf conf;
};

char *fp_port_get_name(FpPort *port)
{
    return port->name;
}

bool fp_port_get_link_up(FpPort *port)
{
    return !qemu_get_queue(port->nic)->link_down;
}

void fp_port_get_info(FpPort *port, RockerPortList *info)
{
    info->value->name = g_strdup(port->name);
    info->value->enabled = port->enabled;
    info->value->link_up = fp_port_get_link_up(port);
    info->value->speed = port->speed;
    info->value->duplex = port->duplex;
    info->value->autoneg = port->autoneg;
}

void fp_port_get_macaddr(FpPort *port, MACAddr *macaddr)
{
    memcpy(macaddr->a, port->conf.macaddr.a, sizeof(macaddr->a));
}

void fp_port_set_macaddr(FpPort *port, MACAddr *macaddr)
{
/* XXX TODO implement and test setting mac addr
 * XXX memcpy(port->conf.macaddr.a, macaddr.a, sizeof(port->conf.macaddr.a));
 */
}

uint8_t fp_port_get_learning(FpPort *port)
{
    return port->learning;
}

void fp_port_set_learning(FpPort *port, uint8_t learning)
{
    port->learning = learning;
}

int fp_port_get_settings(FpPort *port, uint32_t *speed,
                         uint8_t *duplex, uint8_t *autoneg)
{
    *speed = port->speed;
    *duplex = port->duplex;
    *autoneg = port->autoneg;

    return ROCKER_OK;
}

int fp_port_set_settings(FpPort *port, uint32_t speed,
                         uint8_t duplex, uint8_t autoneg)
{
    /* XXX validate inputs */

    port->speed = speed;
    port->duplex = duplex;
    port->autoneg = autoneg;

    return ROCKER_OK;
}

bool fp_port_from_pport(uint32_t pport, uint32_t *port)
{
    if (pport < 1 || pport > ROCKER_FP_PORTS_MAX) {
        return false;
    }
    *port = pport - 1;
    return true;
}

int fp_port_eg(FpPort *port, const struct iovec *iov, int iovcnt)
{
    NetClientState *nc = qemu_get_queue(port->nic);

    if (port->enabled) {
        qemu_sendv_packet(nc, iov, iovcnt);
    }

    return ROCKER_OK;
}

static ssize_t fp_port_receive_iov(NetClientState *nc, const struct iovec *iov,
                                   int iovcnt)
{
    FpPort *port = qemu_get_nic_opaque(nc);

    /* If the port is disabled, we want to drop this pkt
     * now rather than queing it for later.  We don't want
     * any stale pkts getting into the device when the port
     * transitions to enabled.
     */

    if (!port->enabled) {
        return -1;
    }

    return world_ingress(port->world, port->pport, iov, iovcnt);
}

static ssize_t fp_port_receive(NetClientState *nc, const uint8_t *buf,
                               size_t size)
{
    const struct iovec iov = {
        .iov_base = (uint8_t *)buf,
        .iov_len = size
    };

    return fp_port_receive_iov(nc, &iov, 1);
}

static void fp_port_cleanup(NetClientState *nc)
{
}

static void fp_port_set_link_status(NetClientState *nc)
{
    FpPort *port = qemu_get_nic_opaque(nc);

    rocker_event_link_changed(port->r, port->pport, !nc->link_down);
}

static NetClientInfo fp_port_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .receive = fp_port_receive,
    .receive_iov = fp_port_receive_iov,
    .cleanup = fp_port_cleanup,
    .link_status_changed = fp_port_set_link_status,
};

World *fp_port_get_world(FpPort *port)
{
    return port->world;
}

void fp_port_set_world(FpPort *port, World *world)
{
    DPRINTF("port %d setting world \"%s\"\n", port->index, world_name(world));
    port->world = world;
}

bool fp_port_check_world(FpPort *port, World *world)
{
    return port->world == world;
}

bool fp_port_enabled(FpPort *port)
{
    return port->enabled;
}

static void fp_port_set_link(FpPort *port, bool up)
{
    NetClientState *nc = qemu_get_queue(port->nic);

    if (up == nc->link_down) {
        nc->link_down = !up;
        nc->info->link_status_changed(nc);
    }
}

void fp_port_enable(FpPort *port)
{
    fp_port_set_link(port, true);
    port->enabled = true;
    DPRINTF("port %d enabled\n", port->index);
}

void fp_port_disable(FpPort *port)
{
    port->enabled = false;
    fp_port_set_link(port, false);
    DPRINTF("port %d disabled\n", port->index);
}

FpPort *fp_port_alloc(Rocker *r, char *sw_name,
                      MACAddr *start_mac, unsigned int index,
                      NICPeers *peers)
{
    FpPort *port = g_new0(FpPort, 1);

    if (!port) {
        return NULL;
    }

    port->r = r;
    port->index = index;
    port->pport = index + 1;

    /* front-panel switch port names are 1-based */

    port->name = g_strdup_printf("%sp%d", sw_name, port->pport);

    memcpy(port->conf.macaddr.a, start_mac, sizeof(port->conf.macaddr.a));
    port->conf.macaddr.a[5] += index;
    port->conf.bootindex = -1;
    port->conf.peers = *peers;

    port->nic = qemu_new_nic(&fp_port_info, &port->conf,
                             sw_name, NULL, port);
    qemu_format_nic_info_str(qemu_get_queue(port->nic),
                             port->conf.macaddr.a);

    fp_port_reset(port);

    return port;
}

void fp_port_free(FpPort *port)
{
    qemu_del_nic(port->nic);
    g_free(port->name);
    g_free(port);
}

void fp_port_reset(FpPort *port)
{
    fp_port_disable(port);
    port->speed = 10000;   /* 10Gbps */
    port->duplex = DUPLEX_FULL;
    port->autoneg = 0;
}
