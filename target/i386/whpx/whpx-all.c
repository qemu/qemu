/*
 * QEMU Windows Hypervisor Platform accelerator (WHPX)
 *
 * Copyright Microsoft Corp. 2017
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/typedefs.h"
#include "system/address-spaces.h"
#include "system/ioport.h"
#include "gdbstub/helpers.h"
#include "qemu/accel.h"
#include "accel/accel-ops.h"
#include "system/memory.h"
#include "system/whpx.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "qemu/main-loop.h"
#include "qemu/memalign.h"
#include "hw/core/boards.h"
#include "hw/intc/ioapic.h"
#include "hw/intc/i8259.h"
#include "hw/i386/x86.h"
#include "hw/i386/apic_internal.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-common.h"
#include "qapi/qapi-visit-common.h"
#include "migration/blocker.h"
#include "host-cpu.h"
#include "accel/accel-cpu-target.h"
#include <winerror.h>

#include "system/whpx-internal.h"
#include "system/whpx-accel-ops.h"
#include "system/whpx-all.h"
#include "system/whpx-common.h"
#include "whpx-i386.h"

#include "emulate/x86_decode.h"
#include "emulate/x86_emu.h"
#include "emulate/x86_flags.h"
#include "emulate/x86_mmu.h"
#include "trace.h"

#include <winhvplatform.h>

#define HYPERV_APIC_BUS_FREQUENCY      (200000000ULL)
/* for kernel-irqchip=off */
#define HV_X64_MSR_APIC_FREQUENCY       0x40000023
#define HV_X64_MSR_VP_ASSIST_PAGE       0x40000073
#define HV_X64_MSR_GUEST_IDLE           0x400000f0

static bool is_modern_os = true;

static const WHV_REGISTER_NAME whpx_register_names[] = {

    /* X64 General purpose registers */
    WHvX64RegisterRax,
    WHvX64RegisterRcx,
    WHvX64RegisterRdx,
    WHvX64RegisterRbx,
    WHvX64RegisterRsp,
    WHvX64RegisterRbp,
    WHvX64RegisterRsi,
    WHvX64RegisterRdi,
    WHvX64RegisterR8,
    WHvX64RegisterR9,
    WHvX64RegisterR10,
    WHvX64RegisterR11,
    WHvX64RegisterR12,
    WHvX64RegisterR13,
    WHvX64RegisterR14,
    WHvX64RegisterR15,
    WHvX64RegisterRip,
    WHvX64RegisterRflags,

    /* X64 Segment registers */
    WHvX64RegisterEs,
    WHvX64RegisterCs,
    WHvX64RegisterSs,
    WHvX64RegisterDs,
    WHvX64RegisterFs,
    WHvX64RegisterGs,
    WHvX64RegisterLdtr,
    WHvX64RegisterTr,

    /* X64 Table registers */
    WHvX64RegisterIdtr,
    WHvX64RegisterGdtr,

    /* X64 Control Registers */
    WHvX64RegisterCr0,
    WHvX64RegisterCr2,
    WHvX64RegisterCr3,
    WHvX64RegisterCr4,

    /* X64 Debug Registers */
    /*
     * WHvX64RegisterDr0,
     * WHvX64RegisterDr1,
     * WHvX64RegisterDr2,
     * WHvX64RegisterDr3,
     * WHvX64RegisterDr6,
     * WHvX64RegisterDr7,
     */

    /* X64 MSRs */
    WHvX64RegisterEfer,
#ifdef TARGET_X86_64
    WHvX64RegisterKernelGsBase,
#endif
    /* WHvX64RegisterPat, */
    WHvX64RegisterSysenterCs,
    WHvX64RegisterSysenterEip,
    WHvX64RegisterSysenterEsp,
    WHvX64RegisterStar,
#ifdef TARGET_X86_64
    WHvX64RegisterLstar,
    WHvX64RegisterCstar,
    WHvX64RegisterSfmask,
#endif

    /* Interrupt / Event Registers */
    /*
     * WHvRegisterPendingInterruption,
     * WHvRegisterInterruptState,
     * WHvRegisterPendingEvent0,
     * WHvRegisterPendingEvent1
     * WHvX64RegisterDeliverabilityNotifications,
     */
};

static const WHV_REGISTER_NAME whpx_register_names_for_vmexit[] = {
    /* X64 General purpose registers */
    WHvX64RegisterRax,
    WHvX64RegisterRcx,
    WHvX64RegisterRdx,
    WHvX64RegisterRbx,
    WHvX64RegisterRsp,
    WHvX64RegisterRbp,
    WHvX64RegisterRsi,
    WHvX64RegisterRdi,
    WHvX64RegisterR8,
    WHvX64RegisterR9,
    WHvX64RegisterR10,
    WHvX64RegisterR11,
    WHvX64RegisterR12,
    WHvX64RegisterR13,
    WHvX64RegisterR14,
    WHvX64RegisterR15,
};

static const WHV_REGISTER_NAME whpx_register_names_legacy_fp[] = {
    /* X64 Floating Point and Vector Registers (non-xsave) */
    WHvX64RegisterXmm0,
    WHvX64RegisterXmm1,
    WHvX64RegisterXmm2,
    WHvX64RegisterXmm3,
    WHvX64RegisterXmm4,
    WHvX64RegisterXmm5,
    WHvX64RegisterXmm6,
    WHvX64RegisterXmm7,
    WHvX64RegisterXmm8,
    WHvX64RegisterXmm9,
    WHvX64RegisterXmm10,
    WHvX64RegisterXmm11,
    WHvX64RegisterXmm12,
    WHvX64RegisterXmm13,
    WHvX64RegisterXmm14,
    WHvX64RegisterXmm15,
    WHvX64RegisterFpMmx0,
    WHvX64RegisterFpMmx1,
    WHvX64RegisterFpMmx2,
    WHvX64RegisterFpMmx3,
    WHvX64RegisterFpMmx4,
    WHvX64RegisterFpMmx5,
    WHvX64RegisterFpMmx6,
    WHvX64RegisterFpMmx7,
    WHvX64RegisterFpControlStatus,
    WHvX64RegisterXmmControlStatus,
};

struct whpx_register_set {
    WHV_REGISTER_VALUE values[RTL_NUMBER_OF(whpx_register_names)];
};

/*
 * The current implementation of instruction stepping sets the TF flag
 * in RFLAGS, causing the CPU to raise an INT1 after each instruction.
 * This corresponds to the WHvX64ExceptionTypeDebugTrapOrFault exception.
 *
 * This approach has a few limitations:
 *     1. Stepping over a PUSHF/SAHF instruction will save the TF flag
 *        along with the other flags, possibly restoring it later. It would
 *        result in another INT1 when the flags are restored, triggering
 *        a stop in gdb that could be cleared by doing another step.
 *
 *        Stepping over a POPF/LAHF instruction will let it overwrite the
 *        TF flags, ending the stepping mode.
 *
 *     2. Stepping over an instruction raising an exception (e.g. INT, DIV,
 *        or anything that could result in a page fault) will save the flags
 *        to the stack, clear the TF flag, and let the guest execute the
 *        handler. Normally, the guest will restore the original flags,
 *        that will continue single-stepping.
 *
 *     3. Debuggers running on the guest may wish to set TF to do instruction
 *        stepping. INT1 events generated by it would be intercepted by us,
 *        as long as the gdb is connected to QEMU.
 *
 * In practice this means that:
 *     1. Stepping through flags-modifying instructions may cause gdb to
 *        continue or stop in unexpected places. This will be fully recoverable
 *        and will not crash the target.
 *
 *     2. Stepping over an instruction that triggers an exception will step
 *        over the exception handler, not into it.
 *
 *     3. Debugging the guest via gdb, while running debugger on the guest
 *        at the same time may lead to unexpected effects. Removing all
 *        breakpoints set via QEMU will prevent any further interference
 *        with the guest-level debuggers.
 *
 * The limitations can be addressed as shown below:
 *     1. PUSHF/SAHF/POPF/LAHF/IRET instructions can be emulated instead of
 *        stepping through them. The exact semantics of the instructions is
 *        defined in the "Combined Volume Set of Intel 64 and IA-32
 *        Architectures Software Developer's Manuals", however it involves a
 *        fair amount of corner cases due to compatibility with real mode,
 *        virtual 8086 mode, and differences between 64-bit and 32-bit modes.
 *
 *     2. We could step into the guest's exception handlers using the following
 *        sequence:
 *          a. Temporarily enable catching of all exception types via
 *             whpx_set_exception_exit_bitmap().
 *          b. Once an exception is intercepted, read the IDT/GDT and locate
 *             the original handler.
 *          c. Patch the original handler, injecting an INT3 at the beginning.
 *          d. Update the exception exit bitmap to only catch the
 *             WHvX64ExceptionTypeBreakpointTrap exception.
 *          e. Let the affected CPU run in the exclusive mode.
 *          f. Restore the original handler and the exception exit bitmap.
 *        Note that handling all corner cases related to IDT/GDT is harder
 *        than it may seem. See x86_cpu_translate_for_debug() for a
 *        rough idea.
 *
 *     3. In order to properly support guest-level debugging in parallel with
 *        the QEMU-level debugging, we would need to be able to pass some INT1
 *        events to the guest. This could be done via the following methods:
 *          a. Using the WHvRegisterPendingEvent register. As of Windows 21H1,
 *             it seems to only work for interrupts and not software
 *             exceptions.
 *          b. Locating and patching the original handler by parsing IDT/GDT.
 *             This involves relatively complex logic outlined in the previous
 *             paragraph.
 *          c. Emulating the exception invocation (i.e. manually updating RIP,
 *             RFLAGS, and pushing the old values to stack). This is even more
 *             complicated than the previous option, since it involves checking
 *             CPL, gate attributes, and doing various adjustments depending
 *             on the current CPU mode, whether the CPL is changing, etc.
 */
typedef enum WhpxStepMode {
    WHPX_STEP_NONE = 0,
    /* Halt other VCPUs */
    WHPX_STEP_EXCLUSIVE,
} WhpxStepMode;

static uint32_t max_vcpu_index;
static WHV_PROCESSOR_XSAVE_FEATURES whpx_xsave_cap;

bool whpx_has_xsave(void)
{
    return whpx_xsave_cap.XsaveSupport;
}

bool whpx_has_xsaves(void)
{
    return whpx_xsave_cap.XsaveSupervisorSupport;
}

static bool whpx_rdtsc_cap;

bool whpx_has_rdtscp(void)
{
    return whpx_rdtsc_cap;
}

static bool whpx_invpcid_cap;

bool whpx_has_invpcid(void)
{
    return whpx_invpcid_cap;
}

static WHV_X64_SEGMENT_REGISTER whpx_seg_q2h(const SegmentCache *qs, int v86,
                                             int r86)
{
    WHV_X64_SEGMENT_REGISTER hs;
    unsigned flags = qs->flags;

    hs.Base = qs->base;
    hs.Limit = qs->limit;
    hs.Selector = qs->selector;

    if (v86) {
        hs.Attributes = 0;
        hs.SegmentType = 3;
        hs.Present = 1;
        hs.DescriptorPrivilegeLevel = 3;
        hs.NonSystemSegment = 1;

    } else {
        hs.Attributes = (flags >> DESC_TYPE_SHIFT);

        if (r86) {
            /* hs.Base &= 0xfffff; */
        }
    }

    return hs;
}

static SegmentCache whpx_seg_h2q(const WHV_X64_SEGMENT_REGISTER *hs)
{
    SegmentCache qs;

    qs.base = hs->Base;
    qs.limit = hs->Limit;
    qs.selector = hs->Selector;

    qs.flags = ((uint32_t)hs->Attributes) << DESC_TYPE_SHIFT;

    return qs;
}

/* X64 Extended Control Registers */
static void whpx_set_xcrs(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    WHV_REGISTER_VALUE xcr0;
    WHV_REGISTER_NAME xcr0_name = WHvX64RegisterXCr0;

    if (!whpx_has_xsave()) {
        return;
    }

    /* Only xcr0 is supported by the hypervisor currently */
    xcr0.Reg64 = cpu_env(cpu)->xcr0;
    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index, &xcr0_name, 1, &xcr0);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to set register xcr0, hr=%08lx", hr);
    }
}

static int whpx_set_tsc(CPUState *cpu)
{
    WHV_REGISTER_NAME tsc_reg = WHvX64RegisterTsc;
    WHV_REGISTER_VALUE tsc_val;
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;

    /*
     * Suspend the partition prior to setting the TSC to reduce the variance
     * in TSC across vCPUs. When the first vCPU runs post suspend, the
     * partition is automatically resumed.
     */
    if (whp_dispatch.WHvSuspendPartitionTime) {

        /*
         * Unable to suspend partition while setting TSC is not a fatal
         * error. It just increases the likelihood of TSC variance between
         * vCPUs and some guest OS are able to handle that just fine.
         */
        hr = whp_dispatch.WHvSuspendPartitionTime(whpx->partition);
        if (FAILED(hr)) {
            warn_report("WHPX: Failed to suspend partition, hr=%08lx", hr);
        }
    }

    tsc_val.Reg64 = cpu_env(cpu)->tsc;
    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index, &tsc_reg, 1, &tsc_val);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to set TSC, hr=%08lx", hr);
        return -1;
    }

    return 0;
}

static bool whpx_is_xsave_enabled(CPUState *cpu)
{
    CPUX86State *env = &X86_CPU(cpu)->env;
    return env->cr[4] & CR4_OSXSAVE_MASK;
}

static size_t whpx_get_xsave_max_len(void)
{
    return whpx_get_supported_cpuid(0xd, 0, R_ECX);
}

