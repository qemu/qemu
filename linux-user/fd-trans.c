/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include <sys/signalfd.h>
#include <linux/unistd.h>
#include <linux/audit.h>
#ifdef CONFIG_INOTIFY
#include <sys/inotify.h>
#endif
#include <linux/netlink.h>
#ifdef CONFIG_RTNETLINK
#include <linux/rtnetlink.h>
#include <linux/if_bridge.h>
#endif
#include "qemu.h"
#include "fd-trans.h"

enum {
    QEMU_IFLA_BR_UNSPEC,
    QEMU_IFLA_BR_FORWARD_DELAY,
    QEMU_IFLA_BR_HELLO_TIME,
    QEMU_IFLA_BR_MAX_AGE,
    QEMU_IFLA_BR_AGEING_TIME,
    QEMU_IFLA_BR_STP_STATE,
    QEMU_IFLA_BR_PRIORITY,
    QEMU_IFLA_BR_VLAN_FILTERING,
    QEMU_IFLA_BR_VLAN_PROTOCOL,
    QEMU_IFLA_BR_GROUP_FWD_MASK,
    QEMU_IFLA_BR_ROOT_ID,
    QEMU_IFLA_BR_BRIDGE_ID,
    QEMU_IFLA_BR_ROOT_PORT,
    QEMU_IFLA_BR_ROOT_PATH_COST,
    QEMU_IFLA_BR_TOPOLOGY_CHANGE,
    QEMU_IFLA_BR_TOPOLOGY_CHANGE_DETECTED,
    QEMU_IFLA_BR_HELLO_TIMER,
    QEMU_IFLA_BR_TCN_TIMER,
    QEMU_IFLA_BR_TOPOLOGY_CHANGE_TIMER,
    QEMU_IFLA_BR_GC_TIMER,
    QEMU_IFLA_BR_GROUP_ADDR,
    QEMU_IFLA_BR_FDB_FLUSH,
    QEMU_IFLA_BR_MCAST_ROUTER,
    QEMU_IFLA_BR_MCAST_SNOOPING,
    QEMU_IFLA_BR_MCAST_QUERY_USE_IFADDR,
    QEMU_IFLA_BR_MCAST_QUERIER,
    QEMU_IFLA_BR_MCAST_HASH_ELASTICITY,
    QEMU_IFLA_BR_MCAST_HASH_MAX,
    QEMU_IFLA_BR_MCAST_LAST_MEMBER_CNT,
    QEMU_IFLA_BR_MCAST_STARTUP_QUERY_CNT,
    QEMU_IFLA_BR_MCAST_LAST_MEMBER_INTVL,
    QEMU_IFLA_BR_MCAST_MEMBERSHIP_INTVL,
    QEMU_IFLA_BR_MCAST_QUERIER_INTVL,
    QEMU_IFLA_BR_MCAST_QUERY_INTVL,
    QEMU_IFLA_BR_MCAST_QUERY_RESPONSE_INTVL,
    QEMU_IFLA_BR_MCAST_STARTUP_QUERY_INTVL,
    QEMU_IFLA_BR_NF_CALL_IPTABLES,
    QEMU_IFLA_BR_NF_CALL_IP6TABLES,
    QEMU_IFLA_BR_NF_CALL_ARPTABLES,
    QEMU_IFLA_BR_VLAN_DEFAULT_PVID,
    QEMU_IFLA_BR_PAD,
    QEMU_IFLA_BR_VLAN_STATS_ENABLED,
    QEMU_IFLA_BR_MCAST_STATS_ENABLED,
    QEMU_IFLA_BR_MCAST_IGMP_VERSION,
    QEMU_IFLA_BR_MCAST_MLD_VERSION,
    QEMU___IFLA_BR_MAX,
};

enum {
    QEMU_IFLA_UNSPEC,
    QEMU_IFLA_ADDRESS,
    QEMU_IFLA_BROADCAST,
    QEMU_IFLA_IFNAME,
    QEMU_IFLA_MTU,
    QEMU_IFLA_LINK,
    QEMU_IFLA_QDISC,
    QEMU_IFLA_STATS,
    QEMU_IFLA_COST,
    QEMU_IFLA_PRIORITY,
    QEMU_IFLA_MASTER,
    QEMU_IFLA_WIRELESS,
    QEMU_IFLA_PROTINFO,
    QEMU_IFLA_TXQLEN,
    QEMU_IFLA_MAP,
    QEMU_IFLA_WEIGHT,
    QEMU_IFLA_OPERSTATE,
    QEMU_IFLA_LINKMODE,
    QEMU_IFLA_LINKINFO,
    QEMU_IFLA_NET_NS_PID,
    QEMU_IFLA_IFALIAS,
    QEMU_IFLA_NUM_VF,
    QEMU_IFLA_VFINFO_LIST,
    QEMU_IFLA_STATS64,
    QEMU_IFLA_VF_PORTS,
    QEMU_IFLA_PORT_SELF,
    QEMU_IFLA_AF_SPEC,
    QEMU_IFLA_GROUP,
    QEMU_IFLA_NET_NS_FD,
    QEMU_IFLA_EXT_MASK,
    QEMU_IFLA_PROMISCUITY,
    QEMU_IFLA_NUM_TX_QUEUES,
    QEMU_IFLA_NUM_RX_QUEUES,
    QEMU_IFLA_CARRIER,
    QEMU_IFLA_PHYS_PORT_ID,
    QEMU_IFLA_CARRIER_CHANGES,
    QEMU_IFLA_PHYS_SWITCH_ID,
    QEMU_IFLA_LINK_NETNSID,
    QEMU_IFLA_PHYS_PORT_NAME,
    QEMU_IFLA_PROTO_DOWN,
    QEMU_IFLA_GSO_MAX_SEGS,
    QEMU_IFLA_GSO_MAX_SIZE,
    QEMU_IFLA_PAD,
    QEMU_IFLA_XDP,
    QEMU_IFLA_EVENT,
    QEMU_IFLA_NEW_NETNSID,
    QEMU_IFLA_IF_NETNSID,
    QEMU_IFLA_CARRIER_UP_COUNT,
    QEMU_IFLA_CARRIER_DOWN_COUNT,
    QEMU_IFLA_NEW_IFINDEX,
    QEMU___IFLA_MAX
};

