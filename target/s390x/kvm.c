/*
 * QEMU S390x KVM implementation
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Copyright IBM Corp. 2012
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * Contributions after 2012-10-29 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 *
 * You should have received a copy of the GNU (Lesser) General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>
#include <asm/ptrace.h>

#include "qemu-common.h"
#include "cpu.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "sysemu/hw_accel.h"
#include "hw/hw.h"
#include "sysemu/device_tree.h"
#include "qapi/qmp/qjson.h"
#include "exec/gdbstub.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "qapi-event.h"
#include "hw/s390x/s390-pci-inst.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/ipl.h"
#include "hw/s390x/ebcdic.h"
#include "exec/memattrs.h"
#include "hw/s390x/s390-virtio-ccw.h"

#ifndef DEBUG_KVM
#define DEBUG_KVM  0
#endif

#define DPRINTF(fmt, ...) do {                \
    if (DEBUG_KVM) {                          \
        fprintf(stderr, fmt, ## __VA_ARGS__); \
    }                                         \
} while (0);

#define kvm_vm_check_mem_attr(s, attr) \
    kvm_vm_check_attr(s, KVM_S390_VM_MEM_CTRL, attr)

#define IPA0_DIAG                       0x8300
#define IPA0_SIGP                       0xae00
#define IPA0_B2                         0xb200
#define IPA0_B9                         0xb900
#define IPA0_EB                         0xeb00
#define IPA0_E3                         0xe300

#define PRIV_B2_SCLP_CALL               0x20
#define PRIV_B2_CSCH                    0x30
#define PRIV_B2_HSCH                    0x31
#define PRIV_B2_MSCH                    0x32
#define PRIV_B2_SSCH                    0x33
#define PRIV_B2_STSCH                   0x34
#define PRIV_B2_TSCH                    0x35
#define PRIV_B2_TPI                     0x36
#define PRIV_B2_SAL                     0x37
#define PRIV_B2_RSCH                    0x38
#define PRIV_B2_STCRW                   0x39
#define PRIV_B2_STCPS                   0x3a
#define PRIV_B2_RCHP                    0x3b
#define PRIV_B2_SCHM                    0x3c
#define PRIV_B2_CHSC                    0x5f
#define PRIV_B2_SIGA                    0x74
#define PRIV_B2_XSCH                    0x76

#define PRIV_EB_SQBS                    0x8a
#define PRIV_EB_PCISTB                  0xd0
#define PRIV_EB_SIC                     0xd1

#define PRIV_B9_EQBS                    0x9c
#define PRIV_B9_CLP                     0xa0
#define PRIV_B9_PCISTG                  0xd0
#define PRIV_B9_PCILG                   0xd2
#define PRIV_B9_RPCIT                   0xd3

#define PRIV_E3_MPCIFC                  0xd0
#define PRIV_E3_STPCIFC                 0xd4

#define DIAG_TIMEREVENT                 0x288
#define DIAG_IPL                        0x308
#define DIAG_KVM_HYPERCALL              0x500
#define DIAG_KVM_BREAKPOINT             0x501

#define ICPT_INSTRUCTION                0x04
#define ICPT_PROGRAM                    0x08
#define ICPT_EXT_INT                    0x14
#define ICPT_WAITPSW                    0x1c
#define ICPT_SOFT_INTERCEPT             0x24
#define ICPT_CPU_STOP                   0x28
#define ICPT_OPEREXC                    0x2c
#define ICPT_IO                         0x40

#define NR_LOCAL_IRQS 32
/*
 * Needs to be big enough to contain max_cpus emergency signals
 * and in addition NR_LOCAL_IRQS interrupts
 */
#define VCPU_IRQ_BUF_SIZE (sizeof(struct kvm_s390_irq) * \
                           (max_cpus + NR_LOCAL_IRQS))

static CPUWatchpoint hw_watchpoint;
/*
 * We don't use a list because this structure is also used to transmit the
 * hardware breakpoints to the kernel.
 */
static struct kvm_hw_breakpoint *hw_breakpoints;
static int nb_hw_breakpoints;

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static QemuMutex qemu_sigp_mutex;

static int cap_sync_regs;
static int cap_async_pf;
static int cap_mem_op;
static int cap_s390_irq;
static int cap_ri;
static int cap_gs;

static int active_cmma;

static void *legacy_s390_alloc(size_t size, uint64_t *align);

static int kvm_s390_query_mem_limit(KVMState *s, uint64_t *memory_limit)
{
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_MEM_CTRL,
        .attr = KVM_S390_VM_MEM_LIMIT_SIZE,
        .addr = (uint64_t) memory_limit,
    };

    return kvm_vm_ioctl(s, KVM_GET_DEVICE_ATTR, &attr);
}

int kvm_s390_set_mem_limit(KVMState *s, uint64_t new_limit, uint64_t *hw_limit)
{
    int rc;

    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_MEM_CTRL,
        .attr = KVM_S390_VM_MEM_LIMIT_SIZE,
        .addr = (uint64_t) &new_limit,
    };

    if (!kvm_vm_check_mem_attr(s, KVM_S390_VM_MEM_LIMIT_SIZE)) {
        return 0;
    }

    rc = kvm_s390_query_mem_limit(s, hw_limit);
    if (rc) {
        return rc;
    } else if (*hw_limit < new_limit) {
        return -E2BIG;
    }

    return kvm_vm_ioctl(s, KVM_SET_DEVICE_ATTR, &attr);
}

int kvm_s390_cmma_active(void)
{
    return active_cmma;
}

static bool kvm_s390_cmma_available(void)
{
    static bool initialized, value;

    if (!initialized) {
        initialized = true;
        value = kvm_vm_check_mem_attr(kvm_state, KVM_S390_VM_MEM_ENABLE_CMMA) &&
                kvm_vm_check_mem_attr(kvm_state, KVM_S390_VM_MEM_CLR_CMMA);
    }
    return value;
}

void kvm_s390_cmma_reset(void)
{
    int rc;
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_MEM_CTRL,
        .attr = KVM_S390_VM_MEM_CLR_CMMA,
    };

    if (!kvm_s390_cmma_active()) {
        return;
    }

    rc = kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
    trace_kvm_clear_cmma(rc);
}

static void kvm_s390_enable_cmma(void)
{
    int rc;
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_MEM_CTRL,
        .attr = KVM_S390_VM_MEM_ENABLE_CMMA,
    };

    if (mem_path) {
        error_report("Warning: CMM will not be enabled because it is not "
                     "compatible to hugetlbfs.");
        return;
    }
    rc = kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
    active_cmma = !rc;
    trace_kvm_enable_cmma(rc);
}

static void kvm_s390_set_attr(uint64_t attr)
{
    struct kvm_device_attr attribute = {
        .group = KVM_S390_VM_CRYPTO,
        .attr  = attr,
    };

    int ret = kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attribute);

    if (ret) {
        error_report("Failed to set crypto device attribute %lu: %s",
                     attr, strerror(-ret));
    }
}

static void kvm_s390_init_aes_kw(void)
{
    uint64_t attr = KVM_S390_VM_CRYPTO_DISABLE_AES_KW;

    if (object_property_get_bool(OBJECT(qdev_get_machine()), "aes-key-wrap",
                                 NULL)) {
            attr = KVM_S390_VM_CRYPTO_ENABLE_AES_KW;
    }

    if (kvm_vm_check_attr(kvm_state, KVM_S390_VM_CRYPTO, attr)) {
            kvm_s390_set_attr(attr);
    }
}

static void kvm_s390_init_dea_kw(void)
{
    uint64_t attr = KVM_S390_VM_CRYPTO_DISABLE_DEA_KW;

    if (object_property_get_bool(OBJECT(qdev_get_machine()), "dea-key-wrap",
                                 NULL)) {
            attr = KVM_S390_VM_CRYPTO_ENABLE_DEA_KW;
    }

    if (kvm_vm_check_attr(kvm_state, KVM_S390_VM_CRYPTO, attr)) {
            kvm_s390_set_attr(attr);
    }
}

void kvm_s390_crypto_reset(void)
{
    if (s390_has_feat(S390_FEAT_MSA_EXT_3)) {
        kvm_s390_init_aes_kw();
        kvm_s390_init_dea_kw();
    }
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    cap_sync_regs = kvm_check_extension(s, KVM_CAP_SYNC_REGS);
    cap_async_pf = kvm_check_extension(s, KVM_CAP_ASYNC_PF);
    cap_mem_op = kvm_check_extension(s, KVM_CAP_S390_MEM_OP);
    cap_s390_irq = kvm_check_extension(s, KVM_CAP_S390_INJECT_IRQ);

    if (!kvm_check_extension(s, KVM_CAP_S390_GMAP)
        || !kvm_check_extension(s, KVM_CAP_S390_COW)) {
        phys_mem_set_alloc(legacy_s390_alloc);
    }

    kvm_vm_enable_cap(s, KVM_CAP_S390_USER_SIGP, 0);
    kvm_vm_enable_cap(s, KVM_CAP_S390_VECTOR_REGISTERS, 0);
    kvm_vm_enable_cap(s, KVM_CAP_S390_USER_STSI, 0);
    if (ri_allowed()) {
        if (kvm_vm_enable_cap(s, KVM_CAP_S390_RI, 0) == 0) {
            cap_ri = 1;
        }
    }
    if (gs_allowed()) {
        if (kvm_vm_enable_cap(s, KVM_CAP_S390_GS, 0) == 0) {
            cap_gs = 1;
        }
    }

    /*
     * The migration interface for ais was introduced with kernel 4.13
     * but the capability itself had been active since 4.12. As migration
     * support is considered necessary let's disable ais in the 2.10
     * machine.
     */
    /* kvm_vm_enable_cap(s, KVM_CAP_S390_AIS, 0); */

    qemu_mutex_init(&qemu_sigp_mutex);

    return 0;
}

int kvm_arch_irqchip_create(MachineState *ms, KVMState *s)
{
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);
    kvm_s390_set_cpu_state(cpu, cpu->env.cpu_state);
    cpu->irqstate = g_malloc0(VCPU_IRQ_BUF_SIZE);
    return 0;
}

void kvm_s390_reset_vcpu(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);

    /* The initial reset call is needed here to reset in-kernel
     * vcpu data that we can't access directly from QEMU
     * (i.e. with older kernels which don't support sync_regs/ONE_REG).
     * Before this ioctl cpu_synchronize_state() is called in common kvm
     * code (kvm-all) */
    if (kvm_vcpu_ioctl(cs, KVM_S390_INITIAL_RESET, NULL)) {
        error_report("Initial CPU reset failed on CPU %i", cs->cpu_index);
    }
}

