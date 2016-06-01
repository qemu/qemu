/*
 * QEMU RX packets abstractions
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
#include "trace.h"
#include "net_rx_pkt.h"
#include "net/checksum.h"
#include "net/tap.h"

struct NetRxPkt {
    struct virtio_net_hdr virt_hdr;
    uint8_t ehdr_buf[sizeof(struct eth_header)];
    struct iovec *vec;
    uint16_t vec_len_total;
    uint16_t vec_len;
    uint32_t tot_len;
    uint16_t tci;
    bool vlan_stripped;
    bool has_virt_hdr;
    eth_pkt_types_e packet_type;

    /* Analysis results */
    bool isip4;
    bool isip6;
    bool isudp;
    bool istcp;

    size_t l3hdr_off;
    size_t l4hdr_off;
    size_t l5hdr_off;

    eth_ip6_hdr_info ip6hdr_info;
    eth_ip4_hdr_info ip4hdr_info;
    eth_l4_hdr_info  l4hdr_info;
};

void net_rx_pkt_init(struct NetRxPkt **pkt, bool has_virt_hdr)
{
    struct NetRxPkt *p = g_malloc0(sizeof *p);
    p->has_virt_hdr = has_virt_hdr;
    p->vec = NULL;
    p->vec_len_total = 0;
    *pkt = p;
}

void net_rx_pkt_uninit(struct NetRxPkt *pkt)
{
    if (pkt->vec_len_total != 0) {
        g_free(pkt->vec);
    }

    g_free(pkt);
}

struct virtio_net_hdr *net_rx_pkt_get_vhdr(struct NetRxPkt *pkt)
{
    assert(pkt);
    return &pkt->virt_hdr;
}

static inline void
net_rx_pkt_iovec_realloc(struct NetRxPkt *pkt,
                            int new_iov_len)
{
    if (pkt->vec_len_total < new_iov_len) {
        g_free(pkt->vec);
        pkt->vec = g_malloc(sizeof(*pkt->vec) * new_iov_len);
        pkt->vec_len_total = new_iov_len;
    }
}

static void
net_rx_pkt_pull_data(struct NetRxPkt *pkt,
                        const struct iovec *iov, int iovcnt,
                        size_t ploff)
{
    if (pkt->vlan_stripped) {
        net_rx_pkt_iovec_realloc(pkt, iovcnt + 1);

        pkt->vec[0].iov_base = pkt->ehdr_buf;
        pkt->vec[0].iov_len = sizeof(pkt->ehdr_buf);

        pkt->tot_len =
            iov_size(iov, iovcnt) - ploff + sizeof(struct eth_header);

        pkt->vec_len = iov_copy(pkt->vec + 1, pkt->vec_len_total - 1,
                                iov, iovcnt, ploff, pkt->tot_len);
    } else {
        net_rx_pkt_iovec_realloc(pkt, iovcnt);

        pkt->tot_len = iov_size(iov, iovcnt) - ploff;
        pkt->vec_len = iov_copy(pkt->vec, pkt->vec_len_total,
                                iov, iovcnt, ploff, pkt->tot_len);
    }

    eth_get_protocols(pkt->vec, pkt->vec_len, &pkt->isip4, &pkt->isip6,
                      &pkt->isudp, &pkt->istcp,
                      &pkt->l3hdr_off, &pkt->l4hdr_off, &pkt->l5hdr_off,
                      &pkt->ip6hdr_info, &pkt->ip4hdr_info, &pkt->l4hdr_info);

    trace_net_rx_pkt_parsed(pkt->isip4, pkt->isip6, pkt->isudp, pkt->istcp,
                            pkt->l3hdr_off, pkt->l4hdr_off, pkt->l5hdr_off);
}

void net_rx_pkt_attach_iovec(struct NetRxPkt *pkt,
                                const struct iovec *iov, int iovcnt,
                                size_t iovoff, bool strip_vlan)
{
    uint16_t tci = 0;
    uint16_t ploff = iovoff;
    assert(pkt);
    pkt->vlan_stripped = false;

    if (strip_vlan) {
        pkt->vlan_stripped = eth_strip_vlan(iov, iovcnt, iovoff, pkt->ehdr_buf,
                                            &ploff, &tci);
    }

    pkt->tci = tci;

    net_rx_pkt_pull_data(pkt, iov, iovcnt, ploff);
}

