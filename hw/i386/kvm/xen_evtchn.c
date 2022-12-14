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
#include "qemu/log.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "exec/target_page.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"

#include "xen_evtchn.h"
#include "xen_overlay.h"

#include "sysemu/kvm.h"
#include "sysemu/kvm_xen.h"
#include <linux/kvm.h>

#include "hw/xen/interface/memory.h"
#include "hw/xen/interface/hvm/params.h"

#define TYPE_XEN_EVTCHN "xen-evtchn"
OBJECT_DECLARE_SIMPLE_TYPE(XenEvtchnState, XEN_EVTCHN)

typedef struct XenEvtchnPort {
    uint32_t vcpu;      /* Xen/ACPI vcpu_id */
    uint16_t type;      /* EVTCHNSTAT_xxxx */
    uint16_t type_val;  /* pirq# / virq# / remote port according to type */
} XenEvtchnPort;

/* 32-bit compatibility definitions, also used natively in 32-bit build */
struct compat_arch_vcpu_info {
    unsigned int cr2;
    unsigned int pad[5];
};

struct compat_vcpu_info {
    uint8_t evtchn_upcall_pending;
    uint8_t evtchn_upcall_mask;
    uint16_t pad;
    uint32_t evtchn_pending_sel;
    struct compat_arch_vcpu_info arch;
    struct vcpu_time_info time;
}; /* 64 bytes (x86) */

struct compat_arch_shared_info {
    unsigned int max_pfn;
    unsigned int pfn_to_mfn_frame_list_list;
    unsigned int nmi_reason;
    unsigned int p2m_cr3;
    unsigned int p2m_vaddr;
    unsigned int p2m_generation;
    uint32_t wc_sec_hi;
};

struct compat_shared_info {
    struct compat_vcpu_info vcpu_info[XEN_LEGACY_MAX_VCPUS];
    uint32_t evtchn_pending[32];
    uint32_t evtchn_mask[32];
    uint32_t wc_version;      /* Version counter: see vcpu_time_info_t. */
    uint32_t wc_sec;
    uint32_t wc_nsec;
    struct compat_arch_shared_info arch;
};

#define COMPAT_EVTCHN_2L_NR_CHANNELS            1024

/*
 * For unbound/interdomain ports there are only two possible remote
 * domains; self and QEMU. Use a single high bit in type_val for that,
 * and the low bits for the remote port number (or 0 for unbound).
 */
#define PORT_INFO_TYPEVAL_REMOTE_QEMU           0x8000
#define PORT_INFO_TYPEVAL_REMOTE_PORT_MASK      0x7FFF

struct XenEvtchnState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    uint64_t callback_param;
    bool evtchn_in_kernel;

    QemuMutex port_lock;
    uint32_t nr_ports;
    XenEvtchnPort port_table[EVTCHN_2L_NR_CHANNELS];
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

static const VMStateDescription xen_evtchn_port_vmstate = {
    .name = "xen_evtchn_port",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(vcpu, XenEvtchnPort),
        VMSTATE_UINT16(type, XenEvtchnPort),
        VMSTATE_UINT16(type_val, XenEvtchnPort),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription xen_evtchn_vmstate = {
    .name = "xen_evtchn",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_evtchn_is_needed,
    .post_load = xen_evtchn_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(callback_param, XenEvtchnState),
        VMSTATE_UINT32(nr_ports, XenEvtchnState),
        VMSTATE_STRUCT_VARRAY_UINT32(port_table, XenEvtchnState, nr_ports, 1,
                                     xen_evtchn_port_vmstate, XenEvtchnPort),
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

static void inject_callback(XenEvtchnState *s, uint32_t vcpu)
{
    int type = s->callback_param >> CALLBACK_VIA_TYPE_SHIFT;

    kvm_xen_inject_vcpu_callback_vector(vcpu, type);
}

static void deassign_kernel_port(evtchn_port_t port)
{
    struct kvm_xen_hvm_attr ha;
    int ret;

    ha.type = KVM_XEN_ATTR_TYPE_EVTCHN;
    ha.u.evtchn.send_port = port;
    ha.u.evtchn.flags = KVM_XEN_EVTCHN_DEASSIGN;

    ret = kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_SET_ATTR, &ha);
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to unbind kernel port %d: %s\n",
                      port, strerror(ret));
    }
}

