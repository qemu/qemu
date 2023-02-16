#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "internals.h"
#include "migration/cpu.h"

static bool vfp_needed(void *opaque)
{
    ARMCPU *cpu = opaque;

    return (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)
            ? cpu_isar_feature(aa64_fp_simd, cpu)
            : cpu_isar_feature(aa32_vfp_simd, cpu));
}

static int get_fpscr(QEMUFile *f, void *opaque, size_t size,
                     const VMStateField *field)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    uint32_t val = qemu_get_be32(f);

    vfp_set_fpscr(env, val);
    return 0;
}

static int put_fpscr(QEMUFile *f, void *opaque, size_t size,
                     const VMStateField *field, JSONWriter *vmdesc)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    qemu_put_be32(f, vfp_get_fpscr(env));
    return 0;
}

static const VMStateInfo vmstate_fpscr = {
    .name = "fpscr",
    .get = get_fpscr,
    .put = put_fpscr,
};

static const VMStateDescription vmstate_vfp = {
    .name = "cpu/vfp",
    .version_id = 3,
    .minimum_version_id = 3,
    .needed = vfp_needed,
    .fields = (VMStateField[]) {
        /* For compatibility, store Qn out of Zn here.  */
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[0].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[1].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[2].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[3].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[4].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[5].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[6].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[7].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[8].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[9].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[10].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[11].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[12].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[13].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[14].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[15].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[16].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[17].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[18].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[19].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[20].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[21].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[22].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[23].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[24].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[25].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[26].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[27].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[28].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[29].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[30].d, ARMCPU, 0, 2),
        VMSTATE_UINT64_SUB_ARRAY(env.vfp.zregs[31].d, ARMCPU, 0, 2),

        /* The xregs array is a little awkward because element 1 (FPSCR)
         * requires a specific accessor, so we have to split it up in
         * the vmstate:
         */
        VMSTATE_UINT32(env.vfp.xregs[0], ARMCPU),
        VMSTATE_UINT32_SUB_ARRAY(env.vfp.xregs, ARMCPU, 2, 14),
        {
            .name = "fpscr",
            .version_id = 0,
            .size = sizeof(uint32_t),
            .info = &vmstate_fpscr,
            .flags = VMS_SINGLE,
            .offset = 0,
        },
        VMSTATE_END_OF_LIST()
    }
};

static bool iwmmxt_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_IWMMXT);
}

static const VMStateDescription vmstate_iwmmxt = {
    .name = "cpu/iwmmxt",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = iwmmxt_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.iwmmxt.regs, ARMCPU, 16),
        VMSTATE_UINT32_ARRAY(env.iwmmxt.cregs, ARMCPU, 16),
        VMSTATE_END_OF_LIST()
    }
};

#ifdef TARGET_AARCH64
/* The expression ARM_MAX_VQ - 2 is 0 for pure AArch32 build,
 * and ARMPredicateReg is actively empty.  This triggers errors
 * in the expansion of the VMSTATE macros.
 */

static bool sve_needed(void *opaque)
{
    ARMCPU *cpu = opaque;

    return cpu_isar_feature(aa64_sve, cpu);
}

/* The first two words of each Zreg is stored in VFP state.  */
static const VMStateDescription vmstate_zreg_hi_reg = {
    .name = "cpu/sve/zreg_hi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_SUB_ARRAY(d, ARMVectorReg, 2, ARM_MAX_VQ - 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_preg_reg = {
    .name = "cpu/sve/preg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(p, ARMPredicateReg, 2 * ARM_MAX_VQ / 8),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_sve = {
    .name = "cpu/sve",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = sve_needed,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(env.vfp.zregs, ARMCPU, 32, 0,
                             vmstate_zreg_hi_reg, ARMVectorReg),
        VMSTATE_STRUCT_ARRAY(env.vfp.pregs, ARMCPU, 17, 0,
                             vmstate_preg_reg, ARMPredicateReg),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vreg = {
    .name = "vreg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(d, ARMVectorReg, ARM_MAX_VQ * 2),
        VMSTATE_END_OF_LIST()
    }
};

