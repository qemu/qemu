/*
 * Infineon TC4x Board emulation.
 *
 * Copyright (c) 2024 QEMU contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef TC4X_BOARD_H
#define TC4X_BOARD_H

#include "hw/boards.h"
#include "hw/tricore/tc4x_soc.h"
#include "qom/object.h"

#define TYPE_TC4X_MACHINE MACHINE_TYPE_NAME("tc4x")
OBJECT_DECLARE_TYPE(TC4xMachineState, TC4xMachineClass, TC4X_MACHINE)

typedef struct TC4xMachineState {
    MachineState parent_obj;

    TC4xSoCState soc;
} TC4xMachineState;

typedef struct TC4xMachineClass {
    MachineClass parent_class;

    const char *soc_name;
} TC4xMachineClass;

#endif /* TC4X_BOARD_H */
