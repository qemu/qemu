/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch KVM
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/kvm.h>
#include "asm-loongarch/kvm_para.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "system/system.h"
#include "system/kvm.h"
#include "system/kvm_int.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "system/address-spaces.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/loongarch/virt.h"
#include "qemu/log.h"
#include "hw/loader.h"
#include "system/runstate.h"
#include "cpu-csr.h"
#include "kvm_loongarch.h"
#include "trace.h"

static bool cap_has_mp_state;
static unsigned int brk_insn;
const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static int kvm_get_stealtime(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    int err;
    struct kvm_device_attr attr = {
        .group = KVM_LOONGARCH_VCPU_PVTIME_CTRL,
        .attr = KVM_LOONGARCH_VCPU_PVTIME_GPA,
        .addr = (uint64_t)&env->stealtime.guest_addr,
    };

    err = kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, attr);
    if (err) {
        return 0;
    }

    err = kvm_vcpu_ioctl(cs, KVM_GET_DEVICE_ATTR, attr);
    if (err) {
        error_report("PVTIME: KVM_GET_DEVICE_ATTR: %s", strerror(errno));
        return err;
    }

    return 0;
}

static int kvm_set_stealtime(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    int err;
    struct kvm_device_attr attr = {
        .group = KVM_LOONGARCH_VCPU_PVTIME_CTRL,
        .attr = KVM_LOONGARCH_VCPU_PVTIME_GPA,
        .addr = (uint64_t)&env->stealtime.guest_addr,
    };

    err = kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, attr);
    if (err) {
        return 0;
    }

    err = kvm_vcpu_ioctl(cs, KVM_SET_DEVICE_ATTR, attr);
    if (err) {
        error_report("PVTIME: KVM_SET_DEVICE_ATTR %s with gpa "TARGET_FMT_lx,
                      strerror(errno), env->stealtime.guest_addr);
        return err;
    }

    return 0;
}

static int kvm_set_pv_features(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    int err;
    uint64_t val;
    struct kvm_device_attr attr = {
        .group = KVM_LOONGARCH_VCPU_CPUCFG,
        .attr = CPUCFG_KVM_FEATURE,
        .addr = (uint64_t)&val,
    };

    err = kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, attr);
    if (err) {
        return 0;
    }

    val = env->pv_features;
    err = kvm_vcpu_ioctl(cs, KVM_SET_DEVICE_ATTR, attr);
    if (err) {
        error_report("Fail to set pv feature "TARGET_FMT_lx " with error %s",
                      val, strerror(errno));
        return err;
    }

    return 0;
}

