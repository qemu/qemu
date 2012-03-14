#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/pc.h"
#include "hw/isa.h"

#include "cpu.h"
#include "kvm.h"

static const VMStateDescription vmstate_segment = {
    .name = "segment",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
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
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
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
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT64(XMM_Q(0), XMMReg),
        VMSTATE_UINT64(XMM_Q(1), XMMReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_YMMH_REGS_VARS(_field, _state, _n, _v)                         \
    VMSTATE_STRUCT_ARRAY(_field, _state, _n, _v, vmstate_ymmh_reg, XMMReg)

static const VMStateDescription vmstate_mtrr_var = {
    .name = "mtrr_var",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
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
    CPUX86State *env = opaque;

    return (env->fpregs_format_vmstate == 0);
}

static bool fpregs_is_1_mmx(void *opaque, int version_id)
{
    CPUX86State *env = opaque;
    int guess_mmx;

    guess_mmx = ((env->fptag_vmstate == 0xff) &&
                 (env->fpus_vmstate & 0x3800) == 0);
    return (guess_mmx && (env->fpregs_format_vmstate == 1));
}

static bool fpregs_is_1_no_mmx(void *opaque, int version_id)
{
    CPUX86State *env = opaque;
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
    CPUX86State *env = opaque;
    int i;

    /* FPU */
    env->fpus_vmstate = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    env->fptag_vmstate = 0;
    for(i = 0; i < 8; i++) {
        env->fptag_vmstate |= ((!env->fptags[i]) << i);
    }

    env->fpregs_format_vmstate = 0;
}

static int cpu_post_load(void *opaque, int version_id)
{
    CPUX86State *env = opaque;
    int i;

    /* XXX: restore FPU round state */
    env->fpstt = (env->fpus_vmstate >> 11) & 7;
    env->fpus = env->fpus_vmstate & ~0x3800;
    env->fptag_vmstate ^= 0xff;
    for(i = 0; i < 8; i++) {
        env->fptags[i] = (env->fptag_vmstate >> i) & 1;
    }

    cpu_breakpoint_remove_all(env, BP_CPU);
    cpu_watchpoint_remove_all(env, BP_CPU);
    for (i = 0; i < 4; i++)
        hw_breakpoint_insert(env, i);

    tlb_flush(env, 1);
    return 0;
}

static bool async_pf_msr_needed(void *opaque)
{
    CPUX86State *cpu = opaque;

    return cpu->async_pf_en_msr != 0;
}

static const VMStateDescription vmstate_async_pf_msr = {
    .name = "cpu/async_pf_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT64(async_pf_en_msr, CPUX86State),
        VMSTATE_END_OF_LIST()
    }
};

static bool fpop_ip_dp_needed(void *opaque)
{
    CPUX86State *env = opaque;

    return env->fpop != 0 || env->fpip != 0 || env->fpdp != 0;
}

static const VMStateDescription vmstate_fpop_ip_dp = {
    .name = "cpu/fpop_ip_dp",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT16(fpop, CPUX86State),
        VMSTATE_UINT64(fpip, CPUX86State),
        VMSTATE_UINT64(fpdp, CPUX86State),
        VMSTATE_END_OF_LIST()
    }
};

static bool tscdeadline_needed(void *opaque)
{
    CPUX86State *env = opaque;

    return env->tsc_deadline != 0;
}

static const VMStateDescription vmstate_msr_tscdeadline = {
    .name = "cpu/msr_tscdeadline",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT64(tsc_deadline, CPUX86State),
        VMSTATE_END_OF_LIST()
    }
};

static bool misc_enable_needed(void *opaque)
{
    CPUX86State *env = opaque;

    return env->msr_ia32_misc_enable != MSR_IA32_MISC_ENABLE_DEFAULT;
}

