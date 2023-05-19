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

#include "qapi/error.h"
#include "cpu.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "exec/gdbstub.h"
#include "sysemu/runstate.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "kvm_arm.h"
#include "internals.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/ghes.h"
#include "hw/arm/virt.h"

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

void kvm_arm_init_debug(KVMState *s)
{
    have_guest_debug = kvm_check_extension(s,
                                           KVM_CAP_SET_GUEST_DEBUG);

    max_hw_wps = kvm_check_extension(s, KVM_CAP_GUEST_DEBUG_HW_WPS);
    hw_watchpoints = g_array_sized_new(true, true,
                                       sizeof(HWWatchpoint), max_hw_wps);

    max_hw_bps = kvm_check_extension(s, KVM_CAP_GUEST_DEBUG_HW_BPS);
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
 * DBGBCR<n>_EL1, Debug Breakpoint Control Registers
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
 *
 * DBGBVR<n>_EL1, Debug Breakpoint Value Registers
 *
 *  63  53 52       49 48       2  1 0
 * +------+-----------+----------+-----+
 * | RESS | VA[52:49] | VA[48:2] | 0 0 |
 * +------+-----------+----------+-----+
 *
 * Depending on the addressing mode bits the top bits of the register
 * are a sign extension of the highest applicable VA bit. Some
 * versions of GDB don't do it correctly so we ensure they are correct
 * here so future PC comparisons will work properly.
 */

static int insert_hw_breakpoint(target_ulong addr)
{
    HWBreakpoint brk = {
        .bcr = 0x1,                             /* BCR E=1, enable */
        .bvr = sextract64(addr, 0, 53)
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
        .wcr = R_DBGWCR_E_MASK, /* E=1, enable */
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
    wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, PAC, 3);

    switch (type) {
    case GDB_WATCHPOINT_READ:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 1);
        wp.details.flags = BP_MEM_READ;
        break;
    case GDB_WATCHPOINT_WRITE:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 2);
        wp.details.flags = BP_MEM_WRITE;
        break;
    case GDB_WATCHPOINT_ACCESS:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 3);
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
            wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, MASK, bits);
            wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, BAS, 0xff);
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

static bool kvm_arm_set_device_attr(CPUState *cs, struct kvm_device_attr *attr,
                                    const char *name)
{
    int err;

    err = kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, attr);
    if (err != 0) {
        error_report("%s: KVM_HAS_DEVICE_ATTR: %s", name, strerror(-err));
        return false;
    }

    err = kvm_vcpu_ioctl(cs, KVM_SET_DEVICE_ATTR, attr);
    if (err != 0) {
        error_report("%s: KVM_SET_DEVICE_ATTR: %s", name, strerror(-err));
        return false;
    }

    return true;
}

void kvm_arm_pmu_init(CPUState *cs)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PMU_V3_CTRL,
        .attr = KVM_ARM_VCPU_PMU_V3_INIT,
    };

    if (!ARM_CPU(cs)->has_pmu) {
        return;
    }
    if (!kvm_arm_set_device_attr(cs, &attr, "PMU")) {
        error_report("failed to init PMU");
        abort();
    }
}

void kvm_arm_pmu_set_irq(CPUState *cs, int irq)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PMU_V3_CTRL,
        .addr = (intptr_t)&irq,
        .attr = KVM_ARM_VCPU_PMU_V3_IRQ,
    };

    if (!ARM_CPU(cs)->has_pmu) {
        return;
    }
    if (!kvm_arm_set_device_attr(cs, &attr, "PMU")) {
        error_report("failed to set irq for PMU");
        abort();
    }
}

void kvm_arm_pvtime_init(CPUState *cs, uint64_t ipa)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PVTIME_CTRL,
        .attr = KVM_ARM_VCPU_PVTIME_IPA,
        .addr = (uint64_t)&ipa,
    };

    if (ARM_CPU(cs)->kvm_steal_time == ON_OFF_AUTO_OFF) {
        return;
    }
    if (!kvm_arm_set_device_attr(cs, &attr, "PVTIME IPA")) {
        error_report("failed to init PVTIME IPA");
        abort();
    }
}

