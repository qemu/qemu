/*
 * QEMU TX packets abstraction
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

#ifndef NET_TX_PKT_H
#define NET_TX_PKT_H

#include "qemu/osdep.h"
#include "net/eth.h"
#include "exec/hwaddr.h"

/* define to enable packet dump functions */
/*#define NET_TX_PKT_DEBUG*/

struct NetTxPkt;

/**
 * Init function for tx packet functionality
 *
 * @pkt:            packet pointer
 * @max_frags:      max tx ip fragments
 * @has_virt_hdr:   device uses virtio header.
 */
void net_tx_pkt_init(struct NetTxPkt **pkt, uint32_t max_frags,
    bool has_virt_hdr);

/**
 * Clean all tx packet resources.
 *
 * @pkt:            packet.
 */
void net_tx_pkt_uninit(struct NetTxPkt *pkt);

/**
 * get virtio header
 *
 * @pkt:            packet
 * @ret:            virtio header
 */
struct virtio_net_hdr *net_tx_pkt_get_vhdr(struct NetTxPkt *pkt);

/**
 * build virtio header (will be stored in module context)
 *
 * @pkt:            packet
 * @tso_enable:     TSO enabled
 * @csum_enable:    CSO enabled
 * @gso_size:       MSS size for TSO
 *
 */
void net_tx_pkt_build_vheader(struct NetTxPkt *pkt, bool tso_enable,
    bool csum_enable, uint32_t gso_size);

/**
* updates vlan tag, and adds vlan header with custom ethernet type
* in case it is missing.
*
* @pkt:            packet
* @vlan:           VLAN tag
* @vlan_ethtype:   VLAN header Ethernet type
*
*/
void net_tx_pkt_setup_vlan_header_ex(struct NetTxPkt *pkt,
    uint16_t vlan, uint16_t vlan_ethtype);

/**
* updates vlan tag, and adds vlan header in case it is missing
*
* @pkt:            packet
* @vlan:           VLAN tag
*
*/
static inline void
net_tx_pkt_setup_vlan_header(struct NetTxPkt *pkt, uint16_t vlan)
{
    net_tx_pkt_setup_vlan_header_ex(pkt, vlan, ETH_P_VLAN);
}

/**
 * populate data fragment into pkt context.
 *
 * @pkt:            packet
 * @pa:             physical address of fragment
 * @len:            length of fragment
 *
 */
bool net_tx_pkt_add_raw_fragment(struct NetTxPkt *pkt, hwaddr pa,
    size_t len);

/**
 * Fix ip header fields and calculate IP header and pseudo header checksums.
 *
 * @pkt:            packet
 *
 */
void net_tx_pkt_update_ip_checksums(struct NetTxPkt *pkt);

/**
 * Calculate the IP header checksum.
 *
 * @pkt:            packet
 *
 */
void net_tx_pkt_update_ip_hdr_checksum(struct NetTxPkt *pkt);

/**
 * get length of all populated data.
 *
 * @pkt:            packet
 * @ret:            total data length
 *
 */
size_t net_tx_pkt_get_total_len(struct NetTxPkt *pkt);

/**
 * get packet type
 *
 * @pkt:            packet
 * @ret:            packet type
 *
 */
eth_pkt_types_e net_tx_pkt_get_packet_type(struct NetTxPkt *pkt);

/**
 * prints packet data if debug is enabled
 *
 * @pkt:            packet
 *
 */
void net_tx_pkt_dump(struct NetTxPkt *pkt);

/**
 * reset tx packet private context (needed to be called between packets)
 *
 * @pkt:            packet
 *
 */
void net_tx_pkt_reset(struct NetTxPkt *pkt);

/**
 * Send packet to qemu. handles sw offloads if vhdr is not supported.
 *
 * @pkt:            packet
 * @nc:             NetClientState
 * @ret:            operation result
 *
 */
bool net_tx_pkt_send(struct NetTxPkt *pkt, NetClientState *nc);

/**
* Redirect packet directly to receive path (emulate loopback phy).
* Handles sw offloads if vhdr is not supported.
*
* @pkt:            packet
* @nc:             NetClientState
* @ret:            operation result
*
*/
bool net_tx_pkt_send_loopback(struct NetTxPkt *pkt, NetClientState *nc);

/**
 * parse raw packet data and analyze offload requirements.
 *
 * @pkt:            packet
 *
 */
bool net_tx_pkt_parse(struct NetTxPkt *pkt);

/**
* indicates if there are data fragments held by this packet object.
*
* @pkt:            packet
*
*/
bool net_tx_pkt_has_fragments(struct NetTxPkt *pkt);

#endif
