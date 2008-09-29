/*
 * QEMU Bluetooth L2CAP logic.
 *
 * Copyright (C) 2008 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA  02110-1301  USA
 */

#include "qemu-common.h"
#include "qemu-timer.h"
#include "bt.h"

#define L2CAP_CID_MAX	0x100	/* Between 0x40 and 0x10000 */

struct l2cap_instance_s {
    struct bt_link_s *link;
    struct bt_l2cap_device_s *dev;
    int role;

    uint8_t frame_in[65535 + L2CAP_HDR_SIZE] __attribute__ ((aligned (4)));
    int frame_in_len;

    uint8_t frame_out[65535 + L2CAP_HDR_SIZE] __attribute__ ((aligned (4)));
    int frame_out_len;

    /* Signalling channel timers.  They exist per-request but we can make
     * sure we have no more than one outstanding request at any time.  */
    QEMUTimer *rtx;
    QEMUTimer *ertx;

    int last_id;
    int next_id;

    struct l2cap_chan_s {
        struct bt_l2cap_conn_params_s params;

        void (*frame_in)(struct l2cap_chan_s *chan, uint16_t cid,
                        const l2cap_hdr *hdr, int len);
        int mps;
        int min_mtu;

        struct l2cap_instance_s *l2cap;

        /* Only allocated channels */
        uint16_t remote_cid;
#define L2CAP_CFG_INIT	2
#define L2CAP_CFG_ACC	1
        int config_req_id; /* TODO: handle outgoing requests generically */
        int config;

        /* Only connection-oriented channels.  Note: if we allow the tx and
         * rx traffic to be in different modes at any time, we need two.  */
        int mode;

        /* Only flow-controlled, connection-oriented channels */
        uint8_t sdu[65536]; /* TODO: dynamically allocate */
        int len_cur, len_total;
        int rexmit;
        int monitor_timeout;
        QEMUTimer *monitor_timer;
        QEMUTimer *retransmission_timer;
    } *cid[L2CAP_CID_MAX];
    /* The channel state machine states map as following:
     * CLOSED           -> !cid[N]
     * WAIT_CONNECT     -> never occurs
     * WAIT_CONNECT_RSP -> never occurs
     * CONFIG           -> cid[N] && config < 3
     *   WAIT_CONFIG         -> never occurs, cid[N] && config == 0 && !config_r
     *   WAIT_SEND_CONFIG    -> never occurs, cid[N] && config == 1 && !config_r
     *   WAIT_CONFIG_REQ_RSP -> cid[N] && config == 0 && config_req_id
     *   WAIT_CONFIG_RSP     -> cid[N] && config == 1 && config_req_id
     *   WAIT_CONFIG_REQ     -> cid[N] && config == 2
     * OPEN             -> cid[N] && config == 3
     * WAIT_DISCONNECT  -> never occurs
     */

    struct l2cap_chan_s signalling_ch;
    struct l2cap_chan_s group_ch;
};

struct slave_l2cap_instance_s {
    struct bt_link_s link;	/* Underlying logical link (ACL) */
    struct l2cap_instance_s l2cap;
};

struct bt_l2cap_psm_s {
    int psm;
    int min_mtu;
    int (*new_channel)(struct bt_l2cap_device_s *device,
                    struct bt_l2cap_conn_params_s *params);
    struct bt_l2cap_psm_s *next;
};

static const uint16_t l2cap_fcs16_table[256] = {
    0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
    0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440,
    0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40,
    0x0a00, 0xcac1, 0xcb81, 0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841,
    0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40,
    0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41,
    0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
    0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040,
    0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
    0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441,
    0x3c00, 0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41,
    0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840,
    0x2800, 0xe8c1, 0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41,
    0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
    0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640,
    0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0, 0x2080, 0xe041,
    0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240,
    0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
    0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41,
    0xaa01, 0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840,
    0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
    0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40,
    0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1, 0xb681, 0x7640,
    0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041,
    0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0, 0x5280, 0x9241,
    0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440,
    0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
    0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
    0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40,
    0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41,
    0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641,
    0x8201, 0x42c0, 0x4380, 0x8341, 0x4100, 0x81c1, 0x8081, 0x4040,
};

static uint16_t l2cap_fcs16(const uint8_t *message, int len)
{
    uint16_t fcs = 0x0000;

    while (len --)
#if 0
    {
        int i;

        fcs ^= *message ++;
        for (i = 8; i; -- i)
            if (fcs & 1)
                fcs = (fcs >> 1) ^ 0xa001;
            else
                fcs = (fcs >> 1);
    }
#else
        fcs = (fcs >> 8) ^ l2cap_fcs16_table[(fcs ^ *message ++) & 0xff];
#endif

    return fcs;
}

/* L2CAP layer logic (protocol) */

static void l2cap_retransmission_timer_update(struct l2cap_chan_s *ch)
{
#if 0
    if (ch->mode != L2CAP_MODE_BASIC && ch->rexmit)
        qemu_mod_timer(ch->retransmission_timer);
    else
        qemu_del_timer(ch->retransmission_timer);
#endif
}

static void l2cap_monitor_timer_update(struct l2cap_chan_s *ch)
{
#if 0
    if (ch->mode != L2CAP_MODE_BASIC && !ch->rexmit)
        qemu_mod_timer(ch->monitor_timer);
    else
        qemu_del_timer(ch->monitor_timer);
#endif
}

static void l2cap_command_reject(struct l2cap_instance_s *l2cap, int id,
                uint16_t reason, const void *data, int plen)
{
    uint8_t *pkt;
    l2cap_cmd_hdr *hdr;
    l2cap_cmd_rej *params;
    uint16_t len;

    reason = cpu_to_le16(reason);
    len = cpu_to_le16(L2CAP_CMD_REJ_SIZE + plen);

    pkt = l2cap->signalling_ch.params.sdu_out(&l2cap->signalling_ch.params,
                    L2CAP_CMD_HDR_SIZE + L2CAP_CMD_REJ_SIZE + plen);
    hdr = (void *) (pkt + 0);
    params = (void *) (pkt + L2CAP_CMD_HDR_SIZE);

    hdr->code = L2CAP_COMMAND_REJ;
    hdr->ident = id;
    memcpy(&hdr->len, &len, sizeof(hdr->len));
    memcpy(&params->reason, &reason, sizeof(reason));
    if (plen)
       memcpy(pkt + L2CAP_CMD_HDR_SIZE + L2CAP_CMD_REJ_SIZE, data, plen);

    l2cap->signalling_ch.params.sdu_submit(&l2cap->signalling_ch.params);
}

