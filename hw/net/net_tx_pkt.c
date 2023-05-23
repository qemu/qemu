/*
 * QEMU TX packets abstractions
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
#include "qemu/crc32c.h"
#include "net/eth.h"
#include "net/checksum.h"
#include "net/tap.h"
#include "net/net.h"
#include "hw/pci/pci_device.h"
#include "net_tx_pkt.h"

enum {
    NET_TX_PKT_VHDR_FRAG = 0,
    NET_TX_PKT_L2HDR_FRAG,
    NET_TX_PKT_L3HDR_FRAG,
    NET_TX_PKT_PL_START_FRAG
};

/* TX packet private context */
struct NetTxPkt {
    struct virtio_net_hdr virt_hdr;

    struct iovec *raw;
    uint32_t raw_frags;
    uint32_t max_raw_frags;

    struct iovec *vec;

    struct {
        struct eth_header eth;
        struct vlan_header vlan[3];
    } l2_hdr;
    union {
        struct ip_header ip;
        struct ip6_header ip6;
        uint8_t octets[ETH_MAX_IP_DGRAM_LEN];
    } l3_hdr;

    uint32_t payload_len;

    uint32_t payload_frags;
    uint32_t max_payload_frags;

    uint16_t hdr_len;
    eth_pkt_types_e packet_type;
    uint8_t l4proto;
};

void net_tx_pkt_init(struct NetTxPkt **pkt, uint32_t max_frags)
{
    struct NetTxPkt *p = g_malloc0(sizeof *p);

    p->vec = g_new(struct iovec, max_frags + NET_TX_PKT_PL_START_FRAG);

    p->raw = g_new(struct iovec, max_frags);

    p->max_payload_frags = max_frags;
    p->max_raw_frags = max_frags;
    p->vec[NET_TX_PKT_VHDR_FRAG].iov_base = &p->virt_hdr;
    p->vec[NET_TX_PKT_VHDR_FRAG].iov_len = sizeof p->virt_hdr;
    p->vec[NET_TX_PKT_L2HDR_FRAG].iov_base = &p->l2_hdr;
    p->vec[NET_TX_PKT_L3HDR_FRAG].iov_base = &p->l3_hdr;

    *pkt = p;
}

void net_tx_pkt_uninit(struct NetTxPkt *pkt)
{
    if (pkt) {
        g_free(pkt->vec);
        g_free(pkt->raw);
        g_free(pkt);
    }
}

void net_tx_pkt_update_ip_hdr_checksum(struct NetTxPkt *pkt)
{
    uint16_t csum;
    assert(pkt);

    pkt->l3_hdr.ip.ip_len = cpu_to_be16(pkt->payload_len +
        pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_len);

    pkt->l3_hdr.ip.ip_sum = 0;
    csum = net_raw_checksum(pkt->l3_hdr.octets,
        pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_len);
    pkt->l3_hdr.ip.ip_sum = cpu_to_be16(csum);
}

void net_tx_pkt_update_ip_checksums(struct NetTxPkt *pkt)
{
    uint16_t csum;
    uint32_t cntr, cso;
    assert(pkt);
    uint8_t gso_type = pkt->virt_hdr.gso_type & ~VIRTIO_NET_HDR_GSO_ECN;
    void *ip_hdr = pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_base;

    if (pkt->payload_len + pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_len >
        ETH_MAX_IP_DGRAM_LEN) {
        return;
    }

    if (gso_type == VIRTIO_NET_HDR_GSO_TCPV4 ||
        gso_type == VIRTIO_NET_HDR_GSO_UDP) {
        /* Calculate IP header checksum */
        net_tx_pkt_update_ip_hdr_checksum(pkt);

        /* Calculate IP pseudo header checksum */
        cntr = eth_calc_ip4_pseudo_hdr_csum(ip_hdr, pkt->payload_len, &cso);
        csum = cpu_to_be16(~net_checksum_finish(cntr));
    } else if (gso_type == VIRTIO_NET_HDR_GSO_TCPV6) {
        /* Calculate IP pseudo header checksum */
        cntr = eth_calc_ip6_pseudo_hdr_csum(ip_hdr, pkt->payload_len,
                                            IP_PROTO_TCP, &cso);
        csum = cpu_to_be16(~net_checksum_finish(cntr));
    } else {
        return;
    }

    iov_from_buf(&pkt->vec[NET_TX_PKT_PL_START_FRAG], pkt->payload_frags,
                 pkt->virt_hdr.csum_offset, &csum, sizeof(csum));
}

