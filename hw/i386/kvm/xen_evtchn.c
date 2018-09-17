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
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qmp/qdict.h"
#include "qom/object.h"
#include "exec/target_page.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/irq.h"

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
    uint32_t callback_gsi;

    QEMUBH *gsi_bh;

    QemuMutex port_lock;
    uint32_t nr_ports;
    XenEvtchnPort port_table[EVTCHN_2L_NR_CHANNELS];
    qemu_irq gsis[IOAPIC_NUM_PINS];
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

static void gsi_assert_bh(void *opaque)
{
    struct vcpu_info *vi = kvm_xen_get_vcpu_info_hva(0);
    if (vi) {
        xen_evtchn_set_callback_level(!!vi->evtchn_upcall_pending);
    }
}

void xen_evtchn_create(void)
{
    XenEvtchnState *s = XEN_EVTCHN(sysbus_create_simple(TYPE_XEN_EVTCHN,
                                                        -1, NULL));
    int i;

    xen_evtchn_singleton = s;

    qemu_mutex_init(&s->port_lock);
    s->gsi_bh = aio_bh_new(qemu_get_aio_context(), gsi_assert_bh, s);

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(s), &s->gsis[i]);
    }
}

void xen_evtchn_connect_gsis(qemu_irq *system_gsis)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int i;

    if (!s) {
        return;
    }

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(s), i, system_gsis[i]);
    }
}

static void xen_evtchn_register_types(void)
{
    type_register_static(&xen_evtchn_info);
}

type_init(xen_evtchn_register_types)

static int set_callback_pci_intx(XenEvtchnState *s, uint64_t param)
{
    PCMachineState *pcms = PC_MACHINE(qdev_get_machine());
    uint8_t pin = param & 3;
    uint8_t devfn = (param >> 8) & 0xff;
    uint16_t bus = (param >> 16) & 0xffff;
    uint16_t domain = (param >> 32) & 0xffff;
    PCIDevice *pdev;
    PCIINTxRoute r;

    if (domain || !pcms) {
        return 0;
    }

    pdev = pci_find_device(pcms->bus, bus, devfn);
    if (!pdev) {
        return 0;
    }

    r = pci_device_route_intx_to_irq(pdev, pin);
    if (r.mode != PCI_INTX_ENABLED) {
        return 0;
    }

    /*
     * Hm, can we be notified of INTX routing changes? Not without
     * *owning* the device and being allowed to overwrite its own
     * ->intx_routing_notifier, AFAICT. So let's not.
     */
    return r.irq;
}

void xen_evtchn_set_callback_level(int level)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    if (!s) {
        return;
    }

    /*
     * We get to this function in a number of ways:
     *
     *  • From I/O context, via PV backend drivers sending a notification to
     *    the guest.
     *
     *  • From guest vCPU context, via loopback interdomain event channels
     *    (or theoretically even IPIs but guests don't use those with GSI
     *    delivery because that's pointless. We don't want a malicious guest
     *    to be able to trigger a deadlock though, so we can't rule it out.)
     *
     *  • From guest vCPU context when the HVM_PARAM_CALLBACK_IRQ is being
     *    configured.
     *
     *  • From guest vCPU context in the KVM exit handler, if the upcall
     *    pending flag has been cleared and the GSI needs to be deasserted.
     *
     *  • Maybe in future, in an interrupt ack/eoi notifier when the GSI has
     *    been acked in the irqchip.
     *
     * Whichever context we come from if we aren't already holding the BQL
     * then e can't take it now, as we may already hold s->port_lock. So
     * trigger the BH to set the IRQ for us instead of doing it immediately.
     *
     * In the HVM_PARAM_CALLBACK_IRQ and KVM exit handler cases, the caller
     * will deliberately take the BQL because they want the change to take
     * effect immediately. That just leaves interdomain loopback as the case
     * which uses the BH.
     */
    if (!qemu_mutex_iothread_locked()) {
        qemu_bh_schedule(s->gsi_bh);
        return;
    }

    if (s->callback_gsi && s->callback_gsi < IOAPIC_NUM_PINS) {
        qemu_set_irq(s->gsis[s->callback_gsi], level);
        if (level) {
            /* Ensure the vCPU polls for deassertion */
            kvm_xen_set_callback_asserted();
        }
    }
}

