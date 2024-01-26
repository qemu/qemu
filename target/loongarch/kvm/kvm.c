/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch KVM
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "hw/loader.h"
#include "migration/migration.h"
#include "sysemu/runstate.h"
#include "cpu-csr.h"
#include "kvm_loongarch.h"
#include "trace.h"

static bool cap_has_mp_state;
const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static int kvm_loongarch_get_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    struct kvm_regs regs;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    /* Get the current register set as KVM seems it */
    ret = kvm_vcpu_ioctl(cs, KVM_GET_REGS, &regs);
    if (ret < 0) {
        trace_kvm_failed_get_regs_core(strerror(errno));
        return ret;
    }
    /* gpr[0] value is always 0 */
    env->gpr[0] = 0;
    for (i = 1; i < 32; i++) {
        env->gpr[i] = regs.gpr[i];
    }

    env->pc = regs.pc;
    return ret;
}

static int kvm_loongarch_put_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    struct kvm_regs regs;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    /* Set the registers based on QEMU's view of things */
    for (i = 0; i < 32; i++) {
        regs.gpr[i] = env->gpr[i];
    }

    regs.pc = env->pc;
    ret = kvm_vcpu_ioctl(cs, KVM_SET_REGS, &regs);
    if (ret < 0) {
        trace_kvm_failed_put_regs_core(strerror(errno));
    }

    return ret;
}

static int kvm_loongarch_get_csr(CPUState *cs)
{
    int ret = 0;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_CRMD),
                           &env->CSR_CRMD);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PRMD),
                           &env->CSR_PRMD);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_EUEN),
                           &env->CSR_EUEN);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_MISC),
                           &env->CSR_MISC);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_ECFG),
                           &env->CSR_ECFG);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_ESTAT),
                           &env->CSR_ESTAT);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_ERA),
                           &env->CSR_ERA);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_BADV),
                           &env->CSR_BADV);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_BADI),
                           &env->CSR_BADI);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_EENTRY),
                           &env->CSR_EENTRY);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBIDX),
                           &env->CSR_TLBIDX);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBEHI),
                           &env->CSR_TLBEHI);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBELO0),
                           &env->CSR_TLBELO0);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBELO1),
                           &env->CSR_TLBELO1);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_ASID),
                           &env->CSR_ASID);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PGDL),
                           &env->CSR_PGDL);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PGDH),
                           &env->CSR_PGDH);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PGD),
                           &env->CSR_PGD);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PWCL),
                           &env->CSR_PWCL);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PWCH),
                           &env->CSR_PWCH);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_STLBPS),
                           &env->CSR_STLBPS);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_RVACFG),
                           &env->CSR_RVACFG);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_CPUID),
                           &env->CSR_CPUID);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PRCFG1),
                           &env->CSR_PRCFG1);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PRCFG2),
                           &env->CSR_PRCFG2);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PRCFG3),
                           &env->CSR_PRCFG3);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(0)),
                           &env->CSR_SAVE[0]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(1)),
                           &env->CSR_SAVE[1]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(2)),
                           &env->CSR_SAVE[2]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(3)),
                           &env->CSR_SAVE[3]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(4)),
                           &env->CSR_SAVE[4]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(5)),
                           &env->CSR_SAVE[5]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(6)),
                           &env->CSR_SAVE[6]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(7)),
                           &env->CSR_SAVE[7]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TID),
                           &env->CSR_TID);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_CNTC),
                           &env->CSR_CNTC);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TICLR),
                           &env->CSR_TICLR);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_LLBCTL),
                           &env->CSR_LLBCTL);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_IMPCTL1),
                           &env->CSR_IMPCTL1);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_IMPCTL2),
                           &env->CSR_IMPCTL2);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRENTRY),
                           &env->CSR_TLBRENTRY);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRBADV),
                           &env->CSR_TLBRBADV);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRERA),
                           &env->CSR_TLBRERA);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRSAVE),
                           &env->CSR_TLBRSAVE);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRELO0),
                           &env->CSR_TLBRELO0);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRELO1),
                           &env->CSR_TLBRELO1);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBREHI),
                           &env->CSR_TLBREHI);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRPRMD),
                           &env->CSR_TLBRPRMD);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_DMW(0)),
                           &env->CSR_DMW[0]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_DMW(1)),
                           &env->CSR_DMW[1]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_DMW(2)),
                           &env->CSR_DMW[2]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_DMW(3)),
                           &env->CSR_DMW[3]);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TVAL),
                           &env->CSR_TVAL);

    ret |= kvm_get_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TCFG),
                           &env->CSR_TCFG);

    return ret;
}

