#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/i386/pc.h"
#include "hw/isa/isa.h"

#include "cpu.h"
#include "sysemu/kvm.h"

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
        VMSTATE_UINT64(XMM_Q(0), XMMReg),
        VMSTATE_UINT64(XMM_Q(1), XMMReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_XMM_REGS(_field, _state, _n)                         \
    VMSTATE_STRUCT_ARRAY(_field, _state, _n, 0, vmstate_xmm_reg, XMMReg)

/* YMMH format is the same as XMM */
static const VMStateDescription vmstate_ymmh_reg = {
    .name = "ymmh_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(XMM_Q(0), XMMReg),
        VMSTATE_UINT64(XMM_Q(1), XMMReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_YMMH_REGS_VARS(_field, _state, _n, _v)                         \
    VMSTATE_STRUCT_ARRAY(_field, _state, _n, _v, vmstate_ymmh_reg, XMMReg)

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
    for (i = 0; i < DR7_MAX_BP; i++) {
        hw_breakpoint_insert(env, i);
    }
    tlb_flush(cs, 1);

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
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.steal_time_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_async_pf_msr = {
    .name = "cpu/async_pf_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.async_pf_en_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pv_eoi_msr = {
    .name = "cpu/async_pv_eoi_msr",
    .version_id = 1,
    .minimum_version_id = 1,
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
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_ia32_misc_enable, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_msr_ia32_feature_control = {
    .name = "cpu/msr_ia32_feature_control",
    .version_id = 1,
    .minimum_version_id = 1,
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
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_tsc, X86CPU),
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
        VMSTATE_XMM_REGS(env.xmm_regs, X86CPU, CPU_NB_REGS),

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
        VMSTATE_YMMH_REGS_VARS(env.ymmh_regs, X86CPU, CPU_NB_REGS, 12),
        VMSTATE_END_OF_LIST()
        /* The above list is not sorted /wrt version numbers, watch out! */
    },
    .subsections = (VMStateSubsection []) {
        {
            .vmsd = &vmstate_async_pf_msr,
            .needed = async_pf_msr_needed,
        } , {
            .vmsd = &vmstate_pv_eoi_msr,
            .needed = pv_eoi_msr_needed,
        } , {
            .vmsd = &vmstate_steal_time_msr,
            .needed = steal_time_msr_needed,
        } , {
            .vmsd = &vmstate_fpop_ip_dp,
            .needed = fpop_ip_dp_needed,
        }, {
            .vmsd = &vmstate_msr_tsc_adjust,
            .needed = tsc_adjust_needed,
        }, {
            .vmsd = &vmstate_msr_tscdeadline,
            .needed = tscdeadline_needed,
        }, {
            .vmsd = &vmstate_msr_ia32_misc_enable,
            .needed = misc_enable_needed,
        }, {
            .vmsd = &vmstate_msr_ia32_feature_control,
            .needed = feature_control_needed,
        }, {
            .vmsd = &vmstate_msr_architectural_pmu,
            .needed = pmu_enable_needed,
        } , {
            .vmsd = &vmstate_mpx,
            .needed = mpx_needed,
        }, {
            .vmsd = &vmstate_msr_hypercall_hypercall,
            .needed = hyperv_hypercall_enable_needed,
        }, {
            .vmsd = &vmstate_msr_hyperv_vapic,
            .needed = hyperv_vapic_enable_needed,
        }, {
            .vmsd = &vmstate_msr_hyperv_time,
            .needed = hyperv_time_enable_needed,
        } , {
            /* empty */
        }
    }
};
