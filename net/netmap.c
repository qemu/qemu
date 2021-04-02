/*
 * netmap access for qemu
 *
 * Copyright (c) 2012-2013 Luigi Rizzo
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
#include <sys/ioctl.h>
#include <net/if.h>
#define NETMAP_WITH_LIBS
#include <net/netmap.h>
#include <net/netmap_user.h>

#include "net/net.h"
#include "net/tap.h"
#include "clients.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"

typedef struct NetmapState {
    NetClientState      nc;
    struct nm_desc      *nmd;
    char                ifname[IFNAMSIZ];
    struct netmap_ring  *tx;
    struct netmap_ring  *rx;
    bool                read_poll;
    bool                write_poll;
    struct iovec        iov[IOV_MAX];
    int                 vnet_hdr_len;  /* Current virtio-net header length. */
} NetmapState;

#ifndef __FreeBSD__
#define pkt_copy bcopy
#else
/* A fast copy routine only for multiples of 64 bytes, non overlapped. */
static inline void
pkt_copy(const void *_src, void *_dst, int l)
{
    const uint64_t *src = _src;
    uint64_t *dst = _dst;
    if (unlikely(l >= 1024)) {
        bcopy(src, dst, l);
        return;
    }
    for (; l > 0; l -= 64) {
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
    }
}
#endif /* __FreeBSD__ */

/*
 * Open a netmap device. We assume there is only one queue
 * (which is the case for the VALE bridge).
 */
static struct nm_desc *netmap_open(const NetdevNetmapOptions *nm_opts,
                                   Error **errp)
{
    struct nm_desc *nmd;
    struct nmreq req;

    memset(&req, 0, sizeof(req));

    nmd = nm_open(nm_opts->ifname, &req, NETMAP_NO_TX_POLL,
                  NULL);
    if (nmd == NULL) {
        error_setg_errno(errp, errno, "Failed to nm_open() %s",
                         nm_opts->ifname);
        return NULL;
    }

    return nmd;
}

static void netmap_send(void *opaque);
static void netmap_writable(void *opaque);

/* Set the event-loop handlers for the netmap backend. */
static void netmap_update_fd_handler(NetmapState *s)
{
    qemu_set_fd_handler(s->nmd->fd,
                        s->read_poll ? netmap_send : NULL,
                        s->write_poll ? netmap_writable : NULL,
                        s);
}

/* Update the read handler. */
static void netmap_read_poll(NetmapState *s, bool enable)
{
    if (s->read_poll != enable) { /* Do nothing if not changed. */
        s->read_poll = enable;
        netmap_update_fd_handler(s);
    }
}

/* Update the write handler. */
static void netmap_write_poll(NetmapState *s, bool enable)
{
    if (s->write_poll != enable) {
        s->write_poll = enable;
        netmap_update_fd_handler(s);
    }
}

static void netmap_poll(NetClientState *nc, bool enable)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);

    if (s->read_poll != enable || s->write_poll != enable) {
        s->write_poll = enable;
        s->read_poll  = enable;
        netmap_update_fd_handler(s);
    }
}

/*
 * The fd_write() callback, invoked if the fd is marked as
 * writable after a poll. Unregister the handler and flush any
 * buffered packets.
 */
static void netmap_writable(void *opaque)
{
    NetmapState *s = opaque;

    netmap_write_poll(s, false);
    qemu_flush_queued_packets(&s->nc);
}

