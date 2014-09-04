/*
 * ARM implementation of KVM hooks
 *
 * Copyright Christoffer Dall 2009-2010
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "cpu.h"
#include "hw/arm/arm.h"

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

int kvm_arm_vcpu_init(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    struct kvm_vcpu_init init;

    init.target = cpu->kvm_target;
    memcpy(init.features, cpu->kvm_init_features, sizeof(init.features));

    return kvm_vcpu_ioctl(cs, KVM_ARM_VCPU_INIT, &init);
}

bool kvm_arm_create_scratch_host_vcpu(const uint32_t *cpus_to_try,
                                      int *fdarray,
                                      struct kvm_vcpu_init *init)
{
    int ret, kvmfd = -1, vmfd = -1, cpufd = -1;

    kvmfd = qemu_open("/dev/kvm", O_RDWR);
    if (kvmfd < 0) {
        goto err;
    }
    vmfd = ioctl(kvmfd, KVM_CREATE_VM, 0);
    if (vmfd < 0) {
        goto err;
    }
    cpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
    if (cpufd < 0) {
        goto err;
    }

    ret = ioctl(vmfd, KVM_ARM_PREFERRED_TARGET, init);
    if (ret >= 0) {
        ret = ioctl(cpufd, KVM_ARM_VCPU_INIT, init);
        if (ret < 0) {
            goto err;
        }
    } else {
        /* Old kernel which doesn't know about the
         * PREFERRED_TARGET ioctl: we know it will only support
         * creating one kind of guest CPU which is its preferred
         * CPU type.
         */
        while (*cpus_to_try != QEMU_KVM_ARM_TARGET_NONE) {
            init->target = *cpus_to_try++;
            memset(init->features, 0, sizeof(init->features));
            ret = ioctl(cpufd, KVM_ARM_VCPU_INIT, init);
            if (ret >= 0) {
                break;
            }
        }
        if (ret < 0) {
            goto err;
        }
    }

    fdarray[0] = kvmfd;
    fdarray[1] = vmfd;
    fdarray[2] = cpufd;

    return true;

err:
    if (cpufd >= 0) {
        close(cpufd);
    }
    if (vmfd >= 0) {
        close(vmfd);
    }
    if (kvmfd >= 0) {
        close(kvmfd);
    }

    return false;
}

void kvm_arm_destroy_scratch_host_vcpu(int *fdarray)
{
    int i;

    for (i = 2; i >= 0; i--) {
        close(fdarray[i]);
    }
}

static void kvm_arm_host_cpu_class_init(ObjectClass *oc, void *data)
{
    ARMHostCPUClass *ahcc = ARM_HOST_CPU_CLASS(oc);

    /* All we really need to set up for the 'host' CPU
     * is the feature bits -- we rely on the fact that the
     * various ID register values in ARMCPU are only used for
     * TCG CPUs.
     */
    if (!kvm_arm_get_host_cpu_features(ahcc)) {
        fprintf(stderr, "Failed to retrieve host CPU features!\n");
        abort();
    }
}

static void kvm_arm_host_cpu_initfn(Object *obj)
{
    ARMHostCPUClass *ahcc = ARM_HOST_CPU_GET_CLASS(obj);
    ARMCPU *cpu = ARM_CPU(obj);
    CPUARMState *env = &cpu->env;

    cpu->kvm_target = ahcc->target;
    cpu->dtb_compatible = ahcc->dtb_compatible;
    env->features = ahcc->features;
}

static const TypeInfo host_arm_cpu_type_info = {
    .name = TYPE_ARM_HOST_CPU,
#ifdef TARGET_AARCH64
    .parent = TYPE_AARCH64_CPU,
#else
    .parent = TYPE_ARM_CPU,
#endif
    .instance_init = kvm_arm_host_cpu_initfn,
    .class_init = kvm_arm_host_cpu_class_init,
    .class_size = sizeof(ARMHostCPUClass),
};