enum {
    QEMU_IFLA_BRPORT_UNSPEC,
    QEMU_IFLA_BRPORT_STATE,
    QEMU_IFLA_BRPORT_PRIORITY,
    QEMU_IFLA_BRPORT_COST,
    QEMU_IFLA_BRPORT_MODE,
    QEMU_IFLA_BRPORT_GUARD,
    QEMU_IFLA_BRPORT_PROTECT,
    QEMU_IFLA_BRPORT_FAST_LEAVE,
    QEMU_IFLA_BRPORT_LEARNING,
    QEMU_IFLA_BRPORT_UNICAST_FLOOD,
    QEMU_IFLA_BRPORT_PROXYARP,
    QEMU_IFLA_BRPORT_LEARNING_SYNC,
    QEMU_IFLA_BRPORT_PROXYARP_WIFI,
    QEMU_IFLA_BRPORT_ROOT_ID,
    QEMU_IFLA_BRPORT_BRIDGE_ID,
    QEMU_IFLA_BRPORT_DESIGNATED_PORT,
    QEMU_IFLA_BRPORT_DESIGNATED_COST,
    QEMU_IFLA_BRPORT_ID,
    QEMU_IFLA_BRPORT_NO,
    QEMU_IFLA_BRPORT_TOPOLOGY_CHANGE_ACK,
    QEMU_IFLA_BRPORT_CONFIG_PENDING,
    QEMU_IFLA_BRPORT_MESSAGE_AGE_TIMER,
    QEMU_IFLA_BRPORT_FORWARD_DELAY_TIMER,
    QEMU_IFLA_BRPORT_HOLD_TIMER,
    QEMU_IFLA_BRPORT_FLUSH,
    QEMU_IFLA_BRPORT_MULTICAST_ROUTER,
    QEMU_IFLA_BRPORT_PAD,
    QEMU_IFLA_BRPORT_MCAST_FLOOD,
    QEMU_IFLA_BRPORT_MCAST_TO_UCAST,
    QEMU_IFLA_BRPORT_VLAN_TUNNEL,
    QEMU_IFLA_BRPORT_BCAST_FLOOD,
    QEMU_IFLA_BRPORT_GROUP_FWD_MASK,
    QEMU_IFLA_BRPORT_NEIGH_SUPPRESS,
    QEMU___IFLA_BRPORT_MAX
};

enum {
    QEMU_IFLA_TUN_UNSPEC,
    QEMU_IFLA_TUN_OWNER,
    QEMU_IFLA_TUN_GROUP,
    QEMU_IFLA_TUN_TYPE,
    QEMU_IFLA_TUN_PI,
    QEMU_IFLA_TUN_VNET_HDR,
    QEMU_IFLA_TUN_PERSIST,
    QEMU_IFLA_TUN_MULTI_QUEUE,
    QEMU_IFLA_TUN_NUM_QUEUES,
    QEMU_IFLA_TUN_NUM_DISABLED_QUEUES,
    QEMU___IFLA_TUN_MAX,
};

enum {
    QEMU_IFLA_INFO_UNSPEC,
    QEMU_IFLA_INFO_KIND,
    QEMU_IFLA_INFO_DATA,
    QEMU_IFLA_INFO_XSTATS,
    QEMU_IFLA_INFO_SLAVE_KIND,
    QEMU_IFLA_INFO_SLAVE_DATA,
    QEMU___IFLA_INFO_MAX,
};

enum {
    QEMU_IFLA_INET_UNSPEC,
    QEMU_IFLA_INET_CONF,
    QEMU___IFLA_INET_MAX,
};

enum {
    QEMU_IFLA_INET6_UNSPEC,
    QEMU_IFLA_INET6_FLAGS,
    QEMU_IFLA_INET6_CONF,
    QEMU_IFLA_INET6_STATS,
    QEMU_IFLA_INET6_MCAST,
    QEMU_IFLA_INET6_CACHEINFO,
    QEMU_IFLA_INET6_ICMP6STATS,
    QEMU_IFLA_INET6_TOKEN,
    QEMU_IFLA_INET6_ADDR_GEN_MODE,
    QEMU___IFLA_INET6_MAX
};

enum {
    QEMU_IFLA_XDP_UNSPEC,
    QEMU_IFLA_XDP_FD,
    QEMU_IFLA_XDP_ATTACHED,
    QEMU_IFLA_XDP_FLAGS,
    QEMU_IFLA_XDP_PROG_ID,
    QEMU___IFLA_XDP_MAX,
};

enum {
    QEMU_RTA_UNSPEC,
    QEMU_RTA_DST,
    QEMU_RTA_SRC,
    QEMU_RTA_IIF,
    QEMU_RTA_OIF,
    QEMU_RTA_GATEWAY,
    QEMU_RTA_PRIORITY,
    QEMU_RTA_PREFSRC,
    QEMU_RTA_METRICS,
    QEMU_RTA_MULTIPATH,
    QEMU_RTA_PROTOINFO, /* no longer used */
    QEMU_RTA_FLOW,
    QEMU_RTA_CACHEINFO,
    QEMU_RTA_SESSION, /* no longer used */
    QEMU_RTA_MP_ALGO, /* no longer used */
    QEMU_RTA_TABLE,
    QEMU_RTA_MARK,
    QEMU_RTA_MFC_STATS,
    QEMU_RTA_VIA,
    QEMU_RTA_NEWDST,
    QEMU_RTA_PREF,
    QEMU_RTA_ENCAP_TYPE,
    QEMU_RTA_ENCAP,
    QEMU_RTA_EXPIRES,
    QEMU_RTA_PAD,
    QEMU_RTA_UID,
    QEMU_RTA_TTL_PROPAGATE,
    QEMU_RTA_IP_PROTO,
    QEMU_RTA_SPORT,
    QEMU_RTA_DPORT,
    QEMU___RTA_MAX
};

TargetFdTrans **target_fd_trans;
unsigned int target_fd_max;

static void tswap_nlmsghdr(struct nlmsghdr *nlh)
{
    nlh->nlmsg_len = tswap32(nlh->nlmsg_len);
    nlh->nlmsg_type = tswap16(nlh->nlmsg_type);
    nlh->nlmsg_flags = tswap16(nlh->nlmsg_flags);
    nlh->nlmsg_seq = tswap32(nlh->nlmsg_seq);
    nlh->nlmsg_pid = tswap32(nlh->nlmsg_pid);
}

static abi_long host_to_target_for_each_nlmsg(struct nlmsghdr *nlh,
                                              size_t len,
                                              abi_long (*host_to_target_nlmsg)
                                                       (struct nlmsghdr *))
{
    uint32_t nlmsg_len;
    abi_long ret;

    while (len > sizeof(struct nlmsghdr)) {

        nlmsg_len = nlh->nlmsg_len;
        if (nlmsg_len < sizeof(struct nlmsghdr) ||
            nlmsg_len > len) {
            break;
        }

        switch (nlh->nlmsg_type) {
        case NLMSG_DONE:
            tswap_nlmsghdr(nlh);
            return 0;
        case NLMSG_NOOP:
            break;
        case NLMSG_ERROR:
        {
            struct nlmsgerr *e = NLMSG_DATA(nlh);
            e->error = tswap32(e->error);
            tswap_nlmsghdr(&e->msg);
            tswap_nlmsghdr(nlh);
            return 0;
        }
        default:
            ret = host_to_target_nlmsg(nlh);
            if (ret < 0) {
                tswap_nlmsghdr(nlh);
                return ret;
            }
            break;
        }
        tswap_nlmsghdr(nlh);
        len -= NLMSG_ALIGN(nlmsg_len);
        nlh = (struct nlmsghdr *)(((char*)nlh) + NLMSG_ALIGN(nlmsg_len));
    }
    return 0;
}

