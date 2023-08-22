/*
 * RISC-V implementation of KVM hooks
 *
 * Copyright (c) 2020 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>

#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qapi/visitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "cpu.h"
#include "trace.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "hw/loader.h"
#include "kvm_riscv.h"
#include "sbi_ecall_interface.h"
#include "chardev/char-fe.h"
#include "migration/migration.h"
#include "sysemu/runstate.h"

static uint64_t kvm_riscv_reg_id(CPURISCVState *env, uint64_t type,
                                 uint64_t idx)
{
    uint64_t id = KVM_REG_RISCV | type | idx;

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        id |= KVM_REG_SIZE_U32;
        break;
    case MXL_RV64:
        id |= KVM_REG_SIZE_U64;
        break;
    default:
        g_assert_not_reached();
    }
    return id;
}

#define RISCV_CORE_REG(env, name)  kvm_riscv_reg_id(env, KVM_REG_RISCV_CORE, \
                 KVM_REG_RISCV_CORE_REG(name))

#define RISCV_CSR_REG(env, name)  kvm_riscv_reg_id(env, KVM_REG_RISCV_CSR, \
                 KVM_REG_RISCV_CSR_REG(name))

#define RISCV_TIMER_REG(env, name)  kvm_riscv_reg_id(env, KVM_REG_RISCV_TIMER, \
                 KVM_REG_RISCV_TIMER_REG(name))

#define RISCV_FP_F_REG(env, idx)  kvm_riscv_reg_id(env, KVM_REG_RISCV_FP_F, idx)

#define RISCV_FP_D_REG(env, idx)  kvm_riscv_reg_id(env, KVM_REG_RISCV_FP_D, idx)

#define KVM_RISCV_GET_CSR(cs, env, csr, reg) \
    do { \
        int ret = kvm_get_one_reg(cs, RISCV_CSR_REG(env, csr), &reg); \
        if (ret) { \
            return ret; \
        } \
    } while (0)

#define KVM_RISCV_SET_CSR(cs, env, csr, reg) \
    do { \
        int ret = kvm_set_one_reg(cs, RISCV_CSR_REG(env, csr), &reg); \
        if (ret) { \
            return ret; \
        } \
    } while (0)

#define KVM_RISCV_GET_TIMER(cs, env, name, reg) \
    do { \
        int ret = kvm_get_one_reg(cs, RISCV_TIMER_REG(env, name), &reg); \
        if (ret) { \
            abort(); \
        } \
    } while (0)

#define KVM_RISCV_SET_TIMER(cs, env, name, reg) \
    do { \
        int ret = kvm_set_one_reg(cs, RISCV_TIMER_REG(env, name), &reg); \
        if (ret) { \
            abort(); \
        } \
    } while (0)

typedef struct KVMCPUConfig {
    const char *name;
    const char *description;
    target_ulong offset;
    int kvm_reg_id;
    bool user_set;
    bool supported;
} KVMCPUConfig;

#define KVM_MISA_CFG(_bit, _reg_id) \
    {.offset = _bit, .kvm_reg_id = _reg_id}

/* KVM ISA extensions */
static KVMCPUConfig kvm_misa_ext_cfgs[] = {
    KVM_MISA_CFG(RVA, KVM_RISCV_ISA_EXT_A),
    KVM_MISA_CFG(RVC, KVM_RISCV_ISA_EXT_C),
    KVM_MISA_CFG(RVD, KVM_RISCV_ISA_EXT_D),
    KVM_MISA_CFG(RVF, KVM_RISCV_ISA_EXT_F),
    KVM_MISA_CFG(RVH, KVM_RISCV_ISA_EXT_H),
    KVM_MISA_CFG(RVI, KVM_RISCV_ISA_EXT_I),
    KVM_MISA_CFG(RVM, KVM_RISCV_ISA_EXT_M),
};

static void kvm_cpu_set_misa_ext_cfg(Object *obj, Visitor *v,
                                     const char *name,
                                     void *opaque, Error **errp)
{
    KVMCPUConfig *misa_ext_cfg = opaque;
    target_ulong misa_bit = misa_ext_cfg->offset;
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    bool value, host_bit;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    host_bit = env->misa_ext_mask & misa_bit;

    if (value == host_bit) {
        return;
    }

    if (!value) {
        misa_ext_cfg->user_set = true;
        return;
    }

    /*
     * Forbid users to enable extensions that aren't
     * available in the hart.
     */
    error_setg(errp, "Enabling MISA bit '%s' is not allowed: it's not "
               "enabled in the host", misa_ext_cfg->name);
}