void net_rx_pkt_attach_iovec_ex(struct NetRxPkt *pkt,
                                const struct iovec *iov, int iovcnt,
                                size_t iovoff, bool strip_vlan,
                                uint16_t vet)
{
    uint16_t tci = 0;
    uint16_t ploff = iovoff;
    assert(pkt);
    pkt->vlan_stripped = false;

    if (strip_vlan) {
        pkt->vlan_stripped = eth_strip_vlan_ex(iov, iovcnt, iovoff, vet,
                                               pkt->ehdr_buf,
                                               &ploff, &tci);
    }

    pkt->tci = tci;

    net_rx_pkt_pull_data(pkt, iov, iovcnt, ploff);
}

void net_rx_pkt_dump(struct NetRxPkt *pkt)
{
#ifdef NET_RX_PKT_DEBUG
    NetRxPkt *pkt = (NetRxPkt *)pkt;
    assert(pkt);

    printf("RX PKT: tot_len: %d, vlan_stripped: %d, vlan_tag: %d\n",
              pkt->tot_len, pkt->vlan_stripped, pkt->tci);
#endif
}

void net_rx_pkt_set_packet_type(struct NetRxPkt *pkt,
    eth_pkt_types_e packet_type)
{
    assert(pkt);

    pkt->packet_type = packet_type;

}

eth_pkt_types_e net_rx_pkt_get_packet_type(struct NetRxPkt *pkt)
{
    assert(pkt);

    return pkt->packet_type;
}

size_t net_rx_pkt_get_total_len(struct NetRxPkt *pkt)
{
    assert(pkt);

    return pkt->tot_len;
}

void net_rx_pkt_set_protocols(struct NetRxPkt *pkt, const void *data,
                              size_t len)
{
    const struct iovec iov = {
        .iov_base = (void *)data,
        .iov_len = len
    };

    assert(pkt);

    eth_get_protocols(&iov, 1, &pkt->isip4, &pkt->isip6,
                      &pkt->isudp, &pkt->istcp,
                      &pkt->l3hdr_off, &pkt->l4hdr_off, &pkt->l5hdr_off,
                      &pkt->ip6hdr_info, &pkt->ip4hdr_info, &pkt->l4hdr_info);
}

void net_rx_pkt_get_protocols(struct NetRxPkt *pkt,
                              bool *isip4, bool *isip6,
                              bool *isudp, bool *istcp)
{
    assert(pkt);

    *isip4 = pkt->isip4;
    *isip6 = pkt->isip6;
    *isudp = pkt->isudp;
    *istcp = pkt->istcp;
}

size_t net_rx_pkt_get_l3_hdr_offset(struct NetRxPkt *pkt)
{
    assert(pkt);
    return pkt->l3hdr_off;
}

size_t net_rx_pkt_get_l4_hdr_offset(struct NetRxPkt *pkt)
{
    assert(pkt);
    return pkt->l4hdr_off;
}

size_t net_rx_pkt_get_l5_hdr_offset(struct NetRxPkt *pkt)
{
    assert(pkt);
    return pkt->l5hdr_off;
}

eth_ip6_hdr_info *net_rx_pkt_get_ip6_info(struct NetRxPkt *pkt)
{
    return &pkt->ip6hdr_info;
}

eth_ip4_hdr_info *net_rx_pkt_get_ip4_info(struct NetRxPkt *pkt)
{
    return &pkt->ip4hdr_info;
}

eth_l4_hdr_info *net_rx_pkt_get_l4_info(struct NetRxPkt *pkt)
{
    return &pkt->l4hdr_info;
}

static inline void
_net_rx_rss_add_chunk(uint8_t *rss_input, size_t *bytes_written,
                      void *ptr, size_t size)
{
    memcpy(&rss_input[*bytes_written], ptr, size);
    trace_net_rx_pkt_rss_add_chunk(ptr, size, *bytes_written);
    *bytes_written += size;
}

