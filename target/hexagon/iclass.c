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

#include <stddef.h>
#include <stdio.h>
#include "qemu/osdep.h"
#include "iclass.h"

typedef struct {
    const char * const slots;
} iclass_info_t;

static const iclass_info_t iclass_info[] = {

#define DEF_EE_ICLASS32(TYPE, SLOTS, UNITS)    /* nothing */
#define DEF_PP_ICLASS32(TYPE, SLOTS, UNITS) \
    [ICLASS_FROM_TYPE(TYPE)] = { .slots = #SLOTS },

#include "imported/iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

#define DEF_PP_ICLASS32(TYPE, SLOTS, UNITS)    /* nothing */
#define DEF_EE_ICLASS32(TYPE, SLOTS, UNITS) \
    [ICLASS_FROM_TYPE(TYPE)] = { .slots = #SLOTS },

#include "imported/iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32

    {0}
};

const char *find_iclass_slots(opcode_t opcode, int itype)
{
    /* There are some exceptions to what the iclass dictates */
    if (GET_ATTRIB(opcode, A_ICOP)) {
        return "2";
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT0ONLY)) {
        return "0";
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT1ONLY)) {
        return "1";
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT2ONLY)) {
        return "2";
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT3ONLY)) {
        return "3";
    } else if (GET_ATTRIB(opcode, A_COF) &&
               GET_ATTRIB(opcode, A_INDIRECT) &&
               !GET_ATTRIB(opcode, A_MEMLIKE) &&
               !GET_ATTRIB(opcode, A_MEMLIKE_PACKET_RULES)) {
        return "2";
    } else if (GET_ATTRIB(opcode, A_RESTRICT_NOSLOT1)) {
        return "0";
    } else if ((opcode == J2_trap0) || (opcode == J2_trap1) ||
               (opcode == Y2_isync) || (opcode == J2_rte) ||
               (opcode == J2_pause) || (opcode == J4_hintjumpr)) {
        return "2";
    } else if ((itype == ICLASS_V2LDST) && (GET_ATTRIB(opcode, A_STORE))) {
        return "01";
    } else if ((itype == ICLASS_V2LDST) && (!GET_ATTRIB(opcode, A_STORE))) {
        return "01";
    } else if (GET_ATTRIB(opcode, A_CRSLOT23)) {
        return "23";
    } else if (GET_ATTRIB(opcode, A_RESTRICT_PREFERSLOT0)) {
        return "0";
    } else if (GET_ATTRIB(opcode, A_SUBINSN)) {
        return "01";
    } else if (GET_ATTRIB(opcode, A_CALL)) {
        return "23";
    } else if ((opcode == J4_jumpseti) || (opcode == J4_jumpsetr)) {
        return "23";
    } else if (GET_ATTRIB(opcode, A_EXTENSION) && GET_ATTRIB(opcode, A_CVI)) {
        /* CVI EXTENSIONS */
        if (GET_ATTRIB(opcode, A_CVI_VM)) {
            return "01";
        } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT2ONLY)) {
            return "2";
        } else if (GET_ATTRIB(opcode, A_CVI_SLOT23)) {
            return "23";
        } else if (GET_ATTRIB(opcode, A_CVI_VX)) {
            return "23";
        } else if (GET_ATTRIB(opcode, A_CVI_VX_DV)) {
            return "23";
        } else if (GET_ATTRIB(opcode, A_CVI_VS_VX)) {
            return "23";
        } else if (GET_ATTRIB(opcode, A_MEMLIKE)) {
            return "01";
        } else {
            return "0123";
        }
    } else if (GET_ATTRIB(opcode, A_16BIT)) {
        if (GET_ATTRIB(opcode, A_LOAD) || GET_ATTRIB(opcode, A_STORE)) {
            return "01";
        } else {
            return "0123";
        }
    } else {
        return iclass_info[itype].slots;
    }
}