bool net_tx_pkt_update_sctp_checksum(struct NetTxPkt *pkt)
{
    uint32_t csum = 0;
    struct iovec *pl_start_frag = pkt->vec + NET_TX_PKT_PL_START_FRAG;

    if (iov_from_buf(pl_start_frag, pkt->payload_frags, 8, &csum, sizeof(csum)) < sizeof(csum)) {
        return false;
    }

    csum = cpu_to_le32(iov_crc32c(0xffffffff, pl_start_frag, pkt->payload_frags));
    if (iov_from_buf(pl_start_frag, pkt->payload_frags, 8, &csum, sizeof(csum)) < sizeof(csum)) {
        return false;
    }

    return true;
}

static void net_tx_pkt_calculate_hdr_len(struct NetTxPkt *pkt)
{
    pkt->hdr_len = pkt->vec[NET_TX_PKT_L2HDR_FRAG].iov_len +
        pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_len;
}

static bool net_tx_pkt_parse_headers(struct NetTxPkt *pkt)
{
    struct iovec *l2_hdr, *l3_hdr;
    size_t bytes_read;
    size_t full_ip6hdr_len;
    uint16_t l3_proto;

    assert(pkt);

    l2_hdr = &pkt->vec[NET_TX_PKT_L2HDR_FRAG];
    l3_hdr = &pkt->vec[NET_TX_PKT_L3HDR_FRAG];

    bytes_read = iov_to_buf(pkt->raw, pkt->raw_frags, 0, l2_hdr->iov_base,
                            ETH_MAX_L2_HDR_LEN);
    if (bytes_read < sizeof(struct eth_header)) {
        l2_hdr->iov_len = 0;
        return false;
    }

    l2_hdr->iov_len = sizeof(struct eth_header);
    switch (be16_to_cpu(PKT_GET_ETH_HDR(l2_hdr->iov_base)->h_proto)) {
    case ETH_P_VLAN:
        l2_hdr->iov_len += sizeof(struct vlan_header);
        break;
    case ETH_P_DVLAN:
        l2_hdr->iov_len += 2 * sizeof(struct vlan_header);
        break;
    }

    if (bytes_read < l2_hdr->iov_len) {
        l2_hdr->iov_len = 0;
        l3_hdr->iov_len = 0;
        pkt->packet_type = ETH_PKT_UCAST;
        return false;
    } else {
        l2_hdr->iov_len = ETH_MAX_L2_HDR_LEN;
        l2_hdr->iov_len = eth_get_l2_hdr_length(l2_hdr->iov_base);
        pkt->packet_type = get_eth_packet_type(l2_hdr->iov_base);
    }

    l3_proto = eth_get_l3_proto(l2_hdr, 1, l2_hdr->iov_len);

    switch (l3_proto) {
    case ETH_P_IP:
        bytes_read = iov_to_buf(pkt->raw, pkt->raw_frags, l2_hdr->iov_len,
                                l3_hdr->iov_base, sizeof(struct ip_header));

        if (bytes_read < sizeof(struct ip_header)) {
            l3_hdr->iov_len = 0;
            return false;
        }

        l3_hdr->iov_len = IP_HDR_GET_LEN(l3_hdr->iov_base);

        if (l3_hdr->iov_len < sizeof(struct ip_header)) {
            l3_hdr->iov_len = 0;
            return false;
        }

        pkt->l4proto = IP_HDR_GET_P(l3_hdr->iov_base);

        if (IP_HDR_GET_LEN(l3_hdr->iov_base) != sizeof(struct ip_header)) {
            /* copy optional IPv4 header data if any*/
            bytes_read = iov_to_buf(pkt->raw, pkt->raw_frags,
                                    l2_hdr->iov_len + sizeof(struct ip_header),
                                    l3_hdr->iov_base + sizeof(struct ip_header),
                                    l3_hdr->iov_len - sizeof(struct ip_header));
            if (bytes_read < l3_hdr->iov_len - sizeof(struct ip_header)) {
                l3_hdr->iov_len = 0;
                return false;
            }
        }

        break;

    case ETH_P_IPV6:
    {
        eth_ip6_hdr_info hdrinfo;

        if (!eth_parse_ipv6_hdr(pkt->raw, pkt->raw_frags, l2_hdr->iov_len,
                                &hdrinfo)) {
            l3_hdr->iov_len = 0;
            return false;
        }

        pkt->l4proto = hdrinfo.l4proto;
        full_ip6hdr_len = hdrinfo.full_hdr_len;

        if (full_ip6hdr_len > ETH_MAX_IP_DGRAM_LEN) {
            l3_hdr->iov_len = 0;
            return false;
        }

        bytes_read = iov_to_buf(pkt->raw, pkt->raw_frags, l2_hdr->iov_len,
                                l3_hdr->iov_base, full_ip6hdr_len);

        if (bytes_read < full_ip6hdr_len) {
            l3_hdr->iov_len = 0;
            return false;
        } else {
            l3_hdr->iov_len = full_ip6hdr_len;
        }
        break;
    }
    default:
        l3_hdr->iov_len = 0;
        break;
    }

    net_tx_pkt_calculate_hdr_len(pkt);
    return true;
}

