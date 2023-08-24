/*
 * QEMU rocker switch emulation - OF-DPA flow processing support
 *
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "net/eth.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-rocker.h"
#include "qemu/iov.h"
#include "qemu/timer.h"

#include "rocker.h"
#include "rocker_hw.h"
#include "rocker_fp.h"
#include "rocker_tlv.h"
#include "rocker_world.h"
#include "rocker_desc.h"
#include "rocker_of_dpa.h"

static const MACAddr zero_mac = { .a = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
static const MACAddr ff_mac =   { .a = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };

typedef struct of_dpa {
    World *world;
    GHashTable *flow_tbl;
    GHashTable *group_tbl;
    unsigned int flow_tbl_max_size;
    unsigned int group_tbl_max_size;
} OfDpa;

/* flow_key stolen mostly from OVS
 *
 * Note: fields that compare with network packet header fields
 * are stored in network order (BE) to avoid per-packet field
 * byte-swaps.
 */

typedef struct of_dpa_flow_key {
    uint32_t in_pport;               /* ingress port */
    uint32_t tunnel_id;              /* overlay tunnel id */
    uint32_t tbl_id;                 /* table id */
    struct {
        __be16 vlan_id;              /* 0 if no VLAN */
        MACAddr src;                 /* ethernet source address */
        MACAddr dst;                 /* ethernet destination address */
        __be16 type;                 /* ethernet frame type */
    } eth;
    struct {
        uint8_t proto;               /* IP protocol or ARP opcode */
        uint8_t tos;                 /* IP ToS */
        uint8_t ttl;                 /* IP TTL/hop limit */
        uint8_t frag;                /* one of FRAG_TYPE_* */
    } ip;
    union {
        struct {
            struct {
                __be32 src;          /* IP source address */
                __be32 dst;          /* IP destination address */
            } addr;
            union {
                struct {
                    __be16 src;      /* TCP/UDP/SCTP source port */
                    __be16 dst;      /* TCP/UDP/SCTP destination port */
                    __be16 flags;    /* TCP flags */
                } tp;
                struct {
                    MACAddr sha;     /* ARP source hardware address */
                    MACAddr tha;     /* ARP target hardware address */
                } arp;
            };
        } ipv4;
        struct {
            struct {
                Ipv6Addr src;       /* IPv6 source address */
                Ipv6Addr dst;       /* IPv6 destination address */
            } addr;
            __be32 label;            /* IPv6 flow label */
            struct {
                __be16 src;          /* TCP/UDP/SCTP source port */
                __be16 dst;          /* TCP/UDP/SCTP destination port */
                __be16 flags;        /* TCP flags */
            } tp;
            struct {
                Ipv6Addr target;    /* ND target address */
                MACAddr sll;         /* ND source link layer address */
                MACAddr tll;         /* ND target link layer address */
            } nd;
        } ipv6;
    };
    int width;                       /* how many uint64_t's in key? */
} OfDpaFlowKey;

/* Width of key which includes field 'f' in u64s, rounded up */
#define FLOW_KEY_WIDTH(f) \
    DIV_ROUND_UP(offsetof(OfDpaFlowKey, f) + sizeof_field(OfDpaFlowKey, f), \
    sizeof(uint64_t))

typedef struct of_dpa_flow_action {
    uint32_t goto_tbl;
    struct {
        uint32_t group_id;
        uint32_t tun_log_lport;
        __be16 vlan_id;
    } write;
    struct {
        __be16 new_vlan_id;
        uint32_t out_pport;
        uint8_t copy_to_cpu;
        __be16 vlan_id;
    } apply;
} OfDpaFlowAction;

typedef struct of_dpa_flow {
    uint32_t lpm;
    uint32_t priority;
    uint32_t hardtime;
    uint32_t idletime;
    uint64_t cookie;
    OfDpaFlowKey key;
    OfDpaFlowKey mask;
    OfDpaFlowAction action;
    struct {
        uint64_t hits;
        int64_t install_time;
        int64_t refresh_time;
        uint64_t rx_pkts;
        uint64_t tx_pkts;
    } stats;
} OfDpaFlow;

typedef struct of_dpa_flow_pkt_fields {
    uint32_t tunnel_id;
    struct eth_header *ethhdr;
    __be16 *h_proto;
    struct vlan_header *vlanhdr;
    struct ip_header *ipv4hdr;
    struct ip6_header *ipv6hdr;
    Ipv6Addr *ipv6_src_addr;
    Ipv6Addr *ipv6_dst_addr;
} OfDpaFlowPktFields;

typedef struct of_dpa_flow_context {
    uint32_t in_pport;
    uint32_t tunnel_id;
    struct iovec *iov;
    int iovcnt;
    struct eth_header ethhdr_rewrite;
    struct vlan_header vlanhdr_rewrite;
    struct vlan_header vlanhdr;
    OfDpa *of_dpa;
    OfDpaFlowPktFields fields;
    OfDpaFlowAction action_set;
} OfDpaFlowContext;

typedef struct of_dpa_flow_match {
    OfDpaFlowKey value;
    OfDpaFlow *best;
} OfDpaFlowMatch;

typedef struct of_dpa_group {
    uint32_t id;
    union {
        struct {
            uint32_t out_pport;
            uint8_t pop_vlan;
        } l2_interface;
        struct {
            uint32_t group_id;
            MACAddr src_mac;
            MACAddr dst_mac;
            __be16 vlan_id;
        } l2_rewrite;
        struct {
            uint16_t group_count;
            uint32_t *group_ids;
        } l2_flood;
        struct {
            uint32_t group_id;
            MACAddr src_mac;
            MACAddr dst_mac;
            __be16 vlan_id;
            uint8_t ttl_check;
        } l3_unicast;
    };
} OfDpaGroup;

static int of_dpa_mask2prefix(__be32 mask)
{
    int i;
    int count = 32;

    for (i = 0; i < 32; i++) {
        if (!(ntohl(mask) & ((2 << i) - 1))) {
            count--;
        }
    }

    return count;
}

#if defined(DEBUG_ROCKER)
static void of_dpa_flow_key_dump(OfDpaFlowKey *key, OfDpaFlowKey *mask)
{
    char buf[512], *b = buf, *mac;

    b += sprintf(b, " tbl %2d", key->tbl_id);

    if (key->in_pport || (mask && mask->in_pport)) {
        b += sprintf(b, " in_pport %2d", key->in_pport);
        if (mask && mask->in_pport != 0xffffffff) {
            b += sprintf(b, "/0x%08x", key->in_pport);
        }
    }

    if (key->tunnel_id || (mask && mask->tunnel_id)) {
        b += sprintf(b, " tun %8d", key->tunnel_id);
        if (mask && mask->tunnel_id != 0xffffffff) {
            b += sprintf(b, "/0x%08x", key->tunnel_id);
        }
    }

    if (key->eth.vlan_id || (mask && mask->eth.vlan_id)) {
        b += sprintf(b, " vlan %4d", ntohs(key->eth.vlan_id));
        if (mask && mask->eth.vlan_id != 0xffff) {
            b += sprintf(b, "/0x%04x", ntohs(key->eth.vlan_id));
        }
    }

    if (memcmp(key->eth.src.a, zero_mac.a, ETH_ALEN) ||
        (mask && memcmp(mask->eth.src.a, zero_mac.a, ETH_ALEN))) {
        mac = qemu_mac_strdup_printf(key->eth.src.a);
        b += sprintf(b, " src %s", mac);
        g_free(mac);
        if (mask && memcmp(mask->eth.src.a, ff_mac.a, ETH_ALEN)) {
            mac = qemu_mac_strdup_printf(mask->eth.src.a);
            b += sprintf(b, "/%s", mac);
            g_free(mac);
        }
    }

    if (memcmp(key->eth.dst.a, zero_mac.a, ETH_ALEN) ||
        (mask && memcmp(mask->eth.dst.a, zero_mac.a, ETH_ALEN))) {
        mac = qemu_mac_strdup_printf(key->eth.dst.a);
        b += sprintf(b, " dst %s", mac);
        g_free(mac);
        if (mask && memcmp(mask->eth.dst.a, ff_mac.a, ETH_ALEN)) {
            mac = qemu_mac_strdup_printf(mask->eth.dst.a);
            b += sprintf(b, "/%s", mac);
            g_free(mac);
        }
    }

    if (key->eth.type || (mask && mask->eth.type)) {
        b += sprintf(b, " type 0x%04x", ntohs(key->eth.type));
        if (mask && mask->eth.type != 0xffff) {
            b += sprintf(b, "/0x%04x", ntohs(mask->eth.type));
        }
        switch (ntohs(key->eth.type)) {
        case 0x0800:
        case 0x86dd:
            if (key->ip.proto || (mask && mask->ip.proto)) {
                b += sprintf(b, " ip proto %2d", key->ip.proto);
                if (mask && mask->ip.proto != 0xff) {
                    b += sprintf(b, "/0x%02x", mask->ip.proto);
                }
            }
            if (key->ip.tos || (mask && mask->ip.tos)) {
                b += sprintf(b, " ip tos %2d", key->ip.tos);
                if (mask && mask->ip.tos != 0xff) {
                    b += sprintf(b, "/0x%02x", mask->ip.tos);
                }
            }
            break;
        }
        switch (ntohs(key->eth.type)) {
        case 0x0800:
            if (key->ipv4.addr.dst || (mask && mask->ipv4.addr.dst)) {
                b += sprintf(b, " dst %s",
                    inet_ntoa(*(struct in_addr *)&key->ipv4.addr.dst));
                if (mask) {
                    b += sprintf(b, "/%d",
                                 of_dpa_mask2prefix(mask->ipv4.addr.dst));
                }
            }
            break;
        }
    }

    DPRINTF("%s\n", buf);
}
#else
#define of_dpa_flow_key_dump(k, m)
#endif

static void _of_dpa_flow_match(void *key, void *value, void *user_data)
{
    OfDpaFlow *flow = value;
    OfDpaFlowMatch *match = user_data;
    uint64_t *k = (uint64_t *)&flow->key;
    uint64_t *m = (uint64_t *)&flow->mask;
    uint64_t *v = (uint64_t *)&match->value;
    int i;

    if (flow->key.tbl_id == match->value.tbl_id) {
        of_dpa_flow_key_dump(&flow->key, &flow->mask);
    }

    if (flow->key.width > match->value.width) {
        return;
    }

    for (i = 0; i < flow->key.width; i++, k++, m++, v++) {
        if ((~*k & *m & *v) | (*k & *m & ~*v)) {
            return;
        }
    }

    DPRINTF("match\n");

    if (!match->best ||
        flow->priority > match->best->priority ||
        flow->lpm > match->best->lpm) {
        match->best = flow;
    }
}

static OfDpaFlow *of_dpa_flow_match(OfDpa *of_dpa, OfDpaFlowMatch *match)
{
    DPRINTF("\nnew search\n");
    of_dpa_flow_key_dump(&match->value, NULL);

    g_hash_table_foreach(of_dpa->flow_tbl, _of_dpa_flow_match, match);

    return match->best;
}

static OfDpaFlow *of_dpa_flow_find(OfDpa *of_dpa, uint64_t cookie)
{
    return g_hash_table_lookup(of_dpa->flow_tbl, &cookie);
}

static int of_dpa_flow_add(OfDpa *of_dpa, OfDpaFlow *flow)
{
    g_hash_table_insert(of_dpa->flow_tbl, &flow->cookie, flow);

    return ROCKER_OK;
}

static void of_dpa_flow_del(OfDpa *of_dpa, OfDpaFlow *flow)
{
    g_hash_table_remove(of_dpa->flow_tbl, &flow->cookie);
}

