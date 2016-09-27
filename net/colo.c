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

Packet *packet_new(const void *data, int size)
{
    Packet *pkt = g_slice_new(Packet);

    pkt->data = g_memdup(data, size);
    pkt->size = size;

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
