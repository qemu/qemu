/*
 * riscv TCG cpu class initialization
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
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
#include "exec/exec-all.h"
#include "tcg-cpu.h"
#include "cpu.h"
#include "pmu.h"
#include "time_helper.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/accel.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/core/accel-cpu.h"
#include "hw/core/tcg-cpu-ops.h"
#include "tcg/tcg.h"

/* Hash that stores user set extensions */
static GHashTable *multi_ext_user_opts;

static bool cpu_cfg_ext_is_user_set(uint32_t ext_offset)
{
    return g_hash_table_contains(multi_ext_user_opts,
                                 GUINT_TO_POINTER(ext_offset));
}

static void riscv_cpu_synchronize_from_tb(CPUState *cs,
                                          const TranslationBlock *tb)
{
    if (!(tb_cflags(tb) & CF_PCREL)) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;
        RISCVMXL xl = FIELD_EX32(tb->flags, TB_FLAGS, XL);

        tcg_debug_assert(!(cs->tcg_cflags & CF_PCREL));

        if (xl == MXL_RV32) {
            env->pc = (int32_t) tb->pc;
        } else {
            env->pc = tb->pc;
        }
    }
}

static void riscv_restore_state_to_opc(CPUState *cs,
                                       const TranslationBlock *tb,
                                       const uint64_t *data)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    RISCVMXL xl = FIELD_EX32(tb->flags, TB_FLAGS, XL);
    target_ulong pc;

    if (tb_cflags(tb) & CF_PCREL) {
        pc = (env->pc & TARGET_PAGE_MASK) | data[0];
    } else {
        pc = data[0];
    }

    if (xl == MXL_RV32) {
        env->pc = (int32_t)pc;
    } else {
        env->pc = pc;
    }
    env->bins = data[1];
}

static const struct TCGCPUOps riscv_tcg_ops = {
    .initialize = riscv_translate_init,
    .synchronize_from_tb = riscv_cpu_synchronize_from_tb,
    .restore_state_to_opc = riscv_restore_state_to_opc,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = riscv_cpu_tlb_fill,
    .cpu_exec_interrupt = riscv_cpu_exec_interrupt,
    .do_interrupt = riscv_cpu_do_interrupt,
    .do_transaction_failed = riscv_cpu_do_transaction_failed,
    .do_unaligned_access = riscv_cpu_do_unaligned_access,
    .debug_excp_handler = riscv_cpu_debug_excp_handler,
    .debug_check_breakpoint = riscv_cpu_debug_check_breakpoint,
    .debug_check_watchpoint = riscv_cpu_debug_check_watchpoint,
#endif /* !CONFIG_USER_ONLY */
};

static int cpu_cfg_ext_get_min_version(uint32_t ext_offset)
{
    const RISCVIsaExtData *edata;

    for (edata = isa_edata_arr; edata && edata->name; edata++) {
        if (edata->ext_enable_offset != ext_offset) {
            continue;
        }

        return edata->min_version;
    }

    g_assert_not_reached();
}

static void cpu_cfg_ext_auto_update(RISCVCPU *cpu, uint32_t ext_offset,
                                    bool value)
{
    CPURISCVState *env = &cpu->env;
    bool prev_val = isa_ext_is_enabled(cpu, ext_offset);
    int min_version;

    if (prev_val == value) {
        return;
    }

    if (cpu_cfg_ext_is_user_set(ext_offset)) {
        return;
    }

    if (value && env->priv_ver != PRIV_VERSION_LATEST) {
        /* Do not enable it if priv_ver is older than min_version */
        min_version = cpu_cfg_ext_get_min_version(ext_offset);
        if (env->priv_ver < min_version) {
            return;
        }
    }

    isa_ext_update_enabled(cpu, ext_offset, value);
}

static void riscv_cpu_validate_misa_priv(CPURISCVState *env, Error **errp)
{
    if (riscv_has_ext(env, RVH) && env->priv_ver < PRIV_VERSION_1_12_0) {
        error_setg(errp, "H extension requires priv spec 1.12.0");
        return;
    }
}

