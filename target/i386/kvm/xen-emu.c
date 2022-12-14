/*
 * Xen HVM emulation support in KVM
 *
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "hw/xen/xen.h"
#include "sysemu/kvm_int.h"
#include "sysemu/kvm_xen.h"
#include "kvm/kvm_i386.h"
#include "exec/address-spaces.h"
#include "xen-emu.h"
#include "trace.h"
#include "sysemu/runstate.h"

#include "hw/pci/msi.h"
#include "hw/i386/apic-msidef.h"
#include "hw/i386/kvm/xen_overlay.h"
#include "hw/i386/kvm/xen_evtchn.h"

#include "hw/xen/interface/version.h"
#include "hw/xen/interface/sched.h"
#include "hw/xen/interface/memory.h"
#include "hw/xen/interface/hvm/hvm_op.h"
#include "hw/xen/interface/hvm/params.h"
#include "hw/xen/interface/vcpu.h"
#include "hw/xen/interface/event_channel.h"

#include "xen-compat.h"

#ifdef TARGET_X86_64
#define hypercall_compat32(longmode) (!(longmode))
#else
#define hypercall_compat32(longmode) (false)
#endif

static bool kvm_gva_to_gpa(CPUState *cs, uint64_t gva, uint64_t *gpa,
                           size_t *len, bool is_write)
{
        struct kvm_translation tr = {
            .linear_address = gva,
        };

        if (len) {
            *len = TARGET_PAGE_SIZE - (gva & ~TARGET_PAGE_MASK);
        }

        if (kvm_vcpu_ioctl(cs, KVM_TRANSLATE, &tr) || !tr.valid ||
            (is_write && !tr.writeable)) {
            return false;
        }
        *gpa = tr.physical_address;
        return true;
}

static int kvm_gva_rw(CPUState *cs, uint64_t gva, void *_buf, size_t sz,
                      bool is_write)
{
    uint8_t *buf = (uint8_t *)_buf;
    uint64_t gpa;
    size_t len;

    while (sz) {
        if (!kvm_gva_to_gpa(cs, gva, &gpa, &len, is_write)) {
            return -EFAULT;
        }
        if (len > sz) {
            len = sz;
        }

        cpu_physical_memory_rw(gpa, buf, len, is_write);

        buf += len;
        sz -= len;
        gva += len;
    }

    return 0;
}

static inline int kvm_copy_from_gva(CPUState *cs, uint64_t gva, void *buf,
                                    size_t sz)
{
    return kvm_gva_rw(cs, gva, buf, sz, false);
}

static inline int kvm_copy_to_gva(CPUState *cs, uint64_t gva, void *buf,
                                  size_t sz)
{
    return kvm_gva_rw(cs, gva, buf, sz, true);
}

int kvm_xen_init(KVMState *s, uint32_t hypercall_msr)
{
    const int required_caps = KVM_XEN_HVM_CONFIG_HYPERCALL_MSR |
        KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL | KVM_XEN_HVM_CONFIG_SHARED_INFO;
    struct kvm_xen_hvm_config cfg = {
        .msr = hypercall_msr,
        .flags = KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL,
    };
    int xen_caps, ret;

    xen_caps = kvm_check_extension(s, KVM_CAP_XEN_HVM);
    if (required_caps & ~xen_caps) {
        error_report("kvm: Xen HVM guest support not present or insufficient");
        return -ENOSYS;
    }

    if (xen_caps & KVM_XEN_HVM_CONFIG_EVTCHN_SEND) {
        struct kvm_xen_hvm_attr ha = {
            .type = KVM_XEN_ATTR_TYPE_XEN_VERSION,
            .u.xen_version = s->xen_version,
        };
        (void)kvm_vm_ioctl(s, KVM_XEN_HVM_SET_ATTR, &ha);

        cfg.flags |= KVM_XEN_HVM_CONFIG_EVTCHN_SEND;
    }

    ret = kvm_vm_ioctl(s, KVM_XEN_HVM_CONFIG, &cfg);
    if (ret < 0) {
        error_report("kvm: Failed to enable Xen HVM support: %s",
                     strerror(-ret));
        return ret;
    }

    s->xen_caps = xen_caps;
    return 0;
}

int kvm_xen_init_vcpu(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int err;

    /*
     * The kernel needs to know the Xen/ACPI vCPU ID because that's
     * what the guest uses in hypercalls such as timers. It doesn't
     * match the APIC ID which is generally used for talking to the
     * kernel about vCPUs. And if vCPU threads race with creating
     * their KVM vCPUs out of order, it doesn't necessarily match
     * with the kernel's internal vCPU indices either.
     */
    if (kvm_xen_has_cap(EVTCHN_SEND)) {
        struct kvm_xen_vcpu_attr va = {
            .type = KVM_XEN_VCPU_ATTR_TYPE_VCPU_ID,
            .u.vcpu_id = cs->cpu_index,
        };
        err = kvm_vcpu_ioctl(cs, KVM_XEN_VCPU_SET_ATTR, &va);
        if (err) {
            error_report("kvm: Failed to set Xen vCPU ID attribute: %s",
                         strerror(-err));
            return err;
        }
    }

    env->xen_vcpu_info_gpa = INVALID_GPA;
    env->xen_vcpu_info_default_gpa = INVALID_GPA;
    env->xen_vcpu_time_info_gpa = INVALID_GPA;
    env->xen_vcpu_runstate_gpa = INVALID_GPA;

    return 0;
}