static void net_tx_pkt_rebuild_payload(struct NetTxPkt *pkt)
{
    pkt->payload_len = iov_size(pkt->raw, pkt->raw_frags) - pkt->hdr_len;
    pkt->payload_frags = iov_copy(&pkt->vec[NET_TX_PKT_PL_START_FRAG],
                                pkt->max_payload_frags,
                                pkt->raw, pkt->raw_frags,
                                pkt->hdr_len, pkt->payload_len);
}

bool net_tx_pkt_parse(struct NetTxPkt *pkt)
{
    if (net_tx_pkt_parse_headers(pkt)) {
        net_tx_pkt_rebuild_payload(pkt);
        return true;
    } else {
        return false;
    }
}

struct virtio_net_hdr *net_tx_pkt_get_vhdr(struct NetTxPkt *pkt)
{
    assert(pkt);
    return &pkt->virt_hdr;
}

static uint8_t net_tx_pkt_get_gso_type(struct NetTxPkt *pkt,
                                          bool tso_enable)
{
    uint8_t rc = VIRTIO_NET_HDR_GSO_NONE;
    uint16_t l3_proto;

    l3_proto = eth_get_l3_proto(&pkt->vec[NET_TX_PKT_L2HDR_FRAG], 1,
        pkt->vec[NET_TX_PKT_L2HDR_FRAG].iov_len);

    if (!tso_enable) {
        goto func_exit;
    }

    rc = eth_get_gso_type(l3_proto, pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_base,
                          pkt->l4proto);

func_exit:
    return rc;
}