static int read_sys_reg32(int fd, uint32_t *pret, uint64_t id)
{
    uint64_t ret;
    struct kvm_one_reg idreg = { .id = id, .addr = (uintptr_t)&ret };
    int err;

    assert((id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U64);
    err = ioctl(fd, KVM_GET_ONE_REG, &idreg);
    if (err < 0) {
        return -1;
    }
    *pret = ret;
    return 0;
}

static int read_sys_reg64(int fd, uint64_t *pret, uint64_t id)
{
    struct kvm_one_reg idreg = { .id = id, .addr = (uintptr_t)pret };

    assert((id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U64);
    return ioctl(fd, KVM_GET_ONE_REG, &idreg);
}

static bool kvm_arm_pauth_supported(void)
{
    return (kvm_check_extension(kvm_state, KVM_CAP_ARM_PTRAUTH_ADDRESS) &&
            kvm_check_extension(kvm_state, KVM_CAP_ARM_PTRAUTH_GENERIC));
}

bool kvm_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf)
{
    /* Identify the feature bits corresponding to the host CPU, and
     * fill out the ARMHostCPUClass fields accordingly. To do this
     * we have to create a scratch VM, create a single CPU inside it,
     * and then query that CPU for the relevant ID registers.
     */
    int fdarray[3];
    bool sve_supported;
    bool pmu_supported = false;
    uint64_t features = 0;
    int err;

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
    /*
     * target = -1 informs kvm_arm_create_scratch_host_vcpu()
     * to use the preferred target
     */
    struct kvm_vcpu_init init = { .target = -1, };

    /*
     * Ask for SVE if supported, so that we can query ID_AA64ZFR0,
     * which is otherwise RAZ.
     */
    sve_supported = kvm_arm_sve_supported();
    if (sve_supported) {
        init.features[0] |= 1 << KVM_ARM_VCPU_SVE;
    }

    /*
     * Ask for Pointer Authentication if supported, so that we get
     * the unsanitized field values for AA64ISAR1_EL1.
     */
    if (kvm_arm_pauth_supported()) {
        init.features[0] |= (1 << KVM_ARM_VCPU_PTRAUTH_ADDRESS |
                             1 << KVM_ARM_VCPU_PTRAUTH_GENERIC);
    }

    if (kvm_arm_pmu_supported()) {
        init.features[0] |= 1 << KVM_ARM_VCPU_PMU_V3;
        pmu_supported = true;
    }

    if (!kvm_arm_create_scratch_host_vcpu(cpus_to_try, fdarray, &init)) {
        return false;
    }

    ahcf->target = init.target;
    ahcf->dtb_compatible = "arm,arm-v8";

    err = read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64pfr0,
                         ARM64_SYS_REG(3, 0, 0, 4, 0));
    if (unlikely(err < 0)) {
        /*
         * Before v4.15, the kernel only exposed a limited number of system
         * registers, not including any of the interesting AArch64 ID regs.
         * For the most part we could leave these fields as zero with minimal
         * effect, since this does not affect the values seen by the guest.
         *
         * However, it could cause problems down the line for QEMU,
         * so provide a minimal v8.0 default.
         *
         * ??? Could read MIDR and use knowledge from cpu64.c.
         * ??? Could map a page of memory into our temp guest and
         *     run the tiniest of hand-crafted kernels to extract
         *     the values seen by the guest.
         * ??? Either of these sounds like too much effort just
         *     to work around running a modern host kernel.
         */
        ahcf->isar.id_aa64pfr0 = 0x00000011; /* EL1&0, AArch64 only */
        err = 0;
    } else {
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64pfr1,
                              ARM64_SYS_REG(3, 0, 0, 4, 1));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64smfr0,
                              ARM64_SYS_REG(3, 0, 0, 4, 5));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64dfr0,
                              ARM64_SYS_REG(3, 0, 0, 5, 0));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64dfr1,
                              ARM64_SYS_REG(3, 0, 0, 5, 1));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64isar0,
                              ARM64_SYS_REG(3, 0, 0, 6, 0));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64isar1,
                              ARM64_SYS_REG(3, 0, 0, 6, 1));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64mmfr0,
                              ARM64_SYS_REG(3, 0, 0, 7, 0));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64mmfr1,
                              ARM64_SYS_REG(3, 0, 0, 7, 1));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64mmfr2,
                              ARM64_SYS_REG(3, 0, 0, 7, 2));

        /*
         * Note that if AArch32 support is not present in the host,
         * the AArch32 sysregs are present to be read, but will
         * return UNKNOWN values.  This is neither better nor worse
         * than skipping the reads and leaving 0, as we must avoid
         * considering the values in every case.
         */
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_pfr0,
                              ARM64_SYS_REG(3, 0, 0, 1, 0));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_pfr1,
                              ARM64_SYS_REG(3, 0, 0, 1, 1));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_dfr0,
                              ARM64_SYS_REG(3, 0, 0, 1, 2));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr0,
                              ARM64_SYS_REG(3, 0, 0, 1, 4));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr1,
                              ARM64_SYS_REG(3, 0, 0, 1, 5));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr2,
                              ARM64_SYS_REG(3, 0, 0, 1, 6));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr3,
                              ARM64_SYS_REG(3, 0, 0, 1, 7));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar0,
                              ARM64_SYS_REG(3, 0, 0, 2, 0));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar1,
                              ARM64_SYS_REG(3, 0, 0, 2, 1));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar2,
                              ARM64_SYS_REG(3, 0, 0, 2, 2));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar3,
                              ARM64_SYS_REG(3, 0, 0, 2, 3));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar4,
                              ARM64_SYS_REG(3, 0, 0, 2, 4));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar5,
                              ARM64_SYS_REG(3, 0, 0, 2, 5));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr4,
                              ARM64_SYS_REG(3, 0, 0, 2, 6));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar6,
                              ARM64_SYS_REG(3, 0, 0, 2, 7));

        err |= read_sys_reg32(fdarray[2], &ahcf->isar.mvfr0,
                              ARM64_SYS_REG(3, 0, 0, 3, 0));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.mvfr1,
                              ARM64_SYS_REG(3, 0, 0, 3, 1));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.mvfr2,
                              ARM64_SYS_REG(3, 0, 0, 3, 2));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_pfr2,
                              ARM64_SYS_REG(3, 0, 0, 3, 4));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_dfr1,
                              ARM64_SYS_REG(3, 0, 0, 3, 5));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr5,
                              ARM64_SYS_REG(3, 0, 0, 3, 6));

        /*
         * DBGDIDR is a bit complicated because the kernel doesn't
         * provide an accessor for it in 64-bit mode, which is what this
         * scratch VM is in, and there's no architected "64-bit sysreg
         * which reads the same as the 32-bit register" the way there is
         * for other ID registers. Instead we synthesize a value from the
         * AArch64 ID_AA64DFR0, the same way the kernel code in
         * arch/arm64/kvm/sys_regs.c:trap_dbgidr() does.
         * We only do this if the CPU supports AArch32 at EL1.
         */
        if (FIELD_EX32(ahcf->isar.id_aa64pfr0, ID_AA64PFR0, EL1) >= 2) {
            int wrps = FIELD_EX64(ahcf->isar.id_aa64dfr0, ID_AA64DFR0, WRPS);
            int brps = FIELD_EX64(ahcf->isar.id_aa64dfr0, ID_AA64DFR0, BRPS);
            int ctx_cmps =
                FIELD_EX64(ahcf->isar.id_aa64dfr0, ID_AA64DFR0, CTX_CMPS);
            int version = 6; /* ARMv8 debug architecture */
            bool has_el3 =
                !!FIELD_EX32(ahcf->isar.id_aa64pfr0, ID_AA64PFR0, EL3);
            uint32_t dbgdidr = 0;

            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, WRPS, wrps);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, BRPS, brps);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, CTX_CMPS, ctx_cmps);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, VERSION, version);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, NSUHD_IMP, has_el3);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, SE_IMP, has_el3);
            dbgdidr |= (1 << 15); /* RES1 bit */
            ahcf->isar.dbgdidr = dbgdidr;
        }

        if (pmu_supported) {
            /* PMCR_EL0 is only accessible if the vCPU has feature PMU_V3 */
            err |= read_sys_reg64(fdarray[2], &ahcf->isar.reset_pmcr_el0,
                                  ARM64_SYS_REG(3, 3, 9, 12, 0));
        }

        if (sve_supported) {
            /*
             * There is a range of kernels between kernel commit 73433762fcae
             * and f81cb2c3ad41 which have a bug where the kernel doesn't
             * expose SYS_ID_AA64ZFR0_EL1 via the ONE_REG API unless the VM has
             * enabled SVE support, which resulted in an error rather than RAZ.
             * So only read the register if we set KVM_ARM_VCPU_SVE above.
             */
            err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64zfr0,
                                  ARM64_SYS_REG(3, 0, 0, 4, 4));
        }
    }

    kvm_arm_destroy_scratch_host_vcpu(fdarray);

    if (err < 0) {
        return false;
    }

    /*
     * We can assume any KVM supporting CPU is at least a v8
     * with VFPv4+Neon; this in turn implies most of the other
     * feature bits.
     */
    features |= 1ULL << ARM_FEATURE_V8;
    features |= 1ULL << ARM_FEATURE_NEON;
    features |= 1ULL << ARM_FEATURE_AARCH64;
    features |= 1ULL << ARM_FEATURE_PMU;
    features |= 1ULL << ARM_FEATURE_GENERIC_TIMER;

    ahcf->features = features;

    return true;
}

