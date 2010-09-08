/*
 *  Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2000 Maxim Krasnyansky <max_mk@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#ifndef QEMU_TAP_H
#define QEMU_TAP_H

#include <stdint.h>
#ifdef __linux__

#include <linux/ioctl.h>

/* Ioctl defines */
#define TUNSETIFF     _IOW('T', 202, int)
#define TUNGETFEATURES _IOR('T', 207, unsigned int)
#define TUNSETOFFLOAD  _IOW('T', 208, unsigned int)
#define TUNGETIFF      _IOR('T', 210, unsigned int)
#define TUNSETSNDBUF   _IOW('T', 212, int)
#define TUNGETVNETHDRSZ _IOR('T', 215, int)
#define TUNSETVNETHDRSZ _IOW('T', 216, int)

#endif

/* TUNSETIFF ifr flags */
#define IFF_TAP		0x0002
#define IFF_NO_PI	0x1000
#define IFF_VNET_HDR	0x4000

/* Features for GSO (TUNSETOFFLOAD). */
#define TUN_F_CSUM	0x01	/* You can hand me unchecksummed packets. */
#define TUN_F_TSO4	0x02	/* I can handle TSO for IPv4 packets */
#define TUN_F_TSO6	0x04	/* I can handle TSO for IPv6 packets */
#define TUN_F_TSO_ECN	0x08	/* I can handle TSO with ECN bits. */
#define TUN_F_UFO	0x10	/* I can handle UFO packets */

struct virtio_net_hdr
{
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
};

struct virtio_net_hdr_mrg_rxbuf
{
    struct virtio_net_hdr hdr;
    uint16_t num_buffers;   /* Number of merged rx buffers */
};

#endif /* QEMU_TAP_H */
