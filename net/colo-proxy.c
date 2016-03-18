/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2015 FUJITSU LIMITED
 * Copyright (c) 2015 Intel Corporation
 *
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "net/filter.h"
#include "net/queue.h"
#include "qemu-common.h"
#include "qemu/iov.h"
#include "qapi/qmp/qerror.h"
#include "qapi-visit.h"
#include "qom/object.h"
#include "qemu/sockets.h"
#include "qemu/main-loop.h"
#include "qemu/jhash.h"
#include "qemu/coroutine.h"
#include "monitor/monitor.h"
#include "net/eth.h"
#include "slirp/slirp.h"
#include "slirp/slirp_config.h"
#include "slirp/ip.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "qemu/error-report.h"
#include "net/colo-proxy.h"
#include "trace.h"
#include <sys/sysinfo.h>

#define FILTER_COLO_PROXY(obj) \
    OBJECT_CHECK(COLOProxyState, (obj), TYPE_FILTER_COLO_PROXY)

#define TYPE_FILTER_COLO_PROXY "colo-proxy"
#define PRIMARY_MODE "primary"
#define SECONDARY_MODE "secondary"

/* TODO: Should be configurable */
#define REGULAR_CHECK_MS 400

/*

  |COLOProxyState++
  |               |
  +---------------+   +---------------+         +---------------+
  |conn list      +--->conn           +--------->conn           |
  +---------------+   +---------------+         +---------------+
  |               |     |           |             |           |
  +---------------+ +---v----+  +---v----+    +---v----+  +---v----+
                    |primary |  |secondary    |primary |  |secondary
                    |packet  |  |packet  +    |packet  |  |packet  +
                    +--------+  +--------+    +--------+  +--------+
                        |           |             |           |
                    +---v----+  +---v----+    +---v----+  +---v----+
                    |primary |  |secondary    |primary |  |secondary
                    |packet  |  |packet  +    |packet  |  |packet  +
                    +--------+  +--------+    +--------+  +--------+
                        |           |             |           |
                    +---v----+  +---v----+    +---v----+  +---v----+
                    |primary |  |secondary    |primary |  |secondary
                    |packet  |  |packet  +    |packet  |  |packet  +
                    +--------+  +--------+    +--------+  +--------+


*/

typedef enum colo_conn_state {
     COLO_CONN_IDLE,

    /* States on the primary: For incoming connection */
     COLO_CONN_PRI_IN_SYN,   /* Received Syn */
     COLO_CONN_PRI_IN_PSYNACK, /* Received syn/ack from primary, but not
                                yet from secondary */
     COLO_CONN_PRI_IN_SSYNACK, /* Received syn/ack from secondary, but
                                  not yet from primary */
     COLO_CONN_PRI_IN_SYNACK,  /* Received syn/ack from both */
     COLO_CONN_PRI_IN_ESTABLISHED, /* Got the ACK */

    /* States on the secondary: For incoming connection */
     COLO_CONN_SEC_IN_SYNACK,      /* We sent a syn/ack */
     COLO_CONN_SEC_IN_ACK,         /* Saw the ack but didn't yet see our syn/ack */
     COLO_CONN_SEC_IN_ESTABLISHED, /* Got the ACK from the outside */
} colo_conn_state;

static const char *conn_state_str[] = {
     "Idle",
     "P:In Syn",
     "P:In PSynAck",
     "P:In SSynAck",
     "P:In SynAck",
     "P:Established",

     "S:In SynAck",
     "S:In Ack",
     "S:Established"
};

typedef struct COLOProxyState {
    NetFilterState parent_obj;
    NetQueue *incoming_queue;/* guest normal net queue */
    NetFilterDirection direction; /* packet direction */
    /* colo mode (primary or secondary) */
    int colo_mode;
    /* primary colo connect address(192.168.0.100:12345)
     * or secondary listening address(:12345)
     */
    char *addr;
    int sockfd;

     /* connection list: the packet belonged to this NIC
     * could be found in this list.
     * element type: Connection
     */
    GQueue conn_list;
    int status; /* proxy is running or not */
    ssize_t hashtable_size; /* proxy current hash size */
    QemuEvent need_compare_ev;  /* notify compare thread */
    QemuThread thread; /* compare thread, a thread for each NIC */
    VMChangeStateEntry *change_state_handler;

    /* Timer used on the primary to find packets that are never matched */
    QEMUTimer *timer;
} COLOProxyState;

typedef struct Packet {
    void *data;
    union {
        uint8_t *network_layer;
        struct ip *ip;
    };
    uint8_t *transport_layer;
    int size;
    COLOProxyState *s;
    NetClientState *sender;

    /* Time of packet creation, in wall clock ms */
    int64_t creation_ms;
} Packet;

typedef struct ConnectionKey {
    /* (src, dst) must be grouped, in the same way than in IP header */
    struct in_addr src;
    struct in_addr dst;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t ip_proto;
} QEMU_PACKED ConnectionKey;

/* define one connection */
typedef struct Connection {
    QemuMutex list_lock;
    /* These should probably become RCU lists */
    /* connection primary send queue: element type: Packet */
    GQueue primary_list;
    /* connection secondary send queue: element type: Packet */
    GQueue secondary_list;
     /* flag to enqueue unprocessed_connections */
    bool processing;
    int ip_proto;

    void *proto; /* tcp only now */

    colo_conn_state state;
    tcp_seq  primary_seq;
    tcp_seq  secondary_seq;

    ConnectionKey ck; /* For easy debug */
} Connection;

enum {
    COLO_PROXY_NONE,     /* colo proxy is not started */
    COLO_PROXY_RUNNING,  /* colo proxy is running */
    COLO_PROXY_DONE,     /* colo proxyis done(failover) */
};

/* save all the connections of a vm instance in this table */
GHashTable *colo_conn_hash;
/* true if a miscompare is discovered and a checkpoint should be triggered */
static bool colo_do_checkpoint;
/* Used for signalling from the colo-proxy threads to the colo thread */
static pthread_cond_t colo_proxy_signal_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t colo_proxy_signal_mutex = PTHREAD_MUTEX_INITIALIZER;
static ssize_t hashtable_max_size;
static unsigned int checkpoint_num = 0;

static inline void colo_proxy_dump_packet(Packet *pkt)
{
    int i;
    for (i = 0; i < pkt->size; i++) {
        fprintf(stderr, "%02x ", ((uint8_t *)pkt->data)[i]);
    }
    fprintf(stderr, "\n");
}

