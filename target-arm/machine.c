#include "hw/hw.h"
#include "hw/boards.h"

static bool vfp_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_VFP);
}

static int get_fpscr(QEMUFile *f, void *opaque, size_t size)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    uint32_t val = qemu_get_be32(f);

    vfp_set_fpscr(env, val);
    return 0;
}

static void put_fpscr(QEMUFile *f, void *opaque, size_t size)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    qemu_put_be32(f, vfp_get_fpscr(env));
}

static const VMStateInfo vmstate_fpscr = {
    .name = "fpscr",
    .get = get_fpscr,
    .put = put_fpscr,
};

static const VMStateDescription vmstate_vfp = {
    .name = "cpu/vfp",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields = (VMStateField[]) {
        VMSTATE_FLOAT64_ARRAY(env.vfp.regs, ARMCPU, 32),
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
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.iwmmxt.regs, ARMCPU, 16),
        VMSTATE_UINT32_ARRAY(env.iwmmxt.cregs, ARMCPU, 16),
        VMSTATE_END_OF_LIST()
    }
};

static bool m_needed(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    return arm_feature(env, ARM_FEATURE_M);
}

const VMStateDescription vmstate_m = {
    .name = "cpu/m",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.v7m.other_sp, ARMCPU),
        VMSTATE_UINT32(env.v7m.vecbase, ARMCPU),
        VMSTATE_UINT32(env.v7m.basepri, ARMCPU),
        VMSTATE_UINT32(env.v7m.control, ARMCPU),
        VMSTATE_INT32(env.v7m.current_sp, ARMCPU),
        VMSTATE_INT32(env.v7m.exception, ARMCPU),
        VMSTATE_END_OF_LIST()
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
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(env.teecr, ARMCPU),
        VMSTATE_UINT32(env.teehbr, ARMCPU),
        VMSTATE_END_OF_LIST()
    }
};

static int get_cpsr(QEMUFile *f, void *opaque, size_t size)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    uint32_t val = qemu_get_be32(f);

    /* Avoid mode switch when restoring CPSR */
    env->uncached_cpsr = val & CPSR_M;
    cpsr_write(env, val, 0xffffffff);
    return 0;
}

static void put_cpsr(QEMUFile *f, void *opaque, size_t size)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    qemu_put_be32(f, cpsr_read(env));
}

static const VMStateInfo vmstate_cpsr = {
    .name = "cpsr",
    .get = get_cpsr,
    .put = put_cpsr,
};

const VMStateDescription vmstate_arm_cpu = {
    .name = "cpu",
    .version_id = 11,
    .minimum_version_id = 11,
    .minimum_version_id_old = 11,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.regs, ARMCPU, 16),
        {
            .name = "cpsr",
            .version_id = 0,
            .size = sizeof(uint32_t),
            .info = &vmstate_cpsr,
            .flags = VMS_SINGLE,
            .offset = 0,
        },
        VMSTATE_UINT32(env.spsr, ARMCPU),
        VMSTATE_UINT32_ARRAY(env.banked_spsr, ARMCPU, 6),
        VMSTATE_UINT32_ARRAY(env.banked_r13, ARMCPU, 6),
        VMSTATE_UINT32_ARRAY(env.banked_r14, ARMCPU, 6),
        VMSTATE_UINT32_ARRAY(env.usr_regs, ARMCPU, 5),
        VMSTATE_UINT32_ARRAY(env.fiq_regs, ARMCPU, 5),
        VMSTATE_UINT32(env.cp15.c0_cpuid, ARMCPU),
        VMSTATE_UINT32(env.cp15.c0_cssel, ARMCPU),
        VMSTATE_UINT32(env.cp15.c1_sys, ARMCPU),
        VMSTATE_UINT32(env.cp15.c1_coproc, ARMCPU),
        VMSTATE_UINT32(env.cp15.c1_xscaleauxcr, ARMCPU),
        VMSTATE_UINT32(env.cp15.c1_scr, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_base0, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_base0_hi, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_base1, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_base1_hi, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_control, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_mask, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_base_mask, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_data, ARMCPU),
        VMSTATE_UINT32(env.cp15.c2_insn, ARMCPU),
        VMSTATE_UINT32(env.cp15.c3, ARMCPU),
        VMSTATE_UINT32(env.cp15.c5_insn, ARMCPU),
        VMSTATE_UINT32(env.cp15.c5_data, ARMCPU),
        VMSTATE_UINT32_ARRAY(env.cp15.c6_region, ARMCPU, 8),
        VMSTATE_UINT32(env.cp15.c6_insn, ARMCPU),
        VMSTATE_UINT32(env.cp15.c6_data, ARMCPU),
        VMSTATE_UINT32(env.cp15.c7_par, ARMCPU),
        VMSTATE_UINT32(env.cp15.c7_par_hi, ARMCPU),
        VMSTATE_UINT32(env.cp15.c9_insn, ARMCPU),
        VMSTATE_UINT32(env.cp15.c9_data, ARMCPU),
        VMSTATE_UINT32(env.cp15.c9_pmcr, ARMCPU),
        VMSTATE_UINT32(env.cp15.c9_pmcnten, ARMCPU),
        VMSTATE_UINT32(env.cp15.c9_pmovsr, ARMCPU),
        VMSTATE_UINT32(env.cp15.c9_pmxevtyper, ARMCPU),
        VMSTATE_UINT32(env.cp15.c9_pmuserenr, ARMCPU),
        VMSTATE_UINT32(env.cp15.c9_pminten, ARMCPU),
        VMSTATE_UINT32(env.cp15.c13_fcse, ARMCPU),
        VMSTATE_UINT32(env.cp15.c13_context, ARMCPU),
        VMSTATE_UINT32(env.cp15.c13_tls1, ARMCPU),
        VMSTATE_UINT32(env.cp15.c13_tls2, ARMCPU),
        VMSTATE_UINT32(env.cp15.c13_tls3, ARMCPU),
        VMSTATE_UINT32(env.cp15.c15_cpar, ARMCPU),
        VMSTATE_UINT32(env.cp15.c15_ticonfig, ARMCPU),
        VMSTATE_UINT32(env.cp15.c15_i_max, ARMCPU),
        VMSTATE_UINT32(env.cp15.c15_i_min, ARMCPU),
        VMSTATE_UINT32(env.cp15.c15_threadid, ARMCPU),
        VMSTATE_UINT32(env.cp15.c15_power_control, ARMCPU),
        VMSTATE_UINT32(env.cp15.c15_diagnostic, ARMCPU),
        VMSTATE_UINT32(env.cp15.c15_power_diagnostic, ARMCPU),
        VMSTATE_UINT32(env.exclusive_addr, ARMCPU),
        VMSTATE_UINT32(env.exclusive_val, ARMCPU),
        VMSTATE_UINT32(env.exclusive_high, ARMCPU),
        VMSTATE_UINT64(env.features, ARMCPU),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (VMStateSubsection[]) {
        {
            .vmsd = &vmstate_vfp,
            .needed = vfp_needed,
        } , {
            .vmsd = &vmstate_iwmmxt,
            .needed = iwmmxt_needed,
        } , {
            .vmsd = &vmstate_m,
            .needed = m_needed,
        } , {
            .vmsd = &vmstate_thumb2ee,
            .needed = thumb2ee_needed,
        } , {
            /* empty */
        }
    }
};
