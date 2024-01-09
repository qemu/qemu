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

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>

#include "cpu.h"
#include "internal.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "sysemu/runstate.h"
#include "kvm_mips.h"
#include "hw/boards.h"
#include "fpu_helper.h"

#define DEBUG_KVM 0

#define DPRINTF(fmt, ...) \
    do { if (DEBUG_KVM) { fprintf(stderr, fmt, ## __VA_ARGS__); } } while (0)

static int kvm_mips_fpu_cap;
static int kvm_mips_msa_cap;

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static void kvm_mips_update_state(void *opaque, bool running, RunState state);

unsigned long kvm_arch_vcpu_id(CPUState *cs)
{
    return cs->cpu_index;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    /* MIPS has 128 signals */
    kvm_set_sigmask_len(s, 16);

    kvm_mips_fpu_cap = kvm_check_extension(s, KVM_CAP_MIPS_FPU);
    kvm_mips_msa_cap = kvm_check_extension(s, KVM_CAP_MIPS_MSA);

    DPRINTF("%s\n", __func__);
    return 0;
}

int kvm_arch_irqchip_create(KVMState *s)
{
    return 0;
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int ret = 0;

    qemu_add_vm_change_state_handler(kvm_mips_update_state, cs);

    if (kvm_mips_fpu_cap && env->CP0_Config1 & (1 << CP0C1_FP)) {
        ret = kvm_vcpu_enable_cap(cs, KVM_CAP_MIPS_FPU, 0, 0);
        if (ret < 0) {
            /* mark unsupported so it gets disabled on reset */
            kvm_mips_fpu_cap = 0;
            ret = 0;
        }
    }

    if (kvm_mips_msa_cap && ase_msa_available(env)) {
        ret = kvm_vcpu_enable_cap(cs, KVM_CAP_MIPS_MSA, 0, 0);
        if (ret < 0) {
            /* mark unsupported so it gets disabled on reset */
            kvm_mips_msa_cap = 0;
            ret = 0;
        }
    }

    DPRINTF("%s\n", __func__);
    return ret;
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

void kvm_mips_reset_vcpu(MIPSCPU *cpu)
{
    CPUMIPSState *env = &cpu->env;

    if (!kvm_mips_fpu_cap && env->CP0_Config1 & (1 << CP0C1_FP)) {
        warn_report("KVM does not support FPU, disabling");
        env->CP0_Config1 &= ~(1 << CP0C1_FP);
    }
    if (!kvm_mips_msa_cap && ase_msa_available(env)) {
        warn_report("KVM does not support MSA, disabling");
        env->CP0_Config3 &= ~(1 << CP0C3_MSAP);
    }

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

    return env->CP0_Cause & (0x1 << (2 + CP0Ca_IP));
}


void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    int r;
    struct kvm_mips_interrupt intr;

    bql_lock();

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

    bql_unlock();
}

MemTxAttrs kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
    return MEMTXATTRS_UNSPECIFIED;
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

void kvm_arch_init_irq_routing(KVMState *s)
{
}

int kvm_mips_set_interrupt(MIPSCPU *cpu, int irq, int level)
{
    CPUState *cs = CPU(cpu);
    struct kvm_mips_interrupt intr;

    assert(kvm_enabled());

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

    assert(kvm_enabled());

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
    (KVM_REG_MIPS_CP0 | KVM_REG_SIZE_U32 | (8 * (_R) + (_S)))

#define MIPS_CP0_64(_R, _S)                                     \
    (KVM_REG_MIPS_CP0 | KVM_REG_SIZE_U64 | (8 * (_R) + (_S)))

#define KVM_REG_MIPS_CP0_INDEX          MIPS_CP0_32(0, 0)
#define KVM_REG_MIPS_CP0_RANDOM         MIPS_CP0_32(1, 0)
#define KVM_REG_MIPS_CP0_CONTEXT        MIPS_CP0_64(4, 0)
#define KVM_REG_MIPS_CP0_USERLOCAL      MIPS_CP0_64(4, 2)
#define KVM_REG_MIPS_CP0_PAGEMASK       MIPS_CP0_32(5, 0)
#define KVM_REG_MIPS_CP0_PAGEGRAIN      MIPS_CP0_32(5, 1)
#define KVM_REG_MIPS_CP0_PWBASE         MIPS_CP0_64(5, 5)
#define KVM_REG_MIPS_CP0_PWFIELD        MIPS_CP0_64(5, 6)
#define KVM_REG_MIPS_CP0_PWSIZE         MIPS_CP0_64(5, 7)
#define KVM_REG_MIPS_CP0_WIRED          MIPS_CP0_32(6, 0)
#define KVM_REG_MIPS_CP0_PWCTL          MIPS_CP0_32(6, 6)
#define KVM_REG_MIPS_CP0_HWRENA         MIPS_CP0_32(7, 0)
#define KVM_REG_MIPS_CP0_BADVADDR       MIPS_CP0_64(8, 0)
#define KVM_REG_MIPS_CP0_COUNT          MIPS_CP0_32(9, 0)
#define KVM_REG_MIPS_CP0_ENTRYHI        MIPS_CP0_64(10, 0)
#define KVM_REG_MIPS_CP0_COMPARE        MIPS_CP0_32(11, 0)
#define KVM_REG_MIPS_CP0_STATUS         MIPS_CP0_32(12, 0)
#define KVM_REG_MIPS_CP0_CAUSE          MIPS_CP0_32(13, 0)
#define KVM_REG_MIPS_CP0_EPC            MIPS_CP0_64(14, 0)
#define KVM_REG_MIPS_CP0_PRID           MIPS_CP0_32(15, 0)
#define KVM_REG_MIPS_CP0_EBASE          MIPS_CP0_64(15, 1)
#define KVM_REG_MIPS_CP0_CONFIG         MIPS_CP0_32(16, 0)
#define KVM_REG_MIPS_CP0_CONFIG1        MIPS_CP0_32(16, 1)
#define KVM_REG_MIPS_CP0_CONFIG2        MIPS_CP0_32(16, 2)
#define KVM_REG_MIPS_CP0_CONFIG3        MIPS_CP0_32(16, 3)
#define KVM_REG_MIPS_CP0_CONFIG4        MIPS_CP0_32(16, 4)
#define KVM_REG_MIPS_CP0_CONFIG5        MIPS_CP0_32(16, 5)
#define KVM_REG_MIPS_CP0_CONFIG6        MIPS_CP0_32(16, 6)
#define KVM_REG_MIPS_CP0_XCONTEXT       MIPS_CP0_64(20, 0)
#define KVM_REG_MIPS_CP0_ERROREPC       MIPS_CP0_64(30, 0)
#define KVM_REG_MIPS_CP0_KSCRATCH1      MIPS_CP0_64(31, 2)
#define KVM_REG_MIPS_CP0_KSCRATCH2      MIPS_CP0_64(31, 3)
#define KVM_REG_MIPS_CP0_KSCRATCH3      MIPS_CP0_64(31, 4)
#define KVM_REG_MIPS_CP0_KSCRATCH4      MIPS_CP0_64(31, 5)
#define KVM_REG_MIPS_CP0_KSCRATCH5      MIPS_CP0_64(31, 6)
#define KVM_REG_MIPS_CP0_KSCRATCH6      MIPS_CP0_64(31, 7)

static inline int kvm_mips_put_one_reg(CPUState *cs, uint64_t reg_id,
                                       int32_t *addr)
{
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &cp0reg);
}

static inline int kvm_mips_put_one_ureg(CPUState *cs, uint64_t reg_id,
                                        uint32_t *addr)
{
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
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
                                         int64_t *addr)
{
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &cp0reg);
}

static inline int kvm_mips_put_one_ureg64(CPUState *cs, uint64_t reg_id,
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
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &cp0reg);
}

static inline int kvm_mips_get_one_ureg(CPUState *cs, uint64_t reg_id,
                                        uint32_t *addr)
{
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &cp0reg);
}

