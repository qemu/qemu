/*
 * QEMU Xen emulation: Shared/overlay pages support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "xen_overlay.h"
#include "xen_evtchn.h"
#include "xen_xenstore.h"

#include "sysemu/kvm.h"
#include "sysemu/kvm_xen.h"

#include "hw/xen/interface/io/xs_wire.h"
#include "hw/xen/interface/event_channel.h"

#define TYPE_XEN_XENSTORE "xen-xenstore"
OBJECT_DECLARE_SIMPLE_TYPE(XenXenstoreState, XEN_XENSTORE)

#define XEN_PAGE_SHIFT 12
#define XEN_PAGE_SIZE (1ULL << XEN_PAGE_SHIFT)

#define ENTRIES_PER_FRAME_V1 (XEN_PAGE_SIZE / sizeof(grant_entry_v1_t))
#define ENTRIES_PER_FRAME_V2 (XEN_PAGE_SIZE / sizeof(grant_entry_v2_t))

#define XENSTORE_HEADER_SIZE ((unsigned int)sizeof(struct xsd_sockmsg))

struct XenXenstoreState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion xenstore_page;
    struct xenstore_domain_interface *xs;
    uint8_t req_data[XENSTORE_HEADER_SIZE + XENSTORE_PAYLOAD_MAX];
    uint8_t rsp_data[XENSTORE_HEADER_SIZE + XENSTORE_PAYLOAD_MAX];
    uint32_t req_offset;
    uint32_t rsp_offset;
    bool rsp_pending;
    bool fatal_error;

    evtchn_port_t guest_port;
    evtchn_port_t be_port;
    struct xenevtchn_handle *eh;
};

struct XenXenstoreState *xen_xenstore_singleton;

static void xen_xenstore_event(void *opaque);

static void xen_xenstore_realize(DeviceState *dev, Error **errp)
{
    XenXenstoreState *s = XEN_XENSTORE(dev);

    if (xen_mode != XEN_EMULATE) {
        error_setg(errp, "Xen xenstore support is for Xen emulation");
        return;
    }
    memory_region_init_ram(&s->xenstore_page, OBJECT(dev), "xen:xenstore_page",
                           XEN_PAGE_SIZE, &error_abort);
    memory_region_set_enabled(&s->xenstore_page, true);
    s->xs = memory_region_get_ram_ptr(&s->xenstore_page);
    memset(s->xs, 0, XEN_PAGE_SIZE);

    /* We can't map it this early as KVM isn't ready */
    xen_xenstore_singleton = s;

    s->eh = xen_be_evtchn_open();
    if (!s->eh) {
        error_setg(errp, "Xenstore evtchn port init failed");
        return;
    }
    aio_set_fd_handler(qemu_get_aio_context(), xen_be_evtchn_fd(s->eh), true,
                       xen_xenstore_event, NULL, NULL, NULL, s);
}

static bool xen_xenstore_is_needed(void *opaque)
{
    return xen_mode == XEN_EMULATE;
}

static int xen_xenstore_pre_save(void *opaque)
{
    XenXenstoreState *s = opaque;

    if (s->eh) {
        s->guest_port = xen_be_evtchn_get_guest_port(s->eh);
    }
    return 0;
}

static int xen_xenstore_post_load(void *opaque, int ver)
{
    XenXenstoreState *s = opaque;

    /*
     * As qemu/dom0, rebind to the guest's port. The Windows drivers may
     * unbind the XenStore evtchn and rebind to it, having obtained the
     * "remote" port through EVTCHNOP_status. In the case that migration
     * occurs while it's unbound, the "remote" port needs to be the same
     * as before so that the guest can find it, but should remain unbound.
     */
    if (s->guest_port) {
        int be_port = xen_be_evtchn_bind_interdomain(s->eh, xen_domid,
                                                     s->guest_port);
        if (be_port < 0) {
            return be_port;
        }
        s->be_port = be_port;
    }
    return 0;
}

static const VMStateDescription xen_xenstore_vmstate = {
    .name = "xen_xenstore",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_xenstore_is_needed,
    .pre_save = xen_xenstore_pre_save,
    .post_load = xen_xenstore_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(req_data, XenXenstoreState,
                            sizeof_field(XenXenstoreState, req_data)),
        VMSTATE_UINT8_ARRAY(rsp_data, XenXenstoreState,
                            sizeof_field(XenXenstoreState, rsp_data)),
        VMSTATE_UINT32(req_offset, XenXenstoreState),
        VMSTATE_UINT32(rsp_offset, XenXenstoreState),
        VMSTATE_BOOL(rsp_pending, XenXenstoreState),
        VMSTATE_UINT32(guest_port, XenXenstoreState),
        VMSTATE_BOOL(fatal_error, XenXenstoreState),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_xenstore_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xen_xenstore_realize;
    dc->vmsd = &xen_xenstore_vmstate;
}