static inline void colo_proxy_dump_headers(const char *prefix, Packet *pkt)
{
    char *sdebug = strdup(inet_ntoa(pkt->ip->ip_src));
    char *ddebug = strdup(inet_ntoa(pkt->ip->ip_dst));
    struct timeval now;

    gettimeofday(&now, NULL);
    fprintf(stderr, "%s@%zd.%06zd:(%p) proto: %x size: %d src/dst: %s/%s",
            prefix, (size_t)now.tv_sec, (size_t)now.tv_usec,
            pkt, pkt->ip->ip_p, pkt->size, sdebug, ddebug);
    if (pkt->ip->ip_p == IPPROTO_TCP) {
        struct tcphdr *tcp = (struct tcphdr *)pkt->transport_layer;

        fprintf(stderr, " port s/d: %d/%d seq/ack=%u/%u flags: %x\n",
                ntohs(tcp->th_sport), ntohs(tcp->th_dport),
                ntohl(tcp->th_seq), ntohl(tcp->th_ack), tcp->th_flags);
    } else {
        fprintf(stderr, "\n");
    }
}

static void info_packet(void *opaque, void *user_data)
{
    Packet *pkt = opaque;
    Monitor *mon = user_data;
    size_t to_print, i;

    monitor_printf(mon, "    (%5d bytes): ", pkt->size);

    to_print = pkt->size;
    if (to_print > 64) to_print = 64;
    for(i = 0; i< to_print; i++) {
        monitor_printf(mon, "%02x ", ((uint8_t *)pkt->data)[i]);
    }
    monitor_printf(mon, "\n");
}

static void info_hash(gpointer key, gpointer value, gpointer user_data)
{
    Monitor *mon = user_data;
    ConnectionKey *ck = key;
    Connection *conn = value;

    monitor_printf(mon, "  (%4d), %s:%d -> ", ck->ip_proto, inet_ntoa(ck->src), ck->src_port);
    monitor_printf(mon, "  %s:%d %s processing: %d seq (p/s): %u/%u\n ", inet_ntoa(ck->dst), ck->dst_port,
                   conn_state_str[conn->state],
                   conn->processing, conn->primary_seq, conn->secondary_seq);

    qemu_mutex_lock(&conn->list_lock);
    monitor_printf(mon, "  Primary list:\n");
    g_queue_foreach(&conn->primary_list, info_packet, mon);

    monitor_printf(mon, "  Secondary list:\n");
    g_queue_foreach(&conn->secondary_list, info_packet, mon);
    qemu_mutex_unlock(&conn->list_lock);
}

void hmp_info_colo_proxy(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "colo proxy:\n");
    g_hash_table_foreach (colo_conn_hash, info_hash, mon);
}

static void packet_destroy(void *opaque, void *user_data);

static uint32_t connection_key_hash(const void *opaque)
{
    const ConnectionKey *key = opaque;
    uint32_t a, b, c;

    /* Jenkins hash */
    a = b = c = JHASH_INITVAL + sizeof(*key);
    a += key->src.s_addr;
    b += key->dst.s_addr;
    c += (key->src_port | key->dst_port << 16);
    __jhash_mix(a, b, c);

    a += key->ip_proto;
    __jhash_final(a, b, c);

    return c;
}

static int connection_key_equal(const void *opaque1, const void *opaque2)
{
    return memcmp(opaque1, opaque2, sizeof(ConnectionKey)) == 0;
}

static void connection_destroy(void *opaque)
{
    Connection *conn = opaque;

    qemu_mutex_lock(&conn->list_lock);
    g_queue_foreach(&conn->primary_list, packet_destroy, NULL);
    g_queue_free(&conn->primary_list);
    g_queue_foreach(&conn->secondary_list, packet_destroy, NULL);
    g_queue_free(&conn->secondary_list);
    qemu_mutex_unlock(&conn->list_lock);
    qemu_mutex_destroy(&conn->list_lock);
    g_slice_free(Connection, conn);
}

static Connection *connection_new(ConnectionKey *key)
{
    Connection *conn = g_slice_new0(Connection);
    conn->ck = *key;
    qemu_mutex_init(&conn->list_lock);
    conn->ip_proto = key->ip_proto;
    conn->processing = false;
    g_queue_init(&conn->primary_list);
    g_queue_init(&conn->secondary_list);

    return conn;
}

static void colo_send_primary_packet(void *opaque, void *user_data)
{
    Packet *pkt = opaque;
    qemu_net_queue_send(pkt->s->incoming_queue, pkt->sender, 0,
                    (const uint8_t *)pkt->data, pkt->size, NULL);
}

static void proxy_state_change_handler(void *opaque, int running,
                                       RunState state)
{
    COLOProxyState *s = opaque;
    if (running) {
        qemu_net_queue_flush(s->incoming_queue);
    }
}

/* Note the net layer wont let this out until the VM starts, that needs
 * to be fixed really; we've got a change state handler that kicks our
 * queue.
 */
static void colo_flush_connection(void *opaque, void *user_data)
{
    Connection *conn = opaque;
    Packet *pkt = NULL;

    qemu_mutex_lock(&conn->list_lock);
    while (!g_queue_is_empty(&conn->primary_list)) {
        pkt = g_queue_pop_head(&conn->primary_list);
        /*colo_proxy_dump_headers("PQ>>W", pkt); */
        colo_send_primary_packet(pkt, NULL);
    }
    while (!g_queue_is_empty(&conn->secondary_list)) {
        pkt = g_queue_pop_head(&conn->secondary_list);
        packet_destroy(pkt, NULL);
    }
    qemu_mutex_unlock(&conn->list_lock);
}

static void colo_flush_secondary_connection(gpointer key, gpointer value, gpointer user_data)
{
    Connection *conn = value;
    /* We could just empty the hash table at this point, but it's interesting
     * to keep state so we can watch
     */
    switch (conn->state) {
    case COLO_CONN_SEC_IN_SYNACK:
    case COLO_CONN_SEC_IN_ACK:
    case COLO_CONN_SEC_IN_ESTABLISHED:
        conn->state = COLO_CONN_IDLE;
        break;

    default:
        break;
    }
}

/*
 * Clear hashtable, stop this hash growing really huge
 */
static void clear_connection_hashtable(COLOProxyState *s)
{
    s->hashtable_size = 0;
    g_hash_table_remove_all(colo_conn_hash);
    trace_colo_proxy("clear_connection_hashtable");
}

