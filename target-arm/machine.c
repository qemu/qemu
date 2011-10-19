#include "hw/hw.h"
#include "hw/boards.h"

void cpu_save(QEMUFile *f, void *opaque)
{
    int i;
    CPUARMState *env = (CPUARMState *)opaque;

    for (i = 0; i < 16; i++) {
        qemu_put_be32(f, env->regs[i]);
    }
    qemu_put_be32(f, cpsr_read(env));
    qemu_put_be32(f, env->spsr);
    for (i = 0; i < 6; i++) {
        qemu_put_be32(f, env->banked_spsr[i]);
        qemu_put_be32(f, env->banked_r13[i]);
        qemu_put_be32(f, env->banked_r14[i]);
    }
    for (i = 0; i < 5; i++) {
        qemu_put_be32(f, env->usr_regs[i]);
        qemu_put_be32(f, env->fiq_regs[i]);
    }
    qemu_put_be32(f, env->cp15.c0_cpuid);
    qemu_put_be32(f, env->cp15.c0_cachetype);
    qemu_put_be32(f, env->cp15.c0_cssel);
    qemu_put_be32(f, env->cp15.c1_sys);
    qemu_put_be32(f, env->cp15.c1_coproc);
    qemu_put_be32(f, env->cp15.c1_xscaleauxcr);
    qemu_put_be32(f, env->cp15.c2_base0);
    qemu_put_be32(f, env->cp15.c2_base1);
    qemu_put_be32(f, env->cp15.c2_control);
    qemu_put_be32(f, env->cp15.c2_mask);
    qemu_put_be32(f, env->cp15.c2_base_mask);
    qemu_put_be32(f, env->cp15.c2_data);
    qemu_put_be32(f, env->cp15.c2_insn);
    qemu_put_be32(f, env->cp15.c3);
    qemu_put_be32(f, env->cp15.c5_insn);
    qemu_put_be32(f, env->cp15.c5_data);
    for (i = 0; i < 8; i++) {
        qemu_put_be32(f, env->cp15.c6_region[i]);
    }
    qemu_put_be32(f, env->cp15.c6_insn);
    qemu_put_be32(f, env->cp15.c6_data);
    qemu_put_be32(f, env->cp15.c7_par);
    qemu_put_be32(f, env->cp15.c9_insn);
    qemu_put_be32(f, env->cp15.c9_data);
    qemu_put_be32(f, env->cp15.c9_pmcr);
    qemu_put_be32(f, env->cp15.c9_pmcnten);
    qemu_put_be32(f, env->cp15.c9_pmovsr);
    qemu_put_be32(f, env->cp15.c9_pmxevtyper);
    qemu_put_be32(f, env->cp15.c9_pmuserenr);
    qemu_put_be32(f, env->cp15.c9_pminten);
    qemu_put_be32(f, env->cp15.c13_fcse);
    qemu_put_be32(f, env->cp15.c13_context);
    qemu_put_be32(f, env->cp15.c13_tls1);
    qemu_put_be32(f, env->cp15.c13_tls2);
    qemu_put_be32(f, env->cp15.c13_tls3);
    qemu_put_be32(f, env->cp15.c15_cpar);

    qemu_put_be32(f, env->features);

    if (arm_feature(env, ARM_FEATURE_VFP)) {
        for (i = 0;  i < 16; i++) {
            CPU_DoubleU u;
            u.d = env->vfp.regs[i];
            qemu_put_be32(f, u.l.upper);
            qemu_put_be32(f, u.l.lower);
        }
        for (i = 0; i < 16; i++) {
            qemu_put_be32(f, env->vfp.xregs[i]);
        }

        /* TODO: Should use proper FPSCR access functions.  */
        qemu_put_be32(f, env->vfp.vec_len);
        qemu_put_be32(f, env->vfp.vec_stride);

        if (arm_feature(env, ARM_FEATURE_VFP3)) {
            for (i = 16;  i < 32; i++) {
                CPU_DoubleU u;
                u.d = env->vfp.regs[i];
                qemu_put_be32(f, u.l.upper);
                qemu_put_be32(f, u.l.lower);
            }
        }
    }

    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        for (i = 0; i < 16; i++) {
            qemu_put_be64(f, env->iwmmxt.regs[i]);
        }
        for (i = 0; i < 16; i++) {
            qemu_put_be32(f, env->iwmmxt.cregs[i]);
        }
    }

    if (arm_feature(env, ARM_FEATURE_M)) {
        qemu_put_be32(f, env->v7m.other_sp);
        qemu_put_be32(f, env->v7m.vecbase);
        qemu_put_be32(f, env->v7m.basepri);
        qemu_put_be32(f, env->v7m.control);
        qemu_put_be32(f, env->v7m.current_sp);
        qemu_put_be32(f, env->v7m.exception);
    }

    if (arm_feature(env, ARM_FEATURE_THUMB2EE)) {
        qemu_put_be32(f, env->teecr);
        qemu_put_be32(f, env->teehbr);
    }
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    CPUARMState *env = (CPUARMState *)opaque;
    int i;
    uint32_t val;

    if (version_id != CPU_SAVE_VERSION)
        return -EINVAL;

    for (i = 0; i < 16; i++) {
        env->regs[i] = qemu_get_be32(f);
    }
    val = qemu_get_be32(f);
    /* Avoid mode switch when restoring CPSR.  */
    env->uncached_cpsr = val & CPSR_M;
    cpsr_write(env, val, 0xffffffff);
    env->spsr = qemu_get_be32(f);
    for (i = 0; i < 6; i++) {
        env->banked_spsr[i] = qemu_get_be32(f);
        env->banked_r13[i] = qemu_get_be32(f);
        env->banked_r14[i] = qemu_get_be32(f);
    }
    for (i = 0; i < 5; i++) {
        env->usr_regs[i] = qemu_get_be32(f);
        env->fiq_regs[i] = qemu_get_be32(f);
    }
    env->cp15.c0_cpuid = qemu_get_be32(f);
    env->cp15.c0_cachetype = qemu_get_be32(f);
    env->cp15.c0_cssel = qemu_get_be32(f);
    env->cp15.c1_sys = qemu_get_be32(f);
    env->cp15.c1_coproc = qemu_get_be32(f);
    env->cp15.c1_xscaleauxcr = qemu_get_be32(f);
    env->cp15.c2_base0 = qemu_get_be32(f);
    env->cp15.c2_base1 = qemu_get_be32(f);
    env->cp15.c2_control = qemu_get_be32(f);
    env->cp15.c2_mask = qemu_get_be32(f);
    env->cp15.c2_base_mask = qemu_get_be32(f);
    env->cp15.c2_data = qemu_get_be32(f);
    env->cp15.c2_insn = qemu_get_be32(f);
    env->cp15.c3 = qemu_get_be32(f);
    env->cp15.c5_insn = qemu_get_be32(f);
    env->cp15.c5_data = qemu_get_be32(f);
    for (i = 0; i < 8; i++) {
        env->cp15.c6_region[i] = qemu_get_be32(f);
    }
    env->cp15.c6_insn = qemu_get_be32(f);
    env->cp15.c6_data = qemu_get_be32(f);
    env->cp15.c7_par = qemu_get_be32(f);
    env->cp15.c9_insn = qemu_get_be32(f);
    env->cp15.c9_data = qemu_get_be32(f);
    env->cp15.c9_pmcr = qemu_get_be32(f);
    env->cp15.c9_pmcnten = qemu_get_be32(f);
    env->cp15.c9_pmovsr = qemu_get_be32(f);
    env->cp15.c9_pmxevtyper = qemu_get_be32(f);
    env->cp15.c9_pmuserenr = qemu_get_be32(f);
    env->cp15.c9_pminten = qemu_get_be32(f);
    env->cp15.c13_fcse = qemu_get_be32(f);
    env->cp15.c13_context = qemu_get_be32(f);
    env->cp15.c13_tls1 = qemu_get_be32(f);
    env->cp15.c13_tls2 = qemu_get_be32(f);
    env->cp15.c13_tls3 = qemu_get_be32(f);
    env->cp15.c15_cpar = qemu_get_be32(f);

    env->features = qemu_get_be32(f);

    if (arm_feature(env, ARM_FEATURE_VFP)) {
        for (i = 0;  i < 16; i++) {
            CPU_DoubleU u;
            u.l.upper = qemu_get_be32(f);
            u.l.lower = qemu_get_be32(f);
            env->vfp.regs[i] = u.d;
        }
        for (i = 0; i < 16; i++) {
            env->vfp.xregs[i] = qemu_get_be32(f);
        }

        /* TODO: Should use proper FPSCR access functions.  */
        env->vfp.vec_len = qemu_get_be32(f);
        env->vfp.vec_stride = qemu_get_be32(f);

        if (arm_feature(env, ARM_FEATURE_VFP3)) {
            for (i = 16;  i < 32; i++) {
                CPU_DoubleU u;
                u.l.upper = qemu_get_be32(f);
                u.l.lower = qemu_get_be32(f);
                env->vfp.regs[i] = u.d;
            }
        }
    }

    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        for (i = 0; i < 16; i++) {
            env->iwmmxt.regs[i] = qemu_get_be64(f);
        }
        for (i = 0; i < 16; i++) {
            env->iwmmxt.cregs[i] = qemu_get_be32(f);
        }
    }

    if (arm_feature(env, ARM_FEATURE_M)) {
        env->v7m.other_sp = qemu_get_be32(f);
        env->v7m.vecbase = qemu_get_be32(f);
        env->v7m.basepri = qemu_get_be32(f);
        env->v7m.control = qemu_get_be32(f);
        env->v7m.current_sp = qemu_get_be32(f);
        env->v7m.exception = qemu_get_be32(f);
    }

    if (arm_feature(env, ARM_FEATURE_THUMB2EE)) {
        env->teecr = qemu_get_be32(f);
        env->teehbr = qemu_get_be32(f);
    }

    return 0;
}
