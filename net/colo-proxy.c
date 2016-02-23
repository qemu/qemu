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
} colo_conn_state;

static const char *conn_state_str[] = {
     "Idle",
     "P:In Syn",
     "P:In PSynAck",
     "P:In SSynAck",
     "P:In SynAck",
     "P:Established"
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
    /* connection primary send queue: element type: Packet */
    GQueue primary_list;
    /* connection secondary send queue: element type: Packet */
    GQueue secondary_list;
     /* flag to enqueue unprocessed_connections */
    bool processing;
    int ip_proto;

    void *proto; /* tcp only now */

    colo_conn_state state;
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
        printf("%02x ", ((uint8_t *)pkt->data)[i]);
    }
    printf("\n");
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
    monitor_printf(mon, "  %s:%d %s processing: %d\n ", inet_ntoa(ck->dst), ck->dst_port,
                   conn_state_str[conn->state],
                   conn->processing);

    monitor_printf(mon, "  Primary list:\n");
    g_queue_foreach(&conn->primary_list, info_packet, mon);

    monitor_printf(mon, "  Secondary list:\n");
    g_queue_foreach(&conn->secondary_list, info_packet, mon);
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

    g_queue_foreach(&conn->primary_list, packet_destroy, NULL);
    g_queue_free(&conn->primary_list);
    g_queue_foreach(&conn->secondary_list, packet_destroy, NULL);
    g_queue_free(&conn->secondary_list);
    g_slice_free(Connection, conn);
}

static Connection *connection_new(ConnectionKey *key)
{
    Connection *conn = g_slice_new(Connection);

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

static void colo_flush_connection(void *opaque, void *user_data)
{
    Connection *conn = opaque;
    Packet *pkt = NULL;

    while (!g_queue_is_empty(&conn->primary_list)) {
        pkt = g_queue_pop_head(&conn->primary_list);
        colo_send_primary_packet(pkt, NULL);
    }
    while (!g_queue_is_empty(&conn->secondary_list)) {
        pkt = g_queue_pop_head(&conn->secondary_list);
        packet_destroy(pkt, NULL);
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
    Packet *pkt = g_slice_new(Packet);

    pkt->data = data;
    pkt->size = size;
    pkt->s = s;
    pkt->sender = sender;

    if (parse_packet_early(pkt, key)) {
        packet_destroy(pkt, NULL);
        pkt = NULL;
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

/* Primary: Outgoing packet */
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

    conn = colo_proxy_get_conn(s, &key);
    if (!conn->processing) {
        g_queue_push_tail(&s->conn_list, conn);
        conn->processing = true;
    }

    g_queue_push_tail(&conn->primary_list, pkt);
    qemu_event_set(&s->need_compare_ev);
    return 1;
}

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
    g_queue_push_tail(&conn->secondary_list, pkt);
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
            qemu_net_queue_send(s->incoming_queue, nf->netdev,
                            0, (const uint8_t *)buf, len, NULL);
        }
    }
}

/*
 * colo primary handle host's normal send and
 * recv packets to primary guest
 * return:          >= 0      success
 *                  < 0       failed
 */
static ssize_t colo_proxy_primary_handler(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    ssize_t ret = 0;

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
 * return:          >= 0      success
 *                  < 0       failed
 */
static ssize_t colo_proxy_secondary_handler(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
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
        /* ??? Incoming packet from net, ignore - we only pass the
         * packets from the socket to the guest
         */
    } else {
        /* Outgoing packets from secondary guest - send to
         * primary for comparison
         */
        ret = colo_proxy_sock_send(nf, iov, iovcnt);
    }

    return ret;
}

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

    if (s->status != COLO_PROXY_RUNNING) {
        /* proxy is not started or failovered */
        return 0;
    }

    if (s->colo_mode == COLO_MODE_PRIMARY) {
        ret = colo_proxy_primary_handler(nf, sender, flags,
                    iov, iovcnt, sent_cb);
        if (ret == 0) {
            /* We've queued this packet and will release it ourselves later */
            return 0;
        }
    } else {
        ret = colo_proxy_secondary_handler(nf, sender, flags,
                    iov, iovcnt, sent_cb);
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
    char *sdebug, *pdebug;
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

    res = memcmp(ppkt->data + offset, spkt->data + offset,
                 (spkt->size - offset));
    pdebug=strdup(inet_ntoa(ppkt->ip->ip_src));
    sdebug=strdup(inet_ntoa(spkt->ip->ip_src));
    fprintf(stderr,"%s: src: %s/%s offset=%zd p: seq/ack=%d/%d s: seq/ack=%d/%d res=%d flags=%x/%x\n", __func__,
                   pdebug, sdebug, offset,
                   ptcp->th_seq, ptcp->th_ack,
                   stcp->th_seq, stcp->th_ack, res, ptcp->th_flags, stcp->th_flags);
    g_free(pdebug);
    g_free(sdebug);
    return res;
}

/*
 * The IP packets sent by primary and secondary
 * will be comparison in here
 * TODO： support ip fragment
 * return:    0  means packet same
 *            > 0 || < 0 means packet different
 */
static int colo_packet_compare(Packet *ppkt, Packet *spkt)
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
        if (ppkt->ip->ip_p == IPPROTO_TCP) {
            return colo_packet_compare_tcp(ppkt, spkt);
        }
        return memcmp(ppkt->data, spkt->data, spkt->size - 12 /* dgilbert: vhdr hack! */);
    } else {
        trace_colo_proxy("colo_packet_compare size not same");
        return -1;
    }
}

static void colo_compare_connection(void *opaque, void *user_data)
{
    Connection *conn = opaque;
    Packet *pkt = NULL;
    GList *result = NULL;

    while (!g_queue_is_empty(&conn->primary_list) &&
                !g_queue_is_empty(&conn->secondary_list)) {
        pkt = g_queue_pop_head(&conn->primary_list);
        result = g_queue_find_custom(&conn->secondary_list,
                    pkt, (GCompareFunc)colo_packet_compare);
        if (result) {
            colo_send_primary_packet(pkt, NULL);
            trace_colo_proxy("packet same and release packet");
        } else {
            g_queue_push_tail(&conn->primary_list, pkt);
            trace_colo_proxy("packet different");
            colo_proxy_notify_checkpoint();
            break;
        }
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
        colo_proxy_secondary_checkpoint(s);
    }
}

void colo_proxy_stop(int mode)
{
    Error *err = NULL;
    qemu_foreach_netfilter(colo_proxy_stop_one, &mode, &err);
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
    colo_conn_hash = g_hash_table_new_full(connection_key_hash,
                                           connection_key_equal,
                                           g_free,
                                           connection_destroy);
    g_queue_init(&s->conn_list);
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