int xen_evtchn_set_callback_param(uint64_t param)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    struct kvm_xen_hvm_attr xa = {
        .type = KVM_XEN_ATTR_TYPE_UPCALL_VECTOR,
        .u.vector = 0,
    };
    bool in_kernel = false;
    uint32_t gsi = 0;
    int type = param >> CALLBACK_VIA_TYPE_SHIFT;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    /*
     * We need the BQL because set_callback_pci_intx() may call into PCI code,
     * and because we may need to manipulate the old and new GSI levels.
     */
    assert(qemu_mutex_iothread_locked());
    qemu_mutex_lock(&s->port_lock);

    switch (type) {
    case HVM_PARAM_CALLBACK_TYPE_VECTOR: {
        xa.u.vector = (uint8_t)param,

        ret = kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_SET_ATTR, &xa);
        if (!ret && kvm_xen_has_cap(EVTCHN_SEND)) {
            in_kernel = true;
        }
        gsi = 0;
        break;
    }

    case HVM_PARAM_CALLBACK_TYPE_PCI_INTX:
        gsi = set_callback_pci_intx(s, param);
        ret = gsi ? 0 : -EINVAL;
        break;

    case HVM_PARAM_CALLBACK_TYPE_GSI:
        gsi = (uint32_t)param;
        ret = 0;
        break;

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

        if (gsi != s->callback_gsi) {
            struct vcpu_info *vi = kvm_xen_get_vcpu_info_hva(0);

            xen_evtchn_set_callback_level(0);
            s->callback_gsi = gsi;

            if (gsi && vi && vi->evtchn_upcall_pending) {
                kvm_xen_inject_vcpu_callback_vector(0, type);
            }
        }
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

    case EVTCHNSTAT_interdomain:
        if (p->type_val & PORT_INFO_TYPEVAL_REMOTE_QEMU) {
            /* Not yet implemented. This can't happen! */
        } else {
            /* Loopback interdomain */
            XenEvtchnPort *rp = &s->port_table[p->type_val];
            if (!valid_port(p->type_val) || rp->type_val != port ||
                rp->type != EVTCHNSTAT_interdomain) {
                error_report("Inconsistent state for interdomain unbind");
            } else {
                /* Set the other end back to unbound */
                rp->type = EVTCHNSTAT_unbound;
                rp->type_val = 0;
            }
        }
        break;

    default:
        break;
    }

    free_port(s, port);
    return 0;
}

int xen_evtchn_soft_reset(void)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int i;

    if (!s) {
        return -ENOTSUP;
    }

    assert(qemu_mutex_iothread_locked());

    QEMU_LOCK_GUARD(&s->port_lock);

    for (i = 0; i < s->nr_ports; i++) {
        close_port(s, i);
    }

    return 0;
}