static void riscv_cpu_validate_misa_mxl(RISCVCPU *cpu, Error **errp)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPUClass *cc = CPU_CLASS(mcc);
    CPURISCVState *env = &cpu->env;

    /* Validate that MISA_MXL is set properly. */
    switch (env->misa_mxl_max) {
#ifdef TARGET_RISCV64
    case MXL_RV64:
    case MXL_RV128:
        cc->gdb_core_xml_file = "riscv-64bit-cpu.xml";
        break;
#endif
    case MXL_RV32:
        cc->gdb_core_xml_file = "riscv-32bit-cpu.xml";
        break;
    default:
        g_assert_not_reached();
    }

    if (env->misa_mxl_max != env->misa_mxl) {
        error_setg(errp, "misa_mxl_max must be equal to misa_mxl");
        return;
    }
}

static void riscv_cpu_validate_priv_spec(RISCVCPU *cpu, Error **errp)
{
    CPURISCVState *env = &cpu->env;
    int priv_version = -1;

    if (cpu->cfg.priv_spec) {
        if (!g_strcmp0(cpu->cfg.priv_spec, "v1.12.0")) {
            priv_version = PRIV_VERSION_1_12_0;
        } else if (!g_strcmp0(cpu->cfg.priv_spec, "v1.11.0")) {
            priv_version = PRIV_VERSION_1_11_0;
        } else if (!g_strcmp0(cpu->cfg.priv_spec, "v1.10.0")) {
            priv_version = PRIV_VERSION_1_10_0;
        } else {
            error_setg(errp,
                       "Unsupported privilege spec version '%s'",
                       cpu->cfg.priv_spec);
            return;
        }

        env->priv_ver = priv_version;
    }
}

static void riscv_cpu_validate_v(CPURISCVState *env, RISCVCPUConfig *cfg,
                                 Error **errp)
{
    if (!is_power_of_2(cfg->vlen)) {
        error_setg(errp, "Vector extension VLEN must be power of 2");
        return;
    }

    if (cfg->vlen > RV_VLEN_MAX || cfg->vlen < 128) {
        error_setg(errp,
                   "Vector extension implementation only supports VLEN "
                   "in the range [128, %d]", RV_VLEN_MAX);
        return;
    }

    if (!is_power_of_2(cfg->elen)) {
        error_setg(errp, "Vector extension ELEN must be power of 2");
        return;
    }

    if (cfg->elen > 64 || cfg->elen < 8) {
        error_setg(errp,
                   "Vector extension implementation only supports ELEN "
                   "in the range [8, 64]");
        return;
    }

    if (cfg->vext_spec) {
        if (!g_strcmp0(cfg->vext_spec, "v1.0")) {
            env->vext_ver = VEXT_VERSION_1_00_0;
        } else {
            error_setg(errp, "Unsupported vector spec version '%s'",
                       cfg->vext_spec);
            return;
        }
    } else if (env->vext_ver == 0) {
        qemu_log("vector version is not specified, "
                 "use the default value v1.0\n");

        env->vext_ver = VEXT_VERSION_1_00_0;
    }
}

static void riscv_cpu_disable_priv_spec_isa_exts(RISCVCPU *cpu)
{
    CPURISCVState *env = &cpu->env;
    const RISCVIsaExtData *edata;

    /* Force disable extensions if priv spec version does not match */
    for (edata = isa_edata_arr; edata && edata->name; edata++) {
        if (isa_ext_is_enabled(cpu, edata->ext_enable_offset) &&
            (env->priv_ver < edata->min_version)) {
            isa_ext_update_enabled(cpu, edata->ext_enable_offset, false);
#ifndef CONFIG_USER_ONLY
            warn_report("disabling %s extension for hart 0x" TARGET_FMT_lx
                        " because privilege spec version does not match",
                        edata->name, env->mhartid);
#else
            warn_report("disabling %s extension because "
                        "privilege spec version does not match",
                        edata->name);
#endif
        }
    }
}

/*
 * Check consistency between chosen extensions while setting
 * cpu->cfg accordingly.
 */