void kvm_arm_steal_time_finalize(ARMCPU *cpu, Error **errp)
{
    bool has_steal_time = kvm_arm_steal_time_supported();

    if (cpu->kvm_steal_time == ON_OFF_AUTO_AUTO) {
        if (!has_steal_time || !arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
            cpu->kvm_steal_time = ON_OFF_AUTO_OFF;
        } else {
            cpu->kvm_steal_time = ON_OFF_AUTO_ON;
        }
    } else if (cpu->kvm_steal_time == ON_OFF_AUTO_ON) {
        if (!has_steal_time) {
            error_setg(errp, "'kvm-steal-time' cannot be enabled "
                             "on this host");
            return;
        } else if (!arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
            /*
             * DEN0057A chapter 2 says "This specification only covers
             * systems in which the Execution state of the hypervisor
             * as well as EL1 of virtual machines is AArch64.". And,
             * to ensure that, the smc/hvc calls are only specified as
             * smc64/hvc64.
             */
            error_setg(errp, "'kvm-steal-time' cannot be enabled "
                             "for AArch32 guests");
            return;
        }
    }
}

bool kvm_arm_aarch32_supported(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_EL1_32BIT);
}

bool kvm_arm_sve_supported(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_SVE);
}

bool kvm_arm_steal_time_supported(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_STEAL_TIME);
}

