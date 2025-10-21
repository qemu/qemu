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
#include "exec/translation-block.h"
#include "tcg-cpu.h"
#include "cpu.h"
#include "exec/target_page.h"
#include "internals.h"
#include "pmu.h"
#include "time_helper.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/accel.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "accel/accel-cpu-target.h"
#include "accel/tcg/cpu-ops.h"
#include "tcg/tcg.h"
#ifndef CONFIG_USER_ONLY
#include "hw/boards.h"
#include "system/tcg.h"
#include "exec/icount.h"
#endif

/* Hash that stores user set extensions */
static GHashTable *multi_ext_user_opts;
static GHashTable *misa_ext_user_opts;

static GHashTable *multi_ext_implied_rules;
static GHashTable *misa_ext_implied_rules;

static bool cpu_cfg_ext_is_user_set(uint32_t ext_offset)
{
    return g_hash_table_contains(multi_ext_user_opts,
                                 GUINT_TO_POINTER(ext_offset));
}

static bool cpu_misa_ext_is_user_set(uint32_t misa_bit)
{
    return g_hash_table_contains(misa_ext_user_opts,
                                 GUINT_TO_POINTER(misa_bit));
}

static void cpu_cfg_ext_add_user_opt(uint32_t ext_offset, bool value)
{
    g_hash_table_insert(multi_ext_user_opts, GUINT_TO_POINTER(ext_offset),
                        (gpointer)value);
}

static void cpu_misa_ext_add_user_opt(uint32_t bit, bool value)
{
    g_hash_table_insert(misa_ext_user_opts, GUINT_TO_POINTER(bit),
                        (gpointer)value);
}

static void riscv_cpu_write_misa_bit(RISCVCPU *cpu, uint32_t bit,
                                     bool enabled)
{
    CPURISCVState *env = &cpu->env;

    if (enabled) {
        env->misa_ext |= bit;
        env->misa_ext_mask |= bit;
    } else {
        env->misa_ext &= ~bit;
        env->misa_ext_mask &= ~bit;
    }
}

static const char *cpu_priv_ver_to_str(int priv_ver)
{
    const char *priv_spec_str = priv_spec_to_str(priv_ver);

    g_assert(priv_spec_str);

    return priv_spec_str;
}

static int riscv_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return riscv_env_mmu_index(cpu_env(cs), ifetch);
}

static TCGTBCPUState riscv_get_tb_cpu_state(CPUState *cs)
{
    CPURISCVState *env = cpu_env(cs);
    RISCVCPU *cpu = env_archcpu(env);
    RISCVExtStatus fs, vs;
    uint32_t flags = 0;
    bool pm_signext = riscv_cpu_virt_mem_enabled(env);

    if (cpu->cfg.ext_zve32x) {
        /*
         * If env->vl equals to VLMAX, we can use generic vector operation
         * expanders (GVEC) to accerlate the vector operations.
         * However, as LMUL could be a fractional number. The maximum
         * vector size can be operated might be less than 8 bytes,
         * which is not supported by GVEC. So we set vl_eq_vlmax flag to true
         * only when maxsz >= 8 bytes.
         */

        /* lmul encoded as in DisasContext::lmul */
        int8_t lmul = sextract32(FIELD_EX64(env->vtype, VTYPE, VLMUL), 0, 3);
        uint32_t vsew = FIELD_EX64(env->vtype, VTYPE, VSEW);
        uint32_t vlmax = vext_get_vlmax(cpu->cfg.vlenb, vsew, lmul);
        uint32_t maxsz = vlmax << vsew;
        bool vl_eq_vlmax = (env->vstart == 0) && (vlmax == env->vl) &&
                           (maxsz >= 8);
        flags = FIELD_DP32(flags, TB_FLAGS, VILL, env->vill);
        flags = FIELD_DP32(flags, TB_FLAGS, SEW, vsew);
        flags = FIELD_DP32(flags, TB_FLAGS, LMUL,
                           FIELD_EX64(env->vtype, VTYPE, VLMUL));
        flags = FIELD_DP32(flags, TB_FLAGS, VL_EQ_VLMAX, vl_eq_vlmax);
        flags = FIELD_DP32(flags, TB_FLAGS, VTA,
                           FIELD_EX64(env->vtype, VTYPE, VTA));
        flags = FIELD_DP32(flags, TB_FLAGS, VMA,
                           FIELD_EX64(env->vtype, VTYPE, VMA));
        flags = FIELD_DP32(flags, TB_FLAGS, VSTART_EQ_ZERO, env->vstart == 0);
    } else {
        flags = FIELD_DP32(flags, TB_FLAGS, VILL, 1);
    }

    if (cpu_get_fcfien(env)) {
        /*
         * For Forward CFI, only the expectation of a lpad at
         * the start of the block is tracked via env->elp. env->elp
         * is turned on during jalr translation.
         */
        flags = FIELD_DP32(flags, TB_FLAGS, FCFI_LP_EXPECTED, env->elp);
        flags = FIELD_DP32(flags, TB_FLAGS, FCFI_ENABLED, 1);
    }

    if (cpu_get_bcfien(env)) {
        flags = FIELD_DP32(flags, TB_FLAGS, BCFI_ENABLED, 1);
    }

#ifdef CONFIG_USER_ONLY
    fs = EXT_STATUS_DIRTY;
    vs = EXT_STATUS_DIRTY;
#else
    flags = FIELD_DP32(flags, TB_FLAGS, PRIV, env->priv);

    flags |= riscv_env_mmu_index(env, 0);
    fs = get_field(env->mstatus, MSTATUS_FS);
    vs = get_field(env->mstatus, MSTATUS_VS);

    if (env->virt_enabled) {
        flags = FIELD_DP32(flags, TB_FLAGS, VIRT_ENABLED, 1);
        /*
         * Merge DISABLED and !DIRTY states using MIN.
         * We will set both fields when dirtying.
         */
        fs = MIN(fs, get_field(env->mstatus_hs, MSTATUS_FS));
        vs = MIN(vs, get_field(env->mstatus_hs, MSTATUS_VS));
    }

    /* With Zfinx, floating point is enabled/disabled by Smstateen. */
    if (!riscv_has_ext(env, RVF)) {
        fs = (smstateen_acc_ok(env, 0, SMSTATEEN0_FCSR) == RISCV_EXCP_NONE)
             ? EXT_STATUS_DIRTY : EXT_STATUS_DISABLED;
    }

    if (cpu->cfg.debug && !icount_enabled()) {
        flags = FIELD_DP32(flags, TB_FLAGS, ITRIGGER, env->itrigger_enabled);
    }
#endif

    flags = FIELD_DP32(flags, TB_FLAGS, FS, fs);
    flags = FIELD_DP32(flags, TB_FLAGS, VS, vs);
    flags = FIELD_DP32(flags, TB_FLAGS, XL, env->xl);
    flags = FIELD_DP32(flags, TB_FLAGS, AXL, cpu_address_xl(env));
    flags = FIELD_DP32(flags, TB_FLAGS, PM_PMM, riscv_pm_get_pmm(env));
    flags = FIELD_DP32(flags, TB_FLAGS, PM_SIGNEXTEND, pm_signext);

    return (TCGTBCPUState){
        .pc = env->xl == MXL_RV32 ? env->pc & UINT32_MAX : env->pc,
        .flags = flags,
        .cs_base = env->misa_ext,
    };
}

