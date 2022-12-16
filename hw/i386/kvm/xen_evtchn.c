/*
 * QEMU Xen emulation: Event channel support
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
#include "exec/address-spaces.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "xen_evtchn.h"

#include "sysemu/kvm.h"
#include "sysemu/kvm_xen.h"
#include <linux/kvm.h>

#include "hw/xen/interface/memory.h"
#include "hw/xen/interface/hvm/params.h"

#define TYPE_XEN_EVTCHN "xen-evtchn"
OBJECT_DECLARE_SIMPLE_TYPE(XenEvtchnState, XEN_EVTCHN)

struct XenEvtchnState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    uint64_t callback_param;
    bool evtchn_in_kernel;

    QemuMutex port_lock;
};

struct XenEvtchnState *xen_evtchn_singleton;

/* Top bits of callback_param are the type (HVM_PARAM_CALLBACK_TYPE_xxx) */
#define CALLBACK_VIA_TYPE_SHIFT 56

static int xen_evtchn_post_load(void *opaque, int version_id)
{
    XenEvtchnState *s = opaque;

    if (s->callback_param) {
        xen_evtchn_set_callback_param(s->callback_param);
    }

    return 0;
}

static bool xen_evtchn_is_needed(void *opaque)
{
    return xen_mode == XEN_EMULATE;
}

static const VMStateDescription xen_evtchn_vmstate = {
    .name = "xen_evtchn",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_evtchn_is_needed,
    .post_load = xen_evtchn_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(callback_param, XenEvtchnState),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_evtchn_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &xen_evtchn_vmstate;
}

static const TypeInfo xen_evtchn_info = {
    .name          = TYPE_XEN_EVTCHN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenEvtchnState),
    .class_init    = xen_evtchn_class_init,
};

void xen_evtchn_create(void)
{
    XenEvtchnState *s = XEN_EVTCHN(sysbus_create_simple(TYPE_XEN_EVTCHN,
                                                        -1, NULL));
    xen_evtchn_singleton = s;

    qemu_mutex_init(&s->port_lock);
}

static void xen_evtchn_register_types(void)
{
    type_register_static(&xen_evtchn_info);
}

type_init(xen_evtchn_register_types)

int xen_evtchn_set_callback_param(uint64_t param)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    struct kvm_xen_hvm_attr xa = {
        .type = KVM_XEN_ATTR_TYPE_UPCALL_VECTOR,
        .u.vector = 0,
    };
    bool in_kernel = false;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    qemu_mutex_lock(&s->port_lock);

    switch (param >> CALLBACK_VIA_TYPE_SHIFT) {
    case HVM_PARAM_CALLBACK_TYPE_VECTOR: {
        xa.u.vector = (uint8_t)param,

        ret = kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_SET_ATTR, &xa);
        if (!ret && kvm_xen_has_cap(EVTCHN_SEND)) {
            in_kernel = true;
        }
        break;
    }
    default:
        /* Xen doesn't return error even if you set something bogus */
        ret = 0;
        break;
    }

    if (!ret) {
        /* If vector delivery was turned *off* then tell the kernel */
        if ((s->callback_param >> CALLBACK_VIA_TYPE_SHIFT) ==
            HVM_PARAM_CALLBACK_TYPE_VECTOR && !xa.u.vector) {
            kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_SET_ATTR, &xa);
        }
        s->callback_param = param;
        s->evtchn_in_kernel = in_kernel;
    }

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}
