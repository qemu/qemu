/*
 * ARM gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "gdbstub/helpers.h"
#include "gdbstub/commands.h"
#include "system/tcg.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"

typedef struct RegisterSysregFeatureParam {
    CPUState *cs;
    GDBFeatureBuilder builder;
    int n;
} RegisterSysregFeatureParam;

/* Old gdb always expect FPA registers.  Newer (xml-aware) gdb only expect
   whatever the target description contains.  Due to a historical mishap
   the FPA registers appear in between core integer regs and the CPSR.
   We hack round this by giving the FPA regs zero size when talking to a
   newer gdb.  */

int arm_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (n < 16) {
        /* Core integer register.  */
        return gdb_get_reg32(mem_buf, env->regs[n]);
    }
    if (n == 25) {
        /* CPSR, or XPSR for M-profile */
        if (arm_feature(env, ARM_FEATURE_M)) {
            return gdb_get_reg32(mem_buf, xpsr_read(env));
        } else {
            return gdb_get_reg32(mem_buf, cpsr_read(env));
        }
    }
    /* Unknown register.  */
    return 0;
}

int arm_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    /*
     * Mask out low bits of PC to workaround gdb bugs.
     * This avoids an assert in thumb_tr_translate_insn, because it is
     * architecturally impossible to misalign the pc.
     * This will probably cause problems if we ever implement the
     * Jazelle DBX extensions.
     */
    if (n == 15) {
        tmp &= ~1;
    }

    if (n < 16) {
        /* Core integer register.  */
        if (n == 13 && arm_feature(env, ARM_FEATURE_M)) {
            /* M profile SP low bits are always 0 */
            tmp &= ~3;
        }
        env->regs[n] = tmp;
        return 4;
    }
    if (n == 25) {
        /* CPSR, or XPSR for M-profile */
        if (arm_feature(env, ARM_FEATURE_M)) {
            /*
             * Don't allow writing to XPSR.Exception as it can cause
             * a transition into or out of handler mode (it's not
             * writable via the MSR insn so this is a reasonable
             * restriction). Other fields are safe to update.
             */
            xpsr_write(env, tmp, ~XPSR_EXCP);
        } else {
            cpsr_write(env, tmp, 0xffffffff, CPSRWriteByGDBStub);
        }
        return 4;
    }
    /* Unknown register.  */
    return 0;
}

static int vfp_gdb_get_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    int nregs = cpu_isar_feature(aa32_simd_r32, cpu) ? 32 : 16;

    /* VFP data registers are always little-endian.  */
    if (reg < nregs) {
        return gdb_get_reg64(buf, *aa32_vfp_dreg(env, reg));
    }
    if (arm_feature(env, ARM_FEATURE_NEON)) {
        /* Aliases for Q regs.  */
        nregs += 16;
        if (reg < nregs) {
            uint64_t *q = aa32_vfp_qreg(env, reg - 32);
            return gdb_get_reg128(buf, q[0], q[1]);
        }
    }
    switch (reg - nregs) {
    case 0:
        return gdb_get_reg32(buf, vfp_get_fpscr(env));
    }
    return 0;
}

static int vfp_gdb_set_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    int nregs = cpu_isar_feature(aa32_simd_r32, cpu) ? 32 : 16;

    if (reg < nregs) {
        *aa32_vfp_dreg(env, reg) = ldq_le_p(buf);
        return 8;
    }
    if (arm_feature(env, ARM_FEATURE_NEON)) {
        nregs += 16;
        if (reg < nregs) {
            uint64_t *q = aa32_vfp_qreg(env, reg - 32);
            q[0] = ldq_le_p(buf);
            q[1] = ldq_le_p(buf + 8);
            return 16;
        }
    }
    switch (reg - nregs) {
    case 0:
        vfp_set_fpscr(env, ldl_p(buf));
        return 4;
    }
    return 0;
}

static int vfp_gdb_get_sysreg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
        return gdb_get_reg32(buf, env->vfp.xregs[ARM_VFP_FPSID]);
    case 1:
        return gdb_get_reg32(buf, env->vfp.xregs[ARM_VFP_FPEXC]);
    }
    return 0;
}

static int vfp_gdb_set_sysreg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
        env->vfp.xregs[ARM_VFP_FPSID] = ldl_p(buf);
        return 4;
    case 1:
        env->vfp.xregs[ARM_VFP_FPEXC] = ldl_p(buf) & (1 << 30);
        return 4;
    }
    return 0;
}

static int mve_gdb_get_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
        return gdb_get_reg32(buf, env->v7m.vpr);
    default:
        return 0;
    }
}

static int mve_gdb_set_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0:
        env->v7m.vpr = ldl_p(buf);
        return 4;
    default:
        return 0;
    }
}