bool colo_proxy_query_checkpoint(void)
{
    return colo_do_checkpoint;
}

static int colo_proxy_primary_checkpoint(COLOProxyState *s)
{
    g_queue_foreach(&s->conn_list, colo_flush_connection, NULL);
    return 0;
}

static int colo_proxy_secondary_checkpoint(COLOProxyState *s)
{
    g_hash_table_foreach(colo_conn_hash, colo_flush_secondary_connection, NULL);
    return 0;
}

static void colo_proxy_checkpoint_one(NetFilterState *nf,
                                             void *opaque, Error **errp)
{
    COLOProxyState *s;
    int mode;

    if (strcmp(object_get_typename(OBJECT(nf)), TYPE_FILTER_COLO_PROXY)) {
        return;
    }

    s = FILTER_COLO_PROXY(nf);
    mode = *(int *)opaque;
    assert(s->colo_mode == mode);

    if (s->colo_mode == COLO_MODE_PRIMARY) {
        colo_proxy_primary_checkpoint(s);
    } else {
        /* secondary do checkpoint */
        colo_proxy_secondary_checkpoint(s);
    }
}

int colo_proxy_do_checkpoint(int mode)
{
    Error *err = NULL;
    qemu_foreach_netfilter(colo_proxy_checkpoint_one, &mode, &err);
    if (err) {
        error_report("colo proxy do checkpoint failed");
        return -1;
    }

    colo_do_checkpoint = false;
    checkpoint_num++;
    return 0;
}

/* Return 0 on success, or return -1 if the pkt is corrupted */
static int parse_packet_early(Packet *pkt, ConnectionKey *key)
{
    int network_length;
    uint8_t *data = pkt->data + 12; /* dgilbert: vhdr hack! */
    uint16_t l3_proto;
    uint32_t tmp_ports;
    ssize_t l2hdr_len = eth_get_l2_hdr_length(data);

    pkt->network_layer = data + ETH_HLEN;
    l3_proto = eth_get_l3_proto(data, l2hdr_len);
    if (l3_proto != ETH_P_IP) {
        if (l3_proto == ETH_P_ARP) {
            return -1;
        }
        return 0;
    }

    network_length = pkt->ip->ip_hl * 4;
    pkt->transport_layer = pkt->network_layer + network_length;
    key->ip_proto = pkt->ip->ip_p;
    key->src = pkt->ip->ip_src;
    key->dst = pkt->ip->ip_dst;

    switch (key->ip_proto) {
    case IPPROTO_TCP:
    case IPPROTO_UDP:
    case IPPROTO_DCCP:
    case IPPROTO_ESP:
    case IPPROTO_SCTP:
    case IPPROTO_UDPLITE:
        tmp_ports = *(uint32_t *)(pkt->transport_layer);
        key->src_port = ntohs(tmp_ports & 0xffff);
        key->dst_port = ntohs(tmp_ports >> 16);
        break;
    case IPPROTO_AH:
        tmp_ports = *(uint32_t *)(pkt->transport_layer + 4);
        key->src_port = ntohs(tmp_ports & 0xffff);
        key->dst_port = ntohs(tmp_ports >> 16);
        break;
    default:
        break;
    }

    return 0;
}

static Packet *packet_new(COLOProxyState *s, void *data,
                          int size, ConnectionKey *key, NetClientState *sender)
{
    Packet *pkt = g_slice_new0(Packet);

    pkt->data = data;
    pkt->size = size;
    pkt->s = s;
    pkt->sender = sender;

    if (parse_packet_early(pkt, key)) {
        packet_destroy(pkt, NULL);
        pkt = NULL;
    } else {
        pkt->creation_ms = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    }

    return pkt;
}

static void packet_destroy(void *opaque, void *user_data)
{
    Packet *pkt = opaque;
    g_free(pkt->data);
    g_slice_free(Packet, pkt);
}

/* if not found, creata a new connection and add to hash table */
static Connection *colo_proxy_get_conn(COLOProxyState *s,
            ConnectionKey *key)
{
    /* FIXME: protect colo_conn_hash */
    Connection *conn = g_hash_table_lookup(colo_conn_hash, key);

    if (conn == NULL) {
        ConnectionKey *new_key = g_malloc(sizeof(*key));

        conn = connection_new(key);
        memcpy(new_key, key, sizeof(*key));

        s->hashtable_size++;
        if (s->hashtable_size > hashtable_max_size) {
            trace_colo_proxy("colo proxy connection hashtable full, clear it");
            clear_connection_hashtable(s);
            /* TODO:clear conn_list */
        } else {
            g_hash_table_insert(colo_conn_hash, new_key, conn);
        }
    }

     return conn;
}

/* Handle incoming packets on the secondary side.
 * It doesn't need to copy the data, but it does need to pull apart the
 * header.
 * Incoming connections: record the connection with the syn ?
 * Outgoing connections: capture the syn/ack to get the primaries sequence
 */