static int whpx_set_xsave_state(const CPUState *cpu)
{
    struct whpx_state *whpx = &whpx_global;
    X86CPU *x86cpu = X86_CPU(cpu);
    CPUX86State *env = &x86cpu->env;
    HRESULT hr;
    void *xsavec_buf;
    size_t page = qemu_real_host_page_size();
    size_t xsavec_buf_len;

    /* allocate and populate compacted buffer */
    xsavec_buf_len = whpx_get_xsave_max_len();
    xsavec_buf = qemu_memalign(page, xsavec_buf_len);

    /* save registers to standard format buffer */
    x86_cpu_xsave_all_areas(x86cpu, env->xsave_buf, env->xsave_buf_len);

    /* store compacted version of xsave area in xsavec_buf */
    compact_xsave_area(env, xsavec_buf, xsavec_buf_len);

    if (!whpx_is_legacy_os()) {
        hr = whp_dispatch.WHvSetVirtualProcessorState(
            whpx->partition, cpu->cpu_index,
            WHvVirtualProcessorStateTypeXsaveState,
            xsavec_buf,
            xsavec_buf_len);
    } else {
        hr = whp_dispatch.WHvSetVirtualProcessorXsaveState(
            whpx->partition, cpu->cpu_index,
            xsavec_buf,
            xsavec_buf_len);
    }

    qemu_vfree(xsavec_buf);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to get virtual processor context, hr=%08lx",
                     hr);
    }

    return 0;
}

static void whpx_set_legacy_fp_registers(CPUState *cpu, WHPXStateLevel level)
{
    struct whpx_state *whpx = &whpx_global;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    struct whpx_register_set vcxt;
    HRESULT hr;
    int idx = 0;
    int i;
    int idx_next;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    /* 16 XMM registers */
    assert(whpx_register_names_legacy_fp[idx] == WHvX64RegisterXmm0);
    idx_next = idx + 16;
    for (i = 0; i < sizeof(env->xmm_regs) / sizeof(ZMMReg); i += 1, idx += 1) {
        vcxt.values[idx].Reg128.Low64 = env->xmm_regs[i].ZMM_Q(0);
        vcxt.values[idx].Reg128.High64 = env->xmm_regs[i].ZMM_Q(1);
    }
    idx = idx_next;

    /* 8 FP registers */
    assert(whpx_register_names_legacy_fp[idx] == WHvX64RegisterFpMmx0);
    for (i = 0; i < 8; i += 1, idx += 1) {
        vcxt.values[idx].Fp.AsUINT128.Low64 = env->fpregs[i].mmx.MMX_Q(0);
        /* vcxt.values[idx].Fp.AsUINT128.High64 =
               env->fpregs[i].mmx.MMX_Q(1);
        */
    }

    /* FP control status register */
    assert(whpx_register_names_legacy_fp[idx] == WHvX64RegisterFpControlStatus);
    vcxt.values[idx].FpControlStatus.FpControl = env->fpuc;
    vcxt.values[idx].FpControlStatus.FpStatus =
        (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    vcxt.values[idx].FpControlStatus.FpTag = 0;
    for (i = 0; i < 8; ++i) {
        vcxt.values[idx].FpControlStatus.FpTag |= (!env->fptags[i]) << i;
    }
    vcxt.values[idx].FpControlStatus.Reserved = 0;
    vcxt.values[idx].FpControlStatus.LastFpOp = env->fpop;
    vcxt.values[idx].FpControlStatus.LastFpRip = env->fpip;
    idx += 1;

    /* XMM control status register */
    assert(whpx_register_names_legacy_fp[idx] == WHvX64RegisterXmmControlStatus);
    vcxt.values[idx].XmmControlStatus.LastFpRdp = 0;
    vcxt.values[idx].XmmControlStatus.XmmStatusControl = env->mxcsr;
    vcxt.values[idx].XmmControlStatus.XmmStatusControlMask = 0x0000ffff;
    idx += 1;

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index,
        whpx_register_names_legacy_fp,
        idx,
        &vcxt.values[0]);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set virtual processor context, hr=%08lx",
                     hr);
    }
}

void whpx_set_registers(CPUState *cpu, WHPXStateLevel level)
{
    struct whpx_state *whpx = &whpx_global;
    AccelCPUState *vcpu = cpu->accel;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    struct whpx_register_set vcxt;
    HRESULT hr;
    int idx;
    int idx_next;
    int i;
    int v86, r86;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    /*
     * Following MSRs have side effects on the guest or are too heavy for
     * runtime. Limit them to full state update.
     */
    if (level >= WHPX_LEVEL_RESET_STATE) {
        whpx_set_tsc(cpu);
    }

    memset(&vcxt, 0, sizeof(struct whpx_register_set));

    v86 = (env->eflags & VM_MASK);
    r86 = !(env->cr[0] & CR0_PE_MASK);

    vcpu->tpr = cpu_get_apic_tpr(x86_cpu->apic_state);

    idx = 0;

    /* Indexes for first 16 registers match between HV and QEMU definitions */
    idx_next = 16;
    for (idx = 0; idx < CPU_NB_REGS; idx += 1) {
        vcxt.values[idx].Reg64 = (uint64_t)env->regs[idx];
    }
    idx = idx_next;

    /* Same goes for RIP and RFLAGS */
    assert(whpx_register_names[idx] == WHvX64RegisterRip);
    vcxt.values[idx++].Reg64 = env->eip;

    assert(whpx_register_names[idx] == WHvX64RegisterRflags);
    lflags_to_rflags(env);
    vcxt.values[idx++].Reg64 = env->eflags;
    assert(idx == WHvX64RegisterEs);

    if (level > WHPX_LEVEL_FAST_RUNTIME_STATE) {

        /* Translate 6+4 segment registers. HV and QEMU order matches  */
        for (i = 0; i < 6; i += 1, idx += 1) {
            vcxt.values[idx].Segment = whpx_seg_q2h(&env->segs[i], v86, r86);
        }

        assert(idx == WHvX64RegisterLdtr);
        /*
         * Skip those registers for synchronisation after MMIO accesses
         * as they're not going to be modified in that case.
         */

        vcxt.values[idx++].Segment = whpx_seg_q2h(&env->ldt, 0, 0);

        assert(idx == WHvX64RegisterTr);
        vcxt.values[idx++].Segment = whpx_seg_q2h(&env->tr, 0, 0);

        assert(idx == WHvX64RegisterIdtr);
        vcxt.values[idx].Table.Base = env->idt.base;
        vcxt.values[idx].Table.Limit = env->idt.limit;
        idx += 1;

        assert(idx == WHvX64RegisterGdtr);
        vcxt.values[idx].Table.Base = env->gdt.base;
        vcxt.values[idx].Table.Limit = env->gdt.limit;
        idx += 1;

        /* CR0, 2, 3, 4, 8 */
        assert(whpx_register_names[idx] == WHvX64RegisterCr0);
        vcxt.values[idx++].Reg64 = env->cr[0];
        assert(whpx_register_names[idx] == WHvX64RegisterCr2);
        vcxt.values[idx++].Reg64 = env->cr[2];
        assert(whpx_register_names[idx] == WHvX64RegisterCr3);
        vcxt.values[idx++].Reg64 = env->cr[3];
        assert(whpx_register_names[idx] == WHvX64RegisterCr4);
        vcxt.values[idx++].Reg64 = env->cr[4];
        /* For kernel-irqchip=on, TPR is managed as part of APIC state */
        if (!whpx_irqchip_in_kernel()) {
            WHV_REGISTER_VALUE cr8 = {.Reg64 = vcpu->tpr};
            whpx_set_reg(cpu, WHvX64RegisterCr8, cr8);
        }

        /* 8 Debug Registers - Skipped */

        /*
         * Extended control registers needs to be handled separately depending
         * on whether xsave is supported/enabled or not.
         */
        whpx_set_xcrs(cpu);

        if (whpx_is_xsave_enabled(cpu)) {
            whpx_set_xsave_state(cpu);
        } else {
            whpx_set_legacy_fp_registers(cpu, level);
        }
        /* MSRs */
        assert(whpx_register_names[idx] == WHvX64RegisterEfer);
        vcxt.values[idx++].Reg64 = env->efer;
#ifdef TARGET_X86_64
        assert(whpx_register_names[idx] == WHvX64RegisterKernelGsBase);
        vcxt.values[idx++].Reg64 = env->kernelgsbase;
#endif

        /* WHvX64RegisterPat - Skipped */

        assert(whpx_register_names[idx] == WHvX64RegisterSysenterCs);
        vcxt.values[idx++].Reg64 = env->sysenter_cs;
        assert(whpx_register_names[idx] == WHvX64RegisterSysenterEip);
        vcxt.values[idx++].Reg64 = env->sysenter_eip;
        assert(whpx_register_names[idx] == WHvX64RegisterSysenterEsp);
        vcxt.values[idx++].Reg64 = env->sysenter_esp;
        assert(whpx_register_names[idx] == WHvX64RegisterStar);
        vcxt.values[idx++].Reg64 = env->star;
#ifdef TARGET_X86_64
        assert(whpx_register_names[idx] == WHvX64RegisterLstar);
        vcxt.values[idx++].Reg64 = env->lstar;
        assert(whpx_register_names[idx] == WHvX64RegisterCstar);
        vcxt.values[idx++].Reg64 = env->cstar;
        assert(whpx_register_names[idx] == WHvX64RegisterSfmask);
        vcxt.values[idx++].Reg64 = env->fmask;
#endif

        /* Interrupt / Event Registers - Skipped */

        assert(idx == RTL_NUMBER_OF(whpx_register_names));
    }

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index,
        whpx_register_names,
        idx,
        &vcxt.values[0]);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set virtual processor context, hr=%08lx",
                     hr);
    }

    if (level >= WHPX_LEVEL_FULL_STATE) {
        WHV_REGISTER_VALUE apic_base = {};
        apic_base.Reg64 = cpu_get_apic_base(X86_CPU(cpu)->apic_state);
        whpx_set_reg(cpu, WHvX64RegisterApicBase, apic_base);
    }
}

static int whpx_get_tsc(CPUState *cpu)
{
    WHV_REGISTER_NAME tsc_reg = WHvX64RegisterTsc;
    WHV_REGISTER_VALUE tsc_val;
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index, &tsc_reg, 1, &tsc_val);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to get TSC, hr=%08lx", hr);
        return -1;
    }

    cpu_env(cpu)->tsc = tsc_val.Reg64;
    return 0;
}

/* X64 Extended Control Registers */
static void whpx_get_xcrs(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    WHV_REGISTER_VALUE xcr0;
    WHV_REGISTER_NAME xcr0_name = WHvX64RegisterXCr0;

    if (!whpx_has_xsave()) {
        return;
    }

    /* Only xcr0 is supported by the hypervisor currently */
    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index, &xcr0_name, 1, &xcr0);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to get register xcr0, hr=%08lx", hr);
        return;
    }

    cpu_env(cpu)->xcr0 = xcr0.Reg64;
}

static void whpx_get_registers_for_vmexit(CPUState *cpu, WHPXStateLevel level)
{
    struct whpx_state *whpx = &whpx_global;
    AccelCPUState *vcpu = cpu->accel;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    struct whpx_register_set vcxt;
    HRESULT hr;
    int idx;
    int idx_next;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index,
        whpx_register_names_for_vmexit,
        RTL_NUMBER_OF(whpx_register_names_for_vmexit),
        &vcxt.values[0]);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to get virtual processor context, hr=%08lx",
                     hr);
    }

    idx = 0;

    /* Indexes for first 16 registers match between HV and QEMU definitions */
    idx_next = 16;
    for (idx = 0; idx < CPU_NB_REGS; idx += 1) {
        env->regs[idx] = vcxt.values[idx].Reg64;
    }
    idx = idx_next;

    env->eip = vcpu->exit_ctx.VpContext.Rip;
    env->eflags = vcpu->exit_ctx.VpContext.Rflags;
    rflags_to_lflags(env);

    assert(idx == RTL_NUMBER_OF(whpx_register_names_for_vmexit));

    x86_update_hflags(env);
}

static void whpx_get_legacy_fp_registers(CPUState *cpu, WHPXStateLevel level)
{
    struct whpx_state *whpx = &whpx_global;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    struct whpx_register_set vcxt;
    HRESULT hr;
    int i;
    int idx;
    int idx_next;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index,
        whpx_register_names_legacy_fp,
        RTL_NUMBER_OF(whpx_register_names_legacy_fp),
        &vcxt.values[0]);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to get virtual processor context, hr=%08lx",
                     hr);
    }

    idx = 0;
    /* 16 XMM registers */
    assert(whpx_register_names_legacy_fp[idx] == WHvX64RegisterXmm0);
    idx_next = idx + 16;
    for (i = 0; i < sizeof(env->xmm_regs) / sizeof(ZMMReg); i += 1, idx += 1) {
        env->xmm_regs[i].ZMM_Q(0) = vcxt.values[idx].Reg128.Low64;
        env->xmm_regs[i].ZMM_Q(1) = vcxt.values[idx].Reg128.High64;
    }
    idx = idx_next;

    /* 8 FP registers */
    assert(whpx_register_names_legacy_fp[idx] == WHvX64RegisterFpMmx0);
    for (i = 0; i < 8; i += 1, idx += 1) {
        env->fpregs[i].mmx.MMX_Q(0) = vcxt.values[idx].Fp.AsUINT128.Low64;
        /* env->fpregs[i].mmx.MMX_Q(1) =
               vcxt.values[idx].Fp.AsUINT128.High64;
        */
    }

    /* FP control status register */
    assert(whpx_register_names_legacy_fp[idx] == WHvX64RegisterFpControlStatus);
    env->fpuc = vcxt.values[idx].FpControlStatus.FpControl;
    env->fpstt = (vcxt.values[idx].FpControlStatus.FpStatus >> 11) & 0x7;
    env->fpus = vcxt.values[idx].FpControlStatus.FpStatus & ~0x3800;
    for (i = 0; i < 8; ++i) {
        env->fptags[i] = !((vcxt.values[idx].FpControlStatus.FpTag >> i) & 1);
    }
    env->fpop = vcxt.values[idx].FpControlStatus.LastFpOp;
    env->fpip = vcxt.values[idx].FpControlStatus.LastFpRip;
    idx += 1;

    /* XMM control status register */
    assert(whpx_register_names_legacy_fp[idx] == WHvX64RegisterXmmControlStatus);
    env->mxcsr = vcxt.values[idx].XmmControlStatus.XmmStatusControl;
    idx += 1;
}

