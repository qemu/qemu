/*
 * AF_XDP network backend.
 *
 * Copyright (c) 2023 Red Hat, Inc.
 *
 * Authors:
 *  Ilya Maximets <i.maximets@ovn.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */


#include "qemu/osdep.h"
#include <bpf/bpf.h>
#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <net/if.h>
#include <xdp/xsk.h>

#include "clients.h"
#include "monitor/monitor.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/memalign.h"


typedef struct AFXDPState {
    NetClientState       nc;

    struct xsk_socket    *xsk;
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_ring_cons cq;
    struct xsk_ring_prod fq;

    char                 ifname[IFNAMSIZ];
    int                  ifindex;
    bool                 read_poll;
    bool                 write_poll;
    uint32_t             outstanding_tx;

    uint64_t             *pool;
    uint32_t             n_pool;
    char                 *buffer;
    struct xsk_umem      *umem;

    uint32_t             xdp_flags;
    bool                 inhibit;

    char                 *map_path;
    int                  map_fd;
    uint32_t             map_start_index;
} AFXDPState;

#define AF_XDP_BATCH_SIZE 64

static void af_xdp_send(void *opaque);
static void af_xdp_writable(void *opaque);

/* Set the event-loop handlers for the af-xdp backend. */
static void af_xdp_update_fd_handler(AFXDPState *s)
{
    qemu_set_fd_handler(xsk_socket__fd(s->xsk),
                        s->read_poll ? af_xdp_send : NULL,
                        s->write_poll ? af_xdp_writable : NULL,
                        s);
}

/* Update the read handler. */
static void af_xdp_read_poll(AFXDPState *s, bool enable)
{
    if (s->read_poll != enable) {
        s->read_poll = enable;
        af_xdp_update_fd_handler(s);
    }
}

/* Update the write handler. */
static void af_xdp_write_poll(AFXDPState *s, bool enable)
{
    if (s->write_poll != enable) {
        s->write_poll = enable;
        af_xdp_update_fd_handler(s);
    }
}

static void af_xdp_poll(NetClientState *nc, bool enable)
{
    AFXDPState *s = DO_UPCAST(AFXDPState, nc, nc);

    if (s->read_poll != enable || s->write_poll != enable) {
        s->write_poll = enable;
        s->read_poll  = enable;
        af_xdp_update_fd_handler(s);
    }
}

static void af_xdp_complete_tx(AFXDPState *s)
{
    uint32_t idx = 0;
    uint32_t done, i;
    uint64_t *addr;

    done = xsk_ring_cons__peek(&s->cq, XSK_RING_CONS__DEFAULT_NUM_DESCS, &idx);

    for (i = 0; i < done; i++) {
        addr = (void *) xsk_ring_cons__comp_addr(&s->cq, idx++);
        s->pool[s->n_pool++] = *addr;
        s->outstanding_tx--;
    }

    if (done) {
        xsk_ring_cons__release(&s->cq, done);
    }
}

/*
 * The fd_write() callback, invoked if the fd is marked as writable
 * after a poll.
 */
static void af_xdp_writable(void *opaque)
{
    AFXDPState *s = opaque;

    /* Try to recover buffers that are already sent. */
    af_xdp_complete_tx(s);

    /*
     * Unregister the handler, unless we still have packets to transmit
     * and kernel needs a wake up.
     */
    if (!s->outstanding_tx || !xsk_ring_prod__needs_wakeup(&s->tx)) {
        af_xdp_write_poll(s, false);
    }

    /* Flush any buffered packets. */
    qemu_flush_queued_packets(&s->nc);
}

static ssize_t af_xdp_receive(NetClientState *nc,
                              const uint8_t *buf, size_t size)
{
    AFXDPState *s = DO_UPCAST(AFXDPState, nc, nc);
    struct xdp_desc *desc;
    uint32_t idx;
    void *data;

    /* Try to recover buffers that are already sent. */
    af_xdp_complete_tx(s);

    if (size > XSK_UMEM__DEFAULT_FRAME_SIZE) {
        /* We can't transmit packet this size... */
        return size;
    }

    if (!s->n_pool || !xsk_ring_prod__reserve(&s->tx, 1, &idx)) {
        /*
         * Out of buffers or space in tx ring.  Poll until we can write.
         * This will also kick the Tx, if it was waiting on CQ.
         */
        af_xdp_write_poll(s, true);
        return 0;
    }

    desc = xsk_ring_prod__tx_desc(&s->tx, idx);
    desc->addr = s->pool[--s->n_pool];
    desc->len = size;

    data = xsk_umem__get_data(s->buffer, desc->addr);
    memcpy(data, buf, size);

    xsk_ring_prod__submit(&s->tx, 1);
    s->outstanding_tx++;

    if (xsk_ring_prod__needs_wakeup(&s->tx)) {
        af_xdp_write_poll(s, true);
    }

    return size;
}