/**
 * arm_get/set_gdb_*: get/set a gdb register
 * @env: the CPU state
 * @buf: a buffer to copy to/from
 * @reg: register number (offset from start of group)
 *
 * We return the number of bytes copied
 */

static int arm_gdb_get_sysreg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    const ARMCPRegInfo *ri;
    uint32_t key;

    key = cpu->dyn_sysreg_feature.data.cpregs.keys[reg];
    ri = get_arm_cp_reginfo(cpu->cp_regs, key);
    if (ri) {
        if (cpreg_field_is_64bit(ri)) {
            return gdb_get_reg64(buf, (uint64_t)read_raw_cp_reg(env, ri));
        } else {
            return gdb_get_reg32(buf, (uint32_t)read_raw_cp_reg(env, ri));
        }
    }
    return 0;
}

static int arm_gdb_set_sysreg(CPUState *cs, uint8_t *buf, int reg)
{
    return 0;
}

static void arm_gen_one_feature_sysreg(GDBFeatureBuilder *builder,
                                       DynamicGDBFeatureInfo *dyn_feature,
                                       ARMCPRegInfo *ri, uint32_t ri_key,
                                       int bitsize, int n)
{
    gdb_feature_builder_append_reg(builder, ri->name, bitsize, n,
                                   "int", "cp_regs");

    dyn_feature->data.cpregs.keys[n] = ri_key;
}

static void arm_register_sysreg_for_feature(gpointer key, gpointer value,
                                            gpointer p)
{
    uint32_t ri_key = (uintptr_t)key;
    ARMCPRegInfo *ri = value;
    RegisterSysregFeatureParam *param = p;
    ARMCPU *cpu = ARM_CPU(param->cs);
    CPUARMState *env = &cpu->env;
    DynamicGDBFeatureInfo *dyn_feature = &cpu->dyn_sysreg_feature;

    if (!(ri->type & (ARM_CP_NO_RAW | ARM_CP_NO_GDB))) {
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            if (ri->state == ARM_CP_STATE_AA64) {
                arm_gen_one_feature_sysreg(&param->builder, dyn_feature,
                                           ri, ri_key, 64, param->n++);
            }
        } else {
            if (ri->state == ARM_CP_STATE_AA32) {
                if (!arm_feature(env, ARM_FEATURE_EL3) &&
                    (ri->secure & ARM_CP_SECSTATE_S)) {
                    return;
                }
                if (ri->type & ARM_CP_64BIT) {
                    arm_gen_one_feature_sysreg(&param->builder, dyn_feature,
                                               ri, ri_key, 64, param->n++);
                } else {
                    arm_gen_one_feature_sysreg(&param->builder, dyn_feature,
                                               ri, ri_key, 32, param->n++);
                }
            }
        }
    }
}

static GDBFeature *arm_gen_dynamic_sysreg_feature(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    RegisterSysregFeatureParam param = {cs};
    gsize num_regs = g_hash_table_size(cpu->cp_regs);

    gdb_feature_builder_init(&param.builder,
                             &cpu->dyn_sysreg_feature.desc,
                             "org.qemu.gdb.arm.sys.regs",
                             "system-registers.xml",
                             base_reg);
    cpu->dyn_sysreg_feature.data.cpregs.keys = g_new(uint32_t, num_regs);
    g_hash_table_foreach(cpu->cp_regs, arm_register_sysreg_for_feature, &param);
    gdb_feature_builder_end(&param.builder);
    return &cpu->dyn_sysreg_feature.desc;
}

#ifdef CONFIG_TCG
typedef enum {
    M_SYSREG_MSP,
    M_SYSREG_PSP,
    M_SYSREG_PRIMASK,
    M_SYSREG_CONTROL,
    M_SYSREG_BASEPRI,
    M_SYSREG_FAULTMASK,
    M_SYSREG_MSPLIM,
    M_SYSREG_PSPLIM,
} MProfileSysreg;

static const struct {
    const char *name;
    int feature;
} m_sysreg_def[] = {
    [M_SYSREG_MSP] = { "msp", ARM_FEATURE_M },
    [M_SYSREG_PSP] = { "psp", ARM_FEATURE_M },
    [M_SYSREG_PRIMASK] = { "primask", ARM_FEATURE_M },
    [M_SYSREG_CONTROL] = { "control", ARM_FEATURE_M },
    [M_SYSREG_BASEPRI] = { "basepri", ARM_FEATURE_M_MAIN },
    [M_SYSREG_FAULTMASK] = { "faultmask", ARM_FEATURE_M_MAIN },
    [M_SYSREG_MSPLIM] = { "msplim", ARM_FEATURE_V8 },
    [M_SYSREG_PSPLIM] = { "psplim", ARM_FEATURE_V8 },
};