static int can_sync_regs(CPUState *cs, int regs)
{
    return cap_sync_regs && (cs->kvm_run->kvm_valid_regs & regs) == regs;
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    struct kvm_fpu fpu = {};
    int r;
    int i;

    /* always save the PSW  and the GPRS*/
    cs->kvm_run->psw_addr = env->psw.addr;
    cs->kvm_run->psw_mask = env->psw.mask;

    if (can_sync_regs(cs, KVM_SYNC_GPRS)) {
        for (i = 0; i < 16; i++) {
            cs->kvm_run->s.regs.gprs[i] = env->regs[i];
            cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_GPRS;
        }
    } else {
        for (i = 0; i < 16; i++) {
            regs.gprs[i] = env->regs[i];
        }
        r = kvm_vcpu_ioctl(cs, KVM_SET_REGS, &regs);
        if (r < 0) {
            return r;
        }
    }

    if (can_sync_regs(cs, KVM_SYNC_VRS)) {
        for (i = 0; i < 32; i++) {
            cs->kvm_run->s.regs.vrs[i][0] = env->vregs[i][0].ll;
            cs->kvm_run->s.regs.vrs[i][1] = env->vregs[i][1].ll;
        }
        cs->kvm_run->s.regs.fpc = env->fpc;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_VRS;
    } else if (can_sync_regs(cs, KVM_SYNC_FPRS)) {
        for (i = 0; i < 16; i++) {
            cs->kvm_run->s.regs.fprs[i] = get_freg(env, i)->ll;
        }
        cs->kvm_run->s.regs.fpc = env->fpc;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_FPRS;
    } else {
        /* Floating point */
        for (i = 0; i < 16; i++) {
            fpu.fprs[i] = get_freg(env, i)->ll;
        }
        fpu.fpc = env->fpc;

        r = kvm_vcpu_ioctl(cs, KVM_SET_FPU, &fpu);
        if (r < 0) {
            return r;
        }
    }

    /* Do we need to save more than that? */
    if (level == KVM_PUT_RUNTIME_STATE) {
        return 0;
    }

    if (can_sync_regs(cs, KVM_SYNC_ARCH0)) {
        cs->kvm_run->s.regs.cputm = env->cputm;
        cs->kvm_run->s.regs.ckc = env->ckc;
        cs->kvm_run->s.regs.todpr = env->todpr;
        cs->kvm_run->s.regs.gbea = env->gbea;
        cs->kvm_run->s.regs.pp = env->pp;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_ARCH0;
    } else {
        /*
         * These ONE_REGS are not protected by a capability. As they are only
         * necessary for migration we just trace a possible error, but don't
         * return with an error return code.
         */
        kvm_set_one_reg(cs, KVM_REG_S390_CPU_TIMER, &env->cputm);
        kvm_set_one_reg(cs, KVM_REG_S390_CLOCK_COMP, &env->ckc);
        kvm_set_one_reg(cs, KVM_REG_S390_TODPR, &env->todpr);
        kvm_set_one_reg(cs, KVM_REG_S390_GBEA, &env->gbea);
        kvm_set_one_reg(cs, KVM_REG_S390_PP, &env->pp);
    }

    if (can_sync_regs(cs, KVM_SYNC_RICCB)) {
        memcpy(cs->kvm_run->s.regs.riccb, env->riccb, 64);
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_RICCB;
    }

    /* pfault parameters */
    if (can_sync_regs(cs, KVM_SYNC_PFAULT)) {
        cs->kvm_run->s.regs.pft = env->pfault_token;
        cs->kvm_run->s.regs.pfs = env->pfault_select;
        cs->kvm_run->s.regs.pfc = env->pfault_compare;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_PFAULT;
    } else if (cap_async_pf) {
        r = kvm_set_one_reg(cs, KVM_REG_S390_PFTOKEN, &env->pfault_token);
        if (r < 0) {
            return r;
        }
        r = kvm_set_one_reg(cs, KVM_REG_S390_PFCOMPARE, &env->pfault_compare);
        if (r < 0) {
            return r;
        }
        r = kvm_set_one_reg(cs, KVM_REG_S390_PFSELECT, &env->pfault_select);
        if (r < 0) {
            return r;
        }
    }

    /* access registers and control registers*/
    if (can_sync_regs(cs, KVM_SYNC_ACRS | KVM_SYNC_CRS)) {
        for (i = 0; i < 16; i++) {
            cs->kvm_run->s.regs.acrs[i] = env->aregs[i];
            cs->kvm_run->s.regs.crs[i] = env->cregs[i];
        }
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_ACRS;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_CRS;
    } else {
        for (i = 0; i < 16; i++) {
            sregs.acrs[i] = env->aregs[i];
            sregs.crs[i] = env->cregs[i];
        }
        r = kvm_vcpu_ioctl(cs, KVM_SET_SREGS, &sregs);
        if (r < 0) {
            return r;
        }
    }

    if (can_sync_regs(cs, KVM_SYNC_GSCB)) {
        memcpy(cs->kvm_run->s.regs.gscb, env->gscb, 32);
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_GSCB;
    }

    /* Finally the prefix */
    if (can_sync_regs(cs, KVM_SYNC_PREFIX)) {
        cs->kvm_run->s.regs.prefix = env->psa;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_PREFIX;
    } else {
        /* prefix is only supported via sync regs */
    }
    return 0;
}

int kvm_arch_get_registers(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    struct kvm_fpu fpu;
    int i, r;

    /* get the PSW */
    env->psw.addr = cs->kvm_run->psw_addr;
    env->psw.mask = cs->kvm_run->psw_mask;

    /* the GPRS */
    if (can_sync_regs(cs, KVM_SYNC_GPRS)) {
        for (i = 0; i < 16; i++) {
            env->regs[i] = cs->kvm_run->s.regs.gprs[i];
        }
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_GET_REGS, &regs);
        if (r < 0) {
            return r;
        }
         for (i = 0; i < 16; i++) {
            env->regs[i] = regs.gprs[i];
        }
    }

    /* The ACRS and CRS */
    if (can_sync_regs(cs, KVM_SYNC_ACRS | KVM_SYNC_CRS)) {
        for (i = 0; i < 16; i++) {
            env->aregs[i] = cs->kvm_run->s.regs.acrs[i];
            env->cregs[i] = cs->kvm_run->s.regs.crs[i];
        }
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_GET_SREGS, &sregs);
        if (r < 0) {
            return r;
        }
         for (i = 0; i < 16; i++) {
            env->aregs[i] = sregs.acrs[i];
            env->cregs[i] = sregs.crs[i];
        }
    }

    /* Floating point and vector registers */
    if (can_sync_regs(cs, KVM_SYNC_VRS)) {
        for (i = 0; i < 32; i++) {
            env->vregs[i][0].ll = cs->kvm_run->s.regs.vrs[i][0];
            env->vregs[i][1].ll = cs->kvm_run->s.regs.vrs[i][1];
        }
        env->fpc = cs->kvm_run->s.regs.fpc;
    } else if (can_sync_regs(cs, KVM_SYNC_FPRS)) {
        for (i = 0; i < 16; i++) {
            get_freg(env, i)->ll = cs->kvm_run->s.regs.fprs[i];
        }
        env->fpc = cs->kvm_run->s.regs.fpc;
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_GET_FPU, &fpu);
        if (r < 0) {
            return r;
        }
        for (i = 0; i < 16; i++) {
            get_freg(env, i)->ll = fpu.fprs[i];
        }
        env->fpc = fpu.fpc;
    }

    /* The prefix */
    if (can_sync_regs(cs, KVM_SYNC_PREFIX)) {
        env->psa = cs->kvm_run->s.regs.prefix;
    }

    if (can_sync_regs(cs, KVM_SYNC_ARCH0)) {
        env->cputm = cs->kvm_run->s.regs.cputm;
        env->ckc = cs->kvm_run->s.regs.ckc;
        env->todpr = cs->kvm_run->s.regs.todpr;
        env->gbea = cs->kvm_run->s.regs.gbea;
        env->pp = cs->kvm_run->s.regs.pp;
    } else {
        /*
         * These ONE_REGS are not protected by a capability. As they are only
         * necessary for migration we just trace a possible error, but don't
         * return with an error return code.
         */
        kvm_get_one_reg(cs, KVM_REG_S390_CPU_TIMER, &env->cputm);
        kvm_get_one_reg(cs, KVM_REG_S390_CLOCK_COMP, &env->ckc);
        kvm_get_one_reg(cs, KVM_REG_S390_TODPR, &env->todpr);
        kvm_get_one_reg(cs, KVM_REG_S390_GBEA, &env->gbea);
        kvm_get_one_reg(cs, KVM_REG_S390_PP, &env->pp);
    }

    if (can_sync_regs(cs, KVM_SYNC_RICCB)) {
        memcpy(env->riccb, cs->kvm_run->s.regs.riccb, 64);
    }

    if (can_sync_regs(cs, KVM_SYNC_GSCB)) {
        memcpy(env->gscb, cs->kvm_run->s.regs.gscb, 32);
    }

    /* pfault parameters */
    if (can_sync_regs(cs, KVM_SYNC_PFAULT)) {
        env->pfault_token = cs->kvm_run->s.regs.pft;
        env->pfault_select = cs->kvm_run->s.regs.pfs;
        env->pfault_compare = cs->kvm_run->s.regs.pfc;
    } else if (cap_async_pf) {
        r = kvm_get_one_reg(cs, KVM_REG_S390_PFTOKEN, &env->pfault_token);
        if (r < 0) {
            return r;
        }
        r = kvm_get_one_reg(cs, KVM_REG_S390_PFCOMPARE, &env->pfault_compare);
        if (r < 0) {
            return r;
        }
        r = kvm_get_one_reg(cs, KVM_REG_S390_PFSELECT, &env->pfault_select);
        if (r < 0) {
            return r;
        }
    }

    return 0;
}

int kvm_s390_get_clock(uint8_t *tod_high, uint64_t *tod_low)
{
    int r;
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_TOD,
        .attr = KVM_S390_VM_TOD_LOW,
        .addr = (uint64_t)tod_low,
    };

    r = kvm_vm_ioctl(kvm_state, KVM_GET_DEVICE_ATTR, &attr);
    if (r) {
        return r;
    }

    attr.attr = KVM_S390_VM_TOD_HIGH;
    attr.addr = (uint64_t)tod_high;
    return kvm_vm_ioctl(kvm_state, KVM_GET_DEVICE_ATTR, &attr);
}

int kvm_s390_set_clock(uint8_t *tod_high, uint64_t *tod_low)
{
    int r;

    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_TOD,
        .attr = KVM_S390_VM_TOD_LOW,
        .addr = (uint64_t)tod_low,
    };

    r = kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
    if (r) {
        return r;
    }

    attr.attr = KVM_S390_VM_TOD_HIGH;
    attr.addr = (uint64_t)tod_high;
    return kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
}

/**
 * kvm_s390_mem_op:
 * @addr:      the logical start address in guest memory
 * @ar:        the access register number
 * @hostbuf:   buffer in host memory. NULL = do only checks w/o copying
 * @len:       length that should be transferred
 * @is_write:  true = write, false = read
 * Returns:    0 on success, non-zero if an exception or error occurred
 *
 * Use KVM ioctl to read/write from/to guest memory. An access exception
 * is injected into the vCPU in case of translation errors.
 */
