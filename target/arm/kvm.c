/*
 * ARM implementation of KVM hooks
 *
 * Copyright Christoffer Dall 2009-2010
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "kvm_arm.h"
#include "cpu.h"
#include "trace.h"
#include "internals.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qemu/log.h"

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static bool cap_has_mp_state;
static bool cap_has_inject_serror_esr;
static bool cap_has_inject_ext_dabt;

static ARMHostCPUFeatures arm_host_cpu_features;

int kvm_arm_vcpu_init(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    struct kvm_vcpu_init init;

    init.target = cpu->kvm_target;
    memcpy(init.features, cpu->kvm_init_features, sizeof(init.features));

    return kvm_vcpu_ioctl(cs, KVM_ARM_VCPU_INIT, &init);
}

int kvm_arm_vcpu_finalize(CPUState *cs, int feature)
{
    return kvm_vcpu_ioctl(cs, KVM_ARM_VCPU_FINALIZE, &feature);
}

void kvm_arm_init_serror_injection(CPUState *cs)
{
    cap_has_inject_serror_esr = kvm_check_extension(cs->kvm_state,
                                    KVM_CAP_ARM_INJECT_SERROR_ESR);
}

bool kvm_arm_create_scratch_host_vcpu(const uint32_t *cpus_to_try,
                                      int *fdarray,
                                      struct kvm_vcpu_init *init)
{
    int ret = 0, kvmfd = -1, vmfd = -1, cpufd = -1;
    int max_vm_pa_size;

    kvmfd = qemu_open_old("/dev/kvm", O_RDWR);
    if (kvmfd < 0) {
        goto err;
    }
    max_vm_pa_size = ioctl(kvmfd, KVM_CHECK_EXTENSION, KVM_CAP_ARM_VM_IPA_SIZE);
    if (max_vm_pa_size < 0) {
        max_vm_pa_size = 0;
    }
    vmfd = ioctl(kvmfd, KVM_CREATE_VM, max_vm_pa_size);
    if (vmfd < 0) {
        goto err;
    }
    cpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
    if (cpufd < 0) {
        goto err;
    }

    if (!init) {
        /* Caller doesn't want the VCPU to be initialized, so skip it */
        goto finish;
    }

    if (init->target == -1) {
        struct kvm_vcpu_init preferred;

        ret = ioctl(vmfd, KVM_ARM_PREFERRED_TARGET, &preferred);
        if (!ret) {
            init->target = preferred.target;
        }
    }
    if (ret >= 0) {
        ret = ioctl(cpufd, KVM_ARM_VCPU_INIT, init);
        if (ret < 0) {
            goto err;
        }
    } else if (cpus_to_try) {
        /* Old kernel which doesn't know about the
         * PREFERRED_TARGET ioctl: we know it will only support
         * creating one kind of guest CPU which is its preferred
         * CPU type.
         */
        struct kvm_vcpu_init try;

        while (*cpus_to_try != QEMU_KVM_ARM_TARGET_NONE) {
            try.target = *cpus_to_try++;
            memcpy(try.features, init->features, sizeof(init->features));
            ret = ioctl(cpufd, KVM_ARM_VCPU_INIT, &try);
            if (ret >= 0) {
                break;
            }
        }
        if (ret < 0) {
            goto err;
        }
        init->target = try.target;
    } else {
        /* Treat a NULL cpus_to_try argument the same as an empty
         * list, which means we will fail the call since this must
         * be an old kernel which doesn't support PREFERRED_TARGET.
         */
        goto err;
    }

finish:
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

void kvm_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;

    if (!arm_host_cpu_features.dtb_compatible) {
        if (!kvm_enabled() ||
            !kvm_arm_get_host_cpu_features(&arm_host_cpu_features)) {
            /* We can't report this error yet, so flag that we need to
             * in arm_cpu_realizefn().
             */
            cpu->kvm_target = QEMU_KVM_ARM_TARGET_NONE;
            cpu->host_cpu_probe_failed = true;
            return;
        }
    }

    cpu->kvm_target = arm_host_cpu_features.target;
    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar = arm_host_cpu_features.isar;
    env->features = arm_host_cpu_features.features;
}

static bool kvm_no_adjvtime_get(Object *obj, Error **errp)
{
    return !ARM_CPU(obj)->kvm_adjvtime;
}

static void kvm_no_adjvtime_set(Object *obj, bool value, Error **errp)
{
    ARM_CPU(obj)->kvm_adjvtime = !value;
}

