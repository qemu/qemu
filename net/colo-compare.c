/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "net/net.h"
#include "net/eth.h"
#include "qom/object_interfaces.h"
#include "qemu/iov.h"
#include "qom/object.h"
#include "qemu/typedefs.h"
#include "net/queue.h"
#include "chardev/char-fe.h"
#include "qemu/sockets.h"
#include "qapi-visit.h"
#include "net/colo.h"
#include "sysemu/iothread.h"

#define TYPE_COLO_COMPARE "colo-compare"
#define COLO_COMPARE(obj) \
    OBJECT_CHECK(CompareState, (obj), TYPE_COLO_COMPARE)

#define COMPARE_READ_LEN_MAX NET_BUFSIZE
#define MAX_QUEUE_SIZE 1024

/* TODO: Should be configurable */
#define REGULAR_PACKET_CHECK_MS 3000

/*
 *  + CompareState ++
 *  |               |
 *  +---------------+   +---------------+         +---------------+
 *  |   conn list   + - >      conn     + ------- >      conn     + -- > ......
 *  +---------------+   +---------------+         +---------------+
 *  |               |     |           |             |          |
 *  +---------------+ +---v----+  +---v----+    +---v----+ +---v----+
 *                    |primary |  |secondary    |primary | |secondary
 *                    |packet  |  |packet  +    |packet  | |packet  +
 *                    +--------+  +--------+    +--------+ +--------+
 *                        |           |             |          |
 *                    +---v----+  +---v----+    +---v----+ +---v----+
 *                    |primary |  |secondary    |primary | |secondary
 *                    |packet  |  |packet  +    |packet  | |packet  +
 *                    +--------+  +--------+    +--------+ +--------+
 *                        |           |             |          |
 *                    +---v----+  +---v----+    +---v----+ +---v----+
 *                    |primary |  |secondary    |primary | |secondary
 *                    |packet  |  |packet  +    |packet  | |packet  +
 *                    +--------+  +--------+    +--------+ +--------+
 */
typedef struct CompareState {
    Object parent;

    char *pri_indev;
    char *sec_indev;
    char *outdev;
    CharBackend chr_pri_in;
    CharBackend chr_sec_in;
    CharBackend chr_out;
    SocketReadState pri_rs;
    SocketReadState sec_rs;
    bool vnet_hdr;

    /*
     * Record the connection that through the NIC
     * Element type: Connection
     */
    GQueue conn_list;
    /* Record the connection without repetition */
    GHashTable *connection_track_table;

    IOThread *iothread;
    GMainContext *worker_context;
    QEMUTimer *packet_check_timer;
} CompareState;

typedef struct CompareClass {
    ObjectClass parent_class;
} CompareClass;

enum {
    PRIMARY_IN = 0,
    SECONDARY_IN,
};

static int compare_chr_send(CompareState *s,
                            const uint8_t *buf,
                            uint32_t size,
                            uint32_t vnet_hdr_len);

static gint seq_sorter(Packet *a, Packet *b, gpointer data)
{
    struct tcphdr *atcp, *btcp;

    atcp = (struct tcphdr *)(a->transport_header);
    btcp = (struct tcphdr *)(b->transport_header);
    return ntohl(atcp->th_seq) - ntohl(btcp->th_seq);
}

/*
 * Return 0 on success, if return -1 means the pkt
 * is unsupported(arp and ipv6) and will be sent later
 */
