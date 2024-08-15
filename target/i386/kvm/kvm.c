/*
 * QEMU KVM support
 *
 * Copyright (C) 2006-2008 Qumranet Technologies
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/qapi-events-run-state.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include <math.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include "standard-headers/asm-x86/kvm_para.h"
#include "hw/xen/interface/arch-x86/cpuid.h"

#include "cpu.h"
#include "host-cpu.h"
#include "vmsr_energy.h"
#include "sysemu/sysemu.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm_int.h"
#include "sysemu/runstate.h"
#include "kvm_i386.h"
#include "../confidential-guest.h"
#include "sev.h"
#include "xen-emu.h"
#include "hyperv.h"
#include "hyperv-proto.h"

#include "gdbstub/enums.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "qemu/ratelimit.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/memalign.h"
#include "hw/i386/x86.h"
#include "hw/i386/kvm/xen_evtchn.h"
#include "hw/i386/pc.h"
#include "hw/i386/apic.h"
#include "hw/i386/apic_internal.h"
#include "hw/i386/apic-msidef.h"
#include "hw/i386/intel_iommu.h"
#include "hw/i386/topology.h"
#include "hw/i386/x86-iommu.h"
#include "hw/i386/e820_memory_layout.h"

#include "hw/xen/xen.h"

#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "migration/blocker.h"
#include "exec/memattrs.h"
#include "trace.h"

#include CONFIG_DEVICES

//#define DEBUG_KVM

#ifdef DEBUG_KVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

/* From arch/x86/kvm/lapic.h */
#define KVM_APIC_BUS_CYCLE_NS       1
#define KVM_APIC_BUS_FREQUENCY      (1000000000ULL / KVM_APIC_BUS_CYCLE_NS)

#define MSR_KVM_WALL_CLOCK  0x11
#define MSR_KVM_SYSTEM_TIME 0x12

/* A 4096-byte buffer can hold the 8-byte kvm_msrs header, plus
 * 255 kvm_msr_entry structs */
#define MSR_BUF_SIZE 4096

static void kvm_init_msrs(X86CPU *cpu);

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_INFO(SET_TSS_ADDR),
    KVM_CAP_INFO(EXT_CPUID),
    KVM_CAP_INFO(MP_STATE),
    KVM_CAP_INFO(SIGNAL_MSI),
    KVM_CAP_INFO(IRQ_ROUTING),
    KVM_CAP_INFO(DEBUGREGS),
    KVM_CAP_INFO(XSAVE),
    KVM_CAP_INFO(VCPU_EVENTS),
    KVM_CAP_INFO(X86_ROBUST_SINGLESTEP),
    KVM_CAP_INFO(MCE),
    KVM_CAP_INFO(ADJUST_CLOCK),
    KVM_CAP_INFO(SET_IDENTITY_MAP_ADDR),
    KVM_CAP_LAST_INFO
};

static bool has_msr_star;
static bool has_msr_hsave_pa;
static bool has_msr_tsc_aux;
static bool has_msr_tsc_adjust;
static bool has_msr_tsc_deadline;
static bool has_msr_feature_control;
static bool has_msr_misc_enable;
static bool has_msr_smbase;
static bool has_msr_bndcfgs;
static int lm_capable_kernel;
static bool has_msr_hv_hypercall;
static bool has_msr_hv_crash;
static bool has_msr_hv_reset;
static bool has_msr_hv_vpindex;
static bool hv_vpindex_settable;
static bool has_msr_hv_runtime;
static bool has_msr_hv_synic;
static bool has_msr_hv_stimer;
static bool has_msr_hv_frequencies;
static bool has_msr_hv_reenlightenment;
static bool has_msr_hv_syndbg_options;
static bool has_msr_xss;
static bool has_msr_umwait;
static bool has_msr_spec_ctrl;
static bool has_tsc_scale_msr;
static bool has_msr_tsx_ctrl;
static bool has_msr_virt_ssbd;
static bool has_msr_smi_count;
static bool has_msr_arch_capabs;
static bool has_msr_core_capabs;
static bool has_msr_vmx_vmfunc;
static bool has_msr_ucode_rev;
static bool has_msr_vmx_procbased_ctls2;
static bool has_msr_perf_capabs;
static bool has_msr_pkrs;

static uint32_t has_architectural_pmu_version;
static uint32_t num_architectural_pmu_gp_counters;
static uint32_t num_architectural_pmu_fixed_counters;

static int has_xsave2;
static int has_xcrs;
static int has_sregs2;
static int has_exception_payload;
static int has_triple_fault_event;

static bool has_msr_mcg_ext_ctl;

static struct kvm_cpuid2 *cpuid_cache;
static struct kvm_cpuid2 *hv_cpuid_cache;
static struct kvm_msr_list *kvm_feature_msrs;

static KVMMSRHandlers msr_handlers[KVM_MSR_FILTER_MAX_RANGES];

#define BUS_LOCK_SLICE_TIME 1000000000ULL /* ns */
static RateLimit bus_lock_ratelimit_ctrl;
static int kvm_get_one_msr(X86CPU *cpu, int index, uint64_t *value);

static const char *vm_type_name[] = {
    [KVM_X86_DEFAULT_VM] = "default",
    [KVM_X86_SEV_VM] = "SEV",
    [KVM_X86_SEV_ES_VM] = "SEV-ES",
    [KVM_X86_SNP_VM] = "SEV-SNP",
};

bool kvm_is_vm_type_supported(int type)
{
    uint32_t machine_types;

    /*
     * old KVM doesn't support KVM_CAP_VM_TYPES but KVM_X86_DEFAULT_VM
     * is always supported
     */
    if (type == KVM_X86_DEFAULT_VM) {
        return true;
    }

    machine_types = kvm_check_extension(KVM_STATE(current_machine->accelerator),
                                        KVM_CAP_VM_TYPES);
    return !!(machine_types & BIT(type));
}

int kvm_get_vm_type(MachineState *ms)
{
    int kvm_type = KVM_X86_DEFAULT_VM;

    if (ms->cgs) {
        if (!object_dynamic_cast(OBJECT(ms->cgs), TYPE_X86_CONFIDENTIAL_GUEST)) {
            error_report("configuration type %s not supported for x86 guests",
                         object_get_typename(OBJECT(ms->cgs)));
            exit(1);
        }
        kvm_type = x86_confidential_guest_kvm_type(
            X86_CONFIDENTIAL_GUEST(ms->cgs));
    }

    if (!kvm_is_vm_type_supported(kvm_type)) {
        error_report("vm-type %s not supported by KVM", vm_type_name[kvm_type]);
        exit(1);
    }

    return kvm_type;
}

bool kvm_enable_hypercall(uint64_t enable_mask)
{
    KVMState *s = KVM_STATE(current_accel());

    return !kvm_vm_enable_cap(s, KVM_CAP_EXIT_HYPERCALL, 0, enable_mask);
}

bool kvm_has_smm(void)
{
    return kvm_vm_check_extension(kvm_state, KVM_CAP_X86_SMM);
}

bool kvm_has_adjust_clock_stable(void)
{
    int ret = kvm_check_extension(kvm_state, KVM_CAP_ADJUST_CLOCK);

    return (ret & KVM_CLOCK_TSC_STABLE);
}

bool kvm_has_exception_payload(void)
{
    return has_exception_payload;
}

static bool kvm_x2apic_api_set_flags(uint64_t flags)
{
    KVMState *s = KVM_STATE(current_accel());

    return !kvm_vm_enable_cap(s, KVM_CAP_X2APIC_API, 0, flags);
}

#define MEMORIZE(fn, _result) \
    ({ \
        static bool _memorized; \
        \
        if (_memorized) { \
            return _result; \
        } \
        _memorized = true; \
        _result = fn; \
    })

static bool has_x2apic_api;

bool kvm_has_x2apic_api(void)
{
    return has_x2apic_api;
}

bool kvm_enable_x2apic(void)
{
    return MEMORIZE(
             kvm_x2apic_api_set_flags(KVM_X2APIC_API_USE_32BIT_IDS |
                                      KVM_X2APIC_API_DISABLE_BROADCAST_QUIRK),
             has_x2apic_api);
}

bool kvm_hv_vpindex_settable(void)
{
    return hv_vpindex_settable;
}

static int kvm_get_tsc(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint64_t value;
    int ret;

    if (env->tsc_valid) {
        return 0;
    }

    env->tsc_valid = !runstate_is_running();

    ret = kvm_get_one_msr(cpu, MSR_IA32_TSC, &value);
    if (ret < 0) {
        return ret;
    }

    env->tsc = value;
    return 0;
}

static inline void do_kvm_synchronize_tsc(CPUState *cpu, run_on_cpu_data arg)
{
    kvm_get_tsc(cpu);
}

void kvm_synchronize_all_tsc(void)
{
    CPUState *cpu;

    if (kvm_enabled()) {
        CPU_FOREACH(cpu) {
            run_on_cpu(cpu, do_kvm_synchronize_tsc, RUN_ON_CPU_NULL);
        }
    }
}

static struct kvm_cpuid2 *try_get_cpuid(KVMState *s, int max)
{
    struct kvm_cpuid2 *cpuid;
    int r, size;

    size = sizeof(*cpuid) + max * sizeof(*cpuid->entries);
    cpuid = g_malloc0(size);
    cpuid->nent = max;
    r = kvm_ioctl(s, KVM_GET_SUPPORTED_CPUID, cpuid);
    if (r == 0 && cpuid->nent >= max) {
        r = -E2BIG;
    }
    if (r < 0) {
        if (r == -E2BIG) {
            g_free(cpuid);
            return NULL;
        } else {
            fprintf(stderr, "KVM_GET_SUPPORTED_CPUID failed: %s\n",
                    strerror(-r));
            exit(1);
        }
    }
    return cpuid;
}

/* Run KVM_GET_SUPPORTED_CPUID ioctl(), allocating a buffer large enough
 * for all entries.
 */
static struct kvm_cpuid2 *get_supported_cpuid(KVMState *s)
{
    struct kvm_cpuid2 *cpuid;
    int max = 1;

    if (cpuid_cache != NULL) {
        return cpuid_cache;
    }
    while ((cpuid = try_get_cpuid(s, max)) == NULL) {
        max *= 2;
    }
    cpuid_cache = cpuid;
    return cpuid;
}

static bool host_tsx_broken(void)
{
    int family, model, stepping;\
    char vendor[CPUID_VENDOR_SZ + 1];

    host_cpu_vendor_fms(vendor, &family, &model, &stepping);

    /* Check if we are running on a Haswell host known to have broken TSX */
    return !strcmp(vendor, CPUID_VENDOR_INTEL) &&
           (family == 6) &&
           ((model == 63 && stepping < 4) ||
            model == 60 || model == 69 || model == 70);
}

/* Returns the value for a specific register on the cpuid entry
 */
static uint32_t cpuid_entry_get_reg(struct kvm_cpuid_entry2 *entry, int reg)
{
    uint32_t ret = 0;
    switch (reg) {
    case R_EAX:
        ret = entry->eax;
        break;
    case R_EBX:
        ret = entry->ebx;
        break;
    case R_ECX:
        ret = entry->ecx;
        break;
    case R_EDX:
        ret = entry->edx;
        break;
    }
    return ret;
}

/* Find matching entry for function/index on kvm_cpuid2 struct
 */
static struct kvm_cpuid_entry2 *cpuid_find_entry(struct kvm_cpuid2 *cpuid,
                                                 uint32_t function,
                                                 uint32_t index)
{
    int i;
    for (i = 0; i < cpuid->nent; ++i) {
        if (cpuid->entries[i].function == function &&
            cpuid->entries[i].index == index) {
            return &cpuid->entries[i];
        }
    }
    /* not found: */
    return NULL;
}

uint32_t kvm_arch_get_supported_cpuid(KVMState *s, uint32_t function,
                                      uint32_t index, int reg)
{
    struct kvm_cpuid2 *cpuid;
    uint32_t ret = 0;
    uint32_t cpuid_1_edx, unused;
    uint64_t bitmask;

    cpuid = get_supported_cpuid(s);

    struct kvm_cpuid_entry2 *entry = cpuid_find_entry(cpuid, function, index);
    if (entry) {
        ret = cpuid_entry_get_reg(entry, reg);
    }

    /* Fixups for the data returned by KVM, below */

    if (function == 1 && reg == R_EDX) {
        /* KVM before 2.6.30 misreports the following features */
        ret |= CPUID_MTRR | CPUID_PAT | CPUID_MCE | CPUID_MCA;
        /* KVM never reports CPUID_HT but QEMU can support when vcpus > 1 */
        ret |= CPUID_HT;
    } else if (function == 1 && reg == R_ECX) {
        /* We can set the hypervisor flag, even if KVM does not return it on
         * GET_SUPPORTED_CPUID
         */
        ret |= CPUID_EXT_HYPERVISOR;
        /* tsc-deadline flag is not returned by GET_SUPPORTED_CPUID, but it
         * can be enabled if the kernel has KVM_CAP_TSC_DEADLINE_TIMER,
         * and the irqchip is in the kernel.
         */
        if (kvm_irqchip_in_kernel() &&
                kvm_check_extension(s, KVM_CAP_TSC_DEADLINE_TIMER)) {
            ret |= CPUID_EXT_TSC_DEADLINE_TIMER;
        }

        /* x2apic is reported by GET_SUPPORTED_CPUID, but it can't be enabled
         * without the in-kernel irqchip
         */
        if (!kvm_irqchip_in_kernel()) {
            ret &= ~CPUID_EXT_X2APIC;
        }

        if (enable_cpu_pm) {
            int disable_exits = kvm_check_extension(s,
                                                    KVM_CAP_X86_DISABLE_EXITS);

            if (disable_exits & KVM_X86_DISABLE_EXITS_MWAIT) {
                ret |= CPUID_EXT_MONITOR;
            }
        }
    } else if (function == 6 && reg == R_EAX) {
        ret |= CPUID_6_EAX_ARAT; /* safe to allow because of emulated APIC */
    } else if (function == 7 && index == 0 && reg == R_EBX) {
        /* Not new instructions, just an optimization.  */
        uint32_t ebx;
        host_cpuid(7, 0, &unused, &ebx, &unused, &unused);
        ret |= ebx & CPUID_7_0_EBX_ERMS;

        if (host_tsx_broken()) {
            ret &= ~(CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_HLE);
        }
    } else if (function == 7 && index == 0 && reg == R_EDX) {
        /* Not new instructions, just an optimization.  */
        uint32_t edx;
        host_cpuid(7, 0, &unused, &unused, &unused, &edx);
        ret |= edx & CPUID_7_0_EDX_FSRM;

        /*
         * Linux v4.17-v4.20 incorrectly return ARCH_CAPABILITIES on SVM hosts.
         * We can detect the bug by checking if MSR_IA32_ARCH_CAPABILITIES is
         * returned by KVM_GET_MSR_INDEX_LIST.
         */
        if (!has_msr_arch_capabs) {
            ret &= ~CPUID_7_0_EDX_ARCH_CAPABILITIES;
        }
    } else if (function == 7 && index == 1 && reg == R_EAX) {
        /* Not new instructions, just an optimization.  */
        uint32_t eax;
        host_cpuid(7, 1, &eax, &unused, &unused, &unused);
        ret |= eax & (CPUID_7_1_EAX_FZRM | CPUID_7_1_EAX_FSRS | CPUID_7_1_EAX_FSRC);
    } else if (function == 7 && index == 2 && reg == R_EDX) {
        uint32_t edx;
        host_cpuid(7, 2, &unused, &unused, &unused, &edx);
        ret |= edx & CPUID_7_2_EDX_MCDT_NO;
    } else if (function == 0xd && index == 0 &&
               (reg == R_EAX || reg == R_EDX)) {
        /*
         * The value returned by KVM_GET_SUPPORTED_CPUID does not include
         * features that still have to be enabled with the arch_prctl
         * system call.  QEMU needs the full value, which is retrieved
         * with KVM_GET_DEVICE_ATTR.
         */
        struct kvm_device_attr attr = {
            .group = 0,
            .attr = KVM_X86_XCOMP_GUEST_SUPP,
            .addr = (unsigned long) &bitmask
        };

        bool sys_attr = kvm_check_extension(s, KVM_CAP_SYS_ATTRIBUTES);
        if (!sys_attr) {
            return ret;
        }

        int rc = kvm_ioctl(s, KVM_GET_DEVICE_ATTR, &attr);
        if (rc < 0) {
            if (rc != -ENXIO) {
                warn_report("KVM_GET_DEVICE_ATTR(0, KVM_X86_XCOMP_GUEST_SUPP) "
                            "error: %d", rc);
            }
            return ret;
        }
        ret = (reg == R_EAX) ? bitmask : bitmask >> 32;
    } else if (function == 0x80000001 && reg == R_ECX) {
        /*
         * It's safe to enable TOPOEXT even if it's not returned by
         * GET_SUPPORTED_CPUID.  Unconditionally enabling TOPOEXT here allows
         * us to keep CPU models including TOPOEXT runnable on older kernels.
         */
        ret |= CPUID_EXT3_TOPOEXT;
    } else if (function == 0x80000001 && reg == R_EDX) {
        /* On Intel, kvm returns cpuid according to the Intel spec,
         * so add missing bits according to the AMD spec:
         */
        cpuid_1_edx = kvm_arch_get_supported_cpuid(s, 1, 0, R_EDX);
        ret |= cpuid_1_edx & CPUID_EXT2_AMD_ALIASES;
    } else if (function == 0x80000007 && reg == R_EBX) {
        ret |= CPUID_8000_0007_EBX_OVERFLOW_RECOV | CPUID_8000_0007_EBX_SUCCOR;
    } else if (function == KVM_CPUID_FEATURES && reg == R_EAX) {
        /* kvm_pv_unhalt is reported by GET_SUPPORTED_CPUID, but it can't
         * be enabled without the in-kernel irqchip
         */
        if (!kvm_irqchip_in_kernel()) {
            ret &= ~(1U << KVM_FEATURE_PV_UNHALT);
        }
        if (kvm_irqchip_is_split()) {
            ret |= 1U << KVM_FEATURE_MSI_EXT_DEST_ID;
        }
    } else if (function == KVM_CPUID_FEATURES && reg == R_EDX) {
        ret |= 1U << KVM_HINTS_REALTIME;
    }

    if (current_machine->cgs) {
        ret = x86_confidential_guest_mask_cpuid_features(
            X86_CONFIDENTIAL_GUEST(current_machine->cgs),
            function, index, reg, ret);
    }
    return ret;
}

uint64_t kvm_arch_get_supported_msr_feature(KVMState *s, uint32_t index)
{
    struct {
        struct kvm_msrs info;
        struct kvm_msr_entry entries[1];
    } msr_data = {};
    uint64_t value;
    uint32_t ret, can_be_one, must_be_one;

    if (kvm_feature_msrs == NULL) { /* Host doesn't support feature MSRs */
        return 0;
    }

    /* Check if requested MSR is supported feature MSR */
    int i;
    for (i = 0; i < kvm_feature_msrs->nmsrs; i++)
        if (kvm_feature_msrs->indices[i] == index) {
            break;
        }
    if (i == kvm_feature_msrs->nmsrs) {
        return 0; /* if the feature MSR is not supported, simply return 0 */
    }

    msr_data.info.nmsrs = 1;
    msr_data.entries[0].index = index;

    ret = kvm_ioctl(s, KVM_GET_MSRS, &msr_data);
    if (ret != 1) {
        error_report("KVM get MSR (index=0x%x) feature failed, %s",
            index, strerror(-ret));
        exit(1);
    }

    value = msr_data.entries[0].data;
    switch (index) {
    case MSR_IA32_VMX_PROCBASED_CTLS2:
        if (!has_msr_vmx_procbased_ctls2) {
            /* KVM forgot to add these bits for some time, do this ourselves. */
            if (kvm_arch_get_supported_cpuid(s, 0xD, 1, R_ECX) &
                CPUID_XSAVE_XSAVES) {
                value |= (uint64_t)VMX_SECONDARY_EXEC_XSAVES << 32;
            }
            if (kvm_arch_get_supported_cpuid(s, 1, 0, R_ECX) &
                CPUID_EXT_RDRAND) {
                value |= (uint64_t)VMX_SECONDARY_EXEC_RDRAND_EXITING << 32;
            }
            if (kvm_arch_get_supported_cpuid(s, 7, 0, R_EBX) &
                CPUID_7_0_EBX_INVPCID) {
                value |= (uint64_t)VMX_SECONDARY_EXEC_ENABLE_INVPCID << 32;
            }
            if (kvm_arch_get_supported_cpuid(s, 7, 0, R_EBX) &
                CPUID_7_0_EBX_RDSEED) {
                value |= (uint64_t)VMX_SECONDARY_EXEC_RDSEED_EXITING << 32;
            }
            if (kvm_arch_get_supported_cpuid(s, 0x80000001, 0, R_EDX) &
                CPUID_EXT2_RDTSCP) {
                value |= (uint64_t)VMX_SECONDARY_EXEC_RDTSCP << 32;
            }
        }
        /* fall through */
    case MSR_IA32_VMX_TRUE_PINBASED_CTLS:
    case MSR_IA32_VMX_TRUE_PROCBASED_CTLS:
    case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
    case MSR_IA32_VMX_TRUE_EXIT_CTLS:
        /*
         * Return true for bits that can be one, but do not have to be one.
         * The SDM tells us which bits could have a "must be one" setting,
         * so we can do the opposite transformation in make_vmx_msr_value.
         */
        must_be_one = (uint32_t)value;
        can_be_one = (uint32_t)(value >> 32);
        return can_be_one & ~must_be_one;

    default:
        return value;
    }
}

static int kvm_get_mce_cap_supported(KVMState *s, uint64_t *mce_cap,
                                     int *max_banks)
{
    *max_banks = kvm_check_extension(s, KVM_CAP_MCE);
    return kvm_ioctl(s, KVM_X86_GET_MCE_CAP_SUPPORTED, mce_cap);
}

static void kvm_mce_inject(X86CPU *cpu, hwaddr paddr, int code)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;
    uint64_t status = MCI_STATUS_VAL | MCI_STATUS_EN | MCI_STATUS_MISCV |
                      MCI_STATUS_ADDRV;
    uint64_t mcg_status = MCG_STATUS_MCIP | MCG_STATUS_RIPV;
    int flags = 0;

    if (!IS_AMD_CPU(env)) {
        status |= MCI_STATUS_S | MCI_STATUS_UC;
        if (code == BUS_MCEERR_AR) {
            status |= MCI_STATUS_AR | 0x134;
            mcg_status |= MCG_STATUS_EIPV;
        } else {
            status |= 0xc0;
        }
    } else {
        if (code == BUS_MCEERR_AR) {
            status |= MCI_STATUS_UC | MCI_STATUS_POISON;
            mcg_status |= MCG_STATUS_EIPV;
        } else {
            /* Setting the POISON bit for deferred errors indicates to the
             * guest kernel that the address provided by the MCE is valid
             * and usable which will ensure that the guest kernel will send
             * a SIGBUS_AO signal to the guest process. This allows for
             * more desirable behavior in the case that the guest process
             * with poisoned memory has set the MCE_KILL_EARLY prctl flag
             * which indicates that the process would prefer to handle or
             * shutdown due to the poisoned memory condition before the
             * memory has been accessed.
             *
             * While the POISON bit would not be set in a deferred error
             * sent from hardware, the bit is not meaningful for deferred
             * errors and can be reused in this scenario.
             */
            status |= MCI_STATUS_DEFERRED | MCI_STATUS_POISON;
        }
    }

    flags = cpu_x86_support_mca_broadcast(env) ? MCE_INJECT_BROADCAST : 0;
    /* We need to read back the value of MSR_EXT_MCG_CTL that was set by the
     * guest kernel back into env->mcg_ext_ctl.
     */
    cpu_synchronize_state(cs);
    if (env->mcg_ext_ctl & MCG_EXT_CTL_LMCE_EN) {
        mcg_status |= MCG_STATUS_LMCE;
        flags = 0;
    }

    cpu_x86_inject_mce(NULL, cpu, 9, status, mcg_status, paddr,
                       (MCM_ADDR_PHYS << 6) | 0xc, flags);
}

static void emit_hypervisor_memory_failure(MemoryFailureAction action, bool ar)
{
    MemoryFailureFlags mff = {.action_required = ar, .recursive = false};

    qapi_event_send_memory_failure(MEMORY_FAILURE_RECIPIENT_HYPERVISOR, action,
                                   &mff);
}

static void hardware_memory_error(void *host_addr)
{
    emit_hypervisor_memory_failure(MEMORY_FAILURE_ACTION_FATAL, true);
    error_report("QEMU got Hardware memory error at addr %p", host_addr);
    exit(1);
}

void kvm_arch_on_sigbus_vcpu(CPUState *c, int code, void *addr)
{
    X86CPU *cpu = X86_CPU(c);
    CPUX86State *env = &cpu->env;
    ram_addr_t ram_addr;
    hwaddr paddr;

    /* If we get an action required MCE, it has been injected by KVM
     * while the VM was running.  An action optional MCE instead should
     * be coming from the main thread, which qemu_init_sigbus identifies
     * as the "early kill" thread.
     */
    assert(code == BUS_MCEERR_AR || code == BUS_MCEERR_AO);

    if ((env->mcg_cap & MCG_SER_P) && addr) {
        ram_addr = qemu_ram_addr_from_host(addr);
        if (ram_addr != RAM_ADDR_INVALID &&
            kvm_physical_memory_addr_from_host(c->kvm_state, addr, &paddr)) {
            kvm_hwpoison_page_add(ram_addr);
            kvm_mce_inject(cpu, paddr, code);

            /*
             * Use different logging severity based on error type.
             * If there is additional MCE reporting on the hypervisor, QEMU VA
             * could be another source to identify the PA and MCE details.
             */
            if (code == BUS_MCEERR_AR) {
                error_report("Guest MCE Memory Error at QEMU addr %p and "
                    "GUEST addr 0x%" HWADDR_PRIx " of type %s injected",
                    addr, paddr, "BUS_MCEERR_AR");
            } else {
                 warn_report("Guest MCE Memory Error at QEMU addr %p and "
                     "GUEST addr 0x%" HWADDR_PRIx " of type %s injected",
                     addr, paddr, "BUS_MCEERR_AO");
            }

            return;
        }

        if (code == BUS_MCEERR_AO) {
            warn_report("Hardware memory error at addr %p of type %s "
                "for memory used by QEMU itself instead of guest system!",
                 addr, "BUS_MCEERR_AO");
        }
    }

    if (code == BUS_MCEERR_AR) {
        hardware_memory_error(addr);
    }

    /* Hope we are lucky for AO MCE, just notify a event */
    emit_hypervisor_memory_failure(MEMORY_FAILURE_ACTION_IGNORE, false);
}

static void kvm_queue_exception(CPUX86State *env,
                                int32_t exception_nr,
                                uint8_t exception_has_payload,
                                uint64_t exception_payload)
{
    assert(env->exception_nr == -1);
    assert(!env->exception_pending);
    assert(!env->exception_injected);
    assert(!env->exception_has_payload);

    env->exception_nr = exception_nr;

    if (has_exception_payload) {
        env->exception_pending = 1;

        env->exception_has_payload = exception_has_payload;
        env->exception_payload = exception_payload;
    } else {
        env->exception_injected = 1;

        if (exception_nr == EXCP01_DB) {
            assert(exception_has_payload);
            env->dr[6] = exception_payload;
        } else if (exception_nr == EXCP0E_PAGE) {
            assert(exception_has_payload);
            env->cr[2] = exception_payload;
        } else {
            assert(!exception_has_payload);
        }
    }
}

static void cpu_update_state(void *opaque, bool running, RunState state)
{
    CPUX86State *env = opaque;

    if (running) {
        env->tsc_valid = false;
    }
}

unsigned long kvm_arch_vcpu_id(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    return cpu->apic_id;
}

#ifndef KVM_CPUID_SIGNATURE_NEXT
#define KVM_CPUID_SIGNATURE_NEXT                0x40000100
#endif

static bool hyperv_enabled(X86CPU *cpu)
{
    return kvm_check_extension(kvm_state, KVM_CAP_HYPERV) > 0 &&
        ((cpu->hyperv_spinlock_attempts != HYPERV_SPINLOCK_NEVER_NOTIFY) ||
         cpu->hyperv_features || cpu->hyperv_passthrough);
}

/*
 * Check whether target_freq is within conservative
 * ntp correctable bounds (250ppm) of freq
 */
static inline bool freq_within_bounds(int freq, int target_freq)
{
        int max_freq = freq + (freq * 250 / 1000000);
        int min_freq = freq - (freq * 250 / 1000000);

        if (target_freq >= min_freq && target_freq <= max_freq) {
                return true;
        }

        return false;
}

