/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors:
 *  Ziqiao Zhou       <ziqiaozhou@microsoft.com>
 *  Magnus Kulke      <magnuskulke@microsoft.com>
 *  Jinank Jain       <jinankjain@microsoft.com>
 *  Wei Liu           <liuwe@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "hw/boards.h"

#include "hw/hyperv/hvhdk.h"
#include "hw/hyperv/hvhdk_mini.h"
#include "hw/hyperv/hvgdk.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "linux/mshv.h"

#include "qemu/accel.h"
#include "qemu/guest-random.h"
#include "accel/accel-ops.h"
#include "accel/accel-cpu-ops.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "system/accel-blocker.h"
#include "system/address-spaces.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
#include "system/reset.h"
#include "trace.h"
#include <err.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define TYPE_MSHV_ACCEL ACCEL_CLASS_NAME("mshv")

DECLARE_INSTANCE_CHECKER(MshvState, MSHV_STATE, TYPE_MSHV_ACCEL)

bool mshv_allowed;

MshvState *mshv_state;

static int init_mshv(int *mshv_fd)
{
    int fd = open("/dev/mshv", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error_report("Failed to open /dev/mshv: %s", strerror(errno));
        return -1;
    }
    *mshv_fd = fd;
    return 0;
}

/* freeze 1 to pause, 0 to resume */
static int set_time_freeze(int vm_fd, int freeze)
{
    int ret;
    struct hv_input_set_partition_property in = {0};
    in.property_code = HV_PARTITION_PROPERTY_TIME_FREEZE;
    in.property_value = freeze;

    struct mshv_root_hvcall args = {0};
    args.code = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz = sizeof(in);
    args.in_ptr = (uint64_t)&in;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set time freeze");
        return -1;
    }

    return 0;
}