static inline int kvm_mips_get_one_ulreg(CPUState *cs, uint64_t reg_id,
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

static inline int kvm_mips_get_one_reg64(CPUState *cs, uint64_t reg_id,
                                         int64_t *addr)
{
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &cp0reg);
}

static inline int kvm_mips_get_one_ureg64(CPUState *cs, uint64_t reg_id,
                                          uint64_t *addr)
{
    struct kvm_one_reg cp0reg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &cp0reg);
}

#define KVM_REG_MIPS_CP0_CONFIG_MASK    (1U << CP0C0_M)
#define KVM_REG_MIPS_CP0_CONFIG1_MASK   ((1U << CP0C1_M) | \
                                         (1U << CP0C1_FP))
#define KVM_REG_MIPS_CP0_CONFIG2_MASK   (1U << CP0C2_M)
#define KVM_REG_MIPS_CP0_CONFIG3_MASK   ((1U << CP0C3_M) | \
                                         (1U << CP0C3_MSAP))
#define KVM_REG_MIPS_CP0_CONFIG4_MASK   (1U << CP0C4_M)
#define KVM_REG_MIPS_CP0_CONFIG5_MASK   ((1U << CP0C5_MSAEn) | \
                                         (1U << CP0C5_UFE) | \
                                         (1U << CP0C5_FRE) | \
                                         (1U << CP0C5_UFR))
