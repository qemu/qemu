/*
 * Infineon TriBoard System emulation.
 *
 * Copyright (c) 2020 Andreas Konopik <andreas.konopik@efs-auto.de>
 * Copyright (c) 2020 David Brenken <david.brenken@efs-auto.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "qom/object.h"

#include "hw/tricore/tc27x_soc.h"

#define TYPE_TRIBOARD_MACHINE MACHINE_TYPE_NAME("triboard")
typedef struct TriBoardMachineState TriBoardMachineState;
typedef struct TriBoardMachineClass TriBoardMachineClass;
DECLARE_OBJ_CHECKERS(TriBoardMachineState, TriBoardMachineClass,
                     TRIBOARD_MACHINE, TYPE_TRIBOARD_MACHINE)


struct TriBoardMachineState {
    MachineState parent;

    TC27XSoCState tc27x_soc;
};

struct TriBoardMachineClass {
    MachineClass parent_obj;

    const char *name;
    const char *desc;
    const char *soc_name;
};