int kvm_s390_mem_op(S390CPU *cpu, vaddr addr, uint8_t ar, void *hostbuf,
                    int len, bool is_write)
{
    struct kvm_s390_mem_op mem_op = {
        .gaddr = addr,
        .flags = KVM_S390_MEMOP_F_INJECT_EXCEPTION,
        .size = len,
        .op = is_write ? KVM_S390_MEMOP_LOGICAL_WRITE
                       : KVM_S390_MEMOP_LOGICAL_READ,
        .buf = (uint64_t)hostbuf,
        .ar = ar,
    };
    int ret;

    if (!cap_mem_op) {
        return -ENOSYS;
    }
    if (!hostbuf) {
        mem_op.flags |= KVM_S390_MEMOP_F_CHECK_ONLY;
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_S390_MEM_OP, &mem_op);
    if (ret < 0) {
        error_printf("KVM_S390_MEM_OP failed: %s\n", strerror(-ret));
    }
    return ret;
}

/*
 * Legacy layout for s390:
 * Older S390 KVM requires the topmost vma of the RAM to be
 * smaller than an system defined value, which is at least 256GB.
 * Larger systems have larger values. We put the guest between
 * the end of data segment (system break) and this value. We
 * use 32GB as a base to have enough room for the system break
 * to grow. We also have to use MAP parameters that avoid
 * read-only mapping of guest pages.
 */
static void *legacy_s390_alloc(size_t size, uint64_t *align)
{
    void *mem;

    mem = mmap((void *) 0x800000000ULL, size,
               PROT_EXEC|PROT_READ|PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    return mem == MAP_FAILED ? NULL : mem;
}

static uint8_t const *sw_bp_inst;
static uint8_t sw_bp_ilen;

static void determine_sw_breakpoint_instr(void)
{
        /* DIAG 501 is used for sw breakpoints with old kernels */
        static const uint8_t diag_501[] = {0x83, 0x24, 0x05, 0x01};
        /* Instruction 0x0000 is used for sw breakpoints with recent kernels */
        static const uint8_t instr_0x0000[] = {0x00, 0x00};

        if (sw_bp_inst) {
            return;
        }
        if (kvm_vm_enable_cap(kvm_state, KVM_CAP_S390_USER_INSTR0, 0)) {
            sw_bp_inst = diag_501;
            sw_bp_ilen = sizeof(diag_501);
            DPRINTF("KVM: will use 4-byte sw breakpoints.\n");
        } else {
            sw_bp_inst = instr_0x0000;
            sw_bp_ilen = sizeof(instr_0x0000);
            DPRINTF("KVM: will use 2-byte sw breakpoints.\n");
        }
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    determine_sw_breakpoint_instr();

    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn,
                            sw_bp_ilen, 0) ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)sw_bp_inst, sw_bp_ilen, 1)) {
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    uint8_t t[MAX_ILEN];

    if (cpu_memory_rw_debug(cs, bp->pc, t, sw_bp_ilen, 0)) {
        return -EINVAL;
    } else if (memcmp(t, sw_bp_inst, sw_bp_ilen)) {
        return -EINVAL;
    } else if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn,
                                   sw_bp_ilen, 1)) {
        return -EINVAL;
    }

    return 0;
}

static struct kvm_hw_breakpoint *find_hw_breakpoint(target_ulong addr,
                                                    int len, int type)
{
    int n;

    for (n = 0; n < nb_hw_breakpoints; n++) {
        if (hw_breakpoints[n].addr == addr && hw_breakpoints[n].type == type &&
            (hw_breakpoints[n].len == len || len == -1)) {
            return &hw_breakpoints[n];
        }
    }

    return NULL;
}

static int insert_hw_breakpoint(target_ulong addr, int len, int type)
{
    int size;

    if (find_hw_breakpoint(addr, len, type)) {
        return -EEXIST;
    }

    size = (nb_hw_breakpoints + 1) * sizeof(struct kvm_hw_breakpoint);

    if (!hw_breakpoints) {
        nb_hw_breakpoints = 0;
        hw_breakpoints = (struct kvm_hw_breakpoint *)g_try_malloc(size);
    } else {
        hw_breakpoints =
            (struct kvm_hw_breakpoint *)g_try_realloc(hw_breakpoints, size);
    }

    if (!hw_breakpoints) {
        nb_hw_breakpoints = 0;
        return -ENOMEM;
    }

    hw_breakpoints[nb_hw_breakpoints].addr = addr;
    hw_breakpoints[nb_hw_breakpoints].len = len;
    hw_breakpoints[nb_hw_breakpoints].type = type;

    nb_hw_breakpoints++;

    return 0;
}

int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_HW:
        type = KVM_HW_BP;
        break;
    case GDB_WATCHPOINT_WRITE:
        if (len < 1) {
            return -EINVAL;
        }
        type = KVM_HW_WP_WRITE;
        break;
    default:
        return -ENOSYS;
    }
    return insert_hw_breakpoint(addr, len, type);
}

int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    int size;
    struct kvm_hw_breakpoint *bp = find_hw_breakpoint(addr, len, type);

    if (bp == NULL) {
        return -ENOENT;
    }

    nb_hw_breakpoints--;
    if (nb_hw_breakpoints > 0) {
        /*
         * In order to trim the array, move the last element to the position to
         * be removed - if necessary.
         */
        if (bp != &hw_breakpoints[nb_hw_breakpoints]) {
            *bp = hw_breakpoints[nb_hw_breakpoints];
        }
        size = nb_hw_breakpoints * sizeof(struct kvm_hw_breakpoint);
        hw_breakpoints =
             (struct kvm_hw_breakpoint *)g_realloc(hw_breakpoints, size);
    } else {
        g_free(hw_breakpoints);
        hw_breakpoints = NULL;
    }

    return 0;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
    nb_hw_breakpoints = 0;
    g_free(hw_breakpoints);
    hw_breakpoints = NULL;
}

void kvm_arch_update_guest_debug(CPUState *cpu, struct kvm_guest_debug *dbg)
{
    int i;

    if (nb_hw_breakpoints > 0) {
        dbg->arch.nr_hw_bp = nb_hw_breakpoints;
        dbg->arch.hw_bp = hw_breakpoints;

        for (i = 0; i < nb_hw_breakpoints; ++i) {
            hw_breakpoints[i].phys_addr = s390_cpu_get_phys_addr_debug(cpu,
                                                       hw_breakpoints[i].addr);
        }
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
    } else {
        dbg->arch.nr_hw_bp = 0;
        dbg->arch.hw_bp = NULL;
    }
}

void kvm_arch_pre_run(CPUState *cpu, struct kvm_run *run)
{
}

MemTxAttrs kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
    return MEMTXATTRS_UNSPECIFIED;
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return cs->halted;
}

static int s390_kvm_irq_to_interrupt(struct kvm_s390_irq *irq,
                                     struct kvm_s390_interrupt *interrupt)
{
    int r = 0;

    interrupt->type = irq->type;
    switch (irq->type) {
    case KVM_S390_INT_VIRTIO:
        interrupt->parm = irq->u.ext.ext_params;
        /* fall through */
    case KVM_S390_INT_PFAULT_INIT:
    case KVM_S390_INT_PFAULT_DONE:
        interrupt->parm64 = irq->u.ext.ext_params2;
        break;
    case KVM_S390_PROGRAM_INT:
        interrupt->parm = irq->u.pgm.code;
        break;
    case KVM_S390_SIGP_SET_PREFIX:
        interrupt->parm = irq->u.prefix.address;
        break;
    case KVM_S390_INT_SERVICE:
        interrupt->parm = irq->u.ext.ext_params;
        break;
    case KVM_S390_MCHK:
        interrupt->parm = irq->u.mchk.cr14;
        interrupt->parm64 = irq->u.mchk.mcic;
        break;
    case KVM_S390_INT_EXTERNAL_CALL:
        interrupt->parm = irq->u.extcall.code;
        break;
    case KVM_S390_INT_EMERGENCY:
        interrupt->parm = irq->u.emerg.code;
        break;
    case KVM_S390_SIGP_STOP:
    case KVM_S390_RESTART:
        break; /* These types have no parameters */
    case KVM_S390_INT_IO_MIN...KVM_S390_INT_IO_MAX:
        interrupt->parm = irq->u.io.subchannel_id << 16;
        interrupt->parm |= irq->u.io.subchannel_nr;
        interrupt->parm64 = (uint64_t)irq->u.io.io_int_parm << 32;
        interrupt->parm64 |= irq->u.io.io_int_word;
        break;
    default:
        r = -EINVAL;
        break;
    }
    return r;
}

static void inject_vcpu_irq_legacy(CPUState *cs, struct kvm_s390_irq *irq)
{
    struct kvm_s390_interrupt kvmint = {};
    int r;

    r = s390_kvm_irq_to_interrupt(irq, &kvmint);
    if (r < 0) {
        fprintf(stderr, "%s called with bogus interrupt\n", __func__);
        exit(1);
    }

    r = kvm_vcpu_ioctl(cs, KVM_S390_INTERRUPT, &kvmint);
    if (r < 0) {
        fprintf(stderr, "KVM failed to inject interrupt\n");
        exit(1);
    }
}

void kvm_s390_vcpu_interrupt(S390CPU *cpu, struct kvm_s390_irq *irq)
{
    CPUState *cs = CPU(cpu);
    int r;

    if (cap_s390_irq) {
        r = kvm_vcpu_ioctl(cs, KVM_S390_IRQ, irq);
        if (!r) {
            return;
        }
        error_report("KVM failed to inject interrupt %llx", irq->type);
        exit(1);
    }

    inject_vcpu_irq_legacy(cs, irq);
}

static void __kvm_s390_floating_interrupt(struct kvm_s390_irq *irq)
{
    struct kvm_s390_interrupt kvmint = {};
    int r;

    r = s390_kvm_irq_to_interrupt(irq, &kvmint);
    if (r < 0) {
        fprintf(stderr, "%s called with bogus interrupt\n", __func__);
        exit(1);
    }

    r = kvm_vm_ioctl(kvm_state, KVM_S390_INTERRUPT, &kvmint);
    if (r < 0) {
        fprintf(stderr, "KVM failed to inject interrupt\n");
        exit(1);
    }
}

void kvm_s390_floating_interrupt(struct kvm_s390_irq *irq)
{
    static bool use_flic = true;
    int r;

    if (use_flic) {
        r = kvm_s390_inject_flic(irq);
        if (r == -ENOSYS) {
            use_flic = false;
        }
        if (!r) {
            return;
        }
    }
    __kvm_s390_floating_interrupt(irq);
}

void kvm_s390_service_interrupt(uint32_t parm)
{
    struct kvm_s390_irq irq = {
        .type = KVM_S390_INT_SERVICE,
        .u.ext.ext_params = parm,
    };

    kvm_s390_floating_interrupt(&irq);
}

void kvm_s390_program_interrupt(S390CPU *cpu, uint16_t code)
{
    struct kvm_s390_irq irq = {
        .type = KVM_S390_PROGRAM_INT,
        .u.pgm.code = code,
    };

    kvm_s390_vcpu_interrupt(cpu, &irq);
}

