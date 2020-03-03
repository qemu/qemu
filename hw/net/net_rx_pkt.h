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
* fetches L3 header offset
*
* @pkt:            packet
*
*/
size_t net_rx_pkt_get_l3_hdr_offset(struct NetRxPkt *pkt);

/**
* fetches L4 header offset
*
* @pkt:            packet
*
*/
size_t net_rx_pkt_get_l4_hdr_offset(struct NetRxPkt *pkt);

/**
* fetches L5 header offset
*
* @pkt:            packet
*
*/
size_t net_rx_pkt_get_l5_hdr_offset(struct NetRxPkt *pkt);

/**
 * fetches IP6 header analysis results
 *
 * Return:  pointer to analysis results structure which is stored in internal
 *          packet area.
 *
 */
eth_ip6_hdr_info *net_rx_pkt_get_ip6_info(struct NetRxPkt *pkt);

/**
 * fetches IP4 header analysis results
 *
 * Return:  pointer to analysis results structure which is stored in internal
 *          packet area.
 *
 */
eth_ip4_hdr_info *net_rx_pkt_get_ip4_info(struct NetRxPkt *pkt);

/**
 * fetches L4 header analysis results
 *
 * Return:  pointer to analysis results structure which is stored in internal
 *          packet area.
 *
 */
eth_l4_hdr_info *net_rx_pkt_get_l4_info(struct NetRxPkt *pkt);

typedef enum {
    NetPktRssIpV4,
    NetPktRssIpV4Tcp,
    NetPktRssIpV6Tcp,
    NetPktRssIpV6,
    NetPktRssIpV6Ex,
    NetPktRssIpV6TcpEx,
    NetPktRssIpV4Udp,
    NetPktRssIpV6Udp,
    NetPktRssIpV6UdpEx,
} NetRxPktRssType;

/**
* calculates RSS hash for packet
*
* @pkt:            packet
* @type:           RSS hash type
*
* Return:  Toeplitz RSS hash.
*
*/
uint32_t
net_rx_pkt_calc_rss_hash(struct NetRxPkt *pkt,
                         NetRxPktRssType type,
                         uint8_t *key);

/**
* fetches IP identification for the packet
*
* @pkt:            packet
*
*/
uint16_t net_rx_pkt_get_ip_id(struct NetRxPkt *pkt);

/**
* check if given packet is a TCP ACK packet
*
* @pkt:            packet
*
*/
bool net_rx_pkt_is_tcp_ack(struct NetRxPkt *pkt);

/**
* check if given packet contains TCP data
*
* @pkt:            packet
*
*/
bool net_rx_pkt_has_tcp_data(struct NetRxPkt *pkt);

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
* attach scatter-gather data to rx packet
*
* @pkt:            packet
* @iov:            received data scatter-gather list
* @iovcnt          number of elements in iov
* @iovoff          data start offset in the iov
* @strip_vlan:     should the module strip vlan from data
*
*/
void net_rx_pkt_attach_iovec(struct NetRxPkt *pkt,
                                const struct iovec *iov,
                                int iovcnt, size_t iovoff,
                                bool strip_vlan);

/**
* attach scatter-gather data to rx packet
*
* @pkt:            packet
* @iov:            received data scatter-gather list
* @iovcnt          number of elements in iov
* @iovoff          data start offset in the iov
* @strip_vlan:     should the module strip vlan from data
* @vet:            VLAN tag Ethernet type
*
*/
void net_rx_pkt_attach_iovec_ex(struct NetRxPkt *pkt,
                                   const struct iovec *iov, int iovcnt,
                                   size_t iovoff, bool strip_vlan,
                                   uint16_t vet);

/**
 * attach data to rx packet
 *
 * @pkt:            packet
 * @data:           pointer to the data buffer
 * @len:            data length
 * @strip_vlan:     should the module strip vlan from data
 *
 */
static inline void
net_rx_pkt_attach_data(struct NetRxPkt *pkt, const void *data,
                          size_t len, bool strip_vlan)
{
    const struct iovec iov = {
        .iov_base = (void *) data,
        .iov_len = len
    };

    net_rx_pkt_attach_iovec(pkt, &iov, 1, 0, strip_vlan);
}

/**
 * returns io vector that holds the attached data
 *
 * @pkt:            packet
 * @ret:            pointer to IOVec
 *
 */
struct iovec *net_rx_pkt_get_iovec(struct NetRxPkt *pkt);

/**
* returns io vector length that holds the attached data
*
* @pkt:            packet
* @ret:            IOVec length
*
*/
uint16_t net_rx_pkt_get_iovec_len(struct NetRxPkt *pkt);

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
* copy passed vhdr data to packet context
*
* @pkt:            packet
* @iov:            VHDR iov
* @iovcnt:         VHDR iov array size
*
*/
void net_rx_pkt_set_vhdr_iovec(struct NetRxPkt *pkt,
    const struct iovec *iov, int iovcnt);

/**
 * save packet type in packet context
 *
 * @pkt:            packet
 * @packet_type:    the packet type
 *
 */
void net_rx_pkt_set_packet_type(struct NetRxPkt *pkt,
    eth_pkt_types_e packet_type);

/**
* validate TCP/UDP checksum of the packet
*
* @pkt:            packet
* @csum_valid:     checksum validation result
* @ret:            true if validation was performed, false in case packet is
*                  not TCP/UDP or checksum validation is not possible
*
*/
bool net_rx_pkt_validate_l4_csum(struct NetRxPkt *pkt, bool *csum_valid);

/**
* validate IPv4 checksum of the packet
*
* @pkt:            packet
* @csum_valid:     checksum validation result
* @ret:            true if validation was performed, false in case packet is
*                  not TCP/UDP or checksum validation is not possible
*
*/
bool net_rx_pkt_validate_l3_csum(struct NetRxPkt *pkt, bool *csum_valid);

/**
* fix IPv4 checksum of the packet
*
* @pkt:            packet
* @ret:            true if checksum was fixed, false in case packet is
*                  not TCP/UDP or checksum correction is not possible
*
*/
bool net_rx_pkt_fix_l4_csum(struct NetRxPkt *pkt);

#endif