bool net_tx_pkt_build_vheader(struct NetTxPkt *pkt, bool tso_enable,
    bool csum_enable, uint32_t gso_size)
{
    struct tcp_hdr l4hdr;
    size_t bytes_read;
    assert(pkt);

    /* csum has to be enabled if tso is. */
    assert(csum_enable || !tso_enable);

    pkt->virt_hdr.gso_type = net_tx_pkt_get_gso_type(pkt, tso_enable);

    switch (pkt->virt_hdr.gso_type & ~VIRTIO_NET_HDR_GSO_ECN) {
    case VIRTIO_NET_HDR_GSO_NONE:
        pkt->virt_hdr.hdr_len = 0;
        pkt->virt_hdr.gso_size = 0;
        break;

    case VIRTIO_NET_HDR_GSO_UDP:
        pkt->virt_hdr.gso_size = gso_size;
        pkt->virt_hdr.hdr_len = pkt->hdr_len + sizeof(struct udp_header);
        break;

    case VIRTIO_NET_HDR_GSO_TCPV4:
    case VIRTIO_NET_HDR_GSO_TCPV6:
        bytes_read = iov_to_buf(&pkt->vec[NET_TX_PKT_PL_START_FRAG],
                                pkt->payload_frags, 0, &l4hdr, sizeof(l4hdr));
        if (bytes_read < sizeof(l4hdr) ||
            l4hdr.th_off * sizeof(uint32_t) < sizeof(l4hdr)) {
            return false;
        }

        pkt->virt_hdr.hdr_len = pkt->hdr_len + l4hdr.th_off * sizeof(uint32_t);
        pkt->virt_hdr.gso_size = gso_size;
        break;

    default:
        g_assert_not_reached();
    }

    if (csum_enable) {
        switch (pkt->l4proto) {
        case IP_PROTO_TCP:
            if (pkt->payload_len < sizeof(struct tcp_hdr)) {
                return false;
            }
            pkt->virt_hdr.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
            pkt->virt_hdr.csum_start = pkt->hdr_len;
            pkt->virt_hdr.csum_offset = offsetof(struct tcp_hdr, th_sum);
            break;
        case IP_PROTO_UDP:
            if (pkt->payload_len < sizeof(struct udp_hdr)) {
                return false;
            }
            pkt->virt_hdr.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
            pkt->virt_hdr.csum_start = pkt->hdr_len;
            pkt->virt_hdr.csum_offset = offsetof(struct udp_hdr, uh_sum);
            break;
        default:
            break;
        }
    }

    return true;
}

void net_tx_pkt_setup_vlan_header_ex(struct NetTxPkt *pkt,
    uint16_t vlan, uint16_t vlan_ethtype)
{
    assert(pkt);

    eth_setup_vlan_headers(pkt->vec[NET_TX_PKT_L2HDR_FRAG].iov_base,
                           &pkt->vec[NET_TX_PKT_L2HDR_FRAG].iov_len,
                           vlan, vlan_ethtype);

    pkt->hdr_len += sizeof(struct vlan_header);
}

bool net_tx_pkt_add_raw_fragment(struct NetTxPkt *pkt, void *base, size_t len)
{
    struct iovec *ventry;
    assert(pkt);

    if (pkt->raw_frags >= pkt->max_raw_frags) {
        return false;
    }

    ventry = &pkt->raw[pkt->raw_frags];
    ventry->iov_base = base;
    ventry->iov_len = len;
    pkt->raw_frags++;

    return true;
}

bool net_tx_pkt_has_fragments(struct NetTxPkt *pkt)
{
    return pkt->raw_frags > 0;
}

eth_pkt_types_e net_tx_pkt_get_packet_type(struct NetTxPkt *pkt)
{
    assert(pkt);

    return pkt->packet_type;
}

size_t net_tx_pkt_get_total_len(struct NetTxPkt *pkt)
{
    assert(pkt);

    return pkt->hdr_len + pkt->payload_len;
}

void net_tx_pkt_dump(struct NetTxPkt *pkt)
{
#ifdef NET_TX_PKT_DEBUG
    assert(pkt);

    printf("TX PKT: hdr_len: %d, pkt_type: 0x%X, l2hdr_len: %lu, "
        "l3hdr_len: %lu, payload_len: %u\n", pkt->hdr_len, pkt->packet_type,
        pkt->vec[NET_TX_PKT_L2HDR_FRAG].iov_len,
        pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_len, pkt->payload_len);
#endif
}

void net_tx_pkt_reset(struct NetTxPkt *pkt,
                      NetTxPktFreeFrag callback, void *context)
{
    int i;

    /* no assert, as reset can be called before tx_pkt_init */
    if (!pkt) {
        return;
    }

    memset(&pkt->virt_hdr, 0, sizeof(pkt->virt_hdr));

    assert(pkt->vec);

    pkt->payload_len = 0;
    pkt->payload_frags = 0;

    if (pkt->max_raw_frags > 0) {
        assert(pkt->raw);
        for (i = 0; i < pkt->raw_frags; i++) {
            assert(pkt->raw[i].iov_base);
            callback(context, pkt->raw[i].iov_base, pkt->raw[i].iov_len);
        }
    }
    pkt->raw_frags = 0;

    pkt->hdr_len = 0;
    pkt->l4proto = 0;
}