void kvm_s390_access_exception(S390CPU *cpu, uint16_t code, uint64_t te_code)
{
    struct kvm_s390_irq irq = {
        .type = KVM_S390_PROGRAM_INT,
        .u.pgm.code = code,
        .u.pgm.trans_exc_code = te_code,
        .u.pgm.exc_access_id = te_code & 3,
    };

    kvm_s390_vcpu_interrupt(cpu, &irq);
}

static int kvm_sclp_service_call(S390CPU *cpu, struct kvm_run *run,
                                 uint16_t ipbh0)
{
    CPUS390XState *env = &cpu->env;
    uint64_t sccb;
    uint32_t code;
    int r = 0;

    cpu_synchronize_state(CPU(cpu));
    sccb = env->regs[ipbh0 & 0xf];
    code = env->regs[(ipbh0 & 0xf0) >> 4];

    r = sclp_service_call(env, sccb, code);
    if (r < 0) {
        kvm_s390_program_interrupt(cpu, -r);
    } else {
        setcc(cpu, r);
    }

    return 0;
}

static int handle_b2(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    CPUS390XState *env = &cpu->env;
    int rc = 0;
    uint16_t ipbh0 = (run->s390_sieic.ipb & 0xffff0000) >> 16;

    cpu_synchronize_state(CPU(cpu));

    switch (ipa1) {
    case PRIV_B2_XSCH:
        ioinst_handle_xsch(cpu, env->regs[1]);
        break;
    case PRIV_B2_CSCH:
        ioinst_handle_csch(cpu, env->regs[1]);
        break;
    case PRIV_B2_HSCH:
        ioinst_handle_hsch(cpu, env->regs[1]);
        break;
    case PRIV_B2_MSCH:
        ioinst_handle_msch(cpu, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_B2_SSCH:
        ioinst_handle_ssch(cpu, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_B2_STCRW:
        ioinst_handle_stcrw(cpu, run->s390_sieic.ipb);
        break;
    case PRIV_B2_STSCH:
        ioinst_handle_stsch(cpu, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_B2_TSCH:
        /* We should only get tsch via KVM_EXIT_S390_TSCH. */
        fprintf(stderr, "Spurious tsch intercept\n");
        break;
    case PRIV_B2_CHSC:
        ioinst_handle_chsc(cpu, run->s390_sieic.ipb);
        break;
    case PRIV_B2_TPI:
        /* This should have been handled by kvm already. */
        fprintf(stderr, "Spurious tpi intercept\n");
        break;
    case PRIV_B2_SCHM:
        ioinst_handle_schm(cpu, env->regs[1], env->regs[2],
                           run->s390_sieic.ipb);
        break;
    case PRIV_B2_RSCH:
        ioinst_handle_rsch(cpu, env->regs[1]);
        break;
    case PRIV_B2_RCHP:
        ioinst_handle_rchp(cpu, env->regs[1]);
        break;
    case PRIV_B2_STCPS:
        /* We do not provide this instruction, it is suppressed. */
        break;
    case PRIV_B2_SAL:
        ioinst_handle_sal(cpu, env->regs[1]);
        break;
    case PRIV_B2_SIGA:
        /* Not provided, set CC = 3 for subchannel not operational */
        setcc(cpu, 3);
        break;
    case PRIV_B2_SCLP_CALL:
        rc = kvm_sclp_service_call(cpu, run, ipbh0);
        break;
    default:
        rc = -1;
        DPRINTF("KVM: unhandled PRIV: 0xb2%x\n", ipa1);
        break;
    }

    return rc;
}

static uint64_t get_base_disp_rxy(S390CPU *cpu, struct kvm_run *run,
                                  uint8_t *ar)
{
    CPUS390XState *env = &cpu->env;
    uint32_t x2 = (run->s390_sieic.ipa & 0x000f);
    uint32_t base2 = run->s390_sieic.ipb >> 28;
    uint32_t disp2 = ((run->s390_sieic.ipb & 0x0fff0000) >> 16) +
                     ((run->s390_sieic.ipb & 0xff00) << 4);

    if (disp2 & 0x80000) {
        disp2 += 0xfff00000;
    }
    if (ar) {
        *ar = base2;
    }

    return (base2 ? env->regs[base2] : 0) +
           (x2 ? env->regs[x2] : 0) + (long)(int)disp2;
}

static uint64_t get_base_disp_rsy(S390CPU *cpu, struct kvm_run *run,
                                  uint8_t *ar)
{
    CPUS390XState *env = &cpu->env;
    uint32_t base2 = run->s390_sieic.ipb >> 28;
    uint32_t disp2 = ((run->s390_sieic.ipb & 0x0fff0000) >> 16) +
                     ((run->s390_sieic.ipb & 0xff00) << 4);

    if (disp2 & 0x80000) {
        disp2 += 0xfff00000;
    }
    if (ar) {
        *ar = base2;
    }

    return (base2 ? env->regs[base2] : 0) + (long)(int)disp2;
}

static int kvm_clp_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r2 = (run->s390_sieic.ipb & 0x000f0000) >> 16;

    return clp_service_call(cpu, r2);
}

static int kvm_pcilg_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipb & 0x00f00000) >> 20;
    uint8_t r2 = (run->s390_sieic.ipb & 0x000f0000) >> 16;

    return pcilg_service_call(cpu, r1, r2);
}

static int kvm_pcistg_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipb & 0x00f00000) >> 20;
    uint8_t r2 = (run->s390_sieic.ipb & 0x000f0000) >> 16;

    return pcistg_service_call(cpu, r1, r2);
}

static int kvm_stpcifc_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint64_t fiba;
    uint8_t ar;

    cpu_synchronize_state(CPU(cpu));
    fiba = get_base_disp_rxy(cpu, run, &ar);

    return stpcifc_service_call(cpu, r1, fiba, ar);
}

static int kvm_sic_service_call(S390CPU *cpu, struct kvm_run *run)
{
    CPUS390XState *env = &cpu->env;
    uint8_t r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint8_t r3 = run->s390_sieic.ipa & 0x000f;
    uint8_t isc;
    uint16_t mode;
    int r;

    cpu_synchronize_state(CPU(cpu));
    mode = env->regs[r1] & 0xffff;
    isc = (env->regs[r3] >> 27) & 0x7;
    r = css_do_sic(env, isc, mode);
    if (r) {
        kvm_s390_program_interrupt(cpu, -r);
    }

    return 0;
}

static int kvm_rpcit_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipb & 0x00f00000) >> 20;
    uint8_t r2 = (run->s390_sieic.ipb & 0x000f0000) >> 16;

    return rpcit_service_call(cpu, r1, r2);
}

static int kvm_pcistb_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint8_t r3 = run->s390_sieic.ipa & 0x000f;
    uint64_t gaddr;
    uint8_t ar;

    cpu_synchronize_state(CPU(cpu));
    gaddr = get_base_disp_rsy(cpu, run, &ar);

    return pcistb_service_call(cpu, r1, r3, gaddr, ar);
}

static int kvm_mpcifc_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint64_t fiba;
    uint8_t ar;

    cpu_synchronize_state(CPU(cpu));
    fiba = get_base_disp_rxy(cpu, run, &ar);

    return mpcifc_service_call(cpu, r1, fiba, ar);
}

static int handle_b9(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    int r = 0;

    switch (ipa1) {
    case PRIV_B9_CLP:
        r = kvm_clp_service_call(cpu, run);
        break;
    case PRIV_B9_PCISTG:
        r = kvm_pcistg_service_call(cpu, run);
        break;
    case PRIV_B9_PCILG:
        r = kvm_pcilg_service_call(cpu, run);
        break;
    case PRIV_B9_RPCIT:
        r = kvm_rpcit_service_call(cpu, run);
        break;
    case PRIV_B9_EQBS:
        /* just inject exception */
        r = -1;
        break;
    default:
        r = -1;
        DPRINTF("KVM: unhandled PRIV: 0xb9%x\n", ipa1);
        break;
    }

    return r;
}

static int handle_eb(S390CPU *cpu, struct kvm_run *run, uint8_t ipbl)
{
    int r = 0;

    switch (ipbl) {
    case PRIV_EB_PCISTB:
        r = kvm_pcistb_service_call(cpu, run);
        break;
    case PRIV_EB_SIC:
        r = kvm_sic_service_call(cpu, run);
        break;
    case PRIV_EB_SQBS:
        /* just inject exception */
        r = -1;
        break;
    default:
        r = -1;
        DPRINTF("KVM: unhandled PRIV: 0xeb%x\n", ipbl);
        break;
    }

    return r;
}

static int handle_e3(S390CPU *cpu, struct kvm_run *run, uint8_t ipbl)
{
    int r = 0;

    switch (ipbl) {
    case PRIV_E3_MPCIFC:
        r = kvm_mpcifc_service_call(cpu, run);
        break;
    case PRIV_E3_STPCIFC:
        r = kvm_stpcifc_service_call(cpu, run);
        break;
    default:
        r = -1;
        DPRINTF("KVM: unhandled PRIV: 0xe3%x\n", ipbl);
        break;
    }

    return r;
}

static int handle_hypercall(S390CPU *cpu, struct kvm_run *run)
{
    CPUS390XState *env = &cpu->env;
    int ret;

    cpu_synchronize_state(CPU(cpu));
    ret = s390_virtio_hypercall(env);
    if (ret == -EINVAL) {
        kvm_s390_program_interrupt(cpu, PGM_SPECIFICATION);
        return 0;
    }

    return ret;
}

static void kvm_handle_diag_288(S390CPU *cpu, struct kvm_run *run)
{
    uint64_t r1, r3;
    int rc;

    cpu_synchronize_state(CPU(cpu));
    r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    r3 = run->s390_sieic.ipa & 0x000f;
    rc = handle_diag_288(&cpu->env, r1, r3);
    if (rc) {
        kvm_s390_program_interrupt(cpu, PGM_SPECIFICATION);
    }
}

static void kvm_handle_diag_308(S390CPU *cpu, struct kvm_run *run)
{
    uint64_t r1, r3;

    cpu_synchronize_state(CPU(cpu));
    r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    r3 = run->s390_sieic.ipa & 0x000f;
    handle_diag_308(&cpu->env, r1, r3);
}

static int handle_sw_breakpoint(S390CPU *cpu, struct kvm_run *run)
{
    CPUS390XState *env = &cpu->env;
    unsigned long pc;

    cpu_synchronize_state(CPU(cpu));

    pc = env->psw.addr - sw_bp_ilen;
    if (kvm_find_sw_breakpoint(CPU(cpu), pc)) {
        env->psw.addr = pc;
        return EXCP_DEBUG;
    }

    return -ENOENT;
}

#define DIAG_KVM_CODE_MASK 0x000000000000ffff

