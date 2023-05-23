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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "net/eth.h"
#include "net/checksum.h"
#include "net/tap.h"

void eth_setup_vlan_headers_ex(struct eth_header *ehdr, uint16_t vlan_tag,
    uint16_t vlan_ethtype, bool *is_new)
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
        ehdr->h_proto = cpu_to_be16(vlan_ethtype);
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
    qemu_log_mask(LOG_UNIMP, "%s: probably not GSO frame, "
        "unknown L3 protocol: 0x%04"PRIx16"\n", __func__, l3_proto);

    return VIRTIO_NET_HDR_GSO_NONE | ecn_state;
}

uint16_t
eth_get_l3_proto(const struct iovec *l2hdr_iov, int iovcnt, size_t l2hdr_len)
{
    uint16_t proto;
    size_t copied;
    size_t size = iov_size(l2hdr_iov, iovcnt);
    size_t proto_offset = l2hdr_len - sizeof(proto);

    if (size < proto_offset) {
        return ETH_P_UNKNOWN;
    }

    copied = iov_to_buf(l2hdr_iov, iovcnt, proto_offset,
                        &proto, sizeof(proto));

    return (copied == sizeof(proto)) ? be16_to_cpu(proto) : ETH_P_UNKNOWN;
}

static bool
_eth_copy_chunk(size_t input_size,
                const struct iovec *iov, int iovcnt,
                size_t offset, size_t length,
                void *buffer)
{
    size_t copied;

    if (input_size < offset) {
        return false;
    }

    copied = iov_to_buf(iov, iovcnt, offset, buffer, length);

    if (copied < length) {
        return false;
    }

    return true;
}

static bool
_eth_tcp_has_data(bool is_ip4,
                  const struct ip_header  *ip4_hdr,
                  const struct ip6_header *ip6_hdr,
                  size_t full_ip6hdr_len,
                  const struct tcp_header *tcp)
{
    uint32_t l4len;

    if (is_ip4) {
        l4len = be16_to_cpu(ip4_hdr->ip_len) - IP_HDR_GET_LEN(ip4_hdr);
    } else {
        size_t opts_len = full_ip6hdr_len - sizeof(struct ip6_header);
        l4len = be16_to_cpu(ip6_hdr->ip6_ctlun.ip6_un1.ip6_un1_plen) - opts_len;
    }

    return l4len > TCP_HEADER_DATA_OFFSET(tcp);
}

void eth_get_protocols(const struct iovec *iov, size_t iovcnt, size_t iovoff,
                       bool *hasip4, bool *hasip6,
                       size_t *l3hdr_off,
                       size_t *l4hdr_off,
                       size_t *l5hdr_off,
                       eth_ip6_hdr_info *ip6hdr_info,
                       eth_ip4_hdr_info *ip4hdr_info,
                       eth_l4_hdr_info  *l4hdr_info)
{
    int proto;
    bool fragment = false;
    size_t input_size = iov_size(iov, iovcnt);
    size_t copied;
    uint8_t ip_p;

    *hasip4 = *hasip6 = false;
    *l3hdr_off = iovoff + eth_get_l2_hdr_length_iov(iov, iovcnt, iovoff);
    l4hdr_info->proto = ETH_L4_HDR_PROTO_INVALID;

    proto = eth_get_l3_proto(iov, iovcnt, *l3hdr_off);

    if (proto == ETH_P_IP) {
        struct ip_header *iphdr = &ip4hdr_info->ip4_hdr;

        if (input_size < *l3hdr_off) {
            return;
        }

        copied = iov_to_buf(iov, iovcnt, *l3hdr_off, iphdr, sizeof(*iphdr));
        if (copied < sizeof(*iphdr) ||
            IP_HEADER_VERSION(iphdr) != IP_HEADER_VERSION_4) {
            return;
        }

        *hasip4 = true;
        ip_p = iphdr->ip_p;
        ip4hdr_info->fragment = IP4_IS_FRAGMENT(iphdr);
        *l4hdr_off = *l3hdr_off + IP_HDR_GET_LEN(iphdr);

        fragment = ip4hdr_info->fragment;
    } else if (proto == ETH_P_IPV6) {
        if (!eth_parse_ipv6_hdr(iov, iovcnt, *l3hdr_off, ip6hdr_info)) {
            return;
        }

        *hasip6 = true;
        ip_p = ip6hdr_info->l4proto;
        *l4hdr_off = *l3hdr_off + ip6hdr_info->full_hdr_len;
        fragment = ip6hdr_info->fragment;
    } else {
        return;
    }

    if (fragment) {
        return;
    }

    switch (ip_p) {
    case IP_PROTO_TCP:
        if (_eth_copy_chunk(input_size,
                            iov, iovcnt,
                            *l4hdr_off, sizeof(l4hdr_info->hdr.tcp),
                            &l4hdr_info->hdr.tcp)) {
            l4hdr_info->proto = ETH_L4_HDR_PROTO_TCP;
            *l5hdr_off = *l4hdr_off +
                TCP_HEADER_DATA_OFFSET(&l4hdr_info->hdr.tcp);

            l4hdr_info->has_tcp_data =
                _eth_tcp_has_data(proto == ETH_P_IP,
                                  &ip4hdr_info->ip4_hdr,
                                  &ip6hdr_info->ip6_hdr,
                                  *l4hdr_off - *l3hdr_off,
                                  &l4hdr_info->hdr.tcp);
        }
        break;

    case IP_PROTO_UDP:
        if (_eth_copy_chunk(input_size,
                            iov, iovcnt,
                            *l4hdr_off, sizeof(l4hdr_info->hdr.udp),
                            &l4hdr_info->hdr.udp)) {
            l4hdr_info->proto = ETH_L4_HDR_PROTO_UDP;
            *l5hdr_off = *l4hdr_off + sizeof(l4hdr_info->hdr.udp);
        }
        break;
    }
}

