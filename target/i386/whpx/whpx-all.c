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
#include "exec/address-spaces.h"
#include "exec/ioport.h"
#include "qemu-common.h"
#include "qemu/accel.h"
#include "sysemu/whpx.h"
#include "sysemu/cpus.h"
#include "sysemu/runstate.h"
#include "qemu/main-loop.h"
#include "hw/boards.h"
#include "hw/i386/ioapic.h"
#include "hw/i386/apic_internal.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-common.h"
#include "qapi/qapi-visit-common.h"
#include "migration/blocker.h"
#include <winerror.h>

#include "whpx-internal.h"
#include "whpx-accel-ops.h"

#include <WinHvPlatform.h>
#include <WinHvEmulation.h>

#define HYPERV_APIC_BUS_FREQUENCY      (200000000ULL)

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
    WHvX64RegisterCr8,

    /* X64 Debug Registers */
    /*
     * WHvX64RegisterDr0,
     * WHvX64RegisterDr1,
     * WHvX64RegisterDr2,
     * WHvX64RegisterDr3,
     * WHvX64RegisterDr6,
     * WHvX64RegisterDr7,
     */

    /* X64 Floating Point and Vector Registers */
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

    /* X64 MSRs */
    WHvX64RegisterEfer,
#ifdef TARGET_X86_64
    WHvX64RegisterKernelGsBase,
#endif
    WHvX64RegisterApicBase,
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

struct whpx_register_set {
    WHV_REGISTER_VALUE values[RTL_NUMBER_OF(whpx_register_names)];
};

struct whpx_vcpu {
    WHV_EMULATOR_HANDLE emulator;
    bool window_registered;
    bool interruptable;
    bool ready_for_pic_interrupt;
    uint64_t tpr;
    uint64_t apic_base;
    bool interruption_pending;

    /* Must be the last field as it may have a tail */
    WHV_RUN_VP_EXIT_CONTEXT exit_ctx;
};

static bool whpx_allowed;
static bool whp_dispatch_initialized;
static HMODULE hWinHvPlatform, hWinHvEmulation;
static uint32_t max_vcpu_index;
struct whpx_state whpx_global;
struct WHPDispatch whp_dispatch;


/*
 * VP support
 */

static struct whpx_vcpu *get_whpx_vcpu(CPUState *cpu)
{
    return (struct whpx_vcpu *)cpu->hax_vcpu;
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

static int whpx_set_tsc(CPUState *cpu)
{
    CPUX86State *env = cpu->env_ptr;
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

    tsc_val.Reg64 = env->tsc;
    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index, &tsc_reg, 1, &tsc_val);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to set TSC, hr=%08lx", hr);
        return -1;
    }

    return 0;
}

/*
 * The CR8 register in the CPU is mapped to the TPR register of the APIC,
 * however, they use a slightly different encoding. Specifically:
 *
 *     APIC.TPR[bits 7:4] = CR8[bits 3:0]
 *
 * This mechanism is described in section 10.8.6.1 of Volume 3 of Intel 64
 * and IA-32 Architectures Software Developer's Manual.
 */

static uint64_t whpx_apic_tpr_to_cr8(uint64_t tpr)
{
    return tpr >> 4;
}

static void whpx_set_registers(CPUState *cpu, int level)
{
    struct whpx_state *whpx = &whpx_global;
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);
    CPUX86State *env = cpu->env_ptr;
    X86CPU *x86_cpu = X86_CPU(cpu);
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
    if (level >= WHPX_SET_RESET_STATE) {
        whpx_set_tsc(cpu);
    }

    memset(&vcxt, 0, sizeof(struct whpx_register_set));

    v86 = (env->eflags & VM_MASK);
    r86 = !(env->cr[0] & CR0_PE_MASK);

    vcpu->tpr = whpx_apic_tpr_to_cr8(cpu_get_apic_tpr(x86_cpu->apic_state));
    vcpu->apic_base = cpu_get_apic_base(x86_cpu->apic_state);

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
    vcxt.values[idx++].Reg64 = env->eflags;

    /* Translate 6+4 segment registers. HV and QEMU order matches  */
    assert(idx == WHvX64RegisterEs);
    for (i = 0; i < 6; i += 1, idx += 1) {
        vcxt.values[idx].Segment = whpx_seg_q2h(&env->segs[i], v86, r86);
    }

    assert(idx == WHvX64RegisterLdtr);
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
    assert(whpx_register_names[idx] == WHvX64RegisterCr8);
    vcxt.values[idx++].Reg64 = vcpu->tpr;

    /* 8 Debug Registers - Skipped */

    /* 16 XMM registers */
    assert(whpx_register_names[idx] == WHvX64RegisterXmm0);
    idx_next = idx + 16;
    for (i = 0; i < sizeof(env->xmm_regs) / sizeof(ZMMReg); i += 1, idx += 1) {
        vcxt.values[idx].Reg128.Low64 = env->xmm_regs[i].ZMM_Q(0);
        vcxt.values[idx].Reg128.High64 = env->xmm_regs[i].ZMM_Q(1);
    }
    idx = idx_next;

    /* 8 FP registers */
    assert(whpx_register_names[idx] == WHvX64RegisterFpMmx0);
    for (i = 0; i < 8; i += 1, idx += 1) {
        vcxt.values[idx].Fp.AsUINT128.Low64 = env->fpregs[i].mmx.MMX_Q(0);
        /* vcxt.values[idx].Fp.AsUINT128.High64 =
               env->fpregs[i].mmx.MMX_Q(1);
        */
    }

    /* FP control status register */
    assert(whpx_register_names[idx] == WHvX64RegisterFpControlStatus);
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
    assert(whpx_register_names[idx] == WHvX64RegisterXmmControlStatus);
    vcxt.values[idx].XmmControlStatus.LastFpRdp = 0;
    vcxt.values[idx].XmmControlStatus.XmmStatusControl = env->mxcsr;
    vcxt.values[idx].XmmControlStatus.XmmStatusControlMask = 0x0000ffff;
    idx += 1;

    /* MSRs */
    assert(whpx_register_names[idx] == WHvX64RegisterEfer);
    vcxt.values[idx++].Reg64 = env->efer;