/*
 * Complete a previous send (backend --> guest) and enable the
 * fd_read callback.
 */
static void af_xdp_send_completed(NetClientState *nc, ssize_t len)
{
    AFXDPState *s = DO_UPCAST(AFXDPState, nc, nc);

    af_xdp_read_poll(s, true);
}

static void af_xdp_fq_refill(AFXDPState *s, uint32_t n)
{
    uint32_t i, idx = 0;

    /* Leave one packet for Tx, just in case. */
    if (s->n_pool < n + 1) {
        n = s->n_pool;
    }

    if (!n || !xsk_ring_prod__reserve(&s->fq, n, &idx)) {
        return;
    }

    for (i = 0; i < n; i++) {
        *xsk_ring_prod__fill_addr(&s->fq, idx++) = s->pool[--s->n_pool];
    }
    xsk_ring_prod__submit(&s->fq, n);

    if (xsk_ring_prod__needs_wakeup(&s->fq)) {
        /* Receive was blocked by not having enough buffers.  Wake it up. */
        af_xdp_read_poll(s, true);
    }
}

static void af_xdp_send(void *opaque)
{
    uint32_t i, n_rx, idx = 0;
    AFXDPState *s = opaque;

    n_rx = xsk_ring_cons__peek(&s->rx, AF_XDP_BATCH_SIZE, &idx);
    if (!n_rx) {
        return;
    }

    for (i = 0; i < n_rx; i++) {
        const struct xdp_desc *desc;
        struct iovec iov;

        desc = xsk_ring_cons__rx_desc(&s->rx, idx++);

        iov.iov_base = xsk_umem__get_data(s->buffer, desc->addr);
        iov.iov_len = desc->len;

        s->pool[s->n_pool++] = desc->addr;

        if (!qemu_sendv_packet_async(&s->nc, &iov, 1,
                                     af_xdp_send_completed)) {
            /*
             * The peer does not receive anymore.  Packet is queued, stop
             * reading from the backend until af_xdp_send_completed().
             */
            af_xdp_read_poll(s, false);

            /* Return unused descriptors to not break the ring cache. */
            xsk_ring_cons__cancel(&s->rx, n_rx - i - 1);
            n_rx = i + 1;
            break;
        }
    }

    /* Release actually sent descriptors and try to re-fill. */
    xsk_ring_cons__release(&s->rx, n_rx);
    af_xdp_fq_refill(s, AF_XDP_BATCH_SIZE);
}

/* Flush and close. */
static void af_xdp_cleanup(NetClientState *nc)
{
    AFXDPState *s = DO_UPCAST(AFXDPState, nc, nc);
    int idx;

    qemu_purge_queued_packets(nc);

    af_xdp_poll(nc, false);

    xsk_socket__delete(s->xsk);
    s->xsk = NULL;
    g_free(s->pool);
    s->pool = NULL;
    xsk_umem__delete(s->umem);
    s->umem = NULL;
    qemu_vfree(s->buffer);
    s->buffer = NULL;

    if (s->map_fd >= 0) {
        idx = nc->queue_index + s->map_start_index;
        if (bpf_map_delete_elem(s->map_fd, &idx)) {
            fprintf(stderr, "af-xdp: unable to remove AF_XDP socket from map"
                    " %s\n", s->map_path);
        }
        close(s->map_fd);
        s->map_fd = -1;
    }
    g_free(s->map_path);
    s->map_path = NULL;
}