int xen_evtchn_reset_op(struct evtchn_reset *reset)
{
    if (reset->dom != DOMID_SELF && reset->dom != xen_domid) {
        return -ESRCH;
    }

    return xen_evtchn_soft_reset();
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

int xen_evtchn_bind_vcpu_op(struct evtchn_bind_vcpu *vcpu)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    XenEvtchnPort *p;
    int ret = -EINVAL;

    if (!s) {
        return -ENOTSUP;
    }

    if (!valid_port(vcpu->port)) {
        return -EINVAL;
    }

    if (!valid_vcpu(vcpu->vcpu)) {
        return -ENOENT;
    }

    qemu_mutex_lock(&s->port_lock);

    p = &s->port_table[vcpu->port];

    if (p->type == EVTCHNSTAT_interdomain ||
        p->type == EVTCHNSTAT_unbound ||
        p->type == EVTCHNSTAT_pirq ||
        (p->type == EVTCHNSTAT_virq && virq_is_global(p->type_val))) {
        /*
         * unmask_port() with do_unmask==false will just raise the event
         * on the new vCPU if the port was already pending.
         */
        p->vcpu = vcpu->vcpu;
        unmask_port(s, vcpu->port, false);
        ret = 0;
    }

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

int xen_evtchn_bind_interdomain_op(struct evtchn_bind_interdomain *interdomain)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    uint16_t type_val;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (interdomain->remote_dom == DOMID_QEMU) {
        type_val = PORT_INFO_TYPEVAL_REMOTE_QEMU;
    } else if (interdomain->remote_dom == DOMID_SELF ||
               interdomain->remote_dom == xen_domid) {
        type_val = 0;
    } else {
        return -ESRCH;
    }

    if (!valid_port(interdomain->remote_port)) {
        return -EINVAL;
    }

    qemu_mutex_lock(&s->port_lock);

    /* The newly allocated port starts out as unbound */
    ret = allocate_port(s, 0, EVTCHNSTAT_unbound, type_val,
                        &interdomain->local_port);
    if (ret) {
        goto out;
    }

    if (interdomain->remote_dom == DOMID_QEMU) {
        /* We haven't hooked up QEMU's PV drivers to this yet */
        ret = -ENOSYS;
    } else {
        /* Loopback */
        XenEvtchnPort *rp = &s->port_table[interdomain->remote_port];
        XenEvtchnPort *lp = &s->port_table[interdomain->local_port];

        if (rp->type == EVTCHNSTAT_unbound && rp->type_val == 0) {
            /* It's a match! */
            rp->type = EVTCHNSTAT_interdomain;
            rp->type_val = interdomain->local_port;

            lp->type = EVTCHNSTAT_interdomain;
            lp->type_val = interdomain->remote_port;
        } else {
            ret = -EINVAL;
        }
    }

    if (ret) {
        free_port(s, interdomain->local_port);
    }
 out:
    qemu_mutex_unlock(&s->port_lock);

    return ret;

}
int xen_evtchn_alloc_unbound_op(struct evtchn_alloc_unbound *alloc)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    uint16_t type_val;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (alloc->dom != DOMID_SELF && alloc->dom != xen_domid) {
        return -ESRCH;
    }

    if (alloc->remote_dom == DOMID_QEMU) {
        type_val = PORT_INFO_TYPEVAL_REMOTE_QEMU;
    } else if (alloc->remote_dom == DOMID_SELF ||
               alloc->remote_dom == xen_domid) {
        type_val = 0;
    } else {
        return -EPERM;
    }

    qemu_mutex_lock(&s->port_lock);

    ret = allocate_port(s, 0, EVTCHNSTAT_unbound, type_val, &alloc->port);

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

int xen_evtchn_set_port(uint16_t port)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    XenEvtchnPort *p;
    int ret = -EINVAL;

    if (!s) {
        return -ENOTSUP;
    }

    if (!valid_port(port)) {
        return -EINVAL;
    }

    qemu_mutex_lock(&s->port_lock);

    p = &s->port_table[port];

    /* QEMU has no business sending to anything but these */
    if (p->type == EVTCHNSTAT_virq ||
        (p->type == EVTCHNSTAT_interdomain &&
         (p->type_val & PORT_INFO_TYPEVAL_REMOTE_QEMU))) {
        set_port_pending(s, port);
        ret = 0;
    }

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

