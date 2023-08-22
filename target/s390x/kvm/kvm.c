/*
 * QEMU S390x KVM implementation
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Copyright IBM Corp. 2012
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>
#include <asm/ptrace.h>

#include "cpu.h"
#include "s390x-internal.h"
#include "kvm_s390x.h"
#include "sysemu/kvm_int.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "qemu/main-loop.h"
#include "qemu/mmap-alloc.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"
#include "sysemu/device_tree.h"
#include "exec/gdbstub.h"
#include "exec/ram_addr.h"
#include "trace.h"
#include "hw/s390x/s390-pci-inst.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/ipl.h"
#include "hw/s390x/ebcdic.h"
#include "exec/memattrs.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/s390x/s390-virtio-hcall.h"
#include "hw/s390x/pv.h"

#ifndef DEBUG_KVM
#define DEBUG_KVM  0
#endif

#define DPRINTF(fmt, ...) do {                \
    if (DEBUG_KVM) {                          \
        fprintf(stderr, fmt, ## __VA_ARGS__); \
    }                                         \
} while (0)

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
#define DIAG_SET_CONTROL_PROGRAM_CODES  0x318
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
#define ICPT_PV_INSTR                   0x68
#define ICPT_PV_INSTR_NOTIFICATION      0x6c

#define NR_LOCAL_IRQS 32
/*
 * Needs to be big enough to contain max_cpus emergency signals
 * and in addition NR_LOCAL_IRQS interrupts
 */
#define VCPU_IRQ_BUF_SIZE(max_cpus) (sizeof(struct kvm_s390_irq) * \
                                     (max_cpus + NR_LOCAL_IRQS))
/*
 * KVM does only support memory slots up to KVM_MEM_MAX_NR_PAGES pages
 * as the dirty bitmap must be managed by bitops that take an int as
 * position indicator. This would end at an unaligned  address
 * (0x7fffff00000). As future variants might provide larger pages
 * and to make all addresses properly aligned, let us split at 4TB.
 */
#define KVM_SLOT_MAX_BYTES (4UL * TiB)

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

static int cap_sync_regs;
static int cap_async_pf;
static int cap_mem_op;
static int cap_mem_op_extension;
static int cap_s390_irq;
static int cap_ri;
static int cap_hpage_1m;
static int cap_vcpu_resets;
static int cap_protected;
static int cap_zpci_op;
static int cap_protected_dump;

static bool mem_op_storage_key_support;

static int active_cmma;

static int kvm_s390_query_mem_limit(uint64_t *memory_limit)
{
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_MEM_CTRL,
        .attr = KVM_S390_VM_MEM_LIMIT_SIZE,
        .addr = (uint64_t) memory_limit,
    };

    return kvm_vm_ioctl(kvm_state, KVM_GET_DEVICE_ATTR, &attr);
}

int kvm_s390_set_mem_limit(uint64_t new_limit, uint64_t *hw_limit)
{
    int rc;

    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_MEM_CTRL,
        .attr = KVM_S390_VM_MEM_LIMIT_SIZE,
        .addr = (uint64_t) &new_limit,
    };

    if (!kvm_vm_check_mem_attr(kvm_state, KVM_S390_VM_MEM_LIMIT_SIZE)) {
        return 0;
    }

    rc = kvm_s390_query_mem_limit(hw_limit);
    if (rc) {
        return rc;
    } else if (*hw_limit < new_limit) {
        return -E2BIG;
    }

    return kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
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

    if (cap_hpage_1m) {
        warn_report("CMM will not be enabled because it is not "
                    "compatible with huge memory backings.");
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

void kvm_s390_set_max_pagesize(uint64_t pagesize, Error **errp)
{
    if (pagesize == 4 * KiB) {
        return;
    }

    if (!hpage_1m_allowed()) {
        error_setg(errp, "This QEMU machine does not support huge page "
                   "mappings");
        return;
    }

    if (pagesize != 1 * MiB) {
        error_setg(errp, "Memory backing with 2G pages was specified, "
                   "but KVM does not support this memory backing");
        return;
    }

    if (kvm_vm_enable_cap(kvm_state, KVM_CAP_S390_HPAGE_1M, 0)) {
        error_setg(errp, "Memory backing with 1M pages was specified, "
                   "but KVM does not support this memory backing");
        return;
    }

    cap_hpage_1m = 1;
}

int kvm_s390_get_hpage_1m(void)
{
    return cap_hpage_1m;
}

static void ccw_machine_class_foreach(ObjectClass *oc, void *opaque)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->default_cpu_type = S390_CPU_TYPE_NAME("host");
}

