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
#include "qemu/error-report.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qmp/qdict.h"
#include "qom/object.h"
#include "exec/target_page.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"
#include "trace.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/irq.h"
#include "hw/xen/xen_backend_ops.h"

#include "xen_evtchn.h"
#include "xen_overlay.h"
#include "xen_xenstore.h"

#include "sysemu/kvm.h"
#include "sysemu/kvm_xen.h"
#include <linux/kvm.h>
#include <sys/eventfd.h>

#include "hw/xen/interface/memory.h"
#include "hw/xen/interface/hvm/params.h"

/* XX: For kvm_update_msi_routes_all() */
#include "target/i386/kvm/kvm_i386.h"

#define TYPE_XEN_EVTCHN "xen-evtchn"
OBJECT_DECLARE_SIMPLE_TYPE(XenEvtchnState, XEN_EVTCHN)

typedef struct XenEvtchnPort {
    uint32_t vcpu;      /* Xen/ACPI vcpu_id */
    uint16_t type;      /* EVTCHNSTAT_xxxx */
    union {
        uint16_t val;  /* raw value for serialization etc. */
        uint16_t pirq;
        uint16_t virq;
        struct {
            uint16_t port:15;
            uint16_t to_qemu:1; /* Only two targets; qemu or loopback */
        } interdomain;
    } u;
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

/* Local private implementation of struct xenevtchn_handle */
struct xenevtchn_handle {
    evtchn_port_t be_port;
    evtchn_port_t guest_port; /* Or zero for unbound */
    int fd;
};

/*
 * These 'emuirq' values are used by Xen in the LM stream... and yes, I am
 * insane enough to think about guest-transparent live migration from actual
 * Xen to QEMU, and ensuring that we can convert/consume the stream.
 */
#define IRQ_UNBOUND -1
#define IRQ_PT -2
#define IRQ_MSI_EMU -3


struct pirq_info {
    int gsi;
    uint16_t port;
    PCIDevice *dev;
    int vector;
    bool is_msix;
    bool is_masked;
    bool is_translated;
};

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

    /* Connected to the system GSIs for raising callback as GSI / INTx */
    unsigned int nr_callback_gsis;
    qemu_irq *callback_gsis;

    struct xenevtchn_handle *be_handles[EVTCHN_2L_NR_CHANNELS];

    uint32_t nr_pirqs;

    /* Bitmap of allocated PIRQs (serialized) */
    uint16_t nr_pirq_inuse_words;
    uint64_t *pirq_inuse_bitmap;

    /* GSI → PIRQ mapping (serialized) */
    uint16_t gsi_pirq[IOAPIC_NUM_PINS];

    /* Per-GSI assertion state (serialized) */
    uint32_t pirq_gsi_set;

    /* Per-PIRQ information (rebuilt on migration, protected by BQL) */
    struct pirq_info *pirq;
};

#define pirq_inuse_word(s, pirq) (s->pirq_inuse_bitmap[((pirq) / 64)])
#define pirq_inuse_bit(pirq) (1ULL << ((pirq) & 63))

#define pirq_inuse(s, pirq) (pirq_inuse_word(s, pirq) & pirq_inuse_bit(pirq))

struct XenEvtchnState *xen_evtchn_singleton;

/* Top bits of callback_param are the type (HVM_PARAM_CALLBACK_TYPE_xxx) */
#define CALLBACK_VIA_TYPE_SHIFT 56

static void unbind_backend_ports(XenEvtchnState *s);

static int xen_evtchn_pre_load(void *opaque)
{
    XenEvtchnState *s = opaque;

    /* Unbind all the backend-side ports; they need to rebind */
    unbind_backend_ports(s);

    /* It'll be leaked otherwise. */
    g_free(s->pirq_inuse_bitmap);
    s->pirq_inuse_bitmap = NULL;

    return 0;
}