static inline void
_net_rx_rss_prepare_ip4(uint8_t *rss_input,
                        struct NetRxPkt *pkt,
                        size_t *bytes_written)
{
    struct ip_header *ip4_hdr = &pkt->ip4hdr_info.ip4_hdr;

    _net_rx_rss_add_chunk(rss_input, bytes_written,
                          &ip4_hdr->ip_src, sizeof(uint32_t));

    _net_rx_rss_add_chunk(rss_input, bytes_written,
                          &ip4_hdr->ip_dst, sizeof(uint32_t));
}

static inline void
_net_rx_rss_prepare_ip6(uint8_t *rss_input,
                        struct NetRxPkt *pkt,
                        bool ipv6ex, size_t *bytes_written)
{
    eth_ip6_hdr_info *ip6info = &pkt->ip6hdr_info;

    _net_rx_rss_add_chunk(rss_input, bytes_written,
           (ipv6ex && ip6info->rss_ex_src_valid) ? &ip6info->rss_ex_src
                                                 : &ip6info->ip6_hdr.ip6_src,
           sizeof(struct in6_address));

    _net_rx_rss_add_chunk(rss_input, bytes_written,
           (ipv6ex && ip6info->rss_ex_dst_valid) ? &ip6info->rss_ex_dst
                                                 : &ip6info->ip6_hdr.ip6_dst,
           sizeof(struct in6_address));
}

static inline void
_net_rx_rss_prepare_tcp(uint8_t *rss_input,
                        struct NetRxPkt *pkt,
                        size_t *bytes_written)
{
    struct tcp_header *tcphdr = &pkt->l4hdr_info.hdr.tcp;

    _net_rx_rss_add_chunk(rss_input, bytes_written,
                          &tcphdr->th_sport, sizeof(uint16_t));

    _net_rx_rss_add_chunk(rss_input, bytes_written,
                          &tcphdr->th_dport, sizeof(uint16_t));
}

uint32_t
net_rx_pkt_calc_rss_hash(struct NetRxPkt *pkt,
                         NetRxPktRssType type,
                         uint8_t *key)
{
    uint8_t rss_input[36];
    size_t rss_length = 0;
    uint32_t rss_hash = 0;
    net_toeplitz_key key_data;

    switch (type) {
    case NetPktRssIpV4:
        assert(pkt->isip4);
        trace_net_rx_pkt_rss_ip4();
        _net_rx_rss_prepare_ip4(&rss_input[0], pkt, &rss_length);
        break;
    case NetPktRssIpV4Tcp:
        assert(pkt->isip4);
        assert(pkt->istcp);
        trace_net_rx_pkt_rss_ip4_tcp();
        _net_rx_rss_prepare_ip4(&rss_input[0], pkt, &rss_length);
        _net_rx_rss_prepare_tcp(&rss_input[0], pkt, &rss_length);
        break;
    case NetPktRssIpV6Tcp:
        assert(pkt->isip6);
        assert(pkt->istcp);
        trace_net_rx_pkt_rss_ip6_tcp();
        _net_rx_rss_prepare_ip6(&rss_input[0], pkt, true, &rss_length);
        _net_rx_rss_prepare_tcp(&rss_input[0], pkt, &rss_length);
        break;
    case NetPktRssIpV6:
        assert(pkt->isip6);
        trace_net_rx_pkt_rss_ip6();
        _net_rx_rss_prepare_ip6(&rss_input[0], pkt, false, &rss_length);
        break;
    case NetPktRssIpV6Ex:
        assert(pkt->isip6);
        trace_net_rx_pkt_rss_ip6_ex();
        _net_rx_rss_prepare_ip6(&rss_input[0], pkt, true, &rss_length);
        break;
    default:
        assert(false);
        break;
    }

    net_toeplitz_key_init(&key_data, key);
    net_toeplitz_add(&rss_hash, rss_input, rss_length, &key_data);

    trace_net_rx_pkt_rss_hash(rss_length, rss_hash);

    return rss_hash;
}