static void riscv_cpu_synchronize_from_tb(CPUState *cs,
                                          const TranslationBlock *tb)
{
    if (!(tb_cflags(tb) & CF_PCREL)) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;
        RISCVMXL xl = FIELD_EX32(tb->flags, TB_FLAGS, XL);

        tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));

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
    env->excp_uw2 = data[2];
}

#ifndef CONFIG_USER_ONLY
static vaddr riscv_pointer_wrap(CPUState *cs, int mmu_idx,
                                vaddr result, vaddr base)
{
    CPURISCVState *env = cpu_env(cs);
    uint32_t pm_len;
    bool pm_signext;

    if (cpu_address_xl(env) == MXL_RV32) {
        return (uint32_t)result;
    }

    pm_len = riscv_pm_get_pmlen(riscv_pm_get_pmm(env));
    if (pm_len == 0) {
        return result;
    }

    pm_signext = riscv_cpu_virt_mem_enabled(env);
    if (pm_signext) {
        return sextract64(result, 0, 64 - pm_len);
    }
    return extract64(result, 0, 64 - pm_len);
}
#endif

const TCGCPUOps riscv_tcg_ops = {
    .mttcg_supported = true,
    .guest_default_memory_order = 0,

    .initialize = riscv_translate_init,
    .translate_code = riscv_translate_code,
    .get_tb_cpu_state = riscv_get_tb_cpu_state,
    .synchronize_from_tb = riscv_cpu_synchronize_from_tb,
    .restore_state_to_opc = riscv_restore_state_to_opc,
    .mmu_index = riscv_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = riscv_cpu_tlb_fill,
    .pointer_wrap = riscv_pointer_wrap,
    .cpu_exec_interrupt = riscv_cpu_exec_interrupt,
    .cpu_exec_halt = riscv_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
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

static const char *cpu_cfg_ext_get_name(uint32_t ext_offset)
{
    const RISCVCPUMultiExtConfig *feat;
    const RISCVIsaExtData *edata;

    for (edata = isa_edata_arr; edata->name != NULL; edata++) {
        if (edata->ext_enable_offset == ext_offset) {
            return edata->name;
        }
    }

    for (feat = riscv_cpu_named_features; feat->name != NULL; feat++) {
        if (feat->offset == ext_offset) {
            return feat->name;
        }
    }

    g_assert_not_reached();
}

static bool cpu_cfg_offset_is_named_feat(uint32_t ext_offset)
{
    const RISCVCPUMultiExtConfig *feat;

    for (feat = riscv_cpu_named_features; feat->name != NULL; feat++) {
        if (feat->offset == ext_offset) {
            return true;
        }
    }

    return false;
}

static void riscv_cpu_enable_named_feat(RISCVCPU *cpu, uint32_t feat_offset)
{
     /*
      * All other named features are already enabled
      * in riscv_tcg_cpu_instance_init().
      */
    switch (feat_offset) {
    case CPU_CFG_OFFSET(ext_zic64b):
        cpu->cfg.cbom_blocksize = 64;
        cpu->cfg.cbop_blocksize = 64;
        cpu->cfg.cboz_blocksize = 64;
        break;
    case CPU_CFG_OFFSET(ext_sha):
        if (!cpu_misa_ext_is_user_set(RVH)) {
            riscv_cpu_write_misa_bit(cpu, RVH, true);
        }
        /* fallthrough */
    case CPU_CFG_OFFSET(ext_ssstateen):
        cpu->cfg.ext_smstateen = true;
        break;
    }
}

static void cpu_bump_multi_ext_priv_ver(CPURISCVState *env,
                                        uint32_t ext_offset)
{
    int ext_priv_ver;

    if (env->priv_ver == PRIV_VERSION_LATEST) {
        return;
    }

    ext_priv_ver = cpu_cfg_ext_get_min_version(ext_offset);

    if (env->priv_ver < ext_priv_ver) {
        /*
         * Note: the 'priv_spec' command line option, if present,
         * will take precedence over this priv_ver bump.
         */
        env->priv_ver = ext_priv_ver;
    }
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

static void riscv_cpu_validate_v(CPURISCVState *env, RISCVCPUConfig *cfg,
                                 Error **errp)
{
    uint32_t min_vlen;
    uint32_t vlen = cfg->vlenb << 3;

    if (riscv_has_ext(env, RVV)) {
        min_vlen = 128;
    } else if (cfg->ext_zve64x) {
        min_vlen = 64;
    } else if (cfg->ext_zve32x) {
        min_vlen = 32;
    } else {
        return;
    }

    if (vlen > RV_VLEN_MAX || vlen < min_vlen) {
        error_setg(errp,
                   "Vector extension implementation only supports VLEN "
                   "in the range [%d, %d]", min_vlen, RV_VLEN_MAX);
        return;
    }

    if (cfg->elen > 64 || cfg->elen < 8) {
        error_setg(errp,
                   "Vector extension implementation only supports ELEN "
                   "in the range [8, 64]");
        return;
    }

    if (vlen < cfg->elen) {
        error_setg(errp, "Vector extension implementation requires VLEN "
                         "to be greater than or equal to ELEN");
        return;
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
            /*
             * These two extensions are always enabled as they were supported
             * by QEMU before they were added as extensions in the ISA.
             */
            if (!strcmp(edata->name, "zicntr") ||
                !strcmp(edata->name, "zihpm")) {
                continue;
            }

            /*
             * cpu.debug = true is marked as 'sdtrig', priv spec 1.12.
             * Skip this warning since existing CPUs with older priv
             * spec and debug = true will be impacted.
             */
            if (!strcmp(edata->name, "sdtrig")) {
                continue;
            }

            isa_ext_update_enabled(cpu, edata->ext_enable_offset, false);

            /*
             * Do not show user warnings for named features that users
             * can't enable/disable in the command line. See commit
             * 68c9e54bea for more info.
             */
            if (cpu_cfg_offset_is_named_feat(edata->ext_enable_offset)) {
                continue;
            }
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

static void riscv_cpu_update_named_features(RISCVCPU *cpu)
{
    if (cpu->env.priv_ver >= PRIV_VERSION_1_11_0) {
        cpu->cfg.has_priv_1_11 = true;
    }

    if (cpu->env.priv_ver >= PRIV_VERSION_1_12_0) {
        cpu->cfg.has_priv_1_12 = true;
    }

    if (cpu->env.priv_ver >= PRIV_VERSION_1_13_0) {
        cpu->cfg.has_priv_1_13 = true;
    }

    cpu->cfg.ext_zic64b = cpu->cfg.cbom_blocksize == 64 &&
                          cpu->cfg.cbop_blocksize == 64 &&
                          cpu->cfg.cboz_blocksize == 64;

    cpu->cfg.ext_ssstateen = cpu->cfg.ext_smstateen;

    cpu->cfg.ext_sha = riscv_has_ext(&cpu->env, RVH) &&
                       cpu->cfg.ext_ssstateen;

    cpu->cfg.ext_ziccrse = cpu->cfg.has_priv_1_11;
}

static void riscv_cpu_validate_g(RISCVCPU *cpu)
{
    const char *warn_msg = "RVG mandates disabled extension %s";
    uint32_t g_misa_bits[] = {RVI, RVM, RVA, RVF, RVD};
    bool send_warn = cpu_misa_ext_is_user_set(RVG);

    for (int i = 0; i < ARRAY_SIZE(g_misa_bits); i++) {
        uint32_t bit = g_misa_bits[i];

        if (riscv_has_ext(&cpu->env, bit)) {
            continue;
        }

        if (!cpu_misa_ext_is_user_set(bit)) {
            riscv_cpu_write_misa_bit(cpu, bit, true);
            continue;
        }

        if (send_warn) {
            warn_report(warn_msg, riscv_get_misa_ext_name(bit));
        }
    }

    if (!cpu->cfg.ext_zicsr) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zicsr))) {
            cpu->cfg.ext_zicsr = true;
        } else if (send_warn) {
            warn_report(warn_msg, "zicsr");
        }
    }

    if (!cpu->cfg.ext_zifencei) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zifencei))) {
            cpu->cfg.ext_zifencei = true;
        } else if (send_warn) {
            warn_report(warn_msg, "zifencei");
        }
    }
}