static OfDpaFlow *of_dpa_flow_alloc(uint64_t cookie)
{
    OfDpaFlow *flow;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) / 1000;

    flow = g_new0(OfDpaFlow, 1);

    flow->cookie = cookie;
    flow->mask.tbl_id = 0xffffffff;

    flow->stats.install_time = flow->stats.refresh_time = now;

    return flow;
}

static void of_dpa_flow_pkt_hdr_reset(OfDpaFlowContext *fc)
{
    OfDpaFlowPktFields *fields = &fc->fields;

    fc->iov[0].iov_base = fields->ethhdr;
    fc->iov[0].iov_len = sizeof(struct eth_header);
    fc->iov[1].iov_base = fields->vlanhdr;
    fc->iov[1].iov_len = fields->vlanhdr ? sizeof(struct vlan_header) : 0;
}

static void of_dpa_flow_pkt_parse(OfDpaFlowContext *fc,
                                  const struct iovec *iov, int iovcnt)
{
    OfDpaFlowPktFields *fields = &fc->fields;
    size_t sofar = 0;
    int i;

    sofar += sizeof(struct eth_header);
    if (iov->iov_len < sofar) {
        DPRINTF("flow_pkt_parse underrun on eth_header\n");
        return;
    }

    fields->ethhdr = iov->iov_base;
    fields->h_proto = &fields->ethhdr->h_proto;

    if (ntohs(*fields->h_proto) == ETH_P_VLAN) {
        sofar += sizeof(struct vlan_header);
        if (iov->iov_len < sofar) {
            DPRINTF("flow_pkt_parse underrun on vlan_header\n");
            return;
        }
        fields->vlanhdr = (struct vlan_header *)(fields->ethhdr + 1);
        fields->h_proto = &fields->vlanhdr->h_proto;
    }

    switch (ntohs(*fields->h_proto)) {
    case ETH_P_IP:
        sofar += sizeof(struct ip_header);
        if (iov->iov_len < sofar) {
            DPRINTF("flow_pkt_parse underrun on ip_header\n");
            return;
        }
        fields->ipv4hdr = (struct ip_header *)(fields->h_proto + 1);
        break;
    case ETH_P_IPV6:
        sofar += sizeof(struct ip6_header);
        if (iov->iov_len < sofar) {
            DPRINTF("flow_pkt_parse underrun on ip6_header\n");
            return;
        }
        fields->ipv6hdr = (struct ip6_header *)(fields->h_proto + 1);
        break;
    }

    /* To facilitate (potential) VLAN tag insertion, Make a
     * copy of the iov and insert two new vectors at the
     * beginning for eth hdr and vlan hdr.  No data is copied,
     * just the vectors.
     */

    of_dpa_flow_pkt_hdr_reset(fc);

    fc->iov[2].iov_base = fields->h_proto + 1;
    fc->iov[2].iov_len = iov->iov_len - fc->iov[0].iov_len - fc->iov[1].iov_len;

    for (i = 1; i < iovcnt; i++) {
        fc->iov[i+2] = iov[i];
    }

    fc->iovcnt = iovcnt + 2;
}

static void of_dpa_flow_pkt_insert_vlan(OfDpaFlowContext *fc, __be16 vlan_id)
{
    OfDpaFlowPktFields *fields = &fc->fields;
    uint16_t h_proto = fields->ethhdr->h_proto;

    if (fields->vlanhdr) {
        DPRINTF("flow_pkt_insert_vlan packet already has vlan\n");
        return;
    }

    fields->ethhdr->h_proto = htons(ETH_P_VLAN);
    fields->vlanhdr = &fc->vlanhdr;
    fields->vlanhdr->h_tci = vlan_id;
    fields->vlanhdr->h_proto = h_proto;
    fields->h_proto = &fields->vlanhdr->h_proto;

    fc->iov[1].iov_base = fields->vlanhdr;
    fc->iov[1].iov_len = sizeof(struct vlan_header);
}

static void of_dpa_flow_pkt_strip_vlan(OfDpaFlowContext *fc)
{
    OfDpaFlowPktFields *fields = &fc->fields;

    if (!fields->vlanhdr) {
        return;
    }

    fc->iov[0].iov_len -= sizeof(fields->ethhdr->h_proto);
    fc->iov[1].iov_base = fields->h_proto;
    fc->iov[1].iov_len = sizeof(fields->ethhdr->h_proto);
}

static void of_dpa_flow_pkt_hdr_rewrite(OfDpaFlowContext *fc,
                                        uint8_t *src_mac, uint8_t *dst_mac,
                                        __be16 vlan_id)
{
    OfDpaFlowPktFields *fields = &fc->fields;

    if (src_mac || dst_mac) {
        memcpy(&fc->ethhdr_rewrite, fields->ethhdr, sizeof(struct eth_header));
        if (src_mac && memcmp(src_mac, zero_mac.a, ETH_ALEN)) {
            memcpy(fc->ethhdr_rewrite.h_source, src_mac, ETH_ALEN);
        }
        if (dst_mac && memcmp(dst_mac, zero_mac.a, ETH_ALEN)) {
            memcpy(fc->ethhdr_rewrite.h_dest, dst_mac, ETH_ALEN);
        }
        fc->iov[0].iov_base = &fc->ethhdr_rewrite;
    }

    if (vlan_id && fields->vlanhdr) {
        fc->vlanhdr_rewrite = fc->vlanhdr;
        fc->vlanhdr_rewrite.h_tci = vlan_id;
        fc->iov[1].iov_base = &fc->vlanhdr_rewrite;
    }
}

static void of_dpa_flow_ig_tbl(OfDpaFlowContext *fc, uint32_t tbl_id);

static void of_dpa_ig_port_build_match(OfDpaFlowContext *fc,
                                       OfDpaFlowMatch *match)
{
    match->value.tbl_id = ROCKER_OF_DPA_TABLE_ID_INGRESS_PORT;
    match->value.in_pport = fc->in_pport;
    match->value.width = FLOW_KEY_WIDTH(tbl_id);
}

static void of_dpa_ig_port_miss(OfDpaFlowContext *fc)
{
    uint32_t port;

    /* The default on miss is for packets from physical ports
     * to go to the VLAN Flow Table. There is no default rule
     * for packets from logical ports, which are dropped on miss.
     */

    if (fp_port_from_pport(fc->in_pport, &port)) {
        of_dpa_flow_ig_tbl(fc, ROCKER_OF_DPA_TABLE_ID_VLAN);
    }
}

static void of_dpa_vlan_build_match(OfDpaFlowContext *fc,
                                    OfDpaFlowMatch *match)
{
    match->value.tbl_id = ROCKER_OF_DPA_TABLE_ID_VLAN;
    match->value.in_pport = fc->in_pport;
    if (fc->fields.vlanhdr) {
        match->value.eth.vlan_id = fc->fields.vlanhdr->h_tci;
    }
    match->value.width = FLOW_KEY_WIDTH(eth.vlan_id);
}

static void of_dpa_vlan_insert(OfDpaFlowContext *fc,
                               OfDpaFlow *flow)
{
    if (flow->action.apply.new_vlan_id) {
        of_dpa_flow_pkt_insert_vlan(fc, flow->action.apply.new_vlan_id);
    }
}

static void of_dpa_term_mac_build_match(OfDpaFlowContext *fc,
                                        OfDpaFlowMatch *match)
{
    match->value.tbl_id = ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC;
    match->value.in_pport = fc->in_pport;
    match->value.eth.type = *fc->fields.h_proto;
    match->value.eth.vlan_id = fc->fields.vlanhdr->h_tci;
    memcpy(match->value.eth.dst.a, fc->fields.ethhdr->h_dest,
           sizeof(match->value.eth.dst.a));
    match->value.width = FLOW_KEY_WIDTH(eth.type);
}

static void of_dpa_term_mac_miss(OfDpaFlowContext *fc)
{
    of_dpa_flow_ig_tbl(fc, ROCKER_OF_DPA_TABLE_ID_BRIDGING);
}

static void of_dpa_apply_actions(OfDpaFlowContext *fc,
                                 OfDpaFlow *flow)
{
    fc->action_set.apply.copy_to_cpu = flow->action.apply.copy_to_cpu;
    fc->action_set.apply.vlan_id = flow->key.eth.vlan_id;
}

static void of_dpa_bridging_build_match(OfDpaFlowContext *fc,
                                        OfDpaFlowMatch *match)
{
    match->value.tbl_id = ROCKER_OF_DPA_TABLE_ID_BRIDGING;
    if (fc->fields.vlanhdr) {
        match->value.eth.vlan_id = fc->fields.vlanhdr->h_tci;
    } else if (fc->tunnel_id) {
        match->value.tunnel_id = fc->tunnel_id;
    }
    memcpy(match->value.eth.dst.a, fc->fields.ethhdr->h_dest,
           sizeof(match->value.eth.dst.a));
    match->value.width = FLOW_KEY_WIDTH(eth.dst);
}

static void of_dpa_bridging_learn(OfDpaFlowContext *fc,
                                  OfDpaFlow *dst_flow)
{
    OfDpaFlowMatch match = { { 0, }, };
    OfDpaFlow *flow;
    uint8_t *addr;
    uint16_t vlan_id;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) / 1000;
    int64_t refresh_delay = 1;

    /* Do a lookup in bridge table by src_mac/vlan */

    addr = fc->fields.ethhdr->h_source;
    vlan_id = fc->fields.vlanhdr->h_tci;

    match.value.tbl_id = ROCKER_OF_DPA_TABLE_ID_BRIDGING;
    match.value.eth.vlan_id = vlan_id;
    memcpy(match.value.eth.dst.a, addr, sizeof(match.value.eth.dst.a));
    match.value.width = FLOW_KEY_WIDTH(eth.dst);

    flow = of_dpa_flow_match(fc->of_dpa, &match);
    if (flow) {
        if (!memcmp(flow->mask.eth.dst.a, ff_mac.a,
                    sizeof(flow->mask.eth.dst.a))) {
            /* src_mac/vlan already learned; if in_port and out_port
             * don't match, the end station has moved and the port
             * needs updating */
            /* XXX implement the in_port/out_port check */
            if (now - flow->stats.refresh_time < refresh_delay) {
                return;
            }
            flow->stats.refresh_time = now;
        }
    }

    /* Let driver know about mac/vlan.  This may be a new mac/vlan
     * or a refresh of existing mac/vlan that's been hit after the
     * refresh_delay.
     */

    rocker_event_mac_vlan_seen(world_rocker(fc->of_dpa->world),
                               fc->in_pport, addr, vlan_id);
}

static void of_dpa_bridging_miss(OfDpaFlowContext *fc)
{
    of_dpa_bridging_learn(fc, NULL);
    of_dpa_flow_ig_tbl(fc, ROCKER_OF_DPA_TABLE_ID_ACL_POLICY);
}

static void of_dpa_bridging_action_write(OfDpaFlowContext *fc,
                                         OfDpaFlow *flow)
{
    if (flow->action.write.group_id != ROCKER_GROUP_NONE) {
        fc->action_set.write.group_id = flow->action.write.group_id;
    }
    fc->action_set.write.tun_log_lport = flow->action.write.tun_log_lport;
}