static int kvm_loongarch_put_csr(CPUState *cs, int level)
{
    int ret = 0;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_CRMD),
                           &env->CSR_CRMD);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PRMD),
                           &env->CSR_PRMD);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_EUEN),
                           &env->CSR_EUEN);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_MISC),
                           &env->CSR_MISC);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_ECFG),
                           &env->CSR_ECFG);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_ESTAT),
                           &env->CSR_ESTAT);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_ERA),
                           &env->CSR_ERA);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_BADV),
                           &env->CSR_BADV);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_BADI),
                           &env->CSR_BADI);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_EENTRY),
                           &env->CSR_EENTRY);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBIDX),
                           &env->CSR_TLBIDX);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBEHI),
                           &env->CSR_TLBEHI);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBELO0),
                           &env->CSR_TLBELO0);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBELO1),
                           &env->CSR_TLBELO1);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_ASID),
                           &env->CSR_ASID);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PGDL),
                           &env->CSR_PGDL);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PGDH),
                           &env->CSR_PGDH);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PGD),
                           &env->CSR_PGD);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PWCL),
                           &env->CSR_PWCL);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PWCH),
                           &env->CSR_PWCH);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_STLBPS),
                           &env->CSR_STLBPS);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_RVACFG),
                           &env->CSR_RVACFG);

    /* CPUID is constant after poweron, it should be set only once */
    if (level >= KVM_PUT_FULL_STATE) {
        ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_CPUID),
                           &env->CSR_CPUID);
    }

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PRCFG1),
                           &env->CSR_PRCFG1);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PRCFG2),
                           &env->CSR_PRCFG2);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_PRCFG3),
                           &env->CSR_PRCFG3);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(0)),
                           &env->CSR_SAVE[0]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(1)),
                           &env->CSR_SAVE[1]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(2)),
                           &env->CSR_SAVE[2]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(3)),
                           &env->CSR_SAVE[3]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(4)),
                           &env->CSR_SAVE[4]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(5)),
                           &env->CSR_SAVE[5]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(6)),
                           &env->CSR_SAVE[6]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_SAVE(7)),
                           &env->CSR_SAVE[7]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TID),
                           &env->CSR_TID);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_CNTC),
                           &env->CSR_CNTC);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TICLR),
                           &env->CSR_TICLR);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_LLBCTL),
                           &env->CSR_LLBCTL);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_IMPCTL1),
                           &env->CSR_IMPCTL1);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_IMPCTL2),
                           &env->CSR_IMPCTL2);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRENTRY),
                           &env->CSR_TLBRENTRY);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRBADV),
                           &env->CSR_TLBRBADV);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRERA),
                           &env->CSR_TLBRERA);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRSAVE),
                           &env->CSR_TLBRSAVE);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRELO0),
                           &env->CSR_TLBRELO0);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRELO1),
                           &env->CSR_TLBRELO1);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBREHI),
                           &env->CSR_TLBREHI);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TLBRPRMD),
                           &env->CSR_TLBRPRMD);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_DMW(0)),
                           &env->CSR_DMW[0]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_DMW(1)),
                           &env->CSR_DMW[1]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_DMW(2)),
                           &env->CSR_DMW[2]);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_DMW(3)),
                           &env->CSR_DMW[3]);
    /*
     * timer cfg must be put at last since it is used to enable
     * guest timer
     */
    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TVAL),
                           &env->CSR_TVAL);

    ret |= kvm_set_one_reg(cs, KVM_IOC_CSRID(LOONGARCH_CSR_TCFG),
                           &env->CSR_TCFG);
    return ret;
}