static int packet_enqueue(CompareState *s, int mode)
{
    ConnectionKey key;
    Packet *pkt = NULL;
    Connection *conn;

    if (mode == PRIMARY_IN) {
        pkt = packet_new(s->pri_rs.buf,
                         s->pri_rs.packet_len,
                         s->pri_rs.vnet_hdr_len);
    } else {
        pkt = packet_new(s->sec_rs.buf,
                         s->sec_rs.packet_len,
                         s->sec_rs.vnet_hdr_len);
    }

    if (parse_packet_early(pkt)) {
        packet_destroy(pkt, NULL);
        pkt = NULL;
        return -1;
    }
    fill_connection_key(pkt, &key);

    conn = connection_get(s->connection_track_table,
                          &key,
                          &s->conn_list);

    if (!conn->processing) {
        g_queue_push_tail(&s->conn_list, conn);
        conn->processing = true;
    }

    if (mode == PRIMARY_IN) {
        if (g_queue_get_length(&conn->primary_list) <=
                               MAX_QUEUE_SIZE) {
            g_queue_push_tail(&conn->primary_list, pkt);
            if (conn->ip_proto == IPPROTO_TCP) {
                g_queue_sort(&conn->primary_list,
                             (GCompareDataFunc)seq_sorter,
                             NULL);
            }
        } else {
            error_report("colo compare primary queue size too big,"
                         "drop packet");
        }
    } else {
        if (g_queue_get_length(&conn->secondary_list) <=
                               MAX_QUEUE_SIZE) {
            g_queue_push_tail(&conn->secondary_list, pkt);
            if (conn->ip_proto == IPPROTO_TCP) {
                g_queue_sort(&conn->secondary_list,
                             (GCompareDataFunc)seq_sorter,
                             NULL);
            }
        } else {
            error_report("colo compare secondary queue size too big,"
                         "drop packet");
        }
    }

    return 0;
}

/*
 * The IP packets sent by primary and secondary
 * will be compared in here
 * TODO support ip fragment, Out-Of-Order
 * return:    0  means packet same
 *            > 0 || < 0 means packet different
 */
static int colo_packet_compare_common(Packet *ppkt,
                                      Packet *spkt,
                                      int poffset,
                                      int soffset)
{
    if (trace_event_get_state_backends(TRACE_COLO_COMPARE_MISCOMPARE)) {
        char pri_ip_src[20], pri_ip_dst[20], sec_ip_src[20], sec_ip_dst[20];

        strcpy(pri_ip_src, inet_ntoa(ppkt->ip->ip_src));
        strcpy(pri_ip_dst, inet_ntoa(ppkt->ip->ip_dst));
        strcpy(sec_ip_src, inet_ntoa(spkt->ip->ip_src));
        strcpy(sec_ip_dst, inet_ntoa(spkt->ip->ip_dst));

        trace_colo_compare_ip_info(ppkt->size, pri_ip_src,
                                   pri_ip_dst, spkt->size,
                                   sec_ip_src, sec_ip_dst);
    }

    poffset = ppkt->vnet_hdr_len + poffset;
    soffset = ppkt->vnet_hdr_len + soffset;

    if (ppkt->size - poffset == spkt->size - soffset) {
        return memcmp(ppkt->data + poffset,
                      spkt->data + soffset,
                      spkt->size - soffset);
    } else {
        trace_colo_compare_main("Net packet size are not the same");
        return -1;
    }
}

/*
 * Called from the compare thread on the primary
 * for compare tcp packet
 * compare_tcp copied from Dr. David Alan Gilbert's branch
 */
