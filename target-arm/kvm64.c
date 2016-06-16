/*
 * ARM implementation of KVM hooks, 64 bit specific code
 *
 * Copyright Mian-M. Hamayun 2013, Virtual Open Systems
 * Copyright Alex Bennée 2014, Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <sys/ptrace.h>

#include <linux/elf.h>
#include <linux/kvm.h>

#include "qemu-common.h"
#include "cpu.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "exec/gdbstub.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "internals.h"
#include "hw/arm/arm.h"

static bool have_guest_debug;

/*
 * Although the ARM implementation of hardware assisted debugging
 * allows for different breakpoints per-core, the current GDB
 * interface treats them as a global pool of registers (which seems to
 * be the case for x86, ppc and s390). As a result we store one copy
 * of registers which is used for all active cores.
 *
 * Write access is serialised by virtue of the GDB protocol which
 * updates things. Read access (i.e. when the values are copied to the
 * vCPU) is also gated by GDB's run control.
 *
 * This is not unreasonable as most of the time debugging kernels you
 * never know which core will eventually execute your function.
 */

typedef struct {
    uint64_t bcr;
    uint64_t bvr;
} HWBreakpoint;

/* The watchpoint registers can cover more area than the requested
 * watchpoint so we need to store the additional information
 * somewhere. We also need to supply a CPUWatchpoint to the GDB stub
 * when the watchpoint is hit.
 */
typedef struct {
    uint64_t wcr;
    uint64_t wvr;
    CPUWatchpoint details;
} HWWatchpoint;

/* Maximum and current break/watch point counts */
int max_hw_bps, max_hw_wps;
GArray *hw_breakpoints, *hw_watchpoints;

#define cur_hw_wps      (hw_watchpoints->len)
#define cur_hw_bps      (hw_breakpoints->len)
#define get_hw_bp(i)    (&g_array_index(hw_breakpoints, HWBreakpoint, i))
#define get_hw_wp(i)    (&g_array_index(hw_watchpoints, HWWatchpoint, i))

/**
 * kvm_arm_init_debug() - check for guest debug capabilities
 * @cs: CPUState
 *
 * kvm_check_extension returns the number of debug registers we have
 * or 0 if we have none.
 *
 */
static void kvm_arm_init_debug(CPUState *cs)
{
    have_guest_debug = kvm_check_extension(cs->kvm_state,
                                           KVM_CAP_SET_GUEST_DEBUG);

    max_hw_wps = kvm_check_extension(cs->kvm_state, KVM_CAP_GUEST_DEBUG_HW_WPS);
    hw_watchpoints = g_array_sized_new(true, true,
                                       sizeof(HWWatchpoint), max_hw_wps);

    max_hw_bps = kvm_check_extension(cs->kvm_state, KVM_CAP_GUEST_DEBUG_HW_BPS);
    hw_breakpoints = g_array_sized_new(true, true,
                                       sizeof(HWBreakpoint), max_hw_bps);
    return;
}

/**
 * insert_hw_breakpoint()
 * @addr: address of breakpoint
 *
 * See ARM ARM D2.9.1 for details but here we are only going to create
 * simple un-linked breakpoints (i.e. we don't chain breakpoints
 * together to match address and context or vmid). The hardware is
 * capable of fancier matching but that will require exposing that
 * fanciness to GDB's interface
 *
 * D7.3.2 DBGBCR<n>_EL1, Debug Breakpoint Control Registers
 *
 *  31  24 23  20 19   16 15 14  13  12   9 8   5 4    3 2   1  0
 * +------+------+-------+-----+----+------+-----+------+-----+---+
 * | RES0 |  BT  |  LBN  | SSC | HMC| RES0 | BAS | RES0 | PMC | E |
 * +------+------+-------+-----+----+------+-----+------+-----+---+
 *
 * BT: Breakpoint type (0 = unlinked address match)
 * LBN: Linked BP number (0 = unused)
 * SSC/HMC/PMC: Security, Higher and Priv access control (Table D-12)
 * BAS: Byte Address Select (RES1 for AArch64)
 * E: Enable bit
 */
