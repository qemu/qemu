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
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/qemu-print.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "net/net.h"
#include "net/checksum.h"
#include "net/util.h"

#include "hw/xen/xen-backend.h"
#include "hw/xen/xen-bus-helper.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"

#include "hw/xen/interface/io/netif.h"
#include "hw/xen/interface/io/xs_wire.h"

#include "trace.h"

/* ------------------------------------------------------------- */

struct XenNetDev {
    struct XenDevice      xendev;  /* must be first */
    XenEventChannel       *event_channel;
    int                   dev;
    int                   tx_work;
    unsigned int          tx_ring_ref;
    unsigned int          rx_ring_ref;
    struct netif_tx_sring *txs;
    struct netif_rx_sring *rxs;
    netif_tx_back_ring_t  tx_ring;
    netif_rx_back_ring_t  rx_ring;
    NICConf               conf;
    NICState              *nic;
};

typedef struct XenNetDev XenNetDev;

#define TYPE_XEN_NET_DEVICE "xen-net-device"
OBJECT_DECLARE_SIMPLE_TYPE(XenNetDev, XEN_NET_DEVICE)

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
        xen_device_notify_event_channel(XEN_DEVICE(netdev),
                                        netdev->event_channel, NULL);
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

static bool net_tx_packets(struct XenNetDev *netdev)
{
    bool done_something = false;
    netif_tx_request_t txreq;
    RING_IDX rc, rp;
    void *page;
    void *tmpbuf = NULL;

    assert(bql_locked());

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
            done_something = true;

#if 1
            /* should not happen in theory, we don't announce the *
             * feature-{sg,gso,whatelse} flags in xenstore (yet?) */
            if (txreq.flags & NETTXF_extra_info) {
                qemu_log_mask(LOG_UNIMP, "vif%u: FIXME: extra info flag\n",
                              netdev->dev);
                net_tx_error(netdev, &txreq, rc);
                continue;
            }
            if (txreq.flags & NETTXF_more_data) {
                qemu_log_mask(LOG_UNIMP, "vif%u: FIXME: more data flag\n",
                              netdev->dev);
                net_tx_error(netdev, &txreq, rc);
                continue;
            }
#endif

            if (txreq.size < 14) {
                qemu_log_mask(LOG_GUEST_ERROR, "vif%u: bad packet size: %d\n",
                              netdev->dev, txreq.size);
                net_tx_error(netdev, &txreq, rc);
                continue;
            }

            if ((txreq.offset + txreq.size) > XEN_PAGE_SIZE) {
                qemu_log_mask(LOG_GUEST_ERROR, "vif%u: error: page crossing\n",
                              netdev->dev);
                net_tx_error(netdev, &txreq, rc);
                continue;
            }

            trace_xen_netdev_tx(netdev->dev, txreq.gref, txreq.offset,
                                txreq.size, txreq.flags,
                                (txreq.flags & NETTXF_csum_blank)     ? " csum_blank"     : "",
                                (txreq.flags & NETTXF_data_validated) ? " data_validated" : "",
                                (txreq.flags & NETTXF_more_data)      ? " more_data"      : "",
                                (txreq.flags & NETTXF_extra_info)     ? " extra_info"     : "");

            page = xen_device_map_grant_refs(&netdev->xendev, &txreq.gref, 1,
                                             PROT_READ, NULL);
            if (page == NULL) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "vif%u: tx gref dereference failed (%d)\n",
                              netdev->dev, txreq.gref);
                net_tx_error(netdev, &txreq, rc);
                continue;
            }
            if (txreq.flags & NETTXF_csum_blank) {
                /* have read-only mapping -> can't fill checksum in-place */
                if (!tmpbuf) {
                    tmpbuf = g_malloc(XEN_PAGE_SIZE);
                }
                memcpy(tmpbuf, page + txreq.offset, txreq.size);
                net_checksum_calculate(tmpbuf, txreq.size, CSUM_ALL);
                qemu_send_packet(qemu_get_queue(netdev->nic), tmpbuf,
                                 txreq.size);
            } else {
                qemu_send_packet(qemu_get_queue(netdev->nic),
                                 page + txreq.offset, txreq.size);
            }
            xen_device_unmap_grant_refs(&netdev->xendev, page, &txreq.gref, 1,
                                        NULL);
            net_tx_response(netdev, &txreq, NETIF_RSP_OKAY);
        }
        if (!netdev->tx_work) {
            break;
        }
        netdev->tx_work = 0;
    }
    g_free(tmpbuf);
    return done_something;
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

    trace_xen_netdev_rx(netdev->dev, i, resp->status, resp->flags);

    netdev->rx_ring.rsp_prod_pvt = ++i;
    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&netdev->rx_ring, notify);
    if (notify) {
        xen_device_notify_event_channel(XEN_DEVICE(netdev),
                                        netdev->event_channel, NULL);
    }
}