static int colo_packet_compare_tcp(Packet *spkt, Packet *ppkt)
{
    struct tcphdr *ptcp, *stcp;
    int res;

    trace_colo_compare_main("compare tcp");

    ptcp = (struct tcphdr *)ppkt->transport_header;
    stcp = (struct tcphdr *)spkt->transport_header;

    /*
     * The 'identification' field in the IP header is *very* random
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

    /*
     * Check tcp header length for tcp option field.
     * th_off > 5 means this tcp packet have options field.
     * The tcp options maybe always different.
     * for example:
     * From RFC 7323.
     * TCP Timestamps option (TSopt):
     * Kind: 8
     *
     * Length: 10 bytes
     *
     *    +-------+-------+---------------------+---------------------+
     *    |Kind=8 |  10   |   TS Value (TSval)  |TS Echo Reply (TSecr)|
     *    +-------+-------+---------------------+---------------------+
     *       1       1              4                     4
     *
     * In this case the primary guest's timestamp always different with
     * the secondary guest's timestamp. COLO just focus on payload,
     * so we just need skip this field.
     */
    if (ptcp->th_off > 5) {
        ptrdiff_t ptcp_offset, stcp_offset;

        ptcp_offset = ppkt->transport_header - (uint8_t *)ppkt->data
                      + (ptcp->th_off * 4) - ppkt->vnet_hdr_len;
        stcp_offset = spkt->transport_header - (uint8_t *)spkt->data
                      + (stcp->th_off * 4) - spkt->vnet_hdr_len;

        /*
         * When network is busy, some tcp options(like sack) will unpredictable
         * occur in primary side or secondary side. it will make packet size
         * not same, but the two packet's payload is identical. colo just
         * care about packet payload, so we skip the option field.
         */
        res = colo_packet_compare_common(ppkt, spkt, ptcp_offset, stcp_offset);
    } else if (ptcp->th_sum == stcp->th_sum) {
        res = colo_packet_compare_common(ppkt, spkt, ETH_HLEN, ETH_HLEN);
    } else {
        res = -1;
    }

    if (res != 0 &&
        trace_event_get_state_backends(TRACE_COLO_COMPARE_MISCOMPARE)) {
        char pri_ip_src[20], pri_ip_dst[20], sec_ip_src[20], sec_ip_dst[20];

        strcpy(pri_ip_src, inet_ntoa(ppkt->ip->ip_src));
        strcpy(pri_ip_dst, inet_ntoa(ppkt->ip->ip_dst));
        strcpy(sec_ip_src, inet_ntoa(spkt->ip->ip_src));
        strcpy(sec_ip_dst, inet_ntoa(spkt->ip->ip_dst));

        trace_colo_compare_ip_info(ppkt->size, pri_ip_src,
                                   pri_ip_dst, spkt->size,
                                   sec_ip_src, sec_ip_dst);

        trace_colo_compare_tcp_info("pri tcp packet",
                                    ntohl(ptcp->th_seq),
                                    ntohl(ptcp->th_ack),
                                    res, ptcp->th_flags,
                                    ppkt->size);

        trace_colo_compare_tcp_info("sec tcp packet",
                                    ntohl(stcp->th_seq),
                                    ntohl(stcp->th_ack),
                                    res, stcp->th_flags,
                                    spkt->size);

        qemu_hexdump((char *)ppkt->data, stderr,
                     "colo-compare ppkt", ppkt->size);
        qemu_hexdump((char *)spkt->data, stderr,
                     "colo-compare spkt", spkt->size);
    }

    return res;
}

/*
 * Called from the compare thread on the primary
 * for compare udp packet
 */
static int colo_packet_compare_udp(Packet *spkt, Packet *ppkt)
{
    int ret;
    int network_header_length = ppkt->ip->ip_hl * 4;

    trace_colo_compare_main("compare udp");

    /*
     * Because of ppkt and spkt are both in the same connection,
     * The ppkt's src ip, dst ip, src port, dst port, ip_proto all are
     * same with spkt. In addition, IP header's Identification is a random
     * field, we can handle it in IP fragmentation function later.
     * COLO just concern the response net packet payload from primary guest
     * and secondary guest are same or not, So we ignored all IP header include
     * other field like TOS,TTL,IP Checksum. we only need to compare
     * the ip payload here.
     */
    ret = colo_packet_compare_common(ppkt, spkt,
                                     network_header_length + ETH_HLEN,
                                     network_header_length + ETH_HLEN);

    if (ret) {
        trace_colo_compare_udp_miscompare("primary pkt size", ppkt->size);
        trace_colo_compare_udp_miscompare("Secondary pkt size", spkt->size);
        if (trace_event_get_state_backends(TRACE_COLO_COMPARE_MISCOMPARE)) {
            qemu_hexdump((char *)ppkt->data, stderr, "colo-compare pri pkt",
                         ppkt->size);
            qemu_hexdump((char *)spkt->data, stderr, "colo-compare sec pkt",
                         spkt->size);
        }
    }

    return ret;
}

/*
 * Called from the compare thread on the primary
 * for compare icmp packet
 */