static void riscv_cpu_validate_b(RISCVCPU *cpu)
{
    const char *warn_msg = "RVB mandates disabled extension %s";

    if (!cpu->cfg.ext_zba) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zba))) {
            cpu->cfg.ext_zba = true;
        } else {
            warn_report(warn_msg, "zba");
        }
    }

    if (!cpu->cfg.ext_zbb) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zbb))) {
            cpu->cfg.ext_zbb = true;
        } else {
            warn_report(warn_msg, "zbb");
        }
    }

    if (!cpu->cfg.ext_zbs) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zbs))) {
            cpu->cfg.ext_zbs = true;
        } else {
            warn_report(warn_msg, "zbs");
        }
    }
}

/*
 * Check consistency between chosen extensions while setting
 * cpu->cfg accordingly.
 */
void riscv_cpu_validate_set_extensions(RISCVCPU *cpu, Error **errp)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPURISCVState *env = &cpu->env;
    Error *local_err = NULL;

    if (riscv_has_ext(env, RVG)) {
        riscv_cpu_validate_g(cpu);
    }

    if (riscv_has_ext(env, RVB)) {
        riscv_cpu_validate_b(cpu);
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

    if ((cpu->cfg.ext_zacas) && !riscv_has_ext(env, RVA)) {
        error_setg(errp, "Zacas extension requires A extension");
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

    riscv_cpu_validate_v(env, &cpu->cfg, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    /* The Zve64d extension depends on the Zve64f extension */
    if (cpu->cfg.ext_zve64d) {
        if (!riscv_has_ext(env, RVD)) {
            error_setg(errp, "Zve64d/V extensions require D extension");
            return;
        }
    }

    /* The Zve32f extension depends on the Zve32x extension */
    if (cpu->cfg.ext_zve32f) {
        if (!riscv_has_ext(env, RVF)) {
            error_setg(errp, "Zve32f/Zve64f extensions require F extension");
            return;
        }
    }

    if (cpu->cfg.ext_zvfhmin && !cpu->cfg.ext_zve32f) {
        error_setg(errp, "Zvfh/Zvfhmin extensions require Zve32f extension");
        return;
    }

    if (cpu->cfg.ext_zvfh && !cpu->cfg.ext_zfhmin) {
        error_setg(errp, "Zvfh extensions requires Zfhmin extension");
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

    if (cpu->cfg.ext_zcmop && !cpu->cfg.ext_zca) {
        error_setg(errp, "Zcmop extensions require Zca");
        return;
    }

    if (mcc->def->misa_mxl_max != MXL_RV32 && cpu->cfg.ext_zcf) {
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

    if ((cpu->cfg.ext_zvbb || cpu->cfg.ext_zvkb || cpu->cfg.ext_zvkg ||
         cpu->cfg.ext_zvkned || cpu->cfg.ext_zvknha || cpu->cfg.ext_zvksed ||
         cpu->cfg.ext_zvksh) && !cpu->cfg.ext_zve32x) {
        error_setg(errp,
                   "Vector crypto extensions require V or Zve* extensions");
        return;
    }

    if ((cpu->cfg.ext_zvbc || cpu->cfg.ext_zvknhb) && !cpu->cfg.ext_zve64x) {
        error_setg(
            errp,
            "Zvbc and Zvknhb extensions require V or Zve64x extensions");
        return;
    }

    if (cpu->cfg.ext_zicntr && !cpu->cfg.ext_zicsr) {
        if (cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zicntr))) {
            error_setg(errp, "zicntr requires zicsr");
            return;
        }
        cpu->cfg.ext_zicntr = false;
    }

    if (cpu->cfg.ext_zihpm && !cpu->cfg.ext_zicsr) {
        if (cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zihpm))) {
            error_setg(errp, "zihpm requires zicsr");
            return;
        }
        cpu->cfg.ext_zihpm = false;
    }

    if (cpu->cfg.ext_zicfiss) {
        if (!cpu->cfg.ext_zicsr) {
            error_setg(errp, "zicfiss extension requires zicsr extension");
            return;
        }
        if (!riscv_has_ext(env, RVA)) {
            error_setg(errp, "zicfiss extension requires A extension");
            return;
        }
        if (!riscv_has_ext(env, RVS)) {
            error_setg(errp, "zicfiss extension requires S");
            return;
        }
        if (!cpu->cfg.ext_zimop) {
            error_setg(errp, "zicfiss extension requires zimop extension");
            return;
        }
        if (cpu->cfg.ext_zca && !cpu->cfg.ext_zcmop) {
            error_setg(errp, "zicfiss with zca requires zcmop extension");
            return;
        }
    }

    if (!cpu->cfg.ext_zihpm) {
        cpu->cfg.pmu_mask = 0;
        cpu->pmu_avail_ctrs = 0;
    }

    if (cpu->cfg.ext_zicfilp && !cpu->cfg.ext_zicsr) {
        error_setg(errp, "zicfilp extension requires zicsr extension");
        return;
    }

    if (mcc->def->misa_mxl_max == MXL_RV32 && cpu->cfg.ext_svukte) {
        error_setg(errp, "svukte is not supported for RV32");
        return;
    }

    if ((cpu->cfg.ext_smctr || cpu->cfg.ext_ssctr) &&
        (!riscv_has_ext(env, RVS) || !cpu->cfg.ext_sscsrind)) {
        if (cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_smctr)) ||
            cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_ssctr))) {
            error_setg(errp, "Smctr and Ssctr require S-mode and Sscsrind");
            return;
        }
        cpu->cfg.ext_smctr = false;
        cpu->cfg.ext_ssctr = false;
    }

    if (cpu->cfg.ext_svrsw60t59b &&
        (!cpu->cfg.mmu || mcc->def->misa_mxl_max == MXL_RV32)) {
        error_setg(errp, "svrsw60t59b is not supported on RV32 and MMU-less platforms");
        return;
    }

    /*
     * Disable isa extensions based on priv spec after we
     * validated and set everything we need.
     */
    riscv_cpu_disable_priv_spec_isa_exts(cpu);
}

