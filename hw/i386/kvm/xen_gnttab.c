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

#define ENTRIES_PER_FRAME_V1 (XEN_PAGE_SIZE / sizeof(grant_entry_v1_t))

struct XenGnttabState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    QemuMutex gnt_lock;

    uint32_t nr_frames;
    uint32_t max_frames;

    union {
        grant_entry_v1_t *v1;
        /* Theoretically, v2 support could be added here. */
    } entries;

    MemoryRegion gnt_frames;
    MemoryRegion *gnt_aliases;
    uint64_t *gnt_frame_gpas;
};

struct XenGnttabState *xen_gnttab_singleton;

static void xen_gnttab_realize(DeviceState *dev, Error **errp)
{
    XenGnttabState *s = XEN_GNTTAB(dev);
    int i;

    if (xen_mode != XEN_EMULATE) {
        error_setg(errp, "Xen grant table support is for Xen emulation");
        return;
    }
    s->nr_frames = 0;
    s->max_frames = kvm_xen_get_gnttab_max_frames();
    memory_region_init_ram(&s->gnt_frames, OBJECT(dev), "xen:grant_table",
                           XEN_PAGE_SIZE * s->max_frames, &error_abort);
    memory_region_set_enabled(&s->gnt_frames, true);
    s->entries.v1 = memory_region_get_ram_ptr(&s->gnt_frames);
    memset(s->entries.v1, 0, XEN_PAGE_SIZE * s->max_frames);

    /* Create individual page-sizes aliases for overlays */
    s->gnt_aliases = (void *)g_new0(MemoryRegion, s->max_frames);
    s->gnt_frame_gpas = (void *)g_new(uint64_t, s->max_frames);
    for (i = 0; i < s->max_frames; i++) {
        memory_region_init_alias(&s->gnt_aliases[i], OBJECT(dev),
                                 NULL, &s->gnt_frames,
                                 i * XEN_PAGE_SIZE, XEN_PAGE_SIZE);
        s->gnt_frame_gpas[i] = INVALID_GPA;
    }

    qemu_mutex_init(&s->gnt_lock);

    xen_gnttab_singleton = s;
}

static int xen_gnttab_post_load(void *opaque, int version_id)
{
    XenGnttabState *s = XEN_GNTTAB(opaque);
    uint32_t i;

    for (i = 0; i < s->nr_frames; i++) {
        if (s->gnt_frame_gpas[i] != INVALID_GPA) {
            xen_overlay_do_map_page(&s->gnt_aliases[i], s->gnt_frame_gpas[i]);
        }
    }
    return 0;
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
    .post_load = xen_gnttab_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(nr_frames, XenGnttabState),
        VMSTATE_VARRAY_UINT32(gnt_frame_gpas, XenGnttabState, nr_frames, 0,
                              vmstate_info_uint64, uint64_t),
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
    XenGnttabState *s = xen_gnttab_singleton;
    uint64_t gpa = gfn << XEN_PAGE_SHIFT;

    if (!s) {
        return -ENOTSUP;
    }

    if (idx >= s->max_frames) {
        return -EINVAL;
    }

    QEMU_IOTHREAD_LOCK_GUARD();
    QEMU_LOCK_GUARD(&s->gnt_lock);

    xen_overlay_do_map_page(&s->gnt_aliases[idx], gpa);

    s->gnt_frame_gpas[idx] = gpa;

    if (s->nr_frames <= idx) {
        s->nr_frames = idx + 1;
    }

    return 0;
}

int xen_gnttab_set_version_op(struct gnttab_set_version *set)
{
    int ret;

    switch (set->version) {
    case 1:
        ret = 0;
        break;

    case 2:
        /* Behave as before set_version was introduced. */
        ret = -ENOSYS;
        break;

    default:
        ret = -EINVAL;
    }

    set->version = 1;
    return ret;
}

int xen_gnttab_get_version_op(struct gnttab_get_version *get)
{
    if (get->dom != DOMID_SELF && get->dom != xen_domid) {
        return -ESRCH;
    }

    get->version = 1;
    return 0;
}
