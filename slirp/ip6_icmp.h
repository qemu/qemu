/*
 * Copyright (c) 2013
 * Guillaume Subiron, Yann Bordenave, Serigne Modou Wagne.
 */

#ifndef SLIRP_IP6_ICMP_H
#define SLIRP_IP6_ICMP_H

/*
 * Interface Control Message Protocol version 6 Definitions.
 * Per RFC 4443, March 2006.
 *
 * Network Discover Protocol Definitions.
 * Per RFC 4861, September 2007.
 */

struct icmp6_echo { /* Echo Messages */
    uint16_t id;
    uint16_t seq_num;
};

union icmp6_error_body {
    uint32_t unused;
    uint32_t pointer;
    uint32_t mtu;
};

/*
 * NDP Messages
 */
struct ndp_rs {     /* Router Solicitation Message */
    uint32_t reserved;
};

struct ndp_ra {     /* Router Advertisement Message */
    uint8_t chl;    /* Cur Hop Limit */
#ifdef HOST_WORDS_BIGENDIAN
    uint8_t
        M:1,
        O:1,
        reserved:6;
#else
    uint8_t
        reserved:6,
        O:1,
        M:1;
#endif
    uint16_t lifetime;      /* Router Lifetime */
    uint32_t reach_time;    /* Reachable Time */
    uint32_t retrans_time;  /* Retrans Timer */
} QEMU_PACKED;

struct ndp_ns {     /* Neighbor Solicitation Message */
    uint32_t reserved;
    struct in6_addr target; /* Target Address */
} QEMU_PACKED;

struct ndp_na {     /* Neighbor Advertisement Message */
#ifdef HOST_WORDS_BIGENDIAN
    uint32_t
        R:1,                /* Router Flag */
        S:1,                /* Solicited Flag */
        O:1,                /* Override Flag */
        reserved_hi:5,
        reserved_lo:24;
#else
    uint32_t
        reserved_hi:5,
        O:1,
        S:1,
        R:1,
        reserved_lo:24;
#endif
    struct in6_addr target; /* Target Address */
} QEMU_PACKED;

struct ndp_redirect {
    uint32_t reserved;
    struct in6_addr target; /* Target Address */
    struct in6_addr dest;   /* Destination Address */
} QEMU_PACKED;

/*
 * Structure of an icmpv6 header.
 */
struct icmp6 {
    uint8_t     icmp6_type;         /* type of message, see below */
    uint8_t     icmp6_code;         /* type sub code */
    uint16_t    icmp6_cksum;        /* ones complement cksum of struct */
    union {
        union icmp6_error_body error_body;
        struct icmp6_echo echo;
        struct ndp_rs ndp_rs;
        struct ndp_ra ndp_ra;
        struct ndp_ns ndp_ns;
        struct ndp_na ndp_na;
        struct ndp_redirect ndp_redirect;
    } icmp6_body;
#define icmp6_err icmp6_body.error_body
#define icmp6_echo icmp6_body.echo
#define icmp6_nrs icmp6_body.ndp_rs
#define icmp6_nra icmp6_body.ndp_ra
#define icmp6_nns icmp6_body.ndp_ns
#define icmp6_nna icmp6_body.ndp_na
#define icmp6_redirect icmp6_body.ndp_redirect
} QEMU_PACKED;

#define ICMP6_MINLEN    4
#define ICMP6_ERROR_MINLEN  8
#define ICMP6_ECHO_MINLEN   8
#define ICMP6_NDP_RS_MINLEN 8
#define ICMP6_NDP_RA_MINLEN 16
#define ICMP6_NDP_NS_MINLEN 24
#define ICMP6_NDP_NA_MINLEN 24
#define ICMP6_NDP_REDIRECT_MINLEN 40

/*
 * NDP Options
 */
struct ndpopt {
    uint8_t     ndpopt_type;                    /* Option type */
    uint8_t     ndpopt_len;                     /* /!\ In units of 8 octets */
    union {
        unsigned char   linklayer_addr[6];      /* Source/Target Link-layer */
#define ndpopt_linklayer ndpopt_body.linklayer_addr
        struct prefixinfo {                     /* Prefix Information */
            uint8_t     prefix_length;
#ifdef HOST_WORDS_BIGENDIAN
            uint8_t     L:1, A:1, reserved1:6;
#else
            uint8_t     reserved1:6, A:1, L:1;
#endif
            uint32_t    valid_lt;               /* Valid Lifetime */
            uint32_t    pref_lt;                /* Preferred Lifetime */
            uint32_t    reserved2;
            struct in6_addr prefix;
        } QEMU_PACKED prefixinfo;
#define ndpopt_prefixinfo ndpopt_body.prefixinfo
        struct rdnss {
            uint16_t reserved;
            uint32_t lifetime;
            struct in6_addr addr;
        } QEMU_PACKED rdnss;
#define ndpopt_rdnss ndpopt_body.rdnss
    } ndpopt_body;
} QEMU_PACKED;