static void l2cap_command_reject_cid(struct l2cap_instance_s *l2cap, int id,
                uint16_t reason, uint16_t dcid, uint16_t scid)
{
    l2cap_cmd_rej_cid params = {
        .dcid = dcid,
        .scid = scid,
    };

    l2cap_command_reject(l2cap, id, reason, &params, L2CAP_CMD_REJ_CID_SIZE);
}

static void l2cap_connection_response(struct l2cap_instance_s *l2cap,
                int dcid, int scid, int result, int status)
{
    uint8_t *pkt;
    l2cap_cmd_hdr *hdr;
    l2cap_conn_rsp *params;

    pkt = l2cap->signalling_ch.params.sdu_out(&l2cap->signalling_ch.params,
                    L2CAP_CMD_HDR_SIZE + L2CAP_CONN_RSP_SIZE);
    hdr = (void *) (pkt + 0);
    params = (void *) (pkt + L2CAP_CMD_HDR_SIZE);

    hdr->code = L2CAP_CONN_RSP;
    hdr->ident = l2cap->last_id;
    hdr->len = cpu_to_le16(L2CAP_CONN_RSP_SIZE);

    params->dcid = cpu_to_le16(dcid);
    params->scid = cpu_to_le16(scid);
    params->result = cpu_to_le16(result);
    params->status = cpu_to_le16(status);

    l2cap->signalling_ch.params.sdu_submit(&l2cap->signalling_ch.params);
}

static void l2cap_configuration_request(struct l2cap_instance_s *l2cap,
                int dcid, int flag, const uint8_t *data, int len)
{
    uint8_t *pkt;
    l2cap_cmd_hdr *hdr;
    l2cap_conf_req *params;

    pkt = l2cap->signalling_ch.params.sdu_out(&l2cap->signalling_ch.params,
                    L2CAP_CMD_HDR_SIZE + L2CAP_CONF_REQ_SIZE(len));
    hdr = (void *) (pkt + 0);
    params = (void *) (pkt + L2CAP_CMD_HDR_SIZE);

    /* TODO: unify the id sequencing */
    l2cap->last_id = l2cap->next_id;
    l2cap->next_id = l2cap->next_id == 255 ? 1 : l2cap->next_id + 1;

    hdr->code = L2CAP_CONF_REQ;
    hdr->ident = l2cap->last_id;
    hdr->len = cpu_to_le16(L2CAP_CONF_REQ_SIZE(len));

    params->dcid = cpu_to_le16(dcid);
    params->flags = cpu_to_le16(flag);
    if (len)
        memcpy(params->data, data, len);

    l2cap->signalling_ch.params.sdu_submit(&l2cap->signalling_ch.params);
}

static void l2cap_configuration_response(struct l2cap_instance_s *l2cap,
                int scid, int flag, int result, const uint8_t *data, int len)
{
    uint8_t *pkt;
    l2cap_cmd_hdr *hdr;
    l2cap_conf_rsp *params;

    pkt = l2cap->signalling_ch.params.sdu_out(&l2cap->signalling_ch.params,
                    L2CAP_CMD_HDR_SIZE + L2CAP_CONF_RSP_SIZE(len));
    hdr = (void *) (pkt + 0);
    params = (void *) (pkt + L2CAP_CMD_HDR_SIZE);

    hdr->code = L2CAP_CONF_RSP;
    hdr->ident = l2cap->last_id;
    hdr->len = cpu_to_le16(L2CAP_CONF_RSP_SIZE(len));

    params->scid = cpu_to_le16(scid);
    params->flags = cpu_to_le16(flag);
    params->result = cpu_to_le16(result);
    if (len)
        memcpy(params->data, data, len);

    l2cap->signalling_ch.params.sdu_submit(&l2cap->signalling_ch.params);
}

static void l2cap_disconnection_response(struct l2cap_instance_s *l2cap,
                int dcid, int scid)
{
    uint8_t *pkt;
    l2cap_cmd_hdr *hdr;
    l2cap_disconn_rsp *params;

    pkt = l2cap->signalling_ch.params.sdu_out(&l2cap->signalling_ch.params,
                    L2CAP_CMD_HDR_SIZE + L2CAP_DISCONN_RSP_SIZE);
    hdr = (void *) (pkt + 0);
    params = (void *) (pkt + L2CAP_CMD_HDR_SIZE);

    hdr->code = L2CAP_DISCONN_RSP;
    hdr->ident = l2cap->last_id;
    hdr->len = cpu_to_le16(L2CAP_DISCONN_RSP_SIZE);

    params->dcid = cpu_to_le16(dcid);
    params->scid = cpu_to_le16(scid);

    l2cap->signalling_ch.params.sdu_submit(&l2cap->signalling_ch.params);
}

static void l2cap_echo_response(struct l2cap_instance_s *l2cap,
                const uint8_t *data, int len)
{
    uint8_t *pkt;
    l2cap_cmd_hdr *hdr;
    uint8_t *params;

    pkt = l2cap->signalling_ch.params.sdu_out(&l2cap->signalling_ch.params,
                    L2CAP_CMD_HDR_SIZE + len);
    hdr = (void *) (pkt + 0);
    params = (void *) (pkt + L2CAP_CMD_HDR_SIZE);

    hdr->code = L2CAP_ECHO_RSP;
    hdr->ident = l2cap->last_id;
    hdr->len = cpu_to_le16(len);

    memcpy(params, data, len);

    l2cap->signalling_ch.params.sdu_submit(&l2cap->signalling_ch.params);
}