static int kvm_loongarch_get_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    struct kvm_regs regs;
    CPULoongArchState *env = cpu_env(cs);

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
    CPULoongArchState *env = cpu_env(cs);

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
    CPULoongArchState *env = cpu_env(cs);

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
    CPULoongArchState *env = cpu_env(cs);

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
    CPULoongArchState *env = cpu_env(cs);

    ret = kvm_vcpu_ioctl(cs, KVM_GET_FPU, &fpu);
    if (ret < 0) {
        trace_kvm_failed_get_fpu(strerror(errno));
        return ret;
    }

    env->fcsr0 = fpu.fcsr;
    for (i = 0; i < 32; i++) {
        env->fpr[i].vreg.UD[0] = fpu.fpr[i].val64[0];
        env->fpr[i].vreg.UD[1] = fpu.fpr[i].val64[1];
        env->fpr[i].vreg.UD[2] = fpu.fpr[i].val64[2];
        env->fpr[i].vreg.UD[3] = fpu.fpr[i].val64[3];
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
    CPULoongArchState *env = cpu_env(cs);

    fpu.fcsr = env->fcsr0;
    fpu.fcc = 0;
    for (i = 0; i < 32; i++) {
        fpu.fpr[i].val64[0] = env->fpr[i].vreg.UD[0];
        fpu.fpr[i].val64[1] = env->fpr[i].vreg.UD[1];
        fpu.fpr[i].val64[2] = env->fpr[i].vreg.UD[2];
        fpu.fpr[i].val64[3] = env->fpr[i].vreg.UD[3];
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

static int kvm_loongarch_put_lbt(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    uint64_t val;
    int ret;

    /* check whether vm support LBT firstly */
    if (FIELD_EX32(env->cpucfg[2], CPUCFG2, LBT_ALL) != 7) {
        return 0;
    }

    /* set six LBT registers including scr0-scr3, eflags, ftop */
    ret = kvm_set_one_reg(cs, KVM_REG_LOONGARCH_LBT_SCR0, &env->lbt.scr0);
    ret |= kvm_set_one_reg(cs, KVM_REG_LOONGARCH_LBT_SCR1, &env->lbt.scr1);
    ret |= kvm_set_one_reg(cs, KVM_REG_LOONGARCH_LBT_SCR2, &env->lbt.scr2);
    ret |= kvm_set_one_reg(cs, KVM_REG_LOONGARCH_LBT_SCR3, &env->lbt.scr3);
    /*
     * Be cautious, KVM_REG_LOONGARCH_LBT_FTOP is defined as 64-bit however
     * lbt.ftop is 32-bit; the same with KVM_REG_LOONGARCH_LBT_EFLAGS register
     */
    val = env->lbt.eflags;
    ret |= kvm_set_one_reg(cs, KVM_REG_LOONGARCH_LBT_EFLAGS, &val);
    val = env->lbt.ftop;
    ret |= kvm_set_one_reg(cs, KVM_REG_LOONGARCH_LBT_FTOP, &val);

    return ret;
}

static int kvm_loongarch_get_lbt(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    uint64_t val;
    int ret;

    /* check whether vm support LBT firstly */
    if (FIELD_EX32(env->cpucfg[2], CPUCFG2, LBT_ALL) != 7) {
        return 0;
    }

    /* get six LBT registers including scr0-scr3, eflags, ftop */
    ret = kvm_get_one_reg(cs, KVM_REG_LOONGARCH_LBT_SCR0, &env->lbt.scr0);
    ret |= kvm_get_one_reg(cs, KVM_REG_LOONGARCH_LBT_SCR1, &env->lbt.scr1);
    ret |= kvm_get_one_reg(cs, KVM_REG_LOONGARCH_LBT_SCR2, &env->lbt.scr2);
    ret |= kvm_get_one_reg(cs, KVM_REG_LOONGARCH_LBT_SCR3, &env->lbt.scr3);
    ret |= kvm_get_one_reg(cs, KVM_REG_LOONGARCH_LBT_EFLAGS, &val);
    env->lbt.eflags = (uint32_t)val;
    ret |= kvm_get_one_reg(cs, KVM_REG_LOONGARCH_LBT_FTOP, &val);
    env->lbt.ftop = (uint32_t)val;

    return ret;
}

void kvm_arch_reset_vcpu(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    int ret = 0;
    uint64_t unused = 0;

    env->mp_state = KVM_MP_STATE_RUNNABLE;
    ret = kvm_set_one_reg(cs, KVM_REG_LOONGARCH_VCPU_RESET, &unused);
    if (ret) {
        error_report("Failed to set KVM_REG_LOONGARCH_VCPU_RESET: %s",
                     strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static int kvm_loongarch_get_mpstate(CPUState *cs)
{
    int ret = 0;
    struct kvm_mp_state mp_state;
    CPULoongArchState *env = cpu_env(cs);

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
    struct kvm_mp_state mp_state = {
        .mp_state = cpu_env(cs)->mp_state
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
    CPULoongArchState *env = cpu_env(cs);

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
    CPULoongArchState *env = cpu_env(cs);

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
    CPULoongArchState *env = cpu_env(cs);
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

int kvm_arch_get_registers(CPUState *cs, Error **errp)
{
    int ret;

    ret = kvm_loongarch_get_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_cpucfg(cs);
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

    ret = kvm_loongarch_get_lbt(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_get_stealtime(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_mpstate(cs);
    return ret;
}

int kvm_arch_put_registers(CPUState *cs, int level, Error **errp)
{
    int ret;
    static int once;

    ret = kvm_loongarch_put_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_cpucfg(cs);
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

    ret = kvm_loongarch_put_lbt(cs);
    if (ret) {
        return ret;
    }

    if (!once) {
        ret = kvm_set_pv_features(cs);
        if (ret) {
            return ret;
        }
        once = 1;
    }

    if (level >= KVM_PUT_FULL_STATE) {
        /*
         * only KVM_PUT_FULL_STATE is required, kvm kernel will clear
         * guest_addr for KVM_PUT_RESET_STATE
         */
        ret = kvm_set_stealtime(cs);
        if (ret) {
            return ret;
        }
    }

    ret = kvm_loongarch_put_mpstate(cs);
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

static bool kvm_feature_supported(CPUState *cs, enum loongarch_features feature)
{
    int ret;
    struct kvm_device_attr attr;
    uint64_t val;

    switch (feature) {
    case LOONGARCH_FEATURE_LSX:
        attr.group = KVM_LOONGARCH_VM_FEAT_CTRL;
        attr.attr = KVM_LOONGARCH_VM_FEAT_LSX;
        ret = kvm_vm_ioctl(kvm_state, KVM_HAS_DEVICE_ATTR, &attr);
        if (ret == 0) {
            return true;
        }

        /* Fallback to old kernel detect interface */
        val = 0;
        attr.group = KVM_LOONGARCH_VCPU_CPUCFG;
        /* Cpucfg2 */
        attr.attr  = 2;
        attr.addr = (uint64_t)&val;
        ret = kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, &attr);
        if (!ret) {
            ret = kvm_vcpu_ioctl(cs, KVM_GET_DEVICE_ATTR, &attr);
            if (ret) {
                return false;
            }

            ret = FIELD_EX32((uint32_t)val, CPUCFG2, LSX);
            return (ret != 0);
        }
        return false;

    case LOONGARCH_FEATURE_LASX:
        attr.group = KVM_LOONGARCH_VM_FEAT_CTRL;
        attr.attr = KVM_LOONGARCH_VM_FEAT_LASX;
        ret = kvm_vm_ioctl(kvm_state, KVM_HAS_DEVICE_ATTR, &attr);
        if (ret == 0) {
            return true;
        }

        /* Fallback to old kernel detect interface */
        val = 0;
        attr.group = KVM_LOONGARCH_VCPU_CPUCFG;
        /* Cpucfg2 */
        attr.attr  = 2;
        attr.addr = (uint64_t)&val;
        ret = kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, &attr);
        if (!ret) {
            ret = kvm_vcpu_ioctl(cs, KVM_GET_DEVICE_ATTR, &attr);
            if (ret) {
                return false;
            }

            ret = FIELD_EX32((uint32_t)val, CPUCFG2, LASX);
            return (ret != 0);
        }
        return false;

    case LOONGARCH_FEATURE_LBT:
        /*
         * Return all if all the LBT features are supported such as:
         *  KVM_LOONGARCH_VM_FEAT_X86BT
         *  KVM_LOONGARCH_VM_FEAT_ARMBT
         *  KVM_LOONGARCH_VM_FEAT_MIPSBT
         */
        attr.group = KVM_LOONGARCH_VM_FEAT_CTRL;
        attr.attr = KVM_LOONGARCH_VM_FEAT_X86BT;
        ret = kvm_vm_ioctl(kvm_state, KVM_HAS_DEVICE_ATTR, &attr);
        attr.attr = KVM_LOONGARCH_VM_FEAT_ARMBT;
        ret |= kvm_vm_ioctl(kvm_state, KVM_HAS_DEVICE_ATTR, &attr);
        attr.attr = KVM_LOONGARCH_VM_FEAT_MIPSBT;
        ret |= kvm_vm_ioctl(kvm_state, KVM_HAS_DEVICE_ATTR, &attr);
        return (ret == 0);

    case LOONGARCH_FEATURE_PMU:
        attr.group = KVM_LOONGARCH_VM_FEAT_CTRL;
        attr.attr = KVM_LOONGARCH_VM_FEAT_PMU;
        ret = kvm_vm_ioctl(kvm_state, KVM_HAS_DEVICE_ATTR, &attr);
        return (ret == 0);

    case LOONGARCH_FEATURE_PV_IPI:
        attr.group = KVM_LOONGARCH_VM_FEAT_CTRL;
        attr.attr = KVM_LOONGARCH_VM_FEAT_PV_IPI;
        ret = kvm_vm_ioctl(kvm_state, KVM_HAS_DEVICE_ATTR, &attr);
        return (ret == 0);

    case LOONGARCH_FEATURE_STEALTIME:
        attr.group = KVM_LOONGARCH_VM_FEAT_CTRL;
        attr.attr = KVM_LOONGARCH_VM_FEAT_PV_STEALTIME;
        ret = kvm_vm_ioctl(kvm_state, KVM_HAS_DEVICE_ATTR, &attr);
        return (ret == 0);

    default:
        return false;
    }

    return false;
}

static int kvm_cpu_check_lsx(CPUState *cs, Error **errp)
{
    CPULoongArchState *env = cpu_env(cs);
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    bool kvm_supported;

    kvm_supported = kvm_feature_supported(cs, LOONGARCH_FEATURE_LSX);
    env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LSX, 0);
    if (cpu->lsx == ON_OFF_AUTO_ON) {
        if (kvm_supported) {
            env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LSX, 1);
        } else {
            error_setg(errp, "'lsx' feature not supported by KVM on this host");
            return -ENOTSUP;
        }
    } else if ((cpu->lsx == ON_OFF_AUTO_AUTO) && kvm_supported) {
        env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LSX, 1);
    }

    return 0;
}

static int kvm_cpu_check_lasx(CPUState *cs, Error **errp)
{
    CPULoongArchState *env = cpu_env(cs);
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    bool kvm_supported;

    kvm_supported = kvm_feature_supported(cs, LOONGARCH_FEATURE_LASX);
    env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LASX, 0);
    if (cpu->lasx == ON_OFF_AUTO_ON) {
        if (kvm_supported) {
            env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LASX, 1);
        } else {
            error_setg(errp, "'lasx' feature not supported by KVM on host");
            return -ENOTSUP;
        }
    } else if ((cpu->lasx == ON_OFF_AUTO_AUTO) && kvm_supported) {
        env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LASX, 1);
    }

    return 0;
}

static int kvm_cpu_check_lbt(CPUState *cs, Error **errp)
{
    CPULoongArchState *env = cpu_env(cs);
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    bool kvm_supported;

    kvm_supported = kvm_feature_supported(cs, LOONGARCH_FEATURE_LBT);
    if (cpu->lbt == ON_OFF_AUTO_ON) {
        if (kvm_supported) {
            env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LBT_ALL, 7);
        } else {
            error_setg(errp, "'lbt' feature not supported by KVM on this host");
            return -ENOTSUP;
        }
    } else if ((cpu->lbt == ON_OFF_AUTO_AUTO) && kvm_supported) {
        env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LBT_ALL, 7);
    }

    return 0;
}