static bool kvm_steal_time_get(Object *obj, Error **errp)
{
    return ARM_CPU(obj)->kvm_steal_time != ON_OFF_AUTO_OFF;
}

static void kvm_steal_time_set(Object *obj, bool value, Error **errp)
{
    ARM_CPU(obj)->kvm_steal_time = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
}

/* KVM VCPU properties should be prefixed with "kvm-". */
void kvm_arm_add_vcpu_properties(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    CPUARMState *env = &cpu->env;

    if (arm_feature(env, ARM_FEATURE_GENERIC_TIMER)) {
        cpu->kvm_adjvtime = true;
        object_property_add_bool(obj, "kvm-no-adjvtime", kvm_no_adjvtime_get,
                                 kvm_no_adjvtime_set);
        object_property_set_description(obj, "kvm-no-adjvtime",
                                        "Set on to disable the adjustment of "
                                        "the virtual counter. VM stopped time "
                                        "will be counted.");
    }

    cpu->kvm_steal_time = ON_OFF_AUTO_AUTO;
    object_property_add_bool(obj, "kvm-steal-time", kvm_steal_time_get,
                             kvm_steal_time_set);
    object_property_set_description(obj, "kvm-steal-time",
                                    "Set off to disable KVM steal time.");
}

bool kvm_arm_pmu_supported(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_PMU_V3);
}

int kvm_arm_get_max_vm_ipa_size(MachineState *ms, bool *fixed_ipa)
{
    KVMState *s = KVM_STATE(ms->accelerator);
    int ret;

    ret = kvm_check_extension(s, KVM_CAP_ARM_VM_IPA_SIZE);
    *fixed_ipa = ret <= 0;

    return ret > 0 ? ret : 40;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    int ret = 0;
    /* For ARM interrupt delivery is always asynchronous,
     * whether we are using an in-kernel VGIC or not.
     */
    kvm_async_interrupts_allowed = true;

    /*
     * PSCI wakes up secondary cores, so we always need to
     * have vCPUs waiting in kernel space
     */
    kvm_halt_in_kernel_allowed = true;

    cap_has_mp_state = kvm_check_extension(s, KVM_CAP_MP_STATE);

    if (ms->smp.cpus > 256 &&
        !kvm_check_extension(s, KVM_CAP_ARM_IRQ_LINE_LAYOUT_2)) {
        error_report("Using more than 256 vcpus requires a host kernel "
                     "with KVM_CAP_ARM_IRQ_LINE_LAYOUT_2");
        ret = -EINVAL;
    }

    if (kvm_check_extension(s, KVM_CAP_ARM_NISV_TO_USER)) {
        if (kvm_vm_enable_cap(s, KVM_CAP_ARM_NISV_TO_USER, 0)) {
            error_report("Failed to enable KVM_CAP_ARM_NISV_TO_USER cap");
        } else {
            /* Set status for supporting the external dabt injection */
            cap_has_inject_ext_dabt = kvm_check_extension(s,
                                    KVM_CAP_ARM_INJECT_EXT_DABT);
        }
    }

    return ret;
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
 *
 * Sometimes the address must be OR'ed with some other fields
 * (for example for KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION).
 * @kda_addr_ormask aims at storing the value of those fields.
 */
typedef struct KVMDevice {
    struct kvm_arm_device_addr kda;
    struct kvm_device_attr kdattr;
    uint64_t kda_addr_ormask;
    MemoryRegion *mr;
    QSLIST_ENTRY(KVMDevice) entries;
    int dev_fd;
} KVMDevice;

static QSLIST_HEAD(, KVMDevice) kvm_devices_head;

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
    .name = "kvm-arm",
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

        addr |= kd->kda_addr_ormask;
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

    QSLIST_FOREACH_SAFE(kd, &kvm_devices_head, entries, tkd) {
        if (kd->kda.addr != -1) {
            kvm_arm_set_device_addr(kd);
        }
        memory_region_unref(kd->mr);
        QSLIST_REMOVE_HEAD(&kvm_devices_head, entries);
        g_free(kd);
    }
    memory_listener_unregister(&devlistener);
}

static Notifier notify = {
    .notify = kvm_arm_machine_init_done,
};