static int colo_packet_compare_icmp(Packet *spkt, Packet *ppkt)
{
    int network_header_length = ppkt->ip->ip_hl * 4;

    trace_colo_compare_main("compare icmp");

    /*
     * Because of ppkt and spkt are both in the same connection,
     * The ppkt's src ip, dst ip, src port, dst port, ip_proto all are
     * same with spkt. In addition, IP header's Identification is a random
     * field, we can handle it in IP fragmentation function later.
     * COLO just concern the response net packet payload from primary guest
     * and secondary guest are same or not, So we ignored all IP header include
     * other field like TOS,TTL,IP Checksum. we only need to compare
     * the ip payload here.
     */
    if (colo_packet_compare_common(ppkt, spkt,
                                   network_header_length + ETH_HLEN,
                                   network_header_length + ETH_HLEN)) {
        trace_colo_compare_icmp_miscompare("primary pkt size",
                                           ppkt->size);
        trace_colo_compare_icmp_miscompare("Secondary pkt size",
                                           spkt->size);
        if (trace_event_get_state_backends(TRACE_COLO_COMPARE_MISCOMPARE)) {
            qemu_hexdump((char *)ppkt->data, stderr, "colo-compare pri pkt",
                         ppkt->size);
            qemu_hexdump((char *)spkt->data, stderr, "colo-compare sec pkt",
                         spkt->size);
        }
        return -1;
    } else {
        return 0;
    }
}

/*
 * Called from the compare thread on the primary
 * for compare other packet
 */
static int colo_packet_compare_other(Packet *spkt, Packet *ppkt)
{
    trace_colo_compare_main("compare other");
    if (trace_event_get_state_backends(TRACE_COLO_COMPARE_MISCOMPARE)) {
        char pri_ip_src[20], pri_ip_dst[20], sec_ip_src[20], sec_ip_dst[20];

        strcpy(pri_ip_src, inet_ntoa(ppkt->ip->ip_src));
        strcpy(pri_ip_dst, inet_ntoa(ppkt->ip->ip_dst));
        strcpy(sec_ip_src, inet_ntoa(spkt->ip->ip_src));
        strcpy(sec_ip_dst, inet_ntoa(spkt->ip->ip_dst));

        trace_colo_compare_ip_info(ppkt->size, pri_ip_src,
                                   pri_ip_dst, spkt->size,
                                   sec_ip_src, sec_ip_dst);
    }

    return colo_packet_compare_common(ppkt, spkt, 0, 0);
}

static int colo_old_packet_check_one(Packet *pkt, int64_t *check_time)
{
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_HOST);

    if ((now - pkt->creation_ms) > (*check_time)) {
        trace_colo_old_packet_check_found(pkt->creation_ms);
        return 0;
    } else {
        return 1;
    }
}

static int colo_old_packet_check_one_conn(Connection *conn,
                                          void *user_data)
{
    GList *result = NULL;
    int64_t check_time = REGULAR_PACKET_CHECK_MS;

    result = g_queue_find_custom(&conn->primary_list,
                                 &check_time,
                                 (GCompareFunc)colo_old_packet_check_one);

    if (result) {
        /* Do checkpoint will flush old packet */
        /*
         * TODO: Notify colo frame to do checkpoint.
         * colo_compare_inconsistent_notify();
         */
        return 0;
    }

    return 1;
}

/*
 * Look for old packets that the secondary hasn't matched,
 * if we have some then we have to checkpoint to wake
 * the secondary up.
 */
static void colo_old_packet_check(void *opaque)
{
    CompareState *s = opaque;

    /*
     * If we find one old packet, stop finding job and notify
     * COLO frame do checkpoint.
     */
    g_queue_find_custom(&s->conn_list, NULL,
                        (GCompareFunc)colo_old_packet_check_one_conn);
}

/*
 * Called from the compare thread on the primary
 * for compare connection
 */