void riscv_cpu_validate_set_extensions(RISCVCPU *cpu, Error **errp)
{
    CPURISCVState *env = &cpu->env;
    Error *local_err = NULL;

    /* Do some ISA extension error checking */
    if (riscv_has_ext(env, RVG) &&
        !(riscv_has_ext(env, RVI) && riscv_has_ext(env, RVM) &&
          riscv_has_ext(env, RVA) && riscv_has_ext(env, RVF) &&
          riscv_has_ext(env, RVD) &&
          cpu->cfg.ext_zicsr && cpu->cfg.ext_zifencei)) {

        if (cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zicsr)) &&
            !cpu->cfg.ext_zicsr) {
            error_setg(errp, "RVG requires Zicsr but user set Zicsr to false");
            return;
        }

        if (cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zifencei)) &&
            !cpu->cfg.ext_zifencei) {
            error_setg(errp, "RVG requires Zifencei but user set "
                       "Zifencei to false");
            return;
        }

        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zicsr), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zifencei), true);

        env->misa_ext |= RVI | RVM | RVA | RVF | RVD;
        env->misa_ext_mask |= RVI | RVM | RVA | RVF | RVD;
    }

    if (riscv_has_ext(env, RVI) && riscv_has_ext(env, RVE)) {
        error_setg(errp,
                   "I and E extensions are incompatible");
        return;
    }

    if (!riscv_has_ext(env, RVI) && !riscv_has_ext(env, RVE)) {
        error_setg(errp,
                   "Either I or E extension must be set");
        return;
    }

    if (riscv_has_ext(env, RVS) && !riscv_has_ext(env, RVU)) {
        error_setg(errp,
                   "Setting S extension without U extension is illegal");
        return;
    }

    if (riscv_has_ext(env, RVH) && !riscv_has_ext(env, RVI)) {
        error_setg(errp,
                   "H depends on an I base integer ISA with 32 x registers");
        return;
    }

    if (riscv_has_ext(env, RVH) && !riscv_has_ext(env, RVS)) {
        error_setg(errp, "H extension implicitly requires S-mode");
        return;
    }

    if (riscv_has_ext(env, RVF) && !cpu->cfg.ext_zicsr) {
        error_setg(errp, "F extension requires Zicsr");
        return;
    }

    if ((cpu->cfg.ext_zawrs) && !riscv_has_ext(env, RVA)) {
        error_setg(errp, "Zawrs extension requires A extension");
        return;
    }

    if (cpu->cfg.ext_zfa && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zfa extension requires F extension");
        return;
    }

    if (cpu->cfg.ext_zfh) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zfhmin), true);
    }

    if (cpu->cfg.ext_zfhmin && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zfh/Zfhmin extensions require F extension");
        return;
    }

    if (cpu->cfg.ext_zfbfmin && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zfbfmin extension depends on F extension");
        return;
    }

    if (riscv_has_ext(env, RVD) && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "D extension requires F extension");
        return;
    }

    if (riscv_has_ext(env, RVV)) {
        riscv_cpu_validate_v(env, &cpu->cfg, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            return;
        }

        /* The V vector extension depends on the Zve64d extension */
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zve64d), true);
    }

    /* The Zve64d extension depends on the Zve64f extension */
    if (cpu->cfg.ext_zve64d) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zve64f), true);
    }

    /* The Zve64f extension depends on the Zve32f extension */
    if (cpu->cfg.ext_zve64f) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zve32f), true);
    }

    if (cpu->cfg.ext_zve64d && !riscv_has_ext(env, RVD)) {
        error_setg(errp, "Zve64d/V extensions require D extension");
        return;
    }

    if (cpu->cfg.ext_zve32f && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zve32f/Zve64f extensions require F extension");
        return;
    }

    if (cpu->cfg.ext_zvfh) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zvfhmin), true);
    }

    if (cpu->cfg.ext_zvfhmin && !cpu->cfg.ext_zve32f) {
        error_setg(errp, "Zvfh/Zvfhmin extensions require Zve32f extension");
        return;
    }

    if (cpu->cfg.ext_zvfh && !cpu->cfg.ext_zfhmin) {
        error_setg(errp, "Zvfh extensions requires Zfhmin extension");
        return;
    }

    if (cpu->cfg.ext_zvfbfmin && !cpu->cfg.ext_zfbfmin) {
        error_setg(errp, "Zvfbfmin extension depends on Zfbfmin extension");
        return;
    }

    if (cpu->cfg.ext_zvfbfmin && !cpu->cfg.ext_zve32f) {
        error_setg(errp, "Zvfbfmin extension depends on Zve32f extension");
        return;
    }

    if (cpu->cfg.ext_zvfbfwma && !cpu->cfg.ext_zvfbfmin) {
        error_setg(errp, "Zvfbfwma extension depends on Zvfbfmin extension");
        return;
    }

    /* Set the ISA extensions, checks should have happened above */
    if (cpu->cfg.ext_zhinx) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zca), true);
    }

    if ((cpu->cfg.ext_zdinx || cpu->cfg.ext_zhinxmin) && !cpu->cfg.ext_zfinx) {
        error_setg(errp, "Zdinx/Zhinx/Zhinxmin extensions require Zfinx");
        return;
    }

    if (cpu->cfg.ext_zfinx) {
        if (!cpu->cfg.ext_zicsr) {
            error_setg(errp, "Zfinx extension requires Zicsr");
            return;
        }
        if (riscv_has_ext(env, RVF)) {
            error_setg(errp,
                       "Zfinx cannot be supported together with F extension");
            return;
        }
    }

    if (cpu->cfg.ext_zce) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zca), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcb), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcmp), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcmt), true);
        if (riscv_has_ext(env, RVF) && env->misa_mxl_max == MXL_RV32) {
            cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcf), true);
        }
    }

    /* zca, zcd and zcf has a PRIV 1.12.0 restriction */
    if (riscv_has_ext(env, RVC) && env->priv_ver >= PRIV_VERSION_1_12_0) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zca), true);
        if (riscv_has_ext(env, RVF) && env->misa_mxl_max == MXL_RV32) {
            cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcf), true);
        }
        if (riscv_has_ext(env, RVD)) {
            cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcd), true);
        }
    }

    if (env->misa_mxl_max != MXL_RV32 && cpu->cfg.ext_zcf) {
        error_setg(errp, "Zcf extension is only relevant to RV32");
        return;
    }

    if (!riscv_has_ext(env, RVF) && cpu->cfg.ext_zcf) {
        error_setg(errp, "Zcf extension requires F extension");
        return;
    }

    if (!riscv_has_ext(env, RVD) && cpu->cfg.ext_zcd) {
        error_setg(errp, "Zcd extension requires D extension");
        return;
    }

    if ((cpu->cfg.ext_zcf || cpu->cfg.ext_zcd || cpu->cfg.ext_zcb ||
         cpu->cfg.ext_zcmp || cpu->cfg.ext_zcmt) && !cpu->cfg.ext_zca) {
        error_setg(errp, "Zcf/Zcd/Zcb/Zcmp/Zcmt extensions require Zca "
                         "extension");
        return;
    }

    if (cpu->cfg.ext_zcd && (cpu->cfg.ext_zcmp || cpu->cfg.ext_zcmt)) {
        error_setg(errp, "Zcmp/Zcmt extensions are incompatible with "
                         "Zcd extension");
        return;
    }

    if (cpu->cfg.ext_zcmt && !cpu->cfg.ext_zicsr) {
        error_setg(errp, "Zcmt extension requires Zicsr extension");
        return;
    }

    /*
     * In principle Zve*x would also suffice here, were they supported
     * in qemu
     */
    if ((cpu->cfg.ext_zvbb || cpu->cfg.ext_zvkg || cpu->cfg.ext_zvkned ||
         cpu->cfg.ext_zvknha || cpu->cfg.ext_zvksed || cpu->cfg.ext_zvksh) &&
        !cpu->cfg.ext_zve32f) {
        error_setg(errp,
                   "Vector crypto extensions require V or Zve* extensions");
        return;
    }

    if ((cpu->cfg.ext_zvbc || cpu->cfg.ext_zvknhb) && !cpu->cfg.ext_zve64f) {
        error_setg(
            errp,
            "Zvbc and Zvknhb extensions require V or Zve64{f,d} extensions");
        return;
    }

    if (cpu->cfg.ext_zk) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zkn), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zkr), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zkt), true);
    }

    if (cpu->cfg.ext_zkn) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zbkb), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zbkc), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zbkx), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zkne), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zknd), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zknh), true);
    }

    if (cpu->cfg.ext_zks) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zbkb), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zbkc), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zbkx), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zksed), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zksh), true);
    }

    /*
     * Disable isa extensions based on priv spec after we
     * validated and set everything we need.
     */
    riscv_cpu_disable_priv_spec_isa_exts(cpu);
}

