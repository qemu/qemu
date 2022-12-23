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

static void xen_xenstore_event(void *opaque)
{
    XenXenstoreState *s = opaque;
    evtchn_port_t port = xen_be_evtchn_pending(s->eh);
    if (port != s->be_port) {
        return;
    }
    printf("xenstore event\n");
    /* We know this is a no-op. */
    xen_be_evtchn_unmask(s->eh, port);
    qemu_hexdump(stdout, "", s->xs, sizeof(*s->xs));
    xen_be_evtchn_notify(s->eh, s->be_port);
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