static void colo_compare_connection(void *opaque, void *user_data)
{
    CompareState *s = user_data;
    Connection *conn = opaque;
    Packet *pkt = NULL;
    GList *result = NULL;
    int ret;

    while (!g_queue_is_empty(&conn->primary_list) &&
           !g_queue_is_empty(&conn->secondary_list)) {
        pkt = g_queue_pop_head(&conn->primary_list);
        switch (conn->ip_proto) {
        case IPPROTO_TCP:
            result = g_queue_find_custom(&conn->secondary_list,
                     pkt, (GCompareFunc)colo_packet_compare_tcp);
            break;
        case IPPROTO_UDP:
            result = g_queue_find_custom(&conn->secondary_list,
                     pkt, (GCompareFunc)colo_packet_compare_udp);
            break;
        case IPPROTO_ICMP:
            result = g_queue_find_custom(&conn->secondary_list,
                     pkt, (GCompareFunc)colo_packet_compare_icmp);
            break;
        default:
            result = g_queue_find_custom(&conn->secondary_list,
                     pkt, (GCompareFunc)colo_packet_compare_other);
            break;
        }

        if (result) {
            ret = compare_chr_send(s,
                                   pkt->data,
                                   pkt->size,
                                   pkt->vnet_hdr_len);
            if (ret < 0) {
                error_report("colo_send_primary_packet failed");
            }
            trace_colo_compare_main("packet same and release packet");
            g_queue_remove(&conn->secondary_list, result->data);
            packet_destroy(pkt, NULL);
        } else {
            /*
             * If one packet arrive late, the secondary_list or
             * primary_list will be empty, so we can't compare it
             * until next comparison.
             */
            trace_colo_compare_main("packet different");
            g_queue_push_head(&conn->primary_list, pkt);
            /* TODO: colo_notify_checkpoint();*/
            break;
        }
    }
}

static int compare_chr_send(CompareState *s,
                            const uint8_t *buf,
                            uint32_t size,
                            uint32_t vnet_hdr_len)
{
    int ret = 0;
    uint32_t len = htonl(size);

    if (!size) {
        return 0;
    }

    ret = qemu_chr_fe_write_all(&s->chr_out, (uint8_t *)&len, sizeof(len));
    if (ret != sizeof(len)) {
        goto err;
    }

    if (s->vnet_hdr) {
        /*
         * We send vnet header len make other module(like filter-redirector)
         * know how to parse net packet correctly.
         */
        len = htonl(vnet_hdr_len);
        ret = qemu_chr_fe_write_all(&s->chr_out, (uint8_t *)&len, sizeof(len));
        if (ret != sizeof(len)) {
            goto err;
        }
    }

    ret = qemu_chr_fe_write_all(&s->chr_out, (uint8_t *)buf, size);
    if (ret != size) {
        goto err;
    }

    return 0;

err:
    return ret < 0 ? ret : -EIO;
}

static int compare_chr_can_read(void *opaque)
{
    return COMPARE_READ_LEN_MAX;
}

/*
 * Called from the main thread on the primary for packets
 * arriving over the socket from the primary.
 */
static void compare_pri_chr_in(void *opaque, const uint8_t *buf, int size)
{
    CompareState *s = COLO_COMPARE(opaque);
    int ret;

    ret = net_fill_rstate(&s->pri_rs, buf, size);
    if (ret == -1) {
        qemu_chr_fe_set_handlers(&s->chr_pri_in, NULL, NULL, NULL, NULL,
                                 NULL, NULL, true);
        error_report("colo-compare primary_in error");
    }
}

/*
 * Called from the main thread on the primary for packets
 * arriving over the socket from the secondary.
 */
static void compare_sec_chr_in(void *opaque, const uint8_t *buf, int size)
{
    CompareState *s = COLO_COMPARE(opaque);
    int ret;

    ret = net_fill_rstate(&s->sec_rs, buf, size);
    if (ret == -1) {
        qemu_chr_fe_set_handlers(&s->chr_sec_in, NULL, NULL, NULL, NULL,
                                 NULL, NULL, true);
        error_report("colo-compare secondary_in error");
    }
}

/*
 * Check old packet regularly so it can watch for any packets
 * that the secondary hasn't produced equivalents of.
 */
static void check_old_packet_regular(void *opaque)
{
    CompareState *s = opaque;

    /* if have old packet we will notify checkpoint */
    colo_old_packet_check(s);
    timer_mod(s->packet_check_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                REGULAR_PACKET_CHECK_MS);
}

static void colo_compare_timer_init(CompareState *s)
{
    AioContext *ctx = iothread_get_aio_context(s->iothread);

    s->packet_check_timer = aio_timer_new(ctx, QEMU_CLOCK_VIRTUAL,
                                SCALE_MS, check_old_packet_regular,
                                s);
    timer_mod(s->packet_check_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                    REGULAR_PACKET_CHECK_MS);
}

static void colo_compare_timer_del(CompareState *s)
{
    if (s->packet_check_timer) {
        timer_del(s->packet_check_timer);
        timer_free(s->packet_check_timer);
        s->packet_check_timer = NULL;
    }
 }

