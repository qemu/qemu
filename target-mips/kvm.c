/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: MIPS specific KVM APIs
 *
 * Copyright (C) 2012-2014 Imagination Technologies Ltd.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
*/

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "cpu.h"
#include "sysemu/cpus.h"
#include "kvm_mips.h"

#define DEBUG_KVM 0

#define DPRINTF(fmt, ...) \
    do { if (DEBUG_KVM) { fprintf(stderr, fmt, ## __VA_ARGS__); } } while (0)

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static void kvm_mips_update_state(void *opaque, int running, RunState state);

unsigned long kvm_arch_vcpu_id(CPUState *cs)
{
    return cs->cpu_index;
}

int kvm_arch_init(KVMState *s)
{
    /* MIPS has 128 signals */
    kvm_set_sigmask_len(s, 16);

    DPRINTF("%s\n", __func__);
    return 0;
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    int ret = 0;

    qemu_add_vm_change_state_handler(kvm_mips_update_state, cs);

    DPRINTF("%s\n", __func__);
    return ret;
}

void kvm_mips_reset_vcpu(MIPSCPU *cpu)
{
    DPRINTF("%s\n", __func__);
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    DPRINTF("%s\n", __func__);
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    DPRINTF("%s\n", __func__);
    return 0;
}

static inline int cpu_mips_io_interrupts_pending(MIPSCPU *cpu)
{
    CPUMIPSState *env = &cpu->env;

    DPRINTF("%s: %#x\n", __func__, env->CP0_Cause & (1 << (2 + CP0Ca_IP)));
    return env->CP0_Cause & (0x1 << (2 + CP0Ca_IP));
}


void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    int r;
    struct kvm_mips_interrupt intr;

    if ((cs->interrupt_request & CPU_INTERRUPT_HARD) &&
            cpu_mips_io_interrupts_pending(cpu)) {
        intr.cpu = -1;
        intr.irq = 2;
        r = kvm_vcpu_ioctl(cs, KVM_INTERRUPT, &intr);
        if (r < 0) {
            error_report("%s: cpu %d: failed to inject IRQ %x",
                         __func__, cs->cpu_index, intr.irq);
        }
    }
}

void kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
    DPRINTF("%s\n", __func__);
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return cs->halted;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    int ret;

    DPRINTF("%s\n", __func__);
    switch (run->exit_reason) {
    default:
        error_report("%s: unknown exit reason %d",
                     __func__, run->exit_reason);
        ret = -1;
        break;
    }

    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    DPRINTF("%s\n", __func__);
    return true;
}

int kvm_arch_on_sigbus_vcpu(CPUState *cs, int code, void *addr)
{
    DPRINTF("%s\n", __func__);
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    DPRINTF("%s\n", __func__);
    return 1;
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}

int kvm_mips_set_interrupt(MIPSCPU *cpu, int irq, int level)
{
    CPUState *cs = CPU(cpu);
    struct kvm_mips_interrupt intr;

    if (!kvm_enabled()) {
        return 0;
    }

    intr.cpu = -1;

    if (level) {
        intr.irq = irq;
    } else {
        intr.irq = -irq;
    }

    kvm_vcpu_ioctl(cs, KVM_INTERRUPT, &intr);

    return 0;
}

int kvm_mips_set_ipi_interrupt(MIPSCPU *cpu, int irq, int level)
{
    CPUState *cs = current_cpu;
    CPUState *dest_cs = CPU(cpu);
    struct kvm_mips_interrupt intr;

    if (!kvm_enabled()) {
        return 0;
    }

    intr.cpu = dest_cs->cpu_index;

    if (level) {
        intr.irq = irq;
    } else {
        intr.irq = -irq;
    }

    DPRINTF("%s: CPU %d, IRQ: %d\n", __func__, intr.cpu, intr.irq);

    kvm_vcpu_ioctl(cs, KVM_INTERRUPT, &intr);

    return 0;
}

#define MIPS_CP0_32(_R, _S)                                     \
    (KVM_REG_MIPS | KVM_REG_SIZE_U32 | 0x10000 | (8 * (_R) + (_S)))

#define MIPS_CP0_64(_R, _S)                                     \
    (KVM_REG_MIPS | KVM_REG_SIZE_U64 | 0x10000 | (8 * (_R) + (_S)))

#define KVM_REG_MIPS_CP0_INDEX          MIPS_CP0_32(0, 0)
#define KVM_REG_MIPS_CP0_CONTEXT        MIPS_CP0_64(4, 0)
#define KVM_REG_MIPS_CP0_USERLOCAL      MIPS_CP0_64(4, 2)
#define KVM_REG_MIPS_CP0_PAGEMASK       MIPS_CP0_32(5, 0)
#define KVM_REG_MIPS_CP0_WIRED          MIPS_CP0_32(6, 0)
#define KVM_REG_MIPS_CP0_HWRENA         MIPS_CP0_32(7, 0)
#define KVM_REG_MIPS_CP0_BADVADDR       MIPS_CP0_64(8, 0)
#define KVM_REG_MIPS_CP0_COUNT          MIPS_CP0_32(9, 0)
#define KVM_REG_MIPS_CP0_ENTRYHI        MIPS_CP0_64(10, 0)
#define KVM_REG_MIPS_CP0_COMPARE        MIPS_CP0_32(11, 0)
#define KVM_REG_MIPS_CP0_STATUS         MIPS_CP0_32(12, 0)
#define KVM_REG_MIPS_CP0_CAUSE          MIPS_CP0_32(13, 0)
#define KVM_REG_MIPS_CP0_EPC            MIPS_CP0_64(14, 0)
#define KVM_REG_MIPS_CP0_ERROREPC       MIPS_CP0_64(30, 0)

/* CP0_Count control */
#define KVM_REG_MIPS_COUNT_CTL          (KVM_REG_MIPS | KVM_REG_SIZE_U64 | \
                                         0x20000 | 0)