static int kvm_cpu_check_pmu(CPUState *cs, Error **errp)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = cpu_env(cs);
    bool kvm_supported;

    kvm_supported = kvm_feature_supported(cs, LOONGARCH_FEATURE_PMU);
    if (cpu->pmu == ON_OFF_AUTO_ON) {
        if (!kvm_supported) {
            error_setg(errp, "'pmu' feature not supported by KVM on the host");
            return -ENOTSUP;
        }
    } else if (cpu->pmu != ON_OFF_AUTO_AUTO) {
        /* disable pmu if ON_OFF_AUTO_OFF is set */
        kvm_supported = false;
    }

    if (kvm_supported) {
        env->cpucfg[6] = FIELD_DP32(env->cpucfg[6], CPUCFG6, PMP, 1);
        env->cpucfg[6] = FIELD_DP32(env->cpucfg[6], CPUCFG6, PMNUM, 3);
        env->cpucfg[6] = FIELD_DP32(env->cpucfg[6], CPUCFG6, PMBITS, 63);
        env->cpucfg[6] = FIELD_DP32(env->cpucfg[6], CPUCFG6, UPM, 1);
    }
    return 0;
}

static int kvm_cpu_check_pv_features(CPUState *cs, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = cpu_env(cs);
    bool kvm_supported;

    kvm_supported = kvm_feature_supported(cs, LOONGARCH_FEATURE_PV_IPI);
    if (cpu->kvm_pv_ipi == ON_OFF_AUTO_ON) {
        if (!kvm_supported) {
            error_setg(errp, "'pv_ipi' feature not supported by KVM host");
            return -ENOTSUP;
        }
    } else if (cpu->kvm_pv_ipi != ON_OFF_AUTO_AUTO) {
        kvm_supported = false;
    }

    if (kvm_supported) {
        env->pv_features |= BIT(KVM_FEATURE_IPI);
    }

    kvm_supported = kvm_feature_supported(cs, LOONGARCH_FEATURE_STEALTIME);
    if (cpu->kvm_steal_time == ON_OFF_AUTO_ON) {
        if (!kvm_supported) {
            error_setg(errp, "'kvm stealtime' feature not supported by KVM host");
            return -ENOTSUP;
        }
    } else if (cpu->kvm_steal_time != ON_OFF_AUTO_AUTO) {
        kvm_supported = false;
    }

    if (kvm_supported) {
        env->pv_features |= BIT(KVM_FEATURE_STEAL_TIME);
    }

    if (object_dynamic_cast(OBJECT(ms), TYPE_LOONGARCH_VIRT_MACHINE)) {
        LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(ms);

        if (virt_is_veiointc_enabled(lvms)) {
            env->pv_features |= BIT(KVM_FEATURE_VIRT_EXTIOI);
        }
    }

    return 0;
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    uint64_t val;
    int ret;
    Error *local_err = NULL;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);

    cpu->vmsentry = qemu_add_vm_change_state_handler(
                    kvm_loongarch_vm_stage_change, cs);

    if (!kvm_get_one_reg(cs, KVM_REG_LOONGARCH_DEBUG_INST, &val)) {
        brk_insn = val;
    }

    ret = kvm_cpu_check_lsx(cs, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return ret;
    }

    ret = kvm_cpu_check_lasx(cs, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return ret;
    }

    ret = kvm_cpu_check_lbt(cs, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return ret;
    }

    ret = kvm_cpu_check_pmu(cs, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return ret;
    }

    ret = kvm_cpu_check_pv_features(cs, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return ret;
    }

    return 0;
}

