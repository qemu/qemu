/*
 * QEMU network structures definitions and helper functions
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

#include "net/eth.h"
#include "net/checksum.h"
#include "qemu-common.h"
#include "net/tap.h"

void eth_setup_vlan_headers(struct eth_header *ehdr, uint16_t vlan_tag,
    bool *is_new)
{
    struct vlan_header *vhdr = PKT_GET_VLAN_HDR(ehdr);

    switch (be16_to_cpu(ehdr->h_proto)) {
    case ETH_P_VLAN:
    case ETH_P_DVLAN:
        /* vlan hdr exists */
        *is_new = false;
        break;

    default:
        /* No VLAN header, put a new one */
        vhdr->h_proto = ehdr->h_proto;
        ehdr->h_proto = cpu_to_be16(ETH_P_VLAN);
        *is_new = true;
        break;
    }
    vhdr->h_tci = cpu_to_be16(vlan_tag);
}

uint8_t
eth_get_gso_type(uint16_t l3_proto, uint8_t *l3_hdr, uint8_t l4proto)
{
    uint8_t ecn_state = 0;

    if (l3_proto == ETH_P_IP) {
        struct ip_header *iphdr = (struct ip_header *) l3_hdr;

        if (IP_HEADER_VERSION(iphdr) == IP_HEADER_VERSION_4) {
            if (IPTOS_ECN(iphdr->ip_tos) == IPTOS_ECN_CE) {
                ecn_state = VIRTIO_NET_HDR_GSO_ECN;
            }
            if (l4proto == IP_PROTO_TCP) {
                return VIRTIO_NET_HDR_GSO_TCPV4 | ecn_state;
            } else if (l4proto == IP_PROTO_UDP) {
                return VIRTIO_NET_HDR_GSO_UDP | ecn_state;
            }
        }
    } else if (l3_proto == ETH_P_IPV6) {
        struct ip6_header *ip6hdr = (struct ip6_header *) l3_hdr;

        if (IP6_ECN(ip6hdr->ip6_ecn_acc) == IP6_ECN_CE) {
            ecn_state = VIRTIO_NET_HDR_GSO_ECN;
        }

        if (l4proto == IP_PROTO_TCP) {
            return VIRTIO_NET_HDR_GSO_TCPV6 | ecn_state;
        }
    }

    /* Unsupported offload */
    assert(false);

    return VIRTIO_NET_HDR_GSO_NONE | ecn_state;
}

void eth_get_protocols(const uint8_t *headers,
                       uint32_t hdr_length,
                       bool *isip4, bool *isip6,
                       bool *isudp, bool *istcp)
{
    int proto;
    size_t l2hdr_len = eth_get_l2_hdr_length(headers);
    assert(hdr_length >= eth_get_l2_hdr_length(headers));
    *isip4 = *isip6 = *isudp = *istcp = false;

    proto = eth_get_l3_proto(headers, l2hdr_len);
    if (proto == ETH_P_IP) {
        *isip4 = true;

        struct ip_header *iphdr;

        assert(hdr_length >=
            eth_get_l2_hdr_length(headers) + sizeof(struct ip_header));

        iphdr = PKT_GET_IP_HDR(headers);

        if (IP_HEADER_VERSION(iphdr) == IP_HEADER_VERSION_4) {
            if (iphdr->ip_p == IP_PROTO_TCP) {
                *istcp = true;
            } else if (iphdr->ip_p == IP_PROTO_UDP) {
                *isudp = true;
            }
        }
    } else if (proto == ETH_P_IPV6) {
        uint8_t l4proto;
        size_t full_ip6hdr_len;

        struct iovec hdr_vec;
        hdr_vec.iov_base = (void *) headers;
        hdr_vec.iov_len = hdr_length;

        *isip6 = true;
        if (eth_parse_ipv6_hdr(&hdr_vec, 1, l2hdr_len,
                              &l4proto, &full_ip6hdr_len)) {
            if (l4proto == IP_PROTO_TCP) {
                *istcp = true;
            } else if (l4proto == IP_PROTO_UDP) {
                *isudp = true;
            }
        }
    }
}