int kvm_arch_get_default_type(MachineState *ms)
{
    return 0;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    object_class_foreach(ccw_machine_class_foreach, TYPE_S390_CCW_MACHINE,
                         false, NULL);

    if (!kvm_check_extension(kvm_state, KVM_CAP_DEVICE_CTRL)) {
        error_report("KVM is missing capability KVM_CAP_DEVICE_CTRL - "
                     "please use kernel 3.15 or newer");
        return -1;
    }
    if (!kvm_check_extension(s, KVM_CAP_S390_COW)) {
        error_report("KVM is missing capability KVM_CAP_S390_COW - "
                     "unsupported environment");
        return -1;
    }

    cap_sync_regs = kvm_check_extension(s, KVM_CAP_SYNC_REGS);
    cap_async_pf = kvm_check_extension(s, KVM_CAP_ASYNC_PF);
    cap_mem_op = kvm_check_extension(s, KVM_CAP_S390_MEM_OP);
    cap_mem_op_extension = kvm_check_extension(s, KVM_CAP_S390_MEM_OP_EXTENSION);
    mem_op_storage_key_support = cap_mem_op_extension > 0;
    cap_s390_irq = kvm_check_extension(s, KVM_CAP_S390_INJECT_IRQ);
    cap_vcpu_resets = kvm_check_extension(s, KVM_CAP_S390_VCPU_RESETS);
    cap_protected = kvm_check_extension(s, KVM_CAP_S390_PROTECTED);
    cap_zpci_op = kvm_check_extension(s, KVM_CAP_S390_ZPCI_OP);
    cap_protected_dump = kvm_check_extension(s, KVM_CAP_S390_PROTECTED_DUMP);

    kvm_vm_enable_cap(s, KVM_CAP_S390_USER_SIGP, 0);
    kvm_vm_enable_cap(s, KVM_CAP_S390_VECTOR_REGISTERS, 0);
    kvm_vm_enable_cap(s, KVM_CAP_S390_USER_STSI, 0);
    if (ri_allowed()) {
        if (kvm_vm_enable_cap(s, KVM_CAP_S390_RI, 0) == 0) {
            cap_ri = 1;
        }
    }
    if (cpu_model_allowed()) {
        kvm_vm_enable_cap(s, KVM_CAP_S390_GS, 0);
    }

    /*
     * The migration interface for ais was introduced with kernel 4.13
     * but the capability itself had been active since 4.12. As migration
     * support is considered necessary, we only try to enable this for
     * newer machine types if KVM_CAP_S390_AIS_MIGRATION is available.
     */
    if (cpu_model_allowed() && kvm_kernel_irqchip_allowed() &&
        kvm_check_extension(s, KVM_CAP_S390_AIS_MIGRATION)) {
        kvm_vm_enable_cap(s, KVM_CAP_S390_AIS, 0);
    }

    kvm_set_max_memslot_size(KVM_SLOT_MAX_BYTES);
    return 0;
}

int kvm_arch_irqchip_create(KVMState *s)
{
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    unsigned int max_cpus = MACHINE(qdev_get_machine())->smp.max_cpus;
    S390CPU *cpu = S390_CPU(cs);
    kvm_s390_set_cpu_state(cpu, cpu->env.cpu_state);
    cpu->irqstate = g_malloc0(VCPU_IRQ_BUF_SIZE(max_cpus));
    return 0;
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);

    g_free(cpu->irqstate);
    cpu->irqstate = NULL;

    return 0;
}

static void kvm_s390_reset_vcpu(S390CPU *cpu, unsigned long type)
{
    CPUState *cs = CPU(cpu);

    /*
     * The reset call is needed here to reset in-kernel vcpu data that
     * we can't access directly from QEMU (i.e. with older kernels
     * which don't support sync_regs/ONE_REG).  Before this ioctl
     * cpu_synchronize_state() is called in common kvm code
     * (kvm-all).
     */
    if (kvm_vcpu_ioctl(cs, type)) {
        error_report("CPU reset failed on CPU %i type %lx",
                     cs->cpu_index, type);
    }
}

void kvm_s390_reset_vcpu_initial(S390CPU *cpu)
{
    kvm_s390_reset_vcpu(cpu, KVM_S390_INITIAL_RESET);
}

void kvm_s390_reset_vcpu_clear(S390CPU *cpu)
{
    if (cap_vcpu_resets) {
        kvm_s390_reset_vcpu(cpu, KVM_S390_CLEAR_RESET);
    } else {
        kvm_s390_reset_vcpu(cpu, KVM_S390_INITIAL_RESET);
    }
}