uint16_t net_rx_pkt_get_ip_id(struct NetRxPkt *pkt)
{
    assert(pkt);

    if (pkt->isip4) {
        return be16_to_cpu(pkt->ip4hdr_info.ip4_hdr.ip_id);
    }

    return 0;
}

bool net_rx_pkt_is_tcp_ack(struct NetRxPkt *pkt)
{
    assert(pkt);

    if (pkt->istcp) {
        return TCP_HEADER_FLAGS(&pkt->l4hdr_info.hdr.tcp) & TCP_FLAG_ACK;
    }

    return false;
}

bool net_rx_pkt_has_tcp_data(struct NetRxPkt *pkt)
{
    assert(pkt);

    if (pkt->istcp) {
        return pkt->l4hdr_info.has_tcp_data;
    }

    return false;
}

struct iovec *net_rx_pkt_get_iovec(struct NetRxPkt *pkt)
{
    assert(pkt);

    return pkt->vec;
}

uint16_t net_rx_pkt_get_iovec_len(struct NetRxPkt *pkt)
{
    assert(pkt);

    return pkt->vec_len;
}

void net_rx_pkt_set_vhdr(struct NetRxPkt *pkt,
                            struct virtio_net_hdr *vhdr)
{
    assert(pkt);

    memcpy(&pkt->virt_hdr, vhdr, sizeof pkt->virt_hdr);
}

void net_rx_pkt_set_vhdr_iovec(struct NetRxPkt *pkt,
    const struct iovec *iov, int iovcnt)
{
    assert(pkt);

    iov_to_buf(iov, iovcnt, 0, &pkt->virt_hdr, sizeof pkt->virt_hdr);
}

bool net_rx_pkt_is_vlan_stripped(struct NetRxPkt *pkt)
{
    assert(pkt);

    return pkt->vlan_stripped;
}

bool net_rx_pkt_has_virt_hdr(struct NetRxPkt *pkt)
{
    assert(pkt);

    return pkt->has_virt_hdr;
}

uint16_t net_rx_pkt_get_vlan_tag(struct NetRxPkt *pkt)
{
    assert(pkt);

    return pkt->tci;
}

bool net_rx_pkt_validate_l3_csum(struct NetRxPkt *pkt, bool *csum_valid)
{
    uint32_t cntr;
    uint16_t csum;
    uint32_t csl;

    trace_net_rx_pkt_l3_csum_validate_entry();

    if (!pkt->isip4) {
        trace_net_rx_pkt_l3_csum_validate_not_ip4();
        return false;
    }

    csl = pkt->l4hdr_off - pkt->l3hdr_off;

    cntr = net_checksum_add_iov(pkt->vec, pkt->vec_len,
                                pkt->l3hdr_off,
                                csl, 0);

    csum = net_checksum_finish(cntr);

    *csum_valid = (csum == 0);

    trace_net_rx_pkt_l3_csum_validate_csum(pkt->l3hdr_off, csl,
                                           cntr, csum, *csum_valid);

    return true;
}