#define NET_IP_ALIGN 2

static ssize_t net_rx_packet(NetClientState *nc, const uint8_t *buf, size_t size)
{
    struct XenNetDev *netdev = qemu_get_nic_opaque(nc);
    netif_rx_request_t rxreq;
    RING_IDX rc, rp;
    void *page;

    assert(bql_locked());

    if (xen_device_backend_get_state(&netdev->xendev) != XenbusStateConnected) {
        return -1;
    }

    rc = netdev->rx_ring.req_cons;
    rp = netdev->rx_ring.sring->req_prod;
    xen_rmb(); /* Ensure we see queued requests up to 'rp'. */

    if (rc == rp || RING_REQUEST_CONS_OVERFLOW(&netdev->rx_ring, rc)) {
        return 0;
    }
    if (size > XEN_PAGE_SIZE - NET_IP_ALIGN) {
        qemu_log_mask(LOG_GUEST_ERROR, "vif%u: packet too big (%lu > %ld)",
                      netdev->dev, (unsigned long)size,
                      XEN_PAGE_SIZE - NET_IP_ALIGN);
        return -1;
    }

    memcpy(&rxreq, RING_GET_REQUEST(&netdev->rx_ring, rc), sizeof(rxreq));
    netdev->rx_ring.req_cons = ++rc;

    page = xen_device_map_grant_refs(&netdev->xendev, &rxreq.gref, 1,
                                     PROT_WRITE, NULL);
    if (page == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "vif%u: rx gref dereference failed (%d)\n",
                      netdev->dev, rxreq.gref);
        net_rx_response(netdev, &rxreq, NETIF_RSP_ERROR, 0, 0, 0);
        return -1;
    }
    memcpy(page + NET_IP_ALIGN, buf, size);
    xen_device_unmap_grant_refs(&netdev->xendev, page, &rxreq.gref, 1, NULL);
    net_rx_response(netdev, &rxreq, NETIF_RSP_OKAY, NET_IP_ALIGN, size, 0);

    return size;
}

/* ------------------------------------------------------------- */

static NetClientInfo net_xen_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = net_rx_packet,
};

static void xen_netdev_realize(XenDevice *xendev, Error **errp)
{
    ERRP_GUARD();
    XenNetDev *netdev = XEN_NET_DEVICE(xendev);
    NetClientState *nc;

    qemu_macaddr_default_if_unset(&netdev->conf.macaddr);

    xen_device_frontend_printf(xendev, "mac", "%02x:%02x:%02x:%02x:%02x:%02x",
                               netdev->conf.macaddr.a[0],
                               netdev->conf.macaddr.a[1],
                               netdev->conf.macaddr.a[2],
                               netdev->conf.macaddr.a[3],
                               netdev->conf.macaddr.a[4],
                               netdev->conf.macaddr.a[5]);

    netdev->nic = qemu_new_nic(&net_xen_info, &netdev->conf,
                               object_get_typename(OBJECT(xendev)),
                               DEVICE(xendev)->id,
                               &xendev->qdev.mem_reentrancy_guard, netdev);

    nc = qemu_get_queue(netdev->nic);
    qemu_format_nic_info_str(nc, netdev->conf.macaddr.a);

    /* fill info */
    xen_device_backend_printf(xendev, "feature-rx-copy", "%u", 1);
    xen_device_backend_printf(xendev, "feature-rx-flip", "%u", 0);

    trace_xen_netdev_realize(netdev->dev, nc->info_str, nc->peer ?
                             nc->peer->name : "(none)");
}

static bool net_event(void *_xendev)
{
    XenNetDev *netdev = XEN_NET_DEVICE(_xendev);
    bool done_something;

    done_something = net_tx_packets(netdev);
    qemu_flush_queued_packets(qemu_get_queue(netdev->nic));
    return done_something;
}

