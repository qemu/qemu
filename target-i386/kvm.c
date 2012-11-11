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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/utsname.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#include "qemu-common.h"
#include "sysemu.h"
#include "kvm.h"
#include "kvm_i386.h"
#include "cpu.h"
#include "gdbstub.h"
#include "host-utils.h"
#include "hw/pc.h"
#include "hw/apic.h"
#include "ioport.h"
#include "hyperv.h"
#include "hw/pci.h"

//#define DEBUG_KVM

#ifdef DEBUG_KVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define MSR_KVM_WALL_CLOCK  0x11
#define MSR_KVM_SYSTEM_TIME 0x12

#ifndef BUS_MCEERR_AR
#define BUS_MCEERR_AR 4
#endif
#ifndef BUS_MCEERR_AO
#define BUS_MCEERR_AO 5
#endif

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_INFO(SET_TSS_ADDR),
    KVM_CAP_INFO(EXT_CPUID),
    KVM_CAP_INFO(MP_STATE),
    KVM_CAP_LAST_INFO
};

static bool has_msr_star;
static bool has_msr_hsave_pa;
static bool has_msr_tsc_deadline;
static bool has_msr_async_pf_en;
static bool has_msr_pv_eoi_en;
static bool has_msr_misc_enable;
static int lm_capable_kernel;

bool kvm_allows_irq0_override(void)
{
    return !kvm_irqchip_in_kernel() || kvm_has_gsi_routing();
}

static struct kvm_cpuid2 *try_get_cpuid(KVMState *s, int max)
{
    struct kvm_cpuid2 *cpuid;
    int r, size;

    size = sizeof(*cpuid) + max * sizeof(*cpuid->entries);
    cpuid = (struct kvm_cpuid2 *)g_malloc0(size);
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
    while ((cpuid = try_get_cpuid(s, max)) == NULL) {
        max *= 2;
    }
    return cpuid;
}

struct kvm_para_features {
    int cap;
    int feature;
} para_features[] = {
    { KVM_CAP_CLOCKSOURCE, KVM_FEATURE_CLOCKSOURCE },
    { KVM_CAP_NOP_IO_DELAY, KVM_FEATURE_NOP_IO_DELAY },
    { KVM_CAP_PV_MMU, KVM_FEATURE_MMU_OP },
    { KVM_CAP_ASYNC_PF, KVM_FEATURE_ASYNC_PF },
    { -1, -1 }
};

static int get_para_features(KVMState *s)
{
    int i, features = 0;

    for (i = 0; i < ARRAY_SIZE(para_features) - 1; i++) {
        if (kvm_check_extension(s, para_features[i].cap)) {
            features |= (1 << para_features[i].feature);
        }
    }

    return features;
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
    uint32_t cpuid_1_edx;
    bool found = false;

    cpuid = get_supported_cpuid(s);

    struct kvm_cpuid_entry2 *entry = cpuid_find_entry(cpuid, function, index);
    if (entry) {
        found = true;
        ret = cpuid_entry_get_reg(entry, reg);
    }

    /* Fixups for the data returned by KVM, below */

    if (function == 1 && reg == R_EDX) {
        /* KVM before 2.6.30 misreports the following features */
        ret |= CPUID_MTRR | CPUID_PAT | CPUID_MCE | CPUID_MCA;
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
    } else if (function == 0x80000001 && reg == R_EDX) {
        /* On Intel, kvm returns cpuid according to the Intel spec,
         * so add missing bits according to the AMD spec:
         */
        cpuid_1_edx = kvm_arch_get_supported_cpuid(s, 1, 0, R_EDX);
        ret |= cpuid_1_edx & CPUID_EXT2_AMD_ALIASES;
    }

    g_free(cpuid);

    /* fallback for older kernels */
    if ((function == KVM_CPUID_FEATURES) && !found) {
        ret = get_para_features(s);
    }

    return ret;
}

typedef struct HWPoisonPage {
    ram_addr_t ram_addr;
    QLIST_ENTRY(HWPoisonPage) list;
} HWPoisonPage;

static QLIST_HEAD(, HWPoisonPage) hwpoison_page_list =
    QLIST_HEAD_INITIALIZER(hwpoison_page_list);

static void kvm_unpoison_all(void *param)
{
    HWPoisonPage *page, *next_page;

    QLIST_FOREACH_SAFE(page, &hwpoison_page_list, list, next_page) {
        QLIST_REMOVE(page, list);
        qemu_ram_remap(page->ram_addr, TARGET_PAGE_SIZE);
        g_free(page);
    }
}

static void kvm_hwpoison_page_add(ram_addr_t ram_addr)
{
    HWPoisonPage *page;

    QLIST_FOREACH(page, &hwpoison_page_list, list) {
        if (page->ram_addr == ram_addr) {
            return;
        }
    }
    page = g_malloc(sizeof(HWPoisonPage));
    page->ram_addr = ram_addr;
    QLIST_INSERT_HEAD(&hwpoison_page_list, page, list);
}

static int kvm_get_mce_cap_supported(KVMState *s, uint64_t *mce_cap,
                                     int *max_banks)
{
    int r;

    r = kvm_check_extension(s, KVM_CAP_MCE);
    if (r > 0) {
        *max_banks = r;
        return kvm_ioctl(s, KVM_X86_GET_MCE_CAP_SUPPORTED, mce_cap);
    }
    return -ENOSYS;
}

static void kvm_mce_inject(X86CPU *cpu, hwaddr paddr, int code)
{
    CPUX86State *env = &cpu->env;
    uint64_t status = MCI_STATUS_VAL | MCI_STATUS_UC | MCI_STATUS_EN |
                      MCI_STATUS_MISCV | MCI_STATUS_ADDRV | MCI_STATUS_S;
    uint64_t mcg_status = MCG_STATUS_MCIP;

    if (code == BUS_MCEERR_AR) {
        status |= MCI_STATUS_AR | 0x134;
        mcg_status |= MCG_STATUS_EIPV;
    } else {
        status |= 0xc0;
        mcg_status |= MCG_STATUS_RIPV;
    }
    cpu_x86_inject_mce(NULL, cpu, 9, status, mcg_status, paddr,
                       (MCM_ADDR_PHYS << 6) | 0xc,
                       cpu_x86_support_mca_broadcast(env) ?
                       MCE_INJECT_BROADCAST : 0);
}

static void hardware_memory_error(void)
{
    fprintf(stderr, "Hardware memory error!\n");
    exit(1);
}

int kvm_arch_on_sigbus_vcpu(CPUX86State *env, int code, void *addr)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    ram_addr_t ram_addr;
    hwaddr paddr;

    if ((env->mcg_cap & MCG_SER_P) && addr
        && (code == BUS_MCEERR_AR || code == BUS_MCEERR_AO)) {
        if (qemu_ram_addr_from_host(addr, &ram_addr) ||
            !kvm_physical_memory_addr_from_host(env->kvm_state, addr, &paddr)) {
            fprintf(stderr, "Hardware memory error for memory used by "
                    "QEMU itself instead of guest system!\n");
            /* Hope we are lucky for AO MCE */
            if (code == BUS_MCEERR_AO) {
                return 0;
            } else {
                hardware_memory_error();
            }
        }
        kvm_hwpoison_page_add(ram_addr);
        kvm_mce_inject(cpu, paddr, code);
    } else {
        if (code == BUS_MCEERR_AO) {
            return 0;
        } else if (code == BUS_MCEERR_AR) {
            hardware_memory_error();
        } else {
            return 1;
        }
    }
    return 0;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    if ((first_cpu->mcg_cap & MCG_SER_P) && addr && code == BUS_MCEERR_AO) {
        ram_addr_t ram_addr;
        hwaddr paddr;

        /* Hope we are lucky for AO MCE */
        if (qemu_ram_addr_from_host(addr, &ram_addr) ||
            !kvm_physical_memory_addr_from_host(first_cpu->kvm_state, addr,
                                                &paddr)) {
            fprintf(stderr, "Hardware memory error for memory used by "
                    "QEMU itself instead of guest system!: %p\n", addr);
            return 0;
        }
        kvm_hwpoison_page_add(ram_addr);
        kvm_mce_inject(x86_env_get_cpu(first_cpu), paddr, code);
    } else {
        if (code == BUS_MCEERR_AO) {
            return 0;
        } else if (code == BUS_MCEERR_AR) {
            hardware_memory_error();
        } else {
            return 1;
        }
    }
    return 0;
}

