/*
 * Hexagon HVX Extension Context QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon_hvx_context.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

static void hexagon_hvx_context_reset_hold(Object *obj, ResetType type)
{
    HexagonHVXContextState *s = HEXAGON_HVX_CONTEXT(obj);

    memset(&s->regs, 0, sizeof(s->regs));
}

/* gvec needs VRegs/QRegs 16-aligned within the struct. */
QEMU_BUILD_BUG_ON(offsetof(HexagonHVXContextState, regs) % 16 != 0);

static const VMStateDescription vmstate_mmvector = {
    .name = "hexagon_mmvector",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_UINT64_ARRAY(ud, MMVector, MAX_VEC_SIZE_BYTES / 8),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_mmqreg = {
    .name = "hexagon_mmqreg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_UINT64_ARRAY(ud, MMQReg, MAX_VEC_SIZE_BYTES / 8 / 8),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_hexagon_hvx_context = {
    .name = "hexagon_hvx_context",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_STRUCT_ARRAY(regs.VRegs, HexagonHVXContextState, NUM_VREGS,
                             1, vmstate_mmvector, MMVector),
        VMSTATE_STRUCT_ARRAY(regs.QRegs, HexagonHVXContextState, NUM_QREGS,
                             1, vmstate_mmqreg, MMQReg),
        VMSTATE_END_OF_LIST()
    }
};

static const Property hexagon_hvx_context_properties[] = {
    DEFINE_PROP_UINT32("index", HexagonHVXContextState, index, 0),
};

static void hexagon_hvx_context_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = hexagon_hvx_context_reset_hold;
    dc->vmsd = &vmstate_hexagon_hvx_context;
    dc->user_creatable = false;
    device_class_set_props(dc, hexagon_hvx_context_properties);
}

static const TypeInfo hexagon_hvx_context_info = {
    .name = TYPE_HEXAGON_HVX_CONTEXT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HexagonHVXContextState),
    .instance_align = __alignof(HexagonHVXContextState),
    .class_init = hexagon_hvx_context_class_init,
};

static void hexagon_hvx_context_register_types(void)
{
    type_register_static(&hexagon_hvx_context_info);
}

type_init(hexagon_hvx_context_register_types)