void net_tx_pkt_unmap_frag_pci(void *context, void *base, size_t len)
{
    pci_dma_unmap(context, base, len, DMA_DIRECTION_TO_DEVICE, 0);
}

bool net_tx_pkt_add_raw_fragment_pci(struct NetTxPkt *pkt, PCIDevice *pci_dev,
                                     dma_addr_t pa, size_t len)
{
    dma_addr_t mapped_len = len;
    void *base = pci_dma_map(pci_dev, pa, &mapped_len, DMA_DIRECTION_TO_DEVICE);
    if (!base) {
        return false;
    }

    if (mapped_len != len || !net_tx_pkt_add_raw_fragment(pkt, base, len)) {
        net_tx_pkt_unmap_frag_pci(pci_dev, base, mapped_len);
        return false;
    }

    return true;
}

static void net_tx_pkt_do_sw_csum(struct NetTxPkt *pkt,
                                  struct iovec *iov, uint32_t iov_len,
                                  uint16_t csl)
{
    uint32_t csum_cntr;
    uint16_t csum = 0;
    uint32_t cso;
    /* num of iovec without vhdr */
    size_t csum_offset = pkt->virt_hdr.csum_start + pkt->virt_hdr.csum_offset;
    uint16_t l3_proto = eth_get_l3_proto(iov, 1, iov->iov_len);

    /* Put zero to checksum field */
    iov_from_buf(iov, iov_len, csum_offset, &csum, sizeof csum);

    /* Calculate L4 TCP/UDP checksum */
    csum_cntr = 0;
    cso = 0;
    /* add pseudo header to csum */
    if (l3_proto == ETH_P_IP) {
        csum_cntr = eth_calc_ip4_pseudo_hdr_csum(
                pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_base,
                csl, &cso);
    } else if (l3_proto == ETH_P_IPV6) {
        csum_cntr = eth_calc_ip6_pseudo_hdr_csum(
                pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_base,
                csl, pkt->l4proto, &cso);
    }

    /* data checksum */
    csum_cntr +=
        net_checksum_add_iov(iov, iov_len, pkt->virt_hdr.csum_start, csl, cso);

    /* Put the checksum obtained into the packet */
    csum = cpu_to_be16(net_checksum_finish_nozero(csum_cntr));
    iov_from_buf(iov, iov_len, csum_offset, &csum, sizeof csum);
}

#define NET_MAX_FRAG_SG_LIST (64)

static size_t net_tx_pkt_fetch_fragment(struct NetTxPkt *pkt,
    int *src_idx, size_t *src_offset, size_t src_len,
    struct iovec *dst, int *dst_idx)
{
    size_t fetched = 0;
    struct iovec *src = pkt->vec;

    while (fetched < src_len) {

        /* no more place in fragment iov */
        if (*dst_idx == NET_MAX_FRAG_SG_LIST) {
            break;
        }

        /* no more data in iovec */
        if (*src_idx == (pkt->payload_frags + NET_TX_PKT_PL_START_FRAG)) {
            break;
        }


        dst[*dst_idx].iov_base = src[*src_idx].iov_base + *src_offset;
        dst[*dst_idx].iov_len = MIN(src[*src_idx].iov_len - *src_offset,
            src_len - fetched);

        *src_offset += dst[*dst_idx].iov_len;
        fetched += dst[*dst_idx].iov_len;

        if (*src_offset == src[*src_idx].iov_len) {
            *src_offset = 0;
            (*src_idx)++;
        }

        (*dst_idx)++;
    }

    return fetched;
}

static void net_tx_pkt_sendv(
    void *opaque, const struct iovec *iov, int iov_cnt,
    const struct iovec *virt_iov, int virt_iov_cnt)
{
    NetClientState *nc = opaque;

    if (qemu_get_using_vnet_hdr(nc->peer)) {
        qemu_sendv_packet(nc, virt_iov, virt_iov_cnt);
    } else {
        qemu_sendv_packet(nc, iov, iov_cnt);
    }
}