static ssize_t netmap_receive_iov(NetClientState *nc,
                    const struct iovec *iov, int iovcnt)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);
    struct netmap_ring *ring = s->tx;
    unsigned int tail = ring->tail;
    ssize_t totlen = 0;
    uint32_t last;
    uint32_t idx;
    uint8_t *dst;
    int j;
    uint32_t i;

    last = i = ring->head;

    if (nm_ring_space(ring) < iovcnt) {
        /* Not enough netmap slots. Tell the kernel that we have seen the new
         * available slots (so that it notifies us again when it has more
         * ones), but without publishing any new slots to be processed
         * (e.g., we don't advance ring->head). */
        ring->cur = tail;
        netmap_write_poll(s, true);
        return 0;
    }

    for (j = 0; j < iovcnt; j++) {
        int iov_frag_size = iov[j].iov_len;
        int offset = 0;
        int nm_frag_size;

        totlen += iov_frag_size;

        /* Split each iovec fragment over more netmap slots, if
           necessary. */
        while (iov_frag_size) {
            nm_frag_size = MIN(iov_frag_size, ring->nr_buf_size);

            if (unlikely(i == tail)) {
                /* We ran out of netmap slots while splitting the
                   iovec fragments. */
                ring->cur = tail;
                netmap_write_poll(s, true);
                return 0;
            }

            idx = ring->slot[i].buf_idx;
            dst = (uint8_t *)NETMAP_BUF(ring, idx);

            ring->slot[i].len = nm_frag_size;
            ring->slot[i].flags = NS_MOREFRAG;
            pkt_copy(iov[j].iov_base + offset, dst, nm_frag_size);

            last = i;
            i = nm_ring_next(ring, i);

            offset += nm_frag_size;
            iov_frag_size -= nm_frag_size;
        }
    }
    /* The last slot must not have NS_MOREFRAG set. */
    ring->slot[last].flags &= ~NS_MOREFRAG;

    /* Now update ring->head and ring->cur to publish the new slots and
     * the new wakeup point. */
    ring->head = ring->cur = i;

    ioctl(s->nmd->fd, NIOCTXSYNC, NULL);

    return totlen;
}

static ssize_t netmap_receive(NetClientState *nc,
      const uint8_t *buf, size_t size)
{
    struct iovec iov;

    iov.iov_base = (void *)buf;
    iov.iov_len = size;

    return netmap_receive_iov(nc, &iov, 1);
}

/* Complete a previous send (backend --> guest) and enable the
   fd_read callback. */
static void netmap_send_completed(NetClientState *nc, ssize_t len)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);

    netmap_read_poll(s, true);
}

static void netmap_send(void *opaque)
{
    NetmapState *s = opaque;
    struct netmap_ring *ring = s->rx;
    unsigned int tail = ring->tail;

    /* Keep sending while there are available slots in the netmap
       RX ring and the forwarding path towards the peer is open. */
    while (ring->head != tail) {
        uint32_t i = ring->head;
        uint32_t idx;
        bool morefrag;
        int iovcnt = 0;
        int iovsize;

        /* Get a (possibly multi-slot) packet. */
        do {
            idx = ring->slot[i].buf_idx;
            morefrag = (ring->slot[i].flags & NS_MOREFRAG);
            s->iov[iovcnt].iov_base = (void *)NETMAP_BUF(ring, idx);
            s->iov[iovcnt].iov_len = ring->slot[i].len;
            iovcnt++;
            i = nm_ring_next(ring, i);
        } while (i != tail && morefrag);

        /* Advance ring->cur to tell the kernel that we have seen the slots. */
        ring->cur = i;

        if (unlikely(morefrag)) {
            /* This is a truncated packet, so we can stop without releasing the
             * incomplete slots by updating ring->head. We will hopefully
             * re-read the complete packet the next time we are called. */
            break;
        }

        iovsize = qemu_sendv_packet_async(&s->nc, s->iov, iovcnt,
                                            netmap_send_completed);

        /* Release the slots to the kernel. */
        ring->head = i;

        if (iovsize == 0) {
            /* The peer does not receive anymore. Packet is queued, stop
             * reading from the backend until netmap_send_completed(). */
            netmap_read_poll(s, false);
            break;
        }
    }
}

/* Flush and close. */
static void netmap_cleanup(NetClientState *nc)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);

    qemu_purge_queued_packets(nc);

    netmap_poll(nc, false);
    nm_close(s->nmd);
    s->nmd = NULL;
}

