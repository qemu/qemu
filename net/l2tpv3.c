/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2012-2014 Cisco Systems
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

#include "qemu/osdep.h"
#include <linux/ip.h>
#include <netdb.h>
#include "net/net.h"
#include "clients.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/memalign.h"

/* The buffer size needs to be investigated for optimum numbers and
 * optimum means of paging in on different systems. This size is
 * chosen to be sufficient to accommodate one packet with some headers
 */

#define BUFFER_ALIGN sysconf(_SC_PAGESIZE)
#define BUFFER_SIZE 16384
#define IOVSIZE 2
#define MAX_L2TPV3_MSGCNT 64
#define MAX_L2TPV3_IOVCNT (MAX_L2TPV3_MSGCNT * IOVSIZE)

/* Header set to 0x30000 signifies a data packet */

#define L2TPV3_DATA_PACKET 0x30000

/* IANA-assigned IP protocol ID for L2TPv3 */

#ifndef IPPROTO_L2TP
#define IPPROTO_L2TP 0x73
#endif

typedef struct NetL2TPV3State {
    NetClientState nc;
    int fd;

    /*
     * these are used for xmit - that happens packet a time
     * and for first sign of life packet (easier to parse that once)
     */

    uint8_t *header_buf;
    struct iovec *vec;

    /*
     * these are used for receive - try to "eat" up to 32 packets at a time
     */

    struct mmsghdr *msgvec;

    /*
     * peer address
     */

    struct sockaddr_storage *dgram_dst;
    uint32_t dst_size;

    /*
     * L2TPv3 parameters
     */

    uint64_t rx_cookie;
    uint64_t tx_cookie;
    uint32_t rx_session;
    uint32_t tx_session;
    uint32_t header_size;
    uint32_t counter;

    /*
    * DOS avoidance in error handling
    */

    bool header_mismatch;

    /*
     * Ring buffer handling
     */

    int queue_head;
    int queue_tail;
    int queue_depth;

    /*
     * Precomputed offsets
     */

    uint32_t offset;
    uint32_t cookie_offset;
    uint32_t counter_offset;
    uint32_t session_offset;

    /* Poll Control */

    bool read_poll;
    bool write_poll;

    /* Flags */

    bool ipv6;
    bool udp;
    bool has_counter;
    bool pin_counter;
    bool cookie;
    bool cookie_is_64;

} NetL2TPV3State;

static void net_l2tpv3_send(void *opaque);
static void l2tpv3_writable(void *opaque);

static void l2tpv3_update_fd_handler(NetL2TPV3State *s)
{
    qemu_set_fd_handler(s->fd,
                        s->read_poll ? net_l2tpv3_send : NULL,
                        s->write_poll ? l2tpv3_writable : NULL,
                        s);
}

static void l2tpv3_read_poll(NetL2TPV3State *s, bool enable)
{
    if (s->read_poll != enable) {
        s->read_poll = enable;
        l2tpv3_update_fd_handler(s);
    }
}

static void l2tpv3_write_poll(NetL2TPV3State *s, bool enable)
{
    if (s->write_poll != enable) {
        s->write_poll = enable;
        l2tpv3_update_fd_handler(s);
    }
}

static void l2tpv3_writable(void *opaque)
{
    NetL2TPV3State *s = opaque;
    l2tpv3_write_poll(s, false);
    qemu_flush_queued_packets(&s->nc);
}

static void l2tpv3_send_completed(NetClientState *nc, ssize_t len)
{
    NetL2TPV3State *s = DO_UPCAST(NetL2TPV3State, nc, nc);
    l2tpv3_read_poll(s, true);
}

static void l2tpv3_poll(NetClientState *nc, bool enable)
{
    NetL2TPV3State *s = DO_UPCAST(NetL2TPV3State, nc, nc);
    l2tpv3_write_poll(s, enable);
    l2tpv3_read_poll(s, enable);
}

