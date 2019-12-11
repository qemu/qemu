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

#ifndef OPCODES_H
#define OPCODES_H

#include "attribs.h"
#include "insn.h"

typedef enum {
#define OPCODE(IID) IID
#include "opcodes_def_generated.h"
    XX_LAST_OPCODE
#undef OPCODE
} opcode_t;

extern const char *opcode_names[];

extern const char *opcode_reginfo[];
extern const char *opcode_rregs[];
extern const char *opcode_wregs[];

typedef struct {
    const char * const encoding;
    size4u_t vals;
    size4u_t dep_vals;
    size1u_t is_ee:1;
} opcode_encoding_t;

extern opcode_encoding_t opcode_encodings[XX_LAST_OPCODE];

extern semantic_insn_t opcode_genptr[];

extern size4u_t
    opcode_attribs[XX_LAST_OPCODE][(A_ZZ_LASTATTRIB / ATTRIB_WIDTH) + 1];

extern void opcode_init(void);

extern int opcode_which_immediate_is_extended(opcode_t opcode);

#endif
