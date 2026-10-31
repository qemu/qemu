/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "migration/cpu.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "macros.h"
#include "hlx/hlx.h"
#include "hw/hexagon/hexagon_hlx.h"

static int hexagon_cpu_post_load(void *opaque, int version_id)
{
    HexagonCPU *cpu = opaque;
    CPUHexagonState *env = &cpu->env;
    uint32_t ccr;
    bool xe3;
    uint32_t xa3;

    hexagon_hvx_select_context(env, env->t_sreg[HEX_SREG_SSR]);
    if (!cpu->hlx) {
        env->hlx_ctx = NULL;
        return 0;
    }

    ccr = env->t_sreg[HEX_SREG_CCR];
    xe3 = GET_FIELD(CCR_XE3, ccr);
    xa3 = GET_FIELD(CCR_XA3, ccr);
    env->hlx_ctx = xe3 ? hexagon_hlx_get_context(cpu->hlx, xa3) : NULL;
    return 0;
}

static int get_hlxvector(QEMUFile *f, void *pv, size_t size,
                         const VMStateField *field)
{
    HLXVector *v = pv;
    for (int i = 0; i < HLX_VEC_SIZE_BYTES / 8; i++) {
        v->ud[i] = qemu_get_be64(f);
    }
    return 0;
}

static int put_hlxvector(QEMUFile *f, void *pv, size_t size,
                         const VMStateField *field, JSONWriter *vmdesc)
{
    HLXVector *v = pv;
    for (int i = 0; i < HLX_VEC_SIZE_BYTES / 8; i++) {
        qemu_put_be64(f, v->ud[i]);
    }
    return 0;
}

static const VMStateInfo vmstate_info_hlxvector = {
    .name = "hlxvector",
    .get  = get_hlxvector,
    .put  = put_hlxvector,
};

#define VMSTATE_HLXVECTOR_ARRAY_V(_f, _s, _n, _v) \
    VMSTATE_SUB_ARRAY(_f, _s, 0, _n, _v, vmstate_info_hlxvector, HLXVector)
const VMStateDescription vmstate_hexagon_cpu = {
    .name = "cpu",
    .version_id = 4,
    .minimum_version_id = 2,
    .post_load = hexagon_cpu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.gpr, HexagonCPU, TOTAL_PER_THREAD_REGS),
        VMSTATE_UINT32_ARRAY(env.pred, HexagonCPU, NUM_PREGS),
        VMSTATE_UINT32_ARRAY(env.t_sreg, HexagonCPU, NUM_SREGS),
        VMSTATE_UINT32_ARRAY(env.greg, HexagonCPU, NUM_GREGS),
        VMSTATE_UINT32(env.tlb_lock_state, HexagonCPU),
        VMSTATE_UINT32(env.k0_lock_state, HexagonCPU),
        VMSTATE_UINT32(env.tlb_lock_count, HexagonCPU),
        VMSTATE_UINT32(env.k0_lock_count, HexagonCPU),
        VMSTATE_UINT32(env.threadId, HexagonCPU),
        VMSTATE_UINT32(env.cause_code, HexagonCPU),
        VMSTATE_UNUSED(sizeof(uint64_t)),
        VMSTATE_UINT32(env.imprecise_exception, HexagonCPU),
        VMSTATE_UINT32_V(env.cfi_active, HexagonCPU, 3),
        VMSTATE_HLXVECTOR_ARRAY_V(env.future_LRegs, HexagonCPU, NUM_LREGS, 4),

        VMSTATE_END_OF_LIST()
    },
};