void kvm_s390_reset_vcpu_normal(S390CPU *cpu)
{
    if (cap_vcpu_resets) {
        kvm_s390_reset_vcpu(cpu, KVM_S390_NORMAL_RESET);
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
            cs->kvm_run->s.regs.vrs[i][0] = env->vregs[i][0];
            cs->kvm_run->s.regs.vrs[i][1] = env->vregs[i][1];
        }
        cs->kvm_run->s.regs.fpc = env->fpc;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_VRS;
    } else if (can_sync_regs(cs, KVM_SYNC_FPRS)) {
        for (i = 0; i < 16; i++) {
            cs->kvm_run->s.regs.fprs[i] = *get_freg(env, i);
        }
        cs->kvm_run->s.regs.fpc = env->fpc;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_FPRS;
    } else {
        /* Floating point */
        for (i = 0; i < 16; i++) {
            fpu.fprs[i] = *get_freg(env, i);
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

    if (can_sync_regs(cs, KVM_SYNC_BPBC)) {
        cs->kvm_run->s.regs.bpbc = env->bpbc;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_BPBC;
    }

    if (can_sync_regs(cs, KVM_SYNC_ETOKEN)) {
        cs->kvm_run->s.regs.etoken = env->etoken;
        cs->kvm_run->s.regs.etoken_extension  = env->etoken_extension;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_ETOKEN;
    }

    if (can_sync_regs(cs, KVM_SYNC_DIAG318)) {
        cs->kvm_run->s.regs.diag318 = env->diag318_info;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_DIAG318;
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
            env->vregs[i][0] = cs->kvm_run->s.regs.vrs[i][0];
            env->vregs[i][1] = cs->kvm_run->s.regs.vrs[i][1];
        }
        env->fpc = cs->kvm_run->s.regs.fpc;
    } else if (can_sync_regs(cs, KVM_SYNC_FPRS)) {
        for (i = 0; i < 16; i++) {
            *get_freg(env, i) = cs->kvm_run->s.regs.fprs[i];
        }
        env->fpc = cs->kvm_run->s.regs.fpc;
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_GET_FPU, &fpu);
        if (r < 0) {
            return r;
        }
        for (i = 0; i < 16; i++) {
            *get_freg(env, i) = fpu.fprs[i];
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

    if (can_sync_regs(cs, KVM_SYNC_BPBC)) {
        env->bpbc = cs->kvm_run->s.regs.bpbc;
    }

    if (can_sync_regs(cs, KVM_SYNC_ETOKEN)) {
        env->etoken = cs->kvm_run->s.regs.etoken;
        env->etoken_extension = cs->kvm_run->s.regs.etoken_extension;
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

    if (can_sync_regs(cs, KVM_SYNC_DIAG318)) {
        env->diag318_info = cs->kvm_run->s.regs.diag318;
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

int kvm_s390_get_clock_ext(uint8_t *tod_high, uint64_t *tod_low)
{
    int r;
    struct kvm_s390_vm_tod_clock gtod;
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_TOD,
        .attr = KVM_S390_VM_TOD_EXT,
        .addr = (uint64_t)&gtod,
    };

    r = kvm_vm_ioctl(kvm_state, KVM_GET_DEVICE_ATTR, &attr);
    *tod_high = gtod.epoch_idx;
    *tod_low  = gtod.tod;

    return r;
}

int kvm_s390_set_clock(uint8_t tod_high, uint64_t tod_low)
{
    int r;
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_TOD,
        .attr = KVM_S390_VM_TOD_LOW,
        .addr = (uint64_t)&tod_low,
    };

    r = kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
    if (r) {
        return r;
    }

    attr.attr = KVM_S390_VM_TOD_HIGH;
    attr.addr = (uint64_t)&tod_high;
    return kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
}

int kvm_s390_set_clock_ext(uint8_t tod_high, uint64_t tod_low)
{
    struct kvm_s390_vm_tod_clock gtod = {
        .epoch_idx = tod_high,
        .tod  = tod_low,
    };
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_TOD,
        .attr = KVM_S390_VM_TOD_EXT,
        .addr = (uint64_t)&gtod,
    };

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
        .key = (cpu->env.psw.mask & PSW_MASK_KEY) >> PSW_SHIFT_KEY,
    };
    int ret;

    if (!cap_mem_op) {
        return -ENOSYS;
    }
    if (!hostbuf) {
        mem_op.flags |= KVM_S390_MEMOP_F_CHECK_ONLY;
    }
    if (mem_op_storage_key_support) {
        mem_op.flags |= KVM_S390_MEMOP_F_SKEY_PROTECTION;
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_S390_MEM_OP, &mem_op);
    if (ret < 0) {
        warn_report("KVM_S390_MEM_OP failed: %s", strerror(-ret));
    }
    return ret;
}