static int insert_hw_breakpoint(target_ulong addr)
{
    HWBreakpoint brk = {
        .bcr = 0x1,                             /* BCR E=1, enable */
        .bvr = addr
    };

    if (cur_hw_bps >= max_hw_bps) {
        return -ENOBUFS;
    }

    brk.bcr = deposit32(brk.bcr, 1, 2, 0x3);   /* PMC = 11 */
    brk.bcr = deposit32(brk.bcr, 5, 4, 0xf);   /* BAS = RES1 */

    g_array_append_val(hw_breakpoints, brk);

    return 0;
}

/**
 * delete_hw_breakpoint()
 * @pc: address of breakpoint
 *
 * Delete a breakpoint and shuffle any above down
 */

static int delete_hw_breakpoint(target_ulong pc)
{
    int i;
    for (i = 0; i < hw_breakpoints->len; i++) {
        HWBreakpoint *brk = get_hw_bp(i);
        if (brk->bvr == pc) {
            g_array_remove_index(hw_breakpoints, i);
            return 0;
        }
    }
    return -ENOENT;
}

/**
 * insert_hw_watchpoint()
 * @addr: address of watch point
 * @len: size of area
 * @type: type of watch point
 *
 * See ARM ARM D2.10. As with the breakpoints we can do some advanced
 * stuff if we want to. The watch points can be linked with the break
 * points above to make them context aware. However for simplicity
 * currently we only deal with simple read/write watch points.
 *
 * D7.3.11 DBGWCR<n>_EL1, Debug Watchpoint Control Registers
 *
 *  31  29 28   24 23  21  20  19 16 15 14  13   12  5 4   3 2   1  0
 * +------+-------+------+----+-----+-----+-----+-----+-----+-----+---+
 * | RES0 |  MASK | RES0 | WT | LBN | SSC | HMC | BAS | LSC | PAC | E |
 * +------+-------+------+----+-----+-----+-----+-----+-----+-----+---+
 *
 * MASK: num bits addr mask (0=none,01/10=res,11=3 bits (8 bytes))
 * WT: 0 - unlinked, 1 - linked (not currently used)
 * LBN: Linked BP number (not currently used)
 * SSC/HMC/PAC: Security, Higher and Priv access control (Table D2-11)
 * BAS: Byte Address Select
 * LSC: Load/Store control (01: load, 10: store, 11: both)
 * E: Enable
 *
 * The bottom 2 bits of the value register are masked. Therefore to
 * break on any sizes smaller than an unaligned word you need to set
 * MASK=0, BAS=bit per byte in question. For larger regions (^2) you
 * need to ensure you mask the address as required and set BAS=0xff
 */

static int insert_hw_watchpoint(target_ulong addr,
                                target_ulong len, int type)
{
    HWWatchpoint wp = {
        .wcr = 1, /* E=1, enable */
        .wvr = addr & (~0x7ULL),
        .details = { .vaddr = addr, .len = len }
    };

    if (cur_hw_wps >= max_hw_wps) {
        return -ENOBUFS;
    }

    /*
     * HMC=0 SSC=0 PAC=3 will hit EL0 or EL1, any security state,
     * valid whether EL3 is implemented or not
     */
    wp.wcr = deposit32(wp.wcr, 1, 2, 3);

    switch (type) {
    case GDB_WATCHPOINT_READ:
        wp.wcr = deposit32(wp.wcr, 3, 2, 1);
        wp.details.flags = BP_MEM_READ;
        break;
    case GDB_WATCHPOINT_WRITE:
        wp.wcr = deposit32(wp.wcr, 3, 2, 2);
        wp.details.flags = BP_MEM_WRITE;
        break;
    case GDB_WATCHPOINT_ACCESS:
        wp.wcr = deposit32(wp.wcr, 3, 2, 3);
        wp.details.flags = BP_MEM_ACCESS;
        break;
    default:
        g_assert_not_reached();
        break;
    }
    if (len <= 8) {
        /* we align the address and set the bits in BAS */
        int off = addr & 0x7;
        int bas = (1 << len) - 1;

        wp.wcr = deposit32(wp.wcr, 5 + off, 8 - off, bas);
    } else {
        /* For ranges above 8 bytes we need to be a power of 2 */
        if (is_power_of_2(len)) {
            int bits = ctz64(len);

            wp.wvr &= ~((1 << bits) - 1);
            wp.wcr = deposit32(wp.wcr, 24, 4, bits);
            wp.wcr = deposit32(wp.wcr, 5, 8, 0xff);
        } else {
            return -ENOBUFS;
        }
    }

    g_array_append_val(hw_watchpoints, wp);
    return 0;
}