static bool net_tx_pkt_tcp_fragment_init(struct NetTxPkt *pkt,
                                         struct iovec *fragment,
                                         int *pl_idx,
                                         size_t *l4hdr_len,
                                         int *src_idx,
                                         size_t *src_offset,
                                         size_t *src_len)
{
    struct iovec *l4 = fragment + NET_TX_PKT_PL_START_FRAG;
    size_t bytes_read = 0;
    struct tcp_hdr *th;

    if (!pkt->payload_frags) {
        return false;
    }

    l4->iov_len = pkt->virt_hdr.hdr_len - pkt->hdr_len;
    l4->iov_base = g_malloc(l4->iov_len);

    *src_idx = NET_TX_PKT_PL_START_FRAG;
    while (pkt->vec[*src_idx].iov_len < l4->iov_len - bytes_read) {
        memcpy((char *)l4->iov_base + bytes_read, pkt->vec[*src_idx].iov_base,
               pkt->vec[*src_idx].iov_len);

        bytes_read += pkt->vec[*src_idx].iov_len;

        (*src_idx)++;
        if (*src_idx >= pkt->payload_frags + NET_TX_PKT_PL_START_FRAG) {
            g_free(l4->iov_base);
            return false;
        }
    }

    *src_offset = l4->iov_len - bytes_read;
    memcpy((char *)l4->iov_base + bytes_read, pkt->vec[*src_idx].iov_base,
           *src_offset);

    th = l4->iov_base;
    th->th_flags &= ~(TH_FIN | TH_PUSH);

    *pl_idx = NET_TX_PKT_PL_START_FRAG + 1;
    *l4hdr_len = l4->iov_len;
    *src_len = pkt->virt_hdr.gso_size;

    return true;
}

static void net_tx_pkt_tcp_fragment_deinit(struct iovec *fragment)
{
    g_free(fragment[NET_TX_PKT_PL_START_FRAG].iov_base);
}

static void net_tx_pkt_tcp_fragment_fix(struct NetTxPkt *pkt,
                                        struct iovec *fragment,
                                        size_t fragment_len,
                                        uint8_t gso_type)
{
    struct iovec *l3hdr = fragment + NET_TX_PKT_L3HDR_FRAG;
    struct iovec *l4hdr = fragment + NET_TX_PKT_PL_START_FRAG;
    struct ip_header *ip = l3hdr->iov_base;
    struct ip6_header *ip6 = l3hdr->iov_base;
    size_t len = l3hdr->iov_len + l4hdr->iov_len + fragment_len;

    switch (gso_type) {
    case VIRTIO_NET_HDR_GSO_TCPV4:
        ip->ip_len = cpu_to_be16(len);
        eth_fix_ip4_checksum(l3hdr->iov_base, l3hdr->iov_len);
        break;

    case VIRTIO_NET_HDR_GSO_TCPV6:
        len -= sizeof(struct ip6_header);
        ip6->ip6_ctlun.ip6_un1.ip6_un1_plen = cpu_to_be16(len);
        break;
    }
}

static void net_tx_pkt_tcp_fragment_advance(struct NetTxPkt *pkt,
                                            struct iovec *fragment,
                                            size_t fragment_len,
                                            uint8_t gso_type)
{
    struct iovec *l3hdr = fragment + NET_TX_PKT_L3HDR_FRAG;
    struct iovec *l4hdr = fragment + NET_TX_PKT_PL_START_FRAG;
    struct ip_header *ip = l3hdr->iov_base;
    struct tcp_hdr *th = l4hdr->iov_base;

    if (gso_type == VIRTIO_NET_HDR_GSO_TCPV4) {
        ip->ip_id = cpu_to_be16(be16_to_cpu(ip->ip_id) + 1);
    }

    th->th_seq = cpu_to_be32(be32_to_cpu(th->th_seq) + fragment_len);
    th->th_flags &= ~TH_CWR;
}

static void net_tx_pkt_udp_fragment_init(struct NetTxPkt *pkt,
                                         int *pl_idx,
                                         size_t *l4hdr_len,
                                         int *src_idx, size_t *src_offset,
                                         size_t *src_len)
{
    *pl_idx = NET_TX_PKT_PL_START_FRAG;
    *l4hdr_len = 0;
    *src_idx = NET_TX_PKT_PL_START_FRAG;
    *src_offset = 0;
    *src_len = IP_FRAG_ALIGN_SIZE(pkt->virt_hdr.gso_size);
}