int kvm_s390_mem_op_pv(S390CPU *cpu, uint64_t offset, void *hostbuf,
                       int len, bool is_write)
{
    struct kvm_s390_mem_op mem_op = {
        .sida_offset = offset,
        .size = len,
        .op = is_write ? KVM_S390_MEMOP_SIDA_WRITE
                       : KVM_S390_MEMOP_SIDA_READ,
        .buf = (uint64_t)hostbuf,
    };
    int ret;

    if (!cap_mem_op || !cap_protected) {
        return -ENOSYS;
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_S390_MEM_OP, &mem_op);
    if (ret < 0) {
        error_report("KVM_S390_MEM_OP failed: %s", strerror(-ret));
        abort();
    }
    return ret;
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
             g_realloc(hw_breakpoints, size);
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

void kvm_s390_floating_interrupt_legacy(struct kvm_s390_irq *irq)
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

void kvm_s390_program_interrupt(S390CPU *cpu, uint16_t code)
{
    struct kvm_s390_irq irq = {
        .type = KVM_S390_PROGRAM_INT,
        .u.pgm.code = code,
    };
    qemu_log_mask(CPU_LOG_INT, "program interrupt at %#" PRIx64 "\n",
                  cpu->env.psw.addr);
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

static void kvm_sclp_service_call(S390CPU *cpu, struct kvm_run *run,
                                 uint16_t ipbh0)
{
    CPUS390XState *env = &cpu->env;
    uint64_t sccb;
    uint32_t code;
    int r;

    sccb = env->regs[ipbh0 & 0xf];
    code = env->regs[(ipbh0 & 0xf0) >> 4];

    switch (run->s390_sieic.icptcode) {
    case ICPT_PV_INSTR_NOTIFICATION:
        g_assert(s390_is_pv());
        /* The notification intercepts are currently handled by KVM */
        error_report("unexpected SCLP PV notification");
        exit(1);
        break;
    case ICPT_PV_INSTR:
        g_assert(s390_is_pv());
        sclp_service_call_protected(env, sccb, code);
        /* Setting the CC is done by the Ultravisor. */
        break;
    case ICPT_INSTRUCTION:
        g_assert(!s390_is_pv());
        r = sclp_service_call(env, sccb, code);
        if (r < 0) {
            kvm_s390_program_interrupt(cpu, -r);
            return;
        }
        setcc(cpu, r);
    }
}

static int handle_b2(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    CPUS390XState *env = &cpu->env;
    int rc = 0;
    uint16_t ipbh0 = (run->s390_sieic.ipb & 0xffff0000) >> 16;

    switch (ipa1) {
    case PRIV_B2_XSCH:
        ioinst_handle_xsch(cpu, env->regs[1], RA_IGNORED);
        break;
    case PRIV_B2_CSCH:
        ioinst_handle_csch(cpu, env->regs[1], RA_IGNORED);
        break;
    case PRIV_B2_HSCH:
        ioinst_handle_hsch(cpu, env->regs[1], RA_IGNORED);
        break;
    case PRIV_B2_MSCH:
        ioinst_handle_msch(cpu, env->regs[1], run->s390_sieic.ipb, RA_IGNORED);
        break;
    case PRIV_B2_SSCH:
        ioinst_handle_ssch(cpu, env->regs[1], run->s390_sieic.ipb, RA_IGNORED);
        break;
    case PRIV_B2_STCRW:
        ioinst_handle_stcrw(cpu, run->s390_sieic.ipb, RA_IGNORED);
        break;
    case PRIV_B2_STSCH:
        ioinst_handle_stsch(cpu, env->regs[1], run->s390_sieic.ipb, RA_IGNORED);
        break;
    case PRIV_B2_TSCH:
        /* We should only get tsch via KVM_EXIT_S390_TSCH. */
        fprintf(stderr, "Spurious tsch intercept\n");
        break;
    case PRIV_B2_CHSC:
        ioinst_handle_chsc(cpu, run->s390_sieic.ipb, RA_IGNORED);
        break;
    case PRIV_B2_TPI:
        /* This should have been handled by kvm already. */
        fprintf(stderr, "Spurious tpi intercept\n");
        break;
    case PRIV_B2_SCHM:
        ioinst_handle_schm(cpu, env->regs[1], env->regs[2],
                           run->s390_sieic.ipb, RA_IGNORED);
        break;
    case PRIV_B2_RSCH:
        ioinst_handle_rsch(cpu, env->regs[1], RA_IGNORED);
        break;
    case PRIV_B2_RCHP:
        ioinst_handle_rchp(cpu, env->regs[1], RA_IGNORED);
        break;
    case PRIV_B2_STCPS:
        /* We do not provide this instruction, it is suppressed. */
        break;
    case PRIV_B2_SAL:
        ioinst_handle_sal(cpu, env->regs[1], RA_IGNORED);
        break;
    case PRIV_B2_SIGA:
        /* Not provided, set CC = 3 for subchannel not operational */
        setcc(cpu, 3);
        break;
    case PRIV_B2_SCLP_CALL:
        kvm_sclp_service_call(cpu, run, ipbh0);
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

    if (s390_has_feat(S390_FEAT_ZPCI)) {
        return clp_service_call(cpu, r2, RA_IGNORED);
    } else {
        return -1;
    }
}

static int kvm_pcilg_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipb & 0x00f00000) >> 20;
    uint8_t r2 = (run->s390_sieic.ipb & 0x000f0000) >> 16;

    if (s390_has_feat(S390_FEAT_ZPCI)) {
        return pcilg_service_call(cpu, r1, r2, RA_IGNORED);
    } else {
        return -1;
    }
}

static int kvm_pcistg_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipb & 0x00f00000) >> 20;
    uint8_t r2 = (run->s390_sieic.ipb & 0x000f0000) >> 16;

    if (s390_has_feat(S390_FEAT_ZPCI)) {
        return pcistg_service_call(cpu, r1, r2, RA_IGNORED);
    } else {
        return -1;
    }
}

