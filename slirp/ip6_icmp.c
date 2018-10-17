/*
 * Copyright (c) 2013
 * Guillaume Subiron, Yann Bordenave, Serigne Modou Wagne.
 */

#include "qemu/osdep.h"
#include "slirp.h"
#include "ip6_icmp.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/log.h"

#define NDP_Interval g_rand_int_range(slirp->grand, \
        NDP_MinRtrAdvInterval, NDP_MaxRtrAdvInterval)

static void ra_timer_handler(void *opaque)
{
    Slirp *slirp = opaque;
    timer_mod(slirp->ra_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + NDP_Interval);
    ndp_send_ra(slirp);
}

void icmp6_init(Slirp *slirp)
{
    if (!slirp->in6_enabled) {
        return;
    }

    slirp->ra_timer = timer_new_full(NULL, QEMU_CLOCK_VIRTUAL,
                                     SCALE_MS, QEMU_TIMER_ATTR_EXTERNAL,
                                     ra_timer_handler, slirp);
    timer_mod(slirp->ra_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + NDP_Interval);
}

void icmp6_cleanup(Slirp *slirp)
{
    if (!slirp->in6_enabled) {
        return;
    }

    timer_del(slirp->ra_timer);
    timer_free(slirp->ra_timer);
}

static void icmp6_send_echoreply(struct mbuf *m, Slirp *slirp, struct ip6 *ip,
        struct icmp6 *icmp)
{
    struct mbuf *t = m_get(slirp);
    t->m_len = sizeof(struct ip6) + ntohs(ip->ip_pl);
    memcpy(t->m_data, m->m_data, t->m_len);

    /* IPv6 Packet */
    struct ip6 *rip = mtod(t, struct ip6 *);
    rip->ip_dst = ip->ip_src;
    rip->ip_src = ip->ip_dst;

    /* ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = ICMP6_ECHO_REPLY;
    ricmp->icmp6_cksum = 0;

    /* Checksum */
    t->m_data -= sizeof(struct ip6);
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 0);
}

void icmp6_send_error(struct mbuf *m, uint8_t type, uint8_t code)
{
    Slirp *slirp = m->slirp;
    struct mbuf *t;
    struct ip6 *ip = mtod(m, struct ip6 *);

    DEBUG_CALL("icmp6_send_error");
    DEBUG_ARGS((dfd, " type = %d, code = %d\n", type, code));

    if (IN6_IS_ADDR_MULTICAST(&ip->ip_src) ||
            in6_zero(&ip->ip_src)) {
        /* TODO icmp error? */
        return;
    }

    t = m_get(slirp);

    /* IPv6 packet */
    struct ip6 *rip = mtod(t, struct ip6 *);
    rip->ip_src = (struct in6_addr)LINKLOCAL_ADDR;
    rip->ip_dst = ip->ip_src;
#if !defined(_WIN32) || (_WIN32_WINNT >= 0x0600)
    char addrstr[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &rip->ip_dst, addrstr, INET6_ADDRSTRLEN);
    DEBUG_ARG("target = %s", addrstr);
#endif

    rip->ip_nh = IPPROTO_ICMPV6;
    const int error_data_len = MIN(m->m_len,
            IF_MTU - (sizeof(struct ip6) + ICMP6_ERROR_MINLEN));
    rip->ip_pl = htons(ICMP6_ERROR_MINLEN + error_data_len);
    t->m_len = sizeof(struct ip6) + ntohs(rip->ip_pl);

    /* ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = type;
    ricmp->icmp6_code = code;
    ricmp->icmp6_cksum = 0;

    switch (type) {
    case ICMP6_UNREACH:
    case ICMP6_TIMXCEED:
        ricmp->icmp6_err.unused = 0;
        break;
    case ICMP6_TOOBIG:
        ricmp->icmp6_err.mtu = htonl(IF_MTU);
        break;
    case ICMP6_PARAMPROB:
        /* TODO: Handle this case */
        break;
    default:
        g_assert_not_reached();
        break;
    }
    t->m_data += ICMP6_ERROR_MINLEN;
    memcpy(t->m_data, m->m_data, error_data_len);

    /* Checksum */
    t->m_data -= ICMP6_ERROR_MINLEN;
    t->m_data -= sizeof(struct ip6);
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 0);
}

/*
 * Send NDP Router Advertisement
 */