static int assign_kernel_port(uint16_t type, evtchn_port_t port,
                              uint32_t vcpu_id)
{
    CPUState *cpu = qemu_get_cpu(vcpu_id);
    struct kvm_xen_hvm_attr ha;

    if (!cpu) {
        return -ENOENT;
    }

    ha.type = KVM_XEN_ATTR_TYPE_EVTCHN;
    ha.u.evtchn.send_port = port;
    ha.u.evtchn.type = type;
    ha.u.evtchn.flags = 0;
    ha.u.evtchn.deliver.port.port = port;
    ha.u.evtchn.deliver.port.vcpu = kvm_arch_vcpu_id(cpu);
    ha.u.evtchn.deliver.port.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

    return kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_SET_ATTR, &ha);
}

static bool valid_port(evtchn_port_t port)
{
    if (!port) {
        return false;
    }

    if (xen_is_long_mode()) {
        return port < EVTCHN_2L_NR_CHANNELS;
    } else {
        return port < COMPAT_EVTCHN_2L_NR_CHANNELS;
    }
}

static bool valid_vcpu(uint32_t vcpu)
{
    return !!qemu_get_cpu(vcpu);
}

int xen_evtchn_status_op(struct evtchn_status *status)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    XenEvtchnPort *p;

    if (!s) {
        return -ENOTSUP;
    }

    if (status->dom != DOMID_SELF && status->dom != xen_domid) {
        return -ESRCH;
    }

    if (!valid_port(status->port)) {
        return -EINVAL;
    }

    qemu_mutex_lock(&s->port_lock);

    p = &s->port_table[status->port];

    status->status = p->type;
    status->vcpu = p->vcpu;

    switch (p->type) {
    case EVTCHNSTAT_unbound:
        if (p->type_val & PORT_INFO_TYPEVAL_REMOTE_QEMU) {
            status->u.unbound.dom = DOMID_QEMU;
        } else {
            status->u.unbound.dom = xen_domid;
        }
        break;

    case EVTCHNSTAT_interdomain:
        if (p->type_val & PORT_INFO_TYPEVAL_REMOTE_QEMU) {
            status->u.interdomain.dom = DOMID_QEMU;
        } else {
            status->u.interdomain.dom = xen_domid;
        }

        status->u.interdomain.port = p->type_val &
            PORT_INFO_TYPEVAL_REMOTE_PORT_MASK;
        break;

    case EVTCHNSTAT_pirq:
        status->u.pirq = p->type_val;
        break;

    case EVTCHNSTAT_virq:
        status->u.virq = p->type_val;
        break;
    }

    qemu_mutex_unlock(&s->port_lock);
    return 0;
}

/*
 * Never thought I'd hear myself say this, but C++ templates would be
 * kind of nice here.
 *
 * template<class T> static int do_unmask_port(T *shinfo, ...);
 */
static int do_unmask_port_lm(XenEvtchnState *s, evtchn_port_t port,
                             bool do_unmask, struct shared_info *shinfo,
                             struct vcpu_info *vcpu_info)
{
    const int bits_per_word = BITS_PER_BYTE * sizeof(shinfo->evtchn_pending[0]);
    typeof(shinfo->evtchn_pending[0]) mask;
    int idx = port / bits_per_word;
    int offset = port % bits_per_word;

    mask = 1UL << offset;

    if (idx >= bits_per_word) {
        return -EINVAL;
    }

    if (do_unmask) {
        /*
         * If this is a true unmask operation, clear the mask bit. If
         * it was already unmasked, we have nothing further to do.
         */
        if (!((qatomic_fetch_and(&shinfo->evtchn_mask[idx], ~mask) & mask))) {
            return 0;
        }
    } else {
        /*
         * This is a pseudo-unmask for affinity changes. We don't
         * change the mask bit, and if it's *masked* we have nothing
         * else to do.
         */
        if (qatomic_fetch_or(&shinfo->evtchn_mask[idx], 0) & mask) {
            return 0;
        }
    }

    /* If the event was not pending, we're done. */
    if (!(qatomic_fetch_or(&shinfo->evtchn_pending[idx], 0) & mask)) {
        return 0;
    }

    /* Now on to the vcpu_info evtchn_pending_sel index... */
    mask = 1UL << idx;

    /* If a port in this word was already pending for this vCPU, all done. */
    if (qatomic_fetch_or(&vcpu_info->evtchn_pending_sel, mask) & mask) {
        return 0;
    }