static const TypeInfo xen_xenstore_info = {
    .name          = TYPE_XEN_XENSTORE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenXenstoreState),
    .class_init    = xen_xenstore_class_init,
};

void xen_xenstore_create(void)
{
    DeviceState *dev = sysbus_create_simple(TYPE_XEN_XENSTORE, -1, NULL);

    xen_xenstore_singleton = XEN_XENSTORE(dev);

    /*
     * Defer the init (xen_xenstore_reset()) until KVM is set up and the
     * overlay page can be mapped.
     */
}

static void xen_xenstore_register_types(void)
{
    type_register_static(&xen_xenstore_info);
}

type_init(xen_xenstore_register_types)

uint16_t xen_xenstore_get_port(void)
{
    XenXenstoreState *s = xen_xenstore_singleton;
    if (!s) {
        return 0;
    }
    return s->guest_port;
}

static bool req_pending(XenXenstoreState *s)
{
    struct xsd_sockmsg *req = (struct xsd_sockmsg *)s->req_data;

    return s->req_offset == XENSTORE_HEADER_SIZE + req->len;
}

static void reset_req(XenXenstoreState *s)
{
    memset(s->req_data, 0, sizeof(s->req_data));
    s->req_offset = 0;
}

static void reset_rsp(XenXenstoreState *s)
{
    s->rsp_pending = false;

    memset(s->rsp_data, 0, sizeof(s->rsp_data));
    s->rsp_offset = 0;
}

static void process_req(XenXenstoreState *s)
{
    struct xsd_sockmsg *req = (struct xsd_sockmsg *)s->req_data;
    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    const char enosys[] = "ENOSYS";

    assert(req_pending(s));
    assert(!s->rsp_pending);

    rsp->type = XS_ERROR;
    rsp->req_id = req->req_id;
    rsp->tx_id = req->tx_id;
    rsp->len = sizeof(enosys);
    memcpy((void *)&rsp[1], enosys, sizeof(enosys));

    s->rsp_pending = true;
    reset_req(s);
}

static unsigned int copy_from_ring(XenXenstoreState *s, uint8_t *ptr,
                                   unsigned int len)
{
    if (!len) {
        return 0;
    }

    XENSTORE_RING_IDX prod = qatomic_read(&s->xs->req_prod);
    XENSTORE_RING_IDX cons = qatomic_read(&s->xs->req_cons);
    unsigned int copied = 0;

    /* Ensure the ring contents don't cross the req_prod access. */
    smp_rmb();

    while (len) {
        unsigned int avail = prod - cons;
        unsigned int offset = MASK_XENSTORE_IDX(cons);
        unsigned int copylen = avail;

        if (avail > XENSTORE_RING_SIZE) {
            error_report("XenStore ring handling error");
            s->fatal_error = true;
            break;
        } else if (avail == 0) {
            break;
        }

        if (copylen > len) {
            copylen = len;
        }
        if (copylen > XENSTORE_RING_SIZE - offset) {
            copylen = XENSTORE_RING_SIZE - offset;
        }

        memcpy(ptr, &s->xs->req[offset], copylen);
        copied += copylen;

        ptr += copylen;
        len -= copylen;

        cons += copylen;
    }

    /*
     * Not sure this ever mattered except on Alpha, but this barrier
     * is to ensure that the update to req_cons is globally visible
     * only after we have consumed all the data from the ring, and we
     * don't end up seeing data written to the ring *after* the other
     * end sees the update and writes more to the ring. Xen's own
     * xenstored has the same barrier here (although with no comment
     * at all, obviously, because it's Xen code).
     */
    smp_mb();

    qatomic_set(&s->xs->req_cons, cons);

    return copied;
}

static unsigned int copy_to_ring(XenXenstoreState *s, uint8_t *ptr,
                                 unsigned int len)
{
    if (!len) {
        return 0;
    }

    XENSTORE_RING_IDX cons = qatomic_read(&s->xs->rsp_cons);
    XENSTORE_RING_IDX prod = qatomic_read(&s->xs->rsp_prod);
    unsigned int copied = 0;

    /*
     * This matches the barrier in copy_to_ring() (or the guest's
     * equivalent) betweem writing the data to the ring and updating
     * rsp_prod. It protects against the pathological case (which
     * again I think never happened except on Alpha) where our
     * subsequent writes to the ring could *cross* the read of
     * rsp_cons and the guest could see the new data when it was
     * intending to read the old.
     */
    smp_mb();

    while (len) {
        unsigned int avail = cons + XENSTORE_RING_SIZE - prod;
        unsigned int offset = MASK_XENSTORE_IDX(prod);
        unsigned int copylen = len;

        if (avail > XENSTORE_RING_SIZE) {
            error_report("XenStore ring handling error");
            s->fatal_error = true;
            break;
        } else if (avail == 0) {
            break;
        }

        if (copylen > avail) {
            copylen = avail;
        }
        if (copylen > XENSTORE_RING_SIZE - offset) {
            copylen = XENSTORE_RING_SIZE - offset;
        }


        memcpy(&s->xs->rsp[offset], ptr, copylen);
        copied += copylen;

        ptr += copylen;
        len -= copylen;

        prod += copylen;
    }

    /* Ensure the ring contents are seen before rsp_prod update. */
    smp_wmb();

    qatomic_set(&s->xs->rsp_prod, prod);

    return copied;
}