static bool za_needed(void *opaque)
{
    ARMCPU *cpu = opaque;

    /*
     * When ZA storage is disabled, its contents are discarded.
     * It will be zeroed when ZA storage is re-enabled.
     */
    return FIELD_EX64(cpu->env.svcr, SVCR, ZA);
}

static const VMStateDescription vmstate_za = {
    .name = "cpu/sme",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = za_needed,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(env.zarray, ARMCPU, ARM_MAX_VQ * 16, 0,
                             vmstate_vreg, ARMVectorReg),
        VMSTATE_END_OF_LIST()
    }
};
#endif /* AARCH64 */

static bool serror_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return env->serror.pending != 0;
}

static const VMStateDescription vmstate_serror = {
    .name = "cpu/serror",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = serror_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(env.serror.pending, ARMCPU),
        VMSTATE_UINT8(env.serror.has_esr, ARMCPU),
        VMSTATE_UINT64(env.serror.esr, ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool irq_line_state_needed(void *opaque)
{
    return true;
}

static const VMStateDescription vmstate_irq_line_state = {
    .name = "cpu/irq-line-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = irq_line_state_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.irq_line_state, ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool m_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_M);
}

static const VMStateDescription vmstate_m_faultmask_primask = {
    .name = "cpu/m/faultmask-primask",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = m_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.v7m.faultmask[M_REG_NS], ARMCPU),
        VMSTATE_UINT32(env.v7m.primask[M_REG_NS], ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

/* CSSELR is in a subsection because we didn't implement it previously.
 * Migration from an old implementation will leave it at zero, which
 * is OK since the only CPUs in the old implementation make the
 * register RAZ/WI.
 * Since there was no version of QEMU which implemented the CSSELR for
 * just non-secure, we transfer both banks here rather than putting
 * the secure banked version in the m-security subsection.
 */
static bool csselr_vmstate_validate(void *opaque, int version_id)
{
    ARMCPU *cpu = opaque;

    return cpu->env.v7m.csselr[M_REG_NS] <= R_V7M_CSSELR_INDEX_MASK
        && cpu->env.v7m.csselr[M_REG_S] <= R_V7M_CSSELR_INDEX_MASK;
}

static bool m_csselr_needed(void *opaque)
{
    ARMCPU *cpu = opaque;

    return !arm_v7m_csselr_razwi(cpu);
}

static const VMStateDescription vmstate_m_csselr = {
    .name = "cpu/m/csselr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = m_csselr_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.v7m.csselr, ARMCPU, M_REG_NUM_BANKS),
        VMSTATE_VALIDATE("CSSELR is valid", csselr_vmstate_validate),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_m_scr = {
    .name = "cpu/m/scr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = m_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.v7m.scr[M_REG_NS], ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_m_other_sp = {
    .name = "cpu/m/other-sp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = m_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.v7m.other_sp, ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool m_v8m_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_M) && arm_feature(env, ARM_FEATURE_V8);
}

static const VMStateDescription vmstate_m_v8m = {
    .name = "cpu/m/v8m",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = m_v8m_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.v7m.msplim, ARMCPU, M_REG_NUM_BANKS),
        VMSTATE_UINT32_ARRAY(env.v7m.psplim, ARMCPU, M_REG_NUM_BANKS),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_m_fp = {
    .name = "cpu/m/fp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vfp_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.v7m.fpcar, ARMCPU, M_REG_NUM_BANKS),
        VMSTATE_UINT32_ARRAY(env.v7m.fpccr, ARMCPU, M_REG_NUM_BANKS),
        VMSTATE_UINT32_ARRAY(env.v7m.fpdscr, ARMCPU, M_REG_NUM_BANKS),
        VMSTATE_UINT32_ARRAY(env.v7m.cpacr, ARMCPU, M_REG_NUM_BANKS),
        VMSTATE_UINT32(env.v7m.nsacr, ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool mve_needed(void *opaque)
{
    ARMCPU *cpu = opaque;

    return cpu_isar_feature(aa32_mve, cpu);
}

static const VMStateDescription vmstate_m_mve = {
    .name = "cpu/m/mve",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mve_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.v7m.vpr, ARMCPU),
        VMSTATE_UINT32(env.v7m.ltpsize, ARMCPU),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_m = {
    .name = "cpu/m",
    .version_id = 4,
    .minimum_version_id = 4,
    .needed = m_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.v7m.vecbase[M_REG_NS], ARMCPU),
        VMSTATE_UINT32(env.v7m.basepri[M_REG_NS], ARMCPU),
        VMSTATE_UINT32(env.v7m.control[M_REG_NS], ARMCPU),
        VMSTATE_UINT32(env.v7m.ccr[M_REG_NS], ARMCPU),
        VMSTATE_UINT32(env.v7m.cfsr[M_REG_NS], ARMCPU),
        VMSTATE_UINT32(env.v7m.hfsr, ARMCPU),
        VMSTATE_UINT32(env.v7m.dfsr, ARMCPU),
        VMSTATE_UINT32(env.v7m.mmfar[M_REG_NS], ARMCPU),
        VMSTATE_UINT32(env.v7m.bfar, ARMCPU),
        VMSTATE_UINT32(env.v7m.mpu_ctrl[M_REG_NS], ARMCPU),
        VMSTATE_INT32(env.v7m.exception, ARMCPU),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_m_faultmask_primask,
        &vmstate_m_csselr,
        &vmstate_m_scr,
        &vmstate_m_other_sp,
        &vmstate_m_v8m,
        &vmstate_m_fp,
        &vmstate_m_mve,
        NULL
    }
};

static bool thumb2ee_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_THUMB2EE);
}

static const VMStateDescription vmstate_thumb2ee = {
    .name = "cpu/thumb2ee",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = thumb2ee_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.teecr, ARMCPU),
        VMSTATE_UINT32(env.teehbr, ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool pmsav7_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_PMSA) &&
           arm_feature(env, ARM_FEATURE_V7) &&
           !arm_feature(env, ARM_FEATURE_V8);
}

static bool pmsav7_rgnr_vmstate_validate(void *opaque, int version_id)
{
    ARMCPU *cpu = opaque;

    return cpu->env.pmsav7.rnr[M_REG_NS] < cpu->pmsav7_dregion;
}

static const VMStateDescription vmstate_pmsav7 = {
    .name = "cpu/pmsav7",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pmsav7_needed,
    .fields = (VMStateField[]) {
        VMSTATE_VARRAY_UINT32(env.pmsav7.drbar, ARMCPU, pmsav7_dregion, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(env.pmsav7.drsr, ARMCPU, pmsav7_dregion, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(env.pmsav7.dracr, ARMCPU, pmsav7_dregion, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VALIDATE("rgnr is valid", pmsav7_rgnr_vmstate_validate),
        VMSTATE_END_OF_LIST()
    }
};

static bool pmsav7_rnr_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    /* For R profile cores pmsav7.rnr is migrated via the cpreg
     * "RGNR" definition in helper.h. For M profile we have to
     * migrate it separately.
     */
    return arm_feature(env, ARM_FEATURE_M);
}

static const VMStateDescription vmstate_pmsav7_rnr = {
    .name = "cpu/pmsav7-rnr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pmsav7_rnr_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.pmsav7.rnr[M_REG_NS], ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool pmsav8_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_PMSA) &&
        arm_feature(env, ARM_FEATURE_V8);
}

static bool pmsav8r_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_PMSA) &&
        arm_feature(env, ARM_FEATURE_V8) &&
        !arm_feature(env, ARM_FEATURE_M);
}

static const VMStateDescription vmstate_pmsav8r = {
    .name = "cpu/pmsav8/pmsav8r",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pmsav8r_needed,
    .fields = (VMStateField[]) {
        VMSTATE_VARRAY_UINT32(env.pmsav8.hprbar, ARMCPU,
                        pmsav8r_hdregion, 0, vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(env.pmsav8.hprlar, ARMCPU,
                        pmsav8r_hdregion, 0, vmstate_info_uint32, uint32_t),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_pmsav8 = {
    .name = "cpu/pmsav8",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pmsav8_needed,
    .fields = (VMStateField[]) {
        VMSTATE_VARRAY_UINT32(env.pmsav8.rbar[M_REG_NS], ARMCPU, pmsav7_dregion,
                              0, vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(env.pmsav8.rlar[M_REG_NS], ARMCPU, pmsav7_dregion,
                              0, vmstate_info_uint32, uint32_t),
        VMSTATE_UINT32(env.pmsav8.mair0[M_REG_NS], ARMCPU),
        VMSTATE_UINT32(env.pmsav8.mair1[M_REG_NS], ARMCPU),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_pmsav8r,
        NULL
    }
};

static bool s_rnr_vmstate_validate(void *opaque, int version_id)
{
    ARMCPU *cpu = opaque;

    return cpu->env.pmsav7.rnr[M_REG_S] < cpu->pmsav7_dregion;
}

static bool sau_rnr_vmstate_validate(void *opaque, int version_id)
{
    ARMCPU *cpu = opaque;

    return cpu->env.sau.rnr < cpu->sau_sregion;
}

static bool m_security_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_M_SECURITY);
}

static const VMStateDescription vmstate_m_security = {
    .name = "cpu/m-security",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = m_security_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.v7m.secure, ARMCPU),
        VMSTATE_UINT32(env.v7m.other_ss_msp, ARMCPU),
        VMSTATE_UINT32(env.v7m.other_ss_psp, ARMCPU),
        VMSTATE_UINT32(env.v7m.basepri[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.v7m.primask[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.v7m.faultmask[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.v7m.control[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.v7m.vecbase[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.pmsav8.mair0[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.pmsav8.mair1[M_REG_S], ARMCPU),
        VMSTATE_VARRAY_UINT32(env.pmsav8.rbar[M_REG_S], ARMCPU, pmsav7_dregion,
                              0, vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(env.pmsav8.rlar[M_REG_S], ARMCPU, pmsav7_dregion,
                              0, vmstate_info_uint32, uint32_t),
        VMSTATE_UINT32(env.pmsav7.rnr[M_REG_S], ARMCPU),
        VMSTATE_VALIDATE("secure MPU_RNR is valid", s_rnr_vmstate_validate),
        VMSTATE_UINT32(env.v7m.mpu_ctrl[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.v7m.ccr[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.v7m.mmfar[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.v7m.cfsr[M_REG_S], ARMCPU),
        VMSTATE_UINT32(env.v7m.sfsr, ARMCPU),
        VMSTATE_UINT32(env.v7m.sfar, ARMCPU),
        VMSTATE_VARRAY_UINT32(env.sau.rbar, ARMCPU, sau_sregion, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_VARRAY_UINT32(env.sau.rlar, ARMCPU, sau_sregion, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_UINT32(env.sau.rnr, ARMCPU),
        VMSTATE_VALIDATE("SAU_RNR is valid", sau_rnr_vmstate_validate),
        VMSTATE_UINT32(env.sau.ctrl, ARMCPU),
        VMSTATE_UINT32(env.v7m.scr[M_REG_S], ARMCPU),
        /* AIRCR is not secure-only, but our implementation is R/O if the
         * security extension is unimplemented, so we migrate it here.
         */
        VMSTATE_UINT32(env.v7m.aircr, ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static int get_cpsr(QEMUFile *f, void *opaque, size_t size,
                    const VMStateField *field)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    uint32_t val = qemu_get_be32(f);

    if (arm_feature(env, ARM_FEATURE_M)) {
        if (val & XPSR_EXCP) {
            /* This is a CPSR format value from an older QEMU. (We can tell
             * because values transferred in XPSR format always have zero
             * for the EXCP field, and CPSR format will always have bit 4
             * set in CPSR_M.) Rearrange it into XPSR format. The significant
             * differences are that the T bit is not in the same place, the
             * primask/faultmask info may be in the CPSR I and F bits, and
             * we do not want the mode bits.
             * We know that this cleanup happened before v8M, so there
             * is no complication with banked primask/faultmask.
             */
            uint32_t newval = val;

            assert(!arm_feature(env, ARM_FEATURE_M_SECURITY));

            newval &= (CPSR_NZCV | CPSR_Q | CPSR_IT | CPSR_GE);
            if (val & CPSR_T) {
                newval |= XPSR_T;
            }
            /* If the I or F bits are set then this is a migration from
             * an old QEMU which still stored the M profile FAULTMASK
             * and PRIMASK in env->daif. For a new QEMU, the data is
             * transferred using the vmstate_m_faultmask_primask subsection.
             */
            if (val & CPSR_F) {
                env->v7m.faultmask[M_REG_NS] = 1;
            }
            if (val & CPSR_I) {
                env->v7m.primask[M_REG_NS] = 1;
            }
            val = newval;
        }
        /* Ignore the low bits, they are handled by vmstate_m. */
        xpsr_write(env, val, ~XPSR_EXCP);
        return 0;
    }

    env->aarch64 = ((val & PSTATE_nRW) == 0);

    if (is_a64(env)) {
        pstate_write(env, val);
        return 0;
    }

    cpsr_write(env, val, 0xffffffff, CPSRWriteRaw);
    return 0;
}

static int put_cpsr(QEMUFile *f, void *opaque, size_t size,
                    const VMStateField *field, JSONWriter *vmdesc)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    uint32_t val;

    if (arm_feature(env, ARM_FEATURE_M)) {
        /* The low 9 bits are v7m.exception, which is handled by vmstate_m. */
        val = xpsr_read(env) & ~XPSR_EXCP;
    } else if (is_a64(env)) {
        val = pstate_read(env);
    } else {
        val = cpsr_read(env);
    }

    qemu_put_be32(f, val);
    return 0;
}

static const VMStateInfo vmstate_cpsr = {
    .name = "cpsr",
    .get = get_cpsr,
    .put = put_cpsr,
};

static int get_power(QEMUFile *f, void *opaque, size_t size,
                    const VMStateField *field)
{
    ARMCPU *cpu = opaque;
    bool powered_off = qemu_get_byte(f);
    cpu->power_state = powered_off ? PSCI_OFF : PSCI_ON;
    return 0;
}

static int put_power(QEMUFile *f, void *opaque, size_t size,
                    const VMStateField *field, JSONWriter *vmdesc)
{
    ARMCPU *cpu = opaque;

    /* Migration should never happen while we transition power states */

    if (cpu->power_state == PSCI_ON ||
        cpu->power_state == PSCI_OFF) {
        bool powered_off = (cpu->power_state == PSCI_OFF) ? true : false;
        qemu_put_byte(f, powered_off);
        return 0;
    } else {
        return 1;
    }
}

static const VMStateInfo vmstate_powered_off = {
    .name = "powered_off",
    .get = get_power,
    .put = put_power,
};

static int cpu_pre_save(void *opaque)
{
    ARMCPU *cpu = opaque;

    if (!kvm_enabled()) {
        pmu_op_start(&cpu->env);
    }

    if (kvm_enabled()) {
        if (!write_kvmstate_to_list(cpu)) {
            /* This should never fail */
            g_assert_not_reached();
        }

        /*
         * kvm_arm_cpu_pre_save() must be called after
         * write_kvmstate_to_list()
         */
        kvm_arm_cpu_pre_save(cpu);
    } else {
        if (!write_cpustate_to_list(cpu, false)) {
            /* This should never fail. */
            g_assert_not_reached();
        }
    }

    cpu->cpreg_vmstate_array_len = cpu->cpreg_array_len;
    memcpy(cpu->cpreg_vmstate_indexes, cpu->cpreg_indexes,
           cpu->cpreg_array_len * sizeof(uint64_t));
    memcpy(cpu->cpreg_vmstate_values, cpu->cpreg_values,
           cpu->cpreg_array_len * sizeof(uint64_t));

    return 0;
}

static int cpu_post_save(void *opaque)
{
    ARMCPU *cpu = opaque;

    if (!kvm_enabled()) {
        pmu_op_finish(&cpu->env);
    }

    return 0;
}

static int cpu_pre_load(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    /*
     * Pre-initialize irq_line_state to a value that's never valid as
     * real data, so cpu_post_load() can tell whether we've seen the
     * irq-line-state subsection in the incoming migration state.
     */
    env->irq_line_state = UINT32_MAX;

    if (!kvm_enabled()) {
        pmu_op_start(&cpu->env);
    }

    return 0;
}

static int cpu_post_load(void *opaque, int version_id)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    int i, v;

    /*
     * Handle migration compatibility from old QEMU which didn't
     * send the irq-line-state subsection. A QEMU without it did not
     * implement the HCR_EL2.{VI,VF} bits as generating interrupts,
     * so for TCG the line state matches the bits set in cs->interrupt_request.
     * For KVM the line state is not stored in cs->interrupt_request
     * and so this will leave irq_line_state as 0, but this is OK because
     * we only need to care about it for TCG.
     */
    if (env->irq_line_state == UINT32_MAX) {
        CPUState *cs = CPU(cpu);

        env->irq_line_state = cs->interrupt_request &
            (CPU_INTERRUPT_HARD | CPU_INTERRUPT_FIQ |
             CPU_INTERRUPT_VIRQ | CPU_INTERRUPT_VFIQ);
    }

    /* Update the values list from the incoming migration data.
     * Anything in the incoming data which we don't know about is
     * a migration failure; anything we know about but the incoming
     * data doesn't specify retains its current (reset) value.
     * The indexes list remains untouched -- we only inspect the
     * incoming migration index list so we can match the values array
     * entries with the right slots in our own values array.
     */

    for (i = 0, v = 0; i < cpu->cpreg_array_len
             && v < cpu->cpreg_vmstate_array_len; i++) {
        if (cpu->cpreg_vmstate_indexes[v] > cpu->cpreg_indexes[i]) {
            /* register in our list but not incoming : skip it */
            continue;
        }
        if (cpu->cpreg_vmstate_indexes[v] < cpu->cpreg_indexes[i]) {
            /* register in their list but not ours: fail migration */
            return -1;
        }
        /* matching register, copy the value over */
        cpu->cpreg_values[i] = cpu->cpreg_vmstate_values[v];
        v++;
    }

    if (kvm_enabled()) {
        if (!write_list_to_kvmstate(cpu, KVM_PUT_FULL_STATE)) {
            return -1;
        }
        /* Note that it's OK for the TCG side not to know about
         * every register in the list; KVM is authoritative if
         * we're using it.
         */
        write_list_to_cpustate(cpu);
        kvm_arm_cpu_post_load(cpu);
    } else {
        if (!write_list_to_cpustate(cpu)) {
            return -1;
        }
    }

    /*
     * Misaligned thumb pc is architecturally impossible. Fail the
     * incoming migration. For TCG it would trigger the assert in
     * thumb_tr_translate_insn().
     */
    if (!is_a64(env) && env->thumb && (env->regs[15] & 1)) {
        return -1;
    }

    hw_breakpoint_update_all(cpu);
    hw_watchpoint_update_all(cpu);

    /*
     * TCG gen_update_fp_context() relies on the invariant that
     * FPDSCR.LTPSIZE is constant 4 for M-profile with the LOB extension;
     * forbid bogus incoming data with some other value.
     */
    if (arm_feature(env, ARM_FEATURE_M) && cpu_isar_feature(aa32_lob, cpu)) {
        if (extract32(env->v7m.fpdscr[M_REG_NS],
                      FPCR_LTPSIZE_SHIFT, FPCR_LTPSIZE_LENGTH) != 4 ||
            extract32(env->v7m.fpdscr[M_REG_S],
                      FPCR_LTPSIZE_SHIFT, FPCR_LTPSIZE_LENGTH) != 4) {
            return -1;
        }
    }

    if (!kvm_enabled()) {
        pmu_op_finish(&cpu->env);
    }
    arm_rebuild_hflags(&cpu->env);

    return 0;
}

const VMStateDescription vmstate_arm_cpu = {
    .name = "cpu",
    .version_id = 22,
    .minimum_version_id = 22,
    .pre_save = cpu_pre_save,
    .post_save = cpu_post_save,
    .pre_load = cpu_pre_load,
    .post_load = cpu_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.regs, ARMCPU, 16),
        VMSTATE_UINT64_ARRAY(env.xregs, ARMCPU, 32),
        VMSTATE_UINT64(env.pc, ARMCPU),
        {
            .name = "cpsr",
            .version_id = 0,
            .size = sizeof(uint32_t),
            .info = &vmstate_cpsr,
            .flags = VMS_SINGLE,
            .offset = 0,
        },
        VMSTATE_UINT32(env.spsr, ARMCPU),
        VMSTATE_UINT64_ARRAY(env.banked_spsr, ARMCPU, 8),
        VMSTATE_UINT32_ARRAY(env.banked_r13, ARMCPU, 8),
        VMSTATE_UINT32_ARRAY(env.banked_r14, ARMCPU, 8),
        VMSTATE_UINT32_ARRAY(env.usr_regs, ARMCPU, 5),
        VMSTATE_UINT32_ARRAY(env.fiq_regs, ARMCPU, 5),
        VMSTATE_UINT64_ARRAY(env.elr_el, ARMCPU, 4),
        VMSTATE_UINT64_ARRAY(env.sp_el, ARMCPU, 4),
        /* The length-check must come before the arrays to avoid
         * incoming data possibly overflowing the array.
         */
        VMSTATE_INT32_POSITIVE_LE(cpreg_vmstate_array_len, ARMCPU),
        VMSTATE_VARRAY_INT32(cpreg_vmstate_indexes, ARMCPU,
                             cpreg_vmstate_array_len,
                             0, vmstate_info_uint64, uint64_t),
        VMSTATE_VARRAY_INT32(cpreg_vmstate_values, ARMCPU,
                             cpreg_vmstate_array_len,
                             0, vmstate_info_uint64, uint64_t),
        VMSTATE_UINT64(env.exclusive_addr, ARMCPU),
        VMSTATE_UINT64(env.exclusive_val, ARMCPU),
        VMSTATE_UINT64(env.exclusive_high, ARMCPU),
        VMSTATE_UNUSED(sizeof(uint64_t)),
        VMSTATE_UINT32(env.exception.syndrome, ARMCPU),
        VMSTATE_UINT32(env.exception.fsr, ARMCPU),
        VMSTATE_UINT64(env.exception.vaddress, ARMCPU),
        VMSTATE_TIMER_PTR(gt_timer[GTIMER_PHYS], ARMCPU),
        VMSTATE_TIMER_PTR(gt_timer[GTIMER_VIRT], ARMCPU),
        {
            .name = "power_state",
            .version_id = 0,
            .size = sizeof(bool),
            .info = &vmstate_powered_off,
            .flags = VMS_SINGLE,
            .offset = 0,
        },
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_vfp,
        &vmstate_iwmmxt,
        &vmstate_m,
        &vmstate_thumb2ee,
        /* pmsav7_rnr must come before pmsav7 so that we have the
         * region number before we test it in the VMSTATE_VALIDATE
         * in vmstate_pmsav7.
         */
        &vmstate_pmsav7_rnr,
        &vmstate_pmsav7,
        &vmstate_pmsav8,
        &vmstate_m_security,
#ifdef TARGET_AARCH64
        &vmstate_sve,
        &vmstate_za,
#endif
        &vmstate_serror,
        &vmstate_irq_line_state,
        NULL
    }
};
