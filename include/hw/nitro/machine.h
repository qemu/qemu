/*
 * Nitro Enclaves (accel) machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NITRO_MACHINE_H
#define HW_NITRO_MACHINE_H

#include "hw/core/boards.h"
#include "qom/object.h"

#define TYPE_NITRO_MACHINE MACHINE_TYPE_NAME("nitro")
OBJECT_DECLARE_SIMPLE_TYPE(NitroMachineState, NITRO_MACHINE)

struct NitroMachineState {
    MachineState parent;
};

#endif /* HW_NITRO_MACHINE_H */