uint32_t kvm_xen_get_caps(void)
{
    return kvm_state->xen_caps;
}

static bool kvm_xen_hcall_xen_version(struct kvm_xen_exit *exit, X86CPU *cpu,
                                     int cmd, uint64_t arg)
{
    int err = 0;

    switch (cmd) {
    case XENVER_get_features: {
        struct xen_feature_info fi;

        /* No need for 32/64 compat handling */
        qemu_build_assert(sizeof(fi) == 8);

        err = kvm_copy_from_gva(CPU(cpu), arg, &fi, sizeof(fi));
        if (err) {
            break;
        }

        fi.submap = 0;
        if (fi.submap_idx == 0) {
            fi.submap |= 1 << XENFEAT_writable_page_tables |
                         1 << XENFEAT_writable_descriptor_tables |
                         1 << XENFEAT_auto_translated_physmap |
                         1 << XENFEAT_supervisor_mode_kernel |
                         1 << XENFEAT_hvm_callback_vector;
        }

        err = kvm_copy_to_gva(CPU(cpu), arg, &fi, sizeof(fi));
        break;
    }

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

static int kvm_xen_set_vcpu_attr(CPUState *cs, uint16_t type, uint64_t gpa)
{
    struct kvm_xen_vcpu_attr xhsi;

    xhsi.type = type;
    xhsi.u.gpa = gpa;

    trace_kvm_xen_set_vcpu_attr(cs->cpu_index, type, gpa);

    return kvm_vcpu_ioctl(cs, KVM_XEN_VCPU_SET_ATTR, &xhsi);
}

static int kvm_xen_set_vcpu_callback_vector(CPUState *cs)
{
    uint8_t vector = X86_CPU(cs)->env.xen_vcpu_callback_vector;
    struct kvm_xen_vcpu_attr xva;

    xva.type = KVM_XEN_VCPU_ATTR_TYPE_UPCALL_VECTOR;
    xva.u.vector = vector;

    trace_kvm_xen_set_vcpu_callback(cs->cpu_index, vector);

    return kvm_vcpu_ioctl(cs, KVM_XEN_HVM_SET_ATTR, &xva);
}

static void do_set_vcpu_callback_vector(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_callback_vector = data.host_int;

    if (kvm_xen_has_cap(EVTCHN_SEND)) {
        kvm_xen_set_vcpu_callback_vector(cs);
    }
}

static int set_vcpu_info(CPUState *cs, uint64_t gpa)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemoryRegionSection mrs = { .mr = NULL };
    void *vcpu_info_hva = NULL;
    int ret;

    ret = kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_VCPU_INFO, gpa);
    if (ret || gpa == INVALID_GPA) {
        goto out;
    }

    mrs = memory_region_find(get_system_memory(), gpa,
                             sizeof(struct vcpu_info));
    if (mrs.mr && mrs.mr->ram_block &&
        !int128_lt(mrs.size, int128_make64(sizeof(struct vcpu_info)))) {
        vcpu_info_hva = qemu_map_ram_ptr(mrs.mr->ram_block,
                                         mrs.offset_within_region);
    }
    if (!vcpu_info_hva) {
        if (mrs.mr) {
            memory_region_unref(mrs.mr);
            mrs.mr = NULL;
        }
        ret = -EINVAL;
    }

 out:
    if (env->xen_vcpu_info_mr) {
        memory_region_unref(env->xen_vcpu_info_mr);
    }
    env->xen_vcpu_info_hva = vcpu_info_hva;
    env->xen_vcpu_info_mr = mrs.mr;
    return ret;
}

static void do_set_vcpu_info_default_gpa(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_info_default_gpa = data.host_ulong;

    /* Changing the default does nothing if a vcpu_info was explicitly set. */
    if (env->xen_vcpu_info_gpa == INVALID_GPA) {
        set_vcpu_info(cs, env->xen_vcpu_info_default_gpa);
    }
}