static void l2cap_info_response(struct l2cap_instance_s *l2cap, int type,
                int result, const uint8_t *data, int len)
{
    uint8_t *pkt;
    l2cap_cmd_hdr *hdr;
    l2cap_info_rsp *params;

    pkt = l2cap->signalling_ch.params.sdu_out(&l2cap->signalling_ch.params,
                    L2CAP_CMD_HDR_SIZE + L2CAP_INFO_RSP_SIZE + len);
    hdr = (void *) (pkt + 0);
    params = (void *) (pkt + L2CAP_CMD_HDR_SIZE);

    hdr->code = L2CAP_INFO_RSP;
    hdr->ident = l2cap->last_id;
    hdr->len = cpu_to_le16(L2CAP_INFO_RSP_SIZE + len);

    params->type = cpu_to_le16(type);
    params->result = cpu_to_le16(result);
    if (len)
       memcpy(params->data, data, len);

    l2cap->signalling_ch.params.sdu_submit(&l2cap->signalling_ch.params);
}

static uint8_t *l2cap_bframe_out(struct bt_l2cap_conn_params_s *parm, int len);
static void l2cap_bframe_submit(struct bt_l2cap_conn_params_s *parms);
#if 0
static uint8_t *l2cap_iframe_out(struct bt_l2cap_conn_params_s *parm, int len);
static void l2cap_iframe_submit(struct bt_l2cap_conn_params_s *parm);
#endif
static void l2cap_bframe_in(struct l2cap_chan_s *ch, uint16_t cid,
                const l2cap_hdr *hdr, int len);
static void l2cap_iframe_in(struct l2cap_chan_s *ch, uint16_t cid,
                const l2cap_hdr *hdr, int len);

static int l2cap_cid_new(struct l2cap_instance_s *l2cap)
{
    int i;

    for (i = L2CAP_CID_ALLOC; i < L2CAP_CID_MAX; i ++)
        if (!l2cap->cid[i])
            return i;

    return L2CAP_CID_INVALID;
}

static inline struct bt_l2cap_psm_s *l2cap_psm(
                struct bt_l2cap_device_s *device, int psm)
{
    struct bt_l2cap_psm_s *ret = device->first_psm;

    while (ret && ret->psm != psm)
        ret = ret->next;

    return ret;
}

static struct l2cap_chan_s *l2cap_channel_open(struct l2cap_instance_s *l2cap,
                int psm, int source_cid)
{
    struct l2cap_chan_s *ch = 0;
    struct bt_l2cap_psm_s *psm_info;
    int result, status;
    int cid = l2cap_cid_new(l2cap);

    if (cid) {
        /* See what the channel is to be used for.. */
        psm_info = l2cap_psm(l2cap->dev, psm);

        if (psm_info) {
            /* Device supports this use-case.  */
            ch = qemu_mallocz(sizeof(*ch));
            ch->params.sdu_out = l2cap_bframe_out;
            ch->params.sdu_submit = l2cap_bframe_submit;
            ch->frame_in = l2cap_bframe_in;
            ch->mps = 65536;
            ch->min_mtu = MAX(48, psm_info->min_mtu);
            ch->params.remote_mtu = MAX(672, ch->min_mtu);
            ch->remote_cid = source_cid;
            ch->mode = L2CAP_MODE_BASIC;
            ch->l2cap = l2cap;

            /* Does it feel like opening yet another channel though?  */
            if (!psm_info->new_channel(l2cap->dev, &ch->params)) {
                l2cap->cid[cid] = ch;

                result = L2CAP_CR_SUCCESS;
                status = L2CAP_CS_NO_INFO;
            } else {
                qemu_free(ch);

                result = L2CAP_CR_NO_MEM;
                status = L2CAP_CS_NO_INFO;
            }
        } else {
            result = L2CAP_CR_BAD_PSM;
            status = L2CAP_CS_NO_INFO;
        }
    } else {
        result = L2CAP_CR_NO_MEM;
        status = L2CAP_CS_NO_INFO;
    }

    l2cap_connection_response(l2cap, cid, source_cid, result, status);

    return ch;
}

static void l2cap_channel_close(struct l2cap_instance_s *l2cap,
                int cid, int source_cid)
{
    struct l2cap_chan_s *ch = 0;

    /* According to Volume 3, section 6.1.1, pg 1048 of BT Core V2.0, a
     * connection in CLOSED state still responds with a L2CAP_DisconnectRsp
     * message on an L2CAP_DisconnectReq event.  */
    if (unlikely(cid < L2CAP_CID_ALLOC)) {
        l2cap_command_reject_cid(l2cap, l2cap->last_id, L2CAP_REJ_CID_INVAL,
                        cid, source_cid);
        return;
    }
    if (likely(cid >= L2CAP_CID_ALLOC && cid < L2CAP_CID_MAX))
        ch = l2cap->cid[cid];

    if (likely(ch)) {
        if (ch->remote_cid != source_cid) {
            fprintf(stderr, "%s: Ignoring a Disconnection Request with the "
                            "invalid SCID %04x.\n", __FUNCTION__, source_cid);
            return;
        }

        l2cap->cid[cid] = 0;

        ch->params.close(ch->params.opaque);
        qemu_free(ch);
    }

    l2cap_disconnection_response(l2cap, cid, source_cid);
}

static void l2cap_channel_config_null(struct l2cap_instance_s *l2cap,
                struct l2cap_chan_s *ch)
{
    l2cap_configuration_request(l2cap, ch->remote_cid, 0, 0, 0);
    ch->config_req_id = l2cap->last_id;
    ch->config &= ~L2CAP_CFG_INIT;
}

static void l2cap_channel_config_req_event(struct l2cap_instance_s *l2cap,
                struct l2cap_chan_s *ch)
{
    /* Use all default channel options and terminate negotiation.  */
    l2cap_channel_config_null(l2cap, ch);
}

