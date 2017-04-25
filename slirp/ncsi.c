/*
 * NC-SI (Network Controller Sideband Interface) "echo" model
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "slirp.h"

#include "ncsi-pkt.h"

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
        { NCSI_PKT_RSP_GVI,    36, NULL },
        { NCSI_PKT_RSP_GC,     32, ncsi_rsp_handler_gc },
        { NCSI_PKT_RSP_GP,     -1, NULL },
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

    memset(ncsi_reply, 0, sizeof(ncsi_reply));

    memset(reh->h_dest, 0xff, ETH_ALEN);
    memset(reh->h_source, 0xff, ETH_ALEN);
    reh->h_proto = htons(ETH_P_NCSI);

    for (i = 0; i < ARRAY_SIZE(ncsi_rsp_handlers); i++) {
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
    } else {
        rnh->common.length = 0;
        rnh->code          = htons(NCSI_PKT_RSP_C_UNAVAILABLE);
        rnh->reason        = htons(NCSI_PKT_RSP_R_UNKNOWN);
    }

    /* TODO: add a checksum at the end of the frame but the specs
     * allows it to be zero */

    slirp_output(slirp->opaque, ncsi_reply, ETH_HLEN + sizeof(*nh) +
                 (handler ? handler->payload : 0) + 4);
}
