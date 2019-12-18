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

#ifndef ATTRIBS_H
#define ATTRIBS_H 1

enum {
#define DEF_ATTRIB(NAME, ...) A_##NAME,
#include "attribs_def.h"
#undef DEF_ATTRIB
};

#define ATTRIB_WIDTH 32
#define GET_ATTRIB(opcode, attrib) \
    (((opcode_attribs[opcode][attrib / ATTRIB_WIDTH])\
    >> (attrib % ATTRIB_WIDTH)) & 0x1)

#endif /* ATTRIBS_H */