static void colo_compare_iothread(CompareState *s)
{
    object_ref(OBJECT(s->iothread));
    s->worker_context = iothread_get_g_main_context(s->iothread);

    qemu_chr_fe_set_handlers(&s->chr_pri_in, compare_chr_can_read,
                             compare_pri_chr_in, NULL, NULL,
                             s, s->worker_context, true);
    qemu_chr_fe_set_handlers(&s->chr_sec_in, compare_chr_can_read,
                             compare_sec_chr_in, NULL, NULL,
                             s, s->worker_context, true);

    colo_compare_timer_init(s);
}

static char *compare_get_pri_indev(Object *obj, Error **errp)
{
    CompareState *s = COLO_COMPARE(obj);

    return g_strdup(s->pri_indev);
}

static void compare_set_pri_indev(Object *obj, const char *value, Error **errp)
{
    CompareState *s = COLO_COMPARE(obj);

    g_free(s->pri_indev);
    s->pri_indev = g_strdup(value);
}

static char *compare_get_sec_indev(Object *obj, Error **errp)
{
    CompareState *s = COLO_COMPARE(obj);

    return g_strdup(s->sec_indev);
}

static void compare_set_sec_indev(Object *obj, const char *value, Error **errp)
{
    CompareState *s = COLO_COMPARE(obj);

    g_free(s->sec_indev);
    s->sec_indev = g_strdup(value);
}

static char *compare_get_outdev(Object *obj, Error **errp)
{
    CompareState *s = COLO_COMPARE(obj);

    return g_strdup(s->outdev);
}

static void compare_set_outdev(Object *obj, const char *value, Error **errp)
{
    CompareState *s = COLO_COMPARE(obj);

    g_free(s->outdev);
    s->outdev = g_strdup(value);
}

static bool compare_get_vnet_hdr(Object *obj, Error **errp)
{
    CompareState *s = COLO_COMPARE(obj);

    return s->vnet_hdr;
}

static void compare_set_vnet_hdr(Object *obj,
                                 bool value,
                                 Error **errp)
{
    CompareState *s = COLO_COMPARE(obj);

    s->vnet_hdr = value;
}

static void compare_pri_rs_finalize(SocketReadState *pri_rs)
{
    CompareState *s = container_of(pri_rs, CompareState, pri_rs);

    if (packet_enqueue(s, PRIMARY_IN)) {
        trace_colo_compare_main("primary: unsupported packet in");
        compare_chr_send(s,
                         pri_rs->buf,
                         pri_rs->packet_len,
                         pri_rs->vnet_hdr_len);
    } else {
        /* compare connection */
        g_queue_foreach(&s->conn_list, colo_compare_connection, s);
    }
}

static void compare_sec_rs_finalize(SocketReadState *sec_rs)
{
    CompareState *s = container_of(sec_rs, CompareState, sec_rs);

    if (packet_enqueue(s, SECONDARY_IN)) {
        trace_colo_compare_main("secondary: unsupported packet in");
    } else {
        /* compare connection */
        g_queue_foreach(&s->conn_list, colo_compare_connection, s);
    }
}


/*
 * Return 0 is success.
 * Return 1 is failed.
 */
static int find_and_check_chardev(Chardev **chr,
                                  char *chr_name,
                                  Error **errp)
{
    *chr = qemu_chr_find(chr_name);
    if (*chr == NULL) {
        error_setg(errp, "Device '%s' not found",
                   chr_name);
        return 1;
    }

    if (!qemu_chr_has_feature(*chr, QEMU_CHAR_FEATURE_RECONNECTABLE)) {
        error_setg(errp, "chardev \"%s\" is not reconnectable",
                   chr_name);
        return 1;
    }

    return 0;
}

/*
 * Called from the main thread on the primary
 * to setup colo-compare.
 */