#define KVM_REG_MIPS_CP0_CONFIG6_MASK   ((1U << CP0C6_BPPASS) | \
                                         (0x3fU << CP0C6_KPOS) | \
                                         (1U << CP0C6_KE) | \
                                         (1U << CP0C6_VTLBONLY) | \
                                         (1U << CP0C6_LASX) | \
                                         (1U << CP0C6_SSEN) | \
                                         (1U << CP0C6_DISDRTIME) | \
                                         (1U << CP0C6_PIXNUEN) | \
                                         (1U << CP0C6_SCRAND) | \
                                         (1U << CP0C6_LLEXCEN) | \
                                         (1U << CP0C6_DISVC) | \
                                         (1U << CP0C6_VCLRU) | \
                                         (1U << CP0C6_DCLRU) | \
                                         (1U << CP0C6_PIXUEN) | \
                                         (1U << CP0C6_DISBLKLYEN) | \
                                         (1U << CP0C6_UMEMUALEN) | \
                                         (1U << CP0C6_SFBEN) | \
                                         (1U << CP0C6_FLTINT) | \
                                         (1U << CP0C6_VLTINT) | \
                                         (1U << CP0C6_DISBTB) | \
                                         (3U << CP0C6_STPREFCTL) | \
                                         (1U << CP0C6_INSTPREF) | \
                                         (1U << CP0C6_DATAPREF))

