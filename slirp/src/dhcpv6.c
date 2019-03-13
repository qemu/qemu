/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * SLIRP stateless DHCPv6
 *
 * We only support stateless DHCPv6, e.g. for network booting.
 * See RFC 3315, RFC 3736, RFC 3646 and RFC 5970 for details.
 *
 * Copyright 2016 Thomas Huth, Red Hat Inc.
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
#include "dhcpv6.h"

/* DHCPv6 message types */
#define MSGTYPE_REPLY        7
#define MSGTYPE_INFO_REQUEST 11

/* DHCPv6 option types */
#define OPTION_CLIENTID      1
#define OPTION_IAADDR        5
#define OPTION_ORO           6
#define OPTION_DNS_SERVERS   23
#define OPTION_BOOTFILE_URL  59

struct requested_infos {
    uint8_t *client_id;
    int client_id_len;
    bool want_dns;
    bool want_boot_url;
};

/**
 * Analyze the info request message sent by the client to see what data it
 * provided and what it wants to have. The information is gathered in the
 * "requested_infos" struct. Note that client_id (if provided) points into
 * the odata region, thus the caller must keep odata valid as long as it
 * needs to access the requested_infos struct.
 */
static int dhcpv6_parse_info_request(Slirp *slirp, uint8_t *odata, int olen,
                                     struct requested_infos *ri)
{
    int i, req_opt;

    while (olen > 4) {
        /* Parse one option */
        int option = odata[0] << 8 | odata[1];
        int len = odata[2] << 8 | odata[3];

        if (len + 4 > olen) {
            slirp->cb->guest_error("Guest sent bad DHCPv6 packet!", slirp->opaque);
            return -E2BIG;
        }

        switch (option) {
        case OPTION_IAADDR:
            /* According to RFC3315, we must discard requests with IA option */
            return -EINVAL;
        case OPTION_CLIENTID:
            if (len > 256) {
                /* Avoid very long IDs which could cause problems later */
                return -E2BIG;
            }
            ri->client_id = odata + 4;
            ri->client_id_len = len;
            break;
        case OPTION_ORO:        /* Option request option */
            if (len & 1) {
                return -EINVAL;
            }
            /* Check which options the client wants to have */
            for (i = 0; i < len; i += 2) {
                req_opt = odata[4 + i] << 8 | odata[4 + i + 1];
                switch (req_opt) {
                case OPTION_DNS_SERVERS:
                    ri->want_dns = true;
                    break;
                case OPTION_BOOTFILE_URL:
                    ri->want_boot_url = true;
                    break;
                default:
                    DEBUG_MISC("dhcpv6: Unsupported option request %d",
                               req_opt);
                }
            }
            break;
        default:
            DEBUG_MISC("dhcpv6 info req: Unsupported option %d, len=%d",
                       option, len);
        }

        odata += len + 4;
        olen -= len + 4;
    }

    return 0;
}


/**
 * Handle information request messages
 */
static void dhcpv6_info_request(Slirp *slirp, struct sockaddr_in6 *srcsas,
                                uint32_t xid, uint8_t *odata, int olen)
{
    struct requested_infos ri = { NULL };
    struct sockaddr_in6 sa6, da6;
    struct mbuf *m;
    uint8_t *resp;

    if (dhcpv6_parse_info_request(slirp, odata, olen, &ri) < 0) {
        return;
    }

    m = m_get(slirp);
    if (!m) {
        return;
    }
    memset(m->m_data, 0, m->m_size);
    m->m_data += IF_MAXLINKHDR;
    resp = (uint8_t *)m->m_data + sizeof(struct ip6) + sizeof(struct udphdr);

    /* Fill in response */
    *resp++ = MSGTYPE_REPLY;
    *resp++ = (uint8_t)(xid >> 16);
    *resp++ = (uint8_t)(xid >> 8);
    *resp++ = (uint8_t)xid;

    if (ri.client_id) {
        *resp++ = OPTION_CLIENTID >> 8;         /* option-code high byte */
        *resp++ = OPTION_CLIENTID;              /* option-code low byte */
        *resp++ = ri.client_id_len >> 8;        /* option-len high byte */
        *resp++ = ri.client_id_len;             /* option-len low byte */
        memcpy(resp, ri.client_id, ri.client_id_len);
        resp += ri.client_id_len;
    }
    if (ri.want_dns) {
        *resp++ = OPTION_DNS_SERVERS >> 8;      /* option-code high byte */
        *resp++ = OPTION_DNS_SERVERS;           /* option-code low byte */
        *resp++ = 0;                            /* option-len high byte */
        *resp++ = 16;                           /* option-len low byte */
        memcpy(resp, &slirp->vnameserver_addr6, 16);
        resp += 16;
    }
    if (ri.want_boot_url) {
        uint8_t *sa = slirp->vhost_addr6.s6_addr;
        int slen, smaxlen;

        *resp++ = OPTION_BOOTFILE_URL >> 8;     /* option-code high byte */
        *resp++ = OPTION_BOOTFILE_URL;          /* option-code low byte */
        smaxlen = (uint8_t *)m->m_data + IF_MTU - (resp + 2);
        slen = snprintf((char *)resp + 2, smaxlen,
                        "tftp://[%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                                "%02x%02x:%02x%02x:%02x%02x:%02x%02x]/%s",
                        sa[0], sa[1], sa[2], sa[3], sa[4], sa[5], sa[6], sa[7],
                        sa[8], sa[9], sa[10], sa[11], sa[12], sa[13], sa[14],
                        sa[15], slirp->bootp_filename);
        slen = MIN(slen, smaxlen);
        *resp++ = slen >> 8;                    /* option-len high byte */
        *resp++ = slen;                         /* option-len low byte */
        resp += slen;
    }

    sa6.sin6_addr = slirp->vhost_addr6;
    sa6.sin6_port = DHCPV6_SERVER_PORT;
    da6.sin6_addr = srcsas->sin6_addr;
    da6.sin6_port = srcsas->sin6_port;
    m->m_data += sizeof(struct ip6) + sizeof(struct udphdr);
    m->m_len = resp - (uint8_t *)m->m_data;
    udp6_output(NULL, m, &sa6, &da6);
}

/**
 * Handle DHCPv6 messages sent by the client
 */
void dhcpv6_input(struct sockaddr_in6 *srcsas, struct mbuf *m)
{
    uint8_t *data = (uint8_t *)m->m_data + sizeof(struct udphdr);
    int data_len = m->m_len - sizeof(struct udphdr);
    uint32_t xid;

    if (data_len < 4) {
        return;
    }

    xid = ntohl(*(uint32_t *)data) & 0xffffff;

    switch (data[0]) {
    case MSGTYPE_INFO_REQUEST:
        dhcpv6_info_request(m->slirp, srcsas, xid, &data[4], data_len - 4);
        break;
    default:
        DEBUG_MISC("dhcpv6_input: Unsupported message type 0x%x", data[0]);
    }
}