static void do_set_vcpu_info_gpa(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_info_gpa = data.host_ulong;

    set_vcpu_info(cs, env->xen_vcpu_info_gpa);
}

void *kvm_xen_get_vcpu_info_hva(uint32_t vcpu_id)
{
    CPUState *cs = qemu_get_cpu(vcpu_id);
    if (!cs) {
        return NULL;
    }

    return X86_CPU(cs)->env.xen_vcpu_info_hva;
}

void kvm_xen_inject_vcpu_callback_vector(uint32_t vcpu_id, int type)
{
    CPUState *cs = qemu_get_cpu(vcpu_id);
    uint8_t vector;

    if (!cs) {
        return;
    }

    vector = X86_CPU(cs)->env.xen_vcpu_callback_vector;
    if (vector) {
        /*
         * The per-vCPU callback vector injected via lapic. Just
         * deliver it as an MSI.
         */
        MSIMessage msg = {
            .address = APIC_DEFAULT_ADDRESS | X86_CPU(cs)->apic_id,
            .data = vector | (1UL << MSI_DATA_LEVEL_SHIFT),
        };
        kvm_irqchip_send_msi(kvm_state, msg);
        return;
    }

    switch (type) {
    case HVM_PARAM_CALLBACK_TYPE_VECTOR:
        /*
         * If the evtchn_upcall_pending field in the vcpu_info is set, then
         * KVM will automatically deliver the vector on entering the vCPU
         * so all we have to do is kick it out.
         */
        qemu_cpu_kick(cs);
        break;
    }
}

static int kvm_xen_set_vcpu_timer(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    struct kvm_xen_vcpu_attr va = {
        .type = KVM_XEN_VCPU_ATTR_TYPE_TIMER,
        .u.timer.port = env->xen_virq[VIRQ_TIMER],
        .u.timer.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL,
        .u.timer.expires_ns = env->xen_singleshot_timer_ns,
    };

    return kvm_vcpu_ioctl(cs, KVM_XEN_VCPU_SET_ATTR, &va);
}

static void do_set_vcpu_timer_virq(CPUState *cs, run_on_cpu_data data)
{
    kvm_xen_set_vcpu_timer(cs);
}

int kvm_xen_set_vcpu_virq(uint32_t vcpu_id, uint16_t virq, uint16_t port)
{
    CPUState *cs = qemu_get_cpu(vcpu_id);

    if (!cs) {
        return -ENOENT;
    }

    /* cpu.h doesn't include the actual Xen header. */
    qemu_build_assert(NR_VIRQS == XEN_NR_VIRQS);

    if (virq >= NR_VIRQS) {
        return -EINVAL;
    }

    if (port && X86_CPU(cs)->env.xen_virq[virq]) {
        return -EEXIST;
    }

    X86_CPU(cs)->env.xen_virq[virq] = port;
    if (virq == VIRQ_TIMER && kvm_xen_has_cap(EVTCHN_SEND)) {
        async_run_on_cpu(cs, do_set_vcpu_timer_virq,
                         RUN_ON_CPU_HOST_INT(port));
    }
    return 0;
}

static void do_set_vcpu_time_info_gpa(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_time_info_gpa = data.host_ulong;

    kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO,
                          env->xen_vcpu_time_info_gpa);
}

static void do_set_vcpu_runstate_gpa(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_runstate_gpa = data.host_ulong;

    kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR,
                          env->xen_vcpu_runstate_gpa);
}

static void do_vcpu_soft_reset(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_info_gpa = INVALID_GPA;
    env->xen_vcpu_info_default_gpa = INVALID_GPA;
    env->xen_vcpu_time_info_gpa = INVALID_GPA;
    env->xen_vcpu_runstate_gpa = INVALID_GPA;
    env->xen_vcpu_callback_vector = 0;
    env->xen_singleshot_timer_ns = 0;
    memset(env->xen_virq, 0, sizeof(env->xen_virq));

    set_vcpu_info(cs, INVALID_GPA);
    kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO,
                          INVALID_GPA);
    kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR,
                          INVALID_GPA);
    if (kvm_xen_has_cap(EVTCHN_SEND)) {
        kvm_xen_set_vcpu_callback_vector(cs);
        kvm_xen_set_vcpu_timer(cs);
    }

}