static void secondary_from_net(NetFilterState *nf,
                               const struct iovec *iov,
                               int iovcnt,  bool from_guest)
{
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    Packet pkt;
    int network_length;
    uint8_t buffer[128]; /* I think 128 should be enough for ether+TCP header */
    size_t received, l2hdr_len;
    uint16_t l3_proto;
    ConnectionKey key;
    Connection *conn;
    struct tcphdr *tcp;
    
    pkt.data = buffer;
    received = iov_to_buf(iov, iovcnt,
                          12, /* dgilbert: virtio header skip */
                          pkt.data, 128);
#if 0
    fprintf(stderr, "%s: received=%zd:", __func__, received);
    for(i = 0; i < received; i++) {
        fprintf(stderr, "%02x ", buffer[i]);
    }
    fprintf(stderr, "\n");
#endif

    if (received < sizeof(struct eth_header) + 2 * sizeof(struct vlan_header))
    {
        /* Not safe to try and grab the l2 header length? */
        return;
    }
    l2hdr_len = eth_get_l2_hdr_length(pkt.data);
    pkt.network_layer = buffer + ETH_HLEN;
    l3_proto = eth_get_l3_proto(pkt.data, l2hdr_len);
    //fprintf(stderr, "%s: l3_proto=%d\n", __func__, l3_proto);
    if (l3_proto != ETH_P_IP) {
        return;
    }
    network_length = pkt.ip->ip_hl * 4;
    pkt.transport_layer = pkt.network_layer + network_length;
    /* TODO: More packet length checks - although buffer should be enough ? */

    key.ip_proto = pkt.ip->ip_p;
    /* For the secondary always keep the key from the view of the outside,
     * so dst is always the guest whichever direction.
     */
    if (!from_guest) {
        key.src = pkt.ip->ip_src;
        key.dst = pkt.ip->ip_dst;
    } else {
        key.dst = pkt.ip->ip_src;
        key.src = pkt.ip->ip_dst;
    }
    //fprintf(stderr,"%s: IP packet src: %s proto: %d\n",
    //               __func__, inet_ntoa(key.src), key.ip_proto);
    if (key.ip_proto != IPPROTO_TCP) {
        return;
    }
    tcp = (struct tcphdr *)pkt.transport_layer;
    if (!from_guest) {
        key.src_port = tcp->th_sport;
        key.dst_port = tcp->th_dport;
    } else {
        key.dst_port = tcp->th_sport;
        key.src_port = tcp->th_dport;
    }
    key.dst_port = ntohs(key.dst_port);
    key.src_port = ntohs(key.src_port);
    //fprintf(stderr,"%s: TCP packet src: %s flags: %x\n",
    //               __func__, inet_ntoa(key.src), tcp->th_flags);
    if (!from_guest) {
        /* Packets from the primary via the socket or
         * after failover form the real network
         */
       conn = colo_proxy_get_conn(s, &key);
        if (((tcp->th_flags & (TH_ACK | TH_SYN)) == TH_ACK) &&
           (s->status == COLO_PROXY_RUNNING)) {
            /* Incoming connection, this is the ACK from the outside */
           //fprintf(stderr, "W->G ACK; seqs: %u/%u dst-ip: %s ports (s/d): %d/%d state=%d\n",
           //        ntohl(tcp->th_seq), ntohl(tcp->th_ack), inet_ntoa(key.dst), key.src_port, key.dst_port,
           //        conn->state);
           switch (conn->state) {
           case COLO_CONN_IDLE:
               /* Odd case; we see the response before our seq/ack */
               conn->primary_seq = ntohl(tcp->th_ack) - 1;
               conn->state = COLO_CONN_SEC_IN_ACK;
               break;
           case COLO_CONN_SEC_IN_SYNACK:
               /* We have a full pair - so know the diff */
               conn->primary_seq = ntohl(tcp->th_ack) - 1;
               conn->state = COLO_CONN_SEC_IN_ESTABLISHED;
               break;
           default:
               break;
           }

        }
        if (conn->state == COLO_CONN_SEC_IN_ESTABLISHED) {
            /* Fix up the incoming ack's to match the secondarys sent sequence numbers
             * otherwise it can trigger resets
             */
            tcp_seq newseq = ntohl(tcp->th_ack);
            size_t  offset;
            newseq -= conn->primary_seq;
            newseq += conn->secondary_seq;
            offset = ((void *)&(tcp->th_ack) - pkt.data);
            offset += 12; /* dgilbert: vhdr hack */
            //fprintf(stderr,"%s: Updating ack number from %u to %u offset=%zd\n",
            //        __func__, ntohl(tcp->th_ack), newseq, offset);
            newseq = htonl(newseq);
            /* Hmm - do I need to fixup sum? */
            iov_from_buf(iov, iovcnt, offset, &newseq, sizeof(newseq));
        }

    } else {
        /* Packets from the guest */
        if ((tcp->th_flags & (TH_ACK | TH_SYN)) == (TH_ACK | TH_SYN) &&
            (s->status == COLO_PROXY_RUNNING)) {
             /* Incoming connection, SYN/ACK from the guest in response
              * to the syn from the outside.
              * Only record new connections in running mode
              */
           conn = colo_proxy_get_conn(s, &key);
           //fprintf(stderr, "G->W SYN-ACK; seqs: %u/%u src-ip: %s ports (s/d): %d/%d state=%d\n",
           //        ntohl(tcp->th_seq), ntohl(tcp->th_ack), inet_ntoa(key.src), key.src_port, key.dst_port,
           //        conn->state);
           switch (conn->state) {
           case COLO_CONN_IDLE:
               conn->secondary_seq = ntohl(tcp->th_seq);
               conn->state = COLO_CONN_SEC_IN_SYNACK;
               break;
           case COLO_CONN_SEC_IN_ACK:
               conn->secondary_seq = ntohl(tcp->th_seq);
               /* We have a full pair - so know the diff */
               conn->state = COLO_CONN_SEC_IN_ESTABLISHED;
               break;
           default:
               break;
           }
        } else {
            /* If we've got an existing established connection then tweak
             * the sequence numbers to match the primary.
             */
            conn = colo_proxy_get_conn(s, &key);
            //fprintf(stderr, "G->W; seqs: %u/%u src-ip: %s ports (s/d): %d/%d flags=%x state=%d\n",
            //        ntohl(tcp->th_seq), ntohl(tcp->th_ack), inet_ntoa(key.src), key.src_port, key.dst_port,
            //        tcp->th_flags, conn->state);

            if (conn->state == COLO_CONN_SEC_IN_ESTABLISHED) {
                tcp_seq newseq = ntohl(tcp->th_seq);
                size_t  offset;
                newseq -= conn->secondary_seq;
                newseq += conn->primary_seq;
                offset = ((void *)&(tcp->th_seq) - pkt.data);
                offset += 12; /* dgilbert: vhdr hack */
                //fprintf(stderr,"%s: Updating sequence number from %u to %u offset=%zd\n",
                //        __func__, ntohl(tcp->th_seq), newseq, offset);
                newseq = htonl(newseq);
                /* Hmm - do I need to fixup crc? */
                iov_from_buf(iov, iovcnt, offset, &newseq, sizeof(newseq));
            }
        }
    }
}

/* Primary: Outgoing packet */
/* Note: 0=failure, !0 is success */
static ssize_t colo_proxy_enqueue_primary_packet(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    /*
     * 1. parse packet, try to get connection factor
     * (src_ip, src_port, dest_ip, dest_port)
     * 2. enqueue the packet to primary_packet_list by connection
     */
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    ssize_t size = iov_size(iov, iovcnt);
    char *buf = g_malloc0(size); /* free by packet destory */
    ConnectionKey key = {{ 0 } };
    Packet *pkt;
    Connection *conn;

    iov_to_buf(iov, iovcnt, 0, buf, size);
    pkt = packet_new(s, buf, size, &key, sender);
    if (!pkt) {
        return 0;
    }

    /* colo_proxy_dump_headers("PG->Q", pkt); */

    conn = colo_proxy_get_conn(s, &key);
    if (!conn->processing) {
        g_queue_push_tail(&s->conn_list, conn);
        conn->processing = true;
    }

    qemu_mutex_lock(&conn->list_lock);
    g_queue_push_tail(&conn->primary_list, pkt);
    qemu_mutex_unlock(&conn->list_lock);
    qemu_event_set(&s->need_compare_ev);
    return 1;
}