#define KVM_REG_MIPS_COUNT_CTL_DC       0x00000001      /* master disable */
/* CP0_Count resume monotonic nanoseconds */
#define KVM_REG_MIPS_COUNT_RESUME       (KVM_REG_MIPS | KVM_REG_SIZE_U64 | \
                                         0x20000 | 1)
/* CP0_Count rate in Hz */
#define KVM_REG_MIPS_COUNT_HZ           (KVM_REG_MIPS | KVM_REG_SIZE_U64 | \
                                         0x20000 | 2)

static inline int kvm_mips_put_one_reg(CPUState *cs, uint64_t reg_id,
                                       int32_t *addr)
{
    uint64_t val64 = *addr;
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)&val64
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &cp0reg);
}

static inline int kvm_mips_put_one_ulreg(CPUState *cs, uint64_t reg_id,
                                         target_ulong *addr)
{
    uint64_t val64 = *addr;
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)&val64
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &cp0reg);
}

static inline int kvm_mips_put_one_reg64(CPUState *cs, uint64_t reg_id,
                                         uint64_t *addr)
{
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &cp0reg);
}

static inline int kvm_mips_get_one_reg(CPUState *cs, uint64_t reg_id,
                                       int32_t *addr)
{
    int ret;
    uint64_t val64 = 0;
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)&val64
    };

    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &cp0reg);
    if (ret >= 0) {
        *addr = val64;
    }
    return ret;
}

static inline int kvm_mips_get_one_ulreg(CPUState *cs, uint64 reg_id,
                                         target_ulong *addr)
{
    int ret;
    uint64_t val64 = 0;
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)&val64
    };

    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &cp0reg);
    if (ret >= 0) {
        *addr = val64;
    }
    return ret;
}

static inline int kvm_mips_get_one_reg64(CPUState *cs, uint64 reg_id,
                                         uint64_t *addr)
{
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &cp0reg);
}

/*
 * We freeze the KVM timer when either the VM clock is stopped or the state is
 * saved (the state is dirty).
 */