static const VMStateDescription vmstate_msr_ia32_misc_enable = {
    .name = "cpu/msr_ia32_misc_enable",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT64(msr_ia32_misc_enable, CPUX86State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_cpu = {
    .name = "cpu",
    .version_id = CPU_SAVE_VERSION,
    .minimum_version_id = 3,
    .minimum_version_id_old = 3,
    .pre_save = cpu_pre_save,
    .post_load = cpu_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINTTL_ARRAY(regs, CPUX86State, CPU_NB_REGS),
        VMSTATE_UINTTL(eip, CPUX86State),
        VMSTATE_UINTTL(eflags, CPUX86State),
        VMSTATE_UINT32(hflags, CPUX86State),
        /* FPU */
        VMSTATE_UINT16(fpuc, CPUX86State),
        VMSTATE_UINT16(fpus_vmstate, CPUX86State),
        VMSTATE_UINT16(fptag_vmstate, CPUX86State),
        VMSTATE_UINT16(fpregs_format_vmstate, CPUX86State),
        VMSTATE_FP_REGS(fpregs, CPUX86State, 8),

        VMSTATE_SEGMENT_ARRAY(segs, CPUX86State, 6),
        VMSTATE_SEGMENT(ldt, CPUX86State),
        VMSTATE_SEGMENT(tr, CPUX86State),
        VMSTATE_SEGMENT(gdt, CPUX86State),
        VMSTATE_SEGMENT(idt, CPUX86State),

        VMSTATE_UINT32(sysenter_cs, CPUX86State),
#ifdef TARGET_X86_64
        /* Hack: In v7 size changed from 32 to 64 bits on x86_64 */
        VMSTATE_HACK_UINT32(sysenter_esp, CPUX86State, less_than_7),
        VMSTATE_HACK_UINT32(sysenter_eip, CPUX86State, less_than_7),
        VMSTATE_UINTTL_V(sysenter_esp, CPUX86State, 7),
        VMSTATE_UINTTL_V(sysenter_eip, CPUX86State, 7),
#else
        VMSTATE_UINTTL(sysenter_esp, CPUX86State),
        VMSTATE_UINTTL(sysenter_eip, CPUX86State),
#endif

        VMSTATE_UINTTL(cr[0], CPUX86State),
        VMSTATE_UINTTL(cr[2], CPUX86State),
        VMSTATE_UINTTL(cr[3], CPUX86State),
        VMSTATE_UINTTL(cr[4], CPUX86State),
        VMSTATE_UINTTL_ARRAY(dr, CPUX86State, 8),
        /* MMU */
        VMSTATE_INT32(a20_mask, CPUX86State),
        /* XMM */
        VMSTATE_UINT32(mxcsr, CPUX86State),
        VMSTATE_XMM_REGS(xmm_regs, CPUX86State, CPU_NB_REGS),

#ifdef TARGET_X86_64
        VMSTATE_UINT64(efer, CPUX86State),
        VMSTATE_UINT64(star, CPUX86State),
        VMSTATE_UINT64(lstar, CPUX86State),
        VMSTATE_UINT64(cstar, CPUX86State),
        VMSTATE_UINT64(fmask, CPUX86State),
        VMSTATE_UINT64(kernelgsbase, CPUX86State),
#endif
        VMSTATE_UINT32_V(smbase, CPUX86State, 4),

        VMSTATE_UINT64_V(pat, CPUX86State, 5),
        VMSTATE_UINT32_V(hflags2, CPUX86State, 5),

        VMSTATE_UINT32_TEST(halted, CPUX86State, version_is_5),
        VMSTATE_UINT64_V(vm_hsave, CPUX86State, 5),
        VMSTATE_UINT64_V(vm_vmcb, CPUX86State, 5),
        VMSTATE_UINT64_V(tsc_offset, CPUX86State, 5),
        VMSTATE_UINT64_V(intercept, CPUX86State, 5),
        VMSTATE_UINT16_V(intercept_cr_read, CPUX86State, 5),
        VMSTATE_UINT16_V(intercept_cr_write, CPUX86State, 5),
        VMSTATE_UINT16_V(intercept_dr_read, CPUX86State, 5),
        VMSTATE_UINT16_V(intercept_dr_write, CPUX86State, 5),
        VMSTATE_UINT32_V(intercept_exceptions, CPUX86State, 5),
        VMSTATE_UINT8_V(v_tpr, CPUX86State, 5),
        /* MTRRs */
        VMSTATE_UINT64_ARRAY_V(mtrr_fixed, CPUX86State, 11, 8),
        VMSTATE_UINT64_V(mtrr_deftype, CPUX86State, 8),
        VMSTATE_MTRR_VARS(mtrr_var, CPUX86State, 8, 8),
        /* KVM-related states */
        VMSTATE_INT32_V(interrupt_injected, CPUX86State, 9),
        VMSTATE_UINT32_V(mp_state, CPUX86State, 9),
        VMSTATE_UINT64_V(tsc, CPUX86State, 9),
        VMSTATE_INT32_V(exception_injected, CPUX86State, 11),
        VMSTATE_UINT8_V(soft_interrupt, CPUX86State, 11),
        VMSTATE_UINT8_V(nmi_injected, CPUX86State, 11),
        VMSTATE_UINT8_V(nmi_pending, CPUX86State, 11),
        VMSTATE_UINT8_V(has_error_code, CPUX86State, 11),
        VMSTATE_UINT32_V(sipi_vector, CPUX86State, 11),
        /* MCE */
        VMSTATE_UINT64_V(mcg_cap, CPUX86State, 10),
        VMSTATE_UINT64_V(mcg_status, CPUX86State, 10),
        VMSTATE_UINT64_V(mcg_ctl, CPUX86State, 10),
        VMSTATE_UINT64_ARRAY_V(mce_banks, CPUX86State, MCE_BANKS_DEF *4, 10),
        /* rdtscp */
        VMSTATE_UINT64_V(tsc_aux, CPUX86State, 11),
        /* KVM pvclock msr */
        VMSTATE_UINT64_V(system_time_msr, CPUX86State, 11),
        VMSTATE_UINT64_V(wall_clock_msr, CPUX86State, 11),
        /* XSAVE related fields */
        VMSTATE_UINT64_V(xcr0, CPUX86State, 12),
        VMSTATE_UINT64_V(xstate_bv, CPUX86State, 12),
        VMSTATE_YMMH_REGS_VARS(ymmh_regs, CPUX86State, CPU_NB_REGS, 12),
        VMSTATE_END_OF_LIST()
        /* The above list is not sorted /wrt version numbers, watch out! */
    },
    .subsections = (VMStateSubsection []) {
        {
            .vmsd = &vmstate_async_pf_msr,
            .needed = async_pf_msr_needed,
        } , {
            .vmsd = &vmstate_fpop_ip_dp,
            .needed = fpop_ip_dp_needed,
        }, {
            .vmsd = &vmstate_msr_tscdeadline,
            .needed = tscdeadline_needed,
        }, {
            .vmsd = &vmstate_msr_ia32_misc_enable,
            .needed = misc_enable_needed,
        } , {
            /* empty */
        }
    }
};

void cpu_save(QEMUFile *f, void *opaque)
{
    vmstate_save_state(f, &vmstate_cpu, opaque);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    return vmstate_load_state(f, &vmstate_cpu, opaque, version_id);
}