/* 0 is success, -1 is failure */
static ssize_t
colo_proxy_enqueue_secondary_packet(NetFilterState *nf,
                                    char *buf, int len)
{
    /*
     * 1, parse packet, try to get connection factor
     * (src_ip, src_port, dest_ip, dest_port)
     * 2. enqueue the packet to secondary_packet_list by connection
    */
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    Connection *conn;
    ConnectionKey key = {{ 0 } };
    Packet *pkt = packet_new(s, buf, len, &key, NULL);

    if (!pkt) {
        // - mostly this is just arps error_report("%s paket_new failed", __func__);
        return -1;
    }

    conn = colo_proxy_get_conn(s, &key);
    if (!conn->processing) {
        g_queue_push_tail(&s->conn_list, conn);
        conn->processing = true;
    }

    /* In primary notify compare thead */
    qemu_mutex_lock(&conn->list_lock);
    g_queue_push_tail(&conn->secondary_list, pkt);
    qemu_mutex_unlock(&conn->list_lock);
    qemu_event_set(&s->need_compare_ev);
    return 0;
}

/*
 * send a packet to peer
 * >=0: success
 * <0: fail
 */
static ssize_t colo_proxy_sock_send(NetFilterState *nf,
                                         const struct iovec *iov,
                                         int iovcnt)
{
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    ssize_t ret = 0;
    uint64_t size = 0;
    uint64_t tosend = 0;
    struct iovec sizeiov = {
        .iov_base = &tosend,
        .iov_len = sizeof(tosend)
    };
    size = iov_size(iov, iovcnt);
    if (!size) {
        return 0;
    }
    /* The packets sent should always be smaller than 16bit anyway,
     * store a sequence number at the top.
     */
    assert(size < (((uint64_t)1) << 32));
    tosend = size | ((uint64_t)checkpoint_num) << 32;
    ret = iov_send(s->sockfd, &sizeiov, 1, 0, sizeof(size));
    if (ret < 0) {
        fprintf(stderr,"%s: Failed to send size with %zd\n", __func__, ret);
        return ret;
    }
    ret = iov_send(s->sockfd, iov, iovcnt, 0, size);
    return ret;
}

/*
 * receive a packet from peer
 * in primary: enqueue packet to secondary_list
 * in secondary: pass packet to next
 */
static void colo_proxy_sock_receive(void *opaque)
{
    NetFilterState *nf = opaque;
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    uint64_t len = 0;
    uint32_t received_checkpoint_num;
    struct iovec sizeiov = {
        .iov_base = &len,
        .iov_len = sizeof(len)
    };

    iov_recv(s->sockfd, &sizeiov, 1, 0, sizeof(len));
    received_checkpoint_num = len / (((uint64_t)1) << 32);
    len &= 0xfffffffful;
    if (len > 0 && len < NET_BUFSIZE) {
        char *buf = g_malloc0(len);
        struct iovec iov = {
            .iov_base = buf,
            .iov_len = len
        };

        iov_recv(s->sockfd, &iov, 1, 0, len);
        if (s->colo_mode == COLO_MODE_PRIMARY) {
            /* I don't think this should happen given the sequencing of
             * proxy flushing, however receiving an old packet would confuse
             * stuff.
             */
            if (received_checkpoint_num != checkpoint_num) {
                fprintf(stderr, "%s: discarding packet from wrong checkpoint %d, current=%d\n",
                        __func__, received_checkpoint_num, checkpoint_num);
               return;
            }
            colo_proxy_enqueue_secondary_packet(nf, buf, len);
            /* buf will be release when pakcet destroy */
        } else {
            /* The packets to the secondary come from the outside world,
             * so the checkpoint number is irrelevant for us
             */
            secondary_from_net(nf, &iov, 1, false /* not from guest */ );
            qemu_net_queue_send(s->incoming_queue, nf->netdev,
                            0, (const uint8_t *)buf, len, NULL);
        }
    }
}

/*
 * colo primary handle host's normal send and
 * recv packets to primary guest
 * return:          > 0       success (don't pass the packet further)
 *                  0         success, pass the packet on
 *                  < 0       failed
 */
static ssize_t colo_proxy_primary_handler(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    ssize_t ret = 0;

    if (s->status != COLO_PROXY_RUNNING) {
        /* proxy is not started or failovered */
        return 0;
    }

    /*
     * if packet's direction=rx
     * enqueue packets to primary queue
     * and wait secondary queue to compare
     * if packet's direction=tx
     * enqueue packets then send packets to
     * secondary and flush  queued packets
    */
    if (sender == nf->netdev) {
        /* Incoming packet received from network */
        /* Send a copy of the incoming data to the secondary */
        ret = colo_proxy_sock_send(nf, iov, iovcnt);
        if (ret > 0) {
            ret = 0;
        }
    } else {
        /* ??? Outgoing packet from primary, hold it in proxy
         * until the secondary sends the matching packet.
         */
        ret = colo_proxy_enqueue_primary_packet(nf, sender, flags, iov,
                    iovcnt, sent_cb);
    }

    return ret;
}

/*
 * colo secondary handle host's normal send and
 * recv packets to secondary guest
 * return:          > 0       success (don't pass the packet further)
 *                  0         success, pass the packet on
 *                  < 0       failed
 */
static ssize_t colo_proxy_secondary_handler(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    ssize_t ret = 0;

    /*
     * if packet's direction=rx
     * enqueue packets and send to
     * primary QEMU
     * if packet's direction=tx
     * record PVM's packet inital seq & adjust
     * client's ack,send adjusted packets to SVM(next version will be do)
     */
    if (sender == nf->netdev) {
        /* This packet is sent by netdev itself */
        /* Incoming packet from the net - but on the secondary all the packets
         * to the guest are actually going to the primary, we don't see real ones.
         */
        if (s->status == COLO_PROXY_DONE) {
            /* But after failover we have to continue updating the sequence
             * numbers of existing connections.
             */
            secondary_from_net(nf, iov, iovcnt, false /* from net */);
        }
    } else {
        /* Outgoing packets from secondary guest - send to
         * primary for comparison
         * TODO: [outgoing conn] when we see the syn create the connection record
         * TODO: When we see the reset/fin mark as closed?
         */
        secondary_from_net(nf, iov, iovcnt, true /* from guest */);
        if (s->status == COLO_PROXY_RUNNING) {
            ret = colo_proxy_sock_send(nf, iov, iovcnt);
        }
    }

    return ret;
}