#ifdef TARGET_X86_64
    assert(whpx_register_names[idx] == WHvX64RegisterKernelGsBase);
    vcxt.values[idx++].Reg64 = env->kernelgsbase;
#endif

    assert(whpx_register_names[idx] == WHvX64RegisterApicBase);
    vcxt.values[idx++].Reg64 = vcpu->apic_base;

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

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index,
        whpx_register_names,
        RTL_NUMBER_OF(whpx_register_names),
        &vcxt.values[0]);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set virtual processor context, hr=%08lx",
                     hr);
    }

    return;
}

static int whpx_get_tsc(CPUState *cpu)
{
    CPUX86State *env = cpu->env_ptr;
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

    env->tsc = tsc_val.Reg64;
    return 0;
}

static void whpx_get_registers(CPUState *cpu)
{
    struct whpx_state *whpx = &whpx_global;
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);
    CPUX86State *env = cpu->env_ptr;
    X86CPU *x86_cpu = X86_CPU(cpu);
    struct whpx_register_set vcxt;
    uint64_t tpr, apic_base;
    HRESULT hr;
    int idx;
    int idx_next;
    int i;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

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

    if (whpx_apic_in_platform()) {
        /*
         * Fetch the TPR value from the emulated APIC. It may get overwritten
         * below with the value from CR8 returned by
         * WHvGetVirtualProcessorRegisters().
         */
        whpx_apic_get(x86_cpu->apic_state);
        vcpu->tpr = whpx_apic_tpr_to_cr8(
            cpu_get_apic_tpr(x86_cpu->apic_state));
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
    assert(whpx_register_names[idx] == WHvX64RegisterCr8);
    tpr = vcxt.values[idx++].Reg64;
    if (tpr != vcpu->tpr) {
        vcpu->tpr = tpr;
        cpu_set_apic_tpr(x86_cpu->apic_state, tpr);
    }

    /* 8 Debug Registers - Skipped */

    /* 16 XMM registers */
    assert(whpx_register_names[idx] == WHvX64RegisterXmm0);
    idx_next = idx + 16;
    for (i = 0; i < sizeof(env->xmm_regs) / sizeof(ZMMReg); i += 1, idx += 1) {
        env->xmm_regs[i].ZMM_Q(0) = vcxt.values[idx].Reg128.Low64;
        env->xmm_regs[i].ZMM_Q(1) = vcxt.values[idx].Reg128.High64;
    }
    idx = idx_next;

    /* 8 FP registers */
    assert(whpx_register_names[idx] == WHvX64RegisterFpMmx0);
    for (i = 0; i < 8; i += 1, idx += 1) {
        env->fpregs[i].mmx.MMX_Q(0) = vcxt.values[idx].Fp.AsUINT128.Low64;
        /* env->fpregs[i].mmx.MMX_Q(1) =
               vcxt.values[idx].Fp.AsUINT128.High64;
        */
    }

    /* FP control status register */
    assert(whpx_register_names[idx] == WHvX64RegisterFpControlStatus);
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
    assert(whpx_register_names[idx] == WHvX64RegisterXmmControlStatus);
    env->mxcsr = vcxt.values[idx].XmmControlStatus.XmmStatusControl;
    idx += 1;

    /* MSRs */
    assert(whpx_register_names[idx] == WHvX64RegisterEfer);
    env->efer = vcxt.values[idx++].Reg64;
#ifdef TARGET_X86_64
    assert(whpx_register_names[idx] == WHvX64RegisterKernelGsBase);
    env->kernelgsbase = vcxt.values[idx++].Reg64;
#endif

    assert(whpx_register_names[idx] == WHvX64RegisterApicBase);
    apic_base = vcxt.values[idx++].Reg64;
    if (apic_base != vcpu->apic_base) {
        vcpu->apic_base = apic_base;
        cpu_set_apic_base(x86_cpu->apic_state, vcpu->apic_base);
    }

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

    if (whpx_apic_in_platform()) {
        whpx_apic_get(x86_cpu->apic_state);
    }

    x86_update_hflags(env);

    return;
}

static HRESULT CALLBACK whpx_emu_ioport_callback(
    void *ctx,
    WHV_EMULATOR_IO_ACCESS_INFO *IoAccess)
{
    MemTxAttrs attrs = { 0 };
    address_space_rw(&address_space_io, IoAccess->Port, attrs,
                     &IoAccess->Data, IoAccess->AccessSize,
                     IoAccess->Direction);
    return S_OK;
}

static HRESULT CALLBACK whpx_emu_mmio_callback(
    void *ctx,
    WHV_EMULATOR_MEMORY_ACCESS_INFO *ma)
{
    cpu_physical_memory_rw(ma->GpaAddress, ma->Data, ma->AccessSize,
                           ma->Direction);
    return S_OK;
}

static HRESULT CALLBACK whpx_emu_getreg_callback(
    void *ctx,
    const WHV_REGISTER_NAME *RegisterNames,
    UINT32 RegisterCount,
    WHV_REGISTER_VALUE *RegisterValues)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    CPUState *cpu = (CPUState *)ctx;

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index,
        RegisterNames, RegisterCount,
        RegisterValues);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to get virtual processor registers,"
                     " hr=%08lx", hr);
    }

    return hr;
}

static HRESULT CALLBACK whpx_emu_setreg_callback(
    void *ctx,
    const WHV_REGISTER_NAME *RegisterNames,
    UINT32 RegisterCount,
    const WHV_REGISTER_VALUE *RegisterValues)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    CPUState *cpu = (CPUState *)ctx;

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
        whpx->partition, cpu->cpu_index,
        RegisterNames, RegisterCount,
        RegisterValues);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to set virtual processor registers,"
                     " hr=%08lx", hr);
    }

    /*
     * The emulator just successfully wrote the register state. We clear the
     * dirty state so we avoid the double write on resume of the VP.
     */
    cpu->vcpu_dirty = false;

    return hr;
}