static int kvm_arch_set_tsc_khz(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int r, cur_freq;
    bool set_ioctl = false;

    if (!env->tsc_khz) {
        return 0;
    }

    cur_freq = kvm_check_extension(cs->kvm_state, KVM_CAP_GET_TSC_KHZ) ?
               kvm_vcpu_ioctl(cs, KVM_GET_TSC_KHZ) : -ENOTSUP;

    /*
     * If TSC scaling is supported, attempt to set TSC frequency.
     */
    if (kvm_check_extension(cs->kvm_state, KVM_CAP_TSC_CONTROL)) {
        set_ioctl = true;
    }

    /*
     * If desired TSC frequency is within bounds of NTP correction,
     * attempt to set TSC frequency.
     */
    if (cur_freq != -ENOTSUP && freq_within_bounds(cur_freq, env->tsc_khz)) {
        set_ioctl = true;
    }

    r = set_ioctl ?
        kvm_vcpu_ioctl(cs, KVM_SET_TSC_KHZ, env->tsc_khz) :
        -ENOTSUP;

    if (r < 0) {
        /* When KVM_SET_TSC_KHZ fails, it's an error only if the current
         * TSC frequency doesn't match the one we want.
         */
        cur_freq = kvm_check_extension(cs->kvm_state, KVM_CAP_GET_TSC_KHZ) ?
                   kvm_vcpu_ioctl(cs, KVM_GET_TSC_KHZ) :
                   -ENOTSUP;
        if (cur_freq <= 0 || cur_freq != env->tsc_khz) {
            warn_report("TSC frequency mismatch between "
                        "VM (%" PRId64 " kHz) and host (%d kHz), "
                        "and TSC scaling unavailable",
                        env->tsc_khz, cur_freq);
            return r;
        }
    }

    return 0;
}

static bool tsc_is_stable_and_known(CPUX86State *env)
{
    if (!env->tsc_khz) {
        return false;
    }
    return (env->features[FEAT_8000_0007_EDX] & CPUID_APM_INVTSC)
        || env->user_tsc_khz;
}

#define DEFAULT_EVMCS_VERSION ((1 << 8) | 1)

static struct {
    const char *desc;
    struct {
        uint32_t func;
        int reg;
        uint32_t bits;
    } flags[2];
    uint64_t dependencies;
} kvm_hyperv_properties[] = {
    [HYPERV_FEAT_RELAXED] = {
        .desc = "relaxed timing (hv-relaxed)",
        .flags = {
            {.func = HV_CPUID_ENLIGHTMENT_INFO, .reg = R_EAX,
             .bits = HV_RELAXED_TIMING_RECOMMENDED}
        }
    },
    [HYPERV_FEAT_VAPIC] = {
        .desc = "virtual APIC (hv-vapic)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_APIC_ACCESS_AVAILABLE}
        }
    },
    [HYPERV_FEAT_TIME] = {
        .desc = "clocksources (hv-time)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_TIME_REF_COUNT_AVAILABLE | HV_REFERENCE_TSC_AVAILABLE}
        }
    },
    [HYPERV_FEAT_CRASH] = {
        .desc = "crash MSRs (hv-crash)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EDX,
             .bits = HV_GUEST_CRASH_MSR_AVAILABLE}
        }
    },
    [HYPERV_FEAT_RESET] = {
        .desc = "reset MSR (hv-reset)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_RESET_AVAILABLE}
        }
    },
    [HYPERV_FEAT_VPINDEX] = {
        .desc = "VP_INDEX MSR (hv-vpindex)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_VP_INDEX_AVAILABLE}
        }
    },
    [HYPERV_FEAT_RUNTIME] = {
        .desc = "VP_RUNTIME MSR (hv-runtime)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_VP_RUNTIME_AVAILABLE}
        }
    },
    [HYPERV_FEAT_SYNIC] = {
        .desc = "synthetic interrupt controller (hv-synic)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_SYNIC_AVAILABLE}
        }
    },
    [HYPERV_FEAT_STIMER] = {
        .desc = "synthetic timers (hv-stimer)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_SYNTIMERS_AVAILABLE}
        },
        .dependencies = BIT(HYPERV_FEAT_SYNIC) | BIT(HYPERV_FEAT_TIME)
    },
    [HYPERV_FEAT_FREQUENCIES] = {
        .desc = "frequency MSRs (hv-frequencies)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_ACCESS_FREQUENCY_MSRS},
            {.func = HV_CPUID_FEATURES, .reg = R_EDX,
             .bits = HV_FREQUENCY_MSRS_AVAILABLE}
        }
    },
    [HYPERV_FEAT_REENLIGHTENMENT] = {
        .desc = "reenlightenment MSRs (hv-reenlightenment)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EAX,
             .bits = HV_ACCESS_REENLIGHTENMENTS_CONTROL}
        }
    },
    [HYPERV_FEAT_TLBFLUSH] = {
        .desc = "paravirtualized TLB flush (hv-tlbflush)",
        .flags = {
            {.func = HV_CPUID_ENLIGHTMENT_INFO, .reg = R_EAX,
             .bits = HV_REMOTE_TLB_FLUSH_RECOMMENDED |
             HV_EX_PROCESSOR_MASKS_RECOMMENDED}
        },
        .dependencies = BIT(HYPERV_FEAT_VPINDEX)
    },
    [HYPERV_FEAT_EVMCS] = {
        .desc = "enlightened VMCS (hv-evmcs)",
        .flags = {
            {.func = HV_CPUID_ENLIGHTMENT_INFO, .reg = R_EAX,
             .bits = HV_ENLIGHTENED_VMCS_RECOMMENDED}
        },
        .dependencies = BIT(HYPERV_FEAT_VAPIC)
    },
    [HYPERV_FEAT_IPI] = {
        .desc = "paravirtualized IPI (hv-ipi)",
        .flags = {
            {.func = HV_CPUID_ENLIGHTMENT_INFO, .reg = R_EAX,
             .bits = HV_CLUSTER_IPI_RECOMMENDED |
             HV_EX_PROCESSOR_MASKS_RECOMMENDED}
        },
        .dependencies = BIT(HYPERV_FEAT_VPINDEX)
    },
    [HYPERV_FEAT_STIMER_DIRECT] = {
        .desc = "direct mode synthetic timers (hv-stimer-direct)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EDX,
             .bits = HV_STIMER_DIRECT_MODE_AVAILABLE}
        },
        .dependencies = BIT(HYPERV_FEAT_STIMER)
    },
    [HYPERV_FEAT_AVIC] = {
        .desc = "AVIC/APICv support (hv-avic/hv-apicv)",
        .flags = {
            {.func = HV_CPUID_ENLIGHTMENT_INFO, .reg = R_EAX,
             .bits = HV_DEPRECATING_AEOI_RECOMMENDED}
        }
    },
#ifdef CONFIG_SYNDBG
    [HYPERV_FEAT_SYNDBG] = {
        .desc = "Enable synthetic kernel debugger channel (hv-syndbg)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EDX,
             .bits = HV_FEATURE_DEBUG_MSRS_AVAILABLE}
        },
        .dependencies = BIT(HYPERV_FEAT_SYNIC) | BIT(HYPERV_FEAT_RELAXED)
    },
#endif
    [HYPERV_FEAT_MSR_BITMAP] = {
        .desc = "enlightened MSR-Bitmap (hv-emsr-bitmap)",
        .flags = {
            {.func = HV_CPUID_NESTED_FEATURES, .reg = R_EAX,
             .bits = HV_NESTED_MSR_BITMAP}
        }
    },
    [HYPERV_FEAT_XMM_INPUT] = {
        .desc = "XMM fast hypercall input (hv-xmm-input)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EDX,
             .bits = HV_HYPERCALL_XMM_INPUT_AVAILABLE}
        }
    },
    [HYPERV_FEAT_TLBFLUSH_EXT] = {
        .desc = "Extended gva ranges for TLB flush hypercalls (hv-tlbflush-ext)",
        .flags = {
            {.func = HV_CPUID_FEATURES, .reg = R_EDX,
             .bits = HV_EXT_GVA_RANGES_FLUSH_AVAILABLE}
        },
        .dependencies = BIT(HYPERV_FEAT_TLBFLUSH)
    },
    [HYPERV_FEAT_TLBFLUSH_DIRECT] = {
        .desc = "direct TLB flush (hv-tlbflush-direct)",
        .flags = {
            {.func = HV_CPUID_NESTED_FEATURES, .reg = R_EAX,
             .bits = HV_NESTED_DIRECT_FLUSH}
        },
        .dependencies = BIT(HYPERV_FEAT_VAPIC)
    },
};

static struct kvm_cpuid2 *try_get_hv_cpuid(CPUState *cs, int max,
                                           bool do_sys_ioctl)
{
    struct kvm_cpuid2 *cpuid;
    int r, size;

    size = sizeof(*cpuid) + max * sizeof(*cpuid->entries);
    cpuid = g_malloc0(size);
    cpuid->nent = max;

    if (do_sys_ioctl) {
        r = kvm_ioctl(kvm_state, KVM_GET_SUPPORTED_HV_CPUID, cpuid);
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_GET_SUPPORTED_HV_CPUID, cpuid);
    }
    if (r == 0 && cpuid->nent >= max) {
        r = -E2BIG;
    }
    if (r < 0) {
        if (r == -E2BIG) {
            g_free(cpuid);
            return NULL;
        } else {
            fprintf(stderr, "KVM_GET_SUPPORTED_HV_CPUID failed: %s\n",
                    strerror(-r));
            exit(1);
        }
    }
    return cpuid;
}

/*
 * Run KVM_GET_SUPPORTED_HV_CPUID ioctl(), allocating a buffer large enough
 * for all entries.
 */
static struct kvm_cpuid2 *get_supported_hv_cpuid(CPUState *cs)
{
    struct kvm_cpuid2 *cpuid;
    /* 0x40000000..0x40000005, 0x4000000A, 0x40000080..0x40000082 leaves */
    int max = 11;
    int i;
    bool do_sys_ioctl;

    do_sys_ioctl =
        kvm_check_extension(kvm_state, KVM_CAP_SYS_HYPERV_CPUID) > 0;

    /*
     * Non-empty KVM context is needed when KVM_CAP_SYS_HYPERV_CPUID is
     * unsupported, kvm_hyperv_expand_features() checks for that.
     */
    assert(do_sys_ioctl || cs->kvm_state);

    /*
     * When the buffer is too small, KVM_GET_SUPPORTED_HV_CPUID fails with
     * -E2BIG, however, it doesn't report back the right size. Keep increasing
     * it and re-trying until we succeed.
     */
    while ((cpuid = try_get_hv_cpuid(cs, max, do_sys_ioctl)) == NULL) {
        max++;
    }

    /*
     * KVM_GET_SUPPORTED_HV_CPUID does not set EVMCS CPUID bit before
     * KVM_CAP_HYPERV_ENLIGHTENED_VMCS is enabled but we want to get the
     * information early, just check for the capability and set the bit
     * manually.
     */
    if (!do_sys_ioctl && kvm_check_extension(cs->kvm_state,
                            KVM_CAP_HYPERV_ENLIGHTENED_VMCS) > 0) {
        for (i = 0; i < cpuid->nent; i++) {
            if (cpuid->entries[i].function == HV_CPUID_ENLIGHTMENT_INFO) {
                cpuid->entries[i].eax |= HV_ENLIGHTENED_VMCS_RECOMMENDED;
            }
        }
    }

    return cpuid;
}

/*
 * When KVM_GET_SUPPORTED_HV_CPUID is not supported we fill CPUID feature
 * leaves from KVM_CAP_HYPERV* and present MSRs data.
 */
static struct kvm_cpuid2 *get_supported_hv_cpuid_legacy(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    struct kvm_cpuid2 *cpuid;
    struct kvm_cpuid_entry2 *entry_feat, *entry_recomm;

    /* HV_CPUID_FEATURES, HV_CPUID_ENLIGHTMENT_INFO */
    cpuid = g_malloc0(sizeof(*cpuid) + 2 * sizeof(*cpuid->entries));
    cpuid->nent = 2;

    /* HV_CPUID_VENDOR_AND_MAX_FUNCTIONS */
    entry_feat = &cpuid->entries[0];
    entry_feat->function = HV_CPUID_FEATURES;

    entry_recomm = &cpuid->entries[1];
    entry_recomm->function = HV_CPUID_ENLIGHTMENT_INFO;
    entry_recomm->ebx = cpu->hyperv_spinlock_attempts;

    if (kvm_check_extension(cs->kvm_state, KVM_CAP_HYPERV) > 0) {
        entry_feat->eax |= HV_HYPERCALL_AVAILABLE;
        entry_feat->eax |= HV_APIC_ACCESS_AVAILABLE;
        entry_feat->edx |= HV_CPU_DYNAMIC_PARTITIONING_AVAILABLE;
        entry_recomm->eax |= HV_RELAXED_TIMING_RECOMMENDED;
        entry_recomm->eax |= HV_APIC_ACCESS_RECOMMENDED;
    }

    if (kvm_check_extension(cs->kvm_state, KVM_CAP_HYPERV_TIME) > 0) {
        entry_feat->eax |= HV_TIME_REF_COUNT_AVAILABLE;
        entry_feat->eax |= HV_REFERENCE_TSC_AVAILABLE;
    }

    if (has_msr_hv_frequencies) {
        entry_feat->eax |= HV_ACCESS_FREQUENCY_MSRS;
        entry_feat->edx |= HV_FREQUENCY_MSRS_AVAILABLE;
    }

    if (has_msr_hv_crash) {
        entry_feat->edx |= HV_GUEST_CRASH_MSR_AVAILABLE;
    }

    if (has_msr_hv_reenlightenment) {
        entry_feat->eax |= HV_ACCESS_REENLIGHTENMENTS_CONTROL;
    }

    if (has_msr_hv_reset) {
        entry_feat->eax |= HV_RESET_AVAILABLE;
    }

    if (has_msr_hv_vpindex) {
        entry_feat->eax |= HV_VP_INDEX_AVAILABLE;
    }

    if (has_msr_hv_runtime) {
        entry_feat->eax |= HV_VP_RUNTIME_AVAILABLE;
    }

    if (has_msr_hv_synic) {
        unsigned int cap = cpu->hyperv_synic_kvm_only ?
            KVM_CAP_HYPERV_SYNIC : KVM_CAP_HYPERV_SYNIC2;

        if (kvm_check_extension(cs->kvm_state, cap) > 0) {
            entry_feat->eax |= HV_SYNIC_AVAILABLE;
        }
    }

    if (has_msr_hv_stimer) {
        entry_feat->eax |= HV_SYNTIMERS_AVAILABLE;
    }

    if (has_msr_hv_syndbg_options) {
        entry_feat->edx |= HV_GUEST_DEBUGGING_AVAILABLE;
        entry_feat->edx |= HV_FEATURE_DEBUG_MSRS_AVAILABLE;
        entry_feat->ebx |= HV_PARTITION_DEBUGGING_ALLOWED;
    }

    if (kvm_check_extension(cs->kvm_state,
                            KVM_CAP_HYPERV_TLBFLUSH) > 0) {
        entry_recomm->eax |= HV_REMOTE_TLB_FLUSH_RECOMMENDED;
        entry_recomm->eax |= HV_EX_PROCESSOR_MASKS_RECOMMENDED;
    }

    if (kvm_check_extension(cs->kvm_state,
                            KVM_CAP_HYPERV_ENLIGHTENED_VMCS) > 0) {
        entry_recomm->eax |= HV_ENLIGHTENED_VMCS_RECOMMENDED;
    }

    if (kvm_check_extension(cs->kvm_state,
                            KVM_CAP_HYPERV_SEND_IPI) > 0) {
        entry_recomm->eax |= HV_CLUSTER_IPI_RECOMMENDED;
        entry_recomm->eax |= HV_EX_PROCESSOR_MASKS_RECOMMENDED;
    }

    return cpuid;
}

static uint32_t hv_cpuid_get_host(CPUState *cs, uint32_t func, int reg)
{
    struct kvm_cpuid_entry2 *entry;
    struct kvm_cpuid2 *cpuid;

    if (hv_cpuid_cache) {
        cpuid = hv_cpuid_cache;
    } else {
        if (kvm_check_extension(kvm_state, KVM_CAP_HYPERV_CPUID) > 0) {
            cpuid = get_supported_hv_cpuid(cs);
        } else {
            /*
             * 'cs->kvm_state' may be NULL when Hyper-V features are expanded
             * before KVM context is created but this is only done when
             * KVM_CAP_SYS_HYPERV_CPUID is supported and it implies
             * KVM_CAP_HYPERV_CPUID.
             */
            assert(cs->kvm_state);

            cpuid = get_supported_hv_cpuid_legacy(cs);
        }
        hv_cpuid_cache = cpuid;
    }

    if (!cpuid) {
        return 0;
    }

    entry = cpuid_find_entry(cpuid, func, 0);
    if (!entry) {
        return 0;
    }

    return cpuid_entry_get_reg(entry, reg);
}

static bool hyperv_feature_supported(CPUState *cs, int feature)
{
    uint32_t func, bits;
    int i, reg;

    for (i = 0; i < ARRAY_SIZE(kvm_hyperv_properties[feature].flags); i++) {

        func = kvm_hyperv_properties[feature].flags[i].func;
        reg = kvm_hyperv_properties[feature].flags[i].reg;
        bits = kvm_hyperv_properties[feature].flags[i].bits;

        if (!func) {
            continue;
        }

        if ((hv_cpuid_get_host(cs, func, reg) & bits) != bits) {
            return false;
        }
    }

    return true;
}

/* Checks that all feature dependencies are enabled */
static bool hv_feature_check_deps(X86CPU *cpu, int feature, Error **errp)
{
    uint64_t deps;
    int dep_feat;

    deps = kvm_hyperv_properties[feature].dependencies;
    while (deps) {
        dep_feat = ctz64(deps);
        if (!(hyperv_feat_enabled(cpu, dep_feat))) {
            error_setg(errp, "Hyper-V %s requires Hyper-V %s",
                       kvm_hyperv_properties[feature].desc,
                       kvm_hyperv_properties[dep_feat].desc);
            return false;
        }
        deps &= ~(1ull << dep_feat);
    }

    return true;
}

static uint32_t hv_build_cpuid_leaf(CPUState *cs, uint32_t func, int reg)
{
    X86CPU *cpu = X86_CPU(cs);
    uint32_t r = 0;
    int i, j;

    for (i = 0; i < ARRAY_SIZE(kvm_hyperv_properties); i++) {
        if (!hyperv_feat_enabled(cpu, i)) {
            continue;
        }

        for (j = 0; j < ARRAY_SIZE(kvm_hyperv_properties[i].flags); j++) {
            if (kvm_hyperv_properties[i].flags[j].func != func) {
                continue;
            }
            if (kvm_hyperv_properties[i].flags[j].reg != reg) {
                continue;
            }

            r |= kvm_hyperv_properties[i].flags[j].bits;
        }
    }

    /* HV_CPUID_NESTED_FEATURES.EAX also encodes the supported eVMCS range */
    if (func == HV_CPUID_NESTED_FEATURES && reg == R_EAX) {
        if (hyperv_feat_enabled(cpu, HYPERV_FEAT_EVMCS)) {
            r |= DEFAULT_EVMCS_VERSION;
        }
    }

    return r;
}

/*
 * Expand Hyper-V CPU features. In partucular, check that all the requested
 * features are supported by the host and the sanity of the configuration
 * (that all the required dependencies are included). Also, this takes care
 * of 'hv_passthrough' mode and fills the environment with all supported
 * Hyper-V features.
 */
bool kvm_hyperv_expand_features(X86CPU *cpu, Error **errp)
{
    CPUState *cs = CPU(cpu);
    Error *local_err = NULL;
    int feat;

    if (!hyperv_enabled(cpu))
        return true;

    /*
     * When kvm_hyperv_expand_features is called at CPU feature expansion
     * time per-CPU kvm_state is not available yet so we can only proceed
     * when KVM_CAP_SYS_HYPERV_CPUID is supported.
     */
    if (!cs->kvm_state &&
        !kvm_check_extension(kvm_state, KVM_CAP_SYS_HYPERV_CPUID))
        return true;

    if (cpu->hyperv_passthrough) {
        cpu->hyperv_vendor_id[0] =
            hv_cpuid_get_host(cs, HV_CPUID_VENDOR_AND_MAX_FUNCTIONS, R_EBX);
        cpu->hyperv_vendor_id[1] =
            hv_cpuid_get_host(cs, HV_CPUID_VENDOR_AND_MAX_FUNCTIONS, R_ECX);
        cpu->hyperv_vendor_id[2] =
            hv_cpuid_get_host(cs, HV_CPUID_VENDOR_AND_MAX_FUNCTIONS, R_EDX);
        cpu->hyperv_vendor = g_realloc(cpu->hyperv_vendor,
                                       sizeof(cpu->hyperv_vendor_id) + 1);
        memcpy(cpu->hyperv_vendor, cpu->hyperv_vendor_id,
               sizeof(cpu->hyperv_vendor_id));
        cpu->hyperv_vendor[sizeof(cpu->hyperv_vendor_id)] = 0;

        cpu->hyperv_interface_id[0] =
            hv_cpuid_get_host(cs, HV_CPUID_INTERFACE, R_EAX);
        cpu->hyperv_interface_id[1] =
            hv_cpuid_get_host(cs, HV_CPUID_INTERFACE, R_EBX);
        cpu->hyperv_interface_id[2] =
            hv_cpuid_get_host(cs, HV_CPUID_INTERFACE, R_ECX);
        cpu->hyperv_interface_id[3] =
            hv_cpuid_get_host(cs, HV_CPUID_INTERFACE, R_EDX);

        cpu->hyperv_ver_id_build =
            hv_cpuid_get_host(cs, HV_CPUID_VERSION, R_EAX);
        cpu->hyperv_ver_id_major =
            hv_cpuid_get_host(cs, HV_CPUID_VERSION, R_EBX) >> 16;
        cpu->hyperv_ver_id_minor =
            hv_cpuid_get_host(cs, HV_CPUID_VERSION, R_EBX) & 0xffff;
        cpu->hyperv_ver_id_sp =
            hv_cpuid_get_host(cs, HV_CPUID_VERSION, R_ECX);
        cpu->hyperv_ver_id_sb =
            hv_cpuid_get_host(cs, HV_CPUID_VERSION, R_EDX) >> 24;
        cpu->hyperv_ver_id_sn =
            hv_cpuid_get_host(cs, HV_CPUID_VERSION, R_EDX) & 0xffffff;

        cpu->hv_max_vps = hv_cpuid_get_host(cs, HV_CPUID_IMPLEMENT_LIMITS,
                                            R_EAX);
        cpu->hyperv_limits[0] =
            hv_cpuid_get_host(cs, HV_CPUID_IMPLEMENT_LIMITS, R_EBX);
        cpu->hyperv_limits[1] =
            hv_cpuid_get_host(cs, HV_CPUID_IMPLEMENT_LIMITS, R_ECX);
        cpu->hyperv_limits[2] =
            hv_cpuid_get_host(cs, HV_CPUID_IMPLEMENT_LIMITS, R_EDX);

        cpu->hyperv_spinlock_attempts =
            hv_cpuid_get_host(cs, HV_CPUID_ENLIGHTMENT_INFO, R_EBX);

        /*
         * Mark feature as enabled in 'cpu->hyperv_features' as
         * hv_build_cpuid_leaf() uses this info to build guest CPUIDs.
         */
        for (feat = 0; feat < ARRAY_SIZE(kvm_hyperv_properties); feat++) {
            if (hyperv_feature_supported(cs, feat)) {
                cpu->hyperv_features |= BIT(feat);
            }
        }
    } else {
        /* Check features availability and dependencies */
        for (feat = 0; feat < ARRAY_SIZE(kvm_hyperv_properties); feat++) {
            /* If the feature was not requested skip it. */
            if (!hyperv_feat_enabled(cpu, feat)) {
                continue;
            }

            /* Check if the feature is supported by KVM */
            if (!hyperv_feature_supported(cs, feat)) {
                error_setg(errp, "Hyper-V %s is not supported by kernel",
                           kvm_hyperv_properties[feat].desc);
                return false;
            }

            /* Check dependencies */
            if (!hv_feature_check_deps(cpu, feat, &local_err)) {
                error_propagate(errp, local_err);
                return false;
            }
        }
    }

    /* Additional dependencies not covered by kvm_hyperv_properties[] */
    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC) &&
        !cpu->hyperv_synic_kvm_only &&
        !hyperv_feat_enabled(cpu, HYPERV_FEAT_VPINDEX)) {
        error_setg(errp, "Hyper-V %s requires Hyper-V %s",
                   kvm_hyperv_properties[HYPERV_FEAT_SYNIC].desc,
                   kvm_hyperv_properties[HYPERV_FEAT_VPINDEX].desc);
        return false;
    }

    return true;
}

/*
 * Fill in Hyper-V CPUIDs. Returns the number of entries filled in cpuid_ent.
 */
static int hyperv_fill_cpuids(CPUState *cs,
                              struct kvm_cpuid_entry2 *cpuid_ent)
{
    X86CPU *cpu = X86_CPU(cs);
    struct kvm_cpuid_entry2 *c;
    uint32_t signature[3];
    uint32_t cpuid_i = 0, max_cpuid_leaf = 0;
    uint32_t nested_eax =
        hv_build_cpuid_leaf(cs, HV_CPUID_NESTED_FEATURES, R_EAX);

    max_cpuid_leaf = nested_eax ? HV_CPUID_NESTED_FEATURES :
        HV_CPUID_IMPLEMENT_LIMITS;

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNDBG)) {
        max_cpuid_leaf =
            MAX(max_cpuid_leaf, HV_CPUID_SYNDBG_PLATFORM_CAPABILITIES);
    }

    c = &cpuid_ent[cpuid_i++];
    c->function = HV_CPUID_VENDOR_AND_MAX_FUNCTIONS;
    c->eax = max_cpuid_leaf;
    c->ebx = cpu->hyperv_vendor_id[0];
    c->ecx = cpu->hyperv_vendor_id[1];
    c->edx = cpu->hyperv_vendor_id[2];

    c = &cpuid_ent[cpuid_i++];
    c->function = HV_CPUID_INTERFACE;
    c->eax = cpu->hyperv_interface_id[0];
    c->ebx = cpu->hyperv_interface_id[1];
    c->ecx = cpu->hyperv_interface_id[2];
    c->edx = cpu->hyperv_interface_id[3];

    c = &cpuid_ent[cpuid_i++];
    c->function = HV_CPUID_VERSION;
    c->eax = cpu->hyperv_ver_id_build;
    c->ebx = (uint32_t)cpu->hyperv_ver_id_major << 16 |
        cpu->hyperv_ver_id_minor;
    c->ecx = cpu->hyperv_ver_id_sp;
    c->edx = (uint32_t)cpu->hyperv_ver_id_sb << 24 |
        (cpu->hyperv_ver_id_sn & 0xffffff);

    c = &cpuid_ent[cpuid_i++];
    c->function = HV_CPUID_FEATURES;
    c->eax = hv_build_cpuid_leaf(cs, HV_CPUID_FEATURES, R_EAX);
    c->ebx = hv_build_cpuid_leaf(cs, HV_CPUID_FEATURES, R_EBX);
    c->edx = hv_build_cpuid_leaf(cs, HV_CPUID_FEATURES, R_EDX);

    /* Unconditionally required with any Hyper-V enlightenment */
    c->eax |= HV_HYPERCALL_AVAILABLE;

    /* SynIC and Vmbus devices require messages/signals hypercalls */
    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC) &&
        !cpu->hyperv_synic_kvm_only) {
        c->ebx |= HV_POST_MESSAGES | HV_SIGNAL_EVENTS;
    }


    /* Not exposed by KVM but needed to make CPU hotplug in Windows work */
    c->edx |= HV_CPU_DYNAMIC_PARTITIONING_AVAILABLE;

    c = &cpuid_ent[cpuid_i++];
    c->function = HV_CPUID_ENLIGHTMENT_INFO;
    c->eax = hv_build_cpuid_leaf(cs, HV_CPUID_ENLIGHTMENT_INFO, R_EAX);
    c->ebx = cpu->hyperv_spinlock_attempts;

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_VAPIC) &&
        !hyperv_feat_enabled(cpu, HYPERV_FEAT_AVIC)) {
        c->eax |= HV_APIC_ACCESS_RECOMMENDED;
    }

    if (cpu->hyperv_no_nonarch_cs == ON_OFF_AUTO_ON) {
        c->eax |= HV_NO_NONARCH_CORESHARING;
    } else if (cpu->hyperv_no_nonarch_cs == ON_OFF_AUTO_AUTO) {
        c->eax |= hv_cpuid_get_host(cs, HV_CPUID_ENLIGHTMENT_INFO, R_EAX) &
            HV_NO_NONARCH_CORESHARING;
    }

    c = &cpuid_ent[cpuid_i++];
    c->function = HV_CPUID_IMPLEMENT_LIMITS;
    c->eax = cpu->hv_max_vps;
    c->ebx = cpu->hyperv_limits[0];
    c->ecx = cpu->hyperv_limits[1];
    c->edx = cpu->hyperv_limits[2];

    if (nested_eax) {
        uint32_t function;

        /* Create zeroed 0x40000006..0x40000009 leaves */
        for (function = HV_CPUID_IMPLEMENT_LIMITS + 1;
             function < HV_CPUID_NESTED_FEATURES; function++) {
            c = &cpuid_ent[cpuid_i++];
            c->function = function;
        }

        c = &cpuid_ent[cpuid_i++];
        c->function = HV_CPUID_NESTED_FEATURES;
        c->eax = nested_eax;
    }

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNDBG)) {
        c = &cpuid_ent[cpuid_i++];
        c->function = HV_CPUID_SYNDBG_VENDOR_AND_MAX_FUNCTIONS;
        c->eax = hyperv_feat_enabled(cpu, HYPERV_FEAT_EVMCS) ?
            HV_CPUID_NESTED_FEATURES : HV_CPUID_IMPLEMENT_LIMITS;
        memcpy(signature, "Microsoft VS", 12);
        c->eax = 0;
        c->ebx = signature[0];
        c->ecx = signature[1];
        c->edx = signature[2];

        c = &cpuid_ent[cpuid_i++];
        c->function = HV_CPUID_SYNDBG_INTERFACE;
        memcpy(signature, "VS#1\0\0\0\0\0\0\0\0", 12);
        c->eax = signature[0];
        c->ebx = 0;
        c->ecx = 0;
        c->edx = 0;

        c = &cpuid_ent[cpuid_i++];
        c->function = HV_CPUID_SYNDBG_PLATFORM_CAPABILITIES;
        c->eax = HV_SYNDBG_CAP_ALLOW_KERNEL_DEBUGGING;
        c->ebx = 0;
        c->ecx = 0;
        c->edx = 0;
    }

    return cpuid_i;
}