void kvm_arm_register_device(MemoryRegion *mr, uint64_t devid, uint64_t group,
                             uint64_t attr, int dev_fd, uint64_t addr_ormask)
{
    KVMDevice *kd;

    if (!kvm_irqchip_in_kernel()) {
        return;
    }

    if (QSLIST_EMPTY(&kvm_devices_head)) {
        memory_listener_register(&devlistener, &address_space_memory);
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
    kd->kda_addr_ormask = addr_ormask;
    QSLIST_INSERT_HEAD(&kvm_devices_head, kd, entries);
    memory_region_ref(kd->mr);
}

static int compare_u64(const void *a, const void *b)
{
    if (*(uint64_t *)a > *(uint64_t *)b) {
        return 1;
    }
    if (*(uint64_t *)a < *(uint64_t *)b) {
        return -1;
    }
    return 0;
}

/*
 * cpreg_values are sorted in ascending order by KVM register ID
 * (see kvm_arm_init_cpreg_list). This allows us to cheaply find
 * the storage for a KVM register by ID with a binary search.
 */
static uint64_t *kvm_arm_get_cpreg_ptr(ARMCPU *cpu, uint64_t regidx)
{
    uint64_t *res;

    res = bsearch(&regidx, cpu->cpreg_indexes, cpu->cpreg_array_len,
                  sizeof(uint64_t), compare_u64);
    assert(res);

    return &cpu->cpreg_values[res - cpu->cpreg_indexes];
}

/* Initialize the ARMCPU cpreg list according to the kernel's
 * definition of what CPU registers it knows about (and throw away
 * the previous TCG-created cpreg list).
 */
int kvm_arm_init_cpreg_list(ARMCPU *cpu)
{
    struct kvm_reg_list rl;
    struct kvm_reg_list *rlp;
    int i, ret, arraylen;
    CPUState *cs = CPU(cpu);

    rl.n = 0;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_REG_LIST, &rl);
    if (ret != -E2BIG) {
        return ret;
    }
    rlp = g_malloc(sizeof(struct kvm_reg_list) + rl.n * sizeof(uint64_t));
    rlp->n = rl.n;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_REG_LIST, rlp);
    if (ret) {
        goto out;
    }
    /* Sort the list we get back from the kernel, since cpreg_tuples
     * must be in strictly ascending order.
     */
    qsort(&rlp->reg, rlp->n, sizeof(rlp->reg[0]), compare_u64);

    for (i = 0, arraylen = 0; i < rlp->n; i++) {
        if (!kvm_arm_reg_syncs_via_cpreg_list(rlp->reg[i])) {
            continue;
        }
        switch (rlp->reg[i] & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
        case KVM_REG_SIZE_U64:
            break;
        default:
            fprintf(stderr, "Can't handle size of register in kernel list\n");
            ret = -EINVAL;
            goto out;
        }

        arraylen++;
    }

    cpu->cpreg_indexes = g_renew(uint64_t, cpu->cpreg_indexes, arraylen);
    cpu->cpreg_values = g_renew(uint64_t, cpu->cpreg_values, arraylen);
    cpu->cpreg_vmstate_indexes = g_renew(uint64_t, cpu->cpreg_vmstate_indexes,
                                         arraylen);
    cpu->cpreg_vmstate_values = g_renew(uint64_t, cpu->cpreg_vmstate_values,
                                        arraylen);
    cpu->cpreg_array_len = arraylen;
    cpu->cpreg_vmstate_array_len = arraylen;

    for (i = 0, arraylen = 0; i < rlp->n; i++) {
        uint64_t regidx = rlp->reg[i];
        if (!kvm_arm_reg_syncs_via_cpreg_list(regidx)) {
            continue;
        }
        cpu->cpreg_indexes[arraylen] = regidx;
        arraylen++;
    }
    assert(cpu->cpreg_array_len == arraylen);

    if (!write_kvmstate_to_list(cpu)) {
        /* Shouldn't happen unless kernel is inconsistent about
         * what registers exist.
         */
        fprintf(stderr, "Initial read of kernel register state failed\n");
        ret = -EINVAL;
        goto out;
    }

out:
    g_free(rlp);
    return ret;
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