static abi_long target_to_host_for_each_nlmsg(struct nlmsghdr *nlh,
                                              size_t len,
                                              abi_long (*target_to_host_nlmsg)
                                                       (struct nlmsghdr *))
{
    int ret;

    while (len > sizeof(struct nlmsghdr)) {
        if (tswap32(nlh->nlmsg_len) < sizeof(struct nlmsghdr) ||
            tswap32(nlh->nlmsg_len) > len) {
            break;
        }
        tswap_nlmsghdr(nlh);
        switch (nlh->nlmsg_type) {
        case NLMSG_DONE:
            return 0;
        case NLMSG_NOOP:
            break;
        case NLMSG_ERROR:
        {
            struct nlmsgerr *e = NLMSG_DATA(nlh);
            e->error = tswap32(e->error);
            tswap_nlmsghdr(&e->msg);
            return 0;
        }
        default:
            ret = target_to_host_nlmsg(nlh);
            if (ret < 0) {
                return ret;
            }
        }
        len -= NLMSG_ALIGN(nlh->nlmsg_len);
        nlh = (struct nlmsghdr *)(((char *)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    }
    return 0;
}

#ifdef CONFIG_RTNETLINK
static abi_long host_to_target_for_each_nlattr(struct nlattr *nlattr,
                                               size_t len, void *context,
                                               abi_long (*host_to_target_nlattr)
                                                        (struct nlattr *,
                                                         void *context))
{
    unsigned short nla_len;
    abi_long ret;

    while (len > sizeof(struct nlattr)) {
        nla_len = nlattr->nla_len;
        if (nla_len < sizeof(struct nlattr) ||
            nla_len > len) {
            break;
        }
        ret = host_to_target_nlattr(nlattr, context);
        nlattr->nla_len = tswap16(nlattr->nla_len);
        nlattr->nla_type = tswap16(nlattr->nla_type);
        if (ret < 0) {
            return ret;
        }
        len -= NLA_ALIGN(nla_len);
        nlattr = (struct nlattr *)(((char *)nlattr) + NLA_ALIGN(nla_len));
    }
    return 0;
}

static abi_long host_to_target_for_each_rtattr(struct rtattr *rtattr,
                                               size_t len,
                                               abi_long (*host_to_target_rtattr)
                                                        (struct rtattr *))
{
    unsigned short rta_len;
    abi_long ret;

    while (len > sizeof(struct rtattr)) {
        rta_len = rtattr->rta_len;
        if (rta_len < sizeof(struct rtattr) ||
            rta_len > len) {
            break;
        }
        ret = host_to_target_rtattr(rtattr);
        rtattr->rta_len = tswap16(rtattr->rta_len);
        rtattr->rta_type = tswap16(rtattr->rta_type);
        if (ret < 0) {
            return ret;
        }
        len -= RTA_ALIGN(rta_len);
        rtattr = (struct rtattr *)(((char *)rtattr) + RTA_ALIGN(rta_len));
    }
    return 0;
}

#define NLA_DATA(nla) ((void *)((char *)(nla)) + NLA_HDRLEN)

static abi_long host_to_target_data_bridge_nlattr(struct nlattr *nlattr,
                                                  void *context)
{
    uint16_t *u16;
    uint32_t *u32;
    uint64_t *u64;

    switch (nlattr->nla_type) {
    /* no data */
    case QEMU_IFLA_BR_FDB_FLUSH:
        break;
    /* binary */
    case QEMU_IFLA_BR_GROUP_ADDR:
        break;
    /* uint8_t */
    case QEMU_IFLA_BR_VLAN_FILTERING:
    case QEMU_IFLA_BR_TOPOLOGY_CHANGE:
    case QEMU_IFLA_BR_TOPOLOGY_CHANGE_DETECTED:
    case QEMU_IFLA_BR_MCAST_ROUTER:
    case QEMU_IFLA_BR_MCAST_SNOOPING:
    case QEMU_IFLA_BR_MCAST_QUERY_USE_IFADDR:
    case QEMU_IFLA_BR_MCAST_QUERIER:
    case QEMU_IFLA_BR_NF_CALL_IPTABLES:
    case QEMU_IFLA_BR_NF_CALL_IP6TABLES:
    case QEMU_IFLA_BR_NF_CALL_ARPTABLES:
    case QEMU_IFLA_BR_VLAN_STATS_ENABLED:
    case QEMU_IFLA_BR_MCAST_STATS_ENABLED:
    case QEMU_IFLA_BR_MCAST_IGMP_VERSION:
    case QEMU_IFLA_BR_MCAST_MLD_VERSION:
        break;
    /* uint16_t */
    case QEMU_IFLA_BR_PRIORITY:
    case QEMU_IFLA_BR_VLAN_PROTOCOL:
    case QEMU_IFLA_BR_GROUP_FWD_MASK:
    case QEMU_IFLA_BR_ROOT_PORT:
    case QEMU_IFLA_BR_VLAN_DEFAULT_PVID:
        u16 = NLA_DATA(nlattr);
        *u16 = tswap16(*u16);
        break;
    /* uint32_t */
    case QEMU_IFLA_BR_FORWARD_DELAY:
    case QEMU_IFLA_BR_HELLO_TIME:
    case QEMU_IFLA_BR_MAX_AGE:
    case QEMU_IFLA_BR_AGEING_TIME:
    case QEMU_IFLA_BR_STP_STATE:
    case QEMU_IFLA_BR_ROOT_PATH_COST:
    case QEMU_IFLA_BR_MCAST_HASH_ELASTICITY:
    case QEMU_IFLA_BR_MCAST_HASH_MAX:
    case QEMU_IFLA_BR_MCAST_LAST_MEMBER_CNT:
    case QEMU_IFLA_BR_MCAST_STARTUP_QUERY_CNT:
        u32 = NLA_DATA(nlattr);
        *u32 = tswap32(*u32);
        break;
    /* uint64_t */
    case QEMU_IFLA_BR_HELLO_TIMER:
    case QEMU_IFLA_BR_TCN_TIMER:
    case QEMU_IFLA_BR_GC_TIMER:
    case QEMU_IFLA_BR_TOPOLOGY_CHANGE_TIMER:
    case QEMU_IFLA_BR_MCAST_LAST_MEMBER_INTVL:
    case QEMU_IFLA_BR_MCAST_MEMBERSHIP_INTVL:
    case QEMU_IFLA_BR_MCAST_QUERIER_INTVL:
    case QEMU_IFLA_BR_MCAST_QUERY_INTVL:
    case QEMU_IFLA_BR_MCAST_QUERY_RESPONSE_INTVL:
    case QEMU_IFLA_BR_MCAST_STARTUP_QUERY_INTVL:
        u64 = NLA_DATA(nlattr);
        *u64 = tswap64(*u64);
        break;
    /* ifla_bridge_id: uin8_t[] */
    case QEMU_IFLA_BR_ROOT_ID:
    case QEMU_IFLA_BR_BRIDGE_ID:
        break;
    default:
        gemu_log("Unknown QEMU_IFLA_BR type %d\n", nlattr->nla_type);
        break;
    }
    return 0;
}

static abi_long host_to_target_slave_data_bridge_nlattr(struct nlattr *nlattr,
                                                        void *context)
{
    uint16_t *u16;
    uint32_t *u32;
    uint64_t *u64;

    switch (nlattr->nla_type) {
    /* uint8_t */
    case QEMU_IFLA_BRPORT_STATE:
    case QEMU_IFLA_BRPORT_MODE:
    case QEMU_IFLA_BRPORT_GUARD:
    case QEMU_IFLA_BRPORT_PROTECT:
    case QEMU_IFLA_BRPORT_FAST_LEAVE:
    case QEMU_IFLA_BRPORT_LEARNING:
    case QEMU_IFLA_BRPORT_UNICAST_FLOOD:
    case QEMU_IFLA_BRPORT_PROXYARP:
    case QEMU_IFLA_BRPORT_LEARNING_SYNC:
    case QEMU_IFLA_BRPORT_PROXYARP_WIFI:
    case QEMU_IFLA_BRPORT_TOPOLOGY_CHANGE_ACK:
    case QEMU_IFLA_BRPORT_CONFIG_PENDING:
    case QEMU_IFLA_BRPORT_MULTICAST_ROUTER:
    case QEMU_IFLA_BRPORT_MCAST_FLOOD:
    case QEMU_IFLA_BRPORT_MCAST_TO_UCAST:
    case QEMU_IFLA_BRPORT_VLAN_TUNNEL:
    case QEMU_IFLA_BRPORT_BCAST_FLOOD:
    case QEMU_IFLA_BRPORT_NEIGH_SUPPRESS:
        break;
    /* uint16_t */
    case QEMU_IFLA_BRPORT_PRIORITY:
    case QEMU_IFLA_BRPORT_DESIGNATED_PORT:
    case QEMU_IFLA_BRPORT_DESIGNATED_COST:
    case QEMU_IFLA_BRPORT_ID:
    case QEMU_IFLA_BRPORT_NO:
    case QEMU_IFLA_BRPORT_GROUP_FWD_MASK:
        u16 = NLA_DATA(nlattr);
        *u16 = tswap16(*u16);
        break;
    /* uin32_t */
    case QEMU_IFLA_BRPORT_COST:
        u32 = NLA_DATA(nlattr);
        *u32 = tswap32(*u32);
        break;
    /* uint64_t */
    case QEMU_IFLA_BRPORT_MESSAGE_AGE_TIMER:
    case QEMU_IFLA_BRPORT_FORWARD_DELAY_TIMER:
    case QEMU_IFLA_BRPORT_HOLD_TIMER:
        u64 = NLA_DATA(nlattr);
        *u64 = tswap64(*u64);
        break;
    /* ifla_bridge_id: uint8_t[] */
    case QEMU_IFLA_BRPORT_ROOT_ID:
    case QEMU_IFLA_BRPORT_BRIDGE_ID:
        break;
    default:
        gemu_log("Unknown QEMU_IFLA_BRPORT type %d\n", nlattr->nla_type);
        break;
    }
    return 0;
}

static abi_long host_to_target_data_tun_nlattr(struct nlattr *nlattr,
                                                  void *context)
{
    uint32_t *u32;

    switch (nlattr->nla_type) {
    /* uint8_t */
    case QEMU_IFLA_TUN_TYPE:
    case QEMU_IFLA_TUN_PI:
    case QEMU_IFLA_TUN_VNET_HDR:
    case QEMU_IFLA_TUN_PERSIST:
    case QEMU_IFLA_TUN_MULTI_QUEUE:
        break;
    /* uint32_t */
    case QEMU_IFLA_TUN_NUM_QUEUES:
    case QEMU_IFLA_TUN_NUM_DISABLED_QUEUES:
    case QEMU_IFLA_TUN_OWNER:
    case QEMU_IFLA_TUN_GROUP:
        u32 = NLA_DATA(nlattr);
        *u32 = tswap32(*u32);
        break;
    default:
        gemu_log("Unknown QEMU_IFLA_TUN type %d\n", nlattr->nla_type);
        break;
    }
    return 0;
}

struct linkinfo_context {
    int len;
    char *name;
    int slave_len;
    char *slave_name;
};

static abi_long host_to_target_data_linkinfo_nlattr(struct nlattr *nlattr,
                                                    void *context)
{
    struct linkinfo_context *li_context = context;

    switch (nlattr->nla_type) {
    /* string */
    case QEMU_IFLA_INFO_KIND:
        li_context->name = NLA_DATA(nlattr);
        li_context->len = nlattr->nla_len - NLA_HDRLEN;
        break;
    case QEMU_IFLA_INFO_SLAVE_KIND:
        li_context->slave_name = NLA_DATA(nlattr);
        li_context->slave_len = nlattr->nla_len - NLA_HDRLEN;
        break;
    /* stats */
    case QEMU_IFLA_INFO_XSTATS:
        /* FIXME: only used by CAN */
        break;
    /* nested */
    case QEMU_IFLA_INFO_DATA:
        if (strncmp(li_context->name, "bridge",
                    li_context->len) == 0) {
            return host_to_target_for_each_nlattr(NLA_DATA(nlattr),
                                                  nlattr->nla_len,
                                                  NULL,
                                             host_to_target_data_bridge_nlattr);
        } else if (strncmp(li_context->name, "tun",
                    li_context->len) == 0) {
            return host_to_target_for_each_nlattr(NLA_DATA(nlattr),
                                                  nlattr->nla_len,
                                                  NULL,
                                                host_to_target_data_tun_nlattr);
        } else {
            gemu_log("Unknown QEMU_IFLA_INFO_KIND %s\n", li_context->name);
        }
        break;
    case QEMU_IFLA_INFO_SLAVE_DATA:
        if (strncmp(li_context->slave_name, "bridge",
                    li_context->slave_len) == 0) {
            return host_to_target_for_each_nlattr(NLA_DATA(nlattr),
                                                  nlattr->nla_len,
                                                  NULL,
                                       host_to_target_slave_data_bridge_nlattr);
        } else {
            gemu_log("Unknown QEMU_IFLA_INFO_SLAVE_KIND %s\n",
                     li_context->slave_name);
        }
        break;
    default:
        gemu_log("Unknown host QEMU_IFLA_INFO type: %d\n", nlattr->nla_type);
        break;
    }

    return 0;
}

static abi_long host_to_target_data_inet_nlattr(struct nlattr *nlattr,
                                                void *context)
{
    uint32_t *u32;
    int i;

    switch (nlattr->nla_type) {
    case QEMU_IFLA_INET_CONF:
        u32 = NLA_DATA(nlattr);
        for (i = 0; i < (nlattr->nla_len - NLA_HDRLEN) / sizeof(*u32);
             i++) {
            u32[i] = tswap32(u32[i]);
        }
        break;
    default:
        gemu_log("Unknown host AF_INET type: %d\n", nlattr->nla_type);
    }
    return 0;
}

static abi_long host_to_target_data_inet6_nlattr(struct nlattr *nlattr,
                                                void *context)
{
    uint32_t *u32;
    uint64_t *u64;
    struct ifla_cacheinfo *ci;
    int i;

    switch (nlattr->nla_type) {
    /* binaries */
    case QEMU_IFLA_INET6_TOKEN:
        break;
    /* uint8_t */
    case QEMU_IFLA_INET6_ADDR_GEN_MODE:
        break;
    /* uint32_t */
    case QEMU_IFLA_INET6_FLAGS:
        u32 = NLA_DATA(nlattr);
        *u32 = tswap32(*u32);
        break;
    /* uint32_t[] */
    case QEMU_IFLA_INET6_CONF:
        u32 = NLA_DATA(nlattr);
        for (i = 0; i < (nlattr->nla_len - NLA_HDRLEN) / sizeof(*u32);
             i++) {
            u32[i] = tswap32(u32[i]);
        }
        break;
    /* ifla_cacheinfo */
    case QEMU_IFLA_INET6_CACHEINFO:
        ci = NLA_DATA(nlattr);
        ci->max_reasm_len = tswap32(ci->max_reasm_len);
        ci->tstamp = tswap32(ci->tstamp);
        ci->reachable_time = tswap32(ci->reachable_time);
        ci->retrans_time = tswap32(ci->retrans_time);
        break;
    /* uint64_t[] */
    case QEMU_IFLA_INET6_STATS:
    case QEMU_IFLA_INET6_ICMP6STATS:
        u64 = NLA_DATA(nlattr);
        for (i = 0; i < (nlattr->nla_len - NLA_HDRLEN) / sizeof(*u64);
             i++) {
            u64[i] = tswap64(u64[i]);
        }
        break;
    default:
        gemu_log("Unknown host AF_INET6 type: %d\n", nlattr->nla_type);
    }
    return 0;
}

static abi_long host_to_target_data_spec_nlattr(struct nlattr *nlattr,
                                                    void *context)
{
    switch (nlattr->nla_type) {
    case AF_INET:
        return host_to_target_for_each_nlattr(NLA_DATA(nlattr), nlattr->nla_len,
                                              NULL,
                                             host_to_target_data_inet_nlattr);
    case AF_INET6:
        return host_to_target_for_each_nlattr(NLA_DATA(nlattr), nlattr->nla_len,
                                              NULL,
                                             host_to_target_data_inet6_nlattr);
    default:
        gemu_log("Unknown host AF_SPEC type: %d\n", nlattr->nla_type);
        break;
    }
    return 0;
}

static abi_long host_to_target_data_xdp_nlattr(struct nlattr *nlattr,
                                               void *context)
{
    uint32_t *u32;

    switch (nlattr->nla_type) {
    /* uint8_t */
    case QEMU_IFLA_XDP_ATTACHED:
        break;
    /* uint32_t */
    case QEMU_IFLA_XDP_PROG_ID:
        u32 = NLA_DATA(nlattr);
        *u32 = tswap32(*u32);
        break;
    default:
        gemu_log("Unknown host XDP type: %d\n", nlattr->nla_type);
        break;
    }
    return 0;
}

static abi_long host_to_target_data_link_rtattr(struct rtattr *rtattr)
{
    uint32_t *u32;
    struct rtnl_link_stats *st;
    struct rtnl_link_stats64 *st64;
    struct rtnl_link_ifmap *map;
    struct linkinfo_context li_context;

    switch (rtattr->rta_type) {
    /* binary stream */
    case QEMU_IFLA_ADDRESS:
    case QEMU_IFLA_BROADCAST:
    /* string */
    case QEMU_IFLA_IFNAME:
    case QEMU_IFLA_QDISC:
        break;
    /* uin8_t */
    case QEMU_IFLA_OPERSTATE:
    case QEMU_IFLA_LINKMODE:
    case QEMU_IFLA_CARRIER:
    case QEMU_IFLA_PROTO_DOWN:
        break;
    /* uint32_t */
    case QEMU_IFLA_MTU:
    case QEMU_IFLA_LINK:
    case QEMU_IFLA_WEIGHT:
    case QEMU_IFLA_TXQLEN:
    case QEMU_IFLA_CARRIER_CHANGES:
    case QEMU_IFLA_NUM_RX_QUEUES:
    case QEMU_IFLA_NUM_TX_QUEUES:
    case QEMU_IFLA_PROMISCUITY:
    case QEMU_IFLA_EXT_MASK:
    case QEMU_IFLA_LINK_NETNSID:
    case QEMU_IFLA_GROUP:
    case QEMU_IFLA_MASTER:
    case QEMU_IFLA_NUM_VF:
    case QEMU_IFLA_GSO_MAX_SEGS:
    case QEMU_IFLA_GSO_MAX_SIZE:
    case QEMU_IFLA_CARRIER_UP_COUNT:
    case QEMU_IFLA_CARRIER_DOWN_COUNT:
        u32 = RTA_DATA(rtattr);
        *u32 = tswap32(*u32);
        break;
    /* struct rtnl_link_stats */
    case QEMU_IFLA_STATS:
        st = RTA_DATA(rtattr);
        st->rx_packets = tswap32(st->rx_packets);
        st->tx_packets = tswap32(st->tx_packets);
        st->rx_bytes = tswap32(st->rx_bytes);
        st->tx_bytes = tswap32(st->tx_bytes);
        st->rx_errors = tswap32(st->rx_errors);
        st->tx_errors = tswap32(st->tx_errors);
        st->rx_dropped = tswap32(st->rx_dropped);
        st->tx_dropped = tswap32(st->tx_dropped);
        st->multicast = tswap32(st->multicast);
        st->collisions = tswap32(st->collisions);

        /* detailed rx_errors: */
        st->rx_length_errors = tswap32(st->rx_length_errors);
        st->rx_over_errors = tswap32(st->rx_over_errors);
        st->rx_crc_errors = tswap32(st->rx_crc_errors);
        st->rx_frame_errors = tswap32(st->rx_frame_errors);
        st->rx_fifo_errors = tswap32(st->rx_fifo_errors);
        st->rx_missed_errors = tswap32(st->rx_missed_errors);

        /* detailed tx_errors */
        st->tx_aborted_errors = tswap32(st->tx_aborted_errors);
        st->tx_carrier_errors = tswap32(st->tx_carrier_errors);
        st->tx_fifo_errors = tswap32(st->tx_fifo_errors);
        st->tx_heartbeat_errors = tswap32(st->tx_heartbeat_errors);
        st->tx_window_errors = tswap32(st->tx_window_errors);

        /* for cslip etc */
        st->rx_compressed = tswap32(st->rx_compressed);
        st->tx_compressed = tswap32(st->tx_compressed);
        break;
    /* struct rtnl_link_stats64 */
    case QEMU_IFLA_STATS64:
        st64 = RTA_DATA(rtattr);
        st64->rx_packets = tswap64(st64->rx_packets);
        st64->tx_packets = tswap64(st64->tx_packets);
        st64->rx_bytes = tswap64(st64->rx_bytes);
        st64->tx_bytes = tswap64(st64->tx_bytes);
        st64->rx_errors = tswap64(st64->rx_errors);
        st64->tx_errors = tswap64(st64->tx_errors);
        st64->rx_dropped = tswap64(st64->rx_dropped);
        st64->tx_dropped = tswap64(st64->tx_dropped);
        st64->multicast = tswap64(st64->multicast);
        st64->collisions = tswap64(st64->collisions);

        /* detailed rx_errors: */
        st64->rx_length_errors = tswap64(st64->rx_length_errors);
        st64->rx_over_errors = tswap64(st64->rx_over_errors);
        st64->rx_crc_errors = tswap64(st64->rx_crc_errors);
        st64->rx_frame_errors = tswap64(st64->rx_frame_errors);
        st64->rx_fifo_errors = tswap64(st64->rx_fifo_errors);
        st64->rx_missed_errors = tswap64(st64->rx_missed_errors);

        /* detailed tx_errors */
        st64->tx_aborted_errors = tswap64(st64->tx_aborted_errors);
        st64->tx_carrier_errors = tswap64(st64->tx_carrier_errors);
        st64->tx_fifo_errors = tswap64(st64->tx_fifo_errors);
        st64->tx_heartbeat_errors = tswap64(st64->tx_heartbeat_errors);
        st64->tx_window_errors = tswap64(st64->tx_window_errors);

        /* for cslip etc */
        st64->rx_compressed = tswap64(st64->rx_compressed);
        st64->tx_compressed = tswap64(st64->tx_compressed);
        break;
    /* struct rtnl_link_ifmap */
    case QEMU_IFLA_MAP:
        map = RTA_DATA(rtattr);
        map->mem_start = tswap64(map->mem_start);
        map->mem_end = tswap64(map->mem_end);
        map->base_addr = tswap64(map->base_addr);
        map->irq = tswap16(map->irq);
        break;
    /* nested */
    case QEMU_IFLA_LINKINFO:
        memset(&li_context, 0, sizeof(li_context));
        return host_to_target_for_each_nlattr(RTA_DATA(rtattr), rtattr->rta_len,
                                              &li_context,
                                           host_to_target_data_linkinfo_nlattr);
    case QEMU_IFLA_AF_SPEC:
        return host_to_target_for_each_nlattr(RTA_DATA(rtattr), rtattr->rta_len,
                                              NULL,
                                             host_to_target_data_spec_nlattr);
    case QEMU_IFLA_XDP:
        return host_to_target_for_each_nlattr(RTA_DATA(rtattr), rtattr->rta_len,
                                              NULL,
                                                host_to_target_data_xdp_nlattr);
    default:
        gemu_log("Unknown host QEMU_IFLA type: %d\n", rtattr->rta_type);
        break;
    }
    return 0;
}

static abi_long host_to_target_data_addr_rtattr(struct rtattr *rtattr)
{
    uint32_t *u32;
    struct ifa_cacheinfo *ci;

    switch (rtattr->rta_type) {
    /* binary: depends on family type */
    case IFA_ADDRESS:
    case IFA_LOCAL:
        break;
    /* string */
    case IFA_LABEL:
        break;
    /* u32 */
    case IFA_FLAGS:
    case IFA_BROADCAST:
        u32 = RTA_DATA(rtattr);
        *u32 = tswap32(*u32);
        break;
    /* struct ifa_cacheinfo */
    case IFA_CACHEINFO:
        ci = RTA_DATA(rtattr);
        ci->ifa_prefered = tswap32(ci->ifa_prefered);
        ci->ifa_valid = tswap32(ci->ifa_valid);
        ci->cstamp = tswap32(ci->cstamp);
        ci->tstamp = tswap32(ci->tstamp);
        break;
    default:
        gemu_log("Unknown host IFA type: %d\n", rtattr->rta_type);
        break;
    }
    return 0;
}

static abi_long host_to_target_data_route_rtattr(struct rtattr *rtattr)
{
    uint32_t *u32;
    struct rta_cacheinfo *ci;

    switch (rtattr->rta_type) {
    /* binary: depends on family type */
    case QEMU_RTA_GATEWAY:
    case QEMU_RTA_DST:
    case QEMU_RTA_PREFSRC:
        break;
    /* u8 */
    case QEMU_RTA_PREF:
        break;
    /* u32 */
    case QEMU_RTA_PRIORITY:
    case QEMU_RTA_TABLE:
    case QEMU_RTA_OIF:
        u32 = RTA_DATA(rtattr);
        *u32 = tswap32(*u32);
        break;
    /* struct rta_cacheinfo */
    case QEMU_RTA_CACHEINFO:
        ci = RTA_DATA(rtattr);
        ci->rta_clntref = tswap32(ci->rta_clntref);
        ci->rta_lastuse = tswap32(ci->rta_lastuse);
        ci->rta_expires = tswap32(ci->rta_expires);
        ci->rta_error = tswap32(ci->rta_error);
        ci->rta_used = tswap32(ci->rta_used);
#if defined(RTNETLINK_HAVE_PEERINFO)
        ci->rta_id = tswap32(ci->rta_id);
        ci->rta_ts = tswap32(ci->rta_ts);
        ci->rta_tsage = tswap32(ci->rta_tsage);
#endif
        break;
    default:
        gemu_log("Unknown host RTA type: %d\n", rtattr->rta_type);
        break;
    }
    return 0;
}

static abi_long host_to_target_link_rtattr(struct rtattr *rtattr,
                                         uint32_t rtattr_len)
{
    return host_to_target_for_each_rtattr(rtattr, rtattr_len,
                                          host_to_target_data_link_rtattr);
}

static abi_long host_to_target_addr_rtattr(struct rtattr *rtattr,
                                         uint32_t rtattr_len)
{
    return host_to_target_for_each_rtattr(rtattr, rtattr_len,
                                          host_to_target_data_addr_rtattr);
}

static abi_long host_to_target_route_rtattr(struct rtattr *rtattr,
                                         uint32_t rtattr_len)
{
    return host_to_target_for_each_rtattr(rtattr, rtattr_len,
                                          host_to_target_data_route_rtattr);
}

static abi_long host_to_target_data_route(struct nlmsghdr *nlh)
{
    uint32_t nlmsg_len;
    struct ifinfomsg *ifi;
    struct ifaddrmsg *ifa;
    struct rtmsg *rtm;

    nlmsg_len = nlh->nlmsg_len;
    switch (nlh->nlmsg_type) {
    case RTM_NEWLINK:
    case RTM_DELLINK:
    case RTM_GETLINK:
        if (nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(*ifi))) {
            ifi = NLMSG_DATA(nlh);
            ifi->ifi_type = tswap16(ifi->ifi_type);
            ifi->ifi_index = tswap32(ifi->ifi_index);
            ifi->ifi_flags = tswap32(ifi->ifi_flags);
            ifi->ifi_change = tswap32(ifi->ifi_change);
            host_to_target_link_rtattr(IFLA_RTA(ifi),
                                       nlmsg_len - NLMSG_LENGTH(sizeof(*ifi)));
        }
        break;
    case RTM_NEWADDR:
    case RTM_DELADDR:
    case RTM_GETADDR:
        if (nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(*ifa))) {
            ifa = NLMSG_DATA(nlh);
            ifa->ifa_index = tswap32(ifa->ifa_index);
            host_to_target_addr_rtattr(IFA_RTA(ifa),
                                       nlmsg_len - NLMSG_LENGTH(sizeof(*ifa)));
        }
        break;
    case RTM_NEWROUTE:
    case RTM_DELROUTE:
    case RTM_GETROUTE:
        if (nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(*rtm))) {
            rtm = NLMSG_DATA(nlh);
            rtm->rtm_flags = tswap32(rtm->rtm_flags);
            host_to_target_route_rtattr(RTM_RTA(rtm),
                                        nlmsg_len - NLMSG_LENGTH(sizeof(*rtm)));
        }
        break;
    default:
        return -TARGET_EINVAL;
    }
    return 0;
}

