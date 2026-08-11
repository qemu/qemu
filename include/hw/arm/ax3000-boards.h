/*
 * Axiado Boards
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AXIADO_BOARD_H
#define AXIADO_BOARD_H

#include "hw/core/boards.h"
#include "hw/arm/ax3000-soc.h"

#define TYPE_AX3000_MACHINE       MACHINE_TYPE_NAME("ax3000")
OBJECT_DECLARE_TYPE(Ax3000MachineState, Ax3000MachineClass, AX3000_MACHINE)

typedef struct Ax3000MachineState {
    MachineState parent;

    Ax3000SoCState *soc;
} Ax3000MachineState;

typedef struct Ax3000MachineClass {
    MachineClass parent;

} Ax3000MachineClass;
#endif