static void of_dpa_unicast_routing_build_match(OfDpaFlowContext *fc,
                                               OfDpaFlowMatch *match)
{
    match->value.tbl_id = ROCKER_OF_DPA_TABLE_ID_UNICAST_ROUTING;
    match->value.eth.type = *fc->fields.h_proto;
    if (fc->fields.ipv4hdr) {
        match->value.ipv4.addr.dst = fc->fields.ipv4hdr->ip_dst;
    }
    if (fc->fields.ipv6_dst_addr) {
        memcpy(&match->value.ipv6.addr.dst, fc->fields.ipv6_dst_addr,
               sizeof(match->value.ipv6.addr.dst));
    }
    match->value.width = FLOW_KEY_WIDTH(ipv6.addr.dst);
}

static void of_dpa_unicast_routing_miss(OfDpaFlowContext *fc)
{
    of_dpa_flow_ig_tbl(fc, ROCKER_OF_DPA_TABLE_ID_ACL_POLICY);
}

static void of_dpa_unicast_routing_action_write(OfDpaFlowContext *fc,
                                                OfDpaFlow *flow)
{
    if (flow->action.write.group_id != ROCKER_GROUP_NONE) {
        fc->action_set.write.group_id = flow->action.write.group_id;
    }
}

static void
of_dpa_multicast_routing_build_match(OfDpaFlowContext *fc,
                                     OfDpaFlowMatch *match)
{
    match->value.tbl_id = ROCKER_OF_DPA_TABLE_ID_MULTICAST_ROUTING;
    match->value.eth.type = *fc->fields.h_proto;
    match->value.eth.vlan_id = fc->fields.vlanhdr->h_tci;
    if (fc->fields.ipv4hdr) {
        match->value.ipv4.addr.src = fc->fields.ipv4hdr->ip_src;
        match->value.ipv4.addr.dst = fc->fields.ipv4hdr->ip_dst;
    }
    if (fc->fields.ipv6_src_addr) {
        memcpy(&match->value.ipv6.addr.src, fc->fields.ipv6_src_addr,
               sizeof(match->value.ipv6.addr.src));
    }
    if (fc->fields.ipv6_dst_addr) {
        memcpy(&match->value.ipv6.addr.dst, fc->fields.ipv6_dst_addr,
               sizeof(match->value.ipv6.addr.dst));
    }
    match->value.width = FLOW_KEY_WIDTH(ipv6.addr.dst);
}

static void of_dpa_multicast_routing_miss(OfDpaFlowContext *fc)
{
    of_dpa_flow_ig_tbl(fc, ROCKER_OF_DPA_TABLE_ID_ACL_POLICY);
}

static void
of_dpa_multicast_routing_action_write(OfDpaFlowContext *fc,
                                      OfDpaFlow *flow)
{
    if (flow->action.write.group_id != ROCKER_GROUP_NONE) {
        fc->action_set.write.group_id = flow->action.write.group_id;
    }
    fc->action_set.write.vlan_id = flow->action.write.vlan_id;
}

static void of_dpa_acl_build_match(OfDpaFlowContext *fc,
                                   OfDpaFlowMatch *match)
{
    match->value.tbl_id = ROCKER_OF_DPA_TABLE_ID_ACL_POLICY;
    match->value.in_pport = fc->in_pport;
    memcpy(match->value.eth.src.a, fc->fields.ethhdr->h_source,
           sizeof(match->value.eth.src.a));
    memcpy(match->value.eth.dst.a, fc->fields.ethhdr->h_dest,
           sizeof(match->value.eth.dst.a));
    match->value.eth.type = *fc->fields.h_proto;
    match->value.eth.vlan_id = fc->fields.vlanhdr->h_tci;
    match->value.width = FLOW_KEY_WIDTH(eth.type);
    if (fc->fields.ipv4hdr) {
        match->value.ip.proto = fc->fields.ipv4hdr->ip_p;
        match->value.ip.tos = fc->fields.ipv4hdr->ip_tos;
        match->value.width = FLOW_KEY_WIDTH(ip.tos);
    } else if (fc->fields.ipv6hdr) {
        match->value.ip.proto =
            fc->fields.ipv6hdr->ip6_ctlun.ip6_un1.ip6_un1_nxt;
        match->value.ip.tos = 0; /* XXX what goes here? */
        match->value.width = FLOW_KEY_WIDTH(ip.tos);
    }
}

static void of_dpa_eg(OfDpaFlowContext *fc);
static void of_dpa_acl_hit(OfDpaFlowContext *fc,
                           OfDpaFlow *dst_flow)
{
    of_dpa_eg(fc);
}

static void of_dpa_acl_action_write(OfDpaFlowContext *fc,
                                    OfDpaFlow *flow)
{
    if (flow->action.write.group_id != ROCKER_GROUP_NONE) {
        fc->action_set.write.group_id = flow->action.write.group_id;
    }
}

static void of_dpa_drop(OfDpaFlowContext *fc)
{
    /* drop packet */
}

static OfDpaGroup *of_dpa_group_find(OfDpa *of_dpa,
                                              uint32_t group_id)
{
    return g_hash_table_lookup(of_dpa->group_tbl, &group_id);
}

static int of_dpa_group_add(OfDpa *of_dpa, OfDpaGroup *group)
{
    g_hash_table_insert(of_dpa->group_tbl, &group->id, group);

    return 0;
}

#if 0
static int of_dpa_group_mod(OfDpa *of_dpa, OfDpaGroup *group)
{
    OfDpaGroup *old_group = of_dpa_group_find(of_dpa, group->id);

    if (!old_group) {
        return -ENOENT;
    }

    /* XXX */

    return 0;
}
#endif

static int of_dpa_group_del(OfDpa *of_dpa, OfDpaGroup *group)
{
    g_hash_table_remove(of_dpa->group_tbl, &group->id);

    return 0;
}

#if 0
static int of_dpa_group_get_stats(OfDpa *of_dpa, uint32_t id)
{
    OfDpaGroup *group = of_dpa_group_find(of_dpa, id);

    if (!group) {
        return -ENOENT;
    }

    /* XXX get/return stats */

    return 0;
}
#endif

static OfDpaGroup *of_dpa_group_alloc(uint32_t id)
{
    OfDpaGroup *group = g_new0(OfDpaGroup, 1);

    group->id = id;

    return group;
}

static void of_dpa_output_l2_interface(OfDpaFlowContext *fc,
                                       OfDpaGroup *group)
{
    uint8_t copy_to_cpu = fc->action_set.apply.copy_to_cpu;

    if (group->l2_interface.pop_vlan) {
        of_dpa_flow_pkt_strip_vlan(fc);
    }

    /* Note: By default, and as per the OpenFlow 1.3.1
     * specification, a packet cannot be forwarded back
     * to the IN_PORT from which it came in. An action
     * bucket that specifies the particular packet's
     * egress port is not evaluated.
     */

    if (group->l2_interface.out_pport == 0) {
        rx_produce(fc->of_dpa->world, fc->in_pport, fc->iov, fc->iovcnt,
                   copy_to_cpu);
    } else if (group->l2_interface.out_pport != fc->in_pport) {
        rocker_port_eg(world_rocker(fc->of_dpa->world),
                       group->l2_interface.out_pport,
                       fc->iov, fc->iovcnt);
    }
}

static void of_dpa_output_l2_rewrite(OfDpaFlowContext *fc,
                                     OfDpaGroup *group)
{
    OfDpaGroup *l2_group =
        of_dpa_group_find(fc->of_dpa, group->l2_rewrite.group_id);

    if (!l2_group) {
        return;
    }

    of_dpa_flow_pkt_hdr_rewrite(fc, group->l2_rewrite.src_mac.a,
                         group->l2_rewrite.dst_mac.a,
                         group->l2_rewrite.vlan_id);
    of_dpa_output_l2_interface(fc, l2_group);
}

static void of_dpa_output_l2_flood(OfDpaFlowContext *fc,
                                   OfDpaGroup *group)
{
    OfDpaGroup *l2_group;
    int i;

    for (i = 0; i < group->l2_flood.group_count; i++) {
        of_dpa_flow_pkt_hdr_reset(fc);
        l2_group = of_dpa_group_find(fc->of_dpa, group->l2_flood.group_ids[i]);
        if (!l2_group) {
            continue;
        }
        switch (ROCKER_GROUP_TYPE_GET(l2_group->id)) {
        case ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE:
            of_dpa_output_l2_interface(fc, l2_group);
            break;
        case ROCKER_OF_DPA_GROUP_TYPE_L2_REWRITE:
            of_dpa_output_l2_rewrite(fc, l2_group);
            break;
        }
    }
}

static void of_dpa_output_l3_unicast(OfDpaFlowContext *fc, OfDpaGroup *group)
{
    OfDpaGroup *l2_group =
        of_dpa_group_find(fc->of_dpa, group->l3_unicast.group_id);

    if (!l2_group) {
        return;
    }

    of_dpa_flow_pkt_hdr_rewrite(fc, group->l3_unicast.src_mac.a,
                                group->l3_unicast.dst_mac.a,
                                group->l3_unicast.vlan_id);
    /* XXX need ttl_check */
    of_dpa_output_l2_interface(fc, l2_group);
}

static void of_dpa_eg(OfDpaFlowContext *fc)
{
    OfDpaFlowAction *set = &fc->action_set;
    OfDpaGroup *group;
    uint32_t group_id;

    /* send a copy of pkt to CPU (controller)? */

    if (set->apply.copy_to_cpu) {
        group_id = ROCKER_GROUP_L2_INTERFACE(set->apply.vlan_id, 0);
        group = of_dpa_group_find(fc->of_dpa, group_id);
        if (group) {
            of_dpa_output_l2_interface(fc, group);
            of_dpa_flow_pkt_hdr_reset(fc);
        }
    }

    /* process group write actions */

    if (!set->write.group_id) {
        return;
    }

    group = of_dpa_group_find(fc->of_dpa, set->write.group_id);
    if (!group) {
        return;
    }

    switch (ROCKER_GROUP_TYPE_GET(group->id)) {
    case ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE:
        of_dpa_output_l2_interface(fc, group);
        break;
    case ROCKER_OF_DPA_GROUP_TYPE_L2_REWRITE:
        of_dpa_output_l2_rewrite(fc, group);
        break;
    case ROCKER_OF_DPA_GROUP_TYPE_L2_FLOOD:
    case ROCKER_OF_DPA_GROUP_TYPE_L2_MCAST:
        of_dpa_output_l2_flood(fc, group);
        break;
    case ROCKER_OF_DPA_GROUP_TYPE_L3_UCAST:
        of_dpa_output_l3_unicast(fc, group);
        break;
    }
}

typedef struct of_dpa_flow_tbl_ops {
    void (*build_match)(OfDpaFlowContext *fc, OfDpaFlowMatch *match);
    void (*hit)(OfDpaFlowContext *fc, OfDpaFlow *flow);
    void (*miss)(OfDpaFlowContext *fc);
    void (*hit_no_goto)(OfDpaFlowContext *fc);
    void (*action_apply)(OfDpaFlowContext *fc, OfDpaFlow *flow);
    void (*action_write)(OfDpaFlowContext *fc, OfDpaFlow *flow);
} OfDpaFlowTblOps;