size_t
eth_strip_vlan(const struct iovec *iov, int iovcnt, size_t iovoff,
               uint8_t *new_ehdr_buf,
               uint16_t *payload_offset, uint16_t *tci)
{
    struct vlan_header vlan_hdr;
    struct eth_header *new_ehdr = (struct eth_header *) new_ehdr_buf;

    size_t copied = iov_to_buf(iov, iovcnt, iovoff,
                               new_ehdr, sizeof(*new_ehdr));

    if (copied < sizeof(*new_ehdr)) {
        return 0;
    }

    switch (be16_to_cpu(new_ehdr->h_proto)) {
    case ETH_P_VLAN:
    case ETH_P_DVLAN:
        copied = iov_to_buf(iov, iovcnt, iovoff + sizeof(*new_ehdr),
                            &vlan_hdr, sizeof(vlan_hdr));

        if (copied < sizeof(vlan_hdr)) {
            return 0;
        }

        new_ehdr->h_proto = vlan_hdr.h_proto;

        *tci = be16_to_cpu(vlan_hdr.h_tci);
        *payload_offset = iovoff + sizeof(*new_ehdr) + sizeof(vlan_hdr);

        if (be16_to_cpu(new_ehdr->h_proto) == ETH_P_VLAN) {

            copied = iov_to_buf(iov, iovcnt, *payload_offset,
                                PKT_GET_VLAN_HDR(new_ehdr), sizeof(vlan_hdr));

            if (copied < sizeof(vlan_hdr)) {
                return 0;
            }

            *payload_offset += sizeof(vlan_hdr);

            return sizeof(struct eth_header) + sizeof(struct vlan_header);
        } else {
            return sizeof(struct eth_header);
        }
    default:
        return 0;
    }
}

size_t
eth_strip_vlan_ex(const struct iovec *iov, int iovcnt, size_t iovoff,
                  uint16_t vet, uint8_t *new_ehdr_buf,
                  uint16_t *payload_offset, uint16_t *tci)
{
    struct vlan_header vlan_hdr;
    struct eth_header *new_ehdr = (struct eth_header *) new_ehdr_buf;

    size_t copied = iov_to_buf(iov, iovcnt, iovoff,
                               new_ehdr, sizeof(*new_ehdr));

    if (copied < sizeof(*new_ehdr)) {
        return 0;
    }

    if (be16_to_cpu(new_ehdr->h_proto) == vet) {
        copied = iov_to_buf(iov, iovcnt, iovoff + sizeof(*new_ehdr),
                            &vlan_hdr, sizeof(vlan_hdr));

        if (copied < sizeof(vlan_hdr)) {
            return 0;
        }

        new_ehdr->h_proto = vlan_hdr.h_proto;

        *tci = be16_to_cpu(vlan_hdr.h_tci);
        *payload_offset = iovoff + sizeof(*new_ehdr) + sizeof(vlan_hdr);
        return sizeof(struct eth_header);
    }

    return 0;
}

