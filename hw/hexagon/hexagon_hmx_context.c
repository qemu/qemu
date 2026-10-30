/*
 * Hexagon HMX Extension Context QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon_hmx_context.h"
#include "migration/vmstate.h"

static void hexagon_hmx_context_reset_hold(Object *obj, ResetType type)
{
    HexagonHMXContextState *s = HEXAGON_HMX_CONTEXT(obj);

    memset(&s->regs, 0, sizeof(s->regs));
}

static const VMStateDescription vmstate_hmx_acc_fxp = {
    .name = "hexagon_hmx_acc_fxp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_2DARRAY(data, HmxAccFxp, HMX_SPATIAL_DIM_FXP,
                        HMX_OUTPUT_CHANNELS, 0, vmstate_info_int32, int32_t),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_acc_fp = {
    .name = "hexagon_hmx_acc_fp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_2DARRAY(data, HmxAccFp, HMX_SPATIAL_DIM_FP,
                               HMX_OUTPUT_CHANNELS),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_acc_set = {
    .name = "hexagon_hmx_acc_set",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(fxp_primary, HmxAccSet, 0, vmstate_hmx_acc_fxp,
                       HmxAccFxp),
        VMSTATE_STRUCT(fp_primary, HmxAccSet, 0, vmstate_hmx_acc_fp,
                       HmxAccFp),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_cvt_fxp = {
    .name = "hexagon_hmx_cvt_fxp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16_2DARRAY(data, HmxCvtStateFxp, HMX_SPATIAL_DIM_FXP,
                               HMX_OUTPUT_CHANNELS),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_cvt_fp = {
    .name = "hexagon_hmx_cvt_fp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16_2DARRAY(data, HmxCvtStateFp, HMX_SPATIAL_DIM_FP,
                               HMX_OUTPUT_CHANNELS),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_regs = {
    .name = "hexagon_hmx_regs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(acc, HmxState, HMX_NUM_ACC_SETS, 0,
                             vmstate_hmx_acc_set, HmxAccSet),
        VMSTATE_STRUCT_ARRAY(cvt_fxp, HmxState, HMX_NUM_CVT_AGES, 0,
                             vmstate_hmx_cvt_fxp, HmxCvtStateFxp),
        VMSTATE_STRUCT_ARRAY(cvt_fp, HmxState, HMX_NUM_CVT_AGES, 0,
                             vmstate_hmx_cvt_fp, HmxCvtStateFp),
        VMSTATE_STRUCT(cvt_future_fxp, HmxState, 0, vmstate_hmx_cvt_fxp,
                       HmxCvtStateFxp),
        VMSTATE_UINT64_2DARRAY(bias_raw, HmxState, HMX_NUM_BIAS_SETS,
                               HMX_OUTPUT_CHANNELS),
        VMSTATE_UINT32(current_acc_set, HmxState),
        VMSTATE_BUFFER(act_buffer, HmxState),
        VMSTATE_UINT32(act_rs, HmxState),
        VMSTATE_UINT32(act_type, HmxState),
        VMSTATE_UINT8(is_f8_odd, HmxState),
        VMSTATE_UINT8(fp8_odd_sel, HmxState),
        VMSTATE_UINT32(format_offset, HmxState),
        VMSTATE_UINT32(tile_x_mask, HmxState),
        VMSTATE_UINT32(tile_y_mask, HmxState),
        VMSTATE_UINT32(tile_x_inc, HmxState),
        VMSTATE_UINT32(tile_y_inc, HmxState),
        VMSTATE_UINT32(fx, HmxState),
        VMSTATE_UINT32(fy, HmxState),
        VMSTATE_INT32(ch_start, HmxState),
        VMSTATE_INT32(ch_stop, HmxState),
        VMSTATE_UINT32(y_start, HmxState),
        VMSTATE_UINT32(y_stop, HmxState),
        VMSTATE_UINT32(y_dilate, HmxState),
        VMSTATE_UINT32(blocks, HmxState),
        VMSTATE_INT32(dY, HmxState),
        VMSTATE_UINT32(group_conv, HmxState),
        VMSTATE_UINT32(group_size, HmxState),
        VMSTATE_UINT32(group_count, HmxState),
        VMSTATE_INT32(mac_cycle_limit, HmxState),
        VMSTATE_UINT32(cvt_fxp_pending, HmxState),
        VMSTATE_UINT32(cvt_fxp_pending_age, HmxState),
        VMSTATE_UINT32(cvt_acc_clear_pending, HmxState),
        VMSTATE_UINT32(cvt_acc_clear_set, HmxState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hexagon_hmx_context = {
    .name = "hexagon_hmx_context",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(regs, HexagonHMXContextState, 1, vmstate_hmx_regs,
                       HmxState),
        VMSTATE_END_OF_LIST()
    },
};

static void hexagon_hmx_context_class_init(ObjectClass *klass,
                                           const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = hexagon_hmx_context_reset_hold;
    dc->vmsd = &vmstate_hexagon_hmx_context;
    dc->user_creatable = false;
}

static const TypeInfo hexagon_hmx_context_info = {
    .name = TYPE_HEXAGON_HMX_CONTEXT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HexagonHMXContextState),
    .class_init = hexagon_hmx_context_class_init,
};

static void hexagon_hmx_context_register_types(void)
{
    type_register_static(&hexagon_hmx_context_info);
}

type_init(hexagon_hmx_context_register_types)
