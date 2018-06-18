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

#include "hw/i386/kvm/xen_overlay.h"

#include "hw/xen/interface/version.h"
#include "hw/xen/interface/sched.h"
#include "hw/xen/interface/memory.h"
#include "hw/xen/interface/hvm/hvm_op.h"
#include "hw/xen/interface/vcpu.h"

#include "xen-compat.h"

#ifdef TARGET_X86_64
#define hypercall_compat32(longmode) (!(longmode))
#else
#define hypercall_compat32(longmode) (false)
#endif

static int kvm_gva_rw(CPUState *cs, uint64_t gva, void *_buf, size_t sz,
                      bool is_write)
{
    uint8_t *buf = (uint8_t *)_buf;
    int ret;

    while (sz) {
        struct kvm_translation tr = {
            .linear_address = gva,
        };

        size_t len = TARGET_PAGE_SIZE - (tr.linear_address & ~TARGET_PAGE_MASK);
        if (len > sz) {
            len = sz;
        }

        ret = kvm_vcpu_ioctl(cs, KVM_TRANSLATE, &tr);
        if (ret || !tr.valid || (is_write && !tr.writeable)) {
            return -EFAULT;
        }

        cpu_physical_memory_rw(tr.physical_address, buf, len, is_write);

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
                         1 << XENFEAT_supervisor_mode_kernel;
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

static int xen_set_shared_info(uint64_t gfn)
{
    uint64_t gpa = gfn << TARGET_PAGE_BITS;
    int err;

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

static bool kvm_xen_hcall_hvm_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                 int cmd, uint64_t arg)
{
    switch (cmd) {
    case HVMOP_pagetable_dying:
        exit->u.hcall.result = -ENOSYS;
        return true;

    default:
        return false;
    }
}

static bool kvm_xen_hcall_vcpu_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                  int cmd, int vcpu_id, uint64_t arg)
{
    int err;

    switch (cmd) {
    case VCPUOP_register_vcpu_info:
        /* no vcpu info placement for now */
        err = -ENOSYS;
        break;

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

int kvm_xen_soft_reset(void)
{
    int err;

    assert(qemu_mutex_iothread_locked());

    trace_kvm_xen_soft_reset();

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