static Error *hv_passthrough_mig_blocker;
static Error *hv_no_nonarch_cs_mig_blocker;

/* Checks that the exposed eVMCS version range is supported by KVM */
static bool evmcs_version_supported(uint16_t evmcs_version,
                                    uint16_t supported_evmcs_version)
{
    uint8_t min_version = evmcs_version & 0xff;
    uint8_t max_version = evmcs_version >> 8;
    uint8_t min_supported_version = supported_evmcs_version & 0xff;
    uint8_t max_supported_version = supported_evmcs_version >> 8;

    return (min_version >= min_supported_version) &&
        (max_version <= max_supported_version);
}

static int hyperv_init_vcpu(X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    Error *local_err = NULL;
    int ret;

    if (cpu->hyperv_passthrough && hv_passthrough_mig_blocker == NULL) {
        error_setg(&hv_passthrough_mig_blocker,
                   "'hv-passthrough' CPU flag prevents migration, use explicit"
                   " set of hv-* flags instead");
        ret = migrate_add_blocker(&hv_passthrough_mig_blocker, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            return ret;
        }
    }

    if (cpu->hyperv_no_nonarch_cs == ON_OFF_AUTO_AUTO &&
        hv_no_nonarch_cs_mig_blocker == NULL) {
        error_setg(&hv_no_nonarch_cs_mig_blocker,
                   "'hv-no-nonarch-coresharing=auto' CPU flag prevents migration"
                   " use explicit 'hv-no-nonarch-coresharing=on' instead (but"
                   " make sure SMT is disabled and/or that vCPUs are properly"
                   " pinned)");
        ret = migrate_add_blocker(&hv_no_nonarch_cs_mig_blocker, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            return ret;
        }
    }

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_VPINDEX) && !hv_vpindex_settable) {
        /*
         * the kernel doesn't support setting vp_index; assert that its value
         * is in sync
         */
        uint64_t value;

        ret = kvm_get_one_msr(cpu, HV_X64_MSR_VP_INDEX, &value);
        if (ret < 0) {
            return ret;
        }

        if (value != hyperv_vp_index(CPU(cpu))) {
            error_report("kernel's vp_index != QEMU's vp_index");
            return -ENXIO;
        }
    }

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC)) {
        uint32_t synic_cap = cpu->hyperv_synic_kvm_only ?
            KVM_CAP_HYPERV_SYNIC : KVM_CAP_HYPERV_SYNIC2;
        ret = kvm_vcpu_enable_cap(cs, synic_cap, 0);
        if (ret < 0) {
            error_report("failed to turn on HyperV SynIC in KVM: %s",
                         strerror(-ret));
            return ret;
        }

        if (!cpu->hyperv_synic_kvm_only) {
            ret = hyperv_x86_synic_add(cpu);
            if (ret < 0) {
                error_report("failed to create HyperV SynIC: %s",
                             strerror(-ret));
                return ret;
            }
        }
    }

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_EVMCS)) {
        uint16_t evmcs_version = DEFAULT_EVMCS_VERSION;
        uint16_t supported_evmcs_version;

        ret = kvm_vcpu_enable_cap(cs, KVM_CAP_HYPERV_ENLIGHTENED_VMCS, 0,
                                  (uintptr_t)&supported_evmcs_version);

        /*
         * KVM is required to support EVMCS ver.1. as that's what 'hv-evmcs'
         * option sets. Note: we hardcode the maximum supported eVMCS version
         * to '1' as well so 'hv-evmcs' feature is migratable even when (and if)
         * ver.2 is implemented. A new option (e.g. 'hv-evmcs=2') will then have
         * to be added.
         */
        if (ret < 0) {
            error_report("Hyper-V %s is not supported by kernel",
                         kvm_hyperv_properties[HYPERV_FEAT_EVMCS].desc);
            return ret;
        }

        if (!evmcs_version_supported(evmcs_version, supported_evmcs_version)) {
            error_report("eVMCS version range [%d..%d] is not supported by "
                         "kernel (supported: [%d..%d])", evmcs_version & 0xff,
                         evmcs_version >> 8, supported_evmcs_version & 0xff,
                         supported_evmcs_version >> 8);
            return -ENOTSUP;
        }
    }

    if (cpu->hyperv_enforce_cpuid) {
        ret = kvm_vcpu_enable_cap(cs, KVM_CAP_HYPERV_ENFORCE_CPUID, 0, 1);
        if (ret < 0) {
            error_report("failed to enable KVM_CAP_HYPERV_ENFORCE_CPUID: %s",
                         strerror(-ret));
            return ret;
        }
    }

    /* Skip SynIC and VP_INDEX since they are hard deps already */
    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_STIMER) &&
        hyperv_feat_enabled(cpu, HYPERV_FEAT_VAPIC) &&
        hyperv_feat_enabled(cpu, HYPERV_FEAT_RUNTIME)) {
        hyperv_x86_set_vmbus_recommended_features_enabled();
    }

    return 0;
}

static Error *invtsc_mig_blocker;

#define KVM_MAX_CPUID_ENTRIES  100

static void kvm_init_xsave(CPUX86State *env)
{
    if (has_xsave2) {
        env->xsave_buf_len = QEMU_ALIGN_UP(has_xsave2, 4096);
    } else {
        env->xsave_buf_len = sizeof(struct kvm_xsave);
    }

    env->xsave_buf = qemu_memalign(4096, env->xsave_buf_len);
    memset(env->xsave_buf, 0, env->xsave_buf_len);
    /*
     * The allocated storage must be large enough for all of the
     * possible XSAVE state components.
     */
    assert(kvm_arch_get_supported_cpuid(kvm_state, 0xd, 0, R_ECX) <=
           env->xsave_buf_len);
}

static void kvm_init_nested_state(CPUX86State *env)
{
    struct kvm_vmx_nested_state_hdr *vmx_hdr;
    uint32_t size;

    if (!env->nested_state) {
        return;
    }

    size = env->nested_state->size;

    memset(env->nested_state, 0, size);
    env->nested_state->size = size;

    if (cpu_has_vmx(env)) {
        env->nested_state->format = KVM_STATE_NESTED_FORMAT_VMX;
        vmx_hdr = &env->nested_state->hdr.vmx;
        vmx_hdr->vmxon_pa = -1ull;
        vmx_hdr->vmcs12_pa = -1ull;
    } else if (cpu_has_svm(env)) {
        env->nested_state->format = KVM_STATE_NESTED_FORMAT_SVM;
    }
}

static uint32_t kvm_x86_build_cpuid(CPUX86State *env,
                                    struct kvm_cpuid_entry2 *entries,
                                    uint32_t cpuid_i)
{
    uint32_t limit, i, j;
    uint32_t unused;
    struct kvm_cpuid_entry2 *c;

    cpu_x86_cpuid(env, 0, 0, &limit, &unused, &unused, &unused);

    for (i = 0; i <= limit; i++) {
        j = 0;
        if (cpuid_i == KVM_MAX_CPUID_ENTRIES) {
            goto full;
        }
        c = &entries[cpuid_i++];
        switch (i) {
        case 2: {
            /* Keep reading function 2 till all the input is received */
            int times;

            c->function = i;
            c->flags = KVM_CPUID_FLAG_STATEFUL_FUNC |
                       KVM_CPUID_FLAG_STATE_READ_NEXT;
            cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
            times = c->eax & 0xff;

            for (j = 1; j < times; ++j) {
                if (cpuid_i == KVM_MAX_CPUID_ENTRIES) {
                    goto full;
                }
                c = &entries[cpuid_i++];
                c->function = i;
                c->flags = KVM_CPUID_FLAG_STATEFUL_FUNC;
                cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
            }
            break;
        }
        case 0x1f:
            if (!x86_has_extended_topo(env->avail_cpu_topo)) {
                cpuid_i--;
                break;
            }
            /* fallthrough */
        case 4:
        case 0xb:
        case 0xd:
            for (j = 0; ; j++) {
                if (i == 0xd && j == 64) {
                    break;
                }

                c->function = i;
                c->flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
                c->index = j;
                cpu_x86_cpuid(env, i, j, &c->eax, &c->ebx, &c->ecx, &c->edx);

                if (i == 4 && c->eax == 0) {
                    break;
                }
                if (i == 0xb && !(c->ecx & 0xff00)) {
                    break;
                }
                if (i == 0x1f && !(c->ecx & 0xff00)) {
                    break;
                }
                if (i == 0xd && c->eax == 0) {
                    continue;
                }
                if (cpuid_i == KVM_MAX_CPUID_ENTRIES) {
                    goto full;
                }
                c = &entries[cpuid_i++];
            }
            break;
        case 0x12:
            for (j = 0; ; j++) {
                c->function = i;
                c->flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
                c->index = j;
                cpu_x86_cpuid(env, i, j, &c->eax, &c->ebx, &c->ecx, &c->edx);

                if (j > 1 && (c->eax & 0xf) != 1) {
                    break;
                }

                if (cpuid_i == KVM_MAX_CPUID_ENTRIES) {
                    goto full;
                }
                c = &entries[cpuid_i++];
            }
            break;
        case 0x7:
        case 0x14:
        case 0x1d:
        case 0x1e: {
            uint32_t times;

            c->function = i;
            c->index = 0;
            c->flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
            cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
            times = c->eax;

            for (j = 1; j <= times; ++j) {
                if (cpuid_i == KVM_MAX_CPUID_ENTRIES) {
                    goto full;
                }
                c = &entries[cpuid_i++];
                c->function = i;
                c->index = j;
                c->flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
                cpu_x86_cpuid(env, i, j, &c->eax, &c->ebx, &c->ecx, &c->edx);
            }
            break;
        }
        default:
            c->function = i;
            c->flags = 0;
            cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
            if (!c->eax && !c->ebx && !c->ecx && !c->edx) {
                /*
                 * KVM already returns all zeroes if a CPUID entry is missing,
                 * so we can omit it and avoid hitting KVM's 80-entry limit.
                 */
                cpuid_i--;
            }
            break;
        }
    }

    if (limit >= 0x0a) {
        uint32_t eax, edx;

        cpu_x86_cpuid(env, 0x0a, 0, &eax, &unused, &unused, &edx);

        has_architectural_pmu_version = eax & 0xff;
        if (has_architectural_pmu_version > 0) {
            num_architectural_pmu_gp_counters = (eax & 0xff00) >> 8;

            /* Shouldn't be more than 32, since that's the number of bits
             * available in EBX to tell us _which_ counters are available.
             * Play it safe.
             */
            if (num_architectural_pmu_gp_counters > MAX_GP_COUNTERS) {
                num_architectural_pmu_gp_counters = MAX_GP_COUNTERS;
            }

            if (has_architectural_pmu_version > 1) {
                num_architectural_pmu_fixed_counters = edx & 0x1f;

                if (num_architectural_pmu_fixed_counters > MAX_FIXED_COUNTERS) {
                    num_architectural_pmu_fixed_counters = MAX_FIXED_COUNTERS;
                }
            }
        }
    }

    cpu_x86_cpuid(env, 0x80000000, 0, &limit, &unused, &unused, &unused);

    for (i = 0x80000000; i <= limit; i++) {
        j = 0;
        if (cpuid_i == KVM_MAX_CPUID_ENTRIES) {
            goto full;
        }
        c = &entries[cpuid_i++];

        switch (i) {
        case 0x8000001d:
            /* Query for all AMD cache information leaves */
            for (j = 0; ; j++) {
                c->function = i;
                c->flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
                c->index = j;
                cpu_x86_cpuid(env, i, j, &c->eax, &c->ebx, &c->ecx, &c->edx);

                if (c->eax == 0) {
                    break;
                }
                if (cpuid_i == KVM_MAX_CPUID_ENTRIES) {
                    goto full;
                }
                c = &entries[cpuid_i++];
            }
            break;
        default:
            c->function = i;
            c->flags = 0;
            cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
            if (!c->eax && !c->ebx && !c->ecx && !c->edx) {
                /*
                 * KVM already returns all zeroes if a CPUID entry is missing,
                 * so we can omit it and avoid hitting KVM's 80-entry limit.
                 */
                cpuid_i--;
            }
            break;
        }
    }

    /* Call Centaur's CPUID instructions they are supported. */
    if (env->cpuid_xlevel2 > 0) {
        cpu_x86_cpuid(env, 0xC0000000, 0, &limit, &unused, &unused, &unused);

        for (i = 0xC0000000; i <= limit; i++) {
            j = 0;
            if (cpuid_i == KVM_MAX_CPUID_ENTRIES) {
                goto full;
            }
            c = &entries[cpuid_i++];

            c->function = i;
            c->flags = 0;
            cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
        }
    }

    return cpuid_i;

full:
    fprintf(stderr, "cpuid_data is full, no space for "
            "cpuid(eax:0x%x,ecx:0x%x)\n", i, j);
    abort();
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    struct {
        struct kvm_cpuid2 cpuid;
        struct kvm_cpuid_entry2 entries[KVM_MAX_CPUID_ENTRIES];
    } cpuid_data;
    /*
     * The kernel defines these structs with padding fields so there
     * should be no extra padding in our cpuid_data struct.
     */
    QEMU_BUILD_BUG_ON(sizeof(cpuid_data) !=
                      sizeof(struct kvm_cpuid2) +
                      sizeof(struct kvm_cpuid_entry2) * KVM_MAX_CPUID_ENTRIES);

    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint32_t cpuid_i;
    struct kvm_cpuid_entry2 *c;
    uint32_t signature[3];
    int kvm_base = KVM_CPUID_SIGNATURE;
    int max_nested_state_len;
    int r;
    Error *local_err = NULL;

    memset(&cpuid_data, 0, sizeof(cpuid_data));

    cpuid_i = 0;

    has_xsave2 = kvm_check_extension(cs->kvm_state, KVM_CAP_XSAVE2);

    r = kvm_arch_set_tsc_khz(cs);
    if (r < 0) {
        return r;
    }

    /* vcpu's TSC frequency is either specified by user, or following
     * the value used by KVM if the former is not present. In the
     * latter case, we query it from KVM and record in env->tsc_khz,
     * so that vcpu's TSC frequency can be migrated later via this field.
     */
    if (!env->tsc_khz) {
        r = kvm_check_extension(cs->kvm_state, KVM_CAP_GET_TSC_KHZ) ?
            kvm_vcpu_ioctl(cs, KVM_GET_TSC_KHZ) :
            -ENOTSUP;
        if (r > 0) {
            env->tsc_khz = r;
        }
    }

    env->apic_bus_freq = KVM_APIC_BUS_FREQUENCY;

    /*
     * kvm_hyperv_expand_features() is called here for the second time in case
     * KVM_CAP_SYS_HYPERV_CPUID is not supported. While we can't possibly handle
     * 'query-cpu-model-expansion' in this case as we don't have a KVM vCPU to
     * check which Hyper-V enlightenments are supported and which are not, we
     * can still proceed and check/expand Hyper-V enlightenments here so legacy
     * behavior is preserved.
     */
    if (!kvm_hyperv_expand_features(cpu, &local_err)) {
        error_report_err(local_err);
        return -ENOSYS;
    }

    if (hyperv_enabled(cpu)) {
        r = hyperv_init_vcpu(cpu);
        if (r) {
            return r;
        }

        cpuid_i = hyperv_fill_cpuids(cs, cpuid_data.entries);
        kvm_base = KVM_CPUID_SIGNATURE_NEXT;
        has_msr_hv_hypercall = true;
    }

    if (cs->kvm_state->xen_version) {
#ifdef CONFIG_XEN_EMU
        struct kvm_cpuid_entry2 *xen_max_leaf;

        memcpy(signature, "XenVMMXenVMM", 12);

        xen_max_leaf = c = &cpuid_data.entries[cpuid_i++];
        c->function = kvm_base + XEN_CPUID_SIGNATURE;
        c->eax = kvm_base + XEN_CPUID_TIME;
        c->ebx = signature[0];
        c->ecx = signature[1];
        c->edx = signature[2];

        c = &cpuid_data.entries[cpuid_i++];
        c->function = kvm_base + XEN_CPUID_VENDOR;
        c->eax = cs->kvm_state->xen_version;
        c->ebx = 0;
        c->ecx = 0;
        c->edx = 0;

        c = &cpuid_data.entries[cpuid_i++];
        c->function = kvm_base + XEN_CPUID_HVM_MSR;
        /* Number of hypercall-transfer pages */
        c->eax = 1;
        /* Hypercall MSR base address */
        if (hyperv_enabled(cpu)) {
            c->ebx = XEN_HYPERCALL_MSR_HYPERV;
            kvm_xen_init(cs->kvm_state, c->ebx);
        } else {
            c->ebx = XEN_HYPERCALL_MSR;
        }
        c->ecx = 0;
        c->edx = 0;

        c = &cpuid_data.entries[cpuid_i++];
        c->function = kvm_base + XEN_CPUID_TIME;
        c->eax = ((!!tsc_is_stable_and_known(env) << 1) |
            (!!(env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_RDTSCP) << 2));
        /* default=0 (emulate if necessary) */
        c->ebx = 0;
        /* guest tsc frequency */
        c->ecx = env->user_tsc_khz;
        /* guest tsc incarnation (migration count) */
        c->edx = 0;

        c = &cpuid_data.entries[cpuid_i++];
        c->function = kvm_base + XEN_CPUID_HVM;
        xen_max_leaf->eax = kvm_base + XEN_CPUID_HVM;
        if (cs->kvm_state->xen_version >= XEN_VERSION(4, 5)) {
            c->function = kvm_base + XEN_CPUID_HVM;

            if (cpu->xen_vapic) {
                c->eax |= XEN_HVM_CPUID_APIC_ACCESS_VIRT;
                c->eax |= XEN_HVM_CPUID_X2APIC_VIRT;
            }

            c->eax |= XEN_HVM_CPUID_IOMMU_MAPPINGS;

            if (cs->kvm_state->xen_version >= XEN_VERSION(4, 6)) {
                c->eax |= XEN_HVM_CPUID_VCPU_ID_PRESENT;
                c->ebx = cs->cpu_index;
            }

            if (cs->kvm_state->xen_version >= XEN_VERSION(4, 17)) {
                c->eax |= XEN_HVM_CPUID_UPCALL_VECTOR;
            }
        }

        r = kvm_xen_init_vcpu(cs);
        if (r) {
            return r;
        }

        kvm_base += 0x100;
#else /* CONFIG_XEN_EMU */
        /* This should never happen as kvm_arch_init() would have died first. */
        fprintf(stderr, "Cannot enable Xen CPUID without Xen support\n");
        abort();
#endif
    } else if (cpu->expose_kvm) {
        memcpy(signature, "KVMKVMKVM\0\0\0", 12);
        c = &cpuid_data.entries[cpuid_i++];
        c->function = KVM_CPUID_SIGNATURE | kvm_base;
        c->eax = KVM_CPUID_FEATURES | kvm_base;
        c->ebx = signature[0];
        c->ecx = signature[1];
        c->edx = signature[2];

        c = &cpuid_data.entries[cpuid_i++];
        c->function = KVM_CPUID_FEATURES | kvm_base;
        c->eax = env->features[FEAT_KVM];
        c->edx = env->features[FEAT_KVM_HINTS];
    }

    if (cpu->kvm_pv_enforce_cpuid) {
        r = kvm_vcpu_enable_cap(cs, KVM_CAP_ENFORCE_PV_FEATURE_CPUID, 0, 1);
        if (r < 0) {
            fprintf(stderr,
                    "failed to enable KVM_CAP_ENFORCE_PV_FEATURE_CPUID: %s",
                    strerror(-r));
            abort();
        }
    }

    cpuid_i = kvm_x86_build_cpuid(env, cpuid_data.entries, cpuid_i);
    cpuid_data.cpuid.nent = cpuid_i;

    if (((env->cpuid_version >> 8)&0xF) >= 6
        && (env->features[FEAT_1_EDX] & (CPUID_MCE | CPUID_MCA)) ==
           (CPUID_MCE | CPUID_MCA)) {
        uint64_t mcg_cap, unsupported_caps;
        int banks;
        int ret;

        ret = kvm_get_mce_cap_supported(cs->kvm_state, &mcg_cap, &banks);
        if (ret < 0) {
            fprintf(stderr, "kvm_get_mce_cap_supported: %s", strerror(-ret));
            return ret;
        }

        if (banks < (env->mcg_cap & MCG_CAP_BANKS_MASK)) {
            error_report("kvm: Unsupported MCE bank count (QEMU = %d, KVM = %d)",
                         (int)(env->mcg_cap & MCG_CAP_BANKS_MASK), banks);
            return -ENOTSUP;
        }

        unsupported_caps = env->mcg_cap & ~(mcg_cap | MCG_CAP_BANKS_MASK);
        if (unsupported_caps) {
            if (unsupported_caps & MCG_LMCE_P) {
                error_report("kvm: LMCE not supported");
                return -ENOTSUP;
            }
            warn_report("Unsupported MCG_CAP bits: 0x%" PRIx64,
                        unsupported_caps);
        }

        env->mcg_cap &= mcg_cap | MCG_CAP_BANKS_MASK;
        ret = kvm_vcpu_ioctl(cs, KVM_X86_SETUP_MCE, &env->mcg_cap);
        if (ret < 0) {
            fprintf(stderr, "KVM_X86_SETUP_MCE: %s", strerror(-ret));
            return ret;
        }
    }

    cpu->vmsentry = qemu_add_vm_change_state_handler(cpu_update_state, env);

    c = cpuid_find_entry(&cpuid_data.cpuid, 1, 0);
    if (c) {
        has_msr_feature_control = !!(c->ecx & CPUID_EXT_VMX) ||
                                  !!(c->ecx & CPUID_EXT_SMX);
    }

    c = cpuid_find_entry(&cpuid_data.cpuid, 7, 0);
    if (c && (c->ebx & CPUID_7_0_EBX_SGX)) {
        has_msr_feature_control = true;
    }

    if (env->mcg_cap & MCG_LMCE_P) {
        has_msr_mcg_ext_ctl = has_msr_feature_control = true;
    }

    if (!env->user_tsc_khz) {
        if ((env->features[FEAT_8000_0007_EDX] & CPUID_APM_INVTSC) &&
            invtsc_mig_blocker == NULL) {
            error_setg(&invtsc_mig_blocker,
                       "State blocked by non-migratable CPU device"
                       " (invtsc flag)");
            r = migrate_add_blocker(&invtsc_mig_blocker, &local_err);
            if (r < 0) {
                error_report_err(local_err);
                return r;
            }
        }
    }

    if (cpu->vmware_cpuid_freq
        /* Guests depend on 0x40000000 to detect this feature, so only expose
         * it if KVM exposes leaf 0x40000000. (Conflicts with Hyper-V) */
        && cpu->expose_kvm
        && kvm_base == KVM_CPUID_SIGNATURE
        /* TSC clock must be stable and known for this feature. */
        && tsc_is_stable_and_known(env)) {

        c = &cpuid_data.entries[cpuid_i++];
        c->function = KVM_CPUID_SIGNATURE | 0x10;
        c->eax = env->tsc_khz;
        c->ebx = env->apic_bus_freq / 1000; /* Hz to KHz */
        c->ecx = c->edx = 0;

        c = cpuid_find_entry(&cpuid_data.cpuid, kvm_base, 0);
        c->eax = MAX(c->eax, KVM_CPUID_SIGNATURE | 0x10);
    }

    cpuid_data.cpuid.nent = cpuid_i;

    cpuid_data.cpuid.padding = 0;
    r = kvm_vcpu_ioctl(cs, KVM_SET_CPUID2, &cpuid_data);
    if (r) {
        goto fail;
    }
    kvm_init_xsave(env);

    max_nested_state_len = kvm_max_nested_state_length();
    if (max_nested_state_len > 0) {
        assert(max_nested_state_len >= offsetof(struct kvm_nested_state, data));

        if (cpu_has_vmx(env) || cpu_has_svm(env)) {
            env->nested_state = g_malloc0(max_nested_state_len);
            env->nested_state->size = max_nested_state_len;

            kvm_init_nested_state(env);
        }
    }

    cpu->kvm_msr_buf = g_malloc0(MSR_BUF_SIZE);

    if (!(env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_RDTSCP)) {
        has_msr_tsc_aux = false;
    }

    kvm_init_msrs(cpu);

    return 0;

 fail:
    migrate_del_blocker(&invtsc_mig_blocker);

    return r;
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    g_free(env->xsave_buf);

    g_free(cpu->kvm_msr_buf);
    cpu->kvm_msr_buf = NULL;

    g_free(env->nested_state);
    env->nested_state = NULL;

    qemu_del_vm_change_state_handler(cpu->vmsentry);

    return 0;
}

void kvm_arch_reset_vcpu(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    env->xcr0 = 1;
    if (kvm_irqchip_in_kernel()) {
        env->mp_state = cpu_is_bsp(cpu) ? KVM_MP_STATE_RUNNABLE :
                                          KVM_MP_STATE_UNINITIALIZED;
    } else {
        env->mp_state = KVM_MP_STATE_RUNNABLE;
    }

    /* enabled by default */
    env->poll_control_msr = 1;

    kvm_init_nested_state(env);

    sev_es_set_reset_vector(CPU(cpu));
}

void kvm_arch_after_reset_vcpu(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    int i;

    /*
     * Reset SynIC after all other devices have been reset to let them remove
     * their SINT routes first.
     */
    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC)) {
        for (i = 0; i < ARRAY_SIZE(env->msr_hv_synic_sint); i++) {
            env->msr_hv_synic_sint[i] = HV_SINT_MASKED;
        }

        hyperv_x86_synic_reset(cpu);
    }
}

void kvm_arch_do_init_vcpu(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    /* APs get directly into wait-for-SIPI state.  */
    if (env->mp_state == KVM_MP_STATE_UNINITIALIZED) {
        env->mp_state = KVM_MP_STATE_INIT_RECEIVED;
    }
}