/*
 * Save the state of the KVM timer when VM clock is stopped or state is synced
 * to QEMU.
 */
static int kvm_mips_save_count(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    uint64_t count_ctl;
    int err, ret = 0;

    /* freeze KVM timer */
    err = kvm_mips_get_one_reg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
    if (err < 0) {
        DPRINTF("%s: Failed to get COUNT_CTL (%d)\n", __func__, err);
        ret = err;
    } else if (!(count_ctl & KVM_REG_MIPS_COUNT_CTL_DC)) {
        count_ctl |= KVM_REG_MIPS_COUNT_CTL_DC;
        err = kvm_mips_put_one_reg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
        if (err < 0) {
            DPRINTF("%s: Failed to set COUNT_CTL.DC=1 (%d)\n", __func__, err);
            ret = err;
        }
    }

    /* read CP0_Cause */
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_CAUSE, &env->CP0_Cause);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CAUSE (%d)\n", __func__, err);
        ret = err;
    }

    /* read CP0_Count */
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_COUNT, &env->CP0_Count);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_COUNT (%d)\n", __func__, err);
        ret = err;
    }

    return ret;
}

/*
 * Restore the state of the KVM timer when VM clock is restarted or state is
 * synced to KVM.
 */
static int kvm_mips_restore_count(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    uint64_t count_ctl;
    int err_dc, err, ret = 0;

    /* check the timer is frozen */
    err_dc = kvm_mips_get_one_reg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
    if (err_dc < 0) {
        DPRINTF("%s: Failed to get COUNT_CTL (%d)\n", __func__, err_dc);
        ret = err_dc;
    } else if (!(count_ctl & KVM_REG_MIPS_COUNT_CTL_DC)) {
        /* freeze timer (sets COUNT_RESUME for us) */
        count_ctl |= KVM_REG_MIPS_COUNT_CTL_DC;
        err = kvm_mips_put_one_reg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
        if (err < 0) {
            DPRINTF("%s: Failed to set COUNT_CTL.DC=1 (%d)\n", __func__, err);
            ret = err;
        }
    }

    /* load CP0_Cause */
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_CAUSE, &env->CP0_Cause);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_CAUSE (%d)\n", __func__, err);
        ret = err;
    }

    /* load CP0_Count */
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_COUNT, &env->CP0_Count);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_COUNT (%d)\n", __func__, err);
        ret = err;
    }

    /* resume KVM timer */
    if (err_dc >= 0) {
        count_ctl &= ~KVM_REG_MIPS_COUNT_CTL_DC;
        err = kvm_mips_put_one_reg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
        if (err < 0) {
            DPRINTF("%s: Failed to set COUNT_CTL.DC=0 (%d)\n", __func__, err);
            ret = err;
        }
    }

    return ret;
}

/*
 * Handle the VM clock being started or stopped
 */
static void kvm_mips_update_state(void *opaque, int running, RunState state)
{
    CPUState *cs = opaque;
    int ret;
    uint64_t count_resume;

    /*
     * If state is already dirty (synced to QEMU) then the KVM timer state is
     * already saved and can be restored when it is synced back to KVM.
     */
    if (!running) {
        if (!cs->kvm_vcpu_dirty) {
            ret = kvm_mips_save_count(cs);
            if (ret < 0) {
                fprintf(stderr, "Failed saving count\n");
            }
        }
    } else {
        /* Set clock restore time to now */
        count_resume = get_clock();
        ret = kvm_mips_put_one_reg64(cs, KVM_REG_MIPS_COUNT_RESUME,
                                     &count_resume);
        if (ret < 0) {
            fprintf(stderr, "Failed setting COUNT_RESUME\n");
            return;
        }

        if (!cs->kvm_vcpu_dirty) {
            ret = kvm_mips_restore_count(cs);
            if (ret < 0) {
                fprintf(stderr, "Failed restoring count\n");
            }
        }
    }
}

