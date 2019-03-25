/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2013
 * Guillaume Subiron, Yann Bordenave, Serigne Modou Wagne.
 */

#include "slirp.h"

void ndp_table_add(Slirp *slirp, struct in6_addr ip_addr,
                    uint8_t ethaddr[ETH_ALEN])
{
    char addrstr[INET6_ADDRSTRLEN];
    NdpTable *ndp_table = &slirp->ndp_table;
    int i;

    inet_ntop(AF_INET6, &(ip_addr), addrstr, INET6_ADDRSTRLEN);

    DEBUG_CALL("ndp_table_add");
    DEBUG_ARG("ip = %s", addrstr);
    DEBUG_ARG("hw addr = %02x:%02x:%02x:%02x:%02x:%02x",
              ethaddr[0], ethaddr[1], ethaddr[2],
              ethaddr[3], ethaddr[4], ethaddr[5]);

    if (IN6_IS_ADDR_MULTICAST(&ip_addr) || in6_zero(&ip_addr)) {
        /* Do not register multicast or unspecified addresses */
        DEBUG_CALL(" abort: do not register multicast or unspecified address");
        return;
    }

    /* Search for an entry */
    for (i = 0; i < NDP_TABLE_SIZE; i++) {
        if (in6_equal(&ndp_table->table[i].ip_addr, &ip_addr)) {
            DEBUG_CALL(" already in table: update the entry");
            /* Update the entry */
            memcpy(ndp_table->table[i].eth_addr, ethaddr, ETH_ALEN);
            return;
        }
    }

    /* No entry found, create a new one */
    DEBUG_CALL(" create new entry");
    ndp_table->table[ndp_table->next_victim].ip_addr = ip_addr;
    memcpy(ndp_table->table[ndp_table->next_victim].eth_addr,
            ethaddr, ETH_ALEN);
    ndp_table->next_victim = (ndp_table->next_victim + 1) % NDP_TABLE_SIZE;
}

bool ndp_table_search(Slirp *slirp, struct in6_addr ip_addr,
                      uint8_t out_ethaddr[ETH_ALEN])
{
    char addrstr[INET6_ADDRSTRLEN];
    NdpTable *ndp_table = &slirp->ndp_table;
    int i;

    inet_ntop(AF_INET6, &(ip_addr), addrstr, INET6_ADDRSTRLEN);

    DEBUG_CALL("ndp_table_search");
    DEBUG_ARG("ip = %s", addrstr);

    assert(!in6_zero(&ip_addr));

    /* Multicast address: fec0::abcd:efgh/8 -> 33:33:ab:cd:ef:gh */
    if (IN6_IS_ADDR_MULTICAST(&ip_addr)) {
        out_ethaddr[0] = 0x33; out_ethaddr[1] = 0x33;
        out_ethaddr[2] = ip_addr.s6_addr[12];
        out_ethaddr[3] = ip_addr.s6_addr[13];
        out_ethaddr[4] = ip_addr.s6_addr[14];
        out_ethaddr[5] = ip_addr.s6_addr[15];
        DEBUG_ARG("multicast addr = %02x:%02x:%02x:%02x:%02x:%02x",
                  out_ethaddr[0], out_ethaddr[1], out_ethaddr[2],
                  out_ethaddr[3], out_ethaddr[4], out_ethaddr[5]);
        return 1;
    }

    for (i = 0; i < NDP_TABLE_SIZE; i++) {
        if (in6_equal(&ndp_table->table[i].ip_addr, &ip_addr)) {
            memcpy(out_ethaddr, ndp_table->table[i].eth_addr,  ETH_ALEN);
            DEBUG_ARG("found hw addr = %02x:%02x:%02x:%02x:%02x:%02x",
                      out_ethaddr[0], out_ethaddr[1], out_ethaddr[2],
                      out_ethaddr[3], out_ethaddr[4], out_ethaddr[5]);
            return 1;
        }
    }

    DEBUG_CALL(" ip not found in table");
    return 0;
}