static int whpx_get_xsave_state(CPUState *cpu)
{
    struct whpx_state *whpx = &whpx_global;
    X86CPU *x86cpu = X86_CPU(cpu);
    CPUX86State *env = &x86cpu->env;
    int ret;
    HRESULT hr;
    void *xsavec_buf;
    const size_t page = qemu_real_host_page_size();
    size_t xsavec_buf_len = whpx_get_xsave_max_len();
    UINT32 bytes_written;

    xsavec_buf = qemu_memalign(page, xsavec_buf_len);
    memset(xsavec_buf, 0, xsavec_buf_len);

    if (!whpx_is_legacy_os()) {
        hr = whp_dispatch.WHvGetVirtualProcessorState(
            whpx->partition, cpu->cpu_index,
            WHvVirtualProcessorStateTypeXsaveState,
            xsavec_buf,
            xsavec_buf_len, &bytes_written);
    } else {
        hr = whp_dispatch.WHvGetVirtualProcessorXsaveState(
            whpx->partition, cpu->cpu_index,
            xsavec_buf,
            xsavec_buf_len, &bytes_written);
    }
    if (FAILED(hr) || bytes_written == 0) {
        error_report("failed to get xsave state: %s", strerror(errno));
        return -errno;
    }

    ret = decompact_xsave_area(xsavec_buf, xsavec_buf_len, env);
    qemu_vfree(xsavec_buf);
    if (ret < 0) {
        error_report("failed to decompact xsave area");
        return ret;
    }
    x86_cpu_xrstor_all_areas(x86cpu, env->xsave_buf, env->xsave_buf_len);

    return 0;
}

void whpx_get_registers(CPUState *cpu, WHPXStateLevel level)
{
    struct whpx_state *whpx = &whpx_global;
    AccelCPUState *vcpu = cpu->accel;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    struct whpx_register_set vcxt;
    uint64_t tpr;
    HRESULT hr;
    int idx;
    int idx_next;
    int i;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    if (level == WHPX_LEVEL_FAST_RUNTIME_STATE) {
        return whpx_get_registers_for_vmexit(cpu, level);
    }

    if (!env->tsc_valid) {
        whpx_get_tsc(cpu);
        env->tsc_valid = !runstate_is_running();
    }

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index,
        whpx_register_names,
        RTL_NUMBER_OF(whpx_register_names),
        &vcxt.values[0]);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to get virtual processor context, hr=%08lx",
                     hr);
    }

    idx = 0;

    /* Indexes for first 16 registers match between HV and QEMU definitions */
    idx_next = 16;
    for (idx = 0; idx < CPU_NB_REGS; idx += 1) {
        env->regs[idx] = vcxt.values[idx].Reg64;
    }
    idx = idx_next;

    /* Same goes for RIP and RFLAGS */
    assert(whpx_register_names[idx] == WHvX64RegisterRip);
    env->eip = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterRflags);
    env->eflags = vcxt.values[idx++].Reg64;
    rflags_to_lflags(env);

    /* Translate 6+4 segment registers. HV and QEMU order matches  */
    assert(idx == WHvX64RegisterEs);
    for (i = 0; i < 6; i += 1, idx += 1) {
        env->segs[i] = whpx_seg_h2q(&vcxt.values[idx].Segment);
    }

    assert(idx == WHvX64RegisterLdtr);
    env->ldt = whpx_seg_h2q(&vcxt.values[idx++].Segment);
    assert(idx == WHvX64RegisterTr);
    env->tr = whpx_seg_h2q(&vcxt.values[idx++].Segment);
    assert(idx == WHvX64RegisterIdtr);
    env->idt.base = vcxt.values[idx].Table.Base;
    env->idt.limit = vcxt.values[idx].Table.Limit;
    idx += 1;
    assert(idx == WHvX64RegisterGdtr);
    env->gdt.base = vcxt.values[idx].Table.Base;
    env->gdt.limit = vcxt.values[idx].Table.Limit;
    idx += 1;

    /* CR0, 2, 3, 4, 8 */
    assert(whpx_register_names[idx] == WHvX64RegisterCr0);
    env->cr[0] = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterCr2);
    env->cr[2] = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterCr3);
    env->cr[3] = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterCr4);
    env->cr[4] = vcxt.values[idx++].Reg64;

    /* For kernel-irqchip=on, TPR is managed as part of APIC state */
    if (!whpx_irqchip_in_kernel()) {
        tpr = vcpu->exit_ctx.VpContext.Cr8;
        if (tpr != vcpu->tpr) {
            vcpu->tpr = tpr;
            cpu_set_apic_tpr(x86_cpu->apic_state, tpr);
        }
    }

    /* 8 Debug Registers - Skipped */

    /*
     * Extended control registers needs to be handled separately depending
     * on whether xsave is supported/enabled or not.
     */
    whpx_get_xcrs(cpu);

    if (whpx_is_xsave_enabled(cpu)) {
        whpx_get_xsave_state(cpu);
    } else {
        whpx_get_legacy_fp_registers(cpu, level);
    }

    /* MSRs */
    assert(whpx_register_names[idx] == WHvX64RegisterEfer);
    env->efer = vcxt.values[idx++].Reg64;
#ifdef TARGET_X86_64
    assert(whpx_register_names[idx] == WHvX64RegisterKernelGsBase);
    env->kernelgsbase = vcxt.values[idx++].Reg64;
#endif

    /* WHvX64RegisterPat - Skipped */

    assert(whpx_register_names[idx] == WHvX64RegisterSysenterCs);
    env->sysenter_cs = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterSysenterEip);
    env->sysenter_eip = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterSysenterEsp);
    env->sysenter_esp = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterStar);
    env->star = vcxt.values[idx++].Reg64;
#ifdef TARGET_X86_64
    assert(whpx_register_names[idx] == WHvX64RegisterLstar);
    env->lstar = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterCstar);
    env->cstar = vcxt.values[idx++].Reg64;
    assert(whpx_register_names[idx] == WHvX64RegisterSfmask);
    env->fmask = vcxt.values[idx++].Reg64;
#endif

    /* Interrupt / Event Registers - Skipped */

    assert(idx == RTL_NUMBER_OF(whpx_register_names));

    if (whpx_irqchip_in_kernel()) {
        whpx_apic_get(x86_cpu->apic_state);
    }

    x86_update_hflags(env);
}

static int emulate_instruction(CPUState *cpu, const uint8_t *insn_bytes, size_t insn_len)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    struct x86_decode decode = { 0 };
    x86_insn_stream stream = { .bytes = insn_bytes, .len = insn_len };

    whpx_get_registers(cpu, WHPX_LEVEL_FAST_RUNTIME_STATE);
    decode_instruction_stream(env, &decode, &stream);
    exec_instruction(env, &decode);
    whpx_set_registers(cpu, WHPX_LEVEL_FAST_RUNTIME_STATE);

    return 0;
}

static int emulate_msr_instruction(CPUState *cpu,
            const uint8_t *insn_bytes, size_t insn_len)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    struct x86_decode decode = { 0 };
    x86_insn_stream stream = { .bytes = insn_bytes, .len = insn_len };

    whpx_get_registers(cpu, WHPX_LEVEL_FAST_RUNTIME_STATE);
    decode_instruction_stream(env, &decode, &stream);

    if (decode.cmd != X86_DECODE_CMD_RDMSR
        && decode.cmd != X86_DECODE_CMD_WRMSR) {
        return 1;
    }

    exec_instruction(env, &decode);
    whpx_set_registers(cpu, WHPX_LEVEL_FAST_RUNTIME_STATE);
    return 0;
}

static int whpx_handle_mmio(CPUState *cpu, WHV_RUN_VP_EXIT_CONTEXT *exit_ctx)
{
    WHV_MEMORY_ACCESS_CONTEXT *ctx = &exit_ctx->MemoryAccess;
    int ret;

    ret = emulate_instruction(cpu, ctx->InstructionBytes, ctx->InstructionByteCount);
    if (ret < 0) {
        error_report("failed to emulate mmio");
        return -1;
    }

    return 0;
}

static int whpx_handle_msr_from_gpf(CPUState *cpu)
{
    WHV_VP_EXCEPTION_CONTEXT *ctx = &cpu->accel->exit_ctx.VpException;
    int ret;

    ret = emulate_msr_instruction(cpu, ctx->InstructionBytes, ctx->InstructionByteCount);
    if (ret == 1) {
        /* Not an MSR instruction */
        return 1;
    }

    return 0;
}

static void whpx_inject_back_gpf(CPUState *cpu)
{
    WHV_VP_EXCEPTION_CONTEXT *ctx = &cpu->accel->exit_ctx.VpException;
    WHV_REGISTER_VALUE reg = {};

    if (ctx->ExceptionInfo.SoftwareException) {
        /* TODO */
        warn_report("Was asked to inject software exception.");
        return;
    }

    if (ctx->ExceptionType != EXCP0D_GPF) {
        warn_report("Was asked to inject exception other than GPF.");
        return;
    }

    reg.ExceptionEvent.EventPending = 1;
    reg.ExceptionEvent.EventType = WHvX64PendingEventException;
    reg.ExceptionEvent.DeliverErrorCode = ctx->ExceptionInfo.ErrorCodeValid;
    reg.ExceptionEvent.Vector = ctx->ExceptionType;
    reg.ExceptionEvent.ErrorCode = ctx->ErrorCode;
    reg.ExceptionEvent.ExceptionParameter = ctx->ExceptionParameter;
    whpx_set_reg(cpu, WHvRegisterPendingEvent, reg);
}

static void handle_io(CPUState *env, uint16_t port, void *buffer,
                  int direction, int size, int count)
{
    int i;
    uint8_t *ptr = buffer;

    for (i = 0; i < count; i++) {
        address_space_rw(&address_space_io, port, MEMTXATTRS_UNSPECIFIED,
                         ptr, size,
                         direction);
        ptr += size;
    }
}

static void whpx_bump_rip(CPUState *cpu, WHV_RUN_VP_EXIT_CONTEXT *exit_ctx)
{
    WHV_REGISTER_VALUE reg;
    reg.Reg64 = exit_ctx->VpContext.Rip + exit_ctx->VpContext.InstructionLength;
    whpx_set_reg(cpu, WHvX64RegisterRip, reg);
}

static int whpx_handle_portio(CPUState *cpu,
                              WHV_RUN_VP_EXIT_CONTEXT *exit_ctx)
{
    WHV_X64_IO_PORT_ACCESS_CONTEXT *ctx = &exit_ctx->IoPortAccess;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    int ret;

    if (!ctx->AccessInfo.StringOp && !ctx->AccessInfo.IsWrite) {
        uint64_t val = 0;
        WHV_REGISTER_VALUE reg;

        whpx_get_reg(cpu, WHvX64RegisterRax, &reg);
        handle_io(cpu, ctx->PortNumber, &val, 0, ctx->AccessInfo.AccessSize, 1);
        if (ctx->AccessInfo.AccessSize == 1) {
            reg.Reg8 = val;
        } else if (ctx->AccessInfo.AccessSize == 2) {
            reg.Reg16 = val;
        } else if (ctx->AccessInfo.AccessSize == 4) {
            reg.Reg64 = (uint32_t)val;
        } else {
            reg.Reg64 = (uint64_t)val;
        }
        /* vmport calls cpu_synchronize_state on an I/O port read */
        if (!cpu->vcpu_dirty) {
            whpx_bump_rip(cpu, exit_ctx);
            whpx_set_reg(cpu, WHvX64RegisterRax, reg);
        } else {
            env->eip = exit_ctx->VpContext.Rip + exit_ctx->VpContext.InstructionLength;
            env->regs[R_EAX] = reg.Reg64;
        }
        return 0;
    } else if (!ctx->AccessInfo.StringOp && ctx->AccessInfo.IsWrite) {
        RAX(env) = ctx->Rax;
        handle_io(cpu, ctx->PortNumber, &RAX(env), 1, ctx->AccessInfo.AccessSize, 1);
        if (!cpu->vcpu_dirty) {
            whpx_bump_rip(cpu, exit_ctx);
        } else {
            env->eip = exit_ctx->VpContext.Rip + exit_ctx->VpContext.InstructionLength;
        }
        return 0;
    }

    ret = emulate_instruction(cpu, ctx->InstructionBytes, exit_ctx->VpContext.InstructionLength);
    if (ret < 0) {
        error_report("failed to emulate I/O port access");
        return -1;
    }

    return 0;
}

static void whpx_segment_to_x86_descriptor(CPUState *cpu, WHV_X64_SEGMENT_REGISTER* reg,
                                   struct x86_segment_descriptor *desc)
{
    uint32_t limit;
    desc->g = reg->Granularity;

    /*
     * Hyper-V can return reg->Granularity == 0
     * with a higher limit than 0xfffff.
     *
     * Detect that case and set desc->g
     * with shifting the limit properly.
     */
    if (!desc->g && reg->Limit <= 0xfffff) {
        limit = reg->Limit;
    } else {
        limit = (reg->Limit >> 12);
        desc->g = 1;
    }

    x86_set_segment_limit(desc, limit);
    x86_set_segment_base(desc, reg->Base);

    desc->type = reg->SegmentType;
    desc->s = reg->NonSystemSegment;
    desc->dpl = reg->DescriptorPrivilegeLevel;
    desc->p = reg->Present;
    desc->avl = reg->Available;
    desc->l = reg->Long;
    desc->db = reg->Default;
}

static void whpx_read_segment_descriptor(CPUState *cpu, WHV_X64_SEGMENT_REGISTER* reg,
                                    X86Seg seg)
{
    AccelCPUState *vcpu = cpu->accel;
    WHV_REGISTER_NAME reg_name = WHvX64RegisterEs + seg;
    WHV_REGISTER_VALUE val;

    if (seg == R_CS) {
        *reg = vcpu->exit_ctx.VpContext.Cs;
        return;
    }
    if (vcpu->exit_ctx.ExitReason == WHvRunVpExitReasonX64IoPortAccess) {
        if (seg == R_DS) {
            *reg = vcpu->exit_ctx.IoPortAccess.Ds;
            return;
        } else if (seg == R_ES) {
            *reg = vcpu->exit_ctx.IoPortAccess.Es;
            return;
        }
    }

    whpx_get_reg(cpu, reg_name, &val);
    *reg = val.Segment;
}