static void kvm_riscv_update_cpu_misa_ext(RISCVCPU *cpu, CPUState *cs)
{
    CPURISCVState *env = &cpu->env;
    uint64_t id, reg;
    int i, ret;

    for (i = 0; i < ARRAY_SIZE(kvm_misa_ext_cfgs); i++) {
        KVMCPUConfig *misa_cfg = &kvm_misa_ext_cfgs[i];
        target_ulong misa_bit = misa_cfg->offset;

        if (!misa_cfg->user_set) {
            continue;
        }

        /* If we're here we're going to disable the MISA bit */
        reg = 0;
        id = kvm_riscv_reg_id(env, KVM_REG_RISCV_ISA_EXT,
                              misa_cfg->kvm_reg_id);
        ret = kvm_set_one_reg(cs, id, &reg);
        if (ret != 0) {
            /*
             * We're not checking for -EINVAL because if the bit is about
             * to be disabled, it means that it was already enabled by
             * KVM. We determined that by fetching the 'isa' register
             * during init() time. Any error at this point is worth
             * aborting.
             */
            error_report("Unable to set KVM reg %s, error %d",
                         misa_cfg->name, ret);
            exit(EXIT_FAILURE);
        }
        env->misa_ext &= ~misa_bit;
    }
}

#define CPUCFG(_prop) offsetof(struct RISCVCPUConfig, _prop)

#define KVM_EXT_CFG(_name, _prop, _reg_id) \
    {.name = _name, .offset = CPUCFG(_prop), \
     .kvm_reg_id = _reg_id}

static KVMCPUConfig kvm_multi_ext_cfgs[] = {
    KVM_EXT_CFG("zicbom", ext_icbom, KVM_RISCV_ISA_EXT_ZICBOM),
    KVM_EXT_CFG("zicboz", ext_icboz, KVM_RISCV_ISA_EXT_ZICBOZ),
    KVM_EXT_CFG("zihintpause", ext_zihintpause, KVM_RISCV_ISA_EXT_ZIHINTPAUSE),
    KVM_EXT_CFG("zbb", ext_zbb, KVM_RISCV_ISA_EXT_ZBB),
    KVM_EXT_CFG("ssaia", ext_ssaia, KVM_RISCV_ISA_EXT_SSAIA),
    KVM_EXT_CFG("sstc", ext_sstc, KVM_RISCV_ISA_EXT_SSTC),
    KVM_EXT_CFG("svinval", ext_svinval, KVM_RISCV_ISA_EXT_SVINVAL),
    KVM_EXT_CFG("svpbmt", ext_svpbmt, KVM_RISCV_ISA_EXT_SVPBMT),
};

static void *kvmconfig_get_cfg_addr(RISCVCPU *cpu, KVMCPUConfig *kvmcfg)
{
    return (void *)&cpu->cfg + kvmcfg->offset;
}

static void kvm_cpu_cfg_set(RISCVCPU *cpu, KVMCPUConfig *multi_ext,
                            uint32_t val)
{
    bool *ext_enabled = kvmconfig_get_cfg_addr(cpu, multi_ext);

    *ext_enabled = val;
}

static uint32_t kvm_cpu_cfg_get(RISCVCPU *cpu,
                                KVMCPUConfig *multi_ext)
{
    bool *ext_enabled = kvmconfig_get_cfg_addr(cpu, multi_ext);

    return *ext_enabled;
}

static void kvm_cpu_set_multi_ext_cfg(Object *obj, Visitor *v,
                                      const char *name,
                                      void *opaque, Error **errp)
{
    KVMCPUConfig *multi_ext_cfg = opaque;
    RISCVCPU *cpu = RISCV_CPU(obj);
    bool value, host_val;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    host_val = kvm_cpu_cfg_get(cpu, multi_ext_cfg);

    /*
     * Ignore if the user is setting the same value
     * as the host.
     */
    if (value == host_val) {
        return;
    }

    if (!multi_ext_cfg->supported) {
        /*
         * Error out if the user is trying to enable an
         * extension that KVM doesn't support. Ignore
         * option otherwise.
         */
        if (value) {
            error_setg(errp, "KVM does not support disabling extension %s",
                       multi_ext_cfg->name);
        }

        return;
    }

    multi_ext_cfg->user_set = true;
    kvm_cpu_cfg_set(cpu, multi_ext_cfg, value);
}

