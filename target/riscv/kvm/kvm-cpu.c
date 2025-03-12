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
#include <sys/prctl.h>

#include <linux/kvm.h>

#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qapi/visitor.h"
#include "system/system.h"
#include "system/kvm.h"
#include "system/kvm_int.h"
#include "cpu.h"
#include "trace.h"
#include "accel/accel-cpu-target.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "system/address-spaces.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/intc/riscv_imsic.h"
#include "qemu/log.h"
#include "hw/loader.h"
#include "kvm_riscv.h"
#include "sbi_ecall_interface.h"
#include "chardev/char-fe.h"
#include "migration/misc.h"
#include "system/runstate.h"
#include "hw/riscv/numa.h"

#define PR_RISCV_V_SET_CONTROL            69
#define PR_RISCV_V_VSTATE_CTRL_ON          2

void riscv_kvm_aplic_request(void *opaque, int irq, int level)
{
    kvm_set_irq(kvm_state, irq, !!level);
}

static bool cap_has_mp_state;

static uint64_t kvm_riscv_reg_id_ulong(CPURISCVState *env, uint64_t type,
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

static uint64_t kvm_riscv_reg_id_u32(uint64_t type, uint64_t idx)
{
    return KVM_REG_RISCV | KVM_REG_SIZE_U32 | type | idx;
}

static uint64_t kvm_riscv_reg_id_u64(uint64_t type, uint64_t idx)
{
    return KVM_REG_RISCV | KVM_REG_SIZE_U64 | type | idx;
}

static uint64_t kvm_encode_reg_size_id(uint64_t id, size_t size_b)
{
    uint64_t size_ctz = __builtin_ctz(size_b);

    return id | (size_ctz << KVM_REG_SIZE_SHIFT);
}

static uint64_t kvm_riscv_vector_reg_id(RISCVCPU *cpu,
                                        uint64_t idx)
{
    uint64_t id;
    size_t size_b;

    g_assert(idx < 32);

    id = KVM_REG_RISCV | KVM_REG_RISCV_VECTOR | KVM_REG_RISCV_VECTOR_REG(idx);
    size_b = cpu->cfg.vlenb;

    return kvm_encode_reg_size_id(id, size_b);
}

#define RISCV_CORE_REG(env, name) \
    kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_CORE, \
                           KVM_REG_RISCV_CORE_REG(name))

#define RISCV_CSR_REG(env, name) \
    kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_CSR, \
                           KVM_REG_RISCV_CSR_REG(name))

#define RISCV_CONFIG_REG(env, name) \
    kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_CONFIG, \
                           KVM_REG_RISCV_CONFIG_REG(name))

#define RISCV_TIMER_REG(name)  kvm_riscv_reg_id_u64(KVM_REG_RISCV_TIMER, \
                 KVM_REG_RISCV_TIMER_REG(name))

#define RISCV_FP_F_REG(idx)  kvm_riscv_reg_id_u32(KVM_REG_RISCV_FP_F, idx)

#define RISCV_FP_D_REG(idx)  kvm_riscv_reg_id_u64(KVM_REG_RISCV_FP_D, idx)

#define RISCV_VECTOR_CSR_REG(env, name) \
    kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_VECTOR, \
                           KVM_REG_RISCV_VECTOR_CSR_REG(name))

#define KVM_RISCV_GET_CSR(cs, env, csr, reg) \
    do { \
        int _ret = kvm_get_one_reg(cs, RISCV_CSR_REG(env, csr), &reg); \
        if (_ret) { \
            return _ret; \
        } \
    } while (0)

#define KVM_RISCV_SET_CSR(cs, env, csr, reg) \
    do { \
        int _ret = kvm_set_one_reg(cs, RISCV_CSR_REG(env, csr), &reg); \
        if (_ret) { \
            return _ret; \
        } \
    } while (0)

#define KVM_RISCV_GET_TIMER(cs, name, reg) \
    do { \
        int ret = kvm_get_one_reg(cs, RISCV_TIMER_REG(name), &reg); \
        if (ret) { \
            abort(); \
        } \
    } while (0)

#define KVM_RISCV_SET_TIMER(cs, name, reg) \
    do { \
        int ret = kvm_set_one_reg(cs, RISCV_TIMER_REG(name), &reg); \
        if (ret) { \
            abort(); \
        } \
    } while (0)

typedef struct KVMCPUConfig {
    const char *name;
    const char *description;
    target_ulong offset;
    uint64_t kvm_reg_id;
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
    KVM_MISA_CFG(RVV, KVM_RISCV_ISA_EXT_V),
};

static void kvm_cpu_get_misa_ext_cfg(Object *obj, Visitor *v,
                                     const char *name,
                                     void *opaque, Error **errp)
{
    KVMCPUConfig *misa_ext_cfg = opaque;
    target_ulong misa_bit = misa_ext_cfg->offset;
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    bool value = env->misa_ext_mask & misa_bit;

    visit_type_bool(v, name, &value, errp);
}

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
        id = kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_ISA_EXT,
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

#define KVM_EXT_CFG(_name, _prop, _reg_id) \
    {.name = _name, .offset = CPU_CFG_OFFSET(_prop), \
     .kvm_reg_id = _reg_id}