void
eth_fix_ip4_checksum(void *l3hdr, size_t l3hdr_len)
{
    struct ip_header *iphdr = (struct ip_header *) l3hdr;
    iphdr->ip_sum = 0;
    iphdr->ip_sum = cpu_to_be16(net_raw_checksum(l3hdr, l3hdr_len));
}

uint32_t
eth_calc_ip4_pseudo_hdr_csum(struct ip_header *iphdr,
                             uint16_t csl,
                             uint32_t *cso)
{
    struct ip_pseudo_header ipph;
    ipph.ip_src = iphdr->ip_src;
    ipph.ip_dst = iphdr->ip_dst;
    ipph.ip_payload = cpu_to_be16(csl);
    ipph.ip_proto = iphdr->ip_p;
    ipph.zeros = 0;
    *cso = sizeof(ipph);
    return net_checksum_add(*cso, (uint8_t *) &ipph);
}

uint32_t
eth_calc_ip6_pseudo_hdr_csum(struct ip6_header *iphdr,
                             uint16_t csl,
                             uint8_t l4_proto,
                             uint32_t *cso)
{
    struct ip6_pseudo_header ipph;
    ipph.ip6_src = iphdr->ip6_src;
    ipph.ip6_dst = iphdr->ip6_dst;
    ipph.len = cpu_to_be16(csl);
    ipph.zero[0] = 0;
    ipph.zero[1] = 0;
    ipph.zero[2] = 0;
    ipph.next_hdr = l4_proto;
    *cso = sizeof(ipph);
    return net_checksum_add(*cso, (uint8_t *)&ipph);
}

static bool
eth_is_ip6_extension_header_type(uint8_t hdr_type)
{
    switch (hdr_type) {
    case IP6_HOP_BY_HOP:
    case IP6_ROUTING:
    case IP6_FRAGMENT:
    case IP6_AUTHENTICATION:
    case IP6_DESTINATON:
    case IP6_MOBILITY:
        return true;
    default:
        return false;
    }
}

static bool
_eth_get_rss_ex_dst_addr(const struct iovec *pkt, int pkt_frags,
                        size_t ext_hdr_offset,
                        struct ip6_ext_hdr *ext_hdr,
                        struct in6_address *dst_addr)
{
    struct ip6_ext_hdr_routing rt_hdr;
    size_t input_size = iov_size(pkt, pkt_frags);
    size_t bytes_read;

    if (input_size < ext_hdr_offset + sizeof(rt_hdr) + sizeof(*dst_addr)) {
        return false;
    }

    bytes_read = iov_to_buf(pkt, pkt_frags, ext_hdr_offset,
                            &rt_hdr, sizeof(rt_hdr));
    assert(bytes_read == sizeof(rt_hdr));
    if ((rt_hdr.rtype != 2) || (rt_hdr.segleft != 1)) {
        return false;
    }
    bytes_read = iov_to_buf(pkt, pkt_frags, ext_hdr_offset + sizeof(rt_hdr),
                            dst_addr, sizeof(*dst_addr));
    assert(bytes_read == sizeof(*dst_addr));

    return true;
}

static bool
_eth_get_rss_ex_src_addr(const struct iovec *pkt, int pkt_frags,
                        size_t dsthdr_offset,
                        struct ip6_ext_hdr *ext_hdr,
                        struct in6_address *src_addr)
{
    size_t bytes_left = (ext_hdr->ip6r_len + 1) * 8 - sizeof(*ext_hdr);
    struct ip6_option_hdr opthdr;
    size_t opt_offset = dsthdr_offset + sizeof(*ext_hdr);