static int kvm_inject_mce_oldstyle(CPUX86State *env)
{
    if (!kvm_has_vcpu_events() && env->exception_injected == EXCP12_MCHK) {
        unsigned int bank, bank_num = env->mcg_cap & 0xff;
        struct kvm_x86_mce mce;

        env->exception_injected = -1;

        /*
         * There must be at least one bank in use if an MCE is pending.
         * Find it and use its values for the event injection.
         */
        for (bank = 0; bank < bank_num; bank++) {
            if (env->mce_banks[bank * 4 + 1] & MCI_STATUS_VAL) {
                break;
            }
        }
        assert(bank < bank_num);

        mce.bank = bank;
        mce.status = env->mce_banks[bank * 4 + 1];
        mce.mcg_status = env->mcg_status;
        mce.addr = env->mce_banks[bank * 4 + 2];
        mce.misc = env->mce_banks[bank * 4 + 3];

        return kvm_vcpu_ioctl(env, KVM_X86_SET_MCE, &mce);
    }
    return 0;
}

static void cpu_update_state(void *opaque, int running, RunState state)
{
    CPUX86State *env = opaque;

    if (running) {
        env->tsc_valid = false;
    }
}

int kvm_arch_init_vcpu(CPUX86State *env)
{
    struct {
        struct kvm_cpuid2 cpuid;
        struct kvm_cpuid_entry2 entries[100];
    } QEMU_PACKED cpuid_data;
    uint32_t limit, i, j, cpuid_i;
    uint32_t unused;
    struct kvm_cpuid_entry2 *c;
    uint32_t signature[3];
    int r;

    cpuid_i = 0;

    /* Paravirtualization CPUIDs */
    c = &cpuid_data.entries[cpuid_i++];
    memset(c, 0, sizeof(*c));
    c->function = KVM_CPUID_SIGNATURE;
    if (!hyperv_enabled()) {
        memcpy(signature, "KVMKVMKVM\0\0\0", 12);
        c->eax = 0;
    } else {
        memcpy(signature, "Microsoft Hv", 12);
        c->eax = HYPERV_CPUID_MIN;
    }
    c->ebx = signature[0];
    c->ecx = signature[1];
    c->edx = signature[2];

    c = &cpuid_data.entries[cpuid_i++];
    memset(c, 0, sizeof(*c));
    c->function = KVM_CPUID_FEATURES;
    c->eax = env->cpuid_kvm_features;

    if (hyperv_enabled()) {
        memcpy(signature, "Hv#1\0\0\0\0\0\0\0\0", 12);
        c->eax = signature[0];

        c = &cpuid_data.entries[cpuid_i++];
        memset(c, 0, sizeof(*c));
        c->function = HYPERV_CPUID_VERSION;
        c->eax = 0x00001bbc;
        c->ebx = 0x00060001;

        c = &cpuid_data.entries[cpuid_i++];
        memset(c, 0, sizeof(*c));
        c->function = HYPERV_CPUID_FEATURES;
        if (hyperv_relaxed_timing_enabled()) {
            c->eax |= HV_X64_MSR_HYPERCALL_AVAILABLE;
        }
        if (hyperv_vapic_recommended()) {
            c->eax |= HV_X64_MSR_HYPERCALL_AVAILABLE;
            c->eax |= HV_X64_MSR_APIC_ACCESS_AVAILABLE;
        }

        c = &cpuid_data.entries[cpuid_i++];
        memset(c, 0, sizeof(*c));
        c->function = HYPERV_CPUID_ENLIGHTMENT_INFO;
        if (hyperv_relaxed_timing_enabled()) {
            c->eax |= HV_X64_RELAXED_TIMING_RECOMMENDED;
        }
        if (hyperv_vapic_recommended()) {
            c->eax |= HV_X64_APIC_ACCESS_RECOMMENDED;
        }
        c->ebx = hyperv_get_spinlock_retries();

        c = &cpuid_data.entries[cpuid_i++];
        memset(c, 0, sizeof(*c));
        c->function = HYPERV_CPUID_IMPLEMENT_LIMITS;
        c->eax = 0x40;
        c->ebx = 0x40;

        c = &cpuid_data.entries[cpuid_i++];
        memset(c, 0, sizeof(*c));
        c->function = KVM_CPUID_SIGNATURE_NEXT;
        memcpy(signature, "KVMKVMKVM\0\0\0", 12);
        c->eax = 0;
        c->ebx = signature[0];
        c->ecx = signature[1];
        c->edx = signature[2];
    }

    has_msr_async_pf_en = c->eax & (1 << KVM_FEATURE_ASYNC_PF);

    has_msr_pv_eoi_en = c->eax & (1 << KVM_FEATURE_PV_EOI);

    cpu_x86_cpuid(env, 0, 0, &limit, &unused, &unused, &unused);

    for (i = 0; i <= limit; i++) {
        c = &cpuid_data.entries[cpuid_i++];

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
                c = &cpuid_data.entries[cpuid_i++];
                c->function = i;
                c->flags = KVM_CPUID_FLAG_STATEFUL_FUNC;
                cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
            }
            break;
        }
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
                if (i == 0xd && c->eax == 0) {
                    continue;
                }
                c = &cpuid_data.entries[cpuid_i++];
            }
            break;
        default:
            c->function = i;
            c->flags = 0;
            cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
            break;
        }
    }
    cpu_x86_cpuid(env, 0x80000000, 0, &limit, &unused, &unused, &unused);

    for (i = 0x80000000; i <= limit; i++) {
        c = &cpuid_data.entries[cpuid_i++];

        c->function = i;
        c->flags = 0;
        cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
    }

    /* Call Centaur's CPUID instructions they are supported. */
    if (env->cpuid_xlevel2 > 0) {
        cpu_x86_cpuid(env, 0xC0000000, 0, &limit, &unused, &unused, &unused);

        for (i = 0xC0000000; i <= limit; i++) {
            c = &cpuid_data.entries[cpuid_i++];

            c->function = i;
            c->flags = 0;
            cpu_x86_cpuid(env, i, 0, &c->eax, &c->ebx, &c->ecx, &c->edx);
        }
    }

    cpuid_data.cpuid.nent = cpuid_i;

    if (((env->cpuid_version >> 8)&0xF) >= 6
        && (env->cpuid_features&(CPUID_MCE|CPUID_MCA)) == (CPUID_MCE|CPUID_MCA)
        && kvm_check_extension(env->kvm_state, KVM_CAP_MCE) > 0) {
        uint64_t mcg_cap;
        int banks;
        int ret;

        ret = kvm_get_mce_cap_supported(env->kvm_state, &mcg_cap, &banks);
        if (ret < 0) {
            fprintf(stderr, "kvm_get_mce_cap_supported: %s", strerror(-ret));
            return ret;
        }

        if (banks > MCE_BANKS_DEF) {
            banks = MCE_BANKS_DEF;
        }
        mcg_cap &= MCE_CAP_DEF;
        mcg_cap |= banks;
        ret = kvm_vcpu_ioctl(env, KVM_X86_SETUP_MCE, &mcg_cap);
        if (ret < 0) {
            fprintf(stderr, "KVM_X86_SETUP_MCE: %s", strerror(-ret));
            return ret;
        }

        env->mcg_cap = mcg_cap;
    }

    qemu_add_vm_change_state_handler(cpu_update_state, env);

    cpuid_data.cpuid.padding = 0;
    r = kvm_vcpu_ioctl(env, KVM_SET_CPUID2, &cpuid_data);
    if (r) {
        return r;
    }

    r = kvm_check_extension(env->kvm_state, KVM_CAP_TSC_CONTROL);
    if (r && env->tsc_khz) {
        r = kvm_vcpu_ioctl(env, KVM_SET_TSC_KHZ, env->tsc_khz);
        if (r < 0) {
            fprintf(stderr, "KVM_SET_TSC_KHZ failed\n");
            return r;
        }
    }

    if (kvm_has_xsave()) {
        env->kvm_xsave_buf = qemu_memalign(4096, sizeof(struct kvm_xsave));
    }

    return 0;
}