static OfDpaFlowTblOps of_dpa_tbl_ops[] = {
    [ROCKER_OF_DPA_TABLE_ID_INGRESS_PORT] = {
        .build_match = of_dpa_ig_port_build_match,
        .miss = of_dpa_ig_port_miss,
        .hit_no_goto = of_dpa_drop,
    },
    [ROCKER_OF_DPA_TABLE_ID_VLAN] = {
        .build_match = of_dpa_vlan_build_match,
        .hit_no_goto = of_dpa_drop,
        .action_apply = of_dpa_vlan_insert,
    },
    [ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC] = {
        .build_match = of_dpa_term_mac_build_match,
        .miss = of_dpa_term_mac_miss,
        .hit_no_goto = of_dpa_drop,
        .action_apply = of_dpa_apply_actions,
    },
    [ROCKER_OF_DPA_TABLE_ID_BRIDGING] = {
        .build_match = of_dpa_bridging_build_match,
        .hit = of_dpa_bridging_learn,
        .miss = of_dpa_bridging_miss,
        .hit_no_goto = of_dpa_drop,
        .action_apply = of_dpa_apply_actions,
        .action_write = of_dpa_bridging_action_write,
    },
    [ROCKER_OF_DPA_TABLE_ID_UNICAST_ROUTING] = {
        .build_match = of_dpa_unicast_routing_build_match,
        .miss = of_dpa_unicast_routing_miss,
        .hit_no_goto = of_dpa_drop,
        .action_write = of_dpa_unicast_routing_action_write,
    },
    [ROCKER_OF_DPA_TABLE_ID_MULTICAST_ROUTING] = {
        .build_match = of_dpa_multicast_routing_build_match,
        .miss = of_dpa_multicast_routing_miss,
        .hit_no_goto = of_dpa_drop,
        .action_write = of_dpa_multicast_routing_action_write,
    },
    [ROCKER_OF_DPA_TABLE_ID_ACL_POLICY] = {
        .build_match = of_dpa_acl_build_match,
        .hit = of_dpa_acl_hit,
        .miss = of_dpa_eg,
        .action_apply = of_dpa_apply_actions,
        .action_write = of_dpa_acl_action_write,
    },
};

static void of_dpa_flow_ig_tbl(OfDpaFlowContext *fc, uint32_t tbl_id)
{
    OfDpaFlowTblOps *ops = &of_dpa_tbl_ops[tbl_id];
    OfDpaFlowMatch match = { { 0, }, };
    OfDpaFlow *flow;

    if (ops->build_match) {
        ops->build_match(fc, &match);
    } else {
        return;
    }

    flow = of_dpa_flow_match(fc->of_dpa, &match);
    if (!flow) {
        if (ops->miss) {
            ops->miss(fc);
        }
        return;
    }

    flow->stats.hits++;

    if (ops->action_apply) {
        ops->action_apply(fc, flow);
    }

    if (ops->action_write) {
        ops->action_write(fc, flow);
    }

    if (ops->hit) {
        ops->hit(fc, flow);
    }

    if (flow->action.goto_tbl) {
        of_dpa_flow_ig_tbl(fc, flow->action.goto_tbl);
    } else if (ops->hit_no_goto) {
        ops->hit_no_goto(fc);
    }

    /* drop packet */
}

static ssize_t of_dpa_ig(World *world, uint32_t pport,
                         const struct iovec *iov, int iovcnt)
{
    g_autofree struct iovec *iov_copy = g_new(struct iovec, iovcnt + 2);
    OfDpaFlowContext fc = {
        .of_dpa = world_private(world),
        .in_pport = pport,
        .iov = iov_copy,
        .iovcnt = iovcnt + 2,
    };

    of_dpa_flow_pkt_parse(&fc, iov, iovcnt);
    of_dpa_flow_ig_tbl(&fc, ROCKER_OF_DPA_TABLE_ID_INGRESS_PORT);

    return iov_size(iov, iovcnt);
}

#define ROCKER_TUNNEL_LPORT 0x00010000