static uint32_t *m_sysreg_ptr(CPUARMState *env, MProfileSysreg reg, bool sec)
{
    uint32_t *ptr;

    switch (reg) {
    case M_SYSREG_MSP:
        ptr = arm_v7m_get_sp_ptr(env, sec, false, true);
        break;
    case M_SYSREG_PSP:
        ptr = arm_v7m_get_sp_ptr(env, sec, true, true);
        break;
    case M_SYSREG_MSPLIM:
        ptr = &env->v7m.msplim[sec];
        break;
    case M_SYSREG_PSPLIM:
        ptr = &env->v7m.psplim[sec];
        break;
    case M_SYSREG_PRIMASK:
        ptr = &env->v7m.primask[sec];
        break;
    case M_SYSREG_BASEPRI:
        ptr = &env->v7m.basepri[sec];
        break;
    case M_SYSREG_FAULTMASK:
        ptr = &env->v7m.faultmask[sec];
        break;
    case M_SYSREG_CONTROL:
        ptr = &env->v7m.control[sec];
        break;
    default:
        return NULL;
    }
    return arm_feature(env, m_sysreg_def[reg].feature) ? ptr : NULL;
}

static int m_sysreg_get(CPUARMState *env, GByteArray *buf,
                        MProfileSysreg reg, bool secure)
{
    uint32_t *ptr = m_sysreg_ptr(env, reg, secure);

    if (ptr == NULL) {
        return 0;
    }
    return gdb_get_reg32(buf, *ptr);
}

static int arm_gdb_get_m_systemreg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    /*
     * Here, we emulate MRS instruction, where CONTROL has a mix of
     * banked and non-banked bits.
     */
    if (reg == M_SYSREG_CONTROL) {
        return gdb_get_reg32(buf, arm_v7m_mrs_control(env, env->v7m.secure));
    }
    return m_sysreg_get(env, buf, reg, env->v7m.secure);
}

static int arm_gdb_set_m_systemreg(CPUState *cs, uint8_t *buf, int reg)
{
    return 0; /* TODO */
}

static GDBFeature *arm_gen_dynamic_m_systemreg_feature(CPUState *cs,
                                                       int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    GDBFeatureBuilder builder;
    int reg = 0;
    int i;

    gdb_feature_builder_init(&builder, &cpu->dyn_m_systemreg_feature.desc,
                             "org.gnu.gdb.arm.m-system", "arm-m-system.xml",
                             base_reg);

    for (i = 0; i < ARRAY_SIZE(m_sysreg_def); i++) {
        if (arm_feature(env, m_sysreg_def[i].feature)) {
            gdb_feature_builder_append_reg(&builder, m_sysreg_def[i].name, 32,
                                           reg++, "int", NULL);
        }
    }

    gdb_feature_builder_end(&builder);

    return &cpu->dyn_m_systemreg_feature.desc;
}

#ifndef CONFIG_USER_ONLY
/*
 * For user-only, we see the non-secure registers via m_systemreg above.
 * For secext, encode the non-secure view as even and secure view as odd.
 */
static int arm_gdb_get_m_secextreg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    return m_sysreg_get(env, buf, reg >> 1, reg & 1);
}

static int arm_gdb_set_m_secextreg(CPUState *cs, uint8_t *buf, int reg)
{
    return 0; /* TODO */
}

static GDBFeature *arm_gen_dynamic_m_secextreg_feature(CPUState *cs,
                                                       int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GDBFeatureBuilder builder;
    char *name;
    int reg = 0;
    int i;

    gdb_feature_builder_init(&builder, &cpu->dyn_m_secextreg_feature.desc,
                             "org.gnu.gdb.arm.secext", "arm-m-secext.xml",
                             base_reg);

    for (i = 0; i < ARRAY_SIZE(m_sysreg_def); i++) {
        name = g_strconcat(m_sysreg_def[i].name, "_ns", NULL);
        gdb_feature_builder_append_reg(&builder, name, 32, reg++,
                                       "int", NULL);
        name = g_strconcat(m_sysreg_def[i].name, "_s", NULL);
        gdb_feature_builder_append_reg(&builder, name, 32, reg++,
                                       "int", NULL);
    }

    gdb_feature_builder_end(&builder);

    return &cpu->dyn_m_secextreg_feature.desc;
}
#endif
#endif /* CONFIG_TCG */