void kvm_arch_reset_vcpu(CPUX86State *env)
{
    X86CPU *cpu = x86_env_get_cpu(env);

    env->exception_injected = -1;
    env->interrupt_injected = -1;
    env->xcr0 = 1;
    if (kvm_irqchip_in_kernel()) {
        env->mp_state = cpu_is_bsp(cpu) ? KVM_MP_STATE_RUNNABLE :
                                          KVM_MP_STATE_UNINITIALIZED;
    } else {
        env->mp_state = KVM_MP_STATE_RUNNABLE;
    }
}

static int kvm_get_supported_msrs(KVMState *s)
{
    static int kvm_supported_msrs;
    int ret = 0;

    /* first time */
    if (kvm_supported_msrs == 0) {
        struct kvm_msr_list msr_list, *kvm_msr_list;

        kvm_supported_msrs = -1;

        /* Obtain MSR list from KVM.  These are the MSRs that we must
         * save/restore */
        msr_list.nmsrs = 0;
        ret = kvm_ioctl(s, KVM_GET_MSR_INDEX_LIST, &msr_list);
        if (ret < 0 && ret != -E2BIG) {
            return ret;
        }
        /* Old kernel modules had a bug and could write beyond the provided
           memory. Allocate at least a safe amount of 1K. */
        kvm_msr_list = g_malloc0(MAX(1024, sizeof(msr_list) +
                                              msr_list.nmsrs *
                                              sizeof(msr_list.indices[0])));

        kvm_msr_list->nmsrs = msr_list.nmsrs;
        ret = kvm_ioctl(s, KVM_GET_MSR_INDEX_LIST, kvm_msr_list);
        if (ret >= 0) {
            int i;

            for (i = 0; i < kvm_msr_list->nmsrs; i++) {
                if (kvm_msr_list->indices[i] == MSR_STAR) {
                    has_msr_star = true;
                    continue;
                }
                if (kvm_msr_list->indices[i] == MSR_VM_HSAVE_PA) {
                    has_msr_hsave_pa = true;
                    continue;
                }
                if (kvm_msr_list->indices[i] == MSR_IA32_TSCDEADLINE) {
                    has_msr_tsc_deadline = true;
                    continue;
                }
                if (kvm_msr_list->indices[i] == MSR_IA32_MISC_ENABLE) {
                    has_msr_misc_enable = true;
                    continue;
                }
            }
        }

        g_free(kvm_msr_list);
    }

    return ret;
}