static int kvm_get_supported_feature_msrs(KVMState *s)
{
    int ret = 0;

    if (kvm_feature_msrs != NULL) {
        return 0;
    }

    if (!kvm_check_extension(s, KVM_CAP_GET_MSR_FEATURES)) {
        return 0;
    }

    struct kvm_msr_list msr_list;

    msr_list.nmsrs = 0;
    ret = kvm_ioctl(s, KVM_GET_MSR_FEATURE_INDEX_LIST, &msr_list);
    if (ret < 0 && ret != -E2BIG) {
        error_report("Fetch KVM feature MSR list failed: %s",
            strerror(-ret));
        return ret;
    }

    assert(msr_list.nmsrs > 0);
    kvm_feature_msrs = g_malloc0(sizeof(msr_list) +
                 msr_list.nmsrs * sizeof(msr_list.indices[0]));

    kvm_feature_msrs->nmsrs = msr_list.nmsrs;
    ret = kvm_ioctl(s, KVM_GET_MSR_FEATURE_INDEX_LIST, kvm_feature_msrs);

    if (ret < 0) {
        error_report("Fetch KVM feature MSR list failed: %s",
            strerror(-ret));
        g_free(kvm_feature_msrs);
        kvm_feature_msrs = NULL;
        return ret;
    }

    return 0;
}

static int kvm_get_supported_msrs(KVMState *s)
{
    int ret = 0;
    struct kvm_msr_list msr_list, *kvm_msr_list;

    /*
     *  Obtain MSR list from KVM.  These are the MSRs that we must
     *  save/restore.
     */
    msr_list.nmsrs = 0;
    ret = kvm_ioctl(s, KVM_GET_MSR_INDEX_LIST, &msr_list);
    if (ret < 0 && ret != -E2BIG) {
        return ret;
    }
    /*
     * Old kernel modules had a bug and could write beyond the provided
     * memory. Allocate at least a safe amount of 1K.
     */
    kvm_msr_list = g_malloc0(MAX(1024, sizeof(msr_list) +
                                          msr_list.nmsrs *
                                          sizeof(msr_list.indices[0])));

    kvm_msr_list->nmsrs = msr_list.nmsrs;
    ret = kvm_ioctl(s, KVM_GET_MSR_INDEX_LIST, kvm_msr_list);
    if (ret >= 0) {
        int i;

        for (i = 0; i < kvm_msr_list->nmsrs; i++) {
            switch (kvm_msr_list->indices[i]) {
            case MSR_STAR:
                has_msr_star = true;
                break;
            case MSR_VM_HSAVE_PA:
                has_msr_hsave_pa = true;
                break;
            case MSR_TSC_AUX:
                has_msr_tsc_aux = true;
                break;
            case MSR_TSC_ADJUST:
                has_msr_tsc_adjust = true;
                break;
            case MSR_IA32_TSCDEADLINE:
                has_msr_tsc_deadline = true;
                break;
            case MSR_IA32_SMBASE:
                has_msr_smbase = true;
                break;
            case MSR_SMI_COUNT:
                has_msr_smi_count = true;
                break;
            case MSR_IA32_MISC_ENABLE:
                has_msr_misc_enable = true;
                break;
            case MSR_IA32_BNDCFGS:
                has_msr_bndcfgs = true;
                break;
            case MSR_IA32_XSS:
                has_msr_xss = true;
                break;
            case MSR_IA32_UMWAIT_CONTROL:
                has_msr_umwait = true;
                break;
            case HV_X64_MSR_CRASH_CTL:
                has_msr_hv_crash = true;
                break;
            case HV_X64_MSR_RESET:
                has_msr_hv_reset = true;
                break;
            case HV_X64_MSR_VP_INDEX:
                has_msr_hv_vpindex = true;
                break;
            case HV_X64_MSR_VP_RUNTIME:
                has_msr_hv_runtime = true;
                break;
            case HV_X64_MSR_SCONTROL:
                has_msr_hv_synic = true;
                break;
            case HV_X64_MSR_STIMER0_CONFIG:
                has_msr_hv_stimer = true;
                break;
            case HV_X64_MSR_TSC_FREQUENCY:
                has_msr_hv_frequencies = true;
                break;
            case HV_X64_MSR_REENLIGHTENMENT_CONTROL:
                has_msr_hv_reenlightenment = true;
                break;
            case HV_X64_MSR_SYNDBG_OPTIONS:
                has_msr_hv_syndbg_options = true;
                break;
            case MSR_IA32_SPEC_CTRL:
                has_msr_spec_ctrl = true;
                break;
            case MSR_AMD64_TSC_RATIO:
                has_tsc_scale_msr = true;
                break;
            case MSR_IA32_TSX_CTRL:
                has_msr_tsx_ctrl = true;
                break;
            case MSR_VIRT_SSBD:
                has_msr_virt_ssbd = true;
                break;
            case MSR_IA32_ARCH_CAPABILITIES:
                has_msr_arch_capabs = true;
                break;
            case MSR_IA32_CORE_CAPABILITY:
                has_msr_core_capabs = true;
                break;
            case MSR_IA32_PERF_CAPABILITIES:
                has_msr_perf_capabs = true;
                break;
            case MSR_IA32_VMX_VMFUNC:
                has_msr_vmx_vmfunc = true;
                break;
            case MSR_IA32_UCODE_REV:
                has_msr_ucode_rev = true;
                break;
            case MSR_IA32_VMX_PROCBASED_CTLS2:
                has_msr_vmx_procbased_ctls2 = true;
                break;
            case MSR_IA32_PKRS:
                has_msr_pkrs = true;
                break;
            }
        }
    }

    g_free(kvm_msr_list);

    return ret;
}

static bool kvm_rdmsr_core_thread_count(X86CPU *cpu,
                                        uint32_t msr,
                                        uint64_t *val)
{
    CPUState *cs = CPU(cpu);

    *val = cs->nr_threads * cs->nr_cores; /* thread count, bits 15..0 */
    *val |= ((uint32_t)cs->nr_cores << 16); /* core count, bits 31..16 */

    return true;
}

static bool kvm_rdmsr_rapl_power_unit(X86CPU *cpu,
                                      uint32_t msr,
                                      uint64_t *val)
{

    CPUState *cs = CPU(cpu);

    *val = cs->kvm_state->msr_energy.msr_unit;

    return true;
}

static bool kvm_rdmsr_pkg_power_limit(X86CPU *cpu,
                                      uint32_t msr,
                                      uint64_t *val)
{

    CPUState *cs = CPU(cpu);

    *val = cs->kvm_state->msr_energy.msr_limit;

    return true;
}

static bool kvm_rdmsr_pkg_power_info(X86CPU *cpu,
                                     uint32_t msr,
                                     uint64_t *val)
{

    CPUState *cs = CPU(cpu);

    *val = cs->kvm_state->msr_energy.msr_info;

    return true;
}

static bool kvm_rdmsr_pkg_energy_status(X86CPU *cpu,
                                        uint32_t msr,
                                        uint64_t *val)
{

    CPUState *cs = CPU(cpu);
    *val = cs->kvm_state->msr_energy.msr_value[cs->cpu_index];

    return true;
}

static Notifier smram_machine_done;
static KVMMemoryListener smram_listener;
static AddressSpace smram_address_space;
static MemoryRegion smram_as_root;
static MemoryRegion smram_as_mem;

static void register_smram_listener(Notifier *n, void *unused)
{
    MemoryRegion *smram =
        (MemoryRegion *) object_resolve_path("/machine/smram", NULL);

    /* Outer container... */
    memory_region_init(&smram_as_root, OBJECT(kvm_state), "mem-container-smram", ~0ull);
    memory_region_set_enabled(&smram_as_root, true);

    /* ... with two regions inside: normal system memory with low
     * priority, and...
     */
    memory_region_init_alias(&smram_as_mem, OBJECT(kvm_state), "mem-smram",
                             get_system_memory(), 0, ~0ull);
    memory_region_add_subregion_overlap(&smram_as_root, 0, &smram_as_mem, 0);
    memory_region_set_enabled(&smram_as_mem, true);

    if (smram) {
        /* ... SMRAM with higher priority */
        memory_region_add_subregion_overlap(&smram_as_root, 0, smram, 10);
        memory_region_set_enabled(smram, true);
    }

    address_space_init(&smram_address_space, &smram_as_root, "KVM-SMRAM");
    kvm_memory_listener_register(kvm_state, &smram_listener,
                                 &smram_address_space, 1, "kvm-smram");
}

static void *kvm_msr_energy_thread(void *data)
{
    KVMState *s = data;
    struct KVMMsrEnergy *vmsr = &s->msr_energy;

    g_autofree vmsr_package_energy_stat *pkg_stat = NULL;
    g_autofree vmsr_thread_stat *thd_stat = NULL;
    g_autofree CPUState *cpu = NULL;
    g_autofree unsigned int *vpkgs_energy_stat = NULL;
    unsigned int num_threads = 0;

    X86CPUTopoIDs topo_ids;

    rcu_register_thread();

    /* Allocate memory for each package energy status */
    pkg_stat = g_new0(vmsr_package_energy_stat, vmsr->host_topo.maxpkgs);

    /* Allocate memory for thread stats */
    thd_stat = g_new0(vmsr_thread_stat, 1);

    /* Allocate memory for holding virtual package energy counter */
    vpkgs_energy_stat = g_new0(unsigned int, vmsr->guest_vsockets);

    /* Populate the max tick of each packages */
    for (int i = 0; i < vmsr->host_topo.maxpkgs; i++) {
        /*
         * Max numbers of ticks per package
         * Time in second * Number of ticks/second * Number of cores/package
         * ex: 100 ticks/second/CPU, 12 CPUs per Package gives 1200 ticks max
         */
        vmsr->host_topo.maxticks[i] = (MSR_ENERGY_THREAD_SLEEP_US / 1000000)
                        * sysconf(_SC_CLK_TCK)
                        * vmsr->host_topo.pkg_cpu_count[i];
    }

    while (true) {
        /* Get all qemu threads id */
        g_autofree pid_t *thread_ids
            = vmsr_get_thread_ids(vmsr->pid, &num_threads);

        if (thread_ids == NULL) {
            goto clean;
        }

        thd_stat = g_renew(vmsr_thread_stat, thd_stat, num_threads);
        /* Unlike g_new0, g_renew0 function doesn't exist yet... */
        memset(thd_stat, 0, num_threads * sizeof(vmsr_thread_stat));

        /* Populate all the thread stats */
        for (int i = 0; i < num_threads; i++) {
            thd_stat[i].utime = g_new0(unsigned long long, 2);
            thd_stat[i].stime = g_new0(unsigned long long, 2);
            thd_stat[i].thread_id = thread_ids[i];
            vmsr_read_thread_stat(vmsr->pid,
                                  thd_stat[i].thread_id,
                                  &thd_stat[i].utime[0],
                                  &thd_stat[i].stime[0],
                                  &thd_stat[i].cpu_id);
            thd_stat[i].pkg_id =
                vmsr_get_physical_package_id(thd_stat[i].cpu_id);
        }

        /* Retrieve all packages power plane energy counter */
        for (int i = 0; i < vmsr->host_topo.maxpkgs; i++) {
            for (int j = 0; j < num_threads; j++) {
                /*
                 * Use the first thread we found that ran on the CPU
                 * of the package to read the packages energy counter
                 */
                if (thd_stat[j].pkg_id == i) {
                    pkg_stat[i].e_start =
                    vmsr_read_msr(MSR_PKG_ENERGY_STATUS,
                                  thd_stat[j].cpu_id,
                                  thd_stat[j].thread_id,
                                  s->msr_energy.sioc);
                    break;
                }
            }
        }

        /* Sleep a short period while the other threads are working */
        usleep(MSR_ENERGY_THREAD_SLEEP_US);

        /*
         * Retrieve all packages power plane energy counter
         * Calculate the delta of all packages
         */
        for (int i = 0; i < vmsr->host_topo.maxpkgs; i++) {
            for (int j = 0; j < num_threads; j++) {
                /*
                 * Use the first thread we found that ran on the CPU
                 * of the package to read the packages energy counter
                 */
                if (thd_stat[j].pkg_id == i) {
                    pkg_stat[i].e_end =
                    vmsr_read_msr(MSR_PKG_ENERGY_STATUS,
                                  thd_stat[j].cpu_id,
                                  thd_stat[j].thread_id,
                                  s->msr_energy.sioc);
                    /*
                     * Prevent the case we have migrate the VM
                     * during the sleep period or any other cases
                     * were energy counter might be lower after
                     * the sleep period.
                     */
                    if (pkg_stat[i].e_end > pkg_stat[i].e_start) {
                        pkg_stat[i].e_delta =
                            pkg_stat[i].e_end - pkg_stat[i].e_start;
                    } else {
                        pkg_stat[i].e_delta = 0;
                    }
                    break;
                }
            }
        }

        /* Delta of ticks spend by each thread between the sample */
        for (int i = 0; i < num_threads; i++) {
            vmsr_read_thread_stat(vmsr->pid,
                                  thd_stat[i].thread_id,
                                  &thd_stat[i].utime[1],
                                  &thd_stat[i].stime[1],
                                  &thd_stat[i].cpu_id);

            if (vmsr->pid < 0) {
                /*
                 * We don't count the dead thread
                 * i.e threads that existed before the sleep
                 * and not anymore
                 */
                thd_stat[i].delta_ticks = 0;
            } else {
                vmsr_delta_ticks(thd_stat, i);
            }
        }

        /*
         * Identify the vcpu threads
         * Calculate the number of vcpu per package
         */
        CPU_FOREACH(cpu) {
            for (int i = 0; i < num_threads; i++) {
                if (cpu->thread_id == thd_stat[i].thread_id) {
                    thd_stat[i].is_vcpu = true;
                    thd_stat[i].vcpu_id = cpu->cpu_index;
                    pkg_stat[thd_stat[i].pkg_id].nb_vcpu++;
                    thd_stat[i].acpi_id = kvm_arch_vcpu_id(cpu);
                    break;
                }
            }
        }

        /* Retrieve the virtual package number of each vCPU */
        for (int i = 0; i < vmsr->guest_cpu_list->len; i++) {
            for (int j = 0; j < num_threads; j++) {
                if ((thd_stat[j].acpi_id ==
                        vmsr->guest_cpu_list->cpus[i].arch_id)
                    && (thd_stat[j].is_vcpu == true)) {
                    x86_topo_ids_from_apicid(thd_stat[j].acpi_id,
                        &vmsr->guest_topo_info, &topo_ids);
                    thd_stat[j].vpkg_id = topo_ids.pkg_id;
                }
            }
        }

        /* Calculate the total energy of all non-vCPU thread */
        for (int i = 0; i < num_threads; i++) {
            if ((thd_stat[i].is_vcpu != true) &&
                (thd_stat[i].delta_ticks > 0)) {
                double temp;
                temp = vmsr_get_ratio(pkg_stat[thd_stat[i].pkg_id].e_delta,
                    thd_stat[i].delta_ticks,
                    vmsr->host_topo.maxticks[thd_stat[i].pkg_id]);
                pkg_stat[thd_stat[i].pkg_id].e_ratio
                    += (uint64_t)lround(temp);
            }
        }

        /* Calculate the ratio per non-vCPU thread of each package */
        for (int i = 0; i < vmsr->host_topo.maxpkgs; i++) {
            if (pkg_stat[i].nb_vcpu > 0) {
                pkg_stat[i].e_ratio = pkg_stat[i].e_ratio / pkg_stat[i].nb_vcpu;
            }
        }

        /*
         * Calculate the energy for each Package:
         * Energy Package = sum of each vCPU energy that belongs to the package
         */
        for (int i = 0; i < num_threads; i++) {
            if ((thd_stat[i].is_vcpu == true) && \
                    (thd_stat[i].delta_ticks > 0)) {
                double temp;
                temp = vmsr_get_ratio(pkg_stat[thd_stat[i].pkg_id].e_delta,
                    thd_stat[i].delta_ticks,
                    vmsr->host_topo.maxticks[thd_stat[i].pkg_id]);
                vpkgs_energy_stat[thd_stat[i].vpkg_id] +=
                    (uint64_t)lround(temp);
                vpkgs_energy_stat[thd_stat[i].vpkg_id] +=
                    pkg_stat[thd_stat[i].pkg_id].e_ratio;
            }
        }

        /*
         * Finally populate the vmsr register of each vCPU with the total
         * package value to emulate the real hardware where each CPU return the
         * value of the package it belongs.
         */
        for (int i = 0; i < num_threads; i++) {
            if ((thd_stat[i].is_vcpu == true) && \
                    (thd_stat[i].delta_ticks > 0)) {
                vmsr->msr_value[thd_stat[i].vcpu_id] = \
                                        vpkgs_energy_stat[thd_stat[i].vpkg_id];
          }
        }

        /* Freeing memory before zeroing the pointer */
        for (int i = 0; i < num_threads; i++) {
            g_free(thd_stat[i].utime);
            g_free(thd_stat[i].stime);
        }
   }

clean:
    rcu_unregister_thread();
    return NULL;
}

static int kvm_msr_energy_thread_init(KVMState *s, MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    struct KVMMsrEnergy *r = &s->msr_energy;
    int ret = 0;

    /*
     * Sanity check
     * 1. Host cpu must be Intel cpu
     * 2. RAPL must be enabled on the Host
     */
    if (is_host_cpu_intel()) {
        error_report("The RAPL feature can only be enabled on hosts\
                      with Intel CPU models");
        ret = 1;
        goto out;
    }

    if (!is_rapl_enabled()) {
        ret = 1;
        goto out;
    }

    /* Retrieve the virtual topology */
    vmsr_init_topo_info(&r->guest_topo_info, ms);

    /* Retrieve the number of vcpu */
    r->guest_vcpus = ms->smp.cpus;

    /* Retrieve the number of virtual sockets */
    r->guest_vsockets = ms->smp.sockets;

    /* Allocate register memory (MSR_PKG_STATUS) for each vcpu */
    r->msr_value = g_new0(uint64_t, r->guest_vcpus);

    /* Retrieve the CPUArchIDlist */
    r->guest_cpu_list = mc->possible_cpu_arch_ids(ms);

    /* Max number of cpus on the Host */
    r->host_topo.maxcpus = vmsr_get_maxcpus();
    if (r->host_topo.maxcpus == 0) {
        error_report("host max cpus = 0");
        ret = 1;
        goto out;
    }

    /* Max number of packages on the host */
    r->host_topo.maxpkgs = vmsr_get_max_physical_package(r->host_topo.maxcpus);
    if (r->host_topo.maxpkgs == 0) {
        error_report("host max pkgs = 0");
        ret = 1;
        goto out;
    }

    /* Allocate memory for each package on the host */
    r->host_topo.pkg_cpu_count = g_new0(unsigned int, r->host_topo.maxpkgs);
    r->host_topo.maxticks = g_new0(unsigned int, r->host_topo.maxpkgs);

    vmsr_count_cpus_per_package(r->host_topo.pkg_cpu_count,
                                r->host_topo.maxpkgs);
    for (int i = 0; i < r->host_topo.maxpkgs; i++) {
        if (r->host_topo.pkg_cpu_count[i] == 0) {
            error_report("cpu per packages = 0 on package_%d", i);
            ret = 1;
            goto out;
        }
    }

    /* Get QEMU PID*/
    r->pid = getpid();

    /* Compute the socket path if necessary */
    if (s->msr_energy.socket_path == NULL) {
        s->msr_energy.socket_path = vmsr_compute_default_paths();
    }

    /* Open socket with vmsr helper */
    s->msr_energy.sioc = vmsr_open_socket(s->msr_energy.socket_path);

    if (s->msr_energy.sioc == NULL) {
        error_report("vmsr socket opening failed");
        ret = 1;
        goto out;
    }

    /* Those MSR values should not change */
    r->msr_unit  = vmsr_read_msr(MSR_RAPL_POWER_UNIT, 0, r->pid,
                                    s->msr_energy.sioc);
    r->msr_limit = vmsr_read_msr(MSR_PKG_POWER_LIMIT, 0, r->pid,
                                    s->msr_energy.sioc);
    r->msr_info  = vmsr_read_msr(MSR_PKG_POWER_INFO, 0, r->pid,
                                    s->msr_energy.sioc);
    if (r->msr_unit == 0 || r->msr_limit == 0 || r->msr_info == 0) {
        error_report("can't read any virtual msr");
        ret = 1;
        goto out;
    }

    qemu_thread_create(&r->msr_thr, "kvm-msr",
                       kvm_msr_energy_thread,
                       s, QEMU_THREAD_JOINABLE);
out:
    return ret;
}

int kvm_arch_get_default_type(MachineState *ms)
{
    return 0;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    uint64_t identity_base = 0xfffbc000;
    uint64_t shadow_mem;
    int ret;
    struct utsname utsname;
    Error *local_err = NULL;

    /*
     * Initialize SEV context, if required
     *
     * If no memory encryption is requested (ms->cgs == NULL) this is
     * a no-op.
     *
     * It's also a no-op if a non-SEV confidential guest support
     * mechanism is selected.  SEV is the only mechanism available to
     * select on x86 at present, so this doesn't arise, but if new
     * mechanisms are supported in future (e.g. TDX), they'll need
     * their own initialization either here or elsewhere.
     */
    if (ms->cgs) {
        ret = confidential_guest_kvm_init(ms->cgs, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            return ret;
        }
    }

    has_xcrs = kvm_check_extension(s, KVM_CAP_XCRS);
    has_sregs2 = kvm_check_extension(s, KVM_CAP_SREGS2) > 0;

    hv_vpindex_settable = kvm_check_extension(s, KVM_CAP_HYPERV_VP_INDEX);

    has_exception_payload = kvm_check_extension(s, KVM_CAP_EXCEPTION_PAYLOAD);
    if (has_exception_payload) {
        ret = kvm_vm_enable_cap(s, KVM_CAP_EXCEPTION_PAYLOAD, 0, true);
        if (ret < 0) {
            error_report("kvm: Failed to enable exception payload cap: %s",
                         strerror(-ret));
            return ret;
        }
    }

    has_triple_fault_event = kvm_check_extension(s, KVM_CAP_X86_TRIPLE_FAULT_EVENT);
    if (has_triple_fault_event) {
        ret = kvm_vm_enable_cap(s, KVM_CAP_X86_TRIPLE_FAULT_EVENT, 0, true);
        if (ret < 0) {
            error_report("kvm: Failed to enable triple fault event cap: %s",
                         strerror(-ret));
            return ret;
        }
    }

    if (s->xen_version) {
#ifdef CONFIG_XEN_EMU
        if (!object_dynamic_cast(OBJECT(ms), TYPE_PC_MACHINE)) {
            error_report("kvm: Xen support only available in PC machine");
            return -ENOTSUP;
        }
        /* hyperv_enabled() doesn't work yet. */
        uint32_t msr = XEN_HYPERCALL_MSR;
        ret = kvm_xen_init(s, msr);
        if (ret < 0) {
            return ret;
        }
#else
        error_report("kvm: Xen support not enabled in qemu");
        return -ENOTSUP;
#endif
    }

    ret = kvm_get_supported_msrs(s);
    if (ret < 0) {
        return ret;
    }

    kvm_get_supported_feature_msrs(s);

    uname(&utsname);
    lm_capable_kernel = strcmp(utsname.machine, "x86_64") == 0;

    /*
     * On older Intel CPUs, KVM uses vm86 mode to emulate 16-bit code directly.
     * In order to use vm86 mode, an EPT identity map and a TSS  are needed.
     * Since these must be part of guest physical memory, we need to allocate
     * them, both by setting their start addresses in the kernel and by
     * creating a corresponding e820 entry. We need 4 pages before the BIOS,
     * so this value allows up to 16M BIOSes.
     */
    identity_base = 0xfeffc000;
    ret = kvm_vm_ioctl(s, KVM_SET_IDENTITY_MAP_ADDR, &identity_base);
    if (ret < 0) {
        return ret;
    }

    /* Set TSS base one page after EPT identity map. */
    ret = kvm_vm_ioctl(s, KVM_SET_TSS_ADDR, identity_base + 0x1000);
    if (ret < 0) {
        return ret;
    }

    /* Tell fw_cfg to notify the BIOS to reserve the range. */
    e820_add_entry(identity_base, 0x4000, E820_RESERVED);

    shadow_mem = object_property_get_int(OBJECT(s), "kvm-shadow-mem", &error_abort);
    if (shadow_mem != -1) {
        shadow_mem /= 4096;
        ret = kvm_vm_ioctl(s, KVM_SET_NR_MMU_PAGES, shadow_mem);
        if (ret < 0) {
            return ret;
        }
    }

    if (kvm_check_extension(s, KVM_CAP_X86_SMM) &&
        object_dynamic_cast(OBJECT(ms), TYPE_X86_MACHINE) &&
        x86_machine_is_smm_enabled(X86_MACHINE(ms))) {
        smram_machine_done.notify = register_smram_listener;
        qemu_add_machine_init_done_notifier(&smram_machine_done);
    }

    if (enable_cpu_pm) {
        int disable_exits = kvm_check_extension(s, KVM_CAP_X86_DISABLE_EXITS);
/* Work around for kernel header with a typo. TODO: fix header and drop. */
#if defined(KVM_X86_DISABLE_EXITS_HTL) && !defined(KVM_X86_DISABLE_EXITS_HLT)
#define KVM_X86_DISABLE_EXITS_HLT KVM_X86_DISABLE_EXITS_HTL
#endif
        if (disable_exits) {
            disable_exits &= (KVM_X86_DISABLE_EXITS_MWAIT |
                              KVM_X86_DISABLE_EXITS_HLT |
                              KVM_X86_DISABLE_EXITS_PAUSE |
                              KVM_X86_DISABLE_EXITS_CSTATE);
        }

        ret = kvm_vm_enable_cap(s, KVM_CAP_X86_DISABLE_EXITS, 0,
                                disable_exits);
        if (ret < 0) {
            error_report("kvm: guest stopping CPU not supported: %s",
                         strerror(-ret));
        }
    }

    if (object_dynamic_cast(OBJECT(ms), TYPE_X86_MACHINE)) {
        X86MachineState *x86ms = X86_MACHINE(ms);

        if (x86ms->bus_lock_ratelimit > 0) {
            ret = kvm_check_extension(s, KVM_CAP_X86_BUS_LOCK_EXIT);
            if (!(ret & KVM_BUS_LOCK_DETECTION_EXIT)) {
                error_report("kvm: bus lock detection unsupported");
                return -ENOTSUP;
            }
            ret = kvm_vm_enable_cap(s, KVM_CAP_X86_BUS_LOCK_EXIT, 0,
                                    KVM_BUS_LOCK_DETECTION_EXIT);
            if (ret < 0) {
                error_report("kvm: Failed to enable bus lock detection cap: %s",
                             strerror(-ret));
                return ret;
            }
            ratelimit_init(&bus_lock_ratelimit_ctrl);
            ratelimit_set_speed(&bus_lock_ratelimit_ctrl,
                                x86ms->bus_lock_ratelimit, BUS_LOCK_SLICE_TIME);
        }
    }

    if (s->notify_vmexit != NOTIFY_VMEXIT_OPTION_DISABLE &&
        kvm_check_extension(s, KVM_CAP_X86_NOTIFY_VMEXIT)) {
            uint64_t notify_window_flags =
                ((uint64_t)s->notify_window << 32) |
                KVM_X86_NOTIFY_VMEXIT_ENABLED |
                KVM_X86_NOTIFY_VMEXIT_USER;
            ret = kvm_vm_enable_cap(s, KVM_CAP_X86_NOTIFY_VMEXIT, 0,
                                    notify_window_flags);
            if (ret < 0) {
                error_report("kvm: Failed to enable notify vmexit cap: %s",
                             strerror(-ret));
                return ret;
            }
    }
    if (kvm_vm_check_extension(s, KVM_CAP_X86_USER_SPACE_MSR)) {
        bool r;

        ret = kvm_vm_enable_cap(s, KVM_CAP_X86_USER_SPACE_MSR, 0,
                                KVM_MSR_EXIT_REASON_FILTER);
        if (ret) {
            error_report("Could not enable user space MSRs: %s",
                         strerror(-ret));
            exit(1);
        }

        r = kvm_filter_msr(s, MSR_CORE_THREAD_COUNT,
                           kvm_rdmsr_core_thread_count, NULL);
        if (!r) {
            error_report("Could not install MSR_CORE_THREAD_COUNT handler: %s",
                         strerror(-ret));
            exit(1);
        }

        if (s->msr_energy.enable == true) {
            r = kvm_filter_msr(s, MSR_RAPL_POWER_UNIT,
                               kvm_rdmsr_rapl_power_unit, NULL);
            if (!r) {
                error_report("Could not install MSR_RAPL_POWER_UNIT \
                                handler: %s",
                             strerror(-ret));
                exit(1);
            }

            r = kvm_filter_msr(s, MSR_PKG_POWER_LIMIT,
                               kvm_rdmsr_pkg_power_limit, NULL);
            if (!r) {
                error_report("Could not install MSR_PKG_POWER_LIMIT \
                                handler: %s",
                             strerror(-ret));
                exit(1);
            }

            r = kvm_filter_msr(s, MSR_PKG_POWER_INFO,
                               kvm_rdmsr_pkg_power_info, NULL);
            if (!r) {
                error_report("Could not install MSR_PKG_POWER_INFO \
                                handler: %s",
                             strerror(-ret));
                exit(1);
            }
            r = kvm_filter_msr(s, MSR_PKG_ENERGY_STATUS,
                               kvm_rdmsr_pkg_energy_status, NULL);
            if (!r) {
                error_report("Could not install MSR_PKG_ENERGY_STATUS \
                                handler: %s",
                             strerror(-ret));
                exit(1);
            }
            r = kvm_msr_energy_thread_init(s, ms);
            if (r) {
                error_report("kvm : error RAPL feature requirement not meet");
                exit(1);
            }

        }
    }

    return 0;
}