bool write_list_to_kvmstate(ARMCPU *cpu, int level)
{
    CPUState *cs = CPU(cpu);
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        struct kvm_one_reg r;
        uint64_t regidx = cpu->cpreg_indexes[i];
        uint32_t v32;
        int ret;

        if (kvm_arm_cpreg_level(regidx) > level) {
            continue;
        }

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

void kvm_arm_cpu_pre_save(ARMCPU *cpu)
{
    /* KVM virtual time adjustment */
    if (cpu->kvm_vtime_dirty) {
        *kvm_arm_get_cpreg_ptr(cpu, KVM_REG_ARM_TIMER_CNT) = cpu->kvm_vtime;
    }
}

void kvm_arm_cpu_post_load(ARMCPU *cpu)
{
    /* KVM virtual time adjustment */
    if (cpu->kvm_adjvtime) {
        cpu->kvm_vtime = *kvm_arm_get_cpreg_ptr(cpu, KVM_REG_ARM_TIMER_CNT);
        cpu->kvm_vtime_dirty = true;
    }
}

void kvm_arm_reset_vcpu(ARMCPU *cpu)
{
    int ret;

    /* Re-init VCPU so that all registers are set to
     * their respective reset values.
     */
    ret = kvm_arm_vcpu_init(CPU(cpu));
    if (ret < 0) {
        fprintf(stderr, "kvm_arm_vcpu_init failed: %s\n", strerror(-ret));
        abort();
    }
    if (!write_kvmstate_to_list(cpu)) {
        fprintf(stderr, "write_kvmstate_to_list failed\n");
        abort();
    }
    /*
     * Sync the reset values also into the CPUState. This is necessary
     * because the next thing we do will be a kvm_arch_put_registers()
     * which will update the list values from the CPUState before copying
     * the list values back to KVM. It's OK to ignore failure returns here
     * for the same reason we do so in kvm_arch_get_registers().
     */
    write_list_to_cpustate(cpu);
}

/*
 * Update KVM's MP_STATE based on what QEMU thinks it is
 */
int kvm_arm_sync_mpstate_to_kvm(ARMCPU *cpu)
{
    if (cap_has_mp_state) {
        struct kvm_mp_state mp_state = {
            .mp_state = (cpu->power_state == PSCI_OFF) ?
            KVM_MP_STATE_STOPPED : KVM_MP_STATE_RUNNABLE
        };
        int ret = kvm_vcpu_ioctl(CPU(cpu), KVM_SET_MP_STATE, &mp_state);
        if (ret) {
            fprintf(stderr, "%s: failed to set MP_STATE %d/%s\n",
                    __func__, ret, strerror(-ret));
            return -1;
        }
    }

    return 0;
}

/*
 * Sync the KVM MP_STATE into QEMU
 */
int kvm_arm_sync_mpstate_to_qemu(ARMCPU *cpu)
{
    if (cap_has_mp_state) {
        struct kvm_mp_state mp_state;
        int ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_MP_STATE, &mp_state);
        if (ret) {
            fprintf(stderr, "%s: failed to get MP_STATE %d/%s\n",
                    __func__, ret, strerror(-ret));
            abort();
        }
        cpu->power_state = (mp_state.mp_state == KVM_MP_STATE_STOPPED) ?
            PSCI_OFF : PSCI_ON;
    }

    return 0;
}

void kvm_arm_get_virtual_time(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    struct kvm_one_reg reg = {
        .id = KVM_REG_ARM_TIMER_CNT,
        .addr = (uintptr_t)&cpu->kvm_vtime,
    };
    int ret;

    if (cpu->kvm_vtime_dirty) {
        return;
    }

    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        error_report("Failed to get KVM_REG_ARM_TIMER_CNT");
        abort();
    }

    cpu->kvm_vtime_dirty = true;
}

void kvm_arm_put_virtual_time(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    struct kvm_one_reg reg = {
        .id = KVM_REG_ARM_TIMER_CNT,
        .addr = (uintptr_t)&cpu->kvm_vtime,
    };
    int ret;

    if (!cpu->kvm_vtime_dirty) {
        return;
    }

    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        error_report("Failed to set KVM_REG_ARM_TIMER_CNT");
        abort();
    }

    cpu->kvm_vtime_dirty = false;
}

int kvm_put_vcpu_events(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    struct kvm_vcpu_events events;
    int ret;

    if (!kvm_has_vcpu_events()) {
        return 0;
    }

    memset(&events, 0, sizeof(events));
    events.exception.serror_pending = env->serror.pending;

    /* Inject SError to guest with specified syndrome if host kernel
     * supports it, otherwise inject SError without syndrome.
     */
    if (cap_has_inject_serror_esr) {
        events.exception.serror_has_esr = env->serror.has_esr;
        events.exception.serror_esr = env->serror.esr;
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_SET_VCPU_EVENTS, &events);
    if (ret) {
        error_report("failed to put vcpu events");
    }

    return ret;
}