static int pause_vm(int vm_fd)
{
    int ret;

    ret = set_time_freeze(vm_fd, 1);
    if (ret < 0) {
        error_report("Failed to pause partition: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int resume_vm(int vm_fd)
{
    int ret;

    ret = set_time_freeze(vm_fd, 0);
    if (ret < 0) {
        error_report("Failed to resume partition: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int create_partition(int mshv_fd, int *vm_fd)
{
    int ret;
    struct mshv_create_partition args = {0};

    /* Initialize pt_flags with the desired features */
    uint64_t pt_flags = (1ULL << MSHV_PT_BIT_LAPIC) |
                        (1ULL << MSHV_PT_BIT_X2APIC) |
                        (1ULL << MSHV_PT_BIT_GPA_SUPER_PAGES);

    /* Set default isolation type */
    uint64_t pt_isolation = MSHV_PT_ISOLATION_NONE;

    args.pt_flags = pt_flags;
    args.pt_isolation = pt_isolation;

    ret = ioctl(mshv_fd, MSHV_CREATE_PARTITION, &args);
    if (ret < 0) {
        error_report("Failed to create partition: %s", strerror(errno));
        return -1;
    }

    *vm_fd = ret;
    return 0;
}

static int set_synthetic_proc_features(int vm_fd)
{
    int ret;
    struct hv_input_set_partition_property in = {0};
    union hv_partition_synthetic_processor_features features = {0};

    /* Access the bitfield and set the desired features */
    features.hypervisor_present = 1;
    features.hv1 = 1;
    features.access_partition_reference_counter = 1;
    features.access_synic_regs = 1;
    features.access_synthetic_timer_regs = 1;
    features.access_partition_reference_tsc = 1;
    features.access_frequency_regs = 1;
    features.access_intr_ctrl_regs = 1;
    features.access_vp_index = 1;
    features.access_hypercall_regs = 1;
    features.tb_flush_hypercalls = 1;
    features.synthetic_cluster_ipi = 1;
    features.direct_synthetic_timers = 1;

    mshv_arch_amend_proc_features(&features);

    in.property_code = HV_PARTITION_PROPERTY_SYNTHETIC_PROC_FEATURES;
    in.property_value = features.as_uint64[0];

    struct mshv_root_hvcall args = {0};
    args.code = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz = sizeof(in);
    args.in_ptr = (uint64_t)&in;

    trace_mshv_hvcall_args("synthetic_proc_features", args.code, args.in_sz);

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set synthethic proc features");
        return -errno;
    }
    return 0;
}

static int initialize_vm(int vm_fd)
{
    int ret = ioctl(vm_fd, MSHV_INITIALIZE_PARTITION);
    if (ret < 0) {
        error_report("Failed to initialize partition: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int create_vm(int mshv_fd, int *vm_fd)
{
    int ret = create_partition(mshv_fd, vm_fd);
    if (ret < 0) {
        return -1;
    }

    ret = set_synthetic_proc_features(*vm_fd);
    if (ret < 0) {
        return -1;
    }

    ret = initialize_vm(*vm_fd);
    if (ret < 0) {
        return -1;
    }

    ret = mshv_reserve_ioapic_msi_routes(*vm_fd);
    if (ret < 0) {
        return -1;
    }

    ret = mshv_arch_post_init_vm(*vm_fd);
    if (ret < 0) {
        return -1;
    }

    /* Always create a frozen partition */
    pause_vm(*vm_fd);

    return 0;
}

static void mem_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    MshvMemoryListener *mml;
    mml = container_of(listener, MshvMemoryListener, listener);
    memory_region_ref(section->mr);
    mshv_set_phys_mem(mml, section, true);
}

static void mem_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    MshvMemoryListener *mml;
    mml = container_of(listener, MshvMemoryListener, listener);
    mshv_set_phys_mem(mml, section, false);
    memory_region_unref(section->mr);
}

typedef enum {
    DATAMATCH_NONE,
    DATAMATCH_U32,
    DATAMATCH_U64,
} DatamatchTag;

typedef struct {
    DatamatchTag tag;
    union {
        uint32_t u32;
        uint64_t u64;
    } value;
} Datamatch;

/* flags: determine whether to de/assign */
static int ioeventfd(int vm_fd, int event_fd, uint64_t addr, Datamatch dm,
                     uint32_t flags)
{
    struct mshv_user_ioeventfd args = {0};
    args.fd = event_fd;
    args.addr = addr;
    args.flags = flags;

    if (dm.tag == DATAMATCH_NONE) {
        args.datamatch = 0;
    } else {
        flags |= BIT(MSHV_IOEVENTFD_BIT_DATAMATCH);
        args.flags = flags;
        if (dm.tag == DATAMATCH_U64) {
            args.len = sizeof(uint64_t);
            args.datamatch = dm.value.u64;
        } else {
            args.len = sizeof(uint32_t);
            args.datamatch = dm.value.u32;
        }
    }

    return ioctl(vm_fd, MSHV_IOEVENTFD, &args);
}

static int unregister_ioevent(int vm_fd, int event_fd, uint64_t mmio_addr)
{
    uint32_t flags = 0;
    Datamatch dm = {0};

    flags |= BIT(MSHV_IOEVENTFD_BIT_DEASSIGN);
    dm.tag = DATAMATCH_NONE;

    return ioeventfd(vm_fd, event_fd, mmio_addr, dm, flags);
}

static int register_ioevent(int vm_fd, int event_fd, uint64_t mmio_addr,
                            uint64_t val, bool is_64bit, bool is_datamatch)
{
    uint32_t flags = 0;
    Datamatch dm = {0};

    if (!is_datamatch) {
        dm.tag = DATAMATCH_NONE;
    } else if (is_64bit) {
        dm.tag = DATAMATCH_U64;
        dm.value.u64 = val;
    } else {
        dm.tag = DATAMATCH_U32;
        dm.value.u32 = val;
    }

    return ioeventfd(vm_fd, event_fd, mmio_addr, dm, flags);
}

static void mem_ioeventfd_add(MemoryListener *listener,
                              MemoryRegionSection *section,
                              bool match_data, uint64_t data,
                              EventNotifier *e)
{
    int fd = event_notifier_get_fd(e);
    int ret;
    bool is_64 = int128_get64(section->size) == 8;
    uint64_t addr = section->offset_within_address_space;

    trace_mshv_mem_ioeventfd_add(addr, int128_get64(section->size), data);

    ret = register_ioevent(mshv_state->vm, fd, addr, data, is_64, match_data);

    if (ret < 0) {
        error_report("Failed to register ioeventfd: %s (%d)", strerror(-ret),
                     -ret);
        abort();
    }
}

static void mem_ioeventfd_del(MemoryListener *listener,
                              MemoryRegionSection *section,
                              bool match_data, uint64_t data,
                              EventNotifier *e)
{
    int fd = event_notifier_get_fd(e);
    int ret;
    uint64_t addr = section->offset_within_address_space;

    trace_mshv_mem_ioeventfd_del(section->offset_within_address_space,
                                 int128_get64(section->size), data);

    ret = unregister_ioevent(mshv_state->vm, fd, addr);
    if (ret < 0) {
        error_report("Failed to unregister ioeventfd: %s (%d)", strerror(-ret),
                     -ret);
        abort();
    }
}

static MemoryListener mshv_memory_listener = {
    .name = "mshv",
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
    .region_add = mem_region_add,
    .region_del = mem_region_del,
    .eventfd_add = mem_ioeventfd_add,
    .eventfd_del = mem_ioeventfd_del,
};

static MemoryListener mshv_io_listener = {
    .name = "mshv", .priority = MEMORY_LISTENER_PRIORITY_DEV_BACKEND,
    /* MSHV does not support PIO eventfd */
};

static void register_mshv_memory_listener(MshvState *s, MshvMemoryListener *mml,
                                          AddressSpace *as, int as_id,
                                          const char *name)
{
    int i;

    mml->listener = mshv_memory_listener;
    mml->listener.name = name;
    memory_listener_register(&mml->listener, as);
    for (i = 0; i < s->nr_as; ++i) {
        if (!s->as[i].as) {
            s->as[i].as = as;
            s->as[i].ml = mml;
            break;
        }
    }
}

int mshv_hvcall(int fd, const struct mshv_root_hvcall *args)
{
    int ret = 0;

    ret = ioctl(fd, MSHV_ROOT_HVCALL, args);
    if (ret < 0) {
        error_report("Failed to perform hvcall: %s", strerror(errno));
        return -1;
    }
    return ret;
}

static int mshv_init_vcpu(CPUState *cpu)
{
    int vm_fd = mshv_state->vm;
    uint8_t vp_index = cpu->cpu_index;
    int ret;

    cpu->accel = g_new0(AccelCPUState, 1);
    mshv_arch_init_vcpu(cpu);

    ret = mshv_create_vcpu(vm_fd, vp_index, &cpu->accel->cpufd);
    if (ret < 0) {
        return -1;
    }

    cpu->accel->dirty = true;

    return 0;
}

static int mshv_init(AccelState *as, MachineState *ms)
{
    MshvState *s;
    int mshv_fd, vm_fd, ret;

    if (mshv_state) {
        warn_report("MSHV accelerator already initialized");
        return 0;
    }

    s = MSHV_STATE(as);

    accel_blocker_init();

    s->vm = 0;

    ret = init_mshv(&mshv_fd);
    if (ret < 0) {
        return -1;
    }

    mshv_init_mmio_emu();

    mshv_init_msicontrol();

    mshv_init_memory_slot_manager(s);

    ret = create_vm(mshv_fd, &vm_fd);
    if (ret < 0) {
        close(mshv_fd);
        return -1;
    }

    ret = resume_vm(vm_fd);
    if (ret < 0) {
        close(mshv_fd);
        close(vm_fd);
        return -1;
    }

    s->vm = vm_fd;
    s->fd = mshv_fd;
    s->nr_as = 1;
    s->as = g_new0(MshvAddressSpace, s->nr_as);

    mshv_state = s;

    register_mshv_memory_listener(s, &s->memory_listener, &address_space_memory,
                                  0, "mshv-memory");
    memory_listener_register(&mshv_io_listener, &address_space_io);

    return 0;
}

static int mshv_destroy_vcpu(CPUState *cpu)
{
    int cpu_fd = mshv_vcpufd(cpu);
    int vm_fd = mshv_state->vm;

    mshv_remove_vcpu(vm_fd, cpu_fd);
    mshv_vcpufd(cpu) = 0;

    mshv_arch_destroy_vcpu(cpu);
    g_clear_pointer(&cpu->accel, g_free);
    return 0;
}

static int mshv_cpu_exec(CPUState *cpu)
{
    hv_message mshv_msg;
    enum MshvVmExit exit_reason;
    int ret = 0;

    bql_unlock();
    cpu_exec_start(cpu);

    do {
        if (cpu->accel->dirty) {
            ret = mshv_arch_put_registers(cpu);
            if (ret) {
                error_report("Failed to put registers after init: %s",
                              strerror(-ret));
                ret = -1;
                break;
            }
            cpu->accel->dirty = false;
        }

        ret = mshv_run_vcpu(mshv_state->vm, cpu, &mshv_msg, &exit_reason);
        if (ret < 0) {
            error_report("Failed to run on vcpu %d", cpu->cpu_index);
            abort();
        }

        switch (exit_reason) {
        case MshvVmExitIgnore:
            break;
        default:
            ret = EXCP_INTERRUPT;
            break;
        }
    } while (ret == 0);

    cpu_exec_end(cpu);
    bql_lock();

    if (ret < 0) {
        cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
        vm_stop(RUN_STATE_INTERNAL_ERROR);
    }

    return ret;
}

/*
 * The signal handler is triggered when QEMU's main thread receives a SIG_IPI
 * (SIGUSR1). This signal causes the current CPU thread to be kicked, forcing a
 * VM exit on the CPU. The VM exit generates an exit reason that breaks the loop
 * (see mshv_cpu_exec). If the exit is due to a Ctrl+A+x command, the system
 * will shut down. For other cases, the system will continue running.
 */
static void sa_ipi_handler(int sig)
{
    /* TODO: call IOCTL to set_immediate_exit, once implemented. */

    qemu_cpu_kick_self();
}

static void init_signal(CPUState *cpu)
{
    /* init cpu signals */
    struct sigaction sigact;
    sigset_t set;

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = sa_ipi_handler;
    sigaction(SIG_IPI, &sigact, NULL);

    pthread_sigmask(SIG_BLOCK, NULL, &set);
    sigdelset(&set, SIG_IPI);
    pthread_sigmask(SIG_SETMASK, &set, NULL);
}

static void *mshv_vcpu_thread(void *arg)
{
    CPUState *cpu = arg;
    int ret;

    rcu_register_thread();

    bql_lock();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;
    ret = mshv_init_vcpu(cpu);
    if (ret < 0) {
        error_report("Failed to init vcpu %d", cpu->cpu_index);
        goto cleanup;
    }
    init_signal(cpu);

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        qemu_process_cpu_events(cpu);
        if (cpu_can_run(cpu)) {
            mshv_cpu_exec(cpu);
        }
    } while (!cpu->unplug || cpu_can_run(cpu));

    mshv_destroy_vcpu(cpu);
cleanup:
    cpu_thread_signal_destroyed(cpu);
    bql_unlock();
    rcu_unregister_thread();
    return NULL;
}

static void mshv_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/MSHV",
             cpu->cpu_index);

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));

    qemu_cond_init(cpu->halt_cond);

    trace_mshv_start_vcpu_thread(thread_name, cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, mshv_vcpu_thread, cpu,
                       QEMU_THREAD_JOINABLE);
}

static void do_mshv_cpu_synchronize_post_init(CPUState *cpu,
                                              run_on_cpu_data arg)
{
    int ret = mshv_arch_put_registers(cpu);
    if (ret < 0) {
        error_report("Failed to put registers after init: %s", strerror(-ret));
        abort();
    }

    cpu->accel->dirty = false;
}

static void mshv_cpu_synchronize_post_init(CPUState *cpu)
{
    run_on_cpu(cpu, do_mshv_cpu_synchronize_post_init, RUN_ON_CPU_NULL);
}

static void mshv_cpu_synchronize_post_reset(CPUState *cpu)
{
    int ret = mshv_arch_put_registers(cpu);
    if (ret) {
        error_report("Failed to put registers after reset: %s",
                     strerror(-ret));
        cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
        vm_stop(RUN_STATE_INTERNAL_ERROR);
    }
    cpu->accel->dirty = false;
}

static void do_mshv_cpu_synchronize_pre_loadvm(CPUState *cpu,
                                               run_on_cpu_data arg)
{
    cpu->accel->dirty = true;
}

static void mshv_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    run_on_cpu(cpu, do_mshv_cpu_synchronize_pre_loadvm, RUN_ON_CPU_NULL);
}