int kvm_arch_init(KVMState *s)
{
    /* For ARM interrupt delivery is always asynchronous,
     * whether we are using an in-kernel VGIC or not.
     */
    kvm_async_interrupts_allowed = true;

    type_register_static(&host_arm_cpu_type_info);

    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

/* We track all the KVM devices which need their memory addresses
 * passing to the kernel in a list of these structures.
 * When board init is complete we run through the list and
 * tell the kernel the base addresses of the memory regions.
 * We use a MemoryListener to track mapping and unmapping of
 * the regions during board creation, so the board models don't
 * need to do anything special for the KVM case.
 */
typedef struct KVMDevice {
    struct kvm_arm_device_addr kda;
    struct kvm_device_attr kdattr;
    MemoryRegion *mr;
    QSLIST_ENTRY(KVMDevice) entries;
    int dev_fd;
} KVMDevice;

static QSLIST_HEAD(kvm_devices_head, KVMDevice) kvm_devices_head;

static void kvm_arm_devlistener_add(MemoryListener *listener,
                                    MemoryRegionSection *section)
{
    KVMDevice *kd;

    QSLIST_FOREACH(kd, &kvm_devices_head, entries) {
        if (section->mr == kd->mr) {
            kd->kda.addr = section->offset_within_address_space;
        }
    }
}

static void kvm_arm_devlistener_del(MemoryListener *listener,
                                    MemoryRegionSection *section)
{
    KVMDevice *kd;

    QSLIST_FOREACH(kd, &kvm_devices_head, entries) {
        if (section->mr == kd->mr) {
            kd->kda.addr = -1;
        }
    }
}

static MemoryListener devlistener = {
    .region_add = kvm_arm_devlistener_add,
    .region_del = kvm_arm_devlistener_del,
};

static void kvm_arm_set_device_addr(KVMDevice *kd)
{
    struct kvm_device_attr *attr = &kd->kdattr;
    int ret;

    /* If the device control API is available and we have a device fd on the
     * KVMDevice struct, let's use the newer API
     */
    if (kd->dev_fd >= 0) {
        uint64_t addr = kd->kda.addr;
        attr->addr = (uintptr_t)&addr;
        ret = kvm_device_ioctl(kd->dev_fd, KVM_SET_DEVICE_ATTR, attr);
    } else {
        ret = kvm_vm_ioctl(kvm_state, KVM_ARM_SET_DEVICE_ADDR, &kd->kda);
    }

    if (ret < 0) {
        fprintf(stderr, "Failed to set device address: %s\n",
                strerror(-ret));
        abort();
    }
}

static void kvm_arm_machine_init_done(Notifier *notifier, void *data)
{
    KVMDevice *kd, *tkd;

    memory_listener_unregister(&devlistener);
    QSLIST_FOREACH_SAFE(kd, &kvm_devices_head, entries, tkd) {
        if (kd->kda.addr != -1) {
            kvm_arm_set_device_addr(kd);
        }
        memory_region_unref(kd->mr);
        g_free(kd);
    }
}

static Notifier notify = {
    .notify = kvm_arm_machine_init_done,
};

void kvm_arm_register_device(MemoryRegion *mr, uint64_t devid, uint64_t group,
                             uint64_t attr, int dev_fd)
{
    KVMDevice *kd;

    if (!kvm_irqchip_in_kernel()) {
        return;
    }

    if (QSLIST_EMPTY(&kvm_devices_head)) {
        memory_listener_register(&devlistener, NULL);
        qemu_add_machine_init_done_notifier(&notify);
    }
    kd = g_new0(KVMDevice, 1);
    kd->mr = mr;
    kd->kda.id = devid;
    kd->kda.addr = -1;
    kd->kdattr.flags = 0;
    kd->kdattr.group = group;
    kd->kdattr.attr = attr;
    kd->dev_fd = dev_fd;
    QSLIST_INSERT_HEAD(&kvm_devices_head, kd, entries);
    memory_region_ref(kd->mr);
}

bool write_kvmstate_to_list(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        struct kvm_one_reg r;
        uint64_t regidx = cpu->cpreg_indexes[i];
        uint32_t v32;
        int ret;

        r.id = regidx;

        switch (regidx & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
            r.addr = (uintptr_t)&v32;
            ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
            if (!ret) {
                cpu->cpreg_values[i] = v32;
            }
            break;
        case KVM_REG_SIZE_U64:
            r.addr = (uintptr_t)(cpu->cpreg_values + i);
            ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
            break;
        default:
            abort();
        }
        if (ret) {
            ok = false;
        }
    }
    return ok;
}

bool write_list_to_kvmstate(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        struct kvm_one_reg r;
        uint64_t regidx = cpu->cpreg_indexes[i];
        uint32_t v32;
        int ret;

        r.id = regidx;
        switch (regidx & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
            v32 = cpu->cpreg_values[i];
            r.addr = (uintptr_t)&v32;
            break;
        case KVM_REG_SIZE_U64:
            r.addr = (uintptr_t)(cpu->cpreg_values + i);
            break;
        default:
            abort();
        }
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &r);
        if (ret) {
            /* We might fail for "unknown register" and also for
             * "you tried to set a register which is constant with
             * a different value from what it actually contains".
             */
            ok = false;
        }
    }
    return ok;
}

void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
{
}

void kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    return 0;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    return true;
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return 0;
}

int kvm_arch_on_sigbus_vcpu(CPUState *cs, int code, void *addr)
{
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    return 1;
}

void kvm_arch_update_guest_debug(CPUState *cs, struct kvm_guest_debug *dbg)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs,
                                  struct kvm_sw_breakpoint *bp)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
    return -EINVAL;
}

int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
    return -EINVAL;
}

int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
    return -EINVAL;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs,
                                  struct kvm_sw_breakpoint *bp)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
    return -EINVAL;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}

int kvm_arch_irqchip_create(KVMState *s)
{
    int ret;

    /* If we can create the VGIC using the newer device control API, we
     * let the device do this when it initializes itself, otherwise we
     * fall back to the old API */

    ret = kvm_create_device(s, KVM_DEV_TYPE_ARM_VGIC_V2, true);
    if (ret == 0) {
        return 1;
    }

    return 0;
}