static void l2tpv3_form_header(NetL2TPV3State *s)
{
    uint32_t *counter;

    if (s->udp) {
        stl_be_p((uint32_t *) s->header_buf, L2TPV3_DATA_PACKET);
    }
    stl_be_p(
            (uint32_t *) (s->header_buf + s->session_offset),
            s->tx_session
        );
    if (s->cookie) {
        if (s->cookie_is_64) {
            stq_be_p(
                (uint64_t *)(s->header_buf + s->cookie_offset),
                s->tx_cookie
            );
        } else {
            stl_be_p(
                (uint32_t *) (s->header_buf + s->cookie_offset),
                s->tx_cookie
            );
        }
    }
    if (s->has_counter) {
        counter = (uint32_t *)(s->header_buf + s->counter_offset);
        if (s->pin_counter) {
            *counter = 0;
        } else {
            stl_be_p(counter, ++s->counter);
        }
    }
}

static ssize_t net_l2tpv3_receive_dgram_iov(NetClientState *nc,
                    const struct iovec *iov,
                    int iovcnt)
{
    NetL2TPV3State *s = DO_UPCAST(NetL2TPV3State, nc, nc);

    struct msghdr message;
    int ret;

    if (iovcnt > MAX_L2TPV3_IOVCNT - 1) {
        error_report(
            "iovec too long %d > %d, change l2tpv3.h",
            iovcnt, MAX_L2TPV3_IOVCNT
        );
        return -1;
    }
    l2tpv3_form_header(s);
    memcpy(s->vec + 1, iov, iovcnt * sizeof(struct iovec));
    s->vec->iov_base = s->header_buf;
    s->vec->iov_len = s->offset;
    message.msg_name = s->dgram_dst;
    message.msg_namelen = s->dst_size;
    message.msg_iov = s->vec;
    message.msg_iovlen = iovcnt + 1;
    message.msg_control = NULL;
    message.msg_controllen = 0;
    message.msg_flags = 0;
    ret = RETRY_ON_EINTR(sendmsg(s->fd, &message, 0));
    if (ret > 0) {
        ret -= s->offset;
    } else if (ret == 0) {
        /* belt and braces - should not occur on DGRAM
        * we should get an error and never a 0 send
        */
        ret = iov_size(iov, iovcnt);
    } else {
        /* signal upper layer that socket buffer is full */
        ret = -errno;
        if (ret == -EAGAIN || ret == -ENOBUFS) {
            l2tpv3_write_poll(s, true);
            ret = 0;
        }
    }
    return ret;
}

static ssize_t net_l2tpv3_receive_dgram(NetClientState *nc,
                    const uint8_t *buf,
                    size_t size)
{
    NetL2TPV3State *s = DO_UPCAST(NetL2TPV3State, nc, nc);

    struct iovec *vec;
    struct msghdr message;
    ssize_t ret = 0;

    l2tpv3_form_header(s);
    vec = s->vec;
    vec->iov_base = s->header_buf;
    vec->iov_len = s->offset;
    vec++;
    vec->iov_base = (void *) buf;
    vec->iov_len = size;
    message.msg_name = s->dgram_dst;
    message.msg_namelen = s->dst_size;
    message.msg_iov = s->vec;
    message.msg_iovlen = 2;
    message.msg_control = NULL;
    message.msg_controllen = 0;
    message.msg_flags = 0;
    ret = RETRY_ON_EINTR(sendmsg(s->fd, &message, 0));
    if (ret > 0) {
        ret -= s->offset;
    } else if (ret == 0) {
        /* belt and braces - should not occur on DGRAM
        * we should get an error and never a 0 send
        */
        ret = size;
    } else {
        ret = -errno;
        if (ret == -EAGAIN || ret == -ENOBUFS) {
            /* signal upper layer that socket buffer is full */
            l2tpv3_write_poll(s, true);
            ret = 0;
        }
    }
    return ret;
}

