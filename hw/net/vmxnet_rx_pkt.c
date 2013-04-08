/*
 * QEMU VMWARE VMXNET* paravirtual NICs - RX packets abstractions
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

#include "vmxnet_rx_pkt.h"
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
#define VMXNET_MAX_RX_PACKET_FRAGMENTS (2)

struct VmxnetRxPkt {
    struct virtio_net_hdr virt_hdr;
    uint8_t ehdr_buf[ETH_MAX_L2_HDR_LEN];
    struct iovec vec[VMXNET_MAX_RX_PACKET_FRAGMENTS];
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

void vmxnet_rx_pkt_init(struct VmxnetRxPkt **pkt, bool has_virt_hdr)
{
    struct VmxnetRxPkt *p = g_malloc0(sizeof *p);
    p->has_virt_hdr = has_virt_hdr;
    *pkt = p;
}

void vmxnet_rx_pkt_uninit(struct VmxnetRxPkt *pkt)
{
    g_free(pkt);
}

struct virtio_net_hdr *vmxnet_rx_pkt_get_vhdr(struct VmxnetRxPkt *pkt)
{
    assert(pkt);
    return &pkt->virt_hdr;
}

void vmxnet_rx_pkt_attach_data(struct VmxnetRxPkt *pkt, const void *data,
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

    eth_get_protocols(data, len, &pkt->isip4, &pkt->isip6,
        &pkt->isudp, &pkt->istcp);
}

void vmxnet_rx_pkt_dump(struct VmxnetRxPkt *pkt)
{
#ifdef VMXNET_RX_PKT_DEBUG
    VmxnetRxPkt *pkt = (VmxnetRxPkt *)pkt;
    assert(pkt);

    printf("RX PKT: tot_len: %d, vlan_stripped: %d, vlan_tag: %d\n",
              pkt->tot_len, pkt->vlan_stripped, pkt->tci);
#endif
}

void vmxnet_rx_pkt_set_packet_type(struct VmxnetRxPkt *pkt,
    eth_pkt_types_e packet_type)
{
    assert(pkt);

    pkt->packet_type = packet_type;

}

eth_pkt_types_e vmxnet_rx_pkt_get_packet_type(struct VmxnetRxPkt *pkt)
{
    assert(pkt);

    return pkt->packet_type;
}

size_t vmxnet_rx_pkt_get_total_len(struct VmxnetRxPkt *pkt)
{
    assert(pkt);

    return pkt->tot_len;
}

void vmxnet_rx_pkt_get_protocols(struct VmxnetRxPkt *pkt,
                                 bool *isip4, bool *isip6,
                                 bool *isudp, bool *istcp)
{
    assert(pkt);

    *isip4 = pkt->isip4;
    *isip6 = pkt->isip6;
    *isudp = pkt->isudp;
    *istcp = pkt->istcp;
}

struct iovec *vmxnet_rx_pkt_get_iovec(struct VmxnetRxPkt *pkt)
{
    assert(pkt);

    return pkt->vec;
}

void vmxnet_rx_pkt_set_vhdr(struct VmxnetRxPkt *pkt,
                            struct virtio_net_hdr *vhdr)
{
    assert(pkt);

    memcpy(&pkt->virt_hdr, vhdr, sizeof pkt->virt_hdr);
}

bool vmxnet_rx_pkt_is_vlan_stripped(struct VmxnetRxPkt *pkt)
{
    assert(pkt);

    return pkt->vlan_stripped;
}

bool vmxnet_rx_pkt_has_virt_hdr(struct VmxnetRxPkt *pkt)
{
    assert(pkt);

    return pkt->has_virt_hdr;
}

uint16_t vmxnet_rx_pkt_get_num_frags(struct VmxnetRxPkt *pkt)
{
    assert(pkt);

    return pkt->vec_len;
}

uint16_t vmxnet_rx_pkt_get_vlan_tag(struct VmxnetRxPkt *pkt)
{
    assert(pkt);

    return pkt->tci;
}
