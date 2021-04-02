/*
 *  xen paravirt network card backend
 *
 *  (c) Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributions after 2012-01-13 are licensed under the terms of the
 *  GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "net/net.h"
#include "net/checksum.h"
#include "net/util.h"
#include "hw/xen/xen-legacy-backend.h"

#include "hw/xen/interface/io/netif.h"

/* ------------------------------------------------------------- */

struct XenNetDev {
    struct XenLegacyDevice      xendev;  /* must be first */
    char                  *mac;
    int                   tx_work;
    int                   tx_ring_ref;
    int                   rx_ring_ref;
    struct netif_tx_sring *txs;
    struct netif_rx_sring *rxs;
    netif_tx_back_ring_t  tx_ring;
    netif_rx_back_ring_t  rx_ring;
    NICConf               conf;
    NICState              *nic;
};

/* ------------------------------------------------------------- */

static void net_tx_response(struct XenNetDev *netdev, netif_tx_request_t *txp, int8_t st)
{
    RING_IDX i = netdev->tx_ring.rsp_prod_pvt;
    netif_tx_response_t *resp;
    int notify;

    resp = RING_GET_RESPONSE(&netdev->tx_ring, i);
    resp->id     = txp->id;
    resp->status = st;

#if 0
    if (txp->flags & NETTXF_extra_info) {
        RING_GET_RESPONSE(&netdev->tx_ring, ++i)->status = NETIF_RSP_NULL;
    }
#endif

    netdev->tx_ring.rsp_prod_pvt = ++i;
    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&netdev->tx_ring, notify);
    if (notify) {
        xen_pv_send_notify(&netdev->xendev);
    }

    if (i == netdev->tx_ring.req_cons) {
        int more_to_do;
        RING_FINAL_CHECK_FOR_REQUESTS(&netdev->tx_ring, more_to_do);
        if (more_to_do) {
            netdev->tx_work++;
        }
    }
}

static void net_tx_error(struct XenNetDev *netdev, netif_tx_request_t *txp, RING_IDX end)
{
#if 0
    /*
     * Hmm, why netback fails everything in the ring?
     * Should we do that even when not supporting SG and TSO?
     */
    RING_IDX cons = netdev->tx_ring.req_cons;

    do {
        make_tx_response(netif, txp, NETIF_RSP_ERROR);
        if (cons >= end) {
            break;
        }
        txp = RING_GET_REQUEST(&netdev->tx_ring, cons++);
    } while (1);
    netdev->tx_ring.req_cons = cons;
    netif_schedule_work(netif);
    netif_put(netif);
#else
    net_tx_response(netdev, txp, NETIF_RSP_ERROR);
#endif
}

static void net_tx_packets(struct XenNetDev *netdev)
{
    netif_tx_request_t txreq;
    RING_IDX rc, rp;
    void *page;
    void *tmpbuf = NULL;

    for (;;) {
        rc = netdev->tx_ring.req_cons;
        rp = netdev->tx_ring.sring->req_prod;
        xen_rmb(); /* Ensure we see queued requests up to 'rp'. */

        while ((rc != rp)) {
            if (RING_REQUEST_CONS_OVERFLOW(&netdev->tx_ring, rc)) {
                break;
            }
            memcpy(&txreq, RING_GET_REQUEST(&netdev->tx_ring, rc), sizeof(txreq));
            netdev->tx_ring.req_cons = ++rc;

#if 1
            /* should not happen in theory, we don't announce the *
             * feature-{sg,gso,whatelse} flags in xenstore (yet?) */
            if (txreq.flags & NETTXF_extra_info) {
                xen_pv_printf(&netdev->xendev, 0, "FIXME: extra info flag\n");
                net_tx_error(netdev, &txreq, rc);
                continue;
            }
            if (txreq.flags & NETTXF_more_data) {
                xen_pv_printf(&netdev->xendev, 0, "FIXME: more data flag\n");
                net_tx_error(netdev, &txreq, rc);
                continue;
            }
#endif

            if (txreq.size < 14) {
                xen_pv_printf(&netdev->xendev, 0, "bad packet size: %d\n",
                              txreq.size);
                net_tx_error(netdev, &txreq, rc);
                continue;
            }

            if ((txreq.offset + txreq.size) > XC_PAGE_SIZE) {
                xen_pv_printf(&netdev->xendev, 0, "error: page crossing\n");
                net_tx_error(netdev, &txreq, rc);
                continue;
            }

            xen_pv_printf(&netdev->xendev, 3,
                          "tx packet ref %d, off %d, len %d, flags 0x%x%s%s%s%s\n",
                          txreq.gref, txreq.offset, txreq.size, txreq.flags,
                          (txreq.flags & NETTXF_csum_blank)     ? " csum_blank"     : "",
                          (txreq.flags & NETTXF_data_validated) ? " data_validated" : "",
                          (txreq.flags & NETTXF_more_data)      ? " more_data"      : "",
                          (txreq.flags & NETTXF_extra_info)     ? " extra_info"     : "");

            page = xen_be_map_grant_ref(&netdev->xendev, txreq.gref,
                                        PROT_READ);
            if (page == NULL) {
                xen_pv_printf(&netdev->xendev, 0,
                              "error: tx gref dereference failed (%d)\n",
                             txreq.gref);
                net_tx_error(netdev, &txreq, rc);
                continue;
            }
            if (txreq.flags & NETTXF_csum_blank) {
                /* have read-only mapping -> can't fill checksum in-place */
                if (!tmpbuf) {
                    tmpbuf = g_malloc(XC_PAGE_SIZE);
                }
                memcpy(tmpbuf, page + txreq.offset, txreq.size);
                net_checksum_calculate(tmpbuf, txreq.size, CSUM_ALL);
                qemu_send_packet(qemu_get_queue(netdev->nic), tmpbuf,
                                 txreq.size);
            } else {
                qemu_send_packet(qemu_get_queue(netdev->nic),
                                 page + txreq.offset, txreq.size);
            }
            xen_be_unmap_grant_ref(&netdev->xendev, page);
            net_tx_response(netdev, &txreq, NETIF_RSP_OKAY);
        }
        if (!netdev->tx_work) {
            break;
        }
        netdev->tx_work = 0;
    }
    g_free(tmpbuf);
}