QEMU_BUILD_BUG_ON(KVM_ARM64_SVE_VQ_MIN != 1);

uint32_t kvm_arm_sve_get_vls(CPUState *cs)
{
    /* Only call this function if kvm_arm_sve_supported() returns true. */
    static uint64_t vls[KVM_ARM64_SVE_VLS_WORDS];
    static bool probed;
    uint32_t vq = 0;
    int i;

    /*
     * KVM ensures all host CPUs support the same set of vector lengths.
     * So we only need to create the scratch VCPUs once and then cache
     * the results.
     */
    if (!probed) {
        struct kvm_vcpu_init init = {
            .target = -1,
            .features[0] = (1 << KVM_ARM_VCPU_SVE),
        };
        struct kvm_one_reg reg = {
            .id = KVM_REG_ARM64_SVE_VLS,
            .addr = (uint64_t)&vls[0],
        };
        int fdarray[3], ret;

        probed = true;

        if (!kvm_arm_create_scratch_host_vcpu(NULL, fdarray, &init)) {
            error_report("failed to create scratch VCPU with SVE enabled");
            abort();
        }
        ret = ioctl(fdarray[2], KVM_GET_ONE_REG, &reg);
        kvm_arm_destroy_scratch_host_vcpu(fdarray);
        if (ret) {
            error_report("failed to get KVM_REG_ARM64_SVE_VLS: %s",
                         strerror(errno));
            abort();
        }

        for (i = KVM_ARM64_SVE_VLS_WORDS - 1; i >= 0; --i) {
            if (vls[i]) {
                vq = 64 - clz64(vls[i]) + i * 64;
                break;
            }
        }
        if (vq > ARM_MAX_VQ) {
            warn_report("KVM supports vector lengths larger than "
                        "QEMU can enable");
            vls[0] &= MAKE_64BIT_MASK(0, ARM_MAX_VQ);
        }
    }

    return vls[0];
}