static int kvm_stpcifc_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint64_t fiba;
    uint8_t ar;

    if (s390_has_feat(S390_FEAT_ZPCI)) {
        fiba = get_base_disp_rxy(cpu, run, &ar);

        return stpcifc_service_call(cpu, r1, fiba, ar, RA_IGNORED);
    } else {
        return -1;
    }
}

static int kvm_sic_service_call(S390CPU *cpu, struct kvm_run *run)
{
    CPUS390XState *env = &cpu->env;
    uint8_t r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint8_t r3 = run->s390_sieic.ipa & 0x000f;
    uint8_t isc;
    uint16_t mode;
    int r;

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

    if (s390_has_feat(S390_FEAT_ZPCI)) {
        return rpcit_service_call(cpu, r1, r2, RA_IGNORED);
    } else {
        return -1;
    }
}

static int kvm_pcistb_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint8_t r3 = run->s390_sieic.ipa & 0x000f;
    uint64_t gaddr;
    uint8_t ar;

    if (s390_has_feat(S390_FEAT_ZPCI)) {
        gaddr = get_base_disp_rsy(cpu, run, &ar);

        return pcistb_service_call(cpu, r1, r3, gaddr, ar, RA_IGNORED);
    } else {
        return -1;
    }
}

static int kvm_mpcifc_service_call(S390CPU *cpu, struct kvm_run *run)
{
    uint8_t r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint64_t fiba;
    uint8_t ar;

    if (s390_has_feat(S390_FEAT_ZPCI)) {
        fiba = get_base_disp_rxy(cpu, run, &ar);

        return mpcifc_service_call(cpu, r1, fiba, ar, RA_IGNORED);
    } else {
        return -1;
    }
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

    r1 = (run->s390_sieic.ipa & 0x00f0) >> 4;
    r3 = run->s390_sieic.ipa & 0x000f;
    handle_diag_308(&cpu->env, r1, r3, RA_IGNORED);
}

static int handle_sw_breakpoint(S390CPU *cpu, struct kvm_run *run)
{
    CPUS390XState *env = &cpu->env;
    unsigned long pc;

    pc = env->psw.addr - sw_bp_ilen;
    if (kvm_find_sw_breakpoint(CPU(cpu), pc)) {
        env->psw.addr = pc;
        return EXCP_DEBUG;
    }

    return -ENOENT;
}

void kvm_s390_set_diag318(CPUState *cs, uint64_t diag318_info)
{
    CPUS390XState *env = &S390_CPU(cs)->env;

    /* Feat bit is set only if KVM supports sync for diag318 */
    if (s390_has_feat(S390_FEAT_DIAG_318)) {
        env->diag318_info = diag318_info;
        cs->kvm_run->s.regs.diag318 = diag318_info;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_DIAG318;
        /*
         * diag 318 info is zeroed during a clear reset and
         * diag 308 IPL subcodes.
         */
    }
}

