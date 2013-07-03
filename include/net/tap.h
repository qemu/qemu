/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_NET_TAP_H
#define QEMU_NET_TAP_H

#include "qemu-common.h"
#include "qapi-types.h"

bool tap_has_ufo(NetClientState *nc);
int tap_has_vnet_hdr(NetClientState *nc);
int tap_has_vnet_hdr_len(NetClientState *nc, int len);
void tap_using_vnet_hdr(NetClientState *nc, bool using_vnet_hdr);
void tap_set_offload(NetClientState *nc, int csum, int tso4, int tso6, int ecn, int ufo);
void tap_set_vnet_hdr_len(NetClientState *nc, int len);
int tap_enable(NetClientState *nc);
int tap_disable(NetClientState *nc);

int tap_get_fd(NetClientState *nc);

struct vhost_net;
struct vhost_net *tap_get_vhost_net(NetClientState *nc);

struct virtio_net_hdr
{
#define VIRTIO_NET_HDR_F_NEEDS_CSUM     1       // Use csum_start, csum_offset
#define VIRTIO_NET_HDR_F_DATA_VALID    2       // Csum is valid
    uint8_t flags;
#define VIRTIO_NET_HDR_GSO_NONE         0       // Not a GSO frame
#define VIRTIO_NET_HDR_GSO_TCPV4        1       // GSO frame, IPv4 TCP (TSO)
#define VIRTIO_NET_HDR_GSO_UDP          3       // GSO frame, IPv4 UDP (UFO)
#define VIRTIO_NET_HDR_GSO_TCPV6        4       // GSO frame, IPv6 TCP
#define VIRTIO_NET_HDR_GSO_ECN          0x80    // TCP has ECN set
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

#endif /* QEMU_NET_TAP_H */