static bool xen_netdev_connect(XenDevice *xendev, Error **errp)
{
    ERRP_GUARD();
    XenNetDev *netdev = XEN_NET_DEVICE(xendev);
    unsigned int port, rx_copy;

    assert(bql_locked());

    if (xen_device_frontend_scanf(xendev, "tx-ring-ref", "%u",
                                  &netdev->tx_ring_ref) != 1) {
        error_setg(errp, "failed to read tx-ring-ref");
        return false;
    }

    if (xen_device_frontend_scanf(xendev, "rx-ring-ref", "%u",
                                  &netdev->rx_ring_ref) != 1) {
        error_setg(errp, "failed to read rx-ring-ref");
        return false;
    }

    if (xen_device_frontend_scanf(xendev, "event-channel", "%u",
                                  &port) != 1) {
        error_setg(errp, "failed to read event-channel");
        return false;
    }

    if (xen_device_frontend_scanf(xendev, "request-rx-copy", "%u",
                                  &rx_copy) != 1) {
        rx_copy = 0;
    }
    if (rx_copy == 0) {
        error_setg(errp, "frontend doesn't support rx-copy");
        return false;
    }

    netdev->txs = xen_device_map_grant_refs(xendev,
                                            &netdev->tx_ring_ref, 1,
                                            PROT_READ | PROT_WRITE,
                                            errp);
    if (!netdev->txs) {
        error_prepend(errp, "failed to map tx grant ref: ");
        return false;
    }

    netdev->rxs = xen_device_map_grant_refs(xendev,
                                            &netdev->rx_ring_ref, 1,
                                            PROT_READ | PROT_WRITE,
                                            errp);
    if (!netdev->rxs) {
        error_prepend(errp, "failed to map rx grant ref: ");
        return false;
    }

    BACK_RING_INIT(&netdev->tx_ring, netdev->txs, XEN_PAGE_SIZE);
    BACK_RING_INIT(&netdev->rx_ring, netdev->rxs, XEN_PAGE_SIZE);

    netdev->event_channel = xen_device_bind_event_channel(xendev, port,
                                                          net_event,
                                                          netdev,
                                                          errp);
    if (!netdev->event_channel) {
        return false;
    }

    trace_xen_netdev_connect(netdev->dev, netdev->tx_ring_ref,
                             netdev->rx_ring_ref, port);

    net_tx_packets(netdev);
    return true;
}

static void xen_netdev_disconnect(XenDevice *xendev, Error **errp)
{
    XenNetDev *netdev = XEN_NET_DEVICE(xendev);

    trace_xen_netdev_disconnect(netdev->dev);

    assert(bql_locked());

    netdev->tx_ring.sring = NULL;
    netdev->rx_ring.sring = NULL;

    if (netdev->event_channel) {
        xen_device_unbind_event_channel(xendev, netdev->event_channel,
                                        errp);
        netdev->event_channel = NULL;
    }
    if (netdev->txs) {
        xen_device_unmap_grant_refs(xendev, netdev->txs,
                                    &netdev->tx_ring_ref, 1, errp);
        netdev->txs = NULL;
    }
    if (netdev->rxs) {
        xen_device_unmap_grant_refs(xendev, netdev->rxs,
                                    &netdev->rx_ring_ref, 1, errp);
        netdev->rxs = NULL;
    }
}

/* -------------------------------------------------------------------- */


static void xen_netdev_frontend_changed(XenDevice *xendev,
                                       enum xenbus_state frontend_state,
                                       Error **errp)
{
    ERRP_GUARD();
    enum xenbus_state backend_state = xen_device_backend_get_state(xendev);

    trace_xen_netdev_frontend_changed(xendev->name, frontend_state);

    switch (frontend_state) {
    case XenbusStateConnected:
        if (backend_state == XenbusStateConnected) {
            break;
        }

        xen_netdev_disconnect(xendev, errp);
        if (*errp) {
            break;
        }

        if (!xen_netdev_connect(xendev, errp)) {
            xen_netdev_disconnect(xendev, NULL);
            xen_device_backend_set_state(xendev, XenbusStateClosing);
            break;
        }

        xen_device_backend_set_state(xendev, XenbusStateConnected);
        break;

    case XenbusStateClosing:
        xen_device_backend_set_state(xendev, XenbusStateClosing);
        break;

    case XenbusStateClosed:
    case XenbusStateUnknown:
        xen_netdev_disconnect(xendev, errp);
        if (*errp) {
            break;
        }

        xen_device_backend_set_state(xendev, XenbusStateClosed);
        break;

    case XenbusStateInitialised:
        /*
         * Linux netback does nothing on the frontend going (back) to
         * XenbusStateInitialised, so do the same here.
         */
    default:
        break;
    }
}