static void handle_diag_318(S390CPU *cpu, struct kvm_run *run)
{
    uint64_t reg = (run->s390_sieic.ipa & 0x00f0) >> 4;
    uint64_t diag318_info = run->s.regs.gprs[reg];
    CPUState *t;

    /*
     * DIAG 318 can only be enabled with KVM support. As such, let's
     * ensure a guest cannot execute this instruction erroneously.
     */
    if (!s390_has_feat(S390_FEAT_DIAG_318)) {
        kvm_s390_program_interrupt(cpu, PGM_SPECIFICATION);
        return;
    }

    CPU_FOREACH(t) {
        run_on_cpu(t, s390_do_cpu_set_diag318,
                   RUN_ON_CPU_HOST_ULONG(diag318_info));
    }
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
    case DIAG_SET_CONTROL_PROGRAM_CODES:
        handle_diag_318(cpu, run);
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

static int kvm_s390_handle_sigp(S390CPU *cpu, uint8_t ipa1, uint32_t ipb)
{
    CPUS390XState *env = &cpu->env;
    const uint8_t r1 = ipa1 >> 4;
    const uint8_t r3 = ipa1 & 0x0f;
    int ret;
    uint8_t order;

    /* get order code */
    order = decode_basedisp_rs(env, ipb, NULL) & SIGP_ORDER_MASK;

    ret = handle_sigp(env, order, r1, r3);
    setcc(cpu, ret);
    return 0;
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
        r = kvm_s390_handle_sigp(cpu, ipa1, run->s390_sieic.ipb);
        break;
    }

    if (r < 0) {
        r = 0;
        kvm_s390_program_interrupt(cpu, PGM_OPERATION);
    }

    return r;
}

static void unmanageable_intercept(S390CPU *cpu, S390CrashReason reason,
                                   int pswoffset)
{
    CPUState *cs = CPU(cpu);

    s390_cpu_halt(cpu);
    cpu->env.crash_reason = reason;
    qemu_system_guest_panicked(cpu_get_crash_info(cs));
}