static bool check_watchpoint_in_range(int i, target_ulong addr)
{
    HWWatchpoint *wp = get_hw_wp(i);
    uint64_t addr_top, addr_bottom = wp->wvr;
    int bas = extract32(wp->wcr, 5, 8);
    int mask = extract32(wp->wcr, 24, 4);

    if (mask) {
        addr_top = addr_bottom + (1 << mask);
    } else {
        /* BAS must be contiguous but can offset against the base
         * address in DBGWVR */
        addr_bottom = addr_bottom + ctz32(bas);
        addr_top = addr_bottom + clo32(bas);
    }

    if (addr >= addr_bottom && addr <= addr_top) {
        return true;
    }

    return false;
}

/**
 * delete_hw_watchpoint()
 * @addr: address of breakpoint
 *
 * Delete a breakpoint and shuffle any above down
 */

static int delete_hw_watchpoint(target_ulong addr,
                                target_ulong len, int type)
{
    int i;
    for (i = 0; i < cur_hw_wps; i++) {
        if (check_watchpoint_in_range(i, addr)) {
            g_array_remove_index(hw_watchpoints, i);
            return 0;
        }
    }
    return -ENOENT;
}


int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_HW:
        return insert_hw_breakpoint(addr);
        break;
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_ACCESS:
        return insert_hw_watchpoint(addr, len, type);
    default:
        return -ENOSYS;
    }
}

int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_HW:
        return delete_hw_breakpoint(addr);
        break;
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_ACCESS:
        return delete_hw_watchpoint(addr, len, type);
    default:
        return -ENOSYS;
    }
}


void kvm_arch_remove_all_hw_breakpoints(void)
{
    if (cur_hw_wps > 0) {
        g_array_remove_range(hw_watchpoints, 0, cur_hw_wps);
    }
    if (cur_hw_bps > 0) {
        g_array_remove_range(hw_breakpoints, 0, cur_hw_bps);
    }
}

void kvm_arm_copy_hw_debug_data(struct kvm_guest_debug_arch *ptr)
{
    int i;
    memset(ptr, 0, sizeof(struct kvm_guest_debug_arch));

    for (i = 0; i < max_hw_wps; i++) {
        HWWatchpoint *wp = get_hw_wp(i);
        ptr->dbg_wcr[i] = wp->wcr;
        ptr->dbg_wvr[i] = wp->wvr;
    }
    for (i = 0; i < max_hw_bps; i++) {
        HWBreakpoint *bp = get_hw_bp(i);
        ptr->dbg_bcr[i] = bp->bcr;
        ptr->dbg_bvr[i] = bp->bvr;
    }
}

bool kvm_arm_hw_debug_active(CPUState *cs)
{
    return ((cur_hw_wps > 0) || (cur_hw_bps > 0));
}

static bool find_hw_breakpoint(CPUState *cpu, target_ulong pc)
{
    int i;

    for (i = 0; i < cur_hw_bps; i++) {
        HWBreakpoint *bp = get_hw_bp(i);
        if (bp->bvr == pc) {
            return true;
        }
    }
    return false;
}

static CPUWatchpoint *find_hw_watchpoint(CPUState *cpu, target_ulong addr)
{
    int i;

    for (i = 0; i < cur_hw_wps; i++) {
        if (check_watchpoint_in_range(i, addr)) {
            return &get_hw_wp(i)->details;
        }
    }
    return NULL;
}

static bool kvm_arm_pmu_support_ctrl(CPUState *cs, struct kvm_device_attr *attr)
{
    return kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, attr) == 0;
}