static int l2cap_channel_config(struct l2cap_instance_s *l2cap,
                struct l2cap_chan_s *ch, int flag,
                const uint8_t *data, int len)
{
    l2cap_conf_opt *opt;
    l2cap_conf_opt_qos *qos;
    uint32_t val;
    uint8_t rsp[len];
    int result = L2CAP_CONF_SUCCESS;

    data = memcpy(rsp, data, len);
    while (len) {
        opt = (void *) data;

        if (len < L2CAP_CONF_OPT_SIZE ||
                        len < L2CAP_CONF_OPT_SIZE + opt->len) {
            result = L2CAP_CONF_REJECT;
            break;
        }
        data += L2CAP_CONF_OPT_SIZE + opt->len;
        len -= L2CAP_CONF_OPT_SIZE + opt->len;

        switch (opt->type & 0x7f) {
        case L2CAP_CONF_MTU:
            if (opt->len != 2) {
                result = L2CAP_CONF_REJECT;
                break;
            }

            /* MTU */
            val = le16_to_cpup((void *) opt->val);
            if (val < ch->min_mtu) {
                cpu_to_le16w((void *) opt->val, ch->min_mtu);
                result = L2CAP_CONF_UNACCEPT;
                break;
            }

            ch->params.remote_mtu = val;
            break;

        case L2CAP_CONF_FLUSH_TO:
            if (opt->len != 2) {
                result = L2CAP_CONF_REJECT;
                break;
            }

            /* Flush Timeout */
            val = le16_to_cpup((void *) opt->val);
            if (val < 0x0001) {
                opt->val[0] = 0xff;
                opt->val[1] = 0xff;
                result = L2CAP_CONF_UNACCEPT;
                break;
            }
            break;

        case L2CAP_CONF_QOS:
            if (opt->len != L2CAP_CONF_OPT_QOS_SIZE) {
                result = L2CAP_CONF_REJECT;
                break;
            }
            qos = (void *) opt->val;

            /* Flags */
            val = qos->flags;
            if (val) {
                qos->flags = 0;
                result = L2CAP_CONF_UNACCEPT;
            }

            /* Service type */
            val = qos->service_type;
            if (val != L2CAP_CONF_QOS_BEST_EFFORT &&
                            val != L2CAP_CONF_QOS_NO_TRAFFIC) {
                qos->service_type = L2CAP_CONF_QOS_BEST_EFFORT;
                result = L2CAP_CONF_UNACCEPT;
            }

            if (val != L2CAP_CONF_QOS_NO_TRAFFIC) {
                /* XXX: These values should possibly be calculated
                 * based on LM / baseband properties also.  */

                /* Token rate */
                val = le32_to_cpu(qos->token_rate);
                if (val == L2CAP_CONF_QOS_WILDCARD)
                    qos->token_rate = cpu_to_le32(0x100000);

                /* Token bucket size */
                val = le32_to_cpu(qos->token_bucket_size);
                if (val == L2CAP_CONF_QOS_WILDCARD)
                    qos->token_bucket_size = cpu_to_le32(65500);

                /* Any Peak bandwidth value is correct to return as-is */
                /* Any Access latency value is correct to return as-is */
                /* Any Delay variation value is correct to return as-is */
            }
            break;

        case L2CAP_CONF_RFC:
            if (opt->len != 9) {
                result = L2CAP_CONF_REJECT;
                break;
            }

            /* Mode */
            val = opt->val[0];
            switch (val) {
            case L2CAP_MODE_BASIC:
                ch->mode = val;
                ch->frame_in = l2cap_bframe_in;

                /* All other parameters shall be ignored */
                break;

            case L2CAP_MODE_RETRANS:
            case L2CAP_MODE_FLOWCTL:
                ch->mode = val;
                ch->frame_in = l2cap_iframe_in;
                /* Note: most of these parameters refer to incoming traffic
                 * so we don't need to save them as long as we can accept
                 * incoming PDUs at any values of the parameters.  */

                /* TxWindow size */
                val = opt->val[1];
                if (val < 1 || val > 32) {
                    opt->val[1] = 32;
                    result = L2CAP_CONF_UNACCEPT;
                    break;
                }

                /* MaxTransmit */
                val = opt->val[2];
                if (val < 1) {
                    opt->val[2] = 1;
                    result = L2CAP_CONF_UNACCEPT;
                    break;
                }

                /* Remote Retransmission time-out shouldn't affect local
                 * operation (?) */

                /* The Monitor time-out drives the local Monitor timer (?),
                 * so save the value.  */
                val = (opt->val[6] << 8) | opt->val[5];
                if (val < 30) {
                    opt->val[5] = 100 & 0xff;
                    opt->val[6] = 100 >> 8;
                    result = L2CAP_CONF_UNACCEPT;
                    break;
                }
                ch->monitor_timeout = val;
                l2cap_monitor_timer_update(ch);

                /* MPS */
                val = (opt->val[8] << 8) | opt->val[7];
                if (val < ch->min_mtu) {
                    opt->val[7] = ch->min_mtu & 0xff;
                    opt->val[8] = ch->min_mtu >> 8;
                    result = L2CAP_CONF_UNACCEPT;
                    break;
                }
                ch->mps = val;
                break;

            default:
                result = L2CAP_CONF_UNACCEPT;
                break;
            }
            break;

        default:
            if (!(opt->type >> 7))
                result = L2CAP_CONF_UNKNOWN;
            break;
        }

        if (result != L2CAP_CONF_SUCCESS)
            break;	/* XXX: should continue? */
    }

    l2cap_configuration_response(l2cap, ch->remote_cid,
                    flag, result, rsp, len);

    return result == L2CAP_CONF_SUCCESS && !flag;
}

