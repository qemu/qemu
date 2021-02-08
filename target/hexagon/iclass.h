/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_ICLASS_H
#define HEXAGON_ICLASS_H

#include "attribs.h"

#define ICLASS_FROM_TYPE(TYPE) ICLASS_##TYPE

enum {

#define DEF_PP_ICLASS32(TYPE, SLOTS, UNITS)    ICLASS_FROM_TYPE(TYPE),
#define DEF_EE_ICLASS32(TYPE, SLOTS, UNITS)    ICLASS_FROM_TYPE(TYPE),
#include "imported/iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

    ICLASS_FROM_TYPE(COPROC_VX),
    ICLASS_FROM_TYPE(COPROC_VMEM),
    NUM_ICLASSES
};

typedef enum {
    SLOTS_0          = (1 << 0),
    SLOTS_1          = (1 << 1),
    SLOTS_2          = (1 << 2),
    SLOTS_3          = (1 << 3),
    SLOTS_01         = SLOTS_0 | SLOTS_1,
    SLOTS_23         = SLOTS_2 | SLOTS_3,
    SLOTS_0123       = SLOTS_0 | SLOTS_1 | SLOTS_2 | SLOTS_3,
} SlotMask;

SlotMask find_iclass_slots(Opcode opcode, int itype);

#endif