static HRESULT CALLBACK whpx_emu_translate_callback(
    void *ctx,
    WHV_GUEST_VIRTUAL_ADDRESS Gva,
    WHV_TRANSLATE_GVA_FLAGS TranslateFlags,
    WHV_TRANSLATE_GVA_RESULT_CODE *TranslationResult,
    WHV_GUEST_PHYSICAL_ADDRESS *Gpa)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    CPUState *cpu = (CPUState *)ctx;
    WHV_TRANSLATE_GVA_RESULT res;

    hr = whp_dispatch.WHvTranslateGva(whpx->partition, cpu->cpu_index,
                                      Gva, TranslateFlags, &res, Gpa);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to translate GVA, hr=%08lx", hr);
    } else {
        *TranslationResult = res.ResultCode;
    }

    return hr;
}

static const WHV_EMULATOR_CALLBACKS whpx_emu_callbacks = {
    .Size = sizeof(WHV_EMULATOR_CALLBACKS),
    .WHvEmulatorIoPortCallback = whpx_emu_ioport_callback,
    .WHvEmulatorMemoryCallback = whpx_emu_mmio_callback,
    .WHvEmulatorGetVirtualProcessorRegisters = whpx_emu_getreg_callback,
    .WHvEmulatorSetVirtualProcessorRegisters = whpx_emu_setreg_callback,
    .WHvEmulatorTranslateGvaPage = whpx_emu_translate_callback,
};

static int whpx_handle_mmio(CPUState *cpu, WHV_MEMORY_ACCESS_CONTEXT *ctx)
{
    HRESULT hr;
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);
    WHV_EMULATOR_STATUS emu_status;

    hr = whp_dispatch.WHvEmulatorTryMmioEmulation(
        vcpu->emulator, cpu,
        &vcpu->exit_ctx.VpContext, ctx,
        &emu_status);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to parse MMIO access, hr=%08lx", hr);
        return -1;
    }

    if (!emu_status.EmulationSuccessful) {
        error_report("WHPX: Failed to emulate MMIO access with"
                     " EmulatorReturnStatus: %u", emu_status.AsUINT32);
        return -1;
    }

    return 0;
}

static int whpx_handle_portio(CPUState *cpu,
                              WHV_X64_IO_PORT_ACCESS_CONTEXT *ctx)
{
    HRESULT hr;
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);
    WHV_EMULATOR_STATUS emu_status;

    hr = whp_dispatch.WHvEmulatorTryIoEmulation(
        vcpu->emulator, cpu,
        &vcpu->exit_ctx.VpContext, ctx,
        &emu_status);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to parse PortIO access, hr=%08lx", hr);
        return -1;
    }

    if (!emu_status.EmulationSuccessful) {
        error_report("WHPX: Failed to emulate PortIO access with"
                     " EmulatorReturnStatus: %u", emu_status.AsUINT32);
        return -1;
    }

    return 0;
}

static int whpx_handle_halt(CPUState *cpu)
{
    CPUX86State *env = cpu->env_ptr;
    int ret = 0;

    qemu_mutex_lock_iothread();
    if (!((cpu->interrupt_request & CPU_INTERRUPT_HARD) &&
          (env->eflags & IF_MASK)) &&
        !(cpu->interrupt_request & CPU_INTERRUPT_NMI)) {
        cpu->exception_index = EXCP_HLT;
        cpu->halted = true;
        ret = 1;
    }
    qemu_mutex_unlock_iothread();

    return ret;
}

static void whpx_vcpu_pre_run(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);
    CPUX86State *env = cpu->env_ptr;
    X86CPU *x86_cpu = X86_CPU(cpu);
    int irq;
    uint8_t tpr;
    WHV_X64_PENDING_INTERRUPTION_REGISTER new_int;
    UINT32 reg_count = 0;
    WHV_REGISTER_VALUE reg_values[3];
    WHV_REGISTER_NAME reg_names[3];

    memset(&new_int, 0, sizeof(new_int));
    memset(reg_values, 0, sizeof(reg_values));

    qemu_mutex_lock_iothread();

    /* Inject NMI */
    if (!vcpu->interruption_pending &&
        cpu->interrupt_request & (CPU_INTERRUPT_NMI | CPU_INTERRUPT_SMI)) {
        if (cpu->interrupt_request & CPU_INTERRUPT_NMI) {
            cpu->interrupt_request &= ~CPU_INTERRUPT_NMI;
            vcpu->interruptable = false;
            new_int.InterruptionType = WHvX64PendingNmi;
            new_int.InterruptionPending = 1;
            new_int.InterruptionVector = 2;
        }
        if (cpu->interrupt_request & CPU_INTERRUPT_SMI) {
            cpu->interrupt_request &= ~CPU_INTERRUPT_SMI;
        }
    }

    /*
     * Force the VCPU out of its inner loop to process any INIT requests or
     * commit pending TPR access.
     */
    if (cpu->interrupt_request & (CPU_INTERRUPT_INIT | CPU_INTERRUPT_TPR)) {
        if ((cpu->interrupt_request & CPU_INTERRUPT_INIT) &&
            !(env->hflags & HF_SMM_MASK)) {
            cpu->exit_request = 1;
        }
        if (cpu->interrupt_request & CPU_INTERRUPT_TPR) {
            cpu->exit_request = 1;
        }
    }

    /* Get pending hard interruption or replay one that was overwritten */
    if (!whpx_apic_in_platform()) {
        if (!vcpu->interruption_pending &&
            vcpu->interruptable && (env->eflags & IF_MASK)) {
            assert(!new_int.InterruptionPending);
            if (cpu->interrupt_request & CPU_INTERRUPT_HARD) {
                cpu->interrupt_request &= ~CPU_INTERRUPT_HARD;
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
               (cpu->interrupt_request & CPU_INTERRUPT_HARD)) {
        cpu->interrupt_request &= ~CPU_INTERRUPT_HARD;
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
        }
     }

    /* Sync the TPR to the CR8 if was modified during the intercept */
    tpr = cpu_get_apic_tpr(x86_cpu->apic_state);
    if (tpr != vcpu->tpr) {
        vcpu->tpr = tpr;
        reg_values[reg_count].Reg64 = tpr;
        cpu->exit_request = 1;
        reg_names[reg_count] = WHvX64RegisterCr8;
        reg_count += 1;
    }

    /* Update the state of the interrupt delivery notification */
    if (!vcpu->window_registered &&
        cpu->interrupt_request & CPU_INTERRUPT_HARD) {
        reg_values[reg_count].DeliverabilityNotifications =
            (WHV_X64_DELIVERABILITY_NOTIFICATIONS_REGISTER) {
                .InterruptNotification = 1
            };
        vcpu->window_registered = 1;
        reg_names[reg_count] = WHvX64RegisterDeliverabilityNotifications;
        reg_count += 1;
    }

    qemu_mutex_unlock_iothread();
    vcpu->ready_for_pic_interrupt = false;

    if (reg_count) {
        hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
            whpx->partition, cpu->cpu_index,
            reg_names, reg_count, reg_values);
        if (FAILED(hr)) {
            error_report("WHPX: Failed to set interrupt state registers,"
                         " hr=%08lx", hr);
        }
    }

    return;
}