static void colo_compare_complete(UserCreatable *uc, Error **errp)
{
    CompareState *s = COLO_COMPARE(uc);
    Chardev *chr;

    if (!s->pri_indev || !s->sec_indev || !s->outdev || !s->iothread) {
        error_setg(errp, "colo compare needs 'primary_in' ,"
                   "'secondary_in','outdev','iothread' property set");
        return;
    } else if (!strcmp(s->pri_indev, s->outdev) ||
               !strcmp(s->sec_indev, s->outdev) ||
               !strcmp(s->pri_indev, s->sec_indev)) {
        error_setg(errp, "'indev' and 'outdev' could not be same "
                   "for compare module");
        return;
    }

    if (find_and_check_chardev(&chr, s->pri_indev, errp) ||
        !qemu_chr_fe_init(&s->chr_pri_in, chr, errp)) {
        return;
    }

    if (find_and_check_chardev(&chr, s->sec_indev, errp) ||
        !qemu_chr_fe_init(&s->chr_sec_in, chr, errp)) {
        return;
    }

    if (find_and_check_chardev(&chr, s->outdev, errp) ||
        !qemu_chr_fe_init(&s->chr_out, chr, errp)) {
        return;
    }

    net_socket_rs_init(&s->pri_rs, compare_pri_rs_finalize, s->vnet_hdr);
    net_socket_rs_init(&s->sec_rs, compare_sec_rs_finalize, s->vnet_hdr);

    g_queue_init(&s->conn_list);

    s->connection_track_table = g_hash_table_new_full(connection_key_hash,
                                                      connection_key_equal,
                                                      g_free,
                                                      connection_destroy);

    colo_compare_iothread(s);
    return;
}

static void colo_flush_packets(void *opaque, void *user_data)
{
    CompareState *s = user_data;
    Connection *conn = opaque;
    Packet *pkt = NULL;

    while (!g_queue_is_empty(&conn->primary_list)) {
        pkt = g_queue_pop_head(&conn->primary_list);
        compare_chr_send(s,
                         pkt->data,
                         pkt->size,
                         pkt->vnet_hdr_len);
        packet_destroy(pkt, NULL);
    }
    while (!g_queue_is_empty(&conn->secondary_list)) {
        pkt = g_queue_pop_head(&conn->secondary_list);
        packet_destroy(pkt, NULL);
    }
}

static void colo_compare_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = colo_compare_complete;
}

static void colo_compare_init(Object *obj)
{
    CompareState *s = COLO_COMPARE(obj);

    object_property_add_str(obj, "primary_in",
                            compare_get_pri_indev, compare_set_pri_indev,
                            NULL);
    object_property_add_str(obj, "secondary_in",
                            compare_get_sec_indev, compare_set_sec_indev,
                            NULL);
    object_property_add_str(obj, "outdev",
                            compare_get_outdev, compare_set_outdev,
                            NULL);
    object_property_add_link(obj, "iothread", TYPE_IOTHREAD,
                            (Object **)&s->iothread,
                            object_property_allow_set_link,
                            OBJ_PROP_LINK_UNREF_ON_RELEASE, NULL);

    s->vnet_hdr = false;
    object_property_add_bool(obj, "vnet_hdr_support", compare_get_vnet_hdr,
                             compare_set_vnet_hdr, NULL);
}

static void colo_compare_finalize(Object *obj)
{
    CompareState *s = COLO_COMPARE(obj);

    qemu_chr_fe_deinit(&s->chr_pri_in, false);
    qemu_chr_fe_deinit(&s->chr_sec_in, false);
    qemu_chr_fe_deinit(&s->chr_out, false);
    if (s->iothread) {
        colo_compare_timer_del(s);
    }
    /* Release all unhandled packets after compare thead exited */
    g_queue_foreach(&s->conn_list, colo_flush_packets, s);

    g_queue_clear(&s->conn_list);

    if (s->connection_track_table) {
        g_hash_table_destroy(s->connection_track_table);
    }

    if (s->iothread) {
        object_unref(OBJECT(s->iothread));
    }
    g_free(s->pri_indev);
    g_free(s->sec_indev);
    g_free(s->outdev);
}

static const TypeInfo colo_compare_info = {
    .name = TYPE_COLO_COMPARE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(CompareState),
    .instance_init = colo_compare_init,
    .instance_finalize = colo_compare_finalize,
    .class_size = sizeof(CompareClass),
    .class_init = colo_compare_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&colo_compare_info);
}

type_init(register_types);
