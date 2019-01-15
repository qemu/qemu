/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_NET_UTIL_H
#define QEMU_NET_UTIL_H


/*
 * Structure of an internet header, naked of options.
 */
struct ip {
#ifdef HOST_WORDS_BIGENDIAN
    uint8_t ip_v:4,         /* version */
            ip_hl:4;        /* header length */
#else
    uint8_t ip_hl:4,        /* header length */
            ip_v:4;         /* version */
#endif
    uint8_t ip_tos;         /* type of service */
    uint16_t ip_len;        /* total length */
    uint16_t ip_id;         /* identification */
    uint16_t ip_off;        /* fragment offset field */
#define IP_DF 0x4000        /* don't fragment flag */
#define IP_MF 0x2000        /* more fragments flag */
#define IP_OFFMASK 0x1fff   /* mask for fragmenting bits */
    uint8_t ip_ttl;         /* time to live */
    uint8_t ip_p;           /* protocol */
    uint16_t ip_sum;        /* checksum */
    struct in_addr ip_src, ip_dst;  /* source and dest address */
} QEMU_PACKED;

static inline bool in6_equal_net(const struct in6_addr *a,
                                 const struct in6_addr *b,
                                 int prefix_len)
{
    if (memcmp(a, b, prefix_len / 8) != 0) {
        return 0;
    }

    if (prefix_len % 8 == 0) {
        return 1;
    }

    return a->s6_addr[prefix_len / 8] >> (8 - (prefix_len % 8))
        == b->s6_addr[prefix_len / 8] >> (8 - (prefix_len % 8));
}

#define TCPS_CLOSED             0       /* closed */
#define TCPS_LISTEN             1       /* listening for connection */
#define TCPS_SYN_SENT           2       /* active, have sent syn */
#define TCPS_SYN_RECEIVED       3       /* have send and received syn */
/* states < TCPS_ESTABLISHED are those where connections not established */
#define TCPS_ESTABLISHED        4       /* established */
#define TCPS_CLOSE_WAIT         5       /* rcvd fin, waiting for close */
/* states > TCPS_CLOSE_WAIT are those where user has closed */
#define TCPS_FIN_WAIT_1         6       /* have closed, sent fin */
#define TCPS_CLOSING            7       /* closed xchd FIN; await FIN ACK */
#define TCPS_LAST_ACK           8       /* had fin and close; await FIN ACK */
/* states > TCPS_CLOSE_WAIT && < TCPS_FIN_WAIT_2 await ACK of FIN */
#define TCPS_FIN_WAIT_2         9       /* have closed, fin is acked */
#define TCPS_TIME_WAIT          10      /* in 2*msl quiet wait after close */

int net_parse_macaddr(uint8_t *macaddr, const char *p);

#endif /* QEMU_NET_UTIL_H */
