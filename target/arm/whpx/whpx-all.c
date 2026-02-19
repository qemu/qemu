/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Windows Hypervisor Platform accelerator (WHPX)
 *
 * Copyright (c) 2025 Mohamed Mediouni
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "system/address-spaces.h"
#include "system/ioport.h"
#include "gdbstub/helpers.h"
#include "qemu/accel.h"
#include "accel/accel-ops.h"
#include "system/whpx.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "qemu/main-loop.h"
#include "hw/core/boards.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-common.h"
#include "qapi/qapi-visit-common.h"
#include "migration/blocker.h"
#include "accel/accel-cpu-target.h"
#include <winerror.h>

#include "syndrome.h"
#include "target/arm/cpregs.h"
#include "internals.h"

#include "system/whpx-internal.h"
#include "system/whpx-accel-ops.h"
#include "system/whpx-all.h"
#include "system/whpx-common.h"
#include "whpx_arm.h"
#include "hw/arm/bsa.h"
#include "arm-powerctl.h"

#include <winhvplatform.h>
#include <winhvplatformdefs.h>
#include <winreg.h>

typedef struct ARMHostCPUFeatures {
    ARMISARegisters isar;
    uint64_t features;
    uint64_t midr;
    uint32_t reset_sctlr;
    const char *dtb_compatible;
} ARMHostCPUFeatures;

static ARMHostCPUFeatures arm_host_cpu_features;

typedef struct WHPXRegMatch {
   WHV_REGISTER_NAME reg;
   uint64_t offset;
} WHPXRegMatch;

static const WHPXRegMatch whpx_reg_match[] = {
    { WHvArm64RegisterX0,   offsetof(CPUARMState, xregs[0]) },
    { WHvArm64RegisterX1,   offsetof(CPUARMState, xregs[1]) },
    { WHvArm64RegisterX2,   offsetof(CPUARMState, xregs[2]) },
    { WHvArm64RegisterX3,   offsetof(CPUARMState, xregs[3]) },
    { WHvArm64RegisterX4,   offsetof(CPUARMState, xregs[4]) },
    { WHvArm64RegisterX5,   offsetof(CPUARMState, xregs[5]) },
    { WHvArm64RegisterX6,   offsetof(CPUARMState, xregs[6]) },
    { WHvArm64RegisterX7,   offsetof(CPUARMState, xregs[7]) },
    { WHvArm64RegisterX8,   offsetof(CPUARMState, xregs[8]) },
    { WHvArm64RegisterX9,   offsetof(CPUARMState, xregs[9]) },
    { WHvArm64RegisterX10,  offsetof(CPUARMState, xregs[10]) },
    { WHvArm64RegisterX11,  offsetof(CPUARMState, xregs[11]) },
    { WHvArm64RegisterX12,  offsetof(CPUARMState, xregs[12]) },
    { WHvArm64RegisterX13,  offsetof(CPUARMState, xregs[13]) },
    { WHvArm64RegisterX14,  offsetof(CPUARMState, xregs[14]) },
    { WHvArm64RegisterX15,  offsetof(CPUARMState, xregs[15]) },
    { WHvArm64RegisterX16,  offsetof(CPUARMState, xregs[16]) },
    { WHvArm64RegisterX17,  offsetof(CPUARMState, xregs[17]) },
    { WHvArm64RegisterX18,  offsetof(CPUARMState, xregs[18]) },
    { WHvArm64RegisterX19,  offsetof(CPUARMState, xregs[19]) },
    { WHvArm64RegisterX20,  offsetof(CPUARMState, xregs[20]) },
    { WHvArm64RegisterX21,  offsetof(CPUARMState, xregs[21]) },
    { WHvArm64RegisterX22,  offsetof(CPUARMState, xregs[22]) },
    { WHvArm64RegisterX23,  offsetof(CPUARMState, xregs[23]) },
    { WHvArm64RegisterX24,  offsetof(CPUARMState, xregs[24]) },
    { WHvArm64RegisterX25,  offsetof(CPUARMState, xregs[25]) },
    { WHvArm64RegisterX26,  offsetof(CPUARMState, xregs[26]) },
    { WHvArm64RegisterX27,  offsetof(CPUARMState, xregs[27]) },
    { WHvArm64RegisterX28,  offsetof(CPUARMState, xregs[28]) },
    { WHvArm64RegisterFp,   offsetof(CPUARMState, xregs[29]) },
    { WHvArm64RegisterLr,   offsetof(CPUARMState, xregs[30]) },
    { WHvArm64RegisterPc,   offsetof(CPUARMState, pc) },
};