void riscv_tcg_cpu_finalize_features(RISCVCPU *cpu, Error **errp)
{
    CPURISCVState *env = &cpu->env;
    Error *local_err = NULL;

    riscv_cpu_validate_priv_spec(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    riscv_cpu_validate_misa_priv(env, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    if (cpu->cfg.epmp && !cpu->cfg.pmp) {
        /*
         * Enhanced PMP should only be available
         * on harts with PMP support
         */
        error_setg(errp, "Invalid configuration: EPMP requires PMP support");
        return;
    }

    riscv_cpu_validate_set_extensions(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
}

static bool riscv_cpu_is_generic(Object *cpu_obj)
{
    return object_dynamic_cast(cpu_obj, TYPE_RISCV_DYNAMIC_CPU) != NULL;
}

/*
 * We'll get here via the following path:
 *
 * riscv_cpu_realize()
 *   -> cpu_exec_realizefn()
 *      -> tcg_cpu_realize() (via accel_cpu_common_realize())
 */
static bool tcg_cpu_realize(CPUState *cs, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    Error *local_err = NULL;

    if (object_dynamic_cast(OBJECT(cpu), TYPE_RISCV_CPU_HOST)) {
        g_autofree char *name = riscv_cpu_get_name(cpu);
        error_setg(errp, "'%s' CPU is not compatible with TCG acceleration",
                   name);
        return false;
    }

    riscv_cpu_validate_misa_mxl(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return false;
    }

#ifndef CONFIG_USER_ONLY
    CPURISCVState *env = &cpu->env;

    CPU(cs)->tcg_cflags |= CF_PCREL;

    if (cpu->cfg.ext_sstc) {
        riscv_timer_init(cpu);
    }

    if (cpu->cfg.pmu_num) {
        if (!riscv_pmu_init(cpu, cpu->cfg.pmu_num) && cpu->cfg.ext_sscofpmf) {
            cpu->pmu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          riscv_pmu_timer_cb, cpu);
        }
    }

    /* With H-Ext, VSSIP, VSTIP, VSEIP and SGEIP are hardwired to one. */
    if (riscv_has_ext(env, RVH)) {
        env->mideleg = MIP_VSSIP | MIP_VSTIP | MIP_VSEIP | MIP_SGEIP;
    }
#endif

    return true;
}

typedef struct RISCVCPUMisaExtConfig {
    target_ulong misa_bit;
    bool enabled;
} RISCVCPUMisaExtConfig;

static void cpu_set_misa_ext_cfg(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    const RISCVCPUMisaExtConfig *misa_ext_cfg = opaque;
    target_ulong misa_bit = misa_ext_cfg->misa_bit;
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    bool generic_cpu = riscv_cpu_is_generic(obj);
    bool prev_val, value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    prev_val = env->misa_ext & misa_bit;

    if (value == prev_val) {
        return;
    }

    if (value) {
        if (!generic_cpu) {
            g_autofree char *cpuname = riscv_cpu_get_name(cpu);
            error_setg(errp, "'%s' CPU does not allow enabling extensions",
                       cpuname);
            return;
        }

        env->misa_ext |= misa_bit;
        env->misa_ext_mask |= misa_bit;
    } else {
        env->misa_ext &= ~misa_bit;
        env->misa_ext_mask &= ~misa_bit;
    }
}

static void cpu_get_misa_ext_cfg(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    const RISCVCPUMisaExtConfig *misa_ext_cfg = opaque;
    target_ulong misa_bit = misa_ext_cfg->misa_bit;
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    bool value;

    value = env->misa_ext & misa_bit;

    visit_type_bool(v, name, &value, errp);
}

#define MISA_CFG(_bit, _enabled) \
    {.misa_bit = _bit, .enabled = _enabled}

static const RISCVCPUMisaExtConfig misa_ext_cfgs[] = {
    MISA_CFG(RVA, true),
    MISA_CFG(RVC, true),
    MISA_CFG(RVD, true),
    MISA_CFG(RVF, true),
    MISA_CFG(RVI, true),
    MISA_CFG(RVE, false),
    MISA_CFG(RVM, true),
    MISA_CFG(RVS, true),
    MISA_CFG(RVU, true),
    MISA_CFG(RVH, true),
    MISA_CFG(RVJ, false),
    MISA_CFG(RVV, false),
    MISA_CFG(RVG, false),
};

/*
 * We do not support user choice tracking for MISA
 * extensions yet because, so far, we do not silently
 * change MISA bits during realize() (RVG enables MISA
 * bits but the user is warned about it).
 */
static void riscv_cpu_add_misa_properties(Object *cpu_obj)
{
    bool use_def_vals = riscv_cpu_is_generic(cpu_obj);
    int i;

    for (i = 0; i < ARRAY_SIZE(misa_ext_cfgs); i++) {
        const RISCVCPUMisaExtConfig *misa_cfg = &misa_ext_cfgs[i];
        int bit = misa_cfg->misa_bit;
        const char *name = riscv_get_misa_ext_name(bit);
        const char *desc = riscv_get_misa_ext_description(bit);

        /* Check if KVM already created the property */
        if (object_property_find(cpu_obj, name)) {
            continue;
        }

        object_property_add(cpu_obj, name, "bool",
                            cpu_get_misa_ext_cfg,
                            cpu_set_misa_ext_cfg,
                            NULL, (void *)misa_cfg);
        object_property_set_description(cpu_obj, name, desc);
        if (use_def_vals) {
            object_property_set_bool(cpu_obj, name, misa_cfg->enabled, NULL);
        }
    }
}

static bool cpu_ext_is_deprecated(const char *ext_name)
{
    return isupper(ext_name[0]);
}

/*
 * String will be allocated in the heap. Caller is responsible
 * for freeing it.
 */
static char *cpu_ext_to_lower(const char *ext_name)
{
    char *ret = g_malloc0(strlen(ext_name) + 1);

    strcpy(ret, ext_name);
    ret[0] = tolower(ret[0]);

    return ret;
}

static void cpu_set_multi_ext_cfg(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    const RISCVCPUMultiExtConfig *multi_ext_cfg = opaque;
    RISCVCPU *cpu = RISCV_CPU(obj);
    bool generic_cpu = riscv_cpu_is_generic(obj);
    bool prev_val, value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    if (cpu_ext_is_deprecated(multi_ext_cfg->name)) {
        g_autofree char *lower = cpu_ext_to_lower(multi_ext_cfg->name);

        warn_report("CPU property '%s' is deprecated. Please use '%s' instead",
                    multi_ext_cfg->name, lower);
    }

    g_hash_table_insert(multi_ext_user_opts,
                        GUINT_TO_POINTER(multi_ext_cfg->offset),
                        (gpointer)value);

    prev_val = isa_ext_is_enabled(cpu, multi_ext_cfg->offset);

    if (value == prev_val) {
        return;
    }

    if (value && !generic_cpu) {
        g_autofree char *cpuname = riscv_cpu_get_name(cpu);
        error_setg(errp, "'%s' CPU does not allow enabling extensions",
                   cpuname);
        return;
    }

    isa_ext_update_enabled(cpu, multi_ext_cfg->offset, value);
}

static void cpu_get_multi_ext_cfg(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    const RISCVCPUMultiExtConfig *multi_ext_cfg = opaque;
    bool value = isa_ext_is_enabled(RISCV_CPU(obj), multi_ext_cfg->offset);

    visit_type_bool(v, name, &value, errp);
}

static void cpu_add_multi_ext_prop(Object *cpu_obj,
                                   const RISCVCPUMultiExtConfig *multi_cfg)
{
    bool generic_cpu = riscv_cpu_is_generic(cpu_obj);
    bool deprecated_ext = cpu_ext_is_deprecated(multi_cfg->name);

    object_property_add(cpu_obj, multi_cfg->name, "bool",
                        cpu_get_multi_ext_cfg,
                        cpu_set_multi_ext_cfg,
                        NULL, (void *)multi_cfg);

    if (!generic_cpu || deprecated_ext) {
        return;
    }

    /*
     * Set def val directly instead of using
     * object_property_set_bool() to save the set()
     * callback hash for user inputs.
     */
    isa_ext_update_enabled(RISCV_CPU(cpu_obj), multi_cfg->offset,
                           multi_cfg->enabled);
}

static void riscv_cpu_add_multiext_prop_array(Object *obj,
                                        const RISCVCPUMultiExtConfig *array)
{
    const RISCVCPUMultiExtConfig *prop;

    g_assert(array);

    for (prop = array; prop && prop->name; prop++) {
        cpu_add_multi_ext_prop(obj, prop);
    }
}

/*
 * Add CPU properties with user-facing flags.
 *
 * This will overwrite existing env->misa_ext values with the
 * defaults set via riscv_cpu_add_misa_properties().
 */
static void riscv_cpu_add_user_properties(Object *obj)
{
#ifndef CONFIG_USER_ONLY
    riscv_add_satp_mode_properties(obj);
#endif

    riscv_cpu_add_misa_properties(obj);

    riscv_cpu_add_multiext_prop_array(obj, riscv_cpu_extensions);
    riscv_cpu_add_multiext_prop_array(obj, riscv_cpu_vendor_exts);
    riscv_cpu_add_multiext_prop_array(obj, riscv_cpu_experimental_exts);

    riscv_cpu_add_multiext_prop_array(obj, riscv_cpu_deprecated_exts);

    for (Property *prop = riscv_cpu_options; prop && prop->name; prop++) {
        qdev_property_add_static(DEVICE(obj), prop);
    }
}

/*
 * The 'max' type CPU will have all possible ratified
 * non-vendor extensions enabled.
 */
static void riscv_init_max_cpu_extensions(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    const RISCVCPUMultiExtConfig *prop;

    /* Enable RVG, RVJ and RVV that are disabled by default */
    riscv_cpu_set_misa(env, env->misa_mxl, env->misa_ext | RVG | RVJ | RVV);

    for (prop = riscv_cpu_extensions; prop && prop->name; prop++) {
        isa_ext_update_enabled(cpu, prop->offset, true);
    }

    /* set vector version */
    env->vext_ver = VEXT_VERSION_1_00_0;

    /* Zfinx is not compatible with F. Disable it */
    isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_zfinx), false);
    isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_zdinx), false);
    isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_zhinx), false);
    isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_zhinxmin), false);

    isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_zce), false);
    isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_zcmp), false);
    isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_zcmt), false);

    if (env->misa_mxl != MXL_RV32) {
        isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_zcf), false);
    }
}