static inline int kvm_mips_change_one_reg(CPUState *cs, uint64_t reg_id,
                                          int32_t *addr, int32_t mask)
{
    int err;
    int32_t tmp, change;

    err = kvm_mips_get_one_reg(cs, reg_id, &tmp);
    if (err < 0) {
        return err;
    }

    /* only change bits in mask */
    change = (*addr ^ tmp) & mask;
    if (!change) {
        return 0;
    }

    tmp = tmp ^ change;
    return kvm_mips_put_one_reg(cs, reg_id, &tmp);
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
    err = kvm_mips_get_one_ureg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
    if (err < 0) {
        DPRINTF("%s: Failed to get COUNT_CTL (%d)\n", __func__, err);
        ret = err;
    } else if (!(count_ctl & KVM_REG_MIPS_COUNT_CTL_DC)) {
        count_ctl |= KVM_REG_MIPS_COUNT_CTL_DC;
        err = kvm_mips_put_one_ureg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
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
    err_dc = kvm_mips_get_one_ureg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
    if (err_dc < 0) {
        DPRINTF("%s: Failed to get COUNT_CTL (%d)\n", __func__, err_dc);
        ret = err_dc;
    } else if (!(count_ctl & KVM_REG_MIPS_COUNT_CTL_DC)) {
        /* freeze timer (sets COUNT_RESUME for us) */
        count_ctl |= KVM_REG_MIPS_COUNT_CTL_DC;
        err = kvm_mips_put_one_ureg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
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
        err = kvm_mips_put_one_ureg64(cs, KVM_REG_MIPS_COUNT_CTL, &count_ctl);
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
static void kvm_mips_update_state(void *opaque, bool running, RunState state)
{
    CPUState *cs = opaque;
    int ret;
    uint64_t count_resume;

    /*
     * If state is already dirty (synced to QEMU) then the KVM timer state is
     * already saved and can be restored when it is synced back to KVM.
     */
    if (!running) {
        if (!cs->vcpu_dirty) {
            ret = kvm_mips_save_count(cs);
            if (ret < 0) {
                warn_report("Failed saving count");
            }
        }
    } else {
        /* Set clock restore time to now */
        count_resume = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        ret = kvm_mips_put_one_ureg64(cs, KVM_REG_MIPS_COUNT_RESUME,
                                      &count_resume);
        if (ret < 0) {
            warn_report("Failed setting COUNT_RESUME");
            return;
        }

        if (!cs->vcpu_dirty) {
            ret = kvm_mips_restore_count(cs);
            if (ret < 0) {
                warn_report("Failed restoring count");
            }
        }
    }
}

static int kvm_mips_put_fpu_registers(CPUState *cs, int level)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int err, ret = 0;
    unsigned int i;

    /* Only put FPU state if we're emulating a CPU with an FPU */
    if (env->CP0_Config1 & (1 << CP0C1_FP)) {
        /* FPU Control Registers */
        if (level == KVM_PUT_FULL_STATE) {
            err = kvm_mips_put_one_ureg(cs, KVM_REG_MIPS_FCR_IR,
                                        &env->active_fpu.fcr0);
            if (err < 0) {
                DPRINTF("%s: Failed to put FCR_IR (%d)\n", __func__, err);
                ret = err;
            }
        }
        err = kvm_mips_put_one_ureg(cs, KVM_REG_MIPS_FCR_CSR,
                                    &env->active_fpu.fcr31);
        if (err < 0) {
            DPRINTF("%s: Failed to put FCR_CSR (%d)\n", __func__, err);
            ret = err;
        }

        /*
         * FPU register state is a subset of MSA vector state, so don't put FPU
         * registers if we're emulating a CPU with MSA.
         */
        if (!ase_msa_available(env)) {
            /* Floating point registers */
            for (i = 0; i < 32; ++i) {
                if (env->CP0_Status & (1 << CP0St_FR)) {
                    err = kvm_mips_put_one_ureg64(cs, KVM_REG_MIPS_FPR_64(i),
                                                  &env->active_fpu.fpr[i].d);
                } else {
                    err = kvm_mips_get_one_ureg(cs, KVM_REG_MIPS_FPR_32(i),
                                    &env->active_fpu.fpr[i].w[FP_ENDIAN_IDX]);
                }
                if (err < 0) {
                    DPRINTF("%s: Failed to put FPR%u (%d)\n", __func__, i, err);
                    ret = err;
                }
            }
        }
    }

    /* Only put MSA state if we're emulating a CPU with MSA */
    if (ase_msa_available(env)) {
        /* MSA Control Registers */
        if (level == KVM_PUT_FULL_STATE) {
            err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_MSA_IR,
                                       &env->msair);
            if (err < 0) {
                DPRINTF("%s: Failed to put MSA_IR (%d)\n", __func__, err);
                ret = err;
            }
        }
        err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_MSA_CSR,
                                   &env->active_tc.msacsr);
        if (err < 0) {
            DPRINTF("%s: Failed to put MSA_CSR (%d)\n", __func__, err);
            ret = err;
        }

        /* Vector registers (includes FP registers) */
        for (i = 0; i < 32; ++i) {
            /* Big endian MSA not supported by QEMU yet anyway */
            err = kvm_mips_put_one_reg64(cs, KVM_REG_MIPS_VEC_128(i),
                                         env->active_fpu.fpr[i].wr.d);
            if (err < 0) {
                DPRINTF("%s: Failed to put VEC%u (%d)\n", __func__, i, err);
                ret = err;
            }
        }
    }

    return ret;
}