static const WHPXRegMatch whpx_fpreg_match[] = {
    { WHvArm64RegisterQ0,  offsetof(CPUARMState, vfp.zregs[0]) },
    { WHvArm64RegisterQ1,  offsetof(CPUARMState, vfp.zregs[1]) },
    { WHvArm64RegisterQ2,  offsetof(CPUARMState, vfp.zregs[2]) },
    { WHvArm64RegisterQ3,  offsetof(CPUARMState, vfp.zregs[3]) },
    { WHvArm64RegisterQ4,  offsetof(CPUARMState, vfp.zregs[4]) },
    { WHvArm64RegisterQ5,  offsetof(CPUARMState, vfp.zregs[5]) },
    { WHvArm64RegisterQ6,  offsetof(CPUARMState, vfp.zregs[6]) },
    { WHvArm64RegisterQ7,  offsetof(CPUARMState, vfp.zregs[7]) },
    { WHvArm64RegisterQ8,  offsetof(CPUARMState, vfp.zregs[8]) },
    { WHvArm64RegisterQ9,  offsetof(CPUARMState, vfp.zregs[9]) },
    { WHvArm64RegisterQ10, offsetof(CPUARMState, vfp.zregs[10]) },
    { WHvArm64RegisterQ11, offsetof(CPUARMState, vfp.zregs[11]) },
    { WHvArm64RegisterQ12, offsetof(CPUARMState, vfp.zregs[12]) },
    { WHvArm64RegisterQ13, offsetof(CPUARMState, vfp.zregs[13]) },
    { WHvArm64RegisterQ14, offsetof(CPUARMState, vfp.zregs[14]) },
    { WHvArm64RegisterQ15, offsetof(CPUARMState, vfp.zregs[15]) },
    { WHvArm64RegisterQ16, offsetof(CPUARMState, vfp.zregs[16]) },
    { WHvArm64RegisterQ17, offsetof(CPUARMState, vfp.zregs[17]) },
    { WHvArm64RegisterQ18, offsetof(CPUARMState, vfp.zregs[18]) },
    { WHvArm64RegisterQ19, offsetof(CPUARMState, vfp.zregs[19]) },
    { WHvArm64RegisterQ20, offsetof(CPUARMState, vfp.zregs[20]) },
    { WHvArm64RegisterQ21, offsetof(CPUARMState, vfp.zregs[21]) },
    { WHvArm64RegisterQ22, offsetof(CPUARMState, vfp.zregs[22]) },
    { WHvArm64RegisterQ23, offsetof(CPUARMState, vfp.zregs[23]) },
    { WHvArm64RegisterQ24, offsetof(CPUARMState, vfp.zregs[24]) },
    { WHvArm64RegisterQ25, offsetof(CPUARMState, vfp.zregs[25]) },
    { WHvArm64RegisterQ26, offsetof(CPUARMState, vfp.zregs[26]) },
    { WHvArm64RegisterQ27, offsetof(CPUARMState, vfp.zregs[27]) },
    { WHvArm64RegisterQ28, offsetof(CPUARMState, vfp.zregs[28]) },
    { WHvArm64RegisterQ29, offsetof(CPUARMState, vfp.zregs[29]) },
    { WHvArm64RegisterQ30, offsetof(CPUARMState, vfp.zregs[30]) },
    { WHvArm64RegisterQ31, offsetof(CPUARMState, vfp.zregs[31]) },
};

struct whpx_sreg_match {
    WHV_REGISTER_NAME reg;
    uint32_t key;
    bool global;
    uint32_t cp_idx;
};