static void set_v8086_seg(struct kvm_segment *lhs, const SegmentCache *rhs)
{
    lhs->selector = rhs->selector;
    lhs->base = rhs->base;
    lhs->limit = rhs->limit;
    lhs->type = 3;
    lhs->present = 1;
    lhs->dpl = 3;
    lhs->db = 0;
    lhs->s = 1;
    lhs->l = 0;
    lhs->g = 0;
    lhs->avl = 0;
    lhs->unusable = 0;
}

static void set_seg(struct kvm_segment *lhs, const SegmentCache *rhs)
{
    unsigned flags = rhs->flags;
    lhs->selector = rhs->selector;
    lhs->base = rhs->base;
    lhs->limit = rhs->limit;
    lhs->type = (flags >> DESC_TYPE_SHIFT) & 15;
    lhs->present = (flags & DESC_P_MASK) != 0;
    lhs->dpl = (flags >> DESC_DPL_SHIFT) & 3;
    lhs->db = (flags >> DESC_B_SHIFT) & 1;
    lhs->s = (flags & DESC_S_MASK) != 0;
    lhs->l = (flags >> DESC_L_SHIFT) & 1;
    lhs->g = (flags & DESC_G_MASK) != 0;
    lhs->avl = (flags & DESC_AVL_MASK) != 0;
    lhs->unusable = !lhs->present;
    lhs->padding = 0;
}

static void get_seg(SegmentCache *lhs, const struct kvm_segment *rhs)
{
    lhs->selector = rhs->selector;
    lhs->base = rhs->base;
    lhs->limit = rhs->limit;
    lhs->flags = (rhs->type << DESC_TYPE_SHIFT) |
                 ((rhs->present && !rhs->unusable) * DESC_P_MASK) |
                 (rhs->dpl << DESC_DPL_SHIFT) |
                 (rhs->db << DESC_B_SHIFT) |
                 (rhs->s * DESC_S_MASK) |
                 (rhs->l << DESC_L_SHIFT) |
                 (rhs->g * DESC_G_MASK) |
                 (rhs->avl * DESC_AVL_MASK);
}

static void kvm_getput_reg(__u64 *kvm_reg, target_ulong *qemu_reg, int set)
{
    if (set) {
        *kvm_reg = *qemu_reg;
    } else {
        *qemu_reg = *kvm_reg;
    }
}

static int kvm_getput_regs(X86CPU *cpu, int set)
{
    CPUX86State *env = &cpu->env;
    struct kvm_regs regs;
    int ret = 0;

    if (!set) {
        ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_REGS, &regs);
        if (ret < 0) {
            return ret;
        }
    }

    kvm_getput_reg(&regs.rax, &env->regs[R_EAX], set);
    kvm_getput_reg(&regs.rbx, &env->regs[R_EBX], set);
    kvm_getput_reg(&regs.rcx, &env->regs[R_ECX], set);
    kvm_getput_reg(&regs.rdx, &env->regs[R_EDX], set);
    kvm_getput_reg(&regs.rsi, &env->regs[R_ESI], set);
    kvm_getput_reg(&regs.rdi, &env->regs[R_EDI], set);
    kvm_getput_reg(&regs.rsp, &env->regs[R_ESP], set);
    kvm_getput_reg(&regs.rbp, &env->regs[R_EBP], set);
#ifdef TARGET_X86_64
    kvm_getput_reg(&regs.r8, &env->regs[8], set);
    kvm_getput_reg(&regs.r9, &env->regs[9], set);
    kvm_getput_reg(&regs.r10, &env->regs[10], set);
    kvm_getput_reg(&regs.r11, &env->regs[11], set);
    kvm_getput_reg(&regs.r12, &env->regs[12], set);
    kvm_getput_reg(&regs.r13, &env->regs[13], set);
    kvm_getput_reg(&regs.r14, &env->regs[14], set);
    kvm_getput_reg(&regs.r15, &env->regs[15], set);
#endif

    kvm_getput_reg(&regs.rflags, &env->eflags, set);
    kvm_getput_reg(&regs.rip, &env->eip, set);

    if (set) {
        ret = kvm_vcpu_ioctl(CPU(cpu), KVM_SET_REGS, &regs);
    }

    return ret;
}

static int kvm_put_xsave(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    void *xsave = env->xsave_buf;

    x86_cpu_xsave_all_areas(cpu, xsave, env->xsave_buf_len);

    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_XSAVE, xsave);
}

static int kvm_put_xcrs(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_xcrs xcrs = {};

    if (!has_xcrs) {
        return 0;
    }

    xcrs.nr_xcrs = 1;
    xcrs.flags = 0;
    xcrs.xcrs[0].xcr = 0;
    xcrs.xcrs[0].value = env->xcr0;
    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_XCRS, &xcrs);
}

static int kvm_put_sregs(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_sregs sregs;

    /*
     * The interrupt_bitmap is ignored because KVM_SET_SREGS is
     * always followed by KVM_SET_VCPU_EVENTS.
     */
    memset(sregs.interrupt_bitmap, 0, sizeof(sregs.interrupt_bitmap));

    if ((env->eflags & VM_MASK)) {
        set_v8086_seg(&sregs.cs, &env->segs[R_CS]);
        set_v8086_seg(&sregs.ds, &env->segs[R_DS]);
        set_v8086_seg(&sregs.es, &env->segs[R_ES]);
        set_v8086_seg(&sregs.fs, &env->segs[R_FS]);
        set_v8086_seg(&sregs.gs, &env->segs[R_GS]);
        set_v8086_seg(&sregs.ss, &env->segs[R_SS]);
    } else {
        set_seg(&sregs.cs, &env->segs[R_CS]);
        set_seg(&sregs.ds, &env->segs[R_DS]);
        set_seg(&sregs.es, &env->segs[R_ES]);
        set_seg(&sregs.fs, &env->segs[R_FS]);
        set_seg(&sregs.gs, &env->segs[R_GS]);
        set_seg(&sregs.ss, &env->segs[R_SS]);
    }

    set_seg(&sregs.tr, &env->tr);
    set_seg(&sregs.ldt, &env->ldt);

    sregs.idt.limit = env->idt.limit;
    sregs.idt.base = env->idt.base;
    memset(sregs.idt.padding, 0, sizeof sregs.idt.padding);
    sregs.gdt.limit = env->gdt.limit;
    sregs.gdt.base = env->gdt.base;
    memset(sregs.gdt.padding, 0, sizeof sregs.gdt.padding);

    sregs.cr0 = env->cr[0];
    sregs.cr2 = env->cr[2];
    sregs.cr3 = env->cr[3];
    sregs.cr4 = env->cr[4];

    sregs.cr8 = cpu_get_apic_tpr(cpu->apic_state);
    sregs.apic_base = cpu_get_apic_base(cpu->apic_state);

    sregs.efer = env->efer;

    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_SREGS, &sregs);
}

static int kvm_put_sregs2(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_sregs2 sregs;
    int i;

    sregs.flags = 0;

    if ((env->eflags & VM_MASK)) {
        set_v8086_seg(&sregs.cs, &env->segs[R_CS]);
        set_v8086_seg(&sregs.ds, &env->segs[R_DS]);
        set_v8086_seg(&sregs.es, &env->segs[R_ES]);
        set_v8086_seg(&sregs.fs, &env->segs[R_FS]);
        set_v8086_seg(&sregs.gs, &env->segs[R_GS]);
        set_v8086_seg(&sregs.ss, &env->segs[R_SS]);
    } else {
        set_seg(&sregs.cs, &env->segs[R_CS]);
        set_seg(&sregs.ds, &env->segs[R_DS]);
        set_seg(&sregs.es, &env->segs[R_ES]);
        set_seg(&sregs.fs, &env->segs[R_FS]);
        set_seg(&sregs.gs, &env->segs[R_GS]);
        set_seg(&sregs.ss, &env->segs[R_SS]);
    }

    set_seg(&sregs.tr, &env->tr);
    set_seg(&sregs.ldt, &env->ldt);

    sregs.idt.limit = env->idt.limit;
    sregs.idt.base = env->idt.base;
    memset(sregs.idt.padding, 0, sizeof sregs.idt.padding);
    sregs.gdt.limit = env->gdt.limit;
    sregs.gdt.base = env->gdt.base;
    memset(sregs.gdt.padding, 0, sizeof sregs.gdt.padding);

    sregs.cr0 = env->cr[0];
    sregs.cr2 = env->cr[2];
    sregs.cr3 = env->cr[3];
    sregs.cr4 = env->cr[4];

    sregs.cr8 = cpu_get_apic_tpr(cpu->apic_state);
    sregs.apic_base = cpu_get_apic_base(cpu->apic_state);

    sregs.efer = env->efer;

    if (env->pdptrs_valid) {
        for (i = 0; i < 4; i++) {
            sregs.pdptrs[i] = env->pdptrs[i];
        }
        sregs.flags |= KVM_SREGS2_FLAGS_PDPTRS_VALID;
    }

    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_SREGS2, &sregs);
}


static void kvm_msr_buf_reset(X86CPU *cpu)
{
    memset(cpu->kvm_msr_buf, 0, MSR_BUF_SIZE);
}

static void kvm_msr_entry_add(X86CPU *cpu, uint32_t index, uint64_t value)
{
    struct kvm_msrs *msrs = cpu->kvm_msr_buf;
    void *limit = ((void *)msrs) + MSR_BUF_SIZE;
    struct kvm_msr_entry *entry = &msrs->entries[msrs->nmsrs];

    assert((void *)(entry + 1) <= limit);

    entry->index = index;
    entry->reserved = 0;
    entry->data = value;
    msrs->nmsrs++;
}

static int kvm_put_one_msr(X86CPU *cpu, int index, uint64_t value)
{
    kvm_msr_buf_reset(cpu);
    kvm_msr_entry_add(cpu, index, value);

    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_MSRS, cpu->kvm_msr_buf);
}

static int kvm_get_one_msr(X86CPU *cpu, int index, uint64_t *value)
{
    int ret;
    struct {
        struct kvm_msrs info;
        struct kvm_msr_entry entries[1];
    } msr_data = {
        .info.nmsrs = 1,
        .entries[0].index = index,
    };

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_MSRS, &msr_data);
    if (ret < 0) {
        return ret;
    }
    assert(ret == 1);
    *value = msr_data.entries[0].data;
    return ret;
}
void kvm_put_apicbase(X86CPU *cpu, uint64_t value)
{
    int ret;

    ret = kvm_put_one_msr(cpu, MSR_IA32_APICBASE, value);
    assert(ret == 1);
}

static int kvm_put_tscdeadline_msr(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    int ret;

    if (!has_msr_tsc_deadline) {
        return 0;
    }

    ret = kvm_put_one_msr(cpu, MSR_IA32_TSCDEADLINE, env->tsc_deadline);
    if (ret < 0) {
        return ret;
    }

    assert(ret == 1);
    return 0;
}

/*
 * Provide a separate write service for the feature control MSR in order to
 * kick the VCPU out of VMXON or even guest mode on reset. This has to be done
 * before writing any other state because forcibly leaving nested mode
 * invalidates the VCPU state.
 */
static int kvm_put_msr_feature_control(X86CPU *cpu)
{
    int ret;

    if (!has_msr_feature_control) {
        return 0;
    }

    ret = kvm_put_one_msr(cpu, MSR_IA32_FEATURE_CONTROL,
                          cpu->env.msr_ia32_feature_control);
    if (ret < 0) {
        return ret;
    }

    assert(ret == 1);
    return 0;
}

static uint64_t make_vmx_msr_value(uint32_t index, uint32_t features)
{
    uint32_t default1, can_be_one, can_be_zero;
    uint32_t must_be_one;

    switch (index) {
    case MSR_IA32_VMX_TRUE_PINBASED_CTLS:
        default1 = 0x00000016;
        break;
    case MSR_IA32_VMX_TRUE_PROCBASED_CTLS:
        default1 = 0x0401e172;
        break;
    case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
        default1 = 0x000011ff;
        break;
    case MSR_IA32_VMX_TRUE_EXIT_CTLS:
        default1 = 0x00036dff;
        break;
    case MSR_IA32_VMX_PROCBASED_CTLS2:
        default1 = 0;
        break;
    default:
        abort();
    }

    /* If a feature bit is set, the control can be either set or clear.
     * Otherwise the value is limited to either 0 or 1 by default1.
     */
    can_be_one = features | default1;
    can_be_zero = features | ~default1;
    must_be_one = ~can_be_zero;

    /*
     * Bit 0:31 -> 0 if the control bit can be zero (i.e. 1 if it must be one).
     * Bit 32:63 -> 1 if the control bit can be one.
     */
    return must_be_one | (((uint64_t)can_be_one) << 32);
}

static void kvm_msr_entry_add_vmx(X86CPU *cpu, FeatureWordArray f)
{
    uint64_t kvm_vmx_basic =
        kvm_arch_get_supported_msr_feature(kvm_state,
                                           MSR_IA32_VMX_BASIC);

    if (!kvm_vmx_basic) {
        /* If the kernel doesn't support VMX feature (kvm_intel.nested=0),
         * then kvm_vmx_basic will be 0 and KVM_SET_MSR will fail.
         */
        return;
    }

    uint64_t kvm_vmx_misc =
        kvm_arch_get_supported_msr_feature(kvm_state,
                                           MSR_IA32_VMX_MISC);
    uint64_t kvm_vmx_ept_vpid =
        kvm_arch_get_supported_msr_feature(kvm_state,
                                           MSR_IA32_VMX_EPT_VPID_CAP);

    /*
     * If the guest is 64-bit, a value of 1 is allowed for the host address
     * space size vmexit control.
     */
    uint64_t fixed_vmx_exit = f[FEAT_8000_0001_EDX] & CPUID_EXT2_LM
        ? (uint64_t)VMX_VM_EXIT_HOST_ADDR_SPACE_SIZE << 32 : 0;

    /*
     * Bits 0-30, 32-44 and 50-53 come from the host.  KVM should
     * not change them for backwards compatibility.
     */
    uint64_t fixed_vmx_basic = kvm_vmx_basic &
        (MSR_VMX_BASIC_VMCS_REVISION_MASK |
         MSR_VMX_BASIC_VMXON_REGION_SIZE_MASK |
         MSR_VMX_BASIC_VMCS_MEM_TYPE_MASK);

    /*
     * Same for bits 0-4 and 25-27.  Bits 16-24 (CR3 target count) can
     * change in the future but are always zero for now, clear them to be
     * future proof.  Bits 32-63 in theory could change, though KVM does
     * not support dual-monitor treatment and probably never will; mask
     * them out as well.
     */
    uint64_t fixed_vmx_misc = kvm_vmx_misc &
        (MSR_VMX_MISC_PREEMPTION_TIMER_SHIFT_MASK |
         MSR_VMX_MISC_MAX_MSR_LIST_SIZE_MASK);

    /*
     * EPT memory types should not change either, so we do not bother
     * adding features for them.
     */
    uint64_t fixed_vmx_ept_mask =
            (f[FEAT_VMX_SECONDARY_CTLS] & VMX_SECONDARY_EXEC_ENABLE_EPT ?
             MSR_VMX_EPT_UC | MSR_VMX_EPT_WB : 0);
    uint64_t fixed_vmx_ept_vpid = kvm_vmx_ept_vpid & fixed_vmx_ept_mask;

    kvm_msr_entry_add(cpu, MSR_IA32_VMX_TRUE_PROCBASED_CTLS,
                      make_vmx_msr_value(MSR_IA32_VMX_TRUE_PROCBASED_CTLS,
                                         f[FEAT_VMX_PROCBASED_CTLS]));
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_TRUE_PINBASED_CTLS,
                      make_vmx_msr_value(MSR_IA32_VMX_TRUE_PINBASED_CTLS,
                                         f[FEAT_VMX_PINBASED_CTLS]));
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_TRUE_EXIT_CTLS,
                      make_vmx_msr_value(MSR_IA32_VMX_TRUE_EXIT_CTLS,
                                         f[FEAT_VMX_EXIT_CTLS]) | fixed_vmx_exit);
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_TRUE_ENTRY_CTLS,
                      make_vmx_msr_value(MSR_IA32_VMX_TRUE_ENTRY_CTLS,
                                         f[FEAT_VMX_ENTRY_CTLS]));
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_PROCBASED_CTLS2,
                      make_vmx_msr_value(MSR_IA32_VMX_PROCBASED_CTLS2,
                                         f[FEAT_VMX_SECONDARY_CTLS]));
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_EPT_VPID_CAP,
                      f[FEAT_VMX_EPT_VPID_CAPS] | fixed_vmx_ept_vpid);
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_BASIC,
                      f[FEAT_VMX_BASIC] | fixed_vmx_basic);
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_MISC,
                      f[FEAT_VMX_MISC] | fixed_vmx_misc);
    if (has_msr_vmx_vmfunc) {
        kvm_msr_entry_add(cpu, MSR_IA32_VMX_VMFUNC, f[FEAT_VMX_VMFUNC]);
    }

    /*
     * Just to be safe, write these with constant values.  The CRn_FIXED1
     * MSRs are generated by KVM based on the vCPU's CPUID.
     */
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_CR0_FIXED0,
                      CR0_PE_MASK | CR0_PG_MASK | CR0_NE_MASK);
    kvm_msr_entry_add(cpu, MSR_IA32_VMX_CR4_FIXED0,
                      CR4_VMXE_MASK);

    if (f[FEAT_VMX_SECONDARY_CTLS] & VMX_SECONDARY_EXEC_TSC_SCALING) {
        /* TSC multiplier (0x2032).  */
        kvm_msr_entry_add(cpu, MSR_IA32_VMX_VMCS_ENUM, 0x32);
    } else {
        /* Preemption timer (0x482E).  */
        kvm_msr_entry_add(cpu, MSR_IA32_VMX_VMCS_ENUM, 0x2E);
    }
}

static void kvm_msr_entry_add_perf(X86CPU *cpu, FeatureWordArray f)
{
    uint64_t kvm_perf_cap =
        kvm_arch_get_supported_msr_feature(kvm_state,
                                           MSR_IA32_PERF_CAPABILITIES);

    if (kvm_perf_cap) {
        kvm_msr_entry_add(cpu, MSR_IA32_PERF_CAPABILITIES,
                        kvm_perf_cap & f[FEAT_PERF_CAPABILITIES]);
    }
}

static int kvm_buf_set_msrs(X86CPU *cpu)
{
    int ret = kvm_vcpu_ioctl(CPU(cpu), KVM_SET_MSRS, cpu->kvm_msr_buf);
    if (ret < 0) {
        return ret;
    }

    if (ret < cpu->kvm_msr_buf->nmsrs) {
        struct kvm_msr_entry *e = &cpu->kvm_msr_buf->entries[ret];
        error_report("error: failed to set MSR 0x%" PRIx32 " to 0x%" PRIx64,
                     (uint32_t)e->index, (uint64_t)e->data);
    }

    assert(ret == cpu->kvm_msr_buf->nmsrs);
    return 0;
}

static void kvm_init_msrs(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    kvm_msr_buf_reset(cpu);
    if (has_msr_arch_capabs) {
        kvm_msr_entry_add(cpu, MSR_IA32_ARCH_CAPABILITIES,
                          env->features[FEAT_ARCH_CAPABILITIES]);
    }

    if (has_msr_core_capabs) {
        kvm_msr_entry_add(cpu, MSR_IA32_CORE_CAPABILITY,
                          env->features[FEAT_CORE_CAPABILITY]);
    }

    if (has_msr_perf_capabs && cpu->enable_pmu) {
        kvm_msr_entry_add_perf(cpu, env->features);
    }

    if (has_msr_ucode_rev) {
        kvm_msr_entry_add(cpu, MSR_IA32_UCODE_REV, cpu->ucode_rev);
    }

    /*
     * Older kernels do not include VMX MSRs in KVM_GET_MSR_INDEX_LIST, but
     * all kernels with MSR features should have them.
     */
    if (kvm_feature_msrs && cpu_has_vmx(env)) {
        kvm_msr_entry_add_vmx(cpu, env->features);
    }

    assert(kvm_buf_set_msrs(cpu) == 0);
}

static int kvm_put_msrs(X86CPU *cpu, int level)
{
    CPUX86State *env = &cpu->env;
    int i;

    kvm_msr_buf_reset(cpu);

    kvm_msr_entry_add(cpu, MSR_IA32_SYSENTER_CS, env->sysenter_cs);
    kvm_msr_entry_add(cpu, MSR_IA32_SYSENTER_ESP, env->sysenter_esp);
    kvm_msr_entry_add(cpu, MSR_IA32_SYSENTER_EIP, env->sysenter_eip);
    kvm_msr_entry_add(cpu, MSR_PAT, env->pat);
    if (has_msr_star) {
        kvm_msr_entry_add(cpu, MSR_STAR, env->star);
    }
    if (has_msr_hsave_pa) {
        kvm_msr_entry_add(cpu, MSR_VM_HSAVE_PA, env->vm_hsave);
    }
    if (has_msr_tsc_aux) {
        kvm_msr_entry_add(cpu, MSR_TSC_AUX, env->tsc_aux);
    }
    if (has_msr_tsc_adjust) {
        kvm_msr_entry_add(cpu, MSR_TSC_ADJUST, env->tsc_adjust);
    }
    if (has_msr_misc_enable) {
        kvm_msr_entry_add(cpu, MSR_IA32_MISC_ENABLE,
                          env->msr_ia32_misc_enable);
    }
    if (has_msr_smbase) {
        kvm_msr_entry_add(cpu, MSR_IA32_SMBASE, env->smbase);
    }
    if (has_msr_smi_count) {
        kvm_msr_entry_add(cpu, MSR_SMI_COUNT, env->msr_smi_count);
    }
    if (has_msr_pkrs) {
        kvm_msr_entry_add(cpu, MSR_IA32_PKRS, env->pkrs);
    }
    if (has_msr_bndcfgs) {
        kvm_msr_entry_add(cpu, MSR_IA32_BNDCFGS, env->msr_bndcfgs);
    }
    if (has_msr_xss) {
        kvm_msr_entry_add(cpu, MSR_IA32_XSS, env->xss);
    }
    if (has_msr_umwait) {
        kvm_msr_entry_add(cpu, MSR_IA32_UMWAIT_CONTROL, env->umwait);
    }
    if (has_msr_spec_ctrl) {
        kvm_msr_entry_add(cpu, MSR_IA32_SPEC_CTRL, env->spec_ctrl);
    }
    if (has_tsc_scale_msr) {
        kvm_msr_entry_add(cpu, MSR_AMD64_TSC_RATIO, env->amd_tsc_scale_msr);
    }

    if (has_msr_tsx_ctrl) {
        kvm_msr_entry_add(cpu, MSR_IA32_TSX_CTRL, env->tsx_ctrl);
    }
    if (has_msr_virt_ssbd) {
        kvm_msr_entry_add(cpu, MSR_VIRT_SSBD, env->virt_ssbd);
    }

#ifdef TARGET_X86_64
    if (lm_capable_kernel) {
        kvm_msr_entry_add(cpu, MSR_CSTAR, env->cstar);
        kvm_msr_entry_add(cpu, MSR_KERNELGSBASE, env->kernelgsbase);
        kvm_msr_entry_add(cpu, MSR_FMASK, env->fmask);
        kvm_msr_entry_add(cpu, MSR_LSTAR, env->lstar);
        if (env->features[FEAT_7_1_EAX] & CPUID_7_1_EAX_FRED) {
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_RSP0, env->fred_rsp0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_RSP1, env->fred_rsp1);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_RSP2, env->fred_rsp2);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_RSP3, env->fred_rsp3);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_STKLVLS, env->fred_stklvls);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_SSP1, env->fred_ssp1);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_SSP2, env->fred_ssp2);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_SSP3, env->fred_ssp3);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_CONFIG, env->fred_config);
        }
    }
#endif

    /*
     * The following MSRs have side effects on the guest or are too heavy
     * for normal writeback. Limit them to reset or full state updates.
     */
    if (level >= KVM_PUT_RESET_STATE) {
        kvm_msr_entry_add(cpu, MSR_IA32_TSC, env->tsc);
        kvm_msr_entry_add(cpu, MSR_KVM_SYSTEM_TIME, env->system_time_msr);
        kvm_msr_entry_add(cpu, MSR_KVM_WALL_CLOCK, env->wall_clock_msr);
        if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_ASYNC_PF_INT)) {
            kvm_msr_entry_add(cpu, MSR_KVM_ASYNC_PF_INT, env->async_pf_int_msr);
        }
        if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_ASYNC_PF)) {
            kvm_msr_entry_add(cpu, MSR_KVM_ASYNC_PF_EN, env->async_pf_en_msr);
        }
        if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_PV_EOI)) {
            kvm_msr_entry_add(cpu, MSR_KVM_PV_EOI_EN, env->pv_eoi_en_msr);
        }
        if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_STEAL_TIME)) {
            kvm_msr_entry_add(cpu, MSR_KVM_STEAL_TIME, env->steal_time_msr);
        }

        if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_POLL_CONTROL)) {
            kvm_msr_entry_add(cpu, MSR_KVM_POLL_CONTROL, env->poll_control_msr);
        }

        if (has_architectural_pmu_version > 0) {
            if (has_architectural_pmu_version > 1) {
                /* Stop the counter.  */
                kvm_msr_entry_add(cpu, MSR_CORE_PERF_FIXED_CTR_CTRL, 0);
                kvm_msr_entry_add(cpu, MSR_CORE_PERF_GLOBAL_CTRL, 0);
            }

            /* Set the counter values.  */
            for (i = 0; i < num_architectural_pmu_fixed_counters; i++) {
                kvm_msr_entry_add(cpu, MSR_CORE_PERF_FIXED_CTR0 + i,
                                  env->msr_fixed_counters[i]);
            }
            for (i = 0; i < num_architectural_pmu_gp_counters; i++) {
                kvm_msr_entry_add(cpu, MSR_P6_PERFCTR0 + i,
                                  env->msr_gp_counters[i]);
                kvm_msr_entry_add(cpu, MSR_P6_EVNTSEL0 + i,
                                  env->msr_gp_evtsel[i]);
            }
            if (has_architectural_pmu_version > 1) {
                kvm_msr_entry_add(cpu, MSR_CORE_PERF_GLOBAL_STATUS,
                                  env->msr_global_status);
                kvm_msr_entry_add(cpu, MSR_CORE_PERF_GLOBAL_OVF_CTRL,
                                  env->msr_global_ovf_ctrl);

                /* Now start the PMU.  */
                kvm_msr_entry_add(cpu, MSR_CORE_PERF_FIXED_CTR_CTRL,
                                  env->msr_fixed_ctr_ctrl);
                kvm_msr_entry_add(cpu, MSR_CORE_PERF_GLOBAL_CTRL,
                                  env->msr_global_ctrl);
            }
        }
        /*
         * Hyper-V partition-wide MSRs: to avoid clearing them on cpu hot-add,
         * only sync them to KVM on the first cpu
         */
        if (current_cpu == first_cpu) {
            if (has_msr_hv_hypercall) {
                kvm_msr_entry_add(cpu, HV_X64_MSR_GUEST_OS_ID,
                                  env->msr_hv_guest_os_id);
                kvm_msr_entry_add(cpu, HV_X64_MSR_HYPERCALL,
                                  env->msr_hv_hypercall);
            }
            if (hyperv_feat_enabled(cpu, HYPERV_FEAT_TIME)) {
                kvm_msr_entry_add(cpu, HV_X64_MSR_REFERENCE_TSC,
                                  env->msr_hv_tsc);
            }
            if (hyperv_feat_enabled(cpu, HYPERV_FEAT_REENLIGHTENMENT)) {
                kvm_msr_entry_add(cpu, HV_X64_MSR_REENLIGHTENMENT_CONTROL,
                                  env->msr_hv_reenlightenment_control);
                kvm_msr_entry_add(cpu, HV_X64_MSR_TSC_EMULATION_CONTROL,
                                  env->msr_hv_tsc_emulation_control);
                kvm_msr_entry_add(cpu, HV_X64_MSR_TSC_EMULATION_STATUS,
                                  env->msr_hv_tsc_emulation_status);
            }
#ifdef CONFIG_SYNDBG
            if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNDBG) &&
                has_msr_hv_syndbg_options) {
                kvm_msr_entry_add(cpu, HV_X64_MSR_SYNDBG_OPTIONS,
                                  hyperv_syndbg_query_options());
            }
