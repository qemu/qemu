#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "hw/isa/isa.h"
#include "migration/cpu.h"
#include "kvm/hyperv.h"
#include "hw/i386/x86.h"
#include "kvm/kvm_i386.h"
#include "hw/xen/xen.h"

#include "sysemu/kvm.h"
#include "sysemu/kvm_xen.h"
#include "sysemu/tcg.h"

#include "qemu/error-report.h"

static const VMStateDescription vmstate_segment = {
    .name = "segment",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(base, MTRRVar),
        VMSTATE_UINT64(mask, MTRRVar),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_MTRR_VARS(_field, _state, _n, _v)                    \
    VMSTATE_STRUCT_ARRAY(_field, _state, _n, _v, vmstate_mtrr_var, MTRRVar)

static const VMStateDescription vmstate_lbr_records_var = {
    .name = "lbr_records_var",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(from, LBREntry),
        VMSTATE_UINT64(to, LBREntry),
        VMSTATE_UINT64(info, LBREntry),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_LBR_VARS(_field, _state, _n, _v)                    \
    VMSTATE_STRUCT_ARRAY(_field, _state, _n, _v, vmstate_lbr_records_var, \
                         LBREntry)

typedef struct x86_FPReg_tmp {
    FPReg *parent;
    uint64_t tmp_mant;
    uint16_t tmp_exp;
} x86_FPReg_tmp;

static void cpu_get_fp80(uint64_t *pmant, uint16_t *pexp, floatx80 f)
{
    CPU_LDoubleU temp;

    temp.d = f;
    *pmant = temp.l.lower;
    *pexp = temp.l.upper;
}

static floatx80 cpu_set_fp80(uint64_t mant, uint16_t upper)
{
    CPU_LDoubleU temp;

    temp.l.upper = upper;
    temp.l.lower = mant;
    return temp.d;
}

static int fpreg_pre_save(void *opaque)
{
    x86_FPReg_tmp *tmp = opaque;

    /* we save the real CPU data (in case of MMX usage only 'mant'
       contains the MMX register */
    cpu_get_fp80(&tmp->tmp_mant, &tmp->tmp_exp, tmp->parent->d);

    return 0;
}

static int fpreg_post_load(void *opaque, int version)
{
    x86_FPReg_tmp *tmp = opaque;

    tmp->parent->d = cpu_set_fp80(tmp->tmp_mant, tmp->tmp_exp);
    return 0;
}

static const VMStateDescription vmstate_fpreg_tmp = {
    .name = "fpreg_tmp",
    .post_load = fpreg_post_load,
    .pre_save  = fpreg_pre_save,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(tmp_mant, x86_FPReg_tmp),
        VMSTATE_UINT16(tmp_exp, x86_FPReg_tmp),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_fpreg = {
    .name = "fpreg",
    .fields = (const VMStateField[]) {
        VMSTATE_WITH_TMP(FPReg, x86_FPReg_tmp, vmstate_fpreg_tmp),
        VMSTATE_END_OF_LIST()
    }
};

static int cpu_pre_save(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int i;
    env->v_tpr = env->int_ctl & V_TPR_MASK;
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

#ifdef CONFIG_KVM
    /*
     * In case vCPU may have enabled VMX, we need to make sure kernel have
     * required capabilities in order to perform migration correctly:
     *
     * 1) We must be able to extract vCPU nested-state from KVM.
     *
     * 2) In case vCPU is running in guest-mode and it has a pending exception,
     * we must be able to determine if it's in a pending or injected state.
     * Note that in case KVM don't have required capability to do so,
     * a pending/injected exception will always appear as an
     * injected exception.
     */
    if (kvm_enabled() && cpu_vmx_maybe_enabled(env) &&
        (!env->nested_state ||
         (!kvm_has_exception_payload() && (env->hflags & HF_GUEST_MASK) &&
          env->exception_injected))) {
        error_report("Guest maybe enabled nested virtualization but kernel "
                "does not support required capabilities to save vCPU "
                "nested state");
        return -EINVAL;
    }
#endif

    /*
     * When vCPU is running L2 and exception is still pending,
     * it can potentially be intercepted by L1 hypervisor.
     * In contrast to an injected exception which cannot be
     * intercepted anymore.
     *
     * Furthermore, when a L2 exception is intercepted by L1
     * hypervisor, its exception payload (CR2/DR6 on #PF/#DB)
     * should not be set yet in the respective vCPU register.
     * Thus, in case an exception is pending, it is
     * important to save the exception payload separately.
     *
     * Therefore, if an exception is not in a pending state
     * or vCPU is not in guest-mode, it is not important to
     * distinguish between a pending and injected exception
     * and we don't need to store separately the exception payload.
     *
     * In order to preserve better backwards-compatible migration,
     * convert a pending exception to an injected exception in
     * case it is not important to distinguish between them
     * as described above.
     */
    if (env->exception_pending && !(env->hflags & HF_GUEST_MASK)) {
        env->exception_pending = 0;
        env->exception_injected = 1;

        if (env->exception_has_payload) {
            if (env->exception_nr == EXCP01_DB) {
                env->dr[6] = env->exception_payload;
            } else if (env->exception_nr == EXCP0E_PAGE) {
                env->cr[2] = env->exception_payload;
            }
        }
    }

    return 0;
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

    if (env->fpregs_format_vmstate) {
        error_report("Unsupported old non-softfloat CPU state");
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

#ifdef CONFIG_KVM
    if ((env->hflags & HF_GUEST_MASK) &&
        (!env->nested_state ||
        !(env->nested_state->flags & KVM_STATE_NESTED_GUEST_MODE))) {
        error_report("vCPU set in guest-mode inconsistent with "
                     "migrated kernel nested state");
        return -EINVAL;
    }
#endif

    /*
     * There are cases that we can get valid exception_nr with both
     * exception_pending and exception_injected being cleared.
     * This can happen in one of the following scenarios:
     * 1) Source is older QEMU without KVM_CAP_EXCEPTION_PAYLOAD support.
     * 2) Source is running on kernel without KVM_CAP_EXCEPTION_PAYLOAD support.
     * 3) "cpu/exception_info" subsection not sent because there is no exception
     *    pending or guest wasn't running L2 (See comment in cpu_pre_save()).
     *
     * In those cases, we can just deduce that a valid exception_nr means
     * we can treat the exception as already injected.
     */
    if ((env->exception_nr != -1) &&
        !env->exception_pending && !env->exception_injected) {
        env->exception_injected = 1;
    }

    env->fpstt = (env->fpus_vmstate >> 11) & 7;
    env->fpus = env->fpus_vmstate & ~0x3800;
    env->fptag_vmstate ^= 0xff;
    for(i = 0; i < 8; i++) {
        env->fptags[i] = (env->fptag_vmstate >> i) & 1;
    }
    if (tcg_enabled()) {
        target_ulong dr7;
        update_fp_status(env);
        update_mxcsr_status(env);

        cpu_breakpoint_remove_all(cs, BP_CPU);
        cpu_watchpoint_remove_all(cs, BP_CPU);

        /* Indicate all breakpoints disabled, as they are, then
           let the helper re-enable them.  */
        dr7 = env->dr[7];
        env->dr[7] = dr7 & ~(DR7_GLOBAL_BP_MASK | DR7_LOCAL_BP_MASK);
        cpu_x86_update_dr7(env, dr7);
    }
    tlb_flush(cs);
    return 0;
}

static bool async_pf_msr_needed(void *opaque)
{
    X86CPU *cpu = opaque;

    return cpu->env.async_pf_en_msr != 0;
}

static bool async_pf_int_msr_needed(void *opaque)
{
    X86CPU *cpu = opaque;

    return cpu->env.async_pf_int_msr != 0;
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

static bool exception_info_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    /*
     * It is important to save exception-info only in case
     * we need to distinguish between a pending and injected
     * exception. Which is only required in case there is a
     * pending exception and vCPU is running L2.
     * For more info, refer to comment in cpu_pre_save().
     */
    return env->exception_pending && (env->hflags & HF_GUEST_MASK);
}

static const VMStateDescription vmstate_exception_info = {
    .name = "cpu/exception_info",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = exception_info_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(env.exception_pending, X86CPU),
        VMSTATE_UINT8(env.exception_injected, X86CPU),
        VMSTATE_UINT8(env.exception_has_payload, X86CPU),
        VMSTATE_UINT64(env.exception_payload, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

/* Poll control MSR enabled by default */
static bool poll_control_msr_needed(void *opaque)
{
    X86CPU *cpu = opaque;

    return cpu->env.poll_control_msr != 1;
}

static const VMStateDescription vmstate_steal_time_msr = {
    .name = "cpu/steal_time_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = steal_time_msr_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.steal_time_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_async_pf_msr = {
    .name = "cpu/async_pf_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = async_pf_msr_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.async_pf_en_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_async_pf_int_msr = {
    .name = "cpu/async_pf_int_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = async_pf_int_msr_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.async_pf_int_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pv_eoi_msr = {
    .name = "cpu/async_pv_eoi_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pv_eoi_msr_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.pv_eoi_en_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_poll_control_msr = {
    .name = "cpu/poll_control_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = poll_control_msr_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.poll_control_msr, X86CPU),
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.tsc_adjust, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool msr_smi_count_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return cpu->migrate_smi_count && env->msr_smi_count != 0;
}

static const VMStateDescription vmstate_msr_smi_count = {
    .name = "cpu/msr_smi_count",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = msr_smi_count_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.msr_smi_count, X86CPU),
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.msr_ia32_misc_enable, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_msr_ia32_feature_control = {
    .name = "cpu/msr_ia32_feature_control",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = feature_control_needed,
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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

static const VMStateDescription vmstate_msr_hyperv_hypercall = {
    .name = "cpu/msr_hyperv_hypercall",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_hypercall_enable_needed,
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_tsc, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_crash_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int i;

    for (i = 0; i < HV_CRASH_PARAMS; i++) {
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.msr_hv_crash_params, X86CPU, HV_CRASH_PARAMS),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_runtime_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    if (!hyperv_feat_enabled(cpu, HYPERV_FEAT_RUNTIME)) {
        return false;
    }

    return env->msr_hv_runtime != 0;
}

static const VMStateDescription vmstate_msr_hyperv_runtime = {
    .name = "cpu/msr_hyperv_runtime",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_runtime_enable_needed,
    .fields = (const VMStateField[]) {
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

static int hyperv_synic_post_load(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    hyperv_x86_synic_update(cpu);
    return 0;
}

static const VMStateDescription vmstate_msr_hyperv_synic = {
    .name = "cpu/msr_hyperv_synic",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_synic_enable_needed,
    .post_load = hyperv_synic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_synic_control, X86CPU),
        VMSTATE_UINT64(env.msr_hv_synic_evt_page, X86CPU),
        VMSTATE_UINT64(env.msr_hv_synic_msg_page, X86CPU),
        VMSTATE_UINT64_ARRAY(env.msr_hv_synic_sint, X86CPU, HV_SINT_COUNT),
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.msr_hv_stimer_config, X86CPU,
                             HV_STIMER_COUNT),
        VMSTATE_UINT64_ARRAY(env.msr_hv_stimer_count, X86CPU, HV_STIMER_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyperv_reenlightenment_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->msr_hv_reenlightenment_control != 0 ||
        env->msr_hv_tsc_emulation_control != 0 ||
        env->msr_hv_tsc_emulation_status != 0;
}

static int hyperv_reenlightenment_post_load(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    /*
     * KVM doesn't fully support re-enlightenment notifications so we need to
     * make sure TSC frequency doesn't change upon migration.
     */
    if ((env->msr_hv_reenlightenment_control & HV_REENLIGHTENMENT_ENABLE_BIT) &&
        !env->user_tsc_khz) {
        error_report("Guest enabled re-enlightenment notifications, "
                     "'tsc-frequency=' has to be specified");
        return -EINVAL;
    }

    return 0;
}

static const VMStateDescription vmstate_msr_hyperv_reenlightenment = {
    .name = "cpu/msr_hyperv_reenlightenment",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyperv_reenlightenment_enable_needed,
    .post_load = hyperv_reenlightenment_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.msr_hv_reenlightenment_control, X86CPU),
        VMSTATE_UINT64(env.msr_hv_tsc_emulation_control, X86CPU),
        VMSTATE_UINT64(env.msr_hv_tsc_emulation_status, X86CPU),
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.xss, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool umwait_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->umwait != 0;
}

static const VMStateDescription vmstate_umwait = {
    .name = "cpu/umwait",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = umwait_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(env.umwait, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

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
    .fields = (const VMStateField[]){
        VMSTATE_UINT32(env.pkru, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool pkrs_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->pkrs != 0;
}

static const VMStateDescription vmstate_pkrs = {
    .name = "cpu/pkrs",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pkrs_needed,
    .fields = (const VMStateField[]){
        VMSTATE_UINT32(env.pkrs, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool tsc_khz_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());
    X86MachineClass *x86mc = X86_MACHINE_CLASS(mc);
    return env->tsc_khz && x86mc->save_tsc_khz;
}

static const VMStateDescription vmstate_tsc_khz = {
    .name = "cpu/tsc_khz",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tsc_khz_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(env.tsc_khz, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

#ifdef CONFIG_KVM

static bool vmx_vmcs12_needed(void *opaque)
{
    struct kvm_nested_state *nested_state = opaque;
    return (nested_state->size >
            offsetof(struct kvm_nested_state, data.vmx[0].vmcs12));
}

static const VMStateDescription vmstate_vmx_vmcs12 = {
    .name = "cpu/kvm_nested_state/vmx/vmcs12",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmx_vmcs12_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data.vmx[0].vmcs12,
                            struct kvm_nested_state,
                            KVM_STATE_NESTED_VMX_VMCS_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmx_shadow_vmcs12_needed(void *opaque)
{
    struct kvm_nested_state *nested_state = opaque;
    return (nested_state->size >
            offsetof(struct kvm_nested_state, data.vmx[0].shadow_vmcs12));
}

static const VMStateDescription vmstate_vmx_shadow_vmcs12 = {
    .name = "cpu/kvm_nested_state/vmx/shadow_vmcs12",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmx_shadow_vmcs12_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data.vmx[0].shadow_vmcs12,
                            struct kvm_nested_state,
                            KVM_STATE_NESTED_VMX_VMCS_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmx_nested_state_needed(void *opaque)
{
    struct kvm_nested_state *nested_state = opaque;

    return (nested_state->format == KVM_STATE_NESTED_FORMAT_VMX &&
            nested_state->hdr.vmx.vmxon_pa != -1ull);
}

static const VMStateDescription vmstate_vmx_nested_state = {
    .name = "cpu/kvm_nested_state/vmx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmx_nested_state_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_U64(hdr.vmx.vmxon_pa, struct kvm_nested_state),
        VMSTATE_U64(hdr.vmx.vmcs12_pa, struct kvm_nested_state),
        VMSTATE_U16(hdr.vmx.smm.flags, struct kvm_nested_state),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_vmx_vmcs12,
        &vmstate_vmx_shadow_vmcs12,
        NULL,
    }
};

static bool svm_nested_state_needed(void *opaque)
{
    struct kvm_nested_state *nested_state = opaque;

    /*
     * HF_GUEST_MASK and HF2_GIF_MASK are already serialized
     * via hflags and hflags2, all that's left is the opaque
     * nested state blob.
     */
    return (nested_state->format == KVM_STATE_NESTED_FORMAT_SVM &&
            nested_state->size > offsetof(struct kvm_nested_state, data));
}

static const VMStateDescription vmstate_svm_nested_state = {
    .name = "cpu/kvm_nested_state/svm",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = svm_nested_state_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_U64(hdr.svm.vmcb_pa, struct kvm_nested_state),
        VMSTATE_UINT8_ARRAY(data.svm[0].vmcb12,
                            struct kvm_nested_state,
                            KVM_STATE_NESTED_SVM_VMCB_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static bool nested_state_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return (env->nested_state &&
            (vmx_nested_state_needed(env->nested_state) ||
             svm_nested_state_needed(env->nested_state)));
}

static int nested_state_post_load(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    struct kvm_nested_state *nested_state = env->nested_state;
    int min_nested_state_len = offsetof(struct kvm_nested_state, data);
    int max_nested_state_len = kvm_max_nested_state_length();

    /*
     * If our kernel don't support setting nested state
     * and we have received nested state from migration stream,
     * we need to fail migration
     */
    if (max_nested_state_len <= 0) {
        error_report("Received nested state when kernel cannot restore it");
        return -EINVAL;
    }

    /*
     * Verify that the size of received nested_state struct
     * at least cover required header and is not larger
     * than the max size that our kernel support
     */
    if (nested_state->size < min_nested_state_len) {
        error_report("Received nested state size less than min: "
                     "len=%d, min=%d",
                     nested_state->size, min_nested_state_len);
        return -EINVAL;
    }
    if (nested_state->size > max_nested_state_len) {
        error_report("Received unsupported nested state size: "
                     "nested_state->size=%d, max=%d",
                     nested_state->size, max_nested_state_len);
        return -EINVAL;
    }

    /* Verify format is valid */
    if ((nested_state->format != KVM_STATE_NESTED_FORMAT_VMX) &&
        (nested_state->format != KVM_STATE_NESTED_FORMAT_SVM)) {
        error_report("Received invalid nested state format: %d",
                     nested_state->format);
        return -EINVAL;
    }

    return 0;
}

static const VMStateDescription vmstate_kvm_nested_state = {
    .name = "cpu/kvm_nested_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_U16(flags, struct kvm_nested_state),
        VMSTATE_U16(format, struct kvm_nested_state),
        VMSTATE_U32(size, struct kvm_nested_state),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_vmx_nested_state,
        &vmstate_svm_nested_state,
        NULL
    }
};

static const VMStateDescription vmstate_nested_state = {
    .name = "cpu/nested_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = nested_state_needed,
    .post_load = nested_state_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_POINTER(env.nested_state, X86CPU,
                vmstate_kvm_nested_state,
                struct kvm_nested_state),
        VMSTATE_END_OF_LIST()
    }
};

static bool xen_vcpu_needed(void *opaque)
{
    return (xen_mode == XEN_EMULATE);
}

static const VMStateDescription vmstate_xen_vcpu = {
    .name = "cpu/xen_vcpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_vcpu_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.xen_vcpu_info_gpa, X86CPU),
        VMSTATE_UINT64(env.xen_vcpu_info_default_gpa, X86CPU),
        VMSTATE_UINT64(env.xen_vcpu_time_info_gpa, X86CPU),
        VMSTATE_UINT64(env.xen_vcpu_runstate_gpa, X86CPU),
        VMSTATE_UINT8(env.xen_vcpu_callback_vector, X86CPU),
        VMSTATE_UINT16_ARRAY(env.xen_virq, X86CPU, XEN_NR_VIRQS),
        VMSTATE_UINT64(env.xen_singleshot_timer_ns, X86CPU),
        VMSTATE_UINT64(env.xen_periodic_timer_period, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};
#endif

static bool mcg_ext_ctl_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    return cpu->enable_lmce && env->mcg_ext_ctl;
}

static const VMStateDescription vmstate_mcg_ext_ctl = {
    .name = "cpu/mcg_ext_ctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mcg_ext_ctl_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.mcg_ext_ctl, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool spec_ctrl_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->spec_ctrl != 0;
}

static const VMStateDescription vmstate_spec_ctrl = {
    .name = "cpu/spec_ctrl",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spec_ctrl_needed,
    .fields = (const VMStateField[]){
        VMSTATE_UINT64(env.spec_ctrl, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};


static bool amd_tsc_scale_msr_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return (env->features[FEAT_SVM] & CPUID_SVM_TSCSCALE);
}

static const VMStateDescription amd_tsc_scale_msr_ctrl = {
    .name = "cpu/amd_tsc_scale_msr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = amd_tsc_scale_msr_needed,
    .fields = (const VMStateField[]){
        VMSTATE_UINT64(env.amd_tsc_scale_msr, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};


static bool intel_pt_enable_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    int i;

    if (env->msr_rtit_ctrl || env->msr_rtit_status ||
        env->msr_rtit_output_base || env->msr_rtit_output_mask ||
        env->msr_rtit_cr3_match) {
        return true;
    }

    for (i = 0; i < MAX_RTIT_ADDRS; i++) {
        if (env->msr_rtit_addrs[i]) {
            return true;
        }
    }

    return false;
}

static const VMStateDescription vmstate_msr_intel_pt = {
    .name = "cpu/intel_pt",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = intel_pt_enable_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.msr_rtit_ctrl, X86CPU),
        VMSTATE_UINT64(env.msr_rtit_status, X86CPU),
        VMSTATE_UINT64(env.msr_rtit_output_base, X86CPU),
        VMSTATE_UINT64(env.msr_rtit_output_mask, X86CPU),
        VMSTATE_UINT64(env.msr_rtit_cr3_match, X86CPU),
        VMSTATE_UINT64_ARRAY(env.msr_rtit_addrs, X86CPU, MAX_RTIT_ADDRS),
        VMSTATE_END_OF_LIST()
    }
};

static bool virt_ssbd_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->virt_ssbd != 0;
}

static const VMStateDescription vmstate_msr_virt_ssbd = {
    .name = "cpu/virt_ssbd",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = virt_ssbd_needed,
    .fields = (const VMStateField[]){
        VMSTATE_UINT64(env.virt_ssbd, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool svm_npt_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return !!(env->hflags2 & HF2_NPT_MASK);
}

static const VMStateDescription vmstate_svm_npt = {
    .name = "cpu/svn_npt",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = svm_npt_needed,
    .fields = (const VMStateField[]){
        VMSTATE_UINT64(env.nested_cr3, X86CPU),
        VMSTATE_UINT32(env.nested_pg_mode, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool svm_guest_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return tcg_enabled() && env->int_ctl;
}

static const VMStateDescription vmstate_svm_guest = {
    .name = "cpu/svm_guest",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = svm_guest_needed,
    .fields = (const VMStateField[]){
        VMSTATE_UINT32(env.int_ctl, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

#ifndef TARGET_X86_64
static bool intel_efer32_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->efer != 0;
}

static const VMStateDescription vmstate_efer32 = {
    .name = "cpu/efer32",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = intel_efer32_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.efer, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};
#endif

static bool msr_tsx_ctrl_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->features[FEAT_ARCH_CAPABILITIES] & ARCH_CAP_TSX_CTRL_MSR;
}

static const VMStateDescription vmstate_msr_tsx_ctrl = {
    .name = "cpu/msr_tsx_ctrl",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = msr_tsx_ctrl_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(env.tsx_ctrl, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool intel_sgx_msrs_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return !!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_SGX_LC);
}

static const VMStateDescription vmstate_msr_intel_sgx = {
    .name = "cpu/intel_sgx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = intel_sgx_msrs_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.msr_ia32_sgxlepubkeyhash, X86CPU, 4),
                VMSTATE_END_OF_LIST()
            }
        };

static bool pdptrs_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    return env->pdptrs_valid;
}

static int pdptrs_post_load(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    env->pdptrs_valid = true;
    return 0;
}


static const VMStateDescription vmstate_pdptrs = {
    .name = "cpu/pdptrs",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pdptrs_needed,
    .post_load = pdptrs_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.pdptrs, X86CPU, 4),
        VMSTATE_END_OF_LIST()
    }
};

static bool xfd_msrs_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return !!(env->features[FEAT_XSAVE] & CPUID_D_1_EAX_XFD);
}

static const VMStateDescription vmstate_msr_xfd = {
    .name = "cpu/msr_xfd",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xfd_msrs_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.msr_xfd, X86CPU),
        VMSTATE_UINT64(env.msr_xfd_err, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

#ifdef TARGET_X86_64
static bool amx_xtile_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return !!(env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_TILE);
}

static const VMStateDescription vmstate_amx_xtile = {
    .name = "cpu/intel_amx_xtile",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = amx_xtile_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(env.xtilecfg, X86CPU, 64),
        VMSTATE_UINT8_ARRAY(env.xtiledata, X86CPU, 8192),
        VMSTATE_END_OF_LIST()
    }
};
#endif

static bool arch_lbr_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return !!(env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_ARCH_LBR);
}

static const VMStateDescription vmstate_arch_lbr = {
    .name = "cpu/arch_lbr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = arch_lbr_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.msr_lbr_ctl, X86CPU),
        VMSTATE_UINT64(env.msr_lbr_depth, X86CPU),
        VMSTATE_LBR_VARS(env.lbr_records, X86CPU, ARCH_LBR_NR_ENTRIES, 1),
        VMSTATE_END_OF_LIST()
    }
};

static bool triple_fault_needed(void *opaque)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    return env->triple_fault_pending;
}

static const VMStateDescription vmstate_triple_fault = {
    .name = "cpu/triple_fault",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = triple_fault_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(env.triple_fault_pending, X86CPU),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_x86_cpu = {
    .name = "cpu",
    .version_id = 12,
    .minimum_version_id = 11,
    .pre_save = cpu_pre_save,
    .post_load = cpu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.regs, X86CPU, CPU_NB_REGS),
        VMSTATE_UINTTL(env.eip, X86CPU),
        VMSTATE_UINTTL(env.eflags, X86CPU),
        VMSTATE_UINT32(env.hflags, X86CPU),
        /* FPU */
        VMSTATE_UINT16(env.fpuc, X86CPU),
        VMSTATE_UINT16(env.fpus_vmstate, X86CPU),
        VMSTATE_UINT16(env.fptag_vmstate, X86CPU),
        VMSTATE_UINT16(env.fpregs_format_vmstate, X86CPU),

        VMSTATE_STRUCT_ARRAY(env.fpregs, X86CPU, 8, 0, vmstate_fpreg, FPReg),

        VMSTATE_SEGMENT_ARRAY(env.segs, X86CPU, 6),
        VMSTATE_SEGMENT(env.ldt, X86CPU),
        VMSTATE_SEGMENT(env.tr, X86CPU),
        VMSTATE_SEGMENT(env.gdt, X86CPU),
        VMSTATE_SEGMENT(env.idt, X86CPU),

        VMSTATE_UINT32(env.sysenter_cs, X86CPU),
        VMSTATE_UINTTL(env.sysenter_esp, X86CPU),
        VMSTATE_UINTTL(env.sysenter_eip, X86CPU),

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
        VMSTATE_UINT32(env.smbase, X86CPU),

        VMSTATE_UINT64(env.pat, X86CPU),
        VMSTATE_UINT32(env.hflags2, X86CPU),

        VMSTATE_UINT64(env.vm_hsave, X86CPU),
        VMSTATE_UINT64(env.vm_vmcb, X86CPU),
        VMSTATE_UINT64(env.tsc_offset, X86CPU),
        VMSTATE_UINT64(env.intercept, X86CPU),
        VMSTATE_UINT16(env.intercept_cr_read, X86CPU),
        VMSTATE_UINT16(env.intercept_cr_write, X86CPU),
        VMSTATE_UINT16(env.intercept_dr_read, X86CPU),
        VMSTATE_UINT16(env.intercept_dr_write, X86CPU),
        VMSTATE_UINT32(env.intercept_exceptions, X86CPU),
        VMSTATE_UINT8(env.v_tpr, X86CPU),
        /* MTRRs */
        VMSTATE_UINT64_ARRAY(env.mtrr_fixed, X86CPU, 11),
        VMSTATE_UINT64(env.mtrr_deftype, X86CPU),
        VMSTATE_MTRR_VARS(env.mtrr_var, X86CPU, MSR_MTRRcap_VCNT, 8),
        /* KVM-related states */
        VMSTATE_INT32(env.interrupt_injected, X86CPU),
        VMSTATE_UINT32(env.mp_state, X86CPU),
        VMSTATE_UINT64(env.tsc, X86CPU),
        VMSTATE_INT32(env.exception_nr, X86CPU),
        VMSTATE_UINT8(env.soft_interrupt, X86CPU),
        VMSTATE_UINT8(env.nmi_injected, X86CPU),
        VMSTATE_UINT8(env.nmi_pending, X86CPU),
        VMSTATE_UINT8(env.has_error_code, X86CPU),
        VMSTATE_UINT32(env.sipi_vector, X86CPU),
        /* MCE */
        VMSTATE_UINT64(env.mcg_cap, X86CPU),
        VMSTATE_UINT64(env.mcg_status, X86CPU),
        VMSTATE_UINT64(env.mcg_ctl, X86CPU),
        VMSTATE_UINT64_ARRAY(env.mce_banks, X86CPU, MCE_BANKS_DEF * 4),
        /* rdtscp */
        VMSTATE_UINT64(env.tsc_aux, X86CPU),
        /* KVM pvclock msr */
        VMSTATE_UINT64(env.system_time_msr, X86CPU),
        VMSTATE_UINT64(env.wall_clock_msr, X86CPU),
        /* XSAVE related fields */
        VMSTATE_UINT64_V(env.xcr0, X86CPU, 12),
        VMSTATE_UINT64_V(env.xstate_bv, X86CPU, 12),
        VMSTATE_YMMH_REGS_VARS(env.xmm_regs, X86CPU, 0, 12),
        VMSTATE_END_OF_LIST()
        /* The above list is not sorted /wrt version numbers, watch out! */
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_exception_info,
        &vmstate_async_pf_msr,
        &vmstate_async_pf_int_msr,
        &vmstate_pv_eoi_msr,
        &vmstate_steal_time_msr,
        &vmstate_poll_control_msr,
        &vmstate_fpop_ip_dp,
        &vmstate_msr_tsc_adjust,
        &vmstate_msr_tscdeadline,
        &vmstate_msr_ia32_misc_enable,
        &vmstate_msr_ia32_feature_control,
        &vmstate_msr_architectural_pmu,
        &vmstate_mpx,
        &vmstate_msr_hyperv_hypercall,
        &vmstate_msr_hyperv_vapic,
        &vmstate_msr_hyperv_time,
        &vmstate_msr_hyperv_crash,
        &vmstate_msr_hyperv_runtime,
        &vmstate_msr_hyperv_synic,
        &vmstate_msr_hyperv_stimer,
        &vmstate_msr_hyperv_reenlightenment,
        &vmstate_avx512,
        &vmstate_xss,
        &vmstate_umwait,
        &vmstate_tsc_khz,
        &vmstate_msr_smi_count,
        &vmstate_pkru,
        &vmstate_pkrs,
        &vmstate_spec_ctrl,
        &amd_tsc_scale_msr_ctrl,
        &vmstate_mcg_ext_ctl,
        &vmstate_msr_intel_pt,
        &vmstate_msr_virt_ssbd,
        &vmstate_svm_npt,
        &vmstate_svm_guest,
#ifndef TARGET_X86_64
        &vmstate_efer32,
#endif
#ifdef CONFIG_KVM
        &vmstate_nested_state,
        &vmstate_xen_vcpu,
#endif
        &vmstate_msr_tsx_ctrl,
        &vmstate_msr_intel_sgx,
        &vmstate_pdptrs,
        &vmstate_msr_xfd,
#ifdef TARGET_X86_64
        &vmstate_amx_xtile,
#endif
        &vmstate_arch_lbr,
        &vmstate_triple_fault,
        NULL
    }
};