static unsigned int get_req(XenXenstoreState *s)
{
    unsigned int copied = 0;

    if (s->fatal_error) {
        return 0;
    }

    assert(!req_pending(s));

    if (s->req_offset < XENSTORE_HEADER_SIZE) {
        void *ptr = s->req_data + s->req_offset;
        unsigned int len = XENSTORE_HEADER_SIZE;
        unsigned int copylen = copy_from_ring(s, ptr, len);

        copied += copylen;
        s->req_offset += copylen;
    }

    if (s->req_offset >= XENSTORE_HEADER_SIZE) {
        struct xsd_sockmsg *req = (struct xsd_sockmsg *)s->req_data;

        if (req->len > (uint32_t)XENSTORE_PAYLOAD_MAX) {
            error_report("Illegal XenStore request");
            s->fatal_error = true;
            return 0;
        }

        void *ptr = s->req_data + s->req_offset;
        unsigned int len = XENSTORE_HEADER_SIZE + req->len - s->req_offset;
        unsigned int copylen = copy_from_ring(s, ptr, len);

        copied += copylen;
        s->req_offset += copylen;
    }

    return copied;
}

static unsigned int put_rsp(XenXenstoreState *s)
{
    if (s->fatal_error) {
        return 0;
    }

    assert(s->rsp_pending);

    struct xsd_sockmsg *rsp = (struct xsd_sockmsg *)s->rsp_data;
    assert(s->rsp_offset < XENSTORE_HEADER_SIZE + rsp->len);

    void *ptr = s->rsp_data + s->rsp_offset;
    unsigned int len = XENSTORE_HEADER_SIZE + rsp->len - s->rsp_offset;
    unsigned int copylen = copy_to_ring(s, ptr, len);

    s->rsp_offset += copylen;

    /* Have we produced a complete response? */
    if (s->rsp_offset == XENSTORE_HEADER_SIZE + rsp->len) {
        reset_rsp(s);
    }

    return copylen;
}

static void xen_xenstore_event(void *opaque)
{
    XenXenstoreState *s = opaque;
    evtchn_port_t port = xen_be_evtchn_pending(s->eh);
    unsigned int copied_to, copied_from;
    bool processed, notify = false;

    if (port != s->be_port) {
        return;
    }

    /* We know this is a no-op. */
    xen_be_evtchn_unmask(s->eh, port);

    do {
        copied_to = copied_from = 0;
        processed = false;

        if (s->rsp_pending) {
            copied_to = put_rsp(s);
        }

        if (!req_pending(s)) {
            copied_from = get_req(s);
        }

        if (req_pending(s) && !s->rsp_pending) {
            process_req(s);
            processed = true;
        }

        notify |= copied_to || copied_from;
    } while (copied_to || copied_from || processed);

    if (notify) {
        xen_be_evtchn_notify(s->eh, s->be_port);
    }
}

static void alloc_guest_port(XenXenstoreState *s)
{
    struct evtchn_alloc_unbound alloc = {
        .dom = DOMID_SELF,
        .remote_dom = DOMID_QEMU,
    };

    if (!xen_evtchn_alloc_unbound_op(&alloc)) {
        s->guest_port = alloc.port;
    }
}

int xen_xenstore_reset(void)
{
    XenXenstoreState *s = xen_xenstore_singleton;
    int err;

    if (!s) {
        return -ENOTSUP;
    }

    s->req_offset = s->rsp_offset = 0;
    s->rsp_pending = false;

    if (!memory_region_is_mapped(&s->xenstore_page)) {
        uint64_t gpa = XEN_SPECIAL_PFN(XENSTORE) << TARGET_PAGE_BITS;
        xen_overlay_do_map_page(&s->xenstore_page, gpa);
    }

    alloc_guest_port(s);

    /*
     * As qemu/dom0, bind to the guest's port. For incoming migration, this
     * will be unbound as the guest's evtchn table is overwritten. We then
     * rebind to the correct guest port in xen_xenstore_post_load().
     */
    err = xen_be_evtchn_bind_interdomain(s->eh, xen_domid, s->guest_port);
    if (err < 0) {
        return err;
    }
    s->be_port = err;

    return 0;
}