static KVMCPUConfig kvm_cbom_blocksize = {
    .name = "cbom_blocksize",
    .offset = CPUCFG(cbom_blocksize),
    .kvm_reg_id = KVM_REG_RISCV_CONFIG_REG(zicbom_block_size)
};

static KVMCPUConfig kvm_cboz_blocksize = {
    .name = "cboz_blocksize",
    .offset = CPUCFG(cboz_blocksize),
    .kvm_reg_id = KVM_REG_RISCV_CONFIG_REG(zicboz_block_size)
};

static void kvm_cpu_set_cbomz_blksize(Object *obj, Visitor *v,
                                      const char *name,
                                      void *opaque, Error **errp)
{
    KVMCPUConfig *cbomz_cfg = opaque;
    RISCVCPU *cpu = RISCV_CPU(obj);
    uint16_t value, *host_val;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    host_val = kvmconfig_get_cfg_addr(cpu, cbomz_cfg);

    if (value != *host_val) {
        error_report("Unable to set %s to a different value than "
                     "the host (%u)",
                     cbomz_cfg->name, *host_val);
        exit(EXIT_FAILURE);
    }

    cbomz_cfg->user_set = true;
}

static void kvm_riscv_update_cpu_cfg_isa_ext(RISCVCPU *cpu, CPUState *cs)
{
    CPURISCVState *env = &cpu->env;
    uint64_t id, reg;
    int i, ret;

    for (i = 0; i < ARRAY_SIZE(kvm_multi_ext_cfgs); i++) {
        KVMCPUConfig *multi_ext_cfg = &kvm_multi_ext_cfgs[i];

        if (!multi_ext_cfg->user_set) {
            continue;
        }

        id = kvm_riscv_reg_id(env, KVM_REG_RISCV_ISA_EXT,
                              multi_ext_cfg->kvm_reg_id);
        reg = kvm_cpu_cfg_get(cpu, multi_ext_cfg);
        ret = kvm_set_one_reg(cs, id, &reg);
        if (ret != 0) {
            error_report("Unable to %s extension %s in KVM, error %d",
                         reg ? "enable" : "disable",
                         multi_ext_cfg->name, ret);
            exit(EXIT_FAILURE);
        }
    }
}

static void kvm_riscv_add_cpu_user_properties(Object *cpu_obj)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(kvm_misa_ext_cfgs); i++) {
        KVMCPUConfig *misa_cfg = &kvm_misa_ext_cfgs[i];
        int bit = misa_cfg->offset;

        misa_cfg->name = riscv_get_misa_ext_name(bit);
        misa_cfg->description = riscv_get_misa_ext_description(bit);

        object_property_add(cpu_obj, misa_cfg->name, "bool",
                            NULL,
                            kvm_cpu_set_misa_ext_cfg,
                            NULL, misa_cfg);
        object_property_set_description(cpu_obj, misa_cfg->name,
                                        misa_cfg->description);
    }

    for (i = 0; i < ARRAY_SIZE(kvm_multi_ext_cfgs); i++) {
        KVMCPUConfig *multi_cfg = &kvm_multi_ext_cfgs[i];

        object_property_add(cpu_obj, multi_cfg->name, "bool",
                            NULL,
                            kvm_cpu_set_multi_ext_cfg,
                            NULL, multi_cfg);
    }

    object_property_add(cpu_obj, "cbom_blocksize", "uint16",
                        NULL, kvm_cpu_set_cbomz_blksize,
                        NULL, &kvm_cbom_blocksize);

    object_property_add(cpu_obj, "cboz_blocksize", "uint16",
                        NULL, kvm_cpu_set_cbomz_blksize,
                        NULL, &kvm_cboz_blocksize);
}

static int kvm_riscv_get_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    target_ulong reg;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    ret = kvm_get_one_reg(cs, RISCV_CORE_REG(env, regs.pc), &reg);
    if (ret) {
        return ret;
    }
    env->pc = reg;

    for (i = 1; i < 32; i++) {
        uint64_t id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CORE, i);
        ret = kvm_get_one_reg(cs, id, &reg);
        if (ret) {
            return ret;
        }
        env->gpr[i] = reg;
    }

    return ret;
}