void ndp_send_ra(Slirp *slirp)
{
    DEBUG_CALL("ndp_send_ra");

    /* Build IPv6 packet */
    struct mbuf *t = m_get(slirp);
    struct ip6 *rip = mtod(t, struct ip6 *);
    size_t pl_size = 0;
    struct in6_addr addr;
    uint32_t scope_id;

    rip->ip_src = (struct in6_addr)LINKLOCAL_ADDR;
    rip->ip_dst = (struct in6_addr)ALLNODES_MULTICAST;
    rip->ip_nh = IPPROTO_ICMPV6;

    /* Build ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = ICMP6_NDP_RA;
    ricmp->icmp6_code = 0;
    ricmp->icmp6_cksum = 0;

    /* NDP */
    ricmp->icmp6_nra.chl = NDP_AdvCurHopLimit;
    ricmp->icmp6_nra.M = NDP_AdvManagedFlag;
    ricmp->icmp6_nra.O = NDP_AdvOtherConfigFlag;
    ricmp->icmp6_nra.reserved = 0;
    ricmp->icmp6_nra.lifetime = htons(NDP_AdvDefaultLifetime);
    ricmp->icmp6_nra.reach_time = htonl(NDP_AdvReachableTime);
    ricmp->icmp6_nra.retrans_time = htonl(NDP_AdvRetransTime);
    t->m_data += ICMP6_NDP_RA_MINLEN;
    pl_size += ICMP6_NDP_RA_MINLEN;

    /* Source link-layer address (NDP option) */
    struct ndpopt *opt = mtod(t, struct ndpopt *);
    opt->ndpopt_type = NDPOPT_LINKLAYER_SOURCE;
    opt->ndpopt_len = NDPOPT_LINKLAYER_LEN / 8;
    in6_compute_ethaddr(rip->ip_src, opt->ndpopt_linklayer);
    t->m_data += NDPOPT_LINKLAYER_LEN;
    pl_size += NDPOPT_LINKLAYER_LEN;

    /* Prefix information (NDP option) */
    struct ndpopt *opt2 = mtod(t, struct ndpopt *);
    opt2->ndpopt_type = NDPOPT_PREFIX_INFO;
    opt2->ndpopt_len = NDPOPT_PREFIXINFO_LEN / 8;
    opt2->ndpopt_prefixinfo.prefix_length = slirp->vprefix_len;
    opt2->ndpopt_prefixinfo.L = 1;
    opt2->ndpopt_prefixinfo.A = 1;
    opt2->ndpopt_prefixinfo.reserved1 = 0;
    opt2->ndpopt_prefixinfo.valid_lt = htonl(NDP_AdvValidLifetime);
    opt2->ndpopt_prefixinfo.pref_lt = htonl(NDP_AdvPrefLifetime);
    opt2->ndpopt_prefixinfo.reserved2 = 0;
    opt2->ndpopt_prefixinfo.prefix = slirp->vprefix_addr6;
    t->m_data += NDPOPT_PREFIXINFO_LEN;
    pl_size += NDPOPT_PREFIXINFO_LEN;

    /* Prefix information (NDP option) */
    if (get_dns6_addr(&addr, &scope_id) >= 0) {
        /* Host system does have an IPv6 DNS server, announce our proxy.  */
        struct ndpopt *opt3 = mtod(t, struct ndpopt *);
        opt3->ndpopt_type = NDPOPT_RDNSS;
        opt3->ndpopt_len = NDPOPT_RDNSS_LEN / 8;
        opt3->ndpopt_rdnss.reserved = 0;
        opt3->ndpopt_rdnss.lifetime = htonl(2 * NDP_MaxRtrAdvInterval);
        opt3->ndpopt_rdnss.addr = slirp->vnameserver_addr6;
        t->m_data += NDPOPT_RDNSS_LEN;
        pl_size += NDPOPT_RDNSS_LEN;
    }

    rip->ip_pl = htons(pl_size);
    t->m_data -= sizeof(struct ip6) + pl_size;
    t->m_len = sizeof(struct ip6) + pl_size;

    /* ICMPv6 Checksum */
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 0);
}

/*
 * Send NDP Neighbor Solitication
 */
