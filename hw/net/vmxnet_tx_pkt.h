/*
 * QEMU VMWARE VMXNET* paravirtual NICs - TX packets abstraction
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

#ifndef VMXNET_TX_PKT_H
#define VMXNET_TX_PKT_H

#include "stdint.h"
#include "stdbool.h"
#include "net/eth.h"
#include "exec/hwaddr.h"

/* define to enable packet dump functions */
/*#define VMXNET_TX_PKT_DEBUG*/

struct VmxnetTxPkt;

/**
 * Init function for tx packet functionality
 *
 * @pkt:            packet pointer
 * @max_frags:      max tx ip fragments
 * @has_virt_hdr:   device uses virtio header.
 */
void vmxnet_tx_pkt_init(struct VmxnetTxPkt **pkt, uint32_t max_frags,
    bool has_virt_hdr);

/**
 * Clean all tx packet resources.
 *
 * @pkt:            packet.
 */
void vmxnet_tx_pkt_uninit(struct VmxnetTxPkt *pkt);

/**
 * get virtio header
 *
 * @pkt:            packet
 * @ret:            virtio header
 */
struct virtio_net_hdr *vmxnet_tx_pkt_get_vhdr(struct VmxnetTxPkt *pkt);

/**
 * build virtio header (will be stored in module context)
 *
 * @pkt:            packet
 * @tso_enable:     TSO enabled
 * @csum_enable:    CSO enabled
 * @gso_size:       MSS size for TSO
 *
 */
void vmxnet_tx_pkt_build_vheader(struct VmxnetTxPkt *pkt, bool tso_enable,
    bool csum_enable, uint32_t gso_size);

/**
 * updates vlan tag, and adds vlan header in case it is missing
 *
 * @pkt:            packet
 * @vlan:           VLAN tag
 *
 */
void vmxnet_tx_pkt_setup_vlan_header(struct VmxnetTxPkt *pkt, uint16_t vlan);

/**
 * populate data fragment into pkt context.
 *
 * @pkt:            packet
 * @pa:             physical address of fragment
 * @len:            length of fragment
 *
 */
bool vmxnet_tx_pkt_add_raw_fragment(struct VmxnetTxPkt *pkt, hwaddr pa,
    size_t len);

/**
 * fix ip header fields and calculate checksums needed.
 *
 * @pkt:            packet
 *
 */
void vmxnet_tx_pkt_update_ip_checksums(struct VmxnetTxPkt *pkt);

/**
 * get length of all populated data.
 *
 * @pkt:            packet
 * @ret:            total data length
 *
 */
size_t vmxnet_tx_pkt_get_total_len(struct VmxnetTxPkt *pkt);

/**
 * get packet type
 *
 * @pkt:            packet
 * @ret:            packet type
 *
 */
eth_pkt_types_e vmxnet_tx_pkt_get_packet_type(struct VmxnetTxPkt *pkt);

/**
 * prints packet data if debug is enabled
 *
 * @pkt:            packet
 *
 */
void vmxnet_tx_pkt_dump(struct VmxnetTxPkt *pkt);

/**
 * reset tx packet private context (needed to be called between packets)
 *
 * @pkt:            packet
 *
 */
void vmxnet_tx_pkt_reset(struct VmxnetTxPkt *pkt);

/**
 * Send packet to qemu. handles sw offloads if vhdr is not supported.
 *
 * @pkt:            packet
 * @nc:             NetClientState
 * @ret:            operation result
 *
 */
bool vmxnet_tx_pkt_send(struct VmxnetTxPkt *pkt, NetClientState *nc);

/**
 * parse raw packet data and analyze offload requirements.
 *
 * @pkt:            packet
 *
 */
bool vmxnet_tx_pkt_parse(struct VmxnetTxPkt *pkt);

#endif