static int of_dpa_cmd_add_ig_port(OfDpaFlow *flow, RockerTlv **flow_tlvs)
{
    OfDpaFlowKey *key = &flow->key;
    OfDpaFlowKey *mask = &flow->mask;
    OfDpaFlowAction *action = &flow->action;
    bool overlay_tunnel;

    if (!flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]) {
        return -ROCKER_EINVAL;
    }

    key->tbl_id = ROCKER_OF_DPA_TABLE_ID_INGRESS_PORT;
    key->width = FLOW_KEY_WIDTH(tbl_id);

    key->in_pport = rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT]);
    if (flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT_MASK]) {
        mask->in_pport =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT_MASK]);
    }

    overlay_tunnel = !!(key->in_pport & ROCKER_TUNNEL_LPORT);

    action->goto_tbl =
        rocker_tlv_get_le16(flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]);

    if (!overlay_tunnel && action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_VLAN) {
        return -ROCKER_EINVAL;
    }

    if (overlay_tunnel && action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_BRIDGING) {
        return -ROCKER_EINVAL;
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_add_vlan(OfDpaFlow *flow, RockerTlv **flow_tlvs)
{
    OfDpaFlowKey *key = &flow->key;
    OfDpaFlowKey *mask = &flow->mask;
    OfDpaFlowAction *action = &flow->action;
    uint32_t port;
    bool untagged;

    if (!flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]) {
        DPRINTF("Must give in_pport and vlan_id to install VLAN tbl entry\n");
        return -ROCKER_EINVAL;
    }

    key->tbl_id = ROCKER_OF_DPA_TABLE_ID_VLAN;
    key->width = FLOW_KEY_WIDTH(eth.vlan_id);

    key->in_pport = rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT]);
    if (!fp_port_from_pport(key->in_pport, &port)) {
        DPRINTF("in_pport (%d) not a front-panel port\n", key->in_pport);
        return -ROCKER_EINVAL;
    }
    mask->in_pport = 0xffffffff;

    key->eth.vlan_id = rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]);

    if (flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID_MASK]) {
        mask->eth.vlan_id =
            rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID_MASK]);
    }

    if (key->eth.vlan_id) {
        untagged = false; /* filtering */
    } else {
        untagged = true;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]) {
        action->goto_tbl =
            rocker_tlv_get_le16(flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]);
        if (action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC) {
            DPRINTF("Goto tbl (%d) must be TERM_MAC\n", action->goto_tbl);
            return -ROCKER_EINVAL;
        }
    }

    if (untagged) {
        if (!flow_tlvs[ROCKER_TLV_OF_DPA_NEW_VLAN_ID]) {
            DPRINTF("Must specify new vlan_id if untagged\n");
            return -ROCKER_EINVAL;
        }
        action->apply.new_vlan_id =
            rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_NEW_VLAN_ID]);
        if (1 > ntohs(action->apply.new_vlan_id) ||
            ntohs(action->apply.new_vlan_id) > 4095) {
            DPRINTF("New vlan_id (%d) must be between 1 and 4095\n",
                    ntohs(action->apply.new_vlan_id));
            return -ROCKER_EINVAL;
        }
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_add_term_mac(OfDpaFlow *flow, RockerTlv **flow_tlvs)
{
    OfDpaFlowKey *key = &flow->key;
    OfDpaFlowKey *mask = &flow->mask;
    OfDpaFlowAction *action = &flow->action;
    const MACAddr ipv4_mcast = { .a = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x00 } };
    const MACAddr ipv4_mask =  { .a = { 0xff, 0xff, 0xff, 0x80, 0x00, 0x00 } };
    const MACAddr ipv6_mcast = { .a = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x00 } };
    const MACAddr ipv6_mask =  { .a = { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 } };
    uint32_t port;
    bool unicast = false;
    bool multicast = false;

    if (!flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT_MASK] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_ETHERTYPE] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC_MASK] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID_MASK]) {
        return -ROCKER_EINVAL;
    }

    key->tbl_id = ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC;
    key->width = FLOW_KEY_WIDTH(eth.type);

    key->in_pport = rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT]);
    if (!fp_port_from_pport(key->in_pport, &port)) {
        return -ROCKER_EINVAL;
    }
    mask->in_pport =
        rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT_MASK]);

    key->eth.type = rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_ETHERTYPE]);
    if (key->eth.type != htons(0x0800) && key->eth.type != htons(0x86dd)) {
        return -ROCKER_EINVAL;
    }
    mask->eth.type = htons(0xffff);

    memcpy(key->eth.dst.a,
           rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]),
           sizeof(key->eth.dst.a));
    memcpy(mask->eth.dst.a,
           rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC_MASK]),
           sizeof(mask->eth.dst.a));

    if ((key->eth.dst.a[0] & 0x01) == 0x00) {
        unicast = true;
    }

    /* only two wildcard rules are acceptable for IPv4 and IPv6 multicast */
    if (memcmp(key->eth.dst.a, ipv4_mcast.a, sizeof(key->eth.dst.a)) == 0 &&
        memcmp(mask->eth.dst.a, ipv4_mask.a, sizeof(mask->eth.dst.a)) == 0) {
        multicast = true;
    }
    if (memcmp(key->eth.dst.a, ipv6_mcast.a, sizeof(key->eth.dst.a)) == 0 &&
        memcmp(mask->eth.dst.a, ipv6_mask.a, sizeof(mask->eth.dst.a)) == 0) {
        multicast = true;
    }

    if (!unicast && !multicast) {
        return -ROCKER_EINVAL;
    }

    key->eth.vlan_id = rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]);
    mask->eth.vlan_id =
        rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID_MASK]);

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]) {
        action->goto_tbl =
            rocker_tlv_get_le16(flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]);

        if (action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_UNICAST_ROUTING &&
            action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_MULTICAST_ROUTING) {
            return -ROCKER_EINVAL;
        }

        if (unicast &&
            action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_UNICAST_ROUTING) {
            return -ROCKER_EINVAL;
        }

        if (multicast &&
            action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_MULTICAST_ROUTING) {
            return -ROCKER_EINVAL;
        }
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_COPY_CPU_ACTION]) {
        action->apply.copy_to_cpu =
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_COPY_CPU_ACTION]);
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_add_bridging(OfDpaFlow *flow, RockerTlv **flow_tlvs)
{
    OfDpaFlowKey *key = &flow->key;
    OfDpaFlowKey *mask = &flow->mask;
    OfDpaFlowAction *action = &flow->action;
    bool unicast = false;
    bool dst_mac = false;
    bool dst_mac_mask = false;
    enum {
        BRIDGING_MODE_UNKNOWN,
        BRIDGING_MODE_VLAN_UCAST,
        BRIDGING_MODE_VLAN_MCAST,
        BRIDGING_MODE_VLAN_DFLT,
        BRIDGING_MODE_TUNNEL_UCAST,
        BRIDGING_MODE_TUNNEL_MCAST,
        BRIDGING_MODE_TUNNEL_DFLT,
    } mode = BRIDGING_MODE_UNKNOWN;

    key->tbl_id = ROCKER_OF_DPA_TABLE_ID_BRIDGING;

    if (flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]) {
        key->eth.vlan_id =
            rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]);
        mask->eth.vlan_id = 0xffff;
        key->width = FLOW_KEY_WIDTH(eth.vlan_id);
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_TUNNEL_ID]) {
        key->tunnel_id =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_TUNNEL_ID]);
        mask->tunnel_id = 0xffffffff;
        key->width = FLOW_KEY_WIDTH(tunnel_id);
    }

    /* can't do VLAN bridging and tunnel bridging at same time */
    if (key->eth.vlan_id && key->tunnel_id) {
        DPRINTF("can't do VLAN bridging and tunnel bridging at same time\n");
        return -ROCKER_EINVAL;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]) {
        memcpy(key->eth.dst.a,
               rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]),
               sizeof(key->eth.dst.a));
        key->width = FLOW_KEY_WIDTH(eth.dst);
        dst_mac = true;
        unicast = (key->eth.dst.a[0] & 0x01) == 0x00;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC_MASK]) {
        memcpy(mask->eth.dst.a,
               rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC_MASK]),
               sizeof(mask->eth.dst.a));
        key->width = FLOW_KEY_WIDTH(eth.dst);
        dst_mac_mask = true;
    } else if (flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]) {
        memcpy(mask->eth.dst.a, ff_mac.a, sizeof(mask->eth.dst.a));
    }

    if (key->eth.vlan_id) {
        if (dst_mac && !dst_mac_mask) {
            mode = unicast ? BRIDGING_MODE_VLAN_UCAST :
                             BRIDGING_MODE_VLAN_MCAST;
        } else if ((dst_mac && dst_mac_mask) || !dst_mac) {
            mode = BRIDGING_MODE_VLAN_DFLT;
        }
    } else if (key->tunnel_id) {
        if (dst_mac && !dst_mac_mask) {
            mode = unicast ? BRIDGING_MODE_TUNNEL_UCAST :
                             BRIDGING_MODE_TUNNEL_MCAST;
        } else if ((dst_mac && dst_mac_mask) || !dst_mac) {
            mode = BRIDGING_MODE_TUNNEL_DFLT;
        }
    }

    if (mode == BRIDGING_MODE_UNKNOWN) {
        DPRINTF("Unknown bridging mode\n");
        return -ROCKER_EINVAL;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]) {
        action->goto_tbl =
            rocker_tlv_get_le16(flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]);
        if (action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_ACL_POLICY) {
            DPRINTF("Briding goto tbl must be ACL policy\n");
            return -ROCKER_EINVAL;
        }
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]) {
        action->write.group_id =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]);
        switch (mode) {
        case BRIDGING_MODE_VLAN_UCAST:
            if (ROCKER_GROUP_TYPE_GET(action->write.group_id) !=
                ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE) {
                DPRINTF("Bridging mode vlan ucast needs L2 "
                        "interface group (0x%08x)\n",
                        action->write.group_id);
                return -ROCKER_EINVAL;
            }
            break;
        case BRIDGING_MODE_VLAN_MCAST:
            if (ROCKER_GROUP_TYPE_GET(action->write.group_id) !=
                ROCKER_OF_DPA_GROUP_TYPE_L2_MCAST) {
                DPRINTF("Bridging mode vlan mcast needs L2 "
                        "mcast group (0x%08x)\n",
                        action->write.group_id);
                return -ROCKER_EINVAL;
            }
            break;
        case BRIDGING_MODE_VLAN_DFLT:
            if (ROCKER_GROUP_TYPE_GET(action->write.group_id) !=
                ROCKER_OF_DPA_GROUP_TYPE_L2_FLOOD) {
                DPRINTF("Bridging mode vlan dflt needs L2 "
                        "flood group (0x%08x)\n",
                        action->write.group_id);
                return -ROCKER_EINVAL;
            }
            break;
        case BRIDGING_MODE_TUNNEL_MCAST:
            if (ROCKER_GROUP_TYPE_GET(action->write.group_id) !=
                ROCKER_OF_DPA_GROUP_TYPE_L2_OVERLAY) {
                DPRINTF("Bridging mode tunnel mcast needs L2 "
                        "overlay group (0x%08x)\n",
                        action->write.group_id);
                return -ROCKER_EINVAL;
            }
            break;
        case BRIDGING_MODE_TUNNEL_DFLT:
            if (ROCKER_GROUP_TYPE_GET(action->write.group_id) !=
                ROCKER_OF_DPA_GROUP_TYPE_L2_OVERLAY) {
                DPRINTF("Bridging mode tunnel dflt needs L2 "
                        "overlay group (0x%08x)\n",
                        action->write.group_id);
                return -ROCKER_EINVAL;
            }
            break;
        default:
            return -ROCKER_EINVAL;
        }
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_TUNNEL_LPORT]) {
        action->write.tun_log_lport =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_TUNNEL_LPORT]);
        if (mode != BRIDGING_MODE_TUNNEL_UCAST) {
            DPRINTF("Have tunnel logical port but not "
                    "in bridging tunnel mode\n");
            return -ROCKER_EINVAL;
        }
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_COPY_CPU_ACTION]) {
        action->apply.copy_to_cpu =
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_COPY_CPU_ACTION]);
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_add_unicast_routing(OfDpaFlow *flow,
                                          RockerTlv **flow_tlvs)
{
    OfDpaFlowKey *key = &flow->key;
    OfDpaFlowKey *mask = &flow->mask;
    OfDpaFlowAction *action = &flow->action;
    enum {
        UNICAST_ROUTING_MODE_UNKNOWN,
        UNICAST_ROUTING_MODE_IPV4,
        UNICAST_ROUTING_MODE_IPV6,
    } mode = UNICAST_ROUTING_MODE_UNKNOWN;
    uint8_t type;

    if (!flow_tlvs[ROCKER_TLV_OF_DPA_ETHERTYPE]) {
        return -ROCKER_EINVAL;
    }

    key->tbl_id = ROCKER_OF_DPA_TABLE_ID_UNICAST_ROUTING;
    key->width = FLOW_KEY_WIDTH(ipv6.addr.dst);

    key->eth.type = rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_ETHERTYPE]);
    switch (ntohs(key->eth.type)) {
    case 0x0800:
        mode = UNICAST_ROUTING_MODE_IPV4;
        break;
    case 0x86dd:
        mode = UNICAST_ROUTING_MODE_IPV6;
        break;
    default:
        return -ROCKER_EINVAL;
    }
    mask->eth.type = htons(0xffff);

    switch (mode) {
    case UNICAST_ROUTING_MODE_IPV4:
        if (!flow_tlvs[ROCKER_TLV_OF_DPA_DST_IP]) {
            return -ROCKER_EINVAL;
        }
        key->ipv4.addr.dst =
            rocker_tlv_get_u32(flow_tlvs[ROCKER_TLV_OF_DPA_DST_IP]);
        if (ipv4_addr_is_multicast(key->ipv4.addr.dst)) {
            return -ROCKER_EINVAL;
        }
        flow->lpm = of_dpa_mask2prefix(htonl(0xffffffff));
        if (flow_tlvs[ROCKER_TLV_OF_DPA_DST_IP_MASK]) {
            mask->ipv4.addr.dst =
                rocker_tlv_get_u32(flow_tlvs[ROCKER_TLV_OF_DPA_DST_IP_MASK]);
            flow->lpm = of_dpa_mask2prefix(mask->ipv4.addr.dst);
        }
        break;
    case UNICAST_ROUTING_MODE_IPV6:
        if (!flow_tlvs[ROCKER_TLV_OF_DPA_DST_IPV6]) {
            return -ROCKER_EINVAL;
        }
        memcpy(&key->ipv6.addr.dst,
               rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_IPV6]),
               sizeof(key->ipv6.addr.dst));
        if (ipv6_addr_is_multicast(&key->ipv6.addr.dst)) {
            return -ROCKER_EINVAL;
        }
        if (flow_tlvs[ROCKER_TLV_OF_DPA_DST_IPV6_MASK]) {
            memcpy(&mask->ipv6.addr.dst,
                   rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_IPV6_MASK]),
                   sizeof(mask->ipv6.addr.dst));
        }
        break;
    default:
        return -ROCKER_EINVAL;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]) {
        action->goto_tbl =
            rocker_tlv_get_le16(flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]);
        if (action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_ACL_POLICY) {
            return -ROCKER_EINVAL;
        }
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]) {
        action->write.group_id =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]);
        type = ROCKER_GROUP_TYPE_GET(action->write.group_id);
        if (type != ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE &&
            type != ROCKER_OF_DPA_GROUP_TYPE_L3_UCAST &&
            type != ROCKER_OF_DPA_GROUP_TYPE_L3_ECMP) {
            return -ROCKER_EINVAL;
        }
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_add_multicast_routing(OfDpaFlow *flow,
                                            RockerTlv **flow_tlvs)
{
    OfDpaFlowKey *key = &flow->key;
    OfDpaFlowKey *mask = &flow->mask;
    OfDpaFlowAction *action = &flow->action;
    enum {
        MULTICAST_ROUTING_MODE_UNKNOWN,
        MULTICAST_ROUTING_MODE_IPV4,
        MULTICAST_ROUTING_MODE_IPV6,
    } mode = MULTICAST_ROUTING_MODE_UNKNOWN;

    if (!flow_tlvs[ROCKER_TLV_OF_DPA_ETHERTYPE] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]) {
        return -ROCKER_EINVAL;
    }

    key->tbl_id = ROCKER_OF_DPA_TABLE_ID_MULTICAST_ROUTING;
    key->width = FLOW_KEY_WIDTH(ipv6.addr.dst);

    key->eth.type = rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_ETHERTYPE]);
    switch (ntohs(key->eth.type)) {
    case 0x0800:
        mode = MULTICAST_ROUTING_MODE_IPV4;
        break;
    case 0x86dd:
        mode = MULTICAST_ROUTING_MODE_IPV6;
        break;
    default:
        return -ROCKER_EINVAL;
    }

    key->eth.vlan_id = rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]);

    switch (mode) {
    case MULTICAST_ROUTING_MODE_IPV4:

        if (flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IP]) {
            key->ipv4.addr.src =
                rocker_tlv_get_u32(flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IP]);
        }

        if (flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IP_MASK]) {
            mask->ipv4.addr.src =
                rocker_tlv_get_u32(flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IP_MASK]);
        }

        if (!flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IP]) {
            if (mask->ipv4.addr.src != 0) {
                return -ROCKER_EINVAL;
            }
        }

        if (!flow_tlvs[ROCKER_TLV_OF_DPA_DST_IP]) {
            return -ROCKER_EINVAL;
        }

        key->ipv4.addr.dst =
            rocker_tlv_get_u32(flow_tlvs[ROCKER_TLV_OF_DPA_DST_IP]);
        if (!ipv4_addr_is_multicast(key->ipv4.addr.dst)) {
            return -ROCKER_EINVAL;
        }

        break;

    case MULTICAST_ROUTING_MODE_IPV6:

        if (flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IPV6]) {
            memcpy(&key->ipv6.addr.src,
                   rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IPV6]),
                   sizeof(key->ipv6.addr.src));
        }

        if (flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IPV6_MASK]) {
            memcpy(&mask->ipv6.addr.src,
                   rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IPV6_MASK]),
                   sizeof(mask->ipv6.addr.src));
        }

        if (!flow_tlvs[ROCKER_TLV_OF_DPA_SRC_IPV6]) {
            if (mask->ipv6.addr.src.addr32[0] != 0 &&
                mask->ipv6.addr.src.addr32[1] != 0 &&
                mask->ipv6.addr.src.addr32[2] != 0 &&
                mask->ipv6.addr.src.addr32[3] != 0) {
                return -ROCKER_EINVAL;
            }
        }

        if (!flow_tlvs[ROCKER_TLV_OF_DPA_DST_IPV6]) {
            return -ROCKER_EINVAL;
        }

        memcpy(&key->ipv6.addr.dst,
               rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_IPV6]),
               sizeof(key->ipv6.addr.dst));
        if (!ipv6_addr_is_multicast(&key->ipv6.addr.dst)) {
            return -ROCKER_EINVAL;
        }

        break;

    default:
        return -ROCKER_EINVAL;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]) {
        action->goto_tbl =
            rocker_tlv_get_le16(flow_tlvs[ROCKER_TLV_OF_DPA_GOTO_TABLE_ID]);
        if (action->goto_tbl != ROCKER_OF_DPA_TABLE_ID_ACL_POLICY) {
            return -ROCKER_EINVAL;
        }
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]) {
        action->write.group_id =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]);
        if (ROCKER_GROUP_TYPE_GET(action->write.group_id) !=
            ROCKER_OF_DPA_GROUP_TYPE_L3_MCAST) {
            return -ROCKER_EINVAL;
        }
        action->write.vlan_id = key->eth.vlan_id;
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_add_acl_ip(OfDpaFlowKey *key, OfDpaFlowKey *mask,
                                 RockerTlv **flow_tlvs)
{
    key->width = FLOW_KEY_WIDTH(ip.tos);

    key->ip.proto = 0;
    key->ip.tos = 0;
    mask->ip.proto = 0;
    mask->ip.tos = 0;

    if (flow_tlvs[ROCKER_TLV_OF_DPA_IP_PROTO]) {
        key->ip.proto =
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_IP_PROTO]);
    }
    if (flow_tlvs[ROCKER_TLV_OF_DPA_IP_PROTO_MASK]) {
        mask->ip.proto =
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_IP_PROTO_MASK]);
    }
    if (flow_tlvs[ROCKER_TLV_OF_DPA_IP_DSCP]) {
        key->ip.tos =
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_IP_DSCP]);
    }
    if (flow_tlvs[ROCKER_TLV_OF_DPA_IP_DSCP_MASK]) {
        mask->ip.tos =
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_IP_DSCP_MASK]);
    }
    if (flow_tlvs[ROCKER_TLV_OF_DPA_IP_ECN]) {
        key->ip.tos |=
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_IP_ECN]) << 6;
    }
    if (flow_tlvs[ROCKER_TLV_OF_DPA_IP_ECN_MASK]) {
        mask->ip.tos |=
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_IP_ECN_MASK]) << 6;
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_add_acl(OfDpaFlow *flow, RockerTlv **flow_tlvs)
{
    OfDpaFlowKey *key = &flow->key;
    OfDpaFlowKey *mask = &flow->mask;
    OfDpaFlowAction *action = &flow->action;
    enum {
        ACL_MODE_UNKNOWN,
        ACL_MODE_IPV4_VLAN,
        ACL_MODE_IPV6_VLAN,
        ACL_MODE_IPV4_TENANT,
        ACL_MODE_IPV6_TENANT,
        ACL_MODE_NON_IP_VLAN,
        ACL_MODE_NON_IP_TENANT,
        ACL_MODE_ANY_VLAN,
        ACL_MODE_ANY_TENANT,
    } mode = ACL_MODE_UNKNOWN;
    int err = ROCKER_OK;

    if (!flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_ETHERTYPE]) {
        return -ROCKER_EINVAL;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID] &&
        flow_tlvs[ROCKER_TLV_OF_DPA_TUNNEL_ID]) {
        return -ROCKER_EINVAL;
    }

    key->tbl_id = ROCKER_OF_DPA_TABLE_ID_ACL_POLICY;
    key->width = FLOW_KEY_WIDTH(eth.type);

    key->in_pport = rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT]);
    if (flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT_MASK]) {
        mask->in_pport =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_IN_PPORT_MASK]);
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_SRC_MAC]) {
        memcpy(key->eth.src.a,
               rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_SRC_MAC]),
               sizeof(key->eth.src.a));
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_SRC_MAC_MASK]) {
        memcpy(mask->eth.src.a,
               rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_SRC_MAC_MASK]),
               sizeof(mask->eth.src.a));
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]) {
        memcpy(key->eth.dst.a,
               rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]),
               sizeof(key->eth.dst.a));
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC_MASK]) {
        memcpy(mask->eth.dst.a,
               rocker_tlv_data(flow_tlvs[ROCKER_TLV_OF_DPA_DST_MAC_MASK]),
               sizeof(mask->eth.dst.a));
    }

    key->eth.type = rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_ETHERTYPE]);
    if (key->eth.type) {
        mask->eth.type = 0xffff;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]) {
        key->eth.vlan_id =
            rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]);
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID_MASK]) {
        mask->eth.vlan_id =
            rocker_tlv_get_u16(flow_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID_MASK]);
    }

    switch (ntohs(key->eth.type)) {
    case 0x0000:
        mode = (key->eth.vlan_id) ? ACL_MODE_ANY_VLAN : ACL_MODE_ANY_TENANT;
        break;
    case 0x0800:
        mode = (key->eth.vlan_id) ? ACL_MODE_IPV4_VLAN : ACL_MODE_IPV4_TENANT;
        break;
    case 0x86dd:
        mode = (key->eth.vlan_id) ? ACL_MODE_IPV6_VLAN : ACL_MODE_IPV6_TENANT;
        break;
    default:
        mode = (key->eth.vlan_id) ? ACL_MODE_NON_IP_VLAN :
                                    ACL_MODE_NON_IP_TENANT;
        break;
    }

    /* XXX only supporting VLAN modes for now */
    if (mode != ACL_MODE_IPV4_VLAN &&
        mode != ACL_MODE_IPV6_VLAN &&
        mode != ACL_MODE_NON_IP_VLAN &&
        mode != ACL_MODE_ANY_VLAN) {
        return -ROCKER_EINVAL;
    }

    switch (ntohs(key->eth.type)) {
    case 0x0800:
    case 0x86dd:
        err = of_dpa_cmd_add_acl_ip(key, mask, flow_tlvs);
        break;
    }

    if (err) {
        return err;
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]) {
        action->write.group_id =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]);
    }

    if (flow_tlvs[ROCKER_TLV_OF_DPA_COPY_CPU_ACTION]) {
        action->apply.copy_to_cpu =
            rocker_tlv_get_u8(flow_tlvs[ROCKER_TLV_OF_DPA_COPY_CPU_ACTION]);
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_flow_add_mod(OfDpa *of_dpa, OfDpaFlow *flow,
                                   RockerTlv **flow_tlvs)
{
    enum rocker_of_dpa_table_id tbl;
    int err = ROCKER_OK;

    if (!flow_tlvs[ROCKER_TLV_OF_DPA_TABLE_ID] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_PRIORITY] ||
        !flow_tlvs[ROCKER_TLV_OF_DPA_HARDTIME]) {
        return -ROCKER_EINVAL;
    }

    tbl = rocker_tlv_get_le16(flow_tlvs[ROCKER_TLV_OF_DPA_TABLE_ID]);
    flow->priority = rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_PRIORITY]);
    flow->hardtime = rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_HARDTIME]);

    if (flow_tlvs[ROCKER_TLV_OF_DPA_IDLETIME]) {
        if (tbl == ROCKER_OF_DPA_TABLE_ID_INGRESS_PORT ||
            tbl == ROCKER_OF_DPA_TABLE_ID_VLAN ||
            tbl == ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC) {
            return -ROCKER_EINVAL;
        }
        flow->idletime =
            rocker_tlv_get_le32(flow_tlvs[ROCKER_TLV_OF_DPA_IDLETIME]);
    }

    switch (tbl) {
    case ROCKER_OF_DPA_TABLE_ID_INGRESS_PORT:
        err = of_dpa_cmd_add_ig_port(flow, flow_tlvs);
        break;
    case ROCKER_OF_DPA_TABLE_ID_VLAN:
        err = of_dpa_cmd_add_vlan(flow, flow_tlvs);
        break;
    case ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC:
        err = of_dpa_cmd_add_term_mac(flow, flow_tlvs);
        break;
    case ROCKER_OF_DPA_TABLE_ID_BRIDGING:
        err = of_dpa_cmd_add_bridging(flow, flow_tlvs);
        break;
    case ROCKER_OF_DPA_TABLE_ID_UNICAST_ROUTING:
        err = of_dpa_cmd_add_unicast_routing(flow, flow_tlvs);
        break;
    case ROCKER_OF_DPA_TABLE_ID_MULTICAST_ROUTING:
        err = of_dpa_cmd_add_multicast_routing(flow, flow_tlvs);
        break;
    case ROCKER_OF_DPA_TABLE_ID_ACL_POLICY:
        err = of_dpa_cmd_add_acl(flow, flow_tlvs);
        break;
    }

    return err;
}