/* try to detect pgm check loops */
static int handle_oper_loop(S390CPU *cpu, struct kvm_run *run)
{
    CPUState *cs = CPU(cpu);
    PSW oldpsw, newpsw;

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
        unmanageable_intercept(cpu, S390_CRASH_REASON_OPINT_LOOP,
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

    DPRINTF("intercept: 0x%x (at 0x%lx)\n", icpt_code, (long)run->psw_addr);
    switch (icpt_code) {
        case ICPT_INSTRUCTION:
        case ICPT_PV_INSTR:
        case ICPT_PV_INSTR_NOTIFICATION:
            r = handle_instruction(cpu, run);
            break;
        case ICPT_PROGRAM:
            unmanageable_intercept(cpu, S390_CRASH_REASON_PGMINT_LOOP,
                                   offsetof(LowCore, program_new_psw));
            r = EXCP_HALTED;
            break;
        case ICPT_EXT_INT:
            unmanageable_intercept(cpu, S390_CRASH_REASON_EXTINT_LOOP,
                                   offsetof(LowCore, external_new_psw));
            r = EXCP_HALTED;
            break;
        case ICPT_WAITPSW:
            /* disabled wait, since enabled wait is handled in kernel */
            s390_handle_wait(cpu);
            r = EXCP_HALTED;
            break;
        case ICPT_CPU_STOP:
            do_stop_interrupt(&cpu->env);
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

    ret = ioinst_handle_tsch(cpu, cpu->env.regs[1], run->s390_tsch.ipb,
                             RA_IGNORED);
    if (ret < 0) {
        /*
         * Failure.
         * If an I/O interrupt had been dequeued, we have to reinject it.
         */
        if (run->s390_tsch.dequeued) {
            s390_io_interrupt(run->s390_tsch.subchannel_id,
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
    const MachineState *ms = MACHINE(qdev_get_machine());
    uint16_t conf_cpus = 0, reserved_cpus = 0;
    SysIB_322 sysib;
    int del, i;

    if (s390_is_pv()) {
        s390_cpu_pv_mem_read(cpu, 0, &sysib, sizeof(sysib));
    } else if (s390_cpu_virt_mem_read(cpu, addr, ar, &sysib, sizeof(sysib))) {
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

    /* count the cpus and split them into configured and reserved ones */
    for (i = 0; i < ms->possible_cpus->len; i++) {
        if (ms->possible_cpus->cpus[i].cpu) {
            conf_cpus++;
        } else {
            reserved_cpus++;
        }
    }
    sysib.vm[0].total_cpus = conf_cpus + reserved_cpus;
    sysib.vm[0].conf_cpus = conf_cpus;
    sysib.vm[0].reserved_cpus = reserved_cpus;

    /* Insert short machine name in EBCDIC, padded with blanks */
    if (qemu_name) {
        memset(sysib.vm[0].name, 0x40, sizeof(sysib.vm[0].name));
        ebcdic_put(sysib.vm[0].name, qemu_name, MIN(sizeof(sysib.vm[0].name),
                                                    strlen(qemu_name)));
    }
    sysib.vm[0].ext_name_encoding = 2; /* 2 = UTF-8 */
    /* If hypervisor specifies zero Extended Name in STSI322 SYSIB, it's
     * considered by s390 as not capable of providing any Extended Name.
     * Therefore if no name was specified on qemu invocation, we go with the
     * same "KVMguest" default, which KVM has filled into short name field.
     */
    strpadcpy((char *)sysib.ext_names[0],
              sizeof(sysib.ext_names[0]),
              qemu_name ?: "KVMguest", '\0');

    /* Insert UUID */
    memcpy(sysib.vm[0].uuid, &qemu_uuid, sizeof(sysib.vm[0].uuid));

    if (s390_is_pv()) {
        s390_cpu_pv_mem_write(cpu, 0, &sysib, sizeof(sysib));
    } else {
        s390_cpu_virt_mem_write(cpu, addr, ar, &sysib, sizeof(sysib));
    }
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

    kvm_cpu_synchronize_state(cs);

    switch (run->exit_reason) {
        case KVM_EXIT_S390_SIEIC:
            ret = handle_intercept(cpu);
            break;
        case KVM_EXIT_S390_RESET:
            s390_ipl_reset_request(cs, S390_RESET_REIPL);
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
    trace_kvm_assign_subch_ioeventfd(kick.fd, kick.addr, assign,
                                     kick.datamatch);
    if (!kvm_check_extension(kvm_state, KVM_CAP_IOEVENTFD)) {
        return -ENOSYS;
    }
    if (!assign) {
        kick.flags |= KVM_IOEVENTFD_FLAG_DEASSIGN;
    }
    return kvm_vm_ioctl(kvm_state, KVM_IOEVENTFD, &kick);
}

int kvm_s390_get_protected_dump(void)
{
    return cap_protected_dump;
}

int kvm_s390_get_ri(void)
{
    return cap_ri;
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
    case S390_CPU_STATE_STOPPED:
        mp_state.mp_state = KVM_MP_STATE_STOPPED;
        break;
    case S390_CPU_STATE_CHECK_STOP:
        mp_state.mp_state = KVM_MP_STATE_CHECK_STOP;
        break;
    case S390_CPU_STATE_OPERATING:
        mp_state.mp_state = KVM_MP_STATE_OPERATING;
        break;
    case S390_CPU_STATE_LOAD:
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
    unsigned int max_cpus = MACHINE(qdev_get_machine())->smp.max_cpus;
    struct kvm_s390_irq_state irq_state = {
        .buf = (uint64_t) cpu->irqstate,
        .len = VCPU_IRQ_BUF_SIZE(max_cpus),
    };
    CPUState *cs = CPU(cpu);
    int32_t bytes;

    if (!kvm_check_extension(kvm_state, KVM_CAP_S390_IRQ_STATE)) {
        return;
    }

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
    struct kvm_s390_irq_state irq_state = {
        .buf = (uint64_t) cpu->irqstate,
        .len = cpu->irqstate_saved_size,
    };
    int r;

    if (cpu->irqstate_saved_size == 0) {
        return 0;
    }

    if (!kvm_check_extension(kvm_state, KVM_CAP_S390_IRQ_STATE)) {
        return -ENOSYS;
    }

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
    uint32_t vec = data & ZPCI_MSI_VEC_MASK;

    if (!dev) {
        DPRINTF("add_msi_route no pci device\n");
        return -ENODEV;
    }

    pbdev = s390_pci_find_dev_by_target(s390_get_phb(), DEVICE(dev)->id);
    if (!pbdev) {
        DPRINTF("add_msi_route no zpci device\n");
        return -ENODEV;
    }

    route->type = KVM_IRQ_ROUTING_S390_ADAPTER;
    route->flags = 0;
    route->u.adapter.summary_addr = pbdev->routes.adapter.summary_addr;
    route->u.adapter.ind_addr = pbdev->routes.adapter.ind_addr;
    route->u.adapter.summary_offset = pbdev->routes.adapter.summary_offset;
    route->u.adapter.ind_offset = pbdev->routes.adapter.ind_offset + vec;
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
    struct kvm_s390_vm_cpu_subfunc prop = {};
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
    if (test_bit(S390_FEAT_MSA_EXT_9, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_KDSA, prop.kdsa);
    }
    if (test_bit(S390_FEAT_ESORT_BASE, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_SORTL, prop.sortl);
    }
    if (test_bit(S390_FEAT_DEFLATE_BASE, features)) {
        s390_add_from_feat_block(features, S390_FEAT_TYPE_DFLTCC, prop.dfltcc);
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
    if (test_bit(S390_FEAT_MSA_EXT_9, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_KDSA, prop.kdsa);
    }
    if (test_bit(S390_FEAT_ESORT_BASE, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_SORTL, prop.sortl);
    }
    if (test_bit(S390_FEAT_DEFLATE_BASE, features)) {
        s390_fill_feat_block(features, S390_FEAT_TYPE_DFLTCC, prop.dfltcc);
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
    struct kvm_s390_vm_cpu_feat prop = {};
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

    /* PTFF subfunctions might be indicated although kernel support missing */
    if (!test_bit(S390_FEAT_MULTIPLE_EPOCH, model->features)) {
        clear_bit(S390_FEAT_PTFF_QSIE, model->features);
        clear_bit(S390_FEAT_PTFF_QTOUE, model->features);
        clear_bit(S390_FEAT_PTFF_STOE, model->features);
        clear_bit(S390_FEAT_PTFF_STOUE, model->features);
    }

    /* with cpu model support, CMM is only indicated if really available */
    if (kvm_s390_cmma_available()) {
        set_bit(S390_FEAT_CMM, model->features);
    } else {
        /* no cmm -> no cmm nt */
        clear_bit(S390_FEAT_CMM_NT, model->features);
    }

    /* bpb needs kernel support for migration, VSIE and reset */
    if (!kvm_check_extension(kvm_state, KVM_CAP_S390_BPB)) {
        clear_bit(S390_FEAT_BPB, model->features);
    }

    /*
     * If we have support for protected virtualization, indicate
     * the protected virtualization IPL unpack facility.
     */
    if (cap_protected) {
        set_bit(S390_FEAT_UNPACK, model->features);
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
    /* for now, we can only provide the AP feature with HW support */
    if (kvm_vm_check_attr(kvm_state, KVM_S390_VM_CRYPTO,
        KVM_S390_VM_CRYPTO_ENABLE_APIE)) {
        set_bit(S390_FEAT_AP, model->features);
    }

    /*
     * Extended-Length SCCB is handled entirely within QEMU.
     * For PV guests this is completely fenced by the Ultravisor, as Service
     * Call error checking and STFLE interpretation are handled via SIE.
     */
    set_bit(S390_FEAT_EXTENDED_LENGTH_SCCB, model->features);

    if (kvm_check_extension(kvm_state, KVM_CAP_S390_DIAG318)) {
        set_bit(S390_FEAT_DIAG_318, model->features);
    }

    /* strip of features that are not part of the maximum model */
    bitmap_and(model->features, model->features, model->def->full_feat,
               S390_FEAT_MAX);
}

static void kvm_s390_configure_apie(bool interpret)
{
    uint64_t attr = interpret ? KVM_S390_VM_CRYPTO_ENABLE_APIE :
                                KVM_S390_VM_CRYPTO_DISABLE_APIE;

    if (kvm_vm_check_attr(kvm_state, KVM_S390_VM_CRYPTO, attr)) {
        kvm_s390_set_attr(attr);
    }
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

    if (test_bit(S390_FEAT_AP, model->features)) {
        kvm_s390_configure_apie(true);
    }
}

void kvm_s390_restart_interrupt(S390CPU *cpu)
{
    struct kvm_s390_irq irq = {
        .type = KVM_S390_RESTART,
    };

    kvm_s390_vcpu_interrupt(cpu, &irq);
}

void kvm_s390_stop_interrupt(S390CPU *cpu)
{
    struct kvm_s390_irq irq = {
        .type = KVM_S390_SIGP_STOP,
    };

    kvm_s390_vcpu_interrupt(cpu, &irq);
}

bool kvm_arch_cpu_check_are_resettable(void)
{
    return true;
}

int kvm_s390_get_zpci_op(void)
{
    return cap_zpci_op;
}

void kvm_arch_accel_class_init(ObjectClass *oc)
{
}