static int xen_set_shared_info(uint64_t gfn)
{
    uint64_t gpa = gfn << TARGET_PAGE_BITS;
    int i, err;

    QEMU_IOTHREAD_LOCK_GUARD();

    /*
     * The xen_overlay device tells KVM about it too, since it had to
     * do that on migration load anyway (unless we're going to jump
     * through lots of hoops to maintain the fiction that this isn't
     * KVM-specific.
     */
    err = xen_overlay_map_shinfo_page(gpa);
    if (err) {
            return err;
    }

    trace_kvm_xen_set_shared_info(gfn);

    for (i = 0; i < XEN_LEGACY_MAX_VCPUS; i++) {
        CPUState *cpu = qemu_get_cpu(i);
        if (cpu) {
            async_run_on_cpu(cpu, do_set_vcpu_info_default_gpa,
                             RUN_ON_CPU_HOST_ULONG(gpa));
        }
        gpa += sizeof(vcpu_info_t);
    }

    return err;
}

static int add_to_physmap_one(uint32_t space, uint64_t idx, uint64_t gfn)
{
    switch (space) {
    case XENMAPSPACE_shared_info:
        if (idx > 0) {
            return -EINVAL;
        }
        return xen_set_shared_info(gfn);

    case XENMAPSPACE_grant_table:
    case XENMAPSPACE_gmfn:
    case XENMAPSPACE_gmfn_range:
        return -ENOTSUP;

    case XENMAPSPACE_gmfn_foreign:
    case XENMAPSPACE_dev_mmio:
        return -EPERM;

    default:
        return -EINVAL;
    }
}

static int do_add_to_physmap(struct kvm_xen_exit *exit, X86CPU *cpu,
                             uint64_t arg)
{
    struct xen_add_to_physmap xatp;
    CPUState *cs = CPU(cpu);

    if (hypercall_compat32(exit->u.hcall.longmode)) {
        struct compat_xen_add_to_physmap xatp32;

        qemu_build_assert(sizeof(struct compat_xen_add_to_physmap) == 16);
        if (kvm_copy_from_gva(cs, arg, &xatp32, sizeof(xatp32))) {
            return -EFAULT;
        }
        xatp.domid = xatp32.domid;
        xatp.size = xatp32.size;
        xatp.space = xatp32.space;
        xatp.idx = xatp32.idx;
        xatp.gpfn = xatp32.gpfn;
    } else {
        if (kvm_copy_from_gva(cs, arg, &xatp, sizeof(xatp))) {
            return -EFAULT;
        }
    }

    if (xatp.domid != DOMID_SELF && xatp.domid != xen_domid) {
        return -ESRCH;
    }

    return add_to_physmap_one(xatp.space, xatp.idx, xatp.gpfn);
}

static int do_add_to_physmap_batch(struct kvm_xen_exit *exit, X86CPU *cpu,
                                   uint64_t arg)
{
    struct xen_add_to_physmap_batch xatpb;
    unsigned long idxs_gva, gpfns_gva, errs_gva;
    CPUState *cs = CPU(cpu);
    size_t op_sz;

    if (hypercall_compat32(exit->u.hcall.longmode)) {
        struct compat_xen_add_to_physmap_batch xatpb32;

        qemu_build_assert(sizeof(struct compat_xen_add_to_physmap_batch) == 20);
        if (kvm_copy_from_gva(cs, arg, &xatpb32, sizeof(xatpb32))) {
            return -EFAULT;
        }
        xatpb.domid = xatpb32.domid;
        xatpb.space = xatpb32.space;
        xatpb.size = xatpb32.size;

        idxs_gva = xatpb32.idxs.c;
        gpfns_gva = xatpb32.gpfns.c;
        errs_gva = xatpb32.errs.c;
        op_sz = sizeof(uint32_t);
    } else {
        if (kvm_copy_from_gva(cs, arg, &xatpb, sizeof(xatpb))) {
            return -EFAULT;
        }
        op_sz = sizeof(unsigned long);
        idxs_gva = (unsigned long)xatpb.idxs.p;
        gpfns_gva = (unsigned long)xatpb.gpfns.p;
        errs_gva = (unsigned long)xatpb.errs.p;
    }

    if (xatpb.domid != DOMID_SELF && xatpb.domid != xen_domid) {
        return -ESRCH;
    }

    /* Explicitly invalid for the batch op. Not that we implement it anyway. */
    if (xatpb.space == XENMAPSPACE_gmfn_range) {
        return -EINVAL;
    }

    while (xatpb.size--) {
        unsigned long idx = 0;
        unsigned long gpfn = 0;
        int err;

        /* For 32-bit compat this only copies the low 32 bits of each */
        if (kvm_copy_from_gva(cs, idxs_gva, &idx, op_sz) ||
            kvm_copy_from_gva(cs, gpfns_gva, &gpfn, op_sz)) {
            return -EFAULT;
        }
        idxs_gva += op_sz;
        gpfns_gva += op_sz;

        err = add_to_physmap_one(xatpb.space, idx, gpfn);

        if (kvm_copy_to_gva(cs, errs_gva, &err, sizeof(err))) {
            return -EFAULT;
        }
        errs_gva += sizeof(err);
    }
    return 0;
}

