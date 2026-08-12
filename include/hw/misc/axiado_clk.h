/*
 * Axiado AX3000 Clock Control
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AXIADO_AX3000_CLK_H
#define AXIADO_AX3000_CLK_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AX3000_CLK "ax3000-clk"
OBJECT_DECLARE_SIMPLE_TYPE(Ax3000ClkState, AX3000_CLK)

#define AX3000_CLK_PLL_CTRL_SIZE    0x1000

typedef struct Ax3000ClkState {
    SysBusDevice        parent;

    MemoryRegion        pll_ctrl;
} Ax3000ClkState;

#endif /* AXIADO_AX3000_CLK_H */