static int kvm_loongarch_get_regs_fp(CPUState *cs)
{
    int ret, i;
    struct kvm_fpu fpu;

    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    ret = kvm_vcpu_ioctl(cs, KVM_GET_FPU, &fpu);
    if (ret < 0) {
        trace_kvm_failed_get_fpu(strerror(errno));
        return ret;
    }

    env->fcsr0 = fpu.fcsr;
    for (i = 0; i < 32; i++) {
        env->fpr[i].vreg.UD[0] = fpu.fpr[i].val64[0];
    }
    for (i = 0; i < 8; i++) {
        env->cf[i] = fpu.fcc & 0xFF;
        fpu.fcc = fpu.fcc >> 8;
    }

    return ret;
}

static int kvm_loongarch_put_regs_fp(CPUState *cs)
{
    int ret, i;
    struct kvm_fpu fpu;

    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    fpu.fcsr = env->fcsr0;
    fpu.fcc = 0;
    for (i = 0; i < 32; i++) {
        fpu.fpr[i].val64[0] = env->fpr[i].vreg.UD[0];
    }

    for (i = 0; i < 8; i++) {
        fpu.fcc |= env->cf[i] << (8 * i);
    }

    ret = kvm_vcpu_ioctl(cs, KVM_SET_FPU, &fpu);
    if (ret < 0) {
        trace_kvm_failed_put_fpu(strerror(errno));
    }

    return ret;
}

void kvm_arch_reset_vcpu(CPULoongArchState *env)
{
    env->mp_state = KVM_MP_STATE_RUNNABLE;
}

static int kvm_loongarch_get_mpstate(CPUState *cs)
{
    int ret = 0;
    struct kvm_mp_state mp_state;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    if (cap_has_mp_state) {
        ret = kvm_vcpu_ioctl(cs, KVM_GET_MP_STATE, &mp_state);
        if (ret) {
            trace_kvm_failed_get_mpstate(strerror(errno));
            return ret;
        }
        env->mp_state = mp_state.mp_state;
    }

    return ret;
}

static int kvm_loongarch_put_mpstate(CPUState *cs)
{
    int ret = 0;

    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    struct kvm_mp_state mp_state = {
        .mp_state = env->mp_state
    };

    if (cap_has_mp_state) {
        ret = kvm_vcpu_ioctl(cs, KVM_SET_MP_STATE, &mp_state);
        if (ret) {
            trace_kvm_failed_put_mpstate(strerror(errno));
        }
    }

    return ret;
}

static int kvm_loongarch_get_cpucfg(CPUState *cs)
{
    int i, ret = 0;
    uint64_t val;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    for (i = 0; i < 21; i++) {
        ret = kvm_get_one_reg(cs, KVM_IOC_CPUCFG(i), &val);
        if (ret < 0) {
            trace_kvm_failed_get_cpucfg(strerror(errno));
        }
        env->cpucfg[i] = (uint32_t)val;
    }
    return ret;
}

static int kvm_check_cpucfg2(CPUState *cs)
{
    int ret;
    uint64_t val;
    struct kvm_device_attr attr = {
        .group = KVM_LOONGARCH_VCPU_CPUCFG,
        .attr = 2,
        .addr = (uint64_t)&val,
    };
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    ret = kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, &attr);

    if (!ret) {
        kvm_vcpu_ioctl(cs, KVM_GET_DEVICE_ATTR, &attr);
        env->cpucfg[2] &= val;

        if (FIELD_EX32(env->cpucfg[2], CPUCFG2, FP)) {
            /* The FP minimal version is 1. */
            env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, FP_VER, 1);
        }

        if (FIELD_EX32(env->cpucfg[2], CPUCFG2, LLFTP)) {
            /* The LLFTP minimal version is 1. */
            env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LLFTP_VER, 1);
        }
    }

    return ret;
}