#endif
        }
        if (hyperv_feat_enabled(cpu, HYPERV_FEAT_VAPIC)) {
            kvm_msr_entry_add(cpu, HV_X64_MSR_APIC_ASSIST_PAGE,
                              env->msr_hv_vapic);
        }
        if (has_msr_hv_crash) {
            int j;

            for (j = 0; j < HV_CRASH_PARAMS; j++)
                kvm_msr_entry_add(cpu, HV_X64_MSR_CRASH_P0 + j,
                                  env->msr_hv_crash_params[j]);

            kvm_msr_entry_add(cpu, HV_X64_MSR_CRASH_CTL, HV_CRASH_CTL_NOTIFY);
        }
        if (has_msr_hv_runtime) {
            kvm_msr_entry_add(cpu, HV_X64_MSR_VP_RUNTIME, env->msr_hv_runtime);
        }
        if (hyperv_feat_enabled(cpu, HYPERV_FEAT_VPINDEX)
            && hv_vpindex_settable) {
            kvm_msr_entry_add(cpu, HV_X64_MSR_VP_INDEX,
                              hyperv_vp_index(CPU(cpu)));
        }
        if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC)) {
            int j;

            kvm_msr_entry_add(cpu, HV_X64_MSR_SVERSION, HV_SYNIC_VERSION);

            kvm_msr_entry_add(cpu, HV_X64_MSR_SCONTROL,
                              env->msr_hv_synic_control);
            kvm_msr_entry_add(cpu, HV_X64_MSR_SIEFP,
                              env->msr_hv_synic_evt_page);
            kvm_msr_entry_add(cpu, HV_X64_MSR_SIMP,
                              env->msr_hv_synic_msg_page);

            for (j = 0; j < ARRAY_SIZE(env->msr_hv_synic_sint); j++) {
                kvm_msr_entry_add(cpu, HV_X64_MSR_SINT0 + j,
                                  env->msr_hv_synic_sint[j]);
            }
        }
        if (has_msr_hv_stimer) {
            int j;

            for (j = 0; j < ARRAY_SIZE(env->msr_hv_stimer_config); j++) {
                kvm_msr_entry_add(cpu, HV_X64_MSR_STIMER0_CONFIG + j * 2,
                                env->msr_hv_stimer_config[j]);
            }

            for (j = 0; j < ARRAY_SIZE(env->msr_hv_stimer_count); j++) {
                kvm_msr_entry_add(cpu, HV_X64_MSR_STIMER0_COUNT + j * 2,
                                env->msr_hv_stimer_count[j]);
            }
        }
        if (env->features[FEAT_1_EDX] & CPUID_MTRR) {
            uint64_t phys_mask = MAKE_64BIT_MASK(0, cpu->phys_bits);

            kvm_msr_entry_add(cpu, MSR_MTRRdefType, env->mtrr_deftype);
            kvm_msr_entry_add(cpu, MSR_MTRRfix64K_00000, env->mtrr_fixed[0]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix16K_80000, env->mtrr_fixed[1]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix16K_A0000, env->mtrr_fixed[2]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix4K_C0000, env->mtrr_fixed[3]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix4K_C8000, env->mtrr_fixed[4]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix4K_D0000, env->mtrr_fixed[5]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix4K_D8000, env->mtrr_fixed[6]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix4K_E0000, env->mtrr_fixed[7]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix4K_E8000, env->mtrr_fixed[8]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix4K_F0000, env->mtrr_fixed[9]);
            kvm_msr_entry_add(cpu, MSR_MTRRfix4K_F8000, env->mtrr_fixed[10]);
            for (i = 0; i < MSR_MTRRcap_VCNT; i++) {
                /* The CPU GPs if we write to a bit above the physical limit of
                 * the host CPU (and KVM emulates that)
                 */
                uint64_t mask = env->mtrr_var[i].mask;
                mask &= phys_mask;

                kvm_msr_entry_add(cpu, MSR_MTRRphysBase(i),
                                  env->mtrr_var[i].base);
                kvm_msr_entry_add(cpu, MSR_MTRRphysMask(i), mask);
            }
        }
        if (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_INTEL_PT) {
            int addr_num = kvm_arch_get_supported_cpuid(kvm_state,
                                                    0x14, 1, R_EAX) & 0x7;

            kvm_msr_entry_add(cpu, MSR_IA32_RTIT_CTL,
                            env->msr_rtit_ctrl);
            kvm_msr_entry_add(cpu, MSR_IA32_RTIT_STATUS,
                            env->msr_rtit_status);
            kvm_msr_entry_add(cpu, MSR_IA32_RTIT_OUTPUT_BASE,
                            env->msr_rtit_output_base);
            kvm_msr_entry_add(cpu, MSR_IA32_RTIT_OUTPUT_MASK,
                            env->msr_rtit_output_mask);
            kvm_msr_entry_add(cpu, MSR_IA32_RTIT_CR3_MATCH,
                            env->msr_rtit_cr3_match);
            for (i = 0; i < addr_num; i++) {
                kvm_msr_entry_add(cpu, MSR_IA32_RTIT_ADDR0_A + i,
                            env->msr_rtit_addrs[i]);
            }
        }

        if (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_SGX_LC) {
            kvm_msr_entry_add(cpu, MSR_IA32_SGXLEPUBKEYHASH0,
                              env->msr_ia32_sgxlepubkeyhash[0]);
            kvm_msr_entry_add(cpu, MSR_IA32_SGXLEPUBKEYHASH1,
                              env->msr_ia32_sgxlepubkeyhash[1]);
            kvm_msr_entry_add(cpu, MSR_IA32_SGXLEPUBKEYHASH2,
                              env->msr_ia32_sgxlepubkeyhash[2]);
            kvm_msr_entry_add(cpu, MSR_IA32_SGXLEPUBKEYHASH3,
                              env->msr_ia32_sgxlepubkeyhash[3]);
        }

        if (env->features[FEAT_XSAVE] & CPUID_D_1_EAX_XFD) {
            kvm_msr_entry_add(cpu, MSR_IA32_XFD,
                              env->msr_xfd);
            kvm_msr_entry_add(cpu, MSR_IA32_XFD_ERR,
                              env->msr_xfd_err);
        }

        if (kvm_enabled() && cpu->enable_pmu &&
            (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_ARCH_LBR)) {
            uint64_t depth;
            int ret;

            /*
             * Only migrate Arch LBR states when the host Arch LBR depth
             * equals that of source guest's, this is to avoid mismatch
             * of guest/host config for the msr hence avoid unexpected
             * misbehavior.
             */
            ret = kvm_get_one_msr(cpu, MSR_ARCH_LBR_DEPTH, &depth);

            if (ret == 1 && !!depth && depth == env->msr_lbr_depth) {
                kvm_msr_entry_add(cpu, MSR_ARCH_LBR_CTL, env->msr_lbr_ctl);
                kvm_msr_entry_add(cpu, MSR_ARCH_LBR_DEPTH, env->msr_lbr_depth);

                for (i = 0; i < ARCH_LBR_NR_ENTRIES; i++) {
                    if (!env->lbr_records[i].from) {
                        continue;
                    }
                    kvm_msr_entry_add(cpu, MSR_ARCH_LBR_FROM_0 + i,
                                      env->lbr_records[i].from);
                    kvm_msr_entry_add(cpu, MSR_ARCH_LBR_TO_0 + i,
                                      env->lbr_records[i].to);
                    kvm_msr_entry_add(cpu, MSR_ARCH_LBR_INFO_0 + i,
                                      env->lbr_records[i].info);
                }
            }
        }

        /* Note: MSR_IA32_FEATURE_CONTROL is written separately, see
         *       kvm_put_msr_feature_control. */
    }

    if (env->mcg_cap) {
        kvm_msr_entry_add(cpu, MSR_MCG_STATUS, env->mcg_status);
        kvm_msr_entry_add(cpu, MSR_MCG_CTL, env->mcg_ctl);
        if (has_msr_mcg_ext_ctl) {
            kvm_msr_entry_add(cpu, MSR_MCG_EXT_CTL, env->mcg_ext_ctl);
        }
        for (i = 0; i < (env->mcg_cap & 0xff) * 4; i++) {
            kvm_msr_entry_add(cpu, MSR_MC0_CTL + i, env->mce_banks[i]);
        }
    }

    return kvm_buf_set_msrs(cpu);
}


static int kvm_get_xsave(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    void *xsave = env->xsave_buf;
    int type, ret;

    type = has_xsave2 ? KVM_GET_XSAVE2 : KVM_GET_XSAVE;
    ret = kvm_vcpu_ioctl(CPU(cpu), type, xsave);
    if (ret < 0) {
        return ret;
    }
    x86_cpu_xrstor_all_areas(cpu, xsave, env->xsave_buf_len);

    return 0;
}

static int kvm_get_xcrs(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    int i, ret;
    struct kvm_xcrs xcrs;

    if (!has_xcrs) {
        return 0;
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_XCRS, &xcrs);
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < xcrs.nr_xcrs; i++) {
        /* Only support xcr0 now */
        if (xcrs.xcrs[i].xcr == 0) {
            env->xcr0 = xcrs.xcrs[i].value;
            break;
        }
    }
    return 0;
}

static int kvm_get_sregs(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_sregs sregs;
    int ret;

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_SREGS, &sregs);
    if (ret < 0) {
        return ret;
    }

    /*
     * The interrupt_bitmap is ignored because KVM_GET_SREGS is
     * always preceded by KVM_GET_VCPU_EVENTS.
     */

    get_seg(&env->segs[R_CS], &sregs.cs);
    get_seg(&env->segs[R_DS], &sregs.ds);
    get_seg(&env->segs[R_ES], &sregs.es);
    get_seg(&env->segs[R_FS], &sregs.fs);
    get_seg(&env->segs[R_GS], &sregs.gs);
    get_seg(&env->segs[R_SS], &sregs.ss);

    get_seg(&env->tr, &sregs.tr);
    get_seg(&env->ldt, &sregs.ldt);

    env->idt.limit = sregs.idt.limit;
    env->idt.base = sregs.idt.base;
    env->gdt.limit = sregs.gdt.limit;
    env->gdt.base = sregs.gdt.base;

    env->cr[0] = sregs.cr0;
    env->cr[2] = sregs.cr2;
    env->cr[3] = sregs.cr3;
    env->cr[4] = sregs.cr4;

    env->efer = sregs.efer;
    if (sev_es_enabled() && env->efer & MSR_EFER_LME &&
        env->cr[0] & CR0_PG_MASK) {
        env->efer |= MSR_EFER_LMA;
    }

    /* changes to apic base and cr8/tpr are read back via kvm_arch_post_run */
    x86_update_hflags(env);

    return 0;
}

static int kvm_get_sregs2(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_sregs2 sregs;
    int i, ret;

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_SREGS2, &sregs);
    if (ret < 0) {
        return ret;
    }

    get_seg(&env->segs[R_CS], &sregs.cs);
    get_seg(&env->segs[R_DS], &sregs.ds);
    get_seg(&env->segs[R_ES], &sregs.es);
    get_seg(&env->segs[R_FS], &sregs.fs);
    get_seg(&env->segs[R_GS], &sregs.gs);
    get_seg(&env->segs[R_SS], &sregs.ss);

    get_seg(&env->tr, &sregs.tr);
    get_seg(&env->ldt, &sregs.ldt);

    env->idt.limit = sregs.idt.limit;
    env->idt.base = sregs.idt.base;
    env->gdt.limit = sregs.gdt.limit;
    env->gdt.base = sregs.gdt.base;

    env->cr[0] = sregs.cr0;
    env->cr[2] = sregs.cr2;
    env->cr[3] = sregs.cr3;
    env->cr[4] = sregs.cr4;

    env->efer = sregs.efer;
    if (sev_es_enabled() && env->efer & MSR_EFER_LME &&
        env->cr[0] & CR0_PG_MASK) {
        env->efer |= MSR_EFER_LMA;
    }

    env->pdptrs_valid = sregs.flags & KVM_SREGS2_FLAGS_PDPTRS_VALID;

    if (env->pdptrs_valid) {
        for (i = 0; i < 4; i++) {
            env->pdptrs[i] = sregs.pdptrs[i];
        }
    }

    /* changes to apic base and cr8/tpr are read back via kvm_arch_post_run */
    x86_update_hflags(env);

    return 0;
}

static int kvm_get_msrs(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_msr_entry *msrs = cpu->kvm_msr_buf->entries;
    int ret, i;
    uint64_t mtrr_top_bits;

    kvm_msr_buf_reset(cpu);

    kvm_msr_entry_add(cpu, MSR_IA32_SYSENTER_CS, 0);
    kvm_msr_entry_add(cpu, MSR_IA32_SYSENTER_ESP, 0);
    kvm_msr_entry_add(cpu, MSR_IA32_SYSENTER_EIP, 0);
    kvm_msr_entry_add(cpu, MSR_PAT, 0);
    if (has_msr_star) {
        kvm_msr_entry_add(cpu, MSR_STAR, 0);
    }
    if (has_msr_hsave_pa) {
        kvm_msr_entry_add(cpu, MSR_VM_HSAVE_PA, 0);
    }
    if (has_msr_tsc_aux) {
        kvm_msr_entry_add(cpu, MSR_TSC_AUX, 0);
    }
    if (has_msr_tsc_adjust) {
        kvm_msr_entry_add(cpu, MSR_TSC_ADJUST, 0);
    }
    if (has_msr_tsc_deadline) {
        kvm_msr_entry_add(cpu, MSR_IA32_TSCDEADLINE, 0);
    }
    if (has_msr_misc_enable) {
        kvm_msr_entry_add(cpu, MSR_IA32_MISC_ENABLE, 0);
    }
    if (has_msr_smbase) {
        kvm_msr_entry_add(cpu, MSR_IA32_SMBASE, 0);
    }
    if (has_msr_smi_count) {
        kvm_msr_entry_add(cpu, MSR_SMI_COUNT, 0);
    }
    if (has_msr_feature_control) {
        kvm_msr_entry_add(cpu, MSR_IA32_FEATURE_CONTROL, 0);
    }
    if (has_msr_pkrs) {
        kvm_msr_entry_add(cpu, MSR_IA32_PKRS, 0);
    }
    if (has_msr_bndcfgs) {
        kvm_msr_entry_add(cpu, MSR_IA32_BNDCFGS, 0);
    }
    if (has_msr_xss) {
        kvm_msr_entry_add(cpu, MSR_IA32_XSS, 0);
    }
    if (has_msr_umwait) {
        kvm_msr_entry_add(cpu, MSR_IA32_UMWAIT_CONTROL, 0);
    }
    if (has_msr_spec_ctrl) {
        kvm_msr_entry_add(cpu, MSR_IA32_SPEC_CTRL, 0);
    }
    if (has_tsc_scale_msr) {
        kvm_msr_entry_add(cpu, MSR_AMD64_TSC_RATIO, 0);
    }

    if (has_msr_tsx_ctrl) {
        kvm_msr_entry_add(cpu, MSR_IA32_TSX_CTRL, 0);
    }
    if (has_msr_virt_ssbd) {
        kvm_msr_entry_add(cpu, MSR_VIRT_SSBD, 0);
    }
    if (!env->tsc_valid) {
        kvm_msr_entry_add(cpu, MSR_IA32_TSC, 0);
        env->tsc_valid = !runstate_is_running();
    }

#ifdef TARGET_X86_64
    if (lm_capable_kernel) {
        kvm_msr_entry_add(cpu, MSR_CSTAR, 0);
        kvm_msr_entry_add(cpu, MSR_KERNELGSBASE, 0);
        kvm_msr_entry_add(cpu, MSR_FMASK, 0);
        kvm_msr_entry_add(cpu, MSR_LSTAR, 0);
        if (env->features[FEAT_7_1_EAX] & CPUID_7_1_EAX_FRED) {
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_RSP0, 0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_RSP1, 0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_RSP2, 0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_RSP3, 0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_STKLVLS, 0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_SSP1, 0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_SSP2, 0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_SSP3, 0);
            kvm_msr_entry_add(cpu, MSR_IA32_FRED_CONFIG, 0);
        }
    }
#endif
    kvm_msr_entry_add(cpu, MSR_KVM_SYSTEM_TIME, 0);
    kvm_msr_entry_add(cpu, MSR_KVM_WALL_CLOCK, 0);
    if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_ASYNC_PF_INT)) {
        kvm_msr_entry_add(cpu, MSR_KVM_ASYNC_PF_INT, 0);
    }
    if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_ASYNC_PF)) {
        kvm_msr_entry_add(cpu, MSR_KVM_ASYNC_PF_EN, 0);
    }
    if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_PV_EOI)) {
        kvm_msr_entry_add(cpu, MSR_KVM_PV_EOI_EN, 0);
    }
    if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_STEAL_TIME)) {
        kvm_msr_entry_add(cpu, MSR_KVM_STEAL_TIME, 0);
    }
    if (env->features[FEAT_KVM] & (1 << KVM_FEATURE_POLL_CONTROL)) {
        kvm_msr_entry_add(cpu, MSR_KVM_POLL_CONTROL, 1);
    }
    if (has_architectural_pmu_version > 0) {
        if (has_architectural_pmu_version > 1) {
            kvm_msr_entry_add(cpu, MSR_CORE_PERF_FIXED_CTR_CTRL, 0);
            kvm_msr_entry_add(cpu, MSR_CORE_PERF_GLOBAL_CTRL, 0);
            kvm_msr_entry_add(cpu, MSR_CORE_PERF_GLOBAL_STATUS, 0);
            kvm_msr_entry_add(cpu, MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0);
        }
        for (i = 0; i < num_architectural_pmu_fixed_counters; i++) {
            kvm_msr_entry_add(cpu, MSR_CORE_PERF_FIXED_CTR0 + i, 0);
        }
        for (i = 0; i < num_architectural_pmu_gp_counters; i++) {
            kvm_msr_entry_add(cpu, MSR_P6_PERFCTR0 + i, 0);
            kvm_msr_entry_add(cpu, MSR_P6_EVNTSEL0 + i, 0);
        }
    }

    if (env->mcg_cap) {
        kvm_msr_entry_add(cpu, MSR_MCG_STATUS, 0);
        kvm_msr_entry_add(cpu, MSR_MCG_CTL, 0);
        if (has_msr_mcg_ext_ctl) {
            kvm_msr_entry_add(cpu, MSR_MCG_EXT_CTL, 0);
        }
        for (i = 0; i < (env->mcg_cap & 0xff) * 4; i++) {
            kvm_msr_entry_add(cpu, MSR_MC0_CTL + i, 0);
        }
    }

    if (has_msr_hv_hypercall) {
        kvm_msr_entry_add(cpu, HV_X64_MSR_HYPERCALL, 0);
        kvm_msr_entry_add(cpu, HV_X64_MSR_GUEST_OS_ID, 0);
    }
    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_VAPIC)) {
        kvm_msr_entry_add(cpu, HV_X64_MSR_APIC_ASSIST_PAGE, 0);
    }
    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_TIME)) {
        kvm_msr_entry_add(cpu, HV_X64_MSR_REFERENCE_TSC, 0);
    }
    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_REENLIGHTENMENT)) {
        kvm_msr_entry_add(cpu, HV_X64_MSR_REENLIGHTENMENT_CONTROL, 0);
        kvm_msr_entry_add(cpu, HV_X64_MSR_TSC_EMULATION_CONTROL, 0);
        kvm_msr_entry_add(cpu, HV_X64_MSR_TSC_EMULATION_STATUS, 0);
    }
    if (has_msr_hv_syndbg_options) {
        kvm_msr_entry_add(cpu, HV_X64_MSR_SYNDBG_OPTIONS, 0);
    }
    if (has_msr_hv_crash) {
        int j;

        for (j = 0; j < HV_CRASH_PARAMS; j++) {
            kvm_msr_entry_add(cpu, HV_X64_MSR_CRASH_P0 + j, 0);
        }
    }
    if (has_msr_hv_runtime) {
        kvm_msr_entry_add(cpu, HV_X64_MSR_VP_RUNTIME, 0);
    }
    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC)) {
        uint32_t msr;

        kvm_msr_entry_add(cpu, HV_X64_MSR_SCONTROL, 0);
        kvm_msr_entry_add(cpu, HV_X64_MSR_SIEFP, 0);
        kvm_msr_entry_add(cpu, HV_X64_MSR_SIMP, 0);
        for (msr = HV_X64_MSR_SINT0; msr <= HV_X64_MSR_SINT15; msr++) {
            kvm_msr_entry_add(cpu, msr, 0);
        }
    }
    if (has_msr_hv_stimer) {
        uint32_t msr;

        for (msr = HV_X64_MSR_STIMER0_CONFIG; msr <= HV_X64_MSR_STIMER3_COUNT;
             msr++) {
            kvm_msr_entry_add(cpu, msr, 0);
        }
    }
    if (env->features[FEAT_1_EDX] & CPUID_MTRR) {
        kvm_msr_entry_add(cpu, MSR_MTRRdefType, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix64K_00000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix16K_80000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix16K_A0000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix4K_C0000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix4K_C8000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix4K_D0000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix4K_D8000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix4K_E0000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix4K_E8000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix4K_F0000, 0);
        kvm_msr_entry_add(cpu, MSR_MTRRfix4K_F8000, 0);
        for (i = 0; i < MSR_MTRRcap_VCNT; i++) {
            kvm_msr_entry_add(cpu, MSR_MTRRphysBase(i), 0);
            kvm_msr_entry_add(cpu, MSR_MTRRphysMask(i), 0);
        }
    }

    if (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_INTEL_PT) {
        int addr_num =
            kvm_arch_get_supported_cpuid(kvm_state, 0x14, 1, R_EAX) & 0x7;

        kvm_msr_entry_add(cpu, MSR_IA32_RTIT_CTL, 0);
        kvm_msr_entry_add(cpu, MSR_IA32_RTIT_STATUS, 0);
        kvm_msr_entry_add(cpu, MSR_IA32_RTIT_OUTPUT_BASE, 0);
        kvm_msr_entry_add(cpu, MSR_IA32_RTIT_OUTPUT_MASK, 0);
        kvm_msr_entry_add(cpu, MSR_IA32_RTIT_CR3_MATCH, 0);
        for (i = 0; i < addr_num; i++) {
            kvm_msr_entry_add(cpu, MSR_IA32_RTIT_ADDR0_A + i, 0);
        }
    }

    if (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_SGX_LC) {
        kvm_msr_entry_add(cpu, MSR_IA32_SGXLEPUBKEYHASH0, 0);
        kvm_msr_entry_add(cpu, MSR_IA32_SGXLEPUBKEYHASH1, 0);
        kvm_msr_entry_add(cpu, MSR_IA32_SGXLEPUBKEYHASH2, 0);
        kvm_msr_entry_add(cpu, MSR_IA32_SGXLEPUBKEYHASH3, 0);
    }

    if (env->features[FEAT_XSAVE] & CPUID_D_1_EAX_XFD) {
        kvm_msr_entry_add(cpu, MSR_IA32_XFD, 0);
        kvm_msr_entry_add(cpu, MSR_IA32_XFD_ERR, 0);
    }

    if (kvm_enabled() && cpu->enable_pmu &&
        (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_ARCH_LBR)) {
        uint64_t depth;

        ret = kvm_get_one_msr(cpu, MSR_ARCH_LBR_DEPTH, &depth);
        if (ret == 1 && depth == ARCH_LBR_NR_ENTRIES) {
            kvm_msr_entry_add(cpu, MSR_ARCH_LBR_CTL, 0);
            kvm_msr_entry_add(cpu, MSR_ARCH_LBR_DEPTH, 0);

            for (i = 0; i < ARCH_LBR_NR_ENTRIES; i++) {
                kvm_msr_entry_add(cpu, MSR_ARCH_LBR_FROM_0 + i, 0);
                kvm_msr_entry_add(cpu, MSR_ARCH_LBR_TO_0 + i, 0);
                kvm_msr_entry_add(cpu, MSR_ARCH_LBR_INFO_0 + i, 0);
            }
        }
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_MSRS, cpu->kvm_msr_buf);
    if (ret < 0) {
        return ret;
    }

    if (ret < cpu->kvm_msr_buf->nmsrs) {
        struct kvm_msr_entry *e = &cpu->kvm_msr_buf->entries[ret];
        error_report("error: failed to get MSR 0x%" PRIx32,
                     (uint32_t)e->index);
    }

    assert(ret == cpu->kvm_msr_buf->nmsrs);
    /*
     * MTRR masks: Each mask consists of 5 parts
     * a  10..0: must be zero
     * b  11   : valid bit
     * c n-1.12: actual mask bits
     * d  51..n: reserved must be zero
     * e  63.52: reserved must be zero
     *
     * 'n' is the number of physical bits supported by the CPU and is
     * apparently always <= 52.   We know our 'n' but don't know what
     * the destinations 'n' is; it might be smaller, in which case
     * it masks (c) on loading. It might be larger, in which case
     * we fill 'd' so that d..c is consistent irrespetive of the 'n'
     * we're migrating to.
     */

    if (cpu->fill_mtrr_mask) {
        QEMU_BUILD_BUG_ON(TARGET_PHYS_ADDR_SPACE_BITS > 52);
        assert(cpu->phys_bits <= TARGET_PHYS_ADDR_SPACE_BITS);
        mtrr_top_bits = MAKE_64BIT_MASK(cpu->phys_bits, 52 - cpu->phys_bits);
    } else {
        mtrr_top_bits = 0;
    }

    for (i = 0; i < ret; i++) {
        uint32_t index = msrs[i].index;
        switch (index) {
        case MSR_IA32_SYSENTER_CS:
            env->sysenter_cs = msrs[i].data;
            break;
        case MSR_IA32_SYSENTER_ESP:
            env->sysenter_esp = msrs[i].data;
            break;
        case MSR_IA32_SYSENTER_EIP:
            env->sysenter_eip = msrs[i].data;
            break;
        case MSR_PAT:
            env->pat = msrs[i].data;
            break;
        case MSR_STAR:
            env->star = msrs[i].data;
            break;
#ifdef TARGET_X86_64
        case MSR_CSTAR:
            env->cstar = msrs[i].data;
            break;
        case MSR_KERNELGSBASE:
            env->kernelgsbase = msrs[i].data;
            break;
        case MSR_FMASK:
            env->fmask = msrs[i].data;
            break;
        case MSR_LSTAR:
            env->lstar = msrs[i].data;
            break;
        case MSR_IA32_FRED_RSP0:
            env->fred_rsp0 = msrs[i].data;
            break;
        case MSR_IA32_FRED_RSP1:
            env->fred_rsp1 = msrs[i].data;
            break;
        case MSR_IA32_FRED_RSP2:
            env->fred_rsp2 = msrs[i].data;
            break;
        case MSR_IA32_FRED_RSP3:
            env->fred_rsp3 = msrs[i].data;
            break;
        case MSR_IA32_FRED_STKLVLS:
            env->fred_stklvls = msrs[i].data;
            break;
        case MSR_IA32_FRED_SSP1:
            env->fred_ssp1 = msrs[i].data;
            break;
        case MSR_IA32_FRED_SSP2:
            env->fred_ssp2 = msrs[i].data;
            break;
        case MSR_IA32_FRED_SSP3:
            env->fred_ssp3 = msrs[i].data;
            break;
        case MSR_IA32_FRED_CONFIG:
            env->fred_config = msrs[i].data;
            break;
#endif
        case MSR_IA32_TSC:
            env->tsc = msrs[i].data;
            break;
        case MSR_TSC_AUX:
            env->tsc_aux = msrs[i].data;
            break;
        case MSR_TSC_ADJUST:
            env->tsc_adjust = msrs[i].data;
            break;
        case MSR_IA32_TSCDEADLINE:
            env->tsc_deadline = msrs[i].data;
            break;
        case MSR_VM_HSAVE_PA:
            env->vm_hsave = msrs[i].data;
            break;
        case MSR_KVM_SYSTEM_TIME:
            env->system_time_msr = msrs[i].data;
            break;
        case MSR_KVM_WALL_CLOCK:
            env->wall_clock_msr = msrs[i].data;
            break;
        case MSR_MCG_STATUS:
            env->mcg_status = msrs[i].data;
            break;
        case MSR_MCG_CTL:
            env->mcg_ctl = msrs[i].data;
            break;
        case MSR_MCG_EXT_CTL:
            env->mcg_ext_ctl = msrs[i].data;
            break;
        case MSR_IA32_MISC_ENABLE:
            env->msr_ia32_misc_enable = msrs[i].data;
            break;
        case MSR_IA32_SMBASE:
            env->smbase = msrs[i].data;
            break;
        case MSR_SMI_COUNT:
            env->msr_smi_count = msrs[i].data;
            break;
        case MSR_IA32_FEATURE_CONTROL:
            env->msr_ia32_feature_control = msrs[i].data;
            break;
        case MSR_IA32_BNDCFGS:
            env->msr_bndcfgs = msrs[i].data;
            break;
        case MSR_IA32_XSS:
            env->xss = msrs[i].data;
            break;
        case MSR_IA32_UMWAIT_CONTROL:
            env->umwait = msrs[i].data;
            break;
        case MSR_IA32_PKRS:
            env->pkrs = msrs[i].data;
            break;
        default:
            if (msrs[i].index >= MSR_MC0_CTL &&
                msrs[i].index < MSR_MC0_CTL + (env->mcg_cap & 0xff) * 4) {
                env->mce_banks[msrs[i].index - MSR_MC0_CTL] = msrs[i].data;
            }
            break;
        case MSR_KVM_ASYNC_PF_EN:
            env->async_pf_en_msr = msrs[i].data;
            break;
        case MSR_KVM_ASYNC_PF_INT:
            env->async_pf_int_msr = msrs[i].data;
            break;
        case MSR_KVM_PV_EOI_EN:
            env->pv_eoi_en_msr = msrs[i].data;
            break;
        case MSR_KVM_STEAL_TIME:
            env->steal_time_msr = msrs[i].data;
            break;
        case MSR_KVM_POLL_CONTROL: {
            env->poll_control_msr = msrs[i].data;
            break;
        }
        case MSR_CORE_PERF_FIXED_CTR_CTRL:
            env->msr_fixed_ctr_ctrl = msrs[i].data;
            break;
        case MSR_CORE_PERF_GLOBAL_CTRL:
            env->msr_global_ctrl = msrs[i].data;
            break;
        case MSR_CORE_PERF_GLOBAL_STATUS:
            env->msr_global_status = msrs[i].data;
            break;
        case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
            env->msr_global_ovf_ctrl = msrs[i].data;
            break;
        case MSR_CORE_PERF_FIXED_CTR0 ... MSR_CORE_PERF_FIXED_CTR0 + MAX_FIXED_COUNTERS - 1:
            env->msr_fixed_counters[index - MSR_CORE_PERF_FIXED_CTR0] = msrs[i].data;
            break;
        case MSR_P6_PERFCTR0 ... MSR_P6_PERFCTR0 + MAX_GP_COUNTERS - 1:
            env->msr_gp_counters[index - MSR_P6_PERFCTR0] = msrs[i].data;
            break;
        case MSR_P6_EVNTSEL0 ... MSR_P6_EVNTSEL0 + MAX_GP_COUNTERS - 1:
            env->msr_gp_evtsel[index - MSR_P6_EVNTSEL0] = msrs[i].data;
            break;
        case HV_X64_MSR_HYPERCALL:
            env->msr_hv_hypercall = msrs[i].data;
            break;
        case HV_X64_MSR_GUEST_OS_ID:
            env->msr_hv_guest_os_id = msrs[i].data;
            break;
        case HV_X64_MSR_APIC_ASSIST_PAGE:
            env->msr_hv_vapic = msrs[i].data;
            break;
        case HV_X64_MSR_REFERENCE_TSC:
            env->msr_hv_tsc = msrs[i].data;
            break;
        case HV_X64_MSR_CRASH_P0 ... HV_X64_MSR_CRASH_P4:
            env->msr_hv_crash_params[index - HV_X64_MSR_CRASH_P0] = msrs[i].data;
            break;
        case HV_X64_MSR_VP_RUNTIME:
            env->msr_hv_runtime = msrs[i].data;
            break;
        case HV_X64_MSR_SCONTROL:
            env->msr_hv_synic_control = msrs[i].data;
            break;
        case HV_X64_MSR_SIEFP:
            env->msr_hv_synic_evt_page = msrs[i].data;
            break;
        case HV_X64_MSR_SIMP:
            env->msr_hv_synic_msg_page = msrs[i].data;
            break;
        case HV_X64_MSR_SINT0 ... HV_X64_MSR_SINT15:
            env->msr_hv_synic_sint[index - HV_X64_MSR_SINT0] = msrs[i].data;
            break;
        case HV_X64_MSR_STIMER0_CONFIG:
        case HV_X64_MSR_STIMER1_CONFIG:
        case HV_X64_MSR_STIMER2_CONFIG:
        case HV_X64_MSR_STIMER3_CONFIG:
            env->msr_hv_stimer_config[(index - HV_X64_MSR_STIMER0_CONFIG)/2] =
                                msrs[i].data;
            break;
        case HV_X64_MSR_STIMER0_COUNT:
        case HV_X64_MSR_STIMER1_COUNT:
        case HV_X64_MSR_STIMER2_COUNT:
        case HV_X64_MSR_STIMER3_COUNT:
            env->msr_hv_stimer_count[(index - HV_X64_MSR_STIMER0_COUNT)/2] =
                                msrs[i].data;
            break;
        case HV_X64_MSR_REENLIGHTENMENT_CONTROL:
            env->msr_hv_reenlightenment_control = msrs[i].data;
            break;
        case HV_X64_MSR_TSC_EMULATION_CONTROL:
            env->msr_hv_tsc_emulation_control = msrs[i].data;
            break;
        case HV_X64_MSR_TSC_EMULATION_STATUS:
            env->msr_hv_tsc_emulation_status = msrs[i].data;
            break;
        case HV_X64_MSR_SYNDBG_OPTIONS:
            env->msr_hv_syndbg_options = msrs[i].data;
            break;
        case MSR_MTRRdefType:
            env->mtrr_deftype = msrs[i].data;
            break;
        case MSR_MTRRfix64K_00000:
            env->mtrr_fixed[0] = msrs[i].data;
            break;
        case MSR_MTRRfix16K_80000:
            env->mtrr_fixed[1] = msrs[i].data;
            break;
        case MSR_MTRRfix16K_A0000:
            env->mtrr_fixed[2] = msrs[i].data;
            break;
        case MSR_MTRRfix4K_C0000:
            env->mtrr_fixed[3] = msrs[i].data;
            break;
        case MSR_MTRRfix4K_C8000:
            env->mtrr_fixed[4] = msrs[i].data;
            break;
        case MSR_MTRRfix4K_D0000:
            env->mtrr_fixed[5] = msrs[i].data;
            break;
        case MSR_MTRRfix4K_D8000:
            env->mtrr_fixed[6] = msrs[i].data;
            break;
        case MSR_MTRRfix4K_E0000:
            env->mtrr_fixed[7] = msrs[i].data;
            break;
        case MSR_MTRRfix4K_E8000:
            env->mtrr_fixed[8] = msrs[i].data;
            break;
        case MSR_MTRRfix4K_F0000:
            env->mtrr_fixed[9] = msrs[i].data;
            break;
        case MSR_MTRRfix4K_F8000:
            env->mtrr_fixed[10] = msrs[i].data;
            break;
        case MSR_MTRRphysBase(0) ... MSR_MTRRphysMask(MSR_MTRRcap_VCNT - 1):
            if (index & 1) {
                env->mtrr_var[MSR_MTRRphysIndex(index)].mask = msrs[i].data |
                                                               mtrr_top_bits;
            } else {
                env->mtrr_var[MSR_MTRRphysIndex(index)].base = msrs[i].data;
            }
            break;
        case MSR_IA32_SPEC_CTRL:
            env->spec_ctrl = msrs[i].data;
            break;
        case MSR_AMD64_TSC_RATIO:
            env->amd_tsc_scale_msr = msrs[i].data;
            break;
        case MSR_IA32_TSX_CTRL:
            env->tsx_ctrl = msrs[i].data;
            break;
        case MSR_VIRT_SSBD:
            env->virt_ssbd = msrs[i].data;
            break;
        case MSR_IA32_RTIT_CTL:
            env->msr_rtit_ctrl = msrs[i].data;
            break;
        case MSR_IA32_RTIT_STATUS:
            env->msr_rtit_status = msrs[i].data;
            break;
        case MSR_IA32_RTIT_OUTPUT_BASE:
            env->msr_rtit_output_base = msrs[i].data;
            break;
        case MSR_IA32_RTIT_OUTPUT_MASK:
            env->msr_rtit_output_mask = msrs[i].data;
            break;
        case MSR_IA32_RTIT_CR3_MATCH:
            env->msr_rtit_cr3_match = msrs[i].data;
            break;
        case MSR_IA32_RTIT_ADDR0_A ... MSR_IA32_RTIT_ADDR3_B:
            env->msr_rtit_addrs[index - MSR_IA32_RTIT_ADDR0_A] = msrs[i].data;
            break;
        case MSR_IA32_SGXLEPUBKEYHASH0 ... MSR_IA32_SGXLEPUBKEYHASH3:
            env->msr_ia32_sgxlepubkeyhash[index - MSR_IA32_SGXLEPUBKEYHASH0] =
                           msrs[i].data;
            break;
        case MSR_IA32_XFD:
            env->msr_xfd = msrs[i].data;
            break;
        case MSR_IA32_XFD_ERR:
            env->msr_xfd_err = msrs[i].data;
            break;
        case MSR_ARCH_LBR_CTL:
            env->msr_lbr_ctl = msrs[i].data;
            break;
        case MSR_ARCH_LBR_DEPTH:
            env->msr_lbr_depth = msrs[i].data;
            break;
        case MSR_ARCH_LBR_FROM_0 ... MSR_ARCH_LBR_FROM_0 + 31:
            env->lbr_records[index - MSR_ARCH_LBR_FROM_0].from = msrs[i].data;
            break;
        case MSR_ARCH_LBR_TO_0 ... MSR_ARCH_LBR_TO_0 + 31:
            env->lbr_records[index - MSR_ARCH_LBR_TO_0].to = msrs[i].data;
            break;
        case MSR_ARCH_LBR_INFO_0 ... MSR_ARCH_LBR_INFO_0 + 31:
            env->lbr_records[index - MSR_ARCH_LBR_INFO_0].info = msrs[i].data;
            break;
        }
    }

    return 0;
}

