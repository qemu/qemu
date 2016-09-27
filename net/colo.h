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

#ifndef QEMU_COLO_PROXY_H
#define QEMU_COLO_PROXY_H

#include "slirp/slirp.h"
#include "qemu/jhash.h"

#define HASHTABLE_MAX_SIZE 16384

typedef struct Packet {
    void *data;
    union {
        uint8_t *network_header;
        struct ip *ip;
    };
    uint8_t *transport_header;
    int size;
} Packet;

int parse_packet_early(Packet *pkt);
void connection_hashtable_reset(GHashTable *connection_track_table);
Packet *packet_new(const void *data, int size);
void packet_destroy(void *opaque, void *user_data);

#endif /* QEMU_COLO_PROXY_H */