static void l2cap_channel_config_req_msg(struct l2cap_instance_s *l2cap,
                int flag, int cid, const uint8_t *data, int len)
{
    struct l2cap_chan_s *ch;

    if (unlikely(cid >= L2CAP_CID_MAX || !l2cap->cid[cid])) {
        l2cap_command_reject_cid(l2cap, l2cap->last_id, L2CAP_REJ_CID_INVAL,
                        cid, 0x0000);
        return;
    }
    ch = l2cap->cid[cid];

    /* From OPEN go to WAIT_CONFIG_REQ and from WAIT_CONFIG_REQ_RSP to
     * WAIT_CONFIG_REQ_RSP.  This is assuming the transition chart for OPEN
     * on pg 1053, section 6.1.5, volume 3 of BT Core V2.0 has a mistake
     * and on options-acceptable we go back to OPEN and otherwise to
     * WAIT_CONFIG_REQ and not the other way.  */
    ch->config &= ~L2CAP_CFG_ACC;

    if (l2cap_channel_config(l2cap, ch, flag, data, len))
        /* Go to OPEN or WAIT_CONFIG_RSP */
        ch->config |= L2CAP_CFG_ACC;

    /* TODO: if the incoming traffic flow control or retransmission mode
     * changed then we probably need to also generate the
     * ConfigureChannel_Req event and set the outgoing traffic to the same
     * mode.  */
    if (!(ch->config & L2CAP_CFG_INIT) && (ch->config & L2CAP_CFG_ACC) &&
                    !ch->config_req_id)
        l2cap_channel_config_req_event(l2cap, ch);
}

static int l2cap_channel_config_rsp_msg(struct l2cap_instance_s *l2cap,
                int result, int flag, int cid, const uint8_t *data, int len)
{
    struct l2cap_chan_s *ch;

    if (unlikely(cid >= L2CAP_CID_MAX || !l2cap->cid[cid])) {
        l2cap_command_reject_cid(l2cap, l2cap->last_id, L2CAP_REJ_CID_INVAL,
                        cid, 0x0000);
        return 0;
    }
    ch = l2cap->cid[cid];

    if (ch->config_req_id != l2cap->last_id)
        return 1;
    ch->config_req_id = 0;

    if (result == L2CAP_CONF_SUCCESS) {
        if (!flag)
            ch->config |= L2CAP_CFG_INIT;
        else
            l2cap_channel_config_null(l2cap, ch);
    } else
        /* Retry until we succeed */
        l2cap_channel_config_req_event(l2cap, ch);

    return 0;
}

static void l2cap_channel_open_req_msg(struct l2cap_instance_s *l2cap,
                int psm, int source_cid)
{
    struct l2cap_chan_s *ch = l2cap_channel_open(l2cap, psm, source_cid);

    if (!ch)
        return;

    /* Optional */
    if (!(ch->config & L2CAP_CFG_INIT) && !ch->config_req_id)
        l2cap_channel_config_req_event(l2cap, ch);
}

static void l2cap_info(struct l2cap_instance_s *l2cap, int type)
{
    uint8_t data[4];
    int len = 0;
    int result = L2CAP_IR_SUCCESS;

    switch (type) {
    case L2CAP_IT_CL_MTU:
        data[len ++] = l2cap->group_ch.mps & 0xff;
        data[len ++] = l2cap->group_ch.mps >> 8;
        break;

    case L2CAP_IT_FEAT_MASK:
        /* (Prematurely) report Flow control and Retransmission modes.  */
        data[len ++] = 0x03;
        data[len ++] = 0x00;
        data[len ++] = 0x00;
        data[len ++] = 0x00;
        break;

    default:
        result = L2CAP_IR_NOTSUPP;
    }

    l2cap_info_response(l2cap, type, result, data, len);
}

static void l2cap_command(struct l2cap_instance_s *l2cap, int code, int id,
                const uint8_t *params, int len)
{
    int err;

#if 0
    /* TODO: do the IDs really have to be in sequence?  */
    if (!id || (id != l2cap->last_id && id != l2cap->next_id)) {
        fprintf(stderr, "%s: out of sequence command packet ignored.\n",
                        __FUNCTION__);
        return;
    }
#else
    l2cap->next_id = id;
#endif
    if (id == l2cap->next_id) {
        l2cap->last_id = l2cap->next_id;
        l2cap->next_id = l2cap->next_id == 255 ? 1 : l2cap->next_id + 1;
    } else {
        /* TODO: Need to re-send the same response, without re-executing
         * the corresponding command!  */
    }

    switch (code) {
    case L2CAP_COMMAND_REJ:
        if (unlikely(len != 2 && len != 4 && len != 6)) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        /* We never issue commands other than Command Reject currently.  */
        fprintf(stderr, "%s: stray Command Reject (%02x, %04x) "
                        "packet, ignoring.\n", __FUNCTION__, id,
                        le16_to_cpu(((l2cap_cmd_rej *) params)->reason));
        break;

    case L2CAP_CONN_REQ:
        if (unlikely(len != L2CAP_CONN_REQ_SIZE)) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        l2cap_channel_open_req_msg(l2cap,
                        le16_to_cpu(((l2cap_conn_req *) params)->psm),
                        le16_to_cpu(((l2cap_conn_req *) params)->scid));
        break;

    case L2CAP_CONN_RSP:
        if (unlikely(len != L2CAP_CONN_RSP_SIZE)) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        /* We never issue Connection Requests currently. TODO  */
        fprintf(stderr, "%s: unexpected Connection Response (%02x) "
                        "packet, ignoring.\n", __FUNCTION__, id);
        break;

    case L2CAP_CONF_REQ:
        if (unlikely(len < L2CAP_CONF_REQ_SIZE(0))) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        l2cap_channel_config_req_msg(l2cap,
                        le16_to_cpu(((l2cap_conf_req *) params)->flags) & 1,
                        le16_to_cpu(((l2cap_conf_req *) params)->dcid),
                        ((l2cap_conf_req *) params)->data,
                        len - L2CAP_CONF_REQ_SIZE(0));
        break;

    case L2CAP_CONF_RSP:
        if (unlikely(len < L2CAP_CONF_RSP_SIZE(0))) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        if (l2cap_channel_config_rsp_msg(l2cap,
                        le16_to_cpu(((l2cap_conf_rsp *) params)->result),
                        le16_to_cpu(((l2cap_conf_rsp *) params)->flags) & 1,
                        le16_to_cpu(((l2cap_conf_rsp *) params)->scid),
                        ((l2cap_conf_rsp *) params)->data,
                        len - L2CAP_CONF_RSP_SIZE(0)))
            fprintf(stderr, "%s: unexpected Configure Response (%02x) "
                            "packet, ignoring.\n", __FUNCTION__, id);
        break;

    case L2CAP_DISCONN_REQ:
        if (unlikely(len != L2CAP_DISCONN_REQ_SIZE)) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        l2cap_channel_close(l2cap,
                        le16_to_cpu(((l2cap_disconn_req *) params)->dcid),
                        le16_to_cpu(((l2cap_disconn_req *) params)->scid));
        break;

    case L2CAP_DISCONN_RSP:
        if (unlikely(len != L2CAP_DISCONN_RSP_SIZE)) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        /* We never issue Disconnection Requests currently. TODO  */
        fprintf(stderr, "%s: unexpected Disconnection Response (%02x) "
                        "packet, ignoring.\n", __FUNCTION__, id);
        break;

    case L2CAP_ECHO_REQ:
        l2cap_echo_response(l2cap, params, len);
        break;

    case L2CAP_ECHO_RSP:
        /* We never issue Echo Requests currently. TODO  */
        fprintf(stderr, "%s: unexpected Echo Response (%02x) "
                        "packet, ignoring.\n", __FUNCTION__, id);
        break;

    case L2CAP_INFO_REQ:
        if (unlikely(len != L2CAP_INFO_REQ_SIZE)) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        l2cap_info(l2cap, le16_to_cpu(((l2cap_info_req *) params)->type));
        break;

    case L2CAP_INFO_RSP:
        if (unlikely(len != L2CAP_INFO_RSP_SIZE)) {
            err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
            goto reject;
        }

        /* We never issue Information Requests currently. TODO  */
        fprintf(stderr, "%s: unexpected Information Response (%02x) "
                        "packet, ignoring.\n", __FUNCTION__, id);
        break;

    default:
        err = L2CAP_REJ_CMD_NOT_UNDERSTOOD;
    reject:
        l2cap_command_reject(l2cap, id, err, 0, 0);
        break;
    }
}