static void net_tx_pkt_udp_fragment_fix(struct NetTxPkt *pkt,
                                        struct iovec *fragment,
                                        size_t fragment_offset,
                                        size_t fragment_len)
{
    bool more_frags = fragment_offset + fragment_len < pkt->payload_len;
    uint16_t orig_flags;
    struct iovec *l3hdr = fragment + NET_TX_PKT_L3HDR_FRAG;
    struct ip_header *ip = l3hdr->iov_base;
    uint16_t frag_off_units = fragment_offset / IP_FRAG_UNIT_SIZE;
    uint16_t new_ip_off;

    assert(fragment_offset % IP_FRAG_UNIT_SIZE == 0);
    assert((frag_off_units & ~IP_OFFMASK) == 0);

    orig_flags = be16_to_cpu(ip->ip_off) & ~(IP_OFFMASK | IP_MF);
    new_ip_off = frag_off_units | orig_flags | (more_frags ? IP_MF : 0);
    ip->ip_off = cpu_to_be16(new_ip_off);
    ip->ip_len = cpu_to_be16(l3hdr->iov_len + fragment_len);

    eth_fix_ip4_checksum(l3hdr->iov_base, l3hdr->iov_len);
}

static bool net_tx_pkt_do_sw_fragmentation(struct NetTxPkt *pkt,
                                           NetTxPktSend callback,
                                           void *context)
{
    uint8_t gso_type = pkt->virt_hdr.gso_type & ~VIRTIO_NET_HDR_GSO_ECN;

    struct iovec fragment[NET_MAX_FRAG_SG_LIST];
    size_t fragment_len;
    size_t l4hdr_len;
    size_t src_len;

    int src_idx, dst_idx, pl_idx;
    size_t src_offset;
    size_t fragment_offset = 0;
    struct virtio_net_hdr virt_hdr = {
        .flags = pkt->virt_hdr.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM ?
                 VIRTIO_NET_HDR_F_DATA_VALID : 0
    };

    /* Copy headers */
    fragment[NET_TX_PKT_VHDR_FRAG].iov_base = &virt_hdr;
    fragment[NET_TX_PKT_VHDR_FRAG].iov_len = sizeof(virt_hdr);
    fragment[NET_TX_PKT_L2HDR_FRAG] = pkt->vec[NET_TX_PKT_L2HDR_FRAG];
    fragment[NET_TX_PKT_L3HDR_FRAG] = pkt->vec[NET_TX_PKT_L3HDR_FRAG];

    switch (gso_type) {
    case VIRTIO_NET_HDR_GSO_TCPV4:
    case VIRTIO_NET_HDR_GSO_TCPV6:
        if (!net_tx_pkt_tcp_fragment_init(pkt, fragment, &pl_idx, &l4hdr_len,
                                          &src_idx, &src_offset, &src_len)) {
            return false;
        }
        break;

    case VIRTIO_NET_HDR_GSO_UDP:
        net_tx_pkt_do_sw_csum(pkt, &pkt->vec[NET_TX_PKT_L2HDR_FRAG],
                              pkt->payload_frags + NET_TX_PKT_PL_START_FRAG - 1,
                              pkt->payload_len);
        net_tx_pkt_udp_fragment_init(pkt, &pl_idx, &l4hdr_len,
                                     &src_idx, &src_offset, &src_len);
        break;

    default:
        abort();
    }

    /* Put as much data as possible and send */
    while (true) {
        dst_idx = pl_idx;
        fragment_len = net_tx_pkt_fetch_fragment(pkt,
            &src_idx, &src_offset, src_len, fragment, &dst_idx);
        if (!fragment_len) {
            break;
        }

        switch (gso_type) {
        case VIRTIO_NET_HDR_GSO_TCPV4:
        case VIRTIO_NET_HDR_GSO_TCPV6:
            net_tx_pkt_tcp_fragment_fix(pkt, fragment, fragment_len, gso_type);
            net_tx_pkt_do_sw_csum(pkt, fragment + NET_TX_PKT_L2HDR_FRAG,
                                  dst_idx - NET_TX_PKT_L2HDR_FRAG,
                                  l4hdr_len + fragment_len);
            break;

        case VIRTIO_NET_HDR_GSO_UDP:
            net_tx_pkt_udp_fragment_fix(pkt, fragment, fragment_offset,
                                        fragment_len);
            break;
        }

        callback(context,
                 fragment + NET_TX_PKT_L2HDR_FRAG, dst_idx - NET_TX_PKT_L2HDR_FRAG,
                 fragment + NET_TX_PKT_VHDR_FRAG, dst_idx - NET_TX_PKT_VHDR_FRAG);

        if (gso_type == VIRTIO_NET_HDR_GSO_TCPV4 ||
            gso_type == VIRTIO_NET_HDR_GSO_TCPV6) {
            net_tx_pkt_tcp_fragment_advance(pkt, fragment, fragment_len,
                                            gso_type);
        }

        fragment_offset += fragment_len;
    }

    if (gso_type == VIRTIO_NET_HDR_GSO_TCPV4 ||
        gso_type == VIRTIO_NET_HDR_GSO_TCPV6) {
        net_tx_pkt_tcp_fragment_deinit(fragment);
    }

    return true;
}