static void whpx_vcpu_post_run(CPUState *cpu)
{
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);
    CPUX86State *env = cpu->env_ptr;
    X86CPU *x86_cpu = X86_CPU(cpu);

    env->eflags = vcpu->exit_ctx.VpContext.Rflags;

    uint64_t tpr = vcpu->exit_ctx.VpContext.Cr8;
    if (vcpu->tpr != tpr) {
        vcpu->tpr = tpr;
        qemu_mutex_lock_iothread();
        cpu_set_apic_tpr(x86_cpu->apic_state, vcpu->tpr);
        qemu_mutex_unlock_iothread();
    }

    vcpu->interruption_pending =
        vcpu->exit_ctx.VpContext.ExecutionState.InterruptionPending;

    vcpu->interruptable =
        !vcpu->exit_ctx.VpContext.ExecutionState.InterruptShadow;

    return;
}

static void whpx_vcpu_process_async_events(CPUState *cpu)
{
    CPUX86State *env = cpu->env_ptr;
    X86CPU *x86_cpu = X86_CPU(cpu);
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);

    if ((cpu->interrupt_request & CPU_INTERRUPT_INIT) &&
        !(env->hflags & HF_SMM_MASK)) {
        whpx_cpu_synchronize_state(cpu);
        do_cpu_init(x86_cpu);
        vcpu->interruptable = true;
    }

    if (cpu->interrupt_request & CPU_INTERRUPT_POLL) {
        cpu->interrupt_request &= ~CPU_INTERRUPT_POLL;
        apic_poll_irq(x86_cpu->apic_state);
    }

    if (((cpu->interrupt_request & CPU_INTERRUPT_HARD) &&
         (env->eflags & IF_MASK)) ||
        (cpu->interrupt_request & CPU_INTERRUPT_NMI)) {
        cpu->halted = false;
    }

    if (cpu->interrupt_request & CPU_INTERRUPT_SIPI) {
        whpx_cpu_synchronize_state(cpu);
        do_cpu_sipi(x86_cpu);
    }

    if (cpu->interrupt_request & CPU_INTERRUPT_TPR) {
        cpu->interrupt_request &= ~CPU_INTERRUPT_TPR;
        whpx_cpu_synchronize_state(cpu);
        apic_handle_tpr_access_report(x86_cpu->apic_state, env->eip,
                                      env->tpr_access_type);
    }

    return;
}

