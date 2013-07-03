/*
 * QEMU network structures definitions and helper functions
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Portions developed by Free Software Foundation, Inc
 * Copyright (C) 1991-1997, 2001, 2003, 2006 Free Software Foundation, Inc.
 * See netinet/ip6.h and netinet/in.h (GNU C Library)
 *
 * Portions developed by Igor Kovalenko
 * Copyright (c) 2006 Igor Kovalenko
 * See hw/rtl8139.c (QEMU)
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

#ifndef QEMU_ETH_H
#define QEMU_ETH_H

#include <sys/types.h>
#include <string.h>
#include "qemu/bswap.h"
#include "qemu/iov.h"

#define ETH_ALEN 6

struct eth_header {
    uint8_t  h_dest[ETH_ALEN];   /* destination eth addr */
    uint8_t  h_source[ETH_ALEN]; /* source ether addr    */
    uint16_t h_proto;            /* packet type ID field */
};

struct vlan_header {
    uint16_t  h_tci;     /* priority and VLAN ID  */
    uint16_t  h_proto;   /* encapsulated protocol */
};

struct ip_header {
    uint8_t  ip_ver_len;     /* version and header length */
    uint8_t  ip_tos;         /* type of service */
    uint16_t ip_len;         /* total length */
    uint16_t ip_id;          /* identification */
    uint16_t ip_off;         /* fragment offset field */
    uint8_t  ip_ttl;         /* time to live */
    uint8_t  ip_p;           /* protocol */
    uint16_t ip_sum;         /* checksum */
    uint32_t ip_src, ip_dst; /* source and destination address */
};

typedef struct tcp_header {
    uint16_t th_sport;          /* source port */
    uint16_t th_dport;          /* destination port */
    uint32_t th_seq;            /* sequence number */
    uint32_t th_ack;            /* acknowledgment number */
    uint16_t th_offset_flags;   /* data offset, reserved 6 bits, */
                                /* TCP protocol flags */
    uint16_t th_win;            /* window */
    uint16_t th_sum;            /* checksum */
    uint16_t th_urp;            /* urgent pointer */
} tcp_header;

typedef struct udp_header {
    uint16_t uh_sport; /* source port */
    uint16_t uh_dport; /* destination port */
    uint16_t uh_ulen;  /* udp length */
    uint16_t uh_sum;   /* udp checksum */
} udp_header;

typedef struct ip_pseudo_header {
    uint32_t ip_src;
    uint32_t ip_dst;
    uint8_t  zeros;
    uint8_t  ip_proto;
    uint16_t ip_payload;
} ip_pseudo_header;

/* IPv6 address */
struct in6_addr {
    union {
        uint8_t __u6_addr8[16];
    } __in6_u;
};

struct ip6_header {
    union {
        struct ip6_hdrctl {
            uint32_t ip6_un1_flow; /* 4 bits version, 8 bits TC,
                                      20 bits flow-ID */
            uint16_t ip6_un1_plen; /* payload length */
            uint8_t  ip6_un1_nxt;  /* next header */
            uint8_t  ip6_un1_hlim; /* hop limit */
        } ip6_un1;
        uint8_t ip6_un2_vfc;       /* 4 bits version, top 4 bits tclass */
        struct ip6_ecn_access {
            uint8_t  ip6_un3_vfc;  /* 4 bits version, top 4 bits tclass */
            uint8_t  ip6_un3_ecn;  /* 2 bits ECN, top 6 bits payload length */
        } ip6_un3;
    } ip6_ctlun;
    struct in6_addr ip6_src;     /* source address */
    struct in6_addr ip6_dst;     /* destination address */
};

struct ip6_ext_hdr {
    uint8_t        ip6r_nxt;   /* next header */
    uint8_t        ip6r_len;   /* length in units of 8 octets */
};

struct udp_hdr {
  uint16_t uh_sport;           /* source port */
  uint16_t uh_dport;           /* destination port */
  uint16_t uh_ulen;            /* udp length */
  uint16_t uh_sum;             /* udp checksum */
};

struct tcp_hdr {
    u_short     th_sport;   /* source port */
    u_short     th_dport;   /* destination port */
    uint32_t    th_seq;     /* sequence number */
    uint32_t    th_ack;     /* acknowledgment number */
#ifdef HOST_WORDS_BIGENDIAN
    u_char  th_off : 4,     /* data offset */
        th_x2:4;            /* (unused) */
#else
    u_char  th_x2 : 4,      /* (unused) */
        th_off:4;           /* data offset */
#endif

#define TH_ELN  0x1 /* explicit loss notification */
#define TH_ECN  0x2 /* explicit congestion notification */
#define TH_FS   0x4 /* fast start */