static void read_segment_descriptor(CPUState *cpu,
                                    struct x86_segment_descriptor *desc,
                                    enum X86Seg seg_idx)
{
    WHV_X64_SEGMENT_REGISTER reg;
    whpx_read_segment_descriptor(cpu, &reg, seg_idx);
    whpx_segment_to_x86_descriptor(cpu, &reg, desc);
}

static bool is_protected_mode(CPUState *cpu)
{
    AccelCPUState *vcpu = cpu->accel;

    return vcpu->exit_ctx.VpContext.ExecutionState.Cr0Pe == 1;
}

static bool is_long_mode(CPUState *cpu)
{
    AccelCPUState *vcpu = cpu->accel;

    return vcpu->exit_ctx.VpContext.ExecutionState.EferLma == 1;
}

static bool is_user_mode(CPUState *cpu)
{
    AccelCPUState *vcpu = cpu->accel;
    return vcpu->exit_ctx.VpContext.ExecutionState.Cpl == 3;
}

static target_ulong read_cr(CPUState *cpu, int cr)
{
    WHV_REGISTER_NAME whv_cr;
    WHV_REGISTER_VALUE val;

    switch (cr) {
    case 0:
        whv_cr = WHvX64RegisterCr0;
        break;
    case 2:
        whv_cr = WHvX64RegisterCr2;
        break;
    case 3:
        whv_cr = WHvX64RegisterCr3;
        break;
    case 4:
        whv_cr = WHvX64RegisterCr4;
        break;
    case 8:
        whv_cr = WHvX64RegisterCr8;
        break;
    default:
        abort();
    }
    whpx_get_reg(cpu, whv_cr, &val);

    return val.Reg64;
}

static bool whpx_simulate_rdmsr(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint32_t msr = ECX(env);
    uint64_t val = 0;

    switch (msr) {
    default:
        error_report("WHPX: unknown msr 0x%x", msr);
        x86_emul_raise_exception(&X86_CPU(cpu)->env, EXCP0D_GPF, 0);
        return 1;
        break;
    }

    RAX(env) = (uint32_t)val;
    RDX(env) = (uint32_t)(val >> 32);

    return 0;
}

static bool whpx_simulate_wrmsr(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint32_t msr = ECX(env);
    uint64_t data = ((uint64_t)EDX(env) << 32) | EAX(env);

    switch (msr) {
    default:
        error_report("WHPX: unknown msr 0x%x val %llx", msr, data);
        x86_emul_raise_exception(&X86_CPU(cpu)->env, EXCP0D_GPF, 0);
        return 1;
        break;
    }

    return 0;
}

static const struct x86_emul_ops whpx_x86_emul_ops = {
    .read_segment_descriptor = read_segment_descriptor,
    .handle_io = handle_io,
    .is_protected_mode = is_protected_mode,
    .is_long_mode = is_long_mode,
    .is_user_mode = is_user_mode,
    .read_cr = read_cr,
    .simulate_rdmsr = whpx_simulate_rdmsr,
    .simulate_wrmsr = whpx_simulate_wrmsr
};

static void whpx_init_emu(void)
{
    init_decoder();
    init_emu(&whpx_x86_emul_ops);
}

bool whpx_is_legacy_os(void)
{
    return !is_modern_os;
}

uint32_t whpx_get_supported_cpuid(uint32_t func, uint32_t idx, int reg)
{
    WHV_CPUID_OUTPUT output = {};
    uint32_t eax, ebx, ecx, edx;
    uint32_t cpu_index = 0;
    bool temp_cpu = true;
    HRESULT hr;

    /* Legacy OSes don't have WHvGetVirtualProcessorCpuidOutput */
    if (whpx_is_legacy_os()) {
        return whpx_get_supported_cpuid_legacy(func, idx, reg);
    }

    hr = whp_dispatch.WHvCreateVirtualProcessor(
        whpx_global.partition, cpu_index, 0);

    /* This means that the CPU already exists... */
    if (FAILED(hr)) {
        temp_cpu = false;
    }

    hr = whp_dispatch.WHvGetVirtualProcessorCpuidOutput(whpx_global.partition,
        cpu_index, func, idx, &output);

    if (FAILED(hr)) {
        abort();
    }

    if (temp_cpu) {
        hr = whp_dispatch.WHvDeleteVirtualProcessor(whpx_global.partition, cpu_index);
        if (FAILED(hr)) {
            abort();
        }
    }

    eax = output.Eax;
    ebx = output.Ebx;
    ecx = output.Ecx;
    edx = output.Edx;

    /*
     * We can emulate X2APIC even for the kernel-irqchip=off case.
     * CPUID_EXT_HYPERVISOR and CPUID_HT should be considered present
     * always, so report them as unconditionally supported here.
     */
    if (func == 1) {
        ecx |= CPUID_EXT_X2APIC;
        ecx |= CPUID_EXT_HYPERVISOR;
        edx |= CPUID_HT;
    }

    switch (reg) {
    case R_EAX:
        return eax;
    case R_EBX:
        return ebx;
    case R_ECX:
        return ecx;
    case R_EDX:
        return edx;
    default:
        return 0;
    }
}

uint64_t whpx_get_supported_msr_feature(uint32_t index)
{
    WHV_CAPABILITY_CODE cap;
    uint64_t val = 0;

    switch (index) {
    case MSR_IA32_VMX_BASIC:
        cap = WHvCapabilityCodeVmxBasic;
        break;
    case MSR_IA32_VMX_MISC:
        cap = WHvCapabilityCodeVmxMisc;
        break;
    case MSR_IA32_VMX_CR0_FIXED0:
        cap = WHvCapabilityCodeVmxCr0Fixed0;
        break;
    case MSR_IA32_VMX_CR0_FIXED1:
        cap = WHvCapabilityCodeVmxCr0Fixed1;
        break;
    case MSR_IA32_VMX_CR4_FIXED0:
        cap = WHvCapabilityCodeVmxCr4Fixed0;
        break;
    case MSR_IA32_VMX_CR4_FIXED1:
        cap = WHvCapabilityCodeVmxCr4Fixed1;
        break;
    case MSR_IA32_VMX_VMCS_ENUM:
        cap = WHvCapabilityCodeVmxVmcsEnum;
        break;
    case MSR_IA32_VMX_PROCBASED_CTLS2:
        cap = WHvCapabilityCodeVmxProcbasedCtls2;
        break;
    case MSR_IA32_VMX_EPT_VPID_CAP:
        cap = WHvCapabilityCodeVmxEptVpidCap;
        break;
    case MSR_IA32_VMX_TRUE_PINBASED_CTLS:
        cap = WHvCapabilityCodeVmxPinbasedCtls;
        break;
    case MSR_IA32_VMX_TRUE_PROCBASED_CTLS:
        cap = WHvCapabilityCodeVmxProcbasedCtls;
        break;
    case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
        cap = WHvCapabilityCodeVmxTrueEntryCtls;
        break;
    case MSR_IA32_VMX_TRUE_EXIT_CTLS:
        cap = WHvCapabilityCodeVmxTrueExitCtls;
        break;
    default:
        cap = 0;
    }

    if (cap != 0) {
        HRESULT hr = whp_dispatch.WHvGetCapability(
            cap, &val, sizeof(val),
                NULL);
        if (FAILED(hr)) {
            return 0;
        }
        return val;
    }
    return 0;
}

static UINT64 whpx_get_default_exceptions(void)
{
    struct whpx_state *whpx = &whpx_global;
    UINT64 intercepts = 0;

    if (whpx->intercept_msr_gp) {
        intercepts |= 1UL << WHvX64ExceptionTypeGeneralProtectionFault;
    }

    return intercepts;
}

/*
 * Controls whether we should intercept various exceptions on the guest,
 * namely breakpoint/single-step events.
 *
 * The 'exceptions' argument accepts a bitmask, e.g:
 * (1 << WHvX64ExceptionTypeDebugTrapOrFault) | (...)
 */
HRESULT whpx_set_exception_exit_bitmap(UINT64 exceptions)
{
    struct whpx_state *whpx = &whpx_global;
    WHV_PARTITION_PROPERTY prop;
    HRESULT hr;

    if (exceptions == whpx->exception_exit_bitmap) {
        return S_OK;
    }

    /* Register for MSR and CPUID exits */
    memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
    prop.ExtendedVmExits.X64MsrExit = 1;
    prop.ExtendedVmExits.X64CpuidExit = 1;

    if (exceptions != 0 || whpx_get_default_exceptions() != 0) {
        prop.ExtendedVmExits.ExceptionExit = 1;
    }

    hr = whp_dispatch.WHvSetPartitionProperty(
            whpx->partition,
            WHvPartitionPropertyCodeExtendedVmExits,
            &prop,
            sizeof(WHV_PARTITION_PROPERTY));
    if (FAILED(hr)) {
        error_report("WHPX: Failed to enable extended VM exits, hr=%08lx", hr);
        return hr;
    }

    memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
    prop.ExceptionExitBitmap = exceptions | whpx_get_default_exceptions();

    hr = whp_dispatch.WHvSetPartitionProperty(
        whpx->partition,
        WHvPartitionPropertyCodeExceptionExitBitmap,
        &prop,
        sizeof(WHV_PARTITION_PROPERTY));

    if (SUCCEEDED(hr)) {
        whpx->exception_exit_bitmap = exceptions;
    } else {
        error_report("WHPX: Failed to set exception exit bitmap, hr=%08lx", hr);
    }

    return hr;
}


/*
 * This function is called before/after stepping over a single instruction.
 * It will update the CPU registers to arm/disarm the instruction stepping
 * accordingly.
 */
static HRESULT whpx_vcpu_configure_single_stepping(CPUState *cpu,
    bool set,
    uint64_t *exit_context_rflags)
{
    WHV_REGISTER_NAME reg_name;
    WHV_REGISTER_VALUE reg_value;
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;

    /*
     * If we are trying to step over a single instruction, we need to set the
     * TF bit in rflags. Otherwise, clear it.
     */
    reg_name = WHvX64RegisterRflags;
    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
        whpx->partition,
        cpu->cpu_index,
        &reg_name,
        1,
        &reg_value);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to get rflags, hr=%08lx", hr);
        return hr;
    }

    if (exit_context_rflags) {
        assert(*exit_context_rflags == reg_value.Reg64);
    }

    if (set) {
        /* Raise WHvX64ExceptionTypeDebugTrapOrFault after each instruction */
        reg_value.Reg64 |= TF_MASK;
    } else {
        reg_value.Reg64 &= ~TF_MASK;
    }

    if (exit_context_rflags) {
        *exit_context_rflags = reg_value.Reg64;
    }

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
        whpx->partition,
        cpu->cpu_index,
        &reg_name,
        1,
        &reg_value);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set rflags,"
            " hr=%08lx",
            hr);
        return hr;
    }

    reg_name = WHvRegisterInterruptState;
    reg_value.Reg64 = 0;

    /* Suspend delivery of hardware interrupts during single-stepping. */
    reg_value.InterruptState.InterruptShadow = set != 0;

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
    whpx->partition,
        cpu->cpu_index,
        &reg_name,
        1,
        &reg_value);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set InterruptState,"
            " hr=%08lx",
            hr);
        return hr;
    }

    if (!set) {
        /*
         * We have just finished stepping over a single instruction,
         * and intercepted the INT1 generated by it.
         * We need to now hide the INT1 from the guest,
         * as it would not be expecting it.
         */

        reg_name = WHvX64RegisterPendingDebugException;
        hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
        whpx->partition,
            cpu->cpu_index,
            &reg_name,
            1,
            &reg_value);

        if (FAILED(hr)) {
            error_report("WHPX: Failed to get pending debug exceptions,"
                         "hr=%08lx", hr);
            return hr;
        }

        if (reg_value.PendingDebugException.SingleStep) {
            reg_value.PendingDebugException.SingleStep = 0;

            hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
                whpx->partition,
                cpu->cpu_index,
                &reg_name,
                1,
                &reg_value);

            if (FAILED(hr)) {
                error_report("WHPX: Failed to clear pending debug exceptions,"
                             "hr=%08lx", hr);
             return hr;
            }
        }

    }

    return S_OK;
}

/*
 * Linux uses int3 (0xCC) during startup (see int3_selftest()) and for
 * debugging user-mode applications. Since the WHPX API does not offer
 * an easy way to pass the intercepted exception back to the guest, we
 * resort to using INT1 instead, and let the guest always handle INT3.
 */
static const uint8_t whpx_breakpoint_instruction = 0xF1;

/*
 * The WHPX QEMU backend implements breakpoints by writing the INT1
 * instruction into memory (ignoring the DRx registers). This raises a few
 * issues that need to be carefully handled:
 *
 * 1. Although unlikely, other parts of QEMU may set multiple breakpoints
 *    at the same location, and later remove them in arbitrary order.
 *    This should not cause memory corruption, and should only remove the
 *    physical breakpoint instruction when the last QEMU breakpoint is gone.
 *
 * 2. Writing arbitrary virtual memory may fail if it's not mapped to a valid
 *    physical location. Hence, physically adding/removing a breakpoint can
 *    theoretically fail at any time. We need to keep track of it.
 *
 * The function below rebuilds a list of low-level breakpoints (one per
 * address, tracking the original instruction and any errors) from the list of
 * high-level breakpoints (set via cpu_breakpoint_insert()).
 *
 * In order to optimize performance, this function stores the list of
 * high-level breakpoints (a.k.a. CPU breakpoints) used to compute the
 * low-level ones, so that it won't be re-invoked until these breakpoints
 * change.
 *
 * Note that this function decides which breakpoints should be inserted into,
 * memory, but doesn't actually do it. The memory accessing is done in
 * whpx_apply_breakpoints().
 */