static int kvm_riscv_put_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    target_ulong reg;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    reg = env->pc;
    ret = kvm_set_one_reg(cs, RISCV_CORE_REG(env, regs.pc), &reg);
    if (ret) {
        return ret;
    }

    for (i = 1; i < 32; i++) {
        uint64_t id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CORE, i);
        reg = env->gpr[i];
        ret = kvm_set_one_reg(cs, id, &reg);
        if (ret) {
            return ret;
        }
    }

    return ret;
}

static int kvm_riscv_get_regs_csr(CPUState *cs)
{
    int ret = 0;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    KVM_RISCV_GET_CSR(cs, env, sstatus, env->mstatus);
    KVM_RISCV_GET_CSR(cs, env, sie, env->mie);
    KVM_RISCV_GET_CSR(cs, env, stvec, env->stvec);
    KVM_RISCV_GET_CSR(cs, env, sscratch, env->sscratch);
    KVM_RISCV_GET_CSR(cs, env, sepc, env->sepc);
    KVM_RISCV_GET_CSR(cs, env, scause, env->scause);
    KVM_RISCV_GET_CSR(cs, env, stval, env->stval);
    KVM_RISCV_GET_CSR(cs, env, sip, env->mip);
    KVM_RISCV_GET_CSR(cs, env, satp, env->satp);
    return ret;
}

static int kvm_riscv_put_regs_csr(CPUState *cs)
{
    int ret = 0;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    KVM_RISCV_SET_CSR(cs, env, sstatus, env->mstatus);
    KVM_RISCV_SET_CSR(cs, env, sie, env->mie);
    KVM_RISCV_SET_CSR(cs, env, stvec, env->stvec);
    KVM_RISCV_SET_CSR(cs, env, sscratch, env->sscratch);
    KVM_RISCV_SET_CSR(cs, env, sepc, env->sepc);
    KVM_RISCV_SET_CSR(cs, env, scause, env->scause);
    KVM_RISCV_SET_CSR(cs, env, stval, env->stval);
    KVM_RISCV_SET_CSR(cs, env, sip, env->mip);
    KVM_RISCV_SET_CSR(cs, env, satp, env->satp);

    return ret;
}

static int kvm_riscv_get_regs_fp(CPUState *cs)
{
    int ret = 0;
    int i;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    if (riscv_has_ext(env, RVD)) {
        uint64_t reg;
        for (i = 0; i < 32; i++) {
            ret = kvm_get_one_reg(cs, RISCV_FP_D_REG(env, i), &reg);
            if (ret) {
                return ret;
            }
            env->fpr[i] = reg;
        }
        return ret;
    }

    if (riscv_has_ext(env, RVF)) {
        uint32_t reg;
        for (i = 0; i < 32; i++) {
            ret = kvm_get_one_reg(cs, RISCV_FP_F_REG(env, i), &reg);
            if (ret) {
                return ret;
            }
            env->fpr[i] = reg;
        }
        return ret;
    }

    return ret;
}

static int kvm_riscv_put_regs_fp(CPUState *cs)
{
    int ret = 0;
    int i;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    if (riscv_has_ext(env, RVD)) {
        uint64_t reg;
        for (i = 0; i < 32; i++) {
            reg = env->fpr[i];
            ret = kvm_set_one_reg(cs, RISCV_FP_D_REG(env, i), &reg);
            if (ret) {
                return ret;
            }
        }
        return ret;
    }

    if (riscv_has_ext(env, RVF)) {
        uint32_t reg;
        for (i = 0; i < 32; i++) {
            reg = env->fpr[i];
            ret = kvm_set_one_reg(cs, RISCV_FP_F_REG(env, i), &reg);
            if (ret) {
                return ret;
            }
        }
        return ret;
    }

    return ret;
}

static void kvm_riscv_get_regs_timer(CPUState *cs)
{
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    if (env->kvm_timer_dirty) {
        return;
    }

    KVM_RISCV_GET_TIMER(cs, env, time, env->kvm_timer_time);
    KVM_RISCV_GET_TIMER(cs, env, compare, env->kvm_timer_compare);
    KVM_RISCV_GET_TIMER(cs, env, state, env->kvm_timer_state);
    KVM_RISCV_GET_TIMER(cs, env, frequency, env->kvm_timer_frequency);

    env->kvm_timer_dirty = true;
}

