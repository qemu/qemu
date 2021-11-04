/*
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
#include "trace.h"
#include "colo.h"
#include "net/filter.h"
#include "net/net.h"
#include "qemu/error-report.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/iov.h"
#include "net/checksum.h"
#include "net/colo.h"
#include "migration/colo.h"
#include "util.h"

#define TYPE_FILTER_REWRITER "filter-rewriter"
OBJECT_DECLARE_SIMPLE_TYPE(RewriterState, FILTER_REWRITER)

#define FAILOVER_MODE_ON  true
#define FAILOVER_MODE_OFF false

struct RewriterState {
    NetFilterState parent_obj;
    NetQueue *incoming_queue;
    /* hashtable to save connection */
    GHashTable *connection_track_table;
    bool vnet_hdr;
    bool failover_mode;
};

static void filter_rewriter_failover_mode(RewriterState *s)
{
    s->failover_mode = FAILOVER_MODE_ON;
}

static void filter_rewriter_flush(NetFilterState *nf)
{
    RewriterState *s = FILTER_REWRITER(nf);

    if (!qemu_net_queue_flush(s->incoming_queue)) {
        /* Unable to empty the queue, purge remaining packets */
        qemu_net_queue_purge(s->incoming_queue, nf->netdev);
    }
}

/*
 * Return 1 on success, if return 0 means the pkt
 * is not TCP packet
 */
static int is_tcp_packet(Packet *pkt)
{
    if (!parse_packet_early(pkt) &&
        pkt->ip->ip_p == IPPROTO_TCP) {
        return 1;
    } else {
        return 0;
    }
}