static int handle_diag(S390CPU *cpu, struct kvm_run *run, uint32_t ipb)
{
    int r = 0;
    uint16_t func_code;

    /*
     * For any diagnose call we support, bits 48-63 of the resulting
     * address specify the function code; the remainder is ignored.
     */
    func_code = decode_basedisp_rs(&cpu->env, ipb, NULL) & DIAG_KVM_CODE_MASK;
    switch (func_code) {
    case DIAG_TIMEREVENT:
        kvm_handle_diag_288(cpu, run);
        break;
    case DIAG_IPL:
        kvm_handle_diag_308(cpu, run);
        break;
    case DIAG_KVM_HYPERCALL:
        r = handle_hypercall(cpu, run);
        break;
    case DIAG_KVM_BREAKPOINT:
        r = handle_sw_breakpoint(cpu, run);
        break;
    default:
        DPRINTF("KVM: unknown DIAG: 0x%x\n", func_code);
        kvm_s390_program_interrupt(cpu, PGM_SPECIFICATION);
        break;
    }

    return r;
}

typedef struct SigpInfo {
    uint64_t param;
    int cc;
    uint64_t *status_reg;
} SigpInfo;

static void set_sigp_status(SigpInfo *si, uint64_t status)
{
    *si->status_reg &= 0xffffffff00000000ULL;
    *si->status_reg |= status;
    si->cc = SIGP_CC_STATUS_STORED;
}