int kvm_arch_init(KVMState *s)
{
    QemuOptsList *list = qemu_find_opts("machine");
    uint64_t identity_base = 0xfffbc000;
    uint64_t shadow_mem;
    int ret;
    struct utsname utsname;

    ret = kvm_get_supported_msrs(s);
    if (ret < 0) {
        return ret;
    }

    uname(&utsname);
    lm_capable_kernel = strcmp(utsname.machine, "x86_64") == 0;

    /*
     * On older Intel CPUs, KVM uses vm86 mode to emulate 16-bit code directly.
     * In order to use vm86 mode, an EPT identity map and a TSS  are needed.
     * Since these must be part of guest physical memory, we need to allocate
     * them, both by setting their start addresses in the kernel and by
     * creating a corresponding e820 entry. We need 4 pages before the BIOS.
     *
     * Older KVM versions may not support setting the identity map base. In
     * that case we need to stick with the default, i.e. a 256K maximum BIOS
     * size.
     */
    if (kvm_check_extension(s, KVM_CAP_SET_IDENTITY_MAP_ADDR)) {
        /* Allows up to 16M BIOSes. */
        identity_base = 0xfeffc000;

        ret = kvm_vm_ioctl(s, KVM_SET_IDENTITY_MAP_ADDR, &identity_base);
        if (ret < 0) {
            return ret;
        }
    }

    /* Set TSS base one page after EPT identity map. */
    ret = kvm_vm_ioctl(s, KVM_SET_TSS_ADDR, identity_base + 0x1000);
    if (ret < 0) {
        return ret;
    }

    /* Tell fw_cfg to notify the BIOS to reserve the range. */
    ret = e820_add_entry(identity_base, 0x4000, E820_RESERVED);
    if (ret < 0) {
        fprintf(stderr, "e820_add_entry() table is full\n");
        return ret;
    }
    qemu_register_reset(kvm_unpoison_all, NULL);

    if (!QTAILQ_EMPTY(&list->head)) {
        shadow_mem = qemu_opt_get_size(QTAILQ_FIRST(&list->head),
                                       "kvm_shadow_mem", -1);
        if (shadow_mem != -1) {
            shadow_mem /= 4096;
            ret = kvm_vm_ioctl(s, KVM_SET_NR_MMU_PAGES, shadow_mem);
            if (ret < 0) {
                return ret;
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
    lhs->unusable = 0;
    lhs->padding = 0;
}

static void get_seg(SegmentCache *lhs, const struct kvm_segment *rhs)
{
    lhs->selector = rhs->selector;
    lhs->base = rhs->base;
    lhs->limit = rhs->limit;
    lhs->flags = (rhs->type << DESC_TYPE_SHIFT) |
                 (rhs->present * DESC_P_MASK) |
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

static int kvm_getput_regs(CPUX86State *env, int set)
{
    struct kvm_regs regs;
    int ret = 0;

    if (!set) {
        ret = kvm_vcpu_ioctl(env, KVM_GET_REGS, &regs);
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
        ret = kvm_vcpu_ioctl(env, KVM_SET_REGS, &regs);
    }

    return ret;
}

static int kvm_put_fpu(CPUX86State *env)
{
    struct kvm_fpu fpu;
    int i;

    memset(&fpu, 0, sizeof fpu);
    fpu.fsw = env->fpus & ~(7 << 11);
    fpu.fsw |= (env->fpstt & 7) << 11;
    fpu.fcw = env->fpuc;
    fpu.last_opcode = env->fpop;
    fpu.last_ip = env->fpip;
    fpu.last_dp = env->fpdp;
    for (i = 0; i < 8; ++i) {
        fpu.ftwx |= (!env->fptags[i]) << i;
    }
    memcpy(fpu.fpr, env->fpregs, sizeof env->fpregs);
    memcpy(fpu.xmm, env->xmm_regs, sizeof env->xmm_regs);
    fpu.mxcsr = env->mxcsr;

    return kvm_vcpu_ioctl(env, KVM_SET_FPU, &fpu);
}

#define XSAVE_FCW_FSW     0
#define XSAVE_FTW_FOP     1
#define XSAVE_CWD_RIP     2
#define XSAVE_CWD_RDP     4
#define XSAVE_MXCSR       6
#define XSAVE_ST_SPACE    8
#define XSAVE_XMM_SPACE   40
#define XSAVE_XSTATE_BV   128
#define XSAVE_YMMH_SPACE  144

static int kvm_put_xsave(CPUX86State *env)
{
    struct kvm_xsave* xsave = env->kvm_xsave_buf;
    uint16_t cwd, swd, twd;
    int i, r;

    if (!kvm_has_xsave()) {
        return kvm_put_fpu(env);
    }

    memset(xsave, 0, sizeof(struct kvm_xsave));
    twd = 0;
    swd = env->fpus & ~(7 << 11);
    swd |= (env->fpstt & 7) << 11;
    cwd = env->fpuc;
    for (i = 0; i < 8; ++i) {
        twd |= (!env->fptags[i]) << i;
    }
    xsave->region[XSAVE_FCW_FSW] = (uint32_t)(swd << 16) + cwd;
    xsave->region[XSAVE_FTW_FOP] = (uint32_t)(env->fpop << 16) + twd;
    memcpy(&xsave->region[XSAVE_CWD_RIP], &env->fpip, sizeof(env->fpip));
    memcpy(&xsave->region[XSAVE_CWD_RDP], &env->fpdp, sizeof(env->fpdp));
    memcpy(&xsave->region[XSAVE_ST_SPACE], env->fpregs,
            sizeof env->fpregs);
    memcpy(&xsave->region[XSAVE_XMM_SPACE], env->xmm_regs,
            sizeof env->xmm_regs);
    xsave->region[XSAVE_MXCSR] = env->mxcsr;
    *(uint64_t *)&xsave->region[XSAVE_XSTATE_BV] = env->xstate_bv;
    memcpy(&xsave->region[XSAVE_YMMH_SPACE], env->ymmh_regs,
            sizeof env->ymmh_regs);
    r = kvm_vcpu_ioctl(env, KVM_SET_XSAVE, xsave);
    return r;
}

static int kvm_put_xcrs(CPUX86State *env)
{
    struct kvm_xcrs xcrs;

    if (!kvm_has_xcrs()) {
        return 0;
    }

    xcrs.nr_xcrs = 1;
    xcrs.flags = 0;
    xcrs.xcrs[0].xcr = 0;
    xcrs.xcrs[0].value = env->xcr0;
    return kvm_vcpu_ioctl(env, KVM_SET_XCRS, &xcrs);
}

static int kvm_put_sregs(CPUX86State *env)
{
    struct kvm_sregs sregs;

    memset(sregs.interrupt_bitmap, 0, sizeof(sregs.interrupt_bitmap));
    if (env->interrupt_injected >= 0) {
        sregs.interrupt_bitmap[env->interrupt_injected / 64] |=
                (uint64_t)1 << (env->interrupt_injected % 64);
    }

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

    sregs.cr8 = cpu_get_apic_tpr(env->apic_state);
    sregs.apic_base = cpu_get_apic_base(env->apic_state);

    sregs.efer = env->efer;

    return kvm_vcpu_ioctl(env, KVM_SET_SREGS, &sregs);
}

static void kvm_msr_entry_set(struct kvm_msr_entry *entry,
                              uint32_t index, uint64_t value)
{
    entry->index = index;
    entry->data = value;
}

static int kvm_put_msrs(CPUX86State *env, int level)
{
    struct {
        struct kvm_msrs info;
        struct kvm_msr_entry entries[100];
    } msr_data;
    struct kvm_msr_entry *msrs = msr_data.entries;
    int n = 0;

    kvm_msr_entry_set(&msrs[n++], MSR_IA32_SYSENTER_CS, env->sysenter_cs);
    kvm_msr_entry_set(&msrs[n++], MSR_IA32_SYSENTER_ESP, env->sysenter_esp);
    kvm_msr_entry_set(&msrs[n++], MSR_IA32_SYSENTER_EIP, env->sysenter_eip);
    kvm_msr_entry_set(&msrs[n++], MSR_PAT, env->pat);
    if (has_msr_star) {
        kvm_msr_entry_set(&msrs[n++], MSR_STAR, env->star);
    }
    if (has_msr_hsave_pa) {
        kvm_msr_entry_set(&msrs[n++], MSR_VM_HSAVE_PA, env->vm_hsave);
    }
    if (has_msr_tsc_deadline) {
        kvm_msr_entry_set(&msrs[n++], MSR_IA32_TSCDEADLINE, env->tsc_deadline);
    }
    if (has_msr_misc_enable) {
        kvm_msr_entry_set(&msrs[n++], MSR_IA32_MISC_ENABLE,
                          env->msr_ia32_misc_enable);
    }
#ifdef TARGET_X86_64
    if (lm_capable_kernel) {
        kvm_msr_entry_set(&msrs[n++], MSR_CSTAR, env->cstar);
        kvm_msr_entry_set(&msrs[n++], MSR_KERNELGSBASE, env->kernelgsbase);
        kvm_msr_entry_set(&msrs[n++], MSR_FMASK, env->fmask);
        kvm_msr_entry_set(&msrs[n++], MSR_LSTAR, env->lstar);
    }
#endif
    if (level == KVM_PUT_FULL_STATE) {
        /*
         * KVM is yet unable to synchronize TSC values of multiple VCPUs on
         * writeback. Until this is fixed, we only write the offset to SMP
         * guests after migration, desynchronizing the VCPUs, but avoiding
         * huge jump-backs that would occur without any writeback at all.
         */
        if (smp_cpus == 1 || env->tsc != 0) {
            kvm_msr_entry_set(&msrs[n++], MSR_IA32_TSC, env->tsc);
        }
    }
    /*
     * The following paravirtual MSRs have side effects on the guest or are
     * too heavy for normal writeback. Limit them to reset or full state
     * updates.
     */
    if (level >= KVM_PUT_RESET_STATE) {
        kvm_msr_entry_set(&msrs[n++], MSR_KVM_SYSTEM_TIME,
                          env->system_time_msr);
        kvm_msr_entry_set(&msrs[n++], MSR_KVM_WALL_CLOCK, env->wall_clock_msr);
        if (has_msr_async_pf_en) {
            kvm_msr_entry_set(&msrs[n++], MSR_KVM_ASYNC_PF_EN,
                              env->async_pf_en_msr);
        }
        if (has_msr_pv_eoi_en) {
            kvm_msr_entry_set(&msrs[n++], MSR_KVM_PV_EOI_EN,
                              env->pv_eoi_en_msr);
        }
        if (hyperv_hypercall_available()) {
            kvm_msr_entry_set(&msrs[n++], HV_X64_MSR_GUEST_OS_ID, 0);
            kvm_msr_entry_set(&msrs[n++], HV_X64_MSR_HYPERCALL, 0);
        }
        if (hyperv_vapic_recommended()) {
            kvm_msr_entry_set(&msrs[n++], HV_X64_MSR_APIC_ASSIST_PAGE, 0);
        }
    }
    if (env->mcg_cap) {
        int i;

        kvm_msr_entry_set(&msrs[n++], MSR_MCG_STATUS, env->mcg_status);
        kvm_msr_entry_set(&msrs[n++], MSR_MCG_CTL, env->mcg_ctl);
        for (i = 0; i < (env->mcg_cap & 0xff) * 4; i++) {
            kvm_msr_entry_set(&msrs[n++], MSR_MC0_CTL + i, env->mce_banks[i]);
        }
    }

    msr_data.info.nmsrs = n;

    return kvm_vcpu_ioctl(env, KVM_SET_MSRS, &msr_data);

}


static int kvm_get_fpu(CPUX86State *env)
{
    struct kvm_fpu fpu;
    int i, ret;

    ret = kvm_vcpu_ioctl(env, KVM_GET_FPU, &fpu);
    if (ret < 0) {
        return ret;
    }

    env->fpstt = (fpu.fsw >> 11) & 7;
    env->fpus = fpu.fsw;
    env->fpuc = fpu.fcw;
    env->fpop = fpu.last_opcode;
    env->fpip = fpu.last_ip;
    env->fpdp = fpu.last_dp;
    for (i = 0; i < 8; ++i) {
        env->fptags[i] = !((fpu.ftwx >> i) & 1);
    }
    memcpy(env->fpregs, fpu.fpr, sizeof env->fpregs);
    memcpy(env->xmm_regs, fpu.xmm, sizeof env->xmm_regs);
    env->mxcsr = fpu.mxcsr;

    return 0;
}

static int kvm_get_xsave(CPUX86State *env)
{
    struct kvm_xsave* xsave = env->kvm_xsave_buf;
    int ret, i;
    uint16_t cwd, swd, twd;

    if (!kvm_has_xsave()) {
        return kvm_get_fpu(env);
    }

    ret = kvm_vcpu_ioctl(env, KVM_GET_XSAVE, xsave);
    if (ret < 0) {
        return ret;
    }

    cwd = (uint16_t)xsave->region[XSAVE_FCW_FSW];
    swd = (uint16_t)(xsave->region[XSAVE_FCW_FSW] >> 16);
    twd = (uint16_t)xsave->region[XSAVE_FTW_FOP];
    env->fpop = (uint16_t)(xsave->region[XSAVE_FTW_FOP] >> 16);
    env->fpstt = (swd >> 11) & 7;
    env->fpus = swd;
    env->fpuc = cwd;
    for (i = 0; i < 8; ++i) {
        env->fptags[i] = !((twd >> i) & 1);
    }
    memcpy(&env->fpip, &xsave->region[XSAVE_CWD_RIP], sizeof(env->fpip));
    memcpy(&env->fpdp, &xsave->region[XSAVE_CWD_RDP], sizeof(env->fpdp));
    env->mxcsr = xsave->region[XSAVE_MXCSR];
    memcpy(env->fpregs, &xsave->region[XSAVE_ST_SPACE],
            sizeof env->fpregs);
    memcpy(env->xmm_regs, &xsave->region[XSAVE_XMM_SPACE],
            sizeof env->xmm_regs);
    env->xstate_bv = *(uint64_t *)&xsave->region[XSAVE_XSTATE_BV];
    memcpy(env->ymmh_regs, &xsave->region[XSAVE_YMMH_SPACE],
            sizeof env->ymmh_regs);
    return 0;
}

static int kvm_get_xcrs(CPUX86State *env)
{
    int i, ret;
    struct kvm_xcrs xcrs;

    if (!kvm_has_xcrs()) {
        return 0;
    }

    ret = kvm_vcpu_ioctl(env, KVM_GET_XCRS, &xcrs);
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < xcrs.nr_xcrs; i++) {
        /* Only support xcr0 now */
        if (xcrs.xcrs[0].xcr == 0) {
            env->xcr0 = xcrs.xcrs[0].value;
            break;
        }
    }
    return 0;
}

static int kvm_get_sregs(CPUX86State *env)
{
    struct kvm_sregs sregs;
    uint32_t hflags;
    int bit, i, ret;

    ret = kvm_vcpu_ioctl(env, KVM_GET_SREGS, &sregs);
    if (ret < 0) {
        return ret;
    }

    /* There can only be one pending IRQ set in the bitmap at a time, so try
       to find it and save its number instead (-1 for none). */
    env->interrupt_injected = -1;
    for (i = 0; i < ARRAY_SIZE(sregs.interrupt_bitmap); i++) {
        if (sregs.interrupt_bitmap[i]) {
            bit = ctz64(sregs.interrupt_bitmap[i]);
            env->interrupt_injected = i * 64 + bit;
            break;
        }
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

    /* changes to apic base and cr8/tpr are read back via kvm_arch_post_run */

#define HFLAG_COPY_MASK \
    ~( HF_CPL_MASK | HF_PE_MASK | HF_MP_MASK | HF_EM_MASK | \
       HF_TS_MASK | HF_TF_MASK | HF_VM_MASK | HF_IOPL_MASK | \
       HF_OSFXSR_MASK | HF_LMA_MASK | HF_CS32_MASK | \
       HF_SS32_MASK | HF_CS64_MASK | HF_ADDSEG_MASK)

    hflags = (env->segs[R_CS].flags >> DESC_DPL_SHIFT) & HF_CPL_MASK;
    hflags |= (env->cr[0] & CR0_PE_MASK) << (HF_PE_SHIFT - CR0_PE_SHIFT);
    hflags |= (env->cr[0] << (HF_MP_SHIFT - CR0_MP_SHIFT)) &
                (HF_MP_MASK | HF_EM_MASK | HF_TS_MASK);
    hflags |= (env->eflags & (HF_TF_MASK | HF_VM_MASK | HF_IOPL_MASK));
    hflags |= (env->cr[4] & CR4_OSFXSR_MASK) <<
                (HF_OSFXSR_SHIFT - CR4_OSFXSR_SHIFT);

    if (env->efer & MSR_EFER_LMA) {
        hflags |= HF_LMA_MASK;
    }

    if ((hflags & HF_LMA_MASK) && (env->segs[R_CS].flags & DESC_L_MASK)) {
        hflags |= HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK;
    } else {
        hflags |= (env->segs[R_CS].flags & DESC_B_MASK) >>
                    (DESC_B_SHIFT - HF_CS32_SHIFT);
        hflags |= (env->segs[R_SS].flags & DESC_B_MASK) >>
                    (DESC_B_SHIFT - HF_SS32_SHIFT);
        if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK) ||
            !(hflags & HF_CS32_MASK)) {
            hflags |= HF_ADDSEG_MASK;
        } else {
            hflags |= ((env->segs[R_DS].base | env->segs[R_ES].base |
                        env->segs[R_SS].base) != 0) << HF_ADDSEG_SHIFT;
        }
    }
    env->hflags = (env->hflags & HFLAG_COPY_MASK) | hflags;

    return 0;
}

static int kvm_get_msrs(CPUX86State *env)
{
    struct {
        struct kvm_msrs info;
        struct kvm_msr_entry entries[100];
    } msr_data;
    struct kvm_msr_entry *msrs = msr_data.entries;
    int ret, i, n;

    n = 0;
    msrs[n++].index = MSR_IA32_SYSENTER_CS;
    msrs[n++].index = MSR_IA32_SYSENTER_ESP;
    msrs[n++].index = MSR_IA32_SYSENTER_EIP;
    msrs[n++].index = MSR_PAT;
    if (has_msr_star) {
        msrs[n++].index = MSR_STAR;
    }
    if (has_msr_hsave_pa) {
        msrs[n++].index = MSR_VM_HSAVE_PA;
    }
    if (has_msr_tsc_deadline) {
        msrs[n++].index = MSR_IA32_TSCDEADLINE;
    }
    if (has_msr_misc_enable) {
        msrs[n++].index = MSR_IA32_MISC_ENABLE;
    }

    if (!env->tsc_valid) {
        msrs[n++].index = MSR_IA32_TSC;
        env->tsc_valid = !runstate_is_running();
    }

#ifdef TARGET_X86_64
    if (lm_capable_kernel) {
        msrs[n++].index = MSR_CSTAR;
        msrs[n++].index = MSR_KERNELGSBASE;
        msrs[n++].index = MSR_FMASK;
        msrs[n++].index = MSR_LSTAR;
    }
#endif
    msrs[n++].index = MSR_KVM_SYSTEM_TIME;
    msrs[n++].index = MSR_KVM_WALL_CLOCK;
    if (has_msr_async_pf_en) {
        msrs[n++].index = MSR_KVM_ASYNC_PF_EN;
    }
    if (has_msr_pv_eoi_en) {
        msrs[n++].index = MSR_KVM_PV_EOI_EN;
    }

    if (env->mcg_cap) {
        msrs[n++].index = MSR_MCG_STATUS;
        msrs[n++].index = MSR_MCG_CTL;
        for (i = 0; i < (env->mcg_cap & 0xff) * 4; i++) {
            msrs[n++].index = MSR_MC0_CTL + i;
        }
    }

    msr_data.info.nmsrs = n;
    ret = kvm_vcpu_ioctl(env, KVM_GET_MSRS, &msr_data);
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < ret; i++) {
        switch (msrs[i].index) {
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
#endif
        case MSR_IA32_TSC:
            env->tsc = msrs[i].data;
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
        case MSR_IA32_MISC_ENABLE:
            env->msr_ia32_misc_enable = msrs[i].data;
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
        case MSR_KVM_PV_EOI_EN:
            env->pv_eoi_en_msr = msrs[i].data;
            break;
        }
    }

    return 0;
}

static int kvm_put_mp_state(CPUX86State *env)
{
    struct kvm_mp_state mp_state = { .mp_state = env->mp_state };

    return kvm_vcpu_ioctl(env, KVM_SET_MP_STATE, &mp_state);
}

static int kvm_get_mp_state(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kvm_mp_state mp_state;
    int ret;

    ret = kvm_vcpu_ioctl(env, KVM_GET_MP_STATE, &mp_state);
    if (ret < 0) {
        return ret;
    }
    env->mp_state = mp_state.mp_state;
    if (kvm_irqchip_in_kernel()) {
        env->halted = (mp_state.mp_state == KVM_MP_STATE_HALTED);
    }
    return 0;
}

static int kvm_get_apic(CPUX86State *env)
{
    DeviceState *apic = env->apic_state;
    struct kvm_lapic_state kapic;
    int ret;

    if (apic && kvm_irqchip_in_kernel()) {
        ret = kvm_vcpu_ioctl(env, KVM_GET_LAPIC, &kapic);
        if (ret < 0) {
            return ret;
        }

        kvm_get_apic_state(apic, &kapic);
    }
    return 0;
}

static int kvm_put_apic(CPUX86State *env)
{
    DeviceState *apic = env->apic_state;
    struct kvm_lapic_state kapic;

    if (apic && kvm_irqchip_in_kernel()) {
        kvm_put_apic_state(apic, &kapic);

        return kvm_vcpu_ioctl(env, KVM_SET_LAPIC, &kapic);
    }
    return 0;
}

static int kvm_put_vcpu_events(CPUX86State *env, int level)
{
    struct kvm_vcpu_events events;

    if (!kvm_has_vcpu_events()) {
        return 0;
    }

    events.exception.injected = (env->exception_injected >= 0);
    events.exception.nr = env->exception_injected;
    events.exception.has_error_code = env->has_error_code;
    events.exception.error_code = env->error_code;
    events.exception.pad = 0;

    events.interrupt.injected = (env->interrupt_injected >= 0);
    events.interrupt.nr = env->interrupt_injected;
    events.interrupt.soft = env->soft_interrupt;

    events.nmi.injected = env->nmi_injected;
    events.nmi.pending = env->nmi_pending;
    events.nmi.masked = !!(env->hflags2 & HF2_NMI_MASK);
    events.nmi.pad = 0;

    events.sipi_vector = env->sipi_vector;

    events.flags = 0;
    if (level >= KVM_PUT_RESET_STATE) {
        events.flags |=
            KVM_VCPUEVENT_VALID_NMI_PENDING | KVM_VCPUEVENT_VALID_SIPI_VECTOR;
    }

    return kvm_vcpu_ioctl(env, KVM_SET_VCPU_EVENTS, &events);
}

static int kvm_get_vcpu_events(CPUX86State *env)
{
    struct kvm_vcpu_events events;
    int ret;

    if (!kvm_has_vcpu_events()) {
        return 0;
    }

    ret = kvm_vcpu_ioctl(env, KVM_GET_VCPU_EVENTS, &events);
    if (ret < 0) {
       return ret;
    }
    env->exception_injected =
       events.exception.injected ? events.exception.nr : -1;
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

    env->sipi_vector = events.sipi_vector;

    return 0;
}

static int kvm_guest_debug_workarounds(CPUX86State *env)
{
    int ret = 0;
    unsigned long reinject_trap = 0;

    if (!kvm_has_vcpu_events()) {
        if (env->exception_injected == 1) {
            reinject_trap = KVM_GUESTDBG_INJECT_DB;
        } else if (env->exception_injected == 3) {
            reinject_trap = KVM_GUESTDBG_INJECT_BP;
        }
        env->exception_injected = -1;
    }

    /*
     * Kernels before KVM_CAP_X86_ROBUST_SINGLESTEP overwrote flags.TF
     * injected via SET_GUEST_DEBUG while updating GP regs. Work around this
     * by updating the debug state once again if single-stepping is on.
     * Another reason to call kvm_update_guest_debug here is a pending debug
     * trap raise by the guest. On kernels without SET_VCPU_EVENTS we have to
     * reinject them via SET_GUEST_DEBUG.
     */
    if (reinject_trap ||
        (!kvm_has_robust_singlestep() && env->singlestep_enabled)) {
        ret = kvm_update_guest_debug(env, reinject_trap);
    }
    return ret;
}

static int kvm_put_debugregs(CPUX86State *env)
{
    struct kvm_debugregs dbgregs;
    int i;

    if (!kvm_has_debugregs()) {
        return 0;
    }

    for (i = 0; i < 4; i++) {
        dbgregs.db[i] = env->dr[i];
    }
    dbgregs.dr6 = env->dr[6];
    dbgregs.dr7 = env->dr[7];
    dbgregs.flags = 0;

    return kvm_vcpu_ioctl(env, KVM_SET_DEBUGREGS, &dbgregs);
}

static int kvm_get_debugregs(CPUX86State *env)
{
    struct kvm_debugregs dbgregs;
    int i, ret;

    if (!kvm_has_debugregs()) {
        return 0;
    }

    ret = kvm_vcpu_ioctl(env, KVM_GET_DEBUGREGS, &dbgregs);
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

int kvm_arch_put_registers(CPUX86State *env, int level)
{
    CPUState *cpu = ENV_GET_CPU(env);
    int ret;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    ret = kvm_getput_regs(env, 1);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_xsave(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_xcrs(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_sregs(env);
    if (ret < 0) {
        return ret;
    }
    /* must be before kvm_put_msrs */
    ret = kvm_inject_mce_oldstyle(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_msrs(env, level);
    if (ret < 0) {
        return ret;
    }
    if (level >= KVM_PUT_RESET_STATE) {
        ret = kvm_put_mp_state(env);
        if (ret < 0) {
            return ret;
        }
        ret = kvm_put_apic(env);
        if (ret < 0) {
            return ret;
        }
    }
    ret = kvm_put_vcpu_events(env, level);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_put_debugregs(env);
    if (ret < 0) {
        return ret;
    }
    /* must be last */
    ret = kvm_guest_debug_workarounds(env);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

int kvm_arch_get_registers(CPUX86State *env)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    int ret;

    assert(cpu_is_stopped(CPU(cpu)) || qemu_cpu_is_self(CPU(cpu)));

    ret = kvm_getput_regs(env, 0);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_get_xsave(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_get_xcrs(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_get_sregs(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_get_msrs(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_get_mp_state(cpu);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_get_apic(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_get_vcpu_events(env);
    if (ret < 0) {
        return ret;
    }
    ret = kvm_get_debugregs(env);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

void kvm_arch_pre_run(CPUX86State *env, struct kvm_run *run)
{
    int ret;

    /* Inject NMI */
    if (env->interrupt_request & CPU_INTERRUPT_NMI) {
        env->interrupt_request &= ~CPU_INTERRUPT_NMI;
        DPRINTF("injected NMI\n");
        ret = kvm_vcpu_ioctl(env, KVM_NMI);
        if (ret < 0) {
            fprintf(stderr, "KVM: injection failed, NMI lost (%s)\n",
                    strerror(-ret));
        }
    }

    if (!kvm_irqchip_in_kernel()) {
        /* Force the VCPU out of its inner loop to process any INIT requests
         * or pending TPR access reports. */
        if (env->interrupt_request &
            (CPU_INTERRUPT_INIT | CPU_INTERRUPT_TPR)) {
            env->exit_request = 1;
        }

        /* Try to inject an interrupt if the guest can accept it */
        if (run->ready_for_interrupt_injection &&
            (env->interrupt_request & CPU_INTERRUPT_HARD) &&
            (env->eflags & IF_MASK)) {
            int irq;

            env->interrupt_request &= ~CPU_INTERRUPT_HARD;
            irq = cpu_get_pic_interrupt(env);
            if (irq >= 0) {
                struct kvm_interrupt intr;

                intr.irq = irq;
                DPRINTF("injected interrupt %d\n", irq);
                ret = kvm_vcpu_ioctl(env, KVM_INTERRUPT, &intr);
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
        if ((env->interrupt_request & CPU_INTERRUPT_HARD)) {
            run->request_interrupt_window = 1;
        } else {
            run->request_interrupt_window = 0;
        }

        DPRINTF("setting tpr\n");
        run->cr8 = cpu_get_apic_tpr(env->apic_state);
    }
}

void kvm_arch_post_run(CPUX86State *env, struct kvm_run *run)
{
    if (run->if_flag) {
        env->eflags |= IF_MASK;
    } else {
        env->eflags &= ~IF_MASK;
    }
    cpu_set_apic_tpr(env->apic_state, run->cr8);
    cpu_set_apic_base(env->apic_state, run->apic_base);
}

int kvm_arch_process_async_events(CPUX86State *env)
{
    X86CPU *cpu = x86_env_get_cpu(env);

    if (env->interrupt_request & CPU_INTERRUPT_MCE) {
        /* We must not raise CPU_INTERRUPT_MCE if it's not supported. */
        assert(env->mcg_cap);

        env->interrupt_request &= ~CPU_INTERRUPT_MCE;

        kvm_cpu_synchronize_state(env);

        if (env->exception_injected == EXCP08_DBLE) {
            /* this means triple fault */
            qemu_system_reset_request();
            env->exit_request = 1;
            return 0;
        }
        env->exception_injected = EXCP12_MCHK;
        env->has_error_code = 0;

        env->halted = 0;
        if (kvm_irqchip_in_kernel() && env->mp_state == KVM_MP_STATE_HALTED) {
            env->mp_state = KVM_MP_STATE_RUNNABLE;
        }
    }

    if (kvm_irqchip_in_kernel()) {
        return 0;
    }

    if (env->interrupt_request & CPU_INTERRUPT_POLL) {
        env->interrupt_request &= ~CPU_INTERRUPT_POLL;
        apic_poll_irq(env->apic_state);
    }
    if (((env->interrupt_request & CPU_INTERRUPT_HARD) &&
         (env->eflags & IF_MASK)) ||
        (env->interrupt_request & CPU_INTERRUPT_NMI)) {
        env->halted = 0;
    }
    if (env->interrupt_request & CPU_INTERRUPT_INIT) {
        kvm_cpu_synchronize_state(env);
        do_cpu_init(cpu);
    }
    if (env->interrupt_request & CPU_INTERRUPT_SIPI) {
        kvm_cpu_synchronize_state(env);
        do_cpu_sipi(cpu);
    }
    if (env->interrupt_request & CPU_INTERRUPT_TPR) {
        env->interrupt_request &= ~CPU_INTERRUPT_TPR;
        kvm_cpu_synchronize_state(env);
        apic_handle_tpr_access_report(env->apic_state, env->eip,
                                      env->tpr_access_type);
    }

    return env->halted;
}

static int kvm_handle_halt(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    if (!((env->interrupt_request & CPU_INTERRUPT_HARD) &&
          (env->eflags & IF_MASK)) &&
        !(env->interrupt_request & CPU_INTERRUPT_NMI)) {
        env->halted = 1;
        return EXCP_HLT;
    }

    return 0;
}

static int kvm_handle_tpr_access(CPUX86State *env)
{
    struct kvm_run *run = env->kvm_run;

    apic_handle_tpr_access_report(env->apic_state, run->tpr_access.rip,
                                  run->tpr_access.is_write ? TPR_ACCESS_WRITE
                                                           : TPR_ACCESS_READ);
    return 1;
}

int kvm_arch_insert_sw_breakpoint(CPUX86State *env, struct kvm_sw_breakpoint *bp)
{
    static const uint8_t int3 = 0xcc;

    if (cpu_memory_rw_debug(env, bp->pc, (uint8_t *)&bp->saved_insn, 1, 0) ||
        cpu_memory_rw_debug(env, bp->pc, (uint8_t *)&int3, 1, 1)) {
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUX86State *env, struct kvm_sw_breakpoint *bp)
{
    uint8_t int3;

    if (cpu_memory_rw_debug(env, bp->pc, &int3, 1, 0) || int3 != 0xcc ||
        cpu_memory_rw_debug(env, bp->pc, (uint8_t *)&bp->saved_insn, 1, 1)) {
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

int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
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

int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
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

static int kvm_handle_debug(CPUX86State *env,
                            struct kvm_debug_exit_arch *arch_info)
{
    int ret = 0;
    int n;

    if (arch_info->exception == 1) {
        if (arch_info->dr6 & (1 << 14)) {
            if (env->singlestep_enabled) {
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
                        env->watchpoint_hit = &hw_watchpoint;
                        hw_watchpoint.vaddr = hw_breakpoint[n].addr;
                        hw_watchpoint.flags = BP_MEM_WRITE;
                        break;
                    case 0x3:
                        ret = EXCP_DEBUG;
                        env->watchpoint_hit = &hw_watchpoint;
                        hw_watchpoint.vaddr = hw_breakpoint[n].addr;
                        hw_watchpoint.flags = BP_MEM_ACCESS;
                        break;
                    }
                }
            }
        }
    } else if (kvm_find_sw_breakpoint(env, arch_info->pc)) {
        ret = EXCP_DEBUG;
    }
    if (ret == 0) {
        cpu_synchronize_state(env);
        assert(env->exception_injected == -1);

        /* pass to guest */
        env->exception_injected = arch_info->exception;
        env->has_error_code = 0;
    }

    return ret;
}

void kvm_arch_update_guest_debug(CPUX86State *env, struct kvm_guest_debug *dbg)
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

    if (kvm_sw_breakpoints_active(env)) {
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

static bool host_supports_vmx(void)
{
    uint32_t ecx, unused;

    host_cpuid(1, 0, &unused, &unused, &ecx, &unused);
    return ecx & CPUID_EXT_VMX;
}

#define VMX_INVALID_GUEST_STATE 0x80000021

int kvm_arch_handle_exit(CPUX86State *env, struct kvm_run *run)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    uint64_t code;
    int ret;

    switch (run->exit_reason) {
    case KVM_EXIT_HLT:
        DPRINTF("handle_hlt\n");
        ret = kvm_handle_halt(cpu);
        break;
    case KVM_EXIT_SET_TPR:
        ret = 0;
        break;
    case KVM_EXIT_TPR_ACCESS:
        ret = kvm_handle_tpr_access(env);
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
        ret = kvm_handle_debug(env, &run->debug.arch);
        break;
    default:
        fprintf(stderr, "KVM: unknown exit reason %d\n", run->exit_reason);
        ret = -1;
        break;
    }

    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUX86State *env)
{
    kvm_cpu_synchronize_state(env);
    return !(env->cr[0] & CR0_PE_MASK) ||
           ((env->segs[R_CS].selector  & 3) != 3);
}

void kvm_arch_init_irq_routing(KVMState *s)
{
    if (!kvm_check_extension(s, KVM_CAP_IRQ_ROUTING)) {
        /* If kernel can't do irq routing, interrupt source
         * override 0->2 cannot be set up as required by HPET.
         * So we have to disable it.
         */
        no_hpet = 1;
    }
    /* We know at this point that we're using the in-kernel
     * irqchip, so we can use irqfds, and on x86 we know
     * we can use msi via irqfd and GSI routing.
     */
    kvm_irqfds_allowed = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_routing_allowed = true;
}

/* Classic KVM device assignment interface. Will remain x86 only. */
int kvm_device_pci_assign(KVMState *s, PCIHostDeviceAddress *dev_addr,
                          uint32_t flags, uint32_t *dev_id)
{
    struct kvm_assigned_pci_dev dev_data = {
        .segnr = dev_addr->domain,
        .busnr = dev_addr->bus,
        .devfn = PCI_DEVFN(dev_addr->slot, dev_addr->function),
        .flags = flags,
    };
    int ret;

    dev_data.assigned_dev_id =
        (dev_addr->domain << 16) | (dev_addr->bus << 8) | dev_data.devfn;

    ret = kvm_vm_ioctl(s, KVM_ASSIGN_PCI_DEVICE, &dev_data);
    if (ret < 0) {
        return ret;
    }

    *dev_id = dev_data.assigned_dev_id;

    return 0;
}

int kvm_device_pci_deassign(KVMState *s, uint32_t dev_id)
{
    struct kvm_assigned_pci_dev dev_data = {
        .assigned_dev_id = dev_id,
    };

    return kvm_vm_ioctl(s, KVM_DEASSIGN_PCI_DEVICE, &dev_data);
}

static int kvm_assign_irq_internal(KVMState *s, uint32_t dev_id,
                                   uint32_t irq_type, uint32_t guest_irq)
{
    struct kvm_assigned_irq assigned_irq = {
        .assigned_dev_id = dev_id,
        .guest_irq = guest_irq,
        .flags = irq_type,
    };

    if (kvm_check_extension(s, KVM_CAP_ASSIGN_DEV_IRQ)) {
        return kvm_vm_ioctl(s, KVM_ASSIGN_DEV_IRQ, &assigned_irq);
    } else {
        return kvm_vm_ioctl(s, KVM_ASSIGN_IRQ, &assigned_irq);
    }
}

int kvm_device_intx_assign(KVMState *s, uint32_t dev_id, bool use_host_msi,
                           uint32_t guest_irq)
{
    uint32_t irq_type = KVM_DEV_IRQ_GUEST_INTX |
        (use_host_msi ? KVM_DEV_IRQ_HOST_MSI : KVM_DEV_IRQ_HOST_INTX);

    return kvm_assign_irq_internal(s, dev_id, irq_type, guest_irq);
}

int kvm_device_intx_set_mask(KVMState *s, uint32_t dev_id, bool masked)
{
    struct kvm_assigned_pci_dev dev_data = {
        .assigned_dev_id = dev_id,
        .flags = masked ? KVM_DEV_ASSIGN_MASK_INTX : 0,
    };

    return kvm_vm_ioctl(s, KVM_ASSIGN_SET_INTX_MASK, &dev_data);
}

static int kvm_deassign_irq_internal(KVMState *s, uint32_t dev_id,
                                     uint32_t type)
{
    struct kvm_assigned_irq assigned_irq = {
        .assigned_dev_id = dev_id,
        .flags = type,
    };

    return kvm_vm_ioctl(s, KVM_DEASSIGN_DEV_IRQ, &assigned_irq);
}

int kvm_device_intx_deassign(KVMState *s, uint32_t dev_id, bool use_host_msi)
{
    return kvm_deassign_irq_internal(s, dev_id, KVM_DEV_IRQ_GUEST_INTX |
        (use_host_msi ? KVM_DEV_IRQ_HOST_MSI : KVM_DEV_IRQ_HOST_INTX));
}

int kvm_device_msi_assign(KVMState *s, uint32_t dev_id, int virq)
{
    return kvm_assign_irq_internal(s, dev_id, KVM_DEV_IRQ_HOST_MSI |
                                              KVM_DEV_IRQ_GUEST_MSI, virq);
}

int kvm_device_msi_deassign(KVMState *s, uint32_t dev_id)
{
    return kvm_deassign_irq_internal(s, dev_id, KVM_DEV_IRQ_GUEST_MSI |
                                                KVM_DEV_IRQ_HOST_MSI);
}

bool kvm_device_msix_supported(KVMState *s)
{
    /* The kernel lacks a corresponding KVM_CAP, so we probe by calling
     * KVM_ASSIGN_SET_MSIX_NR with an invalid parameter. */
    return kvm_vm_ioctl(s, KVM_ASSIGN_SET_MSIX_NR, NULL) == -EFAULT;
}

int kvm_device_msix_init_vectors(KVMState *s, uint32_t dev_id,
                                 uint32_t nr_vectors)
{
    struct kvm_assigned_msix_nr msix_nr = {
        .assigned_dev_id = dev_id,
        .entry_nr = nr_vectors,
    };

    return kvm_vm_ioctl(s, KVM_ASSIGN_SET_MSIX_NR, &msix_nr);
}

int kvm_device_msix_set_vector(KVMState *s, uint32_t dev_id, uint32_t vector,
                               int virq)
{
    struct kvm_assigned_msix_entry msix_entry = {
        .assigned_dev_id = dev_id,
        .gsi = virq,
        .entry = vector,
    };

    return kvm_vm_ioctl(s, KVM_ASSIGN_SET_MSIX_ENTRY, &msix_entry);
}

int kvm_device_msix_assign(KVMState *s, uint32_t dev_id)
{
    return kvm_assign_irq_internal(s, dev_id, KVM_DEV_IRQ_HOST_MSIX |
                                              KVM_DEV_IRQ_GUEST_MSIX, 0);
}

int kvm_device_msix_deassign(KVMState *s, uint32_t dev_id)
{
    return kvm_deassign_irq_internal(s, dev_id, KVM_DEV_IRQ_GUEST_MSIX |
                                                KVM_DEV_IRQ_HOST_MSIX);
}
