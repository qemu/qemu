/*
 * QEMU rocker switch emulation
 *
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2014 Neil Horman <nhorman@tuxdriver.com>
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

#ifndef ROCKER_H
#define ROCKER_H

#include "qemu/sockets.h"
#include "qom/object.h"

#if defined(DEBUG_ROCKER)
#  define DPRINTF(fmt, ...) \
    do {                                                           \
        g_autoptr(GDateTime) now = g_date_time_new_now_local();    \
        g_autofree char *nowstr = g_date_time_format(now, "%T.%f");\
        fprintf(stderr, "%s ROCKER: " fmt, nowstr, ## __VA_ARGS__);\
    } while (0)
#else
static inline G_GNUC_PRINTF(1, 2) int DPRINTF(const char *fmt, ...)
{
    return 0;
}
#endif

static inline bool ipv4_addr_is_multicast(uint32_t addr)
{
    return (addr & htonl(0xf0000000)) == htonl(0xe0000000);
}

typedef struct ipv6_addr {
    union {
        uint8_t addr8[16];
        uint16_t addr16[8];
        uint32_t addr32[4];
    };
} Ipv6Addr;

static inline bool ipv6_addr_is_multicast(const Ipv6Addr *addr)
{
    return (addr->addr32[0] & htonl(0xFF000000)) == htonl(0xFF000000);
}

typedef struct world World;
typedef struct desc_info DescInfo;
typedef struct desc_ring DescRing;

#define TYPE_ROCKER "rocker"
typedef struct rocker Rocker;
DECLARE_INSTANCE_CHECKER(Rocker, ROCKER,
                         TYPE_ROCKER)

Rocker *rocker_find(const char *name);
int rocker_event_link_changed(Rocker *r, uint32_t pport, bool link_up);
int rocker_event_mac_vlan_seen(Rocker *r, uint32_t pport, uint8_t *addr,
                               uint16_t vlan_id);
int rx_produce(World *world, uint32_t pport,
               const struct iovec *iov, int iovcnt, uint8_t copy_to_cpu);
int rocker_port_eg(Rocker *r, uint32_t pport,
                   const struct iovec *iov, int iovcnt);

#endif /* ROCKER_H */
