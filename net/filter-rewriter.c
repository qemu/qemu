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
#include "net/colo.h"
#include "net/filter.h"
#include "net/net.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qapi-visit.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/iov.h"
#include "net/checksum.h"

#define FILTER_COLO_REWRITER(obj) \
    OBJECT_CHECK(RewriterState, (obj), TYPE_FILTER_REWRITER)

#define TYPE_FILTER_REWRITER "filter-rewriter"

typedef struct RewriterState {
    NetFilterState parent_obj;
    NetQueue *incoming_queue;
    /* hashtable to save connection */
    GHashTable *connection_track_table;
} RewriterState;

static void filter_rewriter_flush(NetFilterState *nf)
{
    RewriterState *s = FILTER_COLO_REWRITER(nf);

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
static int handle_primary_tcp_pkt(NetFilterState *nf,
                                  Connection *conn,
                                  Packet *pkt)
{
    struct tcphdr *tcp_pkt;

    tcp_pkt = (struct tcphdr *)pkt->transport_header;
    if (trace_event_get_state(TRACE_COLO_FILTER_REWRITER_DEBUG)) {
        trace_colo_filter_rewriter_pkt_info(__func__,
                    inet_ntoa(pkt->ip->ip_src), inet_ntoa(pkt->ip->ip_dst),
                    ntohl(tcp_pkt->th_seq), ntohl(tcp_pkt->th_ack),
                    tcp_pkt->th_flags);
        trace_colo_filter_rewriter_conn_offset(conn->offset);
    }

    if (((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == TH_SYN)) {
        /*
         * we use this flag update offset func
         * run once in independent tcp connection
         */
        conn->syn_flag = 1;
    }

    if (((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == TH_ACK)) {
        if (conn->syn_flag) {
            /*
             * offset = secondary_seq - primary seq
             * ack packet sent by guest from primary node,
             * so we use th_ack - 1 get primary_seq
             */
            conn->offset -= (ntohl(tcp_pkt->th_ack) - 1);
            conn->syn_flag = 0;
        }
        if (conn->offset) {
            /* handle packets to the secondary from the primary */
            tcp_pkt->th_ack = htonl(ntohl(tcp_pkt->th_ack) + conn->offset);

            net_checksum_calculate((uint8_t *)pkt->data, pkt->size);
        }
    }

    return 0;
}

/* handle tcp packet from secondary guest */
static int handle_secondary_tcp_pkt(NetFilterState *nf,
                                    Connection *conn,
                                    Packet *pkt)
{
    struct tcphdr *tcp_pkt;

    tcp_pkt = (struct tcphdr *)pkt->transport_header;

    if (trace_event_get_state(TRACE_COLO_FILTER_REWRITER_DEBUG)) {
        trace_colo_filter_rewriter_pkt_info(__func__,
                    inet_ntoa(pkt->ip->ip_src), inet_ntoa(pkt->ip->ip_dst),
                    ntohl(tcp_pkt->th_seq), ntohl(tcp_pkt->th_ack),
                    tcp_pkt->th_flags);
        trace_colo_filter_rewriter_conn_offset(conn->offset);
    }

    if (((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == (TH_ACK | TH_SYN))) {
        /*
         * save offset = secondary_seq and then
         * in handle_primary_tcp_pkt make offset
         * = secondary_seq - primary_seq
         */
        conn->offset = ntohl(tcp_pkt->th_seq);
    }

    if ((tcp_pkt->th_flags & (TH_ACK | TH_SYN)) == TH_ACK) {
        /* Only need to adjust seq while offset is Non-zero */
        if (conn->offset) {
            /* handle packets to the primary from the secondary*/
            tcp_pkt->th_seq = htonl(ntohl(tcp_pkt->th_seq) - conn->offset);

            net_checksum_calculate((uint8_t *)pkt->data, pkt->size);
        }
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
    RewriterState *s = FILTER_COLO_REWRITER(nf);
    Connection *conn;
    ConnectionKey key;
    Packet *pkt;
    ssize_t size = iov_size(iov, iovcnt);
    char *buf = g_malloc0(size);

    iov_to_buf(iov, iovcnt, 0, buf, size);
    pkt = packet_new(buf, size);
    g_free(buf);

    /*
     * if we get tcp packet
     * we will rewrite it to make secondary guest's
     * connection established successfully
     */
    if (pkt && is_tcp_packet(pkt)) {

        fill_connection_key(pkt, &key);

        if (sender == nf->netdev) {
            /*
             * We need make tcp TX and RX packet
             * into one connection.
             */
            reverse_connection_key(&key);
        }
        conn = connection_get(s->connection_track_table,
                              &key,
                              NULL);

        if (sender == nf->netdev) {
            /* NET_FILTER_DIRECTION_TX */
            if (!handle_primary_tcp_pkt(nf, conn, pkt)) {
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
            if (!handle_secondary_tcp_pkt(nf, conn, pkt)) {
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

    packet_destroy(pkt, NULL);
    pkt = NULL;
    return 0;
}

static void colo_rewriter_cleanup(NetFilterState *nf)
{
    RewriterState *s = FILTER_COLO_REWRITER(nf);

    /* flush packets */
    if (s->incoming_queue) {
        filter_rewriter_flush(nf);
        g_free(s->incoming_queue);
    }
}

static void colo_rewriter_setup(NetFilterState *nf, Error **errp)
{
    RewriterState *s = FILTER_COLO_REWRITER(nf);

    s->connection_track_table = g_hash_table_new_full(connection_key_hash,
                                                      connection_key_equal,
                                                      g_free,
                                                      connection_destroy);
    s->incoming_queue = qemu_new_net_queue(qemu_netfilter_pass_to_next, nf);
}

static void colo_rewriter_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    nfc->setup = colo_rewriter_setup;
    nfc->cleanup = colo_rewriter_cleanup;
    nfc->receive_iov = colo_rewriter_receive_iov;
}

static const TypeInfo colo_rewriter_info = {
    .name = TYPE_FILTER_REWRITER,
    .parent = TYPE_NETFILTER,
    .class_init = colo_rewriter_class_init,
    .instance_size = sizeof(RewriterState),
};

static void register_types(void)
{
    type_register_static(&colo_rewriter_info);
}

type_init(register_types);