static int kvm_mips_get_fpu_registers(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int err, ret = 0;
    unsigned int i;

    /* Only get FPU state if we're emulating a CPU with an FPU */
    if (env->CP0_Config1 & (1 << CP0C1_FP)) {
        /* FPU Control Registers */
        err = kvm_mips_get_one_ureg(cs, KVM_REG_MIPS_FCR_IR,
                                    &env->active_fpu.fcr0);
        if (err < 0) {
            DPRINTF("%s: Failed to get FCR_IR (%d)\n", __func__, err);
            ret = err;
        }
        err = kvm_mips_get_one_ureg(cs, KVM_REG_MIPS_FCR_CSR,
                                    &env->active_fpu.fcr31);
        if (err < 0) {
            DPRINTF("%s: Failed to get FCR_CSR (%d)\n", __func__, err);
            ret = err;
        } else {
            restore_fp_status(env);
        }

        /*
         * FPU register state is a subset of MSA vector state, so don't save FPU
         * registers if we're emulating a CPU with MSA.
         */
        if (!ase_msa_available(env)) {
            /* Floating point registers */
            for (i = 0; i < 32; ++i) {
                if (env->CP0_Status & (1 << CP0St_FR)) {
                    err = kvm_mips_get_one_ureg64(cs, KVM_REG_MIPS_FPR_64(i),
                                                  &env->active_fpu.fpr[i].d);
                } else {
                    err = kvm_mips_get_one_ureg(cs, KVM_REG_MIPS_FPR_32(i),
                                    &env->active_fpu.fpr[i].w[FP_ENDIAN_IDX]);
                }
                if (err < 0) {
                    DPRINTF("%s: Failed to get FPR%u (%d)\n", __func__, i, err);
                    ret = err;
                }
            }
        }
    }

    /* Only get MSA state if we're emulating a CPU with MSA */
    if (ase_msa_available(env)) {
        /* MSA Control Registers */
        err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_MSA_IR,
                                   &env->msair);
        if (err < 0) {
            DPRINTF("%s: Failed to get MSA_IR (%d)\n", __func__, err);
            ret = err;
        }
        err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_MSA_CSR,
                                   &env->active_tc.msacsr);
        if (err < 0) {
            DPRINTF("%s: Failed to get MSA_CSR (%d)\n", __func__, err);
            ret = err;
        } else {
            restore_msa_fp_status(env);
        }

        /* Vector registers (includes FP registers) */
        for (i = 0; i < 32; ++i) {
            /* Big endian MSA not supported by QEMU yet anyway */
            err = kvm_mips_get_one_reg64(cs, KVM_REG_MIPS_VEC_128(i),
                                         env->active_fpu.fpr[i].wr.d);
            if (err < 0) {
                DPRINTF("%s: Failed to get VEC%u (%d)\n", __func__, i, err);
                ret = err;
            }
        }
    }

    return ret;
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
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_RANDOM, &env->CP0_Random);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_RANDOM (%d)\n", __func__, err);
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
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_PAGEGRAIN,
                               &env->CP0_PageGrain);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_PAGEGRAIN (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_PWBASE,
                               &env->CP0_PWBase);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_PWBASE (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_PWFIELD,
                               &env->CP0_PWField);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_PWField (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_PWSIZE,
                               &env->CP0_PWSize);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_PWSIZE (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_WIRED, &env->CP0_Wired);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_WIRED (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_PWCTL, &env->CP0_PWCtl);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_PWCTL (%d)\n", __func__, err);
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
    err = kvm_mips_put_one_reg(cs, KVM_REG_MIPS_CP0_PRID, &env->CP0_PRid);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_PRID (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_EBASE, &env->CP0_EBase);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_EBASE (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_change_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG,
                                  &env->CP0_Config0,
                                  KVM_REG_MIPS_CP0_CONFIG_MASK);
    if (err < 0) {
        DPRINTF("%s: Failed to change CP0_CONFIG (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_change_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG1,
                                  &env->CP0_Config1,
                                  KVM_REG_MIPS_CP0_CONFIG1_MASK);
    if (err < 0) {
        DPRINTF("%s: Failed to change CP0_CONFIG1 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_change_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG2,
                                  &env->CP0_Config2,
                                  KVM_REG_MIPS_CP0_CONFIG2_MASK);
    if (err < 0) {
        DPRINTF("%s: Failed to change CP0_CONFIG2 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_change_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG3,
                                  &env->CP0_Config3,
                                  KVM_REG_MIPS_CP0_CONFIG3_MASK);
    if (err < 0) {
        DPRINTF("%s: Failed to change CP0_CONFIG3 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_change_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG4,
                                  &env->CP0_Config4,
                                  KVM_REG_MIPS_CP0_CONFIG4_MASK);
    if (err < 0) {
        DPRINTF("%s: Failed to change CP0_CONFIG4 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_change_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG5,
                                  &env->CP0_Config5,
                                  KVM_REG_MIPS_CP0_CONFIG5_MASK);
    if (err < 0) {
        DPRINTF("%s: Failed to change CP0_CONFIG5 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_change_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG6,
                                  &env->CP0_Config6,
                                  KVM_REG_MIPS_CP0_CONFIG6_MASK);
    if (err < 0) {
        DPRINTF("%s: Failed to change CP0_CONFIG6 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_XCONTEXT,
                                 &env->CP0_XContext);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_XCONTEXT (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_ERROREPC,
                                 &env->CP0_ErrorEPC);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_ERROREPC (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH1,
                                 &env->CP0_KScratch[0]);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_KSCRATCH1 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH2,
                                 &env->CP0_KScratch[1]);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_KSCRATCH2 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH3,
                                 &env->CP0_KScratch[2]);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_KSCRATCH3 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH4,
                                 &env->CP0_KScratch[3]);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_KSCRATCH4 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH5,
                                 &env->CP0_KScratch[4]);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_KSCRATCH5 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_put_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH6,
                                 &env->CP0_KScratch[5]);
    if (err < 0) {
        DPRINTF("%s: Failed to put CP0_KSCRATCH6 (%d)\n", __func__, err);
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
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_RANDOM, &env->CP0_Random);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_RANDOM (%d)\n", __func__, err);
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
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_PAGEGRAIN,
                               &env->CP0_PageGrain);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_PAGEGRAIN (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_PWBASE,
                               &env->CP0_PWBase);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_PWBASE (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_PWFIELD,
                               &env->CP0_PWField);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_PWFIELD (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_PWSIZE,
                               &env->CP0_PWSize);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_PWSIZE (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_WIRED, &env->CP0_Wired);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_WIRED (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_PWCTL, &env->CP0_PWCtl);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_PWCtl (%d)\n", __func__, err);
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
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_PRID, &env->CP0_PRid);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_PRID (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_EBASE, &env->CP0_EBase);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_EBASE (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG, &env->CP0_Config0);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CONFIG (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG1, &env->CP0_Config1);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CONFIG1 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG2, &env->CP0_Config2);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CONFIG2 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG3, &env->CP0_Config3);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CONFIG3 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG4, &env->CP0_Config4);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CONFIG4 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG5, &env->CP0_Config5);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CONFIG5 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_reg(cs, KVM_REG_MIPS_CP0_CONFIG6, &env->CP0_Config6);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_CONFIG6 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_XCONTEXT,
                                 &env->CP0_XContext);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_XCONTEXT (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_ERROREPC,
                                 &env->CP0_ErrorEPC);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_ERROREPC (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH1,
                                 &env->CP0_KScratch[0]);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_KSCRATCH1 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH2,
                                 &env->CP0_KScratch[1]);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_KSCRATCH2 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH3,
                                 &env->CP0_KScratch[2]);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_KSCRATCH3 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH4,
                                 &env->CP0_KScratch[3]);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_KSCRATCH4 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH5,
                                 &env->CP0_KScratch[4]);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_KSCRATCH5 (%d)\n", __func__, err);
        ret = err;
    }
    err = kvm_mips_get_one_ulreg(cs, KVM_REG_MIPS_CP0_KSCRATCH6,
                                 &env->CP0_KScratch[5]);
    if (err < 0) {
        DPRINTF("%s: Failed to get CP0_KSCRATCH6 (%d)\n", __func__, err);
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
        regs.gpr[i] = (int64_t)(target_long)env->active_tc.gpr[i];
    }

    regs.hi = (int64_t)(target_long)env->active_tc.HI[0];
    regs.lo = (int64_t)(target_long)env->active_tc.LO[0];
    regs.pc = (int64_t)(target_long)env->active_tc.PC;

    ret = kvm_vcpu_ioctl(cs, KVM_SET_REGS, &regs);

    if (ret < 0) {
        return ret;
    }

    ret = kvm_mips_put_cp0_registers(cs, level);
    if (ret < 0) {
        return ret;
    }

    ret = kvm_mips_put_fpu_registers(cs, level);
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
    kvm_mips_get_fpu_registers(cs);

    return ret;
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
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
    abort();
}

int kvm_arch_get_default_type(MachineState *machine)
{
#if defined(KVM_CAP_MIPS_VZ)
    int r;
    KVMState *s = KVM_STATE(machine->accelerator);

    r = kvm_check_extension(s, KVM_CAP_MIPS_VZ);
    if (r > 0) {
        return KVM_VM_MIPS_VZ;
    }
#endif

    error_report("KVM_VM_MIPS_VZ type is not available");
    return -1;
}

bool kvm_arch_cpu_check_are_resettable(void)
{
    return true;
}

void kvm_arch_accel_class_init(ObjectClass *oc)
{
}