static char *xen_netdev_get_name(XenDevice *xendev, Error **errp)
{
    XenNetDev *netdev = XEN_NET_DEVICE(xendev);

    if (netdev->dev == -1) {
        XenBus *xenbus = XEN_BUS(qdev_get_parent_bus(DEVICE(xendev)));
        char fe_path[XENSTORE_ABS_PATH_MAX + 1];
        int idx = (xen_mode == XEN_EMULATE) ? 0 : 1;
        char *value;

        /* Theoretically we could go up to INT_MAX here but that's overkill */
        while (idx < 100) {
            snprintf(fe_path, sizeof(fe_path),
                     "/local/domain/%u/device/vif/%u",
                     xendev->frontend_id, idx);
            value = qemu_xen_xs_read(xenbus->xsh, XBT_NULL, fe_path, NULL);
            if (!value) {
                if (errno == ENOENT) {
                    netdev->dev = idx;
                    goto found;
                }
                error_setg(errp, "cannot read %s: %s", fe_path,
                           strerror(errno));
                return NULL;
            }
            free(value);
            idx++;
        }
        error_setg(errp, "cannot find device index for netdev device");
        return NULL;
    }
 found:
    return g_strdup_printf("%u", netdev->dev);
}

static void xen_netdev_unrealize(XenDevice *xendev)
{
    XenNetDev *netdev = XEN_NET_DEVICE(xendev);

    trace_xen_netdev_unrealize(netdev->dev);

    /* Disconnect from the frontend in case this has not already happened */
    xen_netdev_disconnect(xendev, NULL);

    if (netdev->nic) {
        qemu_del_nic(netdev->nic);
    }
}

/* ------------------------------------------------------------- */

static Property xen_netdev_properties[] = {
    DEFINE_NIC_PROPERTIES(XenNetDev, conf),
    DEFINE_PROP_INT32("idx", XenNetDev, dev, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void xen_netdev_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dev_class = DEVICE_CLASS(class);
    XenDeviceClass *xendev_class = XEN_DEVICE_CLASS(class);

    xendev_class->backend = "qnic";
    xendev_class->device = "vif";
    xendev_class->get_name = xen_netdev_get_name;
    xendev_class->realize = xen_netdev_realize;
    xendev_class->frontend_changed = xen_netdev_frontend_changed;
    xendev_class->unrealize = xen_netdev_unrealize;
    set_bit(DEVICE_CATEGORY_NETWORK, dev_class->categories);
    dev_class->user_creatable = true;

    device_class_set_props(dev_class, xen_netdev_properties);
}

static const TypeInfo xen_net_type_info = {
    .name = TYPE_XEN_NET_DEVICE,
    .parent = TYPE_XEN_DEVICE,
    .instance_size = sizeof(XenNetDev),
    .class_init = xen_netdev_class_init,
};

static void xen_net_register_types(void)
{
    type_register_static(&xen_net_type_info);
}

type_init(xen_net_register_types)

/* Called to instantiate a XenNetDev when the backend is detected. */
static void xen_net_device_create(XenBackendInstance *backend,
                                  QDict *opts, Error **errp)
{
    ERRP_GUARD();
    XenBus *xenbus = xen_backend_get_bus(backend);
    const char *name = xen_backend_get_name(backend);
    XenDevice *xendev = NULL;
    unsigned long number;
    const char *macstr;
    XenNetDev *net;
    MACAddr mac;

    if (qemu_strtoul(name, NULL, 10, &number) || number >= INT_MAX) {
        error_setg(errp, "failed to parse name '%s'", name);
        goto fail;
    }

    trace_xen_netdev_create(number);

    macstr = qdict_get_try_str(opts, "mac");
    if (macstr == NULL) {
        error_setg(errp, "no MAC address found");
        goto fail;
    }

    if (net_parse_macaddr(mac.a, macstr) < 0) {
        error_setg(errp, "failed to parse MAC address");
        goto fail;
    }

    xendev = XEN_DEVICE(qdev_new(TYPE_XEN_NET_DEVICE));
    net = XEN_NET_DEVICE(xendev);

    net->dev = number;
    memcpy(&net->conf.macaddr, &mac, sizeof(mac));

    if (qdev_realize_and_unref(DEVICE(xendev), BUS(xenbus), errp)) {
        xen_backend_set_device(backend, xendev);
        return;
    }

    error_prepend(errp, "realization of net device %lu failed: ",
                  number);

 fail:
    if (xendev) {
        object_unparent(OBJECT(xendev));
    }
}

static void xen_net_device_destroy(XenBackendInstance *backend,
                                   Error **errp)
{
    ERRP_GUARD();
    XenDevice *xendev = xen_backend_get_device(backend);
    XenNetDev *netdev = XEN_NET_DEVICE(xendev);

    trace_xen_netdev_destroy(netdev->dev);

    object_unparent(OBJECT(xendev));
}

static const XenBackendInfo xen_net_backend_info  = {
    .type = "qnic",
    .create = xen_net_device_create,
    .destroy = xen_net_device_destroy,
};

static void xen_net_register_backend(void)
{
    xen_backend_register(&xen_net_backend_info);
}

xen_backend_init(xen_net_register_backend);