    u_char  th_flags;
#define TH_FIN  0x01
#define TH_SYN  0x02
#define TH_RST  0x04
#define TH_PUSH 0x08
#define TH_ACK  0x10
#define TH_URG  0x20
    u_short th_win;      /* window */
    u_short th_sum;      /* checksum */
    u_short th_urp;      /* urgent pointer */
};

#define ip6_nxt      ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_ecn_acc  ip6_ctlun.ip6_un3.ip6_un3_ecn

#define PKT_GET_ETH_HDR(p)        \
    ((struct eth_header *)(p))
#define PKT_GET_VLAN_HDR(p)       \
    ((struct vlan_header *) (((uint8_t *)(p)) + sizeof(struct eth_header)))
#define PKT_GET_DVLAN_HDR(p)       \
    (PKT_GET_VLAN_HDR(p) + 1)
#define PKT_GET_IP_HDR(p)         \
    ((struct ip_header *)(((uint8_t *)(p)) + eth_get_l2_hdr_length(p)))
#define IP_HDR_GET_LEN(p)         \
    ((((struct ip_header *)p)->ip_ver_len & 0x0F) << 2)
#define PKT_GET_IP_HDR_LEN(p)     \
    (IP_HDR_GET_LEN(PKT_GET_IP_HDR(p)))
#define PKT_GET_IP6_HDR(p)        \
    ((struct ip6_header *) (((uint8_t *)(p)) + eth_get_l2_hdr_length(p)))
#define IP_HEADER_VERSION(ip)     \
    ((ip->ip_ver_len >> 4)&0xf)

#define ETH_P_IP                  (0x0800)
#define ETH_P_IPV6                (0x86dd)
#define ETH_P_VLAN                (0x8100)
#define ETH_P_DVLAN               (0x88a8)
#define VLAN_VID_MASK             0x0fff
#define IP_HEADER_VERSION_4       (4)
#define IP_HEADER_VERSION_6       (6)
#define IP_PROTO_TCP              (6)
#define IP_PROTO_UDP              (17)
#define IPTOS_ECN_MASK            0x03
#define IPTOS_ECN(x)              ((x) & IPTOS_ECN_MASK)
#define IPTOS_ECN_CE              0x03
#define IP6_ECN_MASK              0xC0
#define IP6_ECN(x)                ((x) & IP6_ECN_MASK)
#define IP6_ECN_CE                0xC0
#define IP4_DONT_FRAGMENT_FLAG    (1 << 14)

#define IS_SPECIAL_VLAN_ID(x)     \
    (((x) == 0) || ((x) == 0xFFF))

#define ETH_MAX_L2_HDR_LEN  \
    (sizeof(struct eth_header) + 2 * sizeof(struct vlan_header))

#define ETH_MAX_IP4_HDR_LEN   (60)
#define ETH_MAX_IP_DGRAM_LEN  (0xFFFF)

#define IP_FRAG_UNIT_SIZE     (8)
#define IP_FRAG_ALIGN_SIZE(x) ((x) & ~0x7)
#define IP_RF                 0x8000           /* reserved fragment flag */
#define IP_DF                 0x4000           /* don't fragment flag */
#define IP_MF                 0x2000           /* more fragments flag */
#define IP_OFFMASK            0x1fff           /* mask for fragmenting bits */

#define IP6_EXT_GRANULARITY   (8)  /* Size granularity for
                                      IPv6 extension headers */

/* IP6 extension header types */
#define IP6_HOP_BY_HOP        (0)
#define IP6_ROUTING           (43)
#define IP6_FRAGMENT          (44)
#define IP6_ESP               (50)
#define IP6_AUTHENTICATION    (51)
#define IP6_NONE              (59)
#define IP6_DESTINATON        (60)
#define IP6_MOBILITY          (135)

static inline int is_multicast_ether_addr(const uint8_t *addr)
{
    return 0x01 & addr[0];
}

static inline int is_broadcast_ether_addr(const uint8_t *addr)
{
    return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] & addr[5]) == 0xff;
}

static inline int is_unicast_ether_addr(const uint8_t *addr)
{
    return !is_multicast_ether_addr(addr);
}