static inline abi_long host_to_target_nlmsg_route(struct nlmsghdr *nlh,
                                                  size_t len)
{
    return host_to_target_for_each_nlmsg(nlh, len, host_to_target_data_route);
}

static abi_long target_to_host_for_each_rtattr(struct rtattr *rtattr,
                                               size_t len,
                                               abi_long (*target_to_host_rtattr)
                                                        (struct rtattr *))
{
    abi_long ret;

    while (len >= sizeof(struct rtattr)) {
        if (tswap16(rtattr->rta_len) < sizeof(struct rtattr) ||
            tswap16(rtattr->rta_len) > len) {
            break;
        }
        rtattr->rta_len = tswap16(rtattr->rta_len);
        rtattr->rta_type = tswap16(rtattr->rta_type);
        ret = target_to_host_rtattr(rtattr);
        if (ret < 0) {
            return ret;
        }
        len -= RTA_ALIGN(rtattr->rta_len);
        rtattr = (struct rtattr *)(((char *)rtattr) +
                 RTA_ALIGN(rtattr->rta_len));
    }
    return 0;
}

static abi_long target_to_host_data_link_rtattr(struct rtattr *rtattr)
{
    switch (rtattr->rta_type) {
    default:
        gemu_log("Unknown target QEMU_IFLA type: %d\n", rtattr->rta_type);
        break;
    }
    return 0;
}