static int of_dpa_cmd_flow_add(OfDpa *of_dpa, uint64_t cookie,
                               RockerTlv **flow_tlvs)
{
    OfDpaFlow *flow = of_dpa_flow_find(of_dpa, cookie);
    int err = ROCKER_OK;

    if (flow) {
        return -ROCKER_EEXIST;
    }

    flow = of_dpa_flow_alloc(cookie);

    err = of_dpa_cmd_flow_add_mod(of_dpa, flow, flow_tlvs);
    if (err) {
        g_free(flow);
        return err;
    }

    return of_dpa_flow_add(of_dpa, flow);
}

static int of_dpa_cmd_flow_mod(OfDpa *of_dpa, uint64_t cookie,
                               RockerTlv **flow_tlvs)
{
    OfDpaFlow *flow = of_dpa_flow_find(of_dpa, cookie);

    if (!flow) {
        return -ROCKER_ENOENT;
    }

    return of_dpa_cmd_flow_add_mod(of_dpa, flow, flow_tlvs);
}

static int of_dpa_cmd_flow_del(OfDpa *of_dpa, uint64_t cookie)
{
    OfDpaFlow *flow = of_dpa_flow_find(of_dpa, cookie);

    if (!flow) {
        return -ROCKER_ENOENT;
    }

    of_dpa_flow_del(of_dpa, flow);

    return ROCKER_OK;
}

static int of_dpa_cmd_flow_get_stats(OfDpa *of_dpa, uint64_t cookie,
                                     struct desc_info *info, char *buf)
{
    OfDpaFlow *flow = of_dpa_flow_find(of_dpa, cookie);
    size_t tlv_size;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) / 1000;
    int pos;

    if (!flow) {
        return -ROCKER_ENOENT;
    }

    tlv_size = rocker_tlv_total_size(sizeof(uint32_t)) +  /* duration */
               rocker_tlv_total_size(sizeof(uint64_t)) +  /* rx_pkts */
               rocker_tlv_total_size(sizeof(uint64_t));   /* tx_ptks */

    if (tlv_size > desc_buf_size(info)) {
        return -ROCKER_EMSGSIZE;
    }

    pos = 0;
    rocker_tlv_put_le32(buf, &pos, ROCKER_TLV_OF_DPA_FLOW_STAT_DURATION,
                        (int32_t)(now - flow->stats.install_time));
    rocker_tlv_put_le64(buf, &pos, ROCKER_TLV_OF_DPA_FLOW_STAT_RX_PKTS,
                        flow->stats.rx_pkts);
    rocker_tlv_put_le64(buf, &pos, ROCKER_TLV_OF_DPA_FLOW_STAT_TX_PKTS,
                        flow->stats.tx_pkts);

    return desc_set_buf(info, tlv_size);
}

static int of_dpa_flow_cmd(OfDpa *of_dpa, struct desc_info *info,
                           char *buf, uint16_t cmd,
                           RockerTlv **flow_tlvs)
{
    uint64_t cookie;

    if (!flow_tlvs[ROCKER_TLV_OF_DPA_COOKIE]) {
        return -ROCKER_EINVAL;
    }

    cookie = rocker_tlv_get_le64(flow_tlvs[ROCKER_TLV_OF_DPA_COOKIE]);

    switch (cmd) {
    case ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_ADD:
        return of_dpa_cmd_flow_add(of_dpa, cookie, flow_tlvs);
    case ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_MOD:
        return of_dpa_cmd_flow_mod(of_dpa, cookie, flow_tlvs);
    case ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_DEL:
        return of_dpa_cmd_flow_del(of_dpa, cookie);
    case ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_GET_STATS:
        return of_dpa_cmd_flow_get_stats(of_dpa, cookie, info, buf);
    }

    return -ROCKER_ENOTSUP;
}