static KVMCPUConfig kvm_multi_ext_cfgs[] = {
    KVM_EXT_CFG("zicbom", ext_zicbom, KVM_RISCV_ISA_EXT_ZICBOM),
    KVM_EXT_CFG("zicboz", ext_zicboz, KVM_RISCV_ISA_EXT_ZICBOZ),
    KVM_EXT_CFG("ziccrse", ext_ziccrse, KVM_RISCV_ISA_EXT_ZICCRSE),
    KVM_EXT_CFG("zicntr", ext_zicntr, KVM_RISCV_ISA_EXT_ZICNTR),
    KVM_EXT_CFG("zicond", ext_zicond, KVM_RISCV_ISA_EXT_ZICOND),
    KVM_EXT_CFG("zicsr", ext_zicsr, KVM_RISCV_ISA_EXT_ZICSR),
    KVM_EXT_CFG("zifencei", ext_zifencei, KVM_RISCV_ISA_EXT_ZIFENCEI),
    KVM_EXT_CFG("zihintntl", ext_zihintntl, KVM_RISCV_ISA_EXT_ZIHINTNTL),
    KVM_EXT_CFG("zihintpause", ext_zihintpause, KVM_RISCV_ISA_EXT_ZIHINTPAUSE),
    KVM_EXT_CFG("zihpm", ext_zihpm, KVM_RISCV_ISA_EXT_ZIHPM),
    KVM_EXT_CFG("zimop", ext_zimop, KVM_RISCV_ISA_EXT_ZIMOP),
    KVM_EXT_CFG("zcmop", ext_zcmop, KVM_RISCV_ISA_EXT_ZCMOP),
    KVM_EXT_CFG("zabha", ext_zabha, KVM_RISCV_ISA_EXT_ZABHA),
    KVM_EXT_CFG("zacas", ext_zacas, KVM_RISCV_ISA_EXT_ZACAS),
    KVM_EXT_CFG("zawrs", ext_zawrs, KVM_RISCV_ISA_EXT_ZAWRS),
    KVM_EXT_CFG("zfa", ext_zfa, KVM_RISCV_ISA_EXT_ZFA),
    KVM_EXT_CFG("zfh", ext_zfh, KVM_RISCV_ISA_EXT_ZFH),
    KVM_EXT_CFG("zfhmin", ext_zfhmin, KVM_RISCV_ISA_EXT_ZFHMIN),
    KVM_EXT_CFG("zba", ext_zba, KVM_RISCV_ISA_EXT_ZBA),
    KVM_EXT_CFG("zbb", ext_zbb, KVM_RISCV_ISA_EXT_ZBB),
    KVM_EXT_CFG("zbc", ext_zbc, KVM_RISCV_ISA_EXT_ZBC),
    KVM_EXT_CFG("zbkb", ext_zbkb, KVM_RISCV_ISA_EXT_ZBKB),
    KVM_EXT_CFG("zbkc", ext_zbkc, KVM_RISCV_ISA_EXT_ZBKC),
    KVM_EXT_CFG("zbkx", ext_zbkx, KVM_RISCV_ISA_EXT_ZBKX),
    KVM_EXT_CFG("zbs", ext_zbs, KVM_RISCV_ISA_EXT_ZBS),
    KVM_EXT_CFG("zca", ext_zca, KVM_RISCV_ISA_EXT_ZCA),
    KVM_EXT_CFG("zcb", ext_zcb, KVM_RISCV_ISA_EXT_ZCB),
    KVM_EXT_CFG("zcd", ext_zcd, KVM_RISCV_ISA_EXT_ZCD),
    KVM_EXT_CFG("zcf", ext_zcf, KVM_RISCV_ISA_EXT_ZCF),
    KVM_EXT_CFG("zknd", ext_zknd, KVM_RISCV_ISA_EXT_ZKND),
    KVM_EXT_CFG("zkne", ext_zkne, KVM_RISCV_ISA_EXT_ZKNE),
    KVM_EXT_CFG("zknh", ext_zknh, KVM_RISCV_ISA_EXT_ZKNH),
    KVM_EXT_CFG("zkr", ext_zkr, KVM_RISCV_ISA_EXT_ZKR),
    KVM_EXT_CFG("zksed", ext_zksed, KVM_RISCV_ISA_EXT_ZKSED),
    KVM_EXT_CFG("zksh", ext_zksh, KVM_RISCV_ISA_EXT_ZKSH),
    KVM_EXT_CFG("zkt", ext_zkt, KVM_RISCV_ISA_EXT_ZKT),
    KVM_EXT_CFG("ztso", ext_ztso, KVM_RISCV_ISA_EXT_ZTSO),
    KVM_EXT_CFG("zvbb", ext_zvbb, KVM_RISCV_ISA_EXT_ZVBB),
    KVM_EXT_CFG("zvbc", ext_zvbc, KVM_RISCV_ISA_EXT_ZVBC),
    KVM_EXT_CFG("zvfh", ext_zvfh, KVM_RISCV_ISA_EXT_ZVFH),
    KVM_EXT_CFG("zvfhmin", ext_zvfhmin, KVM_RISCV_ISA_EXT_ZVFHMIN),
    KVM_EXT_CFG("zvkb", ext_zvkb, KVM_RISCV_ISA_EXT_ZVKB),
    KVM_EXT_CFG("zvkg", ext_zvkg, KVM_RISCV_ISA_EXT_ZVKG),
    KVM_EXT_CFG("zvkned", ext_zvkned, KVM_RISCV_ISA_EXT_ZVKNED),
    KVM_EXT_CFG("zvknha", ext_zvknha, KVM_RISCV_ISA_EXT_ZVKNHA),
    KVM_EXT_CFG("zvknhb", ext_zvknhb, KVM_RISCV_ISA_EXT_ZVKNHB),
    KVM_EXT_CFG("zvksed", ext_zvksed, KVM_RISCV_ISA_EXT_ZVKSED),
    KVM_EXT_CFG("zvksh", ext_zvksh, KVM_RISCV_ISA_EXT_ZVKSH),
    KVM_EXT_CFG("zvkt", ext_zvkt, KVM_RISCV_ISA_EXT_ZVKT),
    KVM_EXT_CFG("smnpm", ext_smnpm, KVM_RISCV_ISA_EXT_SMNPM),
    KVM_EXT_CFG("smstateen", ext_smstateen, KVM_RISCV_ISA_EXT_SMSTATEEN),
    KVM_EXT_CFG("ssaia", ext_ssaia, KVM_RISCV_ISA_EXT_SSAIA),
    KVM_EXT_CFG("sscofpmf", ext_sscofpmf, KVM_RISCV_ISA_EXT_SSCOFPMF),
    KVM_EXT_CFG("ssnpm", ext_ssnpm, KVM_RISCV_ISA_EXT_SSNPM),
    KVM_EXT_CFG("sstc", ext_sstc, KVM_RISCV_ISA_EXT_SSTC),
    KVM_EXT_CFG("svade", ext_svade, KVM_RISCV_ISA_EXT_SVADE),
    KVM_EXT_CFG("svadu", ext_svadu, KVM_RISCV_ISA_EXT_SVADU),
    KVM_EXT_CFG("svinval", ext_svinval, KVM_RISCV_ISA_EXT_SVINVAL),
    KVM_EXT_CFG("svnapot", ext_svnapot, KVM_RISCV_ISA_EXT_SVNAPOT),
    KVM_EXT_CFG("svpbmt", ext_svpbmt, KVM_RISCV_ISA_EXT_SVPBMT),
    KVM_EXT_CFG("svvptc", ext_svvptc, KVM_RISCV_ISA_EXT_SVVPTC),
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

static void kvm_cpu_get_multi_ext_cfg(Object *obj, Visitor *v,
                                      const char *name,
                                      void *opaque, Error **errp)
{
    KVMCPUConfig *multi_ext_cfg = opaque;
    RISCVCPU *cpu = RISCV_CPU(obj);
    bool value = kvm_cpu_cfg_get(cpu, multi_ext_cfg);

    visit_type_bool(v, name, &value, errp);
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
    .offset = CPU_CFG_OFFSET(cbom_blocksize),
    .kvm_reg_id = KVM_REG_RISCV_CONFIG_REG(zicbom_block_size)
};

static KVMCPUConfig kvm_cboz_blocksize = {
    .name = "cboz_blocksize",
    .offset = CPU_CFG_OFFSET(cboz_blocksize),
    .kvm_reg_id = KVM_REG_RISCV_CONFIG_REG(zicboz_block_size)
};

static KVMCPUConfig kvm_v_vlenb = {
    .name = "vlenb",
    .offset = CPU_CFG_OFFSET(vlenb),
    .kvm_reg_id =  KVM_REG_RISCV | KVM_REG_SIZE_U64 | KVM_REG_RISCV_VECTOR |
                   KVM_REG_RISCV_VECTOR_CSR_REG(vlenb)
};

static KVMCPUConfig kvm_sbi_dbcn = {
    .name = "sbi_dbcn",
    .kvm_reg_id = KVM_REG_RISCV | KVM_REG_SIZE_U64 |
                  KVM_REG_RISCV_SBI_EXT | KVM_RISCV_SBI_EXT_DBCN
};

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

        id = kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_ISA_EXT,
                                    multi_ext_cfg->kvm_reg_id);
        reg = kvm_cpu_cfg_get(cpu, multi_ext_cfg);
        ret = kvm_set_one_reg(cs, id, &reg);
        if (ret != 0) {
            if (!reg && ret == -EINVAL) {
                warn_report("KVM cannot disable extension %s",
                            multi_ext_cfg->name);
            } else {
                error_report("Unable to enable extension %s in KVM, error %d",
                             multi_ext_cfg->name, ret);
                exit(EXIT_FAILURE);
            }
        }
    }
}

