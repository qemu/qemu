/*
 * QEMU RX packets abstraction
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

#ifndef NET_RX_PKT_H
#define NET_RX_PKT_H

#include "net/eth.h"

/* defines to enable packet dump functions */
/*#define NET_RX_PKT_DEBUG*/

struct NetRxPkt;

/**
 * Clean all rx packet resources
 *
 * @pkt:            packet
 *
 */
void net_rx_pkt_uninit(struct NetRxPkt *pkt);

/**
 * Init function for rx packet functionality
 *
 * @pkt:            packet pointer
 * @has_virt_hdr:   device uses virtio header
 *
 */
void net_rx_pkt_init(struct NetRxPkt **pkt, bool has_virt_hdr);

/**
 * returns total length of data attached to rx context
 *
 * @pkt:            packet
 *
 * Return:  nothing
 *
 */
size_t net_rx_pkt_get_total_len(struct NetRxPkt *pkt);

/**
 * parse and set packet analysis results
 *
 * @pkt:            packet
 * @data:           pointer to the data buffer to be parsed
 * @len:            data length
 *
 */
void net_rx_pkt_set_protocols(struct NetRxPkt *pkt, const void *data,
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
void net_rx_pkt_get_protocols(struct NetRxPkt *pkt,
                                 bool *isip4, bool *isip6,
                                 bool *isudp, bool *istcp);

/**
 * returns virtio header stored in rx context
 *
 * @pkt:            packet
 * @ret:            virtio header
 *
 */
struct virtio_net_hdr *net_rx_pkt_get_vhdr(struct NetRxPkt *pkt);

/**
 * returns packet type
 *
 * @pkt:            packet
 * @ret:            packet type
 *
 */
eth_pkt_types_e net_rx_pkt_get_packet_type(struct NetRxPkt *pkt);

/**
 * returns vlan tag
 *
 * @pkt:            packet
 * @ret:            VLAN tag
 *
 */
uint16_t net_rx_pkt_get_vlan_tag(struct NetRxPkt *pkt);

/**
 * tells whether vlan was stripped from the packet
 *
 * @pkt:            packet
 * @ret:            VLAN stripped sign
 *
 */
bool net_rx_pkt_is_vlan_stripped(struct NetRxPkt *pkt);

/**
 * notifies caller if the packet has virtio header
 *
 * @pkt:            packet
 * @ret:            true if packet has virtio header, false otherwize
 *
 */
bool net_rx_pkt_has_virt_hdr(struct NetRxPkt *pkt);

/**
 * attach data to rx packet
 *
 * @pkt:            packet
 * @data:           pointer to the data buffer
 * @len:            data length
 * @strip_vlan:     should the module strip vlan from data
 *
 */
void net_rx_pkt_attach_data(struct NetRxPkt *pkt, const void *data,
    size_t len, bool strip_vlan);

/**
 * returns io vector that holds the attached data
 *
 * @pkt:            packet
 * @ret:            pointer to IOVec
 *
 */
struct iovec *net_rx_pkt_get_iovec(struct NetRxPkt *pkt);

/**
 * prints rx packet data if debug is enabled
 *
 * @pkt:            packet
 *
 */
void net_rx_pkt_dump(struct NetRxPkt *pkt);

/**
 * copy passed vhdr data to packet context
 *
 * @pkt:            packet
 * @vhdr:           VHDR buffer
 *
 */
void net_rx_pkt_set_vhdr(struct NetRxPkt *pkt,
    struct virtio_net_hdr *vhdr);

/**
 * save packet type in packet context
 *
 * @pkt:            packet
 * @packet_type:    the packet type
 *
 */
void net_rx_pkt_set_packet_type(struct NetRxPkt *pkt,
    eth_pkt_types_e packet_type);

#endif