/* Return 0: send it to the rest of the filters
 *      !=0: Doesn't get sent???
 */
static ssize_t colo_proxy_receive_iov(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    /*
     * We return size when buffer a packet, the sender will take it as
     * a already sent packet, so sent_cb should not be called later.
     *
     */
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    ssize_t ret = 0;

    if (s->colo_mode == COLO_MODE_PRIMARY) {
        ret = colo_proxy_primary_handler(nf, sender, flags,
                    iov, iovcnt, sent_cb);
    } else {
        ret = colo_proxy_secondary_handler(nf, sender, flags,
                    iov, iovcnt, sent_cb);
    }
    if (ret == 0) {
        /* We've queued this packet and will release it ourselves later */
        return 0;
    }
    if (ret < 0) {
        trace_colo_proxy("colo_proxy_receive_iov running failed");
    }
    /* We stole this packet - dont pass it further */
    return iov_size(iov, iovcnt);
}

static void colo_proxy_cleanup(NetFilterState *nf)
{
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    close(s->sockfd);
    s->sockfd = -1;
    qemu_del_vm_change_state_handler(s->change_state_handler);
    timer_del(s->timer);
    qemu_event_destroy(&s->need_compare_ev);
}

/* wait for peer connecting
 * NOTE: this function will block the caller
 * 0 on success, otherwise returns -1
 */
static int colo_wait_incoming(COLOProxyState *s)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int accept_sock, err;
    int fd = inet_listen(s->addr, NULL, 256, SOCK_STREAM, 0, NULL);

    if (fd < 0) {
        error_report("colo proxy listen failed");
        return -1;
    }

    do {
        accept_sock = qemu_accept(fd, (struct sockaddr *)&addr, &addrlen);
        err = socket_error();
    } while (accept_sock < 0 && err == EINTR);
    closesocket(fd);

    if (accept_sock < 0) {
        error_report("colo proxy accept failed(%s)", strerror(err));
        return -1;
    }
    s->sockfd = accept_sock;
    socket_set_nodelay(s->sockfd);
    qemu_set_fd_handler(s->sockfd, colo_proxy_sock_receive, NULL, (void *)s);

    return 0;
}

/* try to connect listening server
 * 0 on success, otherwise something wrong
 */
static ssize_t colo_proxy_connect(COLOProxyState *s)
{
    int sock;
    sock = inet_connect(s->addr, NULL);

    if (sock < 0) {
        error_report("colo proxy inet_connect failed");
        return -1;
    }
    s->sockfd = sock;
    socket_set_nodelay(s->sockfd);
    qemu_set_fd_handler(s->sockfd, colo_proxy_sock_receive, NULL, (void *)s);

    return 0;
}

/* Wait for either 'wait_ms' or until a miscompare happens (if earlier) */
bool colo_proxy_wait_for_diff(uint64_t wait_ms)
{
    struct timespec t;
    int err = 0;

    trace_colo_proxy_wait_for_diff_entry(wait_ms);
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += wait_ms / 1000l;
    t.tv_nsec += 1000000l * (wait_ms % 1000l);
    t.tv_sec += t.tv_nsec / 1000000000l;
    t.tv_nsec = t.tv_nsec % 1000000000l;

    while (!colo_do_checkpoint) {
        err = pthread_cond_timedwait(&colo_proxy_signal_cond,
                                     &colo_proxy_signal_mutex,
                                     &t);
        if (err) {
            break;
        }
    }
    trace_colo_proxy_wait_for_diff_exit(colo_do_checkpoint, err);
    return colo_do_checkpoint;
}

static void colo_proxy_notify_checkpoint(void)
{
    trace_colo_proxy("colo_proxy_notify_checkpoint");
    colo_do_checkpoint = true;
    /* This is harmless if no one is waiting */
    pthread_cond_broadcast(&colo_proxy_signal_cond);
}

/* dgilbert: Hack - this really needs fixing so it's always defined */
#ifndef trace_event_get_state
#define trace_event_get_state(x) (false)
#endif

/* Primary
 * the TCP packets sent here are equal length and on the same port
 * between the same host pair.
 *
 * return:    0  means packet same
 *            > 0 || < 0 means packet different
 *
 * We ignore sequence number differences for syn/ack packets
 *   - the secondary will fix up future packets sequence numbers
 */
static int colo_packet_compare_tcp(Packet *ppkt, Packet *spkt)
{
    struct tcphdr *ptcp, *stcp;
    int res;
    char *sdebug, *ddebug;
    ptrdiff_t offset;

    ptcp = (struct tcphdr *)ppkt->transport_layer;
    stcp = (struct tcphdr *)spkt->transport_layer;

    /* Initial is compare the whole packet */
    offset = 12; /* Hack! Skip virtio header */

    if (ptcp->th_flags == stcp->th_flags &&
        ((ptcp->th_flags & (TH_ACK | TH_SYN)) == (TH_ACK | TH_SYN))) {
        /* This is the syn/ack response from the guest to an incoming
         * connection; the secondary won't have matched the sequence number
         * Note: We should probably compare the IP level?
         * Note hack: This already has the virtio offset
         */
        offset = sizeof(ptcp->th_ack) + (void *)&ptcp->th_ack - ppkt->data;
    }
    // Note - we want to compare everything as long as it's not the syn/ack?
    assert(offset > 0);
    assert(spkt->size > offset);

    /* The 'identification' field in the IP header is *very* random
     * it almost never matches.  Fudge this by ignoring differences in
     * unfragmented packets; they'll normally sort themselves out if different
     * anyway, and it should recover at the TCP level.
     * An alternative would be to get both the primary and secondary to rewrite
     * somehow; but that would need some sync traffic to sync the state
     */
    if (ntohs(ppkt->ip->ip_off) & IP_DF) {
        spkt->ip->ip_id = ppkt->ip->ip_id;
        /* and the sum will be different if the IDs were different */
        spkt->ip->ip_sum = ppkt->ip->ip_sum;
    }

    res = memcmp(ppkt->data + offset, spkt->data + offset,
                 (spkt->size - offset));
    if (trace_event_get_state(TRACE_COLO_PROXY_MISCOMPARE) && res) {
        sdebug=strdup(inet_ntoa(ppkt->ip->ip_src));
        ddebug=strdup(inet_ntoa(ppkt->ip->ip_dst));
        fprintf(stderr,"%s: src/dst: %s/%s offset=%zd p: seq/ack=%u/%u s: seq/ack=%u/%u res=%d flags=%x/%x\n", __func__,
                   sdebug, ddebug, offset,
                   ntohl(ptcp->th_seq), ntohl(ptcp->th_ack),
                   ntohl(stcp->th_seq), ntohl(stcp->th_ack), res, ptcp->th_flags, stcp->th_flags);
        if (res && (ptcp->th_seq == stcp->th_seq)) {
            fprintf(stderr, "Primary len=%d: ", ppkt->size);
            colo_proxy_dump_packet(ppkt);
            fprintf(stderr, "Secondary len=%d: ", spkt->size);
            colo_proxy_dump_packet(spkt);
        }
        g_free(sdebug);
        g_free(ddebug);
    }
    return res;
}

