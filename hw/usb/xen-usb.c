/*
 *  xen paravirt usb device backend
 *
 *  (c) Juergen Gross <jgross@suse.com>
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
#include <libusb.h>
#include <sys/user.h>

#include "qemu/config-file.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "hw/sysbus.h"
#include "hw/usb.h"
#include "hw/xen/xen-legacy-backend.h"
#include "monitor/qdev.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

#include "hw/xen/interface/io/usbif.h"

/*
 * Check for required support of usbif.h: USBIF_SHORT_NOT_OK was the last
 * macro added we rely on.
 */
#ifdef USBIF_SHORT_NOT_OK

#define TR(xendev, lvl, fmt, args...)                               \
    {                                                               \
        struct timeval tv;                                          \
                                                                    \
        gettimeofday(&tv, NULL);                                    \
        xen_pv_printf(xendev, lvl, "%8ld.%06ld xen-usb(%s):" fmt,   \
                      tv.tv_sec, tv.tv_usec, __func__, ##args);     \
    }
#define TR_BUS(xendev, fmt, args...) TR(xendev, 2, fmt, ##args)
#define TR_REQ(xendev, fmt, args...) TR(xendev, 3, fmt, ##args)

#define USBBACK_MAXPORTS        USBIF_PIPE_PORT_MASK
#define USB_DEV_ADDR_SIZE       (USBIF_PIPE_DEV_MASK + 1)

/* USB wire protocol: structure describing control request parameter. */
struct usbif_ctrlrequest {
    uint8_t    bRequestType;
    uint8_t    bRequest;
    uint16_t   wValue;
    uint16_t   wIndex;
    uint16_t   wLength;
};

struct usbback_info;
struct usbback_req;

struct usbback_stub {
    USBDevice     *dev;
    USBPort       port;
    unsigned int  speed;
    bool          attached;
    QTAILQ_HEAD(, usbback_req) submit_q;
};

struct usbback_req {
    struct usbback_info      *usbif;
    struct usbback_stub      *stub;
    struct usbif_urb_request req;
    USBPacket                packet;

    unsigned int             nr_buffer_segs; /* # of transfer_buffer segments */
    unsigned int             nr_extra_segs;  /* # of iso_frame_desc segments  */

    QTAILQ_ENTRY(usbback_req) q;

    void                     *buffer;
    void                     *isoc_buffer;
    struct libusb_transfer   *xfer;

    bool                     cancelled;
};

struct usbback_hotplug {
    QSIMPLEQ_ENTRY(usbback_hotplug) q;
    unsigned                 port;
};

struct usbback_info {
    struct XenLegacyDevice         xendev;  /* must be first */
    USBBus                   bus;
    void                     *urb_sring;
    void                     *conn_sring;
    struct usbif_urb_back_ring urb_ring;
    struct usbif_conn_back_ring conn_ring;
    int                      num_ports;
    int                      usb_ver;
    bool                     ring_error;
    QTAILQ_HEAD(, usbback_req) req_free_q;
    QSIMPLEQ_HEAD(, usbback_hotplug) hotplug_q;
    struct usbback_stub      ports[USBBACK_MAXPORTS];
    struct usbback_stub      *addr_table[USB_DEV_ADDR_SIZE];
    QEMUBH                   *bh;
};

static struct usbback_req *usbback_get_req(struct usbback_info *usbif)
{
    struct usbback_req *usbback_req;

    if (QTAILQ_EMPTY(&usbif->req_free_q)) {
        usbback_req = g_new0(struct usbback_req, 1);
    } else {
        usbback_req = QTAILQ_FIRST(&usbif->req_free_q);
        QTAILQ_REMOVE(&usbif->req_free_q, usbback_req, q);
    }
    return usbback_req;
}

static void usbback_put_req(struct usbback_req *usbback_req)
{
    struct usbback_info *usbif;

    usbif = usbback_req->usbif;
    memset(usbback_req, 0, sizeof(*usbback_req));
    QTAILQ_INSERT_HEAD(&usbif->req_free_q, usbback_req, q);
}

static int usbback_gnttab_map(struct usbback_req *usbback_req)
{
    unsigned int nr_segs, i, prot;
    uint32_t ref[USBIF_MAX_SEGMENTS_PER_REQUEST];
    struct usbback_info *usbif = usbback_req->usbif;
    struct XenLegacyDevice *xendev = &usbif->xendev;
    struct usbif_request_segment *seg;
    void *addr;

    nr_segs = usbback_req->nr_buffer_segs + usbback_req->nr_extra_segs;
    if (!nr_segs) {
        return 0;
    }

    if (nr_segs > USBIF_MAX_SEGMENTS_PER_REQUEST) {
        xen_pv_printf(xendev, 0, "bad number of segments in request (%d)\n",
                      nr_segs);
        return -EINVAL;
    }

    for (i = 0; i < nr_segs; i++) {
        if ((unsigned)usbback_req->req.seg[i].offset +
            (unsigned)usbback_req->req.seg[i].length > XC_PAGE_SIZE) {
            xen_pv_printf(xendev, 0, "segment crosses page boundary\n");
            return -EINVAL;
        }
    }

    if (usbback_req->nr_buffer_segs) {
        prot = PROT_READ;
        if (usbif_pipein(usbback_req->req.pipe)) {
                prot |= PROT_WRITE;
        }
        for (i = 0; i < usbback_req->nr_buffer_segs; i++) {
            ref[i] = usbback_req->req.seg[i].gref;
        }
        usbback_req->buffer =
            xen_be_map_grant_refs(xendev, ref, usbback_req->nr_buffer_segs,
                                  prot);

        if (!usbback_req->buffer) {
            return -ENOMEM;
        }

        for (i = 0; i < usbback_req->nr_buffer_segs; i++) {
            seg = usbback_req->req.seg + i;
            addr = usbback_req->buffer + i * XC_PAGE_SIZE + seg->offset;
            qemu_iovec_add(&usbback_req->packet.iov, addr, seg->length);
        }
    }

    if (!usbif_pipeisoc(usbback_req->req.pipe)) {
        return 0;
    }

    /*
     * Right now isoc requests are not supported.
     * Prepare supporting those by doing the work needed on the guest
     * interface side.
     */

    if (!usbback_req->nr_extra_segs) {
        xen_pv_printf(xendev, 0, "iso request without descriptor segments\n");
        return -EINVAL;
    }

    prot = PROT_READ | PROT_WRITE;
    for (i = 0; i < usbback_req->nr_extra_segs; i++) {
        ref[i] = usbback_req->req.seg[i + usbback_req->req.nr_buffer_segs].gref;
    }
    usbback_req->isoc_buffer =
        xen_be_map_grant_refs(xendev, ref, usbback_req->nr_extra_segs,
                              prot);

    if (!usbback_req->isoc_buffer) {
        return -ENOMEM;
    }

    return 0;
}

static int usbback_init_packet(struct usbback_req *usbback_req)
{
    struct XenLegacyDevice *xendev = &usbback_req->usbif->xendev;
    USBPacket *packet = &usbback_req->packet;
    USBDevice *dev = usbback_req->stub->dev;
    USBEndpoint *ep;
    unsigned int pid, ep_nr;
    bool sok;
    int ret = 0;

    qemu_iovec_init(&packet->iov, USBIF_MAX_SEGMENTS_PER_REQUEST);
    pid = usbif_pipein(usbback_req->req.pipe) ? USB_TOKEN_IN : USB_TOKEN_OUT;
    ep_nr = usbif_pipeendpoint(usbback_req->req.pipe);
    sok = !!(usbback_req->req.transfer_flags & USBIF_SHORT_NOT_OK);
    if (usbif_pipectrl(usbback_req->req.pipe)) {
        ep_nr = 0;
        sok = false;
    }
    ep = usb_ep_get(dev, pid, ep_nr);
    usb_packet_setup(packet, pid, ep, 0, 1, sok, true);

    switch (usbif_pipetype(usbback_req->req.pipe)) {
    case USBIF_PIPE_TYPE_ISOC:
        TR_REQ(xendev, "iso transfer %s: buflen: %x, %d frames\n",
               (pid == USB_TOKEN_IN) ? "in" : "out",
               usbback_req->req.buffer_length,
               usbback_req->req.u.isoc.nr_frame_desc_segs);
        ret = -EINVAL;  /* isoc not implemented yet */
        break;

    case USBIF_PIPE_TYPE_INT:
        TR_REQ(xendev, "int transfer %s: buflen: %x\n",
               (pid == USB_TOKEN_IN) ? "in" : "out",
               usbback_req->req.buffer_length);
        break;

    case USBIF_PIPE_TYPE_CTRL:
        packet->parameter = *(uint64_t *)usbback_req->req.u.ctrl;
        TR_REQ(xendev, "ctrl parameter: %"PRIx64", buflen: %x\n",
               packet->parameter,
               usbback_req->req.buffer_length);
        break;

    case USBIF_PIPE_TYPE_BULK:
        TR_REQ(xendev, "bulk transfer %s: buflen: %x\n",
               (pid == USB_TOKEN_IN) ? "in" : "out",
               usbback_req->req.buffer_length);
        break;
    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

static void usbback_do_response(struct usbback_req *usbback_req, int32_t status,
                                int32_t actual_length, int32_t error_count)
{
    struct usbback_info *usbif;
    struct usbif_urb_response *res;
    struct XenLegacyDevice *xendev;
    unsigned int notify;

    usbif = usbback_req->usbif;
    xendev = &usbif->xendev;

    TR_REQ(xendev, "id %d, status %d, length %d, errcnt %d\n",
           usbback_req->req.id, status, actual_length, error_count);

    if (usbback_req->packet.iov.iov) {
        qemu_iovec_destroy(&usbback_req->packet.iov);
    }

    if (usbback_req->buffer) {
        xen_be_unmap_grant_refs(xendev, usbback_req->buffer,
                                usbback_req->nr_buffer_segs);
        usbback_req->buffer = NULL;
    }

    if (usbback_req->isoc_buffer) {
        xen_be_unmap_grant_refs(xendev, usbback_req->isoc_buffer,
                                usbback_req->nr_extra_segs);
        usbback_req->isoc_buffer = NULL;
    }

    if (usbif->urb_sring) {
        res = RING_GET_RESPONSE(&usbif->urb_ring, usbif->urb_ring.rsp_prod_pvt);
        res->id = usbback_req->req.id;
        res->status = status;
        res->actual_length = actual_length;
        res->error_count = error_count;
        res->start_frame = 0;
        usbif->urb_ring.rsp_prod_pvt++;
        RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&usbif->urb_ring, notify);

        if (notify) {
            xen_pv_send_notify(xendev);
        }
    }

    if (!usbback_req->cancelled)
        usbback_put_req(usbback_req);
}

static void usbback_do_response_ret(struct usbback_req *usbback_req,
                                    int32_t status)
{
    usbback_do_response(usbback_req, status, 0, 0);
}

static int32_t usbback_xlat_status(int status)
{
    switch (status) {
    case USB_RET_SUCCESS:
        return 0;
    case USB_RET_NODEV:
        return -ENODEV;
    case USB_RET_STALL:
        return -EPIPE;
    case USB_RET_BABBLE:
        return -EOVERFLOW;
    case USB_RET_IOERROR:
        return -EPROTO;
    }

    return -ESHUTDOWN;
}

static void usbback_packet_complete(struct usbback_req *usbback_req)
{
    USBPacket *packet = &usbback_req->packet;
    int32_t status;

    QTAILQ_REMOVE(&usbback_req->stub->submit_q, usbback_req, q);

    status = usbback_xlat_status(packet->status);
    usbback_do_response(usbback_req, status, packet->actual_length, 0);
}

static void usbback_set_address(struct usbback_info *usbif,
                                struct usbback_stub *stub,
                                unsigned int cur_addr, unsigned int new_addr)
{
    if (cur_addr) {
        usbif->addr_table[cur_addr] = NULL;
    }
    if (new_addr) {
        usbif->addr_table[new_addr] = stub;
    }
}

static void usbback_cancel_req(struct usbback_req *usbback_req)
{
    if (usb_packet_is_inflight(&usbback_req->packet)) {
        usb_cancel_packet(&usbback_req->packet);
        QTAILQ_REMOVE(&usbback_req->stub->submit_q, usbback_req, q);
        usbback_req->cancelled = true;
        usbback_do_response_ret(usbback_req, -EPROTO);
    }
}

static void usbback_process_unlink_req(struct usbback_req *usbback_req)
{
    struct usbback_info *usbif;
    struct usbback_req *unlink_req;
    unsigned int id, devnum;
    int ret;

    usbif = usbback_req->usbif;
    ret = 0;
    id = usbback_req->req.u.unlink.unlink_id;
    TR_REQ(&usbif->xendev, "unlink id %d\n", id);
    devnum = usbif_pipedevice(usbback_req->req.pipe);
    if (unlikely(devnum == 0)) {
        usbback_req->stub = usbif->ports +
                            usbif_pipeportnum(usbback_req->req.pipe) - 1;
        if (unlikely(!usbback_req->stub)) {
            ret = -ENODEV;
            goto fail_response;
        }
    } else {
        if (unlikely(!usbif->addr_table[devnum])) {
            ret = -ENODEV;
            goto fail_response;
        }
        usbback_req->stub = usbif->addr_table[devnum];
    }

    QTAILQ_FOREACH(unlink_req, &usbback_req->stub->submit_q, q) {
        if (unlink_req->req.id == id) {
            usbback_cancel_req(unlink_req);
            break;
        }
    }

fail_response:
    usbback_do_response_ret(usbback_req, ret);
}

/*
 * Checks whether a request can be handled at once or should be forwarded
 * to the usb framework.
 * Return value is:
 * 0 in case of usb framework is needed
 * 1 in case of local handling (no error)
 * The request response has been queued already if return value not 0.
 */
static int usbback_check_and_submit(struct usbback_req *usbback_req)
{
    struct usbback_info *usbif;
    unsigned int devnum;
    struct usbback_stub *stub;
    struct usbif_ctrlrequest *ctrl;
    int ret;
    uint16_t wValue;

    usbif = usbback_req->usbif;
    stub = NULL;
    devnum = usbif_pipedevice(usbback_req->req.pipe);
    ctrl = (struct usbif_ctrlrequest *)usbback_req->req.u.ctrl;
    wValue = le16_to_cpu(ctrl->wValue);

    /*
     * When the device is first connected or resetted, USB device has no
     * address. In this initial state, following requests are sent to device
     * address (#0),
     *
     *  1. GET_DESCRIPTOR (with Descriptor Type is "DEVICE") is sent,
     *     and OS knows what device is connected to.
     *
     *  2. SET_ADDRESS is sent, and then device has its address.
     *
     * In the next step, SET_CONFIGURATION is sent to addressed device, and
     * then the device is finally ready to use.
     */
    if (unlikely(devnum == 0)) {
        stub = usbif->ports + usbif_pipeportnum(usbback_req->req.pipe) - 1;
        if (!stub->dev || !stub->attached) {
            ret = -ENODEV;
            goto do_response;
        }

        switch (ctrl->bRequest) {
        case USB_REQ_GET_DESCRIPTOR:
            /*
             * GET_DESCRIPTOR request to device #0.
             * through normal transfer.
             */
            TR_REQ(&usbif->xendev, "devnum 0 GET_DESCRIPTOR\n");
            usbback_req->stub = stub;
            return 0;
        case USB_REQ_SET_ADDRESS:
            /*
             * SET_ADDRESS request to device #0.
             * add attached device to addr_table.
             */
            TR_REQ(&usbif->xendev, "devnum 0 SET_ADDRESS\n");
            usbback_set_address(usbif, stub, 0, wValue);
            ret = 0;
            break;
        default:
            ret = -EINVAL;
            break;
        }
        goto do_response;
    }

    if (unlikely(!usbif->addr_table[devnum])) {
            ret = -ENODEV;
            goto do_response;
    }
    usbback_req->stub = usbif->addr_table[devnum];

    /*
     * Check special request
     */
    if (ctrl->bRequest != USB_REQ_SET_ADDRESS) {
        return 0;
    }

    /*
     * SET_ADDRESS request to addressed device.
     * change addr or remove from addr_table.
     */
    usbback_set_address(usbif, usbback_req->stub, devnum, wValue);
    ret = 0;

do_response:
    usbback_do_response_ret(usbback_req, ret);
    return 1;
}

static void usbback_dispatch(struct usbback_req *usbback_req)
{
    int ret;
    unsigned int devnum;
    struct usbback_info *usbif;

    usbif = usbback_req->usbif;

    TR_REQ(&usbif->xendev, "start req_id %d pipe %08x\n", usbback_req->req.id,
           usbback_req->req.pipe);

    /* unlink request */
    if (unlikely(usbif_pipeunlink(usbback_req->req.pipe))) {
        usbback_process_unlink_req(usbback_req);
        return;
    }

    if (usbif_pipectrl(usbback_req->req.pipe)) {
        if (usbback_check_and_submit(usbback_req)) {
            return;
        }
    } else {
        devnum = usbif_pipedevice(usbback_req->req.pipe);
        usbback_req->stub = usbif->addr_table[devnum];

        if (!usbback_req->stub || !usbback_req->stub->attached) {
            ret = -ENODEV;
            goto fail_response;
        }
    }

    QTAILQ_INSERT_TAIL(&usbback_req->stub->submit_q, usbback_req, q);

    usbback_req->nr_buffer_segs = usbback_req->req.nr_buffer_segs;
    usbback_req->nr_extra_segs = usbif_pipeisoc(usbback_req->req.pipe) ?
                                 usbback_req->req.u.isoc.nr_frame_desc_segs : 0;

    ret = usbback_init_packet(usbback_req);
    if (ret) {
        xen_pv_printf(&usbif->xendev, 0, "invalid request\n");
        ret = -ESHUTDOWN;
        goto fail_free_urb;
    }

    ret = usbback_gnttab_map(usbback_req);
    if (ret) {
        xen_pv_printf(&usbif->xendev, 0, "invalid buffer, ret=%d\n", ret);
        ret = -ESHUTDOWN;
        goto fail_free_urb;
    }

    usb_handle_packet(usbback_req->stub->dev, &usbback_req->packet);
    if (usbback_req->packet.status != USB_RET_ASYNC) {
        usbback_packet_complete(usbback_req);
    }
    return;

fail_free_urb:
    QTAILQ_REMOVE(&usbback_req->stub->submit_q, usbback_req, q);

fail_response:
    usbback_do_response_ret(usbback_req, ret);
}

static void usbback_hotplug_notify(struct usbback_info *usbif)
{
    struct usbif_conn_back_ring *ring = &usbif->conn_ring;
    struct usbif_conn_request req;
    struct usbif_conn_response *res;
    struct usbback_hotplug *usb_hp;
    unsigned int notify;

    if (!usbif->conn_sring) {
        return;
    }

    /* Check for full ring. */
    if ((RING_SIZE(ring) - ring->rsp_prod_pvt - ring->req_cons) == 0) {
        xen_pv_send_notify(&usbif->xendev);
        return;
    }

    usb_hp = QSIMPLEQ_FIRST(&usbif->hotplug_q);
    QSIMPLEQ_REMOVE_HEAD(&usbif->hotplug_q, q);

    RING_COPY_REQUEST(ring, ring->req_cons, &req);
    ring->req_cons++;
    ring->sring->req_event = ring->req_cons + 1;

    res = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
    res->id = req.id;
    res->portnum = usb_hp->port;
    res->speed = usbif->ports[usb_hp->port - 1].speed;
    ring->rsp_prod_pvt++;
    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(ring, notify);

    if (notify) {
        xen_pv_send_notify(&usbif->xendev);
    }

    TR_BUS(&usbif->xendev, "hotplug port %d speed %d\n", usb_hp->port,
           res->speed);

    g_free(usb_hp);

    if (!QSIMPLEQ_EMPTY(&usbif->hotplug_q)) {
        qemu_bh_schedule(usbif->bh);
    }
}

static void usbback_bh(void *opaque)
{
    struct usbback_info *usbif;
    struct usbif_urb_back_ring *urb_ring;
    struct usbback_req *usbback_req;
    RING_IDX rc, rp;
    unsigned int more_to_do;

    usbif = opaque;
    if (usbif->ring_error) {
        return;
    }

    if (!QSIMPLEQ_EMPTY(&usbif->hotplug_q)) {
        usbback_hotplug_notify(usbif);
    }

    urb_ring = &usbif->urb_ring;
    rc = urb_ring->req_cons;
    rp = urb_ring->sring->req_prod;
    xen_rmb(); /* Ensure we see queued requests up to 'rp'. */

    if (RING_REQUEST_PROD_OVERFLOW(urb_ring, rp)) {
        rc = urb_ring->rsp_prod_pvt;
        xen_pv_printf(&usbif->xendev, 0, "domU provided bogus ring requests "
                      "(%#x - %#x = %u). Halting ring processing.\n",
                      rp, rc, rp - rc);
        usbif->ring_error = true;
        return;
    }

    while (rc != rp) {
        if (RING_REQUEST_CONS_OVERFLOW(urb_ring, rc)) {
            break;
        }
        usbback_req = usbback_get_req(usbif);

        RING_COPY_REQUEST(urb_ring, rc, &usbback_req->req);
        usbback_req->usbif = usbif;

        usbback_dispatch(usbback_req);

        urb_ring->req_cons = ++rc;
    }

    RING_FINAL_CHECK_FOR_REQUESTS(urb_ring, more_to_do);
    if (more_to_do) {
        qemu_bh_schedule(usbif->bh);
    }
}

static void usbback_hotplug_enq(struct usbback_info *usbif, unsigned port)
{
    struct usbback_hotplug *usb_hp;

    usb_hp = g_new0(struct usbback_hotplug, 1);
    usb_hp->port = port;
    QSIMPLEQ_INSERT_TAIL(&usbif->hotplug_q, usb_hp, q);
    usbback_hotplug_notify(usbif);
}

static void usbback_portid_drain(struct usbback_info *usbif, unsigned port)
{
    struct usbback_req *req, *tmp;
    bool sched = false;

    QTAILQ_FOREACH_SAFE(req, &usbif->ports[port - 1].submit_q, q, tmp) {
        usbback_cancel_req(req);
        sched = true;
    }

    if (sched) {
        qemu_bh_schedule(usbif->bh);
    }
}

static void usbback_portid_detach(struct usbback_info *usbif, unsigned port)
{
    if (!usbif->ports[port - 1].attached) {
        return;
    }

    usbif->ports[port - 1].speed = USBIF_SPEED_NONE;
    usbif->ports[port - 1].attached = false;
    usbback_portid_drain(usbif, port);
    usbback_hotplug_enq(usbif, port);
}

static void usbback_portid_remove(struct usbback_info *usbif, unsigned port)
{
    if (!usbif->ports[port - 1].dev) {
        return;
    }

    object_unparent(OBJECT(usbif->ports[port - 1].dev));
    usbif->ports[port - 1].dev = NULL;
    usbback_portid_detach(usbif, port);

    TR_BUS(&usbif->xendev, "port %d removed\n", port);
}

static void usbback_portid_add(struct usbback_info *usbif, unsigned port,
                               char *busid)
{
    unsigned speed;
    char *portname;
    Error *local_err = NULL;
    QDict *qdict;
    QemuOpts *opts;
    char *tmp;

    if (usbif->ports[port - 1].dev) {
        return;
    }

    portname = strchr(busid, '-');
    if (!portname) {
        xen_pv_printf(&usbif->xendev, 0, "device %s illegal specification\n",
                      busid);
        return;
    }
    portname++;

    qdict = qdict_new();
    qdict_put_str(qdict, "driver", "usb-host");
    tmp = g_strdup_printf("%s.0", usbif->xendev.qdev.id);
    qdict_put_str(qdict, "bus", tmp);
    g_free(tmp);
    tmp = g_strdup_printf("%s-%u", usbif->xendev.qdev.id, port);
    qdict_put_str(qdict, "id", tmp);
    g_free(tmp);
    qdict_put_int(qdict, "port", port);
    qdict_put_int(qdict, "hostbus", atoi(busid));
    qdict_put_str(qdict, "hostport", portname);
    opts = qemu_opts_from_qdict(qemu_find_opts("device"), qdict, &local_err);
    if (local_err) {
        goto err;
    }
    usbif->ports[port - 1].dev = USB_DEVICE(qdev_device_add(opts, &local_err));
    if (!usbif->ports[port - 1].dev) {
        goto err;
    }
    qobject_unref(qdict);
    speed = usbif->ports[port - 1].dev->speed;
    switch (speed) {
    case USB_SPEED_LOW:
        speed = USBIF_SPEED_LOW;
        break;
    case USB_SPEED_FULL:
        speed = USBIF_SPEED_FULL;
        break;
    case USB_SPEED_HIGH:
        speed = (usbif->usb_ver < USB_VER_USB20) ?
                USBIF_SPEED_NONE : USBIF_SPEED_HIGH;
        break;
    default:
        speed = USBIF_SPEED_NONE;
        break;
    }
    if (speed == USBIF_SPEED_NONE) {
        xen_pv_printf(&usbif->xendev, 0, "device %s wrong speed\n", busid);
        object_unparent(OBJECT(usbif->ports[port - 1].dev));
        usbif->ports[port - 1].dev = NULL;
        return;
    }
    usb_device_reset(usbif->ports[port - 1].dev);
    usbif->ports[port - 1].speed = speed;
    usbif->ports[port - 1].attached = true;
    QTAILQ_INIT(&usbif->ports[port - 1].submit_q);
    usbback_hotplug_enq(usbif, port);

    TR_BUS(&usbif->xendev, "port %d attached\n", port);
    return;

err:
    qobject_unref(qdict);
    xen_pv_printf(&usbif->xendev, 0, "device %s could not be opened\n", busid);
}

static void usbback_process_port(struct usbback_info *usbif, unsigned port)
{
    char node[8];
    char *busid;

    snprintf(node, sizeof(node), "port/%d", port);
    busid = xenstore_read_be_str(&usbif->xendev, node);
    if (busid == NULL) {
        xen_pv_printf(&usbif->xendev, 0, "xenstore_read %s failed\n", node);
        return;
    }

    /* Remove portid, if the port is not connected.  */
    if (strlen(busid) == 0) {
        usbback_portid_remove(usbif, port);
    } else {
        usbback_portid_add(usbif, port, busid);
    }

    g_free(busid);
}

static void usbback_disconnect(struct XenLegacyDevice *xendev)
{
    struct usbback_info *usbif;
    unsigned int i;

    TR_BUS(xendev, "start\n");

    usbif = container_of(xendev, struct usbback_info, xendev);

    xen_pv_unbind_evtchn(xendev);

    if (usbif->urb_sring) {
        xen_be_unmap_grant_ref(xendev, usbif->urb_sring);
        usbif->urb_sring = NULL;
    }
    if (usbif->conn_sring) {
        xen_be_unmap_grant_ref(xendev, usbif->conn_sring);
        usbif->conn_sring = NULL;
    }

    for (i = 0; i < usbif->num_ports; i++) {
        if (usbif->ports[i].dev) {
            usbback_portid_drain(usbif, i + 1);
        }
    }

    TR_BUS(xendev, "finished\n");
}

static int usbback_connect(struct XenLegacyDevice *xendev)
{
    struct usbback_info *usbif;
    struct usbif_urb_sring *urb_sring;
    struct usbif_conn_sring *conn_sring;
    int urb_ring_ref;
    int conn_ring_ref;
    unsigned int i, max_grants;

    TR_BUS(xendev, "start\n");

    /* max_grants: for each request and for the rings (request and connect). */
    max_grants = USBIF_MAX_SEGMENTS_PER_REQUEST * USB_URB_RING_SIZE + 2;
    xen_be_set_max_grant_refs(xendev, max_grants);

    usbif = container_of(xendev, struct usbback_info, xendev);

    if (xenstore_read_fe_int(xendev, "urb-ring-ref", &urb_ring_ref)) {
        xen_pv_printf(xendev, 0, "error reading urb-ring-ref\n");
        return -1;
    }
    if (xenstore_read_fe_int(xendev, "conn-ring-ref", &conn_ring_ref)) {
        xen_pv_printf(xendev, 0, "error reading conn-ring-ref\n");
        return -1;
    }
    if (xenstore_read_fe_int(xendev, "event-channel", &xendev->remote_port)) {
        xen_pv_printf(xendev, 0, "error reading event-channel\n");
        return -1;
    }

    usbif->urb_sring = xen_be_map_grant_ref(xendev, urb_ring_ref,
                                            PROT_READ | PROT_WRITE);
    usbif->conn_sring = xen_be_map_grant_ref(xendev, conn_ring_ref,
                                             PROT_READ | PROT_WRITE);
    if (!usbif->urb_sring || !usbif->conn_sring) {
        xen_pv_printf(xendev, 0, "error mapping rings\n");
        usbback_disconnect(xendev);
        return -1;
    }

    urb_sring = usbif->urb_sring;
    conn_sring = usbif->conn_sring;
    BACK_RING_INIT(&usbif->urb_ring, urb_sring, XC_PAGE_SIZE);
    BACK_RING_INIT(&usbif->conn_ring, conn_sring, XC_PAGE_SIZE);

    xen_be_bind_evtchn(xendev);

    xen_pv_printf(xendev, 1, "urb-ring-ref %d, conn-ring-ref %d, "
                  "remote port %d, local port %d\n", urb_ring_ref,
                  conn_ring_ref, xendev->remote_port, xendev->local_port);

    for (i = 1; i <= usbif->num_ports; i++) {
        if (usbif->ports[i - 1].dev) {
            usbback_hotplug_enq(usbif, i);
        }
    }

    return 0;
}

static void usbback_backend_changed(struct XenLegacyDevice *xendev,
                                    const char *node)
{
    struct usbback_info *usbif;
    unsigned int i;

    TR_BUS(xendev, "path %s\n", node);

    usbif = container_of(xendev, struct usbback_info, xendev);
    for (i = 1; i <= usbif->num_ports; i++) {
        usbback_process_port(usbif, i);
    }
}

static int usbback_init(struct XenLegacyDevice *xendev)
{
    struct usbback_info *usbif;

    TR_BUS(xendev, "start\n");

    usbif = container_of(xendev, struct usbback_info, xendev);

    if (xenstore_read_be_int(xendev, "num-ports", &usbif->num_ports) ||
        usbif->num_ports < 1 || usbif->num_ports > USBBACK_MAXPORTS) {
        xen_pv_printf(xendev, 0, "num-ports not readable or out of bounds\n");
        return -1;
    }
    if (xenstore_read_be_int(xendev, "usb-ver", &usbif->usb_ver) ||
        (usbif->usb_ver != USB_VER_USB11 && usbif->usb_ver != USB_VER_USB20)) {
        xen_pv_printf(xendev, 0, "usb-ver not readable or out of bounds\n");
        return -1;
    }

    usbback_backend_changed(xendev, "port");

    TR_BUS(xendev, "finished\n");

    return 0;
}

static void xen_bus_attach(USBPort *port)
{
    struct usbback_info *usbif;

    usbif = port->opaque;
    TR_BUS(&usbif->xendev, "\n");
    usbif->ports[port->index].attached = true;
    usbback_hotplug_enq(usbif, port->index + 1);
}

static void xen_bus_detach(USBPort *port)
{
    struct usbback_info *usbif;

    usbif = port->opaque;
    TR_BUS(&usbif->xendev, "\n");
    usbback_portid_detach(usbif, port->index + 1);
}

static void xen_bus_child_detach(USBPort *port, USBDevice *child)
{
    struct usbback_info *usbif;

    usbif = port->opaque;
    TR_BUS(&usbif->xendev, "\n");
}

static void xen_bus_complete(USBPort *port, USBPacket *packet)
{
    struct usbback_req *usbback_req;
    struct usbback_info *usbif;

    usbback_req = container_of(packet, struct usbback_req, packet);
    if (usbback_req->cancelled) {
        g_free(usbback_req);
        return;
    }

    usbif = usbback_req->usbif;
    TR_REQ(&usbif->xendev, "\n");
    usbback_packet_complete(usbback_req);
}

static USBPortOps xen_usb_port_ops = {
    .attach = xen_bus_attach,
    .detach = xen_bus_detach,
    .child_detach = xen_bus_child_detach,
    .complete = xen_bus_complete,
};

static USBBusOps xen_usb_bus_ops = {
};

static void usbback_alloc(struct XenLegacyDevice *xendev)
{
    struct usbback_info *usbif;
    USBPort *p;
    unsigned int i;

    usbif = container_of(xendev, struct usbback_info, xendev);

    usb_bus_new(&usbif->bus, sizeof(usbif->bus), &xen_usb_bus_ops,
                DEVICE(&xendev->qdev));
    for (i = 0; i < USBBACK_MAXPORTS; i++) {
        p = &(usbif->ports[i].port);
        usb_register_port(&usbif->bus, p, usbif, i, &xen_usb_port_ops,
                          USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL |
                          USB_SPEED_MASK_HIGH);
    }

    QTAILQ_INIT(&usbif->req_free_q);
    QSIMPLEQ_INIT(&usbif->hotplug_q);
    usbif->bh = qemu_bh_new(usbback_bh, usbif);
}

static int usbback_free(struct XenLegacyDevice *xendev)
{
    struct usbback_info *usbif;
    struct usbback_req *usbback_req;
    struct usbback_hotplug *usb_hp;
    unsigned int i;

    TR_BUS(xendev, "start\n");

    usbback_disconnect(xendev);
    usbif = container_of(xendev, struct usbback_info, xendev);
    for (i = 1; i <= usbif->num_ports; i++) {
        usbback_portid_remove(usbif, i);
    }

    while (!QTAILQ_EMPTY(&usbif->req_free_q)) {
        usbback_req = QTAILQ_FIRST(&usbif->req_free_q);
        QTAILQ_REMOVE(&usbif->req_free_q, usbback_req, q);
        g_free(usbback_req);
    }
    while (!QSIMPLEQ_EMPTY(&usbif->hotplug_q)) {
        usb_hp = QSIMPLEQ_FIRST(&usbif->hotplug_q);
        QSIMPLEQ_REMOVE_HEAD(&usbif->hotplug_q, q);
        g_free(usb_hp);
    }

    qemu_bh_delete(usbif->bh);

    for (i = 0; i < USBBACK_MAXPORTS; i++) {
        usb_unregister_port(&usbif->bus, &(usbif->ports[i].port));
    }

    usb_bus_release(&usbif->bus);

    TR_BUS(xendev, "finished\n");

    return 0;
}

static void usbback_event(struct XenLegacyDevice *xendev)
{
    struct usbback_info *usbif;

    usbif = container_of(xendev, struct usbback_info, xendev);
    qemu_bh_schedule(usbif->bh);
}

struct XenDevOps xen_usb_ops = {
    .size            = sizeof(struct usbback_info),
    .flags           = DEVOPS_FLAG_NEED_GNTDEV,
    .init            = usbback_init,
    .alloc           = usbback_alloc,
    .free            = usbback_free,
    .backend_changed = usbback_backend_changed,
    .initialise      = usbback_connect,
    .disconnect      = usbback_disconnect,
    .event           = usbback_event,
};

#else /* USBIF_SHORT_NOT_OK */

static int usbback_not_supported(void)
{
    return -EINVAL;
}

struct XenDevOps xen_usb_ops = {
    .backend_register = usbback_not_supported,
};

#endif