static void cpu_get_cfg_unavailable(Object *obj, Visitor *v,
                                    const char *name,
                                    void *opaque, Error **errp)
{
    bool value = false;

    visit_type_bool(v, name, &value, errp);
}

static void cpu_set_cfg_unavailable(Object *obj, Visitor *v,
                                    const char *name,
                                    void *opaque, Error **errp)
{
    const char *propname = opaque;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    if (value) {
        error_setg(errp, "'%s' is not available with KVM",
                   propname);
    }
}

static void riscv_cpu_add_kvm_unavail_prop(Object *obj, const char *prop_name)
{
    /* Check if KVM created the property already */
    if (object_property_find(obj, prop_name)) {
        return;
    }

    /*
     * Set the default to disabled for every extension
     * unknown to KVM and error out if the user attempts
     * to enable any of them.
     */
    object_property_add(obj, prop_name, "bool",
                        cpu_get_cfg_unavailable,
                        cpu_set_cfg_unavailable,
                        NULL, (void *)prop_name);
}

static void riscv_cpu_add_kvm_unavail_prop_array(Object *obj,
                                        const RISCVCPUMultiExtConfig *array)
{
    const RISCVCPUMultiExtConfig *prop;

    g_assert(array);

    for (prop = array; prop && prop->name; prop++) {
        riscv_cpu_add_kvm_unavail_prop(obj, prop->name);
    }
}

static void kvm_riscv_add_cpu_user_properties(Object *cpu_obj)
{
    int i;

    riscv_add_satp_mode_properties(cpu_obj);

    for (i = 0; i < ARRAY_SIZE(kvm_misa_ext_cfgs); i++) {
        KVMCPUConfig *misa_cfg = &kvm_misa_ext_cfgs[i];
        int bit = misa_cfg->offset;

        misa_cfg->name = riscv_get_misa_ext_name(bit);
        misa_cfg->description = riscv_get_misa_ext_description(bit);

        object_property_add(cpu_obj, misa_cfg->name, "bool",
                            kvm_cpu_get_misa_ext_cfg,
                            kvm_cpu_set_misa_ext_cfg,
                            NULL, misa_cfg);
        object_property_set_description(cpu_obj, misa_cfg->name,
                                        misa_cfg->description);
    }

    for (i = 0; misa_bits[i] != 0; i++) {
        const char *ext_name = riscv_get_misa_ext_name(misa_bits[i]);
        riscv_cpu_add_kvm_unavail_prop(cpu_obj, ext_name);
    }

    for (i = 0; i < ARRAY_SIZE(kvm_multi_ext_cfgs); i++) {
        KVMCPUConfig *multi_cfg = &kvm_multi_ext_cfgs[i];

        object_property_add(cpu_obj, multi_cfg->name, "bool",
                            kvm_cpu_get_multi_ext_cfg,
                            kvm_cpu_set_multi_ext_cfg,
                            NULL, multi_cfg);
    }

    riscv_cpu_add_kvm_unavail_prop_array(cpu_obj, riscv_cpu_extensions);
    riscv_cpu_add_kvm_unavail_prop_array(cpu_obj, riscv_cpu_vendor_exts);
    riscv_cpu_add_kvm_unavail_prop_array(cpu_obj, riscv_cpu_experimental_exts);

   /* We don't have the needed KVM support for profiles */
    for (i = 0; riscv_profiles[i] != NULL; i++) {
        riscv_cpu_add_kvm_unavail_prop(cpu_obj, riscv_profiles[i]->name);
    }
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
        uint64_t id = kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_CORE, i);
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
        uint64_t id = kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_CORE, i);
        reg = env->gpr[i];
        ret = kvm_set_one_reg(cs, id, &reg);
        if (ret) {
            return ret;
        }
    }

    return ret;
}

static void kvm_riscv_reset_regs_csr(CPURISCVState *env)
{
    env->mstatus = 0;
    env->mie = 0;
    env->stvec = 0;
    env->sscratch = 0;
    env->sepc = 0;
    env->scause = 0;
    env->stval = 0;
    env->mip = 0;
    env->satp = 0;
}

static int kvm_riscv_get_regs_csr(CPUState *cs)
{
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

    return 0;
}

static int kvm_riscv_put_regs_csr(CPUState *cs)
{
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

    return 0;
}