static bool riscv_cpu_has_max_extensions(Object *cpu_obj)
{
    return object_dynamic_cast(cpu_obj, TYPE_RISCV_CPU_MAX) != NULL;
}

static void tcg_cpu_instance_init(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    Object *obj = OBJECT(cpu);

    multi_ext_user_opts = g_hash_table_new(NULL, g_direct_equal);
    riscv_cpu_add_user_properties(obj);

    if (riscv_cpu_has_max_extensions(obj)) {
        riscv_init_max_cpu_extensions(obj);
    }
}

static void tcg_cpu_init_ops(AccelCPUClass *accel_cpu, CPUClass *cc)
{
    /*
     * All cpus use the same set of operations.
     */
    cc->tcg_ops = &riscv_tcg_ops;
}

static void tcg_cpu_class_init(CPUClass *cc)
{
    cc->init_accel_cpu = tcg_cpu_init_ops;
}

static void tcg_cpu_accel_class_init(ObjectClass *oc, void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_class_init = tcg_cpu_class_init;
    acc->cpu_instance_init = tcg_cpu_instance_init;
    acc->cpu_target_realize = tcg_cpu_realize;
}

static const TypeInfo tcg_cpu_accel_type_info = {
    .name = ACCEL_CPU_NAME("tcg"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = tcg_cpu_accel_class_init,
    .abstract = true,
};

static void tcg_cpu_accel_register_types(void)
{
    type_register_static(&tcg_cpu_accel_type_info);
}
type_init(tcg_cpu_accel_register_types);