int kvm_arm_pmu_create(CPUState *cs, int irq)
{
    int err;

    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PMU_V3_CTRL,
        .addr = (intptr_t)&irq,
        .attr = KVM_ARM_VCPU_PMU_V3_IRQ,
        .flags = 0,
    };

    if (!kvm_arm_pmu_support_ctrl(cs, &attr)) {
        return 0;
    }

    err = kvm_vcpu_ioctl(cs, KVM_SET_DEVICE_ATTR, &attr);
    if (err < 0) {
        fprintf(stderr, "KVM_SET_DEVICE_ATTR failed: %s\n",
                strerror(-err));
        abort();
    }

    attr.group = KVM_ARM_VCPU_PMU_V3_CTRL;
    attr.attr = KVM_ARM_VCPU_PMU_V3_INIT;
    attr.addr = 0;
    attr.flags = 0;

    err = kvm_vcpu_ioctl(cs, KVM_SET_DEVICE_ATTR, &attr);
    if (err < 0) {
        fprintf(stderr, "KVM_SET_DEVICE_ATTR failed: %s\n",
                strerror(-err));
        abort();
    }

    return 1;
}

static inline void set_feature(uint64_t *features, int feature)
{
    *features |= 1ULL << feature;
}

bool kvm_arm_get_host_cpu_features(ARMHostCPUClass *ahcc)
{
    /* Identify the feature bits corresponding to the host CPU, and
     * fill out the ARMHostCPUClass fields accordingly. To do this
     * we have to create a scratch VM, create a single CPU inside it,
     * and then query that CPU for the relevant ID registers.
     * For AArch64 we currently don't care about ID registers at
     * all; we just want to know the CPU type.
     */
    int fdarray[3];
    uint64_t features = 0;
    /* Old kernels may not know about the PREFERRED_TARGET ioctl: however
     * we know these will only support creating one kind of guest CPU,
     * which is its preferred CPU type. Fortunately these old kernels
     * support only a very limited number of CPUs.
     */
    static const uint32_t cpus_to_try[] = {
        KVM_ARM_TARGET_AEM_V8,
        KVM_ARM_TARGET_FOUNDATION_V8,
        KVM_ARM_TARGET_CORTEX_A57,
        QEMU_KVM_ARM_TARGET_NONE
    };
    struct kvm_vcpu_init init;

    if (!kvm_arm_create_scratch_host_vcpu(cpus_to_try, fdarray, &init)) {
        return false;
    }

    ahcc->target = init.target;
    ahcc->dtb_compatible = "arm,arm-v8";

    kvm_arm_destroy_scratch_host_vcpu(fdarray);

   /* We can assume any KVM supporting CPU is at least a v8
     * with VFPv4+Neon; this in turn implies most of the other
     * feature bits.
     */
    set_feature(&features, ARM_FEATURE_V8);
    set_feature(&features, ARM_FEATURE_VFP4);
    set_feature(&features, ARM_FEATURE_NEON);
    set_feature(&features, ARM_FEATURE_AARCH64);

    ahcc->features = features;

    return true;
}

#define ARM_CPU_ID_MPIDR       3, 0, 0, 0, 5

int kvm_arch_init_vcpu(CPUState *cs)
{
    int ret;
    uint64_t mpidr;
    ARMCPU *cpu = ARM_CPU(cs);

    if (cpu->kvm_target == QEMU_KVM_ARM_TARGET_NONE ||
        !object_dynamic_cast(OBJECT(cpu), TYPE_AARCH64_CPU)) {
        fprintf(stderr, "KVM is not supported for this guest CPU type\n");
        return -EINVAL;
    }

    /* Determine init features for this CPU */
    memset(cpu->kvm_init_features, 0, sizeof(cpu->kvm_init_features));
    if (cpu->start_powered_off) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_POWER_OFF;
    }
    if (kvm_check_extension(cs->kvm_state, KVM_CAP_ARM_PSCI_0_2)) {
        cpu->psci_version = 2;
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PSCI_0_2;
    }
    if (!arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_EL1_32BIT;
    }
    if (kvm_irqchip_in_kernel() &&
        kvm_check_extension(cs->kvm_state, KVM_CAP_ARM_PMU_V3)) {
        cpu->has_pmu = true;
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PMU_V3;
    }

    /* Do KVM_ARM_VCPU_INIT ioctl */
    ret = kvm_arm_vcpu_init(cs);
    if (ret) {
        return ret;
    }

    /*
     * When KVM is in use, PSCI is emulated in-kernel and not by qemu.
     * Currently KVM has its own idea about MPIDR assignment, so we
     * override our defaults with what we get from KVM.
     */
    ret = kvm_get_one_reg(cs, ARM64_SYS_REG(ARM_CPU_ID_MPIDR), &mpidr);
    if (ret) {
        return ret;
    }
    cpu->mp_affinity = mpidr & ARM64_AFFINITY_MASK;

    kvm_arm_init_debug(cs);

    return kvm_arm_init_cpreg_list(cpu);
}

