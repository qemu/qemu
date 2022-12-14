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
#include "sysemu/kvm_int.h"
#include "sysemu/kvm_xen.h"
#include "kvm/kvm_i386.h"
#include "exec/address-spaces.h"
#include "xen-emu.h"
#include "trace.h"
#include "sysemu/runstate.h"

#include "hw/xen/interface/version.h"
#include "hw/xen/interface/sched.h"

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

int kvm_xen_soft_reset(void)
{
    assert(qemu_mutex_iothread_locked());

    trace_kvm_xen_soft_reset();

    /* Nothing to reset... yet. */
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
