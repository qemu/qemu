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

#ifndef HEXAGON_ATTRIBS_H
#define HEXAGON_ATTRIBS_H

#include "qemu/bitmap.h"
#include "opcodes.h"

enum {
#define DEF_ATTRIB(NAME, ...) A_##NAME,
#include "attribs_def.h.inc"
#undef DEF_ATTRIB
};

extern DECLARE_BITMAP(opcode_attribs[XX_LAST_OPCODE], A_ZZ_LASTATTRIB);

#define GET_ATTRIB(opcode, attrib) \
    test_bit(attrib, opcode_attribs[opcode])

#endif /* HEXAGON_ATTRIBS_H */