static void kvm_riscv_put_regs_timer(CPUState *cs)
{
    uint64_t reg;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    if (!env->kvm_timer_dirty) {
        return;
    }

    KVM_RISCV_SET_TIMER(cs, env, time, env->kvm_timer_time);
    KVM_RISCV_SET_TIMER(cs, env, compare, env->kvm_timer_compare);

    /*
     * To set register of RISCV_TIMER_REG(state) will occur a error from KVM
     * on env->kvm_timer_state == 0, It's better to adapt in KVM, but it
     * doesn't matter that adaping in QEMU now.
     * TODO If KVM changes, adapt here.
     */
    if (env->kvm_timer_state) {
        KVM_RISCV_SET_TIMER(cs, env, state, env->kvm_timer_state);
    }

    /*
     * For now, migration will not work between Hosts with different timer
     * frequency. Therefore, we should check whether they are the same here
     * during the migration.
     */
    if (migration_is_running(migrate_get_current()->state)) {
        KVM_RISCV_GET_TIMER(cs, env, frequency, reg);
        if (reg != env->kvm_timer_frequency) {
            error_report("Dst Hosts timer frequency != Src Hosts");
        }
    }

    env->kvm_timer_dirty = false;
}

typedef struct KVMScratchCPU {
    int kvmfd;
    int vmfd;
    int cpufd;
} KVMScratchCPU;

/*
 * Heavily inspired by kvm_arm_create_scratch_host_vcpu()
 * from target/arm/kvm.c.
 */
static bool kvm_riscv_create_scratch_vcpu(KVMScratchCPU *scratch)
{
    int kvmfd = -1, vmfd = -1, cpufd = -1;

    kvmfd = qemu_open_old("/dev/kvm", O_RDWR);
    if (kvmfd < 0) {
        goto err;
    }
    do {
        vmfd = ioctl(kvmfd, KVM_CREATE_VM, 0);
    } while (vmfd == -1 && errno == EINTR);
    if (vmfd < 0) {
        goto err;
    }
    cpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
    if (cpufd < 0) {
        goto err;
    }

    scratch->kvmfd =  kvmfd;
    scratch->vmfd = vmfd;
    scratch->cpufd = cpufd;

    return true;

 err:
    if (cpufd >= 0) {
        close(cpufd);
    }
    if (vmfd >= 0) {
        close(vmfd);
    }
    if (kvmfd >= 0) {
        close(kvmfd);
    }

    return false;
}

static void kvm_riscv_destroy_scratch_vcpu(KVMScratchCPU *scratch)
{
    close(scratch->cpufd);
    close(scratch->vmfd);
    close(scratch->kvmfd);
}

static void kvm_riscv_init_machine_ids(RISCVCPU *cpu, KVMScratchCPU *kvmcpu)
{
    CPURISCVState *env = &cpu->env;
    struct kvm_one_reg reg;
    int ret;

    reg.id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CONFIG,
                              KVM_REG_RISCV_CONFIG_REG(mvendorid));
    reg.addr = (uint64_t)&cpu->cfg.mvendorid;
    ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
    if (ret != 0) {
        error_report("Unable to retrieve mvendorid from host, error %d", ret);
    }

    reg.id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CONFIG,
                              KVM_REG_RISCV_CONFIG_REG(marchid));
    reg.addr = (uint64_t)&cpu->cfg.marchid;
    ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
    if (ret != 0) {
        error_report("Unable to retrieve marchid from host, error %d", ret);
    }

    reg.id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CONFIG,
                              KVM_REG_RISCV_CONFIG_REG(mimpid));
    reg.addr = (uint64_t)&cpu->cfg.mimpid;
    ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
    if (ret != 0) {
        error_report("Unable to retrieve mimpid from host, error %d", ret);
    }
}

static void kvm_riscv_init_misa_ext_mask(RISCVCPU *cpu,
                                         KVMScratchCPU *kvmcpu)
{
    CPURISCVState *env = &cpu->env;
    struct kvm_one_reg reg;
    int ret;

    reg.id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CONFIG,
                              KVM_REG_RISCV_CONFIG_REG(isa));
    reg.addr = (uint64_t)&env->misa_ext_mask;
    ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);

    if (ret) {
        error_report("Unable to fetch ISA register from KVM, "
                     "error %d", ret);
        kvm_riscv_destroy_scratch_vcpu(kvmcpu);
        exit(EXIT_FAILURE);
    }

    env->misa_ext = env->misa_ext_mask;
}