static int kvm_mips_put_cp0_registers(CPUState *cs, int level)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int err, ret = 0;

    (void)level;

    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_INDEX, &env->CP0_Index);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_INDEX (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_CONTEXT,
                                 &env->CP0_Context);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_CONTEXT (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_USERLOCAL,
                                 &env->active_tc.CP0_UserLocal);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_USERLOCAL (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_PAGEMASK,
                               &env->CP0_PageMask);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_PAGEMASK (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_WIRED, &env->CP0_Wired);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_WIRED (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_HWRENA, &env->CP0_HWREna);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_HWRENA (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_BADVADDR,
                                 &env->CP0_BadVAddr);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_BADVADDR (%d)\n", __func__, err);
        ret = err;
    }

    /* If VM clock stopped then state will be restored when it is restarted */
    if (runstate_is_running()) {
        err = kvm_mips_restore_count(cs);
        if (err < 0) {
            ret = err;
        }
    }

    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_ENTRYHI,
                                 &env->CP0_EntryHi);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_ENTRYHI (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_COMPARE,
                               &env->CP0_Compare);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_COMPARE (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_STATUS, &env->CP0_Status);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_STATUS (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_EPC, &env->CP0_EPC);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_EPC (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_ERROREPC,
                                 &env->CP0_ErrorEPC);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_ERROREPC (%d)\n", __func__, err);
        ret = err;
    }

    return ret;
}

static int kvm_mips_get_cp0_registers(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int err, ret = 0;

    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_INDEX, &env->CP0_Index);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_INDEX (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_CONTEXT,
                                 &env->CP0_Context);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CONTEXT (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_USERLOCAL,
                                 &env->active_tc.CP0_UserLocal);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_USERLOCAL (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_PAGEMASK,
                               &env->CP0_PageMask);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_PAGEMASK (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_WIRED, &env->CP0_Wired);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_WIRED (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_HWRENA, &env->CP0_HWREna);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_HWRENA (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_BADVADDR,
                                 &env->CP0_BadVAddr);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_BADVADDR (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_ENTRYHI,
                                 &env->CP0_EntryHi);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_ENTRYHI (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_COMPARE,
                               &env->CP0_Compare);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_COMPARE (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_STATUS, &env->CP0_Status);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_STATUS (%d)\n", __func__, err);
        ret = err;
    }

    /* If VM clock stopped then state was already saved when it was stopped */
    if (runstate_is_running()) {
        err = kvm_mips_save_count(cs);
        if (err < 0) {
            ret = err;
        }
    }

    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_EPC, &env->CP0_EPC);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_EPC (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_ERROREPC,
                                 &env->CP0_ErrorEPC);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_ERROREPC (%d)\n", __func__, err);
        ret = err;
    }

    return ret;
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    struct kvm_regs regs;
    int ret;
    int i;

    /* Set the registers based on QEMU's view of things */
    for (i = 0; i < 32; i++) {
        regs.gpr[i] = env->active_tc.gpr[i];
    }

    regs.hi = env->active_tc.HI[0];
    regs.lo = env->active_tc.LO[0];
    regs.pc = env->active_tc.PC;

    ret = kvm_vcpu_ioctl(cs, KVM_SET_REGS, &regs);

    if (ret < 0) {
        return ret;
    }

    ret = kvm_mips_put_cp0_registers(cs, level);
    if (ret < 0) {
        return ret;
    }

    return ret;
}

int kvm_arch_get_registers(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int ret = 0;
    struct kvm_regs regs;
    int i;

    /* Get the current register set as KVM seems it */
    ret = kvm_vcpu_ioctl(cs, KVM_GET_REGS, &regs);

    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < 32; i++) {
        env->active_tc.gpr[i] = regs.gpr[i];
    }

    env->active_tc.HI[0] = regs.hi;
    env->active_tc.LO[0] = regs.lo;
    env->active_tc.PC = regs.pc;

    kvm_mips_get_cp0_registers(cs);

    return ret;
}