/* ------------------------------------------------------------- */

static void net_rx_response(struct XenNetDev *netdev,
                            netif_rx_request_t *req, int8_t st,
                            uint16_t offset, uint16_t size,
                            uint16_t flags)
{
    RING_IDX i = netdev->rx_ring.rsp_prod_pvt;
    netif_rx_response_t *resp;
    int notify;

    resp = RING_GET_RESPONSE(&netdev->rx_ring, i);
    resp->offset     = offset;
    resp->flags      = flags;
    resp->id         = req->id;
    resp->status     = (int16_t)size;
    if (st < 0) {
        resp->status = (int16_t)st;
    }

    xen_pv_printf(&netdev->xendev, 3,
                  "rx response: idx %d, status %d, flags 0x%x\n",
                  i, resp->status, resp->flags);

    netdev->rx_ring.rsp_prod_pvt = ++i;
    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&netdev->rx_ring, notify);
    if (notify) {
        xen_pv_send_notify(&netdev->xendev);
    }
}

#define NET_IP_ALIGN 2

static ssize_t net_rx_packet(NetClientState *nc, const uint8_t *buf, size_t size)
{
    struct XenNetDev *netdev = qemu_get_nic_opaque(nc);
    netif_rx_request_t rxreq;
    RING_IDX rc, rp;
    void *page;

    if (netdev->xendev.be_state != XenbusStateConnected) {
        return -1;
    }

    rc = netdev->rx_ring.req_cons;
    rp = netdev->rx_ring.sring->req_prod;
    xen_rmb(); /* Ensure we see queued requests up to 'rp'. */

    if (rc == rp || RING_REQUEST_CONS_OVERFLOW(&netdev->rx_ring, rc)) {
        return 0;
    }
    if (size > XC_PAGE_SIZE - NET_IP_ALIGN) {
        xen_pv_printf(&netdev->xendev, 0, "packet too big (%lu > %ld)",
                      (unsigned long)size, XC_PAGE_SIZE - NET_IP_ALIGN);
        return -1;
    }

    memcpy(&rxreq, RING_GET_REQUEST(&netdev->rx_ring, rc), sizeof(rxreq));
    netdev->rx_ring.req_cons = ++rc;

    page = xen_be_map_grant_ref(&netdev->xendev, rxreq.gref, PROT_WRITE);
    if (page == NULL) {
        xen_pv_printf(&netdev->xendev, 0,
                      "error: rx gref dereference failed (%d)\n",
                      rxreq.gref);
        net_rx_response(netdev, &rxreq, NETIF_RSP_ERROR, 0, 0, 0);
        return -1;
    }
    memcpy(page + NET_IP_ALIGN, buf, size);
    xen_be_unmap_grant_ref(&netdev->xendev, page);
    net_rx_response(netdev, &rxreq, NETIF_RSP_OKAY, NET_IP_ALIGN, size, 0);

    return size;
}

/* ------------------------------------------------------------- */

static NetClientInfo net_xen_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = net_rx_packet,
};