static int of_dpa_cmd_add_l2_interface(OfDpaGroup *group,
                                       RockerTlv **group_tlvs)
{
    if (!group_tlvs[ROCKER_TLV_OF_DPA_OUT_PPORT] ||
        !group_tlvs[ROCKER_TLV_OF_DPA_POP_VLAN]) {
        return -ROCKER_EINVAL;
    }

    group->l2_interface.out_pport =
        rocker_tlv_get_le32(group_tlvs[ROCKER_TLV_OF_DPA_OUT_PPORT]);
    group->l2_interface.pop_vlan =
        rocker_tlv_get_u8(group_tlvs[ROCKER_TLV_OF_DPA_POP_VLAN]);

    return ROCKER_OK;
}

static int of_dpa_cmd_add_l2_rewrite(OfDpa *of_dpa, OfDpaGroup *group,
                                     RockerTlv **group_tlvs)
{
    OfDpaGroup *l2_interface_group;

    if (!group_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID_LOWER]) {
        return -ROCKER_EINVAL;
    }

    group->l2_rewrite.group_id =
        rocker_tlv_get_le32(group_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID_LOWER]);

    l2_interface_group = of_dpa_group_find(of_dpa, group->l2_rewrite.group_id);
    if (!l2_interface_group ||
        ROCKER_GROUP_TYPE_GET(l2_interface_group->id) !=
                              ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE) {
        DPRINTF("l2 rewrite group needs a valid l2 interface group\n");
        return -ROCKER_EINVAL;
    }

    if (group_tlvs[ROCKER_TLV_OF_DPA_SRC_MAC]) {
        memcpy(group->l2_rewrite.src_mac.a,
               rocker_tlv_data(group_tlvs[ROCKER_TLV_OF_DPA_SRC_MAC]),
               sizeof(group->l2_rewrite.src_mac.a));
    }

    if (group_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]) {
        memcpy(group->l2_rewrite.dst_mac.a,
               rocker_tlv_data(group_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]),
               sizeof(group->l2_rewrite.dst_mac.a));
    }

    if (group_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]) {
        group->l2_rewrite.vlan_id =
            rocker_tlv_get_u16(group_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]);
        if (ROCKER_GROUP_VLAN_GET(l2_interface_group->id) !=
            (ntohs(group->l2_rewrite.vlan_id) & VLAN_VID_MASK)) {
            DPRINTF("Set VLAN ID must be same as L2 interface group\n");
            return -ROCKER_EINVAL;
        }
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_add_l2_flood(OfDpa *of_dpa, OfDpaGroup *group,
                                   RockerTlv **group_tlvs)
{
    OfDpaGroup *l2_group;
    RockerTlv **tlvs;
    int err;
    int i;

    if (!group_tlvs[ROCKER_TLV_OF_DPA_GROUP_COUNT] ||
        !group_tlvs[ROCKER_TLV_OF_DPA_GROUP_IDS]) {
        return -ROCKER_EINVAL;
    }

    group->l2_flood.group_count =
        rocker_tlv_get_le16(group_tlvs[ROCKER_TLV_OF_DPA_GROUP_COUNT]);

    tlvs = g_new0(RockerTlv *, group->l2_flood.group_count + 1);

    g_free(group->l2_flood.group_ids);
    group->l2_flood.group_ids =
        g_new0(uint32_t, group->l2_flood.group_count);

    rocker_tlv_parse_nested(tlvs, group->l2_flood.group_count,
                            group_tlvs[ROCKER_TLV_OF_DPA_GROUP_IDS]);

    for (i = 0; i < group->l2_flood.group_count; i++) {
        group->l2_flood.group_ids[i] = rocker_tlv_get_le32(tlvs[i + 1]);
    }

    /* All of the L2 interface groups referenced by the L2 flood
     * must have same VLAN
     */

    for (i = 0; i < group->l2_flood.group_count; i++) {
        l2_group = of_dpa_group_find(of_dpa, group->l2_flood.group_ids[i]);
        if (!l2_group) {
            continue;
        }
        if ((ROCKER_GROUP_TYPE_GET(l2_group->id) ==
             ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE) &&
            (ROCKER_GROUP_VLAN_GET(l2_group->id) !=
             ROCKER_GROUP_VLAN_GET(group->id))) {
            DPRINTF("l2 interface group 0x%08x VLAN doesn't match l2 "
                    "flood group 0x%08x\n",
                    group->l2_flood.group_ids[i], group->id);
            err = -ROCKER_EINVAL;
            goto err_out;
        }
    }

    g_free(tlvs);
    return ROCKER_OK;

err_out:
    group->l2_flood.group_count = 0;
    g_free(group->l2_flood.group_ids);
    g_free(tlvs);

    return err;
}

static int of_dpa_cmd_add_l3_unicast(OfDpaGroup *group, RockerTlv **group_tlvs)
{
    if (!group_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID_LOWER]) {
        return -ROCKER_EINVAL;
    }

    group->l3_unicast.group_id =
        rocker_tlv_get_le32(group_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID_LOWER]);

    if (group_tlvs[ROCKER_TLV_OF_DPA_SRC_MAC]) {
        memcpy(group->l3_unicast.src_mac.a,
               rocker_tlv_data(group_tlvs[ROCKER_TLV_OF_DPA_SRC_MAC]),
               sizeof(group->l3_unicast.src_mac.a));
    }

    if (group_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]) {
        memcpy(group->l3_unicast.dst_mac.a,
               rocker_tlv_data(group_tlvs[ROCKER_TLV_OF_DPA_DST_MAC]),
               sizeof(group->l3_unicast.dst_mac.a));
    }

    if (group_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]) {
        group->l3_unicast.vlan_id =
            rocker_tlv_get_u16(group_tlvs[ROCKER_TLV_OF_DPA_VLAN_ID]);
    }

    if (group_tlvs[ROCKER_TLV_OF_DPA_TTL_CHECK]) {
        group->l3_unicast.ttl_check =
            rocker_tlv_get_u8(group_tlvs[ROCKER_TLV_OF_DPA_TTL_CHECK]);
    }

    return ROCKER_OK;
}

static int of_dpa_cmd_group_do(OfDpa *of_dpa, uint32_t group_id,
                               OfDpaGroup *group, RockerTlv **group_tlvs)
{
    uint8_t type = ROCKER_GROUP_TYPE_GET(group_id);

    switch (type) {
    case ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE:
        return of_dpa_cmd_add_l2_interface(group, group_tlvs);
    case ROCKER_OF_DPA_GROUP_TYPE_L2_REWRITE:
        return of_dpa_cmd_add_l2_rewrite(of_dpa, group, group_tlvs);
    case ROCKER_OF_DPA_GROUP_TYPE_L2_FLOOD:
    /* Treat L2 multicast group same as a L2 flood group */
    case ROCKER_OF_DPA_GROUP_TYPE_L2_MCAST:
        return of_dpa_cmd_add_l2_flood(of_dpa, group, group_tlvs);
    case ROCKER_OF_DPA_GROUP_TYPE_L3_UCAST:
        return of_dpa_cmd_add_l3_unicast(group, group_tlvs);
    }

    return -ROCKER_ENOTSUP;
}

static int of_dpa_cmd_group_add(OfDpa *of_dpa, uint32_t group_id,
                                RockerTlv **group_tlvs)
{
    OfDpaGroup *group = of_dpa_group_find(of_dpa, group_id);
    int err;

    if (group) {
        return -ROCKER_EEXIST;
    }

    group = of_dpa_group_alloc(group_id);

    err = of_dpa_cmd_group_do(of_dpa, group_id, group, group_tlvs);
    if (err) {
        goto err_cmd_add;
    }

    err = of_dpa_group_add(of_dpa, group);
    if (err) {
        goto err_cmd_add;
    }

    return ROCKER_OK;

err_cmd_add:
    g_free(group);
    return err;
}

static int of_dpa_cmd_group_mod(OfDpa *of_dpa, uint32_t group_id,
                                RockerTlv **group_tlvs)
{
    OfDpaGroup *group = of_dpa_group_find(of_dpa, group_id);

    if (!group) {
        return -ROCKER_ENOENT;
    }

    return of_dpa_cmd_group_do(of_dpa, group_id, group, group_tlvs);
}

static int of_dpa_cmd_group_del(OfDpa *of_dpa, uint32_t group_id)
{
    OfDpaGroup *group = of_dpa_group_find(of_dpa, group_id);

    if (!group) {
        return -ROCKER_ENOENT;
    }

    return of_dpa_group_del(of_dpa, group);
}

static int of_dpa_cmd_group_get_stats(OfDpa *of_dpa, uint32_t group_id,
                                      struct desc_info *info, char *buf)
{
    return -ROCKER_ENOTSUP;
}

static int of_dpa_group_cmd(OfDpa *of_dpa, struct desc_info *info,
                            char *buf, uint16_t cmd, RockerTlv **group_tlvs)
{
    uint32_t group_id;

    if (!group_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]) {
        return -ROCKER_EINVAL;
    }

    group_id = rocker_tlv_get_le32(group_tlvs[ROCKER_TLV_OF_DPA_GROUP_ID]);

    switch (cmd) {
    case ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_ADD:
        return of_dpa_cmd_group_add(of_dpa, group_id, group_tlvs);
    case ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_MOD:
        return of_dpa_cmd_group_mod(of_dpa, group_id, group_tlvs);
    case ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_DEL:
        return of_dpa_cmd_group_del(of_dpa, group_id);
    case ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_GET_STATS:
        return of_dpa_cmd_group_get_stats(of_dpa, group_id, info, buf);
    }

    return -ROCKER_ENOTSUP;
}

static int of_dpa_cmd(World *world, struct desc_info *info,
                      char *buf, uint16_t cmd, RockerTlv *cmd_info_tlv)
{
    OfDpa *of_dpa = world_private(world);
    RockerTlv *tlvs[ROCKER_TLV_OF_DPA_MAX + 1];

    rocker_tlv_parse_nested(tlvs, ROCKER_TLV_OF_DPA_MAX, cmd_info_tlv);

    switch (cmd) {
    case ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_ADD:
    case ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_MOD:
    case ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_DEL:
    case ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_GET_STATS:
        return of_dpa_flow_cmd(of_dpa, info, buf, cmd, tlvs);
    case ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_ADD:
    case ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_MOD:
    case ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_DEL:
    case ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_GET_STATS:
        return of_dpa_group_cmd(of_dpa, info, buf, cmd, tlvs);
    }

    return -ROCKER_ENOTSUP;
}

static gboolean rocker_int64_equal(gconstpointer v1, gconstpointer v2)
{
    return *((const uint64_t *)v1) == *((const uint64_t *)v2);
}

static guint rocker_int64_hash(gconstpointer v)
{
    return (guint)*(const uint64_t *)v;
}

static int of_dpa_init(World *world)
{
    OfDpa *of_dpa = world_private(world);

    of_dpa->world = world;

    of_dpa->flow_tbl = g_hash_table_new_full(rocker_int64_hash,
                                             rocker_int64_equal,
                                             NULL, g_free);
    if (!of_dpa->flow_tbl) {
        return -ENOMEM;
    }

    of_dpa->group_tbl = g_hash_table_new_full(g_int_hash, g_int_equal,
                                              NULL, g_free);
    if (!of_dpa->group_tbl) {
        goto err_group_tbl;
    }

    /* XXX hardcode some artificial table max values */
    of_dpa->flow_tbl_max_size = 100;
    of_dpa->group_tbl_max_size = 100;

    return 0;

err_group_tbl:
    g_hash_table_destroy(of_dpa->flow_tbl);
    return -ENOMEM;
}

