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
#include "net_rx_pkt.h"
#include "net/eth.h"
#include "qemu-common.h"
#include "qemu/iov.h"
#include "net/checksum.h"
#include "net/tap.h"

/*
 * RX packet may contain up to 2 fragments - rebuilt eth header
 * in case of VLAN tag stripping
 * and payload received from QEMU - in any case
 */
#define NET_MAX_RX_PACKET_FRAGMENTS (2)

struct NetRxPkt {
    struct virtio_net_hdr virt_hdr;
    uint8_t ehdr_buf[ETH_MAX_L2_HDR_LEN];
    struct iovec vec[NET_MAX_RX_PACKET_FRAGMENTS];
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
};

void net_rx_pkt_init(struct NetRxPkt **pkt, bool has_virt_hdr)
{
    struct NetRxPkt *p = g_malloc0(sizeof *p);
    p->has_virt_hdr = has_virt_hdr;
    *pkt = p;
}

void net_rx_pkt_uninit(struct NetRxPkt *pkt)
{
    g_free(pkt);
}

struct virtio_net_hdr *net_rx_pkt_get_vhdr(struct NetRxPkt *pkt)
{
    assert(pkt);
    return &pkt->virt_hdr;
}

void net_rx_pkt_attach_data(struct NetRxPkt *pkt, const void *data,
                               size_t len, bool strip_vlan)
{
    uint16_t tci = 0;
    uint16_t ploff;
    assert(pkt);
    pkt->vlan_stripped = false;

    if (strip_vlan) {
        pkt->vlan_stripped = eth_strip_vlan(data, pkt->ehdr_buf, &ploff, &tci);
    }

    if (pkt->vlan_stripped) {
        pkt->vec[0].iov_base = pkt->ehdr_buf;
        pkt->vec[0].iov_len = ploff - sizeof(struct vlan_header);
        pkt->vec[1].iov_base = (uint8_t *) data + ploff;
        pkt->vec[1].iov_len = len - ploff;
        pkt->vec_len = 2;
        pkt->tot_len = len - ploff + sizeof(struct eth_header);
    } else {
        pkt->vec[0].iov_base = (void *)data;
        pkt->vec[0].iov_len = len;
        pkt->vec_len = 1;
        pkt->tot_len = len;
    }

    pkt->tci = tci;
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
    assert(pkt);

    eth_get_protocols(data, len, &pkt->isip4, &pkt->isip6,
        &pkt->isudp, &pkt->istcp);
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

struct iovec *net_rx_pkt_get_iovec(struct NetRxPkt *pkt)
{
    assert(pkt);

    return pkt->vec;
}

void net_rx_pkt_set_vhdr(struct NetRxPkt *pkt,
                            struct virtio_net_hdr *vhdr)
{
    assert(pkt);

    memcpy(&pkt->virt_hdr, vhdr, sizeof pkt->virt_hdr);
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