void whpx_translate_cpu_breakpoints(
    struct whpx_breakpoints *breakpoints,
    CPUState *cpu,
    int cpu_breakpoint_count)
{
    CPUBreakpoint *bp;
    int cpu_bp_index = 0;

    breakpoints->original_addresses =
        g_renew(vaddr, breakpoints->original_addresses, cpu_breakpoint_count);

    breakpoints->original_address_count = cpu_breakpoint_count;

    int max_breakpoints = cpu_breakpoint_count +
        (breakpoints->breakpoints ? breakpoints->breakpoints->used : 0);

    struct whpx_breakpoint_collection *new_breakpoints =
        g_malloc0(sizeof(struct whpx_breakpoint_collection)
                  + max_breakpoints * sizeof(struct whpx_breakpoint));

    new_breakpoints->allocated = max_breakpoints;
    new_breakpoints->used = 0;

    /*
     * 1. Preserve all old breakpoints that could not be automatically
     * cleared when the CPU got stopped.
     */
    if (breakpoints->breakpoints) {
        int i;
        for (i = 0; i < breakpoints->breakpoints->used; i++) {
            if (breakpoints->breakpoints->data[i].state != WHPX_BP_CLEARED) {
                new_breakpoints->data[new_breakpoints->used++] =
                    breakpoints->breakpoints->data[i];
            }
        }
    }

    /* 2. Map all CPU breakpoints to WHPX breakpoints */
    QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
        int i;
        bool found = false;

        /* This will be used to detect changed CPU breakpoints later. */
        breakpoints->original_addresses[cpu_bp_index++] = bp->pc;

        for (i = 0; i < new_breakpoints->used; i++) {
            /*
             * WARNING: This loop has O(N^2) complexity, where N is the
             * number of breakpoints. It should not be a bottleneck in
             * real-world scenarios, since it only needs to run once after
             * the breakpoints have been modified.
             * If this ever becomes a concern, it can be optimized by storing
             * high-level breakpoint objects in a tree or hash map.
             */

            if (new_breakpoints->data[i].address == bp->pc) {
                /* There was already a breakpoint at this address. */
                if (new_breakpoints->data[i].state == WHPX_BP_CLEAR_PENDING) {
                    new_breakpoints->data[i].state = WHPX_BP_SET;
                } else if (new_breakpoints->data[i].state == WHPX_BP_SET) {
                    new_breakpoints->data[i].state = WHPX_BP_SET_PENDING;
                }

                found = true;
                break;
            }
        }

        if (!found && new_breakpoints->used < new_breakpoints->allocated) {
            /* No WHPX breakpoint at this address. Create one. */
            new_breakpoints->data[new_breakpoints->used].address = bp->pc;
            new_breakpoints->data[new_breakpoints->used].state =
                WHPX_BP_SET_PENDING;
            new_breakpoints->used++;
        }
    }

    /*
     * Free the previous breakpoint list. This can be optimized by keeping
     * it as shadow buffer for the next computation instead of freeing
     * it immediately.
     */
    g_free(breakpoints->breakpoints);

    breakpoints->breakpoints = new_breakpoints;
}

/*
 * Physically inserts/removes the breakpoints by reading and writing the
 * physical memory, keeping a track of the failed attempts.
 *
 * Passing resuming=true  will try to set all previously unset breakpoints.
 * Passing resuming=false will remove all inserted ones.
 */
void whpx_apply_breakpoints(
    struct whpx_breakpoint_collection *breakpoints,
    CPUState *cpu,
    bool resuming)
{
    int i, rc;
    if (!breakpoints) {
        return;
    }

    for (i = 0; i < breakpoints->used; i++) {
        /* Decide what to do right now based on the last known state. */
        WhpxBreakpointState state = breakpoints->data[i].state;
        switch (state) {
        case WHPX_BP_CLEARED:
            if (resuming) {
                state = WHPX_BP_SET_PENDING;
            }
            break;
        case WHPX_BP_SET_PENDING:
            if (!resuming) {
                state = WHPX_BP_CLEARED;
            }
            break;
        case WHPX_BP_SET:
            if (!resuming) {
                state = WHPX_BP_CLEAR_PENDING;
            }
            break;
        case WHPX_BP_CLEAR_PENDING:
            if (resuming) {
                state = WHPX_BP_SET;
            }
            break;
        }

        if (state == WHPX_BP_SET_PENDING) {
            /* Remember the original instruction. */
            rc = cpu_memory_rw_debug(cpu,
                breakpoints->data[i].address,
                &breakpoints->data[i].original_instruction,
                1,
                false);

            if (!rc) {
                /* Write the breakpoint instruction. */
                rc = cpu_memory_rw_debug(cpu,
                    breakpoints->data[i].address,
                    (void *)&whpx_breakpoint_instruction,
                    1,
                    true);
            }

            if (!rc) {
                state = WHPX_BP_SET;
            }

        }

        if (state == WHPX_BP_CLEAR_PENDING) {
            /* Restore the original instruction. */
            rc = cpu_memory_rw_debug(cpu,
                breakpoints->data[i].address,
                &breakpoints->data[i].original_instruction,
                1,
                true);

            if (!rc) {
                state = WHPX_BP_CLEARED;
            }
        }

        breakpoints->data[i].state = state;
    }
}

bool whpx_arch_supports_guest_debug(void) 
{
    return true;
}

void whpx_arch_destroy_vcpu(CPUState *cpu)
{
    X86CPU *x86cpu = X86_CPU(cpu);
    CPUX86State *env = &x86cpu->env;
    g_free(env->emu_mmio_buf);
    qemu_vfree(env->xsave_buf);
    env->xsave_buf = NULL;
    env->xsave_buf_len = 0;
}

/* Returns the address of the next instruction that is about to be executed. */
static vaddr whpx_vcpu_get_pc(CPUState *cpu, bool exit_context_valid)
{
    if (cpu->vcpu_dirty) {
        /* The CPU registers have been modified by other parts of QEMU. */
        return cpu_env(cpu)->eip;
    } else if (exit_context_valid) {
        /*
         * The CPU registers have not been modified by neither other parts
         * of QEMU, nor this port by calling WHvSetVirtualProcessorRegisters().
         * This is the most common case.
         */
        AccelCPUState *vcpu = cpu->accel;
        return vcpu->exit_ctx.VpContext.Rip;
    } else {
        /*
         * The CPU registers have been modified by a call to
         * WHvSetVirtualProcessorRegisters() and must be re-queried from
         * the target.
         */
        WHV_REGISTER_VALUE reg_value;
        WHV_REGISTER_NAME reg_name = WHvX64RegisterRip;
        HRESULT hr;
        struct whpx_state *whpx = &whpx_global;

        hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
            whpx->partition,
            cpu->cpu_index,
            &reg_name,
            1,
            &reg_value);

        if (FAILED(hr)) {
            error_report("WHPX: Failed to get PC, hr=%08lx", hr);
            return 0;
        }

        return reg_value.Reg64;
    }
}

static int whpx_handle_halt(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    int ret = 0;

    bql_lock();
    if (!(cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD) &&
          x86_cpu_interrupts_enabled(env)) &&
        !cpu_test_interrupt(cpu, CPU_INTERRUPT_NMI)) {
        cpu->exception_index = EXCP_HLT;
        cpu->halted = true;
        ret = 1;
    }
    bql_unlock();

    return ret;
}

static int whpx_handle_hyperv_guestidle(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    env->hflags2 |= HF2_HYPERV_HLT_MASK;
    return whpx_handle_halt(cpu);
}

static void whpx_vcpu_kick_out_of_hlt(CPUState *cpu) 
{
    WHV_REGISTER_VALUE reg;
    whpx_get_reg(cpu, WHvRegisterInternalActivityState, &reg);
    if (reg.InternalActivity.HaltSuspend) {
        reg.InternalActivity.HaltSuspend = 0;
        whpx_set_reg(cpu, WHvRegisterInternalActivityState, reg);
    }
}

static void whpx_vcpu_pre_run(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    AccelCPUState *vcpu = cpu->accel;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    int irq;
    uint8_t tpr;
    WHV_X64_PENDING_INTERRUPTION_REGISTER new_int;
    UINT32 reg_count = 0;
    WHV_REGISTER_VALUE reg_values[3];
    WHV_REGISTER_NAME reg_names[3];
    int irr = apic_get_highest_priority_irr(x86_cpu->apic_state);

    memset(&new_int, 0, sizeof(new_int));
    memset(reg_values, 0, sizeof(reg_values));

    bql_lock();

    /* Inject NMI */
    if (!vcpu->interruption_pending &&
        cpu_test_interrupt(cpu, CPU_INTERRUPT_NMI | CPU_INTERRUPT_SMI)) {
        if (cpu_test_interrupt(cpu, CPU_INTERRUPT_NMI)) {
            cpu_reset_interrupt(cpu, CPU_INTERRUPT_NMI);
            vcpu->interruptable = false;
            new_int.InterruptionType = WHvX64PendingNmi;
            new_int.InterruptionPending = 1;
            new_int.InterruptionVector = 2;
        }
        if (cpu_test_interrupt(cpu, CPU_INTERRUPT_SMI)) {
            cpu_reset_interrupt(cpu, CPU_INTERRUPT_SMI);
        }
    }

    /*
     * Force the VCPU out of its inner loop to process any INIT requests or
     * commit pending TPR access.
     */
    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_INIT | CPU_INTERRUPT_TPR)) {
        if (cpu_test_interrupt(cpu, CPU_INTERRUPT_INIT) &&
            !(env->hflags & HF_SMM_MASK)) {
            qatomic_set(&cpu->exit_request, true);
        }
        if (cpu_test_interrupt(cpu, CPU_INTERRUPT_TPR)) {
            qatomic_set(&cpu->exit_request, true);
        }
    }

    if (irr == -1) {
        if (isa_pic != NULL && pic_get_output(isa_pic)) {
            /* In case it's a PIC interrupt */
            irr = 0;
        } else if (cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD)) {
            abort();
        }
    }

    /* Get pending hard interruption or replay one that was overwritten */
    if (!whpx_irqchip_in_kernel()) {
        if (!vcpu->interruption_pending &&
            vcpu->interruptable && (env->eflags & IF_MASK)
            && (vcpu->tpr < irr || irr == 0)) {
            assert(!new_int.InterruptionPending);
            if (cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD)) {
                cpu_reset_interrupt(cpu, CPU_INTERRUPT_HARD);
                irq = cpu_get_pic_interrupt(env);
                if (irq >= 0) {
                    new_int.InterruptionType = WHvX64PendingInterrupt;
                    new_int.InterruptionPending = 1;
                    new_int.InterruptionVector = irq;
                }
            }
        }

        /* Setup interrupt state if new one was prepared */
        if (new_int.InterruptionPending) {
            reg_values[reg_count].PendingInterruption = new_int;
            reg_names[reg_count] = WHvRegisterPendingInterruption;
            reg_count += 1;
        }
    } else if (vcpu->ready_for_pic_interrupt &&
               cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD)) {
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_HARD);
        irq = cpu_get_pic_interrupt(env);
        if (irq >= 0) {
            reg_names[reg_count] = WHvRegisterPendingEvent;
            reg_values[reg_count].ExtIntEvent = (WHV_X64_PENDING_EXT_INT_EVENT)
            {
                .EventPending = 1,
                .EventType = WHvX64PendingEventExtInt,
                .Vector = irq,
            };
            reg_count += 1;
            /* 
             * When the Hyper-V APIC is enabled, to get out of HLT we
             * either have to request an interrupt or manually get it away
             * from HLT.
             *
             * We also manually do inject some interrupts via WHvRegisterPendingEvent
             * instead of WHVRequestInterrupt, which does not reset the HLT state.
             */
            if (whpx_irqchip_in_kernel()) {
                whpx_vcpu_kick_out_of_hlt(cpu);
            }
        }
     }

    /* Sync the TPR to the CR8 if was modified during the intercept */
    tpr = cpu_get_apic_tpr(x86_cpu->apic_state);
    if (!whpx_irqchip_in_kernel() && tpr != vcpu->tpr) {
        vcpu->tpr = tpr;
        reg_values[reg_count].Reg64 = tpr;
        qatomic_set(&cpu->exit_request, true);
        reg_names[reg_count] = WHvX64RegisterCr8;
        reg_count += 1;
    }

    /* Update the state of the interrupt delivery notification */
    if ((!vcpu->window_registered ||
        (vcpu->window_priority < irr && vcpu->window_priority != 0) ||
        (irr == 0 && vcpu->window_priority != 0)) &&
        cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD)) {
        reg_values[reg_count].DeliverabilityNotifications =
            (WHV_X64_DELIVERABILITY_NOTIFICATIONS_REGISTER) {
                .InterruptNotification = 1,
                .InterruptPriority = irr >> 4
            };
        vcpu->window_registered = 1;
        vcpu->window_priority = irr;
        reg_names[reg_count] = WHvX64RegisterDeliverabilityNotifications;
        reg_count += 1;
    }

    bql_unlock();
    vcpu->ready_for_pic_interrupt = false;

    if (reg_count) {
        hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
            whpx->partition, cpu->cpu_index,
            reg_names, reg_count, reg_values);
        if (FAILED(hr)) {
            error_report("WHPX: Failed to set interrupt state registers,"
                         " hr=%08lx, InterruptPriority=%i", hr, irr >> 4);
        }
    }
}

static void whpx_vcpu_post_run(CPUState *cpu)
{
    AccelCPUState *vcpu = cpu->accel;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    env->eflags = vcpu->exit_ctx.VpContext.Rflags;

    if (!whpx_irqchip_in_kernel()) {
        uint64_t tpr = vcpu->exit_ctx.VpContext.Cr8;
        if (vcpu->tpr != tpr) {
            vcpu->tpr = tpr;
            bql_lock();
            cpu_set_apic_tpr(x86_cpu->apic_state, vcpu->tpr);
            bql_unlock();
        }
    }

    vcpu->interruption_pending =
        vcpu->exit_ctx.VpContext.ExecutionState.InterruptionPending;

    vcpu->interruptable =
        !vcpu->exit_ctx.VpContext.ExecutionState.InterruptShadow;
}