static uint16_t
_net_rx_pkt_calc_l4_csum(struct NetRxPkt *pkt)
{
    uint32_t cntr;
    uint16_t csum;
    uint16_t csl;
    uint32_t cso;

    trace_net_rx_pkt_l4_csum_calc_entry();

    if (pkt->isip4) {
        if (pkt->isudp) {
            csl = be16_to_cpu(pkt->l4hdr_info.hdr.udp.uh_ulen);
            trace_net_rx_pkt_l4_csum_calc_ip4_udp();
        } else {
            csl = be16_to_cpu(pkt->ip4hdr_info.ip4_hdr.ip_len) -
                  IP_HDR_GET_LEN(&pkt->ip4hdr_info.ip4_hdr);
            trace_net_rx_pkt_l4_csum_calc_ip4_tcp();
        }

        cntr = eth_calc_ip4_pseudo_hdr_csum(&pkt->ip4hdr_info.ip4_hdr,
                                            csl, &cso);
        trace_net_rx_pkt_l4_csum_calc_ph_csum(cntr, csl);
    } else {
        if (pkt->isudp) {
            csl = be16_to_cpu(pkt->l4hdr_info.hdr.udp.uh_ulen);
            trace_net_rx_pkt_l4_csum_calc_ip6_udp();
        } else {
            struct ip6_header *ip6hdr = &pkt->ip6hdr_info.ip6_hdr;
            size_t full_ip6hdr_len = pkt->l4hdr_off - pkt->l3hdr_off;
            size_t ip6opts_len = full_ip6hdr_len - sizeof(struct ip6_header);

            csl = be16_to_cpu(ip6hdr->ip6_ctlun.ip6_un1.ip6_un1_plen) -
                  ip6opts_len;
            trace_net_rx_pkt_l4_csum_calc_ip6_tcp();
        }

        cntr = eth_calc_ip6_pseudo_hdr_csum(&pkt->ip6hdr_info.ip6_hdr, csl,
                                            pkt->ip6hdr_info.l4proto, &cso);
        trace_net_rx_pkt_l4_csum_calc_ph_csum(cntr, csl);
    }

    cntr += net_checksum_add_iov(pkt->vec, pkt->vec_len,
                                 pkt->l4hdr_off, csl, cso);

    csum = net_checksum_finish(cntr);

    trace_net_rx_pkt_l4_csum_calc_csum(pkt->l4hdr_off, csl, cntr, csum);

    return csum;
}

bool net_rx_pkt_validate_l4_csum(struct NetRxPkt *pkt, bool *csum_valid)
{
    uint16_t csum;

    trace_net_rx_pkt_l4_csum_validate_entry();

    if (!pkt->istcp && !pkt->isudp) {
        trace_net_rx_pkt_l4_csum_validate_not_xxp();
        return false;
    }

    if (pkt->isudp && (pkt->l4hdr_info.hdr.udp.uh_sum == 0)) {
        trace_net_rx_pkt_l4_csum_validate_udp_with_no_checksum();
        return false;
    }

    if (pkt->isip4 && pkt->ip4hdr_info.fragment) {
        trace_net_rx_pkt_l4_csum_validate_ip4_fragment();
        return false;
    }

    csum = _net_rx_pkt_calc_l4_csum(pkt);

    *csum_valid = ((csum == 0) || (csum == 0xFFFF));

    trace_net_rx_pkt_l4_csum_validate_csum(*csum_valid);

    return true;
}

bool net_rx_pkt_fix_l4_csum(struct NetRxPkt *pkt)
{
    uint16_t csum = 0;
    uint32_t l4_cso;

    trace_net_rx_pkt_l4_csum_fix_entry();

    if (pkt->istcp) {
        l4_cso = offsetof(struct tcp_header, th_sum);
        trace_net_rx_pkt_l4_csum_fix_tcp(l4_cso);
    } else if (pkt->isudp) {
        if (pkt->l4hdr_info.hdr.udp.uh_sum == 0) {
            trace_net_rx_pkt_l4_csum_fix_udp_with_no_checksum();
            return false;
        }
        l4_cso = offsetof(struct udp_header, uh_sum);
        trace_net_rx_pkt_l4_csum_fix_udp(l4_cso);
    } else {
        trace_net_rx_pkt_l4_csum_fix_not_xxp();
        return false;
    }

    if (pkt->isip4 && pkt->ip4hdr_info.fragment) {
            trace_net_rx_pkt_l4_csum_fix_ip4_fragment();
            return false;
    }

    /* Set zero to checksum word */
    iov_from_buf(pkt->vec, pkt->vec_len,
                 pkt->l4hdr_off + l4_cso,
                 &csum, sizeof(csum));

    /* Calculate L4 checksum */
    csum = cpu_to_be16(_net_rx_pkt_calc_l4_csum(pkt));

    /* Set calculated checksum to checksum word */
    iov_from_buf(pkt->vec, pkt->vec_len,
                 pkt->l4hdr_off + l4_cso,
                 &csum, sizeof(csum));

    trace_net_rx_pkt_l4_csum_fix_csum(pkt->l4hdr_off + l4_cso, csum);

    return true;
}