static int af_xdp_umem_create(AFXDPState *s, int sock_fd, Error **errp)
{
    struct xsk_umem_config config = {
        .fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
        .comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
        .frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE,
        .frame_headroom = 0,
    };
    uint64_t n_descs;
    uint64_t size;
    int64_t i;
    int ret;

    /* Number of descriptors if all 4 queues (rx, tx, cq, fq) are full. */
    n_descs = (XSK_RING_PROD__DEFAULT_NUM_DESCS
               + XSK_RING_CONS__DEFAULT_NUM_DESCS) * 2;
    size = n_descs * XSK_UMEM__DEFAULT_FRAME_SIZE;

    s->buffer = qemu_memalign(qemu_real_host_page_size(), size);
    memset(s->buffer, 0, size);

    if (sock_fd < 0) {
        ret = xsk_umem__create(&s->umem, s->buffer, size,
                               &s->fq, &s->cq, &config);
    } else {
        ret = xsk_umem__create_with_fd(&s->umem, sock_fd, s->buffer, size,
                                       &s->fq, &s->cq, &config);
    }

    if (ret) {
        qemu_vfree(s->buffer);
        error_setg_errno(errp, errno,
                         "failed to create umem for %s queue_index: %d",
                         s->ifname, s->nc.queue_index);
        return -1;
    }

    s->pool = g_new(uint64_t, n_descs);
    /* Fill the pool in the opposite order, because it's a LIFO queue. */
    for (i = n_descs - 1; i >= 0; i--) {
        s->pool[i] = i * XSK_UMEM__DEFAULT_FRAME_SIZE;
    }
    s->n_pool = n_descs;

    af_xdp_fq_refill(s, XSK_RING_PROD__DEFAULT_NUM_DESCS);

    return 0;
}

static int af_xdp_socket_create(AFXDPState *s,
                                const NetdevAFXDPOptions *opts, Error **errp)
{
    struct xsk_socket_config cfg = {
        .rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
        .tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
        .libxdp_flags = 0,
        .bind_flags = XDP_USE_NEED_WAKEUP,
        .xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST,
    };
    int queue_id, error = 0;

    if (s->inhibit) {
        cfg.libxdp_flags |= XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD;
    }

    if (opts->has_force_copy && opts->force_copy) {
        cfg.bind_flags |= XDP_COPY;
    }

    queue_id = s->nc.queue_index;
    if (opts->has_start_queue && opts->start_queue > 0) {
        queue_id += opts->start_queue;
    }

    if (opts->has_mode) {
        /* Specific mode requested. */
        cfg.xdp_flags |= (opts->mode == AFXDP_MODE_NATIVE)
                         ? XDP_FLAGS_DRV_MODE : XDP_FLAGS_SKB_MODE;
        if (xsk_socket__create(&s->xsk, s->ifname, queue_id,
                               s->umem, &s->rx, &s->tx, &cfg)) {
            error = errno;
        }
    } else {
        /* No mode requested, try native first. */
        cfg.xdp_flags |= XDP_FLAGS_DRV_MODE;

        if (xsk_socket__create(&s->xsk, s->ifname, queue_id,
                               s->umem, &s->rx, &s->tx, &cfg)) {
            /* Can't use native mode, try skb. */
            cfg.xdp_flags &= ~XDP_FLAGS_DRV_MODE;
            cfg.xdp_flags |= XDP_FLAGS_SKB_MODE;

            if (xsk_socket__create(&s->xsk, s->ifname, queue_id,
                                   s->umem, &s->rx, &s->tx, &cfg)) {
                error = errno;
            }
        }
    }

    if (error) {
        error_setg_errno(errp, error,
                         "failed to create AF_XDP socket for %s queue_id: %d",
                         s->ifname, queue_id);
        return -1;
    }

    s->xdp_flags = cfg.xdp_flags;

    return 0;
}

static int af_xdp_update_xsk_map(AFXDPState *s, Error **errp)
{
    int xsk_fd, idx, error = 0;

    if (!s->map_path) {
        return 0;
    }

    s->map_fd = bpf_obj_get(s->map_path);
    if (s->map_fd < 0) {
        error = errno;
    } else {
        xsk_fd = xsk_socket__fd(s->xsk);
        idx = s->nc.queue_index + s->map_start_index;
        if (bpf_map_update_elem(s->map_fd, &idx, &xsk_fd, 0)) {
            error = errno;
        }
    }

    if (error) {
        error_setg_errno(errp, error,
                         "failed to insert AF_XDP socket into map %s",
                         s->map_path);
        return -1;
    }

    return 0;
}

/* NetClientInfo methods. */
static NetClientInfo net_af_xdp_info = {
    .type = NET_CLIENT_DRIVER_AF_XDP,
    .size = sizeof(AFXDPState),
    .receive = af_xdp_receive,
    .poll = af_xdp_poll,
    .cleanup = af_xdp_cleanup,
};