static int kvm_put_mp_state(X86CPU *cpu)
{
    struct kvm_mp_state mp_state = { .mp_state = cpu->env.mp_state };

    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_MP_STATE, &mp_state);
}

static int kvm_get_mp_state(X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;
    struct kvm_mp_state mp_state;
    int ret;

    ret = kvm_vcpu_ioctl(cs, KVM_GET_MP_STATE, &mp_state);
    if (ret < 0) {
        return ret;
    }
    env->mp_state = mp_state.mp_state;
    if (kvm_irqchip_in_kernel()) {
        cs->halted = (mp_state.mp_state == KVM_MP_STATE_HALTED);
    }
    return 0;
}

static int kvm_get_apic(X86CPU *cpu)
{
    DeviceState *apic = cpu->apic_state;
    struct kvm_lapic_state kapic;
    int ret;

    if (apic && kvm_irqchip_in_kernel()) {
        ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_LAPIC, &kapic);
        if (ret < 0) {
            return ret;
        }

        kvm_get_apic_state(apic, &kapic);
    }
    return 0;
}

static int kvm_put_vcpu_events(X86CPU *cpu, int level)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;
    struct kvm_vcpu_events events = {};

    events.flags = 0;

    if (has_exception_payload) {
        events.flags |= KVM_VCPUEVENT_VALID_PAYLOAD;
        events.exception.pending = env->exception_pending;
        events.exception_has_payload = env->exception_has_payload;
        events.exception_payload = env->exception_payload;
    }
    events.exception.nr = env->exception_nr;
    events.exception.injected = env->exception_injected;
    events.exception.has_error_code = env->has_error_code;
    events.exception.error_code = env->error_code;

    events.interrupt.injected = (env->interrupt_injected >= 0);
    events.interrupt.nr = env->interrupt_injected;
    events.interrupt.soft = env->soft_interrupt;

    events.nmi.injected = env->nmi_injected;
    events.nmi.pending = env->nmi_pending;
    events.nmi.masked = !!(env->hflags2 & HF2_NMI_MASK);

    events.sipi_vector = env->sipi_vector;

    if (has_msr_smbase) {
        events.flags |= KVM_VCPUEVENT_VALID_SMM;
        events.smi.smm = !!(env->hflags & HF_SMM_MASK);
        events.smi.smm_inside_nmi = !!(env->hflags2 & HF2_SMM_INSIDE_NMI_MASK);
        if (kvm_irqchip_in_kernel()) {
            /* As soon as these are moved to the kernel, remove them
             * from cs->interrupt_request.
             */
            events.smi.pending = cs->interrupt_request & CPU_INTERRUPT_SMI;
            events.smi.latched_init = cs->interrupt_request & CPU_INTERRUPT_INIT;
            cs->interrupt_request &= ~(CPU_INTERRUPT_INIT | CPU_INTERRUPT_SMI);
        } else {
            /* Keep these in cs->interrupt_request.  */
            events.smi.pending = 0;
            events.smi.latched_init = 0;
        }
    }

    if (level >= KVM_PUT_RESET_STATE) {
        events.flags |= KVM_VCPUEVENT_VALID_NMI_PENDING;
        if (env->mp_state == KVM_MP_STATE_SIPI_RECEIVED) {
            events.flags |= KVM_VCPUEVENT_VALID_SIPI_VECTOR;
        }
    }

    if (has_triple_fault_event) {
        events.flags |= KVM_VCPUEVENT_VALID_TRIPLE_FAULT;
        events.triple_fault.pending = env->triple_fault_pending;
    }

    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_VCPU_EVENTS, &events);
}

static int kvm_get_vcpu_events(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_vcpu_events events;
    int ret;

    memset(&events, 0, sizeof(events));
    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_VCPU_EVENTS, &events);
    if (ret < 0) {
       return ret;
    }

    if (events.flags & KVM_VCPUEVENT_VALID_PAYLOAD) {
        env->exception_pending = events.exception.pending;
        env->exception_has_payload = events.exception_has_payload;
        env->exception_payload = events.exception_payload;
    } else {
        env->exception_pending = 0;
        env->exception_has_payload = false;
    }
    env->exception_injected = events.exception.injected;
    env->exception_nr =
        (env->exception_pending || env->exception_injected) ?
        events.exception.nr : -1;
    env->has_error_code = events.exception.has_error_code;
    env->error_code = events.exception.error_code;

    env->interrupt_injected =
        events.interrupt.injected ? events.interrupt.nr : -1;
    env->soft_interrupt = events.interrupt.soft;

    env->nmi_injected = events.nmi.injected;
    env->nmi_pending = events.nmi.pending;
    if (events.nmi.masked) {
        env->hflags2 |= HF2_NMI_MASK;
    } else {
        env->hflags2 &= ~HF2_NMI_MASK;
    }

    if (events.flags & KVM_VCPUEVENT_VALID_SMM) {
        if (events.smi.smm) {
            env->hflags |= HF_SMM_MASK;
        } else {
            env->hflags &= ~HF_SMM_MASK;
        }
        if (events.smi.pending) {
            cpu_interrupt(CPU(cpu), CPU_INTERRUPT_SMI);
        } else {
            cpu_reset_interrupt(CPU(cpu), CPU_INTERRUPT_SMI);
        }
        if (events.smi.smm_inside_nmi) {
            env->hflags2 |= HF2_SMM_INSIDE_NMI_MASK;
        } else {
            env->hflags2 &= ~HF2_SMM_INSIDE_NMI_MASK;
        }
        if (events.smi.latched_init) {
            cpu_interrupt(CPU(cpu), CPU_INTERRUPT_INIT);
        } else {
            cpu_reset_interrupt(CPU(cpu), CPU_INTERRUPT_INIT);
        }
    }

    if (events.flags & KVM_VCPUEVENT_VALID_TRIPLE_FAULT) {
        env->triple_fault_pending = events.triple_fault.pending;
    }

    env->sipi_vector = events.sipi_vector;

    return 0;
}

static int kvm_put_debugregs(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_debugregs dbgregs;
    int i;

    memset(&dbgregs, 0, sizeof(dbgregs));
    for (i = 0; i < 4; i++) {
        dbgregs.db[i] = env->dr[i];
    }
    dbgregs.dr6 = env->dr[6];
    dbgregs.dr7 = env->dr[7];
    dbgregs.flags = 0;

    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_DEBUGREGS, &dbgregs);
}

static int kvm_get_debugregs(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_debugregs dbgregs;
    int i, ret;

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_DEBUGREGS, &dbgregs);
    if (ret < 0) {
        return ret;
    }
    for (i = 0; i < 4; i++) {
        env->dr[i] = dbgregs.db[i];
    }
    env->dr[4] = env->dr[6] = dbgregs.dr6;
    env->dr[5] = env->dr[7] = dbgregs.dr7;

    return 0;
}

static int kvm_put_nested_state(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    int max_nested_state_len = kvm_max_nested_state_length();

    if (!env->nested_state) {
        return 0;
    }

    /*
     * Copy flags that are affected by reset from env->hflags and env->hflags2.
     */
    if (env->hflags & HF_GUEST_MASK) {
        env->nested_state->flags |= KVM_STATE_NESTED_GUEST_MODE;
    } else {
        env->nested_state->flags &= ~KVM_STATE_NESTED_GUEST_MODE;
    }

    /* Don't set KVM_STATE_NESTED_GIF_SET on VMX as it is illegal */
    if (cpu_has_svm(env) && (env->hflags2 & HF2_GIF_MASK)) {
        env->nested_state->flags |= KVM_STATE_NESTED_GIF_SET;
    } else {
        env->nested_state->flags &= ~KVM_STATE_NESTED_GIF_SET;
    }

    assert(env->nested_state->size <= max_nested_state_len);
    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_NESTED_STATE, env->nested_state);
}

static int kvm_get_nested_state(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    int max_nested_state_len = kvm_max_nested_state_length();
    int ret;

    if (!env->nested_state) {
        return 0;
    }

    /*
     * It is possible that migration restored a smaller size into
     * nested_state->hdr.size than what our kernel support.
     * We preserve migration origin nested_state->hdr.size for
     * call to KVM_SET_NESTED_STATE but wish that our next call
     * to KVM_GET_NESTED_STATE will use max size our kernel support.
     */
    env->nested_state->size = max_nested_state_len;

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_NESTED_STATE, env->nested_state);
    if (ret < 0) {
        return ret;
    }

    /*
     * Copy flags that are affected by reset to env->hflags and env->hflags2.
     */
    if (env->nested_state->flags & KVM_STATE_NESTED_GUEST_MODE) {
        env->hflags |= HF_GUEST_MASK;
    } else {
        env->hflags &= ~HF_GUEST_MASK;
    }

    /* Keep HF2_GIF_MASK set on !SVM as x86_cpu_pending_interrupt() needs it */
    if (cpu_has_svm(env)) {
        if (env->nested_state->flags & KVM_STATE_NESTED_GIF_SET) {
            env->hflags2 |= HF2_GIF_MASK;
        } else {
            env->hflags2 &= ~HF2_GIF_MASK;
        }
    }

    return ret;
}

int kvm_arch_put_registers(CPUState *cpu, int level)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    int ret;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    /*
     * Put MSR_IA32_FEATURE_CONTROL first, this ensures the VM gets out of VMX
     * root operation upon vCPU reset. kvm_put_msr_feature_control() should also
     * precede kvm_put_nested_state() when 'real' nested state is set.
     */
    if (level >= KVM_PUT_RESET_STATE) {
        ret = kvm_put_msr_feature_control(x86_cpu);
        if (ret < 0) {
            return ret;
        }
    }

    /* must be before kvm_put_nested_state so that EFER.SVME is set */
    ret = has_sregs2 ? kvm_put_sregs2(x86_cpu) : kvm_put_sregs(x86_cpu);
    if (ret < 0) {
        return ret;
    }

    if (level >= KVM_PUT_RESET_STATE) {
        ret = kvm_put_nested_state(x86_cpu);
        if (ret < 0) {
            return ret;
        }
    }

    if (level == KVM_PUT_FULL_STATE) {
        /* We don't check for kvm_arch_set_tsc_khz() errors here,
         * because TSC frequency mismatch shouldn't abort migration,
         * unless the user explicitly asked for a more strict TSC
         * setting (e.g. using an explicit "tsc-freq" option).
         */
        kvm_arch_set_tsc_khz(cpu);
    }

#ifdef CONFIG_XEN_EMU
    if (xen_mode == XEN_EMULATE && level == KVM_PUT_FULL_STATE) {
        ret = kvm_put_xen_state(cpu);
        if (ret < 0) {
            return ret;
        }
    }
#endif

    ret = kvm_getput_regs(x86_cpu, 1);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_xsave(x86_cpu);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_xcrs(x86_cpu);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_msrs(x86_cpu, level);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_vcpu_events(x86_cpu, level);
    if (ret < 0) {
        return ret;
    }
    if (level >= KVM_PUT_RESET_STATE) {
        ret = kvm_put_mp_state(x86_cpu);
        if (ret < 0) {
            return ret;
        }
    }

    ret = kvm_put_tscdeadline_msr(x86_cpu);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_debugregs(x86_cpu);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

int kvm_arch_get_registers(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    int ret;

    assert(cpu_is_stopped(cs) || qemu_cpu_is_self(cs));

    ret = kvm_get_vcpu_events(cpu);
    if (ret < 0) {
        goto out;
    }
    /*
     * KVM_GET_MPSTATE can modify CS and RIP, call it before
     * KVM_GET_REGS and KVM_GET_SREGS.
     */
    ret = kvm_get_mp_state(cpu);
    if (ret < 0) {
        goto out;
    }
    ret = kvm_getput_regs(cpu, 0);
    if (ret < 0) {
        goto out;
    }
    ret = kvm_get_xsave(cpu);
    if (ret < 0) {
        goto out;
    }
    ret = kvm_get_xcrs(cpu);
    if (ret < 0) {
        goto out;
    }
    ret = has_sregs2 ? kvm_get_sregs2(cpu) : kvm_get_sregs(cpu);
    if (ret < 0) {
        goto out;
    }
    ret = kvm_get_msrs(cpu);
    if (ret < 0) {
        goto out;
    }
    ret = kvm_get_apic(cpu);
    if (ret < 0) {
        goto out;
    }
    ret = kvm_get_debugregs(cpu);
    if (ret < 0) {
        goto out;
    }
    ret = kvm_get_nested_state(cpu);
    if (ret < 0) {
        goto out;
    }
#ifdef CONFIG_XEN_EMU
    if (xen_mode == XEN_EMULATE) {
        ret = kvm_get_xen_state(cs);
        if (ret < 0) {
            goto out;
        }
    }
#endif
    ret = 0;
 out:
    cpu_sync_bndcs_hflags(&cpu->env);
    return ret;
}

void kvm_arch_pre_run(CPUState *cpu, struct kvm_run *run)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    int ret;

    /* Inject NMI */
    if (cpu->interrupt_request & (CPU_INTERRUPT_NMI | CPU_INTERRUPT_SMI)) {
        if (cpu->interrupt_request & CPU_INTERRUPT_NMI) {
            bql_lock();
            cpu->interrupt_request &= ~CPU_INTERRUPT_NMI;
            bql_unlock();
            DPRINTF("injected NMI\n");
            ret = kvm_vcpu_ioctl(cpu, KVM_NMI);
            if (ret < 0) {
                fprintf(stderr, "KVM: injection failed, NMI lost (%s)\n",
                        strerror(-ret));
            }
        }
        if (cpu->interrupt_request & CPU_INTERRUPT_SMI) {
            bql_lock();
            cpu->interrupt_request &= ~CPU_INTERRUPT_SMI;
            bql_unlock();
            DPRINTF("injected SMI\n");
            ret = kvm_vcpu_ioctl(cpu, KVM_SMI);
            if (ret < 0) {
                fprintf(stderr, "KVM: injection failed, SMI lost (%s)\n",
                        strerror(-ret));
            }
        }
    }

    if (!kvm_pic_in_kernel()) {
        bql_lock();
    }

    /* Force the VCPU out of its inner loop to process any INIT requests
     * or (for userspace APIC, but it is cheap to combine the checks here)
     * pending TPR access reports.
     */
    if (cpu->interrupt_request & (CPU_INTERRUPT_INIT | CPU_INTERRUPT_TPR)) {
        if ((cpu->interrupt_request & CPU_INTERRUPT_INIT) &&
            !(env->hflags & HF_SMM_MASK)) {
            cpu->exit_request = 1;
        }
        if (cpu->interrupt_request & CPU_INTERRUPT_TPR) {
            cpu->exit_request = 1;
        }
    }

    if (!kvm_pic_in_kernel()) {
        /* Try to inject an interrupt if the guest can accept it */
        if (run->ready_for_interrupt_injection &&
            (cpu->interrupt_request & CPU_INTERRUPT_HARD) &&
            (env->eflags & IF_MASK)) {
            int irq;

            cpu->interrupt_request &= ~CPU_INTERRUPT_HARD;
            irq = cpu_get_pic_interrupt(env);
            if (irq >= 0) {
                struct kvm_interrupt intr;

                intr.irq = irq;
                DPRINTF("injected interrupt %d\n", irq);
                ret = kvm_vcpu_ioctl(cpu, KVM_INTERRUPT, &intr);
                if (ret < 0) {
                    fprintf(stderr,
                            "KVM: injection failed, interrupt lost (%s)\n",
                            strerror(-ret));
                }
            }
        }

        /* If we have an interrupt but the guest is not ready to receive an
         * interrupt, request an interrupt window exit.  This will
         * cause a return to userspace as soon as the guest is ready to
         * receive interrupts. */
        if ((cpu->interrupt_request & CPU_INTERRUPT_HARD)) {
            run->request_interrupt_window = 1;
        } else {
            run->request_interrupt_window = 0;
        }

        DPRINTF("setting tpr\n");
        run->cr8 = cpu_get_apic_tpr(x86_cpu->apic_state);

        bql_unlock();
    }
}

static void kvm_rate_limit_on_bus_lock(void)
{
    uint64_t delay_ns = ratelimit_calculate_delay(&bus_lock_ratelimit_ctrl, 1);

    if (delay_ns) {
        g_usleep(delay_ns / SCALE_US);
    }
}

MemTxAttrs kvm_arch_post_run(CPUState *cpu, struct kvm_run *run)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    if (run->flags & KVM_RUN_X86_SMM) {
        env->hflags |= HF_SMM_MASK;
    } else {
        env->hflags &= ~HF_SMM_MASK;
    }
    if (run->if_flag) {
        env->eflags |= IF_MASK;
    } else {
        env->eflags &= ~IF_MASK;
    }
    if (run->flags & KVM_RUN_X86_BUS_LOCK) {
        kvm_rate_limit_on_bus_lock();
    }

#ifdef CONFIG_XEN_EMU
    /*
     * If the callback is asserted as a GSI (or PCI INTx) then check if
     * vcpu_info->evtchn_upcall_pending has been cleared, and deassert
     * the callback IRQ if so. Ideally we could hook into the PIC/IOAPIC
     * EOI and only resample then, exactly how the VFIO eventfd pairs
     * are designed to work for level triggered interrupts.
     */
    if (x86_cpu->env.xen_callback_asserted) {
        kvm_xen_maybe_deassert_callback(cpu);
    }
#endif

    /* We need to protect the apic state against concurrent accesses from
     * different threads in case the userspace irqchip is used. */
    if (!kvm_irqchip_in_kernel()) {
        bql_lock();
    }
    cpu_set_apic_tpr(x86_cpu->apic_state, run->cr8);
    cpu_set_apic_base(x86_cpu->apic_state, run->apic_base);
    if (!kvm_irqchip_in_kernel()) {
        bql_unlock();
    }
    return cpu_get_mem_attrs(env);
}