void ndp_send_ns(Slirp *slirp, struct in6_addr addr)
{
    DEBUG_CALL("ndp_send_ns");
#if !defined(_WIN32) || (_WIN32_WINNT >= 0x0600)
    char addrstr[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &addr, addrstr, INET6_ADDRSTRLEN);
    DEBUG_ARG("target = %s", addrstr);
#endif

    /* Build IPv6 packet */
    struct mbuf *t = m_get(slirp);
    struct ip6 *rip = mtod(t, struct ip6 *);
    rip->ip_src = slirp->vhost_addr6;
    rip->ip_dst = (struct in6_addr)SOLICITED_NODE_PREFIX;
    memcpy(&rip->ip_dst.s6_addr[13], &addr.s6_addr[13], 3);
    rip->ip_nh = IPPROTO_ICMPV6;
    rip->ip_pl = htons(ICMP6_NDP_NS_MINLEN + NDPOPT_LINKLAYER_LEN);
    t->m_len = sizeof(struct ip6) + ntohs(rip->ip_pl);

    /* Build ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = ICMP6_NDP_NS;
    ricmp->icmp6_code = 0;
    ricmp->icmp6_cksum = 0;

    /* NDP */
    ricmp->icmp6_nns.reserved = 0;
    ricmp->icmp6_nns.target = addr;

    /* Build NDP option */
    t->m_data += ICMP6_NDP_NS_MINLEN;
    struct ndpopt *opt = mtod(t, struct ndpopt *);
    opt->ndpopt_type = NDPOPT_LINKLAYER_SOURCE;
    opt->ndpopt_len = NDPOPT_LINKLAYER_LEN / 8;
    in6_compute_ethaddr(slirp->vhost_addr6, opt->ndpopt_linklayer);

    /* ICMPv6 Checksum */
    t->m_data -= ICMP6_NDP_NA_MINLEN;
    t->m_data -= sizeof(struct ip6);
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 1);
}

/*
 * Send NDP Neighbor Advertisement
 */
static void ndp_send_na(Slirp *slirp, struct ip6 *ip, struct icmp6 *icmp)
{
    /* Build IPv6 packet */
    struct mbuf *t = m_get(slirp);
    struct ip6 *rip = mtod(t, struct ip6 *);
    rip->ip_src = icmp->icmp6_nns.target;
    if (in6_zero(&ip->ip_src)) {
        rip->ip_dst = (struct in6_addr)ALLNODES_MULTICAST;
    } else {
        rip->ip_dst = ip->ip_src;
    }
    rip->ip_nh = IPPROTO_ICMPV6;
    rip->ip_pl = htons(ICMP6_NDP_NA_MINLEN
                        + NDPOPT_LINKLAYER_LEN);
    t->m_len = sizeof(struct ip6) + ntohs(rip->ip_pl);

    /* Build ICMPv6 packet */
    t->m_data += sizeof(struct ip6);
    struct icmp6 *ricmp = mtod(t, struct icmp6 *);
    ricmp->icmp6_type = ICMP6_NDP_NA;
    ricmp->icmp6_code = 0;
    ricmp->icmp6_cksum = 0;

    /* NDP */
    ricmp->icmp6_nna.R = NDP_IsRouter;
    ricmp->icmp6_nna.S = !IN6_IS_ADDR_MULTICAST(&rip->ip_dst);
    ricmp->icmp6_nna.O = 1;
    ricmp->icmp6_nna.reserved_hi = 0;
    ricmp->icmp6_nna.reserved_lo = 0;
    ricmp->icmp6_nna.target = icmp->icmp6_nns.target;

    /* Build NDP option */
    t->m_data += ICMP6_NDP_NA_MINLEN;
    struct ndpopt *opt = mtod(t, struct ndpopt *);
    opt->ndpopt_type = NDPOPT_LINKLAYER_TARGET;
    opt->ndpopt_len = NDPOPT_LINKLAYER_LEN / 8;
    in6_compute_ethaddr(ricmp->icmp6_nna.target,
                    opt->ndpopt_linklayer);

    /* ICMPv6 Checksum */
    t->m_data -= ICMP6_NDP_NA_MINLEN;
    t->m_data -= sizeof(struct ip6);
    ricmp->icmp6_cksum = ip6_cksum(t);

    ip6_output(NULL, t, 0);
}

/*
 * Process a NDP message
 */