static int xen_evtchn_post_load(void *opaque, int version_id)
{
    XenEvtchnState *s = opaque;
    uint32_t i;

    if (s->callback_param) {
        xen_evtchn_set_callback_param(s->callback_param);
    }

    /* Rebuild s->pirq[].port mapping */
    for (i = 0; i < s->nr_ports; i++) {
        XenEvtchnPort *p = &s->port_table[i];

        if (p->type == EVTCHNSTAT_pirq) {
            assert(p->u.pirq);
            assert(p->u.pirq < s->nr_pirqs);

            /*
             * Set the gsi to IRQ_UNBOUND; it may be changed to an actual
             * GSI# below, or to IRQ_MSI_EMU when the MSI table snooping
             * catches up with it.
             */
            s->pirq[p->u.pirq].gsi = IRQ_UNBOUND;
            s->pirq[p->u.pirq].port = i;
        }
    }
    /* Rebuild s->pirq[].gsi mapping */
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        if (s->gsi_pirq[i]) {
            s->pirq[s->gsi_pirq[i]].gsi = i;
        }
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(vcpu, XenEvtchnPort),
        VMSTATE_UINT16(type, XenEvtchnPort),
        VMSTATE_UINT16(u.val, XenEvtchnPort),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription xen_evtchn_vmstate = {
    .name = "xen_evtchn",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_evtchn_is_needed,
    .pre_load = xen_evtchn_pre_load,
    .post_load = xen_evtchn_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(callback_param, XenEvtchnState),
        VMSTATE_UINT32(nr_ports, XenEvtchnState),
        VMSTATE_STRUCT_VARRAY_UINT32(port_table, XenEvtchnState, nr_ports, 1,
                                     xen_evtchn_port_vmstate, XenEvtchnPort),
        VMSTATE_UINT16_ARRAY(gsi_pirq, XenEvtchnState, IOAPIC_NUM_PINS),
        VMSTATE_VARRAY_UINT16_ALLOC(pirq_inuse_bitmap, XenEvtchnState,
                                    nr_pirq_inuse_words, 0,
                                    vmstate_info_uint64, uint64_t),
        VMSTATE_UINT32(pirq_gsi_set, XenEvtchnState),
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

static struct evtchn_backend_ops emu_evtchn_backend_ops = {
    .open = xen_be_evtchn_open,
    .bind_interdomain = xen_be_evtchn_bind_interdomain,
    .unbind = xen_be_evtchn_unbind,
    .close = xen_be_evtchn_close,
    .get_fd = xen_be_evtchn_fd,
    .notify = xen_be_evtchn_notify,
    .unmask = xen_be_evtchn_unmask,
    .pending = xen_be_evtchn_pending,
};

static void gsi_assert_bh(void *opaque)
{
    struct vcpu_info *vi = kvm_xen_get_vcpu_info_hva(0);
    if (vi) {
        xen_evtchn_set_callback_level(!!vi->evtchn_upcall_pending);
    }
}

void xen_evtchn_create(unsigned int nr_gsis, qemu_irq *system_gsis)
{
    XenEvtchnState *s = XEN_EVTCHN(sysbus_create_simple(TYPE_XEN_EVTCHN,
                                                        -1, NULL));
    int i;

    xen_evtchn_singleton = s;

    qemu_mutex_init(&s->port_lock);
    s->gsi_bh = aio_bh_new(qemu_get_aio_context(), gsi_assert_bh, s);

    /*
     * These are the *output* GSI from event channel support, for
     * signalling CPU0's events via GSI or PCI INTx instead of the
     * per-CPU vector. We create a *set* of irqs and connect one to
     * each of the system GSIs which were passed in from the platform
     * code, and then just trigger the right one as appropriate from
     * xen_evtchn_set_callback_level().
     */
    s->nr_callback_gsis = nr_gsis;
    s->callback_gsis = g_new0(qemu_irq, nr_gsis);
    for (i = 0; i < nr_gsis; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(s), &s->callback_gsis[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(s), i, system_gsis[i]);
    }

    /*
     * The Xen scheme for encoding PIRQ# into an MSI message is not
     * compatible with 32-bit MSI, as it puts the high bits of the
     * PIRQ# into the high bits of the MSI message address, instead of
     * using the Extended Destination ID in address bits 4-11 which
     * perhaps would have been a better choice.
     *
     * To keep life simple, kvm_accel_instance_init() initialises the
     * default to 256. which conveniently doesn't need to set anything
     * outside the low 32 bits of the address. It can be increased by
     * setting the xen-evtchn-max-pirq property.
     */
    s->nr_pirqs = kvm_xen_get_evtchn_max_pirq();

    s->nr_pirq_inuse_words = DIV_ROUND_UP(s->nr_pirqs, 64);
    s->pirq_inuse_bitmap = g_new0(uint64_t, s->nr_pirq_inuse_words);
    s->pirq = g_new0(struct pirq_info, s->nr_pirqs);

    /* Set event channel functions for backend drivers to use */
    xen_evtchn_ops = &emu_evtchn_backend_ops;
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

    pdev = pci_find_device(pcms->pcibus, bus, devfn);
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
    if (!bql_locked()) {
        qemu_bh_schedule(s->gsi_bh);
        return;
    }

    if (s->callback_gsi && s->callback_gsi < s->nr_callback_gsis) {
        qemu_set_irq(s->callback_gsis[s->callback_gsi], level);
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
    assert(bql_locked());
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

    /* If the guest has set a per-vCPU callback vector, prefer that. */
    if (gsi && kvm_xen_has_vcpu_callback_vector()) {
        in_kernel = kvm_xen_has_cap(EVTCHN_SEND);
        gsi = 0;
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

static int assign_kernel_eventfd(uint16_t type, evtchn_port_t port, int fd)
{
    struct kvm_xen_hvm_attr ha;

    ha.type = KVM_XEN_ATTR_TYPE_EVTCHN;
    ha.u.evtchn.send_port = port;
    ha.u.evtchn.type = type;
    ha.u.evtchn.flags = 0;
    ha.u.evtchn.deliver.eventfd.port = 0;
    ha.u.evtchn.deliver.eventfd.fd = fd;

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

static void unbind_backend_ports(XenEvtchnState *s)
{
    XenEvtchnPort *p;
    int i;

    for (i = 1; i < s->nr_ports; i++) {
        p = &s->port_table[i];
        if (p->type == EVTCHNSTAT_interdomain && p->u.interdomain.to_qemu) {
            evtchn_port_t be_port = p->u.interdomain.port;

            if (s->be_handles[be_port]) {
                /* This part will be overwritten on the load anyway. */
                p->type = EVTCHNSTAT_unbound;
                p->u.interdomain.port = 0;

                /* Leave the backend port open and unbound too. */
                if (kvm_xen_has_cap(EVTCHN_SEND)) {
                    deassign_kernel_port(i);
                }
                s->be_handles[be_port]->guest_port = 0;
            }
        }
    }
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
        status->u.unbound.dom = p->u.interdomain.to_qemu ? DOMID_QEMU
                                                         : xen_domid;
        break;

    case EVTCHNSTAT_interdomain:
        status->u.interdomain.dom = p->u.interdomain.to_qemu ? DOMID_QEMU
                                                             : xen_domid;
        status->u.interdomain.port = p->u.interdomain.port;
        break;

    case EVTCHNSTAT_pirq:
        status->u.pirq = p->u.pirq;
        break;

    case EVTCHNSTAT_virq:
        status->u.virq = p->u.virq;
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
    s->port_table[port].u.val = 0;
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
            s->port_table[p].u.val = val;

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

static int close_port(XenEvtchnState *s, evtchn_port_t port,
                      bool *flush_kvm_routes)
{
    XenEvtchnPort *p = &s->port_table[port];

    /* Because it *might* be a PIRQ port */
    assert(bql_locked());

    switch (p->type) {
    case EVTCHNSTAT_closed:
        return -ENOENT;

    case EVTCHNSTAT_pirq:
        s->pirq[p->u.pirq].port = 0;
        if (s->pirq[p->u.pirq].is_translated) {
            *flush_kvm_routes = true;
        }
        break;

    case EVTCHNSTAT_virq:
        kvm_xen_set_vcpu_virq(virq_is_global(p->u.virq) ? 0 : p->vcpu,
                              p->u.virq, 0);
        break;

    case EVTCHNSTAT_ipi:
        if (s->evtchn_in_kernel) {
            deassign_kernel_port(port);
        }
        break;

    case EVTCHNSTAT_interdomain:
        if (p->u.interdomain.to_qemu) {
            uint16_t be_port = p->u.interdomain.port;
            struct xenevtchn_handle *xc = s->be_handles[be_port];
            if (xc) {
                if (kvm_xen_has_cap(EVTCHN_SEND)) {
                    deassign_kernel_port(port);
                }
                xc->guest_port = 0;
            }
        } else {
            /* Loopback interdomain */
            XenEvtchnPort *rp = &s->port_table[p->u.interdomain.port];
            if (!valid_port(p->u.interdomain.port) ||
                rp->u.interdomain.port != port ||
                rp->type != EVTCHNSTAT_interdomain) {
                error_report("Inconsistent state for interdomain unbind");
            } else {
                /* Set the other end back to unbound */
                rp->type = EVTCHNSTAT_unbound;
                rp->u.interdomain.port = 0;
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
    bool flush_kvm_routes;
    int i;

    if (!s) {
        return -ENOTSUP;
    }

    assert(bql_locked());

    qemu_mutex_lock(&s->port_lock);

    for (i = 0; i < s->nr_ports; i++) {
        close_port(s, i, &flush_kvm_routes);
    }

    qemu_mutex_unlock(&s->port_lock);

    if (flush_kvm_routes) {
        kvm_update_msi_routes_all(NULL, true, 0, 0);
    }

    return 0;
}

int xen_evtchn_reset_op(struct evtchn_reset *reset)
{
    if (reset->dom != DOMID_SELF && reset->dom != xen_domid) {
        return -ESRCH;
    }

    BQL_LOCK_GUARD();
    return xen_evtchn_soft_reset();
}

int xen_evtchn_close_op(struct evtchn_close *close)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    bool flush_kvm_routes = false;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (!valid_port(close->port)) {
        return -EINVAL;
    }

    BQL_LOCK_GUARD();
    qemu_mutex_lock(&s->port_lock);

    ret = close_port(s, close->port, &flush_kvm_routes);

    qemu_mutex_unlock(&s->port_lock);

    if (flush_kvm_routes) {
        kvm_update_msi_routes_all(NULL, true, 0, 0);
    }

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
        (p->type == EVTCHNSTAT_virq && virq_is_global(p->u.virq))) {
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

int xen_evtchn_bind_pirq_op(struct evtchn_bind_pirq *pirq)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (pirq->pirq >= s->nr_pirqs) {
        return -EINVAL;
    }

    BQL_LOCK_GUARD();

    if (s->pirq[pirq->pirq].port) {
        return -EBUSY;
    }

    qemu_mutex_lock(&s->port_lock);

    ret = allocate_port(s, 0, EVTCHNSTAT_pirq, pirq->pirq,
                        &pirq->port);
    if (ret) {
        qemu_mutex_unlock(&s->port_lock);
        return ret;
    }

    s->pirq[pirq->pirq].port = pirq->port;
    trace_kvm_xen_bind_pirq(pirq->pirq, pirq->port);

    qemu_mutex_unlock(&s->port_lock);

    /*
     * Need to do the unmask outside port_lock because it may call
     * back into the MSI translate function.
     */
    if (s->pirq[pirq->pirq].gsi == IRQ_MSI_EMU) {
        if (s->pirq[pirq->pirq].is_masked) {
            PCIDevice *dev = s->pirq[pirq->pirq].dev;
            int vector = s->pirq[pirq->pirq].vector;
            char *dev_path = qdev_get_dev_path(DEVICE(dev));

            trace_kvm_xen_unmask_pirq(pirq->pirq, dev_path, vector);
            g_free(dev_path);

            if (s->pirq[pirq->pirq].is_msix) {
                msix_set_mask(dev, vector, false);
            } else {
                msi_set_mask(dev, vector, false, NULL);
            }
        } else if (s->pirq[pirq->pirq].is_translated) {
            /*
             * If KVM had attempted to translate this one before, make it try
             * again. If we unmasked, then the notifier on the MSI(-X) vector
             * will already have had the same effect.
             */
            kvm_update_msi_routes_all(NULL, true, 0, 0);
        }
    }

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
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (interdomain->remote_dom != DOMID_QEMU &&
        interdomain->remote_dom != DOMID_SELF &&
        interdomain->remote_dom != xen_domid) {
        return -ESRCH;
    }

    if (!valid_port(interdomain->remote_port)) {
        return -EINVAL;
    }

    qemu_mutex_lock(&s->port_lock);

    /* The newly allocated port starts out as unbound */
    ret = allocate_port(s, 0, EVTCHNSTAT_unbound, 0, &interdomain->local_port);

    if (ret) {
        goto out;
    }

    if (interdomain->remote_dom == DOMID_QEMU) {
        struct xenevtchn_handle *xc = s->be_handles[interdomain->remote_port];
        XenEvtchnPort *lp = &s->port_table[interdomain->local_port];

        if (!xc) {
            ret = -ENOENT;
            goto out_free_port;
        }

        if (xc->guest_port) {
            ret = -EBUSY;
            goto out_free_port;
        }

        assert(xc->be_port == interdomain->remote_port);
        xc->guest_port = interdomain->local_port;
        if (kvm_xen_has_cap(EVTCHN_SEND)) {
            assign_kernel_eventfd(lp->type, xc->guest_port, xc->fd);
        }
        lp->type = EVTCHNSTAT_interdomain;
        lp->u.interdomain.to_qemu = 1;
        lp->u.interdomain.port = interdomain->remote_port;
        ret = 0;
    } else {
        /* Loopback */
        XenEvtchnPort *rp = &s->port_table[interdomain->remote_port];
        XenEvtchnPort *lp = &s->port_table[interdomain->local_port];

        /*
         * The 'remote' port for loopback must be an unbound port allocated
         * for communication with the local domain, and must *not* be the
         * port that was just allocated for the local end.
         */
        if (interdomain->local_port != interdomain->remote_port &&
            rp->type == EVTCHNSTAT_unbound && !rp->u.interdomain.to_qemu) {

            rp->type = EVTCHNSTAT_interdomain;
            rp->u.interdomain.port = interdomain->local_port;

            lp->type = EVTCHNSTAT_interdomain;
            lp->u.interdomain.port = interdomain->remote_port;
        } else {
            ret = -EINVAL;
        }
    }

 out_free_port:
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
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (alloc->dom != DOMID_SELF && alloc->dom != xen_domid) {
        return -ESRCH;
    }

    if (alloc->remote_dom != DOMID_QEMU &&
        alloc->remote_dom != DOMID_SELF &&
        alloc->remote_dom != xen_domid) {
        return -EPERM;
    }

    qemu_mutex_lock(&s->port_lock);

    ret = allocate_port(s, 0, EVTCHNSTAT_unbound, 0, &alloc->port);

    if (!ret && alloc->remote_dom == DOMID_QEMU) {
        XenEvtchnPort *p = &s->port_table[alloc->port];
        p->u.interdomain.to_qemu = 1;
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
        if (p->u.interdomain.to_qemu) {
            /*
             * This is an event from the guest to qemu itself, which is
             * serving as the driver domain.
             */
            uint16_t be_port = p->u.interdomain.port;
            struct xenevtchn_handle *xc = s->be_handles[be_port];
            if (xc) {
                eventfd_write(xc->fd, 1);
                ret = 0;
            } else {
                ret = -ENOENT;
            }
        } else {
            /* Loopback interdomain ports; just a complex IPI */
            set_port_pending(s, p->u.interdomain.port);
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
        (p->type == EVTCHNSTAT_interdomain && p->u.interdomain.to_qemu)) {
        set_port_pending(s, port);
        ret = 0;
    }

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

static int allocate_pirq(XenEvtchnState *s, int type, int gsi)
{
    uint16_t pirq;

    /*
     * Preserve the allocation strategy that Xen has. It looks like
     * we *never* give out PIRQ 0-15, we give out 16-nr_irqs_gsi only
     * to GSIs (counting up from 16), and then we count backwards from
     * the top for MSIs or when the GSI space is exhausted.
     */
    if (type == MAP_PIRQ_TYPE_GSI) {
        for (pirq = 16 ; pirq < IOAPIC_NUM_PINS; pirq++) {
            if (pirq_inuse(s, pirq)) {
                continue;
            }

            /* Found it */
            goto found;
        }
    }
    for (pirq = s->nr_pirqs - 1; pirq >= IOAPIC_NUM_PINS; pirq--) {
        /* Skip whole words at a time when they're full */
        if (pirq_inuse_word(s, pirq) == UINT64_MAX) {
            pirq &= ~63ULL;
            continue;
        }
        if (pirq_inuse(s, pirq)) {
            continue;
        }

        goto found;
    }
    return -ENOSPC;

 found:
    pirq_inuse_word(s, pirq) |= pirq_inuse_bit(pirq);
    if (gsi >= 0) {
        assert(gsi < IOAPIC_NUM_PINS);
        s->gsi_pirq[gsi] = pirq;
    }
    s->pirq[pirq].gsi = gsi;
    return pirq;
}

bool xen_evtchn_set_gsi(int gsi, int level)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int pirq;

    assert(bql_locked());

    if (!s || gsi < 0 || gsi >= IOAPIC_NUM_PINS) {
        return false;
    }

    /*
     * Check that that it *isn't* the event channel GSI, and thus
     * that we are not recursing and it's safe to take s->port_lock.
     *
     * Locking aside, it's perfectly sane to bail out early for that
     * special case, as it would make no sense for the event channel
     * GSI to be routed back to event channels, when the delivery
     * method is to raise the GSI... that recursion wouldn't *just*
     * be a locking issue.
     */
    if (gsi && gsi == s->callback_gsi) {
        return false;
    }

    QEMU_LOCK_GUARD(&s->port_lock);

    pirq = s->gsi_pirq[gsi];
    if (!pirq) {
        return false;
    }

    if (level) {
        int port = s->pirq[pirq].port;

        s->pirq_gsi_set |= (1U << gsi);
        if (port) {
            set_port_pending(s, port);
        }
    } else {
        s->pirq_gsi_set &= ~(1U << gsi);
    }
    return true;
}

static uint32_t msi_pirq_target(uint64_t addr, uint32_t data)
{
    /* The vector (in low 8 bits of data) must be zero */
    if (data & 0xff) {
        return 0;
    }

    uint32_t pirq = (addr & 0xff000) >> 12;
    pirq |= (addr >> 32) & 0xffffff00;

    return pirq;
}

static void do_remove_pci_vector(XenEvtchnState *s, PCIDevice *dev, int vector,
                                 int except_pirq)
{
    uint32_t pirq;

    for (pirq = 0; pirq < s->nr_pirqs; pirq++) {
        /*
         * We could be cleverer here, but it isn't really a fast path, and
         * this trivial optimisation is enough to let us skip the big gap
         * in the middle a bit quicker (in terms of both loop iterations,
         * and cache lines).
         */
        if (!(pirq & 63) && !(pirq_inuse_word(s, pirq))) {
            pirq += 64;
            continue;
        }
        if (except_pirq && pirq == except_pirq) {
            continue;
        }
        if (s->pirq[pirq].dev != dev) {
            continue;
        }
        if (vector != -1 && s->pirq[pirq].vector != vector) {
            continue;
        }

        /* It could theoretically be bound to a port already, but that is OK. */
        s->pirq[pirq].dev = dev;
        s->pirq[pirq].gsi = IRQ_UNBOUND;
        s->pirq[pirq].is_msix = false;
        s->pirq[pirq].vector = 0;
        s->pirq[pirq].is_masked = false;
        s->pirq[pirq].is_translated = false;
    }
}

void xen_evtchn_remove_pci_device(PCIDevice *dev)
{
    XenEvtchnState *s = xen_evtchn_singleton;

    if (!s) {
        return;
    }

    QEMU_LOCK_GUARD(&s->port_lock);
    do_remove_pci_vector(s, dev, -1, 0);
}

void xen_evtchn_snoop_msi(PCIDevice *dev, bool is_msix, unsigned int vector,
                          uint64_t addr, uint32_t data, bool is_masked)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    uint32_t pirq;

    if (!s) {
        return;
    }

    assert(bql_locked());

    pirq = msi_pirq_target(addr, data);

    /*
     * The PIRQ# must be sane, and there must be an allocated PIRQ in
     * IRQ_UNBOUND or IRQ_MSI_EMU state to match it.
     */
    if (!pirq || pirq >= s->nr_pirqs || !pirq_inuse(s, pirq) ||
        (s->pirq[pirq].gsi != IRQ_UNBOUND &&
         s->pirq[pirq].gsi != IRQ_MSI_EMU)) {
        pirq = 0;
    }

    if (pirq) {
        s->pirq[pirq].dev = dev;
        s->pirq[pirq].gsi = IRQ_MSI_EMU;
        s->pirq[pirq].is_msix = is_msix;
        s->pirq[pirq].vector = vector;
        s->pirq[pirq].is_masked = is_masked;
    }

    /* Remove any (other) entries for this {device, vector} */
    do_remove_pci_vector(s, dev, vector, pirq);
}

int xen_evtchn_translate_pirq_msi(struct kvm_irq_routing_entry *route,
                                  uint64_t address, uint32_t data)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    uint32_t pirq, port;
    CPUState *cpu;

    if (!s) {
        return 1; /* Not a PIRQ */
    }

    assert(bql_locked());

    pirq = msi_pirq_target(address, data);
    if (!pirq || pirq >= s->nr_pirqs) {
        return 1; /* Not a PIRQ */
    }

    if (!kvm_xen_has_cap(EVTCHN_2LEVEL)) {
        return -ENOTSUP;
    }

    if (s->pirq[pirq].gsi != IRQ_MSI_EMU) {
        return -EINVAL;
    }

    /* Remember that KVM tried to translate this. It might need to try again. */
    s->pirq[pirq].is_translated = true;

    QEMU_LOCK_GUARD(&s->port_lock);

    port = s->pirq[pirq].port;
    if (!valid_port(port)) {
        return -EINVAL;
    }

    cpu = qemu_get_cpu(s->port_table[port].vcpu);
    if (!cpu) {
        return -EINVAL;
    }

    route->type = KVM_IRQ_ROUTING_XEN_EVTCHN;
    route->u.xen_evtchn.port = port;
    route->u.xen_evtchn.vcpu = kvm_arch_vcpu_id(cpu);
    route->u.xen_evtchn.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

    return 0; /* Handled */
}

bool xen_evtchn_deliver_pirq_msi(uint64_t address, uint32_t data)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    uint32_t pirq, port;

    if (!s) {
        return false;
    }

    assert(bql_locked());

    pirq = msi_pirq_target(address, data);
    if (!pirq || pirq >= s->nr_pirqs) {
        return false;
    }

    QEMU_LOCK_GUARD(&s->port_lock);

    port = s->pirq[pirq].port;
    if (!valid_port(port)) {
        return false;
    }

    set_port_pending(s, port);
    return true;
}

int xen_physdev_map_pirq(struct physdev_map_pirq *map)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int pirq = map->pirq;
    int gsi = map->index;

    if (!s) {
        return -ENOTSUP;
    }

    BQL_LOCK_GUARD();
    QEMU_LOCK_GUARD(&s->port_lock);

    if (map->domid != DOMID_SELF && map->domid != xen_domid) {
        return -EPERM;
    }
    if (map->type != MAP_PIRQ_TYPE_GSI) {
        return -EINVAL;
    }
    if (gsi < 0 || gsi >= IOAPIC_NUM_PINS) {
        return -EINVAL;
    }

    if (pirq < 0) {
        pirq = allocate_pirq(s, map->type, gsi);
        if (pirq < 0) {
            return pirq;
        }
        map->pirq = pirq;
    } else if (pirq > s->nr_pirqs) {
        return -EINVAL;
    } else {
        /*
         * User specified a valid-looking PIRQ#. Allow it if it is
         * allocated and not yet bound, or if it is unallocated
         */
        if (pirq_inuse(s, pirq)) {
            if (s->pirq[pirq].gsi != IRQ_UNBOUND) {
                return -EBUSY;
            }
        } else {
            /* If it was unused, mark it used now. */
            pirq_inuse_word(s, pirq) |= pirq_inuse_bit(pirq);
        }
        /* Set the mapping in both directions. */
        s->pirq[pirq].gsi = gsi;
        s->gsi_pirq[gsi] = pirq;
    }

    trace_kvm_xen_map_pirq(pirq, gsi);
    return 0;
}

int xen_physdev_unmap_pirq(struct physdev_unmap_pirq *unmap)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int pirq = unmap->pirq;
    int gsi;

    if (!s) {
        return -ENOTSUP;
    }

    if (unmap->domid != DOMID_SELF && unmap->domid != xen_domid) {
        return -EPERM;
    }
    if (pirq < 0 || pirq >= s->nr_pirqs) {
        return -EINVAL;
    }

    BQL_LOCK_GUARD();
    qemu_mutex_lock(&s->port_lock);

    if (!pirq_inuse(s, pirq)) {
        qemu_mutex_unlock(&s->port_lock);
        return -ENOENT;
    }

    gsi = s->pirq[pirq].gsi;

    /* We can only unmap GSI PIRQs */
    if (gsi < 0) {
        qemu_mutex_unlock(&s->port_lock);
        return -EINVAL;
    }

    s->gsi_pirq[gsi] = 0;
    s->pirq[pirq].gsi = IRQ_UNBOUND; /* Doesn't actually matter because: */
    pirq_inuse_word(s, pirq) &= ~pirq_inuse_bit(pirq);

    trace_kvm_xen_unmap_pirq(pirq, gsi);
    qemu_mutex_unlock(&s->port_lock);

    if (gsi == IRQ_MSI_EMU) {
        kvm_update_msi_routes_all(NULL, true, 0, 0);
    }

    return 0;
}

int xen_physdev_eoi_pirq(struct physdev_eoi *eoi)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int pirq = eoi->irq;
    int gsi;

    if (!s) {
        return -ENOTSUP;
    }

    BQL_LOCK_GUARD();
    QEMU_LOCK_GUARD(&s->port_lock);

    if (!pirq_inuse(s, pirq)) {
        return -ENOENT;
    }

    gsi = s->pirq[pirq].gsi;
    if (gsi < 0) {
        return -EINVAL;
    }

    /* Reassert a level IRQ if needed */
    if (s->pirq_gsi_set & (1U << gsi)) {
        int port = s->pirq[pirq].port;
        if (port) {
            set_port_pending(s, port);
        }
    }

    return 0;
}

int xen_physdev_query_pirq(struct physdev_irq_status_query *query)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int pirq = query->irq;

    if (!s) {
        return -ENOTSUP;
    }

    BQL_LOCK_GUARD();
    QEMU_LOCK_GUARD(&s->port_lock);

    if (!pirq_inuse(s, pirq)) {
        return -ENOENT;
    }

    if (s->pirq[pirq].gsi >= 0) {
        query->flags = XENIRQSTAT_needs_eoi;
    } else {
        query->flags = 0;
    }

    return 0;
}

int xen_physdev_get_free_pirq(struct physdev_get_free_pirq *get)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int pirq;

    if (!s) {
        return -ENOTSUP;
    }

    QEMU_LOCK_GUARD(&s->port_lock);

    pirq = allocate_pirq(s, get->type, IRQ_UNBOUND);
    if (pirq < 0) {
        return pirq;
    }

    get->pirq = pirq;
    trace_kvm_xen_get_free_pirq(pirq, get->type);
    return 0;
}

struct xenevtchn_handle *xen_be_evtchn_open(void)
{
    struct xenevtchn_handle *xc = g_new0(struct xenevtchn_handle, 1);

    xc->fd = eventfd(0, EFD_CLOEXEC);
    if (xc->fd < 0) {
        free(xc);
        return NULL;
    }

    return xc;
}

static int find_be_port(XenEvtchnState *s, struct xenevtchn_handle *xc)
{
    int i;

    for (i = 1; i < EVTCHN_2L_NR_CHANNELS; i++) {
        if (!s->be_handles[i]) {
            s->be_handles[i] = xc;
            xc->be_port = i;
            return i;
        }
    }
    return 0;
}

int xen_be_evtchn_bind_interdomain(struct xenevtchn_handle *xc, uint32_t domid,
                                   evtchn_port_t guest_port)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    XenEvtchnPort *gp;
    uint16_t be_port = 0;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (!xc) {
        return -EFAULT;
    }

    if (domid != xen_domid) {
        return -ESRCH;
    }

    if (!valid_port(guest_port)) {
        return -EINVAL;
    }

    qemu_mutex_lock(&s->port_lock);

    /* The guest has to have an unbound port waiting for us to bind */
    gp = &s->port_table[guest_port];

    switch (gp->type) {
    case EVTCHNSTAT_interdomain:
        /* Allow rebinding after migration, preserve port # if possible */
        be_port = gp->u.interdomain.port;
        assert(be_port != 0);
        if (!s->be_handles[be_port]) {
            s->be_handles[be_port] = xc;
            xc->guest_port = guest_port;
            ret = xc->be_port = be_port;
            if (kvm_xen_has_cap(EVTCHN_SEND)) {
                assign_kernel_eventfd(gp->type, guest_port, xc->fd);
            }
            break;
        }
        /* fall through */

    case EVTCHNSTAT_unbound:
        be_port = find_be_port(s, xc);
        if (!be_port) {
            ret = -ENOSPC;
            goto out;
        }

        gp->type = EVTCHNSTAT_interdomain;
        gp->u.interdomain.to_qemu = 1;
        gp->u.interdomain.port = be_port;
        xc->guest_port = guest_port;
        if (kvm_xen_has_cap(EVTCHN_SEND)) {
            assign_kernel_eventfd(gp->type, guest_port, xc->fd);
        }
        ret = be_port;
        break;

    default:
        ret = -EINVAL;
        break;
    }

 out:
    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

int xen_be_evtchn_unbind(struct xenevtchn_handle *xc, evtchn_port_t port)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (!xc) {
        return -EFAULT;
    }

    qemu_mutex_lock(&s->port_lock);

    if (port && port != xc->be_port) {
        ret = -EINVAL;
        goto out;
    }

    if (xc->guest_port) {
        XenEvtchnPort *gp = &s->port_table[xc->guest_port];

        /* This should never *not* be true */
        if (gp->type == EVTCHNSTAT_interdomain) {
            gp->type = EVTCHNSTAT_unbound;
            gp->u.interdomain.port = 0;
        }

        if (kvm_xen_has_cap(EVTCHN_SEND)) {
            deassign_kernel_port(xc->guest_port);
        }
        xc->guest_port = 0;
    }

    s->be_handles[xc->be_port] = NULL;
    xc->be_port = 0;
    ret = 0;
 out:
    qemu_mutex_unlock(&s->port_lock);
    return ret;
}

int xen_be_evtchn_close(struct xenevtchn_handle *xc)
{
    if (!xc) {
        return -EFAULT;
    }

    xen_be_evtchn_unbind(xc, 0);

    close(xc->fd);
    free(xc);
    return 0;
}

int xen_be_evtchn_fd(struct xenevtchn_handle *xc)
{
    if (!xc) {
        return -1;
    }
    return xc->fd;
}

int xen_be_evtchn_notify(struct xenevtchn_handle *xc, evtchn_port_t port)
{
    XenEvtchnState *s = xen_evtchn_singleton;
    int ret;

    if (!s) {
        return -ENOTSUP;
    }

    if (!xc) {
        return -EFAULT;
    }

    qemu_mutex_lock(&s->port_lock);

    if (xc->guest_port) {
        set_port_pending(s, xc->guest_port);
        ret = 0;
    } else {
        ret = -ENOTCONN;
    }

    qemu_mutex_unlock(&s->port_lock);

    return ret;
}

int xen_be_evtchn_pending(struct xenevtchn_handle *xc)
{
    uint64_t val;

    if (!xc) {
        return -EFAULT;
    }

    if (!xc->be_port) {
        return 0;
    }

    if (eventfd_read(xc->fd, &val)) {
        return -errno;
    }

    return val ? xc->be_port : 0;
}

int xen_be_evtchn_unmask(struct xenevtchn_handle *xc, evtchn_port_t port)
{
    if (!xc) {
        return -EFAULT;
    }

    if (xc->be_port != port) {
        return -EINVAL;
    }

    /*
     * We don't actually do anything to unmask it; the event was already
     * consumed in xen_be_evtchn_pending().
     */
    return 0;
}

int xen_be_evtchn_get_guest_port(struct xenevtchn_handle *xc)
{
    return xc->guest_port;
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
            info->remote_domain = g_strdup(p->u.interdomain.to_qemu ?
                                           "qemu" : "loopback");
            info->target = p->u.interdomain.port;
        } else {
            info->target = p->u.val; /* pirq# or virq# */
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