static int whpx_vcpu_run(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);
    int ret;

    whpx_vcpu_process_async_events(cpu);
    if (cpu->halted && !whpx_apic_in_platform()) {
        cpu->exception_index = EXCP_HLT;
        qatomic_set(&cpu->exit_request, false);
        return 0;
    }

    qemu_mutex_unlock_iothread();
    cpu_exec_start(cpu);

    do {
        if (cpu->vcpu_dirty) {
            whpx_set_registers(cpu, WHPX_SET_RUNTIME_STATE);
            cpu->vcpu_dirty = false;
        }

        whpx_vcpu_pre_run(cpu);

        if (qatomic_read(&cpu->exit_request)) {
            whpx_vcpu_kick(cpu);
        }

        hr = whp_dispatch.WHvRunVirtualProcessor(
            whpx->partition, cpu->cpu_index,
            &vcpu->exit_ctx, sizeof(vcpu->exit_ctx));

        if (FAILED(hr)) {
            error_report("WHPX: Failed to exec a virtual processor,"
                         " hr=%08lx", hr);
            ret = -1;
            break;
        }

        whpx_vcpu_post_run(cpu);

        switch (vcpu->exit_ctx.ExitReason) {
        case WHvRunVpExitReasonMemoryAccess:
            ret = whpx_handle_mmio(cpu, &vcpu->exit_ctx.MemoryAccess);
            break;

        case WHvRunVpExitReasonX64IoPortAccess:
            ret = whpx_handle_portio(cpu, &vcpu->exit_ctx.IoPortAccess);
            break;

        case WHvRunVpExitReasonX64InterruptWindow:
            vcpu->ready_for_pic_interrupt = 1;
            vcpu->window_registered = 0;
            ret = 0;
            break;

        case WHvRunVpExitReasonX64ApicEoi:
            assert(whpx_apic_in_platform());
            ioapic_eoi_broadcast(vcpu->exit_ctx.ApicEoi.InterruptVector);
            break;

        case WHvRunVpExitReasonX64Halt:
            ret = whpx_handle_halt(cpu);
            break;

        case WHvRunVpExitReasonX64ApicInitSipiTrap: {
            WHV_INTERRUPT_CONTROL ipi = {0};
            uint64_t icr = vcpu->exit_ctx.ApicInitSipi.ApicIcr;
            uint32_t delivery_mode =
                (icr & APIC_ICR_DELIV_MOD) >> APIC_ICR_DELIV_MOD_SHIFT;
            int dest_shorthand =
                (icr & APIC_ICR_DEST_SHORT) >> APIC_ICR_DEST_SHORT_SHIFT;
            bool broadcast = false;
            bool include_self = false;
            uint32_t i;

            /* We only registered for INIT and SIPI exits. */
            if ((delivery_mode != APIC_DM_INIT) &&
                (delivery_mode != APIC_DM_SIPI)) {
                error_report(
                    "WHPX: Unexpected APIC exit that is not a INIT or SIPI");
                break;
            }

            if (delivery_mode == APIC_DM_INIT) {
                ipi.Type = WHvX64InterruptTypeInit;
            } else {
                ipi.Type = WHvX64InterruptTypeSipi;
            }

            ipi.DestinationMode =
                ((icr & APIC_ICR_DEST_MOD) >> APIC_ICR_DEST_MOD_SHIFT) ?
                    WHvX64InterruptDestinationModeLogical :
                    WHvX64InterruptDestinationModePhysical;

            ipi.TriggerMode =
                ((icr & APIC_ICR_TRIGGER_MOD) >> APIC_ICR_TRIGGER_MOD_SHIFT) ?
                    WHvX64InterruptTriggerModeLevel :
                    WHvX64InterruptTriggerModeEdge;

            ipi.Vector = icr & APIC_VECTOR_MASK;
            switch (dest_shorthand) {
            /* no shorthand. Bits 56-63 contain the destination. */
            case 0:
                ipi.Destination = (icr >> 56) & APIC_VECTOR_MASK;
                hr = whp_dispatch.WHvRequestInterrupt(whpx->partition,
                        &ipi, sizeof(ipi));
                if (FAILED(hr)) {
                    error_report("WHPX: Failed to request interrupt  hr=%08lx",
                        hr);
                }

                break;

            /* self */
            case 1:
                include_self = true;
                break;

            /* broadcast, including self */
            case 2:
                broadcast = true;
                include_self = true;
                break;

            /* broadcast, excluding self */
            case 3:
                broadcast = true;
                break;
            }

            if (!broadcast && !include_self) {
                break;
            }

            for (i = 0; i <= max_vcpu_index; i++) {
                if (i == cpu->cpu_index && !include_self) {
                    continue;
                }

                /*
                 * Assuming that APIC Ids are identity mapped since
                 * WHvX64RegisterApicId & WHvX64RegisterInitialApicId registers
                 * are not handled yet and the hypervisor doesn't allow the
                 * guest to modify the APIC ID.
                 */
                ipi.Destination = i;
                hr = whp_dispatch.WHvRequestInterrupt(whpx->partition,
                        &ipi, sizeof(ipi));
                if (FAILED(hr)) {
                    error_report(
                        "WHPX: Failed to request SIPI for %d,  hr=%08lx",
                        i, hr);
                }
            }

            break;
        }

        case WHvRunVpExitReasonCanceled:
            cpu->exception_index = EXCP_INTERRUPT;
            ret = 1;
            break;

        case WHvRunVpExitReasonX64MsrAccess: {
            WHV_REGISTER_VALUE reg_values[3] = {0};
            WHV_REGISTER_NAME reg_names[3];
            UINT32 reg_count;

            reg_names[0] = WHvX64RegisterRip;
            reg_names[1] = WHvX64RegisterRax;
            reg_names[2] = WHvX64RegisterRdx;

            reg_values[0].Reg64 =
                vcpu->exit_ctx.VpContext.Rip +
                vcpu->exit_ctx.VpContext.InstructionLength;

            /*
             * For all unsupported MSR access we:
             *     ignore writes
             *     return 0 on read.
             */
            reg_count = vcpu->exit_ctx.MsrAccess.AccessInfo.IsWrite ?
                        1 : 3;

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
            WHV_REGISTER_VALUE reg_values[5];
            WHV_REGISTER_NAME reg_names[5];
            UINT32 reg_count = 5;
            UINT64 cpuid_fn, rip = 0, rax = 0, rcx = 0, rdx = 0, rbx = 0;
            X86CPU *x86_cpu = X86_CPU(cpu);
            CPUX86State *env = &x86_cpu->env;

            memset(reg_values, 0, sizeof(reg_values));

            rip = vcpu->exit_ctx.VpContext.Rip +
                  vcpu->exit_ctx.VpContext.InstructionLength;
            cpuid_fn = vcpu->exit_ctx.CpuidAccess.Rax;

            /*
             * Ideally, these should be supplied to the hypervisor during VCPU
             * initialization and it should be able to satisfy this request.
             * But, currently, WHPX doesn't support setting CPUID values in the
             * hypervisor once the partition has been setup, which is too late
             * since VCPUs are realized later. For now, use the values from
             * QEMU to satisfy these requests, until WHPX adds support for
             * being able to set these values in the hypervisor at runtime.
             */
            cpu_x86_cpuid(env, cpuid_fn, 0, (UINT32 *)&rax, (UINT32 *)&rbx,
                (UINT32 *)&rcx, (UINT32 *)&rdx);
            switch (cpuid_fn) {
            case 0x40000000:
                /* Expose the vmware cpu frequency cpuid leaf */
                rax = 0x40000010;
                rbx = rcx = rdx = 0;
                break;

            case 0x40000010:
                rax = env->tsc_khz;
                rbx = env->apic_bus_freq / 1000; /* Hz to KHz */
                rcx = rdx = 0;
                break;

            case 0x80000001:
                /* Remove any support of OSVW */
                rcx &= ~CPUID_EXT3_OSVW;
                break;
            }

            reg_names[0] = WHvX64RegisterRip;
            reg_names[1] = WHvX64RegisterRax;
            reg_names[2] = WHvX64RegisterRcx;
            reg_names[3] = WHvX64RegisterRdx;
            reg_names[4] = WHvX64RegisterRbx;

            reg_values[0].Reg64 = rip;
            reg_values[1].Reg64 = rax;
            reg_values[2].Reg64 = rcx;
            reg_values[3].Reg64 = rdx;
            reg_values[4].Reg64 = rbx;

            hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
                whpx->partition, cpu->cpu_index,
                reg_names,
                reg_count,
                reg_values);

            if (FAILED(hr)) {
                error_report("WHPX: Failed to set CpuidAccess state registers,"
                             " hr=%08lx", hr);
            }
            ret = 0;
            break;
        }
        case WHvRunVpExitReasonNone:
        case WHvRunVpExitReasonUnrecoverableException:
        case WHvRunVpExitReasonInvalidVpRegisterValue:
        case WHvRunVpExitReasonUnsupportedFeature:
        case WHvRunVpExitReasonException:
        default:
            error_report("WHPX: Unexpected VP exit code %d",
                         vcpu->exit_ctx.ExitReason);
            whpx_get_registers(cpu);
            qemu_mutex_lock_iothread();
            qemu_system_guest_panicked(cpu_get_crash_info(cpu));
            qemu_mutex_unlock_iothread();
            break;
        }

    } while (!ret);

    cpu_exec_end(cpu);
    qemu_mutex_lock_iothread();
    current_cpu = cpu;

    qatomic_set(&cpu->exit_request, false);

    return ret < 0;
}