static void whpx_vcpu_process_async_events(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    AccelCPUState *vcpu = cpu->accel;

    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_INIT) &&
        !(env->hflags & HF_SMM_MASK)) {
        whpx_cpu_synchronize_state(cpu);
        do_cpu_init(x86_cpu);
        vcpu->interruptable = true;
    }

    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_POLL)) {
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_POLL);
        apic_poll_irq(x86_cpu->apic_state);
    }

    if ((cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD) &&
         ((env->eflags & IF_MASK) || (env->hflags2 & HF2_HYPERV_HLT_MASK))) ||
        cpu_test_interrupt(cpu, CPU_INTERRUPT_NMI)) {
        cpu->halted = false;
        env->hflags2 &= ~HF2_HYPERV_HLT_MASK;
    }

    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_SIPI)) {
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_SIPI);
        whpx_cpu_synchronize_state(cpu);
        do_cpu_sipi(x86_cpu);
    }

    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_TPR)) {
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_TPR);
        whpx_cpu_synchronize_state(cpu);
        apic_handle_tpr_access_report(x86_cpu->apic_state, env->eip,
                                      env->tpr_access_type);
    }
}

static void whpx_inject_exceptions(CPUState* cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    if (env->exception_injected) {
        env->exception_injected = 0;
        WHV_REGISTER_VALUE reg = {};
        reg.ExceptionEvent.EventPending = 1;
        reg.ExceptionEvent.EventType = WHvX64PendingEventException;
        reg.ExceptionEvent.DeliverErrorCode = env->has_error_code;
        reg.ExceptionEvent.Vector = env->exception_nr;
        reg.ExceptionEvent.ErrorCode = env->error_code;
        if (env->exception_has_payload) {
            reg.ExceptionEvent.ExceptionParameter = env->exception_payload;
        }
        whpx_set_reg(cpu, WHvRegisterPendingEvent, reg);
    }
}

int whpx_vcpu_run(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    AccelCPUState *vcpu = cpu->accel;
    struct whpx_breakpoint *stepped_over_bp = NULL;
    WhpxStepMode exclusive_step_mode = WHPX_STEP_NONE;
    int ret;

    g_assert(bql_locked());

    if (whpx->running_cpus++ == 0) {
        /* Insert breakpoints into memory, update exception exit bitmap. */
        ret = whpx_first_vcpu_starting(cpu);
        if (ret != 0) {
            return ret;
        }
    }

    if (whpx->breakpoints.breakpoints &&
        whpx->breakpoints.breakpoints->used > 0)
    {
        uint64_t pc = whpx_vcpu_get_pc(cpu, true);
        stepped_over_bp = whpx_lookup_breakpoint_by_addr(pc);
        if (stepped_over_bp && stepped_over_bp->state != WHPX_BP_SET) {
            stepped_over_bp = NULL;
        }

        if (stepped_over_bp) {
            /*
             * We are trying to run the instruction overwritten by an active
             * breakpoint. We will temporarily disable the breakpoint, suspend
             * other CPUs, and step over the instruction.
             */
            exclusive_step_mode = WHPX_STEP_EXCLUSIVE;
        }
    }

    if (exclusive_step_mode == WHPX_STEP_NONE) {
        whpx_vcpu_process_async_events(cpu);
        if (cpu->halted && !whpx_irqchip_in_kernel()) {
            cpu->exception_index = EXCP_HLT;
            qatomic_set(&cpu->exit_request, false);
            return 0;
        }
    }

    bql_unlock();

    if (exclusive_step_mode != WHPX_STEP_NONE) {
        start_exclusive();
        g_assert(cpu == current_cpu);
        g_assert(!cpu->running);
        cpu->running = true;

        hr = whpx_set_exception_exit_bitmap(
            1UL << WHvX64ExceptionTypeDebugTrapOrFault);
        if (!SUCCEEDED(hr)) {
            error_report("WHPX: Failed to update exception exit mask, "
                         "hr=%08lx.", hr);
            return 1;
        }

        if (stepped_over_bp) {
            /* Temporarily disable the triggered breakpoint. */
            cpu_memory_rw_debug(cpu,
                stepped_over_bp->address,
                &stepped_over_bp->original_instruction,
                1,
                true);
        }
    } else {
        cpu_exec_start(cpu);
    }

    do {
        if (cpu->vcpu_dirty) {
            whpx_set_registers(cpu, WHPX_LEVEL_RUNTIME_STATE);
            cpu->vcpu_dirty = false;
        }

        if (exclusive_step_mode == WHPX_STEP_NONE) {
            whpx_vcpu_pre_run(cpu);

            /* Corresponding store-release is in cpu_exit. */
            if (qatomic_load_acquire(&cpu->exit_request)) {
                whpx_vcpu_kick(cpu);
            }
        }

        if (exclusive_step_mode != WHPX_STEP_NONE || cpu->singlestep_enabled) {
            whpx_vcpu_configure_single_stepping(cpu, true, NULL);
        }

        whpx_inject_exceptions(cpu);

        hr = whp_dispatch.WHvRunVirtualProcessor(
            whpx->partition, cpu->cpu_index,
            &vcpu->exit_ctx, sizeof(vcpu->exit_ctx));

        if (FAILED(hr)) {
            error_report("WHPX: Failed to exec a virtual processor,"
                         " hr=%08lx", hr);
            ret = -1;
            break;
        }

        if (exclusive_step_mode != WHPX_STEP_NONE || cpu->singlestep_enabled) {
            whpx_vcpu_configure_single_stepping(cpu,
                false,
                &vcpu->exit_ctx.VpContext.Rflags);
        }

        whpx_vcpu_post_run(cpu);

        switch (vcpu->exit_ctx.ExitReason) {
        case WHvRunVpExitReasonMemoryAccess:
            ret = whpx_handle_mmio(cpu, &vcpu->exit_ctx);
            break;

        case WHvRunVpExitReasonX64IoPortAccess:
            ret = whpx_handle_portio(cpu, &vcpu->exit_ctx);
            break;

        case WHvRunVpExitReasonX64InterruptWindow:
            vcpu->ready_for_pic_interrupt = 1;
            vcpu->window_registered = 0;
            vcpu->window_priority = 0;
            ret = 0;
            break;

        case WHvRunVpExitReasonX64ApicEoi:
            assert(whpx_irqchip_in_kernel());
            ioapic_eoi_broadcast(vcpu->exit_ctx.ApicEoi.InterruptVector);
            break;

        case WHvRunVpExitReasonX64Halt:
            /*
             * Used for kernel-irqchip=off
             */
            ret = whpx_handle_halt(cpu);
            break;

        case WHvRunVpExitReasonCanceled:
            if (exclusive_step_mode != WHPX_STEP_NONE) {
                /*
                 * We are trying to step over a single instruction, and
                 * likely got a request to stop from another thread.
                 * Delay it until we are done stepping
                 * over.
                 */
                ret = 0;
            } else {
                cpu->exception_index = EXCP_INTERRUPT;
                ret = 1;
            }
            break;
        case WHvRunVpExitReasonX64MsrAccess: {
            WHV_REGISTER_VALUE reg_values[3] = {0};
            WHV_REGISTER_NAME reg_names[3];
            UINT32 reg_count;
            bool is_known_msr = 0; 
            bool raises_gpf = false;
            uint64_t val;

            if (vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite) {
                val = ((uint32_t)vcpu->exit_ctx.MsrAccess.Rax) |
                    ((uint64_t)(vcpu->exit_ctx.MsrAccess.Rdx) << 32);
            } else {
                /*
                 * Workaround for [-Werror=maybe-uninitialized]
                 * with GCC. Not needed with Clang.
                 */
                val = 0;
            }

            reg_names[0] = WHvX64RegisterRip;
            reg_names[1] = WHvX64RegisterRax;
            reg_names[2] = WHvX64RegisterRdx;

            reg_values[0].Reg64 =
                vcpu->exit_ctx.VpContext.Rip +
                vcpu->exit_ctx.VpContext.InstructionLength;

            if (vcpu->exit_ctx.MsrAccess.MsrNumber == HV_X64_MSR_APIC_FREQUENCY
                && !vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite
                && !whpx_irqchip_in_kernel()) {
                is_known_msr = 1;
                val = X86_CPU(cpu)->env.apic_bus_freq;
            }

            if (vcpu->exit_ctx.MsrAccess.MsrNumber == MSR_IA32_APICBASE) {
                is_known_msr = 1;
                if (val & MSR_IA32_APICBASE_RESERVED) {
                    x86_emul_raise_exception(&X86_CPU(cpu)->env, EXCP0D_GPF, 0);
                    raises_gpf = true;
                }
                if (!vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite) {
                    /* Read path unreachable on Hyper-V */
                    abort();
                } else {
                    WHV_REGISTER_VALUE reg = {.Reg64 = val};
                    int msr_ret = cpu_set_apic_base(X86_CPU(cpu)->apic_state, val);
                    if (msr_ret < 0) {
                        x86_emul_raise_exception(&X86_CPU(cpu)->env, EXCP0D_GPF, 0);
                        raises_gpf = true;
                    } else {
                        whpx_set_reg(cpu, WHvX64RegisterApicBase, reg);
                    }
                }
            }

            if (!whpx_irqchip_in_kernel() &&
                vcpu->exit_ctx.MsrAccess.MsrNumber >= MSR_APIC_START &&
                vcpu->exit_ctx.MsrAccess.MsrNumber <= MSR_APIC_END) {
                int index = vcpu->exit_ctx.MsrAccess.MsrNumber - MSR_APIC_START;
                int msr_ret;
                is_known_msr = 1;
                if (!vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite) {
                    bql_lock();
                    msr_ret = apic_msr_read(X86_CPU(cpu)->apic_state, index, &val);
                    bql_unlock();
                    reg_values[1].Reg64 = val;
                    if (msr_ret < 0) {
                        x86_emul_raise_exception(&X86_CPU(cpu)->env, EXCP0D_GPF, 0);
                        raises_gpf = true;
                    }
                } else {
                    bql_lock();
                    msr_ret = apic_msr_write(X86_CPU(cpu)->apic_state, index, val);
                    bql_unlock();
                    if (msr_ret < 0) {
                        x86_emul_raise_exception(&X86_CPU(cpu)->env, EXCP0D_GPF, 0);
                        raises_gpf = true;
                    }
                }
            }

            /*
             * Windows and Linux both use this MSR.
             * Windows 11 25H2 uses it even when not advertised.
             */
            if (vcpu->exit_ctx.MsrAccess.MsrNumber == HV_X64_MSR_GUEST_IDLE
                && !vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite
                && !whpx_irqchip_in_kernel()
                && whpx->hyperv_enlightenments_enabled) {
                is_known_msr = 1;
                whpx_bump_rip(cpu, &vcpu->exit_ctx);
                ret = whpx_handle_hyperv_guestidle(cpu);
                break;
            }

            /*
             * Linux tries to use it anyway even when not exposed.
             * Ignore the write as the VP assist page is not used.
             */
            if (vcpu->exit_ctx.MsrAccess.MsrNumber == HV_X64_MSR_VP_ASSIST_PAGE
                && vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite
                && !whpx_irqchip_in_kernel()
                && whpx->hyperv_enlightenments_enabled) {
                is_known_msr = 1;
            }

            /*
             * For all unsupported MSR access we:
             *     ignore writes
             *     return 0 on read.
             */
            reg_count = vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite ?
                        1 : 3;

            if (!vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite) {
                reg_values[1].Reg32 = (uint32_t)val;
                reg_values[2].Reg32 = (uint32_t)(val >> 32);
            }

            if (!is_known_msr) {
                trace_whpx_unsupported_msr_access(vcpu->exit_ctx.MsrAccess.MsrNumber,
                    vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite);
            }

            if (!is_known_msr && !whpx->ignore_unknown_msr) {
                x86_emul_raise_exception(&X86_CPU(cpu)->env, EXCP0D_GPF, 0);
                raises_gpf = true;
            }

            /* When a GPF is raised, do not change Rip. */
            if (raises_gpf) {
                reg_values[0].Reg64 =
                    vcpu->exit_ctx.VpContext.Rip;
            }

            hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
                whpx->partition,
                cpu->cpu_index,
                reg_names, reg_count,
                reg_values);

            if (FAILED(hr)) {
                error_report("WHPX: Failed to set MsrAccess state "
                             " registers, hr=%08lx", hr);
            }
            ret = 0;
            break;
        }
        case WHvRunVpExitReasonX64Cpuid: {
            WHV_REGISTER_VALUE reg_values[5] = {0};
            WHV_REGISTER_NAME reg_names[5];
            UINT32 reg_count = 5;
            X86CPU *x86_cpu = X86_CPU(cpu);
            CPUX86State *env = &x86_cpu->env;

            reg_names[0] = WHvX64RegisterRip;
            reg_names[1] = WHvX64RegisterRax;
            reg_names[2] = WHvX64RegisterRcx;
            reg_names[3] = WHvX64RegisterRdx;
            reg_names[4] = WHvX64RegisterRbx;

            reg_values[0].Reg64 =
                vcpu->exit_ctx.VpContext.Rip +
                vcpu->exit_ctx.VpContext.InstructionLength;

             cpu_x86_cpuid(env, vcpu->exit_ctx.CpuidAccess.Rax,
                vcpu->exit_ctx.CpuidAccess.Rcx,
                (UINT32 *)&reg_values[1].Reg32,
                (UINT32 *)&reg_values[4].Reg32, (UINT32 *)&reg_values[2].Reg32,
                (UINT32 *)&reg_values[3].Reg32);

            if (!whpx->hyperv_enlightenments_enabled) {
                switch (vcpu->exit_ctx.CpuidAccess.Rax) {
                case 1:
                    reg_values[2].Reg64 |= CPUID_EXT_HYPERVISOR;
                    break;
                case 0x40000000:
                    /*
                     * Use vmware_cpuid_freq as a proxy to report VMware.
                     * This is to get the TSC/APIC frequency query functionality
                     * provided through vmport, as Linux doesn't use leaf
                     * 0x40000010 for getting those frequencies.
                     */
                    if (x86_cpu->vmware_cpuid_freq) {
                        reg_values[1].Reg64 = 0x40000010;
                        reg_values[4].Reg64 = 0x61774d56;
                        reg_values[2].Reg64 = 0x4d566572;
                        reg_values[3].Reg64 = 0x65726177;
                    } else {
                        /* report KVM otherwise if that's disabled */
                        reg_values[1].Reg64 = 0x40000001;
                        reg_values[4].Reg64 = 0x4b4d564b;
                        reg_values[2].Reg64 = 0x564b4d56;
                        reg_values[3].Reg64 = 0x4d;
                    }
                    break;
                case 0x40000001:
                    if (!x86_cpu->vmware_cpuid_freq) {
                        /* KVM reporting of X2APIC support */
                        reg_values[1].Reg64 = reg_values[4].Reg64 =
                            reg_values[2].Reg64 = 1 << 15;
                    }
                    break;
                case 0x40000010:
                    if (x86_cpu->vmware_cpuid_freq) {
                        reg_values[1].Reg64 = env->tsc_khz;
                        reg_values[4].Reg64 = env->apic_bus_freq / 1000; /* Hz to KHz */
                    }
                    break;
                }
            } else {
                switch (vcpu->exit_ctx.CpuidAccess.Rax) {
                case 0x40000000:
                case 0x40000001:
                case 0x40000010:
                    reg_values[1].Reg64 = vcpu->exit_ctx.CpuidAccess.DefaultResultRax;
                    reg_values[2].Reg64 = vcpu->exit_ctx.CpuidAccess.DefaultResultRcx;
                    reg_values[3].Reg64 = vcpu->exit_ctx.CpuidAccess.DefaultResultRdx;
                    reg_values[4].Reg64 = vcpu->exit_ctx.CpuidAccess.DefaultResultRbx;
                    break;
                }
            }

            if (vcpu->exit_ctx.CpuidAccess.Rax == 0x1) {
                if (cpu_has_x2apic_feature(env)) {
                    reg_values[2].Reg64 |= CPUID_EXT_X2APIC;
                } else {
                    reg_values[2].Reg32 &= ~CPUID_EXT_X2APIC;
                }

                /* CPUID[1:EDX].APIC is dynamic */
                if (env->features[FEAT_1_EDX] & CPUID_APIC) {
                    reg_values[3].Reg32 |= CPUID_APIC;
                } else {
                    reg_values[3].Reg32 &= ~CPUID_APIC;
                }
            }

            /* Dynamic depending on XCR0 and XSS, so query DefaultResult */
            if (vcpu->exit_ctx.CpuidAccess.Rax == 0x07
                && vcpu->exit_ctx.CpuidAccess.Rcx == 0) {
                if (vcpu->exit_ctx.CpuidAccess.DefaultResultRdx
                    & CPUID_7_0_EDX_CET_IBT) {
                    reg_values[3].Reg32 |= CPUID_7_0_EDX_CET_IBT;
                } else {
                    reg_values[3].Reg32 &= ~CPUID_7_0_EDX_CET_IBT;
                }

                if (vcpu->exit_ctx.CpuidAccess.DefaultResultRcx
                    & CPUID_7_0_ECX_CET_SHSTK) {
                    reg_values[2].Reg32 |= CPUID_7_0_ECX_CET_SHSTK;
                } else {
                    reg_values[2].Reg32 &= ~CPUID_7_0_ECX_CET_SHSTK;
                }

                if (vcpu->exit_ctx.CpuidAccess.DefaultResultRcx
                    & CPUID_7_0_ECX_OSPKE) {
                    reg_values[2].Reg32 |= CPUID_7_0_ECX_OSPKE;
                } else {
                    reg_values[2].Reg32 &= ~CPUID_7_0_ECX_OSPKE;
                }
            }

            /* CPUID[0xD,{1,2}].EBX are dynamic depending on guest features. */
            if (vcpu->exit_ctx.CpuidAccess.Rax == 0xd) {
                if (vcpu->exit_ctx.CpuidAccess.Rcx == 1
                    || vcpu->exit_ctx.CpuidAccess.Rcx == 2) {
                    reg_values[4].Reg64 = vcpu->exit_ctx.CpuidAccess.DefaultResultRbx;
                }
            }

            /* OSXSAVE is dynamic. Do this instead of syncing CR4 */
            if (vcpu->exit_ctx.CpuidAccess.Rax == 1) {
                if (vcpu->exit_ctx.CpuidAccess.DefaultResultRcx
                    & CPUID_EXT_OSXSAVE) {
                    reg_values[2].Reg32 |= CPUID_EXT_OSXSAVE;
                } else {
                    reg_values[2].Reg32 &= ~CPUID_EXT_OSXSAVE;
                }
            }

            hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
                whpx->partition,
                cpu->cpu_index,
                reg_names, reg_count,
                reg_values);

            if (FAILED(hr)) {
                error_report("WHPX: Failed to set CpuidAccess state "
                             " registers, hr=%08lx", hr);
            }
            ret = 0;
            break;
        }
        case WHvRunVpExitReasonException:
            if (vcpu->exit_ctx.VpException.ExceptionType ==
                WHvX64ExceptionTypeGeneralProtectionFault) {
                if (whpx_handle_msr_from_gpf(cpu)) {
                    whpx_inject_back_gpf(cpu);
                }
                ret = 0;
                break;
            }

            whpx_get_registers(cpu, WHPX_LEVEL_FULL_STATE);

            if ((vcpu->exit_ctx.VpException.ExceptionType ==
                 WHvX64ExceptionTypeDebugTrapOrFault) &&
                (vcpu->exit_ctx.VpException.InstructionByteCount >= 1) &&
                (vcpu->exit_ctx.VpException.InstructionBytes[0] ==
                 whpx_breakpoint_instruction)) {
                /* Stopped at a software breakpoint. */
                cpu->exception_index = EXCP_DEBUG;
            } else if ((vcpu->exit_ctx.VpException.ExceptionType ==
                        WHvX64ExceptionTypeDebugTrapOrFault) &&
                       !cpu->singlestep_enabled) {
                /*
                 * Just finished stepping over a breakpoint, but the
                 * gdb does not expect us to do single-stepping.
                 * Don't do anything special.
                 */
                cpu->exception_index = EXCP_INTERRUPT;
            } else {
                /* Another exception or debug event. Report it to GDB. */
                cpu->exception_index = EXCP_DEBUG;
            }

            ret = 1;
            break;
        case WHvRunVpExitReasonNone:
        case WHvRunVpExitReasonUnrecoverableException:
        case WHvRunVpExitReasonInvalidVpRegisterValue:
        case WHvRunVpExitReasonUnsupportedFeature:
        default:
            error_report("WHPX: Unexpected VP exit code %d",
                         vcpu->exit_ctx.ExitReason);
            whpx_get_registers(cpu, WHPX_LEVEL_FULL_STATE);
            bql_lock();
            vm_stop(RUN_STATE_PAUSED);
            bql_unlock();
            break;
        }

    } while (!ret);

    if (stepped_over_bp) {
        /* Restore the breakpoint we stepped over */
        cpu_memory_rw_debug(cpu,
            stepped_over_bp->address,
            (void *)&whpx_breakpoint_instruction,
            1,
            true);
    }

    if (exclusive_step_mode != WHPX_STEP_NONE) {
        g_assert(cpu_in_exclusive_context(cpu));
        cpu->running = false;
        end_exclusive();

        exclusive_step_mode = WHPX_STEP_NONE;
    } else {
        cpu_exec_end(cpu);
    }

    bql_lock();
    current_cpu = cpu;

    if (--whpx->running_cpus == 0) {
        whpx_last_vcpu_stopping(cpu);
    }

    return ret < 0;
}

