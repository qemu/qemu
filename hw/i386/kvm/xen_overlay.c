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
#include "qapi/error.h"
#include "qom/object.h"
#include "exec/target_page.h"
#include "system/address-spaces.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "xen_overlay.h"

#include "system/kvm.h"
#include "system/kvm_xen.h"
#include <linux/kvm.h>

#include "hw/xen/interface/memory.h"


#define TYPE_XEN_OVERLAY "xen-overlay"
OBJECT_DECLARE_SIMPLE_TYPE(XenOverlayState, XEN_OVERLAY)

#define XEN_PAGE_SHIFT 12
#define XEN_PAGE_SIZE (1ULL << XEN_PAGE_SHIFT)

struct XenOverlayState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion shinfo_mem;
    void *shinfo_ptr;
    uint64_t shinfo_gpa;
    bool long_mode;
};

struct XenOverlayState *xen_overlay_singleton;

void xen_overlay_do_map_page(MemoryRegion *page, uint64_t gpa)
{
    /*
     * Xen allows guests to map the same page as many times as it likes
     * into guest physical frames. We don't, because it would be hard
     * to track and restore them all. One mapping of each page is
     * perfectly sufficient for all known guests... and we've tested
     * that theory on a few now in other implementations. dwmw2.
     */
    if (memory_region_is_mapped(page)) {
        if (gpa == INVALID_GPA) {
            memory_region_del_subregion(get_system_memory(), page);
        } else {
            /* Just move it */
            memory_region_set_address(page, gpa);
        }
    } else if (gpa != INVALID_GPA) {
        memory_region_add_subregion_overlap(get_system_memory(), gpa, page, 0);
    }
}

/* KVM is the only existing back end for now. Let's not overengineer it yet. */
static int xen_overlay_set_be_shinfo(uint64_t gfn)
{
    struct kvm_xen_hvm_attr xa = {
        .type = KVM_XEN_ATTR_TYPE_SHARED_INFO,
        .u.shared_info.gfn = gfn,
    };

    return kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_SET_ATTR, &xa);
}


static void xen_overlay_realize(DeviceState *dev, Error **errp)
{
    XenOverlayState *s = XEN_OVERLAY(dev);

    if (xen_mode != XEN_EMULATE) {
        error_setg(errp, "Xen overlay page support is for Xen emulation");
        return;
    }

    memory_region_init_ram(&s->shinfo_mem, OBJECT(dev), "xen:shared_info",
                           XEN_PAGE_SIZE, &error_abort);
    memory_region_set_enabled(&s->shinfo_mem, true);

    s->shinfo_ptr = memory_region_get_ram_ptr(&s->shinfo_mem);
    s->shinfo_gpa = INVALID_GPA;
    s->long_mode = false;
    memset(s->shinfo_ptr, 0, XEN_PAGE_SIZE);
}

static int xen_overlay_pre_save(void *opaque)
{
    /*
     * Fetch the kernel's idea of long_mode to avoid the race condition
     * where the guest has set the hypercall page up in 64-bit mode but
     * not yet made a hypercall by the time migration happens, so qemu
     * hasn't yet noticed.
     */
    return xen_sync_long_mode();
}

static int xen_overlay_post_load(void *opaque, int version_id)
{
    XenOverlayState *s = opaque;

    if (s->shinfo_gpa != INVALID_GPA) {
        xen_overlay_do_map_page(&s->shinfo_mem, s->shinfo_gpa);
        xen_overlay_set_be_shinfo(s->shinfo_gpa >> XEN_PAGE_SHIFT);
    }
    if (s->long_mode) {
        xen_set_long_mode(true);
    }

    return 0;
}

static bool xen_overlay_is_needed(void *opaque)
{
    return xen_mode == XEN_EMULATE;
}

static const VMStateDescription xen_overlay_vmstate = {
    .name = "xen_overlay",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_overlay_is_needed,
    .pre_save = xen_overlay_pre_save,
    .post_load = xen_overlay_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(shinfo_gpa, XenOverlayState),
        VMSTATE_BOOL(long_mode, XenOverlayState),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_overlay_reset(DeviceState *dev)
{
    kvm_xen_soft_reset();
}

static void xen_overlay_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, xen_overlay_reset);
    dc->realize = xen_overlay_realize;
    dc->vmsd = &xen_overlay_vmstate;
}

static const TypeInfo xen_overlay_info = {
    .name          = TYPE_XEN_OVERLAY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenOverlayState),
    .class_init    = xen_overlay_class_init,
};

void xen_overlay_create(void)
{
    xen_overlay_singleton = XEN_OVERLAY(sysbus_create_simple(TYPE_XEN_OVERLAY,
                                                             -1, NULL));

    /* If xen_domid wasn't explicitly set, at least make sure it isn't zero. */
    if (xen_domid == DOMID_QEMU) {
        xen_domid = 1;
    };
}

static void xen_overlay_register_types(void)
{
    type_register_static(&xen_overlay_info);
}

type_init(xen_overlay_register_types)

int xen_overlay_map_shinfo_page(uint64_t gpa)
{
    XenOverlayState *s = xen_overlay_singleton;
    int ret;

    if (!s) {
        return -ENOENT;
    }

    assert(bql_locked());

    if (s->shinfo_gpa) {
        /* If removing shinfo page, turn the kernel magic off first */
        ret = xen_overlay_set_be_shinfo(INVALID_GFN);
        if (ret) {
            return ret;
        }
    }

    xen_overlay_do_map_page(&s->shinfo_mem, gpa);
    if (gpa != INVALID_GPA) {
        ret = xen_overlay_set_be_shinfo(gpa >> XEN_PAGE_SHIFT);
        if (ret) {
            return ret;
        }
    }
    s->shinfo_gpa = gpa;

    return 0;
}

void *xen_overlay_get_shinfo_ptr(void)
{
    XenOverlayState *s = xen_overlay_singleton;

    if (!s) {
        return NULL;
    }

    return s->shinfo_ptr;
}

int xen_sync_long_mode(void)
{
    int ret;
    struct kvm_xen_hvm_attr xa = {
        .type = KVM_XEN_ATTR_TYPE_LONG_MODE,
    };

    if (!xen_overlay_singleton) {
        return -ENOENT;
    }

    ret = kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_GET_ATTR, &xa);
    if (!ret) {
        xen_overlay_singleton->long_mode = xa.u.long_mode;
    }

    return ret;
}

int xen_set_long_mode(bool long_mode)
{
    int ret;
    struct kvm_xen_hvm_attr xa = {
        .type = KVM_XEN_ATTR_TYPE_LONG_MODE,
        .u.long_mode = long_mode,
    };

    if (!xen_overlay_singleton) {
        return -ENOENT;
    }

    ret = kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_SET_ATTR, &xa);
    if (!ret) {
        xen_overlay_singleton->long_mode = xa.u.long_mode;
    }

    return ret;
}

bool xen_is_long_mode(void)
{
    return xen_overlay_singleton && xen_overlay_singleton->long_mode;
}
