/*
 * QEMU VMWARE VMXNET* paravirtual NICs - RX packets abstraction
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Tamir Shomer <tamirs@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VMXNET_RX_PKT_H
#define VMXNET_RX_PKT_H

#include "stdint.h"
#include "stdbool.h"
#include "net/eth.h"

/* defines to enable packet dump functions */
/*#define VMXNET_RX_PKT_DEBUG*/

struct VmxnetRxPkt;

/**
 * Clean all rx packet resources
 *
 * @pkt:            packet
 *
 */
void vmxnet_rx_pkt_uninit(struct VmxnetRxPkt *pkt);

/**
 * Init function for rx packet functionality
 *
 * @pkt:            packet pointer
 * @has_virt_hdr:   device uses virtio header
 *
 */
void vmxnet_rx_pkt_init(struct VmxnetRxPkt **pkt, bool has_virt_hdr);

/**
 * returns total length of data attached to rx context
 *
 * @pkt:            packet
 *
 * Return:  nothing
 *
 */
size_t vmxnet_rx_pkt_get_total_len(struct VmxnetRxPkt *pkt);

/**
 * parse and set packet analysis results
 *
 * @pkt:            packet
 * @data:           pointer to the data buffer to be parsed
 * @len:            data length
 *
 */
void vmxnet_rx_pkt_set_protocols(struct VmxnetRxPkt *pkt, const void *data,
                                 size_t len);

/**
 * fetches packet analysis results
 *
 * @pkt:            packet
 * @isip4:          whether the packet given is IPv4
 * @isip6:          whether the packet given is IPv6
 * @isudp:          whether the packet given is UDP
 * @istcp:          whether the packet given is TCP
 *
 */
void vmxnet_rx_pkt_get_protocols(struct VmxnetRxPkt *pkt,
                                 bool *isip4, bool *isip6,
                                 bool *isudp, bool *istcp);

/**
 * returns virtio header stored in rx context
 *
 * @pkt:            packet
 * @ret:            virtio header
 *
 */
struct virtio_net_hdr *vmxnet_rx_pkt_get_vhdr(struct VmxnetRxPkt *pkt);

/**
 * returns packet type
 *
 * @pkt:            packet
 * @ret:            packet type
 *
 */
eth_pkt_types_e vmxnet_rx_pkt_get_packet_type(struct VmxnetRxPkt *pkt);

/**
 * returns vlan tag
 *
 * @pkt:            packet
 * @ret:            VLAN tag
 *
 */
uint16_t vmxnet_rx_pkt_get_vlan_tag(struct VmxnetRxPkt *pkt);

/**
 * tells whether vlan was stripped from the packet
 *
 * @pkt:            packet
 * @ret:            VLAN stripped sign
 *
 */
bool vmxnet_rx_pkt_is_vlan_stripped(struct VmxnetRxPkt *pkt);

/**
 * notifies caller if the packet has virtio header
 *
 * @pkt:            packet
 * @ret:            true if packet has virtio header, false otherwize
 *
 */
bool vmxnet_rx_pkt_has_virt_hdr(struct VmxnetRxPkt *pkt);

/**
 * attach data to rx packet
 *
 * @pkt:            packet
 * @data:           pointer to the data buffer
 * @len:            data length
 * @strip_vlan:     should the module strip vlan from data
 *
 */
void vmxnet_rx_pkt_attach_data(struct VmxnetRxPkt *pkt, const void *data,
    size_t len, bool strip_vlan);

/**
 * returns io vector that holds the attached data
 *
 * @pkt:            packet
 * @ret:            pointer to IOVec
 *
 */
struct iovec *vmxnet_rx_pkt_get_iovec(struct VmxnetRxPkt *pkt);

/**
 * prints rx packet data if debug is enabled
 *
 * @pkt:            packet
 *
 */
void vmxnet_rx_pkt_dump(struct VmxnetRxPkt *pkt);

/**
 * copy passed vhdr data to packet context
 *
 * @pkt:            packet
 * @vhdr:           VHDR buffer
 *
 */
void vmxnet_rx_pkt_set_vhdr(struct VmxnetRxPkt *pkt,
    struct virtio_net_hdr *vhdr);

/**
 * save packet type in packet context
 *
 * @pkt:            packet
 * @packet_type:    the packet type
 *
 */
void vmxnet_rx_pkt_set_packet_type(struct VmxnetRxPkt *pkt,
    eth_pkt_types_e packet_type);

#endif