static void sigp_start(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;

    if (s390_cpu_get_state(cpu) != CPU_STATE_STOPPED) {
        si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
        return;
    }

    s390_cpu_set_state(CPU_STATE_OPERATING, cpu);
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_stop(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    struct kvm_s390_irq irq = {
        .type = KVM_S390_SIGP_STOP,
    };

    if (s390_cpu_get_state(cpu) != CPU_STATE_OPERATING) {
        si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
        return;
    }

    /* disabled wait - sleeping in user space */
    if (cs->halted) {
        s390_cpu_set_state(CPU_STATE_STOPPED, cpu);
    } else {
        /* execute the stop function */
        cpu->env.sigp_order = SIGP_STOP;
        kvm_s390_vcpu_interrupt(cpu, &irq);
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

#define ADTL_GS_OFFSET   1024 /* offset of GS data in adtl save area */
#define ADTL_GS_MIN_SIZE 2048 /* minimal size of adtl save area for GS */
static int do_store_adtl_status(S390CPU *cpu, hwaddr addr, hwaddr len)
{
    hwaddr save = len;
    void *mem;

    mem = cpu_physical_memory_map(addr, &save, 1);
    if (!mem) {
        return -EFAULT;
    }
    if (save != len) {
        cpu_physical_memory_unmap(mem, len, 1, 0);
        return -EFAULT;
    }

    if (s390_has_feat(S390_FEAT_VECTOR)) {
        memcpy(mem, &cpu->env.vregs, 512);
    }
    if (s390_has_feat(S390_FEAT_GUARDED_STORAGE) && len >= ADTL_GS_MIN_SIZE) {
        memcpy(mem + ADTL_GS_OFFSET, &cpu->env.gscb, 32);
    }

    cpu_physical_memory_unmap(mem, len, 1, len);

    return 0;
}

#define KVM_S390_STORE_STATUS_DEF_ADDR offsetof(LowCore, floating_pt_save_area)
#define SAVE_AREA_SIZE 512
static int kvm_s390_store_status(S390CPU *cpu, hwaddr addr, bool store_arch)
{
    static const uint8_t ar_id = 1;
    uint64_t ckc = cpu->env.ckc >> 8;
    void *mem;
    int i;
    hwaddr len = SAVE_AREA_SIZE;

    mem = cpu_physical_memory_map(addr, &len, 1);
    if (!mem) {
        return -EFAULT;
    }
    if (len != SAVE_AREA_SIZE) {
        cpu_physical_memory_unmap(mem, len, 1, 0);
        return -EFAULT;
    }

    if (store_arch) {
        cpu_physical_memory_write(offsetof(LowCore, ar_access_id), &ar_id, 1);
    }
    for (i = 0; i < 16; ++i) {
        *((uint64_t *)mem + i) = get_freg(&cpu->env, i)->ll;
    }
    memcpy(mem + 128, &cpu->env.regs, 128);
    memcpy(mem + 256, &cpu->env.psw, 16);
    memcpy(mem + 280, &cpu->env.psa, 4);
    memcpy(mem + 284, &cpu->env.fpc, 4);
    memcpy(mem + 292, &cpu->env.todpr, 4);
    memcpy(mem + 296, &cpu->env.cputm, 8);
    memcpy(mem + 304, &ckc, 8);
    memcpy(mem + 320, &cpu->env.aregs, 64);
    memcpy(mem + 384, &cpu->env.cregs, 128);

    cpu_physical_memory_unmap(mem, len, 1, len);

    return 0;
}

static void sigp_stop_and_store_status(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    struct kvm_s390_irq irq = {
        .type = KVM_S390_SIGP_STOP,
    };

    /* disabled wait - sleeping in user space */
    if (s390_cpu_get_state(cpu) == CPU_STATE_OPERATING && cs->halted) {
        s390_cpu_set_state(CPU_STATE_STOPPED, cpu);
    }

    switch (s390_cpu_get_state(cpu)) {
    case CPU_STATE_OPERATING:
        cpu->env.sigp_order = SIGP_STOP_STORE_STATUS;
        kvm_s390_vcpu_interrupt(cpu, &irq);
        /* store will be performed when handling the stop intercept */
        break;
    case CPU_STATE_STOPPED:
        /* already stopped, just store the status */
        cpu_synchronize_state(cs);
        kvm_s390_store_status(cpu, KVM_S390_STORE_STATUS_DEF_ADDR, true);
        break;
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_store_status_at_address(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    uint32_t address = si->param & 0x7ffffe00u;

    /* cpu has to be stopped */
    if (s390_cpu_get_state(cpu) != CPU_STATE_STOPPED) {
        set_sigp_status(si, SIGP_STAT_INCORRECT_STATE);
        return;
    }

    cpu_synchronize_state(cs);

    if (kvm_s390_store_status(cpu, address, false)) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

#define ADTL_SAVE_LC_MASK  0xfUL
static void sigp_store_adtl_status(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    uint8_t lc = si->param & ADTL_SAVE_LC_MASK;
    hwaddr addr = si->param & ~ADTL_SAVE_LC_MASK;
    hwaddr len = 1UL << (lc ? lc : 10);

    if (!s390_has_feat(S390_FEAT_VECTOR) &&
        !s390_has_feat(S390_FEAT_GUARDED_STORAGE)) {
        set_sigp_status(si, SIGP_STAT_INVALID_ORDER);
        return;
    }

    /* cpu has to be stopped */
    if (s390_cpu_get_state(cpu) != CPU_STATE_STOPPED) {
        set_sigp_status(si, SIGP_STAT_INCORRECT_STATE);
        return;
    }

    /* address must be aligned to length */
    if (addr & (len - 1)) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }

    /* no GS: only lc == 0 is valid */
    if (!s390_has_feat(S390_FEAT_GUARDED_STORAGE) &&
        lc != 0) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }

    /* GS: 0, 10, 11, 12 are valid */
    if (s390_has_feat(S390_FEAT_GUARDED_STORAGE) &&
        lc != 0 &&
        lc != 10 &&
        lc != 11 &&
        lc != 12) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }

    cpu_synchronize_state(cs);

    if (do_store_adtl_status(cpu, addr, len)) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_restart(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    struct kvm_s390_irq irq = {
        .type = KVM_S390_RESTART,
    };

    switch (s390_cpu_get_state(cpu)) {
    case CPU_STATE_STOPPED:
        /* the restart irq has to be delivered prior to any other pending irq */
        cpu_synchronize_state(cs);
        do_restart_interrupt(&cpu->env);
        s390_cpu_set_state(CPU_STATE_OPERATING, cpu);
        break;
    case CPU_STATE_OPERATING:
        kvm_s390_vcpu_interrupt(cpu, &irq);
        break;
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

int kvm_s390_cpu_restart(S390CPU *cpu)
{
    SigpInfo si = {};

    run_on_cpu(CPU(cpu), sigp_restart, RUN_ON_CPU_HOST_PTR(&si));
    DPRINTF("DONE: KVM cpu restart: %p\n", &cpu->env);
    return 0;
}

static void sigp_initial_cpu_reset(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    SigpInfo *si = arg.host_ptr;

    cpu_synchronize_state(cs);
    scc->initial_cpu_reset(cs);
    cpu_synchronize_post_reset(cs);
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_cpu_reset(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    SigpInfo *si = arg.host_ptr;

    cpu_synchronize_state(cs);
    scc->cpu_reset(cs);
    cpu_synchronize_post_reset(cs);
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_set_prefix(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    uint32_t addr = si->param & 0x7fffe000u;

    cpu_synchronize_state(cs);

    if (!address_space_access_valid(&address_space_memory, addr,
                                    sizeof(struct LowCore), false)) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }

    /* cpu has to be stopped */
    if (s390_cpu_get_state(cpu) != CPU_STATE_STOPPED) {
        set_sigp_status(si, SIGP_STAT_INCORRECT_STATE);
        return;
    }

    cpu->env.psa = addr;
    cpu_synchronize_post_init(cs);
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static int handle_sigp_single_dst(S390CPU *dst_cpu, uint8_t order,
                                  uint64_t param, uint64_t *status_reg)
{
    SigpInfo si = {
        .param = param,
        .status_reg = status_reg,
    };

    /* cpu available? */
    if (dst_cpu == NULL) {
        return SIGP_CC_NOT_OPERATIONAL;
    }

    /* only resets can break pending orders */
    if (dst_cpu->env.sigp_order != 0 &&
        order != SIGP_CPU_RESET &&
        order != SIGP_INITIAL_CPU_RESET) {
        return SIGP_CC_BUSY;
    }

    switch (order) {
    case SIGP_START:
        run_on_cpu(CPU(dst_cpu), sigp_start, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_STOP:
        run_on_cpu(CPU(dst_cpu), sigp_stop, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_RESTART:
        run_on_cpu(CPU(dst_cpu), sigp_restart, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_STOP_STORE_STATUS:
        run_on_cpu(CPU(dst_cpu), sigp_stop_and_store_status, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_STORE_STATUS_ADDR:
        run_on_cpu(CPU(dst_cpu), sigp_store_status_at_address, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_STORE_ADTL_STATUS:
        run_on_cpu(CPU(dst_cpu), sigp_store_adtl_status, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_SET_PREFIX:
        run_on_cpu(CPU(dst_cpu), sigp_set_prefix, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_INITIAL_CPU_RESET:
        run_on_cpu(CPU(dst_cpu), sigp_initial_cpu_reset, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_CPU_RESET:
        run_on_cpu(CPU(dst_cpu), sigp_cpu_reset, RUN_ON_CPU_HOST_PTR(&si));
        break;
    default:
        DPRINTF("KVM: unknown SIGP: 0x%x\n", order);
        set_sigp_status(&si, SIGP_STAT_INVALID_ORDER);
    }

    return si.cc;
}

static int sigp_set_architecture(S390CPU *cpu, uint32_t param,
                                 uint64_t *status_reg)
{
    CPUState *cur_cs;
    S390CPU *cur_cpu;
    bool all_stopped = true;

    CPU_FOREACH(cur_cs) {
        cur_cpu = S390_CPU(cur_cs);

        if (cur_cpu == cpu) {
            continue;
        }
        if (s390_cpu_get_state(cur_cpu) != CPU_STATE_STOPPED) {
            all_stopped = false;
        }
    }

    *status_reg &= 0xffffffff00000000ULL;

    /* Reject set arch order, with czam we're always in z/Arch mode. */
    *status_reg |= (all_stopped ? SIGP_STAT_INVALID_PARAMETER :
                    SIGP_STAT_INCORRECT_STATE);
    return SIGP_CC_STATUS_STORED;
}

static int handle_sigp(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    CPUS390XState *env = &cpu->env;
    const uint8_t r1 = ipa1 >> 4;
    const uint8_t r3 = ipa1 & 0x0f;
    int ret;
    uint8_t order;
    uint64_t *status_reg;
    uint64_t param;
    S390CPU *dst_cpu = NULL;

    cpu_synchronize_state(CPU(cpu));

    /* get order code */
    order = decode_basedisp_rs(env, run->s390_sieic.ipb, NULL)
        & SIGP_ORDER_MASK;
    status_reg = &env->regs[r1];
    param = (r1 % 2) ? env->regs[r1] : env->regs[r1 + 1];

    if (qemu_mutex_trylock(&qemu_sigp_mutex)) {
        ret = SIGP_CC_BUSY;
        goto out;
    }

    switch (order) {
    case SIGP_SET_ARCH:
        ret = sigp_set_architecture(cpu, param, status_reg);
        break;
    default:
        /* all other sigp orders target a single vcpu */
        dst_cpu = s390_cpu_addr2state(env->regs[r3]);
        ret = handle_sigp_single_dst(dst_cpu, order, param, status_reg);
    }
    qemu_mutex_unlock(&qemu_sigp_mutex);

out:
    trace_kvm_sigp_finished(order, CPU(cpu)->cpu_index,
                            dst_cpu ? CPU(dst_cpu)->cpu_index : -1, ret);

    if (ret >= 0) {
        setcc(cpu, ret);
        return 0;
    }

    return ret;
}

static int handle_instruction(S390CPU *cpu, struct kvm_run *run)
{
    unsigned int ipa0 = (run->s390_sieic.ipa & 0xff00);
    uint8_t ipa1 = run->s390_sieic.ipa & 0x00ff;
    int r = -1;

    DPRINTF("handle_instruction 0x%x 0x%x\n",
            run->s390_sieic.ipa, run->s390_sieic.ipb);
    switch (ipa0) {
    case IPA0_B2:
        r = handle_b2(cpu, run, ipa1);
        break;
    case IPA0_B9:
        r = handle_b9(cpu, run, ipa1);
        break;
    case IPA0_EB:
        r = handle_eb(cpu, run, run->s390_sieic.ipb & 0xff);
        break;
    case IPA0_E3:
        r = handle_e3(cpu, run, run->s390_sieic.ipb & 0xff);
        break;
    case IPA0_DIAG:
        r = handle_diag(cpu, run, run->s390_sieic.ipb);
        break;
    case IPA0_SIGP:
        r = handle_sigp(cpu, run, ipa1);
        break;
    }

    if (r < 0) {
        r = 0;
        kvm_s390_program_interrupt(cpu, PGM_OPERATION);
    }

    return r;
}

static bool is_special_wait_psw(CPUState *cs)
{
    /* signal quiesce */
    return cs->kvm_run->psw_addr == 0xfffUL;
}

static void unmanageable_intercept(S390CPU *cpu, const char *str, int pswoffset)
{
    CPUState *cs = CPU(cpu);

    error_report("Unmanageable %s! CPU%i new PSW: 0x%016lx:%016lx",
                 str, cs->cpu_index, ldq_phys(cs->as, cpu->env.psa + pswoffset),
                 ldq_phys(cs->as, cpu->env.psa + pswoffset + 8));
    s390_cpu_halt(cpu);
    qemu_system_guest_panicked(NULL);
}

/* try to detect pgm check loops */
static int handle_oper_loop(S390CPU *cpu, struct kvm_run *run)
{
    CPUState *cs = CPU(cpu);
    PSW oldpsw, newpsw;

    cpu_synchronize_state(cs);
    newpsw.mask = ldq_phys(cs->as, cpu->env.psa +
                           offsetof(LowCore, program_new_psw));
    newpsw.addr = ldq_phys(cs->as, cpu->env.psa +
                           offsetof(LowCore, program_new_psw) + 8);
    oldpsw.mask  = run->psw_mask;
    oldpsw.addr  = run->psw_addr;
    /*
     * Avoid endless loops of operation exceptions, if the pgm new
     * PSW will cause a new operation exception.
     * The heuristic checks if the pgm new psw is within 6 bytes before
     * the faulting psw address (with same DAT, AS settings) and the
     * new psw is not a wait psw and the fault was not triggered by
     * problem state. In that case go into crashed state.
     */

    if (oldpsw.addr - newpsw.addr <= 6 &&
        !(newpsw.mask & PSW_MASK_WAIT) &&
        !(oldpsw.mask & PSW_MASK_PSTATE) &&
        (newpsw.mask & PSW_MASK_ASC) == (oldpsw.mask & PSW_MASK_ASC) &&
        (newpsw.mask & PSW_MASK_DAT) == (oldpsw.mask & PSW_MASK_DAT)) {
        unmanageable_intercept(cpu, "operation exception loop",
                               offsetof(LowCore, program_new_psw));
        return EXCP_HALTED;
    }
    return 0;
}

static int handle_intercept(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;
    int icpt_code = run->s390_sieic.icptcode;
    int r = 0;

    DPRINTF("intercept: 0x%x (at 0x%lx)\n", icpt_code,
            (long)cs->kvm_run->psw_addr);
    switch (icpt_code) {
        case ICPT_INSTRUCTION:
            r = handle_instruction(cpu, run);
            break;
        case ICPT_PROGRAM:
            unmanageable_intercept(cpu, "program interrupt",
                                   offsetof(LowCore, program_new_psw));
            r = EXCP_HALTED;
            break;
        case ICPT_EXT_INT:
            unmanageable_intercept(cpu, "external interrupt",
                                   offsetof(LowCore, external_new_psw));
            r = EXCP_HALTED;
            break;
        case ICPT_WAITPSW:
            /* disabled wait, since enabled wait is handled in kernel */
            cpu_synchronize_state(cs);
            if (s390_cpu_halt(cpu) == 0) {
                if (is_special_wait_psw(cs)) {
                    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
                } else {
                    qemu_system_guest_panicked(NULL);
                }
            }
            r = EXCP_HALTED;
            break;
        case ICPT_CPU_STOP:
            if (s390_cpu_set_state(CPU_STATE_STOPPED, cpu) == 0) {
                qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            }
            if (cpu->env.sigp_order == SIGP_STOP_STORE_STATUS) {
                kvm_s390_store_status(cpu, KVM_S390_STORE_STATUS_DEF_ADDR,
                                      true);
            }
            cpu->env.sigp_order = 0;
            r = EXCP_HALTED;
            break;
        case ICPT_OPEREXC:
            /* check for break points */
            r = handle_sw_breakpoint(cpu, run);
            if (r == -ENOENT) {
                /* Then check for potential pgm check loops */
                r = handle_oper_loop(cpu, run);
                if (r == 0) {
                    kvm_s390_program_interrupt(cpu, PGM_OPERATION);
                }
            }
            break;
        case ICPT_SOFT_INTERCEPT:
            fprintf(stderr, "KVM unimplemented icpt SOFT\n");
            exit(1);
            break;
        case ICPT_IO:
            fprintf(stderr, "KVM unimplemented icpt IO\n");
            exit(1);
            break;
        default:
            fprintf(stderr, "Unknown intercept code: %d\n", icpt_code);
            exit(1);
            break;
    }

    return r;
}

static int handle_tsch(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;
    int ret;

    cpu_synchronize_state(cs);

    ret = ioinst_handle_tsch(cpu, cpu->env.regs[1], run->s390_tsch.ipb);
    if (ret < 0) {
        /*
         * Failure.
         * If an I/O interrupt had been dequeued, we have to reinject it.
         */
        if (run->s390_tsch.dequeued) {
            kvm_s390_io_interrupt(run->s390_tsch.subchannel_id,
                                  run->s390_tsch.subchannel_nr,
                                  run->s390_tsch.io_int_parm,
                                  run->s390_tsch.io_int_word);
        }
        ret = 0;
    }
    return ret;
}

static void insert_stsi_3_2_2(S390CPU *cpu, __u64 addr, uint8_t ar)
{
    struct sysib_322 sysib;
    int del;

    if (s390_cpu_virt_mem_read(cpu, addr, ar, &sysib, sizeof(sysib))) {
        return;
    }
    /* Shift the stack of Extended Names to prepare for our own data */
    memmove(&sysib.ext_names[1], &sysib.ext_names[0],
            sizeof(sysib.ext_names[0]) * (sysib.count - 1));
    /* First virt level, that doesn't provide Ext Names delimits stack. It is
     * assumed it's not capable of managing Extended Names for lower levels.
     */
    for (del = 1; del < sysib.count; del++) {
        if (!sysib.vm[del].ext_name_encoding || !sysib.ext_names[del][0]) {
            break;
        }
    }
    if (del < sysib.count) {
        memset(sysib.ext_names[del], 0,
               sizeof(sysib.ext_names[0]) * (sysib.count - del));
    }
    /* Insert short machine name in EBCDIC, padded with blanks */
    if (qemu_name) {
        memset(sysib.vm[0].name, 0x40, sizeof(sysib.vm[0].name));
        ebcdic_put(sysib.vm[0].name, qemu_name, MIN(sizeof(sysib.vm[0].name),
                                                    strlen(qemu_name)));
    }
    sysib.vm[0].ext_name_encoding = 2; /* 2 = UTF-8 */
    memset(sysib.ext_names[0], 0, sizeof(sysib.ext_names[0]));
    /* If hypervisor specifies zero Extended Name in STSI322 SYSIB, it's
     * considered by s390 as not capable of providing any Extended Name.
     * Therefore if no name was specified on qemu invocation, we go with the
     * same "KVMguest" default, which KVM has filled into short name field.
     */
    if (qemu_name) {
        strncpy((char *)sysib.ext_names[0], qemu_name,
                sizeof(sysib.ext_names[0]));
    } else {
        strcpy((char *)sysib.ext_names[0], "KVMguest");
    }
    /* Insert UUID */
    memcpy(sysib.vm[0].uuid, &qemu_uuid, sizeof(sysib.vm[0].uuid));

    s390_cpu_virt_mem_write(cpu, addr, ar, &sysib, sizeof(sysib));
}

static int handle_stsi(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;

    switch (run->s390_stsi.fc) {
    case 3:
        if (run->s390_stsi.sel1 != 2 || run->s390_stsi.sel2 != 2) {
            return 0;
        }
        /* Only sysib 3.2.2 needs post-handling for now. */
        insert_stsi_3_2_2(cpu, run->s390_stsi.addr, run->s390_stsi.ar);
        return 0;
    default:
        return 0;
    }
}

static int kvm_arch_handle_debug_exit(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;

    int ret = 0;
    struct kvm_debug_exit_arch *arch_info = &run->debug.arch;

    switch (arch_info->type) {
    case KVM_HW_WP_WRITE:
        if (find_hw_breakpoint(arch_info->addr, -1, arch_info->type)) {
            cs->watchpoint_hit = &hw_watchpoint;
            hw_watchpoint.vaddr = arch_info->addr;
            hw_watchpoint.flags = BP_MEM_WRITE;
            ret = EXCP_DEBUG;
        }
        break;
    case KVM_HW_BP:
        if (find_hw_breakpoint(arch_info->addr, -1, arch_info->type)) {
            ret = EXCP_DEBUG;
        }
        break;
    case KVM_SINGLESTEP:
        if (cs->singlestep_enabled) {
            ret = EXCP_DEBUG;
        }
        break;
    default:
        ret = -ENOSYS;
    }

    return ret;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    S390CPU *cpu = S390_CPU(cs);
    int ret = 0;

    qemu_mutex_lock_iothread();

    switch (run->exit_reason) {
        case KVM_EXIT_S390_SIEIC:
            ret = handle_intercept(cpu);
            break;
        case KVM_EXIT_S390_RESET:
            s390_reipl_request();
            break;
        case KVM_EXIT_S390_TSCH:
            ret = handle_tsch(cpu);
            break;
        case KVM_EXIT_S390_STSI:
            ret = handle_stsi(cpu);
            break;
        case KVM_EXIT_DEBUG:
            ret = kvm_arch_handle_debug_exit(cpu);
            break;
        default:
            fprintf(stderr, "Unknown KVM exit: %d\n", run->exit_reason);
            break;
    }
    qemu_mutex_unlock_iothread();

    if (ret == 0) {
        ret = EXCP_INTERRUPT;
    }
    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cpu)
{
    return true;
}

void kvm_s390_io_interrupt(uint16_t subchannel_id,
                           uint16_t subchannel_nr, uint32_t io_int_parm,
                           uint32_t io_int_word)
{
    struct kvm_s390_irq irq = {
        .u.io.subchannel_id = subchannel_id,
        .u.io.subchannel_nr = subchannel_nr,
        .u.io.io_int_parm = io_int_parm,
        .u.io.io_int_word = io_int_word,
    };

    if (io_int_word & IO_INT_WORD_AI) {
        irq.type = KVM_S390_INT_IO(1, 0, 0, 0);
    } else {
        irq.type = KVM_S390_INT_IO(0, (subchannel_id & 0xff00) >> 8,
                                      (subchannel_id & 0x0006),
                                      subchannel_nr);
    }
    kvm_s390_floating_interrupt(&irq);
}

static uint64_t build_channel_report_mcic(void)
{
    uint64_t mcic;

    /* subclass: indicate channel report pending */
    mcic = MCIC_SC_CP |
    /* subclass modifiers: none */
    /* storage errors: none */
    /* validity bits: no damage */
        MCIC_VB_WP | MCIC_VB_MS | MCIC_VB_PM | MCIC_VB_IA | MCIC_VB_FP |
        MCIC_VB_GR | MCIC_VB_CR | MCIC_VB_ST | MCIC_VB_AR | MCIC_VB_PR |
        MCIC_VB_FC | MCIC_VB_CT | MCIC_VB_CC;
    if (s390_has_feat(S390_FEAT_VECTOR)) {
        mcic |= MCIC_VB_VR;
    }
    if (s390_has_feat(S390_FEAT_GUARDED_STORAGE)) {
        mcic |= MCIC_VB_GS;
    }
    return mcic;
}

void kvm_s390_crw_mchk(void)
{
    struct kvm_s390_irq irq = {
        .type = KVM_S390_MCHK,
        .u.mchk.cr14 = 1 << 28,
        .u.mchk.mcic = build_channel_report_mcic(),
    };
    kvm_s390_floating_interrupt(&irq);
}

void kvm_s390_enable_css_support(S390CPU *cpu)
{
    int r;

    /* Activate host kernel channel subsystem support. */
    r = kvm_vcpu_enable_cap(CPU(cpu), KVM_CAP_S390_CSS_SUPPORT, 0);
    assert(r == 0);
}

void kvm_arch_init_irq_routing(KVMState *s)
{
    /*
     * Note that while irqchip capabilities generally imply that cpustates
     * are handled in-kernel, it is not true for s390 (yet); therefore, we
     * have to override the common code kvm_halt_in_kernel_allowed setting.
     */
    if (kvm_check_extension(s, KVM_CAP_IRQ_ROUTING)) {
        kvm_gsi_routing_allowed = true;
        kvm_halt_in_kernel_allowed = false;
    }
}

int kvm_s390_assign_subch_ioeventfd(EventNotifier *notifier, uint32_t sch,
                                    int vq, bool assign)
{
    struct kvm_ioeventfd kick = {
        .flags = KVM_IOEVENTFD_FLAG_VIRTIO_CCW_NOTIFY |
        KVM_IOEVENTFD_FLAG_DATAMATCH,
        .fd = event_notifier_get_fd(notifier),
        .datamatch = vq,
        .addr = sch,
        .len = 8,
    };
    if (!kvm_check_extension(kvm_state, KVM_CAP_IOEVENTFD)) {
        return -ENOSYS;
    }
    if (!assign) {
        kick.flags |= KVM_IOEVENTFD_FLAG_DEASSIGN;
    }
    return kvm_vm_ioctl(kvm_state, KVM_IOEVENTFD, &kick);
}

int kvm_s390_get_memslot_count(KVMState *s)
{
    return kvm_check_extension(s, KVM_CAP_NR_MEMSLOTS);
}

int kvm_s390_get_ri(void)
{
    return cap_ri;
}

int kvm_s390_get_gs(void)
{
    return cap_gs;
}

int kvm_s390_set_cpu_state(S390CPU *cpu, uint8_t cpu_state)
{
    struct kvm_mp_state mp_state = {};
    int ret;

    /* the kvm part might not have been initialized yet */
    if (CPU(cpu)->kvm_state == NULL) {
        return 0;
    }

    switch (cpu_state) {
    case CPU_STATE_STOPPED:
        mp_state.mp_state = KVM_MP_STATE_STOPPED;
        break;
    case CPU_STATE_CHECK_STOP:
        mp_state.mp_state = KVM_MP_STATE_CHECK_STOP;
        break;
    case CPU_STATE_OPERATING:
        mp_state.mp_state = KVM_MP_STATE_OPERATING;
        break;
    case CPU_STATE_LOAD:
        mp_state.mp_state = KVM_MP_STATE_LOAD;
        break;
    default:
        error_report("Requested CPU state is not a valid S390 CPU state: %u",
                     cpu_state);
        exit(1);
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_SET_MP_STATE, &mp_state);
    if (ret) {
        trace_kvm_failed_cpu_state_set(CPU(cpu)->cpu_index, cpu_state,
                                       strerror(-ret));
    }

    return ret;
}

void kvm_s390_vcpu_interrupt_pre_save(S390CPU *cpu)
{
    struct kvm_s390_irq_state irq_state;
    CPUState *cs = CPU(cpu);
    int32_t bytes;

    if (!kvm_check_extension(kvm_state, KVM_CAP_S390_IRQ_STATE)) {
        return;
    }

    irq_state.buf = (uint64_t) cpu->irqstate;
    irq_state.len = VCPU_IRQ_BUF_SIZE;

    bytes = kvm_vcpu_ioctl(cs, KVM_S390_GET_IRQ_STATE, &irq_state);
    if (bytes < 0) {
        cpu->irqstate_saved_size = 0;
        error_report("Migration of interrupt state failed");
        return;
    }

    cpu->irqstate_saved_size = bytes;
}

int kvm_s390_vcpu_interrupt_post_load(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    struct kvm_s390_irq_state irq_state;
    int r;

    if (cpu->irqstate_saved_size == 0) {
        return 0;
    }

    if (!kvm_check_extension(kvm_state, KVM_CAP_S390_IRQ_STATE)) {
        return -ENOSYS;
    }

    irq_state.buf = (uint64_t) cpu->irqstate;
    irq_state.len = cpu->irqstate_saved_size;

    r = kvm_vcpu_ioctl(cs, KVM_S390_SET_IRQ_STATE, &irq_state);
    if (r) {
        error_report("Setting interrupt state failed %d", r);
    }
    return r;
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
    S390PCIBusDevice *pbdev;
    uint32_t idx = data >> ZPCI_MSI_VEC_BITS;
    uint32_t vec = data & ZPCI_MSI_VEC_MASK;

    pbdev = s390_pci_find_dev_by_idx(s390_get_phb(), idx);
    if (!pbdev) {
        DPRINTF("add_msi_route no dev\n");
        return -ENODEV;
    }

    pbdev->routes.adapter.ind_offset = vec;

    route->type = KVM_IRQ_ROUTING_S390_ADAPTER;
    route->flags = 0;
    route->u.adapter.summary_addr = pbdev->routes.adapter.summary_addr;
    route->u.adapter.ind_addr = pbdev->routes.adapter.ind_addr;
    route->u.adapter.summary_offset = pbdev->routes.adapter.summary_offset;
    route->u.adapter.ind_offset = pbdev->routes.adapter.ind_offset;
    route->u.adapter.adapter_id = pbdev->routes.adapter.adapter_id;
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

static int query_cpu_subfunc(S390FeatBitmap features)
{
    struct kvm_s390_vm_cpu_subfunc prop;
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_CPU_MODEL,
        .attr = KVM_S390_VM_CPU_MACHINE_SUBFUNC,
        .addr = (uint64_t) &prop,
    };
    int rc;

    rc = kvm_vm_ioctl(kvm_state, KVM_GET_DEVICE_ATTR, &attr);
    if (rc) {
        return  rc;
    }

    /*
     * We're going to add all subfunctions now, if the corresponding feature
     * is available that unlocks the query functions.
     */
    s390_add_from_feat_block(features, S390_FEAT_TYPE_PLO, prop.plo);
    if (test_bit(S390_FEAT_TOD_CLOCK_STEERING, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_PTFF, prop.ptff);
    }
    if (test_bit(S390_FEAT_MSA, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KMAC, prop.kmac);
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KMC, prop.kmc);
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KM, prop.km);
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KIMD, prop.kimd);
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KLMD, prop.klmd);
    }
    if (test_bit(S390_FEAT_MSA_EXT_3, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_PCKMO, prop.pckmo);
    }
    if (test_bit(S390_FEAT_MSA_EXT_4, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KMCTR, prop.kmctr);
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KMF, prop.kmf);
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KMO, prop.kmo);
        s390_add_from_feat_block(features, S390_FEAT_TYPE_PCC, prop.pcc);
    }
    if (test_bit(S390_FEAT_MSA_EXT_5, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_PPNO, prop.ppno);
    }
    if (test_bit(S390_FEAT_MSA_EXT_8, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KMA, prop.kma);
    }
    return 0;
}

static int configure_cpu_subfunc(const S390FeatBitmap features)
{
    struct kvm_s390_vm_cpu_subfunc prop = {};
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_CPU_MODEL,
        .attr = KVM_S390_VM_CPU_PROCESSOR_SUBFUNC,
        .addr = (uint64_t) &prop,
    };

    if (!kvm_vm_check_attr(kvm_state, KVM_S390_VM_CPU_MODEL,
                           KVM_S390_VM_CPU_PROCESSOR_SUBFUNC)) {
        /* hardware support might be missing, IBC will handle most of this */
        return 0;
    }

    s390_fill_feat_block(features, S390_FEAT_TYPE_PLO, prop.plo);
    if (test_bit(S390_FEAT_TOD_CLOCK_STEERING, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_PTFF, prop.ptff);
    }
    if (test_bit(S390_FEAT_MSA, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_KMAC, prop.kmac);
        s390_fill_feat_block(features, S390_FEAT_TYPE_KMC, prop.kmc);
        s390_fill_feat_block(features, S390_FEAT_TYPE_KM, prop.km);
        s390_fill_feat_block(features, S390_FEAT_TYPE_KIMD, prop.kimd);
        s390_fill_feat_block(features, S390_FEAT_TYPE_KLMD, prop.klmd);
    }
    if (test_bit(S390_FEAT_MSA_EXT_3, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_PCKMO, prop.pckmo);
    }
    if (test_bit(S390_FEAT_MSA_EXT_4, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_KMCTR, prop.kmctr);
        s390_fill_feat_block(features, S390_FEAT_TYPE_KMF, prop.kmf);
        s390_fill_feat_block(features, S390_FEAT_TYPE_KMO, prop.kmo);
        s390_fill_feat_block(features, S390_FEAT_TYPE_PCC, prop.pcc);
    }
    if (test_bit(S390_FEAT_MSA_EXT_5, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_PPNO, prop.ppno);
    }
    if (test_bit(S390_FEAT_MSA_EXT_8, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_KMA, prop.kma);
    }
    return kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
}

static int kvm_to_feat[][2] = {
    { KVM_S390_VM_CPU_FEAT_ESOP, S390_FEAT_ESOP },
    { KVM_S390_VM_CPU_FEAT_SIEF2, S390_FEAT_SIE_F2 },
    { KVM_S390_VM_CPU_FEAT_64BSCAO , S390_FEAT_SIE_64BSCAO },
    { KVM_S390_VM_CPU_FEAT_SIIF, S390_FEAT_SIE_SIIF },
    { KVM_S390_VM_CPU_FEAT_GPERE, S390_FEAT_SIE_GPERE },
    { KVM_S390_VM_CPU_FEAT_GSLS, S390_FEAT_SIE_GSLS },
    { KVM_S390_VM_CPU_FEAT_IB, S390_FEAT_SIE_IB },
    { KVM_S390_VM_CPU_FEAT_CEI, S390_FEAT_SIE_CEI },
    { KVM_S390_VM_CPU_FEAT_IBS, S390_FEAT_SIE_IBS },
    { KVM_S390_VM_CPU_FEAT_SKEY, S390_FEAT_SIE_SKEY },
    { KVM_S390_VM_CPU_FEAT_CMMA, S390_FEAT_SIE_CMMA },
    { KVM_S390_VM_CPU_FEAT_PFMFI, S390_FEAT_SIE_PFMFI},
    { KVM_S390_VM_CPU_FEAT_SIGPIF, S390_FEAT_SIE_SIGPIF},
    { KVM_S390_VM_CPU_FEAT_KSS, S390_FEAT_SIE_KSS},
};

static int query_cpu_feat(S390FeatBitmap features)
{
    struct kvm_s390_vm_cpu_feat prop;
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_CPU_MODEL,
        .attr = KVM_S390_VM_CPU_MACHINE_FEAT,
        .addr = (uint64_t) &prop,
    };
    int rc;
    int i;

    rc = kvm_vm_ioctl(kvm_state, KVM_GET_DEVICE_ATTR, &attr);
    if (rc) {
        return  rc;
    }

    for (i = 0; i < ARRAY_SIZE(kvm_to_feat); i++) {
        if (test_be_bit(kvm_to_feat[i][0], (uint8_t *) prop.feat)) {
            set_bit(kvm_to_feat[i][1], features);
        }
    }
    return 0;
}

static int configure_cpu_feat(const S390FeatBitmap features)
{
    struct kvm_s390_vm_cpu_feat prop = {};
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_CPU_MODEL,
        .attr = KVM_S390_VM_CPU_PROCESSOR_FEAT,
        .addr = (uint64_t) &prop,
    };
    int i;

    for (i = 0; i < ARRAY_SIZE(kvm_to_feat); i++) {
        if (test_bit(kvm_to_feat[i][1], features)) {
            set_be_bit(kvm_to_feat[i][0], (uint8_t *) prop.feat);
        }
    }
    return kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
}

bool kvm_s390_cpu_models_supported(void)
{
    if (!cpu_model_allowed()) {
        /* compatibility machines interfere with the cpu model */
        return false;
    }
    return kvm_vm_check_attr(kvm_state, KVM_S390_VM_CPU_MODEL,
                             KVM_S390_VM_CPU_MACHINE) &&
           kvm_vm_check_attr(kvm_state, KVM_S390_VM_CPU_MODEL,
                             KVM_S390_VM_CPU_PROCESSOR) &&
           kvm_vm_check_attr(kvm_state, KVM_S390_VM_CPU_MODEL,
                             KVM_S390_VM_CPU_MACHINE_FEAT) &&
           kvm_vm_check_attr(kvm_state, KVM_S390_VM_CPU_MODEL,
                             KVM_S390_VM_CPU_PROCESSOR_FEAT) &&
           kvm_vm_check_attr(kvm_state, KVM_S390_VM_CPU_MODEL,
                             KVM_S390_VM_CPU_MACHINE_SUBFUNC);
}

void kvm_s390_get_host_cpu_model(S390CPUModel *model, Error **errp)
{
    struct kvm_s390_vm_cpu_machine prop = {};
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_CPU_MODEL,
        .attr = KVM_S390_VM_CPU_MACHINE,
        .addr = (uint64_t) &prop,
    };
    uint16_t unblocked_ibc = 0, cpu_type = 0;
    int rc;

    memset(model, 0, sizeof(*model));

    if (!kvm_s390_cpu_models_supported()) {
        error_setg(errp, "KVM doesn't support CPU models");
        return;
    }

    /* query the basic cpu model properties */
    rc = kvm_vm_ioctl(kvm_state, KVM_GET_DEVICE_ATTR, &attr);
    if (rc) {
        error_setg(errp, "KVM: Error querying host CPU model: %d", rc);
        return;
    }

    cpu_type = cpuid_type(prop.cpuid);
    if (has_ibc(prop.ibc)) {
        model->lowest_ibc = lowest_ibc(prop.ibc);
        unblocked_ibc = unblocked_ibc(prop.ibc);
    }
    model->cpu_id = cpuid_id(prop.cpuid);
    model->cpu_id_format = cpuid_format(prop.cpuid);
    model->cpu_ver = 0xff;

    /* get supported cpu features indicated via STFL(E) */
    s390_add_from_feat_block(model->features, S390_FEAT_TYPE_STFL,
                             (uint8_t *) prop.fac_mask);
    /* dat-enhancement facility 2 has no bit but was introduced with stfle */
    if (test_bit(S390_FEAT_STFLE, model->features)) {
        set_bit(S390_FEAT_DAT_ENH_2, model->features);
    }
    /* get supported cpu features indicated e.g. via SCLP */
    rc = query_cpu_feat(model->features);
    if (rc) {
        error_setg(errp, "KVM: Error querying CPU features: %d", rc);
        return;
    }
    /* get supported cpu subfunctions indicated via query / test bit */
    rc = query_cpu_subfunc(model->features);
    if (rc) {
        error_setg(errp, "KVM: Error querying CPU subfunctions: %d", rc);
        return;
    }

    /* with cpu model support, CMM is only indicated if really available */
    if (kvm_s390_cmma_available()) {
        set_bit(S390_FEAT_CMM, model->features);
    } else {
        /* no cmm -> no cmm nt */
        clear_bit(S390_FEAT_CMM_NT, model->features);
    }

    /* We emulate a zPCI bus and AEN, therefore we don't need HW support */
    set_bit(S390_FEAT_ZPCI, model->features);
    set_bit(S390_FEAT_ADAPTER_EVENT_NOTIFICATION, model->features);

    if (s390_known_cpu_type(cpu_type)) {
        /* we want the exact model, even if some features are missing */
        model->def = s390_find_cpu_def(cpu_type, ibc_gen(unblocked_ibc),
                                       ibc_ec_ga(unblocked_ibc), NULL);
    } else {
        /* model unknown, e.g. too new - search using features */
        model->def = s390_find_cpu_def(0, ibc_gen(unblocked_ibc),
                                       ibc_ec_ga(unblocked_ibc),
                                       model->features);
    }
    if (!model->def) {
        error_setg(errp, "KVM: host CPU model could not be identified");
        return;
    }
    /* strip of features that are not part of the maximum model */
    bitmap_and(model->features, model->features, model->def->full_feat,
               S390_FEAT_MAX);
}

void kvm_s390_apply_cpu_model(const S390CPUModel *model, Error **errp)
{
    struct kvm_s390_vm_cpu_processor prop  = {
        .fac_list = { 0 },
    };
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_CPU_MODEL,
        .attr = KVM_S390_VM_CPU_PROCESSOR,
        .addr = (uint64_t) &prop,
    };
    int rc;

    if (!model) {
        /* compatibility handling if cpu models are disabled */
        if (kvm_s390_cmma_available()) {
            kvm_s390_enable_cmma();
        }
        return;
    }
    if (!kvm_s390_cpu_models_supported()) {
        error_setg(errp, "KVM doesn't support CPU models");
        return;
    }
    prop.cpuid = s390_cpuid_from_cpu_model(model);
    prop.ibc = s390_ibc_from_cpu_model(model);
    /* configure cpu features indicated via STFL(e) */
    s390_fill_feat_block(model->features, S390_FEAT_TYPE_STFL,
                         (uint8_t *) prop.fac_list);
    rc = kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
    if (rc) {
        error_setg(errp, "KVM: Error configuring the CPU model: %d", rc);
        return;
    }
    /* configure cpu features indicated e.g. via SCLP */
    rc = configure_cpu_feat(model->features);
    if (rc) {
        error_setg(errp, "KVM: Error configuring CPU features: %d", rc);
        return;
    }
    /* configure cpu subfunctions indicated via query / test bit */
    rc = configure_cpu_subfunc(model->features);
    if (rc) {
        error_setg(errp, "KVM: Error configuring CPU subfunctions: %d", rc);
        return;
    }
    /* enable CMM via CMMA */
    if (test_bit(S390_FEAT_CMM, model->features)) {
        kvm_s390_enable_cmma();
    }
}