/* NDP options type */
#define NDPOPT_LINKLAYER_SOURCE     1   /* Source Link-Layer Address */
#define NDPOPT_LINKLAYER_TARGET     2   /* Target Link-Layer Address */
#define NDPOPT_PREFIX_INFO          3   /* Prefix Information */
#define NDPOPT_RDNSS                25  /* Recursive DNS Server Address */

/* NDP options size, in octets. */
#define NDPOPT_LINKLAYER_LEN    8
#define NDPOPT_PREFIXINFO_LEN   32
#define NDPOPT_RDNSS_LEN        24

/*
 * Definition of type and code field values.
 * Per https://www.iana.org/assignments/icmpv6-parameters/icmpv6-parameters.xml
 * Last Updated 2012-11-12
 */

/* Errors */
#define ICMP6_UNREACH   1   /* Destination Unreachable */
#define     ICMP6_UNREACH_NO_ROUTE      0   /* no route to dest */
#define     ICMP6_UNREACH_DEST_PROHIB   1   /* com with dest prohibited */
#define     ICMP6_UNREACH_SCOPE         2   /* beyond scope of src addr */
#define     ICMP6_UNREACH_ADDRESS       3   /* address unreachable */
#define     ICMP6_UNREACH_PORT          4   /* port unreachable */
#define     ICMP6_UNREACH_SRC_FAIL      5   /* src addr failed */
#define     ICMP6_UNREACH_REJECT_ROUTE  6   /* reject route to dest */
#define     ICMP6_UNREACH_SRC_HDR_ERROR 7   /* error in src routing header */
#define ICMP6_TOOBIG    2   /* Packet Too Big */
#define ICMP6_TIMXCEED  3   /* Time Exceeded */
#define     ICMP6_TIMXCEED_INTRANS      0   /* hop limit exceeded in transit */
#define     ICMP6_TIMXCEED_REASS        1   /* ttl=0 in reass */
#define ICMP6_PARAMPROB 4   /* Parameter Problem */
#define     ICMP6_PARAMPROB_HDR_FIELD   0   /* err header field */
#define     ICMP6_PARAMPROB_NXTHDR_TYPE 1   /* unrecognized Next Header type */
#define     ICMP6_PARAMPROB_IPV6_OPT    2   /* unrecognized IPv6 option */

/* Informational Messages */
#define ICMP6_ECHO_REQUEST      128 /* Echo Request */
#define ICMP6_ECHO_REPLY        129 /* Echo Reply */
#define ICMP6_NDP_RS            133 /* Router Solicitation (NDP) */
#define ICMP6_NDP_RA            134 /* Router Advertisement (NDP) */
#define ICMP6_NDP_NS            135 /* Neighbor Solicitation (NDP) */
#define ICMP6_NDP_NA            136 /* Neighbor Advertisement (NDP) */
#define ICMP6_NDP_REDIRECT      137 /* Redirect Message (NDP) */

/*
 * Router Configuration Variables (rfc4861#section-6)
 */
#define NDP_IsRouter                1
#define NDP_AdvSendAdvertisements   1
#define NDP_MaxRtrAdvInterval       600000
#define NDP_MinRtrAdvInterval       ((NDP_MaxRtrAdvInterval >= 9) ? \
                                        NDP_MaxRtrAdvInterval / 3 : \
                                        NDP_MaxRtrAdvInterval)
#define NDP_AdvManagedFlag          0
#define NDP_AdvOtherConfigFlag      0
#define NDP_AdvLinkMTU              0
#define NDP_AdvReachableTime        0
#define NDP_AdvRetransTime          0
#define NDP_AdvCurHopLimit          64
#define NDP_AdvDefaultLifetime      ((3 * NDP_MaxRtrAdvInterval) / 1000)
#define NDP_AdvValidLifetime        86400
#define NDP_AdvOnLinkFlag           1
#define NDP_AdvPrefLifetime         14400
#define NDP_AdvAutonomousFlag       1

void icmp6_init(Slirp *slirp);
void icmp6_cleanup(Slirp *slirp);
void icmp6_input(struct mbuf *);
void icmp6_send_error(struct mbuf *m, uint8_t type, uint8_t code);
void ndp_send_ra(Slirp *slirp);
void ndp_send_ns(Slirp *slirp, struct in6_addr addr);

#endif