static void do_whpx_cpu_synchronize_state(CPUState *cpu, run_on_cpu_data arg)
{
    if (!cpu->vcpu_dirty) {
        whpx_get_registers(cpu);
        cpu->vcpu_dirty = true;
    }
}

static void do_whpx_cpu_synchronize_post_reset(CPUState *cpu,
                                               run_on_cpu_data arg)
{
    whpx_set_registers(cpu, WHPX_SET_RESET_STATE);
    cpu->vcpu_dirty = false;
}

static void do_whpx_cpu_synchronize_post_init(CPUState *cpu,
                                              run_on_cpu_data arg)
{
    whpx_set_registers(cpu, WHPX_SET_FULL_STATE);
    cpu->vcpu_dirty = false;
}

static void do_whpx_cpu_synchronize_pre_loadvm(CPUState *cpu,
                                               run_on_cpu_data arg)
{
    cpu->vcpu_dirty = true;
}

/*
 * CPU support.
 */

void whpx_cpu_synchronize_state(CPUState *cpu)
{
    if (!cpu->vcpu_dirty) {
        run_on_cpu(cpu, do_whpx_cpu_synchronize_state, RUN_ON_CPU_NULL);
    }
}

void whpx_cpu_synchronize_post_reset(CPUState *cpu)
{
    run_on_cpu(cpu, do_whpx_cpu_synchronize_post_reset, RUN_ON_CPU_NULL);
}

void whpx_cpu_synchronize_post_init(CPUState *cpu)
{
    run_on_cpu(cpu, do_whpx_cpu_synchronize_post_init, RUN_ON_CPU_NULL);
}

void whpx_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    run_on_cpu(cpu, do_whpx_cpu_synchronize_pre_loadvm, RUN_ON_CPU_NULL);
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
    struct whpx_vcpu *vcpu = NULL;
    Error *local_error = NULL;
    CPUX86State *env = cpu->env_ptr;
    X86CPU *x86_cpu = X86_CPU(cpu);
    UINT64 freq = 0;
    int ret;

    /* Add migration blockers for all unsupported features of the
     * Windows Hypervisor Platform
     */
    if (whpx_migration_blocker == NULL) {
        error_setg(&whpx_migration_blocker,
               "State blocked due to non-migratable CPUID feature support,"
               "dirty memory tracking support, and XSAVE/XRSTOR support");

        if (migrate_add_blocker(whpx_migration_blocker, &local_error) < 0) {
            error_report_err(local_error);
            error_free(whpx_migration_blocker);
            ret = -EINVAL;
            goto error;
        }
    }

    vcpu = g_new0(struct whpx_vcpu, 1);

    if (!vcpu) {
        error_report("WHPX: Failed to allocte VCPU context.");
        ret = -ENOMEM;
        goto error;
    }

    hr = whp_dispatch.WHvEmulatorCreateEmulator(
        &whpx_emu_callbacks,
        &vcpu->emulator);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to setup instruction completion support,"
                     " hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    hr = whp_dispatch.WHvCreateVirtualProcessor(
        whpx->partition, cpu->cpu_index, 0);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to create a virtual processor,"
                     " hr=%08lx", hr);
        whp_dispatch.WHvEmulatorDestroyEmulator(vcpu->emulator);
        ret = -EINVAL;
        goto error;
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

    /*
     * If the vmware cpuid frequency leaf option is set, and we have a valid
     * tsc value, trap the corresponding cpuid's.
     */
    if (x86_cpu->vmware_cpuid_freq && env->tsc_khz) {
        UINT32 cpuidExitList[] = {1, 0x80000001, 0x40000000, 0x40000010};

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
    }

    vcpu->interruptable = true;
    cpu->vcpu_dirty = true;
    cpu->hax_vcpu = (struct hax_vcpu_state *)vcpu;
    max_vcpu_index = max(max_vcpu_index, cpu->cpu_index);
    qemu_add_vm_change_state_handler(whpx_cpu_update_state, cpu->env_ptr);

    return 0;

error:
    g_free(vcpu);

    return ret;
}

int whpx_vcpu_exec(CPUState *cpu)
{
    int ret;
    int fatal;

    for (;;) {
        if (cpu->exception_index >= EXCP_INTERRUPT) {
            ret = cpu->exception_index;
            cpu->exception_index = -1;
            break;
        }

        fatal = whpx_vcpu_run(cpu);

        if (fatal) {
            error_report("WHPX: Failed to exec a virtual processor");
            abort();
        }
    }

    return ret;
}

