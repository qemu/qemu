/*
 * QEMU Xen emulation: Grant table support
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
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "exec/target_page.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "xen_overlay.h"
#include "xen_gnttab.h"

#include "sysemu/kvm.h"
#include "sysemu/kvm_xen.h"

#include "hw/xen/interface/memory.h"
#include "hw/xen/interface/grant_table.h"

#define TYPE_XEN_GNTTAB "xen-gnttab"
OBJECT_DECLARE_SIMPLE_TYPE(XenGnttabState, XEN_GNTTAB)

#define XEN_PAGE_SHIFT 12
#define XEN_PAGE_SIZE (1ULL << XEN_PAGE_SHIFT)

struct XenGnttabState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    uint32_t nr_frames;
    uint32_t max_frames;
};

struct XenGnttabState *xen_gnttab_singleton;

static void xen_gnttab_realize(DeviceState *dev, Error **errp)
{
    XenGnttabState *s = XEN_GNTTAB(dev);

    if (xen_mode != XEN_EMULATE) {
        error_setg(errp, "Xen grant table support is for Xen emulation");
        return;
    }
    s->nr_frames = 0;
    s->max_frames = kvm_xen_get_gnttab_max_frames();
}

static bool xen_gnttab_is_needed(void *opaque)
{
    return xen_mode == XEN_EMULATE;
}

static const VMStateDescription xen_gnttab_vmstate = {
    .name = "xen_gnttab",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_gnttab_is_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(nr_frames, XenGnttabState),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_gnttab_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xen_gnttab_realize;
    dc->vmsd = &xen_gnttab_vmstate;
}

static const TypeInfo xen_gnttab_info = {
    .name          = TYPE_XEN_GNTTAB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenGnttabState),
    .class_init    = xen_gnttab_class_init,
};

void xen_gnttab_create(void)
{
    xen_gnttab_singleton = XEN_GNTTAB(sysbus_create_simple(TYPE_XEN_GNTTAB,
                                                           -1, NULL));
}

static void xen_gnttab_register_types(void)
{
    type_register_static(&xen_gnttab_info);
}

type_init(xen_gnttab_register_types)

int xen_gnttab_map_page(uint64_t idx, uint64_t gfn)
{
    return -ENOSYS;
}

