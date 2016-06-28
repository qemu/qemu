/*
 * Definitions and prototypes for SLIRP stateless DHCPv6
 *
 * Copyright 2016 Thomas Huth, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 */
#ifndef SLIRP_DHCPV6_H
#define SLIRP_DHCPV6_H

#define DHCPV6_SERVER_PORT 547

#define ALLDHCP_MULTICAST { .s6_addr = \
                            { 0xff, 0x02, 0x00, 0x00,\
                            0x00, 0x00, 0x00, 0x00,\
                            0x00, 0x00, 0x00, 0x00,\
                            0x00, 0x01, 0x00, 0x02 } }

void dhcpv6_input(struct sockaddr_in6 *srcsas, struct mbuf *m);

#endif