static abi_long target_to_host_data_addr_rtattr(struct rtattr *rtattr)
{
    switch (rtattr->rta_type) {
    /* binary: depends on family type */
    case IFA_LOCAL:
    case IFA_ADDRESS:
        break;
    default:
        gemu_log("Unknown target IFA type: %d\n", rtattr->rta_type);
        break;
    }
    return 0;
}

static abi_long target_to_host_data_route_rtattr(struct rtattr *rtattr)
{
    uint32_t *u32;
    switch (rtattr->rta_type) {
    /* binary: depends on family type */
    case QEMU_RTA_DST:
    case QEMU_RTA_SRC:
    case QEMU_RTA_GATEWAY:
        break;
    /* u32 */
    case QEMU_RTA_PRIORITY:
    case QEMU_RTA_OIF:
        u32 = RTA_DATA(rtattr);
        *u32 = tswap32(*u32);
        break;
    default:
        gemu_log("Unknown target RTA type: %d\n", rtattr->rta_type);
        break;
    }
    return 0;
}

static void target_to_host_link_rtattr(struct rtattr *rtattr,
                                       uint32_t rtattr_len)
{
    target_to_host_for_each_rtattr(rtattr, rtattr_len,
                                   target_to_host_data_link_rtattr);
}

static void target_to_host_addr_rtattr(struct rtattr *rtattr,
                                     uint32_t rtattr_len)
{
    target_to_host_for_each_rtattr(rtattr, rtattr_len,
                                   target_to_host_data_addr_rtattr);
}