bool net_tx_pkt_send(struct NetTxPkt *pkt, NetClientState *nc)
{
    bool offload = qemu_get_using_vnet_hdr(nc->peer);
    return net_tx_pkt_send_custom(pkt, offload, net_tx_pkt_sendv, nc);
}

bool net_tx_pkt_send_custom(struct NetTxPkt *pkt, bool offload,
                            NetTxPktSend callback, void *context)
{
    assert(pkt);

    uint8_t gso_type = pkt->virt_hdr.gso_type & ~VIRTIO_NET_HDR_GSO_ECN;

    /*
     * Since underlying infrastructure does not support IP datagrams longer
     * than 64K we should drop such packets and don't even try to send
     */
    if (VIRTIO_NET_HDR_GSO_NONE != gso_type) {
        if (pkt->payload_len >
            ETH_MAX_IP_DGRAM_LEN -
            pkt->vec[NET_TX_PKT_L3HDR_FRAG].iov_len) {
            return false;
        }
    }

    if (offload || gso_type == VIRTIO_NET_HDR_GSO_NONE) {
        if (!offload && pkt->virt_hdr.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
            net_tx_pkt_do_sw_csum(pkt, &pkt->vec[NET_TX_PKT_L2HDR_FRAG],
                                  pkt->payload_frags + NET_TX_PKT_PL_START_FRAG - 1,
                                  pkt->payload_len);
        }

        net_tx_pkt_fix_ip6_payload_len(pkt);
        callback(context, pkt->vec + NET_TX_PKT_L2HDR_FRAG,
                 pkt->payload_frags + NET_TX_PKT_PL_START_FRAG - NET_TX_PKT_L2HDR_FRAG,
                 pkt->vec + NET_TX_PKT_VHDR_FRAG,
                 pkt->payload_frags + NET_TX_PKT_PL_START_FRAG - NET_TX_PKT_VHDR_FRAG);
        return true;
    }

    return net_tx_pkt_do_sw_fragmentation(pkt, callback, context);
}

void net_tx_pkt_fix_ip6_payload_len(struct NetTxPkt *pkt)
{
    struct iovec *l2 = &pkt->vec[NET_TX_PKT_L2HDR_FRAG];
    if (eth_get_l3_proto(l2, 1, l2->iov_len) == ETH_P_IPV6) {
        /*
         * TODO: if qemu would support >64K packets - add jumbo option check
         * something like that:
         * 'if (ip6->ip6_plen == 0 && !has_jumbo_option(ip6)) {'
         */
        if (pkt->l3_hdr.ip6.ip6_plen == 0) {
            if (pkt->payload_len <= ETH_MAX_IP_DGRAM_LEN) {
                pkt->l3_hdr.ip6.ip6_plen = htons(pkt->payload_len);
            }
            /*
             * TODO: if qemu would support >64K packets
             * add jumbo option for packets greater then 65,535 bytes
             */
        }
    }
}