    /* Set evtchn_upcall_pending for this vCPU */
    if (qatomic_fetch_or(&vcpu_info->evtchn_upcall_pending, 1)) {
        return 0;
    }

    inject_callback(s, s->port_table[port].vcpu);

    return 0;
}

static int do_unmask_port_compat(XenEvtchnState *s, evtchn_port_t port,
                                 bool do_unmask,
                                 struct compat_shared_info *shinfo,
                                 struct compat_vcpu_info *vcpu_info)
{
    const int bits_per_word = BITS_PER_BYTE * sizeof(shinfo->evtchn_pending[0]);
    typeof(shinfo->evtchn_pending[0]) mask;
    int idx = port / bits_per_word;
    int offset = port % bits_per_word;

    mask = 1UL << offset;

    if (idx >= bits_per_word) {
        return -EINVAL;
    }

    if (do_unmask) {
        /*
         * If this is a true unmask operation, clear the mask bit. If
         * it was already unmasked, we have nothing further to do.
         */
        if (!((qatomic_fetch_and(&shinfo->evtchn_mask[idx], ~mask) & mask))) {
            return 0;
        }
    } else {
        /*
         * This is a pseudo-unmask for affinity changes. We don't
         * change the mask bit, and if it's *masked* we have nothing
         * else to do.
         */
        if (qatomic_fetch_or(&shinfo->evtchn_mask[idx], 0) & mask) {
            return 0;
        }
    }

    /* If the event was not pending, we're done. */
    if (!(qatomic_fetch_or(&shinfo->evtchn_pending[idx], 0) & mask)) {
        return 0;
    }

    /* Now on to the vcpu_info evtchn_pending_sel index... */
    mask = 1UL << idx;

    /* If a port in this word was already pending for this vCPU, all done. */
    if (qatomic_fetch_or(&vcpu_info->evtchn_pending_sel, mask) & mask) {
        return 0;
    }

    /* Set evtchn_upcall_pending for this vCPU */
    if (qatomic_fetch_or(&vcpu_info->evtchn_upcall_pending, 1)) {
        return 0;
    }

    inject_callback(s, s->port_table[port].vcpu);

    return 0;
}

static int unmask_port(XenEvtchnState *s, evtchn_port_t port, bool do_unmask)
{
    void *vcpu_info, *shinfo;

    if (s->port_table[port].type == EVTCHNSTAT_closed) {
        return -EINVAL;
    }

    shinfo = xen_overlay_get_shinfo_ptr();
    if (!shinfo) {
        return -ENOTSUP;
    }

    vcpu_info = kvm_xen_get_vcpu_info_hva(s->port_table[port].vcpu);
    if (!vcpu_info) {
        return -EINVAL;
    }

    if (xen_is_long_mode()) {
        return do_unmask_port_lm(s, port, do_unmask, shinfo, vcpu_info);
    } else {
        return do_unmask_port_compat(s, port, do_unmask, shinfo, vcpu_info);
    }
}

static int do_set_port_lm(XenEvtchnState *s, evtchn_port_t port,
                          struct shared_info *shinfo,
                          struct vcpu_info *vcpu_info)
{
    const int bits_per_word = BITS_PER_BYTE * sizeof(shinfo->evtchn_pending[0]);
    typeof(shinfo->evtchn_pending[0]) mask;
    int idx = port / bits_per_word;
    int offset = port % bits_per_word;

    mask = 1UL << offset;

    if (idx >= bits_per_word) {
        return -EINVAL;
    }

    /* Update the pending bit itself. If it was already set, we're done. */
    if (qatomic_fetch_or(&shinfo->evtchn_pending[idx], mask) & mask) {
        return 0;
    }

    /* Check if it's masked. */
    if (qatomic_fetch_or(&shinfo->evtchn_mask[idx], 0) & mask) {
        return 0;
    }

    /* Now on to the vcpu_info evtchn_pending_sel index... */
    mask = 1UL << idx;

    /* If a port in this word was already pending for this vCPU, all done. */
    if (qatomic_fetch_or(&vcpu_info->evtchn_pending_sel, mask) & mask) {
        return 0;
    }

    /* Set evtchn_upcall_pending for this vCPU */
    if (qatomic_fetch_or(&vcpu_info->evtchn_upcall_pending, 1)) {
        return 0;
    }

    inject_callback(s, s->port_table[port].vcpu);

    return 0;
}