static void target_to_host_route_rtattr(struct rtattr *rtattr,
                                     uint32_t rtattr_len)
{
    target_to_host_for_each_rtattr(rtattr, rtattr_len,
                                   target_to_host_data_route_rtattr);
}

static abi_long target_to_host_data_route(struct nlmsghdr *nlh)
{
    struct ifinfomsg *ifi;
    struct ifaddrmsg *ifa;
    struct rtmsg *rtm;

    switch (nlh->nlmsg_type) {
    case RTM_GETLINK:
        break;
    case RTM_NEWLINK:
    case RTM_DELLINK:
        if (nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(*ifi))) {
            ifi = NLMSG_DATA(nlh);
            ifi->ifi_type = tswap16(ifi->ifi_type);
            ifi->ifi_index = tswap32(ifi->ifi_index);
            ifi->ifi_flags = tswap32(ifi->ifi_flags);
            ifi->ifi_change = tswap32(ifi->ifi_change);
            target_to_host_link_rtattr(IFLA_RTA(ifi), nlh->nlmsg_len -
                                       NLMSG_LENGTH(sizeof(*ifi)));
        }
        break;
    case RTM_GETADDR:
    case RTM_NEWADDR:
    case RTM_DELADDR:
        if (nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(*ifa))) {
            ifa = NLMSG_DATA(nlh);
            ifa->ifa_index = tswap32(ifa->ifa_index);
            target_to_host_addr_rtattr(IFA_RTA(ifa), nlh->nlmsg_len -
                                       NLMSG_LENGTH(sizeof(*ifa)));
        }
        break;
    case RTM_GETROUTE:
        break;
    case RTM_NEWROUTE:
    case RTM_DELROUTE:
        if (nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(*rtm))) {
            rtm = NLMSG_DATA(nlh);
            rtm->rtm_flags = tswap32(rtm->rtm_flags);
            target_to_host_route_rtattr(RTM_RTA(rtm), nlh->nlmsg_len -
                                        NLMSG_LENGTH(sizeof(*rtm)));
        }
        break;
    default:
        return -TARGET_EOPNOTSUPP;
    }
    return 0;
}