#ifndef CONFIG_USER_ONLY
static bool riscv_cpu_validate_profile_satp(RISCVCPU *cpu,
                                            RISCVCPUProfile *profile,
                                            bool send_warn)
{
    int satp_max = cpu->cfg.max_satp_mode;

    assert(satp_max >= 0);
    if (profile->satp_mode > satp_max) {
        if (send_warn) {
            bool is_32bit = riscv_cpu_is_32bit(cpu);
            const char *req_satp = satp_mode_str(profile->satp_mode, is_32bit);
            const char *cur_satp = satp_mode_str(satp_max, is_32bit);

            warn_report("Profile %s requires satp mode %s, "
                        "but satp mode %s was set", profile->name,
                        req_satp, cur_satp);
        }

        return false;
    }

    return true;
}
#endif

static void riscv_cpu_check_parent_profile(RISCVCPU *cpu,
                                           RISCVCPUProfile *profile,
                                           RISCVCPUProfile *parent)
{
    if (!profile->present || !parent) {
        return;
    }

    profile->present = parent->present;
}

static void riscv_cpu_validate_profile(RISCVCPU *cpu,
                                       RISCVCPUProfile *profile)
{
    CPURISCVState *env = &cpu->env;
    const char *warn_msg = "Profile %s mandates disabled extension %s";
    bool send_warn = profile->user_set && profile->enabled;
    bool profile_impl = true;
    int i;

#ifndef CONFIG_USER_ONLY
    if (profile->satp_mode != RISCV_PROFILE_ATTR_UNUSED) {
        profile_impl = riscv_cpu_validate_profile_satp(cpu, profile,
                                                       send_warn);
    }
#endif

    if (profile->priv_spec != RISCV_PROFILE_ATTR_UNUSED &&
        profile->priv_spec > env->priv_ver) {
        profile_impl = false;

        if (send_warn) {
            warn_report("Profile %s requires priv spec %s, "
                        "but priv ver %s was set", profile->name,
                        cpu_priv_ver_to_str(profile->priv_spec),
                        cpu_priv_ver_to_str(env->priv_ver));
        }
    }

    for (i = 0; misa_bits[i] != 0; i++) {
        uint32_t bit = misa_bits[i];

        if (!(profile->misa_ext & bit)) {
            continue;
        }

        if (!riscv_has_ext(&cpu->env, bit)) {
            profile_impl = false;

            if (send_warn) {
                warn_report(warn_msg, profile->name,
                            riscv_get_misa_ext_name(bit));
            }
        }
    }

    for (i = 0; profile->ext_offsets[i] != RISCV_PROFILE_EXT_LIST_END; i++) {
        int ext_offset = profile->ext_offsets[i];

        if (!isa_ext_is_enabled(cpu, ext_offset)) {
            profile_impl = false;

            if (send_warn) {
                warn_report(warn_msg, profile->name,
                            cpu_cfg_ext_get_name(ext_offset));
            }
        }
    }

    profile->present = profile_impl;

    riscv_cpu_check_parent_profile(cpu, profile, profile->u_parent);
    riscv_cpu_check_parent_profile(cpu, profile, profile->s_parent);
}

