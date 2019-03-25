/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * NC-SI (Network Controller Sideband Interface) "echo" model
 *
 * Copyright (C) 2016-2018 IBM Corp.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 * copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "slirp.h"

#include "ncsi-pkt.h"

static uint32_t ncsi_calculate_checksum(uint16_t *data, int len)
{
    uint32_t checksum = 0;
    int i;

    /*
     * 32-bit unsigned sum of the NC-SI packet header and NC-SI packet
     * payload interpreted as a series of 16-bit unsigned integer values.
     */
    for (i = 0; i < len; i++) {
        checksum += htons(data[i]);
    }

    checksum = (~checksum + 1);
    return checksum;
}

/* Get Capabilities */
static int ncsi_rsp_handler_gc(struct ncsi_rsp_pkt_hdr *rnh)
{
    struct ncsi_rsp_gc_pkt *rsp = (struct ncsi_rsp_gc_pkt *) rnh;

    rsp->cap = htonl(~0);
    rsp->bc_cap = htonl(~0);
    rsp->mc_cap = htonl(~0);
    rsp->buf_cap = htonl(~0);
    rsp->aen_cap = htonl(~0);
    rsp->vlan_mode = 0xff;
    rsp->uc_cnt = 2;
    return 0;
}

/* Get Link status */
static int ncsi_rsp_handler_gls(struct ncsi_rsp_pkt_hdr *rnh)
{
    struct ncsi_rsp_gls_pkt *rsp = (struct ncsi_rsp_gls_pkt *) rnh;

    rsp->status = htonl(0x1);
    return 0;
}

/* Get Parameters */
static int ncsi_rsp_handler_gp(struct ncsi_rsp_pkt_hdr *rnh)
{
    struct ncsi_rsp_gp_pkt *rsp = (struct ncsi_rsp_gp_pkt *) rnh;

    /* no MAC address filters or VLAN filters on the channel */
    rsp->mac_cnt = 0;
    rsp->mac_enable = 0;
    rsp->vlan_cnt = 0;
    rsp->vlan_enable = 0;

    return 0;
}

static const struct ncsi_rsp_handler {
        unsigned char   type;
        int             payload;
        int             (*handler)(struct ncsi_rsp_pkt_hdr *rnh);
} ncsi_rsp_handlers[] = {
        { NCSI_PKT_RSP_CIS,     4, NULL },
        { NCSI_PKT_RSP_SP,      4, NULL },
        { NCSI_PKT_RSP_DP,      4, NULL },
        { NCSI_PKT_RSP_EC,      4, NULL },
        { NCSI_PKT_RSP_DC,      4, NULL },
        { NCSI_PKT_RSP_RC,      4, NULL },
        { NCSI_PKT_RSP_ECNT,    4, NULL },
        { NCSI_PKT_RSP_DCNT,    4, NULL },
        { NCSI_PKT_RSP_AE,      4, NULL },
        { NCSI_PKT_RSP_SL,      4, NULL },
        { NCSI_PKT_RSP_GLS,    16, ncsi_rsp_handler_gls },
        { NCSI_PKT_RSP_SVF,     4, NULL },
        { NCSI_PKT_RSP_EV,      4, NULL },
        { NCSI_PKT_RSP_DV,      4, NULL },
        { NCSI_PKT_RSP_SMA,     4, NULL },
        { NCSI_PKT_RSP_EBF,     4, NULL },
        { NCSI_PKT_RSP_DBF,     4, NULL },
        { NCSI_PKT_RSP_EGMF,    4, NULL },
        { NCSI_PKT_RSP_DGMF,    4, NULL },
        { NCSI_PKT_RSP_SNFC,    4, NULL },
        { NCSI_PKT_RSP_GVI,    40, NULL },
        { NCSI_PKT_RSP_GC,     32, ncsi_rsp_handler_gc },
        { NCSI_PKT_RSP_GP,     40, ncsi_rsp_handler_gp },
        { NCSI_PKT_RSP_GCPS,  172, NULL },
        { NCSI_PKT_RSP_GNS,   172, NULL },
        { NCSI_PKT_RSP_GNPTS, 172, NULL },
        { NCSI_PKT_RSP_GPS,     8, NULL },
        { NCSI_PKT_RSP_OEM,     0, NULL },
        { NCSI_PKT_RSP_PLDM,    0, NULL },
        { NCSI_PKT_RSP_GPUUID, 20, NULL }
};

/*
 * packet format : ncsi header + payload + checksum
 */
#define NCSI_MAX_PAYLOAD 172
#define NCSI_MAX_LEN     (sizeof(struct ncsi_pkt_hdr) + NCSI_MAX_PAYLOAD + 4)

void ncsi_input(Slirp *slirp, const uint8_t *pkt, int pkt_len)
{
    struct ncsi_pkt_hdr *nh = (struct ncsi_pkt_hdr *)(pkt + ETH_HLEN);
    uint8_t ncsi_reply[ETH_HLEN + NCSI_MAX_LEN];
    struct ethhdr *reh = (struct ethhdr *)ncsi_reply;
    struct ncsi_rsp_pkt_hdr *rnh = (struct ncsi_rsp_pkt_hdr *)
        (ncsi_reply + ETH_HLEN);
    const struct ncsi_rsp_handler *handler = NULL;
    int i;
    int ncsi_rsp_len = sizeof(*nh);
    uint32_t checksum;
    uint32_t *pchecksum;

    memset(ncsi_reply, 0, sizeof(ncsi_reply));

    memset(reh->h_dest, 0xff, ETH_ALEN);
    memset(reh->h_source, 0xff, ETH_ALEN);
    reh->h_proto = htons(ETH_P_NCSI);

    for (i = 0; i < G_N_ELEMENTS(ncsi_rsp_handlers); i++) {
        if (ncsi_rsp_handlers[i].type == nh->type + 0x80) {
            handler = &ncsi_rsp_handlers[i];
            break;
        }
    }

    rnh->common.mc_id      = nh->mc_id;
    rnh->common.revision   = NCSI_PKT_REVISION;
    rnh->common.id         = nh->id;
    rnh->common.type       = nh->type + 0x80;
    rnh->common.channel    = nh->channel;

    if (handler) {
        rnh->common.length = htons(handler->payload);
        rnh->code          = htons(NCSI_PKT_RSP_C_COMPLETED);
        rnh->reason        = htons(NCSI_PKT_RSP_R_NO_ERROR);

        if (handler->handler) {
            /* TODO: handle errors */
            handler->handler(rnh);
        }
        ncsi_rsp_len += handler->payload;
    } else {
        rnh->common.length = 0;
        rnh->code          = htons(NCSI_PKT_RSP_C_UNAVAILABLE);
        rnh->reason        = htons(NCSI_PKT_RSP_R_UNKNOWN);
    }

    /* Add the optional checksum at the end of the frame. */
    checksum = ncsi_calculate_checksum((uint16_t *) rnh, ncsi_rsp_len);
    pchecksum = (uint32_t *)((void *) rnh + ncsi_rsp_len);
    *pchecksum = htonl(checksum);
    ncsi_rsp_len += 4;

    slirp_send_packet_all(slirp, ncsi_reply, ETH_HLEN + ncsi_rsp_len);
}