static void l2cap_rexmit_enable(struct l2cap_chan_s *ch, int enable)
{
    ch->rexmit = enable;

    l2cap_retransmission_timer_update(ch);
    l2cap_monitor_timer_update(ch);
}

/* Command frame SDU */
static void l2cap_cframe_in(void *opaque, const uint8_t *data, int len)
{
    struct l2cap_instance_s *l2cap = opaque;
    const l2cap_cmd_hdr *hdr;
    int clen;

    while (len) {
        hdr = (void *) data;
        if (len < L2CAP_CMD_HDR_SIZE)
            /* TODO: signal an error */
            return;
        len -= L2CAP_CMD_HDR_SIZE;
        data += L2CAP_CMD_HDR_SIZE;

        clen = le16_to_cpu(hdr->len);
        if (len < clen) {
            l2cap_command_reject(l2cap, hdr->ident,
                            L2CAP_REJ_CMD_NOT_UNDERSTOOD, 0, 0);
            break;
        }

        l2cap_command(l2cap, hdr->code, hdr->ident, data, clen);
        len -= clen;
        data += clen;
    }
}

/* Group frame SDU */
static void l2cap_gframe_in(void *opaque, const uint8_t *data, int len)
{
}

/* Supervisory frame */
static void l2cap_sframe_in(struct l2cap_chan_s *ch, uint16_t ctrl)
{
}

/* Basic L2CAP mode Information frame */
static void l2cap_bframe_in(struct l2cap_chan_s *ch, uint16_t cid,
                const l2cap_hdr *hdr, int len)
{
    /* We have a full SDU, no further processing */
    ch->params.sdu_in(ch->params.opaque, hdr->data, len);
}

/* Flow Control and Retransmission mode frame */
static void l2cap_iframe_in(struct l2cap_chan_s *ch, uint16_t cid,
                const l2cap_hdr *hdr, int len)
{
    uint16_t fcs = le16_to_cpup((void *) (hdr->data + len - 2));

    if (len < 4)
        goto len_error;
    if (l2cap_fcs16((const uint8_t *) hdr, L2CAP_HDR_SIZE + len - 2) != fcs)
        goto fcs_error;

    if ((hdr->data[0] >> 7) == ch->rexmit)
        l2cap_rexmit_enable(ch, !(hdr->data[0] >> 7));

    if (hdr->data[0] & 1) {
        if (len != 4)
            /* TODO: Signal an error? */;
            return;

        return l2cap_sframe_in(ch, le16_to_cpup((void *) hdr->data));
    }

    switch (hdr->data[1] >> 6) {	/* SAR */
    case L2CAP_SAR_NO_SEG:
        if (ch->len_total)
            goto seg_error;
        if (len - 4 > ch->mps)
            goto len_error;

        return ch->params.sdu_in(ch->params.opaque, hdr->data + 2, len - 4);

    case L2CAP_SAR_START:
        if (ch->len_total || len < 6)
            goto seg_error;
        if (len - 6 > ch->mps)
            goto len_error;

        ch->len_total = le16_to_cpup((void *) (hdr->data + 2));
        if (len >= 6 + ch->len_total)
            goto seg_error;

        ch->len_cur = len - 6;
        memcpy(ch->sdu, hdr->data + 4, ch->len_cur);
        break;

    case L2CAP_SAR_END:
        if (!ch->len_total || ch->len_cur + len - 4 < ch->len_total)
            goto seg_error;
        if (len - 4 > ch->mps)
            goto len_error;

        memcpy(ch->sdu + ch->len_cur, hdr->data + 2, len - 4);
        return ch->params.sdu_in(ch->params.opaque, ch->sdu, ch->len_total);

    case L2CAP_SAR_CONT:
        if (!ch->len_total || ch->len_cur + len - 4 >= ch->len_total)
            goto seg_error;
        if (len - 4 > ch->mps)
            goto len_error;

        memcpy(ch->sdu + ch->len_cur, hdr->data + 2, len - 4);
        ch->len_cur += len - 4;
        break;

    seg_error:
    len_error:	/* TODO */
    fcs_error:	/* TODO */
        ch->len_cur = 0;
        ch->len_total = 0;
        break;
    }
}