/*
 * Vcpu support.
 */

static Error *whpx_migration_blocker;

static void whpx_cpu_update_state(void *opaque, bool running, RunState state)
{
    CPUX86State *env = opaque;

    if (running) {
        env->tsc_valid = false;
    }
}

int whpx_init_vcpu(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    AccelCPUState *vcpu = NULL;
    Error *local_error = NULL;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    X86XSaveHeader *header;
    size_t page_size = qemu_real_host_page_size();
    size_t xsave_len;
    UINT64 freq = 0;
    int ret;

    /* Add migration blockers for all unsupported features of the
     * Windows Hypervisor Platform
     */
    if (whpx_migration_blocker == NULL) {
        error_setg(&whpx_migration_blocker,
               "State blocked due to missing dirty memory tracking support,"
               "And some system register/state save-restore ");

        if (migrate_add_blocker(&whpx_migration_blocker, &local_error) < 0) {
            error_report_err(local_error);
            ret = -EINVAL;
            goto error;
        }
    }

    vcpu = g_new0(AccelCPUState, 1);

    hr = whp_dispatch.WHvCreateVirtualProcessor(
        whpx->partition, cpu->cpu_index, 0);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to create a virtual processor,"
                     " hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    if (!whpx_irqchip_in_kernel() && x86_cpu->apic_state != NULL) {
        WHV_REGISTER_VALUE apic_id = {.Reg64 = x86_cpu->apic_state->initial_apic_id};
        whpx_set_reg(cpu, WHvX64RegisterInitialApicId, apic_id);
    }

    /*
     * vcpu's TSC frequency is either specified by user, or use the value
     * provided by Hyper-V if the former is not present. In the latter case, we
     * query it from Hyper-V and record in env->tsc_khz, so that vcpu's TSC
     * frequency can be migrated later via this field.
     */
    if (!env->tsc_khz) {
        hr = whp_dispatch.WHvGetCapability(
            WHvCapabilityCodeProcessorClockFrequency, &freq, sizeof(freq),
                NULL);
        if (hr != WHV_E_UNKNOWN_CAPABILITY) {
            if (FAILED(hr)) {
                printf("WHPX: Failed to query tsc frequency, hr=0x%08lx\n", hr);
            } else {
                env->tsc_khz = freq / 1000; /* Hz to KHz */
            }
        }
    }

    env->apic_bus_freq = HYPERV_APIC_BUS_FREQUENCY;
    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodeInterruptClockFrequency, &freq, sizeof(freq), NULL);
    if (hr != WHV_E_UNKNOWN_CAPABILITY) {
        if (FAILED(hr)) {
            printf("WHPX: Failed to query apic bus frequency hr=0x%08lx\n", hr);
        } else {
            env->apic_bus_freq = freq;
        }
    }

    /* When not using the Hyper-V APIC, the frequency is 1 GHz */
    if (!whpx_irqchip_in_kernel()) {
        env->apic_bus_freq = 1000000000;
    }

    vcpu->interruptable = true;
    cpu->vcpu_dirty = true;
    cpu->accel = vcpu;
    max_vcpu_index = max(max_vcpu_index, cpu->cpu_index);
    qemu_add_vm_change_state_handler(whpx_cpu_update_state, env);

    env->emu_mmio_buf = g_new(char, 4096);
    /* Initialize XSAVE buffer page-aligned */
    xsave_len = whpx_get_xsave_max_len();
    env->xsave_buf = qemu_memalign(page_size, xsave_len);
    env->xsave_buf_len = xsave_len;
    memset(env->xsave_buf, 0, env->xsave_buf_len);

    /* we need to set the compacted format bit in xsave header for Hyper-V */
    header = (X86XSaveHeader *)(env->xsave_buf + sizeof(X86LegacyXSaveArea));
    header->xcomp_bv = header->xstate_bv | (1ULL << 63);

    return 0;

error:
    g_free(vcpu);

    return ret;
}

static void whpx_cpu_xsave_init(void)
{
    static bool first = true;
    int i;

    if (!first) {
        return;
    }
    first = false;

    /* x87 and SSE states are in the legacy region of the XSAVE area. */
    x86_ext_save_areas[XSTATE_FP_BIT].offset = 0;
    x86_ext_save_areas[XSTATE_SSE_BIT].offset = 0;

    for (i = XSTATE_SSE_BIT + 1; i < XSAVE_STATE_AREA_COUNT; i++) {
        ExtSaveArea *esa = &x86_ext_save_areas[i];

        if (esa->size) {
            int sz = whpx_get_supported_cpuid(0xd, i, R_EAX);
            if (sz != 0) {
                assert(esa->size == sz);
                esa->offset = whpx_get_supported_cpuid(0xd, i, R_EBX);
            }
        }
    }
}

static void whpx_cpu_max_instance_init(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    env->cpuid_min_level =
        whpx_get_supported_cpuid(0x0, 0, R_EAX);
    env->cpuid_min_xlevel =
        whpx_get_supported_cpuid(0x80000000, 0, R_EAX);
    env->cpuid_min_xlevel2 =
        whpx_get_supported_cpuid(0xC0000000, 0, R_EAX);
}

static PropValue whpx_default_props[] = {
    { "x2apic", "on" },
    { NULL, NULL },
};


void whpx_cpu_instance_init(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);

    host_cpu_instance_init(cpu);
    x86_cpu_apply_props(cpu, whpx_default_props);

    if (xcc->max_features) {
        whpx_cpu_max_instance_init(cpu);
    }

    if (whpx_has_xsave()) {
        whpx_cpu_xsave_init();
    }
}

/*
 * Partition support
 */

static void whpx_set_unknown_msr(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    struct whpx_state *whpx = &whpx_global;
    OnOffAuto mode;

    if (!visit_type_OnOffAuto(v, name, &mode, errp)) {
        return;
    }

    switch (mode) {
    case ON_OFF_AUTO_ON:
        whpx->ignore_unknown_msr = true;
        break;

    case ON_OFF_AUTO_OFF:
        whpx->ignore_unknown_msr = false;
        break;

    case ON_OFF_AUTO_AUTO:
        whpx->ignore_unknown_msr = true;
        break;
    default:
        /*
         * The value was checked in visit_type_OnOffAuto() above. If
         * we get here, then something is wrong in QEMU.
         */
        abort();
    }
}

static void whpx_set_intercept_msr_gp(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    struct whpx_state *whpx = &whpx_global;
    OnOffAuto mode;

    if (!visit_type_OnOffAuto(v, name, &mode, errp)) {
        return;
    }

    switch (mode) {
    case ON_OFF_AUTO_ON:
        whpx->intercept_msr_gp = true;
        break;

    case ON_OFF_AUTO_OFF:
        whpx->intercept_msr_gp = false;
        break;

    case ON_OFF_AUTO_AUTO:
        whpx->intercept_msr_gp = false;
        break;
    default:
        /*
         * The value was checked in visit_type_OnOffAuto() above. If
         * we get here, then something is wrong in QEMU.
         */
        abort();
    }
}