static void kvm_riscv_read_cbomz_blksize(RISCVCPU *cpu, KVMScratchCPU *kvmcpu,
                                         KVMCPUConfig *cbomz_cfg)
{
    CPURISCVState *env = &cpu->env;
    struct kvm_one_reg reg;
    int ret;

    reg.id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CONFIG,
                              cbomz_cfg->kvm_reg_id);
    reg.addr = (uint64_t)kvmconfig_get_cfg_addr(cpu, cbomz_cfg);
    ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
    if (ret != 0) {
        error_report("Unable to read KVM reg %s, error %d",
                     cbomz_cfg->name, ret);
        exit(EXIT_FAILURE);
    }
}

static void kvm_riscv_init_multiext_cfg(RISCVCPU *cpu, KVMScratchCPU *kvmcpu)
{
    CPURISCVState *env = &cpu->env;
    uint64_t val;
    int i, ret;

    for (i = 0; i < ARRAY_SIZE(kvm_multi_ext_cfgs); i++) {
        KVMCPUConfig *multi_ext_cfg = &kvm_multi_ext_cfgs[i];
        struct kvm_one_reg reg;

        reg.id = kvm_riscv_reg_id(env, KVM_REG_RISCV_ISA_EXT,
                                  multi_ext_cfg->kvm_reg_id);
        reg.addr = (uint64_t)&val;
        ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
        if (ret != 0) {
            if (errno == EINVAL) {
                /* Silently default to 'false' if KVM does not support it. */
                multi_ext_cfg->supported = false;
                val = false;
            } else {
                error_report("Unable to read ISA_EXT KVM register %s, "
                             "error %d", multi_ext_cfg->name, ret);
                kvm_riscv_destroy_scratch_vcpu(kvmcpu);
                exit(EXIT_FAILURE);
            }
        } else {
            multi_ext_cfg->supported = true;
        }

        kvm_cpu_cfg_set(cpu, multi_ext_cfg, val);
    }

    if (cpu->cfg.ext_icbom) {
        kvm_riscv_read_cbomz_blksize(cpu, kvmcpu, &kvm_cbom_blocksize);
    }

    if (cpu->cfg.ext_icboz) {
        kvm_riscv_read_cbomz_blksize(cpu, kvmcpu, &kvm_cboz_blocksize);
    }
}

void kvm_riscv_init_user_properties(Object *cpu_obj)
{
    RISCVCPU *cpu = RISCV_CPU(cpu_obj);
    KVMScratchCPU kvmcpu;

    if (!kvm_riscv_create_scratch_vcpu(&kvmcpu)) {
        return;
    }

    kvm_riscv_add_cpu_user_properties(cpu_obj);
    kvm_riscv_init_machine_ids(cpu, &kvmcpu);
    kvm_riscv_init_misa_ext_mask(cpu, &kvmcpu);
    kvm_riscv_init_multiext_cfg(cpu, &kvmcpu);

    kvm_riscv_destroy_scratch_vcpu(&kvmcpu);
}

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