static abi_long target_to_host_nlmsg_route(struct nlmsghdr *nlh, size_t len)
{
    return target_to_host_for_each_nlmsg(nlh, len, target_to_host_data_route);
}
#endif /* CONFIG_RTNETLINK */

static abi_long host_to_target_data_audit(struct nlmsghdr *nlh)
{
    switch (nlh->nlmsg_type) {
    default:
        gemu_log("Unknown host audit message type %d\n",
                 nlh->nlmsg_type);
        return -TARGET_EINVAL;
    }
    return 0;
}

static inline abi_long host_to_target_nlmsg_audit(struct nlmsghdr *nlh,
                                                  size_t len)
{
    return host_to_target_for_each_nlmsg(nlh, len, host_to_target_data_audit);
}

static abi_long target_to_host_data_audit(struct nlmsghdr *nlh)
{
    switch (nlh->nlmsg_type) {
    case AUDIT_USER:
    case AUDIT_FIRST_USER_MSG ... AUDIT_LAST_USER_MSG:
    case AUDIT_FIRST_USER_MSG2 ... AUDIT_LAST_USER_MSG2:
        break;
    default:
        gemu_log("Unknown target audit message type %d\n",
                 nlh->nlmsg_type);
        return -TARGET_EINVAL;
    }

    return 0;
}