static int kvm_riscv_get_regs_fp(CPUState *cs)
{
    int ret = 0;
    int i;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    if (riscv_has_ext(env, RVD)) {
        uint64_t reg;
        for (i = 0; i < 32; i++) {
            ret = kvm_get_one_reg(cs, RISCV_FP_D_REG(i), &reg);
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
            ret = kvm_get_one_reg(cs, RISCV_FP_F_REG(i), &reg);
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
            ret = kvm_set_one_reg(cs, RISCV_FP_D_REG(i), &reg);
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
            ret = kvm_set_one_reg(cs, RISCV_FP_F_REG(i), &reg);
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

    KVM_RISCV_GET_TIMER(cs, time, env->kvm_timer_time);
    KVM_RISCV_GET_TIMER(cs, compare, env->kvm_timer_compare);
    KVM_RISCV_GET_TIMER(cs, state, env->kvm_timer_state);
    KVM_RISCV_GET_TIMER(cs, frequency, env->kvm_timer_frequency);

    env->kvm_timer_dirty = true;
}

static void kvm_riscv_put_regs_timer(CPUState *cs)
{
    uint64_t reg;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    if (!env->kvm_timer_dirty) {
        return;
    }

    KVM_RISCV_SET_TIMER(cs, time, env->kvm_timer_time);
    KVM_RISCV_SET_TIMER(cs, compare, env->kvm_timer_compare);

    /*
     * To set register of RISCV_TIMER_REG(state) will occur a error from KVM
     * on env->kvm_timer_state == 0, It's better to adapt in KVM, but it
     * doesn't matter that adaping in QEMU now.
     * TODO If KVM changes, adapt here.
     */
    if (env->kvm_timer_state) {
        KVM_RISCV_SET_TIMER(cs, state, env->kvm_timer_state);
    }

    /*
     * For now, migration will not work between Hosts with different timer
     * frequency. Therefore, we should check whether they are the same here
     * during the migration.
     */
    if (migration_is_running()) {
        KVM_RISCV_GET_TIMER(cs, frequency, reg);
        if (reg != env->kvm_timer_frequency) {
            error_report("Dst Hosts timer frequency != Src Hosts");
        }
    }

    env->kvm_timer_dirty = false;
}

uint64_t kvm_riscv_get_timebase_frequency(RISCVCPU *cpu)
{
    uint64_t reg;

    KVM_RISCV_GET_TIMER(CPU(cpu), frequency, reg);

    return reg;
}

static int kvm_riscv_get_regs_vector(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    target_ulong reg;
    uint64_t vreg_id;
    int vreg_idx, ret = 0;

    if (!riscv_has_ext(env, RVV)) {
        return 0;
    }

    ret = kvm_get_one_reg(cs, RISCV_VECTOR_CSR_REG(env, vstart), &reg);
    if (ret) {
        return ret;
    }
    env->vstart = reg;

    ret = kvm_get_one_reg(cs, RISCV_VECTOR_CSR_REG(env, vl), &reg);
    if (ret) {
        return ret;
    }
    env->vl = reg;

    ret = kvm_get_one_reg(cs, RISCV_VECTOR_CSR_REG(env, vtype), &reg);
    if (ret) {
        return ret;
    }
    env->vtype = reg;

    if (kvm_v_vlenb.supported) {
        ret = kvm_get_one_reg(cs, RISCV_VECTOR_CSR_REG(env, vlenb), &reg);
        if (ret) {
            return ret;
        }
        cpu->cfg.vlenb = reg;

        for (int i = 0; i < 32; i++) {
            /*
             * vreg[] is statically allocated using RV_VLEN_MAX.
             * Use it instead of vlenb to calculate vreg_idx for
             * simplicity.
             */
            vreg_idx = i * RV_VLEN_MAX / 64;
            vreg_id = kvm_riscv_vector_reg_id(cpu, i);

            ret = kvm_get_one_reg(cs, vreg_id, &env->vreg[vreg_idx]);
            if (ret) {
                return ret;
            }
        }
    }

    return 0;
}

static int kvm_riscv_put_regs_vector(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    target_ulong reg;
    uint64_t vreg_id;
    int vreg_idx, ret = 0;

    if (!riscv_has_ext(env, RVV)) {
        return 0;
    }

    reg = env->vstart;
    ret = kvm_set_one_reg(cs, RISCV_VECTOR_CSR_REG(env, vstart), &reg);
    if (ret) {
        return ret;
    }

    reg = env->vl;
    ret = kvm_set_one_reg(cs, RISCV_VECTOR_CSR_REG(env, vl), &reg);
    if (ret) {
        return ret;
    }

    reg = env->vtype;
    ret = kvm_set_one_reg(cs, RISCV_VECTOR_CSR_REG(env, vtype), &reg);
    if (ret) {
        return ret;
    }

    if (kvm_v_vlenb.supported) {
        reg = cpu->cfg.vlenb;
        ret = kvm_set_one_reg(cs, RISCV_VECTOR_CSR_REG(env, vlenb), &reg);

        for (int i = 0; i < 32; i++) {
            /*
             * vreg[] is statically allocated using RV_VLEN_MAX.
             * Use it instead of vlenb to calculate vreg_idx for
             * simplicity.
             */
            vreg_idx = i * RV_VLEN_MAX / 64;
            vreg_id = kvm_riscv_vector_reg_id(cpu, i);

            ret = kvm_set_one_reg(cs, vreg_id, &env->vreg[vreg_idx]);
            if (ret) {
                return ret;
            }
        }
    }

    return ret;
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

    reg.id = RISCV_CONFIG_REG(env, mvendorid);
    reg.addr = (uint64_t)&cpu->cfg.mvendorid;
    ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
    if (ret != 0) {
        error_report("Unable to retrieve mvendorid from host, error %d", ret);
    }

    reg.id = RISCV_CONFIG_REG(env, marchid);
    reg.addr = (uint64_t)&cpu->cfg.marchid;
    ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
    if (ret != 0) {
        error_report("Unable to retrieve marchid from host, error %d", ret);
    }

    reg.id = RISCV_CONFIG_REG(env, mimpid);
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

    reg.id = RISCV_CONFIG_REG(env, isa);
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

    reg.id = kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_CONFIG,
                                    cbomz_cfg->kvm_reg_id);
    reg.addr = (uint64_t)kvmconfig_get_cfg_addr(cpu, cbomz_cfg);
    ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
    if (ret != 0) {
        error_report("Unable to read KVM reg %s, error %d",
                     cbomz_cfg->name, ret);
        exit(EXIT_FAILURE);
    }
}

static void kvm_riscv_read_multiext_legacy(RISCVCPU *cpu,
                                           KVMScratchCPU *kvmcpu)
{
    CPURISCVState *env = &cpu->env;
    uint64_t val;
    int i, ret;

    for (i = 0; i < ARRAY_SIZE(kvm_multi_ext_cfgs); i++) {
        KVMCPUConfig *multi_ext_cfg = &kvm_multi_ext_cfgs[i];
        struct kvm_one_reg reg;

        reg.id = kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_ISA_EXT,
                                        multi_ext_cfg->kvm_reg_id);
        reg.addr = (uint64_t)&val;
        ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
        if (ret != 0) {
            if (errno == EINVAL) {
                /* Silently default to 'false' if KVM does not support it. */
                multi_ext_cfg->supported = false;
                val = false;
            } else {
                error_report("Unable to read ISA_EXT KVM register %s: %s",
                             multi_ext_cfg->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
        } else {
            multi_ext_cfg->supported = true;
        }

        kvm_cpu_cfg_set(cpu, multi_ext_cfg, val);
    }

    if (cpu->cfg.ext_zicbom) {
        kvm_riscv_read_cbomz_blksize(cpu, kvmcpu, &kvm_cbom_blocksize);
    }

    if (cpu->cfg.ext_zicboz) {
        kvm_riscv_read_cbomz_blksize(cpu, kvmcpu, &kvm_cboz_blocksize);
    }
}

static int uint64_cmp(const void *a, const void *b)
{
    uint64_t val1 = *(const uint64_t *)a;
    uint64_t val2 = *(const uint64_t *)b;

    if (val1 < val2) {
        return -1;
    }

    if (val1 > val2) {
        return 1;
    }

    return 0;
}

static void kvm_riscv_check_sbi_dbcn_support(RISCVCPU *cpu,
                                             KVMScratchCPU *kvmcpu,
                                             struct kvm_reg_list *reglist)
{
    struct kvm_reg_list *reg_search;

    reg_search = bsearch(&kvm_sbi_dbcn.kvm_reg_id, reglist->reg, reglist->n,
                         sizeof(uint64_t), uint64_cmp);

    if (reg_search) {
        kvm_sbi_dbcn.supported = true;
    }
}

static void kvm_riscv_read_vlenb(RISCVCPU *cpu, KVMScratchCPU *kvmcpu,
                                 struct kvm_reg_list *reglist)
{
    struct kvm_one_reg reg;
    struct kvm_reg_list *reg_search;
    uint64_t val;
    int ret;

    reg_search = bsearch(&kvm_v_vlenb.kvm_reg_id, reglist->reg, reglist->n,
                         sizeof(uint64_t), uint64_cmp);

    if (reg_search) {
        reg.id = kvm_v_vlenb.kvm_reg_id;
        reg.addr = (uint64_t)&val;

        ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
        if (ret != 0) {
            error_report("Unable to read vlenb register, error code: %d",
                         errno);
            exit(EXIT_FAILURE);
        }

        kvm_v_vlenb.supported = true;
        cpu->cfg.vlenb = val;
    }
}

static void kvm_riscv_init_multiext_cfg(RISCVCPU *cpu, KVMScratchCPU *kvmcpu)
{
    KVMCPUConfig *multi_ext_cfg;
    struct kvm_one_reg reg;
    struct kvm_reg_list rl_struct;
    struct kvm_reg_list *reglist;
    uint64_t val, reg_id, *reg_search;
    int i, ret;

    rl_struct.n = 0;
    ret = ioctl(kvmcpu->cpufd, KVM_GET_REG_LIST, &rl_struct);

    /*
     * If KVM_GET_REG_LIST isn't supported we'll get errno 22
     * (EINVAL). Use read_legacy() in this case.
     */
    if (errno == EINVAL) {
        return kvm_riscv_read_multiext_legacy(cpu, kvmcpu);
    } else if (errno != E2BIG) {
        /*
         * E2BIG is an expected error message for the API since we
         * don't know the number of registers. The right amount will
         * be written in rl_struct.n.
         *
         * Error out if we get any other errno.
         */
        error_report("Error when accessing get-reg-list: %s",
                     strerror(errno));
        exit(EXIT_FAILURE);
    }

    reglist = g_malloc(sizeof(struct kvm_reg_list) +
                       rl_struct.n * sizeof(uint64_t));
    reglist->n = rl_struct.n;
    ret = ioctl(kvmcpu->cpufd, KVM_GET_REG_LIST, reglist);
    if (ret) {
        error_report("Error when reading KVM_GET_REG_LIST: %s",
                     strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* sort reglist to use bsearch() */
    qsort(&reglist->reg, reglist->n, sizeof(uint64_t), uint64_cmp);

    for (i = 0; i < ARRAY_SIZE(kvm_multi_ext_cfgs); i++) {
        multi_ext_cfg = &kvm_multi_ext_cfgs[i];
        reg_id = kvm_riscv_reg_id_ulong(&cpu->env, KVM_REG_RISCV_ISA_EXT,
                                        multi_ext_cfg->kvm_reg_id);
        reg_search = bsearch(&reg_id, reglist->reg, reglist->n,
                             sizeof(uint64_t), uint64_cmp);
        if (!reg_search) {
            continue;
        }

        reg.id = reg_id;
        reg.addr = (uint64_t)&val;
        ret = ioctl(kvmcpu->cpufd, KVM_GET_ONE_REG, &reg);
        if (ret != 0) {
            error_report("Unable to read ISA_EXT KVM register %s: %s",
                         multi_ext_cfg->name, strerror(errno));
            exit(EXIT_FAILURE);
        }

        multi_ext_cfg->supported = true;
        kvm_cpu_cfg_set(cpu, multi_ext_cfg, val);
    }

    if (cpu->cfg.ext_zicbom) {
        kvm_riscv_read_cbomz_blksize(cpu, kvmcpu, &kvm_cbom_blocksize);
    }

    if (cpu->cfg.ext_zicboz) {
        kvm_riscv_read_cbomz_blksize(cpu, kvmcpu, &kvm_cboz_blocksize);
    }

    if (riscv_has_ext(&cpu->env, RVV)) {
        kvm_riscv_read_vlenb(cpu, kvmcpu, reglist);
    }

    kvm_riscv_check_sbi_dbcn_support(cpu, kvmcpu, reglist);
}

static void riscv_init_kvm_registers(Object *cpu_obj)
{
    RISCVCPU *cpu = RISCV_CPU(cpu_obj);
    KVMScratchCPU kvmcpu;

    if (!kvm_riscv_create_scratch_vcpu(&kvmcpu)) {
        return;
    }

    kvm_riscv_init_machine_ids(cpu, &kvmcpu);
    kvm_riscv_init_misa_ext_mask(cpu, &kvmcpu);
    kvm_riscv_init_multiext_cfg(cpu, &kvmcpu);

    kvm_riscv_destroy_scratch_vcpu(&kvmcpu);
}

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

int kvm_arch_get_registers(CPUState *cs, Error **errp)
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

    ret = kvm_riscv_get_regs_vector(cs);
    if (ret) {
        return ret;
    }

    return ret;
}

int kvm_riscv_sync_mpstate_to_kvm(RISCVCPU *cpu, int state)
{
    if (cap_has_mp_state) {
        struct kvm_mp_state mp_state = {
            .mp_state = state
        };

        int ret = kvm_vcpu_ioctl(CPU(cpu), KVM_SET_MP_STATE, &mp_state);
        if (ret) {
            fprintf(stderr, "%s: failed to sync MP_STATE %d/%s\n",
                    __func__, ret, strerror(-ret));
            return -1;
        }
    }

    return 0;
}

int kvm_arch_put_registers(CPUState *cs, int level, Error **errp)
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

    ret = kvm_riscv_put_regs_vector(cs);
    if (ret) {
        return ret;
    }

    if (KVM_PUT_RESET_STATE == level) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        if (cs->cpu_index == 0) {
            ret = kvm_riscv_sync_mpstate_to_kvm(cpu, KVM_MP_STATE_RUNNABLE);
        } else {
            ret = kvm_riscv_sync_mpstate_to_kvm(cpu, KVM_MP_STATE_STOPPED);
        }
        if (ret) {
            return ret;
        }
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

    id = RISCV_CONFIG_REG(env, mvendorid);
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

    id = RISCV_CONFIG_REG(env, marchid);
    ret = kvm_set_one_reg(cs, id, &cpu->cfg.marchid);
    if (ret != 0) {
        return ret;
    }

    id = RISCV_CONFIG_REG(env, mimpid);
    ret = kvm_set_one_reg(cs, id, &cpu->cfg.mimpid);

    return ret;
}

static int kvm_vcpu_enable_sbi_dbcn(RISCVCPU *cpu, CPUState *cs)
{
    target_ulong reg = 1;

    if (!kvm_sbi_dbcn.supported) {
        return 0;
    }

    return kvm_set_one_reg(cs, kvm_sbi_dbcn.kvm_reg_id, &reg);
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

    ret = kvm_vcpu_enable_sbi_dbcn(cpu, cs);

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
    cap_has_mp_state = kvm_check_extension(s, KVM_CAP_MP_STATE);
    return 0;
}

int kvm_arch_irqchip_create(KVMState *s)
{
    /*
     * We can create the VAIA using the newer device control API.
     */
    return kvm_check_extension(s, KVM_CAP_DEVICE_CTRL);
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

static void kvm_riscv_handle_sbi_dbcn(CPUState *cs, struct kvm_run *run)
{
    g_autofree uint8_t *buf = NULL;
    RISCVCPU *cpu = RISCV_CPU(cs);
    target_ulong num_bytes;
    uint64_t addr;
    unsigned char ch;
    int ret;

    switch (run->riscv_sbi.function_id) {
    case SBI_EXT_DBCN_CONSOLE_READ:
    case SBI_EXT_DBCN_CONSOLE_WRITE:
        num_bytes = run->riscv_sbi.args[0];

        if (num_bytes == 0) {
            run->riscv_sbi.ret[0] = SBI_SUCCESS;
            run->riscv_sbi.ret[1] = 0;
            break;
        }

        addr = run->riscv_sbi.args[1];

        /*
         * Handle the case where a 32 bit CPU is running in a
         * 64 bit addressing env.
         */
        if (riscv_cpu_mxl(&cpu->env) == MXL_RV32) {
            addr |= (uint64_t)run->riscv_sbi.args[2] << 32;
        }

        buf = g_malloc0(num_bytes);

        if (run->riscv_sbi.function_id == SBI_EXT_DBCN_CONSOLE_READ) {
            ret = qemu_chr_fe_read_all(serial_hd(0)->be, buf, num_bytes);
            if (ret < 0) {
                error_report("SBI_EXT_DBCN_CONSOLE_READ: error when "
                             "reading chardev");
                exit(1);
            }

            cpu_physical_memory_write(addr, buf, ret);
        } else {
            cpu_physical_memory_read(addr, buf, num_bytes);

            ret = qemu_chr_fe_write_all(serial_hd(0)->be, buf, num_bytes);
            if (ret < 0) {
                error_report("SBI_EXT_DBCN_CONSOLE_WRITE: error when "
                             "writing chardev");
                exit(1);
            }
        }

        run->riscv_sbi.ret[0] = SBI_SUCCESS;
        run->riscv_sbi.ret[1] = ret;
        break;
    case SBI_EXT_DBCN_CONSOLE_WRITE_BYTE:
        ch = run->riscv_sbi.args[0];
        ret = qemu_chr_fe_write(serial_hd(0)->be, &ch, sizeof(ch));

        if (ret < 0) {
            error_report("SBI_EXT_DBCN_CONSOLE_WRITE_BYTE: error when "
                         "writing chardev");
            exit(1);
        }

        run->riscv_sbi.ret[0] = SBI_SUCCESS;
        run->riscv_sbi.ret[1] = 0;
        break;
    default:
        run->riscv_sbi.ret[0] = SBI_ERR_NOT_SUPPORTED;
    }
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
    case SBI_EXT_DBCN:
        kvm_riscv_handle_sbi_dbcn(cs, run);
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

static int kvm_riscv_handle_csr(CPUState *cs, struct kvm_run *run)
{
    target_ulong csr_num = run->riscv_csr.csr_num;
    target_ulong new_value = run->riscv_csr.new_value;
    target_ulong write_mask = run->riscv_csr.write_mask;
    int ret = 0;

    switch (csr_num) {
    case CSR_SEED:
        run->riscv_csr.ret_value = riscv_new_csr_seed(new_value, write_mask);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: un-handled CSR EXIT for CSR %lx\n",
                      __func__, csr_num);
        ret = -1;
        break;
    }

    return ret;
}

static bool kvm_riscv_handle_debug(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    /* Ensure PC is synchronised */
    kvm_cpu_synchronize_state(cs);

    if (kvm_find_sw_breakpoint(cs, env->pc)) {
        return true;
    }

    return false;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    int ret = 0;
    switch (run->exit_reason) {
    case KVM_EXIT_RISCV_SBI:
        ret = kvm_riscv_handle_sbi(cs, run);
        break;
    case KVM_EXIT_RISCV_CSR:
        ret = kvm_riscv_handle_csr(cs, run);
        break;
    case KVM_EXIT_DEBUG:
        if (kvm_riscv_handle_debug(cs)) {
            ret = EXCP_DEBUG;
        }
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
    int i;

    for (i = 0; i < 32; i++) {
        env->gpr[i] = 0;
    }
    env->pc = cpu->env.kernel_addr;
    env->gpr[10] = kvm_arch_vcpu_id(CPU(cpu)); /* a0 */
    env->gpr[11] = cpu->env.fdt_addr;          /* a1 */

    kvm_riscv_reset_regs_csr(env);
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

static int aia_mode;

static const char *kvm_aia_mode_str(uint64_t mode)
{
    switch (mode) {
    case KVM_DEV_RISCV_AIA_MODE_EMUL:
        return "emul";
    case KVM_DEV_RISCV_AIA_MODE_HWACCEL:
        return "hwaccel";
    case KVM_DEV_RISCV_AIA_MODE_AUTO:
    default:
        return "auto";
    };
}

static char *riscv_get_kvm_aia(Object *obj, Error **errp)
{
    return g_strdup(kvm_aia_mode_str(aia_mode));
}

static void riscv_set_kvm_aia(Object *obj, const char *val, Error **errp)
{
    if (!strcmp(val, "emul")) {
        aia_mode = KVM_DEV_RISCV_AIA_MODE_EMUL;
    } else if (!strcmp(val, "hwaccel")) {
        aia_mode = KVM_DEV_RISCV_AIA_MODE_HWACCEL;
    } else if (!strcmp(val, "auto")) {
        aia_mode = KVM_DEV_RISCV_AIA_MODE_AUTO;
    } else {
        error_setg(errp, "Invalid KVM AIA mode");
        error_append_hint(errp, "Valid values are emul, hwaccel, and auto.\n");
    }
}

void kvm_arch_accel_class_init(ObjectClass *oc)
{
    object_class_property_add_str(oc, "riscv-aia", riscv_get_kvm_aia,
                                  riscv_set_kvm_aia);
    object_class_property_set_description(oc, "riscv-aia",
        "Set KVM AIA mode. Valid values are 'emul', 'hwaccel' and 'auto'. "
        "Changing KVM AIA modes relies on host support. Defaults to 'auto' "
        "if the host supports it");
    object_property_set_default_str(object_class_property_find(oc, "riscv-aia"),
                                    "auto");
}

void kvm_riscv_aia_create(MachineState *machine, uint64_t group_shift,
                          uint64_t aia_irq_num, uint64_t aia_msi_num,
                          uint64_t aplic_base, uint64_t imsic_base,
                          uint64_t guest_num)
{
    int ret, i;
    int aia_fd = -1;
    uint64_t default_aia_mode;
    uint64_t socket_count = riscv_socket_count(machine);
    uint64_t max_hart_per_socket = 0;
    uint64_t socket, base_hart, hart_count, socket_imsic_base, imsic_addr;
    uint64_t socket_bits, hart_bits, guest_bits;
    uint64_t max_group_id;

    aia_fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_RISCV_AIA, false);

    if (aia_fd < 0) {
        error_report("Unable to create in-kernel irqchip");
        exit(1);
    }

    ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CONFIG,
                            KVM_DEV_RISCV_AIA_CONFIG_MODE,
                            &default_aia_mode, false, NULL);
    if (ret < 0) {
        error_report("KVM AIA: failed to get current KVM AIA mode");
        exit(1);
    }

    if (default_aia_mode != aia_mode) {
        ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                KVM_DEV_RISCV_AIA_CONFIG_MODE,
                                &aia_mode, true, NULL);
        if (ret < 0) {
            warn_report("KVM AIA: failed to set KVM AIA mode '%s', using "
                        "default host mode '%s'",
                        kvm_aia_mode_str(aia_mode),
                        kvm_aia_mode_str(default_aia_mode));

            /* failed to change AIA mode, use default */
            aia_mode = default_aia_mode;
        }
    }

    /*
     * Skip APLIC creation in KVM if we're running split mode.
     * This is done by leaving KVM_DEV_RISCV_AIA_CONFIG_SRCS
     * unset. We can also skip KVM_DEV_RISCV_AIA_ADDR_APLIC
     * since KVM won't be using it.
     */
    if (!kvm_kernel_irqchip_split()) {
        ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                KVM_DEV_RISCV_AIA_CONFIG_SRCS,
                                &aia_irq_num, true, NULL);
        if (ret < 0) {
            error_report("KVM AIA: failed to set number of input irq lines");
            exit(1);
        }

        ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR,
                                KVM_DEV_RISCV_AIA_ADDR_APLIC,
                                &aplic_base, true, NULL);
        if (ret < 0) {
            error_report("KVM AIA: failed to set the base address of APLIC");
            exit(1);
        }
     }

    ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CONFIG,
                            KVM_DEV_RISCV_AIA_CONFIG_IDS,
                            &aia_msi_num, true, NULL);
    if (ret < 0) {
        error_report("KVM AIA: failed to set number of msi");
        exit(1);
    }


    if (socket_count > 1) {
        max_group_id = socket_count - 1;
        socket_bits = find_last_bit(&max_group_id, BITS_PER_LONG) + 1;
        ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                KVM_DEV_RISCV_AIA_CONFIG_GROUP_BITS,
                                &socket_bits, true, NULL);
        if (ret < 0) {
            error_report("KVM AIA: failed to set group_bits");
            exit(1);
        }

        ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                KVM_DEV_RISCV_AIA_CONFIG_GROUP_SHIFT,
                                &group_shift, true, NULL);
        if (ret < 0) {
            error_report("KVM AIA: failed to set group_shift");
            exit(1);
        }
    }

    guest_bits = guest_num == 0 ? 0 :
                 find_last_bit(&guest_num, BITS_PER_LONG) + 1;
    ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CONFIG,
                            KVM_DEV_RISCV_AIA_CONFIG_GUEST_BITS,
                            &guest_bits, true, NULL);
    if (ret < 0) {
        error_report("KVM AIA: failed to set guest_bits");
        exit(1);
    }

    for (socket = 0; socket < socket_count; socket++) {
        socket_imsic_base = imsic_base + socket * (1U << group_shift);
        hart_count = riscv_socket_hart_count(machine, socket);
        base_hart = riscv_socket_first_hartid(machine, socket);

        if (max_hart_per_socket < hart_count) {
            max_hart_per_socket = hart_count;
        }

        for (i = 0; i < hart_count; i++) {
            imsic_addr = socket_imsic_base + i * IMSIC_HART_SIZE(guest_bits);
            ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR,
                                    KVM_DEV_RISCV_AIA_ADDR_IMSIC(i + base_hart),
                                    &imsic_addr, true, NULL);
            if (ret < 0) {
                error_report("KVM AIA: failed to set the IMSIC address for hart %d", i);
                exit(1);
            }
        }
    }


    if (max_hart_per_socket > 1) {
        max_hart_per_socket--;
        hart_bits = find_last_bit(&max_hart_per_socket, BITS_PER_LONG) + 1;
    } else {
        hart_bits = 0;
    }

    ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CONFIG,
                            KVM_DEV_RISCV_AIA_CONFIG_HART_BITS,
                            &hart_bits, true, NULL);
    if (ret < 0) {
        error_report("KVM AIA: failed to set hart_bits");
        exit(1);
    }

    if (kvm_has_gsi_routing()) {
        for (uint64_t idx = 0; idx < aia_irq_num + 1; ++idx) {
            /* KVM AIA only has one APLIC instance */
            kvm_irqchip_add_irq_route(kvm_state, idx, 0, idx);
        }
        kvm_gsi_routing_allowed = true;
        kvm_irqchip_commit_routes(kvm_state);
    }

    ret = kvm_device_access(aia_fd, KVM_DEV_RISCV_AIA_GRP_CTRL,
                            KVM_DEV_RISCV_AIA_CTRL_INIT,
                            NULL, true, NULL);
    if (ret < 0) {
        error_report("KVM AIA: initialized fail");
        exit(1);
    }

    kvm_msi_via_irqfd_allowed = true;
}