/*
 * The IP packets sent by primary and secondary
 * will be comparison in here
 * TODO： support ip fragment
 * return:    0  means packet same
 *            > 0 || < 0 means packet different
 */
static int colo_packet_compare(Packet *spkt, Packet *ppkt)
{
    trace_colo_proxy("colo_packet_compare data   ppkt");
    trace_colo_proxy_packet_size(ppkt->size);
    trace_colo_proxy_packet_src(inet_ntoa(ppkt->ip->ip_src));
    trace_colo_proxy_packet_dst(inet_ntoa(ppkt->ip->ip_dst));
    /*colo_proxy_dump_packet(ppkt);*/
    trace_colo_proxy("colo_packet_compare data   spkt");
    trace_colo_proxy_packet_size(spkt->size);
    trace_colo_proxy_packet_src(inet_ntoa(spkt->ip->ip_src));
    trace_colo_proxy_packet_dst(inet_ntoa(spkt->ip->ip_dst));
    /*colo_proxy_dump_packet(spkt);*/

    if (ppkt->size == spkt->size) {
        int res;
        if (ppkt->ip->ip_p == IPPROTO_TCP) {
            return colo_packet_compare_tcp(ppkt, spkt);
        }
        res = memcmp(ppkt->data, spkt->data, spkt->size - 12 /* dgilbert: vhdr hack! */);
        if (trace_event_get_state(TRACE_COLO_PROXY_MISCOMPARE) && res) {
            fprintf(stderr, "%s: non-TCP miscompare proto=%d:\n", __func__, ppkt->ip->ip_p);
            fprintf(stderr, "Primary: ");
            colo_proxy_dump_packet(ppkt);
            fprintf(stderr, "Secondary: ");
            colo_proxy_dump_packet(spkt);
        }
        return res;
    } else {
        if (trace_event_get_state(TRACE_COLO_PROXY_MISCOMPARE)) {
            fprintf(stderr, "%s: Packet size difference %p/%p %d/%d\n", __func__, ppkt, spkt, ppkt->size, spkt->size);
        }
        trace_colo_proxy("colo_packet_compare size not same");
        return -1;
    }
}

static void colo_old_packet_check(void *opaque_packet, void *opaque_found)
{
    int64_t now;
    bool *found_old = (bool *)opaque_found;
    Packet *ppkt = (Packet *)opaque_packet;

    if (*found_old) {
        /* Someone found an old packet earlier in the queue */
        return;
    }

    now = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    if ((ppkt->creation_ms < now) &&
        ((now-ppkt->creation_ms) > REGULAR_CHECK_MS)) {
        trace_colo_old_packet_check_found(ppkt->creation_ms);
        *found_old = true;
    }
}

static void colo_compare_connection(void *opaque, void *user_data)
{
    Connection *conn = opaque;
    Packet *ppkt = NULL;
    Packet *spkt = NULL;
    bool found_old;
    int result;

    while (!g_queue_is_empty(&conn->primary_list) &&
                !g_queue_is_empty(&conn->secondary_list)) {
        qemu_mutex_lock(&conn->list_lock);
        ppkt = g_queue_pop_head(&conn->primary_list);
        spkt = g_queue_pop_head(&conn->secondary_list);
        qemu_mutex_unlock(&conn->list_lock);
        result = colo_packet_compare(spkt, ppkt);
        if (!result) {
            /*colo_proxy_dump_headers("PQ=>W", ppkt); */
            colo_send_primary_packet(ppkt, NULL);
            trace_colo_proxy("packet same and release packet");
        } else {
            /* Leave it on the list since we're going to cause a checkpoint
             * and flush and we need to send the packets out
             */
            qemu_mutex_lock(&conn->list_lock);
            g_queue_push_head(&conn->primary_list, ppkt);
            qemu_mutex_unlock(&conn->list_lock);
            packet_destroy(spkt, NULL);
            trace_colo_proxy("packet different");
            if (trace_event_get_state(TRACE_COLO_PROXY_MISCOMPARE)) {
                fprintf(stderr, "%s: miscompare for %d conn: %s,%d:", __func__, checkpoint_num,
                        inet_ntoa(conn->ck.src), conn->ck.src_port);
                fprintf(stderr, "-> %s,%d\n",
                        inet_ntoa(conn->ck.dst), conn->ck.dst_port);
            }
            colo_proxy_notify_checkpoint();
            return;
        }
    }

    /* Look for old packets that the secondary hasn't matched, if we have some
     * then we have to checkpoint to wake the secondary up.
    */
    qemu_mutex_lock(&conn->list_lock);
    found_old = false;
    g_queue_foreach(&conn->primary_list, colo_old_packet_check, &found_old);
    qemu_mutex_unlock(&conn->list_lock);
    if (found_old) {
        colo_proxy_notify_checkpoint();
    }
}

static void *colo_proxy_compare_thread(void *opaque)
{
    COLOProxyState *s = opaque;

    while (s->status == COLO_PROXY_RUNNING) {
        qemu_event_wait(&s->need_compare_ev);
        qemu_event_reset(&s->need_compare_ev);
        g_queue_foreach(&s->conn_list, colo_compare_connection, NULL);
    }

    return NULL;
}