static int net_init(struct XenLegacyDevice *xendev)
{
    struct XenNetDev *netdev = container_of(xendev, struct XenNetDev, xendev);

    /* read xenstore entries */
    if (netdev->mac == NULL) {
        netdev->mac = xenstore_read_be_str(&netdev->xendev, "mac");
    }

    /* do we have all we need? */
    if (netdev->mac == NULL) {
        return -1;
    }

    if (net_parse_macaddr(netdev->conf.macaddr.a, netdev->mac) < 0) {
        return -1;
    }

    netdev->nic = qemu_new_nic(&net_xen_info, &netdev->conf,
                               "xen", NULL, netdev);

    snprintf(qemu_get_queue(netdev->nic)->info_str,
             sizeof(qemu_get_queue(netdev->nic)->info_str),
             "nic: xenbus vif macaddr=%s", netdev->mac);

    /* fill info */
    xenstore_write_be_int(&netdev->xendev, "feature-rx-copy", 1);
    xenstore_write_be_int(&netdev->xendev, "feature-rx-flip", 0);

    return 0;
}

static int net_connect(struct XenLegacyDevice *xendev)
{
    struct XenNetDev *netdev = container_of(xendev, struct XenNetDev, xendev);
    int rx_copy;

    if (xenstore_read_fe_int(&netdev->xendev, "tx-ring-ref",
                             &netdev->tx_ring_ref) == -1) {
        return -1;
    }
    if (xenstore_read_fe_int(&netdev->xendev, "rx-ring-ref",
                             &netdev->rx_ring_ref) == -1) {
        return 1;
    }
    if (xenstore_read_fe_int(&netdev->xendev, "event-channel",
                             &netdev->xendev.remote_port) == -1) {
        return -1;
    }

    if (xenstore_read_fe_int(&netdev->xendev, "request-rx-copy", &rx_copy) == -1) {
        rx_copy = 0;
    }
    if (rx_copy == 0) {
        xen_pv_printf(&netdev->xendev, 0,
                      "frontend doesn't support rx-copy.\n");
        return -1;
    }

    netdev->txs = xen_be_map_grant_ref(&netdev->xendev,
                                       netdev->tx_ring_ref,
                                       PROT_READ | PROT_WRITE);
    if (!netdev->txs) {
        return -1;
    }
    netdev->rxs = xen_be_map_grant_ref(&netdev->xendev,
                                       netdev->rx_ring_ref,
                                       PROT_READ | PROT_WRITE);
    if (!netdev->rxs) {
        xen_be_unmap_grant_ref(&netdev->xendev, netdev->txs);
        netdev->txs = NULL;
        return -1;
    }
    BACK_RING_INIT(&netdev->tx_ring, netdev->txs, XC_PAGE_SIZE);
    BACK_RING_INIT(&netdev->rx_ring, netdev->rxs, XC_PAGE_SIZE);

    xen_be_bind_evtchn(&netdev->xendev);

    xen_pv_printf(&netdev->xendev, 1, "ok: tx-ring-ref %d, rx-ring-ref %d, "
                  "remote port %d, local port %d\n",
                  netdev->tx_ring_ref, netdev->rx_ring_ref,
                  netdev->xendev.remote_port, netdev->xendev.local_port);

    net_tx_packets(netdev);
    return 0;
}

static void net_disconnect(struct XenLegacyDevice *xendev)
{
    struct XenNetDev *netdev = container_of(xendev, struct XenNetDev, xendev);

    xen_pv_unbind_evtchn(&netdev->xendev);

    if (netdev->txs) {
        xen_be_unmap_grant_ref(&netdev->xendev, netdev->txs);
        netdev->txs = NULL;
    }
    if (netdev->rxs) {
        xen_be_unmap_grant_ref(&netdev->xendev, netdev->rxs);
        netdev->rxs = NULL;
    }
}

static void net_event(struct XenLegacyDevice *xendev)
{
    struct XenNetDev *netdev = container_of(xendev, struct XenNetDev, xendev);
    net_tx_packets(netdev);
    qemu_flush_queued_packets(qemu_get_queue(netdev->nic));
}

static int net_free(struct XenLegacyDevice *xendev)
{
    struct XenNetDev *netdev = container_of(xendev, struct XenNetDev, xendev);

    if (netdev->nic) {
        qemu_del_nic(netdev->nic);
        netdev->nic = NULL;
    }
    g_free(netdev->mac);
    netdev->mac = NULL;
    return 0;
}

/* ------------------------------------------------------------- */

struct XenDevOps xen_netdev_ops = {
    .size       = sizeof(struct XenNetDev),
    .flags      = DEVOPS_FLAG_NEED_GNTDEV,
    .init       = net_init,
    .initialise    = net_connect,
    .event      = net_event,
    .disconnect = net_disconnect,
    .free       = net_free,
};