static void of_dpa_uninit(World *world)
{
    OfDpa *of_dpa = world_private(world);

    g_hash_table_destroy(of_dpa->group_tbl);
    g_hash_table_destroy(of_dpa->flow_tbl);
}

struct of_dpa_flow_fill_context {
    RockerOfDpaFlowList *list;
    uint32_t tbl_id;
};

static void of_dpa_flow_fill(void *cookie, void *value, void *user_data)
{
    struct of_dpa_flow *flow = value;
    struct of_dpa_flow_key *key = &flow->key;
    struct of_dpa_flow_key *mask = &flow->mask;
    struct of_dpa_flow_fill_context *flow_context = user_data;
    RockerOfDpaFlow *nflow;
    RockerOfDpaFlowKey *nkey;
    RockerOfDpaFlowMask *nmask;
    RockerOfDpaFlowAction *naction;

    if (flow_context->tbl_id != -1 &&
        flow_context->tbl_id != key->tbl_id) {
        return;
    }

    nflow = g_malloc0(sizeof(*nflow));
    nkey = nflow->key = g_malloc0(sizeof(*nkey));
    nmask = nflow->mask = g_malloc0(sizeof(*nmask));
    naction = nflow->action = g_malloc0(sizeof(*naction));

    nflow->cookie = flow->cookie;
    nflow->hits = flow->stats.hits;
    nkey->priority = flow->priority;
    nkey->tbl_id = key->tbl_id;

    if (key->in_pport || mask->in_pport) {
        nkey->has_in_pport = true;
        nkey->in_pport = key->in_pport;
    }

    if (nkey->has_in_pport && mask->in_pport != 0xffffffff) {
        nmask->has_in_pport = true;
        nmask->in_pport = mask->in_pport;
    }

    if (key->eth.vlan_id || mask->eth.vlan_id) {
        nkey->has_vlan_id = true;
        nkey->vlan_id = ntohs(key->eth.vlan_id);
    }

    if (nkey->has_vlan_id && mask->eth.vlan_id != 0xffff) {
        nmask->has_vlan_id = true;
        nmask->vlan_id = ntohs(mask->eth.vlan_id);
    }

    if (key->tunnel_id || mask->tunnel_id) {
        nkey->has_tunnel_id = true;
        nkey->tunnel_id = key->tunnel_id;
    }

    if (nkey->has_tunnel_id && mask->tunnel_id != 0xffffffff) {
        nmask->has_tunnel_id = true;
        nmask->tunnel_id = mask->tunnel_id;
    }

    if (memcmp(key->eth.src.a, zero_mac.a, ETH_ALEN) ||
        memcmp(mask->eth.src.a, zero_mac.a, ETH_ALEN)) {
        nkey->eth_src = qemu_mac_strdup_printf(key->eth.src.a);
    }

    if (nkey->eth_src && memcmp(mask->eth.src.a, ff_mac.a, ETH_ALEN)) {
        nmask->eth_src = qemu_mac_strdup_printf(mask->eth.src.a);
    }

    if (memcmp(key->eth.dst.a, zero_mac.a, ETH_ALEN) ||
        memcmp(mask->eth.dst.a, zero_mac.a, ETH_ALEN)) {
        nkey->eth_dst = qemu_mac_strdup_printf(key->eth.dst.a);
    }

    if (nkey->eth_dst && memcmp(mask->eth.dst.a, ff_mac.a, ETH_ALEN)) {
        nmask->eth_dst = qemu_mac_strdup_printf(mask->eth.dst.a);
    }

    if (key->eth.type) {

        nkey->has_eth_type = true;
        nkey->eth_type = ntohs(key->eth.type);

        switch (ntohs(key->eth.type)) {
        case 0x0800:
        case 0x86dd:
            if (key->ip.proto || mask->ip.proto) {
                nkey->has_ip_proto = true;
                nkey->ip_proto = key->ip.proto;
            }
            if (nkey->has_ip_proto && mask->ip.proto != 0xff) {
                nmask->has_ip_proto = true;
                nmask->ip_proto = mask->ip.proto;
            }
            if (key->ip.tos || mask->ip.tos) {
                nkey->has_ip_tos = true;
                nkey->ip_tos = key->ip.tos;
            }
            if (nkey->has_ip_tos && mask->ip.tos != 0xff) {
                nmask->has_ip_tos = true;
                nmask->ip_tos = mask->ip.tos;
            }
            break;
        }

        switch (ntohs(key->eth.type)) {
        case 0x0800:
            if (key->ipv4.addr.dst || mask->ipv4.addr.dst) {
                char *dst = inet_ntoa(*(struct in_addr *)&key->ipv4.addr.dst);
                int dst_len = of_dpa_mask2prefix(mask->ipv4.addr.dst);
                nkey->ip_dst = g_strdup_printf("%s/%d", dst, dst_len);
            }
            break;
        }
    }

    if (flow->action.goto_tbl) {
        naction->has_goto_tbl = true;
        naction->goto_tbl = flow->action.goto_tbl;
    }

    if (flow->action.write.group_id) {
        naction->has_group_id = true;
        naction->group_id = flow->action.write.group_id;
    }

    if (flow->action.apply.new_vlan_id) {
        naction->has_new_vlan_id = true;
        naction->new_vlan_id = flow->action.apply.new_vlan_id;
    }

    QAPI_LIST_PREPEND(flow_context->list, nflow);
}

RockerOfDpaFlowList *qmp_query_rocker_of_dpa_flows(const char *name,
                                                   bool has_tbl_id,
                                                   uint32_t tbl_id,
                                                   Error **errp)
{
    struct rocker *r;
    struct world *w;
    struct of_dpa *of_dpa;
    struct of_dpa_flow_fill_context fill_context = {
        .list = NULL,
        .tbl_id = tbl_id,
    };

    r = rocker_find(name);
    if (!r) {
        error_setg(errp, "rocker %s not found", name);
        return NULL;
    }

    w = rocker_get_world(r, ROCKER_WORLD_TYPE_OF_DPA);
    if (!w) {
        error_setg(errp, "rocker %s doesn't have OF-DPA world", name);
        return NULL;
    }

    of_dpa = world_private(w);

    g_hash_table_foreach(of_dpa->flow_tbl, of_dpa_flow_fill, &fill_context);

    return fill_context.list;
}

struct of_dpa_group_fill_context {
    RockerOfDpaGroupList *list;
    uint8_t type;
};

static void of_dpa_group_fill(void *key, void *value, void *user_data)
{
    struct of_dpa_group *group = value;
    struct of_dpa_group_fill_context *flow_context = user_data;
    RockerOfDpaGroup *ngroup;
    int i;

    if (flow_context->type != 9 &&
        flow_context->type != ROCKER_GROUP_TYPE_GET(group->id)) {
        return;
    }

    ngroup = g_malloc0(sizeof(*ngroup));

    ngroup->id = group->id;

    ngroup->type = ROCKER_GROUP_TYPE_GET(group->id);

    switch (ngroup->type) {
    case ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE:
        ngroup->has_vlan_id = true;
        ngroup->vlan_id = ROCKER_GROUP_VLAN_GET(group->id);
        ngroup->has_pport = true;
        ngroup->pport = ROCKER_GROUP_PORT_GET(group->id);
        ngroup->has_out_pport = true;
        ngroup->out_pport = group->l2_interface.out_pport;
        ngroup->has_pop_vlan = true;
        ngroup->pop_vlan = group->l2_interface.pop_vlan;
        break;
    case ROCKER_OF_DPA_GROUP_TYPE_L2_REWRITE:
        ngroup->has_index = true;
        ngroup->index = ROCKER_GROUP_INDEX_LONG_GET(group->id);
        ngroup->has_group_id = true;
        ngroup->group_id = group->l2_rewrite.group_id;
        if (group->l2_rewrite.vlan_id) {
            ngroup->has_set_vlan_id = true;
            ngroup->set_vlan_id = ntohs(group->l2_rewrite.vlan_id);
        }
        if (memcmp(group->l2_rewrite.src_mac.a, zero_mac.a, ETH_ALEN)) {
            ngroup->set_eth_src =
                qemu_mac_strdup_printf(group->l2_rewrite.src_mac.a);
        }
        if (memcmp(group->l2_rewrite.dst_mac.a, zero_mac.a, ETH_ALEN)) {
            ngroup->set_eth_dst =
                qemu_mac_strdup_printf(group->l2_rewrite.dst_mac.a);
        }
        break;
    case ROCKER_OF_DPA_GROUP_TYPE_L2_FLOOD:
    case ROCKER_OF_DPA_GROUP_TYPE_L2_MCAST:
        ngroup->has_vlan_id = true;
        ngroup->vlan_id = ROCKER_GROUP_VLAN_GET(group->id);
        ngroup->has_index = true;
        ngroup->index = ROCKER_GROUP_INDEX_GET(group->id);
        for (i = 0; i < group->l2_flood.group_count; i++) {
            ngroup->has_group_ids = true;
            QAPI_LIST_PREPEND(ngroup->group_ids, group->l2_flood.group_ids[i]);
        }
        break;
    case ROCKER_OF_DPA_GROUP_TYPE_L3_UCAST:
        ngroup->has_index = true;
        ngroup->index = ROCKER_GROUP_INDEX_LONG_GET(group->id);
        ngroup->has_group_id = true;
        ngroup->group_id = group->l3_unicast.group_id;
        if (group->l3_unicast.vlan_id) {
            ngroup->has_set_vlan_id = true;
            ngroup->set_vlan_id = ntohs(group->l3_unicast.vlan_id);
        }
        if (memcmp(group->l3_unicast.src_mac.a, zero_mac.a, ETH_ALEN)) {
            ngroup->set_eth_src =
                qemu_mac_strdup_printf(group->l3_unicast.src_mac.a);
        }
        if (memcmp(group->l3_unicast.dst_mac.a, zero_mac.a, ETH_ALEN)) {
            ngroup->set_eth_dst =
                qemu_mac_strdup_printf(group->l3_unicast.dst_mac.a);
        }
        if (group->l3_unicast.ttl_check) {
            ngroup->has_ttl_check = true;
            ngroup->ttl_check = group->l3_unicast.ttl_check;
        }
        break;
    }

    QAPI_LIST_PREPEND(flow_context->list, ngroup);
}

RockerOfDpaGroupList *qmp_query_rocker_of_dpa_groups(const char *name,
                                                     bool has_type,
                                                     uint8_t type,
                                                     Error **errp)
{
    struct rocker *r;
    struct world *w;
    struct of_dpa *of_dpa;
    struct of_dpa_group_fill_context fill_context = {
        .list = NULL,
        .type = type,
    };

    r = rocker_find(name);
    if (!r) {
        error_setg(errp, "rocker %s not found", name);
        return NULL;
    }

    w = rocker_get_world(r, ROCKER_WORLD_TYPE_OF_DPA);
    if (!w) {
        error_setg(errp, "rocker %s doesn't have OF-DPA world", name);
        return NULL;
    }

    of_dpa = world_private(w);

    g_hash_table_foreach(of_dpa->group_tbl, of_dpa_group_fill, &fill_context);

    return fill_context.list;
}

static WorldOps of_dpa_ops = {
    .name = "ofdpa",
    .init = of_dpa_init,
    .uninit = of_dpa_uninit,
    .ig = of_dpa_ig,
    .cmd = of_dpa_cmd,
};

World *of_dpa_world_alloc(Rocker *r)
{
    return world_alloc(r, sizeof(OfDpa), ROCKER_WORLD_TYPE_OF_DPA, &of_dpa_ops);
}
