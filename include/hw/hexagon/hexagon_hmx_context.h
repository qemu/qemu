/*
 * Hexagon HMX Extension Context QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HMX_CONTEXT_H
#define HEXAGON_HMX_CONTEXT_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "target/hexagon/hmx_state.h"

#define TYPE_HEXAGON_HMX_CONTEXT "hexagon-hmx-context"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonHMXContextState, HEXAGON_HMX_CONTEXT)

struct HexagonHMXContextState {
    SysBusDevice parent_obj;

    HmxState regs;
};

#endif /* HEXAGON_HMX_CONTEXT_H */