static void riscv_cpu_validate_profiles(RISCVCPU *cpu)
{
    for (int i = 0; riscv_profiles[i] != NULL; i++) {
        riscv_cpu_validate_profile(cpu, riscv_profiles[i]);
    }
}

static void riscv_cpu_init_implied_exts_rules(void)
{
    RISCVCPUImpliedExtsRule *rule;
#ifndef CONFIG_USER_ONLY
    MachineState *ms = MACHINE(qdev_get_machine());
#endif
    static bool initialized;
    int i;

    /* Implied rules only need to be initialized once. */
    if (initialized) {
        return;
    }

    for (i = 0; (rule = riscv_misa_ext_implied_rules[i]); i++) {
#ifndef CONFIG_USER_ONLY
        rule->enabled = bitmap_new(ms->smp.cpus);
#endif
        g_hash_table_insert(misa_ext_implied_rules,
                            GUINT_TO_POINTER(rule->ext), (gpointer)rule);
    }

    for (i = 0; (rule = riscv_multi_ext_implied_rules[i]); i++) {
#ifndef CONFIG_USER_ONLY
        rule->enabled = bitmap_new(ms->smp.cpus);
#endif
        g_hash_table_insert(multi_ext_implied_rules,
                            GUINT_TO_POINTER(rule->ext), (gpointer)rule);
    }

    initialized = true;
}

