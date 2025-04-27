/*
 * QEMU Xen emulation: Primary console support
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "hw/xen/xen_backend_ops.h"
#include "xen_evtchn.h"
#include "xen_overlay.h"
#include "xen_primary_console.h"

#include "system/kvm.h"
#include "system/kvm_xen.h"

#include "trace.h"

#include "hw/xen/interface/event_channel.h"
#include "hw/xen/interface/grant_table.h"

#define TYPE_XEN_PRIMARY_CONSOLE "xen-primary-console"
OBJECT_DECLARE_SIMPLE_TYPE(XenPrimaryConsoleState, XEN_PRIMARY_CONSOLE)

struct XenPrimaryConsoleState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion console_page;
    void *cp;

    evtchn_port_t guest_port;
    evtchn_port_t be_port;

    struct xengntdev_handle *gt;
    void *granted_xs;
};

struct XenPrimaryConsoleState *xen_primary_console_singleton;

static void xen_primary_console_realize(DeviceState *dev, Error **errp)
{
    XenPrimaryConsoleState *s = XEN_PRIMARY_CONSOLE(dev);

    if (xen_mode != XEN_EMULATE) {
        error_setg(errp, "Xen primary console support is for Xen emulation");
        return;
    }

    memory_region_init_ram(&s->console_page, OBJECT(dev), "xen:console_page",
                           XEN_PAGE_SIZE, &error_abort);
    memory_region_set_enabled(&s->console_page, true);
    s->cp = memory_region_get_ram_ptr(&s->console_page);
    memset(s->cp, 0, XEN_PAGE_SIZE);

    /* We can't map it this early as KVM isn't ready */
    xen_primary_console_singleton = s;
}

static void xen_primary_console_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xen_primary_console_realize;
}

static const TypeInfo xen_primary_console_info = {
    .name          = TYPE_XEN_PRIMARY_CONSOLE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenPrimaryConsoleState),
    .class_init    = xen_primary_console_class_init,
};


void xen_primary_console_create(void)
{
    DeviceState *dev = sysbus_create_simple(TYPE_XEN_PRIMARY_CONSOLE, -1, NULL);

    trace_xen_primary_console_create();

    xen_primary_console_singleton = XEN_PRIMARY_CONSOLE(dev);

    /*
     * Defer the init (xen_primary_console_reset()) until KVM is set up and the
     * overlay page can be mapped.
     */
}

static void xen_primary_console_register_types(void)
{
    type_register_static(&xen_primary_console_info);
}

type_init(xen_primary_console_register_types)

uint16_t xen_primary_console_get_port(void)
{
    XenPrimaryConsoleState *s = xen_primary_console_singleton;
    if (!s) {
        return 0;
    }
    return s->guest_port;
}

void xen_primary_console_set_be_port(uint16_t port)
{
    XenPrimaryConsoleState *s = xen_primary_console_singleton;
    if (s) {
        s->be_port = port;
    }
}

uint64_t xen_primary_console_get_pfn(void)
{
    XenPrimaryConsoleState *s = xen_primary_console_singleton;
    if (!s) {
        return 0;
    }
    return XEN_SPECIAL_PFN(CONSOLE);
}

void *xen_primary_console_get_map(void)
{
    XenPrimaryConsoleState *s = xen_primary_console_singleton;
    if (!s) {
        return 0;
    }
    return s->cp;
}

static void alloc_guest_port(XenPrimaryConsoleState *s)
{
    struct evtchn_alloc_unbound alloc = {
        .dom = DOMID_SELF,
        .remote_dom = DOMID_QEMU,
    };

    if (!xen_evtchn_alloc_unbound_op(&alloc)) {
        s->guest_port = alloc.port;
    }
}

static void rebind_guest_port(XenPrimaryConsoleState *s)
{
    struct evtchn_bind_interdomain inter = {
        .remote_dom = DOMID_QEMU,
        .remote_port = s->be_port,
    };

    if (!xen_evtchn_bind_interdomain_op(&inter)) {
        s->guest_port = inter.local_port;
    }

    s->be_port = 0;
}

int xen_primary_console_reset(void)
{
    XenPrimaryConsoleState *s = xen_primary_console_singleton;
    if (!s) {
        return 0;
    }

    if (!memory_region_is_mapped(&s->console_page)) {
        uint64_t gpa = XEN_SPECIAL_PFN(CONSOLE) << TARGET_PAGE_BITS;
        xen_overlay_do_map_page(&s->console_page, gpa);
    }

    if (s->be_port) {
        rebind_guest_port(s);
    } else {
        alloc_guest_port(s);
    }

    trace_xen_primary_console_reset(s->guest_port);

    s->gt = qemu_xen_gnttab_open();
    uint32_t xs_gntref = GNTTAB_RESERVED_CONSOLE;
    s->granted_xs = qemu_xen_gnttab_map_refs(s->gt, 1, xen_domid, &xs_gntref,
                                             PROT_READ | PROT_WRITE);

    return 0;
}