static int l2tpv3_verify_header(NetL2TPV3State *s, uint8_t *buf)
{

    uint32_t *session;
    uint64_t cookie;

    if ((!s->udp) && (!s->ipv6)) {
        buf += sizeof(struct iphdr) /* fix for ipv4 raw */;
    }

    /* we do not do a strict check for "data" packets as per
    * the RFC spec because the pure IP spec does not have
    * that anyway.
    */

    if (s->cookie) {
        if (s->cookie_is_64) {
            cookie = ldq_be_p(buf + s->cookie_offset);
        } else {
            cookie = ldl_be_p(buf + s->cookie_offset) & 0xffffffffULL;
        }
        if (cookie != s->rx_cookie) {
            if (!s->header_mismatch) {
                error_report("unknown cookie id");
            }
            return -1;
        }
    }
    session = (uint32_t *) (buf + s->session_offset);
    if (ldl_be_p(session) != s->rx_session) {
        if (!s->header_mismatch) {
            error_report("session mismatch");
        }
        return -1;
    }
    return 0;
}

static void net_l2tpv3_process_queue(NetL2TPV3State *s)
{
    int size = 0;
    struct iovec *vec;
    bool bad_read;
    int data_size;
    struct mmsghdr *msgvec;

    /* go into ring mode only if there is a "pending" tail */
    if (s->queue_depth > 0) {
        do {
            msgvec = s->msgvec + s->queue_tail;
            if (msgvec->msg_len > 0) {
                data_size = msgvec->msg_len - s->header_size;
                vec = msgvec->msg_hdr.msg_iov;
                if ((data_size > 0) &&
                    (l2tpv3_verify_header(s, vec->iov_base) == 0)) {
                    vec++;
                    /* Use the legacy delivery for now, we will
                     * switch to using our own ring as a queueing mechanism
                     * at a later date
                     */
                    size = qemu_send_packet_async(
                            &s->nc,
                            vec->iov_base,
                            data_size,
                            l2tpv3_send_completed
                        );
                    if (size == 0) {
                        l2tpv3_read_poll(s, false);
                    }
                    bad_read = false;
                } else {
                    bad_read = true;
                    if (!s->header_mismatch) {
                        /* report error only once */
                        error_report("l2tpv3 header verification failed");
                        s->header_mismatch = true;
                    }
                }
            } else {
                bad_read = true;
            }
            s->queue_tail = (s->queue_tail + 1) % MAX_L2TPV3_MSGCNT;
            s->queue_depth--;
        } while (
                (s->queue_depth > 0) &&
                 qemu_can_send_packet(&s->nc) &&
                ((size > 0) || bad_read)
            );
    }
}

static void net_l2tpv3_send(void *opaque)
{
    NetL2TPV3State *s = opaque;
    int target_count, count;
    struct mmsghdr *msgvec;

    /* go into ring mode only if there is a "pending" tail */

    if (s->queue_depth) {

        /* The ring buffer we use has variable intake
         * count of how much we can read varies - adjust accordingly
         */

        target_count = MAX_L2TPV3_MSGCNT - s->queue_depth;

        /* Ensure we do not overrun the ring when we have
         * a lot of enqueued packets
         */

        if (s->queue_head + target_count > MAX_L2TPV3_MSGCNT) {
            target_count = MAX_L2TPV3_MSGCNT - s->queue_head;
        }
    } else {

        /* we do not have any pending packets - we can use
        * the whole message vector linearly instead of using
        * it as a ring
        */

        s->queue_head = 0;
        s->queue_tail = 0;
        target_count = MAX_L2TPV3_MSGCNT;
    }

    msgvec = s->msgvec + s->queue_head;
    if (target_count > 0) {
        count = RETRY_ON_EINTR(
                recvmmsg(s->fd, msgvec, target_count, MSG_DONTWAIT, NULL)
        );
        if (count < 0) {
            /* Recv error - we still need to flush packets here,
             * (re)set queue head to current position
             */
            count = 0;
        }
        s->queue_head = (s->queue_head + count) % MAX_L2TPV3_MSGCNT;
        s->queue_depth += count;
    }
    net_l2tpv3_process_queue(s);
}

static void destroy_vector(struct mmsghdr *msgvec, int count, int iovcount)
{
    int i, j;
    struct iovec *iov;
    struct mmsghdr *cleanup = msgvec;
    if (cleanup) {
        for (i = 0; i < count; i++) {
            if (cleanup->msg_hdr.msg_iov) {
                iov = cleanup->msg_hdr.msg_iov;
                for (j = 0; j < iovcount; j++) {
                    g_free(iov->iov_base);
                    iov++;
                }
                g_free(cleanup->msg_hdr.msg_iov);
            }
            cleanup++;
        }
        g_free(msgvec);
    }
}