static void cpu_enable_implied_rule(RISCVCPU *cpu,
                                    RISCVCPUImpliedExtsRule *rule)
{
    CPURISCVState *env = &cpu->env;
    RISCVCPUImpliedExtsRule *ir;
    bool enabled = false;
    int i;

#ifndef CONFIG_USER_ONLY
    enabled = test_bit(cpu->env.mhartid, rule->enabled);
#endif

    if (!enabled) {
        /* Enable the implied MISAs. */
        if (rule->implied_misa_exts) {
            for (i = 0; misa_bits[i] != 0; i++) {
                if (rule->implied_misa_exts & misa_bits[i]) {
                    /*
                     * If the user disabled the misa_bit do not re-enable it
                     * and do not apply any implied rules related to it.
                     */
                    if (cpu_misa_ext_is_user_set(misa_bits[i]) &&
                        !(env->misa_ext & misa_bits[i])) {
                        continue;
                    }

                    riscv_cpu_set_misa_ext(env, env->misa_ext | misa_bits[i]);
                    ir = g_hash_table_lookup(misa_ext_implied_rules,
                                             GUINT_TO_POINTER(misa_bits[i]));

                    if (ir) {
                        cpu_enable_implied_rule(cpu, ir);
                    }
                }
            }
        }

        /* Enable the implied extensions. */
        for (i = 0;
             rule->implied_multi_exts[i] != RISCV_IMPLIED_EXTS_RULE_END; i++) {
            cpu_cfg_ext_auto_update(cpu, rule->implied_multi_exts[i], true);

            ir = g_hash_table_lookup(multi_ext_implied_rules,
                                     GUINT_TO_POINTER(
                                         rule->implied_multi_exts[i]));

            if (ir) {
                cpu_enable_implied_rule(cpu, ir);
            }
        }

#ifndef CONFIG_USER_ONLY
        bitmap_set(rule->enabled, cpu->env.mhartid, 1);
#endif
    }
}

/* Zc extension has special implied rules that need to be handled separately. */
static void cpu_enable_zc_implied_rules(RISCVCPU *cpu)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPURISCVState *env = &cpu->env;

    if (cpu->cfg.ext_zce) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zca), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcb), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcmp), true);
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcmt), true);

        if (riscv_has_ext(env, RVF) && mcc->def->misa_mxl_max == MXL_RV32) {
            cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcf), true);
        }
    }

    /* Zca, Zcd and Zcf has a PRIV 1.12.0 restriction */
    if (riscv_has_ext(env, RVC) && env->priv_ver >= PRIV_VERSION_1_12_0) {
        cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zca), true);

        if (riscv_has_ext(env, RVF) && mcc->def->misa_mxl_max == MXL_RV32) {
            cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcf), true);
        }

        if (riscv_has_ext(env, RVD)) {
            cpu_cfg_ext_auto_update(cpu, CPU_CFG_OFFSET(ext_zcd), true);
        }
    }
}

static void riscv_cpu_enable_implied_rules(RISCVCPU *cpu)
{
    RISCVCPUImpliedExtsRule *rule;
    int i;

    /* Enable the implied extensions for Zc. */
    cpu_enable_zc_implied_rules(cpu);

    /* Enable the implied MISAs. */
    for (i = 0; (rule = riscv_misa_ext_implied_rules[i]); i++) {
        if (riscv_has_ext(&cpu->env, rule->ext)) {
            cpu_enable_implied_rule(cpu, rule);
        }
    }

    /* Enable the implied extensions. */
    for (i = 0; (rule = riscv_multi_ext_implied_rules[i]); i++) {
        if (isa_ext_is_enabled(cpu, rule->ext)) {
            cpu_enable_implied_rule(cpu, rule);
        }
    }
}