/* Offloading manipulation support callbacks. */
static int netmap_fd_set_vnet_hdr_len(NetmapState *s, int len)
{
    struct nmreq req;

    /* Issue a NETMAP_BDG_VNET_HDR command to change the virtio-net header
     * length for the netmap adapter associated to 's->ifname'.
     */
    memset(&req, 0, sizeof(req));
    pstrcpy(req.nr_name, sizeof(req.nr_name), s->ifname);
    req.nr_version = NETMAP_API;
    req.nr_cmd = NETMAP_BDG_VNET_HDR;
    req.nr_arg1 = len;

    return ioctl(s->nmd->fd, NIOCREGIF, &req);
}

static bool netmap_has_vnet_hdr_len(NetClientState *nc, int len)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);
    int prev_len = s->vnet_hdr_len;

    /* Check that we can set the new length. */
    if (netmap_fd_set_vnet_hdr_len(s, len)) {
        return false;
    }

    /* Restore the previous length. */
    if (netmap_fd_set_vnet_hdr_len(s, prev_len)) {
        error_report("Failed to restore vnet-hdr length %d on %s: %s",
                     prev_len, s->ifname, strerror(errno));
        abort();
    }

    return true;
}

/* A netmap interface that supports virtio-net headers always
 * supports UFO, so we use this callback also for the has_ufo hook. */
static bool netmap_has_vnet_hdr(NetClientState *nc)
{
    return netmap_has_vnet_hdr_len(nc, sizeof(struct virtio_net_hdr));
}

static void netmap_using_vnet_hdr(NetClientState *nc, bool enable)
{
}

static void netmap_set_vnet_hdr_len(NetClientState *nc, int len)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);
    int err;

    err = netmap_fd_set_vnet_hdr_len(s, len);
    if (err) {
        error_report("Unable to set vnet-hdr length %d on %s: %s",
                     len, s->ifname, strerror(errno));
    } else {
        /* Keep track of the current length. */
        s->vnet_hdr_len = len;
    }
}

static void netmap_set_offload(NetClientState *nc, int csum, int tso4, int tso6,
                               int ecn, int ufo)
{
    NetmapState *s = DO_UPCAST(NetmapState, nc, nc);

    /* Setting a virtio-net header length greater than zero automatically
     * enables the offloadings. */
    if (!s->vnet_hdr_len) {
        netmap_set_vnet_hdr_len(nc, sizeof(struct virtio_net_hdr));
    }
}

/* NetClientInfo methods */
static NetClientInfo net_netmap_info = {
    .type = NET_CLIENT_DRIVER_NETMAP,
    .size = sizeof(NetmapState),
    .receive = netmap_receive,
    .receive_iov = netmap_receive_iov,
    .poll = netmap_poll,
    .cleanup = netmap_cleanup,
    .has_ufo = netmap_has_vnet_hdr,
    .has_vnet_hdr = netmap_has_vnet_hdr,
    .has_vnet_hdr_len = netmap_has_vnet_hdr_len,
    .using_vnet_hdr = netmap_using_vnet_hdr,
    .set_offload = netmap_set_offload,
    .set_vnet_hdr_len = netmap_set_vnet_hdr_len,
};

/* The exported init function
 *
 * ... -net netmap,ifname="..."
 */
int net_init_netmap(const Netdev *netdev,
                    const char *name, NetClientState *peer, Error **errp)
{
    const NetdevNetmapOptions *netmap_opts = &netdev->u.netmap;
    struct nm_desc *nmd;
    NetClientState *nc;
    Error *err = NULL;
    NetmapState *s;

    nmd = netmap_open(netmap_opts, &err);
    if (err) {
        error_propagate(errp, err);
        return -1;
    }
    /* Create the object. */
    nc = qemu_new_net_client(&net_netmap_info, peer, "netmap", name);
    s = DO_UPCAST(NetmapState, nc, nc);
    s->nmd = nmd;
    s->tx = NETMAP_TXRING(nmd->nifp, 0);
    s->rx = NETMAP_RXRING(nmd->nifp, 0);
    s->vnet_hdr_len = 0;
    pstrcpy(s->ifname, sizeof(s->ifname), netmap_opts->ifname);
    netmap_read_poll(s, true); /* Initially only poll for reads. */

    return 0;
}

