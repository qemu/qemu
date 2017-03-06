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
#include "trace.h"
#include "net/colo.h"

uint32_t connection_key_hash(const void *opaque)
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

int connection_key_equal(const void *key1, const void *key2)
{
    return memcmp(key1, key2, sizeof(ConnectionKey)) == 0;
}

int parse_packet_early(Packet *pkt)
{
    int network_length;
    static const uint8_t vlan[] = {0x81, 0x00};
    uint8_t *data = pkt->data;
    uint16_t l3_proto;
    ssize_t l2hdr_len = eth_get_l2_hdr_length(data);

    if (pkt->size < ETH_HLEN) {
        trace_colo_proxy_main("pkt->size < ETH_HLEN");
        return 1;
    }

    /*
     * TODO: support vlan.
     */
    if (!memcmp(&data[12], vlan, sizeof(vlan))) {
        trace_colo_proxy_main("COLO-proxy don't support vlan");
        return 1;
    }

    pkt->network_header = data + l2hdr_len;

    const struct iovec l2vec = {
        .iov_base = (void *) data,
        .iov_len = l2hdr_len
    };
    l3_proto = eth_get_l3_proto(&l2vec, 1, l2hdr_len);

    if (l3_proto != ETH_P_IP) {
        return 1;
    }

    network_length = pkt->ip->ip_hl * 4;
    if (pkt->size < l2hdr_len + network_length) {
        trace_colo_proxy_main("pkt->size < network_header + network_length");
        return 1;
    }
    pkt->transport_header = pkt->network_header + network_length;

    return 0;
}

void fill_connection_key(Packet *pkt, ConnectionKey *key)
{
    uint32_t tmp_ports;

    memset(key, 0, sizeof(*key));
    key->ip_proto = pkt->ip->ip_p;

    switch (key->ip_proto) {
    case IPPROTO_TCP:
    case IPPROTO_UDP:
    case IPPROTO_DCCP:
    case IPPROTO_ESP:
    case IPPROTO_SCTP:
    case IPPROTO_UDPLITE:
        tmp_ports = *(uint32_t *)(pkt->transport_header);
        key->src = pkt->ip->ip_src;
        key->dst = pkt->ip->ip_dst;
        key->src_port = ntohs(tmp_ports & 0xffff);
        key->dst_port = ntohs(tmp_ports >> 16);
        break;
    case IPPROTO_AH:
        tmp_ports = *(uint32_t *)(pkt->transport_header + 4);
        key->src = pkt->ip->ip_src;
        key->dst = pkt->ip->ip_dst;
        key->src_port = ntohs(tmp_ports & 0xffff);
        key->dst_port = ntohs(tmp_ports >> 16);
        break;
    default:
        break;
    }
}

void reverse_connection_key(ConnectionKey *key)
{
    struct in_addr tmp_ip;
    uint16_t tmp_port;

    tmp_ip = key->src;
    key->src = key->dst;
    key->dst = tmp_ip;

    tmp_port = key->src_port;
    key->src_port = key->dst_port;
    key->dst_port = tmp_port;
}

Connection *connection_new(ConnectionKey *key)
{
    Connection *conn = g_slice_new(Connection);

    conn->ip_proto = key->ip_proto;
    conn->processing = false;
    conn->offset = 0;
    conn->syn_flag = 0;
    g_queue_init(&conn->primary_list);
    g_queue_init(&conn->secondary_list);

    return conn;
}

void connection_destroy(void *opaque)
{
    Connection *conn = opaque;

    g_queue_foreach(&conn->primary_list, packet_destroy, NULL);
    g_queue_clear(&conn->primary_list);
    g_queue_foreach(&conn->secondary_list, packet_destroy, NULL);
    g_queue_clear(&conn->secondary_list);
    g_slice_free(Connection, conn);
}

Packet *packet_new(const void *data, int size)
{
    Packet *pkt = g_slice_new(Packet);

    pkt->data = g_memdup(data, size);
    pkt->size = size;
    pkt->creation_ms = qemu_clock_get_ms(QEMU_CLOCK_HOST);

    return pkt;
}

void packet_destroy(void *opaque, void *user_data)
{
    Packet *pkt = opaque;

    g_free(pkt->data);
    g_slice_free(Packet, pkt);
}

/*
 * Clear hashtable, stop this hash growing really huge
 */
void connection_hashtable_reset(GHashTable *connection_track_table)
{
    g_hash_table_remove_all(connection_track_table);
}

/* if not found, create a new connection and add to hash table */
Connection *connection_get(GHashTable *connection_track_table,
                           ConnectionKey *key,
                           GQueue *conn_list)
{
    Connection *conn = g_hash_table_lookup(connection_track_table, key);

    if (conn == NULL) {
        ConnectionKey *new_key = g_memdup(key, sizeof(*key));

        conn = connection_new(key);

        if (g_hash_table_size(connection_track_table) > HASHTABLE_MAX_SIZE) {
            trace_colo_proxy_main("colo proxy connection hashtable full,"
                                  " clear it");
            connection_hashtable_reset(connection_track_table);
            /*
             * clear the conn_list
             */
            while (!g_queue_is_empty(conn_list)) {
                connection_destroy(g_queue_pop_head(conn_list));
            }
        }

        g_hash_table_insert(connection_track_table, new_key, conn);
    }

    return conn;
}