static void l2cap_frame_in(struct l2cap_instance_s *l2cap,
                const l2cap_hdr *frame)
{
    uint16_t cid = le16_to_cpu(frame->cid);
    uint16_t len = le16_to_cpu(frame->len);

    if (unlikely(cid >= L2CAP_CID_MAX || !l2cap->cid[cid])) {
        fprintf(stderr, "%s: frame addressed to a non-existent L2CAP "
                        "channel %04x received.\n", __FUNCTION__, cid);
        return;
    }

    l2cap->cid[cid]->frame_in(l2cap->cid[cid], cid, frame, len);
}

/* "Recombination" */
static void l2cap_pdu_in(struct l2cap_instance_s *l2cap,
                const uint8_t *data, int len)
{
    const l2cap_hdr *hdr = (void *) l2cap->frame_in;

    if (unlikely(len + l2cap->frame_in_len > sizeof(l2cap->frame_in))) {
        if (l2cap->frame_in_len < sizeof(l2cap->frame_in)) {
            memcpy(l2cap->frame_in + l2cap->frame_in_len, data,
                            sizeof(l2cap->frame_in) - l2cap->frame_in_len);
            l2cap->frame_in_len = sizeof(l2cap->frame_in);
            /* TODO: truncate */
            l2cap_frame_in(l2cap, hdr);
        }

        return;
    }

    memcpy(l2cap->frame_in + l2cap->frame_in_len, data, len);
    l2cap->frame_in_len += len;

    if (len >= L2CAP_HDR_SIZE)
        if (len >= L2CAP_HDR_SIZE + le16_to_cpu(hdr->len))
            l2cap_frame_in(l2cap, hdr);
            /* There is never a start of a new PDU in the same ACL packet, so
             * no need to memmove the remaining payload and loop.  */
}

static inline uint8_t *l2cap_pdu_out(struct l2cap_instance_s *l2cap,
                uint16_t cid, uint16_t len)
{
    l2cap_hdr *hdr = (void *) l2cap->frame_out;

    l2cap->frame_out_len = len + L2CAP_HDR_SIZE;

    hdr->cid = cpu_to_le16(cid);
    hdr->len = cpu_to_le16(len);

    return l2cap->frame_out + L2CAP_HDR_SIZE;
}

static inline void l2cap_pdu_submit(struct l2cap_instance_s *l2cap)
{
    /* TODO: Fragmentation */
    (l2cap->role ?
     l2cap->link->slave->lmp_acl_data : l2cap->link->host->lmp_acl_resp)
            (l2cap->link, l2cap->frame_out, 1, l2cap->frame_out_len);
}

static uint8_t *l2cap_bframe_out(struct bt_l2cap_conn_params_s *parm, int len)
{
    struct l2cap_chan_s *chan = (struct l2cap_chan_s *) parm;

    if (len > chan->params.remote_mtu) {
        fprintf(stderr, "%s: B-Frame for CID %04x longer than %i octets.\n",
                        __FUNCTION__,
                        chan->remote_cid, chan->params.remote_mtu);
        exit(-1);
    }

    return l2cap_pdu_out(chan->l2cap, chan->remote_cid, len);
}

static void l2cap_bframe_submit(struct bt_l2cap_conn_params_s *parms)
{
    struct l2cap_chan_s *chan = (struct l2cap_chan_s *) parms;

    return l2cap_pdu_submit(chan->l2cap);
}

#if 0
/* Stub: Only used if an emulated device requests outgoing flow control */
static uint8_t *l2cap_iframe_out(struct bt_l2cap_conn_params_s *parm, int len)
{
    struct l2cap_chan_s *chan = (struct l2cap_chan_s *) parm;

    if (len > chan->params.remote_mtu) {
        /* TODO: slice into segments and queue each segment as a separate
         * I-Frame in a FIFO of I-Frames, local to the CID.  */
    } else {
        /* TODO: add to the FIFO of I-Frames, local to the CID.  */
        /* Possibly we need to return a pointer to a contiguous buffer
         * for now and then memcpy from it into FIFOs in l2cap_iframe_submit
         * while segmenting at the same time.  */
    }
    return 0;
}

static void l2cap_iframe_submit(struct bt_l2cap_conn_params_s *parm)
{
    /* TODO: If flow control indicates clear to send, start submitting the
     * invidual I-Frames from the FIFO, but don't remove them from there.
     * Kick the appropriate timer until we get an S-Frame, and only then
     * remove from FIFO or resubmit and re-kick the timer if the timer
     * expired.  */
}
#endif

static void l2cap_init(struct l2cap_instance_s *l2cap,
                struct bt_link_s *link, int role)
{
    l2cap->link = link;
    l2cap->role = role;
    l2cap->dev = (struct bt_l2cap_device_s *)
            (role ? link->host : link->slave);

    l2cap->next_id = 1;

    /* Establish the signalling channel */
    l2cap->signalling_ch.params.sdu_in = l2cap_cframe_in;
    l2cap->signalling_ch.params.sdu_out = l2cap_bframe_out;
    l2cap->signalling_ch.params.sdu_submit = l2cap_bframe_submit;
    l2cap->signalling_ch.params.opaque = l2cap;
    l2cap->signalling_ch.params.remote_mtu = 48;
    l2cap->signalling_ch.remote_cid = L2CAP_CID_SIGNALLING;
    l2cap->signalling_ch.frame_in = l2cap_bframe_in;
    l2cap->signalling_ch.mps = 65536;
    l2cap->signalling_ch.min_mtu = 48;
    l2cap->signalling_ch.mode = L2CAP_MODE_BASIC;
    l2cap->signalling_ch.l2cap = l2cap;
    l2cap->cid[L2CAP_CID_SIGNALLING] = &l2cap->signalling_ch;