static bool kvm_xen_hcall_memory_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                   int cmd, uint64_t arg)
{
    int err;

    switch (cmd) {
    case XENMEM_add_to_physmap:
        err = do_add_to_physmap(exit, cpu, arg);
        break;

    case XENMEM_add_to_physmap_batch:
        err = do_add_to_physmap_batch(exit, cpu, arg);
        break;

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

static bool handle_set_param(struct kvm_xen_exit *exit, X86CPU *cpu,
                             uint64_t arg)
{
    CPUState *cs = CPU(cpu);
    struct xen_hvm_param hp;
    int err = 0;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(hp) == 16);

    if (kvm_copy_from_gva(cs, arg, &hp, sizeof(hp))) {
        err = -EFAULT;
        goto out;
    }

    if (hp.domid != DOMID_SELF && hp.domid != xen_domid) {
        err = -ESRCH;
        goto out;
    }

    switch (hp.index) {
    case HVM_PARAM_CALLBACK_IRQ:
        err = xen_evtchn_set_callback_param(hp.value);
        xen_set_long_mode(exit->u.hcall.longmode);
        break;
    default:
        return false;
    }

out:
    exit->u.hcall.result = err;
    return true;
}

static int kvm_xen_hcall_evtchn_upcall_vector(struct kvm_xen_exit *exit,
                                              X86CPU *cpu, uint64_t arg)
{
    struct xen_hvm_evtchn_upcall_vector up;
    CPUState *target_cs;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(up) == 8);

    if (kvm_copy_from_gva(CPU(cpu), arg, &up, sizeof(up))) {
        return -EFAULT;
    }

    if (up.vector < 0x10) {
        return -EINVAL;
    }

    target_cs = qemu_get_cpu(up.vcpu);
    if (!target_cs) {
        return -EINVAL;
    }

    async_run_on_cpu(target_cs, do_set_vcpu_callback_vector,
                     RUN_ON_CPU_HOST_INT(up.vector));
    return 0;
}

static bool kvm_xen_hcall_hvm_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                 int cmd, uint64_t arg)
{
    int ret = -ENOSYS;
    switch (cmd) {
    case HVMOP_set_evtchn_upcall_vector:
        ret = kvm_xen_hcall_evtchn_upcall_vector(exit, cpu,
                                                 exit->u.hcall.params[0]);
        break;

    case HVMOP_pagetable_dying:
        ret = -ENOSYS;
        break;

    case HVMOP_set_param:
        return handle_set_param(exit, cpu, arg);

    default:
        return false;
    }

    exit->u.hcall.result = ret;
    return true;
}

static int vcpuop_register_vcpu_info(CPUState *cs, CPUState *target,
                                     uint64_t arg)
{
    struct vcpu_register_vcpu_info rvi;
    uint64_t gpa;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(rvi) == 16);
    qemu_build_assert(sizeof(struct vcpu_info) == 64);

    if (!target) {
        return -ENOENT;
    }

    if (kvm_copy_from_gva(cs, arg, &rvi, sizeof(rvi))) {
        return -EFAULT;
    }

    if (rvi.offset > TARGET_PAGE_SIZE - sizeof(struct vcpu_info)) {
        return -EINVAL;
    }

    gpa = ((rvi.mfn << TARGET_PAGE_BITS) + rvi.offset);
    async_run_on_cpu(target, do_set_vcpu_info_gpa, RUN_ON_CPU_HOST_ULONG(gpa));
    return 0;
}

static int vcpuop_register_vcpu_time_info(CPUState *cs, CPUState *target,
                                          uint64_t arg)
{
    struct vcpu_register_time_memory_area tma;
    uint64_t gpa;
    size_t len;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(tma) == 8);
    qemu_build_assert(sizeof(struct vcpu_time_info) == 32);

    if (!target) {
        return -ENOENT;
    }

    if (kvm_copy_from_gva(cs, arg, &tma, sizeof(tma))) {
        return -EFAULT;
    }

    /*
     * Xen actually uses the GVA and does the translation through the guest
     * page tables each time. But Linux/KVM uses the GPA, on the assumption
     * that guests only ever use *global* addresses (kernel virtual addresses)
     * for it. If Linux is changed to redo the GVA→GPA translation each time,
     * it will offer a new vCPU attribute for that, and we'll use it instead.
     */
    if (!kvm_gva_to_gpa(cs, tma.addr.p, &gpa, &len, false) ||
        len < sizeof(struct vcpu_time_info)) {
        return -EFAULT;
    }

    async_run_on_cpu(target, do_set_vcpu_time_info_gpa,
                     RUN_ON_CPU_HOST_ULONG(gpa));
    return 0;
}