void
eth_setup_ip4_fragmentation(const void *l2hdr, size_t l2hdr_len,
                            void *l3hdr, size_t l3hdr_len,
                            size_t l3payload_len,
                            size_t frag_offset, bool more_frags)
{
    if (eth_get_l3_proto(l2hdr, l2hdr_len) == ETH_P_IP) {
        uint16_t orig_flags;
        struct ip_header *iphdr = (struct ip_header *) l3hdr;
        uint16_t frag_off_units = frag_offset / IP_FRAG_UNIT_SIZE;
        uint16_t new_ip_off;

        assert(frag_offset % IP_FRAG_UNIT_SIZE == 0);
        assert((frag_off_units & ~IP_OFFMASK) == 0);

        orig_flags = be16_to_cpu(iphdr->ip_off) & ~(IP_OFFMASK|IP_MF);
        new_ip_off = frag_off_units | orig_flags  | (more_frags ? IP_MF : 0);
        iphdr->ip_off = cpu_to_be16(new_ip_off);
        iphdr->ip_len = cpu_to_be16(l3payload_len + l3hdr_len);
    }
}

void
eth_fix_ip4_checksum(void *l3hdr, size_t l3hdr_len)
{
    struct ip_header *iphdr = (struct ip_header *) l3hdr;
    iphdr->ip_sum = 0;
    iphdr->ip_sum = cpu_to_be16(net_raw_checksum(l3hdr, l3hdr_len));
}

uint32_t
eth_calc_pseudo_hdr_csum(struct ip_header *iphdr, uint16_t csl)
{
    struct ip_pseudo_header ipph;
    ipph.ip_src = iphdr->ip_src;
    ipph.ip_dst = iphdr->ip_dst;
    ipph.ip_payload = cpu_to_be16(csl);
    ipph.ip_proto = iphdr->ip_p;
    ipph.zeros = 0;
    return net_checksum_add(sizeof(ipph), (uint8_t *) &ipph);
}

static bool
eth_is_ip6_extension_header_type(uint8_t hdr_type)
{
    switch (hdr_type) {
    case IP6_HOP_BY_HOP:
    case IP6_ROUTING:
    case IP6_FRAGMENT:
    case IP6_ESP:
    case IP6_AUTHENTICATION:
    case IP6_DESTINATON:
    case IP6_MOBILITY:
        return true;
    default:
        return false;
    }
}

bool eth_parse_ipv6_hdr(struct iovec *pkt, int pkt_frags,
                        size_t ip6hdr_off, uint8_t *l4proto,
                        size_t *full_hdr_len)
{
    struct ip6_header ip6_hdr;
    struct ip6_ext_hdr ext_hdr;
    size_t bytes_read;

    bytes_read = iov_to_buf(pkt, pkt_frags, ip6hdr_off,
                            &ip6_hdr, sizeof(ip6_hdr));
    if (bytes_read < sizeof(ip6_hdr)) {
        return false;
    }

    *full_hdr_len = sizeof(struct ip6_header);

    if (!eth_is_ip6_extension_header_type(ip6_hdr.ip6_nxt)) {
        *l4proto = ip6_hdr.ip6_nxt;
        return true;
    }

    do {
        bytes_read = iov_to_buf(pkt, pkt_frags, ip6hdr_off + *full_hdr_len,
                                &ext_hdr, sizeof(ext_hdr));
        *full_hdr_len += (ext_hdr.ip6r_len + 1) * IP6_EXT_GRANULARITY;
    } while (eth_is_ip6_extension_header_type(ext_hdr.ip6r_nxt));

    *l4proto = ext_hdr.ip6r_nxt;
    return true;
}