static int kvm_loongarch_put_cpucfg(CPUState *cs)
{
    int i, ret = 0;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    uint64_t val;

    for (i = 0; i < 21; i++) {
	if (i == 2) {
            ret = kvm_check_cpucfg2(cs);
            if (ret) {
                return ret;
            }
	}
        val = env->cpucfg[i];
        ret = kvm_set_one_reg(cs, KVM_IOC_CPUCFG(i), &val);
        if (ret < 0) {
            trace_kvm_failed_put_cpucfg(strerror(errno));
        }
    }
    return ret;
}

int kvm_arch_get_registers(CPUState *cs)
{
    int ret;

    ret = kvm_loongarch_get_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_csr(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_regs_fp(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_mpstate(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_cpucfg(cs);
    return ret;
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    int ret;

    ret = kvm_loongarch_put_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_csr(cs, level);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_regs_fp(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_mpstate(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_cpucfg(cs);
    return ret;
}

static void kvm_loongarch_vm_stage_change(void *opaque, bool running,
                                          RunState state)
{
    int ret;
    CPUState *cs = opaque;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);

    if (running) {
        ret = kvm_set_one_reg(cs, KVM_REG_LOONGARCH_COUNTER,
                              &cpu->kvm_state_counter);
        if (ret < 0) {
            trace_kvm_failed_put_counter(strerror(errno));
        }
    } else {
        ret = kvm_get_one_reg(cs, KVM_REG_LOONGARCH_COUNTER,
                              &cpu->kvm_state_counter);
        if (ret < 0) {
            trace_kvm_failed_get_counter(strerror(errno));
        }
    }
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    qemu_add_vm_change_state_handler(kvm_loongarch_vm_stage_change, cs);
    return 0;
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cs)
{
    return cs->cpu_index;
}

int kvm_arch_release_virq_post(int virq)
{
    return 0;
}

int kvm_arch_msi_data_to_gsi(uint32_t data)
{
    abort();
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

void kvm_arch_init_irq_routing(KVMState *s)
{
}

int kvm_arch_get_default_type(MachineState *ms)
{
    return 0;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    cap_has_mp_state = kvm_check_extension(s, KVM_CAP_MP_STATE);
    return 0;
}

int kvm_arch_irqchip_create(KVMState *s)
{
    return 0;
}

void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
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

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    return true;
}

bool kvm_arch_cpu_check_are_resettable(void)
{
    return true;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    int ret = 0;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    MemTxAttrs attrs = {};

    attrs.requester_id = env_cpu(env)->cpu_index;

    trace_kvm_arch_handle_exit(run->exit_reason);
    switch (run->exit_reason) {
    case KVM_EXIT_LOONGARCH_IOCSR:
        address_space_rw(env->address_space_iocsr,
                         run->iocsr_io.phys_addr,
                         attrs,
                         run->iocsr_io.data,
                         run->iocsr_io.len,
                         run->iocsr_io.is_write);
        break;
    default:
        ret = -1;
        warn_report("KVM: unknown exit reason %d", run->exit_reason);
        break;
    }
    return ret;
}

int kvm_loongarch_set_interrupt(LoongArchCPU *cpu, int irq, int level)
{
    struct kvm_interrupt intr;
    CPUState *cs = CPU(cpu);

    if (level) {
        intr.irq = irq;
    } else {
        intr.irq = -irq;
    }

    trace_kvm_set_intr(irq, level);
    return kvm_vcpu_ioctl(cs, KVM_INTERRUPT, &intr);
}

void kvm_arch_accel_class_init(ObjectClass *oc)
{
}