bool kvm_arm_reg_syncs_via_cpreg_list(uint64_t regidx)
{
    /* Return true if the regidx is a register we should synchronize
     * via the cpreg_tuples array (ie is not a core reg we sync by
     * hand in kvm_arch_get/put_registers())
     */
    switch (regidx & KVM_REG_ARM_COPROC_MASK) {
    case KVM_REG_ARM_CORE:
        return false;
    default:
        return true;
    }
}

typedef struct CPRegStateLevel {
    uint64_t regidx;
    int level;
} CPRegStateLevel;

/* All system registers not listed in the following table are assumed to be
 * of the level KVM_PUT_RUNTIME_STATE. If a register should be written less
 * often, you must add it to this table with a state of either
 * KVM_PUT_RESET_STATE or KVM_PUT_FULL_STATE.
 */
static const CPRegStateLevel non_runtime_cpregs[] = {
    { KVM_REG_ARM_TIMER_CNT, KVM_PUT_FULL_STATE },
};

int kvm_arm_cpreg_level(uint64_t regidx)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(non_runtime_cpregs); i++) {
        const CPRegStateLevel *l = &non_runtime_cpregs[i];
        if (l->regidx == regidx) {
            return l->level;
        }
    }

    return KVM_PUT_RUNTIME_STATE;
}

#define AARCH64_CORE_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define AARCH64_SIMD_CORE_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U128 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define AARCH64_SIMD_CTRL_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U32 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

int kvm_arch_put_registers(CPUState *cs, int level)
{
    struct kvm_one_reg reg;
    uint32_t fpr;
    uint64_t val;
    int i;
    int ret;
    unsigned int el;

    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    /* If we are in AArch32 mode then we need to copy the AArch32 regs to the
     * AArch64 registers before pushing them out to 64-bit KVM.
     */
    if (!is_a64(env)) {
        aarch64_sync_32_to_64(env);
    }

    for (i = 0; i < 31; i++) {
        reg.id = AARCH64_CORE_REG(regs.regs[i]);
        reg.addr = (uintptr_t) &env->xregs[i];
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
    }

    /* KVM puts SP_EL0 in regs.sp and SP_EL1 in regs.sp_el1. On the
     * QEMU side we keep the current SP in xregs[31] as well.
     */
    aarch64_save_sp(env, 1);

    reg.id = AARCH64_CORE_REG(regs.sp);
    reg.addr = (uintptr_t) &env->sp_el[0];
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    reg.id = AARCH64_CORE_REG(sp_el1);
    reg.addr = (uintptr_t) &env->sp_el[1];
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    /* Note that KVM thinks pstate is 64 bit but we use a uint32_t */
    if (is_a64(env)) {
        val = pstate_read(env);
    } else {
        val = cpsr_read(env);
    }
    reg.id = AARCH64_CORE_REG(regs.pstate);
    reg.addr = (uintptr_t) &val;
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    reg.id = AARCH64_CORE_REG(regs.pc);
    reg.addr = (uintptr_t) &env->pc;
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    reg.id = AARCH64_CORE_REG(elr_el1);
    reg.addr = (uintptr_t) &env->elr_el[1];
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    /* Saved Program State Registers
     *
     * Before we restore from the banked_spsr[] array we need to
     * ensure that any modifications to env->spsr are correctly
     * reflected in the banks.
     */
    el = arm_current_el(env);
    if (el > 0 && !is_a64(env)) {
        i = bank_number(env->uncached_cpsr & CPSR_M);
        env->banked_spsr[i] = env->spsr;
    }

    /* KVM 0-4 map to QEMU banks 1-5 */
    for (i = 0; i < KVM_NR_SPSR; i++) {
        reg.id = AARCH64_CORE_REG(spsr[i]);
        reg.addr = (uintptr_t) &env->banked_spsr[i + 1];
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
    }

    /* Advanced SIMD and FP registers
     * We map Qn = regs[2n+1]:regs[2n]
     */
    for (i = 0; i < 32; i++) {
        int rd = i << 1;
        uint64_t fp_val[2];
#ifdef HOST_WORDS_BIGENDIAN
        fp_val[0] = env->vfp.regs[rd + 1];
        fp_val[1] = env->vfp.regs[rd];
#else
        fp_val[1] = env->vfp.regs[rd + 1];
        fp_val[0] = env->vfp.regs[rd];
#endif
        reg.id = AARCH64_SIMD_CORE_REG(fp_regs.vregs[i]);
        reg.addr = (uintptr_t)(&fp_val);
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
    }

    reg.addr = (uintptr_t)(&fpr);
    fpr = vfp_get_fpsr(env);
    reg.id = AARCH64_SIMD_CTRL_REG(fp_regs.fpsr);
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    fpr = vfp_get_fpcr(env);
    reg.id = AARCH64_SIMD_CTRL_REG(fp_regs.fpcr);
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    if (!write_list_to_kvmstate(cpu, level)) {
        return EINVAL;
    }

    kvm_arm_sync_mpstate_to_kvm(cpu);

    return ret;
}