static struct whpx_sreg_match whpx_sreg_match[] = {
    { WHvArm64RegisterDbgbvr0El1, ENCODE_AA64_CP_REG(0, 0, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr0El1, ENCODE_AA64_CP_REG(0, 0, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr0El1, ENCODE_AA64_CP_REG(0, 0, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr0El1, ENCODE_AA64_CP_REG(0, 0, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr0El1, ENCODE_AA64_CP_REG(0, 1, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr0El1, ENCODE_AA64_CP_REG(0, 1, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr0El1, ENCODE_AA64_CP_REG(0, 1, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr0El1, ENCODE_AA64_CP_REG(0, 1, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr2El1, ENCODE_AA64_CP_REG(0, 2, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr2El1, ENCODE_AA64_CP_REG(0, 2, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr2El1, ENCODE_AA64_CP_REG(0, 2, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr2El1, ENCODE_AA64_CP_REG(0, 2, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr3El1, ENCODE_AA64_CP_REG(0, 3, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr3El1, ENCODE_AA64_CP_REG(0, 3, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr3El1, ENCODE_AA64_CP_REG(0, 3, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr3El1, ENCODE_AA64_CP_REG(0, 3, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr4El1, ENCODE_AA64_CP_REG(0, 4, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr4El1, ENCODE_AA64_CP_REG(0, 4, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr4El1, ENCODE_AA64_CP_REG(0, 4, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr4El1, ENCODE_AA64_CP_REG(0, 4, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr5El1, ENCODE_AA64_CP_REG(0, 5, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr5El1, ENCODE_AA64_CP_REG(0, 5, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr5El1, ENCODE_AA64_CP_REG(0, 5, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr5El1, ENCODE_AA64_CP_REG(0, 5, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr6El1, ENCODE_AA64_CP_REG(0, 6, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr6El1, ENCODE_AA64_CP_REG(0, 6, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr6El1, ENCODE_AA64_CP_REG(0, 6, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr6El1, ENCODE_AA64_CP_REG(0, 6, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr7El1, ENCODE_AA64_CP_REG(0, 7, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr7El1, ENCODE_AA64_CP_REG(0, 7, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr7El1, ENCODE_AA64_CP_REG(0, 7, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr7El1, ENCODE_AA64_CP_REG(0, 7, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr8El1, ENCODE_AA64_CP_REG(0, 8, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr8El1, ENCODE_AA64_CP_REG(0, 8, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr8El1, ENCODE_AA64_CP_REG(0, 8, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr8El1, ENCODE_AA64_CP_REG(0, 8, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr9El1, ENCODE_AA64_CP_REG(0, 9, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr9El1, ENCODE_AA64_CP_REG(0, 9, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr9El1, ENCODE_AA64_CP_REG(0, 9, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr9El1, ENCODE_AA64_CP_REG(0, 9, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr10El1, ENCODE_AA64_CP_REG(0, 10, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr10El1, ENCODE_AA64_CP_REG(0, 10, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr10El1, ENCODE_AA64_CP_REG(0, 10, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr10El1, ENCODE_AA64_CP_REG(0, 10, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr11El1, ENCODE_AA64_CP_REG(0, 11, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr11El1, ENCODE_AA64_CP_REG(0, 11, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr11El1, ENCODE_AA64_CP_REG(0, 11, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr11El1, ENCODE_AA64_CP_REG(0, 11, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr12El1, ENCODE_AA64_CP_REG(0, 12, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr12El1, ENCODE_AA64_CP_REG(0, 12, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr12El1, ENCODE_AA64_CP_REG(0, 12, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr12El1, ENCODE_AA64_CP_REG(0, 12, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr13El1, ENCODE_AA64_CP_REG(0, 13, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr13El1, ENCODE_AA64_CP_REG(0, 13, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr13El1, ENCODE_AA64_CP_REG(0, 13, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr13El1, ENCODE_AA64_CP_REG(0, 13, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr14El1, ENCODE_AA64_CP_REG(0, 14, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr14El1, ENCODE_AA64_CP_REG(0, 14, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr14El1, ENCODE_AA64_CP_REG(0, 14, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr14El1, ENCODE_AA64_CP_REG(0, 14, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr15El1, ENCODE_AA64_CP_REG(0, 15, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr15El1, ENCODE_AA64_CP_REG(0, 15, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr15El1, ENCODE_AA64_CP_REG(0, 15, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr15El1, ENCODE_AA64_CP_REG(0, 15, 2, 0, 7) },
#ifdef SYNC_NO_RAW_REGS
    /*
     * The registers below are manually synced on init because they are
     * marked as NO_RAW. We still list them to make number space sync easier.
     */
    { WHvArm64RegisterMidrEl1, ENCODE_AA64_CP_REG(0, 0, 3, 0, 0) },
    { WHvArm64RegisterMpidrEl1, ENCODE_AA64_CP_REG(0, 0, 3, 0, 5) },
    { WHvArm64RegisterIdPfr0El1, ENCODE_AA64_CP_REG(0, 4, 3, 0, 0) },
#endif
    { WHvArm64RegisterIdAa64Pfr1El1, ENCODE_AA64_CP_REG(0, 4, 3, 0, 1), true },
    { WHvArm64RegisterIdAa64Dfr0El1, ENCODE_AA64_CP_REG(0, 5, 3, 0, 0), true },
    { WHvArm64RegisterIdAa64Dfr1El1, ENCODE_AA64_CP_REG(0, 5, 3, 0, 1), true },
    { WHvArm64RegisterIdAa64Isar0El1, ENCODE_AA64_CP_REG(0, 6, 3, 0, 0), true },
    { WHvArm64RegisterIdAa64Isar1El1, ENCODE_AA64_CP_REG(0, 6, 3, 0, 1), true },
#ifdef SYNC_NO_MMFR0
    /* We keep the hardware MMFR0 around. HW limits are there anyway */
    { WHvArm64RegisterIdAa64Mmfr0El1, ENCODE_AA64_CP_REG(0, 7, 3, 0, 0) },
#endif
    { WHvArm64RegisterIdAa64Mmfr1El1, ENCODE_AA64_CP_REG(0, 7, 3, 0, 1), true },
    { WHvArm64RegisterIdAa64Mmfr2El1, ENCODE_AA64_CP_REG(0, 7, 3, 0, 2), true },
    { WHvArm64RegisterIdAa64Mmfr3El1, ENCODE_AA64_CP_REG(0, 7, 3, 0, 3), true },

    { WHvArm64RegisterMdscrEl1, ENCODE_AA64_CP_REG(0, 2, 2, 0, 2) },
    { WHvArm64RegisterSctlrEl1, ENCODE_AA64_CP_REG(1, 0, 3, 0, 0) },
    { WHvArm64RegisterCpacrEl1, ENCODE_AA64_CP_REG(1, 0, 3, 0, 2) },
    { WHvArm64RegisterTtbr0El1, ENCODE_AA64_CP_REG(2, 0, 3, 0, 0) },
    { WHvArm64RegisterTtbr1El1, ENCODE_AA64_CP_REG(2, 0, 3, 0, 1) },
    { WHvArm64RegisterTcrEl1, ENCODE_AA64_CP_REG(2, 0, 3, 0, 2) },

    { WHvArm64RegisterApiAKeyLoEl1, ENCODE_AA64_CP_REG(2, 1, 3, 0, 0) },
    { WHvArm64RegisterApiAKeyHiEl1, ENCODE_AA64_CP_REG(2, 1, 3, 0, 1) },
    { WHvArm64RegisterApiBKeyLoEl1, ENCODE_AA64_CP_REG(2, 1, 3, 0, 2) },
    { WHvArm64RegisterApiBKeyHiEl1, ENCODE_AA64_CP_REG(2, 1, 3, 0, 3) },
    { WHvArm64RegisterApdAKeyLoEl1, ENCODE_AA64_CP_REG(2, 2, 3, 0, 0) },
    { WHvArm64RegisterApdAKeyHiEl1, ENCODE_AA64_CP_REG(2, 2, 3, 0, 1) },
    { WHvArm64RegisterApdBKeyLoEl1, ENCODE_AA64_CP_REG(2, 2, 3, 0, 2) },
    { WHvArm64RegisterApdBKeyHiEl1, ENCODE_AA64_CP_REG(2, 2, 3, 0, 3) },
    { WHvArm64RegisterApgAKeyLoEl1, ENCODE_AA64_CP_REG(2, 3, 3, 0, 0) },
    { WHvArm64RegisterApgAKeyHiEl1, ENCODE_AA64_CP_REG(2, 3, 3, 0, 1) },

    { WHvArm64RegisterSpsrEl1, ENCODE_AA64_CP_REG(4, 0, 3, 0, 0) },
    { WHvArm64RegisterElrEl1, ENCODE_AA64_CP_REG(4, 0, 3, 0, 1) },
    { WHvArm64RegisterSpEl1, ENCODE_AA64_CP_REG(4, 1, 3, 0, 0) },
    { WHvArm64RegisterEsrEl1, ENCODE_AA64_CP_REG(5, 2, 3, 0, 0) },
    { WHvArm64RegisterFarEl1, ENCODE_AA64_CP_REG(6, 0, 3, 0, 0) },
    { WHvArm64RegisterParEl1, ENCODE_AA64_CP_REG(7, 4, 3, 0, 0) },
    { WHvArm64RegisterMairEl1, ENCODE_AA64_CP_REG(10, 2, 3, 0, 0) },
    { WHvArm64RegisterVbarEl1, ENCODE_AA64_CP_REG(12, 0, 3, 0, 0) },
    { WHvArm64RegisterContextidrEl1, ENCODE_AA64_CP_REG(13, 0, 3, 0, 1) },
    { WHvArm64RegisterTpidrEl1, ENCODE_AA64_CP_REG(13, 0, 3, 0, 4) },
    { WHvArm64RegisterCntkctlEl1, ENCODE_AA64_CP_REG(14, 1, 3, 0, 0) },
    { WHvArm64RegisterCsselrEl1, ENCODE_AA64_CP_REG(0, 0, 3, 2, 0) },
    { WHvArm64RegisterTpidrEl0, ENCODE_AA64_CP_REG(13, 0, 3, 3, 2) },
    { WHvArm64RegisterTpidrroEl0, ENCODE_AA64_CP_REG(13, 0, 3, 3, 3) },
    { WHvArm64RegisterCntvCtlEl0, ENCODE_AA64_CP_REG(14, 3, 3, 3, 1) },
    { WHvArm64RegisterCntvCvalEl0, ENCODE_AA64_CP_REG(14, 3, 3, 3, 2) },
    { WHvArm64RegisterSpEl1, ENCODE_AA64_CP_REG(4, 1, 3, 4, 0) },
};

static void flush_cpu_state(CPUState *cpu)
{
    if (cpu->vcpu_dirty) {
        whpx_set_registers(cpu, WHPX_SET_RUNTIME_STATE);
        cpu->vcpu_dirty = false;
    }
}

HRESULT whpx_set_exception_exit_bitmap(UINT64 exceptions)
{
    if (exceptions != 0) {
        return E_NOTIMPL;
    }
    return ERROR_SUCCESS;
}
void whpx_apply_breakpoints(
    struct whpx_breakpoint_collection *breakpoints,
    CPUState *cpu,
    bool resuming)
{
    /* Breakpoints aren’t supported on this platform */
}
void whpx_translate_cpu_breakpoints(
    struct whpx_breakpoints *breakpoints,
    CPUState *cpu,
    int cpu_breakpoint_count)
{
    /* Breakpoints aren’t supported on this platform */
}

static void whpx_get_reg(CPUState *cpu, WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE* val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;

    flush_cpu_state(cpu);

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(whpx->partition, cpu->cpu_index,
         &reg, 1, val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to get register %08x, hr=%08lx", reg, hr);
    }
}

static void whpx_set_reg(CPUState *cpu, WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;
    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(whpx->partition, cpu->cpu_index,
         &reg, 1, &val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set register %08x, hr=%08lx", reg, hr);
    }
}

static void whpx_get_global_reg(WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE *val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(whpx->partition, WHV_ANY_VP,
         &reg, 1, val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to get register %08x, hr=%08lx", reg, hr);
    }
}

static void whpx_set_global_reg(WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;
    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(whpx->partition, WHV_ANY_VP,
         &reg, 1, &val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set register %08x, hr=%08lx", reg, hr);
    }
}

static uint64_t whpx_get_gp_reg(CPUState *cpu, int rt)
{
    assert(rt <= 31);
    if (rt == 31) {
        return 0;
    }
    WHV_REGISTER_NAME reg = WHvArm64RegisterX0 + rt;
    WHV_REGISTER_VALUE val;
    whpx_get_reg(cpu, reg, &val);

    return val.Reg64;
}

static void whpx_set_gp_reg(CPUState *cpu, int rt, uint64_t val)
{
    assert(rt < 31);
    WHV_REGISTER_NAME reg = WHvArm64RegisterX0 + rt;
    WHV_REGISTER_VALUE reg_val = {.Reg64 = val};

    whpx_set_reg(cpu, reg, reg_val);
}

static int whpx_handle_mmio(CPUState *cpu, WHV_MEMORY_ACCESS_CONTEXT *ctx)
{
    uint64_t syndrome = ctx->Syndrome;

    bool isv = syndrome & ARM_EL_ISV;
    bool iswrite = (syndrome >> 6) & 1;
    bool sse = (syndrome >> 21) & 1;
    uint32_t sas = (syndrome >> 22) & 3;
    uint32_t len = 1 << sas;
    uint32_t srt = (syndrome >> 16) & 0x1f;
    uint32_t cm = (syndrome >> 8) & 0x1;
    uint64_t val = 0;

    assert(!cm);
    assert(isv);

    if (iswrite) {
        val = whpx_get_gp_reg(cpu, srt);
        address_space_write(&address_space_memory,
                            ctx->Gpa,
                            MEMTXATTRS_UNSPECIFIED, &val, len);
    } else {
        address_space_read(&address_space_memory,
                           ctx->Gpa,
                           MEMTXATTRS_UNSPECIFIED, &val, len);
        if (sse) {
            val = sextract64(val, 0, len * 8);
        }
        whpx_set_gp_reg(cpu, srt, val);
    }

    return 0;
}

static void whpx_psci_cpu_off(ARMCPU *arm_cpu)
{
    int32_t ret = arm_set_cpu_off(arm_cpu_mp_affinity(arm_cpu));
    assert(ret == QEMU_ARM_POWERCTL_RET_SUCCESS);
}

int whpx_vcpu_run(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    AccelCPUState *vcpu = cpu->accel;
    int ret;


    g_assert(bql_locked());

    if (whpx->running_cpus++ == 0) {
        ret = whpx_first_vcpu_starting(cpu);
        if (ret != 0) {
            return ret;
        }
    }

    bql_unlock();


    cpu_exec_start(cpu);
    do {
        bool advance_pc = false;
        if (cpu->vcpu_dirty) {
            whpx_set_registers(cpu, WHPX_SET_RUNTIME_STATE);
            cpu->vcpu_dirty = false;
        }

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

        switch (vcpu->exit_ctx.ExitReason) {
        case WHvRunVpExitReasonGpaIntercept:
        case WHvRunVpExitReasonUnmappedGpa:
            advance_pc = true;

            if (vcpu->exit_ctx.MemoryAccess.Syndrome & BIT(8)) {
                error_report("WHPX: cached access to unmapped memory"
                "Pc = 0x%llx Gva = 0x%llx Gpa = 0x%llx",
                vcpu->exit_ctx.MemoryAccess.Header.Pc,
                vcpu->exit_ctx.MemoryAccess.Gpa,
                vcpu->exit_ctx.MemoryAccess.Gva);
                break;
            }

            ret = whpx_handle_mmio(cpu, &vcpu->exit_ctx.MemoryAccess);
            break;
        case WHvRunVpExitReasonCanceled:
            cpu->exception_index = EXCP_INTERRUPT;
            ret = 1;
            break;
        case WHvRunVpExitReasonArm64Reset:
            switch (vcpu->exit_ctx.Arm64Reset.ResetType) {
            case WHvArm64ResetTypePowerOff:
                qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
                break;
            case WHvArm64ResetTypeReboot:
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
                break;
            default:
                g_assert_not_reached();
            }
            bql_lock();
            if (arm_cpu->power_state != PSCI_OFF) {
                whpx_psci_cpu_off(arm_cpu);
            }
            /* Partition-wide reset, to reset state for reboots to succeed. */
            whp_dispatch.WHvResetPartition(whpx->partition);
            bql_unlock();
            break;
        case WHvRunVpExitReasonNone:
        case WHvRunVpExitReasonUnrecoverableException:
        case WHvRunVpExitReasonInvalidVpRegisterValue:
        case WHvRunVpExitReasonUnsupportedFeature:
        default:
            error_report("WHPX: Unexpected VP exit code 0x%08x",
                         vcpu->exit_ctx.ExitReason);
            whpx_get_registers(cpu);
            bql_lock();
            qemu_system_guest_panicked(cpu_get_crash_info(cpu));
            bql_unlock();
            break;
        }
        if (advance_pc) {
            WHV_REGISTER_VALUE pc;

            flush_cpu_state(cpu);
            pc.Reg64 = vcpu->exit_ctx.MemoryAccess.Header.Pc + 4;
            whpx_set_reg(cpu, WHvArm64RegisterPc, pc);
        }
    } while (!ret);

    cpu_exec_end(cpu);

    bql_lock();
    current_cpu = cpu;

    if (--whpx->running_cpus == 0) {
        whpx_last_vcpu_stopping(cpu);
    }

    qatomic_set(&cpu->exit_request, false);

    return ret < 0;
}

static void clean_whv_register_value(WHV_REGISTER_VALUE *val)
{
    memset(val, 0, sizeof(WHV_REGISTER_VALUE));
}

void whpx_get_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    WHV_REGISTER_VALUE val;
    int i;

    for (i = 0; i < ARRAY_SIZE(whpx_reg_match); i++) {
        whpx_get_reg(cpu, whpx_reg_match[i].reg, &val);
        *(uint64_t *)((char *)env + whpx_reg_match[i].offset) = val.Reg64;
    }

    for (i = 0; i < ARRAY_SIZE(whpx_fpreg_match); i++) {
        whpx_get_reg(cpu, whpx_fpreg_match[i].reg, &val);
        memcpy((char *)env + whpx_fpreg_match[i].offset, &val, sizeof(val.Reg128));
    }

    whpx_get_reg(cpu, WHvArm64RegisterPc, &val);
    env->pc = val.Reg64;

    whpx_get_reg(cpu, WHvArm64RegisterFpcr, &val);
    vfp_set_fpcr(env, val.Reg32);

    whpx_get_reg(cpu, WHvArm64RegisterFpsr, &val);
    vfp_set_fpsr(env, val.Reg32);

    whpx_get_reg(cpu, WHvArm64RegisterPstate, &val);
    pstate_write(env, val.Reg32);

    for (i = 0; i < ARRAY_SIZE(whpx_sreg_match); i++) {
        if (whpx_sreg_match[i].cp_idx == -1) {
            continue;
        }

        if (whpx_sreg_match[i].global) {
            /* WHP disallows us from accessing global regs as a vCPU */
            whpx_get_global_reg(whpx_sreg_match[i].reg, &val);
        } else {
            whpx_get_reg(cpu, whpx_sreg_match[i].reg, &val);
        }
        arm_cpu->cpreg_values[whpx_sreg_match[i].cp_idx] = val.Reg64;
    }

    assert(write_list_to_cpustate(arm_cpu));
    aarch64_restore_sp(env, arm_current_el(env));
}

void whpx_set_registers(CPUState *cpu, int level)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    WHV_REGISTER_VALUE val;
    clean_whv_register_value(&val);
    int i;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    for (i = 0; i < ARRAY_SIZE(whpx_reg_match); i++) {
        val.Reg64 = *(uint64_t *)((char *)env + whpx_reg_match[i].offset);
        whpx_set_reg(cpu, whpx_reg_match[i].reg, val);
    }

    for (i = 0; i < ARRAY_SIZE(whpx_fpreg_match); i++) {
        memcpy(&val.Reg128, (char *)env + whpx_fpreg_match[i].offset, sizeof(val.Reg128));
        whpx_set_reg(cpu, whpx_fpreg_match[i].reg, val);
    }

    clean_whv_register_value(&val);
    val.Reg64 = env->pc;
    whpx_set_reg(cpu, WHvArm64RegisterPc, val);

    clean_whv_register_value(&val);
    val.Reg32 = vfp_get_fpcr(env);
    whpx_set_reg(cpu, WHvArm64RegisterFpcr, val);
    val.Reg32 = vfp_get_fpsr(env);
    whpx_set_reg(cpu, WHvArm64RegisterFpsr, val);
    val.Reg32 = pstate_read(env);
    whpx_set_reg(cpu, WHvArm64RegisterPstate, val);

    aarch64_save_sp(env, arm_current_el(env));

    assert(write_cpustate_to_list(arm_cpu, false));

    /* Currently set global regs every time. */
    for (i = 0; i < ARRAY_SIZE(whpx_sreg_match); i++) {
        if (whpx_sreg_match[i].cp_idx == -1) {
            continue;
        }

        val.Reg64 = arm_cpu->cpreg_values[whpx_sreg_match[i].cp_idx];
        if (whpx_sreg_match[i].global) {
            /* WHP disallows us from accessing global regs as a vCPU */
            whpx_set_global_reg(whpx_sreg_match[i].reg, val);
        } else {
            whpx_set_reg(cpu, whpx_sreg_match[i].reg, val);
        }
    }
}

static uint32_t max_vcpu_index;

static void whpx_cpu_update_state(void *opaque, bool running, RunState state)
{
}

uint32_t whpx_arm_get_ipa_bit_size(void)
{
    WHV_CAPABILITY whpx_cap;
    UINT32 whpx_cap_size;
    HRESULT hr;
    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodePhysicalAddressWidth, &whpx_cap,
        sizeof(whpx_cap), &whpx_cap_size);
    if (FAILED(hr)) {
        error_report("WHPX: failed to get supported "
             "physical address width, hr=%08lx", hr);
    }

    /*
     * We clamp any IPA size we want to back the VM with to a valid PARange
     * value so the guest doesn't try and map memory outside of the valid range.
     * This logic just clamps the passed in IPA bit size to the first valid
     * PARange value <= to it.
     */
    return round_down_to_parange_bit_size(whpx_cap.PhysicalAddressWidth);
}

static void clamp_id_aa64mmfr0_parange_to_ipa_size(ARMISARegisters *isar)
{
    uint32_t ipa_size = whpx_arm_get_ipa_bit_size();
    uint64_t id_aa64mmfr0;

    /* Clamp down the PARange to the IPA size the kernel supports. */
    uint8_t index = round_down_to_parange_index(ipa_size);
    id_aa64mmfr0 = GET_IDREG(isar, ID_AA64MMFR0);
    id_aa64mmfr0 = (id_aa64mmfr0 & ~R_ID_AA64MMFR0_PARANGE_MASK) | index;
    SET_IDREG(isar, ID_AA64MMFR0, id_aa64mmfr0);
}

static uint64_t whpx_read_midr(void)
{
    HKEY key;
    uint64_t midr_el1;
    DWORD size = sizeof(midr_el1);
    const char *path = "Hardware\\Description\\System\\CentralProcessor\\0\\";
    assert(!RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key));
    assert(!RegGetValueA(key, NULL, "CP 4000", RRF_RT_REG_QWORD, NULL, &midr_el1, &size));
    RegCloseKey(key);
    return midr_el1;
}

static bool whpx_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf)
{
    const struct isar_regs {
        WHV_REGISTER_NAME reg;
        uint64_t *val;
    } regs[] = {
        { WHvArm64RegisterIdAa64Pfr0El1, &ahcf->isar.idregs[ID_AA64PFR0_EL1_IDX] },
        { WHvArm64RegisterIdAa64Pfr1El1, &ahcf->isar.idregs[ID_AA64PFR1_EL1_IDX] },
        { WHvArm64RegisterIdAa64Dfr0El1, &ahcf->isar.idregs[ID_AA64DFR0_EL1_IDX] },
        { WHvArm64RegisterIdAa64Dfr1El1 , &ahcf->isar.idregs[ID_AA64DFR1_EL1_IDX] },
        { WHvArm64RegisterIdAa64Isar0El1, &ahcf->isar.idregs[ID_AA64ISAR0_EL1_IDX] },
        { WHvArm64RegisterIdAa64Isar1El1, &ahcf->isar.idregs[ID_AA64ISAR1_EL1_IDX] },
        { WHvArm64RegisterIdAa64Isar2El1, &ahcf->isar.idregs[ID_AA64ISAR2_EL1_IDX] },
        { WHvArm64RegisterIdAa64Mmfr0El1, &ahcf->isar.idregs[ID_AA64MMFR0_EL1_IDX] },
        { WHvArm64RegisterIdAa64Mmfr1El1, &ahcf->isar.idregs[ID_AA64MMFR1_EL1_IDX] },
        { WHvArm64RegisterIdAa64Mmfr2El1, &ahcf->isar.idregs[ID_AA64MMFR2_EL1_IDX] },
        { WHvArm64RegisterIdAa64Mmfr3El1, &ahcf->isar.idregs[ID_AA64MMFR2_EL1_IDX] }
    };

    int i;
    WHV_REGISTER_VALUE val;

    ahcf->dtb_compatible = "arm,armv8";
    ahcf->features = (1ULL << ARM_FEATURE_V8) |
                     (1ULL << ARM_FEATURE_NEON) |
                     (1ULL << ARM_FEATURE_AARCH64) |
                     (1ULL << ARM_FEATURE_PMU) |
                     (1ULL << ARM_FEATURE_GENERIC_TIMER);

    for (i = 0; i < ARRAY_SIZE(regs); i++) {
        clean_whv_register_value(&val);
        whpx_get_global_reg(regs[i].reg, &val);
        *regs[i].val = val.Reg64;
    }

    /*
     * MIDR_EL1 is not a global register on WHPX
     * As such, read the CPU0 from the registry to get a consistent value.
     * Otherwise, on heterogenous systems, you'll get variance between CPUs.
     */
    ahcf->midr = whpx_read_midr();

    clamp_id_aa64mmfr0_parange_to_ipa_size(&ahcf->isar);

    /*
     * Disable SVE, which is not supported by QEMU whpx yet.
     * Work needed for SVE support:
     * - SVE state save/restore
     * - any potentially needed VL management
     * Also disable SME at the same time. (not currently supported by Hyper-V)
     */
    SET_IDREG(&ahcf->isar, ID_AA64PFR0,
              GET_IDREG(&ahcf->isar, ID_AA64PFR0) & ~R_ID_AA64PFR0_SVE_MASK);

    SET_IDREG(&ahcf->isar, ID_AA64PFR1,
              GET_IDREG(&ahcf->isar, ID_AA64PFR1) & ~R_ID_AA64PFR1_SME_MASK);

    return true;
}

void whpx_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    if (!arm_host_cpu_features.dtb_compatible) {
        if (!whpx_enabled() ||
            !whpx_arm_get_host_cpu_features(&arm_host_cpu_features)) {
            /*
             * We can't report this error yet, so flag that we need to
             * in arm_cpu_realizefn().
             */
            cpu->host_cpu_probe_failed = true;
            return;
        }
    }

    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar = arm_host_cpu_features.isar;
    cpu->env.features = arm_host_cpu_features.features;
    cpu->midr = arm_host_cpu_features.midr;
    cpu->reset_sctlr = arm_host_cpu_features.reset_sctlr;
}

int whpx_init_vcpu(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    uint32_t sregs_match_len = ARRAY_SIZE(whpx_sreg_match);
    uint32_t sregs_cnt = 0;
    WHV_REGISTER_VALUE val;
    int i;

    hr = whp_dispatch.WHvCreateVirtualProcessor(
        whpx->partition, cpu->cpu_index, 0);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to create a virtual processor,"
                     " hr=%08lx", hr);
        return -EINVAL;
    }

    /* Assumption that CNTFRQ_EL0 is the same between the VMM and the partition. */
    asm volatile("mrs %0, cntfrq_el0" : "=r"(arm_cpu->gt_cntfrq_hz));

    cpu->vcpu_dirty = true;
    cpu->accel = g_new0(AccelCPUState, 1);
    max_vcpu_index = MAX(max_vcpu_index, cpu->cpu_index);
    qemu_add_vm_change_state_handler(whpx_cpu_update_state, env);

    env->aarch64 = true;

    /* Allocate enough space for our sysreg sync */
    arm_cpu->cpreg_indexes = g_renew(uint64_t, arm_cpu->cpreg_indexes,
                                     sregs_match_len);
    arm_cpu->cpreg_values = g_renew(uint64_t, arm_cpu->cpreg_values,
                                    sregs_match_len);
    arm_cpu->cpreg_vmstate_indexes = g_renew(uint64_t,
                                             arm_cpu->cpreg_vmstate_indexes,
                                             sregs_match_len);
    arm_cpu->cpreg_vmstate_values = g_renew(uint64_t,
                                            arm_cpu->cpreg_vmstate_values,
                                            sregs_match_len);

    memset(arm_cpu->cpreg_values, 0, sregs_match_len * sizeof(uint64_t));

    /* Populate cp list for all known sysregs */
    for (i = 0; i < sregs_match_len; i++) {
        const ARMCPRegInfo *ri;
        uint32_t key = whpx_sreg_match[i].key;

        ri = get_arm_cp_reginfo(arm_cpu->cp_regs, key);
        if (ri) {
            assert(!(ri->type & ARM_CP_NO_RAW));
            whpx_sreg_match[i].cp_idx = sregs_cnt;
            arm_cpu->cpreg_indexes[sregs_cnt++] = cpreg_to_kvm_id(key);
        } else {
            whpx_sreg_match[i].cp_idx = -1;
        }
    }
    arm_cpu->cpreg_array_len = sregs_cnt;
    arm_cpu->cpreg_vmstate_array_len = sregs_cnt;

    assert(write_cpustate_to_list(arm_cpu, false));

    /* Set CP_NO_RAW system registers on init */
    val.Reg64 = arm_cpu->midr;
    whpx_set_reg(cpu, WHvArm64RegisterMidrEl1,
                              val);

    clean_whv_register_value(&val);

    val.Reg64 = deposit64(arm_cpu->mp_affinity, 31, 1, 1 /* RES1 */);
    whpx_set_reg(cpu, WHvArm64RegisterMpidrEl1, val);

    clamp_id_aa64mmfr0_parange_to_ipa_size(&arm_cpu->isar);
    return 0;
}

void whpx_cpu_instance_init(CPUState *cs)
{
}

int whpx_accel_init(AccelState *as, MachineState *ms)
{
    struct whpx_state *whpx;
    int ret;
    HRESULT hr;
    WHV_CAPABILITY whpx_cap;
    UINT32 whpx_cap_size;
    WHV_PARTITION_PROPERTY prop;
    WHV_CAPABILITY_FEATURES features;
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    int pa_range = 0;

    whpx = &whpx_global;
    /* on arm64 Windows Hypervisor Platform, vGICv3 always used */
    whpx_irqchip_in_kernel = true;

    if (!init_whp_dispatch()) {
        ret = -ENOSYS;
        goto error;
    }

    if (mc->get_physical_address_range) {
        pa_range = mc->get_physical_address_range(ms,
            whpx_arm_get_ipa_bit_size(), whpx_arm_get_ipa_bit_size());
        if (pa_range < 0) {
            return -EINVAL;
        }
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

    memset(&features, 0, sizeof(features));
    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodeFeatures, &features, sizeof(features), NULL);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to query capabilities, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    if (!features.Arm64Support) {
        error_report("WHPX: host OS exposing pre-release WHPX implementation. "
            "Please update your operating system to at least build 26100.3915");
        ret = -EINVAL;
        goto error;
    }

    hr = whp_dispatch.WHvCreatePartition(&whpx->partition);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to create partition, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    memset(&prop, 0, sizeof(prop));
    prop.ProcessorCount = ms->smp.cpus;
    hr = whp_dispatch.WHvSetPartitionProperty(
        whpx->partition,
        WHvPartitionPropertyCodeProcessorCount,
        &prop,
        sizeof(prop));

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set partition processor count to %u,"
                     " hr=%08lx", prop.ProcessorCount, hr);
        ret = -EINVAL;
        goto error;
    }

    if (!whpx->kernel_irqchip_allowed) {
        error_report("WHPX: on Arm, only kernel-irqchip=on is currently supported");
        ret = -EINVAL;
        goto error;
    }

    memset(&prop, 0, sizeof(prop));

    /*
     * The only currently supported configuration for the interrupt
     * controller is kernel-irqchip=on,gic-version=3, with the `virt`
     * machine.
     *
     * Initialising the vGIC here because it needs to be done prior to
     * WHvSetupPartition.
     */

    WHV_ARM64_IC_PARAMETERS ic_params = {
        .EmulationMode = WHvArm64IcEmulationModeGicV3,
        .GicV3Parameters = {
            .GicdBaseAddress = 0x08000000,
            .GitsTranslaterBaseAddress = 0x08080000,
            .GicLpiIntIdBits = 0,
            .GicPpiPerformanceMonitorsInterrupt = VIRTUAL_PMU_IRQ,
            .GicPpiOverflowInterruptFromCntv = ARCH_TIMER_VIRT_IRQ
        }
    };
    prop.Arm64IcParameters = ic_params;

    hr = whp_dispatch.WHvSetPartitionProperty(
            whpx->partition,
            WHvPartitionPropertyCodeArm64IcParameters,
            &prop,
            sizeof(prop));
    if (FAILED(hr)) {
        error_report("WHPX: Failed to enable GICv3 interrupt controller, hr=%08lx", hr);
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

    return 0;

error:
    if (whpx->partition != NULL) {
        whp_dispatch.WHvDeletePartition(whpx->partition);
        whpx->partition = NULL;
    }

    return ret;
}