static int kvm_arm_sve_set_vls(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    uint64_t vls[KVM_ARM64_SVE_VLS_WORDS] = { cpu->sve_vq.map };
    struct kvm_one_reg reg = {
        .id = KVM_REG_ARM64_SVE_VLS,
        .addr = (uint64_t)&vls[0],
    };

    assert(cpu->sve_max_vq <= KVM_ARM64_SVE_VQ_MAX);

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
}

#define ARM_CPU_ID_MPIDR       3, 0, 0, 0, 5

int kvm_arch_init_vcpu(CPUState *cs)
{
    int ret;
    uint64_t mpidr;
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t psciver;

    if (cpu->kvm_target == QEMU_KVM_ARM_TARGET_NONE ||
        !object_dynamic_cast(OBJECT(cpu), TYPE_AARCH64_CPU)) {
        error_report("KVM is not supported for this guest CPU type");
        return -EINVAL;
    }

    qemu_add_vm_change_state_handler(kvm_arm_vm_state_change, cs);

    /* Determine init features for this CPU */
    memset(cpu->kvm_init_features, 0, sizeof(cpu->kvm_init_features));
    if (cs->start_powered_off) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_POWER_OFF;
    }
    if (kvm_check_extension(cs->kvm_state, KVM_CAP_ARM_PSCI_0_2)) {
        cpu->psci_version = QEMU_PSCI_VERSION_0_2;
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PSCI_0_2;
    }
    if (!arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_EL1_32BIT;
    }
    if (!kvm_check_extension(cs->kvm_state, KVM_CAP_ARM_PMU_V3)) {
        cpu->has_pmu = false;
    }
    if (cpu->has_pmu) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PMU_V3;
    } else {
        env->features &= ~(1ULL << ARM_FEATURE_PMU);
    }
    if (cpu_isar_feature(aa64_sve, cpu)) {
        assert(kvm_arm_sve_supported());
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_SVE;
    }
    if (cpu_isar_feature(aa64_pauth, cpu)) {
        cpu->kvm_init_features[0] |= (1 << KVM_ARM_VCPU_PTRAUTH_ADDRESS |
                                      1 << KVM_ARM_VCPU_PTRAUTH_GENERIC);
    }

    /* Do KVM_ARM_VCPU_INIT ioctl */
    ret = kvm_arm_vcpu_init(cs);
    if (ret) {
        return ret;
    }

    if (cpu_isar_feature(aa64_sve, cpu)) {
        ret = kvm_arm_sve_set_vls(cs);
        if (ret) {
            return ret;
        }
        ret = kvm_arm_vcpu_finalize(cs, KVM_ARM_VCPU_SVE);
        if (ret) {
            return ret;
        }
    }

    /*
     * KVM reports the exact PSCI version it is implementing via a
     * special sysreg. If it is present, use its contents to determine
     * what to report to the guest in the dtb (it is the PSCI version,
     * in the same 15-bits major 16-bits minor format that PSCI_VERSION
     * returns).
     */
    if (!kvm_get_one_reg(cs, KVM_REG_ARM_PSCI_VERSION, &psciver)) {
        cpu->psci_version = psciver;
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

    /* Check whether user space can specify guest syndrome value */
    kvm_arm_init_serror_injection(cs);

    return kvm_arm_init_cpreg_list(cpu);
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

bool kvm_arm_reg_syncs_via_cpreg_list(uint64_t regidx)
{
    /* Return true if the regidx is a register we should synchronize
     * via the cpreg_tuples array (ie is not a core or sve reg that
     * we sync by hand in kvm_arch_get/put_registers())
     */
    switch (regidx & KVM_REG_ARM_COPROC_MASK) {
    case KVM_REG_ARM_CORE:
    case KVM_REG_ARM64_SVE:
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

/* Callers must hold the iothread mutex lock */
static void kvm_inject_arm_sea(CPUState *c)
{
    ARMCPU *cpu = ARM_CPU(c);
    CPUARMState *env = &cpu->env;
    uint32_t esr;
    bool same_el;

    c->exception_index = EXCP_DATA_ABORT;
    env->exception.target_el = 1;

    /*
     * Set the DFSC to synchronous external abort and set FnV to not valid,
     * this will tell guest the FAR_ELx is UNKNOWN for this abort.
     */
    same_el = arm_current_el(env) == env->exception.target_el;
    esr = syn_data_abort_no_iss(same_el, 1, 0, 0, 0, 0, 0x10);

    env->exception.syndrome = esr;

    arm_cpu_do_interrupt(c);
}

#define AARCH64_CORE_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define AARCH64_SIMD_CORE_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U128 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define AARCH64_SIMD_CTRL_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U32 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

static int kvm_arch_put_fpsimd(CPUState *cs)
{
    CPUARMState *env = &ARM_CPU(cs)->env;
    struct kvm_one_reg reg;
    int i, ret;

    for (i = 0; i < 32; i++) {
        uint64_t *q = aa64_vfp_qreg(env, i);
#if HOST_BIG_ENDIAN
        uint64_t fp_val[2] = { q[1], q[0] };
        reg.addr = (uintptr_t)fp_val;
#else
        reg.addr = (uintptr_t)q;
#endif
        reg.id = AARCH64_SIMD_CORE_REG(fp_regs.vregs[i]);
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

/*
 * KVM SVE registers come in slices where ZREGs have a slice size of 2048 bits
 * and PREGS and the FFR have a slice size of 256 bits. However we simply hard
 * code the slice index to zero for now as it's unlikely we'll need more than
 * one slice for quite some time.
 */
static int kvm_arch_put_sve(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t tmp[ARM_MAX_VQ * 2];
    uint64_t *r;
    struct kvm_one_reg reg;
    int n, ret;

    for (n = 0; n < KVM_ARM64_SVE_NUM_ZREGS; ++n) {
        r = sve_bswap64(tmp, &env->vfp.zregs[n].d[0], cpu->sve_max_vq * 2);
        reg.addr = (uintptr_t)r;
        reg.id = KVM_REG_ARM64_SVE_ZREG(n, 0);
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
    }

    for (n = 0; n < KVM_ARM64_SVE_NUM_PREGS; ++n) {
        r = sve_bswap64(tmp, r = &env->vfp.pregs[n].p[0],
                        DIV_ROUND_UP(cpu->sve_max_vq * 2, 8));
        reg.addr = (uintptr_t)r;
        reg.id = KVM_REG_ARM64_SVE_PREG(n, 0);
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
    }

    r = sve_bswap64(tmp, &env->vfp.pregs[FFR_PRED_NUM].p[0],
                    DIV_ROUND_UP(cpu->sve_max_vq * 2, 8));
    reg.addr = (uintptr_t)r;
    reg.id = KVM_REG_ARM64_SVE_FFR(0);
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    return 0;
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    struct kvm_one_reg reg;
    uint64_t val;
    uint32_t fpr;
    int i, ret;
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

    if (cpu_isar_feature(aa64_sve, cpu)) {
        ret = kvm_arch_put_sve(cs);
    } else {
        ret = kvm_arch_put_fpsimd(cs);
    }
    if (ret) {
        return ret;
    }

    reg.addr = (uintptr_t)(&fpr);
    fpr = vfp_get_fpsr(env);
    reg.id = AARCH64_SIMD_CTRL_REG(fp_regs.fpsr);
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    reg.addr = (uintptr_t)(&fpr);
    fpr = vfp_get_fpcr(env);
    reg.id = AARCH64_SIMD_CTRL_REG(fp_regs.fpcr);
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }

    write_cpustate_to_list(cpu, true);

    if (!write_list_to_kvmstate(cpu, level)) {
        return -EINVAL;
    }

   /*
    * Setting VCPU events should be triggered after syncing the registers
    * to avoid overwriting potential changes made by KVM upon calling
    * KVM_SET_VCPU_EVENTS ioctl
    */
    ret = kvm_put_vcpu_events(cpu);
    if (ret) {
        return ret;
    }

    kvm_arm_sync_mpstate_to_kvm(cpu);

    return ret;
}

static int kvm_arch_get_fpsimd(CPUState *cs)
{
    CPUARMState *env = &ARM_CPU(cs)->env;
    struct kvm_one_reg reg;
    int i, ret;

    for (i = 0; i < 32; i++) {
        uint64_t *q = aa64_vfp_qreg(env, i);
        reg.id = AARCH64_SIMD_CORE_REG(fp_regs.vregs[i]);
        reg.addr = (uintptr_t)q;
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
        if (ret) {
            return ret;
        } else {
#if HOST_BIG_ENDIAN
            uint64_t t;
            t = q[0], q[0] = q[1], q[1] = t;
#endif
        }
    }

    return 0;
}

/*
 * KVM SVE registers come in slices where ZREGs have a slice size of 2048 bits
 * and PREGS and the FFR have a slice size of 256 bits. However we simply hard
 * code the slice index to zero for now as it's unlikely we'll need more than
 * one slice for quite some time.
 */
static int kvm_arch_get_sve(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    struct kvm_one_reg reg;
    uint64_t *r;
    int n, ret;

    for (n = 0; n < KVM_ARM64_SVE_NUM_ZREGS; ++n) {
        r = &env->vfp.zregs[n].d[0];
        reg.addr = (uintptr_t)r;
        reg.id = KVM_REG_ARM64_SVE_ZREG(n, 0);
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
        sve_bswap64(r, r, cpu->sve_max_vq * 2);
    }

    for (n = 0; n < KVM_ARM64_SVE_NUM_PREGS; ++n) {
        r = &env->vfp.pregs[n].p[0];
        reg.addr = (uintptr_t)r;
        reg.id = KVM_REG_ARM64_SVE_PREG(n, 0);
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
        if (ret) {
            return ret;
        }
        sve_bswap64(r, r, DIV_ROUND_UP(cpu->sve_max_vq * 2, 8));
    }

    r = &env->vfp.pregs[FFR_PRED_NUM].p[0];
    reg.addr = (uintptr_t)r;
    reg.id = KVM_REG_ARM64_SVE_FFR(0);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }
    sve_bswap64(r, r, DIV_ROUND_UP(cpu->sve_max_vq * 2, 8));

    return 0;
}

int kvm_arch_get_registers(CPUState *cs)
{
    struct kvm_one_reg reg;
    uint64_t val;
    unsigned int el;
    uint32_t fpr;
    int i, ret;

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

    if (cpu_isar_feature(aa64_sve, cpu)) {
        ret = kvm_arch_get_sve(cs);
    } else {
        ret = kvm_arch_get_fpsimd(cs);
    }
    if (ret) {
        return ret;
    }

    reg.addr = (uintptr_t)(&fpr);
    reg.id = AARCH64_SIMD_CTRL_REG(fp_regs.fpsr);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }
    vfp_set_fpsr(env, fpr);

    reg.addr = (uintptr_t)(&fpr);
    reg.id = AARCH64_SIMD_CTRL_REG(fp_regs.fpcr);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret) {
        return ret;
    }
    vfp_set_fpcr(env, fpr);

    ret = kvm_get_vcpu_events(cpu);
    if (ret) {
        return ret;
    }

    if (!write_kvmstate_to_list(cpu)) {
        return -EINVAL;
    }
    /* Note that it's OK to have registers which aren't in CPUState,
     * so we can ignore a failure return here.
     */
    write_list_to_cpustate(cpu);

    kvm_arm_sync_mpstate_to_qemu(cpu);

    /* TODO: other registers */
    return ret;
}

void kvm_arch_on_sigbus_vcpu(CPUState *c, int code, void *addr)
{
    ram_addr_t ram_addr;
    hwaddr paddr;

    assert(code == BUS_MCEERR_AR || code == BUS_MCEERR_AO);

    if (acpi_ghes_present() && addr) {
        ram_addr = qemu_ram_addr_from_host(addr);
        if (ram_addr != RAM_ADDR_INVALID &&
            kvm_physical_memory_addr_from_host(c->kvm_state, addr, &paddr)) {
            kvm_hwpoison_page_add(ram_addr);
            /*
             * If this is a BUS_MCEERR_AR, we know we have been called
             * synchronously from the vCPU thread, so we can easily
             * synchronize the state and inject an error.
             *
             * TODO: we currently don't tell the guest at all about
             * BUS_MCEERR_AO. In that case we might either be being
             * called synchronously from the vCPU thread, or a bit
             * later from the main thread, so doing the injection of
             * the error would be more complicated.
             */
            if (code == BUS_MCEERR_AR) {
                kvm_cpu_synchronize_state(c);
                if (!acpi_ghes_record_errors(ACPI_HEST_SRC_ID_SEA, paddr)) {
                    kvm_inject_arm_sea(c);
                } else {
                    error_report("failed to record the error");
                    abort();
                }
            }
            return;
        }
        if (code == BUS_MCEERR_AO) {
            error_report("Hardware memory error at addr %p for memory used by "
                "QEMU itself instead of guest system!", addr);
        }
    }

    if (code == BUS_MCEERR_AR) {
        error_report("Hardware memory error!");
        exit(1);
    }
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
    int hsr_ec = syn_get_ec(debug_exit->hsr);
    ARMCPU *cpu = ARM_CPU(cs);
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
                         " (%"PRIx64", %"PRIx32")",
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
        error_report("%s: unhandled debug exit (%"PRIx32", %"PRIx64")",
                     __func__, debug_exit->hsr, env->pc);
    }

    /* If we are not handling the debug exception it must belong to
     * the guest. Let's re-use the existing TCG interrupt code to set
     * everything up properly.
     */
    cs->exception_index = EXCP_BKPT;
    env->exception.syndrome = debug_exit->hsr;
    env->exception.vaddress = debug_exit->far;
    env->exception.target_el = 1;
    qemu_mutex_lock_iothread();
    arm_cpu_do_interrupt(cs);
    qemu_mutex_unlock_iothread();

    return false;
}

