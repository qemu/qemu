/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGON_HEXAGON_HLX_H
#define HW_HEXAGON_HEXAGON_HLX_H

#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "target/hexagon/hlx/hlx.h"

#define TYPE_HEXAGON_HLX "hexagon-hlx"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonHLXState, HEXAGON_HLX)

typedef struct HexagonHLXContext {
    HLXVector LRegs[NUM_LREGS] QEMU_ALIGNED(16);
} HexagonHLXContext;

struct HexagonHLXState {
    SysBusDevice parent_obj;

    uint32_t contexts_bitmap;
    int32_t num_contexts;
    HexagonHLXContext *contexts;
};

HexagonHLXContext *hexagon_hlx_get_context(HexagonHLXState *s, uint32_t xa3);

void hexagon_hlx_reset_all(HexagonHLXState *s);

#endif