static int vcpuop_register_runstate_info(CPUState *cs, CPUState *target,
                                         uint64_t arg)
{
    struct vcpu_register_runstate_memory_area rma;
    uint64_t gpa;
    size_t len;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(rma) == 8);
    /* The runstate area actually does change size, but Linux copes. */

    if (!target) {
        return -ENOENT;
    }

    if (kvm_copy_from_gva(cs, arg, &rma, sizeof(rma))) {
        return -EFAULT;
    }

    /* As with vcpu_time_info, Xen actually uses the GVA but KVM doesn't. */
    if (!kvm_gva_to_gpa(cs, rma.addr.p, &gpa, &len, false)) {
        return -EFAULT;
    }

    async_run_on_cpu(target, do_set_vcpu_runstate_gpa,
                     RUN_ON_CPU_HOST_ULONG(gpa));
    return 0;
}

static bool kvm_xen_hcall_vcpu_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                  int cmd, int vcpu_id, uint64_t arg)
{
    CPUState *dest = qemu_get_cpu(vcpu_id);
    CPUState *cs = CPU(cpu);
    int err;

    switch (cmd) {
    case VCPUOP_register_runstate_memory_area:
        err = vcpuop_register_runstate_info(cs, dest, arg);
        break;
    case VCPUOP_register_vcpu_time_memory_area:
        err = vcpuop_register_vcpu_time_info(cs, dest, arg);
        break;
    case VCPUOP_register_vcpu_info:
        err = vcpuop_register_vcpu_info(cs, dest, arg);
        break;

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

static bool kvm_xen_hcall_evtchn_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                    int cmd, uint64_t arg)
{
    CPUState *cs = CPU(cpu);
    int err = -ENOSYS;

    switch (cmd) {
    case EVTCHNOP_init_control:
    case EVTCHNOP_expand_array:
    case EVTCHNOP_set_priority:
        /* We do not support FIFO channels at this point */
        err = -ENOSYS;
        break;

    case EVTCHNOP_status: {
        struct evtchn_status status;

        qemu_build_assert(sizeof(status) == 24);
        if (kvm_copy_from_gva(cs, arg, &status, sizeof(status))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_status_op(&status);
        if (!err && kvm_copy_to_gva(cs, arg, &status, sizeof(status))) {
            err = -EFAULT;
        }
        break;
    }
    case EVTCHNOP_close: {
        struct evtchn_close close;

        qemu_build_assert(sizeof(close) == 4);
        if (kvm_copy_from_gva(cs, arg, &close, sizeof(close))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_close_op(&close);
        break;
    }
    case EVTCHNOP_unmask: {
        struct evtchn_unmask unmask;

        qemu_build_assert(sizeof(unmask) == 4);
        if (kvm_copy_from_gva(cs, arg, &unmask, sizeof(unmask))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_unmask_op(&unmask);
        break;
    }
    case EVTCHNOP_bind_virq: {
        struct evtchn_bind_virq virq;

        qemu_build_assert(sizeof(virq) == 12);
        if (kvm_copy_from_gva(cs, arg, &virq, sizeof(virq))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_bind_virq_op(&virq);
        if (!err && kvm_copy_to_gva(cs, arg, &virq, sizeof(virq))) {
            err = -EFAULT;
        }
        break;
    }
    case EVTCHNOP_bind_ipi: {
        struct evtchn_bind_ipi ipi;

        qemu_build_assert(sizeof(ipi) == 8);
        if (kvm_copy_from_gva(cs, arg, &ipi, sizeof(ipi))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_bind_ipi_op(&ipi);
        if (!err && kvm_copy_to_gva(cs, arg, &ipi, sizeof(ipi))) {
            err = -EFAULT;
        }
        break;
    }
    case EVTCHNOP_send: {
        struct evtchn_send send;

        qemu_build_assert(sizeof(send) == 4);
        if (kvm_copy_from_gva(cs, arg, &send, sizeof(send))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_send_op(&send);
        break;
    }
    case EVTCHNOP_alloc_unbound: {
        struct evtchn_alloc_unbound alloc;

        qemu_build_assert(sizeof(alloc) == 8);
        if (kvm_copy_from_gva(cs, arg, &alloc, sizeof(alloc))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_alloc_unbound_op(&alloc);
        if (!err && kvm_copy_to_gva(cs, arg, &alloc, sizeof(alloc))) {
            err = -EFAULT;
        }
        break;
    }
    case EVTCHNOP_bind_interdomain: {
        struct evtchn_bind_interdomain interdomain;

        qemu_build_assert(sizeof(interdomain) == 12);
        if (kvm_copy_from_gva(cs, arg, &interdomain, sizeof(interdomain))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_bind_interdomain_op(&interdomain);
        if (!err &&
            kvm_copy_to_gva(cs, arg, &interdomain, sizeof(interdomain))) {
            err = -EFAULT;
        }
        break;
    }
    case EVTCHNOP_bind_vcpu: {
        struct evtchn_bind_vcpu vcpu;

        qemu_build_assert(sizeof(vcpu) == 8);
        if (kvm_copy_from_gva(cs, arg, &vcpu, sizeof(vcpu))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_bind_vcpu_op(&vcpu);
        break;
    }
    case EVTCHNOP_reset: {
        struct evtchn_reset reset;

        qemu_build_assert(sizeof(reset) == 2);
        if (kvm_copy_from_gva(cs, arg, &reset, sizeof(reset))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_reset_op(&reset);
        break;
    }
    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

int kvm_xen_soft_reset(void)
{
    CPUState *cpu;
    int err;

    assert(qemu_mutex_iothread_locked());

    trace_kvm_xen_soft_reset();

    err = xen_evtchn_soft_reset();
    if (err) {
        return err;
    }

    /*
     * Zero is the reset/startup state for HVM_PARAM_CALLBACK_IRQ. Strictly,
     * it maps to HVM_PARAM_CALLBACK_TYPE_GSI with GSI#0, but Xen refuses to
     * to deliver to the timer interrupt and treats that as 'disabled'.
     */
    err = xen_evtchn_set_callback_param(0);
    if (err) {
        return err;
    }

    CPU_FOREACH(cpu) {
        async_run_on_cpu(cpu, do_vcpu_soft_reset, RUN_ON_CPU_NULL);
    }

    err = xen_overlay_map_shinfo_page(INVALID_GFN);
    if (err) {
        return err;
    }

    return 0;
}

static int schedop_shutdown(CPUState *cs, uint64_t arg)
{
    struct sched_shutdown shutdown;
    int ret = 0;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(shutdown) == 4);

    if (kvm_copy_from_gva(cs, arg, &shutdown, sizeof(shutdown))) {
        return -EFAULT;
    }

    switch (shutdown.reason) {
    case SHUTDOWN_crash:
        cpu_dump_state(cs, stderr, CPU_DUMP_CODE);
        qemu_system_guest_panicked(NULL);
        break;

    case SHUTDOWN_reboot:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        break;

    case SHUTDOWN_poweroff:
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        break;

    case SHUTDOWN_soft_reset:
        qemu_mutex_lock_iothread();
        ret = kvm_xen_soft_reset();
        qemu_mutex_unlock_iothread();
        break;

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

static bool kvm_xen_hcall_sched_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                   int cmd, uint64_t arg)
{
    CPUState *cs = CPU(cpu);
    int err = -ENOSYS;

    switch (cmd) {
    case SCHEDOP_shutdown:
        err = schedop_shutdown(cs, arg);
        break;

    case SCHEDOP_poll:
        /*
         * Linux will panic if this doesn't work. Just yield; it's not
         * worth overthinking it because with event channel handling
         * in KVM, the kernel will intercept this and it will never
         * reach QEMU anyway. The semantics of the hypercall explicltly
         * permit spurious wakeups.
         */
    case SCHEDOP_yield:
        sched_yield();
        err = 0;
        break;

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

static bool do_kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit)
{
    uint16_t code = exit->u.hcall.input;

    if (exit->u.hcall.cpl > 0) {
        exit->u.hcall.result = -EPERM;
        return true;
    }

    switch (code) {
    case __HYPERVISOR_sched_op:
        return kvm_xen_hcall_sched_op(exit, cpu, exit->u.hcall.params[0],
                                      exit->u.hcall.params[1]);
    case __HYPERVISOR_event_channel_op:
        return kvm_xen_hcall_evtchn_op(exit, cpu, exit->u.hcall.params[0],
                                       exit->u.hcall.params[1]);
    case __HYPERVISOR_vcpu_op:
        return kvm_xen_hcall_vcpu_op(exit, cpu,
                                     exit->u.hcall.params[0],
                                     exit->u.hcall.params[1],
                                     exit->u.hcall.params[2]);
    case __HYPERVISOR_hvm_op:
        return kvm_xen_hcall_hvm_op(exit, cpu, exit->u.hcall.params[0],
                                    exit->u.hcall.params[1]);
    case __HYPERVISOR_memory_op:
        return kvm_xen_hcall_memory_op(exit, cpu, exit->u.hcall.params[0],
                                       exit->u.hcall.params[1]);
    case __HYPERVISOR_xen_version:
        return kvm_xen_hcall_xen_version(exit, cpu, exit->u.hcall.params[0],
                                         exit->u.hcall.params[1]);
    default:
        return false;
    }
}

int kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit)
{
    if (exit->type != KVM_EXIT_XEN_HCALL) {
        return -1;
    }

    /*
     * The kernel latches the guest 32/64 mode when the MSR is used to fill
     * the hypercall page. So if we see a hypercall in a mode that doesn't
     * match our own idea of the guest mode, fetch the kernel's idea of the
     * "long mode" to remain in sync.
     */
    if (exit->u.hcall.longmode != xen_is_long_mode()) {
        xen_sync_long_mode();
    }

    if (!do_kvm_xen_handle_exit(cpu, exit)) {
        /*
         * Some hypercalls will be deliberately "implemented" by returning
         * -ENOSYS. This case is for hypercalls which are unexpected.
         */
        exit->u.hcall.result = -ENOSYS;
        qemu_log_mask(LOG_UNIMP, "Unimplemented Xen hypercall %"
                      PRId64 " (0x%" PRIx64 " 0x%" PRIx64 " 0x%" PRIx64 ")\n",
                      (uint64_t)exit->u.hcall.input,
                      (uint64_t)exit->u.hcall.params[0],
                      (uint64_t)exit->u.hcall.params[1],
                      (uint64_t)exit->u.hcall.params[2]);
    }

    trace_kvm_xen_hypercall(CPU(cpu)->cpu_index, exit->u.hcall.cpl,
                            exit->u.hcall.input, exit->u.hcall.params[0],
                            exit->u.hcall.params[1], exit->u.hcall.params[2],
                            exit->u.hcall.result);
    return 0;
}

int kvm_put_xen_state(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint64_t gpa;
    int ret;

    gpa = env->xen_vcpu_info_gpa;
    if (gpa == INVALID_GPA) {
        gpa = env->xen_vcpu_info_default_gpa;
    }

    if (gpa != INVALID_GPA) {
        ret = set_vcpu_info(cs, gpa);
        if (ret < 0) {
            return ret;
        }
    }

    gpa = env->xen_vcpu_time_info_gpa;
    if (gpa != INVALID_GPA) {
        ret = kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO,
                                    gpa);
        if (ret < 0) {
            return ret;
        }
    }

    gpa = env->xen_vcpu_runstate_gpa;
    if (gpa != INVALID_GPA) {
        ret = kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR,
                                    gpa);
        if (ret < 0) {
            return ret;
        }
    }

    if (!kvm_xen_has_cap(EVTCHN_SEND)) {
        return 0;
    }

    if (env->xen_vcpu_callback_vector) {
        ret = kvm_xen_set_vcpu_callback_vector(cs);
        if (ret < 0) {
            return ret;
        }
    }

    if (env->xen_virq[VIRQ_TIMER]) {
        ret = kvm_xen_set_vcpu_timer(cs);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

int kvm_get_xen_state(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint64_t gpa;
    int ret;

    /*
     * The kernel does not mark vcpu_info as dirty when it delivers interrupts
     * to it. It's up to userspace to *assume* that any page shared thus is
     * always considered dirty. The shared_info page is different since it's
     * an overlay and migrated separately anyway.
     */
    gpa = env->xen_vcpu_info_gpa;
    if (gpa == INVALID_GPA) {
        gpa = env->xen_vcpu_info_default_gpa;
    }
    if (gpa != INVALID_GPA) {
        MemoryRegionSection mrs = memory_region_find(get_system_memory(),
                                                     gpa,
                                                     sizeof(struct vcpu_info));
        if (mrs.mr &&
            !int128_lt(mrs.size, int128_make64(sizeof(struct vcpu_info)))) {
            memory_region_set_dirty(mrs.mr, mrs.offset_within_region,
                                    sizeof(struct vcpu_info));
        }
    }

    if (!kvm_xen_has_cap(EVTCHN_SEND)) {
        return 0;
    }

    /*
     * If the kernel is accelerating timers, read out the current value of the
     * singleshot timer deadline.
     */
    if (env->xen_virq[VIRQ_TIMER]) {
        struct kvm_xen_vcpu_attr va = {
            .type = KVM_XEN_VCPU_ATTR_TYPE_TIMER,
        };
        ret = kvm_vcpu_ioctl(cs, KVM_XEN_VCPU_GET_ATTR, &va);
        if (ret < 0) {
            return ret;
        }
        env->xen_singleshot_timer_ns = va.u.timer.expires_ns;
    }

    return 0;
}