#define ARM64_REG_ESR_EL1 ARM64_SYS_REG(3, 0, 5, 2, 0)
#define ARM64_REG_TCR_EL1 ARM64_SYS_REG(3, 0, 2, 0, 2)

/*
 * ESR_EL1
 * ISS encoding
 * AARCH64: DFSC,   bits [5:0]
 * AARCH32:
 *      TTBCR.EAE == 0
 *          FS[4]   - DFSR[10]
 *          FS[3:0] - DFSR[3:0]
 *      TTBCR.EAE == 1
 *          FS, bits [5:0]
 */
#define ESR_DFSC(aarch64, lpae, v)        \
    ((aarch64 || (lpae)) ? ((v) & 0x3F)   \
               : (((v) >> 6) | ((v) & 0x1F)))

#define ESR_DFSC_EXTABT(aarch64, lpae) \
    ((aarch64) ? 0x10 : (lpae) ? 0x10 : 0x8)

bool kvm_arm_verify_ext_dabt_pending(CPUState *cs)
{
    uint64_t dfsr_val;

    if (!kvm_get_one_reg(cs, ARM64_REG_ESR_EL1, &dfsr_val)) {
        ARMCPU *cpu = ARM_CPU(cs);
        CPUARMState *env = &cpu->env;
        int aarch64_mode = arm_feature(env, ARM_FEATURE_AARCH64);
        int lpae = 0;

        if (!aarch64_mode) {
            uint64_t ttbcr;

            if (!kvm_get_one_reg(cs, ARM64_REG_TCR_EL1, &ttbcr)) {
                lpae = arm_feature(env, ARM_FEATURE_LPAE)
                        && (ttbcr & TTBCR_EAE);
            }
        }
        /*
         * The verification here is based on the DFSC bits
         * of the ESR_EL1 reg only
         */
         return (ESR_DFSC(aarch64_mode, lpae, dfsr_val) ==
                ESR_DFSC_EXTABT(aarch64_mode, lpae));
    }
    return false;
}