void whpx_destroy_vcpu(CPUState *cpu)
{
    struct whpx_state *whpx = &whpx_global;
    struct whpx_vcpu *vcpu = get_whpx_vcpu(cpu);

    whp_dispatch.WHvDeleteVirtualProcessor(whpx->partition, cpu->cpu_index);
    whp_dispatch.WHvEmulatorDestroyEmulator(vcpu->emulator);
    g_free(cpu->hax_vcpu);
    return;
}

void whpx_vcpu_kick(CPUState *cpu)
{
    struct whpx_state *whpx = &whpx_global;
    whp_dispatch.WHvCancelRunVirtualProcessor(
        whpx->partition, cpu->cpu_index, 0);
}

/*
 * Memory support.
 */

static void whpx_update_mapping(hwaddr start_pa, ram_addr_t size,
                                void *host_va, int add, int rom,
                                const char *name)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;

    /*
    if (add) {
        printf("WHPX: ADD PA:%p Size:%p, Host:%p, %s, '%s'\n",
               (void*)start_pa, (void*)size, host_va,
               (rom ? "ROM" : "RAM"), name);
    } else {
        printf("WHPX: DEL PA:%p Size:%p, Host:%p,      '%s'\n",
               (void*)start_pa, (void*)size, host_va, name);
    }
    */

    if (add) {
        hr = whp_dispatch.WHvMapGpaRange(whpx->partition,
                                         host_va,
                                         start_pa,
                                         size,
                                         (WHvMapGpaRangeFlagRead |
                                          WHvMapGpaRangeFlagExecute |
                                          (rom ? 0 : WHvMapGpaRangeFlagWrite)));
    } else {
        hr = whp_dispatch.WHvUnmapGpaRange(whpx->partition,
                                           start_pa,
                                           size);
    }

    if (FAILED(hr)) {
        error_report("WHPX: Failed to %s GPA range '%s' PA:%p, Size:%p bytes,"
                     " Host:%p, hr=%08lx",
                     (add ? "MAP" : "UNMAP"), name,
                     (void *)(uintptr_t)start_pa, (void *)size, host_va, hr);
    }
}

static void whpx_process_section(MemoryRegionSection *section, int add)
{
    MemoryRegion *mr = section->mr;
    hwaddr start_pa = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    unsigned int delta;
    uint64_t host_va;

    if (!memory_region_is_ram(mr)) {
        return;
    }

    delta = qemu_real_host_page_size - (start_pa & ~qemu_real_host_page_mask);
    delta &= ~qemu_real_host_page_mask;
    if (delta > size) {
        return;
    }
    start_pa += delta;
    size -= delta;
    size &= qemu_real_host_page_mask;
    if (!size || (start_pa & ~qemu_real_host_page_mask)) {
        return;
    }

    host_va = (uintptr_t)memory_region_get_ram_ptr(mr)
            + section->offset_within_region + delta;

    whpx_update_mapping(start_pa, size, (void *)(uintptr_t)host_va, add,
                        memory_region_is_rom(mr), mr->name);
}

static void whpx_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    memory_region_ref(section->mr);
    whpx_process_section(section, 1);
}

static void whpx_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    whpx_process_section(section, 0);
    memory_region_unref(section->mr);
}

static void whpx_transaction_begin(MemoryListener *listener)
{
}

static void whpx_transaction_commit(MemoryListener *listener)
{
}

static void whpx_log_sync(MemoryListener *listener,
                         MemoryRegionSection *section)
{
    MemoryRegion *mr = section->mr;

    if (!memory_region_is_ram(mr)) {
        return;
    }

    memory_region_set_dirty(mr, 0, int128_get64(section->size));
}

static MemoryListener whpx_memory_listener = {
    .name = "whpx",
    .begin = whpx_transaction_begin,
    .commit = whpx_transaction_commit,
    .region_add = whpx_region_add,
    .region_del = whpx_region_del,
    .log_sync = whpx_log_sync,
    .priority = 10,
};

static void whpx_memory_init(void)
{
    memory_listener_register(&whpx_memory_listener, &address_space_memory);
}

/*
 * Load the functions from the given library, using the given handle. If a
 * handle is provided, it is used, otherwise the library is opened. The
 * handle will be updated on return with the opened one.
 */