void arm_cpu_register_gdb_commands(ARMCPU *cpu)
{
    g_autoptr(GPtrArray) query_table = g_ptr_array_new();
    g_autoptr(GPtrArray) set_table = g_ptr_array_new();
    g_autoptr(GString) qsupported_features = g_string_new(NULL);

    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
    #ifdef TARGET_AARCH64
        aarch64_cpu_register_gdb_commands(cpu, qsupported_features, query_table,
                                          set_table);
    #endif
    }

    /* Set arch-specific handlers for 'q' commands. */
    if (query_table->len) {
        gdb_extend_query_table(query_table);
    }

    /* Set arch-specific handlers for 'Q' commands. */
    if (set_table->len) {
        gdb_extend_set_table(set_table);
    }

    /* Set arch-specific qSupported feature. */
    if (qsupported_features->len) {
        gdb_extend_qsupported_features(qsupported_features->str);
    }
}

void arm_cpu_register_gdb_regs_for_features(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;

    if (arm_feature(env, ARM_FEATURE_AARCH64)) {
        /*
         * The lower part of each SVE register aliases to the FPU
         * registers so we don't need to include both.
         */
#ifdef TARGET_AARCH64
        if (isar_feature_aa64_sve(&cpu->isar)) {
            GDBFeature *feature = arm_gen_dynamic_svereg_feature(cs, cs->gdb_num_regs);
            gdb_register_coprocessor(cs, aarch64_gdb_get_sve_reg,
                                     aarch64_gdb_set_sve_reg, feature, 0);
        } else {
            gdb_register_coprocessor(cs, aarch64_gdb_get_fpu_reg,
                                     aarch64_gdb_set_fpu_reg,
                                     gdb_find_static_feature("aarch64-fpu.xml"),
                                     0);
        }
        /*
         * Note that we report pauth information via the feature name
         * org.gnu.gdb.aarch64.pauth_v2, not org.gnu.gdb.aarch64.pauth.
         * GDB versions 9 through 12 have a bug where they will crash
         * if they see the latter XML from QEMU.
         */
        if (isar_feature_aa64_pauth(&cpu->isar)) {
            gdb_register_coprocessor(cs, aarch64_gdb_get_pauth_reg,
                                     aarch64_gdb_set_pauth_reg,
                                     gdb_find_static_feature("aarch64-pauth.xml"),
                                     0);
        }

#ifdef CONFIG_USER_ONLY
        /* Memory Tagging Extension (MTE) 'tag_ctl' pseudo-register. */
        if (cpu_isar_feature(aa64_mte, cpu)) {
            gdb_register_coprocessor(cs, aarch64_gdb_get_tag_ctl_reg,
                                     aarch64_gdb_set_tag_ctl_reg,
                                     gdb_find_static_feature("aarch64-mte.xml"),
                                     0);
        }
#endif
#endif
    } else {
        if (arm_feature(env, ARM_FEATURE_NEON)) {
            gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                     gdb_find_static_feature("arm-neon.xml"),
                                     0);
        } else if (cpu_isar_feature(aa32_simd_r32, cpu)) {
            gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                     gdb_find_static_feature("arm-vfp3.xml"),
                                     0);
        } else if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                     gdb_find_static_feature("arm-vfp.xml"), 0);
        }
        if (!arm_feature(env, ARM_FEATURE_M)) {
            /*
             * A and R profile have FP sysregs FPEXC and FPSID that we
             * expose to gdb.
             */
            gdb_register_coprocessor(cs, vfp_gdb_get_sysreg, vfp_gdb_set_sysreg,
                                     gdb_find_static_feature("arm-vfp-sysregs.xml"),
                                     0);
        }
    }
    if (cpu_isar_feature(aa32_mve, cpu) && tcg_enabled()) {
        gdb_register_coprocessor(cs, mve_gdb_get_reg, mve_gdb_set_reg,
                                 gdb_find_static_feature("arm-m-profile-mve.xml"),
                                 0);
    }
    gdb_register_coprocessor(cs, arm_gdb_get_sysreg, arm_gdb_set_sysreg,
                             arm_gen_dynamic_sysreg_feature(cs, cs->gdb_num_regs),
                             0);

#ifdef CONFIG_TCG
    if (arm_feature(env, ARM_FEATURE_M) && tcg_enabled()) {
        gdb_register_coprocessor(cs,
            arm_gdb_get_m_systemreg, arm_gdb_set_m_systemreg,
            arm_gen_dynamic_m_systemreg_feature(cs, cs->gdb_num_regs), 0);
#ifndef CONFIG_USER_ONLY
        if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            gdb_register_coprocessor(cs,
                arm_gdb_get_m_secextreg, arm_gdb_set_m_secextreg,
                arm_gen_dynamic_m_secextreg_feature(cs, cs->gdb_num_regs), 0);
        }
#endif
    }
#endif /* CONFIG_TCG */
}