int kvm_get_vcpu_events(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    struct kvm_vcpu_events events;
    int ret;

    if (!kvm_has_vcpu_events()) {
        return 0;
    }

    memset(&events, 0, sizeof(events));
    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_VCPU_EVENTS, &events);
    if (ret) {
        error_report("failed to get vcpu events");
        return ret;
    }

    env->serror.pending = events.exception.serror_pending;
    env->serror.has_esr = events.exception.serror_has_esr;
    env->serror.esr = events.exception.serror_esr;

    return 0;
}

void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (unlikely(env->ext_dabt_raised)) {
        /*
         * Verifying that the ext DABT has been properly injected,
         * otherwise risking indefinitely re-running the faulting instruction
         * Covering a very narrow case for kernels 5.5..5.5.4
         * when injected abort was misconfigured to be
         * an IMPLEMENTATION DEFINED exception (for 32-bit EL1)
         */
        if (!arm_feature(env, ARM_FEATURE_AARCH64) &&
            unlikely(!kvm_arm_verify_ext_dabt_pending(cs))) {

            error_report("Data abort exception with no valid ISS generated by "
                   "guest memory access. KVM unable to emulate faulting "
                   "instruction. Failed to inject an external data abort "
                   "into the guest.");
            abort();
       }
       /* Clear the status */
       env->ext_dabt_raised = 0;
    }
}

MemTxAttrs kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
    ARMCPU *cpu;
    uint32_t switched_level;

    if (kvm_irqchip_in_kernel()) {
        /*
         * We only need to sync timer states with user-space interrupt
         * controllers, so return early and save cycles if we don't.
         */
        return MEMTXATTRS_UNSPECIFIED;
    }

    cpu = ARM_CPU(cs);

    /* Synchronize our shadowed in-kernel device irq lines with the kvm ones */
    if (run->s.regs.device_irq_level != cpu->device_irq_level) {
        switched_level = cpu->device_irq_level ^ run->s.regs.device_irq_level;

        qemu_mutex_lock_iothread();

        if (switched_level & KVM_ARM_DEV_EL1_VTIMER) {
            qemu_set_irq(cpu->gt_timer_outputs[GTIMER_VIRT],
                         !!(run->s.regs.device_irq_level &
                            KVM_ARM_DEV_EL1_VTIMER));
            switched_level &= ~KVM_ARM_DEV_EL1_VTIMER;
        }

        if (switched_level & KVM_ARM_DEV_EL1_PTIMER) {
            qemu_set_irq(cpu->gt_timer_outputs[GTIMER_PHYS],
                         !!(run->s.regs.device_irq_level &
                            KVM_ARM_DEV_EL1_PTIMER));
            switched_level &= ~KVM_ARM_DEV_EL1_PTIMER;
        }

        if (switched_level & KVM_ARM_DEV_PMU) {
            qemu_set_irq(cpu->pmu_interrupt,
                         !!(run->s.regs.device_irq_level & KVM_ARM_DEV_PMU));
            switched_level &= ~KVM_ARM_DEV_PMU;
        }

        if (switched_level) {
            qemu_log_mask(LOG_UNIMP, "%s: unhandled in-kernel device IRQ %x\n",
                          __func__, switched_level);
        }

        /* We also mark unknown levels as processed to not waste cycles */
        cpu->device_irq_level = run->s.regs.device_irq_level;
        qemu_mutex_unlock_iothread();
    }

    return MEMTXATTRS_UNSPECIFIED;
}

void kvm_arm_vm_state_change(void *opaque, bool running, RunState state)
{
    CPUState *cs = opaque;
    ARMCPU *cpu = ARM_CPU(cs);

    if (running) {
        if (cpu->kvm_adjvtime) {
            kvm_arm_put_virtual_time(cs);
        }
    } else {
        if (cpu->kvm_adjvtime) {
            kvm_arm_get_virtual_time(cs);
        }
    }
}

/**
 * kvm_arm_handle_dabt_nisv:
 * @cs: CPUState
 * @esr_iss: ISS encoding (limited) for the exception from Data Abort
 *           ISV bit set to '0b0' -> no valid instruction syndrome
 * @fault_ipa: faulting address for the synchronous data abort
 *
 * Returns: 0 if the exception has been handled, < 0 otherwise
 */