static struct mmsghdr *build_l2tpv3_vector(NetL2TPV3State *s, int count)
{
    int i;
    struct iovec *iov;
    struct mmsghdr *msgvec, *result;

    msgvec = g_new(struct mmsghdr, count);
    result = msgvec;
    for (i = 0; i < count ; i++) {
        msgvec->msg_hdr.msg_name = NULL;
        msgvec->msg_hdr.msg_namelen = 0;
        iov =  g_new(struct iovec, IOVSIZE);
        msgvec->msg_hdr.msg_iov = iov;
        iov->iov_base = g_malloc(s->header_size);
        iov->iov_len = s->header_size;
        iov++ ;
        iov->iov_base = qemu_memalign(BUFFER_ALIGN, BUFFER_SIZE);
        iov->iov_len = BUFFER_SIZE;
        msgvec->msg_hdr.msg_iovlen = 2;
        msgvec->msg_hdr.msg_control = NULL;
        msgvec->msg_hdr.msg_controllen = 0;
        msgvec->msg_hdr.msg_flags = 0;
        msgvec++;
    }
    return result;
}

static void net_l2tpv3_cleanup(NetClientState *nc)
{
    NetL2TPV3State *s = DO_UPCAST(NetL2TPV3State, nc, nc);
    qemu_purge_queued_packets(nc);
    l2tpv3_read_poll(s, false);
    l2tpv3_write_poll(s, false);
    if (s->fd >= 0) {
        close(s->fd);
    }
    destroy_vector(s->msgvec, MAX_L2TPV3_MSGCNT, IOVSIZE);
    g_free(s->vec);
    g_free(s->header_buf);
    g_free(s->dgram_dst);
}

static NetClientInfo net_l2tpv3_info = {
    .type = NET_CLIENT_DRIVER_L2TPV3,
    .size = sizeof(NetL2TPV3State),
    .receive = net_l2tpv3_receive_dgram,
    .receive_iov = net_l2tpv3_receive_dgram_iov,
    .poll = l2tpv3_poll,
    .cleanup = net_l2tpv3_cleanup,
};