static bool load_whp_dispatch_fns(HMODULE *handle,
    WHPFunctionList function_list)
{
    HMODULE hLib = *handle;

    #define WINHV_PLATFORM_DLL "WinHvPlatform.dll"
    #define WINHV_EMULATION_DLL "WinHvEmulation.dll"
    #define WHP_LOAD_FIELD_OPTIONAL(return_type, function_name, signature) \
        whp_dispatch.function_name = \
            (function_name ## _t)GetProcAddress(hLib, #function_name); \

    #define WHP_LOAD_FIELD(return_type, function_name, signature) \
        whp_dispatch.function_name = \
            (function_name ## _t)GetProcAddress(hLib, #function_name); \
        if (!whp_dispatch.function_name) { \
            error_report("Could not load function %s", #function_name); \
            goto error; \
        } \

    #define WHP_LOAD_LIB(lib_name, handle_lib) \
    if (!handle_lib) { \
        handle_lib = LoadLibrary(lib_name); \
        if (!handle_lib) { \
            error_report("Could not load library %s.", lib_name); \
            goto error; \
        } \
    } \

    switch (function_list) {
    case WINHV_PLATFORM_FNS_DEFAULT:
        WHP_LOAD_LIB(WINHV_PLATFORM_DLL, hLib)
        LIST_WINHVPLATFORM_FUNCTIONS(WHP_LOAD_FIELD)
        break;

    case WINHV_EMULATION_FNS_DEFAULT:
        WHP_LOAD_LIB(WINHV_EMULATION_DLL, hLib)
        LIST_WINHVEMULATION_FUNCTIONS(WHP_LOAD_FIELD)
        break;

    case WINHV_PLATFORM_FNS_SUPPLEMENTAL:
        WHP_LOAD_LIB(WINHV_PLATFORM_DLL, hLib)
        LIST_WINHVPLATFORM_FUNCTIONS_SUPPLEMENTAL(WHP_LOAD_FIELD_OPTIONAL)
        break;
    }

    *handle = hLib;
    return true;

error:
    if (hLib) {
        FreeLibrary(hLib);
    }

    return false;
}

static void whpx_set_kernel_irqchip(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    struct whpx_state *whpx = &whpx_global;
    OnOffSplit mode;

    if (!visit_type_OnOffSplit(v, name, &mode, errp)) {
        return;
    }

    switch (mode) {
    case ON_OFF_SPLIT_ON:
        whpx->kernel_irqchip_allowed = true;
        whpx->kernel_irqchip_required = true;
        break;

    case ON_OFF_SPLIT_OFF:
        whpx->kernel_irqchip_allowed = false;
        whpx->kernel_irqchip_required = false;
        break;

    case ON_OFF_SPLIT_SPLIT:
        error_setg(errp, "WHPX: split irqchip currently not supported");
        error_append_hint(errp,
            "Try without kernel-irqchip or with kernel-irqchip=on|off");
        break;

    default:
        /*
         * The value was checked in visit_type_OnOffSplit() above. If
         * we get here, then something is wrong in QEMU.
         */
        abort();
    }
}

/*
 * Partition support
 */

static int whpx_accel_init(MachineState *ms)
{
    struct whpx_state *whpx;
    int ret;
    HRESULT hr;
    WHV_CAPABILITY whpx_cap;
    UINT32 whpx_cap_size;
    WHV_PARTITION_PROPERTY prop;
    UINT32 cpuidExitList[] = {1, 0x80000001};
    WHV_CAPABILITY_FEATURES features = {0};

    whpx = &whpx_global;

    if (!init_whp_dispatch()) {
        ret = -ENOSYS;
        goto error;
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

    memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
    prop.ProcessorCount = ms->smp.cpus;
    hr = whp_dispatch.WHvSetPartitionProperty(
        whpx->partition,
        WHvPartitionPropertyCodeProcessorCount,
        &prop,
        sizeof(WHV_PARTITION_PROPERTY));

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set partition core count to %d,"
                     " hr=%08lx", ms->smp.cores, hr);
        ret = -EINVAL;
        goto error;
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

    if (whpx->kernel_irqchip_allowed && features.LocalApicEmulation &&
        whp_dispatch.WHvSetVirtualProcessorInterruptControllerState2) {
        WHV_X64_LOCAL_APIC_EMULATION_MODE mode =
            WHvX64LocalApicEmulationModeXApic;
        printf("WHPX: setting APIC emulation mode in the hypervisor\n");
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
            whpx->apic_in_platform = true;
        }
    }

    /* Register for MSR and CPUID exits */
    memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
    prop.ExtendedVmExits.X64MsrExit = 1;
    prop.ExtendedVmExits.X64CpuidExit = 1;
    if (whpx_apic_in_platform()) {
        prop.ExtendedVmExits.X64ApicInitSipiExitTrap = 1;
    }

    hr = whp_dispatch.WHvSetPartitionProperty(
            whpx->partition,
            WHvPartitionPropertyCodeExtendedVmExits,
            &prop,
            sizeof(WHV_PARTITION_PROPERTY));
    if (FAILED(hr)) {
        error_report("WHPX: Failed to enable MSR & CPUIDexit, hr=%08lx", hr);
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

    hr = whp_dispatch.WHvSetupPartition(whpx->partition);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to setup partition, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    whpx_memory_init();

    printf("Windows Hypervisor Platform accelerator is operational\n");
    return 0;

error:

    if (NULL != whpx->partition) {
        whp_dispatch.WHvDeletePartition(whpx->partition);
        whpx->partition = NULL;
    }

    return ret;
}

int whpx_enabled(void)
{
    return whpx_allowed;
}

bool whpx_apic_in_platform(void) {
    return whpx_global.apic_in_platform;
}

static void whpx_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "WHPX";
    ac->init_machine = whpx_accel_init;
    ac->allowed = &whpx_allowed;

    object_class_property_add(oc, "kernel-irqchip", "on|off|split",
        NULL, whpx_set_kernel_irqchip,
        NULL, NULL);
    object_class_property_set_description(oc, "kernel-irqchip",
        "Configure WHPX in-kernel irqchip");
}

static void whpx_accel_instance_init(Object *obj)
{
    struct whpx_state *whpx = &whpx_global;

    memset(whpx, 0, sizeof(struct whpx_state));
    /* Turn on kernel-irqchip, by default */
    whpx->kernel_irqchip_allowed = true;
}

static const TypeInfo whpx_accel_type = {
    .name = ACCEL_CLASS_NAME("whpx"),
    .parent = TYPE_ACCEL,
    .instance_init = whpx_accel_instance_init,
    .class_init = whpx_accel_class_init,
};

static void whpx_type_init(void)
{
    type_register_static(&whpx_accel_type);
}

bool init_whp_dispatch(void)
{
    if (whp_dispatch_initialized) {
        return true;
    }

    if (!load_whp_dispatch_fns(&hWinHvPlatform, WINHV_PLATFORM_FNS_DEFAULT)) {
        goto error;
    }

    if (!load_whp_dispatch_fns(&hWinHvEmulation, WINHV_EMULATION_FNS_DEFAULT)) {
        goto error;
    }

    assert(load_whp_dispatch_fns(&hWinHvPlatform,
        WINHV_PLATFORM_FNS_SUPPLEMENTAL));
    whp_dispatch_initialized = true;

    return true;
error:
    if (hWinHvPlatform) {
        FreeLibrary(hWinHvPlatform);
    }

    if (hWinHvEmulation) {
        FreeLibrary(hWinHvEmulation);
    }

    return false;
}

type_init(whpx_type_init);