EvtchnInfoList *qmp_xen_event_list(Error **errp)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    EvtchnInfoList *head = NULL, **tail = &head;
    void *shinfo, *pending, *mask;
    int i;

    if (!s) {
        error_setg(errp, "Xen event channel emulation not enabled");
        return NULL;
    }

    shinfo = xen_overlay_get_shinfo_ptr();
    if (!shinfo) {
        error_setg(errp, "Xen shared info page not allocated");
        return NULL;
    }

    if (xen_is_long_mode()) {
        pending = shinfo + offsetof(struct shared_info, evtchn_pending);
        mask = shinfo + offsetof(struct shared_info, evtchn_mask);
    } else {
        pending = shinfo + offsetof(struct compat_shared_info, evtchn_pending);
        mask = shinfo + offsetof(struct compat_shared_info, evtchn_mask);
    }

    QEMU_LOCK_GUARD(&s->port_lock);

    for (i = 0; i < s->nr_ports; i++) {
        XenEvtchnPort *p = &s->port_table[i];
        EvtchnInfo *info;

        if (p->type == EVTCHNSTAT_closed) {
            continue;
        }

        info = g_new0(EvtchnInfo, 1);

        info->port = i;
        qemu_build_assert(EVTCHN_PORT_TYPE_CLOSED == EVTCHNSTAT_closed);
        qemu_build_assert(EVTCHN_PORT_TYPE_UNBOUND == EVTCHNSTAT_unbound);
        qemu_build_assert(EVTCHN_PORT_TYPE_INTERDOMAIN == EVTCHNSTAT_interdomain);
        qemu_build_assert(EVTCHN_PORT_TYPE_PIRQ == EVTCHNSTAT_pirq);
        qemu_build_assert(EVTCHN_PORT_TYPE_VIRQ == EVTCHNSTAT_virq);
        qemu_build_assert(EVTCHN_PORT_TYPE_IPI == EVTCHNSTAT_ipi);

        info->type = p->type;
        if (p->type == EVTCHNSTAT_interdomain) {
            info->remote_domain = g_strdup((p->type_val & PORT_INFO_TYPEVAL_REMOTE_QEMU) ?
                                           "qemu" : "loopback");
            info->target = p->type_val & PORT_INFO_TYPEVAL_REMOTE_PORT_MASK;
        } else {
            info->target = p->type_val;
        }
        info->vcpu = p->vcpu;
        info->pending = test_bit(i, pending);
        info->masked = test_bit(i, mask);

        QAPI_LIST_APPEND(tail, info);
    }

    return head;
}

void qmp_xen_event_inject(uint32_t port, Error **errp)
{
    XenEvtchnState *s = xen_evtchn_singleton;

    if (!s) {
        error_setg(errp, "Xen event channel emulation not enabled");
        return;
    }

    if (!valid_port(port)) {
        error_setg(errp, "Invalid port %u", port);
    }

    QEMU_LOCK_GUARD(&s->port_lock);

    if (set_port_pending(s, port)) {
        error_setg(errp, "Failed to set port %u", port);
        return;
    }
}

void hmp_xen_event_list(Monitor *mon, const QDict *qdict)
{
    EvtchnInfoList *iter, *info_list;
    Error *err = NULL;

    info_list = qmp_xen_event_list(&err);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }

    for (iter = info_list; iter; iter = iter->next) {
        EvtchnInfo *info = iter->value;

        monitor_printf(mon, "port %4u: vcpu: %d %s", info->port, info->vcpu,
                       EvtchnPortType_str(info->type));
        if (info->type != EVTCHN_PORT_TYPE_IPI) {
            monitor_printf(mon,  "(");
            if (info->remote_domain) {
                monitor_printf(mon, "%s:", info->remote_domain);
            }
            monitor_printf(mon, "%d)", info->target);
        }
        if (info->pending) {
            monitor_printf(mon, " PENDING");
        }
        if (info->masked) {
            monitor_printf(mon, " MASKED");
        }
        monitor_printf(mon, "\n");
    }

    qapi_free_EvtchnInfoList(info_list);
}

void hmp_xen_event_inject(Monitor *mon, const QDict *qdict)
{
    int port = qdict_get_int(qdict, "port");
    Error *err = NULL;

    qmp_xen_event_inject(port, &err);
    if (err) {
        hmp_handle_error(mon, err);
    } else {
        monitor_printf(mon, "Delivered port %d\n", port);
    }
}