int net_init_l2tpv3(const Netdev *netdev,
                    const char *name,
                    NetClientState *peer, Error **errp)
{
    const NetdevL2TPv3Options *l2tpv3;
    NetL2TPV3State *s;
    NetClientState *nc;
    int fd = -1, gairet;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    char *srcport, *dstport;

    nc = qemu_new_net_client(&net_l2tpv3_info, peer, "l2tpv3", name);

    s = DO_UPCAST(NetL2TPV3State, nc, nc);

    s->queue_head = 0;
    s->queue_tail = 0;
    s->header_mismatch = false;

    assert(netdev->type == NET_CLIENT_DRIVER_L2TPV3);
    l2tpv3 = &netdev->u.l2tpv3;

    if (l2tpv3->has_ipv6 && l2tpv3->ipv6) {
        s->ipv6 = l2tpv3->ipv6;
    } else {
        s->ipv6 = false;
    }

    if ((l2tpv3->has_offset) && (l2tpv3->offset > 256)) {
        error_setg(errp, "offset must be less than 256 bytes");
        goto outerr;
    }

    if (l2tpv3->has_rxcookie || l2tpv3->has_txcookie) {
        if (l2tpv3->has_rxcookie && l2tpv3->has_txcookie) {
            s->cookie = true;
        } else {
            error_setg(errp,
                       "require both 'rxcookie' and 'txcookie' or neither");
            goto outerr;
        }
    } else {
        s->cookie = false;
    }

    if (l2tpv3->has_cookie64 || l2tpv3->cookie64) {
        s->cookie_is_64  = true;
    } else {
        s->cookie_is_64  = false;
    }

    if (l2tpv3->has_udp && l2tpv3->udp) {
        s->udp = true;
        if (!(l2tpv3->srcport && l2tpv3->dstport)) {
            error_setg(errp, "need both src and dst port for udp");
            goto outerr;
        } else {
            srcport = l2tpv3->srcport;
            dstport = l2tpv3->dstport;
        }
    } else {
        s->udp = false;
        srcport = NULL;
        dstport = NULL;
    }


    s->offset = 4;
    s->session_offset = 0;
    s->cookie_offset = 4;
    s->counter_offset = 4;

    s->tx_session = l2tpv3->txsession;
    if (l2tpv3->has_rxsession) {
        s->rx_session = l2tpv3->rxsession;
    } else {
        s->rx_session = s->tx_session;
    }

    if (s->cookie) {
        s->rx_cookie = l2tpv3->rxcookie;
        s->tx_cookie = l2tpv3->txcookie;
        if (s->cookie_is_64 == true) {
            /* 64 bit cookie */
            s->offset += 8;
            s->counter_offset += 8;
        } else {
            /* 32 bit cookie */
            s->offset += 4;
            s->counter_offset += 4;
        }
    }

    memset(&hints, 0, sizeof(hints));

    if (s->ipv6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_INET;
    }
    if (s->udp) {
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = 0;
        s->offset += 4;
        s->counter_offset += 4;
        s->session_offset += 4;
        s->cookie_offset += 4;
    } else {
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = IPPROTO_L2TP;
    }

    gairet = getaddrinfo(l2tpv3->src, srcport, &hints, &result);

    if ((gairet != 0) || (result == NULL)) {
        error_setg(errp, "could not resolve src, errno = %s",
                   gai_strerror(gairet));
        goto outerr;
    }
    fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd == -1) {
        fd = -errno;
        error_setg(errp, "socket creation failed, errno = %d",
                   -fd);
        goto outerr;
    }
    if (bind(fd, (struct sockaddr *) result->ai_addr, result->ai_addrlen)) {
        error_setg(errp, "could not bind socket err=%i", errno);
        goto outerr;
    }

    freeaddrinfo(result);

    memset(&hints, 0, sizeof(hints));

    if (s->ipv6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_INET;
    }
    if (s->udp) {
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = 0;
    } else {
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = IPPROTO_L2TP;
    }

    result = NULL;
    gairet = getaddrinfo(l2tpv3->dst, dstport, &hints, &result);
    if ((gairet != 0) || (result == NULL)) {
        error_setg(errp, "could not resolve dst, error = %s",
                   gai_strerror(gairet));
        goto outerr;
    }

    s->dgram_dst = g_new0(struct sockaddr_storage, 1);
    memcpy(s->dgram_dst, result->ai_addr, result->ai_addrlen);
    s->dst_size = result->ai_addrlen;

    freeaddrinfo(result);

    if (l2tpv3->has_counter && l2tpv3->counter) {
        s->has_counter = true;
        s->offset += 4;
    } else {
        s->has_counter = false;
    }

    if (l2tpv3->has_pincounter && l2tpv3->pincounter) {
        s->has_counter = true;  /* pin counter implies that there is counter */
        s->pin_counter = true;
    } else {
        s->pin_counter = false;
    }

    if (l2tpv3->has_offset) {
        /* extra offset */
        s->offset += l2tpv3->offset;
    }

    if ((s->ipv6) || (s->udp)) {
        s->header_size = s->offset;
    } else {
        s->header_size = s->offset + sizeof(struct iphdr);
    }

    s->msgvec = build_l2tpv3_vector(s, MAX_L2TPV3_MSGCNT);
    s->vec = g_new(struct iovec, MAX_L2TPV3_IOVCNT);
    s->header_buf = g_malloc(s->header_size);

    qemu_socket_set_nonblock(fd);

    s->fd = fd;
    s->counter = 0;

    l2tpv3_read_poll(s, true);

    qemu_set_info_str(&s->nc, "l2tpv3: connected");
    return 0;
outerr:
    qemu_del_net_client(nc);
    if (fd >= 0) {
        close(fd);
    }
    if (result) {
        freeaddrinfo(result);
    }
    return -1;
}