static void ndp_input(struct mbuf *m, Slirp *slirp, struct ip6 *ip,
        struct icmp6 *icmp)
{
    m->m_len += ETH_HLEN;
    m->m_data -= ETH_HLEN;
    struct ethhdr *eth = mtod(m, struct ethhdr *);
    m->m_len -= ETH_HLEN;
    m->m_data += ETH_HLEN;

    switch (icmp->icmp6_type) {
    case ICMP6_NDP_RS:
        DEBUG_CALL(" type = Router Solicitation");
        if (ip->ip_hl == 255
                && icmp->icmp6_code == 0
                && ntohs(ip->ip_pl) >= ICMP6_NDP_RS_MINLEN) {
            /* Gratuitous NDP */
            ndp_table_add(slirp, ip->ip_src, eth->h_source);

            ndp_send_ra(slirp);
        }
        break;

    case ICMP6_NDP_RA:
        DEBUG_CALL(" type = Router Advertisement");
        qemu_log_mask(LOG_GUEST_ERROR,
                "Warning: guest sent NDP RA, but shouldn't");
        break;

    case ICMP6_NDP_NS:
        DEBUG_CALL(" type = Neighbor Solicitation");
        if (ip->ip_hl == 255
                && icmp->icmp6_code == 0
                && !IN6_IS_ADDR_MULTICAST(&icmp->icmp6_nns.target)
                && ntohs(ip->ip_pl) >= ICMP6_NDP_NS_MINLEN
                && (!in6_zero(&ip->ip_src)
                    || in6_solicitednode_multicast(&ip->ip_dst))) {
            if (in6_equal_host(&icmp->icmp6_nns.target)) {
                /* Gratuitous NDP */
                ndp_table_add(slirp, ip->ip_src, eth->h_source);
                ndp_send_na(slirp, ip, icmp);
            }
        }
        break;

    case ICMP6_NDP_NA:
        DEBUG_CALL(" type = Neighbor Advertisement");
        if (ip->ip_hl == 255
                && icmp->icmp6_code == 0
                && ntohs(ip->ip_pl) >= ICMP6_NDP_NA_MINLEN
                && !IN6_IS_ADDR_MULTICAST(&icmp->icmp6_nna.target)
                && (!IN6_IS_ADDR_MULTICAST(&ip->ip_dst)
                    || icmp->icmp6_nna.S == 0)) {
            ndp_table_add(slirp, ip->ip_src, eth->h_source);
        }
        break;

    case ICMP6_NDP_REDIRECT:
        DEBUG_CALL(" type = Redirect");
        qemu_log_mask(LOG_GUEST_ERROR,
                "Warning: guest sent NDP REDIRECT, but shouldn't");
        break;
    }
}

/*
 * Process a received ICMPv6 message.
 */
void icmp6_input(struct mbuf *m)
{
    struct icmp6 *icmp;
    struct ip6 *ip = mtod(m, struct ip6 *);
    Slirp *slirp = m->slirp;
    int hlen = sizeof(struct ip6);

    DEBUG_CALL("icmp6_input");
    DEBUG_ARG("m = %lx", (long) m);
    DEBUG_ARG("m_len = %d", m->m_len);

    if (ntohs(ip->ip_pl) < ICMP6_MINLEN) {
        goto end;
    }

    if (ip6_cksum(m)) {
        goto end;
    }

    m->m_len -= hlen;
    m->m_data += hlen;
    icmp = mtod(m, struct icmp6 *);
    m->m_len += hlen;
    m->m_data -= hlen;

    DEBUG_ARG("icmp6_type = %d", icmp->icmp6_type);
    switch (icmp->icmp6_type) {
    case ICMP6_ECHO_REQUEST:
        if (in6_equal_host(&ip->ip_dst)) {
            icmp6_send_echoreply(m, slirp, ip, icmp);
        } else {
            /* TODO */
            error_report("external icmpv6 not supported yet");
        }
        break;

    case ICMP6_NDP_RS:
    case ICMP6_NDP_RA:
    case ICMP6_NDP_NS:
    case ICMP6_NDP_NA:
    case ICMP6_NDP_REDIRECT:
        ndp_input(m, slirp, ip, icmp);
        break;

    case ICMP6_UNREACH:
    case ICMP6_TOOBIG:
    case ICMP6_TIMXCEED:
    case ICMP6_PARAMPROB:
        /* XXX? report error? close socket? */
    default:
        break;
    }

end:
    m_free(m);
}