int kvm_arch_get_registers(CPUState *cs)
{
    struct kvm_one_reg reg;
    uint64_t val;
    uint32_t fpr;
    unsigned int el;
    int i;
    int ret;

    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    for (i = 0; i < 31; i++) {
        reg.id = AARCH64_CORE_REG(regs.regs[i]);
        reg.addr = (uintptr_t) &env->xregs[i];
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
    }

    reg.id = AARCH64_CORE_REG(regs.sp);
    reg.addr = (uintptr_t) &env->sp_el[0];
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    reg.id = AARCH64_CORE_REG(sp_el1);
    reg.addr = (uintptr_t) &env->sp_el[1];
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    reg.id = AARCH64_CORE_REG(regs.pstate);
    reg.addr = (uintptr_t) &val;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    env->aarch64 = ((val & PSTATE_nRW) == 0);
    if (is_a64(env)) {
        pstate_write(env, val);
    } else {
        cpsr_write(env, val, 0xffffffff, CPSRWriteRaw);
    }

    /* KVM puts SP_EL0 in regs.sp and SP_EL1 in regs.sp_el1. On the
     * QEMU side we keep the current SP in xregs[31] as well.
     */
    aarch64_restore_sp(env, 1);

    reg.id = AARCH64_CORE_REG(regs.pc);
    reg.addr = (uintptr_t) &env->pc;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    /* If we are in AArch32 mode then we need to sync the AArch32 regs with the
     * incoming AArch64 regs received from 64-bit KVM.
     * We must perform this after all of the registers have been acquired from
     * the kernel.
     */
    if (!is_a64(env)) {
        aarch64_sync_64_to_32(env);
    }

    reg.id = AARCH64_CORE_REG(elr_el1);
    reg.addr = (uintptr_t) &env->elr_el[1];
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    /* Fetch the SPSR registers
     *
     * KVM SPSRs 0-4 map to QEMU banks 1-5
     */
    for (i = 0; i < KVM_NR_SPSR; i++) {
        reg.id = AARCH64_CORE_REG(spsr[i]);
        reg.addr = (uintptr_t) &env->banked_spsr[i + 1];
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
    }

    el = arm_current_el(env);
    if (el > 0 && !is_a64(env)) {
        i = bank_number(env->uncached_cpsr & CPSR_M);
        env->spsr = env->banked_spsr[i];
    }

    /* Advanced SIMD and FP registers
     * We map Qn = regs[2n+1]:regs[2n]
     */
    for (i = 0; i < 32; i++) {
        uint64_t fp_val[2];
        reg.id = AARCH64_SIMD_CORE_REG(fp_regs.vregs[i]);
        reg.addr = (uintptr_t)(&fp_val);
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
        if (ret) {
            return ret;
        } else {
            int rd = i << 1;
#ifdef HOST_WORDS_BIGENDIAN
            env->vfp.regs[rd + 1] = fp_val[0];
            env->vfp.regs[rd] = fp_val[1];
#else
            env->vfp.regs[rd + 1] = fp_val[1];
            env->vfp.regs[rd] = fp_val[0];
#endif
        }
    }

    reg.addr = (uintptr_t)(&fpr);
    reg.id = AARCH64_SIMD_CTRL_REG(fp_regs.fpsr);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }
    vfp_set_fpsr(env, fpr);

    reg.id = AARCH64_SIMD_CTRL_REG(fp_regs.fpcr);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }
    vfp_set_fpcr(env, fpr);

    if (!write_kvmstate_to_list(cpu)) {
        return EINVAL;
    }
    /* Note that it's OK to have registers which aren't in CPUState,
     * so we can ignore a failure return here.
     */
    write_list_to_cpustate(cpu);

    kvm_arm_sync_mpstate_to_qemu(cpu);

    /* TODO: other registers */
    return ret;
}