int kvm_arch_get_registers(CPUState *cs)
{
    int ret = 0;

    ret = kvm_riscv_get_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_riscv_get_regs_csr(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_riscv_get_regs_fp(cs);
    if (ret) {
        return ret;
    }

    return ret;
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    int ret = 0;

    ret = kvm_riscv_put_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_riscv_put_regs_csr(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_riscv_put_regs_fp(cs);
    if (ret) {
        return ret;
    }

    return ret;
}

int kvm_arch_release_virq_post(int virq)
{
    return 0;
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

static void kvm_riscv_vm_state_change(void *opaque, bool running,
                                      RunState state)
{
    CPUState *cs = opaque;

    if (running) {
        kvm_riscv_put_regs_timer(cs);
    } else {
        kvm_riscv_get_regs_timer(cs);
    }
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}

static int kvm_vcpu_set_machine_ids(RISCVCPU *cpu, CPUState *cs)
{
    CPURISCVState *env = &cpu->env;
    target_ulong reg;
    uint64_t id;
    int ret;

    id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CONFIG,
                          KVM_REG_RISCV_CONFIG_REG(mvendorid));
    /*
     * cfg.mvendorid is an uint32 but a target_ulong will
     * be written. Assign it to a target_ulong var to avoid
     * writing pieces of other cpu->cfg fields in the reg.
     */
    reg = cpu->cfg.mvendorid;
    ret = kvm_set_one_reg(cs, id, &reg);
    if (ret != 0) {
        return ret;
    }

    id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CONFIG,
                          KVM_REG_RISCV_CONFIG_REG(marchid));
    ret = kvm_set_one_reg(cs, id, &cpu->cfg.marchid);
    if (ret != 0) {
        return ret;
    }

    id = kvm_riscv_reg_id(env, KVM_REG_RISCV_CONFIG,
                          KVM_REG_RISCV_CONFIG_REG(mimpid));
    ret = kvm_set_one_reg(cs, id, &cpu->cfg.mimpid);

    return ret;
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    int ret = 0;
    RISCVCPU *cpu = RISCV_CPU(cs);

    qemu_add_vm_change_state_handler(kvm_riscv_vm_state_change, cs);

    if (!object_dynamic_cast(OBJECT(cpu), TYPE_RISCV_CPU_HOST)) {
        ret = kvm_vcpu_set_machine_ids(cpu, cs);
        if (ret != 0) {
            return ret;
        }
    }

    kvm_riscv_update_cpu_misa_ext(cpu, cs);
    kvm_riscv_update_cpu_cfg_isa_ext(cpu, cs);

    return ret;
}

int kvm_arch_msi_data_to_gsi(uint32_t data)
{
    abort();
}

int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_get_default_type(MachineState *ms)
{
    return 0;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    return 0;
}

int kvm_arch_irqchip_create(KVMState *s)
{
    return 0;
}

int kvm_arch_process_async_events(CPUState *cs)
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

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    return true;
}

static int kvm_riscv_handle_sbi(CPUState *cs, struct kvm_run *run)
{
    int ret = 0;
    unsigned char ch;
    switch (run->riscv_sbi.extension_id) {
    case SBI_EXT_0_1_CONSOLE_PUTCHAR:
        ch = run->riscv_sbi.args[0];
        qemu_chr_fe_write(serial_hd(0)->be, &ch, sizeof(ch));
        break;
    case SBI_EXT_0_1_CONSOLE_GETCHAR:
        ret = qemu_chr_fe_read_all(serial_hd(0)->be, &ch, sizeof(ch));
        if (ret == sizeof(ch)) {
            run->riscv_sbi.ret[0] = ch;
        } else {
            run->riscv_sbi.ret[0] = -1;
        }
        ret = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: un-handled SBI EXIT, specific reasons is %lu\n",
                      __func__, run->riscv_sbi.extension_id);
        ret = -1;
        break;
    }
    return ret;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    int ret = 0;
    switch (run->exit_reason) {
    case KVM_EXIT_RISCV_SBI:
        ret = kvm_riscv_handle_sbi(cs, run);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: un-handled exit reason %d\n",
                      __func__, run->exit_reason);
        ret = -1;
        break;
    }
    return ret;
}

void kvm_riscv_reset_vcpu(RISCVCPU *cpu)
{
    CPURISCVState *env = &cpu->env;

    if (!kvm_enabled()) {
        return;
    }
    env->pc = cpu->env.kernel_addr;
    env->gpr[10] = kvm_arch_vcpu_id(CPU(cpu)); /* a0 */
    env->gpr[11] = cpu->env.fdt_addr;          /* a1 */
    env->satp = 0;
}

void kvm_riscv_set_irq(RISCVCPU *cpu, int irq, int level)
{
    int ret;
    unsigned virq = level ? KVM_INTERRUPT_SET : KVM_INTERRUPT_UNSET;

    if (irq != IRQ_S_EXT) {
        perror("kvm riscv set irq != IRQ_S_EXT\n");
        abort();
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_INTERRUPT, &virq);
    if (ret < 0) {
        perror("Set irq failed");
        abort();
    }
}

bool kvm_arch_cpu_check_are_resettable(void)
{
    return true;
}

void kvm_arch_accel_class_init(ObjectClass *oc)
{
}