static void do_mshv_cpu_synchronize(CPUState *cpu, run_on_cpu_data arg)
{
    if (!cpu->accel->dirty) {
        int ret = mshv_load_regs(cpu);
        if (ret < 0) {
            error_report("Failed to load registers for vcpu %d",
                         cpu->cpu_index);

            cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
            vm_stop(RUN_STATE_INTERNAL_ERROR);
        }

        cpu->accel->dirty = true;
    }
}

static void mshv_cpu_synchronize(CPUState *cpu)
{
    if (!cpu->accel->dirty) {
        run_on_cpu(cpu, do_mshv_cpu_synchronize, RUN_ON_CPU_NULL);
    }
}

static bool mshv_cpus_are_resettable(void)
{
    return false;
}

static void mshv_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);

    ac->name = "MSHV";
    ac->init_machine = mshv_init;
    ac->allowed = &mshv_allowed;
}

static void mshv_accel_instance_init(Object *obj)
{
    MshvState *s = MSHV_STATE(obj);

    s->vm = 0;
}

static const TypeInfo mshv_accel_type = {
    .name = TYPE_MSHV_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_init = mshv_accel_instance_init,
    .class_init = mshv_accel_class_init,
    .instance_size = sizeof(MshvState),
};

static void mshv_accel_ops_class_init(ObjectClass *oc, const void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->create_vcpu_thread = mshv_start_vcpu_thread;
    ops->synchronize_post_init = mshv_cpu_synchronize_post_init;
    ops->synchronize_post_reset = mshv_cpu_synchronize_post_reset;
    ops->synchronize_state = mshv_cpu_synchronize;
    ops->synchronize_pre_loadvm = mshv_cpu_synchronize_pre_loadvm;
    ops->cpus_are_resettable = mshv_cpus_are_resettable;
    ops->handle_interrupt = generic_handle_interrupt;
}

static const TypeInfo mshv_accel_ops_type = {
    .name = ACCEL_OPS_NAME("mshv"),
    .parent = TYPE_ACCEL_OPS,
    .class_init = mshv_accel_ops_class_init,
    .abstract = true,
};

static void mshv_type_init(void)
{
    type_register_static(&mshv_accel_type);
    type_register_static(&mshv_accel_ops_type);
}

type_init(mshv_type_init);
