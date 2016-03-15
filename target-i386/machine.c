#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/i386/pc.h"
#include "hw/isa/isa.h"
#include "migration/cpu.h"
#include "exec/exec-all.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "sysemu/kvm.h"

#include "qemu/error-report.h"

static const VMStateDescription vmstate_segment = {
    .name = "segment",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(selector, SegmentCache),
        VMSTATE_UINTTL(base, SegmentCache),
        VMSTATE_UINT32(limit, SegmentCache),
        VMSTATE_UINT32(flags, SegmentCache),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_SEGMENT(_field, _state) {                            \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(SegmentCache),                              \
    .vmsd       = &vmstate_segment,                                  \
    .flags      = VMS_STRUCT,                                        \
    .offset     = offsetof(_state, _field)                           \
            + type_check(SegmentCache,typeof_field(_state, _field))  \
}

#define VMSTATE_SEGMENT_ARRAY(_field, _state, _n)                    \
    VMSTATE_STRUCT_ARRAY(_field, _state, _n, 0, vmstate_segment, SegmentCache)

static const VMStateDescription vmstate_xmm_reg = {
    .name = "xmm_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(ZMM_Q(0), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(1), ZMMReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_XMM_REGS(_field, _state, _start)                         \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, CPU_NB_REGS, 0,     \
                             vmstate_xmm_reg, ZMMReg)

/* YMMH format is the same as XMM, but for bits 128-255 */
static const VMStateDescription vmstate_ymmh_reg = {
    .name = "ymmh_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(ZMM_Q(2), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(3), ZMMReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_YMMH_REGS_VARS(_field, _state, _start, _v)               \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, CPU_NB_REGS, _v,    \
                             vmstate_ymmh_reg, ZMMReg)

static const VMStateDescription vmstate_zmmh_reg = {
    .name = "zmmh_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(ZMM_Q(4), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(5), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(6), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(7), ZMMReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_ZMMH_REGS_VARS(_field, _state, _start)                   \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, CPU_NB_REGS, 0,     \
                             vmstate_zmmh_reg, ZMMReg)

#ifdef TARGET_X86_64
static const VMStateDescription vmstate_hi16_zmm_reg = {
    .name = "hi16_zmm_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(ZMM_Q(0), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(1), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(2), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(3), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(4), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(5), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(6), ZMMReg),
        VMSTATE_UINT64(ZMM_Q(7), ZMMReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_Hi16_ZMM_REGS_VARS(_field, _state, _start)               \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, CPU_NB_REGS, 0,     \
                             vmstate_hi16_zmm_reg, ZMMReg)
#endif

static const VMStateDescription vmstate_bnd_regs = {
    .name = "bnd_regs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(lb, BNDReg),
        VMSTATE_UINT64(ub, BNDReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_BND_REGS(_field, _state, _n)          \
    VMSTATE_STRUCT_ARRAY(_field, _state, _n, 0, vmstate_bnd_regs, BNDReg)

static const VMStateDescription vmstate_mtrr_var = {
    .name = "mtrr_var",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(base, MTRRVar),
        VMSTATE_UINT64(mask, MTRRVar),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_MTRR_VARS(_field, _state, _n, _v)                    \
    VMSTATE_STRUCT_ARRAY(_field, _state, _n, _v, vmstate_mtrr_var, MTRRVar)

static void put_fpreg_error(QEMUFile *f, void *opaque, size_t size)
{
    fprintf(stderr, "call put_fpreg() with invalid arguments\n");
    exit(0);
}

/* XXX: add that in a FPU generic layer */
union x86_longdouble {
    uint64_t mant;
    uint16_t exp;
};

#define MANTD1(fp)	(fp & ((1LL << 52) - 1))
#define EXPBIAS1 1023
#define EXPD1(fp)	((fp >> 52) & 0x7FF)
#define SIGND1(fp)	((fp >> 32) & 0x80000000)

static void fp64_to_fp80(union x86_longdouble *p, uint64_t temp)
{
    int e;
    /* mantissa */
    p->mant = (MANTD1(temp) << 11) | (1LL << 63);
    /* exponent + sign */
    e = EXPD1(temp) - EXPBIAS1 + 16383;
    e |= SIGND1(temp) >> 16;
    p->exp = e;
}

static int get_fpreg(QEMUFile *f, void *opaque, size_t size)
{
    FPReg *fp_reg = opaque;
    uint64_t mant;
    uint16_t exp;

    qemu_get_be64s(f, &mant);
    qemu_get_be16s(f, &exp);
    fp_reg->d = cpu_set_fp80(mant, exp);
    return 0;
}

static void put_fpreg(QEMUFile *f, void *opaque, size_t size)
{
    FPReg *fp_reg = opaque;
    uint64_t mant;
    uint16_t exp;
    /* we save the real CPU data (in case of MMX usage only 'mant'
       contains the MMX register */
    cpu_get_fp80(&mant, &exp, fp_reg->d);
    qemu_put_be64s(f, &mant);
    qemu_put_be16s(f, &exp);
}

static const VMStateInfo vmstate_fpreg = {
    .name = "fpreg",
    .get  = get_fpreg,
    .put  = put_fpreg,
};

static int get_fpreg_1_mmx(QEMUFile *f, void *opaque, size_t size)
{
    union x86_longdouble *p = opaque;
    uint64_t mant;

    qemu_get_be64s(f, &mant);
    p->mant = mant;
    p->exp = 0xffff;
    return 0;
}

static const VMStateInfo vmstate_fpreg_1_mmx = {
    .name = "fpreg_1_mmx",
    .get  = get_fpreg_1_mmx,
    .put  = put_fpreg_error,
};

static int get_fpreg_1_no_mmx(QEMUFile *f, void *opaque, size_t size)
{
    union x86_longdouble *p = opaque;
    uint64_t mant;

    qemu_get_be64s(f, &mant);
    fp64_to_fp80(p, mant);
    return 0;
}

static const VMStateInfo vmstate_fpreg_1_no_mmx = {
    .name = "fpreg_1_no_mmx",
    .get  = get_fpreg_1_no_mmx,
    .put  = put_fpreg_error,
};

static bool fpregs_is_0(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return (env->fpregs_format_vmstate == 0);
}

static bool fpregs_is_1_mmx(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int guess_mmx;

    guess_mmx = ((env->fptag_vmstate == 0xff) &&
                 (env->fpus_vmstate & 0x3800) == 0);
    return (guess_mmx && (env->fpregs_format_vmstate == 1));
}

static bool fpregs_is_1_no_mmx(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int guess_mmx;

    guess_mmx = ((env->fptag_vmstate == 0xff) &&
                 (env->fpus_vmstate & 0x3800) == 0);
    return (!guess_mmx && (env->fpregs_format_vmstate == 1));
}

#define VMSTATE_FP_REGS(_field, _state, _n)                               \
    VMSTATE_ARRAY_TEST(_field, _state, _n, fpregs_is_0, vmstate_fpreg, FPReg), \
    VMSTATE_ARRAY_TEST(_field, _state, _n, fpregs_is_1_mmx, vmstate_fpreg_1_mmx, FPReg), \
    VMSTATE_ARRAY_TEST(_field, _state, _n, fpregs_is_1_no_mmx, vmstate_fpreg_1_no_mmx, FPReg)

static bool version_is_5(void *opaque, int version_id)
{
    return version_id == 5;
}

#ifdef TARGET_X86_64
static bool less_than_7(void *opaque, int version_id)
{
    return version_id < 7;
}

static int get_uint64_as_uint32(QEMUFile *f, void *pv, size_t size)
{
    uint64_t *v = pv;
    *v = qemu_get_be32(f);
    return 0;
}

static void put_uint64_as_uint32(QEMUFile *f, void *pv, size_t size)
{
    uint64_t *v = pv;
    qemu_put_be32(f, *v);
}

static const VMStateInfo vmstate_hack_uint64_as_uint32 = {
    .name = "uint64_as_uint32",
    .get  = get_uint64_as_uint32,
    .put  = put_uint64_as_uint32,
};

#define VMSTATE_HACK_UINT32(_f, _s, _t)                                  \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_hack_uint64_as_uint32, uint64_t)
#endif

static void cpu_pre_save(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int i;

    /* FPU */
    env->fpus_vmstate = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    env->fptag_vmstate = 0;
    for(i = 0; i < 8; i++) {
        env->fptag_vmstate |= ((!env->fptags[i]) << i);
    }

    env->fpregs_format_vmstate = 0;

    /*
     * Real mode guest segments register DPL should be zero.
     * Older KVM version were setting it wrongly.
     * Fixing it will allow live migration to host with unrestricted guest
     * support (otherwise the migration will fail with invalid guest state
     * error).
     */
    if (!(env->cr[0] & CR0_PE_MASK) &&
        (env->segs[R_CS].flags >> DESC_DPL_SHIFT & 3) != 0) {
        env->segs[R_CS].flags &= ~(env->segs[R_CS].flags & DESC_DPL_MASK);
        env->segs[R_DS].flags &= ~(env->segs[R_DS].flags & DESC_DPL_MASK);
        env->segs[R_ES].flags &= ~(env->segs[R_ES].flags & DESC_DPL_MASK);
        env->segs[R_FS].flags &= ~(env->segs[R_FS].flags & DESC_DPL_MASK);
        env->segs[R_GS].flags &= ~(env->segs[R_GS].flags & DESC_DPL_MASK);
        env->segs[R_SS].flags &= ~(env->segs[R_SS].flags & DESC_DPL_MASK);
    }

}

static int cpu_post_load(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;
    int i;

    if (env->tsc_khz && env->user_tsc_khz &&
        env->tsc_khz != env->user_tsc_khz) {
        error_report("Mismatch between user-specified TSC frequency and "
                     "migrated TSC frequency");
        return -EINVAL;
    }

    /*
     * Real mode guest segments register DPL should be zero.
     * Older KVM version were setting it wrongly.
     * Fixing it will allow live migration from such host that don't have
     * restricted guest support to a host with unrestricted guest support
     * (otherwise the migration will fail with invalid guest state
     * error).
     */
    if (!(env->cr[0] & CR0_PE_MASK) &&
        (env->segs[R_CS].flags >> DESC_DPL_SHIFT & 3) != 0) {
        env->segs[R_CS].flags &= ~(env->segs[R_CS].flags & DESC_DPL_MASK);
        env->segs[R_DS].flags &= ~(env->segs[R_DS].flags & DESC_DPL_MASK);
        env->segs[R_ES].flags &= ~(env->segs[R_ES].flags & DESC_DPL_MASK);
        env->segs[R_FS].flags &= ~(env->segs[R_FS].flags & DESC_DPL_MASK);
        env->segs[R_GS].flags &= ~(env->segs[R_GS].flags & DESC_DPL_MASK);
        env->segs[R_SS].flags &= ~(env->segs[R_SS].flags & DESC_DPL_MASK);
    }

    /* Older versions of QEMU incorrectly used CS.DPL as the CPL when
     * running under KVM.  This is wrong for conforming code segments.
     * Luckily, in our implementation the CPL field of hflags is redundant
     * and we can get the right value from the SS descriptor privilege level.
     */
    env->hflags &= ~HF_CPL_MASK;
    env->hflags |= (env->segs[R_SS].flags >> DESC_DPL_SHIFT) & HF_CPL_MASK;

    env->fpstt = (env->fpus_vmstate >> 11) & 7;
    env->fpus = env->fpus_vmstate & ~0x3800;
    env->fptag_vmstate ^= 0xff;
    for(i = 0; i < 8; i++) {
        env->fptags[i] = (env->fptag_vmstate >> i) & 1;
    }
    update_fp_status(env);

    cpu_breakpoint_remove_all(cs, BP_CPU);
    cpu_watchpoint_remove_all(cs, BP_CPU);
    {
        /* Indicate all breakpoints disabled, as they are, then
           let the helper re-enable them.  */
        target_ulong dr7 = env->dr[7];
        env->dr[7] = dr7 & ~(DR7_GLOBAL_BP_MASK | DR7_LOCAL_BP_MASK);
        cpu_x86_update_dr7(env, dr7);
    }
    tlb_flush(cs, 1);

    if (tcg_enabled()) {
        cpu_smm_update(cpu);
    }
    return 0;
}

static bool async_pf_msr_needed(void *opaque)
{
    X86CPU *cpu = opaque;

    return cpu->env.async_pf_en_msr != 0;
}

static bool pv_eoi_msr_needed(void *opaque)
{
    X86CPU *cpu = opaque;

    return cpu->env.pv_eoi_en_msr != 0;
}

static bool steal_time_msr_needed(void *opaque)
{
    X86CPU *cpu = opaque;

    return cpu->env.steal_time_msr != 0;
}

static const VMStateDescription vmstate_steal_time_msr = {
    .name = "cpu/steal_time_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = steal_time_msr_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.steal_time_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_async_pf_msr = {
    .name = "cpu/async_pf_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = async_pf_msr_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.async_pf_en_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pv_eoi_msr = {
    .name = "cpu/async_pv_eoi_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pv_eoi_msr_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.pv_eoi_en_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool fpop_ip_dp_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->fpop != 0 || env->fpip != 0 || env->fpdp != 0;
}

static const VMStateDescription vmstate_fpop_ip_dp = {
    .name = "cpu/fpop_ip_dp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fpop_ip_dp_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(env.fpop, X86CPU),
        VMSTATE_UINT64(env.fpip, X86CPU),
        VMSTATE_UINT64(env.fpdp, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool tsc_adjust_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->tsc_adjust != 0;
}

static const VMStateDescription vmstate_msr_tsc_adjust = {
    .name = "cpu/msr_tsc_adjust",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tsc_adjust_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.tsc_adjust, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool tscdeadline_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->tsc_deadline != 0;
}

static const VMStateDescription vmstate_msr_tscdeadline = {
    .name = "cpu/msr_tscdeadline",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tscdeadline_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.tsc_deadline, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool misc_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->msr_ia32_misc_enable != MSR_IA32_MISC_ENABLE_DEFAULT;
}

static bool feature_control_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->msr_ia32_feature_control != 0;
}

static const VMStateDescription vmstate_msr_ia32_misc_enable = {
    .name = "cpu/msr_ia32_misc_enable",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = misc_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_ia32_misc_enable, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_msr_ia32_feature_control = {
    .name = "cpu/msr_ia32_feature_control",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = feature_control_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_ia32_feature_control, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool pmu_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int i;

    if (env->msr_fixed_ctr_ctrl || env->msr_global_ctrl ||
        env->msr_global_status || env->msr_global_ovf_ctrl) {
        return true;
    }
    for (i = 0; i < MAX_FIXED_COUNTERS; i++) {
        if (env->msr_fixed_counters[i]) {
            return true;
        }
    }
    for (i = 0; i < MAX_GP_COUNTERS; i++) {
        if (env->msr_gp_counters[i] || env->msr_gp_evtsel[i]) {
            return true;
        }
    }

    return false;
}

static const VMStateDescription vmstate_msr_architectural_pmu = {
    .name = "cpu/msr_architectural_pmu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pmu_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_fixed_ctr_ctrl, X86CPU),
        VMSTATE_UINT64(env.msr_global_ctrl, X86CPU),
        VMSTATE_UINT64(env.msr_global_status, X86CPU),
        VMSTATE_UINT64(env.msr_global_ovf_ctrl, X86CPU),
        VMSTATE_UINT64_ARRAY(env.msr_fixed_counters, X86CPU, MAX_FIXED_COUNTERS),
        VMSTATE_UINT64_ARRAY(env.msr_gp_counters, X86CPU, MAX_GP_COUNTERS),
        VMSTATE_UINT64_ARRAY(env.msr_gp_evtsel, X86CPU, MAX_GP_COUNTERS),
        VMSTATE_END_OF_LIST()
    }
};

static bool mpx_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    unsigned int i;

    for (i = 0; i < 4; i++) {
        if (env->bnd_regs[i].lb || env->bnd_regs[i].ub) {
            return true;
        }
    }

    if (env->bndcs_regs.cfgu || env->bndcs_regs.sts) {
        return true;
    }

    return !!env->msr_bndcfgs;
}

static const VMStateDescription vmstate_mpx = {
    .name = "cpu/mpx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mpx_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BND_REGS(env.bnd_regs, X86CPU, 4),
        VMSTATE_UINT64(env.bndcs_regs.cfgu, X86CPU),
        VMSTATE_UINT64(env.bndcs_regs.sts, X86CPU),
        VMSTATE_UINT64(env.msr_bndcfgs, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_hypercall_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->msr_hv_hypercall != 0 || env->msr_hv_guest_os_id != 0;
}

static const VMStateDescription vmstate_msr_hypercall_hypercall = {
    .name = "cpu/msr_hyperv_hypercall",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_hypercall_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_guest_os_id, X86CPU),
        VMSTATE_UINT64(env.msr_hv_hypercall, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_vapic_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->msr_hv_vapic != 0;
}

static const VMStateDescription vmstate_msr_hyperv_vapic = {
    .name = "cpu/msr_hyperv_vapic",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_vapic_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_vapic, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_time_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->msr_hv_tsc != 0;
}

static const VMStateDescription vmstate_msr_hyperv_time = {
    .name = "cpu/msr_hyperv_time",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_time_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_tsc, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_crash_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int i;

    for (i = 0; i < HV_X64_MSR_CRASH_PARAMS; i++) {
        if (env->msr_hv_crash_params[i]) {
            return true;
        }
    }
    return false;
}

static const VMStateDescription vmstate_msr_hyperv_crash = {
    .name = "cpu/msr_hyperv_crash",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_crash_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.msr_hv_crash_params,
                             X86CPU, HV_X64_MSR_CRASH_PARAMS),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_runtime_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->msr_hv_runtime != 0;
}

static const VMStateDescription vmstate_msr_hyperv_runtime = {
    .name = "cpu/msr_hyperv_runtime",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_runtime_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_runtime, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_synic_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int i;

    if (env->msr_hv_synic_control != 0 ||
        env->msr_hv_synic_evt_page != 0 ||
        env->msr_hv_synic_msg_page != 0) {
        return true;
    }

    for (i = 0; i < ARRAY_SIZE(env->msr_hv_synic_sint); i++) {
        if (env->msr_hv_synic_sint[i] != 0) {
            return true;
        }
    }

    return false;
}

static const VMStateDescription vmstate_msr_hyperv_synic = {
    .name = "cpu/msr_hyperv_synic",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_synic_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_synic_control, X86CPU),
        VMSTATE_UINT64(env.msr_hv_synic_evt_page, X86CPU),
        VMSTATE_UINT64(env.msr_hv_synic_msg_page, X86CPU),
        VMSTATE_UINT64_ARRAY(env.msr_hv_synic_sint, X86CPU,
                             HV_SYNIC_SINT_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_stimer_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int i;

    for (i = 0; i < ARRAY_SIZE(env->msr_hv_stimer_config); i++) {
        if (env->msr_hv_stimer_config[i] || env->msr_hv_stimer_count[i]) {
            return true;
        }
    }
    return false;
}

static const VMStateDescription vmstate_msr_hyperv_stimer = {
    .name = "cpu/msr_hyperv_stimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_stimer_enable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.msr_hv_stimer_config,
                             X86CPU, HV_SYNIC_STIMER_COUNT),
        VMSTATE_UINT64_ARRAY(env.msr_hv_stimer_count,
                             X86CPU, HV_SYNIC_STIMER_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static bool avx512_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    unsigned int i;

    for (i = 0; i < NB_OPMASK_REGS; i++) {
        if (env->opmask_regs[i]) {
            return true;
        }
    }

    for (i = 0; i < CPU_NB_REGS; i++) {
#define ENV_XMM(reg, field) (env->xmm_regs[reg].ZMM_Q(field))
        if (ENV_XMM(i, 4) || ENV_XMM(i, 6) ||
            ENV_XMM(i, 5) || ENV_XMM(i, 7)) {
            return true;
        }
#ifdef TARGET_X86_64
        if (ENV_XMM(i+16, 0) || ENV_XMM(i+16, 1) ||
            ENV_XMM(i+16, 2) || ENV_XMM(i+16, 3) ||
            ENV_XMM(i+16, 4) || ENV_XMM(i+16, 5) ||
            ENV_XMM(i+16, 6) || ENV_XMM(i+16, 7)) {
            return true;
        }
#endif
    }

    return false;
}

static const VMStateDescription vmstate_avx512 = {
    .name = "cpu/avx512",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = avx512_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.opmask_regs, X86CPU, NB_OPMASK_REGS),
        VMSTATE_ZMMH_REGS_VARS(env.xmm_regs, X86CPU, 0),
#ifdef TARGET_X86_64
        VMSTATE_Hi16_ZMM_REGS_VARS(env.xmm_regs, X86CPU, 16),
#endif
        VMSTATE_END_OF_LIST()
    }
};

static bool xss_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->xss != 0;
}

static const VMStateDescription vmstate_xss = {
    .name = "cpu/xss",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xss_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.xss, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

#ifdef TARGET_X86_64
static bool pkru_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->pkru != 0;
}

static const VMStateDescription vmstate_pkru = {
    .name = "cpu/pkru",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pkru_needed,
    .fields = (VMStateField[]){
        VMSTATE_UINT32(env.pkru, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};
#endif

static bool tsc_khz_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());
    PCMachineClass *pcmc = PC_MACHINE_CLASS(mc);
    return env->tsc_khz && pcmc->save_tsc_khz;
}

static const VMStateDescription vmstate_tsc_khz = {
    .name = "cpu/tsc_khz",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tsc_khz_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(env.tsc_khz, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

VMStateDescription vmstate_x86_cpu = {
    .name = "cpu",
    .version_id = 12,
    .minimum_version_id = 3,
    .pre_save = cpu_pre_save,
    .post_load = cpu_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.regs, X86CPU, CPU_NB_REGS),
        VMSTATE_UINTTL(env.eip, X86CPU),
        VMSTATE_UINTTL(env.eflags, X86CPU),
        VMSTATE_UINT32(env.hflags, X86CPU),
        /* FPU */
        VMSTATE_UINT16(env.fpuc, X86CPU),
        VMSTATE_UINT16(env.fpus_vmstate, X86CPU),
        VMSTATE_UINT16(env.fptag_vmstate, X86CPU),
        VMSTATE_UINT16(env.fpregs_format_vmstate, X86CPU),
        VMSTATE_FP_REGS(env.fpregs, X86CPU, 8),

        VMSTATE_SEGMENT_ARRAY(env.segs, X86CPU, 6),
        VMSTATE_SEGMENT(env.ldt, X86CPU),
        VMSTATE_SEGMENT(env.tr, X86CPU),
        VMSTATE_SEGMENT(env.gdt, X86CPU),
        VMSTATE_SEGMENT(env.idt, X86CPU),

        VMSTATE_UINT32(env.sysenter_cs, X86CPU),
#ifdef TARGET_X86_64
        /* Hack: In v7 size changed from 32 to 64 bits on x86_64 */
        VMSTATE_HACK_UINT32(env.sysenter_esp, X86CPU, less_than_7),
        VMSTATE_HACK_UINT32(env.sysenter_eip, X86CPU, less_than_7),
        VMSTATE_UINTTL_V(env.sysenter_esp, X86CPU, 7),
        VMSTATE_UINTTL_V(env.sysenter_eip, X86CPU, 7),
#else
        VMSTATE_UINTTL(env.sysenter_esp, X86CPU),
        VMSTATE_UINTTL(env.sysenter_eip, X86CPU),
#endif

        VMSTATE_UINTTL(env.cr[0], X86CPU),
        VMSTATE_UINTTL(env.cr[2], X86CPU),
        VMSTATE_UINTTL(env.cr[3], X86CPU),
        VMSTATE_UINTTL(env.cr[4], X86CPU),
        VMSTATE_UINTTL_ARRAY(env.dr, X86CPU, 8),
        /* MMU */
        VMSTATE_INT32(env.a20_mask, X86CPU),
        /* XMM */
        VMSTATE_UINT32(env.mxcsr, X86CPU),
        VMSTATE_XMM_REGS(env.xmm_regs, X86CPU, 0),

#ifdef TARGET_X86_64
        VMSTATE_UINT64(env.efer, X86CPU),
        VMSTATE_UINT64(env.star, X86CPU),
        VMSTATE_UINT64(env.lstar, X86CPU),
        VMSTATE_UINT64(env.cstar, X86CPU),
        VMSTATE_UINT64(env.fmask, X86CPU),
        VMSTATE_UINT64(env.kernelgsbase, X86CPU),
#endif
        VMSTATE_UINT32_V(env.smbase, X86CPU, 4),

        VMSTATE_UINT64_V(env.pat, X86CPU, 5),
        VMSTATE_UINT32_V(env.hflags2, X86CPU, 5),

        VMSTATE_UINT32_TEST(parent_obj.halted, X86CPU, version_is_5),
        VMSTATE_UINT64_V(env.vm_hsave, X86CPU, 5),
        VMSTATE_UINT64_V(env.vm_vmcb, X86CPU, 5),
        VMSTATE_UINT64_V(env.tsc_offset, X86CPU, 5),
        VMSTATE_UINT64_V(env.intercept, X86CPU, 5),
        VMSTATE_UINT16_V(env.intercept_cr_read, X86CPU, 5),
        VMSTATE_UINT16_V(env.intercept_cr_write, X86CPU, 5),
        VMSTATE_UINT16_V(env.intercept_dr_read, X86CPU, 5),
        VMSTATE_UINT16_V(env.intercept_dr_write, X86CPU, 5),
        VMSTATE_UINT32_V(env.intercept_exceptions, X86CPU, 5),
        VMSTATE_UINT8_V(env.v_tpr, X86CPU, 5),
        /* MTRRs */
        VMSTATE_UINT64_ARRAY_V(env.mtrr_fixed, X86CPU, 11, 8),
        VMSTATE_UINT64_V(env.mtrr_deftype, X86CPU, 8),
        VMSTATE_MTRR_VARS(env.mtrr_var, X86CPU, MSR_MTRRcap_VCNT, 8),
        /* KVM-related states */
        VMSTATE_INT32_V(env.interrupt_injected, X86CPU, 9),
        VMSTATE_UINT32_V(env.mp_state, X86CPU, 9),
        VMSTATE_UINT64_V(env.tsc, X86CPU, 9),
        VMSTATE_INT32_V(env.exception_injected, X86CPU, 11),
        VMSTATE_UINT8_V(env.soft_interrupt, X86CPU, 11),
        VMSTATE_UINT8_V(env.nmi_injected, X86CPU, 11),
        VMSTATE_UINT8_V(env.nmi_pending, X86CPU, 11),
        VMSTATE_UINT8_V(env.has_error_code, X86CPU, 11),
        VMSTATE_UINT32_V(env.sipi_vector, X86CPU, 11),
        /* MCE */
        VMSTATE_UINT64_V(env.mcg_cap, X86CPU, 10),
        VMSTATE_UINT64_V(env.mcg_status, X86CPU, 10),
        VMSTATE_UINT64_V(env.mcg_ctl, X86CPU, 10),
        VMSTATE_UINT64_ARRAY_V(env.mce_banks, X86CPU, MCE_BANKS_DEF * 4, 10),
        /* rdtscp */
        VMSTATE_UINT64_V(env.tsc_aux, X86CPU, 11),
        /* KVM pvclock msr */
        VMSTATE_UINT64_V(env.system_time_msr, X86CPU, 11),
        VMSTATE_UINT64_V(env.wall_clock_msr, X86CPU, 11),
        /* XSAVE related fields */
        VMSTATE_UINT64_V(env.xcr0, X86CPU, 12),
        VMSTATE_UINT64_V(env.xstate_bv, X86CPU, 12),
        VMSTATE_YMMH_REGS_VARS(env.xmm_regs, X86CPU, 0, 12),
        VMSTATE_END_OF_LIST()
        /* The above list is not sorted /wrt version numbers, watch out! */
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_async_pf_msr,
        &vmstate_pv_eoi_msr,
        &vmstate_steal_time_msr,
        &vmstate_fpop_ip_dp,
        &vmstate_msr_tsc_adjust,
        &vmstate_msr_tscdeadline,
        &vmstate_msr_ia32_misc_enable,
        &vmstate_msr_ia32_feature_control,
        &vmstate_msr_architectural_pmu,
        &vmstate_mpx,
        &vmstate_msr_hypercall_hypercall,
        &vmstate_msr_hyperv_vapic,
        &vmstate_msr_hyperv_time,
        &vmstate_msr_hyperv_crash,
        &vmstate_msr_hyperv_runtime,
        &vmstate_msr_hyperv_synic,
        &vmstate_msr_hyperv_stimer,
        &vmstate_avx512,
        &vmstate_xss,
        &vmstate_tsc_khz,
#ifdef TARGET_X86_64
        &vmstate_pkru,
#endif
        NULL
    }
};