int kvm_arch_process_async_events(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (cs->interrupt_request & CPU_INTERRUPT_MCE) {
        /* We must not raise CPU_INTERRUPT_MCE if it's not supported. */
        assert(env->mcg_cap);

        cs->interrupt_request &= ~CPU_INTERRUPT_MCE;

        kvm_cpu_synchronize_state(cs);

        if (env->exception_nr == EXCP08_DBLE) {
            /* this means triple fault */
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            cs->exit_request = 1;
            return 0;
        }
        kvm_queue_exception(env, EXCP12_MCHK, 0, 0);
        env->has_error_code = 0;

        cs->halted = 0;
        if (kvm_irqchip_in_kernel() && env->mp_state == KVM_MP_STATE_HALTED) {
            env->mp_state = KVM_MP_STATE_RUNNABLE;
        }
    }

    if ((cs->interrupt_request & CPU_INTERRUPT_INIT) &&
        !(env->hflags & HF_SMM_MASK)) {
        kvm_cpu_synchronize_state(cs);
        do_cpu_init(cpu);
    }

    if (kvm_irqchip_in_kernel()) {
        return 0;
    }

    if (cs->interrupt_request & CPU_INTERRUPT_POLL) {
        cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
        apic_poll_irq(cpu->apic_state);
    }
    if (((cs->interrupt_request & CPU_INTERRUPT_HARD) &&
         (env->eflags & IF_MASK)) ||
        (cs->interrupt_request & CPU_INTERRUPT_NMI)) {
        cs->halted = 0;
    }
    if (cs->interrupt_request & CPU_INTERRUPT_SIPI) {
        kvm_cpu_synchronize_state(cs);
        do_cpu_sipi(cpu);
    }
    if (cs->interrupt_request & CPU_INTERRUPT_TPR) {
        cs->interrupt_request &= ~CPU_INTERRUPT_TPR;
        kvm_cpu_synchronize_state(cs);
        apic_handle_tpr_access_report(cpu->apic_state, env->eip,
                                      env->tpr_access_type);
    }

    return cs->halted;
}

static int kvm_handle_halt(X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;

    if (!((cs->interrupt_request & CPU_INTERRUPT_HARD) &&
          (env->eflags & IF_MASK)) &&
        !(cs->interrupt_request & CPU_INTERRUPT_NMI)) {
        cs->halted = 1;
        return EXCP_HLT;
    }

    return 0;
}

static int kvm_handle_tpr_access(X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;

    apic_handle_tpr_access_report(cpu->apic_state, run->tpr_access.rip,
                                  run->tpr_access.is_write ? TPR_ACCESS_WRITE
                                                           : TPR_ACCESS_READ);
    return 1;
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    static const uint8_t int3 = 0xcc;

    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 1, 0) ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&int3, 1, 1)) {
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    uint8_t int3;

    if (cpu_memory_rw_debug(cs, bp->pc, &int3, 1, 0)) {
        return -EINVAL;
    }
    if (int3 != 0xcc) {
        return 0;
    }
    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 1, 1)) {
        return -EINVAL;
    }
    return 0;
}

static struct {
    target_ulong addr;
    int len;
    int type;
} hw_breakpoint[4];

static int nb_hw_breakpoint;

static int find_hw_breakpoint(target_ulong addr, int len, int type)
{
    int n;

    for (n = 0; n < nb_hw_breakpoint; n++) {
        if (hw_breakpoint[n].addr == addr && hw_breakpoint[n].type == type &&
            (hw_breakpoint[n].len == len || len == -1)) {
            return n;
        }
    }
    return -1;
}

int kvm_arch_insert_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_HW:
        len = 1;
        break;
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_ACCESS:
        switch (len) {
        case 1:
            break;
        case 2:
        case 4:
        case 8:
            if (addr & (len - 1)) {
                return -EINVAL;
            }
            break;
        default:
            return -EINVAL;
        }
        break;
    default:
        return -ENOSYS;
    }

    if (nb_hw_breakpoint == 4) {
        return -ENOBUFS;
    }
    if (find_hw_breakpoint(addr, len, type) >= 0) {
        return -EEXIST;
    }
    hw_breakpoint[nb_hw_breakpoint].addr = addr;
    hw_breakpoint[nb_hw_breakpoint].len = len;
    hw_breakpoint[nb_hw_breakpoint].type = type;
    nb_hw_breakpoint++;

    return 0;
}

int kvm_arch_remove_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    int n;

    n = find_hw_breakpoint(addr, (type == GDB_BREAKPOINT_HW) ? 1 : len, type);
    if (n < 0) {
        return -ENOENT;
    }
    nb_hw_breakpoint--;
    hw_breakpoint[n] = hw_breakpoint[nb_hw_breakpoint];

    return 0;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
    nb_hw_breakpoint = 0;
}

static CPUWatchpoint hw_watchpoint;

static int kvm_handle_debug(X86CPU *cpu,
                            struct kvm_debug_exit_arch *arch_info)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;
    int ret = 0;
    int n;

    if (arch_info->exception == EXCP01_DB) {
        if (arch_info->dr6 & DR6_BS) {
            if (cs->singlestep_enabled) {
                ret = EXCP_DEBUG;
            }
        } else {
            for (n = 0; n < 4; n++) {
                if (arch_info->dr6 & (1 << n)) {
                    switch ((arch_info->dr7 >> (16 + n*4)) & 0x3) {
                    case 0x0:
                        ret = EXCP_DEBUG;
                        break;
                    case 0x1:
                        ret = EXCP_DEBUG;
                        cs->watchpoint_hit = &hw_watchpoint;
                        hw_watchpoint.vaddr = hw_breakpoint[n].addr;
                        hw_watchpoint.flags = BP_MEM_WRITE;
                        break;
                    case 0x3:
                        ret = EXCP_DEBUG;
                        cs->watchpoint_hit = &hw_watchpoint;
                        hw_watchpoint.vaddr = hw_breakpoint[n].addr;
                        hw_watchpoint.flags = BP_MEM_ACCESS;
                        break;
                    }
                }
            }
        }
    } else if (kvm_find_sw_breakpoint(cs, arch_info->pc)) {
        ret = EXCP_DEBUG;
    }
    if (ret == 0) {
        cpu_synchronize_state(cs);
        assert(env->exception_nr == -1);

        /* pass to guest */
        kvm_queue_exception(env, arch_info->exception,
                            arch_info->exception == EXCP01_DB,
                            arch_info->dr6);
        env->has_error_code = 0;
    }

    return ret;
}

void kvm_arch_update_guest_debug(CPUState *cpu, struct kvm_guest_debug *dbg)
{
    const uint8_t type_code[] = {
        [GDB_BREAKPOINT_HW] = 0x0,
        [GDB_WATCHPOINT_WRITE] = 0x1,
        [GDB_WATCHPOINT_ACCESS] = 0x3
    };
    const uint8_t len_code[] = {
        [1] = 0x0, [2] = 0x1, [4] = 0x3, [8] = 0x2
    };
    int n;

    if (kvm_sw_breakpoints_active(cpu)) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP;
    }
    if (nb_hw_breakpoint > 0) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
        dbg->arch.debugreg[7] = 0x0600;
        for (n = 0; n < nb_hw_breakpoint; n++) {
            dbg->arch.debugreg[n] = hw_breakpoint[n].addr;
            dbg->arch.debugreg[7] |= (2 << (n * 2)) |
                (type_code[hw_breakpoint[n].type] << (16 + n*4)) |
                ((uint32_t)len_code[hw_breakpoint[n].len] << (18 + n*4));
        }
    }
}

static bool kvm_install_msr_filters(KVMState *s)
{
    uint64_t zero = 0;
    struct kvm_msr_filter filter = {
        .flags = KVM_MSR_FILTER_DEFAULT_ALLOW,
    };
    int r, i, j = 0;

    for (i = 0; i < KVM_MSR_FILTER_MAX_RANGES; i++) {
        KVMMSRHandlers *handler = &msr_handlers[i];
        if (handler->msr) {
            struct kvm_msr_filter_range *range = &filter.ranges[j++];

            *range = (struct kvm_msr_filter_range) {
                .flags = 0,
                .nmsrs = 1,
                .base = handler->msr,
                .bitmap = (__u8 *)&zero,
            };

            if (handler->rdmsr) {
                range->flags |= KVM_MSR_FILTER_READ;
            }

            if (handler->wrmsr) {
                range->flags |= KVM_MSR_FILTER_WRITE;
            }
        }
    }

    r = kvm_vm_ioctl(s, KVM_X86_SET_MSR_FILTER, &filter);
    if (r) {
        return false;
    }

    return true;
}

bool kvm_filter_msr(KVMState *s, uint32_t msr, QEMURDMSRHandler *rdmsr,
                    QEMUWRMSRHandler *wrmsr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(msr_handlers); i++) {
        if (!msr_handlers[i].msr) {
            msr_handlers[i] = (KVMMSRHandlers) {
                .msr = msr,
                .rdmsr = rdmsr,
                .wrmsr = wrmsr,
            };

            if (!kvm_install_msr_filters(s)) {
                msr_handlers[i] = (KVMMSRHandlers) { };
                return false;
            }

            return true;
        }
    }

    return false;
}

static int kvm_handle_rdmsr(X86CPU *cpu, struct kvm_run *run)
{
    int i;
    bool r;

    for (i = 0; i < ARRAY_SIZE(msr_handlers); i++) {
        KVMMSRHandlers *handler = &msr_handlers[i];
        if (run->msr.index == handler->msr) {
            if (handler->rdmsr) {
                r = handler->rdmsr(cpu, handler->msr,
                                   (uint64_t *)&run->msr.data);
                run->msr.error = r ? 0 : 1;
                return 0;
            }
        }
    }

    assert(false);
}

static int kvm_handle_wrmsr(X86CPU *cpu, struct kvm_run *run)
{
    int i;
    bool r;

    for (i = 0; i < ARRAY_SIZE(msr_handlers); i++) {
        KVMMSRHandlers *handler = &msr_handlers[i];
        if (run->msr.index == handler->msr) {
            if (handler->wrmsr) {
                r = handler->wrmsr(cpu, handler->msr, run->msr.data);
                run->msr.error = r ? 0 : 1;
                return 0;
            }
        }
    }

    assert(false);
}

static bool has_sgx_provisioning;

static bool __kvm_enable_sgx_provisioning(KVMState *s)
{
    int fd, ret;

    if (!kvm_vm_check_extension(s, KVM_CAP_SGX_ATTRIBUTE)) {
        return false;
    }

    fd = qemu_open_old("/dev/sgx_provision", O_RDONLY);
    if (fd < 0) {
        return false;
    }

    ret = kvm_vm_enable_cap(s, KVM_CAP_SGX_ATTRIBUTE, 0, fd);
    if (ret) {
        error_report("Could not enable SGX PROVISIONKEY: %s", strerror(-ret));
        exit(1);
    }
    close(fd);
    return true;
}

bool kvm_enable_sgx_provisioning(KVMState *s)
{
    return MEMORIZE(__kvm_enable_sgx_provisioning(s), has_sgx_provisioning);
}

static bool host_supports_vmx(void)
{
    uint32_t ecx, unused;

    host_cpuid(1, 0, &unused, &unused, &ecx, &unused);
    return ecx & CPUID_EXT_VMX;
}

/*
 * Currently the handling here only supports use of KVM_HC_MAP_GPA_RANGE
 * to service guest-initiated memory attribute update requests so that
 * KVM_SET_MEMORY_ATTRIBUTES can update whether or not a page should be
 * backed by the private memory pool provided by guest_memfd, and as such
 * is only applicable to guest_memfd-backed guests (e.g. SNP/TDX).
 *
 * Other other use-cases for KVM_HC_MAP_GPA_RANGE, such as for SEV live
 * migration, are not implemented here currently.
 *
 * For the guest_memfd use-case, these exits will generally be synthesized
 * by KVM based on platform-specific hypercalls, like GHCB requests in the
 * case of SEV-SNP, and not issued directly within the guest though the
 * KVM_HC_MAP_GPA_RANGE hypercall. So in this case, KVM_HC_MAP_GPA_RANGE is
 * not actually advertised to guests via the KVM CPUID feature bit, as
 * opposed to SEV live migration where it would be. Since it is unlikely the
 * SEV live migration use-case would be useful for guest-memfd backed guests,
 * because private/shared page tracking is already provided through other
 * means, these 2 use-cases should be treated as being mutually-exclusive.
 */
static int kvm_handle_hc_map_gpa_range(struct kvm_run *run)
{
    uint64_t gpa, size, attributes;

    if (!machine_require_guest_memfd(current_machine))
        return -EINVAL;

    gpa = run->hypercall.args[0];
    size = run->hypercall.args[1] * TARGET_PAGE_SIZE;
    attributes = run->hypercall.args[2];

    trace_kvm_hc_map_gpa_range(gpa, size, attributes, run->hypercall.flags);

    return kvm_convert_memory(gpa, size, attributes & KVM_MAP_GPA_RANGE_ENCRYPTED);
}

static int kvm_handle_hypercall(struct kvm_run *run)
{
    if (run->hypercall.nr == KVM_HC_MAP_GPA_RANGE)
        return kvm_handle_hc_map_gpa_range(run);

    return -EINVAL;
}

#define VMX_INVALID_GUEST_STATE 0x80000021

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    X86CPU *cpu = X86_CPU(cs);
    uint64_t code;
    int ret;
    bool ctx_invalid;
    KVMState *state;

    switch (run->exit_reason) {
    case KVM_EXIT_HLT:
        DPRINTF("handle_hlt\n");
        bql_lock();
        ret = kvm_handle_halt(cpu);
        bql_unlock();
        break;
    case KVM_EXIT_SET_TPR:
        ret = 0;
        break;
    case KVM_EXIT_TPR_ACCESS:
        bql_lock();
        ret = kvm_handle_tpr_access(cpu);
        bql_unlock();
        break;
    case KVM_EXIT_FAIL_ENTRY:
        code = run->fail_entry.hardware_entry_failure_reason;
        fprintf(stderr, "KVM: entry failed, hardware error 0x%" PRIx64 "\n",
                code);
        if (host_supports_vmx() && code == VMX_INVALID_GUEST_STATE) {
            fprintf(stderr,
                    "\nIf you're running a guest on an Intel machine without "
                        "unrestricted mode\n"
                    "support, the failure can be most likely due to the guest "
                        "entering an invalid\n"
                    "state for Intel VT. For example, the guest maybe running "
                        "in big real mode\n"
                    "which is not supported on less recent Intel processors."
                        "\n\n");
        }
        ret = -1;
        break;
    case KVM_EXIT_EXCEPTION:
        fprintf(stderr, "KVM: exception %d exit (error code 0x%x)\n",
                run->ex.exception, run->ex.error_code);
        ret = -1;
        break;
    case KVM_EXIT_DEBUG:
        DPRINTF("kvm_exit_debug\n");
        bql_lock();
        ret = kvm_handle_debug(cpu, &run->debug.arch);
        bql_unlock();
        break;
    case KVM_EXIT_HYPERV:
        ret = kvm_hv_handle_exit(cpu, &run->hyperv);
        break;
    case KVM_EXIT_IOAPIC_EOI:
        ioapic_eoi_broadcast(run->eoi.vector);
        ret = 0;
        break;
    case KVM_EXIT_X86_BUS_LOCK:
        /* already handled in kvm_arch_post_run */
        ret = 0;
        break;
    case KVM_EXIT_NOTIFY:
        ctx_invalid = !!(run->notify.flags & KVM_NOTIFY_CONTEXT_INVALID);
        state = KVM_STATE(current_accel());
        if (ctx_invalid ||
            state->notify_vmexit == NOTIFY_VMEXIT_OPTION_INTERNAL_ERROR) {
            warn_report("KVM internal error: Encountered a notify exit "
                        "with invalid context in guest.");
            ret = -1;
        } else {
            warn_report_once("KVM: Encountered a notify exit with valid "
                             "context in guest. "
                             "The guest could be misbehaving.");
            ret = 0;
        }
        break;
    case KVM_EXIT_X86_RDMSR:
        /* We only enable MSR filtering, any other exit is bogus */
        assert(run->msr.reason == KVM_MSR_EXIT_REASON_FILTER);
        ret = kvm_handle_rdmsr(cpu, run);
        break;
    case KVM_EXIT_X86_WRMSR:
        /* We only enable MSR filtering, any other exit is bogus */
        assert(run->msr.reason == KVM_MSR_EXIT_REASON_FILTER);
        ret = kvm_handle_wrmsr(cpu, run);
        break;
#ifdef CONFIG_XEN_EMU
    case KVM_EXIT_XEN:
        ret = kvm_xen_handle_exit(cpu, &run->xen);
        break;
#endif
    case KVM_EXIT_HYPERCALL:
        ret = kvm_handle_hypercall(run);
        break;
    default:
        fprintf(stderr, "KVM: unknown exit reason %d\n", run->exit_reason);
        ret = -1;
        break;
    }

    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    kvm_cpu_synchronize_state(cs);
    return !(env->cr[0] & CR0_PE_MASK) ||
           ((env->segs[R_CS].selector  & 3) != 3);
}

void kvm_arch_init_irq_routing(KVMState *s)
{
    /* We know at this point that we're using the in-kernel
     * irqchip, so we can use irqfds, and on x86 we know
     * we can use msi via irqfd and GSI routing.
     */
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_routing_allowed = true;

    if (kvm_irqchip_is_split()) {
        KVMRouteChange c = kvm_irqchip_begin_route_changes(s);
        int i;

        /* If the ioapic is in QEMU and the lapics are in KVM, reserve
           MSI routes for signaling interrupts to the local apics. */
        for (i = 0; i < IOAPIC_NUM_PINS; i++) {
            if (kvm_irqchip_add_msi_route(&c, 0, NULL) < 0) {
                error_report("Could not enable split IRQ mode.");
                exit(1);
            }
        }
        kvm_irqchip_commit_route_changes(&c);
    }
}

int kvm_arch_irqchip_create(KVMState *s)
{
    int ret;
    if (kvm_kernel_irqchip_split()) {
        ret = kvm_vm_enable_cap(s, KVM_CAP_SPLIT_IRQCHIP, 0, 24);
        if (ret) {
            error_report("Could not enable split irqchip mode: %s",
                         strerror(-ret));
            exit(1);
        } else {
            DPRINTF("Enabled KVM_CAP_SPLIT_IRQCHIP\n");
            kvm_split_irqchip = true;
            return 1;
        }
    } else {
        return 0;
    }
}

uint64_t kvm_swizzle_msi_ext_dest_id(uint64_t address)
{
    CPUX86State *env;
    uint64_t ext_id;

    if (!first_cpu) {
        return address;
    }
    env = &X86_CPU(first_cpu)->env;
    if (!(env->features[FEAT_KVM] & (1 << KVM_FEATURE_MSI_EXT_DEST_ID))) {
        return address;
    }

    /*
     * If the remappable format bit is set, or the upper bits are
     * already set in address_hi, or the low extended bits aren't
     * there anyway, do nothing.
     */
    ext_id = address & (0xff << MSI_ADDR_DEST_IDX_SHIFT);
    if (!ext_id || (ext_id & (1 << MSI_ADDR_DEST_IDX_SHIFT)) || (address >> 32)) {
        return address;
    }

    address &= ~ext_id;
    address |= ext_id << 35;
    return address;
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
    X86IOMMUState *iommu = x86_iommu_get_default();

    if (iommu) {
        X86IOMMUClass *class = X86_IOMMU_DEVICE_GET_CLASS(iommu);

        if (class->int_remap) {
            int ret;
            MSIMessage src, dst;

            src.address = route->u.msi.address_hi;
            src.address <<= VTD_MSI_ADDR_HI_SHIFT;
            src.address |= route->u.msi.address_lo;
            src.data = route->u.msi.data;

            ret = class->int_remap(iommu, &src, &dst, dev ?     \
                                   pci_requester_id(dev) :      \
                                   X86_IOMMU_SID_INVALID);
            if (ret) {
                trace_kvm_x86_fixup_msi_error(route->gsi);
                return 1;
            }

            /*
             * Handled untranslated compatibility format interrupt with
             * extended destination ID in the low bits 11-5. */
            dst.address = kvm_swizzle_msi_ext_dest_id(dst.address);

            route->u.msi.address_hi = dst.address >> VTD_MSI_ADDR_HI_SHIFT;
            route->u.msi.address_lo = dst.address & VTD_MSI_ADDR_LO_MASK;
            route->u.msi.data = dst.data;
            return 0;
        }
    }

#ifdef CONFIG_XEN_EMU
    if (xen_mode == XEN_EMULATE) {
        int handled = xen_evtchn_translate_pirq_msi(route, address, data);

        /*
         * If it was a PIRQ and successfully routed (handled == 0) or it was
         * an error (handled < 0), return. If it wasn't a PIRQ, keep going.
         */
        if (handled <= 0) {
            return handled;
        }
    }
#endif

    address = kvm_swizzle_msi_ext_dest_id(address);
    route->u.msi.address_hi = address >> VTD_MSI_ADDR_HI_SHIFT;
    route->u.msi.address_lo = address & VTD_MSI_ADDR_LO_MASK;
    return 0;
}

typedef struct MSIRouteEntry MSIRouteEntry;

struct MSIRouteEntry {
    PCIDevice *dev;             /* Device pointer */
    int vector;                 /* MSI/MSIX vector index */
    int virq;                   /* Virtual IRQ index */
    QLIST_ENTRY(MSIRouteEntry) list;
};

/* List of used GSI routes */
static QLIST_HEAD(, MSIRouteEntry) msi_route_list = \
    QLIST_HEAD_INITIALIZER(msi_route_list);

void kvm_update_msi_routes_all(void *private, bool global,
                               uint32_t index, uint32_t mask)
{
    int cnt = 0, vector;
    MSIRouteEntry *entry;
    MSIMessage msg;
    PCIDevice *dev;

    /* TODO: explicit route update */
    QLIST_FOREACH(entry, &msi_route_list, list) {
        cnt++;
        vector = entry->vector;
        dev = entry->dev;
        if (msix_enabled(dev) && !msix_is_masked(dev, vector)) {
            msg = msix_get_message(dev, vector);
        } else if (msi_enabled(dev) && !msi_is_masked(dev, vector)) {
            msg = msi_get_message(dev, vector);
        } else {
            /*
             * Either MSI/MSIX is disabled for the device, or the
             * specific message was masked out.  Skip this one.
             */
            continue;
        }
        kvm_irqchip_update_msi_route(kvm_state, entry->virq, msg, dev);
    }
    kvm_irqchip_commit_routes(kvm_state);
    trace_kvm_x86_update_msi_routes(cnt);
}

int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev)
{
    static bool notify_list_inited = false;
    MSIRouteEntry *entry;

    if (!dev) {
        /* These are (possibly) IOAPIC routes only used for split
         * kernel irqchip mode, while what we are housekeeping are
         * PCI devices only. */
        return 0;
    }

    entry = g_new0(MSIRouteEntry, 1);
    entry->dev = dev;
    entry->vector = vector;
    entry->virq = route->gsi;
    QLIST_INSERT_HEAD(&msi_route_list, entry, list);

    trace_kvm_x86_add_msi_route(route->gsi);

    if (!notify_list_inited) {
        /* For the first time we do add route, add ourselves into
         * IOMMU's IEC notify list if needed. */
        X86IOMMUState *iommu = x86_iommu_get_default();
        if (iommu) {
            x86_iommu_iec_register_notifier(iommu,
                                            kvm_update_msi_routes_all,
                                            NULL);
        }
        notify_list_inited = true;
    }
    return 0;
}

int kvm_arch_release_virq_post(int virq)
{
    MSIRouteEntry *entry, *next;
    QLIST_FOREACH_SAFE(entry, &msi_route_list, list, next) {
        if (entry->virq == virq) {
            trace_kvm_x86_remove_msi_route(virq);
            QLIST_REMOVE(entry, list);
            g_free(entry);
            break;
        }
    }
    return 0;
}

int kvm_arch_msi_data_to_gsi(uint32_t data)
{
    abort();
}

bool kvm_has_waitpkg(void)
{
    return has_msr_umwait;
}

#define ARCH_REQ_XCOMP_GUEST_PERM       0x1025

void kvm_request_xsave_components(X86CPU *cpu, uint64_t mask)
{
    KVMState *s = kvm_state;
    uint64_t supported;

    mask &= XSTATE_DYNAMIC_MASK;
    if (!mask) {
        return;
    }
    /*
     * Just ignore bits that are not in CPUID[EAX=0xD,ECX=0].
     * ARCH_REQ_XCOMP_GUEST_PERM would fail, and QEMU has warned
     * about them already because they are not supported features.
     */
    supported = kvm_arch_get_supported_cpuid(s, 0xd, 0, R_EAX);
    supported |= (uint64_t)kvm_arch_get_supported_cpuid(s, 0xd, 0, R_EDX) << 32;
    mask &= supported;

    while (mask) {
        int bit = ctz64(mask);
        int rc = syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_GUEST_PERM, bit);
        if (rc) {
            /*
             * Older kernel version (<5.17) do not support
             * ARCH_REQ_XCOMP_GUEST_PERM, but also do not return
             * any dynamic feature from kvm_arch_get_supported_cpuid.
             */
            warn_report("prctl(ARCH_REQ_XCOMP_GUEST_PERM) failure "
                        "for feature bit %d", bit);
        }
        mask &= ~BIT_ULL(bit);
    }
}

static int kvm_arch_get_notify_vmexit(Object *obj, Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    return s->notify_vmexit;
}

static void kvm_arch_set_notify_vmexit(Object *obj, int value, Error **errp)
{
    KVMState *s = KVM_STATE(obj);

    if (s->fd != -1) {
        error_setg(errp, "Cannot set properties after the accelerator has been initialized");
        return;
    }

    s->notify_vmexit = value;
}

static void kvm_arch_get_notify_window(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint32_t value = s->notify_window;

    visit_type_uint32(v, name, &value, errp);
}

static void kvm_arch_set_notify_window(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint32_t value;

    if (s->fd != -1) {
        error_setg(errp, "Cannot set properties after the accelerator has been initialized");
        return;
    }

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    s->notify_window = value;
}

static void kvm_arch_get_xen_version(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint32_t value = s->xen_version;

    visit_type_uint32(v, name, &value, errp);
}

static void kvm_arch_set_xen_version(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    Error *error = NULL;
    uint32_t value;

    visit_type_uint32(v, name, &value, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    s->xen_version = value;
    if (value && xen_mode == XEN_DISABLED) {
        xen_mode = XEN_EMULATE;
    }
}

static void kvm_arch_get_xen_gnttab_max_frames(Object *obj, Visitor *v,
                                               const char *name, void *opaque,
                                               Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint16_t value = s->xen_gnttab_max_frames;

    visit_type_uint16(v, name, &value, errp);
}

static void kvm_arch_set_xen_gnttab_max_frames(Object *obj, Visitor *v,
                                               const char *name, void *opaque,
                                               Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    Error *error = NULL;
    uint16_t value;

    visit_type_uint16(v, name, &value, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    s->xen_gnttab_max_frames = value;
}

static void kvm_arch_get_xen_evtchn_max_pirq(Object *obj, Visitor *v,
                                             const char *name, void *opaque,
                                             Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint16_t value = s->xen_evtchn_max_pirq;

    visit_type_uint16(v, name, &value, errp);
}

static void kvm_arch_set_xen_evtchn_max_pirq(Object *obj, Visitor *v,
                                             const char *name, void *opaque,
                                             Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    Error *error = NULL;
    uint16_t value;

    visit_type_uint16(v, name, &value, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    s->xen_evtchn_max_pirq = value;
}

void kvm_arch_accel_class_init(ObjectClass *oc)
{
    object_class_property_add_enum(oc, "notify-vmexit", "NotifyVMexitOption",
                                   &NotifyVmexitOption_lookup,
                                   kvm_arch_get_notify_vmexit,
                                   kvm_arch_set_notify_vmexit);
    object_class_property_set_description(oc, "notify-vmexit",
                                          "Enable notify VM exit");

    object_class_property_add(oc, "notify-window", "uint32",
                              kvm_arch_get_notify_window,
                              kvm_arch_set_notify_window,
                              NULL, NULL);
    object_class_property_set_description(oc, "notify-window",
                                          "Clock cycles without an event window "
                                          "after which a notification VM exit occurs");

    object_class_property_add(oc, "xen-version", "uint32",
                              kvm_arch_get_xen_version,
                              kvm_arch_set_xen_version,
                              NULL, NULL);
    object_class_property_set_description(oc, "xen-version",
                                          "Xen version to be emulated "
                                          "(in XENVER_version form "
                                          "e.g. 0x4000a for 4.10)");

    object_class_property_add(oc, "xen-gnttab-max-frames", "uint16",
                              kvm_arch_get_xen_gnttab_max_frames,
                              kvm_arch_set_xen_gnttab_max_frames,
                              NULL, NULL);
    object_class_property_set_description(oc, "xen-gnttab-max-frames",
                                          "Maximum number of grant table frames");

    object_class_property_add(oc, "xen-evtchn-max-pirq", "uint16",
                              kvm_arch_get_xen_evtchn_max_pirq,
                              kvm_arch_set_xen_evtchn_max_pirq,
                              NULL, NULL);
    object_class_property_set_description(oc, "xen-evtchn-max-pirq",
                                          "Maximum number of Xen PIRQs");
}

void kvm_set_max_apic_id(uint32_t max_apic_id)
{
    kvm_vm_enable_cap(kvm_state, KVM_CAP_MAX_VCPU_ID, 0, max_apic_id);
}