static int do_set_port_compat(XenEvtchnState *s, evtchn_port_t port,
                              struct compat_shared_info *shinfo,
                              struct compat_vcpu_info *vcpu_info)
{
    const int bits_per_word = BITS_PER_BYTE * sizeof(shinfo->evtchn_pending[0]);
    typeof(shinfo->evtchn_pending[0]) mask;
    int idx = port / bits_per_word;
    int offset = port % bits_per_word;

    mask = 1UL << offset;

    if (idx >= bits_per_word) {
        return -EINVAL;
    }

    /* Update the pending bit itself. If it was already set, we're done. */
    if (qatomic_fetch_or(&shinfo->evtchn_pending[idx], mask) & mask) {
        return 0;
    }

    /* Check if it's masked. */
    if (qatomic_fetch_or(&shinfo->evtchn_mask[idx], 0) & mask) {
        return 0;
    }

    /* Now on to the vcpu_info evtchn_pending_sel index... */
    mask = 1UL << idx;

    /* If a port in this word was already pending for this vCPU, all done. */
    if (qatomic_fetch_or(&vcpu_info->evtchn_pending_sel, mask) & mask) {
        return 0;
    }

    /* Set evtchn_upcall_pending for this vCPU */
    if (qatomic_fetch_or(&vcpu_info->evtchn_upcall_pending, 1)) {
        return 0;
    }

    inject_callback(s, s->port_table[port].vcpu);

    return 0;
}

static int set_port_pending(XenEvtchnState *s, evtchn_port_t port)
{
    void *vcpu_info, *shinfo;

    if (s->port_table[port].type == EVTCHNSTAT_closed) {
        return -EINVAL;
    }

    if (s->evtchn_in_kernel) {
        XenEvtchnPort *p = &s->port_table[port];
        CPUState *cpu = qemu_get_cpu(p->vcpu);
        struct kvm_irq_routing_xen_evtchn evt;

        if (!cpu) {
            return 0;
        }

        evt.port = port;
        evt.vcpu = kvm_arch_vcpu_id(cpu);
        evt.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

        return kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_EVTCHN_SEND, &evt);
    }

    shinfo = xen_overlay_get_shinfo_ptr();
    if (!shinfo) {
        return -ENOTSUP;
    }

    vcpu_info = kvm_xen_get_vcpu_info_hva(s->port_table[port].vcpu);
    if (!vcpu_info) {
        return -EINVAL;
    }

    if (xen_is_long_mode()) {
        return do_set_port_lm(s, port, shinfo, vcpu_info);
    } else {
        return do_set_port_compat(s, port, shinfo, vcpu_info);
    }
}

static int clear_port_pending(XenEvtchnState *s, evtchn_port_t port)
{
    void *p = xen_overlay_get_shinfo_ptr();

    if (!p) {
        return -ENOTSUP;
    }

    if (xen_is_long_mode()) {
        struct shared_info *shinfo = p;
        const int bits_per_word = BITS_PER_BYTE * sizeof(shinfo->evtchn_pending[0]);
        typeof(shinfo->evtchn_pending[0]) mask;
        int idx = port / bits_per_word;
        int offset = port % bits_per_word;

        mask = 1UL << offset;

        qatomic_fetch_and(&shinfo->evtchn_pending[idx], ~mask);
    } else {
        struct compat_shared_info *shinfo = p;
        const int bits_per_word = BITS_PER_BYTE * sizeof(shinfo->evtchn_pending[0]);
        typeof(shinfo->evtchn_pending[0]) mask;
        int idx = port / bits_per_word;
        int offset = port % bits_per_word;

        mask = 1UL << offset;

        qatomic_fetch_and(&shinfo->evtchn_pending[idx], ~mask);
    }
    return 0;
}

static void free_port(XenEvtchnState *s, evtchn_port_t port)
{
    s->port_table[port].type = EVTCHNSTAT_closed;
    s->port_table[port].type_val = 0;
    s->port_table[port].vcpu = 0;

    if (s->nr_ports == port + 1) {
        do {
            s->nr_ports--;
        } while (s->nr_ports &&
                 s->port_table[s->nr_ports - 1].type == EVTCHNSTAT_closed);
    }

    /* Clear pending event to avoid unexpected behavior on re-bind. */
    clear_port_pending(s, port);
}