static void colo_proxy_start_one(NetFilterState *nf,
                                      void *opaque, Error **errp)
{
    COLOProxyState *s;
    int mode, ret;

    if (strcmp(object_get_typename(OBJECT(nf)), TYPE_FILTER_COLO_PROXY)) {
        return;
    }

    mode = *(int *)opaque;
    s = FILTER_COLO_PROXY(nf);
    assert(s->colo_mode == mode);

    if (s->colo_mode == COLO_MODE_PRIMARY) {
        char thread_name[1024];

        ret = colo_proxy_connect(s);
        if (ret) {
            error_setg(errp, "colo proxy connect failed");
            return ;
        }

        s->status = COLO_PROXY_RUNNING;
        sprintf(thread_name, "proxy:%s", nf->netdev_id);
        qemu_thread_create(&s->thread, thread_name,
                                colo_proxy_compare_thread, s,
                                QEMU_THREAD_JOINABLE);
    } else {
        ret = colo_wait_incoming(s);
        if (ret) {
            error_setg(errp, "colo proxy wait incoming failed");
            return ;
        }
        s->status = COLO_PROXY_RUNNING;
    }
}

int colo_proxy_start(int mode)
{
    Error *err = NULL;
    qemu_foreach_netfilter(colo_proxy_start_one, &mode, &err);
    if (err) {
        return -1;
    }
    return 0;
}

static void colo_proxy_stop_one(NetFilterState *nf,
                                      void *opaque, Error **errp)
{
    COLOProxyState *s;
    int mode;

    if (strcmp(object_get_typename(OBJECT(nf)), TYPE_FILTER_COLO_PROXY)) {
        return;
    }

    s = FILTER_COLO_PROXY(nf);
    mode = *(int *)opaque;
    assert(s->colo_mode == mode);

    s->status = COLO_PROXY_DONE;
    if (s->sockfd >= 0) {
        qemu_set_fd_handler(s->sockfd, NULL, NULL, NULL);
        closesocket(s->sockfd);
    }
    if (s->colo_mode == COLO_MODE_PRIMARY) {
        colo_proxy_primary_checkpoint(s);
        qemu_event_set(&s->need_compare_ev);
        qemu_thread_join(&s->thread);
    } else {
        /* Let the secondary proxy carry on, it has to keep updating sequence
         * numbers for connections that are open.
         */
    }
}

void colo_proxy_stop(int mode)
{
    Error *err = NULL;
    qemu_foreach_netfilter(colo_proxy_stop_one, &mode, &err);
}

/* Prod the compare thread regularly so it can watch for any packets
 * that the secondary hasn't produced equivalents of.
 */
static void colo_proxy_regular(void *opaque)
{
    COLOProxyState *s = opaque;

    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                        REGULAR_CHECK_MS);
    if (s->colo_mode == COLO_MODE_PRIMARY) {
        qemu_event_set(&s->need_compare_ev);
    }
}

static void colo_proxy_setup(NetFilterState *nf, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(nf);
    ssize_t factor = 8;
    struct sysinfo si;

    if (!s->addr) {
        error_setg(errp, "filter colo_proxy needs 'addr' property set!");
        return;
    }

    if (nf->direction != NET_FILTER_DIRECTION_ALL) {
        error_setg(errp, "colo need queue all packet,"
                        "please startup colo-proxy with queue=all\n");
        return;
    }

    s->sockfd = -1;
    s->hashtable_size = 0;
    colo_do_checkpoint = false;
    qemu_event_init(&s->need_compare_ev, false);

    /*
     *  Idea from kernel tcp.c: use 1/16384 of memory.  On i386: 32MB
     * machine has 512 buckets. >= 1GB machines have 16384 buckets.
     * default factor = 8
     */
    sysinfo(&si);
    hashtable_max_size = 16384;
    if (si.totalram > (1024 * 1024 * 1024)) {
        hashtable_max_size = 16384;
    }
    if (hashtable_max_size < 32) {
        hashtable_max_size = 32;
    }

    hashtable_max_size = hashtable_max_size * factor;
    s->incoming_queue = qemu_new_net_queue(qemu_netfilter_pass_to_next, nf);
    s->change_state_handler =
        qemu_add_vm_change_state_handler(proxy_state_change_handler, s);
    colo_conn_hash = g_hash_table_new_full(connection_key_hash,
                                           connection_key_equal,
                                           g_free,
                                           connection_destroy);
    g_queue_init(&s->conn_list);

    /* A regular timer to kick any packets that the secondary doesn't match */
    s->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, /* Only when guest runs */
                            colo_proxy_regular, s);
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                        REGULAR_CHECK_MS);
}

static void colo_proxy_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    nfc->setup = colo_proxy_setup;
    nfc->cleanup = colo_proxy_cleanup;
    nfc->receive_iov = colo_proxy_receive_iov;

    /* We take the lock, it's only ever released by the wait on
     * the colo_proxy_signal_cond cond variable.
     */
    pthread_mutex_lock(&colo_proxy_signal_mutex);
}

static int colo_proxy_get_mode(Object *obj, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);

    return s->colo_mode;
}

static void
colo_proxy_set_mode(Object *obj, int mode, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);

    s->colo_mode = mode;
}

static char *colo_proxy_get_addr(Object *obj, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);

    return g_strdup(s->addr);
}

static void
colo_proxy_set_addr(Object *obj, const char *value, Error **errp)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);
    g_free(s->addr);
    s->addr = g_strdup(value);
    if (!s->addr) {
        error_setg(errp, "colo_proxy needs 'addr'"
                     "property set!");
        return;
    }
}

static void colo_proxy_init(Object *obj)
{
    object_property_add_enum(obj, "mode", "COLOMode", COLOMode_lookup,
                             colo_proxy_get_mode, colo_proxy_set_mode, NULL);
    object_property_add_str(obj, "addr", colo_proxy_get_addr,
                            colo_proxy_set_addr, NULL);
}

static void colo_proxy_fini(Object *obj)
{
    COLOProxyState *s = FILTER_COLO_PROXY(obj);
    g_free(s->addr);
}

static const TypeInfo colo_proxy_info = {
    .name = TYPE_FILTER_COLO_PROXY,
    .parent = TYPE_NETFILTER,
    .class_init = colo_proxy_class_init,
    .instance_init = colo_proxy_init,
    .instance_finalize = colo_proxy_fini,
    .instance_size = sizeof(COLOProxyState),
};

static void register_types(void)
{
    type_register_static(&colo_proxy_info);
}

type_init(register_types);