static abi_long target_to_host_nlmsg_audit(struct nlmsghdr *nlh, size_t len)
{
    return target_to_host_for_each_nlmsg(nlh, len, target_to_host_data_audit);
}

static abi_long packet_target_to_host_sockaddr(void *host_addr,
                                               abi_ulong target_addr,
                                               socklen_t len)
{
    struct sockaddr *addr = host_addr;
    struct target_sockaddr *target_saddr;

    target_saddr = lock_user(VERIFY_READ, target_addr, len, 1);
    if (!target_saddr) {
        return -TARGET_EFAULT;
    }

    memcpy(addr, target_saddr, len);
    addr->sa_family = tswap16(target_saddr->sa_family);
    /* spkt_protocol is big-endian */

    unlock_user(target_saddr, target_addr, 0);
    return 0;
}

TargetFdTrans target_packet_trans = {
    .target_to_host_addr = packet_target_to_host_sockaddr,
};

#ifdef CONFIG_RTNETLINK
static abi_long netlink_route_target_to_host(void *buf, size_t len)
{
    abi_long ret;

    ret = target_to_host_nlmsg_route(buf, len);
    if (ret < 0) {
        return ret;
    }

    return len;
}

static abi_long netlink_route_host_to_target(void *buf, size_t len)
{
    abi_long ret;

    ret = host_to_target_nlmsg_route(buf, len);
    if (ret < 0) {
        return ret;
    }

    return len;
}

TargetFdTrans target_netlink_route_trans = {
    .target_to_host_data = netlink_route_target_to_host,
    .host_to_target_data = netlink_route_host_to_target,
};
#endif /* CONFIG_RTNETLINK */

static abi_long netlink_audit_target_to_host(void *buf, size_t len)
{
    abi_long ret;

    ret = target_to_host_nlmsg_audit(buf, len);
    if (ret < 0) {
        return ret;
    }

    return len;
}

static abi_long netlink_audit_host_to_target(void *buf, size_t len)
{
    abi_long ret;

    ret = host_to_target_nlmsg_audit(buf, len);
    if (ret < 0) {
        return ret;
    }

    return len;
}

TargetFdTrans target_netlink_audit_trans = {
    .target_to_host_data = netlink_audit_target_to_host,
    .host_to_target_data = netlink_audit_host_to_target,
};

/* signalfd siginfo conversion */

static void
host_to_target_signalfd_siginfo(struct signalfd_siginfo *tinfo,
                                const struct signalfd_siginfo *info)
{
    int sig = host_to_target_signal(info->ssi_signo);

    /* linux/signalfd.h defines a ssi_addr_lsb
     * not defined in sys/signalfd.h but used by some kernels
     */

#ifdef BUS_MCEERR_AO
    if (tinfo->ssi_signo == SIGBUS &&
        (tinfo->ssi_code == BUS_MCEERR_AR ||
         tinfo->ssi_code == BUS_MCEERR_AO)) {
        uint16_t *ssi_addr_lsb = (uint16_t *)(&info->ssi_addr + 1);
        uint16_t *tssi_addr_lsb = (uint16_t *)(&tinfo->ssi_addr + 1);
        *tssi_addr_lsb = tswap16(*ssi_addr_lsb);
    }
#endif

    tinfo->ssi_signo = tswap32(sig);
    tinfo->ssi_errno = tswap32(tinfo->ssi_errno);
    tinfo->ssi_code = tswap32(info->ssi_code);
    tinfo->ssi_pid = tswap32(info->ssi_pid);
    tinfo->ssi_uid = tswap32(info->ssi_uid);
    tinfo->ssi_fd = tswap32(info->ssi_fd);
    tinfo->ssi_tid = tswap32(info->ssi_tid);
    tinfo->ssi_band = tswap32(info->ssi_band);
    tinfo->ssi_overrun = tswap32(info->ssi_overrun);
    tinfo->ssi_trapno = tswap32(info->ssi_trapno);
    tinfo->ssi_status = tswap32(info->ssi_status);
    tinfo->ssi_int = tswap32(info->ssi_int);
    tinfo->ssi_ptr = tswap64(info->ssi_ptr);
    tinfo->ssi_utime = tswap64(info->ssi_utime);
    tinfo->ssi_stime = tswap64(info->ssi_stime);
    tinfo->ssi_addr = tswap64(info->ssi_addr);
}

static abi_long host_to_target_data_signalfd(void *buf, size_t len)
{
    int i;

    for (i = 0; i < len; i += sizeof(struct signalfd_siginfo)) {
        host_to_target_signalfd_siginfo(buf + i, buf + i);
    }

    return len;
}

TargetFdTrans target_signalfd_trans = {
    .host_to_target_data = host_to_target_data_signalfd,
};

static abi_long swap_data_eventfd(void *buf, size_t len)
{
    uint64_t *counter = buf;
    int i;

    if (len < sizeof(uint64_t)) {
        return -EINVAL;
    }

    for (i = 0; i < len; i += sizeof(uint64_t)) {
        *counter = tswap64(*counter);
        counter++;
    }

    return len;
}

TargetFdTrans target_eventfd_trans = {
    .host_to_target_data = swap_data_eventfd,
    .target_to_host_data = swap_data_eventfd,
};

#if (defined(TARGET_NR_inotify_init) && defined(__NR_inotify_init)) || \
    (defined(CONFIG_INOTIFY1) && defined(TARGET_NR_inotify_init1) && \
     defined(__NR_inotify_init1))
static abi_long host_to_target_data_inotify(void *buf, size_t len)
{
    struct inotify_event *ev;
    int i;
    uint32_t name_len;

    for (i = 0; i < len; i += sizeof(struct inotify_event) + name_len) {
        ev = (struct inotify_event *)((char *)buf + i);
        name_len = ev->len;

        ev->wd = tswap32(ev->wd);
        ev->mask = tswap32(ev->mask);
        ev->cookie = tswap32(ev->cookie);
        ev->len = tswap32(name_len);
    }

    return len;
}

TargetFdTrans target_inotify_trans = {
    .host_to_target_data = host_to_target_data_inotify,
};
#endif