void riscv_tcg_cpu_finalize_features(RISCVCPU *cpu, Error **errp)
{
    CPURISCVState *env = &cpu->env;
    Error *local_err = NULL;

    riscv_cpu_init_implied_exts_rules();
    riscv_cpu_enable_implied_rules(cpu);

    riscv_cpu_validate_misa_priv(env, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    riscv_cpu_update_named_features(cpu);
    riscv_cpu_validate_profiles(cpu);

    if (cpu->cfg.ext_smepmp && !cpu->cfg.pmp) {
        /*
         * Enhanced PMP should only be available
         * on harts with PMP support
         */
        error_setg(errp, "Invalid configuration: Smepmp requires PMP support");
        return;
    }

    riscv_cpu_validate_set_extensions(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
#ifndef CONFIG_USER_ONLY
    if (cpu->cfg.pmu_mask) {
        riscv_pmu_init(cpu, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            return;
        }

        if (cpu->cfg.ext_sscofpmf) {
            cpu->pmu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          riscv_pmu_timer_cb, cpu);
        }
    }
#endif
}

void riscv_tcg_cpu_finalize_dynamic_decoder(RISCVCPU *cpu)
{
    GPtrArray *dynamic_decoders;
    dynamic_decoders = g_ptr_array_sized_new(decoder_table_size);
    for (size_t i = 0; i < decoder_table_size; ++i) {
        if (decoder_table[i].guard_func &&
            decoder_table[i].guard_func(&cpu->cfg)) {
            g_ptr_array_add(dynamic_decoders,
                            (gpointer)decoder_table[i].riscv_cpu_decode_fn);
        }
    }

    cpu->decoders = dynamic_decoders;
}

bool riscv_cpu_tcg_compatible(RISCVCPU *cpu)
{
    return object_dynamic_cast(OBJECT(cpu), TYPE_RISCV_CPU_HOST) == NULL;
}

static bool riscv_cpu_is_generic(Object *cpu_obj)
{
    return object_dynamic_cast(cpu_obj, TYPE_RISCV_DYNAMIC_CPU) != NULL;
}

static void riscv_cpu_set_profile(RISCVCPU *cpu,
                                  RISCVCPUProfile *profile,
                                  bool enabled)
{
    int i, ext_offset;

    if (profile->u_parent != NULL) {
        riscv_cpu_set_profile(cpu, profile->u_parent, enabled);
    }

    if (profile->s_parent != NULL) {
        riscv_cpu_set_profile(cpu, profile->s_parent, enabled);
    }

    profile->enabled = enabled;

    if (profile->enabled) {
        cpu->env.priv_ver = profile->priv_spec;

#ifndef CONFIG_USER_ONLY
        if (profile->satp_mode != RISCV_PROFILE_ATTR_UNUSED) {
            object_property_set_bool(OBJECT(cpu), "mmu", true, NULL);
            const char *satp_prop = satp_mode_str(profile->satp_mode,
                                                  riscv_cpu_is_32bit(cpu));
            object_property_set_bool(OBJECT(cpu), satp_prop, true, NULL);
        }
#endif
    }

    for (i = 0; misa_bits[i] != 0; i++) {
        uint32_t bit = misa_bits[i];

        if  (!(profile->misa_ext & bit)) {
            continue;
        }

        if (bit == RVI && !profile->enabled) {
            /*
             * Disabling profiles will not disable the base
             * ISA RV64I.
             */
            continue;
        }

        cpu_misa_ext_add_user_opt(bit, profile->enabled);
        riscv_cpu_write_misa_bit(cpu, bit, profile->enabled);
    }

    for (i = 0; profile->ext_offsets[i] != RISCV_PROFILE_EXT_LIST_END; i++) {
        ext_offset = profile->ext_offsets[i];

        if (profile->enabled) {
            if (cpu_cfg_offset_is_named_feat(ext_offset)) {
                riscv_cpu_enable_named_feat(cpu, ext_offset);
            }

            cpu_bump_multi_ext_priv_ver(&cpu->env, ext_offset);
        }

        cpu_cfg_ext_add_user_opt(ext_offset, profile->enabled);
        isa_ext_update_enabled(cpu, ext_offset, profile->enabled);
    }
}

/*
 * We'll get here via the following path:
 *
 * riscv_cpu_realize()
 *   -> cpu_exec_realizefn()
 *      -> tcg_cpu_realize() (via accel_cpu_common_realize())
 */
static bool riscv_tcg_cpu_realize(CPUState *cs, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (!riscv_cpu_tcg_compatible(cpu)) {
        g_autofree char *name = riscv_cpu_get_name(cpu);
        error_setg(errp, "'%s' CPU is not compatible with TCG acceleration",
                   name);
        return false;
    }

#ifndef CONFIG_USER_ONLY
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);

    if (mcc->def->misa_mxl_max >= MXL_RV128 && qemu_tcg_mttcg_enabled()) {
        /* Missing 128-bit aligned atomics */
        error_setg(errp,
                   "128-bit RISC-V currently does not work with Multi "
                   "Threaded TCG. Please use: -accel tcg,thread=single");
        return false;
    }

    CPURISCVState *env = &cpu->env;

    tcg_cflags_set(CPU(cs), CF_PCREL);

    if (cpu->cfg.ext_sstc) {
        riscv_timer_init(cpu);
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
    bool vendor_cpu = riscv_cpu_is_vendor(obj);
    bool prev_val, value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    cpu_misa_ext_add_user_opt(misa_bit, value);

    prev_val = env->misa_ext & misa_bit;

    if (value == prev_val) {
        return;
    }

    if (value) {
        if (vendor_cpu) {
            g_autofree char *cpuname = riscv_cpu_get_name(cpu);
            error_setg(errp, "'%s' CPU does not allow enabling extensions",
                       cpuname);
            return;
        }

        if (misa_bit == RVH && env->priv_ver < PRIV_VERSION_1_12_0) {
            /*
             * Note: the 'priv_spec' command line option, if present,
             * will take precedence over this priv_ver bump.
             */
            env->priv_ver = PRIV_VERSION_1_12_0;
        }
    }

    riscv_cpu_write_misa_bit(cpu, misa_bit, value);
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
    MISA_CFG(RVV, false),
    MISA_CFG(RVG, false),
    MISA_CFG(RVB, false),
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
            riscv_cpu_write_misa_bit(RISCV_CPU(cpu_obj), bit,
                                     misa_cfg->enabled);
        }
    }
}

