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
#include "sysemu/char.h"
#include "qemu/sockets.h"
#include "qapi-visit.h"
#include "net/colo.h"

#define TYPE_COLO_COMPARE "colo-compare"
#define COLO_COMPARE(obj) \
    OBJECT_CHECK(CompareState, (obj), TYPE_COLO_COMPARE)

#define COMPARE_READ_LEN_MAX NET_BUFSIZE
#define MAX_QUEUE_SIZE 1024

/* TODO: Should be configurable */
#define REGULAR_PACKET_CHECK_MS 3000

/*
  + CompareState ++
  |               |
  +---------------+   +---------------+         +---------------+
  |conn list      +--->conn           +--------->conn           |
  +---------------+   +---------------+         +---------------+
  |               |     |           |             |          |
  +---------------+ +---v----+  +---v----+    +---v----+ +---v----+
                    |primary |  |secondary    |primary | |secondary
                    |packet  |  |packet  +    |packet  | |packet  +
                    +--------+  +--------+    +--------+ +--------+
                        |           |             |          |
                    +---v----+  +---v----+    +---v----+ +---v----+
                    |primary |  |secondary    |primary | |secondary
                    |packet  |  |packet  +    |packet  | |packet  +
                    +--------+  +--------+    +--------+ +--------+
                        |           |             |          |
                    +---v----+  +---v----+    +---v----+ +---v----+
                    |primary |  |secondary    |primary | |secondary
                    |packet  |  |packet  +    |packet  | |packet  +
                    +--------+  +--------+    +--------+ +--------+
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

    /* connection list: the connections belonged to this NIC could be found
     * in this list.
     * element type: Connection
     */
    GQueue conn_list;
    /* hashtable to save connection */
    GHashTable *connection_track_table;
    /* compare thread, a thread for each NIC */
    QemuThread thread;
    /* Timer used on the primary to find packets that are never matched */
    QEMUTimer *timer;
    QemuMutex timer_check_lock;
} CompareState;

typedef struct CompareClass {
    ObjectClass parent_class;
} CompareClass;

enum {
    PRIMARY_IN = 0,
    SECONDARY_IN,
};