static int *parse_socket_fds(const char *sock_fds_str,
                             int64_t n_expected, Error **errp)
{
    gchar **substrings = g_strsplit(sock_fds_str, ":", -1);
    int64_t i, n_sock_fds = g_strv_length(substrings);
    int *sock_fds = NULL;

    if (n_sock_fds != n_expected) {
        error_setg(errp, "expected %"PRIi64" socket fds, got %"PRIi64,
                   n_expected, n_sock_fds);
        goto exit;
    }

    sock_fds = g_new(int, n_sock_fds);

    for (i = 0; i < n_sock_fds; i++) {
        sock_fds[i] = monitor_fd_param(monitor_cur(), substrings[i], errp);
        if (sock_fds[i] < 0) {
            g_free(sock_fds);
            sock_fds = NULL;
            goto exit;
        }
    }

exit:
    g_strfreev(substrings);
    return sock_fds;
}

/*
 * The exported init function.
 *
 * ... -netdev af-xdp,ifname="..."
 */
int net_init_af_xdp(const Netdev *netdev,
                    const char *name, NetClientState *peer, Error **errp)
{
    const NetdevAFXDPOptions *opts = &netdev->u.af_xdp;
    NetClientState *nc, *nc0 = NULL;
    int32_t map_start_index;
    unsigned int ifindex;
    uint32_t prog_id = 0;
    g_autofree int *sock_fds = NULL;
    int64_t i, queues;
    Error *err = NULL;
    AFXDPState *s;
    bool inhibit;

    ifindex = if_nametoindex(opts->ifname);
    if (!ifindex) {
        error_setg_errno(errp, errno, "failed to get ifindex for '%s'",
                         opts->ifname);
        return -1;
    }

    queues = opts->has_queues ? opts->queues : 1;
    if (queues < 1) {
        error_setg(errp, "invalid number of queues (%" PRIi64 ") for '%s'",
                   queues, opts->ifname);
        return -1;
    }

    inhibit = opts->has_inhibit && opts->inhibit;
    if (inhibit && !opts->sock_fds && !opts->map_path) {
        error_setg(errp, "'inhibit=on' requires 'sock-fds' or 'map-path'");
        return -1;
    }
    if (!inhibit && (opts->sock_fds || opts->map_path)) {
        error_setg(errp, "'sock-fds' and 'map-path' require 'inhibit=on'");
        return -1;
    }
    if (opts->sock_fds && opts->map_path) {
        error_setg(errp, "'sock-fds' and 'map-path' are mutually exclusive");
        return -1;
    }
    if (!opts->map_path && opts->has_map_start_index) {
        error_setg(errp, "'map-start-index' requires 'map-path'");
        return -1;
    }

    map_start_index = opts->has_map_start_index ? opts->map_start_index : 0;
    if (map_start_index < 0) {
        error_setg(errp, "'map-start-index' cannot be negative (%d)",
                   map_start_index);
        return -1;
    }

    if (opts->sock_fds) {
        sock_fds = parse_socket_fds(opts->sock_fds, queues, errp);
        if (!sock_fds) {
            return -1;
        }
    }

    for (i = 0; i < queues; i++) {
        nc = qemu_new_net_client(&net_af_xdp_info, peer, "af-xdp", name);
        qemu_set_info_str(nc, "af-xdp%"PRIi64" to %s", i, opts->ifname);
        nc->queue_index = i;

        if (!nc0) {
            nc0 = nc;
        }

        s = DO_UPCAST(AFXDPState, nc, nc);

        pstrcpy(s->ifname, sizeof(s->ifname), opts->ifname);
        s->ifindex = ifindex;
        s->inhibit = inhibit;

        s->map_path = g_strdup(opts->map_path);
        s->map_start_index = map_start_index;
        s->map_fd = -1;

        if (af_xdp_umem_create(s, sock_fds ? sock_fds[i] : -1, &err) ||
            af_xdp_socket_create(s, opts, &err) ||
            af_xdp_update_xsk_map(s, &err)) {
            goto err;
        }
    }

    if (nc0 && !inhibit) {
        s = DO_UPCAST(AFXDPState, nc, nc0);
        if (bpf_xdp_query_id(s->ifindex, s->xdp_flags, &prog_id) || !prog_id) {
            error_setg_errno(&err, errno,
                             "no XDP program loaded on '%s', ifindex: %d",
                             s->ifname, s->ifindex);
            goto err;
        }
    }

    af_xdp_read_poll(s, true); /* Initially only poll for reads. */

    return 0;

err:
    if (nc0) {
        qemu_del_net_client(nc0);
        error_propagate(errp, err);
    }

    return -1;
}