static void kvm_cpu_instance_init(CPUState *cs)
{
    Object *obj = OBJECT(RISCV_CPU(cs));

    riscv_init_kvm_registers(obj);

    kvm_riscv_add_cpu_user_properties(obj);
}

/*
 * We'll get here via the following path:
 *
 * riscv_cpu_realize()
 *   -> cpu_exec_realizefn()
 *      -> kvm_cpu_realize() (via accel_cpu_common_realize())
 */
static bool kvm_cpu_realize(CPUState *cs, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    int ret;

    if (riscv_has_ext(&cpu->env, RVV)) {
        ret = prctl(PR_RISCV_V_SET_CONTROL, PR_RISCV_V_VSTATE_CTRL_ON);
        if (ret) {
            error_setg(errp, "Error in prctl PR_RISCV_V_SET_CONTROL, code: %s",
                       strerrorname_np(errno));
            return false;
        }
    }

   return true;
}

void riscv_kvm_cpu_finalize_features(RISCVCPU *cpu, Error **errp)
{
    CPURISCVState *env = &cpu->env;
    KVMScratchCPU kvmcpu;
    struct kvm_one_reg reg;
    uint64_t val;
    int ret;

    /* short-circuit without spinning the scratch CPU */
    if (!cpu->cfg.ext_zicbom && !cpu->cfg.ext_zicboz &&
        !riscv_has_ext(env, RVV)) {
        return;
    }

    if (!kvm_riscv_create_scratch_vcpu(&kvmcpu)) {
        error_setg(errp, "Unable to create scratch KVM cpu");
        return;
    }

    if (cpu->cfg.ext_zicbom &&
        riscv_cpu_option_set(kvm_cbom_blocksize.name)) {

        reg.id = kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_CONFIG,
                                        kvm_cbom_blocksize.kvm_reg_id);
        reg.addr = (uint64_t)&val;
        ret = ioctl(kvmcpu.cpufd, KVM_GET_ONE_REG, &reg);
        if (ret != 0) {
            error_setg(errp, "Unable to read cbom_blocksize, error %d", errno);
            return;
        }

        if (cpu->cfg.cbom_blocksize != val) {
            error_setg(errp, "Unable to set cbom_blocksize to a different "
                       "value than the host (%lu)", val);
            return;
        }
    }

    if (cpu->cfg.ext_zicboz &&
        riscv_cpu_option_set(kvm_cboz_blocksize.name)) {

        reg.id = kvm_riscv_reg_id_ulong(env, KVM_REG_RISCV_CONFIG,
                                        kvm_cboz_blocksize.kvm_reg_id);
        reg.addr = (uint64_t)&val;
        ret = ioctl(kvmcpu.cpufd, KVM_GET_ONE_REG, &reg);
        if (ret != 0) {
            error_setg(errp, "Unable to read cboz_blocksize, error %d", errno);
            return;
        }

        if (cpu->cfg.cboz_blocksize != val) {
            error_setg(errp, "Unable to set cboz_blocksize to a different "
                       "value than the host (%lu)", val);
            return;
        }
    }

    /* Users are setting vlen, not vlenb */
    if (riscv_has_ext(env, RVV) && riscv_cpu_option_set("vlen")) {
        if (!kvm_v_vlenb.supported) {
            error_setg(errp, "Unable to set 'vlenb': register not supported");
            return;
        }

        reg.id = kvm_v_vlenb.kvm_reg_id;
        reg.addr = (uint64_t)&val;
        ret = ioctl(kvmcpu.cpufd, KVM_GET_ONE_REG, &reg);
        if (ret != 0) {
            error_setg(errp, "Unable to read vlenb register, error %d", errno);
            return;
        }

        if (cpu->cfg.vlenb != val) {
            error_setg(errp, "Unable to set 'vlen' to a different "
                       "value than the host (%lu)", val * 8);
            return;
        }
    }

    kvm_riscv_destroy_scratch_vcpu(&kvmcpu);
}