typedef enum {
    ETH_PKT_UCAST = 0xAABBCC00,
    ETH_PKT_BCAST,
    ETH_PKT_MCAST
} eth_pkt_types_e;

static inline eth_pkt_types_e
get_eth_packet_type(const struct eth_header *ehdr)
{
    if (is_broadcast_ether_addr(ehdr->h_dest)) {
        return ETH_PKT_BCAST;
    } else if (is_multicast_ether_addr(ehdr->h_dest)) {
        return ETH_PKT_MCAST;
    } else { /* unicast */
        return ETH_PKT_UCAST;
    }
}

static inline uint32_t
eth_get_l2_hdr_length(const void *p)
{
    uint16_t proto = be16_to_cpu(PKT_GET_ETH_HDR(p)->h_proto);
    struct vlan_header *hvlan = PKT_GET_VLAN_HDR(p);
    switch (proto) {
    case ETH_P_VLAN:
        return sizeof(struct eth_header) + sizeof(struct vlan_header);
    case ETH_P_DVLAN:
        if (hvlan->h_proto == ETH_P_VLAN) {
            return sizeof(struct eth_header) + 2 * sizeof(struct vlan_header);
        } else {
            return sizeof(struct eth_header) + sizeof(struct vlan_header);
        }
    default:
        return sizeof(struct eth_header);
    }
}

static inline uint16_t
eth_get_pkt_tci(const void *p)
{
    uint16_t proto = be16_to_cpu(PKT_GET_ETH_HDR(p)->h_proto);
    struct vlan_header *hvlan = PKT_GET_VLAN_HDR(p);
    switch (proto) {
    case ETH_P_VLAN:
    case ETH_P_DVLAN:
        return be16_to_cpu(hvlan->h_tci);
    default:
        return 0;
    }
}

static inline bool
eth_strip_vlan(const void *p, uint8_t *new_ehdr_buf,
               uint16_t *payload_offset, uint16_t *tci)
{
    uint16_t proto = be16_to_cpu(PKT_GET_ETH_HDR(p)->h_proto);
    struct vlan_header *hvlan = PKT_GET_VLAN_HDR(p);
    struct eth_header *new_ehdr = (struct eth_header *) new_ehdr_buf;

    switch (proto) {
    case ETH_P_VLAN:
    case ETH_P_DVLAN:
        memcpy(new_ehdr->h_source, PKT_GET_ETH_HDR(p)->h_source, ETH_ALEN);
        memcpy(new_ehdr->h_dest, PKT_GET_ETH_HDR(p)->h_dest, ETH_ALEN);
        new_ehdr->h_proto = hvlan->h_proto;
        *tci = be16_to_cpu(hvlan->h_tci);
        *payload_offset =
            sizeof(struct eth_header) + sizeof(struct vlan_header);
        if (be16_to_cpu(new_ehdr->h_proto) == ETH_P_VLAN) {
            memcpy(PKT_GET_VLAN_HDR(new_ehdr),
                   PKT_GET_DVLAN_HDR(p),
                   sizeof(struct vlan_header));
            *payload_offset += sizeof(struct vlan_header);
        }
        return true;
    default:
        return false;
    }
}

static inline uint16_t
eth_get_l3_proto(const void *l2hdr, size_t l2hdr_len)
{
    uint8_t *proto_ptr = (uint8_t *) l2hdr + l2hdr_len - sizeof(uint16_t);
    return be16_to_cpup((uint16_t *)proto_ptr);
}

void eth_setup_vlan_headers(struct eth_header *ehdr, uint16_t vlan_tag,
    bool *is_new);

uint8_t eth_get_gso_type(uint16_t l3_proto, uint8_t *l3_hdr, uint8_t l4proto);

void eth_get_protocols(const uint8_t *headers,
                       uint32_t hdr_length,
                       bool *isip4, bool *isip6,
                       bool *isudp, bool *istcp);

void eth_setup_ip4_fragmentation(const void *l2hdr, size_t l2hdr_len,
                                 void *l3hdr, size_t l3hdr_len,
                                 size_t l3payload_len,
                                 size_t frag_offset, bool more_frags);

void
eth_fix_ip4_checksum(void *l3hdr, size_t l3hdr_len);

uint32_t
eth_calc_pseudo_hdr_csum(struct ip_header *iphdr, uint16_t csl);

bool
eth_parse_ipv6_hdr(struct iovec *pkt, int pkt_frags,
                   size_t ip6hdr_off, uint8_t *l4proto,
                   size_t *full_hdr_len);

#endif
