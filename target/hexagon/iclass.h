/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef ICLASS_H
#define ICLASS_H

#include "opcodes.h"

#define ICLASS_FROM_TYPE(TYPE) ICLASS_##TYPE

typedef enum {

#define DEF_PP_ICLASS32(TYPE, SLOTS, UNITS)    ICLASS_FROM_TYPE(TYPE),
#define DEF_EE_ICLASS32(TYPE, SLOTS, UNITS)    /* nothing */
#include "imported/iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

#define DEF_EE_ICLASS32(TYPE, SLOTS, UNITS)    ICLASS_FROM_TYPE(TYPE),
#define DEF_PP_ICLASS32(TYPE, SLOTS, UNITS)    /* nothing */
#include "imported/iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

    ICLASS_FROM_TYPE(COPROC_VX),
    ICLASS_FROM_TYPE(COPROC_VMEM),
    NUM_ICLASSES
} iclass_t;

extern const char *find_iclass_slots(opcode_t opcode, int itype);

#endif