static bool loongarch_get_lbt(Object *obj, Error **errp)
{
    return LOONGARCH_CPU(obj)->lbt != ON_OFF_AUTO_OFF;
}

static void loongarch_set_lbt(Object *obj, bool value, Error **errp)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);

    cpu->lbt = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
}

static bool loongarch_get_pmu(Object *obj, Error **errp)
{
    return LOONGARCH_CPU(obj)->pmu != ON_OFF_AUTO_OFF;
}

static void loongarch_set_pmu(Object *obj, bool value, Error **errp)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);

    cpu->pmu = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
}

static bool kvm_pv_ipi_get(Object *obj, Error **errp)
{
    return LOONGARCH_CPU(obj)->kvm_pv_ipi != ON_OFF_AUTO_OFF;
}

static void kvm_pv_ipi_set(Object *obj, bool value, Error **errp)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);

    cpu->kvm_pv_ipi = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
}

static bool kvm_steal_time_get(Object *obj, Error **errp)
{
    return LOONGARCH_CPU(obj)->kvm_steal_time != ON_OFF_AUTO_OFF;
}

static void kvm_steal_time_set(Object *obj, bool value, Error **errp)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);

    cpu->kvm_steal_time = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
}

void kvm_loongarch_cpu_post_init(LoongArchCPU *cpu)
{
    cpu->lbt = ON_OFF_AUTO_AUTO;
    object_property_add_bool(OBJECT(cpu), "lbt", loongarch_get_lbt,
                             loongarch_set_lbt);
    object_property_set_description(OBJECT(cpu), "lbt",
                                   "Set off to disable Binary Tranlation.");

    cpu->pmu = ON_OFF_AUTO_AUTO;
    object_property_add_bool(OBJECT(cpu), "pmu", loongarch_get_pmu,
                             loongarch_set_pmu);
    object_property_set_description(OBJECT(cpu), "pmu",
                               "Set off to disable performance monitor unit.");

    cpu->kvm_pv_ipi = ON_OFF_AUTO_AUTO;
    object_property_add_bool(OBJECT(cpu), "kvm-pv-ipi", kvm_pv_ipi_get,
                             kvm_pv_ipi_set);
    object_property_set_description(OBJECT(cpu), "kvm-pv-ipi",
                                    "Set off to disable KVM paravirt IPI.");

    cpu->kvm_steal_time = ON_OFF_AUTO_AUTO;
    object_property_add_bool(OBJECT(cpu), "kvm-steal-time", kvm_steal_time_get,
                             kvm_steal_time_set);
    object_property_set_description(OBJECT(cpu), "kvm-steal-time",
                                    "Set off to disable KVM steal time.");
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);

    qemu_del_vm_change_state_handler(cpu->vmsentry);
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

void kvm_arch_update_guest_debug(CPUState *cpu, struct kvm_guest_debug *dbg)
{
    if (kvm_sw_breakpoints_active(cpu)) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP;
    }
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 4, 0) ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&brk_insn, 4, 1)) {
        error_report("%s failed", __func__);
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    static uint32_t brk;

    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&brk, 4, 0) ||
        brk != brk_insn ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 4, 1)) {
        error_report("%s failed", __func__);
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_insert_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    return -ENOSYS;
}

int kvm_arch_remove_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    return -ENOSYS;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
}

static bool kvm_loongarch_handle_debug(CPUState *cs, struct kvm_run *run)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    kvm_cpu_synchronize_state(cs);
    if (cs->singlestep_enabled) {
        return true;
    }

    if (kvm_find_sw_breakpoint(cs, env->pc)) {
        return true;
    }

    return false;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    int ret = 0;
    CPULoongArchState *env = cpu_env(cs);
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

    case KVM_EXIT_DEBUG:
        if (kvm_loongarch_handle_debug(cs, run)) {
            ret = EXCP_DEBUG;
        }
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