static void whpx_set_ssd(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    struct whpx_state *whpx = &whpx_global;
    OnOffAuto mode;

    if (!visit_type_OnOffAuto(v, name, &mode, errp)) {
        return;
    }

    switch (mode) {
    case ON_OFF_AUTO_ON:
        whpx->separate_security_domain = true;
        break;

    case ON_OFF_AUTO_OFF:
        whpx->separate_security_domain = false;
        break;

    case ON_OFF_AUTO_AUTO:
        whpx->separate_security_domain = true;
        break;
    default:
        /*
         * The value was checked in visit_type_OnOffAuto() above. If
         * we get here, then something is wrong in QEMU.
         */
        abort();
    }
}


void whpx_arch_accel_class_init(ObjectClass *oc)
{
    object_class_property_add(oc, "ignore-unknown-msr", "OnOffAuto",
        NULL, whpx_set_unknown_msr,
        NULL, NULL);
    object_class_property_set_description(oc, "ignore-unknown-msr",
        "Configure unknown MSR behavior");
    object_class_property_add(oc, "intercept-msr-gp", "OnOffAuto",
        NULL, whpx_set_intercept_msr_gp,
        NULL, NULL);
    object_class_property_set_description(oc, "intercept-msr-gp",
        "Intercept #GP to log erroring MSR accesses.");
    object_class_property_add(oc, "ssd", "OnOffAuto",
        NULL, whpx_set_ssd,
        NULL, NULL);
    object_class_property_set_description(oc, "ssd",
        "Separate security domain");
}

int whpx_accel_init(AccelState *as, MachineState *ms)
{
    struct whpx_state *whpx;
    int ret;
    HRESULT hr;
    WHV_CAPABILITY whpx_cap;
    UINT32 whpx_cap_size;
    WHV_PARTITION_PROPERTY prop;
    WHV_CAPABILITY_FEATURES features = {0};
    WHV_PROCESSOR_FEATURES_BANKS processor_features;
    WHV_PROCESSOR_PERFMON_FEATURES perfmon_features;

    UINT32 cpuidExitList[] = {0x0, 0x1, 0x6, 0x7, 0xb, 0xd, 0x14, 0x24, 0x29, 0x1E,
        0x40000000, 0x40000001, 0x40000010, 0x80000000, 0x80000001,
        0x80000002, 0x80000003, 0x80000004, 0x80000007, 0x80000008,
        0x8000000A, 0x80000021, 0x80000022, 0xC0000000, 0xC0000001};

    X86MachineState *x86ms = X86_MACHINE(ms);
    bool pic_enabled = false;

    if (x86ms->pic == ON_OFF_AUTO_ON || x86ms->pic == ON_OFF_AUTO_AUTO) {
        pic_enabled = true;
    }

    whpx = &whpx_global;

    if (!init_whp_dispatch()) {
        ret = -ENOSYS;
        goto error;
    }

    /* for isapc, disable Hyper-V enlightenments and LAPIC */
    if (!strcmp(MACHINE_GET_CLASS(ms)->name, "isapc")) {
        whpx->kernel_irqchip_allowed = false;
        whpx->kernel_irqchip_required = false;
        whpx->hyperv_enlightenments_allowed = false;
        whpx->hyperv_enlightenments_required = false;
    }

    whpx->mem_quota = ms->ram_size;

    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodeHypervisorPresent, &whpx_cap,
        sizeof(whpx_cap), &whpx_cap_size);
    if (FAILED(hr) || !whpx_cap.HypervisorPresent) {
        error_report("WHPX: No accelerator found, hr=%08lx", hr);
        ret = -ENOSPC;
        goto error;
    }

    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodeFeatures, &features, sizeof(features), NULL);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to query capabilities, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    hr = whp_dispatch.WHvCreatePartition(&whpx->partition);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to create partition, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    /*
     * Query the XSAVE capability of the partition. Any error here is not
     * considered fatal.
     */
    hr = whp_dispatch.WHvGetPartitionProperty(
        whpx->partition,
        WHvPartitionPropertyCodeProcessorXsaveFeatures,
        &whpx_xsave_cap,
        sizeof(whpx_xsave_cap),
        &whpx_cap_size);

    /*
     * Windows version which don't support this property will return with the
     * specific error code.
     */
    if (FAILED(hr) && hr != WHV_E_UNKNOWN_PROPERTY) {
        error_report("WHPX: Failed to query XSAVE capability, hr=%08lx", hr);
    }

    memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
    prop.ProcessorCount = ms->smp.cpus;
    hr = whp_dispatch.WHvSetPartitionProperty(
        whpx->partition,
        WHvPartitionPropertyCodeProcessorCount,
        &prop,
        sizeof(WHV_PARTITION_PROPERTY));

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set partition processor count to %u,"
                     " hr=%08lx", prop.ProcessorCount, hr);
        ret = -EINVAL;
        goto error;
    }

    /* Enable supported performance monitoring capabilities */
    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodeProcessorPerfmonFeatures, &perfmon_features,
        sizeof(WHV_PROCESSOR_PERFMON_FEATURES), &whpx_cap_size);
    /*
     * Relying on this is a crutch to maintain Windows 10 support.
     *
     * WHvCapabilityCodeProcessorPerfmonFeatures and
     * WHvPartitionPropertyCodeSyntheticProcessorFeaturesBanks
     * are implemented starting from Windows Server 2022 (build 20348).
     */
    if (FAILED(hr)) {
        warn_report("WHPX: Failed to get performance "
                    "monitoring features, hr=%08lx", hr);
        is_modern_os = false;
    } else {
        hr = whp_dispatch.WHvSetPartitionProperty(
                whpx->partition,
                WHvPartitionPropertyCodeProcessorPerfmonFeatures,
                &perfmon_features,
                sizeof(WHV_PROCESSOR_PERFMON_FEATURES));
        if (FAILED(hr)) {
            error_report("WHPX: Failed to set performance "
                         "monitoring features, hr=%08lx", hr);
            ret = -EINVAL;
            goto error;
        }
    }

    /*
     * Error out if WHP doesn't support apic emulation and user is requiring
     * it.
     */
    if (whpx->kernel_irqchip_required && (!features.LocalApicEmulation ||
            !whp_dispatch.WHvSetVirtualProcessorInterruptControllerState2)) {
        error_report("WHPX: kernel irqchip requested, but unavailable. "
            "Try without kernel-irqchip or with kernel-irqchip=off");
        ret = -EINVAL;
        goto error;
    }

    if (whpx->kernel_irqchip_allowed && !(whpx_is_legacy_os() && pic_enabled
        && !whpx->kernel_irqchip_required) && features.LocalApicEmulation
        && whp_dispatch.WHvSetVirtualProcessorInterruptControllerState2) {
        WHV_X64_LOCAL_APIC_EMULATION_MODE mode =
            WHvX64LocalApicEmulationModeX2Apic;
        hr = whp_dispatch.WHvSetPartitionProperty(
            whpx->partition,
            WHvPartitionPropertyCodeLocalApicEmulationMode,
            &mode,
            sizeof(mode));
        if (FAILED(hr)) {
            error_report("WHPX: Failed to enable kernel irqchip hr=%08lx", hr);
            if (whpx->kernel_irqchip_required) {
                error_report("WHPX: kernel irqchip requested, but unavailable");
                ret = -EINVAL;
                goto error;
            }
        } else {
            whpx_irqchip_in_kernel = true;
        }
    }

    /* Set all the supported features, to follow the MSHV example */
    memset(&processor_features, 0, sizeof(WHV_PROCESSOR_FEATURES_BANKS));
    processor_features.BanksCount = 2;

    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodeProcessorFeaturesBanks, &processor_features,
        sizeof(WHV_PROCESSOR_FEATURES_BANKS), &whpx_cap_size);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to get processor features, hr=%08lx", hr);
        ret = -ENOSPC;
        goto error;
    }

    whpx_rdtsc_cap = processor_features.Bank0.RdtscpSupport;
    whpx_invpcid_cap = processor_features.Bank0.InvpcidSupport;

    if (whpx_irqchip_in_kernel() && processor_features.Bank1.NestedVirtSupport) {
        memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
        prop.NestedVirtualization = 1;
        hr = whp_dispatch.WHvSetPartitionProperty(
            whpx->partition,
            WHvPartitionPropertyCodeNestedVirtualization,
            &prop,
            sizeof(WHV_PARTITION_PROPERTY));
            if (FAILED(hr)) {
                error_report("WHPX: Failed to enable nested virtualization, hr=%08lx", hr);
                ret = -EINVAL;
                goto error;
        }
    }

    /*
     * The combination of separate security domain off
     * and disabling specifically these features results
     * in a significant vmexit performance improvement
     * by skipping speculative execution mitigations.
     */
    if (!whpx->separate_security_domain) {
        processor_features.Bank0.IbrsSupport = 0;
        processor_features.Bank0.StibpSupport = 0;
        processor_features.Bank0.IbpbSupport = 0;
        processor_features.Bank0.SsbdSupport = 0;
        processor_features.Bank0.IbrsAllSupport = 0;
        processor_features.Bank1.PsfdSupport = 0;
        memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
        prop.SeparateSecurityDomain = 0;
        hr = whp_dispatch.WHvSetPartitionProperty(
            whpx->partition,
            WHvPartitionPropertyCodeSeparateSecurityDomain,
            &prop,
            sizeof(WHV_PARTITION_PROPERTY));
        if (FAILED(hr)) {
            error_report("WHPX: failed to unset separate security domain, hr=%08lx", hr);
            /* Some old Windows 10 releases didn't have this, so not fatal*/
        }
    }

    hr = whp_dispatch.WHvSetPartitionProperty(
            whpx->partition,
            WHvPartitionPropertyCodeProcessorFeaturesBanks,
            &processor_features,
            sizeof(WHV_PROCESSOR_FEATURES_BANKS));
    if (FAILED(hr)) {
        error_report("WHPX: Failed to set processor features, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }


    /* Enable synthetic processor features */
    WHV_SYNTHETIC_PROCESSOR_FEATURES_BANKS synthetic_features;
    memset(&synthetic_features, 0, sizeof(WHV_SYNTHETIC_PROCESSOR_FEATURES_BANKS));
    synthetic_features.BanksCount = 1;

    synthetic_features.Bank0.HypervisorPresent = 1;
    synthetic_features.Bank0.Hv1 = 1;
    synthetic_features.Bank0.AccessVpRunTimeReg = 1;
    synthetic_features.Bank0.AccessPartitionReferenceCounter = 1;
    synthetic_features.Bank0.AccessPartitionReferenceTsc = 1;
    synthetic_features.Bank0.AccessHypercallRegs = 1;
    synthetic_features.Bank0.AccessFrequencyRegs = 1;
    synthetic_features.Bank0.AccessVpIndex = 1;

    if (whpx_irqchip_in_kernel()) {
        synthetic_features.Bank0.AccessSynicRegs = 1;
        synthetic_features.Bank0.AccessSyntheticTimerRegs = 1;
        synthetic_features.Bank0.AccessIntrCtrlRegs = 1;
        synthetic_features.Bank0.SyntheticClusterIpi = 1;
        synthetic_features.Bank0.DirectSyntheticTimers = 1;
        synthetic_features.Bank0.AccessGuestIdleReg = 1;
        /*
         * These technically work without the Hyper-V LAPIC
         * but behave oddly for multi-core VMs.
         */
        synthetic_features.Bank0.TbFlushHypercalls = 1;
        synthetic_features.Bank0.EnableExtendedGvaRangesForFlushVirtualAddressList = 1;
    }

    if (is_modern_os && whpx->hyperv_enlightenments_allowed) {
        whpx->hyperv_enlightenments_enabled = true;
        hr = whp_dispatch.WHvSetPartitionProperty(
                whpx->partition,
                WHvPartitionPropertyCodeSyntheticProcessorFeaturesBanks,
                &synthetic_features,
                sizeof(WHV_SYNTHETIC_PROCESSOR_FEATURES_BANKS));
        if (FAILED(hr)) {
            error_report("WHPX: Failed to set synthetic features, hr=%08lx", hr);
            ret = -EINVAL;
            goto error;
        }
    } else if (!is_modern_os && whpx->hyperv_enlightenments_required) {
        error_report("Hyper-V enlightenments not available on legacy Windows");
        ret = -EINVAL;
        goto error;
    }

    memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
    prop.X64MsrExitBitmap.UnhandledMsrs = 1;
    prop.X64MsrExitBitmap.ApicBaseMsrWrite = 1;

    hr = whp_dispatch.WHvSetPartitionProperty(
            whpx->partition,
            WHvPartitionPropertyCodeX64MsrExitBitmap,
            &prop,
            sizeof(WHV_PARTITION_PROPERTY));
    if (FAILED(hr)) {
        error_report("WHPX: Failed to set MSR exit bitmap, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    hr = whp_dispatch.WHvSetPartitionProperty(
        whpx->partition,
        WHvPartitionPropertyCodeCpuidExitList,
        cpuidExitList,
        RTL_NUMBER_OF(cpuidExitList) * sizeof(UINT32));

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set partition CpuidExitList hr=%08lx",
                     hr);
        ret = -EINVAL;
        goto error;
    }

    /*
     * We do not want to intercept any exceptions from the guest,
     * until we actually start debugging with gdb.
     */
    whpx->exception_exit_bitmap = -1;
    hr = whpx_set_exception_exit_bitmap(0);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set exception exit bitmap, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    hr = whp_dispatch.WHvSetupPartition(whpx->partition);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to setup partition, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    whpx_memory_init();
    whpx_init_emu();

    return 0;

error:

    if (NULL != whpx->partition) {
        whp_dispatch.WHvDeletePartition(whpx->partition);
        whpx->partition = NULL;
    }

    return ret;
}