static int allocate_port(XenEvtchnState *s, uint32_t vcpu, uint16_t type,
                         uint16_t val, evtchn_port_t *port)
{
    evtchn_port_t p = 1;

    for (p = 1; valid_port(p); p++) {
        if (s->port_table[p].type == EVTCHNSTAT_closed) {
            s->port_table[p].vcpu = vcpu;
            s->port_table[p].type = type;
            s->port_table[p].type_val = val;

            *port = p;

            if (s->nr_ports < p + 1) {
                s->nr_ports = p + 1;
            }

            return 0;
        }
    }
    return -ENOSPC;
}

static bool virq_is_global(uint32_t virq)
{
    switch (virq) {
    case VIRQ_TIMER:
    case VIRQ_DEBUG:
    case VIRQ_XENOPROF:
    case VIRQ_XENPMU:
        return false;

    default:
        return true;
    }
}

static int close_port(XenEvtchnState *s, evtchn_port_t port)
{
    XenEvtchnPort *p = &s->port_table[port];

    switch (p->type) {
    case EVTCHNSTAT_closed:
        return -ENOENT;

    case EVTCHNSTAT_virq:
        kvm_xen_set_vcpu_virq(virq_is_global(p->type_val) ? 0 : p->vcpu,
                              p->type_val, 0);
        break;

    case EVTCHNSTAT_ipi:
        if (s->evtchn_in_kernel) {
            deassign_kernel_port(port);
        }
        break;

    default:
        break;
    }

    free_port(s, port);
    return 0;
}

int xen_evtchn_close_op(struct evtchn_close *close)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (!valid_port(close->port)) {
        return -EINVAL;
    }

    qemu_mutex_lock(&s->port_lock);

    ret = close_port(s, close->port);

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

int xen_evtchn_unmask_op(struct evtchn_unmask *unmask)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (!valid_port(unmask->port)) {
        return -EINVAL;
    }

    qemu_mutex_lock(&s->port_lock);

    ret = unmask_port(s, unmask->port, true);

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

int xen_evtchn_bind_virq_op(struct evtchn_bind_virq *virq)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (virq->virq >= NR_VIRQS) {
        return -EINVAL;
    }

    /* Global VIRQ must be allocated on vCPU0 first */
    if (virq_is_global(virq->virq) && virq->vcpu != 0) {
        return -EINVAL;
    }

    if (!valid_vcpu(virq->vcpu)) {
        return -ENOENT;
    }

    qemu_mutex_lock(&s->port_lock);

    ret = allocate_port(s, virq->vcpu, EVTCHNSTAT_virq, virq->virq,
                        &virq->port);
    if (!ret) {
        ret = kvm_xen_set_vcpu_virq(virq->vcpu, virq->virq, virq->port);
        if (ret) {
            free_port(s, virq->port);
        }
    }

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

int xen_evtchn_bind_ipi_op(struct evtchn_bind_ipi *ipi)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (!valid_vcpu(ipi->vcpu)) {
        return -ENOENT;
    }

    qemu_mutex_lock(&s->port_lock);

    ret = allocate_port(s, ipi->vcpu, EVTCHNSTAT_ipi, 0, &ipi->port);
    if (!ret && s->evtchn_in_kernel) {
        assign_kernel_port(EVTCHNSTAT_ipi, ipi->port, ipi->vcpu);
    }

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

int xen_evtchn_send_op(struct evtchn_send *send)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    XenEvtchnPort *p;
    int ret = 0;

    if (!s) {
        return -ENOTSUP;
    }

    if (!valid_port(send->port)) {
        return -EINVAL;
    }

    qemu_mutex_lock(&s->port_lock);

    p = &s->port_table[send->port];

    switch (p->type) {
    case EVTCHNSTAT_interdomain:
        if (p->type_val & PORT_INFO_TYPEVAL_REMOTE_QEMU) {
            /*
             * This is an event from the guest to qemu itself, which is
             * serving as the driver domain. Not yet implemented; it will
             * be hooked up to the qemu implementation of xenstore,
             * console, PV net/block drivers etc.
             */
            ret = -ENOSYS;
        } else {
            /* Loopback interdomain ports; just a complex IPI */
            set_port_pending(s, p->type_val);
        }
        break;

    case EVTCHNSTAT_ipi:
        set_port_pending(s, send->port);
        break;

    case EVTCHNSTAT_unbound:
        /* Xen will silently drop these */
        break;

    default:
        ret = -EINVAL;
        break;
    }

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