/* handle tcp packet from primary guest */
static int handle_primary_tcp_pkt(RewriterState *rf,
                                  Connection *conn,
                                  Packet *pkt, ConnectionKey *key)
{
    struct tcp_hdr *tcp_pkt;

    tcp_pkt = (struct tcp_hdr *)pkt->transport_header;
    if (trace_event_get_state_backends(TRACE_COLO_FILTER_REWRITER_PKT_INFO)) {
        trace_colo_filter_rewriter_pkt_info(__func__,
                    inet_ntoa(pkt->ip->ip_src), inet_ntoa(pkt->ip->ip_dst),
                    ntohl(tcp_pkt->th_seq), ntohl(tcp_pkt->th_ack),
                    tcp_pkt->th_flags);
    }
    if (trace_event_get_state_backends(
          TRACE_COLO_FILTER_REWRITER_CONN_OFFSET)) {
        trace_colo_filter_rewriter_conn_offset(conn->offset);
    }

    if (((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == (TH_ACK | TH_SYN)) &&
        conn->tcp_state == TCPS_SYN_SENT) {
        conn->tcp_state = TCPS_ESTABLISHED;
    }

    if (((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == TH_SYN)) {
        /*
         * we use this flag update offset func
         * run once in independent tcp connection
         */
        conn->tcp_state = TCPS_SYN_RECEIVED;
    }

    if (((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == TH_ACK)) {
        if (conn->tcp_state == TCPS_SYN_RECEIVED) {
            /*
             * offset = secondary_seq - primary seq
             * ack packet sent by guest from primary node,
             * so we use th_ack - 1 get primary_seq
             */
            conn->offset -= (ntohl(tcp_pkt->th_ack) - 1);
            conn->tcp_state = TCPS_ESTABLISHED;
        }
        if (conn->offset) {
            /* handle packets to the secondary from the primary */
            tcp_pkt->th_ack = htonl(ntohl(tcp_pkt->th_ack) + conn->offset);

            net_checksum_calculate((uint8_t *)pkt->data + pkt->vnet_hdr_len,
                                   pkt->size - pkt->vnet_hdr_len, CSUM_TCP);
        }

        /*
         * Passive close step 3
         */
        if ((conn->tcp_state == TCPS_LAST_ACK) &&
            (ntohl(tcp_pkt->th_ack) == (conn->fin_ack_seq + 1))) {
            conn->tcp_state = TCPS_CLOSED;
            g_hash_table_remove(rf->connection_track_table, key);
        }
    }

    if ((tcp_pkt->th_flags & TH_FIN) == TH_FIN) {
        /*
         * Passive close.
         * Step 1:
         * The *server* side of this connect is VM, *client* tries to close
         * the connection. We will into CLOSE_WAIT status.
         *
         * Step 2:
         * In this step we will into LAST_ACK status.
         *
         * We got 'fin=1, ack=1' packet from server side, we need to
         * record the seq of 'fin=1, ack=1' packet.
         *
         * Step 3:
         * We got 'ack=1' packets from client side, it acks 'fin=1, ack=1'
         * packet from server side. From this point, we can ensure that there
         * will be no packets in the connection, except that, some errors
         * happen between the path of 'filter object' and vNIC, if this rare
         * case really happen, we can still create a new connection,
         * So it is safe to remove the connection from connection_track_table.
         *
         */
        if (conn->tcp_state == TCPS_ESTABLISHED) {
            conn->tcp_state = TCPS_CLOSE_WAIT;
        }

        /*
         * Active close step 2.
         */
        if (conn->tcp_state == TCPS_FIN_WAIT_1) {
            /*
             * For simplify implementation, we needn't wait 2MSL time
             * in filter rewriter. Because guest kernel will track the
             * TCP status and wait 2MSL time, if client resend the FIN
             * packet, guest will apply the last ACK too.
             * So, we skip the TCPS_TIME_WAIT state here and go straight
             * to TCPS_CLOSED state.
             */
            conn->tcp_state = TCPS_CLOSED;
            g_hash_table_remove(rf->connection_track_table, key);
        }
    }

    return 0;
}

/* handle tcp packet from secondary guest */
static int handle_secondary_tcp_pkt(RewriterState *rf,
                                    Connection *conn,
                                    Packet *pkt, ConnectionKey *key)
{
    struct tcp_hdr *tcp_pkt;

    tcp_pkt = (struct tcp_hdr *)pkt->transport_header;

    if (trace_event_get_state_backends(TRACE_COLO_FILTER_REWRITER_PKT_INFO)) {
        trace_colo_filter_rewriter_pkt_info(__func__,
                    inet_ntoa(pkt->ip->ip_src), inet_ntoa(pkt->ip->ip_dst),
                    ntohl(tcp_pkt->th_seq), ntohl(tcp_pkt->th_ack),
                    tcp_pkt->th_flags);
    }
    if (trace_event_get_state_backends(
          TRACE_COLO_FILTER_REWRITER_CONN_OFFSET)) {
        trace_colo_filter_rewriter_conn_offset(conn->offset);
    }

    if (conn->tcp_state == TCPS_SYN_RECEIVED &&
        ((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == (TH_ACK | TH_SYN))) {
        /*
         * save offset = secondary_seq and then
         * in handle_primary_tcp_pkt make offset
         * = secondary_seq - primary_seq
         */
        conn->offset = ntohl(tcp_pkt->th_seq);
    }

    /* VM active connect */
    if (conn->tcp_state == TCPS_CLOSED &&
        ((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == TH_SYN)) {
        conn->tcp_state = TCPS_SYN_SENT;
    }

    if ((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == TH_ACK) {
        /* Only need to adjust seq while offset is Non-zero */
        if (conn->offset) {
            /* handle packets to the primary from the secondary*/
            tcp_pkt->th_seq = htonl(ntohl(tcp_pkt->th_seq) - conn->offset);

            net_checksum_calculate((uint8_t *)pkt->data + pkt->vnet_hdr_len,
                                   pkt->size - pkt->vnet_hdr_len, CSUM_TCP);
        }
    }

    /*
     * Passive close step 2:
     */
    if (conn->tcp_state == TCPS_CLOSE_WAIT &&
        (tcp_pkt->th_flags & (TH_ACK | TH_FIN)) == (TH_ACK | TH_FIN)) {
        conn->fin_ack_seq = ntohl(tcp_pkt->th_seq);
        conn->tcp_state = TCPS_LAST_ACK;
    }

    /*
     * Active close
     *
     * Step 1:
     * The *server* side of this connect is VM, *server* tries to close
     * the connection.
     *
     * Step 2:
     * We will into CLOSE_WAIT status.
     * We simplify the TCPS_FIN_WAIT_2, TCPS_TIME_WAIT and
     * CLOSING status.
     */
    if (conn->tcp_state == TCPS_ESTABLISHED &&
        (tcp_pkt->th_flags & (TH_ACK | TH_FIN)) == TH_FIN) {
        conn->tcp_state = TCPS_FIN_WAIT_1;
    }

    return 0;
}

static ssize_t colo_rewriter_receive_iov(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    RewriterState *s = FILTER_REWRITER(nf);
    Connection *conn;
    ConnectionKey key;
    Packet *pkt;
    ssize_t size = iov_size(iov, iovcnt);
    ssize_t vnet_hdr_len = 0;
    char *buf = g_malloc0(size);

    iov_to_buf(iov, iovcnt, 0, buf, size);

    if (s->vnet_hdr) {
        vnet_hdr_len = nf->netdev->vnet_hdr_len;
    }

    pkt = packet_new_nocopy(buf, size, vnet_hdr_len);

    /*
     * if we get tcp packet
     * we will rewrite it to make secondary guest's
     * connection established successfully
     */
    if (pkt && is_tcp_packet(pkt)) {

        fill_connection_key(pkt, &key, sender == nf->netdev);

        /* After failover we needn't change new TCP packet */
        if (s->failover_mode &&
            !connection_has_tracked(s->connection_track_table, &key)) {
            goto out;
        }

        conn = connection_get(s->connection_track_table,
                              &key,
                              NULL);

        if (sender == nf->netdev) {
            /* NET_FILTER_DIRECTION_TX */
            if (!handle_primary_tcp_pkt(s, conn, pkt, &key)) {
                qemu_net_queue_send(s->incoming_queue, sender, 0,
                (const uint8_t *)pkt->data, pkt->size, NULL);
                packet_destroy(pkt, NULL);
                pkt = NULL;
                /*
                 * We block the packet here,after rewrite pkt
                 * and will send it
                 */
                return 1;
            }
        } else {
            /* NET_FILTER_DIRECTION_RX */
            if (!handle_secondary_tcp_pkt(s, conn, pkt, &key)) {
                qemu_net_queue_send(s->incoming_queue, sender, 0,
                (const uint8_t *)pkt->data, pkt->size, NULL);
                packet_destroy(pkt, NULL);
                pkt = NULL;
                /*
                 * We block the packet here,after rewrite pkt
                 * and will send it
                 */
                return 1;
            }
        }
    }

out:
    packet_destroy(pkt, NULL);
    pkt = NULL;
    return 0;
}

static void reset_seq_offset(gpointer key, gpointer value, gpointer user_data)
{
    Connection *conn = (Connection *)value;

    conn->offset = 0;
}

static gboolean offset_is_nonzero(gpointer key,
                                  gpointer value,
                                  gpointer user_data)
{
    Connection *conn = (Connection *)value;

    return conn->offset ? true : false;
}

static void colo_rewriter_handle_event(NetFilterState *nf, int event,
                                       Error **errp)
{
    RewriterState *rs = FILTER_REWRITER(nf);

    switch (event) {
    case COLO_EVENT_CHECKPOINT:
        g_hash_table_foreach(rs->connection_track_table,
                            reset_seq_offset, NULL);
        break;
    case COLO_EVENT_FAILOVER:
        if (!g_hash_table_find(rs->connection_track_table,
                              offset_is_nonzero, NULL)) {
            filter_rewriter_failover_mode(rs);
        }
        break;
    default:
        break;
    }
}

static void colo_rewriter_cleanup(NetFilterState *nf)
{
    RewriterState *s = FILTER_REWRITER(nf);

    /* flush packets */
    if (s->incoming_queue) {
        filter_rewriter_flush(nf);
        g_free(s->incoming_queue);
    }

    g_hash_table_destroy(s->connection_track_table);
}

static void colo_rewriter_setup(NetFilterState *nf, Error **errp)
{
    RewriterState *s = FILTER_REWRITER(nf);

    s->connection_track_table = g_hash_table_new_full(connection_key_hash,
                                                      connection_key_equal,
                                                      g_free,
                                                      connection_destroy);
    s->incoming_queue = qemu_new_net_queue(qemu_netfilter_pass_to_next, nf);
}

static bool filter_rewriter_get_vnet_hdr(Object *obj, Error **errp)
{
    RewriterState *s = FILTER_REWRITER(obj);

    return s->vnet_hdr;
}

static void filter_rewriter_set_vnet_hdr(Object *obj,
                                         bool value,
                                         Error **errp)
{
    RewriterState *s = FILTER_REWRITER(obj);

    s->vnet_hdr = value;
}

static void filter_rewriter_init(Object *obj)
{
    RewriterState *s = FILTER_REWRITER(obj);

    s->vnet_hdr = false;
    s->failover_mode = FAILOVER_MODE_OFF;
}

static void colo_rewriter_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    object_class_property_add_bool(oc, "vnet_hdr_support",
                                   filter_rewriter_get_vnet_hdr,
                                   filter_rewriter_set_vnet_hdr);

    nfc->setup = colo_rewriter_setup;
    nfc->cleanup = colo_rewriter_cleanup;
    nfc->receive_iov = colo_rewriter_receive_iov;
    nfc->handle_event = colo_rewriter_handle_event;
}

static const TypeInfo colo_rewriter_info = {
    .name = TYPE_FILTER_REWRITER,
    .parent = TYPE_NETFILTER,
    .class_init = colo_rewriter_class_init,
    .instance_init = filter_rewriter_init,
    .instance_size = sizeof(RewriterState),
};

static void register_types(void)
{
    type_register_static(&colo_rewriter_info);
}

type_init(register_types);