    /* Establish the connection-less data channel */
    l2cap->group_ch.params.sdu_in = l2cap_gframe_in;
    l2cap->group_ch.params.opaque = l2cap;
    l2cap->group_ch.frame_in = l2cap_bframe_in;
    l2cap->group_ch.mps = 65533;
    l2cap->group_ch.l2cap = l2cap;
    l2cap->group_ch.remote_cid = L2CAP_CID_INVALID;
    l2cap->cid[L2CAP_CID_GROUP] = &l2cap->group_ch;
}

static void l2cap_teardown(struct l2cap_instance_s *l2cap, int send_disconnect)
{
    int cid;

    /* Don't send DISCONNECT if we are currently handling a DISCONNECT
     * sent from the other side.  */
    if (send_disconnect) {
        if (l2cap->role)
            l2cap->dev->device.lmp_disconnect_slave(l2cap->link);
            /* l2cap->link is invalid from now on.  */
        else
            l2cap->dev->device.lmp_disconnect_master(l2cap->link);
    }

    for (cid = L2CAP_CID_ALLOC; cid < L2CAP_CID_MAX; cid ++)
        if (l2cap->cid[cid]) {
            l2cap->cid[cid]->params.close(l2cap->cid[cid]->params.opaque);
            free(l2cap->cid[cid]);
        }

    if (l2cap->role)
        qemu_free(l2cap);
    else
        qemu_free(l2cap->link);
}

/* L2CAP glue to lower layers in bluetooth stack (LMP) */

static void l2cap_lmp_connection_request(struct bt_link_s *link)
{
    struct bt_l2cap_device_s *dev = (struct bt_l2cap_device_s *) link->slave;
    struct slave_l2cap_instance_s *l2cap;

    /* Always accept - we only get called if (dev->device->page_scan).  */

    l2cap = qemu_mallocz(sizeof(struct slave_l2cap_instance_s));
    l2cap->link.slave = &dev->device;
    l2cap->link.host = link->host;
    l2cap_init(&l2cap->l2cap, &l2cap->link, 0);

    /* Always at the end */
    link->host->reject_reason = 0;
    link->host->lmp_connection_complete(&l2cap->link);
}

/* Stub */
static void l2cap_lmp_connection_complete(struct bt_link_s *link)
{
    struct bt_l2cap_device_s *dev = (struct bt_l2cap_device_s *) link->host;
    struct l2cap_instance_s *l2cap;

    if (dev->device.reject_reason) {
        /* Signal to upper layer */
        return;
    }

    l2cap = qemu_mallocz(sizeof(struct l2cap_instance_s));
    l2cap_init(l2cap, link, 1);

    link->acl_mode = acl_active;

    /* Signal to upper layer */
}

/* Stub */
static void l2cap_lmp_disconnect_host(struct bt_link_s *link)
{
    struct bt_l2cap_device_s *dev = (struct bt_l2cap_device_s *) link->host;
    struct l2cap_instance_s *l2cap =
            /* TODO: Retrieve from upper layer */ (void *) dev;

    /* Signal to upper layer */

    l2cap_teardown(l2cap, 0);
}

static void l2cap_lmp_disconnect_slave(struct bt_link_s *link)
{
    struct slave_l2cap_instance_s *l2cap =
            (struct slave_l2cap_instance_s *) link;

    l2cap_teardown(&l2cap->l2cap, 0);
}

static void l2cap_lmp_acl_data_slave(struct bt_link_s *link,
                const uint8_t *data, int start, int len)
{
    struct slave_l2cap_instance_s *l2cap =
            (struct slave_l2cap_instance_s *) link;

    if (start)
        l2cap->l2cap.frame_in_len = 0;

    l2cap_pdu_in(&l2cap->l2cap, data, len);
}

/* Stub */
static void l2cap_lmp_acl_data_host(struct bt_link_s *link,
                const uint8_t *data, int start, int len)
{
    struct bt_l2cap_device_s *dev = (struct bt_l2cap_device_s *) link->host;
    struct l2cap_instance_s *l2cap =
            /* TODO: Retrieve from upper layer */ (void *) dev;

    if (start)
        l2cap->frame_in_len = 0;

    l2cap_pdu_in(l2cap, data, len);
}

static void l2cap_dummy_destroy(struct bt_device_s *dev)
{
    struct bt_l2cap_device_s *l2cap_dev = (struct bt_l2cap_device_s *) dev;

    bt_l2cap_device_done(l2cap_dev);
}

void bt_l2cap_device_init(struct bt_l2cap_device_s *dev,
                struct bt_scatternet_s *net)
{
    bt_device_init(&dev->device, net);

    dev->device.lmp_connection_request = l2cap_lmp_connection_request;
    dev->device.lmp_connection_complete = l2cap_lmp_connection_complete;
    dev->device.lmp_disconnect_master = l2cap_lmp_disconnect_host;
    dev->device.lmp_disconnect_slave = l2cap_lmp_disconnect_slave;
    dev->device.lmp_acl_data = l2cap_lmp_acl_data_slave;
    dev->device.lmp_acl_resp = l2cap_lmp_acl_data_host;

    dev->device.handle_destroy = l2cap_dummy_destroy;
}

void bt_l2cap_device_done(struct bt_l2cap_device_s *dev)
{
    bt_device_done(&dev->device);

    /* Should keep a list of all instances and go through it and
     * invoke l2cap_teardown() for each.  */
}

void bt_l2cap_psm_register(struct bt_l2cap_device_s *dev, int psm, int min_mtu,
                int (*new_channel)(struct bt_l2cap_device_s *dev,
                        struct bt_l2cap_conn_params_s *params))
{
    struct bt_l2cap_psm_s *new_psm = l2cap_psm(dev, psm);

    if (new_psm) {
        fprintf(stderr, "%s: PSM %04x already registered for device `%s'.\n",
                        __FUNCTION__, psm, dev->device.lmp_name);
        exit(-1);
    }

    new_psm = qemu_mallocz(sizeof(*new_psm));
    new_psm->psm = psm;
    new_psm->min_mtu = min_mtu;
    new_psm->new_channel = new_channel;
    new_psm->next = dev->first_psm;
    dev->first_psm = new_psm;
}