static int kvm_arm_handle_dabt_nisv(CPUState *cs, uint64_t esr_iss,
                                    uint64_t fault_ipa)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    /*
     * Request KVM to inject the external data abort into the guest
     */
    if (cap_has_inject_ext_dabt) {
        struct kvm_vcpu_events events = { };
        /*
         * The external data abort event will be handled immediately by KVM
         * using the address fault that triggered the exit on given VCPU.
         * Requesting injection of the external data abort does not rely
         * on any other VCPU state. Therefore, in this particular case, the VCPU
         * synchronization can be exceptionally skipped.
         */
        events.exception.ext_dabt_pending = 1;
        /* KVM_CAP_ARM_INJECT_EXT_DABT implies KVM_CAP_VCPU_EVENTS */
        if (!kvm_vcpu_ioctl(cs, KVM_SET_VCPU_EVENTS, &events)) {
            env->ext_dabt_raised = 1;
            return 0;
        }
    } else {
        error_report("Data abort exception triggered by guest memory access "
                     "at physical address: 0x"  TARGET_FMT_lx,
                     (target_ulong)fault_ipa);
        error_printf("KVM unable to emulate faulting instruction.\n");
    }
    return -1;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    int ret = 0;

    switch (run->exit_reason) {
    case KVM_EXIT_DEBUG:
        if (kvm_arm_handle_debug(cs, &run->debug.arch)) {
            ret = EXCP_DEBUG;
        } /* otherwise return to guest */
        break;
    case KVM_EXIT_ARM_NISV:
        /* External DABT with no valid iss to decode */
        ret = kvm_arm_handle_dabt_nisv(cs, run->arm_nisv.esr_iss,
                                       run->arm_nisv.fault_ipa);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: un-handled exit reason %d\n",
                      __func__, run->exit_reason);
        break;
    }
    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    return true;
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return 0;
}

void kvm_arch_update_guest_debug(CPUState *cs, struct kvm_guest_debug *dbg)
{
    if (kvm_sw_breakpoints_active(cs)) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP;
    }
    if (kvm_arm_hw_debug_active(cs)) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW;
        kvm_arm_copy_hw_debug_data(&dbg->arch);
    }
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}

int kvm_arch_irqchip_create(KVMState *s)
{
    if (kvm_kernel_irqchip_split()) {
        perror("-machine kernel_irqchip=split is not supported on ARM.");
        exit(1);
    }

    /* If we can create the VGIC using the newer device control API, we
     * let the device do this when it initializes itself, otherwise we
     * fall back to the old API */
    return kvm_check_extension(s, KVM_CAP_DEVICE_CTRL);
}

int kvm_arm_vgic_probe(void)
{
    int val = 0;

    if (kvm_create_device(kvm_state,
                          KVM_DEV_TYPE_ARM_VGIC_V3, true) == 0) {
        val |= KVM_ARM_VGIC_V3;
    }
    if (kvm_create_device(kvm_state,
                          KVM_DEV_TYPE_ARM_VGIC_V2, true) == 0) {
        val |= KVM_ARM_VGIC_V2;
    }
    return val;
}

int kvm_arm_set_irq(int cpu, int irqtype, int irq, int level)
{
    int kvm_irq = (irqtype << KVM_ARM_IRQ_TYPE_SHIFT) | irq;
    int cpu_idx1 = cpu % 256;
    int cpu_idx2 = cpu / 256;

    kvm_irq |= (cpu_idx1 << KVM_ARM_IRQ_VCPU_SHIFT) |
               (cpu_idx2 << KVM_ARM_IRQ_VCPU2_SHIFT);

    return kvm_set_irq(kvm_state, kvm_irq, !!level);
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
    AddressSpace *as = pci_device_iommu_address_space(dev);
    hwaddr xlat, len, doorbell_gpa;
    MemoryRegionSection mrs;
    MemoryRegion *mr;

    if (as == &address_space_memory) {
        return 0;
    }

    /* MSI doorbell address is translated by an IOMMU */

    RCU_READ_LOCK_GUARD();

    mr = address_space_translate(as, address, &xlat, &len, true,
                                 MEMTXATTRS_UNSPECIFIED);

    if (!mr) {
        return 1;
    }

    mrs = memory_region_find(mr, xlat, 1);

    if (!mrs.mr) {
        return 1;
    }

    doorbell_gpa = mrs.offset_within_address_space;
    memory_region_unref(mrs.mr);

    route->u.msi.address_lo = doorbell_gpa;
    route->u.msi.address_hi = doorbell_gpa >> 32;

    trace_kvm_arm_fixup_msi_route(address, doorbell_gpa);

    return 0;
}

int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_release_virq_post(int virq)
{
    return 0;
}

int kvm_arch_msi_data_to_gsi(uint32_t data)
{
    return (data - 32) & 0xffff;
}

bool kvm_arch_cpu_check_are_resettable(void)
{
    return true;
}
