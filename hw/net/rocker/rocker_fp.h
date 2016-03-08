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

#ifndef _ROCKER_FP_H_
#define _ROCKER_FP_H_

#include "net/net.h"
#include "qemu/iov.h"

#define ROCKER_FP_PORTS_MAX 62

typedef struct fp_port FpPort;

int fp_port_eg(FpPort *port, const struct iovec *iov, int iovcnt);

char *fp_port_get_name(FpPort *port);
bool fp_port_get_link_up(FpPort *port);
void fp_port_get_info(FpPort *port, RockerPortList *info);
void fp_port_get_macaddr(FpPort *port, MACAddr *macaddr);
void fp_port_set_macaddr(FpPort *port, MACAddr *macaddr);
uint8_t fp_port_get_learning(FpPort *port);
void fp_port_set_learning(FpPort *port, uint8_t learning);
int fp_port_get_settings(FpPort *port, uint32_t *speed,
                         uint8_t *duplex, uint8_t *autoneg);
int fp_port_set_settings(FpPort *port, uint32_t speed,
                         uint8_t duplex, uint8_t autoneg);
bool fp_port_from_pport(uint32_t pport, uint32_t *port);
World *fp_port_get_world(FpPort *port);
void fp_port_set_world(FpPort *port, World *world);
bool fp_port_check_world(FpPort *port, World *world);
bool fp_port_enabled(FpPort *port);
void fp_port_enable(FpPort *port);
void fp_port_disable(FpPort *port);

FpPort *fp_port_alloc(Rocker *r, char *sw_name,
                      MACAddr *start_mac, unsigned int index,
                      NICPeers *peers);
void fp_port_free(FpPort *port);
void fp_port_reset(FpPort *port);

#endif /* _ROCKER_FP_H_ */