/* C6.6.29 BRK instruction */
static const uint32_t brk_insn = 0xd4200000;

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    if (have_guest_debug) {
        if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 4, 0) ||
            cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&brk_insn, 4, 1)) {
            return -EINVAL;
        }
        return 0;
    } else {
        error_report("guest debug not supported on this kernel");
        return -EINVAL;
    }
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    static uint32_t brk;

    if (have_guest_debug) {
        if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&brk, 4, 0) ||
            brk != brk_insn ||
            cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 4, 1)) {
            return -EINVAL;
        }
        return 0;
    } else {
        error_report("guest debug not supported on this kernel");
        return -EINVAL;
    }
}

/* See v8 ARM ARM D7.2.27 ESR_ELx, Exception Syndrome Register
 *
 * To minimise translating between kernel and user-space the kernel
 * ABI just provides user-space with the full exception syndrome
 * register value to be decoded in QEMU.
 */

bool kvm_arm_handle_debug(CPUState *cs, struct kvm_debug_exit_arch *debug_exit)
{
    int hsr_ec = debug_exit->hsr >> ARM_EL_EC_SHIFT;
    ARMCPU *cpu = ARM_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUARMState *env = &cpu->env;

    /* Ensure PC is synchronised */
    kvm_cpu_synchronize_state(cs);

    switch (hsr_ec) {
    case EC_SOFTWARESTEP:
        if (cs->singlestep_enabled) {
            return true;
        } else {
            /*
             * The kernel should have suppressed the guest's ability to
             * single step at this point so something has gone wrong.
             */
            error_report("%s: guest single-step while debugging unsupported"
                         " (%"PRIx64", %"PRIx32")\n",
                         __func__, env->pc, debug_exit->hsr);
            return false;
        }
        break;
    case EC_AA64_BKPT:
        if (kvm_find_sw_breakpoint(cs, env->pc)) {
            return true;
        }
        break;
    case EC_BREAKPOINT:
        if (find_hw_breakpoint(cs, env->pc)) {
            return true;
        }
        break;
    case EC_WATCHPOINT:
    {
        CPUWatchpoint *wp = find_hw_watchpoint(cs, debug_exit->far);
        if (wp) {
            cs->watchpoint_hit = wp;
            return true;
        }
        break;
    }
    default:
        error_report("%s: unhandled debug exit (%"PRIx32", %"PRIx64")\n",
                     __func__, debug_exit->hsr, env->pc);
    }

    /* If we are not handling the debug exception it must belong to
     * the guest. Let's re-use the existing TCG interrupt code to set
     * everything up properly.
     */
    cs->exception_index = EXCP_BKPT;
    env->exception.syndrome = debug_exit->hsr;
    env->exception.vaddress = debug_exit->far;
    cc->do_interrupt(cs);

    return false;
}