    while (bytes_left > sizeof(opthdr)) {
        size_t input_size = iov_size(pkt, pkt_frags);
        size_t bytes_read, optlen;

        if (input_size < opt_offset) {
            return false;
        }

        bytes_read = iov_to_buf(pkt, pkt_frags, opt_offset,
                                &opthdr, sizeof(opthdr));

        if (bytes_read != sizeof(opthdr)) {
            return false;
        }

        optlen = (opthdr.type == IP6_OPT_PAD1) ? 1
                                               : (opthdr.len + sizeof(opthdr));

        if (optlen > bytes_left) {
            return false;
        }

        if (opthdr.type == IP6_OPT_HOME) {
            size_t input_size = iov_size(pkt, pkt_frags);

            if (input_size < opt_offset + sizeof(opthdr)) {
                return false;
            }

            bytes_read = iov_to_buf(pkt, pkt_frags,
                                    opt_offset + sizeof(opthdr),
                                    src_addr, sizeof(*src_addr));

            return bytes_read == sizeof(*src_addr);
        }

        opt_offset += optlen;
        bytes_left -= optlen;
    }

    return false;
}

bool eth_parse_ipv6_hdr(const struct iovec *pkt, int pkt_frags,
                        size_t ip6hdr_off, eth_ip6_hdr_info *info)
{
    struct ip6_ext_hdr ext_hdr;
    size_t bytes_read;
    uint8_t curr_ext_hdr_type;
    size_t input_size = iov_size(pkt, pkt_frags);

    info->rss_ex_dst_valid = false;
    info->rss_ex_src_valid = false;
    info->fragment = false;

    if (input_size < ip6hdr_off) {
        return false;
    }

    bytes_read = iov_to_buf(pkt, pkt_frags, ip6hdr_off,
                            &info->ip6_hdr, sizeof(info->ip6_hdr));
    if (bytes_read < sizeof(info->ip6_hdr)) {
        return false;
    }

    info->full_hdr_len = sizeof(struct ip6_header);

    curr_ext_hdr_type = info->ip6_hdr.ip6_nxt;

    if (!eth_is_ip6_extension_header_type(curr_ext_hdr_type)) {
        info->l4proto = info->ip6_hdr.ip6_nxt;
        info->has_ext_hdrs = false;
        return true;
    }

    info->has_ext_hdrs = true;

    do {
        if (input_size < ip6hdr_off + info->full_hdr_len) {
            return false;
        }

        bytes_read = iov_to_buf(pkt, pkt_frags, ip6hdr_off + info->full_hdr_len,
                                &ext_hdr, sizeof(ext_hdr));

        if (bytes_read < sizeof(ext_hdr)) {
            return false;
        }

        if (curr_ext_hdr_type == IP6_ROUTING) {
            if (ext_hdr.ip6r_len == sizeof(struct in6_address) / 8) {
                info->rss_ex_dst_valid =
                    _eth_get_rss_ex_dst_addr(pkt, pkt_frags,
                                             ip6hdr_off + info->full_hdr_len,
                                             &ext_hdr, &info->rss_ex_dst);
            }
        } else if (curr_ext_hdr_type == IP6_DESTINATON) {
            info->rss_ex_src_valid =
                _eth_get_rss_ex_src_addr(pkt, pkt_frags,
                                         ip6hdr_off + info->full_hdr_len,
                                         &ext_hdr, &info->rss_ex_src);
        } else if (curr_ext_hdr_type == IP6_FRAGMENT) {
            info->fragment = true;
        }

        info->full_hdr_len += (ext_hdr.ip6r_len + 1) * IP6_EXT_GRANULARITY;
        curr_ext_hdr_type = ext_hdr.ip6r_nxt;
    } while (eth_is_ip6_extension_header_type(curr_ext_hdr_type));

    info->l4proto = ext_hdr.ip6r_nxt;
    return true;
}

bool eth_pad_short_frame(uint8_t *padded_pkt, size_t *padded_buflen,
                         const void *pkt, size_t pkt_size)
{
    assert(padded_buflen && *padded_buflen >= ETH_ZLEN);

    if (pkt_size >= ETH_ZLEN) {
        return false;
    }

    /* pad to minimum Ethernet frame length */
    memcpy(padded_pkt, pkt, pkt_size);
    memset(&padded_pkt[pkt_size], 0, ETH_ZLEN - pkt_size);
    *padded_buflen = ETH_ZLEN;

    return true;
}