static void kvm_cpu_accel_class_init(ObjectClass *oc, void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_instance_init = kvm_cpu_instance_init;
    acc->cpu_target_realize = kvm_cpu_realize;
}

static const TypeInfo kvm_cpu_accel_type_info = {
    .name = ACCEL_CPU_NAME("kvm"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = kvm_cpu_accel_class_init,
    .abstract = true,
};
static void kvm_cpu_accel_register_types(void)
{
    type_register_static(&kvm_cpu_accel_type_info);
}
type_init(kvm_cpu_accel_register_types);

static void riscv_host_cpu_class_init(ObjectClass *c, void *data)
{
    RISCVCPUClass *mcc = RISCV_CPU_CLASS(c);

#if defined(TARGET_RISCV32)
    mcc->misa_mxl_max = MXL_RV32;
#elif defined(TARGET_RISCV64)
    mcc->misa_mxl_max = MXL_RV64;
#endif
}

static const TypeInfo riscv_kvm_cpu_type_infos[] = {
    {
        .name = TYPE_RISCV_CPU_HOST,
        .parent = TYPE_RISCV_CPU,
        .class_init = riscv_host_cpu_class_init,
    }
};

DEFINE_TYPES(riscv_kvm_cpu_type_infos)

static const uint32_t ebreak_insn = 0x00100073;
static const uint16_t c_ebreak_insn = 0x9002;

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 2, 0)) {
        return -EINVAL;
    }

    if ((bp->saved_insn & 0x3) == 0x3) {
        if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 4, 0)
            || cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&ebreak_insn, 4, 1)) {
            return -EINVAL;
        }
    } else {
        if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&c_ebreak_insn, 2, 1)) {
            return -EINVAL;
        }
    }

    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    uint32_t ebreak;
    uint16_t c_ebreak;

    if ((bp->saved_insn & 0x3) == 0x3) {
        if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&ebreak, 4, 0) ||
            ebreak != ebreak_insn ||
            cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 4, 1)) {
            return -EINVAL;
        }
    } else {
        if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&c_ebreak, 2, 0) ||
            c_ebreak != c_ebreak_insn ||
            cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 2, 1)) {
            return -EINVAL;
        }
    }

    return 0;
}

int kvm_arch_insert_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    /* TODO; To be implemented later. */
    return -EINVAL;
}

int kvm_arch_remove_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    /* TODO; To be implemented later. */
    return -EINVAL;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
    /* TODO; To be implemented later. */
}

void kvm_arch_update_guest_debug(CPUState *cs, struct kvm_guest_debug *dbg)
{
    if (kvm_sw_breakpoints_active(cs)) {
        dbg->control |= KVM_GUESTDBG_ENABLE;
    }
}