static int compare_chr_send(CharBackend *out,
                            const uint8_t *buf,
                            uint32_t size);

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
        pkt = packet_new(s->pri_rs.buf, s->pri_rs.packet_len);
    } else {
        pkt = packet_new(s->sec_rs.buf, s->sec_rs.packet_len);
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
        } else {
            error_report("colo compare primary queue size too big,"
                         "drop packet");
        }
    } else {
        if (g_queue_get_length(&conn->secondary_list) <=
                               MAX_QUEUE_SIZE) {
            g_queue_push_tail(&conn->secondary_list, pkt);
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
static int colo_packet_compare(Packet *ppkt, Packet *spkt)
{
    trace_colo_compare_ip_info(ppkt->size, inet_ntoa(ppkt->ip->ip_src),
                               inet_ntoa(ppkt->ip->ip_dst), spkt->size,
                               inet_ntoa(spkt->ip->ip_src),
                               inet_ntoa(spkt->ip->ip_dst));

    if (ppkt->size == spkt->size) {
        return memcmp(ppkt->data, spkt->data, spkt->size);
    } else {
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
    if (ppkt->size != spkt->size) {
        if (trace_event_get_state(TRACE_COLO_COMPARE_MISCOMPARE)) {
            trace_colo_compare_main("pkt size not same");
        }
        return -1;
    }

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

    res = memcmp(ppkt->data + ETH_HLEN, spkt->data + ETH_HLEN,
                (spkt->size - ETH_HLEN));

    if (res != 0 && trace_event_get_state(TRACE_COLO_COMPARE_MISCOMPARE)) {
        trace_colo_compare_pkt_info_src(inet_ntoa(ppkt->ip->ip_src),
                                        ntohl(stcp->th_seq),
                                        ntohl(stcp->th_ack),
                                        res, stcp->th_flags,
                                        spkt->size);

        trace_colo_compare_pkt_info_dst(inet_ntoa(ppkt->ip->ip_dst),
                                        ntohl(ptcp->th_seq),
                                        ntohl(ptcp->th_ack),
                                        res, ptcp->th_flags,
                                        ppkt->size);

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

    trace_colo_compare_main("compare udp");
    ret = colo_packet_compare(ppkt, spkt);

    if (ret) {
        trace_colo_compare_udp_miscompare("primary pkt size", ppkt->size);
        qemu_hexdump((char *)ppkt->data, stderr, "colo-compare", ppkt->size);
        trace_colo_compare_udp_miscompare("Secondary pkt size", spkt->size);
        qemu_hexdump((char *)spkt->data, stderr, "colo-compare", spkt->size);
    }

    return ret;
}

/*
 * Called from the compare thread on the primary
 * for compare icmp packet
 */
static int colo_packet_compare_icmp(Packet *spkt, Packet *ppkt)
{
    int network_length;

    trace_colo_compare_main("compare icmp");
    network_length = ppkt->ip->ip_hl * 4;
    if (ppkt->size != spkt->size ||
        ppkt->size < network_length + ETH_HLEN) {
        return -1;
    }

    if (colo_packet_compare(ppkt, spkt)) {
        trace_colo_compare_icmp_miscompare("primary pkt size",
                                           ppkt->size);
        qemu_hexdump((char *)ppkt->data, stderr, "colo-compare",
                     ppkt->size);
        trace_colo_compare_icmp_miscompare("Secondary pkt size",
                                           spkt->size);
        qemu_hexdump((char *)spkt->data, stderr, "colo-compare",
                     spkt->size);
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
    trace_colo_compare_ip_info(ppkt->size, inet_ntoa(ppkt->ip->ip_src),
                               inet_ntoa(ppkt->ip->ip_dst), spkt->size,
                               inet_ntoa(spkt->ip->ip_src),
                               inet_ntoa(spkt->ip->ip_dst));
    return colo_packet_compare(ppkt, spkt);
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

static void colo_old_packet_check_one_conn(void *opaque,
                                           void *user_data)
{
    Connection *conn = opaque;
    GList *result = NULL;
    int64_t check_time = REGULAR_PACKET_CHECK_MS;

    result = g_queue_find_custom(&conn->primary_list,
                                 &check_time,
                                 (GCompareFunc)colo_old_packet_check_one);

    if (result) {
        /* do checkpoint will flush old packet */
        /* TODO: colo_notify_checkpoint();*/
    }
}

/*
 * Look for old packets that the secondary hasn't matched,
 * if we have some then we have to checkpoint to wake
 * the secondary up.
 */
static void colo_old_packet_check(void *opaque)
{
    CompareState *s = opaque;

    g_queue_foreach(&s->conn_list, colo_old_packet_check_one_conn, NULL);
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
        qemu_mutex_lock(&s->timer_check_lock);
        pkt = g_queue_pop_tail(&conn->primary_list);
        qemu_mutex_unlock(&s->timer_check_lock);
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
            ret = compare_chr_send(&s->chr_out, pkt->data, pkt->size);
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
            qemu_mutex_lock(&s->timer_check_lock);
            g_queue_push_tail(&conn->primary_list, pkt);
            qemu_mutex_unlock(&s->timer_check_lock);
            /* TODO: colo_notify_checkpoint();*/
            break;
        }
    }
}

static int compare_chr_send(CharBackend *out,
                            const uint8_t *buf,
                            uint32_t size)
{
    int ret = 0;
    uint32_t len = htonl(size);

    if (!size) {
        return 0;
    }

    ret = qemu_chr_fe_write_all(out, (uint8_t *)&len, sizeof(len));
    if (ret != sizeof(len)) {
        goto err;
    }

    ret = qemu_chr_fe_write_all(out, (uint8_t *)buf, size);
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
        qemu_chr_fe_set_handlers(&s->chr_pri_in, NULL, NULL, NULL,
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
        qemu_chr_fe_set_handlers(&s->chr_sec_in, NULL, NULL, NULL,
                                 NULL, NULL, true);
        error_report("colo-compare secondary_in error");
    }
}

static void *colo_compare_thread(void *opaque)
{
    GMainContext *worker_context;
    GMainLoop *compare_loop;
    CompareState *s = opaque;

    worker_context = g_main_context_new();

    qemu_chr_fe_set_handlers(&s->chr_pri_in, compare_chr_can_read,
                             compare_pri_chr_in, NULL, s, worker_context, true);
    qemu_chr_fe_set_handlers(&s->chr_sec_in, compare_chr_can_read,
                             compare_sec_chr_in, NULL, s, worker_context, true);

    compare_loop = g_main_loop_new(worker_context, FALSE);

    g_main_loop_run(compare_loop);

    g_main_loop_unref(compare_loop);
    g_main_context_unref(worker_context);
    return NULL;
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

static void compare_pri_rs_finalize(SocketReadState *pri_rs)
{
    CompareState *s = container_of(pri_rs, CompareState, pri_rs);

    if (packet_enqueue(s, PRIMARY_IN)) {
        trace_colo_compare_main("primary: unsupported packet in");
        compare_chr_send(&s->chr_out, pri_rs->buf, pri_rs->packet_len);
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
static int find_and_check_chardev(CharDriverState **chr,
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
 * Check old packet regularly so it can watch for any packets
 * that the secondary hasn't produced equivalents of.
 */
static void check_old_packet_regular(void *opaque)
{
    CompareState *s = opaque;

    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              REGULAR_PACKET_CHECK_MS);
    /* if have old packet we will notify checkpoint */
    /*
     * TODO: Make timer handler run in compare thread
     * like qemu_chr_add_handlers_full.
     */
    qemu_mutex_lock(&s->timer_check_lock);
    colo_old_packet_check(s);
    qemu_mutex_unlock(&s->timer_check_lock);
}

/*
 * Called from the main thread on the primary
 * to setup colo-compare.
 */
static void colo_compare_complete(UserCreatable *uc, Error **errp)
{
    CompareState *s = COLO_COMPARE(uc);
    CharDriverState *chr;
    char thread_name[64];
    static int compare_id;

    if (!s->pri_indev || !s->sec_indev || !s->outdev) {
        error_setg(errp, "colo compare needs 'primary_in' ,"
                   "'secondary_in','outdev' property set");
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

    net_socket_rs_init(&s->pri_rs, compare_pri_rs_finalize);
    net_socket_rs_init(&s->sec_rs, compare_sec_rs_finalize);

    g_queue_init(&s->conn_list);
    qemu_mutex_init(&s->timer_check_lock);

    s->connection_track_table = g_hash_table_new_full(connection_key_hash,
                                                      connection_key_equal,
                                                      g_free,
                                                      connection_destroy);

    sprintf(thread_name, "colo-compare %d", compare_id);
    qemu_thread_create(&s->thread, thread_name,
                       colo_compare_thread, s,
                       QEMU_THREAD_JOINABLE);
    compare_id++;

    /* A regular timer to kick any packets that the secondary doesn't match */
    s->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, /* Only when guest runs */
                            check_old_packet_regular, s);
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                        REGULAR_PACKET_CHECK_MS);

    return;
}

static void colo_compare_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = colo_compare_complete;
}

static void colo_compare_init(Object *obj)
{
    object_property_add_str(obj, "primary_in",
                            compare_get_pri_indev, compare_set_pri_indev,
                            NULL);
    object_property_add_str(obj, "secondary_in",
                            compare_get_sec_indev, compare_set_sec_indev,
                            NULL);
    object_property_add_str(obj, "outdev",
                            compare_get_outdev, compare_set_outdev,
                            NULL);
}

static void colo_compare_finalize(Object *obj)
{
    CompareState *s = COLO_COMPARE(obj);

    qemu_chr_fe_deinit(&s->chr_pri_in);
    qemu_chr_fe_deinit(&s->chr_sec_in);
    qemu_chr_fe_deinit(&s->chr_out);

    g_queue_free(&s->conn_list);

    if (qemu_thread_is_self(&s->thread)) {
        /* compare connection */
        g_queue_foreach(&s->conn_list, colo_compare_connection, s);
        qemu_thread_join(&s->thread);
    }

    if (s->timer) {
        timer_del(s->timer);
    }

    qemu_mutex_destroy(&s->timer_check_lock);

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