static void cpu_set_profile(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    RISCVCPUProfile *profile = opaque;
    RISCVCPU *cpu = RISCV_CPU(obj);
    bool value;

    if (riscv_cpu_is_vendor(obj)) {
        error_setg(errp, "Profile %s is not available for vendor CPUs",
                   profile->name);
        return;
    }

    if (cpu->env.misa_mxl != MXL_RV64) {
        error_setg(errp, "Profile %s only available for 64 bit CPUs",
                   profile->name);
        return;
    }

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    profile->user_set = true;

    riscv_cpu_set_profile(cpu, profile, value);
}

static void cpu_get_profile(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    RISCVCPUProfile *profile = opaque;
    bool value = profile->enabled;

    visit_type_bool(v, name, &value, errp);
}

static void riscv_cpu_add_profiles(Object *cpu_obj)
{
    for (int i = 0; riscv_profiles[i] != NULL; i++) {
        RISCVCPUProfile *profile = riscv_profiles[i];

        object_property_add(cpu_obj, profile->name, "bool",
                            cpu_get_profile, cpu_set_profile,
                            NULL, (void *)profile);

        /*
         * CPUs might enable a profile right from the start.
         * Enable its mandatory extensions right away in this
         * case.
         */
        if (profile->enabled) {
            riscv_cpu_set_profile(RISCV_CPU(cpu_obj), profile, true);
        }
    }
}

static void cpu_set_multi_ext_cfg(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    const RISCVCPUMultiExtConfig *multi_ext_cfg = opaque;
    RISCVCPU *cpu = RISCV_CPU(obj);
    bool vendor_cpu = riscv_cpu_is_vendor(obj);
    bool prev_val, value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    cpu_cfg_ext_add_user_opt(multi_ext_cfg->offset, value);

    prev_val = isa_ext_is_enabled(cpu, multi_ext_cfg->offset);

    if (value == prev_val) {
        return;
    }

    if (value && vendor_cpu) {
        g_autofree char *cpuname = riscv_cpu_get_name(cpu);
        error_setg(errp, "'%s' CPU does not allow enabling extensions",
                   cpuname);
        return;
    }

    if (value) {
        cpu_bump_multi_ext_priv_ver(&cpu->env, multi_ext_cfg->offset);
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

    object_property_add(cpu_obj, multi_cfg->name, "bool",
                        cpu_get_multi_ext_cfg,
                        cpu_set_multi_ext_cfg,
                        NULL, (void *)multi_cfg);

    if (!generic_cpu) {
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

    riscv_cpu_add_profiles(obj);
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

    /* Enable RVG and RVV that are disabled by default */
    riscv_cpu_set_misa_ext(env, env->misa_ext | RVB | RVG | RVV);

    for (prop = riscv_cpu_extensions; prop && prop->name; prop++) {
        isa_ext_update_enabled(cpu, prop->offset, true);
    }

    /*
     * Some extensions can't be added without backward compatibilty concerns.
     * Disable those, the user can still opt in to them on the command line.
     */
    cpu->cfg.ext_svade = false;

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
    } else {
        isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_svrsw60t59b), false);
    }

    /*
     * TODO: ext_smrnmi requires OpenSBI changes that our current
     * image does not have. Disable it for now.
     */
    if (cpu->cfg.ext_smrnmi) {
        isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_smrnmi), false);
    }

    /*
     * TODO: ext_smdbltrp requires the firmware to clear MSTATUS.MDT on startup
     * to avoid generating a double trap. OpenSBI does not currently support it,
     * disable it for now.
     */
    if (cpu->cfg.ext_smdbltrp) {
        isa_ext_update_enabled(cpu, CPU_CFG_OFFSET(ext_smdbltrp), false);
    }
}

static bool riscv_cpu_has_max_extensions(Object *cpu_obj)
{
    return object_dynamic_cast(cpu_obj, TYPE_RISCV_CPU_MAX) != NULL;
}

static void riscv_tcg_cpu_instance_init(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    Object *obj = OBJECT(cpu);

    misa_ext_user_opts = g_hash_table_new(NULL, g_direct_equal);
    multi_ext_user_opts = g_hash_table_new(NULL, g_direct_equal);

    if (!misa_ext_implied_rules) {
        misa_ext_implied_rules = g_hash_table_new(NULL, g_direct_equal);
    }

    if (!multi_ext_implied_rules) {
        multi_ext_implied_rules = g_hash_table_new(NULL, g_direct_equal);
    }

    riscv_cpu_add_user_properties(obj);

    if (riscv_cpu_has_max_extensions(obj)) {
        riscv_init_max_cpu_extensions(obj);
    }
}

static void riscv_tcg_cpu_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_instance_init = riscv_tcg_cpu_instance_init;
    acc->cpu_target_realize = riscv_tcg_cpu_realize;
}

static const TypeInfo riscv_tcg_cpu_accel_type_info = {
    .name = ACCEL_CPU_NAME("tcg"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = riscv_tcg_cpu_accel_class_init,
    .abstract = true,
};

static void riscv_tcg_cpu_accel_register_types(void)
{
    type_register_static(&riscv_tcg_cpu_accel_type_info);
}
type_init(riscv_tcg_cpu_accel_register_types);
